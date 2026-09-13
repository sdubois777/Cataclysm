// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
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
 * A bonus decided by which ailment the character at the other end is carrying.
 * Issue #1515.
 *
 * WHAT THESE ARE FOR. Three Ravager nodes:
 *
 *   Run Them Ragged        +2% increased Attack Damage per point against
 *                          Crippled enemies
 *   Nothing Left In Them   +2% increased Attack Damage per point against
 *                          enemies that are both Crippled and Weakened
 *   Wearing Them Down      +2% increased Damage Reduction per point against
 *                          enemies you have Weakened
 *
 * The first two are authored. The third has its condition built and no row: it
 * grants increased DAMAGE REDUCTION, and `UCataclysmDamageCalculation::Resolve`
 * hands the blow record to the damage TAKEN lookup and not to the damage
 * reduction one -- a boundary that file draws deliberately and attributes to
 * issue #666. Which stat that row should target is with the project owner on
 * issue #1748. `AnAilmentOnTheAttackerIsReadFromTheDefendersEnd` below proves
 * the condition works by putting it on `damage_taken`, which does receive the
 * blow, so the vocabulary is not left untested while the row waits.
 *
 * NOTHING HERE STATES AN AILMENT TO THE PIPELINE. Every test applies a real
 * Cripple or Weaken with `UCataclysmSkillEffects::ApplyNamedEffect` and strikes
 * a real actor, so the whole chain runs. That is the lesson
 * `CataclysmStaggeredTargetTests.cpp` records in its own words: a set of tests
 * written for a reading that all built their own hit and filled the fact in
 * themselves failed nothing when the line filling it was broken, three times
 * running. A test that supplies the missing step proves nothing.
 *
 * THE TARGETS HOLD 10,000 HEALTH AND NOT A MILLION, WHICH IS A DELIBERATE
 * DIFFERENCE FROM THE FILES THESE ARE MODELLED ON. A blow measured as the
 * difference of two health readings cannot resolve a tolerance finer than the
 * float step at that magnitude, and near 1,000,000 a float steps by 0.0625 --
 * six times the 0.01 tolerance those files compare against. At 10,000 the step
 * is 0.0009766. Issue #1728 carries the measurement and the test that hit it.
 */
namespace CataclysmTargetAilmentTest
{
	/** Centimetres in a metre, so a test can place an actor in metres. */
	constexpr float AilmentM = 100.0f;

	/**
	 * What every creature here is spawned with.
	 *
	 * LARGE ENOUGH THAT NO TEST KILLS ITS TARGET and small enough that the float
	 * step stays far below the tolerances asserted. See the file comment.
	 */
	constexpr float AilmentHealth = 10'000.0f;

