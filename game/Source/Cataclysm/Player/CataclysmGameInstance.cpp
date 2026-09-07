// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmGameInstance.h"

#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/DateTime.h"
#include "Player/CataclysmGameMode.h"

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

	// AND NO TIER, WHICH IS HOW THE TIER GETS HERE. `BeginEmpireRun` reads the
	// game's own difficulty tier when it is not told one, so this asks for it by
	// staying quiet rather than by reading it a second time. Two reads would be
	// two places to forget, and forgetting is issue #1444.
	return BeginEmpireRun(Seed);
}

UCataclysmEmpireRun* UCataclysmGameInstance::BeginEmpireRun(
	int32 Seed, ECataclysmSurgeMode Mode, int32 LethalityRung,
	int32 DifficultyTier)
{
	EmpireRun = NewObject<UCataclysmEmpireRun>(this);

	// WHICH TIER THE GAME IS BEING PLAYED AT, WHEN NOBODY SAID.
	// `ACataclysmGameMode::DifficultyTierIn` is the one place that answers that
	// question, and until issue #1444 nothing joined its answer to a run. Every
	// run the game started was tier 1 -- one Cataclysm, one modifier a dungeon
	// -- whatever tier the player was fighting at.
	//
	// HERE RATHER THAN IN EACH CALLER, because this is the only door into a run
	// and a caller that forgets is what went wrong before.
	//
	// `this` IS THE WORLD CONTEXT, AND IT IS ALLOWED TO HAVE NO WORLD. A game
	// instance outlives every world it holds and can be asked for a run before
	// there is a game mode to ask. `DifficultyTierIn` then finds no mode and
	// falls back to the `Cataclysm.DifficultyTier` console variable, then to
	// tier 1 -- the same fallback every other reader of the tier already takes,
	// and the reason `DifficultyTierFor` gives for having one: "a great deal
	// runs without one and a hit still has to produce a number". So a run
	// started before the tier is knowable is tier 1 rather than refused.
	//
	// READ ONCE AND KEPT. `docs/Cataclysm_GDD_v2.md` section XII: "A run is
	// played at a fixed tier, so a player does not move up the tiers inside a
	// run". Nothing re-reads this for the life of the run.
	const int32 Tier = DifficultyTier > 0
		? DifficultyTier
		: ACataclysmGameMode::DifficultyTierIn(this);

	// THE MODIFIER TABLE BEFORE THE RUN BEGINS, so the first wave -- which is
	// due on day 0 -- already draws from it. Filling it afterwards would leave
	// exactly the dungeons a fresh run starts with carrying no modifiers, which
	// is the hardest kind of gap to notice. Issue #41.
	//
	// AN EMPTY POOL IS NOT A FAILURE. `LoadPool` answers empty when the
	// DataTable cannot be read, and a run with an empty pool gives every dungeon
	// no modifiers, which is what the game did before this existed.
	EmpireRun->ModifierPool = UCataclysmDungeonModifierTable::LoadPool();

	EmpireRun->Begin(Seed, Mode, LethalityRung, Tier);

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
