// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CataclysmCityUpgrade.generated.h"

/**
 * What a city upgrade does, one value per row of `game/Data/CityUpgrades.csv`.
 *
 * WHY THE EFFECT IS AN ENUM HERE RATHER THAN THE DATA ROW ITSELF. This module
 * must not depend on the `Cataclysm` module -- `CataclysmEmpire.Build.cs` says
 * so and the dependency runs one way -- and `FCataclysmCityUpgradeRow` lives
 * over there, beside every other DataTable row. So the empire layer names the
 * effects it can act on, and something above it turns a data row into one of
 * these. `UCataclysmCityUpgradeMapping` in the `Cataclysm` module is that
 * something, and a test there checks the mapping covers all 24 rows.
 *
 * THERE ARE EXACTLY AS MANY VALUES AS THERE ARE ROWS, because no two of the 24
 * upgrades do the same thing. That is a property of the sheet rather than a
 * decision made here, and if two rows ever share an effect they should share a
 * value.
 *
 * FOUR OF THEM DO NOTHING YET AND THAT IS DELIBERATE, not an oversight (ten
 * when this was written; four since issue #2838).
 * `UCataclysmCityUpgradeRules::IsBuilt` is the single place that says which, and
 * `UCataclysmEmpireRun::BuyCityUpgrade` refuses to spend a slot on one that does
 * nothing rather than letting a player waste it. Each is named below with the
 * system it is waiting for.
 */
UENUM(BlueprintType)
enum class ECataclysmCityUpgradeEffect : uint8
{
	/** Not an upgrade. The value a default-constructed upgrade carries. */
	None						= 0		UMETA(DisplayName = "None"),

	// ----------------------------------------------------------------------
	// Architect. All ten are built.
	// ----------------------------------------------------------------------

	/** Percent. Raises the city's maximum defence, and its current defence by
	 *  the same amount so a full city stays full. */
	MaxDefence					= 1,

	/** Percent. The same, for population. */
	MaxPopulation				= 2,

	/** Percent, one-time. Removes that share of the dungeons standing on this
	 *  city, soonest to resolve first. */
	RemoveDungeons				= 3,

	/** Percent, one-time. Restores that share of maximum defence. */
	RestoreDefence				= 4,

	/** Percent, one-time. The same, for population. */
	RestorePopulation			= 5,

	/** Percent. Reduces the defence a resolving dungeon takes. */
	ResistDefenceLoss			= 6,

	/** Percent. The same, for population. */
	ResistPopulationLoss		= 7,

	/** IntervalPercent. Restores that share of maximum defence every N days. */
	HealDefenceEvery			= 8,

	/** IntervalPercent. The same, for population. */
	RecoverPopulationEvery		= 9,

	/** Percent. Restores that share of maximum defence when the player clears a
	 *  dungeon standing on this city. */
	RestoreDefenceOnClear		= 10,

	// ----------------------------------------------------------------------
	// Explorer, the four that shape the dungeons a city receives. All four are
	// built, in `UCataclysmSurgeScheduler`.
	// ----------------------------------------------------------------------

	/**
	 * Flat. No more than this many dungeons may stand on this city.
	 *
	 * A FULL CITY STOPS BEING A TARGET AND THE WAVE LANDS ELSEWHERE, rather than
	 * the dungeon vanishing. See `UCataclysmSurgeScheduler::PickTargets`.
	 */
	DungeonCap					= 11,

	/**
	 * Flat. Dungeons here take that many fewer days to walk, minimum one day.
	 *
	 * THE FLOOR COUNT DOES NOT MOVE, AND THAT IS THE WHOLE POINT. One floor
	 * costs one day to begin with, and this lowers the time without lowering the
	 * depth: a fifty floor dungeon can be made to cost two days rather than
	 * fifty. It is still fifty floors deep, still worth what fifty floors are
	 * worth, and still bites on the same schedule.
	 *
	 * WHICH IS WHAT MAKES IT A DIFFERENT UPGRADE FROM `DungeonFloorsFewer`.
	 * That one buys speed by giving up depth, reward and resolve time; this one
	 * buys speed and gives up none of them. A player who wants a deep dungeon
	 * they can actually afford to run takes this.
	 *
	 * `FCataclysmDungeon::WalkDays` is where it lands.
	 */
	DungeonWalkDaysFewer		= 12,

