// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/GameInstance.h"
#include "CataclysmGameInstance.generated.h"

class UCataclysmEmpireRun;

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
 *   - **It does not load or save a run.** `UCataclysmRunSave` carries an
 *     `int32 Day` that nothing computes, and the empire graph is in no save
 *     record at all.
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
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	UCataclysmEmpireRun* GetOrBeginEmpireRun();

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
	 * **WHERE THE TIER OUGHT TO COME FROM EVENTUALLY IS THE CHARACTER**, and
	 * not the world. The design has a character at a tier that rises when it
	 * defeats the Cataclysm boss dungeon and replays the same tier when it
	 * fails, and neither the character's tier nor a won state exists in the
	 * game yet. `ACataclysmGameMode::DifficultyTier` says the same of the
	 * world-wide figure it holds: it "does not block the encounter-level answer
	 * and should be replaced by it". What this function does is join the tier
	 * the game HAS to the run; it is not the progression rule. Issue
	 * [#1472](https://github.com/sdubois777/Cataclysm/issues/1472).
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
};
