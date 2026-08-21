// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "CataclysmGatekeeperCharacter.generated.h"

/**
 * The Gatekeeper: the Demonic boss, and the only creature in the game with
 * phases.
 *
 * WHAT IT DOES, from `ABILITIES['Gatekeeper']` in
 * `sim/cataclysm_sim/enemy_abilities.py`:
 *
 *   Dread Cleave      Strike      Basic      1  a 120 degree sweep, 2 m out
 *   Soulfall          Projectile  Special    1  a lobbed gout that burns ground
 *   Call the Damned   Summon      Special    2  three Imps out of the ground
 *   Soul Harvest      Strike      Ultimate   3  a 6.5 m ring at its feet
 *
 * **IT IS THE LARGEST THING IN THE GAME BY EVERY MEASURE.** `health_share` 5.00
 * against the Abyssal Warden's 3.50, `armor_share` 2.50, the slowest attack
 * interval at 3.0 seconds, the highest critical multiplier at 250%, and a mesh
 * 3.11 metres tall -- which is the design document's "towering" as a
 * measurement rather than an adjective.
 *
 * **THREE PHASES, AND A PHASE CHANGES NO NUMBER.** `PHASE_TRANSITIONS` is
 * `(0.60, 0.30)`: the fight opens in phase 1, phase 2 begins at 60% health and
 * phase 3 at 30%. A phase selects which abilities are in the rotation and
 * nothing else -- no damage, speed or critical strike changes at a transition.
 * The research behind that is recorded with issue #354 in `docs/DECISIONS.md`:
 * across ten shipped bosses in Path of Exile and Last Epoch, not one gains a
 * statistic at a transition.
 *
 * The machinery is `ACataclysmEnemyCharacter::PhaseHealthFractions` and
 * `RefreshPhase`, built in its own change so that a subsystem did not arrive in
 * the middle of a creature. This class sets two numbers and each ability names
 * the phase it arrives in.
 *
 * **ITS BASIC ATTACK IS TELEGRAPHED**, which makes it the third creature like
 * that after the Corrupted Sentinel and the Succubus. Dread Cleave is an entry
 * in `EnemyAbilities` with a cooldown of ZERO, so the creature's own 3.0 second
 * attack interval is what spaces it out.
 * `ACataclysmEnemyController::UseAbilitiesOn` gates every ability by that
 * interval as well as by its own cooldown. `AttackTarget` is overridden here to
 * do nothing, or the creature would deal a free melee hit at two metres every
 * three seconds on top of the sweep it already made.
 *
 * WHY DREAD CLEAVE IS LISTED LAST. `ChooseAbility` takes the first entry whose
 * phase, range and cooldown fit and never looks at the shape. A zero-cooldown
 * ability at the front of the array is the only thing the creature would ever
 * do. That is issue #491 on the Abyssal Warden.
 *
 * **NOTHING IT LEAVES BURNS ITS OWN SIDE.** Soulfall's ground was designed to
 * burn the Gatekeeper's own summoned Imps, and on 2026-08-20 the project owner
 * set a general rule that a creature does not burn itself or its allies.
 * `docs/DECISIONS.md` records what that cost: phase 2's summons no longer have
 * a cost attached, and if phase 2 turns out too strong the first levers are the
 * summon's cap of six and its 10 second cooldown.
 */
UCLASS()
class CATACLYSM_API ACataclysmGatekeeperCharacter : public ACataclysmEnemyCharacter
{
	GENERATED_BODY()

public:
	ACataclysmGatekeeperCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	virtual void AttackTarget(AActor* Target) override;
	virtual float DesignedSecondsBetweenAttacks() const override;

	virtual TArray<FCataclysmEnemyAbility> EnemyAbilities() const override;
	virtual void UseEnemyAbility(int32 Index, AActor* Target,
								 const FVector& AimedAt) override;
	virtual void BeginEnemyAbilityWindUp(int32 Index, AActor* Target) override;

	/**
	 * Which entry of EnemyAbilities is which.
	 *
	 * THE ORDER IS THE PRIORITY AND IT IS NOT THE DESIGN'S ORDER. The model
	 * lists Dread Cleave first because it is the Basic; this array lists it LAST
	 * because a zero-cooldown ability at the front crowds out everything behind
	 * it.
	 *
	 * AND THE REST ARE IN DESCENDING ORDER OF WEIGHT, so the creature reaches
	 * for the biggest thing it may use. Soul Harvest is the Ultimate and arrives
	 * in phase 3; Call the Damned arrives in phase 2; Soulfall is available from
	 * the start and answers a player who stands off.
	 */
	enum : int32
	{
		SoulHarvestAbility = 0,
		CallTheDamnedAbility = 1,
		SoulfallAbility = 2,
		DreadCleaveAbility = 3,
	};

