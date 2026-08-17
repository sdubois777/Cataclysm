// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmCombatOverlay.generated.h"

struct FCataclysmDamageResult;
struct FCataclysmIncomingHit;

/**
 * One floating number, waiting for the next frame that draws.
 *
 * IT HOLDS A WORLD POSITION RATHER THAN A SCREEN POSITION, because the camera
 * moves between the hit landing and the number fading. A number pinned to
 * screen pixels slides off whatever it was describing the moment the player
 * walks.
 */
USTRUCT()
struct CATACLYSM_API FCataclysmDamageNumber
{
	GENERATED_BODY()

	/** What it says: a figure, or a word when nothing got through. */
	FString Text;

	/** What it means, said in colour. See ColourFor. */
	FLinearColor Colour = FLinearColor::White;

	/** Where it started, in centimetres. Above the head of what was hit. */
	FVector WorldAnchor = FVector::ZeroVector;

	/** The world's time in seconds when it appeared. */
	float StartedAt = 0.0f;

	/** Text size multiplier. A tick is drawn smaller than a blow. */
	float Scale = 1.0f;
};

/**
 * Every decision the combat overlay makes, and none of the drawing.
 *
 * THE SPLIT IS THE POINT, and it is copied from UCataclysmImpactEffect. The
 * automation test command runs with -nullrhi, and AHUD::PostRender checks
 * FApp::CanEverRender() before it calls DrawHUD at all, so no test in this
 * project can watch anything reach the screen -- the same wall issue #559
 * records for the impact particle. What CAN be tested is every judgement that
 * leads up to the drawing, so every one of them lives here as a static function
 * over plain numbers, needing no world, no canvas and no rendering device.
 * ACataclysmHUD is then thin enough to read in one sitting.
 *
 * WHY A CANVAS HEADS-UP DISPLAY RATHER THAN UMG. This needs no module
 * dependency, no widget Blueprint and no content asset of any kind: AHUD::
 * DrawText falls back to GEngine->GetMediumFont() when given no font, and
 * AHUD::Project does the world-to-screen arithmetic an overhead bar needs. A
 * widget would have cost UMG, Slate and SlateCore on the module, plus either a
 * binary .uasset that cannot be reviewed in a diff -- issue #140 records the
 * editor rewriting those on open -- or a C++ UUserWidget whose widget tree has
 * to be built in RebuildWidget, because NativeConstruct runs after the Slate
 * root has already been captured and a tree built there never appears.
 *
 * THIS IS THE COMBAT READABILITY LAYER, NOT THE INTERFACE. Issue #49 builds the
 * designed heads-up display -- the empire status bar, the skill slots with
 * their cooldowns, the minimap -- and those need layout, text scaling and
 * localisation, which is UMG work. This is expected to be replaced by it.
 * Issue #650.
 */
