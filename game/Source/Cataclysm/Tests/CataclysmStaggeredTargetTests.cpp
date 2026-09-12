// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
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
 * An attacker reading whether the character it is hitting is staggered. #45.
 *
 * WHAT THESE ARE FOR. "Staggered enemies take 20%-35% increased damage from all
 * sources" is an enchantment row, and it is the mirror of "Staggered enemies deal
 * 15%-30% increased damage to you", which `Cataclysm.DamageBySource` covers from
 * the other end. The two ask the same question about the same state and read
 * separate fields: the blow record is filled only on the defender's damage taken
 * lookup and `bTargetIsStaggered` only on the attacker's own lookups.
 *
 * NOTHING IN ANY TEST HERE STATES THE STAGGERED STATE TO THE PIPELINE. Every test
 * staggers a real actor with `ApplyStagger` and strikes it, so the whole chain
 * runs. That is deliberate and it is the lesson these files already carry: a set
 * of tests written for the defender's version of a reading all built their own
 * hit and filled the fact in themselves, so a proof case that broke the line
 * filling it failed nothing, three times running. A test that supplies the
 * missing step proves nothing.
 *
 * "FROM ALL SOURCES" IS THE WEARER'S OWN DAMAGE ACROSS ITS TYPES, which is why
 * there is a spell test here as well as an attack one. The row is authored as an
 * `attack_damage` row and a `spell_damage` row, and those are two separate
 * lookups: testing only the first would leave half the row unproven.
 */
namespace CataclysmStaggeredTargetTest
{
	/** Centimetres in a metre, so a test can place an actor in metres. */
	constexpr float StaggerM = 100.0f;

	/** A bare actor holding an ability system, the attributes a blow reads and a weapon. */
	struct FStaggerArmedActor
	{
		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * An armed actor, under this file's own names so a unity build does not see
	 * two of them.
	 *
	 * IT STANDS AT THE ORIGIN AND CANNOT BE MOVED. A bare actor has no root
	 * component, so its location is the origin whatever it is told. Nothing here
	 * depends on where it stands, unlike the distance tests beside it, but two
	 * summoners built this way are identical in every respect except the stat
	 * line -- which is what makes the minion comparison below mean something.
	 */
	FStaggerArmedActor MakeStaggerArmed(UWorld* World)
	{
		FStaggerArmedActor Made;
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
	ACataclysmEnemyCharacter* SpawnStaggerCreatureAt(UWorld* World,
													 const FVector& Where,
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

	/** A creature's health, read off its ability system rather than the character. */
	float StaggerHealthOf(ACataclysmEnemyCharacter* Creature)
	{
		const UCataclysmAbilitySystemComponent* System =
			Creature ? Cast<UCataclysmAbilitySystemComponent>(
						   Creature->GetAbilitySystemComponent())
					 : nullptr;
		return System
			? System->GetNumericAttribute(
				  UCataclysmVitalAttributeSet::GetHealthAttribute())
			: -1.0f;
	}

	FGameplayTagContainer StaggerMelee()
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
			FName(TEXT("Type.Melee"))));
		return Tags;
	}

