// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A summoner's increased minion damage and minion health reaching its minions.
 * Issue #898.
 *
 * WHAT WAS WRONG. `game/Data/Affixes.csv` grants `minion_damage` and
 * `minion_health`, and no gameplay attribute for any minion stat exists anywhere
 * in the project. The two passes of `UCataclysmPlayerClassStats::ApplyTo` that
 * write attributes are both driven by the stat-to-attribute map, so neither stat
 * was resolved by anything and neither reached anything. A player who found
 * either affix got no benefit and no message.
 *
 * HALF OF THAT IS ALREADY FIXED AND IT IS THE HALF NOBODY CAN SEE. A third pass
 * now RECORDS all three minion stats into the character's stat inputs without
 * writing any attribute, because there is none to write. Nothing read the two
 * this file is about until now, so they were recorded and dropped.
 *
 * THIS IS NOT A FOURTH CHANNEL. The decision of 2026-08-06 lists three channels
 * and then says "Everything else is blocked unless a modifier names minions",
 * and the owner's reversal it records has three parts of which the third is
 * "minion affixes exist on gear on top of that". `Cataclysm.MinionStats` holds
 * the other half of that rule -- that a summoner's GENERAL increases still do
 * not reach a minion -- and the two files have to be read together.
 *
 * DAMAGE IS LIVE AND HEALTH IS FROZEN, which is why the two health cases below
 * are written in opposite orders: one grants the gear before the summoning and
 * one after. Damage is read at every blow, so a gear change shows on the next
 * swing. Health is a pool written once at the summoning, so it does not.
 *
 * THE ASYMMETRY IS THE ONE THE DESIGN ALREADY PREDICTED. The decision of
 * 2026-09-13, section "Minions update live rather than snapshotting", records
 * the owner's ruling that minions should update "anytime gear/passives/skills
 * change", calls it "Not built, and not urgent by the same ruling", and says
 * exactly where it bites: "A per-swing read is live by construction, so this
 * constrains health, which is set once at spawn, and not a stat asked for at the
 * moment of a blow." So this is pinned here rather than left to be discovered.
 *
 * EVERY EXPECTED FIGURE IS COMPUTED IN THIS FILE rather than asked of the engine.
 * A test that asks the thing under test for the expected answer agrees with it
 * however wrong it is.
 */
