// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmEnemyCharacter.h"
#include "Cataclysm.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
// For turning an ability's tag list into a container, the same way a player's
// skill row is read. Issue #519.
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmEnemyDeath.h"
#include "Character/CataclysmEnemyRarity.h"
// For what a kill drops. The rules live in the item module; this file
// only says when they run and where the result lands.
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Save/CataclysmSaveWriter.h"

namespace
{
	/** Half-height and radius of the collision capsule, in centimetres. */
	constexpr float EnemyCapsuleRadius = 48.0f;
	constexpr float EnemyCapsuleHalfHeight = 80.0f;

}

// A SINGLE MELEE SWING AT ONE TARGET, which is what Cinderslash carries in the
// weapon skill matrix. Not area damage, so it can be evaded.
const TCHAR* ACataclysmEnemyCharacter::BasicAttackTags =
	TEXT("Type.Strike, Type.Melee");

// RUNS THROUGH WHAT IT HITS AND SHOVES IT ASIDE. Furnace Charge and Flamedart
// carry Type.Strike with Keyword.Charge; the stagger is this one's own, because
// it displaces.
const TCHAR* ACataclysmEnemyCharacter::ChargeTags =
	TEXT("Type.Strike, Keyword.Charge, Keyword.Stagger");

ACataclysmEnemyCharacter::ACataclysmEnemyCharacter()
{
	// Every enemy is on the Monsters side, so no skill of one enemy's can hit
	// another. The exception the design asks for -- Madness, where "the enemy
	// attacks anything nearby, friend or foe" -- is a change of attitude for a
	// tagged actor and not a change of side. Issue #163 builds it.
	TeamId = UCataclysmTeams::IdFor(ECataclysmTeam::Monsters);

	// Its own brain, possessed as soon as it exists. Without both of these an
	// enemy stands where it was spawned for its whole life: nothing else in the
	// project gives a non-player pawn a controller.
	AIControllerClass = ACataclysmEnemyController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// It turns to face where it is walking, like the player character does.
	// Without this the cylinder slides sideways and there is no way to tell from
	// looking at it which way it thinks it is going.
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 480.0f, 0.0f);

	AbilitySystemComponent = CreateDefaultSubobject<UCataclysmAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	// Minimal replication: no client owns an enemy, so no client needs full
	// gameplay effect data for one. Tags and cues are enough to drive visuals.
	// A dungeon floor can hold a great many enemies and this is where the
	// bandwidth goes if it is set wrong.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	// Three sets, not five. An enemy has no attribute points to spend and no
	// class tree, so the primary attribute set and the class resource set would
	// be dead weight replicated on every spawn, and a dungeon floor can hold a
	// great many enemies.
	VitalAttributes = CreateDefaultSubobject<UCataclysmVitalAttributeSet>(TEXT("VitalAttributes"));
	CombatAttributes = CreateDefaultSubobject<UCataclysmCombatAttributeSet>(TEXT("CombatAttributes"));
	ResistanceAttributes = CreateDefaultSubobject<UCataclysmAllResistanceAttributeSet>(TEXT("ResistanceAttributes"));

	// A stand-in body, for the same reason the player has one: this project's
	// own Content folder holds no meshes at all, so without it an enemy is an
	// invisible capsule and there is no way to tell whether one is there.
	//
	// Slightly wider and shorter than the player's, so the two can be told
	// apart at a glance while both are cylinders.
	GetCapsuleComponent()->InitCapsuleSize(EnemyCapsuleRadius, EnemyCapsuleHalfHeight);

	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(RootComponent);
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetRelativeScale3D(FVector(
		(EnemyCapsuleRadius * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		(EnemyCapsuleRadius * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		(EnemyCapsuleHalfHeight * 2.0f) / ACataclysmCharacterBase::BasicShapeSize));

	// Found by path rather than referenced as an asset, because these are engine
	// content. A failure here is not fatal: the capsule is still there and still
	// takes damage, it is just invisible.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(CylinderMesh.Object);
	}

	// EVERY ENEMY TICKS, BECAUSE A CHARGE ADVANCES PER FRAME.
	// `ACataclysmCharacterBase` turns ticking off, and AdvanceCharge cannot be
	// driven by the brain instead: the brain thinks four times a second and a
	// charge covers metres in one of those. Issue #491.
	//
	// WHAT IT COSTS A CREATURE THAT NEVER CHARGES: one boolean test a frame.
	// Tick below returns immediately unless a charge is running.
	PrimaryActorTick.bCanEverTick = true;
}

