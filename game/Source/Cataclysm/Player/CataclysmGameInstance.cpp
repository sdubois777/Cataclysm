// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmGameInstance.h"

#include "Cataclysm.h"
#include "Character/CataclysmPassivePoints.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Items/CataclysmDroppedItem.h"
#include "Misc/DateTime.h"
#include "Items/CataclysmDropRoll.h"
#include "Player/CataclysmEmpireTree.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayLoopLog.h"
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
	// THE EMPIRE UPGRADE TREE BEFORE THE RUN BEGINS, so its cities are built full at the maxima the tree raises (S1),
	// as the modifier table is handed over before for the same kind of reason. `this` is the world context, as above: a
	// run begun before anyone is playing has no tree to read and begins with none. Issue #2741 slice 3a.
	EmpireRun->SetTreeBonuses(EmpireTreeBonusesFor(this));
	WireTheRunToTheBalance(this, EmpireRun);

	EmpireRun->BeginForCharacter(Seed, CataclysmOrderSeed, Mode, LethalityRung, Tier);

	// AND THE FIRST SURGE, ON DAY 0, so a new run's map shows its dungeons before any day has passed: "A Surge is
	// triggered at run start" (`docs/Cataclysm_GDD_v2.md`, the Surge section). HERE AND NOT IN `BeginForCharacter`,
	// because every new run the game begins comes through this function -- a new character, a loaded character whose run
	// could not be read, the run after a win, and the console commands' `BeginEmpireRun` -- while `BeginForCharacter` is
	// also how a saved run is rebuilt (`FCataclysmSaveApply::RunInto`), which must not roll a wave. Issue #2733.
	const FCataclysmDayReport Opening = EmpireRun->FireOpeningSurge();
	UE_LOG(LogCataclysm, Log, TEXT("A new empire run began at tier %d, facing %d Cataclysms: its first surge landed %d "
								   "dungeons on day %d."),
		   Tier, EmpireRun->ActiveCataclysms.Num(), Opening.Spawned.Num(), Opening.Day);

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

FCataclysmLootFilterPreferences& UCataclysmGameInstance::LootFilterPreferences()
{
	// READ ONCE, ON FIRST ASKING: no file is a new player, whose preferences `Load` leaves in place.
	if (!bLootFiltersRead)
	{
		UCataclysmLootFilterStore::Load(UCataclysmLootFilterStore::FilePath(), LootFilters);
		bLootFiltersRead = true;
	}
	return LootFilters;
}

void UCataclysmGameInstance::SaveLootFilterPreferences()
{
	if (!UCataclysmLootFilterStore::Save(UCataclysmLootFilterStore::FilePath(), LootFilterPreferences()))
	{
		UE_LOG(LogCataclysm, Warning, TEXT("The loot filters could not be written to %s."),
			   *UCataclysmLootFilterStore::FilePath());
	}
}

