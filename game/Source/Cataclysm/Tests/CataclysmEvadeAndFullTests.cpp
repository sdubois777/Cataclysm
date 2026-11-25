// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * Evasions and a full bar that speed the next thing (the Death trees' slice C6a, 2026-11-25): Slip's and Brim's next-hit
 * critical strike charge on `dodge` and `resource_full`, Brim's damage while the charge is held, Keep the Count's longer
 * hold before decay, and the stepped action rows of Find the Rhythm, Sidestep and Keep the Count each keyed apart. Three
 * tests give rows by hand; two read the real tables.
 */

namespace CataclysmEvadeAndFullTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;
	using CataclysmTestWorld::RunClock;

	/** A character on the players' side with a Fervour pool of 100, empty. */
	UCataclysmAbilitySystemComponent* EvaderAt(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Holder = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (!Holder)
		{
			return nullptr;
		}
		Holder->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		Holder->SetAttackDamage(0.0f);
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent());
		if (System)
		{
			System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Holder));
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
		}
		return System;
	}

	FCataclysmPoolAction ChargeRow(const TCHAR* Event, float Seconds, const TCHAR* Key)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(Event);
		Action.Percent = Seconds;
		Action.bNextHitCritical = true;
		Action.TriggerKey = FName(Key);
		return Action;
	}

	FCataclysmStatModifier EvadeRow(ECataclysmStatBucket Bucket, float Value,
									ECataclysmStatCondition Condition = ECataclysmStatCondition::Always)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = Bucket;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		Row.Condition = Condition;
		return Row;
	}

	/** The rows the passive tree copies for these points of one Shadow node, named `Name`. */
	int32 CopiesNamed(const TArray<FCataclysmEnchantmentEffectRow>& Rows, const FString& Name)
	{
		int32 Found = 0;
		for (const FCataclysmEnchantmentEffectRow& Row : Rows)
		{
			Found += Row.Enchantment == Name ? 1 : 0;
		}
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEvadeAndFullChargeTest,
	"Cataclysm.EvadeAndFull.SlipAndBrimGrantTheChargeOnAnEvasionAndAFullBar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Slip's row, 3 seconds on `dodge`: an evasion grants the charge, held at 2.5 seconds and gone at 3.5. Brim's row, 0
 * seconds on `resource_full`, "until used": a full bar grants it, still held 60 seconds later, gone once used. THE
 * CONTROL: a character with neither row evades and fills its bar and holds nothing.
 */
bool FCataclysmEvadeAndFullChargeTest::RunTest(const FString&)
{
	using namespace CataclysmEvadeAndFullTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = EvaderAt(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* Bare = EvaderAt(World, FVector(0.0f, 2000.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: an evader"), System) || !TestNotNull(TEXT("set-up: one with no rows"), Bare))
	{
		return false;
	}
	System->SetPoolActions({ChargeRow(TEXT("dodge"), 3.0f, TEXT("Passive_Shadow_basic_d_stem0:next_hit_critical:dodge")),
							ChargeRow(TEXT("resource_full"), 0.0f,
									  TEXT("Passive_Shadow_basic_spine_010:next_hit_critical:resource_full"))});

	Bare->NoteEvaded();
	Bare->ActOnEvent(FName(TEXT("resource_full")));
	TestFalse(TEXT("control: with neither row, an evasion and a full bar grant nothing"), Bare->HoldsNextHitCritical());

	System->NoteEvaded();
	TestTrue(TEXT("Slip: an evasion grants the charge"), System->HoldsNextHitCritical());
	RunClock(World, 2.5f);
	TestTrue(TEXT("held at 2.5 seconds"), System->HoldsNextHitCritical());
	RunClock(World, 1.0f);
	TestFalse(TEXT("gone at 3.5 seconds"), System->HoldsNextHitCritical());

	System->ActOnEvent(FName(TEXT("resource_full")));
	TestTrue(TEXT("Brim: a full bar grants it"), System->HoldsNextHitCritical());
	RunClock(World, 60.0f);
	TestTrue(TEXT("until used: still held 60 seconds later"), System->HoldsNextHitCritical());
	System->SpendNextHitCritical();
	TestFalse(TEXT("and gone once used"), System->HoldsNextHitCritical());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEvadeAndFullBrimDamageTest,
	"Cataclysm.EvadeAndFull.BrimsDamageHoldsOnlyWhileTheChargeIsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Brim's damage row, 5% increased under `next_hit_is_critical`: attack damage of 100 reads 105 while a charge is held.
 * THE CONTROL: 100 before the charge and 100 after it is used.
 */
bool FCataclysmEvadeAndFullBrimDamageTest::RunTest(const FString&)
{
	using namespace CataclysmEvadeAndFullTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = EvaderAt(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("set-up: an evader"), System))
	{
		return false;
	}
	TMap<FName, FCataclysmStatInputs> Lines;
	FCataclysmStatInputs& Attack = Lines.Add(FName(TEXT("attack_damage")));
	Attack.Base = 100.0f;
	Attack.Modifiers = {EvadeRow(ECataclysmStatBucket::Increased, 5.0f, ECataclysmStatCondition::NextHitIsCritical)};
	System->SetStatInputs(MoveTemp(Lines));
	System->SetPoolActions({ChargeRow(TEXT("resource_full"), 0.0f, TEXT("Test:next_hit_critical:resource_full"))});
	const auto Attack100 = [System]()
	{
		return System->StatForSkill(FName(TEXT("attack_damage")), FGameplayTagContainer(), 0.0f);
	};

	TestEqual(TEXT("control: before the charge, 100"), Attack100(), 100.0f, 0.001f);
	System->ActOnEvent(FName(TEXT("resource_full")));
	TestEqual(TEXT("while it is held, 105"), Attack100(), 105.0f, 0.001f);
	System->SpendNextHitCritical();
	TestEqual(TEXT("control: once it is used, 100"), Attack100(), 100.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEvadeAndFullHoldTest,
	"Cataclysm.EvadeAndFull.KeepTheCountLengthensARunningHoldOnAnEvasion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Keep the Count's row, 1 second on `dodge`, with a full hold of 4 seconds after a kill: a kill at 0, an evasion at 1,
 * and at 4.5 seconds the decay is still held (until 5, never past the full length from the evasion). THE CONTROL: the
 * same kill and evasion without the row, and at 4.5 seconds the decay is no longer held.
 */
bool FCataclysmEvadeAndFullHoldTest::RunTest(const FString&)
{
	using namespace CataclysmEvadeAndFullTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = EvaderAt(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* Bare = EvaderAt(World, FVector(0.0f, 2000.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: an evader"), System) || !TestNotNull(TEXT("set-up: one with no row"), Bare))
	{
		return false;
	}
	for (UCataclysmAbilitySystemComponent* Each : {System, Bare})
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		Lines.Add(FName(UCataclysmFervour::KillDecayGraceSecondsStat)).Modifiers = {
			EvadeRow(ECataclysmStatBucket::Flat, 4.0f)};
		Each->SetStatInputs(MoveTemp(Lines));
	}
	FCataclysmPoolAction Keep;
	Keep.Event = FName(TEXT("dodge"));
	Keep.Percent = 1.0f;
	Keep.bExtendFervourHold = true;
	Keep.TriggerKey = FName(TEXT("Passive_Shadow_basic_d_c1_From3:fervour_hold_extended:dodge"));
	System->SetPoolActions({Keep});

	UCataclysmFervour::NoteOwnKill(System, nullptr);
	UCataclysmFervour::NoteOwnKill(Bare, nullptr);
	RunClock(World, 1.0f);
	System->NoteEvaded();
	Bare->NoteEvaded();
	RunClock(World, 3.5f);
	TestTrue(TEXT("with the row, at 4.5 seconds the decay is still held"), System->KillDecayIsHeld());
	TestFalse(TEXT("control: without it, at 4.5 seconds it is not"), Bare->KillDecayIsHeld());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEvadeAndFullSteppedTest,
	"Cataclysm.EvadeAndFull.AStepAtEachThresholdIsItsOwnRowWithItsOwnName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The real tables: Find the Rhythm at 8 points copies two movement cooldown cuts, one named for its step at 4 and one
 * for its step at 8, each of 1 second, so they hold two keys and both act. At 5 points only the first. THE CONTROL: Brim's
 * damage, a stat row at every point, is not copied, and its action keeps the node's own name.
 */
bool FCataclysmEvadeAndFullSteppedTest::RunTest(const FString&)
{
	using namespace CataclysmEvadeAndFullTest;

	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}
	const TArray<FName> Death = {FName(TEXT("Death"))};

	FCataclysmPassiveAllocation Eight;
	Eight.Add(FName(TEXT("Shadow_basic_b_c0")), 8);
	Eight.Add(FName(TEXT("Shadow_basic_spine_010")), 6);
	const TArray<FCataclysmEnchantmentEffectRow> AtEight =
		UCataclysmPassiveTree::EnchantmentRowsFor(Eight, Nodes, Effects, Death);
	TestEqual(TEXT("at 8 points: the step at 4, under its own name"),
			  CopiesNamed(AtEight, TEXT("Passive_Shadow_basic_b_c0_From4")), 1);
	TestEqual(TEXT("and the step at 8, under its own name"),
			  CopiesNamed(AtEight, TEXT("Passive_Shadow_basic_b_c0_From8")), 1);
	for (const FCataclysmEnchantmentEffectRow& Row : AtEight)
	{
		if (Row.Enchantment.StartsWith(TEXT("Passive_Shadow_basic_b_c0")))
		{
			TestEqual(TEXT("each step takes 1 second"), Row.ValueLow, 1.0f, 0.001f);
		}
	}
	TestEqual(TEXT("control: Brim's action keeps the node's own name"),
			  CopiesNamed(AtEight, TEXT("Passive_Shadow_basic_spine_010")), 1);

	FCataclysmPassiveAllocation Five;
	Five.Add(FName(TEXT("Shadow_basic_b_c0")), 5);
	const TArray<FCataclysmEnchantmentEffectRow> AtFive =
		UCataclysmPassiveTree::EnchantmentRowsFor(Five, Nodes, Effects, Death);
	TestEqual(TEXT("at 5 points: the step at 4"), CopiesNamed(AtFive, TEXT("Passive_Shadow_basic_b_c0_From4")), 1);
	TestEqual(TEXT("and not the step at 8"), CopiesNamed(AtFive, TEXT("Passive_Shadow_basic_b_c0_From8")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEvadeAndFullRealRowsTest,
	"Cataclysm.EvadeAndFull.TheNineNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The real rows of the slice's nine nodes and options, read from the passive effects table: each action and stat. */
bool FCataclysmEvadeAndFullRealRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}
	struct FExpected
	{
		const TCHAR* Node;
		int32 Option;
		const TCHAR* ActionOrStat;
		const TCHAR* Event;
		float Value;
		int32 Count;
	};
	const FExpected Expected[] = {
		{TEXT("Shadow_basic_spine_010"), 0, TEXT("next_hit_critical"), TEXT("resource_full"), 0.0f, 1},
		{TEXT("Shadow_basic_spine_010"), 0, TEXT("attack_damage"), TEXT(""), 5.0f, 1},
		{TEXT("Shadow_basic_d_stem0"), 0, TEXT("next_hit_critical"), TEXT("dodge"), 3.0f, 1},
		{TEXT("Shadow_basic_b_c0"), 0, TEXT("cooldown_reduce_movement"), TEXT("critical_strike"), 1.0f, 2},
		{TEXT("Shadow_keystone_d_kC"), 0, TEXT("cooldown_reduce_movement"), TEXT("dodge"), 1.0f, 2},
		{TEXT("Shadow_basic_d_c1"), 0, TEXT("fervour_hold_extended"), TEXT("dodge"), 1.0f, 2},
		{TEXT("SoulCollector_capstone_200"), 3, TEXT("projectile_count"), TEXT(""), 1.0f, 1},
		{TEXT("Shadow_basic_a_stem2"), 0, TEXT("dot_duration"), TEXT(""), 4.0f, 2},
		{TEXT("Shadow_keystone_c_kC"), 0, TEXT("skill_charges_bonus"), TEXT(""), 1.0f, 1},
		{TEXT("Shadow_keystone_d_kA"), 0, TEXT("strike_target"), TEXT("dodge"), 100.0f, 1},
	};
	for (const FExpected& Want : Expected)
	{
		int32 Found = 0;
		for (const FCataclysmPassiveEffectRow* Row : UCataclysmPassiveTree::EffectsFor(Effects, FName(Want.Node)))
		{
			if (Row->Option != Want.Option || (Row->Action != Want.ActionOrStat && Row->Stat != Want.ActionOrStat))
			{
				continue;
			}
			++Found;
			TestEqual(*FString::Printf(TEXT("%s %s: the event"), Want.Node, Want.ActionOrStat), Row->ActionEvent,
					  FString(Want.Event));
			TestEqual(*FString::Printf(TEXT("%s %s: the value"), Want.Node, Want.ActionOrStat), Row->ValuePerPoint,
					  Want.Value, 0.001f);
		}
		TestEqual(*FString::Printf(TEXT("%s %s: rows"), Want.Node, Want.ActionOrStat), Found, Want.Count);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
