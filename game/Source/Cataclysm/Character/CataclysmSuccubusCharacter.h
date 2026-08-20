// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "CataclysmSuccubusCharacter.generated.h"

/**
 * The Succubus: a ranged caster that makes every other creature near it
 * stronger.
 *
 * WHAT IT DOES, from `ABILITIES['Succubus']` in
 * `sim/cataclysm_sim/enemy_abilities.py`:
 *
 *   Soulfire            Projectile  Basic    a slow bolt down an 8 metre lane
 *   Wither the Living   Debuff      Support  a curse on the player, 5 seconds
 *   Dominion            Aura        Aura     allies within 8 metres are buffed
 *
 * **IT IS THE ONLY CREATURE IN THE SLICE THAT CHANGES HOW THE OTHERS FIGHT**,
 * and the design document says that is the whole lesson it teaches: target
 * priority. Everything about Dominion follows from it -- see the section on the
 * aura below for why it is held on rather than cast.
 *
 * TWO OF ITS THREE SHAPES ARE NEW TO ENEMIES, AND NEITHER NEEDED A NEW MARKER.
 * `ECataclysmSkillShape` has carried `Debuff` and `Aura` since issue #37 built
 * the player's skill templates, and no enemy had used either. Both are shapes
 * the design's telegraph table draws NOTHING for:
 *
 *   - A Debuff has no ground for a marker to be drawn on. `TELEGRAPHED_SHAPES`
 *     in the model lists Strike, Projectile, Aura and Movement, and the design
 *     document's own words are "there is no ground for a curse to be drawn on".
 *     The counter is interrupting the caster, which is what makes crowd control
 *     the answer to this creature.
 *   - An Aura held on for as long as the creature lives has no moment it lands,
 *     so `Ability.cycle_seconds` returns zero for it and `is_telegraphed` is
 *     false. Confirmed by running the model on 2026-08-20.
 *
 * So `ACataclysmEnemyController::ShowWindUpMarker` needed no new case: its
 * default arm already draws nothing for both, and says so.
 *
 * **ITS BASIC ATTACK IS AN ENTRY IN `EnemyAbilities`**, the same shape the
 * Corrupted Sentinel established. Soulfire is TELEGRAPHED, and the ordinary
 * `AttackTarget` path has no wind-up and draws no marker. An ability with a
 * cooldown of ZERO fires exactly on the creature's attack interval, because
 * `ACataclysmEnemyController::UseAbilitiesOn` gates every ability by
 * `SecondsBetweenAttacks()` as well as by the ability's own cooldown, and stamps
 * that interval when a wind-up lands. `AttackTarget` is overridden here to do
 * nothing at all, or the creature would deal a free melee hit at eight metres
 * every 2.6 seconds on top of the bolt it already fired.
 *
 * WHY WITHER THE LIVING IS LISTED FIRST. `ChooseAbility` takes the first entry
 * whose range and cooldown fit and never looks at the shape. Soulfire has a
 * cooldown of zero and the same 8 metre range, so listing it first would make it
 * the only thing this creature ever does and the curse would never be cast.
 * That is issue #491 on the Abyssal Warden, which is the same defect with the
 * numbers changed.
 */
UCLASS()
class CATACLYSM_API ACataclysmSuccubusCharacter : public ACataclysmEnemyCharacter
{
	GENERATED_BODY()

public:
	ACataclysmSuccubusCharacter();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void HandleDeath() override;

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
	 * THE ORDER IS THE PRIORITY AND IT IS NOT THE DESIGN'S ORDER. The model
	 * lists Soulfire first because it is the Basic; this array lists the curse
	 * first because a zero-cooldown ability at the front crowds out everything
	 * behind it. See the class comment.
	 *
	 * DOMINION IS NOT IN THE ARRAY AT ALL, and that is the point of it. See
	 * `DominionRadiusCm`.
	 */
	enum : int32 { WitherTheLivingAbility = 0, SoulfireAbility = 1 };

	virtual float AttackReachCm() const override { return SoulfireRangeCm; }
	virtual float SightRadiusCm() const override { return SuccubusNoticeRadiusCm; }
	virtual float RoamRadiusCm() const override { return SuccubusRoamRadiusCm; }

