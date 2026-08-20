// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmHellhoundCharacter.h"
#include "Cataclysm.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/SoftObjectPath.h"

// THE ONE MESH IN THE PACK, AND IT IS TWO CREATURES. Iggy is a goblin who rides
// Scorch, and there is no separate Scorch mesh to load: the pack holds one
// skeletal mesh and one skeleton for the pair. The header says what follows from
// that; in short, this creature currently wears its rider.
const TCHAR* ACataclysmHellhoundCharacter::BodyMeshPath =
	TEXT("/Game/ParagonIggyScorch/Characters/Heroes/IggyScorch/"
		 "Meshes/IggyScorch.IggyScorch");

// `Scorch_Primary_Fire_Med`, NOT `Scorch_Primary_Fire_Fast`. Measured at 0.9667
// and 0.5333 seconds by tools/probe_hellhound_animation.py on 2026-08-20. The
// designed interval between bites is 1.1 seconds, so the medium one fills it
// with 0.13 seconds of rest and the fast one would leave 0.57 seconds of the
// creature standing still between every bite.
//
// THEY ARE THE ONLY TWO ANIMATIONS SCORCH HAS OF ITS OWN. Of the pack's 144,
// exactly two carry the `Scorch_` prefix and are not aim offsets; every other
// clip this creature plays drives the rider as well.
const TCHAR* ACataclysmHellhoundCharacter::MaulAnimationPath =
	TEXT("/Game/ParagonIggyScorch/Characters/Heroes/IggyScorch/"
		 "Animations/Scorch_Primary_Fire_Med.Scorch_Primary_Fire_Med");

const TCHAR* ACataclysmHellhoundCharacter::IdleAnimationPath =
	TEXT("/Game/ParagonIggyScorch/Characters/Heroes/IggyScorch/"
		 "Animations/IggyScorch_Idle.IggyScorch_Idle");

// `Jog_Fwd`, WHICH IS THE FASTEST WALKING CLIP THE PACK HAS. `Travelmode_Fwd`
// was measured as the alternative and is slower -- 268.1 cm/s against 302.6 --
// so there is nothing to switch to if the play rate this creature needs turns
// out to look wrong. See AuthoredJogSpeedCmPerSecond in the header.
const TCHAR* ACataclysmHellhoundCharacter::JogAnimationPath =
	TEXT("/Game/ParagonIggyScorch/Characters/Heroes/IggyScorch/"
		 "Animations/Jog_Fwd.Jog_Fwd");

// TWO DEATHS, DRAWN PER DEATH, the same arrangement the Abyssal Warden has.
// Both measured at 1.6667 seconds, which is inside
// UCataclysmEnemyDeath::LongestCorpseSeconds with 2.3 seconds to spare.
// WHAT PLAYS WHILE THE LANE IS ON THE FLOOR. Nothing in the pack is a charge,
// so this is the closest motion it has: the creature rearing to breathe fire.
// `game/docs/enemy-source-assets.md` already said so before this creature was
// built -- "the closest existing motion to the fire trail the design gives this
// enemy".
//
// 1.1333 SECONDS INSIDE AN 0.83 SECOND WIND-UP, so it is compressed to a play
// rate of 1.366. That is the only clip on this creature that is compressed at
// all, and it is inside the 2.5 ceiling with room to spare.
const TCHAR* ACataclysmHellhoundCharacter::HellrushAnimationPath =
	TEXT("/Game/ParagonIggyScorch/Characters/Heroes/IggyScorch/"
		 "Animations/R_Ability_FireBreath_Start.R_Ability_FireBreath_Start");

const TCHAR* ACataclysmHellhoundCharacter::FirstDeathAnimationPath =
	TEXT("/Game/ParagonIggyScorch/Characters/Heroes/IggyScorch/"
		 "Animations/Death_Front.Death_Front");

const TCHAR* ACataclysmHellhoundCharacter::SecondDeathAnimationPath =
	TEXT("/Game/ParagonIggyScorch/Characters/Heroes/IggyScorch/"
		 "Animations/Death_Back.Death_Back");

/**
 * Seconds between bites, for tuning one while playing.
 *
 * THE SAME SHAPE THE ABYSSAL WARDEN'S OVERRIDES HAVE, and for the same reason:
 * a figure the design left to play cannot be tuned by rebuilding.
 */
