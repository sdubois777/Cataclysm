// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmHealthDebt.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
#include "Character/CataclysmCharacterBase.h"

const TCHAR* UCataclysmHealthDebt::DeferredShareStat =
	TEXT("deferred_health_cost_share");

const TCHAR* UCataclysmHealthDebt::DelayExtensionStat =
	TEXT("health_debt_delay_extension");

const TCHAR* UCataclysmHealthDebt::ClearedOnlyByAKillStat =
	TEXT("health_debt_cleared_only_by_a_kill");

const TCHAR* UCataclysmHealthDebt::UnpayableBecomesDebtStat =
	TEXT("unpayable_health_cost_becomes_debt");

const TCHAR* UCataclysmHealthDebt::ClearedOnDroppingLowStat =
	TEXT("debt_cleared_on_dropping_low");

float UCataclysmHealthDebt::DeferredSharePercent(
	const UAbilitySystemComponent* AbilitySystem)
{
	using Resource = UCataclysmClassResourceAttributeSet;
	const FGameplayAttribute Share =
		Resource::GetDeferredHealthCostShareAttribute();

	// AN ABILITY SYSTEM WITHOUT THE SET DEFERS NOTHING. Every player carries the
	// class resource set; an enemy's ability system does not, and an enemy using
	// a skill goes through the same cost function.
	if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(Share))
	{
		return 0.0f;
	}

	// The attribute is already held between 0 and 100 by the set's
	// PreAttributeChange, so this guards only a value written before that ran.
	return FMath::Clamp(AbilitySystem->GetNumericAttribute(Share), 0.0f, 100.0f);
}

float UCataclysmHealthDebt::AmountDeferred(float Cost, float SharePercent)
{
	if (Cost <= 0.0f || SharePercent <= 0.0f)
	{
		return 0.0f;
	}

	// NEVER MORE THAN THE COST. A share above a hundred would otherwise defer
	// more than was charged, and the difference would be health handed back.
	return Cost * FMath::Clamp(SharePercent, 0.0f, 100.0f) / 100.0f;
}

void UCataclysmHealthDebt::Defer(UAbilitySystemComponent* AbilitySystem,
								 float Amount)
{
	if (!AbilitySystem || Amount <= 0.0f)
	{
		return;
	}

	const FGameplayAttribute Owed =
		UCataclysmClassResourceAttributeSet::GetHealthOwedAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Owed))
	{
		return;
	}

	// ADDED TO WHAT IS ALREADY OWED RATHER THAN REPLACING IT. A character can
	// cast twice before the first debt falls due, and the design treats what is
	// owed as one amount: the Rolling Debt node speaks of "the delay on what is
	// owed", singular.
	AbilitySystem->ApplyModToAttribute(Owed, EGameplayModOp::Additive, Amount);

	if (UCataclysmAbilitySystemComponent* Cataclysm =
			Cast<UCataclysmAbilitySystemComponent>(AbilitySystem))
	{
		Cataclysm->NoteHealthDebtDueIn(DelaySeconds);
	}
}

float UCataclysmHealthDebt::DrainedInStep(float Remaining, float SecondsLeft,
										  float SecondsInStep)
{
	if (Remaining <= 0.0f || SecondsInStep <= 0.0f)
	{
		return 0.0f;
	}

	// THE LAST STEP TAKES WHATEVER IS LEFT, and so does a step that arrived
	// late. See the header for why a fixed fraction of the remainder would never
	// reach zero.
	if (SecondsLeft <= SecondsInStep)
	{
		return Remaining;
	}

	return Remaining * SecondsInStep / SecondsLeft;
}