	// ----------------------------------------------------------------------
	// The designed stat block
	//
	// EVERY ONE OF THESE IS A COPY, and `sim/cataclysm_sim/enemy_stats.py` is
	// the original. `tools/tests/test_succubus_matches_the_model.py` holds each
	// of them to that file. Continuous integration never builds this file, so
	// that Python test is the only thing checking these on a pull request.
	// ----------------------------------------------------------------------

	/** Seconds between shots. `attack_interval` is 2.6, the second longest in
	 *  the slice behind the Gatekeeper's 3.0. */
	static constexpr float DesignedAttackIntervalSeconds = 2.6f;

	/** Percent of all incoming damage resisted. `resistance` is 10.0. It has
	 *  little of its own; what keeps it alive is the energy shield. */
	static constexpr float DesignedResistancePercent = 10.0f;

	/**
	 * `crit_chance` 10.0 and `crit_multiplier` 200.0.
	 *
	 * **THE HARDEST-HITTING CRITICAL IN THE SLICE**, tied with the Brute's 200.
	 * Its `damage_share` of 1.60 is the second highest of the seven, and a
	 * doubling on top of that is what "slow but powerful attacks" means once the
	 * arithmetic is done.
	 */
	static constexpr float DesignedCritChancePercent = 10.0f;
	static constexpr float DesignedCritMultiplierPercent = 200.0f;

	/** `evasion` is 10.0. It dodges a little; it is not built to be hit. */
	static constexpr float DesignedEvasionPercent = 10.0f;

	/**
	 * What fraction of its health is an energy shield. `energy_shield_fraction`
	 * is 0.50, **the largest of any creature in the roster**, against the
	 * Corrupted Sentinel's 0.35 and zero for the other five.
	 *
	 * IT IS HALF OF WHAT KILLING THIS CREATURE COSTS. A shield sits in front of
	 * health and is not reduced by armour, and this creature's armour share of
	 * 0.20 is nearly the lowest in the slice, so the shield is not a bonus layer
	 * on a tough creature -- it is most of the creature's survivability.
	 * `UCataclysmDamageCalculation::Resolve` takes the shield before health.
	 */
	static constexpr float DesignedEnergyShieldFraction = 0.50f;

	/**
	 * How fast it moves, in centimetres per second. `move_speed` is 3.5 m/s.
	 *
	 * IT DOES NOT RETREAT WHEN THE PLAYER CLOSES, and the design document says
	 * why: 3.5 matches the slowest Demonic class and loses to the other two, so
	 * kiting would produce a chase it cannot win. The creature that punishes
	 * standing still is the Corrupted Sentinel, which never moves at all.
	 */
	static constexpr float DesignedWalkSpeedCmPerSecond = 350.0f;

	/** How fast it turns. `turn_rate_degrees` is the archetype default of 480. */
	static constexpr float DesignedTurnRateDegreesPerSecond = 480.0f;

	// ----------------------------------------------------------------------
	// Its body
	// ----------------------------------------------------------------------

	/**
	 * The capsule's radius, in centimetres.
	 *
	 * 48 IS THE ARCHETYPE DEFAULT AND IT IS NOT MEASURED. `body_radius` is 0.48
	 * for this creature, which is what `enemy_stats.py` gives anything nobody
	 * has measured, and issue #366 is that gap. The mesh is 112.3 cm across in
	 * its reference pose, which overstates the body because the arms are out.
	 */
	static constexpr float SuccubusCapsuleRadius = 48.0f;

	/**
	 * Half the capsule's height, in centimetres.
	 *
	 * FROM THE MESH. `tools/probe_succubus_animation.py` read `SM_Countess` at
	 * 180.8 cm tall on 2026-08-20, so half of it is 90.4. That agrees with the
	 * figure `game/docs/enemy-source-assets.md` recorded on 2026-08-07. The mesh
	 * is dropped by exactly this in `ResolveBody`.
	 */
	static constexpr float SuccubusCapsuleHalfHeight = 90.4f;

