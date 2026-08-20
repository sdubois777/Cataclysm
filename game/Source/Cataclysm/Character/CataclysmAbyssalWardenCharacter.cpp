// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Cataclysm.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
// For turning an ability's tag list into a container, the same way a
// player's skill row is read. Issue #519.
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/SoftObjectPath.h"

const TCHAR* ACataclysmAbyssalWardenCharacter::BodyMeshPath =
	TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Skins/Tier_2/"
		 "Grux_Beetle_Molten/Meshes/GruxMolten.GruxMolten");

// THE GENERATED CLASS, NOT THE ASSET, which is what the _C suffix is. Copied
// character for character from the Brute's, because without it
// TryLoadClass<UAnimInstance> returns null and the creature silently holds its
// reference pose. This asset does not exist yet; see the header.
const TCHAR* ACataclysmAbyssalWardenCharacter::AnimationBlueprintPath =
	TEXT("/Game/Enemies/Demonic/AbyssalWarden/"
		 "ABP_AbyssalWarden.ABP_AbyssalWarden_C");

// THE FAST VARIANTS, BECAUSE THEY JOIN AT 0.01 cm AND THE FULL-SPEED PAIR
// JOINS AT 12.57. Measured by tools/measure_warden_recovery.py on 2026-08-09.
// They are also the only pairing that fits a recovery inside the 2.4 second
// attack interval without compression: 2.100 s against 3.100 for the slow pair.
const TCHAR* ACataclysmAbyssalWardenCharacter::LeftSwingAnimationPath =
	TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Animations/"
		 "PrimaryAttack_LA_Fast.PrimaryAttack_LA_Fast");

const TCHAR* ACataclysmAbyssalWardenCharacter::RightSwingAnimationPath =
	TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Animations/"
		 "PrimaryAttack_RA_Fast.PrimaryAttack_RA_Fast");

// THE RIGHT RECOVERY AFTER A FAST RIGHT SWING, at a 16.24 cm join. The left
// recovery after a fast left swing is 80.73 cm, because the recoveries were
// authored against the FULL-SPEED swings and only this one follows a fast swing
// acceptably.
const TCHAR* ACataclysmAbyssalWardenCharacter::SwingRecoveryAnimationPath =
	TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Animations/"
		 "PrimaryAttack_RA_Recovery.PrimaryAttack_RA_Recovery");

const TCHAR* ACataclysmAbyssalWardenCharacter::MoltenRoarAnimationPath =
	TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Animations/"
		 "Ultimate_Roar.Ultimate_Roar");

// `Stampede`, NOT `Stampede_Knockup`. See the header: the knock-up variant is
// 1.5333 seconds against an 0.83 second wind-up, and nothing in this project can
// knock a target back yet.
const TCHAR* ACataclysmAbyssalWardenCharacter::StampedeAnimationPath =
	TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Animations/"
		 "Stampede.Stampede");

const TCHAR* ACataclysmAbyssalWardenCharacter::IdleAnimationPath =
	TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Animations/Idle.Idle");

// `Run_Fwd` MEASURES THE SAME 281.6 cm/s AS THIS CLIP, which is the same trap
// issue #386 records for Rampage: a pack realising a sprint by playing the jog
// faster rather than animating a second gait. Only the stride estimate has been
// compared, not the bone data, so that is a suspicion rather than a finding.
const TCHAR* ACataclysmAbyssalWardenCharacter::JogAnimationPath =
	TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Animations/Jog_Fwd.Jog_Fwd");

// THE TWO CLIPS THIS CREATURE DIES WITH. Measured in the editor on
// 2026-08-19: Death_A is 1.6667 seconds and Death_B is 1.6333. One is drawn
// per death and the body is kept for exactly that clip's length.
const TCHAR* ACataclysmAbyssalWardenCharacter::FirstDeathAnimationPath =
	TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Animations/"
		 "Death_A.Death_A");

const TCHAR* ACataclysmAbyssalWardenCharacter::SecondDeathAnimationPath =
	TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Animations/"
		 "Death_B.Death_B");

const FName ACataclysmAbyssalWardenCharacter::AttackSlotName =
	FName(TEXT("DefaultSlot"));