FCataclysmLootFilter UCataclysmGameInstance::ActiveLootFilterIn(const UObject* WorldContext)
{
	UCataclysmGameInstance* Instance = In(WorldContext);
	return Instance != nullptr ? UCataclysmLootFilterStore::ActiveFilter(Instance->LootFilterPreferences())
							   : UCataclysmLootFilterRules::HideWorseThanWorn();
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
		// SOLO SELF-FOUND: NO ACCOUNT RECORD. Its points belong in its own character record, which `BankEmpirePointsFor`
		// banks into since issue #2741 slice 1c. Issue #52.
		return nullptr;
	}

	// THE HELD ONE, WHEN IT IS THIS PARTITION'S. A record carries its partition, so the check is on what it says.
	if (HeldAccount != nullptr && HeldAccount->Lethality == Partition.Lethality
		&& HeldAccount->Population == Partition.Population)
	{
		return HeldAccount;
	}

	// A SLOT REFUSED ONCE IS REFUSED FOR THE REST OF THE SESSION, without being read or logged again. The empire screen
	// asks at every refresh of its status line, so without this a slot that will not read would be read from disk and
	// logged as an error every time the screen redrew. Issue #52, found reviewing the first slice, 2026-11-03.
	if (RefusedAccountSlots.Contains(Slot))
	{
		return nullptr;
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
			   TEXT("The account record in '%s' could not be read (%s: %s), so no empire upgrade points or gold are "
					"banked into it this session."),
			   *Slot, FCataclysmSaveStorage::Describe(Result), *Message);
		RefusedAccountSlots.Add(Slot);
		return nullptr;
	}

	if (Account->Lethality != Partition.Lethality || Account->Population != Partition.Population)
	{
		// A FILE COPIED INTO ANOTHER PARTITION'S SLOT, which the record carries its partition to catch. Banking into it
		// would pour one mode's points into another's tree.
		UE_LOG(LogCataclysm, Error,
			   TEXT("The account record in '%s' names another partition, so no empire upgrade points or gold are "
					"banked into it this session."),
			   *Slot);
		RefusedAccountSlots.Add(Slot);
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

UCataclysmAccountSave* UCataclysmGameInstance::BankGold(const FCataclysmCharacterPartition& Partition, int64 Gold)
{
	// NOTHING TO ADD, OR A SUBTRACTION, which is not banking. Issue #2742.
	if (Gold <= 0)
	{
		return nullptr;
	}

	UCataclysmAccountSave* Account = AccountFor(Partition);
	if (Account == nullptr)
	{
		return nullptr;
	}

	Account->Gold += Gold;
	return Account;
}

namespace CataclysmGameInstanceGold
{
	/** The player state of the character playing in a world, or null. The one `PlayingPartition` reads. */
	ACataclysmPlayerState* PlayingState(const UObject* WorldContext)
	{
		const UWorld* World = WorldContext != nullptr && GEngine != nullptr
			? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
			: nullptr;
		const ACataclysmPlayerCharacter* Character = World != nullptr ? FCataclysmSaveGather::CharacterIn(*World)
																	  : nullptr;
		return Character != nullptr ? Character->GetPlayerState<ACataclysmPlayerState>() : nullptr;
	}
}

bool UCataclysmGameInstance::GoldHeldBy(const UObject* WorldContext, int64& OutGold)
{
	OutGold = 0;
	const ACataclysmPlayerState* State = CataclysmGameInstanceGold::PlayingState(WorldContext);
	if (State == nullptr)
	{
		return false;
	}

	// A SOLO SELF-FOUND CHARACTER'S OWN: the design's Storage section, "its gold is its own and is shared with
	// nothing". Issue #2742, the owner's ruling of 2026-11-18.
	const FCataclysmCharacterPartition& Partition = State->GetPartition();
	if (Partition.bSoloSelfFound)
	{
		OutGold = State->GetOwnGold();
		return true;
	}

	// EVERY OTHER CHARACTER'S LETHALITY MODE'S, held once per partition in the account record.
	UCataclysmGameInstance* Instance = In(WorldContext);
	const UCataclysmAccountSave* Account = Instance != nullptr ? Instance->AccountFor(Partition) : nullptr;
	if (Account == nullptr)
	{
		return false;
	}
	OutGold = Account->Gold;
	return true;
}

bool UCataclysmGameInstance::AddGoldFor(const UObject* WorldContext, int64 Gold, bool& bOutIntoTheCharacter)
{
	bOutIntoTheCharacter = false;
	if (Gold <= 0)
	{
		return false;
	}

	ACataclysmPlayerState* State = CataclysmGameInstanceGold::PlayingState(WorldContext);
	if (State == nullptr)
	{
		return false;
	}

	if (State->GetPartition().bSoloSelfFound)
	{
		State->AddOwnGold(Gold);
		bOutIntoTheCharacter = true;
		return true;
	}

	UCataclysmGameInstance* Instance = In(WorldContext);
	return Instance != nullptr && Instance->BankGold(State->GetPartition(), Gold) != nullptr;
}

bool UCataclysmGameInstance::SpendGoldFor(const UObject* WorldContext, int64 Gold, bool& bOutFromTheCharacter)
{
	bOutFromTheCharacter = false;
	if (Gold <= 0)
	{
		return false;
	}

	ACataclysmPlayerState* State = CataclysmGameInstanceGold::PlayingState(WorldContext);
	if (State == nullptr)
	{
		return false;
	}

	// A SOLO SELF-FOUND CHARACTER PAYS FROM ITS OWN, as it earns into its own (the owner's ruling of 2026-11-18).
	if (State->GetPartition().bSoloSelfFound)
	{
		if (State->GetOwnGold() < Gold)
		{
			return false;
		}
		State->SetOwnGold(State->GetOwnGold() - Gold);
		bOutFromTheCharacter = true;
		return true;
	}

	// EVERY OTHER CHARACTER FROM ITS LETHALITY MODE'S ACCOUNT, all or nothing.
	UCataclysmGameInstance* Instance = In(WorldContext);
	UCataclysmAccountSave* Account = Instance != nullptr ? Instance->AccountFor(State->GetPartition()) : nullptr;
	if (Account == nullptr || Account->Gold < Gold)
	{
		return false;
	}
	Account->Gold -= Gold;
	return true;
}

namespace CataclysmGameInstanceGold
{
	/** A debt cleared to nought takes nothing a day any more. */
	void SettleAccountDebt(UCataclysmAccountSave& Account)
	{
		if (Account.GoldOwed <= 0)
		{
			Account.GoldOwed = 0;
			Account.GoldOwedEachDay = 0;
		}
	}

	/** Sovereign Loans' part a day: a tenth of the debt, rounded up, so ten days pay it all (D4). */
	int64 DailyPartOf(int64 Owed)
	{
		const int64 Days = UCataclysmEmpireTree::SovereignLoansDays;
		return Owed > 0 ? (Owed + Days - 1) / Days : 0;
	}
}

bool UCataclysmGameInstance::GoldOwedBy(const UObject* WorldContext, int64& OutOwed, int64& OutEachDay)
{
	OutOwed = 0;
	OutEachDay = 0;
	const ACataclysmPlayerState* State = CataclysmGameInstanceGold::PlayingState(WorldContext);
	if (State == nullptr)
	{
		return false;
	}
	if (State->GetPartition().bSoloSelfFound)
	{
		OutOwed = State->GetOwnGoldOwed();
		OutEachDay = State->GetOwnGoldOwedEachDay();
		return true;
	}
	UCataclysmGameInstance* Instance = In(WorldContext);
	const UCataclysmAccountSave* Account = Instance != nullptr ? Instance->AccountFor(State->GetPartition()) : nullptr;
	if (Account == nullptr)
	{
		return false;
	}
	OutOwed = Account->GoldOwed;
	OutEachDay = Account->GoldOwedEachDay;
	return true;
}

bool UCataclysmGameInstance::GoldSpentOnCityUpgradesBy(const UObject* WorldContext, double& OutSpentAtTierOne)
{
	OutSpentAtTierOne = 0.0;
	const ACataclysmPlayerState* State = CataclysmGameInstanceGold::PlayingState(WorldContext);
	if (State == nullptr)
	{
		return false;
	}
	if (State->GetPartition().bSoloSelfFound)
	{
		OutSpentAtTierOne = State->GetOwnGoldSpentOnCityUpgrades();
		return true;
	}
	UCataclysmGameInstance* Instance = In(WorldContext);
	const UCataclysmAccountSave* Account = Instance != nullptr ? Instance->AccountFor(State->GetPartition()) : nullptr;
	if (Account == nullptr)
	{
		return false;
	}
	OutSpentAtTierOne = Account->GoldSpentOnCityUpgrades;
	return true;
}

bool UCataclysmGameInstance::AddGoldSpentOnCityUpgradesFor(const UObject* WorldContext, double SpentAtTierOne)
{
	ACataclysmPlayerState* State = CataclysmGameInstanceGold::PlayingState(WorldContext);
	if (State == nullptr)
	{
		return false;
	}
	const double Added = FMath::Max(0.0, SpentAtTierOne);
	if (State->GetPartition().bSoloSelfFound)
	{
		State->SetOwnGoldSpentOnCityUpgrades(State->GetOwnGoldSpentOnCityUpgrades() + Added);
		return true;
	}
	UCataclysmGameInstance* Instance = In(WorldContext);
	UCataclysmAccountSave* Account = Instance != nullptr ? Instance->AccountFor(State->GetPartition()) : nullptr;
	if (Account == nullptr)
	{
		return false;
	}
	Account->GoldSpentOnCityUpgrades = FMath::Max(0.0, Account->GoldSpentOnCityUpgrades) + Added;
	return true;
}

bool UCataclysmGameInstance::GoldSpentAtTheForgeBy(const UObject* WorldContext, double& OutSpentAtTierOne)
{
	OutSpentAtTierOne = 0.0;
	const ACataclysmPlayerState* State = CataclysmGameInstanceGold::PlayingState(WorldContext);
	if (State == nullptr)
	{
		return false;
	}
	if (State->GetPartition().bSoloSelfFound)
	{
		OutSpentAtTierOne = State->GetOwnGoldSpentAtTheForge();
		return true;
	}
	UCataclysmGameInstance* Instance = In(WorldContext);
	const UCataclysmAccountSave* Account = Instance != nullptr ? Instance->AccountFor(State->GetPartition()) : nullptr;
	if (Account == nullptr)
	{
		return false;
	}
	OutSpentAtTierOne = Account->GoldSpentAtTheForge;
	return true;
}

bool UCataclysmGameInstance::AddGoldSpentAtTheForgeFor(const UObject* WorldContext, double SpentAtTierOne)
{
	ACataclysmPlayerState* State = CataclysmGameInstanceGold::PlayingState(WorldContext);
	if (State == nullptr)
	{
		return false;
	}
	const double Added = FMath::Max(0.0, SpentAtTierOne);
	if (State->GetPartition().bSoloSelfFound)
	{
		State->SetOwnGoldSpentAtTheForge(State->GetOwnGoldSpentAtTheForge() + Added);
		return true;
	}
	UCataclysmGameInstance* Instance = In(WorldContext);
	UCataclysmAccountSave* Account = Instance != nullptr ? Instance->AccountFor(State->GetPartition()) : nullptr;
	if (Account == nullptr)
	{
		return false;
	}
	Account->GoldSpentAtTheForge = FMath::Max(0.0, Account->GoldSpentAtTheForge) + Added;
	return true;
}

bool UCataclysmGameInstance::SpendGoldOnCreditFor(const UObject* WorldContext, int64 Price, int64 Allowance,
												  bool bRepaidDaily, bool& bOutFromTheCharacter, int64& OutOwed)
{
	bOutFromTheCharacter = false;
	OutOwed = 0;
	ACataclysmPlayerState* State = CataclysmGameInstanceGold::PlayingState(WorldContext);
	if (Price <= 0 || Allowance <= 0 || State == nullptr)
	{
		return false;
	}

	// A SOLO SELF-FOUND CHARACTER BORROWS AGAINST ITS OWN, as it pays from its own.
	if (State->GetPartition().bSoloSelfFound)
	{
		const int64 Short = Price - State->GetOwnGold();
		if (State->GetOwnGoldOwed() > 0 || Short <= 0 || Short > Allowance)
		{
			return false;
		}
		State->SetOwnGold(0);
		State->SetOwnGoldOwed(Short, bRepaidDaily ? CataclysmGameInstanceGold::DailyPartOf(Short) : 0);
		bOutFromTheCharacter = true;
		OutOwed = Short;
		return true;
	}

	// EVERY OTHER CHARACTER AGAINST ITS LETHALITY MODE'S ACCOUNT: the whole balance, and the rest owed (D2).
	UCataclysmGameInstance* Instance = In(WorldContext);
	UCataclysmAccountSave* Account = Instance != nullptr ? Instance->AccountFor(State->GetPartition()) : nullptr;
	if (Account == nullptr)
	{
		return false;
	}
	const int64 Short = Price - Account->Gold;
	if (Account->GoldOwed > 0 || Short <= 0 || Short > Allowance)
	{
		return false;
	}
	Account->Gold = 0;
	Account->GoldOwed = Short;
	Account->GoldOwedEachDay = bRepaidDaily ? CataclysmGameInstanceGold::DailyPartOf(Short) : 0;
	OutOwed = Short;
	return true;
}

bool UCataclysmGameInstance::UndoCreditFor(const UObject* WorldContext, int64 Taken, int64 Owed)
{
	ACataclysmPlayerState* State = CataclysmGameInstanceGold::PlayingState(WorldContext);
	if (State == nullptr)
	{
		return false;
	}
	if (State->GetPartition().bSoloSelfFound)
	{
		State->SetOwnGold(State->GetOwnGold() + FMath::Max<int64>(0, Taken));
		State->SetOwnGoldOwed(State->GetOwnGoldOwed() - FMath::Max<int64>(0, Owed), State->GetOwnGoldOwedEachDay());
		return true;
	}
	UCataclysmGameInstance* Instance = In(WorldContext);
	UCataclysmAccountSave* Account = Instance != nullptr ? Instance->AccountFor(State->GetPartition()) : nullptr;
	if (Account == nullptr)
	{
		return false;
	}
	Account->Gold += FMath::Max<int64>(0, Taken);
	Account->GoldOwed -= FMath::Max<int64>(0, Owed);
	CataclysmGameInstanceGold::SettleAccountDebt(*Account);
	return true;
}

bool UCataclysmGameInstance::AddPickedUpGoldFor(const UObject* WorldContext, int64 Gold, bool& bOutIntoTheCharacter,
												int64& OutRepaid)
{
	bOutIntoTheCharacter = false;
	OutRepaid = 0;
	if (Gold <= 0)
	{
		return false;
	}
	ACataclysmPlayerState* State = CataclysmGameInstanceGold::PlayingState(WorldContext);
	if (State == nullptr)
	{
		return false;
	}

	// THE DEBT FIRST, THEN THE BALANCE, in one place so the two change together (D3).
	if (State->GetPartition().bSoloSelfFound)
	{
		OutRepaid = FMath::Min(Gold, State->GetOwnGoldOwed());
		State->SetOwnGoldOwed(State->GetOwnGoldOwed() - OutRepaid, State->GetOwnGoldOwedEachDay());
		State->AddOwnGold(Gold - OutRepaid);
		bOutIntoTheCharacter = true;
		return true;
	}
	UCataclysmGameInstance* Instance = In(WorldContext);
	UCataclysmAccountSave* Account = Instance != nullptr ? Instance->AccountFor(State->GetPartition()) : nullptr;
	if (Account == nullptr)
	{
		return false;
	}
	OutRepaid = FMath::Min(Gold, FMath::Max<int64>(0, Account->GoldOwed));
	Account->GoldOwed -= OutRepaid;
	CataclysmGameInstanceGold::SettleAccountDebt(*Account);
	Account->Gold += Gold - OutRepaid;
	return true;
}

int64 UCataclysmGameInstance::RepayTheDaysPartFor(const UObject* WorldContext, bool& bOutFromTheCharacter)
{
	bOutFromTheCharacter = false;
	ACataclysmPlayerState* State = CataclysmGameInstanceGold::PlayingState(WorldContext);
	if (State == nullptr)
	{
		return 0;
	}

	// THE PART SAVED AT THE PURCHASE, OR THE DEBT OR THE BALANCE IF LESS (D4).
	if (State->GetPartition().bSoloSelfFound)
	{
		const int64 Paid = FMath::Max<int64>(0, FMath::Min3(State->GetOwnGoldOwedEachDay(), State->GetOwnGoldOwed(),
															 State->GetOwnGold()));
		if (Paid > 0)
		{
			State->SetOwnGold(State->GetOwnGold() - Paid);
			State->SetOwnGoldOwed(State->GetOwnGoldOwed() - Paid, State->GetOwnGoldOwedEachDay());
			bOutFromTheCharacter = true;
		}
		return Paid;
	}
	UCataclysmGameInstance* Instance = In(WorldContext);
	UCataclysmAccountSave* Account = Instance != nullptr ? Instance->AccountFor(State->GetPartition()) : nullptr;
	if (Account == nullptr)
	{
		return 0;
	}
	const int64 Paid = FMath::Max<int64>(0, FMath::Min3(Account->GoldOwedEachDay, Account->GoldOwed, Account->Gold));
	if (Paid > 0)
	{
		Account->Gold -= Paid;
		Account->GoldOwed -= Paid;
		CataclysmGameInstanceGold::SettleAccountDebt(*Account);
	}
	return Paid;
}

FString UCataclysmGameInstance::GoldBalanceText(const UObject* WorldContext)
{
	int64 Held = 0;
	if (!GoldHeldBy(WorldContext, Held))
	{
		return FString();
	}

	// AND THE DEBT BESIDE IT WHILE ONE IS OPEN (D6), so no screen shows a balance as if nothing were owed.
	int64 Owed = 0;
	int64 EachDay = 0;
	GoldOwedBy(WorldContext, Owed, EachDay);
	return Owed > 0 ? FString::Printf(TEXT("%s, %s owed"), *ACataclysmDroppedItem::GoldName(Held),
									  *ACataclysmDroppedItem::GoldAmountText(Owed))
					: ACataclysmDroppedItem::GoldName(Held);
}

bool UCataclysmGameInstance::BankEmpirePointsFor(const UObject* WorldContext, double Points,
											 FCataclysmHeldEmpireTree& OutHeld)
{
	if (!EmpireTreeHeldBy(WorldContext, OutHeld))
	{
		return false;
	}
	// A SUBTRACTION, which is not banking: spending is the tree's. An award of nought is banked, as `BankEmpirePoints`
	// banks it, so the caller's write and log do not change with the figure.
	if (Points < 0.0)
	{
		OutHeld.Why = TEXT("Empire upgrade points below nought are not banked.");
		return false;
	}
	// THE FRACTION IS KEPT, as `BankEmpirePoints` keeps it: ruled 2026-11-03.
	*OutHeld.Bank += Points;
	return true;
}

FCataclysmEmpireTreeBonuses UCataclysmGameInstance::EmpireTreeBonusesFor(const UObject* WorldContext)
{
	FCataclysmHeldEmpireTree Held;
	if (!EmpireTreeHeldBy(WorldContext, Held))
	{
		return FCataclysmEmpireTreeBonuses();
	}
	FCataclysmEmpireTreeBonuses Bonuses = UCataclysmEmpireTree::CityBonusesFor(
		UCataclysmEmpireTree::LoadNodeTable(), UCataclysmEmpireTree::LoadEffectTable(), *Held.Allocation);

	// AND REINVESTMENT'S SHARE, which reads the gold spent across runs that this module holds and the run's cannot
	// (issue #2741 slice 9, V2, V3).
	double Spent = 0.0;
	GoldSpentOnCityUpgradesBy(WorldContext, Spent);
	Bonuses.EmpirePointsFromSpending =
		UCataclysmEmpireTree::EmpirePointsFromSpendingFor(EmpireTreePlayerBonusesFor(WorldContext), Spent);
	return Bonuses;
}

FCataclysmEmpireTreePlayerBonuses UCataclysmGameInstance::EmpireTreePlayerBonusesFor(const UObject* WorldContext)
{
	FCataclysmHeldEmpireTree Held;
	if (!EmpireTreeHeldBy(WorldContext, Held))
	{
		return FCataclysmEmpireTreePlayerBonuses();
	}
	return UCataclysmEmpireTree::PlayerBonusesFor(UCataclysmEmpireTree::LoadNodeTable(),
												  UCataclysmEmpireTree::LoadEffectTable(), *Held.Allocation);
}

void UCataclysmGameInstance::WireTheRunToTheBalance(const UObject* WorldContext, UCataclysmEmpireRun* Run)
{
	UCataclysmGameInstance* Instance = In(WorldContext);
	if (Run == nullptr || Instance == nullptr)
	{
		return;
	}
	const TWeakObjectPtr<UCataclysmGameInstance> Weak(Instance);

	// THE GOLD HELD, IN TIER 1 GOLD (R10), asked whenever a dungeon damages a city (B7).
	Run->GoldHeldAtTierOne = [Weak, WeakRun = TWeakObjectPtr<UCataclysmEmpireRun>(Run)]() -> double
	{
		int64 Held = 0;
		if (!Weak.IsValid() || !WeakRun.IsValid() || !GoldHeldBy(Weak.Get(), Held))
		{
			return 0.0;
		}
		return static_cast<double>(Held) / FMath::Max(1.0e-6, UCataclysmDropRoll::GoldScaleFor(WeakRun->DifficultyTier));
	};

	// AND THE DAY'S INCOME, paid into the balance of the character playing, with the saves a gold pickup raises (B8).
	Run->OnDayPassed = [Weak](UCataclysmEmpireRun& DayRun, int32 Day)
	{
		UCataclysmGameInstance* Paying = Weak.Get();
		if (Paying == nullptr)
		{
			return;
		}
		int64 Held = 0;
		GoldHeldBy(Paying, Held);
		const FCataclysmEmpireTreeDailyIncome Income = UCataclysmEmpireTree::DailyIncomeFor(
			EmpireTreePlayerBonusesFor(Paying), UCataclysmEmpireTree::EmpireStateFor(DayRun), Day, Held,
			UCataclysmDropRoll::GoldScaleFor(DayRun.DifficultyTier));
		bool bIntoTheCharacter = false;
		const bool bPaid = Income.Total() > 0 && AddGoldFor(Paying, Income.Total(), bIntoTheCharacter);

		// AND SOVEREIGN LOANS' PART OF A DEBT, AFTER THE DAY'S INCOME (slice 6e, D4).
		bool bRepaidFromTheCharacter = false;
		const int64 Repaid = RepayTheDaysPartFor(Paying, bRepaidFromTheCharacter);
		if (!bPaid && Repaid <= 0)
		{
			return;
		}
		UWorld* World = Paying->GetWorld();
		if ((bPaid && !bIntoTheCharacter) || (Repaid > 0 && !bRepaidFromTheCharacter))
		{
			UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::AccountChanged);
		}
		UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::InventoryChanged);
		UE_LOG(LogCataclysm, Log, TEXT("Day %d: the empire upgrade tree paid %lld gold (Pax Imperialis %lld, Wealth "
									   "Interest %lld, Tax Collection %lld, Trade Routes %lld), and %lld of a debt was "
									   "repaid."),
			   Day, bPaid ? Income.Total() : 0, Income.Pax, Income.Interest, Income.Tax, Income.Trade, Repaid);
	};

	// AND ECONOMIC BAILOUT'S FEE, all or nothing, with no credit, and the log saying why when it is not paid (V9).
	// Issue #2741 slice 9.
	Run->PayTheBailout = [Weak](UCataclysmEmpireRun& BailRun, int32 CityId, ECataclysmCityTier Tier) -> bool
	{
		UCataclysmGameInstance* Paying = Weak.Get();
		const int64 AtTierOne = UCataclysmEmpireTree::BailoutFeeAtTierOneFor(Tier);
		if (Paying == nullptr || AtTierOne <= 0)
		{
			return false;
		}
		const int64 Fee = static_cast<int64>(FMath::RoundToDouble(
			static_cast<double>(AtTierOne) * UCataclysmDropRoll::GoldScaleFor(BailRun.DifficultyTier)));
		int64 Held = 0;
		const bool bBalance = GoldHeldBy(Paying, Held);
		bool bFromTheCharacter = false;
		if (!bBalance || Held < Fee || !SpendGoldFor(Paying, Fee, bFromTheCharacter))
		{
			FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::BailoutLine(&BailRun, CityId, Fee, bBalance ? Held : -1, false));
			return false;
		}
		UWorld* World = Paying->GetWorld();
		if (!bFromTheCharacter)
		{
			UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::AccountChanged);
		}
		UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::InventoryChanged);
		int64 After = 0;
		GoldHeldBy(Paying, After);
		FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::BailoutLine(&BailRun, CityId, Fee, After, true));
		return true;
	};
}

