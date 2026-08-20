// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmImpCharacter.h"
#include "Cataclysm.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/SoftObjectPath.h"

// THE MELEE LANE MINION. The Corrupted Sentinel is played by the SIEGE lane
// minion out of the same pack, and they are separate meshes on separate
// skeletons, so nothing here is shared with that creature but the folder.
const TCHAR* ACataclysmImpCharacter::BodyMeshPath =
	TEXT("/Game/ParagonMinions/Characters/Minions/Down_Minions/"
		 "Meshes/Minion_Lane_Melee_Dawn.Minion_Lane_Melee_Dawn");

// `NonCombat_Idle`, AND IT IS THE ONLY IDLE THE PACK HAS. There is no combat
// idle to prefer, so this is not a choice between two clips.
const TCHAR* ACataclysmImpCharacter::IdleAnimationPath =
	TEXT("/Game/ParagonMinions/Characters/Minions/Down_Minions/"
		 "Animations/Melee/NonCombat_Idle.NonCombat_Idle");

// `NonCombat_JogFwd_B`, WHICH IS THE FASTEST WALK IN THE PACK AND THE ONLY
// USABLE ONE. Measured 2026-08-20 by tools/measure_animation_stride.py:
//
//     NonCombat_JogFwd_B    382.6 cm/s    play rate 1.699   used
//     NonCombat_JogFwd      277.9 cm/s    play rate 2.339
//     NonCombat_JogFwd_A    277.9 cm/s    play rate 2.339
//     Combat_JogFwd         241.1 cm/s    play rate 2.696   ABOVE THE CEILING
//
// So the combat walk cannot be worn by a creature that moves at 650 cm/s, and
// this creature wears a clip named for not being in combat. See the header.
const TCHAR* ACataclysmImpCharacter::JogAnimationPath =
	TEXT("/Game/ParagonMinions/Characters/Minions/Down_Minions/"
		 "Animations/Melee/NonCombat_JogFwd_B.NonCombat_JogFwd_B");

const TCHAR* ACataclysmImpCharacter::AnimationFolder =
	TEXT("/Game/ParagonMinions/Characters/Minions/Down_Minions/"
		 "Animations/Melee");

// ALL FIVE 0.8000 SECONDS against an 0.9 second interval. The `_SetB` variants
// are 0.8333 and would also fit; the four unsuffixed attacks are 1.0000 and
// would not, which is why these are one set rather than a mixture.
const TCHAR* ACataclysmImpCharacter::RendAnimationNames[RendAnimationCount] = {
	TEXT("Attack_A_SetA"),
	TEXT("Attack_B_SetA"),
	TEXT("Attack_C_SetA"),
	TEXT("Attack_D_SetA"),
	TEXT("Attack_E_SetA"),
};

// FIVE DEATHS, WHICH IS MORE THAN ANY OTHER CREATURE IN THIS PROJECT HAS. The
// Brute has one, the Abyssal Warden and the Hellhound two. That matters more
// for this creature than for any of them, because ten of it die at once.
// Measured 2.1333, 0.5000, 0.6000, 0.5091 and 0.6389 seconds, all inside
// UCataclysmEnemyDeath::LongestCorpseSeconds.
const TCHAR* ACataclysmImpCharacter::DeathAnimationNames[DeathAnimationCount] = {
	TEXT("Death_A"),
	TEXT("Death_B"),
	TEXT("Death_C"),
	TEXT("Death_D"),
	TEXT("Death_E"),
};

// WHERE THE CLIP PATH HELPER WENT. It was an identical copy in an anonymous
// namespace here and in CataclysmCorruptedSentinelCharacter.cpp, which is the
// ordinary way to keep a helper private to one file -- and it broke the build
// the moment both landed in the same unity blob, because Unreal merges a
// module's .cpp files into one translation unit. It is now
// ACataclysmEnemyCharacter::ClipPathIn, whose header comment records the whole
// incident.

/**
 * Seconds between claw swipes, for tuning one while playing.
 *
 * THE SAME SHAPE THE OTHER CREATURES' OVERRIDES HAVE, and for the same reason:
 * a figure the design left to play cannot be tuned by rebuilding.
 */
