// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmGroundZone.h"
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
#include "Dungeon/CataclysmFloorHazardSource.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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
	 * And the one that changes the FLOOR rather than the player. Issues #1605
	 * and #41.
	 */
	const FName InfernalRain(TEXT("Demonic_Infernal_Rain"));

	/** And the one that changes both: it places actors AND moves a stat. */
	const FName SingularityWells(TEXT("Void_Singularity_Wells"));

	/** And the one whose patches are placed by a death rather than a clock. */
	const FName WitheredGround(TEXT("Famine_Withered_Ground"));

	/**
	 * And the one that saps health faster the deeper the floor is, which a kill
	 * slows. Issues #1786 and #41.
	 */
	const FName MortalDecay(TEXT("Death_Mortal_Decay"));

	/**
	 * And the one an enemy's blow stacks onto the player, which the stairs do not
	 * cure. Issues #1786 and #41.
	 */
	const FName WastingSickness(TEXT("Famine_Wasting_Sickness"));

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

	/**
	 * A creature placed clear of whatever is already standing there, able to land
	 * a blow that actually hurts. Issues #1786 and #41.
	 *
	 * THE ATTACK DAMAGE IS THE WHOLE REASON THIS EXISTS, and leaving it out cost
	 * a build cycle: `UCataclysmCombatAttributeSet` starts `AttackDamage` at 0 --
	 * its own comment says it is "supplied by the equipped weapon" -- and a
	 * creature spawned bare has no weapon. `UCataclysmSkillEffects::ApplyHit`
	 * scales its percentage by `WeaponDamageOf` the source, so every blow such a
	 * creature lands deals nothing, whatever percentage is asked for. Two tests
	 * here failed on "the creature's blow landed on the player" for exactly that.
	 *
	 * A HUNDRED, WHICH IS THE FIGURE THE REST OF THE PROJECT'S TESTS USE.
	 * `FScopedFighter` in `CataclysmSkillTemplateTests.cpp` sets the same
	 * attribute the same way.
	 *
	 * SPAWNED WITH COLLISION HANDLING SET, because creatures carry a capsule and
	 * the default handling refuses a spawn whose place is blocked -- which
	 * answers null and reports as "no creature spawned", naming the symptom
	 * rather than the cause. The cleanse test above records losing time to it.
	 */
	ACataclysmEnemyCharacter* SpawnCreatureThatCanHit(UWorld* World, float AlongX)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ACataclysmEnemyCharacter* Enemy = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(),
			FVector(AlongX, 0.0f, 0.0f), FRotator::ZeroRotator, Spawn);
		if (!Enemy)
		{
			return nullptr;
		}

		if (UAbilitySystemComponent* System = Enemy->GetAbilitySystemComponent())
		{
			System->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}
		return Enemy;
	}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFieldMedicBuiltTest,
	"Cataclysm.DungeonModifierEffects.TheFieldMedicRowIsBuiltNowThatItDoesNotAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFieldMedicBuiltTest::RunTest(const FString& Parameters)
{
	// THIS TEST ASSERTED `Partly` UNTIL ISSUE #1680, AND CHANGING IT IS THE
	// POINT OF IT. It was written to be revisited: its previous comment said
	// marking the row Built while a creature could still swing would put a
	// wrong answer in the one place the project asks what is finished.
	//
	// **This is not a test weakened to fit a change.** The row has two halves
	// -- "constantly heals all other enemies in a large radius" and "it does
	// not attack" -- and both are now built. The second half is why this line
	// moved.
	TestEqual(TEXT("the Field Medic row is built"),
			  static_cast<int32>(UCataclysmDungeonModifierEffects::BuiltStateOf(
				  FName(UCataclysmDungeonModifierEffects::FieldMedicKey))),
			  static_cast<int32>(ECataclysmModifierBuilt::Built));

	// AND A CONTROL IN EACH DIRECTION, so "built" is not simply what this
	// returns for everything. Two rows are partly built and one has no rule at
	// all. This sentence said "one row" until Infernal Rain became the second.
	TestEqual(TEXT("Unstable Dimensions is still only partly built"),
			  static_cast<int32>(UCataclysmDungeonModifierEffects::BuiltStateOf(
				  FName(FCataclysmDungeonFloorRules::UnstableDimensionsKey))),
			  static_cast<int32>(ECataclysmModifierBuilt::Partly));

	// INFERNAL RAIN IS THE SECOND, AND THIS LINE IS WRITTEN TO BE REVISITED the
	// way the Field Medic's was. Its burning ground is built, typed and timed;
	// nothing draws a fireball falling into it, which is the half the row names
	// first. Issue #1699 is that half. When it lands this becomes `Built` and
	// Unstable Dimensions is the only `Partly` control left.
	TestEqual(TEXT("Infernal Rain is partly built: no fireball is drawn"),
			  static_cast<int32>(UCataclysmDungeonModifierEffects::BuiltStateOf(
				  FName(UCataclysmDungeonModifierEffects::InfernalRainKey))),
			  static_cast<int32>(ECataclysmModifierBuilt::Partly));

	// THE NOT-BUILT CONTROL USED TO BE Void_Singularity_Wells AND THAT ROW IS NOW
	// PARTLY BUILT, so it had to be replaced. Chaos_Echo_Chamber takes its place
	// for a stated reason rather than because it happened to be unbuilt.
	//
	// IT IS BLOCKED BY AN OWNER RULE, NOT BY MISSING CODE, which is what makes it
	// a control likely to last. Its row asks for "a ghostly copy of that ability
	// ... it can also hit you, dealing a small amount of damage", and
	// `tools/tests/test_hellhound_matches_the_model.py::test_nothing_burns_its_own_side`
	// records the rule it breaks: "A creature does not burn itself or its own
	// side. Set by the project owner on 2026-08-20 as a general rule." That test
	// also records that the opposite was asserted once and deliberately reversed,
	// so somebody has already tried the other way.
	//
	// "NOT BUILT" AND "NOT BUILDABLE UNDER A STANDING RULE" READ THE SAME HERE AND
	// MEAN DIFFERENT THINGS. Whoever replaces this control next should say which
	// applies to their choice.
	TestEqual(TEXT("and a row blocked by an owner rule is not built at all"),
			  static_cast<int32>(UCataclysmDungeonModifierEffects::BuiltStateOf(
				  FName(TEXT("Chaos_Echo_Chamber")))),
			  static_cast<int32>(ECataclysmModifierBuilt::NotBuilt));

	// AND SINGULARITY WELLS IS THE THIRD PARTLY BUILT ROW, written to be revisited
	// the way the other two were. Its orbs are placed, typed and damaging, and
	// standing in one slows the player by the 40% its row states. Nothing pulls,
	// and the row names the pull first.
	TestEqual(TEXT("Singularity Wells is partly built: nothing pulls"),
			  static_cast<int32>(UCataclysmDungeonModifierEffects::BuiltStateOf(
				  FName(UCataclysmDungeonModifierEffects::SingularityWellsKey))),
			  static_cast<int32>(ECataclysmModifierBuilt::Partly));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEveryBuiltKeyIsListedTest,
	"Cataclysm.DungeonModifierEffects.EveryRowWithSomethingBuiltIsInTheRuleList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEveryBuiltKeyIsListedTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table =
		UCataclysmDungeonModifierTable::LoadDungeonModifierTable();
	if (!Table)
	{
		AddError(TEXT("DT_DungeonModifiers would not load."));
		return false;
	}

	// THE DIRECTION THE OTHER TEST CANNOT CHECK, and the reason this exists.
	// `EveryRuleNamesARowOfTheTable` walks `KeysWithARule` and asks about each
	// entry it finds, so a key that SHOULD be in that list and is not never
	// enters its loop. Death's Embrace was exactly that for a while: returned
	// as Built and absent from the list, with nothing able to see it.
	// Issue #1677.
	const TArray<FName> Listed = UCataclysmDungeonModifierEffects::KeysWithARule();
	int32 Checked = 0;
	for (const FName RowKey : Table->GetRowNames())
	{
		if (UCataclysmDungeonModifierEffects::BuiltStateOf(RowKey)
			== ECataclysmModifierBuilt::NotBuilt)
		{
			continue;
		}

		++Checked;
		TestTrue(FString::Printf(
					 TEXT("%s has something built, so it must be listed as having a "
						  "rule"), *RowKey.ToString()),
				 Listed.Contains(RowKey));
	}

	// AND THE WALK ITSELF HAPPENED. Without this the test passes on a table
	// that loaded with no rows, which is the failure it would least notice.
	TestTrue(TEXT("at least one row has something built"), Checked > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInfernalRainCadenceTest,
	"Cataclysm.DungeonModifierEffects.InfernalRainDropsOnItsCadenceAndStopsAtItsCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInfernalRainCadenceTest::RunTest(const FString& Parameters)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// WHEN INFERNAL RAIN DROPS A PATCH, and what is being held here is the
	// judgement rather than the arithmetic. Issues #1605 and #41.
	//
	// THE ROW STATES ONE FIGURE AND THIS RULE NEEDS FOUR. "Fireballs rain in
	// combat zones, leaving patches of burning ground that deal fire damage over
	// time for 10 seconds" gives the ten seconds; the cadence, the cap, the share
	// and the distance are all judgements recorded in docs/DECISIONS.md. A chosen
	// figure drifts silently, so this is what makes changing one deliberate.
	const float Cadence = Effects::InfernalRainSecondsBetweenPatches;
	const int32 Cap = Effects::InfernalRainMostPatches;

	// NOTHING ON THE FIRST BEAT OF A FLOOR. A quarter of a second is not five.
	TestFalse(TEXT("no patch falls before the cadence has passed"),
		Effects::InfernalRainPatchIsDue(0.25f, 0));
	TestFalse(TEXT("nor a hair before it"),
		Effects::InfernalRainPatchIsDue(Cadence - 0.01f, 0));

	// AT THE FIGURE, NOT PAST IT. The beat is a quarter of a second and the
	// cadence is five, so insisting on strictly past would put every patch one
	// beat later than the figure says, for no reason anybody could observe.
	TestTrue(TEXT("one falls at exactly the cadence"),
		Effects::InfernalRainPatchIsDue(Cadence, 0));
	TestTrue(TEXT("and at any time past it"),
		Effects::InfernalRainPatchIsDue(Cadence * 3.0f, 0));

	// THE CAP HOLDS HOWEVER LONG THE FLOOR HAS WAITED, because the function asks
	// it before it asks the clock.
	TestTrue(TEXT("one below the cap still drops"),
		Effects::InfernalRainPatchIsDue(Cadence, Cap - 1));
	TestFalse(TEXT("at the cap nothing drops"),
		Effects::InfernalRainPatchIsDue(Cadence, Cap));
	TestFalse(TEXT("and a long wait does not defeat the cap"),
		Effects::InfernalRainPatchIsDue(Cadence * 100.0f, Cap));
	TestFalse(TEXT("nor does somehow being above it"),
		Effects::InfernalRainPatchIsDue(Cadence * 100.0f, Cap + 5));

	// AND THE CLOCK IS NOT SWALLOWED BY A FULL FLOOR. This is the half that a
	// test checking only "a full floor drops nothing" would miss: the caller goes
	// on counting while the floor is full, so the beat a patch expires on drops
	// the next one at once rather than starting a fresh five seconds.
	TestTrue(TEXT("the beat a patch expires on drops the next one at once"),
		Effects::InfernalRainPatchIsDue(Cadence * 4.0f, Cap - 1));

	// THE TWO FIGURES THEMSELVES, so a change to either is a change to this test.
	// The cadence must be shorter than the patch life or the patches never
	// overlap and the modifier is one hazard at a time rather than rain. The
	// header states the same thing as a static_assert and this is its runtime
	// twin; the assertion is the one that cannot be skipped, this one is the one
	// that says why when it trips.
	TestTrue(FString::Printf(
		TEXT("the cadence %.1fs is shorter than the patch life %.1fs"),
		Cadence, Effects::InfernalRainPatchSeconds),
		Cadence < Effects::InfernalRainPatchSeconds);
	TestTrue(TEXT("more than one patch may burn at once"), Cap > 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInfernalRainDamageTest,
	"Cataclysm.DungeonModifierEffects.InfernalRainCostsAShareOfMaximumHealthASecond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInfernalRainDamageTest::RunTest(const FString& Parameters)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// WHAT ONE SECOND IN AN INFERNAL RAIN PATCH COSTS. Issues #1605 and #41.
	//
	// A SHARE OF MAXIMUM HEALTH RATHER THAN A FLAT FIGURE, which is what the
	// dungeon modifiers beside it already do: `ForcedMarchPercentPerStackPerSecond`
	// carries the reason in its own comment, that a share means the same thing at
	// every character level.
	//
	// AND DELIBERATELY NOT THE RULE A CREATURE'S BURNING GROUND USES. That one is
	// a share of an ordinary hit, sized so a full stay costs exactly one hit:
	// `ACataclysmGatekeeperCharacter` prices its patch from `WeaponDamageOf` its
	// own ability system. A floor hazard has no weapon damage and carries no
	// attribute sets at all -- deliberately, because the damage calculation reads
	// the defender's attributes and not the source's -- so there is no ordinary
	// hit for that share to be a share of.
	//
	// THE PATCH SWEEPS ONCE A SECOND, so this one figure is both the per-second
	// share and the per-sweep damage, with no conversion to get wrong.

	// A THOUSAND MAXIMUM HEALTH MAKES THE ARITHMETIC READABLE: two per cent is 20.
	const float Expected =
		1'000.0f * Effects::InfernalRainPercentPerSecond / 100.0f;
	TestEqual(TEXT("a second in the fire costs its share of maximum health"),
		Effects::InfernalRainDamagePerSecond(1'000.0f), Expected, 0.01f);

	// IT SCALES WITH THE CHARACTER, which is the whole reason it is a share.
	TestEqual(TEXT("twice the maximum health costs twice as much"),
		Effects::InfernalRainDamagePerSecond(2'000.0f), Expected * 2.0f, 0.01f);

	// A WHOLE STAY COSTS LESS THAN EVERYTHING. The row describes ground to walk
	// out of, not a death sentence for being caught in it once.
	const float WholeStay = Effects::InfernalRainDamagePerSecond(1'000.0f)
		* Effects::InfernalRainPatchSeconds;
	TestTrue(FString::Printf(
		TEXT("a whole stay in one patch costs %.0f of 1000"), WholeStay),
		WholeStay < 1'000.0f);
	TestTrue(TEXT("and costs enough to be worth moving for"), WholeStay > 0.0f);

	// NO MAXIMUM HEALTH MEANS NO DAMAGE, rather than a negative figure reaching
	// the patch and healing whoever stands in it. A zero here produces no patch at
	// all, because the placer refuses a non-positive damage, and that is the right
	// answer for a reading nobody can have.
	TestEqual(TEXT("a character with no maximum health takes nothing"),
		Effects::InfernalRainDamagePerSecond(0.0f), 0.0f, 0.01f);
	TestEqual(TEXT("and a negative reading takes nothing rather than healing"),
		Effects::InfernalRainDamagePerSecond(-500.0f), 0.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInfernalRainBeatTest,
	"Cataclysm.DungeonModifierEffects.AFloorCarryingInfernalRainDropsTypedPatches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInfernalRainBeatTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierEffectsTest;
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	// THE WHOLE RULE, DRIVEN THE WAY THE GAME DRIVES IT. Issues #1605 and #41.
	//
	// THROUGH `Tick` AND NOT BY CALLING THE STEP. `StepInfernalRain` is private,
	// and the header states the reason at `ContinueTheWaveArriving`: a test that
	// called a step directly would prove the step and not that anything in the
	// game ever runs it. This is the same shape the three other beat tests in
	// this file use.
	//
	// WHAT MAKES THIS WORTH WRITING rather than trusting the two arithmetic tests
	// above it. Slice 2 shipped two mechanisms that were never called, and the
	// fault was invisible to every test of their arithmetic. This one asserts
	// that a floor carrying the row produces an actor, that the actor carries the
	// damage type off the row, and that a floor without the row produces nothing.
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

	// ONE BEAT IS ONE `Tick` OF THE INTERVAL, because `Tick` zeroes its counter
	// rather than subtracting the interval from it. The number of beats a cadence
	// takes is derived from the two figures so that changing either keeps this
	// test measuring the cadence rather than a number of beats I typed.
	const auto Beats = [Mode](int32 How)
	{
		for (int32 Index = 0; Index < How; ++Index)
		{
			Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
		}
	};
	const int32 BeatsPerCadence = FMath::CeilToInt(
		Effects::InfernalRainSecondsBetweenPatches
		/ ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	TestTrue(TEXT("a cadence is more than one beat"), BeatsPerCadence > 1);

	const auto CountPatches = [World]()
	{
		int32 Count = 0;
		for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				++Count;
			}
		}
		return Count;
	};
	const auto FirstPatch = [World]() -> ACataclysmGroundZone*
	{
		for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				return *It;
			}
		}
		return nullptr;
	};

	// THE FLOOR WITHOUT THE ROW IS TESTED FIRST, AND ON PURPOSE. Doing it last
	// would need the patches this rule has already dropped cleared out, and
	// `BuildFloor` does not clear them -- `UCataclysmFloorContents::ClearTheFloor`
	// is called by `GoToFloor`. Asking the question on a floor that has never
	// rained is the same question with nothing to unpick.
	Mode->DungeonModifiers = {Starvation};
	Mode->FloorNumber = 1;
	if (!TestNotNull(TEXT("the floor was built"), Mode->BuildFloor()))
	{
		return false;
	}
	Beats(BeatsPerCadence * 2);
	TestEqual(TEXT("a floor without Infernal Rain rains nothing"),
			  CountPatches(), 0);
	TestNull(TEXT("and makes no hazard source at all"),
			 ACataclysmFloorHazardSource::Existing(World));

	// NOW THE FLOOR THAT CARRIES IT. Changing the floor also forgets the clock,
	// through `ApplyFloorRulesToPlayer`, so the cadence below is measured from
	// this line rather than from the beats just spent.
	Mode->DungeonModifiers = {InfernalRain};
	Mode->FloorNumber = 1;
	if (!TestNotNull(TEXT("the raining floor was built"), Mode->BuildFloor()))
	{
		return false;
	}
	TestTrue(TEXT("the floor carries Infernal Rain"),
			 Mode->FloorBrief.Modifiers.Contains(InfernalRain));

	const float MaximumHealth = Player.Read(Vital::GetMaxHealthAttribute());
	TestTrue(TEXT("the player has a maximum health to take a share of"),
			 MaximumHealth > 0.0f);

	// NOTHING UNTIL THE CADENCE HAS PASSED, which is what says the rain is paced
	// rather than being one patch a beat.
	Beats(BeatsPerCadence - 1);
	TestEqual(TEXT("no patch falls before the cadence has passed"),
			  CountPatches(), 0);

	// AND ONE ON THE BEAT IT FALLS DUE.
	Beats(1);
	TestEqual(TEXT("the cadence's beat drops exactly one patch"),
			  CountPatches(), 1);

	ACataclysmGroundZone* Patch = FirstPatch();
	if (!TestNotNull(TEXT("the patch is readable"), Patch))
	{
		return false;
	}

	// WHAT THE PATCH IS. Its radius and its damage come from the two figures, and
	// its damage is the share of THIS player's maximum health rather than a flat
	// number, which is the difference between this rule and a creature's burning
	// ground.
	TestEqual(TEXT("the patch is as wide as the figure says"),
			  Patch->RadiusCm, Effects::InfernalRainRadiusCm, 0.01f);
	TestEqual(TEXT("it burns for a share of the player's maximum health"),
			  Patch->DamagePerTick,
			  Effects::InfernalRainDamagePerSecond(MaximumHealth), 0.01f);

	// AND IT IS TIMED RATHER THAN LASTING THE FLOOR, which matters because five
	// other rows of issue #1605 want the floor-long kind and would look the same
	// from the outside if this flag were wrong.
	TestFalse(TEXT("the patch expires rather than lasting the floor"),
			  Patch->bLastsTheFloor);

	// WHERE IT FELL: near the player, at the player's own height, and leaving the
	// player outside it. `Covers` is the patch's own answer to "is this point
	// inside me", so asking it about the player's feet is the assertion that the
	// row describes ground to walk out of rather than an unavoidable hit, and it
	// is a cross-check: a patch laid at a distance my arithmetic thinks is clear
	// but the patch's own extent does not would fail here and nowhere else.
	//
	// STRICTLY PAST THE RADIUS, NOT AT IT, AND THAT IS LOAD-BEARING.
	// `UCataclysmTargeting::IsInLine` decides who is inside with `<=`, so a patch
	// centred at exactly the radius DOES cover a standing player. The placer takes
	// its nearest distance one centimetre past the radius for that reason, which
	// is what makes the line above guaranteed rather than almost always true.
	const FVector Feet = Player.Character->GetActorLocation();
	const FVector Fell = Patch->GetActorLocation();
	// A DOUBLE TOLERANCE, NOT A FLOAT ONE. `FVector`'s components are doubles in
	// Unreal 5, so `1.0f` here makes `TestEqual` ambiguous between its float and
	// double overloads and the file does not compile. The error names the overloads
	// rather than the literal. `CataclysmDropPickupTests.cpp` compares rectangle
	// edges the same way, with a double literal.
	TestEqual(TEXT("it fell at the player's own height"), Fell.Z, Feet.Z, 1.0);
	TestFalse(TEXT("the player is outside it when it is laid"),
			  Patch->Covers(Feet));
	TestTrue(TEXT("and the patch does cover its own centre"),
			 Patch->Covers(Fell));
	const float Away = FVector::Dist2D(Fell, Feet);
	TestTrue(FString::Printf(TEXT("it fell %.0fcm away, within %.0f"),
							 Away, Effects::InfernalRainFallsWithinCm),
			 Away <= Effects::InfernalRainFallsWithinCm);
	TestTrue(FString::Printf(TEXT("and %.0fcm is past its own radius %.0f"),
							 Away, Effects::InfernalRainRadiusCm),
			 Away > Effects::InfernalRainRadiusCm);

	// THE TYPE, WHICH IS THE WHOLE REASON THE FIRST COMMIT ON THIS BRANCH EXISTS.
	// Untyped hazard damage meets none of the player's eight resistances.
	ACataclysmFloorHazardSource* Source =
		ACataclysmFloorHazardSource::Existing(World);
	if (!TestNotNull(TEXT("the rain made a hazard source"), Source))
	{
		return false;
	}
	TestEqual(TEXT("the hazard is typed Demonic, as its row says"),
			  Source->DamageType, FName(TEXT("Demonic")));

	// WHAT THIS CANNOT TELL APART, said plainly: reading the row's type and
	// splitting the row's KEY on its first underscore. `Demonic_Infernal_Rain`
	// begins with its own type, as every key in the table does, so both produce
	// "Demonic" here. The test for that is the row-reading itself, in the
	// implementation, and a row retyped in the workbook is what would show the
	// difference. This assertion is worth keeping anyway: it is what fails if
	// nothing sets the type at all, which is the fault that was there before.

	// THE CAP. Four cadences' worth of beats with nothing expiring, because
	// `Mode->Tick` does not move the world's clock and a patch dies on a timer.
	Beats(BeatsPerCadence * (Effects::InfernalRainMostPatches + 2));
	TestEqual(TEXT("the rain stops at its cap however long the floor goes on"),
			  CountPatches(), Effects::InfernalRainMostPatches);

	// AND IT RESUMES WHEN THEY BURN OUT. This is what says the rule asks what is
	// still alight rather than remembering what it lit: the list holds weak
	// pointers and a patch destroying itself is the expiry. `RunClock` moves the
	// world's clock and the timer manager and ticks no actor, so it expires the
	// patches without adding a beat.
	CataclysmTestWorld::RunClock(World, Effects::InfernalRainPatchSeconds + 1.0f);
	TestEqual(TEXT("the patches burn out on their own"), CountPatches(), 0);

	// THE PLAYER IS STILL THERE, asserted so that a failure below is attributed
	// to the right thing. The clock above is the only stretch of this test in
	// which a patch can sweep, and the beat needs a player to find: if the
	// sweeps had killed this one, "the rain did not start again" would be the
	// symptom and the cause would be invisible. The patches are laid no closer
	// than their own radius, so this is expected to hold rather than being a
	// tolerance.
	if (!TestTrue(TEXT("the player survived the patches burning out"),
				  IsValid(Player.Character)))
	{
		return false;
	}

	Beats(BeatsPerCadence);
	TestTrue(TEXT("and the rain starts again once there is room"),
			 CountPatches() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWellCadenceTest,
	"Cataclysm.DungeonModifierEffects.SingularityWellsAppearOnTheirCadenceUpToTheirCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWellCadenceTest::RunTest(const FString& Parameters)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// WHEN A FLOOR PLACES ANOTHER VOID ORB. Issues #1605 and #41. The row states
	// no cadence and no count, so both of these figures are judgements and this is
	// what makes changing either deliberate.
	const float Cadence = Effects::SingularityWellsSecondsBetweenWells;
	const int32 Cap = Effects::SingularityWellsMostWells;

	// NOTHING BEFORE THE CADENCE HAS PASSED.
	TestFalse(TEXT("no well appears on the first beat of a floor"),
		Effects::SingularityWellIsDue(0.25f, 0));
	TestFalse(TEXT("nor a hair before the cadence"),
		Effects::SingularityWellIsDue(Cadence - 0.01f, 0));

	// AT THE FIGURE, NOT PAST IT. The beat is a quarter of a second, so insisting
	// on strictly past would put every well one beat late for no observable reason.
	TestTrue(TEXT("one appears at exactly the cadence"),
		Effects::SingularityWellIsDue(Cadence, 0));
	TestTrue(TEXT("and at any time past it"),
		Effects::SingularityWellIsDue(Cadence * 3.0f, 0));

	// THE CAP HOLDS HOWEVER LONG THE FLOOR HAS WAITED, because the function asks
	// it before it asks the clock.
	TestTrue(TEXT("one below the cap still appears"),
		Effects::SingularityWellIsDue(Cadence, Cap - 1));
	TestFalse(TEXT("at the cap nothing appears"),
		Effects::SingularityWellIsDue(Cadence, Cap));
	TestFalse(TEXT("and a long wait does not defeat the cap"),
		Effects::SingularityWellIsDue(Cadence * 100.0f, Cap));
	TestFalse(TEXT("nor does somehow being above it"),
		Effects::SingularityWellIsDue(Cadence * 100.0f, Cap + 5));

	// AND A FULL FLOOR DOES NOT SWALLOW THE CLOCK. This is the half a test
	// checking only "a full floor places nothing" would miss: the caller keeps
	// counting while the floor is full, so the beat a well is destroyed on places
	// the next one at once rather than starting a fresh eight seconds.
	TestTrue(TEXT("the beat a well is destroyed on places the next at once"),
		Effects::SingularityWellIsDue(Cadence * 4.0f, Cap - 1));

	// THE CAP IS MORE THAN ONE, because the row says orbs rather than an orb, and
	// it is small, because the coverage arithmetic is what makes the slow
	// avoidable. Three wells of this radius cover 18.8% of the circle they can
	// land in; the header carries the working.
	TestTrue(TEXT("more than one well may exist at once"), Cap > 1);
	TestTrue(TEXT("and few enough to leave most of the floor clear"), Cap <= 5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWellDamageTest,
	"Cataclysm.DungeonModifierEffects.ASingularityWellCostsLessASecondThanBurningGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWellDamageTest::RunTest(const FString& Parameters)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// WHAT ONE SECOND IN A VOID ORB COSTS. Issues #1605 and #41.
	//
	// A SHARE OF MAXIMUM HEALTH, which is what every dungeon modifier here uses,
	// so one figure means the same thing at every character level.

	// A THOUSAND MAXIMUM HEALTH MAKES THE ARITHMETIC READABLE: one per cent is 10.
	const float Expected =
		1'000.0f * Effects::SingularityWellsPercentPerSecond / 100.0f;
	TestEqual(TEXT("a second in a well costs its share of maximum health"),
		Effects::SingularityWellDamagePerSecond(1'000.0f), Expected, 0.01f);

	// IT SCALES WITH THE CHARACTER, which is the whole reason it is a share.
	TestEqual(TEXT("twice the maximum health costs twice as much"),
		Effects::SingularityWellDamagePerSecond(2'000.0f), Expected * 2.0f, 0.01f);

	// AND IT IS THE SMALLEST OF THE HAZARDS HERE, which is the assertion this test
	// exists for. A well slows and is meant to pull as well as damaging, so its
	// damage is the least of what it does; Infernal Rain's patches only damage.
	// Diablo IV's pulling affix states the same shape in words -- it "deals light
	// damage and pulls in players".
	TestTrue(FString::Printf(
		TEXT("a well costs %.0f a second where burning ground costs %.0f"),
		Effects::SingularityWellDamagePerSecond(1'000.0f),
		Effects::InfernalRainDamagePerSecond(1'000.0f)),
		Effects::SingularityWellDamagePerSecond(1'000.0f)
			< Effects::InfernalRainDamagePerSecond(1'000.0f));

	// NO MAXIMUM HEALTH MEANS NO DAMAGE, rather than a negative figure reaching
	// the well. The rule that places them refuses a non-positive damage rather
	// than relying on the ground zone to notice.
	TestEqual(TEXT("a character with no maximum health takes nothing"),
		Effects::SingularityWellDamagePerSecond(0.0f), 0.0f, 0.01f);
	TestEqual(TEXT("and a negative reading takes nothing rather than healing"),
		Effects::SingularityWellDamagePerSecond(-500.0f), 0.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWellBeatTest,
	"Cataclysm.DungeonModifierEffects.AVoidOrbSlowsAPlayerStandingInItAndStopsWhenTheyLeave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWellBeatTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierEffectsTest;
	using Effects = UCataclysmDungeonModifierEffects;
	using Combat = UCataclysmCombatAttributeSet;

	// THE WHOLE RULE, DRIVEN THE WAY THE GAME DRIVES IT. Issues #1605 and #41.
	//
	// WHAT THIS PROVES THAT THE TWO TESTS ABOVE CANNOT. Slice 2 of issue #41
	// shipped two mechanisms nothing ever called, and no test of their arithmetic
	// could have seen it. This asserts that a floor carrying the row produces an
	// orb, that standing in one changes the speed the character walks at, and that
	// walking out puts it back.
	//
	// IT NEVER REFRESHES THE ATTRIBUTES ITSELF. `ApplyToCharacter` calls
	// `UCataclysmEquipmentComponent::RefreshAttributes`, so the rule is the only
	// thing that writes the attribute this test reads. A test that refreshed them
	// would pass with the rule's own apply deleted.
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

	const auto Beats = [Mode](int32 How)
	{
		for (int32 Index = 0; Index < How; ++Index)
		{
			Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
		}
	};
	const int32 BeatsPerCadence = FMath::CeilToInt(
		Effects::SingularityWellsSecondsBetweenWells
		/ ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);

	const auto CountWells = [World]()
	{
		int32 Count = 0;
		for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				++Count;
			}
		}
		return Count;
	};
	const auto FirstWell = [World]() -> ACataclysmGroundZone*
	{
		for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				return *It;
			}
		}
		return nullptr;
	};

	// A FLOOR WITHOUT THE ROW FIRST, while nothing has been placed yet, so the
	// question needs nothing unpicked afterwards.
	Mode->DungeonModifiers = {Starvation};
	Mode->FloorNumber = 1;
	if (!TestNotNull(TEXT("the floor was built"), Mode->BuildFloor()))
	{
		return false;
	}
	Beats(BeatsPerCadence * 2);
	TestEqual(TEXT("a floor without Singularity Wells places none"),
			  CountWells(), 0);
	TestNull(TEXT("and makes no hazard source at all"),
			 ACataclysmFloorHazardSource::Existing(World));

	// NOW THE FLOOR THAT CARRIES IT. Changing the floor also forgets the clock,
	// through `ApplyFloorRulesToPlayer`.
	Mode->DungeonModifiers = {SingularityWells};
	Mode->FloorNumber = 1;
	if (!TestNotNull(TEXT("the raining floor was built"), Mode->BuildFloor()))
	{
		return false;
	}
	TestTrue(TEXT("the floor carries Singularity Wells"),
			 Mode->FloorBrief.Modifiers.Contains(SingularityWells));

	// THE SPEED BEFORE ANY WELL EXISTS, which is what the slow is measured against.
	const float FullSpeed = Player.Read(Combat::GetMovementSpeedAttribute());
	TestTrue(TEXT("the player has a walking speed to lose"), FullSpeed > 0.0f);

	// NOTHING UNTIL THE CADENCE HAS PASSED.
	Beats(BeatsPerCadence - 1);
	TestEqual(TEXT("no well appears before the cadence"), CountWells(), 0);
	TestEqual(TEXT("and the player is not slowed yet"),
			  Player.Read(Combat::GetMovementSpeedAttribute()), FullSpeed, 0.01f);

	// AND ONE ON THE BEAT IT FALLS DUE.
	Beats(1);
	TestEqual(TEXT("the cadence's beat places exactly one well"), CountWells(), 1);

	ACataclysmGroundZone* Well = FirstWell();
	if (!TestNotNull(TEXT("the well is readable"), Well))
	{
		return false;
	}

	// WHAT THE WELL IS: the stated width, the share of maximum health, and lasting
	// the floor rather than expiring, which is the difference from burning ground.
	TestEqual(TEXT("the well is as wide as the figure says"),
			  Well->RadiusCm, Effects::SingularityWellsRadiusCm, 0.01f);
	TestEqual(TEXT("it costs a share of the player's maximum health"),
			  Well->DamagePerTick,
			  Effects::SingularityWellDamagePerSecond(
				  Player.Read(UCataclysmVitalAttributeSet::GetMaxHealthAttribute())),
			  0.01f);
	TestTrue(TEXT("and it lasts the floor rather than expiring"),
			 Well->bLastsTheFloor);

	// AND IT IS TYPED VOID, OFF ITS OWN ROW. Without a type this damage would meet
	// none of the player's eight resistances.
	ACataclysmFloorHazardSource* Source =
		ACataclysmFloorHazardSource::Existing(World);
	if (!TestNotNull(TEXT("the rule made a hazard source"), Source))
	{
		return false;
	}
	TestEqual(TEXT("the well is typed Void, as its row says"),
			  Source->DamageType, FName(TEXT("Void")));

	// IT DID NOT APPEAR ON THE PLAYER'S FEET, which is what makes a slow something
	// to walk out of. `Covers` is the well's own answer, and the same test its
	// sweep makes.
	const FVector Feet = Player.Character->GetActorLocation();
	TestFalse(TEXT("the player is outside it when it appears"),
			  Well->Covers(Feet));
	TestEqual(TEXT("so they are not slowed by a well they are not in"),
			  Player.Read(Combat::GetMovementSpeedAttribute()), FullSpeed, 0.01f);

	// NOW STAND IN IT. The well lands at a random angle and distance, so this
	// reads where it went rather than guessing.
	const FVector Centre = Well->GetActorLocation();
	Player.Character->SetActorLocation(Centre);
	TestTrue(TEXT("the player is now inside the well"),
			 Well->Covers(Player.Character->GetActorLocation()));

	Beats(1);
	const float Slowed = Player.Read(Combat::GetMovementSpeedAttribute());
	TestTrue(FString::Printf(
		TEXT("standing in a well slows the player: %.2f from %.2f"),
		Slowed, FullSpeed), Slowed < FullSpeed);

	// AND BY THE SHARE THE ROW STATES. The pipeline multiplies by
	// (1 + Value / 100), so a Less of 40 is times 0.6.
	const float Share = 1.0f - Effects::SingularityWellsSlowPercent / 100.0f;
	TestEqual(FString::Printf(TEXT("and by the row's own %.0f%%"),
							  Effects::SingularityWellsSlowPercent),
			  Slowed, FullSpeed * Share, 0.05f);

	// AND WALKING OUT PUTS IT BACK. This is the half that fails if the rule only
	// sets the slow on the beats it places something: the beat a player leaves a
	// well is a beat on which nothing is placed.
	Player.Character->SetActorLocation(
		Centre + FVector(Effects::SingularityWellsFallsWithinCm * 3.0f, 0.0f, 0.0f));
	TestFalse(TEXT("the player is outside every well again"),
			  Well->Covers(Player.Character->GetActorLocation()));
	Beats(1);
	TestEqual(TEXT("walking out puts the speed back"),
			  Player.Read(Combat::GetMovementSpeedAttribute()), FullSpeed, 0.01f);

	// THE CAP. Enough beats for more wells than the cap allows, with nothing
	// expiring, because these last the floor.
	Beats(BeatsPerCadence * (Effects::SingularityWellsMostWells + 2));
	TestEqual(TEXT("the floor stops at its cap however long it goes on"),
			  CountWells(), Effects::SingularityWellsMostWells);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWitheredGroundTest,
	"Cataclysm.DungeonModifierEffects.ADeathLeavesGroundThatTakesRecoveryFromWhoeverStandsOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWitheredGroundTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierEffectsTest;
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	// THE WHOLE RULE, DRIVEN THE WAY THE GAME DRIVES IT. Issue #41.
	//
	// IT NEVER REFRESHES THE ATTRIBUTES ITSELF. `ApplyToCharacter` calls
	// `UCataclysmEquipmentComponent::RefreshAttributes`, so the rule is the only
	// thing that writes the attributes this test reads. A test that refreshed them
	// would pass with the rule's own apply deleted.
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
	// never calls it, so the binding is made the way StartPlay makes it. This is
	// the same line the boss-cleanse test above needs, for the same reason.
	//
	// LEAVING IT OUT IS WHAT THE FIRST RUN OF THIS TEST DID. Every earlier
	// assertion passed -- the rule is wired into the beat, a floor without the row
	// places nothing, forty beats with nothing dying place nothing -- and then a
	// creature died and no patch appeared, because nothing was listening. The rule
	// was right and the test was one line short.
	Mode->StartPlay();
	if (!TestNotNull(TEXT("the world announces deaths"),
					 UCataclysmCombatEvents::In(World)))
	{
		return false;
	}

	const auto Beat = [Mode]()
	{
		Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	};
	const auto CountPatches = [World]()
	{
		int32 Count = 0;
		for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				++Count;
			}
		}
		return Count;
	};

	// A CREATURE THAT CAN BE KILLED WHEREVER IT IS PUT. The collision override is
	// the correction another test in this file records: creatures carry a capsule
	// and the default handling refuses a blocked spawn, which reads as "no creature
	// spawned" and names the symptom rather than the cause.
	//
	// IT ASSERTS EACH STEP RATHER THAN ONLY THE OUTCOME, and that is the
	// correction to a first version of this test. That version asserted only that
	// a creature spawned, and its failure read "its death leaves exactly one patch
	// to be 1, but it was 0" -- which cannot tell four faults apart: a creature
	// spawned with no health, a blow that did not kill, a death notice that never
	// reached the dungeon game mode, or a patch spawn that refused. A failing test
	// that does not say which side is wrong costs a whole build to find out.
	// WHERE THE LAST CREATURE KILLED BY THE HELPER BELOW WAS STANDING.
	FVector StoodAt = FVector::ZeroVector;
	const auto KillACreatureAt =
		[this, World, &Player, &StoodAt](const FVector& Where)
		-> ACataclysmEnemyCharacter*
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ACataclysmEnemyCharacter* Creature =
			World->SpawnActor<ACataclysmEnemyCharacter>(
				ACataclysmEnemyCharacter::StaticClass(), Where,
				FRotator::ZeroRotator, Spawn);
		if (!TestNotNull(TEXT("a creature spawned"), Creature))
		{
			return nullptr;
		}

		// IT HAS HEALTH TO LOSE. A creature whose maximum health is zero is
		// already dead, and `UCataclysmSkillEffects::MarkDead` refuses a second
		// time -- so no death would be announced and nothing downstream would
		// run, with no error naming the cause.
		UAbilitySystemComponent* Theirs = Creature->GetAbilitySystemComponent();
		if (!TestNotNull(TEXT("the creature has an ability system"), Theirs))
		{
			return nullptr;
		}
		const float Health = Theirs->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute());
		if (!TestTrue(FString::Printf(
						  TEXT("the creature has health to lose: %.1f"), Health),
					  Health > 0.0f))
		{
			return nullptr;
		}

		// WHERE IT ACTUALLY STANDS, READ BEFORE THE BLOW. The spawn above passes
		// `AdjustIfPossibleButAlwaysSpawn`, which lets the engine move the actor
		// when the asked-for spot is blocked -- so the point passed in is a
		// request and not a fact. Read after the blow it would be a dead actor's
		// location, which is a thing this project has been bitten by.
		StoodAt = Creature->GetActorLocation();

		UCataclysmSkillEffects::ApplyHit(Player.Character, Creature, 100000.0f);
		if (!TestTrue(TEXT("the blow killed the creature"),
					  UCataclysmSkillEffects::IsDead(Creature)))
		{
			return nullptr;
		}
		return Creature;
	};

	// A FLOOR WITHOUT THE ROW FIRST, so the negative case needs nothing unpicked.
	Mode->DungeonModifiers = {Starvation};
	Mode->FloorNumber = 1;
	if (!TestNotNull(TEXT("the floor was built"), Mode->BuildFloor()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("a creature to kill on the plain floor"),
					 KillACreatureAt(FVector(900.0f, 0.0f, 0.0f))))
	{
		return false;
	}
	Beat();
	TestEqual(TEXT("a death on a floor without Withered Ground leaves no patch"),
			  CountPatches(), 0);
	TestNull(TEXT("and makes no hazard source at all"),
			 ACataclysmFloorHazardSource::Existing(World));

	// NOW THE FLOOR THAT CARRIES IT. Changing the floor also empties the patch
	// list, through `ApplyFloorRulesToPlayer`.
	Mode->DungeonModifiers = {WitheredGround};
	Mode->FloorNumber = 1;
	if (!TestNotNull(TEXT("the withered floor was built"), Mode->BuildFloor()))
	{
		return false;
	}
	TestTrue(TEXT("the floor carries Withered Ground"),
			 Mode->FloorBrief.Modifiers.Contains(WitheredGround));

	// THE RATES BEFORE ANY PATCH EXISTS, which the reduction is measured against.
	const float FullHealthRegen = Player.Read(Vital::GetHealthRegenAttribute());
	const float FullManaRegen = Player.Read(Vital::GetManaRegenAttribute());
	if (!TestTrue(TEXT("the player has health regeneration to lose"),
				  FullHealthRegen > 0.0f)
		|| !TestTrue(TEXT("and mana regeneration to lose"), FullManaRegen > 0.0f))
	{
		return false;
	}

	// NO CLOCK PLACES ONE. The two hazard rules beside this one drop something on
	// a cadence; this one waits for a death, so a floor where nothing dies stays
	// clear however long it runs.
	for (int32 Index = 0; Index < 40; ++Index)
	{
		Beat();
	}
	TestEqual(TEXT("ten seconds of beats with nothing dying places no patch"),
			  CountPatches(), 0);

	// A DEATH, WELL AWAY FROM THE PLAYER so the patch is somewhere to walk to
	// rather than somewhere they already stand.
	const FVector DiedAt(1500.0f, 0.0f, 0.0f);
	if (!TestNotNull(TEXT("a creature to kill on the withered floor"),
					 KillACreatureAt(DiedAt)))
	{
		return false;
	}
	// THE HAZARD SOURCE FIRST. The rule makes it before it asks for a patch, so
	// its presence says the death reached the handler and its absence says the
	// handler never ran. Without this the next line's 0 means either.
	TestNotNull(TEXT("the death reached the rule, which made a hazard source"),
				ACataclysmFloorHazardSource::Existing(World));
	TestEqual(TEXT("its death leaves exactly one patch"), CountPatches(), 1);

	ACataclysmGroundZone* Patch = nullptr;
	for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			Patch = *It;
			break;
		}
	}
	if (!TestNotNull(TEXT("the patch is readable"), Patch))
	{
		return false;
	}

	// WHAT THE PATCH IS: where the creature died, the house width, lasting the
	// floor, and taking no health -- the first hazard here that does not burn.
	TestTrue(FString::Printf(
				 TEXT("the patch is where the creature stood: %s against %s"),
				 *Patch->GetActorLocation().ToCompactString(),
				 *StoodAt.ToCompactString()),
			 Patch->GetActorLocation().Equals(StoodAt, 1.0));
	TestEqual(TEXT("and as wide as the figure says"),
			  Patch->RadiusCm, Effects::WitheredGroundPatchRadiusCm, 0.01f);
	TestTrue(TEXT("and lasts the floor rather than expiring"),
			 Patch->bLastsTheFloor);
	TestEqual(TEXT("and takes no health, which the row does not ask for"),
			  Patch->DamagePerTick, 0.0f, 0.01f);

	// THE PLAYER IS OUTSIDE IT, ASSERTED BEFORE THE BEHAVIOUR IS. A test that only
	// checked "standing on one reduces recovery" would pass whether or not the
	// reduction was scoped to the patch at all, because the fault moves both
	// readings. Saying where the player is, and how far away, makes a failure name
	// the setup rather than sending the reader into the rule.
	const FVector Outside = Player.Character->GetActorLocation();
	TestTrue(FString::Printf(
				 TEXT("the player stands %.0f cm from a patch of radius %.0f"),
				 FVector::Dist(Outside, Patch->GetActorLocation()),
				 Effects::WitheredGroundPatchRadiusCm),
			 !Patch->Covers(Outside));
	Beat();
	TestEqual(TEXT("so a patch they are not on takes no health regeneration"),
			  Player.Read(Vital::GetHealthRegenAttribute()), FullHealthRegen, 0.01f);
	TestEqual(TEXT("and no mana regeneration"),
			  Player.Read(Vital::GetManaRegenAttribute()), FullManaRegen, 0.01f);

	// NOW STAND ON IT.
	Player.Character->SetActorLocation(Patch->GetActorLocation());
	TestTrue(TEXT("the player is now on the patch"),
			 Patch->Covers(Player.Character->GetActorLocation()));

	Beat();
	const float Share = 1.0f - Effects::WitheredGroundRecoveryLessPercent / 100.0f;
	TestEqual(FString::Printf(TEXT("health regeneration falls by the row's %.0f%%"),
							  Effects::WitheredGroundRecoveryLessPercent),
			  Player.Read(Vital::GetHealthRegenAttribute()),
			  FullHealthRegen * Share, 0.05f);
	TestEqual(TEXT("and mana regeneration by the same share"),
			  Player.Read(Vital::GetManaRegenAttribute()),
			  FullManaRegen * Share, 0.05f);

	// AND WALKING OFF PUTS BOTH BACK. This is the half that fails if the rule only
	// sets the reduction on beats where something died: the beat a player steps
	// off a patch is a beat on which nothing was placed.
	Player.Character->SetActorLocation(
		Patch->GetActorLocation()
		+ FVector(Effects::WitheredGroundPatchRadiusCm * 5.0f, 0.0f, 0.0f));
	TestFalse(TEXT("the player is off every patch again"),
			  Patch->Covers(Player.Character->GetActorLocation()));
	Beat();
	TestEqual(TEXT("health regeneration comes back"),
			  Player.Read(Vital::GetHealthRegenAttribute()), FullHealthRegen, 0.01f);
	TestEqual(TEXT("and so does mana regeneration"),
			  Player.Read(Vital::GetManaRegenAttribute()), FullManaRegen, 0.01f);

	// A SECOND DEATH LEAVES A SECOND PATCH, WHICH IS THE ROW'S OWN SENTENCE.
	// "Enemies leave patches of Barren Earth on death" states the trigger and no
	// limit, so a cap would make it stop being true at whichever enemy hit it.
	// This is what fails if somebody adds one.
	if (!TestNotNull(TEXT("a second creature to kill"),
					 KillACreatureAt(FVector(-1500.0f, 0.0f, 0.0f))))
	{
		return false;
	}
	TestEqual(TEXT("a second death leaves a second patch, uncapped"),
			  CountPatches(), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMortalDecayRateTest,
	"Cataclysm.DungeonModifierEffects.MortalDecaySapsFasterWithDepthAndStopsAtItsCeiling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMortalDecayRateTest::RunTest(const FString& Parameters)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// THE RULE ON ITS OWN, WITH NUMBERS TYPED IN. Issues #1786 and #41. No world,
	// no floor and no player, which is what lets this check floor 150 as cheaply
	// as floor 1.

	// NO FLOOR TO READ SAPS NOTHING. Zero and below are "no floor", the same
	// answer `ShareTakenOnFloor` gives the two per-floor rules.
	TestEqual(TEXT("floor 0 saps nothing"),
			  Effects::MortalDecayPercentPerSecond(0, false), 0.0f, 0.0001f);
	TestEqual(TEXT("a negative floor saps nothing"),
			  Effects::MortalDecayPercentPerSecond(-5, false), 0.0f, 0.0001f);

	// FLOOR 1 ALREADY COUNTS, which is the judgement `ShareTakenOnFloor` carries
	// and the reason this rule borrows it rather than repeating the multiply.
	TestEqual(TEXT("floor 1 saps one floor's worth"),
			  Effects::MortalDecayPercentPerSecond(1, false),
			  Effects::MortalDecayPercentPerSecondPerFloor, 0.0001f);
	TestEqual(TEXT("floor 5 saps five floors' worth"),
			  Effects::MortalDecayPercentPerSecond(5, false),
			  5.0f * Effects::MortalDecayPercentPerSecondPerFloor, 0.0001f);

	// THE CEILING IS REACHED, AND IT IS REACHED WHERE THE CONSTANTS SAY. Asserting
	// the capped value alone would pass for a rule that returned the ceiling at
	// every depth, so the floor below it is checked as well and has to be under.
	const int32 CeilingFloor = FMath::RoundToInt(
		Effects::MortalDecayMostPercentPerSecond
		/ Effects::MortalDecayPercentPerSecondPerFloor);
	TestEqual(TEXT("the ceiling arrives at the floor the constants put it on"),
			  Effects::MortalDecayPercentPerSecond(CeilingFloor, false),
			  Effects::MortalDecayMostPercentPerSecond, 0.0001f);
	TestTrue(TEXT("and one floor higher is still below it"),
			 Effects::MortalDecayPercentPerSecond(CeilingFloor - 1, false)
				 < Effects::MortalDecayMostPercentPerSecond);
	TestEqual(TEXT("and a very deep floor is held at the ceiling"),
			  Effects::MortalDecayPercentPerSecond(150, false),
			  Effects::MortalDecayMostPercentPerSecond, 0.0001f);

	// A KILL SLOWS IT RATHER THAN STOPPING IT. The row says "temporarily slow the
	// effect of the affliction", so the slowed rate has to be smaller than the
	// full one and larger than nothing.
	const float FullOnFive = Effects::MortalDecayPercentPerSecond(5, false);
	const float SlowedOnFive = Effects::MortalDecayPercentPerSecond(5, true);
	TestTrue(TEXT("a kill slows the decay"), SlowedOnFive < FullOnFive);
	TestTrue(TEXT("and does not stop it"), SlowedOnFive > 0.0f);
	TestEqual(TEXT("by the share the constant states"), SlowedOnFive,
			  FullOnFive * (1.0f - Effects::MortalDecaySlowPercent / 100.0f),
			  0.0001f);

	// THE CAP IS TAKEN BEFORE THE SLOW, AND THIS IS THE ASSERTION THAT SAYS SO.
	// The two orders agree at every depth up to the ceiling floor -- both give
	// the uncapped rate halved -- and disagree past it, so the depth checked here
	// is deliberately well beyond it rather than at a round number near it.
	// Slowing first and capping second would leave this floor at the ceiling,
	// which is the whole rate, so reaping would buy the player nothing.
	const int32 DeepFloor = CeilingFloor * 3;
	TestEqual(TEXT("deep down, the full rate is the ceiling"),
			  Effects::MortalDecayPercentPerSecond(DeepFloor, false),
			  Effects::MortalDecayMostPercentPerSecond, 0.0001f);
	TestEqual(TEXT("and a kill still halves it that deep"),
			  Effects::MortalDecayPercentPerSecond(DeepFloor, true),
			  Effects::MortalDecayMostPercentPerSecond
				  * (1.0f - Effects::MortalDecaySlowPercent / 100.0f),
			  0.0001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMortalDecayBeatTest,
	"Cataclysm.DungeonModifierEffects.AFloorCarryingMortalDecaySapsThePlayersHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMortalDecayBeatTest::RunTest(const FString& Parameters)
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

	// A FLOOR DEEP ENOUGH TO BE AT THE CEILING, so the expected loss is a figure
	// this test can state without restating the depth arithmetic the rate test
	// above already checks.
	const int32 CeilingFloor = FMath::RoundToInt(
		Effects::MortalDecayMostPercentPerSecond
		/ Effects::MortalDecayPercentPerSecondPerFloor);
	Mode->DungeonModifiers = {MortalDecay};
	Mode->FloorNumber = CeilingFloor;
	if (!TestNotNull(TEXT("the floor was built"), Mode->BuildFloor()))
	{
		return false;
	}
	TestTrue(TEXT("the floor carries Mortal Decay"),
			 Mode->FloorBrief.Modifiers.Contains(MortalDecay));

	// THE DEPTH THE TEST MEANT TO BUILD, ASSERTED RATHER THAN ASSUMED. Every
	// figure below is worked out from it, so a floor number that did not arrive
	// would make them all agree with a rule doing the wrong thing.
	TestEqual(TEXT("and it is the floor this test asked for"),
			  Mode->FloorBrief.FloorNumber, CeilingFloor);

	const float Maximum = Player.Read(Vital::GetMaxHealthAttribute());
	if (!TestTrue(TEXT("the player has some maximum health"), Maximum > 0.0f))
	{
		return false;
	}

	// ONE BEAT, MEASURED WITH THE WORLD CLOCK STANDING STILL. `Mode->Tick` does
	// not move `World->TimeSeconds`, so no timer fires between the two reads and
	// the regeneration a character's BeginPlay starts cannot pollute the figure.
	// `CataclysmTestWorld::RunClock` would, which is why it is not used here.
	const float Before = Player.Read(Vital::GetHealthAttribute());
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	const float After = Player.Read(Vital::GetHealthAttribute());

	const float Expected = Maximum
		* Effects::MortalDecayPercentPerSecond(CeilingFloor, false) / 100.0f
		* ACataclysmDungeonGameMode::SecondsBetweenWaveChecks;
	TestTrue(TEXT("the beat sapped some health"), After < Before);
	TestEqual(TEXT("and it sapped a beat's share of the floor's rate"),
			  Before - After, Expected, 0.01f);

	// A SHALLOWER FLOOR SAPS LESS, WHICH IS THE ROW'S "AS THEY PROGRESS THROUGH
	// THE DUNGEON". Floor 1 against the ceiling floor, both read off the same
	// player on the same beat.
	Mode->DungeonModifiers = {MortalDecay};
	Mode->FloorNumber = 1;
	Mode->BuildFloor();
	TestEqual(TEXT("the shallow floor is floor 1"), Mode->FloorBrief.FloorNumber, 1);

	const float ShallowBefore = Player.Read(Vital::GetHealthAttribute());
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	const float ShallowAfter = Player.Read(Vital::GetHealthAttribute());
	TestTrue(TEXT("floor 1 saps something"), ShallowAfter < ShallowBefore);
	TestTrue(TEXT("and less than the deep floor did"),
			 ShallowBefore - ShallowAfter < Before - After);

	// AND A FLOOR WITHOUT THE ROW SAPS NOTHING, which is what says the beat reads
	// the floor's modifier list rather than draining everybody. Starvation is the
	// other row so the floor is not simply empty.
	Mode->DungeonModifiers = {Starvation};
	Mode->FloorNumber = CeilingFloor;
	Mode->BuildFloor();
	TestFalse(TEXT("the other floor does not carry Mortal Decay"),
			  Mode->FloorBrief.Modifiers.Contains(MortalDecay));

	// TWO BEATS, AND THE READING IS TAKEN BETWEEN THEM. `BuildFloor` writes the
	// floor's brief and never touches the player -- `ApplyFloorRulesToPlayer` is
	// called by `GoToFloor` and not by it, which was checked rather than assumed
	// -- so nothing here is settling and the first beat is not a settling beat.
	// It is here so the assertion covers two CONSECUTIVE beats on the new floor
	// rather than the first one, which is the stronger claim and costs a line.
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	const float Elsewhere = Player.Read(Vital::GetHealthAttribute());
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	TestEqual(TEXT("a floor without Mortal Decay saps nothing"),
			  Player.Read(Vital::GetHealthAttribute()), Elsewhere, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMortalDecayReapTest,
	"Cataclysm.DungeonModifierEffects.ReapingAnEnemySlowsMortalDecayAndOtherDeathsDoNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMortalDecayReapTest::RunTest(const FString& Parameters)
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

	// THE GAME MODE'S OWN StartPlay BINDS THE DEATH HANDLER, and a test world
	// never calls it, so the binding is made the way StartPlay makes it. The boss
	// cleanse test above records the same requirement.
	Mode->StartPlay();
	if (!TestNotNull(TEXT("the death announcer exists"),
					 UCataclysmCombatEvents::In(World)))
	{
		return false;
	}

	const int32 CeilingFloor = FMath::RoundToInt(
		Effects::MortalDecayMostPercentPerSecond
		/ Effects::MortalDecayPercentPerSecondPerFloor);
	Mode->DungeonModifiers = {MortalDecay};
	Mode->FloorNumber = CeilingFloor;
	if (!TestNotNull(TEXT("the floor was built"), Mode->BuildFloor()))
	{
		return false;
	}
	TestTrue(TEXT("the floor carries Mortal Decay"),
			 Mode->FloorBrief.Modifiers.Contains(MortalDecay));
	TestEqual(TEXT("and it is the floor this test asked for"),
			  Mode->FloorBrief.FloorNumber, CeilingFloor);

	// WHAT ONE BEAT COSTS, MEASURED THE SAME WAY EVERY TIME. The world clock does
	// not move inside this, so nothing regenerates between the two reads and the
	// only thing that can move the health attribute is the rule.
	const auto SappedOnOneBeat = [&Player, Mode]() -> float
	{
		const float Before = Player.Read(Vital::GetHealthAttribute());
		Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
		return Before - Player.Read(Vital::GetHealthAttribute());
	};

	const float AtFullRate = SappedOnOneBeat();
	if (!TestTrue(TEXT("the decay saps something to begin with"), AtFullRate > 0.0f))
	{
		return false;
	}

	// A CREATURE THE PLAYER KILLS SLOWS IT. Killed with a real blow so the
	// announcement travels the path the game uses, through
	// `UCataclysmSkillEffects::MarkDead`, and so the last blow on record names
	// the player as the killer -- which is what this rule asks about.
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ACataclysmEnemyCharacter* Reaped = World->SpawnActor<ACataclysmEnemyCharacter>(
		ACataclysmEnemyCharacter::StaticClass(), FVector(600.0f, 0.0f, 0.0f),
		FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("a creature to reap spawned"), Reaped))
	{
		return false;
	}
	UCataclysmSkillEffects::ApplyHit(Player.Character, Reaped, 100000.0f);
	if (!TestTrue(TEXT("the player's blow killed it"),
				  UCataclysmSkillEffects::IsDead(Reaped)))
	{
		return false;
	}

	const float AfterReaping = SappedOnOneBeat();
	TestTrue(TEXT("reaping slows the decay"), AfterReaping < AtFullRate);
	TestTrue(TEXT("and does not stop it"), AfterReaping > 0.0f);
	TestEqual(TEXT("by the share the constant states"), AfterReaping,
			  AtFullRate * (1.0f - Effects::MortalDecaySlowPercent / 100.0f),
			  0.01f);

	// THE SLOW IS TEMPORARY, WHICH IS THE ROW'S OWN WORD. Past the window the
	// rate is back where it was. The world clock moves here, which may regenerate
	// the player, so the measurement starts from a reading taken afterwards.
	CataclysmTestWorld::RunClock(World, Effects::MortalDecaySlowSeconds + 1.0f);
	const float WindowGone = SappedOnOneBeat();
	TestEqual(TEXT("past the window the decay is back at its full rate"),
			  WindowGone, AtFullRate, 0.01f);

	// AND A DEATH THE PLAYER DID NOT CAUSE BUYS NOTHING. The row says "the player
	// must give death his due souls by reaping enemies", so a creature that dies
	// to something else is not a soul the player gave.
	//
	// A CREATURE THE PLAYER HAS NEVER STRUCK, WHICH IS WHAT MAKES THIS A CONTROL.
	// The death notice names the last blow ON RECORD, so a creature the player
	// had hit and failed to kill would still name them as its killer; this one
	// has no blow on record at all, and `ReduceHealthDirectly` writes the health
	// attribute rather than dealing a blow, so it does not put one there.
	ACataclysmEnemyCharacter* Bystander =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), FVector(1200.0f, 0.0f, 0.0f),
			FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("a bystanding creature spawned"), Bystander))
	{
		return false;
	}
	UCataclysmSkillEffects::ReduceHealthDirectly(Bystander, Bystander, 100000.0f);
	if (!TestTrue(TEXT("the bystander died without the player"),
				  UCataclysmSkillEffects::IsDead(Bystander)))
	{
		return false;
	}

	TestEqual(TEXT("a death the player did not cause leaves the rate alone"),
			  SappedOnOneBeat(), AtFullRate, 0.01f);

	// AND TWO KILLS BUY ONE WINDOW, NOT TWO. `NoteDeathForMortalDecay` sets the
	// stamp to its own length FROM NOW rather than adding to what is there, so a
	// player who fells a pack does not bank minutes of slowed decay from one
	// fight. Nothing checked that until this arm: the two spellings differ only
	// past the first window's end, which every assertion above is inside.
	//
	// BOTH KILLED BEFORE THE CLOCK MOVES, so the two windows would be exactly
	// stacked if they stacked at all -- one length against two is the widest gap
	// the wait below can be asked to tell apart.
	ACataclysmEnemyCharacter* First = World->SpawnActor<ACataclysmEnemyCharacter>(
		ACataclysmEnemyCharacter::StaticClass(), FVector(1800.0f, 0.0f, 0.0f),
		FRotator::ZeroRotator, Spawn);
	ACataclysmEnemyCharacter* Second = World->SpawnActor<ACataclysmEnemyCharacter>(
		ACataclysmEnemyCharacter::StaticClass(), FVector(2400.0f, 0.0f, 0.0f),
		FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("a first creature for the pair spawned"), First)
		|| !TestNotNull(TEXT("a second creature for the pair spawned"), Second))
	{
		return false;
	}
	UCataclysmSkillEffects::ApplyHit(Player.Character, First, 100000.0f);
	UCataclysmSkillEffects::ApplyHit(Player.Character, Second, 100000.0f);
	if (!TestTrue(TEXT("both of the pair died"),
				  UCataclysmSkillEffects::IsDead(First)
					  && UCataclysmSkillEffects::IsDead(Second)))
	{
		return false;
	}

	// THE WINDOW IS OPEN, ASSERTED BEFORE THE WAIT. Without this the assertion
	// after the wait passes for a pair of kills that opened no window at all,
	// which is the reading it is least able to tell from the one it is testing.
	const float WhileThePairsWindowRuns = SappedOnOneBeat();
	TestTrue(TEXT("the pair opened a window"),
			 WhileThePairsWindowRuns < AtFullRate);

	CataclysmTestWorld::RunClock(World, Effects::MortalDecaySlowSeconds + 1.0f);
	TestEqual(TEXT("and one window's wait ends it, so two kills did not stack"),
			  SappedOnOneBeat(), AtFullRate, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWastingStacksTest,
	"Cataclysm.DungeonModifierEffects.WastingSicknessStacksOnABlowAndStopsAtItsCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWastingStacksTest::RunTest(const FString& Parameters)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// THE STACK RULE ON ITS OWN, WITH NUMBERS TYPED IN. Issues #1786 and #41. The
	// caller keeps the random draw, which is what lets this check the cap and the
	// refusal without making a roll come up.

	// A BLOW THAT DID NOT INFLICT CHANGES NOTHING.
	TestEqual(TEXT("no stacks and no infliction stays at none"),
			  Effects::WastingSicknessStacksAfterHit(0, false), 0);
	TestEqual(TEXT("two stacks and no infliction stays at two"),
			  Effects::WastingSicknessStacksAfterHit(2, false), 2);

	// AND ONE THAT DID ADDS EXACTLY ONE.
	TestEqual(TEXT("the first infliction gives one stack"),
			  Effects::WastingSicknessStacksAfterHit(0, true), 1);
	TestEqual(TEXT("and the next gives two"),
			  Effects::WastingSicknessStacksAfterHit(1, true), 2);

	// THE CAP IS REACHED AND NOT PASSED. The floor below it has to be under the
	// cap, or this would pass for a rule that returned the cap for every input.
	TestEqual(TEXT("one below the cap still rises to the cap"),
			  Effects::WastingSicknessStacksAfterHit(
				  Effects::WastingSicknessMostStacks - 1, true),
			  Effects::WastingSicknessMostStacks);
	TestEqual(TEXT("and at the cap another blow adds nothing"),
			  Effects::WastingSicknessStacksAfterHit(
				  Effects::WastingSicknessMostStacks, true),
			  Effects::WastingSicknessMostStacks);
	TestTrue(TEXT("and one below the cap really is below it"),
			 Effects::WastingSicknessStacksAfterHit(
				 Effects::WastingSicknessMostStacks - 2, true)
				 < Effects::WastingSicknessMostStacks);

	// A COUNT BELOW NOTHING IS NOTHING.
	TestEqual(TEXT("a negative count with no infliction reads as none"),
			  Effects::WastingSicknessStacksAfterHit(-3, false), 0);
	TestEqual(TEXT("and with one, as one"),
			  Effects::WastingSicknessStacksAfterHit(-3, true), 1);

	// WHAT THE STACKS TAKE, WHICH IS ONE FIGURE FOR BOTH MAXIMUMS.
	TestEqual(TEXT("no stacks take nothing"),
			  Effects::WastingSicknessMaximumsLessPercent(0), 0.0f, 0.0001f);
	TestEqual(TEXT("one stack takes the row's per-stack share"),
			  Effects::WastingSicknessMaximumsLessPercent(1),
			  Effects::WastingSicknessPercentPerStack, 0.0001f);
	TestEqual(TEXT("three stacks take three times it"),
			  Effects::WastingSicknessMaximumsLessPercent(3),
			  3.0f * Effects::WastingSicknessPercentPerStack, 0.0001f);

	// AND A COUNT FROM SOMEWHERE ELSE CANNOT ASK FOR MORE THAN THE CAP.
	TestEqual(TEXT("a count past the cap is clamped to it"),
			  Effects::WastingSicknessMaximumsLessPercent(
				  Effects::WastingSicknessMostStacks + 4),
			  Effects::WastingSicknessMostStacks
				  * Effects::WastingSicknessPercentPerStack, 0.0001f);
	TestEqual(TEXT("and a negative count takes nothing"),
			  Effects::WastingSicknessMaximumsLessPercent(-2), 0.0f, 0.0001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWastingBlowTest,
	"Cataclysm.DungeonModifierEffects.AnEnemysBlowStacksWastingSicknessOntoBothMaximums",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWastingBlowTest::RunTest(const FString& Parameters)
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

	// THE GAME MODE'S OWN StartPlay BINDS THE BLOW HANDLER, and a test world never
	// calls it, so the binding is made the way StartPlay makes it -- the same
	// requirement the boss cleanse test above records for the death handler.
	Mode->StartPlay();
	if (!TestNotNull(TEXT("the combat announcer exists"),
					 UCataclysmCombatEvents::In(World)))
	{
		return false;
	}

	// THE ROLL IS PINNED SO THE CHANCE CANNOT DECIDE WHETHER THIS TEST PASSES.
	// Zero beats any chance above zero, so every landed blow inflicts a stack.
	FScopedConsoleString Roll(TEXT("Cataclysm.WastingSicknessRoll"), TEXT("0"));
	if (!TestNotNull(TEXT("the roll can be pinned"), Roll.Variable))
	{
		return false;
	}

	// A FLOOR CARRYING THIS ROW AND STARVATION TOGETHER, which is the case the
	// two separate fields exist for: both rules take a share of maximum health,
	// from different sources, and neither may erase the other. Issue #1765.
	Mode->DungeonModifiers = {WastingSickness, Starvation};
	Mode->FloorNumber = 1;
	if (!TestNotNull(TEXT("the floor was built"), Mode->BuildFloor()))
	{
		return false;
	}
	TestTrue(TEXT("the floor carries Wasting Sickness"),
			 Mode->FloorBrief.Modifiers.Contains(WastingSickness));
	TestTrue(TEXT("and Starvation"),
			 Mode->FloorBrief.Modifiers.Contains(Starvation));

	// A CREATURE TO BE STRUCK BY, WITH DAMAGE TO STRIKE WITH. See
	// `SpawnCreatureThatCanHit`: a creature spawned bare lands blows worth
	// nothing, and this test failed on exactly that before the attack damage was
	// set.
	ACataclysmEnemyCharacter* Enemy = SpawnCreatureThatCanHit(World, 700.0f);
	if (!TestNotNull(TEXT("a creature that can hit spawned"), Enemy))
	{
		return false;
	}

	// NOTHING ON THE PLAYER BEFORE A BLOW LANDS, asserted so the reading after it
	// cannot be something that was already there.
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	TestEqual(TEXT("no stacks are applied before any blow"),
			  Effects::WastingSicknessMaximumsLessPercent(0), 0.0f, 0.0001f);

	// THE CREATURE STRIKES THE PLAYER. A real blow, so the announcement travels
	// the path the game uses.
	const float Landed =
		UCataclysmSkillEffects::ApplyHit(Enemy, Player.Character, 50.0f);
	if (!TestTrue(TEXT("the creature's blow landed on the player"), Landed > 0.0f))
	{
		return false;
	}
	TestFalse(TEXT("and the player survived it"),
			  UCataclysmSkillEffects::IsDead(Player.Character));

	// THE BEAT PUTS IT ON THE CHARACTER, within a quarter of a second.
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);

	// TWO DUNGEON-RULE MODIFIERS ON MAXIMUM HEALTH, WHICH IS THE WHOLE POINT OF
	// THE SEPARATE FIELDS. One is Starvation's per-floor share and one is this
	// row's stack. `DungeonRuleOn` returns the FIRST it finds, so the count is
	// read from the stored inputs directly.
	const FCataclysmStatInputs* HealthInputs =
		Player.AbilitySystem->GetStatInputs(FName(TEXT("max_health")));
	if (!TestNotNull(TEXT("maximum health has stored inputs"), HealthInputs))
	{
		return false;
	}
	int32 DungeonRules = 0;
	for (const FCataclysmStatModifier& Modifier : HealthInputs->Modifiers)
	{
		if (Modifier.Source == ECataclysmModifierSource::DungeonRule)
		{
			++DungeonRules;
			TestTrue(TEXT("and each dungeon rule on it takes rather than gives"),
					 Modifier.Value < 0.0f);
		}
	}
	TestEqual(TEXT("maximum health carries BOTH dungeon rules, not one"),
			  DungeonRules, 2);

	// AND MAXIMUM MANA CARRIES THIS ROW'S, which is what says the row's "max HP
	// and max mana" reached both. Dehydration is not on this floor, so there is
	// exactly one.
	const FCataclysmStatModifier* OnMana =
		DungeonRuleOn(Player.AbilitySystem, TEXT("max_mana"));
	if (TestNotNull(TEXT("maximum mana carries a dungeon rule too"), OnMana))
	{
		TestTrue(TEXT("and it takes rather than gives"), OnMana->Value < 0.0f);
		TestEqual(TEXT("by one stack's share"), OnMana->Value,
				  -Effects::WastingSicknessPercentPerStack, 0.01f);
	}

	// A BLOW THE PLAYER DEALS INFLICTS NOTHING, which is what says the rule reads
	// who was STRUCK. The roll is still pinned to always inflict, so a rule that
	// did not check the target would stack here.
	UCataclysmSkillEffects::ApplyHit(Player.Character, Enemy, 1.0f);
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);

	const FCataclysmStatModifier* StillOneStack =
		DungeonRuleOn(Player.AbilitySystem, TEXT("max_mana"));
	if (TestNotNull(TEXT("maximum mana still carries the rule"), StillOneStack))
	{
		TestEqual(TEXT("and the player's own blow added no stack"),
				  StillOneStack->Value,
				  -Effects::WastingSicknessPercentPerStack, 0.01f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWastingCureTest,
	"Cataclysm.DungeonModifierEffects.ABossCuresWastingSicknessAndTheStairsDoNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWastingCureTest::RunTest(const FString& Parameters)
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

	Mode->StartPlay();
	FScopedConsoleString Roll(TEXT("Cataclysm.WastingSicknessRoll"), TEXT("0"));
	if (!TestNotNull(TEXT("the roll can be pinned"), Roll.Variable))
	{
		return false;
	}

	// THIS ROW ALONE, so every dungeon-rule modifier read below is this rule's.
	Mode->DungeonModifiers = {WastingSickness};
	Mode->FloorNumber = 1;
	if (!TestNotNull(TEXT("the floor was built"), Mode->BuildFloor()))
	{
		return false;
	}

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// TAKE TWO BLOWS, SO THE CURE HAS SOMETHING TO REMOVE AND THE SECOND SAYS THE
	// STACKS ACCUMULATE IN PLAY RATHER THAN ONLY IN THE PURE RULE.
	//
	// A SMALL SHARE OF THE CREATURE'S DAMAGE, DELIBERATELY. The player must
	// SURVIVE both blows: their own death is this rule's other cure, so a test
	// that killed them would clear the stacks it is about to check and pass for
	// the wrong reason.
	const auto StruckOnce = [&](float Where) -> bool
	{
		ACataclysmEnemyCharacter* Enemy = SpawnCreatureThatCanHit(World, Where);
		if (!Enemy)
		{
			return false;
		}
		const float Landed =
			UCataclysmSkillEffects::ApplyHit(Enemy, Player.Character, 20.0f);
		Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
		return Landed > 0.0f;
	};

	if (!TestTrue(TEXT("a first blow landed"), StruckOnce(700.0f))
		|| !TestTrue(TEXT("a second blow landed"), StruckOnce(1100.0f)))
	{
		return false;
	}
	if (!TestFalse(TEXT("and the player survived both, so nothing else cured it"),
				   UCataclysmSkillEffects::IsDead(Player.Character)))
	{
		return false;
	}

	const FCataclysmStatModifier* AfterTwo =
		DungeonRuleOn(Player.AbilitySystem, TEXT("max_health"));
	if (!TestNotNull(TEXT("two blows put the rule on maximum health"), AfterTwo))
	{
		return false;
	}
	TestEqual(TEXT("and two stacks are worth twice one"), AfterTwo->Value,
			  -2.0f * Effects::WastingSicknessPercentPerStack, 0.01f);

	// THE STAIRS DO NOT CURE IT, WHICH IS THE ROW'S "PERMANENT FOR THE DURATION OF
	// THE DUNGEON". Changing floor replaces the player's dungeon modifiers
	// wholesale, so this is also what says the beat puts the reduction back.
	Mode->FloorNumber = 2;
	Mode->BuildFloor();
	Mode->ApplyFloorRulesToPlayer();
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);

	const FCataclysmStatModifier* OnTheNextFloor =
		DungeonRuleOn(Player.AbilitySystem, TEXT("max_health"));
	if (TestNotNull(TEXT("the debuff survives the stairs"), OnTheNextFloor))
	{
		TestEqual(TEXT("with both stacks still on it"), OnTheNextFloor->Value,
				  -2.0f * Effects::WastingSicknessPercentPerStack, 0.01f);
	}

	// A COMMON CREATURE'S DEATH CURES NOTHING. The row asks for a floor boss.
	ACataclysmEnemyCharacter* Common = World->SpawnActor<ACataclysmEnemyCharacter>(
		ACataclysmEnemyCharacter::StaticClass(), FVector(1600.0f, 0.0f, 0.0f),
		FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("a common creature spawned"), Common))
	{
		return false;
	}
	Common->SetRarityStep(0);
	UCataclysmSkillEffects::ApplyHit(Player.Character, Common, 100000.0f);
	TestTrue(TEXT("the common creature died"),
			 UCataclysmSkillEffects::IsDead(Common));
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);

	const FCataclysmStatModifier* StillThere =
		DungeonRuleOn(Player.AbilitySystem, TEXT("max_health"));
	if (TestNotNull(TEXT("a Common's death left the debuff alone"), StillThere))
	{
		TestEqual(TEXT("unchanged"), StillThere->Value,
				  -2.0f * Effects::WastingSicknessPercentPerStack, 0.01f);
	}

	// AND A BOSS'S DEATH CURES IT OUTRIGHT.
	ACataclysmEnemyCharacter* Boss = World->SpawnActor<ACataclysmEnemyCharacter>(
		ACataclysmEnemyCharacter::StaticClass(), FVector(2200.0f, 0.0f, 0.0f),
		FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("a boss spawned"), Boss))
	{
		return false;
	}
	Boss->SetRarityStep(ACataclysmEnemyCharacter::FirstBossRarityStep);
	TestTrue(TEXT("and it really is a boss"), Boss->IsBoss());
	UCataclysmSkillEffects::ApplyHit(Player.Character, Boss, 100000.0f);
	TestTrue(TEXT("the boss died"), UCataclysmSkillEffects::IsDead(Boss));
	Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);

	TestNull(TEXT("a boss's death takes the debuff off entirely"),
			 DungeonRuleOn(Player.AbilitySystem, TEXT("max_health")));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
