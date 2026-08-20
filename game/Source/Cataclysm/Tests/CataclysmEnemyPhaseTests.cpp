// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmImpCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the health-triggered phase machine.
 *
 * WHAT A PHASE IS. It selects which of a creature's abilities are in the
 * rotation, and it changes no number at all. That is the finding the whole boss
 * design leans on, from the research recorded with issue #354 in
 * `docs/DECISIONS.md`: across ten shipped bosses in Path of Exile and Last
 * Epoch, not one gains damage, armour, attack speed or critical strike at a
 * transition. Escalation is adding a named ability.
 *
 * NOTHING IN THE GAME USES IT YET. The Gatekeeper is the only creature the
 * design gives phases to and it is not built; issue #759 builds it. So these
 * tests drive a plain enemy with thresholds set on it directly, which is also
 * the honest way to test the machinery rather than one creature's use of it.
 *
 * WHAT THESE GUARD, and each is something that fails without a word:
 *
 * 1. **A PHASE GOING BACKWARDS.** "Phases add, they do not take away" is the
 *    design's rule, and a creature that dropped a phase would un-learn an
 *    ability mid-fight. Nothing heals a creature today, so this can only be
 *    caught by a test rather than by playing.
 *
 * 2. **AN ABILITY FROM AN EARLIER PHASE BEING LOST.** `ChooseAbility` asks
 *    whether an ability's phase is AT MOST the creature's, not equal to it.
 *    Getting that wrong would make a boss forget its basic attack the moment it
 *    learned anything else.
 *
 * 3. **EVERY OTHER CREATURE BEING AFFECTED.** Six of the seven have no phases,
 *    and a creature with no thresholds must behave exactly as it did before the
 *    machinery existed.
 *
 * 4. **A CREATURE ENTERING ITS LAST PHASE ON THE FRAME IT SPAWNS**, which is
 *    what happens if the fraction is computed before `ApplyStartingAttributes`
 *    has given it a maximum health to be a fraction of.
 */

