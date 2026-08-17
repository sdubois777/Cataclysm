// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "CataclysmAbyssalWardenCharacter.generated.h"

/**
 * The Abyssal Warden: the mini-boss of the Demonic vertical slice.
 *
 * WHAT IT IS. The hardest thing in the slice to hurt, and the only designed
 * MELEE enemy that cannot catch anybody. Both facts are its design, and both
 * come from `ARCHETYPES["Abyssal Warden"]` in `sim/cataclysm_sim/enemy_stats.py`
 * and `ABILITIES["Abyssal Warden"]` in `sim/cataclysm_sim/enemy_abilities.py`.
 * Issue #353 designed it; issue #490 is this class.
 *
 * ALL THREE OF ITS DESIGNED ABILITIES ARE HERE:
 *
 *   Sunder       Basic     Strike    the ordinary attack, on the 2.4 s interval
 *   Stampede     Movement  Movement  an 8 m charge down a 1.5 m lane, every 5 s
 *   Molten Roar  Ultimate  Strike    a 5.6 m ring at its feet, every 12 s
 *
 * STAMPEDE IS WHAT LETS IT FIGHT AT ALL. It walks at 2.8 metres per second and
 * has no chase speed, against player classes at 3.5, 4.0 and 4.6, so a player
 * who walks backwards is never caught on foot. Issue #491 built the Movement
 * shape into the engine for exactly this; before that this creature could be
 * walked away from indefinitely and never fought.
 *
 * ORDER IN EnemyAbilities IS PRIORITY AND IT MATTERS HERE.
 * `ACataclysmEnemyController::ChooseAbility` reads an ability's MinRangeCm,
 * MaxRangeCm and CooldownSeconds and NOTHING ELSE -- it never looks at Shape --
 * and returns the first entry that fits. Molten Roar is listed first so that the
 * 12 second ring is not permanently crowded out by the 5 second charge in the
 * 2.32 to 5.60 metre band where both are legal.
 *
 * WHAT SUNDER IS AND IS NOT. It is the ordinary attack, so it is not an entry in
 * EnemyAbilities at all -- it is `MeleeReachCm` plus `AttackIntervalSeconds` and
 * the base class's `ACataclysmEnemyCharacter::AttackTarget`. Its designed 90
 * degree cone is NOT implemented and cannot be: `FCataclysmEnemyAbility` has no
 * Angle field and AttackTarget hits only the one target the brain chose. Its
 * MaxTargets of 1 holds by accident rather than by design.
 *
 * HOW IT IS SMALLER THAN THE BRUTE. `ACataclysmBruteCharacter` is 1,386 lines of
 * header and 1,276 of implementation, and most of that is machinery this
 * creature has none of: no projectile, no carried prop, no thrown rock, no
 * debris burst, no crater, no second gait. It also needs no montage: the Brute's
 * abilities are each two clips joined into one, and `Ultimate_Roar` is a single
 * clip, so the whole montage timing suite drops out with it.
 */
UCLASS()
class CATACLYSM_API ACataclysmAbyssalWardenCharacter : public ACataclysmEnemyCharacter
{
	GENERATED_BODY()

public:
	ACataclysmAbyssalWardenCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	virtual void AttackTarget(AActor* Target) override;
	virtual float SecondsBetweenAttacks() const override;

	/**
	 * HOW FAR IT WANDERS BEFORE IT HAS SEEN ANYTHING.
	 *
	 * THE OVERRIDE PEOPLE FORGET. `ACataclysmEnemyCharacter` overrides the other
	 * four hooks the brain calls by forwarding them to properties, and does NOT
	 * override this one. `ACataclysmCharacterBase::RoamRadiusCm` returns zero,
	 * and an enemy that does not override it never wanders at all.
	 */
	virtual float RoamRadiusCm() const override { return WardenRoamRadiusCm; }

	virtual TArray<FCataclysmEnemyAbility> EnemyAbilities() const override;
	virtual void UseEnemyAbility(int32 Index, AActor* Target,
								 const FVector& AimedAt) override;
	virtual void BeginEnemyAbilityWindUp(int32 Index, AActor* Target) override;

	/**
	 * Which entry of EnemyAbilities is which. ORDER IN THAT ARRAY IS PRIORITY,
	 * and `ACataclysmEnemyController::ChooseAbility` takes the first entry whose
	 * range and cooldown both allow it, without looking at the shape.
	 *
	 * MOLTEN ROAR IS FIRST DELIBERATELY. Both abilities are available at 2.32 to
	 * 5.60 metres. The ring is on a 12 second cooldown against the charge's 5,
	 * so it comes round less than half as often and would otherwise almost never
	 * be the entry that fits first.
	 */
	enum : int32 { MoltenRoarAbility = 0, StampedeAbility = 1 };