	virtual float AttackReachCm() const override { return DreadCleaveRadiusCm; }
	virtual float SightRadiusCm() const override { return GatekeeperNoticeRadiusCm; }
	virtual float RoamRadiusCm() const override { return GatekeeperRoamRadiusCm; }

	// ----------------------------------------------------------------------
	// The designed stat block
	//
	// EVERY ONE OF THESE IS A COPY, and `sim/cataclysm_sim/enemy_stats.py` is
	// the original. `tools/tests/test_gatekeeper_matches_the_model.py` holds
	// each of them to that file. Continuous integration never builds this file,
	// so that Python test is the only thing checking these on a pull request.
	// ----------------------------------------------------------------------

	/** Seconds between attacks. `attack_interval` is 3.0, **the slowest in the
	 *  roster**. It is what makes a 2 metre telegraphed sweep legal: the rules
	 *  size a marker from half the cycle it runs on. */
	static constexpr float DesignedAttackIntervalSeconds = 3.0f;

	/** Percent of all incoming damage resisted. `resistance` is 30.0, **the most
	 *  of anything designed**, against the Corrupted Sentinel's 20. */
	static constexpr float DesignedResistancePercent = 30.0f;

	/** `crit_chance` 15.0 and `crit_multiplier` 250.0. **The highest multiplier
	 *  in the slice**, against the Brute's and the Succubus's 200. */
	static constexpr float DesignedCritChancePercent = 15.0f;
	static constexpr float DesignedCritMultiplierPercent = 250.0f;

	/** `evasion` is 0.0. **It does not dodge. It absorbs.** Written out rather
	 *  than left to the base's default so the zero is visibly designed. */
	static constexpr float DesignedEvasionPercent = 0.0f;

	/** `energy_shield_fraction` is 0.0. The Succubus and the Corrupted Sentinel
	 *  are the only two creatures that carry one; this one has armour and
	 *  resistance instead. */
	static constexpr float DesignedEnergyShieldFraction = 0.0f;

	/**
	 * How fast it walks, in centimetres per second. `move_speed` is 3.0 m/s.
	 *
	 * **SLOWER THAN EVERY PLAYER CLASS**, which run 3.5, 4.0 and 4.6 metres per
	 * second, and it has no separate chase speed. So it can never catch anybody,
	 * which is the same position the Abyssal Warden is in. Its answer is not the
	 * Warden's charge -- that would make it a bigger Warden -- but Soulfall,
	 * which makes standing off more dangerous than closing.
	 */
	static constexpr float DesignedWalkSpeedCmPerSecond = 300.0f;

	/** How fast it turns. `turn_rate_degrees` is the archetype default of 480. */
	static constexpr float DesignedTurnRateDegreesPerSecond = 480.0f;

	// ----------------------------------------------------------------------
	// Its phases
	// ----------------------------------------------------------------------

	/**
	 * The health fractions at which phases 2 and 3 begin.
	 *
	 * `PHASE_TRANSITIONS["Gatekeeper"]` is `(0.60, 0.30)`. Two transitions make
	 * three phases.
	 *
	 * **THE FIGURES ARE A JUDGEMENT AND THE SHAPE IS NOT.** A long opening band
	 * that teaches the base kit, then two shorter ones that each add exactly one
	 * thing, is what the research found; the exact percentages are not published
	 * by any shipped game, so 60 and 30 were chosen. `docs/DECISIONS.md` labels
	 * them as a judgement.
	 */
	static constexpr float SecondPhaseHealthFraction = 0.60f;
	static constexpr float ThirdPhaseHealthFraction = 0.30f;

	static_assert(
		ThirdPhaseHealthFraction < SecondPhaseHealthFraction,
		"The Gatekeeper's phase thresholds are out of order, so the third phase "
		"would begin before the second. ACataclysmEnemyCharacter::RefreshPhase "
		"counts the thresholds at or below the creature's health fraction, so "
		"they must be listed highest first.");

