// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmCorruptedSentinelCharacter.h"
#include "Cataclysm.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPath.h"

// THE SIEGE LANE MINION. The Imp is played by the MELEE lane minion out of the
// same pack; they are separate meshes on separate skeletons, so the two
// creatures share a folder and nothing else.
const TCHAR* ACataclysmCorruptedSentinelCharacter::BodyMeshPath =
	TEXT("/Game/ParagonMinions/Characters/Minions/Down_Minions/"
		 "Meshes/Minion_Lane_Siege_Dawn.Minion_Lane_Siege_Dawn");

const TCHAR* ACataclysmCorruptedSentinelCharacter::AnimationFolder =
	TEXT("/Game/ParagonMinions/Characters/Minions/Down_Minions/"
		 "Animations/Siege");

// `Idle_Planted`, THE ROOTED IDLE, AND IT IS ONE POSE. 0.0333 seconds, which is
// a single frame at 30 frames a second. The pack also ships `Idle` at 7.40
// seconds, which is the creature standing up; this creature never stands up.
const TCHAR* ACataclysmCorruptedSentinelCharacter::IdleAnimationName =
	TEXT("Idle_Planted");

// THE TWO ROOTED FIRING CLIPS, ALTERNATED. `game/docs/enemy-source-assets.md`
// said before this creature was built that alternating them is what stops
// continuous fire reading as one clip looping, and at 2.40 seconds played into a
// 2.00 second interval there is no gap between one shot and the next at all.
const TCHAR* ACataclysmCorruptedSentinelCharacter::FireAnimationNames
	[FireAnimationCount] = {
	TEXT("Fire_Planted"),
	TEXT("Fire_Planted_B"),
};

// EIGHT DEATHS, THE MOST IN THE PROJECT. Measured 2026-08-20 at 0.6333, 0.4667,
// 0.4667, 0.4333, 0.5000, 0.4667, 0.6667 and 0.7667 seconds, every one of them
// well inside UCataclysmEnemyDeath::LongestCorpseSeconds.
const TCHAR* ACataclysmCorruptedSentinelCharacter::DeathAnimationNames
	[DeathAnimationCount] = {
	TEXT("Death_A"), TEXT("Death_B"), TEXT("Death_C"), TEXT("Death_D"),
	TEXT("Death_E"), TEXT("Death_F"), TEXT("Death_G"), TEXT("Death_H"),
};

namespace
{
	/** Where one clip in this creature's folder lives, in full.
	 *
	 *  THE FORMAT STRING IS A LITERAL HERE AND HAS TO BE. Unreal 5.8's
	 *  `FString::Printf` takes a `TCheckedFormatString`, which cannot be built
	 *  from a `const TCHAR*` variable. */
	FString ClipPathIn(const TCHAR* Folder, const TCHAR* Name)
	{
		return FString::Printf(TEXT("%s/%s.%s"), Folder, Name, Name);
	}
}

/**
 * Seconds between shots, for tuning one while playing.
 */
static TAutoConsoleVariable<float> CVarSentinelAttackInterval(
	TEXT("Cataclysm.Sentinel.AttackInterval"),
	0.0f,
	TEXT("Seconds between the Corrupted Sentinel's shots. 0 uses its designed "
		 "2.0. Below about 1.0 the firing clip has to be played faster than the "
		 "2.5 ceiling allows and is clamped, so one shot is still playing when "
		 "the next begins. Remember that its telegraph is half its interval, so "
		 "shortening this shortens the warning too."),
	ECVF_Default);

/**
 * Seconds before the mortar may be used again.
 */
static TAutoConsoleVariable<float> CVarSentinelMortarCooldown(
	TEXT("Cataclysm.Sentinel.MortarCooldown"),
	0.0f,
	TEXT("Seconds between the Corrupted Sentinel's mortar shells. 0 uses its "
		 "designed 8.0. Below about 2.6 the shell's own 1.26 second telegraph "
		 "stops fitting inside half its cycle, which is the rule that keeps a "
		 "marker walkable."),
	ECVF_Default);

