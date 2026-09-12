// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmMovement.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Data/CataclysmDataRows.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Dungeon/CataclysmFloorBrief.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interface/CataclysmFloorModifierPanelLayout.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * What the dungeon modifiers do on a floor. Issue #41.
 *
 * WHAT THESE COVER. Starvation and Dehydration, the first two of the 117 rows of
 * `game/Data/DungeonModifiers.csv` to change anything a player can see; the
 * console variable that puts chosen modifiers on the dungeon being played; and
 * what the floor panel says about each modifier, the ones that do nothing
 * included.
 *
 * THE CHAIN IS WALKED TO THE END, NOT STOPPED AT THE NUMBERS. A rule that
 * computed the right share and never reached a character's maximum health would
 * pass every test of the share. So the stat tests build a character, refresh its
 * stats through the equipment component the way the game does, and read the
 * attribute back.
 *
 * THE PLAYER LOOKUP IS COVERED SINCE ISSUE #41'S SLICE 2, and this paragraph
 * used to say the opposite. It said a test world has no player controller, so
 * `ApplyFloorRulesToPlayer` finding the player was reached by nothing. What is
 * true is that a test world is GIVEN no player controller unless a test spawns
 * one: the four tests at the end of this file spawn a player state, an
 * `APlayerController` and a player character and call `AController::Possess`,
 * after which `GetFirstPlayerController()->GetPawn()` answers. Fourteen other
 * test files already did this.
 *
 * `AController::Possess` AND NOT `APawn::PossessedBy`, WHICH IS THE TRAP. The
 * pawn's half tells the pawn which controller has it and does not tell the
 * CONTROLLER which pawn it has, so the lookup finds the controller and
 * `GetPawn` answers null. `CataclysmDroppedItemTests.cpp` records losing time
 * to exactly that.
 *
 * WHAT IS STILL NOT COVERED, said plainly: the floor panel. Showing it needs a
 * `ACataclysmPlayerController`, and these tests possess with a plain
 * `APlayerController`, so the panel call is skipped rather than checked.
 */

namespace CataclysmDungeonModifierEffectsTest
{
	const FName Starvation(TEXT("Famine_Starvation"));
	const FName Dehydration(TEXT("Famine_Dehydration"));
	const FName EdictOfSilence(TEXT("Celestial_Edict_of_Silence"));

	/** The two rows of issue #41's slice 2, which change during play. */
	const FName ForcedMarch(TEXT("War_Forced_March"));
	const FName NihilsEmbrace(TEXT("Void_The_Nihil_s_Embrace"));

	/** And slice 5's, which changes during play too. */
	const FName DeathsEmbrace(TEXT("Death_Death_s_Embrace"));

