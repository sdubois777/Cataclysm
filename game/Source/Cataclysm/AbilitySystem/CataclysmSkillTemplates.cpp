// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStrikeEffect.h"
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
		ScaledRadiusCm(), Params.AngleDegrees, Params.MaxTargets);

	// The knockback goes with it. It used to be applied here, for Strikes only;
	// issue #626 moved it into HitTargets so that every shape can shove, which
	// is what making it a rider means.
	HitTargets(Targets, DamagePercent);

	// AND THE SWING IS DRAWN, WHICH UNTIL ISSUE #811 IT WAS NOT. This is the one
	// place a Strike swings, so it is the one place the arc has to be spawned
	// from; every repeating strike reaches here through Repeat and Finish as
	// well, so a spin draws an arc per interval rather than one at the start.
	//
	// DRAWN WHETHER OR NOT ANYTHING WAS HIT, and that is deliberate and the
	// opposite of what the impact burst does. UCataclysmImpactEffect::ShouldDrawFor
	// refuses to draw for a blow that connected with nothing, because that effect
	// means "that landed". This one means "you swung", and a swing that misses
	// still happened -- a player who saw nothing when they hit thin air would
	// read it as the button not working.
	// COLOURED FROM THE SKILL AND NOT ONLY FROM THE CASTER. A player's damage
	// carries no type by design, so asking the caster alone would draw every
	// player swing white -- which is what every other effect in the project
	// still does. Issue #803. `ElementTag` is the skill's own `Element.*` tag
	// out of the Weapon Skills sheet.
	UCataclysmStrikeEffect::PlayAt(Self, Self->GetActorLocation(), AimDirection(),
								   UCataclysmStrikeEffect::DamageTypeFor(
									   Self, ElementTag()),
								   ScaledRadiusCm());

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

	// A REAL ACTOR WHEN IT HAS A SPEED. It moves in steps and sweeps each one,
	// so who it hits is decided by where they stood as it went past rather than
	// by where they stand when it arrives, and a wall stops it. Issue #164.
	// THIS SKILL'S OWN CRITICAL STRIKE CHANCE GOES WITH IT. A projectile lands
	// after this ability has ended, so it has to carry the chance rather than
	// read it off the character on arrival. Named explicitly because the two
	// arguments before it are defaulted. Issue #657.
	InFlight = ACataclysmProjectile::Fire(
		Self, Origin, Destination, ScaledRadiusCm(), Params.SpeedCmPerSecond,
		Params.Pierce, Params.bReturns, GetDamagePercent(), SkillTags,
		Params.bBurns, /*InBodyMesh=*/nullptr, /*InFlightSeconds=*/0.0f,
		CritChancePercent);

	if (!InFlight)
	{
		// No speed, or nowhere to fly. A beam: Infernal Lance drives a lance
		// forward and its description says it arrives at once.
		LandThenFinish();
		return;
	}

	InFlight->OnFinished.AddUObject(
		this, &UCataclysmProjectileSkill::OnProjectileFinished);
}

void UCataclysmProjectileSkill::OnProjectileFinished(
	ACataclysmProjectile* Projectile)
{
	InFlight = nullptr;

	if (Projectile)
	{
		// One per leg, so a throw that came back reports two the way a beam
		// that hits twice does.
		Landings += Projectile->LegsFlown();

		// FROM WHERE IT STARTED TO THE FURTHEST IT GOT, which is not
		// necessarily where it was aimed and not where it ended up. A throw
		// stopped by a wall half way burns half the path; one that returns
		// finishes back at the caster and still burns the whole flight.
		LeaveGroundForFlight(Projectile->StartedAt, Projectile->FurthestReached);
	}
	else
	{
		++Landings;
	}

	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
			   GetCurrentActivationInfo(), true, false);
}

