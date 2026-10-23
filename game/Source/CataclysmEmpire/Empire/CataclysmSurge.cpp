// Copyright Stephen Dubois. All Rights Reserved.

#include "Empire/CataclysmSurge.h"

#include "DayClock/CataclysmDayClock.h"

// ---------------------------------------------------------------------------
// One dungeon
// ---------------------------------------------------------------------------

float FCataclysmDungeon::BiteScale() const
{
	const FCataclysmDungeonSpec Spec =
		UCataclysmSurgeScheduler::SpecFor(Type, CityTier);

	// A TYPICAL DUNGEON OF THIS KIND ON THIS TIER, which is the midpoint of its
	// floor range. `Simulation._resolve` scales the same way, and it is what
	// makes a dungeon rolled deep hurt more than one rolled shallow on the same
	// city rather than only taking longer to walk.
	const float Typical = (Spec.LeastFloors + Spec.MostFloors) / 2.0f;

	// THE FLOORS IT WAS ROLLED WITH, which is `Floors` less what the city's two
	// floor upgrades moved. Investment changes how deep a dungeon is, how long
	// it takes to walk and when its timer runs out; it does not change the size
	// of the hit. `UpgradeFloors` is nought on a dungeon nobody set it on, so a
	// dungeon built by hand is sized by all of its floors.
	return Typical <= 0.0f ? 1.0f : (Floors - UpgradeFloors) / Typical;
}

bool FCataclysmDungeon::AddsVoidStacks() const
{
	return Type == ECataclysmDungeonType::Basic
		&& Cataclysm == ECataclysmType::Void;
}

int32 FCataclysmDungeon::VoidStacksItAdds() const
{
	// THE ROW'S PLAIN RANGE; the header says why it is not the model's 1.25
	// times it.
	const FCataclysmDungeonSpec Spec =
		UCataclysmSurgeScheduler::SpecFor(Type, CityTier);

	const int32 Span = Spec.MostFloors - Spec.LeastFloors;
	if (Span <= 0)
	{
		return 1;
	}

	// WHOLE-NUMBER DIVISION, WHICH CUTS TOWARDS NOUGHT AS THE MODEL'S `int()`
	// DOES. Every operand is whole, so the answer is exact and no rounding of a
	// fraction can carry a depth across a third.
	const int32 Natural = Floors - UpgradeFloors;
	const int32 Third = 3 * (Natural - Spec.LeastFloors) / Span;
	return 1 + FMath::Clamp(Third, 0, 2);
}

bool FCataclysmDungeon::IsHollow() const
{
	return Type == ECataclysmDungeonType::Quest
		&& SubType == ECataclysmDungeonSubType::Hollow;
}

bool FCataclysmDungeon::IsAscendant() const
{
	return Type == ECataclysmDungeonType::Quest
		&& SubType == ECataclysmDungeonSubType::Ascendant;
}

int32 FCataclysmDungeon::WalkSeed() const
{
	// NEVER NOUGHT OR BELOW, the number `EnterEmpireDungeon` used before this
	// existed. A dungeon built by hand with no identifier still walks.
	return FMath::Max(1, DungeonId + 1);
}

int32 FCataclysmDungeon::HollowMinimum() const
{
	// HALF, ROUNDED UP, AND ONE AT LEAST. Every whole number up to sixteen
	// million is exact as a float, and so is half of it, so the ceiling sees
	// the true half and not a value a hair above it.
	return FMath::Max(1, FMath::CeilToInt(
		HollowFrom * UCataclysmSurgeScheduler::HollowShareKept));
}

int32 FCataclysmDungeon::HollowFloorsLeftToLose() const
{
	if (!IsHollow() || HollowFrom <= 0)
	{
		return 0;
	}

	// BY THE NATURAL FLOORS, AND NEVER DOWN TO NONE. The header says why both.
	const int32 Natural = Floors - UpgradeFloors;
	return FMath::Max(0, FMath::Min(Natural - HollowMinimum(), Floors - 1));
}

int32 FCataclysmDungeon::HollowStacksOnto(int32 CityStacks) const
{
	const int32 Span = HollowFrom - HollowMinimum();

	// ONE THAT CANNOT SHRINK ADDS THE MOST, as one that has shrunk all the way
	// does: `Simulation._hollow_stacks_for` answers 3 for it.
	int32 Stacks = 3;

	if (Span > 0)
	{
		const int32 Natural = Floors - UpgradeFloors;
		const int32 Lost = FMath::Clamp(HollowFrom - Natural, 0, Span);

		// ROUNDED HALF UP, as the model's `int(x + 0.5)` is. A share exactly
		// halfway, a quarter or three quarters of the way down, is exact in a
		// float, so it rounds up here as it does there.
		Stacks = FMath::FloorToInt(1.0f + 2.0f * Lost / Span + 0.5f);
	}

	// NO SINGLE HIT ERASES A CITY BELOW EIGHT. The owner's sentence is quoted
	// in the header.
	if (CityStacks < UCataclysmSurgeScheduler::HollowCannotEraseBelow)
	{
		Stacks = FMath::Min(
			Stacks, UCataclysmEmpireMap::VoidStacksToErase - 1 - CityStacks);
	}

	return FMath::Max(0, Stacks);
}

float FCataclysmDungeon::WalkDaysPerFloor() const
{
	const int32 Walked = FMath::Max(1, Floors);

	// NOBODY SET IT, SO A FLOOR COSTS THE ORDINARY DAY. A dungeon built by hand
	// in a test, or one from a save written before this field existed, behaves
	// exactly as it did before.
	if (WalkDays <= 0.0f)
	{
		return UCataclysmDayClock::DaysPerFloor;
	}

	return WalkDays / Walked;
}

// ---------------------------------------------------------------------------
// What a dungeon does differently
// ---------------------------------------------------------------------------

float UCataclysmSurgeScheduler::SpawnWeightFor(ECataclysmDungeonSubType SubType)
{
	// LISTED RATHER THAN DEFAULTED. Adding a sub-type to the enum without giving
	// it a weight is then a compiler warning, and not a value that silently never
	// spawns.
	switch (SubType)
	{
	// **NOT A WEIGHT OF ZERO STANDING IN FOR A REAL ONE.** There is no
	// `SpawnWeightNone` constant any more: the owner ruled on 2026-09-05 that
	// every dungeon a surge makes has a sub-type, so no roll can produce this
	// value and zero is the true answer to how often it is rolled. The enum
	// value itself is still needed, because a dungeon entered outside the empire
	// has no sub-type -- `UCataclysmGameMode::RunDungeonSubType` returns this,
	// and `UCataclysmEnemyScore::SubTypeWeight` gives it no difficulty.
	//
	// NOTHING A SURGE MAKES CARRIES IT. A Siege refused by the one-per-city cap
	// used to become one; since 2026-09-06 it is spread across the other six
	// instead. See `SiegesPerCity`.
	case ECataclysmDungeonSubType::None:			return 0.0f;
	case ECataclysmDungeonSubType::Timed:			return SpawnWeightTimed;
	case ECataclysmDungeonSubType::Horde:			return SpawnWeightHorde;
	case ECataclysmDungeonSubType::Siege:			return SpawnWeightSiege;
	case ECataclysmDungeonSubType::CowLevel:		return SpawnWeightCowLevel;
	case ECataclysmDungeonSubType::Elite:			return SpawnWeightElite;
	case ECataclysmDungeonSubType::Volatile:		return SpawnWeightVolatile;
	case ECataclysmDungeonSubType::Sacrificial:		return SpawnWeightSacrificial;
	// NEVER ROLLED, since 2026-10-22. The Void's Quest dungeon is a Hollow and
	// nothing else is; it is numbered past `Sacrificial`, so the roll never
	// asks. Listed so the switch still names every value.
	case ECataclysmDungeonSubType::Hollow:			return 0.0f;
	// NEVER ROLLED, since 2026-10-22, for the Hollow's reason: Celestial's Quest
	// dungeon is an Ascendant and nothing else is.
	case ECataclysmDungeonSubType::Ascendant:		return 0.0f;
	}

	return 0.0f;
}

