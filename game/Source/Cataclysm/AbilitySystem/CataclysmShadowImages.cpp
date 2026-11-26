// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmShadowImages.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmMovement.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Cataclysm.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

const TCHAR* UCataclysmShadowImages::AfterimageStat = TEXT("afterimage_repeats_your_next_attack");
const TCHAR* UCataclysmShadowImages::CloneEveryEliteKillsStat = TEXT("shadow_clone_every_elite_kills");
const TCHAR* UCataclysmShadowImages::CloneSecondsStat = TEXT("shadow_clone_seconds");
const TCHAR* UCataclysmShadowImages::ShadowStepMetresStat = TEXT("movement_targets_an_enemy_within_metres");
const TCHAR* UCataclysmShadowImages::ExtraStrikesStat = TEXT("hits_strike_again");

namespace CataclysmShadowImagesState
{
	/**
	 * Each character's Elite-or-stronger kills toward its next clone, keyed weakly. HELD HERE AND NOT ON THE ABILITY
	 * SYSTEM, as the Warhound's summoning clock is kept in `CataclysmMinion.cpp`. A gone character's entry is dropped
	 * when the next is written.
	 */
	TMap<TWeakObjectPtr<const AActor>, int32>& EliteKills()
	{
		static TMap<TWeakObjectPtr<const AActor>, int32> Held;
		return Held;
	}

	/** The world's time now, or nought with no world. */
	double NowIn(const UWorld* World)
	{
		return World ? World->GetTimeSeconds() : 0.0;
	}
}

// ==========================================================================
// The shadow itself
// ==========================================================================

ACataclysmShadowImage::ACataclysmShadowImage()
{
	PrimaryActorTick.bCanEverTick = true;

	// A SMALL BODY THAT A WALL STOPS AND NOTHING ELSE TOUCHES: no character, no trace for sight, no overlap a search for
	// creatures makes can find it. It is swept only by `UCataclysmMovement::MoveWithoutSticking` as a clone walks.
	Body = CreateDefaultSubobject<USphereComponent>(TEXT("Body"));
	Body->InitSphereRadius(30.0f);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionObjectType(ECC_WorldDynamic);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCanEverAffectNavigation(false);
	RootComponent = Body;

	Look = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("Look"));
	Look->SetupAttachment(Body);
	Look->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Look->SetCanEverAffectNavigation(false);
}

void ACataclysmShadowImage::TakeTheMakersPose()
{
	const ACharacter* Character = Cast<ACharacter>(Maker.Get());
	USkeletalMeshComponent* Theirs = Character ? Character->GetMesh() : nullptr;
	if (!Look || !Theirs || !Theirs->GetSkeletalMeshAsset())
	{
		return;
	}
	if (Look->GetSkinnedAsset() != Theirs->GetSkeletalMeshAsset())
	{
		Look->SetSkinnedAssetAndUpdate(Theirs->GetSkeletalMeshAsset());
		// WHERE THE MAKER'S MESH SITS ON ITS CAPSULE, so the copy stands on the ground the way the maker does.
		Look->SetRelativeTransform(Theirs->GetRelativeTransform());
	}
	Look->CopyPoseFromSkeletalComponent(Theirs);
}

void ACataclysmShadowImage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = CataclysmShadowImagesState::NowIn(GetWorld());
	if (Kind == ECataclysmShadowImageKind::Clone)
	{
		CloneStep(Now);
		return;
	}
	// AN AFTERIMAGE NOBODY GAVE AN ATTACK IS GONE AT ITS TIME. One holding an attack is spent on the tick after the use.
	if (!bHoldsAnAttack && HasExpired(Now))
	{
		Destroy();
	}
}