	/**
	 * Flat. Dungeons here have that many more floors.
	 *
	 * SLOWER TO WALK, WORTH MORE, AND SLOWER TO BITE, because the resolve timer
	 * is derived from the floor count. That is the trade rather than a side
	 * effect.
	 *
	 * IT DOES NOT MAKE THE DUNGEON HIT HARDER. `FCataclysmDungeon::BiteScale`
	 * reads the floors the dungeon was rolled with, and
	 * `FCataclysmDungeon::UpgradeFloors` holds what this upgrade added. Until
	 * 2026-10-13 a dungeon made deeper here also took more from the city.
	 */
	DungeonFloorsMore			= 13,

	/** Flat. Dungeons here have that many fewer floors, minimum one. Quicker to
	 *  walk, worth less, and bites sooner, for the same reason. IT DOES NOT
	 *  MAKE THE DUNGEON HIT LESS: the hit is sized by the floors the dungeon
	 *  was rolled with. Until 2026-10-13 it did. */
	DungeonFloorsFewer			= 14,

	// ----------------------------------------------------------------------
	// Explorer, continued. NOT BUILT because nothing rolls a dungeon sub-type.
	// `ECataclysmDungeonSubType` names all eight and its own comment says
	// "Nothing in the empire layer rolls a sub-type yet". Issue #41.
	// ----------------------------------------------------------------------

	/** Percent. Raises the chance a dungeon here is a Horde dungeon. */
	SubTypeChanceHorde			= 15,

	/** Percent. The same, for Elite. */
	SubTypeChanceElite			= 16,

	/** Percent. The same, for Sacrificial. */
	SubTypeChanceSacrificial	= 17,

	/** Percent. The same, for Volatile. */
	SubTypeChanceVolatile		= 18,

	// ----------------------------------------------------------------------
	// Treasurer and Artisan. BUILT SINCE ISSUE #2781. Each changes what a walk
	// of a dungeon standing on this city pays, and in this layer an empire
	// dungeon is a timer and a bite. Walking one is the dungeon level's, where a
	// kill reads them through `UCataclysmCityUpgradeRules::DungeonRewardsOf`.
	// ----------------------------------------------------------------------

	/** Multiplier. Dungeons here grant that many times the experience. */
	DungeonExperience			= 19,

	/** Percent. Dungeons here have that much increased magic find. */
	DungeonMagicFind			= 20,

	/** Percent. Dungeons here drop that much more gold. */
	DungeonGold					= 21,

	/** Percent. Dungeons here drop that much more loot. */
	DungeonLoot					= 22,

	/** Percent. Dungeons here drop that many more crafting materials. */
	DungeonCraftingMaterials	= 23,

	// ----------------------------------------------------------------------
	// The unbranched last resort. BUILT SINCE ISSUE #2838.
	// ----------------------------------------------------------------------

	/**
	 * One-time, once a run. When it is finished, every city that has not fallen loses half of its dungeons, soonest
	 * to resolve first, and half of its remaining defence and population. Quest, Fallen City and Cataclysm dungeons
	 * are neither counted nor removed. See `UCataclysmEmpireRun::CleanseEveryCity`.
	 *
	 * ON DIFFICULTY TIER 3 AND ABOVE. Its text, "will only be available on T3 and above", was read as upgrade tier 3
	 * until issue #2838 and refused, because nothing raised an upgrade. The coordinating session ruled on 2026-11-22
	 * that "T3" is the difficulty tier: the row has no upgrade tier 2 or 3, and the design writes difficulty tiers
	 * as T1 to T8.
	 */
	CleanseEveryCity			= 24,
};

/**
 * Whether a purchase went through, and if not, why not.
 *
 * A REASON RATHER THAN A BOOL, because every one of these is a different thing
 * for a screen to say, and "false" tells a player nothing. The city screen is
 * the caller that will need each of them.
 */
UENUM(BlueprintType)
enum class ECataclysmCityUpgradeResult : uint8
{
	/** It went through. The only value that means the city changed. */
	Bought					= 0,

	/** `Begin` has not run, so there is no map to buy anything on. */
	RunHasNotBegun			= 1,

	/** No city carries that identifier. */
	NoSuchCity				= 2,

