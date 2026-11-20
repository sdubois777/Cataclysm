// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Empire/CataclysmCityUpgrade.h"
#include "UObject/Object.h"
#include "CataclysmEmpireMap.generated.h"

/**
 * What tier a city is.
 *
 * THE NUMBER IS `Radius - Ring`, WHICH IS WHY IT COUNTS UP FROM THE WEAKEST.
 * The Pillar sits at ring 0 and is tier 3; an Outpost sits at ring 3 and is
 * tier 0. That identity is what `UCataclysmEmpireMap::TierForRing` is, and it
 * means the enum orders the tiers weakest to strongest the way
 * `config.TIER_ORDER` in the simulation does.
 *
 * "CITY" IS THE GENERAL WORD FOR A PLACE ON THE MAP AT ANY TIER, and it is not
 * a tier name. A Bulwark is a city and so is an Outpost. The design document
 * says so in section IX, because the older documents used "City" for what is
 * now a Bulwark.
 *
 * "THE CAPITAL" IS NOT ANOTHER NAME FOR THE PILLAR. The Pillar is the city at
 * ring 0, with defence and population like any other. The capital is the
 * settlement the player walks around between runs. Losing the Pillar ends the
 * run and takes the capital with it.
 */
UENUM(BlueprintType)
enum class ECataclysmCityTier : uint8
{
	Outpost		= 0	UMETA(DisplayName = "Outpost"),
	Bulwark		= 1	UMETA(DisplayName = "Bulwark"),
	Sanctuary	= 2	UMETA(DisplayName = "Sanctuary"),
	Pillar		= 3	UMETA(DisplayName = "Pillar"),
};

/**
 * What a bought empire tree reduces a dungeon's hit on one city to: the share of its defence damage and of its
 * population damage the city still takes. 1 is the whole hit. Issue #2741 slice 3a.
 */
USTRUCT(BlueprintType)
struct CATACLYSMEMPIRE_API FCataclysmTreeDamageFactors
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float Defence = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float Population = 1.0f;
};

/**
 * What the empire upgrade tree's city nodes add up to, as the empire layer reads it. Issue #2741 slice 3a.
 *
 * PLAIN NUMBERS, BECAUSE THIS MODULE MUST NOT DEPEND ON THE `Cataclysm` MODULE (`CataclysmEmpire.Build.cs`). The tree,
 * its allocation and its effect table live over there; `UCataclysmEmpireTree::CityBonusesFor` adds the bought nodes up
 * into one of these, and `UCataclysmEmpireRun::SetTreeBonuses` hands it to the run. The default is no tree at all, so a
 * run nobody hands one to -- every test that builds a run by hand -- is the run the game had before.
 *
 * NOT SAVED, AND THAT IS THE RULE THIS SLICE IS BUILT ON. A loaded run is rebuilt through `Begin` and then has its saved
 * cities written over it (`FCataclysmSaveApply::RunInto`), so a bonus folded into a saved maximum would be counted
 * twice, or lost. Every number here is applied where the game READS the number it changes, and the saved numbers stay
 * the tree's base: the coordinating session's ruling Q10 of 2026-11-18, under the owner's delegation. A bought node acts
 * at once, and a respec takes it away at once.
 *
 * HOW THE NODES COMBINE, each a judgement of 2026-11-18 (S5) recorded in `docs/DECISIONS.md`: the maximum-defence and
 * base-population increases are summed, as the simulation's `city_health_mult` sums six of them; each damage reduction
 * is a factor, compounded per point as its text says "multiplicative" and as the simulation's `city_damage_mult`
 * compounds them; Manifest Destiny compounds per point on total population, and Golden Age is its own factor on top.
 */
USTRUCT(BlueprintType)
struct CATACLYSMEMPIRE_API FCataclysmEmpireTreeBonuses
{
	GENERATED_BODY()

	// ---------------------------------------------------------------------
	// Maximum defence: increases, summed. 0.05 is +5%.
	// ---------------------------------------------------------------------

	/** Every city: Foundation, Reinforced Walls, Imperial Decree. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float MaxDefenceAll = 0.0f;

	/** Outposts: Fortified Gates, The Aegis of Hope. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float MaxDefenceOutposts = 0.0f;

	/** Bulwarks: Masonry Techniques. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float MaxDefenceBulwarks = 0.0f;

	/** Sanctuaries: Masonry Techniques, Monument Building. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float MaxDefenceSanctuaries = 0.0f;

	/** Cities within `NearPillarRings` of the Pillar, the Pillar too: Beacon of Hope. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float MaxDefenceNearPillar = 0.0f;

	/**
	 * Every city, for each standing city holding a defensive upgrade: The Royal Engineer's Blessing. A defensive upgrade
	 * is one that raises maximum defence or resists defence loss (S5).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float MaxDefencePerDefendedCity = 0.0f;

	// ---------------------------------------------------------------------
	// Maximum population: base increases summed, then one factor on the total.
	// ---------------------------------------------------------------------

	/** Every city: Metropolitan Growth, and Imperial Decree's -10%. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float BasePopulationAll = 0.0f;

	/** Outposts: Urban Planning. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float BasePopulationOutposts = 0.0f;

	/** Bulwarks: Imperial Census. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float BasePopulationBulwarks = 0.0f;

	/** The factor on total empire population: Manifest Destiny compounded, times one plus Golden Age (capped). */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float TotalPopulationFactor = 1.0f;

	// ---------------------------------------------------------------------
	// Damage taken: factors, 1 being the whole hit.
	// ---------------------------------------------------------------------

	/** Defence and population damage, every city: Structural Integrity, Urban Fortification, Siege Resistance, Sovereign's Might. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float DamageTaken = 1.0f;

	/** Population damage only, every city: Civil Defense. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float PopulationDamageTaken = 1.0f;

	/** Defence and population damage, cities adjacent to the Pillar (ring 1): Supply Lines. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float DamageTakenAdjacentToPillar = 1.0f;

	/** Population damage only, cities within `NearPillarRings` of the Pillar: Fortified Pillar. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float PopulationDamageTakenNearPillar = 1.0f;

	/** The factor for ONE active Cataclysm type, raised to the number active: Unyielding Defense. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float DamageTakenPerActiveType = 1.0f;

	/** Iron Will's reduction per point at a full empire, and its points; the reduction falls with the living share. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float LivingShareReductionPerPoint = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 LivingSharePoints = 0;

	/** Cities holding `UpgradesForImperialCommand` or more upgrades: Imperial Command. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float DamageTakenWithUpgrades = 1.0f;

	/** Damage from a dungeon of the Cataclysm type with the most dungeons on the city, ties all counting: The Sentinel. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float DamageTakenFromCommonestType = 1.0f;

	// ---------------------------------------------------------------------
	// Days
	// ---------------------------------------------------------------------

	/** Added to every dungeon's resolve timer: Strategic Reserve, Bastion Spirit (capped). */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float ResolveDaysAll = 0.0f;

