// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmGameInstance.h"

#include "Cataclysm.h"
#include "Character/CataclysmPassivePoints.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/DateTime.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveTriggers.h"
#include "Save/CataclysmSaveWriter.h"

const TCHAR* UCataclysmGameInstance::AutomationTestSlotSuffix = TEXT("_AutomationTest");

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
	// NO CHARACTER NAMED, SO THE RUN'S OWN SEED DRAWS ITS CATACLYSMS, as before issue #1324 slice 6a.
	return BeginACharactersEmpireRun(Seed, Seed, Mode, LethalityRung, DifficultyTier);
}

UCataclysmEmpireRun* UCataclysmGameInstance::BeginACharactersEmpireRun(
	int32 Seed, int32 CataclysmOrderSeed, ECataclysmSurgeMode Mode, int32 LethalityRung,
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

	// THE CHARACTER'S DRAW, NOT THE RUN'S: the run's seed is its chance, and the order seed decides which Cataclysms it
	// faces. Issue #1324 slice 6a.
	EmpireRun->BeginForCharacter(Seed, CataclysmOrderSeed, Mode, LethalityRung, Tier);

	// AND THE GAME MODE OF THE LEVEL BEING PLAYED TAKES ITS TIER, so a run begun during play is fought at its own tier at
	// once. Issue #1472.
	if (UWorld* World = GetWorld())
	{
		if (ACataclysmGameMode* GameMode = World->GetAuthGameMode<ACataclysmGameMode>())
		{
			GameMode->TakeTheRunsTier();
		}
	}

	return EmpireRun;
}