	// ----------------------------------------------------------------------
	// Its body
	// ----------------------------------------------------------------------

	/**
	 * The capsule's radius, in centimetres.
	 *
	 * **48 IS THE ARCHETYPE DEFAULT AND IT IS NOT MEASURED, AND IT MATTERS MORE
	 * HERE THAN ANYWHERE.** `body_radius` is 0.48 for this creature, which is
	 * what `enemy_stats.py` gives anything nobody has measured -- and the mesh
	 * is 2.07 metres across in its reference pose and 3.11 metres tall. A 48 cm
	 * capsule on a three metre creature is a creature the player can stand
	 * inside. Issue #366 is that gap and this is the strongest case in it.
	 *
	 * IT IS STILL USED, because the design says 0.48 and a measured figure would
	 * be inventing design. Dread Cleave reaches 200 cm, so the reach still
	 * clears the radius by a wide margin and nothing here is broken by it.
	 */
	static constexpr float GatekeeperCapsuleRadius = 48.0f;

	/**
	 * Half the capsule's height, in centimetres.
	 *
	 * FROM THE MESH, and **it is by far the tallest in the project**.
	 * `tools/probe_gatekeeper_animation.py` read `Sevarog` at 311.1 cm tall on
	 * 2026-08-20, so half of it is 155.53. That agrees with the figure
	 * `game/docs/enemy-source-assets.md` recorded on 2026-08-07. The Corrupted
	 * Sentinel, the next tallest, is 98.1.
	 */
	static constexpr float GatekeeperCapsuleHalfHeight = 155.53f;

	/**
	 * How far away it notices somebody, in centimetres.
	 *
	 * AS FAR AS SOULFALL REACHES, which is the Corrupted Sentinel's rule rather
	 * than the walking creatures'. This one moves at 3.0 metres per second and
	 * has no chase speed, so it can never close a gap on a player who does not
	 * want it closed; a notice radius shorter than its longest attack would make
	 * the difference ground it can hit and refuses to. The four creatures that
	 * can actually catch somebody all notice at 1000. No enemy has a designed
	 * notice radius and issue #383 is that gap.
	 */
	static constexpr float GatekeeperNoticeRadiusCm = 1400.0f;

	/** How far from where it started it wanders with nothing in sight. The same
	 *  600 every creature that can walk uses. */
	static constexpr float GatekeeperRoamRadiusCm = 600.0f;

	// ----------------------------------------------------------------------
	// Dread Cleave, the telegraphed basic attack
	// ----------------------------------------------------------------------

	/** How far the sweep reaches, in centimetres. `Radius` is 2 metres. */
	static constexpr float DreadCleaveRadiusCm = 200.0f;

	/** How wide the arc is, in degrees. `Angle` is 120, so it is a cone and
	 *  **standing behind the creature is an answer to it** -- unlike the Brute's
	 *  stomp and this creature's own Soul Harvest, which are full circles. */
	static constexpr float DreadCleaveAngleDegrees = 120.0f;

	/** Seconds before it may be used again. **Zero, which is what makes it the
	 *  basic attack.** The creature's 3.0 second attack interval is the only
	 *  thing spacing it out. */
	static constexpr float DreadCleaveCooldownSeconds = 0.0f;

	/**
	 * How long the arc is marked before the hammer lands.
	 *
	 * DERIVED FROM THE RADIUS. The rule is `0.4 + Radius / 3.5` seconds, from
	 * the Attack Telegraphs subsection of `docs/Cataclysm_GDD_v2.md`. 0.4 + 2.0
	 * / 3.5 is 0.9714.
	 *
	 * **IT IS THE ONLY BASIC ATTACK IN THE SLICE THAT IS TELEGRAPHED BECAUSE IT
	 * HAS TO BE**, rather than because it can be. Two of these kill the
	 * reference geared character, which is what makes the warning necessary
	 * rather than decorative.
	 */
	static constexpr float DreadCleaveWindUpSeconds = 0.9714f;

	static_assert(
		DreadCleaveWindUpSeconds
			> 0.4f + DreadCleaveRadiusCm / 100.0f / 3.5f - 0.002f
		&& DreadCleaveWindUpSeconds
			< 0.4f + DreadCleaveRadiusCm / 100.0f / 3.5f + 0.002f,
		"Dread Cleave's wind-up has drifted from the radius it is derived from. "
		"The rule is 0.4 + Radius / 3.5 seconds, from the Attack Telegraphs "
		"subsection of docs/Cataclysm_GDD_v2.md. Change both or neither.");

