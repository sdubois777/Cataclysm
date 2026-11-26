// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"

#include "CataclysmShadowImages.generated.h"

class UPoseableMeshComponent;
class USphereComponent;
struct FCataclysmHitDelivery;
struct FCataclysmSkillUsedNotice;

/** What a shadow of the character is for. */
UENUM()
enum class ECataclysmShadowImageKind : uint8
{
	/** Afterimage's: stands where the movement skill started and repeats the next attack. */
	Afterimage,

	/** Shadow Clone's: fights with the character's basic attack for its seconds. */
	Clone,
};

/**
 * A shadow of the character, made by two of the Shadow's vows (the Death line's finishing slice E, 2026-11-26). It is
 * not a minion: it has no health, no ability system and no brain, nothing can strike or target it, no minion or
 * follower count reaches it, and it reserves nothing. Everything it does is the CHARACTER'S OWN SKILL, started free from
 * its place through `UCataclysmTriggeredSkill::Trigger` (the path a skill gadget's copy takes), so every blow it deals is
 * the character's blow at the character's numbers, and a kill it makes is the character's kill.
 *
 * WHAT IT LOOKS LIKE: a copy of the character's own mesh, posed as the character was posed when it was made (an
 * afterimage) or as the character is posed at each step (a clone, which so moves as its maker moves). Nothing for a
 * character with no mesh, which is every character in an automation test.
 *
 * NO NEW BUTTON (the owner's rule for a class tree): the afterimage repeats what the player pressed, and the clone acts
 * on its own.
 */
UCLASS(NotBlueprintable)
class CATACLYSM_API ACataclysmShadowImage : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmShadowImage();

	/** Whose shadow it is. */
	TWeakObjectPtr<AActor> Maker;

	ECataclysmShadowImageKind Kind = ECataclysmShadowImageKind::Afterimage;

	/** The world time it goes away at. */
	double ExpiresAtSeconds = 0.0;

	/** An afterimage's: the attack it was given to repeat and where, held until the tick after the use. */
	bool bHoldsAnAttack = false;
	FCataclysmWeaponSkill AttackToRepeat;
	FVector AttackAim = FVector::ZeroVector;

	/** A clone's: when it last swung and last stepped, in world seconds; negative for never. */
	double LastSwingAtSeconds = -1.0;
	double LastStepAtSeconds = -1.0;

	/** How many attacks this shadow has started, a clone's swings or an afterimage's repeat. Read by tests. */
	int32 AttacksStarted = 0;

	/** Whether the world time has reached `ExpiresAtSeconds`. */
	bool HasExpired(double NowSeconds) const { return NowSeconds >= ExpiresAtSeconds; }

	/**
	 * One step of a clone at world time `NowSeconds`: gone at its expiry or with its maker gone or dead; otherwise it
	 * takes its maker's pose, picks the nearest living enemy of its maker within `CloneNoticeCm` of itself, walks toward
	 * it at its maker's walking speed while it is beyond the basic attack's reach, and swings the maker's basic attack
	 * at it, free, from its own place, once each of the maker's swing intervals (the first at once). Called by `Tick`,
	 * and by tests with a time of their own. @return whether it started a swing.
	 */
	bool CloneStep(double NowSeconds);

	/**
	 * An afterimage's repeat: the attack it was given, started free for its maker from its own place at the same aim,
	 * and then the afterimage is gone. Made on the tick after the use, because the use is announced inside the used
	 * skill's activation, where no skill can be granted (`UCataclysmTriggeredSkill`). @return whether it started.
	 */
	bool RepeatTheAttack();

	virtual void Tick(float DeltaSeconds) override;

	/** How far a clone looks for an enemy, from where it stands: 15 metres, a summoned imp's notice (a judgement). */
	static constexpr float CloneNoticeCm = 1500.0f;

	/** A clone's walking speed when its maker's cannot be read: 4.4 metres a second, the Shadow's class figure. */
	static constexpr float FallbackWalkCmPerSecond = 440.0f;

	/** The longest step a clone takes in one tick, in seconds of walking, so a hitch does not teleport it. */
	static constexpr float LongestStepSeconds = 0.25f;

	/** Copy the maker's present pose onto the shadow's mesh, when both have one. */
	void TakeTheMakersPose();

protected:
	UPROPERTY()
	TObjectPtr<USphereComponent> Body;

	UPROPERTY()
	TObjectPtr<UPoseableMeshComponent> Look;
};

/**
 * The Shadow's four remaining vow options and its keystone Ever Moving's neighbours in code: Afterimage, Shadow Clone,
 * Shadow Step and Flurry of the Dead (the Death line's finishing slice E, 2026-11-26; issues #2906, #2907, #2909).
 * Ever Moving is read in `UCataclysmDamageCalculation::Resolve` and needs nothing here. `docs/DECISIONS.md` carries
 * each judgement and its sources.
 */
