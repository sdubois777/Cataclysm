// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/CataclysmGameMode.h"
#include "Dungeon/CataclysmFloorBrief.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Dungeon/CataclysmFloorObstacle.h"
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
 * AND THE DUNGEON DECIDES WHAT ITS FLOORS HOLD. `DungeonIdentity` gathers what
 * the dungeon is and `FCataclysmDungeonFloorRules::BriefFor` turns it into what
 * one floor of it is: which layout carves it, which modifiers are in force on
 * it, whether a boss stands at its exit and whether its creatures are one wave.
 * A Gatekeeper now stands on the last floor of every dungeon, and on every
 * floor of an Elite one. Issue #41.
 *
 * WHAT IT DOES NOT DO YET. Beating a dungeon moves the player nowhere, because
 * there is nowhere to go: the capital hub is issue #48. A player who reaches
 * the bottom is left standing on the floor they beat.
 */
UCLASS(Config = Game)
class CATACLYSM_API ACataclysmDungeonGameMode : public ACataclysmGameMode
{
	GENERATED_BODY()

public:
	ACataclysmDungeonGameMode();

	virtual void StartPlay() override;

	/**
	 * Looks at the wave every `SecondsBetweenWaveChecks` and brings the next one
	 * in when it is down to a tenth.
	 *
	 * **THIS IS THE ONLY THING IN THE PROJECT THAT PUTS CREATURES ON A FLOOR
	 * AFTER IT HAS BEEN BUILT.** Everything else about a floor is decided when
	 * it is generated; a wave arriving is a thing that happens while the player
	 * is standing there, so it needs a clock and this game mode had none. That
	 * is why issue #1467 records the arrival as a new runtime system rather than
	 * a rule in floor generation.
	 *
	 * ON AN ORDINARY DUNGEON IT DOES ALMOST NOTHING. Nothing is ever waiting to
	 * arrive there, and `ShouldTheNextWaveArrive` answers false immediately for
	 * a floor that is not a wave.
	 *
	 * AND EVERY FRAME IT PUTS DOWN MORE OF A WAVE THAT IS STILL ARRIVING. Issue
	 * #1544: a wave is put on the floor `WaveCreaturesPerFrame` creatures a
	 * frame rather than all at once. That part does not wait for
	 * `SecondsBetweenWaveChecks`.
	 */
	virtual void Tick(float DeltaSeconds) override;

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
	 * THE KIND IS READ ONLY BY ENEMY SCORE. It changes what a creature is worth
	 * and nothing about how a floor is built.
	 *
	 * THE SUB-TYPE CHANGES THE FLOOR SINCE 2026-09-07, which this comment used
	 * to say it did not. `DungeonIdentity` hands it to
	 * `FCataclysmDungeonFloorRules`, which decides the layout, the modifiers,
	 * whether a boss stands at the exit and whether the creatures are one wave.
	 * Three sub-types use it: Horde, Elite and Volatile. Issue #41.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Dungeon")
	ECataclysmDungeonType DungeonType = ECataclysmDungeonType::Basic;

	/** See `DungeonType`. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Dungeon")
	ECataclysmDungeonSubType DungeonSubType = ECataclysmDungeonSubType::None;

	/**
	 * What this dungeon's modifiers add to every creature's enemy score.
	 *
	 * THE SUM OF THEIR DANGER SCORES AND NOT THE MODIFIERS THEMSELVES. What the
	 * dungeon carries is `FCataclysmDungeon::Modifiers`, over in the empire
	 * layer; this is the one number the score model takes.
	 * `docs/Cataclysm_GDD_v2.md` section VIII is where the sum is defined.
	 *
	 * SET FROM THE EMPIRE DUNGEON BY `EnterEmpireDungeon`, and editable here for
	 * the same reason `DungeonSubType` is: pressing Play in `L_Dungeon` builds a
	 * floor with no empire behind it, and a floor should be able to be told what
	 * it is standing in. Zero is exactly "no modifiers", because the score model
	 * adds it as a flat term.
	 *
	 * **THE DUNGEON'S, AND NOT THE FLOOR'S.** `RunModifierScore` answers with
	 * `FloorBrief.ModifierScore` instead, because a Volatile dungeon's modifiers
	 * change every floor and this would then be a different number on every one
	 * of them. This stays what the dungeon drew, so the per-floor rules always
	 * start from the same place rather than from whatever the last floor left
	 * behind. Issue #41.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Dungeon")
	float DungeonModifierScore = 0.0f;

	/**
	 * The dungeon's own modifiers, as row keys of the modifier table.
	 *
	 * CARRIED SO A PER-FLOOR RULE CAN READ THEM. One of the 117 modifiers,
	 * Unstable Dimensions, gives every floor an extra modifier of its own, and
	 * whether it fires depends on which modifiers the dungeon drew. The score
	 * alone cannot answer that.
	 *
	 * SET BY `EnterEmpireDungeon` FROM `FCataclysmDungeon::Modifiers`. Empty is
	 * a real answer: a floor walked without an empire behind it has none.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	TArray<FName> DungeonModifiers;

	/**
	 * Every modifier a floor of this dungeon may draw for itself.
	 *
	 * ALREADY NARROWED TO THE CATACLYSMS THE RUN IS FACING, by
	 * `UCataclysmDungeonModifierRules::PoolFor`, so the rules do not have to
	 * know what a Cataclysm is. `EnterEmpireDungeon` narrows it once when the
	 * player walks in rather than on every floor.
	 *
	 * EMPTY MEANS NOTHING RE-DRAWS, and every floor carries the dungeon's own
	 * modifiers. That is what a headless test gets, because filling the run's
	 * pool needs the modifier DataTable.
	 */
	TArray<FCataclysmDungeonModifier> DungeonModifierPool;

	/**
	 * Every row of the whole modifier table that does something in play, whatever Cataclysm: what Reality Twister
	 * draws from. Filled where `DungeonModifierPool` is, from the run's whole table, and emptied with it. Issues
	 * #1820 and #41.
	 */
	TArray<FCataclysmDungeonModifier> DungeonEveryBuiltModifier;

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
	// AND THE DAY MOVES. One dungeon floor costs one day as a STARTING RATE, and
	// walking down a floor is what spends it. Until this nothing in the game
	// spent any.
	//
	// A STARTING RATE AND NOT AN INVARIANT. City upgrades and the empire tree
	// lower the days a dungeon takes to walk while its floor count stays where
	// it is, so a floor of an invested player's fifty floor dungeon can cost a
	// fraction of a day. `FCataclysmDungeon::WalkDaysPerFloor` is the rate this
	// actually charges, and `SpendFloorTimeInTheEmpire` below spends it.
	//
	// DEPTH AND REWARD ARE THE SAME AXIS. DEPTH AND TIME ARE NOT, once a player
	// has invested in separating them. A dungeon made shallower is made poorer;
	// a dungeon made quicker to walk is not. `CLAUDE.md` lists this among the
	// rules that are easy to get wrong, and it was got wrong: an earlier version
	// of this comment said depth and time were one axis, which is what
	// `docs/DECISIONS.md` corrects under 2026-09-05.
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
	 *   3. Spends a floor's worth of time, because the player is now standing
	 *      on floor 1. A floor costs a day to begin with, so walking N floors
	 *      costs N days at that starting rate -- one for arriving and one for
	 *      each descent -- and less once a city upgrade has lowered the
	 *      dungeon's `WalkDaysPerFloor`.
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

	/**
	 * The seed handed to everything that fixes the floor being built: the floor request, the brief's draw of the
	 * floor's rows, and Scarcity's slot. Issues #1820 and #41, ruled 2026-10-09.
	 *
	 * `ChooseSeed()` UNCHANGED, EXCEPT ON THE DARK FLOOR, where it is that seed mixed with
	 * `FCataclysmDungeonFloorRules::DarkFloorSalt`. The dark floor carries the number of the floor its stairs lead
	 * to, so without the salt it would be a copy of that floor.
	 */
	int32 ChooseSeedForThisFloor() const;

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
	 * The sub-type that will actually be used, console variable included.
	 *
	 * WHAT MAKES HORDE, ELITE AND VOLATILE REACHABLE. The `DungeonSubType`
	 * setting is `EditDefaultsOnly` and `L_Dungeon` uses this class with no
	 * Blueprint subclass, so before `Cataclysm.DungeonSubType` existed there
	 * was no way to ask for one of them in play at all. Issue #1502.
	 *
	 * A NAME AT THE CONSOLE, NOT A NUMBER, unlike the layout control above.
	 * `Cataclysm.DungeonSubType Horde` is what the console takes, in any
	 * letter case and with or without the space in "Cow Level". A name that is
	 * not a sub-type leaves the setting deciding and says so in the log.
	 *
	 * THE CONSOLE WINS OVER `EnterEmpireDungeon`, the same as the seed and the
	 * layout controls do. Walking a dungeon off the empire map with a sub-type
	 * typed at the console gives the typed one, which is what a control for
	 * looking at a sub-type has to do to be worth having.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	ECataclysmDungeonSubType ChooseSubType() const;

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

	/**
	 * Put everything `Leader` commands beside it, on free floor around the
	 * current floor's entrance. Issue #1202, ruled 2026-09-30: a player's
	 * thralls and summoned creatures go down the stairs with the player.
	 * Answers how many were moved.
	 */
	int32 BringFollowersTo(APawn* Leader);

	/**
	 * Destroy everything any player commands. Issue #1202, ruled 2026-09-30:
	 * leaving the dungeon ends the run, and every thrall, imp and gadget with
	 * it, and their Fervour reserves with them. Answers how many ended.
	 */
	int32 EndEveryPlayersFollowers();

	/** The floor being stood on, once `BuildFloor` has run. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	TObjectPtr<ACataclysmDungeonFloor> CurrentFloor;

	/**
	 * What the dungeon is, as much of it as building a floor needs.
	 *
	 * GATHERED FROM THE SETTINGS ABOVE rather than from the empire dungeon
	 * directly, so that pressing Play in `L_Dungeon` and walking a dungeon off
	 * the empire map go down the same road. `EnterEmpireDungeon` fills those
	 * settings in from the dungeon; nothing else has to.
	 */
	FCataclysmDungeonIdentity DungeonIdentity() const;

	/**
	 * What the floor being stood on is, decided by `BuildFloor`.
	 *
	 * READ BY THREE THINGS. `BuildFloor` takes its layout, `PopulateFloor` takes
	 * its boss and its wave, and `RunModifierScore` takes its modifier score.
	 * They are one decision made once rather than three that could disagree.
	 *
	 * PUBLIC SO A TEST CAN READ IT. "The floor the player is standing on carries
	 * these modifiers" is the thing a Volatile dungeon's test has to check, and
	 * it is not visible in the geometry or in the creatures.
	 *
	 * NOT A `UPROPERTY`, the same as `ACataclysmDungeonFloor::Plan` next door
	 * and for the reason `FCataclysmFloorPlan`'s own comment gives: it is plain
	 * data holding no object, so it is a plain struct until something actually
	 * needs reflection. Unreal's header tool refuses a `UPROPERTY` of one.
	 */
	FCataclysmFloorBrief FloorBrief;

	// ----------------------------------------------------------------------
	// What the floor's modifiers do to the player. Issue #41
	// ----------------------------------------------------------------------

	/**
	 * The modifiers a floor of this dungeon starts from, and their danger.
	 *
	 * `Cataclysm.DungeonModifiers` FIRST, WHEN IT NAMES AT LEAST ONE ROW, and the
	 * dungeon's own otherwise. The console variable exists so a modifier can be
	 * tried in `L_Dungeon` without starting an empire run, which is the only
	 * other way a dungeon gets any -- and the owner's playtests never start one.
	 * The floor rules then treat a typed list exactly as they treat a drawn one,
	 * so a Volatile dungeon still re-draws.
	 *
	 * @param OutScore the sum of their danger scores, for the enemy score model
	 */
	TArray<FName> ChooseModifiers(float& OutScore) const;

	/**
	 * Puts what the floor's modifiers do on one character and works its stats
	 * out again. Starvation and Dehydration, today.
	 *
	 * TAKES THE TWO COMPONENTS RATHER THAN A PAWN, so a test can hand it a
	 * character built by hand: a test world has no player controller and so no
	 * player to find. `ApplyFloorRulesToPlayer` is the finding.
	 *
	 * @return whether the character's stats were worked out again
	 */
	bool ApplyFloorRulesTo(class UCataclysmAbilitySystemComponent* AbilitySystem,
						   class UCataclysmEquipmentComponent* Equipment) const;

	/**
	 * The same for the first player's character, then the floor panel, then one
	 * line in the log saying what the floor carries.
	 *
	 * CALLED WHENEVER THE FLOOR CHANGES -- at the end of `GoToFloor`, after
	 * `StartPlay` has a pawn to find, and after `LeaveEmpireDungeon` has emptied
	 * the brief -- so the rules follow the floor being stood on and never the one
	 * before it. A Horde dungeon's waves are its floors, so each wave applies
	 * them again.
	 *
	 * AND FIRST IT DESTROYS EVERY GROUND ZONE THE FLOOR'S RULES PLACED, with or
	 * without a player. Issue #1925. A Horde dungeon's waves share one arena,
	 * which `GoToFloor` does not clear, so without this each rule's zones would
	 * stay on the next wave with no rule acting for them.
	 */
	void ApplyFloorRulesToPlayer();

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
	 * A WAVE THAT WALKS IN IS NOT ALL PUT DOWN HERE. Issue #1544. This decides
	 * every creature of the wave and puts the first `WaveCreaturesPerFrame` of
	 * them on the floor; `Tick` puts down the rest, the same number a frame, and
	 * `CreaturesStillArriving` says how many are still to come.
	 *
	 * @return how many this call put on the floor: every creature of an ordinary
	 *         floor, or the first few of a wave that walks in
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
	 * Give one creature on this floor the Field Medic role, if the floor
	 * carries that rule and nothing living has it yet.
	 *
	 * WHAT THE ROW ASKS FOR. `War_Field_Medic`: "An elite 'Medic' enemy is
	 * present on each floor. It does not attack, but constantly heals all
	 * other enemies in a large radius." Issue #1648. The half it does not
	 * attack is issue #1680 and is not built.
	 *
	 * CALLED WHEN A FLOOR'S CREATURES ARE ALL DOWN, from both routes: after
	 * the loop that puts an ordinary floor down at once, and when an arriving
	 * wave's last creature lands. Rarity is decided inside
	 * `SpawnPlacedCreature`, so there is nothing to compare until then.
	 *
	 * THE RAREST CREATURE ON THE FLOOR, AND THE FIRST OF THEM ON A TIE, which
	 * is the nearest thing to "elite" the game can say today. Deterministic
	 * on purpose: the same floor chooses the same creature.
	 *
	 * ONE LIVING MEDIC AT A TIME, asked by looking rather than remembered in a
	 * flag, so there is no per-floor state to reset wrongly. A medic that is
	 * killed and cleaned up leaves the floor without one, which is the point
	 * of the rule; on a Horde floor a later wave may then bring another.
	 *
	 * PUBLIC SO A TEST CAN DRIVE THE REAL THING RATHER THAN A COPY OF ITS
	 * RULE. `FloorEnemies` above and `FloorBrief` are public for the same
	 * reason: a test that had to build a whole dungeon to reach this would
	 * not get written, and a pure helper tested on its own would not prove
	 * that anything calls it.
	 */
	void ChooseTheFloorsMedic();

	/**
	 * March of Progress' Commander, chosen as the floor is populated. Issues #1820, #41.
	 *
	 * THE HIGHEST RUNG, TIES TO THE FIRST PLACED, WHICH IS THE MEDIC'S RULE ABOVE AND FOR
	 * ITS REASON: a strictly-greater comparison means the choice does not depend on how
	 * the list happens to be ordered beyond the order the population pass placed them in.
	 * Called from the same two places that one is, for the same reason -- a wave that is
	 * still arriving may yet bring a creature of a higher rung.
	 *
	 * NOTHING ON THE CHOSEN CREATURE CHANGES, WHICH IS WHERE IT DIFFERS FROM THE MEDIC.
	 * That one writes a flag the creature acts on; this rule only needs to know which
	 * creature it is, and the Commander gameplay tag could not carry it: that tag means
	 * "buffed by a commander" rather than "is a commander", which is what
	 * `ACataclysmEnemyCharacter::CommanderMultiplier` does with it. Issue #1997 is what a
	 * player would need in order to see which creature it is.
	 *
	 * ONE COMMANDER AT A TIME AND ONE PAYMENT A FLOOR. It returns early while one is
	 * alive, and again once the player has killed this floor's, so a Horde dungeon's next
	 * wave cannot be paid for a second Commander on a floor already paid for.
	 * `PopulateFloor` forgets both before it places anything.
	 *
	 * PUBLIC FOR THE REASON THE MEDIC'S CHOOSER IS: a test that had to build a whole
	 * dungeon to reach this would not get written, and a pure helper tested on its own
	 * would not prove that anything calls it.
	 */
	void ChooseTheFloorsCommander();

	/**
	 * Plague Harbingers chooses this floor's or wave's Harbingers, from `CurrentWave` once all
	 * of it is placed, where `ChooseTheFloorsCommander` is called and for its reason: no
	 * creature can be ruled out as the floor's boss until it has drawn its rung. Issues #1820
	 * and #41.
	 */
	void ChooseThePlagueHarbingers();

	/**
	 * Which creature is this floor's Commander, or null when the floor has none.
	 *
	 * DEFINED IN THE CPP AND NOT HERE, because `ACataclysmEnemyCharacter` is only
	 * forward-declared at the top of this header. `TWeakObjectPtr::Get` casts to the
	 * pointed-at type, and doing that on an incomplete type is not worth relying on.
	 */
	ACataclysmEnemyCharacter* TheFloorsCommander() const;

	/**
	 * Whether this creature is this floor's Commander and still alive. Issue #1997.
	 *
	 * FALSE FOR ONE THAT HAS DIED, and not by asking the weak pointer: a killed creature
	 * keeps a valid weak pointer until its body is removed, so the floor's own
	 * `bMarchOfProgressCommanderSlain` and the creature's dead mark are asked as well.
	 * Ruled under the project owner's delegation: the hover panel names a living
	 * Commander only.
	 */
	bool IsTheFloorsCommander(const AActor* Creature) const;

	/**
	 * The same, found in a world rather than asked of a game mode in hand. Read by
	 * `ACataclysmHUD::DrawCreaturePanel`.
	 *
	 * **NOTHING IN AN AUTOMATION RUN CAN MAKE THIS ANSWER TRUE**, for the reason
	 * `JudgmentZonesMagicFindIn` gives: it finds the game mode through
	 * `UWorld::GetAuthGameMode`, and a world built for a test has none. So the member
	 * above and `UCataclysmCreaturePanel::LinesUnderTheHealthBar` are tested, and this hop
	 * is held by a Python check that reads the heads-up display's code.
	 */
	static bool IsTheFloorsCommanderIn(const UObject* WorldContext, const AActor* Creature);

	/** How many commanders the player has killed in this run. */
	int32 CommandersKilledThisRun() const { return MarchOfProgressCommandersKilled; }

	// ----------------------------------------------------------------------
	// Waves, for a Horde dungeon. Issue #1467
	//
	// THE PROJECT OWNER'S RULES, 2026-09-07, VERBATIM: "They should walk in, and
	// the next wave should spawn when there is only 10% or less of the previous
	// wave remaining. In horde dungeons, enemies should also get a much larger
	// aggro range, so they all always run towards the player. Horde dungeons are
	// basically arenas, they should be one big open space with all of the
	// enemies spawning around the outside and rushing you."
	//
	// A HORDE DUNGEON IS ONE ARENA AND ITS FLOOR COUNT IS ITS WAVE COUNT.
	// Going down a floor is the next wave arriving rather than a new space, so
	// nothing here changes the floor count, the day cost or what the dungeon is
	// worth. `FCataclysmFloorBrief::bSameArenaAsLastFloor` is the seam that says
	// so and this is what spends it.
	// ----------------------------------------------------------------------

	/**
	 * The creatures of the wave that arrived most recently, in the order placed.
	 *
	 * A SUBSET OF `FloorEnemies` AND NOT A REPLACEMENT FOR IT. Survivors of
	 * earlier waves stay in the arena and are still fought; they are still in
	 * `FloorEnemies` and are no longer in this. What decides when the next wave
	 * arrives is how much of THIS wave is left, which is what the owner's rule
	 * says -- "10% or less of the previous wave remaining" is about one wave and
	 * not about everything standing.
	 *
	 * EMPTY IN AN ORDINARY DUNGEON, whose floors hold no waves at all.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	TArray<TObjectPtr<ACataclysmEnemyCharacter>> CurrentWave;

	/**
	 * How many creatures the current wave has put on the floor.
	 *
	 * IT GROWS WHILE THE WAVE ARRIVES AND STOPS WHEN ALL OF IT HAS. Issue #1544
	 * puts a wave down a few creatures a frame, and `ShouldTheNextWaveArrive`
	 * does not judge a wave until `CreaturesStillArriving` is zero, so the rule
	 * this is the denominator of only ever reads the finished count.
	 *
	 * RECORDED RATHER THAN COUNTED FROM `CurrentWave`, because that array is
	 * what is still standing and this is the denominator. Ten percent of "the
	 * previous wave" is ten percent of what it arrived with, not ten percent of
	 * what is left of it, which would be a threshold that fell as the player
	 * killed and could never be reached.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	int32 WaveSpawned = 0;

	/**
	 * How many waves have arrived in this arena, counted from the first.
	 *
	 * ZERO ON A FLOOR THAT IS NOT A WAVE, which is most floors in the game. An
	 * ordinary dungeon's creatures are not a wave, and counting them as one
	 * would make this figure mean two different things.
	 *
	 * FOR A TEST AND A LOG LINE. "The second wave arrived" is otherwise only
	 * visible as the floor number changing, which also changes for a dungeon
	 * that is not a Horde one.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	int32 WavesArrived = 0;

	/** How many of the current wave are still alive. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	int32 WaveStillAlive() const;

	/**
	 * Whether the wave standing is finished, so the next one may arrive.
	 *
	 * FALSE ON A FLOOR THAT IS NOT A WAVE, so nothing an ordinary dungeon does
	 * can reach the wave machinery. False on a wave that put nothing on the
	 * floor, so an empty arena does not run every wave of the dungeon in one
	 * frame.
	 *
	 * **THE LAST WAVE OF A DUNGEON HAS TO BE CLEARED, NOT THINNED TO A TENTH.**
	 * Finishing it beats the dungeon, and its Gatekeeper is one of its
	 * creatures, so a tenth remaining would let a player win with the boss still
	 * standing. See the reasoning in the `.cpp`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	bool ShouldTheNextWaveArrive() const;

	/**
	 * Brings the next wave in when the one standing is down to a tenth.
	 *
	 * CALLED FROM `Tick` AND FROM NOTHING ELSE IN THE GAME. It is public so a
	 * test can call the same function the tick calls rather than a private
	 * helper written for the test, but the tick is what drives it in play.
	 *
	 * THE NEXT WAVE IS THE NEXT FLOOR, so this goes through `GoDownOneFloor`
	 * exactly as the stairs do. That is what makes a wave cost a day and what
	 * makes the last wave finish the dungeon, without either rule being written
	 * down twice.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	void BringTheNextWaveIn();

	/**
	 * How often the wave is looked at, in seconds.
	 *
	 * NOT EVERY FRAME, because the answer cannot change faster than the player
	 * can kill and the count walks the whole wave -- up to 350 creatures on an
	 * arena floor. A quarter of a second is faster than a player notices and is
	 * a fortieth of the work of doing it at 120 frames a second.
	 */
	static constexpr float SecondsBetweenWaveChecks = 0.25f;

	/**
	 * How many of an arriving wave's creatures are put on the floor in one frame.
	 *
	 * WHAT WAS WRONG, ISSUE #1544. A wave put every one of its creatures on the
	 * floor in the frame it arrived. In the project owner's Horde session on
	 * 2026-09-10 that was 139 creatures in one frame, in each of four waves, and
	 * in the two waves that arrived during play rather than when play began, the
	 * spawning alone took 176 ms and 163 ms of that frame by the log's own
	 * timestamps: about 1.2 ms a creature. It also possessed every creature in
	 * one frame, which is what lined up their thinking.
	 * `ACataclysmEnemyController::FirstThinkDelaySeconds` is the direct fix for
	 * that, and this is the other half.
	 *
	 * FOUR, AND IT IS A JUDGEMENT. At about 1.2 ms a creature, four adds about
	 * 5 ms to each frame while a wave arrives, against 16.7 ms for a whole frame
	 * at 60 frames a second, and a wave of 139 takes 35 frames: a little under
	 * 0.6 seconds at 60 frames a second. Eight would halve the time and double
	 * the cost per frame. The design does not say how fast a wave appears, and a
	 * wave forms around the outside of an arena up to 271 metres across, so the
	 * player sees it come in from the edges either way.
	 *
	 * HOW MANY ARRIVE AND WHERE ARE UNCHANGED. The population pass decides every
	 * creature and its cell when the wave begins, exactly as before, and this
	 * decides only which frame each one appears in.
	 *
	 * ONLY A WAVE THAT WALKS IN. An ordinary floor's creatures are put down while
	 * the floor is built, all at once, as they always were.
	 */
	static constexpr int32 WaveCreaturesPerFrame = 4;

	/**
	 * How many of the current wave's creatures have still to arrive.
	 *
	 * ZERO ONCE THE WHOLE WAVE IS ON THE FLOOR, and always zero on a floor whose
	 * creatures are not a wave that walks in. Read by tests, and by
	 * `ShouldTheNextWaveArrive`, which will not judge a wave until all of it has
	 * arrived.
	 */
	int32 CreaturesStillArriving() const { return WaveStillToArrive.Num(); }

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

	/** True on a floor carrying `Chaos_Chaotic_Loot`. Issues #1820 and #41. */
	virtual bool DropsAreChaotic() const override;

	virtual ECataclysmDungeonType RunDungeonType() const override
	{
		return DungeonType;
	}

	/**
	 * What sub-type the run is walking. Read by Enemy Score.
	 *
	 * `ChooseSubType` RATHER THAN `DungeonSubType`, for the same reason
	 * `RunTotalFloors` above answers with `ChooseTotalFloors`: the console can
	 * move it, and this is read together with what the floor was built from.
	 * `UCataclysmEnemyScore::ScoreThisFloor` takes the sub-type from here and
	 * adds its weight to every creature on the floor, so a game mode that had
	 * carved a Horde arena and reported no sub-type would pay the wrong
	 * experience for everything standing in it. Issue #1502.
	 */
	virtual ECataclysmDungeonSubType RunDungeonSubType() const override
	{
		return ChooseSubType();
	}

	/**
	 * What the modifiers in force ON THIS FLOOR add to every creature's enemy
	 * score.
	 *
	 * **THE FLOOR'S AND NOT THE DUNGEON'S, SINCE 2026-09-07.** For every dungeon
	 * but a Volatile one they are the same number, because an ordinary dungeon's
	 * modifiers do not change as it is walked. A Volatile dungeon re-draws them
	 * on every floor -- "Dungeon modifiers change every floor" is the whole of
	 * what the design gives that sub-type -- so its creatures are worth a
	 * different amount on floor 2 than on floor 1. `DungeonModifierScore` above
	 * is still the dungeon's own. Issue #41.
	 *
	 * ZERO UNTIL A FLOOR HAS BEEN BUILT, which is the same answer this gave
	 * before any dungeon carried modifiers, and the right one: a game mode that
	 * has not built a floor is not standing in a dungeon.
	 */
	virtual float RunModifierScore() const override
	{
		return FloorBrief.ModifierScore;
	}

	/**
	 * Gives one spawned creature the health, armour and attack damage its design
	 * calls for, rolls its rarity, and draws its modifiers.
	 *
	 * PUBLIC SO A TEST CAN GIVE ONE CREATURE ITS STATS THE WAY A FLOOR DOES.
	 * `SpawnPlacedCreature` spawns a creature and then calls this, and a test
	 * that could reach it only through a whole floor could not choose what the
	 * creature draws. Issue #1552 was a fault in the last step this takes: the
	 * draw came after every call that turns modifiers into stats.
	 * `Cataclysm.EnemyModifiers.AFloorCreatureThatDrawsShielderSpawnsWithItsShield`
	 * seeds the draw and then calls this. It was protected until then.
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
							ECataclysmDungeonCreature Creature,
							int32 FixedRung = -1) const;

private:
	/**
	 * Seconds since the wave was last looked at.
	 *
	 * PRIVATE, unlike the wave state above, because it is bookkeeping for the
	 * tick and says nothing about the dungeon. A test drives
	 * `BringTheNextWaveIn` or ticks this actor rather than reading it.
	 */
	float SinceWaveCheckSeconds = 0.0f;

	/**
	 * One beat of every dungeon modifier that changes while the player plays.
	 * Issue #41, slice 2.
	 *
	 * ON THE WAVE CHECK'S BEAT RATHER THAN A TIMER OF ITS OWN, for the reason that
	 * check gives: a quarter of a second is faster than a player notices, and a
	 * timer per rule is one more thing to cancel.
	 *
	 * NOTHING HERE GROWS WITH A HORDE'S CROWD. Every rule below acts on the
	 * player, or on ground placed near the player, and none walks the floor's
	 * creatures. On a floor carrying none of these rows the cost is one test of
	 * a short array per rule and nothing else.
	 *
	 * SAID THAT WAY RATHER THAN AS A COUNT, AND BOTH COUNTS THAT WERE HERE WENT
	 * WRONG. This comment opened "One beat of the TWO dungeon modifiers" and
	 * went on to say "ALL THREE RULES ACT ON THE PLAYER ALONE ... three tests",
	 * while the function below tested six rows and two of them placed actors.
	 * Issue #1786. Read the tests in the function; do not write their number
	 * here again.
	 */
	void StepFloorRulesThatChange();

