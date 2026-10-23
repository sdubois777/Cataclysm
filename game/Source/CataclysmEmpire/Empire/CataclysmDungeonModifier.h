// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmRoster.h"
#include "Math/RandomStream.h"
#include "UObject/Object.h"
#include "CataclysmDungeonModifier.generated.h"

/**
 * One row of the dungeon modifier table, as the empire layer needs it.
 *
 * A COPY OF THREE COLUMNS AND NOT THE ROW. `game/Data/DungeonModifiers.csv` has
 * 117 rows and five columns; this carries the three the draw uses and leaves the
 * description where it is. The table itself is a `UDataTable` whose row type
 * `FCataclysmDungeonModifierRow` lives in the `Cataclysm` module, and this module
 * must not depend on that one.
 *
 * SO THE POOL IS HANDED IN RATHER THAN LOADED. `UCataclysmDungeonModifierTable`
 * over in the `Cataclysm` module reads the asset and builds a list of these;
 * `UCataclysmEmpireRun::ModifierPool` holds it. That is the same arrangement
 * `UCataclysmEmpireRun::Begin` already uses for the lethality rung, and it gives
 * the same benefit: every rule here can be tested against a pool built by hand,
 * with no asset, no editor and no rendering.
 *
 * `Danger` IS THE `Weight` COLUMN AND IT IS NOT A SPAWN FREQUENCY. The project
 * owner settled that on 2026-09-05 -- their words were "How dangerous it is" --
 * and it is recorded in `docs/DECISIONS.md` under "The dungeon modifier Weight
 * column is a danger score, not a spawn frequency". It is renamed here because
 * "weight" beside a random draw reads as the thing being drawn on, which is
 * exactly the misreading the C++ carried for four months. Nothing in this file
 * gives `Danger` to the draw.
 */
USTRUCT(BlueprintType)
struct CATACLYSMEMPIRE_API FCataclysmDungeonModifier
{
	GENERATED_BODY()

	/** The table's row key, e.g. `Celestial_Edict_of_Silence`. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	FName RowKey;

	/** What the design calls it, e.g. `Edict of Silence`. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	FName ModifierName;

	/**
	 * Which Cataclysm's column it came from.
	 *
	 * `None` IS THE `Generic` COLUMN, and that is what keeps the Corrupted
	 * Stalker out of every pool without a rule of its own. See
	 * `UCataclysmDungeonModifierRules::PoolFor`.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	ECataclysmType Cataclysm = ECataclysmType::None;

	/** How much harder it makes the dungeon. The `Weight` column. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float Danger = 0.0f;
};

/**
 * Which modifiers a dungeon carries, and how dangerous they make it.
 *
 * WHAT THIS ANSWERS. `game/Data/DungeonModifiers.csv` has held 117 rows since
 * the design workbook was imported and **nothing in the game had ever read one**.
 * `FCataclysmScoredFloor::ModifierScore` was hard-zeroed with a comment saying
 * dungeon modifiers do not exist. Issue #41.
 *
 * THREE RULES, AND TWO OF THEM ARE WRITTEN DOWN IN THE DESIGN.
 *
 *   - **How many.** `docs/Cataclysm_GDD_v2.md` section VIII: "A dungeon carries
 *     one modifier per difficulty tier, so a tier 8 dungeon carries eight. A
 *     Sacrificial dungeon carries double that." `CountFor` is that sentence.
 *   - **Which pool.** The modifiers of every Cataclysm the run is facing, pooled
 *     together. `config.py` states it: "dungeons then draw from the combined
 *     modifier pool of every ACTIVE Cataclysm". The `Generic` column is excluded,
 *     because the project owner ruled on 2026-09-05 that the one row in it, the
 *     Corrupted Stalker, is "granted separately" and does not compete for a slot.
 *   - **Which ones out of that pool.** An equal chance, no repeats, ruled by
 *     the project owner on 2026-09-07 because the design does not state it. See
 *     `Draw`.
 *
 * A STATIC OVER A LIST AND A STREAM, so it can be tested. This is the shape
 * `UCataclysmEnemyModifiers` uses for the same job on a creature, and the reason
 * it gives holds here: a rule that lived on the run could only be checked by
 * building a run.
 */
UCLASS()
class CATACLYSMEMPIRE_API UCataclysmDungeonModifierRules : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * How many modifiers one difficulty tier is worth.
	 *
	 * A PORT OF `config.modifiers_per_tier`, which is 1.
	 * `tools/tests/test_dungeon_modifier_port.py` fails if the two part company.
	 */
	static constexpr int32 ModifiersPerTier = 1;

