// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmWoundBurst.h"

#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"

const UCataclysmSkillTemplate* UCataclysmWoundBurst::WoundThatBursts(const AActor* Dying)
{
	const UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Dying);
	if (!Theirs)
	{
		return nullptr;
	}

	// EVERY RUNNING EFFECT, AND THE SKILL EACH NAMES. A death is rare beside a hit, so this walk is paid once a death;
	// the ordinary effect names no skill, or one stating no burst, and is passed over at the first question.
	for (const FActiveGameplayEffectHandle& Handle : Theirs->GetActiveEffects(FGameplayEffectQuery()))
	{
		const FActiveGameplayEffect* Active = Theirs->GetActiveGameplayEffect(Handle);
		if (!Active)
		{
			continue;
		}

		const FGameplayEffectContextHandle& Context = Active->Spec.GetContext();
		const UCataclysmSkillTemplate* Skill = Cast<const UCataclysmSkillTemplate>(Context.GetAbilityInstance_NotReplicated());
		if (!Skill || !Skill->Params.OnDeath.Equals(TEXT("Burst"), ESearchCase::IgnoreCase)
			|| Skill->Params.BurstPercentOfMaximumHealth <= 0.0f || Skill->Params.OnDeathRangeCm <= 0.0f)
		{
			continue;
		}

		// THE AILMENT THE ROW NAMES, AND THIS EFFECT GRANTING IT: the wound. Another effect the same skill laid -- a
		// curse beside the bleed -- is not the wound and does not burst.
		const FCataclysmAilmentKind* Kind = UCataclysmAilments::KindNamedInASkillRow(Skill->Params.Ailment);
		const FGameplayTag Wound = Kind
			? FGameplayTag::RequestGameplayTag(FName(Kind->TagName), /*ErrorIfNotFound=*/false)
			: FGameplayTag();
		if (!Wound.IsValid())
		{
			continue;
		}
		FGameplayTagContainer Granted;
		Active->Spec.GetAllGrantedTags(Granted);
		if (!Granted.HasTag(Wound))
		{
			continue;
		}

		// LAID BY THE SKILL'S OWN CASTER, still here: the instigator the running effect names. One the skill's caster did
		// not lay -- a copy a spread made in another's name -- is not this caster's wound.
		const AActor* Caster = Skill->GetAvatarActorFromActorInfo();
		if (!IsValid(Caster) || Context.GetInstigator() != Caster)
		{
			continue;
		}
		return Skill;
	}
	return nullptr;
}

int32 UCataclysmWoundBurst::BurstFromDying(AActor* Dying)
{
	if (!IsValid(Dying))
	{
		return 0;
	}

	const UCataclysmSkillTemplate* Skill = WoundThatBursts(Dying);
	if (!Skill)
	{
		// The ordinary case: every death but one carrying a wound that bursts.
		return 0;
	}

	// THE CASTER, AND THE DYING CREATURE ITS ENEMY: asked of the teams, as a kill's Fervour asks
	// (`UCataclysmKillFervour::PayForTheKillingBlow`), since a creature being marked dead is no target to
	// `UCataclysmTargeting::IsHostileTo`.
	AActor* Caster = Skill->GetAvatarActorFromActorInfo();
	if (!IsValid(Caster) || UCataclysmTeams::AttitudeBetween(Caster, Dying) != ETeamAttitude::Hostile)
	{
		return 0;
	}

	// ITS OWN MAXIMUM, READ NOW, while it still has an ability system to read: "10% of its maximum health".
	const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Dying);
	const float Maximum = Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) : 0.0f;
	const float Damage = Maximum * Skill->Params.BurstPercentOfMaximumHealth / 100.0f;
	if (Damage <= 0.0f)
	{
		return 0;
	}

	// EVERY ENEMY OF THE CASTER'S WITHIN THE ROW'S OWN REACH OF WHERE IT FELL, the creature itself excluded by name, as
	// the curse spread excludes it: it is not dead yet to the search.
	TArray<AActor*> Caught = UCataclysmTargeting::FindEnemiesInSphere(
		Dying->GetWorld(), Caster, Dying->GetActorLocation(), Skill->Params.OnDeathRangeCm);
	Caught.Remove(Dying);

	// THE CONSEQUENCE OF A DEATH, AS THE OVERKILL EXPLOSION IS (`UCataclysmSkillEffects::ExplodeForOverkill`): area damage,
	// not retaliated, never a critical strike, no weapon sub-type, no leech, and none of the caster's on-hit rows. AND
	// THE SKILL WHOSE WOUND IT IS, by name. See the header for each.
	FCataclysmHitDelivery Delivery;
	Delivery.bIsArea = true;
	Delivery.bCannotBeRetaliatedAgainst = true;
	Delivery.bCannotCriticallyStrike = true;
	Delivery.bCarriesNoWeaponSubType = true;
	Delivery.bCannotLeech = true;
	Delivery.bIsConsequenceOfADeath = true;
	Delivery.Skill = Skill;

	int32 Struck = 0;
	for (AActor* Near : Caught)
	{
		// ASKED AGAIN, because a blow earlier in this loop may have killed it through a chain.
		if (!IsValid(Near) || UCataclysmSkillEffects::IsDead(Near))
		{
			continue;
		}
		if (UCataclysmSkillEffects::ApplyDirectDamage(Caster, Near, Damage, Delivery))
		{
			++Struck;
		}
	}

	UE_LOG(LogCataclysm, Display,
		TEXT("%s died while %s's wound still ran on it and burst for %.0f, %.1f%% of its %.0f maximum health, striking "
			 "%d within %.1f m."),
		*GetNameSafe(Dying), *Skill->SkillName, Damage, Skill->Params.BurstPercentOfMaximumHealth, Maximum, Struck,
		Skill->Params.OnDeathRangeCm / 100.0f);
	return Struck;
}
