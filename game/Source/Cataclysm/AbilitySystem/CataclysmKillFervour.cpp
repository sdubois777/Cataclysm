// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmKillFervour.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmBuriedWeapon.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmCurseSpread.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"

float UCataclysmKillFervour::PayForTheKillingBlow(AActor* Dying)
{
	const UCataclysmAbilitySystemComponent* Theirs =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Dying));
	if (!Theirs)
	{
		return 0.0f;
	}

	// THE BLOW THAT KILLED, AND A BLOW RATHER THAN A TICK. `KillingBlowDamage` is written for a lethal blow alone
	// (`UCataclysmCombatEvents::NoteBlow`), so a death written some other way after an ordinary blow is not credited to
	// that blow's skill. A tick is the ailment's kill, a judgement of slice DS3 (see the header). A point-blank skill's
	// share struck at an ally is no kill of an enemy, as it raises none of its striker's rows (ruled 2026-10-15).
	const FCataclysmLastBlow& Blow = Theirs->GetLastBlow();
	if (!Blow.IsOnRecord() || Blow.KillingBlowDamage <= 0.0f || Blow.bDamageOverTime || Blow.bAreaSkillShareOnAnAlly)
	{
		return 0.0f;
	}

	const UCataclysmSkillTemplate* Skill = Cast<const UCataclysmSkillTemplate>(Blow.Skill.Get());
	if (!Skill || Skill->Params.KillFervour <= 0.0f)
	{
		// The ordinary case: every skill but the few Death rows that state it, and every blow no skill dealt.
		return 0.0f;
	}

	// A FLICKER COUNTING DEATHS IS PAID FOR EACH DEATH DURING IT, by the flicker, and not here for its own arrivals'.
	if (CountsTheDeathsWhileItRuns(Skill->Params))
	{
		return 0.0f;
	}

	// ITS CASTER, STILL HERE, AND THE DYING CREATURE'S ENEMY: asked of the teams, as a necrosis kill's imp asks, since a
	// creature being marked dead is no target to `UCataclysmTargeting::IsHostileTo`.
	AActor* Caster = Skill->GetAvatarActorFromActorInfo();
	if (!IsValid(Caster) || UCataclysmTeams::AttitudeBetween(Caster, Dying) != ETeamAttitude::Hostile)
	{
		return 0.0f;
	}

	// ONE DEATH PAYS ONE SKILL ONCE. Death Warrant's own blow killing its warranted target, or a second Hungering Soul
	// killing the host of the first, is paid by the mark, which pays whoever's blow it was.
	if (AMarkOfTheSameSkillPaysFor(Dying, Caster, Skill->SkillName))
	{
		return 0.0f;
	}

	const float Paid = UCataclysmFervour::GainStated(Skill->GetAbilitySystemComponentFromActorInfo(),
													 Skill->Params.KillFervour, /*bForAKill=*/true);
	UE_LOG(LogCataclysm, Display,
		TEXT("%s's blow killed %s: %.1f Fervour paid of the %.1f its row states for a kill."),
		*Skill->SkillName, *GetNameSafe(Dying), Paid, Skill->Params.KillFervour);
	return Paid;
}

bool UCataclysmKillFervour::CarriesACurseLaidBy(const AActor* Who, const TArray<FGameplayTag>& Curses, const AActor* By)
{
	const UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Who);
	if (!Theirs || !By)
	{
		return false;
	}

	FGameplayTagContainer Wanted;
	for (const FGameplayTag& Curse : Curses)
	{
		if (Curse.IsValid())
		{
			Wanted.AddTag(Curse);
		}
	}
	if (Wanted.IsEmpty())
	{
		return false;
	}

	// THE RUNNING EFFECTS GRANTING ONE OF THEM, AND WHO LAID EACH: the effect names its applier as its instigator, the
	// record `UCataclysmRisenImps::CurserOf` and `UCataclysmDebuffs::HoldAppliedNearbyStep` read.
	const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Wanted);
	for (const FActiveGameplayEffectHandle& Handle : Theirs->GetActiveEffects(Query))
	{
		const FActiveGameplayEffect* Active = Theirs->GetActiveGameplayEffect(Handle);
		if (Active && Active->Spec.GetContext().GetInstigator() == By)
		{
			return true;
		}
	}
	return false;
}

bool UCataclysmKillFervour::AMarkOfTheSameSkillPaysFor(const AActor* Dying, const AActor* Caster,
													   const FString& SkillName)
{
	if (!Dying || !Caster)
	{
		return false;
	}

	// A PAYING CURSE THAT STILL HOLDS, which `UCataclysmCurseSpread::SpreadFromDying` pays.
	if (const UCataclysmCurseSpread* Curse = Dying->FindComponentByClass<UCataclysmCurseSpread>())
	{
		if (Curse->KillFervour > 0.0f && Curse->Caster.Get() == Caster && Curse->SkillName == SkillName
			&& CarriesACurseLaidBy(Dying, Curse->Curses, Caster))
		{
			return true;
		}
	}

	// A BURIED WEAPON CARRYING IT, which `UCataclysmBuriedWeapon::LeapFromDying` pays for the host it leaves dead.
	if (const UCataclysmBuriedWeapon* Soul = Dying->FindComponentByClass<UCataclysmBuriedWeapon>())
	{
		if (Soul->KillFervour > 0.0f && Soul->RangeCm > 0.0f && Soul->Caster.Get() == Caster
			&& Soul->SkillName == SkillName)
		{
			return true;
		}
	}
	return false;
}

bool UCataclysmKillFervour::CountsTheDeathsWhileItRuns(const FCataclysmSkillShapeParams& Params)
{
	return Params.MovementMode == ECataclysmMovementMode::Flicker
		&& Params.ScalingSource.Equals(TEXT("Kill"), ESearchCase::IgnoreCase);
}
