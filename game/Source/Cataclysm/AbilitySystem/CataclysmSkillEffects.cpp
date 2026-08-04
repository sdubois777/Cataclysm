// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayTagsManager.h"

namespace
{
	/**
	 * Make an effect refresh rather than stack, and grant a tag while it lasts.
	 *
	 * WHY THE DEPRECATION IS SUPPRESSED HERE AND NOT WORKED AROUND. Unreal 5.7
	 * deprecated writing UGameplayEffect::StackingType directly and offers
	 * SetStackingType instead -- but that setter is inside WITH_EDITOR, so it
	 * does not exist in a packaged build. Assigning the field is the only route
	 * available at runtime until the engine provides one. Kept in one function
	 * so there is a single place to change when it does.
	 *
	 * ONE STACK ONLY is the design's rule for every effect a player can apply,
	 * and it is aggregated by TARGET rather than by source so that two
	 * characters burning the same enemy still produce one burn.
	 */
	void MakeSingleStackTagged(UGameplayEffect* Effect, const FGameplayTag& EffectTag)
	{
		if (!Effect || !EffectTag.IsValid())
		{
			return;
		}

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Effect->StackingType = EGameplayEffectStackingType::AggregateByTarget;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		Effect->StackLimitCount = 1;
		Effect->StackDurationRefreshPolicy =
			EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;

		UTargetTagsGameplayEffectComponent& TagsComponent =
			Effect->FindOrAddComponent<UTargetTagsGameplayEffectComponent>();
		FInheritedTagContainer Granted;
		Granted.Added.AddTag(EffectTag);
		TagsComponent.SetAndApplyTargetTagChanges(Granted);
	}
}

const TCHAR* UCataclysmSkillEffects::BurnRowName = TEXT("DoT_Burn");

const TCHAR* UCataclysmSkillEffects::StatusEffectTableAssetPath =
	TEXT("/Game/Data/DT_StatusEffects.DT_StatusEffects");

const UDataTable* UCataclysmSkillEffects::LoadStatusEffectTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, StatusEffectTableAssetPath);
	if (!Table)
	{
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load %s. It is produced by "
				 "tools/generate_datatable_assets.py from "
				 "game/Data/StatusEffects.csv."), StatusEffectTableAssetPath);
	}
	return Table;
}

float UCataclysmSkillEffects::WeaponDamageOf(const UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}
	return AbilitySystem->GetNumericAttribute(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute());
}

float UCataclysmSkillEffects::ApplyHit(AActor* Instigator, AActor* Target,
									   float DamagePercent)
{
	if (DamagePercent <= 0.0f)
	{
		// A Support skill's slot damage is zero by design -- the Skill Slots
		// sheet says so -- so this is the ordinary case for a buff, not a fault.
		return 0.0f;
	}

	UAbilitySystemComponent* Source = UCataclysmTargeting::AbilitySystemOf(Instigator);
	UAbilitySystemComponent* Defender = UCataclysmTargeting::AbilitySystemOf(Target);
	if (!Source || !Defender)
	{
		return 0.0f;
	}

	const float Damage = WeaponDamageOf(Source) * DamagePercent / 100.0f;
	if (Damage <= 0.0f)
	{
		// A character with no weapon damage. Expected before a weapon is
		// equipped, and worth saying once rather than silently dealing nothing.
		UE_LOG(LogCataclysm, Verbose,
			TEXT("%s hit %s for %.0f%% of a weapon damage of zero."),
			*GetNameSafe(Instigator), *GetNameSafe(Target), DamagePercent);
		return 0.0f;
	}

	return ApplyDirectDamage(Instigator, Target, Damage) ? Damage : 0.0f;
}

bool UCataclysmSkillEffects::ApplyDirectDamage(AActor* Instigator, AActor* Target,
											   float Damage)
{
	if (Damage <= 0.0f)
	{
		return false;
	}

	UAbilitySystemComponent* Source = UCataclysmTargeting::AbilitySystemOf(Instigator);
	UAbilitySystemComponent* Defender = UCataclysmTargeting::AbilitySystemOf(Target);
	if (!Source || !Defender)
	{
		return false;
	}

	// Written into the Damage META attribute, which the defender's vital
	// attribute set intercepts in PostGameplayEffectExecute and runs through
	// UCataclysmDamageCalculation::Resolve -- evasion, block, armor, resistance,
	// flat reduction, mana, energy shield, health, in that order. Nothing here
	// decides how much of it lands.
	//
	// The hit's own properties -- its damage type, whether it is area damage,
	// the weapon sub-type -- are still not carried on the effect, so every hit
	// resolves as an untyped direct hit and resistances do nothing. That gap is
	// older than this change and is recorded on the attribute set.
	UGameplayEffect* Effect = NewObject<UGameplayEffect>(
		GetTransientPackage(), FName(TEXT("CataclysmSkillHit")));
	Effect->DurationPolicy = EGameplayEffectDurationType::Instant;

	const int32 Index = Effect->Modifiers.Num();
	Effect->Modifiers.SetNum(Index + 1);
	FGameplayModifierInfo& Modifier = Effect->Modifiers[Index];
	Modifier.Attribute = UCataclysmVitalAttributeSet::GetDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FScalableFloat(Damage);

	FGameplayEffectContextHandle Context = Source->MakeEffectContext();
	Context.AddInstigator(Instigator, Instigator);
	Defender->ApplyGameplayEffectToSelf(Effect, /*Level=*/1.0f, Context);

	return true;
}

