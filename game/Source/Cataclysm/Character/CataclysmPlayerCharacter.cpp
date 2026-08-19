// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmPlayerCharacter.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Player/CataclysmPlayerState.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/SpringArmComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Half-height and radius of the collision capsule, in centimetres. */
	constexpr float CapsuleRadius = 42.0f;
	constexpr float CapsuleHalfHeight = 96.0f;

}

ACataclysmPlayerCharacter::ACataclysmPlayerCharacter()
{
	// The base class turns ticking off, because most characters have nothing to
	// do every frame. This one eases the camera toward a new distance after the
	// wheel moves. Ticking starts disabled and is switched on only while that
	// glide is running, so a character nobody is zooming costs nothing.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// A second player in a co-operative session is this same class, so both are
	// on the Players side and neither is a legal target for the other's skills.
	TeamId = UCataclysmTeams::IdFor(ECataclysmTeam::Players);

	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);

	// The character faces where it is going, not where the camera points. A
	// top-down game has no independent aim direction to keep the body aligned
	// with, and letting the controller drive yaw would make the character spin
	// with the mouse rather than turn toward its destination.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0f, 640.0f, 0.0f);

	// Movement is confined to the horizontal plane. Dungeon floors are flat and
	// this keeps a click on a wall from pushing the character upward.
	Movement->bConstrainToPlane = true;
	Movement->bSnapToPlaneAtStart = true;

	// A DESIGNED SPEED RATHER THAN THE ENGINE'S 600. Without this line the
	// player moved at UCharacterMovementComponent's own default and no figure
	// from game/Data/ClassStats.csv reached the game at all -- issue #391.
	//
	// THE STARTING POINT, NOT THE ANSWER. The ability system lives on the player
	// state and does not exist yet here, so this is what the pawn walks at until
	// InitAbilityActorInfo hands it over to the MovementSpeed attribute.
	Movement->MaxWalkSpeed = DefaultWalkSpeedCmPerSecond;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);

	// Absolute rotation: the arm keeps its world angle while the character turns
	// underneath it. Without this the whole view swings every time the character
	// changes direction.
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->TargetArmLength = 800.0f;
	CameraBoom->SetRelativeRotation(FRotator(-60.0f, 0.0f, 0.0f));

	// No collision test. The arm would otherwise pull the camera in whenever
	// scenery passed between it and the character, which in a dungeon corridor
	// is most of the time.
	CameraBoom->bDoCollisionTest = false;

	TopDownCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
	TopDownCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCamera->bUsePawnControlRotation = false;

	// The arm length and the sixty degree pitch are a starting point taken from
	// Unreal's own top-down template, not a tuned choice. They are expected to
	// change once there is art and a real sense of scale.

	WeaponSlots = CreateDefaultSubobject<UCataclysmWeaponSlotsComponent>(TEXT("WeaponSlots"));

	// WHAT THE CHARACTER CARRIES. 48 slots, fixed by the design, and
	// empty until something picks a drop up.
	Inventory = CreateDefaultSubobject<UCataclysmInventoryComponent>(TEXT("Inventory"));

	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(RootComponent);
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetRelativeScale3D(FVector(
		(CapsuleRadius * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		(CapsuleRadius * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		(CapsuleHalfHeight * 2.0f) / ACataclysmCharacterBase::BasicShapeSize));

	PlaceholderFacingMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderFacingMarker"));
	PlaceholderFacingMarker->SetupAttachment(RootComponent);
	PlaceholderFacingMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Tipped forward so the cone points along +X, which is the character's
	// forward axis, and pushed out to the front of the body at head height.
	PlaceholderFacingMarker->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
	PlaceholderFacingMarker->SetRelativeLocation(FVector(CapsuleRadius, 0.0f, CapsuleHalfHeight * 0.5f));
	PlaceholderFacingMarker->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.4f));

	// Found by path rather than referenced as an asset, because these are engine
	// content and the project's own Content folder holds no meshes at all yet.
	// A failure here is not fatal: the capsule still moves, it is just invisible.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(CylinderMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(
		TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeMesh.Succeeded())
	{
		PlaceholderFacingMarker->SetStaticMesh(ConeMesh.Object);
	}
}

void ACataclysmPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Taken from the boom rather than repeated as a number here. The resting
	// distance is stated once, in the constructor, and clamping it means a boom
	// set outside the range cannot leave the first wheel notch jumping.
	TargetCameraDistance = FMath::Clamp(CameraBoom->TargetArmLength,
		MinCameraDistance, MaxCameraDistance);
	CameraBoom->TargetArmLength = TargetCameraDistance;

	// THE BASIC ATTACK STARTS LOOKING FOR SOMETHING TO HIT. Nothing swings yet:
	// at this point no weapon is equipped, so the attack speed is zero and the
	// first attempt only re-arms the clock. Issues #36 and #647.
	ScheduleNextBasicAttack(0.0f);
}

void ACataclysmPlayerCharacter::ScheduleNextBasicAttack(
	float SecondsBetweenSwings)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// ZERO MEANS NO RATE, WHICH IS NOT THE SAME AS NO CLOCK. A character holding
	// nothing looks again shortly, so that equipping a weapon starts the basic
	// attack by itself rather than the equip path having to remember to.
	const float Delay = SecondsBetweenSwings > 0.0f ? SecondsBetweenSwings
													: NoWeaponRecheckSeconds;

	World->GetTimerManager().SetTimer(
		BasicAttackTimer, this, &ACataclysmPlayerCharacter::BasicAttackTick,
		Delay, /*bLoop=*/false);
}

void ACataclysmPlayerCharacter::BasicAttackTick()
{
	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(GetAbilitySystemComponent());

	const FGameplayAttribute Speed =
		UCataclysmCombatAttributeSet::GetAttackSpeedAttribute();

	float Interval = 0.0f;
	if (AbilitySystem && AbilitySystem->HasAttributeSetForAttribute(Speed))
	{
		Interval = UCataclysmBasicAttack::SecondsBetweenSwings(
			AbilitySystem->GetNumericAttribute(Speed));
	}

	// THE RATE IS READ FRESH EVERY TIME rather than cached, so swapping a weapon
	// or gaining an increased attack speed affix takes effect on the next swing
	// rather than on the next possession.
	if (Interval > 0.0f
		&& UCataclysmBasicAttack::ShouldSwingNow(
			this, UCataclysmBasicAttack::ReachCmOf(AbilitySystem)))
	{
		UCataclysmBasicAttack::Swing(AbilitySystem);
	}

	ScheduleNextBasicAttack(Interval);
}

void ACataclysmPlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Current = CameraBoom->TargetArmLength;

	// Half a centimetre. FInterpTo approaches its target without ever arriving,
	// so without a threshold the character would tick forever after one notch.
	if (FMath::IsNearlyEqual(Current, TargetCameraDistance, 0.5f))
	{
		CameraBoom->TargetArmLength = TargetCameraDistance;
		SetActorTickEnabled(false);
		return;
	}

	CameraBoom->TargetArmLength = FMath::FInterpTo(Current, TargetCameraDistance,
		DeltaSeconds, CameraZoomInterpSpeed);
}

void ACataclysmPlayerCharacter::AddCameraZoom(float Notches)
{
	if (FMath::IsNearlyZero(Notches))
	{
		return;
	}

	// Subtracted, because a wheel pushed forward reports a positive value and
	// means "closer", and closer is a shorter boom.
	TargetCameraDistance = FMath::Clamp(TargetCameraDistance - Notches * CameraZoomStep,
		MinCameraDistance, MaxCameraDistance);

	SetActorTickEnabled(true);
}

void ACataclysmPlayerCharacter::ApplyMovementSpeed(float MetresPerSecond)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// Refused rather than written. See the header for why zero is not a legal
	// speed here and what would happen if it were.
	if (MetresPerSecond <= 0.0f)
	{
		return;
	}

	Movement->MaxWalkSpeed = MetresPerSecond * CentimetresPerMetre;
}

void ACataclysmPlayerCharacter::OnMovementSpeedChanged(const FOnAttributeChangeData& Data)
{
	ApplyMovementSpeed(Data.NewValue);
}

