// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmDebuffs.h"
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
 * Issue #918, ruled 2026-10-31: "Deals a base of 20 damage a second for 5 seconds, and ticks only while the affected
 * target is moving, stopping after 4 seconds of movement." The gate is in `UCataclysmVitalAttributeSet`, where a tick
 * lands. A gated tick is no tick at all: no damage, and no hit notice.
 *
 * THE BLEEDS ARE REAL AND SO ARE THEIR TICKS. Each is laid by `UCataclysmSkillEffects::ApplyDamageOverTime`, and the
 * world's clock is moved in steps of 0.05 s by `CataclysmTestWorld::RunClock`, which fires the engine's own period
 * timers and the creatures' own movement samples (`UCataclysmMovement::SampleStep`, every 0.25 s).
 *
 * A CREATURE IS MOVED BY BOTH MEANS THE GAME AND ITS TESTS USE, on every step, as
 * `CataclysmDotOnAMovingTargetTests.cpp` moves them: it is put 10 cm further along +Y, which the sampler reads as
 * walking (its threshold is 5 cm a sample), and it is told so with `NoteMovedMetres`. A creature standing is told
 * `NoteDidNotMove` on every step and is not moved. So the reading at a tick is the one the step set, whether or not a
 * sample falls inside the step. EACH TEST ASSERTS THE READING IT SET, as a set-up check.
 *
 * THE BLEEDS ARE LAID 1.1 S AFTER THE WORLD BEGINS, after every creature's first movement sample, which records
 * "standing" whatever it sees.
 *
 * THE APPLIER STANDS 20 M FROM EVERY CREATURE, so nothing depends on what a creature or the player's automatic attack
 * does at close range. Creatures stand 5 m apart.
 *
 * NO FIGURE ASSUMES A CREATURE'S DEFENCES. A figure is compared with another creature's, or with another tick on the
 * same creature, or is a count of hit notices.
 */
namespace CataclysmBleedMovementTest
{
	constexpr float M = 100.0f;

	/** One step of the clock, and how far a walking creature goes in it. */
	constexpr float StepSeconds = 0.05f;
	constexpr float StepCm = 10.0f;

	/** What every bleed and burn here states: ten a tick. */
	constexpr float PerTick = 10.0f;

	/** A player character on a player state at the origin: whoever lays the bleeds. */
	struct FBleedApplier
	{
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;

		explicit FBleedApplier(UWorld* World)
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