	// ----------------------------------------------------------------------
	// The designed figures
	// ----------------------------------------------------------------------
	//
	// EVERY ONE OF THESE IS A COPY, and the simulation is the original.
	// `tools/tests/test_warden_matches_the_model.py` holds each of them to
	// `sim/cataclysm_sim/enemy_stats.py` and `sim/cataclysm_sim/enemy_abilities.py`,
	// so when one of these fails the usual fix is to change the C++ rather than
	// the model. Continuous integration never builds this file, so that Python
	// test is the only thing that checks these on a pull request.

	/** Seconds between ordinary swings. `attack_interval` is 2.4. */
	static constexpr float DesignedAttackIntervalSeconds = 2.4f;

	/** How far it can hit from, centre to centre. `ATTACK_REACH` is 0.90 m. */
	static constexpr float DesignedMeleeReachCm = 90.0f;

	/** Percent of all incoming damage resisted. The highest in the slice. */
	static constexpr float DesignedResistancePercent = 35.0f;

	/** `crit_chance` 10.0 and `crit_multiplier` 200.0. */
	static constexpr float DesignedCritChancePercent = 10.0f;
	static constexpr float DesignedCritMultiplierPercent = 200.0f;

	/** `evasion` 0.0. Written out rather than left to the base's default, so
	 *  the zero is visibly designed rather than visibly forgotten. */
	static constexpr float DesignedEvasionPercent = 0.0f;

	/**
	 * `energy_shield_fraction` 0.0, for the same reason the evasion above is
	 * written out: the zero is designed rather than forgotten.
	 *
	 * TWO OF THE SEVEN SLICE ENEMIES CARRY ONE and this is not either of them.
	 * The Succubus is designed at 0.50 and the Corrupted Sentinel at 0.35; this
	 * creature's survivability is armour and resistance, which are the highest in
	 * the slice. Nothing could express a shield at all until issue #485.
	 */
	static constexpr float DesignedEnergyShieldFraction = 0.0f;

	/**
	 * How fast it walks. `move_speed` is 2.8 metres per second.
	 *
	 * IT HAS NO CHASE SPEED, unlike the Brute, and that is designed:
	 * `chase_speed` is 0.0, so it moves at this one figure whether or not it has
	 * seen anything. Against player classes at 3.5, 4.0 and 4.6 metres per
	 * second it can never catch anybody on foot.
	 *
	 * THE BASE CLASS DOES NOT SET MaxWalkSpeed AT ALL. An enemy that forgets to
	 * set it moves at Unreal's default 600 cm/s -- faster than the player -- so
	 * this is load-bearing rather than tidy.
	 */
	static constexpr float DesignedWalkSpeedCmPerSecond = 280.0f;

	/**
	 * How fast it turns. `turn_rate_degrees` is left at the archetype default of
	 * 480, which is the same figure `ACataclysmEnemyCharacter` constructs every
	 * enemy with. Only the Brute overrides it, at 180, because "can be
	 * outmanoeuvred" is its design and not this creature's.
	 */
	static constexpr float DesignedTurnRateDegreesPerSecond = 480.0f;

	// ----------------------------------------------------------------------
	// Molten Roar
	// ----------------------------------------------------------------------

	/**
	 * The ring's radius, in centimetres.
	 *
	 * 6.5 METRES IS THE CAP, AND SITTING AT IT IS THE DESIGN. It is
	 * `telegraph_cap_metres` in `sim/cataclysm_sim/enemy_abilities.py` for a
	 * 0.48 m body: the largest marker at which the slowest class still has its
	 * full 0.4 second reaction allowance and nothing more. Nothing in the game
	 * may be larger, so this is the hardest telegraph the rules permit.
	 *
	 * IT WAS 560 UNTIL 2026-08-09. The project owner played it and reported the
	 * ring was too easy to escape. Raising it only means anything because the
	 * wind-up is now capped at 2 seconds: before that a bigger ring warned for
	 * proportionally longer and was no harder. Issues #487 and #496.
	 */
	static constexpr float MoltenRoarRadiusCm = 650.0f;

	/** Seconds before it may be used again. Derived twice over in the design:
	 *  five of its 2.4 second swings, which is how long it takes to kill the
	 *  reference geared character, and the bottom of the Ultimate slot's band
	 *  in `game/Data/SkillSlots.csv`. */
	static constexpr float MoltenRoarCooldownSeconds = 12.0f;

