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
 * An attacker reading how far away its target is. Issue #1596.
 *
 * WHAT THESE ARE FOR. Two named sets were waiting on one reading: Brute's Heart's
 * 2-piece bonus, "You gain 25% increased damage against enemies that are within 5
 * meters of you", and Demon King's Regalia's, "You deal 25% more damage to
 * enemies that are within 5 meters of you". `Cataclysm.EnchantmentSets` covers
 * the authored rows themselves; these cover the reading reaching a real blow.
 *
 * NOTHING IN ANY TEST HERE STATES A DISTANCE TO THE PIPELINE. The number comes
 * from where the actors are standing, every time. That is deliberate and it is
 * the lesson of the change before this one: five tests written for the defender's
 * version of this reading all built their own hit and set the distance
 * themselves, so a proof case that broke the line filling it failed NOTHING,
 * three times running. A test that supplies the missing step proves nothing.
 */
namespace CataclysmTargetDistanceTest
{
	/** Centimetres in a metre, so a test can place an actor in metres. */
	constexpr float M = 100.0f;

	/** A bare actor holding an ability system, the attributes a blow reads and a weapon. */
	struct FArmedActor
	{
		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * `CataclysmCombatEventsTests.cpp`'s armed actor, under this file's own names
	 * so a unity build does not see two of them.
	 *
	 * IT STANDS AT THE ORIGIN AND CANNOT BE MOVED. A bare actor has no root
	 * component, so its location is the origin whatever it is told, which is what
	 * makes the TARGET's position the whole of the distance in every test below.
	 */
	FArmedActor MakeArmed(UWorld* World)
	{
		FArmedActor Made;
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

	/** A creature on the monsters' side with this much health. */
	ACataclysmEnemyCharacter* SpawnCreatureAt(UWorld* World, const FVector& Where,
											  float Health)
	{
		ACataclysmEnemyCharacter* Spawned =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where,
													   FRotator::ZeroRotator);
		if (Spawned)
		{
			Spawned->SetGenericTeamId(
				UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Spawned->SetHealth(Health);
		}
		return Spawned;
	}

	UCataclysmAbilitySystemComponent* SystemOf(ACataclysmEnemyCharacter* Creature)
	{
		return Creature
			? Cast<UCataclysmAbilitySystemComponent>(
				  Creature->GetAbilitySystemComponent())
			: nullptr;
	}

	/**
	 * A creature's health, read off its ability system.
	 *
	 * NOT A GETTER ON THE CHARACTER, because there is none: health lives on the
	 * vital attribute set. `CataclysmCombatEventsTests.cpp` reads it the same way.
	 */
	float HealthOf(ACataclysmEnemyCharacter* Creature)
	{
		const UCataclysmAbilitySystemComponent* System = SystemOf(Creature);
		return System
			? System->GetNumericAttribute(
				  UCataclysmVitalAttributeSet::GetHealthAttribute())
			: -1.0f;
	}

	FGameplayTagContainer Melee()
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
			FName(TEXT("Type.Melee"))));
		return Tags;
	}

	/**
	 * An attack damage stat line for the attacker, carrying one modifier that is
	 * conditioned on how far away the target stands and, optionally, one that is
	 * not conditioned at all.
	 *
	 * THE UNCONDITIONED ONE IS WHAT MAKES THE BUCKETS TELL APART. With nothing
	 * else in the line, a 25% increase and a 25% multiplier give the same answer,
	 * so a test carrying only the conditional row would pass whichever bucket the
	 * row had landed in. See `one modifier cannot see the bucket`.
	 */
	void GiveAttackLine(UCataclysmAbilitySystemComponent* System,
						ECataclysmStatBucket ConditionalBucket,
						float ConditionalValue, float ThresholdMetres,
						float UnconditionalIncrease)
	{
		FCataclysmStatModifier Conditional;
		Conditional.Bucket = ConditionalBucket;
		Conditional.Source = ECataclysmModifierSource::Enchantment;
		Conditional.Value = ConditionalValue;
		Conditional.Condition = ECataclysmStatCondition::TargetWithinMetres;
		Conditional.ConditionValue = ThresholdMetres;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line =
			Inputs.FindOrAdd(FName(UCataclysmItemModifiers::AttackDamageStat));
		Line.Base = 100.0f;
		Line.Modifiers = {Conditional};

		if (UnconditionalIncrease != 0.0f)
		{
			FCataclysmStatModifier Always;
			Always.Bucket = ECataclysmStatBucket::Increased;
			Always.Source = ECataclysmModifierSource::Enchantment;
			Always.Value = UnconditionalIncrease;
			Line.Modifiers.Add(Always);
		}

		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** A blow that may not critically strike, so two hits can be compared. */
	FCataclysmHitDelivery NoCritical()
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bCannotCriticallyStrike = true;
		return Delivery;
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is
// built as a unity blob and a `using namespace` at file scope reaches the other
// files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetDistanceNearAndFarTest,
	"Cataclysm.TargetDistance.ANearTargetTakesMoreAndAFarOneTakesNormalDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole chain, from two actor positions to what reached health.
 *
 * TWO TARGETS AND ONE ATTACKER, because a single softened blow would pass
 * against a build that changed every hit. The attacker stands at the origin and
 * cannot move, so the targets' positions are the whole of the distance: one at
 * two metres is inside the five-metre threshold and one at seven is outside it.
 *
 * NOTHING HERE STATES A DISTANCE. The threshold is stated, which is what the row
 * carries; the distance is measured by the game from where the actors stand.
 */
bool FCataclysmTargetDistanceNearAndFarTest::RunTest(const FString&)
{
	using namespace CataclysmTargetDistanceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FArmedActor Attacker = MakeArmed(World);
	ACataclysmEnemyCharacter* Near =
		SpawnCreatureAt(World, FVector(2.0f * M, 0.0f, 0.0f), 1'000'000.0f);
	ACataclysmEnemyCharacter* Far =
		SpawnCreatureAt(World, FVector(7.0f * M, 0.0f, 0.0f), 1'000'000.0f);
	if (!TestNotNull(TEXT("an attacker"), Attacker.Actor)
		|| !TestNotNull(TEXT("a target up close"), Near)
		|| !TestNotNull(TEXT("a target across the room"), Far))
	{
		return false;
	}

	// BRUTE'S HEART'S OWN NUMBERS: a quarter more damage, within five metres.
	GiveAttackLine(Attacker.AbilitySystem, ECataclysmStatBucket::Increased,
				   25.0f, 5.0f, /*UnconditionalIncrease=*/0.0f);

	const auto Strike = [&](ACataclysmEnemyCharacter* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Target, 100.0f, Melee(),
										 NoCritical(), &Resolved);
		return Resolved;
	};

	const FCataclysmDamageResult OnNear = Strike(Near);
	const FCataclysmDamageResult OnFar = Strike(Far);

	if (!TestTrue(TEXT("both blows landed"),
				  OnNear.DealtToHealth > 0.0f && OnFar.DealtToHealth > 0.0f))
	{
		return false;
	}
	TestFalse(TEXT("neither blow was evaded"), OnNear.bEvaded || OnFar.bEvaded);
	TestFalse(TEXT("neither blow was blocked"), OnNear.bBlocked || OnFar.bBlocked);
	TestFalse(TEXT("neither blow critically struck"),
			  OnNear.bWasCritical || OnFar.bWasCritical);

	TestTrue(*FString::Printf(
				 TEXT("the near target took more: %.1f against %.1f"),
				 OnNear.DealtToHealth, OnFar.DealtToHealth),
			 OnNear.DealtToHealth > OnFar.DealtToHealth);
	TestEqual(TEXT("by the quarter the row states"),
			  OnNear.DealtToHealth / OnFar.DealtToHealth, 1.25f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetDistanceBucketsTest,
	"Cataclysm.TargetDistance.TheSameSentenceAsAnIncreaseAndAsAMultiplierGiveDifferentAnswers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * WHY THIS READING IS A CONDITION ON A ROW AND NOT A VALUE ADDED IN CODE.
 *
 * The two set bonuses differ by one word. Brute's Heart says "25% INCREASED
 * damage" and Demon King's Regalia says "25% MORE damage". Issue #1596 first
 * proposed adding a value into the increases sum, which can express the first
 * and CANNOT express the second.
 *
 * THE DIFFERENCE ONLY SHOWS WHEN SOMETHING ELSE IS IN THE LINE. On a character
 * with no other modifiers both give a quarter more, which is why a test carrying
 * only the conditional row would pass whichever bucket it had landed in. With an
 * unconditional +100% increase beside it:
 *
 *     increase   (1 + 1.00 + 0.25) / (1 + 1.00)            = 1.125
 *     multiplier (1 + 1.00) x 1.25 / (1 + 1.00)            = 1.25
 *
 * So this test is the one that would fail if the two buckets were ever collapsed
 * into one sum, and it fails on an invested character rather than a fresh one --
 * which is exactly the way round that makes a fault ship.
 */
bool FCataclysmTargetDistanceBucketsTest::RunTest(const FString&)
{
	using namespace CataclysmTargetDistanceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE RATIO IS MEASURED WITHIN ONE ATTACKER, so whatever its weapon and its
	// folded increases are cancels out and only the conditional row is left.
	const auto RatioFor = [&](ECataclysmStatBucket Bucket) -> float
	{
		FArmedActor Attacker = MakeArmed(World);
		ACataclysmEnemyCharacter* Near =
			SpawnCreatureAt(World, FVector(2.0f * M, 0.0f, 0.0f), 1'000'000.0f);
		ACataclysmEnemyCharacter* Far =
			SpawnCreatureAt(World, FVector(7.0f * M, 0.0f, 0.0f), 1'000'000.0f);
		if (!Attacker.Actor || !Near || !Far)
		{
			return 0.0f;
		}
		GiveAttackLine(Attacker.AbilitySystem, Bucket, 25.0f, 5.0f,
					   /*UnconditionalIncrease=*/100.0f);

		FCataclysmDamageResult OnNear;
		FCataclysmDamageResult OnFar;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Near, 100.0f, Melee(),
										 NoCritical(), &OnNear);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Far, 100.0f, Melee(),
										 NoCritical(), &OnFar);
		return OnFar.DealtToHealth > 0.0f
			? OnNear.DealtToHealth / OnFar.DealtToHealth
			: 0.0f;
	};

	const float AsIncrease = RatioFor(ECataclysmStatBucket::Increased);
	const float AsMultiplier = RatioFor(ECataclysmStatBucket::More);

	TestEqual(TEXT("as an increase, a quarter joins a sum that already holds one"),
			  AsIncrease, 1.125f, 0.01f);
	TestEqual(TEXT("as a multiplier, the quarter multiplies the whole thing"),
			  AsMultiplier, 1.25f, 0.01f);
	TestTrue(TEXT("SO THE TWO BUCKETS ARE NOT INTERCHANGEABLE, which is why this "
				  "reading is a condition on a row rather than a term in code"),
			 AsMultiplier > AsIncrease + 0.05f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetDistanceMinionTest,
	"Cataclysm.TargetDistance.AMinionsBlowEarnsTheSummonersDistanceBonusNothingAtAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A minion's blow earns none of it, and this is the case that is wrong by
 * default rather than by omission.
 *
 * `ACataclysmMinion` strikes with the SUMMONER as the attacker, so every
 * attacker-side reading reaches its blow unless something stops it -- which is
 * why critical strikes, penetration, weapon sub-type, leech, retaliation and
 * ailment chances each had to be blocked by hand at that call site. A distance
 * measured there would be the summoner's distance to the minion's target.
 *
 * THAT NUMBER IS DEFENSIBLE, WHICH IS WHY THE TEST MATTERS. "Enemies within 5
 * meters of you" asks where the WEARER stands, so reading the summoner's
 * distance is arguably what the sentence wants. The refusal rests on something
 * else: a player's conditional damage bonus should not reach a minion's blow at
 * all. Path of Exile treats a minion's actions as separate from its summoner's,
 * and Last Epoch's own documentation says a character's modifiers do not apply
 * unless minions are specified.
 *
 * THE CONTROL IS WHAT MAKES THE ASSERTION MEAN ANYTHING. The summoner strikes the
 * same creature from the same place and DOES earn the bonus. Without that line,
 * a test asserting "no bonus" would also pass if the bonus were broken for
 * everybody.
 */
bool FCataclysmTargetDistanceMinionTest::RunTest(const FString&)
{
	using namespace CataclysmTargetDistanceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FArmedActor Summoner = MakeArmed(World);
	if (!TestNotNull(TEXT("a summoner"), Summoner.Actor))
	{
		return false;
	}
	GiveAttackLine(Summoner.AbilitySystem, ECataclysmStatBucket::Increased,
				   25.0f, 5.0f, /*UnconditionalIncrease=*/0.0f);

	// THE TARGET STANDS TWO METRES FROM THE SUMMONER, so the condition WOULD hold
	// if a minion's blow were allowed to read it. Placing it far away instead
	// would make the test pass for the wrong reason.
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Summoner.Actor, FVector(1.0f * M, 0.0f, 0.0f), /*Lifetime=*/20.0f,
		/*bBurns=*/false);
	// TWO METRES AWAY ON DIFFERENT AXES, NOT THE SAME SPOT. Both are two metres
	// from the summoner at the origin, which is what the test needs, and putting
	// them on one spot would risk a spawn being displaced by the other's collision
	// -- which would change the distance this test rests on without saying so.
	ACataclysmEnemyCharacter* Struck =
		SpawnCreatureAt(World, FVector(2.0f * M, 0.0f, 0.0f), 1'000'000.0f);
	ACataclysmEnemyCharacter* Control =
		SpawnCreatureAt(World, FVector(0.0f, 2.0f * M, 0.0f), 1'000'000.0f);
	ACataclysmEnemyCharacter* FarControl =
		SpawnCreatureAt(World, FVector(7.0f * M, 0.0f, 0.0f), 1'000'000.0f);
	if (!TestNotNull(TEXT("a minion"), Imp)
		|| !TestNotNull(TEXT("a creature for the minion to strike"), Struck)
		|| !TestNotNull(TEXT("one for the summoner to strike"), Control)
		|| !TestNotNull(TEXT("and one across the room"), FarControl))
	{
		return false;
	}

	TestEqual(TEXT("the creature the minion strikes really is inside the "
				   "threshold, measured from the summoner"),
			  UCataclysmTargeting::MetresBetween(Summoner.Actor, Struck), 2.0f,
			  0.05f);

	const auto SummonerStrikes = [&](ACataclysmEnemyCharacter* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Summoner.Actor, Target, 100.0f, Melee(),
										 NoCritical(), &Resolved);
		return Resolved.DealtToHealth;
	};

	// THE CONTROL: the summoner's own blow on a creature at the same distance
	// earns the bonus, against one across the room that does not.
	const float Near = SummonerStrikes(Control);
	const float Far = SummonerStrikes(FarControl);
	if (!TestTrue(TEXT("the summoner's own blows landed"), Near > 0.0f && Far > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("and the summoner DOES earn the bonus up close"),
			  Near / Far, 1.25f, 0.01f);

	// AND THE MINION'S BLOW EARNS NONE OF IT. Its damage is its own share of the
	// summoner's, so it is compared against itself with the bonus made
	// unreachable rather than against the summoner's figure.
	const float BeforeMinion = HealthOf(Struck);
	Imp->AttackTarget(Struck);
	const float MinionDealt = BeforeMinion - HealthOf(Struck);
	if (!TestTrue(TEXT("the minion's blow landed"), MinionDealt > 0.0f))
	{
		return false;
	}

	const float OnFarTarget = HealthOf(FarControl);
	Imp->AttackTarget(FarControl);
	const float MinionDealtFar = OnFarTarget - HealthOf(FarControl);
	if (!TestTrue(TEXT("and its blow on the far creature landed too"),
				  MinionDealtFar > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("A MINION DEALS THE SAME INSIDE THE THRESHOLD AS OUTSIDE IT, "
				   "so it earned none of the summoner's distance bonus"),
			  MinionDealt, MinionDealtFar, 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
