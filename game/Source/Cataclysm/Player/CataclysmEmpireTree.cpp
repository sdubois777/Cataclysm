// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmEmpireTree.h"
#include "Data/CataclysmCityUpgradeMapping.h"
#include "Data/CataclysmDataRows.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"

const FString UCataclysmEmpireTree::CentreBranch = TEXT("Centre");
const FString UCataclysmEmpireTree::BasicKind = TEXT("basic");
const FString UCataclysmEmpireTree::KeystoneKind = TEXT("keystone");
const FString UCataclysmEmpireTree::CapstoneKind = TEXT("capstone");

// A NAMED NAMESPACE AND NOT AN UNNAMED ONE. `CataclysmPassiveTree.cpp` has helpers of the same names in its unnamed
// namespace, and a unity build may compile both files as one, where two unnamed namespaces are one namespace and the
// helpers would be defined twice.
namespace CataclysmEmpireTreeTables
{
	const TCHAR* NodeAssetPath = TEXT("/Game/Data/DT_EmpireTreeNodes.DT_EmpireTreeNodes");
	const TCHAR* EdgeAssetPath = TEXT("/Game/Data/DT_EmpireTreeEdges.DT_EmpireTreeEdges");
	const TCHAR* EffectAssetPath = TEXT("/Game/Data/DT_EmpireTreeEffects.DT_EmpireTreeEffects");

	/**
	 * Kept alive deliberately, as `UCataclysmPassiveTree` keeps its tables: nothing else references the asset, so
	 * garbage collection would otherwise take it back and the next question would pay the load again.
	 */
	TWeakObjectPtr<const UDataTable> CachedNodes;
	TWeakObjectPtr<const UDataTable> CachedEdges;
	TWeakObjectPtr<const UDataTable> CachedEffects;

	const UDataTable* LoadAndKeep(const TCHAR* Path, TWeakObjectPtr<const UDataTable>& Cache)
	{
		if (Cache.IsValid())
		{
			return Cache.Get();
		}

		const UDataTable* Table = LoadObject<UDataTable>(nullptr, Path);
		if (Table)
		{
			const_cast<UDataTable*>(Table)->AddToRoot();
			Cache = Table;
		}
		return Table;
	}

	void ForEachNode(const UDataTable* Table,
					 const TFunctionRef<void(FName, const FCataclysmEmpireTreeNodeRow&)>& Visit)
	{
		if (!Table)
		{
			return;
		}

		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			if (const FCataclysmEmpireTreeNodeRow* Row = reinterpret_cast<const FCataclysmEmpireTreeNodeRow*>(Pair.Value))
			{
				Visit(Pair.Key, *Row);
			}
		}
	}

	void ForEachEdge(const UDataTable* Table, const TFunctionRef<void(const FCataclysmEmpireTreeEdgeRow&)>& Visit)
	{
		if (!Table)
		{
			return;
		}

		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			if (const FCataclysmEmpireTreeEdgeRow* Row = reinterpret_cast<const FCataclysmEmpireTreeEdgeRow*>(Pair.Value))
			{
				Visit(*Row);
			}
		}
	}

	/** One of a row's three option fields, by option number 1 to 3; empty for any other number. */
	const FString& OptionField(const FCataclysmEmpireTreeNodeRow& Row, int32 Option, bool bWaitsOn)
	{
		static const FString Nothing;
		switch (Option)
		{
		case 1: return bWaitsOn ? Row.Option1WaitsOn : Row.Option1Name;
		case 2: return bWaitsOn ? Row.Option2WaitsOn : Row.Option2Name;
		case 3: return bWaitsOn ? Row.Option3WaitsOn : Row.Option3Name;
		default: return Nothing;
		}
	}

	bool IsCentreCapstone(const FCataclysmEmpireTreeNodeRow& Row)
	{
		return Row.Kind == UCataclysmEmpireTree::CapstoneKind && Row.Branch == UCataclysmEmpireTree::CentreBranch;
	}
}

// EVERY HELPER IS NAMED WITH ITS NAMESPACE BELOW, AND THERE IS NO `using namespace`. In a unity build a using
// directive here would reach the files compiled after this one and make their own helpers of the same names ambiguous.

// ---------------------------------------------------------------------------
// Reading the tables
// ---------------------------------------------------------------------------

const UDataTable* UCataclysmEmpireTree::LoadNodeTable()
{
	return CataclysmEmpireTreeTables::LoadAndKeep(CataclysmEmpireTreeTables::NodeAssetPath,
												  CataclysmEmpireTreeTables::CachedNodes);
}

const UDataTable* UCataclysmEmpireTree::LoadEdgeTable()
{
	return CataclysmEmpireTreeTables::LoadAndKeep(CataclysmEmpireTreeTables::EdgeAssetPath,
												  CataclysmEmpireTreeTables::CachedEdges);
}

const UDataTable* UCataclysmEmpireTree::LoadEffectTable()
{
	return CataclysmEmpireTreeTables::LoadAndKeep(CataclysmEmpireTreeTables::EffectAssetPath,
												  CataclysmEmpireTreeTables::CachedEffects);
}

// ---------------------------------------------------------------------------
// What the city nodes add up to -- issue #2741 slice 3a
// ---------------------------------------------------------------------------

// WHAT ONE ROW OF THE EFFECT TABLE IS WORTH, read the same way by `CityBonusesFor` and `PlayerBonusesFor`. A named
// namespace for the unity build's sake, as the one above.
namespace CataclysmEmpireTreeEffectRows
{
	/** A node's points, or one for an option that is chosen and whose capstone's point is bought. */
	int32 PointsFor(const FCataclysmEmpireTreeEffectRow& Row, const FCataclysmPassiveAllocation& Allocation)
	{
		const FName Node(*Row.Node);
		const int32 Held = Allocation.PointsIn(Node);
		return Row.Option == 0
			? Held
			: (Held > 0 && Allocation.ChosenOptionIn(Node) == Row.Option ? 1 : 0);
	}