	/**
	 * How long the marker is on the ground before the ring goes off.
	 *
	 * THE CEILING, NOT THE FORMULA, AND THAT CHANGED ON 2026-08-09. The wind-up
	 * rule in section X of `docs/Cataclysm_GDD_v2.md` is the LESSER of
	 * `0.4 + Radius / 3.5` and 2.0 seconds. At this ring's 6.5 metre radius the
	 * formula alone would give 2.257, so the ceiling is what decides it.
	 *
	 * THAT IS THE WHOLE REASON THE RING GOT BIGGER. Under the formula alone a
	 * wider ring warns for proportionally longer and hands the player back
	 * exactly the ground it took away. Held at 2 seconds, the extra 0.9 metres
	 * of radius is 0.9 metres the player must cross with no extra time to do it.
	 * Issues #487 and #496.
	 *
	 * IT IS STILL THE LONGEST TELEGRAPH IN THE GAME, and now it is the longest
	 * any telegraph may ever be. The Brute's stomp is 1.4 seconds.
	 */
	static constexpr float MoltenRoarWindUpSeconds = 2.0f;

	// AT THE CEILING, so the assert is against the ceiling rather than against
	// the formula. A tolerance rather than equality for the reason the charge's
	// speed assert records: neither 0.4 nor 3.5 divides exactly in binary
	// floating point, so the formula side lands a fraction either way.
	static_assert(
		MoltenRoarWindUpSeconds
			<= 0.4f + MoltenRoarRadiusCm / 100.0f / 3.5f + 0.001f,
		"Molten Roar's wind-up is longer than the uncapped formula allows, "
		"which cannot happen: the rule is the LESSER of 0.4 + Radius / 3.5 and "
		"the 2 second ceiling. See the Attack Telegraphs subsection of "
		"docs/Cataclysm_GDD_v2.md.");

	// AND THE RADIUS IS AT THE CAP THE CEILING IMPLIES. The cap is
	// 3.5 * (ceiling - 0.4) + contact, where contact is the player's 0.42 m plus
	// this creature's 0.48 m body. Written out so that changing the ceiling
	// moves the radius with it rather than leaving the two to drift.
	static_assert(
		MoltenRoarRadiusCm
			> (3.5f * (MoltenRoarWindUpSeconds - 0.4f) + 0.42f + 0.48f)
				* 100.0f - 0.1f
		&& MoltenRoarRadiusCm
			< (3.5f * (MoltenRoarWindUpSeconds - 0.4f) + 0.42f + 0.48f)
				* 100.0f + 0.1f,
		"Molten Roar's radius is no longer the cap its own wind-up ceiling "
		"implies. The ring is designed to sit exactly at the largest marker the "
		"rules permit, which is where the slowest class still has its full 0.4 "
		"second reaction allowance and nothing more. Issues #487 and #496.");

	/** What one use is worth, as a percentage of an ordinary hit. The Ultimate
	 *  row of `game/Data/SkillSlots.csv`. It is the first thing in the game to
	 *  use that slot. */
	static constexpr float MoltenRoarDamagePercent = 400.0f;

	/**
	 * What the molten roar IS, as gameplay tags. Issue #519.
	 *
	 * A POINT-BLANK AREA, which is what Scorching Arc carries, without that
	 * skill's Type.AOE.Persistent because this leaves no burning ground.
	 *
	 * NO Keyword.CC, and that is deliberate rather than forgotten. The comment
	 * at the call site says why: the Brute's stomp is the one thing in this
	 * slice that holds the player still, and a second would spend most of its
	 * uses inside the five second stun immunity window.
	 *
	 * `Type.AOE.PointBlank` IS WHAT MAKES IT UNEVADABLE, and it is now the only
	 * thing that does; the call site used to say so a second way.
	 */
	static const TCHAR* MoltenRoarTags;

	// ----------------------------------------------------------------------
	// Stampede, the charge
	// ----------------------------------------------------------------------
	//
	// WHY IT EXISTS. This creature walks at 2.8 metres per second with no chase
	// speed, against player classes at 3.5, 4.0 and 4.6. Without a gap-closer a
	// player walks backwards and it never fights. Issue #491 built the engine
	// side; this is the ability.

	/** How far it charges. 8 metres, the shortest Movement-shape skill range in
	 *  `game/Data/WeaponSkills.csv`, which is the right one for the slowest
	 *  creature. */
	static constexpr float StampedeRangeCm = 800.0f;

