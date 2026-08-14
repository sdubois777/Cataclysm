// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmImpactEffect.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Character/CataclysmCharacterBase.h"
#include "Cataclysm.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UCataclysmVitalAttributeSet::UCataclysmVitalAttributeSet()
{
	// Placeholders only. Real starting values come from a class stat line
	// applied as a gameplay effect; the three Demonic classes are in the design
	// document. These exist so an attribute set constructed with no class
	// attached is still in a valid state rather than at zero health.
	InitHealth(100.0f);
	InitMaxHealth(100.0f);
	InitMana(50.0f);
	InitMaxMana(50.0f);
	InitEnergyShield(0.0f);
	InitMaxEnergyShield(0.0f);
	InitHealthRegen(1.0f);
	InitManaRegen(1.0f);
	InitEnergyShieldRegen(0.0f);
	InitLifeLeech(0.0f);
	InitManaLeech(0.0f);
	InitEnergyShieldLeech(0.0f);
	InitDamage(0.0f);
}

void UCataclysmVitalAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, Health);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, MaxHealth);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, Mana);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, MaxMana);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, EnergyShield);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, MaxEnergyShield);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, HealthRegen);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, ManaRegen);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, EnergyShieldRegen);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, LifeLeech);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, ManaLeech);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, EnergyShieldLeech);
	// Damage is a meta attribute. It is never replicated.
}

void UCataclysmVitalAttributeSet::PreAttributeChange(
	const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetManaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxMana());
	}
	else if (Attribute == GetEnergyShieldAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxEnergyShield());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		// The one maximum that cannot be zero. See the class comment.
		NewValue = FMath::Max(NewValue, 1.0f);
	}
	else if (Attribute == GetMaxManaAttribute()
		|| Attribute == GetMaxEnergyShieldAttribute()
		|| Attribute == GetHealthRegenAttribute()
		|| Attribute == GetManaRegenAttribute()
		|| Attribute == GetEnergyShieldRegenAttribute()
		|| Attribute == GetLifeLeechAttribute()
		|| Attribute == GetManaLeechAttribute()
		|| Attribute == GetEnergyShieldLeechAttribute())
	{
		// Zero is a legitimate value for all of these. A class with no energy
		// shield is a design position, not an error state.
		NewValue = FMath::Max(NewValue, 0.0f);
	}
}

void UCataclysmVitalAttributeSet::PostGameplayEffectExecute(
	const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetDamageAttribute())
	{
		const float LocalDamage = GetDamage();
		SetDamage(0.0f);

		if (LocalDamage > 0.0f)
		{
			// The whole order lives in UCataclysmDamageCalculation, so it can be
			// tested by passing numbers in rather than by building an effect
			// spec for every case.
			//
			// TWO OF THE HIT'S PROPERTIES NOW REACH IT, and they arrive by two
			// different routes because they are two different kinds of thing.
			// Issue #486.
			//
			//   the DAMAGE TYPE belongs to the hit, and rides on the effect as an
			//   `Element.*` tag, put there by UCataclysmSkillEffects
			//
			//   the RESISTANCE PENETRATION belongs to the attacker rather than to
			//   any one blow, so it is read off the attacker at the moment the
			//   blow lands, which is also the moment it is true
			//
			// HOW IT ARRIVED RIDES ON THE EFFECT TOO, as two more tags. Whether
			// the hit swept a volume decides the evasion step, and whether it is
			// damage over time decides whether an energy shield absorbs it.
			// Issue #513.
			//
			// TWO STILL DO NOT REACH IT: armour penetration, which no attribute
			// holds anywhere, and the weapon sub-type, which decides the slashing
			// and magic bonuses. See the issue filed alongside #513.
			FCataclysmIncomingHit Hit;
			Hit.Damage = LocalDamage;

			FGameplayTagContainer AssetTags;
			Data.EffectSpec.GetAllAssetTags(AssetTags);
			Hit.DamageType =
				UCataclysmDamageCalculation::DamageTypeFromTags(AssetTags);
			Hit.bIsArea = AssetTags.HasTag(
				UCataclysmDamageCalculation::AreaDamageTag());
			Hit.bIsDamageOverTime = AssetTags.HasTag(
				UCataclysmDamageCalculation::DamageOverTimeTag());

			if (const UAbilitySystemComponent* Attacker =
					Data.EffectSpec.GetContext().GetInstigatorAbilitySystemComponent())
			{
				if (const UCataclysmCombatAttributeSet* Offence =
						Attacker->GetSet<UCataclysmCombatAttributeSet>())
				{
					Hit.ResistancePenetration = Offence->GetPenetration();
				}
			}

			const FCataclysmDamageResult Outcome =
				UCataclysmDamageCalculation::Resolve(
					Hit, GetOwningAbilitySystemComponent(), /*Tier=*/1);

			if (Outcome.AbsorbedByShield > 0.0f)
			{
				SetEnergyShield(FMath::Clamp(
					GetEnergyShield() - Outcome.AbsorbedByShield,
					0.0f, GetMaxEnergyShield()));
			}
			if (Outcome.DealtToHealth > 0.0f)
			{
				SetHealth(FMath::Clamp(GetHealth() - Outcome.DealtToHealth,
									   0.0f, GetMaxHealth()));
				NotifyIfHealthReachedZero();
			}

			PlayImpactEffect(Data, Hit, Outcome);
		}
	}
	else if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.0f, GetMaxHealth()));
		NotifyIfHealthReachedZero();
	}
	else if (Data.EvaluatedData.Attribute == GetManaAttribute())
	{
		SetMana(FMath::Clamp(GetMana(), 0.0f, GetMaxMana()));
	}
	else if (Data.EvaluatedData.Attribute == GetEnergyShieldAttribute())
	{
		SetEnergyShield(FMath::Clamp(GetEnergyShield(), 0.0f, GetMaxEnergyShield()));
	}
}

