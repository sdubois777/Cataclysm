// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/GameInstance.h"
#include "Save/CataclysmSavePartition.h"
#include "CataclysmGameInstance.generated.h"

class ACataclysmPlayerState;
class UCataclysmAccountSave;
class UCataclysmEmpireRun;
class UCataclysmSaveWriter;
struct FCataclysmPassiveAllocation;

/**
 * The empire upgrade tree the character playing has, and the bank it is bought with: what
 * `UCataclysmGameInstance::EmpireTreeHeldBy` answers. Issue #2741 slice 1b.
 *
 * POINTERS INTO WHAT HOLDS THEM, to read and change at once and never to keep: the account record the game instance
 * holds (`HeldAccount`), which asking `AccountFor` for another partition replaces; or, for a Solo Self-Found character,
 * its player state's own two fields, which its character record saves (slice 1c). Behind the same two pointers, so
 * nothing that reads them asks which it is -- except what raises the save trigger, which reads `bTheCharactersOwn`.
 */
struct FCataclysmHeldEmpireTree
{
	/** Empire upgrade points banked and not yet spent; null when there is no tree. */
	double* Bank = nullptr;

	/** What the tree has bought; null when there is no tree. */
	FCataclysmPassiveAllocation* Allocation = nullptr;

	/** Why there is no tree, in a sentence a screen or a log can show; empty when there is one. */
	FString Why;

	/**
	 * True when the tree and bank are a Solo Self-Found character's own, which its character record saves; false when
	 * they are its lethality mode's, which the account record saves. Issue #2741 slice 1c.
	 */
	bool bTheCharactersOwn = false;

	bool IsHeld() const { return Bank != nullptr && Allocation != nullptr; }
};

/**
 * A character made on the front end's new-character screen, waiting for the level its Confirm opened to begin: the name
 * and partition, locked at creation, and the weapon type and damage type. Issue #753, select-screen step 4a.
 */
struct FCataclysmPendingCreation
{
	bool bPending = false;
	FString Name;
	FCataclysmCharacterPartition Partition;
	FName WeaponType;
	FName DamageType;
};

/**
 * What survives travelling between levels. Today that is the empire run.
 *
 * WHY THE RUN CANNOT LIVE ON THE GAME MODE. A game mode is created per level and
 * destroyed with it, and the player is going to walk out of the capital into a
 * dungeon level and back again forty days later. An empire that started over
 * every time the level changed would not be an empire. `ACataclysmGameMode`'s own
 * header already says where it belongs: "This game has a run that ends when the
 * capital falls or the boss dies, and the empire layer owns that."
 *
 * A game instance is created once when the game starts and lives until it quits,
 * which is exactly the lifetime a run wants.
 *
 * WHY THERE WAS NONE UNTIL NOW. Nothing needed one. `game/Config/DefaultEngine.ini`
 * set `GlobalDefaultGameMode` and no game instance class at all, so the engine's
 * own `UGameInstance` was used. This is the first thing in the project whose
 * lifetime is longer than a level's. Issue #1087.
 *
 * WHAT IT DOES NOT DO YET:
 *
 *   - **It does not advance the day by itself.** Nothing does. The day moves
 *     when a console command says so, and eventually when the player finishes a
 *     dungeon or spends days at the forge.
 *   - **It does not load or save a run itself.** The save writer reads the
 *     run from here, and `FCataclysmSaveLoad` puts a loaded one into
 *     `EmpireRun` (#753).
 *   - **It does nothing when the run is lost.** The Cataclysm reaching the
 *     Pillar opens the Last Stand, which is issue #43.
 */