	/** Half the lane's width. 1.5 metres, the narrowest radius any player
	 *  Charge-mode skill uses, so the marker is a lane to step out of rather
	 *  than a wall. */
	static constexpr float StampedeRadiusCm = 150.0f;

	/** Seconds before it may be used again. The Movement slot's cooldown in
	 *  `game/Data/SkillSlots.csv`, and the 5 second minimum the telegraph rules
	 *  set for anything that draws a large marker. */
	static constexpr float StampedeCooldownSeconds = 5.0f;

	/**
	 * How long the lane is on the ground before the creature sets off.
	 *
	 * DERIVED FROM THE RADIUS, exactly as Molten Roar's is. The rule is
	 * `0.4 + Radius / 3.5`, and 0.4 + 1.5 / 3.5 is 0.8286. The design document
	 * and the model both round it to 0.83, so that is what is carried here and
	 * the assert below allows the rounding.
	 */
	static constexpr float StampedeWindUpSeconds = 0.83f;

	static_assert(
		StampedeWindUpSeconds
			> 0.4f + StampedeRadiusCm / 100.0f / 3.5f - 0.002f
		&& StampedeWindUpSeconds
			< 0.4f + StampedeRadiusCm / 100.0f / 3.5f + 0.002f,
		"Stampede's wind-up has drifted from the radius it is derived from. The "
		"rule is 0.4 + Radius / 3.5 seconds, from the Attack Telegraphs "
		"subsection of docs/Cataclysm_GDD_v2.md. Change both or neither.");

	/**
	 * How long the `Stampede` animation clip runs, in seconds.
	 *
	 * MEASURED, NOT CHOSEN. `game/docs/enemy-source-assets.md` records the Grux
	 * pack's `Stampede` at 0.7000 seconds, measured 2026-08-09. The model's note
	 * on the ability names the same figure, and it is the reason the design
	 * picked a Charge over a Leap: this is one clip where a leap is five.
	 */
	static constexpr float StampedeAnimationSeconds = 0.700f;

	/**
	 * How fast it travels while charging, in centimetres per second.
	 *
	 * THIS IS A JUDGEMENT AND IT IS LABELLED ONE. No charge speed is stated in
	 * the design document, in the model, or in any shipped game that publishes
	 * its numbers -- Path of Exile's monster charge publishes a 4 second
	 * cooldown and a 2.75 second cast time and no travel speed, and neither Last
	 * Epoch nor Diablo publishes one either. So it is derived from this
	 * project's own figures rather than read off another game.
	 *
	 * THE RULE: A CHARGE COVERS ITS RANGE IN THE LENGTH OF ITS OWN CLIP. That is
	 * the same rule every other timing in this project already follows -- the
	 * Brute's montage delays, this creature's jog play rate against its measured
	 * stride -- and it makes the speed derived rather than invented. 800 cm in
	 * 0.700 s is 1142.9 cm/s.
	 *
	 * WHAT BOUNDS IT FROM BELOW, TWICE OVER.
	 *
	 * It must beat the fastest class or the charge closes nothing: 11.43 metres
	 * per second against 4.6 is two and a half times.
	 *
	 * And it must be far quicker than walking, or winding up is strictly worse
	 * than not. That is the design's own test, stated for the Hellhound's
	 * charge: "a charge shorter than that would be strictly worse than not
	 * winding up at all". At this creature's own 2.8 metres per second the same
	 * 8 metres would take 2.86 seconds -- longer than its whole attack interval.
	 *
	 * WHAT IT COSTS THE CREATURE, WHICH IS WHAT MAKES IT FAIR. It runs the full
	 * 8 metres whether or not anything is still there, so a miss leaves it up to
	 * 8 metres past the player facing away, and walking that back takes 2.86
	 * seconds before it has even turned. 0.70 seconds of closing bought with
	 * 2.86 seconds of walking back is the window the telegraph buys.
	 *
	 * A LITERAL WITH AN ASSERT BESIDE IT, NOT THE EXPRESSION, and that is the
	 * pattern `ACataclysmBruteCharacter::RockThrowMinimumRangeCm` uses for the
	 * same reason. Continuous integration builds no C++ at all, so a
	 * `static_assert` is unchecked on a pull request. Written as a number,
	 * `tools/tests/test_warden_matches_the_model.py` can read it out of the
	 * source as text and recompute the division, and that test DOES run.
	 */
	static constexpr float StampedeSpeedCmPerSecond = 1142.86f;