void UCataclysmGameInstance::RefreshEmpireTreeBonuses(const UObject* WorldContext)
{
	if (UCataclysmEmpireRun* Run = EmpireRunFor(WorldContext, /*bStartIfNone*/ false))
	{
		// A TREE CHANGED IN PLAY, so every city keeps its share of its maxima: "It should maintain the % they were at
		// before buying the upgrades." Issue #2859.
		Run->ChangeTreeBonuses(EmpireTreeBonusesFor(WorldContext));
		WireTheRunToTheBalance(WorldContext, Run);
	}
}

void UCataclysmGameInstance::HandEmpireTreeBonusesToALoadedRun(const UObject* WorldContext)
{
	if (UCataclysmEmpireRun* Run = EmpireRunFor(WorldContext, /*bStartIfNone*/ false))
	{
		// NOTHING WRITTEN INTO THE CITIES: they were saved holding what they held under the tree, and keeping their
		// shares from a run that has had no tree since it was rebuilt would scale them by the whole tree. Issue #2859.
		Run->SetTreeBonuses(EmpireTreeBonusesFor(WorldContext));
		WireTheRunToTheBalance(WorldContext, Run);
	}
}

void UCataclysmGameInstance::NoteTheEmpireTreeChanged(const UObject* WorldContext, const FCataclysmHeldEmpireTree& Held)
{
	const UWorld* World = WorldContext != nullptr && GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	UCataclysmSaveWriter::NoteTriggerIn(World, Held.bTheCharactersOwn ? ECataclysmSaveTrigger::OwnEmpireTreeChanged
																	  : ECataclysmSaveTrigger::AccountChanged);
}

