// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmContagion.h"
#include "AbilitySystem/CataclysmLeech.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmRisenImps.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The four claims the Ritualist audit of 2026-11-24 asked to be measured before the reworked nodes state them to a
 * player, each in the shape of the row that rests on it. The rows are granted by hand, so each case varies one thing:
 *
 *   (a) a `minion_damage` row scaled by `minions_held` changes a minion's blow: Legion, and Set the Pack On's "more"
 *       under its condition, scaled.
 *   (b) a summoner's own `stationary_for_seconds` is judged on its minion's blow: Measured Words.
 *   (c) a spell's hit leeches energy shield through `energy_shield_leech` scoped to `Type.Spell`: Steady Hand.
 *   (d) under Conduit an enemy only a minion hit counts as one the summoner damaged: Conduit's reworded last clause;
 *       and a curse a dying enemy carried passes, credited to the killer, so its new carrier rises for it: Broken Will.
 */
namespace CataclysmRitualistReworkChecksTest
{
	constexpr float M = 100.0f;

	/** Something with an ability system, standing somewhere, that can lay, ask, command and be struck. */
	struct FReworkCheckScopedActor
	{
		FReworkCheckScopedActor(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
			check(Actor);

			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Where);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// Raw pointers on purpose: AddAttributeSetSubobject is a template and a TObjectPtr deduces the wrapper.
			UCataclysmVitalAttributeSet* Vitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* Combat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* Resource = NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(Vitals);
			AbilitySystem->AddAttributeSetSubobject(Combat);
			AbilitySystem->AddAttributeSetSubobject(Resource);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
		}

