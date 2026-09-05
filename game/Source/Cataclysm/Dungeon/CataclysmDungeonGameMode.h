// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/CataclysmGameMode.h"
#include "Dungeon/CataclysmFloorPlan.h"
#include "Dungeon/CataclysmFloorPopulation.h"
#include "Templates/SubclassOf.h"
#include "CataclysmDungeonGameMode.generated.h"

class ACataclysmDungeonFloor;
class ACataclysmDungeonStairs;
class ACataclysmEnemyCharacter;

/**
 * Puts the player on a generated dungeon floor when play begins.
 *
 * WHAT IT IS FOR. `FCataclysmFloorGenerator` decides which cells are walkable,
 * `ACataclysmDungeonFloor` turns that into blocks, and until this existed nothing
 * asked either of them to. The whole thing was reachable only from automation
 * tests. This is what makes pressing Play put a person on a dungeon floor.
 *
 * IT DERIVES FROM THE SANDBOX'S GAME MODE and turns one thing off. What it keeps
 * is worth keeping: `ACataclysmGameMode` is what answers the difficulty tier that
 * every armour calculation reads, and what starts the save writer. What it turns
 * off is `bSpawnsSandboxCreatures`, because every sandbox spawner places its
 * creatures at a fixed offset from the world origin -- which is where the
 * sandbox's flat floor is, and is as likely to be inside solid rock on a
 * generated floor as on the ground.
 *
 * IT PUTS CREATURES ON THE FLOOR, and does it from the floor plan rather than
 * from an offset. `FCataclysmFloorPopulator` decides which creature stands on
 * which cell; `PopulateFloor` below turns that list into characters. The
 * decision and the spawning are separate for the reason the floor plan itself is
 * separate from the geometry: the automation tests run with `-nullrhi`, so a
 * list of cells can be swept over a thousand seeds and sixty spawned characters
 * cannot.
 *
 * AND THE STAIRS WORK. `ACataclysmDungeonStairs` stands at the floor's exit and
 * says when the player has reached it; `GoDownOneFloor` below builds the next
 * floor, puts its creatures on it, moves the marker to its exit and stands the
 * player at its entrance. `FloorNumber` is no longer only a setting.
 *
 * A DUNGEON FROM THE EMPIRE MAP HAS A BOTTOM, and reaching it beats the dungeon:
 * `IsOnTheLastFloor` compares the floor being walked against the floor count the
 * empire dungeon carries, and `ClearEmpireDungeon` takes it off the map and off
 * the day clock, so its host city stops being bitten by it. Issue #1092.
 *
 * A DUNGEON THAT IS NOT BOUND TO ONE STILL DESCENDS FOR EVER. That is what
 * pressing Play gives you, and `IsOnTheLastFloor` says so plainly: no floor
 * count means no bottom. It is the sandbox's behaviour rather than an oversight.
 *
 * WHAT IT DOES NOT DO YET. There is no boss on the last floor -- that is issue
 * #41's side of the join -- and beating a dungeon moves the player nowhere,
 * because there is nowhere to go: the capital hub is issue #48. A player who
 * reaches the bottom is left standing on the floor they beat.
 */
UCLASS(Config = Game)
class CATACLYSM_API ACataclysmDungeonGameMode : public ACataclysmGameMode
{
	GENERATED_BODY()

public:
	ACataclysmDungeonGameMode();

	virtual void StartPlay() override;

	// ----------------------------------------------------------------------
	// Which floor
	// ----------------------------------------------------------------------

