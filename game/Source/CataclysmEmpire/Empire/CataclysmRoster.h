// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "UObject/Object.h"
#include "CataclysmRoster.generated.h"

/**
 * The eight Cataclysms, from `docs/Cataclysm_GDD_v2.md` section III.
 *
 * WHY THIS EXISTS AT ALL. Until issue #1357 the empire layer had no notion of
 * which Cataclysm was running: no enum, no active list, and no field on
 * `FCataclysmDungeon` saying which one sent it. Two rules the project owner had
 * already given could not be built because of it -- a Quest dungeon's spawn
 * chance "should depend on the Cataclysm", and the Cataclysm dungeon unlocks
 * when half the ACTIVE Cataclysms have had their objectives met. A rule keyed on
 * a thing with one reachable value is not a rule.
 *
 * `None` IS A REAL VALUE AND MEANS "NOBODY SAID". A dungeon built by hand in a
 * test, or one from before this enum existed, carries it, and it counts towards
 * no Cataclysm's objectives. It is NOT one of the eight and never appears in an
 * active set. `Dungeon.source` in `sim/cataclysm_sim/engine.py` is the same
 * field with the same unassigned value, spelt as an empty string.
 *
 * THE ORDER OF THESE VALUES MEANS NOTHING, which is the one thing about it that
 * is easy to get wrong. `sim/cataclysm_sim/config.py` says so of its own
 * `CATACLYSM_ROSTER` in as many words: the project owner ruled on 2026-09-06
 * that a character draws its own order, so a Cataclysm's position here is not
 * "when it is added". `UCataclysmRoster::OrderFor` is what draws. What the
 * numbers ARE fixed by is the same rule as `ECataclysmDungeonType`: a save or a
 * data table row stores the number, so inserting a Cataclysm in the middle later
 * would change what an existing value means. Add to the end.
 */
UENUM(BlueprintType)
enum class ECataclysmType : uint8
{
	None        = 0,
	Demonic     = 1,
	Death       = 2,
	War         = 3,
	Pestilence  = 4,
	Famine      = 5,
	Celestial   = 6,
	Chaos       = 7,
	Void        = 8	UMETA(DisplayName = "The Void"),
};

/**
 * Which Cataclysms a campaign faces, what each of them asks for, and how many
 * of them have to be finished.
 *
 * A PORT OF `cataclysm_order_for`, `active_cataclysms_for` in
 * `sim/cataclysm_sim/engine.py` and `TuningConfig.active_cataclysm_count`,
 * `quest_objectives_for` and `cataclysms_required` in
 * `sim/cataclysm_sim/config.py`. Like every other constant in this module the
 * simulation is the original and this is the copy;
 * `tools/tests/test_the_cataclysm_dungeon_unlocks_at_half.py` compares the two
 * and also compares both against `docs/Cataclysm_GDD_v2.md`.
 *
 * WHAT IS NOT PORTED, named so the scope is not argued later:
 *
 *   - **The eight attack patterns.** The model gives each Cataclysm its own
 *     shape of wave -- a Death wave is a swarm of shallow dungeons, a Celestial
 *     one a handful of deep ones -- and weights how much of a wave each active
 *     Cataclysm contributes by that shape. This module knows WHICH Cataclysms
 *     are active and nothing about how they differ, so
 *     `UCataclysmEmpireRun::RollCataclysm` draws uniformly. Issue #53.
 *   - **The 117 dungeon modifiers**, which the model pools across the active
 *     Cataclysms. Issue #41.
 *   - **The Quest dungeon spawn rate.** The owner ruled it "should depend on the
 *     Cataclysm" and the rule is still not derived; issue #1357 owns it. What
 *     this class removes is the reason it COULD not be built, not the work.
 */
UCLASS()
class CATACLYSMEMPIRE_API UCataclysmRoster : public UObject
{
	GENERATED_BODY()

public:
	/** How many Cataclysms there are. `config.CATACLYSM_ROSTER` has eight. */
	static constexpr int32 Count = 8;

	/**
	 * All eight, in declaration order, which means nothing. See the enum.
	 *
	 * `None` IS NOT IN IT. It is the unassigned value and not a Cataclysm.
	 */
	static const TArray<ECataclysmType>& All();

