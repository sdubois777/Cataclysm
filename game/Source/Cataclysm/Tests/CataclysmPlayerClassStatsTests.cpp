// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmClassStats.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Data/CataclysmDataRows.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * The class stat line reaching the player, which nothing did before issue #806.
 *
 * WHAT THESE GUARD, AND IT IS A FAILURE THAT SHOWED UP AS A PLAY REPORT RATHER
 * THAN AS AN ERROR. `game/Data/ClassStats.csv` held every class's health,
 * regeneration and armour, `UCataclysmClassStats::BaseFor` could read them at a
 * level, and no code outside the test suite ever called it. Nothing failed,
 * nothing logged, and the character simply had the attribute set's placeholder
 * 100 health for the whole life of the project. A floor of creatures killed them
 * in about a second.
 *
 * THE MOST IMPORTANT TEST HERE IS THE FIRST ONE, and it is the only one that
 * compares the code against something the code does not own: it reads every
 * stat name out of the data table and insists each has an attribute. A stat the
 * design adds and the map does not know about is dropped in silence, which is
 * exactly the shape of the original defect.
 */

namespace CataclysmPlayerClassStatsTest
{
	/** An actor carrying the three attribute sets the class table writes into. */
	struct FScopedCharacter
	{
		explicit FScopedCharacter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers, not TObjectPtr: AddAttributeSetSubobject is a
			// template and deduces T from the argument, so a TObjectPtr would
			// deduce the wrapper rather than the attribute set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource =
				NewObject<UCataclysmClassResourceAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResource);

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
		}

		~FScopedCharacter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		float Read(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	static UWorld* MakeWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------
// The map against the data, which is the check the original defect needed
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmEveryClassStatDrivesAnAttribute,
	"Cataclysm.PlayerStats.EveryClassStatDrivesAnAttribute")
{
	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(FString::Printf(TEXT("DT_ClassStats does not exist at %s."),
			UCataclysmPlayerClassStats::ClassStatsAssetPath));
		return false;
	}

	const TMap<FString, FGameplayAttribute>& Map =
		UCataclysmPlayerClassStats::StatToAttribute();

	// READ OUT OF THE TABLE RATHER THAN LISTED HERE. A second copy of the stat
	// names in this file would agree with the map by construction and would
	// notice nothing.
	TSet<FString> NamedByTheDesign;
	for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
	{
		const auto* Line = reinterpret_cast<const FCataclysmClassStatRow*>(Row.Value);
		if (Line && !Line->Stat.IsEmpty())
		{
			NamedByTheDesign.Add(Line->Stat);
		}
	}

	// Without this the loop below passes on an empty table, which is what a
	// stale or unbuilt asset actually looks like.
	TestTrue(TEXT("the class table names some stats at all"),
		NamedByTheDesign.Num() >= 20);

	for (const FString& Stat : NamedByTheDesign)
	{
		TestTrue(FString::Printf(
			TEXT("the class table's stat '%s' drives a gameplay attribute. "
				 "Without an entry in StatToAttribute it is dropped in silence, "
				 "which is how the player kept 100 health for the whole life of "
				 "the project."), *Stat),
			Map.Contains(Stat));
	}

	// AND NOTHING IN THE MAP IS ABSENT FROM THE TABLE, which would be a stat
	// being written from a line that does not exist.
	for (const TPair<FString, FGameplayAttribute>& Pair : Map)
	{
		TestTrue(FString::Printf(
			TEXT("the mapped stat '%s' is one the class table actually names"),
			*Pair.Key),
			NamedByTheDesign.Contains(Pair.Key));
	}

	return true;
}

// --------------------------------------------------------------------------
// Applying it
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmClassLineReachesTheCharacter,
	"Cataclysm.PlayerStats.ApplyingTheClassLineReplacesThePlaceholderHealth")
{
	using namespace CataclysmPlayerClassStatsTest;

	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ClassStats does not exist."));
		return false;
	}

	UWorld* World = MakeWorld();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedCharacter Character(World);

	// THE PLACEHOLDER, ASSERTED BEFORE IT IS REPLACED. Without this the test
	// below would pass just as well if the attribute set already happened to
	// start at the class value, and would then say nothing about whether
	// applying did anything.
	const float Placeholder =
		Character.Read(UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	TestEqual(TEXT("a character with no class line starts on the placeholder"),
		Placeholder, 100.0f);

	const int32 Level = 20;
	const int32 Written = UCataclysmPlayerClassStats::ApplyTo(
		Character.AbilitySystem, Table,
		UCataclysmClassStats::DefaultClassName, Level);

	TestEqual(TEXT("every mapped stat was written"),
		Written, UCataclysmPlayerClassStats::StatToAttribute().Num());

	// READ BACK OFF THE TABLE, not written here as a number. What this asks is
	// whether the character got what the design says, and a second copy of 385
	// in this file would answer a different and easier question.
	const float Expected = UCataclysmClassStats::BaseFor(
		Table, UCataclysmClassStats::DefaultClassName, TEXT("max_health"), Level);

	TestEqual(TEXT("maximum health is what the class table says at that level"),
		Character.Read(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()),
		Expected);

	// AND IT IS MEANINGFULLY MORE THAN THE PLACEHOLDER, which is the point of
	// the whole change. A per-level column that resolved to nothing would still
	// satisfy the check above.
	TestTrue(FString::Printf(
		TEXT("and is well above the placeholder: %.0f against %.0f"),
		Expected, Placeholder),
		Expected > Placeholder * 3.0f);

	// THE POOLS ARE FILLED, AND THIS IS THE ORDERING TRAP. The vital attribute
	// set clamps current health to maximum health, so filling before raising
	// leaves the character on 100 with a maximum of 385 and a health bar that
	// starts a quarter full.
	TestEqual(TEXT("current health is filled to the new maximum"),
		Character.Read(UCataclysmVitalAttributeSet::GetHealthAttribute()),
		Expected);
	TestEqual(TEXT("and so is mana"),
		Character.Read(UCataclysmVitalAttributeSet::GetManaAttribute()),
		Character.Read(UCataclysmVitalAttributeSet::GetMaxManaAttribute()));

	return true;
}

