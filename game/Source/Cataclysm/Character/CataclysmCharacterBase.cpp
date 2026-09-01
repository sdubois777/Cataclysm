// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmCharacterBase.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
// For the aura that puts a debuff on whatever stands near. Issue #1057.
#include "AbilitySystem/CataclysmContagion.h"
// For holding the debuffs on a character still. Issue #1070.
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmLeech.h"
// For the nova a character at very low health releases. Issue #1050.
#include "AbilitySystem/CataclysmNova.h"
// For health owed falling due. Issue #991.
// For the Fervour that arrives from the passage of time. Issue #1008.
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmHealthDebt.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Engine/World.h"
#include "TimerManager.h"

ACataclysmCharacterBase::ACataclysmCharacterBase()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACataclysmCharacterBase::BeginPlay()
{
	Super::BeginPlay();

	// A REPEATING TIMER RATHER THAN A TICK. This is the only per-frame work any
	// of these characters would have and it does not need a frame. The base
	// disables ticking for every character deliberately, and the player pawn's
	// own Tick switches itself off the moment its camera glide settles, so
	// adding regeneration to either would undo that.
	//
	// NOT AN INFINITE PERIODIC GAMEPLAY EFFECT, which is the other shape this
	// could take. Its magnitude would have to be attribute-based, because the
	// amount comes from the character's own HealthRegen, ManaRegen and
	// EnergyShieldRegen, and an effect built at runtime with three
	// attribute-based modifiers is both harder to read and impossible to test
	// without an ability system -- whereas UCataclysmRegeneration::GainPerStep
	// and ShieldMayRefill can be checked with plain numbers.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RegenerationTimer, this,
			&ACataclysmCharacterBase::RegenerationStep,
			UCataclysmRegeneration::StepSeconds, /*bLoop=*/true,
			/*InFirstDelay=*/UCataclysmRegeneration::StepSeconds);
	}
}

void ACataclysmCharacterBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RegenerationTimer);
	}

	Super::EndPlay(EndPlayReason);
}

