// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmBruteCharacter.h"
#include "Cataclysm.h"
#include "Character/CataclysmEnemyController.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/SoftObjectPath.h"

/**
 * Live override for the walk animation's authored speed, for tuning by eye.
 *
 * WHY A CONSOLE VARIABLE AND NOT JUST THE PROPERTY. The property is
 * EditAnywhere, but the Brute is spawned by the game mode rather than placed in
 * the level, so before a session starts there is no instance whose Details panel
 * you could open. What this number needs is to be changed WHILE WATCHING the
 * creature walk, which is what a console variable is for. In a play session:
 *
 *     Cataclysm.Brute.AuthoredWalkSpeed 300
 *
 * and the stride changes on the next frame, with no rebuild and no restart.
 *
 * ZERO MEANS "DO NOT OVERRIDE", so the measured figure on the class stays the
 * answer of record and this is only ever a tuning aid. Setting it back to 0
 * returns to the measured value.
 */
static TAutoConsoleVariable<float> CVarBruteAuthoredWalkSpeed(
	TEXT("Cataclysm.Brute.AuthoredWalkSpeed"),
	0.0f,
	TEXT("Ground speed in cm/s that the Brute's walk animation is treated as "
		 "having been authored for. The walk plays at the Brute's real speed "
		 "divided by this, so a smaller number plays it faster. 0 uses the "
		 "measured value on the class (373.7). For tuning foot sliding by eye "
		 "during a play session."),
	ECVF_Default);

const TCHAR* ACataclysmBruteCharacter::BodyMeshPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Meshes/Rampage.Rampage");

const TCHAR* ACataclysmBruteCharacter::IdleAnimationPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/Idle_Biped.Idle_Biped");

const TCHAR* ACataclysmBruteCharacter::WalkAnimationPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/Jog_Biped_Fwd.Jog_Biped_Fwd");

const TCHAR* ACataclysmBruteCharacter::ChaseAnimationPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/Run_Fwd.Run_Fwd");

/**
 * Live override for the chase animation's authored speed, for tuning by eye.
 *
 * The chase clip's authored speed cannot be measured -- it carries no IK foot
 * track for tools/measure_animation_stride.py to follow -- so this is the only
 * way to arrive at it. Zero means do not override.
 */
static TAutoConsoleVariable<float> CVarBruteAuthoredChaseSpeed(
	TEXT("Cataclysm.Brute.AuthoredChaseSpeed"),
	0.0f,
	TEXT("Ground speed in cm/s that the Brute's chase animation is treated as "
		 "having been authored for. It plays at the Brute's real speed divided "
		 "by this, so a smaller number plays it faster. 0 uses the value on the "
		 "class, which is the Brute's own 250 and therefore a play rate of 1.0."),
	ECVF_Default);

/**
 * Which animation the Brute plays while chasing, by asset path.
 *
 * WHY A PATH AND NOT A NUMBER. Which clip should represent running is an open
 * question, not a tuning value: Sprint_Biped_Fwd turned out to be the same
 * animation as the walk (issue #386), and the four-legged gaits are a real
 * alternative that changes the creature's whole posture. Auditioning those
 * means loading a different asset, and doing it from the console means doing it
 * in one session instead of one rebuild each.
 *
 * Empty means use ChaseAnimationPath on the class.
 */
static TAutoConsoleVariable<FString> CVarBruteChaseAnimation(
	TEXT("Cataclysm.Brute.ChaseAnimation"),
	TEXT(""),
	TEXT("Asset path of the animation the Brute plays while chasing. Empty uses "
		 "the class default, Run_Fwd. Try Jog_Quad_Fwd or Sprint_Quad_Fwd for "
		 "the four-legged gaits."),
	ECVF_Default);

ACataclysmBruteCharacter::ACataclysmBruteCharacter()
{
	// TICKS, UNLIKE EVERY OTHER CHARACTER IN THIS PROJECT. Choosing between the
	// standing and walking animation and setting the walk's play rate is a
	// per-frame job. The tick does nothing else; the brain still runs on the
	// controller's own quarter-second timer.
	PrimaryActorTick.bCanEverTick = true;

	// The designed numbers, overriding the base enemy's judgement figures. Each
	// one is cited on its declaration in the header.
	MeleeReachCm = DesignedMeleeReachCm;
	AttackIntervalSeconds = DesignedAttackIntervalSeconds;

	// SEVEN METRES, NOT THE BASE'S FIFTEEN. The header derives it: the distance
	// this enemy covers in one attack cycle, 250 cm/s x 2.8 s. The base's 1500
	// is the longest range a designed player skill reaches, which is a sound
	// rule for a caster and a poor one for a melee enemy slower than the player,
	// because it starts a chase that can never end. Issue #383 asks for the rule
	// covering all seven; this changes only the Brute.
	NoticeRadiusCm = BruteNoticeRadiusCm;

	GetCapsuleComponent()->InitCapsuleSize(BruteCapsuleRadius, BruteCapsuleHalfHeight);

	// SLOW, AND SLOW TO TURN. These two are the whole of "heavily armored slow
	// melee. Can be outmaneuvered". Without them a Brute is a training dummy
	// with more health.
	GetCharacterMovement()->MaxWalkSpeed = DesignedWalkSpeedCmPerSecond;
	GetCharacterMovement()->RotationRate =
		FRotator(0.0f, DesignedTurnRateDegreesPerSecond, 0.0f);
}