void UCataclysmProjectileSkill::LeaveGroundForFlight(const FVector& From,
													 const FVector& To)
{
	if (!Params.LeavesGround())
	{
		return;
	}

	// PIERCE TELLS THE TWO KINDS APART. One that pierces travelled a line and
	// its text says the line burns: Emberhurl leaves "its flight path burning",
	// and Chain of Coals and Hellbrand are written the same way. One that does
	// not pierce stopped somewhere, and Blood Pyre's pyre and Magma Quake's
	// crater belong at that point and nowhere else.
	if (Params.Pierce > 0)
	{
		LeaveGroundAlong(From, To);
	}
	else
	{
		LeaveGroundAt(To);
	}
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
			GetWorld(), Self, Origin, Destination, ScaledRadiusCm(),
			Params.Pierce + 1);
	}
	else
	{
		Targets = UCataclysmTargeting::FindEnemiesInSphere(
			GetWorld(), Self, Destination, ScaledRadiusCm(), Params.MaxTargets);
	}

	HitTargets(Targets);
	++Landings;
	return Targets.Num();
}

void UCataclysmProjectileSkill::LandThenFinish()
{
	Land();
	LeaveGroundForFlight(Origin, Destination);

	// Emberhurl hits "once going out and once returning to your hand". A beam
	// that returns has no flight time either way, so the second hit is
	// immediate. A real projectile turns round by itself and never reaches here.
	if (Params.bReturns && Landings < 2)
	{
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
	// not continuously. An enemy that dies or stops burning during the ten
	// seconds does not lower it, and one that catches fire does not raise it.
	BurningEnemiesAtCast = 0;
	if (ScaledRadiusCm() > 0.0f)
	{
		const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
		for (AActor* Nearby : UCataclysmTargeting::FindEnemiesInSphere(
				GetWorld(), Self, Self->GetActorLocation(), ScaledRadiusCm()))
		{
			if (UCataclysmSkillEffects::HasTag(Nearby, Burn))
			{
				++BurningEnemiesAtCast;
			}
		}
	}

	GrantIncrease();

	// GRANTED ONLY IF THE SKILL NAMES AN EFFECT, and neither designed self buff
	// does: the design gives Burning Wrath and Martyr's Ember a duration and a
	// magnitude but never names the buff, so there is no status in the Buffs
	// sheet for either and no tag to grant. The magnitude no longer depends on
	// this: it is a stat modifier on the caster, not a tag anything reads.
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

void UCataclysmSelfBuffSkill::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	// HERE AND NOT IN Finish, because Finish is only the timer's route out. A
	// buff can also end because the ability was cancelled, because the avatar
	// died, or because the ability system was cleared, and an increase left
	// behind by any of those would last until the character was destroyed.
	RevokeIncrease();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
					  bWasCancelled);
}

void UCataclysmSelfBuffSkill::Finish()
{
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
			   GetCurrentActivationInfo(), true, false);
}

void UCataclysmSelfBuffSkill::GrantIncrease()
{
	GrantedIncrease = 0.0f;
	GrantedScope = FGameplayTag();

	if (Params.IncreasePerBurning <= 0.0f || BurningEnemiesAtCast <= 0)
	{
		// No increase to grant. Burning Wrath with nothing alight nearby is the
		// ordinary case, not a fault: the skill is written to be worth using
		// only after something has been set on fire.
		return;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Avatar()));
	if (!AbilitySystem)
	{
		return;
	}

	FCataclysmStatModifier Modifier;
	Modifier.Bucket = ECataclysmStatBucket::Increased;
	Modifier.Source = ECataclysmModifierSource::SkillBuff;
	Modifier.Value = Params.IncreasePerBurning * BurningEnemiesAtCast;

	// SCOPED TO THE SKILL'S OWN ELEMENT, so the rule is in the data and not
	// here. Burning Wrath carries Element.Demonic, which is this project's fire,
	// so "increased fire damage" is an increase that reaches skills carrying
	// that tag. A self buff written for another damage type scopes to its own
	// element with no code changing. A skill carrying no element tag grants an
	// increase that applies to everything, which is what an unscoped modifier
	// means throughout the pipeline.
	GrantedScope = ElementTag();
	if (GrantedScope.IsValid())
	{
		Modifier.RequiredTags.AddTag(GrantedScope);
	}

	IncreaseHandle = AbilitySystem->AddStatModifier(Modifier);
	if (IncreaseHandle == 0)
	{
		GrantedScope = FGameplayTag();
		return;
	}

	GrantedIncrease = Modifier.Value;
	UE_LOG(LogCataclysm, Verbose,
		TEXT("'%s' granted %.0f%% increased damage scoped to %s, from %d "
			 "burning enemies, for %.1fs."),
		*SkillName, GrantedIncrease,
		GrantedScope.IsValid() ? *GrantedScope.ToString() : TEXT("everything"),
		BurningEnemiesAtCast, Params.Duration);
}

