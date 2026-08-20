// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "CataclysmHellhoundCharacter.generated.h"

/**
 * The Hellhound: an aggressive charger that leaves fire trails.
 *
 * WHAT IT DOES, from `ABILITIES['Hellhound']` in
 * `sim/cataclysm_sim/enemy_abilities.py`:
 *
 *   Maul       Strike    Basic     a bite at whatever it is standing against
 *   Hellrush   Movement  Movement  a 10 m charge that leaves the lane burning
 *
 * **THE BURNING LANE IS THE ONLY THING IN THE GAME THAT BURNS ITS OWN SIDE.**
 * `GroundHitsAllies=1` in the model, and its note says what that means: "The
 * fire burns other enemies and the Hellhound itself." The roster in
 * `docs/Cataclysm_GDD_v2.md` says the same of no other creature. Every ground
 * effect before this one belonged to whoever cast it and hurt the other side,
 * so `ACataclysmGroundZone` gained a `bBurnsEveryone` for exactly this.
 *
 * WHAT MAKES THIS CREATURE DIFFERENT FROM THE OTHER TWO THAT EXIST. It is the
 * fastest thing in the roster at 7.5 metres per second, against a Brute's 3.0
 * and an Abyssal Warden's 2.8, and against player classes at 3.5 to 4.6. **It is
 * designed to catch the player**, which is the opposite of the Abyssal Warden,
 * and it is the first creature in this project for which that is true. Issue
 * #417 records the Brute doing it by accident and treats it as a defect; here it
 * is the design.
 *
 * ITS ART IS THE ONE PACK IN THIS PROJECT THAT IS TWO CREATURES. Iggy is a
 * goblin who rides Scorch, and the pack holds one skeletal mesh and one skeleton
 * for both of them. `game/docs/enemy-source-assets.md` assigns "the Scorch half"
 * to this enemy, and there is no Scorch half to assign: only 2 of the pack's 144
 * animations are Scorch's own and the rest drive the pair. **So this creature
 * currently wears the rider as well**, and whether that is acceptable is a
 * judgement nobody can make from a test, because the automation command runs
 * with `-nullrhi`. The mesh's material slots are named `Mount_` and `Pilot_`
 * separately, so hiding the rider is possible; it is not done here because
 * nobody has looked at it yet.
 */
UCLASS()
class CATACLYSM_API ACataclysmHellhoundCharacter : public ACataclysmEnemyCharacter
{
	GENERATED_BODY()

public:
	ACataclysmHellhoundCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	virtual void AttackTarget(AActor* Target) override;
	/**
	 * Seconds between its attacks, BEFORE any buff.
	 *
	 * NOT `SecondsBetweenAttacks`, WHICH IS `final` ON THE ENEMY BASE. That
	 * one divides this by whatever the creature's buffs are worth, and a
	 * creature that overrode it instead would ignore every buff in the game
	 * without a word. See ACataclysmEnemyCharacter::CommanderMultiplier.
	 */
	virtual float DesignedSecondsBetweenAttacks() const override;

	virtual TArray<FCataclysmEnemyAbility> EnemyAbilities() const override;
	virtual void UseEnemyAbility(int32 Index, AActor* Target,
								 const FVector& AimedAt) override;
	virtual void BeginEnemyAbilityWindUp(int32 Index, AActor* Target) override;

	/**
	 * Which entry of EnemyAbilities is which.
	 *
	 * ONE ABILITY, SO THERE IS NO PRIORITY TO GET WRONG. The Abyssal Warden has
	 * two and the order of its array is what decides which it reaches for; this
	 * creature has Hellrush and its bite, and a bite is not an entry here at
	 * all -- it is `MeleeReachCm` plus `AttackIntervalSeconds`.
	 */
	enum : int32 { HellrushAbility = 0 };

	virtual float AttackReachCm() const override { return DesignedMeleeReachCm; }
	virtual float SightRadiusCm() const override { return HellhoundNoticeRadiusCm; }
	virtual float RoamRadiusCm() const override { return HellhoundRoamRadiusCm; }

	// ----------------------------------------------------------------------
	// The designed stat block
	//
	// EVERY ONE OF THESE IS A COPY, and `sim/cataclysm_sim/enemy_stats.py` is
	// the original. `tools/tests/test_hellhound_matches_the_model.py` holds each
	// of them to that file, so when one of those fails the usual fix is to
	// change the C++ rather than the model. Continuous integration never builds
	// this file, so that Python test is the only thing checking these on a pull
	// request.
	// ----------------------------------------------------------------------