FString UCataclysmSurgeScheduler::SubTypeName(ECataclysmDungeonSubType SubType)
{
	switch (SubType)
	{
	case ECataclysmDungeonSubType::Timed:		return TEXT("Timed");
	case ECataclysmDungeonSubType::Horde:		return TEXT("Horde");
	case ECataclysmDungeonSubType::Siege:		return TEXT("Siege");
	case ECataclysmDungeonSubType::CowLevel:	return TEXT("Cow Level");
	case ECataclysmDungeonSubType::Elite:		return TEXT("Elite");
	case ECataclysmDungeonSubType::Volatile:	return TEXT("Volatile");
	case ECataclysmDungeonSubType::Sacrificial:	return TEXT("Sacrificial");
	case ECataclysmDungeonSubType::Hollow:		return TEXT("Hollow");
	case ECataclysmDungeonSubType::Ascendant:	return TEXT("Ascendant");
	default:									return FString();
	}
}

FString UCataclysmSurgeScheduler::KindName(ECataclysmDungeonType Type)
{
	switch (Type)
	{
	case ECataclysmDungeonType::Quest:		return TEXT("Quest");
	case ECataclysmDungeonType::FallenCity:	return TEXT("Fallen City");
	case ECataclysmDungeonType::Cataclysm:	return TEXT("Cataclysm");
	default:								return FString();
	}
}

ECataclysmDungeonSubType UCataclysmSurgeScheduler::BarredSubTypeOn(
	ECataclysmDungeonType Type)
{
	// ONE KIND IS CONSTRAINED AND THREE ARE NOT. The owner's ruling of
	// 2026-09-06 names exactly one illegal pair of the 28 and says nothing about
	// the other 27; see `CataclysmForbiddenSubType`. A second `case` here is a
	// design decision and belongs to the owner.
	switch (Type)
	{
	case ECataclysmDungeonType::Cataclysm:	return CataclysmForbiddenSubType;
	default:								return ECataclysmDungeonSubType::None;
	}
}

float UCataclysmSurgeScheduler::TotalSpawnWeight(
	ECataclysmDungeonSubType Excluded, ECataclysmDungeonSubType AlsoExcluded)
{
	// THE ENUM IN ITS DECLARED ORDER, STARTING PAST `None`. Every value from
	// `Timed` up to and including `Sacrificial`, which is the last one. Walking
	// the enum rather than a hand-written list means a sub-type added to it
	// counts as soon as `SpawnWeightFor` gives it a weight.
	//
	// **IT STARTS AT `Timed` RATHER THAN AT ZERO BECAUSE `None` IS NOT AN
	// OUTCOME.** Every dungeon a surge makes has a sub-type; see the weights in
	// the header. Beginning at zero and relying on a weight of zero to skip it
	// would work and would read as though the value were still in the running.
	float Total = 0.0f;

	for (uint8 Value = static_cast<uint8>(ECataclysmDungeonSubType::Timed);
		 Value <= static_cast<uint8>(ECataclysmDungeonSubType::Sacrificial);
		 ++Value)
	{
		const ECataclysmDungeonSubType Candidate =
			static_cast<ECataclysmDungeonSubType>(Value);

		if (Candidate != Excluded && Candidate != AlsoExcluded)
		{
			Total += SpawnWeightFor(Candidate);
		}
	}

	return Total;
}

float UCataclysmSurgeScheduler::SpawnWeightBelow(
	ECataclysmDungeonSubType SubType, ECataclysmDungeonSubType Excluded)
{
	float Below = 0.0f;

	for (uint8 Value = static_cast<uint8>(ECataclysmDungeonSubType::Timed);
		 Value < static_cast<uint8>(SubType);
		 ++Value)
	{
		const ECataclysmDungeonSubType Candidate =
			static_cast<ECataclysmDungeonSubType>(Value);

		if (Candidate != Excluded)
		{
			Below += SpawnWeightFor(Candidate);
		}
	}

	return Below;
}

ECataclysmDungeonSubType UCataclysmSurgeScheduler::SubTypeAtPoint(
	float Point, ECataclysmDungeonSubType Excluded,
	ECataclysmDungeonSubType AlsoExcluded)
{
	const uint8 Last = static_cast<uint8>(ECataclysmDungeonSubType::Sacrificial);
	ECataclysmDungeonSubType Furthest = ECataclysmDungeonSubType::None;

	for (uint8 Value = static_cast<uint8>(ECataclysmDungeonSubType::Timed);
		 Value <= Last; ++Value)
	{
		const ECataclysmDungeonSubType Candidate =
			static_cast<ECataclysmDungeonSubType>(Value);

		if (Candidate == Excluded || Candidate == AlsoExcluded)
		{
			continue;
		}

		Furthest = Candidate;
		Point -= SpawnWeightFor(Candidate);

		if (Point < 0.0f)
		{
			return Candidate;
		}
	}

	// PAST THE END OF THE LINE. Reached when `FRand` returns exactly 1.0, which
	// its own documentation says it does not, and by any caller passing a point
	// larger than the total. The last sub-type on the line is the honest answer;
	// `None` would not be, because the caller asked which of THESE a number
	// picks.
	return Furthest;
}

ECataclysmDungeonSubType UCataclysmSurgeScheduler::RollSubType(
	FRandomStream& Stream, bool bSiegeAllowed, ECataclysmDungeonType Type)
{
	// **WHAT THIS KIND OF DUNGEON MAY NOT BE, TAKEN OFF THE LINE BEFORE THE
	// DRAW RATHER THAN REFUSED AFTER IT.** A Cataclysm may not be a Cow Level;
	// see `BarredSubTypeOn` and `CataclysmForbiddenSubType` for the ruling.
	//
	// WHY BEFORE. Shortening the line is what makes the barred sub-type's weight
	// land on the other six in proportion to theirs -- the gap closes rather
	// than staying open -- and it costs no second draw, which the promise above
	// forbids. Refusing after the draw would need one, or a re-read like the
	// Siege's; there is no need for either when the option was never on the line.
	//
	// AND IT LEAVES EVERY EXISTING CALLER'S STREAM EXACTLY WHERE IT WAS. Three
	// of the four kinds bar nothing, so `Barred` is `None`, which is not in the
	// distribution and excludes nothing. Nothing builds a Cataclysm dungeon in
	// the game yet -- see `SpecFor` -- so this rule answers rather than fires,
	// and it is here so that it cannot be forgotten when something does.
	const ECataclysmDungeonSubType Barred = BarredSubTypeOn(Type);

	const float Total = TotalSpawnWeight(Barred);

	// EVERY WEIGHT ZERO WOULD DIVIDE BY NOTHING. It cannot happen with the
	// constants in the header and it is one line to be safe about. A dungeon
	// with no sub-type is still the honest answer when nothing can be chosen,
	// even though nothing can reach it: the alternative is naming one sub-type
	// as the answer to "which of these zero options", which would be a lie.
	if (Total <= 0.0f)
	{
		return ECataclysmDungeonSubType::None;
	}

	// ONE DRAW, AND THE ONLY ONE. See the header.
	const float Point = Stream.FRand() * Total;
	const ECataclysmDungeonSubType Chosen = SubTypeAtPoint(Point, Barred);

	if (bSiegeAllowed || Chosen != ECataclysmDungeonSubType::Siege)
	{
		return Chosen;
	}

	// **THE SAME DRAW, READ A SECOND TIME INTO A SMALLER LINE.** The city
	// already holds a Siege, and the owner's ruling is that this dungeon is
	// spread across the other six rather than made plain or rolled again. See
	// `SiegesPerCity`.
	//
	// WHY RE-READING IS EXACT AND NOT AN APPROXIMATION. `Point` is uniform over
	// the whole line, so given that it landed inside Siege's own band it is
	// uniform over that band. Dividing by the band's width turns it into a
	// number uniform over 0 to 1, and multiplying by the total of the other six
	// turns that into a point uniform over a line made of exactly those six.
	// Each therefore takes a share of the refusals in proportion to its weight,
	// which is what "spread it across the others" asks for -- and it costs no
	// further randomness, because no further randomness is drawn.
	//
	// AND IT IS THE SHORTENED LINE THAT IS RE-READ, not the full one. `Point`
	// was drawn against `TotalSpawnWeight(Barred)`, so Siege's band starts and
	// the smaller line it maps onto both have to leave the same one out, or the
	// re-read would land in the wrong band.
	const float BandStart =
		SpawnWeightBelow(ECataclysmDungeonSubType::Siege, Barred);
	const float BandWidth =
		SpawnWeightFor(ECataclysmDungeonSubType::Siege);

	if (BandWidth <= 0.0f)
	{
		// SIEGE IS NOT ON THE LINE AT ALL, so `Chosen` could not have been one
		// and this is unreachable. It is here because dividing by it is not.
		return Chosen;
	}

	// CLAMPED BELOW ONE so the re-read point cannot land past the end of the
	// smaller line. `Point` is inside the band by construction; the clamp is
	// against floating-point arithmetic at its very edge, not against a real
	// case.
	const float Across = FMath::Clamp((Point - BandStart) / BandWidth, 0.0f,
									  1.0f - KINDA_SMALL_NUMBER);

	return SubTypeAtPoint(
		Across * TotalSpawnWeight(ECataclysmDungeonSubType::Siege, Barred),
		ECataclysmDungeonSubType::Siege, Barred);
}

