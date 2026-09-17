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
	 */
	TMap<FName, FString> LiveCountsForTheFloor() const;

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
	 */
	ACataclysmEnemyCharacter* SpawnPlacedCreature(
		const FCataclysmEnemyPlacement& Placement, float SightRadiusMultiplier);

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