	/** A fallen city cannot be improved. Retake it first. */
	CityHasFallen			= 3,

	/** Every slot on that city is filled. Three, or two on Heretic. */
	NoSlotsLeft				= 4,

	/** That city already has this upgrade. A slot may not be spent twice on
	 *  the same thing. */
	AlreadyBought			= 5,

	/** The upgrade is real and its effect is not built yet. Refused rather than
	 *  sold, so no player spends a slot on nothing. See
	 *  `UCataclysmCityUpgradeRules::IsBuilt`. */
	EffectNotBuiltYet		= 6,

	/** `ECataclysmCityUpgradeEffect::None`, which is not an upgrade. */
	NotAnUpgrade			= 7,

	/**
	 * An upgrade is being built on that city, and a city builds one at a time.
	 *
	 * THE COORDINATING SESSION'S RULING OF 2026-11-21 (issue #2825): one build
	 * at a time per city, and any number of cities may build at once. It is
	 * checked after the slot count, so a city whose last slot is the one being
	 * built says it is full.
	 *
	 * IT WAS `CannotWaitForABuildYet` UNTIL ISSUE #2825, a guard that refused
	 * every purchase if the build days were raised without a timer, and
	 * `CannotPayYet` before issue #2742's second slice. The timer exists now.
	 */
	BuildUnderWay			= 8,

	/**
	 * A Siege stands on that city, and a besieged city may not be improved.
	 *
	 * THE DESIGN DOCUMENT: a Siege "Pauses city upgrades." The project owner
	 * settled what that means on 2026-09-05: it stops any upgrade that is part
	 * way through being built and blocks buying new ones, and it leaves the
	 * effects of upgrades the city already holds working. So a besieged city
	 * keeps its raised maximum defence and its damage resistances; it simply
	 * cannot add to them until the Siege is gone.
	 *
	 * BOTH HALVES EXIST SINCE ISSUE #2825. This refuses a purchase, and
	 * `UCataclysmEmpireRun::AdvanceCityBuilds` counts no day of a build on a
	 * besieged city, so the build waits until the Siege is gone.
	 */
	CityIsBesieged			= 9,

	/**
	 * The character buying it holds less gold than it costs. Issue #2742's
	 * second slice, ruled 2026-11-18.
	 *
	 * THE EMPIRE LAYER IS TOLD BOTH NUMBERS AND DECIDES, rather than looking the
	 * balance up: the gold is held by the account record or a Solo Self-Found
	 * character's player state, both in the `Cataclysm` module, which this
	 * module may not depend on. `UCataclysmCityUpgradePurchase::Buy` over there
	 * reads them and passes them in.
	 *
	 * CHECKED LAST, so a city that is full, fallen or besieged says that, which
	 * no amount of gold would change.
	 */
	NotEnoughGold			= 10,

	/** A rush was asked for and the character playing does not hold Rapid Funding, the empire upgrade tree's node that
	 *  offers one. Issue #2832. Said by `UCataclysmCityUpgradePurchase::Buy`, which reads the tree; the run never
	 *  answers it. */
	RushNotHeld				= 11,

	/**
	 * The upgrade is offered only from a difficulty tier the run is below. Only the cleanse has such a gate: "will
	 * only be available on T3 and above", read as difficulty tier 3 by the coordinating session's ruling of
	 * 2026-11-22. Issue #2838.
	 */
	BelowItsDifficultyTier	= 12,

	/**
	 * The upgrade may be bought once a run, on any city, and this run has bought it. Only the cleanse: "Can only
	 * purchase once", ruled once a run, empire-wide, on 2026-11-22. Issue #2838.
	 */
	AlreadyBoughtThisRun	= 13,

	/** A raise was asked for an upgrade already at tier 3, the top of every ladder. Issue #1265, ruled 2026-11-22. */
	AtTheTopTier			= 14,

	/** A raise was asked for an upgrade the city does not hold. Issue #1265. */
	NotHeld					= 15,

	/**
	 * A raise was asked for an upgrade with no tier ladder: the cleanse, the one row whose tier 2 and 3 cells are
	 * empty. Said by the run for the cleanse, and by `UCataclysmCityUpgradePurchase::Raise`, which reads the table, for
	 * any row without a next tier. Issue #1265.
	 */
	NoTierToRaiseTo			= 16,
};