void ACataclysmEnemyCharacter::HandleDeath()
{
	if (!UCataclysmSkillEffects::MarkDead(this))
	{
		// Already dead. Nothing here is safe to run twice.
		return;
	}

	// A CREATURE DYING IS ONE OF THE FIVE EVENTS SECTION 6 WRITES ON, and it
	// is the one that fires most: it is what stops a player killing a boss,
	// quitting, and coming back to find it alive again.
	//
	// AFTER THE MARK RATHER THAN BEFORE IT, WHICH IS THE OPPOSITE OF THE
	// PLAYER'S. The gather skips a dead creature, and that is exactly what is
	// wanted here: the record should show the creature gone. The player's own
	// death wants the opposite, so its write is placed above its mark.
	//
	// THE WRITE IS ASYNCHRONOUS, so this costs the frame the time to build
	// the JSON and no more. Several creatures dying together share one write,
	// because the run record is written at most once a frame.
	UCataclysmSaveWriter::NoteTriggerIn(GetWorld(),
										ECataclysmSaveTrigger::CreatureDied);

	// WHATEVER IT WAS DOING STOPS. A charge already in flight is the one that
	// matters: it advances per frame from Tick and would otherwise carry the
	// corpse across the room. The same reasoning as being stunned mid-charge,
	// issue #499.
	CancelCharge();

	// AND SO DOES EVERY PER-FRAME JOB THE CREATURE HAD, which matters more
	// than it looks now that the body stays on screen for a clip's length.
	// The Abyssal Warden's UpdateLoopingAnimation runs from Tick and would
	// put an idle loop back over the death pose within a frame or two, since
	// a corpse has no velocity and therefore reads as standing. The Brute's
	// Tick keeps a rock and a crater in step with a wind-up that is no longer
	// happening. Neither of those checks whether the creature is dead, and a
	// check in each would be a thing every future creature had to remember.
	//
	// IT DOES NOT STOP THE ANIMATION. AActor::SetActorTickEnabled sets
	// PrimaryActorTick and nothing else; a skeletal mesh component has its
	// own tick function and that is what evaluates the clip, so the death
	// animation plays out with the actor's own Tick switched off.
	SetActorTickEnabled(false);

	// DisableMovement CLEARS THE VELOCITY TOO, so there is no separate call to
	// stop it. UCharacterMovementComponent::OnMovementModeChanged runs
	// StopMovementKeepPathing when the new mode is MOVE_None -- "Kill velocity
	// and clear queued up events". A StopMovementImmediately beside this was
	// written first and proved to be doing nothing: removing it failed no test,
	// which is how it was found.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->DisableMovement();
	}

	// The capsule stops blocking, so a corpse on its way out cannot push the
	// player around or stand in the way of another creature for the frame it
	// has left.
	SetActorEnableCollision(false);

	// WHAT IT DROPPED, before the actor goes away. How much and how good both
	// follow from this creature's own rarity, which it already knows as a Step
	// from 0 to 5.
	//
	// THE SEED IS THIS CREATURE AND THE MOMENT IT DIED, so two creatures dying
	// in the same frame do not drop identical loot, and so a run is not
	// reproducible in a way a player could exploit. A shared stream would make
	// every kill in a frame the same.
	//
	// NO PLAYER MAGIC FIND OR LOOT QUANTITY IS READ YET. Both are character
	// stats, nothing computes a character's stats at the moment of a kill, and
	// passing the baselines is honest about that: 0% added magic find and 100%
	// loot quantity is a character with no bonuses, which is what the design
	// says those baselines mean.
	if (UWorld* World = GetWorld())
	{
		FRandomStream Stream(GetUniqueID()
			^ static_cast<int32>(World->GetTimeSeconds() * 1000.0f));
		UCataclysmDropSpawner::SpawnDropsFor(
			World, RarityStep, /*MagicFind=*/0.0f,
			UCataclysmDropRoll::BaselineLootQuantity, GetActorLocation(),
			Stream);
	}

	// WHAT DYING LOOKS LIKE, AND HOW LONG THE BODY IS KEPT FOR IT. Before
	// issue #522 a creature played nothing and was gone within a frame.
	CorpseSeconds = PlayDeathAnimation();

	if (UWorld* World = GetWorld())
	{
		FTimerDelegate Removal =
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				Destroy();
			});

		if (CorpseSeconds > 0.0f)
		{
			// THE BODY OUTLIVES THE KILLING BLOW BY EXACTLY ITS CLIP. Removing
			// it any earlier would cut the animation off part way through,
			// and keeping it any longer is a design decision about corpses
			// that has not been made. UCataclysmEnemyDeath::CorpseSecondsFor
			// is where it would be made.
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, Removal, CorpseSeconds,
										  /*bLoop=*/false);
		}
		else
		{
			// NOTHING TO PLAY, SO NOTHING TO WAIT FOR. A creature with no art
			// goes on the next tick, which is what every creature did before
			// this. It is stated rather than left as an accident: five of the
			// seven vertical slice creatures reach it every time they die.
			World->GetTimerManager().SetTimerForNextTick(Removal);
		}
	}
}

FString ACataclysmEnemyCharacter::ClipPathIn(const TCHAR* Folder,
											 const TCHAR* Name)
{
	// AN UNREAL ASSET PATH REPEATS THE ASSET'S NAME after the package path. See
	// the header for why this lives on the base class rather than as a private
	// helper in each creature's own file.
	return FString::Printf(TEXT("%s/%s.%s"), Folder, Name, Name);
}

float ACataclysmEnemyCharacter::PlayDeathAnimation()
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return 0.0f;
	}

	// ITS OWN STREAM, seeded from this creature and the moment it died so two
	// creatures dying in the same frame do not fall the same way, and salted
	// so it is not the stream the drops were rolled from. See the header for
	// why those must not be the same stream.
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	FRandomStream Stream(GetUniqueID()
		^ static_cast<int32>(Now * 1000.0f) ^ DeathDrawSalt);

	const int32 Index =
		UCataclysmEnemyDeath::ClipToPlay(DeathAnimations.Num(), Stream);
	if (!DeathAnimations.IsValidIndex(Index))
	{
		return 0.0f;
	}

	// A NULL ENTRY IS A CLIP THAT FAILED TO LOAD, which is what a subclass
	// leaves behind when its pack is absent. It is not dropped from the array,
	// because doing so would change how many clips there are and therefore
	// which one every OTHER creature drew.
	UAnimSequence* Clip = DeathAnimations[Index].Get();
	if (!Clip)
	{
		return 0.0f;
	}

	DiedWith = Clip;

	// AT ITS AUTHORED SPEED. Nothing constrains a death to a window, unlike
	// every other clip in this project, which has to fit inside a telegraph or
	// an attack interval.
	MeshComponent->PlayAnimation(Clip, /*bLooping=*/false);
	MeshComponent->SetPlayRate(1.0f);

	return UCataclysmEnemyDeath::CorpseSecondsFor(Clip->GetPlayLength());
}

void ACataclysmEnemyCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AdvanceCharge(DeltaSeconds);
	RefreshCommanderBuff();
}

void ACataclysmEnemyCharacter::HealthChanged()
{
	RefreshPhase();
}