ECataclysmDungeonSubType UCataclysmSurgeScheduler::QuestSubTypeFor(
	ECataclysmType Cataclysm)
{
	// SEVEN OF THE MODEL'S EIGHT, BY NAME. `config.QUEST_SUBTYPE_BY_CATACLYSM` is
	// the original; `tools/tests/test_dungeon_subtype_port.py` reads this switch
	// and compares the seven pairs with it. The Void's is the sixth and
	// Celestial's the seventh, both since 2026-10-22. War falls to `default`
	// with `None`, which means "roll"; the header says why.
	switch (Cataclysm)
	{
	case ECataclysmType::Death:			return ECataclysmDungeonSubType::Horde;
	case ECataclysmType::Demonic:		return ECataclysmDungeonSubType::Elite;
	case ECataclysmType::Pestilence:	return ECataclysmDungeonSubType::Timed;
	case ECataclysmType::Famine:		return ECataclysmDungeonSubType::Sacrificial;
	case ECataclysmType::Chaos:			return ECataclysmDungeonSubType::Volatile;
	case ECataclysmType::Void:			return ECataclysmDungeonSubType::Hollow;
	case ECataclysmType::Celestial:		return ECataclysmDungeonSubType::Ascendant;
	default:							return ECataclysmDungeonSubType::None;
	}
}

// ---------------------------------------------------------------------------
// Where a wave lands
// ---------------------------------------------------------------------------

float UCataclysmSurgeScheduler::TargetWeightFor(ECataclysmCityTier Tier)
{
	switch (Tier)
	{
	case ECataclysmCityTier::Bulwark:	return BulwarkTargetWeight;
	case ECataclysmCityTier::Sanctuary:	return SanctuaryTargetWeight;
	case ECataclysmCityTier::Pillar:	return PillarTargetWeight;
	default:							return OutpostTargetWeight;
	}
}

float UCataclysmSurgeScheduler::TargetWeightForSender(
	const FCataclysmCity& City, ECataclysmType Sender)
{
	const float Weight = TargetWeightFor(City.Tier);

	if (Sender != ECataclysmType::Void)
	{
		return Weight;
	}

	// NOUGHT STACKS MULTIPLY BY EXACTLY ONE, so a Void slot on a city with none
	// weighs the same float an ordinary slot does. Ten or more pull by the
	// whole `VoidTargetPullAtFull`.
	const float Share = FMath::Min(
		1.0f, static_cast<float>(City.VoidStacks)
				  / static_cast<float>(UCataclysmEmpireMap::VoidStacksToErase));

	return Weight * (1.0f + VoidTargetPullAtFull * Share);
}

// ---------------------------------------------------------------------------
// What a dungeon is
// ---------------------------------------------------------------------------

FCataclysmDungeonSpec UCataclysmSurgeScheduler::SpecFor(
	ECataclysmDungeonType Type, ECataclysmCityTier Tier)
{
	FCataclysmDungeonSpec Spec;

	switch (Type)
	{
	case ECataclysmDungeonType::Basic:
		// THE DAMAGE COLUMNS ARE POINTS AND PEOPLE, NOT FRACTIONS OF THE CITY.
		// Issue #1331, and `config.DUNGEON_SPECS` holds the same four pairs.
		// Each one was first the fraction it replaced multiplied by that
		// tier's base maximum, so the table reproduced the old arithmetic
		// exactly for a city at its base size and the change of shape could be
		// measured on its own before any number was chosen:
		//
		//   Outpost    10% of  1,000 =   100 defence,  5% of   5,000 =   250
		//   Bulwark      9% of 3,000 =   270 defence,  5% of  20,000 = 1,000
		//   Sanctuary    8% of 8,000 =   640 defence,  4% of  60,000 = 2,400
		//   Pillar     6% of 20,000 = 1,200 defence,  3% of 150,000 = 4,500
		//
		// TIMES 0.7 SINCE 2026-10-20, IN THE MODEL SINCE 2026-10-16: 70 and
		// 175 on an Outpost, 189 and 700 on a Bulwark, 448 and 1,680 on a
		// Sanctuary, 840 and 3,150 on the Pillar. The owner, 2026-10-16,
		// verbatim: "the obvious solution here is to adjust the damage
		// dungeons do." The coordinating session chose 0.7, together with the
		// bigger waves on `DungeonsPerSurge` and `OpeningDungeonsPerSurge`,
		// under the owner's delegation of numbers. A Quest dungeon's hit reads
		// these rows too. The floors did not move.
		//
		// THE TIER MAXIMUMS ARE `UCataclysmEmpireMap::OutpostMaxDefence` and
		// its seven siblings. `tools/tests/test_surge_port.py` ties these four
		// pairs to the model's and to a third hand-written copy.
		switch (Tier)
		{
		case ECataclysmCityTier::Bulwark:
			Spec.LeastFloors = 15;
			Spec.MostFloors = 25;
			Spec.DefenceDamage = 189.0f;
			Spec.PopulationDamage = 700.0f;
			break;

		case ECataclysmCityTier::Sanctuary:
			Spec.LeastFloors = 25;
			Spec.MostFloors = 40;
			Spec.DefenceDamage = 448.0f;
			Spec.PopulationDamage = 1680.0f;
			break;

		case ECataclysmCityTier::Pillar:
			Spec.LeastFloors = 40;
			Spec.MostFloors = 60;
			Spec.DefenceDamage = 840.0f;
			Spec.PopulationDamage = 3150.0f;
			break;

		default:
			Spec.LeastFloors = 8;
			Spec.MostFloors = 15;
			Spec.DefenceDamage = 70.0f;
			Spec.PopulationDamage = 175.0f;
			break;
		}
		break;

	case ECataclysmDungeonType::Quest:
		// THE ZERO DAMAGE IS THE DESIGN AND NOT A MISSING NUMBER. Every Quest
		// row in `config.DUNGEON_SPECS` is 0.0/0.0. A Quest dungeon's timer
		// does hit its host since 2026-10-13, and that hit is not read from
		// these rows: it is the BASIC row of the host's tier, in
		// `UCataclysmEmpireRun::ResolveDungeon` and `Simulation._quest_hit`.
		switch (Tier)
		{
		case ECataclysmCityTier::Bulwark:
			Spec.LeastFloors = 30;
			Spec.MostFloors = 45;
			break;

		case ECataclysmCityTier::Sanctuary:
			Spec.LeastFloors = 30;
			Spec.MostFloors = 50;
			break;

		case ECataclysmCityTier::Pillar:
			Spec.LeastFloors = 50;
			Spec.MostFloors = 70;
			break;

		default:
			Spec.LeastFloors = 20;
			Spec.MostFloors = 30;
			break;
		}
		break;

	case ECataclysmDungeonType::FallenCity:
		// DEEPER THAN A BASIC DUNGEON ON THE SAME CITY, ROUGHLY TWO AND A HALF
		// TIMES. The design's minimums are "20/40/60 for
		// Outpost/Bulwark/Sanctuary" and these ranges start exactly there.
		//
		// AND IT BITES NOTHING, because the city it stands on has already
		// fallen. `Dungeon.resolves` in the model says so: "Fallen City and
		// Cataclysm dungeons have already done their damage."
		switch (Tier)
		{
		case ECataclysmCityTier::Bulwark:
			Spec.LeastFloors = 40;
			Spec.MostFloors = 60;
			break;

		case ECataclysmCityTier::Sanctuary:
			Spec.LeastFloors = 60;
			Spec.MostFloors = 85;
			break;

		case ECataclysmCityTier::Pillar:
			Spec.LeastFloors = 80;
			Spec.MostFloors = 120;
			break;

		default:
			Spec.LeastFloors = 20;
			Spec.MostFloors = 35;
			break;
		}
		break;

	case ECataclysmDungeonType::Cataclysm:
		// AT THE PILLAR AND NOWHERE ELSE, AND EVERY OTHER TIER IS LEFT UNBUILT
		// DELIBERATELY. `config.DUNGEON_SPECS` holds exactly one Cataclysm row,
		// `(CATACLYSM, PILLAR)`, and `TuningConfig.spec` is a bare dictionary
		// lookup -- so asking the model for a Cataclysm on an Outpost raises
		// rather than answering. A spec whose `IsBuilt` is false is the same
		// answer here, where a lookup cannot raise.
		if (Tier == ECataclysmCityTier::Pillar)
		{
			Spec.LeastFloors = 100;
			Spec.MostFloors = 150;
		}
		break;
	}

	return Spec;
}

