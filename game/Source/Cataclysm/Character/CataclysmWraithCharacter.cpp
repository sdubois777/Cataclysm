// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmWraithCharacter.h"
#include "Cataclysm.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "UObject/SoftObjectPath.h"

// THE OWNER'S PURCHASED WRAITH PACK, in the main checkout only and gitignored like the Paragon packs. SKIN1 WITH HELM1
// AND THE SCYTHE for every Wraith, as ruled on 2026-11-26; the other three helms are kept for The Reaper to choose from
// later. The body mesh already carries the Skin1 materials, and so does the helm.
const TCHAR* ACataclysmWraithCharacter::BodyMeshPath = TEXT("/Game/Wraith/Mesh/SK_Wraith.SK_Wraith");
const TCHAR* ACataclysmWraithCharacter::HelmMeshPath = TEXT("/Game/Wraith/Mesh/SK_Wraith_Helm1.SK_Wraith_Helm1");
// THE REAPER'S HELM: one of the three the owner kept for The Reaper, so it reads as no ordinary Wraith.
const TCHAR* ACataclysmWraithCharacter::ReaperHelmMeshPath = TEXT("/Game/Wraith/Mesh/SK_Wraith_Helm2.SK_Wraith_Helm2");
const TCHAR* ACataclysmWraithCharacter::ScytheMeshPath =
	TEXT("/Game/Wraith/Mesh/Weapons/SM_Wraith_Scythe.SM_Wraith_Scythe");

// THE SKELETON'S OWN WEAPON BONE FOR THE SCYTHE. `SK_Wraith_Skeleton` names a `scythe` bone, and the pack's `BP_Wraith`
// attaches its weapon meshes "to its weapon bone".
const TCHAR* ACataclysmWraithCharacter::ScytheBoneName = TEXT("scythe");

// THE TWO SCYTHE SWINGS OF 1.6667 SECONDS, THE PACK'S SHORTEST, played in turn as the design asks. `Scythe_Attack1`,
// `3` and `4` are 2.33, 2.08 and 1.92 seconds. Read from each asset's `SequenceLength` on 2026-11-26.
const TCHAR* ACataclysmWraithCharacter::FirstReapAnimationPath =
	TEXT("/Game/Wraith/Animation/Anim_Wraith_Scythe_Attack2.Anim_Wraith_Scythe_Attack2");
const TCHAR* ACataclysmWraithCharacter::SecondReapAnimationPath =
	TEXT("/Game/Wraith/Animation/Anim_Wraith_Scythe_Attack5.Anim_Wraith_Scythe_Attack5");

// THE "Weap" LOOPS, which hold the scythe: the plain ones have empty hands.
const TCHAR* ACataclysmWraithCharacter::IdleAnimationPath =
	TEXT("/Game/Wraith/Animation/Anim_Wraith_idle1Weap.Anim_Wraith_idle1Weap");
const TCHAR* ACataclysmWraithCharacter::FlyAnimationPath =
	TEXT("/Game/Wraith/Animation/Anim_Wraith_FlyingForwardWeap.Anim_Wraith_FlyingForwardWeap");

// IN PLACE, NOT ROOT MOTION: the code moves the body (`BeginCharge`), as every charge in the game does. 1.4583 seconds.
const TCHAR* ACataclysmWraithCharacter::PhaseDashAnimationPath =
	TEXT("/Game/Wraith/Animation/Anim_Wraith_Ghost_PhaseDash_InPlace.Anim_Wraith_Ghost_PhaseDash_InPlace");

// THE OWNER'S CHOICE OF CLIP FOR THE THIRD ABILITY ("use the animation if we've got it"). 2.2917 seconds.
const TCHAR* ACataclysmWraithCharacter::SoulGrabAnimationPath =
	TEXT("/Game/Wraith/Animation/Anim_Wraith_Ghost_SoulGrab.Anim_Wraith_Ghost_SoulGrab");

// TWO DEATHS OF 2.4583 SECONDS, inside UCataclysmEnemyDeath::LongestCorpseSeconds's 4.
const TCHAR* ACataclysmWraithCharacter::FirstDeathAnimationPath =
	TEXT("/Game/Wraith/Animation/Anim_Wraith_FlyingDeath1.Anim_Wraith_FlyingDeath1");
const TCHAR* ACataclysmWraithCharacter::SecondDeathAnimationPath =
	TEXT("/Game/Wraith/Animation/Anim_Wraith_FlyingDeath2.Anim_Wraith_FlyingDeath2");

