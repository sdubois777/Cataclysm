// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "InputCoreTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmSkillBar.generated.h"

/**
 * What one box of the skill bar shows.
 *
 * READ FROM THE CHARACTER, NOT AUTHORED. Every field here is answered by the
 * ability system: which ability is granted into the slot, what it costs, whether
 * it is waiting to be used again. Nothing about a slot is configured anywhere.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmSkillBarSlot
{
	GENERATED_BODY()

	/** Which of the player's slots this box is. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Interface")
	ECataclysmAbilitySlot Slot = ECataclysmAbilitySlot::None;

	/**
	 * What to write in the box.
	 *
	 * The granted skill's own name when it has one, and the slot's name when it
	 * does not -- a box reading "Special" is more use than an empty box, because
	 * it says the slot exists and is empty rather than leaving the player to
	 * wonder whether the bar is broken.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Interface")
	FString Name;

	/**
	 * The key that fires it, short enough to fit in the box.
	 *
	 * READ FROM THE KEYS ACTUALLY BOUND rather than written down here, and that
	 * is not fussiness. The Support slot is on **W** under mouse movement and on
	 * **1** under keyboard movement, because keyboard movement needs W for
	 * walking forward -- `tools/generate_input_assets.py` says so where it builds
	 * the two mapping contexts. A label written into the interface would be wrong
	 * for half of the players.
	 *
	 * Empty when nothing is bound, or when there is no local player to ask.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Interface")
	FString Key;

	/** Whether an ability is granted into this slot at all. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Interface")
	bool bFilled = false;

	/** Seconds until it can be used again. Zero when it is ready. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Interface")
	float CooldownRemaining = 0.0f;

	/**
	 * How long the wait was in total, so the sweep across the box can show how
	 * much of it is done rather than only how much is left.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Interface")
	float CooldownDuration = 0.0f;

	/**
	 * Uses held now and the most it can hold. Issue #1833, skill charges. A
	 * skill with a second charge can be used while its cooldown runs, so the
	 * sweep alone would tell the player a usable skill was waiting.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Interface")
	int32 Charges = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Interface")
	int32 MaxCharges = 1;

	/** Mana one use costs this character, at their level. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Interface")
	float ManaCost = 0.0f;

	/**
	 * Whether the character has the mana for it.
	 *
	 * THIS IS THE ONE THAT ALREADY COST SOMEBODY AN EVENING. Issue #653 was
	 * reported as "sometimes all of my abilities just become disabled", and what
	 * was happening was an empty mana pool with nothing on screen saying so. The
	 * mana bar was added for that. A skill bar that did not also show which
	 * skills are unaffordable would put the player back to guessing which of
	 * their six is the one they cannot pay for.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Interface")
	bool bAffordable = true;

	/**
	 * Whether a lock is refusing this skill right now.
	 *
	 * THE SECOND REASON A BOX CANNOT BE USED, and it is the one the player had no
	 * way of learning. Issue #1810. `UCataclysmSkillTemplate::CanActivateAbility`
	 * refuses every slot but the basic attack while `UCataclysmSkillSlots::
	 * LockedStat` is above zero, and returns before the engine's own checks on
	 * purpose so the player is not told the wrong reason. Nothing put the right
	 * one in its place, so the outcome was a key that did nothing.
	 *
	 * READ PER BOX AND NOT ONCE FOR THE BAR, which is the difference between this
	 * and `bAffordable` above. Mana is one number for the character, so every box
	 * asks the same question; a lock is scoped by the asking skill's own tags, so
	 * an enchantment that locks only the Movement slot and a dungeon rule that
	 * locks everything have to look different. `UCataclysmSkillBar::Read` passes
	 * each skill's `SkillTags`, the same container the refusal passes.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Interface")
	bool bLocked = false;
};

/**
 * The player's skill bar: which abilities are in which slots, and whether each
 * one can be used right now.
 *
 * WHY THIS IS A SEPARATE CLASS FROM THE THING THAT DRAWS IT. `ACataclysmHUD`
 * draws with the canvas, and `DrawHUD` never runs under test: the automation
 * command passes `-nullrhi`, so there is no canvas at all. Every decision that
 * leads up to the drawing therefore lives here, as functions a headless test can
 * call. `UCataclysmCombatOverlay` next door is split the same way and says the
 * same thing; this is the skill bar's half of that arrangement.
 *
 * WHAT WAS MISSING BEFORE IT. The heads-up display drew health, mana and energy
 * shield and nothing else. Six abilities were granted by the equipped weapon,
 * each with a cooldown and a mana cost, and **nothing on screen said which of
 * them could be used**. Issue #49 lists "active skill slots with cooldown
 * indicators" as part of the heads-up display; this is that part.
 */