// --------------------------------------------------------------------------
// Console overrides
// --------------------------------------------------------------------------
//
// ZERO MEANS "USE THE DESIGNED FIGURE" for every one of these, which is the
// pattern the Brute's five follow. That is what lets a figure be tried in a
// play session and then abandoned by setting it back to zero, rather than
// needing the designed number remembered and retyped.

static TAutoConsoleVariable<float> CVarWardenAttackInterval(
	TEXT("Cataclysm.Warden.AttackInterval"),
	0.0f,
	TEXT("Seconds between the Abyssal Warden's swings. 0 uses its designed "
		 "2.4. Do NOT go below 1.14: PrimaryAttack_LA and PrimaryAttack_RA are "
		 "1.1333 s each and are not rate-scaled, so a shorter interval starts a "
		 "swing that has not finished. This does NOT affect Molten Roar, which "
		 "is telegraphed against its own 12 second cooldown."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarWardenRoarCooldown(
	TEXT("Cataclysm.Warden.RoarCooldown"),
	0.0f,
	TEXT("Seconds between the Abyssal Warden's Molten Roar. 0 uses its designed "
		 "12, which is five of its swings and is how long the creature takes to "
		 "kill the reference geared character. Below 5 it would come round "
		 "faster than a player's Movement skill recharges."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarWardenStampedeCooldown(
	TEXT("Cataclysm.Warden.StampedeCooldown"),
	0.0f,
	TEXT("Seconds between the Abyssal Warden's charge. 0 uses its designed 5, "
		 "which is the Movement slot's cooldown and the minimum any large "
		 "telegraph may run on -- below it the player faces a second lane with "
		 "their own Movement skill still recharging."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarWardenStampedeSpeed(
	TEXT("Cataclysm.Warden.StampedeSpeed"),
	0.0f,
	TEXT("How fast the Abyssal Warden charges, in centimetres per second. 0 "
		 "uses its designed 1142.9, which is its 8 metre range covered in the "
		 "0.700 second Stampede clip. Below 460 it cannot catch the fastest "
		 "class and the charge closes nothing; at its own 280 walking speed the "
		 "charge would take longer than its whole attack interval. This is the "
		 "figure the header labels a judgement, so it is the one to try."),
	ECVF_Default);

// --------------------------------------------------------------------------

// A RING AT ITS OWN FEET. Scorching Arc is the designed player skill of the
// same shape; this one leaves no burning ground, so it carries no
// Type.AOE.Persistent, and it does not stun, so it carries no Keyword.CC.
const TCHAR* ACataclysmAbyssalWardenCharacter::MoltenRoarTags =
	TEXT("Type.AOE.PointBlank, Type.Strike");

ACataclysmAbyssalWardenCharacter::ACataclysmAbyssalWardenCharacter()
{
	// ACataclysmCharacterBase TURNS TICKING OFF. `ACataclysmEnemyCharacter` now
	// turns it back on for every enemy, because a charge advances per frame, so
	// this line is a second assignment of the same value.
	//
	// IT IS KEPT RATHER THAN DELETED because this creature needs ticking for a
	// reason of its own that has nothing to do with charging:
	// UpdateLoopingAnimation runs from Tick and is what returns the mesh to a
	// resting pose after an attack and what puts the walk on while it moves.
	// Without it the creature holds the last frame of its swing until the next
	// one, which the project owner reported on 2026-08-09. Deleted, that would
	// break silently if the base ever stopped ticking.
	PrimaryActorTick.bCanEverTick = true;

	// WHICH ROW OF game/Data/EnemyArchetypes.csv THIS CREATURE IS. It is what
	// lets the hover panel call it an Abyssal Warden; see
	// ACataclysmEnemyCharacter::ArchetypeRow. Nothing reads the creature's stats
	// out of that row yet.
	ArchetypeRow = TEXT("Abyssal_Warden");

	MeleeReachCm = DesignedMeleeReachCm;
	AttackIntervalSeconds = DesignedAttackIntervalSeconds;
	ResistancePercent = DesignedResistancePercent;
	CritChancePercent = DesignedCritChancePercent;
	CritMultiplierPercent = DesignedCritMultiplierPercent;
	EvasionPercent = DesignedEvasionPercent;
	EnergyShieldFraction = DesignedEnergyShieldFraction;
	NoticeRadiusCm = WardenNoticeRadiusCm;

	GetCapsuleComponent()->InitCapsuleSize(WardenCapsuleRadius,
										   WardenCapsuleHalfHeight);

	// NEITHER OF THESE IS SET BY ACataclysmEnemyCharacter. It sets the rotation
	// mode and a 480 degree turn rate and stops. An enemy that does not set
	// MaxWalkSpeed here moves at Unreal's default 600 cm/s, which for this
	// creature would silently repair the very problem issue #491 describes: it
	// is designed to be unable to catch the player.
	GetCharacterMovement()->MaxWalkSpeed = DesignedWalkSpeedCmPerSecond;
	GetCharacterMovement()->RotationRate =
		FRotator(0.0f, DesignedTurnRateDegreesPerSecond, 0.0f);
}

void ACataclysmAbyssalWardenCharacter::BeginPlay()
{
	Super::BeginPlay();
	ResolveBody(/*bIncludeAnimation=*/true);

	// STANDING RATHER THAN THE REFERENCE POSE, from the first frame. Without
	// this the creature holds its bind pose until it first moves or attacks.
	UpdateLoopingAnimation();
}

void ACataclysmAbyssalWardenCharacter::Tick(float DeltaSeconds)
{
	// Super FIRST, because that is what advances a running charge. Issue #491.
	Super::Tick(DeltaSeconds);
	UpdateLoopingAnimation();
}

float ACataclysmAbyssalWardenCharacter::JogPlayRate()
{
	if (AuthoredJogSpeedCmPerSecond <= 0.0f)
	{
		return 1.0f;
	}

	// THE RATIO OF WHAT IT MOVES AT TO WHAT THE CLIP WAS AUTHORED FOR. A planted
	// foot travels backwards at the clip's authored speed; the body travels
	// forwards at the designed speed; playing at the ratio makes the two cancel
	// and the foot stay put. 280 / 281.6 is 0.994.
	return FMath::Clamp(
		DesignedWalkSpeedCmPerSecond / AuthoredJogSpeedCmPerSecond,
		MinimumPlayRate, MaximumPlayRate);
}

void ACataclysmAbyssalWardenCharacter::UpdateLoopingAnimation()
{
	// THE GRAPH OWNS THE MESH WHEN THERE IS ONE. Two things setting the same
	// component's animation would fight, and the graph is the better of the two
	// because it can blend. This whole function is the fallback for not having
	// one. Issue #387.
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

	// THE NEXT CLIP OF AN ATTACK COMES BEFORE ANY LOCOMOTION. This is what
	// turns three separate clips into one left-right-recover movement, and it
	// is what a montage would do in one asset if this creature had an animation
	// Blueprint to play one. Issue #387.
	if (AttackSequence.IsValidIndex(AttackSequenceIndex))
	{
		UAnimSequence* Next = AttackSequence[AttackSequenceIndex].Get();
		++AttackSequenceIndex;
		if (Next)
		{
			PlayOneShot(Next);
			return;
		}
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

float ACataclysmAbyssalWardenCharacter::AttackIntervalSecondsInUse()
{
	const float Override = CVarWardenAttackInterval.GetValueOnAnyThread();
	return Override > 0.0f ? Override : DesignedAttackIntervalSeconds;
}

float ACataclysmAbyssalWardenCharacter::MoltenRoarCooldownSecondsInUse()
{
	const float Override = CVarWardenRoarCooldown.GetValueOnAnyThread();
	return Override > 0.0f ? Override : MoltenRoarCooldownSeconds;
}

float ACataclysmAbyssalWardenCharacter::StampedeCooldownSecondsInUse()
{
	const float Override = CVarWardenStampedeCooldown.GetValueOnAnyThread();
	return Override > 0.0f ? Override : StampedeCooldownSeconds;
}

float ACataclysmAbyssalWardenCharacter::StampedeSpeedCmPerSecondInUse()
{
	const float Override = CVarWardenStampedeSpeed.GetValueOnAnyThread();
	return Override > 0.0f ? Override : StampedeSpeedCmPerSecond;
}

float ACataclysmAbyssalWardenCharacter::SecondsBetweenAttacks() const
{
	return AttackIntervalSecondsInUse();
}

void ACataclysmAbyssalWardenCharacter::AttackTarget(AActor* Target)
{
	Super::AttackTarget(Target);
	PlayAttackAnimation();
}

TArray<UAnimSequence*> ACataclysmAbyssalWardenCharacter::BasicAttackClips() const
{
	// LEFT, RIGHT, THEN BACK TO NEUTRAL. Whichever of the three are present;
	// on a machine without the Paragon Grux pack all are null and this returns
	// an empty list, which PlayAttackAnimation handles by playing nothing.
	TArray<UAnimSequence*> Clips;
	for (UAnimSequence* Clip : {LeftSwingAnimation.Get(),
								RightSwingAnimation.Get(),
								SwingRecoveryAnimation.Get()})
	{
		if (Clip)
		{
			Clips.Add(Clip);
		}
	}
	return Clips;
}

void ACataclysmAbyssalWardenCharacter::PlayAttackAnimation()
{
	// THE WHOLE COMBO IS QUEUED AND THE FIRST CLIP STARTS NOW.
	// UpdateLoopingAnimation starts each of the rest as the one before it ends,
	// and puts the creature back to standing or walking when the list runs out.
	AttackSequence.Reset();
	for (UAnimSequence* Clip : BasicAttackClips())
	{
		AttackSequence.Add(Clip);
	}
	AttackSequenceIndex = 0;

	UpdateLoopingAnimation();
}

float ACataclysmAbyssalWardenCharacter::PlayOneShot(UAnimSequence* Animation,
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

	// No window asked for means play it at its authored speed for as long as it
	// is.
	const float Hold = HoldSeconds > 0.0f ? HoldSeconds : Length;

	// NEVER SLOWER THAN AUTHORED, ONLY FASTER, AND ONLY WHEN IT MUST BE. The
	// same rule and the same two clamps as the Brute, for the reason recorded
	// there: stretching a short clip across a long window was tried and read as
	// slow motion. A clip shorter than its window holds its last pose instead.
	//
	// NOTHING HERE NEEDS IT TODAY. Ultimate_Roar is 1.4 seconds inside a 2.0
	// second wind-up and the swings are 1.1333 inside a 2.4 second interval, so
	// every rate is 1. This exists so that replacing a clip cannot silently
	// start a movement the creature does not finish.
	const float Rate = FMath::Clamp(FMath::Max(1.0f, Length / Hold),
									MinimumPlayRate, MaximumPlayRate);

	if (bAnimationBlueprintBound)
	{
		// THROUGH A SLOT, so the graph keeps driving locomotion underneath and
		// the swing blends in and out rather than cutting.
		if (UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance())
		{
			AnimInstance->PlaySlotAnimationAsDynamicMontage(
				Animation, AttackSlotName, AttackBlendInSeconds,
				AttackBlendOutSeconds, Rate, /*LoopCount=*/1);
		}
	}
	else
	{
		// THE FALLBACK, AND IT IS WHAT RUNS TODAY. With no animation Blueprint
		// there is no anim instance and therefore no slot to play into, so the
		// clip is played straight onto the component in single-node mode. The
		// attack is visible; what is lost is the blend, so the creature cuts
		// from its pose to the swing and back.
		MeshComponent->PlayAnimation(Animation, /*bLooping=*/false);
		MeshComponent->SetPlayRate(Rate);

		// AND THE MESH IS OWED BACK WHEN IT FINISHES. A one-shot in single-node
		// mode plays once and then HOLDS ITS LAST FRAME forever, which is what
		// the project owner reported on 2026-08-09: "at the end of his basic
		// attack animation he holds the final frame till he attacks again".
		// Recording when it ends is what lets UpdateLoopingAnimation take the
		// mesh back and put a resting pose on.
		if (const UWorld* World = GetWorld())
		{
			OneShotEndsAtSeconds = World->GetTimeSeconds() + Length / Rate;
		}

		// CLEARED SO THE LOOP RESTARTS AFTERWARDS. UpdateLoopingAnimation only
		// acts on a change, and without this the creature would still be
		// "already playing" the loop it was on before the swing interrupted it,
		// so nothing would put it back.
		CurrentLoopingAnimation = nullptr;
	}

	return Length / Rate;
}

TArray<FCataclysmEnemyAbility>
ACataclysmAbyssalWardenCharacter::EnemyAbilities() const
{
	FCataclysmEnemyAbility MoltenRoar;
	MoltenRoar.Name = TEXT("Molten Roar");

	// FROM ITS OWN FEET OUT, so no minimum: a target pressed against the
	// creature is inside the ring and should be hit by it. The same reasoning
	// the Brute's stomp records. The minimum-range rule that governs a lobbed
	// attack does not apply, because this marks a circle the caster stands at
	// the centre of rather than one it would be standing inside.
	MoltenRoar.MinRangeCm = 0.0f;
	MoltenRoar.MaxRangeCm = MoltenRoarRadiusCm;

	// READ THROUGH THE OVERRIDE RATHER THAN OFF THE CONSTANT. This array is
	// rebuilt every time the brain asks, so a console variable set mid-fight
	// takes effect on the next thinking pass rather than needing a restart.
	MoltenRoar.CooldownSeconds = MoltenRoarCooldownSecondsInUse();
	MoltenRoar.WindUpSeconds = MoltenRoarWindUpSeconds;

	// A RING ON THE GROUND, DRAWN FROM THE SAME CONSTANT THE DAMAGE USES.
	// UseEnemyAbility below sweeps at MoltenRoarRadiusCm, so the circle the
	// player sees is exactly the circle that hits them.
	MoltenRoar.Shape = ECataclysmSkillShape::Strike;
	MoltenRoar.MarkerRadiusCm = MoltenRoarRadiusCm;

	FCataclysmEnemyAbility Stampede;
	Stampede.Name = TEXT("Stampede");

	// NOT AT SOMETHING IT COULD SIMPLY WALK TO. The design's own test for
	// whether a charge is worth winding up for, stated in the Hellhound's
	// section: a charge that covers less ground than the creature could walk
	// during its own wind-up is strictly worse than not winding up at all.
	//
	// THE HEADER HOLDS THE DERIVATION, in a static_assert beside the constant,
	// so the walk speed and the wind-up cannot move without moving this.
	Stampede.MinRangeCm = StampedeMinimumRangeCm;

	Stampede.MaxRangeCm = StampedeRangeCm;
	Stampede.CooldownSeconds = StampedeCooldownSecondsInUse();
	Stampede.WindUpSeconds = StampedeWindUpSeconds;

	// A LANE, DRAWN FROM THE SAME HALF-WIDTH THE CHARGE HITS WITH.
	// UseEnemyAbility below passes StampedeRadiusCm to BeginCharge as the lane's
	// half-width, so the corridor the player sees is the corridor that hurts.
	Stampede.Shape = ECataclysmSkillShape::Movement;
	Stampede.MarkerRadiusCm = StampedeRadiusCm;

	// MOLTEN ROAR FIRST. Order is priority and ChooseAbility takes the first
	// entry that fits without looking at the shape, so the 12 second ring has to
	// come before the 5 second charge or it would almost never be reached in the
	// band where both are legal. See the enumeration in the header.
	return {MoltenRoar, Stampede};
}

void ACataclysmAbyssalWardenCharacter::UseEnemyAbility(int32 Index,
													   AActor* /*Target*/,
													   const FVector& AimedAt)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (Index == MoltenRoarAbility)
	{
		// EVERYTHING IN THE RING, FROM ITS OWN LOCATION. The same sweep the
		// marker was drawn from, so what was promised is what lands.
		//
		// IT DOES NOT STUN. The Brute's stomp is the one thing in this slice
		// that holds the player still, and a second would spend most of its uses
		// inside the five second stun immunity window.
		// PARSED ONCE RATHER THAN PER TARGET. This is the largest ring in the
		// game and catches a crowd.
		const FGameplayTagContainer AbilityTags =
			UCataclysmSkillShapes::TagsFromCell(MoltenRoarTags);

		for (AActor* Caught : UCataclysmTargeting::FindEnemiesInSphere(
				World, this, GetActorLocation(), MoltenRoarRadiusCm))
		{
			// AREA DAMAGE, so it cannot be evaded. This is the largest
			// telegraph in the game and it swept a sphere. Issue #513.
			UCataclysmSkillEffects::ApplyHit(this, Caught,
											 MoltenRoarDamagePercent, AbilityTags);
		}
		return;
	}

	if (Index == StampedeAbility)
	{
		// TO THE POINT THE MARKER WAS DRAWN TO, WHICH IS THE END OF THE LANE
		// AND NOT THE TARGET. `ACataclysmEnemyController::AimPointFor` returns
		// the far end of the charge for a Movement ability, so this is handed
		// the same point the lane was drawn to. One point rather than two that
		// have to agree -- the rule issue #471 established.
		//
		// THE TARGET IS DELIBERATELY UNUSED. The creature is committed and runs
		// the full distance whether or not anything is still there; reading the
		// target here would be the charge following the player, which is exactly
		// what makes a telegraph unwalkable.
		// AND IT SHOVES WHAT IT RUNS THROUGH, which is the second of the three
		// enemy abilities the design names as displacing the player. Issue #625.
		// The distance is passed in here rather than read inside the charge,
		// because it belongs to this ability: the Brute's Stomp shoves a
		// different distance for a stated reason.
		BeginCharge(AimedAt, StampedeSpeedCmPerSecondInUse(), StampedeRadiusCm,
					StampedeDamagePercent, StampedeKnockbackCm);
	}
}

void ACataclysmAbyssalWardenCharacter::BeginEnemyAbilityWindUp(int32 Index,
															   AActor*)
{
	if (Index == MoltenRoarAbility)
	{
		// THE CLIP IS SHORTER THAN THE WIND-UP AND THAT IS FINE. Ultimate_Roar
		// is 1.4000 seconds inside a 2.0 second telegraph, so it plays at its
		// authored speed and then holds its last pose for the remaining six
		// tenths. That is the same arrangement the Brute's ground smash uses and
		// it is the clearest warning the player gets: a creature poised with the
		// roar finished and the ring still on the floor.
		PlayOneShot(MoltenRoarAnimation);
		return;
	}

	if (Index == StampedeAbility)
	{
		// THE CLIP RUNS DURING THE WIND-UP, NOT DURING THE TRAVEL, and that is
		// worth stating because it reads backwards at first. Every ability in
		// this project plays its animation across the telegraph and resolves at
		// the end of it, and a charge is no exception: the creature digs in and
		// gathers itself while the lane is on the floor, then sets off.
		//
		// 0.700 SECONDS INSIDE 0.83, so it plays at its authored speed and holds
		// its last pose for the remaining tenth. PlayOneShot clamps a rate
		// upwards only, so nothing here is stretched.
		PlayOneShot(StampedeAnimation);
	}
}

bool ACataclysmAbyssalWardenCharacter::ResolveBody(bool bIncludeAnimation)
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
			TEXT("Abyssal Warden art not found at %s, so it is keeping the "
				 "placeholder cylinder. This is expected without the Paragon "
				 "Grux pack; see game/docs/enemy-source-assets.md."),
			BodyMeshPath);
		return false;
	}

	MeshComponent->SetSkeletalMesh(Body);

	// FEET ON THE CAPSULE BOTTOM, AND THE ENGINE'S YAW. A skeletal mesh is
	// authored with its origin at the feet and the capsule's origin is its
	// centre, so the mesh drops by the half-height. The -90 degree yaw is the
	// engine's convention for character meshes, which face -Y while the actor
	// faces +X. The same two lines the Brute uses.
	MeshComponent->SetRelativeLocationAndRotation(
		FVector(0.0f, 0.0f, -WardenCapsuleHalfHeight),
		FRotator(0.0f, -90.0f, 0.0f));

	if (bIncludeAnimation)
	{
		bAnimationBlueprintBound = ResolveAnimationBlueprint(MeshComponent);

		LeftSwingAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(LeftSwingAnimationPath).TryLoad());
		RightSwingAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(RightSwingAnimationPath).TryLoad());
		SwingRecoveryAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(SwingRecoveryAnimationPath).TryLoad());
		MoltenRoarAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(MoltenRoarAnimationPath).TryLoad());
		StampedeAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(StampedeAnimationPath).TryLoad());

		// BOTH DEATH CLIPS, AND A NULL ENTRY IS KEPT rather than skipped. See
		// ACataclysmEnemyCharacter::PlayDeathAnimation: dropping one would
		// change how many clips there are and therefore which one is drawn.
		DeathAnimations.Reset();
		DeathAnimations.Add(Cast<UAnimSequence>(
			FSoftObjectPath(FirstDeathAnimationPath).TryLoad()));
		DeathAnimations.Add(Cast<UAnimSequence>(
			FSoftObjectPath(SecondDeathAnimationPath).TryLoad()));
		IdleAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(IdleAnimationPath).TryLoad());
		JogAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(JogAnimationPath).TryLoad());

		if (!LeftSwingAnimation || !RightSwingAnimation || !MoltenRoarAnimation
			|| !StampedeAnimation)
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Abyssal Warden animations missing: swings %s and %s, "
					 "roar %s, charge %s. It will fight with nothing to show "
					 "for it. This is expected without the Paragon Grux pack. "
					 "The charge still MOVES the creature without its clip -- "
					 "BeginCharge does not need one -- so a missing charge clip "
					 "reads as sliding rather than as nothing happening."),
				LeftSwingAnimation ? TEXT("found") : TEXT("MISSING"),
				RightSwingAnimation ? TEXT("found") : TEXT("MISSING"),
				MoltenRoarAnimation ? TEXT("found") : TEXT("MISSING"),
				StampedeAnimation ? TEXT("found") : TEXT("MISSING"));
		}
	}

	// OTHERWISE THE CYLINDER SITS INSIDE THE DEMON. `ACataclysmEnemyCharacter`
	// creates PlaceholderBody in its constructor and nothing about assigning a
	// skeletal mesh removes it.
	//
	// THIS WAS MISSED WHEN THE CLASS WAS FIRST WRITTEN and the project owner
	// saw it immediately: the placeholder rendered on top of the creature. It is
	// not visible from the Brute's ResolveBody at a glance, because there it
	// sits after two screens of rock and montage loading that this class has
	// none of. `test_every_dressed_enemy_hides_its_placeholder` in
	// `tools/tests/test_warden_matches_the_model.py` now refuses a dressed enemy
	// that does not do this, for every enemy rather than only this one.
	if (PlaceholderBody)
	{
		PlaceholderBody->SetVisibility(false);
	}

	UE_LOG(LogCataclysm, Verbose,
		TEXT("Abyssal Warden is wearing %s."), BodyMeshPath);

	return true;
}