static TAutoConsoleVariable<float> CVarHellhoundAttackInterval(
	TEXT("Cataclysm.Hellhound.AttackInterval"),
	0.0f,
	TEXT("Seconds between the Hellhound's bites. 0 uses its designed 1.1. Do "
		 "NOT go below 0.97: Scorch_Primary_Fire_Med is 0.9667 s and is not "
		 "rate-scaled, so a shorter interval starts a bite that has not "
		 "finished."),
	ECVF_Default);

/**
 * Seconds before Hellrush may be used again, for tuning one while playing.
 */
static TAutoConsoleVariable<float> CVarHellhoundHellrushCooldown(
	TEXT("Cataclysm.Hellhound.HellrushCooldown"),
	0.0f,
	TEXT("Seconds between the Hellhound's charges. 0 uses its designed 5.0. "
		 "Below about 1.0 the lane it left last time is still burning when the "
		 "next one starts, which is legal and looks like a corridor of fire."),
	ECVF_Default);

/**
 * How fast the charge travels, for tuning the one number on this creature that
 * was chosen rather than derived. See the header.
 */
static TAutoConsoleVariable<float> CVarHellhoundHellrushSpeed(
	TEXT("Cataclysm.Hellhound.HellrushSpeed"),
	0.0f,
	TEXT("Centimetres per second the Hellhound charges at. 0 uses its designed "
		 "1428.57, which crosses its 10 metre range in 0.7 seconds. Below 750 it "
		 "charges slower than it walks, which is worse than not charging."),
	ECVF_Default);

ACataclysmHellhoundCharacter::ACataclysmHellhoundCharacter()
{
	// UpdateLoopingAnimation RUNS FROM Tick and is what returns the mesh to a
	// resting pose after a bite and what puts the walk on while it moves.
	// `ACataclysmEnemyCharacter` already turns ticking on for every enemy, so
	// this is a second assignment of the same value, kept for the same reason
	// the Abyssal Warden keeps it: this creature needs ticking for a reason of
	// its own, and deleting the line would break silently if the base ever
	// stopped ticking.
	PrimaryActorTick.bCanEverTick = true;

	// WHICH ROW OF game/Data/EnemyArchetypes.csv THIS CREATURE IS. It is what
	// lets the panel that describes the creature under the cursor call it a
	// Hellhound. Nothing reads its stats out of that row yet.
	ArchetypeRow = TEXT("Hellhound");

	MeleeReachCm = DesignedMeleeReachCm;
	AttackIntervalSeconds = DesignedAttackIntervalSeconds;
	ResistancePercent = DesignedResistancePercent;
	CritChancePercent = DesignedCritChancePercent;
	CritMultiplierPercent = DesignedCritMultiplierPercent;
	EvasionPercent = DesignedEvasionPercent;
	EnergyShieldFraction = DesignedEnergyShieldFraction;
	NoticeRadiusCm = HellhoundNoticeRadiusCm;

	GetCapsuleComponent()->InitCapsuleSize(HellhoundCapsuleRadius,
										   HellhoundCapsuleHalfHeight);

	// NEITHER OF THESE IS SET BY ACataclysmEnemyCharacter. An enemy that does
	// not set MaxWalkSpeed here moves at Unreal's default 600 cm/s -- which for
	// this creature would be a silent SLOWING, since it is designed at 750, and
	// would quietly undo the one thing that makes it what it is.
	GetCharacterMovement()->MaxWalkSpeed = DesignedWalkSpeedCmPerSecond;
	GetCharacterMovement()->RotationRate =
		FRotator(0.0f, DesignedTurnRateDegreesPerSecond, 0.0f);
}

void ACataclysmHellhoundCharacter::BeginPlay()
{
	Super::BeginPlay();
	ResolveBody(/*bIncludeAnimation=*/true);

	// STANDING RATHER THAN THE REFERENCE POSE, from the first frame. Without
	// this the creature holds its bind pose until it first moves or bites.
	UpdateLoopingAnimation();
}

void ACataclysmHellhoundCharacter::Tick(float DeltaSeconds)
{
	// Super FIRST, because that is what advances a running Hellrush. The base
	// class owns the charge and this creature only starts one, so a Tick that
	// did its own work before calling up would read a position the charge had
	// not moved yet.
	Super::Tick(DeltaSeconds);
	UpdateLoopingAnimation();
}