	/**
	 * The dungeon's seed. Every floor of one dungeon shares it.
	 *
	 * A SETTING RATHER THAN SOMETHING CHOSEN, because there is no dungeon object
	 * to take it from -- that is issue #41.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Dungeon")
	int32 DungeonSeed = 1;

	/** Which floor of it, counted from 1, the way `FCataclysmSavedFloor` counts. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Dungeon", meta = (ClampMin = "1"))
	int32 FloorNumber = 1;

	/**
	 * How many floors the whole dungeon has.
	 *
	 * A STAND-IN, THE SAME WAY `Cataclysm.PlayerLevel` WAS ONE BEFORE LEVELLING
	 * EXISTED. There is no dungeon object to take a length from -- that is issue
	 * #41 -- and until there is, this is a setting and a console variable. It
	 * does NOT stop the stairs: `GoDownOneFloor` still descends past it, because
	 * a bottom to the dungeon is part of #41 rather than of this number.
	 *
	 * WHAT IT IS FOR TODAY, AND IT IS ONE THING: Enemy Score. That model's
	 * baseline is driven by `FloorNumber / TotalFloors`, so without a total
	 * there is no floor ratio and a creature has no score at all -- and since
	 * 2026-08-24 a creature's score IS the experience it grants. Issue #926.
	 *
	 * TEN, WHICH IS INSIDE THE DESIGN'S SMALLEST BASIC DUNGEON of 8 to 15
	 * floors, and chosen over the 50-floor average for a reason worth keeping: at
	 * difficulty tier 1 the depth term is large and negative near an entrance, so
	 * in a 50-floor dungeon the first three floors score a Common enemy below
	 * zero and pay no experience at all. In a 10-floor dungeon every floor pays
	 * something, so somebody pressing Play and killing the first creature they
	 * see is not told that it was worth nothing.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Dungeon", meta = (ClampMin = "1"))
	int32 TotalFloors = 10;

	/**
	 * Which kind of dungeon this is, and what it does differently.
	 *
	 * READ ONLY BY ENEMY SCORE SO FAR. Neither changes how a floor is built or
	 * what stands on it; a Horde dungeon being one big arena is issue #41's
	 * side of the join. Both are here because the score model takes them and
	 * Basic with no sub-type is the only combination that adds nothing.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Dungeon")
	ECataclysmDungeonType DungeonType = ECataclysmDungeonType::Basic;

	/** See `DungeonType`. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Dungeon")
	ECataclysmDungeonSubType DungeonSubType = ECataclysmDungeonSubType::None;

	/** Which layout family carves it. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Dungeon")
	ECataclysmFloorLayout Layout = ECataclysmFloorLayout::Halls;

	/**
	 * How many creatures the floor holds, as a multiple of the designed density.
	 *
	 * ONE MEANS `FCataclysmFloorPopulator::EnemiesPerWalkableCell`, which since
	 * 2026-08-21 puts between 73 and 510 creatures on a floor depending on its
	 * size and layout -- three times what it used to, because the project owner
	 * played a floor and said the density was way too low. Issue #809. Zero
	 * empties the floor, which is what walking one to look at its shape wants,
	 * and 0.33 walks the old density.
	 *
	 * THE NUMBER OF CREATURES IS NOT A SETTING AND SHOULD NOT BECOME ONE. Floor
	 * size is rolled per floor, so a count would make a small floor crowded and a
	 * large one empty. What is settable is how dense, and this is that.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Dungeon", meta = (ClampMin = "0"))
	float EnemyScale = 1.0f;

	// ----------------------------------------------------------------------
	// Walking a dungeon that stands on the empire map, issue #1092
	// ----------------------------------------------------------------------
	//
	// WHAT THIS JOINS. Several settings above are stand-ins for something the
	// empire layer now holds. This header said so of `TotalFloors` -- "There is
	// no dungeon object to take a length from -- that is issue #41" -- and there
	// is one: `FCataclysmDungeon` in the `CataclysmEmpire` module carries a
	// dungeon's depth, its kind, which city it is assaulting and what it takes
	// when it resolves.
	//
	// AND THE DAY MOVES. One dungeon floor costs exactly one day, which
	// `CLAUDE.md` lists among the rules that are easy to get wrong: depth and
	// time are the same axis, so a dungeon cannot be made cheaper without also
	// being made poorer. Walking down a floor is what spends that day, and until
	// this nothing in the game spent one.
	//
	// THE SETTINGS STILL WORK. A run bound to a dungeon overrides them; no
	// binding means the settings, exactly as before. Pressing Play in `L_Dungeon`
	// with no empire run has to keep putting somebody on a floor.

	/**
	 * Which dungeon of the empire is being walked, or `INDEX_NONE`.
	 *
	 * `INDEX_NONE` IS THE ORDINARY CASE TODAY, because nothing takes a player
	 * from the empire map into a dungeon -- that needs somewhere to stand
	 * between runs, which is issue #48. `Cataclysm.EnterDungeon` binds one by
	 * hand.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	int32 EmpireDungeonId = INDEX_NONE;

	/**
	 * Start walking a dungeon that stands on the empire map.
	 *
	 * WHAT IT DOES, AND THE THIRD IS THE ONE THAT COSTS SOMETHING:
	 *
	 *   1. Takes the dungeon's depth, seed and kind, so the floor built is one
	 *      that dungeon actually has rather than the setting's.
	 *   2. Tells the day clock the player is inside it, which STOPS THAT ONE
	 *      DUNGEON'S TIMER AND NO OTHER'S. Its residents are busy fighting the
	 *      player rather than marching on the city, so entering is a guaranteed
	 *      save rather than a gamble -- and what it costs is every other timer
	 *      advancing while you are down there.
	 *   3. Spends a day, because the player is now standing on floor 1 and a
	 *      floor costs a day. Walking N floors costs N days: one for arriving
	 *      and one for each descent.
	 *
	 * @return whether a dungeon of that number is standing on the map
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	bool EnterEmpireDungeon(int32 DungeonId);

	/**
	 * Stop walking it without clearing it.
	 *
	 * ITS TIMER STARTS AGAIN and the dungeon stays on the map. That is what
	 * leaving unfinished means, and there is nowhere to leave TO yet -- the
	 * capital hub is issue #48 -- so nothing calls this in play.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	void LeaveEmpireDungeon();

	/**
	 * The dungeon is beaten: it comes off the map and off the clock.
	 *
	 * ITS HOST CITY STOPS BEING BITTEN BY IT, which is the whole reward for
	 * walking it. Anything else standing on that city keeps its own timer.
	 *
	 * @return whether there was a dungeon to clear
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	bool ClearEmpireDungeon();

	/**
	 * How deep the dungeon being walked is, or 0 when none is bound.
	 *
	 * SEPARATE FROM `ChooseTotalFloors` BECAUSE THAT ONE CANNOT SAY "NONE". It
	 * answers the deeper of the setting and the floor being walked, so it is
	 * never zero and cannot be asked whether a bottom exists at all.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	int32 EmpireDungeonFloors() const;

	/**
	 * Whether the floor being walked is the last one.
	 *
	 * ALWAYS FALSE WITH NO DUNGEON BOUND, which is what keeps the stairs
	 * descending for ever in the sandbox. A dungeon from the empire map has a
	 * bottom; a floor built from the settings does not.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	bool IsOnTheLastFloor() const;

	/**
	 * The run to use when there is no game instance to ask.
	 *
	 * A HEADLESS TEST HAS NO GAME INSTANCE OF THIS PROJECT'S CLASS. A world built
	 * by `UWorld::CreateWorld` has none at all, so without this seam every test
	 * of the join above would be a test of the case where there is no empire --
	 * which is the one case that already worked.
	 * `UCataclysmEmpireMapWidget::SetRunForTests` is the same seam for the same
	 * reason.
	 */
	void SetEmpireRunForTests(class UCataclysmEmpireRun* Run);

