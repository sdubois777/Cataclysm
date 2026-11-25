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

bool UCataclysmRisenImps::CarriesCurseLaidBy(const AActor* Carrier, const AActor* Curser)
{
	if (!Carrier || !Curser)
	{
		return false;
	}
	UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Carrier);
	const FGameplayTag Curses = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Status.Debuff")), /*ErrorIfNotFound=*/false);
	if (!Theirs || !Curses.IsValid())
	{
		return false;
	}

	// THE SAME QUERY AND THE SAME RECORD `CurserOf` READS: every `Status.Debuff.*`, and the instigator its effect
	// names. Only the question differs: whether THIS character laid one, not who of the rule's holders did.
	const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
		FGameplayTagContainer(Curses));
	for (const FActiveGameplayEffectHandle& Handle : Theirs->GetActiveEffects(Query))
	{
		const FActiveGameplayEffect* Active = Theirs->GetActiveGameplayEffect(Handle);
		if (Active && Active->Spec.GetContext().GetInstigator() == Curser)
		{
			return true;
		}
	}
	return false;
}

const TCHAR* UCataclysmRisenImps::ExtraImpsStat = TEXT("curse_death_extra_imps");

ACataclysmMinion* UCataclysmRisenImps::SummonImpKindFor(AActor* Commander, const FVector& Where, bool bRisen)
{
	if (!IsValid(Commander))
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
			TEXT("Nothing was made for '%s': the weapon skill table has no '%s' naming a minion to make."),
			*Commander->GetName(), ImpSkillName);
		return nullptr;
	}

	const FString Type = SummonImp->Params.Minions[0].Type;

	// THE POOL MUST HOLD ITS RESERVE, Summon Imp's from Summon Imp's row, after Crowned, AND THAT IS THE ONLY LIMIT on
	// how many are made. Issue #2787, the owner's rulings of 2026-11-20: every minion reserves, and nothing that
	// reserves has a count cap, so the cap risen imps shared with Summon Imp (#1479) is gone. When the pool cannot hold
	// it, nothing is made and nothing is destroyed.
	const float Reserve =
		UCataclysmCommand::ReserveFor(Commander, SummonImp->Params.FervourReserve, SummonImp->Tags);
	if (!UCataclysmCommand::HasRoomToReserve(Commander, Reserve))
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("'%s' has no room in its pool for an imp reserving %.0f Fervour, so none was made."),
			*Commander->GetName(), Reserve);
		return nullptr;
	}

	const float Lifetime =
		SummonImp->Params.Duration > 0.0f ? SummonImp->Params.Duration : 20.0f;
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Commander, Where, Lifetime, SummonImp->Params.bBurns, Type);
	if (Imp)
	{
		Imp->RecordExplosionRadius(SummonImp->Params.RadiusCm);
		if (bRisen)
		{
			if (const UWorld* World = Imp->GetWorld())
			{
				Imp->RisenAtSeconds = World->GetTimeSeconds();
			}
		}
		UCataclysmCommand::HoldReserve(Commander, Imp, Reserve);
	}
	return Imp;
}

ACataclysmMinion* UCataclysmRisenImps::RiseOnDeath(AActor* Dying)
{
	AActor* Curser = CurserOf(Dying);
	if (!Curser)
	{
		return nullptr;
	}

	// ONE, AND OPEN CHANNEL'S EXTRA. The demonic tree rework of 2026-11-24: "An enemy that dies carrying your curse
	// raises two imps rather than one", a flat 1 on the curser. WHOLE IMPS, ROUNDED DOWN, so a reading a hair under a
	// whole number from summed rows is not a lost imp.
	int32 Count = 1;
	if (const UCataclysmAbilitySystemComponent* Theirs = Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Curser)))
	{
		const float Extra = Theirs->StatForSkill(FName(ExtraImpsStat), FGameplayTagContainer(), 0.0f);
		Count += FMath::Max(0, FMath::FloorToInt(Extra + KINDA_SMALL_NUMBER));
	}

	// EACH ASKS THE POOL FOR ITS OWN RESERVE, so a pool with room for one raises one and the second is refused, with
	// nothing destroyed: the owner's rule of 2026-11-20 that the reserve is the only limit. The first stands where the
	// creature died; each extra a little apart, on a circle round it, so two are never made in one place.
	const FVector DiedAt = Dying->GetActorLocation();
	ACataclysmMinion* First = nullptr;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Where = Index == 0
			? DiedAt
			: DiedAt + FRotator(0.0f, 120.0f * static_cast<float>(Index), 0.0f)
				  .RotateVector(FVector(ExtraImpSpacingCm, 0.0f, 0.0f));
		ACataclysmMinion* Imp = SummonImpKindFor(Curser, Where, /*bRisen=*/true);
		if (!Imp)
		{
			UE_LOG(LogCataclysm, Verbose,
				TEXT("'%s' died cursed by '%s', and imp %d of %d did not rise."),
				*GetNameSafe(Dying), *Curser->GetName(), Index + 1, Count);
			break;
		}
		if (!First)
		{
			First = Imp;
		}
	}
	return First;
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
