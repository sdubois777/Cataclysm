// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSkillTemplate.h"
// For UCataclysmSelfBuffSkill, which is the one shape that reacts to a kill.
// A base including its own subclass is confined to this .cpp; the header
// knows nothing about it.
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmCastEffect.h"
// For the health cost a character adds to every skill. Issue #970.
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
// For marking a cursed creature so its curse passes on when it dies.
// The Wand's Anathema. Issue #37.
#include "AbilitySystem/CataclysmCurseSpread.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
// For turning a health cost into Fervour. Issue #954.
#include "AbilitySystem/CataclysmFervour.h"
// For the part of a cost that is taken later instead of now. Issue #991.
#include "AbilitySystem/CataclysmHealthDebt.h"
// For the stack a cost paid soon after the last one builds. Issue #1002.
#include "AbilitySystem/CataclysmStacks.h"
#include "AbilitySystem/CataclysmGroundZone.h"
// For binding together the creatures one throw pinned, so that killing any of
// them frees the rest. The Spear's Skewer. Issue #37.
#include "AbilitySystem/CataclysmPinnedLine.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
// For the persistent geometry a skill leaves: a pit, a wall, a fissure
// or a thicket. Five rows across the Spear and the Warhammer. Issue #37.
#include "AbilitySystem/CataclysmTerrain.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
#include "Character/CataclysmCharacterBase.h"
#include "Engine/World.h"
// For the wait between a swing starting and its blow landing. Issue #1133.
#include "TimerManager.h"
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
		// THE CHARACTER TURNS TO FACE WHAT IT IS HITTING. Decided by the project
		// owner on 2026-09-01, after seeing the first attack animations: a skill
		// aimed its damage and its effect at the cursor while the body kept
		// facing whatever direction it last walked in, so the character swung
		// away from the thing it was killing.
		//
		// EVERY SKILL, NOT JUST THE BASIC ATTACK, because the mismatch is the
		// same for all of them and this function is the one place all eight
		// shapes pass through.
		//
		// YAW ONLY. A character stands upright; pitching it at a cursor on the
		// floor would tip it over.
		//
		// A NO-OP FOR AN ENEMY, WHICH IS WHY IT IS SAFE HERE. AimDirection falls
		// back to the caster's own forward vector when there is no cursor -- and
		// no enemy has one -- so turning to face it changes nothing.
		// ACataclysmHellhoundCharacter uses a skill template and reaches this
		// line every time it casts.
		//
		// NOT A LOCK. `bOrientRotationToMovement` is on, so a character that is
		// walking is turned back toward its movement direction over the next few
		// frames. Standing still, the facing stays. That is the behaviour the
		// genre has: a glance toward the target rather than a snap that fights
		// the player's movement.
		const FVector Aim = AimDirection();
		if (!Aim.IsNearlyZero())
		{
			FRotator Facing = Aim.Rotation();
			Facing.Pitch = 0.0f;
			Facing.Roll = 0.0f;
			Self->SetActorRotation(Facing);
		}

		UCataclysmCastEffect::PlayFor(
			Self, AimDirection(),
			UCataclysmCastEffect::DamageTypeFor(Self, ElementTag()),
			ScaledRadiusCm());

		// AND THE CHARACTER MOVES. Issue #1126. Until this line the player used
		// every skill it had without moving at all: the effect flashed, damage
		// landed, and the body stood still through it.
		//
		// HERE FOR THE SAME REASON THE CAST EFFECT ABOVE IS. All eight skill
		// shapes call this function first, so this reaches every one of them and
		// the basic attack, and it sits past the commit so a skill refused by
		// its cost or its cooldown animates nothing.
		//
		// WHAT PLAYS IS THE CHARACTER'S BUSINESS, NOT THE SKILL'S. The clip has
		// to suit the skeleton, and the skeletons differ -- see
		// ACataclysmCharacterBase::PlayAttackAnimation. A character with no
		// override does nothing, which is what every enemy does: they animate
		// their own attacks from their own classes with clips from their own
		// packs.
		if (ACataclysmCharacterBase* Character =
				Cast<ACataclysmCharacterBase>(Self))
		{
			Character->PlayAttackAnimation();
		}
	}

	return true;
}

float UCataclysmSkillTemplate::SecondsUntilTheSwingConnects() const
{
	// THE CHARACTER IS THE ONLY THING THAT KNOWS. It chose which of its clips
	// came up in the cycle and what rate to play it at, both inside
	// PlayAttackAnimation, which CommitAndBegin called moments ago.
	//
	// ZERO FOR ANYTHING THAT IS NOT ONE OF OUR CHARACTERS, which is the same
	// answer as "no animation played" and means the blow lands now.
	const ACataclysmCharacterBase* Character =
		Cast<ACataclysmCharacterBase>(Avatar());
	return Character ? Character->SecondsUntilTheSwingConnects() : 0.0f;
}

void UCataclysmSkillTemplate::WhenTheSwingConnects(TFunction<void()> Blow)
{
	if (!Blow)
	{
		return;
	}

	UWorld* World = GetWorld();
	const float Delay = SecondsUntilTheSwingConnects();

	// STRIKE NOW WHEN THERE IS NOTHING TO WAIT FOR, AND THIS IS AN ORDINARY PATH
	// RATHER THAN AN EXCEPTION. Every enemy reaches it, because enemies animate
	// from their own classes and never set the figure. Every automation test
	// reaches it, because a world built by UWorld::CreateWorld is never ticked
	// and a timer set in one would never fire, so deferring there would silently
	// stop all damage in the suite. A checkout with no animation assets reaches
	// it too.
	//
	// NO WORLD MEANS THE SAME. A timer needs one, and having nowhere to put the
	// blow is not a reason to drop it.
	if (Delay <= 0.0f || !World)
	{
		Blow();
		return;
	}

	// A WEAK LAMBDA, NOT A PLAIN ONE. It binds to this ability object, so a
	// swing still travelling when the ability is destroyed is dropped rather
	// than run against freed memory.
	World->GetTimerManager().SetTimer(
		SwingTimer,
		FTimerDelegate::CreateWeakLambda(this, [Blow]() { Blow(); }),
		Delay, /*bLoop=*/false);
}

