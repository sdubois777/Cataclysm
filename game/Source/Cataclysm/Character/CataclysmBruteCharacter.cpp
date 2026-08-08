// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmBruteCharacter.h"
#include "Cataclysm.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Character/CataclysmEnemyController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/SoftObjectPath.h"

const TCHAR* ACataclysmBruteCharacter::BodyMeshPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Meshes/Rampage.Rampage");

const TCHAR* ACataclysmBruteCharacter::AnimationBlueprintPath =
	TEXT("/Game/Enemies/Demonic/Brute/ABP_Brute.ABP_Brute_C");

const FName ACataclysmBruteCharacter::AttackSlotName = TEXT("DefaultSlot");

const TCHAR* ACataclysmBruteCharacter::StompAnimationPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/"
		 "Ability_GroundSmash_Start.Ability_GroundSmash_Start");

const TCHAR* ACataclysmBruteCharacter::StompReleaseAnimationPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/"
		 "Ability_GroundSmash_End.Ability_GroundSmash_End");

const TCHAR* ACataclysmBruteCharacter::RockThrowAnimationPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/"
		 "Ability_RipNToss_Rip.Ability_RipNToss_Rip");

const TCHAR* ACataclysmBruteCharacter::RockThrowReleaseAnimationPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/"
		 "Ability_RipNToss_Toss.Ability_RipNToss_Toss");

const TCHAR* ACataclysmBruteCharacter::AttackAnimationPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/"
		 "Attack_Biped_Melee_A.Attack_Biped_Melee_A");

/**
 * Live override for the chase speed, for judging it by eye.
 *
 * THE DESIGNED FIGURE IS NOW 500 AND LIVES IN THE MODEL, as chase_speed on
 * ARCHETYPES["Brute"] in sim/cataclysm_sim/enemy_stats.py, so this is a tuning
 * aid rather than the source of the number. It started as the only way to try a
 * second speed at all, because the design had none for any enemy; playing it on
 * 2026-08-07 settled 500 and that went into the model.
 *
 * Zero means use the designed figure, so clearing it restores the design.
 */
static TAutoConsoleVariable<float> CVarBruteChaseSpeed(
	TEXT("Cataclysm.Brute.ChaseSpeed"),
	0.0f,
	TEXT("Centimetres per second the Brute moves while chasing. 0 uses its "
		 "designed 500. It wanders at 250 either way. The player currently "
		 "moves at Unreal's default 600 rather than a designed class speed, "
		 "which is issue #391, so the margin here is not what the design "
		 "believes it is."),
	ECVF_Default);

/**
 * Live override for the seconds between swings, for finding the right pace.
 *
 * WHY IT IS WANTED. 2.8 seconds is the designed interval and the project owner
 * reported it on 2026-08-07 as "a pretty long delay", with the reasoning that an
 * enemy that is not attacking might as well be scenery. That is a balance
 * judgement about how the game feels, which is exactly the kind of number that
 * has to be found by playing rather than derived.
 *
 * IT DOES NOT TOUCH THE STOMP, which was the obvious worry and is not one. An
 * ability with a cooldown is telegraphed against that cooldown rather than the
 * attack interval, which section X of docs/Cataclysm_GDD_v2.md states and
 * Ability.cycle_seconds implements. The Stomp runs on its 5 second cooldown.
 *
 * WHAT IT DOES SIZE is the marker the ordinary swing could draw, which is
 * nothing: the Slam's 0.9 m radius is under the one metre floor at any interval.
 *
 * Zero means use the designed interval, which is 1.6 as of 2026-08-07.
 */
static TAutoConsoleVariable<float> CVarBruteAttackInterval(
	TEXT("Cataclysm.Brute.AttackInterval"),
	0.0f,
	TEXT("Seconds between the Brute's swings. 0 uses its designed 1.6. This "
		 "does NOT affect the Stomp, which is telegraphed against its own 5 "
		 "second cooldown rather than the attack interval."),
	ECVF_Default);

ACataclysmBruteCharacter::ACataclysmBruteCharacter()
{
	// TICKS, UNLIKE EVERY OTHER CHARACTER IN THIS PROJECT, for one reason: the
	// walk speed has to follow what the brain is doing, and the brain runs on
	// its own quarter-second timer while movement is read every frame. Choosing
	// the animation used to be the other reason and is now ABP_Brute's job.
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

	ApplyChaseSpeed();
}