	// ----------------------------------------------------------------------
	// Looking at another floor without rebuilding
	// ----------------------------------------------------------------------
	//
	// THE FOUR SETTINGS ABOVE CANNOT BE CHANGED WITHOUT A REBUILD. They are
	// `EditDefaultsOnly` on a C++ class, and `L_Dungeon`'s world settings point
	// at that class directly rather than at a Blueprint, so there is nothing in
	// the editor to edit them on. Four console variables answer each of them
	// instead, and each is read once when the floor is built:
	//
	//     Cataclysm.DungeonSeed        0 uses the setting, above 0 is that
	//                                  dungeon, -1 rolls a new one every time
	//                                  play begins
	//     Cataclysm.DungeonFloor       0 uses the setting, above 0 is that floor
	//     Cataclysm.DungeonLayout      -1 uses the setting, 0 Halls, 1 Caverns,
	//                                  2 Arena
	//     Cataclysm.DungeonEnemyScale  below 0 uses the setting, 0 empties the
	//                                  floor, 1 is the designed density, 2 is
	//                                  twice as many
	//
	// ROLLING A SEED DOES NOT MAKE GENERATION RANDOM, and the difference
	// matters. The generator is deterministic and has to stay so: a dungeon must
	// look the same when the player leaves and returns, and a bug in a floor has
	// to be reproducible from its seed. What -1 changes is which seed is handed
	// to it, which is the empire layer's job once that exists.

