// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmVoidAura.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"

const TCHAR* UCataclysmVoidAura::ConsumeChanceStat = TEXT("void_aura_projectile_consume_chance");
const TCHAR* UCataclysmVoidAura::StacksName = TEXT("Void");

static TAutoConsoleVariable<float> CVarVoidAuraConsumeRoll(
	TEXT("Cataclysm.VoidAuraConsumeRoll"), -1.0f,
	TEXT("Pins the 0-100 roll a void aura makes against its chance to consume an enemy projectile. Negative rolls for "
		 "real."),
	ECVF_Default);

namespace
{
	/** Every ability system that has gained a kept stack, weakly. See the class comment. */
	TArray<TWeakObjectPtr<UCataclysmAbilitySystemComponent>>& VoidAuraRegister()
	{
		static TArray<TWeakObjectPtr<UCataclysmAbilitySystemComponent>> Held;
		return Held;
	}
}

float UCataclysmVoidAura::ConsumeChanceOf(const UCataclysmAbilitySystemComponent* Wearer)
{
	// TEN VOID FIRST, read now, so a count spent, cleared at a death or left in another dungeon is no aura.
	if (!Wearer || Wearer->KeptStacksHeldNamed(StacksName) < StacksNeeded)
	{
		return 0.0f;
	}
	return FMath::Clamp(Wearer->StatForSkill(FName(ConsumeChanceStat), FGameplayTagContainer(), 0.0f), 0.0f, 100.0f);
}

bool UCataclysmVoidAura::IsUp(const UCataclysmAbilitySystemComponent* Wearer)
{
	return ConsumeChanceOf(Wearer) > 0.0f;
}

void UCataclysmVoidAura::NoteMayHoldIt(UCataclysmAbilitySystemComponent* Wearer)
{
	if (!Wearer)
	{
		return;
	}
	TArray<TWeakObjectPtr<UCataclysmAbilitySystemComponent>>& Held = VoidAuraRegister();
	// GONE ONES LEAVE HERE, so the register never holds more than the ability systems still alive that gained a stack.
	Held.RemoveAll([](const TWeakObjectPtr<UCataclysmAbilitySystemComponent>& One) { return !One.IsValid(); });
	Held.AddUnique(TWeakObjectPtr<UCataclysmAbilitySystemComponent>(Wearer));
}

const UCataclysmAbilitySystemComponent* UCataclysmVoidAura::AuraThatReaches(const AActor* Firer, const UWorld* World,
																			 const FVector& From, const FVector& To)
{
	if (!Firer || !World)
	{
		return nullptr;
	}
	// ON THE GROUND PLANE, as the flight's range and a pull are measured, so a lob high over the wearer is in reach.
	const FVector FlatFrom(From.X, From.Y, 0.0f);
	const FVector FlatTo(To.X, To.Y, 0.0f);
	const UCataclysmAbilitySystemComponent* Nearest = nullptr;
	float NearestCm = 0.0f;
	for (const TWeakObjectPtr<UCataclysmAbilitySystemComponent>& One : VoidAuraRegister())
	{
		const UCataclysmAbilitySystemComponent* Wearer = One.Get();
		const AActor* Avatar = Wearer ? Wearer->GetAvatarActor() : nullptr;
		if (!IsValid(Avatar) || Avatar->GetWorld() != World)
		{
			continue;
		}
		const FVector At = Avatar->GetActorLocation();
		const float Cm = FMath::PointDistToSegment(FVector(At.X, At.Y, 0.0f), FlatFrom, FlatTo);
		if (Cm > RadiusCm || (Nearest && Cm >= NearestCm))
		{
			continue;
		}
		// AN ENEMY'S SHOT AND NOTHING ELSE: the wearer must be something the firer's skills may hit. The wearer's own
		// shot, an ally's and a minion's are not, nor is anything a dead wearer stands for.
		if (!UCataclysmTargeting::IsHostileTo(Avatar, Firer) || !IsUp(Wearer))
		{
			continue;
		}
		Nearest = Wearer;
		NearestCm = Cm;
	}
	return Nearest;
}

bool UCataclysmVoidAura::RollConsumes(float ChancePercent)
{
	if (ChancePercent <= 0.0f)
	{
		return false;
	}
	const float Pinned = CVarVoidAuraConsumeRoll.GetValueOnAnyThread();
	const float Roll = Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	return ChancePercent >= 100.0f || Roll < ChancePercent;
}