	/** The total, for an effect that adds: per point, times whole steps of another branch where it counts them (C3),
	 *  held at the cap where there is one. */
	float TotalFor(const UDataTable* NodeTable, const FCataclysmEmpireTreeEffectRow& Row,
				   const FCataclysmPassiveAllocation& Allocation, int32 Points)
	{
		float Total = Row.PerPoint * static_cast<float>(Points);
		if (Row.StepPoints > 0)
		{
			Total *= static_cast<float>(
				UCataclysmEmpireTree::SpentInBranch(NodeTable, Allocation, Row.StepBranch) / Row.StepPoints);
		}
		if (Row.Cap > 0.0f)
		{
			Total = FMath::Min(Total, Row.Cap);
		}
		return Total;
	}
}

FCataclysmEmpireTreeBonuses UCataclysmEmpireTree::CityBonusesFor(const UDataTable* NodeTable,
																 const UDataTable* EffectTable,
																 const FCataclysmPassiveAllocation& Allocation)
{
	FCataclysmEmpireTreeBonuses Bonuses;
	if (!NodeTable || !EffectTable)
	{
		return Bonuses;
	}

	for (const TPair<FName, uint8*>& Pair : EffectTable->GetRowMap())
	{
		const FCataclysmEmpireTreeEffectRow* Row = reinterpret_cast<const FCataclysmEmpireTreeEffectRow*>(Pair.Value);
		if (!Row)
		{
			continue;
		}

		const int32 Points = CataclysmEmpireTreeEffectRows::PointsFor(*Row, Allocation);
		if (Points <= 0)
		{
			continue;
		}

		// A COMPOUNDED EFFECT BELOW READS `PerPoint` AND `Points` RATHER THAN THE TOTAL.
		const float Total = CataclysmEmpireTreeEffectRows::TotalFor(NodeTable, *Row, Allocation, Points);
		const float Kept = FMath::Pow(FMath::Max(0.0f, 1.0f - Row->PerPoint), static_cast<float>(Points));

		switch (Row->Effect)
		{
		case ECataclysmEmpireTreeEffect::MaxDefenceAll:
			Bonuses.MaxDefenceAll += Total;
			break;
		case ECataclysmEmpireTreeEffect::MaxDefenceOutposts:
			Bonuses.MaxDefenceOutposts += Total;
			break;
		case ECataclysmEmpireTreeEffect::MaxDefenceBulwarksAndSanctuaries:
			Bonuses.MaxDefenceBulwarks += Total;
			Bonuses.MaxDefenceSanctuaries += Total;
			break;
		case ECataclysmEmpireTreeEffect::MaxDefenceSanctuaries:
			Bonuses.MaxDefenceSanctuaries += Total;
			break;
		case ECataclysmEmpireTreeEffect::MaxDefenceNearPillar:
			Bonuses.MaxDefenceNearPillar += Total;
			break;
		case ECataclysmEmpireTreeEffect::MaxDefencePerDefendedCity:
			Bonuses.MaxDefencePerDefendedCity += Total;
			break;
		case ECataclysmEmpireTreeEffect::BasePopulationAll:
			Bonuses.BasePopulationAll += Total;
			break;
		case ECataclysmEmpireTreeEffect::BasePopulationOutposts:
			Bonuses.BasePopulationOutposts += Total;
			break;
		case ECataclysmEmpireTreeEffect::BasePopulationBulwarks:
			Bonuses.BasePopulationBulwarks += Total;
			break;
		case ECataclysmEmpireTreeEffect::TotalPopulationCompounded:
			Bonuses.TotalPopulationFactor *= FMath::Pow(1.0f + Row->PerPoint, static_cast<float>(Points));
			break;
		case ECataclysmEmpireTreeEffect::TotalPopulationPerBranchStep:
			Bonuses.TotalPopulationFactor *= 1.0f + Total;
			break;
		case ECataclysmEmpireTreeEffect::DamageTakenCompounded:
			Bonuses.DamageTaken *= Kept;
			break;
		case ECataclysmEmpireTreeEffect::PopulationDamageTakenCompounded:
			Bonuses.PopulationDamageTaken *= Kept;
			break;
		case ECataclysmEmpireTreeEffect::DamageTakenAdjacentToPillarCompounded:
			Bonuses.DamageTakenAdjacentToPillar *= Kept;
			break;
		case ECataclysmEmpireTreeEffect::PopulationDamageTakenNearPillarCompounded:
			Bonuses.PopulationDamageTakenNearPillar *= Kept;
			break;
		case ECataclysmEmpireTreeEffect::DamageTakenPerActiveTypeCompounded:
			Bonuses.DamageTakenPerActiveType *= Kept;
			break;
		case ECataclysmEmpireTreeEffect::DamageTakenByLivingShare:
			Bonuses.LivingShareReductionPerPoint = Row->PerPoint;
			Bonuses.LivingSharePoints += Points;
			break;
		case ECataclysmEmpireTreeEffect::DamageTakenWithUpgrades:
			Bonuses.DamageTakenWithUpgrades *= Kept;
			break;
		case ECataclysmEmpireTreeEffect::DamageTakenFromCommonestType:
			Bonuses.DamageTakenFromCommonestType *= Kept;
			break;
		case ECataclysmEmpireTreeEffect::ResolveDaysAll:
		case ECataclysmEmpireTreeEffect::ResolveDaysPerBranchStep:
			Bonuses.ResolveDaysAll += Total;
			break;
		case ECataclysmEmpireTreeEffect::ResolveDaysOutposts:
			Bonuses.ResolveDaysOutposts += Total;
			break;
		case ECataclysmEmpireTreeEffect::ResolveDaysBelowTenPercent:
			Bonuses.ResolveDaysBelowTenPercent += Total;
			break;
		case ECataclysmEmpireTreeEffect::SurgeGapDays:
			Bonuses.SurgeGapDays += Total;
			break;

		// SLICE 3B, the rules that act at an event. A rule read where its event happens holds its figure here, and the
		// two counted reductions their per-clear figure, points and cap apart, because the cap is on the whole and not
		// on a point's share of it (Q1, Q2).
		case ECataclysmEmpireTreeEffect::RetakeFraction:
			Bonuses.RetakeFraction = FMath::Max(Bonuses.RetakeFraction, Row->PerPoint);
			break;
		case ECataclysmEmpireTreeEffect::OutpostDefenceRegeneration:
			Bonuses.OutpostRegeneration += Total;
			break;
		case ECataclysmEmpireTreeEffect::LowestCityRestoresDefence:
			Bonuses.LowestCityDefenceRestored += Total;
			break;
		case ECataclysmEmpireTreeEffect::LowestCityRestoresPopulation:
			Bonuses.LowestCityPopulationRestored += Total;
			break;
		case ECataclysmEmpireTreeEffect::DamageTakenPerClearThisSurge:
			Bonuses.ReductionPerClear = Row->PerPoint;
			Bonuses.ClearPoints += Points;
			Bonuses.ClearReductionCap = Row->Cap;
			break;
		case ECataclysmEmpireTreeEffect::DamageTakenPerCityReclaimed:
			Bonuses.ReductionPerReclaim = Row->PerPoint;
			Bonuses.ReclaimPoints += Points;
			Bonuses.ReclaimReductionCap = Row->Cap;
			break;
		case ECataclysmEmpireTreeEffect::SanctuarySurvivesAt:
			Bonuses.SanctuarySurvivesAt = FMath::Max(Bonuses.SanctuarySurvivesAt, Row->PerPoint);
			break;
		case ECataclysmEmpireTreeEffect::UpgradeEffectAfterDays:
			Bonuses.UpgradeStrengthAfterDays += Total;
			break;

		// SLICE 4, a dungeon's walk and depth. Sovereign's Haste's cap is on its days times the active types, so its
		// per-type figure and cap are held apart; Architectural Insight's steps and cap are in `Total` (W4, C3).
		case ECataclysmEmpireTreeEffect::WalkTimeCompounded:
			Bonuses.WalkTimeFactor *= Kept;
			break;
		case ECataclysmEmpireTreeEffect::WalkTimeHalvedAboveFloors:
			Bonuses.HalveWalkAboveFloors = FMath::RoundToInt(Row->PerPoint);
			break;
		case ECataclysmEmpireTreeEffect::WalkDaysFewer:
			Bonuses.WalkDaysFewer += Total;
			break;
		case ECataclysmEmpireTreeEffect::WalkDaysFewerWhenAlone:
			Bonuses.WalkDaysFewerWhenAlone += Total;
			break;
		case ECataclysmEmpireTreeEffect::WalkDaysFewerPerActiveType:
			Bonuses.WalkDaysFewerPerActiveType += Row->PerPoint * static_cast<float>(Points);
			Bonuses.WalkDaysFewerPerActiveTypeCap = Row->Cap;
			break;
		case ECataclysmEmpireTreeEffect::WalkDaysFewerOnFallenCities:
			Bonuses.WalkDaysFewerOnFallenCities += Total;
			break;
		case ECataclysmEmpireTreeEffect::WalkDaysFewerPerFloorCleared:
			Bonuses.WalkDaysFewerPerFloorCleared += Total;
			break;
		case ECataclysmEmpireTreeEffect::WalkOneDayWithinDaysOfFalling:
			Bonuses.OneDayWalkWithinDaysOfFalling = FMath::RoundToInt(Row->PerPoint);
			break;
		case ECataclysmEmpireTreeEffect::FloorsAdded:
		case ECataclysmEmpireTreeEffect::FloorsPerBranchStep:
			Bonuses.FloorsAdded += FMath::RoundToInt(Total);
			break;
		case ECataclysmEmpireTreeEffect::FloorsAddedPerActiveType:
			Bonuses.FloorsAddedPerActiveType += FMath::RoundToInt(Total);
			break;
		case ECataclysmEmpireTreeEffect::RemoveAnotherDungeonChance:
			Bonuses.RemoveAnotherDungeonChance = FMath::Max(Bonuses.RemoveAnotherDungeonChance, Row->PerPoint);
			break;
		case ECataclysmEmpireTreeEffect::EmpirePointsFromClears:
			Bonuses.EmpirePointsIncrease += Total;
			break;
		default:
			break;
		}
	}
	return Bonuses;
}

