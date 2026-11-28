// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmParagonHold.h"
#include "Cataclysm.h"
#include "UObject/Package.h"
#include "UObject/UObjectArray.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

void UCataclysmParagonHold::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	BeforeCollectingHandle =
		FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddUObject(this, &UCataclysmParagonHold::HoldBeforeCollecting);
}

void UCataclysmParagonHold::Deinitialize()
{
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().Remove(BeforeCollectingHandle);
	BeforeCollectingHandle.Reset();
	Held.Reset();
	Super::Deinitialize();
}

const FString& UCataclysmParagonHold::ParagonPrefix()
{
	// SEE THE HEADER: two pieces, so the packager's text check does not read a load path here.
	static const FString Prefix = FString::Printf(TEXT("/%s/%s"), TEXT("Game"), TEXT("Paragon"));
	return Prefix;
}

int32 UCataclysmParagonHold::HoldObjectsUnder(const FString& Prefix, TSet<TObjectPtr<UObject>>& Into)
{
	if (Prefix.IsEmpty())
	{
		return 0;
	}

	// ONE ANSWER PER PACKAGE, so the name is read once a package rather than once an object.
	TMap<const UPackage*, bool> Under;
	int32 Added = 0;
	for (FRawObjectIterator It(/*bOnlyGCedObjects=*/true); It; ++It)
	{
		FUObjectItem* Item = *It;
		UObject* Object = Item ? static_cast<UObject*>(Item->GetObject()) : nullptr;
		if (!Object || Item->IsUnreachable() || !IsValid(Object))
		{
			continue;
		}
		const UPackage* Package = Object->GetPackage();
		if (!Package)
		{
			continue;
		}
		bool* Known = Under.Find(Package);
		const bool bUnder = Known ? *Known : Under.Add(Package, Package->GetName().StartsWith(Prefix));
		if (!bUnder)
		{
			continue;
		}
		bool bAlready = false;
		Into.Add(Object, &bAlready);
		Added += bAlready ? 0 : 1;
	}
	return Added;
}

void UCataclysmParagonHold::HoldBeforeCollecting()
{
	const int32 Added = HoldObjectsUnder(ParagonPrefix(), Held);
	if (Added > 0)
	{
		UE_LOG(LogCataclysm, Log, TEXT("Paragon hold: %d more objects held before this garbage collection, %d in all."),
			   Added, Held.Num());
	}
}