float UCataclysmHealthDebt::DrainIfDue(AActor* Character, float SecondsInStep)
{
	// A CORPSE PAYS NOTHING, for the reason `UCataclysmRegeneration::ApplyStep`
	// skips one: an enemy is destroyed on the step AFTER it dies, so there is a
	// real window in which a dead creature is still standing there with an
	// ability system and a debt.
	if (!Character || UCataclysmSkillEffects::IsDead(Character))
	{
		return 0.0f;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Character));
	if (!AbilitySystem || !AbilitySystem->IsHealthDebtDue())
	{
		return 0.0f;
	}

	// A RECKONING DEBT NEVER FALLS DUE, whatever the clock says. Issue #997.
	// The keystone reads "Health costs are never taken. They accumulate as a
	// debt... and the debt is cleared only by killing an enemy."
	//
	// BEFORE THE AMOUNT IS READ AND WITHOUT CLEARING THE DUE TIME, deliberately.
	// Clearing it would be the ordinary path saying "settled", and this debt has
	// not been. `ClearOnKill` is the only thing that ends it, and
	// `KillIfDebtExceedsHealth` is what happens if nothing does.
	//
	// NOT THE SAME TEST AS A DEFERRED SHARE OF 100. Deferred Payment at its full
	// ten points also defers the whole cost and its debt still falls due here.
	if (IsClearedOnlyByAKill(AbilitySystem))
	{
		return 0.0f;
	}

	const FGameplayAttribute Owed =
		UCataclysmClassResourceAttributeSet::GetHealthOwedAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Owed))
	{
		return 0.0f;
	}

	const float Amount = AbilitySystem->GetNumericAttribute(Owed);
	if (Amount <= 0.0f)
	{
		// OWED NOTHING BUT MARKED DUE, which a debt settled some other way could
		// leave behind. Clearing the due time here stops it being asked again on
		// every step for the rest of the character's life.
		AbilitySystem->ClearHealthDebtDue();
		return 0.0f;
	}

	// HOW FAR THROUGH THE DRAIN THIS STEP IS, worked out from the due time the
	// ability system already keeps rather than from a second countdown. Issue
	// #1120.
	//
	// A MISSING WORLD DRAINS THE LOT, which is the same safe direction
	// `NoteHealthDebtDueIn` takes with no clock: the debt has fallen due, so it
	// is owed, and the alternative is a character that owes forever.
	const UWorld* World = Character->GetWorld();
	const float SecondsSinceDue = World
		? World->GetTimeSeconds() - AbilitySystem->HealthDebtDueAt()
		: DrainSeconds;
	const float Taken = DrainedInStep(Amount, DrainSeconds - SecondsSinceDue,
									  SecondsInStep);
	if (Taken <= 0.0f)
	{
		return 0.0f;
	}

	// NO FLOOR, AND THAT IS DELIBERATE. The floor that keeps a cost taken from
	// current health from emptying it (issue #986) applies when the cost is
	// worked out, not when a deferred part of it drains. By then it is a debt,
	// and the design is explicit that a debt may kill: The Reckoning reads "If
	// your debt ever exceeds your current health, you die." That keystone's debt
	// never reaches this line at all -- it returned above -- so the two rules do
	// not meet, and this one stays a plain charge that may empty a character.
	//
	// A DRAIN AND NOT ONE WRITE SINCE ISSUE #1120, which is what makes the loss
	// something a player can watch happen and what carries health across Rock
	// Bottom's threshold on the way down. The amount is the same; only its
	// arrival is spread out.
	AbilitySystem->ApplyModToAttribute(
		UCataclysmVitalAttributeSet::GetHealthAttribute(),
		EGameplayModOp::Additive, -Taken);

	// AND WHAT WAS TAKEN IS NO LONGER OWED. The debt shrinks as it drains, which
	// is the whole difference between this and
	// `DrainWhileDebtExceedsHealth` below, where the amount owed stands while
	// health comes out.
	//
	// THE DEBT IS REDUCED WHETHER OR NOT THE HEALTH WAS THERE. A character that
	// could not afford it has still been charged; whether being unable to afford
	// it kills them is issue #971's question and The Reckoning's sentence, and
	// neither is answered here.
	const float Left = FMath::Max(0.0f, Amount - Taken);
	AbilitySystem->SetNumericAttributeBase(Owed, Left);
	if (Left <= 0.0f)
	{
		// NOTHING LEFT TO TAKE, so the due time goes with it. Leaving it behind
		// would have this function ask on every step for the rest of the
		// character's life whether a debt of nothing had fallen due.
		AbilitySystem->ClearHealthDebtDue();
	}

	// AT `Log` AND NOT `Verbose`, like the death two functions down and unlike
	// the rest of this file. Issue #1112.
	//
	// WHY THIS ONE IS WORTH A LINE. A debt starts coming out 3 seconds after the
	// cast that deferred it, so the health it takes arrives detached in time
	// from anything the player did, and lands on whatever they happen to be
	// doing at that instant. The project owner reported losing about 2,500
	// health "as soon as I hit e" on 2026-08-31; several deferred halves falling
	// due together is the shape that would produce exactly that, and nothing
	// recorded it.
	//
	// ONE LINE PER STEP AND NOT ONE PER DEBT, which is four a second for at most
	// five seconds. That is a bounded burst rather than a flood, and it is what
	// lets a log say how fast the drain ran rather than only that it happened.
	//
	// WHAT IS STILL OWED IS NAMED AS WELL AS THE HEALTH, because the two
	// together are what say whether the character is going to survive it.
	UE_LOG(LogCataclysm, Log,
		   TEXT("%s drained %.1f of a health debt, %.1f still owed, "
				"leaving %.1f health."),
		   *Character->GetName(), Taken, Left,
		   AbilitySystem->GetNumericAttribute(
			   UCataclysmVitalAttributeSet::GetHealthAttribute()));

	return Taken;
}