UCLASS()
class CATACLYSM_API UCataclysmSkillBar : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ----------------------------------------------------------------------
	// Which slots the bar shows
	// ----------------------------------------------------------------------

	/**
	 * The slots that get a box, in the order they are drawn.
	 *
	 * SIX OF THE SEVEN, AND THE BASIC ATTACK IS THE ONE LEFT OUT. It has no key,
	 * because the design makes it automatic -- "There is no button to press and
	 * no rotation to perform" -- and it has no cooldown, because attack speed
	 * sets its rate instead. `UCataclysmSkillSlots::CooldownTag` returns an
	 * invalid tag for it and says the same. A box for it would show a skill the
	 * player cannot press and a wait that never happens.
	 *
	 * THE ORDER IS THE DESIGN DOCUMENT'S ORDER, which is also the order the keys
	 * sit in on a keyboard: Heavy on the right mouse button, then Special,
	 * Support, Aura and Ultimate across the row, then Movement.
	 */
	static TArray<ECataclysmAbilitySlot> SlotsShown();

	// ----------------------------------------------------------------------
	// Where the boxes go
	// ----------------------------------------------------------------------

	/** How wide and tall one box is, in pixels. */
	static constexpr float BoxSizePx = 56.0f;

	/** How much space between two boxes, in pixels. */
	static constexpr float BoxGapPx = 8.0f;

	/** How far the bottom of the bar sits above the bottom of the screen. */
	static constexpr float BottomMarginPx = 26.0f;

	/**
	 * The narrowest viewport the layout is checked against.
	 *
	 * NOT A LIMIT THE CODE ENFORCES. It is the width the test uses when it
	 * checks that the bar does not run into the health and mana bars in the
	 * bottom left corner, and it is here so that number lives beside the layout
	 * it constrains rather than only inside a test.
	 */
	static constexpr float NarrowestCheckedViewportPx = 1024.0f;

	/**
	 * The top left corner of one box.
	 *
	 * THE BAR IS CENTRED ALONG THE BOTTOM, which is where every game in this
	 * genre puts it and, more usefully, is the one part of the screen the
	 * existing heads-up display does not already use: the health, mana and
	 * energy shield bars stack upward from the bottom LEFT corner.
	 *
	 * @param Index          which box, counted from 0
	 * @param Count          how many boxes there are altogether
	 * @param ViewportWidth  how wide the screen is, in pixels
	 * @param ViewportHeight how tall
	 */
	static FVector2D BoxOriginFor(int32 Index, int32 Count,
								  float ViewportWidth, float ViewportHeight);

	/** How wide the whole bar is, in pixels, for a given number of boxes. */
	static float BarWidthFor(int32 Count);

	// ----------------------------------------------------------------------
	// What a box says
	// ----------------------------------------------------------------------

	/**
	 * How much of the wait is still to go, from 1 at the moment it starts to 0
	 * when it is over.
	 *
	 * DRAWN AS A DARK SWEEP OVER THE BOX, so the box uncovers itself as the wait
	 * runs down. A duration of zero or less answers 0, because a wait with no
	 * length is a wait that is over.
	 */
	static float CooldownFractionFor(float Remaining, float Duration);

	/**
	 * The wait written as a number, or nothing at all when there is no wait.
	 *
	 * ONE DECIMAL BELOW TEN SECONDS AND WHOLE SECONDS ABOVE IT. A judgement, and
	 * the reason is that a whole number spends its last second reading "1" and
	 * then jumps to nothing, so the player cannot tell a wait that is nearly done
	 * from one that has just under a second to go. Above ten seconds the tenths
	 * are noise and the extra character does not fit the box.
	 */
	static FString CooldownTextFor(float Remaining);

	/**
	 * The uses held, written for the corner of the box, or nothing for a skill
	 * that can hold only one. Issue #1833, skill charges. "x2" rather than
	 * "2/3", because the held count is what decides whether pressing the key
	 * does anything, and the box is small.
	 */
	static FString ChargesTextFor(int32 Held, int32 Maximum);

	/**
	 * Whether this character can pay this cost from `Pool`, by the rule the cast
	 * itself uses. Issue #1910.
	 *
	 * `Pool` IS `UCataclysmGameplayAbility::CostPool`, asked once for the whole
	 * bar: health for a character whose mana pool became health, mana for
	 * everyone else. Comparing raw mana drew every skill with a cost as
	 * unaffordable for a Masochist holding Water to Blood, who has no mana and
	 * casts from health.
	 *
	 * `UCataclysmGameplayAbility::PoolCovers` DECIDES IT, so equal is enough for
	 * mana and strictly more is needed for health, exactly as `CheckCost` asks.
	 * A bar that greyed out a skill the character could actually cast would be
	 * worse than no bar.
	 *
	 * A COST OF NOTHING IS ALWAYS PAYABLE, as `CheckCost` returns early for it.
	 *
	 * AND SO IS ANY COST WHILE THERE IS NO POOL TO READ. For some frames after a
	 * pawn appears it has no ability system, or one without the attribute set
	 * holding the pool, and a pool that cannot be read reads as zero. Greying
	 * out every skill for those frames would look like the fault issue #653 was
	 * reported as.
	 */
	static bool CanAfford(const UAbilitySystemComponent* AbilitySystem,
						  const FGameplayAttribute& Pool, float ManaCost);

	/**
	 * Whether a skill that answered this lock value is refused.
	 *
	 * ABOVE ZERO, WHICH IS THE WHOLE TEST, and it is deliberately the same
	 * comparison `UCataclysmSkillTemplate::CanActivateAbility` makes rather than
	 * a second opinion about it. A bar that decided this differently from the
	 * refusal would tell the player something the game does not do.
	 */
	static bool IsLocked(float LockValue);

	/**
	 * Whether to say in words that every skill is locked.
	 *
	 * ONLY WHEN EVERY BOX HOLDING A SKILL IS LOCKED, never when some are. Issue
	 * #1810. A single-slot lock is the bar's business: `game/Data/
	 * EnchantmentEffects.csv` holds two rows that lock one slot under a condition
	 * the player sets off themselves, and a line of text across the screen for
	 * one greyed box would be noise. The case this exists for is the dungeon rule
	 * `Celestial_Edict_of_Silence`, which locks every slot for fifteen seconds on
	 * a clock the player does not control, and which reads as the game having
	 * stopped working rather than as a rule doing what its row says.
	 *
	 * AN EMPTY BOX IS NOT A LOCKED ONE AND IS NOT COUNTED EITHER. It holds no
	 * skill, so there is nothing to refuse; counting it as unlocked would silence
	 * this for any character with a slot spare, and counting it as locked would
	 * say every skill is locked to a character who has none. So the question is
	 * asked of the boxes that hold a skill, and answered false when there are
	 * none.
	 */
	static bool EverySkillIsLocked(const TArray<FCataclysmSkillBarSlot>& Slots);

	/**
	 * The words shown while that is true.
	 *
	 * IT MUST NOT SAY THE PLAYER CANNOT ACT, because that is false and the design
	 * says so. `UCataclysmSkillTemplate::CanActivateAbility` exempts the basic
	 * attack slot unconditionally, and `Celestial_Edict_of_Silence`'s row is
	 * "Only basic attacks function during this period". A player told they can do
	 * nothing would stop trying the one thing that still works.
	 *
	 * AND IT SAYS NO DURATION. The lock is a stat value, not a time:
	 * `UCataclysmAbilitySystemComponent::StatForSkill` answers how much, not how
	 * long, so a countdown here would be invented rather than read.
	 */
	static FString LockedNotice();

	/**
	 * The line above the bar naming the next-use charges held, or empty when
	 * none are. Issue #1833, phase 2: the owner's rule that every system ships
	 * with a basic interface, so a held charge can be seen in play.
	 *
	 * ONE ENTRY PER KIND, with what the held charges are worth together and,
	 * when more than one is held, how many: "Next skill +60%   Next attack
	 * +40% (2)". Whole percentages, because an enchantment rolls anywhere in
	 * its range and a decimal would be noise on a line read mid-fight.
	 *
	 * PURE, so a test can read the exact words the HUD draws.
	 */
	//
	// AND, SINCE ISSUE #1833'S TIMED GRANTS, an effectiveness charge ("Next
	// skill 300% effectiveness") and one entry per enchantment holding own
	// stacks, each already worded by `OwnStacksEntry`. The seven own-stack
	// enchantments of #2083 shipped with nothing on screen; this is where they
	// show.
	static FString NextUseLine(float SkillPercent, int32 SkillCount,
							  float AttackPercent, int32 AttackCount,
							  float EffectivenessPercent = 0.0f,
							  const TArray<FString>& OwnStacks = TArray<FString>());

	/**
	 * One enchantment's own stacks as the line names them: its stats and the
	 * count against the cap, "armor 2/10". Issue #1833.
	 *
	 * NAMED BY THE STATS, not by the enchantment. Its row name is a key cut
	 * from its sentence ("Positive_Every_10_seconds_gain_a_stack_of_momentum_
	 * granti") and not readable. Attack and spell damage together, which is
	 * how every damage sentence is written, read "attack/spell damage"; any
	 * other stats are joined with a slash, underscores read as spaces.
	 *
	 * A COUNT OF HITS IN A ROW ON ONE ENEMY says so after the count, "attack/
	 * spell damage 3/8 (hits in a row)". Issue #1833, phase 2: without it the
	 * entry would read exactly as a timed stack of the same stats does.
	 */
	static FString OwnStacksEntry(const TArray<FName>& Stats, int32 Held, int32 Cap,
								  bool bConsecutiveHits = false);

	/**
	 * One worn "every Nth" row's count against N, as the line names it: "Hit
	 * taken 4/5", "Spell 2/3" or "Attack 9/10". Issue #1833, phase 2: so the
	 * player sees the Nth coming. Empty for a row of no kind.
	 */
	static FString NthEntry(ECataclysmEveryNth Kind, int32 Count, int32 EveryNth);

	/**
	 * What Nothing Wasted holds for the next melee blow, such as "Next melee
	 * +340", or empty while nothing is stored. Issue #1515. Joined to the line
	 * above by the same three spaces, so the two never draw over each other.
	 * A store below one point says 1, since "+0" would read as nothing held.
	 */
	static FString StoredDamageLine(float Stored);

	/**
	 * What the held next-spell cooldown charges take off the next spell's
	 * cooldown, such as "Next spell cooldown -1.5s", or empty while none is
	 * held. Issue #1833, the cooldown reduction action. One decimal place,
	 * because the rows roll in tenths of a second. Joined to the line above by
	 * the same three spaces.
	 */
	static FString NextSpellCooldownLine(float Seconds);

	/**
	 * How long until Follow Through can repeat an attack again, such as "Follow
	 * Through 2s", or empty when it can now. Issue #1515, approved 2026-09-24.
	 * Whole seconds rounded up, so the last fraction of a second still says 1
	 * rather than 0. Joined to the line above by the same three spaces.
	 */
	static FString FollowThroughLine(float SecondsLeft);

	/**
	 * A key written short enough to fit in a box.
	 *
	 * `FKey` CARRIES TWO NAMES AND THIS TAKES THE SHORT ONE. The long name is
	 * "Right Mouse Button", "Space Bar" and "One"; the short one is "RMB",
	 * "Space" and "1", and only the second kind fits a 56 pixel box.
	 *
	 * A HAND-WRITTEN TABLE OF SPELLINGS WAS TRIED FIRST AND DELETED, because
	 * breaking it changed no test: the engine's short names already were the
	 * spellings this interface wanted.
	 */
	static FString KeyTextFor(const FKey& Key);

	/** What a slot with nothing granted into it is called. */
	static FString NameForEmptySlot(ECataclysmAbilitySlot Slot);

	/**
	 * The most characters of a name that fit under one box.
	 *
	 * ELEVEN, WORKED OUT FROM THE BOX AND NOT CHOSEN. A box is 56 pixels wide
	 * and the next one starts 64 pixels along, and the name is drawn at a scale
	 * where a character is about five pixels wide, so about twelve fit in the
	 * pitch before two names touch. Eleven leaves a gap.
	 *
	 * IT MATTERS BECAUSE DESIGNED NAMES ARE LONG. The weapon skill matrix holds
	 * names such as "Devastating Cleave", which is eighteen characters and would
	 * run under the boxes on either side of its own.
	 */
	static constexpr int32 MostNameCharacters = 11;

	/**
	 * A name shortened to fit under a box.
	 *
	 * CUT AND MARKED, rather than cut silently. A name that stops mid-word with
	 * nothing to say so reads as the skill's actual name, and a player would
	 * have no way to tell "Devastati" from a skill really called that.
	 */
	static FString ShortNameFor(const FString& Name);

	// ----------------------------------------------------------------------
	// Colours
	// ----------------------------------------------------------------------
	//
	// HEXADECIMAL STRINGS, matching `UCataclysmCombatOverlay`'s colours and for
	// the reason recorded there: `FColor::FromHex` is what parses them, so the
	// value in the source is the value a designer would type.

	/** A box holding a skill that can be used right now. */
	static const TCHAR* ReadyHex;

	/** The sweep drawn over a box that is waiting to be used again. */
	static const TCHAR* CoolingHex;

	/** A box holding a skill the character cannot pay for. */
	static const TCHAR* UnaffordableHex;

	/** A box holding a skill a lock is refusing. */
	static const TCHAR* LockedHex;

	/** The words saying every skill is locked. Read against a dark box. */
	static const TCHAR* LockedNoticeInkHex;

	/** A box with nothing granted into it. */
	static const TCHAR* EmptyHex;

	/** The outline every box is drawn inside. */
	static const TCHAR* BoxEdgeHex;

	/** What colour a box is drawn, given what is in it. */
	static FLinearColor TintFor(const FCataclysmSkillBarSlot& Slot);

	// ----------------------------------------------------------------------
	// Whether it is shown at all
	// ----------------------------------------------------------------------

	/**
	 * Whether the skill bar is drawn.
	 *
	 * A CONSOLE VARIABLE, the same as the damage numbers, the overhead bars and
	 * the player's own vitals, so a person judging one part of the interface can
	 * turn the others off. `Cataclysm.ShowSkillBar`.
	 */
	static bool Enabled();

	// ----------------------------------------------------------------------
	// Reading the character
	// ----------------------------------------------------------------------

	/**
	 * What the bar should show for this character, one entry per slot.
	 *
	 * ALWAYS ONE ENTRY PER SLOT IN `SlotsShown`, even when nothing is granted
	 * into it, so the bar does not change width as a weapon is swapped and the
	 * key under a given box never moves.
	 *
	 * @param Player the pawn whose bar this is. Null answers an empty list.
	 */
	static TArray<FCataclysmSkillBarSlot> Read(const AActor* Player);
};
