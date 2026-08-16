// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
// For the boss check in ApplyStun: boss-ness lives on the enemy as its rarity
// step, so rule three has to ask the enemy class. Issue #395.
#include "Character/CataclysmEnemyCharacter.h"
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

float UCataclysmSkillEffects::ModifiedDamage(const UAbilitySystemComponent* Source,
											 float BaseDamage,
											 const FGameplayTagContainer& SkillTags)
{
	// An ability system component this project did not make carries no modifier
	// list, which is not a fault: an enemy's plain melee attack goes through
	// here too and has nothing to scale it.
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<UCataclysmAbilitySystemComponent>(Source);
	if (!Cataclysm || BaseDamage <= 0.0f)
	{
		return BaseDamage;
	}

	const TArray<FCataclysmStatModifier>& Modifiers = Cataclysm->GetStatModifiers();
	if (Modifiers.IsEmpty())
	{
		return BaseDamage;
	}

	// Base is the skill's damage, not the weapon's. A skill buff's increase is
	// written against what the skill deals -- Burning Wrath reads "4% increased
	// fire damage", not "4% increased weapon damage" -- so the skill's own
	// percentage has already been applied by the time this runs.
	return UCataclysmStatPipeline::Evaluate(BaseDamage, Modifiers, SkillTags).Final;
}

float UCataclysmSkillEffects::ApplyHit(AActor* Instigator, AActor* Target,
									   float DamagePercent,
									   const FGameplayTagContainer& SkillTags,
									   const FCataclysmHitDelivery& Delivery)
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

	const float Damage = ModifiedDamage(
		Source, WeaponDamageOf(Source) * DamagePercent / 100.0f, SkillTags);
	if (Damage <= 0.0f)
	{
		// A character with no weapon damage. Expected before a weapon is
		// equipped, and worth saying once rather than silently dealing nothing.
		UE_LOG(LogCataclysm, Verbose,
			TEXT("%s hit %s for %.0f%% of a weapon damage of zero."),
			*GetNameSafe(Instigator), *GetNameSafe(Target), DamagePercent);
		return 0.0f;
	}

	// THE SKILL'S OWN TAGS DECIDE WHETHER THIS IS AREA DAMAGE, and a caller may
	// also say so outright. The two are combined rather than one overriding the
	// other, because a skill row and an enemy's C++ ability answer the same
	// question by different routes and neither should be able to cancel the other.
	FCataclysmHitDelivery Arrived = Delivery;
	Arrived.bIsArea = Arrived.bIsArea || IsAreaDamage(SkillTags);

	return ApplyDirectDamage(Instigator, Target, Damage, Arrived) ? Damage : 0.0f;
}

bool UCataclysmSkillEffects::ApplyDirectDamage(AActor* Instigator, AActor* Target,
											   float Damage,
											   const FCataclysmHitDelivery& Delivery)
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
	ApplyTypedSpec(Effect, Context, Defender, Instigator, Delivery);

	return true;
}

const TCHAR* UCataclysmSkillEffects::PointBlankAreaTagName =
	TEXT("Type.AOE.PointBlank");
const TCHAR* UCataclysmSkillEffects::AuraAreaTagName = TEXT("Type.AOE.Aura");

bool UCataclysmSkillEffects::IsAreaDamage(const FGameplayTagContainer& SkillTags)
{
	if (SkillTags.IsEmpty())
	{
		return false;
	}

	UGameplayTagsManager& Tags = UGameplayTagsManager::Get();
	for (const TCHAR* Name : { PointBlankAreaTagName, AuraAreaTagName })
	{
		const FGameplayTag Tag =
			Tags.RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
		if (Tag.IsValid() && SkillTags.HasTag(Tag))
		{
			return true;
		}
	}
	return false;
}

FName UCataclysmSkillEffects::DamageTypeOf(const AActor* Attacker)
{
	// ONLY AN ENEMY'S DAMAGE IS TYPED. The project owner settled this on
	// 2026-08-12: a player has eight resistances because eight Cataclysms attack
	// them, so an enemy's hit has to say which one applies. An enemy has ONE
	// generic resistance that meets every hit whatever it is, so a player's hit
	// has nothing to choose between and carries no type at all.
	//
	// A minion's or a projectile's hit is typed by whoever fired it, because the
	// instigator passed down the chain is that character rather than the thing it
	// sent. So an enemy's projectile is Demonic and a player's is untyped, which
	// is the same rule and not a special case.
	const ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Attacker);
	return Enemy ? Enemy->DamageType : NAME_None;
}