	/**
	 * How far away it notices somebody, in centimetres.
	 *
	 * THE SAME 1000 THE FOUR CREATURES THAT CAN WALK ALL USE. It shoots 8 metres
	 * and walks at 3.5 metres per second, so the two metres between noticing and
	 * shooting take it well under a second to close. The Corrupted Sentinel is
	 * the one exception at 1400, because it cannot close anything. No enemy has
	 * a designed notice radius and issue #383 is that gap.
	 */
	static constexpr float SuccubusNoticeRadiusCm = 1000.0f;

	/** How far from where it started it wanders with nothing in sight. The same
	 *  600 every creature that can walk uses. */
	static constexpr float SuccubusRoamRadiusCm = 600.0f;

	// ----------------------------------------------------------------------
	// Soulfire, the telegraphed basic attack
	//
	// EVERY NUMBER HERE IS A COPY of `ABILITIES['Succubus']`.
	// ----------------------------------------------------------------------

	/** How far it shoots, in centimetres. `Range` is 8 metres.
	 *
	 *  **EIGHT BECAUSE THAT IS THE SHORTEST GAP-CLOSER THE PLAYER HAS.** The
	 *  design document derives it from the Sword's charge and the Axe's leap in
	 *  `game/Data/WeaponSkills.csv`: a ranged enemy standing further out than
	 *  the shortest Movement-shape skill range could not be answered by every
	 *  build. */
	static constexpr float SoulfireRangeCm = 800.0f;

	/** Half the lane's width. `Radius` is 3.15 metres, so the lane is 6.3 metres
	 *  across -- the largest telegraph any ordinary Demonic enemy produces
	 *  except the Brute's. */
	static constexpr float SoulfireRadiusCm = 315.0f;

	/** How fast the bolt travels. `Speed` is 1200 cm/s, **the slowest projectile
	 *  speed anything in the game uses**: it is Magma Quake's, the slowest player
	 *  projectile in `game/Data/WeaponSkills.csv`. A slow bolt is a readable one,
	 *  and at 1200 it crosses its whole 8 metre range in two thirds of a
	 *  second. */
	static constexpr float SoulfireSpeedCmPerSecond = 1200.0f;

	/** Seconds before it may be used again. **Zero, which is what makes it the
	 *  basic attack.** The creature's 2.6 second attack interval is the only
	 *  thing spacing it out, which is what an interval means. */
	static constexpr float SoulfireCooldownSeconds = 0.0f;

	/**
	 * How long the lane is marked before the bolt leaves.
	 *
	 * DERIVED FROM THE RADIUS. The rule is `0.4 + Radius / 3.5` seconds, from
	 * the Attack Telegraphs subsection of `docs/Cataclysm_GDD_v2.md`. 0.4 + 3.15
	 * / 3.5 is exactly 1.3.
	 *
	 * **AND 3.15 METRES IS EXACTLY THE LARGEST THE RULE ALLOWS IT.** The cap is
	 * `3.5 x (attack interval / 2 - 0.4)`, which at 2.6 seconds is 3.15, so this
	 * creature's basic attack uses the whole of its allowance and its wind-up is
	 * exactly half its interval. The Corrupted Sentinel's Siege Bolt is the only
	 * other ability in the game that does this.
	 */
	static constexpr float SoulfireWindUpSeconds = 1.3f;

	static_assert(
		SoulfireWindUpSeconds > 0.4f + SoulfireRadiusCm / 100.0f / 3.5f - 0.002f
		&& SoulfireWindUpSeconds < 0.4f + SoulfireRadiusCm / 100.0f / 3.5f + 0.002f,
		"Soulfire's wind-up has drifted from the radius it is derived from. "
		"The rule is 0.4 + Radius / 3.5 seconds, from the Attack Telegraphs "
		"subsection of docs/Cataclysm_GDD_v2.md. Change both or neither.");

	static_assert(
		SoulfireWindUpSeconds < DesignedAttackIntervalSeconds,
		"Soulfire's telegraph is at least as long as the interval between "
		"shots, so the creature would begin warning about the next shot before "
		"this one had landed and the marker would never leave the ground.");

	/** What one bolt is worth, as a percentage of an ordinary hit. The Basic row
	 *  of `game/Data/SkillSlots.csv` is 100%, and the note on that row is why: a
	 *  basic attack IS weapon damage. */
	static constexpr float SoulfireDamagePercent = 100.0f;

