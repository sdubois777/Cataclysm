// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
// For the axe a Harrower leaves in what it hits, which tears free when
// that creature dies and buries itself in the next. Issue #37.
#include "AbilitySystem/CataclysmBuriedWeapon.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmProjectile.h"
// For the attack speed a rack of axes is thrown at. Issue #37.
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
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

	// EVERYTHING BELOW WAITS FOR THE WEAPON TO ARRIVE. Issue #1133. Until this,
	// the whole of it ran in the frame the ability activated while the swing
	// animation played for a second or more beside it, so an enemy took damage
	// and the arc was drawn while the weapon was still going backwards.
	//
	// THE END OF THE ABILITY IS INSIDE HERE FOR THE SAME REASON. Ending it on
	// the line after this call would cancel the wait and no blow would land at
	// all. `WhenTheSwingConnects` says so; this is the shape its comment points
	// at.
	//
	// THE HANDLE AND THE ACTOR INFORMATION ARE ASKED FOR AGAIN RATHER THAN
	// CAPTURED. `ActorInfo` is a raw pointer owned by the ability system and
	// holding it across a wait is not safe. `Finish` below already takes this
	// route, and it is the same three accessors.
	WhenTheSwingConnects([this]()
	{
		SwingsMade = 0;
		SwingOnce();

		AActor* Self = Avatar();
		if (Self)
		{
			// Under the caster's own feet. Molten Cleave drags its slag from
			// where the swing started; Pyroclasm leaves "the ground within 5
			// meters".
			LeaveGroundAt(Self->GetActorLocation());
		}

		// A single swing with no duration is over. Pyroclasm's spin is the
		// other case: it repeats for Duration and then lands its final hit.
		const bool bRepeats = Params.Duration > 0.0f && Params.Interval > 0.0f;
		UWorld* World = Self ? Self->GetWorld() : nullptr;
		if (!bRepeats || !World)
		{
			EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
					   GetCurrentActivationInfo(), true, false);
			return;
		}

		World->GetTimerManager().SetTimer(
			RepeatTimer, this, &UCataclysmStrikeSkill::Repeat,
			Params.Interval, /*bLoop=*/true, /*InFirstDelay=*/Params.Interval);
		World->GetTimerManager().SetTimer(
			FinishTimer, this, &UCataclysmStrikeSkill::Finish,
			Params.Duration, /*bLoop=*/false);
	});
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

	// THE FIRES GO OUT BEFORE THE BLOW LANDS, and that order is required rather
	// than tidy. Quench gives an enemy whose fire was just put out 50% more
	// damage and Extinction's damage rises with how many went out at once, so
	// the blow cannot be sized until this has run. Inert for every skill that
	// does not state `ConsumeBurn`, which is 51 of the 56 Demonic rows.
	const TArray<AActor*> Consumed = ConsumeBurnFrom(Targets);

	// The knockback goes with it. It used to be applied here, for Strikes only;
	// issue #626 moved it into HitTargets so that every shape can shove, which
	// is what making it a rider means.
	//
	// AND THE BLOW IS SIZED BY WHAT THIS SKILL SCALES WITH. `HitScaled` is
	// `HitTargets` for a skill that scales with nothing, which is most of them.
	// A caller passing its own figure -- Pyroclasm's closing hit -- overrides
	// both, because that figure is already the skill's own final word.
	//
	// THE ARC IS SET ALIGHT AFTER BEING CONSUMED, WHICH IS WHAT THE ROWS SAY.
	// Quench: "the whole arc is set alight anew behind the blade". Extinction:
	// "anything still standing is set alight again". `HitTargets` applies the
	// burn, so consuming first and hitting second delivers both sentences in
	// the order they are written.
	if (DamagePercent >= 0.0f)
	{
		HitTargets(Targets, DamagePercent);
	}
	else
	{
		HitScaled(Targets, Consumed);
	}

	// AND THE FIRE SPREADS FROM WHERE EACH ONE WENT OUT. Touch Off states three
	// metres of its own; Ashen Edge grants four to whatever the caster consumes
	// with while it runs. Nothing happens for a skill with neither.
	IgniteAroundConsumed(Consumed);

	// AND THE CURSE GOES ON WHATEVER THE SWING TOUCHED. The Wand's Anathema:
	// "laying every curse you know on it for 10 seconds: Demonic resistance cut
	// by 40%, and madness on all of them". Until 2026-09-01 only the Debuff
	// shape applied a named effect, so a Strike stating one applied nothing.
	//
	// AFTER THE BLOW RATHER THAN BEFORE IT, so a Shred cutting resistance does
	// not also reduce the damage of the hit that applied it. The row reads
	// "damn everything ... for 350% weapon damage, setting it alight and laying
	// every curse", in that order.
	for (AActor* Target : Targets)
	{
		ApplyNamedEffectsTo(Target);
	}

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
	//
	// WHERE IT IS AIMED IS FIXED WHEN THE PLAYER ASKS, NOT WHEN IT LEAVES.
	// Issue #1133 put a wait between the two, and reading the cursor at the far
	// end of that wait would let a shot curve toward a cursor moved during the
	// wind-up. It would also disagree with the body, which turned to face this
	// point in CommitAndBegin before the wait began.
	Destination = AimedPointWithin(Params.RangeCm);

	// AND IT LEAVES WHEN THE THROW REACHES ITS RELEASE. Issue #1133. Until then
	// the shot appeared in the frame the ability activated, before the arm had
	// moved.
	//
	// THE ORIGIN IS TAKEN AT THAT MOMENT AND NOT BEFORE IT, unlike the
	// destination, because it is where the projectile physically comes from and
	// the character may have walked during the wind-up. A shot leaving from
	// where the character used to stand would be visibly wrong.
	WhenTheSwingConnects([this]()
	{
		AActor* Caster = Avatar();
		if (!Caster)
		{
			EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
					   GetCurrentActivationInfo(), true, true);
			return;
		}

		Origin = Caster->GetActorLocation();

		// A RACK OF THIRTY RATHER THAN ONE THROW. The Axe's Butcher's Bill is
		// the only row that states a Count beside an Interval, and it needs the
		// ability to stay alive across all of them rather than ending when the
		// first axe lands.
		if (ThrowsRepeatedly())
		{
			BeginEmptyingTheRack();
			return;
		}

		// A REAL ACTOR WHEN IT HAS A SPEED. It moves in steps and sweeps each
		// one, so who it hits is decided by where they stood as it went past
		// rather than by where they stand when it arrives, and a wall stops it.
		// Issue #164.
		// THIS SKILL'S OWN CRITICAL STRIKE CHANCE GOES WITH IT. A projectile
		// lands after this ability has ended, so it has to carry the chance
		// rather than read it off the character on arrival. Named explicitly
		// because the two arguments before it are defaulted. Issue #657.
		//
		// AND WHAT THIS SKILL JUST COST, for the same reason and with more at
		// stake. Blood Pyre is the one skill in the game with a health cost of
		// its own and it is a projectile, so this is the route that matters most
		// for the Masochist's Grand Tithe node. Reading the cost when the shot
		// landed would credit the blow with whatever the character last paid.
		// Issue #983.
		InFlight = ACataclysmProjectile::Fire(
			Caster, Origin, Destination, ScaledRadiusCm(),
			Params.SpeedCmPerSecond, Params.Pierce, Params.bReturns,
			GetDamagePercent(), SkillTags, Params.bBurns,
			/*InBodyMesh=*/nullptr, /*InFlightSeconds=*/0.0f,
			CritChancePercent, LastHealthCostPercentOfMaximum);

		if (!InFlight)
		{
			// No speed, or nowhere to fly. A beam: Infernal Lance drives a lance
			// forward and its description says it arrives at once.
			LandThenFinish();
			return;
		}

		// AND IT GLANCES ONWARD IF THE ROW SAYS SO. The Axe's Carom: "it glances
		// from them to the next nearest and onward through three more ... every
		// enemy it touches after the first adds 20% to its damage."
		//
		// THE SKILL'S OWN RANGE IS HOW FAR A GLANCE LOOKS. The row states no
		// separate reach for it, and a glance that could find an enemy the throw
		// itself could never have reached is not what "the next nearest" means
		// for an eleven metre throw.
		//
		// THE INCREASE APPLIES ONLY WHEN THE ROW COUNTS BOUNCES. Carom writes
		// `ScalingSource=Bounce`; a projectile that bounced and scaled off
		// something else would be taking its number from the wrong thing.
		//
		// TWENTY PER CENT OF WHAT THE SKILL DEALS, NOT TWENTY PERCENTAGE POINTS
		// ADDED TO IT, which is the same reading `ScaledDamagePercent` uses for
		// every other skill: `IncreasedDamagePer` moves the skill's own damage
		// percent, so 20 on a 250% Heavy is 50 points a glance and the throw
		// runs 250, 300, 350, 400. Adding a flat 20 instead would make the row's
		// "adds 20% to its damage" false on every slot but a 100% one.
		if (Params.Bounces > 0)
		{
			const bool bScalesOnBounce = Params.ScalingSource.Equals(
				TEXT("Bounce"), ESearchCase::IgnoreCase);
			const float PerGlance = bScalesOnBounce
				? GetDamagePercent() * Params.IncreasedDamagePer / 100.0f
				: 0.0f;
			InFlight->GlancesOnward(
				Params.Bounces,
				Params.RangeCm > 0.0f ? Params.RangeCm : ScaledRadiusCm(),
				PerGlance);
		}

		InFlight->OnFinished.AddUObject(
			this, &UCataclysmProjectileSkill::OnProjectileFinished);
	});
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

		// AND THE CURSES ON WHAT IT STRUCK ARE COPIED OUTWARD. The Wand's
		// Malefice: "copying every curse it already carries onto the two nearest
		// enemies". Nothing read `SpreadCurses` before 2026-09-01.
		const TArray<AActor*> Struck = Projectile->StruckEnemies();
		CursesSpread += SpreadCursesFrom(Struck);

		// AND THE AXE STAYS IN WHAT IT HIT, if the row says so. The Axe's
		// Harrower: "the axe stays where it lands. When that enemy dies it tears
		// free and buries itself in the nearest living enemy within 10 meters."
		//
		// BURIED IN WHAT IT STRUCK, not where it stopped. A throw that hit
		// nothing leaves no axe, which is what "buries itself in an enemy"
		// means.
		BuryInStruck(Struck);
	}
	else
	{
		++Landings;
	}

	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
			   GetCurrentActivationInfo(), true, false);
}