void ACataclysmBruteCharacter::BeginPlay()
{
	Super::BeginPlay();

	ResolveBody(/*bIncludeAnimation=*/true);
}

void ACataclysmBruteCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	DriveLocomotion();
}

float ACataclysmBruteCharacter::EffectiveAuthoredWalkSpeed() const
{
	const float Override = CVarBruteAuthoredWalkSpeed.GetValueOnAnyThread();
	return Override > 0.0f ? Override : AuthoredWalkSpeedCmPerSecond;
}

float ACataclysmBruteCharacter::EffectiveAuthoredChaseSpeed() const
{
	const float Override = CVarBruteAuthoredChaseSpeed.GetValueOnAnyThread();
	return Override > 0.0f ? Override : AuthoredChaseSpeedCmPerSecond;
}

bool ACataclysmBruteCharacter::IsChasing() const
{
	// ASKED OF THE BRAIN RATHER THAN INFERRED FROM SPEED, because the Brute
	// moves at the same 250 cm/s whether it is wandering or coming at you, so
	// speed cannot tell the two apart. Attacking deliberately does not count:
	// it has stopped moving by then, so the standing animation is right.
	const ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(GetController());
	return Brain && Brain->LastAction == ECataclysmBrainAction::Chasing;
}

UAnimSequence* ACataclysmBruteCharacter::AnimationForGroundSpeed(
	float GroundSpeedCmPerSecond, float& OutPlayRate, bool bChasing) const
{
	const bool bWalking = GroundSpeedCmPerSecond > WalkingThresholdCmPerSecond;

	if (!bWalking)
	{
		OutPlayRate = 1.0f;
		return IdleAnimation;
	}

	// THE CHASE CLIP ONLY WHEN THERE IS ONE. It is loaded from a gitignored
	// pack like everything else here, so a fresh clone has no chase animation
	// and falls back to the walk rather than to nothing.
	const bool bRunning = bChasing && ChaseAnimation != nullptr;

	// FEET MATCHED TO GROUND SPEED. The animation was authored for a character
	// moving at some particular speed; the Brute moves at 250. Playing it at 1.0
	// when those differ makes the feet slide, which is the "walking slower than
	// it is moving" fault this exists to avoid. Clamped because a play rate near
	// zero freezes the pose and a very high one is a blur.
	const float AuthoredSpeed = bRunning
		? EffectiveAuthoredChaseSpeed() : EffectiveAuthoredWalkSpeed();

	OutPlayRate = FMath::Clamp(
		GroundSpeedCmPerSecond / FMath::Max(AuthoredSpeed, 1.0f),
		MinimumPlayRate, MaximumPlayRate);

	return bRunning ? ChaseAnimation : WalkAnimation;
}