	/** Added to the timers of dungeons on Outposts: Border Patrol, Emergency Shelters. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float ResolveDaysOutposts = 0.0f;

	/** Added to the timers of dungeons on a city below `MartialLawDefenceFraction` of its defence: Martial Law. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float ResolveDaysBelowTenPercent = 0.0f;

	/** Added to every gap between surges after the first: The Flood Barrier. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float SurgeGapDays = 0.0f;

	// ---------------------------------------------------------------------
	// Slice 3b: the rules that act at an event. Each reading is a judgement of the coordinating session's Q1 to Q7 of
	// 2026-11-19, recorded in `docs/DECISIONS.md`.
	// ---------------------------------------------------------------------

	/**
	 * The share of its maximum defence and population a retaken city comes back with: The Architect's Legacy, 0.75. 0 is
	 * no node, and the map's own share applies (`UCataclysmEmpireMap::Retake`).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float RetakeFraction = 0.0f;

	/**
	 * The share of its CURRENT defence a living Outpost regains on every run day that is a multiple of
	 * `CivilOrderIntervalDays`, to its maximum: Civil Order, 3% per point (Q4, Q7).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float OutpostRegeneration = 0.0f;

	/**
	 * The shares of its maxima the living Outpost or Bulwark with the lowest share of its maximum defence regains on every
	 * run day that is a multiple of `MasonsGuildIntervalDays`: The Mason's Guild, 20% and 15% (Q4, Q5).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float LowestCityDefenceRestored = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float LowestCityPopulationRestored = 0.0f;

	/**
	 * Global Vigilance: the share each dungeon cleared since the last surge takes off every city's damage, per point,
	 * compounded; its points; and the most the whole may take off, 15% (Q1).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float ReductionPerClear = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 ClearPoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float ClearReductionCap = 0.0f;

	/** Reclaimer's Resolve: the same, for each city reclaimed in the run, with its own cap, 15% (Q2). */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float ReductionPerReclaim = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 ReclaimPoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float ReclaimReductionCap = 0.0f;

	/** Imperial Reserve: the same, for each whole 100,000 tier 1 gold held, with its own cap, 25% (its text). Issue #2741
	 *  slice 6c, B7. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float ReductionPerGoldStep = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 GoldStepPoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float GoldReductionCap = 0.0f;

	/** Imperial Reserve's "per 100,000 Gold in Treasury", in tier 1 gold (R10). */
	static constexpr int32 ImperialReserveGoldAStep = 100000;

	/** Treasury Guard: the same, for each whole 50,000 tier 1 gold held, with its own cap, 20% (its text). Issue #2741
	 *  slice 6d, the owner's ruling of 2026-11-20 ("to your cities"); held apart from Imperial Reserve's (D9). */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float ReductionPerHeldGoldStep = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 HeldGoldStepPoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float HeldGoldReductionCap = 0.0f;

	/** Treasury Guard's "per 50,000 Gold currently held", in tier 1 gold (R10). */
	static constexpr int32 TreasuryGuardGoldAStep = 50000;

	/**
	 * The defence a Sanctuary is left at by the run's first ordinary resolve or Quest timer hit that would fell one: Last
	 * Stand Protocols, 1. 0 is no node (Q3).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float SanctuarySurvivesAt = 0.0f;

	/**
	 * How much stronger a city upgrade held `EnduringFoundationsDays` or more makes its effect: Enduring Foundations, 2%
	 * per point, so 0.2 at ten points multiplies the upgrade's value by 1.2 (Q6).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float UpgradeStrengthAfterDays = 0.0f;

	// ---------------------------------------------------------------------
	// Slice 4: a dungeon's walk and depth, and what a clear brings. Each reading is a judgement of the coordinating
	// session's W1 to W9 of 2026-11-19, recorded in `docs/DECISIONS.md`. THE WALK IS READ WHERE IT IS CHARGED
	// (`UCataclysmEmpireRun::EffectiveWalkDaysFor`): the stored walk days, less the flat days, times `WalkTimeFactor`,
	// halved by Tactical Entry, and never below one day (W1). THE FLOORS ARE ADDED WHEN A DUNGEON IS MADE (W3).
	// ---------------------------------------------------------------------

	/** The share of a walk left, compounded per point: Pacing, Overclock, Temporal Mastery (2.5% each), Fleet Footed (12%). */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float WalkTimeFactor = 1.0f;

	/** Tactical Entry: a dungeon of more than this many floors walks in half the days. 0 is no node. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 HalveWalkAboveFloors = 0;

	/** Days off every walk: The Delver, 5. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float WalkDaysFewer = 0.0f;

	/** Days off the walk of a dungeon standing alone on its city: Opportunist, 1 a point (W9). */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float WalkDaysFewerWhenAlone = 0.0f;

	/** Days off every walk for each active Cataclysm type, and the most it may take: Sovereign's Haste, 1 a point, 30 (W8). */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float WalkDaysFewerPerActiveType = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float WalkDaysFewerPerActiveTypeCap = 0.0f;

	/** Days off a Fallen City dungeon's walk: Imperial Roads, 1 a point. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float WalkDaysFewerOnFallenCities = 0.0f;

	/** Days off a walk for each floor cleared before the last: Rapid Descent, 0.1 a point (W2). */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float WalkDaysFewerPerFloorCleared = 0.0f;

	/**
	 * The Last Stand option: a dungeon on a city the hits already scheduled over this many days would fell walks in one
	 * day, whatever else applies (W5). 0 is no option chosen.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 OneDayWalkWithinDaysOfFalling = 0;

	/**
	 * Floors added to every dungeon made, the boss's count too: Deep Boring, Architect of Greed, Exclusionary Mapping
	 * (less), The Collector's Decree, and Architectural Insight to its cap of +10 (W3, W4). Never below one floor.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 FloorsAdded = 0;

	/** Floors added for each active Cataclysm type to every dungeon made but the boss: Infinite Depths, 2 a point. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 FloorsAddedPerActiveType = 0;

	/** The Warlord: the chance a clear removes another dungeon on the same city (W6). */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float RemoveAnotherDungeonChance = 0.0f;

	/** Veteran's Edge: the increase on the empire upgrade points a clear pays, 3% a point, summed (W7). */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float EmpirePointsIncrease = 0.0f;

	// ---------------------------------------------------------------------
	// The words in the node texts that are rules rather than numbers
	// ---------------------------------------------------------------------

	/** "within 2 rings of the Pillar": rings 0 to 2, the Pillar included (S5). */
	static constexpr int32 NearPillarRings = 2;

	/** "adjacent to the Pillar": ring 1 (S5). */
	static constexpr int32 AdjacentToPillarRing = 1;

	/** Imperial Command's "Cities with 2+ upgrades". */
	static constexpr int32 UpgradesForImperialCommand = 2;

	/** Martial Law's "At <10% City Health". */
	static constexpr float MartialLawDefenceFraction = 0.1f;

	/** Civil Order's "every 30 days": run days that are multiples of it, from day 0 (Q4). */
	static constexpr int32 CivilOrderIntervalDays = 30;

	/** The Mason's Guild's "Every 20 days", the same way (Q4). */
	static constexpr int32 MasonsGuildIntervalDays = 20;

	/** Enduring Foundations' "active for 10+ days": the run's day less the day the upgrade was bought (Q6). */
	static constexpr int32 EnduringFoundationsDays = 10;