	/**
	 * A player the dungeon game mode's beat can find, and the creature-free parts
	 * of a real one: a player state holding the ability system component, a
	 * controller, and a possessed pawn.
	 *
	 * `AController::Possess` AND NOT `APawn::PossessedBy`. The pawn's half does
	 * not tell the controller which pawn it has, and the beat reaches the player
	 * through `GetFirstPlayerController()->GetPawn()`, which would answer null.
	 */
	struct FPossessedPlayer
	{
		explicit FPossessedPlayer(UWorld* World)
		{
			PlayerState = World->SpawnActor<ACataclysmPlayerState>();
			Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
			if (!PlayerState || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(PlayerState);
			Controller->Possess(Character);
			AbilitySystem = Cast<UCataclysmAbilitySystemComponent>(
				Character->GetAbilitySystemComponent());
		}

		bool IsUsable() const { return Character && AbilitySystem; }

		float Read(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		/** Walk this far along X in one sample, so the walk is counted. */
		void Walk(float Metres) const
		{
			const FVector Where = Character->GetActorLocation();
			Character->SetActorLocation(Where + FVector(
				Metres * UCataclysmMovement::CentimetresPerMetre, 0.0f, 0.0f));
			UCataclysmMovement::SampleStep(Character);
		}

		ACataclysmPlayerState* PlayerState = nullptr;
		APlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * A character holding every attribute set a real one holds, and equipment.
	 *
	 * THE SAME SHAPE AS THE EQUIPMENT TESTS' OWN, for the reason they give:
	 * `UCataclysmPlayerClassStats::ApplyTo` skips any stat whose set is missing,
	 * so a character holding only the vital set would take a path no real
	 * character takes.
	 */
	struct FModifierTestCharacter
	{
		explicit FModifierTestCharacter(UWorld* InWorld)
		{
			Actor = InWorld->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers, not TObjectPtr: AddAttributeSetSubobject deduces its
			// type from the argument and would deduce the wrapper.
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmPrimaryAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmClassResourceAttributeSet>(Actor));

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Equipment = NewObject<UCataclysmEquipmentComponent>(Actor);
			Equipment->RegisterComponent();
		}

		~FModifierTestCharacter()
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

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
	};

	/**
	 * A string console variable set the way a person at the console sets it,
	 * and emptied again afterwards.
	 *
	 * AT THE CONSOLE'S PRIORITY, because Unreal discards a write from a lower
	 * priority than the last one, and a test that set it any lower would pass
	 * alone and do nothing in a full run after somebody typed at the console.
	 */
	struct FScopedConsoleString
	{
		FScopedConsoleString(const TCHAR* Name, const TCHAR* Value)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(Name);
			Set(Value);
		}

		~FScopedConsoleString()
		{
			Set(TEXT(""));
		}

		void Set(const TCHAR* Value)
		{
			if (Variable)
			{
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
	};

	/** The one dungeon-rule modifier on a stat in a character's stored inputs. */
	const FCataclysmStatModifier* DungeonRuleOn(
		const UCataclysmAbilitySystemComponent* AbilitySystem, const TCHAR* Stat)
	{
		const FCataclysmStatInputs* Inputs = AbilitySystem->GetStatInputs(FName(Stat));
		if (!Inputs)
		{
			return nullptr;
		}
		for (const FCataclysmStatModifier& Modifier : Inputs->Modifiers)
		{
			if (Modifier.Source == ECataclysmModifierSource::DungeonRule)
			{
				return &Modifier;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierEffectsShareTest,
	"Cataclysm.DungeonModifierEffects.EachFloorTakesOnePercentUpToSixty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierEffectsShareTest::RunTest(const FString& Parameters)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// EVERY BOUNDARY THE RULE HAS: nothing before the first floor, one share on
	// it, the cap reached exactly, and held there however deep the dungeon goes.
	struct FCase
	{
		int32 Floor;
		float Expected;
	};
	const FCase Cases[] = {
		{0, 0.0f}, {1, 1.0f}, {2, 2.0f}, {10, 10.0f}, {59, 59.0f},
		{60, 60.0f}, {61, 60.0f}, {150, 60.0f},
	};

	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("Starvation has taken %.0f%% by floor %d"),
								  Case.Expected, Case.Floor),
				  Effects::ShareTakenOnFloor(Effects::StarvationPercentPerFloor,
											 Effects::StarvationMostPercent,
											 Case.Floor),
				  Case.Expected, 0.001f);

		TestEqual(FString::Printf(TEXT("Dehydration has taken %.0f%% by floor %d"),
								  Case.Expected, Case.Floor),
				  Effects::ShareTakenOnFloor(Effects::DehydrationPercentPerFloor,
											 Effects::DehydrationMostPercent,
											 Case.Floor),
				  Case.Expected, 0.001f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierEffectsOnlyTheFloorsTest,
	"Cataclysm.DungeonModifierEffects.OnlyTheModifiersOnTheFloorTakeAnything",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierEffectsOnlyTheFloorsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierEffectsTest;
	using Effects = UCataclysmDungeonModifierEffects;

	TestTrue(TEXT("a floor carrying nothing takes nothing"),
			 Effects::PlayerEffectsFor({}, 30).IsEmpty());

	// A MODIFIER WITH NO RULE TAKES NOTHING EITHER, which is what "does nothing"
	// on the floor panel has to mean.
	TestTrue(TEXT("a floor carrying only an unbuilt modifier takes nothing"),
			 Effects::PlayerEffectsFor({EdictOfSilence}, 30).IsEmpty());

	const FCataclysmPlayerFloorEffects Starved = Effects::PlayerEffectsFor({Starvation}, 30);
	TestEqual(TEXT("Starvation takes health"), Starved.MaxHealthLessPercent, 30.0f, 0.001f);
	TestEqual(TEXT("and energy shield by the same share"),
			  Starved.MaxEnergyShieldLessPercent, 30.0f, 0.001f);
	TestEqual(TEXT("and no mana"), Starved.MaxManaLessPercent, 0.0f, 0.001f);

	const FCataclysmPlayerFloorEffects Parched = Effects::PlayerEffectsFor({Dehydration}, 30);
	TestEqual(TEXT("Dehydration takes mana"), Parched.MaxManaLessPercent, 30.0f, 0.001f);
	TestEqual(TEXT("and no health"), Parched.MaxHealthLessPercent, 0.0f, 0.001f);
	TestEqual(TEXT("and no energy shield"), Parched.MaxEnergyShieldLessPercent, 0.0f, 0.001f);

	// BOTH AT ONCE, IN ANY ORDER, AND PAST BOTH CAPS.
	const FCataclysmPlayerFloorEffects Both =
		Effects::PlayerEffectsFor({EdictOfSilence, Dehydration, Starvation}, 75);
	TestEqual(TEXT("both on floor 75: health held at 60%"),
			  Both.MaxHealthLessPercent, 60.0f, 0.001f);
	TestEqual(TEXT("energy shield held at 60%"), Both.MaxEnergyShieldLessPercent, 60.0f, 0.001f);
	TestEqual(TEXT("mana held at 60%"), Both.MaxManaLessPercent, 60.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierEffectsLessTest,
	"Cataclysm.DungeonModifierEffects.TheyTakeAShareOfTheFinishedMaximumNotOfTheIncreases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierEffectsLessTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierEffectsTest;
	using Effects = UCataclysmDungeonModifierEffects;

	const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
		Effects::StatModifiersFor(Effects::PlayerEffectsFor({Starvation}, 10));

	TestEqual(TEXT("Starvation touches two stats"), Modifiers.Num(), 2);
	TestFalse(TEXT("and not mana"), Modifiers.Contains(FName(TEXT("max_mana"))));

	for (const TCHAR* Stat : {TEXT("max_health"), TEXT("max_energy_shield")})
	{
		const TArray<FCataclysmStatModifier>* On = Modifiers.Find(FName(Stat));
		if (!TestNotNull(FString::Printf(TEXT("a modifier on %s"), Stat), On)
			|| !TestEqual(FString::Printf(TEXT("exactly one on %s"), Stat), On->Num(), 1))
		{
			continue;
		}

		const FCataclysmStatModifier& Modifier = (*On)[0];
		TestTrue(FString::Printf(TEXT("%s: in the More bucket"), Stat),
				 Modifier.Bucket == ECataclysmStatBucket::More);
		TestTrue(FString::Printf(TEXT("%s: from a dungeon rule"), Stat),
				 Modifier.Source == ECataclysmModifierSource::DungeonRule);
		TestEqual(FString::Printf(TEXT("%s: 10%% less on floor 10"), Stat),
				  Modifier.Value, -10.0f, 0.001f);
		TestTrue(FString::Printf(TEXT("%s: for every skill, not a scoped one"), Stat),
				 Modifier.RequiredTags.IsEmpty());
	}

	// **THE POINT OF THE LESS BUCKET, MEASURED.** A base of 100 carrying +200%
	// increased from gear is 300. Ten per cent LESS of that is 270; ten points
	// taken out of the increases instead would be 290, a 3.3% loss where the row
	// says 10%. The pipeline refusing a More from this source would give 300.
	FCataclysmStatModifier Gear;
	Gear.Bucket = ECataclysmStatBucket::Increased;
	Gear.Source = ECataclysmModifierSource::GearAffix;
	Gear.Value = 200.0f;

	TArray<FCataclysmStatModifier> Line = {Gear};
	Line.Append(Modifiers.FindChecked(FName(TEXT("max_health"))));

	const FCataclysmStatBreakdown Result = UCataclysmStatPipeline::Evaluate(
		100.0f, Line, FGameplayTagContainer(), FCataclysmStatConditions());
	TestEqual(TEXT("a tenth of the finished 300, not ten points of the increases"),
			  Result.Final, 270.0f, 0.01f);
	TestEqual(TEXT("and nothing was refused"), Result.RejectedMoreCount, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierEffectsReachTheCharacterTest,
	"Cataclysm.DungeonModifierEffects.StarvationLowersThePlayersMaximumsAndLeavingGivesThemBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierEffectsReachTheCharacterTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierEffectsTest;
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FModifierTestCharacter Character(World);
	Character.Equipment->RefreshAttributes(Character.AbilitySystem);

	const float BareHealth = Character.Read(Vital::GetMaxHealthAttribute());
	const float BareShield = Character.Read(Vital::GetMaxEnergyShieldAttribute());
	const float BareMana = Character.Read(Vital::GetMaxManaAttribute());
	if (!TestTrue(TEXT("the character has health to lose"), BareHealth > 0.0f)
		|| !TestTrue(TEXT("and mana to lose"), BareMana > 0.0f))
	{
		return false;
	}

	// FLOOR 10 OF A STARVATION DUNGEON.
	TestTrue(TEXT("the rule reached the character"),
			 Effects::ApplyToCharacter(Effects::PlayerEffectsFor({Starvation}, 10),
									   Character.AbilitySystem, Character.Equipment));

	TestEqual(TEXT("maximum health is 10% less on floor 10"),
			  Character.Read(Vital::GetMaxHealthAttribute()), BareHealth * 0.9f, 0.01f);
	TestEqual(TEXT("maximum mana is untouched by Starvation"),
			  Character.Read(Vital::GetMaxManaAttribute()), BareMana, 0.01f);
	TestEqual(TEXT("maximum energy shield is 10% less, whatever it was"),
			  Character.Read(Vital::GetMaxEnergyShieldAttribute()), BareShield * 0.9f, 0.01f);

	// THE SHIELD LINE ABOVE IS EXACT EVEN FOR A CLASS WITH NO SHIELD, where 0.9
	// of nothing is nothing. So the rule's arrival on that stat is checked in the
	// stored inputs as well, which a class with no shield still records.
	const FCataclysmStatModifier* OnShield =
		DungeonRuleOn(Character.AbilitySystem, TEXT("max_energy_shield"));
	if (TestNotNull(TEXT("the shield's stat line carries the dungeon rule"), OnShield))
	{
		TestEqual(TEXT("as 10% less"), OnShield->Value, -10.0f, 0.001f);
	}

	// NOW DEHYDRATION ALONE: the health comes back and the mana goes.
	Effects::ApplyToCharacter(Effects::PlayerEffectsFor({Dehydration}, 10),
							  Character.AbilitySystem, Character.Equipment);
	TestEqual(TEXT("health is back when Starvation is not on the floor"),
			  Character.Read(Vital::GetMaxHealthAttribute()), BareHealth, 0.01f);
	TestEqual(TEXT("maximum mana is 10% less under Dehydration"),
			  Character.Read(Vital::GetMaxManaAttribute()), BareMana * 0.9f, 0.01f);

	// AND LEAVING GIVES EVERYTHING BACK EXACTLY.
	Effects::ApplyToCharacter(FCataclysmPlayerFloorEffects(),
							  Character.AbilitySystem, Character.Equipment);
	TestEqual(TEXT("health restored exactly"),
			  Character.Read(Vital::GetMaxHealthAttribute()), BareHealth, 0.01f);
	TestEqual(TEXT("mana restored exactly"),
			  Character.Read(Vital::GetMaxManaAttribute()), BareMana, 0.01f);
	TestEqual(TEXT("energy shield restored exactly"),
			  Character.Read(Vital::GetMaxEnergyShieldAttribute()), BareShield, 0.01f);
	TestNull(TEXT("and no dungeon rule is left on the health line"),
			 DungeonRuleOn(Character.AbilitySystem, TEXT("max_health")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierEffectsSurviveARefreshTest,
	"Cataclysm.DungeonModifierEffects.ARefreshForAnyOtherReasonKeepsTheFloorsRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierEffectsSurviveARefreshTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierEffectsTest;
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FModifierTestCharacter Character(World);
	Character.Equipment->RefreshAttributes(Character.AbilitySystem);
	const float BareHealth = Character.Read(Vital::GetMaxHealthAttribute());

	Effects::ApplyToCharacter(Effects::PlayerEffectsFor({Starvation}, 20),
							  Character.AbilitySystem, Character.Equipment);

	// THE REFRESH A HELMET CHANGE, A LEVEL GAINED OR A POINT SPENT WOULD RUN.
	// None of them knows a dungeon exists, so if the floor's rule lived anywhere
	// but where this refresh looks, it would be dropped here.
	Character.Equipment->RefreshAttributes(Character.AbilitySystem);

	TestEqual(TEXT("still 20% less after a refresh made for another reason"),
			  Character.Read(Vital::GetMaxHealthAttribute()), BareHealth * 0.8f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierEffectsFollowTheFloorTest,
	"Cataclysm.DungeonModifierEffects.TheGameModeFollowsTheFloorBeingStoodOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierEffectsFollowTheFloorTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierEffectsTest;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode =
		World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode))
	{
		return false;
	}

	FModifierTestCharacter Character(World);
	Character.Equipment->RefreshAttributes(Character.AbilitySystem);
	const float BareHealth = Character.Read(Vital::GetMaxHealthAttribute());

	// A STARVATION DUNGEON, WALKED FROM FLOOR 5 TO FLOOR 6.
	Mode->DungeonModifiers = {Starvation};
	Mode->FloorNumber = 5;
	if (!TestNotNull(TEXT("floor 5 was built"), Mode->BuildFloor()))
	{
		return false;
	}
	TestTrue(TEXT("floor 5 carries Starvation"), Mode->FloorBrief.Modifiers.Contains(Starvation));
	TestTrue(TEXT("the floor's rules reached the character"),
			 Mode->ApplyFloorRulesTo(Character.AbilitySystem, Character.Equipment));
	TestEqual(TEXT("5% less on floor 5"),
			  Character.Read(Vital::GetMaxHealthAttribute()), BareHealth * 0.95f, 0.01f);

	Mode->FloorNumber = 6;
	Mode->BuildFloor();
	Mode->ApplyFloorRulesTo(Character.AbilitySystem, Character.Equipment);
	TestEqual(TEXT("6% less on floor 6, not 5% and not 11%"),
			  Character.Read(Vital::GetMaxHealthAttribute()), BareHealth * 0.94f, 0.01f);

	// LEAVING EMPTIES THE BRIEF, AND THE RULE GOES WITH IT.
	Mode->LeaveEmpireDungeon();
	Mode->ApplyFloorRulesTo(Character.AbilitySystem, Character.Equipment);
	TestEqual(TEXT("everything back after leaving the dungeon"),
			  Character.Read(Vital::GetMaxHealthAttribute()), BareHealth, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierEffectsTypedNamesTest,
	"Cataclysm.DungeonModifierEffects.TypedNamesAreReadAsKeysOrNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierEffectsTypedNamesTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = UCataclysmDungeonModifierTable::LoadDungeonModifierTable();
	if (!Table)
	{
		AddError(TEXT("DT_DungeonModifiers would not load, so nothing below checked "
					  "anything. It is generated from game/Data/DungeonModifiers.csv."));
		return false;
	}

	// A NAME IN LOWER CASE, A KEY, A NAME TYPED WITH A STRAIGHT APOSTROPHE WHERE
	// THE DESIGN HAS A CURLY ONE, A WORD THAT IS NOTHING, AND A REPEAT IN CAPITALS.
	TArray<FString> NotUnderstood;
	const TArray<FName> Keys = UCataclysmDungeonModifierTable::KeysNamedBy(
		TEXT(" starvation , Famine_Dehydration, Heaven's Quake, nonsense, STARVATION "),
		Table, NotUnderstood);

	if (!TestEqual(TEXT("three modifiers were understood, the repeat once"), Keys.Num(), 3))
	{
		return false;
	}

	// CASE-SENSITIVE ON PURPOSE. Unreal's string tests ignore case by default,
	// and the point here is that the table's own spelling comes back.
	TestTrue(TEXT("a name finds its row key, in the table's spelling"),
			 Keys[0].ToString().Equals(TEXT("Famine_Starvation"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("a key finds itself"),
			 Keys[1].ToString().Equals(TEXT("Famine_Dehydration"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("a straight apostrophe finds the curly one"),
			 Keys[2].ToString().Equals(TEXT("Celestial_Heaven_s_Quake"), ESearchCase::CaseSensitive));

	if (TestEqual(TEXT("one piece was not a modifier"), NotUnderstood.Num(), 1))
	{
		TestEqual(TEXT("and it is the one that was nonsense"), NotUnderstood[0],
				  FString(TEXT("nonsense")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierEffectsConsoleTest,
	"Cataclysm.DungeonModifierEffects.TheConsoleCanPutModifiersOnTheDungeon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierEffectsConsoleTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierEffectsTest;

	const UDataTable* Table = UCataclysmDungeonModifierTable::LoadDungeonModifierTable();
	if (!Table)
	{
		AddError(TEXT("DT_DungeonModifiers would not load."));
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode =
		World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode))
	{
		return false;
	}

	// THE DUNGEON'S OWN DRAW, WHICH THE CONSOLE REPLACES.
	Mode->DungeonModifiers = {EdictOfSilence};
	Mode->DungeonModifierScore = 20.0f;

	FScopedConsoleString Forced(TEXT("Cataclysm.DungeonModifiers"),
								TEXT("Starvation, Famine_Dehydration"));
	if (!TestNotNull(TEXT("Cataclysm.DungeonModifiers is registered"), Forced.Variable))
	{
		return false;
	}

	Mode->BuildFloor();
	TestEqual(TEXT("the floor carries what was typed, in the order typed"),
			  Mode->FloorBrief.Modifiers,
			  TArray<FName>({Starvation, Dehydration}));

	// THE DANGER SCORES OF WHAT WAS TYPED, READ FROM THE TABLE: Starvation 20,
	// Dehydration 15. The dungeon's own 20 would be wrong in both directions.
	TestEqual(TEXT("and is worth what those two are worth"),
			  Mode->FloorBrief.ModifierScore, 35.0f, 0.001f);

	// A WORD THAT NAMES NOTHING HANDS THE CHOICE BACK, as every other control in
	// the dungeon game mode does when it is not asked for anything real.
	Forced.Set(TEXT("nonsense"));
	Mode->BuildFloor();
	TestEqual(TEXT("nonsense leaves the dungeon's own modifiers"),
			  Mode->FloorBrief.Modifiers, TArray<FName>({EdictOfSilence}));

	Forced.Set(TEXT(""));
	Mode->BuildFloor();
	TestEqual(TEXT("and so does clearing it"),
			  Mode->FloorBrief.Modifiers, TArray<FName>({EdictOfSilence}));
	TestEqual(TEXT("with the dungeon's own score"),
			  Mode->FloorBrief.ModifierScore, 20.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierEffectsPanelTest,
	"Cataclysm.DungeonModifierEffects.ThePanelMarksTheOnesThatDoNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierEffectsPanelTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierEffectsTest;
	using Layout = UCataclysmFloorModifierPanelLayout;

	const UDataTable* Table = UCataclysmDungeonModifierTable::LoadDungeonModifierTable();
	if (!Table)
	{
		AddError(TEXT("DT_DungeonModifiers would not load."));
		return false;
	}

	const FName UnstableDimensions(FCataclysmDungeonFloorRules::UnstableDimensionsKey);
	const FName NotARow(TEXT("Not_A_Row"));

	const TArray<FCataclysmFloorModifierLine> Lines =
		Layout::LinesFor({Starvation, EdictOfSilence, UnstableDimensions, NotARow}, Table);
	if (!TestEqual(TEXT("one line per modifier"), Lines.Num(), 4))
	{
		return false;
	}

	TestEqual(TEXT("a built one is named plainly"), Layout::NameLineFor(Lines[0]),
			  FString(TEXT("Starvation")));
	TestTrue(TEXT("with its row's own words"),
			 Lines[0].Description.Contains(TEXT("reduced by 1%")));

	TestTrue(TEXT("an unbuilt one says it does nothing"),
			 Layout::NameLineFor(Lines[1]).StartsWith(TEXT("Edict of Silence"))
				 && Layout::NameLineFor(Lines[1]).Contains(TEXT("not built yet")));
	TestTrue(TEXT("a partly built one says so"),
			 Layout::NameLineFor(Lines[2]).Contains(TEXT("partly built")));
	TestTrue(TEXT("a key that is not a row is shown as what it is"),
			 !Lines[3].bIsARow
				 && Layout::NameLineFor(Lines[3]).StartsWith(TEXT("Not_A_Row"))
				 && Layout::NameLineFor(Lines[3]).Contains(TEXT("not a row")));

	TestEqual(TEXT("the heading counts them"), Layout::HeadingFor(5, 4),
			  FString(TEXT("Floor 5: 4 dungeon modifiers")));
	TestEqual(TEXT("in the singular for one"), Layout::HeadingFor(1, 1),
			  FString(TEXT("Floor 1: 1 dungeon modifier")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierStacksTest,
	"Cataclysm.DungeonModifierEffects.ForcedMarchStacksOnlyAfterThreeSecondsStill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierStacksTest::RunTest(const FString& Parameters)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// THE ROW STATES THE THREE SECONDS AND NOTHING ELSE: "You take stacking
	// damage if you stand still for >3s." The share, the rate and the cap are
	// judgements recorded in `docs/DECISIONS.md`, so they are read from the
	// constants here rather than typed, and
	// `tools/tests/test_dungeon_modifier_rules_are_the_rows.py` fails if the row
	// ever states one of its own.
	const float Threshold = Effects::ForcedMarchSecondsBeforeDamage;
	const int32 Most = Effects::ForcedMarchMostStacks;

	// BELOW THE THRESHOLD, NOTHING AT ALL.
	TestEqual(TEXT("a character that just moved has no stacks"),
			  Effects::ForcedMarchStacksAfter(0.0f), 0);
	TestEqual(TEXT("nor one a moment before the threshold"),
			  Effects::ForcedMarchStacksAfter(Threshold - 0.01f), 0);

	// THE ROW SAYS "FOR MORE THAN 3s", so exactly three seconds is not yet
	// standing still for more than three. The first stack arrives once the
	// threshold is passed.
	TestEqual(TEXT("the first stack arrives just past the threshold"),
			  Effects::ForcedMarchStacksAfter(Threshold + 0.01f), 1);

	// ONE A SECOND AFTER THAT. This rate has no constant of its own: it is the
	// shape of this function, one stack plus one for each whole second past the
	// threshold.
	TestEqual(TEXT("two stacks a second later"),
			  Effects::ForcedMarchStacksAfter(Threshold + 1.0f), 2);
	TestEqual(TEXT("three the second after"),
			  Effects::ForcedMarchStacksAfter(Threshold + 2.0f), 3);

	// AND IT STOPS. Without a cap the one comparable mechanism in the genre
	// kills in about five seconds, and this row's danger weight is the low end
	// of the scale.
	TestEqual(TEXT("it stops at the cap"),
			  Effects::ForcedMarchStacksAfter(Threshold + Most + 10.0f), Most);
	TestEqual(TEXT("and a minute still is still the cap"),
			  Effects::ForcedMarchStacksAfter(60.0f), Most);

	// A NEGATIVE WAIT IS "NO CHARACTER TO READ" AND TAKES NOTHING, the same
	// answer the movement conditions give. Without this a character nothing
	// could be read from would be treated as standing still for ever.
	TestEqual(TEXT("an unknown wait takes nothing"),
			  Effects::ForcedMarchStacksAfter(-1.0f), 0);

	// WHAT A STACK COSTS, per second, as a share of maximum health.
	TestEqual(TEXT("no stacks cost nothing"),
			  Effects::ForcedMarchSharePerSecond(0), 0.0f, 0.001f);
	TestEqual(TEXT("one stack costs the share"),
			  Effects::ForcedMarchSharePerSecond(1),
			  Effects::ForcedMarchPercentPerStackPerSecond, 0.001f);
	TestEqual(TEXT("five stacks cost five times it"),
			  Effects::ForcedMarchSharePerSecond(Most),
			  Effects::ForcedMarchPercentPerStackPerSecond * Most, 0.001f);
	TestEqual(TEXT("and a negative count costs nothing"),
			  Effects::ForcedMarchSharePerSecond(-1), 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierEffectsNihilLossTest,
	"Cataclysm.DungeonModifierEffects.TheNihilsEmbraceTakesAPointPerStretchWalked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierEffectsNihilLossTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierEffectsTest;
	using Effects = UCataclysmDungeonModifierEffects;

	const float PerPoint = Effects::NihilsEmbraceMetresPerResistancePercent;
	const float Most = Effects::NihilsEmbraceMostResistancePercent;

	// WHOLE POINTS, ROUNDED DOWN. The row says "slowly and permanently reduced"
	// and states no number, so the distance per point is a judgement; what is
	// not a judgement is that a player should see a whole number change rather
	// than a fraction crawling.
	TestEqual(TEXT("standing still costs nothing"),
			  Effects::NihilsEmbraceResistanceLost(0.0f), 0.0f, 0.001f);
	TestEqual(TEXT("half the distance costs nothing yet"),
			  Effects::NihilsEmbraceResistanceLost(PerPoint * 0.5f), 0.0f, 0.001f);
	TestEqual(TEXT("the whole distance costs one point"),
			  Effects::NihilsEmbraceResistanceLost(PerPoint), 1.0f, 0.001f);
	TestEqual(TEXT("and one and a half still costs one"),
			  Effects::NihilsEmbraceResistanceLost(PerPoint * 1.5f), 1.0f, 0.001f);
	TestEqual(TEXT("twice it costs two"),
			  Effects::NihilsEmbraceResistanceLost(PerPoint * 2.0f), 2.0f, 0.001f);

	// AND IT STOPS, because a resistance falling without limit turns a long
	// floor into an unwinnable one. The sister row Starvation states a cap of
	// its own; this one does not, so the cap is a judgement.
	TestEqual(TEXT("the loss stops at the cap"),
			  Effects::NihilsEmbraceResistanceLost(PerPoint * Most), Most, 0.001f);
	TestEqual(TEXT("and walking far past it takes no more"),
			  Effects::NihilsEmbraceResistanceLost(PerPoint * Most * 10.0f),
			  Most, 0.001f);

	// A NEGATIVE DISTANCE TAKES NOTHING rather than giving resistance back.
	TestEqual(TEXT("a negative distance takes nothing"),
			  Effects::NihilsEmbraceResistanceLost(-50.0f), 0.0f, 0.001f);

	// ALL EIGHT RESISTANCES, AND THE MODIFIERS SAY SO. The row says "your
	// resistances", so one type would be wrong. Read out of the stat inputs
	// rather than off eight attributes, the way the Starvation tests read
	// max_health.
	FCataclysmPlayerFloorEffects Losing;
	Losing.ResistanceLessPercent = 4.0f;
	const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
		Effects::StatModifiersFor(Losing);

	for (const FName& Type : UCataclysmItemModifiers::DamageTypeNames())
	{
		const FName Stat = UCataclysmItemModifiers::ResistanceStatFor(Type);
		const TArray<FCataclysmStatModifier>* On = Modifiers.Find(Stat);
		if (!TestNotNull(*FString::Printf(TEXT("%s carries a modifier"),
										  *Stat.ToString()),
						 On))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s loses four"), *Stat.ToString()),
				  (*On)[0].Value, -4.0f, 0.001f);
	}

	TestEqual(TEXT("and eight of them, one per damage type"),
			  Modifiers.Num(), UCataclysmItemModifiers::DamageTypeNames().Num());

	// THE REWARD IS THE SAME EIGHT STATS THE OTHER WAY UP, so a cleansed player
	// reads a gain rather than a smaller loss.
	FCataclysmPlayerFloorEffects Rewarded;
	Rewarded.ResistanceMorePercent = Effects::NihilsEmbraceRewardResistancePercent;
	const TMap<FName, TArray<FCataclysmStatModifier>> Reward =
		Effects::StatModifiersFor(Rewarded);
	const FName War = UCataclysmItemModifiers::ResistanceStatFor(TEXT("War"));
	if (const TArray<FCataclysmStatModifier>* On = Reward.Find(War))
	{
		TestTrue(TEXT("the reward is a gain and not a loss"), (*On)[0].Value > 0.0f);
	}
	else
	{
		AddError(TEXT("the reward put no modifier on resistance_war"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierMarchBeatTest,
	"Cataclysm.DungeonModifierEffects.ForcedMarchTakesHealthFromAStandingPlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierMarchBeatTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierEffectsTest;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode =
		World->SpawnActor<ACataclysmDungeonGameMode>();
	const FPossessedPlayer Player(World);
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("a possessed player with an ability system"),
					 Player.IsUsable()))
	{
		return false;
	}

	Mode->DungeonModifiers = {ForcedMarch};
	Mode->FloorNumber = 1;
	if (!TestNotNull(TEXT("the floor was built"), Mode->BuildFloor()))
	{
		return false;
	}
	TestTrue(TEXT("the floor carries Forced March"),
			 Mode->FloorBrief.Modifiers.Contains(ForcedMarch));

	// THE FIRST SAMPLE STARTS THE STANDING-STILL CLOCK. Without it the player
	// has never been sampled and the beat reads an unknown wait.
	Player.Walk(0.0f);
	const float Full = Player.Read(Vital::GetHealthAttribute());
	TestTrue(TEXT("the player starts with some health"), Full > 0.0f);

	// STANDING STILL FOR LESS THAN THE ROW'S THRESHOLD COSTS NOTHING.
	CataclysmTestWorld::RunClock(World, 2.0f);
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	TestEqual(TEXT("two seconds still costs nothing"),
			  Player.Read(Vital::GetHealthAttribute()), Full, 0.01f);

	// PAST IT, THE BEAT TAKES A SHARE. Driven through Tick because the beat is
	// private and the header says a test ticks this actor.
	CataclysmTestWorld::RunClock(World, 3.0f);
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	const float Hurt = Player.Read(Vital::GetHealthAttribute());
	TestTrue(TEXT("standing still past the threshold costs health"), Hurt < Full);

	// AND MOVING CLEARS IT. The stacks are derived from the clock rather than
	// stored, so one sample at a new place stops the damage entirely.
	Player.Walk(5.0f);
	const float AfterMoving = Player.Read(Vital::GetHealthAttribute());
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	TestEqual(TEXT("moving stops the damage at once"),
			  Player.Read(Vital::GetHealthAttribute()), AfterMoving, 0.01f);

	// A FLOOR WITHOUT THE ROW TAKES NOTHING, which is what says the beat reads
	// the floor's list rather than hurting everybody.
	Mode->DungeonModifiers = {Starvation};
	Mode->FloorNumber = 1;
	Mode->BuildFloor();
	Player.Walk(0.0f);

	// THE BASELINE IS TAKEN AFTER A BEAT HAS ALREADY RUN ON THE NEW FLOOR, and
	// this is the correction to a first version of this test that failed.
	// Changing the floor applies its rules, Starvation lowers maximum health, and
	// the stat refresh that follows moves the health attribute -- upwards here,
	// by 3.83 -- so a baseline read on the line after `BuildFloor` captures a
	// number that is still settling. The test then reported the rule taking
	// NEGATIVE damage. Reading it after one beat measures what the beat does and
	// nothing else, which is the only thing this assertion is about.
	CataclysmTestWorld::RunClock(World, 10.0f);
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	const float OnAnotherFloor = Player.Read(Vital::GetHealthAttribute());

	CataclysmTestWorld::RunClock(World, 10.0f);
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	TestEqual(TEXT("a floor without Forced March takes nothing"),
			  Player.Read(Vital::GetHealthAttribute()), OnAnotherFloor, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierCleanseTest,
	"Cataclysm.DungeonModifierEffects.ABossDeathCleansesTheNihilsEmbrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierCleanseTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierEffectsTest;
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode =
		World->SpawnActor<ACataclysmDungeonGameMode>();
	const FPossessedPlayer Player(World);
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("a possessed player with an ability system"),
					 Player.IsUsable()))
	{
		return false;
	}

	// THE GAME MODE'S OWN StartPlay BINDS THE DEATH HANDLER, and a test world
	// never calls it, so the binding is made the way StartPlay makes it.
	if (UCataclysmCombatEvents* Announcer = UCataclysmCombatEvents::In(World))
	{
		Mode->StartPlay();
		TestTrue(TEXT("the announcer exists"), Announcer != nullptr);
	}

	Mode->DungeonModifiers = {NihilsEmbrace};
	Mode->FloorNumber = 1;
	if (!TestNotNull(TEXT("the floor was built"), Mode->BuildFloor()))
	{
		return false;
	}

	// WALK FAR ENOUGH TO LOSE SOMETHING, then let the beat apply it.
	Player.Walk(0.0f);
	const float Enough =
		Effects::NihilsEmbraceMetresPerResistancePercent * 3.0f;
	Player.Walk(Enough);
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);

	const FCataclysmStatModifier* Lost =
		DungeonRuleOn(Player.AbilitySystem,
					  *UCataclysmItemModifiers::ResistanceStatFor(
						  TEXT("War")).ToString());
	if (!TestNotNull(TEXT("walking cost some War resistance"), Lost))
	{
		return false;
	}
	TestTrue(TEXT("and the loss is a negative multiplier"), Lost->Value < 0.0f);

	// A COMMON CREATURE'S DEATH CLEANSES NOTHING. The row asks for "a high tier
	// enemy", and which rung that means is a judgement recorded in
	// docs/DECISIONS.md: the boss test, rarity 4 and 5.
	ACataclysmEnemyCharacter* Common = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector(500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a common creature spawned"), Common))
	{
		return false;
	}
	Common->SetRarityStep(0);
	UCataclysmSkillEffects::ApplyHit(Player.Character, Common, 100000.0f);
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);

	const FCataclysmStatModifier* StillLost =
		DungeonRuleOn(Player.AbilitySystem,
					  *UCataclysmItemModifiers::ResistanceStatFor(
						  TEXT("War")).ToString());
	if (TestNotNull(TEXT("a Common's death left the loss alone"), StillLost))
	{
		TestTrue(TEXT("still a loss"), StillLost->Value < 0.0f);
	}

	// A BOSS'S DEATH GIVES EVERY POINT BACK AND GRANTS THE REWARD. Killed with a
	// real blow so the announcement travels the path the game uses, through
	// UCataclysmSkillEffects::MarkDead.
	//
	// SPAWNED WITH COLLISION HANDLING SET, and this is the correction to a first
	// version that failed. The Common above is still standing, creatures carry a
	// capsule, and the default handling refuses a spawn whose place is blocked --
	// so `SpawnActor` answered null and the test reported "a boss spawned" as the
	// failure, which names the symptom and not the cause. Asking the engine to
	// adjust the location says what the test actually needs: a boss somewhere,
	// not a boss at one exact point. A larger distance chosen by eye would work
	// today and break when somebody changes a capsule radius.
	FActorSpawnParameters BossSpawn;
	BossSpawn.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ACataclysmEnemyCharacter* Boss = World->SpawnActor<ACataclysmEnemyCharacter>(
		ACataclysmEnemyCharacter::StaticClass(), FVector(900.0f, 0.0f, 0.0f),
		FRotator::ZeroRotator, BossSpawn);
	if (!TestNotNull(TEXT("a boss spawned"), Boss))
	{
		return false;
	}
	Boss->SetRarityStep(ACataclysmEnemyCharacter::FirstBossRarityStep);
	TestTrue(TEXT("and it really is a boss"), Boss->IsBoss());

	UCataclysmSkillEffects::ApplyHit(Player.Character, Boss, 100000.0f);
	TestTrue(TEXT("the boss died"), UCataclysmSkillEffects::IsDead(Boss));

	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);

	const FCataclysmStatModifier* After =
		DungeonRuleOn(Player.AbilitySystem,
					  *UCataclysmItemModifiers::ResistanceStatFor(
						  TEXT("War")).ToString());
	if (TestNotNull(TEXT("the cleansed player still carries a modifier"), After))
	{
		TestTrue(TEXT("and it is a gain now, not a loss"), After->Value > 0.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierEmbraceStacksTest,
	"Cataclysm.DungeonModifierEffects.EmbraceOfDeathArrivesEveryTenSecondsToFive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierEmbraceStacksTest::RunTest(const FString& Parameters)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// THE ARITHMETIC WITH NO WORLD AT ALL, which is why every figure here is
	// typed by hand. Issue #41, slice 5. Death's Embrace states no number of its
	// own, so all three are judgements: 10 percentage points a stack, 5 stacks
	// at most, one stack every 10 seconds.

	// NOTHING FOR THE FIRST TEN SECONDS OF A FLOOR. A player who takes the
	// stairs promptly never carries a stack.
	TestEqual(TEXT("a floor just entered carries no stacks"),
			  Effects::DeathsEmbraceStacksAfter(0.0f), 0);
	TestEqual(TEXT("and nine seconds still carries none"),
			  Effects::DeathsEmbraceStacksAfter(9.0f), 0);
	TestEqual(TEXT("nor does a tenth of a second short of ten"),
			  Effects::DeathsEmbraceStacksAfter(9.9f), 0);

	// THEN ONE EVERY TEN SECONDS, COUNTED DOWN TO WHOLE STACKS.
	TestEqual(TEXT("ten seconds is one stack"),
			  Effects::DeathsEmbraceStacksAfter(10.0f), 1);
	TestEqual(TEXT("and nineteen is still one"),
			  Effects::DeathsEmbraceStacksAfter(19.9f), 1);
	TestEqual(TEXT("thirty seconds is three"),
			  Effects::DeathsEmbraceStacksAfter(30.0f), 3);

	// AND FIVE IS THE MOST, HOWEVER LONG THE PLAYER LINGERS. Fifty seconds
	// reaches it, which is about one floor's fighting.
	TestEqual(TEXT("fifty seconds reaches the cap of five"),
			  Effects::DeathsEmbraceStacksAfter(50.0f), 5);
	TestEqual(TEXT("and ten minutes is still five"),
			  Effects::DeathsEmbraceStacksAfter(600.0f), 5);

	// A NEGATIVE TIME CARRIES NOTHING, the same answer the movement conditions
	// give for "no character to read".
	TestEqual(TEXT("a negative time carries no stacks"),
			  Effects::DeathsEmbraceStacksAfter(-5.0f), 0);

	// WHAT THE STACKS TAKE OFF HEALING, in percentage points.
	TestEqual(TEXT("no stacks take nothing"),
			  Effects::DeathsEmbraceHealingLessPercent(0), 0.0f, 0.01f);
	TestEqual(TEXT("one stack takes ten"),
			  Effects::DeathsEmbraceHealingLessPercent(1), 10.0f, 0.01f);
	TestEqual(TEXT("three stacks take thirty"),
			  Effects::DeathsEmbraceHealingLessPercent(3), 30.0f, 0.01f);
	TestEqual(TEXT("five stacks take fifty, which is the worst it gets"),
			  Effects::DeathsEmbraceHealingLessPercent(5), 50.0f, 0.01f);

	// AND THE SHARE IS CLAMPED HERE AS WELL AS IN THE COUNT, because this is
	// public and a caller holding a stack count from elsewhere must not be able
	// to ask for more than the cap.
	TestEqual(TEXT("a count past the cap still takes only fifty"),
			  Effects::DeathsEmbraceHealingLessPercent(99), 50.0f, 0.01f);
	TestEqual(TEXT("and a negative count takes nothing"),
			  Effects::DeathsEmbraceHealingLessPercent(-3), 0.0f, 0.01f);

	// FIFTY IS NOT A HUNDRED, SAID AS AN ASSERTION. At the cap a cursed player
	// is healed half as fast rather than not at all, which is what keeps the row
	// a hazard rather than a wall.
	TestTrue(TEXT("even the cap leaves half the healing arriving"),
			 Effects::DeathsEmbraceHealingLessPercent(
				 Effects::DeathsEmbraceMostStacks) < 100.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierEmbraceBeatTest,
	"Cataclysm.DungeonModifierEffects.DeathsEmbraceCutsHealingAndTheStairsClearIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierEmbraceBeatTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierEffectsTest;
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode =
		World->SpawnActor<ACataclysmDungeonGameMode>();
	const FPossessedPlayer Player(World);
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("a possessed player with an ability system"),
					 Player.IsUsable()))
	{
		return false;
	}

	Mode->DungeonModifiers = {DeathsEmbrace};
	Mode->FloorNumber = 1;
	if (!TestNotNull(TEXT("the floor was built"), Mode->BuildFloor()))
	{
		return false;
	}
	TestTrue(TEXT("the floor carries Death's Embrace"),
			 Mode->FloorBrief.Modifiers.Contains(DeathsEmbrace));

	// THE BEAT IS COUNTED IN BEATS AND NOT IN WORLD TIME, so a stack arrives
	// after forty of them rather than after one Tick carrying ten seconds. That
	// is the same convention Forced March's per-beat share uses.
	const auto Beats = [Mode](int32 Count)
	{
		for (int32 Beat = 0; Beat < Count; ++Beat)
		{
			Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
		}
	};
	const auto Reduction = [&Player]()
	{
		return Player.Read(Vital::GetHealingReceivedReductionAttribute());
	};

	// A FLOOR JUST ENTERED CUTS NOTHING, asserted first so every figure below is
	// evidence of the rule rather than of a character that started cursed.
	Beats(1);
	TestEqual(TEXT("a floor just entered cuts no healing"),
			  Reduction(), 0.0f, 0.01f);

	// AND STILL NOTHING A BEAT SHORT OF TEN SECONDS. Thirty-nine beats is 9.75
	// seconds, which is the assertion that says the interval is read rather than
	// a stack being granted on the first beat.
	Beats(38);
	TestEqual(TEXT("thirty-nine beats is short of ten seconds and cuts nothing"),
			  Reduction(), 0.0f, 0.01f);

	// THE FORTIETH BEAT IS TEN SECONDS EXACTLY, and a quarter is a power of two
	// so forty of them is ten with no rounding to argue about.
	Beats(1);
	TestEqual(TEXT("ten seconds on the floor is one stack, cutting ten"),
			  Reduction(), Effects::DeathsEmbracePercentPerStack, 0.01f);

	// IT GROWS WHILE THE PLAYER STAYS. Thirty seconds is three stacks.
	Beats(80);
	TestEqual(TEXT("thirty seconds is three stacks, cutting thirty"),
			  Reduction(), 3.0f * Effects::DeathsEmbracePercentPerStack, 0.01f);

	// AND STOPS AT THE CAP. Fifty seconds reaches five stacks, and two hundred
	// more beats change nothing.
	Beats(80);
	const float AtCap =
		Effects::DeathsEmbraceMostStacks * Effects::DeathsEmbracePercentPerStack;
	TestEqual(TEXT("fifty seconds reaches the cap"), Reduction(), AtCap, 0.01f);

	Beats(200);
	TestEqual(TEXT("and lingering past it changes nothing"),
			  Reduction(), AtCap, 0.01f);

	// THE STAIRS CLEAR THEM, WHICH THE ROW ASKS FOR OUTRIGHT: "Stacks reset when
	// entering a new floor." This is the one part of this rule the data promises
	// rather than the code needing, so it is asserted on its own.
	// THROUGH `GoToFloor` AND NOT `BuildFloor`, AND THE FIRST VERSION OF THIS
	// TEST GOT IT WRONG. `BuildFloor` is an internal step: it rebuilds the floor
	// brief and never calls `ApplyFloorRulesToPlayer`, which is what holds the
	// reset. Its only caller outside the tests is `GoToFloor`, which calls it
	// and then applies the floor's rules -- so taking the stairs always resets,
	// and a test driving `BuildFloor` was exercising a path play never takes. It
	// failed, and it failed for the test's reason rather than the code's.
	//
	// THE SETUP ABOVE STILL USES `BuildFloor` ON PURPOSE, because all it needs
	// is a floor whose brief carries the row, which is what `BuildFloor` does.
	// The difference between the two calls is exactly what this assertion is
	// about.
	if (!TestTrue(TEXT("the second floor was reached"), Mode->GoToFloor(2)))
	{
		return false;
	}
	TestEqual(TEXT("a new floor clears every stack"), Reduction(), 0.0f, 0.01f);

	// AND THEY BUILD UP AGAIN FROM NOTHING, so the reset clears the clock as
	// well as the count. Clearing only the count would put a stack back on the
	// very next beat, because the clock would still read past fifty seconds.
	Beats(38);
	TestEqual(TEXT("and the new floor's clock starts from nothing too"),
			  Reduction(), 0.0f, 0.01f);
	Beats(2);
	TestEqual(TEXT("then earns its first stack ten seconds in"),
			  Reduction(), Effects::DeathsEmbracePercentPerStack, 0.01f);

	// A FLOOR WITHOUT THE ROW CUTS NOTHING, which is what says the beat reads the
	// floor's list rather than cursing everybody.
	Mode->DungeonModifiers = {Starvation};
	if (!TestTrue(TEXT("the third floor was reached"), Mode->GoToFloor(3)))
	{
		return false;
	}
	Beats(200);
	TestEqual(TEXT("a floor without Death's Embrace cuts no healing"),
			  Reduction(), 0.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierTwoBeatRulesTest,
	"Cataclysm.DungeonModifierEffects.AFloorCarryingBothBeatRulesKeepsBothOfThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierTwoBeatRulesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierEffectsTest;
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	// THIS IS THE TEST THAT FAILS WITHOUT ONE APPLIER, and it is worth saying
	// what it catches. Issue #41, slice 5.
	// `UCataclysmDungeonModifierEffects::ApplyToCharacter` replaces the WHOLE set
	// of dungeon stat modifiers. Slice 2's `StepNihilsEmbrace` assembled its own
	// effects and set only the two resistance fields, which was correct while it
	// was the only rule working on the beat. A second such rule written the same
	// way zeroes the first's field every time it applies, so the two undo each
	// other four times a second and which one survives depends on the order they
	// are called in. Nothing about either rule on its own would show it.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode =
		World->SpawnActor<ACataclysmDungeonGameMode>();
	const FPossessedPlayer Player(World);
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("a possessed player with an ability system"),
					 Player.IsUsable()))
	{
		return false;
	}

	// A FLOOR CARRYING STARVATION AS WELL, so the floor's own per-floor rule is
	// in the same assertion: it is the field neither beat rule writes, and a
	// rebuild that dropped it would be the same class of fault.
	Mode->DungeonModifiers = {NihilsEmbrace, DeathsEmbrace, Starvation};
	Mode->FloorNumber = 1;
	if (!TestNotNull(TEXT("the floor was built"), Mode->BuildFloor()))
	{
		return false;
	}

	// WALK FAR ENOUGH FOR THE NIHIL'S EMBRACE TO TAKE SOMETHING, and stay long
	// enough for Death's Embrace to grant stacks. Forty beats is ten seconds.
	Player.Walk(0.0f);
	Player.Walk(Effects::NihilsEmbraceMetresPerResistancePercent * 3.0f);
	for (int32 Beat = 0; Beat < 40; ++Beat)
	{
		Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	}

	// BOTH ARE ON THE CHARACTER AT ONCE. Either assertion alone passes with the
	// broken shape, depending on the call order, and that is exactly why both
	// are here.
	TestEqual(TEXT("Death's Embrace is cutting the player's healing"),
			  Player.Read(Vital::GetHealingReceivedReductionAttribute()),
			  Effects::DeathsEmbracePercentPerStack, 0.01f);

	const FCataclysmStatModifier* Lost =
		DungeonRuleOn(Player.AbilitySystem,
					  *UCataclysmItemModifiers::ResistanceStatFor(
						  TEXT("War")).ToString());
	if (TestNotNull(TEXT("and The Nihil's Embrace still has its resistance"),
					Lost))
	{
		TestTrue(TEXT("as a loss"), Lost->Value < 0.0f);
	}

	// AND THE FLOOR'S OWN RULE SURVIVED BOTH OF THEM.
	const FCataclysmStatModifier* Starved =
		DungeonRuleOn(Player.AbilitySystem, TEXT("max_health"));
	if (TestNotNull(TEXT("and Starvation still has its maximum health"),
					Starved))
	{
		TestTrue(TEXT("as a loss"), Starved->Value < 0.0f);
	}

	// WALKING FURTHER MOVES ONE AND LEAVES THE OTHER, which is the interleaving
	// the broken shape gets wrong on a single beat rather than over time.
	Player.Walk(Effects::NihilsEmbraceMetresPerResistancePercent * 3.0f);
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);

	TestEqual(TEXT("a beat that only moved the resistance kept the healing cut"),
			  Player.Read(Vital::GetHealingReceivedReductionAttribute()),
			  Effects::DeathsEmbracePercentPerStack, 0.01f);

	// AND A SECOND STACK LEAVES THE RESISTANCE WHERE IT WAS.
	for (int32 Beat = 0; Beat < 40; ++Beat)
	{
		Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	}
	TestEqual(TEXT("a second stack arrived"),
			  Player.Read(Vital::GetHealingReceivedReductionAttribute()),
			  2.0f * Effects::DeathsEmbracePercentPerStack, 0.01f);

	const FCataclysmStatModifier* StillLost =
		DungeonRuleOn(Player.AbilitySystem,
					  *UCataclysmItemModifiers::ResistanceStatFor(
						  TEXT("War")).ToString());
	if (TestNotNull(TEXT("and the resistance loss is still there"), StillLost))
	{
		TestTrue(TEXT("as a loss"), StillLost->Value < 0.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierEffectsRealRowsTest,
	"Cataclysm.DungeonModifierEffects.EveryRuleNamesARowOfTheTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierEffectsRealRowsTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = UCataclysmDungeonModifierTable::LoadDungeonModifierTable();
	if (!Table)
	{
		AddError(TEXT("DT_DungeonModifiers would not load."));
		return false;
	}

	// A RULE KEYED BY A NAME THAT IS NOT A ROW NEVER FIRES, and nothing else
	// would say so: the floor would simply never carry it.
	const TArray<FName> Keys = UCataclysmDungeonModifierEffects::KeysWithARule();
	TestTrue(TEXT("some rules exist"), Keys.Num() > 0);
	for (const FName Key : Keys)
	{
		TestNotNull(FString::Printf(TEXT("%s is a row of the table"), *Key.ToString()),
					UCataclysmDungeonModifierTable::FindRow(Table, Key));
		TestTrue(FString::Printf(TEXT("%s is marked as having something built"),
								 *Key.ToString()),
				 UCataclysmDungeonModifierEffects::BuiltStateOf(Key)
					 != ECataclysmModifierBuilt::NotBuilt);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
