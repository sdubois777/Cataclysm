// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * A Boss fight fills the bar without kills (the Death trees' slice C4d2, 2026-11-25): the Shadow's Patient Blade, a kill's
 * Fervour for a tick of your damage over time on a Boss once every 2 seconds, and Death by Inches, every 10th hit you
 * land on one Boss counted as a kill of it. Two tests give rows by hand; two go through a real player's hit handler, on
 * the scene `Cataclysm.Swarm.` builds; one reads the real rows.
 */

namespace CataclysmBossFervourTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;
	using CataclysmTestWorld::RunClock;

	constexpr float BossMetre = 100.0f;
	constexpr int32 BossRung = 4;

	/** A character on the players' side with health and a Fervour pool of 100, empty. */
	UCataclysmAbilitySystemComponent* BossFighterAt(UWorld* World, const FVector& Where)
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

	/** A creature of the monsters' side with this much health and this rarity rung, set alone. */
	ACataclysmEnemyCharacter* RungedCreature(UWorld* World, const FVector& At, float Health, int32 Rung)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(Health);
			// THE RUNG ALONE, as `IsBoss` reads it; `SetRarityStep` would also scale and roll modifiers.
			Made->RarityStep = Rung;
		}
		return Made;
	}

	FCataclysmStatModifier BossRow(float Value, ECataclysmStatCondition Condition = ECataclysmStatCondition::Always)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::Flat;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		Row.Condition = Condition;
		return Row;
	}

	/** 5 Fervour a kill, and `Stat` at `Value` against a Boss, beside any line already given. */
	void AddBossLines(TMap<FName, FCataclysmStatInputs>& Lines, const TCHAR* Stat, float Value)
	{
		Lines.FindOrAdd(FName(UCataclysmFervour::PerKillStat)).Modifiers = {BossRow(5.0f)};
		Lines.FindOrAdd(FName(Stat)).Modifiers = {BossRow(Value, ECataclysmStatCondition::TargetIsBoss)};
	}

	float FervourHeld(const UAbilitySystemComponent* System)
	{
		return System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
	}

	/** A real player on a real player state, as `Cataclysm.Swarm.` builds one, with an empty Fervour pool of 100. */
	struct FBossScene
	{
		ACataclysmPlayerCharacter* Player = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	bool BuildBossScene(FAutomationTestBase& Test, UWorld* World, FBossScene& Out)
	{
		Out.State = World->SpawnActor<ACataclysmPlayerState>();
		Out.Player = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("set-up: a player state"), Out.State)
			|| !Test.TestNotNull(TEXT("set-up: a player"), Out.Player))
		{
			return false;
		}
		Out.Player->SetPlayerState(Out.State);
		Out.Player->OnRep_PlayerState();
		Out.System = Out.State->GetCataclysmAbilitySystemComponent();
		if (!Test.TestNotNull(TEXT("set-up: the player's ability system"), Out.System))
		{
			return false;
		}
		Out.System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		Out.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
		Out.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
		Out.System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
		Out.System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);
		return true;
	}

	/** One blow of the player's at 100%, never a critical strike, in the name of a test strike. */
	void BossBlow(const FBossScene& Scene, AActor* Target)
	{
		UCataclysmStrikeSkill* Swing = NewObject<UCataclysmStrikeSkill>(Scene.Player);
		Swing->SkillName = TEXT("Test Swing");
		Swing->SkillTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Strike"))));
		FCataclysmHitDelivery Delivery;
		Delivery.CritChancePercent = 0.0f;
		Delivery.Skill = Swing;
		UCataclysmSkillEffects::ApplyHit(Scene.Player, Target, /*DamagePercent=*/100.0f, Swing->SkillTags, Delivery);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossFervourPatientTest,
	"Cataclysm.BossFervour.PatientBladePaysAKillForABossTickOnceEveryTwoSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Patient Blade at 2 seconds and 5 Fervour a kill: a tick on a Boss pays 5; a second at once pays nothing; at 2.05
 * seconds one pays 5 again. THE CONTROL: a tick on a Common creature pays nothing and starts no clock.
 */
bool FCataclysmBossFervourPatientTest::RunTest(const FString&)
{
	using namespace CataclysmBossFervourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = BossFighterAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Boss = RungedCreature(World, FVector(5 * BossMetre, 0, 0), 100000.0f, BossRung);
	ACataclysmEnemyCharacter* Common = RungedCreature(World, FVector(-5 * BossMetre, 0, 0), 1000.0f, 0);
	if (!TestNotNull(TEXT("set-up: a fighter"), System) || !Boss || !Common)
	{
		AddError(TEXT("set-up: two creatures"));
		return false;
	}
	TMap<FName, FCataclysmStatInputs> Lines;
	AddBossLines(Lines, UCataclysmFervour::BossTicksPayAKillEverySecondsStat, 2.0f);
	System->SetStatInputs(MoveTemp(Lines));

	TestEqual(TEXT("control: a tick on a Common creature pays nothing"),
			  UCataclysmFervour::PayForABossTick(System, Common), 0.0f, 0.001f);
	TestEqual(TEXT("control: and starts no clock"), System->SecondsSinceBossTickPaid(), -1.0f, 0.001f);
	TestEqual(TEXT("a tick on a Boss pays a kill: 5"), UCataclysmFervour::PayForABossTick(System, Boss), 5.0f, 0.001f);
	TestEqual(TEXT("a second at once pays nothing"), UCataclysmFervour::PayForABossTick(System, Boss), 0.0f, 0.001f);
	RunClock(World, 2.05f);
	TestEqual(TEXT("at 2.05 seconds one pays again: 5"), UCataclysmFervour::PayForABossTick(System, Boss), 5.0f, 0.001f);
	TestEqual(TEXT("10 held"), FervourHeld(System), 10.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossFervourPatientHandlerTest,
	"Cataclysm.BossFervour.PatientBladePaysThroughTheRealTicksOfAPlayersDamageOverTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole path: a real player holding Patient Blade lays a ten-second damage over time on a Boss and one on a Common
 * creature, each ticking once a second. At 1.05 seconds 5 is held: the Boss's tick paid and the Common's did not; at
 * 2.05 still 5, the second tick inside the 2 seconds; at 3.05, 10.
 */
bool FCataclysmBossFervourPatientHandlerTest::RunTest(const FString&)
{
	using namespace CataclysmBossFervourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FBossScene Scene;
	if (!BuildBossScene(*this, World, Scene))
	{
		return false;
	}
	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.Add(FName(TEXT("attack_damage"))).Base = 100.0f;
	AddBossLines(Lines, UCataclysmFervour::BossTicksPayAKillEverySecondsStat, 2.0f);
	Scene.System->SetStatInputs(MoveTemp(Lines));
	ACataclysmEnemyCharacter* Boss = RungedCreature(World, FVector(5 * BossMetre, 0, 0), 100000.0f, BossRung);
	ACataclysmEnemyCharacter* Common = RungedCreature(World, FVector(-5 * BossMetre, 0, 0), 100000.0f, 0);
	const FGameplayTag Bleed = FGameplayTag::RequestGameplayTag(FName(TEXT("Keyword.DoT.Bleed")), false);
	if (!Boss || !Common || !TestTrue(TEXT("set-up: a Bleed tag"), Bleed.IsValid())
		|| !TestTrue(TEXT("set-up: the player bleeds the Boss"),
					 UCataclysmSkillEffects::ApplyDamageOverTime(Scene.Player, Boss, 5.0f, 10.0f, Bleed, false))
		|| !TestTrue(TEXT("set-up: and the Common creature"),
					 UCataclysmSkillEffects::ApplyDamageOverTime(Scene.Player, Common, 5.0f, 10.0f, Bleed, false)))
	{
		return false;
	}

	// THE TICKS ANNOUNCED, counted for window death3-w2's diagnosis: how many reach the hit announcement, and how many
	// name the player as their attacker, which `ACataclysmPlayerCharacter::OnSomethingWasHit` requires.
	int32 TicksAnnounced = 0;
	int32 TicksOfThePlayers = 0;
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	FDelegateHandle Counting;
	if (Events)
	{
		Counting = Events->OnHit.AddLambda([&TicksAnnounced, &TicksOfThePlayers, &Scene](const FCataclysmHitNotice& Notice)
		{
			if (Notice.bDamageOverTime)
			{
				++TicksAnnounced;
				TicksOfThePlayers += Notice.Attacker == Scene.Player ? 1 : 0;
			}
		});
	}
	RunClock(World, 1.05f);
	if (Events)
	{
		Events->OnHit.Remove(Counting);
	}
	AddInfo(FString::Printf(TEXT("PATIENT BLADE DIAGNOSIS: %d tick(s) announced in 1.05 seconds, %d naming the player; "
								 "a Boss tick paid %.2f seconds ago"),
							TicksAnnounced, TicksOfThePlayers, Scene.System->SecondsSinceBossTickPaid()));
	TestEqual(TEXT("at 1.05 seconds: the Boss's tick paid 5 and the Common's nothing"), FervourHeld(Scene.System), 5.0f,
			  0.001f);
	RunClock(World, 1.0f);
	TestEqual(TEXT("at 2.05 seconds: still 5, the second tick inside the 2 seconds"), FervourHeld(Scene.System), 5.0f,
			  0.001f);
	RunClock(World, 1.0f);
	TestEqual(TEXT("at 3.05 seconds: 10"), FervourHeld(Scene.System), 10.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossFervourInchesTest,
	"Cataclysm.BossFervour.DeathByInchesCountsEveryTenthHitOnOneBossAsAKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Death by Inches at 10, 5 Fervour a kill, and Chain Execution's row on `kill` to show the kill's rows act. Nine hits on
 * one Boss credit nothing; one on a second Boss credits nothing, its count its own; the tenth on the first credits a
 * kill: 5 held, the stamp set, the kill's row acted, and the Boss alive. The twentieth credits another. THE CONTROL: ten
 * hits on a Common creature credit nothing.
 */
bool FCataclysmBossFervourInchesTest::RunTest(const FString&)
{
	using namespace CataclysmBossFervourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = BossFighterAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Boss = RungedCreature(World, FVector(5 * BossMetre, 0, 0), 100000.0f, BossRung);
	ACataclysmEnemyCharacter* Other = RungedCreature(World, FVector(-5 * BossMetre, 0, 0), 100000.0f, BossRung);
	ACataclysmEnemyCharacter* Common = RungedCreature(World, FVector(0, 5 * BossMetre, 0), 1000.0f, 0);
	if (!TestNotNull(TEXT("set-up: a fighter"), System) || !Boss || !Other || !Common)
	{
		AddError(TEXT("set-up: three creatures"));
		return false;
	}
	TMap<FName, FCataclysmStatInputs> Lines;
	AddBossLines(Lines, UCataclysmFervour::BossHitsCountedAsAKillEveryStat, 10.0f);
	System->SetStatInputs(MoveTemp(Lines));
	FCataclysmPoolAction Chain;
	Chain.Event = FName(TEXT("kill"));
	Chain.Percent = 2.0f;
	Chain.bNextHitCritical = true;
	Chain.TriggerKey = FName(TEXT("Test:next_hit_critical:kill"));
	System->SetPoolActions({Chain});

	int32 Credited = 0;
	for (int32 Hit = 0; Hit < 10; ++Hit)
	{
		Credited += UCataclysmFervour::CountAHitOnABoss(System, Common) ? 1 : 0;
	}
	TestEqual(TEXT("control: ten hits on a Common creature credit nothing"), Credited, 0);
	for (int32 Hit = 0; Hit < 9; ++Hit)
	{
		Credited += UCataclysmFervour::CountAHitOnABoss(System, Boss) ? 1 : 0;
	}
	TestEqual(TEXT("nine hits on a Boss credit nothing"), Credited, 0);
	TestFalse(TEXT("one on a second Boss credits nothing: its count is its own"),
			  UCataclysmFervour::CountAHitOnABoss(System, Other));
	TestEqual(TEXT("nothing held yet"), FervourHeld(System), 0.0f, 0.001f);
	TestFalse(TEXT("and the kill's row has not acted"), System->HoldsNextHitCritical());

	TestTrue(TEXT("the tenth on the first Boss credits a kill"), UCataclysmFervour::CountAHitOnABoss(System, Boss));
	TestEqual(TEXT("5 held"), FervourHeld(System), 5.0f, 0.001f);
	TestTrue(TEXT("the stamp is set"), System->SecondsSinceOwnKill() >= 0.0f);
	TestTrue(TEXT("the kill's row acted"), System->HoldsNextHitCritical());
	TestFalse(TEXT("and the Boss is alive"), UCataclysmSkillEffects::IsDead(Boss));

	for (int32 Hit = 0; Hit < 9; ++Hit)
	{
		Credited += UCataclysmFervour::CountAHitOnABoss(System, Boss) ? 1 : 0;
	}
	TestEqual(TEXT("the eleventh to the nineteenth credit nothing"), Credited, 0);
	TestTrue(TEXT("the twentieth credits another"), UCataclysmFervour::CountAHitOnABoss(System, Boss));
	TestEqual(TEXT("10 held"), FervourHeld(System), 10.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossFervourInchesHandlerTest,
	"Cataclysm.BossFervour.DeathByInchesCountsThroughTheRealHitHandler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole path: a real player holding Death by Inches lands nine blows on a Boss with health to spare and holds
 * nothing; the tenth credits a kill, 5 held, and the Boss lives.
 */
bool FCataclysmBossFervourInchesHandlerTest::RunTest(const FString&)
{
	using namespace CataclysmBossFervourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	FBossScene Scene;
	if (!BuildBossScene(*this, World, Scene))
	{
		return false;
	}
	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.Add(FName(TEXT("attack_damage"))).Base = 100.0f;
	AddBossLines(Lines, UCataclysmFervour::BossHitsCountedAsAKillEveryStat, 10.0f);
	Scene.System->SetStatInputs(MoveTemp(Lines));
	ACataclysmEnemyCharacter* Boss = RungedCreature(World, FVector(3 * BossMetre, 0, 0), 1000000.0f, BossRung);
	if (!TestNotNull(TEXT("set-up: a Boss"), Boss))
	{
		return false;
	}

	for (int32 Blow = 0; Blow < 9; ++Blow)
	{
		BossBlow(Scene, Boss);
	}
	TestEqual(TEXT("after nine blows nothing is held"), FervourHeld(Scene.System), 0.0f, 0.001f);
	BossBlow(Scene, Boss);
	TestEqual(TEXT("the tenth credits a kill: 5 held"), FervourHeld(Scene.System), 5.0f, 0.001f);
	TestFalse(TEXT("and the Boss lives"), UCataclysmSkillEffects::IsDead(Boss));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossFervourRealRowsTest,
	"Cataclysm.BossFervour.TheTwoNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The two nodes' real rows, read from the passive effects table: 2 seconds, and every 10th hit, each against a Boss. */
bool FCataclysmBossFervourRealRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}
	// A STRUCT AND NOT A NESTED TPair: window death3-w1 refused the nested brace lists (C2440).
	struct FExpectedRow
	{
		const TCHAR* Key;
		const TCHAR* Stat;
		float Value;
	};
	const FExpectedRow Expected[] = {
		{TEXT("Shadow_keystone_a_kC"), UCataclysmFervour::BossTicksPayAKillEverySecondsStat, 2.0f},
		{TEXT("Shadow_keystone_b_kC"), UCataclysmFervour::BossHitsCountedAsAKillEveryStat, 10.0f}};
	for (const auto& Each : Expected)
	{
		const TArray<const FCataclysmPassiveEffectRow*> Rows =
			UCataclysmPassiveTree::EffectsFor(Effects, FName(Each.Key));
		if (!TestEqual(*FString::Printf(TEXT("%s: one row"), Each.Key), Rows.Num(), 1))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s: the stat"), Each.Key), Rows[0]->Stat, FString(Each.Stat));
		TestEqual(*FString::Printf(TEXT("%s: the value"), Each.Key), Rows[0]->ValuePerPoint, Each.Value, 0.001f);
		TestEqual(*FString::Printf(TEXT("%s: against a Boss"), Each.Key), Rows[0]->Condition,
				  FString(TEXT("target_is_boss")));
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