	/**
	 * The dungeon seed that will actually be used, console variable included.
	 *
	 * PUBLIC AND TAKING ITS OWN ENTROPY, so a test can ask what it will choose
	 * without depending on the clock. A test that rolled a seed twice and
	 * expected two different numbers would be a test that usually passes.
	 *
	 * @param Entropy what to roll from when the console variable asks for a new
	 *                seed. Zero means read the clock, which is what play does.
	 * @return a seed above zero
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	int32 ChooseSeed(int64 Entropy = 0) const;

	/** The floor number that will actually be used, console variable included. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	int32 ChooseFloorNumber() const;

	/**
	 * The dungeon's length that will actually be used, console variable included.
	 *
	 * NEVER BELOW THE FLOOR BEING WALKED. The stairs descend for ever -- there
	 * is no bottom until issue #41 -- so a player can walk to floor 40 of a
	 * dungeon set to 10. Reporting a total below the current floor would give
	 * Enemy Score a floor ratio above one, which is outside anything the model
	 * was fitted for and would make every creature down there worth more than a
	 * Cataclysm Boss. Answering with the deeper of the two treats a player who
	 * has walked past the end as being on the last floor.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	int32 ChooseTotalFloors() const;

	/** The layout family that will actually be used, console variable included. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	ECataclysmFloorLayout ChooseLayout() const;

	/**
	 * How dense the floor's creatures will be, console variable included.
	 *
	 * BELOW ZERO AT THE CONSOLE MEANS "USE THE SETTING", not zero, because zero
	 * is a real answer here: it is a floor with nothing on it, which is what
	 * walking one to look at its shape wants. The layout control next door takes
	 * -1 for the same reason.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	float ChooseEnemyScale() const;

	/**
	 * What the save record calls this dungeon.
	 *
	 * A PLACEHOLDER AND SAID TO BE ONE. A dungeon's real name comes from the
	 * empire layer, which does not exist. What matters today is that the record
	 * stops saying "Sandbox" while the player is standing in a dungeon.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Dungeon")
	FName DungeonName = FName(TEXT("Dungeon"));

	// ----------------------------------------------------------------------
	// Building it
	// ----------------------------------------------------------------------

	/**
	 * Generates the floor and builds its geometry, replacing any already there.
	 *
	 * PUBLIC SO A TEST CAN CALL IT, which is the same reason the sandbox's
	 * spawners are public and says the same thing: `StartPlay` wants a player
	 * controller and a pawn, so an automation test cannot call it, and a step
	 * left only inside it is a step nothing covers.
	 *
	 * @return the floor, or null if it could not be built
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	ACataclysmDungeonFloor* BuildFloor();

	/**
	 * Stands a pawn on the floor where the player arrives.
	 *
	 * WHY IT IS NOT A PLAIN `SetActorLocation`. A character is a capsule whose
	 * origin is its middle, so putting that origin on the walking surface leaves
	 * the lower half inside the ground. The pawn is raised by its own half
	 * height, read from the pawn rather than assumed, because the three designed
	 * classes are not all the same size.
	 *
	 * @return whether the pawn was moved
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	bool PlaceAtEntrance(APawn* Pawn);

	/** The floor being stood on, once `BuildFloor` has run. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	TObjectPtr<ACataclysmDungeonFloor> CurrentFloor;

	// ----------------------------------------------------------------------
	// Putting creatures on it
	// ----------------------------------------------------------------------

	/**
	 * Puts creatures on the floor that has been built, removing any left from
	 * the floor before.
	 *
	 * SEPARATE FROM `BuildFloor` RATHER THAN PART OF IT, so that a test which
	 * only wants to check the geometry does not pay for sixty spawned characters,
	 * and so that walking an empty floor is one call rather than a setting. The
	 * sandbox's creature spawners are split from its `StartPlay` for the same
	 * reason and say so.
	 *
	 * REMOVING THE OLD ONES IS NOT TIDINESS. Going down the stairs replaces the
	 * floor in the same actor, and creatures from the floor before would be left
	 * standing in mid-air, or inside the new floor's rock, still hunting the
	 * player.
	 *
	 * @return how many were spawned
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	int32 PopulateFloor();

	/** Removes every creature this game mode put on the floor. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	void ClearFloorEnemies();

	/** Every creature standing on the current floor, in the order placed. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	TArray<TObjectPtr<ACataclysmEnemyCharacter>> FloorEnemies;

	/**
	 * Which character class stands in for one of the designed creatures.
	 *
	 * THE ONE PLACE THE TWO HALVES MEET. `FCataclysmFloorPopulator` names
	 * creatures with a plain enum so that it stays free of actor classes and can
	 * be swept in a headless test; this turns a name into something spawnable.
	 *
	 * STATIC AND PUBLIC so a test can check that every creature the populator can
	 * name has a class, which is the failure this would otherwise have: a new
	 * creature added to the enum, forgotten here, and silently never spawned.
	 *
	 * @return null for a creature with no class, which nothing should produce
	 */
	static TSubclassOf<ACataclysmEnemyCharacter> ClassFor(ECataclysmDungeonCreature Creature);