void ACataclysmPlayerCharacter::HandleDeath()
{
	if (!UCataclysmSkillEffects::MarkDead(this))
	{
		// Already dead. Health can be written at zero repeatedly -- a burn
		// ticking, two hits in one frame -- and the second of those must not
		// restart the timer and hold the player down for ever.
		return;
	}

	// WHATEVER IT WAS DOING STOPS, which is the second of the three things
	// `docs/DECISIONS.md` states an enemy's death is, and it transfers unchanged.
	// DisableMovement clears the velocity too: UCharacterMovementComponent::
	// OnMovementModeChanged runs StopMovementKeepPathing when the new mode is
	// MOVE_None.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->DisableMovement();
	}

	// AND THE PLAYER STOPS DRIVING IT. Without this a dead character still
	// answers the keyboard: it cannot walk, because movement is off, but it can
	// still swing, and a corpse attacking is worse than a corpse standing still.
	//
	// AN ABILITY ALREADY RUNNING IS NOT CANCELLED, and that is deliberate rather
	// than forgotten. This function runs inside the gameplay effect callback that
	// dealt the killing blow -- the same reason an enemy is destroyed on the next
	// tick rather than here -- so cancelling from inside it would tear down
	// something the ability system is still working through. Releasing input is
	// what stops a new one starting. A skill already in flight finishing after
	// its caster died is the rule a projectile already fired follows too.
	if (APlayerController* Driver = Cast<APlayerController>(GetController()))
	{
		DisableInput(Driver);
	}

	// CHOSEN NOW RATHER THAN AT REVIVAL, so a level with no player start puts the
	// character back where it fell instead of at the world origin, which is under
	// the floor in every level this project has.
	RespawnLocation = GetActorLocation();
	RespawnRotation = GetActorRotation();
	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			RespawnLocation = It->GetActorLocation();
			RespawnRotation = It->GetActorRotation();
			break;
		}
	}

	// NOT DESTROYED, UNLIKE AN ENEMY, and that is the design rather than a
	// shortcut. `docs/Cataclysm_GDD_v2.md`: "Ordinary death inside a dungeon is
	// not a run ending", and "A player can delete a character, and that is the
	// only thing that removes one. Nothing that happens in play does."
	//
	// WHAT IS DELIBERATELY NOT CHARGED HERE. The designed penalty is 5 days in
	// Standard, 10 in Hardcore and 15 in Heretic, plus a per-piece equipment drop
	// chance, plus a respawn at the capital. None of it can be applied: the
	// running game has no day clock, no lethality mode, no equipped inventory and
	// no capital. Issue #41 builds the layer that would carry all four. Standing
	// the player back up where the level starts them is the whole of the rule
	// that this game currently has the machinery for, and #570 records the rest
	// as owed.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RespawnTimer, this, &ACataclysmPlayerCharacter::Revive,
			RespawnDelaySeconds, /*bLoop=*/false);
	}
}

void ACataclysmPlayerCharacter::Revive()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimer);
	}

	if (!UCataclysmSkillEffects::ClearDead(this))
	{
		// Not dead, so there is nothing to undo. Refilling here anyway would make
		// this a free heal for anything that called it by mistake.
		return;
	}

	// MOVED BEFORE REFILLING, so a character that comes back in the middle of the
	// pack that killed it is somewhere else by the time it has health to lose.
	// Swept off, because the destination is a spawn point and a sweep from where
	// the body fell would stop against the first thing in the way.
	SetActorLocationAndRotation(RespawnLocation, RespawnRotation,
								/*bSweep=*/false, nullptr,
								ETeleportType::TeleportPhysics);

	// FULL, NOT PARTIAL. No document says what a player comes back with, so the
	// least surprising reading is the one every game in the genre uses: a
	// respawned character is whole and what it lost is measured in the world
	// rather than on the character. Written as the base value rather than through
	// a gameplay effect because there is no heal effect in the project and
	// inventing one to serve a placeholder would be the larger change.
	if (UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent())
	{
		AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealthAttribute(),
			AbilitySystem->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute()));
		AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(),
			AbilitySystem->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()));
		AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetManaAttribute(),
			AbilitySystem->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetMaxManaAttribute()));
	}

	// MOVE_Walking rather than whatever it was, because what it was is MOVE_None:
	// HandleDeath set that and DisableMovement does not remember the previous
	// mode.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
	}

	if (APlayerController* Driver = Cast<APlayerController>(GetController()))
	{
		EnableInput(Driver);
	}
}

