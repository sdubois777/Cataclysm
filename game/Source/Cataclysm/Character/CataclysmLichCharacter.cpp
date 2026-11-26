// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmLichCharacter.h"
#include "Cataclysm.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Animation/AnimSequence.h"
#include "Character/CataclysmEnemyController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPath.h"

// THE OWNER'S LICH PACK, in the main checkout's game/Content/Lich (gitignored). `SK_Lich` is the whole body with its
// cloth off; the pack's `Separate_Mesh` folder holds the same body in parts, unused.
const TCHAR* ACataclysmLichCharacter::BodyMeshPath = TEXT("/Game/Lich/Mesh/SK_Lich.SK_Lich");
const TCHAR* ACataclysmLichCharacter::AnimationFolder = TEXT("/Game/Lich/Animations");

// 1.467 s, the shortest of its three idles.
const TCHAR* ACataclysmLichCharacter::IdleAnimationName = TEXT("Anim_Idle1_Lich");
// 1.100 s, its forward walk; `Anim_Run_Lich` (3.333 s) is the alternative.
const TCHAR* ACataclysmLichCharacter::JogAnimationName = TEXT("Anim_WalkingF_Lich");
// 1.000 s, the only attack clip short enough for a bolt every second. UNCONFIRMED that it is a cast.
const TCHAR* ACataclysmLichCharacter::GraveBoltAnimationName = TEXT("Anim_Attack4_Lich");
// 2.467 s. UNCONFIRMED that it is a strike around it.
const TCHAR* ACataclysmLichCharacter::GraveBurstAnimationName = TEXT("Anim_Attack2_Lich");
// 3.933 s; `Anim_Attack3_Lich` (4.567 s) is the alternative. UNCONFIRMED that it is a raising cast.
const TCHAR* ACataclysmLichCharacter::RaiseAnimationName = TEXT("Anim_Attack1_Lich");

// 4.967 s, its one death.
const TCHAR* ACataclysmLichCharacter::DeathAnimationNames[DeathAnimationCount] = {
	TEXT("Anim_Dead1_Lich"),
};

// A SPELL AND A PROJECTILE, as Soulfire. No area tag, so the bolt is a direct hit that can be evaded.
const TCHAR* ACataclysmLichCharacter::GraveBoltTags = TEXT("Type.Spell, Type.Projectile");
// A SPELL AROUND IT. Not melee, unlike the Brute's Stomp: a caster's burst.
const TCHAR* ACataclysmLichCharacter::GraveBurstTags = TEXT("Type.AOE.PointBlank, Type.Spell");

ACataclysmLichCharacter::ACataclysmLichCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	ArchetypeRow = TEXT("Lich");

	// TWO HIT CLIPS FOR FOUR SIDES, the owner's accepted recommendation (7) of 2026-11-26: `Hit1` struck from the front
	// and from behind, `Hit2` from the left and the right. Which way either clip leans was not checked by eye.
	SetHitReactionClipsIn(AnimationFolder,
		TEXT("Anim_Hit1_Lich"), TEXT("Anim_Hit1_Lich"), TEXT("Anim_Hit2_Lich"), TEXT("Anim_Hit2_Lich"));

	// ITS "MELEE REACH" IS ITS BOLT'S RANGE, as the Succubus's is: the distance it stops walking at.
	MeleeReachCm = GraveBoltRangeCm;
	AttackIntervalSeconds = DesignedAttackIntervalSeconds;
	ResistancePercent = DesignedResistancePercent;
	CritChancePercent = DesignedCritChancePercent;
	CritMultiplierPercent = DesignedCritMultiplierPercent;
	EvasionPercent = DesignedEvasionPercent;
	EnergyShieldFraction = DesignedEnergyShieldFraction;
	NoticeRadiusCm = LichNoticeRadiusCm;

	GetCapsuleComponent()->InitCapsuleSize(LichCapsuleRadius, LichCapsuleHalfHeight);
	GetCharacterMovement()->MaxWalkSpeed = DesignedWalkSpeedCmPerSecond;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, DesignedTurnRateDegreesPerSecond, 0.0f);
}

void ACataclysmLichCharacter::BeginPlay()
{
	Super::BeginPlay();
	ResolveBody(/*bIncludeAnimation=*/true);
	UpdateLoopingAnimation();
}

void ACataclysmLichCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StartPendingWindUpClip();
	UpdateLoopingAnimation();
}