void ACataclysmBruteCharacter::DriveLocomotion()
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent || !MeshComponent->GetSkeletalMeshAsset())
	{
		return;
	}

	// AUDITIONING A DIFFERENT CHASE CLIP WITHOUT A REBUILD. Checked here rather
	// than only in ResolveBody so that changing the console variable takes
	// effect on the next frame, which is the whole point of it. A string
	// compare per frame on one enemy is not worth avoiding.
	const FString WantedChasePath = CVarBruteChaseAnimation.GetValueOnAnyThread();
	if (!WantedChasePath.IsEmpty() && WantedChasePath != LoadedChaseAnimationPath)
	{
		if (UAnimSequence* Swapped =
				Cast<UAnimSequence>(FSoftObjectPath(WantedChasePath).TryLoad()))
		{
			ChaseAnimation = Swapped;
			LoadedChaseAnimationPath = WantedChasePath;
		}
		else
		{
			// REMEMBERED EVEN THOUGH IT FAILED, or a mistyped path retries the
			// load every frame for the rest of the session.
			LoadedChaseAnimationPath = WantedChasePath;
			UE_LOG(LogCataclysm, Warning,
				TEXT("Cataclysm.Brute.ChaseAnimation is set to %s, which did not "
					 "load. Keeping the previous chase animation."),
				*WantedChasePath);
		}
	}

	// HORIZONTAL ONLY. Falling is not walking, and a Brute stepping off a ledge
	// should not break into a jog on the way down.
	float PlayRate = 1.0f;
	UAnimSequence* Wanted = AnimationForGroundSpeed(
		GetVelocity().Size2D(), PlayRate, IsChasing());
	if (!Wanted)
	{
		return;
	}

	// THROUGH THE SINGLE NODE INSTANCE, NOT THE COMPONENT'S AnimationData.
	// AnimationData is the editor-facing default; in a world that never ran
	// InitAnim it does not follow what is actually playing, so comparing against
	// it decided "not the one I want" every frame and restarted the animation
	// every frame -- which holds it on its first pose and looks exactly like the
	// standing-still fault this function exists to fix. Caught by
	// Cataclysm.Brute.AnimatesInsteadOfSliding on 2026-08-07.
	UAnimSingleNodeInstance* Single = MeshComponent->GetSingleNodeInstance();
	if (!Single)
	{
		return;
	}

	// ONLY ON CHANGE, for the reason above.
	if (Single->GetAnimationAsset() != Wanted)
	{
		Single->SetAnimationAsset(Wanted, /*bIsLooping=*/true);
		Single->SetPlaying(true);
	}

	Single->SetPlayRate(PlayRate);
}

bool ACataclysmBruteCharacter::ResolveBody(bool bIncludeAnimation)
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return false;
	}

	USkeletalMesh* Body = Cast<USkeletalMesh>(
		FSoftObjectPath(BodyMeshPath).TryLoad());

	if (!Body)
	{
		// NOT AN ERROR, AND SAID OUT LOUD RATHER THAN LEFT TO BE NOTICED. The
		// Paragon packs are gitignored, so this is the expected state on a fresh
		// clone and in continuous integration. The placeholder cylinder the base
		// class made stays visible, so the Brute is still there to fight -- it
		// just looks like every other enemy.
		UE_LOG(LogCataclysm, Warning,
			TEXT("Brute art not found at %s, so it is keeping the placeholder "
				 "cylinder. This is expected without the Paragon Rampage pack; "
				 "see game/docs/enemy-source-assets.md."),
			BodyMeshPath);
		return false;
	}

	MeshComponent->SetSkeletalMesh(Body);

	// FEET ON THE CAPSULE BOTTOM. A skeletal mesh is authored with its origin at
	// the feet, and the capsule's origin is its centre, so the mesh drops by the
	// half-height. The yaw is the engine's convention for character meshes,
	// which face -Y while the actor faces +X.
	MeshComponent->SetRelativeLocationAndRotation(
		FVector(0.0f, 0.0f, -BruteCapsuleHalfHeight),
		FRotator(0.0f, -90.0f, 0.0f));

	if (bIncludeAnimation)
	{
		IdleAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(IdleAnimationPath).TryLoad());
		WalkAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(WalkAnimationPath).TryLoad());

		ChaseAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(ChaseAnimationPath).TryLoad());
		LoadedChaseAnimationPath = ChaseAnimationPath;

		if (IdleAnimation)
		{
			// SINGLE NODE, NOT AN ANIMATION BLUEPRINT. See the header for the
			// measurement behind this. Setting the mode explicitly matters
			// because PlayAnimation on a component still in AnimationBlueprint
			// mode is ignored.
			MeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			MeshComponent->PlayAnimation(IdleAnimation, /*bLooping=*/true);
		}
		else
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Brute standing animation not found at %s, so it will hold "
					 "its reference pose."),
				IdleAnimationPath);
		}

		if (!WalkAnimation)
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Brute walking animation not found at %s, so it will slide "
					 "rather than walk."),
				WalkAnimationPath);
		}

		if (!ChaseAnimation)
		{
			// NOT A FAULT WORTH MORE THAN A LINE. Without it the Brute walks
			// while chasing, which is what it did before there was a chase
			// animation at all.
			UE_LOG(LogCataclysm, Warning,
				TEXT("Brute chase animation not found at %s, so it will keep "
					 "walking while it chases."),
				ChaseAnimationPath);
		}
	}

	// OTHERWISE THE CYLINDER SITS INSIDE THE DEMON. The base class creates
	// PlaceholderBody in its constructor and nothing about assigning a skeletal
	// mesh removes it.
	if (PlaceholderBody)
	{
		PlaceholderBody->SetVisibility(false);
	}

	UE_LOG(LogCataclysm, Verbose,
		TEXT("Brute is wearing %s."), BodyMeshPath);

	return true;
}