	/** A bare actor holding an ability system, the attributes a blow reads and a weapon. */
	struct FAilmentArmedActor
	{
		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * An armed actor, under this file's own names so a unity build does not see
	 * two of them.
	 */
	FAilmentArmedActor MakeAilmentArmed(UWorld* World)
	{
		FAilmentArmedActor Made;
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

	/** A creature on the monsters' side, with health enough for every blow here. */
	ACataclysmEnemyCharacter* SpawnAilmentCreatureAt(UWorld* World,
													 const FVector& Where)
	{
		ACataclysmEnemyCharacter* Spawned =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where,
													   FRotator::ZeroRotator);
		if (Spawned)
		{
			Spawned->SetGenericTeamId(
				UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Spawned->SetHealth(AilmentHealth);
		}
		return Spawned;
	}

	/** A creature's health, read off its ability system rather than the character. */
	float AilmentHealthOf(AActor* Creature)
	{
		const UCataclysmAbilitySystemComponent* System =
			Creature ? Cast<UCataclysmAbilitySystemComponent>(
						   UCataclysmTargeting::AbilitySystemOf(Creature))
					 : nullptr;
		return System
			? System->GetNumericAttribute(
				  UCataclysmVitalAttributeSet::GetHealthAttribute())
			: -1.0f;
	}

	FGameplayTagContainer AilmentMelee()
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
			FName(TEXT("Type.Melee"))));
		return Tags;
	}

	/**
	 * Put a real Cripple or Weaken on a target, the way the Of Maiming gem and
	 * the chance-to-cripple affix do.
	 *
	 * `ApplyNamedEffect` AND NOT A TAG WRITTEN BY HAND, which is the whole point
	 * of these tests. Granting the tag directly would leave every assertion below
	 * passing while the line that fills the conditions from a real character was
	 * broken.
	 */
	bool ApplyAilment(AActor* From, AActor* Target, const TCHAR* EffectName)
	{
		const FGameplayTag Tag = UCataclysmSkillShapes::StatusTagFor(EffectName);
		const float Seconds =
			UCataclysmSkillEffects::NumbersForEffectTag(Tag).DurationSeconds;
		return UCataclysmSkillEffects::ApplyNamedEffect(From, Target, Tag,
														Seconds);
	}

	/** Whether a target really carries the named debuff, read back off the actor. */
	bool CarriesAilment(AActor* Target, const FGameplayTag& Tag)
	{
		return Tag.IsValid()
			&& UCataclysmDebuffs::TagsOnActor(Target).HasTagExact(Tag);
	}

	/**
	 * An attack damage stat line carrying one modifier under this condition and,
	 * optionally, one that is not conditioned at all.
	 *
	 * THE UNCONDITIONED ONE IS WHAT MAKES THE BUCKETS TELL APART. With nothing
	 * else in the line a 20% increase and a 20% multiplier give the same answer,
	 * so a test carrying only the conditional row would pass whichever bucket the
	 * row had landed in. The authored rows are `increased`.
	 */
	void GiveAilmentAttackLine(UCataclysmAbilitySystemComponent* System,
							   ECataclysmStatCondition Condition,
							   float ConditionalIncrease,
							   float UnconditionalIncrease)
	{
		FCataclysmStatModifier Conditional;
		Conditional.Bucket = ECataclysmStatBucket::Increased;
		Conditional.Source = ECataclysmModifierSource::PassiveKeystone;
		Conditional.Value = ConditionalIncrease;
		Conditional.Condition = Condition;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line =
			Inputs.FindOrAdd(FName(UCataclysmItemModifiers::AttackDamageStat));
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
	FCataclysmHitDelivery AilmentNoCritical()
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bCannotCriticallyStrike = true;
		return Delivery;
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is
// built as a unity blob and a `using namespace` at file scope reaches the other
// files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetAilmentCrippleTest,
	"Cataclysm.TargetAilment.ACrippledTargetTakesMoreAndACleanOneTakesNormalDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Run Them Ragged, from a real Cripple to what reached health.
 *
 * TWO TARGETS AND ONE ATTACKER, because a single larger blow would pass against
 * a build that raised every hit. One target is Crippled and the other is not,
 * and the same attacker strikes both.
 *
 * AND THE CRIPPLE IS CHECKED BEFORE THE BLOWS AND ON BOTH TARGETS. A Cripple
 * that never landed would make the two alike and every assertion pass for the
 * wrong reason; a Cripple that reached the wrong one would do the same.
 */
bool FCataclysmTargetAilmentCrippleTest::RunTest(const FString&)
{
	using namespace CataclysmTargetAilmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FAilmentArmedActor Attacker = MakeAilmentArmed(World);
	ACataclysmEnemyCharacter* Crippled =
		SpawnAilmentCreatureAt(World, FVector(2.0f * AilmentM, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Clean =
		SpawnAilmentCreatureAt(World, FVector(0.0f, 2.0f * AilmentM, 0.0f));
	if (!TestNotNull(TEXT("an attacker"), Attacker.Actor)
		|| !TestNotNull(TEXT("a target to cripple"), Crippled)
		|| !TestNotNull(TEXT("a target to leave alone"), Clean))
	{
		return false;
	}

	// THE NODE'S OWN VALUE AT FULL INVESTMENT -- 2% a point over 8 points is 16%
	// -- WITH AN UNCONDITIONAL 30% BESIDE IT so the bucket is measurable. An
	// increase gives 1.46/1.30 = 1.123 and a multiplier would give 1.16.
	GiveAilmentAttackLine(Attacker.AbilitySystem,
						  ECataclysmStatCondition::TargetCarriesCripple,
						  /*ConditionalIncrease=*/16.0f,
						  /*UnconditionalIncrease=*/30.0f);

	const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();
	if (!TestTrue(TEXT("the Cripple tag exists in the vocabulary"),
				  Cripple.IsValid()))
	{
		return false;
	}
	if (!TestTrue(TEXT("the Cripple was applied"),
				  ApplyAilment(Attacker.Actor, Crippled, TEXT("Cripple")))
		|| !TestTrue(TEXT("and the target carries it"),
					 CarriesAilment(Crippled, Cripple))
		|| !TestFalse(TEXT("and the other one does not"),
					  CarriesAilment(Clean, Cripple)))
	{
		return false;
	}

	const auto Strike = [&](ACataclysmEnemyCharacter* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Target, 100.0f,
										 AilmentMelee(), AilmentNoCritical(),
										 &Resolved);
		return Resolved.DealtToHealth;
	};

	const float OnCrippled = Strike(Crippled);
	const float OnClean = Strike(Clean);
	if (!TestTrue(TEXT("both blows landed"), OnCrippled > 0.0f && OnClean > 0.0f))
	{
		return false;
	}

	TestEqual(*FString::Printf(
				  TEXT("A CRIPPLED TARGET TAKES 1.46/1.30 OF WHAT A CLEAN ONE "
					   "DOES: %.2f against %.2f"),
				  OnCrippled, OnClean),
			  OnCrippled / OnClean, 1.46f / 1.30f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetAilmentConjunctionTest,
	"Cataclysm.TargetAilment.TheConjunctionPaysOnBothAilmentsAndOnNeitherAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Nothing Left In Them, and this is the test the whole shape exists for.
 *
 * FOUR TARGETS, AND THE TWO IN THE MIDDLE ARE THE POINT. One carries both
 * ailments, one carries only Cripple, one carries only Weaken, and one carries
 * nothing. The first and last would pass against a design written as two
 * separate rows, one per ailment; the middle two are what that design gets
 * wrong.
 *
 * WHY TWO ROWS WOULD BE WRONG, IN ONE SENTENCE.
 * `UCataclysmStatPipeline::Accumulate` sums increases, so a row per ailment
 * would pay on a target carrying EITHER and pay TWICE on one carrying both. The
 * node's sentence says it pays on both and not otherwise.
 */
bool FCataclysmTargetAilmentConjunctionTest::RunTest(const FString&)
{
	using namespace CataclysmTargetAilmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FAilmentArmedActor Attacker = MakeAilmentArmed(World);
	ACataclysmEnemyCharacter* Both =
		SpawnAilmentCreatureAt(World, FVector(2.0f * AilmentM, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* OnlyCripple =
		SpawnAilmentCreatureAt(World, FVector(0.0f, 2.0f * AilmentM, 0.0f));
	ACataclysmEnemyCharacter* OnlyWeaken =
		SpawnAilmentCreatureAt(World, FVector(-2.0f * AilmentM, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Neither =
		SpawnAilmentCreatureAt(World, FVector(0.0f, -2.0f * AilmentM, 0.0f));
	if (!TestNotNull(TEXT("an attacker"), Attacker.Actor)
		|| !TestNotNull(TEXT("a target for both ailments"), Both)
		|| !TestNotNull(TEXT("one for Cripple alone"), OnlyCripple)
		|| !TestNotNull(TEXT("one for Weaken alone"), OnlyWeaken)
		|| !TestNotNull(TEXT("and one for neither"), Neither))
	{
		return false;
	}

	// THE NODE'S OWN VALUE AT FULL INVESTMENT: 2% a point over 6 points is 12%.
	GiveAilmentAttackLine(
		Attacker.AbilitySystem,
		ECataclysmStatCondition::TargetCarriesCrippleAndWeaken,
		/*ConditionalIncrease=*/12.0f, /*UnconditionalIncrease=*/30.0f);

	const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();
	const FGameplayTag Weaken = UCataclysmDebuffs::WeakenTag();
	if (!TestTrue(TEXT("both tags exist in the vocabulary"),
				  Cripple.IsValid() && Weaken.IsValid()))
	{
		return false;
	}

	const bool bApplied =
		ApplyAilment(Attacker.Actor, Both, TEXT("Cripple"))
		&& ApplyAilment(Attacker.Actor, Both, TEXT("Weaken"))
		&& ApplyAilment(Attacker.Actor, OnlyCripple, TEXT("Cripple"))
		&& ApplyAilment(Attacker.Actor, OnlyWeaken, TEXT("Weaken"));
	if (!TestTrue(TEXT("every ailment was applied"), bApplied))
	{
		return false;
	}

	// READ BACK OFF EACH ACTOR, ALL EIGHT WAYS. Checking only that the
	// applications returned true would miss an effect that landed on the wrong
	// target or was replaced by the next one -- and a target carrying both when
	// it should carry one is exactly the state that would make this test pass
	// for the wrong reason.
	if (!TestTrue(TEXT("the both-target carries Cripple"),
				  CarriesAilment(Both, Cripple))
		|| !TestTrue(TEXT("and Weaken"), CarriesAilment(Both, Weaken))
		|| !TestTrue(TEXT("the Cripple-only target carries Cripple"),
					 CarriesAilment(OnlyCripple, Cripple))
		|| !TestFalse(TEXT("and not Weaken"), CarriesAilment(OnlyCripple, Weaken))
		|| !TestTrue(TEXT("the Weaken-only target carries Weaken"),
					 CarriesAilment(OnlyWeaken, Weaken))
		|| !TestFalse(TEXT("and not Cripple"),
					  CarriesAilment(OnlyWeaken, Cripple))
		|| !TestFalse(TEXT("the clean target carries no Cripple"),
					  CarriesAilment(Neither, Cripple))
		|| !TestFalse(TEXT("and no Weaken"), CarriesAilment(Neither, Weaken)))
	{
		return false;
	}

	const auto Strike = [&](ACataclysmEnemyCharacter* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Target, 100.0f,
										 AilmentMelee(), AilmentNoCritical(),
										 &Resolved);
		return Resolved.DealtToHealth;
	};

	const float OnBoth = Strike(Both);
	const float OnCripple = Strike(OnlyCripple);
	const float OnWeaken = Strike(OnlyWeaken);
	const float OnNeither = Strike(Neither);
	if (!TestTrue(TEXT("all four blows landed"),
				  OnBoth > 0.0f && OnCripple > 0.0f && OnWeaken > 0.0f
					  && OnNeither > 0.0f))
	{
		return false;
	}

	TestEqual(*FString::Printf(
				  TEXT("A TARGET CARRYING BOTH TAKES 1.42/1.30 OF WHAT A CLEAN "
					   "ONE DOES: %.2f against %.2f"),
				  OnBoth, OnNeither),
			  OnBoth / OnNeither, 1.42f / 1.30f, 0.01f);

	// THE TWO ASSERTIONS A PAIR OF SEPARATE ROWS WOULD FAIL.
	TestEqual(*FString::Printf(
				  TEXT("A TARGET CARRYING ONLY CRIPPLE TAKES EXACTLY WHAT A "
					   "CLEAN ONE DOES: %.2f against %.2f"),
				  OnCripple, OnNeither),
			  OnCripple, OnNeither, 0.01f);
	TestEqual(*FString::Printf(
				  TEXT("AND A TARGET CARRYING ONLY WEAKEN LIKEWISE: %.2f "
					   "against %.2f"),
				  OnWeaken, OnNeither),
			  OnWeaken, OnNeither, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetAilmentMinionTest,
	"Cataclysm.TargetAilment.AMinionsBlowEarnsTheSummonersAilmentBonusNothingAtAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A minion strikes with its summoner as the attacker, so every attacker-side
 * reading reaches its blow unless it is stopped.
 *
 * THE EXCLUSION IS THE ONE THAT ALREADY EXISTS.
 * `FCataclysmHitDelivery::bCarriesNoTargetState` withholds the staggered state
 * from a minion's blow, and `ApplyHit` now passes a null target behind the same
 * flag so the ailment conditions are withheld by it too. The reason is the one
 * recorded for the stagger: the ailments on a minion's target are a true fact
 * about that target, and a player's conditional damage bonus still should not
 * reach a minion's blow.
 *
 * THE CONTROL IS THE SUMMONER'S OWN BLOW. Without it a test asserting "no
 * bonus" would also pass if the bonus were broken for everybody.
 */
bool FCataclysmTargetAilmentMinionTest::RunTest(const FString&)
{
	using namespace CataclysmTargetAilmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FAilmentArmedActor Summoner = MakeAilmentArmed(World);
	if (!TestNotNull(TEXT("a summoner"), Summoner.Actor))
	{
		return false;
	}
	GiveAilmentAttackLine(Summoner.AbilitySystem,
						  ECataclysmStatCondition::TargetCarriesCripple,
						  /*ConditionalIncrease=*/16.0f,
						  /*UnconditionalIncrease=*/0.0f);

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Summoner.Actor, FVector(1.0f * AilmentM, 0.0f, 0.0f), /*Lifetime=*/20.0f,
		/*bBurns=*/false);

	// PLACED APART ON DIFFERENT AXES so no spawn is displaced by another's
	// collision.
	ACataclysmEnemyCharacter* Crippled =
		SpawnAilmentCreatureAt(World, FVector(2.0f * AilmentM, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Clean =
		SpawnAilmentCreatureAt(World, FVector(0.0f, 2.0f * AilmentM, 0.0f));
	if (!TestNotNull(TEXT("a minion"), Imp)
		|| !TestNotNull(TEXT("a crippled creature"), Crippled)
		|| !TestNotNull(TEXT("and a clean one"), Clean))
	{
		return false;
	}

	const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();
	if (!TestTrue(TEXT("the Cripple was applied"),
				  ApplyAilment(Summoner.Actor, Crippled, TEXT("Cripple")))
		|| !TestTrue(TEXT("and the target carries it"),
					 CarriesAilment(Crippled, Cripple))
		|| !TestFalse(TEXT("and the clean one does not"),
					  CarriesAilment(Clean, Cripple)))
	{
		return false;
	}

	// THE CONTROL: the summoner's OWN blow does earn the bonus.
	const auto SummonerStrikes = [&](ACataclysmEnemyCharacter* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Summoner.Actor, Target, 100.0f,
										 AilmentMelee(), AilmentNoCritical(),
										 &Resolved);
		return Resolved.DealtToHealth;
	};
	const float OwnOnCrippled = SummonerStrikes(Crippled);
	const float OwnOnClean = SummonerStrikes(Clean);
	if (!TestTrue(TEXT("the summoner's own blows landed"),
				  OwnOnCrippled > 0.0f && OwnOnClean > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("and the summoner DOES earn the bonus on a crippled target"),
			  OwnOnCrippled / OwnOnClean, 1.16f, 0.01f);

	// AND THE MINION'S BLOW EARNS NONE OF IT. Its damage is its own, so it is
	// compared against its own blow on a clean target rather than against the
	// summoner's figure.
	const auto MinionStrikes = [](ACataclysmMinion* Striker,
								  ACataclysmEnemyCharacter* Target)
	{
		const float Before = AilmentHealthOf(Target);
		Striker->AttackTarget(Target);
		return Before - AilmentHealthOf(Target);
	};

	const float MinionOnCrippled = MinionStrikes(Imp, Crippled);
	const float MinionOnClean = MinionStrikes(Imp, Clean);
	if (!TestTrue(TEXT("both of the minion's blows landed"),
				  MinionOnCrippled > 0.0f && MinionOnClean > 0.0f))
	{
		return false;
	}
	TestEqual(*FString::Printf(
				  TEXT("A MINION DEALS THE SAME TO A CRIPPLED TARGET AS TO A "
					   "CLEAN ONE, so it earned none of the summoner's ailment "
					   "bonus: %.4f against %.4f"),
				  MinionOnCrippled, MinionOnClean),
			  MinionOnCrippled, MinionOnClean, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetAilmentNoTargetTest,
	"Cataclysm.TargetAilment.ALookupWithNoTargetInHandEarnsNoneOfThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A character sheet has no blow and no target, and must be refused.
 *
 * THE REFUSAL EVERY BLOW PREDICATE MAKES, and the one that stops a conditional
 * bonus being folded into a gameplay attribute where it would be wrong the
 * moment the next blow landed.
 *
 * THE CONTROL IS THE SECOND ASSERTION AND IT IS WHAT MAKES THE FIRST MEAN
 * ANYTHING. A build in which the condition never held would pass the first
 * assertion. The unconditional increase beside it has to still arrive, or this
 * test would also pass on a build that dropped every modifier.
 */
bool FCataclysmTargetAilmentNoTargetTest::RunTest(const FString&)
{
	using namespace CataclysmTargetAilmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FAilmentArmedActor Asking = MakeAilmentArmed(World);
	if (!TestNotNull(TEXT("a character"), Asking.Actor))
	{
		return false;
	}
	GiveAilmentAttackLine(Asking.AbilitySystem,
						  ECataclysmStatCondition::TargetCarriesCripple,
						  /*ConditionalIncrease=*/16.0f,
						  /*UnconditionalIncrease=*/30.0f);

	// NO TARGET PASSED, WHICH IS WHAT A CHARACTER SHEET DOES. The reading is the
	// sum of increases as a fraction, so 0.30 is the unconditional row alone and
	// 0.46 would mean the conditional one had held with no target at all.
	const float Increases = Asking.AbilitySystem->AttackDamageIncreasesForSkill(
		FGameplayTagContainer());

	TestEqual(*FString::Printf(
				  TEXT("A LOOKUP WITH NO TARGET GETS THE UNCONDITIONAL 30%% "
					   "AND NOT THE AILMENT ROW: %.4f"),
				  Increases),
			  Increases, 0.30f, 0.001f);

	// AND THE SAME LOOKUP WITH A CRIPPLED TARGET DOES GET IT. Without this the
	// assertion above would pass on a build where the condition never held.
	ACataclysmEnemyCharacter* Crippled =
		SpawnAilmentCreatureAt(World, FVector(2.0f * AilmentM, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("a target to cripple"), Crippled)
		|| !TestTrue(TEXT("the Cripple was applied"),
					 ApplyAilment(Asking.Actor, Crippled, TEXT("Cripple")))
		|| !TestTrue(TEXT("and the target carries it"),
					 CarriesAilment(Crippled, UCataclysmDebuffs::CrippleTag())))
	{
		return false;
	}

	const float WithTarget = Asking.AbilitySystem->AttackDamageIncreasesForSkill(
		FGameplayTagContainer(), /*SkillHealthCostPercent=*/-1.0f,
		/*MetresMovedBeforeBlow=*/-1.0f, /*TargetDistanceMetres=*/-1.0f,
		/*bTargetIsStaggered=*/false, Crippled);

	TestEqual(*FString::Printf(
				  TEXT("AND THE SAME LOOKUP WITH A CRIPPLED TARGET GETS BOTH: "
					   "%.4f"),
				  WithTarget),
			  WithTarget, 0.46f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetAilmentOpponentSideTest,
	"Cataclysm.TargetAilment.AnAilmentOnTheAttackerIsReadFromTheDefendersEndAndNotTheAttackers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The other end of the blow, and the proof that the pair cannot be crossed.
 *
 * WHY THIS IS ON `damage_taken` AND NOT ON `damage_reduction`. The node that
 * will use `opponent_carries_weaken` is Wearing Them Down, which grants
 * increased DAMAGE REDUCTION -- and `UCataclysmDamageCalculation::Resolve`
 * hands the blow record only to the damage taken lookup. Which stat that row
 * should target is with the project owner on issue #1748. This test proves the
 * condition and its plumbing on the lookup that does receive the blow, so the
 * vocabulary is not left untested while the row waits.
 *
 * THE SECOND HALF IS THE ONE WORTH HAVING. `target_carries_cripple` is put on
 * another defender's line and must grant nothing, because `TargetDebuffs` is
 * filled only on the attacker's own lookups. A build that filled one container
 * from both ends would pass every other test in this file and fail here.
 *
 * ONE ATTACKER AND THREE DEFENDERS, NOT TWO ATTACKERS AND ONE DEFENDER, AND THE
 * REASON IS A CHANGE THAT HAS NOT BEEN MADE YET. Weaken's own designed effect is
 * to reduce the damage of whoever carries it, by 20%. **Nothing applies that
 * today** -- `CataclysmSkillEffects.cpp` says so where it lists the effects its
 * stat column deliberately excludes: "Weaken's damage reduction is applied by
 * nothing yet, and building it is separate work." So a test comparing a Weakened
 * attacker against a clean one would pass now and fail the day somebody builds
 * it, for a reason nothing to do with this condition.
 *
 * ONE ATTACKER STRIKES ALL THREE, so whatever its own Weaken does to its damage
 * is the same in every reading and cancels out of every ratio below. The test
 * measures the defenders' rows, which is what it is for.
 */
bool FCataclysmTargetAilmentOpponentSideTest::RunTest(const FString&)
{
	using namespace CataclysmTargetAilmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Attacker =
		SpawnAilmentCreatureAt(World, FVector(2.0f * AilmentM, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Guarded =
		SpawnAilmentCreatureAt(World, FVector(0.0f, 2.0f * AilmentM, 0.0f));
	ACataclysmEnemyCharacter* Unguarded =
		SpawnAilmentCreatureAt(World, FVector(-2.0f * AilmentM, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Crossed =
		SpawnAilmentCreatureAt(World, FVector(0.0f, -2.0f * AilmentM, 0.0f));
	if (!TestNotNull(TEXT("an attacker to weaken"), Attacker)
		|| !TestNotNull(TEXT("a defender carrying the row"), Guarded)
		|| !TestNotNull(TEXT("one carrying no row at all"), Unguarded)
		|| !TestNotNull(TEXT("and one carrying the crossed row"), Crossed))
	{
		return false;
	}

	Attacker->SetAttackDamage(100.0f);
	for (ACataclysmEnemyCharacter* Hit : {Guarded, Unguarded, Crossed})
	{
		Hit->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	}

	// A DAMAGE TAKEN LINE, THE WAY `UCataclysmPlayerClassStats::ApplyTo` RECORDS
	// ONE, with a "less" multiplier under each condition. The two defenders carry
	// the mirrored pair: one the condition that should hold, one the condition
	// that reads a field this lookup never fills.
	const auto TakeDamageUnder = [](ACataclysmEnemyCharacter* Who,
									ECataclysmStatCondition Condition)
	{
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				Who->GetAbilitySystemComponent());
		if (!System)
		{
			return false;
		}
		FCataclysmStatModifier Less;
		Less.Bucket = ECataclysmStatBucket::More;
		Less.Source = ECataclysmModifierSource::PassiveKeystone;
		Less.Value = -50.0f;
		Less.Condition = Condition;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Taken = Inputs.FindOrAdd(
			FName(UCataclysmDamageCalculation::DamageTakenStat));
		Taken.Base = UCataclysmDamageCalculation::NormalDamageTaken;
		Taken.Modifiers = {Less};
		System->SetStatInputs(MoveTemp(Inputs));
		return true;
	};

	if (!TestTrue(TEXT("the guarded defender's line was recorded"),
				  TakeDamageUnder(Guarded,
								  ECataclysmStatCondition::OpponentCarriesWeaken))
		|| !TestTrue(TEXT("and the crossed defender's"),
					 TakeDamageUnder(
						 Crossed,
						 ECataclysmStatCondition::TargetCarriesCripple)))
	{
		return false;
	}

	// THE UNGUARDED DEFENDER IS GIVEN NO STAT LINE AT ALL, which is what an enemy
	// has, and is the baseline both ratios below are taken against.

	const FGameplayTag Weaken = UCataclysmDebuffs::WeakenTag();
	const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();

	// BOTH AILMENTS ON THE ONE ATTACKER. The Weaken is what the guarded
	// defender's row asks about. The Cripple is for the crossed half: that row
	// asks whether the character being HIT carries Cripple, and putting a Cripple
	// on the attacker instead is what makes the assertion sharp -- a build that
	// filled one container from both ends of the blow would find it and apply the
	// row.
	if (!TestTrue(TEXT("the Weaken was applied to the attacker"),
				  ApplyAilment(Attacker, Attacker, TEXT("Weaken")))
		|| !TestTrue(TEXT("and it carries it"), CarriesAilment(Attacker, Weaken))
		|| !TestTrue(TEXT("the Cripple was applied to the attacker"),
					 ApplyAilment(Attacker, Attacker, TEXT("Cripple")))
		|| !TestTrue(TEXT("and it carries that too"),
					 CarriesAilment(Attacker, Cripple)))
	{
		return false;
	}

	// AND NO DEFENDER CARRIES EITHER. The crossed row would hold for the right
	// reason if the defender it is on were itself Crippled, which would make that
	// assertion pass without saying anything about which end is read.
	for (ACataclysmEnemyCharacter* Hit : {Guarded, Unguarded, Crossed})
	{
		if (!TestFalse(TEXT("a defender carries no Cripple"),
					   CarriesAilment(Hit, Cripple))
			|| !TestFalse(TEXT("and no Weaken"), CarriesAilment(Hit, Weaken)))
		{
			return false;
		}
	}

	// ONE ATTACKER, THREE TARGETS. Whatever the attacker's own Weaken does to its
	// damage -- nothing today, possibly 20% less once somebody builds it -- is
	// the same in all three readings and cancels out of both ratios below.
	const auto Strike = [&](ACataclysmEnemyCharacter* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Attacker, Target, 100.0f,
										 AilmentMelee(), AilmentNoCritical(),
										 &Resolved);
		return Resolved.DealtToHealth;
	};

	const float OnGuarded = Strike(Guarded);
	const float OnUnguarded = Strike(Unguarded);
	const float OnCrossed = Strike(Crossed);
	if (!TestTrue(TEXT("all three blows landed"),
				  OnGuarded > 0.0f && OnUnguarded > 0.0f && OnCrossed > 0.0f))
	{
		return false;
	}

	TestEqual(*FString::Printf(
				  TEXT("A DEFENDER WHOSE ROW ASKS ABOUT THE ATTACKER'S WEAKEN "
					   "TAKES HALF: %.2f against %.2f"),
				  OnGuarded, OnUnguarded),
			  OnGuarded / OnUnguarded, 0.5f, 0.01f);

	// THE CROSSED ROW GRANTS NOTHING. The same blow from the same attacker, which
	// really is carrying Cripple, against a defender whose row asks about the
	// ailments of the character being HIT.
	TestEqual(*FString::Printf(
				  TEXT("A ROW READING THE TARGET'S AILMENTS ON THE DEFENDER'S "
					   "OWN LOOKUP GRANTS NOTHING, so the pair cannot be "
					   "crossed: %.2f against %.2f"),
				  OnCrossed, OnUnguarded),
			  OnCrossed, OnUnguarded, 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