namespace CataclysmMinionGearTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	/** Centimetres in a metre, so a case can place a minion in metres. */
	constexpr float M = 100.0f;

	//~ The authored figures, from `game/Data/MinionTypes.csv`, written out for the
	//~ reason above. `Cataclysm.DataTable` pins the table itself, so a row changed
	//~ underneath these is caught there and named.
	constexpr float ImpBaseDamage = 10.5f;
	constexpr float ImpDamagePerLevel = 10.5f;
	constexpr float ImpBaseHealth = 200.0f;
	constexpr float ImpHealthPerLevel = 90.0f;

	/** What the tests grant. Not any affix's top roll, so a reading that matched
	 *  one could only have come from the data rather than from this line. */
	constexpr float DamageIncreasePercent = 25.0f;
	constexpr float HealthIncreasePercent = 50.0f;
	constexpr float ReductionPercent = -40.0f;

	/** The summoner's weapon, large enough that the old share of it -- 30%, which
	 *  a typeless minion still deals -- cannot be confused with an imp's own
	 *  figure by arithmetic coincidence. */
	constexpr float SummonerWeapon = 1000.0f;

	/**
	 * The health a target is given, and it is SMALL ON PURPOSE.
	 *
	 * A BLOW IS MEASURED AS THE DIFFERENCE OF TWO HEALTH READINGS, so the pool
	 * size decides how finely a blow can be read at all. A `float` near
	 * 1,000,000 steps in units of 0.0625: every value between is rounded to one
	 * of them, so a difference taken there cannot resolve anything smaller.
	 *
	 * THAT IS NOT HYPOTHETICAL. This file first used 1,000,000, copied from the
	 * fixture beside it, and the reduction case failed by 0.0125 -- an imp's
	 * 220.5 reduced by 40% is 132.300003, and a difference taken near 1,000,000
	 * reads it as 132.3125. The code was right and the ruler was too coarse.
	 *
	 * WHY THE OTHER CASES PASSED ANYWAY, which is the part worth knowing: 220.5,
	 * 275.625 and 0 are all exact multiples of 0.0625, so they survive the
	 * rounding untouched. A test whose expected figure happens to land on a
	 * representable point passes for a reason that has nothing to do with what it
	 * checks. `CataclysmMinionOwnStatsTests.cpp` still measures against
	 * 1,000,000 and passes for exactly that reason.
	 *
	 * 10,000 STEPS IN UNITS OF 0.0009766, ten times inside the 0.01 tolerance
	 * these cases ask for. The worst error any figure in this file actually
	 * suffers at that pool is 0.0002, because a rounding error is at most half a
	 * step and these values do not sit at the worst point -- so the margin in
	 * practice is fifty-fold and the margin guaranteed by the step alone is
	 * tenfold. The tenfold one is the number to design against.
	 *
	 * AND IT IS STILL LARGE ENOUGH: the two largest blows here take 551 of it, so
	 * nothing approaches death and a reading is the blow rather than the health
	 * that was left.
	 */
	constexpr float TargetHealthPool = 10'000.0f;

	float RaisedByLevel(float Base, float PerLevel, int32 Level)
	{
		return Base + PerLevel * static_cast<float>(Level);
	}

	/**
	 * The level these tests will see.
	 *
	 * NOT A GUESS AND NOT A CONSTANT. A summoner built here is an ordinary actor
	 * with no player state, so the minion falls back to the level the class stats
	 * are being previewed at -- the path every non-player summoner takes.
	 */
	int32 LevelTheseTestsSee()
	{
		return UCataclysmPlayerClassStats::ChosenLevel();
	}

	/**
	 * A bare actor carrying the attributes a blow needs at both ends.
	 *
	 * THE FOUR SETS ARE COPIED FROM `CataclysmMinionOwnStatsTests.cpp`, which
	 * copied them from a fixture known to work. Nothing here mitigates a blow, so
	 * a reading is the whole figure that was swung.
	 *
	 * THE HEALTH POOL IS THE ONE THING THAT DIFFERS FROM THAT FIXTURE, and it
	 * differs because a case here failed against its figure. `TargetHealthPool`
	 * has the measurement.
	 */
	struct FScopedFighter
	{
		FScopedFighter(UWorld* World, float AttackDamage)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template and
			// a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAllResist =
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAllResist);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			// LARGE ENOUGH THAT NOTHING HERE APPROACHES DEATH AND SMALL ENOUGH
			// THAT A BLOW CAN BE READ, which are opposing requirements.
			// `TargetHealthPool` records the arithmetic behind the number and the
			// measurement that forced it.
			AbilitySystem->SetNumericAttributeBase(
				Vital::GetMaxHealthAttribute(), TargetHealthPool);
			AbilitySystem->SetNumericAttributeBase(
				Vital::GetHealthAttribute(), TargetHealthPool);
			AbilitySystem->SetNumericAttributeBase(
				Combat::GetAttackDamageAttribute(), AttackDamage);
		}

		~FScopedFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		float Health() const
		{
			const UAbilitySystemComponent* System =
				UCataclysmTargeting::AbilitySystemOf(Actor);
			return System
				? System->GetNumericAttribute(Vital::GetHealthAttribute())
				: 0.0f;
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * Give a character the stat line a player would have after equipping gear that
	 * grants one increased minion stat.
	 *
	 * THROUGH `SetStatInputs`, WHICH IS THE ROUTE THE REAL ONE TAKES.
	 * `UCataclysmPlayerClassStats::ApplyTo` fills that map from gear and passives
	 * and hands it across whole. Writing an attribute instead would test nothing:
	 * these stats HAVE no attribute, which is the entire problem.
	 *
	 * IT REPLACES THE WHOLE MAP, so a caller granting two stats has to pass both
	 * at once. Every case below grants one.
	 */
	void GrantMinionStat(AActor* Summoner, const TCHAR* Stat, float Percent)
	{
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Summoner));
		if (!System)
		{
			return;
		}

		FCataclysmStatModifier Increase;
		Increase.Bucket = ECataclysmStatBucket::Increased;
		Increase.Source = ECataclysmModifierSource::GearAffix;
		Increase.Value = Percent;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));

		// A BASE OF NOTHING, DELIBERATELY, BECAUSE THAT IS THE REAL CASE. Neither
		// stat has a base and neither can have one: a minion's damage and health
		// come from its own row. Giving one a base here would make these tests
		// pass against an accessor that reads the stat's VALUE, which is the
		// mistake the whole design avoids.
		Line.Base = 0.0f;
		Line.Modifiers = {Increase};
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** A minion's maximum health, read the way anything else reads a character's. */
	float MaxHealthOf(const ACataclysmMinion* Minion)
	{
		const UAbilitySystemComponent* System =
			UCataclysmTargeting::AbilitySystemOf(Minion);
		return System
			? System->GetNumericAttribute(Vital::GetMaxHealthAttribute())
			: -1.0f;
	}

	/**
	 * Summon an imp and assert it really came from the Imp row.
	 *
	 * THE PRECONDITION IS NOT OPTIONAL. A minion whose row could not be found
	 * keeps the defaults AND falls back to dealing a share of the summoner's
	 * weapon, so a stale imported asset would fail every reading below for a
	 * reason that has nothing to do with this rule.
	 */
	ACataclysmMinion* SummonImp(FAutomationTestBase& Test, UWorld* World,
								AActor* Summoner)
	{
		ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
			Summoner, FVector(1 * M, 0, 0), /*Lifetime=*/20.0f,
			/*bBurns=*/false, TEXT("Imp"));
		if (!Test.TestNotNull(TEXT("an imp"), Imp))
		{
			return nullptr;
		}

		if (Imp->TypeName != FString(TEXT("Imp")))
		{
			Test.AddError(TEXT("DT_MinionTypes could not supply the Imp row, so "
							   "this test cannot tell a scaled figure from the "
							   "fallback share. Run "
							   "tools/generate_datatable_assets.py."));
			return nullptr;
		}
		return Imp;
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is
// built as a unity blob and a `using namespace` at file scope reaches the other
// files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionGearDamageTest,
	"Cataclysm.MinionGear.AMinionHitsHarderForItsSummonersIncreasedMinionDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The first half: increased minion damage reaches the blow.
 *
 * THE CONTROL IS INSIDE THIS TEST RATHER THAN BESIDE IT. The same imp is measured
 * before the gear is granted and after, so a build where a minion deals nothing
 * at all fails the first reading and never reaches the second. A "does not
 * happen" test cannot tell a bonus correctly excluded from a feature that never
 * fired, and the first reading is a specific number for that reason.
 */
