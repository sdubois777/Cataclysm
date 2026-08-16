// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmImpactEffect.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Character/CataclysmCharacterBase.h"
// For the difficulty tier a hit resolves at. It lives on the game mode because
// nothing smaller holds one and the design's own home for it, the dungeon, does
// not exist yet. Issue #514.
#include "Player/CataclysmGameMode.h"
// For the weapon sub-type a hit carries, which is a property of what the
// attacker is holding rather than a number on its attribute set. Issue #639.
#include "Items/CataclysmWeaponSlotsComponent.h"
// For the floating number that says what the blow did. Issue #518.
#include "Interface/CataclysmCombatOverlay.h"
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
			// ALL OF THEM REACH IT NOW. Armour penetration gained an attribute on
			// issue #520 and is read beside the resistance penetration below; the
			// weapon sub-type is read off the attacking actor on issue #639,
			// because it is a property of what is in its hand rather than a number
			// it carries.
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

					// AND THE ARMOUR PENETRATION, which is a SECOND stat rather
					// than the same one. The line above cuts into the target's
					// resistance at step 4; this cuts into its armour at step 3.
					// Nothing held an armour penetration value at all until
					// issue #520, so `Hit.ArmorPenetration` was applied
					// correctly by Resolve and never set, and the three
					// enchantments in EnchantmentsPositive.csv that grant it
					// could do nothing.
					//
					// READ OFF THE ATTACKER FOR THE SAME REASON: penetration of
					// either kind belongs to whoever is swinging rather than to
					// any one blow, so it is read at the moment the blow lands,
					// which is also the moment it is true.
					Hit.ArmorPenetration = Offence->GetArmorPenetration();
				}
			}

			// AND THE WEAPON SUB-TYPE, WHICH IS READ OFF THE ATTACKING ACTOR rather
			// than off its attributes, because it is a property of what is in its
			// hand rather than a number it carries. Issue #639: all three of these
			// were applied correctly by Resolve and none was ever set, because
			// nothing joined the equipped weapon to a hit.
			//
			// AN ENEMY ANSWERS EMPTY and always will: it has no weapon slots
			// component, because it attacks from its own attack damage rather than
			// from a weapon. That is the same answer every hit gave before this
			// existed, so nothing about an enemy's hit changes.
			//
			// BLUNT IS NOT HERE, and that is a gap rather than an omission. Its
			// effect is a 10% chance to stun for 0.75 seconds, and nothing in the
			// project rolls a chance to stun on an ordinary hit --
			// UCataclysmSkillEffects::ApplyStun applies one already decided. Issue
			// #639 records what building it takes.
			const FString SubType = UCataclysmWeaponSlotsComponent::SubTypeOf(
				Data.EffectSpec.GetContext().GetEffectCauser());
			Hit.bIsSlashing =
				SubType.Equals(TEXT("Slashing"), ESearchCase::IgnoreCase);
			Hit.bIsMagic = SubType.Equals(TEXT("Magic"), ESearchCase::IgnoreCase);
			Hit.bIsPiercing =
				SubType.Equals(TEXT("Piercing"), ESearchCase::IgnoreCase);
			Hit.bIsBlunt = SubType.Equals(TEXT("Blunt"), ESearchCase::IgnoreCase);

			// THE DIFFICULTY TIER IS READ RATHER THAN ASSUMED, since issue #514.
			// This passed a literal 1 because nothing in the project held a tier
			// at all, and the tier decides what armour is worth: armour removes
			// `armor / (armor + 800 x tier)` of a hit, capped at 75%, so the
			// Abyssal Warden's designed 5,954 stopped 75% of every hit instead
			// of the 48.19% its design states. Every armoured thing in the game
			// was 2.07 times harder to hurt than the simulation said.
			//
			// ASKED OF THE DEFENDER'S WORLD, because that is where the fight is
			// happening. `ACataclysmGameMode::DifficultyTierIn` answers with the
			// console variable, then the game mode, then tier 1 -- and a world
			// with no game mode gets exactly the answer this line used to
			// hard-code, so nothing that does not care is changed by it.
			const FCataclysmDamageResult Outcome =
				UCataclysmDamageCalculation::Resolve(
					Hit, GetOwningAbilitySystemComponent(),
					ACataclysmGameMode::DifficultyTierIn(GetOwningActor()));

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

			// A BLUNT WEAPON MAY STUN WHAT IT HITS. Issue #639, and the last
			// of the four sub-types to be built. Its effect is the only one that
			// is not damage, which is why it is here rather than inside Resolve:
			// a stun goes through UCataclysmSkillEffects::ApplyStun, which
			// enforces the three anti-stun-lock rules, and Resolve is pure
			// arithmetic with no way to reach any of them.
			//
			// THE THRESHOLD, THE WINDOW AND BOSS IMMUNITY ARE ALL ApplyStun'S,
			// so none of them is checked twice. It is passed the damage this hit
			// actually dealt and told the stun is NOT designed, which is what
			// makes it obey the 10% damage threshold -- a stun rolled on an
			// ordinary hit must obey it, or chip damage from a fast blunt weapon
			// would interrupt a well defended character constantly, which is
			// half of what the rule exists to stop.
			//
			// CROWD CONTROL RESISTANCE REDUCES THE TOTAL, NOT THE CAPPED CHANCE,
			// so it bites into the overflow as well and is worth something
			// against a heavy stun build rather than nothing.
			if (Hit.bIsBlunt && Outcome.DealtToHealth > 0.0f)
			{
				float Total = UCataclysmDamageCalculation::BluntStunChance;
				if (const UCataclysmCombatAttributeSet* Defence =
						GetOwningAbilitySystemComponent()
							? GetOwningAbilitySystemComponent()
								  ->GetSet<UCataclysmCombatAttributeSet>()
							: nullptr)
				{
					const float Resisted = FMath::Clamp(
						Defence->GetCrowdControlResistance(), 0.0f, 100.0f);
					Total *= 1.0f - Resisted / 100.0f;
				}

				float Chance = 0.0f;
				float Seconds = 0.0f;
				UCataclysmDamageCalculation::StunApplication(Total, Chance,
															 Seconds);

				if (Chance > 0.0f && FMath::FRandRange(0.0f, 100.0f) < Chance)
				{
					UCataclysmSkillEffects::ApplyStun(
						Data.EffectSpec.GetContext().GetEffectCauser(),
						GetOwningActor(), Seconds, Outcome.DealtToHealth,
						/*bStunIsDesigned=*/false);
				}
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
	// FMath::Min(Damage, Vitals->GetHealth()) and that is zero from then on.
	//
	// THAT CLAMP IS RIGHT AND WAS NEVER THE DEFECT. What was wrong is that a
	// player at zero health was not marked dead, so nothing stopped, nothing
	// stopped attacking it, and the hits went on arriving and dealing nothing --
	// fifty-six of them over seventy seconds in the session that found it.
	// ACataclysmPlayerCharacter::HandleDeath now marks and stops the player, and
	// UCataclysmTargeting no longer finds a dead character at all. Issue #570.
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

	// THE NUMBER IS RECORDED BEFORE THE PARTICLE'S EARLY RETURN, DELIBERATELY,
	// because the two follow opposite rules and the ordering is what enforces
	// it. The particle refuses a hit that never connected, so that a burst means
	// "that landed" rather than "an attack happened". A number is wanted for
	// exactly those hits: an evaded blow says "Evaded" and one armour and
	// resistance took to nothing shows a zero, which is the only way to see
	// issues #483 and #644 happening while playing rather than in arithmetic.
	//
	// Struck may be null, and UCataclysmCombatOverlay::Record answers that by
	// drawing nothing rather than guessing a position -- the same contract
	// ActorToDrawOn states above.
	UCataclysmCombatOverlay::Record(Struck, Hit, Outcome);

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