float ACataclysmEnemyCharacter::StrikeAlignedPlayRate(float StrikeSeconds,
													  float LandsAtSeconds,
													  float MinimumRate,
													  float MaximumRate)
{
	if (StrikeSeconds <= 0.0f || LandsAtSeconds <= 0.0f)
	{
		return 1.0f;
	}

	// NEVER SLOWER THAN AUTHORED. See the header: stretching a clip to fill a
	// longer window was tried and read as slow motion, so a clip that strikes
	// too early is DELAYED rather than slowed.
	return FMath::Clamp(FMath::Max(1.0f, StrikeSeconds / LandsAtSeconds),
						MinimumRate, MaximumRate);
}

float ACataclysmEnemyCharacter::StrikeAlignedDelaySeconds(float StrikeSeconds,
														  float LandsAtSeconds,
														  float MinimumRate,
														  float MaximumRate)
{
	if (StrikeSeconds <= 0.0f || LandsAtSeconds <= 0.0f)
	{
		return 0.0f;
	}

	const float Rate = StrikeAlignedPlayRate(StrikeSeconds, LandsAtSeconds,
											 MinimumRate, MaximumRate);

	// WAIT OUT WHATEVER THE ANIMATION DOES NOT COVER. At rate 1 the Gatekeeper's
	// hammer reaches the ground 0.282 seconds in and its blow lands at 0.971, so
	// this is 0.689. Where the clip had to be compressed instead, the rate above
	// has already put the strike at the blow and this is zero.
	//
	// **DIVIDING BY THE RATE CANNOT CHANGE THE ANSWER TODAY, AND NO TEST CAN
	// PROVE IT.** Breaking it to `LandsAtSeconds - StrikeSeconds` was tried on
	// 2026-08-21 and every test still passed, because the rate rule above makes
	// the two forms agree for every input:
	//
	//   strike <= lands   the rate is exactly 1, so dividing by it does nothing
	//   strike >  lands   the rate is strike/lands, so strike/rate is lands and
	//                     this is zero -- and so is lands minus strike, after the
	//                     Max below
	//   rate at its ceiling   strike/rate still exceeds lands, so both are zero
	//
	// IT IS KEPT RATHER THAN DELETED BECAUSE IT IS WHAT MAKES THE LINE TRUE
	// RATHER THAN COINCIDENTALLY RIGHT. The two forms agree only because the
	// rate is never below 1; remove that floor -- which `StrikeAlignedPlayRate`
	// has its own guard against -- and this division becomes load-bearing at
	// once. Writing the arithmetic that is actually meant is worth more than
	// writing the shortest arithmetic that happens to match it.
	return FMath::Max(0.0f, LandsAtSeconds - StrikeSeconds / Rate);
}

bool ACataclysmEnemyCharacter::RefreshPhase()
{
	if (PhaseHealthFractions.IsEmpty())
	{
		// No phases designed. Every ability's default phase is 1 and so is this,
		// so nothing is ever skipped.
		return false;
	}

	if (!AbilitySystemComponent)
	{
		return false;
	}

	const float MaxHealth = AbilitySystemComponent->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	if (MaxHealth <= 0.0f)
	{
		// Before ApplyStartingAttributes has run there is nothing to be a
		// fraction of, and dividing by it would put the creature in its last
		// phase on the frame it spawned.
		return false;
	}

	const float Health = AbilitySystemComponent->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetHealthAttribute());
	const float Fraction = Health / MaxHealth;

	// HIGHEST THRESHOLD FIRST, so the count of thresholds already passed is the
	// number of phases begun since the first. `(0.60, 0.30)` at 25% health
	// passes both and gives phase 3.
	int32 Wanted = 1;
	for (const float Threshold : PhaseHealthFractions)
	{
		if (Fraction <= Threshold)
		{
			++Wanted;
		}
	}

	// **FORWARD ONLY.** See the header: a creature healed back above a threshold
	// keeps the phase it reached, because "phases add, they do not take away".
	if (Wanted <= PhaseReached)
	{
		return false;
	}

	const int32 From = PhaseReached;
	PhaseReached = Wanted;

	// SAID OUT LOUD, BECAUSE NOTHING ON SCREEN SAYS IT. A phase changes which
	// abilities are in the rotation and changes no number, so the only visible
	// sign is a creature doing something it had not done before -- which is
	// indistinguishable from a cooldown coming up. Issue #740 is the screen work
	// that would make this line unnecessary.
	UE_LOG(LogCataclysm, Log,
		TEXT("%s entered phase %d of %d at %.1f%% health. A phase adds "
			 "abilities and changes no number."),
		*GetNameSafe(this), PhaseReached, PhaseHealthFractions.Num() + 1,
		Fraction * 100.0f);

	// The value is unused today and is what a future transition animation or
	// a screen effect would hang off.
	(void)From;

	return true;
}

float ACataclysmEnemyCharacter::CommanderMultiplier() const
{
	// THE TAG IS THE SINGLE SOURCE OF TRUTH, rather than a flag this class
	// keeps in step with one. The Succubus grants and removes the tag; nothing
	// here has to be told.
	const FGameplayTag Commander = UCataclysmSkillShapes::StatusTagFor(
		TEXT("Commander"));

	if (!UCataclysmSkillEffects::HasTag(this, Commander))
	{
		return 1.0f;
	}

	return 1.0f + CommanderIncreasePercent / 100.0f;
}

void ACataclysmEnemyCharacter::RefreshCommanderBuff()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// NOTHING TO SCALE UNTIL BeginPlay HAS READ THE DESIGNED FIGURE. Writing
	// zero here would freeze every creature in place for a frame.
	if (DesignedWalkSpeedCmPerSecond <= 0.0f)
	{
		return;
	}

	const float Wanted = DesignedWalkSpeedCmPerSecond * CommanderMultiplier();

	// ONLY ON A CHANGE. Assigning the same float every frame is harmless, and
	// checking first says out loud that this is a state that changes rarely
	// rather than a value being recomputed.
	if (!FMath::IsNearlyEqual(Movement->MaxWalkSpeed, Wanted))
	{
		Movement->MaxWalkSpeed = Wanted;
	}
}

