// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"
#include "TimerManager.h"

namespace
{
}

// ==========================================================================
// Strike -- Molten Cleave, Searing Hook, Pyroclasm
// ==========================================================================

void UCataclysmStrikeSkill::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAndBegin(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}

	SwingsMade = 0;
	SwingOnce();

	AActor* Self = Avatar();
	if (Self)
	{
		// Under the caster's own feet. Molten Cleave drags its slag from where
		// the swing started; Pyroclasm leaves "the ground within 5 meters".
		LeaveGroundAt(Self->GetActorLocation());
	}

	// A single swing with no duration is over. Pyroclasm's spin is the other
	// case: it repeats for Duration and then lands its final hit.
	const bool bRepeats = Params.Duration > 0.0f && Params.Interval > 0.0f;
	if (!bRepeats)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	UWorld* World = Self ? Self->GetWorld() : nullptr;
	if (!World)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	World->GetTimerManager().SetTimer(
		RepeatTimer, this, &UCataclysmStrikeSkill::Repeat,
		Params.Interval, /*bLoop=*/true, /*InFirstDelay=*/Params.Interval);
	World->GetTimerManager().SetTimer(
		FinishTimer, this, &UCataclysmStrikeSkill::Finish,
		Params.Duration, /*bLoop=*/false);
}

int32 UCataclysmStrikeSkill::SwingOnce(float DamagePercent)
{
	AActor* Self = Avatar();
	if (!Self)
	{
		return 0;
	}

	// Aimed at the cursor rather than at wherever the character happens to be
	// facing. A top-down game gives the player no other way to point a cone.
	const TArray<AActor*> Targets = UCataclysmTargeting::FindEnemiesInCone(
		GetWorld(), Self, Self->GetActorLocation(), AimDirection(),
		Params.RadiusCm, Params.AngleDegrees, Params.MaxTargets);

	HitTargets(Targets, DamagePercent);

	// Searing Hook "knocks them back 4 meters". Applied as a displacement
	// rather than an impulse: most of what this hits has no physics body, and a
	// knockback that silently did nothing would look like a knockback.
	if (Params.KnockbackCm > 0.0f)
	{
		for (AActor* Target : Targets)
		{
			FVector Away = Target->GetActorLocation() - Self->GetActorLocation();
			Away.Z = 0.0f;
			if (!Away.IsNearlyZero())
			{
				Target->AddActorWorldOffset(
					Away.GetSafeNormal() * Params.KnockbackCm, /*bSweep=*/true);
			}
		}
	}

	++SwingsMade;
	return Targets.Num();
}

void UCataclysmStrikeSkill::Repeat()
{
	SwingOnce();
}

void UCataclysmStrikeSkill::Finish()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RepeatTimer);
	}

	// Pyroclasm: "the final hit at the end of the spin deals 300% weapon damage
	// to all affected enemies". Its own figure, so it overrides the slot's 400%.
	if (Params.FinalHitPercent > 0.0f)
	{
		SwingOnce(Params.FinalHitPercent);
	}

	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
			   GetCurrentActivationInfo(), true, false);
}

// ==========================================================================
// Projectile -- Emberhurl, Blood Pyre, Infernal Lance
// ==========================================================================

void UCataclysmProjectileSkill::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAndBegin(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}

	AActor* Self = Avatar();
	if (!Self)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	Landings = 0;

	// Both ends fixed here and not re-read later. A projectile that followed the
	// cursor after it was thrown would be a homing missile, which none of these
	// are.
	Origin = Self->GetActorLocation();
	Destination = AimedPointWithin(Params.RangeCm);

	const float Flight = Params.SpeedCmPerSecond > 0.0f
		? FVector::Dist(Origin, Destination) / Params.SpeedCmPerSecond
		: 0.0f;

	UWorld* World = Self->GetWorld();
	if (Flight <= 0.0f || !World)
	{
		// A beam. Infernal Lance drives a lance forward and arrives at once.
		LandThenFinish();
		return;
	}

	World->GetTimerManager().SetTimer(
		FlightTimer, this, &UCataclysmProjectileSkill::LandThenFinish,
		Flight, /*bLoop=*/false);
}

