// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmTimeLockRift.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmDamageTakenMultiplier.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"

const TCHAR* UCataclysmTimeLockRift::EverySecondsStat = TEXT("time_lock_rift_every_seconds");
const TCHAR* UCataclysmTimeLockRift::RadiusMetresStat = TEXT("time_lock_rift_radius_metres");
const TCHAR* UCataclysmTimeLockRift::BaseSecondsStat = TEXT("time_lock_rift_seconds");
const TCHAR* UCataclysmTimeLockRift::SecondsPerEnemyStat = TEXT("time_lock_rift_seconds_per_enemy");
const TCHAR* UCataclysmTimeLockRift::MostSecondsStat = TEXT("time_lock_rift_most_seconds");
const TCHAR* UCataclysmTimeLockRift::StunSecondsStat = TEXT("time_lock_rift_stun_seconds");
const TCHAR* UCataclysmTimeLockRift::DamageTakenMorePercentStat = TEXT("time_lock_rift_damage_taken_more_percent");

namespace
{
	/**
	 * One wearer's clock and the rift standing for it: when the next is ready, the rift, when it appeared and its
	 * whole length then, in the world's seconds, and every enemy it has caught.
	 */
	struct FTimeLockRiftState
	{
		float ReadyAtSeconds = 0.0f;
		TWeakObjectPtr<ACataclysmGroundZone> Rift;
		float AppearedAtSeconds = 0.0f;
		float LengthSeconds = 0.0f;
		TArray<TWeakObjectPtr<AActor>> Caught;
	};

	/**
	 * Every armed wearer's clock, keyed weakly. HELD HERE AND NOT ON THE ABILITY SYSTEM, as `UCataclysmGroundPound`
	 * holds its clocks, so this bonus adds no member to that shared class. A gone wearer's is dropped when the next
	 * clock is started.
	 */
	TMap<TWeakObjectPtr<UCataclysmAbilitySystemComponent>, FTimeLockRiftState>& TimeLockRifts()
	{
		static TMap<TWeakObjectPtr<UCataclysmAbilitySystemComponent>, FTimeLockRiftState> Held;
		return Held;
	}

	float TimeLockRiftWorldSecondsOf(const UObject* Thing)
	{
		const UWorld* World = Thing ? Thing->GetWorld() : nullptr;
		return World ? World->GetTimeSeconds() : 0.0f;
	}

	/** The state of `Wearer`'s rift, or null. */
	const FTimeLockRiftState* TimeLockRiftStateOf(const UCataclysmAbilitySystemComponent* Wearer)
	{
		return Wearer ? TimeLockRifts().Find(TWeakObjectPtr<UCataclysmAbilitySystemComponent>(
							const_cast<UCataclysmAbilitySystemComponent*>(Wearer)))
					  : nullptr;
	}

	/** Whether the rift this state holds is standing now. */
	bool TimeLockRiftStands(const FTimeLockRiftState& State, float Now)
	{
		return State.Rift.IsValid() && Now < State.AppearedAtSeconds + State.LengthSeconds;
	}

	/**
	 * The whole length of a rift that has caught `CaughtCount` enemies: the base and the seconds per enemy, AT MOST
	 * THE MOST STATED, the whole of it and not only what was added (K1). With no most stated it has no bound.
	 */
	float TimeLockRiftLengthFor(const UCataclysmAbilitySystemComponent* Wearer, int32 CaughtCount)
	{
		const float Grown = UCataclysmTimeLockRift::StatOf(Wearer, UCataclysmTimeLockRift::BaseSecondsStat)
			+ FMath::Max(0.0f, UCataclysmTimeLockRift::StatOf(Wearer, UCataclysmTimeLockRift::SecondsPerEnemyStat))
				* CaughtCount;
		const float Most = UCataclysmTimeLockRift::StatOf(Wearer, UCataclysmTimeLockRift::MostSecondsStat);
		return Most > 0.0f ? FMath::Min(Grown, Most) : Grown;
	}

