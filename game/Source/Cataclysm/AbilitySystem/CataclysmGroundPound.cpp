// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmGroundPound.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"

const TCHAR* UCataclysmGroundPound::EverySecondsStat = TEXT("ground_pound_every_seconds");
const TCHAR* UCataclysmGroundPound::ArmourPercentStat = TEXT("ground_pound_percent_of_armor");
const TCHAR* UCataclysmGroundPound::RadiusMetresStat = TEXT("ground_pound_radius_metres");
const TCHAR* UCataclysmGroundPound::StunSecondsStat = TEXT("ground_pound_stun_seconds");
const TCHAR* UCataclysmGroundPound::SecondsOffPerKillStat = TEXT("ground_pound_seconds_off_per_kill");

namespace
{
	/** One wearer's clock: when its pound is ready, in its world's seconds, and the listener that hears its kills. */
	struct FGroundPoundClock
	{
		float ReadyAtSeconds = 0.0f;
		FDelegateHandle KillHeard;
	};

	/**
	 * Every armed wearer's clock, keyed weakly. HELD HERE AND NOT ON THE ABILITY SYSTEM, so this bonus adds no member to
	 * that shared class; the register of `UCataclysmVoidAura` is the precedent. A gone wearer's clock is dropped when the
	 * next clock is started.
	 */
	TMap<TWeakObjectPtr<UCataclysmAbilitySystemComponent>, FGroundPoundClock>& GroundPoundClocks()
	{
		static TMap<TWeakObjectPtr<UCataclysmAbilitySystemComponent>, FGroundPoundClock> Held;
		return Held;
	}

	float WorldSecondsOf(const UObject* Thing)
	{
		const UWorld* World = Thing ? Thing->GetWorld() : nullptr;
		return World ? World->GetTimeSeconds() : 0.0f;
	}
}

float UCataclysmGroundPound::StatOf(const UCataclysmAbilitySystemComponent* Wearer, const TCHAR* Stat)
{
	return Wearer ? Wearer->StatForSkill(FName(Stat), FGameplayTagContainer(), 0.0f) : 0.0f;
}

int32 UCataclysmGroundPound::Step(AActor* Character)
{
	UCataclysmAbilitySystemComponent* Wearer =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Character));
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!Wearer || !World || UCataclysmSkillEffects::IsDead(Character))
	{
		return 0;
	}

	TMap<TWeakObjectPtr<UCataclysmAbilitySystemComponent>, FGroundPoundClock>& Clocks = GroundPoundClocks();
	const TWeakObjectPtr<UCataclysmAbilitySystemComponent> Key(Wearer);
	const float Now = World->GetTimeSeconds();

	// NOT ARMED, OR NO LONGER: the clock is forgotten, so the tenth piece put on again starts from the full interval.
	const float Every = StatOf(Wearer, EverySecondsStat);
	if (Every <= 0.0f)
	{
		if (const FGroundPoundClock* Was = Clocks.Find(Key))
		{
			Wearer->OnActionEvent.Remove(Was->KillHeard);
			Clocks.Remove(Key);
		}
		return 0;
	}

	// FIRST ARMED NOW: ready a whole interval from this step (Q5), and from now on its kills are heard. Only a kill
	// credited to the wearer raises `kill` on it (`ACataclysmPlayerCharacter::OnSomethingDied`).
	FGroundPoundClock* Clock = Clocks.Find(Key);
	if (!Clock)
	{
		for (auto It = Clocks.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
		FGroundPoundClock& Started = Clocks.Add(Key);
		Started.ReadyAtSeconds = Now + Every;
		Started.KillHeard = Wearer->OnActionEvent.AddWeakLambda(Wearer, [Key](FName Event)
		{
			static const FName KillEvent(TEXT("kill"));
			if (Event == KillEvent)
			{
				UCataclysmGroundPound::NoteKill(Key.Get());
			}
		});
		return 0;
	}
	if (Now < Clock->ReadyAtSeconds)
	{
		return 0;
	}

	// READY, AND HELD UNTIL AN ENEMY IS INSIDE THE RADIUS. The search every nearby action makes, from the character.
	const float RadiusCm = StatOf(Wearer, RadiusMetresStat) * 100.0f;
	if (RadiusCm <= 0.0f)
	{
		return 0;
	}
	const TArray<AActor*> Struck =
		UCataclysmTargeting::FindEnemiesInSphere(World, Character, Character->GetActorLocation(), RadiusCm);
	if (Struck.IsEmpty())
	{
		return 0;
	}

	// THE CLOCK STARTS AGAIN BEFORE THE BLOW, so a kill the pound makes is taken off the next one.
	Clock->ReadyAtSeconds = Now + Every;

	// THE BLOW `smite_nearby_by_armor` DEALS, asked and dealt as `ActOnNearby` asks and deals it (Q4).
	const UCataclysmCombatAttributeSet* Combat = Wearer->GetSet<UCataclysmCombatAttributeSet>();
	const float Armour = Wearer->StatForSkill(FName(TEXT("armor")), FGameplayTagContainer(),
		Combat ? Combat->GetArmor() : 0.0f);
	const float Amount = Armour * StatOf(Wearer, ArmourPercentStat) / 100.0f;
	const float StunSeconds = StatOf(Wearer, StunSecondsStat);
	for (AActor* Other : Struck)
	{
		if (Amount > 0.0f)
		{
			UCataclysmSkillEffects::ApplyDirectDamage(Character, Other, Amount);
		}
		// THE ORDINARY STUN, AS A DESIGNED ONE (Q2), and not on an enemy the blow killed.
		if (StunSeconds > 0.0f && IsValid(Other) && !UCataclysmSkillEffects::IsDead(Other))
		{
			UCataclysmSkillEffects::ApplyStun(Character, Other, StunSeconds, /*DamageDealt=*/0.0f,
											  /*bStunIsDesigned=*/true);
		}
	}
	return Struck.Num();
}

float UCataclysmGroundPound::SecondsUntilReady(const UCataclysmAbilitySystemComponent* Wearer)
{
	const FGroundPoundClock* Clock =
		Wearer ? GroundPoundClocks().Find(TWeakObjectPtr<UCataclysmAbilitySystemComponent>(
					 const_cast<UCataclysmAbilitySystemComponent*>(Wearer)))
			   : nullptr;
	return Clock ? FMath::Max(0.0f, Clock->ReadyAtSeconds - WorldSecondsOf(Wearer)) : -1.0f;
}

void UCataclysmGroundPound::NoteKill(UCataclysmAbilitySystemComponent* Wearer)
{
	FGroundPoundClock* Clock =
		Wearer ? GroundPoundClocks().Find(TWeakObjectPtr<UCataclysmAbilitySystemComponent>(Wearer)) : nullptr;
	if (!Clock)
	{
		return;
	}
	// A KILL WHILE READY BANKS NOTHING, and one while waiting never takes the time left below nought (Q3).
	const float Now = WorldSecondsOf(Wearer);
	const float Off = StatOf(Wearer, SecondsOffPerKillStat);
	if (Off <= 0.0f || Clock->ReadyAtSeconds <= Now)
	{
		return;
	}
	Clock->ReadyAtSeconds = FMath::Max(Now, Clock->ReadyAtSeconds - Off);
}