float ACataclysmLichCharacter::DesignedSecondsBetweenAttacks() const
{
	return DesignedAttackIntervalSeconds;
}

void ACataclysmLichCharacter::AttackTarget(AActor* /*Target*/)
{
	// NOTHING, as the Succubus: its reach is its bolt's 8 metres, and the base class's melee hit at that reach would be
	// a free blow from nowhere. Its basic attack is Grave Bolt, an entry in `EnemyAbilities`.
}

bool ACataclysmLichCharacter::HasFallenToRaise() const
{
	const UWorld* World = GetWorld();
	const ACataclysmDungeonGameMode* Mode = World ? World->GetAuthGameMode<ACataclysmDungeonGameMode>() : nullptr;
	return Mode && Mode->FallenToRaiseNear(GetActorLocation(), RaiseRadiusCm) > 0;
}

TArray<FCataclysmEnemyAbility> ACataclysmLichCharacter::EnemyAbilities() const
{
	FCataclysmEnemyAbility Raise;
	Raise.Name = TEXT("Raise the Fallen");
	Raise.MinRangeCm = 0.0f;
	// CHOSEN ONLY WHEN A DEATH IT COULD RAISE IS NEAR (the plan's J3): otherwise its range is nought, and the brain, which
	// asks the target's distance against it, never picks it. So a Lich never casts at nothing.
	Raise.MaxRangeCm = HasFallenToRaise() ? LichNoticeRadiusCm : 0.0f;
	Raise.CooldownSeconds = RaiseCooldownSeconds;
	Raise.WindUpSeconds = RaiseWindUpSeconds;
	// A SUMMON DRAWS NO MARKER (`ACataclysmEnemyController::ShowWindUpMarker`): it is read from the cast.
	Raise.Shape = ECataclysmSkillShape::Summon;
	Raise.bArcsOntoItsTarget = false;
	Raise.MarkerRadiusCm = 0.0f;

	FCataclysmEnemyAbility Burst;
	Burst.Name = TEXT("Grave Burst");
	Burst.MinRangeCm = 0.0f;
	// WHEN THE PLAYER COMES WITHIN ITS RING, its answer to being reached.
	Burst.MaxRangeCm = GraveBurstRadiusCm;
	Burst.CooldownSeconds = GraveBurstCooldownSeconds;
	Burst.WindUpSeconds = GraveBurstWindUpSeconds;
	Burst.Shape = ECataclysmSkillShape::Strike;
	Burst.bArcsOntoItsTarget = false;
	Burst.MarkerRadiusCm = GraveBurstRadiusCm;

	FCataclysmEnemyAbility Bolt;
	Bolt.Name = TEXT("Grave Bolt");
	Bolt.MinRangeCm = 0.0f;
	Bolt.MaxRangeCm = GraveBoltRangeCm;
	Bolt.CooldownSeconds = 0.0f;
	Bolt.WindUpSeconds = GraveBoltWindUpSeconds;
	Bolt.Shape = ECataclysmSkillShape::Projectile;
	Bolt.bArcsOntoItsTarget = false;
	Bolt.MarkerRadiusCm = GraveBoltRadiusCm;
	// A QUICK CAST: no lane on the ground; the bolt in flight is the warning.
	Bolt.bDrawsNoMarker = true;
	Bolt.bIsBasicAttack = true;

	return {Raise, Burst, Bolt};
}

UAnimSequence* ACataclysmLichCharacter::ClipFor(int32 Index, float& OutReleaseSeconds, float& OutWindUpSeconds) const
{
	switch (Index)
	{
	case RaiseTheFallenAbility:
		OutReleaseSeconds = RaiseReleaseSeconds;
		OutWindUpSeconds = RaiseWindUpSeconds;
		return RaiseAnimation.Get();
	case GraveBurstAbility:
		OutReleaseSeconds = GraveBurstReleaseSeconds;
		OutWindUpSeconds = GraveBurstWindUpSeconds;
		return GraveBurstAnimation.Get();
	case GraveBoltAbility:
		OutReleaseSeconds = GraveBoltReleaseSeconds;
		OutWindUpSeconds = GraveBoltWindUpSeconds;
		return GraveBoltAnimation.Get();
	default:
		OutReleaseSeconds = 0.0f;
		OutWindUpSeconds = 0.0f;
		return nullptr;
	}
}