bool ACataclysmShadowImage::CloneStep(double NowSeconds)
{
	AActor* Player = Maker.Get();
	if (HasExpired(NowSeconds) || !IsValid(Player) || UCataclysmSkillEffects::IsDead(Player))
	{
		Destroy();
		return false;
	}
	TakeTheMakersPose();

	const double Since = LastStepAtSeconds < 0.0 ? 0.0 : NowSeconds - LastStepAtSeconds;
	LastStepAtSeconds = NowSeconds;

	// THE NEAREST LIVING ENEMY OF ITS MAKER WITHIN ITS NOTICE, measured from where the clone stands.
	const FVector Here = GetActorLocation();
	AActor* Target = nullptr;
	float Nearest = TNumericLimits<float>::Max();
	for (AActor* Enemy : UCataclysmTargeting::FindEnemiesInSphere(GetWorld(), Player, Here, CloneNoticeCm))
	{
		const float Apart = FVector::Dist2D(Here, Enemy->GetActorLocation());
		if (!UCataclysmSkillEffects::IsDead(Enemy) && Apart < Nearest)
		{
			Nearest = Apart;
			Target = Enemy;
		}
	}
	if (!Target)
	{
		return false;
	}

	// FACING IT.
	const FVector Toward = Target->GetActorLocation() - Here;
	if (!Toward.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.0f, Toward.Rotation().Yaw, 0.0f));
	}

	// THE MAKER'S BASIC ATTACK, ITS REACH AND ITS RATE. A maker whose basic attack reach cannot be read is given the
	// row's own Radius, as its strike would reach.
	const FCataclysmWeaponSkill* Basic = UCataclysmTriggeredSkill::HeldBasicAttack(Player);
	const UCataclysmAbilitySystemComponent* Theirs =
		Cast<const UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Player));
	if (!Basic || !Theirs)
	{
		return false;
	}
	float ReachCm = UCataclysmBasicAttack::ReachCmOf(Theirs);
	if (ReachCm <= 0.0f)
	{
		ReachCm = Basic->Params.RangeCm > 0.0f ? Basic->Params.RangeCm : Basic->Params.RadiusCm;
	}
	const float Interval = UCataclysmBasicAttack::SecondsBetweenSwingsFor(Theirs);

	// OUT OF REACH, IT WALKS, at its maker's walking speed, along the ground; a wall stops it.
	if (Nearest > ReachCm + UCataclysmTargeting::BodyRadiusOf(Target))
	{
		const ACharacter* Walker = Cast<ACharacter>(Player);
		const UCharacterMovementComponent* Moves = Walker ? Walker->GetCharacterMovement() : nullptr;
		const float Speed = Moves && Moves->MaxWalkSpeed > 0.0f ? Moves->MaxWalkSpeed : FallbackWalkCmPerSecond;
		const float Walk = FMath::Min(Speed * static_cast<float>(FMath::Min(Since, static_cast<double>(LongestStepSeconds))),
									  Nearest - ReachCm);
		if (Walk > 0.0f)
		{
			UCataclysmMovement::MoveWithoutSticking(this, Toward.GetSafeNormal2D() * Walk);
		}
		return false;
	}

	// IN REACH, IT SWINGS ONCE EACH OF ITS MAKER'S SWING INTERVALS, the first at once. A maker who cannot swing (no
	// rate) gives it none either.
	if (Interval <= 0.0f || (LastSwingAtSeconds >= 0.0 && NowSeconds - LastSwingAtSeconds < Interval))
	{
		return false;
	}
	const FVector From = GetActorLocation();
	if (!UCataclysmTriggeredSkill::Trigger(Player, *Basic, Target->GetActorLocation(), /*DamageShare=*/1.0f,
										   /*bPaysCost=*/false, &From, /*CastBy=*/this))
	{
		return false;
	}
	LastSwingAtSeconds = NowSeconds;
	++AttacksStarted;
	return true;
}