int32 UCataclysmProjectileSkill::SpreadCursesFrom(const TArray<AActor*>& Struck)
{
	if (Params.SpreadCurses <= 0 || Struck.IsEmpty())
	{
		return 0;
	}

	AActor* Self = Avatar();
	if (!Self)
	{
		return 0;
	}

	const float ReachCm = Params.RangeCm > 0.0f ? Params.RangeCm : ScaledRadiusCm();
	const FName Type = DamageTypeName();

	int32 Applied = 0;
	for (AActor* Cursed : Struck)
	{
		if (!IsValid(Cursed))
		{
			continue;
		}

		// NEAREST TO THE CURSED ENEMY, which `FindEnemiesInSphere` answers in
		// order. Searched around that enemy rather than around the caster,
		// because the curse spreads outward from the creature carrying it.
		TArray<AActor*> Nearby = UCataclysmTargeting::FindEnemiesInSphere(
			GetWorld(), Self, Cursed->GetActorLocation(), ReachCm);

		// NOT BACK ONTO THE ONE IT CAME FROM, and not onto anything the bolt
		// already struck: those carry the curse already, and re-applying would
		// refresh a duration the row says nothing about.
		Nearby.RemoveAll([&Struck](AActor* One)
		{
			return Struck.Contains(One);
		});

		if (Nearby.Num() > Params.SpreadCurses)
		{
			Nearby.SetNum(Params.SpreadCurses);
		}

		Applied += UCataclysmSkillEffects::CopyDebuffsTo(Self, Cursed, Nearby, Type);
	}

	return Applied;
}

