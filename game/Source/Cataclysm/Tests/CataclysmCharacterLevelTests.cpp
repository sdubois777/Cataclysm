// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmClassStats.h"
#include "Character/CataclysmExperience.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for a character's level living on the player state.
 *
 * REGISTERED UNDER `Cataclysm.CharacterLevel`, which is not this file's name. A
 * run narrowed with the wrong prefix reports "0 tests performed" and reads
 * exactly like a passing guard.
 *
 * WHAT IS BEING CHECKED THAT `Cataclysm.Experience` DOES NOT. Those tests are
 * about the curve as arithmetic. These are about the character: that the level
 * survives on the player state rather than the pawn, that
 * `Cataclysm.PlayerLevel` still decides the STARTING level so nothing that
 * already sets it changed meaning, that a level gained overrides it, and that
 * attribute points follow the level that was earned rather than the console
 * variable.
 *
 * THE CONSOLE VARIABLE IS PUT BACK IN EVERY TEST THAT MOVES IT. It is global,
 * the tests share a process, and `docs/DECISIONS.md` records that a C++ test in
 * this project has already been found to depend on which tests ran before it.
 */

namespace CataclysmCharacterLevelTest
{
	/**
	 * A player state spawned into a world, the way the attribute allocation
	 * tests do it. SPAWNED RATHER THAN `NewObject`, because a player state is an
	 * actor and an actor made outside a world has not run the engine's own
	 * initialisation.
	 */
	ACataclysmPlayerState* Spawn(UWorld* World)
	{
		return World ? World->SpawnActor<ACataclysmPlayerState>() : nullptr;
	}

	/** Sets `Cataclysm.PlayerLevel` and puts it back when it goes out of scope. */
	struct FStartingLevel
	{
		explicit FStartingLevel(int32 Level)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(
				TEXT("Cataclysm.PlayerLevel"));
			if (Variable)
			{
				Previous = Variable->GetInt();
				Variable->Set(Level, ECVF_SetByCode);
			}
		}