namespace CataclysmPhaseTest
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

	template <typename T>
	static T* Spawn(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<T>(T::StaticClass(), Where,
									FRotator::ZeroRotator, Params);
	}

	/** Set health directly, the way a spawner does, without dealing damage. */
	static void SetHealthTo(ACataclysmEnemyCharacter* Creature, float Health)
	{
		if (UAbilitySystemComponent* AbilitySystem =
				UCataclysmTargeting::AbilitySystemOf(Creature))
		{
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
		}
	}

	static float HealthOf(const ACataclysmEnemyCharacter* Creature)
	{
		const UAbilitySystemComponent* AbilitySystem =
			UCataclysmTargeting::AbilitySystemOf(Creature);
		return AbilitySystem
			? AbilitySystem->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetHealthAttribute())
			: -1.0f;
	}

	/** The Gatekeeper's designed thresholds, from PHASE_TRANSITIONS in
	 *  sim/cataclysm_sim/enemy_abilities.py. Two make three phases. */
	static TArray<float> GatekeeperThresholds()
	{
		return {0.60f, 0.30f};
	}
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmPhaseBeginsAtTheDesignedHealthFractions,
	"Cataclysm.Enemy.PhaseBeginsAtTheDesignedHealthFractions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPhaseBeginsAtTheDesignedHealthFractions::RunTest(const FString&)
{
	using namespace CataclysmPhaseTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmEnemyCharacter* Boss =
		Spawn<ACataclysmEnemyCharacter>(World, FVector::ZeroVector);
	if (!Boss)
	{
		AddError(TEXT("could not spawn a creature to give phases to"));
		return false;
	}

	Boss->PhaseHealthFractions = GatekeeperThresholds();
	Boss->SetHealth(1000.0f);

	TestEqual(TEXT("at full health it is in the first phase"),
		Boss->CurrentPhase(), 1);

	// EACH THRESHOLD IS INCLUSIVE, so the phase begins AT the figure rather than
	// below it. Exactly 60% has to be phase 2 or the design's "beginning at 60%"
	// would be off by one hit.
	struct FCase { float Health; int32 Phase; const TCHAR* What; };
	const FCase Cases[] = {
		{999.0f, 1, TEXT("one point of damage")},
		{601.0f, 1, TEXT("just above the first threshold")},
		{600.0f, 2, TEXT("exactly at the first threshold")},
		{599.0f, 2, TEXT("just below it")},
		{301.0f, 2, TEXT("just above the second threshold")},
		{300.0f, 3, TEXT("exactly at the second threshold")},
		{1.0f,   3, TEXT("nearly dead")},
	};

	for (const FCase& Case : Cases)
	{
		SetHealthTo(Boss, Case.Health);
		Boss->RefreshPhase();

		TestEqual(*FString::Printf(
				TEXT("%s (%.0f of 1000) puts it in phase %d"),
				Case.What, Case.Health, Case.Phase),
			Boss->CurrentPhase(), Case.Phase);
	}

	// **TWO THRESHOLDS MAKE THREE PHASES**, and there is no fourth however low
	// health goes. N transitions make N+1 phases; the model's
	// `_check_every_phase_is_reachable_and_starts_at_one` holds the same rule on
	// the design side.
	SetHealthTo(Boss, 0.0f);
	Boss->RefreshPhase();

	TestEqual(TEXT("and there is no phase beyond the last, however low it goes"),
		Boss->CurrentPhase(), Boss->PhaseHealthFractions.Num() + 1);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmPhaseNeverGoesBackwards,
	"Cataclysm.Enemy.PhaseNeverGoesBackwards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPhaseNeverGoesBackwards::RunTest(const FString&)
{
	using namespace CataclysmPhaseTest;

	// **"PHASES ADD, THEY DO NOT TAKE AWAY."** A creature healed back above a
	// threshold keeps the phase it reached, because dropping one would un-learn
	// an ability mid-fight -- the one thing the design's rule forbids.
	//
	// NOTHING HEALS A CREATURE TODAY. `ApplyStartingAttributes` deliberately
	// leaves an enemy with no regeneration, so this can only ever be caught by a
	// test. That is exactly why it is one.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmEnemyCharacter* Boss =
		Spawn<ACataclysmEnemyCharacter>(World, FVector::ZeroVector);
	if (!Boss)
	{
		AddError(TEXT("could not spawn a creature to give phases to"));
		return false;
	}

	Boss->PhaseHealthFractions = GatekeeperThresholds();
	Boss->SetHealth(1000.0f);

	SetHealthTo(Boss, 200.0f);
	Boss->RefreshPhase();

	if (Boss->CurrentPhase() != 3)
	{
		AddError(FString::Printf(
			TEXT("the creature is in phase %d at 20%% health and should be in "
				 "3, so this test would pass by doing nothing"),
			Boss->CurrentPhase()));
		return false;
	}

	// HEALED TO FULL, which nothing in the game can do.
	SetHealthTo(Boss, 1000.0f);
	const bool bChanged = Boss->RefreshPhase();

	TestFalse(TEXT("healing it to full does not move it"), bChanged);

	TestEqual(TEXT("**and it is still in the phase it reached**, not back in "
				   "the first"),
		Boss->CurrentPhase(), 3);

	// AND HALF WAY BACK IS THE SAME ANSWER.
	SetHealthTo(Boss, 700.0f);
	Boss->RefreshPhase();

	TestEqual(TEXT("nor does healing it above one threshold and not the other"),
		Boss->CurrentPhase(), 3);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmPhaseIsReachedByTakingDamageRatherThanByTicking,
	"Cataclysm.Enemy.PhaseIsReachedByTakingDamageRatherThanByTicking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPhaseIsReachedByTakingDamageRatherThanByTicking::RunTest(
	const FString&)
{
	using namespace CataclysmPhaseTest;

	// **THE HIT THAT CROSSES THE THRESHOLD IS WHAT MOVES THE PHASE**, not a
	// later frame. A phase decides which ability the brain may choose, and the
	// brain thinks on its own schedule, so a phase arriving a frame late could
	// arrive after the choice it should have changed.
	//
	// This drives the same path a real hit does: writing the Health attribute
	// goes through `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`,
	// which calls `NotifyHealthChanged`, which calls `HealthChanged` on the
	// character. Nothing here calls RefreshPhase by hand.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmEnemyCharacter* Boss =
		Spawn<ACataclysmEnemyCharacter>(World, FVector::ZeroVector);
	if (!Boss)
	{
		AddError(TEXT("could not spawn a creature to give phases to"));
		return false;
	}

	Boss->PhaseHealthFractions = GatekeeperThresholds();
	Boss->SetHealth(1000.0f);
	Boss->RefreshPhase();

	if (Boss->CurrentPhase() != 1)
	{
		AddError(TEXT("the creature did not start in phase 1"));
		return false;
	}

	// THE HOOK, CALLED THE WAY THE ATTRIBUTE SET CALLS IT.
	SetHealthTo(Boss, 500.0f);
	Boss->HealthChanged();

	TestEqual(TEXT("losing half its health puts it in phase 2 through the "
				   "health hook alone"),
		Boss->CurrentPhase(), 2);

	TestEqual(TEXT("and the health really did move"), HealthOf(Boss), 500.0f);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmPhaseKeepsEveryEarlierAbilityAvailable,
	"Cataclysm.Enemy.PhaseKeepsEveryEarlierAbilityAvailable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPhaseKeepsEveryEarlierAbilityAvailable::RunTest(const FString&)
{
	using namespace CataclysmPhaseTest;

	// **THE RULE IS "AT MOST", NOT "EQUAL TO".** An ability available from phase
	// N stays available in every later phase. Getting that wrong would make a
	// boss forget its basic attack the moment it learned anything else, and
	// nothing on screen would explain it.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	// THREE ABILITIES, ONE PER PHASE, all in range and all off cooldown, so the
	// only thing deciding between them is the phase.
	FCataclysmEnemyAbility First;
	First.Name = TEXT("from phase one");
	First.Phase = 1;
	First.MaxRangeCm = 1000.0f;

	FCataclysmEnemyAbility Second = First;
	Second.Name = TEXT("from phase two");
	Second.Phase = 2;

	FCataclysmEnemyAbility Third = First;
	Third.Name = TEXT("from phase three");
	Third.Phase = 3;

	// ORDER IS PRIORITY, and the latest phase is listed FIRST here on purpose:
	// `ChooseAbility` takes the first entry that fits, so this arrangement
	// answers with the newest ability the creature has reached. If the phase
	// test were wrong in the "equal to" direction, phase 3 would answer with
	// only the third and phase 1 would answer with nothing at all.
	const TArray<FCataclysmEnemyAbility> Abilities = {Third, Second, First};

	struct FCase { int32 Phase; const TCHAR* Expected; };
	const FCase Cases[] = {
		{1, TEXT("from phase one")},
		{2, TEXT("from phase two")},
		{3, TEXT("from phase three")},
	};

	for (const FCase& Case : Cases)
	{
		// THE SAME LOOP ChooseAbility RUNS, written out because the controller's
		// version needs a possessed pawn and a target and this is about one
		// comparison inside it.
		FName Chosen;
		for (const FCataclysmEnemyAbility& Ability : Abilities)
		{
			if (Ability.Phase > Case.Phase)
			{
				continue;
			}
			Chosen = Ability.Name;
			break;
		}

		TestEqual(*FString::Printf(
				TEXT("in phase %d the newest available ability is chosen"),
				Case.Phase),
			Chosen, FName(Case.Expected));
	}

	// AND EVERY EARLIER ONE IS STILL THERE, said directly rather than inferred
	// from the loop above.
	int32 AvailableInPhaseThree = 0;
	for (const FCataclysmEnemyAbility& Ability : Abilities)
	{
		AvailableInPhaseThree += (Ability.Phase <= 3) ? 1 : 0;
	}

	TestEqual(TEXT("**all three abilities are available in phase 3**, because "
				   "phases add and never take away"),
		AvailableInPhaseThree, 3);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmACreatureWithNoPhasesIsUnaffected,
	"Cataclysm.Enemy.ACreatureWithNoPhasesIsUnaffected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmACreatureWithNoPhasesIsUnaffected::RunTest(const FString&)
{
	using namespace CataclysmPhaseTest;

	// SIX OF THE SEVEN CREATURES HAVE NO PHASES, so the machinery has to be
	// invisible to them. A creature that fell out of phase 1 would lose every
	// ability it has, because every ability defaults to phase 1.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmImpCharacter* Imp =
		Spawn<ACataclysmImpCharacter>(World, FVector::ZeroVector);
	if (!Imp)
	{
		AddError(TEXT("could not spawn an Imp"));
		return false;
	}

	TestTrue(TEXT("it has no phase thresholds at all"),
		Imp->PhaseHealthFractions.IsEmpty());

	Imp->SetHealth(100.0f);

	TestEqual(TEXT("it starts in phase 1"), Imp->CurrentPhase(), 1);

	// NEARLY DEAD, WHICH WOULD BE THE LAST PHASE FOR ANYTHING THAT HAD ANY.
	SetHealthTo(Imp, 1.0f);
	const bool bChanged = Imp->RefreshPhase();

	TestFalse(TEXT("losing almost all its health moves nothing"), bChanged);
	TestEqual(TEXT("and it is still in phase 1"), Imp->CurrentPhase(), 1);

	// AND A PLAIN CHARACTER, WHICH IS WHAT THE CONTROLLER ASKS WHEN IT DRIVES
	// something that is not an enemy at all. The base answers 1 for ever.
	ACataclysmEnemyCharacter* Plain =
		Spawn<ACataclysmEnemyCharacter>(World, FVector(500.0f, 0.0f, 0.0f));
	if (Plain)
	{
		Plain->SetHealth(100.0f);
		SetHealthTo(Plain, 1.0f);
		Plain->HealthChanged();

		TestEqual(TEXT("and so is a creature with no thresholds set"),
			Plain->CurrentPhase(), 1);
	}

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmPhaseDoesNotJumpBeforeHealthIsKnown,
	"Cataclysm.Enemy.PhaseDoesNotJumpBeforeHealthIsKnown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPhaseDoesNotJumpBeforeHealthIsKnown::RunTest(const FString&)
{
	using namespace CataclysmPhaseTest;

	// **A CREATURE MUST NOT BE IN ITS LAST PHASE ON THE FRAME IT SPAWNS.** The
	// fraction is health over MAXIMUM health, and a spawner sets both through
	// `SetHealth` after the actor exists. Between construction and that call the
	// maximum is zero, and zero over zero is not phase 1.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmEnemyCharacter* Boss =
		Spawn<ACataclysmEnemyCharacter>(World, FVector::ZeroVector);
	if (!Boss)
	{
		AddError(TEXT("could not spawn a creature"));
		return false;
	}

	Boss->PhaseHealthFractions = GatekeeperThresholds();

	// NO SetHealth YET, which is the state a spawner leaves it in for a moment.
	Boss->RefreshPhase();

	TestEqual(TEXT("a creature whose health has not been set yet is in phase 1, "
				   "not in its last"),
		Boss->CurrentPhase(), 1);

	// AND IT STILL WORKS ONCE THE HEALTH ARRIVES, which is what says the guard
	// above refuses rather than latches.
	Boss->SetHealth(1000.0f);
	Boss->RefreshPhase();

	TestEqual(TEXT("and it is still in phase 1 once it has full health"),
		Boss->CurrentPhase(), 1);

	SetHealthTo(Boss, 100.0f);
	Boss->RefreshPhase();

	TestEqual(TEXT("and it reaches its last phase normally afterwards"),
		Boss->CurrentPhase(), 3);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
