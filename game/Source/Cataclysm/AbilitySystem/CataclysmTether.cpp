// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmTether.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Cataclysm.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

ACataclysmTether::ACataclysmTether()
{
	// Nothing to do per frame. It measures itself on a timer four times a
	// second, and a tick would run that fifteen times more often for the same
	// result. The same reason ACataclysmGroundZone gives.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Anchor = CreateDefaultSubobject<USceneComponent>(TEXT("Anchor"));
	SetRootComponent(Anchor);
}

ACataclysmTether* ACataclysmTether::Bind(AActor* Caster,
										 const TArray<AActor*>& Pair,
										 float MaxSeparationCm,
										 float DurationSeconds,
										 float LineHalfWidthCm)
{
	if (!IsValid(Caster) || Pair.Num() != 2 || MaxSeparationCm <= 0.0f
		|| DurationSeconds <= 0.0f)
	{
		return nullptr;
	}

	if (!IsValid(Pair[0]) || !IsValid(Pair[1]) || Pair[0] == Pair[1])
	{
		return nullptr;
	}

	UWorld* World = Caster->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Caster;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// SPAWNED DEFERRED, AND THIS IS THE WHOLE REASON. `SpawnActor` runs
	// `BeginPlay` before it returns, so anything written onto the actor on the
	// line after it is written too late for `BeginPlay` to see. This actor's
	// `BeginPlay` starts the repeating check, and a check that ran before the
	// two ends were set would measure a line between two null pointers and
	// destroy itself immediately.
	//
	// `ACataclysmGroundZone::SpawnAlong` HAS EXACTLY THIS FAULT AND STILL HAS IT.
	// It sets its radius, its far end and its duration after `SpawnActor`
	// returns, so every patch of burning ground in the game is drawn with a far
	// end at the world origin and no size. That is issue #1153, and it is
	// invisible there only because the damage runs on a timer that reads the
	// fields later. Nothing here would be so lucky.
	ACataclysmTether* Line = World->SpawnActorDeferred<ACataclysmTether>(
		ACataclysmTether::StaticClass(),
		FTransform(FRotator::ZeroRotator, Pair[0]->GetActorLocation()),
		Caster, /*Instigator=*/nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Line)
	{
		return nullptr;
	}

	Line->Caster = Caster;
	Line->First = Pair[0];
	Line->Second = Pair[1];
	Line->MaxSeparationCm = MaxSeparationCm;
	Line->LineHalfWidthCm = LineHalfWidthCm;
	Line->SetLifeSpan(DurationSeconds);

	Line->FinishSpawning(
		FTransform(FRotator::ZeroRotator, Pair[0]->GetActorLocation()));

	UE_LOG(LogCataclysm, Verbose,
		TEXT("A tether bound '%s' to '%s' at %.0fcm for %.1f seconds."),
		*Pair[0]->GetName(), *Pair[1]->GetName(), MaxSeparationCm,
		DurationSeconds);

	return Line;
}

void ACataclysmTether::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		// FIRST CHECK A FULL INTERVAL IN. Nothing can have moved in the instant
		// the line was made, so a check at once would measure the distance the
		// caster deliberately bound and find it acceptable, which is work for
		// nothing.
		World->GetTimerManager().SetTimer(
			CheckTimer, this, &ACataclysmTether::Check,
			SecondsPerCheck, /*bLoop=*/true, /*InFirstDelay=*/SecondsPerCheck);
	}
}

bool ACataclysmTether::IsHolding() const
{
	const AActor* A = First.Get();
	const AActor* B = Second.Get();
	return IsValid(A) && IsValid(B)
		&& !UCataclysmSkillEffects::IsDead(A)
		&& !UCataclysmSkillEffects::IsDead(B);
}

float ACataclysmTether::SeparationCm() const
{
	const AActor* A = First.Get();
	const AActor* B = Second.Get();
	if (!IsValid(A) || !IsValid(B))
	{
		return 0.0f;
	}

	// ALONG THE GROUND, so a creature standing on a ledge above the other is not
	// counted as having broken away. The same flattening every displacement in
	// `UCataclysmSkillEffects` does.
	FVector Apart = B->GetActorLocation() - A->GetActorLocation();
	Apart.Z = 0.0f;
	return static_cast<float>(Apart.Size());
}

void ACataclysmTether::Check()
{
	if (!IsHolding())
	{
		// EITHER END DYING ENDS THE LINE. The row binds two creatures to each
		// other and says nothing about what a survivor stays bound to.
		Destroy();
		return;
	}

	AActor* A = First.Get();
	AActor* B = Second.Get();

	LastCheckLit = 0;

	// "IF EITHER TRIES TO BREAK AWAY BOTH ARE DRAGGED BACK TOGETHER", and the
	// row says both, so the correction is split evenly rather than moving
	// whichever one walked. Nothing here knows which of them moved, and finding
	// out would mean remembering both positions from the last check to compare
	// against -- which would still be wrong when both moved at once.
	const float Separation = SeparationCm();
	if (Separation > MaxSeparationCm)
	{
		FVector Apart = B->GetActorLocation() - A->GetActorLocation();
		Apart.Z = 0.0f;

		const FVector Toward = Apart.GetSafeNormal();
		const float Excess = Separation - MaxSeparationCm;
		const FVector Half = Toward * (Excess / 2.0f);

		// SET DIRECTLY RATHER THAN THROUGH `UCataclysmSkillEffects::ApplyPull`,
		// AND THE DIFFERENCE MATTERS. Every displacement in that file goes
		// through the design's diminishing-returns rule, which halves each shove
		// applied to a target already displaced in the last five seconds. That
		// rule exists to stop crowd control chaining; a tether is a constraint
		// rather than a shove, and running it through that rule would mean the
		// line held for about a second and then let go, because it corrects four
		// times a second and every correction after the first would be worth
		// half of the last.
		//
		// SWEPT, so a creature dragged into a wall stops at the wall rather than
		// being put inside it.
		A->SetActorLocation(A->GetActorLocation() + Half, /*bSweep=*/true);
		B->SetActorLocation(B->GetActorLocation() - Half, /*bSweep=*/true);

		++TimesDragged;
	}

	// "THE LINE SETS ALIGHT ANYTHING THAT TOUCHES IT." A search along the line
	// between the two ends, at the skill's own radius, every check.
	if (LineHalfWidthCm <= 0.0f)
	{
		return;
	}

	for (AActor* Touching : UCataclysmTargeting::FindEnemiesInLine(
			GetWorld(), Caster.Get(), A->GetActorLocation(),
			B->GetActorLocation(), LineHalfWidthCm))
	{
		// NOT THE TWO ENDS THEMSELVES. They are what the line is tied to rather
		// than something that walked into it, and the bolt that bound them has
		// already set them alight through the skill's own `Burn=1`. Relighting
		// them here would refresh a burn on a timer the row never mentions, so
		// a tethered creature could never burn out while the line held.
		if (Touching == A || Touching == B)
		{
			continue;
		}

		// A DESIGNED BURN, because setting alight what touches it is the line's
		// own sentence. Issue #917: the figure passed as the hit is zero and
		// that is the truth -- nothing struck anybody, and burn has been a flat
		// 25 a second since 2026-08-24 -- so without that decision the line
		// would light nobody at all.
		if (UCataclysmSkillEffects::ApplyBurn(Caster.Get(), Touching,
											  /*HitDamage=*/0.0f,
											  /*bScalesWithInstigator=*/true,
											  /*bBurnIsDesigned=*/true))
		{
			++LastCheckLit;
		}
	}
}
