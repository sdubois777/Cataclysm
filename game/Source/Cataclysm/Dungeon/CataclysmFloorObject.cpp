// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmFloorObject.h"

#include "Components/SceneComponent.h"
#include "EngineUtils.h"

ACataclysmFloorObject::ACataclysmFloorObject()
{
	// NOTHING TICKS. The HUD asks for it each frame and the game mode answers a choice; it does nothing on its own.
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

const FCataclysmFloorObjectChoice* ACataclysmFloorObject::ChoiceOf(FName ChoiceKey) const
{
	return Choices.FindByPredicate(
		[ChoiceKey](const FCataclysmFloorObjectChoice& Choice) { return Choice.Key == ChoiceKey; });
}

void ACataclysmFloorObject::ObjectsToName(const UWorld* World, const FVector& Standing, float Range,
										  TArray<ACataclysmFloorObject*>& Out)
{
	Out.Reset();
	if (!World)
	{
		return;
	}
	for (TActorIterator<ACataclysmFloorObject> It(World); It; ++It)
	{
		ACataclysmFloorObject* Object = *It;
		if (IsValid(Object) && FVector::Dist2D(Object->GetActorLocation(), Standing) <= Range)
		{
			Out.Add(Object);
		}
	}
	Out.Sort([&Standing](const ACataclysmFloorObject& A, const ACataclysmFloorObject& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), Standing)
			< FVector::DistSquared2D(B.GetActorLocation(), Standing);
	});
}
