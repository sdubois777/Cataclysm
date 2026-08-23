// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmCastEffect.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagsManager.h"

UCataclysmSkillTemplate::UCataclysmSkillTemplate()
{
	// Left at None on the class default. Which slot a skill occupies is decided
	// by the weapon that granted it, not by which template implements it: the
	// same Projectile class fills the Heavy slot for a Staff and the Special
	// slot for a Greataxe. GiveAbilityInSlot stamps it per grant.
	Slot = ECataclysmAbilitySlot::None;
}

float UCataclysmSkillTemplate::GetDamagePercent() const
{
	// THE SKILL'S OWN FIGURE FIRST, AND THAT ORDER IS THE WHOLE POINT.
	// A slot is a key: any skill may go in any slot, so a skill taking its
	// damage from whichever key it was put on would be worth 250% of weapon
	// damage on the right mouse button and 400% on R. Decided 2026-08-22;
	// see docs/DECISIONS.md and issue #836.
	if (DamagePercentOverride >= 0.0f)
	{
		return DamagePercentOverride;
	}

	// THE SLOT'S FIGURE WHEN THE SKILL STATES NONE, which every skill in
	// the game does today. That is what makes this landable before the 112
	// designed skills have numbers written: nothing behaves differently
	// until one does.
	const UDataTable* Table = UCataclysmSkillSlots::LoadGeneratedTable();
	const FCataclysmSkillSlotNumbers Numbers =
		UCataclysmSkillSlots::NumbersFor(Table, Slot);
	return Numbers.bFound ? Numbers.DamagePercent : 0.0f;
}

FGameplayTag UCataclysmSkillTemplate::ElementTag() const
{
	// ASKED OF THE TAG MANAGER RATHER THAN MATCHED BY STRING, so a tag renamed
	// in the workbook is renamed here too. RequestGameplayTag with
	// ErrorIfNotFound false returns an invalid tag when Element is not a
	// registered parent, which cannot happen while the generated tag list has
	// eight children under it, but costs nothing to allow for.
	static const FGameplayTag Element =
		UGameplayTagsManager::Get().RequestGameplayTag(
			FName(TEXT("Element")), /*ErrorIfNotFound=*/false);
	if (!Element.IsValid())
	{
		return FGameplayTag();
	}

	// The first, not every one. A row of the Weapon Skills sheet has exactly one
	// damage type because the sheet is a matrix of weapon against damage type,
	// and Cataclysm.Data.EverySkillRowCarriesOneElementTag holds that.
	for (const FGameplayTag& Tag : SkillTags)
	{
		if (Tag.MatchesTag(Element) && Tag != Element)
		{
			return Tag;
		}
	}
	return FGameplayTag();
}

bool UCataclysmSkillTemplate::CommitAndBegin(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	// CommitAbility is what runs ApplyCost and ApplyCooldown. Issue #155 wrote
	// both and nothing called them, because the only ability in the project was
	// the placeholder, which ends immediately and commits nothing.
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo,
				   /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
		return false;
	}

	PayHealthCost();

	// THE BURST AT THE CASTER, AND THIS IS THE ONLY PLACE IT IS ASKED FOR.
	// Every one of the eight skill shapes calls this function first, so one call
	// here gives all of them the beat that was missing: a skill used to begin
	// with nothing happening at the caster at all. See UCataclysmCastEffect for
	// why that matters and why this fires at the moment of release rather than
	// before it. Issue #811.
	//
	// AFTER THE COMMIT, NOT BEFORE IT. CommitAbility returns false when the cost
	// or the cooldown refuses the skill, and this line is past that return, so a
	// skill that did not fire draws nothing. A flash on a refused skill would
	// read as a bug.
	//
	// ITS RETURN VALUE IS DELIBERATELY DROPPED. Null is the ordinary answer past
	// the effect type's cull distance, outside the view frustum, and in every
	// automation test, which runs with -nullrhi. None of those is a reason not
	// to use the skill.
	if (AActor* Self = Avatar())
	{
		UCataclysmCastEffect::PlayFor(
			Self, AimDirection(),
			UCataclysmCastEffect::DamageTypeFor(Self, ElementTag()),
			Params.RadiusCm);
	}

	return true;
}

