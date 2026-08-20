// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "CataclysmImpCharacter.generated.h"

/**
 * The Imp: fast swarming melee, weak on its own.
 *
 * WHAT IT DOES, from `ABILITIES['Imp']` in
 * `sim/cataclysm_sim/enemy_abilities.py`:
 *
 *   Rend   Strike   Basic   a claw swipe at whatever it is standing next to
 *
 * AND THAT IS THE WHOLE LIST. **`EnemyAbilities` is not overridden here**, so
 * this creature offers none, which is the only creature in the slice that
 * offers none. It is the design rather than an omission, and
 * `docs/Cataclysm_GDD_v2.md` says so in as many words: "The Imp has one attack
 * and nothing else, and that is the design rather than an omission." Rend is
 * not an entry in that array either -- like every other basic attack in this
 * project it is `MeleeReachCm` plus `AttackIntervalSeconds`.
 *
 * IT CANNOT TELEGRAPH, AND NOTHING HERE DECIDES THAT. The telegraph rule caps a
 * marker at `3.5 x (attack interval / 2 - 0.4)` metres, and at a 0.9 second
 * interval that is 0.2 metres -- smaller than the creature standing in it. So
 * the exclusion falls out of the creature being fast. The same document's Twenty
 * Markers subsection is why that matters: the enemies that arrive in packs are
 * exactly the ones that produce no markers, so a pack cannot fill the screen
 * with them.
 *
 * WHAT MAKES THIS CREATURE DIFFERENT FROM THE THREE THAT EXIST. It is the first
 * that is designed to arrive in numbers. **A pack is ten**, which the design
 * states outright, and the sandbox spawns ten. Everything about its shape
 * follows from that: its reach is set by where the second rank of a crowd
 * stands, its defence is evasion rather than armour so that area damage answers
 * it and single-target damage does not, and it has no ability, because whatever
 * an Imp does is multiplied by ten.
 *
 * ITS REACH IS SET BY THE PACK, NOT BY THE CREATURE. See DesignedMeleeReachCm.
 */
UCLASS()
class CATACLYSM_API ACataclysmImpCharacter : public ACataclysmEnemyCharacter
{
	GENERATED_BODY()

public:
	ACataclysmImpCharacter();

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

	virtual float AttackReachCm() const override { return DesignedMeleeReachCm; }
	virtual float SightRadiusCm() const override { return ImpNoticeRadiusCm; }
	virtual float RoamRadiusCm() const override { return ImpRoamRadiusCm; }

	// ----------------------------------------------------------------------
	// The designed stat block
	//
	// EVERY ONE OF THESE IS A COPY, and `sim/cataclysm_sim/enemy_stats.py` is
	// the original. `tools/tests/test_imp_matches_the_model.py` holds each of
	// them to that file, so when one of those fails the usual fix is to change
	// the C++. Continuous integration never builds this file, so that Python
	// test is the only thing checking these on a pull request.
	// ----------------------------------------------------------------------

	/** Seconds between claw swipes. `attack_interval` is 0.9, the shortest in
	 *  the slice. */
	static constexpr float DesignedAttackIntervalSeconds = 0.9f;

	/**
	 * How far it can reach, centre to centre, in centimetres.
	 *
	 * **SET BY THE PACK, NOT BY THE CREATURE, AND THAT IS THE WHOLE POINT.**
	 * `Radius` in `ABILITIES['Imp']` is 1.32 metres, and the design derives it
	 * from crowd geometry rather than from the length of an arm. Bodies cannot
	 * overlap, so a swarm queues in rings around whatever it is attacking, and a
	 * body of radius `r` standing `D` from the centre covers `2 x arcsin(r / D)`
	 * of the circle. With the player's 0.42 m capsule and this creature's 0.30 m
	 * body that gives:
	 *
	 *     first rank    0.72 m out     7 fit      7 in total
	 *     second rank   1.32 m out    13 fit     20 in total
	 *     third rank    1.92 m out    20 fit     40 in total
	 *
	 * **1.32 metres is exactly the second rank.** The design document commits to
	 * ten Imps killing a geared character in 4.9 seconds and twenty in 2.4, and
	 * one rank is seven. A reach that let only the rank in contact swing would
	 * make both of those figures false, because the eighth Imp onward would be
	 * standing behind the first seven doing nothing.
	 *
	 * SO TWENTY IS THE MOST THAT CAN EVER HIT AT ONCE, and the geometry is the
	 * only thing enforcing it. There is no attack-token rule and the design says
	 * there deliberately will not be one.
	 */
	static constexpr float DesignedMeleeReachCm = 132.0f;

