// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmPlayerCharacter.h"
#include "Cataclysm.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Player/CataclysmPlayerState.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmWearing.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/SpringArmComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Save/CataclysmSaveWriter.h"

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

	// WHAT THE CHARACTER IS WEARING. Issue #828. Empty until something is
	// equipped, which is the state every character was permanently in before.
	Equipment = CreateDefaultSubobject<UCataclysmEquipmentComponent>(TEXT("Equipment"));

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

	// HERE RATHER THAN IN THE CONSTRUCTOR. A constructor also runs for the
	// class default object, and binding a live pawn's handler from it would
	// put the archetype in the delegate's list. BeginPlay runs once, on the
	// instance, which is what this needs.
	if (Equipment)
	{
		Equipment->EquipmentChanged.AddUObject(
			this, &ACataclysmPlayerCharacter::OnEquipmentChanged);
	}

	// THE CHARACTER PUTS ON ITS STARTING WEAPON. Issue #840: before this, a
	// character wearing nothing still swung a Greataxe, because the ability
	// slots were filled from a weapon TYPE and no item existed to show. The gear
	// panel drew an empty weapon slot and equipping a whip looked like the
	// character getting weaker for no reason.
	//
	// HERE RATHER THAN AT POSSESSION, AND THE ORDER MATTERS. Wearing an item
	// broadcasts EquipmentChanged, which reaches OnEquipmentChanged, which
	// applies the whole class stat line through
	// UCataclysmEquipmentComponent::RefreshAttributes. At this point the ability
	// system does not exist yet -- it lives on the player state and arrives with
	// possession -- so that call does nothing, which is exactly what is wanted.
	// Doing this at possession instead applies a stat line over attributes
	// something else has already set, and fails
	// Cataclysm.Player.MovementSpeedFollowsTheAttribute among others.
	//
	// The axe still reaches the character's attributes: ApplyChosenClassStats
	// runs at possession and asks the equipment for its modifiers.
	//
	// SERVER ONLY, because giving a character an item is a server decision, in
	// the same way granting its abilities is.
	if (HasAuthority())
	{
		GiveStartingWeapon();
	}

	// Taken from the boom rather than repeated as a number here. The resting
	// distance is stated once, in the constructor, and clamping it means a boom
	// set outside the range cannot leave the first wheel notch jumping.
	TargetCameraDistance = FMath::Clamp(CameraBoom->TargetArmLength,
		MinCameraDistance, MaxCameraDistance);
	CameraBoom->TargetArmLength = TargetCameraDistance;

	// THE BASIC ATTACK STARTS LOOKING FOR SOMETHING TO HIT. Nothing swings yet.
	// A weapon is worn by this point, since issue #840, but the ability system
	// lives on the player state and does not exist until possession, so no
	// attack speed has been written to anything. It reads as zero and the first
	// attempt only re-arms the clock. Issues #36 and #647.
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
	// THE SAVE IS WRITTEN FIRST, SYNCHRONOUSLY, AND BEFORE THE CHARACTER IS
	// EVEN MARKED DEAD. `docs/Save_System_Design.md` section 6 names this as
	// the one rule that cannot be relaxed: for a Hardcore character, death is
	// the event the whole feature exists to make stick, so it is written in
	// the same frame health reaches zero, before the death is otherwise
	// processed.
	//
	// BEFORE THE MARK, NOT AFTER IT, AND THAT IS THE POINT. The gather skips
	// a character it can see is dead, so a write placed one line lower would
	// record a floor with nobody standing on it -- and putting that back
	// would restore the fight without the death. Written here, the record
	// holds the character where it fell with the health it had, which is
	// none, so the death is what comes back.
	//
	// IT IS NOT INSIDE THE ONCE-ONLY GUARD BELOW FOR THAT REASON, and it does
	// not need to be: `UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero`
	// already returns early for a character that is dead, so this is reached
	// once. Reaching it twice would write the same bytes twice.
	UCataclysmSaveWriter::NoteTriggerIn(GetWorld(),
										ECataclysmSaveTrigger::CharacterDied);

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

	// THE CLASS STAT LINE, WHICH NOTHING APPLIED UNTIL ISSUE #806. Without it
	// the player kept the placeholder 100 health that
	// UCataclysmVitalAttributeSet's constructor writes, and whose own comment
	// says the real values come from a class stat line. A Brute deals 35 a hit,
	// so three of them killed the character and no dungeon floor could be
	// finished.
	//
	// HERE AND NOT IN InitAbilityActorInfo, AND THE DIFFERENCE IS NOT COSMETIC.
	// That function is documented as safe to run twice and really does: the
	// server reaches it from here and a client from OnRep_PlayerState. Writing a
	// whole stat line from it would overwrite any attribute something else had
	// already set, and would refill the character's health every time it ran.
	// Cataclysm.Player.MovementSpeedFollowsTheAttribute caught exactly that --
	// it sets the movement speed attribute before wiring the pawn up, and a stat
	// line applied from InitAbilityActorInfo overwrote it. Possession happens
	// once, which is what "this character is starting" actually means.
	//
	// WRITING THE MovementSpeed ATTRIBUTE FIRES THE HANDLER InitAbilityActorInfo
	// bound a moment ago, so the pawn's walking speed follows from this without
	// being applied again here.
	ApplyChosenClassStats();
}