FCataclysmEmpireTreePlayerBonuses UCataclysmEmpireTree::PlayerBonusesFor(const UDataTable* NodeTable,
																	   const UDataTable* EffectTable,
																	   const FCataclysmPassiveAllocation& Allocation)
{
	FCataclysmEmpireTreePlayerBonuses Bonuses;
	if (!NodeTable || !EffectTable)
	{
		return Bonuses;
	}

	for (const TPair<FName, uint8*>& Pair : EffectTable->GetRowMap())
	{
		const FCataclysmEmpireTreeEffectRow* Row = reinterpret_cast<const FCataclysmEmpireTreeEffectRow*>(Pair.Value);
		if (!Row)
		{
			continue;
		}
		const int32 Points = CataclysmEmpireTreeEffectRows::PointsFor(*Row, Allocation);
		if (Points <= 0)
		{
			continue;
		}
		const float Total = CataclysmEmpireTreeEffectRows::TotalFor(NodeTable, *Row, Allocation, Points);

		// THE EXPLORER BRANCH'S FLAT FIGURES APART FROM EVERY OTHER'S, for One-Day Specialist. Read from the node table
		// rather than named: The Hoarder is a centre capstone's option, and that is why it is not doubled.
		const bool bExplorer = BranchOf(NodeTable, FName(*Row->Node)) == TEXT("Explorer");

		switch (Row->Effect)
		{
		case ECataclysmEmpireTreeEffect::MagicFindAdded:
			(bExplorer ? Bonuses.ExplorerMagicFind : Bonuses.MagicFind) += Total;
			break;
		case ECataclysmEmpireTreeEffect::LootQuantityIncreased:
		case ECataclysmEmpireTreeEffect::LootQuantityPerBranchStep:
			(bExplorer ? Bonuses.ExplorerLootQuantity : Bonuses.LootQuantity) += Total;
			break;
		case ECataclysmEmpireTreeEffect::MovementSpeedInDungeons:
			Bonuses.MovementSpeed += Total;
			break;
		case ECataclysmEmpireTreeEffect::LootQuantityPerActiveType:
			Bonuses.LootQuantityPerActiveType += Total;
			break;

		// THE CAP IS ON WHAT THE DAYS BRING, so the figure a step is held without it and the cap beside it.
		case ECataclysmEmpireTreeEffect::LootQuantityPerTwoDaysRemoved:
			Bonuses.LootQuantityPerTwoDaysRemoved += Row->PerPoint * static_cast<float>(Points);
			Bonuses.TwoDaysRemovedCap = Row->Cap;
			break;
		case ECataclysmEmpireTreeEffect::LootQuantityPerDayRemoved:
			Bonuses.LootQuantityPerDayRemoved += Row->PerPoint * static_cast<float>(Points);
			Bonuses.DayRemovedCap = Row->Cap;
			break;

		case ECataclysmEmpireTreeEffect::MagicFindPerFloorRemoved:
			Bonuses.MagicFindPerFloorRemoved += Total;
			break;
		case ECataclysmEmpireTreeEffect::MagicFindPerModifierActive:
			Bonuses.MagicFindPerModifierActive += Total;
			break;
		case ECataclysmEmpireTreeEffect::MagicFindPerFloorCleared:
			Bonuses.MagicFindPerFloorCleared += Total;
			break;
		case ECataclysmEmpireTreeEffect::MagicFindPerFiveClears:
			Bonuses.MagicFindPerFiveClears += Total;
			break;
		case ECataclysmEmpireTreeEffect::LootQuantityPerExplorerCity:
			Bonuses.LootQuantityPerExplorerCity += Total;
			break;
		case ECataclysmEmpireTreeEffect::MagicFindPerExplorerCity:
			Bonuses.MagicFindPerExplorerCity += Total;
			break;
		case ECataclysmEmpireTreeEffect::ExplorerLootDoubledAtOneDay:
			Bonuses.ExplorerLootDoubledAtWalkDays = FMath::RoundToInt(Row->PerPoint);
			break;
		case ECataclysmEmpireTreeEffect::DamagePerKindDefeated:
			Bonuses.DamagePerKindDefeated += Total;
			break;

		// SLICE 5B. Relentless Pursuit's cap is on what the clears bring, so it is held beside the figure (D5).
		case ECataclysmEmpireTreeEffect::LootQuantityPerCleanClear:
			Bonuses.LootQuantityPerCleanClear += Row->PerPoint * static_cast<float>(Points);
			Bonuses.CleanClearCap = Row->Cap;
			break;
		case ECataclysmEmpireTreeEffect::EliteExtraItemChance:
			Bonuses.EliteExtraItemChance += Total;
			break;
		case ECataclysmEmpireTreeEffect::ExtraMaterialChance:
			Bonuses.ExtraMaterialChance += Total;
			break;
		case ECataclysmEmpireTreeEffect::LowTierMaterialExtraChance:
			Bonuses.LowTierMaterialExtraChance += Total;
			break;
		case ECataclysmEmpireTreeEffect::BossLegendaryChancePerTenFloorsAdded:
			Bonuses.BossLegendaryChancePerTenFloorsAdded += Total;
			break;
		default:
			break;
		}
	}
	return Bonuses;
}

