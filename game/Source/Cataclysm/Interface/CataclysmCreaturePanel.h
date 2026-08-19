// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Math/Box2D.h"
#include "CataclysmCreaturePanel.generated.h"

class AActor;
class UDataTable;

/**
 * Everything the panel at the top of the screen decides, and none of the
 * drawing.
 *
 * WHAT IT IS FOR. The word over a creature's head says WHICH creature in a pack
 * is worth looking at. It has no room to say WHAT that creature is, and the
 * thing a player most needs is exactly what will not fit: the design gives an
 * enemy one modifier per rung above Common, up to five for a Cataclysm Boss, and
 * those are mechanical effects -- a burning aura, a charm on being hit -- that
 * change how the creature must be fought. This panel is where they go. Issue
 * #740, and `docs/DECISIONS.md` carries the genre sources.
 *
 * NOTHING ASSIGNS A CREATURE A MODIFIER YET, so in play the modifier lines are
 * empty for every creature until one is typed into a placed enemy by hand.
 * That is issue #742, and it is stated here rather than discovered: a panel
 * that never shows the thing it was built for reads as broken.
 *
 * WHY IT IS A SEPARATE CLASS FROM UCataclysmCombatOverlay. That class is the
 * combat readability layer: what happens to a creature while it is being fought,
 * which is a bar, a number and a word. This is a description of one creature the
 * player deliberately pointed at, and it has its own switch, its own colours and
 * its own layout. Keeping them apart is what lets the panel be turned off
 * without losing the damage numbers, and the other way round.
 *
 * THE SPLIT BETWEEN JUDGEMENT AND DRAWING IS THE SAME ONE, and for the same
 * reason: the automation test command in `tools/unreal_build.py` passes -nullrhi
 * and AHUD::PostRender checks FApp::CanEverRender() before calling DrawHUD, so
 * no test in this project can watch anything reach the screen. Every judgement
 * here is therefore a static function over plain values, needing no world, no
 * canvas and no rendering device, and ACataclysmHUD::DrawCreaturePanel is
 * measurement and draw calls with no decisions in it.
 *
 * WHAT IS NOT DECIDED HERE. Which creature the cursor is over. That needs a
 * physics trace from a player controller and cannot be tested at all, so it is
 * ACataclysmHUD::CreatureUnderCursor and it is deliberately three lines long.
 */
