// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for what a target's crowd control resistance is worth.
 *
 * WHAT WAS WRONG. The `CrowdControlResistance` attribute was read in exactly one
 * place in the whole game: it reduced the CHANCE that a blunt weapon caused an
 * incidental stun. It did nothing about the four skills that stun by design,
 * nothing about a slow, and nothing about being shoved. So a stat on the
 * character sheet -- granted by an affix on seven gear slots, by two class lines
 * and by a helmet implicit -- was worth almost nothing, and the Generic enemy
 * modifier Unyielding, which reads "Immunity to crowd control effects", could not
 * be built at all.
 *
 * WHAT IT IS NOW. It scales the amount of a crowd control effect: a stun's
 * seconds and a shove's centimetres. At 100 nothing lands.
 *
 * **AT 100 A STAT REACHES IMMUNITY, WHICH THIS GAME OTHERWISE REFUSES.** Armour
 * caps at 75%, resistance at 70% and flat damage reduction at 75%, and
 * `UCataclysmDamageCalculation::DamageReductionCap` states the rule: "No
 * combination of these layers reaches immunity." The project owner was shown
 * that conflict, with the figures a player can actually reach, and chose an
 * uncapped stat on 2026-09-05. `docs/DECISIONS.md` records it.
 *
 * WHAT THESE CANNOT CHECK. Whether a stun that is 40% shorter feels different,
 * and whether a creature nothing can stun or shove is a good fight or an
 * infuriating one. Somebody has to play it.
 */

namespace CataclysmCrowdControlTest
{
	/**
	 * A creature with a given crowd control resistance.
	 *
	 * A REAL CREATURE RATHER THAN A BARE ACTOR, AND THAT WAS LEARNED THE HARD
	 * WAY. The first version of this fixture spawned a plain `AActor` and gave
	 * it two attribute sets, and every control assertion failed: a stun could
	 * not land because `ApplyTagForDuration` needs an ability system on the
	 * INSTIGATOR as well as the target, and a shove moved nothing because an
	 * actor spawned with no root component cannot be moved by
	 * `AddActorWorldOffset`. Both failures were the fixture and not the rule
	 * under test, which is exactly what a control assertion is for.
	 */
	struct FScopedCreature
	{
		FScopedCreature(UWorld* World, float Resistance, const FVector& Where)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(
				Where, FRotator::ZeroRotator);
			check(Actor);

			// COMMON, so the rule that a boss cannot be stunned at all does not
			// answer these tests instead of the rule they are about.
			Actor->SetRarityStep(0);
			Actor->SetHealth(500.0f);

			if (UAbilitySystemComponent* AbilitySystem =
					Actor->GetAbilitySystemComponent())
			{
				AbilitySystem->SetNumericAttributeBase(
					UCataclysmCombatAttributeSet::
						GetCrowdControlResistanceAttribute(),
					Resistance);
			}
		}

		~FScopedCreature()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		TObjectPtr<ACataclysmEnemyCharacter> Actor = nullptr;
	};
}

#define CATACLYSM_CC_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// The rule itself
// ---------------------------------------------------------------------------