	/**
	 * What a Sacrificial dungeon multiplies that by.
	 *
	 * A PORT OF `config.sacrificial_modifier_multiplier`, which is 2. The design:
	 * "A Sacrificial dungeon carries double that, which the player may either
	 * shed by sacrificing materials or keep for bonus rewards."
	 *
	 * THE SHEDDING IS NOT BUILT AND THIS IS ONLY THE DOUBLING. A Sacrificial
	 * dungeon gets twice the modifiers and there is nothing yet to sacrifice
	 * materials to, so today it is purely harder. Issue #41 keeps that half.
	 */
	static constexpr int32 SacrificialMultiplier = 2;

	/**
	 * How many modifiers a dungeon of this tier and sub-type carries.
	 *
	 * @param DifficultyTier the run's tier, 1 to 8. Clamped at zero from below,
	 *                       so a tier of 0 or less asks for no modifiers rather
	 *                       than for a negative number of them
	 * @param SubType        Sacrificial doubles it; every other sub-type leaves
	 *                       it alone
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static int32 CountFor(int32 DifficultyTier,
						  ECataclysmDungeonSubType SubType);

	/**
	 * The modifiers a run facing these Cataclysms may draw, sorted by row key.
	 *
	 * THE `Generic` COLUMN FALLS OUT RATHER THAN BEING CUT OUT. Its rows carry
	 * `ECataclysmType::None`, and `None` is never in an active set --
	 * `UCataclysmRoster::All` leaves it out and `ActiveFor` draws from that. So
	 * the ordinary membership test excludes it, with no second rule to keep in
	 * step with the first. `Cataclysm.DungeonModifiers.TheGenericColumnIsNever
	 * Drawable` is what holds that true rather than the argument.
	 *
	 * SORTED, BECAUSE THE ORDER OF THE INPUT IS NOT GUARANTEED. The pool is
	 * built by walking a `UDataTable`, which is a map, so an unsorted walk would
	 * hand the same seed a different modifier on a different run.
	 * `UCataclysmEnemyModifiers::PoolFor` sorts for the same reason.
	 *
	 * @param All     every row of the table
	 * @param Active  the run's `ActiveCataclysms`
	 */
	static TArray<FCataclysmDungeonModifier> PoolFor(
		const TArray<FCataclysmDungeonModifier>& All,
		const TArray<ECataclysmType>& Active);

	/**
	 * Draw `Count` distinct modifiers from a pool.
	 *
	 * **AN EQUAL CHANCE ACROSS THE POOL, WITH NO REPEATS. THE PROJECT OWNER
	 * CHOSE IT ON 2026-09-07.** No design document says which modifiers a
	 * dungeon gets, so it was put to them as three options: an equal chance; the
	 * more dangerous a modifier is the rarer it is; the more dangerous a modifier
	 * is the commoner it becomes at high difficulty tiers. They picked the first.
	 *
	 * THEY SELECTED RATHER THAN WROTE, so there is no sentence of theirs to
	 * quote. `docs/DECISIONS.md` carries the exact text of all three options as
	 * it was shown to them, which is what a quotation would have been for. The
	 * two they refused are the part worth reading: they are what stops this
	 * being re-opened.
	 *
	 * **THE DANGER SCORE IS NEVER READ HERE, AND THAT IS THE POINT.** Using it
	 * to control frequency was one of the two shapes that was refused, and the
	 * owner's earlier ruling of 2026-09-05 -- that the `Weight` column is a
	 * danger score -- says in as many words not to reuse it as one. So
	 * `FCataclysmDungeonModifier::Danger` appears nowhere in this function.
	 *
	 * THE RULING AGREES WITH WHAT THE REST OF THE REPOSITORY ALREADY DID, which
	 * is why the question could be asked in one sentence:
	 * `Simulation._make_dungeon` in `sim/cataclysm_sim/engine.py`, which issue
	 * #41 names as the reference implementation, draws with `rng.sample`, and
	 * `UCataclysmEnemyModifiers::Draw` next door draws a creature's modifiers the
	 * same way. Neither was a decision until now.
	 *
	 * NO DUPLICATES. Two copies of one environmental effect on one dungeon read
	 * to a player as one effect that is worse, which is the reasoning the project
	 * owner accepted for enemy modifiers on 2026-09-05, and `rng.sample` in the
	 * model is a draw without replacement too.
	 *
	 * **IT IS STILL TRUE THAT NOTHING SAYS HOW OFTEN EACH MODIFIER SHOULD
	 * APPEAR.** Uniform is not a designed distribution; it is the absence of
	 * one, chosen deliberately. If a rarity is ever designed it belongs in a
	 * column of `game/Data/DungeonModifiers.csv` of its own, not in `Danger`.
	 *
	 * FEWER THAN ASKED FOR WHEN THE POOL RUNS OUT, rather than repeating. It can
	 * happen: one active Cataclysm is 12 to 15 rows and difficulty tier 8
	 * Sacrificial asks for 16. `config.py` names that exact risk -- "~26
	 * modifiers to roll from, which is what stops deep tiers running dry" -- and
	 * a run at tier 8 has all eight Cataclysms active, so the pool is 116 and it
	 * cannot happen in a real run. A test asks for more than a pool holds anyway.
	 *
	 * @param Pool    what may be drawn. Not modified
	 * @param Count   how many. Zero or fewer draws none
	 * @param Stream  advanced once per modifier drawn, and not at all when none
	 *                is
	 */
	static TArray<FCataclysmDungeonModifier> Draw(
		const TArray<FCataclysmDungeonModifier>& Pool, int32 Count,
		FRandomStream& Stream);

