// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSiphon.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"

const TCHAR* FCataclysmSiphon::RadiusStat = TEXT("siphon_radius_metres");
const TCHAR* FCataclysmSiphon::EnemiesDealLessStat = TEXT("siphon_enemies_deal_less_percent");
const TCHAR* FCataclysmSiphon::SlowStat = TEXT("siphon_enemy_slow_percent");
const TCHAR* FCataclysmSiphon::LingerStat = TEXT("siphon_linger_seconds");
const TCHAR* FCataclysmSiphon::DrainStat = TEXT("siphon_drain_weapon_percent");
const TCHAR* FCataclysmSiphon::NecrosisChanceStat = TEXT("siphon_necrosis_chance");
const TCHAR* FCataclysmSiphon::LifeStat = TEXT("siphon_life_percent");
const TCHAR* FCataclysmSiphon::FervourStat = TEXT("siphon_fervour_per_second");

namespace CataclysmSiphonParts
{
	/** A stat of the holder's, asked with no skill's tags and nought when nothing grants it. */
	float HolderStat(const UAbilitySystemComponent* Holder, const TCHAR* Stat)
	{
		const UCataclysmAbilitySystemComponent* Cataclysm = Cast<const UCataclysmAbilitySystemComponent>(Holder);
		return Cataclysm ? Cataclysm->StatForSkill(FName(Stat), FGameplayTagContainer(), 0.0f) : 0.0f;
	}

	/** The members still in the world and alive, in the record's order. */
	TArray<AActor*> LivingMembers(const UCataclysmAbilitySystemComponent* System)
	{
		TArray<AActor*> Living;
		if (!System)
		{
			return Living;
		}
		for (const TPair<TWeakObjectPtr<AActor>, UCataclysmAbilitySystemComponent::FSiphonMember>& Each :
			 System->SiphonMembers())
		{
			AActor* Member = Each.Key.Get();
			if (Member && !UCataclysmSkillEffects::IsDead(Member))
			{
				Living.Add(Member);
			}
		}
		return Living;
	}
}

float FCataclysmSiphon::RadiusMetresOf(const UAbilitySystemComponent* Holder)
{
	return FMath::Max(0.0f, CataclysmSiphonParts::HolderStat(Holder, RadiusStat));
}

