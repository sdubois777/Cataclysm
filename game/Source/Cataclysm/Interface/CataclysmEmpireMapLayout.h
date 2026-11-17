// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmEmpireMapLayout.generated.h"

class UCataclysmEmpireRun;

/**
 * How a city should read at a glance.
 *
 * THREE STATES AND NOT TWO, because "cannot be attacked yet" is the empire's
 * whole structure. A player who cannot see which cities are exposed cannot make
 * the only decision the strategy layer asks of them.
 */
UENUM(BlueprintType)
enum class ECataclysmCityMark : uint8
{
	/** Behind a standing city. Nothing can reach it. */
	Sealed		= 0	UMETA(DisplayName = "Sealed"),

	/** On the frontier. The next wave can land here. */
	Exposed		= 1	UMETA(DisplayName = "Exposed"),

	/** Lost. The lane behind it is open. */
	Fallen		= 2	UMETA(DisplayName = "Fallen"),
};

/**
 * Where the empire's 25 cities go on screen, and how each one should read.
 *
 * WHY IT IS A SEPARATE CLASS FROM THE WIDGET, which is the reason
 * `UCataclysmPassiveTreeLayout` is separate from `UCataclysmPassiveTreeWidget`:
 * the automation test command in `tools/unreal_build.py` passes `-nullrhi`, so
 * nothing that reaches the screen can be watched by a test. Everything here is
 * arithmetic over plain values, so all of it is covered while the drawing itself
 * stays uncovered.
 *
 * MUCH SIMPLER THAN THE PASSIVE TREE'S, AND FOR A REAL REASON. A class tree is
 * authored on a canvas of its own in `C:\Projects\PassiveTreeCreator`, so it has
 * to be scaled, panned and zoomed to be read at all. The empire is 25 cities at
 * fixed lattice coordinates from -3 to 3 on each axis. There is no authored
 * layout, nothing to pan and nothing to zoom -- only fitting a diamond seven
 * cells across into whatever panel it is given.
 */