	/**
	 * What these modifiers add to the enemy score of every creature in the
	 * dungeon.
	 *
	 * THE SUM OF THE DANGER SCORES, straight from the design:
	 * `docs/Cataclysm_GDD_v2.md` section VIII, "the sum of the weights on a
	 * dungeon is the Modifier Score in the Enemy Score formula". A flat addend
	 * rather than a multiplier, which is what `UCataclysmEnemyScore::ScoreFor`
	 * expects, so no modifiers is exactly zero and not an approximation.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static float DangerOf(const TArray<FCataclysmDungeonModifier>& Drawn);

	/** Just the row keys, in the order they were drawn. */
	static TArray<FName> KeysOf(const TArray<FCataclysmDungeonModifier>& Drawn);

	// ----------------------------------------------------------------------
	// The Divine Edicts and the Ascendant -- 2026-10-22
	// ----------------------------------------------------------------------

	/**
	 * Which rows of the modifier table are Divine Edicts, in a fixed order.
	 *
	 * `docs/Cataclysm_GDD_v2.md`, Heaven's Wrath: "Each Edict is a Celestial
	 * dungeon modifier". Eight are designed and two are rows today: the Edict
	 * of Silence, `Celestial_Edict_of_Silence`, and the Edict of Tithe, which
	 * is the existing modifier Forced Tithes, `Celestial_Forced_Tithes`. The
	 * six new rows come with their own layer, G7b, and are added here then. A
	 * C++ list and not a column of the table, ruled by the coordinating
	 * session on 2026-10-22.
	 *
	 * IN THIS MODULE, because the empire's hit and the floor rules both need
	 * it, and the empire module must not depend on the game's.
	 */
	static const TArray<FName>& EdictKeys();

	/** Whether a row is a Divine Edict. */
	static bool IsEdict(FName RowKey);

	/**
	 * What a player is shown for an Edict: "Edict of Silence", "Edict of
	 * Tithe". Empty for a row that is not one.
	 *
	 * THE DESIGN'S NAMES AND NOT THE ROWS'. The row of the Edict of Tithe is
	 * called Forced Tithes; the design names it the Edict of Tithe and says it
	 * is that modifier, unchanged. A name and not a lookup, so the city screen
	 * can say it without the modifier table.
	 */
	static FString EdictName(FName RowKey);

	/**
	 * How many Edicts an Ascendant holds at most beyond its modifiers: four,
	 * from floor 4. The owner's, 2026-10-19 ("Full plan design"). The model's
	 * `config.ascendant_extra_modifiers_cap` is the same 4;
	 * `tools/tests/test_the_ascendant_in_the_model_and_in_the_game.py` holds
	 * the two together.
	 */
	static constexpr int32 AscendantMostEdicts = 4;

