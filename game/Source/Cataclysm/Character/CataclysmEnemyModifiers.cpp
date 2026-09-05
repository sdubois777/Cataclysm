// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmEnemyModifiers.h"

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Cataclysm.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"

const TCHAR* UCataclysmEnemyModifiers::GenericCataclysm = TEXT("Generic");

const TCHAR* UCataclysmEnemyModifiers::TitanicResolveRow =
	TEXT("Generic_Titanic_Resolve");
const TCHAR* UCataclysmEnemyModifiers::OverpoweredRow =
	TEXT("Generic_Overpowered");
const TCHAR* UCataclysmEnemyModifiers::BloodthirstyRow =
	TEXT("Generic_Bloodthirsty");
const TCHAR* UCataclysmEnemyModifiers::ThornsOfGlassRow =
	TEXT("Generic_Thorns_of_Glass");
const TCHAR* UCataclysmEnemyModifiers::HellfireAuraRow =
	TEXT("Demonic_Hellfire_Aura");

const UDataTable* UCataclysmEnemyModifiers::LoadEnemyModifierTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(
		nullptr, TEXT("/Game/Data/DT_EnemyModifiers.DT_EnemyModifiers"));
	if (!Table)
	{
		// NAMES BOTH SCRIPTS, because the two failures look the same from here:
		// the workbook never produced the CSV, or the CSV was never imported as
		// an asset. Every other loader in this project says the same thing.
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load DT_EnemyModifiers. It is produced by "
				 "tools/generate_datatable_assets.py from "
				 "game/Data/EnemyModifiers.csv, which "
				 "tools/generate_datatables.py produces from the design "
				 "workbook."));
	}
	return Table;
}

int32 UCataclysmEnemyModifiers::CountForRarityStep(int32 RarityStep)
{
	// THE STEP ITSELF. `docs/Cataclysm_GDD_v2.md` gives 0, 1, 2, 3, 4, 5 for
	// Common through Cataclysm Boss and `game/Data/EnemyRarities.csv` numbers
	// those same rungs 0 to 5, so the two ladders are one ladder.
	//
	// NOT CAPPED AT FIVE HERE. A step above the table's own is a caller error
	// rather than a rung, and `ACataclysmEnemyCharacter::SetRarityStep` is where
	// a step is bounded. Capping here as well would hide that.
	return FMath::Max(0, RarityStep);
}

bool UCataclysmEnemyModifiers::IsDrawableBy(
	const FCataclysmEnemyModifierRow& Row, FName CataclysmType)
{
	// THE WHOLE POOL RULE, IN ONE PLACE. `docs/Cataclysm_GDD_v2.md`: an enemy's
	// modifiers are "drawn from its own Cataclysm's column and the Generic one".
	return Row.CataclysmType.Equals(CataclysmType.ToString(),
									ESearchCase::IgnoreCase)
		|| Row.CataclysmType.Equals(GenericCataclysm, ESearchCase::IgnoreCase);
}

TArray<FName> UCataclysmEnemyModifiers::PoolFor(
	const UDataTable* EnemyModifierTable, FName CataclysmType)
{
	TArray<FName> Pool;
	if (!EnemyModifierTable)
	{
		return Pool;
	}

	EnemyModifierTable->ForeachRow<FCataclysmEnemyModifierRow>(
		TEXT("UCataclysmEnemyModifiers::PoolFor"),
		[&Pool, CataclysmType](const FName& Key,
							   const FCataclysmEnemyModifierRow& Row)
		{
			if (IsDrawableBy(Row, CataclysmType))
			{
				Pool.Add(Key);
			}
		});

	// SORTED, BECAUSE A DataTable IS A MAP. See the header: an unsorted walk
	// would hand the same seed a different modifier on a different run, which is
	// the fault `UCataclysmEnemyRarity::SpawnableSteps` sorts to avoid.
	Pool.Sort([](const FName& A, const FName& B)
	{
		return A.LexicalLess(B);
	});

	return Pool;
}

TArray<FName> UCataclysmEnemyModifiers::Draw(
	const UDataTable* EnemyModifierTable, FName CataclysmType, int32 Count,
	FRandomStream& Stream, const TArray<FName>& Already)
{
	TArray<FName> Drawn;
	if (Count <= 0)
	{
		return Drawn;
	}

	TArray<FName> Pool = PoolFor(EnemyModifierTable, CataclysmType);

	// WHAT THE CREATURE ALREADY HAS IS OUT OF THE POOL BEFORE ANYTHING IS
	// DRAWN, rather than being drawn and rejected. Rejecting would make the
	// number of draws depend on what was already carried, so the same seed on
	// two creatures that had been given different starting lists would produce
	// unrelated results for the rest of the draw.
	Pool.RemoveAll([&Already](const FName& Key)
	{
		return Already.Contains(Key);
	});

	while (Drawn.Num() < Count && Pool.Num() > 0)
	{
		const int32 Index = Stream.RandRange(0, Pool.Num() - 1);
		Drawn.Add(Pool[Index]);

		// REMOVED SO IT CANNOT COME UP AGAIN. `RemoveAtSwap` would be cheaper
		// and would reorder what is left, which makes the draw depend on the
		// order things were removed in as well as on the stream.
		Pool.RemoveAt(Index);
	}

	return Drawn;
}