void ACataclysmEnemyCharacter::BeginCharge(const FVector& ToPoint,
										   float SpeedCmPerSecond,
										   float HalfWidthCm,
										   float DamagePercent,
										   float KnockbackCm)
{
	if (SpeedCmPerSecond <= 0.0f)
	{
		// A charge with no speed would never end, because nothing else finishes
		// one. Refused rather than started, so the creature keeps walking.
		return;
	}

	// ON THE CREATURE'S OWN HEIGHT, NOT THE POINT'S. The lane is a floor-plane
	// thing and ToPoint arrives flattened onto the ground -- the controller
	// captures it through FloorUnder for the same reason issue #471 flattened
	// the aim point. Charging toward a point at floor height would drag the
	// capsule down through the floor by its own half-height.
	//
	// AND IT IS ONLY THE HEIGHT IT SET OFF AT, NOT THE HEIGHT IT WILL ARRIVE AT.
	// Nothing reads this Z: the direction and the distance left are both measured
	// in the floor plane, and each step's height comes from the floor under it.
	// Issue #497.
	ChargeEndPoint = FVector(ToPoint.X, ToPoint.Y, GetActorLocation().Z);

	ChargeSpeedCmPerSecond = SpeedCmPerSecond;
	ChargeHalfWidthCm = HalfWidthCm;
	ChargeDamagePercent = DamagePercent;
	ChargeKnockbackCm = KnockbackCm;
	ChargeTravelledCm = 0.0f;
	ChargeHitCount = 0;
	ChargeAlreadyHit.Reset();
	bCharging = true;
}

void ACataclysmEnemyCharacter::CancelCharge()
{
	// WHERE IT STOPPED IS WHERE IT STOPS. The creature is left standing at the
	// point it had reached, which is the honest outcome of being interrupted
	// mid-run. Nothing is rewound and it does not slide on.
	//
	// THE HIT LIST GOES WITH IT. It belongs to this charge, and the next one has
	// to be able to hit the same target again.
	bCharging = false;
	ChargeAlreadyHit.Reset();
}

void ACataclysmEnemyCharacter::AdvanceCharge(float DeltaSeconds)
{
	if (!bCharging || DeltaSeconds <= 0.0f)
	{
		return;
	}

	// SPLIT INTO STEPS NO LONGER THAN LongestChargeStepCm. See the header: one
	// long sweep can tunnel a thin wall. A whole frame's travel is covered
	// either way, so this changes where the charge is checked and not how far
	// it goes.
	float RemainingThisFrameCm = ChargeSpeedCmPerSecond * DeltaSeconds;

	while (bCharging && RemainingThisFrameCm > 0.0f)
	{
		const float StepCm = FMath::Min(RemainingThisFrameCm, LongestChargeStepCm);
		RemainingThisFrameCm -= StepCm;

		if (!StepCharge(StepCm))
		{
			return;
		}
	}
}

