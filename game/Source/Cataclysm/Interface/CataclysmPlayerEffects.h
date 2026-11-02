// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmPlayerEffects.generated.h"

class AActor;

/**
 * One thing affecting the player now, as the list above the player's bars shows it. Issue #1591.
 *
 * PLAIN DATA RATHER THAN TEXT, so the port to UMG (issue #650) can lay an entry out as a box with its own count and
 * clock without parsing words back apart. `UCataclysmPlayerEffects::TextFor` is the canvas's reading of it.
 */
struct FCataclysmPlayerEffect
{
	/**
	 * What it is called: "Bleed", "Knocked Down", "Forced March".
	 *
	 * FOR A THING THE LINE ABOVE THE BARS ALREADY NAMED BEFORE THIS LIST EXISTED, THE WHOLE OF THAT TEXT, as the
	 * `UCataclysmCombatOverlay` function that names it returns it: "Withering 3/10", "Commander 12s", "Owed 132".
	 * Those words, and the tests that pin them, did not change; the list carries them as they are.
	 */
	FString Name;

	/** Whether it harms the player. Debuffs are listed first, and drawn in red. */
	bool bDebuff = false;

	/** How many stacks are held, or 0 for a thing that does not stack or holds one. */
	int32 Stacks = 0;

	/** The most stacks it can hold, or 0 for no cap to show. Read only with `Stacks`. */
	int32 MaxStacks = 0;

	/** The seconds it has left, or -1 for a thing with no end time: a state the player is in until it changes. */
	float SecondsLeft = -1.0f;
};

/**
 * What is affecting the player now, for the list drawn above the player's own bars. Issue #1591.
 *
 * THE ONE PLACE THE HEADS-UP DISPLAY ASKS. Until this, `ACataclysmHUD::DrawPlayerVitals` called fourteen text functions
 * one by one, and anything not wired there -- a bleed, a burn, a knockdown, a pin, a stagger, every other named
 * debuff, every buff, Forced March's stacks -- was shown nowhere. The list is built here and the HUD draws it, the
 * split `UCataclysmSkillBar` and `ACataclysmHUD::DrawSkillBar` already use, so what the list says can be tested
 * without drawing anything: the automation suite runs with -nullrhi and `DrawHUD` never runs under it.
 *
 * WHAT IT LISTS, DEBUFFS FIRST AND THEN BUFFS (the order inside each is a judgement; `docs/DECISIONS.md` has it):
 * - every tag under the debuff roots, `UCataclysmDebuffs::TagsOn`: each damage over time, a stun, each named curse;
 * - the crowd control states that are not debuffs there: `State.KnockedDown`, `State.Pinned`, `State.Staggered`;
 * - every `Status.Buff` tag;
 * - every text the line above the bars showed before, by calling the same `UCataclysmCombatOverlay` function, so the
 *   words cannot drift apart: Feared, Maddened, Crippled, Weakened, Shredded, Stunned, Withering N/10, Withered,
 *   Touch of Nothing N, Negated, the held buff, the damage type immunities, the kept stacks and what is owed;
 * - Forced March's stacks and The Nihil's Embrace's loss and reward, read from the dungeon game mode.
 *
 * NOTHING IS LISTED TWICE. A tag one of those text functions names is not listed again under the tag's own name;
 * the text carries it, with the tag's seconds left added.
 *
 * NOT LISTED YET: every other floor rule that holds state on the game mode -- Death's Embrace's stacks, Contagious
 * Touch's, Wasting Sickness's, Mortal Decay's and the rest. Each needs its own reading, and is later work.
 */
UCLASS()
class CATACLYSM_API UCataclysmPlayerEffects : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Everything affecting `Actor` now, debuffs first. Empty for no actor, an actor with no ability system, and a dead
	 * one, which is what each of the text functions it calls answers for those.
	 */
	static TArray<FCataclysmPlayerEffect> EntriesFor(const AActor* Actor);

	/**
	 * One entry as a line of text: the name, then "3/5" or "x3" when it stacks, then "4s" when it is timed, so
	 * "Bleed 3s", "Forced March 4/5", "Withering 3/10".
	 *
	 * THE SECONDS ARE ROUNDED UP, as `UCataclysmCombatOverlay::HeldBuffTextFor`'s are: a thing in its last second reads
	 * "1s" and never "0s" while it is still there.
	 */
	static FString TextFor(const FCataclysmPlayerEffect& Entry);

	/**
	 * The most entries drawn. A judgement of the writing session, ruled with it on 2026-11-01: eight lines of text
	 * above four bars is as tall as the bottom left can take before the list reaches the middle of the screen.
	 */
	static constexpr int32 MostShown = 8;

	/**
	 * The entries as the screen shows them: the first `MostShown`, then one more line "+N more" when anything was
	 * left out. That line is a buff-coloured entry with no count and no clock, so `TextFor` reads it as its name.
	 */
	static TArray<FCataclysmPlayerEffect> ShownFrom(const TArray<FCataclysmPlayerEffect>& Entries);

	/** The ink a debuff's line is drawn in, as six-digit sRGB hex. A judgement: a red that reads on a dark floor. */
	static const TCHAR* DebuffInkHex;

	/** The ink a buff's line, and the "+N more" line, are drawn in. White, as every line above the bars was. */
	static const TCHAR* BuffInkHex;

	/**
	 * The name a tag is listed under: the ailment's own name where `UCataclysmAilments::Kinds` has the tag
	 * ("Void Splinter"), else the tag's last segment with a space before each capital that follows a lower-case
	 * letter ("Knocked Down", "Abyssal Aura").
	 */
	static FString NameForTag(const FGameplayTag& Tag);
};