void ACataclysmBruteCharacter::ApplyChaseSpeed()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// TWO DESIGNED SPEEDS, PICKED BY WHAT THE BRAIN IS DOING. The console
	// variable overrides the chase one for tuning by eye and zero means "use
	// the designed figure", so clearing it mid-session restores the design
	// rather than leaving whatever was last typed. Written every frame for that
	// reason rather than only when the state changes.
	const float Override = CVarBruteChaseSpeed.GetValueOnAnyThread();
	const float ChaseSpeed = Override > 0.0f
		? Override : DesignedChaseSpeedCmPerSecond;

	Movement->MaxWalkSpeed = IsChasing()
		? ChaseSpeed : DesignedWalkSpeedCmPerSecond;
}

TArray<FCataclysmEnemyAbility> ACataclysmBruteCharacter::EnemyAbilities() const
{
	FCataclysmEnemyAbility Stomp;
	Stomp.Name = TEXT("Stomp");
	// FROM ITS OWN FEET OUT, so no minimum: a target pressed against the Brute
	// is inside the ring and should be hit by it.
	Stomp.MinRangeCm = 0.0f;
	Stomp.MaxRangeCm = StompRadiusCm;
	Stomp.CooldownSeconds = StompCooldownSeconds;
	Stomp.WindUpSeconds = StompWindUpSeconds;

	FCataclysmEnemyAbility RockThrow;
	RockThrow.Name = TEXT("Rip and Toss");
	// NOT AT SOMETHING IT COULD HIT INSTEAD. There is no sense throwing a rock
	// at a target already within swinging distance, and the model says so: the
	// ability exists to answer standing off, not to replace the swing.
	RockThrow.MinRangeCm = DesignedMeleeReachCm;
	RockThrow.MaxRangeCm = RockThrowRangeCm;
	RockThrow.CooldownSeconds = RockThrowCooldownSeconds;
	RockThrow.WindUpSeconds = RockThrowWindUpSeconds;

	// ORDER IS PRIORITY. See the StompAbility enumeration in the header.
	return {Stomp, RockThrow};
}

void ACataclysmBruteCharacter::BeginEnemyAbilityWindUp(int32 Index, AActor*)
{
	// THE ANIMATION IS THE TELEGRAPH, for now. There is no ground marker drawn
	// anywhere in the project -- issue #371 covers that -- so the only warning
	// a player gets is the creature visibly starting the attack. That is why
	// the wind-up animation starts here, when the wind-up starts, rather than
	// when the damage lands.
	UAnimSequence* Wanted = Index == StompAbility
		? StompAnimation.Get() : RockThrowAnimation.Get();
	if (!Wanted)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// HELD FOR THE WHOLE WIND-UP, NOT THE ANIMATION'S OWN LENGTH, which is the
	// opposite of what the ordinary swing does and is right for the opposite
	// reason. The swing's animation IS the attack. A wind-up animation is
	// shorter than the wind-up it illustrates -- the ground smash start is 0.83
	// seconds inside a 1.4 second telegraph -- so holding the mesh for the
	// animation would leave the Brute standing in its normal pose for the last
	// half second before a stomp lands, which is exactly when a player is
	// deciding whether to move.
	const float Hold = Index == StompAbility
		? StompWindUpSeconds : RockThrowWindUpSeconds;

	// STRETCHED TO FILL THE TELEGRAPH EXACTLY, which PlayOneShot does from the
	// duration. Held at rate 1.0 instead, the ground smash wind-up finished
	// half a second early and froze, and the rock throw wind-up was cut off
	// before the rock left the ground.
	PlayOneShot(Wanted, Hold);
}

UAnimSequence* ACataclysmBruteCharacter::ReleaseAnimationFor(int32 Index) const
{
	if (Index == StompAbility)
	{
		return StompReleaseAnimation.Get();
	}
	if (Index == RockThrowAbility)
	{
		return RockThrowReleaseAnimation.Get();
	}
	return nullptr;
}

