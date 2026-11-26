// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "CataclysmWraithCharacter.generated.h"

/**
 * The Wraith: Death's first creature of its own, a hooded scythe ghost that cannot be walked away from.
 *
 * THE DESIGN THE PROJECT OWNER APPROVED ON 2026-11-26 ("I'm happy with those enemy designs"), and it replaces the
 * Hellhound in Death dungeons (`FCataclysmFloorPopulator::PackKindsFor`). WHAT IT DOES, from `ABILITIES['Wraith']` in
 * `sim/cataclysm_sim/enemy_abilities.py`:
 *
 *   Reap        Strike    Basic     a scythe swing in a 120 degree arc, 1.6 metres, up to three caught
 *   Phase Dash  Movement  Movement  an 8 metre dash through the player along a marked lane, ending behind them
 *   Soul Grab   Strike    Special   a marked 4 metre ring that hits and pulls whoever is in it to the Wraith
 *
 * THREE ABILITIES, THE MOST A BASIC CREATURE MAY HAVE (ruled 2026-08-07). Each covers its own distance: Reap up to its
 * reach, Soul Grab from there out to 4 metres, and the dash from 4 to 8, so a player at any distance has something
 * coming. Soul Grab's effect was left to be designed ("Design its effect yourself"), and is a judgement.
 *
 * ITS ART IS THE OWNER'S PURCHASED WRAITH PACK (`/Game/Wraith`), which lives only in the main checkout, gitignored like
 * the Paragon packs. One look for every Wraith, as ruled: Skin1 with Helm1 and the scythe. The helm is a skeletal mesh of
 * its own that follows the body's pose (`SetLeaderPoseComponent`, as the pack's own `BP_Wraith` does), and the scythe a
 * static mesh on the skeleton's `scythe` bone.
 *
 * IT FLIES ALONG THE SAME GROUND PATHS EVERY CREATURE WALKS. Its loops are the pack's flying ones, and a hovering body
 * has no planted foot to slide, so they play at their authored speed: there is nothing to measure a stride from.
 */
UCLASS()
class CATACLYSM_API ACataclysmWraithCharacter : public ACataclysmEnemyCharacter
{
	GENERATED_BODY()

public:
	ACataclysmWraithCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Reap: its target, then up to two more in the arc. */
	virtual void AttackTarget(AActor* Target) override;

	//~ Flinching when hit. Issue #745; see ACataclysmEnemyCharacter::FlinchFrom.
	virtual bool IsPlayingAOneShot() const override;
	virtual float PlayFlinchClip(class UAnimSequence* Clip) override;

	/** Seconds between swings BEFORE any buff. See the Hellhound's: `SecondsBetweenAttacks` is `final` on the base. */
	virtual float DesignedSecondsBetweenAttacks() const override;

	virtual TArray<FCataclysmEnemyAbility> EnemyAbilities() const override;
	virtual void UseEnemyAbility(int32 Index, AActor* Target, const FVector& AimedAt) override;
	virtual void BeginEnemyAbilityWindUp(int32 Index, AActor* Target) override;

	/** Which entry of EnemyAbilities is which. Their range bands do not overlap, so their order decides nothing. */
	enum : int32 { PhaseDashAbility = 0, SoulGrabAbility = 1 };

	virtual float AttackReachCm() const override { return DesignedMeleeReachCm; }
	virtual float SightRadiusCm() const override { return WraithNoticeRadiusCm; }
	virtual float RoamRadiusCm() const override { return WraithRoamRadiusCm; }

	// ----------------------------------------------------------------------
	// The designed stat block
	//
	// EVERY ONE OF THESE IS A COPY, and `sim/cataclysm_sim/enemy_stats.py` is the original.
	// `tools/tests/test_wraith_matches_the_model.py` holds each of them to that file; continuous integration never builds
	// this file, so that Python test is the only thing checking these on a pull request.
	// ----------------------------------------------------------------------

	/**
	 * Seconds between swings. `attack_interval` is 1.25.
	 *
	 * NOT THE DESIGN'S 1.6, A JUDGEMENT OF 2026-11-26. A 1.6 second cycle allows a 1.4 metre marker by the Attack
	 * Telegraphs rules, so a 1.6 metre swing on it would have to be marked; at 1.25 the largest marker is 0.79 metres,
	 * under the one metre floor, so the swing stays unmarked as the design meant. The damage per second is the design's.
	 */
	static constexpr float DesignedAttackIntervalSeconds = 1.25f;

