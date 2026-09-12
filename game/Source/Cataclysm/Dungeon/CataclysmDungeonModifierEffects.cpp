// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmDungeonModifierEffects.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "Dungeon/CataclysmFloorBrief.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"

const TCHAR* UCataclysmDungeonModifierEffects::StarvationKey = TEXT("Famine_Starvation");
const TCHAR* UCataclysmDungeonModifierEffects::DehydrationKey = TEXT("Famine_Dehydration");
const TCHAR* UCataclysmDungeonModifierEffects::ForcedMarchKey =
	TEXT("War_Forced_March");
const TCHAR* UCataclysmDungeonModifierEffects::NihilsEmbraceKey =
	TEXT("Void_The_Nihil_s_Embrace");

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

	/**
	 * One multiplier from a dungeon rule, or nothing for a value of nothing.
	 *
	 * SIGNED, AND THE SIGN IS WHAT MAKES A LESS. The pipeline multiplies by
	 * (1 + Value / 100), so -10 is x0.9 and +10 is x1.1, and it floors a Less at
	 * -99 so no rule can take a number to nothing. The most any rule here takes
	 * is 60, well inside that.
	 *
	 * ONE PLACE BUILDS THE MODIFIER, which is why the two callers below pass a
	 * sign rather than each building their own.
	 */
	void DungeonModifierEffectsAddMultiplier(
		TMap<FName, TArray<FCataclysmStatModifier>>& Into, FName Stat, float Value)
	{
		if (FMath::IsNearlyZero(Value))
		{
			return;
		}

		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::More;
		Modifier.Source = ECataclysmModifierSource::DungeonRule;
		Modifier.Value = Value;

		Into.FindOrAdd(Stat).Add(Modifier);
	}

	/** One Less multiplier from a dungeon rule, or nothing for a share of zero. */
	void DungeonModifierEffectsAddLess(
		TMap<FName, TArray<FCataclysmStatModifier>>& Into, const TCHAR* Stat,
		float LessPercent)
	{
		if (LessPercent <= 0.0f)
		{
			return;
		}

		DungeonModifierEffectsAddMultiplier(Into, FName(Stat), -LessPercent);
	}
}

ECataclysmModifierBuilt UCataclysmDungeonModifierEffects::BuiltStateOf(FName RowKey)
{
	// FOUR ARE BUILT, AND THE LAST TWO WERE ADDED BY ISSUE #41'S SLICE 2. Forced
	// March and The Nihil's Embrace do everything their rows describe, including
	// that row's cleanse on a high tier enemy's defeat, so neither is "partly".
	if (RowKey == FName(StarvationKey) || RowKey == FName(DehydrationKey)
		|| RowKey == FName(ForcedMarchKey) || RowKey == FName(NihilsEmbraceKey))
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
		FName(ForcedMarchKey),
		FName(NihilsEmbraceKey),
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

int32 UCataclysmDungeonModifierEffects::ForcedMarchStacksAfter(
	float SecondsStoodStill)
{
	// NOTHING UNTIL THE ROW'S THRESHOLD, and nothing at all for a character that
	// cannot be asked: a negative wait means "no character to read", which is the
	// same reading the movement conditions refuse on. Both fail this comparison.
	if (SecondsStoodStill < ForcedMarchSecondsBeforeDamage)
	{
		return 0;
	}

	// ONE A SECOND PAST THE THRESHOLD, in whole seconds, so three seconds exactly
	// is the first stack and four seconds is the second.
	const int32 Stacks =
		1 + FMath::FloorToInt(SecondsStoodStill - ForcedMarchSecondsBeforeDamage);
	return FMath::Min(Stacks, ForcedMarchMostStacks);
}

float UCataclysmDungeonModifierEffects::ForcedMarchSharePerSecond(int32 Stacks)
{
	return FMath::Max(0, Stacks) * ForcedMarchPercentPerStackPerSecond;
}

float UCataclysmDungeonModifierEffects::NihilsEmbraceResistanceLost(
	float MetresWalked)
{
	if (MetresWalked <= 0.0f || NihilsEmbraceMetresPerResistancePercent <= 0.0f)
	{
		return 0.0f;
	}

	const float Points = FMath::FloorToFloat(
		MetresWalked / NihilsEmbraceMetresPerResistancePercent);
	return FMath::Min(Points, NihilsEmbraceMostResistancePercent);
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

	// AND THE NIHIL'S EMBRACE, ON ALL EIGHT RESISTANCES. Issue #41, slice 2. The
	// row says "your resistances", and this game holds one resistance per
	// Cataclysm damage type rather than a single number, so that is eight
	// modifiers rather than one.
	//
	// BUILT FROM THE SHIPPING LIST OF DAMAGE TYPES rather than eight names typed
	// here, so a type renamed in the design workbook moves this with it.
	//
	// THE LOSS AND THE REWARD SHARE EVERY STAT AND THE SAME BUCKET, so the
	// pipeline adds them: a cleanse puts the loss back to nothing and starts the
	// reward, and a character that walks again while the reward runs carries both
	// at once.
	for (const FName DamageType : UCataclysmItemModifiers::DamageTypeNames())
	{
		const FName Stat = UCataclysmItemModifiers::ResistanceStatFor(DamageType);
		DungeonModifierEffectsAddMultiplier(Modifiers, Stat,
											-Effects.ResistanceLessPercent);
		DungeonModifierEffectsAddMultiplier(Modifiers, Stat,
											Effects.ResistanceMorePercent);
	}

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

	// THE NIHIL'S EMBRACE, IN BOTH DIRECTIONS. Issue #41, slice 2. The per-floor
	// log and the floor panel both read this, and a rule taking a player's
	// resistances with nothing saying so would read as a fault in the game.
	if (Effects.ResistanceLessPercent > 0.0f)
	{
		Clauses.Add(FString::Printf(TEXT("all resistances %.0f%% less"),
									Effects.ResistanceLessPercent));
	}
	if (Effects.ResistanceMorePercent > 0.0f)
	{
		Clauses.Add(FString::Printf(TEXT("all resistances %.0f%% more"),
									Effects.ResistanceMorePercent));
	}
	return FString::Join(Clauses, TEXT(", "));
}