void UCataclysmSkillTemplate::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	// A CANCELLED SKILL STRIKES NOTHING. That is one of the four things issue
	// #1133 requires to still hold once damage moves later in time. Before that
	// issue a refused skill could not deal damage because the refusal and the
	// damage were in the same frame; with a wait in between, an interrupted
	// skill would otherwise still land its blow.
	//
	// ONLY ON CANCELLATION. An ordinary end is called from inside the blow, once
	// it has already landed, so clearing there would be clearing a timer that
	// has already fired.
	if (bWasCancelled && SwingTimer.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(SwingTimer);
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
					  bWasCancelled);
}

bool UCataclysmSkillTemplate::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags,
								   OptionalRelevantTags))
	{
		return false;
	}

	// A CHARACTER STANDING IN A PIT CANNOT LEAP OUT OF IT. The Warhammer's
	// Crater: "anything inside has to climb out: enemies in the pit cannot charge
	// or leap." An enemy's charge is refused in
	// `ACataclysmEnemyCharacter::BeginCharge`, which is C++ on the creature
	// rather than a skill template; this is the other half, and it covers the
	// player as well as any creature that ever gets a Movement skill.
	//
	// HERE RATHER THAN IN `ActivateAbility`, so a refused leap costs no mana and
	// starts no cooldown. By the time `ActivateAbility` runs, `CommitAndBegin`
	// has already spent both.
	//
	// EVERY MOVEMENT MODE, NOT ONLY `Leap`. The row says a creature cannot leave
	// by leaping, and a blink, a charge and a flicker all leave the same way. A
	// pit that stopped one arc and let a blink through would be a hole with a
	// door in it.
	if (Shape() == ECataclysmSkillShape::Movement
		&& ACataclysmTerrain::IsStandingIn(ActorInfo ? ActorInfo->AvatarActor.Get()
													: Avatar(),
										   ECataclysmTerrainKind::Pit))
	{
		return false;
	}

	// THE BASE'S ANSWER FIRST, so a skill that cannot be paid for is refused for
	// that reason rather than for a condition it also happens to fail. The order
	// decides which of the two the player is told about.
	//
	// THE ACTOR INFORMATION THE ENGINE HANDED IN, and not this instance's own.
	// This runs before the ability is active, and `GetCurrentActorInfo` is not
	// guaranteed to be set then.
	return RequirementsAreMet(ActorInfo);
}

// ==========================================================================
// Conditions on activation -- the `Requires` column
// ==========================================================================

bool UCataclysmSkillTemplate::RequiresCondition(const TCHAR* Condition) const
{
	if (Params.Requires.IsEmpty())
	{
		return false;
	}

	// COMMA SEPARATED AND MORE THAN ONE MAY BE NAMED, which the parameter's own
	// header says. No designed row names two today; the Spear's Nail Down is the
	// nearest, at one.
	TArray<FString> Named;
	Params.Requires.ParseIntoArray(Named, TEXT(","), /*InCullEmpty=*/true);
	for (const FString& One : Named)
	{
		if (One.TrimStartAndEnd().Equals(Condition, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

float UCataclysmSkillTemplate::RequirementReachCm() const
{
	// A SKILL THAT REACHES OUT STATES A RANGE AND ONE THAT HITS AROUND ITSELF
	// STATES A RADIUS, and the condition has to be judged over whichever of the
	// two the skill actually covers. Flashpoint darts fourteen metres and states
	// `Range=14`; Touch Off rings eight metres around the caster and states
	// `Radius=8`. Asking about the wrong one would let Touch Off be used on
	// something it cannot touch.
	//
	// THE SCALED RADIUS, not the written one, so a character with area of effect
	// may activate on an enemy its widened ring will actually reach.
	return Params.RangeCm > 0.0f ? Params.RangeCm : ScaledRadiusCm();
}

bool UCataclysmSkillTemplate::RequirementsAreMet(
	const FGameplayAbilityActorInfo* ActorInfo) const
{
	if (Params.Requires.IsEmpty())
	{
		return true;
	}

	const AActor* Self = ActorInfo ? ActorInfo->AvatarActor.Get() : Avatar();
	if (!Self)
	{
		// No avatar, so nothing can be judged. Refusing here would make every
		// conditional skill unusable in a state the ability system reaches
		// briefly during setup; allowing it leaves the decision to activation.
		return true;
	}

	// `RearHit` IS DELIBERATELY ABSENT FROM THIS FUNCTION. See the header: it is
	// a condition on what a running buff reacts to, not on whether the buff may
	// be cast, so it gates nothing here.
	if (RequiresCondition(TEXT("Stationary")))
	{
		// THE GREATSWORD'S UNBROKEN, "the whole bonus is lost the instant you
		// take a step". Only the casting half is judged here; losing the bonus
		// on moving belongs with the hold-and-release work in issue #1141.
		if (!Self->GetVelocity().IsNearlyZero())
		{
			return false;
		}
	}

	const bool bNeedsTarget = RequiresCondition(TEXT("Target"));
	const bool bNeedsBurning = RequiresCondition(TEXT("Burning"));
	if (!bNeedsTarget && !bNeedsBurning)
	{
		return true;
	}

	const float ReachCm = RequirementReachCm();
	if (ReachCm <= 0.0f)
	{
		// A skill demanding a target and stating no reach to find one in. The
		// generator cannot catch this, because the two columns are independent.
		UE_LOG(LogCataclysm, Warning,
			TEXT("'%s' requires '%s' and states neither a range nor a radius to "
				 "look for one in, so it can never be used."),
			*SkillName, *Params.Requires);
		return false;
	}

	// THE AVATAR'S WORLD AND NOT THE ABILITY'S. This runs before the ability is
	// active, and an ability that has not been activated has no world of its
	// own, so asking it would find nobody and refuse every conditional skill.
	const TArray<AActor*> Nearby = UCataclysmTargeting::FindEnemiesInSphere(
		Self->GetWorld(), Self, Self->GetActorLocation(), ReachCm);
	if (Nearby.IsEmpty())
	{
		return false;
	}

	if (!bNeedsBurning)
	{
		return true;
	}

	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	if (!Burn.IsValid())
	{
		// The tag vocabulary has no burn, which means the generated tag list is
		// older than the design. Refusing every consuming skill for that would
		// be a worse failure than allowing them, and the burn itself would
		// already be broken everywhere else.
		return true;
	}

	for (AActor* One : Nearby)
	{
		if (UCataclysmSkillEffects::HasTag(One, Burn))
		{
			return true;
		}
	}
	return false;
}

// ==========================================================================
// A kill this character made
// ==========================================================================

int32 UCataclysmSkillTemplate::NoteKill(AActor* Killer)
{
	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Killer);
	if (!AbilitySystem)
	{
		return 0;
	}

	// ASKED OF THE RUNNING ABILITIES RATHER THAN HELD AS STATE, which is the
	// route `HeldConsumeSpreadRadiusCm` already takes for the Sword's Ashen
	// Edge. A buff that lasts IS an active ability for as long as it lasts, so
	// there is nothing to register and nothing to clear when it ends, is
	// cancelled, or its owner dies.
	int32 Told = 0;
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		if (!Spec.IsActive())
		{
			continue;
		}

		if (UCataclysmSelfBuffSkill* Buff =
				Cast<UCataclysmSelfBuffSkill>(Spec.GetPrimaryInstance()))
		{
			Buff->NoteKill();
			++Told;
		}
	}

	return Told;
}

int32 UCataclysmSkillTemplate::NoteBlowLanded(AActor* Attacker,
											  const FVector& Where)
{
	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Attacker);
	if (!AbilitySystem)
	{
		return 0;
	}

	// THE SAME SHAPE AS `NoteKill` DIRECTLY ABOVE, and for the same reason: a
	// buff that lasts IS an active ability for as long as it lasts, so asking the
	// running abilities beats registering and unregistering something.
	//
	// A SECOND FUNCTION RATHER THAN A FLAG ON THE FIRST. A kill and a landed blow
	// are different events -- one skill counts kills to grow its bonus and
	// another cracks the ground under every blow -- and a shared entry point
	// taking "which kind" would put the two one argument apart.
	int32 Told = 0;
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		if (!Spec.IsActive())
		{
			continue;
		}

		if (UCataclysmSelfBuffSkill* Buff =
				Cast<UCataclysmSelfBuffSkill>(Spec.GetPrimaryInstance()))
		{
			Buff->NoteBlowLanded(Where);
			++Told;
		}
	}

	return Told;
}