namespace CataclysmGameInstanceEmpireTree
{
	/** What a buy, a choice and a respec say when the tree's two tables are not there to read. */
	const TCHAR* const TablesMissing = TEXT("The empire upgrade tree's tables did not load, so nothing in it can be bought.");

	/** The world a context is in, or null: what `UCataclysmSaveWriter::NoteTriggerIn` is handed. */
	const UWorld* WorldOf(const UObject* WorldContext)
	{
		return WorldContext != nullptr && GEngine != nullptr
			? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
			: nullptr;
	}

	/** A node's name as the tree file gives it, for a log line; its row name when the table has no such row. */
	FString NameOf(const UDataTable* NodeTable, FName Node)
	{
		const FCataclysmEmpireTreeNodeRow* Row = UCataclysmEmpireTree::FindNode(NodeTable, Node);
		return Row != nullptr ? Row->NodeName : Node.ToString();
	}
}

bool UCataclysmGameInstance::EmpireTreeHeldBy(const UObject* WorldContext, FCataclysmHeldEmpireTree& OutHeld)
{
	OutHeld = FCataclysmHeldEmpireTree();
	ACataclysmPlayerState* State = CataclysmGameInstanceGold::PlayingState(WorldContext);
	if (State == nullptr)
	{
		OutHeld.Why = TEXT("Nobody is playing, so there is no empire upgrade tree to read.");
		return false;
	}

	// A SOLO SELF-FOUND CHARACTER'S TREE IS ITS OWN, on its player state and saved in its character record: the owner's
	// ruling of 2026-11-18, which reversed the coordinating session's C8. Issue #2741 slice 1c.
	const FCataclysmCharacterPartition& Partition = State->GetPartition();
	if (Partition.bSoloSelfFound)
	{
		OutHeld.Bank = &State->EditOwnEmpireUpgradePoints();
		OutHeld.Allocation = &State->EditOwnEmpireTree();
		OutHeld.bTheCharactersOwn = true;
		return true;
	}

	// EVERY OTHER CHARACTER'S LETHALITY MODE'S, held once per partition in the account record.
	UCataclysmGameInstance* Instance = In(WorldContext);
	if (Instance == nullptr)
	{
		OutHeld.Why = TEXT("There is no game instance of this project's, so no account record holds an empire upgrade "
						   "tree.");
		return false;
	}
	UCataclysmAccountSave* Account = Instance->AccountFor(Partition);
	if (Account == nullptr)
	{
		OutHeld.Why = TEXT("The account record of this lethality mode would not read, so its empire upgrade tree "
						   "cannot be bought into this session.");
		return false;
	}

	OutHeld.Bank = &Account->EmpireUpgradePoints;
	OutHeld.Allocation = &Account->EmpireTree;
	return true;
}