static TAutoConsoleVariable<float> CVarWraithAttackInterval(
	TEXT("Cataclysm.Wraith.AttackInterval"),
	0.0f,
	TEXT("Seconds between the Wraith's swings. 0 uses its designed 1.25."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarWraithPhaseDashCooldown(
	TEXT("Cataclysm.Wraith.PhaseDashCooldown"),
	0.0f,
	TEXT("Seconds between the Wraith's dashes. 0 uses its designed 5.0."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarWraithSoulGrabCooldown(
	TEXT("Cataclysm.Wraith.SoulGrabCooldown"),
	0.0f,
	TEXT("Seconds between the Wraith's Soul Grabs. 0 uses its designed 10.0."),
	ECVF_Default);

ACataclysmWraithCharacter::ACataclysmWraithCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// WHICH ROW OF game/Data/EnemyArchetypes.csv THIS CREATURE IS: its starting figures and its name under the cursor.
	ArchetypeRow = TEXT("Wraith");

	// ITS FOUR HIT REACTIONS, front, back, left and right. The pack's front one is `gethit1`. Issue #745.
	SetHitReactionClipsIn(TEXT("/Game/Wraith/Animation"), TEXT("Anim_Wraith_gethit1"), TEXT("Anim_Wraith_Hit_Back"),
		TEXT("Anim_Wraith_Hit_Left"), TEXT("Anim_Wraith_Hit_Right"));

	MeleeReachCm = DesignedMeleeReachCm;
	AttackIntervalSeconds = DesignedAttackIntervalSeconds;
	ResistancePercent = DesignedResistancePercent;
	CritChancePercent = DesignedCritChancePercent;
	CritMultiplierPercent = DesignedCritMultiplierPercent;
	EvasionPercent = DesignedEvasionPercent;
	EnergyShieldFraction = DesignedEnergyShieldFraction;
	NoticeRadiusCm = WraithNoticeRadiusCm;

	GetCapsuleComponent()->InitCapsuleSize(WraithCapsuleRadius, WraithCapsuleHalfHeight);

	// NOT SET BY ACataclysmEnemyCharacter. Without it the Wraith would move at Unreal's 600 cm/s.
	GetCharacterMovement()->MaxWalkSpeed = DesignedWalkSpeedCmPerSecond;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, DesignedTurnRateDegreesPerSecond, 0.0f);

	// THE HELM FOLLOWS THE BODY'S POSE and the scythe rides the `scythe` bone, as the pack's `BP_Wraith` puts them on.
	// Made here and left empty, so a worktree without the pack has no meshes to show and nothing missing to crash on.
	Helm = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Helm"));
	Helm->SetupAttachment(GetMesh());
	Helm->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Scythe = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Scythe"));
	Scythe->SetupAttachment(GetMesh(), FName(ScytheBoneName));
	Scythe->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACataclysmWraithCharacter::BeginPlay()
{
	Super::BeginPlay();
	ResolveBody(/*bIncludeAnimation=*/true);
	UpdateLoopingAnimation();
}

void ACataclysmWraithCharacter::Tick(float DeltaSeconds)
{
	// Super FIRST: it advances a running dash, and the loop below reads the speed that leaves.
	Super::Tick(DeltaSeconds);
	UpdateLoopingAnimation();
}

float ACataclysmWraithCharacter::AttackIntervalSecondsInUse()
{
	const float Override = CVarWraithAttackInterval.GetValueOnAnyThread();
	return Override > 0.0f ? Override : DesignedAttackIntervalSeconds;
}

float ACataclysmWraithCharacter::PhaseDashCooldownSecondsInUse()
{
	const float Override = CVarWraithPhaseDashCooldown.GetValueOnAnyThread();
	return Override > 0.0f ? Override : PhaseDashCooldownSeconds;
}

float ACataclysmWraithCharacter::SoulGrabCooldownSecondsInUse()
{
	const float Override = CVarWraithSoulGrabCooldown.GetValueOnAnyThread();
	return Override > 0.0f ? Override : SoulGrabCooldownSeconds;
}

float ACataclysmWraithCharacter::DesignedSecondsBetweenAttacks() const
{
	return AttackIntervalSecondsInUse();
}

TArray<FCataclysmEnemyAbility> ACataclysmWraithCharacter::EnemyAbilities() const
{
	// THE REAPER DASHES AND GRABS NOTHING: one blow of its kills, and a dash or a pull would make that blow impossible to
	// walk away from. It has its scythe and its slow walk.
	if (bIsTheReaper)
	{
		return {};
	}

	// THE DASH FROM 4 TO 8 METRES. Its shortest use is Soul Grab's radius, so the two never compete for one distance;
	// the header holds both against the walk in static_asserts.
	FCataclysmEnemyAbility PhaseDash;
	PhaseDash.Name = TEXT("Phase Dash");
	PhaseDash.MinRangeCm = PhaseDashMinimumRangeCm;
	PhaseDash.MaxRangeCm = PhaseDashRangeCm;
	PhaseDash.CooldownSeconds = PhaseDashCooldownSecondsInUse();
	PhaseDash.WindUpSeconds = PhaseDashWindUpSeconds;
	PhaseDash.Shape = ECataclysmSkillShape::Movement;
	PhaseDash.MarkerRadiusCm = PhaseDashRadiusCm;

	// SOUL GRAB FROM JUST OUTSIDE REAP'S REACH TO 4 METRES: a player Reap already reaches is swung at, not grabbed.
	FCataclysmEnemyAbility SoulGrab;
	SoulGrab.Name = TEXT("Soul Grab");
	SoulGrab.MinRangeCm = DesignedMeleeReachCm;
	SoulGrab.MaxRangeCm = SoulGrabRadiusCm;
	SoulGrab.CooldownSeconds = SoulGrabCooldownSecondsInUse();
	SoulGrab.WindUpSeconds = SoulGrabWindUpSeconds;
	SoulGrab.Shape = ECataclysmSkillShape::Strike;
	SoulGrab.MarkerRadiusCm = SoulGrabRadiusCm;

	return {PhaseDash, SoulGrab};
}

void ACataclysmWraithCharacter::BeginEnemyAbilityWindUp(int32 Index, AActor* /*Target*/)
{
	// THE DASH CLIP RUNS ACROSS THE WIND-UP AND THE RUN, its 1.46 seconds over the 1.39 the two take, so the ghost
	// is still mid-dash when it arrives. SOUL GRAB'S RUNS ACROSS ITS WIND-UP, the hands closing as the ring lands.
	if (Index == PhaseDashAbility)
	{
		PlayOneShot(PhaseDashAnimation.Get(), PhaseDashWindUpSeconds + PhaseDashRangeCm / PhaseDashSpeedCmPerSecond);
	}
	else if (Index == SoulGrabAbility)
	{
		PlayOneShot(SoulGrabAnimation.Get(), SoulGrabWindUpSeconds);
	}
}

void ACataclysmWraithCharacter::UseEnemyAbility(int32 Index, AActor* /*Target*/, const FVector& AimedAt)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (Index == PhaseDashAbility)
	{
		// TO THE FAR END OF THE MARKED LANE, NOT THE TARGET, which is what puts it behind the player: the controller
		// aims a Movement ability at the end of its full range. THROUGH CREATURES AND NOT WALLS, because the base
		// class's charge sweeps for walls alone, and it hits each creature in the lane once. NO SHOVE: it goes through
		// the player rather than knocking them aside.
		BeginCharge(AimedAt, PhaseDashSpeedCmPerSecond, PhaseDashRadiusCm, PhaseDashDamagePercent,
					/*KnockbackCm=*/0.0f);
		return;
	}

	if (Index == SoulGrabAbility)
	{
		// WHOEVER IS STILL IN THE RING WHEN IT LANDS takes the hit and is pulled to the Wraith's side, inside Reap's
		// reach. A marked ring, so anybody who walked out is not caught.
		LastSoulGrabCaught = 0;
		const FVector Here = GetActorLocation();
		for (AActor* Caught : UCataclysmTargeting::FindEnemiesInSphere(World, this, Here, SoulGrabRadiusCm))
		{
			if (!Caught)
			{
				continue;
			}
			++LastSoulGrabCaught;
			UCataclysmSkillEffects::ApplyHit(this, Caught, SoulGrabDamagePercent);
			const float Gap = FVector::Dist2D(Here, Caught->GetActorLocation());
			if (Gap > SoulGrabLeavesAtCm)
			{
				UCataclysmSkillEffects::ApplyPull(this, Caught, Gap - SoulGrabLeavesAtCm);
			}
		}
	}
}

void ACataclysmWraithCharacter::AttackTarget(AActor* Target)
{
	// THE SWING, ITS TARGET FIRST, as every creature's basic attack lands: `StrikeWhenTheSwingConnects` with the
	// unmeasured strike of zero lands it now (issue #783).
	UAnimSequence* Swing = ReapAnimations.IsValidIndex(NextReapClip) ? ReapAnimations[NextReapClip].Get() : nullptr;
	NextReapClip = (NextReapClip + 1) % 2;
	PlayOneShot(Swing, AttackIntervalSecondsInUse());
	StrikeWhenTheSwingConnects(Target, BasicAttackStrikeDelaySeconds(ReapStrikeSeconds, LastPlayedRate));

	// AND UP TO TWO MORE IN THE ARC, Reap's `MaxTargets=3`, at the same 100%. Its target is already struck, so it is
	// skipped here; the search is nearest first, so a crowd gives up its nearest two.
	UWorld* World = GetWorld();
	LastReapCaught = Target ? 1 : 0;
	if (!World)
	{
		return;
	}
	for (AActor* Other : UCataclysmTargeting::FindEnemiesInCone(World, this, GetActorLocation(),
			GetActorForwardVector(), DesignedMeleeReachCm, ReapAngleDegrees, ReapMaxTargets))
	{
		if (!Other || Other == Target || LastReapCaught >= ReapMaxTargets)
		{
			continue;
		}
		++LastReapCaught;
		UCataclysmSkillEffects::ApplyHit(this, Other, 100.0f);
	}
}

void ACataclysmWraithCharacter::UpdateLoopingAnimation()
{
	const UWorld* World = GetWorld();
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!World || !MeshComponent)
	{
		return;
	}

	// A ONE-SHOT KEEPS THE MESH UNTIL IT ENDS.
	if (World->GetTimeSeconds() < OneShotEndsAtSeconds)
	{
		return;
	}

	const bool bMoving = GetVelocity().Size2D() > MovingThresholdCmPerSecond;
	UAnimSequence* Wanted = bMoving ? FlyAnimation.Get() : IdleAnimation.Get();
	if (!Wanted || Wanted == CurrentLoopingAnimation)
	{
		return;
	}

	// ONLY ON A CHANGE, because PlayAnimation restarts a clip. AT ITS AUTHORED SPEED: a hovering body has no planted
	// foot to slide.
	CurrentLoopingAnimation = Wanted;
	MeshComponent->PlayAnimation(Wanted, /*bLooping=*/true);
	MeshComponent->SetPlayRate(1.0f);
}

float ACataclysmWraithCharacter::PlayOneShot(UAnimSequence* Animation, float HoldSeconds)
{
	// WHATEVER PLAYS NEXT REPLACES A FLINCH. Issue #745.
	ForgetFlinch();

	LastPlayedAnimation = Animation;
	LastPlayedRate = 0.0f;

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

	// NEVER SLOWER THAN AUTHORED, ONLY FASTER, AND ONLY WHEN IT MUST BE: the rule every creature follows.
	const float Hold = HoldSeconds > 0.0f ? HoldSeconds : Length;
	const float Rate = FMath::Clamp(FMath::Max(1.0f, Length / Hold), MinimumPlayRate, MaximumPlayRate);

	MeshComponent->PlayAnimation(Animation, /*bLooping=*/false);
	MeshComponent->SetPlayRate(Rate);
	LastPlayedRate = Rate;

	if (const UWorld* World = GetWorld())
	{
		OneShotEndsAtSeconds = World->GetTimeSeconds() + Length / Rate;
	}
	CurrentLoopingAnimation = nullptr;
	return Length / Rate;
}

bool ACataclysmWraithCharacter::IsPlayingAOneShot() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() < OneShotEndsAtSeconds;
}

