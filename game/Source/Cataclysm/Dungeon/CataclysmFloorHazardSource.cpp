// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmFloorHazardSource.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
// For finding the one this world already has rather than keeping a pointer that
// would need clearing on every way a floor can end.
#include "EngineUtils.h"

ACataclysmFloorHazardSource::ACataclysmFloorHazardSource()
{
	// Nothing to do per frame, and nothing to do at all. It is asked questions;
	// it never acts.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// INVISIBLE, AND NOT MERELY UNDRAWN. It has no mesh to hide, so this is
	// about anything that walks the world looking for drawable actors rather
	// than about the player seeing it.
	SetActorHiddenInGame(true);

	// See the header. Without a root component the actor has no position and
	// reports the world origin.
	Anchor = CreateDefaultSubobject<USceneComponent>(TEXT("Anchor"));
	SetRootComponent(Anchor);

	AbilitySystemComponent = CreateDefaultSubobject<UCataclysmAbilitySystemComponent>(
		TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	// MONSTERS, AND STATED HERE RATHER THAN LEFT TO THE DEFAULT. The default is
	// `FGenericTeamId::NoTeam`, and `UCataclysmTeams` records that no team means
	// hostile to everything rather than neutral -- so leaving it would make a
	// floor's hazards hostile to the creatures that floor spawned as well as to
	// the player. The header says why the side alone does not decide who a
	// hazard affects.
	TeamId = UCataclysmTeams::IdFor(ECataclysmTeam::Monsters);
}

ACataclysmFloorHazardSource* ACataclysmFloorHazardSource::Existing(
	const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	// WALKED RATHER THAN REMEMBERED, which is what `HeldBy` does for a planted
	// weapon and `RegenerationScaleFor` for a patch of ground, for the same
	// reason: a pointer would need clearing on every way a floor can end, and
	// there is at most one of these in a level.
	for (TActorIterator<ACataclysmFloorHazardSource> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}

	return nullptr;
}

ACataclysmFloorHazardSource* ACataclysmFloorHazardSource::ForFloor(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	if (ACataclysmFloorHazardSource* Already = Existing(World))
	{
		return Already;
	}

	// AT THE ORIGIN, BECAUSE ITS POSITION IS NEVER READ. A hazard carries its
	// own two ends and its own radius; this actor is asked for an ability system
	// component and a side, and for nothing else.
	//
	// A ONE-STEP SPAWN IS CORRECT HERE, unlike in `ACataclysmGroundZone` and
	// `ACataclysmTerrain`. Both of those defer because `BeginPlay` reads
	// properties set after the spawn call -- a radius, a far end, a life span.
	// This actor sets everything it has in its constructor and does no work at
	// `BeginPlay` at all, so there is nothing that could be read too early.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ACataclysmFloorHazardSource>(
		ACataclysmFloorHazardSource::StaticClass(), FTransform::Identity, Params);
}

bool ACataclysmFloorHazardSource::MayBurn(FName Kind, const AActor* Target, double NowSeconds)
{
	if (Kind.IsNone() || !Target)
	{
		return true;
	}

	const TPair<FName, TWeakObjectPtr<const AActor>> Key(Kind, Target);
	if (const double* Last = LastBurnedAt.Find(Key))
	{
		if (NowSeconds - *Last < ACataclysmGroundZone::TickSeconds - BurnSlackSeconds)
		{
			return false;
		}
	}
	LastBurnedAt.Add(Key, NowSeconds);
	return true;
}

UAbilitySystemComponent* ACataclysmFloorHazardSource::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ACataclysmFloorHazardSource::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	TeamId = NewTeamId;
}

FGenericTeamId ACataclysmFloorHazardSource::GetGenericTeamId() const
{
	return TeamId;
}