int32 UCataclysmProjectileSkill::Land()
{
	AActor* Self = Avatar();
	if (!Self)
	{
		return 0;
	}

	// PIERCE IS WHAT TELLS THE TWO KINDS APART. One that pierces is a line and
	// hits what it passes; one that does not lands and hits in a radius. See the
	// class comment.
	TArray<AActor*> Targets;
	if (Params.Pierce > 0)
	{
		// Pierce is how many it passes THROUGH, so it hits one more than that.
		Targets = UCataclysmTargeting::FindEnemiesInLine(
			GetWorld(), Self, Origin, Destination, Params.RadiusCm,
			Params.Pierce + 1);
	}
	else
	{
		Targets = UCataclysmTargeting::FindEnemiesInSphere(
			GetWorld(), Self, Destination, Params.RadiusCm, Params.MaxTargets);
	}

	HitTargets(Targets);
	++Landings;
	return Targets.Num();
}

void UCataclysmProjectileSkill::LandThenFinish()
{
	Land();

	AActor* Self = Avatar();
	UWorld* World = Self ? Self->GetWorld() : nullptr;

	if (Params.LeavesGround())
	{
		// Where it landed. Emberhurl leaves "its flight path burning" and Blood
		// Pyre leaves a pyre where it hit; one zone at the far end is the honest
		// approximation of the first and exactly right for the second. A line of
		// zones along the path would be closer for Emberhurl. Issue #167.
		LeaveGroundAt(Destination);
	}

	// Emberhurl hits "once going out and once returning to your hand".
	if (Params.bReturns && Landings < 2 && World)
	{
		const float Flight = Params.SpeedCmPerSecond > 0.0f
			? FVector::Dist(Origin, Destination) / Params.SpeedCmPerSecond
			: 0.0f;
		if (Flight > 0.0f)
		{
			World->GetTimerManager().SetTimer(
				FlightTimer, this, &UCataclysmProjectileSkill::Return,
				Flight, /*bLoop=*/false);
			return;
		}

		Return();
		return;
	}

	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
			   GetCurrentActivationInfo(), true, false);
}

void UCataclysmProjectileSkill::Return()
{
	// The same line walked backwards, so the same enemies are struck a second
	// time, which is what "hitting each enemy twice" means.
	Swap(Origin, Destination);
	Land();
	Swap(Origin, Destination);

	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
			   GetCurrentActivationInfo(), true, false);
}

// ==========================================================================
// SelfBuff -- Burning Wrath, Martyr's Ember
// ==========================================================================

void UCataclysmSelfBuffSkill::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAndBegin(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}

	AActor* Self = Avatar();
	if (!Self || Params.Duration <= 0.0f)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Burning Wrath is "4% increased fire damage for every enemy currently
	// burning within 15 meters", so the count is taken once, when it goes up,
	// not continuously. Counted even though nothing can yet apply the increase,
	// because the count is the part that is genuinely this skill's rule.
	BurningEnemiesAtCast = 0;
	if (Params.RadiusCm > 0.0f)
	{
		const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
		for (AActor* Nearby : UCataclysmTargeting::FindEnemiesInSphere(
				GetWorld(), Self, Self->GetActorLocation(), Params.RadiusCm))
		{
			if (UCataclysmSkillEffects::HasTag(Nearby, Burn))
			{
				++BurningEnemiesAtCast;
			}
		}
	}

	// GRANTED ONLY IF THE SKILL NAMES AN EFFECT, and neither designed self buff
	// does: the design gives Burning Wrath and Martyr's Ember a duration and a
	// magnitude but never names the buff, so there is no status in the Buffs
	// sheet for either and no tag to grant. The duration is still real -- the
	// timer below runs and the ability ends when it expires -- and the magnitude
	// is what is missing. Issue #166.
	UCataclysmSkillEffects::ApplyTagForDuration(
		Self, Self, UCataclysmSkillShapes::StatusTagFor(Params.Effect),
		Params.Duration);

	if (UWorld* World = Self->GetWorld())
	{
		World->GetTimerManager().SetTimer(
			FinishTimer, this, &UCataclysmSelfBuffSkill::Finish,
			Params.Duration, /*bLoop=*/false);
	}
	else
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
}

void UCataclysmSelfBuffSkill::Finish()
{
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
			   GetCurrentActivationInfo(), true, false);
}

// ==========================================================================
// Movement -- Infernal Plunge, Cinder Rush, Emberstep
// ==========================================================================