// ---------------------------------------------------------------------------
// The schedule
// ---------------------------------------------------------------------------

float UCataclysmSurgeScheduler::DungeonMultiplier() const
{
	// 2 IS HERETIC. See `LethalityRung`; the numbering is `ECataclysmLethality`'s
	// and `tools/tests/test_day_clock_port.py` proves it stays that way.
	return LethalityRung == 2 ? HereticDungeonMultiplier : 1.0f;
}

int32 UCataclysmSurgeScheduler::DungeonsInNextSurge() const
{
	// THE OPENING WAVE HAS ITS OWN SIZE, since 2026-10-20 in the game and
	// 2026-10-16 in the model: at `SurgeIndex` 0 the wave starts from
	// `OpeningDungeonsPerSurge`, and at every later index from
	// `DungeonsPerSurge`. `Simulation.surge_count` asks the same question of
	// `surge_index`.
	//
	// THE INDEX AND NOT `SurgesFired`, because that is what the model asks.
	// `RecordSurge` raises `SurgeIndex` after each surge, after the gap is
	// read, as `Simulation.trigger_surge` raises `surge_index`, so the first
	// wave of a run is the only one rolled at 0. A fall-triggered surge
	// that did not advance the counter would leave it there, but
	// `bCityFallAdvancesEscalation` is true.
	float Count = static_cast<float>(SurgeIndex == 0 ? OpeningDungeonsPerSurge
													 : DungeonsPerSurge);

	if (Mode == ECataclysmSurgeMode::Swelling || Mode == ECataclysmSurgeMode::Both)
	{
		Count += CountGrowthPerSurge * SurgeIndex;
	}

	Count = FMath::Min(Count, static_cast<float>(MostDungeonsPerSurge));

	// THE CAP FIRST AND THE LETHALITY MODE SECOND. See
	// `HereticDungeonMultiplier`: the order is what stops Heretic collapsing
	// into Standard at the cap.
	return FMath::Max(1, FMath::TruncToInt(Count * DungeonMultiplier()));
}

float UCataclysmSurgeScheduler::GapAfterThisSurge() const
{
	float Gap = IntervalDays;

	if (Mode == ECataclysmSurgeMode::Accelerating
		|| Mode == ECataclysmSurgeMode::Both)
	{
		Gap *= FMath::Pow(IntervalDecay, static_cast<float>(SurgeIndex));
	}

	return FMath::Max(LeastIntervalDays, Gap);
}

bool UCataclysmSurgeScheduler::IsDue(int32 Day) const
{
	return static_cast<float>(Day) >= NextSurgeDay;
}

float UCataclysmSurgeScheduler::DaysUntilNextSurge(int32 Day) const
{
	return FMath::Max(0.0f, NextSurgeDay - static_cast<float>(Day));
}

float UCataclysmSurgeScheduler::DaysSinceLastSurge(int32 Day) const
{
	return static_cast<float>(Day - LastSurgeDay);
}

bool UCataclysmSurgeScheduler::IsDueOnEmptyBoard(int32 Day,
												 int32 DungeonsStanding) const
{
	// THE INTERVAL IS THE ONLY BRAKE. A board can be emptied the day after a
	// wave lands, and without it a player who clears quickly would be sent a
	// wave every day.
	//
	// ONE EXPRESSION AND NOT THREE EARLY RETURNS, because the flag is a constant
	// and a block behind a constant condition is code the compiler can call
	// unreachable.
	return bSurgeOnEmptyBoard
		&& DungeonsStanding <= 0
		&& DaysSinceLastSurge(Day) >= LeastIntervalDays;
}

bool UCataclysmSurgeScheduler::IsDueOnCityFall(int32 Day) const
{
	// THE SAME BRAKE THE EMPTY BOARD HAS, AND THE SAME COMPARISON. The model
	// fires when the gap is at or above `surge_interval_min`, so `>` here
	// would refuse the fall that comes exactly 25 days after a surge.
	//
	// ON ONE LINE, so the port check in `tools/tests/test_surge_port.py` and
	// a guard proof can each name the whole condition as one line of text.
	return bSurgeOnCityFall && DaysSinceLastSurge(Day) >= LeastIntervalDays;
}

void UCataclysmSurgeScheduler::RecordSurge(int32 Day, bool bFromCityFall)
{
	++SurgesFired;

	// WHAT `IsDueOnEmptyBoard` AND `IsDueOnCityFall` MEASURE THEIR INTERVAL
	// FROM. See `LastSurgeDay`.
	LastSurgeDay = Day;

	// THE GAP IS READ BEFORE THE COUNTER MOVES, which is the order
	// `Simulation.trigger_surge` uses. Reading it afterwards would skip the
	// first step of an accelerating run's decay.
	NextSurgeDay = static_cast<float>(Day) + GapAfterThisSurge();

	if (!bFromCityFall || bCityFallAdvancesEscalation)
	{
		++SurgeIndex;
	}
}

// ---------------------------------------------------------------------------
// The wave
// ---------------------------------------------------------------------------

int32 UCataclysmSurgeScheduler::RoomLeftOn(
	const FCataclysmCity& City, const TArray<int32>& DungeonsPerCity)
{
	// NO CAP MEANS NO LIMIT. A city that never bought the upgrade carries zero
	// for the effect, and zero here is "no cap" rather than "no room" -- reading
	// it the other way would stop every wave landing anywhere at all.
	const float Cap =
		City.UpgradeValueFor(ECataclysmCityUpgradeEffect::DungeonCap);

	if (Cap <= 0.0f)
	{
		return MAX_int32;
	}

	// AND NEITHER DOES A CALLER THAT DID NOT SAY. `UCataclysmEmpireRun` owns the
	// dungeons; this class deliberately does not, and a test that rolls a wave
	// straight off a map has none to count.
	if (!DungeonsPerCity.IsValidIndex(City.CityId))
	{
		return MAX_int32;
	}

	return FMath::Max(0, FMath::RoundToInt(Cap) - DungeonsPerCity[City.CityId]);
}