	// ---------------------------------------------------------------------
	// What a city gets
	// ---------------------------------------------------------------------

	/** The factor on a city's maximum defence: one plus every increase that reaches it. */
	float MaxDefenceFactorFor(ECataclysmCityTier Tier, int32 Ring, int32 CitiesWithADefensiveUpgrade) const;

	/** The factor on a city's maximum population: one plus every base increase that reaches it, times the total factor. */
	float MaxPopulationFactorFor(ECataclysmCityTier Tier) const;

	/**
	 * What a dungeon's hit on a city is reduced to.
	 *
	 * @param Ring the city's ring; 0 is the Pillar
	 * @param ActiveCataclysmTypes how many Cataclysms the run faces
	 * @param LivingShare the empire's living population over its maximum, 0 to 1
	 * @param UpgradesHeld how many upgrades the city holds
	 * @param bFromTheCommonestType whether the dungeon is of a Cataclysm type with the most dungeons on the city
	 */
	FCataclysmTreeDamageFactors DamageFactorsFor(int32 Ring, int32 ActiveCataclysmTypes, float LivingShare,
												 int32 UpgradesHeld, bool bFromTheCommonestType) const;

	/** The days added to the timer of a dungeon standing on a city of this tier, at this share of its defence. */
	float ResolveDaysFor(ECataclysmCityTier Tier, float DefenceFraction) const;

	/**
	 * The factor Global Vigilance and Reclaimer's Resolve put on every city's damage, defence and population alike: each
	 * compounded per point for each clear since the last surge, or each city reclaimed in the run, and held at its cap.
	 */
	float EventDamageFactorFor(int32 ClearsSinceLastSurge, int32 CitiesReclaimed) const;

	/**
	 * The factor Imperial Reserve and Treasury Guard put on every city's damage, defence and population alike, from the
	 * gold held, in tier 1 gold (R10): each compounded per point for each whole step of its own (100,000 and 50,000) and
	 * held at its own cap, as Reclaimer's Resolve is, and the two then multiplied. Issue #2741 slices 6c (B7) and 6d
	 * (D9). Exactly 1 with no point or under one step of either.
	 */
	float GoldDamageFactorFor(double GoldHeldAtTierOne) const;

	/** Whether any walk node is held, so a walk with none is charged exactly the stored figure. */
	bool ReachesTheWalk() const;

	/** The floors added to a dungeon made now: `FloorsAdded`, and Infinite Depths for the active types unless the boss. */
	int32 FloorsAddedTo(bool bTheBoss, int32 ActiveCataclysmTypes) const;
};

/**
 * One city on the empire map.
 *
 * A PORT OF `world.City` in `sim/cataclysm_sim/world.py`.
 *
 * WHAT IT IS NOT: a place with buildings, a level, or anything the player walks
 * around. It is the strategy layer's record of a settlement -- where it sits,
 * what shields it, how much punishment it has left, and whether it still stands.
 *
 * ONLY THE MUTABLE HALF IS SAVED, AND THE SPLIT IS DELIBERATE. A save writes
 * only fields marked `SaveGame`, and the ones marked here are the ones a run can
 * change: the two maxima, which an upgrade raises; the current defence and
 * population; the three flags; the Void stacks; and the upgrades bought.
 * `CityId` is marked as well, so a restore can check the record lines up with
 * the map it is applied to rather than trusting the order.
 *
 * THE NAME, TIER, POSITION AND THE THREE ADJACENCY LISTS ARE NOT SAVED, AND
 * MUST NOT BE. `UCataclysmEmpireMap::Build` recomputes every one of them
 * identically from the lattice, so a save would be storing 96 integers and four
 * other fields that can never legitimately differ from what a rebuild produces
 * -- and a file that did differ would be describing an empire this build cannot
 * make. Restoring is `Build` and then overlay. Issue #1307.
 */
USTRUCT(BlueprintType)
struct CATACLYSMEMPIRE_API FCataclysmCity
{
	GENERATED_BODY()