void UCataclysmMovementSkill::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAndBegin(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}

	AActor* Self = Avatar();
	if (!Self)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const FVector Start = Self->GetActorLocation();
	const FVector End = AimedPointWithin(Params.RangeCm);

	TArray<AActor*> Targets;
	switch (Params.MovementMode)
	{
	case ECataclysmMovementMode::Charge:
		// "Barrelling through any enemies in your path": everything on the line.
		Targets = UCataclysmTargeting::FindEnemiesInLine(
			GetWorld(), Self, Start, End, Params.RadiusCm, Params.MaxTargets);
		break;

	case ECataclysmMovementMode::Blink:
		// "Enemies at the point you left and the point you arrive": both ends,
		// nothing between. Gathered from both and merged, because an enemy
		// standing between the two circles is hit by neither and one standing in
		// both must still only be hit once.
		Targets = UCataclysmTargeting::FindEnemiesInSphere(
			GetWorld(), Self, Start, Params.RadiusCm);
		for (AActor* Far : UCataclysmTargeting::FindEnemiesInSphere(
				GetWorld(), Self, End, Params.RadiusCm))
		{
			Targets.AddUnique(Far);
		}
		break;

	case ECataclysmMovementMode::Leap:
	default:
		// "Slam down, dealing damage in a 5 meter radius on impact": where it
		// lands only. Nothing under the arc is touched.
		Targets = UCataclysmTargeting::FindEnemiesInSphere(
			GetWorld(), Self, End, Params.RadiusCm, Params.MaxTargets);
		break;
	}

	// The ground is left BEFORE the move for a blink, because Emberstep burns
	// "both points", and the point left behind stops being the caster's position
	// the moment they arrive.
	if (Params.MovementMode == ECataclysmMovementMode::Blink)
	{
		LeaveGroundAt(Start);
	}

	// Swept, so a leap into a wall stops at the wall rather than putting the
	// character inside it.
	Self->SetActorLocation(End, /*bSweep=*/true);
	ArrivedAt = Self->GetActorLocation();

	EnemiesHit = Targets.Num();
	HitTargets(Targets);

	// Charge leaves "a trail of fire behind you"; Leap leaves "a pool of lava"
	// where it landed. Both are put at the far end, which is exactly right for a
	// leap and an approximation for a charge: the trail should follow the whole
	// path. Issue #167.
	LeaveGroundAt(ArrivedAt);

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// ==========================================================================
// Summon -- Summon Imp, Open the Rift
// ==========================================================================

int32 UCataclysmSummonSkill::LivingMinionCount()
{
	Minions.RemoveAll([](const TObjectPtr<ACataclysmMinion>& Minion)
	{
		return !IsValid(Minion);
	});
	return Minions.Num();
}

ACataclysmMinion* UCataclysmSummonSkill::SummonOne()
{
	AActor* Self = Avatar();
	if (!Self)
	{
		return nullptr;
	}

	// THE CAP IS ENFORCED BEFORE THE SPAWN, NOT AFTER, so the number alive never
	// exceeds it even for an instant. Summon Imp: "up to 3 imps may be active at
	// once. Summoning a fourth destroys the oldest, which explodes for damage in
	// a 3 meter radius."
	if (Params.MaxActive > 0 && LivingMinionCount() >= Params.MaxActive)
	{
		ACataclysmMinion* Oldest = Minions[0];
		Minions.RemoveAt(0);
		if (IsValid(Oldest))
		{
			Oldest->Explode(Params.RadiusCm, GetSlotDamagePercent());
		}
	}

	const float Lifetime = Params.Duration > 0.0f ? Params.Duration : 20.0f;
	ACataclysmMinion* Minion = ACataclysmMinion::Spawn(
		Self, RiftLocation, Lifetime, Params.bBurns);
	if (Minion)
	{
		Minions.Add(Minion);
	}
	return Minion;
}