TArray<int32> UCataclysmSurgeScheduler::PickTargets(
	const UCataclysmEmpireMap& Map, int32 Count, FRandomStream& Stream,
	const TArray<int32>& DungeonsPerCity,
	const TFunction<ECataclysmType()>& DrawCataclysm,
	TArray<ECataclysmType>* OutSenders) const
{
	TArray<int32> Landed;

	if (OutSenders != nullptr)
	{
		OutSenders->Reset();
	}

	// THE FRONTIER, AND THE PILLAR IS NOT IN IT. `ExposedCities` leaves the
	// Pillar out unless asked for, which is the same rule
	// `PillarTargetWeight` states from the other side. Both are here because
	// either alone would be a single point of failure for a rule the design
	// states plainly.
	const TArray<int32> Exposed = Map.ExposedCities();
	if (Exposed.IsEmpty() || Count <= 0)
	{
		return Landed;
	}

	// A CITY WEIGHTED AT NOTHING IS LEFT OUT RATHER THAN LEFT IN WITH A ZERO.
	// A cumulative walk over a list holding zeroes can land on one of them, when
	// the roll happens to arrive at a boundary exactly, and a tier weighted at
	// nothing means "never", not "rarely".
	TArray<int32> Candidates;
	TArray<float> Weights;

	// HOW MANY MORE EACH CANDIDATE WILL ACCEPT, counted down as the wave is
	// built. A city with no dungeon cap carries `MAX_int32` and is never
	// removed, which is what makes a run with no caps roll exactly what it
	// rolled before this existed.
	TArray<int32> Room;

	float Total = 0.0f;
	for (const int32 CityId : Exposed)
	{
		const FCataclysmCity* City = Map.Find(CityId);
		const float Weight = City ? TargetWeightFor(City->Tier) : 0.0f;
		if (Weight <= 0.0f)
		{
			continue;
		}

		// A CITY ALREADY AT ITS CAP IS NOT A CANDIDATE AT ALL.
		const int32 Left = RoomLeftOn(*City, DungeonsPerCity);
		if (Left <= 0)
		{
			continue;
		}

		Candidates.Add(CityId);
		Weights.Add(Weight);
		Room.Add(Left);
		Total += Weight;
	}

	if (Candidates.IsEmpty() || Total <= 0.0f)
	{
		// NOTHING LEFT TO LAND ON. Either every exposed city is weighted at
		// nothing -- only the Pillar is, so that cannot happen on a built map --
		// or every one of them has reached its dungeon cap, which can.
		return Landed;
	}

	Landed.Reserve(Count);

	// A VOID SLOT'S OWN WEIGHTS, rebuilt for each such slot from the candidates
	// still holding room. Kept outside the loop only so it is not reallocated.
	TArray<float> Pulled;

	for (int32 Rolled = 0; Rolled < Count; ++Rolled)
	{
		// WHO SENDS THIS SLOT, ASKED BEFORE IT IS ROLLED, since 2026-10-22,
		// because the Void aims by its stacks. One ask per city landed on, as
		// `RollWave` asked after the targets until then; the answer comes off
		// the run's own Cataclysm stream, so `Stream` below takes the draw it
		// always took.
		const ECataclysmType Sender =
			DrawCataclysm ? DrawCataclysm() : ECataclysmType::None;

		// THE TIER WEIGHTS AND THEIR RUNNING TOTAL, UNLESS THE VOID SENDS THE
		// SLOT AND SOME CANDIDATE CARRIES STACKS. Only then is a second set of
		// weights summed afresh. A Void slot on an empire with no stacks keeps
		// `Weights` and `Total` themselves -- not a recomputed sum, which after
		// a full city has been taken out could differ from `Total` in its last
		// bit and move the city a roll lands on.
		const TArray<float>* SlotWeights = &Weights;
		float SlotTotal = Total;

		if (Sender == ECataclysmType::Void)
		{
			Pulled.Reset(Candidates.Num());

			float PulledTotal = 0.0f;
			bool bAnyPulled = false;

			for (int32 Index = 0; Index < Candidates.Num(); ++Index)
			{
				const FCataclysmCity* City = Map.Find(Candidates[Index]);
				const float Weight = City
					? TargetWeightForSender(*City, Sender)
					: Weights[Index];

				bAnyPulled = bAnyPulled || Weight != Weights[Index];
				Pulled.Add(Weight);
				PulledTotal += Weight;
			}

			if (bAnyPulled)
			{
				SlotWeights = &Pulled;
				SlotTotal = PulledTotal;
			}
		}

		// WITH REPLACEMENT. Two dungeons of one wave may land on the same city,
		// which is how a wave concentrates rather than spreading evenly.
		float Roll = Stream.FRand() * SlotTotal;

		int32 Chosen = Candidates.Num() - 1;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			Roll -= (*SlotWeights)[Index];
			if (Roll <= 0.0f)
			{
				Chosen = Index;
				break;
			}
		}

		Landed.Add(Candidates[Chosen]);

		if (OutSenders != nullptr)
		{
			OutSenders->Add(Sender);
		}

		// AND THAT CITY HAS ONE LESS ROOM. A city one short of its cap can be
		// chosen twice in one wave, because the roll is with replacement, so
		// counting down only when the wave is finished would let it go over.
		//
		// A CITY WITH NO CAP COUNTS DOWN FROM `MAX_int32` AND NEVER REACHES
		// ZERO, so nothing below runs for a run that bought no caps and the
		// draws stay identical to what they were.
		if (--Room[Chosen] > 0)
		{
			continue;
		}

		// FULL, SO IT STOPS BEING A CANDIDATE AND THE WAVE LANDS ELSEWHERE.
		// `Total` has to lose its weight with it or every later roll would be
		// scaled against a total that includes a city it can no longer pick, and
		// the walk would fall off the end onto whichever city happens to be last.
		// THE TIER WEIGHT, whoever sent this slot: `Total` is the tier weights'
		// total, and a later Void slot sums its own.
		Total -= Weights[Chosen];

		Candidates.RemoveAt(Chosen);
		Weights.RemoveAt(Chosen);
		Room.RemoveAt(Chosen);

		if (Candidates.IsEmpty() || Total <= 0.0f)
		{
			// EVERY EXPOSED CITY IS FULL. The rest of the wave has nowhere to go
			// and the wave is short, which is the honest answer rather than
			// putting dungeons somewhere the design forbids.
			break;
		}
	}

	return Landed;
}

FCataclysmDungeon UCataclysmSurgeScheduler::MakeFallenCityDungeon(
	int32 DungeonId, const FCataclysmCity& City, int32 Day,
	int32 DungeonsAbsorbed)
{
	const FCataclysmDungeonSpec Spec =
		SpecFor(ECataclysmDungeonType::FallenCity, City.Tier);

	FCataclysmDungeon Dungeon;
	Dungeon.DungeonId = DungeonId;
	Dungeon.Type = ECataclysmDungeonType::FallenCity;
	Dungeon.CityId = City.CityId;
	Dungeon.CityTier = City.Tier;
	Dungeon.SpawnedDay = Day;

	// AT LEAST ONE, AND SINCE 2026-10-21 IT CAN HAVE TO BE. A Quest dungeon
	// standing on a falling city moves away rather than being absorbed, so a
	// city whose own Quest dungeon's hit felled it, with nothing else standing
	// there, absorbs none. The coordinating session ruled on 2026-10-21 that
	// it still becomes a Dungeon City with one boss. Until then this said the
	// count was one or more whenever this was called, because the dungeon
	// that felled the city was always absorbed with the rest. A bossless,
	// zero-floor dungeon would be worse than a floor of one.
	const int32 Absorbed = FMath::Max(1, DungeonsAbsorbed);

	// THE FLOOR IS THE SPEC'S SHALLOW END, which is the design's 20/40/60. A
	// city that fell carrying more dungeons than that is deeper than the
	// minimum; a quiet one is exactly it.
	Dungeon.Floors = FMath::Max(Absorbed, Spec.LeastFloors);

	// ONE BOSS PER DUNGEON THAT WAS STANDING, which is the same count and not
	// the same number as the floors: a city that fell holding three dungeons is
	// a twenty floor dungeon with three bosses in it.
	Dungeon.Bosses = Absorbed;

	// IT NEVER RESOLVES, so its timer is set past the end of any run rather
	// than derived from its depth.
	Dungeon.ResolveDays = FallenCityResolveDays;

	// AND IT TAKES NOTHING. `SpecFor` answers zero for both, and copying them
	// rather than writing zeroes here means a change to the spec cannot leave
	// this behind.
	Dungeon.DefenceDamage = Spec.DefenceDamage;
	Dungeon.PopulationDamage = Spec.PopulationDamage;

	// NO SUB-TYPE. A Fallen City is what a city became rather than something a
	// surge rolled, and the sub-types are what a surge rolls.
	Dungeon.SubType = ECataclysmDungeonSubType::None;

	return Dungeon;
}