	/**
	 * Which city this is. Also its index in `UCataclysmEmpireMap::Cities`.
	 *
	 * ASSIGNED IN LATTICE SCAN ORDER, row by row and then column by column, so
	 * the identifiers match the ones `world.build_empire` hands out for the same
	 * coordinates. That is deliberate: it makes a figure worked out in the
	 * simulation directly comparable with one worked out here.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 CityId = INDEX_NONE;

	/**
	 * What to call it.
	 *
	 * A PLACEHOLDER, AND THE SIMULATION'S PLACEHOLDER: the tier followed by the
	 * coordinate, as in "Bulwark (-2,0)". No city in this game has a designed
	 * name yet.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	ECataclysmCityTier Tier = ECataclysmCityTier::Outpost;

	/** Lattice row. Runs from -3 to 3. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 R = 0;

	/** Lattice column. Runs from -3 to 3. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 C = 0;

	/**
	 * The neighbours one ring further out, which are the cities shielding this
	 * one. Empty on the rim, which is why the rim is always exposed.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<int32> Outward;

	/** The neighbours one ring further in, which this city shields. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<int32> Inward;

	/**
	 * The neighbours in the same ring, along the rim.
	 *
	 * RIM OUTPOSTS ONLY, AND THEY CARRY NO LANE. They are the curved edges in
	 * the design sketch and they exist for adjacency effects -- a passive that
	 * reads "and its neighbours" -- rather than for exposure. `IsExposed` reads
	 * `Outward` and never this, which is why.
	 *
	 * WHAT DOES READ THEM. `UCataclysmSurgeScheduler::AdjacentCities`, since
	 * issue #1324 slice 4: a Quest dungeon whose timer runs out moves to an
	 * adjacent city, and two rim Outposts joined by one of these are adjacent.
	 * That is a reading of a design that never defines adjacency, and the class
	 * comment below states adjacency the other way -- see `docs/DECISIONS.md`,
	 * 2026-09-06, which records the disagreement and what it is worth.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<int32> Perimeter;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	float MaxDefence = 0.0f;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	float MaxPopulation = 0.0f;

	/** What defence is left. A city falls when this reaches zero. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	float Defence = 0.0f;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	float Population = 0.0f;

	/**
	 * Whether it has fallen.
	 *
	 * A FALLEN CITY IS NOT A DESTROYED ONE. It becomes a Dungeon City, which can
	 * be retaken -- and the lane it was sealing closes again when it is. What
	 * builds the Dungeon City is not here; that is a separate piece of the
	 * strategy layer.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	bool bFallen = false;

	/**
	 * Marked by the Void: if this city falls it is erased rather than merely
	 * lost, and the lane it was sealing can never be closed again.
	 *
	 * SET BY THE TENTH VOID STACK, since 2026-10-22, and by nothing else.
	 * `UCataclysmEmpireMap::AddVoidStacks` marks the city and then calls
	 * `Fall`, which reads this mark and erases the city -- the erase path `Fall`
	 * already had, kept rather than given a second one, as the coordinating
	 * session ruled that day. Until then nothing set it. The simulation has no
	 * such mark any more: its `_fall` reads `City.void_stacks` itself.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	bool bDoomed = false;

	/** Fallen and gone for good. Only an erased city is ever both. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	bool bErased = false;

	/**
	 * WHETHER A DUNGEON HAS DAMAGED THIS CITY IN THIS RUN, which Price Controls' "Cities that have taken Surge damage"
	 * reads. Issue #2741 slice 6b, the coordinating session's B1 of 2026-11-20. Set by `UCataclysmEmpireMap::Damage`
	 * whenever the city loses defence or population, and every caller of it is a dungeon's damage (C4: an ordinary
	 * resolve, a Quest dungeon's timer and a Siege's daily share); never cleared within the run, and a new run builds
	 * a new map. RUN RECORD VERSION 12: `Migrate_11_to_12` writes false on every city of an older run, because the
	 * record cannot prove past damage -- a city below its maximum may be one whose maximum the tree raised (S1), which
	 * is why the reading "below its maximum now" was withdrawn.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	bool bTookSurgeDamage = false;

	/**
	 * How many Void stacks this city carries. Ten erase it.
	 *
	 * A PORT OF `world.City.void_stacks`, since 2026-10-22. An ordinary dungeon
	 * the Void sent adds 1, 2 or 3 when its timer runs out on this city, INSTEAD
	 * of taking defence or people -- see `UCataclysmEmpireRun::ResolveDungeon`
	 * and `FCataclysmDungeon::VoidStacksItAdds` -- and at
	 * `UCataclysmEmpireMap::VoidStacksToErase` the city falls and is erased. The
	 * owner, 2026-10-16, verbatim: "All void dungeons when they resolve add 1-3
	 * void stacks to a city instead of doing damage to the city or population
	 * and at 10 stacks the city is erased."
	 *
	 * A CITY THAT FALLS WITH FEWER IS NOT ERASED, AND RETAKING IT CLEARS THEM.
	 * See `UCataclysmEmpireMap::Retake`. A fallen city keeps the count it fell
	 * with until then, so the city screen can say what retaking clears.
	 *
	 * THE VOID'S AIM READS THEM. A wave slot the Void sends weighs a city by its
	 * stacks as well as its tier, up to twice at ten; see
	 * `UCataclysmSurgeScheduler::TargetWeightForSender`.
	 *
	 * SAVED WITH THE REST OF THE CITY, and put back by `FCataclysmSaveApply::RunInto`.
	 * Until issue #2741 slice 3a this said nothing loaded it back, which stopped
	 * being true when the run's cities were first loaded.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 VoidStacks = 0;

	/**
	 * The Divine Edict an Ascendant left on this city, as a row key of the
	 * modifier table, or none.
	 *
	 * SINCE 2026-10-22. When an Ascendant's timer runs out on this city it
	 * leaves one of its Edicts here, and every dungeon on the city, standing
	 * or arriving, carries it beyond its own modifiers until
	 * `LeftEdictUntilDay`. The design, Heaven's Wrath: "A city carries at most
	 * one left Edict; a new hit replaces it and restarts the 30 days." See
	 * `UCataclysmEmpireMap::LeaveEdict` and
	 * `UCataclysmEmpireRun::LeftEdictCarriedBy`.
	 *
	 * KEPT WHEN THE CITY FALLS, as the Void stacks are, until its days run
	 * out; nothing clears it.
	 *
	 * SAVED WITH THE REST OF THE CITY, and put back by
	 * `FCataclysmSaveApply::RunInto`. Until issue #2741 slice 3a this said
	 * nothing loaded it back.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	FName LeftEdict = NAME_None;

	/**
	 * The first day the left Edict is no longer carried: the day of the hit
	 * plus `UCataclysmEmpireMap::LeftEdictDays`. Nought when none was left.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 LeftEdictUntilDay = 0;

	/**
	 * The Edict this city's dungeons carry on day `Today`, or none: the left
	 * one while `Today` is before `LeftEdictUntilDay`.
	 */
	FName EdictCarriedOn(int32 Today) const
	{
		return Today < LeftEdictUntilDay ? LeftEdict : FName(NAME_None);
	}

	/** How many days the left Edict has still to run on day `Today`; nought when none is carried. */
	int32 EdictDaysLeftOn(int32 Today) const
	{
		return EdictCarriedOn(Today).IsNone() ? 0 : LeftEdictUntilDay - Today;
	}

	/**
	 * What this city has bought, in the order it bought them.
	 *
	 * IT USED TO BE A BARE COUNT that nothing read, exactly as
	 * `world.City.upgrades` in the simulation still is. A count cannot say which
	 * upgrades a city has, so it could not refuse a duplicate and no effect
	 * could be applied from it.
	 *
	 * HOW MANY MAY BE HERE is `UCataclysmEmpireMap::UpgradeSlots`, three
	 * normally and two on Heretic, counted with `Building`. Only
	 * `UCataclysmEmpireRun::CompleteCityUpgrade` should add to this -- a purchase
	 * with no build days reaches it at once, and a build the day its days run
	 * out (issue #2825) -- because several upgrades act on the dungeons standing
	 * on a city and the map does not own those.
	 *
	 * A ONE-TIME UPGRADE STAYS IN THE LIST after it has fired. It has spent its
	 * slot, and removing it would hand the slot back and let it be bought again.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<FCataclysmCityUpgrade> Upgrades;

	/**
	 * What this city has bought and is still building. Issue #2825, ruled 2026-11-21.
	 *
	 * AT MOST ONE: a city builds one upgrade at a time (`ECataclysmCityUpgradeResult::BuildUnderWay`). An array so the
	 * record holds nothing at all when nothing is building.
	 *
	 * A LIST OF ITS OWN AND NOT A FLAG ON `Upgrades`, so that every reader of `Upgrades` -- the effects, the empire
	 * tree's counts of upgrades held, the dungeon rewards -- sees only finished upgrades and none of them had to
	 * change. What reads both is the slot count (`UCataclysmEmpireMap::FreeUpgradeSlots`) and the duplicate check
	 * (`IsBuilding`): an upgrade being built has taken its slot and cannot be bought twice.
	 *
	 * IT DOES NOTHING UNTIL IT IS FINISHED. `UCataclysmEmpireRun::AdvanceCityBuilds` takes a day off it for each day
	 * that passes while this city is neither fallen nor besieged, and at nought moves it to `Upgrades` and applies it.
	 * RUN RECORD VERSION 13; `Migrate_12_to_13` writes an empty list on every city.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<FCataclysmCityUpgrade> Building;

	/** Whether this city has already bought that upgrade and finished it. */
	bool HasUpgrade(FName RowName) const;

	/** Whether this city is building that upgrade now. Issue #2825. */
	bool IsBuilding(FName RowName) const;

	/**
	 * The total value of every standing upgrade with that effect.
	 *
	 * A SUM RATHER THAN THE FIRST MATCH, because two upgrades with one effect
	 * should add up. No two rows of the sheet share an effect today, so this
	 * returns either zero or one upgrade's value in practice; summing is what it
	 * should do if that ever changes.
	 */
	float UpgradeValueFor(ECataclysmCityUpgradeEffect Effect) const;