	// ----------------------------------------------------------------------
	// The stairs down
	// ----------------------------------------------------------------------

	/**
	 * Puts the marker for the way down at the current floor's exit, and starts
	 * it watching for the player.
	 *
	 * ONE ACTOR FOR THE WHOLE DUNGEON, MOVED RATHER THAN REPLACED. Spawning a
	 * second and destroying the first on every floor is one more thing to destroy
	 * at the wrong moment, and the marker carries the binding that makes the
	 * stairs work.
	 *
	 * @return the marker, or null if it could not be placed
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	ACataclysmDungeonStairs* PlaceStairs();

	/** The way down, once `PlaceStairs` has run. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	TObjectPtr<ACataclysmDungeonStairs> Stairs;

	/**
	 * Builds a floor, puts its creatures on it, moves the stairs to its exit and
	 * stands the player at its entrance.
	 *
	 * THE WHOLE FLOOR CHANGE IN ONE CALL, so that taking the stairs and beginning
	 * play do the same four things in the same order rather than two lists that
	 * can drift apart. Every step it calls is public and separately tested.
	 *
	 * IT ALSO TELLS THE SAVE WRITER. `UCataclysmSaveWriter::SetFloor` has existed
	 * since the save system was built and nothing ever called it, because nothing
	 * changed floors. It notes an `ECataclysmSaveTrigger::ChangedFloor`, so a
	 * floor change is now one of the moments the game saves itself.
	 *
	 * @param NewFloorNumber which floor, counted from 1. Below 1 is clamped.
	 * @param PawnToMove    who to stand at the new floor's entrance. Null means
	 *                      the pawn the first player controller is driving, which
	 *                      is what play passes.
	 *
	 *                      IT IS A PARAMETER SO A TEST CAN REACH THE STEP. An
	 *                      automation test world has no player controller, so a
	 *                      floor change that could only find the player through
	 *                      `GetFirstPlayerController` would leave the most
	 *                      player-visible thing about the stairs untested: a
	 *                      player who is not moved is left standing where the old
	 *                      floor's exit was, which on the new floor is as likely
	 *                      to be solid rock, or nothing at all, as ground.
	 *
	 * @return whether the floor was built
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	bool GoToFloor(int32 NewFloorNumber, APawn* PawnToMove = nullptr);

	/** The floor below the one being walked. See `GoToFloor`. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	bool GoDownOneFloor(APawn* PawnToMove = nullptr);

	/**
	 * How many floors down the player has gone since play began.
	 *
	 * KEPT SO A TEST CAN TELL A FLOOR CHANGE FROM A REBUILD. Building floor 2 by
	 * hand and walking down to floor 2 leave the world in the same state, and
	 * only one of them is the stairs working.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	int32 FloorsDescended = 0;

	/** What the stairs call when the player reaches them. */
	UFUNCTION()
	void HandleStairsTaken();