	static_assert(
		DreadCleaveWindUpSeconds < DesignedAttackIntervalSeconds / 2.0f,
		"Dread Cleave's telegraph is longer than half the interval between "
		"swings, which is the rule that keeps a marker walkable.");

	/** What one sweep is worth. The Basic row of `game/Data/SkillSlots.csv` is
	 *  100%: a basic attack IS weapon damage. */
	static constexpr float DreadCleaveDamagePercent = 100.0f;

	// ----------------------------------------------------------------------
	// Soulfall, the lobbed gout
	// ----------------------------------------------------------------------

	/** How far it lobs, in centimetres. `Range` is 14 metres, the same as the
	 *  Corrupted Sentinel's mortar and reused from it rather than invented. */
	static constexpr float SoulfallRangeCm = 1400.0f;

	/** How wide it bursts. `Radius` is 3.0 metres. */
	static constexpr float SoulfallRadiusCm = 300.0f;

	/**
	 * The shortest distance worth lobbing at, in centimetres.
	 *
	 * DERIVED, NOT CHOSEN, and it is the rule the Brute and the Corrupted
	 * Sentinel already follow. Below `marker radius + own body radius` the
	 * circle the gout marks covers the ground the creature is standing on,
	 * which is a melee attack wearing a thrown attack's telegraph. Issue #475
	 * records it happening on the Brute.
	 *
	 * WRITTEN OUT AS A NUMBER AND HELD TO THE SUM BY THE ASSERT BELOW, because
	 * `tools/tests/test_gatekeeper_matches_the_model.py` reads these constants
	 * out of the source text and cannot evaluate an expression.
	 */
	static constexpr float SoulfallMinimumRangeCm = 348.0f;

	static_assert(
		SoulfallMinimumRangeCm == SoulfallRadiusCm + GatekeeperCapsuleRadius,
		"Soulfall's minimum range has drifted from the marked radius plus the "
		"creature's body radius that it is supposed to be. Below that sum the "
		"circle the gout marks covers the ground the creature is standing on.");

	/** Seconds before it may be lobbed again. `cooldown` is 10.0, the top of the
	 *  Special slot's band, **because the burning ground it leaves must not
	 *  accumulate faster than one patch per expiry**. */
	static constexpr float SoulfallCooldownSeconds = 10.0f;

	/**
	 * How long the landing circle is marked before the gout arrives.
	 *
	 * The same `0.4 + Radius / 3.5` rule. 0.4 + 3.0 / 3.5 is 1.2571, and it is
	 * the same figure the Corrupted Sentinel's mortar carries for the same
	 * radius.
	 */
	static constexpr float SoulfallWindUpSeconds = 1.2571f;

	static_assert(
		SoulfallWindUpSeconds > 0.4f + SoulfallRadiusCm / 100.0f / 3.5f - 0.002f
		&& SoulfallWindUpSeconds < 0.4f + SoulfallRadiusCm / 100.0f / 3.5f + 0.002f,
		"Soulfall's wind-up has drifted from the radius it is derived from. "
		"The rule is 0.4 + Radius / 3.5 seconds.");

	/** What one gout is worth. The Special row of `game/Data/SkillSlots.csv` is
	 *  150%. */
	static constexpr float SoulfallDamagePercent = 150.0f;

	/** How many things the gout passes through. `Pierce` is 0. */
	static constexpr int32 SoulfallPierce = 0;

	/**
	 * How far the gout sags below its own chord, as a fraction of the distance
	 * lobbed. `Arc` is 0.25, the same as the Brute's rock and the Sentinel's
	 * mortar.
	 */
	static constexpr float SoulfallApexFraction = 0.25f;

	// --- what it leaves behind ---------------------------------------------

	/** How wide the burning ground is. `GroundRadius` is 3.0 metres, the same as
	 *  the burst, because it is the ground the burst covered. */
	static constexpr float SoulfallGroundRadiusCm = 300.0f;

	/**
	 * How long the burning ground lasts, in seconds. `GroundDuration` is 10.
	 *
	 * **EQUAL TO THE COOLDOWN, WHICH IS THE WHOLE DESIGN OF THE ABILITY.** In
	 * steady state one patch is always on the floor and the arena shrinks by
	 * exactly one circle per cycle until the old one expires. That is the
	 * persistence the genre research found real bosses use: the arena changing,
	 * not being replaced.
	 */
	static constexpr float SoulfallGroundSeconds = 10.0f;