void UCataclysmSummonSkill::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAndBegin(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}

	AActor* Self = Avatar();
	if (!Self)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// A rift is torn at a place and stays there. Summon Imp has no Range, so its
	// imps appear at the caster.
	RiftLocation = Params.RangeCm > 0.0f
		? AimedPointWithin(Params.RangeCm)
		: Self->GetActorLocation();

	UWorld* World = Self->GetWorld();

	// Open the Rift spawns over time; Summon Imp spawns Count at once.
	const bool bSpawnsOverTime =
		Params.Interval > 0.0f && Params.Duration > 0.0f && World;

	if (bSpawnsOverTime)
	{
		// "Lasts 10 seconds, burns every enemy within 6 meters."
		LeaveGroundAt(RiftLocation);

		World->GetTimerManager().SetTimer(
			SpawnTimer, this, &UCataclysmSummonSkill::SpawnTick,
			Params.Interval, /*bLoop=*/true, /*InFirstDelay=*/Params.Interval);
		World->GetTimerManager().SetTimer(
			CollapseTimer, this, &UCataclysmSummonSkill::Collapse,
			Params.Duration, /*bLoop=*/false);
		return;
	}

	for (int32 Index = 0; Index < FMath::Max(1, Params.Count); ++Index)
	{
		SummonOne();
	}

	// DELIBERATELY NOT ENDED. The ability instance is what holds the minion
	// list, and ending it is fine -- the list lives on the instance, which is
	// per actor and outlives the activation -- but the cap only works if the
	// same instance is used next time, which InstancedPerActor guarantees.
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UCataclysmSummonSkill::SpawnTick()
{
	// "Spawns a lesser imp every 2 seconds to a maximum of 5." The maximum is a
	// total for this rift rather than a rolling cap, so once it has made five it
	// stops rather than replacing them.
	if (Params.MaxActive > 0 && Minions.Num() >= Params.MaxActive)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(SpawnTimer);
		}
		return;
	}
	SummonOne();
}

void UCataclysmSummonSkill::Collapse()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpawnTimer);
	}

	AActor* Self = Avatar();

	// "When the rift closes it collapses, dealing 400% weapon damage in its
	// radius and destroying the imps it spawned."
	if (Self && Params.FinalHitPercent > 0.0f && Params.RadiusCm > 0.0f)
	{
		const TArray<AActor*> Caught = UCataclysmTargeting::FindEnemiesInSphere(
			GetWorld(), Self, RiftLocation, Params.RadiusCm);
		HitTargets(Caught, Params.FinalHitPercent);
	}

	for (const TObjectPtr<ACataclysmMinion>& Minion : Minions)
	{
		if (IsValid(Minion))
		{
			Minion->Destroy();
		}
	}
	Minions.Reset();

	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
			   GetCurrentActivationInfo(), true, false);
}

// ==========================================================================
// Aura -- Conflagration, Living Pyre
// ==========================================================================

UCataclysmAuraSkill::UCataclysmAuraSkill()
{
	// LEFT FALSE DELIBERATELY. Allowing a retrigger makes the engine end the
	// running instance and start it again, which for a toggle means a second
	// press restarts the aura instead of stopping it. The stop belongs in
	// InputPressed; see the comment on it.
	bRetriggerInstancedAbility = false;
}

void UCataclysmAuraSkill::InputPressed(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputPressed(Handle, ActorInfo, ActivationInfo);

	// Issue #36 requires the aura to toggle. The aura slot has no cooldown --
	// there is nothing to wait for on a toggle -- so this is the only thing
	// stopping the key from being pressed to no effect while it runs.
	if (bHeld)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo,
				   /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
	}
}

void UCataclysmAuraSkill::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAndBegin(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}

	AActor* Self = Avatar();
	UWorld* World = Self ? Self->GetWorld() : nullptr;
	if (!World)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	bHeld = true;
	bEndedForLackOfMana = false;
	Pulses = 0;

	const float Period = Params.Interval > 0.0f ? Params.Interval : 1.0f;
	World->GetTimerManager().SetTimer(
		PulseTimer, this, &UCataclysmAuraSkill::PulseTick,
		Period, /*bLoop=*/true, /*InFirstDelay=*/Period);

	// Living Pyre is an Ultimate lasting 6 seconds. Conflagration has no
	// duration and is held until it is switched off or paid out.
	if (Params.Duration > 0.0f)
	{
		World->GetTimerManager().SetTimer(
			FinishTimer, this, &UCataclysmAuraSkill::Finish,
			Params.Duration, /*bLoop=*/false);
	}
}