	/** How far the scythe reaches, centre to centre, in centimetres. Reap's `Radius` is 1.6 metres. */
	static constexpr float DesignedMeleeReachCm = 160.0f;

	/** Reap's arc, in degrees, and how many one swing may catch. `Angle` 120 and `MaxTargets` 3. */
	static constexpr float ReapAngleDegrees = 120.0f;
	static constexpr int32 ReapMaxTargets = 3;

	/** `resistance` 15.0, the Brute's. */
	static constexpr float DesignedResistancePercent = 15.0f;

	/** `crit_chance` 10.0 and `crit_multiplier` 175.0. */
	static constexpr float DesignedCritChancePercent = 10.0f;
	static constexpr float DesignedCritMultiplierPercent = 175.0f;

	/** `evasion` 30.0, THE HIGHEST OF ANY CREATURE (the Imp's 25 was), with no armour: area damage is the answer. */
	static constexpr float DesignedEvasionPercent = 30.0f;

	/** `energy_shield_fraction` 0.0, written out so the zero is visibly designed. */
	static constexpr float DesignedEnergyShieldFraction = 0.0f;

	/**
	 * How fast it moves. `move_speed` is 5.0 metres per second: faster than every class (the Ravager's 4.6), so it
	 * cannot be walked away from, and slower than the Imp's 6.5. No separate chase speed (`chase_speed` 0.0).
	 *
	 * THE BASE CLASS DOES NOT SET MaxWalkSpeed, so this constructor must.
	 */
	static constexpr float DesignedWalkSpeedCmPerSecond = 500.0f;

	/** `turn_rate_degrees`, the archetype default of 480. */
	static constexpr float DesignedTurnRateDegreesPerSecond = 480.0f;

	// ----------------------------------------------------------------------
	// Its body
	// ----------------------------------------------------------------------

	/** The capsule's radius. 48, THE ARCHETYPE DEFAULT, NOT MEASURED (`body_radius` 0.48; issue #366's gap). */
	static constexpr float WraithCapsuleRadius = 48.0f;

	/**
	 * Half the capsule's height. 88, NOT MEASURED: the mesh's bounds cannot be read without the editor, which this
	 * slice had no use of. A hooded figure a little taller than the player's 176 cm; the window's mesh test is where a
	 * wrong figure shows, and this is the one number to change.
	 */
	static constexpr float WraithCapsuleHalfHeight = 88.0f;

	/** How far away it notices somebody: 10 metres, the Hellhound's and the Abyssal Warden's (issue #383's gap). It
	 *  must be at least the dash's 8 metres, or the dash could never be used on something it had seen. */
	static constexpr float WraithNoticeRadiusCm = 1000.0f;

	/** How far from where it started it wanders while it has seen nothing. The Hellhound's. */
	static constexpr float WraithRoamRadiusCm = 600.0f;

	// ----------------------------------------------------------------------
	// Phase Dash: through the player, out the other side
	//
	// COPIES OF `ABILITIES['Wraith']`, except the speed, a judgement marked below.
	// ----------------------------------------------------------------------

	/** How far it dashes. `Range` is 8 metres, the shortest player gap-closer in the game. */
	static constexpr float PhaseDashRangeCm = 800.0f;

	/** Half the lane's width. `Radius` is 1.0 metre, the smallest marker the telegraph rules allow (the design table's
	 *  0.5 would be a marker smaller than the creature, which is not a telegraph). A judgement. */
	static constexpr float PhaseDashRadiusCm = 100.0f;

	/** Seconds before it may dash again. `cooldown` is 5.0, the Movement slot's and the Hellhound's. */
	static constexpr float PhaseDashCooldownSeconds = 5.0f;

	/** How long the lane is marked: 0.4 + Radius / 3.5 seconds, the Attack Telegraphs rule. 0.4 + 1.0 / 3.5 is 0.686. */
	static constexpr float PhaseDashWindUpSeconds = 0.69f;

	static_assert(
		PhaseDashWindUpSeconds > 0.4f + PhaseDashRadiusCm / 100.0f / 3.5f - 0.005f
		&& PhaseDashWindUpSeconds < 0.4f + PhaseDashRadiusCm / 100.0f / 3.5f + 0.005f,
		"Phase Dash's wind-up has drifted from the radius it is derived from. The rule is 0.4 + Radius / 3.5 seconds, "
		"from the Attack Telegraphs subsection of docs/Cataclysm_GDD_v2.md.");

