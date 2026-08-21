// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
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
	 * Whether a character with this much mana can pay this cost.
	 *
	 * EQUAL IS ENOUGH. Spending exactly what is left is a use the design allows,
	 * and a bar that greyed out a skill the character could actually cast would
	 * be worse than no bar.
	 */
	static bool CanAfford(float ManaCost, float Mana);

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