void UCataclysmSkillEffects::ApplyTypedSpec(UGameplayEffect* Effect,
											 const FGameplayEffectContextHandle& Context,
											 UAbilitySystemComponent* Defender,
											 const AActor* Attacker,
											 const FCataclysmHitDelivery& Delivery)
{
	// BUILT AS A SPEC RATHER THAN HANDED TO ApplyGameplayEffectToSelf, purely so
	// a tag can be put on it. That overload builds the spec itself and gives no
	// chance to add one, and a dynamic asset tag is how the hit's damage type
	// reaches the defender: see UCataclysmDamageCalculation::ElementTagFor.
	FGameplayEffectSpec Spec(Effect, Context, /*Level=*/1.0f);

	const FGameplayTag Element =
		UCataclysmDamageCalculation::ElementTagFor(DamageTypeOf(Attacker));
	if (Element.IsValid())
	{
		Spec.AddDynamicAssetTag(Element);
	}

	// HOW THE HIT ARRIVED, as two more tags on the same spec. Added only when
	// true, so an ordinary direct blow carries nothing extra.
	if (Delivery.bIsArea)
	{
		const FGameplayTag Area = UCataclysmDamageCalculation::AreaDamageTag();
		if (Area.IsValid())
		{
			Spec.AddDynamicAssetTag(Area);
		}
	}
	if (Delivery.bIsDamageOverTime)
	{
		const FGameplayTag OverTime =
			UCataclysmDamageCalculation::DamageOverTimeTag();
		if (OverTime.IsValid())
		{
			Spec.AddDynamicAssetTag(OverTime);
		}
	}
	if (Delivery.bCannotCriticallyStrike)
	{
		const FGameplayTag NoCrit =
			UCataclysmDamageCalculation::NoCriticalStrikeTag();
		if (NoCrit.IsValid())
		{
			Spec.AddDynamicAssetTag(NoCrit);
		}
	}

	Defender->ApplyGameplayEffectSpecToSelf(Spec);
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
	// Typed the same way a direct hit is. A burn left by an enemy is that
	// enemy's damage type, so the player's resistance to it applies to every
	// tick and not only to the blow that started it.
	//
	// AND MARKED AS DAMAGE OVER TIME, which is what stops an energy shield
	// absorbing it. A shield that soaked burn would be a second health bar rather
	// than a distinct defence, and damage over time is the design's answer to
	// shield stacking. Issue #513.
	FCataclysmHitDelivery Delivery;
	Delivery.bIsDamageOverTime = true;
	ApplyTypedSpec(Effect, Context, Defender, Instigator, Delivery);

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

FGameplayTag UCataclysmSkillEffects::StunnedTag()
{
	// Requested by name rather than declared natively, for the reason BurnTag
	// gives: a native declaration would create the tag whether or not the
	// workbook still lists it, hiding exactly the disagreement that matters.
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("State.Stunned")), /*ErrorIfNotFound=*/false);
}

FGameplayTag UCataclysmSkillEffects::StunImmuneTag()
{
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("State.StunImmune")), /*ErrorIfNotFound=*/false);
}

bool UCataclysmSkillEffects::IsStunned(const AActor* Actor)
{
	return HasTag(Actor, StunnedTag());
}

bool UCataclysmSkillEffects::ApplyKnockback(AActor* Instigator, AActor* Target,
											float DistanceCm)
{
	if (DistanceCm <= 0.0f || !IsValid(Instigator) || !IsValid(Target))
	{
		return false;
	}

	// Away from whatever hit it, along the ground.
	FVector Away = Target->GetActorLocation() - Instigator->GetActorLocation();
	Away.Z = 0.0f;
	if (Away.IsNearlyZero())
	{
		// Standing exactly on the instigator. There is no direction to push in,
		// and picking one arbitrarily would shove a target somewhere nobody could
		// have predicted.
		return false;
	}

	// HALVED FOR EACH DISPLACEMENT THE TARGET HAS ALREADY TAKEN INSIDE THE
	// WINDOW: the full distance, then half, then a quarter, resetting once 5
	// seconds pass with no displacement at all. Three shoves inside the window
	// move a target seven metres in total rather than twelve, which is what stops
	// it being held at the far end of a room. Issues #302 and #628.
	//
	// ASKED OF THE TARGET, because the count belongs to the target rather than to
	// whatever is shoving: the previous shove was usually a different attack and
	// often a different actor. It is kept on the ability system component because
	// that is what everything hittable has.
	float Share = 1.0f;
	if (UCataclysmAbilitySystemComponent* TargetAbilities =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Target)))
	{
		Share = TargetAbilities->TakeNextDisplacementShare();
	}

	// A DISPLACEMENT RATHER THAN AN IMPULSE, because most of what this hits has
	// no physics body and a knockback that silently did nothing would look
	// exactly like a knockback. Swept, so a shove into a wall stops at the wall.
	Target->AddActorWorldOffset(Away.GetSafeNormal() * DistanceCm * Share,
								/*bSweep=*/true);
	return true;
}