ECataclysmDungeonType UCataclysmSurgeScheduler::RollKind(FRandomStream& Stream)
{
	// STRICTLY LESS THAN, which is what `self.rng.random() < quest_dungeon_chance`
	// is in `Simulation.trigger_surge`. `FRandomStream::FRand` draws from [0, 1)
	// exactly as `random.random` does, so a chance of zero can never come out
	// true and a chance of one always does.
	return Stream.FRand() < QuestChance
		? ECataclysmDungeonType::Quest
		: ECataclysmDungeonType::Basic;
}

TArray<int32> UCataclysmSurgeScheduler::AdjacentCities(
	const FCataclysmCity& City)
{
	TArray<int32> Neighbours;
	Neighbours.Reserve(City.Outward.Num() + City.Inward.Num()
					   + City.Perimeter.Num());

	Neighbours.Append(City.Outward);
	Neighbours.Append(City.Inward);

	// THE RIM'S CURVED EDGES, AND THEY ARE ONLY ON THE RIM. `Perimeter` is
	// empty for every city inside it, so this costs nothing there and needs no
	// test on the tier. See the header for why they are counted at all.
	Neighbours.Append(City.Perimeter);

	return Neighbours;
}

int32 UCataclysmSurgeScheduler::PickRelocation(
	const UCataclysmEmpireMap& Map,
	const FCataclysmCity& From,
	FRandomStream& Stream,
	bool bCarriesSiege,
	const TArray<int32>& SiegesPerCityNow)
{
	TArray<int32> Targets;

	for (const int32 CityId : AdjacentCities(From))
	{
		// THE PILLAR IS NEVER A TARGET. `ExposedCities` leaves it out of a
		// surge's targets by default and the model's `exposed_cities` does the
		// same; a dungeon reaching the Pillar is the Last Stand, issue #43, and
		// not a quest dungeon wandering onto it.
		if (CityId == Map.PillarId)
		{
			continue;
		}

		// AND A SEALED OR FALLEN ONE IS NOT EITHER. `IsExposed` answers false
		// for both, so a Quest dungeon can only move somewhere a surge could
		// have landed one -- which also means it never moves onto a fallen city
		// and lands on top of the Fallen City dungeon standing there.
		if (!Map.IsExposed(CityId))
		{
			continue;
		}

		// **AND A CITY THAT ALREADY HAS A SIEGE IS NOT A TARGET FOR A DUNGEON
		// CARRYING ONE.** "Max 1 per city" -- the design's Siege row -- was
		// enforced at spawn by `RollSubType` and by nothing here, so a Quest
		// dungeon that had rolled Siege could walk onto a besieged city and the
		// city would take the daily bite twice. The project owner ruled on
		// 2026-09-06, verbatim "Check the limit on arrival too". Issue #1371.
		//
		// THE TEST IS BEFORE THE DRAW AND NOT AFTER IT, so the remaining
		// neighbours keep their full share of the draw. Refusing afterwards
		// would either cost a second draw or cancel a move the owner said
		// should be redirected.
		//
		// AN EMPTY ARRAY MEANS THE CALLER DID NOT SAY, the same convention
		// `RollWave` uses for the same count, so a test rolling against a bare
		// map is not silently given a rule it did not ask for.
		if (bCarriesSiege
			&& SiegesPerCityNow.IsValidIndex(CityId)
			&& SiegesPerCityNow[CityId] >= SiegesPerCity)
		{
			continue;
		}

		Targets.Add(CityId);
	}

	if (Targets.Num() == 0)
	{
		// NOWHERE ADJACENT TO GO, SO IT STAYS. Not a failure: a dungeon whose
		// neighbours are all sealed, fallen, the Pillar, or -- if it carries a
		// Siege -- already besieged, is hemmed in. No draw is taken, which is
		// what `Simulation._resolve` does -- its `self.rng.choice` sits behind
		// `if targets` -- and taking one here would put the two streams out of
		// step for the rest of the run.
		//
		// **THAT IS ONE OF THE TWO REASONS A QUEST DUNGEON STAYS AND THIS
		// COMMENT USED TO SAY IT WAS THE WHOLE OF THE DESIGN'S "MAY".** The
		// other is the coin, `QuestMoveChance`, flipped by
		// `UCataclysmEmpireRun::RelocateQuestDungeon` after this answers. The
		// owner ruled it on 2026-09-06, verbatim "A chance each time".
		//
		// THE COIN IS NOT DRAWN HERE, AND KEEPING IT OUT IS DELIBERATE. This
		// answering `INDEX_NONE` means the map refused, and a caller -- or a
		// test -- can hold it to that. See the header.
		return INDEX_NONE;
	}

	return Targets[Stream.RandRange(0, Targets.Num() - 1)];
}

int32 UCataclysmSurgeScheduler::PickRefuge(
	const UCataclysmEmpireMap& Map,
	const FCataclysmCity& Fallen,
	FRandomStream& Stream,
	bool bCarriesSiege,
	const TArray<int32>& SiegesPerCityNow)
{
	// THE THREE TESTS THE TIMER'S MOVE MAKES, AND ONLY THOSE. Not the Pillar,
	// exposed -- which a fallen or a sealed city is not -- and, for a dungeon
	// carrying a Siege, not a city already holding one.
	// `Simulation._quest_refuge` asks the same: `is_exposed`, not the
	// Pillar, and its `allowed`. An empty count means the caller did not say
	// and refuses nothing, the convention `RollWave` uses.
	const auto Qualifies = [&Map, bCarriesSiege, &SiegesPerCityNow](int32 CityId)
	{
		if (CityId == Map.PillarId || !Map.IsExposed(CityId))
		{
			return false;
		}

		return !(bCarriesSiege
				 && SiegesPerCityNow.IsValidIndex(CityId)
				 && SiegesPerCityNow[CityId] >= SiegesPerCity);
	};

	TArray<int32> Targets;

	// AN ADJACENT CITY FIRST. The owner's words name it -- "They just move to
	// an adjacent city" -- and the model asks `adjacent_exposed_cities` first
	// for the same reason.
	for (const int32 CityId : AdjacentCities(Fallen))
	{
		if (Qualifies(CityId))
		{
			Targets.Add(CityId);
		}
	}

	// THEN THE NEAREST QUALIFYING CITY, COUNTED IN LANES. Ring by ring,
	// breadth first, over every link and through any city: a fallen or sealed
	// city is not somewhere to stop, but it is somewhere to pass through. The
	// first ring holding a city that qualifies is the answer's ring. The
	// order a ring is built in is the model's -- each city of the ring before
	// it in turn, each one's links in `AdjacentCities` order, each city once --
	// so the one draw below picks from the same list there and here.
	//
	// RING ONE IS THE NEIGHBOURS AGAIN, and none of them qualified or this
	// loop would not run. Searching it a second time costs nothing and keeps
	// the shape the model's search has.
	TArray<bool> Seen;
	Seen.Init(false, Map.Cities.Num());
	if (Seen.IsValidIndex(Fallen.CityId))
	{
		Seen[Fallen.CityId] = true;
	}

	TArray<int32> Frontier;
	Frontier.Add(Fallen.CityId);

	while (Targets.Num() == 0 && Frontier.Num() > 0)
	{
		TArray<int32> Ring;

		for (const int32 Inner : Frontier)
		{
			const FCataclysmCity* InnerCity = Map.Find(Inner);
			if (InnerCity == nullptr)
			{
				continue;
			}

			for (const int32 Next : AdjacentCities(*InnerCity))
			{
				if (Seen.IsValidIndex(Next) && !Seen[Next])
				{
					Seen[Next] = true;
					Ring.Add(Next);
				}
			}
		}

		for (const int32 CityId : Ring)
		{
			if (Qualifies(CityId))
			{
				Targets.Add(CityId);
			}
		}

		Frontier = MoveTemp(Ring);
	}

	if (Targets.Num() == 0)
	{
		// NOWHERE QUALIFIES ANYWHERE ON THE MAP, SO IT STAYS on the fallen
		// city. That happens only once no exposed city but the Pillar is left,
		// or every one left already holds a Siege and this dungeon carries
		// one. NO DRAW IS TAKEN, which is the model's `if targets else None`.
		return INDEX_NONE;
	}

	// ONE DRAW AMONG THE NEAREST RING'S CITIES THAT QUALIFY, and the tie is
	// all it breaks. `self.rng.choice(targets)` in the model.
	return Targets[Stream.RandRange(0, Targets.Num() - 1)];
}