	static_assert(
		SoulfallGroundSeconds == SoulfallCooldownSeconds,
		"Soulfall's burning ground no longer lasts exactly its own cooldown. "
		"Shorter and the arena stops shrinking; longer and the patches "
		"accumulate faster than they expire. Both are the design's own words.");

	/**
	 * What one second in the burning ground costs, as a percentage of an
	 * ordinary hit. `GroundPercent` is 10.
	 *
	 * THE PROJECT'S RULE IS `100 / GroundDuration`, so standing in a patch for
	 * its whole life costs exactly one hit. The Hellhound's 25 over 4 seconds is
	 * the same arithmetic.
	 */
	static constexpr float SoulfallGroundPercent = 10.0f;

	static_assert(
		SoulfallGroundPercent * SoulfallGroundSeconds > 99.0f
		&& SoulfallGroundPercent * SoulfallGroundSeconds < 101.0f,
		"Soulfall's burning ground no longer costs one whole hit over its life. "
		"The rule is 100 / GroundDuration percent per second.");

	// ----------------------------------------------------------------------
	// Call the Damned, the summon
	// ----------------------------------------------------------------------

	/** How far from itself the Imps claw out, in centimetres. `Range` is 4
	 *  metres. */
	static constexpr float CallTheDamnedRangeCm = 400.0f;

	/** How far the ground opens, in centimetres. `Radius` is 2 metres. */
	static constexpr float CallTheDamnedRadiusCm = 200.0f;

	/** How many arrive per cast. `Count` is 3. */
	static constexpr int32 CallTheDamnedCount = 3;

	/**
	 * How many of its Imps may be alive at once. `MaxActive` is 6.
	 *
	 * **THE CAP IS WHAT MAKES KILLING THEM WORTH IT.** Dead Imps are only
	 * replaced on the next cast, ten seconds later, so clearing them buys the
	 * player that long. Without a cap the answer would be to ignore them.
	 */
	static constexpr int32 CallTheDamnedMaxAlive = 6;

	/** Seconds before it may be cast again. `cooldown` is 10.0. */
	static constexpr float CallTheDamnedCooldownSeconds = 10.0f;

	/**
	 * Seconds it stands committed before the Imps arrive. **Zero, and that is
	 * designed rather than unfinished.**
	 *
	 * A SUMMON DRAWS NO MARKER. `TELEGRAPHED_SHAPES` in
	 * `sim/cataclysm_sim/enemy_abilities.py` covers Strike, Projectile, Aura and
	 * Movement, and a Summon is not one of them: there is no ground for it to be
	 * drawn on, because the answer to adds is killing them rather than standing
	 * somewhere else.
	 */
	static constexpr float CallTheDamnedWindUpSeconds = 0.0f;

	/** The phase it arrives in. `phase` is 2. */
	static constexpr int32 CallTheDamnedPhase = 2;

	// ----------------------------------------------------------------------
	// Soul Harvest, the ring
	// ----------------------------------------------------------------------

	/**
	 * How far the ring reaches, in centimetres. `Radius` is 6.5 metres.
	 *
	 * **IT IS THE CAP: the largest marker the telegraph rules permit for any
	 * creature with a 0.48 m body.** Identical to the Abyssal Warden's Molten
	 * Roar and for the same reason -- a boss finale should be the hardest legal
	 * telegraph, and the cap is what hardest-legal means.
	 */
	static constexpr float SoulHarvestRadiusCm = 650.0f;

	/** How wide the arc is, in degrees. `Angle` is 360, so there is no standing
	 *  behind it. */
	static constexpr float SoulHarvestAngleDegrees = 360.0f;

	/** Seconds before it may be used again. `cooldown` is 20.0, inside the
	 *  Ultimate slot's 12-to-40 band and above the Warden's 12 because this
	 *  creature kills in six seconds. */
	static constexpr float SoulHarvestCooldownSeconds = 20.0f;