// ==========================================================================
// Returning a cooldown
// ==========================================================================

bool UCataclysmSkillTemplate::AtNoHealth(const AActor* Actor)
{
	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Actor);
	if (!AbilitySystem)
	{
		return false;
	}

	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Health))
	{
		return false;
	}

	return AbilitySystem->GetNumericAttribute(Health) <= 0.0f;
}

bool UCataclysmSkillTemplate::RefundCooldown()
{
	if (Params.RefundsCooldown.IsEmpty())
	{
		return false;
	}

	// WHICH SLOT'S COOLDOWN. `Self` is this skill's own, whichever slot it was
	// granted into; `Movement` is that slot's, whatever skill happens to be in
	// it. Those are the only two values `REFUND_TARGETS` in
	// `tools/generate_datatables.py` allows.
	ECataclysmAbilitySlot Which = Slot;
	if (Params.RefundsCooldown.Equals(TEXT("Movement"), ESearchCase::IgnoreCase))
	{
		Which = ECataclysmAbilitySlot::Movement;
	}
	else if (!Params.RefundsCooldown.Equals(TEXT("Self"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("'%s' says it refunds the '%s' cooldown, which is neither Self "
				 "nor Movement, so nothing was returned."),
			*SkillName, *Params.RefundsCooldown);
		return false;
	}

	const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(Which);
	if (!Tag.IsValid())
	{
		return false;
	}

	// TAKEN OFF BY TAG, WHICH IS HOW IT WENT ON.
	// `UCataclysmGameplayAbility::ApplyCooldown` builds a duration effect that
	// grants the slot's tag and nothing else, so removing every effect granting
	// that tag is exactly undoing it.
	const int32 Removed =
		UCataclysmSkillEffects::RemoveEffectsGranting(Avatar(), Tag);
	if (Removed > 0)
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("'%s' returned the %s cooldown."), *SkillName, *Tag.ToString());
	}
	return Removed > 0;
}

// ==========================================================================
// Applied status effects -- the Wand's verb
// ==========================================================================

TArray<FGameplayTag> UCataclysmSkillTemplate::NamedEffectTags() const
{
	TArray<FGameplayTag> Tags;
	if (Params.Effect.IsEmpty())
	{
		return Tags;
	}

	// COMMA SEPARATED, because Anathema writes `Effect=Shred, Madness` --
	// "laying every curse you know on it". Read as one name, that row named an
	// effect called "Shred, Madness" which no sheet has, and granted nothing.
	const FGameplayTag Stunned = UCataclysmSkillEffects::StunnedTag();

	TArray<FString> Named;
	Params.Effect.ParseIntoArray(Named, TEXT(","), /*InCullEmpty=*/true);
	for (const FString& One : Named)
	{
		const FGameplayTag Tag =
			UCataclysmSkillShapes::StatusTagFor(One.TrimStartAndEnd());
		if (!Tag.IsValid())
		{
			continue;
		}

		// A STUN IS LEFT OUT, and letting one through would be a fault rather
		// than a gap. `UCataclysmSkillEffects::ApplyStun` carries three rules the
		// design states -- a damage threshold, five seconds of immunity after one
		// lands, and bosses immune outright -- and granting `Status.Stunned` as a
		// plain tag walks past all three. Four skills write `Effect=Stun` beside
		// a `StunSeconds`, and that number is read by nothing, so those four do
		// not stun today.
		if (Stunned.IsValid() && Tag == Stunned)
		{
			continue;
		}

		// AND SO IS ANYTHING THAT IS DAMAGE OVER TIME. `ApplyDamageOverTime` is
		// the path for burn and bleed and works out a per-tick amount from the
		// hit that caused it; granting the tag instead would leave a target
		// bleeding for nothing. `bUsable` is the sheet's own answer to "is this
		// row damage over time": true only for a row carrying both a duration
		// and a per-tick amount. The Crossbow's Bolt Turret writes `Effect=Bleed`.
		if (UCataclysmSkillEffects::NumbersForEffectTag(Tag).bUsable)
		{
			continue;
		}

		Tags.AddUnique(Tag);
	}
	return Tags;
}

float UCataclysmSkillTemplate::AppliedEffectSeconds(
	const FGameplayTag& EffectTag) const
{
	if (Params.EffectDuration > 0.0f)
	{
		return Params.EffectDuration;
	}

	// THE SHEET'S OWN FIGURE AND NOT A NUMBER WRITTEN HERE. Foul Wake states
	// `Effect=Shred` and no duration at all; the Status Effects sheet gives
	// Shred six seconds, which is exactly what that row's sentence says.
	return UCataclysmSkillEffects::NumbersForEffectTag(EffectTag).DurationSeconds;
}