bool ACataclysmEnemyCharacter::StepCharge(float StepCm)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		bCharging = false;
		return false;
	}

	const FVector From = GetActorLocation();

	// HOW FAR IS LEFT, IN THE FLOOR PLANE. The lane is a floor-plane thing -- the
	// marker is drawn on the ground as a rectangle -- so both the distance left
	// and the distance travelled are measured flat. The creature's path over a
	// slope is longer than that, and charging it for the difference would leave it
	// short of the end of the lane the marker promised.
	const float RemainingCm = FVector::Dist2D(From, ChargeEndPoint);
	if (RemainingCm <= KINDA_SMALL_NUMBER)
	{
		bCharging = false;
		return false;
	}

	FVector Direction = ChargeEndPoint - From;
	Direction.Z = 0.0f;
	Direction = Direction.GetSafeNormal();

	// NEVER PAST THE END OF ITS OWN LANE. The marker was drawn to exactly this
	// point, so a step that overshot would take the creature onto ground it
	// never warned about.
	const float ThisStepCm = FMath::Min(StepCm, RemainingCm);
	FVector To = From + Direction * ThisStepCm;

	// ALONG THE GROUND, NOT AT THE HEIGHT IT SET OFF FROM. Issue #497. Direction
	// is flat, so without this the step would keep the previous height and a
	// charge would be horizontal for its whole run.
	if (!SetChargeStepHeight(From, RemainingCm, ThisStepCm, To))
	{
		// GROUND IT COULD NOT GET ONTO IS THE LEVEL STOPPING IT, the same as a
		// wall, and the creature is left standing at the foot of it.
		bCharging = false;
		return false;
	}

	// STOPPED BY THE LEVEL, NOT BY BODIES, AND THE DESIGN ASKS FOR BOTH HALVES.
	//
	// Not by bodies: the creature is committed and runs the full distance,
	// ending past its target -- "it ends up ten metres past the player, facing
	// away", which is the window the telegraph buys. A charge that stopped on
	// contact would arrive in melee range instead, which is the opposite of what
	// the design says a miss costs.
	//
	// By the level: a charge that ran through a wall would be the marker lying
	// about where the creature ends up.
	//
	// BY OBJECT TYPE, NOT BY CHANNEL, AND THAT DISTINCTION IS THE WHOLE THING.
	// SweepSingleByChannel(ECC_WorldStatic) asks "what BLOCKS the WorldStatic
	// channel", and a Pawn capsule blocks it -- so the first version of this
	// stopped dead on the creature's own capsule and travelled nothing, and
	// would have stopped on the player too. SweepSingleByObjectType asks "what
	// IS a WorldStatic object", which is the question actually being asked here.
	//
	// A SPHERE RATHER THAN THE CAPSULE, AND IT IS SMALLER ON PURPOSE. A capsule
	// sweep would be more faithful and is wrong here: the capsule's bottom rests
	// ON the floor, and the floor is WorldStatic, so a capsule swept along it
	// grazes the ground and every charge would stop on its first step. A sphere
	// of the capsule's radius centred at the capsule's centre sits well clear of
	// the floor -- 66 cm clear for the Warden -- and still meets a wall at body
	// height, which is what a charge should be stopped by.
	//
	// SWEPT TO WHERE THE STEP IS REALLY GOING, WHICH IS WHY IT RUNS AFTER THE
	// HEIGHT IS DECIDED AND NOT BEFORE. Up a ramp the sphere then travels PARALLEL
	// to the slope and keeps its whole 66 cm of clearance, where a sphere swept
	// flat into rising ground would graze the ramp and stop the charge on the
	// floor. That clearance is also what the sphere cannot see: anything shorter
	// than it passes underneath, which is why the height check above is what stops
	// a charge on a low obstacle and this sweep is not.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CataclysmCharge),
								 /*bInTraceComplex=*/false, this);

	FHitResult Hit;
	const bool bBlocked = World->SweepSingleByObjectType(
		Hit, From, To, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldStatic),
		FCollisionShape::MakeSphere(GetCapsuleComponent()
			? GetCapsuleComponent()->GetScaledCapsuleRadius()
			: 0.0f),
		Params);

	const FVector Landed = bBlocked ? Hit.Location : To;

	// NO SWEEP ON THE MOVE ITSELF, because the sweep above already decided where
	// it may go and a second one against Pawn would stop it on the very target
	// it is meant to run through.
	SetActorLocation(Landed, /*bSweep=*/false);
	ChargeTravelledCm += FVector::Dist2D(From, Landed);

	// EVERYTHING THIS STEP PASSED, ONCE EACH. "A charge hits everything on the
	// way" -- section X of docs/Cataclysm_GDD_v2.md, which distinguishes it from
	// a leap that hits only where it lands. Tested against the segment actually
	// travelled rather than the whole lane, so nothing is hit before the
	// creature reaches it.
	// PARSED ONCE RATHER THAN PER TARGET. A charge can run through several.
	const FGameplayTagContainer AbilityTags =
		UCataclysmSkillShapes::TagsFromCell(ChargeTags);

	for (AActor* Caught : UCataclysmTargeting::FindEnemiesInLine(
			World, this, From, Landed, ChargeHalfWidthCm))
	{
		if (!Caught || ChargeAlreadyHit.Contains(Caught))
		{
			continue;
		}

		ChargeAlreadyHit.Add(Caught);
		++ChargeHitCount;
		UCataclysmSkillEffects::ApplyHit(this, Caught, ChargeDamagePercent, AbilityTags);

		// AND IT SHOVES WHAT IT RUNS THROUGH ASIDE, when the ability asked for
		// it. The design settled on issue #310 that enemies displace the player
		// and gave the reason for a charge in particular: the player's own Bull
		// Rush and Cinder Rush charge through a crowd "knocking them aside", so a
		// charge that runs through the player does the same to them. Issue #625.
		//
		// AWAY FROM THE CREATURE AT THE MOMENT OF CONTACT, which is what
		// ApplyKnockback works out from the two positions. For a charge that is
		// diagonal rather than straight out: contact happens at the LEADING edge
		// of the lane, so the target is carried forward as well as out. It still
		// finishes outside the lane, which is what clears the ground.
		//
		// ONCE PER TARGET PER CHARGE, because it sits inside the same guard the
		// damage does. A charge that shoved on every step would push a target the
		// whole length of the lane.
		UCataclysmSkillEffects::ApplyKnockback(this, Caught, ChargeKnockbackCm);
	}

	if (bBlocked || FVector::Dist2D(Landed, ChargeEndPoint) <= KINDA_SMALL_NUMBER)
	{
		bCharging = false;
		return false;
	}

	return true;
}