CATACLYSM_TEST(FCataclysmClassLinesDifferFromEachOther,
	"Cataclysm.PlayerStats.TheThreeDemonicClassesGetDifferentStatLines")
{
	using namespace CataclysmPlayerClassStatsTest;

	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ClassStats does not exist."));
		return false;
	}

	UWorld* World = MakeWorld();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const FGameplayAttribute Armour =
		UCataclysmCombatAttributeSet::GetArmorAttribute();

	TMap<FString, float> HealthOf;
	TMap<FString, float> ArmourOf;

	for (const TCHAR* ClassName :
			{TEXT("Ravager"), TEXT("Ritualist"), TEXT("Masochist")})
	{
		const FScopedCharacter Character(World);
		UCataclysmPlayerClassStats::ApplyTo(Character.AbilitySystem, Table,
											FString(ClassName), /*Level=*/20);
		HealthOf.Add(ClassName, Character.Read(MaxHealth));
		ArmourOf.Add(ClassName, Character.Read(Armour));
	}

	// THE DESIGN'S OWN ORDERING, which is independent of anything this code
	// computes: the Masochist is written at 150 base health, the Ravager at 130
	// and the Ritualist at 70. If applying resolved every class to the same
	// shared line, all three would be equal and every other test here would
	// still pass.
	TestTrue(FString::Printf(
		TEXT("the Masochist has more health than the Ravager: %.0f against %.0f"),
		HealthOf[TEXT("Masochist")], HealthOf[TEXT("Ravager")]),
		HealthOf[TEXT("Masochist")] > HealthOf[TEXT("Ravager")]);
	TestTrue(FString::Printf(
		TEXT("and the Ravager more than the Ritualist: %.0f against %.0f"),
		HealthOf[TEXT("Ravager")], HealthOf[TEXT("Ritualist")]),
		HealthOf[TEXT("Ravager")] > HealthOf[TEXT("Ritualist")]);

	// A CLASS THAT DECLINES A STAT GETS ZERO, which is the design working rather
	// than failing. The Ritualist takes no armour at all.
	TestEqual(TEXT("the Ritualist has no armour, because its line names none and "
				   "the shared line names none either"),
		ArmourOf[TEXT("Ritualist")], 0.0f);
	TestTrue(FString::Printf(
		TEXT("while the Ravager has some: %.1f"), ArmourOf[TEXT("Ravager")]),
		ArmourOf[TEXT("Ravager")] > 0.0f);

	return true;
}

CATACLYSM_TEST(FCataclysmChosenLevelIsClamped,
	"Cataclysm.PlayerStats.TheChosenLevelStaysInsideTheDesignedRange")
{
	// A CONSOLE VARIABLE IS TYPED AT BY A PERSON, so it can hold anything. Level
	// zero would resolve every per-level term to one level below the base, which
	// gives a character less than the written base rather than more.
	IConsoleVariable* Level =
		IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.PlayerLevel"));
	if (!Level)
	{
		AddError(TEXT("Cataclysm.PlayerLevel does not exist."));
		return false;
	}

	const int32 Restore = Level->GetInt();
	ON_SCOPE_EXIT { Level->Set(Restore, ECVF_SetByCode); };

	// WRITTEN THROUGH THE CONSOLE VARIABLE AND NOT THROUGH THE C++ VARIABLE,
	// because a console variable keeps a copy of the value beside whatever it
	// references and assigning to the other one leaves the two disagreeing.
	Level->Set(0, ECVF_SetByCode);
	TestEqual(TEXT("level zero is raised to one"),
		UCataclysmPlayerClassStats::ChosenLevel(), 1);

	Level->Set(-50, ECVF_SetByCode);
	TestEqual(TEXT("and so is a negative level"),
		UCataclysmPlayerClassStats::ChosenLevel(), 1);

	Level->Set(9999, ECVF_SetByCode);
	TestEqual(TEXT("and a level past the end is lowered to the maximum"),
		UCataclysmPlayerClassStats::ChosenLevel(), UCataclysmClassStats::MaxLevel);

	Level->Set(20, ECVF_SetByCode);
	TestEqual(TEXT("and a level inside the range is left alone"),
		UCataclysmPlayerClassStats::ChosenLevel(), 20);

	return true;
}