	/** The dungeon's name, so the save record does not say "Sandbox". */
	virtual FName RunFloorName() const override { return DungeonName; }

	/** Which floor of it. See `RunFloorName`. */
	virtual int32 RunFloorNumber() const override { return FloorNumber; }

	/**
	 * How long the dungeon is, and what kind it is. Read by Enemy Score.
	 *
	 * `ChooseTotalFloors` RATHER THAN `TotalFloors`, unlike `RunFloorNumber`
	 * above, and the asymmetry is deliberate. `RunFloorNumber` can return the
	 * raw setting because `GoToFloor` writes the walked floor back into it, so
	 * the setting is always current. Nothing writes the LENGTH back, and the
	 * stairs descend past it, so the raw setting can fall below the floor being
	 * walked. The two are read together to make a floor ratio, and a ratio above
	 * one is outside anything the Enemy Score model was fitted for.
	 */
	virtual int32 RunTotalFloors() const override { return ChooseTotalFloors(); }

	virtual ECataclysmDungeonType RunDungeonType() const override
	{
		return DungeonType;
	}

	virtual ECataclysmDungeonSubType RunDungeonSubType() const override
	{
		return DungeonSubType;
	}

protected:

	/**
	 * Gives one spawned creature the health, armour and attack damage its design
	 * calls for, and rolls its rarity.
	 *
	 * THE SAME FIGURES THE SANDBOX USES, read from the same settings on
	 * `ACataclysmGameMode`, so a Brute in a dungeon and a Brute in the sandbox
	 * are the same creature. A second set of numbers here would be a second
	 * place for them to drift from the design model.
	 *
	 * THE IMP IS GIVEN NO ARMOUR ON PURPOSE. Its designed armour share is exactly
	 * zero and it is the only creature in the roster with none, so calling
	 * `SetArmour(0)` would look like a figure somebody chose. The sandbox's own
	 * Imp spawner says the same thing.
	 */
	void ApplyDesignedStats(ACataclysmEnemyCharacter* Enemy,
							ECataclysmDungeonCreature Creature) const;

private:
	// ----------------------------------------------------------------------
	// Reaching the empire from a dungeon, issue #1092
	// ----------------------------------------------------------------------

	/**
	 * The run in progress, or null.
	 *
	 * IT NEVER STARTS ONE. A player pressing Play in `L_Dungeon` to look at a
	 * floor is not beginning a campaign, and a run started here would be one
	 * nothing else in the game knows about.
	 */
	class UCataclysmEmpireRun* EmpireRun() const;

	/** The dungeon `EmpireDungeonId` names, or null when none is bound. */
	const struct FCataclysmDungeon* BoundDungeon() const;

	/**
	 * One floor's worth of empire time passes, if there is an empire.
	 *
	 * ONE PLACE, so that what a floor costs is written down once. Nothing else
	 * in the game moves the day except the console commands, and
	 * `tools/tests/test_game_readme_is_true.py` is what holds that claim to
	 * whatever `game/README.md` says.
	 *
	 * NOT ALWAYS A WHOLE DAY. A floor costs one day by default, and a city
	 * upgrade can lower the rate for the dungeons that city receives WITHOUT
	 * lowering their floor count -- a fifty floor dungeon may cost two days, so
	 * one of its floors costs a twenty-fifth of a day.
	 * `FCataclysmDungeon::WalkDaysPerFloor` is the rate and
	 * `UCataclysmDayClock::SpendDays` is what turns fractions into whole days.
	 *
	 * A WHOLE DAY WHEN NO DUNGEON IS BOUND, which is what pressing Play gives
	 * you: a sandbox descent has no empire dungeon to take a rate from.
	 */
	void SpendFloorTimeInTheEmpire();

	/** See `SetEmpireRunForTests`. Null in a running game, always. */
	UPROPERTY(Transient)
	TObjectPtr<class UCataclysmEmpireRun> EmpireRunForTests;
};