float UCataclysmHealthDebt::DelayExtensionSeconds(
	const UAbilitySystemComponent* AbilitySystem)
{
	using Resource = UCataclysmClassResourceAttributeSet;
	const FGameplayAttribute Extension =
		Resource::GetHealthDebtDelayExtensionAttribute();

	// AN ABILITY SYSTEM WITHOUT THE SET EXTENDS NOTHING, the same refusal
	// `DeferredSharePercent` makes and for the same reason: every player carries
	// the class resource set and no enemy does.
	if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(Extension))
	{
		return 0.0f;
	}

	// FLOORED AT ZERO. The set's PreAttributeChange already floors it, so this
	// guards only a value written before that ran.
	return FMath::Max(0.0f, AbilitySystem->GetNumericAttribute(Extension));
}

float UCataclysmHealthDebt::ExtendForPaymentWhileOwing(
	UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	const float Extension = DelayExtensionSeconds(AbilitySystem);
	if (Extension <= 0.0f)
	{
		// NO POINT IN ROLLING DEBT, which is every character but one build of
		// one class. Nothing below this line runs for them.
		return 0.0f;
	}

	const FGameplayAttribute Owed =
		UCataclysmClassResourceAttributeSet::GetHealthOwedAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Owed)
		|| AbilitySystem->GetNumericAttribute(Owed) <= 0.0f)
	{
		// "WHILE ONE IS STILL OWED" IS THE WHOLE CONDITION. A character that
		// owes nothing has no delay to extend, which is the ordinary case for
		// the first payment of a fight.
		return 0.0f;
	}

	UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm)
	{
		return 0.0f;
	}

	const float Moved =
		Cataclysm->ExtendHealthDebtDueBy(Extension, MaxDelayExtensionSeconds);
	if (Moved > 0.0f)
	{
		UE_LOG(LogCataclysm, Verbose,
			   TEXT("A health cost pushed an outstanding debt out by %.2f "
					"seconds."), Moved);
	}
	return Moved;
}

bool UCataclysmHealthDebt::IsClearedOnlyByAKill(
	const UAbilitySystemComponent* AbilitySystem)
{
	using Resource = UCataclysmClassResourceAttributeSet;
	const FGameplayAttribute Flag =
		Resource::GetHealthDebtClearedOnlyByAKillAttribute();

	if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(Flag))
	{
		return false;
	}

	// ABOVE ZERO IS YES. The stat is written as a flat 1 by the one node that
	// grants it, and reading it as "anything above nothing" rather than as
	// "exactly 1" means a second source, or a floating point value that arrived
	// as 0.9999, still turns it on rather than silently doing nothing.
	return AbilitySystem->GetNumericAttribute(Flag) > 0.0f;
}