	/** Percent of all incoming damage resisted. `resistance` is 0.0. Written out
	 *  rather than left to the base's default, so the zero is visibly designed
	 *  rather than visibly forgotten. */
	static constexpr float DesignedResistancePercent = 0.0f;

	/** `crit_chance` 5.0 and `crit_multiplier` 150.0, which is the archetype
	 *  baseline for both. This creature is not the one that lands big hits. */
	static constexpr float DesignedCritChancePercent = 5.0f;
	static constexpr float DesignedCritMultiplierPercent = 150.0f;

	/**
	 * Percent of blows it avoids outright. `evasion` is 25.0, the highest in the
	 * slice.
	 *
	 * **ITS DEFENCE IS NOT ARMOUR, AND `armor_share` IS EXACTLY ZERO.** No other
	 * creature in the roster has none at all. `enemy_stats.py` says why in as
	 * many words: evasion is the Imp's and the Hellhound's designed defence.
	 *
	 * AND IT IS WHAT MAKES AREA DAMAGE THE ANSWER TO A PACK. Evasion avoids
	 * direct attacks only, so a single-target build fighting twenty of these
	 * misses a quarter of its swings and an area skill misses none. The design
	 * document's advice for this creature is one line: bring area damage.
	 */
	static constexpr float DesignedEvasionPercent = 25.0f;

	/** `energy_shield_fraction` 0.0. Written out for the same reason the
	 *  resistance above is. */
	static constexpr float DesignedEnergyShieldFraction = 0.0f;

	/**
	 * How fast it moves. `move_speed` is 6.5 metres per second.
	 *
	 * **THE SECOND FASTEST THING IN THE GAME**, behind the Hellhound's 7.5 and
	 * ahead of every player class at 3.5, 4.0 and 4.6. The design document is
	 * blunt about what that means: "Walking away from an Imp is never an
	 * escape", so a pack that has closed cannot be un-closed on foot and the
	 * Movement slot is what breaks it.
	 *
	 * IT HAS NO SEPARATE CHASE SPEED. `chase_speed` is 0.0, the same arrangement
	 * the Abyssal Warden and the Hellhound have and the opposite of the Brute's.
	 *
	 * THE BASE CLASS DOES NOT SET MaxWalkSpeed AT ALL. An enemy that forgets to
	 * set it moves at Unreal's default 600 cm/s, which for this creature would
	 * be a silent slowing.
	 */
	static constexpr float DesignedWalkSpeedCmPerSecond = 650.0f;

	/** How fast it turns. `turn_rate_degrees` is the archetype default of 480,
	 *  which is what `ACataclysmEnemyCharacter` constructs every enemy with. */
	static constexpr float DesignedTurnRateDegreesPerSecond = 480.0f;

	// ----------------------------------------------------------------------
	// Its body
	// ----------------------------------------------------------------------

