// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "CataclysmCorruptedSentinelCharacter.generated.h"

/**
 * The Corrupted Sentinel: a turret. It never moves and it never stops shooting.
 *
 * WHAT IT DOES, from `ABILITIES['Corrupted Sentinel']` in
 * `sim/cataclysm_sim/enemy_abilities.py`:
 *
 *   Siege Bolt        Projectile  Basic    a flat bolt down a 14 metre lane
 *   Brimstone Mortar  Projectile  Special  a lobbed shell bursting 3 m wide
 *
 * **IT IS THE ONLY CREATURE IN THE ROSTER THAT CANNOT MOVE.** `move_speed` is
 * 0.0 and so is `chase_speed`. The design's one line about it is "Stationary
 * ranged. Forces the player to stay mobile", and everything else about the
 * creature follows: it has no walk clip, no roam radius, and the whole of its
 * threat is that the ground it covers is ground you cannot stand on.
 *
 * **ITS BASIC ATTACK IS AN ENTRY IN `EnemyAbilities`, WHICH IS NEW HERE.** Every
 * other creature's basic attack is `MeleeReachCm` plus `AttackIntervalSeconds`
 * and reaches the target through `AttackTarget`. That path has no wind-up and
 * draws no marker, and this creature's basic attack is TELEGRAPHED -- Siege Bolt
 * marks a lane for a full second before it fires.
 *
 * NO NEW MACHINERY WAS NEEDED FOR THAT, and it is worth saying exactly why,
 * because the obvious conclusion is that some was.
 * `ACataclysmEnemyController::UseAbilitiesOn` gates every ability by the
 * creature's own `SecondsBetweenAttacks()` as well as by the ability's own
 * cooldown, and it stamps that interval when a wind-up LANDS. So an ability with
 * a cooldown of zero fires exactly on the attack interval, which is what a basic
 * attack is, and gets a marker and a wind-up for nothing. `AttackTarget` is
 * overridden here to do nothing at all.
 *
 * THREE OF THE SEVEN HAVE A TELEGRAPHED BASIC ATTACK, not one. The design
 * document says in the Gatekeeper's section that its Dread Cleave is the only
 * one; the Corrupted Sentinel's Siege Bolt and the Succubus's Soulfire are the
 * others, and the same document says so about the Succubus a few pages earlier.
 * Issue #763.
 *
 * WHY THE MORTAR IS LISTED FIRST. `ChooseAbility` takes the first entry whose
 * range and cooldown fit and never looks at the shape. Siege Bolt has a cooldown
 * of zero and the same 14 metre range, so listing it first would make it the
 * only thing this creature ever does and the mortar would never fire at all.
 * That is issue #491 on the Abyssal Warden, which is the same defect with the
 * numbers changed.
 */
