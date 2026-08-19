// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Math/Box2D.h"
#include "CataclysmHUD.generated.h"

class ACataclysmDroppedItem;
class ACataclysmEnemyCharacter;
class UFont;

/**
 * Everything the player can see about a fight while it is happening.
 *
 * THREE THINGS AND NOTHING ELSE, which is the whole of issue #518: a bar over
 * every creature that has been hurt, a number where each blow lands, and the
 * player's own health in the corner. The designed heads-up display is larger
 * than this -- an empire status bar, skill slots with cooldowns, a minimap --
 * and it is issue #49. This is not a first instalment of that; it is the
 * smallest thing that makes a combat figure judgeable by playing it, which is
 * how this project settles combat constants.
 *
 * IT DRAWS ON THE CANVAS RATHER THAN THROUGH UMG, and the reasoning is on
 * UCataclysmCombatOverlay. In short: no module dependency, no widget Blueprint
 * and no content asset of any kind, at the cost of no layout and no
 * localisation. Issue #650 is the port.
 *
 * NOTHING HERE DECIDES ANYTHING. Every judgement -- whether a bar draws, what a
 * number says, what colour, how far above a head, how fast it fades -- is a
 * static function on UCataclysmCombatOverlay, because no automation test can
 * watch anything reach the screen. AHUD::PostRender checks FApp::CanEverRender()
 * before it calls DrawHUD, and the test command in tools/unreal_build.py passes
 * -nullrhi, so DrawHUD never runs under test at all. The same wall issue #559
 * records for the impact particle. Keeping this class to arithmetic and draw
 * calls is what leaves the rest covered.
 *
 * bShowHUD, which AHUD already has, is what the design's Heretic lethality mode
 * needs: it hides the heads-up display entirely, and Hardcore shows the map
 * overlay only. Neither mode exists yet.
 */
UCLASS()
class CATACLYSM_API ACataclysmHUD : public AHUD
{
	GENERATED_BODY()

public:
	ACataclysmHUD();

	virtual void DrawHUD() override;

	/**
	 * Takes a number to draw. Called from the attribute set that resolved the
	 * hit, through UCataclysmCombatOverlay::Record.
	 *
	 * DROPS THE OLDEST WHEN FULL rather than refusing the newest. An area skill
	 * landing on twenty enemies produces twenty numbers in one frame, and the
	 * ones worth keeping are the ones that just arrived.
	 */
	void AddDamageNumber(const FCataclysmDamageNumber& Number);

	/** How many numbers are waiting to be drawn. For tests. */
	int32 NumbersWaiting() const { return Numbers.Num(); }

	/**
	 * The numbers waiting to be drawn, oldest first. For tests.
	 *
	 * COUNTING THEM IS NOT ENOUGH TO CHECK THE CEILING. Whether the oldest or
	 * the newest are dropped is the whole of the rule, and both answers leave
	 * the same count behind.
	 */
	const TArray<FCataclysmDamageNumber>& NumbersWaitingList() const
	{
		return Numbers;
	}

	/** Removes every number that has outlived its lifetime, given the time now. */
	void DropExpired(float WorldSeconds);

	/**
	 * The drop whose drawn name covers this point on screen, or nullptr.
	 *
	 * WHY THE HEADS-UP DISPLAY ANSWERS THIS. The name is what a player
	 * clicks, and where a name IS on screen is only known by whatever
	 * drew it: it depends on the projection, the font, the text scale and
	 * the length of the item's own name. So the draw records the rectangle
	 * it filled and this reads it back.
	 *
	 * THE RECTANGLES ARE ONE FRAME OLD AT MOST. Input is processed before
	 * the frame is drawn, so a click is tested against where the names were
	 * last drawn. A drop does not move, and the camera cannot travel far in
	 * a frame, so the error is smaller than the name itself.
	 *
	 * EMPTY WHEN NOTHING HAS BEEN DRAWN, which is every automation test:
	 * AHUD::PostRender checks FApp::CanEverRender() and the test command
	 * passes -nullrhi, so DrawHUD never runs. The judgement this makes is
	 * therefore tested through UCataclysmDropPickup::IndexOfNameUnderPoint
	 * rather than through this.
	 */
	ACataclysmDroppedItem* DropUnderPoint(const FVector2D& Point) const;

private:
	/**
	 * The player's own health and mana, and its energy shield when it has one.
	 */
	void DrawPlayerVitals();

	/** One of the player's own pools: a bar in the corner, with its figures. */
	void DrawPlayerPool(float Top, float Current, float Maximum,
						const TCHAR* FillHex);

	/** A bar over every creature that has been hurt and is not the player. */
	void DrawOverheadBars();

