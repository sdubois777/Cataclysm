// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmPlayerClassStats.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmClassStats.h"
#include "Engine/DataTable.h"
#include "HAL/IConsoleManager.h"

const TCHAR* UCataclysmPlayerClassStats::ClassStatsAssetPath =
	TEXT("/Game/Data/DT_ClassStats.DT_ClassStats");

namespace
{
	/**
	 * Which class stat line the player starts on.
	 *
	 * A STRING RATHER THAN AN INDEX, because the row names in
	 * game/Data/ClassStats.csv are what UCataclysmClassStats::BaseFor takes and
	 * an index would be a second ordering that could disagree with the table.
	 * `Default` is the shared line every class inherits from, and it is a
	 * legitimate answer rather than a placeholder: there is no class selection
	 * screen, so a character that has chosen nothing sits on it.
	 */
	TAutoConsoleVariable<FString> CVarPlayerClass(
		TEXT("Cataclysm.PlayerClass"),
		TEXT("Default"),
		TEXT("Which row of game/Data/ClassStats.csv the player starts on: "
			 "Default, Ravager, Ritualist or Masochist. Read when the pawn is "
			 "possessed, so press Play again after changing it."),
		ECVF_Default);

	/**
	 * Which level the player's stat line is resolved at.
	 *
	 * IT IS A STAND-IN FOR A LEVELLING SYSTEM THAT DOES NOT EXIST. Nothing in
	 * this project grants experience or raises a level, so without this the
	 * PerLevel column of the class table would never be read by the game at all.
	 */
	TAutoConsoleVariable<int32> CVarPlayerLevel(
		TEXT("Cataclysm.PlayerLevel"),
		UCataclysmPlayerClassStats::DefaultLevel,
		TEXT("The level the player's class stat line is resolved at, 1 to 100. "
			 "A stand-in until levelling exists. Read when the pawn is "
			 "possessed, so press Play again after changing it."),
		ECVF_Default);

	/**
	 * Kept alive deliberately, the same way the damage type colour table is.
	 * Nothing else references the asset, so garbage collection would otherwise
	 * be free to take it back and the next possession would pay the load again.
	 */
	TWeakObjectPtr<const UDataTable> CachedClassStats;
}

const UDataTable* UCataclysmPlayerClassStats::LoadTable()
{
	if (CachedClassStats.IsValid())
	{
		return CachedClassStats.Get();
	}

	const UDataTable* Table = LoadObject<UDataTable>(nullptr, ClassStatsAssetPath);
	if (Table)
	{
		const_cast<UDataTable*>(Table)->AddToRoot();
		CachedClassStats = Table;
	}
	return Table;
}

const TMap<FString, FGameplayAttribute>&
UCataclysmPlayerClassStats::StatToAttribute()
{
	// BUILT ONCE ON FIRST USE RATHER THAN AS A FILE-SCOPE STATIC. An
	// FGameplayAttribute wraps an FProperty found by reflection, and the
	// reflection data for these attribute sets is not ready during static
	// initialisation.
	static const TMap<FString, FGameplayAttribute> Map = []
	{
		using Vital = UCataclysmVitalAttributeSet;
		using Combat = UCataclysmCombatAttributeSet;
		using Resource = UCataclysmClassResourceAttributeSet;

		return TMap<FString, FGameplayAttribute>{
			// The pools and what refills them.
			{TEXT("max_health"), Vital::GetMaxHealthAttribute()},
			{TEXT("max_mana"), Vital::GetMaxManaAttribute()},
			{TEXT("max_energy_shield"), Vital::GetMaxEnergyShieldAttribute()},
			{TEXT("health_regen"), Vital::GetHealthRegenAttribute()},
			{TEXT("mana_regen"), Vital::GetManaRegenAttribute()},
			{TEXT("energy_shield_regen"), Vital::GetEnergyShieldRegenAttribute()},
			{TEXT("life_leech"), Vital::GetLifeLeechAttribute()},

			// The class's own resource. Its maximum only -- see the header.
			{TEXT("class_resource"), Resource::GetMaxClassResourceAttribute()},

			// What keeps a hit from landing in full.
			{TEXT("armor"), Combat::GetArmorAttribute()},
			{TEXT("damage_reduction"), Combat::GetDamageReductionAttribute()},
			{TEXT("crowd_control_resistance"),
			 Combat::GetCrowdControlResistanceAttribute()},
			{TEXT("retaliation"), Combat::GetRetaliationAttribute()},

			// What a hit is worth.
			{TEXT("crit_multiplier"), Combat::GetCritMultiplierAttribute()},
			{TEXT("spell_damage"), Combat::GetSpellDamageAttribute()},
			{TEXT("area_of_effect"), Combat::GetAreaOfEffectAttribute()},
			{TEXT("dot_damage"), Combat::GetDotDamageAttribute()},
			{TEXT("dot_frequency"), Combat::GetDotFrequencyAttribute()},
			{TEXT("dot_duration"), Combat::GetDotDurationAttribute()},

			// Everything else the class table names.
			{TEXT("movement_speed"), Combat::GetMovementSpeedAttribute()},
			{TEXT("loot_quantity"), Combat::GetLootQuantityAttribute()},
		};
	}();

	return Map;
}

