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
 * TWO OF ITS THREE DESIGNED ABILITIES ARE HERE. The third is not, and leaving it
 * out is deliberate rather than unfinished:
 *
 *   Sunder       Basic     Strike    the ordinary attack, on the 2.4 s interval
 *   Molten Roar  Ultimate  Strike    a 5.6 m ring at its feet, every 12 s
 *   Stampede     Movement  Movement  ABSENT. Issue #491
 *
 * WHY STAMPEDE IS ABSENT RATHER THAN STUBBED, and this matters because adding it
 * "so it is there" would be worse than useless.
 * `ACataclysmEnemyController::ChooseAbility` reads an ability's MinRangeCm,
 * MaxRangeCm and CooldownSeconds and NOTHING ELSE -- it never looks at Shape --
 * and returns the first entry that fits. A Stampede entry spanning 0 to 800 cm
 * would therefore be chosen ahead of Molten Roar everywhere inside eight metres,
 * draw no marker at all because the marker switch's `default:` case returns
 * silently for a Movement shape, hold the creature still for the length of its
 * wind-up, and then do nothing. It would fail without complaining.
 *
 * SO THIS CREATURE CANNOT CLOSE ON THE PLAYER. It walks at 2.8 metres per second
 * and has no chase speed at all, against player classes at 3.5, 4.0 and 4.6. A
 * player who walks backwards is never caught. That is the known consequence of
 * #491 and not a defect in this class.
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
	 * Which entry of EnemyAbilities is which. Order in that array is priority,
	 * and there is only one thing in it.
	 */
	enum : int32 { MoltenRoarAbility = 0 };

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

	/** The ring's radius. 5.6 metres, the largest marker in the game. */
	static constexpr float MoltenRoarRadiusCm = 560.0f;

	/** Seconds before it may be used again. Derived twice over in the design:
	 *  five of its 2.4 second swings, which is how long it takes to kill the
	 *  reference geared character, and the bottom of the Ultimate slot's band
	 *  in `game/Data/SkillSlots.csv`. */
	static constexpr float MoltenRoarCooldownSeconds = 12.0f;

	/**
	 * How long the marker is on the ground before the ring goes off.
	 *
	 * DERIVED FROM THE RADIUS, NOT CHOSEN. The wind-up rule in section X of
	 * `docs/Cataclysm_GDD_v2.md` is `0.4 + Radius / 3.5` seconds, and
	 * 0.4 + 5.6 / 3.5 is exactly 2.0. A `static_assert` beside this holds the
	 * two together.
	 *
	 * IT IS THE LONGEST TELEGRAPH IN THE GAME. The Brute's stomp is 1.4 seconds
	 * and nothing else is over one.
	 */
	static constexpr float MoltenRoarWindUpSeconds = 2.0f;

	// A TOLERANCE RATHER THAN EQUALITY, DELIBERATELY. 0.4 + 5.6 / 3.5 is exactly
	// 2 in decimal and is not exactly 2 in binary floating point: neither 5.6
	// nor 0.4 is representable, so the sum lands a fraction either side. An
	// equality assert here would either fail on a correct number or pass by
	// luck, and which of the two would depend on the compiler.
	static_assert(
		MoltenRoarWindUpSeconds
			> 0.4f + MoltenRoarRadiusCm / 100.0f / 3.5f - 0.001f
		&& MoltenRoarWindUpSeconds
			< 0.4f + MoltenRoarRadiusCm / 100.0f / 3.5f + 0.001f,
		"Molten Roar's wind-up has drifted from the radius it is derived from. "
		"The rule is 0.4 + Radius / 3.5 seconds, from the Attack Telegraphs "
		"subsection of docs/Cataclysm_GDD_v2.md. Change both or neither.");

	/** What one use is worth, as a percentage of an ordinary hit. The Ultimate
	 *  row of `game/Data/SkillSlots.csv`. It is the first thing in the game to
	 *  use that slot. */
	static constexpr float MoltenRoarDamagePercent = 400.0f;

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
	 * The two ordinary swings, and the roar.
	 *
	 * TWO SWINGS BECAUSE THIS CREATURE CAN AFFORD TWO, and it is the only one of
	 * the seven that can. `PrimaryAttack_LA` and `PrimaryAttack_RA` are 1.1333
	 * seconds each, measured 2026-08-09, and 2.2667 fits inside the 2.4 second
	 * interval with a tenth of a second to spare. Alternating them is
	 * presentation rather than a second ability.
	 *
	 * `Ultimate_Roar` is 1.4000 seconds, so the 2.0 second wind-up holds the
	 * whole clip at its authored speed and nothing is compressed.
	 */
	static const TCHAR* LeftSwingAnimationPath;
	static const TCHAR* RightSwingAnimationPath;
	static const TCHAR* MoltenRoarAnimationPath;

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
	TObjectPtr<class UAnimSequence> MoltenRoarAnimation;

	/** Which swing comes next. Flipped by every swing, so the creature
	 *  alternates rather than repeating one arm. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bSwingLeftNext = true;

	/** The clip the last call to PlayOneShot chose. Read by tests, which cannot
	 *  otherwise see what was played. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> LastPlayedAnimation;

	/** Which swing it would play next, without changing anything. Separated from
	 *  PlayAttackAnimation so a test can ask without playing. */
	class UAnimSequence* NextSwingAnimation() const;

	/** Plays one clip once, and records it. Returns how long it will take,
	 *  which is its length divided by whatever rate it needed. */
	float PlayOneShot(class UAnimSequence* Animation, float HoldSeconds = 0.0f);

	/** The cooldown Molten Roar is really using, which is the console override
	 *  when one is set and the designed figure otherwise. Read through this
	 *  rather than off the constant, so a console variable set mid-fight takes
	 *  effect on the next thinking pass. */
	static float MoltenRoarCooldownSecondsInUse();

	/** The seconds between swings really in use, same arrangement. */
	static float AttackIntervalSecondsInUse();

	/** Play rate floor and ceiling, the same two figures the Brute clamps to.
	 *  Below the floor an animation reads as frozen; above the ceiling it reads
	 *  as a blur. Molten Roar needs no compression at all -- 1.4 seconds inside
	 *  a 2.0 second wind-up -- so these only bite if a clip is ever replaced. */
	static constexpr float MinimumPlayRate = 0.2f;
	static constexpr float MaximumPlayRate = 2.5f;

private:
	void PlayAttackAnimation();
	bool ResolveAnimationBlueprint(class USkeletalMeshComponent* MeshComponent);
};