	/** The longest time left on a stun `Target` carries: the stun as it was laid, after every rule that moved it. */
	float TimeLockRiftStunLeftOn(const AActor* Target)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Target);
		const FGameplayTag Stunned = UCataclysmSkillEffects::StunnedTag();
		float Longest = 0.0f;
		if (System && Stunned.IsValid())
		{
			for (const float Left : System->GetActiveEffectsTimeRemaining(
					 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Stunned))))
			{
				Longest = FMath::Max(Longest, Left);
			}
		}
		return Longest;
	}

	/**
	 * Catch every enemy of `Inside` this rift has not caught before (K5, #10): stunned by the wearer, and, where the
	 * stun lands, held to take more damage for as long as the stun lasts (K7).
	 *
	 * @return how many it caught now
	 */
	int32 TimeLockRiftCatch(AActor* Character, const UCataclysmAbilitySystemComponent* Wearer,
							FTimeLockRiftState& State, const TArray<AActor*>& Inside)
	{
		const float StunSeconds = UCataclysmTimeLockRift::StatOf(Wearer, UCataclysmTimeLockRift::StunSecondsStat);
		const float MorePercent =
			UCataclysmTimeLockRift::StatOf(Wearer, UCataclysmTimeLockRift::DamageTakenMorePercentStat);
		int32 Newly = 0;
		for (AActor* Other : Inside)
		{
			// ONCE PER RIFT, whether it stayed inside or left and came back (#10).
			if (!IsValid(Other) || State.Caught.Contains(Other))
			{
				continue;
			}
			State.Caught.Add(Other);
			++Newly;

			// THE GAME'S STUN, AS A DESIGNED ONE, BY THE WEARER (K5): crowd control resistance shortens it, a boss is
			// never stunned, an enemy stunned in the last five seconds is not stunned again, and the wearer's own
			// health ceiling on crowd control holds. A stun refused here leaves the enemy caught all the same (K6).
			const bool bFrozen = StunSeconds > 0.0f
				&& UCataclysmSkillEffects::ApplyStun(Character, Other, StunSeconds, /*DamageDealt=*/0.0f,
													 /*bStunIsDesigned=*/true);

			// AND FOR AS LONG AS THAT STUN LASTS, AS IT WAS LAID, IT TAKES MORE FROM EVERYTHING (K7). Read off the stun
			// itself, so resistance shortening it shortens this, and nothing is held on an enemy the stun refused.
			if (bFrozen && MorePercent > 0.0f)
			{
				UCataclysmDamageTakenMultiplier::Hold(Other, 1.0f + MorePercent / 100.0f, TimeLockRiftStunLeftOn(Other));
			}
		}
		return Newly;
	}
}

float UCataclysmTimeLockRift::StatOf(const UCataclysmAbilitySystemComponent* Wearer, const TCHAR* Stat)
{
	return Wearer ? Wearer->StatForSkill(FName(Stat), FGameplayTagContainer(), 0.0f) : 0.0f;
}

