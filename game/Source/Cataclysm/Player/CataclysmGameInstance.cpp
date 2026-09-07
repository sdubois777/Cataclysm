// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmGameInstance.h"

#include "Dungeon/CataclysmDungeonModifierTable.h"
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
	int32 Seed, ECataclysmSurgeMode Mode, int32 LethalityRung,
	int32 DifficultyTier)
{
	EmpireRun = NewObject<UCataclysmEmpireRun>(this);

	// THE MODIFIER TABLE BEFORE THE RUN BEGINS, so the first wave -- which is
	// due on day 0 -- already draws from it. Filling it afterwards would leave
	// exactly the dungeons a fresh run starts with carrying no modifiers, which
	// is the hardest kind of gap to notice. Issue #41.
	//
	// AN EMPTY POOL IS NOT A FAILURE. `LoadPool` answers empty when the
	// DataTable cannot be read, and a run with an empty pool gives every dungeon
	// no modifiers, which is what the game did before this existed.
	EmpireRun->ModifierPool = UCataclysmDungeonModifierTable::LoadPool();

	EmpireRun->Begin(Seed, Mode, LethalityRung, DifficultyTier);

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