bool ACataclysmEnemyCharacter::SetChargeStepHeight(const FVector& From,
												   float RemainingCm,
												   float StepCm,
												   FVector& Step) const
{
	UWorld* World = GetWorld();
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!World || !Movement)
	{
		return true;
	}

	// AN ACTOR'S LOCATION IS ITS CAPSULE CENTRE, so this is what turns a height
	// on the ground into a height for the creature and back again.
	const float HalfHeightCm = GetCapsuleComponent()
		? GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: 0.0f;

	// THE STEEPEST SLOPE THE CREATURE COULD WALK, AS A RISE PER CENTIMETRE ALONG.
	// The movement component holds that angle as the cosine of itself, which is
	// what GetWalkableFloorZ returns, and tan = sqrt(1 - cos^2) / cos converts it.
	// At the engine's default 44.765 degrees this is 0.99, so very nearly one
	// centimetre up for each one along.
	//
	// CLAMPED AWAY FROM ZERO because a walkable angle of 90 degrees would divide
	// by nothing. It is not a figure anybody would set, and the guard is one line.
	const float CosWalkable = FMath::Clamp(Movement->GetWalkableFloorZ(), 0.05f, 1.0f);
	const float WalkableRisePerCm =
		FMath::Sqrt(1.0f - CosWalkable * CosWalkable) / CosWalkable;

	// HOW FAR DOWN IT IS WORTH LOOKING, AND WHY IT IS DERIVED RATHER THAN PICKED.
	// The creature descends at most WalkableRisePerCm for every centimetre it has
	// still to travel, so a floor lower than that cannot be reached before the
	// lane ends. Below that depth, finding a floor and finding none produce
	// exactly the same run, so there is nothing to gain by looking further.
	const float DeepestWorthLookingCm =
		HalfHeightCm + RemainingCm * WalkableRisePerCm;

	// FROM THE MIDDLE OF THE BODY, NOT FROM OVER ITS HEAD. A trace that began
	// above the creature would find a ceiling or an overhead walkway and call it
	// the floor. Begun at the capsule's centre, the first thing below is the floor
	// or something resting on it, and anything higher than that is a wall, which
	// is what the sweep in StepCharge is for.
	const FVector LookFrom(Step.X, Step.Y, From.Z);

	// BY OBJECT TYPE, FOR THE SAME REASON THE SWEEP IN StepCharge IS. Asking what
	// IS a WorldStatic object excludes other creatures' capsules, so one enemy
	// cannot serve as the floor another charges along.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CataclysmChargeFloor),
								 /*bInTraceComplex=*/false, this);

	FHitResult Floor;
	if (!World->LineTraceSingleByObjectType(
			Floor, LookFrom,
			LookFrom - FVector(0.0f, 0.0f, DeepestWorthLookingCm),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		// NOTHING UNDER IT MEANS NOTHING TO FOLLOW, so the step keeps the height it
		// already had and the charge is horizontal, which is what it always was.
		// That is the case in every automation test in this project, because a test
		// world built with UWorld::CreateWorld holds no geometry at all.
		return true;
	}

	// HOW MUCH THIS ONE STEP MAY RISE. Two allowances and the larger wins.
	//
	// A SLOPE'S WORTH, which is what makes a ramp followed and a steeper ramp
	// refused. Being proportional to the step, the angle the creature will follow
	// is the same at any frame rate, where a fixed allowance would let a charge up
	// a steeper hill the slower the machine.
	//
	// OR ONE STEP UP, when the ground ahead is flat enough to stand on. A doorway
	// lip is a vertical rise of a few centimetres with no slope to it at all, so
	// the proportional allowance alone would refuse it on a fast frame and allow
	// it on a slow one. MaxStepHeight is the movement component's own answer to
	// the same question for walking, so a charge mounts what a walk would.
	const bool bCouldStandOnIt =
		Floor.ImpactNormal.Z >= Movement->GetWalkableFloorZ();
	const float SlopesWorthCm = StepCm * WalkableRisePerCm;
	const float MostItCouldClimbCm = bCouldStandOnIt
		? FMath::Max(Movement->MaxStepHeight, SlopesWorthCm)
		: SlopesWorthCm;

	const float WantedChangeCm = (Floor.ImpactPoint.Z + HalfHeightCm) - From.Z;

	if (WantedChangeCm > MostItCouldClimbCm)
	{
		return false;
	}

	// AND DOWN NO FASTER THAN IT COULD WALK DOWN, which is a slope's worth and not
	// a step's. Asked of the project owner on 2026-08-18 and answered "run down as
	// steeply as it could walk": a charge that reaches a ledge leaves the edge and
	// descends at the walkable angle until it meets the floor below, rather than
	// dropping to it in one frame. See docs/DECISIONS.md.
	Step.Z = From.Z + FMath::Max(WantedChangeCm, -SlopesWorthCm);
	return true;
}

void ACataclysmEnemyCharacter::SetHealth(float NewMaxHealth)
{
	if (NewMaxHealth <= 0.0f)
	{
		return;
	}

	// REMEMBERED AS WELL AS APPLIED, and that is what makes the order of calls
	// not matter. A spawner naturally sets health on the line after SpawnActor,
	// which on an actor whose BeginPlay has not run yet is before the attribute
	// sets are registered -- so writing then is either lost or, worse, raises an
	// engine ensure. Storing it means InitAbilityActorInfo applies it when the
	// ability system is genuinely ready.
	StartingMaxHealth = NewMaxHealth;
	ApplyStartingAttributes();
}

void ACataclysmEnemyCharacter::SetAttackDamage(float NewAttackDamage)
{
	if (NewAttackDamage < 0.0f)
	{
		return;
	}

	StartingAttackDamage = NewAttackDamage;
	ApplyStartingAttributes();
}

void ACataclysmEnemyCharacter::SetRarityStep(int32 NewStep)
{
	// A NEGATIVE STEP IS A CALLER ERROR, ANSWERED WITH COMMON. There is nothing
	// below Common on the ladder, and clamping beats letting a bad value make
	// IsBoss's comparison quietly meaningless.
	RarityStep = FMath::Max(0, NewStep);

	// A RARER CREATURE IS PHYSICALLY BIGGER, so a player can tell what they
	// are facing before it hits them. Issue #849.
	//
	// THE ACTOR'S SCALE AND NOT THE CAPSULE'S, so the collision capsule, the
	// body and everything attached move together. It is also what makes
	// GetScaledCapsuleRadius report the real size, which is what the telegraph
	// drawing reads, so a bigger creature's attack shapes follow without a
	// second rule.
	SetActorScale3D(FVector(UCataclysmEnemyRarity::BodyScaleForStep(
		UCataclysmEnemyRarity::LoadEnemyRarityTable(), RarityStep)));

	// RE-APPLIED, BECAUSE THE RARITY IS PART OF THE STAT BLOCK SINCE ISSUE #848.
	// Every other setter here re-applies for the reason SetHealth states: a
	// spawner sets these on the lines after SpawnActor, in whatever order suits
	// it, and the order must not matter. This one did not, because rarity used
	// to change nothing about the creature -- only what its corpse dropped.
	ApplyStartingAttributes();
}