	// A TOLERANCE RATHER THAN EQUALITY, for the reason Molten Roar's wind-up
	// assert records: 800 / 0.7 is 1142.857142... and no rounding of it is
	// exactly representable in binary floating point.
	static_assert(
		StampedeSpeedCmPerSecond
			> StampedeRangeCm / StampedeAnimationSeconds - 0.01f
		&& StampedeSpeedCmPerSecond
			< StampedeRangeCm / StampedeAnimationSeconds + 0.01f,
		"Stampede's speed has drifted from the range and clip length it is "
		"derived from. The rule is that a charge covers its range in the "
		"length of its own animation clip. Change both or neither.");

	/**
	 * The nearest target worth charging, in centimetres.
	 *
	 * DERIVED FROM THE DESIGN'S OWN TEST, not picked. The Hellhound's charge
	 * section states the rule: a charge is only worth winding up for if it beats
	 * simply walking during that wind-up, because otherwise "a charge shorter
	 * than that would be strictly worse than not winding up at all". This
	 * creature walks 2.8 metres per second for 0.83 seconds, which is 2.32
	 * metres. Inside that it should take the step rather than the charge.
	 *
	 * A LITERAL WITH AN ASSERT BESIDE IT, for the reason the speed above gives:
	 * continuous integration builds no C++, so only a number a Python test can
	 * read out of the source is really checked on a pull request.
	 */
	static constexpr float StampedeMinimumRangeCm = 232.4f;

	static_assert(
		StampedeMinimumRangeCm
			> DesignedWalkSpeedCmPerSecond * StampedeWindUpSeconds - 0.01f
		&& StampedeMinimumRangeCm
			< DesignedWalkSpeedCmPerSecond * StampedeWindUpSeconds + 0.01f,
		"StampedeMinimumRangeCm has drifted from the distance the creature "
		"walks during its own wind-up, which is what it is derived from. Below "
		"that figure it arrives sooner by taking a step, so the charge is "
		"strictly worse than not winding up.");

	/** What one pass is worth, as a percentage of an ordinary hit. The Movement
	 *  row of `game/Data/SkillSlots.csv`, whose note says "the design says some
	 *  also deal damage, so a basic attack's worth is the right middle". */
	static constexpr float StampedeDamagePercent = 100.0f;

	/**
	 * How far it shoves what it runs through, in centimetres. Knockback=4.
	 *
	 * THE TOP OF THE BAND THE DESIGN NAMES, and it takes the larger of the two
	 * figures because this charge does not stun. The design puts an enemy shove
	 * between the player's own two numeric knockbacks -- Molten Crush's 3 metres
	 * and Searing Hook's 4 -- and notes that Path of Exile's default knockback
	 * distance is 4 units. The Brute's Stomp takes the 3 because it also holds
	 * the player still for 1.5 seconds; nothing else about this charge denies the
	 * player anything after it has passed, so it takes the 4.
	 *
	 * IT LEAVES THE LANE, DIAGONALLY RATHER THAN STRAIGHT OUT. The shove is
	 * away from the creature, and a charge meets its target at the LEADING edge
	 * of its lane rather than beside them: 150 cm of half-width against somebody
	 * 75 cm off the axis puts first contact about 130 cm short. So the push
	 * carries them forward as well as out -- measured at 334 cm along against
	 * 219 cm across -- and they still finish outside the lane, which is what
	 * knocking a crowd aside has to achieve.
	 *
	 * Issue #625.
	 */
	static constexpr float StampedeKnockbackCm = 400.0f;

	// ----------------------------------------------------------------------
	// The body
	// ----------------------------------------------------------------------

	/**
	 * How wide the collision capsule is.
	 *
	 * DELIBERATELY NOT THE ART'S WIDTH, for the reason the Brute's header
	 * records: the melee reach is 90 cm, so a capsule wider than 48 would put
	 * the creature permanently outside its own reach and it could never touch
	 * anything. `EnemyCapsuleRadius` on `ACataclysmEnemyCharacter` is the same
	 * 48, so this is the shared figure rather than a Warden number.
	 *
	 * It is also `body_radius` in the model, 0.48 m, which is the DEFAULT that
	 * six of the seven enemies use because nobody has measured them. Issue #366.
	 */
	static constexpr float WardenCapsuleRadius = 48.0f;