	/**
	 * How far out this city sits, which is also its distance from the Pillar.
	 *
	 * Every LANE is orthogonal and changes the ring by exactly one, so the two
	 * are the same number. "Within 2 rings of the Pillar" is the four
	 * Sanctuaries and the eight Bulwarks, twelve cities.
	 *
	 * THIS SAID "ADJACENCY IS ORTHOGONAL" UNTIL 2026-09-06, which is the
	 * reading the project owner rejected -- verbatim, "Include the perimeter
	 * links". `Perimeter` above joins two rim Outposts at the SAME ring, so it
	 * is adjacency that is not a lane and does not change this number.
	 * `docs/Cataclysm_GDD_v2.md` section IX now states both. Issue #1324.
	 */
	int32 Ring() const { return FMath::Abs(R) + FMath::Abs(C); }

	bool IsAlive() const { return !bFallen; }

	/**
	 * Defence as a fraction of its maximum, 0 to 1. Zero if it has none.
	 *
	 * THE BASE, WITHOUT THE EMPIRE UPGRADE TREE. The map and the city screen read
	 * `UCataclysmEmpireMap::DefenceFractionOf`, which applies it. Issue #2741 slice 3a.
	 */
	float DefenceFraction() const
	{
		return MaxDefence <= 0.0f ? 0.0f : Defence / MaxDefence;
	}
};

/**
 * The empire map: 25 cities in a diamond, and the lanes the Cataclysm comes
 * down.
 *
 * A PORT OF `sim/cataclysm_sim/world.py`, and the second thing in the
 * `CataclysmEmpire` module after `UCataclysmDayClock`. Nothing here touches a
 * world, an actor or a pawn, which is what that module's build file asks for.
 *
 * THE SHAPE. A diamond lattice: the taxicab ball of radius 3. A cell exists at
 * grid coordinate `(R, C)` when `|R| + |C| <= 3`, and `|R| + |C|` is its ring.
 *
 *         ring 0    1 cell    Pillar
 *         ring 1    4 cells   Sanctuary
 *         ring 2    8 cells   Bulwark
 *         ring 3   12 cells   Outpost
 *                 ---------
 *                 25 cells
 *
 *                            O
 *                        O   B   O
 *                    O   B   S   B   O
 *                O   B   S   P   S   B   O
 *                    O   B   S   B   O
 *                        O   B   O
 *                            O
 *
 * RING N HOLDS EXACTLY 4N CELLS, so the design document's counts -- 12 Outposts,
 * 8 Bulwarks, 4 Sanctuaries and the Pillar -- are a property of the geometry
 * rather than a separate decision. Changing the radius would change all four
 * together.
 *
 * LANES. A lane is orthogonal: `(R+-1, C)` and `(R, C+-1)`. Every orthogonal
 * step changes the ring by one, so a cell's lanes run strictly to the cells one
 * step further out and one step further in. That is what makes the frontier
 * rule exact: a cell is attackable once ANY of the cells shielding it from
 * outside has fallen, and retaking that cell seals the lane again.
 *
 * A LANE IS NOT THE WHOLE OF ADJACENCY, AND THIS PARAGRAPH USED TO SAY IT WAS.
 * It read "Adjacency is orthogonal ... a cell's neighbours are strictly the
 * cells one step further out and one step further in", which contradicted
 * `FCataclysmCity::Perimeter` two hundred lines above -- the rim's curved
 * edges, joining rim Outposts to each other, which that field's own comment
 * says exist for adjacency effects. Nothing read them when both were written,
 * so nothing had to choose. `UCataclysmSurgeScheduler::AdjacentCities` chose,
 * in favour of counting them, and `docs/DECISIONS.md` records why.
 *
 * THE CONSTANTS ARE A COPY AND THE SIMULATION IS THE ORIGINAL, the same
 * arrangement `UCataclysmDayClock` is in and for the same reason.
 * `tools/tests/test_empire_map_port.py` reads the per-tier defence and
 * population back out of this header and compares them against
 * `config.TIER_STATS`. The power model has silently drifted from its own source
 * twice, which is what `CLAUDE.md` warns about at length.
 *
 * WHAT IT DOES NOT DO, named so the scope is not argued later:
 *
 *   - **It does not spawn dungeons.** Surges do that, and the surge scheduler
 *     is a separate piece of the strategy layer.
 *   - **It does not build a Dungeon City.** A city that falls here is recorded
 *     as fallen and its lane opens. Turning it into a retakeable dungeon with
 *     the design's 20/40/60 floor minimums needs a dungeon runtime, issue #41.
 *   - **It does not fire the Last Stand.** It answers whether the Pillar is
 *     exposed, which is the condition; what happens next is issue #43.
 *   - **It does not sell a city upgrade.** It holds what each city has bought
 *     and applies the standing ones, but buying goes through
 *     `UCataclysmEmpireRun::BuyCityUpgrade`, because several upgrades act on the
 *     dungeons standing on a city and this does not own those.
 *   - **The simulation still has no city upgrades at all.**
 *     `sim/cataclysm_sim/world.py` carries a bare `upgrades: int` that nothing
 *     sets or reads, so Heretic's two slots instead of three still cannot be
 *     measured in a sweep. Issue #318, and this change does not close it.
 *   - **Nothing writes it to a save.** `UCataclysmRunSave` says the empire graph
 *     is deliberately absent because it "has no runtime shape yet". It has one
 *     now, and joining the two is separate work.
 */
UCLASS(BlueprintType)
class CATACLYSMEMPIRE_API UCataclysmEmpireMap : public UObject
{
	GENERATED_BODY()

public:
	// ----------------------------------------------------------------------
	// The shape
	// ----------------------------------------------------------------------

	/**
	 * How many rings out the map reaches. `world.RADIUS`.
	 *
	 * IT DECIDES FIVE NUMBERS AT ONCE: the city count of every tier, and how
	 * many cities have to fall in a line before the Pillar is reachable. Three
	 * rings is what the design document specifies and what the simulation is
	 * tuned against.
	 */
	static constexpr int32 Radius = 3;

	/** How many cities the whole map holds. 1 + 4 + 8 + 12. */
	static constexpr int32 CityCount = 25;

	/**
	 * How many cities a ring holds: 4N, except ring 0, which holds the Pillar
	 * alone.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static int32 CityCountForRing(int32 Ring);

	/** Which tier sits at a ring. `Radius - Ring`, clamped. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static ECataclysmCityTier TierForRing(int32 Ring);

	/** Which ring a tier sits at. The same identity, read the other way. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static int32 RingForTier(ECataclysmCityTier Tier);

	/** "Outpost", "Bulwark", "Sanctuary" or "Pillar". */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString TierName(ECataclysmCityTier Tier);

	// ----------------------------------------------------------------------
	// What a city is worth
	// ----------------------------------------------------------------------

