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
	 * **HOW MANY OBJECTIVES ARE NEEDED IS NOT SHOWN, AND THAT IS DELIBERATE.**
	 * The requirement is stated per Cataclysm in `docs/Cataclysm_GDD_v2.md`
	 * section XI -- 10, 5, 10, 5, 5, 10, 8, 5 -- and the empire layer has no
	 * notion of which Cataclysm is running, so "1 of 8" would be a denominator
	 * this build invented. Issue #1357 is what has to land before a fraction can
	 * be honest.
	 *
	 * @param Run the run to describe. Null, or one that has not begun, answers
	 *            an empty string rather than a line of zeroes, so a screen with
	 *            no run shows nothing instead of a false achievement.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString ProgressLine(const UCataclysmEmpireRun* Run);
};
