// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmEmpireStatusBar.generated.h"

class UCataclysmEmpireRun;

/**
 * The three figures the empire status bar says, gathered from a run, and whether there was a run to gather them from.
 *
 * PLAIN NUMBERS SO A TEST CAN WRITE THEM. `UCataclysmEmpireStatusBar::LineFor` turns these into the words, and a test
 * of the words should not have to build a campaign that happens to land on "1 city at risk".
 */
struct CATACLYSM_API FCataclysmEmpireStatusFacts
{
	/** Whether a begun run was found. False draws nothing at all, ruling (c) of 2026-11-02. */
	bool bRunBound = false;

	/** Every dungeon standing on the map, `UCataclysmEmpireRun::DungeonCount`. */
	int32 DungeonsStanding = 0;

	/**
	 * Whole days from the day the walk has reached to the next scheduled surge, rounded up; 0 when the days the walk
	 * owes already reach it. See `UCataclysmEmpireStatusBar::DaysToNextSurgeIn`.
	 */
	int32 DaysToNextSurge = 0;

	/** The days the walk under way owes, `UCataclysmEmpireRun::DaysOwed`. Read only to word a surge at 0 days. */
	int32 DaysOwed = 0;

	/** Distinct standing cities with a dungeon on them. See `UCataclysmEmpireStatusBar::CitiesAtRiskIn`. */
	int32 CitiesAtRisk = 0;

	/**
	 * Whether every field is the same as another set's, the flag included.
	 *
	 * NOT WHETHER THE TWO LINES ARE THE SAME: days owed are read only at 0 days, so two sets differing in them alone
	 * can say one line. The HUD then writes it again, which costs one line's writing and is never wrong.
	 *
	 * WHAT LETS THE HUD KEEP ITS LINE. `ACataclysmHUD::DrawEmpireStatusBar` reads the figures each frame -- a few
	 * integers -- and writes and measures the line again only when they differ from the ones it last drew, so a field
	 * left out of this comparison would leave a stale figure on screen. A new field goes here as well.
	 */
	bool SaysTheSameAs(const FCataclysmEmpireStatusFacts& Other) const
	{
		return bRunBound == Other.bRunBound
			&& DungeonsStanding == Other.DungeonsStanding
			&& DaysToNextSurge == Other.DaysToNextSurge
			&& DaysOwed == Other.DaysOwed
			&& CitiesAtRisk == Other.CitiesAtRisk;
	}
};

/**
 * The empire status bar: one line in the top left corner of the dungeon screen saying how many dungeons stand, when
 * the next surge comes and how many cities are at risk. Issue #49, the design's HUD Elements list: "Empire status bar
 * -- active dungeon count, next surge countdown, cities at risk".
 *
 * WHAT THE PLAYER SEES, while an empire run is bound:
 *
 *     3 dungeons standing | surge in 4 days | 2 cities at risk
 *
 * and nothing at all while none is, which is Play in the sandbox or in `L_Dungeon` with no run.
 *
 * EVERY JUDGEMENT IS HERE AND THE DRAWING IS IN `ACataclysmHUD::DrawEmpireStatusBar`, the split
 * `UCataclysmCombatOverlay`, `UCataclysmSkillBar` and `UCataclysmPotions` already make, for their reason: the
 * automation command passes `-nullrhi`, so `DrawHUD` never runs under test.
 *
 * THE RULINGS, by the coordinating session under the owner's delegation, 2026-11-02 (`docs/DECISIONS.md`):
 *
 *   (a) A city at risk is a distinct city, not fallen, with at least one dungeon standing on it right now. A standing
 *       dungeon is what can take a city when it resolves; `UCataclysmEmpireMap::IsExposed` only says a dungeon COULD
 *       land there. The design does not define the term.
 *   (b) The surge countdown counts from the day the walk has reached, `Day() + DaysOwed()`: the days a walk spends are
 *       paid when the player leaves, because nothing resolves while the player is inside a dungeon (the owner's
 *       decision of 2026-10-24).
 *   (c) No run bound, nothing drawn.
 */