void ACataclysmLichCharacter::BeginEnemyAbilityWindUp(int32 Index, AActor* /*Target*/)
{
	// EVERY ABILITY'S CLIP IS LINED UP SO ITS RELEASE FALLS AS THE WIND-UP ENDS, the Succubus's way: it waits, from
	// `Tick`, when the release comes later in the clip than the wind-up is long.
	PendingWindUpAbility = INDEX_NONE;
	float Release = 0.0f;
	float WindUp = 0.0f;
	UAnimSequence* Clip = ClipFor(Index, Release, WindUp);
	if (!Clip)
	{
		return;
	}
	if (const UWorld* World = GetWorld())
	{
		WindUpBeganAtSeconds = World->GetTimeSeconds();
		if (StrikeAlignedDelaySeconds(Release, WindUp, MinimumPlayRate, MaximumPlayRate) > 0.0f)
		{
			PendingWindUpAbility = Index;
			return;
		}
	}
	PlayOneShotAtRate(Clip, StrikeAlignedPlayRate(Release, WindUp, MinimumPlayRate, MaximumPlayRate));
}

void ACataclysmLichCharacter::StartPendingWindUpClip()
{
	if (PendingWindUpAbility == INDEX_NONE)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const ACataclysmEnemyController* Brain = Cast<ACataclysmEnemyController>(GetController());
	// AN INTERRUPTED CAST STARTS NO CLIP: the brain is asked whether it is still winding this up.
	if (!World || !Brain || Brain->WindingUpAbility != PendingWindUpAbility)
	{
		PendingWindUpAbility = INDEX_NONE;
		return;
	}
	float Release = 0.0f;
	float WindUp = 0.0f;
	UAnimSequence* Clip = ClipFor(PendingWindUpAbility, Release, WindUp);
	const float Elapsed = World->GetTimeSeconds() - WindUpBeganAtSeconds;
	if (Elapsed + UE_KINDA_SMALL_NUMBER < StrikeAlignedDelaySeconds(Release, WindUp, MinimumPlayRate, MaximumPlayRate))
	{
		return;
	}
	PendingWindUpAbility = INDEX_NONE;
	PlayOneShotAtRate(Clip, StrikeAlignedPlayRate(Release, WindUp, MinimumPlayRate, MaximumPlayRate));
}

void ACataclysmLichCharacter::UseEnemyAbility(int32 Index, AActor* Target, const FVector& AimedAt)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (Index == GraveBoltAbility)
	{
		// FROM THE MIDDLE OF THE CREATURE, as Soulfire: no hand bone was measured. Aimed where the target stood when the
		// cast began; flat, at the slowest player projectile's speed.
		LastShotFired = ACataclysmProjectile::Fire(
			this, GetActorLocation(), AimedAt, GraveBoltRadiusCm, GraveBoltSpeedCmPerSecond, /*InPierce=*/0,
			/*bInReturns=*/false, GraveBoltDamagePercent, UCataclysmSkillShapes::TagsFromCell(GraveBoltTags),
			/*bInBurns=*/false, /*InBodyMesh=*/nullptr, /*InFlightSeconds=*/0.0f, /*InCritChancePercent=*/-1.0f,
			/*InSkillHealthCostPercent=*/-1.0f, /*InFiringSkill=*/nullptr, /*bInQuickCast=*/true);
		return;
	}

	if (Index == GraveBurstAbility)
	{
		// EVERYTHING HOSTILE IN THE RING, the Brute's Stomp's way, with no stun and no knockback: a caster's burst.
		const FGameplayTagContainer Tags = UCataclysmSkillShapes::TagsFromCell(GraveBurstTags);
		LastBurstCaught = 0;
		for (AActor* Caught : UCataclysmTargeting::FindEnemiesInSphere(World, this, GetActorLocation(),
																		GraveBurstRadiusCm))
		{
			UCataclysmSkillEffects::ApplyHit(this, Caught, GraveBurstDamagePercent, Tags);
			++LastBurstCaught;
		}
		return;
	}

	if (Index == RaiseTheFallenAbility)
	{
		// THE DUNGEON KEEPS THE DEATHS AND RAISES THEM: the record is the floor's, not the Lich's, so two Liches share it
		// and a death is raised at most once whichever raises it.
		ACataclysmDungeonGameMode* Mode = World->GetAuthGameMode<ACataclysmDungeonGameMode>();
		LastRaised = Mode ? Mode->RaiseTheFallenNear(this, GetActorLocation(), RaiseRadiusCm, RaiseMostAtOnce) : 0;
		return;
	}
}

