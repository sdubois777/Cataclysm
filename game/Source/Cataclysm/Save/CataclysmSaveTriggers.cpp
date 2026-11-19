// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSaveTriggers.h"

bool UCataclysmSaveTriggers::MustBeWrittenBeforeTheFrameEnds(
	ECataclysmSaveTrigger Trigger)
{
	// DEATH, AND THE HAND-OVER BEFORE A LEVEL CHANGE. Section 6 names one rule
	// that cannot be relaxed and the first is it: written "in the same frame
	// the health reaches zero [...] before the death is otherwise processed".
	// The second is read by the next level's load, so it must be on disk
	// before the level changes. Issue #2733 slice 3, ruled 2026-11-17.
	return Trigger == ECataclysmSaveTrigger::CharacterDied || Trigger == ECataclysmSaveTrigger::LeavingTheLevel;
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

		// A NEW CHARACTER, so it is listed on the select screen at once. Issue #753, select-screen step 4a.
		case ECataclysmSaveTrigger::CharacterCreated: // A trigger's: a new character writes its record.
			return true;

		// A WON RUN, after its tier was raised, so the record holds the tier the next run is played at. Issue #1324
		// slice 6e.
		case ECataclysmSaveTrigger::RunWon: // A trigger's: a won run writes the character record.
			return true;

		// A TRIP TO ANOTHER LEVEL, which loads the character from this record. Issue #2733 slice 3.
		case ECataclysmSaveTrigger::LeavingTheLevel: // A trigger's: the hand-over writes the character record.
			return true;

		// A CITY UPGRADE BOUGHT, since issue #2742's second slice: a Solo Self-Found character paid from its own gold,
		// which its character record saves. Written for every character, as `InventoryChanged` is.
		case ECataclysmSaveTrigger::CityUpgradeBought: // A trigger's: a purchase writes the character record.
			return true;

		// A SOLO SELF-FOUND CHARACTER'S OWN EMPIRE POINTS OR TREE, which this record holds. Issue #2741 slice 1c.
		case ECataclysmSaveTrigger::OwnEmpireTreeChanged: // A trigger's: its own bank and tree are in this record.
			return true;

		// THE CHARACTER'S BUILD, which this record holds: its passive tree, attribute points and skill choices. Issue
		// #2763. Until it, a point spent waited for the next item, death or trip to reach the disk.
		case ECataclysmSaveTrigger::CharacterBuildChanged: // A trigger's: the build is in this record.
			return true;

		// A CRAFT AT THE FORGE, ruled 2026-11-19: the raised item and the stones spent are in this record.
		case ECataclysmSaveTrigger::ForgeCrafted: // A trigger's: a craft writes the character record.
			return true;

		default:
			return false;
	}
}

bool UCataclysmSaveTriggers::WritesEvenIfTheRunWasWrittenThisFrame(ECataclysmSaveTrigger Trigger)
{
	// AND A WON RUN, since issue #1324 slice 6e: its write replaces the won run's record with the next run's, and a
	// run record written earlier in the same frame would otherwise leave the won run on disk to be loaded again.
	//
	// AND A NEW CHARACTER, since issue #2733, for the same reason: giving it its chosen weapon writes the run record
	// (the weapon it wore goes into the bag, `InventoryChanged`) before its run has begun, in the same frame, so its
	// creation write was skipped and the run record on disk held no empire until the clock's next write.
	//
	// AND THE HAND-OVER BEFORE A LEVEL CHANGE, since issue #2733 slice 3: the clear, a win and a death each write the
	// run record in the frame the player leaves, and the next level loads from the hand-over's.
	//
	// AND A CHANGE OF FLOOR, since issue #2733 slice 3 (ruled 2026-11-18): it writes where the party now stands, and a
	// write earlier in the same frame gathered the floor before the change. Window travel-w1's first run found the
	// arrival's write skipped so. Each place that raises it does so once per change of floor (the entry lists them).
	//
	// AND A RESPEC OF THE EMPIRE UPGRADE TREE, since issue #2741 slice 1b (ruled 2026-11-18): the days it cost passed
	// after any earlier write in its frame gathered the run, and a respec whose days were not written would cost nothing
	// to a player who quit before the next write.
	//
	// AND A CITY UPGRADE BOUGHT, since issue #2742's second slice, for the same reason: a write earlier in the same
	// frame gathered the city before it held the upgrade, while the gold that paid for it is written apart, with the
	// account record. `UCataclysmCityUpgradePurchase::Buy` raises it once per purchase.
	//
	// AND A CRAFT AT THE FORGE, ruled 2026-11-19, for the respec's reason: an upgrade's days passed after any earlier
	// write in its frame gathered the run. `UCataclysmForgeCraft` raises it once per craft.
	return Trigger == ECataclysmSaveTrigger::CharacterDied || Trigger == ECataclysmSaveTrigger::DeathProcessed
		|| Trigger == ECataclysmSaveTrigger::RunWon || Trigger == ECataclysmSaveTrigger::CharacterCreated
		|| Trigger == ECataclysmSaveTrigger::LeavingTheLevel || Trigger == ECataclysmSaveTrigger::ChangedFloor
		|| Trigger == ECataclysmSaveTrigger::EmpireTreeRespecced
		|| Trigger == ECataclysmSaveTrigger::CityUpgradeBought
		|| Trigger == ECataclysmSaveTrigger::ForgeCrafted;
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
		case ECataclysmSaveTrigger::DeathProcessed:         return TEXT("a death having been paid for");
		case ECataclysmSaveTrigger::CharacterCreated:       return TEXT("a character being created");
		case ECataclysmSaveTrigger::RunWon:                 return TEXT("a run being won");
		case ECataclysmSaveTrigger::LeavingTheLevel:        return TEXT("the character leaving the level");
		case ECataclysmSaveTrigger::EmpireTreeRespecced:    return TEXT("the empire upgrade tree being respecced");
		case ECataclysmSaveTrigger::CityUpgradeBought:      return TEXT("a city upgrade being bought");
		case ECataclysmSaveTrigger::OwnEmpireTreeChanged:
			return TEXT("a Solo Self-Found character's own empire points or tree changing");
		case ECataclysmSaveTrigger::CharacterBuildChanged:
			return TEXT("the character's passive tree, attribute points or skill choices changing");
		case ECataclysmSaveTrigger::ForgeCrafted:           return TEXT("a craft at the forge");
	}

	// A TRIGGER ADDED WITHOUT A WORDING. Named rather than fallen through to one
	// of the others, so a log line saying this is a missing case rather than a
	// misdescribed write.
	return TEXT("an unnamed trigger");
}

TArray<ECataclysmSaveTrigger> UCataclysmSaveTriggers::Every()
{
	// WRITTEN OUT RATHER THAN COUNTED FROM THE ENUM. A test walks this list and
	// asserts that exactly two triggers are synchronous (one until issue #2733
	// slice 3) and exactly one writes the account record; a list built from the enum's own range would grow silently
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
		ECataclysmSaveTrigger::DeathProcessed,
		ECataclysmSaveTrigger::CharacterCreated,
		ECataclysmSaveTrigger::RunWon,
		ECataclysmSaveTrigger::LeavingTheLevel,
		ECataclysmSaveTrigger::EmpireTreeRespecced,
		ECataclysmSaveTrigger::CityUpgradeBought,
		ECataclysmSaveTrigger::OwnEmpireTreeChanged,
		ECataclysmSaveTrigger::CharacterBuildChanged,
		ECataclysmSaveTrigger::ForgeCrafted,
	};
}