void ACataclysmCharacterBase::RegenerationStep()
{
	UCataclysmRegeneration::ApplyStep(this, UCataclysmRegeneration::StepSeconds,
									  SecondsSinceLastDamage());

	// LEECH ARRIVES ON THE SAME TIMER, AND IS NOT REGENERATION. Issue #895. The
	// design pays a hit's leech out across the next three seconds rather than at
	// once, and this is the only per-character step that already runs often
	// enough to do it smoothly. It is a separate call rather than another job
	// inside ApplyStep because the two obey different rules: an energy shield
	// waits three seconds after damage before it regenerates, and leech must not
	// wait, or it would stop exactly when it is meant to work.
	UCataclysmLeech::PayOutStep(this, UCataclysmRegeneration::StepSeconds);

	// AND HEALTH OWED FALLS DUE ON THE SAME TIMER. Issue #991. The Masochist's
	// Deferred Payment node takes part of a skill's health cost three seconds
	// after the skill is used, and this is the only per-character step that
	// already runs often enough to notice.
	//
	// A THIRD JOB HERE RATHER THAN A TIMER PER DEBT. A debt falling due a
	// fraction of a second late is not something a player can perceive, and a
	// timer per debt would be one more thing to cancel when a character dies.
	//
	// AND IT DRAINS OUT OVER FIVE SECONDS RATHER THAN ARRIVING IN ONE WRITE,
	// since issue #1120. The step length is passed in for that reason: this
	// timer is the clock the drain runs on, the same way leech above uses it.
	//
	// ITS RETURN VALUE IS DROPPED. Zero is the ordinary answer -- nothing owed,
	// or owed and not yet due -- and it is returned for tests rather than for
	// this caller.
	UCataclysmHealthDebt::DrainIfDue(this, UCataclysmRegeneration::StepSeconds);

	// AND A DEBT THAT NEVER FALLS DUE BLEEDS ITS OWNER OUT INSTEAD, ON THE SAME
	// TIMER. Issue #997, changed from an instant death to a drain by issue
	// #1120. The Masochist's The Reckoning keystone reads "If your debt ever
	// exceeds your current health, you die", and "ever" is what a step that runs
	// several times a second is for.
	//
	// AFTER THE DRAIN RATHER THAN BEFORE IT, so an ordinary debt draining this
	// step is taken first and this sees the health it left behind. The two never
	// both act on one character -- a Reckoning debt returns early from the drain
	// -- so the order changes nothing today, and it is the order that stays
	// right if that ever stops being true.
	//
	// ITS RETURN VALUE IS DROPPED, the same as the drain above and for the same
	// reason: zero is the ordinary answer and it is returned for tests.
	UCataclysmHealthDebt::DrainWhileDebtExceedsHealth(
		this, UCataclysmRegeneration::StepSeconds);

	// AND FERVOUR MAY ARRIVE FROM THE PASSAGE OF TIME ALONE. Issue #1008. The
	// Masochist's Low Life keystone reads "While at or below 35% health you gain
	// 10 Fervour per second", and it is the first thing that fills the pool
	// without health having moved at all.
	//
	// A FIFTH JOB ON THIS STEP RATHER THAN A TIMER OF ITS OWN, for the reason
	// the debt gives above: this already runs several times a second, and a
	// timer per character is one more thing to cancel when one dies.
	//
	// AFTER THE LETHAL CHECK, so a character that just died gains nothing. The
	// step's own guards would catch it anyway -- the grant refuses a component
	// with no class resource set and this one refuses a corpse -- but the order
	// is what makes that true rather than incidental.
	//
	// ITS RETURN VALUE IS DROPPED, the same as the two calls above. Zero is the
	// ordinary answer for every character in the game.
	if (!UCataclysmSkillEffects::IsDead(this))
	{
		UCataclysmFervour::GainPerSecondStep(
			UCataclysmTargeting::AbilitySystemOf(this),
			UCataclysmRegeneration::StepSeconds);
	}

	// AND A CHARACTER LOW ENOUGH ON HEALTH MAY RELEASE A NOVA. Issue #1050.
	// The Masochist's Unstable Aura reads "While at or below 10% health, you
	// release a nova every 5 seconds dealing damage equal to 1% of your
	// missing health per point to enemies within 6 metres", and it is the
	// only thing in the game that DEALS damage without the character acting.
	//
	// A SIXTH JOB ON THIS STEP RATHER THAN A TIMER OF ITS OWN, for the reason
	// the debt and the Fervour above both give: this already runs several
	// times a second, and a timer per character is one more thing to cancel
	// when one dies. The five second interval is kept by a timestamp on the
	// ability system component rather than by how often this is called.
	//
	// IT REFUSES A CORPSE ITSELF rather than sitting inside the branch above.
	// The two questions differ: that branch guards a grant to the character
	// itself, and this deals damage to other actors, so its own refusal is
	// where a reader looking for it will be.
	//
	// ITS RETURN VALUE IS DROPPED, the same as the three calls above. Zero is
	// the ordinary answer for every character in the game.
	UCataclysmNova::Step(this);

	// AND A CHARACTER MAY RADIATE AN AURA THAT PUTS A DEBUFF ON WHATEVER IS
	// STANDING NEAR IT. Issue #1057. The Masochist's Beacon of Despair reads
	// "You radiate an aura that applies a random debuff to enemies within 6
	// metres every 3 seconds", and it is the second thing in the game that
	// reaches another character without this one acting.
	//
	// A SEVENTH JOB ON THIS STEP RATHER THAN A TIMER OF ITS OWN, for the reason
	// every one above gives. Its three second interval is kept by a timestamp on
	// the ability system component, separate from the nova's five, so a
	// character holding both nodes runs each at its own rate.
	//
	// AFTER THE NOVA AND NOT BEFORE IT, though nothing today depends on the
	// order: they touch different things, one dealing damage and one applying a
	// lasting effect, and neither reads what the other wrote.
	//
	// IT REFUSES A CORPSE ITSELF, the same as the nova and for the same reason.
	UCataclysmContagion::AuraStep(this);

	// AND THE DEBUFFS ON A CHARACTER MAY BE HELD STILL RATHER THAN COUNTING
	// DOWN. Issue #1070. The Masochist's Ceaseless Penance reads "Debuffs on you
	// no longer expire while you are above 50% health", and something has to
	// keep pushing their end times out for as long as that stays true.
	//
	// AN EIGHTH JOB ON THIS STEP RATHER THAN A TIMER OF ITS OWN, for the reason
	// every one above gives, and with one more of its own: the condition can
	// stop being true between one step and the next, so what this needs is
	// exactly a thing that runs often and asks again each time.
	//
	// IT IS PASSED THE LENGTH OF THE STEP, WHICH NOTHING ELSE HERE NEEDS IN THE
	// SAME WAY. The others use it as a rate; this pushes each effect's end out
	// by precisely the time that passed, so the time left on it does not move.
	//
	// IT REFUSES A CORPSE ITSELF, the same as the two above.
	//
	// ITS RETURN VALUE IS DROPPED. Zero is the answer for every character in the
	// game without that capstone option, and it is returned for tests.
	UCataclysmDebuffs::HoldStep(this, UCataclysmRegeneration::StepSeconds);
}

void ACataclysmCharacterBase::NoteDamageTaken()
{
	const UWorld* World = GetWorld();
	LastDamagedAtSeconds = World ? World->GetTimeSeconds() : 0.0f;
}

bool ACataclysmCharacterBase::IsRegenerating() const
{
	const UWorld* World = GetWorld();
	return World
		&& World->GetTimerManager().IsTimerActive(RegenerationTimer);
}

float ACataclysmCharacterBase::SecondsSinceLastDamage() const
{
	// NEVER HURT ANSWERS WITH A LARGE NUMBER RATHER THAN WITH ZERO. Zero would
	// read as "hurt this instant" and would stop an energy shield ever filling
	// on a character nothing has touched.
	if (LastDamagedAtSeconds < 0.0f)
	{
		return TNumericLimits<float>::Max();
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return TNumericLimits<float>::Max();
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - LastDamagedAtSeconds);
}

UAbilitySystemComponent* ACataclysmCharacterBase::GetAbilitySystemComponent() const
{
	// Subclasses decide where the component lives.
	return nullptr;
}

void ACataclysmCharacterBase::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	TeamId = NewTeamId;
}

FGenericTeamId ACataclysmCharacterBase::GetGenericTeamId() const
{
	return TeamId;
}

void ACataclysmCharacterBase::InitAbilityActorInfo()
{
	// Subclasses supply the owner and avatar.
}
