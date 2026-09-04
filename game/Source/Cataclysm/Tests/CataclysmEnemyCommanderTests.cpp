// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmCorruptedSentinelCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmSuccubusCharacter.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for Commander, the only effect in the game that makes a creature better.
 *
 * WHERE IT COMES FROM. The Succubus's aura Dominion grants it to every allied
 * creature within 8 metres, and nothing else grants it today. Its own tests, in
 * CataclysmSuccubusTests.cpp, check that the right creatures are given the tag
 * and that they lose it again. **These check what holding the tag DOES**, which
 * belongs to every creature rather than to the one that grants it.
 *
 * WHAT A CREATURE HOLDING IT GAINS: 20% more movement speed and 20% more attack
 * speed, and nothing else. The project owner set those two on 2026-08-20; the
 * design had said only "20% increased stats", which named none.
 * `docs/DECISIONS.md` records why maximum health was ruled out.
 *
 * WHAT THESE GUARD, and each is something that can fail silently:
 *
 * 1. **A CREATURE OVERRIDING THE WRONG FUNCTION AND OPTING OUT OF EVERY BUFF.**
 *    Six creatures used to override `SecondsBetweenAttacks` to return their own
 *    designed interval. The buff is applied in that function on the enemy base,
 *    so every one of them would have thrown it away without a word. The base's
 *    version is now `final` and creatures override
 *    `DesignedSecondsBetweenAttacks` instead, so the mistake is a compile error
 *    -- but a test that walks the real creatures is what says the rename was
 *    done everywhere rather than only where it was noticed.
 *
 * 2. **THE INTERVAL BEING MULTIPLIED INSTEAD OF DIVIDED.** The stored figure is
 *    seconds between attacks and the buff is a speed, so 20% more attack speed
 *    makes the number SMALLER. Getting it backwards makes a buffed creature
 *    slower, which reads as the aura working -- something changed -- while doing
 *    the opposite of what it says.
 *
 * 3. **WALK SPEED NEVER COMING BACK DOWN.** It is a stored number the movement
 *    component reads every frame, unlike the interval, so something has to write
 *    it when the buff lapses as well as when it lands.
 */

namespace CataclysmCommanderTest
{
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}

	static void TearDown(UWorld* World)
	{
		if (World)
		{
			World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	}

	static FGameplayTag CommanderTag()
	{
		return UCataclysmSkillShapes::StatusTagFor(TEXT("Commander"));
	}

	/** Puts Commander on a creature the way the Succubus's aura does: a granted
	 *  tag with a duration, from an instigator. */
	static bool Buff(AActor* From, AActor* Target)
	{
		return UCataclysmSkillEffects::ApplyTagForDuration(
			From, Target, CommanderTag(),
			ACataclysmSuccubusCharacter::DominionGrantSeconds);
	}

	static int32 Unbuff(AActor* Target)
	{
		return UCataclysmSkillEffects::RemoveEffectsGranting(
			Target, CommanderTag());
	}

	static FGameplayTag CrippleTag()
	{
		return UCataclysmSkillShapes::StatusTagFor(TEXT("Cripple"));
	}

	/** Puts Cripple on a creature the way the Of Maiming gem and the chance to
	 *  cripple affix do: `ApplyNamedEffect`, which grants the tag for the row's
	 *  own duration and keeps no magnitude. */
	static bool Cripple(AActor* From, AActor* Target)
	{
		const float Seconds =
			UCataclysmSkillEffects::NumbersForEffectTag(CrippleTag())
				.DurationSeconds;
		return UCataclysmSkillEffects::ApplyNamedEffect(
			From, Target, CrippleTag(), Seconds);
	}

	template <typename T>
	static T* Spawn(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<T>(T::StaticClass(), Where,
									FRotator::ZeroRotator, Params);
	}
}

// --------------------------------------------------------------------------

// --------------------------------------------------------------------------
// Cripple, which could not reach anything until 2026-09-04. Issue #1152.
// --------------------------------------------------------------------------