void ACataclysmPlayerCharacter::ApplyChosenClassStats()
{
	UCataclysmAbilitySystemComponent* ASC =
		Cast<UCataclysmAbilitySystemComponent>(GetAbilitySystemComponent());
	if (!ASC)
	{
		return;
	}

	// THE CLASS LINE PLUS WHAT IS WORN, and until issue #828 it was the class
	// line alone. GatherModifiers answers an empty map for a character wearing
	// nothing, which gives exactly the old behaviour, so this is not a
	// different result for an unequipped character.
	const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
		Equipment ? Equipment->GatherModifiers()
				  : TMap<FName, TArray<FCataclysmStatModifier>>();

	UCataclysmPlayerClassStats::ApplyTo(
		ASC, UCataclysmPlayerClassStats::LoadTable(),
		UCataclysmPlayerClassStats::ChosenClass(),
		UCataclysmPlayerClassStats::ChosenLevel(),
		&Modifiers,
		// A CHARACTER STARTING. This runs from PossessedBy, which happens once,
		// so filling the pools is right here and is exactly what it did before.
		// OnEquipmentChanged is the path that must not fill them.
		ECataclysmPoolFill::FillToMaximum);
}

void ACataclysmPlayerCharacter::OnEquipmentChanged()
{
	if (UCataclysmAbilitySystemComponent* ASC =
			Cast<UCataclysmAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		if (Equipment)
		{
			Equipment->RefreshAttributes(ASC);
		}
	}

	// THE WEAPON DECIDES WHICH ABILITIES EXIST, so a change to what is worn
	// has to reach the ability slots and not only the numbers. EquipWeaponType
	// takes back everything it granted before it grants again, so calling it
	// for a change that was not a weapon refills the same six slots with the
	// same six abilities rather than doubling them.
	FillAbilitySlotsFromWornWeapon();
}

void ACataclysmPlayerCharacter::FillAbilitySlotsFromWornWeapon()
{
	if (!WeaponSlots)
	{
		return;
	}

	const FString WornWeapon =
		Equipment ? Equipment->EquippedWeaponType() : FString();
	if (!WornWeapon.IsEmpty())
	{
		WeaponSlots->EquipWeaponType(WornWeapon);
		return;
	}

	// THIS IS NOW A NET UNDER A CASE THAT SHOULD NOT ARISE, AND IT SAYS SO WHEN
	// IT CATCHES ONE. It used to be the ordinary path: characters started
	// wearing nothing, so without it taking a ring off would have emptied every
	// ability slot. That is what made issue #840 possible -- an empty weapon
	// slot swung a Greataxe, nothing on screen said so, and equipping a whip
	// read as the character getting weaker for no reason.
	//
	// Two things now stand between a character and an empty weapon slot:
	// GiveStartingWeapon wears a real Greataxe at the start, and
	// UCataclysmWearing::TakeOffInto refuses to remove the only weapon worn
	// (issue #841). So reaching here means one of those failed -- most likely
	// the item bases table could not be read, or StartingWeaponBase names a row
	// that is not there.
	if (Equipment)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("No weapon is worn, so the ability slots were filled from the "
				 "starting weapon type instead of from an item. Since issue "
				 "#840 a character is supposed to be wearing a real weapon by "
				 "this point. Check that StartingWeaponBase on the player "
				 "character names a row in game/Data/ItemBases.csv and that the "
				 "table loaded."));
	}

	WeaponSlots->EquipStartingWeapon();
}

