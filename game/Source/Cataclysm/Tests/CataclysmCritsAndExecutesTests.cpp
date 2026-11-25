// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
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
 * Critical strikes, executes and kills that speed the next one (the Death trees' slice C4d1, 2026-11-25): the Shadow's
 * "2 for each critical strike you land" and Telling Blows; the Soul Collector's Reaper's Edge; the Shadow's Relentless
 * and Chain Execution. Three tests give rows by hand; three go through a real player's hit and kill handlers, on the
 * scene `Cataclysm.Swarm.` builds; one reads the real rows.
 */

namespace CataclysmCritsAndExecutesTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;
	using CataclysmTestWorld::RunClock;

	constexpr float CritMetre = 100.0f;

	/** A character on the players' side with health and a Fervour pool of `Maximum`, empty. */
	UCataclysmAbilitySystemComponent* PoolHolderAt(UWorld* World, const FVector& Where, float Maximum = 100.0f)
	{
		ACataclysmEnemyCharacter* Holder = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (!Holder)
		{
			return nullptr;
		}
		Holder->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		Holder->SetAttackDamage(0.0f);
		Holder->SetHealth(1000.0f);
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent());
		if (!System)
		{
			return nullptr;
		}
		System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Holder));
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), Maximum);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);
		return System;
	}

	/** Set what a creature has left. */
	void HoldAt(AActor* Creature, float Left)
	{
		if (UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Creature))
		{
			Theirs->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Left);
		}
	}

	/** A creature of the monsters' side with this maximum health, this much left, and this rarity rung. */
	ACataclysmEnemyCharacter* CritCreature(UWorld* World, const FVector& At, float Maximum, float Left, int32 Rung = 0)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(Maximum);
			// THE RUNG ALONE, as `WithTargetState` reads it; `SetRarityStep` would also scale and roll modifiers.
			Made->RarityStep = Rung;
			HoldAt(Made, Left);
		}
		return Made;
	}

	float HealthOf(const AActor* Creature)
	{
		const UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Creature);
		return Theirs ? Theirs->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** One flat row of a stat line, under a condition when one is given. */
	FCataclysmStatModifier CritRow(ECataclysmStatBucket Bucket, float Value,
								   ECataclysmStatCondition Condition = ECataclysmStatCondition::Always,
								   float ConditionValue = 0.0f)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = Bucket;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		Row.Condition = Condition;
		Row.ConditionValue = ConditionValue;
		return Row;
	}

	/** The Shadow's "2 for each critical strike" and Telling Blows' 1 more on an Elite or stronger, as stat lines. */
	TArray<FCataclysmStatModifier> CriticalStrikeRows()
	{
		return {CritRow(ECataclysmStatBucket::Flat, 2.0f),
				CritRow(ECataclysmStatBucket::Flat, 1.0f, ECataclysmStatCondition::TargetRarityAtLeast, 1.0f)};
	}

	float FervourOf(const UAbilitySystemComponent* System)
	{
		return System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
	}

	void EmptyThePool(UAbilitySystemComponent* System)
	{
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);
	}

	/** A real player on a real player state, as `Cataclysm.Swarm.` builds one, with a Fervour pool of 100. */
	struct FCritScene
	{
		ACataclysmPlayerCharacter* Player = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	bool BuildCritScene(FAutomationTestBase& Test, UWorld* World, FCritScene& Out)
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
		EmptyThePool(Out.System);
		return true;
	}

	/** The scene's stat lines: the damage a blow needs, and whatever else is given. */
	void WearCritLines(const FCritScene& Scene, TMap<FName, FCataclysmStatInputs> Lines = {})
	{
		Lines.FindOrAdd(FName(TEXT("attack_damage"))).Base = 100.0f;
		Lines.FindOrAdd(FName(TEXT("spell_damage"))).Base = 100.0f;
		Scene.System->SetStatInputs(MoveTemp(Lines));
	}

	/** One blow of the player's at 100%, in the name of a test strike, with the skill's stated critical strike chance. */
	void CritBlow(const FCritScene& Scene, AActor* Target, float CritChancePercent)
	{
		UCataclysmStrikeSkill* Swing = NewObject<UCataclysmStrikeSkill>(Scene.Player);
		Swing->SkillName = TEXT("Test Swing");
		Swing->SkillTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Strike"))));
		FCataclysmHitDelivery Delivery;
		Delivery.CritChancePercent = CritChancePercent;
		Delivery.Skill = Swing;
		UCataclysmSkillEffects::ApplyHit(Scene.Player, Target, /*DamagePercent=*/100.0f, Swing->SkillTags, Delivery);
	}

	bool IsCritDead(const AActor* Who)
	{
		return !IsValid(Who) || UCataclysmSkillEffects::IsDead(Who);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCritsTellingBlowsTest,
	"Cataclysm.CritsAndExecutes.ACriticalStrikePaysTwoAndTellingBlowsOneMoreOnAnEliteOrStronger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Shadow's "2 for each critical strike you land" and Telling Blows' "+1 Fervour per point for each critical strike
 * you land on an Elite or stronger enemy", one point: a critical strike on an Elite pays 3, on a Common 2. THE CONTROL:
 * a character without the stat is paid nothing.
 */
bool FCataclysmCritsTellingBlowsTest::RunTest(const FString&)
{
	using namespace CataclysmCritsAndExecutesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = PoolHolderAt(World, FVector::ZeroVector);
	const float M = CritMetre;
	ACataclysmEnemyCharacter* Common = CritCreature(World, FVector(5 * M, 0, 0), 1000.0f, 1000.0f, 0);
	ACataclysmEnemyCharacter* Elite = CritCreature(World, FVector(-5 * M, 0, 0), 1000.0f, 1000.0f, 1);
	if (!TestNotNull(TEXT("set-up: a holder"), System) || !Common || !Elite)
	{
		AddError(TEXT("set-up: two creatures"));
		return false;
	}

	TestEqual(TEXT("control: without the stat a critical strike pays nothing"),
			  UCataclysmFervour::GainOnCriticalStrike(System, Elite), 0.0f, 0.001f);
	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.Add(FName(UCataclysmFervour::PerCriticalStrikeStat)).Modifiers = CriticalStrikeRows();
	System->SetStatInputs(MoveTemp(Lines));
	TestEqual(TEXT("on an Elite: 3"), UCataclysmFervour::GainOnCriticalStrike(System, Elite), 3.0f, 0.001f);
	EmptyThePool(System);
	TestEqual(TEXT("on a Common: 2"), UCataclysmFervour::GainOnCriticalStrike(System, Common), 2.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCritsHandlerTest,
	"Cataclysm.CritsAndExecutes.ThePlayersCriticalStrikePaysFervourThroughTheRealHitHandler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole path: a real player holding the two rows lands a critical strike on an Elite with health to spare, and its
 * bar goes from 0 to 3. THE CONTROL: the same blow that is not a critical strike pays nothing.
 */
bool FCataclysmCritsHandlerTest::RunTest(const FString&)
{
	using namespace CataclysmCritsAndExecutesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll RollsZero(0.0f);
	FCritScene Scene;
	if (!BuildCritScene(*this, World, Scene))
	{
		return false;
	}
	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.Add(FName(UCataclysmFervour::PerCriticalStrikeStat)).Modifiers = CriticalStrikeRows();
	WearCritLines(Scene, MoveTemp(Lines));
	ACataclysmEnemyCharacter* Elite = CritCreature(World, FVector(3 * CritMetre, 0, 0), 100000.0f, 100000.0f, 1);
	if (!TestNotNull(TEXT("set-up: an Elite"), Elite))
	{
		return false;
	}

	CritBlow(Scene, Elite, /*CritChancePercent=*/0.0f);
	TestEqual(TEXT("control: a blow that is not a critical strike pays nothing"), FervourOf(Scene.System), 0.0f,
			  0.001f);
	CritBlow(Scene, Elite, /*CritChancePercent=*/100.0f);
	TestEqual(TEXT("a critical strike on an Elite pays 3"), FervourOf(Scene.System), 3.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCritsReapersEdgeTest,
	"Cataclysm.CritsAndExecutes.ReapersEdgeDoublesTheFervourOfAnExecutesKillThroughTheRealHandlers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole path, measured as `Cataclysm.Skills.` measures an execute: what one blow takes is read off a creature with
 * health to spare, and each creature is held relative to it. A real player holding Reaper's Edge's two rows (the execute
 * below 15%, and 100% more Fervour per kill on an execute's kill) and 10 Fervour a kill: a blow that leaves a creature
 * below 15% executes it, and the kill pays 20. THE CONTROL: a creature the blow kills by itself pays 10.
 */
bool FCataclysmCritsReapersEdgeTest::RunTest(const FString&)
{
	using namespace CataclysmCritsAndExecutesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	FCritScene Scene;
	if (!BuildCritScene(*this, World, Scene))
	{
		return false;
	}
	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.Add(FName(UCataclysmFervour::PerKillStat)).Modifiers = {
		CritRow(ECataclysmStatBucket::Flat, 10.0f),
		CritRow(ECataclysmStatBucket::More, 100.0f, ECataclysmStatCondition::KillWasAnExecute)};
	WearCritLines(Scene, MoveTemp(Lines));
	FCataclysmPoolAction Execute;
	Execute.Event = FName(TEXT("hit_dealt"));
	Execute.Pool = FName(UCataclysmAbilitySystemComponent::ExecuteTargetAction);
	Execute.Percent = 100.0f;
	Execute.bExecuteTarget = true;
	Execute.Condition = ECataclysmStatCondition::TargetHealthBelowPercent;
	Execute.ConditionValue = 15.0f;
	Execute.TriggerKey = FName(TEXT("Passive_SoulCollector_capstone_100_Option2:execute_target:hit_dealt"));
	Scene.System->SetPoolActions({Execute});

	const float M = CritMetre;
	ACataclysmEnemyCharacter* Measure = CritCreature(World, FVector(3 * M, 0, 0), 1000.0f, 1000.0f);
	ACataclysmEnemyCharacter* LeftBelow = CritCreature(World, FVector(3 * M, 3 * M, 0), 1000.0f, 1000.0f);
	ACataclysmEnemyCharacter* Frail = CritCreature(World, FVector(3 * M, -3 * M, 0), 1000.0f, 1000.0f);
	if (!Measure || !LeftBelow || !Frail)
	{
		AddError(TEXT("set-up: three creatures"));
		return false;
	}

	CritBlow(Scene, Measure, 0.0f);
	const float Taken = 1000.0f - HealthOf(Measure);
	if (!TestTrue(*FString::Printf(TEXT("set-up: one blow takes something and leaves the measure above 15%%: %.2f"),
								   Taken),
				  Taken > 1.0f && Taken < 300.0f))
	{
		return false;
	}

	EmptyThePool(Scene.System);
	HoldAt(Frail, Taken / 2.0f);
	CritBlow(Scene, Frail, 0.0f);
	if (!TestTrue(TEXT("set-up: the blow killed the frail creature itself"), IsCritDead(Frail)))
	{
		return false;
	}
	TestEqual(TEXT("control: a kill by the blow pays 10"), FervourOf(Scene.System), 10.0f, 0.001f);

	EmptyThePool(Scene.System);
	HoldAt(LeftBelow, 150.0f + Taken / 2.0f);
	CritBlow(Scene, LeftBelow, 0.0f);
	if (!TestTrue(TEXT("set-up: the creature the blow left below 15% was executed"), IsCritDead(LeftBelow)))
	{
		return false;
	}
	TestEqual(TEXT("an execute's kill pays double: 20"), FervourOf(Scene.System), 20.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCritsRelentlessTest,
	"Cataclysm.CritsAndExecutes.RelentlessIsSizedByTheWholeHundredsOfFervourHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * As ruled, whole hundreds: `TimesSizedByFervourHeld` is 0 at 0 and at 99, 1 at 100 and at 199, 2 at 200. And through
 * the event loop, with a heal of 1% of maximum health on the same base as Relentless's cooldown cut, on `kill`: at 250
 * held the 500 of 1,000 left becomes 520. THE CONTROL: at 99 held the kill heals nothing.
 */
bool FCataclysmCritsRelentlessTest::RunTest(const FString&)
{
	using namespace CataclysmCritsAndExecutesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = PoolHolderAt(World, FVector::ZeroVector, /*Maximum=*/300.0f);
	if (!TestNotNull(TEXT("set-up: a holder"), System))
	{
		return false;
	}
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const TPair<float, float> Expected[] = {{0.0f, 0.0f}, {99.0f, 0.0f}, {100.0f, 1.0f}, {199.0f, 1.0f}, {200.0f, 2.0f}};
	for (const TPair<float, float>& Case : Expected)
	{
		System->SetNumericAttributeBase(Pool, Case.Key);
		TestEqual(*FString::Printf(TEXT("%.0f held: %.0f whole hundreds"), Case.Key, Case.Value),
				  System->TimesSizedByFervourHeld(), Case.Value, 0.001f);
	}

	FCataclysmPoolAction Heal;
	Heal.Event = FName(TEXT("kill"));
	Heal.Pool = FName(TEXT("health"));
	Heal.Percent = 1.0f;
	Heal.Base = ECataclysmPoolActionBase::ForEachHundredClassResourceHeld;
	Heal.TriggerKey = FName(TEXT("A_row:health"));
	System->SetPoolActions({Heal});
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();

	System->SetNumericAttributeBase(Health, 500.0f);
	System->SetNumericAttributeBase(Pool, 99.0f);
	System->ActOnEvent(FName(TEXT("kill")), nullptr, 0.0f, /*bLanded=*/true, nullptr);
	TestEqual(TEXT("control: at 99 held the kill heals nothing"), System->GetNumericAttribute(Health), 500.0f, 0.01f);

	System->SetNumericAttributeBase(Pool, 250.0f);
	System->ActOnEvent(FName(TEXT("kill")), nullptr, 0.0f, /*bLanded=*/true, nullptr);
	TestEqual(TEXT("at 250 held: twice 1% of 1,000, so 520"), System->GetNumericAttribute(Health), 520.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCritsChainTest,
	"Cataclysm.CritsAndExecutes.ChainExecutionMakesTheNextLandedHitCriticalAndOnlyThatOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole path, with every roll pinned to miss a critical strike. A real player holding Chain Execution's row kills a
 * creature with a blow and holds the charge; its next blow takes more than one and a fifth of a plain blow, and the
 * charge is used; the blow after takes a plain blow. THE CONTROL: after another kill, at 2.5 seconds the charge has run
 * out and the blow is plain.
 */
bool FCataclysmCritsChainTest::RunTest(const FString&)
{
	using namespace CataclysmCritsAndExecutesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	FCritScene Scene;
	if (!BuildCritScene(*this, World, Scene))
	{
		return false;
	}
	WearCritLines(Scene);
	FCataclysmPoolAction Chain;
	Chain.Event = FName(TEXT("kill"));
	Chain.Percent = 2.0f;
	Chain.bNextHitCritical = true;
	Chain.TriggerKey = FName(TEXT("Passive_Shadow_capstone_100_Option2:next_hit_critical:kill"));
	Scene.System->SetPoolActions({Chain});

	const float M = CritMetre;
	ACataclysmEnemyCharacter* Big = CritCreature(World, FVector(3 * M, 0, 0), 100000.0f, 100000.0f);
	ACataclysmEnemyCharacter* FrailOne = CritCreature(World, FVector(3 * M, 3 * M, 0), 1000.0f, 1.0f);
	ACataclysmEnemyCharacter* FrailTwo = CritCreature(World, FVector(3 * M, -3 * M, 0), 1000.0f, 1.0f);
	if (!Big || !FrailOne || !FrailTwo)
	{
		AddError(TEXT("set-up: three creatures"));
		return false;
	}
	const auto TakenByABlow = [&Scene, Big]()
	{
		const float Before = HealthOf(Big);
		CritBlow(Scene, Big, 0.0f);
		return Before - HealthOf(Big);
	};

	const float Plain = TakenByABlow();
	if (!TestTrue(TEXT("set-up: a plain blow takes something"), Plain > 1.0f)
		|| !TestFalse(TEXT("set-up: no charge before a kill"), Scene.System->HoldsNextHitCritical()))
	{
		return false;
	}
	CritBlow(Scene, FrailOne, 0.0f);
	if (!TestTrue(TEXT("set-up: the blow killed the frail creature"), IsCritDead(FrailOne))
		|| !TestTrue(TEXT("the kill granted the charge"), Scene.System->HoldsNextHitCritical()))
	{
		return false;
	}
	const float Charged = TakenByABlow();
	TestTrue(*FString::Printf(TEXT("the next blow is a critical strike: %.2f against a plain %.2f"), Charged, Plain),
			 Charged > Plain * 1.2f);
	TestFalse(TEXT("and it used the charge"), Scene.System->HoldsNextHitCritical());
	TestEqual(TEXT("the blow after is plain"), TakenByABlow(), Plain, 0.01f);

	CritBlow(Scene, FrailTwo, 0.0f);
	if (!TestTrue(TEXT("set-up: another kill granted the charge"), Scene.System->HoldsNextHitCritical()))
	{
		return false;
	}
	RunClock(World, 2.5f);
	TestFalse(TEXT("control: 2.5 seconds on, the charge has run out"), Scene.System->HoldsNextHitCritical());
	TestEqual(TEXT("control: and the blow is plain"), TakenByABlow(), Plain, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCritsRealRowsTest,
	"Cataclysm.CritsAndExecutes.TheFiveNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The real rows, read from the passive effects table: the Shadow's starting node's 2 for a critical strike; Telling
 * Blows' 1 per point on an Elite or stronger; Reaper's Edge's execute below 15% on a hit and 100% more Fervour on an
 * execute's kill; Relentless's 1 second off every cooldown at a kill for each whole hundred held; Chain Execution's 2
 * seconds.
 */
bool FCataclysmCritsRealRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}
	const auto RowsOf = [Effects](const TCHAR* Node, int32 Option)
	{
		TArray<const FCataclysmPassiveEffectRow*> Out;
		for (const FCataclysmPassiveEffectRow* Row : UCataclysmPassiveTree::EffectsFor(Effects, FName(Node)))
		{
			if (Row->Option == Option)
			{
				Out.Add(Row);
			}
		}
		return Out;
	};

	int32 Crits = 0;
	for (const FCataclysmPassiveEffectRow* Row : RowsOf(TEXT("Shadow_basic_spine_000"), 0))
	{
		if (Row->Stat == UCataclysmFervour::PerCriticalStrikeStat)
		{
			++Crits;
			TestEqual(TEXT("the Shadow's starting node: 2 a critical strike"), Row->ValuePerPoint, 2.0f, 0.001f);
			TestEqual(TEXT("with no condition"), Row->Condition, FString());
		}
	}
	TestEqual(TEXT("the Shadow's starting node: one critical strike row"), Crits, 1);

	const TArray<const FCataclysmPassiveEffectRow*> Telling = RowsOf(TEXT("Shadow_basic_spine_007"), 0);
	if (TestEqual(TEXT("Telling Blows: one row"), Telling.Num(), 1))
	{
		TestEqual(TEXT("Telling Blows: the stat"), Telling[0]->Stat, FString(UCataclysmFervour::PerCriticalStrikeStat));
		TestEqual(TEXT("Telling Blows: 1 per point"), Telling[0]->ValuePerPoint, 1.0f, 0.001f);
		TestEqual(TEXT("Telling Blows: on an Elite or stronger"), Telling[0]->Condition,
				  FString(TEXT("target_rarity_at_least")));
		TestEqual(TEXT("Telling Blows: rung 1"), Telling[0]->ConditionValue, 1.0f, 0.001f);
	}

	int32 Executes = 0;
	int32 Doubled = 0;
	for (const FCataclysmPassiveEffectRow* Row : RowsOf(TEXT("SoulCollector_capstone_100"), 2))
	{
		if (Row->Action == UCataclysmAbilitySystemComponent::ExecuteTargetAction)
		{
			++Executes;
			TestEqual(TEXT("Reaper's Edge: on a hit"), Row->ActionEvent, FString(TEXT("hit_dealt")));
			TestEqual(TEXT("Reaper's Edge: below 15%"), Row->ConditionValue, 15.0f, 0.001f);
		}
		else if (Row->Stat == UCataclysmFervour::PerKillStat)
		{
			++Doubled;
			TestEqual(TEXT("Reaper's Edge: more"), Row->ValueKind, FString(TEXT("more")));
			TestEqual(TEXT("Reaper's Edge: 100"), Row->ValuePerPoint, 100.0f, 0.001f);
			TestEqual(TEXT("Reaper's Edge: on an execute's kill"), Row->Condition, FString(TEXT("kill_was_an_execute")));
		}
	}
	TestEqual(TEXT("Reaper's Edge: one execute"), Executes, 1);
	TestEqual(TEXT("Reaper's Edge: one doubled payment"), Doubled, 1);

	const TArray<const FCataclysmPassiveEffectRow*> Relentless = RowsOf(TEXT("Shadow_capstone_100"), 3);
	if (TestEqual(TEXT("Relentless: one row"), Relentless.Num(), 1))
	{
		TestEqual(TEXT("Relentless: every cooldown"), Relentless[0]->Action, FString(TEXT("cooldown_reduce_all")));
		TestEqual(TEXT("Relentless: at a kill"), Relentless[0]->ActionEvent, FString(TEXT("kill")));
		TestEqual(TEXT("Relentless: 1 second"), Relentless[0]->ValuePerPoint, 1.0f, 0.001f);
		TestEqual(TEXT("Relentless: for each whole hundred held"), Relentless[0]->FractionOf,
				  FString(TEXT("for_each_hundred_class_resource_held")));
	}

	const TArray<const FCataclysmPassiveEffectRow*> Chain = RowsOf(TEXT("Shadow_capstone_100"), 2);
	if (TestEqual(TEXT("Chain Execution: one row"), Chain.Num(), 1))
	{
		TestEqual(TEXT("Chain Execution: the charge"), Chain[0]->Action, FString(TEXT("next_hit_critical")));
		TestEqual(TEXT("Chain Execution: at a kill"), Chain[0]->ActionEvent, FString(TEXT("kill")));
		TestEqual(TEXT("Chain Execution: 2 seconds"), Chain[0]->ValuePerPoint, 2.0f, 0.001f);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