int32 FCataclysmSiphon::Step(ACataclysmCharacterBase* Holder, float StepSeconds)
{
	UCataclysmAbilitySystemComponent* System =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Holder));
	UWorld* World = Holder ? Holder->GetWorld() : nullptr;
	if (!System || !World)
	{
		return 0;
	}

	TMap<TWeakObjectPtr<AActor>, UCataclysmAbilitySystemComponent::FSiphonMember>& Members = System->SiphonMembers();
	const float Radius = RadiusMetresOf(System);

	// NO SIPHON, OR A DEAD HOLDER: NOBODY IS IN IT. "Without this keystone you have no Siphon."
	if (Radius <= 0.0f || UCataclysmSkillEffects::IsDead(Holder))
	{
		Members.Reset();
		System->SiphonPulseSeconds = 0.0f;
		return 0;
	}

	const float Now = World->GetTimeSeconds();

	// WHO IS WITHIN THE RADIUS NOW. An enemy new to it enters now; one already in it is seen again.
	TSet<const AActor*> SeenNow;
	for (AActor* Each : UCataclysmTargeting::FindEnemiesInSphere(World, Holder, Holder->GetActorLocation(),
																	   Radius * 100.0f))
	{
		if (!Each || UCataclysmSkillEffects::IsDead(Each))
		{
			continue;
		}
		SeenNow.Add(Each);
		if (UCataclysmAbilitySystemComponent::FSiphonMember* Known = Members.Find(Each))
		{
			Known->LastInsideAtSeconds = Now;
		}
		else
		{
			UCataclysmAbilitySystemComponent::FSiphonMember Fresh;
			Fresh.EnteredAtSeconds = Now;
			Fresh.LastInsideAtSeconds = Now;
			Members.Add(Each, Fresh);
		}
	}

	// WHO HAS GONE: dead, removed, or not within the radius this step and out of it for the linger or longer. With no
	// Lingering Pull an enemy that stepped out is gone at once, however little time has passed.
	const float Linger = FMath::Max(0.0f, CataclysmSiphonParts::HolderStat(System, LingerStat));
	for (auto It = Members.CreateIterator(); It; ++It)
	{
		const AActor* Member = It.Key().Get();
		const bool bGone = !Member || UCataclysmSkillEffects::IsDead(Member)
			|| (!SeenNow.Contains(Member) && Now - It.Value().LastInsideAtSeconds >= Linger);
		if (bGone)
		{
			It.RemoveCurrent();
		}
	}

	// WHAT EACH MEMBER CARRIES WHILE IT IS IN, renewed every step and held three steps past it.
	const float Less = CataclysmSiphonParts::HolderStat(System, EnemiesDealLessStat);
	const float Slow = CataclysmSiphonParts::HolderStat(System, SlowStat);
	if (Less > 0.0f || Slow > 0.0f)
	{
		for (AActor* Member : CataclysmSiphonParts::LivingMembers(System))
		{
			if (Less > 0.0f)
			{
				if (UCataclysmAbilitySystemComponent* Its =
						Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Member)))
				{
					Its->NoteSiphonDamageCut(Less, Now + HoldSeconds);
				}
			}
			if (Slow > 0.0f)
			{
				if (ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Member))
				{
					Enemy->NoteGroundDown(Slow, Now + HoldSeconds);
				}
			}
		}
	}

	// AND ONCE A SECOND, WHAT EACH SECOND PAYS (ruled 2026-11-25).
	System->SiphonPulseSeconds += FMath::Max(0.0f, StepSeconds);
	if (System->SiphonPulseSeconds >= 1.0f - KINDA_SMALL_NUMBER)
	{
		System->SiphonPulseSeconds = FMath::Max(0.0f, System->SiphonPulseSeconds - 1.0f);
		Pulse(Holder);
	}
	return CataclysmSiphonParts::LivingMembers(System).Num();
}

void FCataclysmSiphon::Pulse(ACataclysmCharacterBase* Holder)
{
	UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Holder);
	DrainOnce(Holder);
	NecrosisOnce(Holder);
	LifeOnce(System);
	FervourOnce(System);
}

float FCataclysmSiphon::DrainOnce(ACataclysmCharacterBase* Holder)
{
	const UCataclysmAbilitySystemComponent* System =
		Cast<const UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Holder));
	const float Percent = CataclysmSiphonParts::HolderStat(System, DrainStat);
	if (!System || Percent <= 0.0f)
	{
		return 0.0f;
	}
	const float Base = UCataclysmSkillEffects::WeaponDamageOf(System) * Percent / 100.0f;
	if (Base <= 0.0f)
	{
		return 0.0f;
	}

	// DAMAGE OVER TIME AS DEATH DAMAGE, priced through the holder's own damage rows with each member as the target, and
	// delivered as a damage over time's tick is: it cannot strike critically, carries no ailment chance and is not
	// retaliated against (judgements, each the way this game's other damage over time is delivered).
	FCataclysmHitDelivery Delivery;
	Delivery.DamageType = FName(TEXT("Death"));
	Delivery.bIsDamageOverTime = true;
	Delivery.bCannotCriticallyStrike = true;
	Delivery.bCarriesNoAilmentChance = true;
	Delivery.bCannotBeRetaliatedAgainst = true;

	float Sent = 0.0f;
	for (AActor* Member : CataclysmSiphonParts::LivingMembers(System))
	{
		const float Landed = UCataclysmSkillEffects::ModifiedDamage(
			System, Base, FGameplayTagContainer(), /*SkillHealthCostPercent=*/-1.0f, /*MetresMovedBeforeBlow=*/-1.0f,
			/*TargetDistanceMetres=*/-1.0f, /*bTargetIsStaggered=*/false, Member);
		if (Landed > 0.0f && UCataclysmSkillEffects::ApplyDirectDamage(Holder, Member, Landed, Delivery))
		{
			Sent += Landed;
		}
	}
	return Sent;
}

