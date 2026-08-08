// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmBruteCharacter.h"
#include "Cataclysm.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
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
		 "figure on the class, 225, which was set by eye. For tuning foot "
		 "sliding by eye during a play session."),
	ECVF_Default);

const TCHAR* ACataclysmBruteCharacter::BodyMeshPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Meshes/Rampage.Rampage");

const TCHAR* ACataclysmBruteCharacter::IdleAnimationPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/Idle_Biped.Idle_Biped");

const TCHAR* ACataclysmBruteCharacter::WalkAnimationPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/Jog_Biped_Fwd.Jog_Biped_Fwd");

const TCHAR* ACataclysmBruteCharacter::ChaseAnimationPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/"
		 "Jog_Quad_Fwd.Jog_Quad_Fwd");

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
		 "by this, so a smaller number plays it faster. 0 uses the figure on "
		 "the class, 350, set by eye against the four-legged chase gait."),
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

static TAutoConsoleVariable<FString> CVarBruteChaseAnimation(
	TEXT("Cataclysm.Brute.ChaseAnimation"),
	TEXT(""),
	TEXT("Asset path of the animation the Brute plays while chasing. Empty uses "
		 "the class default, Jog_Quad_Fwd, the four-legged stance. Set "
		 "Cataclysm.Brute.AuthoredChaseSpeed to whatever the replacement was "
		 "authored for, or its feet will slide."),
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

	ApplyChaseSpeed();
	DriveLocomotion();
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

	SwingUntilSeconds = World->GetTimeSeconds() + Hold;

	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		if (UAnimSingleNodeInstance* Single = MeshComponent->GetSingleNodeInstance())
		{
			// NOT LOOPING, and restarted from the beginning every time, which
			// is why this sets the asset even when it is already the one
			// playing. Two swings in a row should read as two swings.
			Single->SetAnimationAsset(Animation, /*bIsLooping=*/false);
			Single->SetPosition(0.0f, /*bFireNotifies=*/false);
			Single->SetPlayRate(Rate);
			Single->SetPlaying(true);
		}
	}
}

bool ACataclysmBruteCharacter::IsSwinging() const
{
	const UWorld* World = GetWorld();
	return World && SwingUntilSeconds > 0.0f
		&& World->GetTimeSeconds() < SwingUntilSeconds;
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

	// THE SWING OWNS THE MESH UNTIL IT HAS PLAYED OUT. The Brute stops moving
	// to attack, so without this the next frame's choice would be the standing
	// animation and the swing would be cut off after one frame -- which looks
	// exactly like not attacking at all.
	if (IsSwinging())
	{
		return;
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

		if (!AttackAnimation)
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Brute attack animation not found at %s, so its swings "
					 "will be invisible."),
				AttackAnimationPath);
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