FName UCataclysmSkillTemplate::DamageTypeName() const
{
	return UCataclysmDamageCalculation::DamageTypeFromTags(SkillTags);
}

int32 UCataclysmSkillTemplate::ApplyNamedEffectsTo(AActor* Target,
												  float DurationScale)
{
	if (!IsValid(Target) || DurationScale <= 0.0f)
	{
		return 0;
	}

	AActor* Self = Avatar();
	if (!Self)
	{
		return 0;
	}

	// STUNS AND DAMAGE OVER TIME ARE ALREADY GONE. `NamedEffectTags` drops both,
	// so every caller of it gets the same rule rather than each remembering.
	const FName Type = DamageTypeName();

	int32 Applied = 0;
	for (const FGameplayTag& EffectTag : NamedEffectTags())
	{
		const float Seconds = AppliedEffectSeconds(EffectTag) * DurationScale;
		if (Seconds <= 0.0f)
		{
			// A skill naming an effect that neither it nor the sheet gives a
			// duration. The Staff's Quarry is why this is said out loud rather
			// than passed over: its designed duration is zero and its row
			// carries `EffectDuration=12`, so a row losing that one cell would
			// apply nothing and report nothing.
			UE_LOG(LogCataclysm, Warning,
				TEXT("'%s' names the effect %s, and neither the skill nor the "
					 "Status Effects sheet says how long it lasts, so it "
					 "applies nothing."),
				*SkillName, *EffectTag.ToString());
			continue;
		}

		if (UCataclysmSkillEffects::ApplyNamedEffect(
				Self, Target, EffectTag, Seconds, Params.EffectMagnitude, Type))
		{
			++Applied;
		}
	}

	// AND THE CURSE PASSES ON WHEN ITS HOLDER DIES, IF THE ROW SAYS SO. The
	// Wand's Anathema: "anything that dies while damned passes the curse to the
	// nearest living enemy", written as `OnDeath=SpreadDebuff; OnDeathRange=8`.
	//
	// MARKED ONLY WHEN SOMETHING WAS ACTUALLY APPLIED, so a creature that took
	// no curse is not left carrying an instruction with nothing to pass on.
	//
	// `Leap` AND `Release` ARE THE OTHER TWO VALUES `OnDeath` MAY TAKE, and
	// neither is built. The Axe's Harrower leaps a buried axe onward and the
	// Spear's Skewer releases what it pinned; both need a thing to move rather
	// than a tag to copy, which is why they are not handled here.
	if (Applied > 0 && Params.OnDeathRangeCm > 0.0f
		&& Params.OnDeath.Equals(TEXT("SpreadDebuff"), ESearchCase::IgnoreCase))
	{
		UCataclysmCurseSpread::MarkOn(Target, Self, Params.OnDeathRangeCm);
	}

	return Applied;
}

// ==========================================================================
// Consuming burn -- the Sword's verb
// ==========================================================================

TArray<AActor*> UCataclysmSkillTemplate::ConsumeBurnFrom(
	const TArray<AActor*>& Targets)
{
	TArray<AActor*> Consumed;
	if (!Params.bConsumeBurn || Targets.IsEmpty())
	{
		return Consumed;
	}

	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	if (!Burn.IsValid())
	{
		return Consumed;
	}

	for (AActor* Target : Targets)
	{
		if (!UCataclysmSkillEffects::HasTag(Target, Burn))
		{
			continue;
		}

		// REMOVED BY TAG, WHICH TAKES THE WHOLE BURN AND NOT A SHARE OF IT.
		// `RemoveEffectsGranting` says in its own header that two casters
		// granting one tag share one effect, so consuming it takes it from both.
		// That is the right answer for this: the design's word is "consumed",
		// and a fire that is partly put out is not a mechanic anything states.
		if (UCataclysmSkillEffects::RemoveEffectsGranting(Target, Burn) > 0)
		{
			Consumed.Add(Target);
		}
	}

	if (!Consumed.IsEmpty())
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("'%s' consumed the burn from %d of %d targets."),
			*SkillName, Consumed.Num(), Targets.Num());
	}

	return Consumed;
}

int32 UCataclysmSkillTemplate::IgniteAroundConsumed(
	const TArray<AActor*>& Consumed)
{
	if (Consumed.IsEmpty())
	{
		return 0;
	}

	AActor* Self = Avatar();
	if (!Self)
	{
		return 0;
	}

	// THE SKILL'S OWN SPREAD PLUS WHATEVER A HELD BUFF ADDS. Touch Off states
	// three metres of its own; Ashen Edge grants four while it runs and consumes
	// nothing itself. A skill with neither spreads nothing, which is Quench,
	// Extinction and Flashpoint with no buff up.
	const float RadiusCm =
		(Params.ConsumeRadiusCm + HeldConsumeSpreadRadiusCm(Self))
		* AreaOfEffectMultiplier();
	if (RadiusCm <= 0.0f)
	{
		return 0;
	}

	// WHAT THE SPREAD BURN IS WORTH. A burn is a share of the hit that caused
	// it, so a spread with no hit behind it needs a figure of its own: this uses
	// the skill's own blow, which is what the fire being spent was worth.
	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Self);
	const float HitDamage = UCataclysmSkillEffects::ModifiedDamage(
		AbilitySystem,
		UCataclysmSkillEffects::WeaponDamageOf(AbilitySystem)
			* GetDamagePercent() / 100.0f,
		SkillTags);
	if (HitDamage <= 0.0f)
	{
		return 0;
	}

	int32 Lit = 0;
	for (AActor* One : Consumed)
	{
		if (!IsValid(One))
		{
			continue;
		}

		for (AActor* Caught : UCataclysmTargeting::FindEnemiesInSphere(
				GetWorld(), Self, One->GetActorLocation(), RadiusCm))
		{
			// NOT THE ENEMY WHOSE FIRE THIS WAS. Relighting it from its own
			// spread would make consuming it free, and every consuming skill
			// would leave the field exactly as it found it.
			if (Consumed.Contains(Caught))
			{
				continue;
			}

			if (UCataclysmSkillEffects::ApplyBurn(Self, Caught, HitDamage))
			{
				++Lit;
			}
		}
	}

	if (Lit > 0)
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("'%s' spread fire from %d consumed enemies to %d others, "
				 "within %.0fcm."),
			*SkillName, Consumed.Num(), Lit, RadiusCm);
	}

	return Lit;
}