int32 FCataclysmSiphon::NecrosisOnce(ACataclysmCharacterBase* Holder)
{
	const UCataclysmAbilitySystemComponent* System =
		Cast<const UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Holder));
	const float Chance = CataclysmSiphonParts::HolderStat(System, NecrosisChanceStat);
	const UWorld* World = Holder ? Holder->GetWorld() : nullptr;
	const FCataclysmAilmentKind* Necrosis = UCataclysmAilments::KindNamed(TEXT("Necrosis"));
	if (!System || !World || !Necrosis || Chance <= 0.0f)
	{
		return 0;
	}

	const float Now = World->GetTimeSeconds();
	int32 Given = 0;
	for (const TPair<TWeakObjectPtr<AActor>, UCataclysmAbilitySystemComponent::FSiphonMember>& Each :
		 System->SiphonMembers())
	{
		AActor* Member = Each.Key.Get();
		if (!Member || UCataclysmSkillEffects::IsDead(Member)
			|| Now - Each.Value.EnteredAtSeconds < NecrosisAfterSeconds)
		{
			continue;
		}
		// A CHANCE OF 100 OR MORE ALWAYS GIVES IT, and nothing below nought does.
		if (Chance >= 100.0f || FMath::FRand() * 100.0f < Chance)
		{
			Given += UCataclysmAilments::Apply(Holder, Member, *Necrosis, /*Magnitude=*/1.0f) ? 1 : 0;
		}
	}
	return Given;
}

float FCataclysmSiphon::LifeOnce(UAbilitySystemComponent* Holder)
{
	const float Percent = CataclysmSiphonParts::HolderStat(Holder, LifeStat);
	if (!Holder || Percent <= 0.0f)
	{
		return 0.0f;
	}
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const float Maximum = Holder->GetNumericAttribute(MaxHealth);
	const float Before = Holder->GetNumericAttribute(Health);
	if (Maximum <= 0.0f || Before >= Maximum)
	{
		return 0.0f;
	}
	// THROUGH `TopUp`, as every heal is, so the ceiling and the healing-received rules apply.
	UCataclysmRegeneration::TopUp(*Holder, Health, MaxHealth, Maximum * Percent / 100.0f);
	return Holder->GetNumericAttribute(Health) - Before;
}

float FCataclysmSiphon::FervourOnce(UAbilitySystemComponent* Holder)
{
	const float Amount = CataclysmSiphonParts::HolderStat(Holder, FervourStat);
	const UCataclysmClassResourceAttributeSet* Resource =
		Holder ? Holder->GetSet<UCataclysmClassResourceAttributeSet>() : nullptr;
	if (!Resource || Amount <= 0.0f)
	{
		return 0.0f;
	}
	// AT THE GENERATION RATE AND CLAMPED AT THE MAXIMUM LESS THE RESERVE, the rule every gain follows. NOT A KILL: it
	// stamps no kill clock, so "on its own it does not hold off decay", as Living Harvest says.
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = Holder->GetNumericAttribute(Pool);
	const float Change =
		FMath::Clamp(Before + UCataclysmAbilitySystemComponent::ClassResourceGainScaled(Holder, Amount), 0.0f,
					 Resource->MaximumClassResourceAsked())
		- Before;
	if (FMath::IsNearlyZero(Change))
	{
		return 0.0f;
	}
	Holder->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);
	return Holder->GetNumericAttribute(Pool) - Before;
}

FString FCataclysmSiphon::DisplayLine(const UCataclysmAbilitySystemComponent* Holder)
{
	const float Radius = RadiusMetresOf(Holder);
	if (!Holder || Radius <= 0.0f)
	{
		return FString();
	}
	return FString::Printf(TEXT("Siphon: %d within %.1f m"), Holder->EnemiesInSiphon(), Radius);
}