float ACataclysmHellhoundCharacter::AttackIntervalSecondsInUse()
{
	const float Override = CVarHellhoundAttackInterval.GetValueOnAnyThread();
	return Override > 0.0f ? Override : DesignedAttackIntervalSeconds;
}

float ACataclysmHellhoundCharacter::HellrushCooldownSecondsInUse()
{
	const float Override = CVarHellhoundHellrushCooldown.GetValueOnAnyThread();
	return Override > 0.0f ? Override : HellrushCooldownSeconds;
}

float ACataclysmHellhoundCharacter::HellrushSpeedCmPerSecondInUse()
{
	const float Override = CVarHellhoundHellrushSpeed.GetValueOnAnyThread();
	return Override > 0.0f ? Override : HellrushSpeedCmPerSecond;
}

TArray<FCataclysmEnemyAbility>
ACataclysmHellhoundCharacter::EnemyAbilities() const
{
	FCataclysmEnemyAbility Hellrush;
	Hellrush.Name = TEXT("Hellrush");

	// NOT AT SOMETHING IT COULD SIMPLY WALK TO. A charge covering less ground
	// than the creature could walk during its own wind-up is strictly worse than
	// not winding up at all. The header holds the derivation in a static_assert
	// beside the constant, so the walk speed and the wind-up cannot move without
	// moving this.
	Hellrush.MinRangeCm = HellrushMinimumRangeCm;
	Hellrush.MaxRangeCm = HellrushRangeCm;

	// READ THROUGH THE OVERRIDE RATHER THAN OFF THE CONSTANT. This array is
	// rebuilt every time the brain asks, so a console variable set mid-fight
	// takes effect on the next thinking pass rather than needing a restart.
	Hellrush.CooldownSeconds = HellrushCooldownSecondsInUse();
	Hellrush.WindUpSeconds = HellrushWindUpSeconds;

	// A LANE, DRAWN FROM THE SAME HALF-WIDTH THE CHARGE HITS WITH, and the same
	// half-width the fire is left along. UseEnemyAbility below passes this one
	// constant to all three, so the corridor the player sees is the corridor
	// that hurts and the corridor that goes on burning.
	Hellrush.Shape = ECataclysmSkillShape::Movement;
	Hellrush.MarkerRadiusCm = HellrushRadiusCm;

	return {Hellrush};
}

void ACataclysmHellhoundCharacter::UseEnemyAbility(int32 Index,
												   AActor* /*Target*/,
												   const FVector& AimedAt)
{
	if (Index != HellrushAbility || !GetWorld())
	{
		return;
	}

	// WHERE IT STARTS FROM IS READ BEFORE IT MOVES, because the lane of fire is
	// left from here to there and BeginCharge starts the creature travelling.
	const FVector From = GetActorLocation();

	// TO THE POINT THE MARKER WAS DRAWN TO, WHICH IS THE END OF THE LANE AND NOT
	// THE TARGET. `ACataclysmEnemyController::AimPointFor` returns the far end of
	// the charge for a Movement ability, so this is handed the same point the
	// lane was drawn to.
	//
	// THE TARGET IS DELIBERATELY UNUSED. The creature is committed and runs the
	// full distance whether or not anything is still there; reading the target
	// here would be the charge following the player, which is exactly what makes
	// a telegraph unwalkable.
	//
	// AND IT SHOVES WHAT IT RUNS THROUGH. The third of the three enemy abilities
	// the design names as displacing the player, and the one issue #625 could not
	// build because this creature did not exist.
	BeginCharge(AimedAt, HellrushSpeedCmPerSecondInUse(), HellrushRadiusCm,
				HellrushDamagePercent, HellrushKnockbackCm);

	// AND THE LANE BURNS BEHIND IT.
	//
	// LAID ALONG THE WHOLE RUN RATHER THAN LEFT WHERE IT STOPS. The design says
	// the creature leaves "that lane on fire", and a patch at the far end would
	// be a different ability -- the one the player's Infernal Plunge has.
	//
	// FROM WHERE IT STARTED TO WHERE IT IS AIMED, which is not necessarily where
	// it ends up: a charge into a wall stops at the wall. The fire is laid now
	// rather than on arrival because the creature is still at the near end, and
	// because a lane that appeared only after the charge finished would burn
	// nobody who was standing in it during the run.
	//
	// AND IT BURNS EVERYTHING, INCLUDING THIS CREATURE. `GroundHitsAllies=1` in
	// the model: "The fire burns other enemies and the Hellhound itself." It is
	// the only thing in the game that does.
	//
	// PRICED ONCE, WHEN THE FIRE IS LAID, and priced off this creature's own
	// attack damage. A patch outlives the ability that left it, so reading the
	// creature's damage on every tick would make a lane keep paying for a buff
	// that has since expired. The same rule the player's ground effects follow,
	// in UCataclysmSkillTemplate::LeaveGroundAlong.
	//
	// A QUARTER OF A HIT PER SECOND FOR FOUR SECONDS is one whole hit for
	// standing in the entire lane, which is the same as being run over once.
	const float PerSecond =
		UCataclysmSkillEffects::WeaponDamageOf(
			UCataclysmTargeting::AbilitySystemOf(this))
		* HellrushGroundPercent / 100.0f;

	LastLaneLeftBurning = ACataclysmGroundZone::SpawnAlong(
		this, From, AimedAt, HellrushGroundRadiusCm, HellrushGroundSeconds,
		PerSecond, /*bBurnsEveryone=*/true);
}