	/** Seconds between bites. `attack_interval` is 1.1. */
	static constexpr float DesignedAttackIntervalSeconds = 1.1f;

	/**
	 * How far it can bite from, centre to centre, in centimetres.
	 *
	 * MAUL'S OWN RADIUS, 0.9 metres, from `ABILITIES['Hellhound']`. The same
	 * figure the Abyssal Warden's reach is, and the model's note says why it is
	 * not telegraphed at that size: "a 1.1 second attack interval allows a 0.5
	 * metre marker, which is smaller than the animal standing in it".
	 */
	static constexpr float DesignedMeleeReachCm = 90.0f;

	/** Percent of all incoming damage resisted. `resistance` is 10.0. */
	static constexpr float DesignedResistancePercent = 10.0f;

	/** `crit_chance` 15.0 and `crit_multiplier` 175.0. The highest critical
	 *  strike chance in the slice, which suits a creature built to reach you. */
	static constexpr float DesignedCritChancePercent = 15.0f;
	static constexpr float DesignedCritMultiplierPercent = 175.0f;

	/**
	 * Percent of blows it avoids outright. `evasion` is 20.0.
	 *
	 * ITS DEFENCE IS NOT ARMOUR. `armor_share` is 0.30 against the Brute's 2.0,
	 * so this creature survives by not being hit rather than by absorbing.
	 * `enemy_stats.py` says so in as many words: evasion is the Imp's and the
	 * Hellhound's designed defence.
	 */
	static constexpr float DesignedEvasionPercent = 20.0f;

	/** `energy_shield_fraction` 0.0. Written out rather than left to the base's
	 *  default, so the zero is visibly designed rather than visibly forgotten. */
	static constexpr float DesignedEnergyShieldFraction = 0.0f;

	/**
	 * How fast it moves. `move_speed` is 7.5 metres per second.
	 *
	 * **THE FASTEST THING IN THE GAME.** Player classes run at 3.5, 4.0 and 4.6
	 * metres per second, so this creature closes on all of them and none of them
	 * outruns it. That is its design rather than an oversight: the roster calls
	 * it "an aggressive charger", and a charger that can be walked away from is
	 * not one.
	 *
	 * IT HAS NO SEPARATE CHASE SPEED. `chase_speed` is 0.0, so it moves at this
	 * one figure whether or not it has seen anything, the same arrangement the
	 * Abyssal Warden has and the opposite of the Brute's.
	 *
	 * THE BASE CLASS DOES NOT SET MaxWalkSpeed AT ALL. An enemy that forgets to
	 * set it moves at Unreal's default 600 cm/s, which for this creature would
	 * be a silent slowing rather than a silent speeding.
	 */
	static constexpr float DesignedWalkSpeedCmPerSecond = 750.0f;

	/** How fast it turns. `turn_rate_degrees` is the archetype default of 480,
	 *  which is what `ACataclysmEnemyCharacter` constructs every enemy with. */
	static constexpr float DesignedTurnRateDegreesPerSecond = 480.0f;

	// ----------------------------------------------------------------------
	// Its body
	// ----------------------------------------------------------------------

	/**
	 * The capsule's radius, in centimetres.
	 *
	 * 48 IS THE ARCHETYPE DEFAULT AND IT IS NOT MEASURED. `body_radius` is 0.48
	 * for this creature, which is the figure `enemy_stats.py` gives anything
	 * nobody has measured. Issue #366 is that gap: only the Imp has a measured
	 * radius and six enemies silently use this default. **The mesh is a
	 * quadruped with a rider on it and is unlikely to be 96 cm wide**, so this
	 * figure should be expected to move once somebody measures it.
	 */
	static constexpr float HellhoundCapsuleRadius = 48.0f;

	/**
	 * Half the capsule's height, in centimetres.
	 *
	 * FROM THE MESH. `game/docs/enemy-source-assets.md` records the IggyScorch
	 * mesh at 212.5 cm tall, measured from the asset, so half of it is 106.25
	 * and that is what the capsule uses. The mesh is dropped by exactly this in
	 * `ResolveBody`, which is what puts its feet on the capsule's bottom.
	 */
	static constexpr float HellhoundCapsuleHalfHeight = 106.25f;

	/** How far away it notices somebody, in centimetres. The same 10 metres the
	 *  Abyssal Warden uses. **No enemy has a designed notice radius** and issue
	 *  #383 is that gap; this matches the creature nearest it in size. */
	static constexpr float HellhoundNoticeRadiusCm = 1000.0f;