	/**
	 * Forced March: take a share of maximum health from a player standing still.
	 * Issue #41, slice 2.
	 */
	void StepForcedMarch(class ACataclysmPlayerCharacter* Player,
						 class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * The Nihil's Embrace: the resistance its walking has cost, and the reward a
	 * cleanse granted while it lasts. Issue #41, slice 2.
	 */
	void StepNihilsEmbrace(class ACataclysmPlayerCharacter* Player,
						   class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Death's Embrace: the stacks the time on this floor has earned, and what
	 * they take off the player's healing. Issue #41, slice 5.
	 */
	/**
	 * Drop an Infernal Rain patch if one is due. Issues #1605 and #41.
	 *
	 * THE ROW: "Fireballs rain in combat zones, leaving patches of burning ground
	 * that deal fire damage over time for 10 seconds."
	 *
	 * IT TAKES THE PLAYER BECAUSE IT NEEDS TWO THINGS FROM THEM: where the
	 * fighting is, which is the only reading of "in combat zones" this rule can
	 * take cheaply on every beat, and their maximum health, because a modifier's
	 * damage here is a share of that rather than a flat figure.
	 *
	 * THE ONLY WRITER OF `InfernalRainSecondsSinceLastPatch`. See that field.
	 */
	void StepInfernalRain(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Singularity Wells: places void orbs and slows whoever stands in one.
	 * Issues #1605 and #41.
	 *
	 * TWO JOBS ON ONE BEAT, AND THE ORDER MATTERS. It sets the slow from the wells
	 * that exist BEFORE it considers placing another, because the beat a player
	 * walks out of a well is a beat on which nothing is placed. Placing first and
	 * setting the slow inside that branch would leave a character slowed by a well
	 * they had left, or by one that had been destroyed.
	 *
	 * THE SLOW IS A FIELD AND NOT AN APPLY. See `ApplyChangingFloorEffects`: a
	 * rule that assembled its own effects would undo every other rule's four
	 * times a second.
	 *
	 * IT ASKS EACH WELL WHETHER IT COVERS THE PLAYER rather than measuring
	 * distances itself. `ACataclysmGroundZone::Covers` is the same test the zone's
	 * own sweep makes, so the thing that slows a character and the thing that
	 * damages them cannot disagree about where the well is. That function's
	 * comment says "Read by tests", and this is the first production caller.
	 */
	void StepSingularityWells(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	void StepDeathsEmbrace(class ACataclysmPlayerCharacter* Player,
						   class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Withered Ground: whether the player is standing on a patch of Barren
	 * Earth right now. Issue #41.
	 *
	 * IT ONLY READS. Patches are placed by `NoteDeathForWitheredGround` when a
	 * creature dies; this asks, every beat, whether any of them covers the
	 * player, and sets the field the one applier reads.
	 *
	 * THE SAME SHAPE AS `StepSingularityWells` AND FOR THE SAME REASON. The
	 * reading must not sit inside the placing branch: the beat a player walks
	 * OFF a patch is a beat on which nothing was placed, and a reduction left
	 * behind would follow them around the floor.
	 *
	 * STANDING ON TWO PATCHES IS THE SAME AS STANDING ON ONE, deliberately.
	 * The row states one figure and says nothing about overlapping patches,
	 * and this row's patches overlap far more readily than the other two
	 * hazards' -- they are placed wherever creatures die, which on a floor
	 * with a choke point is repeatedly the same few metres. Stacking would
	 * reach the pipeline's floor after two.
	 */
	void StepWitheredGround(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Fungal Overgrowth: whether the player is standing on a mushroom right now,
	 * and which kind. Issues #1820 and #41.
	 *
	 * IT ONLY READS, like `StepWitheredGround` above it, and for the reason that
	 * function gives: the beat a player walks OFF a mushroom is a beat on which
	 * nothing was placed, so the reading cannot sit inside the placing branch.
	 *
	 * IT ASKS TWICE AND SETS TWO FIELDS, which is the whole difference. A player
	 * can stand where a helping and a hurting mushroom overlap -- mushrooms are
	 * placed wherever creatures die, which on a floor with a choke point is
	 * repeatedly the same few metres -- and both then apply. Neither figure is
	 * changed by the other being present.
	 *
	 * STANDING ON TWO MUSHROOMS OF ONE KIND IS THE SAME AS STANDING ON ONE, for
	 * the reason `StepWitheredGround` gives: the row states one figure per kind
	 * and says nothing about overlapping, and stacking a 50% slow twice would
	 * reach the pipeline's floor almost at once.
	 */
	void StepFungalOvergrowth(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Holy Repercussions: put the Judgment the player is carrying back on them.
	 * Issues #1820 and #41.
	 *
	 * ITS BEAT DECIDES NOTHING, exactly like `StepWastingSickness`. The count
	 * moves when a burst lands; this compares two integers and returns on almost
	 * every beat. It exists because taking the stairs takes the reduction off the
	 * character, and something has to notice and put it back.
	 */
	void StepHolyRepercussions(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Leech Spores: spend every cloud the player is touching. Issues #1820 and #41.
	 *
	 * ONE DRAIN PER CLOUD, AND THE CLOUD IS GONE AFTER IT. A cloud lasts the floor
	 * untouched; contact takes a share of the player's maximum health, heals the
	 * creatures near the player with it, and removes the cloud. That reads the
	 * row's "explodes" and "contact" together, which is a judgement recorded in
	 * `docs/DECISIONS.md`.
	 *
	 * TWO CLOUDS UNDER THE PLAYER ARE TWO CONTACTS. Clouds are left wherever
	 * creatures die, so several can overlap, and "once per cloud" means each
	 * drains once rather than the pile draining once.
	 *
	 * IT READS AND ACTS ON THE SAME BEAT, which is the difference from
	 * `StepWitheredGround`. That rule turns a reduction on and off as the player
	 * moves; this one does a thing once and ends, so there is nothing to turn
	 * back off when they step away.
	 */
	void StepLeechSpores(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Blood Altar: stand the altar at the exit, and pulse on its clock. Issues #1820
	 * and #41.
	 *
	 * THE ALTAR IS PLACED ON THE FIRST BEAT OF A FLOOR CARRYING THE ROW, at
	 * `ACataclysmDungeonFloor::ExitWorld` -- the cell `PlaceStairs` puts the stairs
	 * on, which exists before the stairs do. A Horde floor places its stairs only
	 * after its waves.
	 *
	 * A PULSE EVERY `BloodAltarSecondsBetweenPulses` FROM THE FLOOR'S START. It takes
	 * a share of the player's maximum health for each death the altar has counted,
	 * nothing from an altar nobody has fed, and only from the player, only within
	 * the altar's reach.
	 */
	void StepBloodAltar(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Necrotic Ground: spread the fog on its cadence, and on each beat act on whoever
	 * stands in it. Issues #1820 and #41.
	 *
	 * THE PLAYER IS IN THE FOG WHEN ANY PATCH COVERS THEIR LOCATION, the reading
	 * Withered Ground makes of its patches. A CREATURE IS IN IT BY THE SAME TEST:
	 * `UCataclysmTargeting::FindEnemiesInLine` asks the patch about each creature's
	 * location, as `ACataclysmGroundZone::Covers` does for the player.
	 *
	 * THREE THINGS ON ONE BEAT: the healing cut written when it changes, through the
	 * shared applier; the burn once a second, dealt by this rule rather than by each
	 * patch; and a creature regeneration. Then the cadence, so a patch placed on this
	 * beat is first stood in on the next.
	 */
	void StepNecroticGround(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Ravenous Hoard: count every hostile creature's time alive and give it the stacks
	 * that time has earned. Issues #1820 and #41.
	 *
	 * EVERY CREATURE, WHEREVER IT CAME FROM -- placed with the floor, spawned by a
	 * wave, summoned by another creature -- because the row names no exception. The
	 * player is asked for only to tell which side a creature is on.
	 */
	void StepRavenousHoard(class ACataclysmPlayerCharacter* Player);

	/**
	 * Grave Tide: on its cadence, put a wave of creatures on the floor. Issues #1820
	 * and #41.
	 *
	 * THE RULE SPAWNS ITS OWN WAVE RATHER THAN QUEUEING IT. `WaveStillToArrive` is the
	 * one queue of creatures still to arrive, a floor change replaces or keeps it whole,
	 * and nothing on a queued creature says which rule queued it -- so a queued wave
	 * could arrive on a floor that does not carry the row. A wave here is a handful of
	 * creatures, where that queue exists for a wave of sixty.
	 *
	 * THE FLOOR'S OWN POPULATOR CHOOSES WHERE THEY STAND, so one place decides what a
	 * cell may hold.
	 */
	void StepGraveTide();

	/**
	 * Volatile Evolution: give every wounded creature its chance of rising a rung of
	 * the rarity ladder. Issues #1820 and #41.
	 *
	 * THE PLAYER IS ASKED FOR ONLY TO TELL WHICH SIDE A CREATURE IS ON, which is what
	 * `StepRavenousHoard` above asks it for.
	 *
	 * IT PUTS THE HEALTH AND THE ENERGY SHIELD BACK ITSELF. Both routes into a new rung
	 * -- `ACataclysmEnemyCharacter::SetRarityStep` and its `DrawModifiersForRarity` --
	 * end by calling `ApplyStartingAttributes`, which refills both pools to the new
	 * maximums. A mutation that healed the creature would undo the work that wounded it,
	 * and the wound is what let it mutate.
	 */
	void StepVolatileEvolution(class ACataclysmPlayerCharacter* Player);

	/**
	 * Royal Guard: give each badly hurt creature of Elite rank or above its one chance of
	 * calling two guards of its own kind. Issues #1820 and #41.
	 *
	 * THE PLAYER IS ASKED FOR ONLY TO TELL WHICH SIDE A CREATURE IS ON, which is what
	 * `StepRavenousHoard` and `StepVolatileEvolution` above ask it for.
	 *
	 * THE GUARDS ARE THE SUMMONER'S OWN KIND, WORKED OUT FROM ITS CLASS. A creature does
	 * not carry which of the seven kinds it is, so the step compares its class with each
	 * kind's through `ClassFor`. A creature that is none of them -- the plain
	 * `ACataclysmEnemyCharacter` a test spawns, or a kind added to the class list and not
	 * to `ClassFor` -- calls nothing and says so in the log.
	 */
	void StepRoyalGuard(class ACataclysmPlayerCharacter* Player);

	/**
	 * Mortal Decay: take the floor's share of the player's health this beat.
	 * Issues #1786 and #41.
	 *
	 * THE SAME SHAPE AS `StepForcedMarch` AND NOT THE SHAPE OF THE FOUR RULES
	 * BETWEEN THEM. It writes no field on this object and calls no applier: the
	 * row saps health outright, so `UCataclysmSkillEffects::ReduceHealthDirectly`
	 * is the whole of it and `FCataclysmPlayerFloorEffects` never hears about it.
	 *
	 * HOW FAST IT SAPS IS THE FLOOR NUMBER AND WHETHER A KILL'S WINDOW IS STILL
	 * RUNNING, and both are read here rather than stored: the depth is on the
	 * floor's brief and the window is a world-time stamp, so nothing has to be
	 * put back when either changes.
	 *
	 * NOT A HIT, for the reason Forced March gives: the damage comes from the
	 * floor rather than from an attacker, so no evasion roll, no block, no
	 * armour, no resistance, no critical strike and no ailment touch it.
	 */
	void StepMortalDecay(class ACataclysmPlayerCharacter* Player,
						 class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Suffering Aura: take a beat's share of the player's maximum health and maximum
	 * mana. Issues #1820 and #41.
	 *
	 * THE SAME RATE ON EVERY FLOOR, and NOT A HIT, for Mortal Decay's reason: the loss
	 * comes from the floor, so nothing in the mitigation order touches it. It reads no
	 * state of its own, so it has nothing to reset at the stairs.
	 */
	void StepSufferingAura(class ACataclysmPlayerCharacter* Player,
						   class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Wasting Sickness: put whatever stacks the player has onto their maximums.
	 * Issues #1786 and #41.
	 *
	 * IT DECIDES NOTHING AND ROLLS NOTHING. The stacks change on events -- a blow
	 * landing, a boss dying, the player dying -- and this only notices that the
	 * count has moved away from what was last applied, which is the shape every
	 * other beat-driven rule here uses.
	 *
	 * THE BEAT IS WHAT PUTS THE REDUCTION BACK AFTER A FLOOR CHANGE, and that is
	 * why this rule needs a beat step at all despite being event-driven.
	 * `ApplyFloorRulesToPlayer` replaces the player's whole set of dungeon
	 * modifiers when a floor changes, which takes this reduction off them; the
	 * stacks themselves survive, because the row says the debuff is "permanent
	 * for the duration of the dungeon". Without this the player would walk down a
	 * staircase and be cured.
	 */
	void StepWastingSickness(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Grasping Tentacles: place them, decide whether one grabs, and hold or
	 * release the player. Issues #1786 and #41.
	 *
	 * THREE JOBS ON ONE BEAT, AND THE ORDER MATTERS, which is the lesson
	 * `StepSingularityWells` records. It resolves the grab the player is ALREADY
	 * under before looking for a new one, and looks for a new one before placing
	 * another tentacle. Placing first would let a tentacle appear and grab on the
	 * same beat, which is not "careful of getting too close" -- the player had no
	 * chance to be careful of something that was not there.
	 *
	 * A GRAB IS A WORLD-TIME STAMP AND SO IS EACH TENTACLE'S COOLDOWN, so nothing
	 * has to be counted down and a beat that does not run costs the player
	 * nothing.
	 *
	 * ONE ROLL PER TENTACLE THAT COVERS THE PLAYER, not one roll for the floor.
	 * Standing where two reaches overlap is twice as dangerous, which is what
	 * "getting too close" should mean when you are close to two of them.
	 */
	void StepGraspingTentacles(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Edict of Silence: bring the silence when it is due, and lift it when it has
	 * run. Issues #1786 and #41.
	 *
	 * A CADENCE AND AN EXPIRY STAMP, WHICH ARE BOTH SHAPES THIS FILE ALREADY
	 * USES. Three rules count a cadence to place something and three hold a
	 * world-time stamp that expires; this is the first to do both, and issue
	 * #1786 flagged the row for needing a cycle "no existing rule has" on that
	 * basis. The halves existed; only their combination is new.
	 *
	 * THE CLOCK IS COUNTED IN BEATS AND THE SILENCE IS HELD IN WORLD TIME, which
	 * is deliberate and not an inconsistency. Death's Embrace's field records the
	 * reason for counting beats: a subtraction from world time would count a
	 * paused game, and that difference is exactly what a player would call
	 * unfair. The silence itself is a stamp because the beat only has to ask
	 * whether it has passed.
	 */
	void StepEdictOfSilence(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Dirge Resonance: move the floor's clock a beat, and at a crescendo give every living
	 * creature on the floor `Status.Buff.Commander` for the row's ten seconds. Issues
	 * #1820 and #41.
	 *
	 * THE CLOCK COUNTS BEATS, as the Edict of Silence's does, so a cycle is ninety seconds
	 * of the floor being played. The haste itself runs on the world's clock, because the
	 * status is a gameplay effect with a duration. Refreshes the floor panel when the
	 * whole second it shows changes, and not four times a second.
	 */
	void StepDirgeResonance();

	/**
	 * Famine Scarcity: switch one worn non-weapon slot off for this floor, or clear it on
	 * a floor without the row. Issues #1820 and #41. Called as the floor's rules reach the
	 * player, BEFORE the attributes are written, so the refresh that follows reads it.
	 */
	void ChooseTheScarceSlot(class UCataclysmEquipmentComponent* Equipment) const;

	/**
	 * Call in an artillery strike, and land the one already called.
	 *
	 * TWO THINGS IN ONE BEAT STEP, AND THEY NEVER BOTH HAPPEN. While a circle is
	 * on the ground the step only counts the warning down; when the warning is
	 * out it lands the shell and destroys the circle, and the cadence is free to
	 * place the next on a later beat. The rule's own `ArtilleryStrikeIsDue`
	 * refuses while one is in the air, so the order inside this function is not
	 * what keeps them apart.
	 *
	 * THE CIRCLE DEALS NO DAMAGE OF ITS OWN. It is an `ACataclysmGroundZone`
	 * placed with a damage of zero, which is what `Void_Grasping_Tentacles`
	 * already does for a tentacle that only has to be somewhere. The shell is
	 * this function asking `UCataclysmTargeting::FindEveryoneInLine` what is
	 * standing in the circle at the moment it lands, so what the player was
	 * warned about and what is hit cannot disagree.
	 *
	 * EVERYONE, NOT THE OTHER SIDE, which is the row's own sentence: "Enemies
	 * and players can be hit". The ground zone makes the same choice on its
	 * `bBurnsEveryone`, and no production code sets that flag; this rule asks
	 * the everyone-search directly because a flag on a harmless circle would
	 * decide nothing.
	 */
	void StepArtilleryStrike(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Drop a bombardment when it is due, and empower whatever is standing in a
	 * crater.
	 *
	 * TWO HALVES ON DIFFERENT CLOCKS. The craters arrive every thirty seconds and
	 * burn on their own, because an `ACataclysmGroundZone` needs nothing from the
	 * beat once it is placed. The empowerment is re-applied EVERY beat to every
	 * creature standing in one, because a status effect has a duration and a
	 * creature that walks out must lose it.
	 *
	 * THE CRATER BURNS THE PLAYER AND NEVER A CREATURE. The zone's own sweep asks
	 * for its source's enemies, and its source is the floor's hazard actor, whose
	 * enemy is the player. Nothing here sets a flag; the default is already what
	 * the row asks for.
	 *
	 * AND THE BEAT EMPOWERS CREATURES AND NEVER THE PLAYER, by asking the
	 * opposite question: `FindEnemiesInLine` with the PLAYER as the origin finds
	 * what the player is fighting. The two searches cannot overlap, which is why
	 * no rule here has to exclude anybody by name.
	 */
	void StepHallowedGroundfall(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Put every floor effect on the player, the beat-driven ones included.
	 * Issue #41, slice 5.
	 *
	 * ONE APPLIER FOR EVERY RULE, AND THAT IS NOT TIDINESS.
	 * `UCataclysmDungeonModifierEffects::ApplyToCharacter` replaces the whole set
	 * of dungeon stat modifiers, so a rule that assembled its own effects and
	 * applied them would zero every field it did not know about. With one such
	 * rule that is invisible; with two on one floor they undo each other four
	 * times a second, and which survives depends on the order they are called in.
	 * Slice 2 had one and slice 5 is the second, so the shape is fixed here.
	 *
	 * THE FLOOR'S OWN RULES COME FROM `PlayerEffectsFor` AND THE REST FROM THIS
	 * OBJECT'S FIELDS, which is the whole contract: anything worked out on the
	 * beat is held on the game mode, and this is the only place that reads all of
	 * it at once.
	 */
	void ApplyChangingFloorEffects(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * A death anywhere on the floor, for whichever rules the floor carries.
	 * Issue #41, slices 2 and 6.
	 *
	 * IT DISPATCHES AND DOES NOTHING ITSELF. Each rule below tests for its own
	 * row and returns if the floor does not carry it. This used to hold one
	 * rule's logic behind a single early return on that rule's key, which is
	 * the shape that stops a second listener from ever being added.
	 *
	 * THEY ONLY RECORD AND PLACE. No stat work happens inside the notice a
	 * death arrived on; the beat applies what they record, within a quarter of
	 * a second.
	 */
	void OnSomethingDied(const struct FCataclysmDeathNotice& Notice);

	/**
	 * A blow landing anywhere on the floor, for whichever rules the floor
	 * carries. Issues #1786 and #41.
	 *
	 * THE FIRST PRODUCTION LISTENER ON `UCataclysmCombatEvents::OnHit`.
	 * That announcement has existed since issue #41's slice 4 and every binding
	 * to it was in a test until this one. The announcement is not new; the
	 * listening is.
	 *
	 * IT DISPATCHES AND DOES NOTHING ITSELF, deliberately shaped like
	 * `OnSomethingDied` above rather than holding one rule's logic behind an
	 * early return on that rule's key. That shape is what stopped a second death
	 * listener from being added without rewriting the first, and there are two
	 * more rows that will want a blow.
	 */
	void OnSomethingWasHit(const struct FCataclysmHitNotice& Notice);

	/**
	 * Wasting Sickness's stack, on a blow that landed on the player.
	 * Issues #1786 and #41.
	 *
	 * A LANDED BLOW AND NOT AN ATTEMPT. `FCataclysmHitNotice::Landed` is what
	 * reached the target after every mitigation step and is zero for a blow that
	 * was evaded or wholly stopped, so such a blow inflicts nothing and the row's
	 * chance keeps meaning what it says.
	 *
	 * THE PLAYER MUST BE THE ONE STRUCK. The announcement is made for every blow
	 * on the floor, the player's own blows on creatures included, and this row
	 * says "Enemies have a chance to inflict" -- so the notice's target has to be
	 * the player's own pawn.
	 *
	 * IT ONLY RECORDS. The stack count moves here and the beat applies it, within
	 * a quarter of a second, which is what both death listeners already do.
	 */
	void NoteHitForWastingSickness(const struct FCataclysmHitNotice& Notice);

	/**
	 * Wasting Sickness's two cures, on a death. Issues #1786 and #41.
	 *
	 * TWO ROUTES, AND THE ROW STATES ONE OF THEM. "can only be removed by
	 * defeating a floor boss" is the row's. The other is the project owner's
	 * ruling of 2026-09-10, recorded in `docs/DECISIONS.md`: anything that lasts
	 * only for a dungeon ends at the player's death, "since in the real game that
	 * dungeon would resolve on death and you wouldn't respawn in it". This row is
	 * named there as one of the five that ruling covers.
	 *
	 * "A FLOOR BOSS" IS ANY BOSS ON THE FLOOR, which is a judgement recorded with
	 * the rest. `UCataclysmDungeonModifierEffects::WastingSicknessKey` carries
	 * the reasoning and the reading it was chosen over.
	 *
	 * THE PLAYER'S OWN DEATH APPLIES AT ONCE RATHER THAN WAITING FOR THE BEAT,
	 * and that is the one place in this file where the difference is observable.
	 * `ACataclysmPlayerCharacter::Revive` refills the vitals, and the refill
	 * READS the maximums; a beat that had not yet run would leave the player
	 * refilled to the lowered figure and then lifted, standing up short of full.
	 * That file's own comment names this row as the reason its clearing runs
	 * before its refill.
	 */
	void NoteDeathForWastingSickness(const struct FCataclysmDeathNotice& Notice);

	/**
	 * The Nihil's Embrace's cleanse, on a boss's defeat. Issue #41, slice 2.
	 */
	void NoteDeathForNihilsEmbrace(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Withered Ground's patch of Barren Earth, where a creature died.
	 * Issue #41.
	 *
	 * EVERY DEATH, WITH NO CAP AND NO CHANCE, because the row states the
	 * trigger and states no limit: "Enemies leave patches of Barren Earth on
	 * death." A cap would make that sentence stop being true at whichever
	 * enemy hit it, and a chance would need a number the row does not give.
	 *
	 * THE PATCH IS OWNED BY THE FLOOR AND NOT BY THE CREATURE THAT DIED.
	 * `ACataclysmFloorHazardSource` exists for this: every route that applies
	 * anything refuses unless the source resolves to an ability system
	 * component, and a corpse cannot be that. Its own header names this row
	 * as one of the two reasons it was written.
	 */
	void NoteDeathForWitheredGround(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Fungal Overgrowth: leave a mushroom where a creature died, of one kind or
	 * the other. Issues #1820 and #41.
	 *
	 * THE SAME SHAPE AS `NoteDeathForWitheredGround` ABOVE, which is the rule
	 * this one was written from: a creature dies, a piece of ground lasting the
	 * floor is left where it fell, and the beat asks whether the player is
	 * standing on one. It differs in placing one of TWO kinds and in remembering
	 * them in two lists.
	 *
	 * EVERY CREATURE DEATH LEAVES ONE. The roll here decides the KIND and never
	 * whether there is a mushroom -- "Killing enemies creates mushrooms" states
	 * no chance, the way Withered Ground's row states none. That is the whole
	 * difference from `NoteDeathForSporeClouds` and `NoteDeathForHellfire`,
	 * whose rows both say "chance" and whose rolls can therefore come to
	 * nothing.
	 *
	 * NOTHING IS DEALT AND NOTHING IS APPLIED, so unlike the two rules named
	 * above this one needs no instigator that could refuse. The hazard source is
	 * asked for anyway, for the reason `NoteDeathForWitheredGround` gives: a
	 * patch with no owner is a different kind of object from every other floor
	 * hazard, and a later change giving a mushroom something to apply would have
	 * to rebuild the ownership first.
	 *
	 * IT GIVES ITS MUSHROOMS NO DAMAGE TYPE. A patch's `DamageType` decides which
	 * resistance its damage is met by, and a mushroom deals no damage at all; its
	 * appearance comes from `ACataclysmGroundZone::DrawnAsType`, which is set per
	 * mushroom and cannot be changed afterwards by another rule placing something
	 * else.
	 */
	void NoteDeathForFungalOvergrowth(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Holy Repercussions: a creature answers a blow the player landed on it with
	 * a burst, and leaves a Judgment stack on the player. Issues #1820 and #41.
	 *
	 * THE SAME DIRECTION AS `NoteHitForBrandOfTheAggressor` AND THE OPPOSITE OF
	 * `NoteHitForWastingSickness`. "upon being hit" is the CREATURE being hit, so
	 * this tests that the player is the attacker and a creature is the target --
	 * which is Brand's test exactly. Wasting Sickness tests the other way because
	 * its row is about blows landing on the player.
	 *
	 * THE BURST IS DEALT IN THE CREATURE'S OWN NAME, unlike every other rule
	 * here, and that is the row's own sentence: the ENEMY retaliates. So the
	 * damage is the creature's own attack damage, its element is read off the
	 * creature the way every creature's blow is, and an illusion -- whose attack
	 * damage `Chaos_Illusory_Enemies` sets to zero -- retaliates for nothing
	 * without this function knowing that rule exists.
	 *
	 * IT REACHES THE CREATURE'S ENEMIES, WHICH IS THE PLAYER'S SIDE.
	 * `UCataclysmTargeting::FindEnemiesInSphere` asked with the creature as
	 * instigator answers the player and their allies. The row says "dealing
	 * damage in an area" and names no side, and the side is decided by whose
	 * burst it is rather than by a ruling.
	 *
	 * TWO TESTS DECIDE WHOSE BLOW COUNTS, AND EACH GUARDS A BLOW THE OTHER DOES
	 * NOT.
	 *
	 * THE TARGET MUST BE A CREATURE. On its own this is the only thing refusing a
	 * blow the PLAYER lands on the PLAYER -- which is exactly how
	 * `Demonic_Brand_of_the_Aggressor` delivers its eruption. Without it, on a
	 * floor carrying both rows, every eruption would add a Judgment stack.
	 *
	 * THE ATTACKER MUST BE THE PLAYER. On its own this is the only thing refusing a
	 * blow on a creature from another CREATURE, a floor hazard or another rule's
	 * explosion. THIS IS A READING OF THE ROW, not its wording: "upon being hit"
	 * names no attacker. Requiring the player follows "retaliate", which answers an
	 * attacker, and Judgment, which lands on the player and makes sense only if the
	 * player provoked it.
	 *
	 * BOTH REFUSE THIS RULE'S OWN BURST, which is a creature hitting the player. So
	 * a burst cannot provoke another whichever half is removed.
	 *
	 * TWO EARLIER DRAFTS OF THIS WERE WRONG, and the second tried to fix the first.
	 * The first said the attacker half is what stops a burst provoking a burst. The
	 * second said a creature-on-creature test would let each half fail on its own,
	 * forgetting the target half's case. Both were caught by tracing a guard proof's
	 * prediction through every assertion before any build.
	 */
	void NoteHitForHolyRepercussions(const struct FCataclysmHitNotice& Notice);

	/**
	 * Leech Spores: leave a cloud where a creature died. Issues #1820 and #41.
	 *
	 * THE SAME SHAPE AS `NoteDeathForWitheredGround`: a creature dies, a piece of
	 * ground lasting the floor is left where it fell, and the beat asks whether
	 * the player is standing on it. It differs in what contact does, which is
	 * `StepLeechSpores`'s business, and in a cloud being spent by it.
	 *
	 * EVERY KILL BY THE PLAYER LEAVES ONE, AND NOTHING ELSE DOES. The row says
	 * "When you kill an enemy, a cloud ... explodes", which names the killer and
	 * states no chance. A death whose notice names anyone else, or nobody,
	 * leaves no cloud.
	 *
	 * NO DAMAGE PER TICK. The cloud does nothing to anybody by being there; the
	 * drain happens on contact, decided by the beat.
	 */
	void NoteDeathForLeechSpores(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Blood Altar: count a creature's death. Issues #1820 and #41.
	 *
	 * NO KILLER IS ASKED FOR, DELIBERATELY, which is the opposite of
	 * `NoteDeathForLeechSpores` above. This row says "Slaying enemies", which names
	 * nobody; that one says "When you kill an enemy". A creature killed by another
	 * creature, by a hazard or by its own health running out feeds the altar all the
	 * same, and so does an illusion, whose death is announced like any other.
	 */
	void NoteDeathForBloodAltar(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Spore Clouds' poison, on a creature dying near the player. Issues #1820
	 * and #41.
	 *
	 * THE ROLL IS DRAWN BEFORE THE PLAYER IS LOOKED FOR, because the row's own
	 * sentence puts it there: "Enemies have a chance to RELEASE SPORES on death
	 * that poison the player." Releasing is what the chance decides; reaching
	 * the player is a separate fact about where they were standing. Drawing it
	 * first also means a death rolls the same number of times wherever the
	 * player is, which is the reason `NoteHitForWastingSickness` gives for
	 * rolling even when its stack is already at the cap.
	 *
	 * THE POISON IS OWNED BY THE FLOOR AND NOT BY THE CREATURE THAT DIED, for
	 * exactly the reason `NoteDeathForWitheredGround` above gives: every apply
	 * route refuses unless the instigator resolves to an ability system
	 * component, and a corpse cannot be that.
	 * `UCataclysmSkillEffects::ApplyDamageOverTime` also reads the instigator's
	 * three damage-over-time stats, so whose name this is dealt in is not
	 * decoration.
	 *
	 * NOTHING HERE STATES A DAMAGE OR A DURATION. `UCataclysmAilments::Apply`
	 * applies Poison as `game/Data/StatusEffects.csv` says, at magnitude one,
	 * which is that row's designed figure and no more. A number copied into this
	 * file would be a second place to change it.
	 *
	 * NO STATE AND NO PER-FLOOR RESET. Each death is decided on its own, so
	 * unlike every other rule on this beat there is nothing to carry between
	 * floors and nothing for `ApplyFloorRulesToPlayer` to clear.
	 */
	void NoteDeathForSporeClouds(const struct FCataclysmDeathNotice& Notice);

	/** Contagious Touch: a creature that applied stacks died, so the panel says what is left. Issue #41. */
	void NoteDeathForContagiousTouch(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Hellfire's explosion, on a creature the player killed. Issues #1820 and
	 * #41.
	 *
	 * THE DYING CREATURE'S OWN ATTACK DAMAGE DECIDES THE SIZE, read off it while
	 * the notice is being dispatched and before anything is applied.
	 * `UCataclysmEnemyModifiers::InfernalBrand` works the same way and says why:
	 * a figure written into the rule would make every creature explode alike.
	 *
	 * THE BLOW IS DEALT IN THE FLOOR HAZARD SOURCE'S NAME AND NOT THE CORPSE'S,
	 * for the reason `NoteDeathForSporeClouds` above gives: an apply route refuses
	 * an instigator with no ability system, and that fault made three of the
	 * Artillery Strike's tests fail.
	 *
	 * IT REACHES EVERYONE INSIDE IT, WHICH IS A RULING AND NOT A DEFAULT. The row
	 * does not say who an exploding enemy hits. `docs/DECISIONS.md` records that a
	 * dungeon hazard belongs to no side, and `StepArtilleryStrike` asks
	 * `FindEveryoneInLine` for the same reason, so a creature standing beside the
	 * one that exploded is caught too. A chain of exploding creatures is the
	 * intended reading rather than an accident.
	 *
	 * THE CHAIN ENDS BY ITSELF AND NEEDS NO GUARD. A creature killed by an
	 * explosion announces its own death and may explode in turn, but
	 * `UCataclysmSkillEffects::MarkDead` refuses a second time for the same
	 * creature, so nothing can explode twice and the depth is bounded by how many
	 * creatures are alive.
	 *
	 * NO STATE AND NO PER-FLOOR RESET, like Spore Clouds and unlike every other
	 * rule on this beat.
	 */
	void NoteDeathForHellfire(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Demon Prince: a creature the PLAYER killed may bring a greater one of its own kind
	 * out of its corpse. Issues #1820 and #41.
	 *
	 * THE FIRST OF THESE LISTENERS TO ASK WHO DID THE KILLING. The nine above it fire on
	 * any creature's death; this row's sentence is "when you slay an enemy".
	 *
	 * WHICH IS TWO QUESTIONS, NOT ONE. `FCataclysmDeathNotice::Killer` is the blow's
	 * instigator, and a minion's blow is credited to its summoner, so the killer is the
	 * player when a minion lands the blow as well. `KillingCauser` is the actor that
	 * dealt it, which is the minion itself for a minion's blow, so the rule asks for the
	 * killer to be the player AND the dealer not to be a minion.
	 *
	 * RULED UNDER THE PROJECT OWNER'S DECISION that a minion's hits are the minion's own.
	 * If the Conduit keystone later makes a minion's kill the player's, this reads the
	 * dealer and would follow it.
	 */
	void NoteDeathForDemonPrince(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Epidemic: pass a dead creature's debuffs to the creature beside it, and end a chain
	 * of five by killing everything within the same reach. Issues #1820 and #41.
	 *
	 * THE KILL MUST BE THE PLAYER'S OWN, which is ONE question since issue #1515: the
	 * killer on the notice is the player only when the player really killed it, and a
	 * minion's kill is credited to the minion unless its summoner holds the Conduit
	 * keystone. A summoner who holds it spreads the disease from its minion's kill.
	 *
	 * IT REFUSES TO ROLL WHILE ITS OWN MASS KILL IS RUNNING. Those deaths are real --
	 * health to zero and `HandleDeath`, so the loot and the experience are paid -- and a
	 * real death is announced, so without the guard each one would come back here, roll
	 * again, and the five-spread event would feed itself.
	 */
	void NoteDeathForEpidemic(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Kill every creature within Epidemic's reach of a point, and bring one Plague Lord.
	 *
	 * SEPARATE FROM THE LISTENER SO THE GUARD IS ONE PLACE. It sets the flag, kills, and
	 * clears it, and the listener above never has to remember to.
	 */
	void EpidemicEndTheChain(const FVector& Where,
							 ECataclysmDungeonCreature LastVictimsKind);

	/**
	 * Blood-Forged Champions, on any creature's death: feed the nearest Elite.
	 *
	 * IT ASKS FOR NO KILLER, AND THE ABSENCE IS THE RULE RATHER THAN AN OVERSIGHT. The
	 * four death listeners above all ask "did the player do this", because their rows say
	 * "when you kill" or "when you slay". This row says "nearby dying allies" and names
	 * nobody, so a creature killed by another creature, by burning ground or by another
	 * floor rule feeds a champion exactly as the player's own kill does. Ruled under the
	 * project owner's delegation.
	 */
	void NoteDeathForBloodForgedChampions(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Vengeful Wraiths, on a creature the PLAYER killed: it may stand back up as a wraith.
	 *
	 * IT ASKS WHO KILLED, AND THE ROW IS WHY. "The one who killed them" needs a killer, so
	 * a kill by another creature or by burning ground raises nothing. A minion's kill
	 * without the Conduit keystone raises nothing either, because
	 * `UCataclysmCombatEvents::NoteBlow` credits that blow to the minion -- issue #1515 --
	 * and reading it as the summoner's here would put back by hand the credit that change
	 * took away.
	 *
	 * WHAT IT ASKS DECIDES WHETHER A WRAITH RISES, NOT WHOM IT CHASES. The creature AI
	 * picks targets by sight and takes no named quarry, so what makes a wraith a hunter is
	 * the sight multiplier it is spawned with.
	 */
	void NoteDeathForVengefulWraiths(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Void Parasite: a creature the player killed may leave a voidling where it died. Issues #1820 and #41.
	 * The kill is read as Demon Prince reads it; a floor source and a death that pays nothing leave none.
	 */
	void NoteDeathForVoidParasite(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Celestial Divine Resurgence, on every death. Issues #1820 and #41.
	 *
	 * RECORDS WHERE, WHAT AND AT WHICH RUNG the creature died, then asks whether enough
	 * of the floor has fallen; when it has, raises every recorded death at once, marked
	 * and at half health, and never again on this floor.
	 *
	 * A MARKED CREATURE IS SKIPPED ENTIRELY: not counted as placed or fallen, and not
	 * recorded. That is what keeps a risen creature, or a wraith, from rising again and
	 * from bringing the revival on sooner.
	 *
	 * THE RECORD IS NEEDED BECAUSE BODIES DO NOT LAST. A creature is destroyed once its
	 * death animation ends, often on the next tick, so nothing would be left to raise by
	 * the time half the floor had fallen.
	 */
	void NoteDeathForDivineResurgence(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Death Dead Rising, on every death. Issues #1820 and #41.
	 *
	 * ROLLS `DeadRisingChancePercent` FOR EVERY UNMARKED CREATURE THAT DIES, whoever
	 * killed it, and on a hit puts the same kind back where it fell, at the rung it died
	 * at, at full health and marked. A marked creature never rolls, so no creature gets
	 * more than one extra life from this row.
	 */
	void NoteDeathForDeadRising(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Demonic Blood Gates, on every death. Issues #1820 and #41.
	 *
	 * COUNTS THE PLAYER'S KILLS OF UNMARKED CREATURES, and nothing else: a death to any
	 * other cause changes nothing here, and is seen only because the creature stops
	 * standing, which lowers `BloodGatesPlacedCount`. Refreshes the panel on every death
	 * on a floor carrying the row, because either kind of death can move its figures.
	 */
	void NoteDeathForBloodGates(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Rule of Chaos, on every death, on a floor that drew its change to skill behaviour: a kill by the player clears
	 * every cooldown of the player and returns every spent use. Issues #1820 and #41.
	 *
	 * "A KILL" IS WHAT BLOOD GATES COUNTS: the notice names the player as the killer, the creature pays for its death,
	 * and it is not one a rule raised. A death to any other cause clears nothing.
	 *
	 * NO ROLL, ruled on 2026-10-08 under the owner's delegation. It calls
	 * `UCataclysmAbilitySystemComponent::RefillSkillCharges` with every slot's cooldown tag and then
	 * `RemoveActiveEffectsWithGrantedTags` with the same tags, in that order, so ending the cooldowns starts no
	 * further recharge. NOT `RollAndResetCooldowns`, whose roll can refuse a percent of 100, and NOT
	 * `ClearWhatDeathEnds`, which also ends every buff.
	 */
	void NoteDeathForRuleOfChaos(const struct FCataclysmDeathNotice& Notice);

public:
	/**
	 * Divine Resurgence's state, for the floor panel and for tests. How many unmarked
	 * creatures have fallen on this floor, how many the floor has placed that are not
	 * marked (the fallen plus the unmarked still standing), and how many rose.
	 */
	int32 DivineResurgenceFallenCount() const { return DivineResurgenceFallen; }
	int32 DivineResurgencePlacedCount() const;
	int32 DivineResurgenceRisenCount() const { return DivineResurgenceRisen; }

	/** How many creatures Dead Rising has put back on this floor, for the panel and tests. */
	int32 DeadRisingRisenCount() const { return DeadRisingRisen; }

	/**
	 * Blood Gates' state, for the floor panel, the stairs and tests. How many unmarked
	 * creatures the player has slain on this floor; how many the floor counts as placed
	 * (those kills plus the unmarked still standing); and whether the stairs are sealed
	 * right now, which is false on a floor without the row and on the last floor.
	 */
	int32 BloodGatesSlainCount() const { return BloodGatesSlain; }
	int32 BloodGatesPlacedCount() const;
	bool BloodGatesSealTheStairs() const;

	/**
	 * Unstable Portal's state, for the floor panel and tests: how many times the portal has
	 * rolled on this floor, and what the last roll did (0 down, 1 back to the entrance, 2 a
	 * mini-boss, -1 none yet). Issues #1820 and #41.
	 */
	int32 UnstablePortalRollCount() const { return UnstablePortalRolls; }
	int32 UnstablePortalLastOutcome() const { return UnstablePortalLast; }

	/**
	 * Nothing Is Forgotten's state, for the floor panel and tests: the health and attack
	 * damage the void holds for the final boss, and the boss it fed, if one has been.
	 * Issues #1820 and #41.
	 */
	float NothingIsForgottenHealthHeld() const { return NothingIsForgottenHealth; }
	float NothingIsForgottenDamageHeld() const { return NothingIsForgottenDamage; }
	ACataclysmEnemyCharacter* NothingIsForgottenFinalBoss() const
	{
		return NothingIsForgottenBoss.Get();
	}

	/**
	 * Add what the void holds to this boss, and remember it as the one fed, so a rung
	 * change can put the figures back. Called on the final floor's boss when it is
	 * placed; public so a test can feed a boss it made.
	 */
	void FeedTheFinalBoss(ACataclysmEnemyCharacter* Boss);

	/**
	 * The starvation curse's stacks, for the floor panel and tests. Issues #1820 and #41.
	 */
	int32 StarvationCurseMovementStacksHeld() const { return StarvationCurseMovementStacks; }
	int32 StarvationCurseHealthStacksHeld() const { return StarvationCurseHealthStacks; }

	/**
	 * Trick or Treat's counts in this dungeon, for the floor panel and tests, and whether a
	 * treat is hasting the player now. Issues #1820 and #41.
	 */
	int32 TrickOrTreatPickupCount() const { return TrickOrTreatPickups; }

	/** The skill Wild Magic has drawn and not yet triggered, or none. It is triggered on the next tick. */
	FName WildMagicPendingSkill() const { return WildMagicPending; }

	/**
	 * Triggers the pending skill at the aim it was drawn with. Called on the tick after the use that drew it,
	 * because a skill cannot be granted inside another skill's activation; public so a test, whose world is never
	 * ticked, can make it. @return whether a skill started.
	 */
	bool MakeTheWildMagicTrigger();

	int32 WildMagicTriggeredCount() const { return WildMagicTriggered; }
	FName WildMagicLastSkill() const { return WildMagicLast; }

	/** Seconds until Wild Magic may trigger again; zero or less when it may. */
	float WildMagicSecondsUntilNext() const;

	/** The skill Echo Chamber will copy on the next tick, or none. */
	FName EchoChamberPendingSkill() const { return EchoChamberPending; }

	/**
	 * Fires the pending copy at the point it was drawn for, and hits the player when they stand inside its area.
	 * Called on the tick after the use, for the reason `MakeTheWildMagicTrigger` is; public so a test, whose world is
	 * never ticked, can make it. @return whether a copy started.
	 */
	bool MakeTheEchoChamberCopy();

	int32 EchoChamberCopiesFired() const { return EchoChamberCopiesMade; }
	int32 EchoChamberSelfHits() const { return EchoChamberHitsOnThePlayer; }

	/** Where the pending copy, or the last one fired, is aimed. */
	FVector EchoChamberAimNow() const { return EchoChamberPendingAim; }
	int32 TrickOrTreatRaisedCount() const { return TrickOrTreatRaised; }
	bool TrickOrTreatIsHasting() const;

	/**
	 * Soul Harvest, for the floor panel and tests: the souls this creature holds, and the
	 * souls given in this dungeon. Issues #1820 and #41.
	 */
	int32 SoulHarvestSoulsOn(const ACataclysmEnemyCharacter* Creature) const;
	int32 SoulHarvestSoulsGiven() const { return SoulHarvestGiven; }

	/** The Reaper on this floor, or null before it arrives. For the floor panel and tests. */
	ACataclysmEnemyCharacter* TheReaperOnTheFloor() const { return TheReaper.Get(); }

	/** Demonic Guide, for the panel and tests: the floor's guide, or null. */
	ACataclysmEnemyCharacter* DemonicGuideOnTheFloor() const { return DemonicGuide.Get(); }

	/** The Plaguebearer on this floor, or null; and the stacks every other creature carries. Issues #1820 and #41. */
	ACataclysmEnemyCharacter* PlaguebearerOnTheFloor() const { return Plaguebearer.Get(); }
	int32 PlaguebearerStacksNow() const { return PlaguebearerStacks; }

	/**
	 * The Plaguebearer is chosen from this floor's own creatures once they are placed: a random one at the Elite rung, or
	 * a Common raised to it. Public so a test can choose again after changing the creatures' rungs. Issues #1820 and #41.
	 */
	void ChooseThePlaguebearer();

	/** Forget the Plaguebearer and its stacks. Public for the reason above. */
	void ForgetThePlaguebearer();

	/** Morale Break, for the panel and tests: the leaders still standing, the panicked, and the escaped away now. */
	TArray<ACataclysmEnemyCharacter*> MoraleLeadersNow() const;
	int32 MoraleBreakPanickedNow() const;
	int32 MoraleBreakEscapedNow() const;

	/**
	 * Morale Break's leaders are chosen from this floor's or wave's own groups once they are placed. Public so a test can
	 * choose again after changing the creatures' rungs. Issues #1820 and #41.
	 */
	void ChooseTheMoraleLeaders();

	/**
	 * Infernal Seals' bearers, chosen from this floor's own creatures once they are placed: the four highest rungs,
	 * each below Elite raised to it. Public so a test can choose after placing its own creatures. Issues #1820, #41.
	 */
	void ChooseTheSealBearers();

	/** Infernal Seals' beat: a bearer dead, taken, gone or made unable to be hurt gives its piece. */
	void StepInfernalSeals();

	/** Infernal Seals' state, for the floor panel and tests. */
	TArray<ACataclysmEnemyCharacter*> InfernalSealBearersNow() const;
	int32 InfernalSealPiecesHeld() const { return InfernalSealPieces; }
	int32 InfernalSealPiecesNeeded() const;
	bool InfernalSealsSealTheStairs() const;

	/**
	 * EVERY ROW SEALING THE STAIRS NOW, by its key; empty when they are open. `HandleStairsTaken` refuses while any
	 * seals, so with two rows the stairs open only when both release. The one question every sealing row answers.
	 * Issues #1820 and #41.
	 */
	TArray<FName> StairsSealedBy() const;

	/**
	 * Singularity Wells: a well at this point dealing this much a second, as the rule places one -- typed by its row,
	 * burning once a second with the others, turning projectiles -- and counted toward the cap. Public so a test can
	 * place one where it means to. Null when the row will not load or nothing could be spawned. Issues #1605, #41.
	 */
	class ACataclysmGroundZone* PlaceASingularityWellAt(const FVector& Where, float DamagePerSecond);

	/** Infernal Rain, for tests: the fireballs still falling, and the points their patches will be placed at. */
	TArray<class ACataclysmProjectile*> InfernalRainFireballsFalling() const;
	TArray<FVector> InfernalRainLandingPoints() const;
	TArray<class ACataclysmGroundZone*> InfernalRainPatchesNow() const;

	/**
	 * Sanctioned Passage, for the panel and tests: the Divine Gate standing, or null; whether its channel has begun; the
	 * seconds channelled; and whether it seals the stairs. Issues #1820 and #41.
	 */
	class ACataclysmFloorObject* DivineGateNow() const;
	bool DivineGateChannelBegun() const { return bDivineGateChannelling; }
	float DivineGateSecondsChannelled() const { return DivineGateSeconds; }
	bool SanctionedPassageSealsTheStairs() const;

	/**
	 * Lightforged Walls, for the panel and tests: how many creatures the floor placed still stand -- in the floor's
	 * creatures, not raised by a rule, able to be hurt and not the player's follower -- and whether they seal the
	 * stairs. Issues #1820 and #41.
	 */
	int32 LightforgedWallsStanding() const;
	bool LightforgedWallsSealTheStairs() const;

	/**
	 * Rule of Chaos, for the panel, the stairs and tests: the rule change this floor drew, one of
	 * `FCataclysmDungeonFloorRules::RuleOfChaos...`; whether its change "the stairs open by time" seals the stairs
	 * right now; and the whole seconds left until they open, rounded up, nought once they are open. Issues #1820 and
	 * #41.
	 *
	 * THE SEAL IS BY TIME ALONE: the floor's own clock, `SecondsOnThisFloor`, against
	 * `UCataclysmDungeonModifierEffects::RuleOfChaosStairsOpenAfterSeconds`, whatever has or has not been slain. Not on
	 * a Horde floor, which has no stairs, and not on the last floor, whose way out no row seals.
	 */
	int32 RuleOfChaosChangeNow() const { return FloorBrief.RuleOfChaosChange; }
	/** And how many of the player's kills on this floor cleared the cooldowns, under its change to skill behaviour. */
	int32 RuleOfChaosKillsCount() const { return RuleOfChaosKills; }
	bool RuleOfChaosSealsTheStairs() const;
	int32 RuleOfChaosStairsSecondsLeft() const;

	/**
	 * Lightforged Walls' sections, for the panel, the enemy modifiers and tests. Issues #1820 and #41. Ruled
	 * 2026-10-08, each a labelled judgement by the coordinating session under the owner's delegation.
	 *
	 * `FloorGetsSections` IS THE ONE CONDITION: the floor carries Lightforged Walls or Fragmented Reality, its plan
	 * is Halls, it is not a Horde arena, and it does not carry Shadowy Enemies. On every other floor Lightforged
	 * Walls is the sealed stairs only and Fragmented Reality does nothing. BARRIERS STAND ONLY ON A FLOOR CARRYING
	 * LIGHTFORGED WALLS: with Fragmented Reality alone the sections are planned and nothing is closed.
	 * `FloorSectionsNow` is what the search found for this floor, with no boundaries when it found none or was not
	 * asked. A BARRIER is the cells of one boundary closed with pillars: barrier i lies between sections i and i + 1,
	 * and the last section has none, the stairs being its seal.
	 *
	 * `StandingInSection` counts the creatures the floor placed in a section that still stand, by
	 * `IsOneOfTheFloorsOwnStanding`. A creature a rule adds later carries no section and is counted in none.
	 * `LightforgedWallsSectionHeld` is the lowest-numbered section whose barrier is closed and which still holds one,
	 * or `INDEX_NONE`. `ClosedSectionBarrierCells` is the cells of every barrier still closed.
	 * `AClosedBarrierStandsBetween` is true when both creatures carry a section, the sections differ, and any barrier
	 * between those two sections is closed; a creature with no section is never parted from anything.
	 */
	bool FloorGetsSections(const FCataclysmFloorPlan& Plan) const;
	const FCataclysmFloorSections& FloorSectionsNow() const { return FloorSections; }
	bool SectionBarrierIsClosed(int32 Barrier) const
	{
		return SectionBarrierClosed.IsValidIndex(Barrier) && SectionBarrierClosed[Barrier];
	}
	int32 StandingInSection(int32 Section) const;
	int32 LightforgedWallsSectionHeld() const;
	TArray<FIntPoint> ClosedSectionBarrierCells() const;

	/**
	 * The cells of one boundary that no other boundary of this floor holds. Issues #1820 and #41. Ruled 2026-10-08.
	 *
	 * TWO BOUNDARIES MAY SHARE CELLS: the search closes the cells of both together and nothing makes them apart. A
	 * CELL TWO BARRIERS HOLD CARRIES ONE PILLAR AND STAYS CLOSED UNTIL BOTH HAVE OPENED. Opening a barrier opens only
	 * those of its cells that no still-closed barrier holds. A FLOOR ON WHICH A BOUNDARY HAS NO CELL OF ITS OWN GETS
	 * NO SECTIONS, because opening that barrier would open nothing. `ClosedSectionBarrierCells` names a shared cell
	 * once.
	 */
	TArray<FIntPoint> CellsOnlyBarrierHolds(int32 Barrier) const;
	bool AClosedBarrierStandsBetween(const ACataclysmEnemyCharacter* One, const ACataclysmEnemyCharacter* Other) const;

	/**
	 * The floor's plan with the cells of every section barrier still closed made walkable: the floor as it is on the
	 * same seed without sections. A copy; the floor's own plan is not changed. Issues #1820 and #41. Ruled 2026-10-08.
	 *
	 * `FloorPopulationNow` IS THE ONE WAY THIS GAME MODE ASKS THE POPULATOR ANYTHING. The populator places only where
	 * the entrance can be walked from, so asked about the plan as it stands on a floor with a sealed section it would
	 * answer for the open sections alone. Asked through this it answers for the whole floor: when the floor's own
	 * creatures are placed, with `NoCreatureOn` the cells of every boundary, and when a rule asks during play which
	 * kinds the floor holds, with nothing barred. On a floor with no barrier closed it is the populator asked about
	 * the plan, as before.
	 */
	FCataclysmFloorPlan PlanWithSectionBarriersOpen() const;
	FCataclysmFloorPopulation FloorPopulationNow(const TSet<FIntPoint>& NoCreatureOn = TSet<FIntPoint>()) const;

	/**
	 * Angelic Wardens, for the panel and tests. Issues #1820 and #41. Ruled 2026-10-08, each a labelled judgement by
	 * the coordinating session under the owner's delegation; see `UCataclysmDungeonModifierEffects::AngelicWardensKey`.
	 *
	 * `AngelicStatueCellsStanding` is the cells of the statues not yet woken. `AngelicWardensNow` is the woken
	 * wardens that still stand; a slain one is not among them.
	 *
	 * `PlaceAnAngelicStatueOn` IS THE ONE WAY A STATUE IS PLACED, and public so a test can place one where it means
	 * to. It asks `AnObstacleMayClose` about the one cell, from the floor's entrance, with the cells
	 * `CellsHeldOrWarned` names held, and answers false when that refuses. Otherwise the cell is Solid in the plan, a
	 * raised pillar in the row's colour stands on it, and the statue remembers the section of its cell.
	 *
	 * `AClosedBarrierStandsBetweenSections` is `AClosedBarrierStandsBetween`'s question asked of two section
	 * numbers: true when both are sections, they differ, and any barrier between them is closed.
	 */
	TArray<FIntPoint> AngelicStatueCellsStanding() const;
	TArray<ACataclysmEnemyCharacter*> AngelicWardensNow() const;
	bool PlaceAnAngelicStatueOn(FIntPoint Cell);
	bool AClosedBarrierStandsBetweenSections(int32 One, int32 Other) const;

	/**
	 * Fragmented Reality, for the panel and tests. Issues #1820 and #41. Ruled 2026-10-08, each a labelled judgement
	 * by the coordinating session under the owner's delegation; see
	 * `UCataclysmDungeonModifierEffects::FragmentedRealityKey`.
	 *
	 * `FragmentedRealitySectionNow` is the section of this floor that is Fragmented, decided when the floor's
	 * sections are planned, or `INDEX_NONE` on a floor that does not carry the row or has no sections.
	 * `FragmentedRealityPairNow` is the pair in force, one of
	 * `UCataclysmDungeonModifierEffects::FragmentedReality...`, or `INDEX_NONE` while the player is outside the
	 * section. `FragmentedRealityEntriesSoFar` is how many times the player has entered the section on this floor,
	 * which is the number the next entry's draw is made with.
	 *
	 * `ForceFragmentedRealityPairForTests` IS FOR A TEST AND NOTHING IN PLAY CALLS IT: while it holds a pair, every
	 * entry draws that pair and not the seeded one. `INDEX_NONE`, the default, leaves the seeded draw. It is not
	 * cleared by a floor change.
	 */
	int32 FragmentedRealitySectionNow() const { return FragmentedRealitySection; }
	int32 FragmentedRealityPairNow() const { return FragmentedRealityPairApplied; }
	int32 FragmentedRealityEntriesSoFar() const { return FragmentedRealityEntries; }
	void ForceFragmentedRealityPairForTests(int32 Pair) { FragmentedRealityForcedPair = Pair; }

	/**
	 * Those in the Dark: what each placement rule costs on the floor as it stands, FOR A TEST TO LOG. NOTHING IN
	 * PLAY CALLS THIS, ruled 2026-10-09: `PlaceTheChasms` makes the draw once, with every rule.
	 *
	 * The draw is made again four times on the floor as it stands, with one more rule each time, and changes
	 * nothing. The four `After` figures are how many chasms each of those draws places. THEY ARE CAPPED AT THE
	 * NUMBER ASKED FOR: a cell a rule refuses is replaced by the next cell of the shuffle, so two of them differ
	 * only on a floor where the cells run out. The four `Refused` figures are what say whether a rule refused
	 * anything: in the draw with every rule, how many cells the draw came to and skipped for that rule before it
	 * had the number asked for.
	 *
	 * NOT THE SAME HELD CELLS AS WHEN THE FLOOR'S CHASMS WERE CHOSEN. The floor's own chasm cells are left out of
	 * the held cells here, so they can be chosen again. But the floor's creatures, placed after the chasms, hold
	 * their cells now, the player stands at the entrance and the stairs on the exit. So `AfterTheCrossingQuestion`
	 * may differ from what was placed.
	 */
	struct FThoseInTheDarkRuleCounts
	{
		int32 WalkableNow = 0;
		int32 AfterTheEntranceAndStairs = 0;
		int32 AfterHeldAndBarriers = 0;
		int32 AfterNoTwoSideBySide = 0;
		int32 AfterTheCrossingQuestion = 0;
		int32 RefusedNearTheEntranceOrOnTheStairs = 0;
		int32 RefusedHeldOrOnABarrier = 0;
		int32 RefusedBesideAChasm = 0;
		int32 RefusedByTheCrossingQuestion = 0;
	};
	FThoseInTheDarkRuleCounts ThoseInTheDarkCountsRuleByRule() const;

	/**
	 * Those in the Dark, layer 1 of 2, for the panel and tests. Issues #1820 and #41. Ruled 2026-10-08 and
	 * 2026-10-09; see `UCataclysmDungeonModifierEffects::ThoseInTheDarkKey`.
	 *
	 * `ThoseInTheDarkChasmCellsNow` is this floor's chasm cells, in the order they were chosen. Each is walkable in
	 * the plan. `ThoseInTheDarkChasmZonesDrawn` is how many of their marks stand now; the marks are made on the
	 * beat, and made again there when one is lost. `ThoseInTheDarkFallsOnThisFloor` is how many falls this floor has
	 * recorded, which is nought or one.
	 *
	 * `ThePlayerIsOnTheDarkFloor` IS THE ONE FLAG OF LAYER 2: true from a fall until the dark floor's stairs are
	 * taken or the dungeon is left, and false at every other time. Only a fall sets it. It is asked where the
	 * chasms are placed, so the dark floor gets none. `TheDarkFloorFellFromFloor` is the number of the floor the
	 * player fell from, nought when not on the dark floor. `TheDarkFloorSealsTheStairs` is whether a creature the
	 * dark floor placed still stands, which seals its stairs.
	 *
	 * `FThoseInTheDarkCount` IS WHAT THE PLACEMENT COUNTED ON THIS FLOOR, kept so a test can log it: the walkable
	 * cells when the chasms were chosen, the chasms asked for, and how many the draw places with the rules applied
	 * all together, which is what was placed. What each rule cost is not counted in play, ruled 2026-10-09; a
	 * test asks `ThoseInTheDarkCountsRuleByRule` for it.
	 */
	struct FThoseInTheDarkCount
	{
		int32 Walkable = 0;
		int32 Asked = 0;
		int32 Placed = 0;
	};
	const TArray<FIntPoint>& ThoseInTheDarkChasmCellsNow() const { return ThoseInTheDarkChasmCells; }
	int32 ThoseInTheDarkChasmZonesDrawn() const;
	int32 ThoseInTheDarkFallsOnThisFloor() const { return ThoseInTheDarkFallsNoted; }
	const FThoseInTheDarkCount& ThoseInTheDarkCountNow() const { return ThoseInTheDarkCount; }
	bool ThePlayerIsOnTheDarkFloor() const { return bOnTheDarkFloor; }
	int32 TheDarkFloorFellFromFloor() const { return DarkFloorFellFrom; }
	bool TheDarkFloorSealsTheStairs() const;

	/** Forget Morale Break's leaders, flights and the escaped. Public for the reason above. */
	void ForgetMoraleBreak();

	/** Contagious Touch: whether this floor carries it. Issues #1820 and #41. */
	bool ContagiousTouchIsOn() const;

	/** Contagious Touch: the stacks the player carries, the sum over the living creatures that applied them. */
	int32 ContagionStacksNow() const;

	/** Contagious Touch: a creature's touch landed on the player, so it applies one stack more, up to the most. */
	void NoteContagiousTouch(ACataclysmEnemyCharacter* Toucher);

	/**
	 * The dungeon game mode in this world, or null. FOUND BY WALKING THE LEVEL, as the player's revival finds it,
	 * because a world built for a test has no authority game mode and no test can supply one.
	 */
	static ACataclysmDungeonGameMode* InWorld(UWorld* World);

	/** Famished Beasts, for the panel and tests: the drops eaten on this floor. Issues #1820 and #41. */
	int32 FamishedBeastsDropsEatenNow() const { return FamishedBeastsFloor == FloorNumber ? FamishedBeastsDropsEaten : 0; }

	/** The elite a Blood Bond holds on this floor, or null. For the floor panel and tests. */
	ACataclysmEnemyCharacter* BloodBondedOnTheFloor() const { return BloodBonded.Get(); }

	/** Plague Convergence, for the floor panel and tests: the living creatures it sent. */
	int32 PlagueConvergenceCreaturesAlive() const;

	/** Whether this creature is one Plague Convergence sent. */
	bool IsAPlagueConvergenceCreature(const AActor* Actor) const;

	/** Plague Convergence's disease stacks on the player. */
	int32 PlagueConvergenceDiseaseStacks() const { return PlagueConvergenceStacks; }

	/** Divine Wrath's beam on this floor, or null between beams. For the panel and tests. */
	class ACataclysmGroundZone* DivineWrathBeamOnTheFloor() const { return DivineWrathBeam.Get(); }

	/** How many creatures Divine Wrath's beams have destroyed on this floor. */
	int32 DivineWrathDestroyedCount() const { return DivineWrathDestroyed; }

	/** Echoes of the Past, for the panel and tests: the last attacks recorded on this floor. */
	TArray<int32> EchoAttacksRecordedThisFloor() const;

	/** How many of the last floor's dead come back as echoes on this one. */
	int32 EchoesComingThisFloor() const { return EchoesFromLastFloor.Num(); }

	/** The echoes standing now. */
	TArray<ACataclysmEnemyCharacter*> EchoesStandingNow() const;

	/** Plague Harbingers, for the panel and tests: the Harbingers alive now. */
	TArray<ACataclysmEnemyCharacter*> PlagueHarbingersAlive() const;

	/** The trail patches standing now, of every Harbinger, or of `Harbinger` alone. */
	int32 PlagueHarbingerTrailPatches(const ACataclysmEnemyCharacter* Harbinger = nullptr) const;

	/** Wings of the Host, for the panel and tests: the feather marks counting down now. */
	TArray<class ACataclysmGroundZone*> WingsOfTheHostMarksNow() const;

	/**
	 * Where a flyover's feathers fall: every `WingsOfTheHostFeatherEveryCm` along the straight
	 * line through `Through` in the level direction `Direction`, as far as the floor reaches both
	 * ways, keeping only the points on floor cells. At `Through`'s height. Issues #1820 and #41.
	 */
	static TArray<FVector> WingsOfTheHostFeathers(const ACataclysmDungeonFloor& Floor,
												  const FVector& Through, const FVector& Direction);

	/**
	 * The floor cells a flyover may pass through: those whose middle is within
	 * `WingsOfTheHostPassesWithinCm` of `Centre`, measured level. A line through a cell's middle
	 * marks that cell's feather at least, so every flyover marks one. Issues #1820 and #41.
	 */
	static TArray<FIntPoint> WingsOfTheHostThroughCells(const ACataclysmDungeonFloor& Floor, const FVector& Centre);

	/** Eternal Chorus, for the panel and tests: the sources still singing. */
	TArray<ACataclysmEnemyCharacter*> EternalChorusSourcesNow() const;

	/** Eternal Chorus, for tests: the earshot zone of `Source`, or null. */
	class ACataclysmGroundZone* EternalChorusEarshotOf(const ACataclysmEnemyCharacter* Source) const;

	/**
	 * The health a chorus source is given: the Imp's at Common, 87, as a play-test value. Issues
	 * #1820 and #41.
	 *
	 * RULED SO A PLAYER DESTROYS IT IN A FEW SECONDS: four to six seconds of basic attacks with a +0
	 * one-handed weapon and under two with a two-handed one. The rule's challenge is reaching the
	 * source, not a long fight with something that does nothing. `docs/DECISIONS.md` has the table.
	 */
	float EternalChorusSourceHealth() const { return ImpHealth; }

	/**
	 * Where a floor's choruses stand: up to `Count` random floor cells, each at least
	 * `EternalChorusApartCm` from the floor's entrance and from each other. Fewer when the floor
	 * has no more room. Issues #1820 and #41.
	 */
	static TArray<FIntPoint> EternalChorusCells(const ACataclysmDungeonFloor& Floor, int32 Count);

	/**
	 * Whether `Cell` is a floor cell beside a wall: at least one of its four neighbours is not floor,
	 * a neighbour off the plan counting as not floor. Infested Veins' veins stand only on such cells.
	 * Issues #1820 and #41.
	 */
	static bool InfestedVeinsCellIsBesideAWall(const FCataclysmFloorPlan& Plan, FIntPoint Cell);

	/**
	 * Where a floor's veins stand: `EternalChorusCells`' rule, among floor cells beside a wall only.
	 * Fewer when the floor has no more room. Issues #1820 and #41.
	 */
	static TArray<FIntPoint> InfestedVeinsCells(const ACataclysmDungeonFloor& Floor, int32 Count);

	/** Infested Veins, for the panel and tests: the veins standing now. */
	TArray<ACataclysmEnemyCharacter*> InfestedVeinsStanding() const;

	/** Infested Veins, for tests: the zone drawn around `Vein`, or null. */
	class ACataclysmGroundZone* InfestedVeinZoneOf(const ACataclysmEnemyCharacter* Vein) const;

	/** Infested Veins, for the panel and tests: veins destroyed on this floor, and whether the guardians came. */
	int32 InfestedVeinsDestroyedHere() const { return InfestedVeinsDestroyed; }
	bool InfestedVeinsGuardiansCame() const { return bInfestedVeinsGuardiansCame; }

	/** Carrion Feast, for the panel and tests: the carcasses lying, the feeders standing, and the carcasses eaten. */
	TArray<ACataclysmEnemyCharacter*> CarrionCarcassesNow() const;
	TArray<ACataclysmEnemyCharacter*> CarrionFeedersNow() const;
	int32 CarrionFeastStacksNow() const { return CarrionFeastStacks; }

	/** The health a vein is given: the Imp's at Common, 87, as the other floor sources have. A play-test value. */
	float InfestedVeinHealth() const { return ImpHealth; }

	/** Void Parasite, for the panel and tests: the voidlings standing now, not yet attached or killed. */
	TArray<ACataclysmEnemyCharacter*> VoidlingsNow() const;

	/** Void Parasite, for the panel and tests: how many voidlings the player carries. */
	int32 VoidParasiteStacksHeld() const { return VoidParasiteStacks; }

	/** Wasting Sickness, for tests: how many stacks the player carries. */
	int32 WastingSicknessStacksHeld() const { return WastingSicknessStacks; }

	/**
	 * The choice screen: a choice made at a floor object, sent to the rule that placed it. Answers whether anything
	 * happened: false for an object gone, a choice it does not offer or cannot offer now, or a rule that does not
	 * answer. Called by `UCataclysmChoicePanelWidget` and by tests. Issues #1820 and #41.
	 */
	bool ChooseAtFloorObject(class ACataclysmFloorObject* Object, FName ChoiceKey);

	/** Grim Totems, for the panel and tests: the totems standing, and the Elite creatures embracing brought. */
	TArray<class ACataclysmFloorObject*> GrimTotemsNow() const;

	/** Battlefield Relics, for the panel and tests: the relics standing, a relic's kind, and the spirits standing. */
	TArray<class ACataclysmFloorObject*> BattlefieldRelicsNow() const;
	int32 BattlefieldRelicKindOf(const class ACataclysmFloorObject* Relic) const;
	TArray<ACataclysmEnemyCharacter*> RelicSpiritsStanding() const;

	/**
	 * Pandora's Box, for the panel and tests: the boxes standing, the creatures its waves brought still standing, and the
	 * drops the last box opened for a reward gave.
	 */
	TArray<class ACataclysmFloorObject*> PandorasBoxesNow() const;
	TArray<ACataclysmEnemyCharacter*> ChaosSpawnStanding() const;
	int32 PandorasBoxLastRewardDrops() const { return PandorasBoxRewardDrops; }

	/** Carrion Feast, for the panel and tests: its purification altar standing, or null; and whether it was used. */
	class ACataclysmFloorObject* PurificationAltarNow() const;
	bool AltarIsConsecrated() const { return bAltarConsecrated; }

	/** Infernal Beacons, for the panel and tests: the beacons standing, and the stacks this dungeon has activated. */
	TArray<class ACataclysmFloorObject*> InfernalBeaconsNow() const;
	int32 InfernalBeaconStacksNow() const { return InfernalBeaconStacks; }

	/**
	 * War Banner, for the panel and tests: the banner standing to be planted, or null; whether it is planted and held,
	 * the seconds held, and the creatures its waves brought still standing.
	 */
	class ACataclysmFloorObject* WarBannerNow() const;
	bool WarBannerIsPlanted() const { return bWarBannerPlanted; }
	bool WarBannerIsHeld() const { return bWarBannerHeld; }
	float WarBannerSecondsHeld() const { return WarBannerHeldSeconds; }
	TArray<ACataclysmEnemyCharacter*> BannerAssailantsStanding() const;

	/**
	 * Forced Tithes, for the panel and tests: the altar standing, or null; whether this floor's tithe was paid or
	 * refused; whether angels are owed for a tithe left unpaid; and the angels still standing.
	 */
	class ACataclysmFloorObject* TitheAltarNow() const;
	bool TitheWasPaid() const { return bTithePaid; }
	bool TitheWasRefused() const { return bTitheRefused; }
	bool TitheAngelsAreOwed() const { return bTitheAngelsDue; }
	TArray<ACataclysmEnemyCharacter*> TitheAngelsStanding() const;

	/**
	 * Pact of Temptation, for the panel and tests: the altar standing, or null; the pacts it offers; this floor's buff
	 * and the next floor's, INDEX_NONE for none; how many of each pact this dungeon has taken; and how many in all.
	 */
	class ACataclysmFloorObject* PactAltarNow() const;
	const TArray<int32>& PactsOfferedNow() const { return PactOffered; }
	int32 PactBuffThisFloor() const { return PactBuffNow; }
	int32 PactBuffNextFloor() const { return PactBuffNext; }
	const TArray<int32>& PactCursesTaken() const { return PactCurseCounts; }
	int32 PactsTakenNow() const { return PactsTaken; }

	/** Blood Price, for the panel and tests: the bleed stacks this dungeon has left on the player. */
	int32 BloodPriceStacksHeld() const { return BloodPriceStacks; }

	/**
	 * Where a rule standing an altar at the floor's exit puts it: THE EXIT CELL FOR THE FIRST, and for each after it the
	 * next walkable cell beside the exit, so two altars never share a cell. The first is the earliest in a fixed order
	 * of such rows the floor carries: Blood Altar, then Forced Tithes, then Pact of Temptation, then Sanctioned
	 * Passage's Divine Gate. Issues #1820 and #41.
	 */
	FVector ExitAltarWorld(FName RuleKey) const;
	TArray<ACataclysmEnemyCharacter*> GrimTotemElitesStanding() const;

	/** Void Parasite, for tests: this floor's light zone, or null before its first beat or on a floor without one. */
	class ACataclysmGroundZone* VoidParasiteLightNow() const;

	/**
	 * Shadowy Enemies, for tests: the centres of this floor's light zones, the exit's last when a boss stands there;
	 * empty on a floor without the row. Issues #1820 and #41.
	 */
	TArray<FVector> ShadowyEnemiesLightsNow() const;

	/** Shadowy Enemies, for tests: how many of this floor's light zones are drawn now. */
	int32 ShadowyEnemiesLightZonesDrawn() const;

	/** Necrotic Bloom, for the panel and tests: the flowers still standing. */
	TArray<ACataclysmEnemyCharacter*> NecroticBloomFlowersNow() const;

	/** Necrotic Bloom, for tests: how many waves `Flower` has sent, or -1 when it is not this floor's. */
	int32 NecroticBloomWavesOf(const ACataclysmEnemyCharacter* Flower) const;

	/** The health a flower is given: the Imp's at Common, 87, as the Chorus source has. A play-test value. */
	float NecroticBloomFlowerHealth() const { return ImpHealth; }

	/**
	 * Where a flower's wave stands: the floor cells whose middle is within `NecroticBloomWaveWithinCm`
	 * of `Flower`, measured level, leaving out the cell the flower stands in so a creature is not put
	 * inside it; that cell alone when there is no other. Issues #1820 and #41.
	 */
	static TArray<FIntPoint> NecroticBloomWaveCells(const ACataclysmDungeonFloor& Floor, const FVector& Flower);

	/**
	 * Sets what the rule `Source` does to `Creature`'s all-resistance -- `Added` points, then times
	 * `Multiplier` -- and writes its base: its own, plus every rule's points, times every rule's multiplier.
	 * 0 and 1 take the rule off. Issues #1820 and #41.
	 *
	 * ONE RECORD FOR EVERY RULE, THE SAME SHAPE AS THE CREATURE'S DAMAGE MAP, as ruled by the coordinating
	 * session on 2026-09-25: Trial of Endurance's bookkeeping, turned into a map keyed by rule, with Obsidian
	 * Sarcophagi its second key. POINTS BEFORE MULTIPLIERS, as ruled: a coffin's bonus under a trial run out
	 * is (own + 15) x 2. The creature's own figure is its base the first time any rule writes it. A
	 * recompute that puts its own figure back is written over again; any other change to the base is
	 * another writer's, kept, and its own figure moves by that much, so it is not multiplied twice.
	 *
	 * PUBLIC FOR THE CONTROL TEST, which drives it directly.
	 */
	void SetRuleResistance(ACataclysmEnemyCharacter* Creature, const TCHAR* Source, float Added, float Multiplier);

	/** The keys of the rule resistance record, one per rule that changes a creature's all-resistance. */
	static constexpr const TCHAR* TrialOfEnduranceResistanceSource = TEXT("TrialOfEndurance");
	static constexpr const TCHAR* ObsidianSarcophagiResistanceSource = TEXT("ObsidianSarcophagi");

	/** Obsidian Sarcophagi, for the panel and tests: the coffins on this floor or arena. */
	TArray<ACataclysmEnemyCharacter*> SarcophagiNow() const;

	/** Obsidian Sarcophagi, for tests: the zone drawn around `Coffin`, or null. */
	class ACataclysmGroundZone* SarcophagusZoneOf(const ACataclysmEnemyCharacter* Coffin) const;

	/** Obsidian Sarcophagi, for tests: the paid deaths counted beside `Coffin`, and whether its lord came. */
	int32 SarcophagusDeathsBeside(const ACataclysmEnemyCharacter* Coffin) const;
	bool SarcophagusLordCame(const ACataclysmEnemyCharacter* Coffin) const;

	/** The health a coffin is given: the Imp's at Common, 87, as the other floor sources have. */
	float SarcophagusHealth() const { return ImpHealth; }

	/** Golden Spires, for the panel and tests: the spires still standing. */
	TArray<ACataclysmEnemyCharacter*> GoldenSpiresStanding() const;

	/** Golden Spires, for tests: the zone drawn around `Spire`, or null. */
	class ACataclysmGroundZone* GoldenSpireZoneOf(const ACataclysmEnemyCharacter* Spire) const;

	/** The health a spire is given: the Imp's at Common, 87, as the other floor sources have. A play-test value. */
	float GoldenSpireHealth() const { return ImpHealth; }

	/** Pestilent Empowerment, for the panel and tests: this floor's beacons still standing. */
	TArray<ACataclysmEnemyCharacter*> PlagueBeaconsStanding() const;

	/** Portal Unleashing, for the panel and tests: the portals on this floor or arena. */
	TArray<ACataclysmEnemyCharacter*> VoidPortalsNow() const;

	/** Portal Unleashing, for tests: the zone drawn around `Portal`, or null. */
	class ACataclysmGroundZone* VoidPortalZoneOf(const ACataclysmEnemyCharacter* Portal) const;

	/** Portal Unleashing, for the panel and tests: the creatures `Portal` sent that still stand. */
	TArray<ACataclysmEnemyCharacter*> AbominationsOf(const ACataclysmEnemyCharacter* Portal) const;

	/** The health a portal is given: the Imp's at Common, 87, as the other floor sources have. */
	float VoidPortalHealth() const { return ImpHealth; }

	/** Mind-Shattering Illusions, for the panel and tests: the phantasms that still stand. */
	TArray<ACataclysmEnemyCharacter*> PhantasmsStanding() const;

	/**
	 * Reality Rifts, for tests: this arena's rift cells. The first `RealityRiftPairs` pairs are paired in order, 0 with
	 * 1 and 2 with 3; the last is the gift rift.
	 */
	const TArray<FIntPoint>& RealityRiftCellsNow() const { return RealityRiftCells; }

	/** Reality Rifts, for tests: whether the gift rift has been used on this arena. */
	bool RealityGiftTaken() const { return bRealityGiftTaken; }

	/** Luxury Hoarders, for tests: where this floor's hoards lie. */
	const TArray<FVector>& LuxuryHoardsNow() const { return LuxuryHoards; }

	/** Luxury Hoarders, for the panel and tests: the hoards' guards that still stand. */
	TArray<ACataclysmEnemyCharacter*> LuxuryHoardGuardsStanding() const;

	/** Luxury Hoarders, for tests: how many drops the piles were laid with. */
	int32 LuxuryHoardDropsLaid() const { return LuxuryHoardDrops; }

	/** Funereal Procession, for tests: the procession crossing now, or null. */
	class ACataclysmGroundZone* FunerealProcessionNow() const;

	/** Funereal Procession, for tests: where it walks, in centimetres a second. */
	FVector FunerealProcessionVelocityNow() const { return FunerealProcessionVelocity; }

	/** Blood Debt, for the panel and tests: the kills paid toward the debt in this dungeon. */
	int32 BloodDebtPaidHeld() const { return BloodDebtPaid; }

	/** Blood Debt, for the panel and tests: the kills this dungeon owes. */
	int32 BloodDebtOwed() const;

	/** Quarantine Breach, for tests: this floor's containment, or null once broken or on a floor with none. */
	ACataclysmEnemyCharacter* QuarantineNow() const;

	/** Quarantine Breach, for tests: the creatures it released that still stand. */
	TArray<ACataclysmEnemyCharacter*> QuarantineReleasedStanding() const;

	/** Quarantine Breach, for tests: the patches its released creatures have left. */
	TArray<class ACataclysmGroundZone*> QuarantinePatchesNow() const;

	/** The health a containment is given: the Imp's at Common, 87, as the other floor sources have. */
	float QuarantineHealth() const { return ImpHealth; }

	/** Infection Bloom, for tests: this floor's bloom, or null once destroyed or on a floor with none. */
	ACataclysmEnemyCharacter* InfectionBloomNow() const;

	/** Infection Bloom, for tests: the patches it has spread, which go when it is destroyed. */
	TArray<class ACataclysmGroundZone*> InfectionBloomPatchesNow() const;

	/** Infection Bloom, for tests: the creatures its waves sent that still stand. */
	TArray<ACataclysmEnemyCharacter*> InfectionBloomWaveCreaturesStanding() const;

	/** The health a bloom is given: the Imp's at Common, 87, as the other floor sources have. */
	float InfectionBloomHealth() const { return ImpHealth; }

	/** The Infested Hoard, for the panel and tests: the Infestation stacks the player holds. */
	int32 InfestedHoardStacksHeld() const { return InfestedHoardStacks; }

	/** Abyssal Rifts, for the panel and tests: where this floor's rift stands in its life. */
	enum class ERiftState : uint8
	{
		Waiting,
		Open,
		Closed
	};

	/** Abyssal Rifts, for tests: this floor's rift, or null once it has closed or on a floor with none. */
	ACataclysmEnemyCharacter* AbyssalRiftNow() const { return AbyssalRift.Get(); }

	/** Abyssal Rifts, for tests: the rift's zone, or null before its first beat and once it has closed. */
	class ACataclysmGroundZone* AbyssalRiftZoneNow() const { return AbyssalRiftZone.Get(); }

	/** Abyssal Rifts, for tests: the rift's state. */
	ERiftState AbyssalRiftStateNow() const { return AbyssalRiftState; }

	/** Abyssal Rifts, for the panel and tests: the rifts closed in time this dungeon. */
	int32 AbyssalRiftSuccessesHeld() const { return AbyssalRiftSuccesses; }

	/** Abyssal Rifts, for the panel and tests: the creatures the rift sent that still stand. */
	TArray<ACataclysmEnemyCharacter*> AbyssalRiftCreaturesStanding() const;

	/** The health a rift is given: the Imp's at Common, 87, as the other floor sources have. */
	float AbyssalRiftHealth() const { return ImpHealth; }

	/** Swarm of Locusts, for tests: the swarm on the floor, or null between swarms. */
	class ACataclysmGroundZone* SwarmOfLocustsNow() const { return SwarmOfLocusts.Get(); }

	/** Swarm of Locusts, for tests: whether the swarm on the floor has begun to travel. */
	bool SwarmOfLocustsIsTravelling() const { return bSwarmOfLocustsTravelling; }

	/** Warzone Control Points, for tests: this arena's point cells. */
	const TArray<FIntPoint>& WarzonePointCellsNow() const { return WarzonePointCells; }

	/** Warzone Control Points, for tests and the panel: how many of this floor's points the player holds. */
	int32 WarzonePointsHeld() const;

	/** Warzone Control Points, for tests: the creatures its waves sent that still stand. */
	TArray<ACataclysmEnemyCharacter*> WarzoneAttackersStanding() const;

	/** Warzone Control Points, for tests: the allied soldiers its captured points brought that still stand. */
	TArray<ACataclysmEnemyCharacter*> WarzoneAlliesStanding() const;

	/**
	 * Heaven's Quake's pillars and Cryptquake's pits on this floor, standing or still a warning. Issues #1820 and #41.
	 */
	TArray<ACataclysmFloorObstacle*> FloorObstaclesNow() const;

	/** How many of Heaven's Quake's pillars this floor has raised. Counts against `HeavensQuakeMostPillars`. */
	int32 HeavensQuakePillarsRaised() const { return HeavensQuakePillars; }

	/** How many of Cryptquake's sections this floor has collapsed. Counts against `CryptquakeMostSections`. */
	int32 CryptquakeSectionsCollapsed() const { return CryptquakeSections; }

	/**
	 * Every cell this floor still holds a use for, which no obstacle may close (`CataclysmFloorCanBlock`'s `Held`).
	 * Issues #1820 and #41.
	 *
	 * SOME OF THE GAME MODE KEEPS CELLS CHOSEN EARLIER AND USES THEM LATER WITHOUT ASKING THE PLAN AGAIN, and
	 * `SpawnPlacedCreature` puts a creature on whatever cell it is handed. Closing one of those cells would put a
	 * creature, the player or a zone into an obstacle. So they are refused here, not checked where they are used:
	 * - the entrance and the exit, and every living creature, floor object, ground zone and the stairs, from the world;
	 * - a Horde wave still arriving (`WaveStillToArrive`, `ArrivingPackSites`), and where Morale Break's fled return
	 *   (`MoraleBreakGroups`, and every creature's `PackMiddleCell`, which a group's middle is taken from);
	 * - where Infernal Rain's falling fireballs will land and leave a patch (`InfernalRainFalls`);
	 * - Reality Rifts' cells, which the player is teleported to; Infested Veins' cells, where a vein regrows; Divine
	 *   Resurgence's graves, where the dead rise;
	 * - the cells zones are redrawn on: Warzone's points, the locust shelters, the shadow lights, the Void Parasite's
	 *   light, Raw Sewage's marks and the Infection Bloom's patches.
	 * A LIST ADDED LATER THAT KEEPS CELLS FOR LATER USE BELONGS HERE TOO, or an obstacle can close one of its cells.
	 * `tools/tests/test_cells_the_floor_holds_names_every_kept_cell.py` fails until it is named here or, with a
	 * reason, in that file's list of the harmless.
	 * An obstacle still in its warning is NOT here: see `ChooseObstacleCells`.
	 */
	TSet<FIntPoint> CellsTheFloorHolds() const;

	/**
	 * Put the warning for an obstacle on these cells now, as the rule does on its cadence. Null when the placement
	 * rule refuses them. Public for a test that must choose the cells.
	 */
	ACataclysmFloorObstacle* WarnOfAnObstacle(const TArray<FIntPoint>& Cells, ECataclysmObstacleKind Kind,
											  FName RowKey);

	/** Remove every obstacle and warning, give their cells back and start the rules' clocks again. */
	void EndTheFloorObstacles();

	// ----------------------------------------------------------------------
	// The gated shortcuts: Warzone's shortcuts, Soul Chains and The Labrynth. Issues #1820 and #41. Ruled 2026-10-04.
	// ----------------------------------------------------------------------

	/** The shortcuts this floor carved for a row, in the order the row uses them. Empty on a floor with none. */
	TArray<FCataclysmFloorShortcut> GatedShortcutsOf(FName RowKey) const;

	/** Whether a row's shortcut is open. False for one that does not exist. */
	bool GatedShortcutIsOpen(FName RowKey, int32 Index) const;

	/** What a row's shortcut leads to, as the floor panel says it: "to the exit", "to the entrance" or "nearby". */
	FString GatedShortcutLeadsTo(FName RowKey, int32 Index) const;

	/** True when the floor carries a gate row and its shape allows no gates: not Halls, or a Horde arena. */
	bool GateRowsHaveNoShape() const { return bGateRowsHaveNoShape; }

	/** The living creatures that hold a Soul Chains gate shut. */
	TArray<ACataclysmEnemyCharacter*> SoulChainBearersNow(int32 Gate) const;

	/** How many drops Soul Chains' freed gates have given on this floor. */
	int32 SoulChainsRewardDropsSpawned() const { return SoulChainsRewardDrops; }

	/** How many reward rolls Soul Chains has made on this floor: one for each gate its bearers' deaths opened. */
	int32 SoulChainsRewardRollsMade() const { return SoulChainsRewardRolls; }

	/** Warzone's points as chosen on the floor's seed before it was built, on a Halls floor. Empty elsewhere. */
	const TArray<FIntPoint>& WarzonePlannedPointsNow() const { return WarzonePlannedPoints; }

	/** Swarm of Locusts, for tests: this arena's shelters, drawn from its first beat. */
	TArray<class ACataclysmGroundZone*> LocustSheltersNow() const;

	/** Raw Sewage, for the panel and tests: the disease stacks the player carries. */
	int32 RawSewageStacksHeld() const { return RawSewageStacks; }

	/** Raw Sewage, for tests: the marks of this floor's rivers, drawn from its first beat. */
	TArray<class ACataclysmGroundZone*> RawSewageMarksNow() const;

	/** Pestilent Empowerment, for the panel and tests: beacons left standing on this dungeon's earlier floors. */
	int32 PlagueBeaconsLeftStanding() const { return PestilentBeaconsLeftStanding; }

	/** The health a beacon is given: the Imp's at Common, 87, as the other floor sources have. A play-test value. */
	float PlagueBeaconHealth() const { return ImpHealth; }

	/**
	 * Whether this creature is one of the floor's own and still stands: valid, not dead, not raised by a rule by either
	 * of the two marks a rule leaves, able to be hurt, and not a player's follower. Issue #2194, ruled 2026-10-01: the
	 * Reaper, a Blood Bond's elite and every rule's arrivals are not the floor's own. Shared by `LivingFloorEnemies` and
	 * `LightforgedWallsStanding`, so "cleared" and "every creature the floor placed" cannot drift apart.
	 */
	bool IsOneOfTheFloorsOwnStanding(ACataclysmEnemyCharacter* Creature) const;

	/**
	 * How many of the floor's own creatures still stand, by `IsOneOfTheFloorsOwnStanding`, plus Morale Break's escaped,
	 * who hold the floor while they are away. The floor sources are not in `FloorEnemies`, so they are never counted.
	 * Issues #1820, #41 and #2194.
	 */
	int32 LivingFloorEnemies() const;

	/**
	 * Whether the floor is cleared: none of its own creatures standing and none escaped. What the floors-cleared count
	 * and Trial of Endurance read. Issue #2194.
	 */
	bool FloorIsCleared() const { return LivingFloorEnemies() == 0; }

	/** Seconds since this floor or wave was placed, on the beat. */
	float SecondsOnThisFloor() const { return FloorSecondsSincePlaced; }

	/**
	 * The seconds after placing at which this floor or wave was first found cleared, or -1 while it has not
	 * been. The same moment is logged, with the floor's number and cell count, whether or not any rule is
	 * on, so the time Trial of Endurance allows can be tuned from play. Issues #1820 and #41.
	 */
	float FloorClearedAfterSeconds() const { return FloorClearedSeconds; }

	/** Trial of Endurance, for the panel and tests: its clock, and whether it was cleared in time or ran out. */
	float TrialOfEnduranceSecondsSoFar() const { return TrialSeconds; }
	bool TrialOfEnduranceClearedInTime() const { return bTrialClearedInTime; }
	bool TrialOfEnduranceRanOut() const { return bTrialRanOut; }

	/**
	 * The vision system: how far the player sees on this floor, in centimetres, or 0 for unlimited, as last worked out
	 * on the beat from the rows in force. A creature further than this from the player is hidden. Issues #1820 and #41.
	 */
	float PlayerSightRadiusCm() const { return PlayerSightRadius; }

	/** The Blackest Shadow, for tests: whether this creature carries the Invisible Stalker buff now. */
	bool IsAnInvisibleStalker(const ACataclysmEnemyCharacter* Creature) const;

	/**
	 * The floor's edge cells farthest from `From`, at most `Count` of them, the farthest first: a
	 * floor cell with a side on rock or off the grid. Where Plague Convergence's waves arrive.
	 */
	static TArray<FIntPoint> ConvergenceArrivalCells(const struct FCataclysmFloorPlan& Plan,
												   FIntPoint From, int32 Count);

	/** Chaos Touched's stacks of one kind, for the floor panel and tests. Issues #1820, #41. */
	int32 ChaosTouchedStacksOf(int32 Kind) const
	{
		return ChaosTouchedStacks.IsValidIndex(Kind) ? ChaosTouchedStacks[Kind] : 0;
	}

	/**
	 * Whether this death was a floor's boss: a Gatekeeper, the creature the game places as a
	 * floor's boss, or any creature at the Boss rung. `IsBoss()` alone asks the rung, which
	 * every creature draws, the Gatekeeper included, so on its own it is a 1% draw.
	 */
	static bool DiedAsAFloorsBoss(const AActor* Died);

private:
	/** Unstable Portal's mini-boss: an Abyssal Warden beside the stairs at the mini-boss rung. */
	void RaiseTheUnstablePortalsWarden();

	/** Nothing Is Forgotten, on every death: the player's kill feeds the void. */
	void NoteDeathForNothingIsForgotten(const struct FCataclysmDeathNotice& Notice);

	/**
	 * The void's figures on the boss it fed, and on nothing else. Written at the feeding
	 * and again after any rung change, which rewrites the creature's whole stat block.
	 */
	void ApplyNothingIsForgottenFigures(ACataclysmEnemyCharacter* Creature);

	/**
	 * Whether this is the dungeon's final floor, whose exit holds its final boss: at or past
	 * the dungeon's floor count, when it has more than one floor. The same test
	 * `FCataclysmDungeonFloorRules::BossAtTheExit` makes of depth.
	 */
	bool IsTheFinalFloorForItsBoss() const;

	/** The starvation curse, as a floor carrying it begins: one more stack of one kind. */
	void AddAStarvationCurse();

	/**
	 * The starvation curse, on the beat: puts the stacks back on the player after a floor
	 * change took them off. Runs on any floor while stacks are held, because the stacks are
	 * the dungeon's and outlast floors that do not carry the row.
	 */
	void StepStarvationCurse(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** The starvation curse, on every death: a floor's boss or the player clears it. */
	void NoteDeathForStarvationCurse(const struct FCataclysmDeathNotice& Notice);

	/** Trick or Treat, on every take: a clicked drop on a floor carrying the row rolls. */
	void OnLootTaken(const struct FCataclysmLootTakenNotice& Notice);

	/** A skill used anywhere on the floor. Wild Magic's roll. Issues #1820 and #41. */
	void OnSkillWasUsed(const struct FCataclysmSkillUsedNotice& Notice);

	/** Echo Chamber's half of a skill use: draws the copy that is fired on the next tick. Issues #1820 and #41. */
	void NoteSkillUseForEchoChamber(const struct FCataclysmSkillUsedNotice& Notice);

	/**
	 * A cleanse, on every character cleansed: when it is the player, the dungeon stacks whose rows say they are
	 * cleansed are cleared -- Raw Sewage's, the Starvation Curse's, and Chaos Touched's debuff kinds. The rest are
	 * kept, as their rows name another remedy or none. Ruled 2026-09-26.
	 */
	void OnSomethingWasCleansed(AActor* Character);

	/** Trick or Treat's trick: two creatures of the floor's kinds where the drop lay. */
	void RaiseTheTrickOrTreatPair(const FVector& Where);

	/** Chaos Touched, as a floor carrying it begins: one more stack of one kind. */
	void AddAChaosTouch();

	/** Chaos Touched, on the beat: its stacks back on the player after a floor change. */
	void StepChaosTouched(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** Chaos Touched, on every death: a floor's boss cleanses the debuffs, the player all. */
	void NoteDeathForChaosTouched(const struct FCataclysmDeathNotice& Notice);

	/** The Reaper, on the beat: counts the floor's seconds and raises it when due. */
	void StepTheReaper();

	/** The Reaper itself: an Abyssal Warden at the entrance that cannot be hurt. */
	void RaiseTheReaper();

	/** The Reaper, on every blow: one of its that lands on the player kills them. */
	void NoteHitForTheReaper(const struct FCataclysmHitNotice& Notice);

	/** Blood Bond, on the beat: the first elite that notices the player takes the bond. */
	void StepBloodBond(class ACataclysmPlayerCharacter* Player);

	/**
	 * The vision system, on the beat: the sight worked out from the rows in force, every creature beyond it hidden and
	 * every other shown, and the camera darkened while sight is limited.
	 */
	void StepVision(class ACataclysmPlayerCharacter* Player);

	/**
	 * The vision system: the player's sight as last worked out, and the creatures it hid, so only those are shown
	 * again. Issues #1820 and #41.
	 */
	float PlayerSightRadius = 0.0f;
	TSet<TWeakObjectPtr<ACataclysmEnemyCharacter>> HiddenBySight;

	/**
	 * The Blackest Shadow: the creatures carrying the Invisible Stalker buff now, so only those have it taken off again.
	 * Issues #1820 and #41.
	 */
	TSet<TWeakObjectPtr<ACataclysmEnemyCharacter>> InvisibleStalkers;

	/**
	 * Shadowy Enemies: this floor's light zones' cells, the exit's last when a boss stands there; the zones drawn on
	 * them; the creatures shrouded now, so only those are released; and the seconds each fire-hit creature stays
	 * exposed. Issues #1820 and #41.
	 */
	TArray<FIntPoint> ShadowLightCells;
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> ShadowLights;
	TSet<TWeakObjectPtr<ACataclysmEnemyCharacter>> ShroudedCreatures;
	TMap<TWeakObjectPtr<ACataclysmEnemyCharacter>, float> ShadowFireSecondsLeft;

	/**
	 * Shadowy Enemies, on the beat: the light zones kept drawn, and every floor creature shrouded or exposed as the
	 * lights, The Blackest Shadow's light and its fire seconds say; on a floor without the row, every shroud taken off.
	 */
	void StepShadowyEnemies(class ACataclysmPlayerCharacter* Player);

	/** Shadowy Enemies, on every blow: a fire hit on a floor creature exposes it at once, for the row's seconds. */
	void NoteHitForShadowyEnemies(const struct FCataclysmHitNotice& Notice);

	/** Shadowy Enemies: this arena's light cells chosen, where a new arena is populated. Drawn on the next beat. */
	void PlaceTheShadowLights();

	/** Shadowy Enemies: the light zones destroyed and forgotten, and the fire seconds; the floor has ended. */
	void ForgetTheShadowLights();

	/** Blood Bond, on every death: the player's death kills the elite bonded on this floor. */
	void NoteDeathForBloodBond(const struct FCataclysmDeathNotice& Notice);

	/** Plague Convergence, on the beat: the clock, the waves and the disease's burn. */
	void StepPlagueConvergence(class ACataclysmPlayerCharacter* Player,
							   class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** Plague Convergence, on every blow: one of its creatures' landed blows adds a stack. */
	void NoteHitForPlagueConvergence(const struct FCataclysmHitNotice& Notice);

	/** Plague Convergence, on every death: the player's own clears the disease. */
	void NoteDeathForPlagueConvergence(const struct FCataclysmDeathNotice& Notice);

	/** Divine Wrath, on the beat: a beam when one is due, aimed at the player, killing creatures. */
	/** Echoes of the Past, on every death: a creature's kind, rung and last attack are kept. */
	void NoteDeathForEchoesOfThePast(const struct FCataclysmDeathNotice& Notice);

	/** Echoes of the Past, on the beat: the last floor's dead appear, strike once and go. */
	void StepEchoesOfThePast(class ACataclysmPlayerCharacter* Player);

	/** Every echo standing is destroyed and forgotten. */
	void DismissTheEchoes();

	/** Plague Harbingers, on the beat: trails laid, the player burned by them, creatures in them empowered. */
	void StepPlagueHarbingers(class ACataclysmPlayerCharacter* Player,
							  class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** Plague Harbingers, on every death: a Harbinger's trail goes and those near it are weakened. */
	void NoteDeathForPlagueHarbingers(const struct FCataclysmDeathNotice& Notice);

	/** Every Harbinger unmarked and every trail forgotten; the zones go with the floor. */
	void ForgetThePlagueHarbingers();

	/** Wings of the Host, on the beat: a flyover marked every thirty seconds, landing three later. */
	void StepWingsOfTheHost(class ACataclysmPlayerCharacter* Player);

	/** Eternal Chorus: this arena's sources, placed where a new arena is populated. */
	void PlaceTheChoruses();

	/** Every chorus source destroyed and forgotten. */
	void ForgetTheChoruses();

	/** Necrotic Bloom: this arena's flowers, placed where a new arena is populated. */
	void PlaceTheBlooms();

	/** Every flower destroyed and forgotten. Their waves' creatures are the floor's and go with it. */
	void ForgetTheBlooms();

	/** Necrotic Bloom, on the beat: each living flower's clock, and its wave when one is due. */
	void StepNecroticBloom();

	/** Golden Spires: this arena's spires, placed where a new arena is populated. */
	void PlaceTheSpires();

	/** Every spire and its zone destroyed and forgotten. */
	void ForgetTheSpires();

	/** Pestilent Empowerment: this arena's beacons, placed where a new arena is populated. */
	void PlaceTheBeacons();

	/** Every beacon destroyed and forgotten. The count carried from earlier floors is not touched. */
	void ForgetTheBeacons();

	/**
	 * Pestilent Empowerment: adds every beacon still standing and not yet counted to the count carried
	 * to later floors, and marks it counted. Called as the player leaves a floor or a Horde wave.
	 */
	void CountThePlagueBeaconsLeftStanding();

	/** Pestilent Empowerment, on the beat: every creature's multiplier for the count carried to this floor. */
	void StepPestilentEmpowerment(class ACataclysmPlayerCharacter* Player);

	/** Portal Unleashing: this arena's portals, placed where a new arena is populated. */
	void PlaceThePortals();

	/** Reality Rifts: this arena's rifts chosen, where a new arena is populated. Drawn on the next beat. */
	void PlaceTheRealityRifts();

	/** Reality Rifts, on the beat: the rifts drawn, a step into one answered, and the gift written on the player. */
	void StepRealityRifts(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** Luxury Hoarders: this arena's hoards, their piles and guards, placed where a new arena is populated. */
	void PlaceTheHoards();

	/** The hoards' record forgotten. Their drops lie where they are, and their guards are the floor's. */
	void ForgetTheHoards();

	/** Luxury Hoarders, on a death: a guard's death updates the panel. */
	void NoteDeathForLuxuryHoarders(const struct FCataclysmDeathNotice& Notice);

	/** Insanity Bursts, on the beat: its clock, its warning, the burst, and the lock written on the player. */
	void StepInsanityBursts(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Insanity Bursts: whether a living minion of this player's stands, an `ACataclysmMinion` whose owner chain reaches
	 * them, summons and deployables alike. A madness burst is drawn only then.
	 */
	bool PlayerHasALivingMinion(const AActor* Player) const;

	/** Funereal Procession, on the beat: a procession set out on its clock, its contact, and its end. */
	void StepFunerealProcession(class ACataclysmPlayerCharacter* Player,
								class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** Quarantine Breach: this arena's containment, placed where a new arena is populated, holding one kind. */
	void PlaceTheQuarantine();

	/** The containment, what it released and their patches destroyed and forgotten. */
	void ForgetTheQuarantine();

	/**
	 * The kind the containment holds, as the creature panel names it ("Abyssal Warden"), from the kind's class's
	 * archetype row; the populator's own name for it when the table cannot say.
	 */
	FString QuarantineHeldName() const;

	/** Quarantine Breach, on a death: the containment's releases its group; a released creature's leaves a patch. */
	void NoteDeathForQuarantineBreach(const struct FCataclysmDeathNotice& Notice);

	/** Infection Bloom: this arena's bloom, placed where a new arena is populated. */
	void PlaceTheBloom();

	/** The bloom, its patches and the waves' record destroyed and forgotten. */
	void ForgetTheInfectionBloom();

	/** Infection Bloom, on the beat: the patches drawn and spread, the damage they give, and the waves. */
	void StepInfectionBloom(class ACataclysmPlayerCharacter* Player);

	/** Infection Bloom, on a death: the bloom's own ends its spread and sends the surge. */
	void NoteDeathForInfectionBloom(const struct FCataclysmDeathNotice& Notice);

	/** `Count` of the floor's own kinds beside `Where`, at Common; the floor's creatures when `bTheFloors`. */
	TArray<ACataclysmEnemyCharacter*> InfectionBloomSend(const FVector& Where, int32 Count, bool bTheFloors);

	/** Abyssal Rifts: this floor's rift, placed where a new arena is populated. None on a Horde arena. */
	void PlaceTheRift();

	/** The rift and its zone destroyed and forgotten. Its creatures are the floor's and stay; the successes stay. */
	void ForgetTheRift();

	/** The rift closes: destroyed with its zone, with a success or without. */
	void CloseTheRift(bool bInTime);

	/**
	 * Abyssal Rifts, on the beat: the reward written on the player when the successes changed, the rift's zone kept
	 * drawn, the rift opening when the player comes near, its waves, and its closing in time or not.
	 */
	void StepAbyssalRifts(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** Abyssal Rifts: the player's death ends the successes and their magic find. */
	void NoteDeathForAbyssalRifts(const struct FCataclysmDeathNotice& Notice);

	/** Swarm of Locusts: this arena's shelters chosen, where a new arena is populated. Drawn on the next beat. */
	void PlaceTheShelters();

	/** Every shelter and any swarm destroyed and forgotten, and the swarm's clock started again. */
	void ForgetTheLocusts();

	/**
	 * Swarm of Locusts, on the beat: the shelters kept drawn, a swarm when one is due, its warning and its travel,
	 * the burn once a second for a player it covers who is in no shelter, and the swarm gone when it has crossed.
	 */
	void StepSwarmOfLocusts(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** Warzone Control Points: this arena's points chosen, where a new arena is populated. Drawn on the next beat. */
	void PlaceTheControlPoints();

	/** Warzone Control Points: the zones, the counts, the captures and the wave clock forgotten; the cells kept. */
	void ForgetTheWarzoneHold();

	/**
	 * Warzone Control Points, on the beat: the points drawn, the count for the one the player stands in, a wave when
	 * one is due, a capture, and the strength of the points held written on the player.
	 */
	void StepWarzoneControlPoints(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** Warzone Control Points: a wave near this point; returns how many creatures came. */
	int32 SendWarzoneWave(const FVector& Point);

	/** Warzone Control Points: a captured point's allied soldiers beside it, on the player's side; how many came. */
	int32 BringWarzoneAllies(const FVector& Point, class ACataclysmPlayerCharacter* Player);

	/** Warzone Control Points: every allied soldier the points brought removed. At the floor change and with the hold. */
	void EndTheWarzoneAllies();

	/** Raw Sewage: this arena's rivers chosen, where a new arena is populated. Drawn on the next beat. */
	void PlaceTheRivers();

	/** Every river mark destroyed and forgotten. The stacks are the dungeon's and are not touched. */
	void ForgetTheRivers();

	/**
	 * Raw Sewage, on the beat: the rivers kept drawn, a stack for entering one and one each further
	 * `RawSewageSecondsPerStack` in it, the burn once a second on any floor while a stack is held, and the disease
	 * tag held while any is.
	 */
	void StepRawSewage(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** Raw Sewage: a floor's boss's death, or the player's, clears every stack. */
	void NoteDeathForRawSewage(const struct FCataclysmDeathNotice& Notice);

	/** Demonic Guide: this arena's guide raised at the entrance, where a new arena is populated. */
	void PlaceTheGuide();

	/** Demonic Guide: the guide and its chain destroyed and forgotten. */
	void ForgetTheGuide();

	/**
	 * Demonic Guide, on the beat: the guide sent to the exit or told to wait, its chain drawn around it, and the damage
	 * a player beyond the chain takes written on the player.
	 */
	void StepDemonicGuide(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** The Plaguebearer's beat: its flight, the stacks, and every other floor creature's multiplier. Issue #41. */
	void StepPlaguebearer(class ACataclysmPlayerCharacter* Player);

	/** Morale Break's beat: a leader's death, the flight, the escape and the return. Issues #1820 and #41. */
	void StepMoraleBreak(class ACataclysmPlayerCharacter* Player);

	/**
	 * Writes the floor population's group onto a creature it placed: `PackGroup` and `PackMiddleCell`. Called only where
	 * the floor's own population is spawned, all at once or as a wave. Issues #1820 and #41, for Morale Break.
	 */
	void NoteThePack(ACataclysmEnemyCharacter* Enemy, const FCataclysmEnemyPlacement& Placement) const;

	/** Famished Beasts' beat: which creatures seek drops, and each one standing on a drop eats it. Issue #41. */
	void StepFamishedBeasts();

	/** Puts back, on every thrall a player commands, each value a floor rule set on it. Issue #1202. */
	void StepPlayersFollowers();

	/**
	 * Writes back onto the player, at once, every dungeon-long rule that changes its maximum health, so the floor's
	 * start reads the maximum they leave. Issue #2190; the reason and the guard are on the definition.
	 */
	void WriteTheMaximumHealthRulesBack();

	/**
	 * Every caller's way to apply the floor's rules to the player: `ApplyFloorRulesToPlayer`, then
	 * `WriteTheMaximumHealthRulesBack`, then health restored to the lower of what it was and the maximum they leave.
	 * Issue #2190; the reason is on the definition.
	 */
	void ApplyFloorRulesKeepingHealth();

	/**
	 * Famished Beasts: an eater's damage and maximum health for the drops it has eaten. Issues #1820 and #41.
	 * `bFreshBlock` after a rung change has written its whole stat block over, as Soul Harvest's is: nothing this rule
	 * added is still on it.
	 */
	void StrengthenTheEater(ACataclysmEnemyCharacter* Eater, bool bFreshBlock = false);

	/**
	 * After a rung change has written a creature's whole stat block over, put the floor rules' health shares back on
	 * the new rung's maximum: a Carrion feeder's, with Carrion Feast's own record taken again from that maximum, and
	 * the drops it ate for Famished Beasts. Called by Volatile Evolution and Blood-Forged Champions right after the
	 * rung is written, before the pools are held to the new maximum. Issues #1820 and #41.
	 */
	void PutTheHealthSharesBack(ACataclysmEnemyCharacter* Creature);

	/** Every portal, its zone and every creature it sent destroyed and forgotten. */
	void ForgetThePortals();

	/**
	 * Portal Unleashing, on the beat: each portal's zone kept drawn, and one creature sent by each portal whose
	 * clock has run and whose own creatures standing are under the cap.
	 */
	void StepPortalUnleashing();

	/**
	 * The cells `EternalChorusCells` and `InfestedVeinsCells` share: every floor cell at least
	 * `EternalChorusApartCm` from the entrance -- beside a wall as well when asked -- in an even shuffle,
	 * then the first that are that far from every one already taken.
	 */
	static TArray<FIntPoint> FloorSourceCells(const ACataclysmDungeonFloor& Floor, int32 Count, bool bBesideAWallOnly);

	/** Infested Veins: this arena's veins, placed where a new arena is populated, and its count reset. */
	void PlaceTheVeins();

	/** Every vein and its zone destroyed and forgotten. */
	void ForgetTheVeins();

	/** Void Parasite: this arena's light zone chosen, where a new arena is populated. Drawn on the next beat. */
	void PlaceTheLight();

	/**
	 * Floor objects of one rule on this floor: up to `Count`, on Eternal Chorus's cells -- away from the entrance, so
	 * the player walks to one -- each carrying the rule's key, the name and the prompt given. The caller gives each its
	 * choices and keeps them. Issues #1820 and #41. Taken out of Grim Totems so every rule that places a floor object
	 * places it the one way.
	 */
	TArray<class ACataclysmFloorObject*> PlaceFloorObjects(FName RuleKey, int32 Count, const FString& DisplayName,
														   const FString& Prompt);

	/**
	 * Up to `Count` creatures of the floor's own kinds, on cells beside a point `AwayCm` from `At` at a random angle --
	 * or around `At` when that point has no floor within reach, or on the floor cells nearest `At` when it has none
	 * either, which is a collapsed pit (issue #2219) -- at `FixedRung`, or a rung drawn as usual when it is
	 * -1, noticing the player from `SightMultiplier` times the ordinary distance. Each is raised by a rule and is one of
	 * the floor's creatures, paying as its rung does. Issues #1820 and #41. Taken out of Grim Totems' embrace so every
	 * rule that brings creatures after the player brings them the one way.
	 */
	TArray<ACataclysmEnemyCharacter*> BringCreaturesNear(const FVector& At, float AwayCm, int32 Count, int32 FixedRung,
														 float SightMultiplier);

	/** Grim Totems: this arena's totems placed, where a new arena is populated. */
	void PlaceTheTotems();

	/** Battlefield Relics: this arena's relics placed, where a new arena is populated. Issues #1820 and #41. */
	void PlaceTheRelics();

	/** Battlefield Relics: every relic destroyed and forgotten, and the spirits forgotten. */
	void ForgetTheRelics();

	/** Battlefield Relics: the one choice at a relic. */
	bool ChooseAtBattlefieldRelic(class ACataclysmFloorObject* Relic, FName ChoiceKey);

	/** Battlefield Relics, on the beat: each kind's buff written when it changed and counted down. */
	void StepBattlefieldRelics(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** Pandora's Box: this arena's boxes placed, where a new arena is populated. Issues #1820 and #41. */
	void PlaceTheBoxes();

	/** Pandora's Box: every box destroyed and forgotten, and every wave under way forgotten. */
	void ForgetTheBoxes();

	/** Pandora's Box: the one choice at a box. */
	bool ChooseAtPandorasBox(class ACataclysmFloorObject* Box, FName ChoiceKey);

	/** Pandora's Box, on the beat: each box's next wave, once the last is all dead. */
	void StepPandorasBox();

	/** Carrion Feast's purification altar: this arena's altar placed, where a new arena is populated. #1820, #41. */
	void PlaceTheAltar();

	/** Carrion Feast's purification altar: the altar and its zone destroyed, and the consecration forgotten. */
	void ForgetTheAltar();

	/** Carrion Feast's purification altar: the one choice at it. */
	bool ChooseAtPurificationAltar(class ACataclysmFloorObject* Altar, FName ChoiceKey);

	/** Infernal Beacons: this arena's beacon placed, where a new arena is populated. Issues #1820 and #41. */
	void PlaceTheInfernalBeacons();

	/** Infernal Beacons: every beacon standing destroyed and forgotten; the dungeon's stacks are kept. */
	void ForgetTheInfernalBeacons();

	/** Infernal Beacons: the one choice at a beacon. */
	bool ChooseAtInfernalBeacon(class ACataclysmFloorObject* Beacon, FName ChoiceKey);

	/** War Banner: this arena's banner placed, where a new arena is populated, and the last one's forgotten. #1820, #41. */
	void PlaceTheWarBanner();

	/** War Banner: the banner, its zone and its hold forgotten. */
	void ForgetTheWarBanner();

	/** War Banner: the one choice at the banner, which plants it where it stands and brings the first wave. */
	bool ChooseAtWarBanner(class ACataclysmFloorObject* Banner, FName ChoiceKey);

	/** War Banner: one wave of assailants brought near the planted banner. */
	void BringABannerWave();

	/** War Banner, on the beat: the zone drawn, the hold counted, the waves brought and the aura written. */
	void StepWarBanner(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Fragmented Reality, on the beat: whether the player stands in the Fragmented section, the draw made on the
	 * beat they are first found inside, and the pair written on the player when it changed. Issues #1820 and #41.
	 */
	void StepFragmentedReality(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Forced Tithes, on every floor and every wave: angels owed if the last altar was left unpaid, the last altar
	 * forgotten, and this floor's placed at the exit unless it is the dungeon's last. Issues #1820 and #41.
	 */
	void PlaceTheTitheAltar();

	/** Forced Tithes: the altar and this floor's tithe forgotten. The angels owed are not touched. */
	void ForgetTheTitheAltar();

	/** Forced Tithes: a choice at the altar, a price paid or the tithe refused. */
	bool ChooseAtTitheAltar(class ACataclysmFloorObject* Altar, FName ChoiceKey);

	/**
	 * Forced Tithes: whether the player can pay this price now, and what paying takes -- the potion slot or the
	 * material. False for a key that is not a price.
	 */
	bool CanPayTheTithe(FName ChoiceKey, class ACataclysmPlayerCharacter* Player, int32* OutPotionSlot,
						FName* OutMaterial) const;

	/** Forced Tithes: the angels brought near this point. */
	void BringTheTitheAngels(const FVector& At);

	/** Forced Tithes, on the beat: owed angels brought to the entrance, and the altar's prices shown as payable or not. */
	void StepForcedTithes(class ACataclysmPlayerCharacter* Player);

	/**
	 * Pact of Temptation, on every floor and every wave: last floor's accepted pact becomes this floor's buff, the last
	 * altar goes, and this floor's is placed at the exit offering pacts unless it is the dungeon's last. #1820, #41.
	 */
	void PlaceThePactAltar();

	/** Pact of Temptation: the altar and its offer forgotten. The buffs and curses are not touched. */
	void ForgetThePactAltar();

	/** Pact of Temptation: a pact accepted at the altar. */
	bool ChooseAtPactAltar(class ACataclysmFloorObject* Altar, FName ChoiceKey);

	/**
	 * Sanctioned Passage, on every floor and every wave: the last gate forgotten, and this floor's placed beside the exit
	 * unless the floor is a Horde floor or the dungeon's last. Issues #1820 and #41.
	 */
	void PlaceTheDivineGate();

	/** Sanctioned Passage: the gate and its channel forgotten, and every creature it called given its own sight back. */
	void ForgetTheDivineGate();

	/** Sanctioned Passage: "Channel" chosen at the gate, which begins the channel once. */
	bool ChooseAtDivineGate(class ACataclysmFloorObject* Gate, FName ChoiceKey);

	/** Sanctioned Passage, on the beat: the channel counted within reach, and every creature called while it lasts. */
	void StepSanctionedPassage(class ACataclysmPlayerCharacter* Player);

	/** Sanctioned Passage: every creature the channel called given its own sight back. */
	void SendBackTheCalled();

	/** Pact of Temptation, on the beat: Greed's curse on every creature, and the buff and curses on the player. */
	void StepPactOfTemptation(class ACataclysmPlayerCharacter* Player,
							  class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * The choice screen's rule half: a choice sent to the rule that placed the object, answering whether anything
	 * happened. `ChooseAtFloorObject` wraps it, so a rule that prices every choice -- Blood Price -- asks once.
	 */
	bool ChooseAtFloorObjectForItsRule(class ACataclysmFloorObject* Object, FName ChoiceKey);

	/** Blood Price: the price of a choice that acted, from the health the player had before it, and a bleed stack. */
	void PayTheBloodPrice(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem,
						  float HealthBefore);

	/** Blood Price, on the beat: the buttons priced, the bleed once a second, and the bleed keyword while it is held. */
	void StepBloodPrice(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** A floor object at this point, carrying the rule's key, the name and the prompt given. Issues #1820 and #41. */
	class ACataclysmFloorObject* PlaceFloorObjectAt(FName RuleKey, const FVector& Where, const FString& DisplayName,
													const FString& Prompt);

	/** Infernal Beacons, on the beat: every creature's damage at the dungeon's stacks, and the player's magic find. */
	void StepInfernalBeacons(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** Carrion Feast: the carcass at this index burned -- marked, and removed on the next beat -- by fire or an altar. */
	void BurnTheCarcass(int32 Index);

	/** Carrion Feast: whether a consecrated altar's area covers this point, measured flat. */
	bool AltarConsecrates(const FVector& Where) const;

	/** Carrion Feast: every carcass lying inside the consecrated area burned; how many. */
	int32 BurnTheConsecratedCarcasses();

	/** Grim Totems: every totem and its zone destroyed and forgotten. */
	void ForgetTheTotems();

	/** Grim Totems: a choice at one of its totems. */
	bool ChooseAtGrimTotem(class ACataclysmFloorObject* Totem, FName ChoiceKey);

	/** Grim Totems, on the beat: the totems' zones drawn, and an embrace's strength written and counted down. */
	void StepGrimTotems(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** Every voidling, the light zone and the player's stacks forgotten: the floor has ended. */
	void ForgetTheVoidParasite();

	/**
	 * Void Parasite, on the beat: the light zone kept drawn, each voidling within reach attached, every stack
	 * cleared in the light, and the player's stats written again when the stacks changed.
	 */
	void StepVoidParasite(class ACataclysmPlayerCharacter* Player,
						  class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** One vein on `Cell`: a floor source raised by the rule. Null when it could not be spawned. */
	ACataclysmEnemyCharacter* SpawnAVeinOn(FIntPoint Cell);

	/**
	 * Infested Veins, on the beat: each living vein's zone kept drawn, a destroyed vein counted and grown
	 * back, the guardians once, and the burn once a second for a player in a living vein's zone.
	 */
	void StepInfestedVeins(class ACataclysmPlayerCharacter* Player,
						   class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** Carrion Feast: every carcass destroyed and forgotten, and the feeders and the carcasses eaten forgotten. */
	void ForgetTheCarrion();

	/** Carrion Feast, on the beat: burned carcasses removed, carcasses eaten at their seconds, and feeders brought. */
	void StepCarrionFeast();

	/** Carrion Feast: a floor creature's death leaves a carcass where it died. */
	void NoteDeathForCarrionFeast(const struct FCataclysmDeathNotice& Notice);

	/** Carrion Feast: a fire hit on a carcass burns it, and it is removed on the next beat. */
	void NoteHitForCarrionFeast(const struct FCataclysmHitNotice& Notice);

	/** Carrion Feast: every feeder standing given the damage and health of the carcasses eaten. */
	void StrengthenTheFeeders();

	/**
	 * On every beat whatever the rules: the seconds on this floor, and the one log line the first time it
	 * is found cleared. Issues #1820 and #41.
	 */
	void NoteTheFloorsClearTime();

	/**
	 * Trial of Endurance, on the beat: its clock until the floor is cleared or it runs out, and once run
	 * out, double damage and twice its own all-resistance on every creature on the player's other side but
	 * a floor source.
	 */
	void StepTrialOfEndurance(class ACataclysmPlayerCharacter* Player);

	/**
	 * Every creature a rule wrote a resistance on given its own back, and the record emptied, once a floor or
	 * wave. A Horde wave's survivors keep standing, so a bonus would otherwise be counted in their own figure
	 * and added again.
	 */
	void ForgetRuleResistances();

	/** Obsidian Sarcophagi: this arena's coffins, placed where a new arena is populated. */
	void PlaceTheSarcophagi();

	/** Every coffin and its zone destroyed and forgotten. */
	void ForgetTheSarcophagi();

	/**
	 * Obsidian Sarcophagi, on the beat: a zone kept drawn around each coffin, and every creature's damage and
	 * resistance written for whether it stands near one.
	 */
	void StepObsidianSarcophagi(class ACataclysmPlayerCharacter* Player);

	/**
	 * Obsidian Sarcophagi: a paid death of the floor's creatures counted beside every coffin within reach of
	 * it, and a coffin's Vampire Lord let out at the threshold.
	 */
	void NoteDeathForObsidianSarcophagi(const struct FCataclysmDeathNotice& Notice);

	/** Mind-Shattering Illusions, on a blow: a phantasm's hit on the player slows them for a moment. */
	void NoteHitForMindShatteringIllusions(const struct FCataclysmHitNotice& Notice);

	/** Mind-Shattering Illusions, on the beat: phantasms on their clock, and the slow counted down and written. */
	void StepMindShatteringIllusions(class ACataclysmPlayerCharacter* Player,
									 class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** The phantasms destroyed and forgotten, with the clock and the slow. */
	void ForgetThePhantasms();

	/**
	 * Blood Debt, on a death: a paying creature's death on a floor carrying the row pays one kill; the player's death
	 * ends the debt and what was paid.
	 */
	void NoteDeathForBloodDebt(const struct FCataclysmDeathNotice& Notice);

	/** Blood Debt, on the beat: the blessing and the curse written on the player when either changed. */
	void StepBloodDebt(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** Blood Debt: the blessings and whether the curse holds, as they are now. */
	int32 BloodDebtBlessingsNow() const;
	bool BloodDebtCursedNow() const;

	/**
	 * The Infested Hoard, on a death: a paying floor creature's may leave one infested drop; the player's ends the
	 * stacks. Issues #1820 and #41.
	 */
	void NoteDeathForInfestedHoard(const struct FCataclysmDeathNotice& Notice);

	/** The Infested Hoard, on a take: an infested drop taken by the player by hand adds one stack. */
	void NoteLootTakenForInfestedHoard(const struct FCataclysmLootTakenNotice& Notice);

	/** The Infested Hoard, on the beat: the drain, once a second while a stack is held. */
	void StepInfestedHoard(class ACataclysmPlayerCharacter* Player, class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Golden Spires, on the beat: a zone kept drawn around each living spire, and every creature's
	 * spire damage multiplier written for whether it stands near one.
	 */
	void StepGoldenSpires(class ACataclysmPlayerCharacter* Player);

	/** Eternal Chorus, on the beat: earshots kept drawn for living sources, and the player's effects. */
	void StepEternalChorus(class ACataclysmPlayerCharacter* Player,
						   class UCataclysmAbilitySystemComponent* AbilitySystem);

	void StepDivineWrath(class ACataclysmPlayerCharacter* Player,
						 class UCataclysmAbilitySystemComponent* AbilitySystem);

	/** Soul Harvest, on every death: a soul to the nearest living creature within reach. */
	void NoteDeathForSoulHarvest(const struct FCataclysmDeathNotice& Notice);

	/**
	 * Soul Harvest's figures on one creature, written onto its bases. `bFreshBlock` is true
	 * right after a rung change has written the creature's whole stat block over, so nothing
	 * a soul added is still on it.
	 */
	void ApplySoulHarvestFigures(ACataclysmEnemyCharacter* Creature, bool bFreshBlock);

	/** Trick or Treat, on the beat: the haste on while its clock runs and off after. */
	void StepTrickOrTreat(
		class ACataclysmPlayerCharacter* Player,
		class UCataclysmAbilitySystemComponent* AbilitySystem);

public:

private:

	/**
	 * Give a wraith the four figures that make it one, and put them back when they go.
	 *
	 * CALL THIS ONLY WHERE THE CREATURE'S STAT BLOCK HAS JUST BEEN WRITTEN, because the
	 * three increases MULTIPLY WHAT THEY READ. Twice in a row without a fresh block in
	 * between would give a wraith 44% rather than 20%. There are two such places and no
	 * more: the spawn below, and immediately after a rung change, where
	 * `ApplyStartingAttributes` has just put the new rung's own figures on.
	 */
	void ApplyVengefulWraithFigures(ACataclysmEnemyCharacter* Wraith);

	/**
	 * Judgment Zones, on every beat: lay radiant ground, and punish standing in it.
	 *
	 * THIS RULE DEALS THE DAMAGE AND COUNTS THE TRIGGERS ITSELF, and the zones it lays
	 * carry none. A zone's damage is fixed when it is spawned and it ticks on its own
	 * second, so a rule that owned the count while the zone owned the damage would have
	 * two clocks for one figure. Ruled under the project owner's delegation; issue #1701
	 * is what made a zone that does not damage possible.
	 */
	void StepJudgmentZones(ACataclysmPlayerCharacter* Player,
						   class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * March of Progress, on every beat: make every creature on the floor hit harder for
	 * how deep the floor is, and put the armour the player has earned on them.
	 *
	 * THE DAMAGE IS SET AND NOT ADDED, SO THE BEAT IS IDEMPOTENT. Every creature is given
	 * the same multiplier, worked out from the floor number alone, so running this beat
	 * twice leaves the floor exactly as running it once does.
	 * `ACataclysmEnemyCharacter::SetFloorDepthDamageMultiplier` returns at once when the
	 * value has not changed, so after the first beat of a floor this writes nothing.
	 *
	 * A SWEEP RATHER THAN A CALL AT POPULATION, because creatures arrive on a floor after
	 * it is populated: Grave Tide's waves, Royal Guard's guards, Vengeful Wraiths' risen
	 * and a Horde dungeon's next wave. A creature placed on this beat is given its
	 * multiplier on the next one, which is the order every rule on the beat follows.
	 */
	void StepMarchOfProgress(ACataclysmPlayerCharacter* Player,
							 class UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Commander's Aura, on every beat: every creature at Elite or above grants the
	 * Commander buff to its nearby allies. Issues #1820 and #41.
	 *
	 * THE BUFF IS RE-APPLIED EVERY BEAT RATHER THAN TRACKED, which is
	 * `Celestial_Hallowed_Groundfall`'s shape and its stated reason: the effect is a
	 * single stack, so a second application refreshes the one already there rather than
	 * adding another. A creature that stays beside its commander keeps it; one that walks
	 * away loses it when the second runs out.
	 *
	 * NOTHING IS REMEMBERED BETWEEN BEATS EXCEPT THE COUNT THE PANEL SHOWS. There is no
	 * list of who is buffed, because the buff expires on its own and re-applying is
	 * cheaper than tracking. The Succubus tracks its own holders because it must take the
	 * buff off the moment an ally leaves its aura; this rule does not, and the entry in
	 * `docs/DECISIONS.md` records that as a judgement.
	 *
	 * A COMMANDER DOES NOT BUFF ITSELF. `UCataclysmTargeting::FindAlliesInSphere` excludes
	 * the instigator, so passing the commanding creature as the instigator is the whole of
	 * it. Ruled under the project owner's delegation.
	 */
	void StepCommandersAura(ACataclysmPlayerCharacter* Player);

	/**
	 * Anti-Magic Zones, on every beat: lay ground that refuses spells, and lock the
	 * player's spells while they stand in it. Issues #1820 and #41.
	 *
	 * THE LOCK FIRST, FROM WHAT IS STANDING, AND THEN THE LAYING. The beat a player walks
	 * out of a zone is a beat on which nothing is laid, so a lock decided inside the
	 * laying branch would follow them around the floor. `StepSingularityWells` has the
	 * same order for the same reason.
	 *
	 * THE ZONE ITSELF DOES NOTHING. It is spawned with no damage and no effect, so its own
	 * sweep is skipped; it is the ground the player sees and the circle this rule asks
	 * `Covers` of. The lock is this rule's, written through the shared applier, so a zone
	 * expiring and a player stepping out end it the same way.
	 */
	void StepAntiMagicZones(ACataclysmPlayerCharacter* Player,
							class UCataclysmAbilitySystemComponent* AbilitySystem);


	/**
	 * March of Progress' armour, on the death of the floor's Commander.
	 *
	 * THE PLAYER MUST HAVE STRUCK THE LAST BLOW TO BE PAID. The row says "killing the
	 * Commander", so a Commander that burns to death on another rule's ground, or is
	 * killed by another creature, pays nothing. The four other listeners whose rows say
	 * "when you kill" ask the same question the same way.
	 *
	 * BUT THE FLOOR LOSES ITS COMMANDER EITHER WAY, and that is recorded before the
	 * question of who struck the blow. The row names "the Commander in each level" --
	 * one creature a floor -- so a floor whose Commander died to something else does not
	 * get another, and the player who let that happen earns nothing there.
	 *
	 * IT PAYS ONCE. `bMarchOfProgressCommanderSlain` is what stops a second notice for
	 * the same body paying twice, which matters because a floor has one Commander and
	 * the count it feeds lasts the whole run.
	 */
	void NoteDeathForMarchOfProgress(const struct FCataclysmDeathNotice& Notice);

public:
	/**
	 * What this floor adds to a boss's drop roll, or nothing when it has not been earned.
	 *
	 * READ BY `ACataclysmEnemyCharacter::HandleDeath` THROUGH THE STATIC BELOW.
	 */
	float JudgmentZonesMagicFindBonus() const;

	/**
	 * The same, found in a world rather than asked of a game mode in hand.
	 *
	 * **NOTHING IN AN AUTOMATION RUN CAN MAKE THIS ANSWER**, and that is a property of the
	 * engine rather than of this rule. `UWorld::GetAuthGameMode` finds the authority game
	 * mode, `UWorld::AuthorityGameMode` is private and only a game instance sets it, and a
	 * world built by `UWorld::CreateWorld` has none -- so a game mode spawned into a test
	 * world is never found. `UCataclysmEnemyScore::FloorIn` carries the same gap for the
	 * floor number and the same note.
	 *
	 * SO THE ARITHMETIC LIVES WHERE A TEST CAN REACH IT:
	 * `UCataclysmDungeonModifierEffects::JudgmentZonesMagicFindFor` takes the bonus as an
	 * argument. This function is the hop that is not covered, and the decisions entry says
	 * so beside the floor number's identical gap.
	 */
	static float JudgmentZonesMagicFindIn(const UObject* WorldContext);

private:

	/**
	 * Brand of the Aggressor's stack, on a blow the PLAYER landed on a creature.
	 * Issues #1820 and #41.
	 *
	 * THE OPPOSITE WAY ROUND FROM `NoteHitForWastingSickness`, AND THAT IS THE
	 * WHOLE DIFFERENCE BETWEEN THEM. That rule's row is about blows landing on
	 * the player, so it tests `Notice.Target`; this rule's row says "Hitting an
	 * enemy applies a stack ... to you", so it tests `Notice.Attacker` and
	 * requires the target to be a creature.
	 *
	 * A LANDED BLOW AND NOT AN ATTEMPT, for the reason that rule gives:
	 * `FCataclysmHitNotice::Landed` is what reached the target after every
	 * mitigation step and is zero for a blow that was evaded or wholly stopped.
	 *
	 * THE NOVA REACHES THE PLAYER AND THEIR ALLIES AND NOT THE CREATURES. The row
	 * says "to you and nearby allies". `UCataclysmTargeting::FindAlliesInSphere`
	 * answers the allies and **excludes the actor it is asked on behalf of** --
	 * its shared gather step drops `Actor == Instigator` -- so the player is
	 * damaged by a separate call. Leaving that out would build a rule that erupts
	 * and never touches the player, which is the row's main promise.
	 *
	 * THE PLAYER IS THE INSTIGATOR OF THEIR OWN NOVA, because the row says "you
	 * erupt". Unlike the two rules above it, whose source had died, there is a
	 * living actor with an ability system to name.
	 *
	 * NOTHING TELLS THE PLAYER THE COUNT, WHICH IS WHAT WASTING SICKNESS DOES AND
	 * IS WORTH SAYING OUT LOUD. Measured 2026-09-14: outside the dungeon rule
	 * files, nothing in `game/Source` reads a floor rule's stack count. The floor
	 * panel names the row and prints its description word for word, so a player
	 * can read that twenty blows erupt; it does not say how many they have.
	 *
	 * THE TWO ROWS ARE NOT ALIKE IN HOW MUCH THAT COSTS. Wasting Sickness's
	 * stacks lower maximum health and mana, so its count is visible in the bar
	 * even though the number is not. This one changes nothing at all until the
	 * twentieth blow. Recorded on #1820 rather than fixed here.
	 */
	void NoteHitForBrandOfTheAggressor(const struct FCataclysmHitNotice& Notice);

	/** Contagious Touch: a hit the player landed on a creature costs them its share for every stack. Issue #41. */
	void NoteHitForContagiousTouch(const struct FCataclysmHitNotice& Notice);

	/**
	 * Put the floor panel's lines back on the screen, carrying whatever the
	 * stateful rules are counting now. Issues #1820 and #41.
	 *
	 * CALLED WHEN A COUNT MOVES AND NOT ONLY WHEN A FLOOR BEGINS, which is the
	 * whole reason it exists as a function. Until 2026-09-14 the panel was drawn
	 * once, at the end of `ApplyFloorRulesToPlayer`, so a count put on it would
	 * always have read as the value the player had before they did anything --
	 * which is nothing.
	 *
	 * FROM WHERE THE COUNT CHANGES RATHER THAN FROM THE BEAT. The two counts move
	 * on a blow, four times a second is not when they move, and a panel redrawn
	 * on a clock would be doing work on every floor whether or not anything
	 * counts. `NoteHitForWastingSickness` and `NoteHitForBrandOfTheAggressor`
	 * each call this immediately after raising their own.
	 *
	 * IT ASKS THE ROWS WHETHER THEY ARE ON THE FLOOR, so a floor carrying neither
	 * hands the panel an empty map and every line reads exactly as it did before
	 * any of this existed.
	 *
	 * NOTHING TESTS THAT THIS IS CALLED, AND ISSUE #1841 CARRIES WHY. The tests
	 * read `LiveCountsForTheFloor` below, which is the figure the panel is handed
	 * and not the handing, so deleting a call to this from a listener leaves
	 * every one of them passing. Closing it needs a test world with a game
	 * instance, which is that issue's subject and not a rule's to widen into.
	 */
	void RefreshFloorModifierPanel();

public:
	/**
	 * What each stateful rule on this floor is counting right now, by row key.
	 * Issues #1820 and #41.
	 *
	 * PUBLIC, AND SEPARATE FROM THE DRAW, BECAUSE THAT IS THE ONLY WAY A TEST CAN
	 * SEE IT. `UCataclysmFloorModifierPanelLayout`'s own header records the rule
	 * this follows: the automation tests run with `-nullrhi`, a widget built in a
	 * headless test has no children to read, so what the panel says is decided
	 * where a test can read it. The panel itself needs a widget class loaded from
	 * an asset that a test world does not have, so `RefreshFloorModifierPanel`
	 * returns early there and proves nothing. This function is the part worth
	 * asserting on.
	 *
	 * ONLY THE ROWS THE FLOOR CARRIES. A count for a row not in force would be a
	 * number with nothing behind it, and there is no line to put it on.
	 *
	 * WITH ONE EXCEPTION, ruled 2026-10-09: on the dark floor a fall leads to, Those in the Dark's line is counted
	 * whatever rows that floor drew, and `RowsTheFloorPanelLists` gives the panel the row to put it on.
	 */
	TMap<FName, FString> LiveCountsForTheFloor() const;

	/**
	 * The row keys the floor panel draws a line for: the floor's own rows in their order, and on the dark floor a
	 * fall leads to, Those in the Dark after them when that floor did not draw it. Issues #1820 and #41, ruled
	 * 2026-10-09: a player on the dark floor always sees why the stairs are shut and how many creatures still
	 * stand. The floor's own rows and nothing else at every other time.
	 */
	TArray<FName> RowsTheFloorPanelLists() const;

private:

	/**
	 * Mortal Decay's slowing, on a creature the player reaped. Issues #1786
	 * and #41.
	 *
	 * THE PLAYER MUST HAVE DONE THE KILLING, WHICH IS THE ROW'S OWN WORDS AND
	 * THE ONE PLACE THIS DIFFERS FROM WITHERED GROUND'S LISTENER. That row says
	 * "Enemies leave patches of Barren Earth on death" and takes every death;
	 * this one says "the player must give death his due souls by reaping
	 * enemies", so a creature killed by a patch of burning ground, by another
	 * creature, or by anything else buys the player nothing.
	 *
	 * A MINION'S KILL COUNTS, AND THAT NEEDS NO CODE HERE. `FCataclysmDeathNotice
	 * ::Killer` is credited to the SUMMONER for a minion's blow -- its own
	 * comment says so -- so a Ritualist reaping through its imps is reaping.
	 *
	 * NO COUNT OF KILLS IS KEPT, AND NOTHING NEEDS ONE. The row asks for the
	 * affliction to be slowed temporarily rather than for souls to be tallied,
	 * so a world-time stamp pushed forward by each kill is the whole state.
	 * `NihilsEmbraceRewardUntilSeconds` is the same shape for the same reason.
	 *
	 * A SECOND KILL REPLACES THE WINDOW RATHER THAN EXTENDING IT, so a player
	 * killing steadily stays slowed and one kill never buys more than its own
	 * few seconds.
	 */
	void NoteDeathForMortalDecay(const struct FCataclysmDeathNotice& Notice);

	/**
	 * How far the player had walked when The Nihil's Embrace was last cleansed.
	 * Issue #41, slice 2.
	 *
	 * THE LOSS IS THE DIFFERENCE between this and the total the character keeps,
	 * which is why a cleanse needs no second counter: it moves this up to where
	 * the character is now and the difference becomes nothing.
	 *
	 * IT SURVIVES A FLOOR, because the row says the reduction is permanent. It is
	 * put back to nothing when the player leaves the dungeon and the floor carries
	 * no modifiers at all.
	 */
	float MetresWalkedAtLastCleanse = 0.0f;

	/**
	 * World time until which The Nihil's Embrace's reward lasts, or negative for
	 * no reward running. Issue #41, slice 2.
	 */
	float NihilsEmbraceRewardUntilSeconds = -1.0f;

	/**
	 * World time until which Mortal Decay is slowed by a kill, or negative for
	 * a decay running at its full rate. Issues #1786 and #41.
	 *
	 * THE SAME SHAPE AS THE FIELD ABOVE, AND FOR THE SAME REASON: the rule needs
	 * to know whether a window is open, not how many kills opened it, so there
	 * is nothing to count and nothing to decrement.
	 *
	 * IT IS NOT PUT BACK WHEN THE FLOOR CHANGES, UNLIKE EVERY BEAT-DRIVEN FIELD
	 * BELOW. Those hold something applied to the character, which changing floor
	 * takes off; this holds nothing but a time. A player who kills and takes the
	 * stairs at once carries the rest of that window onto the next floor, which
	 * is a few seconds and is what "temporarily" already means.
	 *
	 * LEAVING THE DUNGEON DOES PUT IT BACK, beside The Nihil's Embrace's, so the
	 * field's lifetime is the dungeon's rather than the session's.
	 */
	float MortalDecaySlowedUntilSeconds = -1.0f;

	/**
	 * Wasting Sickness: the stacks the player carries, and what the last apply
	 * put on them. Issues #1786 and #41.
	 *
	 * THE COUNT SURVIVES A FLOOR AND THE APPLIED FIGURE DOES NOT, which is the
	 * whole difference between this rule and Death's Embrace. The row says the
	 * debuff is "permanent for the duration of the dungeon", so the stairs take
	 * nothing away; but changing floor replaces the player's dungeon modifiers
	 * wholesale, so what was applied is gone from the character and the applied
	 * figure has to go back to nothing or the beat will believe the reduction is
	 * still there and never put it back.
	 *
	 * BOTH GO BACK TO NOTHING WHEN THE PLAYER LEAVES THE DUNGEON, because
	 * "for the duration of the dungeon" ends there.
	 *
	 * A COUNT AND NOT A TIME, unlike Mortal Decay's stamp above, because this row
	 * asks for stacks: each one is worth the same share.
	 *
	 * THIS SAID "the player is told how many they have" AND NOTHING TELLS THEM.
	 * Measured 2026-09-14 while building `Demonic_Brand_of_the_Aggressor`, which
	 * keeps a count of its own: outside `Dungeon/`, nothing in `game/Source`
	 * reads either count. What the player sees is the CONSEQUENCE -- these stacks
	 * lower maximum health and mana, so the bar shrinks -- and the floor panel's
	 * line for the row, which carries the row's description word for word. The
	 * number itself is shown nowhere. Corrected by the change that noticed it.
	 */
	int32 WastingSicknessStacks = 0;
	int32 WastingSicknessStacksApplied = 0;

	/**
	 * How many of the player's landed blows have branded them on this floor.
	 * Issues #1820 and #41.
	 *
	 * NO PAIRED "APPLIED" FIELD, UNLIKE THE COUNT ABOVE. Wasting Sickness's
	 * stacks are a standing reduction that the beat has to keep on the character,
	 * so it must remember what it last wrote. A brand does nothing at all until
	 * the twentieth, and then it deals one blow and is gone, so there is no
	 * standing state to reconcile.
	 *
	 * IT GOES WITH THE FLOOR, AND THAT IS A JUDGEMENT RATHER THAN THE ROW'S
	 * WORDS. The row says nothing about floors. Every other count on this beat
	 * that the data does not rule on is cleared by `ApplyFloorRulesToPlayer`, and
	 * carrying a part-built nova across a loading screen would fire it on a floor
	 * the player had not yet hit anything on.
	 */
	int32 BrandStacks = 0;

	/**
	 * One Grasping Tentacle on this floor, and when it may grab again.
	 * Issues #1786 and #41.
	 *
	 * PLAIN DATA AND NOT A `UPROPERTY`, for the reason `WaveStillToArrive` gives:
	 * Unreal's header tool refuses a `UPROPERTY` of an unreflected struct, and a
	 * weak pointer plus a float needs no reflection.
	 *
	 * THE COOLDOWN IS PER TENTACLE AND NOT PER FLOOR, which is what makes walking
	 * away from one worth doing. A floor-wide cooldown would mean a player held
	 * by one tentacle is safe from all of them, and standing among several would
	 * be no worse than standing by one.
	 */
	struct FCataclysmGraspingTentacle
	{
		TWeakObjectPtr<class ACataclysmGroundZone> Zone;

		/** World time before which this one will not grab. Negative is ready. */
		float MayGrabAgainAtSeconds = -1.0f;
	};

	/**
	 * The tentacles on this floor, when the grab now holding ends, and what the
	 * last beat put on the player. Issues #1786 and #41.
	 *
	 * THEY LAST THE FLOOR, so this list only ever shrinks by something destroying
	 * a tentacle -- which a floor change does, whatever arena comes next (see
	 * `ApplyFloorRulesToPlayer`). A weak pointer going invalid IS that, so nothing
	 * has to be told.
	 *
	 * ALL FOUR GO BACK TO NOTHING ON A NEW FLOOR. The list because those actors
	 * are already gone; the clock so the first tentacle of a floor does not
	 * arrive on its first beat carrying the last floor's wait; the grab because a
	 * player who takes the stairs is not still held by a tentacle they left
	 * behind; and the applied figure because changing floor has already taken the
	 * reduction off the character.
	 *
	 * THE GRAB IS FORGOTTEN ON A NEW FLOOR AND WASTING SICKNESS'S STACKS ARE NOT,
	 * and the difference is the rows: that one says its debuff is "permanent for
	 * the duration of the dungeon", and this one describes being held by a
	 * particular tentacle in a particular place.
	 */
	/**
	 * Edict of Silence: how long since the last silence began, when the one
	 * running ends, and what the last beat put on the player. Issues #1786
	 * and #41.
	 *
	 * THE CADENCE SURVIVES THE STAIRS, AND IT IS THE ONLY CLOCK IN THIS FILE
	 * THAT DOES. Every other rule forgets its clock when the floor changes,
	 * because every other rule describes something happening on a floor. This
	 * row says the silence sweeps THE DUNGEON, and a player who took the stairs
	 * every eighty seconds would otherwise never be silenced at all -- the one
	 * place the two readings of that sentence differ in play. Ruled on
	 * 2026-09-14 and recorded in `docs/DECISIONS.md`.
	 *
	 * SO DOES THE SILENCE ITSELF, for the same reason: walking downstairs in the
	 * middle of one does not end it.
	 *
	 * THE APPLIED FIGURE DOES NOT SURVIVE, like every other rule's. Changing
	 * floor replaces the player's dungeon modifiers wholesale, which takes the
	 * lock off the character; forgetting the figure is what makes the next beat
	 * put it back while the silence is still running.
	 *
	 * ALL THREE GO BACK TO NOTHING WHEN THE PLAYER LEAVES THE DUNGEON, which is
	 * where "sweeps the dungeon" ends.
	 */
	float EdictOfSilenceSecondsSinceLast = 0.0f;
	float EdictOfSilencedUntilSeconds = -1.0f;
	float EdictOfSilenceLockApplied = 0.0f;

	/**
	 * Dirge Resonance: the beat's clock since the last crescendo, when the last one's haste
	 * ends on the world's clock, and the whole second the panel last showed. All of it is
	 * the floor's and goes at the stairs.
	 */
	float DirgeResonanceSecondsSinceLast = 0.0f;
	float DirgeResonanceHastedUntilSeconds = -1.0f;
	int32 DirgeResonanceShownSeconds = -1;

	/**
	 * The circle on the ground, and how long it has been there.
	 *
	 * A WEAK POINTER, because a floor change destroys the circle, whatever arena
	 * comes next (see `ApplyFloorRulesToPlayer`), and a raw pointer would outlive
	 * it. The pointer going invalid IS the circle being gone, which is the same
	 * reading `GraspingTentacles` makes of its own list.
	 *
	 * THE WARNING IS COUNTED IN BEATS RATHER THAN STAMPED IN WORLD TIME, which
	 * is the choice Death's Embrace records: a subtraction from world time would
	 * count a paused game against the player, and three seconds of warning is
	 * exactly the kind of figure where that would be felt.
	 *
	 * BOTH GO BACK TO NOTHING ON A NEW FLOOR. A circle drawn on the last floor
	 * is not a warning about this one, and the floor change has already destroyed
	 * it, so keeping the count would land a shell nobody was warned about.
	 */
	/**
	 * The craters burning now, and how long since the last bombardment.
	 *
	 * WEAK POINTERS, because a floor change destroys every crater still burning,
	 * whatever arena comes next (see `ApplyFloorRulesToPlayer`); a pointer going
	 * invalid IS its crater being gone, which is the reading every other rule here
	 * makes of its own list.
	 *
	 * THE LIST IS KEPT EVEN THOUGH THE ZONES BURN WITHOUT IT, because the beat
	 * has to ask WHERE the craters are in order to empower what is standing in
	 * them. Infernal Rain keeps its patches for the cap; this keeps its craters
	 * for their positions.
	 */
	// NAMED FOR WHAT IT HOLDS AND NOT FOR THE CONSTANT BESIDE IT.
	// `UCataclysmDungeonModifierEffects::HallowedGroundfallCraters` is how many
	// a bombardment leaves; this is which ones are burning. Two names one
	// letter apart in the same function would be a trap for the next reader.
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> HallowedGroundfallCratersBurning;
	float HallowedGroundfallSecondsSinceLast = 0.0f;

	TWeakObjectPtr<class ACataclysmGroundZone> ArtilleryStrikeCircle;
	float ArtilleryStrikeWarningSoFar = 0.0f;
	float ArtilleryStrikeSecondsSinceLast = 0.0f;

	TArray<FCataclysmGraspingTentacle> GraspingTentacles;
	float GraspingTentaclesSecondsSinceLast = 0.0f;
	float GraspedUntilSeconds = -1.0f;
	float GraspMovementLessApplied = 0.0f;

	/**
	 * What the last beat put on the player, so a beat that changes nothing asks
	 * for no stat refresh. Issue #41, slice 2.
	 *
	 * A REFRESH REWRITES THE CHARACTER'S WHOLE STANDING STAT LINE, so doing it
	 * four times a second for a number that has not moved would be waste. Both go
	 * back to nothing when a floor's rules are applied, because that replaces the
	 * floor's modifiers wholesale and the next beat has to put these back.
	 */
	float ResistanceLessApplied = 0.0f;
	float ResistanceMoreApplied = 0.0f;

	/**
	 * How long the player has been on this floor, counted in beats, and the
	 * stacks of Embrace of Death that time has been turned into. Issue #41,
	 * slice 5.
	 *
	 * COUNTED IN BEATS RATHER THAN TAKEN OFF THE WORLD CLOCK, so it measures
	 * time the game actually ran on this floor. A subtraction from world time
	 * would count a paused game, and that difference is exactly the thing a
	 * player would call unfair.
	 *
	 * WHICH MAKES A BEAT WORTH A QUARTER OF A SECOND ONLY ABOVE FOUR FRAMES A
	 * SECOND, and that is stated rather than hidden: the beat is zeroed rather
	 * than decremented, deliberately, so a long frame yields one beat however
	 * long it was. Below that rate the stacks arrive slower than every ten
	 * seconds. It is in the player's favour, Forced March's per-beat share has
	 * the same property, and issue #1613 carries the fix -- which belongs in the
	 * rules, because the wave arrival needs the zeroing it has.
	 *
	 * THE COUNT IS KEPT AS WELL AS THE CLOCK because it is what a beat compares
	 * against to decide whether anything moved, the same argument the two
	 * resistance fields above make. A stack arrives every ten seconds and the
	 * beat runs four times a second, so forty beats in forty-one do nothing.
	 *
	 * BOTH GO BACK TO NOTHING ON A NEW FLOOR, which the row states outright:
	 * "Stacks reset when entering a new floor." Forgetting only the clock would
	 * leave the player at whatever they had reached; forgetting only the count
	 * would re-apply it on the next beat.
	 */
	float DeathsEmbraceSecondsOnFloor = 0.0f;
	int32 DeathsEmbraceStacksApplied = 0;

	/**
	 * How long since Infernal Rain last dropped a patch, and what is still alight.
	 *
	 * ONE THING ADVANCES IT, AND THAT IS `StepInfernalRain`. A fault repaired
	 * earlier today had two functions each advancing and resetting one timer: a
	 * creature running both pulsed at twice the intended rate, and on the step it
	 * fell due whichever ran first reset it so the other never fired.
	 *
	 * THE FLOOR RESET WRITES IT TOO, AND SAYING "ONE WRITER" WOULD BE FALSE. An
	 * earlier version of this comment said exactly that. Forgetting the clock when
	 * the floor changes is a different act from counting towards the next patch,
	 * and the rule needs both; what must stay true is that only one thing adds to
	 * it.
	 *
	 * THE PATCHES ARE WEAK AND COUNTED BY ASKING, not tracked by being told. A
	 * patch destroys itself when its ten seconds end, and a floor change destroys
	 * every one still burning (see `ApplyFloorRulesToPlayer`), so "how many are
	 * alight" is "how many of these are still valid" and nothing has to notice an
	 * expiry. The patch actor keeps its own drawings the same way and for the same
	 * reason.
	 *
	 * BOTH GO BACK TO NOTHING ON A NEW FLOOR. The clock, so the first patch of a
	 * floor does not arrive on its first beat carrying the last floor's wait; the
	 * list, because those actors are gone and a stale list would count expired
	 * patches against the cap and stop the rain.
	 */
	float InfernalRainSecondsSinceLastPatch = 0.0f;
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> InfernalRainPatches;

	/**
	 * Infernal Rain: a fireball falling, where its patch will be placed, when, at what damage and type. The patch is
	 * placed when the fall ends, whether or not the ball is still there to see. Issue #1699.
	 */
	struct FInfernalRainFall
	{
		FVector Where = FVector::ZeroVector;
		float SecondsLeft = 0.0f;
		float DamagePerSecond = 0.0f;
		FName PatchType;
		TWeakObjectPtr<class ACataclysmProjectile> Ball;
	};
	TArray<FInfernalRainFall> InfernalRainFalls;

	/** The runtime floor obstacles. See `FloorObstaclesNow`. Issues #1820 and #41. */
	TArray<TWeakObjectPtr<ACataclysmFloorObstacle>> FloorObstacles;

	/** An obstacle still in its warning, and how long is left. */
	struct FFloorObstacleWarning
	{
		TWeakObjectPtr<ACataclysmFloorObstacle> Obstacle;
		float SecondsLeft = 0.0f;
	};
	TArray<FFloorObstacleWarning> FloorObstacleWarnings;

	/** What opening a shortcut brings closer, which the floor panel names for Warzone. */
	enum class EShortcutLeadsTo : uint8
	{
		Anywhere,
		Exit,
		Entrance,
		Nearby,
	};

	/** One shortcut this floor carved, the row that owns it, and its gate. */
	struct FGatedShortcut
	{
		FCataclysmFloorShortcut Shortcut;
		FName RowKey;
		int32 Index = 0;
		EShortcutLeadsTo LeadsTo = EShortcutLeadsTo::Anywhere;
		bool bOpen = false;
		TArray<TWeakObjectPtr<ACataclysmFloorObstacle>> GateActors;
		/** Soul Chains only: who holds it shut, and whether anyone ever did. */
		TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> Bearers;
		bool bHadBearers = false;
	};
	TArray<FGatedShortcut> GatedShortcuts;

	/** See `WarzonePlannedPointsNow`. */
	TArray<FIntPoint> WarzonePlannedPoints;

	bool bGateRowsHaveNoShape = false;
	float LabrynthSecondsSince = 0.0f;
	int32 SoulChainsRewardDrops = 0;
	int32 SoulChainsRewardRolls = 0;

	/** The stream every gate choice of this floor is drawn on, seeded from the floor's plan. */
	FRandomStream GatedShortcutStream;

	/** Added to the floor's seed for that stream, so it is not the stream the floor itself was carved on. */
	static constexpr int32 GatedShortcutSalt = 0x6A7E;

	/**
	 * Called by `BuildFloor` between generating a plan and building it: forgets the last floor's shortcuts, and on a
	 * Halls floor that is not a Horde arena finds and carves this floor's, row by row, each searched for with the
	 * earlier ones' gates closed.
	 */
	void PlanTheGatedShortcuts(FCataclysmFloorPlan& Plan);

	/** Destroy every gate and forget every shortcut. */
	void ForgetTheGatedShortcuts();

	/** Close every gate that should be closed: its cells blocked in the plan and its two pillars standing. */
	void PlaceTheShortcutGates();

	/** Close one gate. With `bAsk`, only when the placement rule allows its cells; false when it does not. */
	bool CloseTheGate(FGatedShortcut& One, bool bAsk);

	/** Open one gate: its pillars gone and its cells walkable. */
	void OpenTheGate(FGatedShortcut& One);

	/**
	 * The per-cell half of closing a gate, shared so a barrier that is not a shortcut's gate can use it. Issues #1820
	 * and #41. Blocks every cell in the floor's plan and, when none of `Pillars` still stands, places one one-cell
	 * pillar on each cell, raised, and keeps them in `Pillars`. `RowKey` is the row the pillars are coloured and named
	 * for. It asks nothing: whether the cells may be closed is the caller's question. Does nothing with no floor.
	 */
	void BlockCellsWithPillars(const TArray<FIntPoint>& Cells, FName RowKey,
		TArray<TWeakObjectPtr<ACataclysmFloorObstacle>>& Pillars);

	/** The per-cell half of opening a gate: every pillar in `Pillars` destroyed and forgotten, every cell walkable. */
	void UnblockCellsAndDestroyPillars(const TArray<FIntPoint>& Cells,
		TArray<TWeakObjectPtr<ACataclysmFloorObstacle>>& Pillars);

	// ----------------------------------------------------------------------
	// Lightforged Walls' sections. Issues #1820 and #41. Ruled 2026-10-08.
	// ----------------------------------------------------------------------

	/**
	 * Where this floor is divided, as `FCataclysmFloorGenerator::FindSections` answered when its plan was made, and
	 * empty on a floor that gets no sections. NOT IN `CellsTheFloorHolds`, as ruled: a closed barrier's cells are
	 * Solid in the plan, which the placement rule refuses before it asks anything else, and an opened barrier's cells
	 * are ordinary floor.
	 */
	FCataclysmFloorSections FloorSections;

	/** Whether each barrier is closed now, one for each boundary. An opened barrier is never closed again. */
	TArray<bool> SectionBarrierClosed;

	/** Each barrier's pillars, one array for each boundary, as `BlockCellsWithPillars` keeps them. */
	TArray<TArray<TWeakObjectPtr<ACataclysmFloorObstacle>>> SectionBarrierPillars;

	/** The section and its count that the panel last showed, beside `LightforgedWallsPanelCount`. */
	int32 LightforgedWallsPanelSection = INDEX_NONE;
	int32 LightforgedWallsPanelSectionCount = -1;

	/**
	 * Fragmented Reality: the section of this floor that is Fragmented; how many times the player has entered it on
	 * this floor; the pair last written on the player, `INDEX_NONE` for none; and the pair a test forces. Issues
	 * #1820 and #41. The section is decided by `PlanTheSections` and forgotten by `ForgetTheSections`; the entries
	 * and the applied pair are forgotten with the floor's other applied state in `ApplyFloorRulesToPlayer`, which
	 * runs after the floor is built.
	 */
	int32 FragmentedRealitySection = INDEX_NONE;
	int32 FragmentedRealityEntries = 0;
	int32 FragmentedRealityPairApplied = INDEX_NONE;
	int32 FragmentedRealityForcedPair = INDEX_NONE;

	/** Whether this floor's section barriers are closed when it begins: only on a floor carrying Lightforged Walls. */
	bool SectionBarriersStandOnThisFloor() const;

	/**
	 * Called by `BuildFloor` after `PlanTheGatedShortcuts`: forgets the last floor's sections and barriers, and on a
	 * floor that gets sections asks the search, passing every planned shortcut's gate cells as cells that may close.
	 * Closes nothing.
	 */
	void PlanTheSections(const FCataclysmFloorPlan& Plan);

	/** Open every barrier still closed and forget the sections. */
	void ForgetTheSections();

	/** Open every barrier still closed: pillars gone, cells walkable. Used before a floor is populated again. */
	void OpenEverySectionBarrier();

	/**
	 * Open one barrier, if it is closed. THE ONE PLACE A BARRIER'S CELLS ARE OPENED. A cell that another barrier still
	 * closed also holds is left Solid, and its pillar is handed to that barrier to destroy when it opens. Every
	 * other cell of the barrier is made walkable and its pillar destroyed.
	 */
	void OpenTheSectionBarrier(int32 Barrier);

	/**
	 * Called by `PopulateFloor` BEFORE any rule chooses a cell for an object and before the creatures are placed:
	 * every barrier is opened if it was closed and then closed, with pillars in the row's colour, raised at once, one
	 * pillar a cell: a cell an earlier barrier already closed is not given a second. So
	 * every picker reads a barrier's cells as Solid. Once the creatures stand, `StepTheSectionBarriers` is called at
	 * once and opens the barrier of any section that holds none.
	 */
	void CloseTheSectionBarriers();

	/** The beat: a closed barrier whose section holds no standing creature the floor placed opens, and stays open. */
	void StepTheSectionBarriers();

	/**
	 * Whether an obstacle raised during play may close these cells: `CataclysmFloorCanBlockBesideBarriers` on this
	 * floor's plan with the cells of every barrier still closed. With none closed it is `CataclysmFloorCanBlock`.
	 */
	bool AnObstacleMayClose(const TArray<FIntPoint>& Cells, FIntPoint From, const TSet<FIntPoint>& Held) const;

	/** Open a row's shortcut by its index, if it exists and is closed. */
	void OpenTheShortcutOf(FName RowKey, int32 Index);

	/** Soul Chains' bearers, chosen once the floor's creatures stand, as Infernal Seals' are. */
	void ChooseTheSoulChainBearers();

	/** The beat: a Soul Chains gate whose bearers are dead opens and pays; The Labrynth swaps a gate when due. */
	void StepGatedShortcuts();

	/** `FloorSourceCells`' two rules on a plan not built yet, drawn on a stream rather than the global random. */
	static TArray<FIntPoint> SeededSourceCells(const FCataclysmFloorPlan& Plan, int32 Count, FRandomStream& Stream);

	float HeavensQuakeSecondsSince = 0.0f;
	float CryptquakeSecondsSince = 0.0f;
	int32 HeavensQuakePillars = 0;
	int32 CryptquakeSections = 0;

	/** Heaven's Quake's and Cryptquake's beat: the warnings run down, then each rule places one when it is due. */
	void StepFloorObstacles(class ACataclysmPlayerCharacter* Player, bool bHeavensQuake, bool bCryptquake);

	/**
	 * A square of cells `Side` across, in the band around the player, that the placement rule allows, or false.
	 * `CellsTheFloorHolds` plus the cells of every warning still pending, so two warnings never share a cell.
	 */
	bool ChooseObstacleCells(const class ACataclysmPlayerCharacter* Player, int32 Side, TArray<FIntPoint>& Out) const;

	/** `CellsTheFloorHolds` and the cells of every warning still pending but `Except`. */
	TSet<FIntPoint> CellsHeldOrWarned(const ACataclysmFloorObstacle* Except = nullptr) const;

	/** A warning whose second is over: raised, or cancelled when its cells are no longer allowed. */
	void RaiseOrCancel(ACataclysmFloorObstacle* Obstacle);

	/** The player's cell on this floor, or (-1, -1). */
	FIntPoint ThePlayersCell() const;

	// ----------------------------------------------------------------------
	// Angelic Wardens. Issues #1820 and #41. Ruled 2026-10-08.
	// ----------------------------------------------------------------------

	/**
	 * One statue of this arena: its cell, the section of that cell or `INDEX_NONE` on a floor with no sections,
	 * whether it has woken, its pillar while it stands, and its warden once it has woken.
	 */
	struct FAngelicStatue
	{
		FIntPoint Cell = FIntPoint(-1, -1);
		int32 Section = INDEX_NONE;
		bool bWoken = false;
		TArray<TWeakObjectPtr<ACataclysmFloorObstacle>> Pillars;
		TWeakObjectPtr<ACataclysmEnemyCharacter> Warden;
	};

	/**
	 * This arena's statues, woken or not. NOT IN `CellsTheFloorHolds`: a standing statue's cell is Solid in the
	 * plan, which the placement rule refuses before it asks what is held, and a woken statue's cell is ordinary
	 * floor with a creature on it, held as any creature's cell is.
	 */
	TArray<FAngelicStatue> AngelicStatues;

	/** The two figures the panel last showed for the row. */
	int32 AngelicWardensPanelStanding = -1;
	int32 AngelicWardensPanelAwake = -1;

	/** Angelic Wardens: this arena's statues placed, where a new arena is populated. None without the row. */
	void PlaceTheAngelicStatues();

	/**
	 * Angelic Wardens: every statue forgotten. A statue whose pillar still stands gives its cell back; one whose
	 * pillar went with the last floor's actors names a cell of a plan that no longer exists, and touches nothing.
	 * The woken wardens are the floor's creatures and go as those go.
	 */
	void ForgetTheAngelicStatues();

	/** Angelic Wardens, on the beat: a statue the player stands near wakes, and the panel follows the two counts. */
	void StepAngelicWardens(class ACataclysmPlayerCharacter* Player);

	/** Angelic Wardens, on every skill use: the player's skill, not the basic attack, wakes the statues near them. */
	void NoteSkillUseForAngelicWardens(const struct FCataclysmSkillUsedNotice& Notice);

	/**
	 * Wakes every standing statue whose centre is within `WithinCm` of `PlayerAt`, flat, unless a closed barrier
	 * stands between the statue's section and the section of the cell `PlayerAt` is in. Both triggers ask here.
	 * Answers how many woke.
	 */
	int32 WakeTheAngelicStatuesNear(const FVector& PlayerAt, float WithinCm);

	/** One statue woken: its pillar gone, its cell walkable, and its warden raised on that cell, or null. */
	ACataclysmEnemyCharacter* WakeTheAngelicStatue(FAngelicStatue& One);

	// ----------------------------------------------------------------------
	// Those in the Dark, layer 1 of 2: the chasms and the fall. Issues #1820 and #41. Ruled 2026-10-08 and
	// 2026-10-09.
	// ----------------------------------------------------------------------

	/**
	 * This floor's chasm cells. Each stays walkable in the plan. HELD (`CellsTheFloorHolds`), a judgement by the
	 * writing session: nothing a rule places later is put on one, and no obstacle closes one.
	 */
	TArray<FIntPoint> ThoseInTheDarkChasmCells;

	/** The mark over each chasm cell, by the cell's index. Made on the beat, and made again there when lost. */
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> ThoseInTheDarkChasmZones;

	/** What the placement counted on this floor. */
	FThoseInTheDarkCount ThoseInTheDarkCount;

	/** How many falls this floor has recorded: nought or one. */
	int32 ThoseInTheDarkFallsNoted = 0;

	/**
	 * LAYER 2'S ONE FLAG: the player is on the dark floor a fall leads to. ONLY `ThePlayerFellIntoAChasm` SETS IT.
	 * Cleared by `LeaveTheDarkFloor`, `LeaveEmpireDungeon` and `EnterEmpireDungeon`. While it is clear, nothing
	 * this layer added changes a floor, a seed, a day charge or which floor is the last, and it changes one draw
	 * only: the row now answers Built, so the pool Reality Twister draws from holds one more row, as it did when
	 * each earlier row became Built.
	 */
	bool bOnTheDarkFloor = false;

	/** The number of the floor the player fell from; nought when not on the dark floor. */
	int32 DarkFloorFellFrom = 0;

	/** The count of the standing the panel last showed on the dark floor, so it is drawn again when it moves. */
	int32 DarkFloorPanelStanding = -1;

	/** Mixed with the floor's seed for the stream the chasms are drawn on, as `GatedShortcutSalt` is. */
	static constexpr int32 ThoseInTheDarkSalt = 0x7D4B;

	/** The placement's rules in the order they are applied. `ChooseTheChasmCells` applies the first so many. */
	static constexpr int32 ChasmRuleEntranceAndStairs = 1;
	static constexpr int32 ChasmRuleHeldAndBarriers = 2;
	static constexpr int32 ChasmRuleNoTwoSideBySide = 3;
	static constexpr int32 ChasmRuleCrossable = 4;

	/**
	 * The chasm cells the seeded draw gives this floor with the first `RulesApplied` rules applied. Every walkable
	 * cell of the plan as it stands, row by row, shuffled on a stream made from the plan's seed and
	 * `ThoseInTheDarkSalt`; then taken in that order until one for every
	 * `ThoseInTheDarkWalkableCellsPerChasm` walkable cells is chosen or the cells run out. `OutWalkable` is how many
	 * walkable cells there were. Changes nothing. The floor's own chasm cells are left out of the held cells, so a
	 * draw made again after the chasms are placed may choose them again. `OutRefusedByRule`, when given, is set to
	 * four counts, one a rule in the order above: the cells the draw came to and skipped for that rule.
	 */
	TArray<FIntPoint> ChooseTheChasmCells(int32 RulesApplied, int32& OutWalkable,
										  TArray<int32>* OutRefusedByRule = nullptr) const;

	/**
	 * Those in the Dark: this floor's chasms chosen, where a new arena is populated, after every other object of the
	 * arena is placed. None without the row, none on a Horde arena, none on the last floor, none on the dark floor.
	 */
	void PlaceTheChasms();

	/** Those in the Dark: the chasms, their marks, the count and the recorded fall forgotten. */
	void ForgetTheChasms();

	/** Those in the Dark, on the beat: a lost mark is made again, and a player standing within a chasm's ring falls. */
	void StepThoseInTheDark(class ACataclysmPlayerCharacter* Player);

	/**
	 * WHAT A FALL CALLS: THE WAY TO THE DARK FLOOR. The beat calls it once a floor, with the player and the chasm
	 * cell they stood on. It sets the flag, keeps the floor fallen from, and goes to the dark floor by `GoToFloor`
	 * with the number of the floor the dark floor's stairs lead to. No walk time is charged and no descent is
	 * counted. Nothing on a Horde arena. If the dark floor cannot be built the flag is cleared again.
	 */
	void ThePlayerFellIntoAChasm(class ACataclysmPlayerCharacter* Player, FIntPoint Chasm);

	/**
	 * THE DARK FLOOR'S STAIRS: their own route, which `GoDownOneFloor` hands over to before anything else. Clears
	 * the flag and goes by `GoToFloor` to the number the dark floor already carries, which is then built as the
	 * ordinary floor of that number. No walk time is charged and no descent is counted. The three rules that tell
	 * a new floor by its number are told it is a new floor. Answers whether the floor was built.
	 */
	bool LeaveTheDarkFloor(APawn* PawnToMove);

	/**
	 * One creature the dark floor's population placed, raised to the rung
	 * `UCataclysmDungeonModifierEffects::ThoseInTheDarkRungOnTheDarkFloor` gives, with the modifiers the new rung
	 * carries, and stood again on its cell now its size is known. Nothing for a creature already at the ceiling.
	 */
	void RaiseForTheDarkFloor(ACataclysmEnemyCharacter* Enemy, FIntPoint Cell);

	/** Infernal Rain: a patch at this point, typed and burning once a second with the others; null if none came. */
	class ACataclysmGroundZone* PlaceAnInfernalRainPatch(UWorld* World, const FVector& Where, float DamagePerSecond,
														 FName PatchType);

	/**
	 * Singularity Wells' clock, the wells on this floor, and the slow in force.
	 * Issues #1605 and #41.
	 *
	 * ONE THING ADVANCES THE CLOCK, AND THAT IS `StepSingularityWells`. The
	 * per-floor reset clears it, which is a different act, and the rule needs
	 * both. A fault repaired on 2026-09-12 had two functions each advancing and
	 * resetting one timer: whichever ran first reset it, so the other never fired.
	 *
	 * THE WELLS LAST THE FLOOR, so this list only ever shrinks by something
	 * destroying a well -- which a floor change does, whatever arena comes next
	 * (see `ApplyFloorRulesToPlayer`). A weak pointer going invalid IS that, so
	 * nothing has to be told.
	 *
	 * THE SLOW IN FORCE IS REMEMBERED SO A BEAT THAT CHANGES NOTHING ASKS FOR NO
	 * REFRESH, which is what every other beat-driven field here does. It is also
	 * the only one that can fall back to nothing without the floor changing.
	 */
	float SingularityWellsSecondsSinceLastWell = 0.0f;
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> SingularityWells;
	float SingularityWellsSlowApplied = 0.0f;

	/**
	 * Withered Ground's patches on this floor, and the reduction in force.
	 * Issue #41.
	 *
	 * NO CLOCK, WHICH IS THE DIFFERENCE FROM THE TWO HAZARD RULES ABOVE. Those
	 * place on a cadence and hold a timer; a patch of Barren Earth is placed
	 * by a death, so there is nothing to advance and nothing to reset.
	 *
	 * THE PATCHES LAST THE FLOOR, so this list only ever shrinks by something
	 * destroying a patch -- which a floor change does, whatever arena comes next
	 * (see `ApplyFloorRulesToPlayer`). A weak pointer going invalid IS that.
	 *
	 * THE REDUCTION IN FORCE IS REMEMBERED SO A BEAT THAT CHANGES NOTHING ASKS
	 * FOR NO REFRESH, which every beat-driven field here does.
	 */
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> WitheredGroundPatches;
	float WitheredGroundRecoveryLessApplied = 0.0f;

	/**
	 * Fungal Overgrowth's mushrooms on this floor, kept apart by kind, and what
	 * each kind is doing to the player right now. Issues #1820 and #41.
	 *
	 * TWO LISTS AND NOT ONE LIST OF PAIRS, because the beat asks a separate
	 * question of each -- is the player on a helping one, is the player on a
	 * hurting one -- and both answers can be yes at once.
	 *
	 * NO CLOCK AND NO CAP, which is Withered Ground's shape and for its reasons:
	 * a mushroom is placed by a death rather than by a cadence, so there is
	 * nothing to advance, and the row states no limit, so a cap would make
	 * "killing enemies creates mushrooms" stop being true at whichever kill hit
	 * it.
	 *
	 * THE MUSHROOMS LAST THE FLOOR, so these lists only ever shrink by something
	 * destroying one -- which a floor change does, whatever arena comes next (see
	 * `ApplyFloorRulesToPlayer`). A weak pointer going invalid IS that.
	 */
	/**
	 * How many Judgment stacks the player is carrying, and how many are on them.
	 * Issues #1820 and #41.
	 *
	 * TWO NUMBERS FOR THE REASON `WastingSicknessStacks` HAS TWO: the count moves
	 * on a burst and the reduction is applied on the beat, so the pair is what
	 * lets a beat that changes nothing ask for no refresh.
	 *
	 * BOTH GO AT THE STAIRS, WHICH IS THE DIFFERENCE FROM WASTING SICKNESS. That
	 * row says its debuff is "permanent for the duration of the dungeon", so only
	 * its applied figure is cleared per floor. This row says nothing of the kind,
	 * and its stacks come from creatures on the floor being left behind, so both
	 * go.
	 */
	int32 JudgmentStacks = 0;
	int32 JudgmentStacksApplied = 0;

	/**
	 * Leech Spores' clouds on this floor that nobody has touched yet. Issues #1820
	 * and #41.
	 *
	 * A CLOUD LEAVES THIS LIST TWO WAYS: the player touches it and it is spent,
	 * or the floor ends and the floor change destroys it, whatever arena comes next
	 * (see `ApplyFloorRulesToPlayer`). A weak pointer going invalid is the second;
	 * the first is removed by name.
	 */
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> LeechSporesClouds;

	/**
	 * Blood Altar's count of deaths on this floor, the time since the floor's start
	 * or its last pulse, and its ring. Issues #1820 and #41.
	 *
	 * ALL THREE GO AT THE STAIRS, AND THE RING IS DESTROYED THERE RATHER THAN
	 * FORGOTTEN, with every other zone the floor's rules placed, by
	 * `ApplyFloorRulesToPlayer` (issue #1925). A floor change clears the world only
	 * when the next floor is a new arena, and a Horde dungeon keeps its arena, so a
	 * ring that was only forgotten would stand on beside the one the next beat
	 * places.
	 */
	int32 BloodAltarDeaths = 0;
	float BloodAltarSecondsSinceLastPulse = 0.0f;
	TWeakObjectPtr<class ACataclysmGroundZone> BloodAltarRing;

	/**
	 * Necrotic Ground's patches, the time since its last patch and since its last burn,
	 * and the healing cut in force. Issues #1820 and #41.
	 *
	 * ALL FOUR GO AT THE STAIRS, and the patches are destroyed there with every other
	 * zone the floor's rules placed (issue #1925).
	 */
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> NecroticGroundPatches;
	float NecroticGroundSecondsSinceLastPatch = 0.0f;
	float NecroticGroundSecondsSinceLastBurn = 0.0f;
	float NecroticGroundHealingLessApplied = 0.0f;

	/**
	 * Ravenous Hoard: each creature's seconds alive on this floor, and the most stacks
	 * any creature held on the last beat, which the floor panel shows. Issues #1820
	 * and #41.
	 *
	 * WEAK KEYS, so a creature destroyed with its floor leaves nothing behind that could
	 * be read as alive. Both go at the stairs, and every creature still standing then
	 * is given its own damage back.
	 */
	TMap<TWeakObjectPtr<ACataclysmEnemyCharacter>, float> RavenousHoardSecondsAlive;
	int32 RavenousHoardStrongest = 0;

	/**
	 * Grave Tide: the seconds since its last wave and how many waves this floor has had.
	 * Issues #1820 and #41. Both go at the stairs; the creatures a wave placed are the
	 * floor's, and a floor change disposes of them as it does of any other.
	 */
	float GraveTideSecondsSinceLastWave = 0.0f;
	int32 GraveTideWaves = 0;

	/**
	 * Volatile Evolution: which creatures have already mutated, and how many mutated on
	 * this floor, which the floor panel shows. Issues #1820 and #41.
	 *
	 * THE MEMBERSHIP OUTLIVES THE FLOOR AND THE COUNT DOES NOT, and the two are
	 * deliberately different. The rung a creature reached stays with it -- nothing in
	 * this project puts a rarity back, and its drawn modifiers cannot be taken away --
	 * so a creature that lives through a Horde dungeon's change of wave has already had
	 * its one mutation. The count answers a different question, what happened on this
	 * floor, which is what the panel is showing.
	 *
	 * WEAK POINTERS, AND THE STEP DROPS THE STALE ONES, the way Ravenous Hoard keeps its
	 * clocks, so a destroyed creature leaves nothing behind that could be read as one
	 * still standing.
	 */
	TSet<TWeakObjectPtr<ACataclysmEnemyCharacter>> VolatileEvolutionMutated;
	int32 VolatileEvolutionMutations = 0;

	/**
	 * Royal Guard: which creatures have already had their one roll, and how many guards
	 * arrived on this floor, which the floor panel shows. Issues #1820 and #41.
	 *
	 * THE MEMBERSHIP OUTLIVES THE FLOOR AND THE COUNT DOES NOT, for the reason written
	 * above `VolatileEvolutionMutated`: a creature that lives through a Horde dungeon's
	 * change of wave has already had its roll, while the count answers what happened on
	 * this floor.
	 *
	 * THE ROLL IS RECORDED WHETHER OR NOT IT SUCCEEDED, which is what "one chance" means.
	 * A creature that rolled and missed does not roll again on the next beat, and a
	 * creature whose kind could not be told does not either.
	 *
	 * WEAK POINTERS, AND THE STEP DROPS THE STALE ONES, as Ravenous Hoard keeps its
	 * clocks.
	 */
	TSet<TWeakObjectPtr<ACataclysmEnemyCharacter>> RoyalGuardRolled;
	int32 RoyalGuardGuardsArrived = 0;

	/**
	 * Demon Prince: how many have risen on this floor, which the floor panel shows and
	 * which is the whole of the rule's state. Issues #1820 and #41.
	 *
	 * A COUNT AND NOT A RECORD OF CREATURES, unlike the two rules above. The ceiling is
	 * per floor rather than per creature -- a creature that has died cannot be asked
	 * again -- so there is nothing to remember about anybody. It goes at the stairs.
	 */
	int32 DemonPrincesRisen = 0;

	/**
	 * Epidemic: how many spreads in a row this floor has had, how many Plague Lords have
	 * risen on it, and whether the rule is carrying out its own mass kill right now.
	 * Issues #1820 and #41.
	 *
	 * THE CHAIN IS THE FLOOR'S AND NOT A CREATURE'S. It counts consecutive spreads, is
	 * set back to nothing by a kill that rolls and does not spread, and goes at the
	 * stairs. A kill of a creature carrying no spreadable debuff rolls nothing at all and
	 * leaves it where it is: not being diseased is not the same as a disease failing to
	 * pass on.
	 *
	 * THE FLAG IS WHAT STOPS THE CHAIN FEEDING ITSELF. See `EpidemicEndTheChain`.
	 */
	int32 EpidemicChain = 0;
	int32 EpidemicPlagueLordsRisen = 0;
	bool bEpidemicKilling = false;

	/**
	 * Blood-Forged Champions: how many deaths each Elite has taken since its last rung,
	 * and what this floor has seen. Issues #1820 and #41.
	 *
	 * THE TALLY OUTLIVES THE FLOOR AND THE TWO COUNTS DO NOT, which is the split stated
	 * above `VolatileEvolutionMutated`. A creature's progress towards its next rung is
	 * its own, in the way the rung it already reached is its own, so a champion that
	 * lives through a Horde dungeon's change of wave does not lose two thirds of a rung
	 * it had earned. The two counts answer what happened on THIS floor, which is what the
	 * panel shows, so they go at the stairs. Ruled under the project owner's delegation.
	 *
	 * WEAK POINTERS, AND THE LISTENER DROPS THE STALE ONES, as Royal Guard's record does:
	 * a creature that has died keeps its entry until the actor itself is destroyed, which
	 * costs nothing, and a destroyed one leaves nothing behind that could be read as a
	 * champion still standing.
	 */
	TMap<TWeakObjectPtr<ACataclysmEnemyCharacter>, int32> BloodForgedChampionsFed;
	int32 BloodForgedChampionsAbsorbed = 0;
	int32 BloodForgedChampionsRungsGained = 0;

	/**
	 * Vengeful Wraiths: which creatures are wraiths, and how many rose on this floor.
	 * Issues #1820 and #41.
	 *
	 * THE MEMBERSHIP OUTLIVES THE FLOOR AND THE COUNT DOES NOT, the split stated above
	 * `VolatileEvolutionMutated`. A wraith is a wraith wherever it goes, and a Horde
	 * dungeon's change of wave does not make it an ordinary creature again; the count
	 * answers what happened on THIS floor, which is what the panel shows.
	 *
	 * THE SET IS WHAT LETS A RUNG CHANGE PUT THE FIGURES BACK. Blood-Forged Champions and
	 * Volatile Evolution both raise a living creature's rung, and both end in
	 * `ApplyStartingAttributes`, which writes the whole stat block over. Without this set
	 * neither rule could tell a wraith from anything else it had just strengthened.
	 *
	 * WEAK POINTERS, AND THE LISTENER DROPS THE STALE ONES, as Royal Guard's record does.
	 */
	TSet<TWeakObjectPtr<ACataclysmEnemyCharacter>> VengefulWraiths;
	int32 VengefulWraithsRisen = 0;

	/**
	 * One death Divine Resurgence may raise: where it happened, which kind and at which
	 * rung. Issues #1820 and #41. Copied at the death notice, while the creature still
	 * exists, because the body is gone moments later.
	 */
	struct FDivineResurgenceGrave
	{
		FVector Location = FVector::ZeroVector;
		ECataclysmDungeonCreature Kind = ECataclysmDungeonCreature::Count;
		int32 RarityStep = 0;
	};

	/**
	 * Divine Resurgence: the graves waiting to rise, how many unmarked creatures have
	 * fallen, whether the floor's one revival has come, and how many rose in it. All of
	 * it is the floor's and goes at the stairs.
	 */
	TArray<FDivineResurgenceGrave> DivineResurgenceGraves;
	int32 DivineResurgenceFallen = 0;
	bool bDivineResurgenceDone = false;
	int32 DivineResurgenceRisen = 0;

	/** How many creatures Dead Rising has put back on this floor. Goes at the stairs. */
	int32 DeadRisingRisen = 0;

	/** How many unmarked creatures the player has slain on this floor. Goes at the stairs. */
	int32 BloodGatesSlain = 0;

	/** Infernal Seals: this floor's bearers, whether each has given its piece, and the pieces given. */
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> InfernalSealBearers;
	TArray<bool> InfernalSealBearerGave;
	int32 InfernalSealPieces = 0;

	/**
	 * Sanctioned Passage: this floor's gate; whether its channel has begun and the seconds channelled; each creature the
	 * channel called, with the sight it had; and what the panel last showed. Issues #1820 and #41.
	 */
	TWeakObjectPtr<class ACataclysmFloorObject> DivineGate;
	bool bDivineGateChannelling = false;
	float DivineGateSeconds = 0.0f;
	TMap<TWeakObjectPtr<ACataclysmEnemyCharacter>, float> DivineGateCalled;
	int32 DivineGatePanelKey = -1;

	/** Lightforged Walls: the count of the standing the panel last showed. Issues #1820 and #41. */
	int32 LightforgedWallsPanelCount = -1;

	/** Rule of Chaos: the seconds left its panel line last showed, so the panel is drawn again once a second. */
	int32 RuleOfChaosPanelSeconds = -1;

	/** Rule of Chaos: the player's kills on this floor that cleared the cooldowns. Goes at the stairs. */
	int32 RuleOfChaosKills = 0;

	/**
	 * Unstable Portal: its rolls on this floor and the last outcome. Both go at the stairs.
	 */
	int32 UnstablePortalRolls = 0;
	int32 UnstablePortalLast = -1;

	/**
	 * The creatures a rule raised mid-floor in answer to the player -- the Unstable Portal's
	 * Warden and Trick or Treat's pair -- which Blood Gates leaves out of its count, slain
	 * or standing, so they cannot seal again stairs the player had opened. Goes at the
	 * stairs. It held the portal's Wardens alone until Trick or Treat.
	 */
	TSet<TWeakObjectPtr<ACataclysmEnemyCharacter>> CreaturesRaisedByARule;

	/**
	 * Trick or Treat: the dungeon's clicked pickups and creatures raised, when the treat's
	 * haste ends in world seconds (negative for none), and the haste on the character.
	 */
	int32 TrickOrTreatPickups = 0;
	int32 TrickOrTreatRaised = 0;
	float TrickOrTreatHasteUntilSeconds = -1.0f;
	float TrickOrTreatHasteApplied = 0.0f;

	/** Wild Magic: what is drawn and waits a tick, where it is aimed, the count, the last one and the wait. */
	FName WildMagicPending;
	FVector WildMagicPendingAim = FVector::ZeroVector;
	int32 WildMagicTriggered = 0;
	FName WildMagicLast;
	float WildMagicNextAllowedSeconds = -1.0f;

	/** Echo Chamber: the skill to copy next tick, where the copy is aimed and how far that is, and the two counts. */
	FName EchoChamberPending;
	FVector EchoChamberPendingAim = FVector::ZeroVector;
	float EchoChamberPendingAimCm = 0.0f;
	int32 EchoChamberCopiesMade = 0;
	int32 EchoChamberHitsOnThePlayer = 0;

	/**
	 * Soul Harvest: each fed creature's souls and what they have added to it, and the souls
	 * given in this dungeon. The additions are kept so a later soul can find the creature's
	 * own figures under them.
	 */
	struct FSoulHarvestHeld
	{
		int32 Souls = 0;
		float HealthAdded = 0.0f;
		float DamageAdded = 0.0f;
		float ResistanceAdded = 0.0f;
	};
	TMap<TWeakObjectPtr<ACataclysmEnemyCharacter>, FSoulHarvestHeld> SoulHarvestHeld;
	int32 SoulHarvestGiven = 0;

	/**
	 * Chaos Touched: the stacks of each kind the dungeon has added, and what is on the
	 * character. Eight entries each, in `ChaosTouchedKindFor`'s order.
	 */
	TArray<int32> ChaosTouchedStacks = {0, 0, 0, 0, 0, 0, 0, 0};
	TArray<int32> ChaosTouchedApplied = {0, 0, 0, 0, 0, 0, 0, 0};

	/**
	 * The Reaper: the seconds this floor has run on the beat, whether it has come, and the
	 * creature. The floor's: all three go back when a floor begins.
	 */
	float TheReaperSecondsOnFloor = 0.0f;
	bool bTheReaperRaised = false;
	TWeakObjectPtr<ACataclysmEnemyCharacter> TheReaper;

	/**
	 * Blood Bond: the elite bonded on this floor, and whether this floor has bonded at all,
	 * which a revival does not undo. The floor's: both go back when a floor begins.
	 */
	TWeakObjectPtr<ACataclysmEnemyCharacter> BloodBonded;
	bool bBloodBondFormed = false;

	/**
	 * Plague Convergence: the floor's seconds on the beat, the seconds since the last wave, the
	 * creatures it sent, the disease's stacks and the seconds since its last burn. The floor's:
	 * all go back when a floor begins.
	 */
	float PlagueConvergenceSecondsOnFloor = 0.0f;
	float PlagueConvergenceSecondsSinceWave = 0.0f;
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> PlagueConvergenceCreatures;
	int32 PlagueConvergenceStacks = 0;
	float PlagueConvergenceSecondsSinceBurn = 0.0f;

	/**
	 * Divine Wrath: the seconds since the last beam, the beam now chasing, and the creatures
	 * the floor's beams destroyed. The floor's: all go back when a floor begins.
	 */
	float DivineWrathSecondsSinceLast = 0.0f;
	TWeakObjectPtr<class ACataclysmGroundZone> DivineWrathBeam;
	int32 DivineWrathDestroyed = 0;

	/** Echoes of the Past: one death that may come back, as recorded. */
	struct FEchoOfTheDead
	{
		ECataclysmDungeonCreature Kind = ECataclysmDungeonCreature::Count;
		int32 Rung = 0;
		int32 Attack = -2; // ACataclysmEnemyCharacter::NoAttackYet; the class is only declared here
	};

	/** An echo standing, and the attack it will make. */
	struct FEchoStanding
	{
		TWeakObjectPtr<ACataclysmEnemyCharacter> Echo;
		int32 Attack = -2; // ACataclysmEnemyCharacter::NoAttackYet; the class is only declared here
	};

	/**
	 * Echoes of the Past: this floor's deaths, the last `EchoesMost`; the last floor's, handed
	 * over as a floor begins; the echoes standing; the floor's seconds; and how far the echoes
	 * have got -- 0 waiting, 1 appeared, 2 struck, 3 gone.
	 */
	TArray<FEchoOfTheDead> EchoesThisFloor;
	TArray<FEchoOfTheDead> EchoesFromLastFloor;
	TArray<FEchoStanding> EchoesStanding;
	float EchoesSecondsOnFloor = 0.0f;
	int32 EchoesStage = 0;

	/** Plague Harbingers: one Harbinger, where it last laid a patch, and its patches, oldest first. */
	struct FPlagueHarbingerTrail
	{
		TWeakObjectPtr<ACataclysmEnemyCharacter> Harbinger;
		FVector LastPatchAt = FVector::ZeroVector;
		bool bHasLaidAPatch = false;
		TArray<TWeakObjectPtr<class ACataclysmGroundZone>> Patches;
	};

	/** Plague Harbingers: this floor's or wave's Harbingers and their trails. */
	TArray<FPlagueHarbingerTrail> PlagueHarbingerTrails;

	/** What the panel last said, so it is refreshed only when a count changes. */
	int32 PlagueHarbingersPanelAlive = -1;
	int32 PlagueHarbingersPanelPatches = -1;

	/** Wings of the Host: the marks counting down, the warning so far and the time since the last. */
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> WingsOfTheHostMarks;
	float WingsOfTheHostWarningSoFar = 0.0f;
	float WingsOfTheHostSecondsSinceLast = 0.0f;

	/** Eternal Chorus: one source and its earshot zone. */
	struct FEternalChorus
	{
		TWeakObjectPtr<ACataclysmEnemyCharacter> Source;
		TWeakObjectPtr<class ACataclysmGroundZone> Earshot;
	};

	/** Eternal Chorus: this arena's choruses, what is applied to the player, and what the panel said. */
	TArray<FEternalChorus> EternalChoruses;
	float EternalChorusCooldownApplied = 0.0f;
	float EternalChorusRegenApplied = 0.0f;
	int32 EternalChorusPanelSinging = -1;

	/** Necrotic Bloom: one flower, the time since it was placed or last sent a wave, and its waves. */
	struct FNecroticBloom
	{
		TWeakObjectPtr<ACataclysmEnemyCharacter> Flower;
		float SecondsSinceLastWave = 0.0f;
		int32 Waves = 0;
	};

	/** Necrotic Bloom: this arena's flowers, and how many the panel last said. */
	TArray<FNecroticBloom> NecroticBlooms;
	int32 NecroticBloomPanelFlowers = -1;

	/** Golden Spires: one spire and the zone drawn around it. */
	struct FGoldenSpire
	{
		TWeakObjectPtr<ACataclysmEnemyCharacter> Spire;
		TWeakObjectPtr<class ACataclysmGroundZone> Zone;
	};

	/** Golden Spires: this arena's spires, and how many the panel last said. */
	TArray<FGoldenSpire> GoldenSpires;
	int32 GoldenSpiresPanelStanding = -1;

	/** Pestilent Empowerment: one beacon, and whether it has been counted, so a Horde arena's counts once. */
	struct FPlagueBeacon
	{
		TWeakObjectPtr<ACataclysmEnemyCharacter> Beacon;
		bool bCounted = false;
	};

	/** Pestilent Empowerment: this arena's beacons, the count carried from earlier floors, and the panel's last. */
	TArray<FPlagueBeacon> PlagueBeacons;
	int32 PestilentBeaconsLeftStanding = 0;
	int32 PestilentPanelStanding = -1;

	/**
	 * Portal Unleashing: one portal, the zone drawn around it, the seconds since it last sent a creature, and
	 * the creatures it sent. Issues #1820 and #41.
	 */
	struct FVoidPortal
	{
		TWeakObjectPtr<ACataclysmEnemyCharacter> Portal;
		TWeakObjectPtr<class ACataclysmGroundZone> Zone;
		float SecondsSinceLastSent = 0.0f;
		TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> Sent;
	};

	/** Portal Unleashing: this arena's portals, and the creatures standing the panel last showed. */
	TArray<FVoidPortal> VoidPortals;
	int32 VoidPortalsPanelStanding = -1;

	/**
	 * Mind-Shattering Illusions: the phantasms, the clock, the slow's seconds left and what was last written on the
	 * player, and what the panel last showed. Issues #1820 and #41.
	 */
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> Phantasms;
	float IllusionSecondsSinceLast = 0.0f;
	float IllusionSlowLeft = 0.0f;
	float IllusionSlowApplied = 0.0f;
	int32 IllusionPanelKey = -1;

	/**
	 * Reality Rifts: this arena's rift cells and the zones drawn there, whether the gift has been taken, the seconds
	 * the rifts rest and the gift lasts, what was last written on the player, and what the panel last showed. Issues
	 * #1820 and #41.
	 */
	TArray<FIntPoint> RealityRiftCells;
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> RealityRiftZones;
	bool bRealityGiftTaken = false;
	float RealityRiftRestLeft = 0.0f;
	float RealityGiftLeft = 0.0f;
	float RealityGiftApplied = 0.0f;
	int32 RealityRiftPanelKey = -1;

	/** Luxury Hoarders: where the hoards lie, their guards, and how many drops the piles were laid with. */
	TArray<FVector> LuxuryHoards;
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> LuxuryHoardGuards;
	int32 LuxuryHoardDrops = 0;

	/**
	 * Insanity Bursts: the clock, the warning under way, which burst it will be (`InsanityBurstLocks`, `...Stuns` or
	 * `...Maddens`), the lock's and the madness's seconds left, the lock last written on the player, and what the panel
	 * last showed. Issues #1820 and #41.
	 */
	float InsanityBurstsSecondsSinceLast = 0.0f;
	bool bInsanityBurstsWarning = false;
	float InsanityBurstsWarningSoFar = 0.0f;
	int32 InsanityBurstsKind = 0;
	float InsanityBurstsLockLeft = 0.0f;
	float InsanityBurstsMaddenedLeft = 0.0f;
	float InsanityBurstsLockApplied = 0.0f;
	int32 InsanityBurstsPanelSecond = -1;

	/**
	 * Funereal Procession: the line crossing now, where it walks, its two clocks and its contact's, and what the panel
	 * last showed. Issues #1820 and #41.
	 */
	TWeakObjectPtr<class ACataclysmGroundZone> FunerealProcession;
	FVector FunerealProcessionVelocity = FVector::ZeroVector;
	float FunerealProcessionSecondsIntoIt = 0.0f;
	float FunerealProcessionSecondsSinceLast = 0.0f;
	float FunerealProcessionSecondsSinceBurn = 0.0f;
	int32 FunerealProcessionPanelSecond = -1;

	/**
	 * Blood Debt: the kills paid in this dungeon, and the blessings and curse last written on the player. Issues
	 * #1820 and #41.
	 */
	int32 BloodDebtPaid = 0;
	int32 BloodDebtBlessingsApplied = 0;
	bool bBloodDebtCurseApplied = false;

	/**
	 * Quarantine Breach: the containment, the kind it holds, whether it has been broken, what it released, and the
	 * patches they left. Issues #1820 and #41.
	 */
	TWeakObjectPtr<ACataclysmEnemyCharacter> Quarantine;
	ECataclysmDungeonCreature QuarantineHeldKind = ECataclysmDungeonCreature::Imp;
	bool bQuarantineBroken = false;
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> QuarantineReleased;
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> QuarantinePatches;

	/**
	 * Infection Bloom: the bloom and where it stood, where its patches are and the zones drawn for them, its two
	 * clocks, the creatures its waves sent, whether it has been destroyed, and what the panel last showed. Issues
	 * #1820 and #41.
	 */
	TWeakObjectPtr<ACataclysmEnemyCharacter> InfectionBloom;
	FVector InfectionBloomWhere = FVector::ZeroVector;
	TArray<FVector> InfectionBloomPatchPoints;
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> InfectionBloomPatches;
	float InfectionBloomSecondsSincePatch = 0.0f;
	float InfectionBloomSecondsSinceWave = 0.0f;
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> InfectionBloomWaveCreatures;
	bool bInfectionBloomDestroyed = false;
	int32 InfectionBloomPanelPatches = -1;

	/**
	 * The Infested Hoard: the Infestation stacks the player holds, the seconds since the last drain, and what the
	 * panel last showed. Issues #1820 and #41.
	 */
	int32 InfestedHoardStacks = 0;
	float InfestedHoardSecondsSinceDrain = 0.0f;
	int32 InfestedHoardPanelStacks = -1;

	/**
	 * Abyssal Rifts: this floor's rift and its zone, its state, how long it has been open, the waves it has sent and
	 * the creatures in them, the dungeon's successes and what was last written on the player, and what the panel
	 * last showed. Issues #1820 and #41.
	 */
	TWeakObjectPtr<ACataclysmEnemyCharacter> AbyssalRift;
	TWeakObjectPtr<class ACataclysmGroundZone> AbyssalRiftZone;
	ERiftState AbyssalRiftState = ERiftState::Waiting;
	float AbyssalRiftSecondsOpen = 0.0f;
	int32 AbyssalRiftWavesSent = 0;
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> AbyssalRiftCreatures;
	int32 AbyssalRiftSuccesses = 0;
	int32 AbyssalRiftSuccessesApplied = 0;
	int32 AbyssalRiftPanelSecond = -1;

	/**
	 * Swarm of Locusts: the swarm on the floor, its travel, how long since it appeared and since the last one
	 * ended, the burn's clock, the shelters' cells and zones, and what the panel last showed. Issues #1820 and #41.
	 */
	TWeakObjectPtr<class ACataclysmGroundZone> SwarmOfLocusts;
	FVector SwarmOfLocustsVelocity = FVector::ZeroVector;
	bool bSwarmOfLocustsTravelling = false;
	float SwarmOfLocustsSecondsIntoIt = 0.0f;
	float SwarmOfLocustsSecondsSinceLast = 0.0f;
	float SwarmOfLocustsSecondsSinceBurn = 0.0f;
	TArray<FIntPoint> LocustShelterCells;
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> LocustShelters;
	int32 SwarmOfLocustsPanelSecond = -1;

	/**
	 * Warzone Control Points: this arena's point cells and the zones drawn there, the seconds stood in each, which
	 * are captured, the wave clock, the creatures the waves sent, the points last written on the player, and what the
	 * panel last showed. Issues #1820 and #41.
	 */
	TArray<FIntPoint> WarzonePointCells;
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> WarzonePointZones;
	TArray<float> WarzoneSecondsHeld;
	TArray<bool> WarzoneCaptured;
	float WarzoneWaveClock = 0.0f;
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> WarzoneAttackers;
	int32 WarzonePointsApplied = 0;
	int32 WarzonePanelKey = -1;

	/** Warzone Control Points: the allied soldiers this floor's captured points brought. Issues #1820 and #41. */
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> WarzoneAllies;

	/**
	 * Raw Sewage: where this arena's river marks stand and the marks drawn there; the dungeon's stacks; whether
	 * the player stood in a river on the last beat, and for how long; the burn's clock; whether the disease tag
	 * is on the player; and what the panel last showed. Issues #1820 and #41.
	 */
	TArray<FVector> RawSewageMarkPoints;
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> RawSewageMarks;
	int32 RawSewageStacks = 0;
	bool bRawSewageInARiver = false;
	float RawSewageSecondsInARiver = 0.0f;
	float RawSewageSecondsSinceBurn = 0.0f;
	bool bRawSewageTagged = false;
	int32 RawSewagePanelStacks = -1;

	/**
	 * Demonic Guide: the guide, the zone drawn as its chain, the damage taken last written on the player, and what
	 * the panel last showed. Issues #1820 and #41.
	 */
	TWeakObjectPtr<ACataclysmEnemyCharacter> DemonicGuide;
	TWeakObjectPtr<class ACataclysmGroundZone> DemonicGuideChain;
	float DemonicGuideApplied = 0.0f;
	int32 DemonicGuidePanelKey = -1;

	/**
	 * The Plaguebearer, its stacks, and the floor they belong to. Issues #1820 and #41. THE FLOOR NUMBER IS WHAT STARTS
	 * THEM AGAIN, so the reset does not depend on which of the floor's resets runs first.
	 */
	TWeakObjectPtr<ACataclysmEnemyCharacter> Plaguebearer;
	int32 PlaguebearerStacks = 0;
	float PlaguebearerSecondsSinceStack = 0.0f;
	int32 PlaguebearerFloor = -1;
	bool bPlaguebearerChosen = false;
	bool bPlaguebearerFallen = false;
	int32 PlaguebearerPanelKey = -1;

	/**
	 * The groups the floor's populations were placed in, counted up and never reset, so a Horde arena's later wave never
	 * reuses an earlier wave's number while that wave's creatures still stand. And the population now arriving: the first
	 * number it was given and each of its groups' middle cells. Issues #1820 and #41, for Morale Break.
	 */
	int32 PackGroupsPlaced = 0;
	int32 ArrivingPackGroupBase = 0;
	TArray<FIntPoint> ArrivingPackSites;

	/** Morale Break: one group with a leader, and what has happened to it. Issues #1820 and #41. */
	struct FMoraleBreakGroup
	{
		int32 Group = INDEX_NONE;
		FIntPoint Middle = FIntPoint(-1, -1);
		TWeakObjectPtr<ACataclysmEnemyCharacter> Leader;
		bool bLeaderRallies = false;
		bool bFallen = false;
		float SecondsSinceFall = 0.0f;
		TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> Panicked;
		bool bFlightOver = false;
		/** Each escaped creature's kind and rung. */
		TArray<TPair<ECataclysmDungeonCreature, int32>> Escaped;
		float SecondsAway = 0.0f;
		bool bReturned = false;
	};

	/**
	 * Morale Break's groups, and the floor they belong to. THE FLOOR NUMBER IS WHAT STARTS THEM AGAIN, so the reset does
	 * not depend on which of the floor's resets runs first, and the escaped are forgotten on leaving the floor.
	 */
	TArray<FMoraleBreakGroup> MoraleBreakGroups;
	int32 MoraleBreakFloor = -1;
	int32 MoraleBreakPanelKey = -1;

	/**
	 * Famished Beasts: the drops eaten on the floor, the floor they belong to, and the maximum health this rule has added
	 * to each eater that is not a Carrion feeder, as Soul Harvest keeps what its souls added. THE FLOOR NUMBER STARTS
	 * THEM AGAIN. Issue #41.
	 */
	int32 FamishedBeastsDropsEaten = 0;
	int32 FamishedBeastsFloor = -1;
	TMap<TWeakObjectPtr<ACataclysmEnemyCharacter>, float> FamishedBeastsHealthAdded;

	/** Infested Veins: one vein's cell, the vein, its zone, and the seconds since it was destroyed (-1 alive). */
	struct FInfestedVein
	{
		FIntPoint Cell = FIntPoint(-1, -1);
		TWeakObjectPtr<ACataclysmEnemyCharacter> Vein;
		TWeakObjectPtr<class ACataclysmGroundZone> Zone;
		float SecondsSinceDestroyed = -1.0f;
	};

	/** Infested Veins: this arena's veins, its destroyed count, whether the guardians came, the burn clock. */
	TArray<FInfestedVein> InfestedVeins;
	int32 InfestedVeinsDestroyed = 0;
	bool bInfestedVeinsGuardiansCame = false;
	float InfestedVeinsSecondsSinceBurn = 0.0f;
	int32 InfestedVeinsPanelStanding = -1;
	int32 InfestedVeinsPanelDestroyed = -1;

	/**
	 * Carrion Feast: the carcasses and the seconds each has lain (below zero once burned), the feeders and the
	 * maximum health each had when it came, the carcasses eaten on this floor, and what the panel last showed.
	 * Issues #1820 and #41.
	 */
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> CarrionCarcasses;
	TArray<float> CarrionCarcassSeconds;
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> CarrionFeeders;
	TArray<float> CarrionFeederOwnMaxHealth;
	int32 CarrionFeastStacks = 0;
	int32 CarrionFeastPanelKey = -1;

	/** Every floor: seconds since it was placed, and when it was first found cleared (-1 not yet). */
	float FloorSecondsSincePlaced = 0.0f;
	float FloorClearedSeconds = -1.0f;

	/**
	 * What the rules have written on one creature's all-resistance: its own base, what was last written in its
	 * place, and each rule's points and multiplier under the rule's own key. See `SetRuleResistance`.
	 */
	struct FRuleResistance
	{
		float Own = 0.0f;
		float Applied = 0.0f;
		TMap<FName, float> AddedBySource;
		TMap<FName, float> MultipliedBySource;
	};

	/** Every creature a rule has written a resistance on this floor or wave. */
	TMap<TWeakObjectPtr<ACataclysmEnemyCharacter>, FRuleResistance> RuleResistances;

	/** Trial of Endurance: its clock, how it ended, and what the panel last said. */
	float TrialSeconds = 0.0f;
	bool bTrialClearedInTime = false;
	bool bTrialRanOut = false;
	int32 TrialPanelLiving = -1;

	/** Obsidian Sarcophagi: one coffin, the zone drawn around it, the paid deaths beside it, and its lord. */
	struct FSarcophagus
	{
		TWeakObjectPtr<ACataclysmEnemyCharacter> Coffin;
		TWeakObjectPtr<class ACataclysmGroundZone> Zone;
		int32 Deaths = 0;
		bool bLordCame = false;
	};

	/** Obsidian Sarcophagi: this arena's coffins. */
	TArray<FSarcophagus> Sarcophagi;

	/**
	 * Void Parasite: the voidlings standing, the stacks the player carries and what was last put on the
	 * character, the light zone's cell and zone, and what the panel last showed. Issues #1820 and #41.
	 */
	TSet<TWeakObjectPtr<ACataclysmEnemyCharacter>> Voidlings;
	int32 VoidParasiteStacks = 0;
	int32 VoidParasiteStacksApplied = 0;
	FIntPoint VoidParasiteLightCell = FIntPoint(-1, -1);
	TWeakObjectPtr<class ACataclysmGroundZone> VoidParasiteLight;
	int32 VoidParasitePanelStacks = -1;

	/**
	 * Grim Totems: the totems and the zones drawn under them, the Elite creatures embracing brought, an embrace's
	 * seconds left and what was last written on the player, and what the panel last showed. Issues #1820 and #41.
	 */
	TArray<TWeakObjectPtr<class ACataclysmFloorObject>> GrimTotems;
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> GrimTotemZones;
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> GrimTotemElites;
	float GrimEmbraceLeft = 0.0f;
	float GrimEmbraceApplied = 0.0f;
	int32 GrimTotemsPanelKey = -1;

	/**
	 * Battlefield Relics: the relics standing and each one's kind, the spirits activating brought, each kind's seconds
	 * left and what was last written on the player, and what the panel last showed. Issues #1820 and #41.
	 */
	TArray<TWeakObjectPtr<class ACataclysmFloorObject>> BattlefieldRelics;
	TArray<int32> BattlefieldRelicKinds;
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> RelicSpirits;
	float RelicFuryLeft = 0.0f;
	float RelicHasteLeft = 0.0f;
	float RelicBulwarkLeft = 0.0f;
	float RelicFuryApplied = 0.0f;
	float RelicHasteApplied = 0.0f;
	float RelicBulwarkApplied = 0.0f;
	int32 BattlefieldRelicsPanelKey = -1;

	/** Pandora's Box: one opened box's waves, where they come from, how many have come, and the wave standing. */
	struct FPandorasBoxWaves
	{
		FVector At = FVector::ZeroVector;
		int32 Came = 0;
		TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> Standing;
	};

	/**
	 * Pandora's Box: the boxes standing, each box's seed for its reward, the waves under way, the drops the last reward
	 * gave, and what the panel last showed. Issues #1820 and #41.
	 */
	TArray<TWeakObjectPtr<class ACataclysmFloorObject>> PandorasBoxes;
	TArray<int32> PandorasBoxSeeds;
	TArray<FPandorasBoxWaves> PandorasBoxWaves;
	int32 PandorasBoxRewardDrops = 0;
	int32 PandorasBoxPanelKey = -1;

	/**
	 * Carrion Feast's purification altar: the altar standing, whether it was consecrated and where, and the zone drawn
	 * there. Issues #1820 and #41.
	 */
	TWeakObjectPtr<class ACataclysmFloorObject> PurificationAltar;
	bool bAltarConsecrated = false;
	FVector AltarAt = FVector::ZeroVector;
	TWeakObjectPtr<class ACataclysmGroundZone> AltarZone;

	/**
	 * Infernal Beacons: the beacons standing, the stacks activated in this dungeon, what was last written on the player,
	 * and what the panel last showed. Issues #1820 and #41. THE STACKS ARE THE DUNGEON'S, cleared on leaving it.
	 */
	TArray<TWeakObjectPtr<class ACataclysmFloorObject>> InfernalBeacons;
	int32 InfernalBeaconStacks = 0;
	int32 InfernalBeaconStacksApplied = 0;
	int32 InfernalBeaconsPanelKey = -1;

	/**
	 * War Banner: the banner to be planted, whether it is planted and where, the seconds held and since the last wave,
	 * whether it is held, the zone, the creatures its waves brought, what was last written on the player, and what the
	 * panel last showed. Issues #1820 and #41.
	 */
	TWeakObjectPtr<class ACataclysmFloorObject> WarBanner;
	bool bWarBannerPlanted = false;
	FVector WarBannerAt = FVector::ZeroVector;
	float WarBannerHeldSeconds = 0.0f;
	float WarBannerSecondsSinceWave = 0.0f;
	bool bWarBannerHeld = false;
	TWeakObjectPtr<class ACataclysmGroundZone> WarBannerZone;
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> BannerAssailants;
	float WarBannerDamageApplied = 0.0f;
	float WarBannerResistanceApplied = 0.0f;
	int32 WarBannerPanelKey = -1;

	/**
	 * Forced Tithes: this floor's altar; whether one was placed, and whether its tithe was paid or refused; whether
	 * angels are owed at the next beat for an altar left unpaid; the angels brought; and what the panel last showed.
	 * Issues #1820 and #41.
	 */
	TWeakObjectPtr<class ACataclysmFloorObject> TitheAltar;
	bool bTitheAltarPlaced = false;
	bool bTithePaid = false;
	bool bTitheRefused = false;
	bool bTitheAngelsDue = false;
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> TitheAngels;
	int32 TithePanelKey = -1;

	/**
	 * Pact of Temptation: this floor's altar and what it offers, and the last floor's offer; the buff of the pact taken
	 * on the floor before, for this floor, and the one taken on this floor, for the next; how many of each pact the
	 * dungeon has taken, and in all; what was last written on the player, and whether it was written since the last
	 * floor change replaced the player's floor modifiers; and what the panel last showed. Issues #1820 and #41.
	 */
	TWeakObjectPtr<class ACataclysmFloorObject> PactAltar;
	TArray<int32> PactOffered;
	TArray<int32> PactLastOffered;
	int32 PactBuffNow = INDEX_NONE;
	int32 PactBuffNext = INDEX_NONE;
	TArray<int32> PactCurseCounts = {0, 0, 0, 0, 0};
	int32 PactsTaken = 0;
	int32 PactBuffApplied = INDEX_NONE;
	TArray<int32> PactCursesApplied = {0, 0, 0, 0, 0};
	bool bPactWritten = false;
	int32 PactPanelKey = -1;

	/**
	 * Blood Price: the bleed stacks this dungeon has left, the clock to the next second's bleed, whether the player
	 * carries the bleed keyword, and what the panel last showed. The dungeon's, cleared on leaving it. Issues #1820, #41.
	 */
	int32 BloodPriceStacks = 0;
	float BloodPriceSecondsSinceBleed = 0.0f;
	bool bBloodPriceTagged = false;
	int32 BloodPricePanelStacks = -1;

	/**
	 * Nothing Is Forgotten: what the void holds, the boss it fed, and what it added to that
	 * boss. The dungeon's, not the floor's: emptied when the player leaves the dungeon.
	 */
	float NothingIsForgottenHealth = 0.0f;
	float NothingIsForgottenDamage = 0.0f;
	TWeakObjectPtr<ACataclysmEnemyCharacter> NothingIsForgottenBoss;
	float NothingIsForgottenHealthGiven = 0.0f;
	float NothingIsForgottenDamageGiven = 0.0f;

	/**
	 * The starvation curse: the stacks of each kind the dungeon has added, and what is on the
	 * character. Two numbers per kind for the reason `WastingSicknessStacks` has two.
	 */
	int32 StarvationCurseMovementStacks = 0;
	int32 StarvationCurseHealthStacks = 0;
	int32 StarvationCurseMovementApplied = 0;
	int32 StarvationCurseHealthApplied = 0;

	/**
	 * Judgment Zones: the ground standing now, the clock that lays more, and what the
	 * player has taken from it. Issues #1820 and #41.
	 *
	 * ALL OF IT IS THE FLOOR'S AND GOES AT THE STAIRS, unlike the records above. A zone is
	 * an actor on this floor and a trigger is something that happened on this floor; there
	 * is nothing here a creature carries with it.
	 *
	 * `JudgmentZonesStandingIn` IS WHAT MAKES LEAVING RESET THE RAMP. The ramp is per
	 * zone, so stepping from one zone straight into another starts again -- which is what
	 * "standing inside ramps" means, and is why the zone is remembered rather than just a
	 * yes or no.
	 *
	 * `JudgmentZonesSecondsInside` CARRIES THE REMAINDER BETWEEN BEATS. The beat is a
	 * quarter-second and the ramp is per second, so four beats make one tick.
	 */
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> JudgmentZones;
	float JudgmentZonesSecondsSinceLastZone = 0.0f;
	TWeakObjectPtr<class ACataclysmGroundZone> JudgmentZonesStandingIn;
	float JudgmentZonesSecondsInside = 0.0f;
	int32 JudgmentZonesTicksInThisZone = 0;
	int32 JudgmentZonesTriggers = 0;

	/**
	 * March of Progress: which creature is this floor's Commander, whether one has been
	 * chosen, whether the player has killed it, how many the player has killed in this
	 * run, and how much armour is standing on the player for them.
	 * Issues #1820 and #41.
	 *
	 * THE COUNT IS THE ONLY FIELD HERE THAT OUTLIVES THE FLOOR, and it is the one the row
	 * asks to outlive it: the armour is paid "in each level" and nothing in the row takes
	 * it back. `LeaveEmpireDungeon` clears it, which is where a run ends.
	 *
	 * `bMarchOfProgressCommanderSlain` MEANS THIS FLOOR'S COMMANDER HAS DIED, WHOEVER
	 * KILLED IT -- not that the player was paid for it. A Horde dungeon's next wave
	 * arrives in the same arena and calls the chooser again, and the weak pointer alone
	 * cannot answer whether this floor has already had its Commander: a creature that
	 * died and one destroyed for any other reason both leave it invalid, and a creature
	 * that has died but whose body is still standing leaves it VALID. The chooser returns
	 * early on either the living pointer or this flag.
	 *
	 * BOTH ARE FORGOTTEN IN `PopulateFloor` AND NOT IN `ApplyFloorRulesToPlayer`, AND
	 * THAT ORDER IS LOAD-BEARING. `GoToFloor` populates the floor and applies the floor
	 * rules afterwards, so clearing them in the applier would wipe the Commander the
	 * population pass had just chosen and every floor would have none.
	 *
	 * `MarchOfProgressArmourApplied` IS WHAT IS ON THE CHARACTER, NOT WHAT IS OWED.
	 * `ApplyFloorRulesToPlayer` replaces the floor's modifiers wholesale on a floor
	 * change, which takes the armour off, so that function sets this back to nothing and
	 * the next beat notices the difference and puts the armour back. Issue #41's slice 2
	 * built that shape for The Nihil's Embrace and its comment there says the same.
	 */
	TWeakObjectPtr<ACataclysmEnemyCharacter> MarchOfProgressCommander;
	bool bMarchOfProgressCommanderSlain = false;
	int32 MarchOfProgressCommandersKilled = 0;
	float MarchOfProgressArmourApplied = 0.0f;

	/**
	 * Commander's Aura: how many creatures on this floor commanded on the last beat.
	 * Issues #1820 and #41.
	 *
	 * FOR THE FLOOR PANEL AND NOTHING ELSE. The rule itself needs no memory between
	 * beats: it re-applies the buff every beat and the buff expires on its own, so this
	 * count is written for the player to read rather than for the rule to act on.
	 *
	 * NOT THE SAME AS ANYTHING MARCH OF PROGRESS HOLDS, DESPITE THE WORD. That rule's
	 * `MarchOfProgressCommander` is the ONE creature the player is asked to hunt; this is
	 * how many creatures are buffing their neighbours. A floor can carry both rows, so
	 * every identifier on either side names its own rule.
	 */
	int32 CommandersAuraCommanders = 0;

	/**
	 * Anti-Magic Zones: the zones standing now, the clock that lays more, and the spell
	 * lock this rule last put on the player. Issues #1820 and #41.
	 *
	 * ALL OF IT IS THE FLOOR'S AND GOES AT THE STAIRS, in Singularity Wells' shape. A
	 * zone is an actor on this floor, and the floor change's own apply has already taken
	 * the lock off the character.
	 *
	 * NO TEST CAN SEE THE RESET OF `AntiMagicZonesLockApplied`, AND THAT WAS MEASURED BY
	 * READING, NOT ASSUMED. A stale value would be corrected on the first beat after the
	 * stairs: no zone can stand on a new floor for its first eight seconds, so the player
	 * is outside every zone on that beat, the lock wanted is nothing, and it differs from
	 * the stale one, so the apply runs. The reset is kept so the field never holds a
	 * figure that is not on the character, which is what every field beside it means.
	 */
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> AntiMagicZones;
	float AntiMagicZonesSecondsSinceLastZone = 0.0f;
	float AntiMagicZonesLockApplied = 0.0f;

	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> FungalOvergrowthBoostMushrooms;
	TArray<TWeakObjectPtr<class ACataclysmGroundZone>> FungalOvergrowthSlowMushrooms;
	float FungalOvergrowthSpeedMoreApplied = 0.0f;
	float FungalOvergrowthSpeedLessApplied = 0.0f;

	/**
	 * The arriving wave's creatures that are not on the floor yet, in the order
	 * the population pass placed them, so the first placed is the first to
	 * arrive. Issue #1544; see `WaveCreaturesPerFrame`.
	 *
	 * PLAIN DATA, NOT A `UPROPERTY`, for the reason `FloorBrief` gives: a
	 * placement holds no object, and Unreal's header tool refuses a `UPROPERTY`
	 * of an unreflected struct.
	 */
	TArray<FCataclysmEnemyPlacement> WaveStillToArrive;

	/**
	 * What the arriving wave's creatures notice from, as a multiple of their own
	 * distance, taken when the wave began arriving.
	 *
	 * TAKEN THEN AND NOT READ OFF `FloorBrief` LATER, because the brief belongs
	 * to the floor being stood on, and a new floor's brief is written before its
	 * creatures are put down. A wave still arriving when the next one is brought
	 * in finishes with the figure it began with.
	 */
	float ArrivingSightRadiusMultiplier = 1.0f;

	/**
	 * Puts up to `WaveCreaturesPerFrame` more of the arriving wave on the floor,
	 * and returns how many it put down.
	 *
	 * CALLED FROM `Tick` EVERY FRAME, and from `PopulateFloor` in the frame a
	 * wave begins, so a wave's first creatures appear in the frame it arrives.
	 *
	 * ALWAYS SHORTENS THE QUEUE, or empties it when there is no floor to put
	 * anything on, so a loop that calls it until nothing is left always ends.
	 *
	 * PRIVATE ON PURPOSE. A test drives `Tick`, which is what drives this in
	 * play; a test that called this directly would prove the spawning and not
	 * that anything in the game ever does it.
	 */
	int32 ContinueTheWaveArriving();

	/**
	 * Spawns one placed creature on the current floor and gives it everything
	 * the floor decides about it: its designed numbers, its rarity, its standing
	 * height and what it notices from. Null when it could not be spawned.
	 *
	 * ONE FUNCTION FOR BOTH WAYS A CREATURE ARRIVES -- all of an ordinary floor
	 * at once, and a wave a few at a time -- so the two cannot drift apart.
	 *
	 * `FixedRung` PUTS IT ON A RUNG RATHER THAN DRAWING ONE. Celestial Divine Resurgence
	 * passes it to bring each creature back at the rung it died at, Necrotic Bloom to raise
	 * Commons, and Morale Break to bring the escaped back at their own rungs with Common
	 * reinforcements. It is passed down to `ApplyDesignedStats` so the rung is set BEFORE the
	 * creature's modifiers are drawn: setting it afterwards would leave the modifiers
	 * of whatever rung was drawn first, because drawing only ever adds. Issues #1820
	 * and #41. Left at `RollTheRarity`, every other caller's behaviour is unchanged.
	 */
	ACataclysmEnemyCharacter* SpawnPlacedCreature(
		const FCataclysmEnemyPlacement& Placement, float SightRadiusMultiplier,
		int32 FixedRung = -1);

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
