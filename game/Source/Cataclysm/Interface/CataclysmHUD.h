// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "CataclysmHUD.generated.h"

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

	/** Every floating number that has not yet faded. */
	void DrawDamageNumbers();

	/** One bar: a dark backing, then the filled share of it. */
	void DrawBar(float ScreenX, float ScreenY, float Width, float Height,
				 float Fraction, const FLinearColor& Fill, float Opacity);

	/**
	 * Draws text centred on a point rather than starting at it.
	 *
	 * A NUMBER MUST SIT OVER WHAT IT DESCRIBES, and DrawText places the LEFT
	 * edge at the coordinate given, so a four figure number would sit noticeably
	 * further right than a one figure one over the same creature.
	 */
	void DrawTextCentred(const FString& Text, const FLinearColor& Colour,
						 float CentreX, float TopY, float Scale);

	/** The font every draw here uses. The engine's, so no asset is needed. */
	UFont* OverlayFont() const;

	/** Waiting to be drawn, oldest first. */
	TArray<FCataclysmDamageNumber> Numbers;

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
