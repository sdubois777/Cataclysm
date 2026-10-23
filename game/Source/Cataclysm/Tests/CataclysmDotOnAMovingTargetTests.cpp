// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * Ruled 2026-10-23 (#2235), for the owner's reword "While the enemy is moving, your DoTs on them deal 20%-40% more
 * damage": `dot_damage` more under `target_is_moving`, JUDGED ON EACH TICK AGAINST THE CHARACTER IT LANDS ON (Q2 (a)),
 * and only on a tick whose figure was priced from its applier's own stats (point 3). No row is authored: the wearer's
 * stat line is written by hand with a TEST-ONLY row of the new row's shape, `dot_damage` more 30 under
 * `target_is_moving`; the real row, 20 to 40, comes with the data commit.
 *
 * THE BURNS ARE REAL AND SO ARE THEIR TICKS. Each is laid by `UCataclysmSkillEffects::ApplyDamageOverTime`, ten a tick
 * for ten seconds, and the world's clock is moved in steps of 0.05 s by `CataclysmTestWorld::RunClock`, which fires the
 * engine's own period timers and the creatures' own movement samples.
 *
 * A CREATURE IS MOVED BY BOTH MEANS THE GAME AND ITS TESTS USE, on every step: it is put 10 cm further along +Y, which
 * the movement sampler (`UCataclysmMovement::SampleStep`, every 0.25 s) reads as walking, and it is told so with
 * `NoteMovedMetres`, the call the sampler makes and the call the moving-row tests make. A creature standing is told
 * `NoteDidNotMove` on every step and is not moved. So whether or not a sample falls inside a step, the reading at a
 * tick is the one the step set. EACH WINDOW ASSERTS THE READING IT SET, as a set-up check: a test that cannot make its
 * target read as moving fails there, by name, and not as a figure.
 *
 * NO FIGURE ASSUMES A CREATURE'S DEFENCES. Every expected figure is a ratio of one creature's loss to a CONTROL
 * creature's loss over the same window, each window holding exactly one tick (the ticks land at whole seconds after the
 * lay; each window runs from half a second before one to half a second after it). A creature's armour and resistance
 * multiply a tick, so they cancel in the ratio. Each creature's health regeneration and energy shield are set to
 * nought first, so the loss is the tick and nothing else.
 *
 * THE BURNS ARE LAID 1.1 S AFTER THE WORLD BEGINS, after every creature's first movement sample, so a first sample
 * (which records "standing" whatever it sees) never falls in a window, and no tick lands on a sample's instant.
 *
 * THE WEARER STANDS 20 M FROM EVERY CREATURE, so nothing depends on what a creature or the player's automatic attack
 * does at close range. Creatures stand at least 5 m apart. A tick cannot critically strike; the roll is pinned at
 * 100 all the same, after the world is made, which sets it.
 */
namespace CataclysmDotOnAMovingTargetTest
{
	constexpr float M = 100.0f;

	/** What every burn states: ten a tick, at one tick a second, for ten seconds. */
	constexpr float PerTick = 10.0f;
	constexpr float BurnSeconds = 10.0f;

	/** The test-only row's "more", and Plague Doctor's two-piece "more". */
	constexpr float MoreWhileTheTargetMoves = 30.0f;
	constexpr float BakedMore = 25.0f;

	/** One step of the clock, and how far a walking creature goes in it. */
	constexpr float StepSeconds = 0.05f;
	constexpr float StepCm = 10.0f;

	/** A player character on a player state at the origin, with health to spare. */
	struct FWearer
	{
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;

		explicit FWearer(UWorld* World)
		{
			ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
			AbilitySystem = PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (Character && PlayerState && AbilitySystem)
			{
				Character->SetPlayerState(PlayerState);
				Character->OnRep_PlayerState();
				AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(),
													   100000.0f);
				AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			}
		}