void ACataclysmBruteCharacter::UseEnemyAbility(int32 Index, AActor* Target,
											   const FVector& AimedAt)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// THE HALF OF THE ATTACK YOU ACTUALLY SEE HAPPEN. The wind-up clip ends
	// with the creature poised and nothing else was ever played, so at the
	// moment of impact the mesh went straight back to standing or walking and
	// the attack read as abandoned. Played before the damage rather than after
	// only so that the two cannot be separated by an early return added later.
	PlayOneShot(ReleaseAnimationFor(Index));

	if (Index == StompAbility)
	{
		// EVERYTHING IN THE RING, not just the target. Angle=360 in the model,
		// which is what stops the answer to a Brute being "stand behind it".
		//
		// THE STUN IS THE POINT OF THIS ATTACK, NOT A RIDER ON IT. The Brute is
		// the first thing in the game that stuns the player, and this is the
		// attack that does it. It is applied after the damage rather than before
		// so the hit is what the threshold rule would see if this were an
		// ordinary stun -- it is not, it is a designed one, but ordering the two
		// the other way round would make that distinction invisible.
		for (AActor* Caught : UCataclysmTargeting::FindEnemiesInSphere(
				World, this, GetActorLocation(), StompRadiusCm))
		{
			const float Dealt =
				UCataclysmSkillEffects::ApplyHit(this, Caught, StompDamagePercent);

			UCataclysmSkillEffects::ApplyStun(this, Caught, StompStunSeconds,
											  Dealt, /*bStunIsDesigned=*/true);
		}
		return;
	}

	if (Index == RockThrowAbility)
	{
		// AIMED WHERE IT WAS MARKED. AimedAt is where the target stood when the
		// wind-up began, so a player who moved has moved out of the line.
		ACataclysmProjectile::Fire(
			this, GetActorLocation(), AimedAt,
			RockThrowRadiusCm, RockThrowSpeedCmPerSecond,
			/*InPierce=*/0, /*bInReturns=*/false, RockThrowDamagePercent,
			FGameplayTagContainer(), /*bInBurns=*/false);
		return;
	}
}

float ACataclysmBruteCharacter::SecondsBetweenAttacks() const
{
	const float Override = CVarBruteAttackInterval.GetValueOnAnyThread();
	return Override > 0.0f ? Override : AttackIntervalSeconds;
}

void ACataclysmBruteCharacter::AttackTarget(AActor* Target)
{
	Super::AttackTarget(Target);
	PlayAttackAnimation();
}

void ACataclysmBruteCharacter::PlayAttackAnimation()
{
	// THE ANIMATION'S OWN LENGTH, NOT THE ATTACK INTERVAL, which is what
	// passing no duration means. The swing is 1.0 seconds and the interval
	// between swings is 1.6, so holding the mesh for the interval would leave
	// the Brute frozen in its finishing pose after every hit.
	PlayOneShot(AttackAnimation);
}

void ACataclysmBruteCharacter::PlayOneShot(UAnimSequence* Animation,
										   float HoldSeconds)
{
	const UWorld* World = GetWorld();
	if (!World || !Animation)
	{
		return;
	}

	const float Length = Animation->GetPlayLength();
	if (Length <= 0.0f)
	{
		return;
	}

	// No duration asked for means play it at normal speed for as long as it is.
	const float Hold = HoldSeconds > 0.0f ? HoldSeconds : Length;

	// NEVER SLOWER THAN IT WAS AUTHORED. ONLY FASTER, AND ONLY WHEN IT MUST BE.
	//
	// Stretching a short clip across a long window was tried first and is
	// wrong. The ground smash wind-up is 0.83 seconds inside a 1.4 second
	// telegraph, so filling the window played it at 0.59 speed: the Brute
	// raised its arms in slow motion and then the release ran at full speed,
	// which was reported from a play session as a glitch rather than as one
	// movement. The clips are authored at one speed and changing it on only
	// half of them is what looks wrong.
	//
	// HOLDING THE LAST POSE IS WHAT THE PACK EXPECTS. Rampage ships a separate
	// Ability_GroundSmash_Loop of 0.03 seconds, whose only purpose is to hold a
	// wind-up open for as long as the telegraph needs. A creature poised with
	// its arms up for the last half second of a telegraph is the intended
	// reading, and it is also the clearest warning the player gets.
	//
	// COMPRESSION IS STILL NEEDED IN THE OTHER DIRECTION. The rock throw
	// wind-up clip is longer than its 1.0 second telegraph, so at authored
	// speed it is cut off before the rock comes free -- which is the other half
	// of what that play session reported.
	const float Rate = FMath::Clamp(FMath::Max(1.0f, Length / Hold),
									MinimumPlayRate, MaximumPlayRate);

	// RECORDED BEFORE ANYTHING IS ASKED TO PLAY IT. See the declaration: the
	// application needs a running animation graph and the decision does not, so
	// a test can check which clip an ability chose in a world where nothing can
	// play anything.
	LastPlayedAnimation = Animation;
	LastPlayedRate = Rate;

	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return;
	}

	// THROUGH THE ANIMATION SLOT, NOT BY REPLACING WHAT THE MESH IS PLAYING.
	//
	// WHAT THIS FIXED. The previous version drove the mesh in
	// EAnimationMode::AnimationSingleNode, which plays exactly one clip and
	// cannot blend between two. Each ability is two clips -- a wind-up and a
	// release -- so the moment the release started, the mesh jumped from the
	// last pose of the wind-up to the first pose of the release in a single
	// frame. Reported from a play session on 2026-08-08 as the stomp reading
	// wrong. Nothing in C++ could fix it, because the fault was the animation
	// mode rather than the code driving it.
	//
	// A dynamic montage needs no montage asset on disk: it wraps a plain
	// sequence and plays it in the named slot of ABP_Brute's animation graph,
	// blending in over AttackBlendInSeconds and back out over
	// AttackBlendOutSeconds. Locomotion keeps running underneath and is
	// blended back to when the clip ends, which is also why nothing has to
	// hold the mesh open for the duration any more.
	if (UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance())
	{
		AnimInstance->PlaySlotAnimationAsDynamicMontage(
			Animation, AttackSlotName,
			AttackBlendInSeconds, AttackBlendOutSeconds,
			Rate, /*LoopCount=*/1);
	}
}