	/** How many things it passes through. The design states no `Pierce`, so it
	 *  stops at the first thing it hits. */
	static constexpr int32 SoulfirePierce = 0;

	// ----------------------------------------------------------------------
	// Wither the Living, the curse
	//
	// THE FIRST DEBUFF ANY ENEMY IN THE GAME HAS CAST. `Withered Touch` has been
	// a row of `game/Data/StatusEffects.csv` since the table was generated and
	// nothing has ever applied it to anything.
	// ----------------------------------------------------------------------

	/** How far the curse reaches, in centimetres. `Range` is 8 metres, the same
	 *  as the bolt's: the creature curses whatever it is already shooting at. */
	static constexpr float WitherRangeCm = 800.0f;

	/** Seconds before it may be cast again. `cooldown` is 10.0 -- twice the
	 *  effect's own duration, so the player has as long without the curse as
	 *  with it. It is also the top of the Support slot's cooldown band in
	 *  `game/Data/SkillSlots.csv`, which is the slot curses live in. */
	static constexpr float WitherCooldownSeconds = 10.0f;

	/** How long Withered Touch lasts once applied. `Duration` is 5 seconds. */
	static constexpr float WitherDurationSeconds = 5.0f;

	/** How many things one cast curses. `MaxTargets` is 1. */
	static constexpr int32 WitherMaxTargets = 1;

	static_assert(
		WitherCooldownSeconds >= 2.0f * WitherDurationSeconds,
		"Wither the Living can be recast before the last one has expired, so "
		"the player would never have a moment without the curse. The design "
		"sets the cooldown at twice the duration on purpose.");

	/**
	 * Seconds it stands committed before the curse lands. **Zero, and that is
	 * designed rather than unfinished.**
	 *
	 * A DEBUFF DRAWS NO MARKER, so there is nothing for a wind-up to warn about.
	 * `TELEGRAPHED_SHAPES` in `sim/cataclysm_sim/enemy_abilities.py` covers
	 * Strike, Projectile, Aura and Movement, and the design document's sentence
	 * is "there is no ground for a curse to be drawn on". A wind-up with no
	 * marker would be the creature standing still for no visible reason.
	 *
	 * **THE CAST CLIP STILL PLAYS**, so the curse is read off the caster, which
	 * is what the design says the counter is: interrupting it. See
	 * `CastAnimationName`.
	 */
	static constexpr float WitherWindUpSeconds = 0.0f;

	/** The name of the effect it applies, from `game/Data/StatusEffects.csv`.
	 *  `UCataclysmSkillShapes::StatusTagFor` turns this into `Status.
	 *  WitheredTouch` by dropping everything that is not a letter or a digit. */
	static const TCHAR* WitherEffectName;

	// ----------------------------------------------------------------------
	// Dominion, the aura
	// ----------------------------------------------------------------------

	/**
	 * How far the aura reaches, in centimetres. `Radius` is 8 metres.
	 *
	 * **ITS RADIUS IS ITS OWN ATTACK RANGE, AND THAT IS DERIVED RATHER THAN
	 * CHOSEN.** The Succubus stands 8 metres from the player, so an ally
	 * fighting that player is at most 8 metres from the Succubus. A smaller
	 * field would buff nothing at the moment it matters; a larger one would buff
	 * a fight the Succubus is not in.
	 *
	 * **IT IS OVER THE TELEGRAPH CAP AND IS THE ONE THING EXEMPT FROM IT.**
	 * `telegraph_cap_metres` is 6.50 m for every creature with a 0.48 m body,
	 * and 8 is more. `fits_its_cycle` exempts an ability held on for as long as
	 * the creature lives, because the cap asks whether the player can be clear
	 * by the time an attack LANDS and a field that is simply on has no moment it
	 * lands. The player may walk out whenever they choose.
	 *
	 * **IT IS NOT AN ENTRY IN `EnemyAbilities` AND MUST NOT BECOME ONE.** Every
	 * entry there is chosen by range, gated by the attack interval, and USED at
	 * a moment. This is none of those things: it is on from the moment the
	 * creature spawns until the moment it dies. Putting it in the array would
	 * make it compete with Soulfire for the creature's attack interval, so the
	 * creature would stop shooting in order to hold up an aura it is already
	 * holding up.
	 */
	static constexpr float DominionRadiusCm = 800.0f;