float UCataclysmSkillTemplate::HeldConsumeSpreadRadiusCm(const AActor* Self)
{
	// ASKED OF THE CASTER'S RUNNING ABILITIES RATHER THAN HELD AS STATE. Ashen
	// Edge is a self buff that lasts ten seconds, and while it lasts it IS an
	// active ability on the caster, so the radius it grants can be read from it
	// directly. Recording it somewhere else would be a second copy that has to
	// be cleared when the buff ends, cancels, or its owner dies.
	//
	// THE LARGEST RATHER THAN THE SUM, because two copies of one buff are one
	// buff: the design gives every player-applied effect a single stack, and a
	// second application refreshes rather than adds.
	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Self);
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	float Largest = 0.0f;
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		if (!Spec.IsActive())
		{
			continue;
		}

		const UCataclysmSkillTemplate* Running =
			Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());

		// A SELF BUFF AND NOT ANY RUNNING SKILL. A skill that consumes states a
		// ConsumeRadius of its own for its own spread, and reading it here as
		// well would double it for the skill that is running.
		if (Running && Running->Shape() == ECataclysmSkillShape::SelfBuff)
		{
			Largest = FMath::Max(Largest, Running->Params.ConsumeRadiusCm);
		}
	}
	return Largest;
}

// ==========================================================================
// A skill's own damage scaling
// ==========================================================================

float UCataclysmSkillTemplate::ScalingUnits(int32 ConsumedCount,
										   bool bThisTargetConsumed) const
{
	if (Params.ScalingSource.Equals(TEXT("HealthMissing"), ESearchCase::IgnoreCase))
	{
		const UAbilitySystemComponent* AbilitySystem =
			UCataclysmTargeting::AbilitySystemOf(Avatar());
		if (!AbilitySystem)
		{
			return 0.0f;
		}

		using Vitals = UCataclysmVitalAttributeSet;
		const float Maximum =
			AbilitySystem->GetNumericAttribute(Vitals::GetMaxHealthAttribute());
		if (Maximum <= 0.0f)
		{
			return 0.0f;
		}

		const float Current =
			AbilitySystem->GetNumericAttribute(Vitals::GetHealthAttribute());

		// PERCENTAGE POINTS MISSING, because the row is written per point:
		// "1% increased damage for every 1% of your maximum health you are
		// currently missing". A character at half health is 50 units, not 0.5.
		return FMath::Clamp((Maximum - Current) / Maximum * 100.0f, 0.0f, 100.0f);
	}

	if (Params.ScalingSource.Equals(TEXT("Consumed"), ESearchCase::IgnoreCase))
	{
		// EVERY OTHER ENEMY, so one fire put out is worth nothing. Extinction:
		// "rising by 15% for every OTHER enemy consumed in the same instant".
		return static_cast<float>(FMath::Max(0, ConsumedCount - 1));
	}

	if (Params.ScalingSource.Equals(TEXT("Consume"), ESearchCase::IgnoreCase))
	{
		// ONE OR NOTHING, ASKED OF THE ENEMY IN FRONT. Quench: "any enemy
		// already alight has their fire consumed and takes 50% more damage for
		// it", so an enemy that was never alight takes the plain blow.
		return bThisTargetConsumed ? 1.0f : 0.0f;
	}

	// One of the eight sources nothing counts yet -- Kill, Second, Meter,
	// HitTaken, Bounce, Pierced, Pinned -- or Burning, which the self buff
	// counts for itself. Scaling by nothing is the safe answer: a skill naming
	// one deals its plain damage rather than a figure taken from the wrong
	// thing.
	return 0.0f;
}

float UCataclysmSkillTemplate::ScaledDamagePercent(float Units) const
{
	float Percent = GetDamagePercent();

	if (Units > 0.0f)
	{
		// THE TWO BUCKETS, APPLIED THE WAY THE DESIGN DEFINES THEM. Increases
		// are summed and applied once; a More multiplies on its own. A skill
		// stating both is therefore not the same as one stating their total,
		// which is why `docs/DECISIONS.md` puts the bucket in the parameter's
		// name rather than in a column beside it.
		Percent *= 1.0f + Params.IncreasedDamagePer * Units / 100.0f;
		Percent *= 1.0f + Params.MoreDamagePer * Units / 100.0f;
	}

	if (Params.MaxDamagePercent > 0.0f)
	{
		Percent = FMath::Min(Percent, Params.MaxDamagePercent);
	}

	return Percent;
}

