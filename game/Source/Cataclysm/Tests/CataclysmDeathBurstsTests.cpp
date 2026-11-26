// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmMinion.h"
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
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * Explosions at a kill (the Death trees' slice C4c1, 2026-11-25): the Soul Collector's Passed On, the existing overkill
 * explosion at half for a kill by a hit, reaching the largest radius any source gives; and The Ferryman, a burst of 10%
 * of the slain creature's maximum health while the bar is full, whose kills burst too. Three tests drive the event loop
 * with rows given by hand; two go through a real player's kill handler, on the scene `Cataclysm.Swarm.` builds; one
 * reads the real rows.
 */

namespace CataclysmDeathBurstsTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	constexpr float BurstMetre = 100.0f;

	/** An actor with health and a Fervour pool of 100, able to be given action rows. */
	struct FBurstHolder
	{
		explicit FBurstHolder(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource = NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewResource);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												   100.0f);
		}

		~FBurstHolder()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void SetFervour(float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
												   Value);
		}

		/** Raise `kill` with the rows given, and answer what they recorded. */
		void KillAndTake(float& Share, float& RadiusCm, float& Burst) const
		{
			AbilitySystem->ActOnEvent(FName(TEXT("kill")), nullptr, 0.0f, /*bLanded=*/true, nullptr);
			Share = AbilitySystem->TakePendingOverkillExplosionSharePercent();
			RadiusCm = AbilitySystem->TakePendingOverkillExplosionRadiusCm();
			Burst = AbilitySystem->TakePendingSlainBurstSharePercent();
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	/** An overkill explosion row on `kill` under this trigger key, at this share. */
	FCataclysmPoolAction OverkillRow(const TCHAR* TriggerKey, float Share,
									 ECataclysmStatCondition Condition = ECataclysmStatCondition::Always)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("kill"));
		Action.Percent = Share;
		Action.bExplodeVictimForOverkill = true;
		Action.TriggerKey = FName(TriggerKey);
		Action.Condition = Condition;
		return Action;
	}

	/** The Ferryman's burst row on `kill`, at this share, under this condition. */
	FCataclysmPoolAction BurstRow(float Share, ECataclysmStatCondition Condition)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("kill"));
		Action.Percent = Share;
		Action.bBurstSlainForMaximumHealth = true;
		Action.TriggerKey = FName(TEXT("Passive_SoulCollector_capstone_200_Option2:burst:kill"));
		Action.Condition = Condition;
		return Action;
	}

	/** One flat row of a stat line. */
	FCataclysmStatModifier BurstFlat(float Value)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::Flat;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		return Row;
	}

	static const TCHAR* const PassedOnKey = TEXT("Passive_SoulCollector_keystone_b_kA:explode_victim_for_overkill:kill");
	static const TCHAR* const ItemKey = TEXT("Positive_Enemies_killed_by_you_explode_for_the_overkill_a:explode_victim_for_overkill:kill");

	/** A real player on a real player state, as `Cataclysm.Swarm.` builds one. */
	struct FBurstScene
	{
		ACataclysmPlayerCharacter* Player = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	bool BuildBurstScene(FAutomationTestBase& Test, UWorld* World, FBurstScene& Out)
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
		return true;
	}

	/** The scene's stat lines: the damage a blow needs, and Passed On's radius when `RadiusMetres` is above nought. */
	void WearBurstLines(const FBurstScene& Scene, float RadiusMetres)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		Lines.Add(FName(TEXT("attack_damage"))).Base = 100.0f;
		Lines.Add(FName(TEXT("spell_damage"))).Base = 100.0f;
		if (RadiusMetres > 0.0f)
		{
			Lines.Add(FName(UCataclysmAbilitySystemComponent::OverkillExplosionRadiusMetresStat)).Modifiers = {
				BurstFlat(RadiusMetres)};
		}
		Scene.System->SetStatInputs(MoveTemp(Lines));
	}

	/** A creature of the monsters' side with this maximum health and this much left. */
	ACataclysmEnemyCharacter* BurstCreature(UWorld* World, const FVector& At, float Maximum, float Left)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(Maximum);
			if (UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Made))
			{
				Theirs->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Left);
			}
		}
		return Made;
	}

	/** One blow of the player's at 100%, never a critical strike, in the name of a test strike. */
	void BurstBlow(const FBurstScene& Scene, AActor* Target)
	{
		UCataclysmStrikeSkill* Swing = NewObject<UCataclysmStrikeSkill>(Scene.Player);
		Swing->SkillName = TEXT("Test Swing");
		Swing->SkillTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Strike"))));
		FCataclysmHitDelivery Delivery;
		Delivery.CritChancePercent = 0.0f;
		Delivery.Skill = Swing;
		UCataclysmSkillEffects::ApplyHit(Scene.Player, Target, /*DamagePercent=*/100.0f, Swing->SkillTags, Delivery);
	}

	bool IsBurstDead(const AActor* Who)
	{
		return !IsValid(Who) || UCataclysmSkillEffects::IsDead(Who);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathBurstsRadiusTest,
	"Cataclysm.DeathBursts.PassedOnsRadiusIsTheLargestAnySourceGives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * As the coordinating session ruled: the overkill explosion reaches the largest radius any row that asked gives. Passed
 * On's row alone, with its 3 metres: share 50 at 300 cm. An item's row alone: share 100 at the old 500 cm. Both: the
 * larger share, 100, at 500 cm, so holding Passed On never shrinks an item's explosion. THE CONTROL: Passed On's row with
 * no radius stat reaches the old 500 cm.
 */
bool FCataclysmDeathBurstsRadiusTest::RunTest(const FString&)
{
	using namespace CataclysmDeathBurstsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FBurstHolder Holder(World);
	UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;
	float Share = 0.0f;
	float RadiusCm = 0.0f;
	float Burst = 0.0f;

	System->SetPoolActions({OverkillRow(PassedOnKey, 50.0f)});
	Holder.KillAndTake(Share, RadiusCm, Burst);
	TestEqual(TEXT("control: Passed On's row with no radius stat: the old 500 cm"), RadiusCm, 500.0f, 0.001f);

	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.Add(FName(UCataclysmAbilitySystemComponent::OverkillExplosionRadiusMetresStat)).Modifiers = {BurstFlat(3.0f)};
	System->SetStatInputs(MoveTemp(Lines));
	Holder.KillAndTake(Share, RadiusCm, Burst);
	TestEqual(TEXT("Passed On alone: share 50"), Share, 50.0f, 0.001f);
	TestEqual(TEXT("Passed On alone: 300 cm"), RadiusCm, 300.0f, 0.001f);

	System->SetPoolActions({OverkillRow(ItemKey, 100.0f)});
	Holder.KillAndTake(Share, RadiusCm, Burst);
	TestEqual(TEXT("the item alone: share 100"), Share, 100.0f, 0.001f);
	TestEqual(TEXT("the item alone, the radius stat held: still 500 cm"), RadiusCm, 500.0f, 0.001f);

	System->SetPoolActions({OverkillRow(PassedOnKey, 50.0f), OverkillRow(ItemKey, 100.0f)});
	Holder.KillAndTake(Share, RadiusCm, Burst);
	TestEqual(TEXT("both: the larger share, 100"), Share, 100.0f, 0.001f);
	TestEqual(TEXT("both: the larger radius, 500 cm, never shrunk by Passed On"), RadiusCm, 500.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathBurstsHitTest,
	"Cataclysm.DeathBursts.PassedOnActsOnlyOnAKillByAHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Passed On's row under `killing_blow_was_a_hit`, as ruled ("your hit" leaves out a tick): marked as a kill by a hit,
 * it records share 50. THE CONTROL: the same kill marked as by a tick records nothing, and nor does a kill raised with no
 * mark at all.
 */
bool FCataclysmDeathBurstsHitTest::RunTest(const FString&)
{
	using namespace CataclysmDeathBurstsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FBurstHolder Holder(World);
	UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;
	System->SetPoolActions({OverkillRow(PassedOnKey, 50.0f, ECataclysmStatCondition::KillingBlowWasAHit)});
	float Share = 0.0f;
	float RadiusCm = 0.0f;
	float Burst = 0.0f;

	System->SetActingOnAKillByAHit(true);
	Holder.KillAndTake(Share, RadiusCm, Burst);
	System->SetActingOnAKillByAHit(false);
	TestEqual(TEXT("a kill by a hit: share 50"), Share, 50.0f, 0.001f);

	Holder.KillAndTake(Share, RadiusCm, Burst);
	TestEqual(TEXT("control: a kill by a tick, or no mark: nothing"), Share, 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathBurstsFullBarTest,
	"Cataclysm.DeathBursts.TheFerrymanBurstsOnlyWhileTheBarIsFull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Ferryman's row under `class_resource_at_maximum`: with the bar at 100 of 100 a kill records a burst of 10. THE
 * CONTROL: at 99 it records nothing.
 */
bool FCataclysmDeathBurstsFullBarTest::RunTest(const FString&)
{
	using namespace CataclysmDeathBurstsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FBurstHolder Holder(World);
	Holder.AbilitySystem->SetPoolActions({BurstRow(10.0f, ECataclysmStatCondition::ClassResourceAtMaximum)});
	float Share = 0.0f;
	float RadiusCm = 0.0f;
	float Burst = 0.0f;

	Holder.SetFervour(100.0f);
	Holder.KillAndTake(Share, RadiusCm, Burst);
	TestEqual(TEXT("a full bar: a burst of 10"), Burst, 10.0f, 0.001f);

	Holder.SetFervour(99.0f);
	Holder.KillAndTake(Share, RadiusCm, Burst);
	TestEqual(TEXT("control: at 99, nothing"), Burst, 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathBurstsFerrymanChainTest,
	"Cataclysm.DeathBursts.TheFerrymansBurstChainsThroughTheRealKillHandler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole path: a real player with a full bar and The Ferryman's row kills a creature of 1,000 maximum health. Its
 * burst, 100, kills a creature 3 metres off (600 maximum, 20 left); that one's own burst, 60, kills a third 3 metres
 * beyond it and 6 from the first (10 left), so "a creature the burst kills bursts too". THE CONTROL: a fourth creature
 * 4.5 metres from the first and more than 4 from the others (20 left) is untouched, so the burst reaches 4 metres and no
 * more.
 */
bool FCataclysmDeathBurstsFerrymanChainTest::RunTest(const FString&)
{
	using namespace CataclysmDeathBurstsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	FBurstScene Scene;
	if (!BuildBurstScene(*this, World, Scene))
	{
		return false;
	}
	WearBurstLines(Scene, /*RadiusMetres=*/0.0f);
	Scene.System->SetPoolActions({BurstRow(10.0f, ECataclysmStatCondition::ClassResourceAtMaximum)});
	Scene.System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 100.0f);

	const float M = BurstMetre;
	ACataclysmEnemyCharacter* First = BurstCreature(World, FVector(6 * M, 0, 0), 1000.0f, 1.0f);
	ACataclysmEnemyCharacter* Second = BurstCreature(World, FVector(6 * M, 3 * M, 0), 600.0f, 20.0f);
	ACataclysmEnemyCharacter* Third = BurstCreature(World, FVector(6 * M, 6 * M, 0), 600.0f, 10.0f);
	ACataclysmEnemyCharacter* Beyond = BurstCreature(World, FVector(10.5f * M, 0, 0), 600.0f, 20.0f);
	if (!First || !Second || !Third || !Beyond)
	{
		AddError(TEXT("set-up: four creatures"));
		return false;
	}

	BurstBlow(Scene, First);
	if (!TestTrue(TEXT("set-up: the player's blow killed the first"), IsBurstDead(First)))
	{
		return false;
	}
	TestTrue(TEXT("its burst killed the second, 3 metres off"), IsBurstDead(Second));
	TestTrue(TEXT("whose own burst killed the third, 6 metres from the first"), IsBurstDead(Third));
	TestFalse(TEXT("control: the fourth, 4.5 metres from the first, is untouched"), IsBurstDead(Beyond));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathBurstsFollowerTest,
	"Cataclysm.DeathBursts.TheFerrymanBurstsForAFollowersKillUnderDeathsToll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Death's Toll makes a follower's kill the character's for everything the Soul Collector tree asks, The Ferryman among
 * it. A real player with a full bar, The Ferryman's row and Death's Toll has its real imp kill a creature of 1,000
 * maximum health: the burst, 100, kills a creature 3 metres off (20 left). THE CONTROL: the same kill without Death's
 * Toll bursts nothing, and a creature 3 metres from that one is untouched.
 */
bool FCataclysmDeathBurstsFollowerTest::RunTest(const FString&)
{
	using namespace CataclysmDeathBurstsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	FBurstScene Scene;
	if (!BuildBurstScene(*this, World, Scene))
	{
		return false;
	}
	Scene.System->SetPoolActions({BurstRow(10.0f, ECataclysmStatCondition::ClassResourceAtMaximum)});
	Scene.System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 100.0f);
	const auto Wear = [&Scene](bool bDeathsToll)
	{
		WearBurstLines(Scene, /*RadiusMetres=*/0.0f);
		if (bDeathsToll)
		{
			TMap<FName, FCataclysmStatInputs> Lines;
			Lines.Add(FName(TEXT("attack_damage"))).Base = 100.0f;
			Lines.Add(FName(TEXT("spell_damage"))).Base = 100.0f;
			Lines.Add(FName(UCataclysmFervour::FollowerKillsCountAsYoursStat)).Modifiers = {BurstFlat(1.0f)};
			Scene.System->SetStatInputs(MoveTemp(Lines));
		}
	};

	const float M = BurstMetre;
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Scene.Player, FVector(2 * M, 2 * M, 0), /*Lifetime=*/60.0f,
													/*bBurns=*/false, TEXT("Imp"));
	ACataclysmEnemyCharacter* ControlSlain = BurstCreature(World, FVector(2 * M, -2 * M, 0), 1000.0f, 1.0f);
	ACataclysmEnemyCharacter* ControlNear = BurstCreature(World, FVector(2 * M, -5 * M, 0), 600.0f, 20.0f);
	ACataclysmEnemyCharacter* Slain = BurstCreature(World, FVector(6 * M, 2 * M, 0), 1000.0f, 1.0f);
	ACataclysmEnemyCharacter* Near = BurstCreature(World, FVector(6 * M, 5 * M, 0), 600.0f, 20.0f);
	if (!TestNotNull(TEXT("set-up: the player's imp"), Imp) || !ControlSlain || !ControlNear || !Slain || !Near)
	{
		AddError(TEXT("set-up: four creatures"));
		return false;
	}

	Wear(/*bDeathsToll=*/false);
	Imp->AttackTarget(ControlSlain);
	if (!TestTrue(TEXT("set-up: the imp killed the first creature"), IsBurstDead(ControlSlain)))
	{
		return false;
	}
	TestFalse(TEXT("control: without Death's Toll the imp's kill bursts nothing"), IsBurstDead(ControlNear));

	Wear(/*bDeathsToll=*/true);
	Imp->AttackTarget(Slain);
	if (!TestTrue(TEXT("set-up: the imp killed the second creature"), IsBurstDead(Slain)))
	{
		return false;
	}
	TestTrue(TEXT("with Death's Toll the imp's kill bursts, killing the creature 3 metres off"), IsBurstDead(Near));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathBurstsPassedOnTest,
	"Cataclysm.DeathBursts.PassedOnExplodesWithinThreeMetresThroughTheRealKillHandler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole path: a real player with Passed On's row and its 3 metres kills a creature with 1 health left by a hit, so
 * the hit's overkill is nearly all of it. Half of that kills a creature 2 metres off (5 left). THE CONTROL: a creature 4
 * metres off (5 left), inside the old 5 metres and outside Passed On's 3, is untouched.
 */
bool FCataclysmDeathBurstsPassedOnTest::RunTest(const FString&)
{
	using namespace CataclysmDeathBurstsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	FBurstScene Scene;
	if (!BuildBurstScene(*this, World, Scene))
	{
		return false;
	}
	WearBurstLines(Scene, /*RadiusMetres=*/3.0f);
	Scene.System->SetPoolActions({OverkillRow(PassedOnKey, 50.0f, ECataclysmStatCondition::KillingBlowWasAHit)});

	const float M = BurstMetre;
	ACataclysmEnemyCharacter* Slain = BurstCreature(World, FVector(6 * M, 0, 0), 1000.0f, 1.0f);
	ACataclysmEnemyCharacter* Near = BurstCreature(World, FVector(8 * M, 0, 0), 600.0f, 5.0f);
	ACataclysmEnemyCharacter* Outside = BurstCreature(World, FVector(2 * M, 0, 0), 600.0f, 5.0f);
	if (!Slain || !Near || !Outside)
	{
		AddError(TEXT("set-up: three creatures"));
		return false;
	}

	BurstBlow(Scene, Slain);
	if (!TestTrue(TEXT("set-up: the player's blow killed the first"), IsBurstDead(Slain)))
	{
		return false;
	}
	TestTrue(TEXT("half its overkill killed the creature 2 metres off"), IsBurstDead(Near));
	TestFalse(TEXT("control: the creature 4 metres off, outside Passed On's 3, is untouched"), IsBurstDead(Outside));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathBurstsRealRowsTest,
	"Cataclysm.DeathBursts.TheTwoNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The two nodes' real rows, read from the passive effects table: Passed On's explosion at 50 on a kill by a hit, and its
 * 3 metres; The Ferryman's burst of 10 on a kill while the bar is full, as the 200 vow's second option.
 */
bool FCataclysmDeathBurstsRealRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}

	int32 Explosions = 0;
	int32 Radii = 0;
	for (const FCataclysmPassiveEffectRow* Row :
		 UCataclysmPassiveTree::EffectsFor(Effects, FName(TEXT("SoulCollector_keystone_b_kA"))))
	{
		if (Row->Action == TEXT("explode_victim_for_overkill"))
		{
			++Explosions;
			TestEqual(TEXT("Passed On: half"), Row->ValuePerPoint, 50.0f, 0.001f);
			TestEqual(TEXT("Passed On: on a kill"), Row->ActionEvent, FString(TEXT("kill")));
			TestEqual(TEXT("Passed On: by a hit"), Row->Condition, FString(TEXT("killing_blow_was_a_hit")));
		}
		else if (Row->Stat == UCataclysmAbilitySystemComponent::OverkillExplosionRadiusMetresStat)
		{
			++Radii;
			TestEqual(TEXT("Passed On: 3 metres"), Row->ValuePerPoint, 3.0f, 0.001f);
		}
	}
	TestEqual(TEXT("Passed On: one explosion row"), Explosions, 1);
	TestEqual(TEXT("Passed On: one radius row"), Radii, 1);

	int32 Bursts = 0;
	for (const FCataclysmPassiveEffectRow* Row :
		 UCataclysmPassiveTree::EffectsFor(Effects, FName(TEXT("SoulCollector_capstone_200"))))
	{
		if (Row->Option == 2)
		{
			++Bursts;
			TestEqual(TEXT("The Ferryman: the burst"), Row->Action, FString(TEXT("burst_slain_for_share_of_maximum_health")));
			TestEqual(TEXT("The Ferryman: 10%"), Row->ValuePerPoint, 10.0f, 0.001f);
			TestEqual(TEXT("The Ferryman: while the bar is full"), Row->Condition,
					  FString(TEXT("class_resource_at_maximum")));
		}
	}
	TestEqual(TEXT("The Ferryman: one row"), Bursts, 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