static TAutoConsoleVariable<float> CVarImpAttackInterval(
	TEXT("Cataclysm.Imp.AttackInterval"),
	0.0f,
	TEXT("Seconds between the Imp's claw swipes. 0 uses its designed 0.9. Do "
		 "NOT go below 0.8: Attack_A_SetA is 0.8000 s and is not rate-scaled, "
		 "so a shorter interval starts a swipe that has not finished. Remember "
		 "that a pack is ten, so this figure is multiplied by ten."),
	ECVF_Default);

ACataclysmImpCharacter::ACataclysmImpCharacter()
{
	// UpdateLoopingAnimation RUNS FROM Tick and is what returns the mesh to a
	// resting pose after a swipe and what puts the walk on while it moves.
	PrimaryActorTick.bCanEverTick = true;

	// WHICH ROW OF game/Data/EnemyArchetypes.csv THIS CREATURE IS. It is what
	// lets the panel that describes the creature under the cursor call it an
	// Imp. Nothing reads its stats out of that row yet.
	ArchetypeRow = TEXT("Imp");

	MeleeReachCm = DesignedMeleeReachCm;
	AttackIntervalSeconds = DesignedAttackIntervalSeconds;
	ResistancePercent = DesignedResistancePercent;
	CritChancePercent = DesignedCritChancePercent;
	CritMultiplierPercent = DesignedCritMultiplierPercent;
	EvasionPercent = DesignedEvasionPercent;
	EnergyShieldFraction = DesignedEnergyShieldFraction;
	NoticeRadiusCm = ImpNoticeRadiusCm;

	GetCapsuleComponent()->InitCapsuleSize(ImpCapsuleRadius,
										   ImpCapsuleHalfHeight);

	// NEITHER OF THESE IS SET BY ACataclysmEnemyCharacter. An enemy that does
	// not set MaxWalkSpeed here moves at Unreal's default 600 cm/s, which for
	// this creature would be a silent SLOWING, since it is designed at 650, and
	// would put it back inside the speed a player can walk away from.
	GetCharacterMovement()->MaxWalkSpeed = DesignedWalkSpeedCmPerSecond;
	GetCharacterMovement()->RotationRate =
		FRotator(0.0f, DesignedTurnRateDegreesPerSecond, 0.0f);
}

void ACataclysmImpCharacter::BeginPlay()
{
	Super::BeginPlay();
	ResolveBody(/*bIncludeAnimation=*/true);

	// STANDING RATHER THAN THE REFERENCE POSE, from the first frame. Without
	// this the creature holds its bind pose until it first moves or swings.
	UpdateLoopingAnimation();
}

void ACataclysmImpCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateLoopingAnimation();
}

float ACataclysmImpCharacter::AttackIntervalSecondsInUse()
{
	const float Override = CVarImpAttackInterval.GetValueOnAnyThread();
	return Override > 0.0f ? Override : DesignedAttackIntervalSeconds;
}

float ACataclysmImpCharacter::SecondsBetweenAttacks() const
{
	return AttackIntervalSecondsInUse();
}

void ACataclysmImpCharacter::AttackTarget(AActor* Target)
{
	Super::AttackTarget(Target);
	PlayRendAnimation();
}

void ACataclysmImpCharacter::PlayRendAnimation()
{
	if (RendAnimations.IsEmpty())
	{
		return;
	}

	// ITS OWN STREAM, seeded from this creature and the moment it swung, so ten
	// Imps swinging in the same frame do not all draw the same clip. Salted with
	// RendDrawSalt so it is not the stream the death draw or the drop roll uses:
	// two draws seeded identically agree with each other, which would tie a
	// creature's last swipe to the way it falls over.
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	FRandomStream Stream(GetUniqueID()
		^ static_cast<int32>(Now * 1000.0f) ^ RendDrawSalt);

	const int32 Index = Stream.RandRange(0, RendAnimations.Num() - 1);
	if (!RendAnimations.IsValidIndex(Index))
	{
		return;
	}

	// ONE CLIP DRAWN FROM FIVE, NOT A SEQUENCE. The Abyssal Warden's basic
	// attack is a left swing, a right swing and a recovery played in order,
	// because its swing clips end 151 cm away from its idle. These five are
	// each a whole swipe that returns where it started, so any of them can
	// follow any other.
	PlayOneShot(RendAnimations[Index].Get(), AttackIntervalSecondsInUse());
}

