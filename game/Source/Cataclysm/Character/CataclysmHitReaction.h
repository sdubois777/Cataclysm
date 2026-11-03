// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmHitReaction.generated.h"

struct FGameplayEffectSpec;

/**
 * Which way a blow arrived from, relative to where the creature struck is facing.
 *
 * THE ORDER IS THE ORDER OF EVERY CREATURE'S FOUR CLIPS. `ACataclysmEnemyCharacter::HitReactionClipPaths` and
 * `HitReactionAnimations` are indexed by this, so Front is entry 0 and Right is entry 3. Appending a value is safe;
 * inserting one would move every creature's clips.
 */
UENUM(BlueprintType)
enum class ECataclysmHitDirection : uint8
{
	/** The blow came from in front of the creature. */
	Front,

	/** From behind it. */
	Back,

	/** From its left. */
	Left,

	/** From its right. */
	Right,
};

/**
 * Why a struck creature did not flinch, or `None` when it did. Issue #745.
 *
 * IN THE ORDER THEY ARE ASKED, which is `UCataclysmHitReaction::RefusalFor`'s. The first that applies is the one
 * reported, so a dead creature that was also winding up reports `Dead`.
 *
 * WHY A REASON RATHER THAN A YES OR NO. A test that only reads "no flinch" passes whatever stopped it; a creature
 * refused for being dead when the test meant to show a wind-up refusing it would hide a broken wind-up rule. Every
 * refusal test reads the reason as well as the count.
 */
UENUM(BlueprintType)
enum class ECataclysmFlinchRefusal : uint8
{
	/** Nothing refused it: the creature flinched. */
	None,

	/** The creature names no hit-reaction clips: every creature but the seven Demonic ones, and the plain dummy. */
	NoHitReactions,

	/** It is dead. A creature at zero health plays its death and nothing else. */
	Dead,

	/** Stunned or knocked down: `UCataclysmSkillEffects::CannotAct`. It is held in its own reaction already. */
	CannotAct,

	/** Travelling down a charge. Ruled 2026-11-02, (e). */
	Charging,

	/** Committed to a telegraphed ability: its brain is winding one up, or it stands in Inferno Charge's channel. */
	WindingUp,

	/** A flinch is still playing. The clip's own length is the rate limit; ruled 2026-11-02, (b). */
	AlreadyFlinching,

	/** Its ordinary swing's blow is still waiting to land, or a one-shot of its own -- a swing, a cast, a roar -- still
	 *  holds its body. Ruled 2026-11-02, (a). */
	Attacking,
};

/**
 * What a creature knows about itself at the moment a blow lands, for `UCataclysmHitReaction::RefusalFor`.
 *
 * PLAIN FACTS, GATHERED BY THE CREATURE. `ACataclysmEnemyCharacter::FlinchFactsNow` fills one from the creature, its
 * brain and the ability system; the rule that weighs them is a static here, so a test can ask it about every
 * combination without building a fight for each.
 */
struct FCataclysmFlinchFacts
{
	bool bHasHitReactions = false;
	bool bDead = false;
	bool bCannotAct = false;
	bool bCharging = false;
	bool bWindingUp = false;
	bool bFlinching = false;
	bool bAttacking = false;
};

/**
 * What being hit looks like, decided, and none of the playing. Issue #745.
 *
 * WHY IT EXISTS. A creature that was hit showed nothing: it kept walking, or kept standing, in exactly the pose it
 * was in, and the only thing on screen saying the blow landed was the floating number and the impact burst. Every
 * Demonic creature's art pack ships four directional hit-reaction clips and nothing loaded them.
 *
 * THE SAME SPLIT `UCataclysmEnemyDeath` MAKES, for the same reason: an automation test cannot watch a clip play under
 * `-nullrhi`. What a test CAN check is which of the four clips a blow from a given side chooses, and whether a creature
 * in a given state flinches at all, so both of those are static functions over plain values here. The playing is
 * `ACataclysmEnemyCharacter::FlinchFrom`, through each creature's own `PlayOneShot`.
 */
UCLASS()
class CATACLYSM_API UCataclysmHitReaction : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** How many directions, and so how many clips each dressed creature names. */
	static constexpr int32 DirectionCount = 4;

	/**
	 * Which of the four clips a blow from `ToSource` plays on a creature facing `Facing`. Ruled 2026-11-02, (c).
	 *
	 * THE NEAREST OF FOUR NINETY-DEGREE QUARTERS, centred on the creature's front, back, left and right. A blow up to
	 * 45 degrees either side of straight ahead is Front, and so on round.
	 *
	 * EXACTLY ON A DIAGONAL GOES TO FRONT OR BACK, a judgement. A boundary has to belong to one side, and a blow at 45
	 * degrees is as much in front as beside; the front and back clips are the two a player reads most easily.
	 *
	 * IN THE FLOOR PLANE. Height is dropped from both vectors, so a blow from a ledge above reads the same as one from
	 * level ground at the same place, the rule `UCataclysmSkillEffects::IsBehind` already follows.
	 *
	 * FRONT WHEN THERE IS NO DIRECTION TO JUDGE: a source standing exactly where the creature stands, or a creature with
	 * no facing. Something has to play, and the front clip is the neutral one.
	 *
	 * @param Facing    where the creature is pointed; its actor forward vector. Need not be unit length.
	 * @param ToSource  from the creature to where the blow came from. Need not be unit length.
	 */
	static ECataclysmHitDirection DirectionOf(const FVector& Facing, const FVector& ToSource);

	/**
	 * Why a creature with these facts does not flinch, or `None` when it does. Issue #745, the rulings of 2026-11-02.
	 *
	 * ASKED IN `ECataclysmFlinchRefusal`'S ORDER: no clips named, dead, unable to act, charging, winding up, a flinch
	 * still playing, attacking. The order only decides which reason is REPORTED; any one of them refuses.
	 */
	static ECataclysmFlinchRefusal RefusalFor(const FCataclysmFlinchFacts& Facts);

	/**
	 * Where the blow `Spec` came from, for choosing which way the struck creature flinches. Ruled 2026-11-02, (c).
	 *
	 * THE BLOW'S OWN SOURCE WHEN THE DAMAGE PATH CARRIES ONE, AND THE ACTOR THAT DEALT IT OTHERWISE, in this order:
	 *
	 *   1. the point the blow was struck from, when its effect carries the stamp
	 *      `UCataclysmSkillEffects::StruckFromSetDataName` -- a gadget's copy of its summoner's skill, the one blow in
	 *      the game struck from somewhere other than its instigator. `MetresFromWhereItWasStruck` reads the same stamp.
	 *   2. the effect context's source object, when it is an actor: the minion whose blow was dealt in its summoner's
	 *      name (`FCataclysmHitDelivery::DealtBy`). The summoner may be across the room; the minion is the one swinging.
	 *   3. the effect causer, which every blow in this project sets to its instigator (`AddInstigator(I, I)`), so this
	 *      is the instigator's location: the character who swung, cast or fired.
	 *   4. the struck creature's own place, when the blow names nobody, which `DirectionOf` reads as Front.
	 *
	 * NO HIT RESULT IS READ, because no blow in this project carries one: `UCataclysmImpactEffect::ImpactLocationFor`
	 * already falls back to the actor's location for every blow, and nothing calls `AddHitResult`. A projectile's blow
	 * names the character who fired it, so a shot reads as coming from the shooter, which is the projectile's
	 * direction of travel for every straight shot.
	 */
	static FVector WhereTheBlowCameFrom(const FGameplayEffectSpec& Spec, const AActor* Struck);
};