UCLASS()
class CATACLYSM_API UCataclysmCombatOverlay : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ How long a number lives and how it behaves while it does.

	/** Seconds a floating number lasts before it is dropped. */
	static constexpr float NumberLifetimeSeconds = 1.1f;

	/** How far up the screen a number travels over its whole life, in pixels. */
	static constexpr float NumberRisePixels = 55.0f;

	/** The share of a number's life it stays fully opaque for. */
	static constexpr float NumberOpaqueShare = 0.55f;

	/** A damage over time tick is drawn at this share of a blow's size. */
	static constexpr float DamageOverTimeScale = 0.7f;

	/**
	 * A critical strike is drawn at this multiple of an ordinary blow's size.
	 *
	 * SIZE IS THE SECOND CHANNEL AND NOT THE ONLY ONE, since issue #668. It was
	 * the only one, alongside an exclamation mark on the figure, and the project
	 * owner played that and reported "you really can't tell the difference
	 * between a crit and a normal hit even though it's slightly bigger and has an
	 * exclamation point". The mark is gone and CriticalStrikeHex now carries the
	 * distinction; this stays because a critical strike being larger is right,
	 * not because it is sufficient.
	 *
	 * IT MULTIPLIES WITH THE TICK SIZE ABOVE rather than replacing it. Nothing in
	 * the game can produce that combination today, because a damage over time
	 * tick can never critically strike, but the arithmetic is written so that if
	 * one ever could the number would still be smaller than a blow.
	 */
	static constexpr float CriticalStrikeScale = 1.35f;

	/**
	 * How many numbers may wait to be drawn at once.
	 *
	 * A CEILING RATHER THAN A HOPE. Issue #563 measured one player attack
	 * producing seven impacts in five seconds, and an area skill landing on
	 * twenty enemies produces twenty numbers in one frame. The oldest are
	 * dropped first, so the ceiling costs the numbers already fading rather
	 * than the ones just landed.
	 */
	static constexpr int32 MaxNumbersWaiting = 96;

	/** Centimetres above the top of a creature that its bar and numbers sit. */
	static constexpr float AnchorMarginCm = 30.0f;

	//~ Colours, as six-digit sRGB hex, so they read straight against the tables
	//~ in section XIII of docs/Cataclysm_GDD_v2.md. The leading hash is dropped
	//~ because FColor::FromHex is what parses them, matching the constants on
	//~ ACataclysmTelegraphMarker.

	/**
	 * The near-black behind every bar, and it is the telegraph's outer ring
	 * value on purpose.
	 *
	 * FOR THE SAME REASON THE TELEGRAPH HAS ONE. The design's readability
	 * guarantee is that a world surface stays under 30% brightness and an
	 * effect's primary stays over 60%, which holds a fill against a floor but
	 * says nothing about a thin bar seen against lava, against a gold Celestial
	 * wall, or against another creature. A dark backing under the whole bar
	 * makes the contrast a property of the bar rather than of wherever it
	 * happens to be standing.
	 */
	static const TCHAR* BarBackingHex;

	/**
	 * Health, and NOT the telegraph's #FF3020.
	 *
	 * THAT RED IS RESERVED FOR THE WHOLE GAME. Section XIII of
	 * docs/Cataclysm_GDD_v2.md gives it to the attack marker under the heading
	 * "There is one telegraph colour for the whole game", and says it does not
	 * vary by Cataclysm, by damage type or by enemy, because the marker has to
	 * mean "this ground is about to hurt" everywhere. A health bar wearing
	 * the same red weakens the only signal that has to survive every
	 * environment. This is a darker, less saturated red that still reads as
	 * health and cannot be mistaken for a warning.
	 */
	static const TCHAR* HealthFillHex;

	/**
	 * An energy shield, and the same blue a shield-absorbed number is drawn in.
	 *
	 * DELIBERATELY NOT DEATH'S #8FD8EC. That pale icy blue is a damage type's
	 * primary, and the design permits the rarity and damage-type palettes to
	 * overlap only because "the two palettes never share a surface". A bar over
	 * a creature is a third surface, so it takes a blue of its own.
	 */
	static const TCHAR* ShieldFillHex;

	/**
	 * Mana.
	 *
	 * A BAR AT ALL BECAUSE ITS ABSENCE HID A BUG. Issue #653: mana was spent and
	 * never returned, so every ability became permanently refused, and it was
	 * reported from play as "sometimes all of my abilities just become disabled"
	 * rather than as "I have run out of mana" -- because nothing on screen said
	 * so. The design has listed mana as a player resource bar since section XIII
	 * was written. It was left out of the first version of this overlay and
	 * should not have been.
	 *
	 * DELIBERATELY NOT VOID'S #B978F5, which is a damage type's primary, and not
	 * the shield's blue either, because a player reading two bars in a corner
	 * has to tell them apart at a glance.
	 */
	static const TCHAR* ManaFillHex;

	/** A figure that reached health. Warm near-white. */
	static const TCHAR* ReachedHealthHex;

	/** A figure a shield or mana pool swallowed. Matches the shield bar. */
	static const TCHAR* AbsorbedHex;

	/** An evaded, blocked or wholly mitigated hit. Mid grey. */
	static const TCHAR* NothingThroughHex;

	/**
	 * A critical strike that got through. Amber orange.
	 *
	 * COLOUR NOW SAYS TWO THINGS, AND THAT IS A REVERSAL DECIDED BY PLAYING IT.
	 * When the numbers were built under issue #518 colour was reserved to say
	 * only where the damage went, and colouring numbers by damage type was
	 * rejected to keep it that way. A critical strike was therefore marked by
	 * size and by an exclamation mark instead. The project owner played that on
	 * 2026-08-17 and reported that "you really can't tell the difference between
	 * a crit and a normal hit". Issue #668.
	 *
	 * WHAT IT COSTS, STATED RATHER THAN DISCOVERED LATER. For a critical strike,
	 * colour no longer separates a hit that reached health from one an energy
	 * shield absorbed. The text still separates them -- "12 (+30)" against "12"
	 * -- so the information moved rather than went. Diablo 4 makes the same
	 * trade, white for an ordinary hit and yellow for a critical one.
	 *
	 * ORANGE RATHER THAN RED, AND THAT IS NOT A TASTE. #FF3020 is reserved
	 * game-wide for the attack telegraph and nothing else may claim it. A red
	 * number beside a red telegraph is exactly the confusion the reservation
	 * exists to stop, so this sits far enough away in the green channel to be
	 * told apart at a glance, and a test measures the distance rather than
	 * checking the two strings differ.
	 */
	static const TCHAR* CriticalStrikeHex;

	/** Parses one of the constants above. Black when it will not parse. */
	static FLinearColor ColourFromHex(const TCHAR* Hex);

	//~ The live switches. All three default to on.

	/** Whether floating damage numbers are drawn at all. */
	static bool DamageNumbersEnabled();

	/** Whether a bar is drawn over damaged creatures. */
	static bool OverheadBarsEnabled();

	/** Whether the player's own health is drawn on the frame. */
	static bool PlayerVitalsEnabled();

	//~ What to draw, decided.

	/**
	 * Whether a landed hit is worth putting a number on.
	 *
	 * ALMOST EVERYTHING IS, and that is the opposite of the rule the impact
	 * particle follows. UCataclysmImpactEffect::ShouldDrawFor refuses a hit
	 * that never connected, because a burst appearing for it would make the
	 * burst mean "an attack happened" rather than "that landed". A number is
	 * the other way round: a hit that resolved to nothing is exactly the case
	 * nobody can currently see, and it is what issues #483 and #644 are about
	 * -- defensive layers combining to stop everything. So an evaded hit says
	 * so, and a hit armour and resistance took to nothing shows a zero.
	 *
	 * THE ONE REFUSAL IS A CORPSE. UCataclysmDamageCalculation::Resolve ends
	 * with FMath::Min(Damage, Vitals->GetHealth()), so every hit on something
	 * already at zero health deals nothing. Issue #570 counted fifty-six of
	 * those arriving over seventy seconds in one session. A killing blow is not
	 * this case and must still be drawn: it leaves health at zero but dealt
	 * real damage getting there, which is why the test is on both.
	 */
	static bool ShouldShowNumberFor(const FCataclysmIncomingHit& Hit,
									const FCataclysmDamageResult& Outcome,
									float HealthRemaining);

	/**
	 * What the number says.
	 *
	 * A WORD WHEN NOTHING GOT THROUGH AND A FIGURE WHEN SOMETHING DID, which
	 * makes the text itself a second channel beside the colour. The design
	 * requires that: "colour is still not the only channel", because a player
	 * who cannot separate two hues still has to be able to separate two
	 * outcomes.
	 *
	 * A hit that reached health past a shield says both figures, health first,
	 * so one number covers one hit rather than two numbers racing each other up
	 * the screen.
	 *
	 * IT SAYS NOTHING ABOUT A CRITICAL STRIKE, and briefly did. Issue #649 added
	 * an exclamation mark to a critical strike's figure; the project owner played
	 * it and could not tell a critical strike from an ordinary hit, so the mark
	 * came out and the colour took the job. Issue #668.
	 */
	static FString TextFor(const FCataclysmDamageResult& Outcome);

	/**
	 * Whether this outcome is drawn as a critical strike.
	 *
	 * BOTH THAT IT WAS ONE AND THAT SOMETHING GOT THROUGH. The roll happens
	 * before block, armour and resistance, so a critical strike can still be
	 * stopped dead by a well-defended target -- and a grey, oversized "0!" would
	 * say the opposite of what happened. The size and the mark ask this same
	 * question so the two can never disagree.
	 */
	static bool ShowsCriticalStrike(const FCataclysmDamageResult& Outcome);

	/** What colour that text is drawn in. Three cases, described above. */
	static FLinearColor ColourFor(const FCataclysmDamageResult& Outcome);

	/**
	 * One damage figure as it is printed: rounded, but never rounded away.
	 *
	 * ANY DAMAGE AT ALL PRINTS AT LEAST 1, and that is the whole reason this is
	 * a function rather than a call to FMath::RoundToInt. Rounding alone reads
	 * 0.42 as "0", and "0" already means something specific here -- armour,
	 * resistance and flat reduction stopped the whole blow. So a hit that did
	 * something would say it did nothing, which is the one thing a damage number
	 * must never do.
	 *
	 * IT IS NOT A RARE CASE. UCataclysmDamageCalculation::Resolve ends with
	 * FMath::Min(Damage, Vitals->GetHealth()), so a killing blow's figure is
	 * exactly the target's remaining health however large the blow was, and
	 * health is an unrounded float that is only clamped. A creature sitting on
	 * 0.3 health is alive, hittable, and killed by a blow that would have
	 * printed "0".
	 *
	 * IT IS ALSO WHAT KEEPS THE TEXT AND THE COLOUR AGREEING. ColourFor asks
	 * whether the raw figure is above zero; without this, the text could take
	 * the "nothing got through" branch while the colour said the blow reached
	 * health, and the two channels the design requires would contradict each
	 * other on the same number.
	 *
	 * Returns 0, and only 0, when nothing at all arrived.
	 */
	static int32 FigureFor(float Amount);

	/**
	 * How large. A damage over time tick is drawn smaller than a blow, and a
	 * critical strike that got through is drawn larger.
	 *
	 * IT NEEDS BOTH ARGUMENTS because the two facts live on different structs:
	 * whether a hit was a tick is a property of the hit, and whether it landed as
	 * a critical strike is decided while it is being resolved.
	 */
	static float ScaleFor(const FCataclysmIncomingHit& Hit,
						  const FCataclysmDamageResult& Outcome);

	/**
	 * Whether a creature gets a bar over its head at this instant.
	 *
	 * NOTHING OVER AN UNDAMAGED CREATURE. That is the project owner's decision
	 * and it is what Path of Exile does even with its own "Show Mini Life Bars
	 * on Enemies" setting enabled: a bar appears once an enemy has been damaged
	 * or moused over, and not before. It keeps the design's deliberately dark,
	 * low-light world from being papered over with interface, and it makes the
	 * bar mean "this fight has started" rather than "there is a creature here".
	 *
	 * Nothing over a corpse either. An enemy destroys itself on the tick after
	 * it dies, so without this a bar at zero flashes for one frame.
	 */
	static bool ShouldShowBarFor(float Health, float MaxHealth);

	/** How much of a bar is filled, 0 to 1. Zero when the maximum is not real. */
	static float BarFractionFor(float Current, float Maximum);

	/**
	 * Whether an actor is a candidate for an overhead bar at all.
	 *
	 * NOT THE PLAYER'S OWN PAWN, which has its bar on the frame instead, and
	 * nothing marked dead. Separate from ShouldShowBarFor because this asks
	 * about the actor and that asks about the numbers.
	 */
	static bool IsOverheadBarCandidate(const AActor* Actor,
									   const AActor* LocalPlayerPawn);

	/**
	 * How far above an actor's own location its bar and numbers sit.
	 *
	 * READ OFF THE ACTOR'S BOUNDS rather than fixed, because a Brute and an Imp
	 * are not the same height and a fixed offset puts one inside a head and the
	 * other in mid air. An actor with no components has no bounds and gets the
	 * margin alone, which is the honest answer for something with no body.
	 */
	static float AnchorHeightFor(const AActor* Actor);

	/** How far up the screen a number has travelled, in pixels. */
	static float RisePixelsFor(float Age);

	/** How opaque a number is at a given age. Zero once it has expired. */
	static float FadeFor(float Age);

	/**
	 * Reads health and maximum health off any actor that has an ability system.
	 *
	 * WORKS FOR PLAYER, ENEMY AND MINION ALIKE, because it goes through
	 * UAbilitySystemGlobals::GetAbilitySystemComponentFromActor, which asks the
	 * actor's IAbilitySystemInterface rather than assuming where the component
	 * lives. The player's is on its player state and an enemy's is on its pawn,
	 * and this does not have to know that.
	 *
	 * Returns false and writes nothing when the actor has no ability system or
	 * no vital attribute set, which includes every actor before its ability
	 * system has been initialised.
	 */
	static bool VitalsOf(const AActor* Actor, float& OutHealth,
						 float& OutMaxHealth);

	/** The same for an energy shield. False when the actor has no shield set. */
	static bool ShieldOf(const AActor* Actor, float& OutShield,
						 float& OutMaxShield);

	/** The same for mana. False when the actor has no vital attribute set. */
	static bool ManaOf(const AActor* Actor, float& OutMana, float& OutMaxMana);

	/**
	 * Hands a landed hit to the heads-up display so it can be drawn.
	 *
	 * THE AVATAR IS THE CALLER'S PROBLEM, not this function's. It is passed the
	 * actor already resolved by UCataclysmImpactEffect::ActorToDrawOn, because
	 * an attribute set's GetOwningActor answers with the ability system's OWNER
	 * and for the player that is the player state, which is not placed in the
	 * world and reports the origin. Issue #562 drew every blow an enemy landed
	 * on the player in the middle of the level for exactly that reason, and
	 * issue #565 was the same mistake again in the death path.
	 *
	 * Does nothing when there is no world, no player controller, no heads-up
	 * display, or the numbers are switched off. A dedicated server and every
	 * automation test take that path.
	 */
	static void Record(const AActor* Struck, const FCataclysmIncomingHit& Hit,
					   const FCataclysmDamageResult& Outcome);
};
