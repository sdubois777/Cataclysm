// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmEnemyStrength.h"
// For a summoned Imp's figures at a level, which a thrall's are a multiple of. Issue #2868.
#include "AbilitySystem/CataclysmMinion.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

const UDataTable* UCataclysmEnemyStrength::LoadEnemyArchetypeTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(
		nullptr, TEXT("/Game/Data/DT_EnemyArchetypes.DT_EnemyArchetypes"));
	if (!Table)
	{
		// NAMES BOTH SCRIPTS, because the two failures look the same from here:
		// the model never produced the CSV, or the CSV was never imported as an
		// asset. Every other loader in this project says the same thing.
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load DT_EnemyArchetypes. It is produced by "
				 "tools/generate_datatable_assets.py from "
				 "game/Data/EnemyArchetypes.csv, which "
				 "tools/generate_datatables.py produces from the design "
				 "workbook."));
	}
	return Table;
}

bool UCataclysmEnemyStrength::StartingFiguresFor(const UDataTable* EnemyRarityTable,
												  const UDataTable* EnemyArchetypeTable,
												  FName ArchetypeRow,
												  const FCataclysmScoredFloor& Floor,
												  int32 RarityStep,
												  FCataclysmStartingFigures& Out)
{
	if (!EnemyRarityTable || !EnemyArchetypeTable || ArchetypeRow.IsNone())
	{
		return false;
	}

	// COMMON IS FOUND BY ITS STEP AND NOT BY ITS NAME, as `UCataclysmEnemyRarity::ScalingFromCommon` finds it: step 0
	// is what the table means by the bottom rung. The creature then multiplies these by that function's ratio.
	const FCataclysmEnemyRarityRow* Common = nullptr;
	EnemyRarityTable->ForeachRow<FCataclysmEnemyRarityRow>(
		TEXT("UCataclysmEnemyStrength::StartingFiguresFor"),
		[&Common](const FName&, const FCataclysmEnemyRarityRow& Row)
		{
			if (Row.Step == 0)
			{
				Common = &Row;
			}
		});

	const FCataclysmEnemyArchetypeRow* Kind = EnemyArchetypeTable->FindRow<FCataclysmEnemyArchetypeRow>(
		ArchetypeRow, TEXT("UCataclysmEnemyStrength::StartingFiguresFor"), false);
	if (!Common || !Kind)
	{
		return false;
	}

	const double Strength = UCataclysmEnemyScore::StrengthScoreFor(Floor, RarityStep);

	// AT LEAST ONE HEALTH, as the model's `stats_for` holds it. The minimum strength keeps every creature far above it
	// -- a Common Imp on the first floor of tier 1 has 4.2 -- so this only stops a nonsense table building a corpse.
	Out.Health = FMath::Max(1.0f, static_cast<float>(Strength * Common->HealthPerScore * Kind->HealthShare));
	// THE RING MOVES THE DAMAGE ALONE, at tier 1 (issue #2749's ring slice): health and armour are the same on every
	// ring, and so is the strength they are built from.
	Out.AttackDamage = static_cast<float>(Strength * Common->DamagePerScore * Kind->DamageShare
		* UCataclysmEnemyScore::DamageScale(Floor.DifficultyTier, Floor.CityTier));
	Out.Armour = static_cast<float>(Strength * Common->ArmorPerScore * Kind->ArmorShare);
	return true;
}

const TCHAR* UCataclysmEnemyStrength::ThrallMeasuredAgainst = TEXT("Imp");

bool UCataclysmEnemyStrength::ThrallFiguresFor(const UDataTable* EnemyRarityTable,
												const UDataTable* EnemyArchetypeTable,
												const UDataTable* MinionTypeTable,
												FName ArchetypeRow,
												int32 CommanderLevel,
												FCataclysmStartingFigures& Out)
{
	if (!EnemyRarityTable || !EnemyArchetypeTable || !MinionTypeTable || ArchetypeRow.IsNone())
	{
		return false;
	}

	// COMMON BY ITS STEP, as `StartingFiguresFor` finds it. Only its armour-to-health proportion is read here.
	const FCataclysmEnemyRarityRow* Common = nullptr;
	EnemyRarityTable->ForeachRow<FCataclysmEnemyRarityRow>(
		TEXT("UCataclysmEnemyStrength::ThrallFiguresFor"),
		[&Common](const FName&, const FCataclysmEnemyRarityRow& Row)
		{
			if (Row.Step == 0)
			{
				Common = &Row;
			}
		});

	const FName MeasuredAgainst(ThrallMeasuredAgainst);
	const FCataclysmEnemyArchetypeRow* Kind = EnemyArchetypeTable->FindRow<FCataclysmEnemyArchetypeRow>(
		ArchetypeRow, TEXT("UCataclysmEnemyStrength::ThrallFiguresFor"), false);
	const FCataclysmEnemyArchetypeRow* ImpKind = EnemyArchetypeTable->FindRow<FCataclysmEnemyArchetypeRow>(
		MeasuredAgainst, TEXT("UCataclysmEnemyStrength::ThrallFiguresFor"), false);
	const FCataclysmMinionTypeRow* SummonedImp = MinionTypeTable->FindRow<FCataclysmMinionTypeRow>(
		MeasuredAgainst, TEXT("UCataclysmEnemyStrength::ThrallFiguresFor"), false);
	if (!Common || !Kind || !ImpKind || !SummonedImp || ImpKind->HealthShare <= 0.0f || ImpKind->DamageShare <= 0.0f)
	{
		return false;
	}

	// WHAT A SUMMONED IMP HAS AT THE COMMANDER'S LEVEL, read as `ACataclysmMinion::Spawn` reads it.
	float ImpHealth = 0.0f;
	float ImpDamage = 0.0f;
	ACataclysmMinion::TypeFiguresAtLevel(*SummonedImp, CommanderLevel, ImpHealth, ImpDamage);

	// THE KIND'S MULTIPLE OF AN IMP, health and damage apart: the ratio of the shares the design gives the two kinds.
	// AT LEAST ONE HEALTH, as `StartingFiguresFor` holds it.
	Out.Health = FMath::Max(1.0f, ImpHealth * Kind->HealthShare / ImpKind->HealthShare);
	Out.AttackDamage = FMath::Max(0.0f, ImpDamage * Kind->DamageShare / ImpKind->DamageShare);

	// ARMOUR IN THE PROPORTION TO HEALTH THE KIND HAS ON A FLOOR. See the header. A kind stating no health share, or
	// a Common stating no health per score, has no proportion to keep, and no armour.
	const float HealthPerShare = Common->HealthPerScore * Kind->HealthShare;
	Out.Armour = HealthPerShare > 0.0f
		? FMath::Max(0.0f, Out.Health * Common->ArmorPerScore * Kind->ArmorShare / HealthPerShare)
		: 0.0f;
	return true;
}
