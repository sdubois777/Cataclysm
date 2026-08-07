// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "CataclysmBruteCharacter.generated.h"

/**
 * The Brute: heavily armoured slow melee, meant to be outmanoeuvred.
 *
 * The first of the seven Demonic vertical slice enemies (issue #39) to exist as
 * its own class, and the first character in this project with real art.
 *
 * WHY A C++ SUBCLASS RATHER THAN A BLUEPRINT. Three project rules point the same
 * way. game/README.md says Content/ holds generated assets rather than authored
 * ones; .gitignore excludes the Paragon folders under game/Content, so a
 * Blueprint saved beside the art would be dropped by git without a word; and
 * issue #370 question 3,
 * which asks where authored enemy assets should live, is open and waiting on the
 * operator. A C++ class is reviewable text, needs no answer to that question,
 * and is the same shape ACataclysmMinion already uses to override its capsule,
 * speed and turn rate.
 *
 * WHAT IT DELIBERATELY DOES NOT DO. The Stomp -- the 3.5 metre, 360 degree,
 * 1.5 second stun on a 5 second cooldown that issue #351 designed -- is not
 * here. It has nothing to attach to: there is no stun in the project, no Stun
 * row in game/Data/StatusEffects.csv, and no wind-up state in
 * ECataclysmBrainAction for the telegraph to sit in. Issue #371 covers that.
 * This class is the Slam, the body and the movement.
 */
UCLASS()
class CATACLYSM_API ACataclysmBruteCharacter : public ACataclysmEnemyCharacter
{
	GENERATED_BODY()

public:
	ACataclysmBruteCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Chooses the standing or walking animation and sets the walk's play rate.
	 *
	 * PUBLIC SO A TEST CAN STEP IT WITHOUT A TICKING WORLD, the same reason
	 * ResolveBody is public. Safe to call when the mesh has no art: it returns
	 * immediately.
	 */
	void DriveLocomotion();

	/**
	 * Which animation belongs at a given ground speed, and how fast to play it.
	 *
	 * SEPARATE FROM DriveLocomotion ON PURPOSE. This is the decision; the other
	 * is the application. A test can call this with any speed and get a definite
	 * answer, in any world, with no ticking and no animation instance -- which
	 * matters, because applying an animation needs a component that has run
	 * InitAnim and a synthetic test world does not always give one.
	 *
	 * @param GroundSpeedCmPerSecond  Horizontal speed. Vertical is ignored:
	 *   falling is not walking.
	 * @param OutPlayRate  Set to the rate the returned animation should play at.
	 * @return the standing or the walking animation, or null if neither loaded.
	 */
	UAnimSequence* AnimationForGroundSpeed(float GroundSpeedCmPerSecond,
										   float& OutPlayRate) const;

	/** What it plays standing still. Null until ResolveBody runs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> IdleAnimation;

	/** What it plays moving. Null until ResolveBody runs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> WalkAnimation;

	//~ The designed numbers.
	//
	// EVERY ONE OF THESE IS A SECOND COPY. The first copy is the Brute entry in
	// ARCHETYPES in sim/cataclysm_sim/enemy_stats.py and ATTACK_REACH in
	// sim/cataclysm_sim/enemy_abilities.py, and those are authoritative.
	// tools/tests/test_brute_matches_the_model.py fails when these drift from
	// them, which is the only thing keeping the two honest. The power model has
	// silently drifted from its own source twice before; see CLAUDE.md.

	/** Seconds between attacks. enemy_stats.py, attack_interval=2.8. */
	static constexpr float DesignedAttackIntervalSeconds = 2.8f;

	/**
	 * How close it must be to hit, centre to centre, in centimetres.
	 *
	 * enemy_abilities.py, ATTACK_REACH["Brute"] = 0.90 metres. That figure is
	 * not arbitrary: ring_distance() in the same file makes contact reach
	 * PLAYER_BODY_RADIUS + body_radius, which is 0.42 + 0.48. Those are exactly
	 * the player's 42 cm capsule radius in CataclysmPlayerCharacter.cpp and the
	 * enemy's 48 cm in CataclysmEnemyCharacter.cpp, so the model and the engine
	 * already agree and this is the distance at which the two capsules touch.
	 */
	static constexpr float DesignedMeleeReachCm = 90.0f;

	/** Centimetres per second. enemy_stats.py, move_speed=2.5 metres. */
	static constexpr float DesignedWalkSpeedCmPerSecond = 250.0f;

	/**
	 * Degrees of yaw per second. enemy_stats.py, turn_rate_degrees=180.0.
	 *
	 * The number that makes "can be outmanoeuvred" real rather than flavour.
	 * Every other enemy turns at 480. A player circling at the Brute's own reach
	 * sweeps 223 degrees per second even in the slowest Demonic class, so
	 * anything below that can be got behind by every build.
	 */
	static constexpr float DesignedTurnRateDegreesPerSecond = 180.0f;
	//~ End designed numbers

	/**
	 * Collision radius in centimetres. Deliberately the same 48 as every other
	 * enemy, and NOT the Rampage mesh's width.
	 *
	 * The art is much wider than this. Rampage's own physics asset,
	 * Rampage_Extents, models its torso as a sphere of radius 82 cm. Using that
	 * as the capsule would put the closest the Brute could stand to the player
	 * at 82 + 42 = 124 cm, which is greater than the 90 cm reach above, so the
	 * Brute would press against the player and never once attack.
	 *
	 * So the visual and the collision are decoupled, which is ordinary practice
	 * and is why the mesh overhangs the capsule. The measured 0.82 m belongs to
	 * issue #366, which decides what each enemy's body_radius should be; it is
	 * not something an art import gets to change as a side effect.
	 */
	static constexpr float BruteCapsuleRadius = 48.0f;