float UCataclysmHealthDebt::ClearOnKill(AActor* Killer)
{
	if (!Killer)
	{
		return 0.0f;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Killer));
	if (!AbilitySystem || !IsClearedOnlyByAKill(AbilitySystem))
	{
		// EVERY OTHER CHARACTER'S DEBT IS UNTOUCHED BY A KILL. Issue #997. The
		// design gives a kill this job for one keystone; for a character with an
		// ordinary deferred cost, clearing the debt would be handing back health
		// the design says they owe.
		return 0.0f;
	}

	const FGameplayAttribute Owed =
		UCataclysmClassResourceAttributeSet::GetHealthOwedAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Owed))
	{
		return 0.0f;
	}

	const float Amount = AbilitySystem->GetNumericAttribute(Owed);
	if (Amount <= 0.0f)
	{
		return 0.0f;
	}

	AbilitySystem->SetNumericAttributeBase(Owed, 0.0f);
	AbilitySystem->ClearHealthDebtDue();

	UE_LOG(LogCataclysm, Verbose,
		   TEXT("%s cleared a health debt of %.1f by killing something."),
		   *Killer->GetName(), Amount);

	return Amount;
}

float UCataclysmHealthDebt::DrainWhileDebtExceedsHealth(AActor* Character,
													    float SecondsInStep)
{
	// A CORPSE IS NOT BLED AGAIN, the same guard `DrainIfDue` opens with and
	// for the same reason: a dead character keeps its ability system for a
	// window before it is removed.
	if (!Character || UCataclysmSkillEffects::IsDead(Character))
	{
		return 0.0f;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Character));
	if (!AbilitySystem || !IsClearedOnlyByAKill(AbilitySystem))
	{
		// ONLY THE RECKONING'S DEBT BLEEDS ITS OWNER. An ordinary deferred debt
		// larger than current health simply takes health to nothing as it
		// drains, which is issue #971's separate question.
		return 0.0f;
	}

	const FGameplayAttribute Owed =
		UCataclysmClassResourceAttributeSet::GetHealthOwedAttribute();
	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Owed)
		|| !AbilitySystem->HasAttributeSetForAttribute(Health))
	{
		return 0.0f;
	}

	const float Amount = AbilitySystem->GetNumericAttribute(Owed);
	const float Current = AbilitySystem->GetNumericAttribute(Health);

	// STRICTLY GREATER, BECAUSE THE DESIGN WRITES "EXCEEDS". A debt exactly
	// equal to current health is survivable, and the boundary is reachable
	// rather than theoretical: a cost is a percentage of a round number often
	// enough that the two land on the same value.
	//
	// AND NOTHING OWED NEVER BLEEDS, however little health is left. Zero is not
	// greater than a positive number, so the comparison already says so; the
	// guard is here because a character at zero health that owes zero would
	// otherwise be a coin toss on floating point.
	if (Amount <= 0.0f || Amount <= Current)
	{
		return 0.0f;
	}

	// WHAT IS OWED, SPREAD ACROSS `DrainSeconds`. Issue #1120.
	//
	// FROM THE WHOLE DEBT AND NOT FROM WHAT IS LEFT OF IT, because nothing is
	// paid off here: the amount owed stands until a kill clears it, so this rate
	// is constant unless the character casts again and adds to the debt.
	//
	// SO THE CHARACTER DIES IN LESS THAN `DrainSeconds`, NOT IN EXACTLY THAT
	// MANY. Health is already smaller than the debt when this runs, so it runs
	// out before the span does, and the deeper in debt the character is the
	// faster that happens. That is the right way round.
	const float Rate = Amount / DrainSeconds;
	const float Wanted = Rate * FMath::Max(0.0f, SecondsInStep);
	if (Wanted <= 0.0f)
	{
		return 0.0f;
	}

	if (Wanted < Current)
	{
		// STILL BLEEDING. Health comes out and the debt is untouched.
		//
		// THIS WRITE IS WHAT PUTS ROCK BOTTOM WITHIN REACH, and it only works
		// because issue #1097 made a direct write to health reach
		// `UCataclysmVitalAttributeSet::PostAttributeBaseChange`, which is where
		// `UCataclysmLowHealthRelief::NoteHealthChanged` is called from. Before
		// that, health falling this way crossed the threshold and nothing
		// noticed. Issue #1119 has what that option was worth beforehand.
		AbilitySystem->ApplyModToAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute(),
			EGameplayModOp::Additive, -Wanted);

		// AT `Log` AND NOT `Verbose`, and once per step rather than once per
		// death. Four lines a second for at most five seconds is a bounded
		// burst, and it is the only way a log can say how fast a character was
		// bleeding rather than only that it died. Issue #1112 is why the
		// death line below is at `Log`; this is the same argument applied to
		// the seconds leading up to it.
		UE_LOG(LogCataclysm, Log,
			   TEXT("%s owes %.1f with %.1f health and is bleeding out at "
					"%.1f a second; %.1f taken this step."),
			   *Character->GetName(), Amount, Current, Rate, Wanted);

		return Wanted;
	}

	// AND THIS STEP IS THE LAST ONE. What is wanted is at least the health that
	// is left, so the character dies now rather than at the end of the span.

	// AT `Log` AND NOT `Verbose`, UNLIKE EVERY OTHER MESSAGE IN THIS FILE.
	// Issue #1101. The rest are per-cast bookkeeping and would fill a play
	// session with one line per skill use; this one says a character died, which
	// happens rarely and is what somebody reading a log is looking for. It was
	// at `Verbose` when the project owner asked on 2026-08-31 what had killed
	// them, and that is why the log could not say.
	UE_LOG(LogCataclysm, Log,
		   TEXT("%s owed %.1f health with %.1f left and bled out."),
		   *Character->GetName(), Amount, Current);

	// THE DEBT GOES WITH THE CHARACTER. Leaving it standing would have the
	// regeneration step ask this question again on the corpse for as long as the
	// body is in the level, and the guard at the top of this function is what
	// would have to catch it. Clearing it says the debt was collected.
	//
	// BEFORE THE HEALTH AND NOT AFTER IT, WHICH IS THE OPPOSITE OF THE ORDER
	// THIS USED TO BE IN. Issue #1072 made the health write below fire the death
	// itself, from `UCataclysmVitalAttributeSet::PostAttributeBaseChange`, and a
	// player's death writes the save record synchronously. Left in the old
	// order, that record would hold a character that died still owing the debt
	// that killed it, which is a different thing from what it holds today and
	// would quietly answer issue #1013 rather than leaving it open.
	AbilitySystem->SetNumericAttributeBase(Owed, 0.0f);
	AbilitySystem->ClearHealthDebtDue();

	// HEALTH TO ZERO AND THEN `HandleDeath`, which is the pair
	// `UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero` uses. Writing the
	// health first means anything that reads it during the death -- a save
	// record, a screen -- sees a character that died rather than one standing
	// with health left and marked dead.
	AbilitySystem->SetNumericAttributeBase(Health, 0.0f);

	// AND THE DEATH IS ASKED FOR EXPLICITLY AS WELL, WHICH IS NOW USUALLY THE
	// SECOND ASK. The line above reaches `HandleDeath` through the attribute
	// hook for any character whose ability system has it as its avatar, and
	// `HandleDeath` refuses a second time because `MarkDead` reports the tag is
	// already there. This stays because the hook asks the AVATAR and this asks
	// the actor that was passed in, and those are the same object for every
	// character in the game today but are not required to be.
	if (ACataclysmCharacterBase* AsCharacter =
			Cast<ACataclysmCharacterBase>(Character))
	{
		AsCharacter->HandleDeath();
	}

	// WHAT THE LAST STEP REALLY TOOK, which is the health that was there rather
	// than the rate's full share of it.
	return Current;
}