bool UCataclysmProjectileSkill::ThrowsRepeatedly() const
{
	return Params.Count > 1 && Params.Interval > 0.0f;
}

float UCataclysmProjectileSkill::SecondsBetweenThrows() const
{
	const float Stated = FMath::Max(Params.Interval, 0.0f);
	if (!Params.bScalesWithAttackSpeed || Stated <= 0.0f)
	{
		return Stated;
	}

	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Avatar());
	if (!AbilitySystem)
	{
		return Stated;
	}

	const float PerSecond = AbilitySystem->GetNumericAttribute(
		UCataclysmCombatAttributeSet::GetAttackSpeedAttribute());
	if (PerSecond <= 0.0f)
	{
		// A character with no weapon equipped has no attack speed at all. The
		// stated interval is the right answer rather than an infinite one.
		return Stated;
	}

	return Stated / PerSecond;
}

AActor* UCataclysmProjectileSkill::NextThrowTarget()
{
	AActor* Self = Avatar();
	if (!Self)
	{
		return nullptr;
	}

	// ASKED AFRESH FOR EVERY THROW. A rack empties over ten seconds and the
	// enemies move and die during it, so a list gathered once would go on
	// throwing at corpses and at places nobody is standing any more.
	const TArray<AActor*> InRange = UCataclysmTargeting::FindEnemiesInSphere(
		GetWorld(), Self, Self->GetActorLocation(), Params.RangeCm);
	if (InRange.IsEmpty())
	{
		return nullptr;
	}

	// `Furthest` is the reverse of the nearest-first order the search answers in.
	if (Params.TargetMode.Equals(TEXT("Furthest"), ESearchCase::IgnoreCase))
	{
		return InRange.Last();
	}

	// `Nearest`, and anything the row does not name, takes the first.
	if (!Params.TargetMode.Equals(TEXT("All"), ESearchCase::IgnoreCase))
	{
		return InRange[0];
	}

	// `All` MEANS SPREAD ACROSS THEM RATHER THAN HIT THEM ALL AT ONCE. Butcher's
	// Bill throws "at every enemy within 10 meters" over ten seconds, one axe at
	// a time, so each throw takes the next in turn and the rack covers the group.
	// The index is kept rather than recomputed because the list changes as
	// enemies die, and a modulo over a shrinking list still walks all of it.
	const int32 Pick = NextTargetIndex % InRange.Num();
	++NextTargetIndex;
	return InRange[Pick];
}