UCataclysmEmpireRun* UCataclysmGameInstance::BeginTheRunAfterAWin(ACataclysmPlayerState& Winner,
																  UCataclysmSaveWriter* Writer)
{
	// ONLY A WON RUN. A clear that did not win, or no run at all, does nothing here. Issue #1324 slice 6e.
	const UCataclysmEmpireRun* Won = EmpireRun.Get();
	if (Won == nullptr || !Won->bWon)
	{
		return nullptr;
	}

	// 1. WHAT WAS WON, READ BEFORE THE NEXT RUN REPLACES IT. `BeginACharactersEmpireRun` below makes a new run object,
	// and `Won` is not read after it.
	const int32 TierWon = FMath::Clamp(Won->DifficultyTier, ACataclysmGameMode::LowestDifficultyTier,
									   ACataclysmGameMode::HighestDifficultyTier);
	const int32 WonDay = Won->WonDay;
	const ECataclysmSurgeMode Mode = Won->Surges != nullptr ? Won->Surges->Mode : ECataclysmSurgeMode::Static;
	const int32 LethalityRung = Won->Surges != nullptr ? Won->Surges->LethalityRung : 0;

	// 2. THE CHARACTER'S DRAW. One that never drew a seed is given the seed its identifier gives, the seed its record is
	// written with in any case (`FCataclysmSaveGather::CharacterFrom`), so the boss paid for and the next run agree
	// with what the record says from now on. A judgement of the session that built it.
	if (!Winner.HasCataclysmOrderSeed())
	{
		const FGuid Identifier = Writer != nullptr ? Writer->HoldsTheCharacter() : FGuid();
		Winner.SetCataclysmOrderSeed(UCataclysmCharacterSave::CataclysmOrderSeedFor(Identifier));
	}
	const int32 OrderSeed = Winner.GetCataclysmOrderSeed();

	// 3. THE FIRST-KILL PASSIVE POINTS, FOR THE BOSS OF THE TIER-TH CATACLYSM IN THE CHARACTER'S DRAW. The coordinating
	// session's ruling R4 of 2026-11-02: a win at tier T pays the first-kill 10 for `OrderFor(seed)[T-1]`. Eight wins at
	// tiers 1 to 8 pay 80, the design's budget: "Levelling to 100 gives 150 ... and the boss kills give the other 80"
	// (`docs/Cataclysm_GDD_v2.md`, section XII). A boss defeated before pays nothing.
	const TArray<ECataclysmType> Order = UCataclysmRoster::OrderFor(OrderSeed);
	const ECataclysmType Against = Order.IsValidIndex(TierWon - 1) ? Order[TierWon - 1] : ECataclysmType::None;
	const bool bFirstDefeat = Winner.RecordCataclysmBossDefeat(UCataclysmRoster::NameFor(Against));
	const int32 PassivePoints = bFirstDefeat ? UCataclysmPassivePoints::PerFirstBossKill : 0;

	// 4. THE TIER, ONE HIGHER, AND BEFORE ANY RECORD IS WRITTEN: the coordinating session's comment of 2026-11-01 on
	// #1324, "before the character record is written". Held at 8 (`RaiseDifficultyTierForAWin`).
	const int32 TierNow = Winner.RaiseDifficultyTierForAWin(); // A won run's: one tier higher, before the write.

	// 5. THIS RUN'S KILLS START AGAIN, since the run they counted is over. The character's lifetime count is kept.
	Winner.RestoreRunKills(0);

	// 6. THE NEXT RUN, AT ONCE: the ruling R7. A new run's chance from the clock, as a loaded character's new run takes
	// it (`FCataclysmSaveLoad::CharacterAndRun`); its Cataclysms from the character's draw at the new tier, so it faces
	// the won run's and one more, or at tier 8 the same eight.
	const int32 Seed = static_cast<int32>(FDateTime::Now().GetTicks() & 0x7FFFFFFF);
	UCataclysmEmpireRun* Next = BeginACharactersEmpireRun(Seed, OrderSeed, Mode, LethalityRung, TierNow);
	Next->NoteLastRunWon(WonDay, TierWon, Against, PassivePoints);

	UE_LOG(LogCataclysm, Log,
		   TEXT("Run won on day %d at tier %d against %s: %d passive points. The next run is at tier %d, facing %d "
				"Cataclysms."),
		   WonDay, TierWon, *UCataclysmRoster::NameFor(Against).ToString(), PassivePoints, TierNow,
		   Next->ActiveCataclysms.Num());

	// 7. WRITTEN ONCE, AFTER ALL OF IT: the character record at the new tier with the boss defeated, and the run record
	// of the next run, carrying the win line.
	if (Writer != nullptr)
	{
		Writer->NoteTrigger(ECataclysmSaveTrigger::RunWon); // A won run's: written once, after the raise.
	}

	return Next;
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

UCataclysmGameInstance* UCataclysmGameInstance::In(const UObject* WorldContext)
{
	const UWorld* World = WorldContext != nullptr && GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World != nullptr ? Cast<UCataclysmGameInstance>(World->GetGameInstance()) : nullptr;
}

FString UCataclysmGameInstance::AccountSlotFor(const FCataclysmCharacterPartition& Partition)
{
	const FString Slot = UCataclysmSavePartition::AccountSlotName(Partition);

	// NOT THE PLAYER'S OWN BANK WHILE A TEST RUNS. See the header: an account slot is named for its partition, so
	// without this a test's clear would read and rewrite the bank in the player's own slot.
	if (Slot.IsEmpty() || !GIsAutomationTesting)
	{
		return Slot;
	}
	return Slot + AutomationTestSlotSuffix;
}

UCataclysmAccountSave* UCataclysmGameInstance::AccountFor(const FCataclysmCharacterPartition& Partition)
{
	const FString Slot = AccountSlotFor(Partition);
	if (Slot.IsEmpty())
	{
		// SOLO SELF-FOUND: NO ACCOUNT RECORD. Its points belong in its own character record, which this slice does not
		// bank into. Issue #52.
		return nullptr;
	}

	// THE HELD ONE, WHEN IT IS THIS PARTITION'S. A record carries its partition, so the check is on what it says.
	if (HeldAccount != nullptr && HeldAccount->Lethality == Partition.Lethality
		&& HeldAccount->Population == Partition.Population)
	{
		return HeldAccount;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
	FString Message;
	UCataclysmSaveRecord* Read = FCataclysmSaveStorage::ReadFromSlot(
		Slot, UCataclysmSaveWriter::UserIndex, UCataclysmAccountSave::StaticClass(), this, Result, Message);

	if (Result == ECataclysmSaveLoadResult::SlotIsEmpty)
	{
		// A NEW BANK: nothing has been banked into this partition on this machine yet.
		UCataclysmAccountSave* Fresh = NewObject<UCataclysmAccountSave>(this);
		Fresh->SchemaVersion = UCataclysmAccountSave::SchemaVersionNow;
		Fresh->Lethality = Partition.Lethality;
		Fresh->Population = Partition.Population;
		HeldAccount = Fresh;
		return HeldAccount;
	}

	UCataclysmAccountSave* Account = Cast<UCataclysmAccountSave>(Read);
	if (Account == nullptr || !FCataclysmSaveStorage::Succeeded(Result))
	{
		// REFUSED, NOT REPLACED. A fresh record banked into would be written over this file and empty its bank.
		UE_LOG(LogCataclysm, Error,
			   TEXT("The account record in '%s' could not be read (%s: %s), so no empire upgrade points are banked "
					"into it this session."),
			   *Slot, FCataclysmSaveStorage::Describe(Result), *Message);
		return nullptr;
	}

	if (Account->Lethality != Partition.Lethality || Account->Population != Partition.Population)
	{
		// A FILE COPIED INTO ANOTHER PARTITION'S SLOT, which the record carries its partition to catch. Banking into it
		// would pour one mode's points into another's tree.
		UE_LOG(LogCataclysm, Error,
			   TEXT("The account record in '%s' names another partition, so no empire upgrade points are banked into "
					"it this session."),
			   *Slot);
		return nullptr;
	}

	HeldAccount = Account;
	return HeldAccount;
}

UCataclysmAccountSave* UCataclysmGameInstance::BankEmpirePoints(const FCataclysmCharacterPartition& Partition,
																 double Points)
{
	UCataclysmAccountSave* Account = AccountFor(Partition);
	if (Account == nullptr)
	{
		return nullptr;
	}

	// THE FRACTION IS KEPT: ruled 2026-11-03. Rounding each award down would erase most ordinary dungeons' awards.
	Account->EmpireUpgradePoints += Points;
	return Account;
}

bool UCataclysmGameInstance::PlayingPartition(const UObject* WorldContext, FCataclysmCharacterPartition& OutPartition)
{
	const UWorld* World = WorldContext != nullptr && GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	const ACataclysmPlayerCharacter* Character = World != nullptr ? FCataclysmSaveGather::CharacterIn(*World) : nullptr;
	const ACataclysmPlayerState* State = Character != nullptr ? Character->GetPlayerState<ACataclysmPlayerState>()
															  : nullptr;
	if (State == nullptr)
	{
		return false;
	}

	OutPartition = State->GetPartition();
	return true;
}
