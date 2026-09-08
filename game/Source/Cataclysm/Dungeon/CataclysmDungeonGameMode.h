// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/CataclysmGameMode.h"
#include "Dungeon/CataclysmFloorBrief.h"
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
	 * IT COSTS AN ORDINARY DUNGEON A COMPARISON AND NOTHING ELSE.
	 * `ShouldTheNextWaveArrive` answers false immediately for a floor that is
	 * not a wave.
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
	 * How many creatures the current wave put on the floor when it arrived.
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
	/**
	 * Seconds since the wave was last looked at.
	 *
	 * PRIVATE, unlike the wave state above, because it is bookkeeping for the
	 * tick and says nothing about the dungeon. A test drives
	 * `BringTheNextWaveIn` or ticks this actor rather than reading it.
	 */
	float SinceWaveCheckSeconds = 0.0f;

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