	/** The ability system of this project's that a creature holds. */
	UCataclysmAbilitySystemComponent* BleedSystemOf(const AActor* Who)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Who));
	}

	/** A creature of the monsters' team standing here, with a thousand health, no regeneration and no shield. */
	ACataclysmEnemyCharacter* BleedCreatureAt(UWorld* World, const FVector& At)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(1000.0f);
			// ONLY ONCE ITS ATTRIBUTE SETS ARE REGISTERED: writing before raises an engine ensure (see `SetHealth`).
			UCataclysmAbilitySystemComponent* System = BleedSystemOf(Made);
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
	float BleedHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* System = IsValid(Who) ? UCataclysmTargeting::AbilitySystemOf(Who) : nullptr;
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : 0.0f;
	}

	/** Whether this actor carries this tag now. */
	bool CarriesTag(const AActor* Who, const FGameplayTag& Tag)
	{
		const UCataclysmAbilitySystemComponent* System = BleedSystemOf(Who);
		return System && System->HasMatchingGameplayTag(Tag);
	}

	/** Whether a creature reads as moving now. */
	bool BleedReadsAsMoving(const AActor* Who)
	{
		const UCataclysmAbilitySystemComponent* System = BleedSystemOf(Who);
		return System && System->IsMoving();
	}

	/**
	 * Runs `Seconds` of the clock in steps of 0.05 s: each of `Walking` is put 10 cm further along +Y and told it
	 * moved before each step, and each of `Standing` is told it did not. Answers what each creature lost, `Walking`
	 * first and then `Standing`, in the order given.
	 */
	TArray<float> PaceTheBled(UWorld* World, const TArray<ACataclysmEnemyCharacter*>& Walking,
							  const TArray<ACataclysmEnemyCharacter*>& Standing, float Seconds)
	{
		TArray<float> Before;
		for (const ACataclysmEnemyCharacter* One : Walking)
		{
			Before.Add(BleedHealthOf(One));
		}
		for (const ACataclysmEnemyCharacter* One : Standing)
		{
			Before.Add(BleedHealthOf(One));
		}
		const int32 Steps = FMath::RoundToInt(Seconds / StepSeconds);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			for (ACataclysmEnemyCharacter* One : Walking)
			{
				if (UCataclysmAbilitySystemComponent* System = IsValid(One) ? BleedSystemOf(One) : nullptr)
				{
					One->SetActorLocation(One->GetActorLocation() + FVector(0.0f, StepCm, 0.0f));
					System->NoteMovedMetres(StepCm / M);
				}
			}
			for (ACataclysmEnemyCharacter* One : Standing)
			{
				if (UCataclysmAbilitySystemComponent* System = IsValid(One) ? BleedSystemOf(One) : nullptr)
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
			Lost.Add(Before[Index++] - BleedHealthOf(One));
		}
		for (const ACataclysmEnemyCharacter* One : Standing)
		{
			Lost.Add(Before[Index++] - BleedHealthOf(One));
		}
		return Lost;
	}

	/**
	 * The hit notices `UCataclysmCombatEvents` sends, counted by the character struck and by whether the blow granted
	 * a tag: a tick of a bleed grants the bleed's, a tick of a burn the burn's. A notice is what every listener for a
	 * blow taken hears, the combat log and the worn rows' events among them.
	 */
	struct FBleedNoticeCount
	{
		struct FOne
		{
			const AActor* Target = nullptr;
			FGameplayTagContainer Granted;
		};
		TArray<FOne> Heard;
		FDelegateHandle Handle;
		UCataclysmCombatEvents* Events = nullptr;

		explicit FBleedNoticeCount(UWorld* World)
		{
			Events = UCataclysmCombatEvents::In(World);
			if (Events)
			{
				Handle = Events->OnHit.AddLambda([this](const FCataclysmHitNotice& Notice)
				{
					FOne One;
					One.Target = Notice.Target;
					if (Notice.GrantedTags)
					{
						One.Granted = *Notice.GrantedTags;
					}
					Heard.Add(MoveTemp(One));
				});
			}
		}

		~FBleedNoticeCount()
		{
			if (Events)
			{
				Events->OnHit.Remove(Handle);
			}
		}

		/** How many notices said a blow granting `Tag` landed on `Target`. */
		int32 On(const AActor* Target, const FGameplayTag& Tag) const
		{
			int32 Count = 0;
			for (const FOne& One : Heard)
			{
				if (One.Target == Target && One.Granted.HasTag(Tag))
				{
					++Count;
				}
			}
			return Count;
		}
	};

	/** Where the creatures stand. The applier is at the origin. */
	const FVector BleedFirstAt(20.0f * M, 0.0f, 0.0f);
	const FVector BleedSecondAt(25.0f * M, 0.0f, 0.0f);
}

#define CATACLYSM_BLEED_MOVEMENT_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

/**
 * A CREATURE THAT STANDS STILL FOR THE WHOLE OF ITS BLEED TAKES NOTHING FROM IT, AND THE BLEED STILL RUNS OUT. Two
 * creatures stand for 5.5 s. The first carries a bleed of ten a tick for 5 s and a burn of ten a tick for 5 s; the
 * second, the control, carries the same burn and no bleed. The first loses exactly what the control loses, so the
 * bleed took nothing. Its burn's ticks were announced as the control's were, and its bleed's were not announced at all:
 * a gated tick is no tick. The bleed is gone at the end, so its window ran while the creature stood, and none of the
 * four seconds of movement was used.
 */