	FGameplayTagContainer StaggerSpell()
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
			FName(TEXT("Type.Spell"))));
		return Tags;
	}

	/**
	 * An attack damage stat line carrying one modifier conditioned on the target
	 * being staggered and, optionally, one that is not conditioned at all.
	 *
	 * THE UNCONDITIONED ONE IS WHAT MAKES THE BUCKETS TELL APART. With nothing
	 * else in the line, a 20% increase and a 20% multiplier give the same answer,
	 * so a test carrying only the conditional row would pass whichever bucket the
	 * row had landed in. The authored row is `increased`, so this matters: with a
	 * 30% unconditional increase beside it, an increase gives 1.50/1.30 = 1.154
	 * and a multiplier would give 1.20.
	 */
	void GiveStaggerAttackLine(UCataclysmAbilitySystemComponent* System,
							   float ConditionalIncrease,
							   float UnconditionalIncrease)
	{
		FCataclysmStatModifier Conditional;
		Conditional.Bucket = ECataclysmStatBucket::Increased;
		Conditional.Source = ECataclysmModifierSource::Enchantment;
		Conditional.Value = ConditionalIncrease;
		Conditional.Condition = ECataclysmStatCondition::TargetIsStaggered;

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

	/**
	 * A SPELL damage stat line conditioned the same way, and the attribute behind
	 * it.
	 *
	 * A SEPARATE LOOKUP FROM ATTACK DAMAGE, which is why it needs its own test.
	 * `UCataclysmSkillEffects::SpellDamageOf` asks for `spell_damage` through its
	 * own call and receives its own copy of the per-blow facts, so the reading can
	 * reach one and not the other.
	 */
	void GiveStaggerSpellLine(UCataclysmAbilitySystemComponent* System,
							  float ConditionalIncrease)
	{
		System->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetSpellDamageAttribute(), 100.0f);

		FCataclysmStatModifier Conditional;
		Conditional.Bucket = ECataclysmStatBucket::Increased;
		Conditional.Source = ECataclysmModifierSource::Enchantment;
		Conditional.Value = ConditionalIncrease;
		Conditional.Condition = ECataclysmStatCondition::TargetIsStaggered;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(TEXT("spell_damage")));
		Line.Base = 100.0f;
		Line.Modifiers = {Conditional};
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** A blow that may not critically strike, so two hits can be compared. */
	FCataclysmHitDelivery StaggerNoCritical()
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bCannotCriticallyStrike = true;
		return Delivery;
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is
// built as a unity blob and a `using namespace` at file scope reaches the other
// files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStaggeredTargetAttackTest,
	"Cataclysm.StaggeredTarget.AStaggeredTargetTakesMoreAndASteadyOneTakesNormalDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole chain, from a real staggered state to what reached health.
 *
 * TWO TARGETS AND ONE ATTACKER, because a single larger blow would pass against a
 * build that changed every hit. One target is staggered and the other is not, and
 * the same attacker strikes both.
 *
 * NOTHING HERE STATES THE STATE TO THE PIPELINE. `ApplyStagger` puts it on a real
 * actor and the game reads it back during the blow.
 *
 * AND THE STAGGER IS CHECKED BEFORE AND AFTER THE BLOWS. Before, because a stagger
 * that never landed would make the two targets alike and every assertion below
 * pass for the wrong reason. After, because a stagger that a hit cleared would
 * make the second blow's reading depend on the order the strikes ran in.
 */