	/** How far from where it started it wanders while it has seen nothing. */
	static constexpr float HellhoundRoamRadiusCm = 600.0f;

	// ----------------------------------------------------------------------
	// Hellrush, the charge that leaves a lane on fire
	//
	// EVERY NUMBER HERE IS A COPY of `ABILITIES['Hellhound']` in
	// `sim/cataclysm_sim/enemy_abilities.py`, except the speed, which that file
	// does not state and which is marked below as the judgement it is.
	// ----------------------------------------------------------------------

	/** How far it charges, in centimetres. `Range` is 10 metres. */
	static constexpr float HellrushRangeCm = 1000.0f;

	/** Half the lane's width. `Radius` is 1.5 metres, the same narrow corridor
	 *  the Abyssal Warden's charge draws: a lane to step out of rather than a
	 *  wall to be caught by. */
	static constexpr float HellrushRadiusCm = 150.0f;

	/** Seconds before it may be used again. `cooldown` is 5.0, which is also
	 *  the Movement slot's cooldown in `game/Data/SkillSlots.csv` and the
	 *  minimum the telegraph rules set for anything drawing a large marker. */
	static constexpr float HellrushCooldownSeconds = 5.0f;

	/**
	 * How long the lane is on the ground before the creature sets off.
	 *
	 * DERIVED FROM THE RADIUS. The rule is `0.4 + Radius / 3.5` seconds, from
	 * the Attack Telegraphs subsection of `docs/Cataclysm_GDD_v2.md` and
	 * `wind_up_seconds` in the model. 0.4 + 1.5 / 3.5 is 0.8286, which both
	 * round to 0.83. The same figure the Abyssal Warden's charge uses, because
	 * it draws a lane of the same width.
	 */
	static constexpr float HellrushWindUpSeconds = 0.83f;

	static_assert(
		HellrushWindUpSeconds > 0.4f + HellrushRadiusCm / 100.0f / 3.5f - 0.002f
		&& HellrushWindUpSeconds < 0.4f + HellrushRadiusCm / 100.0f / 3.5f + 0.002f,
		"Hellrush's wind-up has drifted from the radius it is derived from. The "
		"rule is 0.4 + Radius / 3.5 seconds, from the Attack Telegraphs "
		"subsection of docs/Cataclysm_GDD_v2.md. Change both or neither.");

	/**
	 * How fast it travels while charging, in centimetres per second.
	 *
	 * **CHOSEN, NOT DERIVED, AND IT IS THE ONE NUMBER ON THIS CREATURE THAT IS.**
	 * The Abyssal Warden's charge speed falls out of its clip: 800 cm over the
	 * 0.700 seconds `Stampede` runs for. This creature has no charge clip at all
	 * -- of the Paragon Iggy and Scorch pack's 144 animations none is a charge --
	 * so there is no length to divide by and the number has to come from
	 * somewhere else.
	 *
	 * WHERE IT COMES FROM: the charge takes the same 0.700 seconds the Abyssal
	 * Warden's does, over this creature's longer 10 metres. A charge's DURATION
	 * is what a player reads once the marker has gone, so two charges that last
	 * the same time read the same, and 1000 / 0.7 is 1428.57.
	 *
	 * IT IS 1.9 TIMES THIS CREATURE'S OWN WALK, against the Abyssal Warden's
	 * 4.08. That is right rather than inconsistent: this creature already moves
	 * at 7.5 metres per second, so a charge does not have to be what makes it
	 * fast. **Only playing settles whether it is enough**, and the console
	 * variable below is what settles it without a rebuild.
	 */
	static constexpr float HellrushSpeedCmPerSecond = 1428.57f;

	/**
	 * The shortest distance worth charging, in centimetres.
	 *
	 * DERIVED, NOT CHOSEN. A charge that covers less ground than the creature
	 * could simply walk during its own wind-up is strictly worse than not
	 * winding up at all -- the design's own test, stated in this creature's own
	 * section. At 750 cm/s for 0.83 seconds that is 622.5 cm.
	 *
	 * **IT IS 62% OF THE RANGE, WHICH IS FAR NARROWER A BAND THAN THE ABYSSAL
	 * WARDEN'S 29%.** This creature walks so fast that most of its charge's
	 * reach is ground it could cover on foot anyway, so Hellrush is legal only
	 * between 6.2 and 10 metres. That follows from the design rather than from
	 * anything decided here, and it is worth knowing before somebody wonders
	 * why the creature rarely charges.
	 */
	static constexpr float HellrushMinimumRangeCm = 622.5f;