bool UCataclysmHealthDebt::UnpayableBecomesDebt(
	const UAbilitySystemComponent* AbilitySystem)
{
	using Resource = UCataclysmClassResourceAttributeSet;
	const FGameplayAttribute Flag =
		Resource::GetUnpayableHealthCostBecomesDebtAttribute();

	// AN ABILITY SYSTEM WITHOUT THE SET OWES NOTHING, the same refusal
	// `IsClearedOnlyByAKill` makes and for the same reason: every player carries
	// the class resource set, no enemy does, and an enemy using a skill goes
	// through the same cost function.
	if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(Flag))
	{
		return false;
	}

	// ASKED FOR RATHER THAN READ WHERE THE COMPONENT IS ONE THIS PROJECT MADE,
	// so a later row carrying a condition is not dropped in silence. The one
	// row today carries none, so both routes agree and the attribute is the
	// fallback.
	if (const UCataclysmAbilitySystemComponent* Asking =
			Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem))
	{
		return Asking->StatForSkill(FName(UnpayableBecomesDebtStat),
									FGameplayTagContainer(),
									AbilitySystem->GetNumericAttribute(Flag))
			> 0.0f;
	}

	// ABOVE ZERO IS YES, the way every other flag stat in the project is read.
	return AbilitySystem->GetNumericAttribute(Flag) > 0.0f;
}