float ACataclysmWraithCharacter::PlayFlinchClip(UAnimSequence* Clip)
{
	return PlayOneShot(Clip);
}

void ACataclysmWraithCharacter::BecomeTheReaper()
{
	bIsTheReaper = true;

	// THE WALK BOTH WHERE IT IS SET AND WHERE IT IS READ BACK: the base class captured the designed walk in BeginPlay,
	// which has already run, and `RefreshWalkSpeed` rebuilds the speed from that capture.
	ACataclysmEnemyCharacter::DesignedWalkSpeedCmPerSecond = ReaperWalkSpeedCmPerSecond;
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = ReaperWalkSpeedCmPerSecond;
	}
	RefreshWalkSpeed();

	if (Helm)
	{
		if (USkeletalMesh* ReaperHelm = Cast<USkeletalMesh>(FSoftObjectPath(ReaperHelmMeshPath).TryLoad()))
		{
			Helm->SetSkeletalMesh(ReaperHelm);
			if (USkeletalMeshComponent* MeshComponent = GetMesh())
			{
				Helm->SetLeaderPoseComponent(MeshComponent);
			}
		}
	}
}

bool ACataclysmWraithCharacter::ResolveBody(bool bIncludeAnimation)
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return false;
	}

	USkeletalMesh* Body = Cast<USkeletalMesh>(FSoftObjectPath(BodyMeshPath).TryLoad());
	if (!Body)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("Wraith art not found at %s, so it is keeping the placeholder cylinder. This is expected without the "
				 "owner's Wraith pack, which only the main checkout has; see game/docs/enemy-source-assets.md."),
			BodyMeshPath);
		return false;
	}

	MeshComponent->SetSkeletalMesh(Body);

	// FEET ON THE CAPSULE BOTTOM AND THE ENGINE'S YAW, as every dressed creature: the mesh drops by the half-height and
	// turns -90 degrees, the engine's convention for a character mesh facing -Y.
	MeshComponent->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -WraithCapsuleHalfHeight),
												  FRotator(0.0f, -90.0f, 0.0f));

	// THE HELM AND THE SCYTHE. A missing one is logged and the body still worn: a Wraith without its helm still fights.
	if (Helm)
	{
		if (USkeletalMesh* HelmMesh = Cast<USkeletalMesh>(FSoftObjectPath(HelmMeshPath).TryLoad()))
		{
			Helm->SetSkeletalMesh(HelmMesh);
			Helm->SetLeaderPoseComponent(MeshComponent);
		}
		else
		{
			UE_LOG(LogCataclysm, Warning, TEXT("Wraith helm not found at %s."), HelmMeshPath);
		}
	}
	if (Scythe)
	{
		if (UStaticMesh* ScytheMesh = Cast<UStaticMesh>(FSoftObjectPath(ScytheMeshPath).TryLoad()))
		{
			Scythe->SetStaticMesh(ScytheMesh);
			Scythe->AttachToComponent(MeshComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale,
									  FName(ScytheBoneName));
		}
		else
		{
			UE_LOG(LogCataclysm, Warning, TEXT("Wraith scythe not found at %s."), ScytheMeshPath);
		}
	}

	if (bIncludeAnimation)
	{
		ReapAnimations.Reset();
		ReapAnimations.Add(Cast<UAnimSequence>(FSoftObjectPath(FirstReapAnimationPath).TryLoad()));
		ReapAnimations.Add(Cast<UAnimSequence>(FSoftObjectPath(SecondReapAnimationPath).TryLoad()));
		IdleAnimation = Cast<UAnimSequence>(FSoftObjectPath(IdleAnimationPath).TryLoad());
		FlyAnimation = Cast<UAnimSequence>(FSoftObjectPath(FlyAnimationPath).TryLoad());
		PhaseDashAnimation = Cast<UAnimSequence>(FSoftObjectPath(PhaseDashAnimationPath).TryLoad());
		SoulGrabAnimation = Cast<UAnimSequence>(FSoftObjectPath(SoulGrabAnimationPath).TryLoad());

		// BOTH DEATH CLIPS, A NULL KEPT rather than skipped: see ACataclysmEnemyCharacter::PlayDeathAnimation.
		DeathAnimations.Reset();
		DeathAnimations.Add(Cast<UAnimSequence>(FSoftObjectPath(FirstDeathAnimationPath).TryLoad()));
		DeathAnimations.Add(Cast<UAnimSequence>(FSoftObjectPath(SecondDeathAnimationPath).TryLoad()));

		LoadHitReactionClips();

		if (!IdleAnimation || !FlyAnimation || !ReapAnimations[0] || !ReapAnimations[1])
		{
			UE_LOG(LogCataclysm, Warning, TEXT("Wraith animations missing: idle %s, fly %s, swings %s and %s."),
				IdleAnimation ? TEXT("found") : TEXT("MISSING"), FlyAnimation ? TEXT("found") : TEXT("MISSING"),
				ReapAnimations[0] ? TEXT("found") : TEXT("MISSING"), ReapAnimations[1] ? TEXT("found") : TEXT("MISSING"));
		}
	}

	// OTHERWISE THE CYLINDER SITS INSIDE THE CREATURE. `test_every_dressed_enemy_hides_its_placeholder_cylinder` holds it.
	if (PlaceholderBody)
	{
		PlaceholderBody->SetVisibility(false);
	}
	return true;
}
