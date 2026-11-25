// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmRisingCurse.h"

#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmKillFervour.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Cataclysm.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UCataclysmRisingCurse* UCataclysmRisingCurse::MarkOn(AActor* Target, AActor* Curser, const FString& RisesAs,
													 const FString& DamageType, const TArray<FGameplayTag>& Laid)
{
	if (!IsValid(Target) || !IsValid(Curser) || RisesAs.IsEmpty())
	{
		return nullptr;
	}

	// THE ROW THAT RAISES THE KIND, READ NOW, while the curse is laid, so the death needs no table. The generator refuses
	// a `RisesAs` no row raises, so missing here means the table was edited outside it, and is said out loud.
	FCataclysmWeaponSkill Row;
	if (!FindTheRowThatRaises(DamageType, RisesAs, Row))
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("'%s' was cursed to rise as a %s, and no %s Summon row raises a %s with a Duration, so nothing will rise "
				 "from it."),
			*GetNameSafe(Target), *RisesAs, *DamageType, *RisesAs);
		return nullptr;
	}

	// REFRESHED RATHER THAN ADDED TO, the one-stack rule `UCataclysmCurseSpread::MarkOn` follows.
	UCataclysmRisingCurse* Mark = Target->FindComponentByClass<UCataclysmRisingCurse>();
	if (!Mark)
	{
		Mark = NewObject<UCataclysmRisingCurse>(Target);
		if (!Mark)
		{
			return nullptr;
		}
		Mark->RegisterComponent();
	}

	Mark->Caster = Curser;
	Mark->Kind = RisesAs;
	Mark->FromRow = Row.Name;
	Mark->LifetimeSeconds = Row.Params.Duration;
	Mark->StatedReserve = Row.Params.FervourReserve;
	Mark->RowTags = Row.Tags;
	Mark->RadiusCm = Row.Params.RadiusCm;
	Mark->bTaunts = Row.Params.bTaunts;
	Mark->bBurns = Row.Params.bBurns;
	Mark->Curses = Laid;
	return Mark;
}

bool UCataclysmRisingCurse::FindTheRowThatRaises(const FString& DamageType, const FString& RisesAs,
												 FCataclysmWeaponSkill& OutRow)
{
	for (const FCataclysmWeaponSkill& Skill :
		 UCataclysmWeaponSkills::SkillsOfDamageType(UCataclysmWeaponSkills::LoadGeneratedTable(), DamageType))
	{
		if (Skill.Shape != ECataclysmSkillShape::Summon || Skill.Params.bPossess || Skill.Params.Duration <= 0.0f)
		{
			continue;
		}
		for (const FCataclysmMinionSpawn& Spawn : Skill.Params.Minions)
		{
			if (Spawn.Type.Equals(RisesAs, ESearchCase::IgnoreCase))
			{
				OutRow = Skill;
				return true;
			}
		}
	}
	return false;
}

ACataclysmMinion* UCataclysmRisingCurse::RiseFromDying(AActor* Dying)
{
	if (!IsValid(Dying))
	{
		return nullptr;
	}

	const UCataclysmRisingCurse* Mark = Dying->FindComponentByClass<UCataclysmRisingCurse>();
	if (!Mark || Mark->Kind.IsEmpty() || Mark->LifetimeSeconds <= 0.0f)
	{
		// The ordinary case: every creature no rising curse was laid on.
		return nullptr;
	}

	// ITS CURSER, STILL HERE AND ALIVE: a minion fights for somebody.
	AActor* Curser = Mark->Caster.Get();
	if (!IsValid(Curser) || UCataclysmSkillEffects::IsDead(Curser))
	{
		return nullptr;
	}

	// "ANY THAT DIES WHILE IT RATTLES": a curse that ran out or was cleansed raises nothing.
	if (!UCataclysmKillFervour::CarriesACurseLaidBy(Dying, Mark->Curses, Curser))
	{
		return nullptr;
	}

	// THE POOL MUST HOLD ITS RESERVE, the row's, after Crowned, as a risen imp's must. When it cannot, nothing rises
	// and nothing is destroyed. Issue #2787.
	const float Reserve = UCataclysmCommand::ReserveFor(Curser, Mark->StatedReserve, Mark->RowTags);
	if (!UCataclysmCommand::HasRoomToReserve(Curser, Reserve))
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("'%s' died under a rising curse, and its curser's pool has no room for a %s reserving %.0f Fervour, so "
				 "nothing rose."),
			*GetNameSafe(Dying), *Mark->Kind, Reserve);
		return nullptr;
	}

	// WHERE IT DIED, FOR THE ROW'S SECONDS. The lifetime is the row's own, as a risen imp's is Summon Imp's.
	ACataclysmMinion* Risen =
		ACataclysmMinion::Spawn(Curser, Dying->GetActorLocation(), Mark->LifetimeSeconds, Mark->bBurns, Mark->Kind);
	if (!Risen)
	{
		return nullptr;
	}

	// WHAT ITS EXPLOSION REACHES, THE "RISEN" LABEL, ITS RESERVE, AND ITS TAUNT, each as the row's own raise has them:
	// "turns the enemies around it on itself", C3, once a second from the moment it rises.
	Risen->RecordExplosionRadius(Mark->RadiusCm);
	if (const UWorld* World = Risen->GetWorld())
	{
		Risen->RisenAtSeconds = World->GetTimeSeconds();
	}
	UCataclysmCommand::HoldReserve(Curser, Risen, Reserve);
	if (Mark->bTaunts)
	{
		Risen->BeginTaunting(Mark->RadiusCm, ACataclysmMinion::RowTauntEverySeconds);
	}

	UE_LOG(LogCataclysm, Display,
		TEXT("%s died under a rising curse and rose as %s's %s for %s, lasting %.1f s and reserving %.0f Fervour."),
		*GetNameSafe(Dying), *Mark->FromRow, *Mark->Kind, *GetNameSafe(Curser), Mark->LifetimeSeconds, Reserve);
	return Risen;
}