	/**
	 * Defence and population by tier. `config.TIER_STATS`.
	 *
	 * THESE ARE ONE OF THE SIMULATION'S FIVE NAMED UNKNOWNS -- the design
	 * documents do not specify them -- and they are anchored loosely to the
	 * Bulwark class tree, which gates nodes at 5,000 to 25,000 player maximum
	 * health and so implies a five-digit scale. They are copied here rather than
	 * re-derived, and `tools/tests/test_empire_map_port.py` fails if the two
	 * copies part.
	 */
	static constexpr float OutpostMaxDefence = 1000.0f;
	static constexpr float OutpostMaxPopulation = 5000.0f;
	static constexpr float BulwarkMaxDefence = 3000.0f;
	static constexpr float BulwarkMaxPopulation = 20000.0f;
	static constexpr float SanctuaryMaxDefence = 8000.0f;
	static constexpr float SanctuaryMaxPopulation = 60000.0f;
	static constexpr float PillarMaxDefence = 20000.0f;
	static constexpr float PillarMaxPopulation = 150000.0f;

	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static float MaxDefenceFor(ECataclysmCityTier Tier);

	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static float MaxPopulationFor(ECataclysmCityTier Tier);

	/**
	 * What share of its defence and population a retaken city comes back with.
	 * `Simulation._retake`.
	 *
	 * HALF, AND NOT ALL OF IT, so retaking is a real repair rather than an
	 * undo. A city taken and retaken twice is materially weaker than one never
	 * lost.
	 */
	static constexpr float RetakenFraction = 0.5f;

	/**
	 * How many Void stacks erase a city. `config.void_stacks_to_erase`.
	 *
	 * A COPY, AND THE SIMULATION IS THE ORIGINAL.
	 * `tools/tests/test_void_stacks_in_the_model_and_in_the_game.py` reads it
	 * back out of this header and compares it with the model's.
	 */
	static constexpr int32 VoidStacksToErase = 10;

	/**
	 * How many days an Edict an Ascendant leaves on a city is carried. The
	 * owner's 30, 2026-10-19 ("Full plan design").
	 */
	static constexpr int32 LeftEdictDays = 30;

	// ----------------------------------------------------------------------
	// The map itself
	// ----------------------------------------------------------------------

	/** Every city, indexed by its own identifier. Empty until `Build` runs. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<FCataclysmCity> Cities;

	/** Which city is the Pillar, or `INDEX_NONE` before `Build` runs. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 PillarId = INDEX_NONE;

	/**
	 * How many upgrades each city may hold. Three normally, two on Heretic.
	 *
	 * ONE NUMBER FOR THE WHOLE MAP rather than one per city, because the design
	 * gives every city the same count and the only thing that changes it is the
	 * lethality mode, which belongs to the run. `UCataclysmEmpireRun::Begin` sets
	 * it from the rung it was given; a map built on its own keeps the ordinary
	 * three, which is what a test that never mentions Heretic should see.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 UpgradeSlots = UCataclysmCityUpgradeRules::SlotsNormally;

	/**
	 * What the empire upgrade tree adds to every city, as the run hands it over. Issue #2741 slice 3a.
	 *
	 * NOT SAVED. Each city's `MaxDefence` and `MaxPopulation` stay the base -- the tier's figure and what city upgrades
	 * added -- and the readers below apply this; see `FCataclysmEmpireTreeBonuses` for why. A map built on its own,
	 * which is every map a test builds by hand, holds none, and is the map the game had before.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Cataclysm|Empire")
	FCataclysmEmpireTreeBonuses TreeBonuses;

	/**
	 * Today's number, asked of the run that holds this map: how long a city upgrade has been held, which Enduring
	 * Foundations reads (issue #2741 slice 3b). Set by `UCataclysmEmpireRun::BeginForCharacter`. A map built on its own
	 * holds none and reads day 0.
	 */
	TFunction<int32()> TodayIs;

	/** The city `ArmTheLastStand` armed for the next `Damage` call, `INDEX_NONE` for none; and the defence it keeps. */
	int32 LastStandArmedFor = INDEX_NONE;
	float LastStandDefence = 0.0f;

	/** Set when an armed Last Stand held, until `TakeTheLastStandHeld` reads it. */
	bool bLastStandHeld = false;

	/**
	 * Lays out the 25 cities and links their lanes. Discards whatever was here
	 * before, so a second call is a fresh empire rather than a doubled one.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	void Build();

	/** The city with that identifier, or null. */
	const FCataclysmCity* Find(int32 CityId) const;
	FCataclysmCity* FindMutable(int32 CityId);

	/** Which city sits at a lattice coordinate, or `INDEX_NONE` off the map. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	int32 CityAt(int32 InR, int32 InC) const;

	// ----------------------------------------------------------------------
	// Lanes
	// ----------------------------------------------------------------------

	/**
	 * Whether dungeons can spawn on a city yet.
	 *
	 * THE RIM IS PERMANENTLY EXPOSED. Everything inside it is sealed until a
	 * lane opens -- that is, until one of the cities shielding it falls. Retake
	 * that city and the lane closes again.
	 *
	 * A FALLEN CITY IS NOT EXPOSED. There is nothing left there to attack.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	bool IsExposed(int32 CityId) const;

	/** Every exposed city. The Pillar is left out unless asked for. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	TArray<int32> ExposedCities(bool bIncludePillar = false) const;

	/**
	 * Whether a Sanctuary has fallen, so the Cataclysm can reach the Pillar.
	 *
	 * THIS IS THE LOSS CONDITION. The design document states it as "the run is
	 * lost when a clear path to the capital is opened", and in lattice terms a
	 * clear path is exactly this. What happens next -- the Cataclysm boss
	 * dungeon moving to the Pillar and absorbing every dungeon still standing --
	 * is the Last Stand, issue #43, and is not here.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	bool IsPillarExposed() const;

	/**
	 * For each city, the fewest cities that must fall -- itself included -- for
	 * that city to be lost, given the map as it stands. Indexed by city
	 * identifier.
	 *
	 * A rim Outpost costs 1: it is already exposed and just has to break.
	 * Anything further in costs one more than the cheapest city shielding it.
	 *
	 * @param ExtraFallen a city to treat as fallen although it is not, or
	 *                    `INDEX_NONE`. It is what `LaneCriticality` asks with.
	 */
	TArray<int32> FallCost(int32 ExtraFallen = INDEX_NONE) const;

	/**
	 * How many more cities must fall before the Cataclysm reaches the Pillar.
	 * Starts at 3 and the run is lost at 0.
	 *
	 * THIS, AND NOT THE RAW COUNT OF CITIES LOST, IS THE NUMBER THAT DECIDES THE
	 * RUN. Twenty Outposts scattered around the rim cost less than three in a
	 * line.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	int32 DistanceToDefeat() const;

	/** The same question with one more city treated as fallen. */
	int32 DistanceToDefeatIf(int32 ExtraFallen) const;

	/**
	 * Per city, how much closer to defeat its loss would put you. Indexed by
	 * city identifier.
	 *
	 * A city off every shortest lane scores 0 and is, structurally, free to
	 * lose. A city on one scores 1 or more and is what actually needs saving.
	 * This is the number a triage screen should be built on.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	TArray<int32> LaneCriticality() const;

	/** How many sealed cities currently have a lane open into them. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	int32 OpenLanes() const;

	/** How far in the deepest breach reaches: 0 intact, 3 a Sanctuary, 4 the Pillar. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	int32 BreachDepth() const;

	// ----------------------------------------------------------------------
	// The state of the empire
	// ----------------------------------------------------------------------

	/** How many people are left, across every city still standing. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	float TotalPopulation() const;

	/** How many there would be with every city intact. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	float TotalMaxPopulation() const;

	// ----------------------------------------------------------------------
	// What the empire upgrade tree makes of a city -- issue #2741 slice 3a
	// ----------------------------------------------------------------------

	/**
	 * A city's maximum defence with the empire upgrade tree: its own `MaxDefence` times the tree's factor for its tier
	 * and ring. EVERY READER OF A CITY'S MAXIMUM ASKS THIS RATHER THAN THE FIELD, because the field is the saved base.
	 */
	float EffectiveMaxDefence(const FCataclysmCity& City) const;