FCataclysmEmpireTreeDungeonState UCataclysmEmpireTree::DungeonStateFor(const UCataclysmEmpireRun& Run,
																	   const FCataclysmDungeon& Dungeon,
																	   const TArray<FName>& FloorModifiers,
																	   int32 FloorNumber)
{
	FCataclysmEmpireTreeDungeonState State;
	State.ActiveTypes = Run.ActiveCataclysms.Num();

	// THE DEFAULT WALK IS THE STARTING RATE, ONE FLOOR A DAY, OVER THE FLOORS THE DUNGEON HAS -- the city's and the
	// tree's included, as the #1390 analysis counted "days under default run time" -- and a Cow Level's double, which
	// the surge scheduler sets and nothing reduces. The walk is what the game charges.
	const bool bCowLevel = Dungeon.SubType == ECataclysmDungeonSubType::CowLevel;
	State.DefaultWalkDays = static_cast<float>(FMath::Max(1, Dungeon.Floors))
		* (bCowLevel ? UCataclysmSurgeScheduler::CowLevelWalkMultiplier : 1.0f);
	State.WalkDays = Run.EffectiveWalkDaysFor(Dungeon);

	// THE DEFAULT DEPTH IS THE DEPTH BEFORE CITY UPGRADES AND THE TREE, from fields every kind already saves.
	int32 DefaultFloors = Dungeon.Floors - Dungeon.UpgradeFloors;
	if (Dungeon.Type == ECataclysmDungeonType::FallenCity)
	{
		DefaultFloors = FMath::Max(Dungeon.Bosses,
			UCataclysmSurgeScheduler::SpecFor(ECataclysmDungeonType::FallenCity, Dungeon.CityTier).LeastFloors);
	}
	else if (Dungeon.Type == ECataclysmDungeonType::Cataclysm)
	{
		DefaultFloors = FMath::Max(1, Run.CataclysmDungeonBaseFloors
			+ Run.BasicDungeonsCleared * UCataclysmSurgeScheduler::CataclysmFloorsPerOrdinaryDungeon);
	}
	State.FloorsRemoved = FMath::Max(0, DefaultFloors - Dungeon.Floors);

	// AND THE FLOORS ADDED, THE OTHER SIDE OF THE SAME DEFAULT, which The High Roller counts (slice 5b, D6): the city's
	// and the tree's floors beyond the default depth, net, as `UpgradeFloors` records them.
	State.FloorsAdded = FMath::Max(0, Dungeon.Floors - DefaultFloors);

	TSet<FName> Distinct;
	Distinct.Append(FloorModifiers);
	State.ModifiersActive = Distinct.Num();
	State.FloorsCleared = FMath::Max(0, FloorNumber - 1);
	State.DungeonsCleared = Run.DungeonsCleared;

	// AND THE CLEARS SINCE THE LAST DEATH, which Relentless Pursuit counts (slice 5b, D5).
	State.ClearsSinceLastDeath = Run.ClearsSinceLastDeath;

	// THE FOUR KINDS: three counted where a clear is counted, and the Fallen City clears as what is left over of every
	// clear. `bWon` is the boss's, and a won run is followed by the next run at once.
	const int32 Won = Run.bWon ? 1 : 0;
	const int32 FallenCityClears = Run.DungeonsCleared - Run.BasicDungeonsCleared - Run.QuestObjectives - Won;
	State.KindsDefeated = (Run.BasicDungeonsCleared > 0 ? 1 : 0) + (Run.QuestObjectives > 0 ? 1 : 0)
		+ (FallenCityClears > 0 ? 1 : 0) + Won;

	if (Run.Map)
	{
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			if (City.bFallen)
			{
				continue;
			}
			for (const FCataclysmCityUpgrade& Upgrade : City.Upgrades)
			{
				if (UCataclysmCityUpgradeMapping::BranchFor(Upgrade.RowName) == TEXT("Explorer"))
				{
					++State.ExplorerCities;
					break;
				}
			}
		}
	}
	return State;
}