ACataclysmCorruptedSentinelCharacter::ACataclysmCorruptedSentinelCharacter()
{
	// UpdateLoopingAnimation RUNS FROM Tick and is what returns the mesh to its
	// rooted pose after a shot.
	PrimaryActorTick.bCanEverTick = true;

	// UNDERSCORED, BECAUSE THAT IS WHAT THE TABLE'S ROW IS CALLED.
	// `game/Data/EnemyArchetypes.csv` names this row `Corrupted_Sentinel`, and
	// the four creatures built before this one are all single words so none of
	// them met it. A row name that does not match makes the panel that describes
	// the creature under the cursor fall back to
	// `UCataclysmCreaturePanel::UnnamedCreature`.
	ArchetypeRow = TEXT("Corrupted_Sentinel");

	// ITS "MELEE REACH" IS ITS SHOT'S RANGE, AND THAT IS NOT A MISUSE.
	// `ACataclysmEnemyController::Think` treats the reach as the distance at
	// which the creature stops walking and starts attacking, and for a creature
	// that shoots 14 metres and cannot walk at all, that distance is 14 metres.
	// `ATTACK_REACH['Corrupted Sentinel']` in the model is 14.0 for the same
	// reason: it is the basic attack's own range.
	MeleeReachCm = SiegeBoltRangeCm;
	AttackIntervalSeconds = DesignedAttackIntervalSeconds;
	ResistancePercent = DesignedResistancePercent;
	CritChancePercent = DesignedCritChancePercent;
	CritMultiplierPercent = DesignedCritMultiplierPercent;
	EvasionPercent = DesignedEvasionPercent;
	EnergyShieldFraction = DesignedEnergyShieldFraction;
	NoticeRadiusCm = SentinelNoticeRadiusCm;

	GetCapsuleComponent()->InitCapsuleSize(SentinelCapsuleRadius,
										   SentinelCapsuleHalfHeight);

	// ZERO, AND IT IS THE ONLY ZERO IN THE ROSTER. An enemy that does not set
	// this moves at Unreal's default 600 cm/s, which would turn a turret into a
	// slow melee creature and remove the one thing it is for. It still TURNS,
	// which is what lets a rooted creature track a player who circles it.
	GetCharacterMovement()->MaxWalkSpeed = DesignedWalkSpeedCmPerSecond;
	GetCharacterMovement()->RotationRate =
		FRotator(0.0f, DesignedTurnRateDegreesPerSecond, 0.0f);
}

void ACataclysmCorruptedSentinelCharacter::BeginPlay()
{
	Super::BeginPlay();
	ResolveBody(/*bIncludeAnimation=*/true);
	UpdateLoopingAnimation();
}

void ACataclysmCorruptedSentinelCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateLoopingAnimation();
}

float ACataclysmCorruptedSentinelCharacter::AttackIntervalSecondsInUse()
{
	const float Override = CVarSentinelAttackInterval.GetValueOnAnyThread();
	return Override > 0.0f ? Override : DesignedAttackIntervalSeconds;
}

float ACataclysmCorruptedSentinelCharacter::BrimstoneMortarCooldownSecondsInUse()
{
	const float Override = CVarSentinelMortarCooldown.GetValueOnAnyThread();
	return Override > 0.0f ? Override : BrimstoneMortarCooldownSeconds;
}

float ACataclysmCorruptedSentinelCharacter::SecondsBetweenAttacks() const
{
	return AttackIntervalSecondsInUse();
}

void ACataclysmCorruptedSentinelCharacter::AttackTarget(AActor* /*Target*/)
{
	// NOTHING, AND THAT IS THE WHOLE POINT OF THIS OVERRIDE.
	//
	// The base class's AttackTarget applies direct damage at `MeleeReachCm`, and
	// this creature's reach is its SHOT's range of 14 metres. Left to the base
	// it would deal a free melee hit at fourteen metres every two seconds on top
	// of the bolt it is already firing.
	//
	// ITS BASIC ATTACK IS Siege Bolt, which is an entry in EnemyAbilities
	// because it is telegraphed. See the class comment for why that works
	// without new machinery, and why nothing here ever reaches this function
	// while a target is inside 14 metres: an ability off cooldown is chosen
	// before the reach test runs.
}

