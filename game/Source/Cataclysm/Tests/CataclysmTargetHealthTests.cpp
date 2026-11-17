// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A bonus decided by how much health the character being hit has left.
 * Issue #1515.
 *
 * WHAT THESE ARE FOR. Two nodes:
 *
 *   Ravager_basic_d_a2       Cornered Quarry
 *       "+2% increased Attack Damage per point against enemies below 35% health."
 *   Ritualist_basic_a_stem1  Broken Will
 *       "+2% increased Spell Damage per point against enemies below half health."
 *
 * Both are six points, so both are worth +12% at full investment.
 *
 * NOTHING HERE STATES A HEALTH SHARE TO THE PIPELINE. Every test spawns a real
 * creature, writes its health onto its own attributes, and strikes it, so the
 * whole chain runs. That is the lesson `CataclysmStaggeredTargetTests.cpp`
 * records in its own words: a set of tests that all built their own hit and
 * filled the fact in themselves failed nothing when the line filling it was
 * broken, three times running. A test that supplies the missing step proves
 * nothing.
 *
 * THE MAXIMUM IS WRITTEN BEFORE THE CURRENT HEALTH, because health is clamped to
 * it. `CataclysmAilmentTests.cpp` records the same ordering.
 *
 * THE 10,000 POOL IS FOR RESOLUTION AND THE BOUNDARY IS EXACT AT IT, MEASURED.
 * A blow read as the difference of two health readings cannot resolve a
 * tolerance finer than the float step at that magnitude, and near 1,000,000 a
 * float steps 0.0625 -- issue #1728. At 10,000 the step is 0.0009766.
 *
 * AND THE ON-THE-BOUNDARY TEST NEEDED CHECKING RATHER THAN ASSUMING, because
 * 0.35 is not a binary fraction. Measured in float32: `3500.0f / 10000.0f *
 * 100.0f` is exactly `35.0f`, because the rounding in the division and the
 * rounding in the multiplication cancel. So a target on exactly 35% reads
 * exactly 35 and `35 < 35` is false, which is the behaviour the node's word
 * "below" promises. **That is true of these numbers and was not assumed to be
 * true in general.**
 */
namespace CataclysmTargetHealthTest
{
	/** Centimetres in a metre, so a test can place an actor in metres. */
	constexpr float HealthM = 100.0f;

	/** Every creature here is spawned with this maximum. See the file comment. */
	constexpr float PoolMax = 10'000.0f;

	/** A bare actor holding an ability system, the attributes a blow reads and a weapon. */
	struct FHealthArmedActor
	{
		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * An armed actor, under this file's own names so a unity build does not see
	 * two of them.
	 */
	FHealthArmedActor MakeHealthArmed(UWorld* World)
	{
		FHealthArmedActor Made;
		Made.Actor = World->SpawnActor<AActor>();
		if (!Made.Actor)
		{
			return Made;
		}

		Made.AbilitySystem =
			NewObject<UCataclysmAbilitySystemComponent>(Made.Actor);
		Made.AbilitySystem->RegisterComponent();
		Made.AbilitySystem->AddAttributeSetSubobject(
			NewObject<UCataclysmCombatAttributeSet>(Made.Actor));
		Made.AbilitySystem->AddAttributeSetSubobject(
			NewObject<UCataclysmVitalAttributeSet>(Made.Actor));
		Made.AbilitySystem->InitAbilityActorInfo(Made.Actor, Made.Actor);

		UCataclysmWeaponSlotsComponent* Slots =
			NewObject<UCataclysmWeaponSlotsComponent>(Made.Actor);
		Slots->RegisterComponent();
		Slots->SetDamageType(TEXT("Demonic"));

		Made.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		return Made;
	}

	/**
	 * A creature on the monsters' side, standing here, holding this much of a
	 * 10,000 maximum.
	 *
	 * THE MAXIMUM FIRST AND THE CURRENT HEALTH SECOND, because health is clamped
	 * to the maximum and the other order silently gives a creature at full
	 * health.
	 */
	ACataclysmEnemyCharacter* SpawnAt(UWorld* World, const FVector& Where,
									  float Health)
	{
		ACataclysmEnemyCharacter* Spawned =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where,
													   FRotator::ZeroRotator);
		if (!Spawned)
		{
			return nullptr;
		}
		Spawned->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Spawned->SetHealth(PoolMax);

