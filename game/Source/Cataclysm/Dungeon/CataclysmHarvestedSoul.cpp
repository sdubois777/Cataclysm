// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmHarvestedSoul.h"

#include "Character/CataclysmEnemyCharacter.h"
#include "Components/SceneComponent.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Engine/World.h"
#include "EngineUtils.h"

const TCHAR* ACataclysmHarvestedSoul::NameText = TEXT("Demonic soul");

FLinearColor ACataclysmHarvestedSoul::NameColour()
{
	return FLinearColor(0.85f, 0.15f, 0.2f, 1.0f);
}

ACataclysmHarvestedSoul::ACataclysmHarvestedSoul()
{
	// IT TICKS, TO FLOAT, AND COLLIDES WITH NOTHING: the root is a bare scene component, which has no collision at all.
	// Not decoration: an actor with no scene root reports the world origin as its location however it was spawned.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

ACataclysmHarvestedSoul* ACataclysmHarvestedSoul::Release(ACataclysmDungeonGameMode* By, const FVector& Where,
														  ACataclysmEnemyCharacter* Toward)
{
	UWorld* Into = IsValid(By) ? By->GetWorld() : nullptr;
	if (!Into || !IsValid(Toward))
	{
		return nullptr;
	}

	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACataclysmHarvestedSoul* Soul = Into->SpawnActor<ACataclysmHarvestedSoul>(Where, FRotator::ZeroRotator, Parameters);
	if (Soul)
	{
		Soul->Releaser = By;
		Soul->Toward = Toward;
		Soul->Origin = Where;
	}
	return Soul;
}

ACataclysmEnemyCharacter* ACataclysmHarvestedSoul::Target() const
{
	return Toward.Get();
}

ACataclysmDungeonGameMode* ACataclysmHarvestedSoul::ReleasedBy() const
{
	return Releaser.Get();
}

void ACataclysmHarvestedSoul::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Step(DeltaSeconds);
}

bool ACataclysmHarvestedSoul::Step(float DeltaSeconds)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (Ended != ECataclysmHarvestedSoulEnd::InFlight)
	{
		return false;
	}

	// THE GAME MODE THAT RELEASED IT IS WHO GIVES THE SOUL. Without it there is nobody to give it to and nothing to ask
	// for another demon, so it is as if no demon were left.
	ACataclysmDungeonGameMode* Mode = Releaser.Get();
	if (!IsValid(Mode))
	{
		End(ECataclysmHarvestedSoulEnd::NoDemonLeft);
		return false;
	}

	// ITS DEMON DIED, OR IS NO LONGER AN ENEMY DEMON: it turns to the nearest demon within reach of where it is now.
	// With none, it fades, giving nothing. Ruled 2026-11-17, issue #1671.
	ACataclysmEnemyCharacter* Demon = Toward.Get();
	if (!Mode->SoulHarvestMayFeed(Demon))
	{
		Demon = Mode->NearestDemonForASoul(GetActorLocation(), nullptr);
		if (!Demon)
		{
			End(ECataclysmHarvestedSoulEnd::NoDemonLeft);
			return false;
		}
		Toward = Demon;
		++Turned;
	}

	// TOWARD WHERE ITS DEMON STANDS NOW, every step, so a demon that walks is followed. Arrival before fading: a soul
	// that reaches its demon in the step that would have ended it still gives the soul.
	Floated += FMath::Max(0.0f, DeltaSeconds);
	FVector At = GetActorLocation();
	if (Effects::SoulHarvestSoulArrivesAfter(GetActorLocation(), Demon->GetActorLocation(), DeltaSeconds, At))
	{
		SetActorLocation(At);
		// NO LONGER FLOATING BEFORE IT IS GIVEN: the arrival refreshes the floor panel, whose floating count asks
		// `FloatingIn`, and a soul still marked in flight there was counted as floating beside the soul it had just given.
		Ended = ECataclysmHarvestedSoulEnd::Arrived;
		Mode->SoulHarvestSoulArrived(Demon);
		End(ECataclysmHarvestedSoulEnd::Arrived);
		return false;
	}
	SetActorLocation(At);

	if (Floated >= Effects::SoulHarvestSoulLifetimeSeconds)
	{
		End(ECataclysmHarvestedSoulEnd::Faded);
		return false;
	}
	return true;
}

void ACataclysmHarvestedSoul::End(ECataclysmHarvestedSoulEnd How)
{
	Ended = How;
	Toward.Reset();
	// ONE THAT GAVE NOTHING STILL CHANGES THE PANEL'S FLOATING COUNT; an arrival refreshed it already.
	if (How != ECataclysmHarvestedSoulEnd::Arrived)
	{
		if (ACataclysmDungeonGameMode* Mode = Releaser.Get())
		{
			Mode->SoulHarvestSoulLost();
		}
	}
	Destroy();
}

void ACataclysmHarvestedSoul::FloatingIn(const UWorld* World, const ACataclysmDungeonGameMode* By,
										 TArray<ACataclysmHarvestedSoul*>& Out)
{
	Out.Reset();
	if (!World)
	{
		return;
	}
	for (TActorIterator<ACataclysmHarvestedSoul> It(World); It; ++It)
	{
		ACataclysmHarvestedSoul* Soul = *It;
		if (IsValid(Soul) && Soul->Ended == ECataclysmHarvestedSoulEnd::InFlight
			&& (!By || Soul->Releaser.Get() == By))
		{
			Out.Add(Soul);
		}
	}
}