CATACLYSM_CC_TEST(FCataclysmCrowdControlScalesTest,
	"Cataclysm.CrowdControl.ResistanceShortensAnEffectRatherThanRollingAgainstIt")
{
	using namespace CataclysmCrowdControlTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	{
		// NONE OF IT LEAVES THE AMOUNT ALONE, which is every creature in the
		// game that nothing has given the stat to.
		FScopedCreature Bare(World, 0.0f, FVector(200.0f, 0.0f, 0.0f));
		TestEqual(TEXT("no resistance takes nothing off a 3 second stun"),
				  UCataclysmSkillEffects::AfterCrowdControlResistance(
					  Bare.Actor, 3.0f), 3.0f, 0.001f);
	}

	{
		// HALF IS HALF. The rule is a plain scaling, so a player can work out
		// what the number on their sheet is doing.
		FScopedCreature Half(World, 50.0f, FVector(400.0f, 0.0f, 0.0f));
		TestEqual(TEXT("50 resistance halves a 3 second stun"),
				  UCataclysmSkillEffects::AfterCrowdControlResistance(
					  Half.Actor, 3.0f), 1.5f, 0.001f);
		TestEqual(TEXT("and halves a 400 centimetre shove"),
				  UCataclysmSkillEffects::AfterCrowdControlResistance(
					  Half.Actor, 400.0f), 200.0f, 0.001f);
	}

	{
		// A HUNDRED IS IMMUNE, and this is the one place this game lets a stat
		// reach immunity. The project owner chose it on 2026-09-05 knowing the
		// rest of the design refuses it.
		FScopedCreature Immune(World, 100.0f, FVector(600.0f, 0.0f, 0.0f));
		TestEqual(TEXT("100 resistance stops a stun entirely"),
				  UCataclysmSkillEffects::AfterCrowdControlResistance(
					  Immune.Actor, 3.0f), 0.0f, 0.001f);
		TestEqual(TEXT("and stops a shove entirely"),
				  UCataclysmSkillEffects::AfterCrowdControlResistance(
					  Immune.Actor, 400.0f), 0.0f, 0.001f);
	}

	{
		// PAST A HUNDRED IS STILL IMMUNE AND NOT NEGATIVE. Two class lines and
		// an affix on seven slots feed this stat, so overshooting is reachable
		// rather than hypothetical, and a negative answer would be a stun that
		// lasts backwards.
		FScopedCreature Overcapped(World, 250.0f, FVector(800.0f, 0.0f, 0.0f));
		TestEqual(TEXT("past 100 is still nothing rather than negative"),
				  UCataclysmSkillEffects::AfterCrowdControlResistance(
					  Overcapped.Actor, 3.0f), 0.0f, 0.001f);
	}

	{
		// A NEGATIVE STAT DOES NOT LENGTHEN A STUN. Nothing in the design says a
		// value below zero makes a target easier to control, and a modifier
		// subtracting from the stat could produce one.
		FScopedCreature Negative(World, -40.0f, FVector(1000.0f, 0.0f, 0.0f));
		TestEqual(TEXT("a negative stat leaves the effect at its own length"),
				  UCataclysmSkillEffects::AfterCrowdControlResistance(
					  Negative.Actor, 3.0f), 3.0f, 0.001f);
	}

	// A TARGET WITH NO ABILITY SYSTEM AT ALL TAKES THE WHOLE OF IT rather than
	// none. "Cannot be asked" is not "is immune", and answering zero would make
	// every stun in the game fail against a plain actor.
	TestEqual(TEXT("nothing to ask means the whole amount"),
			  UCataclysmSkillEffects::AfterCrowdControlResistance(nullptr, 3.0f),
			  3.0f, 0.001f);

	// AND NOTHING IS STILL NOTHING. A zero-length stun is not an effect.
	TestEqual(TEXT("no amount answers no amount"),
			  UCataclysmSkillEffects::AfterCrowdControlResistance(nullptr, 0.0f),
			  0.0f, 0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// What it does to a stun that lands
// ---------------------------------------------------------------------------

CATACLYSM_CC_TEST(FCataclysmCrowdControlStunTest,
	"Cataclysm.CrowdControl.ADesignedStunObeysResistanceTheSameAsAnyOther")
{
	using namespace CataclysmCrowdControlTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE ATTACKER NEEDS AN ABILITY SYSTEM TOO. `ApplyTagForDuration` reads
	// one off the instigator to build the effect context, so a bare actor
	// swinging at anything applies nothing at all.
	FScopedCreature Attacker(World, 0.0f, FVector::ZeroVector);

	{
		// A DESIGNED STUN IS THE CASE THAT MATTERS. It skips the damage
		// threshold, because an attack built to stun should not fail to when it
		// lands -- and until 2026-09-05 that meant it ignored the target's
		// resistance completely, because the stat only touched the CHANCE of an
		// incidental stun and a designed one has no chance to reduce.
		FScopedCreature Immune(World, 100.0f, FVector(600.0f, 0.0f, 0.0f));

		TestFalse(TEXT("a designed stun does not land on a fully resistant target"),
				  UCataclysmSkillEffects::ApplyStun(
					  Attacker.Actor, Immune.Actor, /*DurationSeconds=*/1.5f,
					  /*DamageDealt=*/0.0f, /*bStunIsDesigned=*/true));

		TestFalse(TEXT("and the target is not stunned"),
				  UCataclysmSkillEffects::IsStunned(Immune.Actor));
	}

	{
		// AND ONE WITH NO RESISTANCE IS STILL STUNNED, which is what says the
		// refusal above came from the stat rather than from the stun path being
		// broken. A test where the effect never lands either way proves nothing.
		FScopedCreature Bare(World, 0.0f, FVector(1200.0f, 0.0f, 0.0f));

		TestTrue(TEXT("the same stun lands on a target with no resistance"),
				 UCataclysmSkillEffects::ApplyStun(
					 Attacker.Actor, Bare.Actor, /*DurationSeconds=*/1.5f,
					 /*DamageDealt=*/0.0f, /*bStunIsDesigned=*/true));

		TestTrue(TEXT("and that target is stunned"),
				 UCataclysmSkillEffects::IsStunned(Bare.Actor));
	}

	return true;
}

// ---------------------------------------------------------------------------
// What it does to being shoved
// ---------------------------------------------------------------------------

CATACLYSM_CC_TEST(FCataclysmCrowdControlKnockbackTest,
	"Cataclysm.CrowdControl.ResistanceShortensAShoveAndAtAHundredStopsIt")
{
	using namespace CataclysmCrowdControlTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE ATTACKER NEEDS AN ABILITY SYSTEM TOO. `ApplyTagForDuration` reads
	// one off the instigator to build the effect context, so a bare actor
	// swinging at anything applies nothing at all.
	FScopedCreature Attacker(World, 0.0f, FVector::ZeroVector);

	// MEASURED AS A DISTANCE MOVED, not as a return value, because the return
	// value would be true for a shove of any length.
	auto ShovedBy = [&](float Resistance)
	{
		FScopedCreature Target(World, Resistance,
							   FVector(200.0f, 0.0f, 0.0f));
		const FVector Before = Target.Actor->GetActorLocation();

		UCataclysmSkillEffects::ApplyKnockback(Attacker.Actor, Target.Actor,
											   400.0f);

		return (Target.Actor->GetActorLocation() - Before).Size();
	};

	const float Bare = ShovedBy(0.0f);
	TestTrue(TEXT("a target with no resistance is moved"), Bare > 1.0f);

	const float Half = ShovedBy(50.0f);
	TestEqual(TEXT("50 resistance halves how far it is moved"), Half, Bare * 0.5f,
			  1.0f);

	const float Immune = ShovedBy(100.0f);
	TestEqual(TEXT("and 100 resistance means it is not moved at all"), Immune,
			  0.0f, 0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// The modifier this was built for
// ---------------------------------------------------------------------------

CATACLYSM_CC_TEST(FCataclysmUnyieldingTest,
	"Cataclysm.CrowdControl.UnyieldingMakesACreatureImmuneToBeingStunned")
{
	using namespace CataclysmCrowdControlTest;

	// THE STAT ANSWER FIRST, without a world. Unyielding reads "Immunity to
	// crowd control effects" and 100 is what makes that true.
	TestEqual(TEXT("Unyielding grants a hundred crowd control resistance"),
			  UCataclysmEnemyModifiers::CrowdControlResistancePercent(
				  {FName(UCataclysmEnemyModifiers::UnyieldingRow)}), 100.0f);

	TestEqual(TEXT("and a creature carrying no modifiers gets none"),
			  UCataclysmEnemyModifiers::CrowdControlResistancePercent({}), 0.0f);

	// AND THEN THAT IT REACHES A REAL CREATURE AND STOPS A REAL STUN, which the
	// answer above does not say. A rule nothing asks for is a rule that does not
	// run, and this project has shipped that mistake before.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE ATTACKER NEEDS AN ABILITY SYSTEM OF ITS OWN, for the reason the
	// fixture's header gives.
	FScopedCreature Attacker(World, 0.0f, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Enemy =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(300.0f, 0.0f, 0.0f),
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature"), Enemy))
	{
		return false;
	}

	Enemy->SetRarityStep(0);
	Enemy->SetHealth(500.0f);

	// WITHOUT THE MODIFIER IT IS STUNNED, so the refusal below is the modifier's
	// doing and not the creature being unstunnable for some other reason.
	TestTrue(TEXT("a creature without Unyielding is stunned"),
			 UCataclysmSkillEffects::ApplyStun(Attacker.Actor, Enemy,
											   /*DurationSeconds=*/1.5f,
											   /*DamageDealt=*/0.0f,
											   /*bStunIsDesigned=*/true));

	// A STUNNED TARGET CANNOT BE STUNNED AGAIN FOR FIVE SECONDS, so a second
	// creature is needed rather than a second stun on this one. Without this the
	// test below would pass for the wrong reason.
	ACataclysmEnemyCharacter* Unyielding =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(600.0f, 0.0f, 0.0f),
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a second creature"), Unyielding))
	{
		return false;
	}

	Unyielding->SetRarityStep(0);
	Unyielding->SetHealth(500.0f);
	Unyielding->ModifierRows.Add(FName(UCataclysmEnemyModifiers::UnyieldingRow));
	Unyielding->ApplyStartingAttributes();

	TestFalse(TEXT("a creature carrying Unyielding is not stunned"),
			  UCataclysmSkillEffects::ApplyStun(Attacker.Actor, Unyielding,
												/*DurationSeconds=*/1.5f,
												/*DamageDealt=*/0.0f,
												/*bStunIsDesigned=*/true));
	TestFalse(TEXT("and it is not stunned afterwards either"),
			  UCataclysmSkillEffects::IsStunned(Unyielding));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