UCLASS()
class CATACLYSM_API ACataclysmCorruptedSentinelCharacter
	: public ACataclysmEnemyCharacter
{
	GENERATED_BODY()

public:
	ACataclysmCorruptedSentinelCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	virtual void AttackTarget(AActor* Target) override;
	virtual float SecondsBetweenAttacks() const override;

	virtual TArray<FCataclysmEnemyAbility> EnemyAbilities() const override;
	virtual void UseEnemyAbility(int32 Index, AActor* Target,
								 const FVector& AimedAt) override;
	virtual void BeginEnemyAbilityWindUp(int32 Index, AActor* Target) override;

	/**
	 * Which entry of EnemyAbilities is which.
	 *
	 * THE ORDER IS THE PRIORITY AND IT IS NOT THE DESIGN'S ORDER. The model
	 * lists Siege Bolt first because it is the Basic; this array lists the
	 * mortar first because a zero-cooldown ability at the front crowds out
	 * everything behind it. See the class comment.
	 */
	enum : int32 { BrimstoneMortarAbility = 0, SiegeBoltAbility = 1 };

	virtual float AttackReachCm() const override { return SiegeBoltRangeCm; }
	virtual float SightRadiusCm() const override { return SentinelNoticeRadiusCm; }
	virtual float RoamRadiusCm() const override { return SentinelRoamRadiusCm; }

	// ----------------------------------------------------------------------
	// The designed stat block
	//
	// EVERY ONE OF THESE IS A COPY, and `sim/cataclysm_sim/enemy_stats.py` is
	// the original. `tools/tests/test_sentinel_matches_the_model.py` holds each
	// of them to that file. Continuous integration never builds this file, so
	// that Python test is the only thing checking these on a pull request.
	// ----------------------------------------------------------------------

	/** Seconds between shots. `attack_interval` is 2.0. */
	static constexpr float DesignedAttackIntervalSeconds = 2.0f;

	/** Percent of all incoming damage resisted. `resistance` is 20.0, the second
	 *  highest in the slice behind the Gatekeeper's 30. */
	static constexpr float DesignedResistancePercent = 20.0f;

	/** `crit_chance` 5.0 and `crit_multiplier` 150.0, the archetype baseline for
	 *  both. A turret does not need to land big hits; it needs to land them
	 *  wherever you are standing. */
	static constexpr float DesignedCritChancePercent = 5.0f;
	static constexpr float DesignedCritMultiplierPercent = 150.0f;

	/** `evasion` is 0.0. It cannot dodge, which is what a thing bolted to the
	 *  ground should not be able to do. Written out rather than left to the
	 *  base's default so the zero is visibly designed. */
	static constexpr float DesignedEvasionPercent = 0.0f;

	/**
	 * What fraction of its health is an energy shield. `energy_shield_fraction`
	 * is 0.35.
	 *
	 * **THE FIRST CREATURE BUILT THAT HAS ONE.** The Brute, the Abyssal Warden,
	 * the Hellhound and the Imp are all 0.0. An energy shield sits in front of
	 * health and is not reduced by armour, so it changes what killing this
	 * creature costs rather than only how much: damage over time and area damage
	 * go through armour differently from a direct hit, and
	 * `UCataclysmDamageCalculation::Resolve` takes the shield before health in
	 * that order.
	 */
	static constexpr float DesignedEnergyShieldFraction = 0.35f;

	/**
	 * How fast it moves. `move_speed` is 0.0, and so is `chase_speed`.
	 *
	 * **ZERO, AND IT IS THE ONLY ZERO IN THE ROSTER.** This is not a creature
	 * that happens to be slow. `ACataclysmEnemyCharacter` never sets
	 * MaxWalkSpeed, so an enemy that forgets to moves at Unreal's default 600 --
	 * which for this creature would turn a turret into a slow melee enemy
	 * without anything saying so, and would remove the one thing it is for.
	 *
	 * IT IS STILL ORDERED TO CHASE A PLAYER FURTHER AWAY THAN IT CAN SHOOT, and
	 * nothing comes of it. `ACataclysmEnemyController::Think` asks for an
	 * ability first, and beyond 14 metres neither is in range, so the ordinary
	 * chase runs and the movement component moves the pawn at zero. That is
	 * wasted work rather than a defect, and it is the honest outcome: the
	 * creature wants to close and cannot.
	 */
	static constexpr float DesignedWalkSpeedCmPerSecond = 0.0f;

	/** How fast it turns. `turn_rate_degrees` is the archetype default of 480.
	 *  **It can turn even though it cannot walk**, which is what lets a rooted
	 *  creature track a player who circles it. */
	static constexpr float DesignedTurnRateDegreesPerSecond = 480.0f;

	// ----------------------------------------------------------------------
	// Its body
	// ----------------------------------------------------------------------

	/**
	 * The capsule's radius, in centimetres.
	 *
	 * 48 IS THE ARCHETYPE DEFAULT AND IT IS NOT MEASURED. `body_radius` is 0.48
	 * for this creature, which is what `enemy_stats.py` gives anything nobody has
	 * measured, and issue #366 is that gap: only the Imp has a measured radius.
	 * The mesh is 155.9 cm across in its reference pose, which overstates the
	 * body because the arms are out, so this figure should be expected to move.
	 */
	static constexpr float SentinelCapsuleRadius = 48.0f;

	/**
	 * Half the capsule's height, in centimetres.
	 *
	 * FROM THE MESH. `tools/probe_sentinel_animation.py` read
	 * `Minion_Lane_Siege_Dawn` at 196.2 cm tall on 2026-08-20, so half of it is
	 * 98.1. The mesh is dropped by exactly this in `ResolveBody`.
	 */
	static constexpr float SentinelCapsuleHalfHeight = 98.1f;

	/**
	 * How far away it notices somebody, in centimetres.
	 *
	 * **AS FAR AS IT CAN SHOOT, WHICH NO OTHER CREATURE NEEDS.** The Brute, the
	 * Abyssal Warden, the Hellhound and the Imp all notice at 1000 cm and all of
	 * them can walk, so a target further off is one they close on. This creature
	 * cannot close on anything: a notice radius shorter than its range would
	 * make the difference between the two dead ground it can shoot across and
	 * refuses to. No enemy has a designed notice radius and issue #383 is that
	 * gap; this one is derived from the creature rather than copied.
	 */
	static constexpr float SentinelNoticeRadiusCm = 1400.0f;

	/** How far from where it started it wanders. **Zero, because it cannot
	 *  move.** Every other creature roams 600 cm. */
	static constexpr float SentinelRoamRadiusCm = 0.0f;

	// ----------------------------------------------------------------------
	// Siege Bolt, the telegraphed basic attack
	//
	// EVERY NUMBER HERE IS A COPY of `ABILITIES['Corrupted Sentinel']`.
	// ----------------------------------------------------------------------

	/** How far it shoots, in centimetres. `Range` is 14 metres, the longest
	 *  reach in the slice and the same range the mortar has. */
	static constexpr float SiegeBoltRangeCm = 1400.0f;

	/** Half the lane's width. `Radius` is 2.1 metres. */
	static constexpr float SiegeBoltRadiusCm = 210.0f;

	/** How fast the bolt travels. `Speed` is 1400 cm/s, so it crosses its whole
	 *  range in exactly one second. */
	static constexpr float SiegeBoltSpeedCmPerSecond = 1400.0f;

	/** Seconds before it may be used again. **Zero, which is what makes it the
	 *  basic attack.** The creature's 2.0 second attack interval is the only
	 *  thing spacing it out, which is what an interval means. */
	static constexpr float SiegeBoltCooldownSeconds = 0.0f;

	/**
	 * How long the lane is marked before the bolt leaves.
	 *
	 * DERIVED FROM THE RADIUS. The rule is `0.4 + Radius / 3.5` seconds, from
	 * the Attack Telegraphs subsection of `docs/Cataclysm_GDD_v2.md`. 0.4 + 2.1
	 * / 3.5 is exactly 1.0.
	 *
	 * **AND 2.1 METRES IS EXACTLY THE LARGEST THE RULE ALLOWS IT.** The cap is
	 * `3.5 x (attack interval / 2 - 0.4)`, which at 2.0 seconds is 2.1, so this
	 * creature's basic attack uses the whole of its allowance and its wind-up is
	 * exactly half its interval. The Succubus's basic does the same thing.
	 */
	static constexpr float SiegeBoltWindUpSeconds = 1.0f;

	static_assert(
		SiegeBoltWindUpSeconds > 0.4f + SiegeBoltRadiusCm / 100.0f / 3.5f - 0.002f
		&& SiegeBoltWindUpSeconds < 0.4f + SiegeBoltRadiusCm / 100.0f / 3.5f + 0.002f,
		"Siege Bolt's wind-up has drifted from the radius it is derived from. "
		"The rule is 0.4 + Radius / 3.5 seconds, from the Attack Telegraphs "
		"subsection of docs/Cataclysm_GDD_v2.md. Change both or neither.");

	static_assert(
		SiegeBoltWindUpSeconds < DesignedAttackIntervalSeconds,
		"Siege Bolt's telegraph is at least as long as the interval between "
		"shots, so the creature would begin warning about the next shot before "
		"this one had landed and the marker would never leave the ground.");

	/** What one bolt is worth, as a percentage of an ordinary hit. The Basic row
	 *  of `game/Data/SkillSlots.csv` is 100%, and the design's note on that row
	 *  is why: a basic attack IS weapon damage. */
	static constexpr float SiegeBoltDamagePercent = 100.0f;

	/** How many things it passes through. `Pierce` is 0, so it stops at the
	 *  first thing it hits. */
	static constexpr int32 SiegeBoltPierce = 0;

	// ----------------------------------------------------------------------
	// Brimstone Mortar, the lobbed shell
	// ----------------------------------------------------------------------

	/** How far it lobs, in centimetres. `Range` is 14 metres, the same as the
	 *  bolt's. */
	static constexpr float BrimstoneMortarRangeCm = 1400.0f;

	/** How wide it bursts. `Radius` is 3.0 metres, so the circle it marks is 6
	 *  metres across -- nearly the Abyssal Warden's 6.5 metre ring. */
	static constexpr float BrimstoneMortarRadiusCm = 300.0f;

	/**
	 * The shortest distance worth lobbing at, in centimetres.
	 *
	 * DERIVED, NOT CHOSEN, and it is the Brute's rule applied to a second
	 * creature. Below `marker radius + own body radius` the circle the shell
	 * marks covers the ground the creature is standing on, which is a melee
	 * attack wearing a thrown attack's telegraph. Issue #475 is where that was
	 * found, on the Brute, and the project owner reported it as the creature
	 * throwing rocks at point blank.
	 *
	 * WRITTEN OUT AS A NUMBER AND HELD TO THE SUM BY THE ASSERT BELOW, which is
	 * the arrangement `ACataclysmBruteCharacter::RockThrowMinimumRangeCm` uses
	 * and for the same reason: `tools/tests/test_sentinel_matches_the_model.py`
	 * reads these constants out of the source text, and it cannot evaluate an
	 * expression. A figure spelled as a sum is one the Python guard cannot see.
	 */
	static constexpr float BrimstoneMortarMinimumRangeCm = 348.0f;

	static_assert(
		BrimstoneMortarMinimumRangeCm
			== BrimstoneMortarRadiusCm + SentinelCapsuleRadius,
		"Brimstone Mortar's minimum range has drifted from the marked radius "
		"plus the creature's body radius that it is supposed to be. Below that "
		"sum the circle the shell marks covers the ground the creature is "
		"standing on. Issue #475 records it happening on the Brute.");

	/** Seconds before it may be used again. `cooldown` is 8.0. */
	static constexpr float BrimstoneMortarCooldownSeconds = 8.0f;

	/**
	 * How long the landing circle is marked before the shell arrives.
	 *
	 * The same `0.4 + Radius / 3.5` rule. 0.4 + 3.0 / 3.5 is 1.2571.
	 *
	 * **ITS RADIUS IS LARGER THAN THE BASIC ATTACK'S ALLOWANCE, AND THAT IS
	 * LEGAL.** A marker's radius is bounded by half the CYCLE the ability runs
	 * on, not by the creature's attack interval. Siege Bolt runs on the 2.0
	 * second interval, so its allowance is 2.1 metres and it uses all of it;
	 * this one runs on its own 8 second cooldown, so its allowance is far larger
	 * and 3.0 metres is well inside it. Both are also under
	 * `telegraph_cap_metres`, which is 6.50 metres for every creature with a
	 * 0.48 m body. `fits_its_cycle` in `enemy_abilities.py` checks both
	 * conditions and both abilities pass, confirmed 2026-08-20.
	 */
	static constexpr float BrimstoneMortarWindUpSeconds = 1.2571f;

	static_assert(
		BrimstoneMortarWindUpSeconds
			> 0.4f + BrimstoneMortarRadiusCm / 100.0f / 3.5f - 0.002f
		&& BrimstoneMortarWindUpSeconds
			< 0.4f + BrimstoneMortarRadiusCm / 100.0f / 3.5f + 0.002f,
		"Brimstone Mortar's wind-up has drifted from the radius it is derived "
		"from. The rule is 0.4 + Radius / 3.5 seconds.");

	/** What one shell is worth, as a percentage of an ordinary hit. The Special
	 *  row of `game/Data/SkillSlots.csv` is 150%. */
	static constexpr float BrimstoneMortarDamagePercent = 150.0f;

	/**
	 * How far the shell sags below its own chord, as a fraction of the distance
	 * lobbed. `Arc` is 0.25, exactly the Brute's.
	 *
	 * A FRACTION RATHER THAN A TIME, for the reason issue #474 records on the
	 * Brute: a stated flight time fixes the whole vertical part of the
	 * trajectory whatever the distance, which makes every short lob a
	 * near-vertical mortar. A parabola sags `g * t * t / 8` below its chord, so a
	 * sag of `Arc x range` is in the air for `sqrt(8 x Arc x range / g)`.
	 */
	static constexpr float BrimstoneMortarApexFraction = 0.25f;

	/** How many things the shell passes through. `Pierce` is 0. */
	static constexpr int32 BrimstoneMortarPierce = 0;

	/** The figures really in use, which are the console overrides when they are
	 *  set and the designed ones otherwise. */
	static float AttackIntervalSecondsInUse();
	static float BrimstoneMortarCooldownSecondsInUse();

	/** How long a shell lobbed to `LandsAt` spends in the air. Zero when it
	 *  would go nowhere. */
	float BrimstoneMortarFlightSecondsFor(const FVector& LandsAt) const;

	// ----------------------------------------------------------------------
	// Where its art lives
	// ----------------------------------------------------------------------

	static const TCHAR* BodyMeshPath;
	static const TCHAR* AnimationFolder;

	/** The rooted idle, which is a SINGLE POSE 0.03 seconds long rather than a
	 *  loop. That is what makes a rooted state cheap to hold: a telegraph needs
	 *  a wind-up that can be held open for a variable time, and one pose does
	 *  that with no seam. Rampage's `Ability_GroundSmash_Loop` exists for the
	 *  same reason and is the same 0.03 seconds. */
	static const TCHAR* IdleAnimationName;

	/** The two rooted firing clips, both 2.40 seconds. **They are alternated**,
	 *  which is what stops continuous fire reading as one clip looping --
	 *  `game/docs/enemy-source-assets.md` said so before this creature was
	 *  built. */
	static constexpr int32 FireAnimationCount = 2;
	static const TCHAR* FireAnimationNames[FireAnimationCount];

	/** **EIGHT DEATHS, WHICH IS THE MOST IN THE PROJECT.** The Brute ships one,
	 *  the Abyssal Warden and the Hellhound two, the Imp five. Measured 0.43 to
	 *  0.77 seconds, all inside `UCataclysmEnemyDeath::LongestCorpseSeconds`. */
	static constexpr int32 DeathAnimationCount = 8;
	static const TCHAR* DeathAnimationNames[DeathAnimationCount];

	/**
	 * How long each rooted firing clip runs, in seconds.
	 *
	 * MEASURED, NOT CHOSEN. `tools/probe_sentinel_animation.py` read 2.4000 from
	 * both on 2026-08-20, which matched what was recorded on 2026-08-07.
	 *
	 * **IT IS LONGER THAN THE INTERVAL, AND THIS CREATURE IS THE ONLY ONE WHOSE
	 * ATTACK CLIP IS.** Issue #369 settled what to do: the clip is played to fit
	 * the interval, so 2.40 into 2.00 is a play rate of 1.20, which is the
	 * gentlest rate scaling in the project. The alternative clips the pack ships
	 * -- `Fire_A`, `Fire_B` and `Fire_C` -- are 2.80 and are unrooted, so there
	 * is nothing shorter to reach for.
	 */
	static constexpr float FireAnimationSeconds = 2.4f;

	/** Play rate floor and ceiling, the same two figures every other creature
	 *  clamps to. This creature's firing clip needs 1.20. */
	static constexpr float MinimumPlayRate = 0.2f;
	static constexpr float MaximumPlayRate = 2.5f;

	static_assert(
		FireAnimationSeconds / DesignedAttackIntervalSeconds <= MaximumPlayRate,
		"The Corrupted Sentinel's firing clip no longer fits inside its attack "
		"interval even at the play rate ceiling, so one shot would still be "
		"playing when the next began. See tools/probe_sentinel_animation.py and "
		"sim/cataclysm_sim/enemy_stats.py.");

	/** The play rate the firing clip needs to fit the interval. */
	static float FirePlayRate();

	// ----------------------------------------------------------------------
	// What it is wearing and playing
	// ----------------------------------------------------------------------

	/**
	 * Puts the real mesh and its animations on, if the art pack is installed.
	 *
	 * PUBLIC SO A TEST CAN CALL IT, the same reason every other creature's is.
	 *
	 * @return whether the mesh was found and worn
	 */
	bool ResolveBody(bool bIncludeAnimation = true);

	/** Puts the rooted idle back on once a firing clip has finished. PUBLIC SO A
	 *  TEST CAN DRIVE IT WITHOUT TICKING A WORLD. */
	void UpdateLoopingAnimation();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> IdleAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TArray<TObjectPtr<class UAnimSequence>> FireAnimations;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> CurrentLoopingAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> LastPlayedAnimation;

	/** When the clip a one-shot started will finish, in world seconds. Zero
	 *  means nothing is playing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float OneShotEndsAtSeconds = 0.0f;

	/**
	 * Which of the two firing clips the next shot plays.
	 *
	 * COUNTED RATHER THAN DRAWN, unlike the Imp's five claw swipes. Two clips
	 * drawn at random repeat about half the time, which is exactly the "one clip
	 * looping" the pack's two firing clips exist to avoid; alternating them
	 * cannot. The Imp draws because ten of it fire at once and a shared counter
	 * would put a whole pack in step.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	int32 NextFireAnimation = 0;

	/** Whether an animation Blueprint took the mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bAnimationBlueprintBound = false;

	/** The last thing either ability fired. Read by tests, which have nothing
	 *  else to look at. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class ACataclysmProjectile> LastShotFired;

private:
	/** Plays one clip once and records it. Returns how long it will take. */
	float PlayOneShot(class UAnimSequence* Animation, float HoldSeconds = 0.0f);

	void PlayFireAnimation();
};