void ACataclysmHellhoundCharacter::BeginEnemyAbilityWindUp(int32 Index, AActor*)
{
	if (Index != HellrushAbility)
	{
		return;
	}

	// THE CLIP RUNS DURING THE WIND-UP, NOT DURING THE TRAVEL, and that is worth
	// stating because it reads backwards at first. Every ability in this project
	// plays its animation across the telegraph and resolves at the end of it, and
	// a charge is no exception: the creature rears and gathers itself while the
	// lane is on the floor, then sets off.
	//
	// 1.1333 SECONDS COMPRESSED INTO 0.83, a play rate of 1.366. This is the only
	// clip on this creature that is compressed at all.
	PlayOneShot(HellrushAnimation.Get(), HellrushWindUpSeconds);
}

float ACataclysmHellhoundCharacter::SecondsBetweenAttacks() const
{
	return AttackIntervalSecondsInUse();
}

void ACataclysmHellhoundCharacter::AttackTarget(AActor* Target)
{
	Super::AttackTarget(Target);
	PlayMaulAnimation();
}

void ACataclysmHellhoundCharacter::PlayMaulAnimation()
{
	// ONE CLIP, NOT THREE. The Abyssal Warden's basic attack is a left swing, a
	// right swing and a recovery, because its swing clips end 151 cm away from
	// its idle and cutting straight back is visible. This creature's bite is one
	// clip that fits inside its interval, so there is nothing to queue.
	PlayOneShot(MaulAnimation.Get(), AttackIntervalSecondsInUse());
}

float ACataclysmHellhoundCharacter::JogPlayRate()
{
	if (AuthoredJogSpeedCmPerSecond <= 0.0f)
	{
		return 1.0f;
	}

	// THE RATIO OF WHAT IT MOVES AT TO WHAT THE CLIP WAS AUTHORED FOR. A planted
	// foot travels backwards at the clip's authored speed; the body travels
	// forwards at the designed speed; playing at the ratio makes the two cancel
	// and the foot stay put.
	//
	// FOR THIS CREATURE THAT IS 750 / 302.6 = 2.478, which is the highest rate
	// anything in this project uses and is 0.022 below the ceiling. The header
	// says what that means and why there is no faster clip to use instead.
	return FMath::Clamp(
		DesignedWalkSpeedCmPerSecond / AuthoredJogSpeedCmPerSecond,
		MinimumPlayRate, MaximumPlayRate);
}

void ACataclysmHellhoundCharacter::UpdateLoopingAnimation()
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
	// whichever loop it is in -- which looks exactly like the held final frame
	// this function exists to stop.
	if (Wanted == CurrentLoopingAnimation)
	{
		return;
	}

	CurrentLoopingAnimation = Wanted;
	MeshComponent->PlayAnimation(Wanted, /*bLooping=*/true);
	MeshComponent->SetPlayRate(bWalking ? JogPlayRate() : 1.0f);
}