void ACataclysmPlayerCharacter::GiveStartingWeapon()
{
	if (!Equipment || StartingWeaponBase.IsNone())
	{
		return;
	}

	// ASKED RATHER THAN REMEMBERED, so that running twice cannot produce two
	// axes. InitAbilityActorInfo runs from PossessedBy and from
	// OnRep_PlayerState, and on a listen server both happen. Anything at all in
	// either weapon slot means there is nothing to do: either this already ran,
	// or the character loaded a save holding something better, and replacing
	// that would be worse than doing nothing.
	for (const ECataclysmGearSlot WeaponSlot :
		 UCataclysmGearSlots::WeaponSlots())
	{
		if (!Equipment->SlotIsEmpty(WeaponSlot))
		{
			return;
		}
	}

	// AN EVERYDAY ITEM, AND THAT IS NOT A CHOICE MADE HERE. Rarity is computed
	// from what fills an item's four slots rather than stored, so a base with no
	// affixes and no enchantments IS an Everyday. UCataclysmItemValues::RarityOf.
	FCataclysmItem Starting;
	Starting.Base = StartingWeaponBase;
	Starting.GearLevel = 0;

	FCataclysmItem CameOff;
	FCataclysmItem AlsoCameOff;
	ECataclysmGearSlot WentTo = ECataclysmGearSlot::Weapon1;
	const ECataclysmEquipResult Result =
		Equipment->Equip(Starting, CameOff, AlsoCameOff, WentTo);

	if (Result != ECataclysmEquipResult::Equipped)
	{
		// LOUD, BECAUSE THE SYMPTOM IS THE BUG THIS FIXED. A character who does
		// not get this weapon falls through to the starting weapon type above
		// and swings a Greataxe nobody can see.
		UE_LOG(LogCataclysm, Error,
			TEXT("The starting weapon %s could not be worn, so the character "
				 "begins holding nothing visible. Check that it names a weapon "
				 "row in game/Data/ItemBases.csv."),
			*StartingWeaponBase.ToString());
	}
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
	//
	// THE SLOTS ARE FILLED FROM THE WORN WEAPON SINCE ISSUE #840, and the weapon
	// itself is put on in BeginPlay rather than here. Before #840 this filled
	// them from a weapon type with no item behind it, so the gear panel showed
	// an empty weapon slot on a character swinging a Greataxe.
	//
	// FillAbilitySlotsFromWornWeapon AND NOT OnEquipmentChanged, which is the
	// same call with UCataclysmEquipmentComponent::RefreshAttributes in front of
	// it. That applies the whole class stat line, and this function runs more
	// than once -- the server reaches it from PossessedBy and a client from
	// OnRep_PlayerState -- so a stat line applied here overwrites attributes
	// something else has already set. Calling OnEquipmentChanged from here was
	// tried and failed three tests: Cataclysm.Player.MovementSpeedFollowsTheAttribute,
	// Cataclysm.PlayerStats.APlayerCharacterLeavesThePlaceholderBehind and
	// Cataclysm.Death.APlayerStandsBackUpRatherThanBeingRemoved. The comment on
	// ApplyChosenClassStats above says the same thing and names the same test.
	FillAbilitySlotsFromWornWeapon();
}

// ---------------------------------------------------------------------------
// Wearing something, from the console
// ---------------------------------------------------------------------------

/**
 * `Cataclysm.Equip`, `Cataclysm.Unequip` and `Cataclysm.ShowEquipment`.
 *
 * WHY CONSOLE COMMANDS AND NOT THE INVENTORY SCREEN. Issue #828 built the
 * equipment slots and made a worn item's stats reach the character. The screen
 * that lets a player do it by clicking is step 5 of that issue and is not built:
 * `UCataclysmInventoryWidget` draws the 48 carried cells and has no gear panel.
 *
 * Without these three commands the whole system would be unreachable in play --
 * every rule tested, nothing usable -- so somebody would have to take the tests
 * on trust. They are a stepping stone to the screen and not a substitute for it.
 *
 * THE BAG IS WHERE EVERYTHING COMES FROM AND GOES BACK TO, because an item that
 * is neither worn nor carried has been destroyed. `AddItem` answers where an
 * item went rather than assuming it went anywhere, and every use below reads
 * that answer.
 */
namespace CataclysmEquipConsole
{
	/** The player's pawn as this class, or null with the reason printed. */
	ACataclysmPlayerCharacter* Player(UWorld* World, FOutputDevice& Ar)
	{
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ACataclysmPlayerCharacter* Character =
			Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
		if (!Character)
		{
			Ar.Log(TEXT("There is no player character. Press Play first."));
		}
		return Character;
	}

	/** A gear slot named on the command line, or Count when it names none. */
	ECataclysmGearSlot SlotNamed(const FString& Name)
	{
		for (const ECataclysmGearSlot Slot : UCataclysmGearSlots::AllSlots())
		{
			const UEnum* Enum = StaticEnum<ECataclysmGearSlot>();
			const FString Plain = Enum
				? Enum->GetNameStringByValue(static_cast<int64>(Slot)) : FString();
			if (Plain.Equals(Name, ESearchCase::IgnoreCase))
			{
				return Slot;
			}
		}
		return ECataclysmGearSlot::Count;
	}