float ACataclysmImpCharacter::JogPlayRate()
{
	// THE CLIP'S AUTHORED SPEED AT THE SIZE THE MESH IS WORN. A mesh scaled to
	// half its authored size takes half the stride, so its planted foot travels
	// backwards at half the speed and the clip needs twice the rate to keep up
	// with the same body. That factor is one today and the arithmetic is written
	// out anyway, because ImpMeshScale is the number issue #760 may move.
	const float AuthoredAtThisSize =
		AuthoredJogSpeedCmPerSecond * ImpMeshScale;
	if (AuthoredAtThisSize <= 0.0f)
	{
		return 1.0f;
	}

	// THE RATIO OF WHAT IT MOVES AT TO WHAT THE CLIP CARRIES IT AT. A planted
	// foot travels backwards at the clip's authored speed; the body travels
	// forwards at the designed speed; playing at the ratio makes the two cancel
	// and the foot stay put.
	//
	// FOR THIS CREATURE THAT IS 650 / 382.6 = 1.699, which is comfortable
	// against the Hellhound's 2.478.
	return FMath::Clamp(DesignedWalkSpeedCmPerSecond / AuthoredAtThisSize,
						MinimumPlayRate, MaximumPlayRate);
}

void ACataclysmImpCharacter::UpdateLoopingAnimation()
{
	// THE GRAPH OWNS THE MESH WHEN THERE IS ONE. Two things setting the same
	// component's animation would fight, and the graph is the better of the two
	// because it can blend. This whole function is the fallback for not having
	// one.
	if (bAnimationBlueprintBound)
	{
		return;
	}

	const UWorld* World = GetWorld();
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!World || !MeshComponent)
	{
		return;
	}

	// A ONE-SHOT KEEPS THE MESH UNTIL IT ENDS. Its end is recorded rather than
	// asked of the component, because a single-node instance reports its own
	// position and not whether the caller considers it finished.
	if (World->GetTimeSeconds() < OneShotEndsAtSeconds)
	{
		return;
	}

	const bool bWalking = GetVelocity().Size2D() > WalkingThresholdCmPerSecond;
	UAnimSequence* Wanted = bWalking ? JogAnimation.Get() : IdleAnimation.Get();

	if (!Wanted)
	{
		return;
	}

	// ONLY ON A CHANGE. PlayAnimation restarts the clip from the beginning, so
	// calling it every frame would freeze the creature on the first pose of
	// whichever loop it is in.
	if (Wanted == CurrentLoopingAnimation)
	{
		return;
	}

	CurrentLoopingAnimation = Wanted;
	MeshComponent->PlayAnimation(Wanted, /*bLooping=*/true);
	MeshComponent->SetPlayRate(bWalking ? JogPlayRate() : 1.0f);
}

float ACataclysmImpCharacter::PlayOneShot(UAnimSequence* Animation,
										  float HoldSeconds)
{
	LastPlayedAnimation = Animation;

	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!Animation || !MeshComponent)
	{
		return 0.0f;
	}

	const float Length = Animation->GetPlayLength();
	if (Length <= 0.0f)
	{
		return 0.0f;
	}

	// NEVER SLOWER THAN AUTHORED, ONLY FASTER, AND ONLY WHEN IT MUST BE. The
	// same rule every other creature here uses, for the reason recorded there:
	// stretching a short clip across a long window was tried and read as slow
	// motion. A clip shorter than its window holds its last pose instead.
	//
	// NOTHING HERE NEEDS IT TODAY. A swipe is 0.8 seconds inside an 0.9 second
	// interval, so the rate is 1. This exists so that replacing a clip cannot
	// silently start a movement the creature does not finish.
	const float Hold = HoldSeconds > 0.0f ? HoldSeconds : Length;
	const float Rate = FMath::Clamp(FMath::Max(1.0f, Length / Hold),
									MinimumPlayRate, MaximumPlayRate);

	// NO ANIMATION BLUEPRINT EXISTS FOR THIS CREATURE, so this always takes the
	// single-node path today.
	MeshComponent->PlayAnimation(Animation, /*bLooping=*/false);
	MeshComponent->SetPlayRate(Rate);

	// AND THE MESH IS OWED BACK WHEN IT FINISHES. A one-shot in single-node mode
	// plays once and then HOLDS ITS LAST FRAME forever.
	if (const UWorld* World = GetWorld())
	{
		OneShotEndsAtSeconds = World->GetTimeSeconds() + Length / Rate;
	}

	// CLEARED SO THE LOOP RESTARTS AFTERWARDS. UpdateLoopingAnimation only acts
	// on a change, and without this the creature would still be "already
	// playing" the loop the swipe interrupted, so nothing would put it back.
	CurrentLoopingAnimation = nullptr;

	return Length / Rate;
}