float ACataclysmHellhoundCharacter::PlayOneShot(UAnimSequence* Animation,
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
	// same rule the Brute and the Abyssal Warden use, for the reason recorded
	// there: stretching a short clip across a long window was tried and read as
	// slow motion. A clip shorter than its window holds its last pose instead.
	//
	// NOTHING HERE NEEDS IT TODAY. The bite is 0.9667 seconds inside a 1.1
	// second interval, so the rate is 1. This exists so that replacing the clip
	// cannot silently start a movement the creature does not finish.
	// No window asked for means play it at its authored speed for as long as it
	// is.
	const float Hold = HoldSeconds > 0.0f ? HoldSeconds : Length;
	const float Rate = FMath::Clamp(FMath::Max(1.0f, Length / Hold),
									MinimumPlayRate, MaximumPlayRate);

	// NO ANIMATION BLUEPRINT EXISTS FOR THIS CREATURE, so this always takes the
	// single-node path today. The branch is kept because the Abyssal Warden's
	// has one written for it and this creature will want the same, and because
	// playing a clip straight onto a component that a graph is driving is the
	// bug that branch prevents.
	MeshComponent->PlayAnimation(Animation, /*bLooping=*/false);
	MeshComponent->SetPlayRate(Rate);

	// AND THE MESH IS OWED BACK WHEN IT FINISHES. A one-shot in single-node mode
	// plays once and then HOLDS ITS LAST FRAME forever. Recording when it ends
	// is what lets UpdateLoopingAnimation take the mesh back.
	if (const UWorld* World = GetWorld())
	{
		OneShotEndsAtSeconds = World->GetTimeSeconds() + Length / Rate;
	}

	// CLEARED SO THE LOOP RESTARTS AFTERWARDS. UpdateLoopingAnimation only acts
	// on a change, and without this the creature would still be "already
	// playing" the loop the bite interrupted, so nothing would put it back.
	CurrentLoopingAnimation = nullptr;

	return Length / Rate;
}

bool ACataclysmHellhoundCharacter::ResolveBody(bool bIncludeAnimation)
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
			TEXT("Hellhound art not found at %s, so it is keeping the "
				 "placeholder cylinder. This is expected without the Paragon "
				 "Iggy and Scorch pack; see game/docs/enemy-source-assets.md."),
			BodyMeshPath);
		return false;
	}

	MeshComponent->SetSkeletalMesh(Body);

	// FEET ON THE CAPSULE BOTTOM, AND THE ENGINE'S YAW. A skeletal mesh is
	// authored with its origin at the feet and the capsule's origin is its
	// centre, so the mesh drops by the half-height. The -90 degree yaw is the
	// engine's convention for character meshes, which face -Y while the actor
	// faces +X -- and the stride measurement found this rig's forward axis to be
	// -Y, which is what says the convention holds for it.
	MeshComponent->SetRelativeLocationAndRotation(
		FVector(0.0f, 0.0f, -HellhoundCapsuleHalfHeight),
		FRotator(0.0f, -90.0f, 0.0f));

	if (bIncludeAnimation)
	{
		MaulAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(MaulAnimationPath).TryLoad());
		HellrushAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(HellrushAnimationPath).TryLoad());
		IdleAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(IdleAnimationPath).TryLoad());
		JogAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(JogAnimationPath).TryLoad());

		// BOTH DEATH CLIPS, AND A NULL ENTRY IS KEPT rather than skipped. See
		// ACataclysmEnemyCharacter::PlayDeathAnimation: dropping one would
		// change how many clips there are and therefore which one is drawn.
		DeathAnimations.Reset();
		DeathAnimations.Add(Cast<UAnimSequence>(
			FSoftObjectPath(FirstDeathAnimationPath).TryLoad()));
		DeathAnimations.Add(Cast<UAnimSequence>(
			FSoftObjectPath(SecondDeathAnimationPath).TryLoad()));

		if (!MaulAnimation || !IdleAnimation || !JogAnimation)
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Hellhound animations missing: bite %s, idle %s, walk %s, "
				 "charge %s. "
					 "It will fight with nothing to show for it. This is "
					 "expected without the Paragon Iggy and Scorch pack."),
				MaulAnimation ? TEXT("found") : TEXT("MISSING"),
				IdleAnimation ? TEXT("found") : TEXT("MISSING"),
				JogAnimation ? TEXT("found") : TEXT("MISSING"),
			HellrushAnimation ? TEXT("found") : TEXT("MISSING"));
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

	UE_LOG(LogCataclysm, Verbose, TEXT("Hellhound is wearing %s."), BodyMeshPath);
	return true;
}
