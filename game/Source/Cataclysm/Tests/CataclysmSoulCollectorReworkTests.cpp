// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * The Soul Collector keystone rework, issue #2951 (2026-11-27). The owner found that most of the tree's keystones were
 * about gaining or keeping Fervour, which play did not need, and chose to replace all eight, to rework the small nodes
 * about gaining or keeping it in the same change, and to count kills by the player's minions, gadgets and traps as the
 * player's tree-wide. `docs/DECISIONS.md` holds the words and every figure.
 *
 * ONE ROWS TEST AND ONE BEHAVIOUR TEST PER CHANGED NODE, EACH WITH A CONTROL. The rows test reads every changed node's
 * rows from `DT_PassiveEffects`; the behaviour tests wear the real rows on a real possessed Soul Collector, as the game
 * does, through `ACataclysmPlayerState::SetPassiveAllocation` and the equipment's refresh, and compare a figure with the
 * node against the same figure without it, or one state against another. Expected figures are the nodes' own numbers.
 *
 * WRITTEN WITHOUT A BUILD. Every figure is reasoned from the code; the window that runs them is the coordinating
 * session's.
 */
namespace CataclysmSoulCollectorReworkTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;
	using CataclysmTestWorld::RunClock;

	/** Centimetres in a metre. */
	constexpr float ReworkMetre = 100.0f;

	/** How far a figure may fall from one it was built to equal. */
	constexpr float ReworkAllowance = 0.001f;

	/** The starting node, worn under every allocation below, as every Soul Collector wears it. */
	const TCHAR* const ReworkRoot = TEXT("SoulCollector_basic_spine_000");

	/** A possessed Death player with a Dagger, level 30, a Fervour pool of 150 and health of 10,000. */
	struct FReworkPlayer
	{
		explicit FReworkPlayer(UWorld* World)
		{
			ACataclysmPlayerState* SpawnedState = World->SpawnActor<ACataclysmPlayerState>();
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!SpawnedState || !Controller || !Character)
			{
				return;
			}
			// DEATH CHOSEN BEFORE POSSESSION, as `Cataclysm.DeathFinishSoulCollector.` builds its character, so the
			// starting weapon is given carrying Death and the Soul Collector's tree is reached.
			SpawnedState->SetCreationChoice(FName(TEXT("Dagger")), FName(TEXT("Death")));
			Controller->SetPlayerState(SpawnedState);
			Controller->Possess(Character);
			State = Character->GetPlayerState<ACataclysmPlayerState>();
			Equipment = Character->GetEquipment();
			System = State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
			if (State)
			{
				State->SetLevelAndExperience(30, 0);
			}
		}

		bool IsReady() const { return Character && State && Equipment && System; }

		/** Wear the starting node and these nodes, by their real rows; then the pool, its maximum and health. */
		void Wear(const TArray<TPair<const TCHAR*, int32>>& Nodes, int32 PointsBesides = 0) const
		{
			FCataclysmPassiveAllocation Allocation;
			Allocation.Add(FName(ReworkRoot), 1);
			for (const TPair<const TCHAR*, int32>& Node : Nodes)
			{
				Allocation.Add(FName(Node.Key), Node.Value);
			}
			// POINTS SPENT AND NOTHING ELSE, which opens the Fervour thresholds, as
			// `Cataclysm.FervourThreshold.` opens them.
			if (PointsBesides > 0)
			{
				Allocation.Add(FName(TEXT("Test_PointsSpentAndNothingElse")), PointsBesides);
			}
			State->SetPassiveAllocation(Allocation, TArray<FName>());
			Equipment->RefreshAttributes(System);
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
		}

		void SetFervour(float Value) const
		{
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Value);
		}

		float Health() const
		{
			return System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		void SetHealth(float Value) const
		{
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Value);
		}

		/** A stat's modifiers applied to a base of 100, with no skill in hand. */
		float On100(const TCHAR* Stat) const
		{
			return System->StatAppliedTo(FName(Stat), FGameplayTagContainer(), 100.0f);
		}

		/** The increases on a stat, as a fraction: 0.02 for 2%. */
		float Increases(const TCHAR* Stat) const
		{
			return System->IncreasesForStat(FName(Stat), FGameplayTagContainer());
		}

		/** A kill announced to the rows as the player's kill handler raises it, the slain creature as its character. */
		void Kill(const AActor* Slain) const
		{
			System->ActOnEvent(FName(TEXT("kill")), nullptr, 0.0f, /*bLanded=*/true, Slain);
		}

		ACataclysmPlayerCharacter* Character = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** A hostile creature at `Where`, of this rarity rung, with health of 100,000 and nothing that turns a blow aside. */
	ACataclysmEnemyCharacter* ReworkCreature(UWorld* World, const FVector& Where, int32 Rung = 0)
	{
		ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (!Creature)
		{
			return nullptr;
		}
		Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		if (Rung > 0)
		{
			Creature->SetRarityStep(Rung);
		}
		Creature->SetHealth(100000.0f);
		Creature->SetArmour(0.0f);
		if (UAbilitySystemComponent* Its = Creature->GetAbilitySystemComponent())
		{
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		}
		return Creature;
	}

	float CreatureHealth(const ACataclysmEnemyCharacter* Creature)
	{
		const UAbilitySystemComponent* Its = Creature ? Creature->GetAbilitySystemComponent() : nullptr;
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** Pins the roll every row with a chance makes, and puts it back on leaving. */
	struct FPinnedChanceRoll
	{
		explicit FPinnedChanceRoll(float Roll)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.ClassResourceChanceRoll"));
			if (Variable)
			{
				Before = Variable->GetFloat();
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}

		~FPinnedChanceRoll()
		{
			if (Variable)
			{
				Variable->Set(Before, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
		float Before = -1.0f;
	};
}

// ---------------------------------------------------------------------------------------------------------------------
// The rows
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkRowsTest,
	"Cataclysm.SoulCollectorRework.TheTwentyNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Every changed node's rows, read from `DT_PassiveEffects`: the stat or action, the bucket, the value per point, and the
 * column that makes the node its sentence. THE CONTROL: each node's row count is checked, so a row left over from the
 * old node fails.
 */
bool FCataclysmSoulCollectorReworkRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}
	struct FExpected
	{
		const TCHAR* Node;
		const TCHAR* StatOrAction;
		const TCHAR* Kind;
		float Value;
		const TCHAR* Scale;
		float Step;
		const TCHAR* Event;
		const TCHAR* Condition;
		float ConditionValue;
	};
	const FExpected Expected[] = {
		// THE STARTING NODE: its damage under the new condition, and answer 5.
		{TEXT("SoulCollector_basic_spine_000"), TEXT("attack_damage"), TEXT("more"), 1.0f, TEXT("class_resource_held"),
		 2.0f, TEXT(""), TEXT("fervour_held_adds_damage"), 0.0f},
		{TEXT("SoulCollector_basic_spine_000"), TEXT("spell_damage"), TEXT("more"), 1.0f, TEXT("class_resource_held"),
		 2.0f, TEXT(""), TEXT("fervour_held_adds_damage"), 0.0f},
		{TEXT("SoulCollector_basic_spine_000"), TEXT("follower_kills_count_as_yours"), TEXT("flat"), 1.0f, TEXT(""), 0.0f,
		 TEXT(""), TEXT(""), 0.0f},
		// THE EIGHT KEYSTONES.
		{TEXT("SoulCollector_keystone_spine_001"), TEXT("attack_speed"), TEXT("increased"), 2.0f, TEXT("own_stacks"), 1.0f,
		 TEXT("nearby_death"), TEXT(""), 0.0f},
		{TEXT("SoulCollector_keystone_spine_001"), TEXT("movement_speed"), TEXT("increased"), 2.0f, TEXT("own_stacks"),
		 1.0f, TEXT("nearby_death"), TEXT(""), 0.0f},
		{TEXT("SoulCollector_keystone_spine_002"), TEXT("strike_strongest_near"), TEXT(""), 40.0f, TEXT(""), 0.0f,
		 TEXT("fervour_decayed"), TEXT(""), 0.0f},
		{TEXT("SoulCollector_keystone_spine_003"), TEXT("fervour_held_adds_no_damage"), TEXT("flat"), 1.0f, TEXT(""), 0.0f,
		 TEXT(""), TEXT(""), 0.0f},
		{TEXT("SoulCollector_keystone_spine_003"), TEXT("damage_taken"), TEXT("more"), -1.0f, TEXT("class_resource_held"),
		 4.0f, TEXT(""), TEXT(""), 0.0f},
		{TEXT("SoulCollector_keystone_a_kB"), TEXT("attack_damage"), TEXT("more"), 0.5f, TEXT("class_resource_held"), 1.0f,
		 TEXT(""), TEXT(""), 0.0f},
		{TEXT("SoulCollector_keystone_a_kB"), TEXT("spell_damage"), TEXT("more"), 0.5f, TEXT("class_resource_held"), 1.0f,
		 TEXT(""), TEXT(""), 0.0f},
		{TEXT("SoulCollector_keystone_a_kC"), TEXT("smite_nearby"), TEXT(""), 100.0f, TEXT(""), 0.0f,
		 TEXT("fervour_threshold_reached"), TEXT(""), 0.0f},
		{TEXT("SoulCollector_keystone_b_kB"), TEXT("attack_damage"), TEXT("more"), 20.0f, TEXT("own_stacks"), 1.0f,
		 TEXT("kill"), TEXT("target_rarity_at_least"), 1.0f},
		{TEXT("SoulCollector_keystone_b_kB"), TEXT("spell_damage"), TEXT("more"), 20.0f, TEXT("own_stacks"), 1.0f,
		 TEXT("kill"), TEXT("target_rarity_at_least"), 1.0f},
		{TEXT("SoulCollector_keystone_b_kC"), TEXT("attack_damage"), TEXT("more"), 15.0f, TEXT("own_stacks"), 5.0f,
		 TEXT("kill"), TEXT(""), 0.0f},
		{TEXT("SoulCollector_keystone_b_kC"), TEXT("spell_damage"), TEXT("more"), 15.0f, TEXT("own_stacks"), 5.0f,
		 TEXT("kill"), TEXT(""), 0.0f},
		{TEXT("SoulCollector_keystone_b_kC"), TEXT("damage_taken"), TEXT("more"), -10.0f, TEXT("own_stacks"), 5.0f,
		 TEXT("kill"), TEXT(""), 0.0f},
		{TEXT("SoulCollector_keystone_c_kA"), TEXT("siphon_drain_weapon_percent"), TEXT("flat"), 0.5f,
		 TEXT("class_resource_held"), 5.0f, TEXT(""), TEXT(""), 0.0f},
		// THE ELEVEN SMALL NODES.
		{TEXT("SoulCollector_basic_spine_001"), TEXT("attack_damage"), TEXT("increased"), 1.0f, TEXT(""), 0.0f, TEXT(""),
		 TEXT("target_within_metres"), 6.0f},
		{TEXT("SoulCollector_basic_spine_001"), TEXT("spell_damage"), TEXT("increased"), 1.0f, TEXT(""), 0.0f, TEXT(""),
		 TEXT("target_within_metres"), 6.0f},
		{TEXT("SoulCollector_basic_spine_004"), TEXT("health"), TEXT(""), 0.2f, TEXT(""), 0.0f, TEXT("fervour_decayed"),
		 TEXT(""), 0.0f},
		{TEXT("SoulCollector_basic_spine_007"), TEXT("attack_damage"), TEXT("increased"), 1.0f, TEXT(""), 0.0f, TEXT(""),
		 TEXT("seconds_after_kill"), 4.0f},
		{TEXT("SoulCollector_basic_spine_007"), TEXT("spell_damage"), TEXT("increased"), 1.0f, TEXT(""), 0.0f, TEXT(""),
		 TEXT("seconds_after_kill"), 4.0f},
		{TEXT("SoulCollector_basic_a_b1"), TEXT("attack_damage"), TEXT("increased"), 2.0f, TEXT(""), 0.0f, TEXT(""),
		 TEXT("class_resource_points_at_least"), 50.0f},
		{TEXT("SoulCollector_basic_a_b1"), TEXT("spell_damage"), TEXT("increased"), 2.0f, TEXT(""), 0.0f, TEXT(""),
		 TEXT("class_resource_points_at_least"), 50.0f},
		{TEXT("SoulCollector_basic_a_b2"), TEXT("damage_taken"), TEXT("increased"), -1.0f, TEXT(""), 0.0f, TEXT(""),
		 TEXT("class_resource_points_at_least"), 50.0f},
		{TEXT("SoulCollector_basic_b_stem0"), TEXT("strike_strongest_near"), TEXT(""), 50.0f, TEXT(""), 0.0f, TEXT("kill"),
		 TEXT(""), 0.0f},
		{TEXT("SoulCollector_basic_b_stem2"), TEXT("attack_damage"), TEXT("increased"), 1.0f, TEXT("enemies_in_reach"),
		 2.0f, TEXT(""), TEXT(""), 0.0f},
		{TEXT("SoulCollector_basic_b_stem2"), TEXT("spell_damage"), TEXT("increased"), 1.0f, TEXT("enemies_in_reach"),
		 2.0f, TEXT(""), TEXT(""), 0.0f},
		{TEXT("SoulCollector_basic_b_b1"), TEXT("health"), TEXT(""), 1.0f, TEXT(""), 0.0f, TEXT("kill"),
		 TEXT("target_rarity_at_least"), 1.0f},
		{TEXT("SoulCollector_basic_b_b2"), TEXT("attack_speed"), TEXT("increased"), 1.0f, TEXT(""), 0.0f, TEXT(""),
		 TEXT("enemy_of_rarity_in_reach"), 1.0f},
		{TEXT("SoulCollector_basic_b_c0"), TEXT("attack_speed"), TEXT("increased"), 0.5f, TEXT("own_stacks"), 1.0f,
		 TEXT("kill"), TEXT(""), 0.0f},
		{TEXT("SoulCollector_basic_b_c1"), TEXT("damage_taken"), TEXT("increased"), -0.5f, TEXT("own_stacks"), 2.0f,
		 TEXT("kill"), TEXT(""), 0.0f},
	};

	// HOW MANY ROWS EACH NODE HOLDS, so a row left from the old node, or one the generator added, fails.
	TMap<FString, int32> Want;
	for (const FExpected& Row : Expected)
	{
		++Want.FindOrAdd(Row.Node);
	}
	// THE STARTING NODE HOLDS ITS EIGHT OTHER ROWS AS WELL: five of Fervour by rung, the decay and two graces.
	Want[TEXT("SoulCollector_basic_spine_000")] += 8;

	for (const TPair<FString, int32>& Node : Want)
	{
		TestEqual(*FString::Printf(TEXT("%s: rows"), *Node.Key),
				  UCataclysmPassiveTree::EffectsFor(Effects, FName(*Node.Key)).Num(), Node.Value);
	}
	for (const FExpected& Row : Expected)
	{
		int32 Found = 0;
		for (const FCataclysmPassiveEffectRow* Each : UCataclysmPassiveTree::EffectsFor(Effects, FName(Row.Node)))
		{
			const FString& Named = Each->Action.IsEmpty() ? Each->Stat : Each->Action;
			if (Each->Option != 0 || Named != Row.StatOrAction || Each->Condition != Row.Condition
				|| !FMath::IsNearlyEqual(Each->ValuePerPoint, Row.Value, ReworkAllowance))
			{
				continue;
			}
			++Found;
			const FString Where = FString::Printf(TEXT("%s %s"), Row.Node, Row.StatOrAction);
			TestEqual(*(Where + TEXT(": the bucket")), Each->ValueKind, FString(Row.Kind));
			TestEqual(*(Where + TEXT(": the scale")), Each->Scale, FString(Row.Scale));
			if (Row.Step > 0.0f)
			{
				TestEqual(*(Where + TEXT(": the step")), Each->ScaleStep, Row.Step, ReworkAllowance);
			}
			TestEqual(*(Where + TEXT(": the event")), Each->ActionEvent, FString(Row.Event));
			TestEqual(*(Where + TEXT(": the condition's value")), Each->ConditionValue, Row.ConditionValue,
					  ReworkAllowance);
		}
		TestEqual(*FString::Printf(TEXT("%s %s: found once"), Row.Node, Row.StatOrAction), Found, 1);
	}

	// AND THE COLUMNS ONE NODE OR TWO STATE.
	const auto First = [Effects](const TCHAR* Node, const TCHAR* StatOrAction) -> const FCataclysmPassiveEffectRow*
	{
		for (const FCataclysmPassiveEffectRow* Each : UCataclysmPassiveTree::EffectsFor(Effects, FName(Node)))
		{
			if ((Each->Action.IsEmpty() ? Each->Stat : Each->Action) == StatOrAction)
			{
				return Each;
			}
		}
		return nullptr;
	};
	if (const FCataclysmPassiveEffectRow* Gathered = First(TEXT("SoulCollector_keystone_spine_001"), TEXT("attack_speed")))
	{
		TestEqual(TEXT("Gathered Souls: within 6 metres"), Gathered->EventValue, 6.0f, ReworkAllowance);
		TestEqual(TEXT("Gathered Souls: 6 seconds"), Gathered->StackSeconds, 6.0f, ReworkAllowance);
		TestEqual(TEXT("Gathered Souls: up to 10 stacks"), Gathered->ScaleMaxSteps, 10);
	}
	if (const FCataclysmPassiveEffectRow* Flesh = First(TEXT("SoulCollector_keystone_spine_003"), TEXT("damage_taken")))
	{
		TestEqual(TEXT("Death Made Flesh: up to 25 steps"), Flesh->ScaleMaxSteps, 25);
	}
	if (const FCataclysmPassiveEffectRow* Urn = First(TEXT("SoulCollector_keystone_a_kB"), TEXT("attack_damage")))
	{
		TestEqual(TEXT("Heavy Urn: above 50"), Urn->ScaleOffset, 50.0f, ReworkAllowance);
	}
	if (const FCataclysmPassiveEffectRow* Trophy = First(TEXT("SoulCollector_keystone_b_kB"), TEXT("attack_damage")))
	{
		TestEqual(TEXT("Trophy of the Strong: 10 seconds"), Trophy->StackSeconds, 10.0f, ReworkAllowance);
		TestEqual(TEXT("Trophy of the Strong: one stack"), Trophy->ScaleMaxSteps, 1);
	}
	if (const FCataclysmPassiveEffectRow* Rhythm = First(TEXT("SoulCollector_keystone_b_kC"), TEXT("attack_damage")))
	{
		TestEqual(TEXT("Reaper's Rhythm: a chain's 2 seconds"), Rhythm->StackSeconds, 2.0f, ReworkAllowance);
		TestEqual(TEXT("Reaper's Rhythm: 5 stacks at most, one step"), Rhythm->ScaleMaxSteps, 5);
	}
	if (const FCataclysmPassiveEffectRow* Gleaning = First(TEXT("SoulCollector_basic_b_stem0"), TEXT("strike_strongest_near")))
	{
		TestEqual(TEXT("Gleaning: a chance of 5 a point"), Gleaning->ChancePercent, 5.0f, ReworkAllowance);
	}
	if (const FCataclysmPassiveEffectRow* Herd = First(TEXT("SoulCollector_basic_b_stem2"), TEXT("attack_damage")))
	{
		TestEqual(TEXT("Thinning the Herd: within 8 metres"), Herd->ReachMetres, 8.0f, ReworkAllowance);
		TestEqual(TEXT("Thinning the Herd: up to 10 enemies, 5 steps"), Herd->ScaleMaxSteps, 5);
	}
	if (const FCataclysmPassiveEffectRow* Vigil = First(TEXT("SoulCollector_basic_b_b2"), TEXT("attack_speed")))
	{
		TestEqual(TEXT("Vigil Over the Strong: within 15 metres"), Vigil->ReachMetres, 15.0f, ReworkAllowance);
	}
	if (const FCataclysmPassiveEffectRow* Tear = First(TEXT("SoulCollector_basic_b_b1"), TEXT("health")))
	{
		TestEqual(TEXT("Tear Loose: a share of maximum health"), Tear->FractionOf, FString(TEXT("maximum")));
	}
	if (const FCataclysmPassiveEffectRow* Patience = First(TEXT("SoulCollector_basic_spine_004"), TEXT("health")))
	{
		TestEqual(TEXT("Grave Patience: a share of maximum health"), Patience->FractionOf, FString(TEXT("maximum")));
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// The keystones
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkDeathMadeFleshTest,
	"Cataclysm.SoulCollectorRework.DeathMadeFleshTakesAwayTheFervoursDamageAndGivesDefence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Death Made Flesh: "The Fervour you hold no longer adds to your damage. Instead you take 1% less damage for every 4
 * Fervour you hold, up to 25%." THE CONTROL: the starting node alone gives 50% more damage at 100 Fervour and nothing off
 * the damage taken. With the keystone, the damage is the same at 100 as at none, the damage at none is unchanged (so
 * the keystone took the starting node's rows away and nothing else), and 100 Fervour takes 25% off the damage taken.
 * 120 Fervour is 30 steps, capped at 25.
 */
bool FCataclysmSoulCollectorReworkDeathMadeFleshTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}

	Player.Wear({});
	Player.SetFervour(0.0f);
	const float PlainAtNone = Player.On100(TEXT("attack_damage"));
	const float TakenAtNone = Player.On100(TEXT("damage_taken"));
	Player.SetFervour(100.0f);
	TestEqual(TEXT("control: the starting node gives 50% more damage at 100 Fervour"),
			  Player.On100(TEXT("attack_damage")) / PlainAtNone, 1.5f, ReworkAllowance);
	TestEqual(TEXT("control: and takes nothing off the damage taken"), Player.On100(TEXT("damage_taken")), TakenAtNone,
			  ReworkAllowance);

	Player.Wear({{TEXT("SoulCollector_keystone_spine_003"), 1}});
	Player.SetFervour(0.0f);
	TestEqual(TEXT("Death Made Flesh: the damage at no Fervour is what it was, so no other damage was taken away"),
			  Player.On100(TEXT("attack_damage")), PlainAtNone, ReworkAllowance);
	Player.SetFervour(100.0f);
	TestEqual(TEXT("and at 100 Fervour the Fervour adds no damage"), Player.On100(TEXT("attack_damage")), PlainAtNone,
			  ReworkAllowance);
	TestEqual(TEXT("and takes 25% off the damage taken"), Player.On100(TEXT("damage_taken")) / TakenAtNone, 0.75f,
			  ReworkAllowance);
	Player.SetFervour(120.0f);
	TestEqual(TEXT("and no more than 25% at 120"), Player.On100(TEXT("damage_taken")) / TakenAtNone, 0.75f,
			  ReworkAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkHeavyUrnTest,
	"Cataclysm.SoulCollectorRework.HeavyUrnPaysForEachFervourAboveFifty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Heavy Urn: "You deal 0.5% more damage for each Fervour you hold above 50." Against the starting node alone at the same
 * Fervour: nothing at 50 (THE CONTROL, the offset), 25% more at 100 and 35% more at 120.
 */
bool FCataclysmSoulCollectorReworkHeavyUrnTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}
	const auto DamageAt = [&Player](float Fervour)
	{
		Player.SetFervour(Fervour);
		return Player.On100(TEXT("attack_damage"));
	};

	Player.Wear({});
	const float Plain50 = DamageAt(50.0f);
	const float Plain100 = DamageAt(100.0f);
	const float Plain120 = DamageAt(120.0f);
	Player.Wear({{TEXT("SoulCollector_keystone_a_kB"), 1}});
	TestEqual(TEXT("control: at 50 Fervour Heavy Urn adds nothing"), DamageAt(50.0f) / Plain50, 1.0f, ReworkAllowance);
	TestEqual(TEXT("at 100, 25% more"), DamageAt(100.0f) / Plain100, 1.25f, ReworkAllowance);
	TestEqual(TEXT("at 120, 35% more"), DamageAt(120.0f) / Plain120, 1.35f, ReworkAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkGatheredSoulsTest,
	"Cataclysm.SoulCollectorRework.GatheredSoulsCountsADeathWithinSixMetresAndNotSeven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Gathered Souls: "Each enemy that dies within 6 metres of you, whoever kills it, gives you a stack of Gathered Souls.
 * Each stack gives 2% increased Attack Speed and 2% increased Movement Speed". Two creatures are killed by having their
 * health set to nothing, which no character of the player's did, so the death reaches the player through
 * `ACataclysmPlayerCharacter`'s death listener exactly as any other death near it does. THE CONTROL: one 7 metres away
 * adds nothing; one 5 metres away adds a stack.
 */
bool FCataclysmSoulCollectorReworkGatheredSoulsTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}
	Player.Wear({{TEXT("SoulCollector_keystone_spine_001"), 1}});
	ACataclysmEnemyCharacter* Far = ReworkCreature(World, FVector(7.0f * ReworkMetre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Near = ReworkCreature(World, FVector(0.0f, 5.0f * ReworkMetre, 0.0f));
	if (!TestNotNull(TEXT("set-up: a creature 7 metres away"), Far)
		|| !TestNotNull(TEXT("set-up: and one 5 metres away"), Near))
	{
		return false;
	}
	const float SpeedBefore = Player.Increases(TEXT("attack_speed"));
	const float MovingBefore = Player.Increases(TEXT("movement_speed"));

	Far->GetAbilitySystemComponent()->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
	TestTrue(TEXT("set-up: the far creature died"), UCataclysmSkillEffects::IsDead(Far));
	TestEqual(TEXT("control: a death 7 metres away gives no stack"), Player.Increases(TEXT("attack_speed")), SpeedBefore,
			  ReworkAllowance);

	Near->GetAbilitySystemComponent()->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
	TestTrue(TEXT("set-up: the near creature died"), UCataclysmSkillEffects::IsDead(Near));
	TestEqual(TEXT("a death 5 metres away gives 2% increased attack speed"),
			  Player.Increases(TEXT("attack_speed")) - SpeedBefore, 0.02f, ReworkAllowance);
	TestEqual(TEXT("and 2% increased movement speed"), Player.Increases(TEXT("movement_speed")) - MovingBefore, 0.02f,
			  ReworkAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkUnquietSoulsTest,
	"Cataclysm.SoulCollectorRework.UnquietSoulsStrikesForEachFiveFervourDecayed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Unquiet Souls: "Every 5 Fervour you lose to decay strikes the strongest enemy within 10 metres for 40% of your weapon
 * damage." Decay is recorded through `NoteFervourDecayed`, which both decays call. THE CONTROL: 4 decayed strikes
 * nothing; the fifth makes 5 and the creature 5 metres away is struck.
 */
bool FCataclysmSoulCollectorReworkUnquietSoulsTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}
	Player.Wear({{TEXT("SoulCollector_keystone_spine_002"), 1}});
	ACataclysmEnemyCharacter* Creature = ReworkCreature(World, FVector(5.0f * ReworkMetre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a creature 5 metres away"), Creature))
	{
		return false;
	}
	const float Full = CreatureHealth(Creature);

	Player.System->NoteFervourDecayed(4.0f);
	TestEqual(TEXT("control: 4 Fervour decayed strikes nothing"), CreatureHealth(Creature), Full, ReworkAllowance);
	Player.System->NoteFervourDecayed(1.0f);
	TestTrue(TEXT("the fifth makes 5, and the creature is struck"), CreatureHealth(Creature) < Full);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkTollingCountTest,
	"Cataclysm.SoulCollectorRework.TollingCountBlastsOnRisingToAnOpenedThresholdOnceInFiveSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Tolling Count: "Each time your Fervour rises to a threshold you have opened, you release a blast dealing 100% of your
 * weapon damage to every enemy within 5 metres. Each threshold can do this once every 5 seconds." 30 points open the
 * first threshold, 20 Fervour. THE CONTROLS: the first step only records; a fall raises nothing; rising again within 5
 * seconds raises nothing; after 5 seconds it raises again.
 */
bool FCataclysmSoulCollectorReworkTollingCountTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}
	Player.Wear({{TEXT("SoulCollector_keystone_a_kC"), 1}}, /*PointsBesides=*/28);
	ACataclysmEnemyCharacter* Creature = ReworkCreature(World, FVector(3.0f * ReworkMetre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a creature 3 metres away"), Creature))
	{
		return false;
	}
	const float Full = CreatureHealth(Creature);

	Player.SetFervour(10.0f);
	TestEqual(TEXT("control: the first step only records"), Player.System->RaiseFervourThresholdsReached(), 0);
	Player.SetFervour(25.0f);
	TestEqual(TEXT("rising to 20 raises the event once"), Player.System->RaiseFervourThresholdsReached(), 1);
	const float AfterTheBlast = CreatureHealth(Creature);
	TestTrue(TEXT("and the blast struck the creature 3 metres away"), AfterTheBlast < Full);
	Player.SetFervour(10.0f);
	TestEqual(TEXT("control: a fall raises nothing"), Player.System->RaiseFervourThresholdsReached(), 0);
	Player.SetFervour(25.0f);
	TestEqual(TEXT("control: rising again within 5 seconds raises nothing"),
			  Player.System->RaiseFervourThresholdsReached(), 0);
	TestEqual(TEXT("and strikes nothing"), CreatureHealth(Creature), AfterTheBlast, ReworkAllowance);
	Player.SetFervour(10.0f);
	Player.System->RaiseFervourThresholdsReached();
	RunClock(World, 5.2f);
	Player.SetFervour(25.0f);
	TestEqual(TEXT("after 5 seconds rising raises it again"), Player.System->RaiseFervourThresholdsReached(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkTrophyTest,
	"Cataclysm.SoulCollectorRework.TrophyOfTheStrongIsGrantedByAnEliteKillAndNotACommonOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Trophy of the Strong: "Killing an Elite or stronger creature gives you 20% more damage for 10 seconds." The rarity is
 * the slain creature's, judged where the kill grants the stack. THE CONTROL: a Common kill grants nothing; an Elite kill
 * gives 20% more; a second Elite kill gives no more than that.
 */
bool FCataclysmSoulCollectorReworkTrophyTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}
	Player.Wear({{TEXT("SoulCollector_keystone_b_kB"), 1}});
	Player.SetFervour(0.0f);
	ACataclysmEnemyCharacter* Common = ReworkCreature(World, FVector(5.0f * ReworkMetre, 0.0f, 0.0f), 0);
	ACataclysmEnemyCharacter* Elite = ReworkCreature(World, FVector(-5.0f * ReworkMetre, 0.0f, 0.0f), 1);
	if (!TestNotNull(TEXT("set-up: a Common creature"), Common) || !TestNotNull(TEXT("set-up: an Elite"), Elite))
	{
		return false;
	}
	const float Plain = Player.On100(TEXT("attack_damage"));

	Player.Kill(Common);
	TestEqual(TEXT("control: a Common kill gives nothing"), Player.On100(TEXT("attack_damage")), Plain, ReworkAllowance);
	Player.Kill(Elite);
	TestEqual(TEXT("an Elite kill gives 20% more"), Player.On100(TEXT("attack_damage")) / Plain, 1.2f, ReworkAllowance);
	Player.Kill(Elite);
	TestEqual(TEXT("and a second gives no more"), Player.On100(TEXT("attack_damage")) / Plain, 1.2f, ReworkAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkReapersRhythmTest,
	"Cataclysm.SoulCollectorRework.ReapersRhythmNeedsFiveKillsInTheChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Reaper's Rhythm: "While your current chain of kills has 5 or more kills, you deal 15% more damage and take 10% less
 * damage." THE CONTROL: four kills give nothing; the fifth gives both; a sixth adds nothing more.
 */
bool FCataclysmSoulCollectorReworkReapersRhythmTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}
	Player.Wear({{TEXT("SoulCollector_keystone_b_kC"), 1}});
	Player.SetFervour(0.0f);
	ACataclysmEnemyCharacter* Common = ReworkCreature(World, FVector(5.0f * ReworkMetre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a creature"), Common))
	{
		return false;
	}
	const float Plain = Player.On100(TEXT("attack_damage"));
	const float Taken = Player.On100(TEXT("damage_taken"));

	for (int32 Each = 0; Each < 4; ++Each)
	{
		Player.Kill(Common);
	}
	TestEqual(TEXT("control: four kills give no more damage"), Player.On100(TEXT("attack_damage")), Plain,
			  ReworkAllowance);
	TestEqual(TEXT("control: and take nothing off"), Player.On100(TEXT("damage_taken")), Taken, ReworkAllowance);
	Player.Kill(Common);
	TestEqual(TEXT("the fifth gives 15% more damage"), Player.On100(TEXT("attack_damage")) / Plain, 1.15f,
			  ReworkAllowance);
	TestEqual(TEXT("and 10% less damage taken"), Player.On100(TEXT("damage_taken")) / Taken, 0.9f, ReworkAllowance);
	Player.Kill(Common);
	TestEqual(TEXT("and a sixth no more"), Player.On100(TEXT("attack_damage")) / Plain, 1.15f, ReworkAllowance);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// The small nodes
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkTitheTest,
	"Cataclysm.SoulCollectorRework.TitheOfTheDeadReachesAnEnemySixMetresAwayAndNotSeven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Tithe of the Dead, 12 points: "+1% increased damage per point against enemies within 6 metres of you", the distance
 * to the struck enemy an outgoing hit carries. THE CONTROL: against an enemy 7 metres away, nothing.
 */
bool FCataclysmSoulCollectorReworkTitheTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}
	Player.Wear({{TEXT("SoulCollector_basic_spine_001"), 12}});
	Player.SetFervour(0.0f);
	const auto DamageAtMetres = [&Player](float Metres)
	{
		return Player.System->StatForSkill(FName(TEXT("attack_damage")), FGameplayTagContainer(), 0.0f, -1.0f,
										   FCataclysmBlowContext(), -1.0f, Metres);
	};
	const float Others = Player.Increases(TEXT("attack_damage"));
	const float At7 = DamageAtMetres(7.0f);
	const float At5 = DamageAtMetres(5.0f);
	if (!TestTrue(TEXT("set-up: the character's attack damage is above nought"), At7 > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("against an enemy 5 metres away, 12% increased more than 7 metres away"), At5 / At7,
			  (1.0f + Others + 0.12f) / (1.0f + Others), ReworkAllowance);
	TestEqual(TEXT("control: 7 metres away is what no distance at all gives"), At7, DamageAtMetres(-1.0f),
			  ReworkAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkGravePatienceTest,
	"Cataclysm.SoulCollectorRework.GravePatienceHealsForEachFiveFervourDecayed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Grave Patience, 10 points: "Every 5 Fervour you lose to decay restores 0.2% of your maximum health per point": 2% of
 * 10,000, 200. THE CONTROL: 4 decayed restores nothing.
 */
bool FCataclysmSoulCollectorReworkGravePatienceTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}
	Player.Wear({{TEXT("SoulCollector_basic_spine_004"), 10}});
	Player.SetHealth(5000.0f);
	Player.System->NoteFervourDecayed(4.0f);
	TestEqual(TEXT("control: 4 decayed restores nothing"), Player.Health(), 5000.0f, 0.01f);
	Player.System->NoteFervourDecayed(1.0f);
	TestEqual(TEXT("the fifth restores 2% of 10,000"), Player.Health(), 5200.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkClaimedTest,
	"Cataclysm.SoulCollectorRework.ClaimedHoldsForFourSecondsAfterAKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Claimed, 8 points: "+1% increased damage per point for 4 seconds after you kill an enemy". THE CONTROL: before any
 * kill, nothing; at 4.2 seconds after, nothing again.
 */
bool FCataclysmSoulCollectorReworkClaimedTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}
	Player.Wear({{TEXT("SoulCollector_basic_spine_007"), 8}});
	ACataclysmEnemyCharacter* Common = ReworkCreature(World, FVector(5.0f * ReworkMetre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a creature"), Common))
	{
		return false;
	}
	const float Before = Player.Increases(TEXT("attack_damage"));
	UCataclysmFervour::NoteOwnKill(Player.System, Common);
	TestEqual(TEXT("just after a kill, 8% increased"), Player.Increases(TEXT("attack_damage")) - Before, 0.08f,
			  ReworkAllowance);
	RunClock(World, 4.2f);
	TestEqual(TEXT("control: 4.2 seconds after, nothing"), Player.Increases(TEXT("attack_damage")), Before,
			  ReworkAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkFiftyTest,
	"Cataclysm.SoulCollectorRework.MisersGripAndColdStorageNeedFiftyFervour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Miser's Grip and Cold Storage, 8 points each: "+2% increased damage per point" and "You take 1% reduced damage per
 * point", both "while you hold 50 or more Fervour": 16% and 8%. THE CONTROL: at 49 Fervour, neither.
 */
bool FCataclysmSoulCollectorReworkFiftyTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}
	Player.Wear({{TEXT("SoulCollector_basic_a_b1"), 8}, {TEXT("SoulCollector_basic_a_b2"), 8}});
	Player.SetFervour(49.0f);
	const float Damage49 = Player.Increases(TEXT("attack_damage"));
	const float Taken49 = Player.Increases(TEXT("damage_taken"));
	Player.SetFervour(50.0f);
	TestEqual(TEXT("at 50, 16% increased damage more than at 49"), Player.Increases(TEXT("attack_damage")) - Damage49,
			  0.16f, ReworkAllowance);
	TestEqual(TEXT("and 8% reduced damage taken"), Player.Increases(TEXT("damage_taken")) - Taken49, -0.08f,
			  ReworkAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkGleaningTest,
	"Cataclysm.SoulCollectorRework.GleaningStrikesOnItsRollAndNotOtherwise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Gleaning, 6 points: "Each kill has a 5% chance per point to strike the strongest enemy within 10 metres for 50% of your
 * weapon damage", a chance of 30. The roll is pinned. THE CONTROL: a roll of 99 strikes nothing; a roll of 0 strikes the
 * other creature, and never the one slain.
 */
bool FCataclysmSoulCollectorReworkGleaningTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}
	Player.Wear({{TEXT("SoulCollector_basic_b_stem0"), 6}});
	ACataclysmEnemyCharacter* Slain = ReworkCreature(World, FVector(3.0f * ReworkMetre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Other = ReworkCreature(World, FVector(0.0f, 5.0f * ReworkMetre, 0.0f));
	if (!TestNotNull(TEXT("set-up: a creature to slay"), Slain) || !TestNotNull(TEXT("set-up: and another"), Other))
	{
		return false;
	}
	// THE SLAIN CREATURE DEAD FIRST, as it is when the kill is announced, so the strike looks past it.
	Slain->GetAbilitySystemComponent()->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
	const float Full = CreatureHealth(Other);
	{
		const FPinnedChanceRoll High(99.0f);
		Player.Kill(Slain);
	}
	TestEqual(TEXT("control: a roll of 99 strikes nothing"), CreatureHealth(Other), Full, ReworkAllowance);
	{
		const FPinnedChanceRoll Low(0.0f);
		Player.Kill(Slain);
	}
	TestTrue(TEXT("a roll of 0 strikes the other creature"), CreatureHealth(Other) < Full);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkThinningTest,
	"Cataclysm.SoulCollectorRework.ThinningTheHerdCountsEveryTwoEnemiesWithinEightMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Thinning the Herd, 6 points: "+1% increased damage per point for every 2 enemies within 8 metres of you". THE CONTROL:
 * one enemy within 8 metres and one at 9 give nothing; three within give one step, 6%; four give two, 12%.
 */
bool FCataclysmSoulCollectorReworkThinningTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}
	Player.Wear({{TEXT("SoulCollector_basic_b_stem2"), 6}});
	const float Before = Player.Increases(TEXT("attack_damage"));
	ReworkCreature(World, FVector(9.0f * ReworkMetre, 0.0f, 0.0f));
	ReworkCreature(World, FVector(3.0f * ReworkMetre, 0.0f, 0.0f));
	TestEqual(TEXT("control: one within 8 metres and one at 9 give nothing"), Player.Increases(TEXT("attack_damage")),
			  Before, ReworkAllowance);
	ReworkCreature(World, FVector(0.0f, 3.0f * ReworkMetre, 0.0f));
	ReworkCreature(World, FVector(-3.0f * ReworkMetre, 0.0f, 0.0f));
	TestEqual(TEXT("three within give one step, 6%"), Player.Increases(TEXT("attack_damage")) - Before, 0.06f,
			  ReworkAllowance);
	ReworkCreature(World, FVector(0.0f, -3.0f * ReworkMetre, 0.0f));
	TestEqual(TEXT("four give two, 12%"), Player.Increases(TEXT("attack_damage")) - Before, 0.12f, ReworkAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkTearLooseTest,
	"Cataclysm.SoulCollectorRework.TearLooseHealsOnAnEliteKillAndNotACommonOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Tear Loose, 8 points: "Killing an Elite or stronger creature restores 1% of your maximum health per point": 8% of
 * 10,000. THE CONTROL: a Common kill restores nothing.
 */
bool FCataclysmSoulCollectorReworkTearLooseTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}
	Player.Wear({{TEXT("SoulCollector_basic_b_b1"), 8}});
	ACataclysmEnemyCharacter* Common = ReworkCreature(World, FVector(5.0f * ReworkMetre, 0.0f, 0.0f), 0);
	ACataclysmEnemyCharacter* Elite = ReworkCreature(World, FVector(-5.0f * ReworkMetre, 0.0f, 0.0f), 1);
	if (!TestNotNull(TEXT("set-up: a Common creature"), Common) || !TestNotNull(TEXT("set-up: an Elite"), Elite))
	{
		return false;
	}
	Player.SetHealth(5000.0f);
	Player.Kill(Common);
	TestEqual(TEXT("control: a Common kill restores nothing"), Player.Health(), 5000.0f, 0.01f);
	Player.Kill(Elite);
	TestEqual(TEXT("an Elite kill restores 8% of 10,000"), Player.Health(), 5800.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkVigilTest,
	"Cataclysm.SoulCollectorRework.VigilOverTheStrongNeedsAnEliteWithinFifteenMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Vigil Over the Strong, 8 points: "+1% increased Attack Speed per point while an Elite or stronger creature is within
 * 15 metres of you". THE CONTROL: a Common creature 10 metres away and an Elite 20 metres away give nothing; an Elite
 * 10 metres away gives 8%.
 */
bool FCataclysmSoulCollectorReworkVigilTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}
	Player.Wear({{TEXT("SoulCollector_basic_b_b2"), 8}});
	const float Before = Player.Increases(TEXT("attack_speed"));
	ReworkCreature(World, FVector(10.0f * ReworkMetre, 0.0f, 0.0f), 0);
	ReworkCreature(World, FVector(20.0f * ReworkMetre, 0.0f, 0.0f), 1);
	TestEqual(TEXT("control: a Common creature near and an Elite far give nothing"), Player.Increases(TEXT("attack_speed")),
			  Before, ReworkAllowance);
	ReworkCreature(World, FVector(0.0f, 10.0f * ReworkMetre, 0.0f), 1);
	TestEqual(TEXT("an Elite 10 metres away gives 8%"), Player.Increases(TEXT("attack_speed")) - Before, 0.08f,
			  ReworkAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSoulCollectorReworkChainTest,
	"Cataclysm.SoulCollectorRework.GatheringPaceAndBorrowedTimeCountTheKillsInTheChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Gathering Pace, 8 points: "+0.5% increased Attack Speed per point for each kill in your current chain of kills", 4% a
 * kill; Borrowed Time, 6 points: "You take 0.5% reduced damage per point for every 2 kills in your current chain", 3%
 * for every 2. THE CONTROLS: one kill gives Borrowed Time nothing; three give it one step; and a chain broken by 3
 * seconds without a kill gives neither anything.
 */
bool FCataclysmSoulCollectorReworkChainTest::RunTest(const FString&)
{
	using namespace CataclysmSoulCollectorReworkTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FReworkPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed Death player"), Player.IsReady()))
	{
		return false;
	}
	Player.Wear({{TEXT("SoulCollector_basic_b_c0"), 8}, {TEXT("SoulCollector_basic_b_c1"), 6}});
	ACataclysmEnemyCharacter* Common = ReworkCreature(World, FVector(5.0f * ReworkMetre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a creature"), Common))
	{
		return false;
	}
	const float Speed = Player.Increases(TEXT("attack_speed"));
	const float Taken = Player.Increases(TEXT("damage_taken"));

	Player.Kill(Common);
	TestEqual(TEXT("one kill: 4% attack speed"), Player.Increases(TEXT("attack_speed")) - Speed, 0.04f, ReworkAllowance);
	TestEqual(TEXT("control: and nothing off the damage taken"), Player.Increases(TEXT("damage_taken")), Taken,
			  ReworkAllowance);
	Player.Kill(Common);
	Player.Kill(Common);
	TestEqual(TEXT("three kills: 12% attack speed"), Player.Increases(TEXT("attack_speed")) - Speed, 0.12f,
			  ReworkAllowance);
	TestEqual(TEXT("and one step of 3% reduced damage taken"), Player.Increases(TEXT("damage_taken")) - Taken, -0.03f,
			  ReworkAllowance);
	Player.Kill(Common);
	TestEqual(TEXT("four kills: two steps, 6%"), Player.Increases(TEXT("damage_taken")) - Taken, -0.06f,
			  ReworkAllowance);
	RunClock(World, 3.0f);
	TestEqual(TEXT("control: 3 seconds without a kill ends the chain"), Player.Increases(TEXT("attack_speed")), Speed,
			  ReworkAllowance);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
