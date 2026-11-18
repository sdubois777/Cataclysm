// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmExperienceBar.generated.h"

class AActor;

/**
 * The heads-up display's level and experience bar: what it is filled to, what it says, and where its figures come
 * from. Issue #2744, the owner's ruling of 2026-11-18, "Level 1 + experience bar": "The HUD gains a level and
 * experience bar." Until it, no screen showed experience during play, and the character sheet showed the level only.
 *
 * WHERE IT IS DRAWN: `ACataclysmHUD::DrawPlayerVitals`, the lowest bar of the bottom-left stack, under health, the size
 * of the health bar. Drawn on the canvas like every other bar there, so no widget asset holds it.
 *
 * NOTHING HERE DRAWS, for the reason `ACataclysmHUD` gives: the automation tests run with `-nullrhi`, so `DrawHUD`
 * never runs under test. Every judgement the bar makes is a static function here, over plain numbers.
 *
 * POLLED EVERY FRAME, as the health bar is, so a kill that grants experience and a level gained both show on the next
 * frame without an event.
 */
UCLASS()
class CATACLYSM_API UCataclysmExperienceBar : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * How full the bar is: the experience into the level over what the next level costs
	 * (`UCataclysmExperience::CostOfLevel(Level + 1)`), from 0 to 1.
	 *
	 * FULL AT THE MAXIMUM LEVEL, where the next level costs nothing: the issue's "At the maximum level, the bar is full
	 * and says so". A level below 1 is read as 1, as `UCataclysmExperience::Grant` reads one.
	 */
	static float FractionFor(int32 Level, int64 ExperienceIntoLevel);

	/**
	 * What the bar says: "Level 7: 45.2%", the share of the next level's cost earned, in tenths of a percent; at the
	 * maximum level, "Level 100: maximum".
	 *
	 * ROUNDED DOWN, so the bar never reads 100.0% before the level is reached: one experience short of a level reads
	 * 99.9%.
	 *
	 * A SHARE AND NOT THE FIGURES, A JUDGEMENT of the session that built it (issue #2744). The figures at the top of the
	 * climb are nine digits each -- "Level 99: 480123456 / 519995268" -- and do not fit the 280 pixels the health bar is
	 * drawn in. The exact figures are in the PLAY LOOP line a level gained says, and in `Cataclysm.ShowLevel`.
	 */
	static FString TextFor(int32 Level, int64 ExperienceIntoLevel);

	/**
	 * The level and the experience into it of the character this pawn belongs to, read from its
	 * `ACataclysmPlayerState`, where the level lives.
	 *
	 * @return false, with the outputs untouched, for a null actor, an actor that is not a pawn, or a pawn with no
	 *         player state of this game's class yet -- which on a client is an ordinary frame, as for the health bar.
	 */
	static bool LevelOf(const AActor* Pawn, int32& OutLevel, int64& OutExperienceIntoLevel);

	/**
	 * The bar's fill: a muted gold, a judgement. Kept apart from every fill the bottom-left stack already uses -- the
	 * health red, the mana blue, the shield's light blue, the absorb's pale cream and the Fervour magenta -- and from
	 * the amber of a critical strike's number (`UCataclysmCombatOverlay`).
	 */
	static const TCHAR* FillHex;
};