const FCataclysmEnemyModifierRow* UCataclysmEnemyModifiers::FindRow(
	const UDataTable* EnemyModifierTable, FName Key)
{
	if (!EnemyModifierTable || Key.IsNone())
	{
		return nullptr;
	}

	return EnemyModifierTable->FindRow<FCataclysmEnemyModifierRow>(
		Key, TEXT("UCataclysmEnemyModifiers::FindRow"), /*bWarnIfMissing=*/false);
}

// ---------------------------------------------------------------------------
// What the carried modifiers do
// ---------------------------------------------------------------------------

bool UCataclysmEnemyModifiers::Carries(const TArray<FName>& Rows,
									   const TCHAR* RowKey)
{
	return RowKey != nullptr && Rows.Contains(FName(RowKey));
}

float UCataclysmEnemyModifiers::MaxHealthMultiplier(const TArray<FName>& Rows)
{
	// ONE MODIFIER MOVES THIS TODAY. Written as a running multiplier rather
	// than as a branch, so a second one that also scales health multiplies with
	// it rather than replacing it.
	float Multiplier = 1.0f;

	if (Carries(Rows, TitanicResolveRow))
	{
		Multiplier *= TitanicResolveHealthMultiplier;
	}

	return Multiplier;
}

float UCataclysmEnemyModifiers::ForcedCritChance(const TArray<FName>& Rows)
{
	if (Carries(Rows, OverpoweredRow))
	{
		return OverpoweredCritChance;
	}

	return -1.0f;
}

float UCataclysmEnemyModifiers::LifeLeechPercent(const TArray<FName>& Rows)
{
	// A SUM, because two sources of leech should add rather than one winning.
	float Percent = 0.0f;

	if (Carries(Rows, BloodthirstyRow))
	{
		Percent += BloodthirstyLeechPercent;
	}

	return Percent;
}

float UCataclysmEnemyModifiers::RetaliationPercent(const TArray<FName>& Rows)
{
	float Percent = 0.0f;

	if (Carries(Rows, ThornsOfGlassRow))
	{
		Percent += ThornsOfGlassRetaliationPercent;
	}

	return Percent;
}

int32 UCataclysmEnemyModifiers::AuraStep(AActor* Character, float StepSeconds)
{
	ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Character);
	if (Enemy == nullptr || Enemy->ModifierRows.IsEmpty())
	{
		// THE PLAYER COMES THROUGH HERE EVERY STEP, because the step this hangs
		// off is on the shared character base. So does every Common creature.
		return 0;
	}

	if (!Carries(Enemy->ModifierRows, HellfireAuraRow))
	{
		return 0;
	}

	// A DEAD CREATURE BURNS NOBODY. The regeneration timer is cleared on death,
	// but a step already in flight would otherwise land afterwards, which is the
	// guard `ACataclysmSuccubusCharacter::PulseDominion` carries for the same
	// reason.
	if (UCataclysmSkillEffects::IsDead(Enemy))
	{
		return 0;
	}

	Enemy->SecondsSinceAuraPulse += StepSeconds;
	if (!AuraPulseIsDue(Enemy->SecondsSinceAuraPulse))
	{
		return 0;
	}
	Enemy->SecondsSinceAuraPulse = 0.0f;

	const UWorld* World = Enemy->GetWorld();
	if (World == nullptr)
	{
		return 0;
	}

	// EVERYTHING THIS CREATURE COUNTS AS AN ENEMY, which from a creature's point
	// of view is the player. `FindEnemiesInSphere` is the same search every
	// skill in the game uses, so a target that is out of reach of a swing is out
	// of reach of this too.
	const TArray<AActor*> Caught = UCataclysmTargeting::FindEnemiesInSphere(
		World, Enemy, Enemy->GetActorLocation(), AuraRadiusCm);

	int32 Burned = 0;
	for (AActor* Target : Caught)
	{
		// THE BURN IS DESIGNED, which is what the parameter means: "true when
		// the skill's own row states it burns". Hellfire Aura's row is "Emits a
		// burning aura that deals constant fire damage to nearby players", and
		// `DoT_Burn`'s own row names this modifier as one of the two that apply
		// it. So there is no hit to measure a threshold against and none is
		// wanted -- an aura that only caught a player who had just been hit hard
		// would not be an aura.
		if (UCataclysmSkillEffects::ApplyBurn(Enemy, Target, /*HitDamage=*/0.0f,
											  /*bScalesWithInstigator=*/true,
											  /*bBurnIsDesigned=*/true))
		{
			++Burned;
		}
	}

	return Burned;
}

bool UCataclysmEnemyModifiers::AuraPulseIsDue(float SecondsSinceLastPulse)
{
	// AT OR PAST THE INTERVAL, not strictly past it. The per-character step runs
	// at a fixed 0.25 seconds and the interval is a whole second, so the
	// accumulated figure lands exactly on 1.0 rather than near it, and a strict
	// comparison would make every pulse wait an extra step.
	return SecondsSinceLastPulse >= AuraPulseIntervalSeconds;
}