bool FCataclysmStaggeredTargetAttackTest::RunTest(const FString&)
{
	using namespace CataclysmStaggeredTargetTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FStaggerArmedActor Attacker = MakeStaggerArmed(World);
	ACataclysmEnemyCharacter* Staggered = SpawnStaggerCreatureAt(
		World, FVector(2.0f * StaggerM, 0.0f, 0.0f), 1'000'000.0f);
	ACataclysmEnemyCharacter* Steady = SpawnStaggerCreatureAt(
		World, FVector(0.0f, 2.0f * StaggerM, 0.0f), 1'000'000.0f);
	if (!TestNotNull(TEXT("an attacker"), Attacker.Actor)
		|| !TestNotNull(TEXT("a target to stagger"), Staggered)
		|| !TestNotNull(TEXT("a target to leave alone"), Steady))
	{
		return false;
	}

	// THE ROW'S OWN LOWEST NUMBER, 20%, WITH AN UNCONDITIONAL 30% BESIDE IT so the
	// bucket is measurable. See `GiveStaggerAttackLine`.
	GiveStaggerAttackLine(Attacker.AbilitySystem, /*ConditionalIncrease=*/20.0f,
						  /*UnconditionalIncrease=*/30.0f);

	// STAGGER ONE OF THE TWO, AND CHECK IT TOOK. Both what the call reports and
	// what the character carries, because the two can disagree: a call that
	// refused would return false while a tag left by something else was still
	// present, and checking only the tag would read that as a stagger this test
	// applied.
	const bool bApplied =
		UCataclysmSkillEffects::ApplyStagger(Attacker.Actor, Staggered);
	if (!TestTrue(TEXT("the stagger was applied"), bApplied)
		|| !TestTrue(TEXT("and the target carries it"),
					 UCataclysmSkillEffects::IsStaggered(Staggered))
		|| !TestFalse(TEXT("and the other one does not"),
					  UCataclysmSkillEffects::IsStaggered(Steady)))
	{
		return false;
	}

	const auto Strike = [&](ACataclysmEnemyCharacter* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Target, 100.0f,
										 StaggerMelee(), StaggerNoCritical(),
										 &Resolved);
		return Resolved;
	};

	const FCataclysmDamageResult OnStaggered = Strike(Staggered);
	const FCataclysmDamageResult OnSteady = Strike(Steady);

	if (!TestTrue(TEXT("both blows landed"),
				  OnStaggered.DealtToHealth > 0.0f && OnSteady.DealtToHealth > 0.0f))
	{
		return false;
	}
	TestFalse(TEXT("neither blow was evaded"), OnStaggered.bEvaded || OnSteady.bEvaded);
	TestFalse(TEXT("neither blow was blocked"), OnStaggered.bBlocked || OnSteady.bBlocked);
	TestFalse(TEXT("neither blow critically struck"),
			  OnStaggered.bWasCritical || OnSteady.bWasCritical);

	// THE STAGGER SURVIVED BOTH BLOWS, so the reading above was the one this test
	// applied and not an artefact of the order the strikes ran in.
	TestTrue(TEXT("the staggered target is still staggered afterwards"),
			 UCataclysmSkillEffects::IsStaggered(Staggered));
	TestFalse(TEXT("and the steady one still is not"),
			  UCataclysmSkillEffects::IsStaggered(Steady));

	TestTrue(*FString::Printf(
				 TEXT("the staggered target took more: %.1f against %.1f"),
				 OnStaggered.DealtToHealth, OnSteady.DealtToHealth),
			 OnStaggered.DealtToHealth > OnSteady.DealtToHealth);

	// 1.50 AGAINST 1.30, BECAUSE INCREASES ARE A SUM. A "more" multiplier in the
	// conditional row's place would give 1.30 x 1.20 = 1.56 and a ratio of 1.20,
	// so this figure is what says the row landed in the bucket its words state.
	TestEqual(TEXT("by the sum of the two increases and not by a multiplier"),
			  OnStaggered.DealtToHealth / OnSteady.DealtToHealth,
			  1.5f / 1.3f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStaggeredTargetSpellTest,
	"Cataclysm.StaggeredTarget.ASpellEarnsItTooBecauseItIsItsOwnLookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A spell earns it as well, and that is not implied by the test above.
 *
 * THE ROW IS AUTHORED AS TWO EFFECT ROWS, one on `attack_damage` and one on
 * `spell_damage`, because "from all sources" in this workbook means the wearer's
 * damage across its types. `SpellDamageOf` asks for spell damage through its own
 * call and receives its own copy of the per-blow facts, so a build that threaded
 * the staggered reading into the attack lookup and not the spell one would pass
 * the test above and leave half the authored row dead.
 */
bool FCataclysmStaggeredTargetSpellTest::RunTest(const FString&)
{
	using namespace CataclysmStaggeredTargetTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FStaggerArmedActor Attacker = MakeStaggerArmed(World);
	ACataclysmEnemyCharacter* Staggered = SpawnStaggerCreatureAt(
		World, FVector(2.0f * StaggerM, 0.0f, 0.0f), 1'000'000.0f);
	ACataclysmEnemyCharacter* Steady = SpawnStaggerCreatureAt(
		World, FVector(0.0f, 2.0f * StaggerM, 0.0f), 1'000'000.0f);
	if (!TestNotNull(TEXT("an attacker"), Attacker.Actor)
		|| !TestNotNull(TEXT("a target to stagger"), Staggered)
		|| !TestNotNull(TEXT("a target to leave alone"), Steady))
	{
		return false;
	}

	GiveStaggerSpellLine(Attacker.AbilitySystem, /*ConditionalIncrease=*/20.0f);

	if (!TestTrue(TEXT("the stagger was applied"),
				  UCataclysmSkillEffects::ApplyStagger(Attacker.Actor, Staggered))
		|| !TestTrue(TEXT("and the target carries it"),
					 UCataclysmSkillEffects::IsStaggered(Staggered)))
	{
		return false;
	}

	const auto Cast = [&](ACataclysmEnemyCharacter* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Target, 100.0f,
										 StaggerSpell(), StaggerNoCritical(),
										 &Resolved);
		return Resolved.DealtToHealth;
	};

	const float OnStaggered = Cast(Staggered);
	const float OnSteady = Cast(Steady);
	if (!TestTrue(TEXT("both spells landed"), OnStaggered > 0.0f && OnSteady > 0.0f))
	{
		return false;
	}

	TestTrue(*FString::Printf(
				 TEXT("the staggered target took more from a spell: %.1f "
					  "against %.1f"), OnStaggered, OnSteady),
			 OnStaggered > OnSteady);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStaggeredTargetMinionTest,
	"Cataclysm.StaggeredTarget.AMinionsBlowEarnsTheSummonersStaggerBonusNothingAtAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A minion's blow earns none of it, and this case is wrong by default rather
 * than by omission.
 *
 * `ACataclysmMinion` strikes with the SUMMONER as the attacker, so every
 * attacker-side reading reaches its blow unless something stops it. That is why
 * critical strikes, penetration, weapon sub-type, leech, retaliation, ailment
 * chances and the target distance each had to be blocked by hand at that call
 * site. This is the eighth.
 *
 * AND THE READING IT WOULD GET IS SIMPLY CORRECT, WHICH MAKES THIS STRICTER THAN
 * THE DISTANCE CASE. A distance measured on a minion's blow is the summoner's
 * distance to the minion's target, so that one at least reports the wrong end.
 * Whether the minion's target is staggered is a fact about that target and does
 * not depend on who struck it: there is no wrong number to point at. It is
 * refused purely because a player's conditional damage bonus should not reach a
 * minion's blow at all. Path of Exile treats a minion's actions as separate from
 * its summoner's, and Last Epoch's own documentation says a character's modifiers
 * do not apply unless minions are specified.
 *
 * THE SECOND SUMMONER IS WHAT MAKES THE ASSERTION MEAN ANYTHING. Comparing the
 * subject minion's blow on a staggered target against its own blow on a steady
 * one would pass if the bonus were broken for everybody, or if it were applied to
 * everybody. A minion whose summoner carries no staggered row at all cannot be
 * lifted by either fault, so the two minions dealing the same is the line that
 * survives them.
 */
bool FCataclysmStaggeredTargetMinionTest::RunTest(const FString&)
{
	using namespace CataclysmStaggeredTargetTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FStaggerArmedActor Summoner = MakeStaggerArmed(World);
	if (!TestNotNull(TEXT("a summoner"), Summoner.Actor))
	{
		return false;
	}
	GiveStaggerAttackLine(Summoner.AbilitySystem, /*ConditionalIncrease=*/20.0f,
						  /*UnconditionalIncrease=*/0.0f);

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Summoner.Actor, FVector(1.0f * StaggerM, 0.0f, 0.0f), /*Lifetime=*/20.0f,
		/*bBurns=*/false);

	// PLACED APART ON DIFFERENT AXES so no spawn is displaced by another's
	// collision. Nothing here depends on where they stand, but a displaced spawn
	// would be a silent difference between two things this test treats as alike.
	ACataclysmEnemyCharacter* Staggered = SpawnStaggerCreatureAt(
		World, FVector(2.0f * StaggerM, 0.0f, 0.0f), 1'000'000.0f);
	ACataclysmEnemyCharacter* Steady = SpawnStaggerCreatureAt(
		World, FVector(0.0f, 2.0f * StaggerM, 0.0f), 1'000'000.0f);

	// A SECOND SUMMONER WITH NO STAGGERED ROW AT ALL, AND ITS OWN MINION. Both
	// summoners stand at the origin and are identical except for the stat line.
	FStaggerArmedActor Plain = MakeStaggerArmed(World);
	ACataclysmMinion* PlainImp = Plain.Actor
		? ACataclysmMinion::Spawn(Plain.Actor, FVector(0.0f, 1.0f * StaggerM, 0.0f),
								  /*Lifetime=*/20.0f, /*bBurns=*/false)
		: nullptr;
	ACataclysmEnemyCharacter* PlainStaggered = SpawnStaggerCreatureAt(
		World, FVector(0.0f, 3.0f * StaggerM, 0.0f), 1'000'000.0f);

	if (!TestNotNull(TEXT("a minion"), Imp)
		|| !TestNotNull(TEXT("a staggered creature for it to strike"), Staggered)
		|| !TestNotNull(TEXT("a steady one"), Steady)
		|| !TestNotNull(TEXT("a summoner with no staggered row"), Plain.Actor)
		|| !TestNotNull(TEXT("its minion"), PlainImp)
		|| !TestNotNull(TEXT("and a creature for that minion"), PlainStaggered))
	{
		return false;
	}

	for (ACataclysmEnemyCharacter* Target : {Staggered, PlainStaggered})
	{
		if (!TestTrue(TEXT("the stagger was applied"),
					  UCataclysmSkillEffects::ApplyStagger(Summoner.Actor, Target))
			|| !TestTrue(TEXT("and the creature carries it"),
						 UCataclysmSkillEffects::IsStaggered(Target)))
		{
			return false;
		}
	}
	if (!TestFalse(TEXT("and the steady creature does not"),
				   UCataclysmSkillEffects::IsStaggered(Steady)))
	{
		return false;
	}

	// THE CONTROL: the summoner's OWN blow does earn the bonus. Without this, a
	// test asserting "no bonus" would also pass if the bonus were broken for
	// everybody.
	const auto SummonerStrikes = [&](ACataclysmEnemyCharacter* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Summoner.Actor, Target, 100.0f,
										 StaggerMelee(), StaggerNoCritical(),
										 &Resolved);
		return Resolved.DealtToHealth;
	};
	const float OwnOnStaggered = SummonerStrikes(Staggered);
	const float OwnOnSteady = SummonerStrikes(Steady);
	if (!TestTrue(TEXT("the summoner's own blows landed"),
				  OwnOnStaggered > 0.0f && OwnOnSteady > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("and the summoner DOES earn the bonus on a staggered target"),
			  OwnOnStaggered / OwnOnSteady, 1.2f, 0.01f);

	// AND THE MINION'S BLOW EARNS NONE OF IT. Its damage is its own share of the
	// summoner's, so it is compared against its own blow on a steady target
	// rather than against the summoner's figure.
	const auto MinionStrikes = [](ACataclysmMinion* Striker,
								  ACataclysmEnemyCharacter* Target)
	{
		const float Before = StaggerHealthOf(Target);
		Striker->AttackTarget(Target);
		return Before - StaggerHealthOf(Target);
	};

	const float MinionOnStaggered = MinionStrikes(Imp, Staggered);
	const float MinionOnSteady = MinionStrikes(Imp, Steady);
	if (!TestTrue(TEXT("both of the minion's blows landed"),
				  MinionOnStaggered > 0.0f && MinionOnSteady > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("A MINION DEALS THE SAME TO A STAGGERED TARGET AS TO A STEADY "
				   "ONE, so it earned none of the summoner's stagger bonus"),
			  MinionOnStaggered, MinionOnSteady, 0.01f);

	// AND THE SAME AS A MINION WHOSE SUMMONER HAS NO SUCH ROW AT ALL. This is the
	// assertion that survives a fault lifting both of the blows above together,
	// which the line above cannot see.
	const float PlainDealt = MinionStrikes(PlainImp, PlainStaggered);
	if (!TestTrue(TEXT("the other minion's blow landed"), PlainDealt > 0.0f))
	{
		return false;
	}
	TestEqual(*FString::Printf(
				  TEXT("AND THE SAME AS A MINION WHOSE SUMMONER CARRIES NO "
					   "STAGGERED ROW: %.2f against %.2f"),
				  MinionOnStaggered, PlainDealt),
			  MinionOnStaggered, PlainDealt, 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
