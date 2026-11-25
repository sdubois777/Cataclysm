// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmLeech.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * When the Necromancer's minions die, and when he is hurt (the Death trees' slice C6d2, 2026-11-25, as ruled): a
 * follower killed raises `minion_death` on its commander and starts the two clocks on it (Bone Knit, Death Ward);
 * Marrow Feast heals on each death at most ten times a second; Avenging Dead's stack is granted only by a hit that
 * took enough of maximum health; Marshal's damage by the minion's distance; Shared Leech, Eternal Army and Bound Bones.
 */

namespace CataclysmMinionLossTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	constexpr float M = 100.0f;
	constexpr float Pool = 10'000.0f;

	/** A bare summoner at the origin: combat and vital sets, 10,000 health of 10,000. */
	struct FMinionLossSummoner
	{
		explicit FMinionLossSummoner(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// Raw pointers, because AddAttributeSetSubobject deduces its type from the argument.
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Pool);
			SetHealthShare(1.0f);
		}

		void SetHealthShare(float Share) const
		{
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Pool * Share);
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		/** One line per stat, each holding the one row given. */
		void Lines(const TArray<TPair<const TCHAR*, FCataclysmStatModifier>>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Inputs;
			for (const TPair<const TCHAR*, FCataclysmStatModifier>& Row : Rows)
			{
				Inputs.FindOrAdd(FName(Row.Key)).Modifiers.Add(Row.Value);
			}
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	FCataclysmStatModifier Row(ECataclysmStatBucket Bucket, float Value,
							   ECataclysmStatCondition Condition = ECataclysmStatCondition::Always,
							   float ConditionValue = 0.0f, float ReachMetres = -1.0f)
	{
		FCataclysmStatModifier Made;
		Made.Bucket = Bucket;
		Made.Source = ECataclysmModifierSource::PassiveKeystone;
		Made.Value = Value;
		Made.Condition = Condition;
		Made.ConditionValue = ConditionValue;
		Made.ReachMetres = ReachMetres;
		return Made;
	}

	ACataclysmMinion* SpawnImp(AActor* Summoner, const FVector& Where)
	{
		return ACataclysmMinion::Spawn(Summoner, Where, /*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("Imp"));
	}

	/** Write an actor's health at nought, which is where every death begins. */
	void WriteHealthAtNought(AActor* Body)
	{
		if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Body))
		{
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
		}
	}

	/** Counts one action event raised on an ability system while it lives. */
	struct FMinionLossEventCount
	{
		FMinionLossEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
			: System(InSystem)
			, Event(InEvent)
		{
			Handle = System->OnActionEvent.AddLambda([this](FName Raised)
			{
				Count += Raised == Event ? 1 : 0;
			});
		}
		~FMinionLossEventCount()
		{
			System->OnActionEvent.Remove(Handle);
		}
		UCataclysmAbilitySystemComponent* System = nullptr;
		FName Event;
		int32 Count = 0;
		FDelegateHandle Handle;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionLossDeathEventTest,
	"Cataclysm.MinionLoss.AMinionKilledRaisesMinionDeathOnceAndStartsBoneKnitsClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * P1, as ruled: an Imp killed raises `minion_death` on its summoner once, and the clock starts. Bone Knit's row,
 * `health_regen` 3 under `seconds_after_minion_death` 3, by hand. CONTROL: before the death the row is worth nothing
 * and no event was raised; a second write at nought on the dead Imp raises nothing more.
 */
bool FCataclysmMinionLossDeathEventTest::RunTest(const FString&)
{
	using namespace CataclysmMinionLossTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionLossSummoner Summoner(World);
	Summoner.Lines({{TEXT("health_regen"), Row(ECataclysmStatBucket::Flat, 3.0f,
											   ECataclysmStatCondition::WithinSecondsOfMinionDeath, 3.0f)}});
	ACataclysmMinion* Imp = SpawnImp(Summoner.Actor, FVector(3.0f * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: an Imp"), Imp))
	{
		return false;
	}
	auto Regen = [&Summoner]()
	{
		return Summoner.AbilitySystem->StatForSkill(FName(TEXT("health_regen")), FGameplayTagContainer(), 0.0f);
	};

	FMinionLossEventCount Deaths(Summoner.AbilitySystem, TEXT("minion_death"));
	const float Before = Regen();
	const float SecondsBefore = Summoner.AbilitySystem->SecondsSinceMinionDied();
	WriteHealthAtNought(Imp);
	const int32 AfterOne = Deaths.Count;
	const float After = Regen();
	const float SecondsAfter = Summoner.AbilitySystem->SecondsSinceMinionDied();
	WriteHealthAtNought(Imp);

	AddInfo(FString::Printf(TEXT("MINION DEATH: %d event(s), then %d; regen %.1f before, %.1f after; clock %.2f -> %.2f"),
							AfterOne, Deaths.Count, Before, After, SecondsBefore, SecondsAfter));
	TestEqual(TEXT("CONTROL: before any death the clock reads never"), SecondsBefore, -1.0f, 0.001f);
	TestEqual(TEXT("CONTROL: and Bone Knit's row is worth nothing"), Before, 0.0f, 0.001f);
	TestEqual(TEXT("an Imp killed raises minion_death once"), AfterOne, 1);
	TestTrue(TEXT("and starts the clock"), SecondsAfter >= 0.0f && SecondsAfter <= 0.01f);
	TestEqual(TEXT("so Bone Knit's row holds, 3"), After, 3.0f, 0.001f);
	TestEqual(TEXT("and a second write at nought on the dead Imp raises nothing more"), Deaths.Count, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionLossDeathWardTest,
	"Cataclysm.MinionLoss.DeathWardHoldsForADeathWithinSixMetresAndNotForOneFurther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Death Ward's row by hand: `damage_reduction` 2 under `minion_death_in_reach_within_seconds` 3, reach 6. An Imp
 * killed 8 metres away starts the plain clock and not this one; one killed 4 metres away holds it.
 */
bool FCataclysmMinionLossDeathWardTest::RunTest(const FString&)
{
	using namespace CataclysmMinionLossTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionLossSummoner Summoner(World);
	Summoner.Lines({{TEXT("damage_reduction"),
					 Row(ECataclysmStatBucket::Flat, 2.0f, ECataclysmStatCondition::MinionDeathInReachWithinSeconds,
						 3.0f, 6.0f)}});
	ACataclysmMinion* Far = SpawnImp(Summoner.Actor, FVector(8.0f * M, 0, 0));
	ACataclysmMinion* Near = SpawnImp(Summoner.Actor, FVector(0, 4.0f * M, 0));
	if (!TestNotNull(TEXT("set-up: a far Imp"), Far) || !TestNotNull(TEXT("set-up: a near Imp"), Near))
	{
		return false;
	}
	auto Reduction = [&Summoner]()
	{
		return Summoner.AbilitySystem->StatForSkill(FName(TEXT("damage_reduction")), FGameplayTagContainer(), 0.0f);
	};

	WriteHealthAtNought(Far);
	const float AfterFar = Reduction();
	const float ClockAfterFar = Summoner.AbilitySystem->SecondsSinceMinionDied();
	WriteHealthAtNought(Near);
	const float AfterNear = Reduction();

	AddInfo(FString::Printf(TEXT("DEATH WARD: %.1f after a death at 8 m (clock %.2f), %.1f after one at 4 m"), AfterFar,
							ClockAfterFar, AfterNear));
	TestTrue(TEXT("set-up: the far death started the plain clock"), ClockAfterFar >= 0.0f);
	TestEqual(TEXT("CONTROL: a death 8 metres away is beyond the reach"), AfterFar, 0.0f, 0.001f);
	TestEqual(TEXT("a death 4 metres away holds Death Ward's row"), AfterNear, 2.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionLossMarrowFeastTest,
	"Cataclysm.MinionLoss.MarrowFeastHealsOnePercentForEachDeathAtMostTenTimesASecond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Marrow Feast's row by hand: a `health` action of 1% of maximum on `minion_death`, trigger cooldown 0.1 seconds (Q3:
 * "up to 10% each second"). The summoner at half of 10,000; two Imps killed in the same instant heal it once, 100, not
 * 200. A third killed 0.2 seconds later heals it again.
 */
bool FCataclysmMinionLossMarrowFeastTest::RunTest(const FString&)
{
	using namespace CataclysmMinionLossTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionLossSummoner Summoner(World);
	Summoner.SetHealthShare(0.5f);
	FCataclysmPoolAction Heal;
	Heal.Event = FName(TEXT("minion_death"));
	Heal.Pool = FName(TEXT("health"));
	Heal.Percent = 1.0f;
	Heal.Base = ECataclysmPoolActionBase::Maximum;
	Heal.TriggerCooldownSeconds = 0.1f;
	Heal.TriggerKey = FName(TEXT("Passive_marrow_feast:minion_death"));
	Summoner.AbilitySystem->SetPoolActions({Heal});

	ACataclysmMinion* First = SpawnImp(Summoner.Actor, FVector(3.0f * M, 0, 0));
	ACataclysmMinion* Second = SpawnImp(Summoner.Actor, FVector(0, 3.0f * M, 0));
	ACataclysmMinion* Third = SpawnImp(Summoner.Actor, FVector(-3.0f * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: three Imps"), First && Second ? Third : nullptr))
	{
		return false;
	}
	const float Start = Summoner.Health();
	WriteHealthAtNought(First);
	WriteHealthAtNought(Second);
	const float AfterTwo = Summoner.Health();
	CataclysmTestWorld::RunClock(World, 0.2f);
	WriteHealthAtNought(Third);
	const float AfterThree = Summoner.Health();

	AddInfo(FString::Printf(TEXT("MARROW FEAST: %.1f -> %.1f after two deaths at once -> %.1f after a third 0.2 s later"),
							Start, AfterTwo, AfterThree));
	TestEqual(TEXT("two deaths in the same instant heal once, 1% of 10,000"), AfterTwo - Start, 100.0f, 0.01f);
	TestEqual(TEXT("and a third 0.2 seconds later heals again"), AfterThree - AfterTwo, 100.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionLossAvengingDeadTest,
	"Cataclysm.MinionLoss.AvengingDeadsStackIsGrantedOnlyByAHitThatTookFivePercent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Avenging Dead's grant, as `CataclysmItem.cpp` builds it from the row: an own stack on `hit_taken` carrying the event
 * condition `hit_took_at_least_percent_of_max_health` 5, and the stat row `minion_attack_speed` 3 per stack carrying
 * none. A landed hit that took 4% grants nothing; one that took 6% grants the stack, and the stat reads 3. A tick that
 * took 6% grants nothing, as every tick on `hit_taken` does.
 */
bool FCataclysmMinionLossAvengingDeadTest::RunTest(const FString&)
{
	using namespace CataclysmMinionLossTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionLossSummoner Summoner(World);
	const FName Key(TEXT("Passive_avenging_dead"));
	FCataclysmPoolAction Grant;
	Grant.Event = FName(TEXT("hit_taken"));
	Grant.StackKey = Key;
	Grant.StackSeconds = 4.0f;
	Grant.StackCap = 1;
	Grant.Condition = ECataclysmStatCondition::HitTookAtLeastPercentOfMaxHealth;
	Grant.ConditionValue = 5.0f;
	Grant.TriggerKey = FName(TEXT("Passive_avenging_dead:hit_taken"));
	Summoner.AbilitySystem->SetPoolActions({Grant});
	FCataclysmStatModifier PerStack = Row(ECataclysmStatBucket::Increased, 3.0f);
	PerStack.Scale = ECataclysmStatScale::PerOwnStack;
	PerStack.ScaleStep = 1.0f;
	PerStack.ScaleMaxSteps = 1;
	PerStack.StackKey = Key;
	Summoner.Lines({{TEXT("minion_attack_speed"), PerStack}});

	Summoner.AbilitySystem->NoteHitTaken(/*bLanded=*/true, nullptr, /*bDamageOverTime=*/false, 4.0f);
	const int32 AfterFour = Summoner.AbilitySystem->OwnStacksHeld(Key);
	Summoner.AbilitySystem->NoteHitTaken(/*bLanded=*/true, nullptr, /*bDamageOverTime=*/true, 6.0f);
	const int32 AfterTick = Summoner.AbilitySystem->OwnStacksHeld(Key);
	Summoner.AbilitySystem->NoteHitTaken(/*bLanded=*/true, nullptr, /*bDamageOverTime=*/false, 6.0f);
	const int32 AfterSix = Summoner.AbilitySystem->OwnStacksHeld(Key);
	const float Increases =
		Summoner.AbilitySystem->IncreasesForStat(FName(TEXT("minion_attack_speed")), FGameplayTagContainer());

	AddInfo(FString::Printf(TEXT("AVENGING DEAD: stacks %d after 4%%, %d after a 6%% tick, %d after a 6%% hit; "
								 "minion attack speed increases %.3f"),
							AfterFour, AfterTick, AfterSix, Increases));
	TestEqual(TEXT("CONTROL: a hit that took 4% grants nothing"), AfterFour, 0);
	TestEqual(TEXT("CONTROL: nor does a tick that took 6%"), AfterTick, 0);
	TestEqual(TEXT("a hit that took 6% grants the stack"), AfterSix, 1);
	TestEqual(TEXT("and the stat carries no condition of its own: 3% increased"), Increases, 0.03f, 0.0001f);
	TestEqual(TEXT("outside the hit's announcement the share reads never"),
			  Summoner.AbilitySystem->CurrentConditions().HitTakenPercentOfMaxHealth, -1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionLossHitTakenRowTest,
	"Cataclysm.MinionLoss.AnExistingHitTakenHealthRowIsUnchangedByTheShareTheHitCarries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * As ruled (Q2), an existing `hit_taken` row is unchanged: the drawback "Every hit you take deals an additional 5% of
 * your maximum health", a `health` action of -5% of maximum on `hit_taken`. `hit_taken` now carries the share the hit
 * took as its amount, and this row takes a share of the maximum, so a hit that took 50% of maximum health costs 500
 * of 10,000 as one that took 1% does.
 */
bool FCataclysmMinionLossHitTakenRowTest::RunTest(const FString&)
{
	using namespace CataclysmMinionLossTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionLossSummoner Summoner(World);
	FCataclysmPoolAction Drawback;
	Drawback.Event = FName(TEXT("hit_taken"));
	Drawback.Pool = FName(TEXT("health"));
	Drawback.Percent = -5.0f;
	Drawback.Base = ECataclysmPoolActionBase::Maximum;
	Drawback.TriggerKey = FName(TEXT("Negative_drawback:hit_taken"));
	Summoner.AbilitySystem->SetPoolActions({Drawback});

	Summoner.AbilitySystem->NoteHitTaken(/*bLanded=*/true, nullptr, /*bDamageOverTime=*/false, 1.0f);
	const float AfterSmall = Summoner.Health();
	Summoner.SetHealthShare(1.0f);
	Summoner.AbilitySystem->NoteHitTaken(/*bLanded=*/true, nullptr, /*bDamageOverTime=*/false, 50.0f);
	const float AfterLarge = Summoner.Health();

	AddInfo(FString::Printf(TEXT("HIT TAKEN DRAWBACK: %.1f after a 1%% hit, %.1f after a 50%% hit"), AfterSmall,
							AfterLarge));
	TestEqual(TEXT("a hit that took 1% costs 5% of maximum"), Pool - AfterSmall, 500.0f, 0.01f);
	TestEqual(TEXT("and one that took 50% costs the same"), Pool - AfterLarge, 500.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionLossMarshalTest,
	"Cataclysm.MinionLoss.MarshalRaisesTheDamageOfAMinionWithinTenMetresOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Marshal at 10 points by hand: `minion_damage` 20% increased under `minion_within_metres_of_you` 10. The summoner's
 * multiplier for an Imp 3 metres away is 1.2; for one 15 metres away, 1; asked with no minion named, 1.
 */
bool FCataclysmMinionLossMarshalTest::RunTest(const FString&)
{
	using namespace CataclysmMinionLossTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionLossSummoner Summoner(World);
	Summoner.Lines({{TEXT("minion_damage"), Row(ECataclysmStatBucket::Increased, 20.0f,
												ECataclysmStatCondition::MinionWithinMetresOfYou, 10.0f)}});
	ACataclysmMinion* Near = SpawnImp(Summoner.Actor, FVector(3.0f * M, 0, 0));
	ACataclysmMinion* Far = SpawnImp(Summoner.Actor, FVector(15.0f * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: two Imps"), Near ? Far : nullptr))
	{
		return false;
	}
	const float ForNear = UCataclysmCommand::SummonerMultiplierAgainst(Summoner.Actor, TEXT("minion_damage"), nullptr,
																	   Near->TypeTags, Near);
	const float ForFar = UCataclysmCommand::SummonerMultiplierAgainst(Summoner.Actor, TEXT("minion_damage"), nullptr,
																	  Far->TypeTags, Far);
	const float ForNobody =
		UCataclysmCommand::SummonerMultiplierAgainst(Summoner.Actor, TEXT("minion_damage"), nullptr, Near->TypeTags);

	AddInfo(FString::Printf(TEXT("MARSHAL: %.3f at 3 m, %.3f at 15 m, %.3f with no minion named"), ForNear, ForFar,
							ForNobody));
	TestEqual(TEXT("an Imp within 10 metres deals 20% more"), ForNear, 1.2f, 0.001f);
	TestEqual(TEXT("CONTROL: one 15 metres away does not"), ForFar, 1.0f, 0.001f);
	TestEqual(TEXT("CONTROL: nor does a lookup that names no minion"), ForNobody, 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionLossSharedLeechTest,
	"Cataclysm.MinionLoss.SharedLeechPaysTheSummonerAShareOfAMinionsLandedBlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Shared Leech by hand at 10%, for a figure easy to read: a real Imp's landed blows on an enemy. The summoner is owed
 * 10% of what the enemy lost, as health. CONTROL: before the row, a landed blow pays the summoner nothing.
 */
bool FCataclysmMinionLossSharedLeechTest::RunTest(const FString&)
{
	using namespace CataclysmMinionLossTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionLossSummoner Summoner(World);
	ACataclysmEnemyCharacter* Victim =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(4.0f * M, 0, 0), FRotator::ZeroRotator);
	ACataclysmMinion* Imp = SpawnImp(Summoner.Actor, FVector(3.0f * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a victim"), Victim) || !TestNotNull(TEXT("set-up: an Imp"), Imp))
	{
		return false;
	}
	Victim->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Victim->SetHealth(1'000'000.0f);
	const UAbilitySystemComponent* VictimSystem = UCataclysmTargeting::AbilitySystemOf(Victim);
	auto VictimHealth = [VictimSystem]()
	{
		return VictimSystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	};
	// AN EVADED BLOW LANDS NOTHING, so each half strikes until health moves; twenty is room to spare.
	auto LandOne = [Imp, Victim, &VictimHealth]()
	{
		for (int32 Try = 0; Try < 20; ++Try)
		{
			const float Was = VictimHealth();
			Imp->AttackTarget(Victim);
			if (VictimHealth() < Was)
			{
				return Was - VictimHealth();
			}
		}
		return 0.0f;
	};

	const float FirstLoss = LandOne();
	const int32 PaidWithNoRow = Summoner.AbilitySystem->GetLeechPayments().Num();
	Summoner.Lines({{UCataclysmLeech::MinionDamageLeechedStat, Row(ECataclysmStatBucket::Flat, 10.0f)}});
	const float SecondLoss = LandOne();
	const TArray<FCataclysmLeechPayment>& Paid = Summoner.AbilitySystem->GetLeechPayments();

	AddInfo(FString::Printf(TEXT("SHARED LEECH: blows took %.2f and %.2f; %d payment(s) before the row, %d after"),
							FirstLoss, SecondLoss, PaidWithNoRow, Paid.Num()));
	if (!TestTrue(TEXT("set-up: both blows landed"), FirstLoss > 0.0f && SecondLoss > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: with no row a landed blow pays the summoner nothing"), PaidWithNoRow, 0);
	if (TestEqual(TEXT("with 10%, one payment"), Paid.Num(), 1))
	{
		TestTrue(TEXT("of health"), Paid[0].Pool == ECataclysmLeechPool::Health);
		TestEqual(TEXT("of 10% of what the blow took"), Paid[0].Remaining, SecondLoss * 0.1f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionLossEternalArmyTest,
	"Cataclysm.MinionLoss.EternalArmyKeepsAMinionAtOneHealthOnlyWhileTheSummonerIsAboveHalf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Eternal Army by hand: `minion_cannot_die_above_health_percent` 50. With the summoner at 60%, an Imp's health written
 * at nought leaves it alive at 1 and raises no `minion_death`; at exactly 50%, "above" is strict and the next Imp dies;
 * at 40%, so does the third.
 */
bool FCataclysmMinionLossEternalArmyTest::RunTest(const FString&)
{
	using namespace CataclysmMinionLossTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionLossSummoner Summoner(World);
	Summoner.Lines({{UCataclysmVitalAttributeSet::MinionCannotDieAboveHealthPercentStat,
					 Row(ECataclysmStatBucket::Flat, 50.0f)}});
	FMinionLossEventCount Deaths(Summoner.AbilitySystem, TEXT("minion_death"));
	auto Written = [&Summoner](float Share, float Metres, bool& bOutDead)
	{
		Summoner.SetHealthShare(Share);
		ACataclysmMinion* Imp = SpawnImp(Summoner.Actor, FVector(3.0f * M, Metres * M, 0));
		WriteHealthAtNought(Imp);
		bOutDead = !Imp || UCataclysmSkillEffects::IsDead(Imp);
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Imp);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	};
	bool bDeadAtSixty = true;
	const float LeftAtSixty = Written(0.6f, -3.0f, bDeadAtSixty);
	const int32 DeathsAtSixty = Deaths.Count;
	bool bDeadAtFifty = false;
	Written(0.5f, 0.0f, bDeadAtFifty);
	bool bDeadAtForty = false;
	Written(0.4f, 3.0f, bDeadAtForty);

	AddInfo(FString::Printf(TEXT("ETERNAL ARMY: at 60%% dead %s with %.1f left; at 50%% dead %s; at 40%% dead %s; "
								 "%d minion_death"),
							bDeadAtSixty ? TEXT("yes") : TEXT("no"), LeftAtSixty, bDeadAtFifty ? TEXT("yes") : TEXT("no"),
							bDeadAtForty ? TEXT("yes") : TEXT("no"), Deaths.Count));
	TestFalse(TEXT("with the summoner at 60% the Imp does not die"), bDeadAtSixty);
	TestEqual(TEXT("and is left at 1 health"), LeftAtSixty, 1.0f, 0.001f);
	TestEqual(TEXT("and raises no minion_death"), DeathsAtSixty, 0);
	TestTrue(TEXT("CONTROL: at exactly 50%, \"above\" is strict, so it dies"), bDeadAtFifty);
	TestTrue(TEXT("CONTROL: and at 40% it dies"), bDeadAtForty);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionLossBoundBonesTest,
	"Cataclysm.MinionLoss.BoundBonesRecountsTheHealthAsMinionsComeAndGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Bound Bones by hand: `minion_health_percent_of_yours_per_other_minion` 5, a summoner of 10,000 maximum health. With
 * three Imps, a step gives the first 1,000 more maximum health; a second step changes nothing; with one of the others
 * killed, a step takes 500 back and its health is held within the new maximum.
 */
bool FCataclysmMinionLossBoundBonesTest::RunTest(const FString&)
{
	using namespace CataclysmMinionLossTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionLossSummoner Summoner(World);
	Summoner.Lines({{UCataclysmRegeneration::BoundBonesStat, Row(ECataclysmStatBucket::Flat, 5.0f)}});
	ACataclysmMinion* First = SpawnImp(Summoner.Actor, FVector(3.0f * M, 0, 0));
	ACataclysmMinion* Second = SpawnImp(Summoner.Actor, FVector(0, 3.0f * M, 0));
	ACataclysmMinion* Third = SpawnImp(Summoner.Actor, FVector(-3.0f * M, 0, 0));
	UAbilitySystemComponent* FirstSystem = UCataclysmTargeting::AbilitySystemOf(First);
	if (!TestNotNull(TEXT("set-up: three Imps"), Second && Third ? FirstSystem : nullptr))
	{
		return false;
	}
	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const float Start = FirstSystem->GetNumericAttribute(MaxHealth);
	UCataclysmRegeneration::BoundBonesStep(First);
	const float WithTwoOthers = FirstSystem->GetNumericAttribute(MaxHealth);
	UCataclysmRegeneration::BoundBonesStep(First);
	const float AgainWithTwo = FirstSystem->GetNumericAttribute(MaxHealth);
	WriteHealthAtNought(Third);
	UCataclysmRegeneration::BoundBonesStep(First);
	const float WithOneOther = FirstSystem->GetNumericAttribute(MaxHealth);

	AddInfo(FString::Printf(TEXT("BOUND BONES: maximum %.1f -> %.1f with two others -> %.1f again -> %.1f with one"),
							Start, WithTwoOthers, AgainWithTwo, WithOneOther));
	TestEqual(TEXT("two others: 5% of 10,000 each, 1,000 more"), WithTwoOthers - Start, 1000.0f, 0.01f);
	TestEqual(TEXT("a second step with the same count changes nothing"), AgainWithTwo, WithTwoOthers, 0.01f);
	TestEqual(TEXT("one other killed: 500 taken back"), WithOneOther - Start, 500.0f, 0.01f);
	TestTrue(TEXT("and its health is within the new maximum"), FirstSystem->GetNumericAttribute(Health) <= WithOneOther);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionLossRealRowsTest,
	"Cataclysm.MinionLoss.TheEightNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The real rows of the eight nodes, read from the passive effects table. */
bool FCataclysmMinionLossRealRowsTest::RunTest(const FString&)
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
		const TCHAR* StatOrAction;
		float Value;
		const TCHAR* Condition;
		float ConditionValue;
	};
	const FExpected Expected[] = {
		{TEXT("Necromancer_basic_gr_b1"), 0, TEXT("damage_reduction"), 2.0f, TEXT("minion_death_in_reach_within_seconds"), 3.0f},
		{TEXT("Necromancer_basic_gr_b2"), 0, TEXT("health_regen"), 3.0f, TEXT("seconds_after_minion_death"), 3.0f},
		{TEXT("Necromancer_keystone_gr_kB"), 0, TEXT("health"), 1.0f, TEXT(""), 0.0f},
		{TEXT("Necromancer_basic_gr_stem2"), 0, TEXT("minion_attack_speed"), 3.0f, TEXT("hit_took_at_least_percent_of_max_health"), 5.0f},
		{TEXT("Necromancer_basic_gr_stem1"), 0, TEXT("minion_damage"), 2.0f, TEXT("minion_within_metres_of_you"), 10.0f},
		{TEXT("Necromancer_basic_gr_b0"), 0, TEXT("minion_damage_leeched_to_you_percent"), 1.0f, TEXT(""), 0.0f},
		{TEXT("Necromancer_capstone_200"), 2, TEXT("minion_cannot_die_above_health_percent"), 50.0f, TEXT(""), 0.0f},
		{TEXT("Necromancer_capstone_100"), 3, TEXT("minion_health_percent_of_yours_per_other_minion"), 5.0f, TEXT(""), 0.0f},
	};
	for (const FExpected& Want : Expected)
	{
		int32 Found = 0;
		for (const FCataclysmPassiveEffectRow* Effect : UCataclysmPassiveTree::EffectsFor(Effects, FName(Want.Node)))
		{
			const bool bAction = FString(Want.StatOrAction) == TEXT("health");
			if (Effect->Option != Want.Option
				|| (bAction ? Effect->Action != Want.StatOrAction : Effect->Stat != Want.StatOrAction))
			{
				continue;
			}
			++Found;
			const FString Label = FString::Printf(TEXT("%s %s"), Want.Node, Want.StatOrAction);
			TestEqual(*(Label + TEXT(": the value")), Effect->ValuePerPoint, Want.Value, 0.001f);
			TestEqual(*(Label + TEXT(": the condition")), Effect->Condition, FString(Want.Condition));
			TestEqual(*(Label + TEXT(": its value")), Effect->ConditionValue, Want.ConditionValue, 0.001f);
			if (bAction)
			{
				TestEqual(*(Label + TEXT(": on minion_death")), Effect->ActionEvent, FString(TEXT("minion_death")));
				TestEqual(*(Label + TEXT(": at most ten times a second")), Effect->TriggerCooldown, 0.1f, 0.001f);
			}
		}
		TestEqual(*FString::Printf(TEXT("%s %s: one row"), Want.Node, Want.StatOrAction), Found, 1);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