	/** A city's maximum population with the empire upgrade tree. */
	float EffectiveMaxPopulation(const FCataclysmCity& City) const;

	/**
	 * A city's defence as the game reads it: what it holds, and never more than its maximum.
	 *
	 * A RAISED MAXIMUM LEAVES WHAT A CITY HOLDS WHERE IT IS, AND A LOWERED ONE IS READ AS THE SMALLER OF THE TWO. The
	 * coordinating session's S1 of 2026-11-18, a judgement for the owner, matching this game's rule for a player's
	 * health in `CataclysmVitalAttributeSet.cpp` (raising the maximum raises nothing; a lowered one brings health down
	 * to it). So a point bought mid-run makes a full Outpost read "1000 of 1050", and a respec writes nothing: the
	 * smaller of the two is read until a hit writes it.
	 */
	float DefenceOf(const FCataclysmCity& City) const;

	/** A city's population as the game reads it, on `DefenceOf`'s rule. */
	float PopulationOf(const FCataclysmCity& City) const;

	/** `DefenceOf` over `EffectiveMaxDefence`, 0 to 1, zero when there is no maximum. */
	float DefenceFractionOf(const FCataclysmCity& City) const;

	/** How many standing cities hold a defensive upgrade, which The Royal Engineer's Blessing counts. */
	int32 CitiesWithADefensiveUpgrade() const;

	/** Whether an upgrade raises maximum defence or resists defence loss: the reading of "defensive upgrade" (S5). */
	static bool IsADefensiveUpgrade(const FCataclysmCityUpgrade& Upgrade);

	// ----------------------------------------------------------------------
	// Enduring Foundations -- issue #2741 slice 3b
	// ----------------------------------------------------------------------

	/** Today, as `TodayIs` answers it; day 0 when nothing does. */
	int32 Today() const;

	/**
	 * Whether Enduring Foundations reaches an upgrade of this effect: the percentage effects read after the buy -- the
	 * two raised maxima, the two resistances, the two interval heals, and the restore on a clear. NOT the whole-number
	 * effects, which have no 2% to take, and not the one-time ones, which are spent at the buy (Q6).
	 */
	static bool IsStrengthenedByEnduringFoundations(ECataclysmCityUpgradeEffect Effect);

	/** Whether a held upgrade has been held `EnduringFoundationsDays` or more today. */
	bool HasBeenHeldLongEnough(const FCataclysmCityUpgrade& Upgrade) const;

	/**
	 * An upgrade's value as the game reads it today: its own, times one plus Enduring Foundations' strength once it has
	 * been held long enough and is an effect the node reaches.
	 */
	float UpgradeValueNow(const FCataclysmCityUpgrade& Upgrade) const;

	/** The sum of `UpgradeValueNow` over a city's upgrades of one effect: `FCataclysmCity::UpgradeValueFor`, today. */
	float UpgradeValueNowFor(const FCataclysmCity& City, ECataclysmCityUpgradeEffect Effect) const;

	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	int32 FallenCityCount() const;

	/** Every city still standing. The Pillar is left out unless asked for. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	TArray<int32> AliveCities(bool bIncludePillar = false) const;

	// ----------------------------------------------------------------------
	// What happens to a city
	// ----------------------------------------------------------------------

	/**
	 * A city loses a NUMBER OF POINTS of defence and a number of people.
	 *
	 * THE ONE PLACE A CITY LOSES ANYTHING. `Bite` below is a share expressed in
	 * terms of this, so the resistances and the fall check exist once.
	 *
	 * POINTS AND NOT A SHARE OF THE MAXIMUM, WHICH IS THE WHOLE OF ISSUE #1331.
	 * While a resolve took a fraction of the host city's own maximum, that
	 * maximum divided out of "how many resolves does this city survive": a
	 * Pillar holding twenty times an Outpost's defence lasted 17 resolves
	 * against 10, and every upgrade in the game that raises a city's health was
	 * worth nothing -- including the two that ship,
	 * `Architect_Increase_max_defense_by_20` and its population twin in
	 * `game/Data/CityUpgrades.csv`. The project owner ruled on 2026-09-05,
	 * verbatim: "damage to cities shouldn't be a % of their hp. Instead,
	 * dungeons should have damage ranges that aren't % based, but should be flat
	 * damage numbers." `Simulation._resolve` was changed first, on issue #1327.
	 *
	 * WHAT THE CALLER IS EXPECTED TO HAVE FOLDED IN: the dungeon type's own
	 * damage, how deep this dungeon is relative to a typical one of its type,
	 * and any empire tree reduction. This takes the finished number because none
	 * of those three exist here.
	 *
	 * WHAT THIS FOLDS IN ITSELF: the city's own two resistance upgrades. They
	 * are applied here rather than by the caller because they belong to the city
	 * and this is the only place a city loses anything, so a second caller
	 * cannot forget them.
	 *
	 * @param DefencePoints    points of defence to take. Negative is read as
	 *                         zero; nothing here heals a city.
	 * @param PopulationPoints people to take.
	 * @return whether the city fell as a result. False if it was already fallen,
	 *         in which case nothing is taken -- there is nothing left to take.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	bool Damage(int32 CityId, float DefencePoints, float PopulationPoints);

	/**
	 * Last Stand Protocols for the NEXT `Damage` call only: if that call would fell this city, the city is left at
	 * `SurviveAt` defence instead and `TakeTheLastStandHeld` answers true once. Issue #2741 slice 3b, Q3.
	 *
	 * ARMED FOR ONE CALL AND SPENT BY IT, whether or not it was needed, so it cannot reach a later hit. The run arms it
	 * before the two hits a dungeon's resolution deals -- an ordinary dungeon resolving and a Quest dungeon's timer --
	 * and never before a Siege's daily share; and the tenth Void stack fells a city without calling `Damage`. ARMED
	 * HERE AND NOT PASSED IN, so the two calls stay the single lines the port tests read.
	 */
	void ArmTheLastStand(int32 CityId, float SurviveAt);

	/** Whether the Last Stand held since this was last asked; asking clears it. */
	bool TakeTheLastStandHeld();

