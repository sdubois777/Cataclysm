// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmSaveTriggers.generated.h"

/**
 * What made the game decide to write itself to disk.
 *
 * TWO KINDS AND BOTH ARE NEEDED. `docs/Save_System_Design.md` section 6: a clock,
 * "so nothing is ever far from being written", and an immediate write on the
 * events that matter, which "is what makes the gap inside a fight near zero
 * without writing constantly while a player walks around a city".
 *
 * NUMBERED, BECAUSE A TEST NAMES THEM AND A LOG LINE PRINTS THEM. Nothing
 * persists this value, so the numbers are not a compatibility promise the way
 * `ECataclysmLethality`'s are.
 */
UENUM(BlueprintType)
enum class ECataclysmSaveTrigger : uint8
{
	/** The interval elapsed. Nothing in particular happened. */
	Clock = 0,

	/** A creature noticed somebody and started fighting. */
	FightStarted = 1,

	/** A creature died. */
	CreatureDied = 2,

	/** The character's health fell through the threshold. */
	HealthCrossedThreshold = 3,

	/** The party moved to another floor. **NOTHING RAISES THIS YET.** */
	ChangedFloor = 4,

	/** An item or a material entered or left the carried inventory. */
	InventoryChanged = 5,

	/**
	 * The character died.
	 *
	 * THE ONE THAT CANNOT BE RELAXED, and the reason the whole feature exists.
	 * Section 6: for a Hardcore character, death "is written in the same frame
	 * the health reaches zero, through the synchronous write, before the death
	 * is otherwise processed".
	 */
	CharacterDied = 6,

	/**
	 * Something in the account record changed: banked empire upgrade points, the
	 * shared stash, the list of characters.
	 *
	 * **NOTHING RAISES THIS YET** because nothing writes to an account record.
	 * It exists so that the rule below has something to be a rule about.
	 */
	AccountChanged = 7,
};

/**
 * When a save is written, decided, and none of the writing.
 *
 * EVERY JUDGEMENT HERE IS A STATIC OVER PLAIN VALUES, so all of it is testable
 * without a world, a clock or a filesystem -- the same split
 * `UCataclysmEnemyDeath` makes between deciding what dying looks like and
 * playing it. `UCataclysmSaveWriter` is what joins these answers to a world.
 *
 * WHY THE RULES ARE WORTH WRITING DOWN SEPARATELY. Every one of them fails
 * quietly. A death written asynchronously still writes, a moment later, and the
 * moment later is exactly the window a player closing the game would use. An
 * account record written on the clock still holds the right thing, and costs a
 * 600-slot stash serialised every interval for nothing.
 */
UCLASS()
class CATACLYSM_API UCataclysmSaveTriggers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Whether this trigger must be written before the frame ends, rather than
	 * handed to a background thread.
	 *
	 * TRUE FOR EXACTLY ONE TRIGGER. Section 6 names death and nothing else:
	 * "Everything else may be written asynchronously." Making a second trigger
	 * synchronous would be trading frame time for nothing, and making death
	 * asynchronous would give back the escape this feature closes.
	 */
	static bool MustBeWrittenBeforeTheFrameEnds(ECataclysmSaveTrigger Trigger);

	/**
	 * Whether this trigger writes the run record.
	 *
	 * TRUE FOR EVERY TRIGGER EXCEPT THE ACCOUNT ONE. The run is the thing that
	 * changes constantly -- the day, the floor, every creature's health -- and
	 * section 6 says it is what the cadence is for.
	 */
	static bool WritesTheRunRecord(ECataclysmSaveTrigger Trigger);

	/**
	 * Whether this trigger writes the account record.
	 *
	 * TRUE FOR EXACTLY ONE TRIGGER, and that is the point of the split. Section
	 * 6: "the run record on the cadence above, the account record only when it
	 * actually changes. That split was made in section 1 for a different reason
	 * and it is what makes a frequent save affordable." The account record is
	 * the one carrying a 600-slot stash.
	 */
	static bool WritesTheAccountRecord(ECataclysmSaveTrigger Trigger);

	/**
	 * Whether this trigger writes the character record.
	 *
	 * THE INVENTORY AND DEATH, AND NOTHING ELSE. What a character is carrying
	 * lives in its own record rather than in the run, so an item picked up has
	 * to reach that record; and a death has to be written whole, which means the
	 * character as well as the floor it fell on.
	 */
	static bool WritesTheCharacterRecord(ECataclysmSaveTrigger Trigger);

	/** Whether enough time has passed for the clock to write again. */
	static bool ClockHasElapsed(float SecondsSinceLastWrite, float Interval);

	/**
	 * A share of maximum health, from 0 to 1.
	 *
	 * A MAXIMUM OF ZERO ANSWERS ZERO rather than dividing. A character whose
	 * maximum health has not been set yet is not at full health; it has no
	 * health at all, and the threshold below should not fire on the frame the
	 * attributes arrive.
	 */
	static float HealthFraction(float Health, float MaxHealth);

	/**
	 * Whether health just fell through the threshold.
	 *
	 * FALLING ONLY, AND ONLY ON THE CROSSING. A character sitting below the
	 * threshold does not write again every frame, and one healing back through
	 * it does not write at all. Without the first of those, a wounded character
	 * standing still would write the run record on every tick for as long as it
	 * stood there.
	 */
	static bool HealthCrossedTheThreshold(float Before, float After, float Threshold);

	/** The trigger in words, for a log line or a test failure. */
	static const TCHAR* NameOf(ECataclysmSaveTrigger Trigger);

	/** Every trigger there is, so a test can walk the whole set. */
	static TArray<ECataclysmSaveTrigger> Every();

	/**
	 * How long the clock waits between writes, in seconds.
	 *
	 * A STARTING FIGURE, NOT A DERIVED ONE, and section 7 says so: "The exact
	 * autosave interval, in seconds [...] is a tuning constant that needs a
	 * measured write cost." There is still almost nothing in a record to
	 * measure, so this is chosen rather than measured.
	 *
	 * WHY FIFTEEN. The clock only decides what a player loses when NOTHING
	 * happened, because everything that happens inside a fight raises a trigger
	 * of its own. So it is the amount of walking around a city a player would
	 * have to do again, and fifteen seconds of walking is a small loss against
	 * a write every fifteen seconds while idle. **It needs playing.**
	 */
	static constexpr float SecondsBetweenClockWrites = 15.0f;

	/**
	 * The share of maximum health that forces an immediate write, as a fraction.
	 *
	 * ALSO A STARTING FIGURE. Death is already written synchronously, so what
	 * this catches is the APPROACH to death: the fight going badly, before the
	 * blow that ends it. Half is chosen because it is the point at which a fight
	 * has clearly turned, and because a threshold close to zero would fire so
	 * near the death write as to be the same write twice. **It needs playing.**
	 */
	static constexpr float HealthFractionThatForcesAWrite = 0.5f;
};