/**
 * One upgrade a city has bought.
 *
 * IT CARRIES THE NUMBERS RATHER THAN A ROW NAME, so the empire layer never has
 * to look anything up and stays free of the `Cataclysm` module. What fills it in
 * is `UCataclysmCityUpgradeMapping::Make` over there.
 */
USTRUCT(BlueprintType)
struct CATACLYSMEMPIRE_API FCataclysmCityUpgrade
{
	GENERATED_BODY()

	/**
	 * Which row of `game/Data/CityUpgrades.csv` this is.
	 *
	 * CARRIED FOR DISPLAY AND FOR REFUSING A DUPLICATE, and read for nothing
	 * else. The effect below is what the empire layer acts on, so this module
	 * never has to know what any particular row is called.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	FName RowName;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	ECataclysmCityUpgradeEffect Effect = ECataclysmCityUpgradeEffect::None;

	/**
	 * How strong it is, in whatever unit the effect names.
	 *
	 * A PERCENTAGE IS A FRACTION, so 0.2 is twenty per cent. That is how the
	 * CSV stores it and converting on the way in would put two conventions in
	 * one system.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	float Value = 0.0f;

	/** Days between triggers. Only meaningful for the two "every N days"
	 *  effects; zero for every other. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	float IntervalDays = 0.0f;

	/**
	 * Which tier it is at, 1 to 3. Bought at tier 1.
	 *
	 * RAISED IN PLACE SINCE ISSUE #1265, the coordinating session's rulings of 2026-11-22: in the same slot, for the
	 * next tier's gold and its build days (`UCataclysmEmpireRun::RaiseCityUpgrade`), acting at its old tier while the
	 * raise is built and at the new one from the day it is finished. On an upgrade in `FCataclysmCity::Building` it is
	 * the tier the build will reach.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 Tier = 1;

	/**
	 * Fires once and is spent, rather than being a standing improvement.
	 *
	 * IT STILL FILLS A SLOT. The sheet marks four of these with an asterisk on
	 * the branch name, and the 2026-08-02 decision says they fire once and are
	 * spent. Nothing in the design says the slot comes back, so it does not.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	bool bOneTimeUse = false;

	/**
	 * The next day one of the two "every N days" effects should fire.
	 *
	 * SET WHEN THE UPGRADE IS BOUGHT, to the day it was bought plus the
	 * interval, so the first trigger is a full interval away rather than
	 * immediate. Zero for every other effect, and unread for them.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 NextTriggerDay = 0;

	/**
	 * The day it was bought, which Enduring Foundations reads: "City upgrades that have been active for 10+ days".
	 * Issue #2741 slice 3b, Q6.
	 *
	 * SET BY `UCataclysmEmpireRun::CompleteCityUpgrade`, ON THE DAY THE UPGRADE IS FINISHED, since issue #2825: an
	 * upgrade does nothing while it is being built, so it has not been "active" until then. RUN RECORD VERSION 9: an
	 * upgrade saved before the field is given day 0 by `Migrate_8_to_9`, so it counts as held since its run began.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 BoughtDay = 0;

	/**
	 * The days still to count before an upgrade being built is finished. Issue #2825, ruled 2026-11-21.
	 *
	 * NOUGHT ON EVERY FINISHED UPGRADE, in `FCataclysmCity::Upgrades`. Above nought only on one in
	 * `FCataclysmCity::Building`, where `UCataclysmEmpireRun::AdvanceCityBuilds` takes a day off it for each day that
	 * passes while the city is neither fallen nor besieged. DAYS LEFT RATHER THAN A FINISHING DAY, because a pause
	 * moves the finishing day and would have to rewrite it. RUN RECORD VERSION 13: `Migrate_12_to_13` writes 0 on every
	 * upgrade a version 12 run holds, since all of them were finished.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 BuildDaysLeft = 0;

	bool IsValid() const
	{
		return Effect != ECataclysmCityUpgradeEffect::None;
	}
};

/**
 * WHAT THE UPGRADES A CITY HOLDS GIVE A KILL IN A DUNGEON STANDING ON IT. Issue #2781, the coordinating session's
 * rulings of 2026-11-20: the four Treasurer upgrades and the Artisan one, each at its tier 1 figure (ruling 3); every
 * "more" a multiplier of its own and the "increased" magic find added to the kill's other magic find (ruling 2); and
 * each for kills in a dungeon standing on the city that holds it (ruling 4). Worked out by
 * `UCataclysmCityUpgradeRules::DungeonRewardsOf` and read at a kill through
 * `ACataclysmDungeonGameMode::CityRewardsIn`. THE DEFAULT GIVES NOTHING: every multiplier exactly 1, so a kill with
 * none draws its stream as it did.
 */
struct CATACLYSMEMPIRE_API FCataclysmCityDungeonRewards
{
	/** "Dungeons here provide 2x more experience": the creature's experience figure is multiplied, and skill experience
	 *  follows it (ruling 1). */
	float ExperienceMore = 1.0f;