bool UCataclysmProjectileSkill::ThrowOne()
{
	// THE COUNT IS A LIMIT AND NOT A TARGET. Stopping the timer is not
	// enough on its own: `ThrowOne` is public so a test can drive a rack
	// without waiting, and a caller asking for one more after the rack is
	// empty must get nothing. A test caught it, at five throws from a row
	// stating four.
	if (Params.Count > 0 && ThrowsMade >= Params.Count)
	{
		return false;
	}

	AActor* Self = Avatar();
	AActor* Target = NextThrowTarget();
	if (!Self || !Target)
	{
		// Nothing left in reach. The rack keeps its remaining axes rather than
		// throwing them at the floor, and the timer below brings it back when
		// something walks into range.
		return false;
	}

	// FROM WHERE THE CHARACTER IS NOW, not from where the rack started. Ten
	// seconds is long enough to walk across a room.
	const FVector From = Self->GetActorLocation();

	ACataclysmProjectile* Axe = ACataclysmProjectile::Fire(
		Self, From, Target->GetActorLocation(), ScaledRadiusCm(),
		Params.SpeedCmPerSecond, Params.Pierce, Params.bReturns,
		GetDamagePercent(), SkillTags, Params.bBurns,
		/*InBodyMesh=*/nullptr, /*InFlightSeconds=*/0.0f,
		CritChancePercent, LastHealthCostPercentOfMaximum);

	// NOTHING IS HOOKED TO ITS FINISH, unlike a single throw. Thirty axes are in
	// the air at once and the ability must not end when the first of them lands;
	// what ends it is the count or the rack's own time, below.
	++ThrowsMade;
	++Landings;

	if (!Axe)
	{
		// No speed stated, so it arrives at once. Butcher's Bill states 2000 and
		// nothing else empties a rack, so this is a guard rather than a case.
		UCataclysmSkillEffects::ApplyHit(Self, Target, GetDamagePercent(),
										 SkillTags, FCataclysmHitDelivery());
	}

	if (ThrowsMade >= Params.Count)
	{
		StopThrowing();
	}
	return true;
}

