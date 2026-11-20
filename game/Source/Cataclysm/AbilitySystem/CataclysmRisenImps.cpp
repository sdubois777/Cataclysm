// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmRisenImps.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"
#include "Cataclysm.h"

const TCHAR* UCataclysmRisenImps::RisesStat = TEXT("curse_death_raises_imp");
const TCHAR* UCataclysmRisenImps::ImpSkillWeapon = TEXT("Staff");
const TCHAR* UCataclysmRisenImps::ImpSkillDamageType = TEXT("Demonic");
const TCHAR* UCataclysmRisenImps::ImpSkillName = TEXT("Summon Imp");
const TCHAR* UCataclysmRisenImps::NecrosisRiseSecondsStat =
	TEXT("necrosis_kill_raises_imp_seconds");

namespace
{
	/** Whether this character holds the rule. */
	bool HoldsTheRule(const AActor* Character)
	{
		const UCataclysmAbilitySystemComponent* Cataclysm =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Character));
		return Cataclysm
			&& Cataclysm->StatForSkill(FName(UCataclysmRisenImps::RisesStat),
									   FGameplayTagContainer(), 0.0f) > 0.0f;
	}
}

AActor* UCataclysmRisenImps::CurserOf(const AActor* Dying)
{
	UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Dying);
	const FGameplayTag Curses = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Status.Debuff")), /*ErrorIfNotFound=*/false);
	if (!Theirs || !Curses.IsValid())
	{
		return nullptr;
	}

	// EVERY `Status.Debuff.*` IS A CURSE, ruled 2026-09-27 under the owner's
	// delegation: the Wand's Shred and Madness, and Quarry and the rest as well.
	// A tag query on the parent matches every child of it.
	const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
		FGameplayTagContainer(Curses));
	for (const FActiveGameplayEffectHandle& Handle : Theirs->GetActiveEffects(Query))
	{
		const FActiveGameplayEffect* Active = Theirs->GetActiveGameplayEffect(Handle);
		if (!Active)
		{
			continue;
		}

		// "A CURSE YOU LAID": the effect names its applier as its instigator,
		// the same record `UCataclysmDebuffs::HoldAppliedNearbyStep` reads.
		AActor* Applier = Active->Spec.GetContext().GetInstigator();
		if (IsValid(Applier) && Applier != Dying && HoldsTheRule(Applier))
		{
			return Applier;
		}
	}
	return nullptr;
}

ACataclysmMinion* UCataclysmRisenImps::RiseOnDeath(AActor* Dying)
{
	AActor* Curser = CurserOf(Dying);
	if (!Curser)
	{
		return nullptr;
	}

	const TArray<FCataclysmWeaponSkill> Staff = UCataclysmWeaponSkills::SkillsFor(
		UCataclysmWeaponSkills::LoadGeneratedTable(), ImpSkillWeapon, ImpSkillDamageType);
	const FCataclysmWeaponSkill* SummonImp = Staff.FindByPredicate(
		[](const FCataclysmWeaponSkill& Skill) { return Skill.Name == ImpSkillName; });
	if (!SummonImp || SummonImp->Params.Minions.IsEmpty())
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("'%s' died cursed by '%s', and nothing rose: the weapon skill table "
				 "has no '%s' naming a minion to raise."),
			*GetNameSafe(Dying), *Curser->GetName(), ImpSkillName);
		return nullptr;
	}

	const FString Type = SummonImp->Params.Minions[0].Type;

	// THE POOL MUST HOLD ITS RESERVE, Summon Imp's from Summon Imp's row, after Crowned, AND THAT IS THE ONLY LIMIT on
	// how many rise. Issue #2787, the owner's rulings of 2026-11-20: every minion reserves, and nothing that reserves
	// has a count cap, so the cap risen imps shared with Summon Imp (#1479) is gone. When the pool cannot hold it,
	// nothing rises and nothing is destroyed.
	const float Reserve =
		UCataclysmCommand::ReserveFor(Curser, SummonImp->Params.FervourReserve, SummonImp->Tags);
	if (!UCataclysmCommand::HasRoomToReserve(Curser, Reserve))
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("'%s' died cursed by '%s', whose pool has no room for an imp reserving %.0f Fervour, so "
				 "nothing rose."),
			*GetNameSafe(Dying), *Curser->GetName(), Reserve);
		return nullptr;
	}

	const float Lifetime =
		SummonImp->Params.Duration > 0.0f ? SummonImp->Params.Duration : 20.0f;
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Curser, Dying->GetActorLocation(), Lifetime, SummonImp->Params.bBurns, Type);
	if (Imp)
	{
		Imp->RecordExplosionRadius(SummonImp->Params.RadiusCm);
		if (const UWorld* World = Imp->GetWorld())
		{
			Imp->RisenAtSeconds = World->GetTimeSeconds();
		}
		UCataclysmCommand::HoldReserve(Curser, Imp, Reserve);
	}
	return Imp;
}