		~FStartingLevel()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByCode);
			}
		}

		bool IsUsable() const { return Variable != nullptr; }

		IConsoleVariable* Variable = nullptr;
		int32 Previous = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLevelStartsFromTheConsoleVariable,
	"Cataclysm.CharacterLevel.TheStartingLevelStillComesFromTheConsoleVariable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLevelStartsFromTheConsoleVariable::RunTest(const FString&)
{
	using namespace CataclysmCharacterLevelTest;

	// THE COMPATIBILITY THAT MATTERS. Before levelling existed, three call sites
	// read `Cataclysm.PlayerLevel` and every automation test wanting a levelled
	// character set it. If a fresh character stopped answering with it, all of
	// that would silently start describing a level 1 character instead.
	FStartingLevel Starting(37);
	if (!Starting.IsUsable())
	{
		AddError(TEXT("Cataclysm.PlayerLevel does not exist, so this test cannot "
					  "check what a character starts at."));
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* State = Spawn(World);
	if (!State)
	{
		AddError(TEXT("a player state would not spawn"));
		return false;
	}
	TestEqual(TEXT("a fresh character is at the console variable's level"),
		State->GetCharacterLevel(), 37);
	TestEqual(TEXT("and has earned nothing toward the next"),
		State->GetExperienceIntoLevel(), (int64)0);
	TestEqual(TEXT("and has one attribute point for every level"),
		State->AttributePointsAvailable(), 37);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLevelGainedOverridesTheConsoleVariable,
	"Cataclysm.CharacterLevel.ALevelGainedStopsTheConsoleVariableDecidingIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLevelGainedOverridesTheConsoleVariable::RunTest(const FString&)
{
	using namespace CataclysmCharacterLevelTest;

	FStartingLevel Starting(10);
	if (!Starting.IsUsable())
	{
		AddError(TEXT("Cataclysm.PlayerLevel does not exist."));
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* State = Spawn(World);
	if (!State)
	{
		AddError(TEXT("a player state would not spawn"));
		return false;
	}
	TestEqual(TEXT("starts at 10"), State->GetCharacterLevel(), 10);

	// GRANTING NOTHING MUST NOT SETTLE THE LEVEL. A kill worth zero is a real
	// case while Enemy Score has no port, and if it fixed the character at
	// whatever the console variable said at that instant, an event that did
	// nothing would have made a lasting change. Checked by moving the console
	// variable afterwards: a character that is still unsettled follows it.
	TestEqual(TEXT("granting nothing gains nothing"),
		State->GrantExperience(0), 0);
	Starting.Variable->Set(15, ECVF_SetByCode);
	TestEqual(TEXT("and left the level free to follow the console variable"),
		State->GetCharacterLevel(), 15);
	Starting.Variable->Set(10, ECVF_SetByCode);

	const int32 Gained = State->GrantExperience(
		UCataclysmExperience::CostOfLevel(11));
	TestEqual(TEXT("one level gained"), Gained, 1);
	TestEqual(TEXT("now level 11"), State->GetCharacterLevel(), 11);

	// AND THE CONSOLE VARIABLE MOVING NO LONGER MOVES THE CHARACTER. A level
	// that was earned has to stop being at the mercy of a setting, or every
	// level gained would be undone by the next person typing into the console.
	Starting.Variable->Set(4, ECVF_SetByCode);
	TestEqual(TEXT("the earned level survives the console variable changing"),
		State->GetCharacterLevel(), 11);
	TestEqual(TEXT("and the attribute points follow the earned level"),
		State->AttributePointsAvailable(), 11);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLevelCarriesTheRemainder,
	"Cataclysm.CharacterLevel.ExperienceShortOfALevelIsKept",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLevelCarriesTheRemainder::RunTest(const FString&)
{
	using namespace CataclysmCharacterLevelTest;

	FStartingLevel Starting(1);
	if (!Starting.IsUsable())
	{
		AddError(TEXT("Cataclysm.PlayerLevel does not exist."));
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* State = Spawn(World);
	if (!State)
	{
		AddError(TEXT("a player state would not spawn"));
		return false;
	}
	const int64 ToSecond = UCataclysmExperience::CostOfLevel(2);

	// In three parts, because a kill grants a fraction of a level and the parts
	// have to add up. This is the case the whole design rests on.
	TestEqual(TEXT("a third of a level gains nothing"),
		State->GrantExperience(ToSecond / 3), 0);
	TestEqual(TEXT("another third still gains nothing"),
		State->GrantExperience(ToSecond / 3), 0);
	TestEqual(TEXT("still level 1"), State->GetCharacterLevel(), 1);
	TestEqual(TEXT("with two thirds banked"),
		State->GetExperienceIntoLevel(), (ToSecond / 3) * 2);

	TestEqual(TEXT("the last third gains the level"),
		State->GrantExperience(ToSecond), 1);
	TestEqual(TEXT("now level 2"), State->GetCharacterLevel(), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLevelRestoredFromASave,
	"Cataclysm.CharacterLevel.ASavedLevelAndProgressCanBePutBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLevelRestoredFromASave::RunTest(const FString&)
{
	using namespace CataclysmCharacterLevelTest;

	FStartingLevel Starting(20);
	if (!Starting.IsUsable())
	{
		AddError(TEXT("Cataclysm.PlayerLevel does not exist."));
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* State = Spawn(World);
	if (!State)
	{
		AddError(TEXT("a player state would not spawn"));
		return false;
	}
	State->SetLevelAndExperience(63, 12345);
	TestEqual(TEXT("the loaded level is what the character is"),
		State->GetCharacterLevel(), 63);
	TestEqual(TEXT("and so is the loaded progress"),
		State->GetExperienceIntoLevel(), (int64)12345);
	TestEqual(TEXT("attribute points follow the loaded level"),
		State->AttributePointsAvailable(), 63);

	// A RECORD HOLDS WHATEVER WAS LAST WRITTEN TO IT, including whatever a
	// future migration leaves behind, so nonsense is corrected rather than
	// trusted or refused.
	State->SetLevelAndExperience(-8, -900);
	TestEqual(TEXT("a level below 1 is clamped to 1"),
		State->GetCharacterLevel(), 1);
	TestEqual(TEXT("and negative progress becomes none"),
		State->GetExperienceIntoLevel(), (int64)0);

	State->SetLevelAndExperience(500, 0);
	TestEqual(TEXT("a level above the maximum is clamped to it"),
		State->GetCharacterLevel(), UCataclysmExperience::MaxLevel);

	// PROGRESS IS CLAMPED TO LESS THAN THE NEXT LEVEL COSTS, not merely to
	// something positive. A record saying a level 5 character has more than
	// level 6 costs describes a character that should already have levelled, and
	// leaving it would make the next single point jump a level.
	State->SetLevelAndExperience(5, UCataclysmExperience::CostOfLevel(6) * 4);
	TestEqual(TEXT("progress cannot exceed what the next level costs"),
		State->GetExperienceIntoLevel(),
		UCataclysmExperience::CostOfLevel(6) - 1);
	TestEqual(TEXT("and one more point is exactly enough to level"),
		State->GrantExperience(1), 1);
	TestEqual(TEXT("landing on level 6"), State->GetCharacterLevel(), 6);

	// AT THE MAXIMUM THE NEXT LEVEL COSTS NOTHING, so nothing is kept.
	State->SetLevelAndExperience(UCataclysmExperience::MaxLevel, 999999);
	TestEqual(TEXT("the maximum level keeps no progress"),
		State->GetExperienceIntoLevel(), (int64)0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLevelStopsAtTheMaximum,
	"Cataclysm.CharacterLevel.TheClimbEndsAtTheMaximumLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLevelStopsAtTheMaximum::RunTest(const FString&)
{
	using namespace CataclysmCharacterLevelTest;

	FStartingLevel Starting(1);
	if (!Starting.IsUsable())
	{
		AddError(TEXT("Cataclysm.PlayerLevel does not exist."));
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* State = Spawn(World);
	if (!State)
	{
		AddError(TEXT("a player state would not spawn"));
		return false;
	}

	// The whole climb in one grant, which is what the balance work says eight
	// campaigns of dungeons are worth.
	const int32 Gained = State->GrantExperience(
		UCataclysmExperience::TotalToReach(UCataclysmExperience::MaxLevel));
	TestEqual(TEXT("ninety-nine levels gained"), Gained, 99);
	TestEqual(TEXT("at the maximum level"),
		State->GetCharacterLevel(), UCataclysmExperience::MaxLevel);
	TestEqual(TEXT("and the maximum number of attribute points"),
		State->AttributePointsAvailable(), UCataclysmExperience::MaxLevel);

	TestEqual(TEXT("a further grant gains nothing"),
		State->GrantExperience(1000000000), 0);
	TestEqual(TEXT("and banks nothing"),
		State->GetExperienceIntoLevel(), (int64)0);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLevelReachesTheCharactersStats,
	"Cataclysm.CharacterLevel.GainingALevelActuallyMakesTheCharacterStronger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLevelReachesTheCharactersStats::RunTest(const FString&)
{
	using namespace CataclysmCharacterLevelTest;

	// THE TEST THE WHOLE FEATURE EXISTS FOR, and the one whose absence would be
	// hardest to notice. Every other test here checks that a number on the
	// player state changes. This checks that the number reaches the character:
	// that a level gained moves the per-level terms in game/Data/ClassStats.csv
	// and therefore the character's actual maximum health.
	//
	// WITHOUT IT, `ApplyChosenClassStats` and `RefreshAttributes` could go back
	// to reading `Cataclysm.PlayerLevel` and every other test in this file would
	// still pass while levelling did nothing at all.

	FStartingLevel Starting(10);
	if (!Starting.IsUsable())
	{
		AddError(TEXT("Cataclysm.PlayerLevel does not exist."));
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player state"), PlayerState)
		|| !TestNotNull(TEXT("player character"), Character))
	{
		return false;
	}

	// The client path, which is the one a test world can reach: it has no
	// controller to possess with, so PossessedBy itself is out of reach.
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!TestNotNull(TEXT("ability system"), ASC)
		|| !TestNotNull(TEXT("DT_ClassStats"), Table))
	{
		return false;
	}

	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const FString& ClassName = UCataclysmPlayerClassStats::ChosenClass();

	Character->ApplyChosenClassStats();

	const float AtTen = UCataclysmClassStats::BaseFor(
		Table, ClassName, TEXT("max_health"), 10);
	TestEqual(TEXT("a level 10 character has the class table's level 10 health"),
		ASC->GetNumericAttribute(MaxHealth), AtTen);

	// NOW EARN THIRTY LEVELS AND REFRESH, which is what the console command and
	// a kill both do.
	const int64 Needed = UCataclysmExperience::TotalToReach(40)
		- UCataclysmExperience::TotalToReach(10);
	TestEqual(TEXT("thirty levels gained"),
		PlayerState->GrantExperience(Needed), 30);
	TestEqual(TEXT("now level 40"), PlayerState->GetCharacterLevel(), 40);

	if (Character->GetEquipment())
	{
		Character->GetEquipment()->RefreshAttributes(ASC);
	}

	const float AtForty = UCataclysmClassStats::BaseFor(
		Table, ClassName, TEXT("max_health"), 40);
	TestTrue(TEXT("the class table gives a level 40 character more health than a "
				  "level 10 one, so this test can tell them apart"),
		AtForty > AtTen);
	TestEqual(TEXT("and the character now has the level 40 health"),
		ASC->GetNumericAttribute(MaxHealth), AtForty);

	// AND THE CONSOLE VARIABLE IS NO LONGER WHAT DECIDES IT. Moving it must not
	// move a character that has earned its level.
	Starting.Variable->Set(3, ECVF_SetByCode);
	if (Character->GetEquipment())
	{
		Character->GetEquipment()->RefreshAttributes(ASC);
	}
	TestEqual(TEXT("the stat line still resolves at the level that was earned"),
		ASC->GetNumericAttribute(MaxHealth), AtForty);

	// AND THE POSSESSION PATH AS WELL, WHICH IS A DIFFERENT FUNCTION AND WOULD
	// OTHERWISE BE UNCHECKED. `ApplyChosenClassStats` is what stands a character
	// up when it arrives in the world; `RefreshAttributes` is what re-resolves
	// it afterwards. Both had to stop reading `Cataclysm.PlayerLevel`, and
	// checking only the second would let a character loaded at level 63 arrive
	// with level 3 stats and stay that way until it changed a helmet.
	PlayerState->SetLevelAndExperience(63, 0);
	Character->ApplyChosenClassStats();

	const float AtSixtyThree = UCataclysmClassStats::BaseFor(
		Table, ClassName, TEXT("max_health"), 63);
	TestTrue(TEXT("a level 63 character has more health than a level 40 one, so "
				  "this test can tell them apart"),
		AtSixtyThree > AtForty);
	TestEqual(TEXT("and arriving in the world resolves at the loaded level"),
		ASC->GetNumericAttribute(MaxHealth), AtSixtyThree);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLevelReachesTheSaveRecord,
	"Cataclysm.CharacterLevel.TheLevelAndProgressReachTheSaveRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLevelReachesTheSaveRecord::RunTest(const FString&)
{
	using namespace CataclysmCharacterLevelTest;

	// `FCataclysmSaveGather::CharacterFrom` HAD NO TEST OF ANY KIND before this.
	// The save record's reading side is covered by a fixture in
	// CataclysmSaveRecordTests.cpp, which loads a level 42 character off disk;
	// nothing checked that the game ever WRITES a level, so the two halves could
	// have disagreed without a failure anywhere.

	FStartingLevel Starting(20);
	if (!Starting.IsUsable())
	{
		AddError(TEXT("Cataclysm.PlayerLevel does not exist."));
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player state"), PlayerState)
		|| !TestNotNull(TEXT("player character"), Character))
	{
		return false;
	}
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	UCataclysmCharacterSave* Record = NewObject<UCataclysmCharacterSave>();
	if (!TestNotNull(TEXT("character record"), Record))
	{
		return false;
	}

	// A CHARACTER THAT HAS EARNED SOMETHING BUT NOT A WHOLE LEVEL, because that
	// is the case where the two fields have to travel together. A level with no
	// progress would pass even if the progress were never written.
	const int64 Part = UCataclysmExperience::CostOfLevel(21) / 4;
	PlayerState->GrantExperience(Part);
	TestEqual(TEXT("still level 20"), PlayerState->GetCharacterLevel(), 20);

	if (!TestTrue(TEXT("the character was gathered"),
			FCataclysmSaveGather::CharacterFrom(*Character, *Record)))
	{
		return false;
	}

	TestEqual(TEXT("the level written to the record"), Record->Level, 20);
	TestEqual(TEXT("and the progress into it"), Record->Experience, Part);

	// AND AGAIN AFTER A LEVEL IS GAINED, so a record refreshed twice follows the
	// character rather than keeping the first answer.
	PlayerState->GrantExperience(UCataclysmExperience::CostOfLevel(21));
	if (!TestTrue(TEXT("the character was gathered a second time"),
			FCataclysmSaveGather::CharacterFrom(*Character, *Record)))
	{
		return false;
	}
	TestEqual(TEXT("the record follows the level up"), Record->Level, 21);
	TestEqual(TEXT("carrying the remainder with it"),
		Record->Experience, PlayerState->GetExperienceIntoLevel());

	// A CHARACTER WITH NO PLAYER STATE MUST NOT ZERO WHAT IS ALREADY THERE,
	// which is the rule the whole of CharacterFrom is written around: a record
	// loaded off disk a moment earlier would otherwise be emptied by a refresh.
	Character->SetPlayerState(nullptr);
	if (!TestTrue(TEXT("a character with no player state still gathers"),
			FCataclysmSaveGather::CharacterFrom(*Character, *Record)))
	{
		return false;
	}
	TestEqual(TEXT("and the level already in the record is left alone"),
		Record->Level, 21);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