	/**
	 * Half-height in centimetres, raised from the base enemy's 80 because the
	 * Rampage mesh is 221.2 cm tall (measured from its skeletal mesh bounds).
	 * 110 puts the capsule bottom at its feet and the top at its head.
	 *
	 * Half-height does not enter the reach comparison. See the note on
	 * ACataclysmEnemyController::Think about horizontal distance.
	 */
	static constexpr float BruteCapsuleHalfHeight = 110.0f;

	/**
	 * Where the art comes from.
	 *
	 * SOFT PATHS, RESOLVED IN BeginPlay, NOT ConstructorHelpers. The Paragon
	 * packs are excluded from git by .gitignore, so on a fresh clone, in CI, and
	 * in every other worktree these assets are absent. A constructor-time
	 * FObjectFinder miss fires during module load and is noisy; a soft load in
	 * BeginPlay can warn once and fall back to the placeholder cylinder, which
	 * is what this does.
	 */
	static const TCHAR* BodyMeshPath;

	/**
	 * The two animations this enemy plays, and why it plays them itself rather
	 * than through the Paragon animation blueprint that ships beside the mesh.
	 *
	 * THE PACK'S ANIMATION BLUEPRINT DOES NOT WORK ON THIS CHARACTER, MEASURED
	 * RATHER THAN ASSUMED. Read at runtime on 2026-08-07 from a live
	 * Play-In-Editor session, on the animation instance attached to a Brute the
	 * controller had reported as Chasing:
	 *
	 *     speed          = 0
	 *     isAccelerating = false
	 *     character      = None
	 *
	 * `character` is the blueprint's own reference to the pawn it animates. It
	 * fills that by casting the pawn owner to the class it was written for,
	 * which is the pack's own RampagePlayerCharacter blueprint. An
	 * ACataclysmBruteCharacter is not that class, the cast fails, and every
	 * value the graph derives from it stays at zero for ever. The body still
	 * moves, because the character movement component moves it, so what you see
	 * is a Brute sliding across the floor in a fixed pose.
	 *
	 * The pose it holds is the all-fours one, because Rampage has two stances
	 * and the graph's entry state is the quadruped locomotion at zero speed.
	 * At any distance that reads as the creature lying on the ground.
	 *
	 * So the mesh is driven directly instead. Two animations, chosen in Tick,
	 * with the walk's play rate scaled to ground speed. Both are the upright
	 * "Biped" variants, matching the upright attack animations this enemy will
	 * use. Issue #374 covers replacing this with an animation blueprint written
	 * for this project, which is the proper answer and needs #370 settled first.
	 */
	static const TCHAR* IdleAnimationPath;
	static const TCHAR* WalkAnimationPath;

	/**
	 * Below this ground speed the Brute is standing rather than walking, in
	 * centimetres per second.
	 *
	 * A JUDGEMENT. Not zero, because a character that has been told to stop
	 * keeps a little residual velocity for a frame or two while friction takes
	 * it down, and comparing against zero makes it flicker between standing and
	 * walking every time it arrives.
	 */
	static constexpr float WalkingThresholdCmPerSecond = 10.0f;

	/**
	 * The ground speed the walk animation was authored for, in centimetres per
	 * second. The play rate is the Brute's real speed divided by this.
	 *
	 * A BY-EYE NUMBER, AND THE ONLY ONE IN THIS CLASS THAT IS. Jog_Biped_Fwd has
	 * no root motion -- checked, `bEnableRootMotion` is False -- so there is no
	 * distance stored in the asset to derive the authored speed from. It has to
	 * be set by watching whether the feet slide.
	 *
	 * Editable so that tuning it does not need a rebuild.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Cataclysm|Enemy",
			  meta = (ClampMin = "1.0"))
	float AuthoredWalkSpeedCmPerSecond = 500.0f;

	/** Play rate floor. Below this the animation reads as frozen. */
	static constexpr float MinimumPlayRate = 0.2f;

	/** Play rate ceiling. Above this it reads as a blur. */
	static constexpr float MaximumPlayRate = 2.5f;

	/**
	 * Puts the Rampage mesh on, or logs why it could not and keeps the
	 * placeholder cylinder. Returns true when the skeletal mesh resolved.
	 *
	 * PUBLIC SO A TEST CAN CALL IT RATHER THAN INFER IT. BeginPlay calls this,
	 * but whether BeginPlay runs at all depends on how the world was made, and a
	 * test that spawns into a synthetic world and then checks the mesh cannot
	 * tell "the art is missing" apart from "BeginPlay did not fire". Calling it
	 * directly and reading the return value distinguishes the two, and it is
	 * safe to call twice: assigning the same mesh again is a no-op.
	 *
	 * @param bIncludeAnimation  Pass false to bind the mesh without loading or
	 *   starting the standing and walking animations. Tests use this to check
	 *   the mesh binding without pulling in animation assets they do not need.
	 */
	bool ResolveBody(bool bIncludeAnimation = true);
};