	/**
	 * How tall the collision capsule is, measured from its centre.
	 *
	 * 114 IS BOUNDED FROM BOTH SIDES AND THE UPPER BOUND IS NOT OBVIOUS.
	 *
	 * From below, the art: `game/docs/enemy-source-assets.md` measures the
	 * GruxMolten mesh at 227.8 cm tall, so its true half-height is 113.9.
	 *
	 * From above, a test. `ACataclysmEnemyController` compares reach on the
	 * floor plane, but `ContactToleranceCm` is only 2.0 cm of slack, and the
	 * two capsule centres do not sit at the same height. The player's capsule
	 * half-height is 96. At contact the two bodies are 42 + 48 = 90 cm apart
	 * horizontally, so the 3D distance is sqrt(90 squared + height gap
	 * squared), and that must stay inside 90 + 2. Rearranged, the height gap
	 * must be at most 19.08 cm, so this figure must be at most 115.08.
	 *
	 * 114 CLEARS IT BY 0.22 CM. 116 would not. Do not round this up.
	 */
	static constexpr float WardenCapsuleHalfHeight = 114.0f;

	/**
	 * How far away it notices the player, and how far it wanders before it has.
	 *
	 * NEITHER IS A DESIGNED FIGURE. No enemy has a designed notice radius --
	 * issue #383 -- and the Brute's 1000 and 600 were settled by playing it
	 * against the 1200 cm sandbox spawn distance, so that roaming is visible and
	 * the creature notices the player at about the moment the player notices it.
	 * They are carried over here rather than invented, and they should move
	 * together with the Brute's when #383 settles them.
	 */
	static constexpr float WardenNoticeRadiusCm = 1000.0f;
	static constexpr float WardenRoamRadiusCm = 600.0f;

	// ----------------------------------------------------------------------
	// Art
	// ----------------------------------------------------------------------

	/** The skeletal mesh. `GruxMolten` is a separate mesh rather than a material
	 *  on the base Grux, and it shares `Grux_Skeleton` with it, so every clip
	 *  below plays on it without retargeting. */
	static const TCHAR* BodyMeshPath;

	/**
	 * The animation Blueprint, WHICH DOES NOT EXIST YET.
	 *
	 * `ABP_Brute` was authored by hand in the editor in pull request #407, and
	 * `tools/probe_brute_animation.py` established that Unreal's Python exposes
	 * no way to connect two animation graph pins, so one cannot be generated.
	 *
	 * WHAT HAPPENS WITHOUT IT. `ResolveBody` falls back to the single-clip
	 * animation mode this project used until 2026-08-08, so the swing and the
	 * roar are still visible and are played straight onto the mesh component.
	 * What is lost is blending: walking does not blend into the idle, so the
	 * creature slides rather than steps. Authoring one is #387-shaped work.
	 */
	static const TCHAR* AnimationBlueprintPath;

	/**
	 * ONE BASIC ATTACK, PLAYED AS THREE CLIPS: a left swing, a right swing, and
	 * a recovery that puts the creature back to a neutral stance.
	 *
	 * ASKED FOR BY THE PROJECT OWNER on 2026-08-09: "if he has an attack for
	 * both arms include them both so he gives a good left right", after
	 * reporting that the creature "just kinda teleports back to the neutral
	 * position".
	 *
	 * THE TELEPORT WAS REAL AND IT WAS MEASURED. `tools/measure_warden_recovery.py`
	 * evaluates each clip's last pose against the idle's first, which is exactly
	 * the size of the jump the player sees when the animation switches:
	 *
	 *     PrimaryAttack_LA            151.18 cm from the idle
	 *     PrimaryAttack_LA_Recovery    16.92 cm
	 *     PrimaryAttack_RA            161.78 cm
	 *     PrimaryAttack_RA_Recovery    24.12 cm
	 *     Ultimate_Roar                 0.39 cm
	 *
	 * So a swing ends a metre and a half of hand travel away from neutral and
	 * its recovery ends within a hand's width of it. The roar needs no recovery
	 * at all -- it already returns to neutral, which is why it has none in the
	 * pack and none here.
	 *
	 * THE FAST VARIANTS, NOT THE FULL-SPEED ONES, AND THE MEASUREMENT CHOSE
	 * THEM. `PrimaryAttack_LA_Fast` into `PrimaryAttack_RA_Fast` joins at
	 * **0.01 cm** -- the pack authored those two as a chain. The full-speed pair
	 * joins at 12.57 cm, which would be a visible hitch in the middle of the
	 * combo.
	 *
	 * AND ONLY THE FAST PAIR FITS. Against the 2.4 second attack interval:
	 *
	 *     left, right, recovery at full speed   3.100 s   needs a 1.29 play rate
	 *     the fast pair then a recovery         2.100 s   fits, 0.3 s to spare
	 *
	 * `PrimaryAttack_RA_Recovery` is used rather than the left one because
	 * `RA_Fast` joins it at 16.24 cm where `LA_Fast` joins `LA_Recovery` at
	 * 80.73: the recoveries are matched to the full-speed swings, and only that
	 * one follows a fast swing acceptably.
	 *
	 * A MONTAGE WOULD BE THE RIGHT CONTAINER AND IS NOT AVAILABLE. The project
	 * owner asked "we should be able to bundle that into a montage and call a
	 * double swing the basic attack right?" -- yes, and the Brute does exactly
	 * that. A montage needs a `UAnimInstance` to play it, which means an
	 * animation Blueprint, and this creature has none. The three clips are
	 * therefore queued in C++ and played one after another, which is why
	 * `AttackSequence` exists. When #387 authors the Blueprint this becomes one
	 * montage asset and the queue goes away.
	 */
	static const TCHAR* LeftSwingAnimationPath;
	static const TCHAR* RightSwingAnimationPath;
	static const TCHAR* SwingRecoveryAnimationPath;
	static const TCHAR* MoltenRoarAnimationPath;