FString UCataclysmGameInstance::EmpireTreeChangeRefusal(const UObject* WorldContext)
{
	// ONE RULE FOR BUYING, CHOOSING AND RESPEC: NOT DURING A WALK. Issue #2741 slice 2, ruled by the coordinating session
	// on 2026-11-18. Slices 3 to 5 read the tree where each number is read, so a point bought mid-walk would change the
	// walk under way -- its days, its floors, its loot -- and a player could bank points and buy them inside a dungeon to
	// shorten the walk they are on; the design puts the tree's services at the capital. And for a respec, the reason
	// `Cataclysm.EmpireAdvance` gives too: nothing resolves while the player is inside a dungeon (the owner, 2026-10-24).
	const UCataclysmEmpireRun* Run = EmpireRunFor(WorldContext, /*bStartIfNone*/ false);
	if (Run != nullptr && Run->IsAWalkUnderWay())
	{
		return TEXT("The empire upgrade tree can be changed in the capital, not during a walk.");
	}
	return FString();
}

bool UCataclysmGameInstance::BuyEmpireTreePoint(const UObject* WorldContext, FName Node, FString& OutReason)
{
	FCataclysmHeldEmpireTree Held;
	if (!EmpireTreeHeldBy(WorldContext, Held))
	{
		OutReason = Held.Why;
		return false;
	}
	OutReason = EmpireTreeChangeRefusal(WorldContext);
	if (!OutReason.IsEmpty())
	{
		return false;
	}

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Edges = UCataclysmEmpireTree::LoadEdgeTable();
	if (Nodes == nullptr || Edges == nullptr)
	{
		OutReason = CataclysmGameInstanceEmpireTree::TablesMissing;
		return false;
	}

	if (!UCataclysmEmpireTree::Spend(Nodes, Edges, *Held.Allocation, Node, *Held.Bank, OutReason))
	{
		return false;
	}

	// THE NODE ACTS AT ONCE: the run is handed the tree as it now stands. Issue #2741 slice 3a.
	RefreshEmpireTreeBonuses(WorldContext);

	// THE PLAY LOOP: the point, the node's points after it, and the bank. Issue #2741 slice 2.
	const FCataclysmEmpireTreeNodeRow* Row = UCataclysmEmpireTree::FindNode(Nodes, Node);
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::TreePointLine(
		CataclysmGameInstanceEmpireTree::NameOf(Nodes, Node), Row != nullptr ? Row->Branch : FString(),
		Held.Allocation->PointsIn(Node), Row != nullptr ? Row->MaxPoints : 0, *Held.Bank));
	NoteTheEmpireTreeChanged(WorldContext, Held);
	return true;
}