	static_assert(
		HellrushMinimumRangeCm
			> DesignedWalkSpeedCmPerSecond * HellrushWindUpSeconds - 0.01f
		&& HellrushMinimumRangeCm
			< DesignedWalkSpeedCmPerSecond * HellrushWindUpSeconds + 0.01f,
		"Hellrush's minimum range has drifted from the walk speed and wind-up it "
		"is derived from. A charge shorter than the creature could walk during "
		"its own telegraph is worse than no charge.");

	static_assert(HellrushMinimumRangeCm < HellrushRangeCm,
		"Hellrush can never be used: the shortest distance worth charging is "
		"further than it can charge. Either the creature got faster, the wind-up "
		"got longer, or the range got shorter.");

	/** What one pass is worth, as a percentage of an ordinary hit. The Movement
	 *  row of `game/Data/SkillSlots.csv`, the same figure the Abyssal Warden's
	 *  charge uses. */
	static constexpr float HellrushDamagePercent = 100.0f;

	/** How far it shoves what it runs through. `Knockback` is 4 metres, and it
	 *  is the third of the three enemy abilities the design names as displacing
	 *  the player. Issue #625 built the other two and could not build this one,
	 *  because this creature had no class to put it on. */
	static constexpr float HellrushKnockbackCm = 400.0f;

	/** Half the width of the lane it leaves burning. `GroundRadius` is 1.5
	 *  metres, the same as the charge's own, so what burned you is what the
	 *  marker showed. */
	static constexpr float HellrushGroundRadiusCm = 150.0f;

	/** How long the lane burns. `GroundDuration` is 4 seconds. */
	static constexpr float HellrushGroundSeconds = 4.0f;

	/** What one second in the fire is worth, as a percentage of an ordinary
	 *  hit. `GroundPercent` is 25.0, so standing in the whole 4 seconds costs
	 *  the same as one pass of the charge itself. */
	static constexpr float HellrushGroundPercent = 25.0f;

	/** The Hellrush cooldown really in use, which is the console override when
	 *  one is set and the designed figure otherwise. */
	static float HellrushCooldownSecondsInUse();

	/** The charge speed really in use, same arrangement. This is the figure the
	 *  header labels a judgement, so it is the one most likely to move in a play
	 *  session. */
	static float HellrushSpeedCmPerSecondInUse();

	/** The lane this creature last left burning, or null. Read by tests, which
	 *  have nothing else to look at. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class ACataclysmGroundZone> LastLaneLeftBurning;

	// ----------------------------------------------------------------------
	// Where its art lives
	// ----------------------------------------------------------------------

	static const TCHAR* BodyMeshPath;
	static const TCHAR* MaulAnimationPath;
	static const TCHAR* IdleAnimationPath;
	static const TCHAR* JogAnimationPath;
	static const TCHAR* HellrushAnimationPath;
	static const TCHAR* FirstDeathAnimationPath;
	static const TCHAR* SecondDeathAnimationPath;

	/**
	 * How long `Scorch_Primary_Fire_Med` runs, in seconds.
	 *
	 * MEASURED, NOT CHOSEN. `tools/probe_hellhound_animation.py` read 0.9667
	 * from the asset on 2026-08-20, and `game/docs/enemy-source-assets.md`
	 * records the same figure. It is the reason this clip was chosen over
	 * `Scorch_Primary_Fire_Fast` at 0.5333: it fills the 1.1 second interval
	 * without being compressed, leaving 0.13 seconds of rest rather than 0.57.
	 */
	static constexpr float MaulAnimationSeconds = 0.9667f;

	static_assert(MaulAnimationSeconds < DesignedAttackIntervalSeconds,
		"The Hellhound's bite is longer than the interval between bites, so one "
		"swing would still be playing when the next began. Either the clip "
		"changed or the designed interval did; see "
		"tools/probe_hellhound_animation.py and sim/cataclysm_sim/enemy_stats.py.");