UCLASS()
class CATACLYSM_API UCataclysmCreaturePanel : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Whether the panel is drawn at all. Its own switch, not the bars'. */
	static bool CreaturePanelEnabled();

	/**
	 * Whether a creature the cursor is over gets a panel.
	 *
	 * EVERY RUNG, INCLUDING COMMON, and that is the opposite of the rule the
	 * word over the head follows. `UCataclysmCombatOverlay::ShouldShowRarityNameFor`
	 * refuses a Common because a word over 60% of what spawns is a word over most
	 * of the screen. Nothing is cluttered by this: the player pointed at one
	 * creature, and "Common Brute" is the answer to the question they asked.
	 *
	 * NOTHING OVER A CORPSE, the same rule and the same reason
	 * `UCataclysmCombatOverlay::ShouldShowBarFor` gives: an enemy destroys itself
	 * on the tick after it dies, so without this the panel would flash for one
	 * frame at the end of every fight.
	 *
	 * NOT THE PLAYER'S OWN PAWN. Their health is already on the frame, and
	 * `IsOverheadBarCandidate` is what answers that so there is one rule for it
	 * rather than two.
	 */
	static bool ShouldShowFor(const AActor* Creature,
							  const AActor* LocalPlayerPawn, float Health,
							  float MaxHealth);

	/** The enemy archetype table, loaded once. Null when it cannot be read. */
	static const UDataTable* LoadEnemyArchetypeTable();

	/** The enemy modifier table, loaded once. Null when it cannot be read. */
	static const UDataTable* LoadEnemyModifierTable();

	/**
	 * What an archetype row is called, as a person reads it.
	 *
	 * FROM THE TABLE RATHER THAN FROM THE CLASS NAME. `game/Data/EnemyArchetypes.csv`
	 * carries "Abyssal Warden" for the row `Abyssal_Warden`, and it is generated
	 * from the design workbook, so a creature renamed in the design is renamed on
	 * screen without anybody editing C++.
	 *
	 * @return an empty string for a row the table does not hold, and for a
	 *         creature that names no row at all
	 */
	static FString ArchetypeNameForRow(const UDataTable* EnemyArchetypeTable,
									   FName Row);

	/**
	 * What a modifier row is called. The name only, not its description.
	 *
	 * THE NAME AND NOT THE SENTENCE. `game/Data/EnemyModifiers.csv` carries a
	 * full description for each -- "Emits a burning aura that deals constant fire
	 * damage to nearby players" -- and five of those at the top of the screen
	 * during a fight is a wall of text over the thing the player is fighting.
	 * Path of Exile shows short modifier names for the same reason and its
	 * players learn them. The descriptions stay in the table for a fuller view
	 * later; `docs/DECISIONS.md` records the trade.
	 *
	 * @return an empty string for a row the table does not hold
	 */
	static FString ModifierNameForRow(const UDataTable* EnemyModifierTable,
									  FName Row);

	/**
	 * Every named modifier a creature carries, in the order it carries them.
	 *
	 * A ROW THE TABLE DOES NOT HOLD IS DROPPED rather than drawn as a blank
	 * line. A blank line in the middle of a panel reads as a fault in the panel;
	 * a missing line reads as a creature with fewer modifiers, which is at least
	 * true of what could be found.
	 */
	static void ModifierNamesFor(const UDataTable* EnemyModifierTable,
								 const TArray<FName>& Rows,
								 TArray<FString>& OutNames);

	/**
	 * The panel's first line: which rung, then which creature.
	 *
	 * ONE LINE RATHER THAN TWO, because "Elite Brute" is how a player says it and
	 * two lines would put the rarity and the name at different sizes for no
	 * reason. The rarity comes first for the same reason it does in speech.
	 *
	 * A CREATURE WITH NO ARCHETYPE STILL GETS A TITLE. The sandbox's training
	 * dummies are the base enemy class with no archetype row, and issue #39 is
	 * what gives every creature one. Until then they are named by
	 * UnnamedCreature rather than leaving the panel headed by a rarity on its
	 * own, which would read as the name having gone missing.
	 */
	static FString TitleFor(const FString& ArchetypeName,
							const FString& RarityName);

	/**
	 * The health figures, as they are printed on the bar.
	 *
	 * A LIVING CREATURE NEVER READS ZERO. Health is an unrounded float that is
	 * only clamped, so a creature sitting on 0.3 health is alive and hittable,
	 * and rounding alone would print "0 / 250" for it -- which is the one thing
	 * a health readout must not say about something that is still standing. This
	 * is the same rule and the same reason `UCataclysmCombatOverlay::FigureFor`
	 * exists for a damage number.
	 *
	 * @return an empty string when the creature has no health pool at all, which
	 *         is every creature before its ability system has been initialised
	 */
	static FString HealthTextFor(float Health, float MaxHealth);

	/**
	 * Where the whole panel goes, given how much is inside it.
	 *
	 * THE TOP CENTRE OF THE SCREEN. That is where the project owner asked for
	 * it, and it is where Last Epoch puts the same information. It is also the
	 * one part of the screen that is free: the player's own bars are in the
	 * bottom left corner and the creature being described is under the cursor,
	 * which is nowhere near the top edge in a top-down game.
	 *
	 * A FLOOR ON THE WIDTH, WHICH IS WHAT STOPS IT JITTERING. Health is redrawn
	 * every frame and its figures change width as the digits do, so a panel
	 * sized only to its contents would breathe in and out through a fight. Below
	 * the floor the panel is a fixed size; above it, a long modifier name still
	 * fits rather than being cut off.
	 *
	 * @param Viewport       the whole drawable area, in pixels
	 * @param ContentWidth   the widest thing that has to fit, without padding
	 * @param ContentHeight  everything stacked up, without padding
	 */
	static FBox2D PanelBoxFor(const FVector2D& Viewport, float ContentWidth,
							  float ContentHeight);

	//~ Layout, in pixels at an unscaled viewport. None of this can be seen by a
	//~ test; every figure is a starting point and only play settles it.

	/** Clear space between the top of the screen and the panel. */
	static constexpr float TopMarginPx = 24.0f;

	/** Clear space between the panel's edge and everything inside it. */
	static constexpr float PaddingPx = 14.0f;

	/** Clear space between one line inside the panel and the next. */
	static constexpr float LineGapPx = 6.0f;

	/**
	 * The narrowest the panel is drawn, as a share of the viewport's width.
	 *
	 * 0.28 IS 538 PIXELS AT 1920 WIDE, which holds "Cataclysm Boss Abyssal
	 * Warden" and every modifier name in `game/Data/EnemyModifiers.csv` without
	 * the panel having to grow for them.
	 */
	static constexpr float MinimumWidthShare = 0.28f;

	/** How tall the health bar inside the panel is. */
	static constexpr float HealthBarHeightPx = 16.0f;

	/**
	 * How thick the outline around that bar is.
	 *
	 * THE BAR HAS AN OUTLINE RATHER THAN A TRACK BEHIND IT, and that is a
	 * measurement rather than a preference. A bar drawn the ordinary way -- a
	 * grey track with a red fill over part of it -- needs three colours to be
	 * separable from each other, and there is no grey that works: to clear 3:1
	 * against this panel a track has to reach about 14% relative luminance, and
	 * the health red is already at 14.3%. `#606B78` measures 3.21:1 against the
	 * panel and **1.00:1 against the health fill**, so the filled part and the
	 * empty part would be one flat block to anybody who cannot separate the
	 * hues. An outline avoids the three-way problem: the bar's full extent is
	 * shown by the line, and the filled share is red against the panel itself,
	 * which measures 3.20:1.
	 */
	static constexpr float BarOutlinePx = 1.0f;

	/** The title's size, relative to a damage number. */
	static constexpr float TitleScale = 1.0f;

	/** A modifier line's size, relative to a damage number. Quieter than the
	 *  title, because the title is what the player looked for. */
	static constexpr float LineScale = 0.7f;

	/** How thick the panel's own edge is. */
	static constexpr float EdgePx = 2.0f;

	//~ Colours, as six-digit sRGB hex, the same form UCataclysmCombatOverlay and
	//~ UCataclysmInventoryScreen use.

	/**
	 * The panel behind the text.
	 *
	 * THE SAME NEAR-BLACK THE INVENTORY SCREEN USES, so the game has one panel
	 * rather than two that nearly match. It is a separate constant because the
	 * two panels differ in the thing that matters here -- how much of the world
	 * they hide -- and sharing the value is not a reason to share the decision.
	 */
	static const TCHAR* PanelHex;

	/**
	 * How much of the world the panel hides.
	 *
	 * LOWER THAN THE INVENTORY SCREEN'S 0.94, WHICH IS THE WHOLE DIFFERENCE
	 * BETWEEN THEM. That screen stops the player looking at the dungeon. This
	 * one is read while the fight is still happening, at the top of the screen,
	 * where something can walk into the strip it covers. Ten per cent of the
	 * world showing through is enough to see that happen and not enough to hurt
	 * the text: `tools/tests/test_the_creature_panel_is_readable.py` measures the
	 * ink against this panel composited over the brightest world surface the
	 * design allows, rather than against the panel colour on its own.
	 */
	static constexpr float PanelOpacity = 0.90f;

	/**
	 * The panel's own edge, and the outline around the health bar inside it.
	 *
	 * THE EDGE IS THE ONLY THING THAT MAKES THE PANEL A SHAPE, and that is a
	 * measured claim rather than a stylistic one. The design caps a world
	 * surface at 30% brightness, and this panel over a surface at that cap
	 * measures **1.86:1** -- so on a pale stone floor the fill alone is very
	 * nearly invisible, however opaque it is made. Nothing about the opacity
	 * fixes that; the panel is near-black on purpose, because that is what keeps
	 * the text readable.
	 *
	 * SO THIS COLOUR HAS TO CLEAR 3:1 AGAINST BOTH SIDES OF ITSELF: against the
	 * panel within, and against the brightest floor the design permits without.
	 * It measures 5.98:1 and 3.21:1. That is what forced it lighter than the
	 * inventory screen's edge, which only ever has to be seen against its own
	 * panel and sits at 2.43:1.
	 *
	 * `tools/tests/test_the_creature_panel_is_readable.py` measures all of it.
	 */
	static const TCHAR* EdgeHex;

	/**
	 * The colour every piece of text on the panel is drawn in.
	 *
	 * ONE INK FOR THE WHOLE PANEL, which is the answer issue #734 reached for
	 * the inventory screen: one colour is one contrast ratio to keep above the
	 * threshold rather than one per rung. Measured at 13.75:1 against this panel
	 * over the brightest world surface the design allows.
	 */
	static const TCHAR* InkHex;

	/**
	 * What a creature with no archetype is called.
	 *
	 * IT IS NOT A GUESS AT THE CREATURE'S NAME. Nothing in the game reads
	 * `game/Data/EnemyArchetypes.csv` to build a creature yet -- that is issue
	 * #39 -- so the two enemies that exist name their own row and the sandbox's
	 * training dummies, which are not an archetype at all, name none. This is
	 * what the panel says for those.
	 */
	static const TCHAR* UnnamedCreature;
};