		if (UAbilitySystemComponent* System =
				UCataclysmTargeting::AbilitySystemOf(Spawned))
		{
			System->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), PoolMax);
			System->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
		}
		return Spawned;
	}

	/** A creature's health, read off its ability system rather than the character. */
	float HealthOf(AActor* Creature)
	{
		const UAbilitySystemComponent* System =
			UCataclysmTargeting::AbilitySystemOf(Creature);
		return System
			? System->GetNumericAttribute(
				  UCataclysmVitalAttributeSet::GetHealthAttribute())
			: -1.0f;
	}

	/** What share of its own maximum a creature is on, as the engine computes it. */
	float ShareOf(AActor* Creature)
	{
		const UAbilitySystemComponent* System =
			UCataclysmTargeting::AbilitySystemOf(Creature);
		if (!System)
		{
			return -1.0f;
		}
		return FCataclysmStatConditions::FromHealth(
				   System->GetNumericAttribute(
					   UCataclysmVitalAttributeSet::GetHealthAttribute()),
				   System->GetNumericAttribute(
					   UCataclysmVitalAttributeSet::GetMaxHealthAttribute()))
			.HealthPercent;
	}

	FGameplayTagContainer HealthMelee()
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
			FName(TEXT("Type.Melee"))));
		return Tags;
	}

	FGameplayTagContainer HealthSpell()
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
			FName(TEXT("Type.Spell"))));
		return Tags;
	}

	/**
	 * A stat line carrying one modifier under the target health condition and,
	 * optionally, one that is not conditioned at all.
	 *
	 * THE UNCONDITIONED ONE IS WHAT MAKES THE BUCKETS TELL APART. With nothing
	 * else in the line a 12% increase and a 12% multiplier give the same answer,
	 * so a test carrying only the conditional row would pass whichever bucket the
	 * row had landed in. The authored rows are `increased`: with a 30%
	 * unconditional beside it, an increase gives 1.42/1.30 = 1.092 where a
	 * multiplier would give 1.12.
	 */
	void GiveLine(UCataclysmAbilitySystemComponent* System, const TCHAR* Stat,
				  float Threshold, float ConditionalIncrease,
				  float UnconditionalIncrease)
	{
		FCataclysmStatModifier Conditional;
		Conditional.Bucket = ECataclysmStatBucket::Increased;
		Conditional.Source = ECataclysmModifierSource::PassiveKeystone;
		Conditional.Value = ConditionalIncrease;
		Conditional.Condition =
			ECataclysmStatCondition::TargetHealthBelowPercent;
		Conditional.ConditionValue = Threshold;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
		Line.Base = 100.0f;
		Line.Modifiers = {Conditional};

		if (UnconditionalIncrease != 0.0f)
		{
			FCataclysmStatModifier Always;
			Always.Bucket = ECataclysmStatBucket::Increased;
			Always.Source = ECataclysmModifierSource::PassiveKeystone;
			Always.Value = UnconditionalIncrease;
			Line.Modifiers.Add(Always);
		}

		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** A blow that may not critically strike, so two hits can be compared. */
	FCataclysmHitDelivery HealthNoCritical()
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bCannotCriticallyStrike = true;
		return Delivery;
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is
// built as a unity blob and a `using namespace` at file scope reaches the other
// files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetHealthBelowTest,
	"Cataclysm.TargetHealth.ATargetBelowTheThresholdTakesMoreAndAHealthyOneTakesNormalDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Cornered Quarry, from a real wounded creature to what reached health.
 *
 * TWO TARGETS AND ONE ATTACKER, because a single larger blow would pass against
 * a build that raised every hit.
 *
 * AND BOTH SHARES ARE READ BACK BEFORE THE BLOWS. A creature whose health was
 * not written would make the two alike and every assertion below pass for the
 * wrong reason.
 */
bool FCataclysmTargetHealthBelowTest::RunTest(const FString&)
{
	using namespace CataclysmTargetHealthTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FHealthArmedActor Attacker = MakeHealthArmed(World);
	// 2,000 of 10,000 is 20%, which is below the node's 35%.
	ACataclysmEnemyCharacter* Wounded =
		SpawnAt(World, FVector(2.0f * HealthM, 0.0f, 0.0f), 2'000.0f);
	ACataclysmEnemyCharacter* Healthy =
		SpawnAt(World, FVector(0.0f, 2.0f * HealthM, 0.0f), PoolMax);
	if (!TestNotNull(TEXT("an attacker"), Attacker.Actor)
		|| !TestNotNull(TEXT("a wounded target"), Wounded)
		|| !TestNotNull(TEXT("a healthy target"), Healthy))
	{
		return false;
	}

	// THE NODE'S OWN VALUE AT FULL INVESTMENT -- 2% a point over 6 points is 12%
	// -- WITH AN UNCONDITIONAL 30% BESIDE IT so the bucket is measurable.
	GiveLine(Attacker.AbilitySystem, UCataclysmItemModifiers::AttackDamageStat,
			 /*Threshold=*/35.0f, /*ConditionalIncrease=*/12.0f,
			 /*UnconditionalIncrease=*/30.0f);

	if (!TestEqual(TEXT("the wounded target really is on 20%"),
				   ShareOf(Wounded), 20.0f, 0.01f)
		|| !TestEqual(TEXT("and the healthy one is on 100%"),
					  ShareOf(Healthy), 100.0f, 0.01f))
	{
		return false;
	}

	const auto Strike = [&](ACataclysmEnemyCharacter* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Target, 100.0f,
										 HealthMelee(), HealthNoCritical(),
										 &Resolved);
		return Resolved.DealtToHealth;
	};

	const float OnWounded = Strike(Wounded);
	const float OnHealthy = Strike(Healthy);
	if (!TestTrue(TEXT("both blows landed"), OnWounded > 0.0f && OnHealthy > 0.0f))
	{
		return false;
	}

	TestEqual(*FString::Printf(
				  TEXT("A TARGET BELOW THE THRESHOLD TAKES 1.42/1.30 OF WHAT A "
					   "HEALTHY ONE DOES: %.2f against %.2f"),
				  OnWounded, OnHealthy),
			  OnWounded / OnHealthy, 1.42f / 1.30f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetHealthOnThresholdTest,
	"Cataclysm.TargetHealth.ATargetExactlyOnTheThresholdTakesNormalDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The assertion the whole choice of predicate exists for.
 *
 * THE NODE SAYS "BELOW 35% HEALTH", NOT "AT OR BELOW". A target sitting exactly
 * on the threshold is not below it and earns nothing. This project keeps a
 * strict and an inclusive name for the character's own health because real nodes
 * differ, and its own comment on that pair says a row naming the wrong one is
 * delivered differently from how its sentence reads for exactly one value of
 * health, with nothing reporting it. **This is that one value.**
 *
 * THE BOUNDARY IS EXACTLY REPRESENTABLE AT THESE NUMBERS AND THAT WAS MEASURED.
 * 0.35 is not a binary fraction, so `3500/10000*100` landing exactly on 35 is not
 * something to assume. In float32 it does, because the two roundings cancel. If
 * this test ever fails by a hair rather than by the 12% bonus, that is the thing
 * to re-measure.
 *
 * A THIRD TARGET ONE POINT OF HEALTH LOWER IS THE CONTROL. Without it, a build
 * where the condition never held would pass this test.
 */
bool FCataclysmTargetHealthOnThresholdTest::RunTest(const FString&)
{
	using namespace CataclysmTargetHealthTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FHealthArmedActor Attacker = MakeHealthArmed(World);
	ACataclysmEnemyCharacter* OnIt =
		SpawnAt(World, FVector(2.0f * HealthM, 0.0f, 0.0f), 3'500.0f);
	ACataclysmEnemyCharacter* JustUnder =
		SpawnAt(World, FVector(0.0f, 2.0f * HealthM, 0.0f), 3'499.0f);
	ACataclysmEnemyCharacter* Healthy =
		SpawnAt(World, FVector(-2.0f * HealthM, 0.0f, 0.0f), PoolMax);
	if (!TestNotNull(TEXT("an attacker"), Attacker.Actor)
		|| !TestNotNull(TEXT("a target exactly on the threshold"), OnIt)
		|| !TestNotNull(TEXT("one a point of health under it"), JustUnder)
		|| !TestNotNull(TEXT("and a healthy one"), Healthy))
	{
		return false;
	}

	GiveLine(Attacker.AbilitySystem, UCataclysmItemModifiers::AttackDamageStat,
			 /*Threshold=*/35.0f, /*ConditionalIncrease=*/12.0f,
			 /*UnconditionalIncrease=*/30.0f);

	// THE SHARE THE ENGINE ITSELF COMPUTES, read back before anything is struck.
	// If this is not exactly 35 the rest of the test means nothing, so it fails
	// here with the number rather than later with a ratio.
	const float OnItShare = ShareOf(OnIt);
	if (!TestEqual(*FString::Printf(
					   TEXT("the target on the threshold reads exactly 35, and "
							"it reads %.6f"),
					   OnItShare),
				   OnItShare, 35.0f, 0.0001f))
	{
		return false;
	}

	const auto Strike = [&](ACataclysmEnemyCharacter* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Target, 100.0f,
										 HealthMelee(), HealthNoCritical(),
										 &Resolved);
		return Resolved.DealtToHealth;
	};

	const float OnThreshold = Strike(OnIt);
	const float Under = Strike(JustUnder);
	const float Full = Strike(Healthy);
	if (!TestTrue(TEXT("all three blows landed"),
				  OnThreshold > 0.0f && Under > 0.0f && Full > 0.0f))
	{
		return false;
	}

	TestEqual(*FString::Printf(
				  TEXT("A TARGET EXACTLY ON THE THRESHOLD TAKES WHAT A HEALTHY "
					   "ONE DOES, because the node says BELOW: %.2f against "
					   "%.2f"),
				  OnThreshold, Full),
			  OnThreshold, Full, 0.01f);

	// THE CONTROL: one point of health lower and the bonus does apply.
	TestEqual(*FString::Printf(
				  TEXT("AND ONE POINT OF HEALTH UNDER IT, THE BONUS APPLIES: "
					   "%.2f against %.2f"),
				  Under, Full),
			  Under / Full, 1.42f / 1.30f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetHealthSpellTest,
	"Cataclysm.TargetHealth.ASpellEarnsItTooBecauseItIsItsOwnLookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Broken Will, which is a SPELL damage node at a different threshold.
 *
 * A SEPARATE LOOKUP FROM ATTACK DAMAGE, which is why it needs its own test.
 * `UCataclysmSkillEffects::SpellDamageOf` asks for `spell_damage` through its
 * own call and receives its own copy of the per-blow facts, so a reading can
 * reach one and not the other. Testing only the attack node would leave half the
 * change unproven.
 */
bool FCataclysmTargetHealthSpellTest::RunTest(const FString&)
{
	using namespace CataclysmTargetHealthTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FHealthArmedActor Caster = MakeHealthArmed(World);
	// 4,000 of 10,000 is 40%, below Broken Will's half.
	ACataclysmEnemyCharacter* Wounded =
		SpawnAt(World, FVector(2.0f * HealthM, 0.0f, 0.0f), 4'000.0f);
	ACataclysmEnemyCharacter* Healthy =
		SpawnAt(World, FVector(0.0f, 2.0f * HealthM, 0.0f), PoolMax);
	if (!TestNotNull(TEXT("a caster"), Caster.Actor)
		|| !TestNotNull(TEXT("a wounded target"), Wounded)
		|| !TestNotNull(TEXT("a healthy target"), Healthy))
	{
		return false;
	}

	Caster.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetSpellDamageAttribute(), 100.0f);
	GiveLine(Caster.AbilitySystem, TEXT("spell_damage"), /*Threshold=*/50.0f,
			 /*ConditionalIncrease=*/12.0f, /*UnconditionalIncrease=*/30.0f);

	if (!TestEqual(TEXT("the wounded target really is on 40%"),
				   ShareOf(Wounded), 40.0f, 0.01f))
	{
		return false;
	}

	const auto Cast = [&](ACataclysmEnemyCharacter* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Caster.Actor, Target, 100.0f,
										 HealthSpell(), HealthNoCritical(),
										 &Resolved);
		return Resolved.DealtToHealth;
	};

	const float OnWounded = Cast(Wounded);
	const float OnHealthy = Cast(Healthy);
	if (!TestTrue(TEXT("both spells landed"),
				  OnWounded > 0.0f && OnHealthy > 0.0f))
	{
		return false;
	}

	TestTrue(*FString::Printf(
				 TEXT("A SPELL DEALS MORE TO A TARGET BELOW HALF HEALTH: %.2f "
					  "against %.2f"),
				 OnWounded, OnHealthy),
			 OnWounded > OnHealthy);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetHealthMinionTest,
	"Cataclysm.TargetHealth.AMinionsBlowEarnsTheSummonersHealthBonusNothingAtAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A minion strikes with its summoner as the attacker, so every attacker-side
 * reading reaches its blow unless it is stopped.
 *
 * THE EXCLUSION IS THE ONE THAT ALREADY EXISTS AND THIS ADDS NO CODE TO IT.
 * `ApplyHit` passes a null target behind
 * `FCataclysmHitDelivery::bCarriesNoTargetState`, and a null target leaves the
 * health reading negative. This test is here because "it needs no new code" is a
 * claim about behaviour and behaviour is what a test is for.
 *
 * THE CONTROL IS THE SUMMONER'S OWN BLOW. Without it a test asserting "no bonus"
 * would also pass if the bonus were broken for everybody.
 */
bool FCataclysmTargetHealthMinionTest::RunTest(const FString&)
{
	using namespace CataclysmTargetHealthTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FHealthArmedActor Summoner = MakeHealthArmed(World);
	if (!TestNotNull(TEXT("a summoner"), Summoner.Actor))
	{
		return false;
	}
	GiveLine(Summoner.AbilitySystem, UCataclysmItemModifiers::AttackDamageStat,
			 /*Threshold=*/35.0f, /*ConditionalIncrease=*/12.0f,
			 /*UnconditionalIncrease=*/0.0f);

	// A REAL IMP, NAMED, SINCE ISSUE #1515. This case used to spawn a minion
	// with no type row, which swung for 30% of its SUMMONER'S weapon damage.
	// The project owner ruled that a bug on 2026-09-17 and the share is
	// deleted, so a typeless minion now deals nothing and neither blow below
	// would land. What the figure is does not matter here; that there is one
	// does.
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Summoner.Actor, FVector(1.0f * HealthM, 0.0f, 0.0f), /*Lifetime=*/20.0f,
		/*bBurns=*/false, /*TypeName=*/TEXT("Imp"));
	ACataclysmEnemyCharacter* Wounded =
		SpawnAt(World, FVector(2.0f * HealthM, 0.0f, 0.0f), 2'000.0f);
	ACataclysmEnemyCharacter* Healthy =
		SpawnAt(World, FVector(0.0f, 2.0f * HealthM, 0.0f), PoolMax);
	if (!TestNotNull(TEXT("a minion"), Imp)
		|| !TestNotNull(TEXT("a wounded creature"), Wounded)
		|| !TestNotNull(TEXT("and a healthy one"), Healthy))
	{
		return false;
	}

	// THE CONTROL: the summoner's OWN blow does earn the bonus.
	const auto SummonerStrikes = [&](ACataclysmEnemyCharacter* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Summoner.Actor, Target, 100.0f,
										 HealthMelee(), HealthNoCritical(),
										 &Resolved);
		return Resolved.DealtToHealth;
	};
	const float OwnOnWounded = SummonerStrikes(Wounded);
	const float OwnOnHealthy = SummonerStrikes(Healthy);
	if (!TestTrue(TEXT("the summoner's own blows landed"),
				  OwnOnWounded > 0.0f && OwnOnHealthy > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("and the summoner DOES earn the bonus on a wounded target"),
			  OwnOnWounded / OwnOnHealthy, 1.12f, 0.01f);

	// AND THE MINION'S BLOW EARNS NONE OF IT. Its damage is its own, so it is
	// compared against its own blow on a healthy target rather than against the
	// summoner's figure.
	const auto MinionStrikes = [](ACataclysmMinion* Striker,
								  ACataclysmEnemyCharacter* Target)
	{
		const float Before = HealthOf(Target);
		Striker->AttackTarget(Target);
		return Before - HealthOf(Target);
	};

	const float MinionOnWounded = MinionStrikes(Imp, Wounded);
	const float MinionOnHealthy = MinionStrikes(Imp, Healthy);
	if (!TestTrue(TEXT("both of the minion's blows landed"),
				  MinionOnWounded > 0.0f && MinionOnHealthy > 0.0f))
	{
		return false;
	}
	TestEqual(*FString::Printf(
				  TEXT("A MINION DEALS THE SAME TO A WOUNDED TARGET AS TO A "
					   "HEALTHY ONE, so it earned none of the summoner's bonus: "
					   "%.4f against %.4f"),
				  MinionOnWounded, MinionOnHealthy),
			  MinionOnWounded, MinionOnHealthy, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetHealthNoTargetTest,
	"Cataclysm.TargetHealth.ALookupWithNoTargetInHandEarnsNoneOfIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A character sheet has no blow and no target, and must be refused.
 *
 * THE CONTROL IS THE SECOND ASSERTION AND IT IS WHAT MAKES THE FIRST MEAN
 * ANYTHING. A build in which the condition never held would pass the first. The
 * unconditional increase beside it has to still arrive, or this test would also
 * pass on a build that dropped every modifier.
 */
bool FCataclysmTargetHealthNoTargetTest::RunTest(const FString&)
{
	using namespace CataclysmTargetHealthTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FHealthArmedActor Asking = MakeHealthArmed(World);
	if (!TestNotNull(TEXT("a character"), Asking.Actor))
	{
		return false;
	}
	GiveLine(Asking.AbilitySystem, UCataclysmItemModifiers::AttackDamageStat,
			 /*Threshold=*/35.0f, /*ConditionalIncrease=*/12.0f,
			 /*UnconditionalIncrease=*/30.0f);

	// NO TARGET PASSED, WHICH IS WHAT A CHARACTER SHEET DOES. The reading is the
	// sum of increases as a fraction, so 0.30 is the unconditional row alone and
	// 0.42 would mean the conditional one had held with no target at all.
	const float Increases = Asking.AbilitySystem->AttackDamageIncreasesForSkill(
		FGameplayTagContainer());
	TestEqual(*FString::Printf(
				  TEXT("A LOOKUP WITH NO TARGET GETS THE UNCONDITIONAL 30%% AND "
					   "NOT THE HEALTH ROW: %.4f"),
				  Increases),
			  Increases, 0.30f, 0.001f);

	// AND THE SAME LOOKUP WITH A WOUNDED TARGET DOES GET IT.
	ACataclysmEnemyCharacter* Wounded =
		SpawnAt(World, FVector(2.0f * HealthM, 0.0f, 0.0f), 2'000.0f);
	if (!TestNotNull(TEXT("a wounded target"), Wounded))
	{
		return false;
	}

	const float WithTarget = Asking.AbilitySystem->AttackDamageIncreasesForSkill(
		FGameplayTagContainer(), /*SkillHealthCostPercent=*/-1.0f,
		/*MetresMovedBeforeBlow=*/-1.0f, /*TargetDistanceMetres=*/-1.0f,
		/*bTargetIsStaggered=*/false, Wounded);
	TestEqual(*FString::Printf(
				  TEXT("AND THE SAME LOOKUP WITH A WOUNDED TARGET GETS BOTH: "
					   "%.4f"),
				  WithTarget),
			  WithTarget, 0.42f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetHealthUnreadableTest,
	"Cataclysm.TargetHealth.ATargetWhoseHealthCannotBeReadEarnsNoneOfIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A target that exists and has no health, which is not the same case as no
 * target at all.
 *
 * A BARE ACTOR IS A REAL CASE AND NOT A CONTRIVANCE. A patch of burning ground,
 * a projectile and a piece of terrain are all actors and none of them has an
 * ability system to read health from.
 *
 * IT REFUSES, AND THE STAGGER CEILING DELIBERATELY DOES NOT. `ApplyStagger`
 * leaves a target whose health cannot be read staggerable, in its own words
 * "because refusing on an unknown would make the row stronger than it says."
 * **Same rule, opposite direction**: that one is a drawback, so refusing would
 * widen it; this is a bonus, so granting would widen it. This test pins the
 * direction that belongs to a bonus.
 */
bool FCataclysmTargetHealthUnreadableTest::RunTest(const FString&)
{
	using namespace CataclysmTargetHealthTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FHealthArmedActor Asking = MakeHealthArmed(World);
	AActor* Bare = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("a character"), Asking.Actor)
		|| !TestNotNull(TEXT("an actor with no ability system"), Bare))
	{
		return false;
	}
	if (!TestNull(TEXT("and it really has none"),
				  UCataclysmTargeting::AbilitySystemOf(Bare)))
	{
		return false;
	}

	GiveLine(Asking.AbilitySystem, UCataclysmItemModifiers::AttackDamageStat,
			 /*Threshold=*/35.0f, /*ConditionalIncrease=*/12.0f,
			 /*UnconditionalIncrease=*/30.0f);

	const float Increases = Asking.AbilitySystem->AttackDamageIncreasesForSkill(
		FGameplayTagContainer(), /*SkillHealthCostPercent=*/-1.0f,
		/*MetresMovedBeforeBlow=*/-1.0f, /*TargetDistanceMetres=*/-1.0f,
		/*bTargetIsStaggered=*/false, Bare);

	TestEqual(*FString::Printf(
				  TEXT("A TARGET WHOSE HEALTH CANNOT BE READ EARNS THE "
					   "UNCONDITIONAL 30%% AND NOT THE HEALTH ROW: %.4f"),
				  Increases),
			  Increases, 0.30f, 0.001f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
