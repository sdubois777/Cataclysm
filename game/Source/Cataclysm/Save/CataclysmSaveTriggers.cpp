// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSaveTriggers.h"

bool UCataclysmSaveTriggers::MustBeWrittenBeforeTheFrameEnds(
	ECataclysmSaveTrigger Trigger)
{
	// DEATH AND NOTHING ELSE. Section 6 names one rule that cannot be relaxed
	// and this is it: written "in the same frame the health reaches zero [...]
	// before the death is otherwise processed".
	return Trigger == ECataclysmSaveTrigger::CharacterDied;
}

bool UCataclysmSaveTriggers::WritesTheRunRecord(ECataclysmSaveTrigger Trigger)
{
	// EVERYTHING EXCEPT AN ACCOUNT CHANGE. Banking an empire upgrade point does
	// not change the floor, the day or where anybody is standing.
	return Trigger != ECataclysmSaveTrigger::AccountChanged;
}

bool UCataclysmSaveTriggers::WritesTheAccountRecord(ECataclysmSaveTrigger Trigger)
{
	// ONLY WHEN THE ACCOUNT ACTUALLY CHANGED, which is the whole reason the
	// design splits the records. The account record is the one holding a
	// 600-slot stash, and writing it on the clock would serialise all of that
	// every interval to record that nothing had happened to it.
	return Trigger == ECataclysmSaveTrigger::AccountChanged;
}

bool UCataclysmSaveTriggers::WritesTheCharacterRecord(ECataclysmSaveTrigger Trigger)
{
	switch (Trigger)
	{
		// WHAT THE CHARACTER IS CARRYING lives in the character record and not
		// in the run, so an item picked up off the floor has to reach it.
		case ECataclysmSaveTrigger::InventoryChanged:
			return true;

		// A DEATH IS WRITTEN WHOLE. The floor says the character fell; the
		// character record says what it was carrying when it did.
		case ECataclysmSaveTrigger::CharacterDied:
			return true;

		default:
			return false;
	}
}

bool UCataclysmSaveTriggers::ClockHasElapsed(float SecondsSinceLastWrite,
											 float Interval)
{
	if (Interval <= 0.0f)
	{
		// AN INTERVAL OF ZERO IS NOT "WRITE EVERY FRAME". It is a number nobody
		// set, and treating it as a cadence would write the run record on every
		// tick for the whole session. There is no configuration that switches
		// the clock off -- section 6 requires one -- so this is a misconfigured
		// value rather than a choice, and refusing is what makes it visible.
		return false;
	}

	return SecondsSinceLastWrite >= Interval;
}

float UCataclysmSaveTriggers::HealthFraction(float Health, float MaxHealth)
{
	if (MaxHealth <= 0.0f)
	{
		return 0.0f;
	}

	return FMath::Clamp(Health / MaxHealth, 0.0f, 1.0f);
}

bool UCataclysmSaveTriggers::HealthCrossedTheThreshold(float Before, float After,
													   float Threshold)
{
	// FALLING THROUGH IT, WHICH IS THREE CONDITIONS AND NOT ONE.
	//
	// It was at or above the line before      -- otherwise a character already
	//                                            below writes on every frame it
	//                                            stays there
	// it is below the line now                -- the crossing itself
	// and the line is a real one              -- a threshold of zero would fire
	//                                            on the frame health reaches
	//                                            zero, which is the death write's
	//                                            job and would double it
	if (Threshold <= 0.0f)
	{
		return false;
	}

	return Before >= Threshold && After < Threshold;
}

const TCHAR* UCataclysmSaveTriggers::NameOf(ECataclysmSaveTrigger Trigger)
{
	switch (Trigger)
	{
		case ECataclysmSaveTrigger::Clock:                  return TEXT("the clock");
		case ECataclysmSaveTrigger::FightStarted:           return TEXT("a fight starting");
		case ECataclysmSaveTrigger::CreatureDied:           return TEXT("a creature dying");
		case ECataclysmSaveTrigger::HealthCrossedThreshold: return TEXT("health falling through the threshold");
		case ECataclysmSaveTrigger::ChangedFloor:           return TEXT("changing floor");
		case ECataclysmSaveTrigger::InventoryChanged:       return TEXT("an item entering or leaving the inventory");
		case ECataclysmSaveTrigger::CharacterDied:          return TEXT("the character dying");
		case ECataclysmSaveTrigger::AccountChanged:         return TEXT("the account record changing");
	}

	// A TRIGGER ADDED WITHOUT A WORDING. Named rather than fallen through to one
	// of the others, so a log line saying this is a missing case rather than a
	// misdescribed write.
	return TEXT("an unnamed trigger");
}

TArray<ECataclysmSaveTrigger> UCataclysmSaveTriggers::Every()
{
	// WRITTEN OUT RATHER THAN COUNTED FROM THE ENUM. A test walks this list and
	// asserts that exactly one trigger is synchronous and exactly one writes the
	// account record; a list built from the enum's own range would grow silently
	// when a trigger is added, and the new one would be covered by nothing.
	// `Cataclysm.SaveTriggers.EveryTriggerIsInTheListThisFileAnswersFor` is what
	// notices.
	return {
		ECataclysmSaveTrigger::Clock,
		ECataclysmSaveTrigger::FightStarted,
		ECataclysmSaveTrigger::CreatureDied,
		ECataclysmSaveTrigger::HealthCrossedThreshold,
		ECataclysmSaveTrigger::ChangedFloor,
		ECataclysmSaveTrigger::InventoryChanged,
		ECataclysmSaveTrigger::CharacterDied,
		ECataclysmSaveTrigger::AccountChanged,
	};
}