bool ACataclysmBruteCharacter::IsChasing() const
{
	// ASKED OF THE BRAIN RATHER THAN INFERRED FROM SPEED. ABP_Brute does infer
	// it from speed, because by the time the animation graph runs the two
	// states really are two different speeds -- 250 wandering and 500 chasing.
	// This is asked earlier than that: ApplyChaseSpeed is what SETS those two
	// speeds, so it cannot read them back to decide which one to use.
	const ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(GetController());
	return Brain && Brain->LastAction == ECataclysmBrainAction::Chasing;
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
		ResolveAnimationBlueprint(MeshComponent);

		AttackAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(AttackAnimationPath).TryLoad());

		StompAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(StompAnimationPath).TryLoad());
		StompReleaseAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(StompReleaseAnimationPath).TryLoad());
		RockThrowAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(RockThrowAnimationPath).TryLoad());
		RockThrowReleaseAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(RockThrowReleaseAnimationPath).TryLoad());

		if (!AttackAnimation)
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Brute attack animation not found at %s, so its swings "
					 "will be invisible."),
				AttackAnimationPath);
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

bool ACataclysmBruteCharacter::ResolveAnimationBlueprint(
	USkeletalMeshComponent* MeshComponent)
{
	if (!MeshComponent)
	{
		return false;
	}

	// THE GENERATED CLASS, NOT THE BLUEPRINT ASSET. SetAnimInstanceClass wants a
	// UClass, and an animation Blueprint's runtime class is its asset path with
	// _C on the end. Loading the asset itself and casting would silently give
	// null, because a UAnimBlueprint is not a UAnimInstance subclass.
	UClass* AnimationClass =
		FSoftClassPath(AnimationBlueprintPath).TryLoadClass<UAnimInstance>();

	if (!AnimationClass)
	{
		// NOT AN ERROR, FOR THE SAME REASON THE MISSING MESH IS NOT. ABP_Brute
		// is committed, but every animation it plays comes from the gitignored
		// Paragon Rampage pack, so on a fresh clone the graph has nothing to
		// reference. The Brute still fights; it just holds its reference pose.
		UE_LOG(LogCataclysm, Warning,
			TEXT("Brute animation Blueprint not found at %s, so it will hold "
				 "its reference pose and its attacks will be invisible. This is "
				 "expected without the Paragon Rampage pack; see "
				 "game/docs/enemy-source-assets.md."),
			AnimationBlueprintPath);
		return false;
	}

	// THE MODE AS WELL AS THE CLASS. SetAnimInstanceClass sets the mode too, but
	// saying it outright is what stops a later reader assuming this component is
	// still in the single-node mode it used until 2026-08-08.
	MeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	MeshComponent->SetAnimInstanceClass(AnimationClass);

	return true;
}