float UCataclysmHealthDebt::AmountUnpayable(float Charge, float CurrentHealth,
											float LeastHealthLeft)
{
	if (Charge <= 0.0f)
	{
		return 0.0f;
	}

	// WHAT IS ABOVE THE FLOOR IS ALL THAT CAN BE TAKEN. A character already at
	// or below it has nothing available, so the whole charge is owed, and the
	// max is what stops a character below the floor being credited a negative
	// amount that would then be subtracted from what it owes.
	const float Available = FMath::Max(0.0f, CurrentHealth - LeastHealthLeft);
	return FMath::Max(0.0f, Charge - Available);
}

float UCataclysmHealthDebt::ClearOnDroppingLow(AActor* Character)
{
	if (!Character)
	{
		return 0.0f;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Character));
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	const FGameplayAttribute Flag = UCataclysmClassResourceAttributeSet
		::GetDebtClearedOnDroppingLowAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Flag))
	{
		return 0.0f;
	}

	// EVERY OTHER CHARACTER'S DEBT IS UNTOUCHED BY FALLING LOW. Issue #1069.
	// The design gives a health crossing this job for one capstone option; for
	// anybody else a debt is meant to be paid, which is the same argument
	// `ClearOnKill` makes about a kill.
	//
	// ASKED FOR RATHER THAN READ, the standing rule for anything a later node
	// might condition. The one row today carries none.
	//
	// AND THE FALLBACK IS THE ATTRIBUTE'S OWN VALUE RATHER THAN ZERO, which is
	// the distinction `UCataclysmDamageConversion::RuleApplies` draws and the
	// one that is easy to get backwards. `StatForSkill` answers with the
	// fallback when no stat line has been recorded for this character at all,
	// which is ordinary rather than a fault. This row carries no condition and
	// no tags, so it IS folded into the attribute, and passing zero would throw
	// the answer away in exactly the case the fallback exists for. The stats
	// that pass zero are the ones whose rows carry a condition, which is never
	// folded in.
	if (AbilitySystem->StatForSkill(FName(ClearedOnDroppingLowStat),
									FGameplayTagContainer(),
									AbilitySystem->GetNumericAttribute(Flag))
		<= 0.0f)
	{
		return 0.0f;
	}

	const FGameplayAttribute Owed =
		UCataclysmClassResourceAttributeSet::GetHealthOwedAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Owed))
	{
		return 0.0f;
	}

	const float Amount = AbilitySystem->GetNumericAttribute(Owed);
	if (Amount <= 0.0f)
	{
		return 0.0f;
	}

	// ALL OF IT, BECAUSE THE OPTION SAYS "ALL OUTSTANDING DEBT". A share would
	// need a number the sentence does not give.
	//
	// AND THE DUE TIME GOES WITH IT, the same pair `ClearOnKill` writes. Leaving
	// the due time behind would have the regeneration step ask on every step for
	// the rest of the character's life whether a debt of nothing had fallen due.
	AbilitySystem->SetNumericAttributeBase(Owed, 0.0f);
	AbilitySystem->ClearHealthDebtDue();

	UE_LOG(LogCataclysm, Verbose,
		   TEXT("%s cleared a health debt of %.1f by dropping to low health."),
		   *Character->GetName(), Amount);

	return Amount;
}