	/** Every slot name, for a message that has to tell somebody what to type. */
	FString SlotNames()
	{
		TArray<FString> Names;
		const UEnum* Enum = StaticEnum<ECataclysmGearSlot>();
		for (const ECataclysmGearSlot Slot : UCataclysmGearSlots::AllSlots())
		{
			if (Enum)
			{
				Names.Add(Enum->GetNameStringByValue(static_cast<int64>(Slot)));
			}
		}
		return FString::Join(Names, TEXT(", "));
	}
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmEquip(
	TEXT("Cataclysm.Equip"),
	TEXT("Wear the item in a carried inventory slot, 0 to 47. Whatever comes "
		 "off goes back into the bag."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmEquipConsole;

			ACataclysmPlayerCharacter* Character = Player(World, Ar);
			if (!Character)
			{
				return;
			}

			if (Args.Num() < 1)
			{
				Ar.Log(TEXT("Cataclysm.Equip <carried slot 0 to 47>. "
							"Cataclysm.ShowEquipment lists what is carried."));
				return;
			}

			// THE RULE IS NOT HERE, AND IT USED TO BE. Every step of taking an
			// item out of the bag, putting it on and stowing what came off lived
			// inside this lambda when the equipment slots landed in issue #828.
			// The gear panel of issue #831 needs exactly the same steps, and a
			// second copy of a rule whose failure mode is a destroyed item is
			// not a thing to have. UCataclysmWearing is the one copy, and unlike
			// a console command it can be tested.
			ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
			const ECataclysmWearResult Result = UCataclysmWearing::WearFromCarried(
				Character->GetInventory(), Character->GetEquipment(),
				FCString::Atoi(*Args[0]), Slot);

			Ar.Log(*UCataclysmWearing::Explain(Result));

			if (Result == ECataclysmWearResult::Worn
				|| Result == ECataclysmWearResult::Swapped)
			{
				Ar.Logf(TEXT("It went in the %s slot."),
						*UCataclysmGearSlots::DisplayName(Slot));
			}
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmUnequip(
	TEXT("Cataclysm.Unequip"),
	TEXT("Take off what is worn in a named gear slot and put it in the bag."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmEquipConsole;

			ACataclysmPlayerCharacter* Character = Player(World, Ar);
			if (!Character)
			{
				return;
			}

			if (Args.Num() < 1)
			{
				Ar.Logf(TEXT("Cataclysm.Unequip <slot>. The slots are: %s"),
						*SlotNames());
				return;
			}

			const ECataclysmGearSlot Slot = SlotNamed(Args[0]);
			if (Slot == ECataclysmGearSlot::Count)
			{
				Ar.Logf(TEXT("There is no slot called %s. The slots are: %s"),
						*Args[0], *SlotNames());
				return;
			}

			Ar.Log(*UCataclysmWearing::Explain(UCataclysmWearing::TakeOffInto(
				Character->GetInventory(), Character->GetEquipment(), Slot)));
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmShowEquipment(
	TEXT("Cataclysm.ShowEquipment"),
	TEXT("List what is worn, and what is carried, with the slot numbers "
		 "Cataclysm.Equip takes."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmEquipConsole;

			ACataclysmPlayerCharacter* Character = Player(World, Ar);
			if (!Character || !Character->GetInventory() || !Character->GetEquipment())
			{
				return;
			}

			Ar.Log(TEXT("--- worn ---"));
			for (const ECataclysmGearSlot Slot : UCataclysmGearSlots::AllSlots())
			{
				const FCataclysmItem* Worn = Character->GetEquipment()->EquippedAt(Slot);
				Ar.Logf(TEXT("  %-12s %s"),
						*UCataclysmGearSlots::DisplayName(Slot),
						Worn ? *Worn->Base.ToString() : TEXT("-"));
			}

			Ar.Log(TEXT("--- carried ---"));
			const TArray<FCataclysmCarriedSlot>& Carried =
				Character->GetInventory()->GetSlots();
			for (int32 Index = 0; Index < Carried.Num(); ++Index)
			{
				const FCataclysmItem* Item = Character->GetInventory()->ItemAt(Index);
				if (Item)
				{
					Ar.Logf(TEXT("  %2d  %s"), Index, *Item->Base.ToString());
				}
			}

			Ar.Logf(TEXT("Weapon type in hand: %s"),
					Character->GetEquipment()->EquippedWeaponType().IsEmpty()
						? TEXT("none")
						: *Character->GetEquipment()->EquippedWeaponType());
		}));