bool ACataclysmShadowImage::RepeatTheAttack()
{
	AActor* Player = Maker.Get();
	const bool bHeld = bHoldsAnAttack;
	bHoldsAnAttack = false;
	bool bStarted = false;
	if (bHeld && IsValid(Player) && !UCataclysmSkillEffects::IsDead(Player))
	{
		const FVector From = GetActorLocation();
		bStarted = UCataclysmTriggeredSkill::Trigger(Player, AttackToRepeat, AttackAim, /*DamageShare=*/1.0f,
													 /*bPaysCost=*/false, &From, /*CastBy=*/this);
		if (bStarted)
		{
			++AttacksStarted;
		}
		UE_LOG(LogCataclysm, Display, TEXT("%s's afterimage repeated %s from (%.0f, %.0f, %.0f): %s."),
			   *GetNameSafe(Player), *AttackToRepeat.Name, From.X, From.Y, From.Z,
			   bStarted ? TEXT("started") : TEXT("refused"));
	}
	// SPENT EITHER WAY: one attack is what it repeats.
	Destroy();
	return bStarted;
}

// ==========================================================================
// Afterimage
// ==========================================================================

float UCataclysmShadowImages::StatOf(const AActor* Character, const TCHAR* Stat, const FGameplayTagContainer& Tags)
{
	const UCataclysmAbilitySystemComponent* Theirs =
		Cast<const UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Character));
	return Theirs ? Theirs->StatForSkill(FName(Stat), Tags, /*Fallback=*/0.0f) : 0.0f;
}

bool UCataclysmShadowImages::IsAMovementSkillUse(const AActor* Character, const FCataclysmSkillUsedNotice& Notice)
{
	if (Notice.Slot == ECataclysmAbilitySlot::Movement)
	{
		return true;
	}
	FCataclysmWeaponSkill Held;
	return UCataclysmTriggeredSkill::HeldSkillNamed(Character, Notice.SkillName, Held)
		&& UCataclysmWeaponSkills::DesignedSlotOf(Held) == ECataclysmAbilitySlot::Movement;
}

ACataclysmShadowImage* UCataclysmShadowImages::StandingAfterimageOf(const AActor* Character)
{
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	const double Now = CataclysmShadowImagesState::NowIn(World);
	for (TActorIterator<ACataclysmShadowImage> It(World); It; ++It)
	{
		ACataclysmShadowImage* Image = *It;
		if (IsValid(Image) && Image->Maker.Get() == Character && Image->Kind == ECataclysmShadowImageKind::Afterimage
			&& !Image->bHoldsAnAttack && !Image->HasExpired(Now))
		{
			return Image;
		}
	}
	return nullptr;
}

ACataclysmShadowImage* UCataclysmShadowImages::LeaveAfterimage(AActor* Character, const FVector& Where,
															   const FGameplayTagContainer& SkillTags)
{
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!World || StatOf(Character, AfterimageStat, SkillTags) <= 0.0f)
	{
		return nullptr;
	}
	// ONE AT A TIME: the standing one is replaced by the new one.
	if (ACataclysmShadowImage* Standing = StandingAfterimageOf(Character))
	{
		Standing->Destroy();
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACataclysmShadowImage* Image =
		World->SpawnActor<ACataclysmShadowImage>(Where, FRotator(0.0f, Character->GetActorRotation().Yaw, 0.0f), Spawn);
	if (!Image)
	{
		return nullptr;
	}
	Image->Maker = Character;
	Image->Kind = ECataclysmShadowImageKind::Afterimage;
	Image->ExpiresAtSeconds = CataclysmShadowImagesState::NowIn(World) + AfterimageWaitsSeconds;
	// THE POSE THE CHARACTER HELD AS IT LEFT, and kept.
	Image->TakeTheMakersPose();
	UE_LOG(LogCataclysm, Display, TEXT("%s left an afterimage at (%.0f, %.0f, %.0f), waiting %.0f seconds for the next "
									   "attack."),
		   *GetNameSafe(Character), Where.X, Where.Y, Where.Z, AfterimageWaitsSeconds);
	return Image;
}

bool UCataclysmShadowImages::IsAnAttack(const FCataclysmWeaponSkill& Skill)
{
	const FGameplayTag Spell = FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Spell")), /*ErrorIfNotFound=*/false);
	return UCataclysmTriggeredSkill::RepeatsFromARow(Skill) && !(Spell.IsValid() && Skill.Tags.HasTag(Spell));
}

