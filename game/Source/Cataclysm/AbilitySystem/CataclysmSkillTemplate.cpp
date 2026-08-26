// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmCastEffect.h"
// For the health cost a character adds to every skill. Issue #970.
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
// For turning a health cost into Fervour. Issue #954.
#include "AbilitySystem/CataclysmFervour.h"
// For the part of a cost that is taken later instead of now. Issue #991.
#include "AbilitySystem/CataclysmHealthDebt.h"
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
#include "Items/CataclysmItem.h"

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
			ScaledRadiusCm());
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

	// AND WHAT THIS SKILL JUST COST TRAVELS WITH EVERY BLOW IT DEALS, for the
	// same reason the critical strike chance does: it belongs to the skill
	// rather than to the character, and `ApplyHit` receives the skill's tags and
	// not the skill. The Masochist's Grand Tithe node is what reads it.
	// Issue #983.
	//
	// -1 UNTIL THE SKILL HAS BEEN USED, which cannot happen here: every one of
	// the eight skill shapes goes through `CommitAndBegin` first, and that calls
	// `PayHealthCost`, which writes this on every use whether it charged
	// anything or not.
	Delivery.SkillHealthCostPercent = LastHealthCostPercentOfMaximum;

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

float UCataclysmSkillTemplate::AreaOfEffectMultiplier() const
{
	// A HUNDRED MEANS UNCHANGED, which is what the design gives area of effect
	// and the three damage over time stats: "They are percentages of whatever
	// the skill or the effect itself does, so their baseline is 100% rather than
	// zero." AsMultiplierForSkill is the same reading those three use.
	//
	// THIS SKILL'S OWN TAGS, RATHER THAN A PLAIN ATTRIBUTE READ. Issue #943. The
	// attribute holds area of effect worked out with no skill in hand, so a
	// modifier naming a required tag is missing from it, and the Saboteur node
	// "+15% area of effect for traps per point" widened nothing at all. Passing
	// the tags is what makes it widen a trap and leave every other skill alone.
	return UCataclysmSkillEffects::AsMultiplierForSkill(
		GetAbilitySystemComponentFromActorInfo(),
		UCataclysmCombatAttributeSet::GetAreaOfEffectAttribute(),
		FName(UCataclysmItemModifiers::AreaOfEffectStat), SkillTags);
}

float UCataclysmSkillTemplate::ScaledRadiusCm() const
{
	// ANYTHING CARRYING AN AREA TAG, which is the project owner's rule of
	// 2026-08-24. `Type.AOE` is the parent of PointBlank, Aura and Persistent,
	// and a tag query against a parent matches every child, so this one check
	// covers all three.
	//
	// NOT UCataclysmSkillEffects::IsAreaDamage, WHICH ANSWERS A DIFFERENT
	// QUESTION. That one names PointBlank and Aura as "the two tags that make a
	// skill's hit area damage", and it leaves Persistent out on purpose: a
	// charge that leaves a fire trail can itself be evaded, so its BLOW is not
	// area damage even though the trail is. Whether a blow can be evaded and
	// whether a skill's size follows the character's area of effect are not the
	// same question, and scoping this by the narrower one left 26 of the 63
	// skills carrying an area tag out.
	//
	// A RADIUS THAT IS NOT AN AREA AT ALL IS STILL LEFT ALONE. A plain Strike's
	// radius is how far it reaches and a plain Projectile's is how wide the bolt
	// is; neither is an area of effect, and growing them is not what the affix
	// says it does.
	if (!SkillTags.HasTag(UCataclysmDamageCalculation::AreaDamageTag()))
	{
		return Params.RadiusCm;
	}

	return Params.RadiusCm * AreaOfEffectMultiplier();
}

float UCataclysmSkillTemplate::ScaledGroundRadiusCm() const
{
	return Params.GroundRadiusCm * AreaOfEffectMultiplier();
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

	return ACataclysmGroundZone::SpawnAlong(Self, Start, End, ScaledGroundRadiusCm(),
											Params.GroundDuration, PerTick);
}

float UCataclysmSkillTemplate::AddedHealthCostPercent(
	const UAbilitySystemComponent* AbilitySystem)
{
	using Resource = UCataclysmClassResourceAttributeSet;
	const FGameplayAttribute Added = Resource::GetAddedHealthCostAttribute();

	// AN ABILITY SYSTEM WITHOUT THE SET PAYS NOTHING EXTRA. Every player carries
	// the class resource set; an enemy's ability system does not, and an enemy
	// using a skill goes through this same function.
	if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(Added))
	{
		return 0.0f;
	}

	// The attribute is already floored at zero by the set's PreAttributeChange,
	// so this guards only against a value written before that ran.
	return FMath::Max(0.0f, AbilitySystem->GetNumericAttribute(Added));
}