CATACLYSM_BLEED_MOVEMENT_TEST(FCataclysmBleedStandingStillTest,
	"Cataclysm.Ailments.ABleedDealsNothingToATargetStandingStill")
{
	using namespace CataclysmBleedMovementTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	FBleedApplier Applier(World);
	ACataclysmEnemyCharacter* Bled = BleedCreatureAt(World, BleedFirstAt);
	ACataclysmEnemyCharacter* Control = BleedCreatureAt(World, BleedSecondAt);
	if (!TestTrue(TEXT("set-up: the bleed and burn tags"), Bleed.IsValid() && Burn.IsValid())
		|| !TestTrue(TEXT("set-up: the applier"), Applier.IsComplete())
		|| !TestNotNull(TEXT("set-up: creature 0, bleeding and burning"), Bled)
		|| !TestNotNull(TEXT("set-up: creature 1, the control, burning only"), Control)
		|| !TestNotNull(TEXT("set-up: creature 0's ability system"), BleedSystemOf(Bled)))
	{
		return false;
	}
	PaceTheBled(World, {}, {Bled, Control}, 1.1f);

	FBleedNoticeCount Notices(World);
	if (!TestNotNull(TEXT("set-up: the hit notices"), Notices.Events)
		|| !TestTrue(TEXT("set-up: creature 0's bleed is laid"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Character, Bled, PerTick, 5.0f, Bleed,
														/*bScalesWithInstigator=*/false))
		|| !TestTrue(TEXT("set-up: creature 0's burn is laid"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Character, Bled, PerTick, 5.0f, Burn,
														/*bScalesWithInstigator=*/false))
		|| !TestTrue(TEXT("set-up: the control's burn is laid"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Character, Control, PerTick, 5.0f, Burn,
														/*bScalesWithInstigator=*/false))
		|| !TestTrue(TEXT("set-up: creature 0 carries the bleed"), CarriesTag(Bled, Bleed)))
	{
		return false;
	}

	const TArray<float> Lost = PaceTheBled(World, {}, {Bled, Control}, 5.5f);
	TestFalse(TEXT("set-up: creature 0 reads as standing at the end"), BleedReadsAsMoving(Bled));
	TestFalse(TEXT("set-up: the control reads as standing at the end"), BleedReadsAsMoving(Control));
	if (!TestTrue(FString::Printf(TEXT("control: the burn took %.3f from the control"), Lost[1]), Lost[1] > 0.0f)
		|| !TestTrue(TEXT("control: the control's burn ticks were announced"), Notices.On(Control, Burn) > 0))
	{
		return false;
	}
	TestEqual(FString::Printf(TEXT("creature 0 lost only what its burn took (%.3f against the control's %.3f)"),
							  Lost[0], Lost[1]),
			  Lost[0], Lost[1], 0.001f);
	TestEqual(TEXT("creature 0's burn ticks were announced as the control's were"),
			  Notices.On(Bled, Burn), Notices.On(Control, Burn));
	TestEqual(TEXT("and not one of its bleed's ticks was announced"), Notices.On(Bled, Bleed), 0);
	TestFalse(TEXT("the bleed's window ran out while the creature stood"), CarriesTag(Bled, Bleed));
	TestEqual(TEXT("and it used none of the four seconds of movement"),
			  BleedSystemOf(Bled)->BleedMovingSeconds, 0.0f, 0.0001f);
	return true;
}

/**
 * A BLEED ON A CREATURE THAT NEVER STOPS WALKING DEALS FOUR TICKS AND NO MORE, AND STAYS ON IT. The bleed is ten a tick
 * for 8 s, longer than the designed 5 s, so a fifth to an eighth tick fall due inside its window and only the four
 * seconds of movement can stop them; a duration stat lengthens a real bleed the same way. The creature walks from the
 * moment the bleed is laid. Eight one-second windows, each holding one tick: the first four each lose the same and
 * more than nothing, the last four lose nothing, four ticks are announced in all, and the count stands at four
 * seconds. At the end of the fifth window, 5.5 s after the bleed was laid, it is still carried with its ticks
 * stopped; once its 8 s have run, it is gone.
 */
CATACLYSM_BLEED_MOVEMENT_TEST(FCataclysmBleedStopsAfterFourSecondsTest,
	"Cataclysm.Ailments.ABleedOnAMovingTargetStopsAfterFourSecondsOfMovement")
{
	using namespace CataclysmBleedMovementTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	FBleedApplier Applier(World);
	ACataclysmEnemyCharacter* Walker = BleedCreatureAt(World, BleedFirstAt);
	if (!TestTrue(TEXT("set-up: the bleed tag"), Bleed.IsValid())
		|| !TestTrue(TEXT("set-up: the applier"), Applier.IsComplete())
		|| !TestNotNull(TEXT("set-up: the walker"), Walker)
		|| !TestNotNull(TEXT("set-up: the walker's ability system"), BleedSystemOf(Walker)))
	{
		return false;
	}
	PaceTheBled(World, {}, {Walker}, 1.1f);

	FBleedNoticeCount Notices(World);
	if (!TestNotNull(TEXT("set-up: the hit notices"), Notices.Events)
		|| !TestTrue(TEXT("set-up: the bleed is laid"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Character, Walker, PerTick, 8.0f, Bleed,
														/*bScalesWithInstigator=*/false)))
	{
		return false;
	}

	// HALF A SECOND OF WALKING, WITH NO TICK IN IT, then eight windows of a second each holding one tick.
	PaceTheBled(World, {Walker}, {}, 0.5f);
	TArray<float> Windows;
	for (int32 Window = 0; Window < 8; ++Window)
	{
		Windows.Add(PaceTheBled(World, {Walker}, {}, 1.0f)[0]);
		TestTrue(FString::Printf(TEXT("set-up: the walker reads as moving at the end of window %d"), Window + 1),
				 BleedReadsAsMoving(Walker));
		if (Window == 4)
		{
			TestTrue(TEXT("5.5 s after it was laid, a tick after its last, the bleed is still carried"),
					 CarriesTag(Walker, Bleed));
		}
	}

	if (!TestTrue(FString::Printf(TEXT("control: the first tick took %.3f"), Windows[0]), Windows[0] > 0.0f))
	{
		return false;
	}
	for (int32 Window = 1; Window < 4; ++Window)
	{
		TestEqual(FString::Printf(TEXT("tick %d, inside the four seconds, took what the first did"), Window + 1),
				  Windows[Window], Windows[0], 0.001f);
	}
	for (int32 Window = 4; Window < 8; ++Window)
	{
		TestEqual(FString::Printf(TEXT("tick %d, after four seconds of movement, took nothing"), Window + 1),
				  Windows[Window], 0.0f, 0.001f);
	}
	TestEqual(TEXT("four of the bleed's ticks were announced, and no more"), Notices.On(Walker, Bleed), 4);
	TestEqual(TEXT("the count stands at the four seconds"),
			  BleedSystemOf(Walker)->BleedMovingSeconds, UCataclysmDebuffs::BleedMovingSecondsCap, 0.001f);
	TestFalse(TEXT("and once its 8 s have run, the bleed is gone"), CarriesTag(Walker, Bleed));
	return true;
}