ACataclysmShadowImage* UCataclysmShadowImages::GiveTheAttackToTheAfterimage(AActor* Character,
																			 const FCataclysmSkillUsedNotice& Notice,
																			 bool bBasicAttack)
{
	ACataclysmShadowImage* Image = StandingAfterimageOf(Character);
	if (!Image)
	{
		return nullptr;
	}
	FCataclysmWeaponSkill Used;
	if (bBasicAttack)
	{
		const FCataclysmWeaponSkill* Basic = UCataclysmTriggeredSkill::HeldBasicAttack(Character);
		if (!Basic)
		{
			return nullptr;
		}
		Used = *Basic;
	}
	else if (!UCataclysmTriggeredSkill::HeldSkillNamed(Character, Notice.SkillName, Used))
	{
		return nullptr;
	}
	if (!IsAnAttack(Used))
	{
		return nullptr;
	}
	Image->bHoldsAnAttack = true;
	Image->AttackToRepeat = Used;
	Image->AttackAim = Notice.Aim;
	return Image;
}

int32 UCataclysmShadowImages::MakeAfterimageRepeats(const AActor* Character)
{
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!World)
	{
		return 0;
	}
	TArray<ACataclysmShadowImage*> Holding;
	for (TActorIterator<ACataclysmShadowImage> It(World); It; ++It)
	{
		if (IsValid(*It) && It->Maker.Get() == Character && It->bHoldsAnAttack)
		{
			Holding.Add(*It);
		}
	}
	int32 Started = 0;
	for (ACataclysmShadowImage* Image : Holding)
	{
		Started += Image->RepeatTheAttack() ? 1 : 0;
	}
	return Started;
}

// ==========================================================================
// Shadow Clone
// ==========================================================================

int32 UCataclysmShadowImages::EliteKillsTowardTheNextClone(const AActor* Killer)
{
	const int32* Held = CataclysmShadowImagesState::EliteKills().Find(TWeakObjectPtr<const AActor>(Killer));
	return Held ? *Held : 0;
}

TArray<ACataclysmShadowImage*> UCataclysmShadowImages::ClonesOf(const AActor* Character)
{
	TArray<ACataclysmShadowImage*> Found;
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!World)
	{
		return Found;
	}
	for (TActorIterator<ACataclysmShadowImage> It(World); It; ++It)
	{
		if (IsValid(*It) && It->Maker.Get() == Character && It->Kind == ECataclysmShadowImageKind::Clone)
		{
			Found.Add(*It);
		}
	}
	return Found;
}