// --------------------------------------------------------------------------
// The seam: a real player character, not a bare ability system
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmPlayerCharacterGetsItsClassLine,
	"Cataclysm.PlayerStats.APlayerCharacterLeavesThePlaceholderBehind")
{
	using namespace CataclysmPlayerClassStatsTest;

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
	// controller to possess with, so PossessedBy itself is out of reach. This
	// wires the ability system up exactly as possession would.
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("ability system"), ASC))
	{
		return false;
	}

	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();

	// WIRING THE ABILITY SYSTEM UP MUST NOT BY ITSELF APPLY A STAT LINE. This is
	// the assertion that pins where the call lives: it belongs on possession,
	// which happens once, and not on InitAbilityActorInfo, which is documented
	// as safe to run twice and does.
	TestEqual(TEXT("initialising the ability system leaves the placeholder alone"),
		ASC->GetNumericAttribute(MaxHealth), 100.0f);

	Character->ApplyChosenClassStats();

	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ClassStats does not exist."));
		return false;
	}

	const float Expected = UCataclysmClassStats::BaseFor(
		Table, UCataclysmPlayerClassStats::ChosenClass(), TEXT("max_health"),
		UCataclysmPlayerClassStats::ChosenLevel());

	TestEqual(TEXT("and the character then has the class table's health"),
		ASC->GetNumericAttribute(MaxHealth), Expected);

	// THE FIGURE THAT DECIDES WHETHER A FLOOR CAN BE FINISHED. A Brute deals 35
	// a hit -- BruteAttackDamage in CataclysmGameMode.h -- and at 100 health
	// three of them killed the character. This says the default level leaves
	// enough health for at least the eight to ten hits
	// sim/cataclysm_sim/enemy_stats.py fitted the enemy damage constants around.
	const float BruteHitsSurvived = Expected / 35.0f;
	TestTrue(FString::Printf(
		TEXT("which is %.0f health, or %.1f hits from a Brute"),
		Expected, BruteHitsSurvived),
		BruteHitsSurvived >= 8.0f);

	return true;
}

CATACLYSM_TEST(FCataclysmPossessionAppliesTheClassLine,
	"Cataclysm.PlayerStats.BeingPossessedIsWhatAppliesTheClassLine")
{
	using namespace CataclysmPlayerClassStatsTest;

	// WITHOUT THIS TEST NOTHING WOULD NOTICE THE CALL BEING DELETED, and that is
	// precisely the defect this whole change exists to repair: a function that
	// worked, that had tests, and that no code path reached. The test above
	// drives ApplyChosenClassStats by hand, so it would pass just as well with
	// PossessedBy calling nothing at all.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();

	// A PLAYER CONTROLLER AND NOT A BARE AController, WHICH IS ABSTRACT and
	// fails to spawn with "class Controller is abstract" in the log rather than
	// with an error the test would otherwise attribute to something else.
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player state"), PlayerState)
		|| !TestNotNull(TEXT("controller"), Controller)
		|| !TestNotNull(TEXT("player character"), Character))
	{
		return false;
	}

	// A PLAYER STATE ATTACHED BY HAND. A test world has no game mode, and a game
	// mode is what normally creates a player state, so it is put on the
	// controller directly. APawn::PossessedBy copies it onto the pawn only when
	// the controller has one, which is why this line is what makes the rest
	// work.
	Controller->SetPlayerState(PlayerState);

	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();

	// THE PAWN'S HALF OF POSSESSION, AND NOT AController::Possess. That would
	// also run the controller's own half -- view targets, restarting the client
	// -- which a test world with no player has no business doing, and none of it
	// is what this test is about.
	Character->PossessedBy(Controller);

	UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("possession wired the ability system up"), ASC))
	{
		return false;
	}

	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ClassStats does not exist."));
		return false;
	}

	const float Expected = UCataclysmClassStats::BaseFor(
		Table, UCataclysmPlayerClassStats::ChosenClass(), TEXT("max_health"),
		UCataclysmPlayerClassStats::ChosenLevel());

	TestEqual(TEXT("being possessed is what gives the character its health"),
		ASC->GetNumericAttribute(MaxHealth), Expected);
	TestNotEqual(TEXT("and it is not the placeholder the attribute set writes"),
		ASC->GetNumericAttribute(MaxHealth), 100.0f);

	return true;
}

CATACLYSM_TEST(FCataclysmApplyingRefusesNothing,
	"Cataclysm.PlayerStats.ApplyingWithNothingToApplyToWritesNothing")
{
	// Both of these are reachable: the table is missing on a checkout whose
	// data assets have not been built, and a caller can hold no ability system
	// before possession.
	TestEqual(TEXT("no ability system writes nothing"),
		UCataclysmPlayerClassStats::ApplyTo(
			nullptr, UCataclysmPlayerClassStats::LoadTable(),
			UCataclysmClassStats::DefaultClassName, 20), 0);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