bool UCataclysmEmpireTree::DoublesExplorerLoot(const FCataclysmEmpireTreePlayerBonuses& Bonuses,
											   const FCataclysmEmpireTreeDungeonState& State)
{
	// "IF RUN TIME REDUCED TO 1-DAY MINIMUM": the walk at its days, from a default that was longer. A dungeon of one
	// floor walks in one day with no reduction at all, and that is not a run time reduced.
	const float Days = static_cast<float>(Bonuses.ExplorerLootDoubledAtWalkDays);
	return Bonuses.ExplorerLootDoubledAtWalkDays > 0
		&& State.WalkDays <= Days + KINDA_SMALL_NUMBER
		&& State.DefaultWalkDays > Days + KINDA_SMALL_NUMBER;
}

float UCataclysmEmpireTree::MagicFindFor(const FCataclysmEmpireTreePlayerBonuses& Bonuses,
										 const FCataclysmEmpireTreeDungeonState& State)
{
	const float Explorer = Bonuses.ExplorerMagicFind
		+ Bonuses.MagicFindPerFloorRemoved * static_cast<float>(State.FloorsRemoved)
		+ Bonuses.MagicFindPerModifierActive * static_cast<float>(State.ModifiersActive)
		+ Bonuses.MagicFindPerFloorCleared * static_cast<float>(State.FloorsCleared)
		+ Bonuses.MagicFindPerFiveClears * static_cast<float>(State.DungeonsCleared / DungeonLegendClearsAStep)
		+ Bonuses.MagicFindPerExplorerCity * static_cast<float>(State.ExplorerCities);
	const float Doubled = DoublesExplorerLoot(Bonuses, State) ? OneDaySpecialistFactor : 1.0f;

	// PERCENTAGE POINTS: a share of 0.05 is 5 magic find.
	return 100.0f * (Bonuses.MagicFind + Explorer * Doubled);
}

float UCataclysmEmpireTree::LootQuantityFor(const FCataclysmEmpireTreePlayerBonuses& Bonuses,
											const FCataclysmEmpireTreeDungeonState& State)
{
	// WHOLE DAYS AND WHOLE STEPS OF TWO DAYS (C3), never fewer than none.
	const int32 DaysRemoved = FMath::Max(0, FMath::FloorToInt(State.DefaultWalkDays - State.WalkDays + KINDA_SMALL_NUMBER));
	float SpeedRunner = Bonuses.LootQuantityPerTwoDaysRemoved * static_cast<float>(DaysRemoved / SpeedRunnerDaysAStep);
	if (Bonuses.TwoDaysRemovedCap > 0.0f)
	{
		SpeedRunner = FMath::Min(SpeedRunner, Bonuses.TwoDaysRemovedCap);
	}
	float EfficiencyPremium = Bonuses.LootQuantityPerDayRemoved * static_cast<float>(DaysRemoved);
	if (Bonuses.DayRemovedCap > 0.0f)
	{
		EfficiencyPremium = FMath::Min(EfficiencyPremium, Bonuses.DayRemovedCap);
	}

	// RELENTLESS PURSUIT, per clear since the last death, to its cap (slice 5b, D5). An Explorer figure, so it is doubled
	// below after its cap, as Speed Runner is.
	float RelentlessPursuit = Bonuses.LootQuantityPerCleanClear * static_cast<float>(State.ClearsSinceLastDeath);
	if (Bonuses.CleanClearCap > 0.0f)
	{
		RelentlessPursuit = FMath::Min(RelentlessPursuit, Bonuses.CleanClearCap);
	}

	const float Explorer = Bonuses.ExplorerLootQuantity
		+ Bonuses.LootQuantityPerActiveType * static_cast<float>(State.ActiveTypes)
		+ SpeedRunner + EfficiencyPremium
		+ Bonuses.LootQuantityPerExplorerCity * static_cast<float>(State.ExplorerCities)
		+ RelentlessPursuit;
	const float Doubled = DoublesExplorerLoot(Bonuses, State) ? OneDaySpecialistFactor : 1.0f;
	return 100.0f * (Bonuses.LootQuantity + Explorer * Doubled);
}

