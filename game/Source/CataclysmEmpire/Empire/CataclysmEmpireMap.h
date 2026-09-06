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
 * population; the three flags; and the upgrades bought. `CityId` is marked as
 * well, so a restore can check the record lines up with the map it is applied to
 * rather than trusting the order.
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
	 * NOTHING SETS THIS YET, and it is carried because `Retake` has to refuse an
	 * erased city and a reader will look for the field the simulation has. The
	 * Void is one of the seven Cataclysms that do not exist; that is issue #53.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	bool bDoomed = false;

	/** Fallen and gone for good. Only an erased city is ever both. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	bool bErased = false;

	/**
	 * What this city has bought, in the order it bought them.
	 *
	 * IT USED TO BE A BARE COUNT that nothing read, exactly as
	 * `world.City.upgrades` in the simulation still is. A count cannot say which
	 * upgrades a city has, so it could not refuse a duplicate and no effect
	 * could be applied from it.
	 *
	 * HOW MANY MAY BE HERE is `UCataclysmEmpireMap::UpgradeSlots`, three
	 * normally and two on Heretic. `UCataclysmEmpireRun::BuyCityUpgrade` is the
	 * only thing that should add to this, because several upgrades act on the
	 * dungeons standing on a city and the map does not own those.
	 *
	 * A ONE-TIME UPGRADE STAYS IN THE LIST after it has fired. It has spent its
	 * slot, and removing it would hand the slot back and let it be bought again.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<FCataclysmCityUpgrade> Upgrades;

	/** Whether this city has already bought that upgrade. */
	bool HasUpgrade(FName RowName) const;

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

	/** Defence as a fraction of its maximum, 0 to 1. Zero if it has none. */
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

	// ----------------------------------------------------------------------
	// City upgrades
	// ----------------------------------------------------------------------

	/**
	 * Records an upgrade on a city and applies what it does to the city itself.
	 *
	 * NOT THE PLACE TO BUY ONE. It makes none of the checks a purchase has to
	 * make -- it does not look at the slot count, at whether the city has this
	 * already, or at whether the effect is built.
	 * `UCataclysmEmpireRun::BuyCityUpgrade` makes all of those and then calls
	 * this. Buying lives there because several upgrades act on the dungeons
	 * standing on a city, and this class does not own dungeons.
	 *
	 * WHICH EFFECTS ARE APPLIED HERE. The two that raise a maximum and the two
	 * that restore, because all four change a city's numbers the moment they are
	 * bought. The two resistances are applied in `Bite` instead, and everything
	 * that involves a dungeon or a day is the run's.
	 *
	 * A RAISED MAXIMUM RAISES CURRENT DEFENCE BY THE SAME AMOUNT, so a city at
	 * full defence stays at full defence and a damaged one stays short by the
	 * same absolute figure. Without that, buying "increase max defence by 20%"
	 * would make an untouched city read as 83% damaged.
	 *
	 * @return false only when there is no such city.
	 */
	bool AddUpgrade(int32 CityId, const FCataclysmCityUpgrade& Upgrade);

	/** How many upgrade slots that city has left, or zero if it does not
	 *  exist. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	int32 FreeUpgradeSlots(int32 CityId) const;

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
	 * with half its defence and half its people.
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