/**
 * THE FOUR ARE SECONDS OF MOVEMENT AND NOT FOUR TICKS, ruled 2026-10-31, so the tick-frequency stat still works. The
 * applier's `dot_frequency` is 200, written by hand from a base of 100 with nothing on it, so its bleed ticks every
 * half second; `dot_damage` and `dot_duration` are 100, unchanged. The bleed is ten a tick for 8 s on a creature that
 * walks throughout: sixteen ticks fall due, and eight land, the ones in the first four seconds.
 */
CATACLYSM_BLEED_MOVEMENT_TEST(FCataclysmBleedSecondsNotTicksTest,
	"Cataclysm.Ailments.ABleedTickingTwiceASecondStopsAfterFourSecondsOfMovementNotAfterFourTicks")
{
	using namespace CataclysmBleedMovementTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	FBleedApplier Applier(World);
	ACataclysmEnemyCharacter* Walker = BleedCreatureAt(World, BleedFirstAt);
	UCataclysmAbilitySystemComponent* WalkerSystem = BleedSystemOf(Walker);
	if (!TestTrue(TEXT("set-up: the bleed tag"), Bleed.IsValid())
		|| !TestTrue(TEXT("set-up: the applier"), Applier.IsComplete())
		|| !TestNotNull(TEXT("set-up: the walker's ability system"), WalkerSystem))
	{
		return false;
	}
	{
		TMap<FName, FCataclysmStatInputs> Inputs;
		Inputs.FindOrAdd(FName(TEXT("dot_frequency"))).Base = 200.0f;
		Inputs.FindOrAdd(FName(TEXT("dot_damage"))).Base = 100.0f;
		Inputs.FindOrAdd(FName(TEXT("dot_duration"))).Base = 100.0f;
		Applier.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
	}
	PaceTheBled(World, {}, {Walker}, 1.1f);

	FBleedNoticeCount Notices(World);
	if (!TestNotNull(TEXT("set-up: the hit notices"), Notices.Events)
		|| !TestTrue(TEXT("set-up: the bleed is laid, priced by the applier's stats"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Character, Walker, PerTick, 8.0f, Bleed,
														/*bScalesWithInstigator=*/true)))
	{
		return false;
	}
	const TArray<FActiveGameplayEffectHandle> Handles = WalkerSystem->GetActiveEffects(
		FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Bleed)));
	const FActiveGameplayEffect* Laid =
		Handles.Num() == 1 ? WalkerSystem->GetActiveGameplayEffect(Handles[0]) : nullptr;
	if (!TestNotNull(TEXT("set-up: one bleed effect"), Laid)
		|| !TestEqual(TEXT("set-up: it ticks every half second"), Laid->GetPeriod(), 0.5f, 0.001f))
	{
		return false;
	}

	PaceTheBled(World, {Walker}, {}, 8.5f);
	TestTrue(TEXT("set-up: the walker reads as moving at the end"), BleedReadsAsMoving(Walker));
	TestEqual(TEXT("eight ticks of half a second were announced, four seconds of movement"),
			  Notices.On(Walker, Bleed), 8);
	TestEqual(TEXT("the count stands at the four seconds"), WalkerSystem->BleedMovingSeconds,
			  UCataclysmDebuffs::BleedMovingSecondsCap, 0.001f);
	return true;
}

#undef CATACLYSM_BLEED_MOVEMENT_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