FCataclysmEmpireTreeDropRules UCataclysmEmpireTree::DropRulesFor(const FCataclysmEmpireTreePlayerBonuses& Bonuses,
																 const FCataclysmEmpireTreeDungeonState& State)
{
	// THE THREE EXPLORER CHANCES ARE THE BRANCH'S LOOT FIGURES, so One-Day Specialist doubles them where it doubles the
	// rest (D6); Scavenger is an Artisan node and is not doubled. THE HIGH ROLLER COUNTS WHOLE STEPS OF TEN FLOORS (C3).
	const float Doubled = DoublesExplorerLoot(Bonuses, State) ? OneDaySpecialistFactor : 1.0f;
	const float Steps = static_cast<float>(State.FloorsAdded / HighRollerFloorsAStep);

	// EVERY CHANCE HELD AT ONE: a chance is a share of kills, and no node says a kill drops two.
	FCataclysmEmpireTreeDropRules Rules;
	Rules.EliteExtraItemChance = FMath::Clamp(Bonuses.EliteExtraItemChance * Doubled, 0.0f, 1.0f);
	Rules.ExtraMaterialChance = FMath::Clamp(Bonuses.ExtraMaterialChance * Doubled, 0.0f, 1.0f);
	Rules.LowTierMaterialExtraChance = FMath::Clamp(Bonuses.LowTierMaterialExtraChance, 0.0f, 1.0f);
	Rules.BossLegendaryChance = FMath::Clamp(Bonuses.BossLegendaryChancePerTenFloorsAdded * Steps * Doubled, 0.0f, 1.0f);
	return Rules;
}

float UCataclysmEmpireTree::MovementSpeedFor(const FCataclysmEmpireTreePlayerBonuses& Bonuses)
{
	return 100.0f * Bonuses.MovementSpeed;
}

float UCataclysmEmpireTree::DamageFor(const FCataclysmEmpireTreePlayerBonuses& Bonuses,
									  const FCataclysmEmpireTreeDungeonState& State)
{
	// PER POINT AND PER KIND, NO CAP: the four kinds are its limit (D2).
	return 100.0f * Bonuses.DamagePerKindDefeated * static_cast<float>(State.KindsDefeated);
}

TMap<FName, TArray<FCataclysmStatModifier>> UCataclysmEmpireTree::PlayerStatModifiersFor(
	const FCataclysmEmpireTreePlayerBonuses& Bonuses, const FCataclysmEmpireTreeDungeonState& State)
{
	TMap<FName, TArray<FCataclysmStatModifier>> Modifiers;
	auto Add = [&Modifiers](const TCHAR* Stat, ECataclysmStatBucket Bucket, float Value)
	{
		if (FMath::IsNearlyZero(Value))
		{
			return;
		}
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = Bucket;
		Modifier.Source = ECataclysmModifierSource::EmpireTree;
		Modifier.Value = Value;
		Modifiers.FindOrAdd(FName(Stat)).Add(Modifier);
	};

	Add(TEXT("magic_find"), ECataclysmStatBucket::Flat, MagicFindFor(Bonuses, State));
	Add(TEXT("loot_quantity"), ECataclysmStatBucket::Increased, LootQuantityFor(Bonuses, State));
	Add(TEXT("movement_speed"), ECataclysmStatBucket::Increased, MovementSpeedFor(Bonuses));
	const float Damage = DamageFor(Bonuses, State);
	Add(TEXT("attack_damage"), ECataclysmStatBucket::Increased, Damage);
	Add(TEXT("spell_damage"), ECataclysmStatBucket::Increased, Damage);
	return Modifiers;
}

TArray<FString> UCataclysmEmpireTree::BranchNames()
{
	// THE SOURCE FILE'S OWN ORDER: "Branches: Architect (NE), Explorer (SE), Treasury (SW), Artisan (NW)".
	return {TEXT("Architect"), TEXT("Explorer"), TEXT("Treasury"), TEXT("Artisan"), CentreBranch};
}

const FCataclysmEmpireTreeNodeRow* UCataclysmEmpireTree::FindNode(const UDataTable* NodeTable, FName Node)
{
	if (!NodeTable || Node.IsNone())
	{
		return nullptr;
	}
	return NodeTable->FindRow<FCataclysmEmpireTreeNodeRow>(Node, TEXT("UCataclysmEmpireTree::FindNode"),
														   /*bWarnIfMissing=*/false);
}

FString UCataclysmEmpireTree::BranchOf(const UDataTable* NodeTable, FName Node)
{
	const FCataclysmEmpireTreeNodeRow* Row = FindNode(NodeTable, Node);
	return Row ? Row->Branch : FString();
}

TArray<FName> UCataclysmEmpireTree::NodesIn(const UDataTable* NodeTable, const FString& Branch)
{
	TArray<TPair<FName, const FCataclysmEmpireTreeNodeRow*>> Found;
	CataclysmEmpireTreeTables::ForEachNode(NodeTable, [&](FName Key, const FCataclysmEmpireTreeNodeRow& Row)
	{
		if (Row.Branch == Branch)
		{
			Found.Add({Key, &Row});
		}
	});

	// A DATATABLE KEEPS ITS ROWS IN A TMAP, so without a sort the order would change between imports.
	Found.Sort([](const TPair<FName, const FCataclysmEmpireTreeNodeRow*>& Left,
				  const TPair<FName, const FCataclysmEmpireTreeNodeRow*>& Right)
	{
		if (Left.Value->PositionY != Right.Value->PositionY)
		{
			return Left.Value->PositionY < Right.Value->PositionY;
		}
		if (Left.Value->PositionX != Right.Value->PositionX)
		{
			return Left.Value->PositionX < Right.Value->PositionX;
		}
		return Left.Key.LexicalLess(Right.Key);
	});

	TArray<FName> Out;
	Out.Reserve(Found.Num());
	for (const TPair<FName, const FCataclysmEmpireTreeNodeRow*>& Each : Found)
	{
		Out.Add(Each.Key);
	}
	return Out;
}