	/** "Dungeons here have 10% increased magic find": percentage points added to the kill's magic find. */
	float MagicFindIncreased = 0.0f;

	/** "Dungeons here drop 25% more gold": each gold pile, after the empire tree's gold multiplier. */
	float GoldMore = 1.0f;

	/** "Dungeons here drop 25% more loot": the kill's loot quantity, so its gear and its materials. */
	float LootMore = 1.0f;

	/** "Dungeons here drop 25% more crafting materials": the kill's ordinary material roll only. */
	float MaterialsMore = 1.0f;

	/** The creature's experience figure after this city's "more" (ruling 1, X4): the figure itself when there is none,
	 *  so a kill elsewhere grants exactly what it did. */
	int32 ExperienceFrom(int32 Score) const
	{
		return ExperienceMore == 1.0f
			? Score
			: FMath::RoundToInt32(static_cast<double>(Score) * FMath::Max(0.0f, ExperienceMore));
	}

	/** Whether any figure differs from giving nothing. */
	bool GivesAnything() const
	{
		return ExperienceMore != 1.0f || MagicFindIncreased != 0.0f || GoldMore != 1.0f || LootMore != 1.0f
			|| MaterialsMore != 1.0f;
	}
};

/**
 * The rules a city upgrade obeys: how many a city may hold, which effects are
 * built, and what one costs.
 *
 * STATICS ON A CLASS, the arrangement `UCataclysmDayClock` and
 * `UCataclysmEmpireMap` already use for their own constants, so a Blueprint can
 * reach them.
 */
UCLASS(BlueprintType)
class CATACLYSMEMPIRE_API UCataclysmCityUpgradeRules : public UObject
{
	GENERATED_BODY()

public:
	// ----------------------------------------------------------------------
	// Slots
	// ----------------------------------------------------------------------

	/** `docs/Cataclysm_GDD_v2.md`: "Each city has upgrade slots (3 normally, 2
	 *  on Heretic difficulty)." */
	static constexpr int32 SlotsNormally = 3;

	/** The same sentence's other half, and the Heretic row of the difficulty
	 *  table: "Cities have 2 upgrade slots instead of 3." */
	static constexpr int32 SlotsOnHeretic = 2;

	/**
	 * How many upgrade slots each city has.
	 *
	 * @param LethalityRung 0 Standard, 1 Hardcore, 2 Heretic, the same numbering
	 *                      `UCataclysmEmpireRun::Begin` and
	 *                      `UCataclysmDayClock::DeathDayCostFor` use.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static int32 SlotsFor(int32 LethalityRung);

	// ----------------------------------------------------------------------
	// What an upgrade costs
	// ----------------------------------------------------------------------

	// THE PRICE IN GOLD IS NOT HERE. It is data -- the City Upgrades sheet's
	// Tier 1 Gold column, scaled to the run's difficulty tier -- and data lives
	// in the `Cataclysm` module, which this one may not depend on.
	// `UCataclysmCityUpgradeMapping::PriceFor` computes it there, and
	// `UCataclysmEmpireRun::BuyCityUpgrade` is told it, with the gold the
	// character holds. Until issue #2742's second slice (2026-11-18) this was a
	// constant `GoldCost` of zero: the owner decided on 2026-09-05 that an
	// upgrade is free until gold exists, and on 2026-11-18 that it costs gold.
	// Issues #1264 and #2742; `docs/DECISIONS.md` records both.

	// ----------------------------------------------------------------------
	// How long an upgrade takes to build. Issue #2825.
	// ----------------------------------------------------------------------

	// THE OWNER, 2026-11-21, reversing the ruling of 2026-11-18 that an upgrade
	// is instant: "They need build times. Probably based on their tier." "Their
	// tier" is the UPGRADE's tier, the 1 to 3 ladder its price uses, ruled by
	// the coordinating session the same day, as were the figures below.
	//
	// THE DAYS ARE NOT SCALED BY THE DIFFICULTY TIER. A price is, because gold
	// income is; a floor is one day at every tier, and so is a day of building.

	/**
	 * Tier 1's days. DERIVED: Rapid Renovation takes up to 3 days off and Rapid
	 * Funding 2, and both texts say "Min 1 day", so 3 + 2 + 1 is the least at
	 * which both act in full.
	 */
	static constexpr int32 BuildDaysTier1 = 6;