void UCataclysmProjectileSkill::ThrowTick()
{
	ThrowOne();
}

void UCataclysmProjectileSkill::BeginEmptyingTheRack()
{
	ThrowsMade = 0;
	NextTargetIndex = 0;

	// THE FIRST GOES AT ONCE, so a skill with a long interval is not silent for
	// a third of a second after the button was pressed.
	ThrowOne();
	if (ThrowsMade >= Params.Count)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		StopThrowing();
		return;
	}

	const float Interval = SecondsBetweenThrows();
	World->GetTimerManager().SetTimer(
		ThrowTimer, this, &UCataclysmProjectileSkill::ThrowTick,
		Interval, /*bLoop=*/true, /*InFirstDelay=*/Interval);

	// AND A CEILING ON HOW LONG THE RACK MAY TAKE. At one attack a second the
	// count and this land together -- thirty axes at 0.333 seconds is exactly
	// the ten the row states -- so this only bites when there was nothing to
	// throw at for part of it. Scaled the same way the interval is, or a faster
	// character would be cut off early.
	if (Params.Duration > 0.0f)
	{
		const float Ceiling = Params.bScalesWithAttackSpeed && Params.Interval > 0.0f
			? Params.Duration * Interval / Params.Interval
			: Params.Duration;
		World->GetTimerManager().SetTimer(
			RackTimer, this, &UCataclysmProjectileSkill::StopThrowing,
			Ceiling, /*bLoop=*/false);
	}
}

void UCataclysmProjectileSkill::StopThrowing()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ThrowTimer);
		World->GetTimerManager().ClearTimer(RackTimer);
	}

	UE_LOG(LogCataclysm, Verbose, TEXT("'%s' emptied its rack: %d thrown."),
		*SkillName, ThrowsMade);

	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
			   GetCurrentActivationInfo(), true, false);
}

int32 UCataclysmProjectileSkill::BuryInStruck(const TArray<AActor*>& Struck)
{
	if (Struck.IsEmpty() || Params.OnDeathRangeCm <= 0.0f
		|| !Params.OnDeath.Equals(TEXT("Leap"), ESearchCase::IgnoreCase))
	{
		return 0;
	}

	AActor* Self = Avatar();
	if (!Self)
	{
		return 0;
	}

	int32 Buried = 0;
	for (AActor* Target : Struck)
	{
		// THE SKILL'S OWN DAMAGE AND ITS OWN TAGS GO WITH IT, because the axe
		// keeps striking long after the skill that threw it has ended. Reading
		// either off the character at the moment it leaps would credit the blow
		// to whatever the player used last.
		if (UCataclysmBuriedWeapon::BuryIn(Target, Self, Params.OnDeathRangeCm,
										   GetDamagePercent(), SkillTags,
										   Params.bBurns))
		{
			++Buried;
		}
	}

	if (Buried > 0)
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("'%s' left its weapon in %d enemies, reaching %.0fcm for the "
				 "next."),
			*SkillName, Buried, Params.OnDeathRangeCm);
	}

	return Buried;
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

	// Burning Wrath is "4% more fire damage for every enemy currently
	// burning within 15 meters", so the count is taken once, when it goes up,
	// not continuously. An enemy that dies or stops burning during the ten
	// seconds does not lower it, and one that catches fire does not raise it.
	// RESET ON EVERY CAST. An ability is instanced per actor, so one instance
	// stands for one character's Butcher's Heat across every use of it: a tally
	// left from the last use would start the next one already hot.
	KillsCounted = 0;
	TotalDuration = Params.Duration;

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

	// TWO SOURCES ARE COUNTED AND THEY ARE COUNTED DIFFERENTLY. `Burning` is a
	// number taken once, when the buff goes up, because Burning Wrath's sentence
	// says "currently burning" at that moment. `Kill` is a running tally,
	// because Butcher's Heat says "every enemy you kill while it lasts". Every
	// other source answers zero and grants nothing, rather than silently
	// counting the wrong thing; see the note on ScalingSource in
	// CataclysmSkillShape.h.
	const int32 Units = ScalingCount();
	if (Params.MoreDamagePer <= 0.0f || Units <= 0)
	{
		// No increase to grant. Burning Wrath with nothing alight nearby is the
		// ordinary case, not a fault: the skill is written to be worth using
		// only after something has been set on fire. Butcher's Heat before its
		// first kill is the same case.
		return;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Avatar()));
	if (!AbilitySystem)
	{
		return;
	}

	// THE MULTIPLICATIVE BUCKET, CHANGED 2026-09-01. Burning Wrath reads "4%
	// more fire damage for every enemy currently burning", and "more" is this
	// project's word -- section VI's -- for a multiplier that applies separately
	// rather than joining the additive sum. In the additive bucket the buff
	// competed with every gear affix the character wore, which is what made a
	// kill-fed or count-fed ramp feel like nothing.
	FCataclysmStatModifier Modifier;
	Modifier.Bucket = ECataclysmStatBucket::More;
	Modifier.Source = ECataclysmModifierSource::SkillBuff;
	Modifier.Value = Params.MoreDamagePer * Units;

	// SCOPED TO THE SKILL'S OWN ELEMENT, so the rule is in the data and not
	// here. Burning Wrath carries Element.Demonic, which is this project's fire,
	// so "more fire damage" is a multiplier that reaches skills carrying
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
		TEXT("'%s' granted %.0f%% more damage scoped to %s, from %d of '%s', "
			 "for %.1fs."),
		*SkillName, GrantedIncrease,
		GrantedScope.IsValid() ? *GrantedScope.ToString() : TEXT("everything"),
		Units, *Params.ScalingSource, TotalDuration);
}