AActor* UCataclysmSkillTemplate::Avatar() const
{
	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	return Info ? Info->AvatarActor.Get() : nullptr;
}

FVector UCataclysmSkillTemplate::AimPoint() const
{
	const AActor* Self = Avatar();
	const FVector Fallback = Self ? Self->GetActorLocation() : FVector::ZeroVector;

	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	APlayerController* Controller = Info ? Info->PlayerController.Get() : nullptr;
	if (!Controller)
	{
		// An enemy or a minion using a skill. There is no cursor, so the caster's
		// own position is the only answer available.
		return Fallback;
	}

	FHitResult Hit;
	if (Controller->GetHitResultUnderCursor(ECollisionChannel::ECC_Visibility,
											/*bTraceComplex=*/true, Hit))
	{
		return Hit.Location;
	}

	// The cursor is over the sky or past the edge of the floor. Falling back to
	// the caster is what makes a targeted skill fire at their feet rather than
	// at the world origin, which is where an un-hit trace would otherwise put it.
	return Fallback;
}

FVector UCataclysmSkillTemplate::AimDirection() const
{
	const AActor* Self = Avatar();
	if (!Self)
	{
		return FVector::ForwardVector;
	}

	FVector Direction = AimPoint() - Self->GetActorLocation();
	Direction.Z = 0.0f;

	// Nearly zero means the cursor is on the caster, or there is no cursor at
	// all. Either way there is no aimed direction, so the character's own facing
	// is the only sensible answer -- and it is never the zero vector.
	if (Direction.IsNearlyZero())
	{
		FVector Facing = Self->GetActorForwardVector();
		Facing.Z = 0.0f;
		return Facing.IsNearlyZero() ? FVector::ForwardVector : Facing.GetSafeNormal();
	}

	return Direction.GetSafeNormal();
}

FVector UCataclysmSkillTemplate::AimedPointWithin(float RangeCm) const
{
	const AActor* Self = Avatar();
	if (!Self)
	{
		return FVector::ZeroVector;
	}

	const FVector Origin = Self->GetActorLocation();
	if (RangeCm <= 0.0f)
	{
		return Origin;
	}

	// Clamped to the range, so aiming past a skill's reach fires it as far as it
	// goes rather than refusing. Held at the caster's own height, because a
	// cursor trace lands on the floor and a projectile starting at the floor
	// would pass under everything it should hit.
	const FVector Aim = AimPoint();
	FVector Offset = FVector(Aim.X, Aim.Y, Origin.Z) - Origin;
	if (Offset.IsNearlyZero())
	{
		return Origin + AimDirection() * RangeCm;
	}
	if (Offset.SizeSquared() > RangeCm * RangeCm)
	{
		Offset = Offset.GetSafeNormal() * RangeCm;
	}
	return Origin + Offset;
}