FCataclysmDungeon UCataclysmSurgeScheduler::MakeDungeon(
	int32 DungeonId, const FCataclysmCity& City, int32 Day,
	FRandomStream& Stream, bool bSiegeAllowed, ECataclysmDungeonType Type,
	ECataclysmType Cataclysm) const
{
	// A SURGE ROLLS TWO KINDS AND `RollKind` IS WHERE THAT IS DECIDED. A Fallen
	// City is built by `MakeFallenCityDungeon` from what the city was carrying,
	// and nothing builds a Cataclysm yet -- so neither reaches here. Asked for
	// one anyway, this builds a Basic rather than a dungeon carrying a Fallen
	// City's floors and a Quest's timer, which is a shape nothing in the design
	// describes.
	const ECataclysmDungeonType Kind =
		(Type == ECataclysmDungeonType::Quest)
			? ECataclysmDungeonType::Quest
			: ECataclysmDungeonType::Basic;

	const FCataclysmDungeonSpec Spec = SpecFor(Kind, City.Tier);

	FCataclysmDungeon Dungeon;
	Dungeon.DungeonId = DungeonId;
	Dungeon.Type = Kind;

	// ONE BOSS, ON THE FINAL FLOOR. The design's universal rule, and set here
	// rather than left to the field's default so that a wave-rolled dungeon and
	// a Fallen City are visibly answering the same question differently. A Quest
	// dungeon is not the exception a Fallen City is: the design names only a
	// Dungeon City as having "multiple boss fights".
	Dungeon.Bosses = 1;
	Dungeon.CityId = City.CityId;
	Dungeon.CityTier = City.Tier;
	Dungeon.SpawnedDay = Day;
	Dungeon.DefenceDamage = Spec.DefenceDamage;
	Dungeon.PopulationDamage = Spec.PopulationDamage;

	// INCLUSIVE AT BOTH ENDS, which is what `FRandomStream::RandRange` is and
	// what `random.randint` is, so the deepest dungeon the table describes can
	// actually be rolled.
	Dungeon.Floors = FMath::Max(1, Stream.RandRange(Spec.LeastFloors,
													Spec.MostFloors));

	// KEPT, because the hit is sized by this and not by what the upgrades
	// below make of it. See `FCataclysmDungeon::UpgradeFloors`.
	const int32 Rolled = Dungeon.Floors;

	// AND THEN THE CITY'S OWN UPGRADES MOVE IT. Both are read and the difference
	// applied once, so a city holding both ends up with the net change rather
	// than with whichever happened to be checked second.
	//
	// AFTER THE ROLL AND NOT INSTEAD OF IT, so the depth a dungeon would have
	// had is still drawn from the same range with the same draw. Rolling a
	// different range would change every later roll in the run.
	const int32 Deeper = FMath::RoundToInt(
		City.UpgradeValueFor(ECataclysmCityUpgradeEffect::DungeonFloorsMore));

	const int32 Shallower = FMath::RoundToInt(
		City.UpgradeValueFor(ECataclysmCityUpgradeEffect::DungeonFloorsFewer));

	// A MINIMUM OF ONE, which is what "to a minimum of 1" in the sheet says and
	// what a dungeon with no floors would otherwise be: unwalkable.
	Dungeon.Floors = FMath::Max(1, Dungeon.Floors + Deeper - Shallower);

	// WHAT THE UPGRADES MOVED, TAKEN AFTER THE CLAMP. Not `Deeper - Shallower`:
	// where the clamp at one floor stopped the change short, the two differ,
	// and `BiteScale` must get back exactly the floors that were rolled.
	Dungeon.UpgradeFloors = Dungeon.Floors - Rolled;

	// AND WHAT IT DOES DIFFERENTLY. Rolled after the depth so the depth is drawn
	// from the same place in the stream it always was, and before the walk cost
	// because Cow Level decides what that cost is.
	//
	// `Kind` AND NOT `Type`, because `Kind` is what this dungeon actually became
	// and `Type` is what was asked for. Both are Basic or Quest here and neither
	// bars anything, so this changes no draw today; it is passed so that the one
	// rule `RollSubType` knows about -- a Cataclysm may not be a Cow Level -- is
	// wired up rather than waiting to be remembered.
	Dungeon.SubType = RollSubType(Stream, bSiegeAllowed, Kind);

	// **A QUEST DUNGEON TAKES ITS CATACLYSM'S SUB-TYPE, AND THE DRAW ABOVE IS
	// STILL TAKEN AND THROWN AWAY.** The owner, 2026-10-16: "Each cataclysm's
	// quest dungeons will be one specific type." Five Cataclysms name one in the
	// game; see `QuestSubTypeFor`, which answers `None` -- roll -- for the rest.
	//
	// WHY THE DRAW IS NOT SKIPPED. `RollSubType` promises one draw, always, so
	// that what one dungeon rolled never moves the dungeons after it. A fixed
	// Quest dungeon that skipped it would take one number fewer off the stream,
	// and every later depth, timer and sub-type in the run would shift with
	// which Cataclysm happened to send a Quest dungeon. Discarding the answer
	// keeps the stream exactly where it was before this rule existed. The model
	// skips its draw instead; its stream is its own, and nothing compares the
	// two roll by roll. Ruled by the coordinating session, 2026-10-21.
	//
	// HERE, BEFORE THE WALK COST, because the walk cost reads the sub-type: a
	// fixed sub-type must not leave a discarded Cow Level doubling the walk.
	const ECataclysmDungeonSubType Fixed = QuestSubTypeFor(Cataclysm);
	if (Kind == ECataclysmDungeonType::Quest
		&& Fixed != ECataclysmDungeonSubType::None)
	{
		Dungeon.SubType = Fixed;
	}

	// A HOLLOW REMEMBERS THE FLOORS IT WAS MADE WITH, since 2026-10-22, because
	// it shrinks from them to half and its hit grows as it does; see
	// `FCataclysmDungeon::HollowFrom`. `Rolled`, the plain roll: what the
	// city's upgrades moved is not natural depth. Every other dungeon keeps
	// nought. NO DRAW: the sub-type above was fixed without one.
	if (Dungeon.IsHollow())
	{
		Dungeon.HollowFrom = Rolled;
	}

	// AND HOW LONG WALKING IT COSTS, WHICH IS NOT THE SAME QUESTION AS HOW DEEP
	// IT IS. One floor costs one day to begin with, so this starts at the floor
	// count; "Dungeons here take 4 less days to beat" lowers it WITHOUT lowering
	// the floor count, which is what lets an invested player make a fifty floor
	// dungeon cost two days rather than fifty. The dungeon is still fifty floors
	// deep, still worth what that is worth, and still bites on the schedule
	// below.
	//
	// A MINIMUM OF ONE DAY, which the sheet states. A dungeon that cost no time
	// at all would be free, and the empire layer's whole tension is that nothing
	// is.
	const float Quicker = City.UpgradeValueFor(
		ECataclysmCityUpgradeEffect::DungeonWalkDaysFewer);

	// EXCEPT ON A COW LEVEL, WHERE THE UPGRADE IS NOT READ AT ALL. "Time to
	// complete is doubled and cannot be reduced" is two rules, and doubling the
	// already-reduced figure would honour only the first: a city that bought the
	// walk-days upgrade would still get its four days off, halved. So the
	// doubling is applied to what the depth alone costs.
	//
	// IT IS STILL THE SLOWEST DUNGEON THERE IS, which is the trade the design
	// document sets against "ridiculous amounts of loot".
	Dungeon.WalkDays =
		(Dungeon.SubType == ECataclysmDungeonSubType::CowLevel)
			? FMath::Max(1.0f, Dungeon.Floors * UCataclysmDayClock::DaysPerFloor)
				  * CowLevelWalkMultiplier
			: FMath::Max(
				  1.0f,
				  Dungeon.Floors * UCataclysmDayClock::DaysPerFloor - Quicker);

	// THE TIMER COMES FROM THE DEPTH AND THEN VARIES. `ResolveDaysFor` is the
	// day clock's; the jitter is what stops two dungeons of one depth coming due
	// on the same day. See `ResolveJitter`.
	//
	// SO THE FLOOR UPGRADES MOVE THE TIMER TOO, and that is the point rather
	// than a side effect. They do not move the size of the hit: `BiteScale`
	// reads the floors that were rolled.
	//
	// Depth and reward are the same axis, which `CLAUDE.md`
	// states: a deeper dungeon is worth more and slower to bite, and a shallower
	// one is poorer and bites sooner. Adjusting the timer separately would break
	// the trade the whole strategy layer rests on.
	//
	// THE WALK COST ABOVE IS NOT AN INPUT HERE, AND MUST NOT BECOME ONE. Depth
	// and time come apart as soon as a player invests, so a dungeon made quicker
	// to walk keeps the timer its depth earned. A Cow Level takes twice as long
	// to walk and bites on exactly the same day it would have.
	const float Base = UCataclysmDayClock::ResolveDaysFor(Dungeon.Floors);
	const float Jitter = 1.0f + Stream.FRandRange(-ResolveJitter, ResolveJitter);

	// A QUEST DUNGEON'S TIMER FOLLOWS ITS DEPTH TOO, as an ordinary dungeon's
	// does. Until 2026-10-19 it was a flat 25 days whatever the floor count;
	// the owner ruled on 2026-10-16 that it reads the depth like any other
	// dungeon's, and the design document says the same. Since 2026-10-13 that
	// timer hits the host city when it runs out, so it is a hit schedule, and a
	// deeper Quest dungeon is now paid for with a slower one, the trade the
	// paragraph above describes. `docs/DECISIONS.md`, 2026-10-19, has the
	// change.
	//
	// THE DRAWS HAPPEN WHATEVER THE KIND, AND THAT IS DELIBERATE. `Base` and
	// `Jitter` are drawn above for every dungeon, so every kind takes exactly
	// as many numbers off the stream as a Basic one. A kind that skipped the
	// jitter draw would make every later roll in the run depend on which kinds
	// came out earlier, and nothing about a Quest dungeon should change what
	// the dungeon after it looks like. Making the Quest timer read the depth
	// added no draw and removed none, so a run's random stream is unchanged.
	Dungeon.ResolveDays = Base * Jitter;

	return Dungeon;
}