FString UCataclysmPlayerClassStats::ChosenClass()
{
	const FString Asked = CVarPlayerClass.GetValueOnGameThread();
	return Asked.IsEmpty() ? UCataclysmClassStats::DefaultClassName : Asked;
}

int32 UCataclysmPlayerClassStats::ChosenLevel()
{
	// CLAMPED RATHER THAN REFUSED, because this is a console variable a person
	// types at and a nonsensical one should still leave a playable character.
	// Level 0 would resolve every per-level term to minus one level's worth.
	return FMath::Clamp(CVarPlayerLevel.GetValueOnGameThread(), 1,
						UCataclysmClassStats::MaxLevel);
}

int32 UCataclysmPlayerClassStats::ApplyTo(
	UAbilitySystemComponent* AbilitySystem,
	const UDataTable* ClassTable,
	const FString& ClassName, int32 Level,
	const TMap<FName, TArray<FCataclysmStatModifier>>* Modifiers,
	ECataclysmPoolFill PoolFill)
{
	if (!AbilitySystem || !ClassTable)
	{
		return 0;
	}

	int32 Written = 0;

	for (const TPair<FString, FGameplayAttribute>& Pair : StatToAttribute())
	{
		// THE CHECK IS NOT OPTIONAL. Writing to an attribute whose set the
		// component does not hold raises an engine ensure rather than failing
		// quietly. The player's component holds all three of these sets, but a
		// test may build one that does not.
		if (!AbilitySystem->HasAttributeSetForAttribute(Pair.Value))
		{
			continue;
		}

		// BaseFor falls back to the shared Default line when the class does not
		// name the stat, and to zero when neither does. Zero is the right answer
		// there and not a failure: most classes leave most stats alone, and a
		// Ritualist really does have no armour.
		const float Base = UCataclysmClassStats::BaseFor(
			ClassTable, ClassName, Pair.Key, Level);

		// THE THREE-BUCKET PIPELINE, NOT ADDITION. A gear affix can be flat or
		// increased, and the design's whole damage model is
		// (base + flat) x (1 + increases) x more, so adding the numbers up here
		// would give a different answer from the one every skill already uses.
		//
		// AN EMPTY TAG CONTAINER, AND THAT IS THE POINT OF PASSING ONE. A stat
		// on the character sheet has no skill in hand, so a modifier scoped to
		// something -- "increased damage against Demons" -- must not apply.
		// UCataclysmStatPipeline::ModifierApplies is what enforces that; only
		// globally scoped modifiers survive an empty container.
		float Value = Base;
		if (Modifiers)
		{
			if (const TArray<FCataclysmStatModifier>* ForStat =
					Modifiers->Find(FName(*Pair.Key)))
			{
				Value = UCataclysmStatPipeline::Evaluate(
					Base, *ForStat, FGameplayTagContainer()).Final;
			}
		}

		AbilitySystem->SetNumericAttributeBase(Pair.Value, Value);
		++Written;
	}

	// A CHARACTER ALREADY IN PLAY KEEPS ITS POOLS WHERE THEY ARE. Filling
	// them below is right for a character arriving in the world and is a free
	// heal for one that only changed a helmet. See ECataclysmPoolFill.
	if (PoolFill == ECataclysmPoolFill::LeaveAsTheyAre)
	{
		return Written;
	}

	// THE POOLS COME LAST, AND THAT IS THE WHOLE REASON THIS IS TWO STEPS.
	// UCataclysmVitalAttributeSet clamps current health to maximum health, so a
	// character whose current health was filled before its maximum was raised
	// would be clamped straight back to the placeholder 100 and none of the
	// above would show on screen.
	const TPair<FGameplayAttribute, FGameplayAttribute> Pools[] = {
		{UCataclysmVitalAttributeSet::GetHealthAttribute(),
		 UCataclysmVitalAttributeSet::GetMaxHealthAttribute()},
		{UCataclysmVitalAttributeSet::GetManaAttribute(),
		 UCataclysmVitalAttributeSet::GetMaxManaAttribute()},
		{UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(),
		 UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()},
	};

	for (const TPair<FGameplayAttribute, FGameplayAttribute>& Pool : Pools)
	{
		if (!AbilitySystem->HasAttributeSetForAttribute(Pool.Key))
		{
			continue;
		}

		AbilitySystem->SetNumericAttributeBase(
			Pool.Key, AbilitySystem->GetNumericAttribute(Pool.Value));
	}

	return Written;
}
