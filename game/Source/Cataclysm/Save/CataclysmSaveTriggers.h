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

	/**
	 * The party moved to another floor: `UCataclysmSaveWriter::SetFloor`, which the dungeon raises on the stairs, and,
	 * since issue #2733 slice 3, a walk begun on arriving from the capital. This said nothing raised it until then,
	 * which `SetFloor` had stopped making true.
	 */
	ChangedFloor = 4,

	/**
	 * An item or a material entered or left the carried inventory.
	 *
	 * AND GOLD WAS PICKED UP, since issue #2742: `UCataclysmDropPickup::BankGold` raises it, because a pickup of gold
	 * changes what this writes -- the run record's floor loses the piles, and a Solo Self-Found character's own gold is
	 * in its character record -- exactly as an item picked up does. The account's gold is `AccountChanged`'s.
	 */
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
	 * A DEFEATED DUNGEON RAISES IT, since issue #52's first slice (2026-11-03):
	 * `ACataclysmDungeonGameMode::ClearEmpireDungeon` banks the defeat's empire
	 * upgrade points into the account record the game instance holds, and the
	 * writer writes that record. This said nothing raised it until then.
	 *
	 * SO DO GOLD PICKED UP INTO THE ACCOUNT (`UCataclysmDropPickup::BankGold`,
	 * issue #2742), A CITY UPGRADE PAID FOR FROM THE ACCOUNT
	 * (`UCataclysmCityUpgradePurchase::Buy`, issue #2742 slice 2, beside
	 * `CityUpgradeBought`), and A POINT BOUGHT, AN OPTION CHOSEN OR A RESPEC in a
	 * lethality mode's empire upgrade tree (`UCataclysmGameInstance::
	 * NoteTheEmpireTreeChanged`, issue #2741 slices 1b and 1c; a respec raises
	 * `EmpireTreeRespecced` as well, for the run record). A Solo Self-Found
	 * character's own tree raises `OwnEmpireTreeChanged` instead: it has no
	 * account record. SO DOES A DEBT that changes in the account (issue #2741
	 * slice 6e): a city upgrade bought on credit, gold picked up paying it down,
	 * and Sovereign Loans' part taken on a day that passes (the day's hook in
	 * `UCataclysmGameInstance::WireTheRunToTheBalance`).
	 * This said "Nothing else raises it yet -- there is no stash" until slice 1b;
	 * gold had already made that untrue. Nothing fills the stash yet.
	 */
	AccountChanged = 7,

	/**
	 * A death has been paid for: the dungeon ended, the days charged, and the character stood back up. Issue #753,
	 * the start-up step, ruled 2026-10-30.
	 *
	 * WHY IT EXISTS. The death write above leaves a record holding the character dead, and a load runs the death
	 * again from it, which is right until the death has been paid for. Without this the record would keep the
	 * unpaid death until the next clock write, and a crash in that gap would charge the death twice on load.
	 * `ACataclysmPlayerCharacter::Revive` raises it last.
	 *
	 * AN ORDINARY ASYNCHRONOUS WRITE, OF THE RUN RECORD ONLY. Processing a death changes nothing the character record
	 * holds: the days are the run's, and the health is in the run record's floor placement.
	 */
	DeathProcessed = 8,

	/**
	 * A character made on the front end's new-character screen, its name, partition and creation choice applied when
	 * its level begins (`ACataclysmGameMode::LoadThePendingCharacter`). Issue #753, select-screen step 4a, ruled
	 * 2026-11-01.
	 *
	 * WHY IT EXISTS. Without it the new character's record waited for its first item or its first death, and a player
	 * who quit before either found the character missing from the select screen. AN ORDINARY ASYNCHRONOUS WRITE, of the
	 * character record and the run record.
	 */
	CharacterCreated = 9,

	/**
	 * A run was won: the Cataclysm boss dungeon cleared, the first-kill passive points paid, the character's tier
	 * raised and its next run begun (`UCataclysmGameInstance::BeginTheRunAfterAWin`, which raises it last). Issue
	 * #1324 slice 6e, ruled 2026-11-02.
	 *
	 * WHY IT EXISTS. The coordinating session's comment of 2026-11-01 on #1324: the tier is raised "before the
	 * character record is written". Nothing else a win does raises a trigger -- the clock's write does not write the
	 * character record -- so without this the raised tier, the boss defeated and the new run waited for the next item
	 * or death, and a player who quit first came back to the run that was won, at the old tier. Raised once, after all
	 * of it, so each record is written once and holds the win.
	 *
	 * AN ORDINARY ASYNCHRONOUS WRITE, of the character record and the run record, as `CharacterCreated` is.
	 */
	RunWon = 10,

	/**
	 * The character is leaving this level for another: the capital for a dungeon, or a dungeon for the capital at the
	 * end of a walk (`UCataclysmSaveWriter::HandOver`). Issue #2733 slice 3, ruled by the coordinating session on
	 * 2026-11-17.
	 *
	 * WHY IT EXISTS. A trip between levels is a save and a Continue: the next level loads the character from the two
	 * records this writes, by the identifier the game instance carries across. Whatever these records do not hold, the
	 * trip loses.
	 *
	 * WRITTEN BEFORE THE FRAME ENDS, the second trigger that is, because the next level reads what it wrote: an
	 * asynchronous write could still be in flight when that level's load reads the slot. A loading screen follows it,
	 * so the stall is not seen. Of the character record and the run record, and whatever else wrote the run record
	 * this frame -- the last floor's clear, a win's `RunWon`, a death's `DeathProcessed`.
	 */
	LeavingTheLevel = 11,

	/**
	 * The empire upgrade tree was respecced, and the days it cost passed in the run
	 * (`UCataclysmGameInstance::RespecEmpireTree`). Issue #2741 slice 1b, ruled by the coordinating session on
	 * 2026-11-18.
	 *
	 * WHY IT EXISTS, AND IS NOT THE CLOCK. The respec raises `AccountChanged` for the points it returned, and the run
	 * record must hold the days it took, or a player who quit before the next run write would come back to a respec
	 * that cost nothing. `Clock` writes the run record, but at most once a frame, so a write earlier in the respec's
	 * frame would have left the days off the disk until the next one. This one is in
	 * `UCataclysmSaveTriggers::WritesEvenIfTheRunWasWrittenThisFrame`.
	 *
	 * AN ORDINARY ASYNCHRONOUS WRITE, OF THE RUN RECORD ONLY: a respec changes nothing the character record holds.
	 */
	EmpireTreeRespecced = 12,

	/**
	 * A city upgrade was bought: `UCataclysmCityUpgradePurchase::Buy`, once it has taken the price and the run has
	 * recorded the upgrade. Issue #2742's second slice, ruled by the coordinating session on 2026-11-18.
	 *
	 * WHY IT EXISTS. A purchase changes two records at once: the run record's city holds the upgrade, and the gold that
	 * paid for it left the account record or, for a Solo Self-Found character, the character record. Until gold priced
	 * an upgrade nothing raised a save for one, and the upgrade waited for the clock's next write. Written so, a quit or
	 * a crash in that gap would keep the gold spent and lose the upgrade. `AccountChanged` is raised beside it when the
	 * gold came from the account.
	 *
	 * AN ORDINARY ASYNCHRONOUS WRITE, of the run record and the character record, as `InventoryChanged` is; and written
	 * even if the run record was written earlier the same frame, since that write gathered the city before the upgrade.
	 *
	 * THIRTEEN, NOT TWELVE: the empire tree's slice 1b (#2741) took 12 for `EmpireTreeRespecced` and merged first, the
	 * coordinating session's ruling of 2026-11-18. Nothing writes these numbers to disk.
	 */
	CityUpgradeBought = 13,

	/**
	 * A Solo Self-Found character's own empire upgrade points or tree changed: a defeat's award banked, a point bought,
	 * an option chosen, or a respec (`UCataclysmGameInstance::NoteTheEmpireTreeChanged`). Issue #2741 slice 1c.
	 *
	 * WHY IT EXISTS. Such a character's bank and tree are its own, in its character record (version 9), and
	 * `AccountChanged` writes only the account record, which it has none of. OF THE CHARACTER RECORD, and of the run
	 * record as every trigger but `AccountChanged` is; an ordinary asynchronous write. A respec raises
	 * `EmpireTreeRespecced` after it, for the run record's days.
	 *
	 * NOT IN `UCataclysmSaveTriggers::WritesEvenIfTheRunWasWrittenThisFrame`, AND IT NEED NOT BE. When an earlier write
	 * in the frame wrote the run record, the once-a-frame rule skips this trigger's run-record write and nothing else:
	 * `UCataclysmSaveWriter::NoteTrigger` asks for each record on its own, and only `WriteTheRunRecord` checks the frame.
	 * The character record holds what changed, and it is written. `Cataclysm.EmpireTree.
	 * ASoloSelfFoundPointBoughtIsWrittenToItsOwnRecord` writes the run record first in the same frame and reads the point
	 * back from the character record's slot. Asked by the coordinating session on 2026-11-18.
	 *
	 * FOURTEEN, NOT THIRTEEN: gold's slice 2 (#2742) took 13 for `CityUpgradeBought` and merged first, the coordinating
	 * session's ruling of 2026-11-18. Nothing writes these numbers to disk.
	 */
	OwnEmpireTreeChanged = 14,

	/**
	 * The player changed the character's build: a passive point spent (`ACataclysmPlayerState::SpendPassivePoint`), a
	 * capstone option taken (`ChoosePassiveOption`), the passive tree reset (`ResetPassivePoints`), attribute points
	 * spent or reset (`SpendAttributePoints`, `ResetAttributePoints`), or a skill put on a key or a key's choice
	 * forgotten (`ChooseSkill`, `ForgetSkillChoice`). Issue #2763, ruled by the coordinating session on
	 * 2026-11-19.
	 *
	 * WHY IT EXISTS. All of these live in the character record, and until this nothing wrote it when they changed:
	 * the clock writes the run record only. The project owner spent passive points in a dungeon on 2026-11-19 and
	 * stopped Play In Editor 43 seconds after entering it; their character record, last written when they pressed
	 * Walk, holds no passive node and no attribute point.
	 *
	 * RAISED BY THE PLAYER'S ACTS AND NEVER BY A LOAD. `AssignSkill` is what a load calls to put saved choices back
	 * (`FCataclysmSaveApply`), so the trigger is raised by `ChooseSkill`, which the skill screen and
	 * `Cataclysm.AssignSkill` call, and not by `AssignSkill`; `RestorePassiveAllocation`, `RestoreSpentAttributePoints`
	 * and `SetPassiveAllocation` raise nothing.
	 *
	 * AN ORDINARY ASYNCHRONOUS WRITE, OF THE CHARACTER RECORD, and of the run record as every trigger but
	 * `AccountChanged` is. NOT IN `UCataclysmSaveTriggers::WritesEvenIfTheRunWasWrittenThisFrame`: the run record holds
	 * none of it, and the character record's write does not wait on the frame rule (see `OwnEmpireTreeChanged`).
	 * One write for each act, so `Cataclysm.SpendPassivePoint <node> 5` writes five times in one frame; accepted.
	 */
	CharacterBuildChanged = 15,

	/**
	 * A craft at the Cataclysmic Forge: an item raised a gear level (`UCataclysmForgeCraft::Upgrade`), or two upgrade
	 * stones combined into one of the next level (`UCataclysmForgeCraft::Combine`). The owner's ruling of 2026-11-19,
	 * "build it next".
	 *
	 * WHY IT EXISTS. An upgrade changes two records at once: the character record holds the raised item and the stones
	 * it spent, and the run record holds the days it cost. Written by neither, a quit before the clock's next write
	 * would hand back the stones and the days, or keep the stones spent and the item unraised. A combine changes the
	 * character record alone, and raises it too.
	 *
	 * AN ORDINARY ASYNCHRONOUS WRITE, of the run record and the character record; and written even if the run record
	 * was written earlier the same frame, since that write gathered the run before the upgrade's days had passed,
	 * `EmpireTreeRespecced`'s reason.
	 *
	 * SIXTEEN, NOT FIFTEEN: issue #2763 took 15 for `CharacterBuildChanged` and merged first, the coordinating
	 * session's ruling of 2026-11-19. Nothing writes these numbers to disk.
	 */
	ForgeCrafted = 16,
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
	 * TRUE FOR EXACTLY TWO TRIGGERS: the character dying, and the hand-over before a level change. Section 6 names
	 * death as the write that cannot be relaxed, and says "Everything else may be written asynchronously" -- may, and
	 * the hand-over is the one that may not: the next level reads it (`LeavingTheLevel`). Any other trigger made
	 * synchronous would be trading frame time for nothing, and making death asynchronous would give back the escape
	 * this feature closes. Exactly one until issue #2733 slice 3, ruled by the coordinating session on 2026-11-17.
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
	 * THE INVENTORY AND DEATH, AND NOTHING ELSE -- and since then a new character (`CharacterCreated`, 2026-11-01),
	 * a won run (`RunWon`, issue #1324 slice 6e, 2026-11-03), the hand-over before a level change (`LeavingTheLevel`,
	 * issue #2733 slice 3), a city upgrade bought (`CityUpgradeBought`, issue #2742 slice 2), a Solo Self-Found
	 * character's own empire points or tree (`OwnEmpireTreeChanged`, issue #2741 slice 1c), a change the player
	 * made to the character's build (`CharacterBuildChanged`, issue #2763) and a craft at the forge (`ForgeCrafted`,
	 * ruled 2026-11-19). What a character is carrying
	 * lives in its own record rather than in the run, so an item picked up has
	 * to reach that record; and a death has to be written whole, which means the
	 * character as well as the floor it fell on.
	 */
	static bool WritesTheCharacterRecord(ECataclysmSaveTrigger Trigger);

	/**
	 * Whether this trigger writes the run record even when another write already wrote it in the same frame.
	 *
	 * THE DEATH AND THE PROCESSING OF ONE. The run record is otherwise written at most once a frame, which is right
	 * when several creatures die together. A death must be written whatever else happened; the processing write must
	 * replace the unpaid death on disk, and a trigger raised on the way out of the dungeon in the same frame would
	 * otherwise stop it. Issue #753, the start-up step.
	 *
	 * AND A WON RUN, since issue #1324 slice 6e (2026-11-03), for the processing write's reason: it replaces the won
	 * run's record with the next run's in the same slot, and a run record written earlier in the frame -- a creature
	 * dying, a floor changing -- would otherwise leave the won run on disk until the next write, for a load to resume.
	 *
	 * AND A NEW CHARACTER, since issue #2733, for the same reason. `ACataclysmGameMode::MakeThePendingCharacter` gives
	 * the character its chosen weapon (`ApplyCreationChoice`) before it begins the run; the weapon it wore goes into the
	 * bag, which raises `InventoryChanged` and writes a run record with no run in it; and `CharacterCreated`, in the same
	 * frame, was skipped, so a Continue before the clock's next write found no empire and began a new run. The two
	 * writes go through `FCataclysmSaveStorage`'s one pipe, which runs them in the order they were started, so the
	 * creation's lands last.
	 *
	 * AND THE HAND-OVER BEFORE A LEVEL CHANGE, since issue #2733 slice 3: the last floor's clear, a win's `RunWon` and
	 * a death's `DeathProcessed` all write the run record in the frame the player leaves, and the next level loads
	 * from what the hand-over writes. Synchronous, it waits for those earlier writes to the slot to finish first, so
	 * it lands last.
	 *
	 * AND A CHANGE OF FLOOR, since issue #2733 slice 3 (ruled 2026-11-18): a write earlier in the same frame gathered
	 * the floor before the change, and the one after it must replace it. Window travel-w1's first run found the walk
	 * an arrival begins skipped so, and the record on disk still saying the capital. Every place that raises it does
	 * so once per change of floor; `docs/DECISIONS.md`'s 2026-11-18 entry lists them.
	 *
	 * AND A RESPEC OF THE EMPIRE UPGRADE TREE, since issue #2741 slice 1b (ruled 2026-11-18): the days it cost passed in
	 * the run after any earlier write in its frame gathered it, and the one after must hold them.
	 */
	static bool WritesEvenIfTheRunWasWrittenThisFrame(ECataclysmSaveTrigger Trigger);

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