float UCataclysmSkillTemplate::HitTargets(const TArray<AActor*>& Targets,
										  float DamagePercent)
{
	AActor* Self = Avatar();
	if (!Self || Targets.IsEmpty())
	{
		return 0.0f;
	}

	const float Percent = DamagePercent >= 0.0f ? DamagePercent : GetDamagePercent();

	// THIS SKILL'S OWN CRITICAL STRIKE CHANCE TRAVELS WITH EVERY BLOW IT DEALS.
	// It is -1 for every skill in the game today, which means "take the
	// character's attribute" and is exactly what happened before this existed.
	// Sent per hit rather than written onto the character because a character
	// holds six skills at once and has one CritChance attribute. Issue #657.
	FCataclysmHitDelivery Delivery;
	Delivery.CritChancePercent = CritChancePercent;

	float Total = 0.0f;
	for (AActor* Target : Targets)
	{
		const float Dealt = UCataclysmSkillEffects::ApplyHit(Self, Target, Percent,
															SkillTags, Delivery);
		Total += Dealt;

		// The burn is a share of the hit that caused it, so a skill that deals
		// no damage sets nothing alight. That is right for a Support skill,
		// whose slot damage is zero by design, and it is why Subjugate reads
		// "subjugating an enemy that is ALREADY burning" rather than burning it
		// itself.
		if (Params.bBurns && Dealt > 0.0f)
		{
			UCataclysmSkillEffects::ApplyBurn(Self, Target, Dealt);
		}

		// KNOCKBACK IS APPLIED HERE, WHICH IS WHAT MAKES IT A RIDER. It used to
		// live inside UCataclysmStrikeSkill::SwingOnce, so only a Strike could
		// shove. Issue #626 moved it: displacement is not specific to one kind of
		// skill, and while it was a Strike parameter Shockwave Leap knocked back
		// in its prose and could not say so in its data. Every template that hits
		// anything comes through this function, so every one of them can now
		// shove.
		//
		// NOT SCALED BY THE DAMAGE DEALT, deliberately. A Support skill deals no
		// damage by design and can still push, which is what Forge Stance's
		// opposite number would be. That is the difference between this and the
		// burn above.
		ApplyKnockbackTo(Self, Target);
	}

	// MANA ON HIT, WHICH ONLY THE BASIC ATTACK HAS. SkillSlots.csv gives the
	// Basic row 6 and every other row zero, so this is inert for the other six
	// slots rather than a special case carved out for one of them.
	//
	// PAID ONCE PER LANDED USE, NOT ONCE PER TARGET. The design states the
	// arithmetic it has to satisfy -- "returns 6 mana each time it lands. At a
	// typical 1.3 attacks per second that is about 8 mana per second" -- and 6
	// times 1.3 is 7.8, so the 6 is per swing. Paying per target would turn an
	// area basic attack into a mana engine, and the design's own reason for the
	// mechanic is that it is "income for being in a fight rather than a filler
	// action".
	//
	// ONLY WHEN SOMETHING WAS ACTUALLY DEALT, which is what "lands" means. A
	// swing that was evaded, or that armour and resistance stopped completely,
	// returns nothing.
	if (Total > 0.0f)
	{
		ApplyManaOnHit();
	}

	return Total;
}

void UCataclysmSkillTemplate::ApplyManaOnHit() const
{
	const float Gained = GetManaOnHit();
	if (Gained <= 0.0f)
	{
		return;
	}

	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	UAbilitySystemComponent* AbilitySystem =
		Info ? Info->AbilitySystemComponent.Get() : nullptr;
	if (!AbilitySystem)
	{
		return;
	}

	// APPLIED DIRECTLY RATHER THAN THROUGH A GAMEPLAY EFFECT ASSET, the same way
	// UCataclysmGameplayAbility::ApplyCost spends mana, and for the same reason:
	// the magnitude comes from a generated table, so there is no authored asset
	// to carry it, and an effect built for every landed hit would allocate on
	// every swing.
	//
	// THE CLAMP IN PreAttributeChange IS WHAT STOPS IT OVERFILLING, so this does
	// not check the maximum itself.
	AbilitySystem->ApplyModToAttribute(
		UCataclysmVitalAttributeSet::GetManaAttribute(),
		EGameplayModOp::Additive, Gained);
}

void UCataclysmSkillTemplate::ApplyKnockbackTo(AActor* Self, AActor* Target) const
{
	// THE RULE ITSELF LIVES IN UCataclysmSkillEffects, and this reads its own
	// distance out of the skill row and hands it over. It used to hold the whole
	// body -- the direction, the halving and the swept move -- and that made
	// displacement something only a player skill could do. An enemy attack is
	// C++ on the creature rather than a skill template, so the Brute's Stomp and
	// the Abyssal Warden's Stampede had no way to reach any of it. Issue #625
	// moved it out; there is one definition of a shove and both directions use it.
	UCataclysmSkillEffects::ApplyKnockback(Self, Target, Params.KnockbackCm);
}