UCLASS()
class CATACLYSM_API UCataclysmGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	/**
	 * The run in progress, or null before one has been started.
	 *
	 * NULL RATHER THAN AN EMPTY RUN, so "no run has been started" and "a run in
	 * which nothing has happened yet" are different states. They are: the second
	 * has a surge due.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TObjectPtr<UCataclysmEmpireRun> EmpireRun;

	/**
	 * The run in progress, starting one if there is none.
	 *
	 * THE SEED COMES FROM THE CLOCK, so two sessions are two different empires.
	 * A run started deliberately, by a console command or a test, passes its own
	 * seed to `BeginEmpireRun` and gets the same empire every time.
	 *
	 * AND THE DIFFICULTY TIER COMES FROM THE GAME, through `BeginEmpireRun`
	 * below rather than through a second read here. This is the path
	 * `Cataclysm.EmpireMap` and `Cataclysm.EmpireAdvance` reach, so it is the
	 * one that has to be right. Issue #1444.
	 *
	 * IT NAMES NO CHARACTER, so the clock's seed also draws the run's Cataclysms
	 * (`BeginEmpireRun`). In the C++, only console commands and the M key reach
	 * it (`Cataclysm.EmpireMap` and M through
	 * `ACataclysmPlayerController::ToggleEmpireMap`, `Cataclysm.EmpireAdvance`,
	 * `Cataclysm.EnterDungeon`); a character's run comes through
	 * `BeginACharactersEmpireRun`, and once one is held none of these begins
	 * another. Issue #1324 slice 6a; the M key since 2026-11-17.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	UCataclysmEmpireRun* GetOrBeginEmpireRun();

	/**
	 * THE CHARACTER SELECT SCREEN'S HAND-OVER, which outlives the level travel between the screen and the game. Issue
	 * #753, select-screen step 3a, ruled 2026-10-31.
	 *
	 * `PendingCharacterId`: the character Continue chose, loaded by `ACataclysmGameMode::LoadThePendingCharacter` when
	 * the level it opened begins, and cleared before that load runs; invalid when nothing is pending, which is every
	 * start but a Continue. `LastLoadRefusal`: why that load was refused, which the screen shows and clears.
	 * `LastLoadNotice`: why it began a new run instead, kept and logged; the heads-up display shows it later (ruled).
	 */
	FGuid PendingCharacterId;
	FString LastLoadRefusal;
	FString LastLoadNotice;

	/**
	 * THE CAPITAL'S WALK, carried to the dungeon level with the character (`PendingCharacterId`): the empire dungeon the
	 * arriving dungeon level enters once the character is loaded (`ACataclysmDungeonGameMode::EnterThePendingWalkIfReady`),
	 * cleared before it is entered. INDEX_NONE when no walk is pending. Issue #2733 slice 3.
	 */
	int32 PendingWalkDungeonId = INDEX_NONE;

	/**
	 * THE PLAY LOOP LOG'S ACCOUNT OF A WALK'S DAYS AWAY, carried from the dungeon level to the capital, which says it in
	 * its arrival line and empties it (`FCataclysmPlayLoopLog::AwayAccount`). Built where the days' own reports still
	 * exist: the run record keeps only their summary, and a dungeon the days resolved is no longer on the run. NOT SAVED:
	 * a log line, lost with the session, like the game instance. Empty when no walk has just ended. Issue #2733 slice 4.
	 */
	FString PlayLoopAwayAccount;

	/** The new character the new-character screen made, made by the level's start and cleared first. Step 4a. */
	FCataclysmPendingCreation PendingCreation;

	/**
	 * Throws away whatever run was in progress and starts a fresh one.
	 *
	 * @param Seed          what to seed it with. The same seed gives the same
	 *                      empire: the same waves on the same cities, and
	 *                      therefore the same cities lost on the same days.
	 * @param Mode          how surges escalate. An open tuning question; see
	 *                      `ECataclysmSurgeMode`.
	 * @param LethalityRung 0 Standard, 1 Hardcore, 2 Heretic. Heretic surges
	 *                      bring 25% more dungeons.
	 * @param DifficultyTier which tier the run is played at, 1 to 8, or 0 to
	 *                      take whatever tier the game is being played at. It
	 *                      decides how many Cataclysms the run faces and how
	 *                      many modifiers each dungeon carries.
	 *
	 * **ZERO MEANS ASK THE GAME, AND IT IS THE DEFAULT.** The tier then comes
	 * from `ACataclysmGameMode::DifficultyTierIn`, which is the one place that
	 * answers what tier this world is being played at: the
	 * `Cataclysm.DifficultyTier` console variable when it has been set above
	 * zero, then the game mode's own `DifficultyTier`, then tier 1.
	 *
	 * IT IS READ HERE RATHER THAN BY EACH CALLER, because this is the only door
	 * into a run and a caller that forgets to pass a tier is exactly what went
	 * wrong before: every run the game started was tier 1, faced one Cataclysm
	 * and gave every dungeon one modifier, whatever tier the player was
	 * fighting at. Issue
	 * [#1444](https://github.com/sdubois777/Cataclysm/issues/1444).
	 *
	 * ZERO RATHER THAN A SECOND FUNCTION, because `Cataclysm.DifficultyTier`
	 * already uses zero to mean "nobody said, work it out" and a second
	 * convention for the same question would be one more thing to remember.
	 *
	 * A POSITIVE TIER IS USED AS GIVEN AND IS NOT CLAMPED, so a caller that
	 * wants a run outside the eight tiers still gets one and nothing that could
	 * reach `UCataclysmEmpireRun::Begin` before is refused now.
	 * `Cataclysm.EmpireBegin` clamps its own argument before it gets here.
	 *
	 * THE TIER IS READ ONCE, WHEN THE RUN STARTS, AND KEPT.
	 * `docs/Cataclysm_GDD_v2.md` section XII: "A run is played at a fixed tier,
	 * so a player does not move up the tiers inside a run; they finish a
	 * campaign and start the next one higher." Typing a new tier at the console
	 * part way through therefore changes what armour is worth and does not
	 * re-tier the campaign; the next `Cataclysm.EmpireBegin` picks it up.
	 *
	 * **THE TIER COMES FROM THE CHARACTER**, since issue
	 * [#1472](https://github.com/sdubois777/Cataclysm/issues/1472): a new
	 * character's run, and a loaded character's new run, are begun with the
	 * character's tier passed in. With no tier passed -- a console command, a
	 * test -- this falls back to `ACataclysmGameMode::DifficultyTierIn` as
	 * before. A won run's raise (`RaiseDifficultyTierForAWin`) is called by
	 * `BeginTheRunAfterAWin` below, since issue #1324 slice 6e; **this said
	 * nothing in play called it until #1324 made a run winnable.** Once begun,
	 * the level's game mode takes the run's tier (`TakeTheRunsTier`).
	 *
	 * IT ALSO FILLS THE RUN'S MODIFIER POOL, which is the one thing a run cannot
	 * do for itself: `DT_DungeonModifiers` has a row type in this module and
	 * `CataclysmEmpire` must not depend on it, so the caller that has both
	 * modules is the one that joins them. Issue #41.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	UCataclysmEmpireRun* BeginEmpireRun(
		int32 Seed = 0,
		ECataclysmSurgeMode Mode = ECataclysmSurgeMode::Static,
		int32 LethalityRung = 0,
		int32 DifficultyTier = 0);

	/**
	 * Throws away whatever run was in progress and starts a CHARACTER'S run: as `BeginEmpireRun`, except that the
	 * Cataclysms it faces are drawn from the character's Cataclysm order seed rather than from the run's `Seed`
	 * (`UCataclysmEmpireRun::BeginForCharacter`). Issue #1324 slice 6a, ruled 2026-11-02 from
	 * `docs/Cataclysm_GDD_v2.md`, Ending a Run: "The same tier means the same Cataclysms, by name and not only by count."
	 *
	 * THE TWO DOORS A CHARACTER'S RUN COMES THROUGH IN PLAY: a new character's (`ACataclysmGameMode::
	 * MakeThePendingCharacter`) and a loaded character's new run (`FCataclysmSaveLoad::CharacterAndRun`). Both pass
	 * `ACataclysmPlayerState::GetCataclysmOrderSeed` or the record's `CataclysmOrderSeed`. `BeginEmpireRun` is this with
	 * the run's own seed as the order seed, for a caller that names no character: a console command, a test.
	 *
	 * THE RUN'S FIRST SURGE FIRES HERE, ON DAY 0 (`UCataclysmEmpireRun::FireOpeningSurge`), so every run the game
	 * begins -- through either door above, `BeginEmpireRun`, `GetOrBeginEmpireRun` or `BeginTheRunAfterAWin` -- opens
	 * with its dungeons standing. A run restored from a save does not come through here and does not fire it again.
	 * Issue #2733.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	UCataclysmEmpireRun* BeginACharactersEmpireRun(
		int32 Seed,
		int32 CataclysmOrderSeed,
		ECataclysmSurgeMode Mode = ECataclysmSurgeMode::Static,
		int32 LethalityRung = 0,
		int32 DifficultyTier = 0);

	/**
	 * The character's half of a won run, and its next run. Issue #1324 slice 6e, ruled by the coordinating session on
	 * 2026-11-02 under the owner's delegation.
	 *
	 * CALLED BY `ACataclysmDungeonGameMode::ClearEmpireDungeon` when the clear it credited won the run this instance
	 * holds (`UCataclysmEmpireRun::bWon`), after the walk was left -- which ran none of its owed days, since a win
	 * drops them (R6). Does nothing, and answers null, when this instance holds no run or one that is not won.
	 *
	 * IN THIS ORDER, and the order is the point of it:
	 *
	 *   1. WHAT WAS WON is read off the won run before anything replaces it: its tier, the day it was won, its
	 *      surge mode and lethality.
	 *   2. THE CHARACTER'S DRAW. A character that never drew a Cataclysm order seed -- the fresh start's nameless one,
	 *      one a test stands up -- is given the seed its identifier gives (`UCataclysmCharacterSave::
	 *      CataclysmOrderSeedFor` of `Writer`'s character), which is the seed its record is written with anyway.
	 *   3. THE FIRST-KILL PASSIVE POINTS (R4): a win at tier T pays for the boss of the T-th Cataclysm in that draw,
	 *      `UCataclysmRoster::OrderFor(seed)[T-1]`, through `ACataclysmPlayerState::RecordCataclysmBossDefeat`, which
	 *      pays 10 the first time and nothing for a boss defeated before. Eight wins at tiers 1 to 8 pay 80.
	 *   4. THE TIER, ONE HIGHER, held at 8 (`RaiseDifficultyTierForAWin`). BEFORE ANY RECORD IS WRITTEN: the
	 *      coordinating session's comment of 2026-11-01 on #1324.
	 *   5. THIS RUN'S KILLS START AGAIN at nought, since the run they counted is over (a judgement of the session that
	 *      built it).
	 *   6. THE NEXT RUN, AT ONCE (R7): `BeginACharactersEmpireRun` with a clock seed, the character's order seed, the
	 *      won run's surge mode and lethality, and the new tier -- so it faces the won run's Cataclysms and one more
	 *      (`docs/Cataclysm_GDD_v2.md`, Ending a Run: "Only defeating the boss dungeon adds a Cataclysm to the next
	 *      run."). At tier 8 the tier holds, and the next run faces the same eight. It carries the win line
	 *      (`UCataclysmEmpireRun::NoteLastRunWon`).
	 *   7. WRITTEN ONCE, after all of it: `ECataclysmSaveTrigger::RunWon`, which writes the character record and the
	 *      run record. The run record goes to the slot the writer already writes, which the won run held.
	 *
	 * HERE AND NOT IN THE EMPIRE RUN, because the character, its draw, its tier and the save writer are this module's,
	 * and `CataclysmEmpire` must not depend on it. HERE AND NOT IN THE GAME MODE, because this is the one door into a
	 * run (`BeginACharactersEmpireRun`), and the run is the instance's to replace.
	 *
	 * @param Winner the player state of the character that won.
	 * @param Writer the world's save writer, or null: then nothing is written and a character with no seed is given
	 *               the zero identifier's.
	 * @return the run begun, or null when nothing was done.
	 */
	UCataclysmEmpireRun* BeginTheRunAfterAWin(ACataclysmPlayerState& Winner, UCataclysmSaveWriter* Writer);

	/**
	 * The run belonging to the game instance a world is part of, or null.
	 *
	 * WHAT IT IS FOR IS EVERY CALLER THAT IS NOT THE GAME INSTANCE. A screen and
	 * a console command both hold a world and neither should have to know how to
	 * walk from one to a game instance, nor what to do when the game instance is
	 * the engine's own rather than this one.
	 *
	 * @param bStartIfNone whether to start a run when there is none. False for a
	 *                     caller that is only looking, so that opening a screen
	 *                     does not silently begin a campaign.
	 */
	static UCataclysmEmpireRun* EmpireRunFor(const UObject* WorldContext,
											 bool bStartIfNone = false);

	// ----------------------------------------------------------------------
	// The empire upgrade point bank -- issue #52, the first slice
	// ----------------------------------------------------------------------

	/**
	 * The account record the bank is held in: the one partition's record last asked for, or null before any was.
	 *
	 * THE BANK BELONGS TO THE ACCOUNT, NOT THE RUN. `docs/Cataclysm_GDD_v2.md`, Empire-Wide Upgrades: the points
	 * "persist through all runs -- including failed ones", and "An empire upgrade point earned by a character is earned
	 * into that character's lethality mode" -- one balance per lethality mode and population, which is what an account
	 * record is (`UCataclysmSavePartition`). HELD HERE because a game instance outlives every level, as the run does.
	 * The save writer writes it on `ECataclysmSaveTrigger::AccountChanged`.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UCataclysmAccountSave> HeldAccount;

	/**
	 * The account slots `AccountFor` refused this session -- a record that would not read, or one naming another
	 * partition -- so each is read and logged once and then refused without either. The empire screen asks at every
	 * refresh of its status line. Issue #52, found reviewing the first slice, 2026-11-03.
	 */
	TSet<FString> RefusedAccountSlots;

	/**
	 * The slot a partition's account record is read from and written to: `UCataclysmSavePartition::AccountSlotName`,
	 * empty for a Solo Self-Found character.
	 *
	 * **WHILE AN AUTOMATION TEST RUNS IT ENDS IN `AutomationTestSlotSuffix`.** An account slot is named for its
	 * partition, not for a generated identifier as a character's and a run's are, so a test that clears a dungeon would
	 * otherwise read and rewrite the player's own bank in `game/Saved/SaveGames`. With the suffix the slot did not
	 * exist before the test, so `FCataclysmTestSaves` deletes it when the test ends. Issue #52, 2026-11-03.
	 */
	static FString AccountSlotFor(const FCataclysmCharacterPartition& Partition);

	/** What `AccountSlotFor` appends while an automation test runs. */
	static const TCHAR* AutomationTestSlotSuffix;

	/**
	 * The account record of a partition, held from then on: the held one when it is that partition's, otherwise read
	 * from its slot, and made fresh -- at the current version, holding nothing -- when the slot is empty.
	 *
	 * NULL FOR A SOLO SELF-FOUND CHARACTER, which has no account record: its points belong in its own character record
	 * (`UCataclysmCharacterSave::PrivateEmpireUpgradePoints`), which `BankEmpirePointsFor` banks into since issue #2741
	 * slice 1c. Issue #52.
	 *
	 * NULL WHEN THE SLOT HOLDS A RECORD THAT WOULD NOT READ, or one naming another partition, and that is logged: a
	 * fresh record banked into would be written over the file and empty the bank it held. Logged ONCE: the slot is
	 * remembered in `RefusedAccountSlots`, and every later ask this session is refused without reading it.
	 */
	UCataclysmAccountSave* AccountFor(const FCataclysmCharacterPartition& Partition);

	/**
	 * Adds points to a partition's bank, keeping the fraction. Answers the record banked into, or null when nothing
	 * was banked -- a Solo Self-Found character, or an account record that would not read (see `AccountFor`).
	 *
	 * IT DOES NOT WRITE THE RECORD. The caller raises `ECataclysmSaveTrigger::AccountChanged`, which the save writer
	 * answers by writing `HeldAccount` (`UCataclysmSaveWriter::NoteTrigger`).
	 *
	 * CALL `BankEmpirePointsFor` RATHER THAN THIS for the character playing: it decides between this and a Solo
	 * Self-Found character's own bank.
	 */
	UCataclysmAccountSave* BankEmpirePoints(const FCataclysmCharacterPartition& Partition, double Points);

	/**
	 * Adds points to the bank `EmpireTreeHeldBy` reads for the character playing, keeping the fraction: its lethality
	 * mode's, in the account record, or a Solo Self-Found character's own, on its player state. Issue #2741 slice 1c; the
	 * owner's reversal of C8 on 2026-11-18. `AddGoldFor` is the precedent for gold.
	 *
	 * IT DOES NOT WRITE ANY RECORD; the caller raises the trigger `OutHeld.bTheCharactersOwn` names.
	 *
	 * @param OutHeld  the tree and bank banked into, `Why` saying why when nothing was
	 * @return false, and nothing added, for points below nought or where `EmpireTreeHeldBy` has no bank; an award of
	 *         nought is banked, as `BankEmpirePoints` banks it
	 */
	static bool BankEmpirePointsFor(const UObject* WorldContext, double Points, FCataclysmHeldEmpireTree& OutHeld);

	/**
	 * Has the record that holds a tree written: `AccountChanged` for a lethality mode's, which the account record holds,
	 * and `OwnEmpireTreeChanged` for a Solo Self-Found character's own, which its character record holds. Issue #2741
	 * slice 1c. Every change to a tree or its bank -- a point bought, an option chosen, a respec, a defeat's award -- is
	 * saved through this, so none of them asks which record it is.
	 */
	static void NoteTheEmpireTreeChanged(const UObject* WorldContext, const FCataclysmHeldEmpireTree& Held);

	// ----------------------------------------------------------------------
	// Gold -- issue #2742, slice 1
	// ----------------------------------------------------------------------

	/**
	 * Adds gold to a partition's ACCOUNT balance. Answers the record banked into, or null when nothing was banked -- a
	 * Solo Self-Found partition, which has no account record, or an account record that would not read (see
	 * `AccountFor`) -- and an amount of zero or less is refused, since spending is not this. The design's Storage
	 * section: "Gold is held by the account, once per lethality mode".
	 *
	 * CALL `AddGoldFor` RATHER THAN THIS for the character playing: it decides between this and a Solo Self-Found
	 * character's own balance. IT DOES NOT WRITE THE RECORD, as `BankEmpirePoints` does not: `UCataclysmDropPickup::
	 * BankGold` raises the save triggers once for every pile it took.
	 */
	UCataclysmAccountSave* BankGold(const FCataclysmCharacterPartition& Partition, int64 Gold);

	/**
	 * THE ONE PLACE THAT SAYS WHICH BALANCE THE CHARACTER PLAYING HAS, and what it holds. Issue #2742.
	 *
	 * A SOLO SELF-FOUND CHARACTER'S OWN, on its player state and saved in its character record (version 8); EVERY
	 * OTHER CHARACTER'S LETHALITY MODE'S, in the account record (version 3). The design's Storage section, and the
	 * owner's ruling of 2026-11-18 that a Solo Self-Found character earns gold. The inventory screen, the empire map's
	 * status line, the PLAY LOOP line and slice 2's city screen all read it here.
	 *
	 * @return false, with `OutGold` 0, when there is no balance to read: no world, no character playing, or -- for a
	 *         character that is not Solo Self-Found -- no game instance of this project's or an account record that
	 *         would not read. A Solo Self-Found character's needs no game instance.
	 */
	static bool GoldHeldBy(const UObject* WorldContext, int64& OutGold);

	/**
	 * Adds gold to the balance `GoldHeldBy` reads, and says which record now holds the change. Issue #2742.
	 *
	 * @param bOutIntoTheCharacter  true when the gold went to a Solo Self-Found character's own balance, which the
	 *                              character record saves; false when it went to the account's
	 * @return false, and nothing added, for an amount of zero or less or where `GoldHeldBy` has no balance
	 */
	static bool AddGoldFor(const UObject* WorldContext, int64 Gold, bool& bOutIntoTheCharacter);

	/**
	 * Takes gold from the balance `GoldHeldBy` reads, and says which record now holds the change. Issue #2742's second
	 * slice: a city upgrade's price. `AddGoldFor`'s other half.
	 *
	 * ALL OR NOTHING. A balance holding less than `Gold` is left as it was and the answer is false, so no balance goes
	 * below nought and no purchase is half paid. IT DOES NOT WRITE THE RECORD: `UCataclysmCityUpgradePurchase::Buy`
	 * raises the save triggers.
	 *
	 * @param bOutFromTheCharacter  true when the gold came from a Solo Self-Found character's own balance, which the
	 *                              character record saves; false when it came from the account's
	 * @return false, and nothing taken, for an amount of zero or less, where `GoldHeldBy` has no balance, or where the
	 *         balance holds less than `Gold`
	 */
	static bool SpendGoldFor(const UObject* WorldContext, int64 Gold, bool& bOutFromTheCharacter);

	/**
	 * The balance as every screen shows it: "1,234 gold". Empty when `GoldHeldBy` has no balance to read, so a screen
	 * shows nothing rather than a 0 that is not true.
	 */
	static FString GoldBalanceText(const UObject* WorldContext);

	/**
	 * The gold picked up since the walk being played began, for the PLAY LOOP line that reports it at the walk's end
	 * (`FCataclysmPlayLoopLog::GoldLine`). Issue #2742.
	 *
	 * HELD HERE BECAUSE A GAME INSTANCE OUTLIVES THE LEVEL, as `HeldAccount` does. Set to zero when a walk is entered
	 * (`ACataclysmDungeonGameMode`, beside the "entered" PLAY LOOP line). NOT SAVED: a walk resumed from a save counts
	 * from the load, which the line says nothing to contradict -- it reports what this session picked up.
	 */
	int64 GoldPickedUpThisWalk = 0;

	// ----------------------------------------------------------------------
	// The empire upgrade tree -- issue #2741, slice 1b
	// ----------------------------------------------------------------------

	/**
	 * THE ONE PLACE THAT SAYS WHICH EMPIRE UPGRADE TREE THE CHARACTER PLAYING HAS, and the bank it is bought with.
	 * Issue #2741 slice 1b; `GoldHeldBy` is the precedent for gold.
	 *
	 * EVERY CHARACTER BUT A SOLO SELF-FOUND ONE: ITS LETHALITY MODE'S, the partition's account record's
	 * `EmpireUpgradePoints` and `EmpireTree` (version 4). `docs/Cataclysm_GDD_v2.md`, Empire-Wide Upgrades: a point
	 * earned by a character is earned into its lethality mode and shared by every character in it, so what the points
	 * buy is shared the same way. The screen and every node's effect read the tree here (slices 2 to 5).
	 *
	 * A SOLO SELF-FOUND CHARACTER'S OWN, on its player state and saved in its character record (version 9), with
	 * `OutHeld.bTheCharactersOwn` true. The owner ruled on 2026-11-18 that such a character banks and spends its own
	 * ("Include them"), reversing the coordinating session's C8; the design's Solo Self-Found rule, it "shares a tree
	 * with no other character at all". Slice 1c; until then this answered false for one. It needs no game instance.
	 *
	 * @return false, with `OutHeld.Why` saying why and both pointers null, when there is no tree to read: no world, no
	 *         character playing, or -- for a character that is not Solo Self-Found -- no game instance of this
	 *         project's or an account record that would not read (see `AccountFor`)
	 */
	static bool EmpireTreeHeldBy(const UObject* WorldContext, FCataclysmHeldEmpireTree& OutHeld);

	/**
	 * Why the empire upgrade tree cannot be changed now -- a point bought, an option chosen, a respec -- or empty when it
	 * can. Issue #2741 slice 2, ruled by the coordinating session on 2026-11-18: refused WHILE A WALK IS UNDER WAY, one
	 * rule for all three, because slices 3 to 5 read the tree where each number is read and a point bought mid-walk would
	 * change the walk under way. The tree's screen still opens during a walk, to be read; this is the sentence it shows.
	 */
	static FString EmpireTreeChangeRefusal(const UObject* WorldContext);

	/**
	 * Puts one point into a node of the tree `EmpireTreeHeldBy` reads, paid from its bank, and has the record written.
	 * Issue #2741 slice 1b.
	 *
	 * THE RULES ARE `UCataclysmEmpireTree::Spend`'s, and a refusal is its sentence (or `EmpireTreeHeldBy`'s): nothing
	 * changes and nothing is written. A point bought has the record that holds the tree written
	 * (`NoteTheEmpireTreeChanged`): the account record, or a Solo Self-Found character's own since slice 1c. REFUSED
	 * DURING A WALK (`EmpireTreeChangeRefusal`). Says a PLAY LOOP line (`FCataclysmPlayLoopLog::TreePointLine`).
	 */
	static bool BuyEmpireTreePoint(const UObject* WorldContext, FName Node, FString& OutReason);

	/**
	 * Chooses one of a centre capstone's three options in the tree `EmpireTreeHeldBy` reads, and has the record
	 * written. The rules and refusals are `UCataclysmEmpireTree::ChooseOption`'s, and refused during a walk
	 * (`EmpireTreeChangeRefusal`). Says a PLAY LOOP line (`FCataclysmPlayLoopLog::TreeOptionLine`). Issue #2741 slice 1b.
	 */
	static bool ChooseEmpireTreeOption(const UObject* WorldContext, FName Node, int32 Option, FString& OutReason);

	/**
	 * Returns every point spent in the tree `EmpireTreeHeldBy` reads to its bank and forgets every choice, at the
	 * price of days off the run being played. Issue #2741 slice 1b; the coordinating session's Q3 of 2026-11-18.
	 *
	 * THE DAYS ARE THE RUN'S DEATH COST, `UCataclysmEmpireTree::RespecDays` of the run's own lethality rung -- 5, 10 or
	 * 15 -- and they pass through `UCataclysmEmpireRun::AdvanceDays`, so a surge or a resolve that falls inside them
	 * happens on its own day, as for any other days passed.
	 *
	 * REFUSED, CHANGING NOTHING, WHILE A DUNGEON IS BEING WALKED (`EmpireTreeChangeRefusal`, the rule buying and
	 * choosing obey too since slice 2), when the tree holds nothing, and when there is no run to take the days from.
	 * Says a PLAY LOOP line (`FCataclysmPlayLoopLog::TreeRespecLine`).
	 *
	 * SAVED WHOLE: the record that holds the tree (`NoteTheEmpireTreeChanged`: `AccountChanged` for the account record,
	 * `OwnEmpireTreeChanged` for a Solo Self-Found character's own since slice 1c) and then `EmpireTreeRespecced` for the
	 * run record, in the same frame. With the account alone, a player who quit before the next run write would come back to a respec that cost
	 * no days; and `Clock`, which also writes the run record, is skipped when another write wrote it earlier in the
	 * frame. Ruled by the coordinating session on 2026-11-18.
	 *
	 * @param OutPointsReturned how many points went back to the bank
	 * @param OutDays           how many days the run passed
	 */
	static bool RespecEmpireTree(const UObject* WorldContext, int32& OutPointsReturned, int32& OutDays,
								 FString& OutReason);

	/**
	 * The partition of the character being played in a world: the first player character's player state's. False,
	 * and nothing written, when there is no world, no player character or no player state.
	 */
	static bool PlayingPartition(const UObject* WorldContext, FCataclysmCharacterPartition& OutPartition);

	/** The world's game instance, when it is this project's; null otherwise. */
	static UCataclysmGameInstance* In(const UObject* WorldContext);
};