UCLASS()
class CATACLYSM_API UCataclysmShadowImages : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// --------------------------------------------------------------- Afterimage

	/** Afterimage's flag: above nought, a movement skill leaves a shadow that repeats the next attack. Its row: 1. */
	static const TCHAR* AfterimageStat;

	/**
	 * How long an afterimage waits for the next attack, in seconds: 5, as long as Shadow Clone's clone fights. A
	 * judgement; the sentence states no time.
	 */
	static constexpr float AfterimageWaitsSeconds = 5.0f;

	/**
	 * Whether the use `Notice` announces is of the character's movement skill: the key's slot is Movement, or the row
	 * held under that name was made for the Movement slot.
	 */
	static bool IsAMovementSkillUse(const AActor* Character, const FCataclysmSkillUsedNotice& Notice);

	/**
	 * Afterimage's half at a movement skill's paid use: with the flag (asked with the skill's tags), a shadow is left at
	 * `Where`, the place the use started from, for `AfterimageWaitsSeconds`. One at a time: a standing one is replaced.
	 * @return the afterimage, or null without the flag.
	 */
	static ACataclysmShadowImage* LeaveAfterimage(AActor* Character, const FVector& Where,
												  const FGameplayTagContainer& SkillTags);

	/** The character's standing afterimage not yet given an attack, or null. */
	static ACataclysmShadowImage* StandingAfterimageOf(const AActor* Character);

	/**
	 * Whether `Skill` is an attack an afterimage repeats: one a row may cast again
	 * (`UCataclysmTriggeredSkill::RepeatsFromARow`, so no movement skill, self buff, aura, summon, deployable, held or
	 * channelled skill) and not a spell. The basic attack is one. As ruled 2026-10-07, "next attack" is any hit that is
	 * not a spell and not a tick, the basic attack included.
	 */
	static bool IsAnAttack(const FCataclysmWeaponSkill& Skill);

	/**
	 * The other half, at the next paid use of an attack (the basic attack's swing included): the standing afterimage
	 * is given the attack and its aim. The repeat itself is made on the next tick by `MakeAfterimageRepeats`.
	 * @return the afterimage given it, or null when none stands or the use is no attack.
	 */
	static ACataclysmShadowImage* GiveTheAttackToTheAfterimage(AActor* Character, const FCataclysmSkillUsedNotice& Notice,
															   bool bBasicAttack);

	/** Every afterimage of the character holding an attack repeats it. @return how many started. */
	static int32 MakeAfterimageRepeats(const AActor* Character);

	// --------------------------------------------------------------- Shadow Clone

	/** Shadow Clone's two rows: how many Elite-or-stronger kills make a clone (3), and how long it fights (5). */
	static const TCHAR* CloneEveryEliteKillsStat;
	static const TCHAR* CloneSecondsStat;

	/**
	 * A kill credited to `Killer`: an Elite or stronger `Victim` (rarity rung 1 and up) is counted, and every
	 * `CloneEveryEliteKillsStat`-th one leaves a clone beside the killer for `CloneSecondsStat` seconds. Nothing without
	 * both rows. @return the clone made, or null.
	 */
	static ACataclysmShadowImage* NoteKillForShadowClone(AActor* Killer, const AActor* Victim);

	/** How many counted kills the character has toward its next clone. Read by tests. */
	static int32 EliteKillsTowardTheNextClone(const AActor* Killer);

	/** The character's clones standing now. */
	static TArray<ACataclysmShadowImage*> ClonesOf(const AActor* Character);

	// --------------------------------------------------------------- Shadow Step

	/** Shadow Step's row: the metres within which the movement skill may target an enemy. Its row: 15. */
	static const TCHAR* ShadowStepMetresStat;

	/**
	 * How far from the aimed point an enemy may stand and still be the one aimed at, past its own body: 1 metre. A
	 * judgement; the cursor's own pick already answers the enemy under it (`ACataclysmPlayerController`).
	 */
	static constexpr float AimAllowanceCm = 100.0f;

	/**
	 * The enemy Shadow Step takes the movement skill to, or null: with the row (asked with the skill's tags), the
	 * living enemy whose body is nearest `Aim` and within `AimAllowanceCm` of it, standing within the row's metres of
	 * the mover, that the mover can see (nothing on the visibility channel between their middles).
	 */
	static AActor* ShadowStepTarget(const AActor* Mover, const FVector& Aim, const FGameplayTagContainer& SkillTags);

	// --------------------------------------------------------------- Flurry of the Dead

	/** Flurry of the Dead's row: extra strikes a hit makes, 1 for every 10 Fervour held up to 10. */
	static const TCHAR* ExtraStrikesStat;

	/**
	 * Flurry of the Dead's strikes, after the character's own landed hit: the hit is dealt again, whole, as many times
	 * as the row says now (read once, before the first). Each extra strike is marked a row's answer to an event, so it
	 * strikes nothing again itself and is not recorded as a first blow. Stops when the target dies. Called by
	 * `UCataclysmSkillEffects::ApplyHit` for a blow the character struck. @return how many extra strikes were dealt.
	 */
	static int32 StrikeAgain(AActor* Instigator, AActor* Target, float DamagePercent,
							 const FGameplayTagContainer& SkillTags, const FCataclysmHitDelivery& Delivery);

	/** The figure a stat with no attribute reads for `Character`, asked with `Tags`; nought without one. */
	static float StatOf(const AActor* Character, const TCHAR* Stat,
						const FGameplayTagContainer& Tags = FGameplayTagContainer());
};