int32 UCataclysmEmpireTree::SpentInBranch(const UDataTable* NodeTable, const FCataclysmPassiveAllocation& Allocation,
										  const FString& Branch)
{
	int32 Sum = 0;
	for (const FCataclysmSpentNode& Spent : Allocation.Nodes)
	{
		if (Spent.Points > 0 && BranchOf(NodeTable, Spent.Node) == Branch)
		{
			Sum += Spent.Points;
		}
	}
	return Sum;
}

int32 UCataclysmEmpireTree::WholePointsIn(double Bank)
{
	return FMath::Max(0, static_cast<int32>(FMath::FloorToDouble(Bank + BankNoise)));
}

// ---------------------------------------------------------------------------
// What is open
// ---------------------------------------------------------------------------

bool UCataclysmEmpireTree::EdgesAllow(const UDataTable* NodeTable, const UDataTable* EdgeTable,
									  const FCataclysmPassiveAllocation& Allocation, FName Node)
{
	const FCataclysmEmpireTreeNodeRow* Row = FindNode(NodeTable, Node);
	if (!Row)
	{
		return false;
	}

	if (Row->Kind == CapstoneKind)
	{
		return true;
	}

	const FString Key = Node.ToString();
	bool bAnyEdge = false;
	bool bAnySatisfied = false;
	CataclysmEmpireTreeTables::ForEachEdge(EdgeTable, [&](const FCataclysmEmpireTreeEdgeRow& Edge)
	{
		if (Edge.Target != Key)
		{
			return;
		}

		bAnyEdge = true;
		if (Allocation.PointsIn(FName(*Edge.Source)) >= Edge.RequiredPoints)
		{
			bAnySatisfied = true;
		}
	});

	// NO EDGE MEANS OPEN: twenty nodes start the tree, one or more at the inner edge of each branch. ANY ONE SATISFIED
	// EDGE OPENS IT: nineteen nodes have two, and two edges into one node are two routes to it (C2).
	return !bAnyEdge || bAnySatisfied;
}

bool UCataclysmEmpireTree::CapstoneThreshold(const UDataTable* NodeTable,
											 const FCataclysmPassiveAllocation& Allocation, FName Node,
											 int32& OutThreshold, int32& OutSpent, FString& OutWhere)
{
	const FCataclysmEmpireTreeNodeRow* Row = FindNode(NodeTable, Node);
	if (!Row || Row->Kind != CapstoneKind)
	{
		return false;
	}

	OutThreshold = Row->Threshold;
	if (Row->Branch == CentreBranch)
	{
		OutSpent = SpentInTree(Allocation);
		OutWhere = TEXT("the tree");
	}
	else
	{
		OutSpent = SpentInBranch(NodeTable, Allocation, Row->Branch);
		OutWhere = FString::Printf(TEXT("the %s branch"), *Row->Branch);
	}
	return true;
}

FString UCataclysmEmpireTree::WaitsOn(const UDataTable* NodeTable, FName Node)
{
	const FCataclysmEmpireTreeNodeRow* Row = FindNode(NodeTable, Node);
	return Row ? Row->WaitsOn : FString();
}

FString UCataclysmEmpireTree::OptionWaitsOn(const UDataTable* NodeTable, FName Node, int32 Option)
{
	const FCataclysmEmpireTreeNodeRow* Row = FindNode(NodeTable, Node);
	return Row ? CataclysmEmpireTreeTables::OptionField(*Row, Option, /*bWaitsOn=*/true) : FString();
}

// ---------------------------------------------------------------------------
// Spending
// ---------------------------------------------------------------------------

FString UCataclysmEmpireTree::RefusalForSpending(const UDataTable* NodeTable, const UDataTable* EdgeTable,
												 const FCataclysmPassiveAllocation& Allocation, FName Node,
												 double Bank)
{
	const FCataclysmEmpireTreeNodeRow* Row = FindNode(NodeTable, Node);
	if (!Row)
	{
		return FString::Printf(TEXT("%s is not a node of the empire upgrade tree."), *Node.ToString());
	}

	if (!Row->WaitsOn.IsEmpty())
	{
		return FString::Printf(TEXT("%s cannot be bought yet: it needs %s."), *Row->NodeName, *Row->WaitsOn);
	}

	if (Allocation.PointsIn(Node) >= Row->MaxPoints)
	{
		return FString::Printf(TEXT("%s is full at %d point%s."), *Row->NodeName, Row->MaxPoints,
							   Row->MaxPoints == 1 ? TEXT("") : TEXT("s"));
	}

	if (WholePointsIn(Bank) < 1)
	{
		return FString::Printf(TEXT("No empire upgrade point to spend: %.1f banked, and a node takes whole points."),
							   FMath::FloorToDouble(FMath::Max(0.0, Bank) * 10.0 + BankNoise) / 10.0);
	}

	int32 Threshold = 0;
	int32 Spent = 0;
	FString Where;
	if (CapstoneThreshold(NodeTable, Allocation, Node, Threshold, Spent, Where))
	{
		if (Spent < Threshold)
		{
			return FString::Printf(TEXT("%s opens at %d points spent in %s. %d so far."), *Row->NodeName, Threshold,
								   *Where, Spent);
		}

		if (CataclysmEmpireTreeTables::IsCentreCapstone(*Row) && Allocation.ChosenOptionIn(Node) <= 0)
		{
			return FString::Printf(TEXT("Choose one of %s's three options first."), *Row->NodeName);
		}

		return FString();
	}

	if (!EdgesAllow(NodeTable, EdgeTable, Allocation, Node))
	{
		return FString::Printf(TEXT("%s is shut. Nothing leading to it is invested in far enough."), *Row->NodeName);
	}

	return FString();
}