ACataclysmShadowImage* UCataclysmShadowImages::NoteKillForShadowClone(AActor* Killer, const AActor* Victim)
{
	UWorld* World = Killer ? Killer->GetWorld() : nullptr;
	const int32 Every = FMath::FloorToInt(StatOf(Killer, CloneEveryEliteKillsStat));
	const float Seconds = StatOf(Killer, CloneSecondsStat);
	// "OF AN ELITE OR STRONGER": rarity rung 1 and up, what the creature is (as `target_rarity_at_least` reads it).
	const ACataclysmEnemyCharacter* Slain = Cast<ACataclysmEnemyCharacter>(Victim);
	if (!World || Every <= 0 || Seconds <= 0.0f || !Slain || Slain->RarityStep < 1)
	{
		return nullptr;
	}
	TMap<TWeakObjectPtr<const AActor>, int32>& Counts = CataclysmShadowImagesState::EliteKills();
	for (auto It = Counts.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	int32& Count = Counts.FindOrAdd(TWeakObjectPtr<const AActor>(Killer));
	if (++Count < Every)
	{
		return nullptr;
	}
	Count = 0;

	// BESIDE THE KILLER, a metre to its right (a judgement: "leaves a clone of you").
	const FVector Where = Killer->GetActorLocation() + Killer->GetActorRightVector() * 100.0f;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACataclysmShadowImage* Clone =
		World->SpawnActor<ACataclysmShadowImage>(Where, FRotator(0.0f, Killer->GetActorRotation().Yaw, 0.0f), Spawn);
	if (!Clone)
	{
		return nullptr;
	}
	Clone->Maker = Killer;
	Clone->Kind = ECataclysmShadowImageKind::Clone;
	Clone->ExpiresAtSeconds = CataclysmShadowImagesState::NowIn(World) + Seconds;
	Clone->TakeTheMakersPose();
	UE_LOG(LogCataclysm, Display, TEXT("%s's third Elite or stronger kill left a clone, fighting for %.0f seconds."),
		   *GetNameSafe(Killer), Seconds);
	return Clone;
}

// ==========================================================================
// Shadow Step
// ==========================================================================

AActor* UCataclysmShadowImages::ShadowStepTarget(const AActor* Mover, const FVector& Aim,
												 const FGameplayTagContainer& SkillTags)
{
	const UWorld* World = Mover ? Mover->GetWorld() : nullptr;
	const float ReachCm = StatOf(Mover, ShadowStepMetresStat, SkillTags) * 100.0f;
	if (!World || ReachCm <= 0.0f)
	{
		return nullptr;
	}
	const FVector Here = Mover->GetActorLocation();
	AActor* Chosen = nullptr;
	float Nearest = TNumericLimits<float>::Max();
	// EVERY ENEMY WITHIN THE ROW'S METRES OF THE MOVER, the one aimed at being whose body is nearest the aimed point.
	for (AActor* Enemy : UCataclysmTargeting::FindEnemiesInSphere(World, Mover, Here, ReachCm))
	{
		if (UCataclysmSkillEffects::IsDead(Enemy) || FVector::Dist2D(Here, Enemy->GetActorLocation()) > ReachCm)
		{
			continue;
		}
		const float FromAim = FMath::Max(0.0f, static_cast<float>(FVector::Dist2D(Aim, Enemy->GetActorLocation()))
												   - UCataclysmTargeting::BodyRadiusOf(Enemy));
		if (FromAim > AimAllowanceCm || FromAim >= Nearest)
		{
			continue;
		}
		// "THAT YOU CAN SEE": nothing on the visibility channel between the two middles, the two bodies left out, as a
		// trade asks it of a creature commanded.
		FCollisionQueryParams Query(SCENE_QUERY_STAT(CataclysmShadowStepSight), /*bInTraceComplex=*/false, Mover);
		Query.AddIgnoredActor(Enemy);
		FHitResult Blocked;
		if (World->LineTraceSingleByChannel(Blocked, Here, Enemy->GetActorLocation(), ECC_Visibility, Query))
		{
			continue;
		}
		Nearest = FromAim;
		Chosen = Enemy;
	}
	return Chosen;
}

// ==========================================================================
// Flurry of the Dead
// ==========================================================================

int32 UCataclysmShadowImages::StrikeAgain(AActor* Instigator, AActor* Target, float DamagePercent,
										  const FGameplayTagContainer& SkillTags, const FCataclysmHitDelivery& Delivery)
{
	if (!Instigator || !Target || DamagePercent <= 0.0f)
	{
		return 0;
	}
	// READ ONCE, BEFORE THE FIRST: Fervour an extra strike earns does not add strikes to this hit.
	const int32 Times = FMath::Max(0, FMath::FloorToInt(StatOf(Instigator, ExtraStrikesStat, SkillTags)));
	int32 Dealt = 0;
	FCataclysmHitDelivery Again = Delivery;
	Again.bIsARowsAnswerToAnEvent = true;
	for (int32 Strike = 0; Strike < Times; ++Strike)
	{
		if (UCataclysmSkillEffects::IsDead(Target) || UCataclysmSkillEffects::IsDead(Instigator))
		{
			break;
		}
		UCataclysmSkillEffects::ApplyHit(Instigator, Target, DamagePercent, SkillTags, Again);
		++Dealt;
	}
	return Dealt;
}
