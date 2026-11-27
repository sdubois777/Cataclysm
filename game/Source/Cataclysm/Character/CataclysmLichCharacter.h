// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "CataclysmLichCharacter.generated.h"

/**
 * The Lich: Death's slow skeletal caster, which stays behind a fight and raises what the player just killed.
 *
 * THE OWNER, 2026-11-26, approving the design: "I'm happy with those enemy designs." It escorts one group in four in
 * Death dungeons in place of the Succubus (`FCataclysmFloorPopulator::EscortFor`). The Succubus makes allies stronger;
 * the Lich makes kills come back. The counter is to interrupt the raise, or to kill the Lich before its next cooldown.
 *
 * THREE ABILITIES, the cap a basic creature may have (the ruling of 2026-08-07), in priority order:
 * - **Raise the Fallen** (Special, Summon, 10 s): up to three of the most recent deaths within 10 m of it come back as
 *   Commons of their own kind at half health, worth nothing, where they fell
 *   (`ACataclysmDungeonGameMode::RaiseTheFallenNear`). It is chosen only when such a death is near: otherwise its range
 *   is nought and the brain never picks it. No marker; interrupting the cast cancels it.
 * - **Grave Burst** (Heavy, Strike, 8 s): a 3 m ring around it after a 1.26 s circle, at 250%, when the player comes
 *   within 3 m. Its answer to being reached, as the mortar is the Sentinel's.
 * - **Grave Bolt** (Basic, Projectile): a quick cast every second, as the Succubus's Soulfire is.
 *
 * ITS ART is the owner's Lich pack in the main checkout (`/Game/Lich`), gitignored as the Paragon packs are, so a
 * worktree keeps the placeholder cylinder and its art tests report a skipped half. **The five attack clips are named
 * only `Attack1` to `Attack5`.** Their lengths, root motion (off in every clip) and notifies (none) were read from the
 * .uasset files; which is a cast and which a strike, the frame each releases on, and whether the bones move the body
 * were NOT confirmed. Each release time is one named constant below, so a correction is one line.
 */
UCLASS()
class CATACLYSM_API ACataclysmLichCharacter : public ACataclysmEnemyCharacter
{
	GENERATED_BODY()

public:
	ACataclysmLichCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void AttackTarget(AActor* Target) override;
	virtual bool IsPlayingAOneShot() const override;
	virtual float PlayFlinchClip(class UAnimSequence* Clip) override;
	virtual float DesignedSecondsBetweenAttacks() const override;
	virtual TArray<FCataclysmEnemyAbility> EnemyAbilities() const override;
	virtual void UseEnemyAbility(int32 Index, AActor* Target, const FVector& AimedAt) override;
	virtual void BeginEnemyAbilityWindUp(int32 Index, AActor* Target) override;

	/** ORDER IS PRIORITY: the raise first, the burst second, the bolt, which has no cooldown, last. */
	enum : int32 { RaiseTheFallenAbility = 0, GraveBurstAbility = 1, GraveBoltAbility = 2 };

	// ---- Where it fights --------------------------------------------------------------------------------------------

	/** It holds at its bolt's range, as the Succubus holds at Soulfire's. */
	virtual float AttackReachCm() const override { return GraveBoltRangeCm; }
	virtual bool AttackReachIsAContactReach() const override { return false; }
	virtual float SightRadiusCm() const override { return LichNoticeRadiusCm; }
	virtual float RoamRadiusCm() const override { return LichRoamRadiusCm; }

	// ---- Its figures, from the approved design; the archetype row `Lich` sets its health, damage and armour ----------

	static constexpr float DesignedAttackIntervalSeconds = 1.0f;
	static constexpr float DesignedResistancePercent = 25.0f;
	static constexpr float DesignedCritChancePercent = 10.0f;
	static constexpr float DesignedCritMultiplierPercent = 200.0f;
	/** None, a judgement: the design states none, and a slow skeleton in robes has no reason to dodge. */
	static constexpr float DesignedEvasionPercent = 0.0f;
	/** 30% of its health, from PoE 2's Desiccated Lich. */
	static constexpr float DesignedEnergyShieldFraction = 0.30f;
	static constexpr float DesignedWalkSpeedCmPerSecond = 300.0f;
	static constexpr float DesignedTurnRateDegreesPerSecond = 480.0f;

	/** The Succubus's capsule, a judgement: the Lich mesh's height was not measured. */
	static constexpr float LichCapsuleRadius = 48.0f;
	static constexpr float LichCapsuleHalfHeight = 90.4f;
	static constexpr float LichNoticeRadiusCm = 1000.0f;
	static constexpr float LichRoamRadiusCm = 600.0f;