		~FReworkCheckScopedActor()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value)
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		/** Replace the whole stat line with these modifiers, one stat each. */
		void Hold(const TMap<FName, FCataclysmStatModifier>& Rows)
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const TPair<FName, FCataclysmStatModifier>& Row : Rows)
			{
				FCataclysmStatInputs& Line = Stats.FindOrAdd(Row.Key);
				Line.Base = 0.0f;
				Line.Modifiers.Add(Row.Value);
			}
			AbilitySystem->SetStatInputs(MoveTemp(Stats));
		}

		ACataclysmMinion* Summon(const FVector& Where)
		{
			return ACataclysmMinion::Spawn(Actor, Where, /*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("Imp"));
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	FCataclysmStatModifier Row(ECataclysmStatBucket Bucket, float Value)
	{
		FCataclysmStatModifier Made;
		Made.Bucket = Bucket;
		Made.Source = ECataclysmModifierSource::PassiveKeystone;
		Made.Value = Value;
		return Made;
	}

	float BlowOn(ACataclysmMinion* Striker, FReworkCheckScopedActor& Target)
	{
		const float Before = Target.Health();
		Striker->AttackTarget(Target.Actor);
		return Before - Target.Health();
	}

	FGameplayTag NamedTag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a
// `using namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReworkLegionTest,
	"Cataclysm.RitualistRework.AMinionDamageRowScaledByMinionsHeldRaisesAnImpsBlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * (a), Legion's shape: `minion_damage` increased 10 for each minion held. The same imp strikes the same target with
 * one minion held and then with three: (1 + 0.3) / (1 + 0.1). A read that ignored the scale gives 1.
 */
bool FCataclysmReworkLegionTest::RunTest(const FString&)
{
	using namespace CataclysmRitualistReworkChecksTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FReworkCheckScopedActor Summoner(World, FVector::ZeroVector);
	FReworkCheckScopedActor Target(World, FVector(4 * M, 0, 0));
	FCataclysmStatModifier PerMinion = Row(ECataclysmStatBucket::Increased, 10.0f);
	PerMinion.Scale = ECataclysmStatScale::PerMinionHeld;
	PerMinion.ScaleStep = 1.0f;
	Summoner.Hold({{FName(TEXT("minion_damage")), PerMinion}});

	ACataclysmMinion* Striker = Summoner.Summon(FVector(2 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: an imp"), Striker))
	{
		return false;
	}
	const float WithOne = BlowOn(Striker, Target);
	Summoner.Summon(FVector(-4 * M, 0, 0));
	Summoner.Summon(FVector(-8 * M, 0, 0));
	const float WithThree = BlowOn(Striker, Target);
	if (!TestTrue(TEXT("set-up: the blow lands"), WithOne > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("with three minions held the imp's blow is 1.3 / 1.1 of its blow with one"), WithThree / WithOne,
			  1.3f / 1.1f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReworkPackTest,
	"Cataclysm.RitualistRework.AScaledMoreRowUnderItsConditionMultipliesAnImpsBlowOnAnEnemyYouDamaged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * (a), Set the Pack On's shape: `minion_damage` more 3 for each minion held, under
 * `target_damaged_by_you_within_seconds` 2. With two minions held, an enemy the summoner struck a moment ago takes
 * 1.06 times the imp's blow on one it never struck.
 */
bool FCataclysmReworkPackTest::RunTest(const FString&)
{
	using namespace CataclysmRitualistReworkChecksTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FReworkCheckScopedActor Summoner(World, FVector::ZeroVector);
	FReworkCheckScopedActor Struck(World, FVector(4 * M, 0, 0));
	FReworkCheckScopedActor Untouched(World, FVector(4 * M, 4 * M, 0));
	FCataclysmStatModifier Pack = Row(ECataclysmStatBucket::More, 3.0f);
	Pack.Condition = ECataclysmStatCondition::TargetDamagedByYouWithinSeconds;
	Pack.ConditionValue = 2.0f;
	Pack.Scale = ECataclysmStatScale::PerMinionHeld;
	Pack.ScaleStep = 1.0f;
	Summoner.Hold({{FName(TEXT("minion_damage")), Pack}});

	ACataclysmMinion* Striker = Summoner.Summon(FVector(2 * M, 0, 0));
	ACataclysmMinion* Other = Summoner.Summon(FVector(-4 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: two imps"), Striker) || !TestNotNull(TEXT("set-up: two imps"), Other))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the summoner's blow on the first enemy is recorded"),
				  Struck.AbilitySystem->NoteStruckBy(Summoner.AbilitySystem, /*bCritical=*/false)))
	{
		return false;
	}

	const float OnStruck = BlowOn(Striker, Struck);
	const float OnUntouched = BlowOn(Striker, Untouched);
	if (!TestTrue(TEXT("set-up: the blow lands"), OnUntouched > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("two minions held make the blow on the enemy you damaged 1.06 times the other"),
			  OnStruck / OnUntouched, 1.06f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReworkStillnessTest,
	"Cataclysm.RitualistRework.TheSummonersStillnessIsJudgedOnItsImpsBlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * (b), Measured Words' shape: `minion_damage` increased 20 under `stationary_for_seconds` 2, on the summoner. The imp's
 * blow reads the summoner's component, so the summoner's own stillness decides it: just after the summoner moved the
 * blow is plain, and after it has stood still for over two seconds it is 1.2 times that.
 */
bool FCataclysmReworkStillnessTest::RunTest(const FString&)
{
	using namespace CataclysmRitualistReworkChecksTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FReworkCheckScopedActor Summoner(World, FVector::ZeroVector);
	FReworkCheckScopedActor Target(World, FVector(4 * M, 0, 0));
	FCataclysmStatModifier Still = Row(ECataclysmStatBucket::Increased, 20.0f);
	Still.Condition = ECataclysmStatCondition::StationaryForSeconds;
	Still.ConditionValue = 2.0f;
	Summoner.Hold({{FName(TEXT("minion_damage")), Still}});

	ACataclysmMinion* Striker = Summoner.Summon(FVector(2 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: an imp"), Striker))
	{
		return false;
	}

	Summoner.AbilitySystem->NoteMovedMetres(1.0f);
	const float JustMoved = BlowOn(Striker, Target);
	Summoner.AbilitySystem->NoteDidNotMove();
	CataclysmTestWorld::RunClock(World, 2.5f);
	const float StoodStill = BlowOn(Striker, Target);
	if (!TestTrue(TEXT("set-up: the blow lands"), JustMoved > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("after the summoner stood still for two seconds the imp's blow is 1.2 times its blow just after "
				   "the summoner moved"),
			  StoodStill / JustMoved, 1.2f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReworkSpellLeechTest,
	"Cataclysm.RitualistRework.ASpellsHitLeechesEnergyShieldUnderARowScopedToSpellsAndAStrikesDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * (c), Steady Hand's shape: `energy_shield_leech` flat 2 requiring `Type.Spell`. `UCataclysmLeech::NoteHit` is handed
 * the tags of the skill that dealt the hit (issue #947); a hit of 1000 under a spell's tags queues 20 into the energy
 * shield, and the same hit under a strike's tags queues nothing.
 */
bool FCataclysmReworkSpellLeechTest::RunTest(const FString&)
{
	using namespace CataclysmRitualistReworkChecksTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FGameplayTag Spell = NamedTag(TEXT("Type.Spell"));
	const FGameplayTag Strike = NamedTag(TEXT("Type.Strike"));
	if (!TestTrue(TEXT("set-up: both tags are declared"), Spell.IsValid() && Strike.IsValid()))
	{
		return false;
	}

	const auto QueuedIntoShield = [World, &Spell](const FGameplayTag& Dealt)
	{
		FReworkCheckScopedActor Caster(World, FVector::ZeroVector);
		Caster.Set(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 1000.0f);
		FCataclysmStatModifier Leech = Row(ECataclysmStatBucket::Flat, 2.0f);
		Leech.RequiredTags.AddTag(Spell);
		Caster.Hold({{FName(TEXT("energy_shield_leech")), Leech}});

		UCataclysmLeech::NoteHit(Caster.AbilitySystem, 1000.0f, FGameplayTagContainer(Dealt));
		float Total = 0.0f;
		for (const FCataclysmLeechPayment& Payment : Caster.AbilitySystem->GetLeechPayments())
		{
			Total += Payment.Pool == ECataclysmLeechPool::EnergyShield ? Payment.Remaining : 0.0f;
		}
		return Total;
	};

	TestEqual(TEXT("a spell's hit of 1000 queues 2% of it into the energy shield"), QueuedIntoShield(Spell), 20.0f,
			  0.01f);
	TestEqual(TEXT("control: a strike's hit queues nothing, the row being scoped to spells"),
			  QueuedIntoShield(Strike), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReworkConduitTest,
	"Cataclysm.RitualistRework.UnderConduitAnEnemyOnlyYourMinionHitCountsAsOneYouDamaged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * (d), Conduit's reworded last clause: "an enemy a minion hits counts as one you have damaged". Two summoners, each
 * with `spell_damage` increased 20 under `target_damaged_by_you_within_seconds` 2, each with an imp that strikes its own
 * enemy; neither summoner strikes. Only the one holding Conduit (`minion_hits_count_as_yours`) reads the bonus against
 * the enemy its imp hit, because `NoteStruckBy` records the blow under `UCataclysmCombatEvents::AttackerOf`.
 */
bool FCataclysmReworkConduitTest::RunTest(const FString&)
{
	using namespace CataclysmRitualistReworkChecksTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FReworkCheckScopedActor WithConduit(World, FVector::ZeroVector);
	FReworkCheckScopedActor Without(World, FVector(0, 20 * M, 0));
	FReworkCheckScopedActor HitByMine(World, FVector(4 * M, 0, 0));
	FReworkCheckScopedActor HitByTheirs(World, FVector(4 * M, 20 * M, 0));

	FCataclysmStatModifier Recent = Row(ECataclysmStatBucket::Increased, 20.0f);
	Recent.Condition = ECataclysmStatCondition::TargetDamagedByYouWithinSeconds;
	Recent.ConditionValue = 2.0f;
	WithConduit.Hold({{FName(TEXT("spell_damage")), Recent},
					  {FName(TEXT("minion_hits_count_as_yours")), Row(ECataclysmStatBucket::Flat, 1.0f)}});
	Without.Hold({{FName(TEXT("spell_damage")), Recent}});

	ACataclysmMinion* Mine = WithConduit.Summon(FVector(2 * M, 0, 0));
	ACataclysmMinion* Theirs = Without.Summon(FVector(2 * M, 20 * M, 0));
	if (!TestNotNull(TEXT("set-up: two imps"), Mine) || !TestNotNull(TEXT("set-up: two imps"), Theirs))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the imp's blow lands"), BlowOn(Mine, HitByMine) > 0.0f)
		|| !TestTrue(TEXT("set-up: and the other's"), BlowOn(Theirs, HitByTheirs) > 0.0f))
	{
		return false;
	}

	const FGameplayTagContainer NoTags;
	TestEqual(TEXT("under Conduit the enemy your imp hit counts as one you damaged: 1.2"),
			  WithConduit.AbilitySystem->MultiplierForStatAgainst(FName(TEXT("spell_damage")), NoTags,
																  HitByMine.Actor),
			  1.2f, 0.0001f);
	TestEqual(TEXT("control: without Conduit the enemy your imp hit is not one you damaged"),
			  Without.AbilitySystem->MultiplierForStatAgainst(FName(TEXT("spell_damage")), NoTags,
															  HitByTheirs.Actor),
			  1.0f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReworkBrokenWillTest,
	"Cataclysm.RitualistRework.ACursePassedOnDeathIsTheKillersAndItsNewCarrierRisesForIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * (d), Broken Will's shape: `debuff_spread_on_death_chance` on a Ritualist holding the risen-imp rule, on a certain
 * roll. A body carrying the Ritualist's Shred AND a Bleed a stranger laid passes BOTH to the creature beside it: the
 * spread passes every debuff, not only curses and burns. The Shred that arrives is credited to the Ritualist, so the
 * creature now carrying it rises for the Ritualist when it dies (`CurserOf`).
 */
bool FCataclysmReworkBrokenWillTest::RunTest(const FString&)
{
	using namespace CataclysmRitualistReworkChecksTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FReworkCheckScopedActor Ritualist(World, FVector(0, 0, 40 * M));
	FReworkCheckScopedActor Stranger(World, FVector(0, -40 * M, 0));
	FReworkCheckScopedActor Dying(World, FVector::ZeroVector);
	FReworkCheckScopedActor Beside(World, FVector(2 * M, 0, 0));
	Ritualist.Hold({{FName(UCataclysmRisenImps::RisesStat), Row(ECataclysmStatBucket::Flat, 1.0f)},
					{FName(UCataclysmContagion::DeathChanceStat), Row(ECataclysmStatBucket::Flat, 50.0f)}});

	const FGameplayTag Shred = NamedTag(TEXT("Status.Debuff.Shred"));
	const FGameplayTag Bleed = NamedTag(TEXT("Keyword.DoT.Bleed"));
	if (!TestTrue(TEXT("set-up: the Ritualist lays Shred on the body"),
				  UCataclysmSkillEffects::ApplyTagForDuration(Ritualist.Actor, Dying.Actor, Shred, 30.0f))
		|| !TestTrue(TEXT("set-up: a stranger makes it bleed"),
					 UCataclysmSkillEffects::ApplyDamageOverTime(Stranger.Actor, Dying.Actor, 1.0f, 30.0f, Bleed)))
	{
		return false;
	}
	UCataclysmSkillEffects::MarkDead(Dying.Actor);

	TestEqual(TEXT("on a certain roll both debuffs pass"),
			  UCataclysmContagion::SpreadOnDeath(Dying.Actor, Ritualist.Actor, /*PinnedRoll=*/0.0f), 2);
	TestTrue(TEXT("the creature beside the body now carries the Shred"),
			 UCataclysmSkillEffects::HasTag(Beside.Actor, Shred));
	TestTrue(TEXT("and the Bleed, which is neither a curse nor a burn"),
			 UCataclysmSkillEffects::HasTag(Beside.Actor, Bleed));
	TestTrue(TEXT("the Shred it carries is the Ritualist's"),
			 UCataclysmRisenImps::CarriesCurseLaidBy(Beside.Actor, Ritualist.Actor));
	TestTrue(TEXT("so when it dies it rises for the Ritualist"), UCataclysmRisenImps::CurserOf(Beside.Actor)
															  == Ritualist.Actor);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