void UCataclysmVitalAttributeSet::PlayImpactEffect(
	const FGameplayEffectModCallbackData& Data,
	const FCataclysmIncomingHit& Hit,
	const FCataclysmDamageResult& Outcome)
{
	// Whether this is worth drawing at all lives in UCataclysmImpactEffect, so a
	// test can reach it without a world or a rendering device. It refuses a blow
	// that never connected, and a burn ticking, which is not a blow at all.
	const bool bWorthDrawing =
		UCataclysmImpactEffect::ShouldDrawFor(Hit, Outcome);

	// THE AVATAR, NOT THE OWNER. GetOwningActor answers with the ability
	// system's owner, and for the player that is the player state, which is not
	// placed in the world and reports the origin. Issue #562.
	const AActor* Struck =
		UCataclysmImpactEffect::ActorToDrawOn(GetOwningAbilitySystemComponent());

	// EVERY LANDED HIT IS LOGGED, INCLUDING THE ONES THAT DRAW NOTHING, and that
	// ordering is the point. An earlier version logged only after deciding to
	// draw, so a hit that arrived and did nothing left no trace at all -- which
	// is precisely the case somebody hit while playing: the effect stopped
	// appearing and there was no way to tell whether hits had stopped landing or
	// had stopped counting.
	//
	// healthLeft is what answers it. A character at zero health takes no further
	// damage, because UCataclysmDamageCalculation::Resolve ends with
	// FMath::Min(Damage, Vitals->GetHealth()) and that is zero from then on. It
	// keeps being hit and nothing happens, because a player's death is
	// deliberately not built -- see ACataclysmCharacterBase::HandleDeath.
	//
	// Counted is a running total for the session, so a burst of hits can be
	// counted without timestamps. Issue #563 needed exactly that.
	static int32 Counted = 0;
	++Counted;

	UE_LOG(LogCataclysm, Verbose,
		TEXT("hit %d: on=%s type=%s dot=%s area=%s toHealth=%.1f toShield=%.1f "
			 "healthLeft=%.1f drawn=%s"),
		Counted, Struck ? *Struck->GetName() : TEXT("(no avatar)"),
		Hit.DamageType.IsNone() ? TEXT("(none)") : *Hit.DamageType.ToString(),
		Hit.bIsDamageOverTime ? TEXT("yes") : TEXT("no"),
		Hit.bIsArea ? TEXT("yes") : TEXT("no"),
		Outcome.DealtToHealth, Outcome.AbsorbedByShield, GetHealth(),
		bWorthDrawing ? TEXT("yes") : TEXT("no"));

	if (!bWorthDrawing || !Struck)
	{
		return;
	}

	// WHERE THE BLOW LANDED. The choice lives in UCataclysmImpactEffect so a
	// test can reach it without a world or a rendering device, which is the only
	// way any of this is covered -- see issue #559.
	const FHitResult* Landed = Data.EffectSpec.GetContext().GetHitResult();
	FVector Normal = FVector::UpVector;
	const FVector Location =
		UCataclysmImpactEffect::ImpactLocationFor(Landed, Struck, Normal);

	UCataclysmImpactEffect::SpawnAt(Struck, Location, Normal, Hit.DamageType);
}

void UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero()
{
	if (GetHealth() > 0.0f)
	{
		return;
	}

	// ONCE, WHICH IS WHAT THE TAG IS FOR HERE AS WELL AS FOR ASKING. Health can
	// be written repeatedly at zero -- a burn ticking on a corpse, two hits in
	// the same frame -- and HandleDeath removes the character from the level, so
	// running it twice would be running it on something already leaving.
	// THE AVATAR, NOT THE OWNER, and for the same reason the hit effect needs it
	// in issue #562. GetOwningActor answers with the ability system's owner, and
	// ACataclysmPlayerCharacter::InitAbilityActorInfo makes that the player
	// state -- deliberately, because it survives death -- while the pawn is the
	// avatar. A player state is not a character, so the cast below failed and
	// this returned early.
	//
	// IT COSTS NOTHING TODAY, because HandleDeath is inert on the base by design
	// and a player's death is not built. It would silently stop that death ever
	// firing the moment somebody builds it, with no error to follow. Issue #565.
	const UAbilitySystemComponent* AbilitySystem =
		GetOwningAbilitySystemComponent();
	ACataclysmCharacterBase* Character = AbilitySystem
		? Cast<ACataclysmCharacterBase>(AbilitySystem->GetAvatarActor())
		: nullptr;
	if (!Character || UCataclysmSkillEffects::IsDead(Character))
	{
		return;
	}

	Character->HandleDeath();
}

TArray<FGameplayAttribute> UCataclysmVitalAttributeSet::GetAllAttributes()
{
	return {
		GetHealthAttribute(), GetMaxHealthAttribute(),
		GetManaAttribute(), GetMaxManaAttribute(),
		GetEnergyShieldAttribute(), GetMaxEnergyShieldAttribute(),
		GetHealthRegenAttribute(), GetManaRegenAttribute(),
		GetEnergyShieldRegenAttribute(), GetLifeLeechAttribute(),
		GetManaLeechAttribute(), GetEnergyShieldLeechAttribute(),
		GetDamageAttribute(),
	};
}

CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, Health)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, MaxHealth)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, Mana)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, MaxMana)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, EnergyShield)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, MaxEnergyShield)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, HealthRegen)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, ManaRegen)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, EnergyShieldRegen)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, LifeLeech)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, ManaLeech)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, EnergyShieldLeech)
