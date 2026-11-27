// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * The first Death nodes that act on a kill (the Death trees' slice C4a, 2026-11-25): the Soul Collector's Soul Tide,
 * and the Shadow's Death's Patience, Flicker, Clean Kill and Hunting Leap. The mechanism
 * of each is read with stats given by hand, so each figure is exact; one test reads the seven nodes' real rows.
 */

namespace CataclysmKillHooksTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;
	using CataclysmTestWorld::RunClock;

	constexpr float Metre = 100.0f;

	/** A character on the players' side with health and a Fervour pool of 100, empty. */
	UCataclysmAbilitySystemComponent* KillerAt(UWorld* World, const FVector& Where)
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
		if (!System)
		{
			return nullptr;
		}
		System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Holder));
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);
		return System;
	}

	/** A creature of the monsters' side with a thousand health. */
	ACataclysmEnemyCharacter* VictimAt(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Actor = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Actor)
		{
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Actor->SetHealth(1000.0f);
		}
		return Actor;
	}

	/** A flat modifier, optionally under a condition. */
	FCataclysmStatModifier FlatRow(float Value, ECataclysmStatCondition Condition = ECataclysmStatCondition::Always,
								   float ConditionValue = 0.0f)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::Flat;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		Row.Condition = Condition;
		Row.ConditionValue = ConditionValue;
		return Row;
	}

	/** The Fervour one kill pays from an empty bar. */
	float PaidForAKill(UCataclysmAbilitySystemComponent* System, const AActor* Slain)
	{
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);
		return UCataclysmFervour::GainOnOwnKill(System, Slain);
	}

	/**
	 * A real ten-second ailment of this kind on `Carrier`, laid by `Applier` through the game's own damage over time,
	 * at 5 a tick and not scaled by the applier, so it cannot kill. Whether it took.
	 */
	bool LayAilment(AActor* Applier, AActor* Carrier, const TCHAR* AilmentTag)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(AilmentTag), /*ErrorIfNotFound=*/false);
		return Applier && Carrier && Tag.IsValid()
			&& UCataclysmSkillEffects::ApplyDamageOverTime(Applier, Carrier, /*DamagePerTick=*/5.0f,
														   /*DurationSeconds=*/10.0f, Tag,
														   /*bScalesWithInstigator=*/false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillHooksThinningTest,
	"Cataclysm.KillHooks.ThinningTheHerdCountsTheLivingEnemiesNearAndNotTheDead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Thinning the Herd's row given by hand at +50% increased Fervour a kill for each living enemy within 8 metres (the node
 * reaches 6% at its 6 points; 50 keeps each figure whole), on a kill that pays 2: two enemies at 3 and 5 metres make it
 * 4. Once the one at 3 metres is dead, as a slain creature is when its kill is paid, it is 3. THE CONTROL: an enemy 12
 * metres off is never counted.
 */