	/**
	 * How long the ring is marked before the ground erupts. `Radius` 6.5 gives
	 * `0.4 + 6.5 / 3.5` = 2.257, which is over the ceiling, so it is held at
	 * **`MAXIMUM_WIND_UP_SECONDS`, which is 2.0**.
	 *
	 * THE CEILING IS WHAT MAKES A BIGGER RING HARDER. Below it the wind-up grows
	 * exactly as fast as the ground to cross, so the escape margin is the same
	 * at every radius; above it the warning stops growing while the ground keeps
	 * growing. `docs/DECISIONS.md` records that as the reason the ceiling exists.
	 */
	static constexpr float SoulHarvestWindUpSeconds = 2.0f;

	/**
	 * What the ring is worth. The Ultimate row of `game/Data/SkillSlots.csv` is
	 * 400%.
	 *
	 * **STANDING IN IT IS DEATH FROM FULL HEALTH.** Four of this creature's
	 * ordinary hits against a character who survives two. That is designed, not
	 * incidental: the genre's rule for a long-telegraph boss ultimate is that
	 * standing in it is death and the two second warning is the answer.
	 */
	static constexpr float SoulHarvestDamagePercent = 400.0f;

	/** The phase it arrives in. `phase` is 3. */
	static constexpr int32 SoulHarvestPhase = 3;

	/** The figures really in use, which are the console overrides when they are
	 *  set and the designed ones otherwise. */
	static float AttackIntervalSecondsInUse();
	static float SoulfallCooldownSecondsInUse();
	static float SoulHarvestCooldownSecondsInUse();

	/** How long a gout lobbed to `LandsAt` spends in the air. Zero when it would
	 *  go nowhere. */
	float SoulfallFlightSecondsFor(const FVector& LandsAt) const;

	// ----------------------------------------------------------------------
	// Where its art lives
	// ----------------------------------------------------------------------

	static const TCHAR* BodyMeshPath;
	static const TCHAR* AnimationFolder;

	/** Standing. `Idle`, measured at 8.9000 seconds. */
	static const TCHAR* IdleAnimationName;

	/**
	 * Walking. `Jog_Fwd`, 9.0000 seconds.
	 *
	 * **CHOSEN BECAUSE IT IS THE ONLY LOCOMOTION CLIP IN THE PACK WHOSE FEET
	 * MOVE AT ALL.** `tools/probe_gatekeeper_foot_bones.py` measured how far
	 * each leg bone travels across each clip on 2026-08-20: `Walk_Fwd` and
	 * `Run_Fwd` move no foot, calf or thigh bone by more than 0.03 cm across
	 * their whole 1.6 seconds, and carry 127 animated tracks where this one
	 * carries 155. They appear to be partial clips rather than complete ones.
	 */
	static const TCHAR* JogAnimationName;

	/** Dread Cleave. `Swing1_Medium`, 1.1333 seconds, played across the 0.9714
	 *  second wind-up at a rate of 1.1667. The pack ships three swing chains at
	 *  four speeds each; the medium one is the shortest that is longer than the
	 *  wind-up, so it fills it without being sped up hard. */
	static const TCHAR* CleaveAnimationName;

	/** Soulfall. `Soul_Siphon`, 1.8333 seconds, played across the 1.2571 second
	 *  wind-up at a rate of 1.4584. A different clip from the sweep, because a
	 *  lob and a hammer swing should not look the same. */
	static const TCHAR* SoulfallAnimationName;

	/** Call the Damned. `Subjugation`, 2.8667 seconds. **It has no wind-up to
	 *  fit**, so it plays at its authored speed across its own length. */
	static const TCHAR* CallAnimationName;

	/** Soul Harvest. `Ultimate_Swing_120fps`, 2.6333 seconds, played across the
	 *  2.0 second wind-up at a rate of 1.3167. The pack also ships
	 *  `Ultimate_Targeting` at 0.8667 and a hold loop at 2.2333, which is the
	 *  start-hold-release a long telegraph wants; using all three is issue
	 *  #779. */
	static const TCHAR* UltimateAnimationName;

	/** **ONE DEATH, WHICH IS THE FEWEST IN THE PROJECT** along with the Brute's
	 *  and the Succubus's. `Death_front`, measured at 0.9667 seconds, well
	 *  inside `UCataclysmEnemyDeath::LongestCorpseSeconds`. */
	static constexpr int32 DeathAnimationCount = 1;
	static const TCHAR* DeathAnimationNames[DeathAnimationCount];