int32 UCataclysmSelfBuffSkill::ScalingCount() const
{
	if (Params.ScalingSource.Equals(TEXT("Burning"), ESearchCase::IgnoreCase))
	{
		return BurningEnemiesAtCast;
	}
	if (Params.ScalingSource.Equals(TEXT("Kill"), ESearchCase::IgnoreCase))
	{
		return KillsCounted;
	}

	// One of the nine sources nothing counts. A buff naming one grants nothing,
	// which is the safe answer: the alternative is a number taken from whatever
	// happened to be to hand.
	return 0;
}

void UCataclysmSelfBuffSkill::NoteKill()
{
	if (!Params.ScalingSource.Equals(TEXT("Kill"), ESearchCase::IgnoreCase))
	{
		return;
	}

	++KillsCounted;

	// PUT BACK RATHER THAN ADJUSTED. A stat modifier is held by a handle and has
	// no route to change its value in place, so raising the bonus means taking
	// the old one off and putting a bigger one on. `GrantIncrease` reads the new
	// tally through `ScalingCount`.
	RevokeIncrease();
	GrantIncrease();

	// AND THE HEAT LASTS LONGER. "Adds another second to the heat", said by
	// `DurationPer`, which no other row states.
	//
	// THE TIMER IS RESET TO WHAT IS LEFT PLUS THE EXTENSION, not simply extended,
	// because a timer manager has no way to add to a running timer. Reading what
	// remains and setting that plus the extra is the same thing done in the one
	// step the interface offers.
	if (Params.DurationPer <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Remaining = World->GetTimerManager().GetTimerRemaining(FinishTimer);
	if (Remaining <= 0.0f)
	{
		// Already finishing. Lengthening a buff that has run out would bring it
		// back, which is not what "adds another second" means.
		return;
	}

	TotalDuration += Params.DurationPer;
	World->GetTimerManager().SetTimer(
		FinishTimer, this, &UCataclysmSelfBuffSkill::Finish,
		Remaining + Params.DurationPer, /*bLoop=*/false);
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

FVector UCataclysmMovementSkill::ConditionalDestination(const FVector& Start) const
{
	const bool bToBurning = RequiresCondition(TEXT("Burning"));
	const bool bToAnything = RequiresCondition(TEXT("Target"));
	if (!bToBurning && !bToAnything)
	{
		// Every other movement skill goes where the player pointed. Eight of the
		// ten Demonic movement rows take this line.
		return AimedPointWithin(Params.RangeCm);
	}

	const AActor* Self = Avatar();
	if (!Self)
	{
		return AimedPointWithin(Params.RangeCm);
	}

	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	for (AActor* Candidate : UCataclysmTargeting::FindEnemiesInSphere(
			GetWorld(), Self, Start, RequirementReachCm()))
	{
		if (bToBurning && Burn.IsValid()
			&& !UCataclysmSkillEffects::HasTag(Candidate, Burn))
		{
			continue;
		}

		// AT THE ENEMY'S FEET AND NOT INSIDE IT. `SetActorLocation` sweeps, so
		// aiming at the creature itself stops the caster against its collision
		// short of where the row says it arrives. This is the same point the
		// creature is standing on, which is what "haul yourself to it" means.
		return FVector(Candidate->GetActorLocation().X,
					   Candidate->GetActorLocation().Y, Start.Z);
	}

	return AimedPointWithin(Params.RangeCm);
}

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

	// WHERE IT ARRIVES IS THE ENEMY, NOT THE CURSOR, WHEN THE ROW SAYS SO. The
	// Sword's Flashpoint reads "dart to a burning enemy up to 14 meters away"
	// and closes with "only something already alight can be reached", so the
	// destination is decided by the condition rather than by where the player is
	// pointing. `CanActivateAbility` has already refused the cast if there is no
	// such enemy, so failing to find one here means it died in between; falling
	// back to the aimed point is then better than not moving at all.
	//
	// THE AXE'S EMBERHAUL WORKS THE SAME WAY through `Requires=Target`: "bury
	// your axe in the FIRST enemy within 12 meters and haul yourself to it".
	const FVector End = ConditionalDestination(Start);

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

	// THE FIRE IS PUT OUT ON ARRIVAL, BEFORE THE BLOW. Flashpoint: "consuming
	// their fire on arrival to burst for damage in a 3 meter radius and set
	// alight everything the burst catches". The blow that follows re-lights what
	// it damages, so consuming and then hitting delivers both halves in order.
	// Inert for a movement skill that does not state `ConsumeBurn`.
	const TArray<AActor*> Consumed = ConsumeBurnFrom(Targets);
	HitScaled(Targets, Consumed);
	IgniteAroundConsumed(Consumed);

	// AND THE COOLDOWN COMES BACK IF THE ARRIVAL KILLED. The Axe's Emberhaul:
	// "if the arrival kills them, the axe comes back ready to throw again."
	//
	// ASKED OF THE TARGETS THIS MOVE HIT, and not of any kill anywhere. The row
	// says the ARRIVAL has to kill, so a creature that happened to die of a burn
	// somewhere else does not return the axe.
	//
	// AFTER THE BLOW, because a target is not dead until it has been struck. The
	// death is recorded by whatever the damage killed, which runs inside
	// `HitScaled` above.
	if (!Params.RefundsCooldown.IsEmpty())
	{
		for (AActor* Target : Targets)
		{
			// EITHER THE DEATH HAS BEEN RECORDED OR THE HEALTH HAS RUN OUT.
			// Asking only whether it is marked dead ties this to the order in
			// which the damage and the death path run, and a creature at no
			// health has been killed by the arrival whichever of the two got
			// there first.
			if (UCataclysmSkillEffects::IsDead(Target) || AtNoHealth(Target))
			{
				RefundCooldown();
				break;
			}
		}
	}

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
	//
	// AND IT NEEDS A RELEASE FIRST, WHICH IS THE WHOLE OF ISSUE #1114. The press
	// this reacts to arrives EVERY FRAME the key is held, because
	// `ACataclysmPlayerController::Input_AbilitySlotPressed` is bound to
	// `ETriggerEvent::Triggered`. Without waiting for a release, holding the key
	// switched the aura off on one frame and
	// `UCataclysmAbilitySystemComponent::ProcessAbilityInput` started it again
	// on the next, and every start paid a full health cost. One press by the
	// project owner on 2026-08-31 charged five costs of about 253 health inside
	// 117 milliseconds and killed them.
	//
	// EVERY OTHER SLOT WAS SAFE FROM THIS BECAUSE IT HAS A COOLDOWN. Holding a
	// key on those repeats the cast, which issue #1016's comment says is wanted,
	// and the cooldown decides how often. The aura is the only slot with none.
	if (bHeld && bKeyReleasedSinceActivation)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo,
				   /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
	}
}