	/**
	 * The capsule's radius, in centimetres.
	 *
	 * **THE ONLY MEASURED BODY RADIUS IN THE ROSTER.** `body_radius` is 0.30 for
	 * this creature and 0.48 -- the dataclass default -- for the other six, which
	 * is the gap issue #366 tracks. The design document says where the 0.30 came
	 * from: it is the lesser imp minion's capsule in
	 * `game/Source/Cataclysm/AbilitySystem/CataclysmMinion.cpp`, "because it is
	 * the same creature".
	 *
	 * IT IS LOAD-BEARING RATHER THAN DECORATIVE. The ring arithmetic under
	 * DesignedMeleeReachCm above is computed from this number and the player's,
	 * and the creature's reach is computed from that. Change this and the reach
	 * has to move with it.
	 *
	 * AND THE MESH REALLY IS THIS WIDE, which is not something anybody had
	 * checked. `tools/probe_imp_animation.py` measured the shoulders of
	 * `Minion_Lane_Melee_Dawn` at **63.5 cm apart** at the mesh's authored size,
	 * against the 60 cm this capsule is across. See ImpMeshScale.
	 */
	static constexpr float ImpCapsuleRadius = 30.0f;

	/**
	 * Half the capsule's height, in centimetres.
	 *
	 * FROM THE MESH. `tools/probe_imp_animation.py` read
	 * `Minion_Lane_Melee_Dawn` at 175.9 cm tall on 2026-08-20, and
	 * `game/docs/enemy-source-assets.md` records the same figure, so half of it
	 * is 87.95. The mesh is dropped by exactly this in `ResolveBody`, which is
	 * what puts its feet on the capsule's bottom.
	 *
	 * **THE DESIGN DOES NOT STATE A HEIGHT FOR THIS CREATURE.** It states a body
	 * radius and nothing else, so this comes from the art. See ImpMeshScale for
	 * why the art is worn at the size it was authored at.
	 */
	static constexpr float ImpCapsuleHalfHeight = 87.95f;

	/** How far away it notices somebody, in centimetres. The same 10 metres the
	 *  Brute and the Abyssal Warden use. **No enemy has a designed notice
	 *  radius** and issue #383 is that gap. */
	static constexpr float ImpNoticeRadiusCm = 1000.0f;

	/** How far from where it started it wanders while it has seen nothing. The
	 *  same 6 metres the other three creatures use. */
	static constexpr float ImpRoamRadiusCm = 600.0f;

	// ----------------------------------------------------------------------
	// Where its art lives
	// ----------------------------------------------------------------------

	static const TCHAR* BodyMeshPath;
	static const TCHAR* IdleAnimationPath;
	static const TCHAR* JogAnimationPath;

	/** The folder every clip this creature plays lives in. */
	static const TCHAR* AnimationFolder;

	/** How many claw swipes the pack ships that fit inside the attack interval,
	 *  and how many ways there are to fall over. Both are read by `ResolveBody`
	 *  to build its arrays and by the tests to check it built them. */
	static constexpr int32 RendAnimationCount = 5;
	static constexpr int32 DeathAnimationCount = 5;

	/**
	 * The clips themselves, by asset name.
	 *
	 * WRITTEN OUT RATHER THAN LETTERED IN A LOOP, and that is not only taste.
	 * `FString::Printf` in Unreal 5.8 takes a `TCheckedFormatString`, which has
	 * to be a literal at the call site, so a format string held in a variable
	 * does not compile at all -- error C2664, and the message is about a
	 * conversion rather than about the rule. Naming each clip also means a
	 * reader searching the repository for `Attack_C_SetA` finds it.
	 */
	static const TCHAR* RendAnimationNames[RendAnimationCount];
	static const TCHAR* DeathAnimationNames[DeathAnimationCount];

