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
 * WHY A C++ SUBCLASS RATHER THAN A BLUEPRINT. It is reviewable text rather than
 * a binary asset, and it is the same shape ACataclysmMinion already uses to
 * override its capsule, speed and turn rate.
 *
 * THAT IS NOW A PREFERENCE, NOT A CONSTRAINT. When this class was written there
 * was nowhere agreed to save a Blueprint: .gitignore excludes the Paragon
 * folders, so one saved beside the art would have been dropped by git without a
 * word, and issue #370 had not settled where authored enemy assets belong. It
 * has since: game/Content/Enemies/<Cataclysm>/<Enemy>/, so this enemy's is
 * game/Content/Enemies/Demonic/Brute/. game/docs/content-layout.md is the
 * convention. A Blueprint here would be legitimate; there has been no reason to
 * make one.
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
										   float& OutPlayRate,
										   bool bChasing = false) const;

	/** Whether the brain driving this Brute says it is chasing something. */
	bool IsChasing() const;

	/** What it plays standing still. Null until ResolveBody runs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> IdleAnimation;

	/** What it plays moving with nothing to chase. Null until ResolveBody runs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> WalkAnimation;

	/**
	 * What it plays moving toward something it has noticed. Null until
	 * ResolveBody runs, and null is not a fault: the walk is used instead.
	 *
	 * CHOSEN BY BRAIN STATE, NOT BY SPEED, and it has to be, because the Brute
	 * moves at the same 250 cm/s whether it is wandering or coming at you. Its
	 * movement speed is a designed number that makes "can be outmanoeuvred"
	 * true, so this reads the controller's state instead.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> ChaseAnimation;

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
	 * How far it notices the player, in centimetres. Replaces the base enemy's
	 * 1500.
	 *
	 * DERIVED FROM THE BRUTE'S OWN TWO DESIGNED NUMBERS, and it is the distance
	 * it covers in one attack cycle:
	 *
	 *     move_speed x attack_interval = 250 cm/s x 2.8 s = 700 cm
	 *
	 * So noticing the player means a fight starts within one cycle, rather than
	 * beginning a walk it will not finish. Both inputs are authoritative and
	 * pinned: enemy_stats.py gives move_speed 2.5 and attack_interval 2.8, and
	 * tools/tests/test_brute_matches_the_model.py fails when either drifts.
	 *
	 * IT IS ALSO EXACTLY TWICE THE STOMP'S TELEGRAPH RADIUS, which is the more
	 * useful way to read it. largest_telegraphed_radius(2.8) in
	 * enemy_abilities.py is 3.5 m, so 700 cm is 2 x 350. Walking the first half
	 * takes (700 - 350) / 250 = 1.4 s and the Stomp's wind-up is 1.4 s, so the
	 * time from noticing the player to a Stomp landing is exactly one attack
	 * interval. That is one property rather than two coincidences: it holds
	 * whenever the telegraph radius is half of speed times interval, which is
	 * what the telegraph rule makes it.
	 *
	 * WHY THE INHERITED 1500 WAS WRONG FOR THIS ENEMY RATHER THAN WRONG. It is
	 * derived -- docs/DECISIONS.md records it as "the same distance Subjugate
	 * reaches, which is the longest range the designed Demonic skills use", and
	 * Range=15 in game/Data/WeaponSkills.csv is indeed the longest. That
	 * reasoning is symmetry: a monster notices you from as far as you could hit
	 * it. It suits a caster. It does not suit a melee enemy that moves at
	 * 2.5 m/s against a player moving at 3.5 to 4.6, because such an enemy can
	 * never close a 15 metre gap against a player who does not want it closed.
	 * The same entry labels all of those figures judgements "expected to
	 * change".
	 *
	 * WHAT IT LOOKS LIKE IN THE SANDBOX, which is the check that matters most.
	 * ACataclysmGameMode spawns the Brute 1200 cm from the player start. At
	 * 1500 it notices the player at the instant the level opens and walks at
	 * them for ever, so it never roams and the roaming cannot be seen at all.
	 * At 700 it does not, so the first thing a player sees it do is wander.
	 *
	 * IT DOES NOT FOLLOW THAT A MOTIONLESS PLAYER IS SAFE, and the first draft
	 * of this comment wrongly implied it was. The roam circle reaches to
	 * 1200 - 600 = 600 cm from the player start, which is inside the 700 cm
	 * notice radius, so a Brute that happens to wander to the near edge of its
	 * circle notices a player who has not moved. That is the intended
	 * behaviour, not a leak: a creature patrolling ground near you should find
	 * you eventually.
	 *
	 * ONE ENEMY, NOT SEVEN. The design document states no notice radius for any
	 * enemy, and this figure is derived from numbers only the Brute has, so it
	 * says nothing about the other six. Issue #383 asks for the general rule.
	 */
	static constexpr float BruteNoticeRadiusCm = 700.0f;

	/**
	 * How far from where it spawned it wanders, in centimetres.
	 *
	 * BOUNDED BY THE LEVEL, NOT BY TASTE. The sandbox floor and its navigation
	 * bounds are 4000 cm across centred on the world origin, so nothing can
	 * path further than 2000 cm from the centre (FLOOR_EXTENT in
	 * tools/generate_input_assets.py). ACataclysmGameMode puts the Brute 1200
	 * cm out, leaving 800 cm to the bound. Recast insets the walkable surface
	 * by the agent radius, which for this capsule is 48 cm, so about 752 cm is
	 * really reachable. 600 leaves a margin of roughly 150 cm.
	 *
	 * ALSO SMALLER THAN THE NOTICE RADIUS ABOVE, and the property that buys is
	 * precise: a player standing at the anchor is inside the Brute's notice
	 * radius from anywhere in its roam circle. Roaming can therefore never take
	 * it somewhere that it would fail to see something standing on the spot it
	 * is guarding. 600 against 700 gives that with 100 cm to spare.
	 *
	 * An earlier version of this comment argued it in terms of "wandering out
	 * of the area it is meant to guard", which conflates two different
	 * measurements -- distance from the anchor and distance to a target -- and
	 * does not actually follow. The area it holds is roam plus notice, 1300 cm,
	 * and that is the figure that has to fit the level.
	 *
	 * The navigation system is asked first and will not return an unreachable
	 * point, so this figure is the bound on the fallback rather than on the
	 * usual case. See ACataclysmEnemyController::ChooseRoamTarget.
	 */
	static constexpr float BruteRoamRadiusCm = 600.0f;

	//~ Driven by ACataclysmEnemyController
	virtual float RoamRadiusCm() const override { return BruteRoamRadiusCm; }
	//~ End

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
	 * What it plays while chasing, and why this one is a starting point rather
	 * than an answer.
	 *
	 * NOT Sprint_Biped_Fwd, WHICH WAS THE OBVIOUS CHOICE AND IS USELESS.
	 * Measured on 2026-08-07: it returns identical bone poses to
	 * Jog_Biped_Fwd at every time sampled, with the same length, the same 29
	 * frames and the same 189 tracks. The pack appears to realise a sprint by
	 * playing the jog faster rather than by animating a second gait. Selecting
	 * between the two would look like nothing had changed. Issue #386.
	 *
	 * Run_Fwd IS a different clip: 0.667 seconds, 20 frames, 47 tracks against
	 * the jog's 189. It is the only genuinely distinct forward gait in the
	 * pack besides the four-legged set.
	 *
	 * ITS AUTHORED SPEED CANNOT BE MEASURED. tools/measure_animation_stride.py
	 * works by tracking the planted IK foot, and this clip carries no
	 * ik_foot_l track at all -- which is the measured reason it reads zero on
	 * every axis, where the documentation previously only inferred it. So the
	 * play rate has to be judged by eye, which is exactly how the walk's 225
	 * was arrived at, and Cataclysm.Brute.AuthoredChaseSpeed is how to do it
	 * without a rebuild.
	 *
	 * TO AUDITION A DIFFERENT ONE WITHOUT A REBUILD, set
	 * Cataclysm.Brute.ChaseAnimation to any animation's asset path. The
	 * four-legged gaits are the interesting alternatives, because a heavy
	 * demon dropping onto all fours reads as a charge in a way a faster walk
	 * does not:
	 *
	 *     /Game/ParagonRampage/Characters/Heroes/Rampage/Animations/Jog_Quad_Fwd
	 *     /Game/ParagonRampage/Characters/Heroes/Rampage/Animations/Sprint_Quad_Fwd
	 */
	static const TCHAR* ChaseAnimationPath;

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
	 * The ground speed the walk animation is treated as having been authored
	 * for, in centimetres per second. The play rate is the Brute's real speed
	 * divided by this, so at the designed 250 cm/s the walk plays at
	 * 250 / 225 = 1.11.
	 *
	 * SET BY EYE, ON 2026-08-07, BY THE PROJECT OWNER WATCHING IT WALK. That is
	 * the right authority for this one: the whole criterion is whether a planted
	 * foot appears to slide, which is a judgement about what a person sees.
	 *
	 * `tools/measure_animation_stride.py` estimates the same figure from the
	 * animation, and agrees to within 8%:
	 *
	 *     Jog_Biped_Fwd   242.9 cm/s   (estimate)   225 cm/s (by eye, in use)
	 *     Idle_Biped        0.0 cm/s   (the control: standing still)
	 *
	 * Idle_Biped reading zero is what makes the estimate credible at all.
	 *
	 * TWO EARLIER VALUES WERE WRONG, AND HOW THEY WERE WRONG IS WORTH KEEPING.
	 * The first, 500, was an outright guess and made the play rate 0.50, so the
	 * animation ran half speed while the body moved at full speed and the
	 * planted foot slid forwards. The second, 373.7, came from the measuring
	 * script when it averaged only the top quartile of its samples; that
	 * measures the peak of the foot's velocity curve rather than a
	 * representative speed, and was 66% high. The script now averages the whole
	 * cycle. See its module docstring.
	 *
	 * TO TUNE IT WITHOUT A REBUILD, use the console variable
	 * `Cataclysm.Brute.AuthoredWalkSpeed` during a play session; see
	 * EffectiveAuthoredWalkSpeed below.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Enemy",
			  meta = (ClampMin = "1.0"))
	float AuthoredWalkSpeedCmPerSecond = 225.0f;

	/**
	 * The authored walk speed actually in use: the console variable
	 * `Cataclysm.Brute.AuthoredWalkSpeed` when it is above zero, otherwise the
	 * measured property above.
	 *
	 * THE PROPERTY ALONE WAS NOT REACHABLE, WHICH IS WHY THIS EXISTS. It was
	 * EditDefaultsOnly, and there is no Blueprint subclass of this class to open
	 * -- issue #370, where authored enemy assets should live, is still open --
	 * so there was no class default to edit and the Details panel hides
	 * EditDefaultsOnly properties on a placed actor. Changing the number meant
	 * editing this header and rebuilding, which is a poor way to judge something
	 * by eye.
	 */
	float EffectiveAuthoredWalkSpeed() const;

	/**
	 * The ground speed the chase animation is treated as having been authored
	 * for, in centimetres per second.
	 *
	 * DEFAULTS TO THE BRUTE'S OWN SPEED, WHICH MAKES THE PLAY RATE EXACTLY 1.0
	 * -- that is, "play it as it was authored". That is deliberate rather than
	 * lazy. The walk's figure of 225 was arrived at by measuring and then
	 * correcting by eye, and two earlier guesses at it were wrong by 122% and
	 * 66%. This clip cannot be measured at all, so rather than guess a fourth
	 * number the default is the one choice that asserts nothing.
	 *
	 * Tune it with Cataclysm.Brute.AuthoredChaseSpeed while watching. A smaller
	 * number plays the animation faster.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Enemy",
			  meta = (ClampMin = "1.0"))
	float AuthoredChaseSpeedCmPerSecond = 250.0f;

	/** As EffectiveAuthoredWalkSpeed, for the chase animation. */
	float EffectiveAuthoredChaseSpeed() const;

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

private:
	/**
	 * The asset path the chase animation currently in ChaseAnimation was loaded
	 * from, so the console override only reloads when it actually changes.
	 *
	 * Also set when a load fails, or a mistyped path would be retried every
	 * frame for the rest of the session.
	 */
	FString LoadedChaseAnimationPath;
};