void ACataclysmLichCharacter::UpdateLoopingAnimation()
{
	const UWorld* World = GetWorld();
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!World || !MeshComponent || World->GetTimeSeconds() < OneShotEndsAtSeconds)
	{
		return;
	}
	const bool bWalking = GetVelocity().Size2D() > 10.0f;
	UAnimSequence* Wanted = bWalking ? JogAnimation.Get() : IdleAnimation.Get();
	if (!Wanted || Wanted == CurrentLoopingAnimation)
	{
		return;
	}
	CurrentLoopingAnimation = Wanted;
	MeshComponent->PlayAnimation(Wanted, /*bLooping=*/true);
	MeshComponent->SetPlayRate(1.0f);
}

float ACataclysmLichCharacter::PlayOneShot(UAnimSequence* Animation)
{
	return PlayOneShotAtRate(Animation, 1.0f);
}

float ACataclysmLichCharacter::PlayOneShotAtRate(UAnimSequence* Animation, float Rate)
{
	ForgetFlinch();
	LastPlayedAnimation = Animation;
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!Animation || !MeshComponent || Animation->GetPlayLength() <= 0.0f || Rate <= 0.0f)
	{
		return 0.0f;
	}
	MeshComponent->PlayAnimation(Animation, /*bLooping=*/false);
	MeshComponent->SetPlayRate(Rate);
	const float Seconds = Animation->GetPlayLength() / Rate;
	if (const UWorld* World = GetWorld())
	{
		OneShotEndsAtSeconds = World->GetTimeSeconds() + Seconds;
	}
	CurrentLoopingAnimation = nullptr;
	return Seconds;
}

bool ACataclysmLichCharacter::IsPlayingAOneShot() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() < OneShotEndsAtSeconds;
}

float ACataclysmLichCharacter::PlayFlinchClip(UAnimSequence* Clip)
{
	return PlayOneShot(Clip);
}

bool ACataclysmLichCharacter::ResolveBody(bool bIncludeAnimation)
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
			   TEXT("Lich art not found at %s, so it is keeping the placeholder cylinder. This is expected without the "
					"Lich pack; see game/docs/enemy-source-assets.md."),
			   BodyMeshPath);
		return false;
	}
	MeshComponent->SetSkeletalMesh(Body);
	// FEET ON THE CAPSULE BOTTOM AND THE ENGINE'S -90 DEGREE YAW, the other creatures' convention. The pack's forward
	// axis was NOT measured.
	MeshComponent->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -LichCapsuleHalfHeight),
												  FRotator(0.0f, -90.0f, 0.0f));
	if (bIncludeAnimation)
	{
		const auto Load = [](const TCHAR* Name)
		{
			return Cast<UAnimSequence>(FSoftObjectPath(ClipPathIn(AnimationFolder, Name)).TryLoad());
		};
		IdleAnimation = Load(IdleAnimationName);
		JogAnimation = Load(JogAnimationName);
		GraveBoltAnimation = Load(GraveBoltAnimationName);
		GraveBurstAnimation = Load(GraveBurstAnimationName);
		RaiseAnimation = Load(RaiseAnimationName);
		DeathAnimations.Reset();
		for (const TCHAR* Name : DeathAnimationNames)
		{
			DeathAnimations.Add(Load(Name));
		}
		LoadHitReactionClips();
		if (!IdleAnimation || !JogAnimation || !GraveBoltAnimation || !GraveBurstAnimation || !RaiseAnimation)
		{
			UE_LOG(LogCataclysm, Warning, TEXT("Lich animations missing: idle %s, walk %s, bolt %s, burst %s, raise %s."),
				   IdleAnimation ? TEXT("found") : TEXT("MISSING"), JogAnimation ? TEXT("found") : TEXT("MISSING"),
				   GraveBoltAnimation ? TEXT("found") : TEXT("MISSING"),
				   GraveBurstAnimation ? TEXT("found") : TEXT("MISSING"),
				   RaiseAnimation ? TEXT("found") : TEXT("MISSING"));
		}
	}
	// OTHERWISE THE CYLINDER SITS INSIDE THE CREATURE.
	if (PlaceholderBody)
	{
		PlaceholderBody->SetVisibility(false);
	}
	return true;
}