bool FCataclysmMinionGearDamageTest::RunTest(const FString&)
{
	using namespace CataclysmMinionGearTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Summoner(World, SummonerWeapon);
	FScopedFighter Target(World, /*AttackDamage=*/0.0f);

	ACataclysmMinion* Imp = SummonImp(*this, World, Summoner.Actor);
	if (!Imp)
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	const float ItsOwn =
		RaisedByLevel(ImpBaseDamage, ImpDamagePerLevel, LevelTheseTestsSee());

	const float Before = Target.Health();
	Imp->AttackTarget(Target.Actor);
	TestEqual(TEXT("with no such gear it deals its own figure"),
			  Before - Target.Health(), ItsOwn, 0.01f);

	GrantMinionStat(Summoner.Actor, TEXT("minion_damage"), DamageIncreasePercent);

	const float BeforeGeared = Target.Health();
	Imp->AttackTarget(Target.Actor);
	TestEqual(TEXT("and its summoner's increased minion damage raises it"),
			  BeforeGeared - Target.Health(),
			  ItsOwn * (1.0f + DamageIncreasePercent / 100.0f), 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionGearHealthTest,
	"Cataclysm.MinionGear.AMinionIsSummonedToughedForItsSummonersIncreasedMinionHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The second half: increased minion health reaches the pool.
 *
 * THE GEAR IS GRANTED BEFORE THE SUMMONING, which is the only order that can
 * work, because health is written once when the minion is made. The test after
 * this one is the other order, and it asserts the opposite.
 *
 * TWO IMPS FROM TWO SUMMONERS RATHER THAN ONE IMP MEASURED TWICE. A pool cannot
 * be re-read after a change the way a blow can, so the control has to be a
 * second minion summoned in the same world by a character without the gear.
 */
bool FCataclysmMinionGearHealthTest::RunTest(const FString&)
{
	using namespace CataclysmMinionGearTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Plain(World, SummonerWeapon);
	FScopedFighter Geared(World, SummonerWeapon);
	GrantMinionStat(Geared.Actor, TEXT("minion_health"), HealthIncreasePercent);

	ACataclysmMinion* PlainImp = SummonImp(*this, World, Plain.Actor);
	ACataclysmMinion* GearedImp = SummonImp(*this, World, Geared.Actor);
	if (!PlainImp || !GearedImp)
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(PlainImp)) { PlainImp->Destroy(); } };
	ON_SCOPE_EXIT { if (IsValid(GearedImp)) { GearedImp->Destroy(); } };

	const float ItsOwn =
		RaisedByLevel(ImpBaseHealth, ImpHealthPerLevel, LevelTheseTestsSee());

	TestEqual(TEXT("a minion of a summoner with no such gear has its own health"),
			  MaxHealthOf(PlainImp), ItsOwn, 0.01f);

	TestEqual(TEXT("and its summoner's increased minion health raises it"),
			  MaxHealthOf(GearedImp),
			  ItsOwn * (1.0f + HealthIncreasePercent / 100.0f), 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionGearHealthIsSnapshotTest,
	"Cataclysm.MinionGear.HealthIsTakenAtTheSummoningAndLaterGearDoesNotChangeIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The snapshot, pinned deliberately rather than left to be discovered.
 *
 * THIS TEST ASSERTS A LIMITATION, NOT A FEATURE, AND SAYS SO. Health is a pool:
 * the maximum is written to an attribute when the minion is made, and nothing
 * re-runs that when equipment or passives change. Damage and attack speed are
 * both read fresh, so this is the one minion figure that does not follow a gear
 * change.
 *
 * IT IS DEFERRED BY A RULING RATHER THAN MISSED. The decision of 2026-09-13,
 * section "Minions update live rather than snapshotting", records it as "Not
 * built, and not urgent by the same ruling", and names health as the case a
 * per-swing read does not cover.
 *
 * WHEN THE REFRESH IS BUILT, THIS TEST SHOULD FAIL AND BE REPLACED. That is the
 * point of writing it: a change that starts updating a live minion's health will
 * be told by this test that it has changed something, rather than quietly
 * altering behaviour nobody had recorded.
 *
 * IT CANNOT PASS BY THE FEATURE BEING ABSENT. The first reading is the geared
 * figure from the test above, so a build where minion health never scaled at all
 * fails here before reaching the claim about snapshotting.
 */
bool FCataclysmMinionGearHealthIsSnapshotTest::RunTest(const FString&)
{
	using namespace CataclysmMinionGearTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Summoner(World, SummonerWeapon);
	GrantMinionStat(Summoner.Actor, TEXT("minion_health"), HealthIncreasePercent);

	ACataclysmMinion* Imp = SummonImp(*this, World, Summoner.Actor);
	if (!Imp)
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	const float ItsOwn =
		RaisedByLevel(ImpBaseHealth, ImpHealthPerLevel, LevelTheseTestsSee());
	const float Summoned = ItsOwn * (1.0f + HealthIncreasePercent / 100.0f);

	if (!TestEqual(TEXT("it was summoned with the geared figure"),
				   MaxHealthOf(Imp), Summoned, 0.01f))
	{
		return false;
	}

	// THE GEAR COMES OFF, AND THE MINION KEEPS WHAT IT WAS SUMMONED WITH.
	{
		TMap<FName, FCataclysmStatInputs> Nothing;
		Summoner.AbilitySystem->SetStatInputs(MoveTemp(Nothing));
	}

	TestEqual(TEXT("and taking the gear off does not change it, because health "
				   "is taken once at the summoning"),
			  MaxHealthOf(Imp), Summoned, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionGearReductionTest,
	"Cataclysm.MinionGear.AReductionMakesAMinionWeakerRatherThanBeingIgnored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A negative increase is kept.
 *
 * WHY THIS CAN HAPPEN. Ten rows of `game/Data/PassiveEffects.csv` already carry a
 * negative `ValuePerPoint`, so a node that reduces minion damage is something the
 * data can express today.
 *
 * WHAT IT GUARDS. Clamping the increases at zero would read as harmless and would
 * discard a designed drawback in silence -- the failure would be a node that says
 * it weakens your minions and does not. The engine floors the MULTIPLIER instead,
 * which only stops a figure below -100% turning into negative damage.
 */
bool FCataclysmMinionGearReductionTest::RunTest(const FString&)
{
	using namespace CataclysmMinionGearTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Summoner(World, SummonerWeapon);
	FScopedFighter Target(World, /*AttackDamage=*/0.0f);

	ACataclysmMinion* Imp = SummonImp(*this, World, Summoner.Actor);
	if (!Imp)
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	const float ItsOwn =
		RaisedByLevel(ImpBaseDamage, ImpDamagePerLevel, LevelTheseTestsSee());

	GrantMinionStat(Summoner.Actor, TEXT("minion_damage"), ReductionPercent);

	const float Before = Target.Health();
	Imp->AttackTarget(Target.Actor);
	const float Reduced = Before - Target.Health();

	TestEqual(TEXT("a negative increase lowers the blow"), Reduced,
			  ItsOwn * (1.0f + ReductionPercent / 100.0f), 0.01f);

	// AND STATED AS ITS OWN READING, so a build that clamped the reduction away
	// fails with the rule named rather than with a number that looks close.
	TestTrue(TEXT("and it is not simply ignored"), Reduced < ItsOwn - 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionGearNoFallbackTest,
	"Cataclysm.MinionGear.AMinionStrippedOfItsDamageDoesNotFallBackToItsSummonersWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The mistake this test exists for is one line away from the change it guards.
 *
 * `AttackTarget` CHOOSES BETWEEN TWO RULES on whether the minion has a damage
 * figure of its own. A minion with one deals it; a minion without deals a share
 * of its summoner's weapon, which is what the whole design of 2026-08-06
 * reversed. Writing the branch as "if the SCALED damage is above zero" instead of
 * "if the TYPE ROW'S figure is above zero" would send a minion reduced to nothing
 * down the fallback -- and the fallback pays 30% of a weapon, so the minion would
 * suddenly hit very much HARDER for having its damage removed.
 *
 * 300 AGAINST 0 IS WHY THE SUMMONER'S WEAPON IS 1000. The two answers cannot be
 * confused.
 */
bool FCataclysmMinionGearNoFallbackTest::RunTest(const FString&)
{
	using namespace CataclysmMinionGearTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Summoner(World, SummonerWeapon);
	FScopedFighter Target(World, /*AttackDamage=*/0.0f);

	ACataclysmMinion* Imp = SummonImp(*this, World, Summoner.Actor);
	if (!Imp)
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	GrantMinionStat(Summoner.Actor, TEXT("minion_damage"), -100.0f);

	const float Before = Target.Health();
	Imp->AttackTarget(Target.Actor);
	const float Dealt = Before - Target.Health();

	TestEqual(TEXT("it deals nothing"), Dealt, 0.0f, 0.01f);

	// THE SHARP HALF. Named separately so a failure says which rule was broken.
	TestTrue(TEXT("and above all it does not deal a share of its summoner's "
				  "weapon instead"),
			 Dealt < SummonerWeapon * 0.3f - 0.01f);

	return true;
}


// ---------------------------------------------------------------------------
// Set Upon and Set the Pack On: a minion's blow against an enemy its summoner
// damaged in the last 2 seconds. Issue #1515.
// ---------------------------------------------------------------------------

namespace CataclysmMinionGearTest
{
	/**
	 * `minion_damage` on a summoner, as the two rows grant it: an increase and a
	 * "more", both under `target_damaged_by_you_within_seconds` at 2. Through
	 * `SetStatInputs`, for the reason `GrantMinionStat` gives.
	 */
	void GrantDamagedByYouRows(AActor* Summoner, float IncreasePercent,
							   float MorePercent)
	{
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Summoner));
		if (!System)
		{
			return;
		}

		FCataclysmStatModifier Increase;
		Increase.Bucket = ECataclysmStatBucket::Increased;
		Increase.Source = ECataclysmModifierSource::PassiveKeystone;
		Increase.Value = IncreasePercent;
		Increase.Condition = ECataclysmStatCondition::TargetDamagedByYouWithinSeconds;
		Increase.ConditionValue = 2.0f;

		FCataclysmStatModifier More = Increase;
		More.Bucket = ECataclysmStatBucket::More;
		More.Value = MorePercent;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(TEXT("minion_damage")));
		Line.Base = 0.0f;
		Line.Modifiers = {Increase, More};
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** One swing of `Imp` at `Target`, read as the health it took. */
	float SwingAt(ACataclysmMinion* Imp, const FScopedFighter& Target)
	{
		const float Before = Target.Health();
		Imp->AttackTarget(Target.Actor);
		return Before - Target.Health();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionGearDamagedByYouTest,
	"Cataclysm.MinionGear.AMinionHitsHarderOnlyAgainstAnEnemyItsSummonerDamagedInTheLastTwoSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Set Upon and Set the Pack On in the shape their rows take. Issue #1515.
 *
 * THE SUMMONER'S BLOW GOES THROUGH THE PATH EVERY BLOW TAKES, so the record is
 * written where the game writes it rather than by hand.
 *
 * 16% INCREASED AND 25% MORE ARE 1.45, and added into one sum they would be
 * 1.41, so a build that put the "more" row among the increases fails here, and
 * so does one that dropped it.
 *
 * THE IMP'S OWN BLOWS ARE NOT ITS SUMMONER'S, the owner's ruling of 2026-09-17:
 * a second swing at the enemy nobody else struck is still its own figure,
 * although the imp itself struck it a moment before. Ruled for this condition
 * on 2026-09-23 under the owner's delegation.
 */
bool FCataclysmMinionGearDamagedByYouTest::RunTest(const FString&)
{
	using namespace CataclysmMinionGearTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Summoner(World, SummonerWeapon);
	FScopedFighter Struck(World, /*AttackDamage=*/0.0f);
	FScopedFighter Untouched(World, /*AttackDamage=*/0.0f);

	ACataclysmMinion* Imp = SummonImp(*this, World, Summoner.Actor);
	if (!Imp)
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	GrantDamagedByYouRows(Summoner.Actor, 16.0f, 25.0f);
	const float ItsOwn =
		RaisedByLevel(ImpBaseDamage, ImpDamagePerLevel, LevelTheseTestsSee());

	UCataclysmSkillEffects::ApplyDirectDamage(Summoner.Actor, Struck.Actor, 1.0f);
	if (!TestTrue(TEXT("the summoner's blow is on the struck enemy's record"),
				  Struck.AbilitySystem->SecondsSinceStruckBy(
					  Summoner.AbilitySystem) >= 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("against an enemy its summoner just damaged: 16% increased and 25% more"),
			  SwingAt(Imp, Struck), ItsOwn * 1.16f * 1.25f, 0.01f);
	TestEqual(TEXT("against one its summoner never damaged: its own figure"),
			  SwingAt(Imp, Untouched), ItsOwn, 0.01f);
	TestEqual(TEXT("and still its own after the imp struck it, since the imp's blows are its own"),
			  SwingAt(Imp, Untouched), ItsOwn, 0.01f);

	World->TimeSeconds += 2.5f;
	TestEqual(TEXT("and against the first, 2.5 seconds later: its own figure"),
			  SwingAt(Imp, Struck), ItsOwn, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionGearSummonClockTest,
	"Cataclysm.MinionGear.ARowOnTheSecondsAfterASummonStillReachesAMinionsBlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The enchantment "Summoned minions deal 30%-50% increased damage for 5 seconds
 * after being summoned" (`EnchantmentEffects.csv`, `seconds_after_summon`
 * at 5) through the minion's new target-aware multiplier. Issue #1515.
 *
 * WHY THIS IS PINNED. Before Set Upon the minion read its summoner's
 * increases with the summoner's state; it now reads them with a target added.
 * The row is unchanged only while that state is still built on the SUMMONER's
 * component, whose summon clock this row reads. A build that asked the minion's
 * own component would read a clock no summon ever stamps, and the row would
 * grant nothing, with no error anywhere.
 */
bool FCataclysmMinionGearSummonClockTest::RunTest(const FString&)
{
	using namespace CataclysmMinionGearTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Summoner(World, SummonerWeapon);
	FScopedFighter Target(World, /*AttackDamage=*/0.0f);

	ACataclysmMinion* Imp = SummonImp(*this, World, Summoner.Actor);
	if (!Imp)
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	FCataclysmStatModifier AfterSummon;
	AfterSummon.Bucket = ECataclysmStatBucket::Increased;
	AfterSummon.Source = ECataclysmModifierSource::Enchantment;
	AfterSummon.Value = 40.0f;
	AfterSummon.Condition = ECataclysmStatCondition::WithinSecondsOfSummon;
	AfterSummon.ConditionValue = 5.0f;

	TMap<FName, FCataclysmStatInputs> Inputs;
	FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(TEXT("minion_damage")));
	Line.Base = 0.0f;
	Line.Modifiers = {AfterSummon};
	Summoner.AbilitySystem->SetStatInputs(MoveTemp(Inputs));

	const float ItsOwn =
		RaisedByLevel(ImpBaseDamage, ImpDamagePerLevel, LevelTheseTestsSee());

	TestEqual(TEXT("with no summon used, its own figure"),
			  SwingAt(Imp, Target), ItsOwn, 0.01f);

	Summoner.AbilitySystem->NoteSummonUsed();
	TestEqual(TEXT("within five seconds of its summoner's summon, 40% increased"),
			  SwingAt(Imp, Target), ItsOwn * 1.4f, 0.01f);

	World->TimeSeconds += 6.0f;
	TestEqual(TEXT("and six seconds later, its own figure again"),
			  SwingAt(Imp, Target), ItsOwn, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionGearStruckRecordTimeTest,
	"Cataclysm.MinionGear.TheStruckRecordKeepsWhenEachStrikersMostRecentBlowGotThrough",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The record the first-hit conditions read, with the time Set Upon needs.
 * Issue #1515.
 *
 * ONE ENTRY PER STRIKER, OVERWRITTEN BY THEIR NEXT BLOW: a stranger has no
 * time, and a second blow resets the first's age rather than keeping it.
 */
bool FCataclysmMinionGearStruckRecordTimeTest::RunTest(const FString&)
{
	using namespace CataclysmMinionGearTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter You(World, SummonerWeapon);
	FScopedFighter Stranger(World, SummonerWeapon);
	FScopedFighter Enemy(World, /*AttackDamage=*/0.0f);

	TestEqual(TEXT("before any blow the enemy has no time for you"),
			  Enemy.AbilitySystem->SecondsSinceStruckBy(You.AbilitySystem), -1.0f,
			  0.001f);

	UCataclysmSkillEffects::ApplyDirectDamage(You.Actor, Enemy.Actor, 1.0f);
	TestEqual(TEXT("your blow is recorded as this instant"),
			  Enemy.AbilitySystem->SecondsSinceStruckBy(You.AbilitySystem), 0.0f,
			  0.001f);
	TestEqual(TEXT("and a stranger still has none"),
			  Enemy.AbilitySystem->SecondsSinceStruckBy(Stranger.AbilitySystem),
			  -1.0f, 0.001f);

	World->TimeSeconds += 1.5f;
	TestEqual(TEXT("1.5 seconds later it is 1.5 seconds old"),
			  Enemy.AbilitySystem->SecondsSinceStruckBy(You.AbilitySystem), 1.5f,
			  0.001f);
	TestTrue(TEXT("and the first-hit record still holds you"),
			 Enemy.AbilitySystem->WasStruckBy(You.AbilitySystem));

	UCataclysmSkillEffects::ApplyDirectDamage(You.Actor, Enemy.Actor, 1.0f);
	TestEqual(TEXT("and a second blow makes it this instant again"),
			  Enemy.AbilitySystem->SecondsSinceStruckBy(You.AbilitySystem), 0.0f,
			  0.001f);

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