	/**
	 * The rarity of every enemy that is not Common, said in a word over it.
	 *
	 * ITS OWN PASS RATHER THAN A LINE INSIDE DrawOverheadBars, because the two
	 * answer different questions and appear at different times. A bar says how a
	 * fight is going and waits for the creature to be hurt; a word says whether
	 * to start one and has to be there before anything is hit. Sharing a loop
	 * would mean sharing a condition, and the whole point is that they differ.
	 *
	 * A SECOND WALK OVER THE CREATURES IN THE WORLD, and that is affordable: a
	 * dungeon floor holds tens of them, not thousands, and the same walk already
	 * happens for the bars and again for the drop names.
	 *
	 * NOTHING HERE DECIDES ANYTHING. Whether a word is drawn at all is
	 * UCataclysmCombatOverlay::ShouldShowRarityNameFor and what it says is
	 * UCataclysmEnemyRarity::RarityNameForStep, for the reason this class's
	 * header gives.
	 */
	void DrawRarityNames();

	/**
	 * The panel at the top of the screen describing the creature under the
	 * cursor.
	 *
	 * WHAT IT IS FOR, WHICH IS NOT WHAT THE WORD OVER THE HEAD IS FOR. The word
	 * says which creature in a pack to look at. This says what that one
	 * creature is, and it is the only place a creature's MODIFIERS can ever
	 * appear -- one per rung above Common, five for a Cataclysm Boss, and they
	 * are mechanical effects that change how it has to be fought. Issue #740.
	 *
	 * NOTHING HERE DECIDES ANYTHING, for the reason this class's header gives.
	 * Whether a panel is drawn is UCataclysmCreaturePanel::ShouldShowFor, what
	 * its first line says is UCataclysmCreaturePanel::TitleFor, and where it
	 * goes is UCataclysmCreaturePanel::PanelBoxFor. This measures, lays out and
	 * draws.
	 *
	 * MEASURED IN FULL BEFORE ANYTHING IS DRAWN, the same two-pass shape
	 * DrawDropNames uses and for a sharper version of its reason: the panel is
	 * as wide as the widest line inside it, so the first line cannot be printed
	 * until the last one has been measured.
	 */
	void DrawCreaturePanel();

	/**
	 * The creature the cursor is pointing at, or nullptr.
	 *
	 * AN OBJECT QUERY FOR PAWNS, NOT A TRACE ON THE VISIBILITY CHANNEL, and
	 * that is the one thing about this worth knowing. The engine's stock `Pawn`
	 * collision profile sets `Visibility` to Ignore and its `CharacterMesh`
	 * profile does the same, so the visibility trace
	 * ACataclysmPlayerController::UpdateCachedDestination uses passes straight
	 * through every creature in the game and lands on the floor behind it --
	 * which is exactly why clicking an enemy walks to it rather than doing
	 * something else. Asking for Pawn OBJECTS instead is what finds the
	 * creature, and it is the same channel UCataclysmTargeting already uses to
	 * find things to hit.
	 *
	 * A CURSOR OVER AN OPEN SCREEN POINTS AT NOTHING. Without that test a
	 * cursor resting on an inventory cell would describe whatever creature
	 * happens to stand behind the panel, which is the same fault issue #731
	 * fixed for clicks.
	 *
	 * NOT TESTABLE, AND DELIBERATELY SHORT BECAUSE OF IT. It needs a player
	 * controller, a mouse and a physics scene; the automation command in
	 * tools/unreal_build.py passes -nullrhi. Everything it feeds is testable
	 * and lives on UCataclysmCreaturePanel.
	 */
	const ACataclysmEnemyCharacter* CreatureUnderCursor() const;

	/** Every floating number that has not yet faded. */
	void DrawDamageNumbers();

	/** The name of every item lying on the floor, over where it lies. */
	void DrawDropNames();

	/** One bar: a dark backing, then the filled share of it. */
	void DrawBar(float ScreenX, float ScreenY, float Width, float Height,
				 float Fraction, const FLinearColor& Fill, float Opacity);

	/**
	 * Draws text centred on a point rather than starting at it.
	 *
	 * A NUMBER MUST SIT OVER WHAT IT DESCRIBES, and DrawText places the LEFT
	 * edge at the coordinate given, so a four figure number would sit noticeably
	 * further right than a one figure one over the same creature.
	 *
	 * @return the rectangle the text filled, so a caller that needs the
	 *         text to be clickable knows where it landed. A drop's name
	 *         is the only such caller today.
	 */
	FBox2D DrawTextCentred(const FString& Text, const FLinearColor& Colour,
						   float CentreX, float TopY, float Scale);