ACataclysmGroundZone* UCataclysmSkillTemplate::LeaveGroundAt(const FVector& Location)
{
	// A patch is a path whose two ends are the same point.
	return LeaveGroundAlong(Location, Location);
}

ACataclysmGroundZone* UCataclysmSkillTemplate::LeaveGroundAlong(
	const FVector& Start, const FVector& End)
{
	if (!Params.LeavesGround())
	{
		return nullptr;
	}

	AActor* Self = Avatar();
	if (!Self)
	{
		return nullptr;
	}

	// THE GROUND STATES WHAT IT DEALS AND THIS READS IT. Every skill that leaves
	// ground carries a GroundPercent, added on issue #361: the percent of the
	// skill's own damage that patch deals per second, set so that standing in it
	// for its whole GroundDuration costs exactly one hit of the skill.
	//
	// IT USED TO BE DERIVED FROM THE BURN EFFECT INSTEAD, and that was wrong in a
	// way nothing reported. Burn is 20% of a hit over 4 seconds, so every patch
	// dealt 5% of the skill's damage per second whatever its own duration was --
	// which made a three second patch worth 15% of a hit and a ten second one
	// worth 50%. A longer patch was automatically a bigger one, which is the
	// exact property issue #361's rule was chosen to remove. Issue #590.
	if (Params.GroundPercent <= 0.0f)
	{
		// A patch with a radius and a duration and no stated damage would burn
		// visibly and hurt nobody, which reads as working. The generator writes
		// GroundPercent on all 22 rows that leave ground, so reaching here means
		// the imported table is older than the sheet.
		UE_LOG(LogCataclysm, Warning,
			TEXT("'%s' leaves ground and states no GroundPercent, so that ground "
				 "would deal nothing. None was left. Run "
				 "tools/generate_datatable_assets.py."),
			*SkillName);
		return nullptr;
	}

	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Self);
	const float WeaponDamage = UCataclysmSkillEffects::WeaponDamageOf(AbilitySystem);

	// PRICED WITH THE CASTER'S MODIFIERS APPLIED, and priced once, when the
	// ground is created. A patch outlives the skill that left it and can outlive
	// the buff that was up at the time, so the alternative -- reading the
	// caster's modifiers on every tick -- would make a buff that has expired
	// keep paying, or stop paying part way through a patch the player already
	// earned. The design says the ground burns for a duration, not that it
	// tracks the caster.
	const float PerTick = UCataclysmSkillEffects::ModifiedDamage(
							AbilitySystem,
							WeaponDamage * GetDamagePercent() / 100.0f,
							SkillTags)
						* Params.GroundPercent / 100.0f;

	return ACataclysmGroundZone::SpawnAlong(Self, Start, End, Params.GroundRadiusCm,
											Params.GroundDuration, PerTick);
}

void UCataclysmSkillTemplate::PayHealthCost()
{
	if (Params.HealthCostPercent <= 0.0f)
	{
		return;
	}

	UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Avatar());
	if (!AbilitySystem)
	{
		return;
	}

	// A PERCENT OF CURRENT HEALTH, NOT OF MAXIMUM, because Blood Pyre says so:
	// "paying 8% of your current health". That is what makes it self-limiting --
	// each cast costs less than the last, so it cannot kill the caster.
	const float Current = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetHealthAttribute());
	const float Cost = Current * Params.HealthCostPercent / 100.0f;
	if (Cost > 0.0f)
	{
		AbilitySystem->ApplyModToAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute(),
			EGameplayModOp::Additive, -Cost);
	}
}
