// Copyright Stephen Dubois. All Rights Reserved.

#include "Empire/CataclysmRoster.h"

namespace
{
	/**
	 * What the order draw's seed is mixed with before it reaches the stream.
	 *
	 * WITHOUT IT THE ORDER WOULD BE CORRELATED WITH THE RUN. The order is drawn
	 * from its own stream so that it does not consume the run's chance, but a
	 * stream initialised with the same number as the run's would still be the
	 * same sequence -- so seeds 1 and 2 would draw orders as closely related as
	 * their runs are. `FRandomStream` is a linear congruential generator and
	 * adjacent seeds give closely related first draws, which is exactly what a
	 * shuffle reads.
	 *
	 * KNUTH'S MULTIPLICATIVE CONSTANT, and the addend is the golden ratio in
	 * fixed point, so seed 0 does not mix to 0. This is a hash and not a
	 * cryptographic anything; what it has to do is spread adjacent seeds, and
	 * `Cataclysm.Roster.AdjacentSeedsDrawUnrelatedOrders` is what checks it
	 * does.
	 */
	constexpr uint32 CataclysmOrderSeedMix = 2654435761u;
	constexpr uint32 CataclysmOrderSeedOffset = 2654435769u;
}

const TArray<ECataclysmType>& UCataclysmRoster::All()
{
	// FUNCTION-LOCAL AND NOT A FILE-SCOPE STATIC. Unreal merges a module's
	// translation units, so a file-scope name here would collide with any other
	// file in `CataclysmEmpire` that used it; issue #159's incident and
	// `tools/tests/test_no_two_files_share_an_anonymous_helper.py` are why this
	// module keeps helpers out of file scope.
	static const TArray<ECataclysmType> Roster = {
		ECataclysmType::Demonic,
		ECataclysmType::Death,
		ECataclysmType::War,
		ECataclysmType::Pestilence,
		ECataclysmType::Famine,
		ECataclysmType::Celestial,
		ECataclysmType::Chaos,
		ECataclysmType::Void,
	};

	return Roster;
}

int32 UCataclysmRoster::ActiveCountFor(int32 DifficultyTier)
{
	// CLAMPED TO 1..8 EXACTLY AS `active_cataclysm_count` CLAMPS. A tier of 0 or
	// of 9 is a caller's mistake and the honest answer is the nearest real one,
	// not a run facing no Cataclysm at all or asking for a ninth that does not
	// exist.
	return FMath::Clamp(DifficultyTier, 1, Count);
}

int32 UCataclysmRoster::MixedSeed(int32 Seed, int32 Salt)
{
	// THE SALT GOES IN BEFORE THE MULTIPLY, not after, so two salts do not
	// merely offset one another's output -- an addend applied afterwards would
	// leave the two sequences a fixed distance apart, which is the correlation
	// this exists to remove.
	const uint32 Salted = static_cast<uint32>(Seed)
		+ static_cast<uint32>(Salt) * CataclysmOrderSeedOffset;

	return static_cast<int32>(Salted * CataclysmOrderSeedMix
							  + CataclysmOrderSeedOffset);
}

TArray<ECataclysmType> UCataclysmRoster::OrderFor(int32 Seed)
{
	TArray<ECataclysmType> Order = All();

	FRandomStream Stream(MixedSeed(Seed, OrderSalt));

	// FISHER-YATES, BACKWARDS, WHICH IS THE ONLY SHUFFLE THAT IS UNIFORM.
	// `RandRange` is inclusive at both ends, so swapping index `Index` with
	// something in `[0, Index]` includes leaving it where it is -- which is the
	// step people drop, and dropping it makes some orderings unreachable.
	for (int32 Index = Order.Num() - 1; Index > 0; --Index)
	{
		Order.Swap(Index, Stream.RandRange(0, Index));
	}

	return Order;
}

TArray<ECataclysmType> UCataclysmRoster::ActiveFor(int32 Seed,
												   int32 DifficultyTier)
{
	TArray<ECataclysmType> Active = OrderFor(Seed);
	Active.SetNum(ActiveCountFor(DifficultyTier));
	return Active;
}

int32 UCataclysmRoster::QuestObjectivesFor(ECataclysmType Cataclysm)
{
	// THE EIGHT COUNTS FROM `docs/Cataclysm_GDD_v2.md` SECTION XI, which states
	// each one in its own prose and all eight together in a summary table.
	// `tools/tests/test_quest_objective_counts_are_stated.py` guards them there
	// and `tools/tests/test_the_cataclysm_dungeon_unlocks_at_half.py` guards
	// that these agree with the document and with the model.
	switch (Cataclysm)
	{
	case ECataclysmType::Demonic:     return 10;  // seal 10 Rifts
	case ECataclysmType::Death:       return 5;   // 5 Seeds of Undeath
	case ECataclysmType::War:         return 10;  // 10 Essences of War
	case ECataclysmType::Pestilence:  return 5;   // 5 towards a vaccine
	case ECataclysmType::Famine:      return 5;
	case ECataclysmType::Celestial:   return 10;  // 10 Heavenly Cores
	case ECataclysmType::Chaos:       return 8;   // 8 Pillars of Order
	case ECataclysmType::Void:        return 5;   // 5 Sealing Rituals
	default:                          return 0;   // `None` is not a Cataclysm
	}
}

int32 UCataclysmRoster::CataclysmsRequiredFor(int32 ActiveCount)
{
	const int32 Active = FMath::Clamp(ActiveCount, 1, Count);

	// `(N + 1) / 2` IS THE CEILING OF A HALF for a positive integer, and the
	// header says why the ceiling rather than the floor. 3 gives 2, 4 gives 2,
	// 5 gives 3, 7 gives 4 and 8 gives 4, which is the owner's own table.
	return (Active + 1) / 2;
}

FName UCataclysmRoster::NameFor(ECataclysmType Cataclysm)
{
	switch (Cataclysm)
	{
	case ECataclysmType::Demonic:     return TEXT("Demonic");
	case ECataclysmType::Death:       return TEXT("Death");
	case ECataclysmType::War:         return TEXT("War");
	case ECataclysmType::Pestilence:  return TEXT("Pestilence");
	case ECataclysmType::Famine:      return TEXT("Famine");
	case ECataclysmType::Celestial:   return TEXT("Celestial");
	case ECataclysmType::Chaos:       return TEXT("Chaos");
	case ECataclysmType::Void:        return TEXT("Void");
	default:                          return TEXT("None");
	}
}