bool ACataclysmAbyssalWardenCharacter::ResolveAnimationBlueprint(
	USkeletalMeshComponent* MeshComponent)
{
	if (!MeshComponent)
	{
		return false;
	}

	UClass* AnimationClass =
		FSoftClassPath(AnimationBlueprintPath).TryLoadClass<UAnimInstance>();

	if (!AnimationClass)
	{
		// EXPECTED TODAY, NOT AN ERROR. No ABP_AbyssalWarden has been authored:
		// one has to be built by hand in the editor, because Unreal's Python
		// exposes no way to connect two animation graph pins. The creature
		// still fights and its attacks are still visible, through the
		// single-clip mode set below; what is lost is blending, so it slides
		// rather than steps while walking. Issue #387 is that work.
		UE_LOG(LogCataclysm, Warning,
			TEXT("Abyssal Warden animation Blueprint not found at %s, so it "
				 "will play single clips instead of blending. Its swing and "
				 "its roar are still visible; its walk will slide. See "
				 "game/docs/enemy-source-assets.md."),
			AnimationBlueprintPath);

		// SAID OUTRIGHT RATHER THAN LEFT AT THE DEFAULT. A skeletal mesh
		// component starts in single-node mode, so this line changes nothing
		// today -- and it is what stops the fallback silently breaking if that
		// default ever changes, and what makes the intent readable.
		MeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		return false;
	}

	MeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	MeshComponent->SetAnimInstanceClass(AnimationClass);

	return true;
}