		bool IsComplete() const { return Character && AbilitySystem; }
	};

	/**
	 * The wearer's stat line, written by hand. `dot_damage` from a base of 100 carries the test-only row, more 30
	 * under `target_is_moving`, and with `bBaked` Plague Doctor's two-piece shape too, more 25 with no condition.
	 * `dot_frequency` and `dot_duration` are 100 with nothing on them, so a tick comes each second whatever the
	 * wearer's class line holds. `SetStatInputs` replaces the whole stat line.
	 */
	void Hold(UCataclysmAbilitySystemComponent* System, bool bBaked)
	{
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatModifier WhileTheTargetMoves;
		WhileTheTargetMoves.Bucket = ECataclysmStatBucket::More;
		WhileTheTargetMoves.Source = ECataclysmModifierSource::Enchantment;
		WhileTheTargetMoves.Value = MoreWhileTheTargetMoves;
		WhileTheTargetMoves.Condition = ECataclysmStatCondition::TargetIsMoving;
		FCataclysmStatInputs& Damage = Inputs.FindOrAdd(FName(TEXT("dot_damage")));
		Damage.Base = 100.0f;
		Damage.Modifiers = {WhileTheTargetMoves};
		if (bBaked)
		{
			FCataclysmStatModifier Baked;
			Baked.Bucket = ECataclysmStatBucket::More;
			Baked.Source = ECataclysmModifierSource::Enchantment;
			Baked.Value = BakedMore;
			Damage.Modifiers.Add(Baked);
		}
		Inputs.FindOrAdd(FName(TEXT("dot_frequency"))).Base = 100.0f;
		Inputs.FindOrAdd(FName(TEXT("dot_duration"))).Base = 100.0f;
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** The ability system of this project's that a creature holds. */
	UCataclysmAbilitySystemComponent* SystemOf(const AActor* Who)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Who));
	}

	/** A creature of the monsters' team standing here, with a thousand health, no regeneration and no shield. */
	ACataclysmEnemyCharacter* CreatureAt(UWorld* World, const FVector& At)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(1000.0f);
			// ONLY ONCE ITS ATTRIBUTE SETS ARE REGISTERED: writing before raises an engine ensure (see `SetHealth`).
			UCataclysmAbilitySystemComponent* System = SystemOf(Made);
			if (System && System->HasAttributeSetForAttribute(UCataclysmVitalAttributeSet::GetHealthRegenAttribute()))
			{
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthRegenAttribute(), 0.0f);
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 0.0f);
			}
		}
		return Made;
	}

	/** The health an actor holds, or nought for one that is gone. */
	float HealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* System = IsValid(Who) ? UCataclysmTargeting::AbilitySystemOf(Who) : nullptr;
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : 0.0f;
	}

	/** A burn of ten a tick, laid by the wearer's character; `bPriced` is `bScalesWithInstigator`. */
	bool LayBurn(const FWearer& Wearer, AActor* Target, bool bPriced = true)
	{
		return UCataclysmSkillEffects::ApplyDamageOverTime(Wearer.Character, Target, PerTick, BurnSeconds,
														   UCataclysmSkillEffects::BurnTag(), bPriced);
	}

	/**
	 * Runs `Seconds` of the clock in steps of 0.05 s: each of `Walking` is put 10 cm further along +Y and told it
	 * moved before each step, and each of `Standing` is told it did not. Answers what each creature lost, `Walking`
	 * first and then `Standing`, in the order given.
	 */
	TArray<float> Pace(UWorld* World, const TArray<ACataclysmEnemyCharacter*>& Walking,
					   const TArray<ACataclysmEnemyCharacter*>& Standing, float Seconds)
	{
		TArray<float> Before;
		for (const ACataclysmEnemyCharacter* One : Walking)
		{
			Before.Add(HealthOf(One));
		}
		for (const ACataclysmEnemyCharacter* One : Standing)
		{
			Before.Add(HealthOf(One));
		}
		const int32 Steps = FMath::RoundToInt(Seconds / StepSeconds);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			for (ACataclysmEnemyCharacter* One : Walking)
			{
				if (UCataclysmAbilitySystemComponent* System = IsValid(One) ? SystemOf(One) : nullptr)
				{
					One->SetActorLocation(One->GetActorLocation() + FVector(0.0f, StepCm, 0.0f));
					System->NoteMovedMetres(StepCm / M);
				}
			}
			for (ACataclysmEnemyCharacter* One : Standing)
			{
				if (UCataclysmAbilitySystemComponent* System = IsValid(One) ? SystemOf(One) : nullptr)
				{
					System->NoteDidNotMove();
				}
			}
			CataclysmTestWorld::RunClock(World, StepSeconds, StepSeconds);
		}
		TArray<float> Lost;
		int32 Index = 0;
		for (const ACataclysmEnemyCharacter* One : Walking)
		{
			Lost.Add(Before[Index++] - HealthOf(One));
		}
		for (const ACataclysmEnemyCharacter* One : Standing)
		{
			Lost.Add(Before[Index++] - HealthOf(One));
		}
		return Lost;
	}

	/** Whether a creature reads as moving now. */
	bool ReadsAsMoving(const AActor* Who)
	{
		const UCataclysmAbilitySystemComponent* System = SystemOf(Who);
		return System && System->IsMoving();
	}

	/** The world's first 1.1 s with every creature standing: each one's first movement sample is taken. */
	void Settle(UWorld* World, const TArray<ACataclysmEnemyCharacter*>& Everyone)
	{
		Pace(World, {}, Everyone, 1.1f);
	}

	/** Where the creatures of every scene stand. The wearer is at the origin. */
	const FVector FirstAt(20.0f * M, 0.0f, 0.0f);
	const FVector SecondAt(25.0f * M, 0.0f, 0.0f);
	const FVector ThirdAt(30.0f * M, 0.0f, 0.0f);
}