TArray<FCataclysmEnemyAbility>
ACataclysmCorruptedSentinelCharacter::EnemyAbilities() const
{
	FCataclysmEnemyAbility Mortar;
	Mortar.Name = TEXT("Brimstone Mortar");
	Mortar.MinRangeCm = BrimstoneMortarMinimumRangeCm;
	Mortar.MaxRangeCm = BrimstoneMortarRangeCm;
	Mortar.CooldownSeconds = BrimstoneMortarCooldownSecondsInUse();
	Mortar.WindUpSeconds = BrimstoneMortarWindUpSeconds;

	// A CIRCLE WHERE IT LANDS, NOT A LANE ALONG THE WAY, because it is lobbed.
	// The shell rises over everything between the creature and its target and
	// endangers only the ground it comes down on. Issue #459 is where the Brute
	// learned that; this is the same shape.
	Mortar.Shape = ECataclysmSkillShape::Projectile;
	Mortar.bArcsOntoItsTarget = true;
	Mortar.MarkerRadiusCm = BrimstoneMortarRadiusCm;

	FCataclysmEnemyAbility Bolt;
	Bolt.Name = TEXT("Siege Bolt");

	// NO MINIMUM RANGE, AND THE DESIGN STATES NONE. The mortar has one because
	// it marks a circle where it lands and below `radius + own body` that circle
	// covers the creature's own feet. A flat shot marks a LANE, and a lane
	// starts at the creature whatever the distance, so the same argument does
	// not apply and there is nothing to derive.
	Bolt.MinRangeCm = 0.0f;
	Bolt.MaxRangeCm = SiegeBoltRangeCm;

	// ZERO, WHICH IS WHAT MAKES IT THE BASIC ATTACK. The creature's own attack
	// interval is the only thing spacing it out, and
	// `ACataclysmEnemyController::UseAbilitiesOn` applies that to every ability.
	Bolt.CooldownSeconds = SiegeBoltCooldownSeconds;
	Bolt.WindUpSeconds = SiegeBoltWindUpSeconds;

	// A LANE, because it travels flat and hits what it passes.
	Bolt.Shape = ECataclysmSkillShape::Projectile;
	Bolt.bArcsOntoItsTarget = false;
	Bolt.MarkerRadiusCm = SiegeBoltRadiusCm;

	// ORDER IS PRIORITY, AND THE MORTAR HAS TO COME FIRST. See the enumeration
	// in the header: a zero-cooldown ability at the front of this array is the
	// only thing the creature would ever do.
	return {Mortar, Bolt};
}

void ACataclysmCorruptedSentinelCharacter::BeginEnemyAbilityWindUp(
	int32 Index, AActor*)
{
	if (Index != BrimstoneMortarAbility && Index != SiegeBoltAbility)
	{
		return;
	}

	// THE CLIP RUNS ACROSS THE WHOLE INTERVAL, NOT ACROSS THE WIND-UP, AND THIS
	// CREATURE IS THE ONLY ONE LIKE THAT.
	//
	// Every other creature here compresses its wind-up clip into the telegraph,
	// because the clip IS the wind-up and the attack lands as it ends. A firing
	// animation is not shaped that way: it aims, fires, and recovers, so the
	// shot leaves part way through and the rest is the creature settling.
	//
	// Issue #369 already decided what rate to play it at -- "an enemy's attack
	// animation is played to fit its designed attack interval" -- which for 2.40
	// seconds into 2.00 is 1.20. Passing the interval as the window is that
	// decision, and it means the clip finishes exactly as the next shot begins.
	//
	// SO THE BOLT LEAVES HALF WAY THROUGH THE CLIP. The wind-up is 1.0 second of
	// a 2.0 second window, and the shell leaves at 1.26 of it. **Whether the
	// visible muzzle flash agrees with either is unknown**: issue #478 is that
	// the release moment inside `Fire_Planted` has never been measured, and
	// `tools/measure_sentinel_release.py` records two ways of measuring it that
	// both failed their control.
	PlayFireAnimation();
}

