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
	 * (`BeginEmpireRun`). In the C++, only console commands reach it
	 * (`Cataclysm.EmpireMap`, `Cataclysm.EmpireAdvance`, `Cataclysm.EnterDungeon`); a
	 * character's run comes through `BeginACharactersEmpireRun`. Issue #1324
	 * slice 6a.
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
	 * (`UCataclysmCharacterSave::PrivateEmpireUpgradePoints`), and THIS SLICE DOES NOT BANK THEM. Issue #52.
	 *
	 * NULL WHEN THE SLOT HOLDS A RECORD THAT WOULD NOT READ, or one naming another partition, and that is logged: a
	 * fresh record banked into would be written over the file and empty the bank it held.
	 */
	UCataclysmAccountSave* AccountFor(const FCataclysmCharacterPartition& Partition);

	/**
	 * Adds points to a partition's bank, keeping the fraction. Answers the record banked into, or null when nothing
	 * was banked -- a Solo Self-Found character, or an account record that would not read (see `AccountFor`).
	 *
	 * IT DOES NOT WRITE THE RECORD. The caller raises `ECataclysmSaveTrigger::AccountChanged`, which the save writer
	 * answers by writing `HeldAccount` (`UCataclysmSaveWriter::NoteTrigger`).
	 */
	UCataclysmAccountSave* BankEmpirePoints(const FCataclysmCharacterPartition& Partition, double Points);

	/**
	 * The partition of the character being played in a world: the first player character's player state's. False,
	 * and nothing written, when there is no world, no player character or no player state.
	 */
	static bool PlayingPartition(const UObject* WorldContext, FCataclysmCharacterPartition& OutPartition);

	/** The world's game instance, when it is this project's; null otherwise. */
	static UCataclysmGameInstance* In(const UObject* WorldContext);
};