	/**
	 * The charge.
	 *
	 * `Stampede` RATHER THAN `Stampede_Knockup`. The pack has both, at 0.7000
	 * and 1.5333 seconds. The knock-up variant is the wrong one twice over:
	 * nothing in this project can knock a target up or back, issue #310, and its
	 * length does not fit the 0.83 second wind-up.
	 */
	static const TCHAR* StampedeAnimationPath;

	/**
	 * Standing still, and walking.
	 *
	 * WHY A CREATURE WITH NO ANIMATION BLUEPRINT NEEDS THESE NAMED. With a
	 * Blueprint the animation graph picks a locomotion clip and blends it. There
	 * is none, so nothing picks one and nothing returns the mesh to a resting
	 * pose after an attack. The project owner reported both on 2026-08-09: "he
	 * doesn't walk, he slides", and "at the end of his basic attack animation he
	 * holds the final frame till he attacks again".
	 *
	 * `UpdateLoopingAnimation` is what plays these, and it is the whole of the
	 * fallback's state machine: standing, walking, or held by a one-shot.
	 */
	static const TCHAR* IdleAnimationPath;
	static const TCHAR* JogAnimationPath;

	/** The montage slot an attack clip is played into when an animation
	 *  Blueprint is present. The same slot name the Brute uses. */
	static const FName AttackSlotName;

	static constexpr float AttackBlendInSeconds = 0.15f;
	static constexpr float AttackBlendOutSeconds = 0.15f;

	/**
	 * Puts the GruxMolten mesh on, and either an animation Blueprint or the
	 * single-clip fallback. Returns true when the skeletal mesh resolved.
	 *
	 * PUBLIC SO A TEST CAN CALL IT RATHER THAN INFER IT, for the reason the
	 * Brute's header gives: whether BeginPlay ran at all depends on how the
	 * world was made, so a test that spawns into a synthetic world and then
	 * checks the mesh cannot tell "the art is missing" from "BeginPlay did not
	 * fire". Calling this and reading the return value distinguishes them, and
	 * it is safe to call twice.
	 *
	 * @param bIncludeAnimation  Pass false to bind the mesh without loading any
	 *   animation. Tests use this to check the mesh alone.
	 */
	bool ResolveBody(bool bIncludeAnimation = true);

	/** True when the mesh is being driven by an animation Blueprint rather than
	 *  by the single-clip fallback. Read by tests. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bAnimationBlueprintBound = false;

	/** The two swings and the roar, once loaded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> LeftSwingAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> RightSwingAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> SwingRecoveryAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> MoltenRoarAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> StampedeAnimation;

	/**
	 * The clips still to play from the current attack, in order.
	 *
	 * THE STAND-IN FOR A MONTAGE. Each is started as the one before it ends,
	 * from `UpdateLoopingAnimation`, and when the list runs out the creature
	 * goes back to standing or walking. A montage would hold the same three
	 * clips in one asset and blend between them; this cuts. The measured cuts
	 * are 0.01 cm and 16.24 cm, against the 151 cm jump it replaces.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TArray<TObjectPtr<class UAnimSequence>> AttackSequence;

	/** How far through `AttackSequence` the creature is. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	int32 AttackSequenceIndex = 0;

	/** The clip the last call to PlayOneShot chose. Read by tests, which cannot
	 *  otherwise see what was played. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> LastPlayedAnimation;

	/** The three clips one basic attack plays, in order, without starting them.
	 *  Separated from PlayAttackAnimation so a test can ask without playing. */
	TArray<class UAnimSequence*> BasicAttackClips() const;