float UCataclysmSkillTemplate::AddedHealthCostOfCurrentPercent(
	const UAbilitySystemComponent* AbilitySystem)
{
	using Resource = UCataclysmClassResourceAttributeSet;
	const FGameplayAttribute Added =
		Resource::GetAddedHealthCostOfCurrentAttribute();

	// THE SAME TWO GUARDS AS THE READER ABOVE, and for the same reasons: an
	// enemy's ability system carries no class resource set, and the attribute
	// is already floored at zero when it is written. Issue #986.
	if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(Added))
	{
		return 0.0f;
	}
	return FMath::Max(0.0f, AbilitySystem->GetNumericAttribute(Added));
}

void UCataclysmSkillTemplate::PayHealthCost()
{
	UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Avatar());
	if (!AbilitySystem)
	{
		return;
	}

	// THE CHECK ON THE SKILL'S OWN COST USED TO BE THE FIRST LINE OF THIS
	// FUNCTION AND CANNOT BE ANY MORE. Issue #970. A character with a point in
	// the Masochist's Deeper Cuts node pays health for EVERY skill, including
	// the ones that state no cost of their own -- which is every skill in the
	// game except Blood Pyre. Returning early on the skill's own figure would
	// have made that node do nothing at all.

	// A PERCENT OF CURRENT HEALTH, NOT OF MAXIMUM, because Blood Pyre says so:
	// "paying 8% of your current health". That is what makes it self-limiting --
	// each cast costs less than the last, so it cannot kill the caster.
	const float Current = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetHealthAttribute());
	const float OwnPercent = FMath::Max(0.0f, Params.HealthCostPercent);

	// AND THE CHARACTER'S OWN ADDED SHARE OF CURRENT HEALTH JOINS IT, which is
	// what the Masochist's Exsanguinate keystone grants: "Every skill costs an
	// additional 15% of your current health". Issue #986.
	//
	// SUMMED WITH THE SKILL'S OWN PERCENTAGE BEFORE EITHER IS TAKEN, rather
	// than charged one after the other. Two shares of current health applied in
	// turn would compound -- the second would be a share of what the first left
	// -- and the design says "an additional 15%", which is a sum.
	const float FromCurrentPercent =
		OwnPercent + AddedHealthCostOfCurrentPercent(AbilitySystem);

	// FLOORED SO IT LEAVES AT LEAST ONE HEALTH BEHIND. The design states it,
	// and it applies only to this half of the cost. See
	// `LeastHealthAfterCurrentHealthCost` for why it is here at all when the
	// arithmetic nearly guarantees it.
	//
	// NAMED FOR WHAT IT NOW HOLDS. It was `Own`, the skill's own cost, while
	// the skill was the only thing charging a share of current health.
	const float FromCurrent = FMath::Min(
		Current * FromCurrentPercent / 100.0f,
		FMath::Max(0.0f, Current - LeastHealthAfterCurrentHealthCost));

	// AND THE CHARACTER'S OWN ADDED COST, WHICH IS A PERCENT OF MAXIMUM HEALTH
	// AND SO CAN KILL. Issue #970. The two are measured against different things
	// deliberately: `docs/DECISIONS.md` records the project owner drawing that
	// exact distinction, that a share of current health "cannot kill on its own
	// ... it would kill if it were a share of maximum health". Deeper Cuts is
	// written as a share of maximum health.
	const float Maximum = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	const float Added = Maximum * AddedHealthCostPercent(AbilitySystem) / 100.0f;

	// ADDED, NOT COMPOUNDED. The node says "in addition to any other cost", so
	// the two are summed rather than one being applied to what the other left.
	const float Cost = FromCurrent + Added;

	// WHAT THIS SKILL JUST COST, AS A SHARE OF MAXIMUM HEALTH. Issue #983. Grand
	// Tithe asks "a skill whose health cost is above 10% of your maximum health",
	// and nothing anywhere recorded what a skill had cost.
	//
	// AGAINST MAXIMUM HEALTH WHATEVER EACH HALF WAS MEASURED AGAINST. The
	// skill's own cost is a share of CURRENT health and the character's added
	// cost is a share of MAXIMUM health; the node asks about maximum, so the
	// total is divided by the maximum here rather than at the far end where the
	// two halves can no longer be told apart.
	//
	// OUTSIDE THE BRANCH BELOW, DELIBERATELY, so a skill that cost nothing
	// records a real zero rather than keeping whatever the last cast recorded.
	// The ability is instanced per actor, so this value outlives the cast that
	// wrote it, and a stale one would hand a free skill the bonus a paid one
	// earned.
	//
	// NO MAXIMUM HEALTH LEAVES IT UNKNOWN. An attribute set that has not been
	// written yet reports zero, and dividing by it would be worse than saying
	// nothing is known.
	LastHealthCostPercentOfMaximum =
		Maximum > 0.0f ? Cost / Maximum * 100.0f : -1.0f;

	// AND PART OF IT MAY NOT BE TAKEN YET. Issue #991. The Masochist's
	// Deferred Payment node reads "10% per point of the health a skill costs
	// is not taken when the skill is used. It is taken 3 seconds later."
	//
	// A SHARE OF THE WHOLE COST, not of one half of it. The node says "the
	// health a skill costs", which is the total the character was about to
	// pay, whichever pool each part of it was measured against.
	//
	// ZERO FOR A CHARACTER WITHOUT THE NODE, so `Immediate` is the whole cost
	// and nothing below this line behaves differently for anybody else.
	const float Deferred = UCataclysmHealthDebt::AmountDeferred(
		Cost, UCataclysmHealthDebt::DeferredSharePercent(AbilitySystem));
	const float Immediate = Cost - Deferred;

	if (Cost > 0.0f)
	{
		// THE BRANCH IS ON THE WHOLE COST AND THE WRITE IS ON WHAT IS TAKEN
		// NOW, and the two differ once any of it is deferred. A character
		// deferring the whole cost still generates Fervour and still opens the
		// window below, because it has still committed to the cost.
		if (Immediate > 0.0f)
		{
			AbilitySystem->ApplyModToAttribute(
				UCataclysmVitalAttributeSet::GetHealthAttribute(),
				EGameplayModOp::Additive, -Immediate);
		}

		// AND PAYING WHILE SOMETHING IS ALREADY OWED PUSHES THAT DEBT OUT.
		// Issue #995. Rolling Debt: "Paying a health cost while one is still
		// owed extends the delay on what is owed by 0.5 seconds per point."
		//
		// BEFORE THE LINE BELOW, WHICH IS THE WHOLE ORDERING QUESTION. `Defer`
		// adds this cast's own deferral to what is owed, so asking afterwards
		// whether anything was owed would answer yes for the first debt of a
		// fight and let it extend itself. Asked here, "still owed" means owed
		// before this payment, which is what the node says.
		//
		// NOTHING HAPPENS FOR A CHARACTER WITHOUT THE NODE, and nothing happens
		// with no debt outstanding, which is every character in the game today
		// except a Masochist a few seconds into a fight.
		UCataclysmHealthDebt::ExtendForPaymentWhileOwing(AbilitySystem);

		// AND WHAT WAS NOT TAKEN IS OWED. Nothing happens for a character
		// without the node, whose deferred share is zero.
		UCataclysmHealthDebt::Defer(AbilitySystem, Deferred);

		// AND THE COST FILLS FERVOUR. Issue #954. The Masochist's starting node
		// states two ways in and this is the second: "1 per 1% of maximum health
		// spent as an ability cost". A character with no generator has a rate of
		// zero and this costs it nothing.
		//
		// HERE RATHER THAN WHERE HEALTH CHANGES, because a cost and a wound are
		// different things to this tree. Separate nodes increase each of the two
		// rates, and two keystones trade one against the other, so a hook that
		// only saw "health went down" could not tell them apart.
		//
		// A SHARE OF CURRENT HEALTH BUT MEASURED AGAINST MAXIMUM. The cost is
		// what the skill charges and the design writes Fervour generation as a
		// share of MAXIMUM health, so a character at low health pays less and
		// generates proportionally less. Both readings are consistent.
		UCataclysmFervour::GainFromHealthCost(AbilitySystem, Cost);

		// AND THE COST OPENS A WINDOW A PASSIVE NODE CAN READ. Issue #962. Blood
		// Rush grants "+2% increased damage per point for 2 seconds after you
		// pay a health cost", and nothing on the character remembered that
		// anything had happened at all.
		//
		// INSIDE THIS BRANCH, so a cost that came to nothing opens nothing. A
		// skill with no health cost returned at the top of this function; what
		// this guards is the remaining case, a character on so little health
		// that the percentage rounds away.
		//
		// THIS IS THE ONLY CALLER. A second place that charges health would have
		// to call it too, and the failure if it did not would be silent: the
		// node would simply never fire.
		if (UCataclysmAbilitySystemComponent* Cataclysm =
				Cast<UCataclysmAbilitySystemComponent>(AbilitySystem))
		{
			Cataclysm->NoteHealthCostPaid();
		}
	}
}