FGameplayTag UCataclysmSkillEffects::DeadTag()
{
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("State.Dead")), /*ErrorIfNotFound=*/false);
}

bool UCataclysmSkillEffects::IsDead(const AActor* Actor)
{
	return HasTag(Actor, DeadTag());
}

bool UCataclysmSkillEffects::MarkDead(AActor* Actor)
{
	UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
	const FGameplayTag Dead = DeadTag();
	if (!System || !Dead.IsValid() || System->HasMatchingGameplayTag(Dead))
	{
		return false;
	}

	// LOOSELY RATHER THAN THROUGH AN EFFECT, because every other tag here is
	// granted for a duration and this one must never expire on its own. A
	// duration effect with an infinite duration would do the same thing with
	// more moving parts and one more way to get the duration wrong. What takes
	// it off again is ClearDead below, called deliberately.
	System->AddLooseGameplayTag(Dead);
	return true;
}

bool UCataclysmSkillEffects::ClearDead(AActor* Actor)
{
	UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
	const FGameplayTag Dead = DeadTag();
	if (!System || !Dead.IsValid() || !System->HasMatchingGameplayTag(Dead))
	{
		return false;
	}

	System->RemoveLooseGameplayTag(Dead);
	return true;
}

bool UCataclysmSkillEffects::ApplyStun(AActor* Instigator, AActor* Target,
									   float DurationSeconds, float DamageDealt,
									   bool bStunIsDesigned)
{
	if (DurationSeconds <= 0.0f)
	{
		return false;
	}

	// RULE TWO: A STUNNED TARGET CANNOT BE STUNNED AGAIN FOR FIVE SECONDS.
	// Checked first because it is the cheapest and because it applies to every
	// stun, designed or incidental. A designed stun does not escape it: that is
	// the whole reason the Brute's Stomp has a five second cooldown rather than
	// a cooldown from the Heavy slot's one-to-four second band.
	if (HasTag(Target, StunImmuneTag()))
	{
		return false;
	}

	// RULE ONE: A HIT MUST TAKE AT LEAST A TENTH OF MAXIMUM HEALTH TO STUN.
	//
	// A DESIGNED STUN SKIPS THIS AND ONLY THIS. An attack whose entire purpose
	// is to stun should not fail to when it lands, so the Stomp does not have to
	// clear the bar. It still obeys the window above. The distinction is
	// stun_is_designed in sim/cataclysm_sim/damage.py.
	if (!bStunIsDesigned)
	{
		const UAbilitySystemComponent* Defender =
			UCataclysmTargeting::AbilitySystemOf(Target);
		if (!Defender)
		{
			return false;
		}

		// A target already dead is not stunned, matching can_be_stunned in the
		// model. Without this a killing blow would stun a corpse.
		const float Health = Defender->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute());
		if (Health <= 0.0f)
		{
			return false;
		}

		const float MaxHealth = Defender->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
		if (MaxHealth <= 0.0f)
		{
			return false;
		}

		if (DamageDealt < MaxHealth * StunDamageThresholdPercent / 100.0f)
		{
			return false;
		}
	}

	// RULE THREE: A BOSS CANNOT BE STUNNED AT ALL. Section VI of the design
	// document, and the last of its three anti-stun-lock rules to arrive --
	// issue #395. Boss-ness derives from the rarity the spawner set, steps 4
	// and 5 of the ladder: Boss and Cataclysm Boss. See
	// ACataclysmEnemyCharacter::IsBoss for why it is rarity rather than a flag
	// or a tag.
	//
	// UNCONDITIONALLY, INCLUDING FOR A DESIGNED STUN. bStunIsDesigned exists to
	// skip the damage THRESHOLD above, because an attack built to stun should
	// not fail to when it lands; it does not skip the immunity window and it
	// does not skip this. "At all" means at all.
	if (const ACataclysmEnemyCharacter* Enemy =
			Cast<ACataclysmEnemyCharacter>(Target))
	{
		if (Enemy->IsBoss())
		{
			return false;
		}
	}

	if (!ApplyTagForDuration(Instigator, Target, StunnedTag(), DurationSeconds))
	{
		return false;
	}

	// THE WINDOW STARTS WHEN THE STUN DOES, not when it ends, which is why five
	// seconds of immunity and 1.5 seconds of stun leave a 3.5 second gap the
	// target can act in before it can be stunned again.
	ApplyTagForDuration(Instigator, Target, StunImmuneTag(),
						StunImmunityWindowSeconds);

	return true;
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
