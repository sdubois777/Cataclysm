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
 * TEN OF THEM DO NOTHING YET AND THAT IS DELIBERATE, not an oversight.
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
	 */
	DungeonFloorsMore			= 13,

	/** Flat. Dungeons here have that many fewer floors, minimum one. Quicker to
	 *  walk, worth less, and bites sooner, for the same reason. */
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
	// Treasurer and Artisan. NOT BUILT. Each needs a reward a dungeon in this
	// layer does not have: an empire dungeon is a timer and a bite, and it
	// grants no experience, no loot, no gold and no materials. Issue #41 is the
	// dungeon runtime; issue #1264 is gold.
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
	// The unbranched last resort. NOT BUILT.
	// ----------------------------------------------------------------------

	/**
	 * One-time. Cleanses half the dungeons from every city and costs every city
	 * half its remaining defence and population.
	 *
	 * NOT BUILT BECAUSE THE DESIGN GATES IT AND THERE IS NOTHING TO GATE ON.
	 * Its own text says it "will only be available on T3 and above", meaning
	 * upgrade tier 3, and nothing in this game raises a bought upgrade above
	 * tier 1. Building it now would make it buyable at tier 1, which the design
	 * forbids. Issue #1265.
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
	 * An upgrade is no longer free and instant, and nothing can pay for it.
	 *
	 * THIS IS A GUARD RATHER THAN A STATE THE GAME REACHES. It fires only if
	 * someone raises `UCataclysmCityUpgradeRules::GoldCost` or `BuildDays` above
	 * zero without building the payment and the timer that would then be
	 * needed. Refusing loudly is better than silently handing out a paid upgrade
	 * for nothing. See `IsFreeAndInstant`.
	 */
	CannotPayYet			= 8,

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
	 * ONLY THE BLOCKING HALF EXISTS, AND THAT IS NOT AN OVERSIGHT. Nothing is
	 * ever part way through being built, because `BuildDays` is zero and every
	 * purchase is instant -- see `IsFreeAndInstant`. There is no in-progress
	 * build for a Siege to interrupt until issue #1264 gives an upgrade a build
	 * time, and raising `BuildDays` to create one would trip that same guard and
	 * refuse every purchase in the game.
	 */
	CityIsBesieged			= 9,
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
	 * Which tier it was bought at. Always 1 today.
	 *
	 * NOTHING RAISES IT. The sheet gives every upgrade a tier 2 and a tier 3
	 * value and no system in this game upgrades an upgrade, so a bought upgrade
	 * is at tier 1 for the whole run. That is also why
	 * `ECataclysmCityUpgradeEffect::CleanseEveryCity` is refused: it is gated on
	 * tier 3 and tier 3 is unreachable.
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

	bool IsValid() const
	{
		return Effect != ECataclysmCityUpgradeEffect::None;
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

	/**
	 * Gold to buy one. ZERO, DELIBERATELY, AND IT SHOULD NOT STAY ZERO.
	 *
	 * The design has five empire tree nodes that only make sense if a city
	 * upgrade costs gold -- Public Works, Municipal Bonds, Global Credit,
	 * Scaffolding and Reinvestment, all in
	 * `docs/Empire_Skill_Tree_Keystones.md`. **No such cost is written
	 * anywhere**: the City Upgrades sheet has four columns and none of them is a
	 * price, and no character, account or run in this game holds gold at all.
	 *
	 * So an upgrade is free until gold exists and a cost is designed. The
	 * project owner decided that on 2026-09-05, to be revisited once gold drops
	 * are in. Issue #1264, and `docs/DECISIONS.md` records it.
	 */
	static constexpr int32 GoldCost = 0;

	/**
	 * Days to build one. ZERO, DELIBERATELY, AND IT SHOULD NOT STAY ZERO.
	 *
	 * `docs/Cataclysm_GDD_v2.md`: "Time is the primary resource -- every
	 * dungeon, craft, and upgrade costs days." Rapid Renovation reduces "city
	 * upgrade construction time (Min 1 day)" and Scaffolding makes upgrades
	 * "take 2x longer to build", so both a base duration and a floor are
	 * assumed. Neither is written down. Same decision, same issue.
	 *
	 * AN UPGRADE THEREFORE TAKES EFFECT THE MOMENT IT IS BOUGHT. There is no
	 * build timer and no half-built upgrade anywhere in this module. Adding one
	 * is not a matter of raising this number: the standing effects are applied
	 * to the city when the upgrade is bought rather than recomputed each day, so
	 * a delay would need them to become derived first. `IsFreeAndInstant` below
	 * is the guard that stops this being changed by halves.
	 */
	static constexpr int32 BuildDays = 0;

	/**
	 * Whether an upgrade is still free and immediate.
	 *
	 * WHY A PURCHASE ASKS. Raising either constant above zero without also
	 * building something to pay from and something to wait on would hand out a
	 * paid upgrade for nothing, silently. `UCataclysmEmpireRun::BuyCityUpgrade`
	 * refuses with `ECataclysmCityUpgradeResult::CannotPayYet` instead, so the
	 * half-done change fails loudly at the first purchase.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static bool IsFreeAndInstant();

	// ----------------------------------------------------------------------
	// Which effects exist
	// ----------------------------------------------------------------------

	/**
	 * Whether the empire layer actually acts on this effect.
	 *
	 * THE ONE PLACE THAT KNOWS. Ten of the 24 are built and fourteen are not,
	 * and every one of the fourteen is waiting on a system that does not exist:
	 * a dungeon sub-type roll, a dungeon that grants rewards, gold, or an
	 * upgrade tier above one. `ECataclysmCityUpgradeEffect` names what each is
	 * waiting for, beside the value.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static bool IsBuilt(ECataclysmCityUpgradeEffect Effect);

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