TArray<FCataclysmDungeon> UCataclysmSurgeScheduler::RollWave(
	const UCataclysmEmpireMap& Map, int32 Day, int32 FirstDungeonId,
	FRandomStream& Stream, const TArray<int32>& DungeonsPerCity,
	const TArray<int32>& SiegesPerCityNow,
	const TFunction<ECataclysmType()>& DrawCataclysm) const
{
	TArray<FCataclysmDungeon> Wave;

	// THE SENDERS COME BACK WITH THE TARGETS, one per city landed on, because
	// `PickTargets` draws each before rolling its slot; see there.
	TArray<ECataclysmType> Senders;

	const TArray<int32> Targets =
		PickTargets(Map, DungeonsInNextSurge(), Stream, DungeonsPerCity,
					DrawCataclysm, &Senders);

	Wave.Reserve(Targets.Num());

	// COPIED SO THIS WAVE'S OWN SIEGES COUNT TOWARDS THE CAP. Two dungeons in
	// one wave can land on the same city -- `PickTargets` rolls with replacement
	// -- so counting only what was already standing would let a single wave put
	// two Sieges on one city, which is the thing the cap forbids.
	TArray<int32> Sieges = SiegesPerCityNow;

	for (int32 Index = 0; Index < Targets.Num(); ++Index)
	{
		const FCataclysmCity* City = Map.Find(Targets[Index]);
		if (City == nullptr)
		{
			continue;
		}

		// **ASKED BEFORE THE DUNGEON IS MADE, NOT CORRECTED AFTERWARDS.** The
		// roll needs to know whether a Siege is allowed here, because when it is
		// not it re-reads its own draw into the other six sub-types rather than
		// taking a second one. See `UCataclysmSurgeScheduler::RollSubType`.
		//
		// AN EMPTY LIST MEANS THE CALLER DID NOT SAY, and then nothing is
		// refused on account of what was already standing. Sieges this wave
		// makes are still counted, because `Sieges` is grown to fit below.
		const bool bSiegeAllowed =
			!Sieges.IsValidIndex(City->CityId)
			|| Sieges[City->CityId] < SiegesPerCity;

		// WHICH KIND, BEFORE ANYTHING ABOUT THE DUNGEON IS DRAWN. A Quest
		// dungeon's floors come from a different row of `SpecFor`, so the kind
		// has to be settled before the depth is rolled rather than corrected
		// after. `Simulation.trigger_surge` decides it in the same place and for
		// the same reason, one line before it calls `_make_dungeon`.
		//
		// THE CITY DOES NOT AFFECT IT. Every exposed city is as likely to
		// receive a Quest dungeon as any other; the design gives no rule saying
		// otherwise and the model reads only the chance. What the city DOES
		// affect is how deep it is, through the tier's Quest row.
		const ECataclysmDungeonType Kind = RollKind(Stream);

		// AND WHICH CATACLYSM SENDS IT, BEFORE IT IS MADE, because a Quest
		// dungeon's sub-type depends on it. Since 2026-10-21; until then the run
		// stamped the wave after this function returned. SINCE 2026-10-22 IT
		// WAS DRAWN BY `PickTargets`, before this slot's target was rolled,
		// because the Void aims by its stacks: still one draw per city landed
		// on and in the same order, so the run's Cataclysm stream takes the
		// same draws it always took. With no callback the dungeon carries
		// `None` and nothing is fixed.
		//
		// A TARGET NOT ON THE MAP, skipped above, still had its sender drawn.
		// No built map has one, since every target comes from the map's own
		// exposed cities.
		const ECataclysmType Sender =
			Senders.IsValidIndex(Index) ? Senders[Index] : ECataclysmType::None;

		FCataclysmDungeon Dungeon = MakeDungeon(FirstDungeonId + Index, *City,
												Day, Stream, bSiegeAllowed,
												Kind, Sender);
		Dungeon.Cataclysm = Sender;

		if (Dungeon.SubType == ECataclysmDungeonSubType::Siege)
		{
			if (!Sieges.IsValidIndex(Dungeon.CityId))
			{
				Sieges.SetNumZeroed(
					FMath::Max(Sieges.Num(), Dungeon.CityId + 1));
			}

			++Sieges[Dungeon.CityId];
		}

		Wave.Add(Dungeon);
	}

	return Wave;
}