UCLASS()
class CATACLYSM_API UCataclysmEmpireMapLayout : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ----------------------------------------------------------------------
	// The shape of the drawing
	// ----------------------------------------------------------------------

	/**
	 * How far apart two neighbouring cities are, at a scale of one, in pixels.
	 *
	 * WIDER THAN IT IS TALL BECAUSE A CITY IS. A city is drawn as a box holding
	 * its tier and how much defence it has left, which is a wide shape, and the
	 * lattice would look like a squashed diamond if the spacing did not follow
	 * it.
	 */
	static constexpr float CellWidthPx = 188.0f;

	/**
	 * How far apart two rows are.
	 *
	 * IT DECIDES WHETHER THE WORDS FIT, WHICH IS NOT OBVIOUS. The diamond is
	 * seven rows tall and the panel it is given is much wider than it is tall
	 * once the screen's four labels have taken their lines, so the HEIGHT is
	 * what binds the scale -- and the words only fit their boxes at a scale of
	 * one. It was 92 until issue #1089, which put the scale at about 0.79 on the
	 * project owner's screen and cut every label short.
	 *
	 * WHY SCALING THE WORDS DOES NOT MAKE THIS UNNECESSARY. It helps and it is
	 * not enough: the button's own padding is a fixed number of pixels that does
	 * not scale, so it eats a growing share of a shrinking box, and a font at
	 * half the points is more than half as wide because of hinting. Measured on
	 * 2026-08-31, "Sanctuary 100%" needed 96 pixels in an 86 pixel box at a scale
	 * of a half. Below about a scale of one the words are cut and the line under
	 * the map is what carries them.
	 */
	static constexpr float CellHeightPx = 68.0f;

	/**
	 * How big one city is drawn, at a scale of one, in pixels.
	 *
	 * SMALLER THAN THE CELL, so two neighbouring cities have clear space between
	 * them. A diamond of touching boxes reads as a grid rather than as a map.
	 *
	 * WIDE ENOUGH FOR THE WIDEST LABEL AS MEASURED, and it was 128 until issue
	 * #1089.
	 *
	 * MEASURED, NOT ESTIMATED, AND THE DIFFERENCE IS WHY THE BUG EXISTED. The
	 * 128 was chosen against a guess that "Sanctuary 100%" -- the label with the
	 * most CHARACTERS -- would be the widest, at about 115 pixels. Slate says
	 * "Outpost 100%" is the widest at 132 pixels, because it has more wide
	 * letters and fewer narrow ones. Counting characters is not measuring text.
	 * `Cataclysm.EmpireScreen.EveryCityLabelMeasuresNarrowerThanItsBox` measures
	 * every label the screen can produce and fails if any of them stops fitting.
	 */
	static constexpr float CityWidthPx = 172.0f;
	static constexpr float CityHeightPx = 48.0f;

	/**
	 * Clear space left around the diamond when it is fitted to a panel.
	 *
	 * WITHOUT IT THE OUTERMOST CITIES TOUCH THE EDGE, and a city is drawn
	 * centred on its position, so half of the Outpost at the top would be off
	 * the panel.
	 */
	static constexpr float FitMarginPx = 24.0f;

	/**
	 * How far the drawing may be scaled.
	 *
	 * THE CEILING IS ONE AND THAT IS DELIBERATE. The sizes above are the size a
	 * city is meant to be read at; drawing one larger gains nothing, because the
	 * words inside it are already fully visible. A panel with room to spare gets
	 * a centred diamond and empty space around it rather than a swollen one.
	 *
	 * THE FLOOR IS NOT A READABLE SIZE AND IS NOT MEANT TO BE. It is there so
	 * that a panel of almost no size cannot produce a scale of zero, which would
	 * put all 25 cities on one point.
	 */
	static constexpr float SmallestScale = 0.15f;
	static constexpr float LargestScale = 1.0f;

	/** How many cities the lattice is across at its widest. Seven. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static int32 CellsAcross();

	/** How wide and tall the whole diamond is at a scale of one, in pixels. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FVector2D DiamondSize();

	/**
	 * The scale at which the whole diamond fits inside a panel of this size.
	 *
	 * THE SMALLER OF WHAT THE WIDTH AND THE HEIGHT ALLOW, so it fits both ways
	 * rather than one.
	 *
	 * @return the clamped scale. A panel smaller than the margins answers the
	 *         smallest scale rather than zero or a negative one.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static float ScaleToFit(FVector2D PanelSize);

	/**
	 * Where a city sits on the panel, as the CENTRE of the city.
	 *
	 * THE CENTRE AND NOT THE CORNER, because a city is placed on a canvas slot
	 * whose alignment is the middle, the same as a passive tree node.
	 *
	 * The lattice row grows downwards and the column rightwards, which is what
	 * turns the taxicab ball into the diamond the design document draws.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FVector2D PositionFor(int32 R, int32 C, float Scale,
								 FVector2D PanelSize);

	/** How big one city is drawn at this scale. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FVector2D CitySize(float Scale);

	/** Keep a scale inside the range, whatever produced it. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static float ClampScale(float Scale);

	// ----------------------------------------------------------------------
	// What a city says
	// ----------------------------------------------------------------------

	/**
	 * How a city should read: sealed, exposed or fallen.
	 *
	 * IT IS THE MAP'S OWN ANSWER AND NOT A SECOND OPINION. `IsExposed` is what
	 * decides where a surge can land, so a screen that decided differently would
	 * be showing the player a rule the game does not obey.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static ECataclysmCityMark MarkFor(const UCataclysmEmpireMap* Map,
									  int32 CityId);

	/**
	 * What one city's box says: its tier, and the share of its defence left.
	 *
	 * "Outpost 100%", "Bulwark 63%", "Sanctuary lost".
	 *
	 * THE SHARE AND NOT THE NUMBER. A Sanctuary has 8,000 defence and an Outpost
	 * 1,000, so the raw figures cannot be compared by eye, and what a player has
	 * to decide is which city is closest to falling.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString CityLabel(const UCataclysmEmpireMap* Map, int32 CityId);

	/**
	 * What the PLAYER has done to the map: dungeons beaten and objectives earned.
	 *
	 * "4 dungeons cleared, 1 quest objective". Everything else the empire screen
	 * says is what is being done TO the empire -- the day, the cities lost, the
	 * dungeons standing, the next surge -- and until this there was nowhere on
	 * the screen a player could see what they had achieved. Issue #1324 slice 5.
	 *
	 * A FUNCTION HERE RATHER THAN A `Printf` INSIDE THE WIDGET, which is the
	 * split this whole class exists for: the automation tests run with
	 * `-nullrhi` and a headless widget's `BindWidget` labels are all null, so a
	 * string built inside `WriteStatus` cannot be read back by any test. This
	 * takes a run and answers a string, so every case below is covered.
	 *
	 * **AND HOW FAR THE PLAYER IS FROM THE CATACLYSM BOSS DUNGEON**, since issue
	 * #1324 slice 6c (2026-11-03): "4 dungeons cleared, 7 quest objectives, 1 of
	 * 5 Cataclysms finished; the boss dungeon opens when 3 are finished". The
	 * fraction is Cataclysms finished (`UCataclysmEmpireRun::CataclysmsComplete`)
	 * of the Cataclysms the run faces, and the number it opens at is
	 * `CataclysmDungeonRequirement`, half of them rounded up. Once it has opened
	 * the clause says "the Cataclysm boss dungeon is open", and once it has left
	 * the board, which only a clear does, "the Cataclysm boss dungeon has been
	 * cleared". A run whose requirement is met and whose boss has not opened --
	 * which play cannot reach, because the clear that meets it opens it, but a
	 * run record from before version 4 can, since `Migrate_3_to_4` writes it
	 * unopened -- is told the next quest dungeon cleared opens it, which is when
	 * `OpenCataclysmDungeonIfEarned` next runs.
	 *
	 * **THIS SAID NO DENOMINATOR COULD BE SHOWN, AND FOR OBJECTIVES THAT IS STILL
	 * SO.** Each Cataclysm asks for its own count, `docs/Cataclysm_GDD_v2.md`
	 * section XI -- 10, 5, 10, 5, 5, 10, 8, 5 -- so "7 of 8 quest objectives"
	 * would add counts that belong to different Cataclysms. What issue #1357
	 * made honest is the count of CATACLYSMS finished, which the run has known
	 * since it began keeping objectives per Cataclysm; the line said "until
	 * #1357" and #1357 had landed.
	 *
	 * @param Run the run to describe. Null, or one that has not begun, answers
	 *            an empty string rather than a line of zeroes, so a screen with
	 *            no run shows nothing instead of a false achievement.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString ProgressLine(const UCataclysmEmpireRun* Run);

	// ----------------------------------------------------------------------
	// The empire upgrade point bank -- issue #52, the first slice
	// ----------------------------------------------------------------------

	/**
	 * A number of empire upgrade points as the screen shows it: ROUNDED DOWN TO A TENTH, "12.5", "0.3", "1.0".
	 *
	 * DOWN AND NOT TO THE NEAREST, so the screen never shows a point the bank does not hold: the bank keeps fractions
	 * (ruled 2026-11-03), and a figure rounded up could promise a tree node that cannot yet be bought. A TENTH AND NOT A
	 * WHOLE POINT, because an ordinary dungeon is worth 1 point times the share of the population living, so most
	 * awards are below one and a whole-point figure would show most clears earning nothing.
	 *
	 * ARITHMETIC NOISE BELOW A MILLIONTH IS FORGIVEN before rounding, so three awards of a third that add up to
	 * 0.99999999 in binary read "1.0" and not "0.9". Negative points, which nothing produces, read as 0.0.
	 */
	static FString EmpirePointsText(double Points);

	/**
	 * The bank, and what the last dungeon cleared added to it: "12.5 empire points banked (+0.5 from the last
	 * dungeon defeated)", or without the bracket when the last clear awarded nothing -- or when none has this session,
	 * since the run does not save its last award.
	 */
	static FString EmpirePointsLine(double Banked, double LastAwarded);

	/**
	 * What the empire screen's status line says about the bank, for the character playing in a world: `EmpirePointsLine`
	 * of its partition's bank (`UCataclysmGameInstance::AccountFor`) and the run's last award.
	 *
	 * "Solo Self-Found: empire points are not banked yet" for a Solo Self-Found character, whose points belong in its
	 * own character record, which this slice does not write. Empty with no game instance of this project's, no
	 * character playing, or an account record that would not read.
	 *
	 * NOT CONST ON THE GAME INSTANCE: the first ask reads the bank from its slot and holds it from then on.
	 */
	static FString EmpirePointsLineIn(const UObject* WorldContext, const UCataclysmEmpireRun* Run);

	/**
	 * The days a walk under way has spent and the empire has not yet run, as a line: "3 days pass when you
	 * leave", "1 day passes when you leave", or empty when none are owed.
	 *
	 * WHY THE SCREEN NEEDS IT. Since 2026-10-24 nothing resolves while the player is inside a dungeon, by the
	 * owner's decision, so the day this screen shows does not move during a walk. Without this line a player who
	 * opens the map mid-walk reads a day that is not the one their walk has reached, and nothing that says the
	 * days are coming. `UCataclysmEmpireRun::DaysOwed` is the count.
	 *
	 * @param Run the run to describe. Null, or one owing nothing, answers an empty string.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString DaysOwedLine(const UCataclysmEmpireRun* Run);

	/**
	 * The last hit a city took from a dungeon's timer, as one line: "Day 37: a
	 * dungeon hit Outpost (-3,0) and left." or "Day 37: a Quest dungeon's
	 * timer hit Outpost (-3,0)." with the city's own name.
	 *
	 * WHY THE SCREEN NEEDS IT. Since 2026-10-13 an ordinary dungeon hits its
	 * city once and leaves the map, so the dungeon that did the damage is not
	 * on the screen afterwards, and a Quest dungeon's timer now costs its host
	 * city. Without this line a player sees a city's defence go down and
	 * nothing that says why.
	 *
	 * THE LAST ONE ONLY. The run keeps three fields for it,
	 * `UCataclysmEmpireRun::LastHitDay` and the two beside it; a list of the
	 * day's events is the console command's.
	 *
	 * **OR A FALL'S MOVE, WHEN THAT IS AS RECENT.** Since 2026-10-21 a Quest
	 * dungeon standing on a city that falls moves to another city instead of
	 * being absorbed, and the line then reads "Day 37: a Quest dungeon moved
	 * from Outpost (-3,0) to Outpost (-2,1) because Outpost (-3,0) fell." The
	 * run keeps `UCataclysmEmpireRun::LastMoveDay` and the two beside it for
	 * it. The later of the last hit and the last such move is shown, and on
	 * the same day the move.
	 *
	 * A FUNCTION HERE RATHER THAN A `Printf` INSIDE THE WIDGET, for the reason
	 * `ProgressLine` gives: a headless test cannot read a label.
	 *
	 * @param Run the run to describe. Null, one that has not begun, and one in
	 *            which no hit has landed and no fall has moved a Quest dungeon
	 *            all answer an empty string.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString LastHitLine(const UCataclysmEmpireRun* Run);

	/**
	 * What the player's last death in a dungeon of the empire cost, as one line: "Day 42: you died in the Hollow
	 * dungeon on Outpost (-3,0). The walk's 3 days ran, the death cost 5 more days, and Outpost (-3,0) fell." Issue
	 * #41, ruled by the coordinating session on 2026-11-02.
	 *
	 * WHY THE SCREEN NEEDS IT. A death ends the walk, runs the days it had spent, charges 5, 10 or 15 more, and
	 * resolves an ordinary dungeon at once, which can fell its city. Until this, all of it was worked out and only
	 * written to the log.
	 *
	 * READ FROM THE FIVE FIELDS THE RUN KEEPS, `UCataclysmEmpireRun::LastDeathDay` and the four beside it, which
	 * `ACataclysmDungeonGameMode::EndTheDungeonForADeath` writes with the figures it computed.
	 *
	 * THE WORDS. Every city the death's resolve felled is named, by the map's name for it, "X fell", "X and Y fell",
	 * "X, Y and Z fell", and "no city fell" when none did. A day count of one is "1 day". A walk that owed no whole
	 * day says so, and the death's days are then not "more".
	 *
	 * @param Run the run to describe. Null, one that has not begun, and one with no death in it all answer an empty
	 *            string.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString LastDeathLine(const UCataclysmEmpireRun* Run);

	/**
	 * The one line the empire screen shows for the latest thing that happened to the empire: `LastDeathLine` while
	 * the death is at least as recent as the last hit, the last fall's move and the opening of the Cataclysm boss
	 * dungeon; `CataclysmDungeonOpenedLine` while the opening is at least as recent as the last hit and the last
	 * move; and `LastHitLine` otherwise.
	 *
	 * ON THE SAME DAY THE DEATH IS SHOWN, a judgement of 2026-11-02: the death's own resolve can be that day's hit,
	 * and the death's line names what it felled, so it is the larger account of the two. A hit on a LATER day -- in
	 * the days the death charged, say -- is more recent, and its line replaces the death's.
	 *
	 * **THE OPENING, ON THE SAME DAY, BEATS A HIT OR A MOVE AND LOSES TO A DEATH**, a judgement of issue #1324 slice
	 * 6c (2026-11-03), from the order the three happen in on one day. A hit and a fall's move of day D happen while
	 * day D is advanced, and nothing resolves while the player is inside a dungeon (2026-10-24), so they come before
	 * any walk begun on day D; the opening's day is `Day()` when the walk's quest clear was credited, before the
	 * walk's own owed days run, so it comes after them. A death costs at least five days on top of its walk's, so no
	 * opening can share a day with a death that came before it; a death can share a day with an opening only from a
	 * LATER walk that owed no whole day, which came after it. So the later event is shown each time.
	 *
	 * @param Run the run to describe. Empty when no line has anything to say.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString LatestEventLine(const UCataclysmEmpireRun* Run);

	// ----------------------------------------------------------------------
	// The Cataclysm boss dungeon, which stands on no city -- issue #1324 slice 6c
	// ----------------------------------------------------------------------

	/**
	 * How big the Cataclysm boss dungeon's marker is drawn, at a scale of one, in pixels: two cities wide and one
	 * tall.
	 *
	 * IT IS ON NO CITY, so it cannot be drawn where cities are. The project owner, 2026-11-02, verbatim: "On no city
	 * (Recommended)" -- the empire map shows a Cataclysm boss dungeon marker the player enters from, and it never
	 * bites a city. So it is a box of its own in the map panel's top left corner, where the diamond has no city.
	 *
	 * TWO CITIES WIDE BECAUSE ITS LABEL IS LONGER than any city's: "Cataclysm boss: 137 floors" against "Outpost
	 * 100%". `Cataclysm.BossOnTheMap.TheMarkerSitsInTheEmptyCornerAndItsLabelFits` measures it.
	 */
	static constexpr float CataclysmDungeonMarkerWidthPx = 2.0f * CityWidthPx;
	static constexpr float CataclysmDungeonMarkerHeightPx = CityHeightPx;

	/**
	 * Where the marker sits, as its CENTRE, for the reason `PositionFor` answers a centre: the box is placed on a
	 * canvas slot aligned to its middle, as a city's is.
	 *
	 * THE TOP LEFT CORNER, A MARGIN IN FROM BOTH EDGES, AND IT COVERS NO CITY ON ANY PANEL THE DIAMOND FITS IN. The
	 * diamond is fitted inside the same margins and centred, so its top row starts no higher than the marker's top,
	 * and that row holds only the Outpost in the middle, whose box begins 564 scaled pixels right of the diamond's left
	 * edge -- three cells -- where the marker ends at most 344 right of it. The next row down begins 68 below
	 * the top row, where the marker is 48 tall. `Cataclysm.BossOnTheMap.TheMarkerSitsInTheEmptyCornerAndItsLabelFits`
	 * checks it against every city on the panels the city overlap test uses.
	 *
	 * NOT ON A PANEL TOO SMALL FOR THE DIAMOND, where `ScaleToFit` is held at `SmallestScale` and the diamond spills
	 * past the margins: then the corner is not empty and the marker can lie over the top rows. Measured on 2026-11-03
	 * with a Python copy of `ScaleToFit` and `PositionFor`, sweeping panels from 100 to 4,000 pixels wide and 100 to
	 * 3,000 tall in steps of 7: the 73 panels on which it covered a city were all held at `SmallestScale`, and none
	 * was wider than 233 or taller than 128.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FVector2D CataclysmDungeonMarkerPosition(float Scale);

	/** How big the marker is drawn at this scale. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FVector2D CataclysmDungeonMarkerSize(float Scale);

	/**
	 * The gap between the marker and the Walk box under it, at a scale of one, in pixels. Issue #2715.
	 */
	static constexpr float CataclysmDungeonWalkGapPx = 4.0f;

	/**
	 * Where the boss dungeon's Walk box sits, as its CENTRE: directly under the marker, its left edge on the marker's,
	 * the gap below it. Issue #2715.
	 *
	 * ONE CITY WIDE AND ONE TALL, AND IT COVERS NO CITY ON ANY PANEL THE DIAMOND FITS IN, for the reason the marker
	 * covers none, worked the same way. Its right edge is at most one city width (172 scaled pixels) right of the
	 * diamond's left edge, and the first three rows of the diamond begin at least one cell (188) right of it; the fourth
	 * row, the first that begins at the diamond's left edge, starts 204 below the diamond's top, where the Walk box ends
	 * 100 below it (48 of marker, 4 of gap, 48 of box). A test in `Cataclysm.WalkFromTheScreen.` checks it against
	 * every city on the panels the marker's test uses. NOT ON A PANEL TOO SMALL FOR THE DIAMOND, for the marker's
	 * reason.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FVector2D CataclysmDungeonWalkPosition(float Scale);

	/** How big the Walk box is drawn at this scale: one city's box. Issue #2715. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FVector2D CataclysmDungeonWalkSize(float Scale);

	/**
	 * The name the boss dungeon's Walk box carries, which says which dungeon a press walks:
	 * `FCataclysmWalkFromTheScreen::ButtonNameFor` the boss's identifier. `NAME_None` while no Cataclysm boss dungeon
	 * stands. Issue #2715.
	 *
	 * @param Run the run to describe. Null, or one that has not begun, answers `NAME_None`.
	 */
	static FName CataclysmDungeonWalkButtonName(const UCataclysmEmpireRun* Run);

	/**
	 * What the marker's box says: "Cataclysm boss: 137 floors". Empty while no Cataclysm boss dungeon stands --
	 * before it opens, and after it has been cleared -- which is what hides the marker.
	 *
	 * @param Run the run to describe. Null, or one that has not begun, answers an empty string.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString CataclysmDungeonMarkerLabel(const UCataclysmEmpireRun* Run);

	/**
	 * What the line under the map says when the cursor is on the marker, or it is pressed: "The Cataclysm boss
	 * dungeon (Death, Horde): 137 floors, 12 of them added by ordinary dungeons defeated. Every ordinary dungeon
	 * defeated adds a floor. It stands on no city and never resolves. Walk, under this box, enters it." Empty while
	 * none stands.
	 *
	 * IT NAMES THE WALK BOX, since issue #2715, which is what enters it. Until then it named the console command
	 * `Cataclysm.EnterDungeon` with the dungeon's identifier and said nothing entered a dungeon from this map, which
	 * was true then and is not now.
	 *
	 * THE CATACLYSM IS `UCataclysmRoster::NameFor`'s, the name `Describe` uses, and the sub-type is
	 * `UCataclysmSurgeScheduler::SubTypeName`'s; either is left out when it says nothing. THE FLOORS ADDED are the
	 * dungeon's floors less the floors it rolled, `CataclysmDungeonBaseFloors`, which is every ordinary dungeon
	 * defeated this run (`UCataclysmEmpireRun::CataclysmDungeonFloorsNow`). None added reads "as it rolled".
	 *
	 * @param Run the run to describe. Null, or one that has not begun, answers an empty string.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString CataclysmDungeonBrief(const UCataclysmEmpireRun* Run);

	/**
	 * The opening of the Cataclysm boss dungeon, as the empire screen's latest event: "Day 214: the Cataclysm boss
	 * dungeon opened." The day is `UCataclysmEmpireRun::CataclysmDungeonOpenedDay`. It stays this run's opening after
	 * the dungeon has been cleared, as a hit's line stays after its dungeon has left.
	 *
	 * @param Run the run to describe. Null, one that has not begun, and one that has not opened it answer an empty
	 *            string.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString CataclysmDungeonOpenedLine(const UCataclysmEmpireRun* Run);

	// ----------------------------------------------------------------------
	// The win of the run before this one -- issue #1324 slice 6e
	// ----------------------------------------------------------------------

	/**
	 * The win of the run before this one, as one line: "Run won on day 380 at tier 3 against Death. +10 passive
	 * points. This run faces 4 Cataclysms." Issue #1324 slice 6e, ruled by the coordinating session on 2026-11-02
	 * (R7: the next run begins at once, "with a win line"); the words are the session's that built it.
	 *
	 * READ FROM THE FOUR FIELDS THE NEW RUN CARRIES, `UCataclysmEmpireRun::LastRunWonDay` and the three beside it,
	 * which `UCataclysmGameInstance::BeginTheRunAfterAWin` writes, and from the run's own list of Cataclysms.
	 *
	 * PLAIN AND LITERALLY TRUE. A boss the character had defeated before paid nothing, and the line says so rather
	 * than "+0". At tier 8 no ninth Cataclysm is added, and the line says the run faces the same eight.
	 *
	 * @param Run the run to describe. Null, one that has not begun, and one whose run before it was not won all answer
	 *            an empty string.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString LastRunWonLine(const UCataclysmEmpireRun* Run);
};