	/**
	 * How many Cataclysms are active at once at this difficulty tier.
	 *
	 * THE DIFFICULTY TIER IS THE COUNT. `docs/Cataclysm_GDD_v2.md`, Game Start:
	 * a character faces one Cataclysm at first and every boss defeated adds one
	 * more, "so the player will eventually face all eight simultaneously". A
	 * port of `TuningConfig.active_cataclysm_count`, including its clamp: a tier
	 * outside 1..8 is held to the range rather than asked for a ninth
	 * Cataclysm.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static int32 ActiveCountFor(int32 DifficultyTier);

	/**
	 * The order this character adds Cataclysms in, all eight of them.
	 *
	 * THE SEED IS THE CHARACTER, which is the whole of why this is drawn rather
	 * than fixed. The project owner ruled on 2026-09-06, verbatim, "if they are
	 * on t3 with demonic/war/death they restart with those same cataclysms", so
	 * replaying a run at the same seed must meet the same Cataclysms and the
	 * same seed one tier higher must meet those plus one. Drawing from the seed
	 * gives both. `cataclysm_order_for` in `sim/cataclysm_sim/engine.py` has the
	 * argument in full.
	 *
	 * **IT DRAWS FROM ITS OWN STREAM AND NOT FROM THE RUN'S**, which is a port
	 * of the model's private generator and is here for the model's own stated
	 * reason: taking these draws from the run's stream would shift every later
	 * draw, so a change to how many Cataclysms are active would silently re-roll
	 * the whole campaign -- every wave, every depth, every city lost.
	 *
	 * IT DOES NOT REPRODUCE THE MODEL'S SEQUENCE and does not claim to.
	 * `random.Random` seeded from a string and `FRandomStream` are different
	 * generators, so seed 7 draws a different order in each. What is ported is
	 * the shape -- a uniform draw over the eight, keyed only on the seed -- and
	 * uniformity is the model's stated ASSUMPTION rather than a design decision.
	 * Issue #1338 carries whether any ordering should be constrained.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static TArray<ECataclysmType> OrderFor(int32 Seed);

	/** The Cataclysms this campaign faces: the first `ActiveCountFor` of its draw. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static TArray<ECataclysmType> ActiveFor(int32 Seed, int32 DifficultyTier);

	/**
	 * How many quest dungeons this Cataclysm asks the player to clear.
	 *
	 * THE COUNTS DIFFER AND THAT IS DELIBERATE. `docs/Cataclysm_GDD_v2.md`
	 * section XI states all eight, and the project owner was asked whether to
	 * keep them or settle on one number and answered "Keep the per-Cataclysm
	 * numbers". A port of `TuningConfig.quest_objectives_for`.
	 *
	 * `None` ASKS FOR NOTHING, because it is not a Cataclysm. Nothing can be
	 * completed by it: it never appears in an active set, and the unlock rule
	 * only ever reads the active set.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static int32 QuestObjectivesFor(ECataclysmType Cataclysm);

	/**
	 * How many of the active Cataclysms must be finished to open the Cataclysm
	 * dungeon.
	 *
	 * HALF, ROUNDED UP. The project owner ruled it on 2026-09-06, verbatim:
	 *
	 *     "what if we did something a bit more creative. For instance something
	 *     like, you have to meet the quest objectives for half of the cataclysms
	 *     you're facing in order to unlock the cataclysm dungeon. So if you're
	 *     facing 4, you have to complete 2 quests, 8 would be 4. For odd numbers
	 *     do something like 3 you need 2, 5 you need 3, 7 you need 4? Since what
	 *     quest dungeons spawn during a surge, if any, is random, this might
	 *     make it a bit more interesting."
	 *
	 * Every worked example is the ceiling of a half -- 3 to 2, 4 to 2, 5 to 3,
	 * 7 to 4, 8 to 4 -- so that is what this computes, as `(N + 1) / 2` in
	 * integer arithmetic.
	 *
	 * **THE ODD COUNTS ARE THE ONLY ONES THAT PROVE WHICH WAS BUILT.** Floor and
	 * ceiling agree at 4 and at 8 and disagree at 3, 5 and 7, so a test that
	 * exercises only the owner's even examples says nothing about whether the
	 * rounding is right. `Cataclysm.Roster.HalfRoundsUpAtEveryOddCount` is the
	 * one that does.
	 *
	 * AT ONE ACTIVE CATACLYSM IT IS ONE. Half of one rounded down is none, which
	 * would open the boss before the player had cleared anything; the ceiling
	 * gives one, and the rule then reduces to the single count the design
	 * already describes at difficulty tier 1.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static int32 CataclysmsRequiredFor(int32 ActiveCount);

	/**
	 * What the design calls this Cataclysm.
	 *
	 * THE SPELLING IS THE MODEL'S KEY and not the display name, so
	 * `tools/tests/test_the_cataclysm_dungeon_unlocks_at_half.py` can join the
	 * two sides of the port by name. That is why The Void is `Void` here while
	 * the enum's `UMETA(DisplayName)` says "The Void": the design document uses
	 * the article and `config.CATACLYSM_ROSTER` does not.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FName NameFor(ECataclysmType Cataclysm);

	/**
	 * A run's seed, scrambled, so a second stream keyed on it is not a copy of
	 * the first.
	 *
	 * WHY IT IS NEEDED AT ALL. `FRandomStream` is a linear congruential
	 * generator: two streams initialised with neighbouring numbers produce
	 * closely related first draws, and a stream initialised with `Seed + 1` is
	 * exactly the stream the run of seed `Seed + 1` uses. Deriving a second
	 * stream by adding to the seed therefore makes one run's Cataclysm draws a
	 * copy of the next run's wave draws, which is a correlation nobody asked
	 * for and which a test comparing two runs could easily trip over.
	 *
	 * KNUTH'S MULTIPLICATIVE HASH. `Salt` separates the uses -- the order draw
	 * and the per-dungeon draw take different ones -- so both derive from the
	 * run's seed and neither is the other.
	 */
	static int32 MixedSeed(int32 Seed, int32 Salt);

	/** The salt `OrderFor` uses. */
	static constexpr int32 OrderSalt = 1;

	/**
	 * The salt `UCataclysmEmpireRun::RollCataclysm`'s stream uses.
	 *
	 * DIFFERENT FROM `OrderSalt` so that a campaign's order and its per-dungeon
	 * draws are not the same sequence read twice.
	 */
	static constexpr int32 WaveSalt = 2;
};