void ACataclysmCorruptedSentinelCharacter::UseEnemyAbility(
	int32 Index, AActor* /*Target*/, const FVector& AimedAt)
{
	if (!GetWorld())
	{
		return;
	}

	// FROM THE MIDDLE OF THE CREATURE, WHICH IS A KNOWN APPROXIMATION. The Brute
	// fires from the bone its rock hangs off, because issue #454 found a rock
	// appearing at its waist while the animation threw it overhead. The same
	// question is open here and cannot be answered yet: issue #478 could not
	// even establish which bone carries this creature's muzzle, because three of
	// the four weapon bones are not animated by its firing clips at all.
	const FVector From = GetActorLocation();

	// NO TAGS, WHICH IS WHAT EVERY ENEMY PROJECTILE PASSES TODAY. Tags scope the
	// caster's own stat modifiers, and an enemy carries none.
	const FGameplayTagContainer NoTags;

	if (Index == SiegeBoltAbility)
	{
		// AIMED WHERE IT WAS MARKED. AimedAt is where the target stood when the
		// wind-up began, so a player who stepped out of the lane is not hit --
		// which is the whole of what a telegraph buys.
		//
		// A SPEED AND NO FLIGHT TIME, so it travels flat. At 1400 cm/s it
		// crosses its whole 14 metre range in exactly one second, so a player
		// who leaves the lane after the marker goes still has time.
		LastShotFired = ACataclysmProjectile::Fire(
			this, From, AimedAt, SiegeBoltRadiusCm,
			SiegeBoltSpeedCmPerSecond, SiegeBoltPierce, /*bInReturns=*/false,
			SiegeBoltDamagePercent, NoTags, /*bInBurns=*/false);
		return;
	}

	if (Index == BrimstoneMortarAbility)
	{
		// A FLIGHT TIME AND NO SPEED AT ALL, so it lobs. A ballistic shot has no
		// single speed -- it is slowest at the top of its arc and fastest as it
		// lands -- so the projectile is told how long it has and works the rest
		// out. Issue #465. The zero here is not a beam: a projectile is a beam
		// when it is given NEITHER a speed nor a flight time.
		LastShotFired = ACataclysmProjectile::Fire(
			this, From, AimedAt, BrimstoneMortarRadiusCm, /*InSpeed=*/0.0f,
			BrimstoneMortarPierce, /*bInReturns=*/false,
			BrimstoneMortarDamagePercent, NoTags, /*bInBurns=*/false,
			/*InBodyMesh=*/nullptr, BrimstoneMortarFlightSecondsFor(AimedAt));
		return;
	}
}

float ACataclysmCorruptedSentinelCharacter::BrimstoneMortarFlightSecondsFor(
	const FVector& LandsAt) const
{
	// A PARABOLA SAGS g * t * t / 8 BELOW ITS OWN CHORD, so a sag of
	// `fraction * range` is in the air for `sqrt(8 * fraction * range / g)`.
	// Inverting it here rather than stating a time is issue #474 on the Brute: a
	// stated time fixes the whole vertical part of the trajectory whatever the
	// distance, which made every short lob a near-vertical mortar.
	//
	// MEASURED ACROSS THE GROUND, because that is what the fraction is a
	// fraction of.
	const float ApexCm = BrimstoneMortarApexFraction
					   * static_cast<float>(FVector::Dist2D(GetActorLocation(),
															LandsAt));
	if (ApexCm <= 0.0f)
	{
		return 0.0f;
	}

	return FMath::Sqrt(
		8.0f * ApexCm / ACataclysmProjectile::LobGravityCmPerSecondSquared);
}

float ACataclysmCorruptedSentinelCharacter::FirePlayRate()
{
	const float Interval = AttackIntervalSecondsInUse();
	if (Interval <= 0.0f)
	{
		return 1.0f;
	}

	// NEVER SLOWER THAN AUTHORED, ONLY FASTER. The same rule every creature
	// here uses. 2.40 seconds into a 2.00 second interval is 1.20, which is the
	// gentlest rate scaling in the project.
	return FMath::Clamp(FMath::Max(1.0f, FireAnimationSeconds / Interval),
						MinimumPlayRate, MaximumPlayRate);
}

void ACataclysmCorruptedSentinelCharacter::PlayFireAnimation()
{
	if (FireAnimations.IsEmpty())
	{
		return;
	}

	// ALTERNATED RATHER THAN DRAWN. Two clips drawn at random repeat about half
	// the time, which is exactly the "one clip looping" the pack's two firing
	// clips exist to avoid.
	const int32 Index = NextFireAnimation % FireAnimations.Num();
	NextFireAnimation = (NextFireAnimation + 1) % FireAnimations.Num();

	PlayOneShot(FireAnimations[Index].Get(), AttackIntervalSecondsInUse());
}