	/** How long each clip runs, in seconds. All measured 2026-08-20 by
	 *  `tools/probe_gatekeeper_animation.py`, and every one confirmed the figure
	 *  recorded by hand on 2026-08-07. */
	static constexpr float CleaveAnimationSeconds = 1.1333f;
	static constexpr float SoulfallAnimationSeconds = 1.8333f;
	static constexpr float UltimateAnimationSeconds = 2.6333f;

	/** Play rate floor and ceiling, the same two figures every other creature
	 *  clamps to. */
	static constexpr float MinimumPlayRate = 0.2f;
	static constexpr float MaximumPlayRate = 2.5f;

	static_assert(
		CleaveAnimationSeconds <= DreadCleaveWindUpSeconds * MaximumPlayRate,
		"The Gatekeeper's sweep clip no longer fits inside Dread Cleave's "
		"wind-up even at the play rate ceiling, so the hammer would land while "
		"the creature was still winding up to swing it.");

	static_assert(
		UltimateAnimationSeconds <= SoulHarvestWindUpSeconds * MaximumPlayRate,
		"The Gatekeeper's ultimate clip no longer fits inside Soul Harvest's "
		"wind-up even at the play rate ceiling.");

	/**
	 * The play rate the walk needs so the feet do not slide.
	 *
	 * **IT IS 1.0 AND IT IS UNVERIFIED, WHICH IS DIFFERENT FROM DERIVED.** Every
	 * other creature's walk rate is `designed speed / authored speed`, with the
	 * authored speed measured by `tools/measure_animation_stride.py` and the
	 * idle checked as a control. **That tool cannot read this rig**: run on
	 * 2026-08-20 it reported 0.0 cm/s for all three of Sevarog's locomotion
	 * clips and 14.7 cm/s for `Idle`, which is the control and must read zero.
	 * Wrong in both directions, so none of its numbers may be used.
	 *
	 * Issue #778 carries the evidence and what to do. **Guessing a walk play
	 * rate is what produced visible foot sliding on the Brute**, which is why
	 * that tool exists, so this figure should be treated as a placeholder and
	 * judged by eye before anybody trusts it.
	 */
	static constexpr float JogPlayRate = 1.0f;

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

	/** Puts the idle or the walk back on once a one-shot has finished. PUBLIC SO
	 *  A TEST CAN DRIVE IT WITHOUT TICKING A WORLD. */
	void UpdateLoopingAnimation();

	/** Above this speed the creature is walking rather than standing. The same
	 *  figure every other creature here uses. */
	static constexpr float WalkingThresholdCmPerSecond = 10.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> IdleAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> JogAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> CleaveAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> SoulfallAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> CallAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> UltimateAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> CurrentLoopingAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> LastPlayedAnimation;

	/** When the clip a one-shot started will finish, in world seconds. Zero
	 *  means nothing is playing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float OneShotEndsAtSeconds = 0.0f;

	/** Whether an animation Blueprint took the mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bAnimationBlueprintBound = false;

	/** The last gout it lobbed. Read by tests, which have nothing else to look
	 *  at. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class ACataclysmProjectile> LastGoutLobbed;

	/** The last patch of burning ground it left. Read by tests. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class ACataclysmGroundZone> LastGroundLeftBurning;

	/**
	 * The Imps it has called up and not yet buried.
	 *
	 * WEAK, SO A DEAD IMP CAN BE COLLECTED. A hard reference would keep every
	 * Imp this creature ever summoned alive for as long as the boss was,
	 * including corpses already removed from the level -- and the cap counts
	 * what is alive, so it would fill up and stay full.
	 *
	 * AND THEREFORE NOT `BlueprintReadOnly`. Unreal Header Tool refuses
	 * `TArray<TWeakObjectPtr<...>>` as a Blueprint type outright.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Enemy")
	TArray<TWeakObjectPtr<class ACataclysmImpCharacter>> CalledImps;

	/** How many of its Imps are alive right now, after forgetting the dead.
	 *  PUBLIC SO A TEST CAN ASK. */
	int32 ImpsStillAlive();

private:
	/** Plays one clip once and records it. Returns how long it will take. */
	float PlayOneShot(class UAnimSequence* Animation, float HoldSeconds = 0.0f);

	/** Everything Dread Cleave and Soul Harvest have in common: sweep, hit. */
	void StrikeAround(float RadiusCm, float AngleDegrees, float DamagePercent);

	static const TCHAR* CleaveTags;
	static const TCHAR* SoulHarvestTags;
};