	/**
	 * Where a piece of text WOULD be drawn, without drawing it.
	 *
	 * SEPARATE FROM THE DRAW so that several pieces of text can be laid out
	 * against each other before any of them is committed to the screen. That is
	 * what DrawDropNames does: it measures every drop's name, moves the ones
	 * that would overlap, and draws them afterwards.
	 */
	FBox2D MeasureTextCentred(const FString& Text, float CentreX, float TopY,
							  float Scale);

	/**
	 * Draws text with its black outline, with its top left corner at a point.
	 *
	 * EVERY PIECE OF TEXT THIS CLASS DRAWS GOES THROUGH HERE. DrawTextCentred is
	 * a measure followed by a call to this, and the drop names are a measure,
	 * a layout pass and then a call to this. The outline lives here for that
	 * reason, and `tools/tests/test_the_heads_up_display_outlines_its_text.py`
	 * reads this function's body.
	 */
	void DrawOutlinedText(const FString& Text, const FLinearColor& Colour,
						  float Left, float Top, float Scale);

	/**
	 * Draws a hollow rectangle of a given thickness, inside a box.
	 *
	 * FOUR FILLED RECTANGLES, because the canvas has no outline primitive. The
	 * thickness is drawn INWARD from the box's edge, so the box is the outer
	 * extent and a caller that laid two boxes out so they do not overlap gets
	 * two borders that do not overlap either.
	 *
	 * WITH A BLACK EDGE ON BOTH SIDES OF IT, one pixel each, for the reason
	 * DrawOutlinedText gives: the design guarantees a world surface stays under
	 * 30% brightness, which says nothing about a few pixels seen against Demonic
	 * lava. This border is what tells a colour-blind player which rarity they
	 * are looking at, so it washing out is worse than a damage number washing
	 * out.
	 */
	void DrawBorder(const FBox2D& Around, float Thickness,
					const FLinearColor& Colour);

	/** The font every draw here uses. The engine's, so no asset is needed. */
	UFont* OverlayFont() const;

	/** Waiting to be drawn, oldest first. */
	TArray<FCataclysmDamageNumber> Numbers;

	/**
	 * Where each drop's name was drawn last frame, and which drop it was.
	 *
	 * TWO PARALLEL ARRAYS RATHER THAN ONE OF PAIRS, so that the judgement
	 * -- which rectangle a point falls in -- takes nothing but rectangles
	 * and can be tested without a world, an actor or a renderer.
	 * UCataclysmDropPickup::IndexOfNameUnderPoint is that function.
	 *
	 * WEAK POINTERS, because a drop is destroyed the moment it is picked
	 * up and these outlive that by up to a frame.
	 */
	TArray<FBox2D> DropNameRects;
	TArray<TWeakObjectPtr<ACataclysmDroppedItem>> DropsNamed;

	//~ Layout, in pixels at an unscaled viewport.

	/** The player's own bars, measured in from the bottom left corner. */
	static constexpr float PlayerBarMarginPx = 28.0f;
	static constexpr float PlayerBarWidthPx = 280.0f;
	static constexpr float PlayerBarHeightPx = 20.0f;

	/** Clear space between one of the player's bars and the next. */
	static constexpr float PlayerBarGapPx = 6.0f;

	/** A bar over a creature's head. */
	static constexpr float OverheadBarWidthPx = 74.0f;
	static constexpr float OverheadBarHeightPx = 7.0f;

	/** How thick the dark backing sticks out past the fill, on every side. */
	static constexpr float BarBackingInsetPx = 2.0f;

public:
	/**
	 * What every piece of text this draws is scaled by, on top of its own size.
	 *
	 * A BASE SIZE RATHER THAN A SIZE PER CALLER. It multiplies the floating
	 * damage numbers and the player's own health and mana figures together,
	 * because the project owner's complaint after playing was that the font is
	 * "too small in general" rather than about one of them. Issue #668.
	 *
	 * PUBLIC SO A TEST CAN READ IT. Nothing here can be judged by drawing: the
	 * automation tests run with -nullrhi and AHUD::PostRender checks
	 * FApp::CanEverRender() before calling DrawHUD, so every test in
	 * CataclysmCombatOverlayTests.cpp is a static function over plain numbers.
	 *
	 * IT IS A PLACEHOLDER AND ONLY PLAY SETTLES IT, the same as the bar sizes
	 * above. Two figures have now been measured against a real fight and reported
	 * as too small: 1.0, and then 1.6 against the engine's medium font.
	 *
	 * THE FONT CHANGED UNDERNEATH IT AT THE SAME TIME. `OverlayFont` now asks for
	 * the engine's large face rather than its medium one, so this multiplies a
	 * bigger starting size than the 1.6 did. Issue #671.
	 */
	static constexpr float TextScale = 1.8f;
};