	/**
	 * How fast it travels while dashing, in centimetres per second.
	 *
	 * **CHOSEN, NOT DERIVED, A JUDGEMENT.** The pack's dash clip is in place, so there is no length to divide a
	 * distance by. The run takes the 0.700 seconds the Abyssal Warden's and the Hellhound's charges take, which is what
	 * a player reads once the marker has gone: 800 / 0.7 is 1142.86, 2.3 times its own walk.
	 */
	static constexpr float PhaseDashSpeedCmPerSecond = 1142.86f;

	/** What one pass is worth, as a percentage of an ordinary hit: the Movement row of SkillSlots.csv. */
	static constexpr float PhaseDashDamagePercent = 100.0f;

	// ----------------------------------------------------------------------
	// Soul Grab: grasping hands round it, and a pull
	// ----------------------------------------------------------------------

	/** The ring's radius. `Radius` is 4.0 metres. */
	static constexpr float SoulGrabRadiusCm = 400.0f;

	/** Seconds before it may grab again. `cooldown` is 10.0, the top of the Special slot's band. */
	static constexpr float SoulGrabCooldownSeconds = 10.0f;

	/** 0.4 + 4.0 / 3.5 is 1.543 seconds, the Attack Telegraphs rule. */
	static constexpr float SoulGrabWindUpSeconds = 1.54f;

	static_assert(
		SoulGrabWindUpSeconds > 0.4f + SoulGrabRadiusCm / 100.0f / 3.5f - 0.005f
		&& SoulGrabWindUpSeconds < 0.4f + SoulGrabRadiusCm / 100.0f / 3.5f + 0.005f,
		"Soul Grab's wind-up has drifted from the radius it is derived from.");

	/** What it is worth, as a percentage of an ordinary hit: the Special row of SkillSlots.csv, 150. */
	static constexpr float SoulGrabDamagePercent = 150.0f;

	/** How far from the Wraith's centre a pulled player is left, in centimetres: inside Reap's reach, so the next
	 *  swing reaches them. A judgement. */
	static constexpr float SoulGrabLeavesAtCm = 100.0f;

	static_assert(SoulGrabLeavesAtCm < DesignedMeleeReachCm,
		"Soul Grab must leave its catch inside Reap's reach, or the pull sets up nothing.");

	/**
	 * THE DASH'S SHORTEST USE IS SOUL GRAB'S RADIUS, BY DESIGN: Soul Grab covers the ground inside it, so the two never
	 * compete for one distance. "Between 4 and 8 metres", the design's band.
	 */
	static constexpr float PhaseDashMinimumRangeCm = SoulGrabRadiusCm;

	static_assert(PhaseDashMinimumRangeCm > DesignedWalkSpeedCmPerSecond * PhaseDashWindUpSeconds,
		"The dash's shortest use is ground the Wraith could walk during its own wind-up, which is worse than walking.");
	static_assert(PhaseDashMinimumRangeCm < PhaseDashRangeCm, "Phase Dash can never be used.");

	/** The figures really in use: the console override when one is set, the designed figure otherwise. */
	static float AttackIntervalSecondsInUse();
	static float PhaseDashCooldownSecondsInUse();
	static float SoulGrabCooldownSecondsInUse();

	/** How many the last Reap and the last Soul Grab caught. Read by tests, which have nothing else to look at. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	int32 LastReapCaught = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	int32 LastSoulGrabCaught = 0;

	// ----------------------------------------------------------------------
	// The Reaper, which wears it
	//
	// THE OWNER'S RULING OF 2026-11-26: the Wraith art plays The Reaper dungeon modifier, an Abyssal Warden until then.
	// THE ROW'S OWN WORDS STILL RULE IT: "The embodiment of death slowly stalks the player. If they are hit by his
	// scythe, they instantly die." So the Reaper is a Wraith that WALKS AT THE WARDEN'S 2.8 METRES A SECOND, under the
	// slowest class's 3.5, and USES NEITHER THE DASH NOR THE PULL, either of which would make a blow that kills outright
	// impossible to escape. It wears Helm2, one of the three helms the owner kept for it, so it reads as no ordinary
	// Wraith. A JUDGEMENT.
	// ----------------------------------------------------------------------

	/** The Reaper's walk: the Abyssal Warden's 2.8 metres a second, which is what made "slowly" true before. */
	static constexpr float ReaperWalkSpeedCmPerSecond = 280.0f;