	/**
	 * The size the mesh is worn at, as a multiple of the size it was authored.
	 *
	 * **ONE, AND THAT IS A DECISION RATHER THAN A DEFAULT.**
	 * `game/docs/enemy-source-assets.md` said the opposite before this creature
	 * was built -- "The Imp's mesh is 1.76 metres tall, roughly a person, so
	 * playing a small swarming creature with it means scaling it down rather
	 * than using it as authored" -- and that note was written from the mesh's
	 * height and its reference-pose bounds, which are an ARM SPAN because the
	 * reference pose has the arms out. Nobody had measured the body.
	 *
	 * MEASURED ON 2026-08-20 BY `tools/probe_imp_animation.py`: the shoulders
	 * are **63.5 cm apart** and the hips 16.7 cm, against the 60 cm across that
	 * this creature's designed 0.30 m body radius gives it. **The mesh at its
	 * authored size already is the width the design specifies**, to within 6%,
	 * so scaling it down would make the creature narrower than its own collision
	 * and leave visible gaps between the bodies in a pack.
	 *
	 * AND SCALING IT DOWN CANNOT BE PAID FOR ANYWAY. A scaled mesh's foot
	 * travels proportionally less far per stride, so the play rate its walk
	 * needs rises by the same factor. At the authored size the walk needs 1.699
	 * against a ceiling of 2.5; at the 0.5117 that would make it 90 cm tall --
	 * the height of the summoned lesser imp's capsule -- it needs **3.32**,
	 * which is beyond the ceiling and would slide. The largest scale the ceiling
	 * permits is 0.68, about 1.20 metres, and that leaves no margin at all.
	 *
	 * SO THE CREATURE IS PERSON-SIZED, and whether a swarm of person-sized imps
	 * reads as a swarm is a judgement only the project owner can make from a
	 * play session. Issue #760.
	 */
	static constexpr float ImpMeshScale = 1.0f;

	/**
	 * How long each `_SetA` claw swipe runs, in seconds.
	 *
	 * MEASURED, NOT CHOSEN. `tools/probe_imp_animation.py` read 0.8000 from all
	 * five on 2026-08-20, and `game/docs/enemy-source-assets.md` records the
	 * same figure. The `_SetB` variants are 0.8333 and the four unsuffixed
	 * attacks are 1.0000, which is longer than the interval between swipes and
	 * is why they are not used.
	 */
	static constexpr float RendAnimationSeconds = 0.8f;

	static_assert(RendAnimationSeconds < DesignedAttackIntervalSeconds,
		"The Imp's claw swipe is longer than the interval between swipes, so one "
		"would still be playing when the next began. Either the clip changed or "
		"the designed interval did; see tools/probe_imp_animation.py and "
		"sim/cataclysm_sim/enemy_stats.py.");

	/**
	 * The ground speed `NonCombat_JogFwd_B` was authored for, in centimetres per
	 * second.
	 *
	 * MEASURED, NOT GUESSED. `tools/measure_animation_stride.py` samples the
	 * planted foot every frame and averages how fast it travels backwards over
	 * the gait cycle. Run 2026-08-20 it reports **382.6 cm/s** on the -Y axis for
	 * this clip, with `NonCombat_Idle` reading 0.0 as the control that shows the
	 * method is on the right axis for this rig.
	 *
	 * **AND THAT RUN IS THE ONE THAT FOUND THE TOOL WAS LYING.** This rig has no
	 * inverse kinematics bones at all -- `Minion_Lane_Core_Skeleton` animates 69
	 * bones and not one of them is an `ik_` bone -- and the tool read the planted
	 * foot through `ik_foot_l` and `ik_foot_r`. A bone the skeleton does not have
	 * returns an identity transform rather than raising, so both walks and the
	 * idle all measured 0.0 cm/s and the walks looked like idles. The tool now
	 * picks the rig from the bones the clip really drives and refuses out loud
	 * when it recognises none.
	 *
	 * **`NonCombat_JogFwd_B` IS THE FASTEST WALK IN THE PACK AND THE ONLY USABLE
	 * ONE.** `Combat_JogFwd` measures 241.1 cm/s, which needs a play rate of
	 * 2.696 -- above the ceiling -- so the combat walk cannot be used by a
	 * creature this fast. `NonCombat_JogFwd` and `_A` are 277.9. That is worth
	 * knowing before somebody wonders why an aggressive creature wears a clip
	 * named for not being in combat.
	 */
	static constexpr float AuthoredJogSpeedCmPerSecond = 382.6f;

