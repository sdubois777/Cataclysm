// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmHordeAscendant.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Cataclysm.h"
#include "Character/CataclysmEnemyController.h"
#include "GameFramework/Pawn.h"

const TCHAR* UCataclysmHordeAscendant::ReachMetresStat = TEXT("minions_cast_your_skills_within_metres");
const TCHAR* UCataclysmHordeAscendant::DamagePercentStat = TEXT("minion_skill_copy_damage_percent");

namespace
{
	float PlayersStat(const AActor* Player, const TCHAR* Stat)
	{
		const UCataclysmAbilitySystemComponent* Theirs =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Player));
		return Theirs ? Theirs->StatForSkill(FName(Stat), FGameplayTagContainer(), 0.0f) : 0.0f;
	}
}

bool UCataclysmHordeAscendant::Holds(const AActor* Player)
{
	return PlayersStat(Player, ReachMetresStat) > 0.0f;
}

int32 UCataclysmHordeAscendant::EveryFollowerNearCasts(AActor* Player, const FCataclysmWeaponSkill& Skill,
														const FVector& Aim)
{
	const float ReachCm = PlayersStat(Player, ReachMetresStat) * 100.0f;
	const float SharePercent = PlayersStat(Player, DamagePercentStat);
	if (!IsValid(Player) || UCataclysmSkillEffects::IsDead(Player) || ReachCm <= 0.0f || SharePercent <= 0.0f
		|| !UCataclysmTriggeredSkill::RepeatsFromARow(Skill))
	{
		return 0;
	}

	// GATHERED FIRST, then cast: a copy's blow may kill, and a kill may raise or end a follower.
	TArray<TWeakObjectPtr<AActor>> Casting;
	for (AActor* Follower : UCataclysmCommand::ThingsCommandedBy(Player, ReachCm))
	{
		const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower);
		if (Minion && Minion->bIsMachine)
		{
			continue;
		}
		Casting.Add(Follower);
	}

	int32 Casts = 0;
	for (const TWeakObjectPtr<AActor>& Each : Casting)
	{
		AActor* Follower = Each.Get();
		if (!IsValid(Follower) || UCataclysmSkillEffects::IsDead(Follower) || UCataclysmSkillEffects::CannotAct(Follower))
		{
			continue;
		}

		// AT ITS OWN TARGET, the one its brain has chosen, as a gadget's copy is aimed; with none, where the player aimed.
		FVector AimedAt = Aim;
		if (const APawn* Body = Cast<APawn>(Follower))
		{
			if (const ACataclysmEnemyController* Brain = Cast<ACataclysmEnemyController>(Body->GetController()))
			{
				AActor* Chosen = Brain->ChooseTarget();
				if (IsValid(Chosen) && !UCataclysmSkillEffects::IsDead(Chosen))
				{
					AimedAt = Chosen->GetActorLocation();
				}
			}
		}

		// THE PLAYER'S SKILL, granted to the player for one use, free, cast from the follower and naming it.
		const FVector From = Follower->GetActorLocation();
		if (UCataclysmTriggeredSkill::Trigger(Player, Skill, AimedAt, SharePercent / 100.0f, /*bPaysCost=*/false, &From,
											  /*CastBy=*/Follower))
		{
			++Casts;
		}
	}
	UE_LOG(LogCataclysm, Display, TEXT("THE HORDE ASCENDANT: %d of %s's followers within %.0f m cast %s at %.0f%%."), Casts,
		   *GetNameSafe(Player), ReachCm / 100.0f, *Skill.Name, SharePercent);
	return Casts;
}