float UCataclysmSkillTemplate::HitScaled(const TArray<AActor*>& Targets,
										const TArray<AActor*>& Consumed)
{
	if (Targets.IsEmpty())
	{
		return 0.0f;
	}

	// NOTHING TO SCALE BY, so this is the ordinary blow. Every skill that states
	// no ScalingSource takes this path, which is 50 of the 56 Demonic rows.
	if (Params.ScalingSource.IsEmpty()
		|| (Params.IncreasedDamagePer <= 0.0f && Params.MoreDamagePer <= 0.0f))
	{
		return HitTargets(Targets, ScaledDamagePercent(0.0f));
	}

	// PER TARGET OR PER USE, and only `Consume` is per target. Splitting the
	// group for a source that is not would deal two identical blows in two
	// calls, which changes nothing but costs a second pass over the targets.
	if (!Params.ScalingSource.Equals(TEXT("Consume"), ESearchCase::IgnoreCase))
	{
		return HitTargets(Targets,
						  ScaledDamagePercent(ScalingUnits(Consumed.Num(), false)));
	}

	TArray<AActor*> Burned;
	TArray<AActor*> Untouched;
	for (AActor* Target : Targets)
	{
		(Consumed.Contains(Target) ? Burned : Untouched).Add(Target);
	}

	float Total = 0.0f;
	if (!Burned.IsEmpty())
	{
		Total += HitTargets(
			Burned, ScaledDamagePercent(ScalingUnits(Consumed.Num(), true)));
	}
	if (!Untouched.IsEmpty())
	{
		Total += HitTargets(
			Untouched, ScaledDamagePercent(ScalingUnits(Consumed.Num(), false)));
	}
	return Total;
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

	// EVERYTHING THIS USE PINNED, KEPT SO THAT `OnDeath=Release` CAN BIND IT
	// AFTERWARDS. The Spear's Skewer: "the whole line is held together for 4
	// seconds. Killing any one of them frees the rest." The line is not known
	// until every target has been dealt with, so it cannot be bound inside the
	// loop. Empty for every skill that does not pin, which is all but four.
	TArray<AActor*> Pinned;

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

		// AND ANY RUNNING BUFF THAT REACTS TO A LANDED BLOW IS TOLD, AND WHERE.
		// The Warhammer's Groundbreaker: "for 10 seconds every blow you land
		// cracks the ground beneath what it hits". Here rather than in the Strike
		// template, because "every blow you land" includes a projectile, an aura
		// pulse and a leap, and this is the one place all of them pass through.
		//
		// ONLY WHEN SOMETHING WAS ACTUALLY DEALT, which is the same test the burn
		// above makes and the same one `ApplyManaOnHit` makes below. A swing that
		// was evaded, or that armour stopped completely, did not land -- so it
		// cracks nothing.
		//
		// IT IS ASKED OF THE CASTER'S OWN ABILITIES, so a skill notifies the buff
		// running beside it rather than itself. Groundbreaker deals no damage of
		// its own: the Support slot's damage percent is zero, so it never reaches
		// this line through its own casting and only ever hears about other
		// skills' blows.
		if (Dealt > 0.0f)
		{
			NoteBlowLanded(Self, Target->GetActorLocation());
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

		// AND SO IS FORCED MOVEMENT, FOR THE SAME REASON KNOCKBACK IS. Nine rows
		// across the Spear, the Warhammer and the Whip state one of the five
		// verbs, spread over three different shapes, so putting it in any one
		// template would mean putting it in three.
		//
		// AFTER THE KNOCKBACK RATHER THAN BEFORE IT, which matters only for a
		// row that states both. None does today: `Knockback` and `ForcedMovement`
		// are separate columns and no designed row fills them together. If one
		// ever did, the shove would happen first and then the haul, which is the
		// order the two columns are written in.
		//
		// NOT SCALED BY THE DAMAGE DEALT, like the knockback and unlike the
		// burn. The damage is handed over only because an undesigned knockdown
		// would weigh it against the target's maximum health, and every row here
		// states a designed one.
		if (ApplyForcedMovementTo(Self, Target, Dealt))
		{
			Pinned.Add(Target);
		}
	}

	// A LINE IS BOUND ONLY WHEN THE ROW ASKS FOR ONE. `OnDeath` may take three
	// values and this is the third: `Leap` buries a weapon, `SpreadDebuff`
	// passes a curse, and `Release` frees the survivors of a pinned line.
	//
	// FROM WHAT WAS ACTUALLY PINNED AND NOT FROM WHAT WAS HIT. A boss is not
	// pinned once issue #1149 is settled that way, and a target that was already
	// pinned by something else has its pin refreshed rather than joining this
	// line. Binding the hit list instead would put creatures in a line that were
	// never held in it.
	if (Params.OnDeath.Equals(TEXT("Release"), ESearchCase::IgnoreCase))
	{
		UCataclysmPinnedLine::BindTogether(Pinned);
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

bool UCataclysmSkillTemplate::ForcedMovementNames(const TCHAR* Verb) const
{
	if (Params.ForcedMovement.IsEmpty())
	{
		return false;
	}

	TArray<FString> Named;
	Params.ForcedMovement.ParseIntoArray(Named, TEXT(","), /*InCullEmpty=*/true);
	for (const FString& One : Named)
	{
		if (One.TrimStartAndEnd().Equals(Verb, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

bool UCataclysmSkillTemplate::ApplyForcedMovementTo(AActor* Self, AActor* Target,
													float DamageDealt) const
{
	if (Params.ForcedMovement.IsEmpty() || !IsValid(Self) || !IsValid(Target))
	{
		return false;
	}

	// THE DISPLACEMENTS FIRST. A target that is going to be both moved and held
	// must be moved before it is held, or the hold applies where the target is
	// about to stop being. The Whip's The Gathering is the row that does both:
	// "haul every enemy you catch into a burning heap at your feet ... and they
	// cannot rise for 2 seconds."
	if (ForcedMovementNames(TEXT("Pull")) || ForcedMovementNames(TEXT("Drag")))
	{
		UCataclysmSkillEffects::ApplyPull(Self, Target,
										  Params.ForcedMovementDistanceCm);
	}

	if (ForcedMovementNames(TEXT("Launch")))
	{
		UCataclysmSkillEffects::ApplyLaunch(Self, Target,
											Params.ForcedMovementDistanceCm);
	}

	// THEN THE HOLDS. A knockdown is checked against all three anti-stun-lock
	// rules inside `ApplyKnockdown` and a pin against none of them; the reasoning
	// for that difference is on `UCataclysmSkillEffects::ApplyPin` and the
	// question is open as issue #1149.
	if (ForcedMovementNames(TEXT("Knockdown")))
	{
		UCataclysmSkillEffects::ApplyKnockdown(
			Self, Target, Params.ForcedMovementDuration, DamageDealt,
			/*bKnockdownIsDesigned=*/true);
	}

	if (!ForcedMovementNames(TEXT("Pin")))
	{
		return false;
	}

	// THE MAGNITUDE ON A PINNING ROW IS WHAT THE TARGET TAKES WHILE HELD, and
	// only the Spear's Impale states one: "while a target is pinned it takes 30%
	// more damage from every source". `EffectMagnitude` is the column that
	// carries the size of an applied effect, and a pin is one, so no new
	// parameter was needed. A pinning row that states no magnitude simply holds.
	return UCataclysmSkillEffects::ApplyPin(Self, Target,
											Params.ForcedMovementDuration,
											Params.EffectMagnitude);
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

ACataclysmTerrain* UCataclysmSkillTemplate::LeaveTerrainAlong(
	const FVector& Start, const FVector& End)
{
	const ECataclysmTerrainKind Kind =
		ACataclysmTerrain::KindFromCell(Params.Terrain);
	if (Kind == ECataclysmTerrainKind::None)
	{
		// The ordinary case: 398 of the 403 rows in the sheet leave no terrain.
		return nullptr;
	}

	AActor* Self = Avatar();
	if (!Self)
	{
		return nullptr;
	}

	if (Params.TerrainSizeCm <= 0.0f || Params.TerrainDuration <= 0.0f)
	{
		// A kind with no size or no duration would be terrain occupying nothing
		// or vanishing at once, which reads as working and is not. All five rows
		// state both, so reaching here means the imported table is older than the
		// sheet.
		UE_LOG(LogCataclysm, Warning,
			TEXT("'%s' states Terrain='%s' and no size or no duration, so none "
				 "was left. Run tools/generate_datatable_assets.py."),
			*SkillName, *Params.Terrain);
		return nullptr;
	}

	// WIDENED BY THE CHARACTER'S AREA OF EFFECT, the same as the burning ground
	// and the strike itself. Terrain is an area the skill covers, and a passive
	// node saying "+15% area of effect" has no reason to widen a patch of fire
	// and leave a thicket of spears the size the sheet wrote.
	//
	// A WALL IS NOT WIDENED BY IT, because its size is a length rather than a
	// radius and its two ends are handed in already. `ACataclysmTerrain::Spawn`
	// reads the length off those ends and ignores the size for a wall.
	const float SizeCm = Params.TerrainSizeCm * AreaOfEffectMultiplier();

	// THE HOLD COMES FROM `ForcedMovementDuration`, WHICH IS WHY THICKET PINS FOR
	// THE SAME SIX SECONDS EITHER WAY. Its row states "pinning everything caught
	// for 6 seconds" and "anything that walks into them is pinned as well", and
	// reading one number for both is what stops those two drifting apart.
	ACataclysmTerrain* Terrain = ACataclysmTerrain::Spawn(
		Self, Kind, Start, End, SizeCm, Params.TerrainDuration,
		Params.ForcedMovementDuration);

	if (Terrain)
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("'%s' left %s terrain for %.1fs."),
			*SkillName, *Params.Terrain, Params.TerrainDuration);
	}

	return Terrain;
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

	ACataclysmGroundZone* Zone = ACataclysmGroundZone::SpawnAlong(
		Self, Start, End, ScaledGroundRadiusCm(), Params.GroundDuration, PerTick);

	// AND THE GROUND CARRIES THE SKILL'S CURSE, IF IT NAMES ONE. The Wand's
	// Foul Wake: "the ground you fled burns for 6 seconds and strips the Demonic
	// resistance of anything that walks into it". Set after spawning rather than
	// passed in, because `SpawnAlong` already takes seven arguments and only one
	// zone in the game wants these.
	//
	// THE FIRST NAMED EFFECT ONLY. No row leaves ground carrying two, and a zone
	// holds one.
	if (Zone)
	{
		const TArray<FGameplayTag> Named = NamedEffectTags();
		if (!Named.IsEmpty())
		{
			Zone->AlsoApply(Named[0], AppliedEffectSeconds(Named[0]),
							Params.EffectMagnitude, DamageTypeName());
		}
	}

	return Zone;
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

const TCHAR* UCataclysmSkillTemplate::HealthCostSuppressedStat =
	TEXT("health_cost_suppressed");

const TCHAR* UCataclysmSkillTemplate::ManaPoolBecomesHealthStat =
	TEXT("mana_pool_becomes_health");

bool UCataclysmSkillTemplate::HealthCostIsSuppressed(
	const UAbilitySystemComponent* AbilitySystem)
{
	using Resource = UCataclysmClassResourceAttributeSet;
	const FGameplayAttribute Flag = Resource::GetHealthCostSuppressedAttribute();

	// THE SAME FIRST GUARD AS THE TWO READERS ABOVE: an enemy's ability system
	// carries no class resource set, and an enemy using a skill goes through
	// the same cost function.
	if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(Flag))
	{
		return false;
	}

	// AND THEN ASKED FOR RATHER THAN READ, WHICH THE TWO ABOVE DO NOT DO. The
	// Last Drop's row carries a health condition, so this attribute holds zero
	// even for a character holding the option; reading it would suppress
	// nothing, for ever, with nothing at run time reporting it. Issue #1051.
	//
	// NO SKILL TAGS AND NO COST IN HAND. The option applies to every skill,
	// and passing the cost would be circular: this is what decides whether
	// there is a cost at all.
	const UCataclysmAbilitySystemComponent* Asking =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Asking)
	{
		return false;
	}

	// ANY VALUE ABOVE ZERO IS YES, which is how every other flag stat in the
	// project is read.
	return Asking->StatForSkill(FName(HealthCostSuppressedStat),
								FGameplayTagContainer(), 0.0f) > 0.0f;
}

bool UCataclysmSkillTemplate::ManaPoolBecomesHealth(
	const UAbilitySystemComponent* AbilitySystem)
{
	// ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE, the same way its
	// neighbour above is. The row carries no condition today, so both routes
	// give the same answer; asking means a later row that does carry one is
	// not dropped in silence.
	const UCataclysmAbilitySystemComponent* Asking =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Asking)
	{
		return false;
	}

	// ANY VALUE ABOVE ZERO IS YES, which is how every other flag stat in the
	// project is read.
	return Asking->StatForSkill(FName(ManaPoolBecomesHealthStat),
								FGameplayTagContainer(), 0.0f) > 0.0f;
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
	// THE BASIC ATTACK PAYS NOTHING, AND IT IS THE ONLY SLOT THAT DOES NOT.
	// Issue #1110.
	//
	// WHY IT IS EXEMPT. The design calls that slot "Automatic and free. It IS
	// weapon damage, which is what makes it the anchor every other slot is
	// measured against." It is the only row of `game/Data/SkillSlots.csv` with a
	// mana cost of zero and the only one with `ManaOnHit`, so it RETURNS
	// resource rather than spending it.
	//
	// AND THE PLAYER CANNOT CHOOSE NOT TO SWING, which is the argument that
	// settles it. `UCataclysmBasicAttack`'s header quotes the design: "The basic
	// attack is on no key. It fires automatically... Nothing the player presses
	// triggers it." `ACataclysmPlayerCharacter` swings it at the weapon's attack
	// speed whenever an enemy is in reach. Every other health cost in the game
	// is paid because a button was pressed.
	//
	// WHAT IT COST BEFORE THIS. The project owner played a Masochist holding
	// Exsanguinate on 2026-08-31, which charges 15% of CURRENT health a skill,
	// and reported: "I used my teleport a few times, then pressed e once, and
	// instantly died. Nothing had hit me." At a Fist's 1.45 swings a second the
	// automatic attack alone took a full health bar to nothing in about six
	// seconds, or killed through accumulated debt in under five with The
	// Reckoning. Nothing on screen showed either.
	//
	// ALL HEALTH COSTS AND NOT ONLY THE ADDED ONES, decided by the project owner
	// on 2026-08-31. Blood Pyre is the one skill that states a health cost of
	// its own and it is not a basic attack, so nothing is lost today; the rule
	// is written for whatever weapon row states one next.
	//
	// BEFORE THE ABILITY SYSTEM IS EVEN LOOKED UP, because this decides nothing
	// about the character. `Slot` is stamped onto the ability by
	// `UCataclysmAbilitySystemComponent::GiveAbilityInSlot` when it is granted.
	if (Slot == ECataclysmAbilitySlot::BasicAttack)
	{
		return;
	}

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
	//
	// AND THE WHOLE OF IT MAY BE SUPPRESSED. Issue #1051. The Masochist's The
	// Last Drop reads "While below 20% health your skills cost no health", and
	// that covers what the skill charges AND what the character adds, which is
	// why it is applied to the sum rather than to either half.
	//
	// ZERO AND NOT A REDUCTION, which is forced rather than chosen: a Less
	// multiplier is floored at -99, so "no health" cannot be written as one.
	//
	// THREE THINGS FOLLOW AND ALL THREE ARE CONSEQUENCES OF THE COST BEING
	// ZERO RATHER THAN SEPARATE RULES. The branch below is guarded on the cost,
	// so a suppressed cost generates no Fervour from spending, defers no health
	// debt, and opens no "after you pay a health cost" window for Blood Rush.
	// Nothing was paid, so none of those happened.
	const float Cost = HealthCostIsSuppressed(AbilitySystem)
		? 0.0f
		: FromCurrent + Added;

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
	const float DeferredByShare = UCataclysmHealthDebt::AmountDeferred(
		Cost, UCataclysmHealthDebt::DeferredSharePercent(AbilitySystem));

	// AND WHAT IS LEFT MAY BE MORE THAN THE CHARACTER CAN PAY, IN WHICH CASE
	// THE REST IS OWED RATHER THAN FATAL. Issue #1069. The Masochist's Rock
	// Bottom reads "A health cost can never reduce you below 1 health; anything
	// you cannot pay becomes health debt instead."
	//
	// A DIFFERENT THING FROM THE DEFERRED SHARE ABOVE, though both end up as
	// debt. That share is decided BEFORE the charge, as a percentage the node
	// states; this is whatever is left over AFTER it, which depends on how much
	// health the character has at this instant. A character can have both.
	//
	// AGAINST THE SAME FLOOR THE SKILL'S OWN SHARE ALREADY OBEYS, which is what
	// makes the option's first clause true of the WHOLE cost rather than of
	// half of it. `FromCurrent` above is already floored; `Added` is not, and
	// the design allows it to kill a character who does not hold this option.
	//
	// ZERO FOR EVERY CHARACTER WITHOUT THE OPTION, and zero for one holding it
	// that can afford the charge, which is the ordinary case.
	const float Unpayable =
		UCataclysmHealthDebt::UnpayableBecomesDebt(AbilitySystem)
			? UCataclysmHealthDebt::AmountUnpayable(
				  Cost - DeferredByShare, Current,
				  LeastHealthAfterCurrentHealthCost)
			: 0.0f;

	const float Deferred = DeferredByShare + Unpayable;
	const float Immediate = Cost - Deferred;

	// AND THE LOG SAYS WHAT A SKILL CHARGED, WHICH IT DID NOT UNTIL ISSUE #1112.
	//
	// WHY THIS LINE EXISTS. On 2026-08-31 the project owner reported losing
	// about 2,500 health the instant they pressed one key, and nothing anywhere
	// could say which skill ran or what it charged. Working out the two health
	// cost faults before this one -- issues #1107 and #1110 -- each took reading
	// a save file, two data tables and the design document, and neither reading
	// could answer this one.
	//
	// AT `Log` AND NOT `Verbose`, unlike the rest of this file's messages. A
	// health cost is rare enough to be worth a line: only a Masochist pays one
	// at all, and only when a skill is used. `UCataclysmHealthDebt`'s per-cast
	// bookkeeping stays at `Verbose` for the opposite reason.
	//
	// IT NAMES THE SKILL AND THE SLOT, because which of the two is wrong is the
	// question. A cost that is right for the skill and wrong for the slot is a
	// different fault from a cost that is simply too large.
	//
	// PAST THE `Cost > 0` BRANCH BELOW ON PURPOSE, so a skill that charged
	// nothing says so rather than being silent. "It cost nothing" and "nothing
	// ran" look identical in a log otherwise, and telling them apart is most of
	// the work of answering a report like the one above.
	// THE SLOT AS ITS TAG RATHER THAN AS A NUMBER, because a log a person reads
	// should not need the enum in front of them. `CataclysmAbilitySlots::Tag`
	// answers `Slot.Aura` and the like, and is the same lookup the skill bar and
	// the enchantment scoping use.
	UE_LOG(LogCataclysm, Log,
		   TEXT("%s in %s cost %.1f health: %.1f taken now, %.1f deferred. "
				"Health %.1f of %.1f."),
		   *SkillName, *CataclysmAbilitySlots::Tag(Slot).ToString(),
		   Cost, Immediate, Deferred, Current, Maximum);

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
			// AND A COST PAID SOON AFTER THE LAST ONE BUILDS A STACK. Issue
			// #1002. Sanguine Momentum: "Each health cost paid within 3 seconds
			// of the last grants a stack, up to 5 stacks."
			//
			// BEFORE THE LINE BELOW, WHICH IS THE WHOLE ORDERING QUESTION.
			// `NoteHealthCostPaid` on the component moves the timestamp to now,
			// and this reads how long ago the PREVIOUS payment was. Called
			// afterwards, every payment would be nought seconds after itself and
			// the first health cost of a fight would grant a stack.
			UCataclysmStacks::NoteHealthCostPaid(Cataclysm);

			Cataclysm->NoteHealthCostPaid();
		}
	}

	// AND CASTING AT ALL MAY GRANT FERVOUR, WHATEVER IT COST. Issue #1051.
	// The Masochist's The Last Drop reads "While below 20% health your skills
	// cost no health, and every skill you cast grants 10 Fervour."
	//
	// OUTSIDE THE BRANCH ABOVE, AND IT HAS TO BE. That branch is guarded on
	// the cost being above zero, and this option's OTHER clause makes the cost
	// zero, so a grant inside it would never fire for the one character that
	// has the option. The node also says "every skill", which includes the
	// ones that state no cost at all -- every skill in the game but Blood Pyre.
	//
	// A DIFFERENT RULE FROM THE FERVOUR A COST GENERATES, which is charged
	// inside that branch by `GainFromHealthCost` and is a share of the health
	// spent. This is a flat count for the act of casting. A character holding
	// The Last Drop gets this one and not that one, because it spends nothing.
	//
	// ITS RETURN VALUE IS DROPPED. Zero is the answer for every character in
	// the game without that capstone option, and it is returned for tests.
	UCataclysmFervour::GainForCast(AbilitySystem);
}
