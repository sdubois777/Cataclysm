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
	 * Hits the target, and plays the swing that goes with it.
	 *
	 * OVERRIDDEN HERE RATHER THAN DRIVEN FROM THE CONTROLLER so that the
	 * animation cannot drift apart from the damage: there is exactly one place
	 * a hit happens, and this is it.
	 */
	virtual void AttackTarget(AActor* Target) override;

	/**
	 * Seconds between swings: the designed interval, or the console override.
	 *
	 * `Cataclysm.Brute.AttackInterval` exists because how often an enemy swings
	 * is a judgement about how the game feels, and 2.8 seconds was reported as
	 * too long to play against. It defaults to zero, meaning use the design.
	 * See the console variable for why shortening it is not free: the telegraph
	 * rule sizes the Stomp's ground marker from this number.
	 */
	virtual float SecondsBetweenAttacks() const override;

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

	/**
	 * Sets the walk speed to the designed figure, or to the chase speed the
	 * console variable asks for while chasing.
	 *
	 * DEFAULTS TO CHANGING NOTHING. `Cataclysm.Brute.ChaseSpeed` is zero unless
	 * someone sets it, and zero means the designed 250 in both states. It
	 * exists because a running animation on a character that has not changed
	 * speed reads as running on the spot, and finding the speed that looks
	 * right is a judgement made by watching. The number that comes out of that
	 * belongs in the design, not here.
	 */
	void ApplyChaseSpeed();

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

	/** What it plays when it swings. Null until ResolveBody runs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> AttackAnimation;

	/**
	 * Start the swing animation, and hold it until it has played out.
	 *
	 * Called by the base class when an attack actually lands, so that the
	 * animation and the damage cannot drift apart. Does nothing when the art is
	 * absent, which is every fresh clone.
	 */
	void PlayAttackAnimation();

	/**
	 * Whether the swing animation is still playing and should not be replaced.
	 *
	 * WHY LOCOMOTION HAS TO ASK. DriveLocomotion runs every frame and picks an
	 * animation from ground speed and brain state. The Brute stops moving to
	 * attack, so on the very next frame that logic would choose the standing
	 * animation and cut the swing off after one frame. This is what stops it.
	 */
	bool IsSwinging() const;

	/**
	 * World seconds until which the swing animation owns the mesh. Read by
	 * tests. Zero means it is not swinging.
	 *
	 * PUBLIC FOR THE SAME REASON THE CONTROLLER'S ROAM DEADLINE IS: a synthetic
	 * test world is never ticked, so a test cannot wait a second for the swing
	 * to finish and has to read the deadline instead.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float SwingUntilSeconds = 0.0f;

	//~ The designed numbers.
	//
	// EVERY ONE OF THESE IS A SECOND COPY. The first copy is the Brute entry in
	// ARCHETYPES in sim/cataclysm_sim/enemy_stats.py and ATTACK_REACH in
	// sim/cataclysm_sim/enemy_abilities.py, and those are authoritative.
	// tools/tests/test_brute_matches_the_model.py fails when these drift from
	// them, which is the only thing keeping the two honest. The power model has
	// silently drifted from its own source twice before; see CLAUDE.md.

	/**
	 * Seconds between attacks. enemy_stats.py, attack_interval=1.6.
	 *
	 * WAS 2.8, CHANGED ON 2026-08-07 BY PLAYING IT. At 2.8 the Brute swung for
	 * one second and then stood still for one point eight, and the project
	 * owner's judgement was that an enemy that is not attacking might as well
	 * be scenery. 1.6 was found by trying values live with
	 * Cataclysm.Brute.AttackInterval.
	 *
	 * IT DOES NOT TOUCH THE STOMP, which was the obvious worry and is not one.
	 * An ability with a cooldown is telegraphed against that cooldown rather
	 * than the attack interval -- the rule is stated in section X of
	 * docs/Cataclysm_GDD_v2.md -- and the Stomp's 5 second cooldown allows a
	 * 7.35 metre marker. It draws 3.5, which was well inside the allowance
	 * before and still is. Its wind-up is unchanged at 1.4 seconds.
	 *
	 * WHAT IT DOES CHANGE is how hard this creature hits over time: the same
	 * damage per swing arrives 75% more often.
	 */
	static constexpr float DesignedAttackIntervalSeconds = 1.6f;

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

	/**
	 * Centimetres per second once it has noticed the player.
	 * enemy_stats.py, chase_speed=5.0 metres.
	 *
	 * TWICE ITS PATROL SPEED, and that is the whole of what makes it a threat
	 * rather than scenery. Set by the project owner on 2026-08-07 by playing
	 * it: a Brute that chased at the 250 it wanders at could be walked away
	 * from without thinking about it.
	 *
	 * IT IS STILL SLOWER THAN THE PLAYER, but not by the margin the design
	 * believes. ACataclysmPlayerCharacter never sets MaxWalkSpeed, so the
	 * player runs at Unreal's default 600 rather than the 350 to 460 the class
	 * table specifies -- issue #391. Against the 600 the game really uses, 500
	 * leaves a 100 cm/s margin. Against the designed figures it would be
	 * unescapable. THIS NUMBER HAS TO BE RE-JUDGED WHEN #391 IS FIXED.
	 *
	 * "Can be outmanoeuvred" is unaffected either way: it is a turn rate
	 * property, and a player circling at this creature's reach sweeps 223
	 * degrees per second against its 180.
	 */
	static constexpr float DesignedChaseSpeedCmPerSecond = 500.0f;
	//~ End designed numbers

	/**
	 * How far it notices the player, in centimetres. Replaces the base enemy's
	 * 1500.
	 *
	 * SET BY PLAYING IT, ON 2026-08-07, and that is the honest description.
	 * There is no derivation behind 1000 and it would be dishonest to build one
	 * after the fact.
	 *
	 * WHAT IT REPLACED, AND WHY THAT WENT. It was 700, derived as the ground
	 * this creature covers in one attack cycle -- move_speed x attack_interval,
	 * 250 x 2.8. That arithmetic was tidy and it stopped being true twice over
	 * on the same day: the attack interval moved to 1.6, which would make the
	 * same formula give 400, and the Brute now chases at 500 rather than 250,
	 * so "one attack cycle of walking" is not a distance it takes one attack
	 * cycle to walk. A derivation that has to be rewritten every time an input
	 * moves was never really the reason for the number.
	 *
	 * WHY THE INHERITED 1500 STILL WENT. docs/DECISIONS.md records it as "the
	 * same distance Subjugate reaches, which is the longest range the designed
	 * Demonic skills use" -- symmetry, a monster notices you from as far as you
	 * could hit it. That suits a caster. The same entry labels it a judgement
	 * "expected to change".
	 *
	 * THE ONE CONSTRAINT THAT IS ARITHMETIC, and it still holds at 1000.
	 * ACataclysmGameMode spawns the Brute 1200 cm from the player start, so a
	 * notice radius under 1200 means it does not see the player as the level
	 * opens and the first thing anyone sees it do is wander. At 1500 it never
	 * roamed at all. 1000 leaves 200 cm of margin, which is thinner than 700
	 * left and is the figure to revisit first if roaming stops being visible.
	 *
	 * A MOTIONLESS PLAYER IS STILL FOUND EVENTUALLY. The roam circle reaches to
	 * 1200 - 600 = 600 cm from the player start, inside this radius, so a Brute
	 * that wanders to the near edge of its circle notices someone who has not
	 * moved. That is intended.
	 *
	 * ONE ENEMY, NOT SEVEN. The design document states no notice radius for any
	 * enemy. Issue #383 asks for the general rule.
	 */
	static constexpr float BruteNoticeRadiusCm = 1000.0f;

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
	 * THE FOUR-LEGGED GAIT, CHOSEN BY THE PROJECT OWNER ON 2026-08-07 after
	 * watching all three candidates. Rampage has two stances and this is the
	 * other one: the Brute drops onto all fours to close. That reads as having
	 * noticed you through POSTURE rather than through pace, which is what makes
	 * it the right answer here -- the Brute moves at the same designed 2.5 m/s
	 * whether it is wandering or chasing, so any gait that claims speed it does
	 * not have looks like running on the spot. This one claims nothing.
	 *
	 * TWO CANDIDATES WERE REJECTED, both for measured reasons.
	 *
	 * Sprint_Biped_Fwd returns identical bone poses to the walking animation at
	 * every time sampled, with the same length, the same 29 frames and the same
	 * 189 tracks. The pack realises a sprint by playing the jog faster rather
	 * than by animating a second gait, so selecting between them would look
	 * like nothing had changed. Issue #386. Sprint_Quad_Fwd is a duplicate of
	 * Jog_Quad_Fwd in exactly the same way, measured 2026-08-07.
	 *
	 * Run_Fwd is genuinely distinct -- 0.667 s, 20 frames, 47 tracks against
	 * 189 -- but it carries no ik_foot_l track, so its authored speed cannot be
	 * measured at all and its play rate would be a guess. It was tried and
	 * looked like running on the spot.
	 *
	 * TO AUDITION ANOTHER WITHOUT A REBUILD, set Cataclysm.Brute.ChaseAnimation
	 * to any animation's asset path, and set Cataclysm.Brute.AuthoredChaseSpeed
	 * to whatever that one was authored for.
	 */
	static const TCHAR* ChaseAnimationPath;

	/**
	 * What it plays when it swings.
	 *
	 * A PLAIN ANIMATION, NOT THE MONTAGE THAT SITS BESIDE IT. The pack ships
	 * Attack_Biped_Melee_A_Montage and that is the ordinary way to do this, but
	 * this mesh is driven in EAnimationMode::AnimationSingleNode and montages
	 * are silently ignored in that mode -- they do not fail, they do nothing.
	 * Issue #387 replaces the whole single-animation scheme with an animation
	 * Blueprint, and montages become usable then. Until it lands, the plain
	 * sequence played once is what makes a swing visible at all, and a swing
	 * nobody can see was reported as the Brute not attacking.
	 */
	static const TCHAR* AttackAnimationPath;

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
	 * SET BY EYE ON 2026-08-07, BY THE PROJECT OWNER WATCHING IT RUN, and that
	 * is the right authority for it: the whole criterion is whether a planted
	 * foot appears to slide, which is a judgement about what a person sees.
	 * With the chase speed at 500 this makes the play rate 500 / 350 = 1.43.
	 *
	 * IT IS NOT THE MEASURED FIGURE, AND THE GAP IS EXPECTED.
	 * tools/measure_animation_stride.py reports Jog_Quad_Fwd at 304.5 cm/s, and
	 * the same tool reported 242.9 for the walking animation where the by-eye
	 * answer was 225. It reads high on both, by 8% on the walk and 15% here.
	 * The tool's own documentation calls its output a starting estimate good to
	 * roughly ten percent, because the IK foot bones it tracks never touch the
	 * ground on this skeleton.
	 *
	 * WHAT LEAVING IT AT THE BRUTE'S OWN SPEED LOOKED LIKE, because that was
	 * the first attempt: a play rate of exactly 1.0, so the feet travelled as
	 * if the body were moving at 304.5 while it moved at 250, sliding 22% --
	 * reported as "it is like he isn't moving far enough within the animation".
	 *
	 * Retune it with Cataclysm.Brute.AuthoredChaseSpeed while watching. A
	 * smaller number plays the animation faster.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Enemy",
			  meta = (ClampMin = "1.0"))
	float AuthoredChaseSpeedCmPerSecond = 350.0f;

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