void ACataclysmCorruptedSentinelCharacter::UpdateLoopingAnimation()
{
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

	// THERE IS ONLY ONE LOOPING CLIP, BECAUSE THE CREATURE ONLY EVER DOES ONE
	// THING WHEN IT IS NOT SHOOTING. Every other creature here chooses between
	// standing and walking from its own velocity; this one cannot walk, so
	// there is nothing to choose.
	UAnimSequence* Wanted = IdleAnimation.Get();
	if (!Wanted || Wanted == CurrentLoopingAnimation)
	{
		return;
	}

	CurrentLoopingAnimation = Wanted;

	// LOOPED, EVEN THOUGH IT IS ONE FRAME. A single pose played once holds that
	// pose anyway, and looping it costs nothing and means the branch does not
	// have to know how long the clip is.
	MeshComponent->PlayAnimation(Wanted, /*bLooping=*/true);
	MeshComponent->SetPlayRate(1.0f);
}

float ACataclysmCorruptedSentinelCharacter::PlayOneShot(UAnimSequence* Animation,
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

	const float Hold = HoldSeconds > 0.0f ? HoldSeconds : Length;
	const float Rate = FMath::Clamp(FMath::Max(1.0f, Length / Hold),
									MinimumPlayRate, MaximumPlayRate);

	MeshComponent->PlayAnimation(Animation, /*bLooping=*/false);
	MeshComponent->SetPlayRate(Rate);

	if (const UWorld* World = GetWorld())
	{
		OneShotEndsAtSeconds = World->GetTimeSeconds() + Length / Rate;
	}

	// CLEARED SO THE ROOTED POSE COMES BACK AFTERWARDS. UpdateLoopingAnimation
	// only acts on a change.
	CurrentLoopingAnimation = nullptr;

	return Length / Rate;
}

bool ACataclysmCorruptedSentinelCharacter::ResolveBody(bool bIncludeAnimation)
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
			TEXT("Corrupted Sentinel art not found at %s, so it is keeping the "
				 "placeholder cylinder. This is expected without the Paragon "
				 "Minions pack; see game/docs/enemy-source-assets.md."),
			BodyMeshPath);
		return false;
	}

	MeshComponent->SetSkeletalMesh(Body);

	// FEET ON THE CAPSULE BOTTOM, AND THE ENGINE'S YAW for a character mesh,
	// which face -Y while the actor faces +X.
	MeshComponent->SetRelativeLocationAndRotation(
		FVector(0.0f, 0.0f, -SentinelCapsuleHalfHeight),
		FRotator(0.0f, -90.0f, 0.0f));

	if (bIncludeAnimation)
	{
		IdleAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(ClipPathIn(AnimationFolder, IdleAnimationName))
				.TryLoad());

		FireAnimations.Reset();
		for (const TCHAR* Name : FireAnimationNames)
		{
			FireAnimations.Add(Cast<UAnimSequence>(
				FSoftObjectPath(ClipPathIn(AnimationFolder, Name)).TryLoad()));
		}

		// A NULL ENTRY IS KEPT rather than skipped, for the reason
		// ACataclysmEnemyCharacter::PlayDeathAnimation gives: dropping one would
		// change how many clips there are and therefore which one is drawn.
		DeathAnimations.Reset();
		for (const TCHAR* Name : DeathAnimationNames)
		{
			DeathAnimations.Add(Cast<UAnimSequence>(
				FSoftObjectPath(ClipPathIn(AnimationFolder, Name)).TryLoad()));
		}

		int32 ShotsFound = 0;
		for (const TObjectPtr<UAnimSequence>& Clip : FireAnimations)
		{
			ShotsFound += Clip ? 1 : 0;
		}

		if (!IdleAnimation || ShotsFound == 0)
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Corrupted Sentinel animations missing: rooted idle %s, %d "
					 "of %d firing clips. It will shoot with nothing to show "
					 "for it. This is expected without the Paragon Minions "
					 "pack."),
				IdleAnimation ? TEXT("found") : TEXT("MISSING"),
				ShotsFound, FireAnimationCount);
		}
	}

	// OTHERWISE THE CYLINDER SITS INSIDE THE CREATURE.
	if (PlaceholderBody)
	{
		PlaceholderBody->SetVisibility(false);
	}

	UE_LOG(LogCataclysm, Verbose,
		   TEXT("Corrupted Sentinel is wearing %s."), BodyMeshPath);
	return true;
}