	// ---- Grave Bolt ----------------------------------------------------------------------------------------------------

	static constexpr float GraveBoltRangeCm = 800.0f;
	static constexpr float GraveBoltRadiusCm = 50.0f;
	static constexpr float GraveBoltSpeedCmPerSecond = 1200.0f;
	static constexpr float GraveBoltWindUpSeconds = 0.5f;
	static_assert(GraveBoltWindUpSeconds == FCataclysmEnemyAbility::QuickCastSeconds,
				  "Grave Bolt is a quick cast and its wind-up is not the quick cast's.");
	static constexpr float GraveBoltDamagePercent = 100.0f;
	static const TCHAR* GraveBoltTags;

	// ---- Grave Burst ---------------------------------------------------------------------------------------------------

	static constexpr float GraveBurstRadiusCm = 300.0f;
	static constexpr float GraveBurstCooldownSeconds = 8.0f;
	/** 0.4 + 3.0 / 3.5, from the wind-up rule. */
	static constexpr float GraveBurstWindUpSeconds = 1.2571f;
	static constexpr float GraveBurstDamagePercent = 250.0f;
	static const TCHAR* GraveBurstTags;

	// ---- Raise the Fallen ----------------------------------------------------------------------------------------------

	/** The deaths it can raise lie within this of it. */
	static constexpr float RaiseRadiusCm = 1000.0f;
	static constexpr int32 RaiseMostAtOnce = 3;
	/** PoE 2's Desiccated Lich's revive. */
	static constexpr float RaiseCooldownSeconds = 10.0f;
	/** A raised creature's health, as a share of its maximum: the owner's ruling 3, Haunting's terms. */
	static constexpr float RaisedHealthShare = 0.5f;

	// ---- Its clips, and when each releases (UNCONFIRMED: read the class comment) ---------------------------------------

	static const TCHAR* BodyMeshPath;
	static const TCHAR* AnimationFolder;
	static const TCHAR* IdleAnimationName;
	static const TCHAR* JogAnimationName;
	static const TCHAR* GraveBoltAnimationName;
	static const TCHAR* GraveBurstAnimationName;
	static const TCHAR* RaiseAnimationName;
	static constexpr int32 DeathAnimationCount = 1;
	static const TCHAR* DeathAnimationNames[DeathAnimationCount];

	/** The seconds into `Attack4` (1.000 s) at which Grave Bolt is taken to leave. A judgement: half the clip. */
	static constexpr float GraveBoltReleaseSeconds = 0.5f;
	/** The seconds into `Attack2` (2.467 s) at which Grave Burst is taken to land. A judgement: half the clip. */
	static constexpr float GraveBurstReleaseSeconds = 1.2f;
	/** The seconds into `Attack1` (3.933 s) at which Raise the Fallen is taken to raise: the design's J4, 2.0 s. */
	static constexpr float RaiseReleaseSeconds = 2.0f;
	static constexpr float RaiseWindUpSeconds = RaiseReleaseSeconds;

	/** WalkingF's authored speed was not measured; at 1.0 the clip plays as authored. */
	static constexpr float AuthoredJogSpeedCmPerSecond = 0.0f;
	static constexpr float MinimumPlayRate = 0.2f;
	static constexpr float MaximumPlayRate = 2.5f;

	/** Loads the mesh and the clips; false, keeping the placeholder cylinder, when the pack is not installed. */
	bool ResolveBody(bool bIncludeAnimation = true);

	/** The bolt last fired, the creatures the last raise brought back, and how many there were. For the tests. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class ACataclysmProjectile> LastShotFired;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	int32 LastRaised = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	int32 LastBurstCaught = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> IdleAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> JogAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> GraveBoltAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> GraveBurstAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> RaiseAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> CurrentLoopingAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> LastPlayedAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float OneShotEndsAtSeconds = 0.0f;

	/** The ability whose clip waits to start, so its release lines up with the wind-up's end; -1 for none. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	int32 PendingWindUpAbility = -1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float WindUpBeganAtSeconds = 0.0f;

	/** The clip, release time and wind-up of an ability; null clip for none. */
	class UAnimSequence* ClipFor(int32 Index, float& OutReleaseSeconds, float& OutWindUpSeconds) const;

private:
	void UpdateLoopingAnimation();
	void StartPendingWindUpClip();
	float PlayOneShot(class UAnimSequence* Animation);
	float PlayOneShotAtRate(class UAnimSequence* Animation, float Rate);

	/** Whether a death it could raise lies near it now. */
	bool HasFallenToRaise() const;
};