void ACataclysmEnemyCharacter::ApplyStartingAttributes()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	// WHAT THE RARITY MULTIPLIES THESE BY. Issue #848: until it, an enemy's
	// rarity changed how much its corpse dropped and nothing else, so a
	// Legendary was exactly as easy to kill as a Common. The figures every
	// spawner passes in are the model's COMMON figures -- `ImpHealth` names
	// `stats_on_floor("Common", ...)` in its own comment -- so scaling from
	// Common is what turns one into the other and leaves Common untouched.
	//
	// THE THREE CLIMB AT DIFFERENT RATES ON PURPOSE. Health multiplies by 1.85 a
	// step and damage by only 1.4, so a Legendary takes 3.4 times as long to
	// kill while hitting under twice as hard. A single multiplier for all three
	// would make every rung above Common a damage race.
	float HealthScale = 1.0f;
	float DamageScale = 1.0f;
	float ArmourScale = 1.0f;
	UCataclysmEnemyRarity::ScalingFromCommon(
		UCataclysmEnemyRarity::LoadEnemyRarityTable(), RarityStep,
		HealthScale, DamageScale, ArmourScale);

	// Writing to an attribute the ability system does not hold yet raises an
	// engine ensure rather than failing quietly, so each is checked rather than
	// attempted. HasAttributeSetForAttribute is false before the component has
	// been initialised.
	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	if (StartingMaxHealth > 0.0f
		&& AbilitySystemComponent->HasAttributeSetForAttribute(MaxHealth))
	{
		const float ScaledHealth = StartingMaxHealth * HealthScale;

		// MAXIMUM FIRST, THEN CURRENT, and the order is not incidental. The vital
		// attribute set clamps health to the maximum in PreAttributeChange, so
		// raising the current value before the maximum would clamp it straight
		// back down to whatever the old maximum was.
		AbilitySystemComponent->SetNumericAttributeBase(MaxHealth, ScaledHealth);
		AbilitySystemComponent->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealthAttribute(), ScaledHealth);
	}

	const FGameplayAttribute Damage =
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute();
	if (StartingAttackDamage > 0.0f
		&& AbilitySystemComponent->HasAttributeSetForAttribute(Damage))
	{
		AbilitySystemComponent->SetNumericAttributeBase(
			Damage, StartingAttackDamage * DamageScale);
	}

	// --- the rest of the designed stat block. Issue #372 ---
	//
	// UNTIL THIS LANDED AN ENEMY HELD THREE ATTRIBUTES OUT OF ROUGHLY TWENTY.
	// Armour, every resistance, evasion and both crit figures sat at the
	// attribute sets' own defaults and nothing ever wrote to them, so the Brute
	// -- which the design document calls heavily armoured and gives the
	// second-highest armour share of the seven vertical slice enemies -- was a
	// slow enemy with extra health and nothing else.
	//
	// WRITTEN DIRECTLY RATHER THAN THROUGH A GameplayEffect, which was the open
	// question on that issue. These are BASE values, and a modifier layers on
	// top of the base either way, so the effect's one real advantage does not
	// apply here. It would need an asset per enemy or a programmatic effect, and
	// issue #355 rebuilds this transport anyway once the archetype numbers are
	// game data. Two mechanisms for one job is worse than one.
	//
	// NO ZERO CHECKS BELOW, unlike the two writes above. Zero armour, zero
	// resistance and zero evasion are all designed values -- the Imp's armour
	// share really is 0.0 -- so treating zero as "not configured" would make an
	// unarmoured creature impossible to express.
	// ARMOUR SCALES WITH RARITY AND THE THREE BELOW IT DO NOT, which is the
	// split FCataclysmEnemyRarityRow states: rarity scales MAGNITUDE, and
	// evasion, critical chance and critical multiplier say what KIND of creature
	// this is. A Legendary Imp is a bigger Imp, not a different one.
	ApplyIfHeld(UCataclysmCombatAttributeSet::GetArmorAttribute(),
				StartingArmour * ArmourScale);
	ApplyIfHeld(UCataclysmCombatAttributeSet::GetEvasionAttribute(), EvasionPercent);
	ApplyIfHeld(UCataclysmCombatAttributeSet::GetCritChanceAttribute(),
				CritChancePercent);
	ApplyIfHeld(UCataclysmCombatAttributeSet::GetCritMultiplierAttribute(),
				CritMultiplierPercent);

	// ONE FIGURE, AND THIS CREATURE HAS NO TYPED RESISTANCES AT ALL -- it does not
	// hold the attribute set they live in. The design model says so in as many
	// words: "percent of all incoming damage resisted, whatever its type. One
	// figure, not eight." A per-type enemy profile is not something the design
	// has, and inventing one here would be inventing design.
	//
	// IT USED TO BE THE SAME NUMBER WRITTEN INTO ALL EIGHT TYPED SLOTS, and that
	// resisted nothing. `ResistanceFor` in CataclysmDamageCalculation.cpp picks a
	// slot from the incoming hit's damage type, and player damage carries no type
	// -- deliberately, because this creature resists everything equally, so a type
	// would be choosing between eight copies of one number. With no type there was
	// no slot to pick and all eight were skipped. Issue #486.
	ApplyIfHeld(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(),
				ResistancePercent);

	// A CREATURE DOES NOT REGENERATE, AND THAT IS A STATED POSITION RATHER THAN
	// AN OMISSION. Issue #653 wired up the three regeneration attributes, which
	// until then no code read at all. UCataclysmVitalAttributeSet's constructor
	// sets health and mana regeneration to 1.0 as a placeholder for a character
	// with no class attached, so leaving them alone would have handed every
	// creature in the game a heal nobody designed: a Brute recovering while the
	// player disengages, and anything walked away from returning to full.
	//
	// THE DESIGN GIVES REGENERATION TO CLASSES, NOT TO CREATURES. Each of the
	// three Demonic class stat lines states a health and a mana regeneration
	// figure. The enemy archetype table has no column for either, and
	// `stats_for` in sim/cataclysm_sim/enemy_stats.py computes no such figure. A
	// creature that should regenerate would be expressed as an archetype
	// property or a modifier, which is issue #355's territory rather than a
	// default nobody chose.
	//
	// ZERO RATHER THAN A SKIPPED TICK, so the rule lives in the creature's own
	// numbers where it can be read and changed, rather than inside the
	// mechanism as a special case for one kind of character.
	ApplyIfHeld(UCataclysmVitalAttributeSet::GetHealthRegenAttribute(), 0.0f);
	ApplyIfHeld(UCataclysmVitalAttributeSet::GetManaRegenAttribute(), 0.0f);
	ApplyIfHeld(UCataclysmVitalAttributeSet::GetEnergyShieldRegenAttribute(),
				0.0f);

	// THE ENERGY SHIELD, WHICH NOTHING WROTE AT ALL UNTIL ISSUE #485. The
	// fraction reached the generated archetype table and the row struct that
	// reads it, and then stopped: there was no property on this class and no
	// write here, so every enemy in the editor had a shield of zero whatever the
	// design said. The layer itself works -- it is step 7 of the eight in
	// UCataclysmDamageCalculation::Resolve -- so the number was the only thing
	// missing.
	//
	// COMPUTED FROM THE FRACTION AND THE MAXIMUM HEALTH, which is the same
	// arithmetic `stats_for` in sim/cataclysm_sim/enemy_stats.py does:
	// `health * energy_shield_fraction`. Storing a second absolute number here
	// would be a figure that could disagree with the health beside it.
	//
	// READ BACK OFF THE ATTRIBUTE RATHER THAN FROM StartingMaxHealth, because
	// this function runs from the setters AND from InitAbilityActorInfo, and a
	// creature whose health was never set through SetHealth still has the
	// attribute set's own maximum. Reading the attribute gives the right answer
	// in both cases; StartingMaxHealth is zero in the second.
	//
	// MAXIMUM FIRST, THEN CURRENT, for the same reason the health write above
	// says: the vital attribute set clamps the current shield to the maximum, so
	// filling it before raising the maximum would clamp it straight back down.
	//
	// NO ZERO CHECK, like the four writes above it and unlike the two at the top.
	// Five of the seven designed enemies have a fraction of exactly 0.00, so
	// treating zero as "not configured" would make an unshielded creature
	// impossible to express.
	const FGameplayAttribute MaxShield =
		UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute();
	if (AbilitySystemComponent->HasAttributeSetForAttribute(MaxShield))
	{
		const float Health = AbilitySystemComponent->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
		const float Shield = FMath::Max(0.0f, Health * EnergyShieldFraction);

		AbilitySystemComponent->SetNumericAttributeBase(MaxShield, Shield);
		AbilitySystemComponent->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), Shield);
	}
}