	static_assert(ReaperWalkSpeedCmPerSecond < 350.0f,
		"The Reaper must be slower than the slowest class's 3.5 metres a second: its row says it slowly stalks, and one "
		"blow of it kills.");

	static const TCHAR* ReaperHelmMeshPath;

	/** Makes this Wraith the Reaper: the slow walk, no dash or pull, and the Reaper's helm. Called by
	 *  `ACataclysmDungeonGameMode::RaiseTheReaper` after the spawn. */
	void BecomeTheReaper();

	/** Whether this Wraith is the Reaper. Read by tests and by `EnemyAbilities`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bIsTheReaper = false;

	// ----------------------------------------------------------------------
	// Where its art lives
	// ----------------------------------------------------------------------

	static const TCHAR* BodyMeshPath;
	static const TCHAR* HelmMeshPath;
	static const TCHAR* ScytheMeshPath;
	static const TCHAR* ScytheBoneName;
	static const TCHAR* FirstReapAnimationPath;
	static const TCHAR* SecondReapAnimationPath;
	static const TCHAR* IdleAnimationPath;
	static const TCHAR* FlyAnimationPath;
	static const TCHAR* PhaseDashAnimationPath;
	static const TCHAR* SoulGrabAnimationPath;
	static const TCHAR* FirstDeathAnimationPath;
	static const TCHAR* SecondDeathAnimationPath;

	/** How long each scythe swing runs: `Anim_Wraith_Scythe_Attack2` and `_Attack5` are 1.6667 seconds each, read from
	 *  the assets' `SequenceLength` on 2026-11-26. Played at 1.33 inside the 1.25 second interval. */
	static constexpr float ReapAnimationSeconds = 1.6667f;

	static_assert(ReapAnimationSeconds / DesignedAttackIntervalSeconds <= 2.5f,
		"A scythe swing would need a play rate above the 2.5 ceiling to fit the interval.");

	/** When the swing connects, in seconds into the clip: ZERO, NOT MEASURED, as the Hellhound's bite (issue #783).
	 *  The blow lands at once; the one number to change once `tools/measure_attack_impact.py` reads these clips. */
	static constexpr float ReapStrikeSeconds = 0.0f;

	/** Above this it is moving, below it is standing. The Hellhound's figure, for its reason. */
	static constexpr float MovingThresholdCmPerSecond = 10.0f;

	/** Play rate floor and ceiling, the figures every creature clamps to. */
	static constexpr float MinimumPlayRate = 0.2f;
	static constexpr float MaximumPlayRate = 2.5f;

	// ----------------------------------------------------------------------
	// What it is wearing and playing
	// ----------------------------------------------------------------------

	/**
	 * Puts the body, the helm, the scythe and the animations on, if the art pack is installed. PUBLIC SO A TEST CAN
	 * CALL IT, as the Hellhound's is.
	 *
	 * @return whether the body was found and worn
	 */
	bool ResolveBody(bool bIncludeAnimation = true);

	/** Puts the right looping clip on, or leaves a one-shot alone to finish. Public so a test can drive it. */
	void UpdateLoopingAnimation();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class USkeletalMeshComponent> Helm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UStaticMeshComponent> Scythe;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> IdleAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> FlyAnimation;

	/** The two swings, played in turn. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TArray<TObjectPtr<class UAnimSequence>> ReapAnimations;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> PhaseDashAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> SoulGrabAnimation;

	/** Which looping clip is on now, or null while a one-shot has the mesh. Read by tests. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> CurrentLoopingAnimation;

	/** The clip the last one-shot chose, and the rate it played at. Read by tests. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> LastPlayedAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float LastPlayedRate = 0.0f;

	/** When the one-shot playing now finishes, in world seconds; zero when none is. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float OneShotEndsAtSeconds = 0.0f;

	/** Which swing comes next: 0 or 1. */
	int32 NextReapClip = 0;

private:
	/** Plays one clip once inside `HoldSeconds` (its own length when zero), never slower than authored. */
	float PlayOneShot(class UAnimSequence* Animation, float HoldSeconds = 0.0f);
};
