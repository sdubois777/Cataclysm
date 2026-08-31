// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmGameInstance.h"

#include "Empire/CataclysmEmpireRun.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/DateTime.h"

UCataclysmEmpireRun* UCataclysmGameInstance::GetOrBeginEmpireRun()
{
	if (EmpireRun != nullptr)
	{
		return EmpireRun;
	}

	// FROM THE CLOCK, SO TWO SESSIONS ARE TWO DIFFERENT EMPIRES. Masked to the
	// positive range because `FRandomStream` takes a seed it will treat as one
	// and a negative here would be a needless thing to reason about.
	const int32 Seed = static_cast<int32>(
		FDateTime::Now().GetTicks() & 0x7FFFFFFF);

	return BeginEmpireRun(Seed);
}

UCataclysmEmpireRun* UCataclysmGameInstance::BeginEmpireRun(
	int32 Seed, ECataclysmSurgeMode Mode, int32 LethalityRung)
{
	EmpireRun = NewObject<UCataclysmEmpireRun>(this);
	EmpireRun->Begin(Seed, Mode, LethalityRung);

	return EmpireRun;
}

UCataclysmEmpireRun* UCataclysmGameInstance::EmpireRunFor(
	const UObject* WorldContext, bool bStartIfNone)
{
	if (WorldContext == nullptr)
	{
		return nullptr;
	}

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContext,
											 EGetWorldErrorMode::ReturnNull)
		: nullptr;

	if (World == nullptr)
	{
		return nullptr;
	}

	// A CAST AND NOT A CHECK, because the engine's own `UGameInstance` is what a
	// project without this class set gets, and answering null is the honest
	// result rather than a crash. `game/Config/DefaultEngine.ini` is what makes
	// it this one.
	UCataclysmGameInstance* Instance =
		Cast<UCataclysmGameInstance>(World->GetGameInstance());

	if (Instance == nullptr)
	{
		return nullptr;
	}

	return bStartIfNone ? Instance->GetOrBeginEmpireRun()
						: Instance->EmpireRun.Get();
}