bool UCataclysmEmpireTree::Spend(const UDataTable* NodeTable, const UDataTable* EdgeTable,
								 FCataclysmPassiveAllocation& Allocation, FName Node, double& Bank,
								 FString& OutReason)
{
	OutReason = RefusalForSpending(NodeTable, EdgeTable, Allocation, Node, Bank);
	if (!OutReason.IsEmpty())
	{
		return false;
	}

	Allocation.Add(Node, 1);
	Bank = FMath::Max(0.0, Bank - PointCost);
	return true;
}

// ---------------------------------------------------------------------------
// The centre capstones' choices
// ---------------------------------------------------------------------------

TArray<FString> UCataclysmEmpireTree::OptionNamesOf(const FCataclysmEmpireTreeNodeRow& Row)
{
	return {Row.Option1Name, Row.Option2Name, Row.Option3Name};
}

FString UCataclysmEmpireTree::RefusalForChoosingOption(const UDataTable* NodeTable,
													   const FCataclysmPassiveAllocation& Allocation, FName Node,
													   int32 Option)
{
	const FCataclysmEmpireTreeNodeRow* Row = FindNode(NodeTable, Node);
	if (!Row)
	{
		return FString::Printf(TEXT("%s is not a node of the empire upgrade tree."), *Node.ToString());
	}

	if (!CataclysmEmpireTreeTables::IsCentreCapstone(*Row))
	{
		return FString::Printf(TEXT("%s offers no choice."), *Row->NodeName);
	}

	const FString& Name = CataclysmEmpireTreeTables::OptionField(*Row, Option, /*bWaitsOn=*/false);
	if (Name.IsEmpty())
	{
		return FString::Printf(TEXT("Choose 1, 2 or 3, not %d."), Option);
	}

	const FString& Needs = CataclysmEmpireTreeTables::OptionField(*Row, Option, /*bWaitsOn=*/true);
	if (!Needs.IsEmpty())
	{
		return FString::Printf(TEXT("%s cannot be chosen yet: it needs %s."), *Name, *Needs);
	}

	// A CHOICE STANDS UNTIL A RESPEC, which clears all four (the coordinating session's Q3). Choosing again would be a
	// respec of one capstone for nothing.
	const int32 Already = Allocation.ChosenOptionIn(Node);
	if (Already > 0 && Already != Option)
	{
		return FString::Printf(TEXT("%s is already committed to %s. A respec of the empire tree frees the choice."),
							   *Row->NodeName, *CataclysmEmpireTreeTables::OptionField(*Row, Already, /*bWaitsOn=*/false));
	}

	// THE THRESHOLD IS ASKED HERE TOO, so a choice cannot run ahead of the tier: otherwise all four could be decided at
	// the first point and spent into later with nothing left to decide.
	int32 Threshold = 0;
	int32 Spent = 0;
	FString Where;
	CapstoneThreshold(NodeTable, Allocation, Node, Threshold, Spent, Where);
	if (Spent < Threshold)
	{
		return FString::Printf(TEXT("%s opens at %d points spent in %s. %d so far."), *Row->NodeName, Threshold, *Where,
							   Spent);
	}

	return FString();
}

bool UCataclysmEmpireTree::ChooseOption(const UDataTable* NodeTable, FCataclysmPassiveAllocation& Allocation,
										FName Node, int32 Option, FString& OutReason)
{
	OutReason = RefusalForChoosingOption(NodeTable, Allocation, Node, Option);
	if (!OutReason.IsEmpty())
	{
		return false;
	}

	Allocation.SetChosenOption(Node, Option);
	return true;
}

bool UCataclysmEmpireTree::AwaitsAnOptionChoice(const UDataTable* NodeTable,
												const FCataclysmPassiveAllocation& Allocation, FName Node)
{
	const FCataclysmEmpireTreeNodeRow* Row = FindNode(NodeTable, Node);
	if (!Row || !CataclysmEmpireTreeTables::IsCentreCapstone(*Row) || Allocation.ChosenOptionIn(Node) > 0)
	{
		return false;
	}

	int32 Threshold = 0;
	int32 Spent = 0;
	FString Where;
	CapstoneThreshold(NodeTable, Allocation, Node, Threshold, Spent, Where);
	if (Spent < Threshold)
	{
		return false;
	}

	for (int32 Option = 1; Option <= CapstoneOptions; ++Option)
	{
		if (RefusalForChoosingOption(NodeTable, Allocation, Node, Option).IsEmpty())
		{
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Respec
// ---------------------------------------------------------------------------

int32 UCataclysmEmpireTree::RespecDays(int32 LethalityRung)
{
	return UCataclysmDayClock::DeathDayCostFor(LethalityRung);
}

FString UCataclysmEmpireTree::RefusalForRespec(const FCataclysmPassiveAllocation& Allocation, bool bRunBeingPlayed)
{
	if (Allocation.Nodes.Num() == 0)
	{
		return TEXT("Nothing is spent or chosen in the empire upgrade tree, so there is nothing to respec.");
	}

	if (!bRunBeingPlayed)
	{
		return TEXT("A respec of the empire upgrade tree costs days of a run, and no run is being played.");
	}

	return FString();
}

int32 UCataclysmEmpireTree::Respec(FCataclysmPassiveAllocation& Allocation, double& Bank, bool bRunBeingPlayed,
								   FString& OutReason)
{
	OutReason = RefusalForRespec(Allocation, bRunBeingPlayed);
	if (!OutReason.IsEmpty())
	{
		return 0;
	}

	const int32 Returned = Allocation.Total();
	Bank += static_cast<double>(Returned) * PointCost;
	Allocation.Clear();
	return Returned;
}