	/** Above this the creature is walking, below it is standing. A threshold
	 *  rather than zero, because a character's velocity is rarely exactly zero
	 *  while it settles against the floor. */
	static constexpr float WalkingThresholdCmPerSecond = 10.0f;

	/** Play rate floor and ceiling, the same two figures every other creature
	 *  clamps to. **This creature's walk sits at 1.699**, which is comfortable
	 *  against the Hellhound's 2.478. */
	static constexpr float MinimumPlayRate = 0.2f;
	static constexpr float MaximumPlayRate = 2.5f;

	static_assert(
		DesignedWalkSpeedCmPerSecond
			/ (AuthoredJogSpeedCmPerSecond * ImpMeshScale) <= MaximumPlayRate,
		"The Imp's designed speed now needs a play rate above the ceiling, so "
		"its walk would be clamped and its feet would slide. Either the speed "
		"rose, the clip changed, or the mesh was scaled down -- a smaller mesh "
		"takes a shorter stride and needs a HIGHER rate for the same ground "
		"speed. See ImpMeshScale.");

	/** The play rate the walk needs so its planted foot does not slide. */
	static float JogPlayRate();

	/** The seconds between swipes really in use, which is the console override
	 *  when one is set and the designed figure otherwise. */
	static float AttackIntervalSecondsInUse();

	// ----------------------------------------------------------------------
	// What it is wearing and playing
	// ----------------------------------------------------------------------

	/**
	 * Puts the real mesh and its animations on, if the art pack is installed.
	 *
	 * PUBLIC SO A TEST CAN CALL IT, the same reason the other three creatures'
	 * are public: whether `BeginPlay` runs at all depends on how the world was
	 * built.
	 *
	 * @return whether the mesh was found and worn
	 */
	bool ResolveBody(bool bIncludeAnimation = true);

	/**
	 * Puts the right looping clip on, or leaves a one-shot alone to finish.
	 *
	 * PUBLIC SO A TEST CAN DRIVE IT WITHOUT TICKING A WORLD.
	 */
	void UpdateLoopingAnimation();

	/** Standing and walking, once loaded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> IdleAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> JogAnimation;

	/**
	 * The five claw swipes, one of which is drawn per attack.
	 *
	 * **FIVE RATHER THAN ONE, BECAUSE THIS CREATURE ARRIVES IN TENS.** The
	 * Hellhound has one bite and says so: "one clip, not three... there is
	 * nothing to queue". That reasoning holds for a creature you meet alone.
	 * Ten of these swinging the same 0.8 second clip is the one place in this
	 * project where a single clip is visibly wrong, and the pack ships five that
	 * all fit the interval, so there is nothing to pay for using them.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TArray<TObjectPtr<class UAnimSequence>> RendAnimations;

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
	 * THIS IS WHAT STOPS THE HELD FINAL FRAME. Without it nothing knows a swipe
	 * has ended, so the mesh sits on the last pose until the next one begins.
	 * Zero means nothing is playing.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float OneShotEndsAtSeconds = 0.0f;

	/** Whether an animation Blueprint took the mesh. When one has, nothing here
	 *  sets an animation, because two things driving one component fight. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bAnimationBlueprintBound = false;

	/**
	 * Salt for the stream that draws which claw swipe plays.
	 *
	 * SO IT IS NOT THE STREAM ANYTHING ELSE DRAWS FROM. The base class already
	 * keeps a `DeathDrawSalt` for exactly this reason: two draws seeded from the
	 * same creature at the same moment would agree with each other, which for a
	 * swipe and a death would mean the fifth clip always went with the fifth
	 * way of falling over.
	 */
	static constexpr int32 RendDrawSalt = 0x52454E44;

private:
	/** Plays one clip once and records it. Returns how long it will take, which
	 *  is its length divided by whatever rate it needed. A window of zero means
	 *  play it at its authored speed for as long as it is. */
	float PlayOneShot(class UAnimSequence* Animation, float HoldSeconds = 0.0f);

	void PlayRendAnimation();
};