FCataclysmStatusEffectNumbers UCataclysmSkillEffects::BurnNumbers()
{
	FCataclysmStatusEffectNumbers Numbers;

	const UDataTable* Table = LoadStatusEffectTable();
	if (!Table)
	{
		return Numbers;
	}

	const FCataclysmStatusEffectRow* Row = Table->FindRow<FCataclysmStatusEffectRow>(
		FName(BurnRowName), TEXT("UCataclysmSkillEffects::BurnNumbers"),
		/*bWarnIfRowMissing=*/false);
	if (!Row)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("The status effect table has no %s row, so no skill can set "
				 "anything alight."), BurnRowName);
		return Numbers;
	}

	Numbers.DurationSeconds = Row->DurationSeconds;
	Numbers.PercentOfHit = Row->PercentOfHit;

	// BOTH HALVES ARE NEEDED AND EITHER ONE MISSING IS THE SAME FAULT. Burn had
	// neither until this change, and a burn lasting zero seconds or worth zero
	// damage is indistinguishable from a burn nobody wrote -- which is exactly
	// how the missing cooldown in issue #155 stayed hidden.
	Numbers.bUsable = Numbers.DurationSeconds > 0.0f && Numbers.PercentOfHit > 0.0f;
	if (!Numbers.bUsable)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("Burn states a duration of %.1fs and %.0f%% of the hit. Both "
				 "must be above zero or nothing is applied. They come from "
				 "columns B and C of the DoTs sheet."),
			Numbers.DurationSeconds, Numbers.PercentOfHit);
	}

	return Numbers;
}

bool UCataclysmSkillEffects::ApplyDamageOverTime(
	AActor* Instigator, AActor* Target, float TotalDamage,
	float DurationSeconds, const FGameplayTag& EffectTag)
{
	if (TotalDamage <= 0.0f || DurationSeconds <= 0.0f)
	{
		return false;
	}

	UAbilitySystemComponent* Source = UCataclysmTargeting::AbilitySystemOf(Instigator);
	UAbilitySystemComponent* Defender = UCataclysmTargeting::AbilitySystemOf(Target);
	if (!Source || !Defender)
	{
		return false;
	}

	// One tick a second. The design has a DotFrequency stat that is meant to
	// change this, and it is not wired in yet; a fixed second keeps the total
	// damage the same however that lands.
	constexpr float SecondsPerTick = 1.0f;
	const float Ticks = FMath::Max(1.0f, DurationSeconds / SecondsPerTick);
	const float DamagePerTick = TotalDamage / Ticks;

	UGameplayEffect* Effect = NewObject<UGameplayEffect>(
		GetTransientPackage(), FName(TEXT("CataclysmDamageOverTime")));
	Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
	Effect->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(DurationSeconds));
	Effect->Period = FScalableFloat(SecondsPerTick);

	// False, so the first tick lands a second in rather than at once. Applying
	// on the instant would mean a skill that reapplies burn faster than once a
	// second dealt its whole burn on every application.
	Effect->bExecutePeriodicEffectOnApplication = false;

	const int32 Index = Effect->Modifiers.Num();
	Effect->Modifiers.SetNum(Index + 1);
	FGameplayModifierInfo& Modifier = Effect->Modifiers[Index];
	Modifier.Attribute = UCataclysmVitalAttributeSet::GetDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FScalableFloat(DamagePerTick);

	MakeSingleStackTagged(Effect, EffectTag);

	FGameplayEffectContextHandle Context = Source->MakeEffectContext();
	Context.AddInstigator(Instigator, Instigator);
	Defender->ApplyGameplayEffectToSelf(Effect, /*Level=*/1.0f, Context);

	return true;
}

FGameplayTag UCataclysmSkillEffects::BurnTag()
{
	// Requested by name rather than declared as a native tag, for the same
	// reason CataclysmAbilitySlots requests the slot tags by name: a native
	// declaration would create the tag whether or not the workbook still lists
	// it, hiding exactly the disagreement that matters.
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Keyword.DoT.Burn")), /*ErrorIfNotFound=*/false);
}

bool UCataclysmSkillEffects::ApplyBurn(AActor* Instigator, AActor* Target,
									   float HitDamage)
{
	const FCataclysmStatusEffectNumbers Burn = BurnNumbers();
	if (!Burn.bUsable || HitDamage <= 0.0f)
	{
		return false;
	}

	return ApplyDamageOverTime(Instigator, Target,
							   HitDamage * Burn.PercentOfHit / 100.0f,
							   Burn.DurationSeconds, BurnTag());
}

bool UCataclysmSkillEffects::HasTag(const AActor* Actor, const FGameplayTag& Tag)
{
	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Actor);
	return AbilitySystem && Tag.IsValid() && AbilitySystem->HasMatchingGameplayTag(Tag);
}

bool UCataclysmSkillEffects::ApplyTagForDuration(
	AActor* Instigator, AActor* Target, const FGameplayTag& EffectTag,
	float DurationSeconds)
{
	if (!EffectTag.IsValid() || DurationSeconds <= 0.0f)
	{
		return false;
	}

	UAbilitySystemComponent* Source = UCataclysmTargeting::AbilitySystemOf(Instigator);
	UAbilitySystemComponent* Defender = UCataclysmTargeting::AbilitySystemOf(Target);
	if (!Source || !Defender)
	{
		return false;
	}

	UGameplayEffect* Effect = NewObject<UGameplayEffect>(
		GetTransientPackage(),
		FName(*FString::Printf(TEXT("CataclysmStatus_%s"), *EffectTag.ToString())));
	Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
	Effect->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(DurationSeconds));

	MakeSingleStackTagged(Effect, EffectTag);

	FGameplayEffectContextHandle Context = Source->MakeEffectContext();
	Context.AddInstigator(Instigator, Instigator);
	Defender->ApplyGameplayEffectToSelf(Effect, /*Level=*/1.0f, Context);

	return true;
}
