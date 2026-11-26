// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMovement.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Tests/CataclysmTestWorld.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * The Shadow's marks and Hollow Step (the Death trees' slice C6e, 2026-11-25, as ruled). Deathmark marks the strongest
 * enemy within 15 metres, keeps the mark until it dies and then moves it to the strongest within 10 metres of the body;
 * Two Marks makes it two; a marked creature takes more from its marker and says "Marked". Hollow Step's movement skill
 * passes through enemies for the move only, never ends inside one, restores on every way out, and hits the line once.
 */

namespace CataclysmShadowMarksTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	constexpr float M = 100.0f;

	/** A character on the player's side at `Where`, with 100 attack damage and a mana pool. */
	ACataclysmEnemyCharacter* MoverAt(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
			Made->SetAttackDamage(100.0f);
			if (UAbilitySystemComponent* System = Made->GetAbilitySystemComponent())
			{
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
				// NO CRITICAL STRIKES, so two uses compared by what a creature lost differ only by what was struck.
				System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 0.0f);
			}
		}
		return Made;
	}

	/** A creature on the monsters' side at `Where` with `Health`, which deals nothing. */
	ACataclysmEnemyCharacter* CreatureAt(UWorld* World, const FVector& Where, float Health = 1000.0f)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(Health);
			Made->SetAttackDamage(0.0f);
		}
		return Made;
	}

	UCataclysmAbilitySystemComponent* SystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	float HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** One flat line per stat named, and nothing else. */
	void FlatLines(UCataclysmAbilitySystemComponent* System, const TArray<TPair<const TCHAR*, float>>& Flats,
				   const TArray<TPair<const TCHAR*, FCataclysmStatModifier>>& Rows = {})
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		for (const TPair<const TCHAR*, float>& Flat : Flats)
		{
			FCataclysmStatModifier Row;
			Row.Bucket = ECataclysmStatBucket::Flat;
			Row.Source = ECataclysmModifierSource::PassiveKeystone;
			Row.Value = Flat.Value;
			Lines.FindOrAdd(FName(Flat.Key)).Modifiers.Add(Row);
		}
		for (const TPair<const TCHAR*, FCataclysmStatModifier>& Row : Rows)
		{
			FCataclysmStatInputs& Line = Lines.FindOrAdd(FName(Row.Key));
			Line.Base = 100.0f;
			Line.Modifiers.Add(Row.Value);
		}
		System->SetStatInputs(MoveTemp(Lines));
	}

	/** A movement skill of `Shape`, granted in the Movement slot at 100% damage. */
	UCataclysmMovementSkill* GrantMove(UCataclysmAbilitySystemComponent* System, const TCHAR* Shape,
									   FGameplayAbilitySpecHandle& OutHandle)
	{
		OutHandle = System->GiveAbilityInSlot(UCataclysmMovementSkill::StaticClass(), ECataclysmAbilitySlot::Movement,
											  /*Level=*/100, System->GetAvatarActor());
		FGameplayAbilitySpec* Spec = OutHandle.IsValid() ? System->FindAbilitySpecFromHandle(OutHandle) : nullptr;
		UCataclysmMovementSkill* Move = Spec ? Cast<UCataclysmMovementSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Move)
		{
			Move->SkillName = TEXT("A test move");
			Move->Params = UCataclysmSkillShapes::ParseParams(Shape);
			Move->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Slot.Movement"));
			Move->DamagePercentOverride = 100.0f;
		}
		return Move;
	}

	/** Use `Move` aimed at `Aim`, as a free repeat aims, since a test has no cursor. */
	bool UseAimedAt(UCataclysmAbilitySystemComponent* System, UCataclysmMovementSkill* Move,
					FGameplayAbilitySpecHandle Handle, const FVector& Aim)
	{
		Move->bFreeRepeat = true;
		Move->FreeRepeatAim = Aim;
		return System->TryActivateAbility(Handle);
	}

	float RadiusOf(const AActor* Actor)
	{
		const ACharacter* Body = Cast<ACharacter>(Actor);
		return Body && Body->GetCapsuleComponent() ? Body->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.0f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowMarksStrongestTest,
	"Cataclysm.ShadowMarks.DeathmarkMarksTheStrongestWithinFifteenMetresAndSaysMarked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A count of one: a creature of 5,000 health at 10 metres is marked rather than one of 1,000 at 5 metres, and one of
 * 9,000 at 20 metres is beyond the reach. The marked creature's status line says "Marked" and the weaker one's does
 * not. With the count taken away, the mark is lifted.
 */
bool FCataclysmShadowMarksStrongestTest::RunTest(const FString&)
{
	using namespace CataclysmShadowMarksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Marker = MoverAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Weak = CreatureAt(World, FVector(5.0f * M, 0, 0), 1000.0f);
	ACataclysmEnemyCharacter* Strong = CreatureAt(World, FVector(0, 10.0f * M, 0), 5000.0f);
	ACataclysmEnemyCharacter* Far = CreatureAt(World, FVector(-20.0f * M, 0, 0), 9000.0f);
	UCataclysmAbilitySystemComponent* System = SystemOf(Marker);
	if (!TestNotNull(TEXT("set-up: a marker and three creatures"), Weak && Strong && Far ? System : nullptr))
	{
		return false;
	}
	FlatLines(System, {{UCataclysmAbilitySystemComponent::ShadowMarksStat, 1.0f}});
	System->MaintainShadowMarks();

	AddInfo(FString::Printf(TEXT("DEATHMARK: weak %s, strong %s, far %s; strong says '%s', weak says '%s'"),
							System->HoldsShadowMarkOn(Weak) ? TEXT("marked") : TEXT("not"),
							System->HoldsShadowMarkOn(Strong) ? TEXT("marked") : TEXT("not"),
							System->HoldsShadowMarkOn(Far) ? TEXT("marked") : TEXT("not"),
							*UCataclysmCombatOverlay::MarkedTextFor(Strong), *UCataclysmCombatOverlay::MarkedTextFor(Weak)));
	TestTrue(TEXT("the strongest within 15 metres is marked"), System->HoldsShadowMarkOn(Strong));
	TestFalse(TEXT("CONTROL: a weaker one nearer is not"), System->HoldsShadowMarkOn(Weak));
	TestFalse(TEXT("CONTROL: a stronger one at 20 metres is beyond the reach"), System->HoldsShadowMarkOn(Far));
	TestEqual(TEXT("one mark"), System->ShadowMarksHeld(), 1);
	TestEqual(TEXT("the marked creature says Marked"), UCataclysmCombatOverlay::MarkedTextFor(Strong), FString(TEXT("Marked")));
	TestEqual(TEXT("CONTROL: the other says nothing"), UCataclysmCombatOverlay::MarkedTextFor(Weak), FString());

	FlatLines(System, {});
	System->MaintainShadowMarks();
	TestEqual(TEXT("with no count, the mark is lifted"), System->ShadowMarksHeld(), 0);
	TestEqual(TEXT("and the label goes with it"), UCataclysmCombatOverlay::MarkedTextFor(Strong), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowMarksStickyTest,
	"Cataclysm.ShadowMarks.AMarkIsKeptUntilItsCreatureDiesThenMovesWithinTenMetresOfIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * As ruled (Q5), a mark is sticky: a stronger creature arriving within reach does not take it. When the marked creature
 * dies, the mark moves to the strongest within 10 metres of the body, not of the marker; with none that near, it is
 * lifted, and the next step chooses within 15 metres of the marker.
 */
bool FCataclysmShadowMarksStickyTest::RunTest(const FString&)
{
	using namespace CataclysmShadowMarksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Marker = MoverAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* First = CreatureAt(World, FVector(12.0f * M, 0, 0), 3000.0f);
	UCataclysmAbilitySystemComponent* System = SystemOf(Marker);
	if (!TestNotNull(TEXT("set-up: a marker and a creature"), First ? System : nullptr))
	{
		return false;
	}
	FlatLines(System, {{UCataclysmAbilitySystemComponent::ShadowMarksStat, 1.0f}});
	System->MaintainShadowMarks();
	TestTrue(TEXT("set-up: the first creature is marked"), System->HoldsShadowMarkOn(First));

	// A STRONGER ONE ARRIVES BESIDE THE MARKER, and another stronger still beside the first creature, 6 metres from it
	// and 18 from the marker.
	ACataclysmEnemyCharacter* Arrived = CreatureAt(World, FVector(-3.0f * M, 0, 0), 8000.0f);
	ACataclysmEnemyCharacter* NearTheBody = CreatureAt(World, FVector(18.0f * M, 0, 0), 4000.0f);
	System->MaintainShadowMarks();
	TestTrue(TEXT("sticky: the first keeps its mark"), System->HoldsShadowMarkOn(First));
	TestFalse(TEXT("CONTROL: the stronger arrival does not take it"), System->HoldsShadowMarkOn(Arrived));

	if (UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(First))
	{
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
	}
	const bool bMovedToNearTheBody = System->HoldsShadowMarkOn(NearTheBody);
	const bool bMovedToArrival = System->HoldsShadowMarkOn(Arrived);

	AddInfo(FString::Printf(TEXT("DEATHMARK ON DEATH: near the body %s, beside the marker %s"),
							bMovedToNearTheBody ? TEXT("marked") : TEXT("not"), bMovedToArrival ? TEXT("marked") : TEXT("not")));
	TestTrue(TEXT("the mark moves to the strongest within 10 metres of the body"), bMovedToNearTheBody);
	TestFalse(TEXT("CONTROL: not to the stronger one 15 metres from the body"), bMovedToArrival);
	TestEqual(TEXT("still one mark"), System->ShadowMarksHeld(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowMarksTwoTest,
	"Cataclysm.ShadowMarks.TwoMarksMarkTwoDifferentCreaturesAndTheMarkedTakeThirtyMore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A count of two: the two strongest within reach, never one twice. Deathmark's damage row by hand, `attack_damage` 30
 * more under `target_marked_by_you`: 130 against a marked creature and 100 against an unmarked one.
 */
bool FCataclysmShadowMarksTwoTest::RunTest(const FString&)
{
	using namespace CataclysmShadowMarksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Marker = MoverAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* A = CreatureAt(World, FVector(5.0f * M, 0, 0), 5000.0f);
	ACataclysmEnemyCharacter* B = CreatureAt(World, FVector(0, 5.0f * M, 0), 4000.0f);
	ACataclysmEnemyCharacter* C = CreatureAt(World, FVector(-5.0f * M, 0, 0), 1000.0f);
	UCataclysmAbilitySystemComponent* System = SystemOf(Marker);
	if (!TestNotNull(TEXT("set-up: a marker and three creatures"), A && B && C ? System : nullptr))
	{
		return false;
	}
	FCataclysmStatModifier More;
	More.Bucket = ECataclysmStatBucket::More;
	More.Source = ECataclysmModifierSource::PassiveKeystone;
	More.Value = 30.0f;
	More.Condition = ECataclysmStatCondition::TargetMarkedByYou;
	FlatLines(System, {{UCataclysmAbilitySystemComponent::ShadowMarksStat, 2.0f}}, {{TEXT("attack_damage"), More}});
	System->MaintainShadowMarks();
	auto Against = [System](const AActor* Target)
	{
		return System->StatForSkill(FName(TEXT("attack_damage")), FGameplayTagContainer(), 0.0f, -1.0f,
									FCataclysmBlowContext(), -1.0f, -1.0f, false, Target);
	};

	AddInfo(FString::Printf(TEXT("TWO MARKS: %d held; damage %.1f against A, %.1f against B, %.1f against C"),
							System->ShadowMarksHeld(), Against(A), Against(B), Against(C)));
	TestEqual(TEXT("two marks"), System->ShadowMarksHeld(), 2);
	TestTrue(TEXT("on the two strongest"), System->HoldsShadowMarkOn(A) && System->HoldsShadowMarkOn(B));
	TestFalse(TEXT("CONTROL: not on the weakest"), System->HoldsShadowMarkOn(C));
	TestEqual(TEXT("a marked creature takes 30% more from its marker"), Against(A), 130.0f, 0.01f);
	TestEqual(TEXT("CONTROL: an unmarked one does not"), Against(C), 100.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHollowStepPassesThroughTest,
	"Cataclysm.ShadowMarks.HollowStepPassesThroughAnEnemyAndHitsItOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Leap of 6 metres aimed past a creature 3 metres ahead on its line. CONTROL: without the stat the sweep stops
 * against the creature, short of 3 metres. With `movement_passes_through_enemies` the mover arrives at 6 metres, the
 * creature on the line is struck, and the mover's moves no longer pass through enemies once the use has ended.
 */
bool FCataclysmHollowStepPassesThroughTest::RunTest(const FString&)
{
	using namespace CataclysmShadowMarksTest;

	float Arrived[2] = {0.0f, 0.0f};
	float Lost[2] = {0.0f, 0.0f};
	bool bStillThrough = true;
	for (int32 WithStat = 0; WithStat < 2; ++WithStat)
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		ACataclysmEnemyCharacter* Mover = MoverAt(World, FVector::ZeroVector);
		ACataclysmEnemyCharacter* InTheWay = CreatureAt(World, FVector(3.0f * M, 0, 0));
		UCataclysmAbilitySystemComponent* System = SystemOf(Mover);
		FGameplayAbilitySpecHandle Handle;
		UCataclysmMovementSkill* Leap = System ? GrantMove(System, TEXT("Mode=Leap; Range=6; Radius=1"), Handle) : nullptr;
		if (!TestNotNull(TEXT("set-up: a mover, a creature and a leap"), InTheWay ? Leap : nullptr))
		{
			return false;
		}
		if (WithStat)
		{
			FlatLines(System, {{UCataclysmMovementSkill::PassesThroughEnemiesStat, 1.0f}});
		}
		const float Before = HealthOf(InTheWay);
		TestTrue(TEXT("the leap was used"), UseAimedAt(System, Leap, Handle, FVector(6.0f * M, 0, 0)));
		Arrived[WithStat] = Mover->GetActorLocation().X;
		Lost[WithStat] = Before - HealthOf(InTheWay);
		if (WithStat)
		{
			bStillThrough = Mover->MovesThroughHostiles();
		}
	}

	AddInfo(FString::Printf(TEXT("HOLLOW STEP: arrived at %.0f cm without, %.0f with; the creature lost %.1f without, %.1f "
								 "with"),
							Arrived[0], Arrived[1], Lost[0], Lost[1]));
	TestTrue(TEXT("CONTROL: without the stat the creature stops the leap short of it"), Arrived[0] < 3.0f * M);
	TestEqual(TEXT("with it the leap arrives at 6 metres"), Arrived[1], 6.0f * M, 5.0f);
	TestTrue(TEXT("and the creature on the line is struck"), Lost[1] > 0.0f);
	TestFalse(TEXT("and once the use has ended, the mover's moves no longer pass through enemies"), bStillThrough);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHollowStepEndsBesideTest,
	"Cataclysm.ShadowMarks.HollowStepEndingInsideAnEnemyEndsBesideItAlongTheLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * As ruled (H1, condition 2): a Leap aimed at the centre of a creature 4 metres ahead, which the move passes into,
 * ends beside it on the line, its capsule touching but not inside the creature's, and short of 4 metres.
 */
bool FCataclysmHollowStepEndsBesideTest::RunTest(const FString&)
{
	using namespace CataclysmShadowMarksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmEnemyCharacter* Mover = MoverAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* AtTheEnd = CreatureAt(World, FVector(4.0f * M, 0, 0));
	UCataclysmAbilitySystemComponent* System = SystemOf(Mover);
	FGameplayAbilitySpecHandle Handle;
	UCataclysmMovementSkill* Leap = System ? GrantMove(System, TEXT("Mode=Leap; Range=6; Radius=1"), Handle) : nullptr;
	if (!TestNotNull(TEXT("set-up: a mover, a creature and a leap"), AtTheEnd ? Leap : nullptr))
	{
		return false;
	}
	FlatLines(System, {{UCataclysmMovementSkill::PassesThroughEnemiesStat, 1.0f}});
	TestTrue(TEXT("the leap was used"), UseAimedAt(System, Leap, Handle, FVector(4.0f * M, 0, 0)));
	const float Apart = FVector::Dist2D(Mover->GetActorLocation(), AtTheEnd->GetActorLocation());
	const float Touching = RadiusOf(Mover) + RadiusOf(AtTheEnd);

	AddInfo(FString::Printf(TEXT("HOLLOW STEP'S END: at %.0f cm, %.1f cm from the creature's centre; the two radii make "
								 "%.1f"),
							Mover->GetActorLocation().X, Apart, Touching));
	TestTrue(TEXT("it ends outside the creature"), Apart >= Touching - 0.5f);
	TestTrue(TEXT("beside it, within one 10 cm step"), Apart <= Touching + 10.5f);
	TestTrue(TEXT("on the near side, short of 4 metres"), Mover->GetActorLocation().X < 4.0f * M);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHollowStepInterruptedTest,
	"Cataclysm.ShadowMarks.AnInterruptedWalkedChargeLeavesTheMoverBlockedByEnemiesAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * As ruled (H1, condition 3): a walked Charge of 2 seconds passes through enemies while it runs; cancelled part way,
 * as a stun cancels it, the mover's moves no longer pass through enemies, and a swept move toward a creature 3 metres
 * away stops against it.
 */
bool FCataclysmHollowStepInterruptedTest::RunTest(const FString&)
{
	using namespace CataclysmShadowMarksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmEnemyCharacter* Mover = MoverAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Ahead = CreatureAt(World, FVector(0, 3.0f * M, 0));
	UCataclysmAbilitySystemComponent* System = SystemOf(Mover);
	FGameplayAbilitySpecHandle Handle;
	UCataclysmMovementSkill* Charge =
		System ? GrantMove(System, TEXT("Mode=Charge; Range=10; Radius=1; Duration=2"), Handle) : nullptr;
	if (!TestNotNull(TEXT("set-up: a mover, a creature and a walked charge"), Ahead ? Charge : nullptr))
	{
		return false;
	}
	FlatLines(System, {{UCataclysmMovementSkill::PassesThroughEnemiesStat, 1.0f}});
	TestTrue(TEXT("the charge was used"), UseAimedAt(System, Charge, Handle, FVector(10.0f * M, 0, 0)));
	const bool bWhileRunning = Mover->MovesThroughHostiles();
	System->CancelAbilityHandle(Handle);
	const bool bAfterCancel = Mover->MovesThroughHostiles();
	const FVector Was = Mover->GetActorLocation();
	UCataclysmMovement::MoveWithoutSticking(Mover, Ahead->GetActorLocation() - Was);
	const float Apart = FVector::Dist2D(Mover->GetActorLocation(), Ahead->GetActorLocation());

	AddInfo(FString::Printf(TEXT("INTERRUPTED CHARGE: through %s while running, %s after the cancel; a move toward the "
								 "creature stopped %.1f cm from its centre"),
							bWhileRunning ? TEXT("yes") : TEXT("no"), bAfterCancel ? TEXT("yes") : TEXT("no"), Apart));
	TestTrue(TEXT("while the walked charge runs, the mover passes through enemies"), bWhileRunning);
	TestFalse(TEXT("cancelled part way, it no longer does"), bAfterCancel);
	TestTrue(TEXT("so a move toward a creature stops against it"), Apart >= RadiusOf(Mover) + RadiusOf(Ahead) - 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHollowStepOnceTest,
	"Cataclysm.ShadowMarks.HollowStepsLineHitDoesNotStrikeWhatTheSkillsOwnHitStruck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * As ruled (H2), once a use: a Blink's own hit strikes a creature standing at its end; the line hit skips it, so it
 * loses the same with the stat as without.
 */
bool FCataclysmHollowStepOnceTest::RunTest(const FString&)
{
	using namespace CataclysmShadowMarksTest;

	float Lost[2] = {0.0f, 0.0f};
	for (int32 WithStat = 0; WithStat < 2; ++WithStat)
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		ACataclysmEnemyCharacter* Mover = MoverAt(World, FVector::ZeroVector);
		ACataclysmEnemyCharacter* AtTheEnd = CreatureAt(World, FVector(6.0f * M, 0, 0));
		UCataclysmAbilitySystemComponent* System = SystemOf(Mover);
		FGameplayAbilitySpecHandle Handle;
		UCataclysmMovementSkill* Blink =
			System ? GrantMove(System, TEXT("Mode=Blink; Range=5; Radius=2"), Handle) : nullptr;
		if (!TestNotNull(TEXT("set-up: a mover, a creature and a blink"), AtTheEnd ? Blink : nullptr))
		{
			return false;
		}
		if (WithStat)
		{
			FlatLines(System, {{UCataclysmMovementSkill::PassesThroughEnemiesStat, 1.0f}});
		}
		const float Before = HealthOf(AtTheEnd);
		TestTrue(TEXT("the blink was used"), UseAimedAt(System, Blink, Handle, FVector(5.0f * M, 0, 0)));
		Lost[WithStat] = Before - HealthOf(AtTheEnd);
	}

	AddInfo(FString::Printf(TEXT("ONCE A USE: the creature at the end lost %.2f without the stat, %.2f with"), Lost[0],
							Lost[1]));
	TestTrue(TEXT("set-up: the blink's own hit struck it"), Lost[0] > 0.0f);
	TestEqual(TEXT("with the stat it is struck no more"), Lost[1], Lost[0], 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