void UCataclysmSelfBuffSkill::RevokeIncrease()
{
	if (IncreaseHandle == 0)
	{
		return;
	}

	if (UCataclysmAbilitySystemComponent* AbilitySystem =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Avatar())))
	{
		AbilitySystem->RemoveStatModifier(IncreaseHandle);
	}

	IncreaseHandle = 0;
	GrantedIncrease = 0.0f;
	GrantedScope = FGameplayTag();
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
			GetWorld(), Self, Start, End, ScaledRadiusCm(), Params.MaxTargets);
		break;

	case ECataclysmMovementMode::Blink:
		// "Enemies at the point you left and the point you arrive": both ends,
		// nothing between. Gathered from both and merged, because an enemy
		// standing between the two circles is hit by neither and one standing in
		// both must still only be hit once.
		Targets = UCataclysmTargeting::FindEnemiesInSphere(
			GetWorld(), Self, Start, ScaledRadiusCm());
		for (AActor* Far : UCataclysmTargeting::FindEnemiesInSphere(
				GetWorld(), Self, End, ScaledRadiusCm()))
		{
			Targets.AddUnique(Far);
		}
		break;

	case ECataclysmMovementMode::Leap:
	default:
		// "Slam down, dealing damage in a 5 meter radius on impact": where it
		// lands only. Nothing under the arc is touched.
		Targets = UCataclysmTargeting::FindEnemiesInSphere(
			GetWorld(), Self, End, ScaledRadiusCm(), Params.MaxTargets);
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

	// Cinder Rush's charge "leaves a trail of fire behind you", so the ground
	// burns along the whole run. Infernal Plunge's leap leaves "a pool of lava"
	// where it landed and Emberstep's blink burns "both points", so for those two
	// the far end is a point and the ground there is a patch. The blink's other
	// patch was already left at the start, above.
	if (Params.MovementMode == ECataclysmMovementMode::Charge)
	{
		LeaveGroundAlong(Start, ArrivedAt);
	}
	else
	{
		LeaveGroundAt(ArrivedAt);
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// ==========================================================================
// Deployable -- Bolt Turret, Ballista, Iron Fortress
// ==========================================================================

int32 UCataclysmDeployableSkill::LivingDeployedCount()
{
	Deployed.RemoveAll([](const TObjectPtr<ACataclysmMinion>& Machine)
	{
		return !IsValid(Machine);
	});
	return Deployed.Num();
}

ACataclysmMinion* UCataclysmDeployableSkill::DeployOne(const FString& InTypeName)
{
	AActor* Self = Avatar();
	if (!Self)
	{
		return nullptr;
	}

	// THE CAP STOPS PLACING RATHER THAN REPLACING. Summon Imp destroys its
	// oldest to make room and says so in its description; none of the three
	// deployable skills says anything of the kind, so reaching the cap here
	// simply means nothing more goes down.
	if (Params.MaxActive > 0 && LivingDeployedCount() >= Params.MaxActive)
	{
		return nullptr;
	}

	// A deployable with no Duration is not permanent, it is undesigned. Twenty
	// seconds matches what the summon template uses for the same case, so an
	// undesigned row behaves the same way whichever shape it names.
	const float Lifetime = Params.Duration > 0.0f ? Params.Duration : 20.0f;

	ACataclysmMinion* Machine = ACataclysmMinion::Spawn(
		Self, DeployLocation, Lifetime, Params.bBurns, InTypeName,
		Params.MinionHealthPercent);

	if (Machine)
	{
		Deployed.Add(Machine);
	}
	return Machine;
}

void UCataclysmDeployableSkill::ActivateAbility(
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

	// PUT DOWN WHERE IT WAS AIMED, WHICH IS THE POINT OF THE SHAPE. A deployable
	// with no Range goes at the caster's feet, which is what a spike trap laid
	// underfoot would be.
	DeployLocation = Params.RangeCm > 0.0f
		? AimedPointWithin(Params.RangeCm)
		: Self->GetActorLocation();

	// EVERY KIND THE ROW NAMES, NOT JUST THE FIRST. Iron Fortress writes
	// `Ballista:2, SpikeTrap:3`, and placing only ballistae would be a fortress
	// missing three fifths of itself with nothing to report it.
	//
	// COUNT IS NOT MULTIPLIED IN. The Minions parameter already says how many of
	// each kind. Bolt Turret writes `Count=1` and `Minions=BoltTurret:1`, which
	// is the same number said twice; multiplying them would deploy one turret
	// per turret and quietly double anything that stated both.
	int32 Placed = 0;
	for (const FCataclysmMinionSpawn& Kind : Params.Minions)
	{
		for (int32 Index = 0; Index < Kind.Count; ++Index)
		{
			if (DeployOne(Kind.Type))
			{
				++Placed;
			}
		}
	}

	if (Placed == 0)
	{
		// Names nothing, or the cap was already full. Verbose rather than a
		// warning: a player pressing a deployable key with its cap full is an
		// ordinary event, and a row that names nothing is refused by the
		// generator before it reaches here.
		UE_LOG(LogCataclysm, Verbose,
			TEXT("'%s' deployed nothing."), *SkillName);
	}

	// DELIBERATELY NOT ENDED WITH bWasCancelled. The ability instance holds the
	// list of what is out, and the cap only works if the same instance is used
	// next time, which InstancedPerActor guarantees.
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
			// A MINION'S EXPLOSION TAKES NONE OF THE SUMMONER'S AREA OF EFFECT, so
			// this reads the stated radius rather than the scaled one. The
			// design names area of effect among what a minion does not take
			// from its summoner, and the only way in is a stat that says
			// "minion".
			//
			// THAT STAT DOES NOT EXIST YET. The project owner asked for one on
			// 2026-08-24 and it is #910, waiting on #340 alongside the four
			// minion affixes: no minion stat has an attribute, and nothing
			// reads MinionScaling.csv. So this is a recorded gap rather than
			// an oversight, and the line below is deliberate. Issue #895.
			Oldest->Explode(Params.RadiusCm, GetDamagePercent());
		}
	}

	const float Lifetime = Params.Duration > 0.0f ? Params.Duration : 20.0f;

	// WHAT IT SUMMONS, WHICH THIS COULD NOT SAY BEFORE ISSUE #622. The row's
	// Minions parameter names it -- Summon Imp writes `Imp:1` and Cinder Swarm
	// writes `Mote:2` -- and until that parameter could be parsed at all, every
	// summon in the game spawned the same creature carrying the same
	// compile-time constants.
	//
	// THE FIRST TYPE, BECAUSE A SUMMON NAMES EXACTLY ONE. All three summoning
	// skills in game/Data/WeaponSkills.csv name a single kind. Producing more
	// than one kind at once is what Iron Fortress does, and that is the
	// Deployable shape with its own template.
	const FString SummonedType =
		Params.Minions.IsEmpty() ? FString() : Params.Minions[0].Type;

	ACataclysmMinion* Minion = ACataclysmMinion::Spawn(
		Self, RiftLocation, Lifetime, Params.bBurns, SummonedType);
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
	if (Self && Params.FinalHitPercent > 0.0f && ScaledRadiusCm() > 0.0f)
	{
		const TArray<AActor*> Caught = UCataclysmTargeting::FindEnemiesInSphere(
			GetWorld(), Self, RiftLocation, ScaledRadiusCm());
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
		GetWorld(), Self, Self->GetActorLocation(), ScaledRadiusCm());

	const float Period = Params.Interval > 0.0f ? Params.Interval : 1.0f;
	HitTargets(Inside, GetDamagePercent() * Period);

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