	/** Plays one clip once, and records it. Returns how long it will take,
	 *  which is its length divided by whatever rate it needed. */
	float PlayOneShot(class UAnimSequence* Animation, float HoldSeconds = 0.0f);

	/**
	 * The ground speed `Jog_Fwd` was authored for, in centimetres per second.
	 *
	 * MEASURED, NOT GUESSED. `tools/measure_animation_stride.py` samples the two
	 * IK foot bones every frame, takes whichever is lower as the planted one,
	 * and averages how fast it travels backwards over the gait cycle. Run
	 * 2026-08-09 it reports **281.6 cm/s** on the -Y axis for this clip, with
	 * `Idle` reading 0.0 as the control that shows the method is on the right
	 * axis for this rig.
	 *
	 * SO THE PLAY RATE IS 280 / 281.6 = 0.994, which is as close to the authored
	 * speed as any of the seven gets. The Brute needs 1.11 for its walk and 1.43
	 * for its chase. That is luck rather than design, and it is why this
	 * creature's walk can be made to stop sliding without an animation Blueprint.
	 *
	 * TREAT IT AS GOOD TO ROUGHLY TEN PERCENT. The script's own documentation
	 * says so, because the IK foot bones never quite touch the ground, so "the
	 * lower foot" is an approximation of "the planted foot". If it still slides
	 * visibly, this is the number to tune by eye, the way the Brute's 225 was.
	 */
	static constexpr float AuthoredJogSpeedCmPerSecond = 281.6f;

	/**
	 * Above this the creature is walking, below it is standing.
	 *
	 * A THRESHOLD RATHER THAN ZERO, because a character's velocity is rarely
	 * exactly zero while it settles against the floor or turns on the spot, and
	 * a walk clip flickering on for a frame at a time reads worse than either
	 * state does on its own.
	 */
	static constexpr float WalkingThresholdCmPerSecond = 10.0f;

	/** The play rate the walk needs so its planted foot does not slide. */
	static float JogPlayRate();

	/** The cooldown Molten Roar is really using, which is the console override
	 *  when one is set and the designed figure otherwise. Read through this
	 *  rather than off the constant, so a console variable set mid-fight takes
	 *  effect on the next thinking pass. */
	static float MoltenRoarCooldownSecondsInUse();

	/** The seconds between swings really in use, same arrangement. */
	static float AttackIntervalSecondsInUse();

	/** The Stampede cooldown really in use, same arrangement. */
	static float StampedeCooldownSecondsInUse();

	/** The charge speed really in use, same arrangement. This is the figure the
	 *  header labels a judgement, so it is the one most likely to move in a play
	 *  session. */
	static float StampedeSpeedCmPerSecondInUse();

	/** Play rate floor and ceiling, the same two figures the Brute clamps to.
	 *  Below the floor an animation reads as frozen; above the ceiling it reads
	 *  as a blur. Molten Roar needs no compression at all -- 1.4 seconds inside
	 *  a 2.0 second wind-up -- so these only bite if a clip is ever replaced. */
	static constexpr float MinimumPlayRate = 0.2f;
	static constexpr float MaximumPlayRate = 2.5f;

	/** Standing and walking, once loaded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> IdleAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> JogAnimation;

	/** Which looping clip is on now, or null while a one-shot has the mesh.
	 *  Read by tests, which cannot otherwise see what is playing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> CurrentLoopingAnimation;

	/**
	 * When the clip a one-shot started will finish, in world seconds.
	 *
	 * THIS IS WHAT STOPS THE HELD FINAL FRAME. Without it nothing knew a swing
	 * had ended, so the mesh sat on the last pose of the attack until the next
	 * one began. Zero means nothing is playing.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float OneShotEndsAtSeconds = 0.0f;

	/**
	 * Puts the right looping clip on, or leaves a one-shot alone to finish.
	 *
	 * PUBLIC SO A TEST CAN DRIVE IT WITHOUT TICKING A WORLD, which is what makes
	 * the standing-and-walking behaviour checkable at all.
	 *
	 * DOES NOTHING WHEN AN ANIMATION BLUEPRINT IS BOUND. The graph owns the mesh
	 * then, and two things setting the same component's animation would fight.
	 */
	void UpdateLoopingAnimation();

private:
	void PlayAttackAnimation();
	bool ResolveAnimationBlueprint(class USkeletalMeshComponent* MeshComponent);
};
