// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStacks.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmCorruptedSentinelCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmSuccubusCharacter.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for Commander, the only effect in the game that makes a creature better.
 *
 * WHERE IT COMES FROM: FOUR THINGS, AND THIS SAID ONE. It read "The Succubus's
 * aura Dominion grants it to every allied creature within 8 metres, and nothing
 * else grants it today", and that was already wrong. Counted on 2026-09-14:
 * the Succubus's aura, `UCataclysmEnemyModifiers::RallyAlliesOnDeath`,
 * `UCataclysmEnemyModifiers::TimedStep`, and the dungeon rule
 * `Celestial_Hallowed_Groundfall` for every creature standing in one of its
 * craters.
 *
 * WHICH CHANGES NOTHING ABOUT THIS FILE, and that is the point worth keeping:
 * **these check what holding the tag DOES**, which belongs to every creature
 * rather than to whatever granted it. Each granter's own tests check that the
 * right creatures are given the tag and that they lose it again.
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

	static FGameplayTag WeakenTag()
	{
		return UCataclysmSkillShapes::StatusTagFor(TEXT("Weaken"));
	}

	/** What a creature's blow is worth right now, read the way every blow reads
	 *  it: `ApplyHit` asks `WeaponDamageOf` for the live aggregated value, so a
	 *  modifier on `attack_damage` is included without anything recomputing. */
	static float DamageOf(const AActor* Creature)
	{
		return UCataclysmSkillEffects::WeaponDamageOf(
			UCataclysmTargeting::AbilitySystemOf(Creature));
	}

	static FGameplayTag FeastingTag()
	{
		return UCataclysmSkillShapes::StatusTagFor(TEXT("Feasting"));
	}

	/** Puts Feasting on a creature. Its row states NO duration -- it describes a
	 *  trait rather than a timed buff -- so whatever applies it chooses one,
	 *  exactly as the Succubus chooses Commander's. `ApplyNamedEffect` cannot be
	 *  used here for that reason: it reads the row's duration, which is zero,
	 *  and `ApplyTagForDuration` refuses a duration of zero. */
	static bool Feast(AActor* From, AActor* Target, float Seconds)
	{
		return UCataclysmSkillEffects::ApplyTagForDuration(
			From, Target, FeastingTag(), Seconds);
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
 * Magnitude raises Cripple's reduction to the row's cap and then extends its
 * duration instead. Issue #1256.
 *
 * THE TEST ABOVE IS THE CONTROL FOR THIS ONE AND IS DELIBERATELY UNCHANGED. It
 * applies Cripple stating no figure, which is what every caller did before this
 * change, and asserts the row's own 30%. **If this change moved the base case,
 * that test fails** -- so the before-and-after is an existing test passing
 * rather than a new one asserting.
 *
 * WHAT WAS BROKEN. `UCataclysmAilments::Apply` discarded the magnitude for
 * Cripple entirely: its shape was `AtItsRowsFigures`, which passes the row's
 * duration and nothing else. So every Cripple in the game was the designed 30%
 * however it was applied, and the row's own sentence -- "Magnitude raises the
 * reduction to a cap of 80%, then extends the duration instead" -- was true of
 * nothing. `StrengthCap` was read by no code anywhere.
 *
 * THE FIGURES ARE READ FROM THE ROW AND NOT TYPED, so re-tuning the curse in the
 * sheet does not break this test. With the sheet as it stands, Strength 30 and
 * StrengthCap 80 give a cap scale of 80/30, about 2.67.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmCrippleScalesToItsCapThenLasts,
	"Cataclysm.Enemy.CripplesMagnitudeRaisesItsReductionToTheCapThenItsDuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCrippleScalesToItsCapThenLasts::RunTest(const FString&)
{
	using namespace CataclysmCommanderTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	const FCataclysmAilmentKind* Kind = UCataclysmAilments::KindNamed(TEXT("Cripple"));
	if (!TestNotNull(TEXT("Cripple is an ailment kind"), Kind))
	{
		return false;
	}

	const FCataclysmStatusEffectNumbers Row =
		UCataclysmSkillEffects::NumbersForEffectTag(CrippleTag());

	// THE ROW HAS TO STATE BOTH OR THIS TEST MEASURES NOTHING. A strength of
	// zero would make every reduction zero and a cap of zero would mean no cap,
	// and either way the assertions below would pass over an unscaled curse.
	if (!TestTrue(FString::Printf(
			TEXT("the Cripple row states a strength and a cap, got %.1f and %.1f"),
			Row.Strength, Row.StrengthCap),
		Row.Strength > 0.0f && Row.StrengthCap > Row.Strength))
	{
		return false;
	}

	const float CapScale = Row.StrengthCap / Row.Strength;

	// --- MAGNITUDE ONE IS TODAY'S BEHAVIOUR, EXACTLY -----------------------

	{
		ACataclysmImpCharacter* Imp = Spawn<ACataclysmImpCharacter>(World, FVector::ZeroVector);
		ACataclysmImpCharacter* Curser =
			Spawn<ACataclysmImpCharacter>(World, FVector(500.0f, 0.0f, 0.0f));
		if (!TestNotNull(TEXT("an Imp"), Imp) || !TestNotNull(TEXT("a curser"), Curser))
		{
			return false;
		}

		TestTrue(TEXT("Cripple applies at magnitude one"),
			UCataclysmAilments::Apply(Curser, Imp, *Kind, 1.0f));

		TestEqual(TEXT("at magnitude one the reduction is the row's own figure"),
			Imp->CrippleMultiplier(), 1.0f - Row.Strength / 100.0f);
	}

	// --- BELOW THE CAP, THE REDUCTION SCALES -------------------------------

	{
		ACataclysmImpCharacter* Imp = Spawn<ACataclysmImpCharacter>(World, FVector(100.0f, 0.0f, 0.0f));
		ACataclysmImpCharacter* Curser =
			Spawn<ACataclysmImpCharacter>(World, FVector(600.0f, 0.0f, 0.0f));
		if (!TestNotNull(TEXT("an Imp"), Imp) || !TestNotNull(TEXT("a curser"), Curser))
		{
			return false;
		}

		// HALFWAY TO THE CAP, so the figure is scaled and still under it. Taken
		// from the row rather than typed, so it stays halfway if the sheet moves.
		const float Half = 1.0f + (CapScale - 1.0f) * 0.5f;
		TestTrue(TEXT("Cripple applies below the cap"),
			UCataclysmAilments::Apply(Curser, Imp, *Kind, Half));

		TestEqual(TEXT("below the cap the reduction is the row's figure times the magnitude"),
			Imp->CrippleMultiplier(), 1.0f - Row.Strength * Half / 100.0f, 0.001f);
	}

	// --- AT AND ABOVE THE CAP, THE REDUCTION STOPS -------------------------

	{
		ACataclysmImpCharacter* Imp = Spawn<ACataclysmImpCharacter>(World, FVector(200.0f, 0.0f, 0.0f));
		ACataclysmImpCharacter* Curser =
			Spawn<ACataclysmImpCharacter>(World, FVector(700.0f, 0.0f, 0.0f));
		if (!TestNotNull(TEXT("an Imp"), Imp) || !TestNotNull(TEXT("a curser"), Curser))
		{
			return false;
		}

		// TWICE THE SCALE THAT REACHES THE CAP. The reduction stops at the cap
		// and the leftover doubles the duration.
		TestTrue(TEXT("Cripple applies above the cap"),
			UCataclysmAilments::Apply(Curser, Imp, *Kind, CapScale * 2.0f));

		TestEqual(TEXT("above the cap the reduction is the cap and no more"),
			Imp->CrippleMultiplier(), 1.0f - Row.StrengthCap / 100.0f, 0.001f);

		// AND THE SURPLUS WENT SOMEWHERE, WHICH IS THE HALF A REDUCTION CANNOT
		// SHOW. The curse is still on the creature after its own designed
		// duration has passed, which it would not be if the leftover magnitude
		// had been discarded.
		CataclysmTestWorld::RunClock(World, Row.DurationSeconds * 1.5f);
		TestTrue(TEXT("and the curse outlives the row's own duration"),
			UCataclysmSkillEffects::HasTag(Imp, CrippleTag()));
	}

	// --- A WEAKER APPLICATION DOES NOT LOWER A RUNNING ONE -----------------

	{
		ACataclysmImpCharacter* Imp = Spawn<ACataclysmImpCharacter>(World, FVector(300.0f, 0.0f, 0.0f));
		ACataclysmImpCharacter* Curser =
			Spawn<ACataclysmImpCharacter>(World, FVector(800.0f, 0.0f, 0.0f));
		if (!TestNotNull(TEXT("an Imp"), Imp) || !TestNotNull(TEXT("a curser"), Curser))
		{
			return false;
		}

		// THE STRONGEST APPLICATION WINS, which is the owner's ruling of
		// 2026-09-09. Cripple only joins that rule now that it states a figure:
		// until this change every tag-only effect stated zero and the comparison
		// was nothing against nothing.
		TestTrue(TEXT("a strong Cripple applies"),
			UCataclysmAilments::Apply(Curser, Imp, *Kind, CapScale));
		const float Strong = Imp->CrippleMultiplier();

		TestTrue(TEXT("and a weaker one applies too"),
			UCataclysmAilments::Apply(Curser, Imp, *Kind, 1.0f));

		TestEqual(TEXT("but the weaker one does not lift the stronger reduction"),
			Imp->CrippleMultiplier(), Strong, 0.001f);
	}

	return true;
}

/**
 * Weaken takes the row's share off the damage an enemy deals, and magnitude
 * raises that share to the cap and then extends the duration. Issue #1256.
 *
 * WHAT WAS BROKEN. Weaken's row promises "reduces the affected enemy's damage by
 * 20% for 5 seconds" and nothing applied it. Its ailment shape was
 * `AtItsRowsFigures`, which passes the row's duration and no strength at all, so
 * a chance above 100% changed nothing: not the reduction, not the duration.
 *
 * IT SITS BESIDE THE CRIPPLE TEST BECAUSE IT IS THE SAME RULE WITH A DIFFERENT
 * DESTINATION. Both curses state a proportion and both divide their magnitude at
 * a cap. Cripple carries its figure on the tag because no enemy attribute is read
 * for speed; Weaken moves `attack_damage`, which every blow reads live.
 *
 * THE RATIO IS ASSERTED AND NOT THE DIFFERENCE, for two reasons. An enemy's
 * attack damage is `StartingAttackDamage * DamageScale`, so the absolute figure
 * depends on scaling this test does not set and should not assume. And the ratio
 * is the thing the row promises: "by 20%" is a share, whatever the number is.
 *
 * THE SECTION THAT MATTERS MOST IS THE SECOND. Two creatures with different
 * damage lose the SAME SHARE. Subtracting the figure -- which is what the shared
 * path did before this change, and what issue #1256 originally proposed --
 * reduces a stat by `min(Strength, Current) / Current`, which is the intended
 * share only when the stat is exactly 100. The eight designed attack damage
 * figures run from 9 to 42, so it would take an Imp at 9 to zero and a
 * Gatekeeper at 42 down by 48%.
 *
 * THAT IS WHY THE TWO CREATURES ARE SET EITHER SIDE OF THE ROW'S OWN STRENGTH.
 * One below it and one well above it separate the two cases: a subtraction
 * empties the first and takes a fifth of the second, and a share takes the same
 * fifth from both. A single creature that happened to hit hard enough would pass
 * every other assertion in this test under a subtraction.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWeakenTakesAShareOfDamage,
	"Cataclysm.Enemy.WeakenTakesTheRowsShareOffTheDamageAnEnemyDeals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWeakenTakesAShareOfDamage::RunTest(const FString&)
{
	using namespace CataclysmCommanderTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	const FCataclysmAilmentKind* Kind = UCataclysmAilments::KindNamed(TEXT("Weaken"));
	if (!TestNotNull(TEXT("Weaken is an ailment kind"), Kind))
	{
		return false;
	}

	const FCataclysmStatusEffectNumbers Row =
		UCataclysmSkillEffects::NumbersForEffectTag(WeakenTag());

	// THE ROW HAS TO STATE BOTH OR THIS TEST MEASURES NOTHING. A strength of zero
	// would make every reduction zero and a cap of zero would mean no cap, and
	// either way the assertions below would pass over an unscaled curse.
	if (!TestTrue(FString::Printf(
			TEXT("the Weaken row states a strength and a larger cap, got %.1f and %.1f"),
			Row.Strength, Row.StrengthCap),
		Row.Strength > 0.0f && Row.StrengthCap > Row.Strength))
	{
		return false;
	}

	const float CapScale = Row.StrengthCap / Row.Strength;

	// --- MAGNITUDE ONE TAKES THE ROW'S OWN SHARE ---------------------------

	{
		ACataclysmImpCharacter* Imp = Spawn<ACataclysmImpCharacter>(World, FVector::ZeroVector);
		ACataclysmImpCharacter* Curser =
			Spawn<ACataclysmImpCharacter>(World, FVector(500.0f, 0.0f, 0.0f));
		if (!TestNotNull(TEXT("an Imp"), Imp) || !TestNotNull(TEXT("a curser"), Curser))
		{
			return false;
		}

		// THE TEST HAS TO GIVE THE CREATURE ITS DAMAGE. Only a game mode calls
		// `SetAttackDamage`, so a creature spawned straight into a test world has
		// a starting attack damage of zero and `ApplyStartingAttributes` writes
		// the attribute only when that figure is above zero. **The first version
		// of this test read the attribute without setting it and measured 0.00.**
		//
		// COMFORTABLY ABOVE THE ROW'S CAP, and read from the row so it stays that
		// way if the sheet moves. A figure below the cap would let a subtracted
		// reduction reach zero and make the sections below indistinguishable from
		// one that worked.
		Imp->SetAttackDamage(Row.StrengthCap * 2.0f);

		// THE STATE THIS TEST BUILDS IS ASSERTED BEFORE THE BEHAVIOUR IS. A
		// creature with no attack damage would give a ratio of zero over zero,
		// and every assertion below would be measuring nothing. **This assertion
		// is not decoration: it is what caught the missing line above.**
		const float Before = DamageOf(Imp);
		if (!TestTrue(FString::Printf(
				TEXT("the Imp deals damage to begin with, got %.2f"), Before),
			Before > 0.0f))
		{
			return false;
		}

		TestTrue(TEXT("Weaken applies at magnitude one"),
			UCataclysmAilments::Apply(Curser, Imp, *Kind, 1.0f));

		TestEqual(TEXT("and the damage is the row's share off what it was"),
			DamageOf(Imp) / Before, 1.0f - Row.Strength / 100.0f, 0.001f);
	}

	// --- THE SAME SHARE OFF TWO DIFFERENT CREATURES ------------------------

	{
		ACataclysmImpCharacter* Weak = Spawn<ACataclysmImpCharacter>(World, FVector(100.0f, 0.0f, 0.0f));
		ACataclysmImpCharacter* Strong = Spawn<ACataclysmImpCharacter>(World, FVector(200.0f, 0.0f, 0.0f));
		ACataclysmImpCharacter* Curser =
			Spawn<ACataclysmImpCharacter>(World, FVector(600.0f, 0.0f, 0.0f));
		if (!TestNotNull(TEXT("a weak Imp"), Weak) || !TestNotNull(TEXT("a strong Imp"), Strong)
			|| !TestNotNull(TEXT("a curser"), Curser))
		{
			return false;
		}

		// ONE BELOW THE ROW'S STRENGTH AND ONE WELL ABOVE ITS CAP, which is what
		// makes the two cases distinguishable: subtracting the figure takes ALL
		// of the first creature's damage and a small part of the second's, where
		// a share takes the same part from both.
		//
		// AND NEITHER IS 100. A subtracted reduction is `Strength / Current`,
		// which equals the intended `Strength / 100` exactly when the stat is
		// 100 -- so a creature with 100 attack damage is the one value where a
		// subtraction passes a share's assertion. An earlier version of this
		// section set the stronger creature to `Row.Strength * 5`, which is
		// precisely 100, and put one of the two assertions on that coincidence.
		Weak->SetAttackDamage(Row.Strength * 0.5f);
		Strong->SetAttackDamage(Row.StrengthCap * 2.0f);

		const float WeakBefore = DamageOf(Weak);
		const float StrongBefore = DamageOf(Strong);
		if (!TestTrue(FString::Printf(
				TEXT("the two creatures deal different damage, got %.2f and %.2f"),
				WeakBefore, StrongBefore),
			WeakBefore > 0.0f && StrongBefore > WeakBefore))
		{
			return false;
		}

		TestTrue(TEXT("Weaken applies to the weaker creature"),
			UCataclysmAilments::Apply(Curser, Weak, *Kind, 1.0f));
		TestTrue(TEXT("and to the stronger one"),
			UCataclysmAilments::Apply(Curser, Strong, *Kind, 1.0f));

		const float Share = 1.0f - Row.Strength / 100.0f;

		TestEqual(TEXT("the weaker creature keeps the row's share of its damage"),
			DamageOf(Weak) / WeakBefore, Share, 0.001f);
		TestEqual(TEXT("and so does the stronger one, which a subtraction would not"),
			DamageOf(Strong) / StrongBefore, Share, 0.001f);
	}

	// --- BELOW THE CAP, THE SHARE SCALES -----------------------------------

	{
		ACataclysmImpCharacter* Imp = Spawn<ACataclysmImpCharacter>(World, FVector(300.0f, 0.0f, 0.0f));
		ACataclysmImpCharacter* Curser =
			Spawn<ACataclysmImpCharacter>(World, FVector(700.0f, 0.0f, 0.0f));
		if (!TestNotNull(TEXT("an Imp"), Imp) || !TestNotNull(TEXT("a curser"), Curser))
		{
			return false;
		}

		Imp->SetAttackDamage(Row.StrengthCap * 2.0f);
		const float Before = DamageOf(Imp);
		if (!TestTrue(FString::Printf(
				TEXT("the Imp deals damage to begin with, got %.2f"), Before),
			Before > 0.0f))
		{
			return false;
		}

		// HALFWAY TO THE CAP, so the figure is scaled and still under it. Taken
		// from the row rather than typed, so it stays halfway if the sheet moves.
		const float Half = 1.0f + (CapScale - 1.0f) * 0.5f;
		TestTrue(TEXT("Weaken applies below the cap"),
			UCataclysmAilments::Apply(Curser, Imp, *Kind, Half));

		TestEqual(TEXT("below the cap the share is the row's figure times the magnitude"),
			DamageOf(Imp) / Before, 1.0f - Row.Strength * Half / 100.0f, 0.001f);
	}

	// --- AT AND ABOVE THE CAP, THE SHARE STOPS AND THE DURATION GROWS ------

	{
		ACataclysmImpCharacter* Imp = Spawn<ACataclysmImpCharacter>(World, FVector(400.0f, 0.0f, 0.0f));
		ACataclysmImpCharacter* Curser =
			Spawn<ACataclysmImpCharacter>(World, FVector(800.0f, 0.0f, 0.0f));
		if (!TestNotNull(TEXT("an Imp"), Imp) || !TestNotNull(TEXT("a curser"), Curser))
		{
			return false;
		}

		Imp->SetAttackDamage(Row.StrengthCap * 2.0f);
		const float Before = DamageOf(Imp);
		if (!TestTrue(FString::Printf(
				TEXT("the Imp deals damage to begin with, got %.2f"), Before),
			Before > 0.0f))
		{
			return false;
		}

		// TWICE THE SCALE THAT REACHES THE CAP. The reduction stops at the cap and
		// the leftover doubles the duration.
		TestTrue(TEXT("Weaken applies above the cap"),
			UCataclysmAilments::Apply(Curser, Imp, *Kind, CapScale * 2.0f));

		TestEqual(TEXT("above the cap the share stops at the cap and no further"),
			DamageOf(Imp) / Before, 1.0f - Row.StrengthCap / 100.0f, 0.001f);

		// AND THE SURPLUS WENT SOMEWHERE, WHICH IS THE HALF A REDUCTION CANNOT
		// SHOW. The curse is still on the creature after its own designed duration
		// has passed, which it would not be if the leftover had been discarded.
		CataclysmTestWorld::RunClock(World, Row.DurationSeconds * 1.5f);
		TestTrue(TEXT("and the curse outlives the row's own duration"),
			UCataclysmSkillEffects::HasTag(Imp, WeakenTag()));
	}

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
		AddError(TEXT("Status.Buff.Commander is not in the tag vocabulary, so "
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
		AddError(TEXT("Status.Buff.Commander is not in the tag vocabulary."));
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
		// THE BRUTE, WHICH WRITES ITS OWN WALK. Issue #1515: until 2026-09-24 it
		// wrote its designed figure over the buffed one every frame, so this
		// case is what shows `RefreshWalkSpeed` reaches its code and scales it.
		{TEXT("the Brute"),
		 Spawn<ACataclysmBruteCharacter>(World, FVector(0.0f, 3000.0f, 0.0f))},
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

// --------------------------------------------------------------------------
// Feasting, the first creature buff whose size grows with a count. Issue #1720.
// --------------------------------------------------------------------------

/**
 * A feasting creature attacks faster the more it is hit, and walks no faster.
 *
 * THE ROW, in game/Data/StatusEffects.csv: "This enemy gains a stack of "Feast"
 * every time it is hit, up to 5 stacks, and for every stack its attack speed is
 * increased by 4%. A stack lasts 5 seconds."
 *
 * WHY THE WALK SPEED IS HALF OF WHAT THIS CHECKS. Commander and Cripple, above,
 * each name BOTH movement and attack speed and so share one multiplier. This row
 * names one of the two. The obvious way to build it -- adding it to
 * `SpeedMultiplier` beside the other two -- would also speed the creature's
 * WALKING up, which its row does not say, and nothing else in the project would
 * report it. So the control here is a number that must NOT move.
 *
 * WHERE THIS TEST STARTS, STATED SO IT IS NOT MISTAKEN FOR MORE THAN IT IS. It
 * calls `UCataclysmStacks::NoteDamageTaken` directly, which is the function the
 * real damage path calls one line into
 * `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`. That the damage path
 * reaches that function at all is covered by
 * `Cataclysm.ConditionalDamage.TakingDamageBuildsABloodlustStack`. This starts
 * one step in, and checks what the stacks then do.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmFeastingSpeedsUpAttacks,
	"Cataclysm.Enemy.FeastingSpeedsUpACreaturesAttacksAndNotItsWalking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFeastingSpeedsUpAttacks::RunTest(const FString&)
{
	using namespace CataclysmCommanderTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	// TWO IMPS AND NOT A SUCCUBUS, for the reason the Cripple test above gives:
	// a Succubus grants Commander to the allies around it and every figure here
	// would come out 1.2 times what it should be.
	ACataclysmImpCharacter* Imp =
		Spawn<ACataclysmImpCharacter>(World, FVector::ZeroVector);
	ACataclysmImpCharacter* Other =
		Spawn<ACataclysmImpCharacter>(World, FVector(500.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("an Imp"), Imp)
		|| !TestNotNull(TEXT("something to apply the buff"), Other))
	{
		return false;
	}

	UCataclysmAbilitySystemComponent* Eating =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Imp));
	if (!TestNotNull(TEXT("the Imp has a Cataclysm ability system"), Eating))
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

	// --- NOT FEASTING, WHICH IS THE CONTROL -------------------------------
	//
	// AND IT IS HIT FIRST, WHICH IS THE POINT OF DOING IT HERE. A creature
	// without the buff must bank no stacks at all, so that one which gains the
	// buff part way through a fight starts at nothing rather than inheriting a
	// count from the blows it took before.
	UCataclysmStacks::NoteDamageTaken(Eating);
	UCataclysmStacks::NoteDamageTaken(Eating);

	TestEqual(TEXT("a creature without the buff banks no Feast stacks"),
			  UCataclysmStacks::Held(Eating, ECataclysmStackKind::Feast), 0);
	TestEqual(TEXT("and its multiplier is exactly one"),
			  Imp->FeastingMultiplier(), 1.0f);
	TestEqual(TEXT("and it attacks on its designed interval"),
			  Imp->SecondsBetweenAttacks(), DesignedInterval);

	// --- FEASTING, BUT NOT YET HIT ----------------------------------------

	if (!Feast(Other, Imp, /*Seconds=*/30.0f))
	{
		AddError(TEXT("Feasting could not be applied to the Imp"));
		return false;
	}

	Imp->RefreshWalkSpeed();
	TestEqual(TEXT("holding the buff and unhit, the multiplier is still one"),
			  Imp->FeastingMultiplier(), 1.0f);
	TestEqual(TEXT("and it still attacks on its designed interval"),
			  Imp->SecondsBetweenAttacks(), DesignedInterval);

	// --- FED ---------------------------------------------------------------

	// THE ROW'S OWN FIGURE RATHER THAN A LITERAL 4, so re-tuning the buff in the
	// sheet does not break this test -- and it cannot be re-tuned to nothing
	// without the guard below noticing.
	const float PerStack =
		UCataclysmSkillEffects::NumbersForEffectTag(FeastingTag()).Strength;
	if (PerStack <= 0.0f)
	{
		AddError(FString::Printf(
			TEXT("the Feasting row states %.2f per stack. At zero every "
				 "comparison below would hold for a buff that does nothing."),
			PerStack));
		return false;
	}

	UCataclysmStacks::NoteDamageTaken(Eating);
	TestEqual(TEXT("one blow is one stack"),
			  UCataclysmStacks::Held(Eating, ECataclysmStackKind::Feast), 1);
	TestEqual(TEXT("and one stack is one step faster"),
			  Imp->FeastingMultiplier(), 1.0f + PerStack / 100.0f);

	// AND THE INTERVAL SHORTENS RATHER THAN LENGTHENING. The stored figure is
	// seconds BETWEEN attacks and the buff is a SPEED, so more attack speed
	// makes this number smaller. Getting it backwards reads as the buff working.
	TestTrue(TEXT("one stack shortens the interval"),
			 Imp->SecondsBetweenAttacks() < DesignedInterval);
	TestEqual(TEXT("by exactly the row's figure"),
			  Imp->SecondsBetweenAttacks(),
			  DesignedInterval / (1.0f + PerStack / 100.0f));

	// --- THE STAT THAT MUST NOT MOVE ---------------------------------------

	Imp->RefreshWalkSpeed();
	TestEqual(TEXT("and it walks at exactly its designed speed, unchanged"),
			  Imp->GetCharacterMovement()->MaxWalkSpeed, DesignedWalk);
	TestEqual(TEXT("because Feasting is not in SpeedMultiplier"),
			  Imp->SpeedMultiplier(), 1.0f);

	// --- THE CAP -----------------------------------------------------------

	// PUSHED WELL PAST IT, because a cap that only bites at exactly the limit is
	// a cap nobody has tested.
	for (int32 Blow = 0; Blow < 20; ++Blow)
	{
		UCataclysmStacks::NoteDamageTaken(Eating);
	}

	const int32 Cap = UCataclysmStacks::CapFor(ECataclysmStackKind::Feast);
	TestEqual(TEXT("twenty-one blows are capped at the row's five"),
			  UCataclysmStacks::Held(Eating, ECataclysmStackKind::Feast), Cap);
	TestEqual(TEXT("and a full creature is exactly as quick as an inspired one"),
			  Imp->FeastingMultiplier(),
			  1.0f + ACataclysmEnemyCharacter::CommanderIncreasePercent
						 / 100.0f);

	Imp->RefreshWalkSpeed();
	TestEqual(TEXT("and still walks at its designed speed with five stacks"),
			  Imp->GetCharacterMovement()->MaxWalkSpeed, DesignedWalk);

	// --- AND IT LAPSES -----------------------------------------------------

	World->TimeSeconds +=
		UCataclysmStacks::WindowSecondsFor(ECataclysmStackKind::Feast) + 0.1f;
	TestEqual(TEXT("left alone past the window it holds nothing"),
			  UCataclysmStacks::Held(Eating, ECataclysmStackKind::Feast), 0);
	TestEqual(TEXT("and is back to its designed interval with nothing cleared"),
			  Imp->SecondsBetweenAttacks(), DesignedInterval);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