	/**
	 * The name of the effect it grants, from `game/Data/StatusEffects.csv`,
	 * whose description is "All nearby allies gain 20% increased movement
	 * speed and attack speed". `UCataclysmSkillShapes::StatusTagFor` turns
	 * this into `Status.Commander`.
	 *
	 * **WHAT THE TAG DOES IS NOT THIS CLASS'S BUSINESS.** The Succubus grants
	 * and removes it; `ACataclysmEnemyCharacter::CommanderMultiplier` is what
	 * reads it and turns it into a number. That split is what lets a second
	 * source of Commander appear later without touching this creature.
	 *
	 * THOSE TWO STATS AND NOT EVERY STAT, decided by the project owner on
	 * 2026-08-20. The design had said only "stats", which named none.
	 * `docs/DECISIONS.md` records why maximum health in particular was
	 * ruled out.
	 */
	static const TCHAR* DominionEffectName;

	/**
	 * Seconds between one sweep for allies and the next.
	 *
	 * A TIMER RATHER THAN Tick, which is what `UCataclysmAuraSkill` does for the
	 * player's auras and for the same reason: the work is an overlap query and
	 * running it every frame would cost sixty times what it costs to be correct.
	 * Half a second is fast enough that an ally walking into the field is buffed
	 * before it has taken a swing at 2.6 second intervals.
	 */
	static constexpr float DominionRefreshSeconds = 0.5f;

	/**
	 * How long the granted effect lasts if nothing refreshes it.
	 *
	 * A SAFETY NET RATHER THAN THE MECHANISM. The buff is taken away explicitly
	 * -- when an ally leaves the field, and when this creature dies -- because
	 * "killing it first is the correct play" has to be true the instant it dies
	 * rather than up to a duration later. This duration only covers the case
	 * where neither of those runs, such as the creature being destroyed outright
	 * without dying.
	 *
	 * THREE REFRESH PERIODS, so a single missed sweep cannot make the buff
	 * flicker off and on.
	 */
	static constexpr float DominionGrantSeconds = 1.5f;

	static_assert(
		DominionGrantSeconds > DominionRefreshSeconds,
		"Dominion's granted effect expires before the next sweep renews it, so "
		"an ally standing still inside the field would lose the buff and get it "
		"back over and over.");

	/**
	 * Sweeps for allies once and brings the field up to date.
	 *
	 * PUBLIC SO A TEST CAN DRIVE IT WITHOUT WAITING FOR A TIMER.
	 *
	 * @return how many allies are inside the field and holding the effect
	 */
	int32 PulseDominion();

	/** Takes the effect off everything currently holding it from this creature.
	 *  Called when it dies and when it leaves the level. */
	void EndDominion();

	/**
	 * Everything this creature's aura is currently buffing. Read by tests,
	 * which have nothing else to look at.
	 *
	 * WEAK, SO A BUFFED ALLY CAN STILL BE COLLECTED. A hard reference here
	 * would keep every creature this Succubus ever buffed alive for as long as
	 * the Succubus was, including corpses that had already been removed.
	 *
	 * AND THEREFORE NOT `BlueprintReadOnly`. Unreal Header Tool refuses
	 * `TArray<TWeakObjectPtr<AActor>>` as a Blueprint type outright -- "Type
	 * 'TArray<TWeakObjectPtr<AActor>>' is not supported by blueprint" -- so the
	 * choice is between weak pointers and Blueprint access, and nothing in
	 * Blueprint reads this.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Enemy")
	TArray<TWeakObjectPtr<AActor>> DominionHolders;

	/** The figures really in use, which are the console overrides when they are
	 *  set and the designed ones otherwise. */
	static float AttackIntervalSecondsInUse();
	static float WitherCooldownSecondsInUse();

	// ----------------------------------------------------------------------
	// Where its art lives
	// ----------------------------------------------------------------------

	static const TCHAR* BodyMeshPath;
	static const TCHAR* AnimationFolder;

	/** Standing. `Idle_Relaxed`, measured at 42.3333 seconds -- by a long way the
	 *  longest idle in the project, against the Hellhound's 10.0. The pack also
	 *  ships `Idle_Straight` at 7.5 seconds. */
	static const TCHAR* IdleAnimationName;