bool ACataclysmPlayerCharacter::IsAwaitingRespawn() const
{
	return UCataclysmSkillEffects::IsDead(this);
}

UAbilitySystemComponent* ACataclysmPlayerCharacter::GetAbilitySystemComponent() const
{
	if (const ACataclysmPlayerState* PS = GetPlayerState<ACataclysmPlayerState>())
	{
		return PS->GetAbilitySystemComponent();
	}
	return nullptr;
}

void ACataclysmPlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Server path. The player state exists by the time possession happens.
	InitAbilityActorInfo();
}

void ACataclysmPlayerCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// Client path. PossessedBy does not run on clients, so without this the
	// owning client's ability system would never learn its avatar, and every
	// locally predicted activation and every attribute-driven widget would fail
	// with no obvious cause.
	InitAbilityActorInfo();
}

void ACataclysmPlayerCharacter::InitAbilityActorInfo()
{
	ACataclysmPlayerState* PS = GetPlayerState<ACataclysmPlayerState>();
	if (!PS)
	{
		return;
	}

	UCataclysmAbilitySystemComponent* ASC = PS->GetCataclysmAbilitySystemComponent();
	if (!ASC)
	{
		return;
	}

	// Owner is the player state, which holds the component and survives death.
	// Avatar is this pawn, which is what acts in the world.
	ASC->InitAbilityActorInfo(PS, this);

	// THE PAWN FOLLOWS THE ATTRIBUTE FROM HERE ON, RATHER THAN CARRYING A NUMBER.
	// Movement speed is a stat the design expects to be modified: game/Data/
	// Affixes.csv has an increased movement speed suffix, four boot bases in
	// ItemBases.csv carry one as an implicit, Agility scales it in
	// Attributes.csv, and several enchantments raise and lower it. Writing a
	// figure into MaxWalkSpeed once and leaving it would be the same defect as
	// issue #391 one number later: every one of those sources would move the
	// attribute and none of them would move the character.
	//
	// BOTH SIDES OF THE NETWORK. This runs from PossessedBy on the server and
	// from OnRep_PlayerState on the owning client, so the pawn a client is
	// predicting movement for uses the same speed the server does.
	FOnGameplayAttributeValueChange& SpeedChanged =
		ASC->GetGameplayAttributeValueChangeDelegate(
			UCataclysmCombatAttributeSet::GetMovementSpeedAttribute());

	// Removed before adding, because this function is safe to run twice and on a
	// listen server it does. Without this a second call would leave two handlers
	// on one pawn.
	SpeedChanged.Remove(MovementSpeedChangedHandle);
	MovementSpeedChangedHandle = SpeedChanged.AddUObject(
		this, &ACataclysmPlayerCharacter::OnMovementSpeedChanged);

	// BOUND FIRST, THEN READ. A change arriving between the two would otherwise
	// be missed. GetNumericAttribute answers zero rather than failing when the
	// component holds no combat attribute set, and ApplyMovementSpeed refuses
	// zero, so a pawn whose attributes have not arrived keeps the designed
	// default from the constructor.
	ApplyMovementSpeed(ASC->GetNumericAttribute(
		UCataclysmCombatAttributeSet::GetMovementSpeedAttribute()));

	// Granting is server-only; the results replicate.
	if (!HasAuthority())
	{
		return;
	}

	if (StartingAbilitySet)
	{
		GrantedHandles.TakeFromAbilitySystem(ASC);
		StartingAbilitySet->GiveToAbilitySystem(ASC, &GrantedHandles, this);
	}

	// THE SIX WEAPON SLOTS ARE FILLED HERE AND NOWHERE ELSE, and until issue
	// #169 nothing filled them at all: UCataclysmWeaponSlotsComponent had no
	// caller outside the automation tests, so a play session granted no skill
	// and every skill key reached nothing.
	//
	// It has to happen here rather than in the component's own BeginPlay,
	// because the ability system lives on the PLAYER STATE and does not exist
	// until possession completes. The component says so itself: possession order
	// is not something it controls.
	//
	// Safe to run twice. EquipWeaponType takes back everything it granted before
	// it grants again, so a second possession refills rather than doubling.
	if (WeaponSlots)
	{
		WeaponSlots->EquipStartingWeapon();
	}
}
