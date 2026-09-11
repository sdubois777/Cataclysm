// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmDungeonModifierEffects.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "Dungeon/CataclysmFloorBrief.h"
#include "Items/CataclysmEquipmentComponent.h"

const TCHAR* UCataclysmDungeonModifierEffects::StarvationKey = TEXT("Famine_Starvation");
const TCHAR* UCataclysmDungeonModifierEffects::DehydrationKey = TEXT("Famine_Dehydration");

namespace
{
	/**
	 * The character-sheet stat names the three maximums are written under.
	 *
	 * THE SAME SPELLINGS `UCataclysmPlayerClassStats::StatToAttribute` USES, and
	 * they have to be: a modifier keyed by a name that map does not hold is not
	 * written anywhere, silently. `Cataclysm.DungeonModifierEffects.Starvation
	 * LowersThePlayersMaximumsAndLeavingGivesThemBack` is what fails if either
	 * side is renamed.
	 *
	 * NAMED FOR THIS FILE, because Unreal merges a module's `.cpp` files into one
	 * translation unit and `tools/tests/test_no_two_files_share_an_anonymous_helper.py`
	 * fails when two files declare the same helper.
	 */
	const TCHAR* const DungeonModifierEffectsMaxHealthStat = TEXT("max_health");
	const TCHAR* const DungeonModifierEffectsMaxEnergyShieldStat = TEXT("max_energy_shield");
	const TCHAR* const DungeonModifierEffectsMaxManaStat = TEXT("max_mana");

	/** One Less multiplier from a dungeon rule, or nothing for a share of zero. */
	void DungeonModifierEffectsAddLess(
		TMap<FName, TArray<FCataclysmStatModifier>>& Into, const TCHAR* Stat,
		float LessPercent)
	{
		if (LessPercent <= 0.0f)
		{
			return;
		}

		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::More;
		Modifier.Source = ECataclysmModifierSource::DungeonRule;

		// NEGATIVE, WHICH IS WHAT MAKES A MORE A LESS. The pipeline multiplies by
		// (1 + Value / 100), so -10 is x0.9, and it floors a Less at -99 so no
		// rule can take a maximum to nothing. 60 is the most either rule here
		// takes, well inside that.
		Modifier.Value = -LessPercent;

		Into.FindOrAdd(FName(Stat)).Add(Modifier);
	}
}

ECataclysmModifierBuilt UCataclysmDungeonModifierEffects::BuiltStateOf(FName RowKey)
{
	if (RowKey == FName(StarvationKey) || RowKey == FName(DehydrationKey))
	{
		return ECataclysmModifierBuilt::Built;
	}

	// PARTLY. Its rule draws another dungeon modifier onto the floor, where the
	// row asks for "a new, random modifier to all enemies on the next floor", and
	// it adds that modifier on floor 1 as well, where no floor has been cleared.
	// Question 3 of the modifier plan asks the owner which it should draw.
	if (RowKey == FName(FCataclysmDungeonFloorRules::UnstableDimensionsKey))
	{
		return ECataclysmModifierBuilt::Partly;
	}

	return ECataclysmModifierBuilt::NotBuilt;
}

TArray<FName> UCataclysmDungeonModifierEffects::KeysWithARule()
{
	return {
		FName(StarvationKey),
		FName(DehydrationKey),
		FName(FCataclysmDungeonFloorRules::UnstableDimensionsKey),
	};
}

float UCataclysmDungeonModifierEffects::ShareTakenOnFloor(float PercentPerFloor,
														  float MostPercent,
														  int32 FloorNumber)
{
	if (FloorNumber <= 0 || PercentPerFloor <= 0.0f || MostPercent <= 0.0f)
	{
		return 0.0f;
	}

	return FMath::Min(PercentPerFloor * static_cast<float>(FloorNumber), MostPercent);
}

FCataclysmPlayerFloorEffects UCataclysmDungeonModifierEffects::PlayerEffectsFor(
	const TArray<FName>& FloorModifiers, int32 FloorNumber)
{
	FCataclysmPlayerFloorEffects Effects;

	// STARVATION: HEALTH AND SHIELD TOGETHER, by the same share. The row names
	// both in one sentence and gives them one number.
	if (FloorModifiers.Contains(FName(StarvationKey)))
	{
		const float Share = ShareTakenOnFloor(
			StarvationPercentPerFloor, StarvationMostPercent, FloorNumber);
		Effects.MaxHealthLessPercent = Share;
		Effects.MaxEnergyShieldLessPercent = Share;
	}

	// DEHYDRATION: "MAXIMUM RESOURCE", READ AS MAXIMUM MANA. Mana is what every
	// class's skills are paid from (`UCataclysmGameplayAbility::CheckCost`); a
	// class resource such as Fervour is built up in a fight and starts empty, so
	// taking a share of its maximum would take nothing a player had. A judgement,
	// recorded in `docs/DECISIONS.md`.
	if (FloorModifiers.Contains(FName(DehydrationKey)))
	{
		Effects.MaxManaLessPercent = ShareTakenOnFloor(
			DehydrationPercentPerFloor, DehydrationMostPercent, FloorNumber);
	}

	return Effects;
}

TMap<FName, TArray<FCataclysmStatModifier>> UCataclysmDungeonModifierEffects::StatModifiersFor(
	const FCataclysmPlayerFloorEffects& Effects)
{
	TMap<FName, TArray<FCataclysmStatModifier>> Modifiers;
	DungeonModifierEffectsAddLess(Modifiers, DungeonModifierEffectsMaxHealthStat,
								  Effects.MaxHealthLessPercent);
	DungeonModifierEffectsAddLess(Modifiers, DungeonModifierEffectsMaxEnergyShieldStat,
								  Effects.MaxEnergyShieldLessPercent);
	DungeonModifierEffectsAddLess(Modifiers, DungeonModifierEffectsMaxManaStat,
								  Effects.MaxManaLessPercent);
	return Modifiers;
}

bool UCataclysmDungeonModifierEffects::ApplyToCharacter(
	const FCataclysmPlayerFloorEffects& Effects,
	UCataclysmAbilitySystemComponent* AbilitySystem,
	UCataclysmEquipmentComponent* Equipment)
{
	if (!AbilitySystem)
	{
		return false;
	}

	// HELD FIRST, AND HELD EVEN WHEN THERE IS NO EQUIPMENT TO REFRESH WITH, so
	// the next refresh made for any reason picks them up.
	AbilitySystem->SetDungeonStatModifiers(StatModifiersFor(Effects));

	if (!Equipment)
	{
		return false;
	}

	Equipment->RefreshAttributes(AbilitySystem);
	return true;
}

FString UCataclysmDungeonModifierEffects::Describe(const FCataclysmPlayerFloorEffects& Effects)
{
	TArray<FString> Clauses;
	if (Effects.MaxHealthLessPercent > 0.0f)
	{
		Clauses.Add(FString::Printf(TEXT("maximum health %.0f%% less"),
									Effects.MaxHealthLessPercent));
	}
	if (Effects.MaxEnergyShieldLessPercent > 0.0f)
	{
		Clauses.Add(FString::Printf(TEXT("maximum energy shield %.0f%% less"),
									Effects.MaxEnergyShieldLessPercent));
	}
	if (Effects.MaxManaLessPercent > 0.0f)
	{
		Clauses.Add(FString::Printf(TEXT("maximum mana %.0f%% less"),
									Effects.MaxManaLessPercent));
	}
	return FString::Join(Clauses, TEXT(", "));
}