bool ACataclysmImpCharacter::ResolveBody(bool bIncludeAnimation)
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return false;
	}

	USkeletalMesh* Body =
		Cast<USkeletalMesh>(FSoftObjectPath(BodyMeshPath).TryLoad());
	if (!Body)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("Imp art not found at %s, so it is keeping the placeholder "
				 "cylinder. This is expected without the Paragon Minions pack; "
				 "see game/docs/enemy-source-assets.md."),
			BodyMeshPath);
		return false;
	}

	MeshComponent->SetSkeletalMesh(Body);

	// AT THE SIZE IT WAS AUTHORED, WHICH IS A DECISION. See ImpMeshScale in the
	// header: the mesh's shoulders measure 63.5 cm apart against the 60 cm this
	// creature's designed body radius gives it, so it is already the width the
	// design asks for. Set here rather than left alone so that changing it is
	// one line and so that a reader can see a choice was made.
	MeshComponent->SetRelativeScale3D(FVector(ImpMeshScale));

	// FEET ON THE CAPSULE BOTTOM, AND THE ENGINE'S YAW. A skeletal mesh is
	// authored with its origin at the feet and the capsule's origin is its
	// centre, so the mesh drops by the half-height. The -90 degree yaw is the
	// engine's convention for character meshes, which face -Y while the actor
	// faces +X -- and the stride measurement found this rig's forward axis to be
	// -Y, which is what says the convention holds for it.
	//
	// THE DROP IS SCALED WITH THE MESH. A mesh at half size stands half as tall,
	// so dropping it by the full half-height would bury it. The factor is one
	// today; it is written out because ImpMeshScale is what issue #760 may move.
	MeshComponent->SetRelativeLocationAndRotation(
		FVector(0.0f, 0.0f, -ImpCapsuleHalfHeight * ImpMeshScale),
		FRotator(0.0f, -90.0f, 0.0f));

	if (bIncludeAnimation)
	{
		IdleAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(IdleAnimationPath).TryLoad());
		JogAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(JogAnimationPath).TryLoad());

		// FIVE SWIPES AND FIVE DEATHS, BOTH LETTERED A TO E. A null entry is
		// KEPT in the death array rather than skipped, for the reason
		// ACataclysmEnemyCharacter::PlayDeathAnimation gives: dropping one would
		// change how many clips there are and therefore which one is drawn.
		RendAnimations.Reset();
		DeathAnimations.Reset();
		for (const TCHAR* Name : RendAnimationNames)
		{
			RendAnimations.Add(Cast<UAnimSequence>(
				FSoftObjectPath(ClipPathIn(AnimationFolder, Name)).TryLoad()));
		}
		for (const TCHAR* Name : DeathAnimationNames)
		{
			DeathAnimations.Add(Cast<UAnimSequence>(
				FSoftObjectPath(ClipPathIn(AnimationFolder, Name)).TryLoad()));
		}

		// COUNTED RATHER THAN CHECKED ONE BY ONE, because there are ten of them
		// and naming each in a warning would be a wall of text nobody reads.
		int32 SwipesFound = 0;
		for (const TObjectPtr<UAnimSequence>& Clip : RendAnimations)
		{
			SwipesFound += Clip ? 1 : 0;
		}

		if (!IdleAnimation || !JogAnimation || SwipesFound == 0)
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Imp animations missing: idle %s, walk %s, %d of %d claw "
					 "swipes. It will fight with nothing to show for it. This "
					 "is expected without the Paragon Minions pack."),
				IdleAnimation ? TEXT("found") : TEXT("MISSING"),
				JogAnimation ? TEXT("found") : TEXT("MISSING"),
				SwipesFound, RendAnimationCount);
		}
	}

	// OTHERWISE THE CYLINDER SITS INSIDE THE CREATURE.
	// `ACataclysmEnemyCharacter` creates PlaceholderBody in its constructor and
	// nothing about assigning a skeletal mesh removes it.
	// `test_every_dressed_enemy_hides_its_placeholder` refuses a dressed enemy
	// that does not do this.
	if (PlaceholderBody)
	{
		PlaceholderBody->SetVisibility(false);
	}

	UE_LOG(LogCataclysm, Verbose, TEXT("Imp is wearing %s."), BodyMeshPath);
	return true;
}