#define CATACLYSM_DOT_MOVING_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

/**
 * TWO CREATURES, ONE BURN EACH FROM ONE WEARER: the control stands, the other walks. In the window holding the first
 * tick, the walker loses 1.3 times what the control loses. The set-up checks first: both burns are laid, the walker
 * reads as moving and the control as standing at the window's end, and the control lost something.
 */
CATACLYSM_DOT_MOVING_TEST(FCataclysmDotOnAMovingTargetMoreTest,
	"Cataclysm.DotOnAMovingTarget.AMarkedTickOnAMovingEnemyIsWorthThirtyPerCentMoreThanOnAStillOne")
{
	using namespace CataclysmDotOnAMovingTargetTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	FWearer Wearer(World);
	ACataclysmEnemyCharacter* Still = CreatureAt(World, FirstAt);
	ACataclysmEnemyCharacter* Walker = CreatureAt(World, SecondAt);
	if (!TestTrue(TEXT("set-up: the wearer"), Wearer.IsComplete())
		|| !TestNotNull(TEXT("set-up: creature 0, the control"), Still)
		|| !TestNotNull(TEXT("set-up: creature 1, the walker"), Walker))
	{
		return false;
	}
	Hold(Wearer.AbilitySystem, /*bBaked=*/false);
	Settle(World, {Still, Walker});

	if (!TestTrue(TEXT("set-up: the control's burn is laid"), LayBurn(Wearer, Still))
		|| !TestTrue(TEXT("set-up: the walker's burn is laid"), LayBurn(Wearer, Walker)))
	{
		return false;
	}
	Pace(World, {}, {Still, Walker}, 0.5f);
	const TArray<float> Lost = Pace(World, {Walker}, {Still}, 1.0f);
	TestTrue(TEXT("set-up: the walker reads as moving at the window's end"), ReadsAsMoving(Walker));
	TestFalse(TEXT("set-up: the control reads as standing at the window's end"), ReadsAsMoving(Still));
	if (!TestTrue(FString::Printf(TEXT("control: the still creature lost one tick, %.3f"), Lost[1]), Lost[1] > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the same tick on the moving creature is 1.3 times the still one's"), Lost[0] / Lost[1], 1.3f,
			  0.001f);
	return true;
}

/**
 * ONE WALKER WHO STOPS AND STARTS AGAIN, beside a control that stands throughout. First tick walking: 1.3 times the
 * control. Second tick standing: the control's figure. Third tick walking again: 1.3 times. So the condition is judged
 * on each tick, not when the burn was laid (while it stood).
 */
CATACLYSM_DOT_MOVING_TEST(FCataclysmDotOnAMovingTargetStopsTest,
	"Cataclysm.DotOnAMovingTarget.AnEnemyThatStopsTakesTheStillFigureOnItsNextTickAndTheMoreAgainWhenItWalks")
{
	using namespace CataclysmDotOnAMovingTargetTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	FWearer Wearer(World);
	ACataclysmEnemyCharacter* Still = CreatureAt(World, FirstAt);
	ACataclysmEnemyCharacter* Walker = CreatureAt(World, SecondAt);
	if (!TestTrue(TEXT("set-up: the wearer"), Wearer.IsComplete())
		|| !TestNotNull(TEXT("set-up: creature 0, the control"), Still)
		|| !TestNotNull(TEXT("set-up: creature 1, the walker"), Walker))
	{
		return false;
	}
	Hold(Wearer.AbilitySystem, /*bBaked=*/false);
	Settle(World, {Still, Walker});

	if (!TestTrue(TEXT("set-up: the control's burn is laid"), LayBurn(Wearer, Still))
		|| !TestTrue(TEXT("set-up: the walker's burn is laid while it stands"), LayBurn(Wearer, Walker)))
	{
		return false;
	}
	Pace(World, {}, {Still, Walker}, 0.5f);

	const TArray<float> First = Pace(World, {Walker}, {Still}, 1.0f);
	TestTrue(TEXT("set-up: the walker reads as moving at the first window's end"), ReadsAsMoving(Walker));
	if (!TestTrue(TEXT("control: the still creature lost its first tick"), First[1] > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("first tick, walking: 1.3 times the control's"), First[0] / First[1], 1.3f, 0.001f);

	const TArray<float> Second = Pace(World, {}, {Walker, Still}, 1.0f);
	TestFalse(TEXT("set-up: the walker reads as standing at the second window's end"), ReadsAsMoving(Walker));
	if (!TestTrue(TEXT("control: the still creature lost its second tick"), Second[1] > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("second tick, stopped: the control's figure"), Second[0] / Second[1], 1.0f, 0.001f);

	const TArray<float> Third = Pace(World, {Walker}, {Still}, 1.0f);
	TestTrue(TEXT("set-up: the walker reads as moving at the third window's end"), ReadsAsMoving(Walker));
	if (!TestTrue(TEXT("control: the still creature lost its third tick"), Third[1] > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("third tick, walking again: 1.3 times the control's"), Third[0] / Third[1], 1.3f, 0.001f);
	return true;
}

/**
 * Q2'S TRAP: THE WEARER ALSO HOLDS AN UNCONDITIONED `dot_damage` MORE 25, PLAGUE DOCTOR'S TWO-PIECE SHAPE, which
 * `DamageOverTimeNumbers` bakes into the burn when it is laid. Three creatures: the base is given a burn laid with
 * none of the wearer's stats (`bScalesWithInstigator` off), so its tick is the stated ten after the creature's
 * defences; the still one and the walker are given priced burns. Control: the still creature's tick is 1.25 times the
 * base. The walker's is 1.25 x 1.3 = 1.625 times the base, and not 1.25 x 1.25 x 1.3 = 2.03125: the ratio asked at the
 * tick cancels the 25 that the figure already holds.
 */
CATACLYSM_DOT_MOVING_TEST(FCataclysmDotOnAMovingTargetBakedMoreTest,
	"Cataclysm.DotOnAMovingTarget.ABakedUnconditionedMoreIsNotCountedTwiceOnAMovingEnemy")
{
	using namespace CataclysmDotOnAMovingTargetTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	FWearer Wearer(World);
	ACataclysmEnemyCharacter* Base = CreatureAt(World, FirstAt);
	ACataclysmEnemyCharacter* Still = CreatureAt(World, SecondAt);
	ACataclysmEnemyCharacter* Walker = CreatureAt(World, ThirdAt);
	if (!TestTrue(TEXT("set-up: the wearer"), Wearer.IsComplete())
		|| !TestNotNull(TEXT("set-up: creature 0, the base"), Base)
		|| !TestNotNull(TEXT("set-up: creature 1, the still one"), Still)
		|| !TestNotNull(TEXT("set-up: creature 2, the walker"), Walker))
	{
		return false;
	}
	Hold(Wearer.AbilitySystem, /*bBaked=*/true);
	Settle(World, {Base, Still, Walker});

	if (!TestTrue(TEXT("set-up: the base's burn is laid unpriced"), LayBurn(Wearer, Base, /*bPriced=*/false))
		|| !TestTrue(TEXT("set-up: the still one's burn is laid"), LayBurn(Wearer, Still))
		|| !TestTrue(TEXT("set-up: the walker's burn is laid"), LayBurn(Wearer, Walker)))
	{
		return false;
	}
	Pace(World, {}, {Base, Still, Walker}, 0.5f);
	const TArray<float> Lost = Pace(World, {Walker}, {Base, Still}, 1.0f);
	TestTrue(TEXT("set-up: the walker reads as moving at the window's end"), ReadsAsMoving(Walker));
	if (!TestTrue(TEXT("control: the base lost one tick"), Lost[1] > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("control: the still creature's priced tick holds the baked 25% more, 1.25 times the base"),
			  Lost[2] / Lost[1], 1.25f, 0.001f);
	TestEqual(TEXT("the walker's tick is 1.25 x 1.3 = 1.625 times the base"), Lost[0] / Lost[1], 1.625f, 0.001f);
	TestFalse(TEXT("and not 1.25 x 1.25 x 1.3 = 2.03125: the baked 25 is not counted twice"),
			  FMath::IsNearlyEqual(Lost[0] / Lost[1], 2.03125f, 0.01f));
	return true;
}

/**
 * THE MARKER'S CONTROL: A SPREAD COPY GAINS NOTHING. The wearer's priced burn is laid on the original's carrier, and
 * at once a spread copy of it (`UCataclysmSkillEffects::ApplySpreadCopy`, which lays it with
 * `bScalesWithInstigator` off, as every spread copy is laid) is put on a second creature in the wearer's name. Both
 * walk; a third creature with a priced burn stands as the control. The original's tick is 1.3 times the control's;
 * the copy's is the control's, though its carrier walks and its instigator is the wearer.
 */
CATACLYSM_DOT_MOVING_TEST(FCataclysmDotOnAMovingTargetSpreadCopyTest,
	"Cataclysm.DotOnAMovingTarget.ASpreadCopyOnAMovingEnemyGainsNothingWhileTheOriginalDoes")
{
	using namespace CataclysmDotOnAMovingTargetTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	FWearer Wearer(World);
	ACataclysmEnemyCharacter* Still = CreatureAt(World, FirstAt);
	ACataclysmEnemyCharacter* Original = CreatureAt(World, SecondAt);
	ACataclysmEnemyCharacter* Copied = CreatureAt(World, ThirdAt);
	if (!TestTrue(TEXT("set-up: the wearer"), Wearer.IsComplete())
		|| !TestNotNull(TEXT("set-up: creature 0, the control"), Still)
		|| !TestNotNull(TEXT("set-up: creature 1, the original's carrier"), Original)
		|| !TestNotNull(TEXT("set-up: creature 2, the copy's carrier"), Copied))
	{
		return false;
	}
	Hold(Wearer.AbilitySystem, /*bBaked=*/false);
	Settle(World, {Still, Original, Copied});

	if (!TestTrue(TEXT("set-up: the control's burn is laid"), LayBurn(Wearer, Still))
		|| !TestTrue(TEXT("set-up: the original burn is laid"), LayBurn(Wearer, Original)))
	{
		return false;
	}
	UCataclysmSkillEffects::FRunningAilment Running;
	if (!TestTrue(TEXT("set-up: the original reads as a running burn"),
				  UCataclysmSkillEffects::RunningAilmentOn(Original, UCataclysmSkillEffects::BurnTag(), Running))
		|| !TestTrue(TEXT("set-up: the copy is put on the second creature in the wearer's name"),
					 UCataclysmSkillEffects::ApplySpreadCopy(Wearer.Character, Copied, Running))
		|| !TestTrue(TEXT("set-up: the copy's carrier burns"),
					 UCataclysmSkillEffects::HasTag(Copied, UCataclysmSkillEffects::BurnTag())))
	{
		return false;
	}
	Pace(World, {}, {Still, Original, Copied}, 0.5f);
	const TArray<float> Lost = Pace(World, {Original, Copied}, {Still}, 1.0f);
	TestTrue(TEXT("set-up: the original's carrier reads as moving at the window's end"), ReadsAsMoving(Original));
	TestTrue(TEXT("set-up: the copy's carrier reads as moving at the window's end"), ReadsAsMoving(Copied));
	if (!TestTrue(TEXT("control: the still creature lost one tick"), Lost[2] > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the original's tick on its moving carrier is 1.3 times the control's"), Lost[0] / Lost[2], 1.3f,
			  0.001f);
	TestEqual(TEXT("the spread copy's tick on its moving carrier is the control's: a copy is not priced by its applier"),
			  Lost[1] / Lost[2], 1.0f, 0.001f);
	return true;
}

#undef CATACLYSM_DOT_MOVING_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
