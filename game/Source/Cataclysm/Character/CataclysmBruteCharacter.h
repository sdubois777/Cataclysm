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
 * convention.
 *
 * ONE PIECE OF THIS ENEMY IS A BLUEPRINT, AND IT HAD TO BE. ABP_Brute, in that
 * folder, is its animation graph. An animation graph cannot be written in C++
 * at all -- it is compiled out of a Blueprint into the generated class -- and
 * without one this creature could only ever play a single clip at a time with
 * no blending between them. See AnimationBlueprintPath below.
 *
 * WHAT IT DELIBERATELY DOES NOT DO. Nothing draws a ground marker, so the only
 * warning the player gets before a stomp or a thrown rock is the wind-up
 * animation. The design's telegraph rules assume a marked area on the floor.
 * Issue #396 covers that; the rest of the Stomp is here.
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

	//~ The two abilities beyond the ordinary swing. Driven by the controller.
	virtual TArray<FCataclysmEnemyAbility> EnemyAbilities() const override;
	virtual void UseEnemyAbility(int32 Index, AActor* Target,
								 const FVector& AimedAt) override;
	virtual void BeginEnemyAbilityWindUp(int32 Index, AActor* Target) override;
	//~ End

	/**
	 * Where each ability sits in EnemyAbilities.
	 *
	 * THE STOMP FIRST, so that a target close enough for both gets the bigger
	 * hit. They barely overlap: the stomp reaches 3.5 m and the rock throw does
	 * not start until beyond melee reach, so the contested band is 0.9 to 3.5 m
	 * and the stomp deserves it at 250% against the throw's 150%.
	 *
	 * THE ORDINARY SWING IS IN NEITHER, because it has no cooldown and is what
	 * happens when both of these are unavailable. See EnemyAbilities on the base
	 * class for why that is structural rather than a rule to remember.
	 */
	enum : int32 { StompAbility = 0, RockThrowAbility = 1 };

	//~ The Stomp, from ABILITIES["Brute"] in enemy_abilities.py.

	/** Radius in centimetres. Radius=3.5 metres. */
	static constexpr float StompRadiusCm = 350.0f;

	/** Seconds between stomps. Its own cooldown, the stun immunity window. */
	static constexpr float StompCooldownSeconds = 5.0f;

	/** Seconds of telegraph. 0.4 + 3.5 / 3.5, from the wind-up rule. */
	static constexpr float StompWindUpSeconds = 1.4f;

	/** Percent of its damage one stomp deals. The Heavy slot's 250%. */
	static constexpr float StompDamagePercent = 250.0f;

	/**
	 * Seconds everything caught in the ring is held still. StunSeconds=1.5.
	 *
	 * THIS IS WHAT THE FIVE SECOND COOLDOWN ABOVE IS FOR. The design's stun
	 * immunity window is five seconds, so a stomp that came round sooner would
	 * be refused by the window rather than limited by its slot -- which is why
	 * StompCooldownSeconds is the window and not a figure from the Heavy band.
	 *
	 * A DESIGNED STUN, so it skips the ten percent damage threshold that stops
	 * small hits interrupting. See UCataclysmSkillEffects::ApplyStun.
	 */
	static constexpr float StompStunSeconds = 1.5f;

	//~ Rip and Toss, from the same table.

	/** How far it can throw, in centimetres. Range=10 metres. */
	static constexpr float RockThrowRangeCm = 1000.0f;

	/** Half the width of the marked line, in centimetres. Radius=2.1 metres. */
	static constexpr float RockThrowRadiusCm = 210.0f;

	/** Centimetres per second the rock travels. Speed=1200. */
	static constexpr float RockThrowSpeedCmPerSecond = 1200.0f;

	/** Seconds between throws. */
	static constexpr float RockThrowCooldownSeconds = 5.0f;

	/** Seconds of telegraph. 0.4 + 2.1 / 3.5. */
	static constexpr float RockThrowWindUpSeconds = 1.0f;

	/** Percent of its damage one rock deals. The Special slot's 150%. */
	static constexpr float RockThrowDamagePercent = 150.0f;
	//~ End designed ability numbers

	//~ Each ability is ONE MONTAGE holding two clips back to back: the wind-up,
	// then the release.
	//
	// BOTH HALVES ARE NEEDED. The wind-up clip ends with the creature poised --
	// fist raised, rock held overhead -- and the release is the half where the
	// attack actually happens.
	//
	// WHY A MONTAGE ASSET RATHER THAN CLIPS SEQUENCED HERE. Until 2026-08-08 this
	// class played each ability as three separate dynamic montages -- a wind-up,
	// a hold clip repeated a fractional number of times, then a release -- driven
	// by two timers. Pull requests #407, #409, #410 and #411 were four attempts to
	// make that work, and every one of them was the same fault wearing a new
	// disguise: a timer firing in the wrong order, a clip sized to the wrong
	// window, a blend setting that discarded the end of a clip, a hold that
	// outlived the ability it was holding open.
	//
	// Inside one montage the two clips are one continuous timeline. There is no
	// seam to blend, nothing to schedule between them, and no second clip that
	// can outlive the first. Issue #412.
	//
	// NO HOLD CLIP APPEARS HERE AND NONE IS WANTED. Ability_GroundSmash_Loop and
	// Ability_RipNToss_Idle exist to pad a wind-up out to a longer telegraph.
	// Pausing the montage on its join frame does the same thing without a second
	// asset -- see HoldsAbilityPose below.
	//
	// tools/generate_brute_montages.py builds both assets and is the reviewable
	// record of what they contain, because a .uasset is binary and cannot be
	// diffed. Re-running it overwrites anything edited by hand in the editor.

	/** Where the ground smash montage lives. Wind-up, then impact. */
	static const TCHAR* StompMontagePath;

	/** Where the rock throw montage lives. Tearing the rock out, then throwing. */
	static const TCHAR* RockThrowMontagePath;

	/** The ground smash montage. Null until ResolveBody runs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimMontage> StompMontage;

	/** The rock throw montage. Null until ResolveBody runs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimMontage> RockThrowMontage;

	/**
	 * The rock the throw flies. Null until ResolveBody runs, and null for good
	 * without the Paragon pack -- the projectile then keeps its engine sphere.
	 *
	 * Read by tests, which is why it is not private.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UStaticMesh> RockMesh;

	/**
	 * The bone the carried rock hangs from.
	 *
	 * MEASURED OUT OF THE ASSET, NOT GUESSED, AND THE ANSWER IS A BONE RATHER
	 * THAN A SOCKET. `tools/probe_brute_animation.py`-style reads on 2026-08-08
	 * found that the Rampage mesh has NO sockets at all: `find_socket` answers
	 * null for every name tried, including this one. So the rock attaches to a
	 * bone.
	 *
	 * WHY THIS BONE AND NOT `hand_r`. `weapon_r` is the rig's prop bone and it
	 * is animated for exactly this purpose. Standing in `Idle_Biped` it sits 1.8
	 * cm from `hand_r`, parked at the hand. Through the Rip and Toss clips the
	 * two separate by 40 to 110 cm, because the animator is moving the prop
	 * rather than the hand -- at the top of the throw's arc `hand_r` is at
	 * (-60.5, 81.4, 209.6) and `weapon_r` is at (-45.2, 190.8, 211.1).
	 *
	 * THAT IS WHY THE ROCK NEEDS NO OFFSET AND NO SCALE. Where it sits is
	 * authored in the animation. Issue #421 expected this to be a judgement
	 * somebody had to make by eye; the measurement removed it.
	 */
	static const FName RockHoldBoneName;

	/**
	 * The rock while the creature is holding it, before the throw.
	 *
	 * WHAT IT REPLACED. The rock throw played `Ability_RipNToss_Rip`, an
	 * animation whose whole content is tearing a rock out of the ground, with
	 * nothing in the creature's hands. The rock then appeared in mid-air and
	 * flew off. Issue #421.
	 *
	 * HIDDEN RATHER THAN SPAWNED AND DESTROYED. It is the same mesh every time
	 * and it is needed several times a fight, so it is made once and its
	 * visibility follows the brain. See UpdateCarriedRock.
	 *
	 * Read by tests, which is why it is not private.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UStaticMeshComponent> CarriedRock;

	/**
	 * Shows the rock while the rock throw is being wound up and hides it
	 * otherwise. Called every frame from Tick.
	 *
	 * ASKS THE BRAIN RATHER THAN REMEMBERING, which is the same shape
	 * UpdateAbilityMontage uses and for the same reason. Every way a wind-up can
	 * end clears the controller's WindingUpAbility -- the attack landing, a stun
	 * cancelling it, the pawn being unpossessed -- so asking covers all of them
	 * without this function knowing what any of them are.
	 */
	void UpdateCarriedRock();

	/**
	 * The pieces the thrown rock breaks into, and their material.
	 *
	 * THE MATERIAL IS CARRIED SEPARATELY BECAUSE THE FRAGMENTS HAVE NONE.
	 * Measured 2026-08-08: all five SM_Rampage_Rock_Frag meshes have
	 * /Engine/EngineMaterials/WorldGridMaterial assigned, the engine's grey
	 * checkerboard placeholder. The rock they are pieces of has a real one,
	 * M_Rock_To_Throw, so they wear that.
	 *
	 * Empty without the Paragon pack, which leaves the throw exactly as it was.
	 * Read by tests.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TArray<TObjectPtr<class UStaticMesh>> RockFragments;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UMaterialInterface> RockMaterial;

	/**
	 * Leaves broken pieces where a thrown rock stopped.
	 *
	 * BOUND TO THE PROJECTILE'S OWN OnFinished, which already exists and already
	 * reports where it stopped. That is what keeps ACataclysmProjectile generic:
	 * it knows nothing about rocks, and a skill that wants sparks instead binds
	 * something else to the same delegate. Issue #422.
	 */
	void LeaveRockDebris(class ACataclysmProjectile* Thrown);

	/**
	 * Where the five fragments live in the pack, as a folder and a base name.
	 *
	 * TWO PIECES RATHER THAN A FORMAT STRING. Unreal 5.8 checks FString::Printf
	 * format strings at compile time, so one held in a variable is rejected
	 * outright. The path is built by joining instead.
	 */
	static const TCHAR* RockFragmentFolder;
	static const TCHAR* RockFragmentBaseName;

	/** How many of them there are. */
	static constexpr int32 RockFragmentCount = 5;

	/**
	 * How far from the impact the pieces are placed, and how large each is, in
	 * centimetres.
	 *
	 * BOTH JUDGEMENTS, AND NEITHER HAS BEEN WATCHED. The piece size is a third
	 * of the flying rock's own body width, so five of them read as one rock
	 * broken up rather than five more rocks. The spread is twice that, so they
	 * do not overlap.
	 */
	static constexpr float RockFragmentRadiusCm = 13.0f;
	static constexpr float RockFragmentSpreadCm = 26.0f;

	/**
	 * The hole the rock was torn out of.
	 *
	 * Issue #432. `Ability_RipNToss_Rip` is an animation whose whole content is
	 * reaching down, tearing a rock out of the ground and lifting it. Since
	 * issue #421 the rock is visibly in the creature's hand while it does that.
	 * The ground it came out of was untouched.
	 *
	 * IT CARRIES THE CHECKERBOARD TOO, like the five fragments before it.
	 * Measured 2026-08-08 and recorded in `game/docs/enemy-source-assets.md`:
	 * `SM_Rampage_Rock_Rip_Crater` has `/Engine/EngineMaterials/WorldGridMaterial`
	 * assigned. So it wears `RockMaterial`, from the rock that came out of it,
	 * exactly as the fragments do.
	 */
	static const TCHAR* RockCraterMeshPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UStaticMesh> RockCraterMesh;

	/**
	 * How far in front of the creature the rock comes out of the ground, in
	 * centimetres.
	 *
	 * MEASURED OUT OF THE CLIP RATHER THAN JUDGED BY EYE, which is the first of
	 * the four decisions issue #432 asks for. Following both hands through
	 * `Ability_RipNToss_Rip` with `AnimPoseExtensions` on 2026-08-08:
	 *
	 *     hand_l  lowest at 0.2833 s  ( 83.8, 62.0, 2.9)
	 *     hand_r  lowest at 0.2644 s  (-84.9, 43.7, 2.9)
	 *
	 * Their midpoint is (-0.6, 52.9). THE HALF-CENTIMETRE IS THE CHECK: a
	 * two-handed rip should be symmetric about the creature's centre line, and a
	 * midpoint that landed anywhere else would have meant the wrong bones or the
	 * wrong axis.
	 *
	 * AND THE AXIS IS Y, NOT X. The Rampage mesh takes a -90 degree yaw on its
	 * component to face the way its actor faces, so forward in the animation's
	 * own space is +Y. `tools/measure_animation_stride.py` records the same trap
	 * from the other side: a first version of it measured X, got symmetric
	 * extremes about zero -- the signature of a side-to-side axis -- and reported
	 * a nonsense jog speed.
	 */
	static constexpr float CraterAheadCm = 52.9f;

	/**
	 * How wide the hole is, in centimetres.
	 *
	 * A JUDGEMENT, AND LABELLED AS ONE. Slightly wider than the 40 cm rock that
	 * came out of it, which is the only relationship between the two that can be
	 * argued for without watching it.
	 * `ACataclysmProjectile::DefaultBodyRadiusCm` is that 40.
	 */
	static constexpr float CraterRadiusCm = 50.0f;

	/**
	 * How long the hole stays, in seconds.
	 *
	 * DELIBERATELY UNDER THE THROW'S OWN COOLDOWN, which is the third and fourth
	 * of issue #432's decisions taken together. `RockThrowCooldownSeconds` is 5,
	 * so a lifetime below it means ONE BRUTE CAN NEVER HAVE TWO CRATERS. That
	 * turns "how do craters accumulate" from a thing needing a manager and a cap
	 * into an invariant, and there is a test holding it.
	 *
	 * WHICH ALSO ANSWERS PER-BRUTE VERSUS PER-PLACE. Each creature rips its own
	 * rock out of the ground in front of itself, so the crater belongs to the
	 * Brute that made it and goes away on its own. Two Brutes standing in the
	 * same place is not a case to design for; their capsules do not overlap.
	 *
	 * NOBODY HAS WATCHED THIS. Four seconds is long enough to still be there
	 * when the rock lands and short enough to satisfy the invariant above.
	 */
	static constexpr float CraterSecondsOnTheGround = 4.0f;

	/**
	 * When in `Ability_RipNToss_Rip`, at its authored speed, the hands reach the
	 * ground. Seconds.
	 *
	 * MEASURED, as above: `hand_r` bottoms out at 0.2644 s and `hand_l` at
	 * 0.2833 s, both 2.9 cm off the floor. The earlier of the two is used,
	 * because that is when the ground is first broken.
	 *
	 * THIS IS THE SECOND OF ISSUE #432'S DECISIONS -- when the hole appears. Not
	 * at the start of the wind-up, or it is there before anything has dug it.
	 * The clip is compressed by `MontageRateFor`, so the wall-clock moment is
	 * this divided by the rate. See RipReachesGroundAtSeconds.
	 */
	static constexpr float RipReachesGroundSeconds = 0.2644f;

	/**
	 * Leaves the crater once the hands have reached the ground. Called every
	 * frame from Tick.
	 *
	 * COMPARED AGAINST THE CLOCK EVERY FRAME RATHER THAN SET AS A TIMER, for the
	 * reason UpdateAbilityMontage gives at length: a timer fixes its deadline
	 * when it is created, which is how a held clip came to fire on the wrong
	 * side of the pass that landed its ability in pull request #411.
	 */
	void UpdateRipCrater();

	/** Where the rock comes out of the ground, in world space. */
	FVector RipCraterLocation() const;

	/**
	 * How long after the wind-up begins the hands reach the ground, in seconds
	 * of wall clock.
	 *
	 * The montage does not start when the wind-up does -- MontageDelaySecondsFor
	 * waits out whatever the animation does not cover -- and it does not play at
	 * its authored speed either. Both are asked rather than assumed.
	 */
	float RipReachesGroundAtSeconds() const;

	/**
	 * The same moment, from a delay and a play rate given to it.
	 *
	 * SPLIT OUT SO THAT THE COMPRESSION CAN BE TESTED AT ALL. The rate comes
	 * from a montage, and a test that asks the member function above when to
	 * advance its clock moves with whatever that function does -- deleting the
	 * division by the rate left all four crater tests passing, which is how this
	 * was found. Given the rate directly, a test can say the load-bearing thing:
	 * an animation played twice as fast reaches the ground in half the time.
	 */
	static float RipReachesGroundAtSeconds(float DelaySeconds, float Rate);

	/**
	 * Whether this wind-up has already dug its hole.
	 *
	 * ONE PER THROW. Without it Tick would spawn a crater every frame from the
	 * moment the hands land until the rock leaves. Cleared when the creature
	 * stops winding up the throw, which is the same question UpdateCarriedRock
	 * asks and covers every way a wind-up can end.
	 *
	 * Read by tests, which is why it is not private.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bCraterLeftForThisThrow = false;

	/** The montage for one ability, or null if the art is absent. */
	class UAnimMontage* AbilityMontageFor(int32 Index) const;

	/** The designed telegraph for one ability, in seconds. Zero if not an ability. */
	static float WindUpSecondsFor(int32 Index);

	/**
	 * The EARLIEST moment an ability can land, in seconds after its wind-up
	 * begins. Not the telegraph, and not a promise about the real landing.
	 *
	 * NOT THE TELEGRAPH, AND THE DIFFERENCE IS WHAT FOUR PULL REQUESTS KEPT
	 * TRIPPING OVER. An ability does not land when its telegraph expires. It
	 * lands on the first pass of the brain's thinking timer at or after that
	 * moment -- ACataclysmEnemyController::ContinueWindUp returns early while
	 * Now < WindUpLandsAt and only runs on a pass. So the stomp's 1.4 second
	 * telegraph lands at 1.50, and anything sized to 1.4 finishes a tenth of a
	 * second early.
	 *
	 * AND NOT A FIXED GRID EITHER, WHICH IS THE PART THAT IS EASY TO GET WRONG.
	 * A timer callback runs on the first frame whose clock has passed its
	 * deadline, so every pass carries up to one frame of overshoot and the
	 * overshoot differs between the pass that starts a wind-up and the pass that
	 * lands it. Where a telegraph sits clear of a step that does not matter: the
	 * stomp's 1.4 is 0.1 clear of 1.50 and never moves. Where a telegraph sits
	 * exactly ON a step it matters a great deal, because the comparison is a
	 * strict less-than with no tolerance. The rock throw's 1.0 second telegraph
	 * is exactly four steps, and simulating the engine's own timer arithmetic
	 * over 500 jittery frames between 50 and 70 per second landed it at 1.25
	 * rather than 1.00 in very nearly half of them.
	 *
	 * SO THIS RETURNS THE EARLIEST, AND THAT IS DELIBERATE RATHER THAN
	 * OPTIMISTIC. The montage is timed so that its strike arrives by this moment.
	 * Arriving early costs a small misalignment; arriving late would deal the
	 * damage while the creature was still raising its fists.
	 */
	static float LandsAtSecondsFor(float WindUpSeconds);

	/**
	 * Seconds into a montage where the wind-up clip ends and the release clip
	 * begins. The length of its first segment, or zero if it has none.
	 *
	 * THIS IS NOT THE MOMENT OF IMPACT, WHICH IS WHAT THE FIRST VERSION OF THIS
	 * CODE ASSUMED AND GOT WRONG. See ImpactSecondsFor.
	 */
	static float JoinSecondsFor(const class UAnimMontage* Montage);

	/**
	 * Seconds into the RELEASE clip at which the blow actually arrives.
	 *
	 * MEASURED, NOT GUESSED, AND THE FIRST VERSION OF THIS CODE GUESSED. It
	 * assumed the release clip begins at the moment of impact, so that lining the
	 * join up with the damage lined the blow up with it too. That is not what the
	 * art does. Ability_GroundSmash_End BEGINS with the creature's fists still
	 * overhead, at 341 and 349 centimetres -- continuous with the 321 and 333 the
	 * wind-up clip ends on -- and they take 0.179 seconds to reach the ground.
	 *
	 * Ability_RipNToss_Toss is the same shape for a different reason: the rock
	 * does not leave the hand as the clip starts. The throwing hand climbs to the
	 * top of its arc at 0.539 seconds and that is where the rock is released.
	 *
	 * WHAT THE WRONG ASSUMPTION COST. Treating the join as the impact left 0.667
	 * seconds of the telegraph that the animation did not cover, which was filled
	 * by freezing the montage on a single frame. The project owner reported it on
	 * 2026-08-08: "He reaches his arms up in the air, freezes for a second or so,
	 * then continues to slamming down."
	 *
	 * HOW TO RE-MEASURE. tools/measure_animation_impact.py evaluates each clip
	 * with unreal.AnimPoseExtensions and follows the hands through it. The clips
	 * carry no animation notifies -- all five were checked on 2026-08-08 -- so
	 * there is no authored marker to read instead.
	 */
	static constexpr float StompStrikeIntoReleaseSeconds = 0.179f;
	static constexpr float RockThrowStrikeIntoReleaseSeconds = 0.539f;

	/** The figure above for one ability, or zero if it is not an ability. */
	static float StrikeIntoReleaseSecondsFor(int32 Index);

	/**
	 * Seconds into the montage at which the attack visibly strikes: the join,
	 * plus however far into the release clip the blow arrives.
	 *
	 * HALF READ FROM THE ASSET AND HALF A MEASURED CONSTANT, on purpose. The join
	 * moves if the wind-up clip is ever replaced with a longer one, and reading it
	 * off the montage means this follows without anyone remembering to. How far
	 * into the release clip the blow lands is a property of what the art shows and
	 * cannot be read from any asset, so it is the constant above.
	 */
	static float ImpactSecondsFor(const class UAnimMontage* Montage, int32 Index);

	/**
	 * The rate an ability montage plays at.
	 *
	 * NEVER SLOWER THAN AUTHORED, ONLY FASTER, AND ONLY WHEN IT MUST BE.
	 * Stretching a clip to fill a longer window was tried first and reported from
	 * a play session as slow motion. Where the montage reaches its strike sooner
	 * than the attack lands, the answer is to start it later rather than to slow
	 * it down -- see MontageDelaySecondsFor.
	 *
	 * COMPRESSION IS STILL NEEDED THE OTHER WAY, and the rock throw needs a lot of
	 * it. Tearing the rock out takes 1.133 seconds and the throwing arm does not
	 * reach the top of its arc until 0.539 seconds into the clip after that, so
	 * the rock does not leave the creature's hand until 1.672 seconds. Its
	 * telegraph gives it 1.000. There is no way to show all of that in the time
	 * the design allows, so it plays at 1.67 and looks hurried. That is a design
	 * question rather than a bug in this arithmetic, and it is issue #416.
	 */
	static float MontageRateFor(float ImpactSeconds, float LandsAtSeconds);

	/**
	 * Seconds after the wind-up begins that the montage should start, so that its
	 * strike arrives exactly when the attack lands.
	 *
	 * WHY A DELAY RATHER THAN A HELD POSE, WHICH IS THE CHANGE OF 2026-08-08.
	 * The ground smash reaches its strike 1.012 seconds in and the attack lands at
	 * 1.500, so 0.488 seconds have to come from somewhere. The first version froze
	 * the montage on its join frame for that long, which is a single unchanging
	 * frame and read as the creature seizing up mid-swing. Waiting before starting
	 * instead means the creature stands in its ordinary idle -- which moves -- and
	 * then performs the whole attack as one continuous movement.
	 */
	static float MontageDelaySecondsFor(const class UAnimMontage* Montage,
										int32 Index);

	/**
	 * Start a waiting ability montage once its delay has elapsed.
	 *
	 * DRIVEN FROM Tick RATHER THAN A TIMER, and that is deliberate. A timer has a
	 * deadline fixed when it is set; this compares the clock every frame, so it
	 * cannot fire in the wrong order relative to anything, and it abandons the
	 * montage if the brain has stopped winding the ability up in the meantime. A
	 * timer with a deadline is how a held clip came to outlive its own ability in
	 * pull request #411.
	 *
	 * PUBLIC AND CALLABLE SO A TEST CAN RUN IT WITHOUT WAITING, the same reason
	 * ACataclysmEnemyController::Think is. An automation test world is never
	 * ticked.
	 */
	void UpdateAbilityMontage();

	/** Which ability is waiting for its montage to start, or INDEX_NONE. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	int32 PendingAbilityMontage = -1;

	/** World time the current wind-up began. Meaningless when none is pending. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float AbilityWindUpBeganAtSeconds = 0.0f;

	/** Which ability's montage is running, or INDEX_NONE. Read by tests. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	int32 ActiveAbilityMontage = -1;

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

	/** What it plays when it swings. Null until ResolveBody runs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> AttackAnimation;

	/**
	 * Start the swing animation.
	 *
	 * Called by the base class when an attack actually lands, so that the
	 * animation and the damage cannot drift apart. Does nothing when the art is
	 * absent, which is every fresh clone.
	 */
	void PlayAttackAnimation();

	/**
	 * Play one animation once, blended over whatever the creature is doing.
	 *
	 * THE ONE PLACE AN ATTACK CLIP IS PLAYED, so the three callers cannot drift.
	 * The swing, the wind-up and the release all mean the same thing.
	 *
	 * IT NO LONGER TAKES THE MESH OVER, which is the change of 2026-08-08. The
	 * clip goes into AttackSlotName in ABP_Brute's animation graph, so
	 * locomotion carries on underneath it and is blended back to when the clip
	 * ends. Before that the mesh ran in EAnimationMode::AnimationSingleNode,
	 * which plays exactly one clip and cannot blend, so a wind-up followed by a
	 * release cut between the two in a single frame.
	 *
	 * THE PLAY RATE IS DERIVED, NOT PASSED IN, and that is the point of taking a
	 * duration rather than a rate. A clip longer than the window it plays inside
	 * is compressed to fit; a shorter one is NOT stretched, because stretching
	 * the 0.83 second ground smash wind-up across a 1.4 second telegraph played
	 * it at 0.59 speed and read as slow motion.
	 *
	 * The rate is clamped to MinimumPlayRate and MaximumPlayRate, so a clip
	 * wildly out of proportion to its window plays wrong rather than absurdly.
	 *
	 * Does nothing when the animation is null, which is every fresh clone.
	 *
	 * @param HoldSeconds  the window the clip has to fit inside. Zero or less
	 *                     uses the clip's own length, meaning normal speed.
	 * @param BlendOutTriggerTime  see AbilityBlendOutTriggerTime. Defaults to
	 *                     the engine's own default, which is what the ordinary
	 *                     swing wants.
	 * @return how many seconds the clip will actually take, which is its length
	 *                     divided by the rate it was given. Zero if nothing was
	 *                     played. The caller needs this to know when the clip
	 *                     ends; the clip's own length is the wrong answer
	 *                     whenever it was compressed to fit.
	 */
	float PlayOneShot(class UAnimSequence* Animation, float HoldSeconds = 0.0f,
					  float BlendOutTriggerTime = SwingBlendOutTriggerTime);

	/**
	 * Play one plain clip in the attack slot, and record what was asked for.
	 *
	 * THE ORDINARY SWING ONLY. The two abilities play montage assets instead and
	 * do not come through here; see PlayAbilityMontage. This wraps a plain
	 * sequence in a dynamic montage so it blends against locomotion, which is
	 * what a montage asset would give but without a second binary file to review
	 * for a clip that needs no sequencing.
	 *
	 * The blend settings that are recorded and the ones that are used come from
	 * the same two local variables and cannot disagree. They disagreed once: a
	 * caller recorded AttackBlendInSeconds while passing a literal zero, so a
	 * test written against the record passed while the creature visibly snapped
	 * into its pose instead of blending into it.
	 */
	void PlayInAttackSlot(class UAnimSequence* Animation, float Rate,
						  float BlendOutTriggerTime);

	/**
	 * Start the montage for one ability, timed so its join lands on the impact.
	 *
	 * Called when the wind-up begins. Records what it chose whether or not
	 * anything can play it, for the reason given on LastPlayedAnimation.
	 */
	void PlayAbilityMontage(int32 Index);

	/**
	 * Which slot of ABP_Brute's animation graph an attack clip plays in.
	 *
	 * DefaultSlot, because that is what the Slot node in that graph is named and
	 * a montage played into a slot that does not exist is silently dropped.
	 */
	static const FName AttackSlotName;

	/**
	 * Seconds an attack clip takes to blend in, and to blend back out to
	 * locomotion.
	 *
	 * A JUDGEMENT AT 0.15 SECONDS, roughly four frames at 30. The whole point is
	 * that the number is not zero: zero is what the single-node mode gave, and a
	 * wind-up cutting to a release in one frame is the fault this replaced. Long
	 * enough to read as one movement, short enough that a swing still lands when
	 * it looks like it lands.
	 */
	static constexpr float AttackBlendInSeconds = 0.15f;
	static constexpr float AttackBlendOutSeconds = 0.15f;

	/**
	 * When a clip starts blending back to walking and standing, measured as
	 * seconds before its own end.
	 *
	 * THE ENGINE'S DEFAULT IS -1 AND IT DOES NOT MEAN WHAT IT LOOKS LIKE. Its
	 * own header, Engine/Classes/Animation/AnimMontage.h, says:
	 *
	 *     Time from Sequence End to trigger blend out.
	 *     <0 means using BlendOutTime, so BlendOut finishes as Montage ends.
	 *     >=0 means using 'SequenceEnd - BlendOutTriggerTime' to trigger.
	 *
	 * So at -1 the blend FINISHES as the clip ends, which means it STARTS
	 * AttackBlendOutSeconds before the end. The last 0.15 seconds of the clip
	 * is a falling cross-fade against whatever ABP_Brute is playing
	 * underneath. Confirmed in Engine/Private/Animation/AnimMontage.cpp, in
	 * FAnimMontageInstance::Advance: the branch is taken when
	 * PlayTimeToEnd <= BlendOut.GetBlendTime(), and the blend duration is then
	 * set to PlayTimeToEnd rather than to the full blend time.
	 *
	 * WHAT THAT LOOKED LIKE. The 0.83 second ground smash wind-up was at full
	 * weight only from 0.15 to 0.68 seconds, so the creature never reached the
	 * poised pose it was winding up into -- its arms sagged back toward
	 * standing before the attack landed. Reported on 2026-08-08.
	 *
	 * ZERO MEANS PLAY THE WHOLE CLIP, THEN BLEND. At zero the trigger
	 * comparison collapses to a near-zero epsilon, so nothing fires until the
	 * clip is over; the montage then holds its final frame while the blend runs
	 * for its full length. That is what an ability needs, because the pose its
	 * wind-up half ends on IS the telegraph.
	 *
	 * IT IS NO LONGER PASSED TO ANYTHING. Since 2026-08-08 the two ability
	 * montages carry this setting themselves, written by
	 * tools/generate_brute_montages.py, so it can be dragged against a live
	 * preview in the montage editor. What this constant is now is the figure
	 * those assets are REQUIRED to hold, and
	 * Cataclysm.Brute.ItsAbilityMontagesAreBuiltCorrectly reads it back off
	 * each asset and fails when they disagree. That guard is the answer to issue
	 * #406 for these two assets: a tuned number inside a binary file with
	 * nothing checking it is a number that can drift silently, and this one
	 * cannot any more.
	 */
	static constexpr float AbilityBlendOutTriggerTime = 0.0f;

	/**
	 * The same, for the ordinary swing, left at the engine default on purpose.
	 *
	 * A SWING IS NOT A WIND-UP. Its last frames are the arm following through,
	 * not a pose that has to be held and read, so dissolving them into walking
	 * is right and holding them for an extra 0.15 seconds would be a small
	 * version of the frozen finishing pose that PlayAttackAnimation's comment
	 * argues against. It is also by far the most frequent clip this creature
	 * plays, so changing it is the change most likely to be noticed and least
	 * likely to have been asked for.
	 */
	static constexpr float SwingBlendOutTriggerTime = -1.0f;

	/**
	 * What the last call to PlayOneShot asked for, beyond the clip and the rate.
	 * Read by tests.
	 *
	 * WHY THESE ARE RECORDED AT ALL. Both of the numbers above are arguments
	 * passed to an engine function, and an argument is not observable from
	 * outside. Without these, deleting either one would leave every test in the
	 * project green while the fault they were added to fix came straight back.
	 * That is exactly what happened between 2026-08-08's first and second
	 * attempts at this animation.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float LastPlayedBlendInSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float LastPlayedBlendOutTriggerTime = 0.0f;

	/**
	 * The montage the last ability asked for, and the rate it asked for. Null
	 * and zero until an ability winds up. Read by tests.
	 *
	 * RECORDED FOR THE SAME REASON AS LastPlayedAnimation BELOW: an argument
	 * handed to an engine function is not observable from outside, so without
	 * these a test could not tell which ability played which montage, or at what
	 * speed, and deleting the timing arithmetic would leave every test green.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimMontage> LastPlayedMontage;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float LastPlayedMontageRate = 0.0f;

	/**
	 * How many ability montages this creature has started. Read by tests.
	 *
	 * A COUNT RATHER THAN A COMPARISON, AND THAT IS THE WHOLE POINT OF IT. The
	 * fault this guards against is a second montage being started at the moment
	 * of impact, which is what pull requests #409, #410 and #411 each tried to
	 * make work. The obvious test for that -- remember which montage was playing
	 * before the attack landed, and check it is still that one afterwards --
	 * CANNOT FAIL, because starting the same ability's montage a second time
	 * leaves the recorded montage pointing at the same asset. That test was
	 * written first, the fault was deliberately reintroduced, and it passed.
	 *
	 * Counting is what makes the difference observable.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	int32 AbilityMontagesStarted = 0;

	/**
	 * The clip PlayOneShot last chose, and the rate it asked for. Read by tests.
	 * Null and zero until something is played.
	 *
	 * THE DECISION RECORDED SEPARATELY FROM THE APPLICATION, which is the split
	 * this project already makes in ACataclysmEnemyController::ChooseRoamTarget
	 * against Think. It is here because the application needs a running
	 * animation instance, which needs the art: without the Paragon packs there
	 * is no skeleton to run a graph on, and that is every clone and every
	 * continuous integration run.
	 *
	 * THE GRAPH ITSELF RUNS FINE IN A TEST WORLD, which it did not when this
	 * was written. Issue #374 recorded a Paragon graph hanging the test process
	 * for over three minutes; that was Rampage_AnimBlueprint, the pack's own.
	 * ABP_Brute is this project's and
	 * Cataclysm.Brute.TheAnimationGraphRunsAndReadsTheCreaturesSpeed ticks it.
	 *
	 * SET BEFORE THE ANIMATION INSTANCE IS ASKED FOR, so these record what was
	 * chosen even where nothing can play it -- which is every automation test
	 * world and every clone without the Paragon packs. Without that, the tests
	 * covering which clip an ability plays could only ever check the no-art
	 * path, and would pass while playing the wrong clip.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> LastPlayedAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float LastPlayedRate = 0.0f;

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
	 * IT IS NOW FASTER THAN THE PLAYER, AND THAT IS NOT WHAT WAS JUDGED. When
	 * this figure was set, ACataclysmPlayerCharacter never assigned MaxWalkSpeed
	 * and so ran at Unreal's default 600, which left 500 as a 100 cm/s margin in
	 * the player's favour. Issue #391 fixed that: the player now walks at the
	 * 400 the class stat data gives, so the same 500 is a 100 cm/s margin
	 * AGAINST them and the Brute cannot be walked away from at all.
	 *
	 * IT IS NOT AS SIMPLE AS SCALING IT DOWN. ABP_Brute chooses the four-legged
	 * chase gait above 375 cm/s, so the whole usable window against a 400 cm/s
	 * player is 375 to 400; and the Ritualist is designed at 350, which no
	 * chase speed above 375 can be escaped by. That is a design question rather
	 * than arithmetic and it is issue #417.
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
	 * The rock this creature tears out and throws.
	 *
	 * WHAT IT REPLACED. Every projectile in the game carried a grey engine
	 * sphere, because ACataclysmProjectile is generic -- all 398 rows of
	 * game/Data/WeaponSkills.csv fire through it -- and putting a rock in its
	 * constructor would have given every player fire bolt a rock. So the mesh is
	 * chosen by whoever fires and passed in. Issue #404.
	 *
	 * NAMED FOR HOLDING BECAUSE IT IS ALSO THE CARRY ASSET. The pack's animation
	 * set has a whole carrying state -- Ability_RipNToss_Idle plus directional
	 * carrying gaits and a cancel clip -- and none of it is used. Putting this
	 * mesh in the creature's hand during the wind-up is issue #421 and is not
	 * done here.
	 *
	 * A soft path resolved in BeginPlay, for the same reason BodyMeshPath is:
	 * the Paragon packs are gitignored, so this is absent on a fresh clone and
	 * the projectile keeps its engine sphere.
	 */
	static const TCHAR* RockMeshPath;

	/**
	 * The animation Blueprint that drives this creature, by generated-class path.
	 *
	 * WHY AN ANIMATION BLUEPRINT AT ALL, AND WHY THIS ONE RATHER THAN THE PACK'S.
	 * The Paragon pack ships Rampage_AnimBlueprint and it does not work on this
	 * character, measured rather than assumed. Read at runtime on 2026-08-07 from
	 * a live Play-In-Editor session, on the animation instance attached to a
	 * Brute the controller had reported as Chasing:
	 *
	 *     speed          = 0
	 *     isAccelerating = false
	 *     character      = None
	 *
	 * `character` is that graph's own reference to the pawn it animates. It fills
	 * it by casting the pawn owner to the class it was written for, which is the
	 * pack's own RampagePlayerCharacter Blueprint. An ACataclysmBruteCharacter is
	 * not that class, the cast fails, and every value the graph derives from it
	 * stays at zero for ever.
	 *
	 * ABP_Brute is this project's own, in game/Content/Enemies/Demonic/Brute/ by
	 * the convention in game/docs/content-layout.md. It holds three animation
	 * clips blended by two booleans, and a Slot node named AttackSlotName that
	 * attacks are played into. It reads the pawn's own ground speed rather than
	 * casting to any particular class, so it cannot fail the way the pack's does.
	 *
	 * WHAT IT LOOKS LIKE INSIDE, so that a reader does not have to open a binary
	 * asset to know what drives the creature:
	 *
	 *     Jog_Quad_Fwd  ─┐                         (true:  chasing)
	 *                    ├─ Blend Poses by bool ─┐ (bChasing)
	 *     Jog_Biped_Fwd ─┘                       │ (false: wandering)
	 *                                            ├─ Blend Poses by bool ─→ Slot
	 *     Idle_Biped ─────────────────────────────┘ (bMoving)
	 *
	 * BLEND POSES BY BOOL PUTS THE TRUE POSE ON THE **TOP** PIN, WHICH IS THE
	 * OPPOSITE OF HOW IT READS. The engine says so itself, in
	 * Engine/Source/Runtime/AnimGraphRuntime/Private/AnimNodes/
	 * AnimNode_BlendListByBool.cpp:
	 *
	 *     // Note: Intentionally flipped boolean sense
	 *     // (the true input is #0, and the false input is #1)
	 *     return GetActiveValue() ? 0 : 1;
	 *
	 * Both blends in this graph were wired the wrong way round on 2026-08-08 for
	 * exactly that reason, and the project owner reported it as the Brute
	 * standing still in its running animation and walking in its standing one.
	 * Nothing caught it: the automation tests check that the graph loads and is
	 * assigned, not which pose is on which pin. Issue #408.
	 *
	 * IT REPLACED DRIVING THE MESH DIRECTLY, on 2026-08-08. Until then the
	 * component ran in EAnimationMode::AnimationSingleNode with C++ choosing one
	 * clip per frame. That mode plays exactly one clip and cannot blend, so every
	 * ability cut from the last frame of its wind-up to the first frame of its
	 * release. Issue #387.
	 *
	 * THE _C SUFFIX IS NOT A TYPO. An animation Blueprint's runtime class is its
	 * asset path with _C appended; the asset itself is a UAnimBlueprint, which is
	 * not a UAnimInstance and cannot be handed to SetAnimInstanceClass.
	 */
	static const TCHAR* AnimationBlueprintPath;

	/**
	 * What it plays when it swings.
	 *
	 * A PLAIN ANIMATION, NOT THE MONTAGE THAT SITS BESIDE IT. The pack ships
	 * Attack_Biped_Melee_A_Montage and that would be the ordinary way to do
	 * this. It is not used because a montage asset is a second binary file to
	 * review for every clip, and PlaySlotAnimationAsDynamicMontage gives the
	 * same blending from the plain sequence with nothing to author.
	 */
	static const TCHAR* AttackAnimationPath;

	/**
	 * The two figures the walking and chasing gaits are played back at, and where
	 * they now live.
	 *
	 * NOT IN THIS FILE ANY MORE. Until 2026-08-08 the Brute held both an authored
	 * speed per clip and the arithmetic that turned them into a play rate. Both
	 * moved into ABP_Brute, where the two sequence player nodes carry the rate
	 * directly:
	 *
	 *     Jog_Biped_Fwd   1.111111   the wandering gait, 250 cm/s over 225
	 *     Jog_Quad_Fwd    1.428571   the chasing gait,   500 cm/s over 350
	 *
	 * 225 and 350 were both set by eye on 2026-08-07 by the project owner
	 * watching the creature move, which is the right authority for them: the
	 * whole criterion is whether a planted foot appears to slide.
	 * tools/measure_animation_stride.py estimates 242.9 and 304.5 for the same
	 * two clips, high by 8% and 15%, and its own documentation calls its output
	 * an estimate good to roughly ten percent.
	 *
	 * WHAT WAS LOST, SAID PLAINLY. The play rate used to follow the Brute's real
	 * ground speed frame by frame; it is now a constant per clip. It differs only
	 * while the creature is accelerating or stopping, because those are the only
	 * moments its speed is neither 250 nor 500.
	 *
	 * THE TWO NUMBERS ARE BACK IN TEXT, which they were not when this comment
	 * was first written and which issue #406 was opened about.
	 * tools/read_animation_graph.py reads them out of ABP_Brute and writes them
	 * to game/Data/animation_graph_readings.json, which is committed. Three
	 * tests run the whole way from the design model to the bytes on disk:
	 *
	 *   the designed speeds -> this comment
	 *       test_brute_matches_the_model.py, the two play rate tests
	 *   this comment -> what ABP_Brute actually carries
	 *       test_animation_graph_reading_is_current.py, the play rates test
	 *   that reading -> the asset's own SHA-256
	 *       the same file, the reading-is-current test
	 *
	 * SO EDITING THIS COMMENT ALONE FAILS, and editing the asset without
	 * re-running the reader fails as well. Regenerate the record with:
	 *   python tools/run_editor_python.py tools/read_animation_graph.py
	 */

	/** Play rate floor. Below this the animation reads as frozen. */
	static constexpr float MinimumPlayRate = 0.2f;

	/** Play rate ceiling. Above this it reads as a blur. */
	static constexpr float MaximumPlayRate = 2.5f;

	/**
	 * Puts the Rampage mesh and ABP_Brute on, or logs why it could not and keeps
	 * the placeholder cylinder. Returns true when the skeletal mesh resolved.
	 *
	 * PUBLIC SO A TEST CAN CALL IT RATHER THAN INFER IT. BeginPlay calls this,
	 * but whether BeginPlay runs at all depends on how the world was made, and a
	 * test that spawns into a synthetic world and then checks the mesh cannot
	 * tell "the art is missing" apart from "BeginPlay did not fire". Calling it
	 * directly and reading the return value distinguishes the two, and it is
	 * safe to call twice: assigning the same mesh again is a no-op.
	 *
	 * @param bIncludeAnimation  Pass false to bind the mesh without loading the
	 *   animation Blueprint or any attack clip. Tests use this to check the mesh
	 *   binding without pulling in animation assets they do not need.
	 */
	bool ResolveBody(bool bIncludeAnimation = true);

	/**
	 * Puts ABP_Brute on the mesh component, or logs why it could not.
	 *
	 * SEPARATE FROM ResolveBody SO A TEST CAN ASK FOR IT ALONE and read whether
	 * it succeeded, which is the only way to tell "the animation Blueprint is
	 * absent" apart from "the whole art pack is absent". They are different
	 * failures: ABP_Brute is committed and the Paragon pack is not.
	 *
	 * @return true when the generated class loaded and was assigned.
	 */
	bool ResolveAnimationBlueprint(class USkeletalMeshComponent* MeshComponent);

};
