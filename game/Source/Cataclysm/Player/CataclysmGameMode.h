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

	/** Every Abyssal Warden this game mode placed. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Sandbox")
	TArray<TObjectPtr<class ACataclysmAbyssalWardenCharacter>> AbyssalWardens;

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
	// COMMON BY DEFAULT, because that is what every creature has always spawned
	// at and a play session should not silently become a boss fight. Raise the
	// Brute's to 4 to watch loot drop: a Common kill gives nothing five times in
	// six and a Boss gives five items every time.
	//
	// SANDBOX SCAFFOLDING. The thing meant to assign a rarity is the enemy
	// generator, which is issue #508 and does not exist.
	// ----------------------------------------------------------------------

	/** Which rung each Brute spawns at. See the note above. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Cataclysm|Sandbox",
			  meta = (ClampMin = "0", ClampMax = "5"))
	int32 BruteRarityStep = 0;

	/** Which rung each Abyssal Warden spawns at. See the note above. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Cataclysm|Sandbox",
			  meta = (ClampMin = "0", ClampMax = "5"))
	int32 AbyssalWardenRarityStep = 0;

	/** Which rung each training dummy spawns at. See the note above. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Cataclysm|Sandbox",
			  meta = (ClampMin = "0", ClampMax = "5"))
	int32 TrainingDummyRarityStep = 0;
};