int32 UCataclysmAuraSkill::Pulse()
{
	AActor* Self = Avatar();
	if (!Self)
	{
		Finish();
		return 0;
	}

	UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Self);

	// PAID BEFORE IT WORKS, AND SWITCHING OFF WHEN IT CANNOT BE PAID IS THE
	// REQUIREMENT. Issue #36: the aura must turn off when the resource is
	// exhausted. The aura slot costs 20 mana a second, which empties a Ravager
	// standing still in 48 seconds and never empties a Ritualist.
	//
	// A toggle has no duration to pay for, so the cost is per pulse rather than
	// per activation, and it is the only cost that is not taken by CommitAbility.
	if (AbilitySystem && Params.Duration <= 0.0f)
	{
		const float Period = Params.Interval > 0.0f ? Params.Interval : 1.0f;
		const float Cost = GetManaCost() * Period;
		const float Mana = AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetManaAttribute());

		if (Cost > 0.0f && Mana < Cost)
		{
			bEndedForLackOfMana = true;
			Finish();
			return 0;
		}
		if (Cost > 0.0f)
		{
			AbilitySystem->ApplyModToAttribute(
				UCataclysmVitalAttributeSet::GetManaAttribute(),
				EGameplayModOp::Additive, -Cost);
		}
	}

	// A ring around the caster, following them. The Aura slot's damage is 25%
	// of weapon damage PER SECOND, which the Skill Slots sheet says explicitly,
	// so a pulse is worth that much of a second.
	const TArray<AActor*> Inside = UCataclysmTargeting::FindEnemiesInSphere(
		GetWorld(), Self, Self->GetActorLocation(), Params.RadiusCm);

	const float Period = Params.Interval > 0.0f ? Params.Interval : 1.0f;
	HitTargets(Inside, GetSlotDamagePercent() * Period);

	++Pulses;
	return Inside.Num();
}

void UCataclysmAuraSkill::PulseTick()
{
	Pulse();
}

void UCataclysmAuraSkill::Finish()
{
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
			   GetCurrentActivationInfo(), true, false);
}

void UCataclysmAuraSkill::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	bHeld = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PulseTimer);
		World->GetTimerManager().ClearTimer(FinishTimer);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
					  bWasCancelled);
}

// ==========================================================================
// Debuff -- Subjugate
// ==========================================================================

void UCataclysmDebuffSkill::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAndBegin(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}

	AActor* Self = Avatar();
	if (!Self)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	EnemiesAffected = 0;
	LastDurationApplied = 0.0f;

	// IN RANGE OF THE CASTER, ORDERED BY WHERE THE PLAYER IS POINTING. The first
	// attempt searched a small circle at the aim point, and it was wrong twice
	// over: a cursor a metre off the enemy took nobody, and with no cursor at
	// all -- an enemy casting, or a test -- the aim ran out to the full 15
	// metres and found empty ground.
	//
	// Range bounds who can be reached, which is what Subjugate's "up to 15
	// meters" means; the cursor only decides which of those is picked. So a
	// player pointing roughly at an enemy takes that enemy, and pointing at
	// nothing takes the nearest, which is what a single-target curse should do.
	TArray<AActor*> InRange = UCataclysmTargeting::FindEnemiesInSphere(
		GetWorld(), Self, Self->GetActorLocation(), Params.RangeCm);

	const FVector Aim = AimPoint();
	InRange.Sort([&Aim](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), Aim)
			 < FVector::DistSquared(B.GetActorLocation(), Aim);
	});

	const int32 Cap = Params.MaxTargets > 0 ? Params.MaxTargets : 1;
	if (InRange.Num() > Cap)
	{
		InRange.SetNum(Cap);
	}
	const TArray<AActor*>& Targets = InRange;

	// Subjugate applies Madness, which the design's own effect table gives as
	// "the enemy attacks anything nearby, friend or foe, for 3 seconds". The
	// tag comes from that name via the Debuffs sheet, so a skill applying an
	// effect nobody designed grants nothing and the generator refuses the row.
	const FGameplayTag Effect = UCataclysmSkillShapes::StatusTagFor(Params.Effect);
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();

	for (AActor* Target : Targets)
	{
		// "Subjugating an enemy that is already burning makes the madness last
		// twice as long." Read off the target's own tags, so any source of burn
		// counts and not only this character's.
		const bool bAlreadyBurning = UCataclysmSkillEffects::HasTag(Target, Burn);
		const float Duration = Params.Duration * (bAlreadyBurning ? 2.0f : 1.0f);

		if (UCataclysmSkillEffects::ApplyTagForDuration(Self, Target, Effect, Duration))
		{
			LastDurationApplied = Duration;
			++EnemiesAffected;
		}
	}

	// A Support slot deals no damage by design, so this only lands a hit for a
	// debuff whose slot has one.
	HitTargets(Targets);

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