	/** Tier 2's days. A JUDGEMENT: the price ladder's doubling. */
	static constexpr int32 BuildDaysTier2 = 12;

	/** Tier 3's days. A JUDGEMENT: the price ladder's doubling. */
	static constexpr int32 BuildDaysTier3 = 24;

	/** "Min 1 day", in both Rapid Renovation's and Rapid Funding's texts: no build is shorter. */
	static constexpr int32 MinimumBuildDays = 1;

	/**
	 * The days an upgrade at that tier takes to build, before the empire upgrade tree's three build nodes
	 * (Scaffolding, Rapid Renovation, Rapid Funding, which act in the `Cataclysm` module because the tree is there).
	 *
	 * A TIER BELOW 1 IS READ AS 1 AND ONE ABOVE 3 AS 3, the ends of the ladder, as `UCataclysmCityUpgradeMapping`
	 * reads a price. A purchase is at tier 1; a raise is at the tier it reaches, 2 or 3 (issue #1265).
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static int32 BuildDaysFor(int32 UpgradeTier);

	/** The top of every upgrade's tier ladder: the sheet's Tier 1, Tier 2 and Tier 3 columns. Issue #1265. */
	static constexpr int32 HighestTier = 3;

	// ----------------------------------------------------------------------
	// The cleanse. Issue #2838, ruled by the coordinating session on 2026-11-22.
	// ----------------------------------------------------------------------

	/** "will only be available on T3 and above": the lowest difficulty tier the cleanse is offered at. */
	static constexpr int32 CleanseLowestDifficultyTier = 3;

	/** "Cleanse every player city of half of the dungeons on them": the share of each city's counted dungeons it
	 *  removes, rounded as `RemoveDungeons` rounds. */
	static constexpr float CleanseDungeonShare = 0.5f;

	/** "The cities lose 50% of their remaining defenses and population": the share of each that is taken. */
	static constexpr float CleanseLossShare = 0.5f;

	// ----------------------------------------------------------------------
	// Which effects exist
	// ----------------------------------------------------------------------

	/**
	 * Whether the empire layer actually acts on this effect.
	 *
	 * THE ONE PLACE THAT KNOWS. Twenty of the 24 are built since issue #2838
	 * and four are not, each waiting on a dungeon sub-type roll (issue #41).
	 * `ECataclysmCityUpgradeEffect` names what each is waiting for, beside the
	 * value.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static bool IsBuilt(ECataclysmCityUpgradeEffect Effect);

	/**
	 * What these upgrades give a kill in a dungeon standing on the city that holds them. Issue #2781: the five
	 * dungeon-reward effects at each upgrade's own figure, which is its tier 1 one (ruling 3); every other effect gives
	 * nothing here. The caller passes a fallen city's upgrades as none.
	 */
	static FCataclysmCityDungeonRewards DungeonRewardsOf(const TArray<FCataclysmCityUpgrade>& Upgrades);

	/** How many of the 24 effects are built. Read by the tests, so a change to
	 *  `IsBuilt` has to be a deliberate one. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static int32 BuiltEffectCount();

	/** Every effect the enum names, in order, without `None`. */
	static TArray<ECataclysmCityUpgradeEffect> AllEffects();

	/** "MaxDefence", "RemoveDungeons", and so on. For a log line and a screen. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString EffectName(ECataclysmCityUpgradeEffect Effect);

	/** Why a purchase was refused, in words a screen can show. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString ResultText(ECataclysmCityUpgradeResult Result);
};