UCLASS()
class CATACLYSM_API UCataclysmEmpireStatusBar : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Whether the bar is drawn at all: `Cataclysm.Overlay.EmpireStatusBar`, default 1. */
	static bool Enabled();

	// ----------------------------------------------------------------------
	// Gathering the figures from a run
	// ----------------------------------------------------------------------

	/**
	 * How many distinct cities that have not fallen have at least one dungeon standing on them. Ruling (a).
	 *
	 * CITIES, NOT DUNGEONS: three dungeons on one Outpost are one city at risk. A FALLEN CITY IS NOT AT RISK even with a
	 * dungeon on it -- a fall puts a Fallen City dungeon there (`UCataclysmEmpireRun::AddFallenCityDungeon`) -- because
	 * it has nothing left to lose. 0 for no run or a run that has not begun.
	 */
	static int32 CitiesAtRiskIn(const UCataclysmEmpireRun* Run);

	/**
	 * Whole days from the day the walk has reached to the next scheduled surge. Ruling (b).
	 *
	 * `UCataclysmSurgeScheduler::DaysUntilNextSurge` asked at `Day() + DaysOwed()`, and ROUNDED UP: the surge lands on
	 * the first whole day at or after `NextSurgeDay`, so 10.5 days off is 11 advances off. 0 when the days owed reach the
	 * surge's day, or when it is due before the next day begins.
	 *
	 * THE SCHEDULED SURGE ONLY, as the empire screen's own line is: an empty board or a city's fall can bring one sooner
	 * (`UCataclysmSurgeScheduler::IsDueOnEmptyBoard`, `IsDueOnCityFall`), and no countdown can say when that will be.
	 */
	static int32 DaysToNextSurgeIn(const UCataclysmEmpireRun* Run);

	/** All three figures from one run. Not bound -- every figure 0 -- for null and for a run that has not begun. */
	static FCataclysmEmpireStatusFacts FactsOf(const UCataclysmEmpireRun* Run);

	/**
	 * The figures of the run the world's game instance holds, through `UCataclysmGameInstance::EmpireRunFor`, which
	 * never starts one. Not bound for no world, a game instance that is not this project's, or one holding no run.
	 */
	static FCataclysmEmpireStatusFacts FactsForWorld(const UObject* WorldContext);

	// ----------------------------------------------------------------------
	// What the line says
	// ----------------------------------------------------------------------

	/**
	 * The line, or empty when no run is bound.
	 *
	 * "3 dungeons standing | surge in 4 days | 2 cities at risk". ONE IS SINGULAR AND EVERYTHING ELSE PLURAL, nought
	 * included: "1 dungeon standing", "0 cities at risk". The surge reads:
	 *
	 *   "surge in N days"       N of 2 or more
	 *   "surge tomorrow"        N of 1; and N of 0 with no days owed, which is a surge already due and lands as the next
	 *                           day begins (`UCataclysmEmpireRun::AdvanceDay` asks `IsDue` of the day about to begin)
	 *   "surge when you leave"  N of 0 with days owed: the owed days reach it, and they run when the player leaves
	 *
	 * The last is the one case where the line has to say which way it counts to be true; every other count is from the
	 * day the walk has reached, which is the player's present.
	 */
	static FString LineFor(const FCataclysmEmpireStatusFacts& Facts);

	/** `LineFor(FactsForWorld(WorldContext))`: what `ACataclysmHUD::DrawEmpireStatusBar` draws. */
	static FString LineForWorld(const UObject* WorldContext);

	// ----------------------------------------------------------------------
	// Where it goes, in pixels at an unscaled viewport
	// ----------------------------------------------------------------------

	/**
	 * The top left corner, WHICH NOTHING ELSE ON THE DUNGEON SCREEN USES: the player's bars and their effects list
	 * stack up from the bottom left, the skill bar is bottom middle, the potions bottom right, the creature panel top
	 * middle and the floor's modifier panel top right. The left inset is the player's bars' own, so the two left edges
	 * line up; the top is the creature panel's, so the two tops do.
	 */
	static constexpr float LeftPx = 28.0f;
	static constexpr float TopPx = 24.0f;

	/** Clear space kept between the end of the line and the creature panel's narrowest left edge. */
	static constexpr float GapToPanelPx = 16.0f;

	/**
	 * The text's size, relative to a damage number, on top of `ACataclysmHUD::TextScale`.
	 *
	 * SMALLER THAN A BLOW LANDING, for `UCataclysmCombatOverlay::RarityNameScale`'s reason: it is on screen the whole
	 * time, so at full size it would compete with what is only there for a moment. Tuned by eye; no test can see it.
	 */
	static constexpr float BaseScale = 0.6f;

	/** The smallest it is shrunk to on a narrow screen, so it is never unreadably small. */
	static constexpr float SmallestScale = 0.35f;

	/** Warm near-white, the ink the rarity word is drawn in, outlined in black as every piece of HUD text is. */
	static const TCHAR* TextHex;

	/**
	 * The width the line may take: from `LeftPx` to `GapToPanelPx` short of where the creature panel's left edge is at
	 * its narrowest (`UCataclysmCreaturePanel::MinimumWidthShare`, centred).
	 */
	static float RoomBesideThePanel(float ViewportWidth);

	/**
	 * The scale to draw the line at, given how wide it measures at `BaseScale`: `BaseScale` when it fits
	 * `RoomBesideThePanel`, shrunk until it does when it does not, and never below `SmallestScale`.
	 *
	 * A CREATURE PANEL WIDER THAN ITS NARROWEST can still reach the line, and so can a screen narrow enough to reach the
	 * floor. Both are drawn over each other rather than moved; the creature panel is drawn last, on top.
	 */
	static float ScaleToFit(float WidthAtBaseScale, float ViewportWidth);
};