	/** Walking. `Jog_Fwd`, 1.8000 seconds. */
	static const TCHAR* JogAnimationName;

	/**
	 * Casting Soulfire. `Primary_Attack_Normal`, 0.9000 seconds.
	 *
	 * **IT IS SHORTER THAN THE WIND-UP IT PLAYS ACROSS**, which is 1.3 seconds,
	 * so it is played at its authored speed and the creature holds the last pose
	 * for the remaining 0.4 seconds before the bolt leaves. That is the
	 * project's rule from issue #369 -- never slower than authored, only faster
	 * -- applied to a clip that is already short enough.
	 *
	 * THE PACK'S `Primary_Attack_Slow` WOULD FILL THE WIND-UP ALMOST EXACTLY at
	 * 1.5000 seconds and a play rate of 1.1538, and it is NOT used, because it
	 * is the one primary attack that ships a separate
	 * `Primary_Attack_Slow_Recovery` clip. A clip that needs a recovery ends
	 * somewhere other than where it started, and nothing here plays a recovery.
	 * Whether the 0.4 second hold reads badly is issue #767.
	 */
	static const TCHAR* AttackAnimationName;

	/** Casting Wither the Living. `Cast`, 1.1333 seconds. **A different clip
	 *  from the bolt's on purpose**: the design's counter to the curse is
	 *  interrupting it, and a player cannot interrupt what they cannot tell
	 *  apart from an ordinary attack. */
	static const TCHAR* CastAnimationName;

	/** **ONE DEATH, WHICH IS THE FEWEST IN THE PROJECT** along with the Brute's.
	 *  `Death`, measured at 1.6667 seconds, inside
	 *  `UCataclysmEnemyDeath::LongestCorpseSeconds`. */
	static constexpr int32 DeathAnimationCount = 1;
	static const TCHAR* DeathAnimationNames[DeathAnimationCount];

	/**
	 * How fast the walk clip carries a planted foot, in centimetres per second.
	 *
	 * MEASURED, NOT CHOSEN. `tools/measure_animation_stride.py` read 321.0 cm/s
	 * on the -Y axis for `Jog_Fwd` on 2026-08-20, with `Idle_Relaxed` reading
	 * 0.0 as the control -- which is what says the method found the right axis
	 * on this rig rather than measuring nothing.
	 */
	static constexpr float AuthoredJogSpeedCmPerSecond = 321.0f;

	/** How long the attack clip runs, in seconds. Measured 0.9000. */
	static constexpr float AttackAnimationSeconds = 0.9f;

	/** Play rate floor and ceiling, the same two figures every other creature
	 *  clamps to. */
	static constexpr float MinimumPlayRate = 0.2f;
	static constexpr float MaximumPlayRate = 2.5f;

	static_assert(
		AttackAnimationSeconds <= SoulfireWindUpSeconds * MaximumPlayRate,
		"The Succubus's attack clip no longer fits inside Soulfire's wind-up "
		"even at the play rate ceiling, so the bolt would leave while the "
		"creature was still winding up to fire it. See "
		"tools/probe_succubus_animation.py.");

	/**
	 * The play rate the walk needs so the feet do not slide.
	 *
	 * THE RATIO OF WHAT IT MOVES AT TO WHAT THE CLIP CARRIES IT AT: 350 / 321.0
	 * is 1.090, **the gentlest walk in the project**, against the Imp's 1.699
	 * and the Hellhound's 2.478.
	 */
	static float JogPlayRate();

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
	TObjectPtr<class UAnimSequence> AttackAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> CastAnimation;

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

	/** The last bolt it fired. Read by tests, which have nothing else to look
	 *  at. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class ACataclysmProjectile> LastShotFired;

	/** The last thing it cursed, and whether the curse took. Read by tests. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TWeakObjectPtr<AActor> LastCursed;

private:
	/** Plays one clip once and records it. Returns how long it will take. */
	float PlayOneShot(class UAnimSequence* Animation, float HoldSeconds = 0.0f);

	/** Runs `PulseDominion` from the world's timer manager. */
	void DominionTick();

	FTimerHandle DominionTimer;
};
