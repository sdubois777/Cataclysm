// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * ONE SEED FOR ONE STREAM, FROM A SEED, A SALT AND AN INDEX, so that no two streams the game derives are a fixed
 * distance apart. Issue #2749's placement seeding slice, ruled by the coordinating session on 2026-11-19.
 *
 * WHY. `FRandomStream` is a linear congruential generator (`Seed = Seed * 196314165 + 907633515`, a fraction from the
 * top 23 bits). Two streams whose seeds differ by a constant give first fractions that differ by a constant, and so
 * on draw by draw. The game derived streams by XOR with a salt whose low 16 bits are nought plus an index
 * (`Plan.Seed ^ (0x72720000 + Index)`), by XOR with a small salt, by `UCataclysmRoster::MixedSeed` (linear: a
 * multiply and an add), and from `GetUniqueID() ^ clock` -- each makes related seeds related streams. On a floor that
 * put every Elite's first modifier in a band of a few adjacent rows of the sorted pool: the project owner's floor 1 on
 * 2026-11-19 had 48 Elites carrying four modifiers. `sim/analyse_placement_seeds.py` replays it.
 *
 * WHAT. The three numbers each times its own odd constant, summed, then the avalanche finaliser
 * `FCataclysmFloorGenerator::SeedForFloor` ends with, so changing any input changes about half the output's bits.
 * A JUDGEMENT of the slice, not a rule another game states. `sim/analyse_placement_seeds.py`'s `planned_seed` is the
 * same arithmetic, and `tools/tests/test_placement_seed_mix_port.py` holds the two equal.
 *
 * NEVER NOUGHT, because a nought seed means "draw from the clock" to the callers that take one. The result is a
 * non-negative `int32`.
 *
 * HERE, IN THE EMPIRE MODULE, because the empire module's streams use it and that module must not depend on the game's;
 * the game's module includes this header. Inline, so it needs no export.
 */
namespace CataclysmSeedMix
{
	/** The odd constants the three inputs are multiplied by before they are summed. */
	inline constexpr uint32 SeedFactor = 0x9E3779B1u;
	inline constexpr uint32 SaltFactor = 0x85EBCA6Bu;
	inline constexpr uint32 IndexFactor = 0xC2B2AE35u;

	/** `SeedForFloor`'s finaliser, to a non-negative `int32`. */
	inline int32 Finalise(uint32 Mixed)
	{
		Mixed ^= Mixed >> 16;
		Mixed *= 0x7FEB352Du;
		Mixed ^= Mixed >> 15;
		Mixed *= 0x846CA68Bu;
		Mixed ^= Mixed >> 16;
		return static_cast<int32>(Mixed & 0x7FFFFFFFu);
	}

	/** The seed for the stream `Salt` names, the `Index`-th of its kind, derived from `Seed`. Never nought. */
	inline int32 Mix(int32 Seed, int32 Salt, int32 Index)
	{
		const uint32 Summed = static_cast<uint32>(Seed) * SeedFactor + static_cast<uint32>(Salt) * SaltFactor
			+ static_cast<uint32>(Index) * IndexFactor;
		const int32 Mixed = Finalise(Summed);
		return Mixed != 0 ? Mixed : 1;
	}
}