	/**
	 * The ground speed `Jog_Fwd` was authored for, in centimetres per second.
	 *
	 * MEASURED, NOT GUESSED. `tools/measure_animation_stride.py` samples the two
	 * IK foot bones every frame, takes whichever is lower as the planted one,
	 * and averages how fast it travels backwards over the gait cycle. Run
	 * 2026-08-20 it reports **302.6 cm/s** on the -Y axis for this clip, with
	 * `IggyScorch_Idle` reading 0.0 as the control that shows the method is on
	 * the right axis for this rig -- which mattered more here than anywhere else,
	 * because this is the first rig measured that is two creatures.
	 *
	 * **SO THE PLAY RATE IS 750 / 302.6 = 2.478, WHICH IS ALMOST THE CEILING.**
	 * Nothing else in the project comes near it: the Abyssal Warden needs 0.994
	 * and the Brute 1.11 to walk and 1.43 to chase. This creature is designed to
	 * move at two and a half times the speed its only walking clip was drawn
	 * for, so it will read as a sped-up film rather than as a running animal.
	 *
	 * `Travelmode_Fwd` WAS MEASURED AS THE ALTERNATIVE AND IS SLOWER, at 268.1
	 * cm/s, so there is no faster clip in the pack to switch to. **Only the
	 * project owner can judge whether this looks acceptable**, and the automation
	 * command cannot: it runs with `-nullrhi`.
	 */
	static constexpr float AuthoredJogSpeedCmPerSecond = 302.6f;

	/** Above this the creature is walking, below it is standing. A threshold
	 *  rather than zero, because a character's velocity is rarely exactly zero
	 *  while it settles against the floor, and a walk clip flickering on for a
	 *  frame at a time reads worse than either state does on its own. */
	static constexpr float WalkingThresholdCmPerSecond = 10.0f;

	/** Play rate floor and ceiling, the same two figures the Abyssal Warden and
	 *  the Brute clamp to. Below the floor an animation reads as frozen; above
	 *  the ceiling it reads as a blur. **This creature's walk sits at 2.478,
	 *  which is inside the ceiling by 0.022.** */
	static constexpr float MinimumPlayRate = 0.2f;
	static constexpr float MaximumPlayRate = 2.5f;

	static_assert(
		DesignedWalkSpeedCmPerSecond / AuthoredJogSpeedCmPerSecond <= MaximumPlayRate,
		"The Hellhound's designed speed now needs a play rate above the ceiling, "
		"so its walk would be clamped and its feet would slide. Either measure a "
		"faster clip in the Paragon Iggy and Scorch pack, give the creature an "
		"animation Blueprint, or change the designed speed in "
		"sim/cataclysm_sim/enemy_stats.py.");

	/** The play rate the walk needs so its planted foot does not slide. */
	static float JogPlayRate();

	/** The seconds between bites really in use, which is the console override
	 *  when one is set and the designed figure otherwise. */
	static float AttackIntervalSecondsInUse();

	// ----------------------------------------------------------------------
	// What it is wearing and playing
	// ----------------------------------------------------------------------

	/**
	 * Puts the real mesh and its animations on, if the art pack is installed.
	 *
	 * PUBLIC SO A TEST CAN CALL IT, the same reason
	 * `ACataclysmAbyssalWardenCharacter::ResolveBody` is public: whether
	 * `BeginPlay` runs at all depends on how the world was built.
	 *
	 * @return whether the mesh was found and worn
	 */
	bool ResolveBody(bool bIncludeAnimation = true);

	/**
	 * Puts the right looping clip on, or leaves a one-shot alone to finish.
	 *
	 * PUBLIC SO A TEST CAN DRIVE IT WITHOUT TICKING A WORLD, which is what makes
	 * the standing-and-walking behaviour checkable at all.
	 */
	void UpdateLoopingAnimation();

	/** Standing and walking, once loaded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> IdleAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> JogAnimation;

	/** The bite. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> MaulAnimation;

	/** What plays while the lane is on the floor and the creature gathers
	 *  itself. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> HellrushAnimation;

	/** Which looping clip is on now, or null while a one-shot has the mesh.
	 *  Read by tests, which cannot otherwise see what is playing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> CurrentLoopingAnimation;

	/** The clip the last one-shot chose. Read by tests for the same reason. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> LastPlayedAnimation;

	/**
	 * When the clip a one-shot started will finish, in world seconds.
	 *
	 * THIS IS WHAT STOPS THE HELD FINAL FRAME. Without it nothing knows a bite
	 * has ended, so the mesh sits on the last pose until the next one begins.
	 * Zero means nothing is playing.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float OneShotEndsAtSeconds = 0.0f;

	/** Whether an animation Blueprint took the mesh. When one has, nothing here
	 *  sets an animation, because two things driving one component fight. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bAnimationBlueprintBound = false;

private:
	/** Plays one clip once and records it. Returns how long it will take, which
	 *  is its length divided by whatever rate it needed. A window of zero means
	 *  play it at its authored speed for as long as it is. */
	float PlayOneShot(class UAnimSequence* Animation, float HoldSeconds = 0.0f);

	void PlayMaulAnimation();
};