	/**
	 * The same, expressed as a SHARE OF THE CITY'S MAXIMUM.
	 *
	 * KEPT DELIBERATELY, AND IT IS NOT THE ORDINARY PATH. Issue #1331 moved
	 * every dungeon resolve onto `Damage` above, and this is what remains of the
	 * shape it replaced. It is here because one thing in the design still works
	 * this way and needs a name for it:
	 *
	 * THE `Siege` SUB-TYPE KEEPS PERCENTAGE DAMAGE, AS A DELIBERATE EXCEPTION.
	 * The project owner ruled on 2026-09-05, verbatim: "Keep it as a deliberate
	 * exception (Recommended)" -- a siege does not care how thick your walls
	 * are, which makes it the one threat city-health investment does not protect
	 * against. The Siege row of the sub-type table in
	 * `docs/Cataclysm_GDD_v2.md` and `docs/DECISIONS.md` both carry it.
	 * `UCataclysmEmpireRun::ApplySiegeDamage` is where that share is
	 * taken; it multiplies by the city's maximum itself, because a Siege's
	 * damage is a share PLUS a growth in points and only one of the two fits
	 * through here.
	 *
	 * SO A READER WHO FINDS A SHARE HERE HAS NOT FOUND AN OVERSIGHT. Removing
	 * this would remove the only written statement of what "a share of the
	 * maximum" means for a city.
	 *
	 * OF THE MAXIMUM AND NOT OF WHAT IS LEFT, which is what makes a city bitten
	 * this way die on a schedule rather than approaching zero for ever.
	 *
	 * @param DefenceFraction    share of maximum defence to take. Negative is
	 *                           read as zero.
	 * @param PopulationFraction share of maximum population to take.
	 * @return whether the city fell as a result.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	bool Bite(int32 CityId, float DefenceFraction, float PopulationFraction);

	/**
	 * A city takes Void stacks instead of damage, and at `VoidStacksToErase`
	 * it falls and is erased.
	 *
	 * A PORT OF THE STACK HALF OF `Simulation._resolve` and of the erase in
	 * `Simulation._fall`. The ordinary Void dungeon whose timer ran out is the
	 * only caller; its Siege's daily bite stays damage and goes through `Damage`.
	 *
	 * THE TENTH STACK MARKS THE CITY DOOMED AND THEN CALLS `Fall`, which erases
	 * a doomed city. One erase path, not two: the coordinating session ruled on
	 * 2026-10-22 that this reuse `Fall`'s. Defence and population are left as
	 * they are, and `Fall` then sets defence to nought like any fall.
	 *
	 * @param Stacks how many to add. Negative is read as nought; nothing here
	 *               takes stacks away, and `Retake` is what clears them.
	 * @return whether the city fell as a result. False if it had already fallen
	 *         or does not exist, and then nothing is added -- there is nothing
	 *         left to mark.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	bool AddVoidStacks(int32 CityId, int32 Stacks);

	/**
	 * An Ascendant leaves a Divine Edict on a city for `LeftEdictDays` from
	 * `Today`.
	 *
	 * A CITY CARRIES AT MOST ONE, AND A NEW ONE REPLACES IT AND RESTARTS THE
	 * DAYS, ruled by the coordinating session under the owner's delegation and
	 * written into the design. Nothing else about the city changes.
	 *
	 * @return false, and nothing changed, when there is no such city or no
	 *         Edict was given.
	 */
	bool LeaveEdict(int32 CityId, FName Edict, int32 Today);

	// ----------------------------------------------------------------------
	// City upgrades
	// ----------------------------------------------------------------------

	/**
	 * Records an upgrade on a city and applies what it does to the city itself.
	 *
	 * NOT THE PLACE TO BUY ONE. It makes none of the checks a purchase has to
	 * make -- it does not look at the slot count, at whether the city has this
	 * already, or at whether the effect is built.
	 * `UCataclysmEmpireRun::BuyCityUpgrade` makes all of those, and
	 * `UCataclysmEmpireRun::CompleteCityUpgrade` calls this when the upgrade is
	 * finished: at once for one with no build days, or the day its build's days
	 * run out (issue #2825). Buying lives in the run because several upgrades act
	 * on the dungeons standing on a city, and this class does not own dungeons.
	 *
	 * WHICH EFFECTS ARE APPLIED HERE. The two that raise a maximum and the two
	 * that restore, because all four change a city's numbers the moment they are
	 * finished. The two resistances are applied in `Damage` instead, and everything
	 * that involves a dungeon or a day is the run's.
	 *
	 * A RAISED MAXIMUM RAISES CURRENT DEFENCE BY THE SAME AMOUNT, so a city at
	 * full defence stays at full defence and a damaged one stays short by the
	 * same absolute figure. Without that, buying "increase max defence by 20%"
	 * would make an untouched city read as 83% damaged. THE FIGURE IS WHAT THE
	 * MAXIMUM WITH THE EMPIRE UPGRADE TREE ROSE BY, for the same reason: a full
	 * city under the tree is full at its effective maximum. Issue #2741 slice 3a.
	 *
	 * @return false only when there is no such city.
	 */
	bool AddUpgrade(int32 CityId, const FCataclysmCityUpgrade& Upgrade);

	/** How many upgrade slots that city has left, or zero if it does not
	 *  exist. AN UPGRADE BEING BUILT HAS TAKEN ITS SLOT, since issue #2825. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	int32 FreeUpgradeSlots(int32 CityId) const;

	/**
	 * Puts an upgrade on a city's build list, to be finished when its `BuildDaysLeft` have been counted. Issue #2825.
	 *
	 * NOT THE PLACE TO BUY ONE, as `AddUpgrade` is not: `UCataclysmEmpireRun::BuyCityUpgrade` makes every check and
	 * then calls this. It applies nothing; an upgrade being built does nothing.
	 *
	 * @return false only when there is no such city.
	 */
	bool StartBuild(int32 CityId, const FCataclysmCityUpgrade& Upgrade);

	/**
	 * A city falls: its defence is gone and its lane opens.
	 *
	 * @return whether it fell now. False if it had already fallen or does not
	 *         exist.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	bool Fall(int32 CityId);

	/**
	 * A city is retaken: the lane it was sealing closes again, and it comes back
	 * with half its defence and half its people -- half of each maximum with the
	 * empire upgrade tree, since issue #2741 slice 3a, and three quarters with
	 * The Architect's Legacy, since slice 3b (`TreeBonuses.RetakeFraction`).
	 *
	 * AND WITH NO VOID STACKS, since 2026-10-22: retaking a city clears them, as
	 * `Simulation._retake` does, ruled by the coordinating session on
	 * 2026-10-16. An erased city cannot be retaken, so its stacks stay.
	 *
	 * @return whether it was retaken. False if it was not fallen, does not
	 *         exist, or was erased rather than merely lost.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	bool Retake(int32 CityId);

	// ----------------------------------------------------------------------
	// Seeing it
	// ----------------------------------------------------------------------

	/**
	 * The map as text, one row per lattice row: a tier initial and a mark.
	 * `.` still sealed, `!` exposed, `x` fallen.
	 *
	 *         O!
	 *        O! B. O!
	 *       O! B. S. B. O!
	 *      O! B. S. P. S. B. O!
	 *       O! B. S. B. O!
	 *        O! B. O!
	 *         O!
	 *
	 * WHAT IT IS FOR IS EVIDENCE, not the interface. It is `Empire.render` in
	 * the simulation, character for character, so a state reached here can be
	 * compared with one reached there by eye. A console command can print it and
	 * a test can assert on it.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	FString Render() const;
};
