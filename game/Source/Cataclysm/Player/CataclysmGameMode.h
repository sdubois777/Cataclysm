// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CataclysmGameMode.generated.h"

/**
 * Names the pawn, controller and player state the game runs with.
 *
 * GameModeBase rather than GameMode: the match state machine GameMode adds --
 * waiting to start, in progress, waiting post match -- describes a session with
 * rounds. This game has a run that ends when the capital falls or the boss dies,
 * and the empire layer owns that. There is no round to wait for.
 *
 * Set as the project default in Config/DefaultEngine.ini rather than per level,
 * so a level opened directly still gets the real player pawn.
 */
class ACataclysmEnemyCharacter;

UCLASS(Config = Game)
class CATACLYSM_API ACataclysmGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACataclysmGameMode();

	virtual void StartPlay() override;

	/**
	 * Puts training dummies in a ring around the player start.
	 *
	 * Public so a test can call it against a world it built, and so it can be
	 * called again from the console while playing.
	 *
	 * @return how many were spawned
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Sandbox")
	int32 SpawnTrainingDummies();

	/** The dummies this game mode spawned, oldest first. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Sandbox")
	TArray<TObjectPtr<ACataclysmEnemyCharacter>> TrainingDummies;

	/**
	 * Puts Brutes in the sandbox, beyond where the ring of training dummies
	 * would be. Public for the same reasons SpawnTrainingDummies is.
	 *
	 * SEPARATE FROM SpawnTrainingDummies RATHER THAN A PARAMETER ON IT, so the
	 * dummy ring's settled behaviour is untouched by adding a second kind of
	 * enemy. Returns how many were placed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Sandbox")
	int32 SpawnBrutes();

	/** Every Brute this game mode placed. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Sandbox")
	TArray<TObjectPtr<class ACataclysmBruteCharacter>> Brutes;

	/**
	 * Puts Abyssal Wardens in the sandbox, on the same arrangement as the
	 * Brutes and for the same reasons. Returns how many were placed.
	 *
	 * IT CANNOT CHASE, which changes what watching it is like. Its designed
	 * movement speed is 2.8 metres per second and it has no chase speed at all,
	 * against player classes at 3.5, 4.0 and 4.6, so a player who walks
	 * backwards is never caught. Its charge, which is what the design gives it
	 * to close with, cannot be executed by the current brain -- issue #491.
	 * Stand still to see it fight.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Sandbox")
	int32 SpawnAbyssalWardens();

	/**
	 * Puts Hellhounds in the sandbox. Returns how many were placed.
	 *
	 * IT IS THE OPPOSITE OF THE ABYSSAL WARDEN, AND THAT CHANGES WHAT WATCHING
	 * IT IS LIKE. It moves at 7.5 metres per second against player classes at
	 * 3.5, 4.0 and 4.6, so walking away from it does not work. It is the first
	 * creature in this project designed to catch the player rather than to be
	 * escaped, and the instinct the other two teach is the wrong one to bring.
	 *
	 * IT IS PLACED ON THE FAR SIDE OF THE PLAYER START FROM THE OTHER TWO. See
	 * `HellhoundBearingDegrees` for why that is required rather than tidy.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Sandbox")
	int32 SpawnHellhounds();

	/**
	 * Puts a pack of Imps in the sandbox. Returns how many were placed.
	 *
	 * A PACK RATHER THAN A CREATURE, WHICH NO OTHER SPAWNER HERE DOES. The other
	 * three place one each and spread several around a circle if asked for more.
	 * This one places ten in a cluster of their own, because ten is what
	 * `docs/Cataclysm_GDD_v2.md` calls a pack and because one Imp shows almost
	 * nothing: the design's own line is that a single Common enemy is not the
	 * threat, a pack is. One Imp takes 48 seconds to kill a geared character;
	 * ten take 4.9.
	 *
	 * THEY WILL NOT QUEUE INTO RINGS YET. The ring behaviour the design
	 * describes is what crowd avoidance produces, and this project has none at
	 * all: issue #761. Until that lands they will shove rather than surround.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Sandbox")
	int32 SpawnImps();

	/**
	 * Puts Corrupted Sentinels in the sandbox. Returns how many were placed.
	 *
	 * **IT CANNOT MOVE AT ALL**, which is the first thing anybody watching it
	 * will notice and is the whole creature rather than a defect. It shoots 14
	 * metres, which is further than anything else in the game reaches, and the
	 * design's line about it is "forces the player to stay mobile". Walk into
	 * its range and it fires every two seconds; walk out and it can do nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Sandbox")
	int32 SpawnCorruptedSentinels();

	/**
	 * Tell the save writer which run and which character this session is
	 * playing, which is what makes the game start saving itself.
	 *
	 * NOTHING IS WRITTEN UNTIL THIS RUNS. A record is stored in a slot named
	 * after a generated identifier, and until one is supplied there is no slot.
	 * `docs/Save_System_Design.md` section 6, set by the project owner on
	 * 2026-08-20: the game saves itself, often, and there is no manual save.
	 *
	 * A FRESH RUN EVERY SESSION, AND NOTHING EVER READS IT BACK. There is no
	 * new-game or continue flow, so a run begun here is written to a slot named
	 * after an identifier generated a moment ago and forgotten when the session
	 * ends. **That is the honest state of the save system**: the writing half is
	 * built and the choosing half is not. Issue #753.
	 *
	 * PUBLIC SO A TEST CAN CALL IT, which is the same reason the three spawners
	 * above are public. `StartPlay` cannot be called from an automation test --
	 * it wants a player controller and a pawn -- so a call left inside it would
	 * be the one line turning this whole feature on with nothing covering it.
	 *
	 * @return whether the writer was found and told
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Save")
	bool BeginSavingThisRun();

	/**
	 * The rung a creature spawns at, given what its sandbox setting holds.
	 *
	 * NOT A UFUNCTION. Nothing in Blueprint calls it, and it takes a raw actor
	 * pointer, which is what all three spawners already have in hand.
	 *
	 * @param Setting  a rung from 0 to 5, or UCataclysmEnemyRarity::RollTheRarity
	 *                 to draw one from the spawn weights
	 * @param Spawned  the creature, which seeds its own draw so that two made in
	 *                 the same frame do not come out the same
	 *
	 * @return Common when there is no world or no creature, which is what every
	 *         creature spawned as before anything drew a rarity at all
	 */
	int32 RarityStepFor(int32 Setting, const AActor* Spawned) const;

	/** Every Abyssal Warden this game mode placed. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Sandbox")
	TArray<TObjectPtr<class ACataclysmAbyssalWardenCharacter>> AbyssalWardens;

	/** Every Hellhound this game mode placed. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Sandbox")
	TArray<TObjectPtr<class ACataclysmHellhoundCharacter>> Hellhounds;

	/** Every Imp this game mode placed. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Sandbox")
	TArray<TObjectPtr<class ACataclysmImpCharacter>> Imps;

	/** Every Corrupted Sentinel this game mode placed. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Sandbox")
	TArray<TObjectPtr<class ACataclysmCorruptedSentinelCharacter>>
		CorruptedSentinels;

	// ----------------------------------------------------------------------
	// Difficulty
	// ----------------------------------------------------------------------

	/** The lowest and highest difficulty tier. `DIFFICULTY_TIERS` is 8 in
	 *  `sim/cataclysm_sim/affixes.py`, and `UCataclysmPowerScore::TierAnchors`
	 *  already carries one score anchor per tier plus an unused entry 0. */
	static constexpr int32 LowestDifficultyTier = 1;
	static constexpr int32 HighestDifficultyTier = 8;

	/**
	 * Which difficulty tier everything in this world stands at.
	 *
	 * WHAT IT DECIDES, AND IT IS ONLY ONE THING: what armour is worth.
	 * `UCataclysmDamageCalculation::ArmorReduction` is
	 * `armor / (armor + 800 x tier)` capped at 75%, so the same armour removes
	 * far more of a hit at tier 1 than at tier 8. Nothing else in the damage
	 * calculation reads the tier.
	 *
	 * EVERY HIT USED TO RESOLVE AT TIER 1 BECAUSE NOTHING HELD A TIER AT ALL.
	 * `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` passed a literal
	 * 1, honestly, because there was nowhere to read one from. The Abyssal
	 * Warden's designed armour of 5,954 removes 88.2% of a hit at tier 1 --
	 * capped to 75% -- against 48.19% at tier 8, so every armoured thing in the
	 * game was 2.07 times harder to hurt than its design says. Issue #514.
	 *
	 * THE DESIGN PUTS THIS ON THE ENCOUNTER, NOT THE WORLD. A dungeon has a
	 * difficulty tier and the enemies inside it stand at that tier. There is no
	 * dungeon yet -- that is issue #41 -- so this world-wide figure is the
	 * smallest thing that lets the sandbox match a chosen tier. It does not
	 * block the encounter-level answer and should be replaced by it.
	 *
	 * ONE, BECAUSE THE SANDBOX IS WHERE A NEW CHARACTER STANDS. That is the
	 * start of the game rather than a placeholder. Use the
	 * `Cataclysm.DifficultyTier` console variable to play at another tier
	 * without editing this.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Difficulty",
			  meta = (ClampMin = "1", ClampMax = "8"))
	int32 DifficultyTier = LowestDifficultyTier;

	/**
	 * The difficulty tier a hit in this world resolves at.
	 *
	 * THE ONE PLACE THAT ANSWERS THE QUESTION, so a second caller cannot arrive
	 * at a different answer. It finds this world's game mode and hands it to
	 * `DifficultyTierFor` below, which decides.
	 *
	 * @param WorldContext anything that can find a world. Null answers with the
	 *                     lowest tier.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Difficulty",
			  meta = (WorldContext = "WorldContext"))
	static int32 DifficultyTierIn(const UObject* WorldContext);

	/**
	 * The same question with the game mode already in hand.
	 *
	 * SPLIT FROM `DifficultyTierIn` SO THE RULE CAN BE TESTED. A world built by
	 * an automation test has no authority game mode and cannot be given one:
	 * `UWorld::AuthorityGameMode` is private and `UWorld::SetGameMode` needs a
	 * game instance the test has not built. Keeping the decision here and the
	 * world lookup there leaves exactly one untested line -- finding the mode --
	 * instead of the whole rule.
	 *
	 * IT READS, IN ORDER: the `Cataclysm.DifficultyTier` console variable when
	 * that has been set above zero, then the game mode's own `DifficultyTier`,
	 * then `LowestDifficultyTier`. Every answer is clamped to the legal range,
	 * because a console variable is typed by hand while playing and a tier of 40
	 * would make armour worth almost nothing without saying so.
	 *
	 * FALLING BACK RATHER THAN FAILING on a null game mode, because a great deal
	 * runs without one and a hit still has to produce a number. Tier 1 is the
	 * answer those worlds already gave before anything read a tier at all.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Difficulty")
	static int32 DifficultyTierFor(const ACataclysmGameMode* Mode);

	// ----------------------------------------------------------------------
	// WHICH ENCOUNTER THE SANDBOX IS. Issue #525.
	//
	// Every health and armour figure below for a designed creature is read out
	// of `sim/cataclysm_sim/enemy_stats.py` at ONE stated encounter:
	//
	//     stats_on_floor(rarity="Common", tier=1, dungeon_type="Cataclysm",
	//                    total_floors=50, floor=50)
	//
	// TIER 1 BECAUSE THE SANDBOX IS WHERE A NEW CHARACTER STANDS, which is what
	// `DifficultyTier` above already says and defaults to.
	//
	// COMMON BECAUSE THAT IS WHAT THE SANDBOX SPAWNS BY DEFAULT.
	// `ACataclysmEnemyCharacter::RarityStep` starts at 0, which is Common, and
	// the three rarity settings at the bottom of this class default to 0 too.
	//
	// RAISING ONE OF THOSE DOES NOT MOVE THESE FIGURES, and that is worth being
	// plain about: a Brute set to Boss keeps a Common's health, armour and
	// attack damage and only its DROPS and its stun immunity change. The
	// creature is then a mixture that the design does not describe. That is
	// acceptable for sandbox scaffolding whose purpose is to make loot visible,
	// and it is not acceptable for judging a fight. Issue #39 is what makes a
	// creature's whole stat block follow its rarity.
	//
	// THE LAST FLOOR OF A 50-FLOOR CATACLYSM DUNGEON because that is the hardest
	// encounter tier 1 contains, and because it is the configuration every other
	// figure in this project is quoted against. The first floor of a Basic
	// dungeon gives a Brute 8 health, which is a creature nobody could watch do
	// anything.
	//
	// WHY THE FIGURES ARE COPIED RATHER THAN READ. `game/Data/EnemyArchetypes.csv`
	// publishes the archetype table and nothing in the engine loads it; a Power
	// Score, which is what turns a share into a number, has no port at all. Issue
	// #355 builds that transport and issue #39 wires the creatures onto it, and
	// when they do these constants go away.
	//
	// WHAT IS DELIBERATELY NOT TAKEN FROM THE MODEL: the attack damage figures
	// below. The project owner ruled on 2026-08-14 that how hard an enemy hits
	// cannot be judged yet, because there is no gear, no character level and no
	// attribute allocation to judge it against. Issue #570 records it.
	// ----------------------------------------------------------------------

	/**
	 * How many enemies to put in the level at the start of play.
	 *
	 * WHY THE GAME MODE SPAWNS THEM RATHER THAN THE LEVEL HOLDING THEM. Issue
	 * #170: game/Content/Maps/L_Sandbox.umap contained a floor, a light, a sky,
	 * a player start and navigation, and no enemy at all, so every skill built
	 * so far had nothing to act on. Placing actors by hand would put them inside
	 * a binary .umap, which cannot be reviewed in a diff and which issue #140
	 * already records as a source of churn every time the editor opens.
	 *
	 * SANDBOX SCAFFOLDING, NOT THE REAL SPAWNER. Dungeon population is issue
	 * #40 and the seven designed Demonic enemies are issue #39.
	 *
	 * ZERO SINCE 2026-08-07, ON THE PROJECT OWNER'S INSTRUCTION. Five cylinders
	 * chasing the same player collide with and crowd around the Brute, which
	 * makes it hard to watch what the Brute itself is doing. They were the only
	 * thing in the sandbox to hit before there was a designed enemy; now there
	 * is one, and they are in the way. Set this back to 5 to restore them.
	 *
	 * PUBLIC, unlike the other sandbox settings, so a test can ask for dummies
	 * regardless of what the default happens to be. The two tests in
	 * CataclysmSandboxTests.cpp check that the spawner works, not that anybody
	 * currently wants five of them, and they would otherwise fail the moment
	 * this default changed -- which is exactly what happened when it went to 0.
	 */