	/**
	 * How much stronger an Edict is for each floor below the first: a tenth.
	 * The owner's, 2026-10-19, and "From floor 1", 2026-10-20.
	 */
	static constexpr float EdictStrongerPerFloor = 0.10f;

	/**
	 * What an Edict's numbers are multiplied by on the Ascendant's last floor
	 * while its Heavenly Core stands: half as strong again. The owner's ("Core
	 * floor only", 2026-10-20); the design's "Edicts are 50% stronger inside
	 * gate dungeons", read as the Core's floor.
	 */
	static constexpr float EdictStrongerWhileTheCoreStands = 1.5f;

	/** Mixed into a dungeon's walk seed for the order its Edicts come in. The letters "edct". */
	static constexpr int32 EdictSalt = 0x65646374;

	/** Mixed into a dungeon's walk seed for which Edict its hit leaves. The letters "left". */
	static constexpr int32 LeftEdictSalt = 0x6C656674;

	/**
	 * Every Edict, in the order an Ascendant of this walk seed takes them.
	 *
	 * **EVEN, WITH NO REPEATS, AND DRAWN ONCE FOR THE WHOLE DUNGEON.** The
	 * owner's ruling of 2026-09-07 for which modifiers a dungeon carries -- an
	 * equal chance, and the danger score never sets how often; see `Draw` --
	 * is the ruling the coordinating session applied to the Edicts on
	 * 2026-10-22, in place of the design's "by their weights". `Draw` itself,
	 * over every Edict, on a stream made from the walk seed alone: a shuffle.
	 * NEITHER THE DANGER SCORE NOR ANY WEIGHT IS READ HERE.
	 *
	 * FROM THE SEED ALONE, so floor N's Edicts are always the first N of the
	 * same order and hold floor N-1's, and so the empire can answer what a
	 * walk would meet. Nothing here advances any of the run's streams.
	 */
	static TArray<FName> EdictOrderFor(int32 WalkSeed);

	/**
	 * The Edicts an Ascendant adds to floor `FloorNumber`, beyond the
	 * modifiers already in force there.
	 *
	 * ONE ON FLOOR 1, TWO ON FLOOR 2, THREE ON FLOOR 3, FOUR FROM FLOOR 4, as
	 * the design says; and "when none is left to draw, no further Edict is
	 * added". They are taken from `EdictOrderFor` in order, PASSING OVER ONE
	 * ALREADY IN FORCE: an Edict among the dungeon's own modifiers is already
	 * active and is not drawn again. Passing over is still an even draw from
	 * what is left, because an even shuffle of all of them is an even shuffle
	 * of any part of them.
	 *
	 * @param WalkSeed       `FCataclysmDungeon::WalkSeed`
	 * @param AlreadyActive  the modifiers in force without the Edicts
	 * @param FloorNumber    counted from 1; below 1 adds none
	 */
	static TArray<FName> EdictsOnFloor(int32 WalkSeed,
									   const TArray<FName>& AlreadyActive,
									   int32 FloorNumber);

	/**
	 * Which of its Edicts an Ascendant's hit leaves on its city, or none.
	 *
	 * ONE OF THE EDICTS IT WOULD HOLD FROM FLOOR 4, ruled by the coordinating
	 * session on 2026-10-22: `EdictsOnFloor` at `AscendantMostEdicts`, the
	 * same answer a walk gives, walked or not. The owner's "One at random"
	 * picks between them evenly, on a stream made from the walk seed and the
	 * day of the hit, so it moves none of the run's streams; a second hit on
	 * another day may leave another.
	 */
	static FName EdictLeftBy(int32 WalkSeed, const TArray<FName>& AlreadyActive,
							 int32 Day);

	/**
	 * What an Edict's numbers are multiplied by on this floor of an Ascendant:
	 * `(1 + 0.10 x (N - 1))`, and half as much again on its last floor while
	 * its Heavenly Core stands.
	 *
	 * Floor 1 is 1, floor 2 1.1, floor 6 1.5, floor 70 7.9. A floor below 1 is
	 * read as floor 1. ONLY AN ASCENDANT ASKS; an Edict anywhere else, and the
	 * Edict an Ascendant leaves on its city, is at its base strength. What each
	 * Edict scales, and the cap that stops one becoming permanent, is the
	 * Edict's own rule; see `UCataclysmDungeonModifierEffects`.
	 */
	static float EdictStrengthOn(int32 FloorNumber, bool bTheCoreStandsOnIt);
};