ACataclysmMinion* UCataclysmRisenImps::RiseOnNecrosisKill(
	AActor* Killer, const AActor* Victim, const FVector& Where,
	const FGameplayTagContainer* KillingTags)
{
	const UCataclysmAbilitySystemComponent* Theirs =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Killer));
	if (!Theirs || !Victim || !KillingTags)
	{
		return nullptr;
	}
	const float Seconds = Theirs->StatForSkill(
		FName(NecrosisRiseSecondsStat), FGameplayTagContainer(), 0.0f);
	if (Seconds <= 0.0f)
	{
		return nullptr;
	}

	const FGameplayTag Necrosis = UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Keyword.DoT.Necrosis")), /*ErrorIfNotFound=*/false);
	// AN ENEMY, ASKED OF THE TEAMS AND NOT OF `UCataclysmTargeting::IsHostileTo`,
	// which answers no for anything dead -- and the victim is dead by now.
	if (!Necrosis.IsValid() || !KillingTags->HasTagExact(Necrosis)
		|| UCataclysmTeams::AttitudeBetween(Killer, Victim) != ETeamAttitude::Hostile)
	{
		return nullptr;
	}

	const TArray<FCataclysmWeaponSkill> Staff = UCataclysmWeaponSkills::SkillsFor(
		UCataclysmWeaponSkills::LoadGeneratedTable(), ImpSkillWeapon, ImpSkillDamageType);
	const FCataclysmWeaponSkill* SummonImp = Staff.FindByPredicate(
		[](const FCataclysmWeaponSkill& Skill) { return Skill.Name == ImpSkillName; });
	if (!SummonImp || SummonImp->Params.Minions.IsEmpty())
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("'%s' killed '%s' with necrosis, and nothing rose: the weapon skill "
				 "table has no '%s' naming a minion to raise."),
			*Killer->GetName(), *GetNameSafe(Victim), ImpSkillName);
		return nullptr;
	}

	// AND THE POOL MUST HOLD ITS OWN RESERVE, issue #2787: not Summon Imp's 10, since it lasts 5-10 seconds against
	// Summon Imp's 20. When it cannot, nothing rises.
	const FString Type = SummonImp->Params.Minions[0].Type;
	const float Reserve = UCataclysmCommand::ReserveFor(
		Killer, NecrosisImpFervourReserve, ACataclysmMinion::TagsOfType(Type));
	if (!UCataclysmCommand::HasRoomToReserve(Killer, Reserve))
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("'%s' killed '%s' with necrosis, and its pool has no room for an imp reserving %.0f Fervour, so "
				 "nothing rose."),
			*Killer->GetName(), *GetNameSafe(Victim), Reserve);
		return nullptr;
	}

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Killer, Where, Seconds, SummonImp->Params.bBurns, Type);
	if (Imp)
	{
		Imp->RecordExplosionRadius(SummonImp->Params.RadiusCm);
		if (const UWorld* World = Imp->GetWorld())
		{
			Imp->RisenAtSeconds = World->GetTimeSeconds();
		}
		UCataclysmCommand::HoldReserve(Killer, Imp, Reserve);
	}
	return Imp;
}

bool UCataclysmRisenImps::ShowsRisen(const AActor* Actor)
{
	const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Actor);
	if (!IsValid(Minion) || Minion->RisenAtSeconds < 0.0f)
	{
		return false;
	}
	const UWorld* World = Minion->GetWorld();
	return World && World->GetTimeSeconds() - Minion->RisenAtSeconds < RisenLabelSeconds;
}