public:
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	int32 TrainingDummyCount = 0;

protected:

	/** How far from the player start the ring sits, in centimetres. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float TrainingDummyRingRadius = 600.0f;

	/**
	 * How much health each one has.
	 *
	 * HIGH ON PURPOSE. A Heavy Attack with an unupgraded Greataxe deals about
	 * 102, so at an enemy's default 100 health a dummy dies to one press and
	 * there is nothing to watch. At this figure it survives long enough to see a
	 * burn tick, a ground zone, and an Ultimate. Real enemy health comes from
	 * rarity and difficulty, which is issue #39.
	 *
	 * DELIBERATELY NOT MOVED ONTO THE MODEL'S FIGURE WITH THE BRUTE AND THE
	 * WARDEN. Issue #525 lowered those to what the design model states, and left
	 * this alone, because a training dummy is a practice target rather than a
	 * creature: its whole job is to outlast an effect so the effect can be
	 * watched, and the model's Common baseline of 250 dies to two and a half uses
	 * of Molten Cleave. It also spawns none by default, so nothing in a play
	 * session is affected either way.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "1"))
	float TrainingDummyHealth = 5000.0f;

	/**
	 * What one of a dummy's own attacks is worth.
	 *
	 * THEY FIGHT BACK NOW, WHICH THEY DID NOT BEFORE. Until issue #163 no pawn
	 * except the player had a controller, so a dummy stood where it was spawned
	 * and never acted. It now walks to the player and hits them, which is the
	 * only way to see in a play session that the behaviour works at all.
	 *
	 * A JUDGEMENT, AND DELIBERATELY SMALL. Five of them at 20 damage every 1.5
	 * seconds come to about 67 damage a second against a character starting at
	 * 100 health, so standing in the middle of the ring doing nothing is fatal in
	 * a couple of seconds while walking out of it is not. Set this to zero for a
	 * ring that only absorbs damage, which is what it used to be.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float TrainingDummyAttackDamage = 20.0f;

	/** How many Brutes to place. Zero for none. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	int32 BruteCount = 1;

	/**
	 * How far from the player start, in centimetres.
	 *
	 * Beyond the dummy ring at 600, so the Brute is not standing among them and
	 * has ground to walk across. Inside the sandbox's 4000 x 4000 cm navigation
	 * bounds, because outside them it cannot path at all.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float BruteDistanceCm = 1200.0f;

	/**
	 * How much health each one has.
	 *
	 * THE DESIGN MODEL'S OWN FIGURE, NOT SCAFFOLDING, SINCE ISSUE #525. It is
	 * `stats_on_floor("Common", tier=1, "Cataclysm", kind="Brute")` in
	 * `sim/cataclysm_sim/enemy_stats.py`, rounded to a whole number:
	 * `SandboxEncounter` above says which encounter that is and why.
	 * `tools/tests/test_brute_matches_the_model.py` holds it against the model.
	 *
	 * IT WAS 11,000 AND THAT MADE THE CREATURE UNFIGHTABLE. That figure was the
	 * training dummy's 5,000 times the Brute's health_share of 2.20 -- a ratio
	 * the model does state, applied to a base the model does not. An ungeared
	 * character with a bare Greataxe deals about 102 a use with Molten Cleave and
	 * about 96 of that reached the creature, so killing one took 116 uses of a
	 * 1.5 second cooldown: nearly three minutes of uninterrupted attacking. At
	 * this figure it takes about 7 uses and about 10 seconds.
	 *
	 * WHY THE MODEL'S FIGURE RATHER THAN A NUMBER PICKED TO FEEL RIGHT. The two
	 * sides of the exchange were never both honest: the player's damage is what a
	 * bare weapon really supplies, and the enemy's health was invented. Reading
	 * the model at the tier the sandbox already stands at makes both sides the
	 * project's own numbers, and it lands inside the eight-to-ten uses issue #525
	 * asked for without that target being aimed at.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "1"))
	float BruteHealth = 549.0f;

	/**
	 * How much armour each one has.
	 *
	 * NOTHING SET THIS BEFORE, AND THE BRUTE IS THE CREATURE THE DESIGN CALLS
	 * HEAVILY ARMOURED. `ACataclysmEnemyCharacter::StartingArmour` defaults to
	 * zero and nothing outside a test ever called `SetArmour`, so every enemy in
	 * the sandbox had none. Armour was the one defensive layer that reached the
	 * attribute set and was never given a value.
	 *
	 * IT ALSO MADE THE DIFFICULTY TIER DO NOTHING. `ArmorReduction` is
	 * `armor / (armor + 800 x tier)`, and with armour at zero that is zero at
	 * every tier, so the whole of issue #514's work was invisible in play.
	 *
	 * The figure is from the same stat block as `BruteHealth`. At tier 1 it
	 * removes 15.76% of a hit.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float BruteArmour = 150.0f;

	/**
	 * What one of its attacks is worth. The dummy's 20 times the Brute's
	 * damage_share of 1.75, on the same scaffolding reasoning as BruteHealth.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float BruteAttackDamage = 35.0f;

	/** How many Abyssal Wardens to place. Zero for none. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	int32 AbyssalWardenCount = 1;

	/**
	 * How far from the player start each one is put.
	 *
	 * FURTHER OUT THAN THE BRUTES AT 1200 BY MORE THAN ITS OWN RING, because
	 * this creature's Molten Roar marks a ring 6.5 metres across and the two
	 * should not be standing inside each other's telegraphs before the player
	 * has done anything.
	 *
	 * IT WAS 1800 UNTIL 2026-08-09, and the ring growing from 5.6 to 6.5 metres
	 * closed the gap: 1800 less the Brutes' 1200 is 600 cm, which the 650 cm
	 * ring now reaches across. `test_it_is_spawned_further_out_than_the_brutes`
	 * in `tools/tests/test_warden_matches_the_model.py` failed on exactly that
	 * and is what caught it. Issues #487 and #496.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float AbyssalWardenDistanceCm = 1900.0f;

	/**
	 * How much health each one has.
	 *
	 * THE DESIGN MODEL'S OWN FIGURE, on exactly the reasoning BruteHealth
	 * records. It is `stats_on_floor("Common", tier=1, "Cataclysm",
	 * kind="Abyssal Warden")`, rounded to a whole number. It was 17,500, which
	 * took 240 uses of Molten Cleave; it now takes about 15 and about 22 seconds.
	 *
	 * COMMON RARITY, THOUGH THE DESIGN MEETS THIS CREATURE AT HERALD. The same
	 * stat block at Herald is 6,161 health, which is 148 uses and nearly four
	 * minutes for an ungeared character. The sandbox exists to watch what a
	 * creature DOES, and its rarity ladder is what issues #39 and #355 build; a
	 * fight nobody can finish shows less of the creature, not more.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "1"))
	float AbyssalWardenHealth = 873.0f;

	/**
	 * How much armour each one has. See `BruteArmour` for why nothing set this
	 * before. At tier 1 it removes 17.92% of a hit.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float AbyssalWardenArmour = 175.0f;

	/**
	 * What one of its ordinary swings is worth. The dummy's 20 times its
	 * damage_share of 1.90, on the same scaffolding reasoning.
	 *
	 * MOLTEN ROAR IS WORTH FOUR OF THESE, because the Ultimate slot is 400%.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float AbyssalWardenAttackDamage = 38.0f;

	/** How many Hellhounds to place. Zero for none. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	int32 HellhoundCount = 1;

	/**
	 * How far from the player start each one is put, in centimetres.
	 *
	 * THE SAME DISTANCE THE ABYSSAL WARDEN STANDS AT, on the opposite bearing.
	 * See `HellhoundBearingDegrees` for why the bearing exists at all.
	 *
	 * BEYOND ITS OWN NOTICE RADIUS ON PURPOSE. The creature notices at 1000 cm,
	 * so at this distance it does not set off at a player who has just appeared;
	 * the player walks towards it and it starts when they are 10 metres away.
	 * That distance is also exactly the far end of Hellrush's range, and
	 * `ACataclysmEnemyController::ChooseAbility` allows a distance equal to the
	 * maximum, so the charge is legal from the first moment the creature has
	 * seen anything at all.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float HellhoundDistanceCm = 1900.0f;

	/**
	 * Which direction from the player start it is placed in, in degrees.
	 *
	 * THE ONLY CREATURE WITH ONE, AND IT IS REQUIRED RATHER THAN TIDY. All three
	 * spawners put a single creature at angle zero, which is +X, so the Brute at
	 * 1200 cm and the Abyssal Warden at 1900 cm already stand on that one line.
	 * The sandbox floor is 4000 cm across -- `FLOOR_EXTENT` in
	 * `tools/generate_input_assets.py` is both the floor's size and the
	 * navigation bounds volume's size -- so it reaches 2000 cm from the player
	 * start and the Warden is already 100 cm from its edge. There is no room
	 * further out along +X, and a creature placed between the other two would
	 * stand inside the Warden's 650 cm ring.
	 *
	 * AND THIS CREATURE NEEDS TEN METRES OF CLEAR GROUND IN FRONT OF IT, which
	 * is the length of its charge. 180 degrees is the point of the same circle
	 * furthest from both, and the whole lane between it and the player start is
	 * ground neither of the others is standing on.
	 *
	 * ITS FIRE WOULD OTHERWISE BURN THEM. The lane it leaves burns whatever
	 * stands in it, its own side included, which is correct and is the one thing
	 * that makes this creature different. A Hellhound charging down the line the
	 * other two are on would set both of them alight every five seconds, and
	 * watching any of the three would be much harder for it.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox")
	float HellhoundBearingDegrees = 180.0f;

	/**
	 * How much health each one has.
	 *
	 * THE DESIGN MODEL'S OWN FIGURE, on exactly the reasoning `BruteHealth`
	 * records. It is `stats_on_floor("Common", tier=1, "Cataclysm",
	 * kind="Hellhound")` in `sim/cataclysm_sim/enemy_stats.py`, rounded to a
	 * whole number, and `tools/tests/test_hellhound_matches_the_model.py` holds
	 * it against that file.
	 *
	 * THE LOWEST OF THE THREE, AND THAT IS THE DESIGN RATHER THAN AN OVERSIGHT.
	 * Its health share is 0.75 against the Brute's 2.20 and the Abyssal Warden's
	 * 3.50: it is a fast skirmisher that survives by not being hit, and an
	 * `evasion` of 20 with an `armor_share` of 0.30 is how the model says so. It
	 * dies to a few uses of Molten Cleave, which is what a creature that reaches
	 * the player in under two seconds has to do.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "1"))
	float HellhoundHealth = 187.0f;

	/**
	 * How much armour each one has. See `BruteArmour` for why nothing set this
	 * on any creature before issue #525. From the same stat block as
	 * `HellhoundHealth`. At tier 1 it removes 1.84% of a hit, which is almost
	 * nothing and is what an `armor_share` of 0.30 means.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float HellhoundArmour = 15.0f;

	/**
	 * What one of its bites is worth. The training dummy's 20 times the designed
	 * damage share of 0.95, on the same scaffolding reasoning as
	 * `BruteAttackDamage`.
	 *
	 * ONE PASS OF HELLRUSH IS WORTH ONE OF THESE, because the Movement slot is
	 * 100%, and standing in the whole four seconds of the lane it leaves is
	 * worth one more.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float HellhoundAttackDamage = 19.0f;

	/**
	 * How many Imps to place. Zero for none.
	 *
	 * **TEN, WHICH IS THE ONLY FIGURE HERE THE DESIGN STATES OUTRIGHT.**
	 * `docs/Cataclysm_GDD_v2.md` has a subsection headed "A pack is ten", and it
	 * gives the reason: ten is the pack that kills a geared character in 4.9
	 * seconds, and it is three more than one full ring of seven, which is what
	 * makes the second ring -- and therefore this creature's 1.32 metre reach --
	 * matter in an ordinary encounter rather than only in a swarm event.
	 *
	 * ONE IMP SHOWS ALMOST NOTHING. It takes 48 seconds to kill a geared
	 * character on its own. Spawning one the way the other three creatures are
	 * spawned would put a creature in the level that cannot demonstrate the only
	 * thing it is for.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	int32 ImpCount = 10;

	/**
	 * How far the middle of the pack is from the player start, in centimetres.
	 *
	 * BEYOND THE PACK'S OWN NOTICE RADIUS EVEN AT ITS NEAR EDGE. The creature
	 * notices at 1000 cm and the nearest of the ten stands 1300 cm out, so the
	 * pack does not set off at a player who has just appeared. Walk towards it
	 * and it comes, at 6.5 metres per second, and walking back is not an escape.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float ImpDistanceCm = 1500.0f;

	/**
	 * Which direction from the player start the pack is placed in, in degrees.
	 *
	 * THE SECOND SPAWNER WITH A BEARING, AND THE THIRD DIRECTION USED. The Brute
	 * and the Abyssal Warden stand in front of the player start at 0 degrees, the
	 * Hellhound behind it at 180, and this pack to one side. The sandbox floor is
	 * 4000 cm across so it reaches 2000 cm in every direction, and four creatures
	 * on one line would not fit; putting each on its own bearing is what lets
	 * each be walked up to on its own.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox")
	float ImpBearingDegrees = 90.0f;

	/**
	 * How far from the middle of the pack each Imp stands, in centimetres.
	 *
	 * A CLUSTER RATHER THAN A LINE OR A GRID. Ten on a circle of this radius sit
	 * 126 cm apart, which clears their own 60 cm bodies with room, so none of
	 * them starts inside another and the movement component does not have to
	 * push them apart before anything happens.
	 *
	 * IT IS NOT THE RING THE DESIGN DESCRIBES. That ring forms around the PLAYER
	 * when the pack has closed, out of crowd avoidance, and this project has no
	 * crowd avoidance yet -- issue #761. This is only where they are standing
	 * when the level opens.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float ImpPackRadiusCm = 200.0f;

	/**
	 * How much health each one has.
	 *
	 * THE DESIGN MODEL'S OWN FIGURE, on exactly the reasoning `BruteHealth`
	 * records. It is `stats_on_floor("Common", tier=1, "Cataclysm", kind="Imp")`
	 * in `sim/cataclysm_sim/enemy_stats.py`, rounded to a whole number.
	 *
	 * THE LOWEST IN THE ROSTER, AND THAT IS THE CREATURE. Its health share is
	 * 0.35 against the Brute's 2.20; the design calls it "weak individually,
	 * overwhelming in packs" and this is the first half of that sentence as a
	 * number. Ten of them together carry 870, which is more than the Abyssal
	 * Warden's 873 by nothing at all -- and that is the second half.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "1"))
	float ImpHealth = 87.0f;

	/**
	 * What one of its claw swipes is worth. The training dummy's 20 times the
	 * designed damage share of 0.45, on the same scaffolding reasoning as
	 * `BruteAttackDamage`.
	 *
	 * TEN OF THESE EVERY 0.9 SECONDS is what a pack does, which is 100 damage a
	 * second against a character starting at 100 health. That is the design's
	 * own claim -- ten Imps kill a geared character in 4.9 seconds -- arriving
	 * from the numbers rather than being aimed at.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float ImpAttackDamage = 9.0f;

	/** How many Corrupted Sentinels to place. Zero for none. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	int32 CorruptedSentinelCount = 1;

	/**
	 * How far from the player start each one is put, in centimetres.
	 *
	 * A HUNDRED CENTIMETRES BEYOND ITS OWN RANGE, which is the tightest margin
	 * of any creature here and is deliberate. It shoots 14 metres and notices at
	 * 14 metres -- the two are one number for a creature that cannot close the
	 * difference -- so at 1500 it can see and hit nothing at the player start,
	 * and one step forward puts the player inside its range at the exact edge.
	 * Anything much further out would mean walking a long way to find out what
	 * the creature does, since it will not come to you.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float CorruptedSentinelDistanceCm = 1500.0f;

	/**
	 * Which direction from the player start it is placed in, in degrees.
	 *
	 * THE FOURTH DIRECTION, AND THE LAST ONE FREE. The Brute and the Abyssal
	 * Warden stand in front of the player start at 0 degrees, the Hellhound
	 * behind it at 180, the pack of Imps to one side at 90, and this one to the
	 * other. The sandbox floor is 4000 cm across so it reaches 2000 cm in every
	 * direction, and a fifth creature will need somewhere else to go.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox")
	float CorruptedSentinelBearingDegrees = 270.0f;

	/**
	 * How much health each one has.
	 *
	 * THE DESIGN MODEL'S OWN FIGURE, on exactly the reasoning `BruteHealth`
	 * records. It is `stats_on_floor("Common", tier=1, "Cataclysm",
	 * kind="Corrupted Sentinel")`, rounded to a whole number.
	 *
	 * **AND 35% OF IT IS AN ENERGY SHIELD**, which no other creature in the
	 * sandbox has. That is set by the creature's own class rather than here,
	 * from `energy_shield_fraction`, and it sits in front of the health rather
	 * than adding to it.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "1"))
	float CorruptedSentinelHealth = 324.0f;

	/**
	 * How much armour each one has. From the same stat block as its health, and
	 * the most of any creature in the sandbox: its `armor_share` is 2.2 against
	 * the Brute's 2.0. At tier 1 it removes 12.09% of a hit.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float CorruptedSentinelArmour = 110.0f;

	/**
	 * What one of its bolts is worth. The training dummy's 20 times the designed
	 * damage share of 1.10, on the same scaffolding reasoning as
	 * `BruteAttackDamage`.
	 *
	 * ITS MORTAR IS WORTH ONE AND A HALF OF THESE, because the Special slot is
	 * 150%.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float CorruptedSentinelAttackDamage = 22.0f;

	// NO ImpArmour, AND THAT IS THE DESIGN RATHER THAN AN OVERSIGHT. The Imp's
	// `armor_share` is exactly 0.0 and it is the only creature in the roster
	// with none at all: its defence is 25% evasion, which is why area damage
	// answers a pack and single-target damage does not. So this spawner does not
	// call SetArmour, and the Imp is deliberately left out of the automation
	// test `Cataclysm.Sandbox.TheDesignedCreaturesSpawnWithArmour`, which
	// requires a creature to carry some.

public:
	// ----------------------------------------------------------------------
	// WHICH RARITY THE SANDBOX SPAWNS EACH CREATURE AT.
	//
	// Common 0, Elite 1, Legendary 2, Herald 3, Boss 4, Cataclysm Boss 5, from
	// `game/Data/EnemyRarities.csv`.
	//
	// WHAT RARITY CHANGES HERE, AND IT IS NOT THE STAT BLOCK. Health, armour and
	// attack damage come from the settings above and are untouched by these. What
	// rarity decides is what a kill gives and whether the creature can be stunned:
	// `game/Data/EnemyDrops.csv` gives a Common 0.16 gear drops and a Boss exactly
	// 5, a Boss adds 300% magic find to its own drops and a Cataclysm Boss 500%,
	// and the design's rule that a boss cannot be stunned at all is
	// `RarityStep >= FirstBossRarityStep`. See the longer note beside the stat
	// figures above for why that mixture is acceptable in a sandbox and not in a
	// judgement about a fight.
	//
	// `Config`, SO THEY CAN BE CHANGED WITHOUT REBUILDING. This project runs
	// `ACataclysmGameMode` directly -- `game/Config/DefaultEngine.ini` sets
	// `GlobalDefaultGameMode=/Script/Cataclysm.CataclysmGameMode` and there is no
	// Blueprint subclass of it anywhere in `game/Content/` -- so an
	// `EditDefaultsOnly` value on this class appears in no editor panel at all,
	// and changing one otherwise means editing this header and compiling. Set
	// these in `game/Config/DefaultGame.ini`, which already carries a section for
	// `ACataclysmPlayerController` and now carries one for this class. Issue #721.
	//
	// PUBLIC, for the reason `TrainingDummyCount` above is public: a test has to
	// be able to ask for a rarity regardless of what the default happens to be,
	// and these defaults are expected to be flipped by hand and flipped back.
	//
	// DRAWN BY DEFAULT, NOT COMMON, SINCE ISSUE #508. Each of these is -1 in
	// the shipped config, which is UCataclysmEnemyRarity::RollTheRarity and
	// means "draw one from the spawn weights": Common 0.60, Elite 0.20,
	// Legendary 0.15, Herald 0.04, Boss 0.01, and never a Cataclysm Boss. Set
	// one to a rung from 0 to 5 to force it, which is what a person testing a
	// single rarity wants and what the automation tests pass.
	//
	// EVERY CREATURE SPAWNED COMMON BEFORE THIS, because nothing drew a rarity
	// anywhere, so the eight rarity colours and the varying drop counts were
	// almost never seen. Watching loot drop meant typing 4 in here by hand, and
	// that hand edit broke the test suite repeatedly.
	//
	// STILL SANDBOX SCAFFOLDING. What decides how many creatures of each rarity
	// a floor holds, rather than what one creature draws, belongs with the
	// dungeon runtime, issue #41.
	// ----------------------------------------------------------------------

	/** Which rung each Brute spawns at, or -1 to draw one. See the note above. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Cataclysm|Sandbox",
			  meta = (ClampMin = "-1", ClampMax = "5"))
	int32 BruteRarityStep = -1;

	/** Which rung each Abyssal Warden spawns at, or -1 to draw one. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Cataclysm|Sandbox",
			  meta = (ClampMin = "-1", ClampMax = "5"))
	int32 AbyssalWardenRarityStep = -1;

	/** Which rung each Hellhound spawns at, or -1 to draw one. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Cataclysm|Sandbox",
			  meta = (ClampMin = "-1", ClampMax = "5"))
	int32 HellhoundRarityStep = -1;

	/**
	 * Which rung each Imp spawns at, or -1 to draw one.
	 *
	 * TEN INDEPENDENT DRAWS, WHICH IS THE FIRST TIME THE LADDER IS VISIBLE IN
	 * ONE PLACE. Each creature draws its own from the weights in
	 * `game/Data/EnemyRarities.csv` -- Common 0.60, Elite 0.20, Legendary 0.15,
	 * Herald 0.04, Boss 0.01 -- so a pack of ten usually holds two or three
	 * creatures above Common and drops accordingly.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Cataclysm|Sandbox",
			  meta = (ClampMin = "-1", ClampMax = "5"))
	int32 ImpRarityStep = -1;

	/** Which rung each Corrupted Sentinel spawns at, or -1 to draw one. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Cataclysm|Sandbox",
			  meta = (ClampMin = "-1", ClampMax = "5"))
	int32 CorruptedSentinelRarityStep = -1;

	/**
	 * Which rung each training dummy spawns at, or -1 to draw one.
	 *
	 * COMMON RATHER THAN DRAWN, UNLIKE THE TWO ABOVE. A dummy is a practice
	 * target and its stats come from the settings above rather than from its
	 * rarity, but a dummy that drew Boss could not be stunned at all, and being
	 * able to hit a predictable thing is what a dummy is there for. Set it to -1
	 * to draw one anyway.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Cataclysm|Sandbox",
			  meta = (ClampMin = "-1", ClampMax = "5"))
	int32 TrainingDummyRarityStep = 0;
};