bool FCataclysmKillHooksThinningTest::RunTest(const FString&)
{
	using namespace CataclysmKillHooksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = KillerAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Near = VictimAt(World, FVector(3.0f * Metre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Nearer = VictimAt(World, FVector(0.0f, 5.0f * Metre, 0.0f));
	ACataclysmEnemyCharacter* Far = VictimAt(World, FVector(-12.0f * Metre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a killer"), System) || !TestNotNull(TEXT("set-up: an enemy at 3 m"), Near)
		|| !TestNotNull(TEXT("set-up: an enemy at 5 m"), Nearer) || !TestNotNull(TEXT("set-up: an enemy at 12 m"), Far))
	{
		return false;
	}
	FCataclysmStatModifier Herd;
	Herd.Bucket = ECataclysmStatBucket::Increased;
	Herd.Source = ECataclysmModifierSource::PassiveKeystone;
	Herd.Value = 50.0f;
	Herd.Scale = ECataclysmStatScale::PerEnemyInReach;
	Herd.ScaleStep = 1.0f;
	Herd.ScaleMaxSteps = 10;
	Herd.ReachMetres = 8.0f;
	TMap<FName, FCataclysmStatInputs> Inputs;
	Inputs.FindOrAdd(FName(UCataclysmFervour::PerKillStat)).Modifiers = {FlatRow(2.0f), Herd};
	System->SetStatInputs(MoveTemp(Inputs));

	TestEqual(TEXT("two living enemies within 8 m, the one at 12 m left out: 2 times 2"), PaidForAKill(System, Near),
			  4.0f, 0.001f);
	UCataclysmSkillEffects::MarkDead(Near);
	TestEqual(TEXT("the one at 3 m dead: 2 times 1.5"), PaidForAKill(System, Near), 3.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillHooksPatienceTest,
	"Cataclysm.KillHooks.DeathsPatienceAsksForYourPoisonOrBleedOnTheSlain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Death's Patience at three points, +3 Fervour for a kill of an enemy carrying your Poison or Bleed, on a kill that pays
 * 1: the slain creature carrying the killer's Poison pays 4, and one carrying the killer's Bleed pays 4. THE CONTROLS: a
 * creature carrying nothing pays 1, and one carrying another character's Poison pays 1.
 */
bool FCataclysmKillHooksPatienceTest::RunTest(const FString&)
{
	using namespace CataclysmKillHooksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = KillerAt(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* Someone = KillerAt(World, FVector(0.0f, 20.0f * Metre, 0.0f));
	ACataclysmEnemyCharacter* Bare = VictimAt(World, FVector(3.0f * Metre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Poisoned = VictimAt(World, FVector(-3.0f * Metre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Bleeding = VictimAt(World, FVector(0.0f, 3.0f * Metre, 0.0f));
	ACataclysmEnemyCharacter* TheirPoison = VictimAt(World, FVector(0.0f, -3.0f * Metre, 0.0f));
	if (!TestNotNull(TEXT("set-up: a killer"), System) || !TestNotNull(TEXT("set-up: someone else"), Someone)
		|| !Bare || !Poisoned || !Bleeding || !TheirPoison)
	{
		AddError(TEXT("set-up: four enemies"));
		return false;
	}
	AActor* Killer = System->GetAvatarActor();
	if (!TestTrue(TEXT("set-up: the killer poisons one"), LayAilment(Killer, Poisoned, TEXT("Keyword.DoT.Poison")))
		|| !TestTrue(TEXT("set-up: and bleeds another"), LayAilment(Killer, Bleeding, TEXT("Keyword.DoT.Bleed")))
		|| !TestTrue(TEXT("set-up: someone else poisons a third"),
					 LayAilment(Someone->GetAvatarActor(), TheirPoison, TEXT("Keyword.DoT.Poison"))))
	{
		return false;
	}
	TMap<FName, FCataclysmStatInputs> Inputs;
	Inputs.FindOrAdd(FName(UCataclysmFervour::PerKillStat)).Modifiers = {
		FlatRow(1.0f), FlatRow(3.0f, ECataclysmStatCondition::TargetCarriesYourPoisonOrBleed)};
	System->SetStatInputs(MoveTemp(Inputs));

	TestTrue(TEXT("set-up: the Poison is the killer's"), UCataclysmDebuffs::CarriesPoisonOrBleedLaidBy(Poisoned, Killer));
	TestEqual(TEXT("your Poison: 4"), PaidForAKill(System, Poisoned), 4.0f, 0.001f);
	TestEqual(TEXT("your Bleed: 4"), PaidForAKill(System, Bleeding), 4.0f, 0.001f);
	TestEqual(TEXT("control: nothing carried: 1"), PaidForAKill(System, Bare), 1.0f, 0.001f);
	TestFalse(TEXT("control: another character's Poison is not yours"),
			  UCataclysmDebuffs::CarriesPoisonOrBleedLaidBy(TheirPoison, Killer));
	TestEqual(TEXT("and pays 1"), PaidForAKill(System, TheirPoison), 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillHooksCriticalTest,
	"Cataclysm.KillHooks.ADeathNoticeSaysWhetherTheKillingBlowWasCritical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * For Clean Kill: a killing blow that is a critical strike gives a death notice saying so. An attacker with a 50% chance
 * to crit kills one enemy with the roll pinned at 0 and another with the roll pinned at 100. THE CONTROL is the second:
 * the same blow, not critical, says no.
 */
bool FCataclysmKillHooksCriticalTest::RunTest(const FString&)
{
	using namespace CataclysmKillHooksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	UCataclysmAbilitySystemComponent* System = KillerAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* First = VictimAt(World, FVector(1.0f * Metre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Second = VictimAt(World, FVector(-1.0f * Metre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the combat events"), Events) || !TestNotNull(TEXT("set-up: a killer"), System)
		|| !First || !Second)
	{
		return false;
	}
	System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100000.0f);
	System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 50.0f);
	System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetCritMultiplierAttribute(), 150.0f);
	TMap<const AActor*, bool> Critical;
	const FDelegateHandle Handle = Events->OnDeath.AddLambda([&Critical](const FCataclysmDeathNotice& Notice)
	{
		Critical.Add(Notice.Victim, Notice.bByACriticalStrike);
	});
	ON_SCOPE_EXIT { Events->OnDeath.Remove(Handle); };

	AActor* Attacker = System->GetAvatarActor();
	{
		const CataclysmTestWorld::FScopedCritRoll AlwaysCrits(0.0f);
		UCataclysmSkillEffects::ApplyHit(Attacker, First, /*DamagePercent=*/100.0f);
	}
	{
		const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
		UCataclysmSkillEffects::ApplyHit(Attacker, Second, /*DamagePercent=*/100.0f);
	}
	if (!TestTrue(TEXT("set-up: both died"), Critical.Contains(First) && Critical.Contains(Second)))
	{
		return false;
	}
	TestTrue(TEXT("a critical killing blow: the notice says so"), Critical[First]);
	TestFalse(TEXT("control: the same blow not critical: the notice says no"), Critical[Second]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillHooksCleanKillTest,
	"Cataclysm.KillHooks.CleanKillRestoresItsShareOfMaximumHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Clean Kill at eight points, 4% of maximum health: a character at 500 of 1,000 health is restored 40. THE CONTROL:
 * without the node it is restored nothing. The player's kill handler calls this only for a notice saying the killing
 * blow was critical; `tools/tests/test_a_critical_killing_blow_reaches_clean_kill.py` reads that from the source.
 */
bool FCataclysmKillHooksCleanKillTest::RunTest(const FString&)
{
	using namespace CataclysmKillHooksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = KillerAt(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("set-up: a killer"), System))
	{
		return false;
	}
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
	System->SetNumericAttributeBase(Health, 500.0f);

	TestEqual(TEXT("control: without Clean Kill, nothing"), UCataclysmFervour::RestoreHealthOnCriticalKill(System),
			  0.0f, 0.001f);
	TMap<FName, FCataclysmStatInputs> Inputs;
	Inputs.FindOrAdd(FName(UCataclysmFervour::HealthRestoredOnCriticalKillStat)).Modifiers = {FlatRow(4.0f)};
	System->SetStatInputs(MoveTemp(Inputs));
	TestEqual(TEXT("at 4%, 40 restored"), UCataclysmFervour::RestoreHealthOnCriticalKill(System), 40.0f, 0.01f);
	TestEqual(TEXT("so 540 held"), System->GetNumericAttribute(Health), 540.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillHooksLeapTest,
	"Cataclysm.KillHooks.HuntingLeapDoublesForTwoSecondsAfterAKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Hunting Leap at its 6 points, +18% increased range for the movement skill and 18% more for 2 seconds after a kill:
 * 18% before any kill, 36% just after one, 18% again 2.1 seconds later. THE CONTROL: a skill that is not the movement
 * skill gains nothing.
 */
bool FCataclysmKillHooksLeapTest::RunTest(const FString&)
{
	using namespace CataclysmKillHooksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = KillerAt(World, FVector::ZeroVector);
	const FGameplayTag MovementTag =
		FGameplayTag::RequestGameplayTag(FName(TEXT("Slot.Movement")), /*ErrorIfNotFound=*/false);
	if (!TestNotNull(TEXT("set-up: a killer"), System) || !TestTrue(TEXT("set-up: the movement tag"), MovementTag.IsValid()))
	{
		return false;
	}
	FCataclysmStatModifier Always;
	Always.Bucket = ECataclysmStatBucket::Increased;
	Always.Source = ECataclysmModifierSource::PassiveKeystone;
	Always.Value = 18.0f;
	Always.RequiredTags.AddTag(MovementTag);
	FCataclysmStatModifier AfterAKill = Always;
	AfterAKill.Condition = ECataclysmStatCondition::WithinSecondsOfKill;
	AfterAKill.ConditionValue = 2.0f;
	TMap<FName, FCataclysmStatInputs> Inputs;
	Inputs.FindOrAdd(FName(TEXT("skill_range"))).Modifiers = {Always, AfterAKill};
	System->SetStatInputs(MoveTemp(Inputs));
	const FGameplayTagContainer Movement(MovementTag);
	const FName Range(TEXT("skill_range"));

	TestEqual(TEXT("before any kill: 18%"), System->IncreasesForStat(Range, Movement), 0.18f, 0.001f);
	System->NoteOwnKill(/*GraceSeconds=*/0.0f);
	TestEqual(TEXT("just after a kill: 36%"), System->IncreasesForStat(Range, Movement), 0.36f, 0.001f);
	TestEqual(TEXT("control: not the movement skill: nothing"),
			  System->IncreasesForStat(Range, FGameplayTagContainer()), 0.0f, 0.001f);
	RunClock(World, 2.1f);
	TestEqual(TEXT("2.1 seconds later: 18% again"), System->IncreasesForStat(Range, Movement), 0.18f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillHooksRealRowsTest,
	"Cataclysm.KillHooks.TheSevenNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The seven nodes' real rows, read from the passive effects table: Soul Tide as the first vow's first option under a
 * 2-second kill clock; Gleaning's 5; Thinning the Herd scaled by the enemies within 8 metres, up to 10; Death's Patience
 * under the new condition; Flicker resetting the movement skill on a kill; Clean Kill at each second point; Hunting Leap's
 * two rows for the movement skill, one under the kill clock.
 */
bool FCataclysmKillHooksRealRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}
	const auto RowsOf = [Effects](const TCHAR* Node)
	{
		return UCataclysmPassiveTree::EffectsFor(Effects, FName(Node));
	};

	int32 Tide = 0;
	for (const FCataclysmPassiveEffectRow* Row : RowsOf(TEXT("SoulCollector_capstone_25")))
	{
		if (Row->Option == 1)
		{
			++Tide;
			TestEqual(TEXT("Soul Tide: Fervour a kill"), Row->Stat, FString(UCataclysmFervour::PerKillStat));
			// UNDER THE KILL-CHAIN CLOCK SINCE SLICE C6c, which Unbroken Chain lengthens; this read the plain kill clock
			// until window death3-w2 ran it.
			TestEqual(TEXT("Soul Tide: under the kill-chain clock"), Row->Condition,
					  FString(TEXT("kill_chain_within_seconds")));
			TestEqual(TEXT("Soul Tide: 2 seconds"), Row->ConditionValue, 2.0f, 0.001f);
		}
	}
	TestEqual(TEXT("Soul Tide: one row"), Tide, 1);

	// THINNING THE HERD LEFT THIS TEST WITH THE SOUL COLLECTOR KEYSTONE REWORK (issue #2951): its new rows are read by
	// `Cataclysm.SoulCollectorRework.TheTwentyNodesRowsAreTheirSentences`.
	const TArray<const FCataclysmPassiveEffectRow*> Patience = RowsOf(TEXT("Shadow_basic_a_stem0"));
	if (TestEqual(TEXT("Death's Patience: one row"), Patience.Num(), 1))
	{
		TestEqual(TEXT("Death's Patience: under your Poison or Bleed"), Patience[0]->Condition,
				  FString(TEXT("target_carries_your_poison_or_bleed")));
	}
	const TArray<const FCataclysmPassiveEffectRow*> Flicker = RowsOf(TEXT("Shadow_keystone_c_kA"));
	if (TestEqual(TEXT("Flicker: one row"), Flicker.Num(), 1))
	{
		TestEqual(TEXT("Flicker: resets the movement skill"), Flicker[0]->Action, FString(TEXT("cooldown_reset_movement")));
		TestEqual(TEXT("Flicker: on a kill"), Flicker[0]->ActionEvent, FString(TEXT("kill")));
	}
	TArray<int32> CleanSteps;
	for (const FCataclysmPassiveEffectRow* Row : RowsOf(TEXT("Shadow_basic_b_a1")))
	{
		CleanSteps.Add(Row->MinPoints);
	}
	CleanSteps.Sort();
	TestTrue(TEXT("Clean Kill: a row at each second point, 2, 4, 6 and 8"), CleanSteps == TArray<int32>({2, 4, 6, 8}));
	int32 Leap = 0;
	int32 LeapAfterAKill = 0;
	for (const FCataclysmPassiveEffectRow* Row : RowsOf(TEXT("Shadow_basic_c_stem2")))
	{
		++Leap;
		LeapAfterAKill += Row->Condition == TEXT("seconds_after_kill") ? 1 : 0;
		TestEqual(TEXT("Hunting Leap: the movement skill's"), Row->RequiredTags, FString(TEXT("Slot.Movement")));
	}
	TestEqual(TEXT("Hunting Leap: two rows"), Leap, 2);
	TestEqual(TEXT("Hunting Leap: one of them after a kill"), LeapAfterAKill, 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