bool UCataclysmGameInstance::ChooseEmpireTreeOption(const UObject* WorldContext, FName Node, int32 Option,
												   FString& OutReason)
{
	FCataclysmHeldEmpireTree Held;
	if (!EmpireTreeHeldBy(WorldContext, Held))
	{
		OutReason = Held.Why;
		return false;
	}
	OutReason = EmpireTreeChangeRefusal(WorldContext);
	if (!OutReason.IsEmpty())
	{
		return false;
	}

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	if (Nodes == nullptr)
	{
		OutReason = CataclysmGameInstanceEmpireTree::TablesMissing;
		return false;
	}

	if (!UCataclysmEmpireTree::ChooseOption(Nodes, *Held.Allocation, Node, Option, OutReason))
	{
		return false;
	}

	// THE OPTION ACTS AT ONCE, once the capstone's point is bought. Issue #2741 slice 3a.
	RefreshEmpireTreeBonuses(WorldContext);

	// THE PLAY LOOP: which option of which capstone. Issue #2741 slice 2.
	const FCataclysmEmpireTreeNodeRow* Row = UCataclysmEmpireTree::FindNode(Nodes, Node);
	const TArray<FString> Options = Row != nullptr ? UCataclysmEmpireTree::OptionNamesOf(*Row) : TArray<FString>();
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::TreeOptionLine(
		Options.IsValidIndex(Option - 1) ? Options[Option - 1] : FString::Printf(TEXT("option %d"), Option),
		CataclysmGameInstanceEmpireTree::NameOf(Nodes, Node)));
	NoteTheEmpireTreeChanged(WorldContext, Held);
	return true;
}

