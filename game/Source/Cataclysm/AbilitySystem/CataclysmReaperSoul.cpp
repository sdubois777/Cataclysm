// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmReaperSoul.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameplayTagContainer.h"

const TCHAR* ACataclysmReaperSoul::MinionDeathLeavesSoulStat = TEXT("minion_death_leaves_soul");
const TCHAR* ACataclysmReaperSoul::NameText = TEXT("Soul");

FLinearColor ACataclysmReaperSoul::NameColour()
{
	return FLinearColor(0.55f, 0.85f, 1.0f, 1.0f);
}

ACataclysmReaperSoul::ACataclysmReaperSoul()
{
	// NOTHING TICKS AND NOTHING COLLIDES. The player controller looks for it each frame and the HUD names it; it does
	// nothing on its own. No mesh (R8): its tag is what is seen.
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

ACataclysmReaperSoul* ACataclysmReaperSoul::LeaveFor(AActor* Commander, const FVector& Where)
{
	if (!IsValid(Commander))
	{
		return nullptr;
	}
	UWorld* Into = Commander->GetWorld();
	const UCataclysmAbilitySystemComponent* Theirs =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Commander));
	// OFF THE COMMANDER WITH NO TAGS: the bonus is the commander's, and no skill of it is in hand at a minion's death.
	if (!Into || !Theirs
		|| Theirs->StatForSkill(FName(MinionDeathLeavesSoulStat), FGameplayTagContainer(), 0.0f) <= 0.0f)
	{
		return nullptr;
	}

	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACataclysmReaperSoul* Left =
		Into->SpawnActor<ACataclysmReaperSoul>(Where, FRotator::ZeroRotator, Parameters);
	if (Left)
	{
		// NO LIFESPAN, R7: it lasts the floor.
		Left->LeftByCommander = Commander;
	}
	return Left;
}

int32 ACataclysmReaperSoul::TakeSoulsNear(AActor* Taker)
{
	if (!IsValid(Taker) || UCataclysmSkillEffects::IsDead(Taker))
	{
		return 0;
	}
	UWorld* Around = Taker->GetWorld();
	if (!Around)
	{
		return 0;
	}

	// GATHERED BEFORE ANY IS TAKEN, as the material pick-up gathers: taking one destroys it, and destroying an actor
	// while a TActorIterator walks the level is not something to rely on.
	//
	// ALONG THE FLOOR, R3, as a drop's reach is measured: the height between a capsule's centre and the floor a Soul
	// lies on is not distance walked. ONLY ITS OWN COMMANDER, R2.
	const FVector Standing = Taker->GetActorLocation();
	TArray<ACataclysmReaperSoul*> Reached;
	for (TActorIterator<ACataclysmReaperSoul> It(Around); It; ++It)
	{
		ACataclysmReaperSoul* Soul = *It;
		if (IsValid(Soul) && Soul->LeftByCommander.Get() == Taker
			&& FVector::Dist2D(Soul->GetActorLocation(), Standing) <= PickUpReachCm)
		{
			Reached.Add(Soul);
		}
	}

	UCataclysmAbilitySystemComponent* Theirs =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Taker));
	int32 Taken = 0;
	for (ACataclysmReaperSoul* Soul : Reached)
	{
		// THE EVENT ONCE PER SOUL, R5 and R6: Soul Strike is rows on it, one stack up to the cap that restarts the window,
		// at the cap too. With no ability system, or no row on it, the Soul is still taken.
		if (Theirs)
		{
			Theirs->ActOnEvent(FName(TEXT("soul_picked_up")));
		}
		Soul->Destroy();
		++Taken;
	}
	return Taken;
}

void ACataclysmReaperSoul::SoulsToName(const UWorld* World, const FVector& Standing, float Range,
									   TArray<ACataclysmReaperSoul*>& Out)
{
	Out.Reset();
	if (!World)
	{
		return;
	}
	for (TActorIterator<ACataclysmReaperSoul> It(World); It; ++It)
	{
		ACataclysmReaperSoul* Soul = *It;
		if (IsValid(Soul) && FVector::Dist2D(Soul->GetActorLocation(), Standing) <= Range)
		{
			Out.Add(Soul);
		}
	}
	Out.Sort([&Standing](const ACataclysmReaperSoul& A, const ACataclysmReaperSoul& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), Standing)
			< FVector::DistSquared2D(B.GetActorLocation(), Standing);
	});
}