int32 UCataclysmTimeLockRift::Step(AActor* Character)
{
	UCataclysmAbilitySystemComponent* Wearer =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Character));
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!Wearer || !World || UCataclysmSkillEffects::IsDead(Character))
	{
		return 0;
	}

	TMap<TWeakObjectPtr<UCataclysmAbilitySystemComponent>, FTimeLockRiftState>& Rifts = TimeLockRifts();
	const TWeakObjectPtr<UCataclysmAbilitySystemComponent> Key(Wearer);
	const float Now = World->GetTimeSeconds();

	// NOT ARMED, OR NO LONGER: the clock is forgotten, so the tenth piece put on again waits the full interval, and a
	// rift standing ends with it, so nothing is drawn that catches nobody.
	const float Every = StatOf(Wearer, EverySecondsStat);
	if (Every <= 0.0f)
	{
		if (FTimeLockRiftState* Was = Rifts.Find(Key))
		{
			if (ACataclysmGroundZone* Standing = Was->Rift.Get())
			{
				Standing->Destroy();
			}
			Rifts.Remove(Key);
		}
		return 0;
	}

	// FIRST ARMED NOW: ready a whole interval from this step.
	FTimeLockRiftState* State = Rifts.Find(Key);
	if (!State)
	{
		for (auto It = Rifts.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
		Rifts.Add(Key).ReadyAtSeconds = Now + Every;
		return 0;
	}

	// A RIFT STANDING CATCHES WHOEVER HAS COME INTO IT SINCE THE LAST STEP, and grows for each, up to the most (K1).
	// Asked as the zone's own sweep asks who stands in it, from where the rift was placed (K4).
	if (ACataclysmGroundZone* Rift = State->Rift.Get())
	{
		if (TimeLockRiftStands(*State, Now))
		{
			const FVector At = Rift->GetActorLocation();
			const int32 Newly = TimeLockRiftCatch(Character, Wearer, *State,
				UCataclysmTargeting::FindEnemiesInLine(World, Character, At, At, Rift->RadiusCm));
			const float Grown = TimeLockRiftLengthFor(Wearer, State->Caught.Num());
			if (Newly > 0 && Grown > State->LengthSeconds)
			{
				State->LengthSeconds = Grown;
				Rift->LastFor(State->AppearedAtSeconds + Grown - Now);
			}
			return Newly;
		}
	}
	// A RIFT THAT HAS RUN ITS LENGTH, or that the floor took, is forgotten; its zone ends by its own life span.
	State->Rift.Reset();
	State->Caught.Reset();

	if (Now < State->ReadyAtSeconds)
	{
		return 0;
	}

	// READY, AND IT WAITS UNTIL AN ENEMY IS WITHIN THE RADIUS OF WHERE THE WEARER STANDS (K4). Asked as the rift will
	// ask once it is there, so the enemy that calls it is one it catches.
	const float RadiusCm = StatOf(Wearer, RadiusMetresStat) * 100.0f;
	if (RadiusCm <= 0.0f || StatOf(Wearer, BaseSecondsStat) <= 0.0f)
	{
		return 0;
	}
	const FVector Where = Character->GetActorLocation();
	const TArray<AActor*> Inside = UCataclysmTargeting::FindEnemiesInLine(World, Character, Where, Where, RadiusCm);
	if (Inside.IsEmpty())
	{
		return 0;
	}

	// IT APPEARS WHERE THE WEARER STANDS AND STAYS THERE, as long as its base and a second for each enemy it catches
	// now, at most the most: placed at that length so it is drawn once for it. It deals nothing and lays nothing.
	const float Length = TimeLockRiftLengthFor(Wearer, Inside.Num());
	ACataclysmGroundZone* Placed =
		ACataclysmGroundZone::Spawn(Character, Where, RadiusCm, Length, /*DamagePerTick=*/0.0f);
	if (!Placed)
	{
		return 0;
	}
	Placed->bIsNotAPersistentArea = true;

	// THE CLOCK RESTARTS AS IT APPEARS (K4).
	State->ReadyAtSeconds = Now + Every;
	State->Rift = Placed;
	State->AppearedAtSeconds = Now;
	State->LengthSeconds = Length;
	return TimeLockRiftCatch(Character, Wearer, *State, Inside);
}

float UCataclysmTimeLockRift::SecondsUntilReady(const UCataclysmAbilitySystemComponent* Wearer)
{
	const FTimeLockRiftState* State = TimeLockRiftStateOf(Wearer);
	return State ? FMath::Max(0.0f, State->ReadyAtSeconds - TimeLockRiftWorldSecondsOf(Wearer)) : -1.0f;
}

ACataclysmGroundZone* UCataclysmTimeLockRift::RiftOf(const UCataclysmAbilitySystemComponent* Wearer)
{
	const FTimeLockRiftState* State = TimeLockRiftStateOf(Wearer);
	return State && TimeLockRiftStands(*State, TimeLockRiftWorldSecondsOf(Wearer)) ? State->Rift.Get() : nullptr;
}

float UCataclysmTimeLockRift::RiftLengthSeconds(const UCataclysmAbilitySystemComponent* Wearer)
{
	const FTimeLockRiftState* State = TimeLockRiftStateOf(Wearer);
	return State && TimeLockRiftStands(*State, TimeLockRiftWorldSecondsOf(Wearer)) ? State->LengthSeconds : 0.0f;
}

int32 UCataclysmTimeLockRift::CaughtBy(const UCataclysmAbilitySystemComponent* Wearer)
{
	const FTimeLockRiftState* State = TimeLockRiftStateOf(Wearer);
	return State && TimeLockRiftStands(*State, TimeLockRiftWorldSecondsOf(Wearer)) ? State->Caught.Num() : 0;
}