bool UCataclysmGameInstance::ChooseEmpireTreeExtraOption(const UObject* WorldContext, FName Node, int32 Option,
														FString& OutReason)
{
	FCataclysmHeldEmpireTree Held;
	if (!EmpireTreeHeldBy(WorldContext, Held))
	{
		OutReason = Held.Why;
		return false;
	}
	OutReason = EmpireTreeChangeRefusal(WorldContext);
	if (!OutReason.IsEmpty())
	{
		return false;
	}

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmEmpireTree::LoadEffectTable();
	if (Nodes == nullptr || Effects == nullptr)
	{
		OutReason = CataclysmGameInstanceEmpireTree::TablesMissing;
		return false;
	}

	if (!UCataclysmEmpireTree::ChooseExtraOption(Nodes, Effects, *Held.Allocation, Node, Option, OutReason))
	{
		return false;
	}

	// THE EXTRA OPTION ACTS AT ONCE: its milestone's point is already bought (J1). Issue #2741 slice 8.
	RefreshEmpireTreeBonuses(WorldContext);

	// THE PLAY LOOP: which option of which milestone, as a choice says it.
	const FCataclysmEmpireTreeNodeRow* Row = UCataclysmEmpireTree::FindNode(Nodes, Node);
	const TArray<FString> Options = Row != nullptr ? UCataclysmEmpireTree::OptionNamesOf(*Row) : TArray<FString>();
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::TreeOptionLine(
		Options.IsValidIndex(Option - 1) ? Options[Option - 1] : FString::Printf(TEXT("option %d"), Option),
		CataclysmGameInstanceEmpireTree::NameOf(Nodes, Node)));
	NoteTheEmpireTreeChanged(WorldContext, Held);
	return true;
}