void UCataclysmAuraSkill::InputReleased(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputReleased(Handle, ActorInfo, ActivationInfo);

	// THE RELEASE ENDS NOTHING, AND ARMS THE SWITCH-OFF. A toggle stays on when
	// the key comes up; that is what makes it a toggle rather than a hold. What
	// the release changes is that the NEXT press is a new press rather than
	// another frame of the one that started the aura.
	bKeyReleasedSinceActivation = true;
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

	// AND THE KEY COUNTS AS STILL DOWN UNTIL A RELEASE ARRIVES. Issue #1114.
	// Cleared here rather than in `EndAbility`, because the aura also ends for
	// reasons that are not a key press -- running out of mana, or a duration
	// expiring -- and a fresh activation is the only moment at which "the key
	// has not been let go yet" is true again.
	bKeyReleasedSinceActivation = false;

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

	// THE CURSE LANDS WHEN THE GESTURE REACHES ITS POINT. Issue #1133. Until
	// then it was applied in the frame the ability activated, with the body
	// still winding up.
	//
	// WHO IT TAKES IS DECIDED AT THAT MOMENT AND NOT BEFORE IT. The search below
	// is deliberately inside the wait: enemies move, and a curse that picked its
	// victim at the start of a wind-up and applied it at the end would curse
	// whoever used to be nearest.
	WhenTheSwingConnects([this]()
	{
		AActor* Caster = Avatar();
		if (!Caster)
		{
			EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
					   GetCurrentActivationInfo(), true, true);
			return;
		}

		// IN RANGE OF THE CASTER, ORDERED BY WHERE THE PLAYER IS POINTING. The
		// first attempt searched a small circle at the aim point, and it was
		// wrong twice over: a cursor a metre off the enemy took nobody, and with
		// no cursor at all -- an enemy casting, or a test -- the aim ran out to
		// the full 15 metres and found empty ground.
		//
		// Range bounds who can be reached, which is what Subjugate's "up to 15
		// meters" means; the cursor only decides which of those is picked. So a
		// player pointing roughly at an enemy takes that enemy, and pointing at
		// nothing takes the nearest, which is what a single-target curse should
		// do.
		TArray<AActor*> InRange = UCataclysmTargeting::FindEnemiesInSphere(
			GetWorld(), Caster, Caster->GetActorLocation(), Params.RangeCm);

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

		// THE DURATION COMES FROM `EffectDuration` AND USED TO COME FROM
		// `Duration`, which is a different number and one that no Debuff row in
		// the game states. The sheet split the two on 2026-09-01 -- a skill's own
		// length against the length of what it leaves behind -- and this was not
		// changed with it, so all three Debuff-shaped skills asked for a curse
		// lasting zero seconds and `ApplyTagForDuration` refused every one.
		// `AppliedEffectSeconds` reads the right key and falls back to the
		// effect's own designed duration.
		//
		// AND MORE THAN ONE EFFECT MAY BE NAMED. Anathema writes
		// `Effect=Shred, Madness`; the Wand's verb is inflicting, so a curse
		// arriving with company is the ordinary case rather than an oddity.
		const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();

		for (AActor* Target : Targets)
		{
			// "The whisper lasts twice as long in a mind that is already
			// burning." Read off the target's own tags, so any source of burn
			// counts and not only this character's.
			const bool bAlreadyBurning =
				UCataclysmSkillEffects::HasTag(Target, Burn);
			const float Scale = bAlreadyBurning ? 2.0f : 1.0f;

			if (ApplyNamedEffectsTo(Target, Scale) > 0)
			{
				// The first named effect's length, which is every skill's whole
				// answer today: no row names two effects of different durations.
				const TArray<FGameplayTag> Named = NamedEffectTags();
				LastDurationApplied =
					Named.IsEmpty() ? 0.0f
									: AppliedEffectSeconds(Named[0]) * Scale;
				++EnemiesAffected;
			}
		}

		// A Support slot deals no damage by design, so this only lands a hit for
		// a debuff whose slot has one.
		HitTargets(Targets);

		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
				   GetCurrentActivationInfo(), true, false);
	});
}