/**
 * The Cripple curse slows a creature's walking and its attacking.
 *
 * WHAT WENT WRONG. Its row in game/Data/StatusEffects.csv reads "Reduces the
 * affected enemy's movement and attack speed by 30% for 4 seconds", and nothing
 * in the project could change either. `CataclysmStatMovedByEffect` in
 * CataclysmSkillEffects.cpp said so in terms: "Cripple's slow has no
 * movement-speed debuff route". So the tag landed, lasted four seconds and did
 * nothing at all.
 *
 * A PLAYER WAS NEVER THE PROBLEM. `ACataclysmPlayerCharacter::RefreshMovementSpeed`
 * has followed the movement speed attribute since issue #959. An enemy's speed is
 * its own designed figure times the Commander buff, and the attribute reached it
 * nowhere.
 *
 * BOTH STATS, BECAUSE THE ROW NAMES BOTH. A slow that left the attack rate alone
 * would be half a curse and would read as working.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmCrippleSlowsACreature,
	"Cataclysm.Enemy.CrippleSlowsACreaturesWalkingAndItsAttacking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCrippleSlowsACreature::RunTest(const FString&)
{
	using namespace CataclysmCommanderTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmImpCharacter* Imp =
		Spawn<ACataclysmImpCharacter>(World, FVector::ZeroVector);
	// A SECOND IMP AND NOT A SUCCUBUS. A Succubus grants Commander to the
	// allies around it, so spawning one to be the curser buffed the Imp by
	// 20% and every figure below came out 1.2 times what it should be. The
	// instigator only sets the effect context here, so anything will do.
	ACataclysmImpCharacter* Curser =
		Spawn<ACataclysmImpCharacter>(World, FVector(500.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("an Imp"), Imp)
		|| !TestNotNull(TEXT("something to curse it"), Curser))
	{
		return false;
	}

	const float DesignedInterval = Imp->DesignedSecondsBetweenAttacks();
	const float DesignedWalk = Imp->DesignedWalkSpeedCmPerSecond;
	if (DesignedInterval <= 0.0f || DesignedWalk <= 0.0f)
	{
		AddError(FString::Printf(
			TEXT("the Imp's designed interval is %.4f and its designed walk "
				 "speed is %.1f. Both must be above zero or this test would "
				 "pass by comparing nothing."),
			DesignedInterval, DesignedWalk));
		return false;
	}

	// --- UNCURSED, WHICH IS THE CONTROL -----------------------------------

	Imp->RefreshWalkSpeed();
	TestEqual(TEXT("uncursed, its multiplier is exactly one"),
		Imp->CrippleMultiplier(), 1.0f);
	TestEqual(TEXT("and it walks at its designed speed"),
		Imp->GetCharacterMovement()->MaxWalkSpeed, DesignedWalk);
	TestEqual(TEXT("and attacks on its designed interval"),
		Imp->SecondsBetweenAttacks(), DesignedInterval);

	// --- CURSED ------------------------------------------------------------

	if (!Cripple(Curser, Imp))
	{
		AddError(TEXT("Cripple could not be applied to the Imp"));
		return false;
	}
	Imp->RefreshWalkSpeed();

	// THE ROW'S OWN FIGURE RATHER THAN A LITERAL 30, so re-tuning the curse in
	// the sheet does not break this test and cannot be re-tuned to nothing
	// without the assertion below noticing.
	const float Reduction =
		UCataclysmSkillEffects::NumbersForEffectTag(CrippleTag()).Strength;
	TestTrue(FString::Printf(
			TEXT("the Cripple row states a reduction, got %.1f"), Reduction),
		Reduction > 0.0f);

	const float Expected = 1.0f - Reduction / 100.0f;

	TestEqual(TEXT("cursed, its multiplier is the row's reduction"),
		Imp->CrippleMultiplier(), Expected);

	TestEqual(TEXT("it walks slower by exactly that"),
		Imp->GetCharacterMovement()->MaxWalkSpeed, DesignedWalk * Expected);
	TestTrue(FString::Printf(
			TEXT("which is slower than it was designed with: %.1f against %.1f"),
			Imp->GetCharacterMovement()->MaxWalkSpeed, DesignedWalk),
		Imp->GetCharacterMovement()->MaxWalkSpeed < DesignedWalk);

	// **DIVIDED, NOT MULTIPLIED.** The stored figure is seconds BETWEEN attacks
	// and the curse is a reduction in SPEED, so less attack speed makes this
	// number larger.
	TestEqual(TEXT("and its attack interval is DIVIDED by the reduction, so it "
				   "attacks less often rather than more"),
		Imp->SecondsBetweenAttacks(), DesignedInterval / Expected);
	TestTrue(FString::Printf(
			TEXT("which is a longer interval than it was designed with: %.4f "
				 "against %.4f"),
			Imp->SecondsBetweenAttacks(), DesignedInterval),
		Imp->SecondsBetweenAttacks() > DesignedInterval);

	// --- AND IT LIFTS ------------------------------------------------------

	UCataclysmSkillEffects::RemoveEffectsGranting(Imp, CrippleTag());
	Imp->RefreshWalkSpeed();

	TestEqual(TEXT("with the curse gone it walks at its designed speed again"),
		Imp->GetCharacterMovement()->MaxWalkSpeed, DesignedWalk);
	TestEqual(TEXT("and attacks on its designed interval again"),
		Imp->SecondsBetweenAttacks(), DesignedInterval);

	return true;
}

/**
 * A creature that is inspired and cursed at once gets both, not the last one.
 *
 * WHY THIS IS WORTH ITS OWN TEST. Commander and Cripple move the same two stats
 * in opposite directions, and the obvious mistake is for one to overwrite the
 * other -- which would pass every single-effect test above. Both are read
 * through `SpeedMultiplier`, and this is what says so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmCommanderAndCrippleBothApply,
	"Cataclysm.Enemy.ACreatureInspiredAndCrippledAtOnceGetsBoth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCommanderAndCrippleBothApply::RunTest(const FString&)
{
	using namespace CataclysmCommanderTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmImpCharacter* Imp =
		Spawn<ACataclysmImpCharacter>(World, FVector::ZeroVector);
	ACataclysmSuccubusCharacter* Other =
		Spawn<ACataclysmSuccubusCharacter>(World, FVector(500.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("an Imp"), Imp) || !TestNotNull(TEXT("a granter"), Other))
	{
		return false;
	}

	const float DesignedWalk = Imp->DesignedWalkSpeedCmPerSecond;
	const float DesignedInterval = Imp->DesignedSecondsBetweenAttacks();

	if (!Buff(Other, Imp) || !Cripple(Other, Imp))
	{
		AddError(TEXT("could not apply both Commander and Cripple"));
		return false;
	}
	Imp->RefreshWalkSpeed();

	const float Both = Imp->CommanderMultiplier() * Imp->CrippleMultiplier();

	TestTrue(TEXT("Commander is above one"), Imp->CommanderMultiplier() > 1.0f);
	TestTrue(TEXT("and Cripple is below one"), Imp->CrippleMultiplier() < 1.0f);

	TestEqual(TEXT("the two multiply rather than one winning"),
		Imp->SpeedMultiplier(), Both);
	TestEqual(TEXT("its walk speed is the designed figure times both"),
		Imp->GetCharacterMovement()->MaxWalkSpeed, DesignedWalk * Both);
	TestEqual(TEXT("and its attack interval is the designed one divided by both"),
		Imp->SecondsBetweenAttacks(), DesignedInterval / Both);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmCommanderMakesACreatureFasterAndQuicker,
	"Cataclysm.Enemy.CommanderMakesACreatureFasterAndQuicker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCommanderMakesACreatureFasterAndQuicker::RunTest(const FString&)
{
	using namespace CataclysmCommanderTest;
	using Enemy_t = ACataclysmEnemyCharacter;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	if (!CommanderTag().IsValid())
	{
		AddError(TEXT("Status.Commander is not in the tag vocabulary, so "
					  "nothing could be granted whatever the code did. See "
					  "game/Config/Tags/CataclysmTags.ini."));
		return false;
	}

	// THE IMP, BECAUSE IT WALKS AND IT SWINGS. Any creature that does both would
	// do; this one is the fastest and the quickest, so a mistake in either
	// direction is large enough to read.
	ACataclysmImpCharacter* Imp = Spawn<ACataclysmImpCharacter>(
		World, FVector::ZeroVector);

	// **A PLAIN ENEMY GRANTS IT, NOT A SUCCUBUS.** These tests are about what
	// holding the tag DOES, and a real Succubus brings its own aura with it: an
	// earlier version of this test put one 300 cm from the Imp and the control
	// failed, because `PulseDominion` runs from BeginPlay and 300 cm is inside
	// the 8 metre field. The Imp was buffed before the test buffed it. That was
	// the creature working, and it is checked where it belongs, in
	// CataclysmSuccubusTests.cpp.
	ACataclysmEnemyCharacter* Granter = Spawn<ACataclysmEnemyCharacter>(
		World, FVector(20000.0f, 0.0f, 0.0f));
	if (!Imp || !Granter)
	{
		AddError(TEXT("could not spawn an Imp and something to buff it"));
		return false;
	}

	const float DesignedInterval = Imp->DesignedSecondsBetweenAttacks();
	const float DesignedWalk = Imp->DesignedWalkSpeedCmPerSecond;

	if (DesignedInterval <= 0.0f || DesignedWalk <= 0.0f)
	{
		AddError(FString::Printf(
			TEXT("the Imp's designed interval is %.4f and its designed walk "
				 "speed is %.1f. Both must be above zero or this test would "
				 "pass by comparing nothing."),
			DesignedInterval, DesignedWalk));
		return false;
	}

	// --- UNBUFFED, WHICH IS THE CONTROL ----------------------------------

	Imp->RefreshWalkSpeed();

	TestEqual(TEXT("unbuffed, it attacks on its designed interval"),
		Imp->SecondsBetweenAttacks(), DesignedInterval);

	TestEqual(TEXT("and walks at its designed speed"),
		Imp->GetCharacterMovement()->MaxWalkSpeed, DesignedWalk);

	TestEqual(TEXT("and its multiplier is exactly one"),
		Imp->CommanderMultiplier(), 1.0f);

	// --- BUFFED -----------------------------------------------------------

	if (!Buff(Granter, Imp))
	{
		AddError(TEXT("Commander could not be applied to the Imp"));
		return false;
	}
	Imp->RefreshWalkSpeed();

	const float Expected = 1.0f + Enemy_t::CommanderIncreasePercent / 100.0f;

	TestEqual(TEXT("buffed, its multiplier is the designed increase"),
		Imp->CommanderMultiplier(), Expected);

	// **DIVIDED, NOT MULTIPLIED.** The stored figure is seconds BETWEEN attacks
	// and the buff is a SPEED, so more attack speed makes this number smaller.
	TestEqual(TEXT("its attack interval is DIVIDED by the increase, so it "
				   "attacks more often rather than less"),
		Imp->SecondsBetweenAttacks(), DesignedInterval / Expected);

	TestTrue(FString::Printf(
			TEXT("which is a shorter interval than it was designed with: %.4f "
				 "against %.4f"),
			Imp->SecondsBetweenAttacks(), DesignedInterval),
		Imp->SecondsBetweenAttacks() < DesignedInterval);

	// AND WALK SPEED IS MULTIPLIED, because that one really is a speed.
	TestEqual(TEXT("its walk speed is MULTIPLIED by the increase"),
		Imp->GetCharacterMovement()->MaxWalkSpeed, DesignedWalk * Expected);

	// THE DESIGNED FIGURES THEMSELVES DO NOT MOVE. If the buff wrote back into
	// them it would compound every time it was reapplied, and the aura reapplies
	// twice a second.
	TestEqual(TEXT("and the designed interval underneath is untouched"),
		Imp->DesignedSecondsBetweenAttacks(), DesignedInterval);

	TestEqual(TEXT("as is the designed walk speed"),
		Imp->DesignedWalkSpeedCmPerSecond, DesignedWalk);

	// APPLIED TWICE DOES NOT COMPOUND. The effect is single-stack, so a second
	// application replaces the first. The aura refreshes every half second, so
	// this is the case that runs constantly rather than an edge one.
	Buff(Granter, Imp);
	Imp->RefreshWalkSpeed();

	TestEqual(TEXT("granting it a second time changes nothing, because the "
				   "effect is one stack refreshed rather than two added"),
		Imp->GetCharacterMovement()->MaxWalkSpeed, DesignedWalk * Expected);

	// --- AND BACK AGAIN ---------------------------------------------------

	Unbuff(Imp);
	Imp->RefreshWalkSpeed();

	TestEqual(TEXT("with the buff gone it attacks on its designed interval "
				   "again"),
		Imp->SecondsBetweenAttacks(), DesignedInterval);

	TestEqual(TEXT("and walks at its designed speed again"),
		Imp->GetCharacterMovement()->MaxWalkSpeed, DesignedWalk);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmCommanderReachesEveryCreatureThatCanBeBuffed,
	"Cataclysm.Enemy.CommanderReachesEveryCreatureThatCanBeBuffed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCommanderReachesEveryCreatureThatCanBeBuffed::RunTest(const FString&)
{
	using namespace CataclysmCommanderTest;

	// **WHAT THIS EXISTS FOR.** Every creature used to override
	// `SecondsBetweenAttacks` to return its own designed interval, and the buff
	// is applied in that function. The base's version is `final` now, so a
	// creature that overrode it would not compile -- but "it compiles" and "the
	// rename was done on every creature" are different claims, and a creature
	// whose override was deleted rather than renamed would compile and quietly
	// use the base's 1.5 second default.
	//
	// SO THIS WALKS THE REAL CLASSES rather than trusting the compiler.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	if (!CommanderTag().IsValid())
	{
		AddError(TEXT("Status.Commander is not in the tag vocabulary."));
		return false;
	}

	// A PLAIN ENEMY AGAIN, AND FAR AWAY. See the note in the test above: a
	// Succubus used as a granter buffs whatever is near it on its own.
	ACataclysmEnemyCharacter* Granter = Spawn<ACataclysmEnemyCharacter>(
		World, FVector(20000.0f, 0.0f, 0.0f));
	if (!Granter)
	{
		AddError(TEXT("could not spawn something to grant the buff"));
		return false;
	}

	struct FCase
	{
		const TCHAR* Name;
		ACataclysmEnemyCharacter* Creature;
	};

	// TWO CREATURES THAT SIT AT OPPOSITE ENDS OF WHAT THE BUFF CAN DO. The Imp
	// is the fastest and quickest thing in the roster; the Corrupted Sentinel
	// cannot move at all, so 20% more of its walk speed must still be zero.
	const FCase Cases[] = {
		{TEXT("the Imp"),
		 Spawn<ACataclysmImpCharacter>(World, FVector(0.0f, 0.0f, 0.0f))},
		{TEXT("the Corrupted Sentinel"),
		 Spawn<ACataclysmCorruptedSentinelCharacter>(
			 World, FVector(0.0f, 1000.0f, 0.0f))},
		{TEXT("the Succubus"),
		 Spawn<ACataclysmSuccubusCharacter>(World, FVector(0.0f, 2000.0f, 0.0f))},
	};

	const float Expected =
		1.0f + ACataclysmEnemyCharacter::CommanderIncreasePercent / 100.0f;

	for (const FCase& Case : Cases)
	{
		if (!Case.Creature)
		{
			AddError(FString::Printf(TEXT("could not spawn %s"), Case.Name));
			continue;
		}

		const float DesignedInterval =
			Case.Creature->DesignedSecondsBetweenAttacks();
		const float DesignedWalk = Case.Creature->DesignedWalkSpeedCmPerSecond;

		if (!Buff(Granter, Case.Creature))
		{
			AddError(FString::Printf(TEXT("could not buff %s"), Case.Name));
			continue;
		}
		Case.Creature->RefreshWalkSpeed();

		TestEqual(*FString::Printf(
				TEXT("%s attacks more often while buffed"), Case.Name),
			Case.Creature->SecondsBetweenAttacks(), DesignedInterval / Expected);

		// **A CREATURE DESIGNED AT ZERO SPEED STAYS AT ZERO**, because 20% more
		// of nothing is nothing. An aura does not un-root a turret, and the
		// Corrupted Sentinel is the case that proves the arithmetic is a
		// multiplication of the designed figure rather than an addition to it.
		TestEqual(*FString::Printf(
				TEXT("%s walks at its designed speed times the increase"),
				Case.Name),
			Case.Creature->GetCharacterMovement()->MaxWalkSpeed,
			DesignedWalk * Expected);

		Unbuff(Case.Creature);
		Case.Creature->RefreshWalkSpeed();

		TestEqual(*FString::Printf(
				TEXT("and %s is back to its designed interval afterwards"),
				Case.Name),
			Case.Creature->SecondsBetweenAttacks(), DesignedInterval);
	}

	// THE SENTINEL SPECIFICALLY, said out loud rather than left implied by the
	// loop above: it cannot move and the buff must not change that.
	if (ACataclysmEnemyCharacter* Sentinel = Cases[1].Creature)
	{
		Buff(Granter, Sentinel);
		Sentinel->RefreshWalkSpeed();

		TestEqual(TEXT("**a buffed Corrupted Sentinel still cannot move**"),
			Sentinel->GetCharacterMovement()->MaxWalkSpeed, 0.0f);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