bool UCataclysmGameInstance::RespecEmpireTree(const UObject* WorldContext, int32& OutPointsReturned, int32& OutDays,
											  FString& OutReason)
{
	OutPointsReturned = 0;
	OutDays = 0;

	FCataclysmHeldEmpireTree Held;
	if (!EmpireTreeHeldBy(WorldContext, Held))
	{
		OutReason = Held.Why;
		return false;
	}

	// NOT DURING A WALK, the one rule buying and choosing obey too (`EmpireTreeChangeRefusal`, slice 2).
	OutReason = EmpireTreeChangeRefusal(WorldContext);
	if (!OutReason.IsEmpty())
	{
		return false;
	}

	// THE RUN THE DAYS COME OFF: the one this game instance holds, and none is begun to pay for a respec.
	UCataclysmEmpireRun* Run = EmpireRunFor(WorldContext, /*bStartIfNone*/ false);
	OutReason = UCataclysmEmpireTree::RefusalForRespec(*Held.Allocation, Run != nullptr);
	if (!OutReason.IsEmpty())
	{
		return false;
	}

	const int32 Days = UCataclysmEmpireTree::RespecDays(Run->Surges != nullptr ? Run->Surges->LethalityRung : 0);
	OutPointsReturned = UCataclysmEmpireTree::Respec(*Held.Allocation, *Held.Bank, /*bRunBeingPlayed*/ true, OutReason);
	if (!OutReason.IsEmpty())
	{
		return false; // The same rule refused it above; this cannot happen, and changes nothing if it does.
	}

	// THE TREE GOES BEFORE THE DAYS PASS, so they pass without it. Issue #2741 slice 3a.
	RefreshEmpireTreeBonuses(WorldContext);

	// THE DAYS, through the call every other run of days goes through; THEIR REPORTS KEPT, and the dungeons standing named
	// before them, since a dungeon the days resolve is gone from the run after. Issue #2857.
	const int32 DayBefore = Run->Day();
	const TMap<int32, FString> NamesBeforeTheDays = FCataclysmPlayLoopLog::NamesStanding(Run);
	const TArray<FCataclysmDayReport> TheDays = Run->AdvanceDays(Days);
	int32 Resolved = 0;
	int32 Fallen = 0;
	for (const FCataclysmDayReport& Report : TheDays)
	{
		Resolved += Report.Resolved.Num();
		Fallen += Report.Fallen.Num();
	}
	OutDays = Days;

	UE_LOG(LogCataclysm, Log,
		   TEXT("Empire upgrade tree respecced: %d point(s) returned, %.3f banked. It cost %d days, day %d to day %d, "
				"in which %d dungeon(s) resolved and %d city(ies) fell."),
		   OutPointsReturned, *Held.Bank, Days, DayBefore, Run->Day(), Resolved, Fallen);
	// THE PLAY LOOP: the points back, the bank, and the days it cost. Issue #2741 slice 2.
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::TreeRespecLine(OutPointsReturned, *Held.Bank, Days, DayBefore,
		Run->Day()));
	// AND WHAT THOSE DAYS DID, in the walk's words after the respec's own lead, said whatever they did; and kept for the
	// tree's screen to show when they did something. Issue #2857.
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::AwayAccount(DayBefore, Run->Day(), TheDays, NamesBeforeTheDays, Run,
																  TEXT("for the respec")));
	KeepCapitalDays(WorldContext, DayBefore, Run->Day(), TheDays, NamesBeforeTheDays, Run);

	// SAVED WHOLE: the points, in the record that holds the tree, and the run's days by a trigger that writes the run
	// record whatever else wrote it earlier in this frame. `Clock` would have been skipped then; the coordinating
	// session's ruling of 2026-11-18.
	NoteTheEmpireTreeChanged(WorldContext, Held);
	UCataclysmSaveWriter::NoteTriggerIn(CataclysmGameInstanceEmpireTree::WorldOf(WorldContext),
										ECataclysmSaveTrigger::EmpireTreeRespecced);
	return true;
}

void UCataclysmGameInstance::KeepCapitalDays(const UObject* WorldContext, int32 FromDay, int32 ToDay,
											 const TArray<FCataclysmDayReport>& Days,
											 const TMap<int32, FString>& NamesBefore, const UCataclysmEmpireRun* Run)
{
	UCataclysmGameInstance* Instance = In(WorldContext);
	if (Instance == nullptr)
	{
		return;
	}

	// THE PLAY LOOP'S OWN ACCOUNT OF THE DAYS, after "Meanwhile", so the screen and the log name the same dungeons and the
	// same figures; and NOTHING WHEN THE DAYS DID NOTHING, so a screen says no more than it did before. Issue #2857.
	Instance->CapitalDaysAccount = ToDay > FromDay && FCataclysmPlayLoopLog::DaysDidSomething(Days)
		? FCataclysmPlayLoopLog::AwayAccount(FromDay, ToDay, Days, NamesBefore, Run, TEXT("Meanwhile")) + TEXT(".")
		: FString();
}

FString UCataclysmGameInstance::CapitalDaysAccountIn(const UObject* WorldContext)
{
	const UCataclysmGameInstance* Instance = In(WorldContext);
	return Instance != nullptr ? Instance->CapitalDaysAccount : FString();
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