void ACataclysmEnemyCharacter::ApplyIfHeld(const FGameplayAttribute& Attribute,
										   float Value)
{
	// THE CHECK IS THE WHOLE POINT OF THE HELPER. Writing to an attribute the
	// ability system does not hold yet raises an engine ensure rather than
	// failing quietly, and HasAttributeSetForAttribute is false until the
	// component has been initialised. ApplyStartingAttributes runs both from the
	// setters and from InitAbilityActorInfo, so it really does run before that
	// sometimes.
	if (AbilitySystemComponent
		&& AbilitySystemComponent->HasAttributeSetForAttribute(Attribute))
	{
		AbilitySystemComponent->SetNumericAttributeBase(Attribute, Value);
	}
}

void ACataclysmEnemyCharacter::SetArmour(float NewArmour)
{
	if (NewArmour < 0.0f)
	{
		return;
	}

	StartingArmour = NewArmour;
	ApplyStartingAttributes();
}

void ACataclysmEnemyCharacter::SetEnergyShieldFraction(float NewFraction)
{
	if (NewFraction < 0.0f)
	{
		return;
	}

	EnergyShieldFraction = NewFraction;
	ApplyStartingAttributes();
}

void ACataclysmEnemyCharacter::AttackTarget(AActor* Target)
{
	// The same path a player's skill takes: written into the Damage meta
	// attribute and resolved through the full mitigation order. An enemy with no
	// attack damage set deals nothing and says so once, which ApplyHit handles.
	//
	// AND IT CARRIES TAGS NOW, LIKE A PLAYER'S SKILL DOES. Issue #519.
	UCataclysmSkillEffects::ApplyHit(
		this, Target, AttackPercentOfOwnDamage,
		UCataclysmSkillShapes::TagsFromCell(BasicAttackTags));
}

UAbilitySystemComponent* ACataclysmEnemyCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ACataclysmEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Owner and avatar are both this actor, so there is no possession or
	// replication ordering to wait for. BeginPlay is sufficient on both sides.
	InitAbilityActorInfo();

	// WHAT THIS CREATURE WALKS AT WITH NOTHING HELPING IT, captured before
	// anything can change it. Every creature sets MaxWalkSpeed in its own
	// constructor, so by now it holds the designed figure; recording it here
	// rather than declaring it again per creature means there is one copy of
	// the number and it cannot drift.
	//
	// A CREATURE DESIGNED AT ZERO STAYS AT ZERO. The Corrupted Sentinel's
	// designed speed really is 0.0, and 20% more of nothing is nothing, which is
	// the right answer: an aura does not un-root a turret.
	if (const UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		DesignedWalkSpeedCmPerSecond = Movement->MaxWalkSpeed;
	}
}

void ACataclysmEnemyCharacter::InitAbilityActorInfo()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	AbilitySystemComponent->InitAbilityActorInfo(this, this);

	// Now that the attribute sets are registered, whatever health and attack
	// damage a spawner asked for before this point can finally be written.
	ApplyStartingAttributes();

	if (HasAuthority() && StartingAbilitySet)
	{
		GrantedHandles.TakeFromAbilitySystem(AbilitySystemComponent);
		StartingAbilitySet->GiveToAbilitySystem(AbilitySystemComponent, &GrantedHandles, this);
	}
}
