// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmPlayerCharacter.h"
#include "Cataclysm.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
// For the Cataclysm.ShowDebuffs console command. Issue #962.
#include "AbilitySystem/CataclysmDebuffs.h"
// For the Cataclysm.ShowFervour console command. Issue #954.
#include "AbilitySystem/CataclysmFervour.h"
// For the Cataclysm.ShowDebt console command. Issue #1100.
#include "AbilitySystem/CataclysmHealthDebt.h"
#include "AbilitySystem/CataclysmLowHealthRelief.h"
// For a weapon left standing in the ground, whose hands are drawn empty.
// Issue #1141.
#include "AbilitySystem/CataclysmPlantedWeapon.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
// For the Cataclysm.ShowStacks console command. Issue #1002.
#include "AbilitySystem/CataclysmStacks.h"
// For when the swing this clip is about to play actually connects. Issue #1133.
#include "AbilitySystem/CataclysmSwingTiming.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmCharacterCreation.h"
#include "Data/CataclysmDataRows.h"
#include "Character/CataclysmPassivePoints.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Character/CataclysmExperience.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Character/CataclysmEnemyDeath.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Items/CataclysmWeaponMeshes.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "Player/CataclysmGameMode.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmWearing.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/SpringArmComponent.h"
#include "TimerManager.h"
#include "UObject/SoftObjectPath.h"
#include "Save/CataclysmSaveWriter.h"

namespace
{
	/** Half-height and radius of the collision capsule, in centimetres. */
	constexpr float CapsuleRadius = 42.0f;
	constexpr float CapsuleHalfHeight = 96.0f;

}

const TCHAR* ACataclysmPlayerCharacter::BodyMeshPath =
	TEXT("/Game/Characters/Mannequins/Meshes/"
		 "SKM_Manny_Simple.SKM_Manny_Simple");

const TCHAR* ACataclysmPlayerCharacter::AnimationBlueprintPath =
	TEXT("/Game/Characters/Mannequins/Anims/Unarmed/"
		 "ABP_Unarmed.ABP_Unarmed_C");

const TCHAR* ACataclysmPlayerCharacter::AttackAnimationFolder =
	TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack");

// THREE OF THE FOUR THE ENGINE SHIPS, AND THE ORDER IS THE CYCLE. See the
// header for why MM_ChargedAttack is not among them.
const TCHAR* ACataclysmPlayerCharacter::AttackAnimationNames[3] = {
	TEXT("MM_Attack_01"),
	TEXT("MM_Attack_02"),
	TEXT("MM_Attack_03"),
};

const FName ACataclysmPlayerCharacter::AttackSlotName = TEXT("DefaultSlot");

const TCHAR* ACataclysmPlayerCharacter::DeathAnimationFolder =
	TEXT("/Game/Characters/Mannequins/Anims/Death");

// ALL SIX, AND THE ORDER IS THE DRAW. `UCataclysmEnemyDeath::ClipToPlay` picks
// an index into this array, so reordering these changes which clip a given
// death plays. There is no reason to prefer one over another: nothing carries
// the direction a killing blow came from, which is what would let the front,
// back, left and right clips be chosen rather than drawn.
const TCHAR* ACataclysmPlayerCharacter::DeathAnimationNames[6] = {
	TEXT("MM_Death_Front_01"),
	TEXT("MM_Death_Front_02"),
	TEXT("MM_Death_Front_03"),
	TEXT("MM_Death_Back_01"),
	TEXT("MM_Death_Left_01"),
	TEXT("MM_Death_Right_01"),
};

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

	// THE BODY IS PUT ON IN BeginPlay, NOT HERE. Issue #1124. What used to be
	// here was a cylinder and a cone from /Engine/BasicShapes, found with
	// ConstructorHelpers because they are engine content and cost nothing to
	// reference. The Mannequin is project content and is loaded by path at
	// BeginPlay instead, so that the class default object built during module
	// startup does not drag a 15 MB skeletal mesh in with it. See ResolveBody.
	//
	// THE MESH COMPONENT ITSELF ALREADY EXISTS. `ACharacter` creates one in its
	// own constructor and `GetMesh()` returns it; nothing here has to make one.
	// It is empty until ResolveBody puts a mesh on it.

	// WHAT IS HELD IN EACH HAND. Issue #1125. Empty until something is
	// equipped, and attached to the mesh's hand sockets rather than to the
	// capsule so that they follow the animation instead of floating beside it.
	//
	// ATTACHED IN THE CONSTRUCTOR AND FILLED LATER. A socket name is resolved
	// when the skeletal mesh arrives, so naming one that does not exist yet is
	// correct rather than early: until it appears the component simply follows
	// the component it is attached to.
	RightHandWeapon = CreateDefaultSubobject<UStaticMeshComponent>(
		TEXT("RightHandWeapon"));
	RightHandWeapon->SetupAttachment(GetMesh(),
									 UCataclysmWeaponMeshes::RightHandSocket);

	LeftHandWeapon = CreateDefaultSubobject<UStaticMeshComponent>(
		TEXT("LeftHandWeapon"));
	LeftHandWeapon->SetupAttachment(GetMesh(),
									UCataclysmWeaponMeshes::LeftHandSocket);

	// NO COLLISION ON EITHER. A weapon in this game hits through the ability
	// system rather than by touching anything -- see UCataclysmStrikeTemplate --
	// so collision here would only push the character around and catch on
	// scenery.
	RightHandWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RightHandWeapon->SetGenerateOverlapEvents(false);
	LeftHandWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LeftHandWeapon->SetGenerateOverlapEvents(false);
}

void ACataclysmPlayerCharacter::RefreshWeaponMeshes()
{
	if (!RightHandWeapon || !LeftHandWeapon)
	{
		return;
	}

	// A WEAPON LEFT STANDING IN THE GROUND IS NOT IN A HAND. The Greatsword's
	// Buried Fire: "drive the greatsword into the ground and leave it there ...
	// you fight unarmed until you do." The item is still worn -- nothing is
	// unequipped, and what makes the character unarmed is a refusal asked in
	// `UCataclysmSkillTemplate::CanActivateAbility` -- so the ordinary path below
	// would go on drawing a greatsword the player can see is somewhere else.
	//
	// ASKED HERE RATHER THAN FLAGGED ON THE CHARACTER, which is the shape
	// `ACataclysmPlantedWeapon::HeldBy`'s other callers take. Nothing has to be
	// cleared when the sword comes back: the actor is destroyed, and its own
	// `EndPlay` calls this again to fill the hands.
	if (ACataclysmPlantedWeapon::HeldBy(this))
	{
		RightHandWeapon->SetStaticMesh(nullptr);
		LeftHandWeapon->SetStaticMesh(nullptr);
		return;
	}

	const UDataTable* Table = UCataclysmWeaponMeshes::LoadTable();

	// THE TWO SLOTS THE DESIGN HAS, RIGHT THEN LEFT. A two-handed weapon sits in
	// Weapon1 and blocks Weapon2, so it draws in the right hand and the left
	// stays empty. See the header for why that is a limitation and not a
	// decision.
	const TPair<ECataclysmGearSlot, UStaticMeshComponent*> Hands[] = {
		{ECataclysmGearSlot::Weapon1, RightHandWeapon},
		{ECataclysmGearSlot::Weapon2, LeftHandWeapon},
	};

	for (const TPair<ECataclysmGearSlot, UStaticMeshComponent*>& Hand : Hands)
	{
		const FCataclysmItem* Worn =
			Equipment ? Equipment->EquippedAt(Hand.Key) : nullptr;

		if (!Worn)
		{
			// EMPTIED RATHER THAN LEFT ALONE, AND THIS IS THE HALF THAT WOULD
			// BREAK SILENTLY. Taking a weapon off has to remove it from the
			// hand, or the character goes on holding a weapon it no longer has
			// while every number says otherwise.
			Hand.Value->SetStaticMesh(nullptr);
			continue;
		}

		// SAID WHEN A BASE HAS NO ROW AT ALL, which is a different thing from a
		// base that draws nothing on purpose. tools/generate_datatables.py
		// refuses a weapon base with no row, so reaching this means the table on
		// disk is older than the design -- exactly the case that would otherwise
		// look like a weapon quietly failing to appear.
		if (Table && !UCataclysmWeaponMeshes::HasRowFor(Table, Worn->Base))
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("%s is worn but has no row in DT_WeaponMeshes, so nothing "
					 "is drawn for it. Every weapon base needs a row, including "
					 "the ones that draw nothing. Regenerate the table with "
					 "tools/generate_datatable_assets.py."),
				*Worn->Base.ToString());
		}

		// NOT CALLED `Mesh`. `ACharacter` has a member of that name -- the
		// character's own skeletal mesh -- and the build refuses a local that
		// hides it, which is the right call twice over here: the two are one
		// letter apart in meaning and the wrong one would compile.
		float Scale = 1.0f;
		UStaticMesh* WeaponMesh =
			UCataclysmWeaponMeshes::MeshFor(Table, Worn->Base, Scale);

		Hand.Value->SetStaticMesh(WeaponMesh);
		Hand.Value->SetRelativeScale3D(FVector(Scale));
	}
}

bool ACataclysmPlayerCharacter::ResolveBody()
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
		// NOT AN ERROR, AND NOT FATAL. A checkout without the Mannequin assets
		// gets an invisible capsule that still walks, still fights and still
		// dies. Everything this class does other than being looked at is
		// unaffected, which is why this is a warning and the function returns
		// rather than refusing to start.
		UE_LOG(LogCataclysm, Warning,
			TEXT("The player's Mannequin mesh was not found at %s, so the "
				 "character will be invisible. It still walks and still "
				 "fights. See game/docs/player-source-assets.md."),
			BodyMeshPath);
		return false;
	}

	MeshComponent->SetSkeletalMesh(Body);

	// FEET ON THE CAPSULE BOTTOM, AND THE ENGINE'S YAW. The same two lines the
	// Brute and the Abyssal Warden use, for the same two reasons. A skeletal
	// mesh is authored with its origin at the feet and a capsule's origin is its
	// centre, so the mesh drops by the half-height or the character walks around
	// with its waist at floor level. The -90 degree yaw is the engine's
	// convention for character meshes, which face -Y while the actor faces +X;
	// without it the character walks sideways.
	MeshComponent->SetRelativeLocationAndRotation(
		FVector(0.0f, 0.0f, -CapsuleHalfHeight),
		FRotator(0.0f, -90.0f, 0.0f));

	ResolveAnimationBlueprint(MeshComponent);

	// THE DEATH CLIPS, AND A NULL ENTRY IS KEPT rather than skipped, which is
	// the rule ACataclysmEnemyCharacter::PlayDeathAnimation already follows:
	// dropping one would change how many clips there are and therefore which one
	// every other death draws.
	DeathAnimations.Reset();
	int32 Found = 0;
	for (const TCHAR* Name : DeathAnimationNames)
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"),
											 DeathAnimationFolder, Name, Name);
		UAnimSequence* Clip =
			Cast<UAnimSequence>(FSoftObjectPath(Path).TryLoad());
		DeathAnimations.Add(Clip);
		Found += Clip ? 1 : 0;
	}

	if (Found < static_cast<int32>(UE_ARRAY_COUNT(DeathAnimationNames)))
	{
		// SAID EVEN THOUGH THE MESH LOADED, because these are separate assets
		// and one can be missing while the body is not. A character with no
		// death clip dies exactly as it did before this change: it stops, and
		// stands there until it comes back.
		UE_LOG(LogCataclysm, Warning,
			TEXT("The player found %d of %d death clips in %s. A death with no "
				 "clip stands still instead of falling."),
			Found, static_cast<int32>(UE_ARRAY_COUNT(DeathAnimationNames)),
			DeathAnimationFolder);
	}

	// THE ATTACK CLIPS. Issue #1126. Loaded here for the same reason the death
	// clips above are: once, when the body goes on, rather than on every swing.
	// A basic attack fires every two thirds of a second at a fast weapon, so
	// resolving a path each time would be a load attempt several times a second.
	//
	// A NULL ENTRY IS DROPPED HERE, UNLIKE THE DEATH CLIPS. Those keep their
	// nulls because a random draw over a fixed count decides which one plays,
	// and removing one would change every other draw. These are cycled through
	// in order, so a missing clip should simply not be part of the cycle rather
	// than showing as a swing where the character does nothing.
	AttackAnimations.Reset();
	for (const TCHAR* Name : AttackAnimationNames)
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"),
											 AttackAnimationFolder, Name, Name);
		if (UAnimSequence* Clip =
				Cast<UAnimSequence>(FSoftObjectPath(Path).TryLoad()))
		{
			AttackAnimations.Add(Clip);
		}
	}
	NextAttackAnimation = 0;

	if (AttackAnimations.Num() < UE_ARRAY_COUNT(AttackAnimationNames))
	{
		// SAID EVEN THOUGH THE MESH LOADED, for the same reason the death clip
		// warning above is. A character with no attack clip fights exactly as it
		// did before this change: damage lands and the body does not move.
		UE_LOG(LogCataclysm, Warning,
			TEXT("The player found %d of %d attack clips in %s. A skill with no "
				 "clip deals its damage without the character moving."),
			AttackAnimations.Num(),
			static_cast<int32>(UE_ARRAY_COUNT(AttackAnimationNames)),
			AttackAnimationFolder);
	}

	UE_LOG(LogCataclysm, Verbose,
		TEXT("The player character is wearing %s."), BodyMeshPath);

	return true;
}

void ACataclysmPlayerCharacter::PlayAttackAnimation()
{
	// CLEARED FIRST, AND EVERY EARLY RETURN BELOW DEPENDS ON IT. Issue #1133.
	// Zero means "the blow lands now". Five of the paths through this function
	// give up without playing anything -- no clips, no animation Blueprint, a
	// null clip, a clip of no length -- and each one has to leave a character
	// that deals damage at once rather than one still holding the delay its
	// previous swing worked out. Setting it only on success would make a
	// refused animation silently reuse the last figure.
	SwingConnectsInSeconds = 0.0f;

	if (AttackAnimations.Num() == 0)
	{
		return;
	}

	USkeletalMeshComponent* MeshComponent = GetMesh();
	UAnimInstance* AnimInstance =
		MeshComponent ? MeshComponent->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		// NO ANIMATION BLUEPRINT MEANS NO SLOT TO PLAY INTO, which is the state
		// a checkout without the Mannequin assets is in. Playing the clip onto
		// the component instead is deliberately not done: that is what a death
		// does, and it would leave the character holding the last frame of a
		// swing until something else took the mesh back.
		return;
	}

	// IN TURN RATHER THAN AT RANDOM. See the header: a basic attack fires often
	// enough that a random draw over three clips repeats one about a third of
	// the time, which reads as the animation sticking rather than as variety.
	UAnimSequence* Clip =
		AttackAnimations[NextAttackAnimation % AttackAnimations.Num()].Get();
	NextAttackAnimation = (NextAttackAnimation + 1) % AttackAnimations.Num();

	if (!Clip)
	{
		return;
	}

	const float Length = Clip->GetPlayLength();
	if (Length <= 0.0f)
	{
		return;
	}

	// NEVER SLOWER THAN AUTHORED, ONLY FASTER, AND ONLY WHEN IT MUST BE. The
	// Abyssal Warden's rule, and it is here for a sharper version of the same
	// reason. Every attack clip is longer than the interval a weapon's attack
	// speed allows: the shortest is 1.0 second against an interval of 0.833 down
	// to 0.667. Stretching a clip to fill a longer window reads as slow motion,
	// which is why the floor is 1.
	//
	// AN INTERVAL OF ZERO MEANS NOTHING IS DRIVING A RATE. A character with no
	// weapon, or one whose ability system has not arrived yet, has no attack
	// speed to fit to, so the clip plays at the speed it was authored at.
	float Rate = 1.0f;
	float Interval = 0.0f;
	if (const UCataclysmAbilitySystemComponent* AbilitySystem =
			Cast<UCataclysmAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		Interval = UCataclysmBasicAttack::SecondsBetweenSwingsFor(AbilitySystem);
		if (Interval > 0.0f)
		{
			Rate = FMath::Clamp(FMath::Max(1.0f, Length / Interval),
								1.0f, MaximumAttackPlayRate);
		}
	}

	// WHEN THIS SWING WILL CONNECT, LEFT FOR THE SKILL TO READ. Issue #1133.
	// Until this line every skill dealt its damage in the frame it activated,
	// while the swing played for a second or more beside it, so an enemy was
	// hurt while the weapon was still going backwards.
	//
	// THIS IS THE ONLY PLACE THAT CAN WORK IT OUT, which is why it is here and
	// not in the skill. The answer depends on which of the three clips came up
	// in the cycle and on the rate that clip is being played at, and both are
	// decided immediately above.
	//
	// THE CLIP'S OWN MARKER FIRST. `MarkedStrikeSeconds` answers negative for a
	// clip that carries no `UCataclysmSwingConnectsNotify`, and
	// `SecondsUntilSwingConnects` falls back to the middle of the clip for that
	// case, so an unmarked clip behaves sensibly rather than failing.
	SwingConnectsInSeconds = UCataclysmSwingTiming::SecondsUntilSwingConnects(
		Length, UCataclysmSwingTiming::MarkedStrikeSeconds(Clip), Rate, Interval);

	// THE SWING DOES NOT MOVE THE CHARACTER, AND WHAT STOPS IT IS NOT HERE. The
	// animation instance is set to ERootMotionMode::IgnoreRootMotion when the
	// animation Blueprint is bound; see ResolveAnimationBlueprint, which also
	// records what was tried first and why it did nothing.
	AnimInstance->PlaySlotAnimationAsDynamicMontage(
		Clip, AttackSlotName, AttackBlendInSeconds, AttackBlendOutSeconds,
		Rate, /*LoopCount=*/1);
}

bool ACataclysmPlayerCharacter::ResolveAnimationBlueprint(
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
		// THE SAME FALLBACK THE ABYSSAL WARDEN HAS, and for once it is expected
		// not to be reached: ABP_Unarmed ships with the Mannequin and is copied
		// beside it, unlike ABP_AbyssalWarden which has never been authored.
		// Without it the character stands in its reference pose and slides.
		UE_LOG(LogCataclysm, Warning,
			TEXT("The player's animation Blueprint was not found at %s, so the "
				 "character will slide rather than walk. Its death clips are "
				 "played onto the component directly and are unaffected."),
			AnimationBlueprintPath);

		// SAID OUTRIGHT RATHER THAN LEFT AT THE DEFAULT, which is what the
		// Warden does and for the reason recorded there: a mesh component
		// starts in single-node mode, so this line changes nothing today and is
		// what stops the fallback breaking silently if that default ever moves.
		MeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		return false;
	}

	MeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	MeshComponent->SetAnimInstanceClass(AnimationClass);

	// NO ANIMATION EVER MOVES THIS CHARACTER. Issue #1126.
	//
	// WHAT WENT WRONG WITHOUT IT. The project owner played the first version of
	// the attack animations on 2026-09-01 and reported that every ability made
	// the character surge forward: "it's just a slide forward". All three attack
	// clips carry root motion, and a character's animation instance defaults to
	// `RootMotionFromMontagesOnly`, so every swing drove the capsule.
	//
	// `IgnoreRootMotion` MEANS "EXTRACT IT BUT DO NOT APPLY IT", which is the
	// one that is wanted. `NoRootMotionExtraction` leaves the motion in the pose
	// instead, so the mesh would walk away from the capsule it is attached to.
	//
	// WHAT WAS TRIED FIRST AND DOES NOTHING, recorded so it is not tried again:
	// clearing `bEnableRootMotionTranslation` and `bEnableRootMotionRotation` on
	// the montage after playing it. Those two are read in `UAnimMontage::PostLoad`
	// and nowhere else at run time -- they push a setting onto the referenced
	// sequence when the asset loads. What actually decides it is
	// `UAnimMontage::HasRootMotion()`, which asks each sequence and never looks
	// at either flag, and `UAnimInstance::Montage_Play` consults it while
	// starting the montage, so a flag set afterwards is set too late twice over.
	//
	// ON THE ANIMATION INSTANCE RATHER THAN PER MONTAGE, because it is a
	// statement about this character: nothing it plays is meant to move it. A
	// skill that should move the character has a Movement shape and moves it
	// through the movement component, which this does not touch.
	if (UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance())
	{
		AnimInstance->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
	}

	return true;
}

float ACataclysmPlayerCharacter::PlayDeathAnimation()
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return 0.0f;
	}

	// ITS OWN STREAM, seeded from this character and the moment it died, so two
	// deaths in the same session do not always fall the same way.
	//
	// NO SALT, UNLIKE THE ENEMIES'. `ACataclysmEnemyCharacter` salts its death
	// draw so it cannot share a stream with the one its drops were rolled from.
	// A player character drops nothing on death -- issue #529 is where that
	// would be decided -- so there is no second stream to be kept apart from.
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	FRandomStream Stream(GetUniqueID() ^ static_cast<int32>(Now * 1000.0f));

	const int32 Index =
		UCataclysmEnemyDeath::ClipToPlay(DeathAnimations.Num(), Stream);
	if (!DeathAnimations.IsValidIndex(Index))
	{
		return 0.0f;
	}

	UAnimSequence* Clip = DeathAnimations[Index].Get();
	if (!Clip)
	{
		return 0.0f;
	}

	// SINGLE-NODE MODE, WHICH IS WHAT MAKES THE BODY STAY DOWN. See the header:
	// the clips are about 1.1 seconds and the character lies dead for 3, so a
	// montage through the animation Blueprint's slot would blend back to an
	// idle stand and hold it for the rest. Played onto the component the clip
	// holds its last frame. `Revive` puts the animation Blueprint back.
	MeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	MeshComponent->PlayAnimation(Clip, /*bLooping=*/false);
	MeshComponent->SetPlayRate(1.0f);

	return Clip->GetPlayLength();
}

void ACataclysmPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	// THE CHARACTER PUTS ITS BODY ON. Issue #1124. Before this the player was a
	// cylinder with a cone on the front and could not play anything at all.
	// A failure here is logged and survivable; see ResolveBody.
	ResolveBody();

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

	// AND WHATEVER IS WORN GETS DRAWN. Issue #1125.
	//
	// HERE AS WELL AS ON OnEquipmentChanged, AND BOTH ARE NEEDED. Equipping
	// broadcasts, so the starting weapon above already reaches
	// RefreshWeaponMeshes through the handler bound earlier in this function.
	// A character that arrives ALREADY WEARING something broadcasts nothing --
	// GiveStartingWeapon deliberately does nothing when a weapon is already on,
	// which is the case for a character restored from a save -- and without
	// this line that character would stand there with empty hands and a full
	// gear panel.
	//
	// RUNNING TWICE COSTS A TABLE LOOKUP AND CHANGES NOTHING, because the
	// function reads what is worn rather than toggling anything.
	RefreshWeaponMeshes();

	// Taken from the boom rather than repeated as a number here. The resting
	// distance is stated once, in the constructor, and clamping it means a boom
	// set outside the range cannot leave the first wheel notch jumping.
	TargetCameraDistance = FMath::Clamp(CameraBoom->TargetArmLength,
		MinCameraDistance, MaxCameraDistance);
	CameraBoom->TargetArmLength = TargetCameraDistance;

	// NOTHING STARTS THE BASIC ATTACK ANY MORE, and that is the change rather
	// than an omission. Issue #1187: it used to run off a timer that re-armed
	// itself at the weapon's attack speed and swung whenever any creature was
	// inside its reach, which is what "The basic attack is on no key. It fires
	// automatically" asked for. The project owner reversed that on 2026-09-02
	// after playing it, so a swing now happens because the player clicked a
	// creature. `ACataclysmPlayerController` owns that.
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
	// THE NEW VALUE IS IGNORED AND THE STAT IS ASKED FOR INSTEAD. Issue #959.
	// `Data.NewValue` is the attribute, which by design carries no bonus that
	// depends on the character's state, so using it here would drop the
	// Masochist's Desperate Measures node every time gear changed.
	RefreshMovementSpeed();
}

void ACataclysmPlayerCharacter::RefreshMovementSpeed()
{
	const UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent();
	if (!AbilitySystem)
	{
		return;
	}

	// THE ATTRIBUTE IS THE FALLBACK AND NOT THE ANSWER. `StatForSkill` returns
	// it unchanged for a character whose movement speed has no modifier at all,
	// which is every character until one is granted, so nothing without a
	// conditional bonus behaves differently from before.
	//
	// AN EMPTY TAG CONTAINER, BECAUSE THERE IS NO SKILL IN HAND. Movement speed
	// is a property of the character rather than of anything it is doing, so the
	// only question being asked here is the character's own state.
	const float FromAttribute = AbilitySystem->GetNumericAttribute(
		UCataclysmCombatAttributeSet::GetMovementSpeedAttribute());

	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	ApplyMovementSpeed(Cataclysm
		? Cataclysm->StatForSkill(FName(TEXT("movement_speed")),
								  FGameplayTagContainer(), FromAttribute)
		: FromAttribute);
}

void ACataclysmPlayerCharacter::HealthChanged()
{
	Super::HealthChanged();

	// HEALTH CROSSING A THRESHOLD CHANGES A SPEED AND NOTHING ELSE NOTICES.
	// Issue #959. The movement component is told a speed once and keeps it, and
	// the attribute-change delegate that normally re-tells it does not fire,
	// because a conditional bonus never writes the attribute.
	//
	// EVERY HEALTH CHANGE RATHER THAN ONLY THE ONES THAT CROSS A THRESHOLD.
	// Working out whether a threshold was crossed would need this to remember
	// where the health was, and there can be several thresholds at once with
	// different values. Re-asking is one pipeline pass over one stat's modifier
	// list, which for a character with no conditional speed bonus is an empty
	// list and a map lookup that misses.
	RefreshMovementSpeed();
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

	// AND THE LOG SAYS THE PLAYER DIED, WHICH IT DID NOT UNTIL ISSUE #1101.
	//
	// WHY THIS LINE EXISTS. On 2026-08-31 the project owner reported dying after
	// two or three abilities and asked what had happened. The play session log
	// could not answer it: nothing recorded a player death at all, and every
	// message about a health cost, a debt and a death by debt is logged at
	// `Verbose`, which is dropped. Working out the cause took reading the save
	// file, the passive tree data and the class stat line.
	//
	// AT `Log` AND NOT `Verbose`, BECAUSE A PLAYER DYING IS NOT A DETAIL. It
	// happens rarely enough to cost nothing, and it is the single line somebody
	// reading a play session most needs.
	//
	// HEALTH OWED IS HERE AND IS THE POINT OF THE LINE. A Masochist holding The
	// Reckoning dies when its debt passes its current health, and with that
	// keystone no cost is ever taken from health -- so the health bar reads full
	// the whole way down and only this number moves. Issues #1098 and #1100.
	//
	// INSIDE THE ONCE-ONLY GUARD, unlike the save write above it, so a burn
	// ticking on a corpse does not print a second death.
	if (const UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent())
	{
		UE_LOG(LogCataclysm, Log,
			   TEXT("%s died at %.0f of %.0f health, owing %.0f health, with "
					"%.0f class resource."),
			   *GetName(),
			   AbilitySystem->GetNumericAttribute(
				   UCataclysmVitalAttributeSet::GetHealthAttribute()),
			   AbilitySystem->GetNumericAttribute(
				   UCataclysmVitalAttributeSet::GetMaxHealthAttribute()),
			   AbilitySystem->GetNumericAttribute(
				   UCataclysmClassResourceAttributeSet::GetHealthOwedAttribute()),
			   AbilitySystem->GetNumericAttribute(
				   UCataclysmClassResourceAttributeSet::GetClassResourceAttribute()));
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

	// AND THE BODY FALLS OVER. Issue #1124. Nothing played a death animation on
	// a player before this, because a cylinder has nothing to play one on.
	//
	// AFTER MOVEMENT IS OFF, AND THAT ORDER MATTERS. The animation Blueprint
	// picks its pose from the movement component's velocity, so a death clip
	// started while the character was still sliding would be fighting the
	// locomotion graph for the first frames. DisableMovement clears the
	// velocity, so by here there is nothing to fight.
	PlayDeathAnimation();

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

	// IN A DUNGEON THE PLAYER START IS THE WRONG PLACE, AND IT IS WRONG BY A
	// WHOLE FLOOR. Issue #1103. `HandleDeath` records the first `APlayerStart`
	// in the level, which is right for a hand-built level and wrong for a
	// generated one: a dungeon floor is generated at run time and the player
	// start is wherever the map happens to have put it.
	// `ACataclysmDungeonGameMode::StartPlay` says exactly that in its own
	// comment -- "the player start is wherever the map put it, and where the
	// player arrives is decided by the generator and is different every floor"
	// -- and moves the player to the entrance when it first arrives. Reviving
	// did not, so dying in `L_Dungeon` put the player off the floor.
	//
	// `PlaceAtEntrance` RATHER THAN A LOCATION OF OUR OWN, because standing a
	// pawn on a floor is not a `SetActorLocation`. A character is a capsule
	// whose origin is its middle, so it has to be raised by its own half height,
	// read off the pawn because the designed classes are not all the same size.
	// That arithmetic exists in one place already.
	//
	// FOUND BY WALKING THE LEVEL RATHER THAN BY `GetAuthGameMode`. Both find the
	// same object in the running game, and only this one can be tested: the
	// world `CataclysmTestWorld::MakeWorldThatHasBegunPlay` builds has no
	// authority game mode on purpose, and `UWorld::AuthorityGameMode` is
	// private, so no test can supply one. It is the same lookup `HandleDeath`
	// makes to find a player start, and a revival happens once per death.
	bool bPlacedByTheDungeon = false;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ACataclysmDungeonGameMode> It(World); It; ++It)
		{
			bPlacedByTheDungeon = It->PlaceAtEntrance(this);
			break;
		}
	}

	// AND EVERY OTHER LEVEL KEEPS WHAT IT HAD. `PlaceAtEntrance` also answers
	// false inside a dungeon whose floor has not been built, which is worth
	// falling back from rather than leaving the player wherever it fell.
	// Swept off, because the destination is a spawn point and a sweep from where
	// the body fell would stop against the first thing in the way.
	if (!bPlacedByTheDungeon)
	{
		SetActorLocationAndRotation(RespawnLocation, RespawnRotation,
									/*bSweep=*/false, nullptr,
									ETeleportType::TeleportPhysics);
	}

	// THE THREE VITALS COME BACK FULL, NOT PARTIAL. No document says what a player
	// comes back with, so the least surprising reading is the one every game in
	// the genre uses: a respawned character is whole and what it lost is measured
	// in the world rather than on the character. Written as the base value rather
	// than through a gameplay effect because there is no heal effect in the
	// project and inventing one to serve a placeholder would be the larger change.
	//
	// THE CLASS RESOURCE IS THE EXCEPTION AND IS EMPTIED. See below.
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

		// AND THE CLASS RESOURCE IS EMPTIED, WHICH IS THE OPPOSITE DIRECTION.
		// Issue #956, decided by the project owner on 2026-08-26.
		//
		// A RESPAWN IS THE LARGEST AMOUNT OF HEALTH A CHARACTER EVER GETS BACK AT
		// ONCE, and the Masochist's whole rule is that health coming back takes
		// Fervour with it. Leaving it alone let a player bank a full bar through a
		// death, which is a reason to die. The rule the class is built on has no
		// exception for respawning, so neither does this.
		//
		// ONE ANSWER FOR ALL FOUR CLASSES, not four. The other three generators do
		// not exist yet; this is the resource attribute every class shares, so
		// whatever they fill it with, a respawn empties it.
		//
		// WRITTEN DIRECTLY RATHER THAN THROUGH THE HEALING PATH, for the same
		// reason the three refills above are. The refills do not run through
		// `UCataclysmRegeneration::TopUp`, so they never removed any Fervour of
		// their own accord -- that is exactly why this line has to exist rather
		// than falling out of the refill.
		AbilitySystem->SetNumericAttributeBase(
			UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
			0.0f);
	}

	// MOVE_Walking rather than whatever it was, because what it was is MOVE_None:
	// HandleDeath set that and DisableMovement does not remember the previous
	// mode.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
	}

	// AND THE CHARACTER STANDS BACK UP. Issue #1124. `PlayDeathAnimation` put
	// the mesh into single-node mode so the corpse would hold the last frame of
	// its death clip, and nothing else would ever take it out again: without
	// this the revived character walks around frozen in the pose it died in.
	//
	// THE ANIMATION BLUEPRINT ONLY, NOT THE WHOLE BODY. The mesh and the six
	// death clips were loaded once at BeginPlay and are still loaded, so there
	// is nothing to reload -- only the animation mode to put back.
	ResolveAnimationBlueprint(GetMesh());

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

	// AND WHAT THE PLAYER CHOSE WHEN THE CHARACTER WAS CREATED, which is where a
	// character coming back from the capital gets its weapon and its damage type
	// back. It does nothing at all when nobody has chosen, so every character
	// that existed before the creator did keeps exactly what it had. Issue #50.
	//
	// AFTER THE STAT LINE, NOT BEFORE. Changing the worn weapon broadcasts
	// EquipmentChanged, which applies the whole stat line again through
	// UCataclysmEquipmentComponent::RefreshAttributes. Doing it first would mean
	// the line was applied twice, and the second application is the one with the
	// right weapon in hand.
	ApplyCreationChoice();
}

void ACataclysmPlayerCharacter::ApplyChosenClassStats()
{
	UCataclysmAbilitySystemComponent* ASC =
		Cast<UCataclysmAbilitySystemComponent>(GetAbilitySystemComponent());
	if (!ASC)
	{
		return;
	}

	// THROUGH THE EQUIPMENT COMPONENT, WHICH IS THE ONE PLACE THAT KNOWS EVERY
	// SOURCE A CHARACTER'S STATS COME FROM. Issue #1054.
	//
	// WHAT THIS USED TO DO AND WHY IT WAS WRONG. It gathered the class line, the
	// worn items, the weapons' swing rate and the spent attribute points itself,
	// and left the passive tree out -- because `RefreshAttributes` was where the
	// tree had been joined in and this was a second, older copy of the same
	// gathering. A character arriving in the world with points already spent got
	// nothing for them until it happened to change a worn item. The four sources
	// are now named in exactly one place.
	//
	// THE POOLS ARE FILLED, AND ONLY HERE. This runs from PossessedBy, which
	// happens once, and "this character is starting" is the one case where
	// health, mana and shield should go to their maximums.
	if (Equipment)
	{
		Equipment->RefreshAttributes(ASC, ECataclysmPoolFill::FillToMaximum);
		return;
	}

	// NO EQUIPMENT COMPONENT AT ALL: the class line and the spent attribute
	// points, and nothing that has to be read off an item. `Equipment` is a
	// default subobject and so is never null on a character the engine built,
	// but this function has always guarded it and a possessed character with no
	// stat line at all would be worse than one with a partial one.
	TMap<FName, float> Bases;

	// THE EIGHT ATTRIBUTES THE CHARACTER HAS SPENT POINTS ON. Issue #50.
	// They are a base rather than a modifier because no class line can state how
	// many points a particular character has spent.
	// THE LEVEL COMES FROM THE PLAYER STATE AND NOT FROM Cataclysm.PlayerLevel
	// ANY MORE. Reading the console variable here could not see a level the
	// character had gained, and a character that levels up has to resolve its
	// stat line at the level it now is. `GetCharacterLevel` still falls back to
	// that console variable until a level has been earned or loaded, so setting
	// it before pressing Play works exactly as it did. Issue #50.
	int32 Level = UCataclysmPlayerClassStats::ChosenLevel();
	if (const ACataclysmPlayerState* State = GetPlayerState<ACataclysmPlayerState>())
	{
		UCataclysmPlayerClassStats::MergeAttributeBases(
			State->GetSpentAttributePoints(), Bases);
		Level = State->GetCharacterLevel();
	}

	UCataclysmPlayerClassStats::ApplyTo(
		ASC, UCataclysmPlayerClassStats::LoadTable(),
		UCataclysmPlayerClassStats::ChosenClass(),
		Level,
		// NO MODIFIERS AT ALL, because every one of them would have come off a
		// worn item or the passive tree, and both are read through the equipment
		// component this branch does not have.
		nullptr,
		// A CHARACTER STARTING. This runs from PossessedBy, which happens once,
		// so filling the pools is right here and is exactly what it did before.
		// OnEquipmentChanged is the path that must not fill them.
		ECataclysmPoolFill::FillToMaximum,
		&Bases);
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

	// AND WHAT IS IN THE CHARACTER'S HANDS. Issue #1125. Before this a player
	// could equip a Greataxe and see nothing change at all.
	//
	// ON EVERY EQUIPMENT CHANGE RATHER THAN ONLY A WEAPON ONE, which costs a
	// table lookup for a change of boots and is what makes taking a weapon off
	// reliably clear the hand. This function already recomputes the whole stat
	// line for a change of boots for the same reason.
	RefreshWeaponMeshes();
}

void ACataclysmPlayerCharacter::FillAbilitySlotsFromWornWeapon(
	ECataclysmWeaponExpected Expected)
{
	if (!WeaponSlots)
	{
		return;
	}

	// THE ITEMS AND THE TYPE ARE TWO DIFFERENT QUESTIONS, and until issue #840
	// only the type was asked. **The type decides which six skills exist. The
	// items decide what a swing is worth**, and only the first of those is this
	// function's business.
	//
	// NOTHING ABOUT DAMAGE HAPPENS HERE. Since issue #845 the attack damage and
	// attack speed attributes are written in one place only,
	// UCataclysmPlayerClassStats::ApplyTo, from the modifiers every worn item
	// supplies. UCataclysmEquipmentComponent::RefreshAttributes runs it whenever
	// equipment changes, which is the first half of OnEquipmentChanged above.
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
	// NOT AT POSSESSION, WHICH IS BEFORE THE WEAPON IS PUT ON. Measured on
	// 2026-08-25 for issue #933: PossessedBy runs first, then
	// InitAbilityActorInfo, and only then BeginPlay, which is where
	// GiveStartingWeapon wears the axe. So this fired on every single start
	// of the game, telling the reader to check a StartingWeaponBase that was
	// perfectly correct and a table that had loaded.
	if (Equipment && Expected == ECataclysmWeaponExpected::Yes)
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

bool ACataclysmPlayerCharacter::ApplyCreationChoice()
{
	const ACataclysmPlayerState* State = GetPlayerState<ACataclysmPlayerState>();
	if (!State || !State->HasChosenAtCreation())
	{
		// NOTHING WAS CHOSEN, SO NOTHING CHANGES, AND THAT IS THE ORDINARY CASE
		// RATHER THAN A FAILURE. Every character an automation test stands up is
		// here, and so is every character that existed before the creator did.
		return false;
	}

	if (!Equipment || !Inventory || !WeaponSlots)
	{
		UE_LOG(LogCataclysm, Warning,
			   TEXT("The creation choice cannot be applied: the character is "
					"missing its equipment, inventory or weapon slots."));
		return false;
	}

	const FName ChosenWeaponType = State->GetChosenWeaponType();
	const FName ChosenDamageType = State->GetChosenDamageType();

	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	const FName ChosenBase =
		UCataclysmCharacterCreation::ItemBaseFor(BaseTable, ChosenWeaponType);
	if (ChosenBase.IsNone())
	{
		UE_LOG(LogCataclysm, Error,
			   TEXT("No row of game/Data/ItemBases.csv carries the weapon type "
					"%s, so the character cannot be given one."),
			   *ChosenWeaponType.ToString());
		return false;
	}

	// THE DAMAGE TYPE FIRST, BECAUSE IT COSTS NOTHING AND CANNOT FAIL. It is a
	// stand-in on the weapon slots component until a rolled item carries its own
	// types; the field's own comment says so. Setting it before the weapon means
	// a failure to change the weapon still leaves the two agreeing, rather than
	// a Greataxe granting Void skills.
	WeaponSlots->SetDamageType(ChosenDamageType.ToString());
	WeaponSlots->SetStartingWeaponType(ChosenWeaponType.ToString());

	// ALREADY HOLDING ONE OF THE CHOSEN TYPE IS DONE, NOT REFUSED. Confirming
	// the same choice twice, or confirming a choice that only changed the damage
	// type, should not put a second axe in the bag.
	if (Equipment->EquippedWeaponType() == ChosenWeaponType.ToString())
	{
		FillAbilitySlotsFromWornWeapon();
		return true;
	}

	// ASKED BEFORE ANYTHING MOVES, which is the rule `UCataclysmWearing` keeps
	// and the reason an item is never destroyed here either. A two-handed weapon
	// can take two one-handed ones off, so it needs two free slots; nothing else
	// takes more than one off.
	FCataclysmItem Chosen;
	Chosen.Base = ChosenBase;
	Chosen.GearLevel = 0;

	int32 WeaponsWorn = 0;
	for (const ECataclysmGearSlot WeaponSlot : UCataclysmGearSlots::WeaponSlots())
	{
		if (!Equipment->SlotIsEmpty(WeaponSlot))
		{
			++WeaponsWorn;
		}
	}

	if (Inventory->NumFreeSlots() < WeaponsWorn)
	{
		UE_LOG(LogCataclysm, Warning,
			   TEXT("The bag has %d free slots and %d worn weapons would come "
					"off, so the creation choice was not applied and nothing "
					"moved."),
			   Inventory->NumFreeSlots(), WeaponsWorn);
		return false;
	}

	FCataclysmItem CameOff;
	FCataclysmItem AlsoCameOff;
	ECataclysmGearSlot WentTo = ECataclysmGearSlot::Weapon1;
	const ECataclysmEquipResult Result =
		Equipment->Equip(Chosen, CameOff, AlsoCameOff, WentTo);

	if (Result != ECataclysmEquipResult::Equipped &&
		Result != ECataclysmEquipResult::Swapped)
	{
		UE_LOG(LogCataclysm, Error,
			   TEXT("A %s could not be worn, so the creation choice was not "
					"applied."),
			   *ChosenBase.ToString());
		return false;
	}

	// WHAT CAME OFF GOES INTO THE BAG. The room for it was counted above, so
	// neither of these can answer INDEX_NONE -- but the answer is read anyway,
	// because a caller of AddItem that ignores it has destroyed an item.
	for (const FCataclysmItem& Removed : {CameOff, AlsoCameOff})
	{
		if (Removed.Base.IsNone())
		{
			continue;
		}

		if (Inventory->AddItem(Removed) == INDEX_NONE)
		{
			UE_LOG(LogCataclysm, Error,
				   TEXT("%s came off and the bag had no room for it after all, "
						"so it has been lost."),
				   *Removed.Base.ToString());
		}
	}

	// THE SIX ABILITY SLOTS FOLLOW THE WEAPON. `Equip` broadcasts
	// EquipmentChanged, which reaches OnEquipmentChanged, which does this --
	// but only once the ability system exists, and this is reachable from
	// possession where the order is not something to rely on. Running it twice
	// refills the same six slots with the same six abilities.
	FillAbilitySlotsFromWornWeapon();
	return true;
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
	for (const ECataclysmGearSlot HeldWeaponSlot :
		 UCataclysmGearSlots::WeaponSlots())
	{
		if (!Equipment->SlotIsEmpty(HeldWeaponSlot))
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
	// be missed. The read answers zero rather than failing when the component
	// holds no combat attribute set, and ApplyMovementSpeed refuses zero, so a
	// pawn whose attributes have not arrived keeps the designed default from the
	// constructor.
	RefreshMovementSpeed();

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
	// NOTHING IS WORN YET AND THAT IS NOT A FAULT. See
	// ECataclysmWeaponExpected: possession happens before BeginPlay, which is
	// where the starting weapon is put on. The slots are still filled, from
	// the starting weapon TYPE, and OnEquipmentChanged fills them again from
	// the real item a moment later. Issue #933.
	FillAbilitySlotsFromWornWeapon(ECataclysmWeaponExpected::NotYet);
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

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmDrop(
	TEXT("Cataclysm.Drop"),
	TEXT("Take the item in a carried inventory slot, 0 to 47, out of the bag "
		 "and leave it on the floor in front of the character."),
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
				Ar.Log(TEXT("Cataclysm.Drop <carried slot 0 to 47>. "
							"Cataclysm.ShowEquipment lists what is carried."));
				return;
			}

			// THE SAME RULE THE SCREEN USES. A left press on neither panel while
			// holding an item runs UCataclysmWearing::DropCarried too, so this
			// command cannot drift away from the gesture it stands in for.
			// Issue #1190 asked for this because there was no way to drop an
			// item at all -- not by click, not by key, and not by console.
			Ar.Log(*UCataclysmWearing::Explain(UCataclysmWearing::DropCarried(
				Character->GetInventory(), FCString::Atoi(*Args[0]), World,
				UCataclysmDropSpawner::DropSpotInFrontOf(*Character))));
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

// ---------------------------------------------------------------------------
// Attribute points
//
// A STAND-IN, IN THE SAME SHAPE AS Cataclysm.PlayerClass AND PlayerLevel. There
// is no character sheet to spend points on and no levelling to earn them from;
// issue #50 is where both arrive. Until then a character has one point for every
// level and these are how they are spent, so the eight attributes and the eight
// gear affixes that increase them can be exercised at all.
//
// THEY RE-APPLY THE STAT LINE THEMSELVES rather than asking the player to press
// Play again, which is what changing PlayerClass or PlayerLevel needs. Spending
// a point is meant to be something a person does repeatedly while looking at the
// result.
// ---------------------------------------------------------------------------

namespace CataclysmAttributeConsole
{
	/** The player's own state, complaining in the log when there is none. */
	ACataclysmPlayerState* State(UWorld* World, FOutputDevice& Ar)
	{
		using namespace CataclysmEquipConsole;

		ACataclysmPlayerCharacter* Character = Player(World, Ar);
		ACataclysmPlayerState* Found =
			Character ? Character->GetPlayerState<ACataclysmPlayerState>() : nullptr;
		if (Character && !Found)
		{
			Ar.Log(TEXT("The player character has no Cataclysm player state, so "
						"it has nowhere to keep attribute points."));
		}
		return Found;
	}
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmSpendAttributePoint(
	TEXT("Cataclysm.SpendAttributePoint"),
	TEXT("Spend attribute points: Cataclysm.SpendAttributePoint <attribute> "
		 "[count]. A character has one point for every level. Refused whole "
		 "when it would spend more than are left."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmAttributeConsole;

			if (Args.Num() < 1)
			{
				Ar.Logf(TEXT("Name an attribute: %s. A count may follow it."),
						*FString::Join(FCataclysmAttributePoints::Names(),
									   TEXT(", ")));
				return;
			}

			ACataclysmPlayerState* PlayerState = State(World, Ar);
			if (!PlayerState)
			{
				return;
			}

			// ONE POINT WHEN NO COUNT IS GIVEN, which is what spending a point
			// means. FCString::Atoi answers zero for anything that is not a
			// number, and zero is refused below with a message that says so.
			const int32 Count = Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : 1;

			FString Reason;
			if (!PlayerState->SpendAttributePoints(Args[0], Count, Reason))
			{
				Ar.Logf(TEXT("Refused: %s"), *Reason);
				return;
			}

			// THE STAT LINE IS REBUILT, NOT NUDGED. An attribute scales sixteen
			// stats through game/Data/Attributes.csv and several of those have
			// gear increases of their own, so the only correct answer is to run
			// the whole pipeline again.
			using namespace CataclysmEquipConsole;
			if (ACataclysmPlayerCharacter* Character = Player(World, Ar))
			{
				if (Character->GetEquipment())
				{
					// RefreshAttributes AND NOT OnEquipmentChanged, although the
					// latter would work. Nothing about what is worn changed, and
					// a call named for equipment would say it did. It also
					// leaves the pools where they are, so spending a point into
					// Vitality raises maximum health without healing anybody.
					Character->GetEquipment()->RefreshAttributes(
						Character->GetAbilitySystemComponent());
				}
			}

			Ar.Logf(TEXT("Spent %d into %s. %d of %d points now unspent."),
					Count, *Args[0], PlayerState->AttributePointsUnspent(),
					PlayerState->AttributePointsAvailable());
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmResetAttributePoints(
	TEXT("Cataclysm.ResetAttributePoints"),
	TEXT("Take back every spent attribute point."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmAttributeConsole;

			ACataclysmPlayerState* PlayerState = State(World, Ar);
			if (!PlayerState)
			{
				return;
			}

			PlayerState->ResetAttributePoints();

			using namespace CataclysmEquipConsole;
			if (ACataclysmPlayerCharacter* Character = Player(World, Ar))
			{
				if (Character->GetEquipment())
				{
					// RefreshAttributes AND NOT OnEquipmentChanged, although the
					// latter would work. Nothing about what is worn changed, and
					// a call named for equipment would say it did. It also
					// leaves the pools where they are, so spending a point into
					// Vitality raises maximum health without healing anybody.
					Character->GetEquipment()->RefreshAttributes(
						Character->GetAbilitySystemComponent());
				}
			}

			Ar.Logf(TEXT("Returned every point. %d unspent."),
					PlayerState->AttributePointsUnspent());
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmShowAttributes(
	TEXT("Cataclysm.ShowAttributes"),
	TEXT("List the eight attributes: points spent, and what each is worth after "
		 "the gear affixes that increase it."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmAttributeConsole;
			using namespace CataclysmEquipConsole;

			ACataclysmPlayerState* PlayerState = State(World, Ar);
			ACataclysmPlayerCharacter* Character = Player(World, Ar);
			if (!PlayerState || !Character)
			{
				return;
			}

			const UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
			const FCataclysmAttributePoints& Spent =
				PlayerState->GetSpentAttributePoints();

			Ar.Logf(TEXT("%d of %d attribute points spent."),
					Spent.Total(), PlayerState->AttributePointsAvailable());
			Ar.Log(TEXT("  attribute       spent   after gear"));

			for (const FString& Name : FCataclysmAttributePoints::Names())
			{
				// SPENT AND RESOLVED SIDE BY SIDE, because the difference
				// between them IS what the eight attribute affixes do. Equal
				// numbers mean no gear is increasing that attribute.
				const FGameplayAttribute* Attribute =
					UCataclysmPlayerClassStats::StatToAttribute().Find(Name);
				const float Resolved =
					(ASC && Attribute && ASC->HasAttributeSetForAttribute(*Attribute))
						? ASC->GetNumericAttribute(*Attribute)
						: 0.0f;

				Ar.Logf(TEXT("  %-14s %5d   %10.2f"),
						*Name, Spent.PointsIn(Name), Resolved);
			}
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmShowFervour(
	TEXT("Cataclysm.ShowFervour"),
	TEXT("What is in the Fervour bar, and the three rates that move it: gained "
		 "per 1% of maximum health lost to damage, gained per 1% spent as an "
		 "ability cost, and removed per 1% restored by healing. All three are "
		 "zero until a passive tree's generator node is bought."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmEquipConsole;

			// WHY THE FIGURES AND NOT ONLY THE BAR. Issue #954. The bar answers
			// "is it filling" and these answer "by how much, and why that much".
			// A rate that is 1.16 rather than 1.00 is eight points in Open
			// Wounds doing their work, and no bar can show that.
			ACataclysmPlayerCharacter* Character = Player(World, Ar);
			const UAbilitySystemComponent* ASC =
				Character ? Character->GetAbilitySystemComponent() : nullptr;
			if (!ASC || !ASC->GetSet<UCataclysmClassResourceAttributeSet>())
			{
				Ar.Log(TEXT("No character with a Fervour pool."));
				return;
			}

			using Resource = UCataclysmClassResourceAttributeSet;

			Ar.Logf(TEXT("Fervour %.1f / %.1f"),
					ASC->GetNumericAttribute(Resource::GetClassResourceAttribute()),
					ASC->GetNumericAttribute(Resource::GetMaxClassResourceAttribute()));

			const TPair<const TCHAR*, FGameplayAttribute> Rates[] = {
				{TEXT("gained per 1% of maximum health lost to damage"),
				 Resource::GetFervourFromDamageAttribute()},
				{TEXT("gained per 1% spent as an ability cost"),
				 Resource::GetFervourFromCostAttribute()},
				{TEXT("removed per 1% restored by healing"),
				 Resource::GetFervourLostToHealingAttribute()},
			};

			for (const TPair<const TCHAR*, FGameplayAttribute>& Rate : Rates)
			{
				Ar.Logf(TEXT("  %6.2f  %s"),
						ASC->GetNumericAttribute(Rate.Value), Rate.Key);
			}

			if (!UCataclysmFervour::HasAGenerator(ASC))
			{
				// SAID PLAINLY RATHER THAN LEFT TO BE INFERRED FROM THREE ZEROS.
				// This is the ordinary state of every character in the game and
				// reads as a broken feature otherwise.
				Ar.Log(TEXT("  No generator. Fervour cannot move. Buy one with "
							"Cataclysm.SpendPassivePoint Masochist_basic_spine_000"));
			}
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmShowDebt(
	TEXT("Cataclysm.ShowDebt"),
	TEXT("How much health the character owes, whether it is being taken yet, "
		 "and whether the low health rescue that clears it is ready. Nothing in "
		 "the game shows any of this, and it is the number that decides whether "
		 "a Masochist lives."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmEquipConsole;
			using Resource = UCataclysmClassResourceAttributeSet;
			using Vital = UCataclysmVitalAttributeSet;
			using Debt = UCataclysmHealthDebt;

			// WHY THIS COMMAND EXISTS. Issue #1100. Every fault found on
			// 2026-08-31 was invisible for the same reason: a debt is a number
			// that kills the character and nothing anywhere displayed it. The
			// project owner died repeatedly without being able to say why.
			ACataclysmPlayerCharacter* Character = Player(World, Ar);
			UCataclysmAbilitySystemComponent* ASC =
				Character ? Cast<UCataclysmAbilitySystemComponent>(
								Character->GetAbilitySystemComponent())
						  : nullptr;
			if (!ASC || !ASC->GetSet<Resource>())
			{
				Ar.Log(TEXT("No character that can owe health."));
				return;
			}

			const float Owed = ASC->GetNumericAttribute(
				Resource::GetHealthOwedAttribute());
			const float Health =
				ASC->GetNumericAttribute(Vital::GetHealthAttribute());
			const float Maximum =
				ASC->GetNumericAttribute(Vital::GetMaxHealthAttribute());

			Ar.Logf(TEXT("Health debt %.1f. Health %.1f of %.1f."),
					Owed, Health, Maximum);

			if (Owed <= 0.0f)
			{
				// SAID PLAINLY, because zero owed and a character that cannot
				// owe anything at all look identical in a number.
				Ar.Log(TEXT("  Nothing owed."));
			}
			else if (Maximum > 0.0f)
			{
				// AS A SHARE OF MAXIMUM HEALTH, because that is what the two
				// damage bonuses read: The Reckoning gives 1% more damage per 2%
				// owed, and Compound Interest 1% increased per 5%.
				Ar.Logf(TEXT("  which is %.1f%% of maximum health, and that is "
							 "what the damage bonuses read"),
						Owed / Maximum * 100.0f);
			}

			const bool bOnlyAKill = Debt::IsClearedOnlyByAKill(ASC);
			if (bOnlyAKill)
			{
				Ar.Log(TEXT("  The Reckoning: it never falls due, and killing "
							"an enemy is what clears it."));

				if (Owed > Health)
				{
					// THE STATE THAT KILLS, AND THE ONE A PLAYER MOST NEEDS TO
					// SEE. Since issue #1120 this is a drain rather than an
					// instant death, so there are a few seconds in which asking
					// this question is worth something.
					const float Rate = Owed / Debt::DrainSeconds;
					Ar.Logf(TEXT("  BLEEDING OUT at %.1f a second. %.1f seconds "
								 "of health left unless something dies."),
							Rate, Rate > 0.0f ? Health / Rate : 0.0f);
				}
				else
				{
					Ar.Logf(TEXT("  Not bleeding. %.1f more owed would start "
								 "it."), Health - Owed + 1.0f);
				}
			}
			else if (Owed > 0.0f)
			{
				// AN ORDINARY DEFERRED DEBT, which falls due and then drains.
				const float Now = World ? World->GetTimeSeconds() : 0.0f;
				const float DueAt = ASC->HealthDebtDueAt();
				if (DueAt < 0.0f)
				{
					Ar.Log(TEXT("  Owed with no due time recorded."));
				}
				else if (ASC->IsHealthDebtDue())
				{
					Ar.Logf(TEXT("  Draining now, %.1f seconds into a %.1f "
								 "second drain."),
							Now - DueAt, Debt::DrainSeconds);
				}
				else
				{
					Ar.Logf(TEXT("  Falls due in %.1f seconds, then drains over "
								 "%.1f."),
							DueAt - Now, Debt::DrainSeconds);
				}
			}

			// AND WHETHER THE WAY OUT IS AVAILABLE. Rock Bottom is what turns a
			// bleeding character into a living one, and its cooldown is the
			// difference between the two. Issue #1119 recorded how little that
			// option was worth before the debt became a drain.
			const bool bHasRescue = ASC->GetNumericAttribute(
				Resource::GetDebtClearedOnDroppingLowAttribute()) > 0.0f;
			if (!bHasRescue)
			{
				Ar.Log(TEXT("  No low health rescue. Rock Bottom grants one."));
			}
			else if (ASC->MayTakeLowHealthRelief())
			{
				Ar.Logf(TEXT("  Rock Bottom is ready: falling below %.0f%% of "
							 "maximum health clears the whole debt."),
						UCataclysmLowHealthRelief::HealthShare * 100.0f);
			}
			else
			{
				const float Now = World ? World->GetTimeSeconds() : 0.0f;
				Ar.Logf(TEXT("  Rock Bottom used. Ready again in %.1f seconds."),
						ASC->LowHealthReliefAllowedAt() - Now);
			}
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmShowStacks(
	TEXT("Cataclysm.ShowStacks"),
	TEXT("How many stacks of each kind the character is holding right now, and "
		 "how many it may hold. All are zero until the node that reads them is "
		 "bought and the event that grants them happens."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmEquipConsole;

			// WHY THERE IS A COMMAND FOR THIS AT ALL. Issues #1002 to #1004. A
			// stack count is not a gameplay attribute, so nothing on the
			// character sheet shows it and `Cataclysm.ShowAttributes` cannot.
			// It expires on its own with nothing running, so a player who
			// cannot see it has no way to tell a node that is not working from
			// one whose stacks ran out a second ago.
			ACataclysmPlayerCharacter* Character = Player(World, Ar);
			UCataclysmAbilitySystemComponent* ASC =
				Character
					? Cast<UCataclysmAbilitySystemComponent>(
						  Character->GetAbilitySystemComponent())
					: nullptr;
			if (!ASC)
			{
				Ar.Log(TEXT("No character."));
				return;
			}

			for (int32 Index = 0; Index < UCataclysmStacks::KindCount; ++Index)
			{
				const ECataclysmStackKind Kind =
					static_cast<ECataclysmStackKind>(Index);
				Ar.Logf(TEXT("  %-18s %2d / %2d   lasts %.0fs"),
						UCataclysmStacks::NameOf(Kind),
						UCataclysmStacks::Held(ASC, Kind),
						UCataclysmStacks::CapFor(Kind),
						UCataclysmStacks::WindowSecondsFor(Kind));
			}
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmShowDebuffs(
	TEXT("Cataclysm.ShowDebuffs"),
	TEXT("Which harmful effects the character is under right now, how many of "
		 "them count as unique debuffs, and whether one of them is Bleeding. "
		 "Five Masochist nodes read one of those two numbers."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmEquipConsole;

			// WHY THERE IS A COMMAND FOR THIS AT ALL. Issue #962, and the same
			// argument `Cataclysm.ShowStacks` above makes. A debuff count is not
			// a gameplay attribute, so nothing on the character sheet shows it
			// and `Cataclysm.ShowAttributes` cannot. It falls back to nothing on
			// its own with nothing running, so a player who cannot see it has no
			// way to tell a node that is not working from one whose debuff ran
			// out a second ago.
			//
			// THE TAGS AS WELL AS THE COUNT, because the count alone cannot say
			// WHICH effects it counted. A character carrying a stun and a bleed
			// reads two and so does one carrying two ailments; only the list says
			// which, and that is the difference between Thirst for Pain applying
			// and not.
			ACataclysmPlayerCharacter* Character = Player(World, Ar);
			UAbilitySystemComponent* ASC =
				Character ? Character->GetAbilitySystemComponent() : nullptr;
			if (!ASC)
			{
				Ar.Log(TEXT("No character."));
				return;
			}

			Ar.Logf(TEXT("Unique debuffs %d.  Bleeding: %s"),
					UCataclysmDebuffs::CountOn(ASC),
					UCataclysmDebuffs::IsBleeding(ASC) ? TEXT("yes")
													   : TEXT("no"));

			const FGameplayTagContainer Roots = UCataclysmDebuffs::DebuffRoots();
			FGameplayTagContainer Owned;
			ASC->GetOwnedGameplayTags(Owned);

			int32 Listed = 0;
			for (const FGameplayTag& Tag : Owned)
			{
				if (Tag.MatchesAny(Roots))
				{
					Ar.Logf(TEXT("  %s"), *Tag.ToString());
					++Listed;
				}
			}

			if (Listed == 0)
			{
				// SAID PLAINLY RATHER THAN LEFT AS AN EMPTY LIST, the way the
				// Fervour command says why its three rates are zero. This is the
				// ordinary state of an unhurt character and reads as a broken
				// feature otherwise.
				Ar.Log(TEXT("  Nothing. Buy Masochist_basic_ll_b1 with "
							"Cataclysm.SpendPassivePoint, then take a hit below "
							"half health to bleed yourself."));
			}
		}));

// ---------------------------------------------------------------------------
// Level and experience
//
// A STAND-IN FOR KILLING THINGS, and it says so because the shape is temporary.
// The design says an enemy's Enemy Score IS the experience it grants, and Enemy
// Score has no port in this project -- issue #926 -- so nothing in the game can
// yet say what a kill is worth. Until it can, this is how the curve, the level
// and everything that reads a level are exercised at all.
//
// THEY RE-APPLY THE STAT LINE THEMSELVES, the same way spending an attribute
// point does. A level raises every per-level term in the class table, so a
// level gained that did not change the character would look like a level that
// did nothing.
// ---------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmGrantExperience(
	TEXT("Cataclysm.GrantExperience"),
	TEXT("Give the character experience: Cataclysm.GrantExperience <amount>. "
		 "Levels are gained as far as it pays for and the remainder is kept "
		 "toward the next one. A stand-in until killing things grants it."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmAttributeConsole;
			using namespace CataclysmEquipConsole;

			if (Args.Num() < 1)
			{
				Ar.Log(TEXT("Name an amount: Cataclysm.GrantExperience <amount>. "
							"Level 2 costs 230000."));
				return;
			}

			ACataclysmPlayerState* PlayerState = State(World, Ar);
			if (!PlayerState)
			{
				return;
			}

			// Atoi64 AND NOT Atoi, because the later levels cost hundreds of
			// millions and the whole climb is past six billion. A 32-bit read
			// of "6858596102" is not that number.
			const int64 Amount = FCString::Atoi64(*Args[0]);
			if (Amount <= 0)
			{
				Ar.Logf(TEXT("%s is not an amount of experience to grant."),
						*Args[0]);
				return;
			}

			const int32 Before = PlayerState->GetCharacterLevel();
			const int32 Gained = PlayerState->GrantExperience(Amount);

			if (Gained > 0)
			{
				// THE STAT LINE IS REBUILT, NOT NUDGED, for the reason the
				// attribute point commands give: a level moves every per-level
				// term in game/Data/ClassStats.csv and several of those have
				// gear increases of their own.
				if (ACataclysmPlayerCharacter* Character = Player(World, Ar))
				{
					if (Character->GetEquipment())
					{
						Character->GetEquipment()->RefreshAttributes(
							Character->GetAbilitySystemComponent());
					}
				}
			}

			const int32 Now = PlayerState->GetCharacterLevel();
			const int64 Next = UCataclysmExperience::CostOfLevel(Now + 1);
			if (Gained > 0)
			{
				Ar.Logf(TEXT("Level %d to %d. %lld of %lld toward level %d."),
						Before, Now, PlayerState->GetExperienceIntoLevel(),
						Next, Now + 1);
			}
			else if (UCataclysmExperience::IsMaxLevel(Now))
			{
				Ar.Logf(TEXT("Already at the maximum level, %d. Nothing to earn."),
						Now);
			}
			else
			{
				Ar.Logf(TEXT("Still level %d. %lld of %lld toward level %d."),
						Now, PlayerState->GetExperienceIntoLevel(), Next, Now + 1);
			}
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmShowLevel(
	TEXT("Cataclysm.ShowLevel"),
	TEXT("What level the character is, how far into it, and what the next level "
		 "and the whole climb cost."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmAttributeConsole;

			ACataclysmPlayerState* PlayerState = State(World, Ar);
			if (!PlayerState)
			{
				return;
			}

			const int32 Level = PlayerState->GetCharacterLevel();
			const int64 Into = PlayerState->GetExperienceIntoLevel();

			Ar.Logf(TEXT("Level %d of %d."), Level,
					UCataclysmExperience::MaxLevel);

			if (UCataclysmExperience::IsMaxLevel(Level))
			{
				Ar.Log(TEXT("At the maximum level. No further experience is kept."));
			}
			else
			{
				const int64 Next = UCataclysmExperience::CostOfLevel(Level + 1);
				Ar.Logf(TEXT("  %lld of %lld toward level %d, which is %.1f%% of it."),
						Into, Next, Level + 1,
						Next > 0 ? 100.0 * static_cast<double>(Into)
								   / static_cast<double>(Next)
								 : 0.0);
			}

			// EARNED AGAINST THE WHOLE CLIMB, because a level number on its own
			// says nothing about how far through the game a character is. Level
			// 50 is 1.9% of the climb to 100, and that is the design working
			// rather than a fault.
			const int64 Earned = UCataclysmExperience::TotalToReach(Level) + Into;
			const int64 Whole =
				UCataclysmExperience::TotalToReach(UCataclysmExperience::MaxLevel);
			Ar.Logf(TEXT("  %lld earned of %lld for the whole climb, which is %.2f%%."),
					Earned, Whole,
					100.0 * static_cast<double>(Earned) / static_cast<double>(Whole));

			Ar.Logf(TEXT("  %d attribute points, %d of them unspent."),
					PlayerState->AttributePointsAvailable(),
					PlayerState->AttributePointsUnspent());
		}));

// ---------------------------------------------------------------------------
// Character creation
//
// THE SCREEN IS THE WAY THIS IS MEANT TO BE DRIVEN. `Cataclysm.CharacterCreation`
// opens it, and the C key does the same thing without the console. These two
// commands exist beside it for the reason every other console command in this
// file exists: an automation test and a person checking one thing quickly both
// need a way in that does not involve a mouse, and the screen's own logic is the
// same code either way.
//
// THEY REPLACE `Cataclysm.PlayerClass` FOR NOTHING. That variable still chooses
// which class stat line the character sits on and is unaffected by this. Which
// class a character is has no answer yet; issue #932 is where it gets one.
// ---------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmChooseAtCreation(
	TEXT("Cataclysm.ChooseAtCreation"),
	TEXT("Choose a starting weapon type and damage type: "
		 "Cataclysm.ChooseAtCreation <weapon type> <damage type>. The character "
		 "puts on a weapon of that type and its six skills come from that "
		 "damage type. Run with no arguments to see what is on offer."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmAttributeConsole;
			using namespace CataclysmEquipConsole;

			const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
			const UDataTable* Skills = UCataclysmWeaponSkills::LoadGeneratedTable();

			// A WEAPON TYPE MAY BE TWO WORDS -- `2H Crossbow` is one of the
			// fourteen -- and the console splits on spaces. So the damage type
			// is the LAST argument and the weapon type is everything before it,
			// rather than the first two arguments being the answer.
			if (Args.Num() < 2)
			{
				Ar.Logf(TEXT("Weapon types: %s"),
						*FString::JoinBy(
							UCataclysmCharacterCreation::StartingWeaponTypes(Bases),
							TEXT(", "),
							[](const FName& Name) { return Name.ToString(); }));
				Ar.Logf(TEXT("Damage types: %s"),
						*FString::JoinBy(
							UCataclysmItemModifiers::DamageTypeNames(), TEXT(", "),
							[](const FName& Name) { return Name.ToString(); }));
				Ar.Log(TEXT("Not every damage type is on every weapon. "
							"Cataclysm.ShowCreation lists what the one in hand "
							"can carry."));
				return;
			}

			TArray<FString> WeaponWords = Args;
			const FString DamageWord = WeaponWords.Pop();
			const FName WeaponType(*FString::Join(WeaponWords, TEXT(" ")));
			const FName DamageType(*DamageWord);

			ACataclysmPlayerState* PlayerState = State(World, Ar);
			if (!PlayerState)
			{
				return;
			}

			FString Reason;
			if (!PlayerState->ChooseAtCreation(Skills, Bases, WeaponType,
											   DamageType, Reason))
			{
				Ar.Logf(TEXT("Refused: %s"), *Reason);
				return;
			}

			ACataclysmPlayerCharacter* Character = Player(World, Ar);
			if (Character && !Character->ApplyCreationChoice())
			{
				Ar.Log(TEXT("The choice was recorded but the character could "
							"not be changed to match it. The log says why."));
				return;
			}

			Ar.Logf(TEXT("%s, %s. %s"), *DamageType.ToString(),
					*WeaponType.ToString(),
					*UCataclysmCharacterCreation::UnlockedClassesFor(
						PlayerState->GetCreationChoice()));
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmShowCreation(
	TEXT("Cataclysm.ShowCreation"),
	TEXT("What was chosen when the character was created, which damage types "
		 "the chosen weapon can carry, and which class trees they unlock."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmAttributeConsole;

			ACataclysmPlayerState* PlayerState = State(World, Ar);
			if (!PlayerState)
			{
				return;
			}

			const UDataTable* Skills = UCataclysmWeaponSkills::LoadGeneratedTable();
			const FCataclysmCreationChoice Choice = PlayerState->GetCreationChoice();

			// SAID PLAINLY WHEN NOBODY CHOSE, because the pair below is then the
			// stand-in every character has had since long before the creator
			// existed, and reading it as a choice would be wrong.
			Ar.Logf(TEXT("%s"),
					PlayerState->HasChosenAtCreation()
						? TEXT("Chosen at creation:")
						: TEXT("Nothing was chosen. These are the defaults:"));

			Ar.Logf(TEXT("  weapon type   %s"), *Choice.WeaponType.ToString());
			Ar.Logf(TEXT("  damage type   %s"), *Choice.DamageType.ToString());
			Ar.Logf(TEXT("  %s"),
					*UCataclysmCharacterCreation::SummaryFor(Skills, Choice));
			Ar.Logf(TEXT("  %s"),
					*UCataclysmCharacterCreation::UnlockedClassesFor(Choice));

			Ar.Logf(TEXT("A %s can carry: %s"), *Choice.WeaponType.ToString(),
					*FString::JoinBy(
						UCataclysmCharacterCreation::DamageTypesFor(
							Skills, Choice.WeaponType),
						TEXT(", "),
						[](const FName& Name) { return Name.ToString(); }));
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmCharacterCreation(
	TEXT("Cataclysm.CharacterCreation"),
	TEXT("Open or close the character creation screen. The C key does the same "
		 "thing, once the input assets have been generated."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			APlayerController* Plain =
				World ? World->GetFirstPlayerController() : nullptr;
			ACataclysmPlayerController* Controller =
				Cast<ACataclysmPlayerController>(Plain);
			if (!Controller)
			{
				Ar.Log(TEXT("There is no Cataclysm player controller, so there "
							"is nothing to open a screen on."));
				return;
			}

			// THE SAME FUNCTION THE KEY CALLS, rather than a second copy of
			// opening a screen. Whatever the key does, this does.
			Controller->ToggleCharacterCreation();
		}));

// ---------------------------------------------------------------------------
// Passive points
//
// THE SCREEN IS HOW THIS IS MEANT TO BE DRIVEN. `Cataclysm.PassiveTree` opens
// it and the P key does the same. These exist beside it for the reason every
// other console command in this file exists: a node's row name is long, a person
// checking one thing quickly should not have to click twice, and an automation
// test has no mouse.
//
// A NODE IS NAMED BY ITS ROW IN `game/Data/PassiveNodes.csv`, which is the tree
// and the node identifier together -- `Masochist_basic_spine_005`. A node
// identifier alone is unique only within its tree; fourteen are shared by more
// than one of the four.
// ---------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmShowPassives(
	TEXT("Cataclysm.ShowPassives"),
	TEXT("How many passive points the character has earned and spent, which "
		 "trees its damage type reaches, and how much is in each."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmAttributeConsole;

			ACataclysmPlayerState* PlayerState = State(World, Ar);
			if (!PlayerState)
			{
				return;
			}

			const int32 Level = PlayerState->GetCharacterLevel();
			const int32 Bosses = PlayerState->GetDefeatedCataclysmBosses().Num();

			Ar.Logf(TEXT("Passive points: %d earned, %d spent, %d left. The "
						 "budget is %d."),
					PlayerState->PassivePointsAvailable(),
					PlayerState->GetPassiveAllocation().Total(),
					PlayerState->PassivePointsUnspent(),
					UCataclysmPassivePoints::Budget);

			// WHERE THEY CAME FROM, SEPARATELY. The three sources are the whole
			// award rule and a single total hides which of them is short.
			Ar.Logf(TEXT("  %d from level %d, %d from %d first boss kill%s."),
					UCataclysmPassivePoints::FromLevel(Level), Level,
					UCataclysmPassivePoints::FromBossKills(Bosses), Bosses,
					Bosses == 1 ? TEXT("") : TEXT("s"));

			const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
			if (!NodeTable)
			{
				Ar.Log(TEXT("The passive node table could not be loaded. Run "
							"python tools/run_editor_python.py "
							"tools/generate_datatable_assets.py"));
				return;
			}

			const TArray<FString> Reachable = PlayerState->ReachableTrees();
			Ar.Logf(TEXT("Reachable trees (%s damage): %s"),
					*PlayerState->GetChosenDamageType().ToString(),
					Reachable.Num() > 0
						? *FString::Join(Reachable, TEXT(", "))
						: TEXT("none"));

			for (const FString& Tree :
				 UCataclysmPassiveTree::TreeNames(NodeTable))
			{
				const int32 Spent = UCataclysmPassiveTree::SpentInTree(
					NodeTable, PlayerState->GetPassiveAllocation(), Tree);
				Ar.Logf(TEXT("  %-12s %3d spent%s"), *Tree, Spent,
						Reachable.Contains(Tree)
							? TEXT("")
							: TEXT("   (not reachable, so nothing in it applies)"));
			}
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmSpendPassivePoint(
	TEXT("Cataclysm.SpendPassivePoint"),
	TEXT("Put passive points into a node: Cataclysm.SpendPassivePoint <node> "
		 "[count]. The node is a row name in game/Data/PassiveNodes.csv, such as "
		 "Masochist_basic_spine_005. One point without a count. It fills as many "
		 "as it can and says where it stopped. Run with no arguments to list "
		 "what is open."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmAttributeConsole;

			ACataclysmPlayerState* PlayerState = State(World, Ar);
			if (!PlayerState)
			{
				return;
			}

			const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
			const UDataTable* EdgeTable = UCataclysmPassiveTree::LoadEdgeTable();
			if (!NodeTable || !EdgeTable)
			{
				Ar.Log(TEXT("The passive tables could not be loaded. Run "
							"python tools/run_editor_python.py "
							"tools/generate_datatable_assets.py"));
				return;
			}

			// NO ARGUMENT LISTS WHAT IS OPEN, which is what a person actually
			// needs: 293 nodes exist and at any moment a handful can take a
			// point. Listing all of them would be a wall to read.
			if (Args.Num() < 1)
			{
				int32 Listed = 0;
				for (const FString& Tree : PlayerState->ReachableTrees())
				{
					for (const FName& Node :
						 UCataclysmPassiveTree::NodesIn(NodeTable, Tree))
					{
						if (UCataclysmPassiveTree::RefusalForSpending(
								NodeTable, EdgeTable,
								PlayerState->GetPassiveAllocation(), Node,
								PlayerState->PassivePointsAvailable()).IsEmpty())
						{
							Ar.Logf(TEXT("  %s"), *Node.ToString());
							++Listed;
						}
					}
				}
				if (Listed == 0)
				{
					Ar.Log(TEXT("Nothing can take a point right now."));
				}

				// AND A CAPSTONE THAT HAS OPENED AND IS WAITING TO BE DECIDED.
				// Issue #1064. It is listed separately and not above, because
				// it cannot take a point until its option is chosen, so the
				// refusal above is never empty for it. The project owner spent
				// thirty points, crossed the first capstone's threshold of
				// twenty-five, and this command -- the one that says what is
				// open -- said nothing about it at all.
				//
				// THE COMMAND TO TYPE IS PRINTED WITH IT, because the node name
				// and the option number are both needed and neither is
				// guessable from a list of node names.
				int32 Waiting = 0;
				for (const FString& Tree : PlayerState->ReachableTrees())
				{
					for (const FName& Node :
						 UCataclysmPassiveTree::NodesIn(NodeTable, Tree))
					{
						if (!UCataclysmPassiveTree::AwaitsAnOptionChoice(
								NodeTable, PlayerState->GetPassiveAllocation(),
								Node))
						{
							continue;
						}

						if (Waiting == 0)
						{
							Ar.Log(TEXT("Waiting on a choice. A capstone takes "
										"no point until one of its three "
										"options is taken, and the choice is "
										"permanent:"));
						}
						++Waiting;

						const FCataclysmPassiveNodeRow* Row =
							UCataclysmPassiveTree::FindNode(NodeTable, Node);
						Ar.Logf(TEXT("  %s (%s)"),
								Row ? *Row->NodeName : *Node.ToString(),
								*Node.ToString());

						// THE THREE NAMES, because the choice is permanent and
						// a player cannot make it from a node name alone.
						if (Row)
						{
							const TArray<FString> Options =
								UCataclysmPassiveTree::OptionNamesOf(*Row);
							for (int32 Index = 0; Index < Options.Num(); ++Index)
							{
								if (!Options[Index].IsEmpty())
								{
									Ar.Logf(TEXT("      %d  %s"), Index + 1,
											*Options[Index]);
								}
							}
						}

						Ar.Logf(TEXT("      Cataclysm.ChoosePassiveOption %s "
									 "<1, 2 or 3>"), *Node.ToString());
					}
				}
				return;
			}

			// ONE POINT WHEN NO COUNT IS GIVEN, the same shape
			// `Cataclysm.SpendAttributePoint` above uses. FCString::Atoi answers
			// zero for anything that is not a number, and zero is refused below.
			//
			// WHY A COUNT AT ALL. A node deep in a tree opens only once its
			// whole chain is filled, and the chains are long: Thirst for Pain
			// sits behind ten nodes and needs 31 points altogether. One point
			// per command made that 31 commands typed by hand, which is enough
			// friction that the node went unchecked by play.
			const int32 Count = Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : 1;
			if (Count < 1)
			{
				Ar.Logf(TEXT("Refused: %d is not a number of points to spend."),
						Count);
				return;
			}

			const FName Node(*Args[0]);

			// STOPS AT THE FIRST REFUSAL AND KEEPS WHAT IT ALREADY SPENT. A
			// count larger than the node's remaining room, or than the points
			// the character holds, fills what it can and says where it stopped.
			// Undoing the earlier ones would be worse: the character would be
			// back where it started with no sign of how far it got.
			int32 Spent = 0;
			FString Reason;
			while (Spent < Count && PlayerState->SpendPassivePoint(Node, Reason))
			{
				++Spent;
			}

			if (Spent == 0)
			{
				Ar.Logf(TEXT("Refused: %s"), *Reason);
				return;
			}

			Ar.Logf(TEXT("%s. %d left."),
					*UCataclysmPassiveTree::DescribeNode(
						NodeTable, EdgeTable,
						PlayerState->GetPassiveAllocation(), Node,
						PlayerState->PassivePointsAvailable()),
					PlayerState->PassivePointsUnspent());

			if (Spent < Count)
			{
				// SAID PLAINLY RATHER THAN LEFT TO BE INFERRED FROM THE COUNT.
				// Asking for eight and getting six is the ordinary case at the
				// edge of a node's maximum, and it reads as a broken command
				// otherwise.
				Ar.Logf(TEXT("Put in %d of the %d asked for. Stopped because: %s"),
						Spent, Count, *Reason);
			}
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmChoosePassiveOption(
	TEXT("Cataclysm.ChoosePassiveOption"),
	TEXT("Take one of a capstone's three options: "
		 "Cataclysm.ChoosePassiveOption <node> <1, 2 or 3>. The choice is "
		 "permanent until the whole tree is respecced."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmAttributeConsole;

			if (Args.Num() < 2)
			{
				Ar.Log(TEXT("Name a capstone node and an option: 1, 2 or 3."));
				return;
			}

			ACataclysmPlayerState* PlayerState = State(World, Ar);
			if (!PlayerState)
			{
				return;
			}

			const FName Node(*Args[0]);
			FString Reason;
			if (!PlayerState->ChoosePassiveOption(Node, FCString::Atoi(*Args[1]),
												  Reason))
			{
				Ar.Logf(TEXT("Refused: %s"), *Reason);
				return;
			}

			const FCataclysmPassiveNodeRow* Row = UCataclysmPassiveTree::FindNode(
				UCataclysmPassiveTree::LoadNodeTable(), Node);
			const int32 Chosen = PlayerState->GetPassiveAllocation()
									 .ChosenOptionIn(Node);
			Ar.Logf(TEXT("%s: %s"), Row ? *Row->NodeName : *Node.ToString(),
					Row ? *UCataclysmPassiveTree::OptionNamesOf(*Row)[Chosen - 1]
						: TEXT(""));
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmResetPassivePoints(
	TEXT("Cataclysm.ResetPassivePoints"),
	TEXT("Return every passive point, and every capstone choice with them. "
		 "What the Trainer sells, at a cost in days nothing charges yet."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmAttributeConsole;

			ACataclysmPlayerState* PlayerState = State(World, Ar);
			if (!PlayerState)
			{
				return;
			}

			const int32 Returned = PlayerState->GetPassiveAllocation().Total();
			PlayerState->ResetPassivePoints();
			Ar.Logf(TEXT("%d passive point%s returned. %d to spend."), Returned,
					Returned == 1 ? TEXT("") : TEXT("s"),
					PlayerState->PassivePointsUnspent());
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmDefeatCataclysmBoss(
	TEXT("Cataclysm.DefeatCataclysmBoss"),
	TEXT("Record the first defeat of a unique Cataclysm boss, which is worth 10 "
		 "passive points: Cataclysm.DefeatCataclysmBoss <name>. A stand-in "
		 "until a Cataclysm boss exists in the game."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmAttributeConsole;

			if (Args.Num() < 1)
			{
				Ar.Log(TEXT("Name a boss. Any name will do; there are none in "
							"the game yet, and the design gives eight."));
				return;
			}

			ACataclysmPlayerState* PlayerState = State(World, Ar);
			if (!PlayerState)
			{
				return;
			}

			const FName Boss(*Args[0]);
			if (!PlayerState->RecordCataclysmBossDefeat(Boss))
			{
				Ar.Logf(TEXT("%s was already beaten, so it is worth nothing "
							 "further. Only a first defeat pays."),
						*Boss.ToString());
				return;
			}

			Ar.Logf(TEXT("%s beaten for the first time. %d passive points "
						 "earned, %d left to spend."),
					*Boss.ToString(), UCataclysmPassivePoints::PerFirstBossKill,
					PlayerState->PassivePointsUnspent());
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmPassiveTreeScreen(
	TEXT("Cataclysm.PassiveTree"),
	TEXT("Open or close the passive tree screen. The P key does the same "
		 "thing, once the input assets have been generated."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			ACataclysmPlayerController* Controller =
				Cast<ACataclysmPlayerController>(
					World ? World->GetFirstPlayerController() : nullptr);
			if (!Controller)
			{
				Ar.Log(TEXT("There is no Cataclysm player controller, so there "
							"is nothing to open a screen on."));
				return;
			}

			Controller->TogglePassiveTree();
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmPassiveTreeZoom(
	TEXT("Cataclysm.PassiveTreeZoom"),
	TEXT("Scale the passive tree view: Cataclysm.PassiveTreeZoom <notches>. "
		 "Positive scales in, negative out, and no argument fits the whole tree "
		 "in the panel. The mouse wheel does the same, and dragging with the "
		 "right button moves the view."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			ACataclysmPlayerController* Controller =
				Cast<ACataclysmPlayerController>(
					World ? World->GetFirstPlayerController() : nullptr);
			if (!Controller)
			{
				Ar.Log(TEXT("There is no Cataclysm player controller."));
				return;
			}

			// NOTHING MEANS FIT, which is the thing a person wants most often
			// after panning into a limb and losing the rest of the tree.
			const float Notches = Args.Num() >= 1
				? FCString::Atof(*Args[0]) : 0.0f;
			Controller->ZoomPassiveTree(Notches);
		}));

// ---------------------------------------------------------------------------
// The empire, issue #1087
//
// WHY THESE EXIST AT ALL. Nothing in the game advances the day. Walking a
// dungeon and spending days at the forge are what eventually will, and neither
// reaches the empire layer yet, so without a command to move time the empire
// overview shows an empire on day 0 for ever.
// ---------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmEmpireMapScreen(
	TEXT("Cataclysm.EmpireMap"),
	TEXT("Open or close the empire overview. It has no key of its own yet."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			ACataclysmPlayerController* Controller =
				Cast<ACataclysmPlayerController>(
					World ? World->GetFirstPlayerController() : nullptr);
			if (!Controller)
			{
				Ar.Log(TEXT("There is no Cataclysm player controller, so there "
							"is nothing to open a screen on."));
				return;
			}

			// STARTED HERE RATHER THAN BY THE SCREEN, so that opening a screen
			// to look at it never silently begins a campaign. Asking for the
			// empire map IS asking for an empire.
			UCataclysmGameInstance::EmpireRunFor(World, /*bStartIfNone*/ true);

			Controller->ToggleEmpireMap();
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmEmpireBegin(
	TEXT("Cataclysm.EmpireBegin"),
	TEXT("Start a fresh empire run: Cataclysm.EmpireBegin [seed] [static|"
		 "accelerating|swelling|both] [0 Standard, 1 Hardcore, 2 Heretic]. "
		 "THROWS AWAY THE RUN IN PROGRESS. The same seed gives the same empire."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			UCataclysmGameInstance* Instance = World
				? Cast<UCataclysmGameInstance>(World->GetGameInstance())
				: nullptr;
			if (!Instance)
			{
				Ar.Log(TEXT("This world has no Cataclysm game instance, so "
							"there is nowhere for a run to live. Check "
							"GameInstanceClass in Config/DefaultEngine.ini."));
				return;
			}

			const int32 Seed = Args.Num() >= 1 ? FCString::Atoi(*Args[0]) : 1;

			ECataclysmSurgeMode Mode = ECataclysmSurgeMode::Static;
			if (Args.Num() >= 2)
			{
				const FString Named = Args[1].ToLower();
				if (Named == TEXT("accelerating"))
				{
					Mode = ECataclysmSurgeMode::Accelerating;
				}
				else if (Named == TEXT("swelling"))
				{
					Mode = ECataclysmSurgeMode::Swelling;
				}
				else if (Named == TEXT("both"))
				{
					Mode = ECataclysmSurgeMode::Both;
				}
				else if (Named != TEXT("static"))
				{
					Ar.Logf(TEXT("'%s' is not an escalation. Use static, "
								 "accelerating, swelling or both."), *Args[1]);
					return;
				}
			}

			const int32 Rung = Args.Num() >= 3 ? FCString::Atoi(*Args[2]) : 0;

			UCataclysmEmpireRun* Run = Instance->BeginEmpireRun(Seed, Mode, Rung);

			Ar.Logf(TEXT("A fresh empire, seed %d. %s"), Seed, *Run->Describe());
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmEmpireAdvance(
	TEXT("Cataclysm.EmpireAdvance"),
	TEXT("Pass days in the empire: Cataclysm.EmpireAdvance <days>. One day with "
		 "no argument. Surges land, timers run out, cities fall."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			UCataclysmEmpireRun* Run =
				UCataclysmGameInstance::EmpireRunFor(World, /*bStartIfNone*/ true);
			if (!Run)
			{
				Ar.Log(TEXT("This world has no Cataclysm game instance, so "
							"there is no empire to advance."));
				return;
			}

			const int32 Days = Args.Num() >= 1
				? FMath::Max(1, FCString::Atoi(*Args[0])) : 1;

			// EVERY DAY REPORTED, NOT A SUMMARY. What a person running this
			// wants to know is which day a city was lost on, and a total loses
			// exactly that.
			for (const FCataclysmDayReport& Report : Run->AdvanceDays(Days))
			{
				if (Report.Spawned.IsEmpty() && Report.Resolved.IsEmpty()
					&& Report.Fallen.IsEmpty())
				{
					continue;
				}

				TArray<FString> Parts;
				if (Report.bSurged)
				{
					Parts.Add(FString::Printf(TEXT("a surge brought %d dungeons"),
											  Report.Spawned.Num()));
				}
				if (!Report.Resolved.IsEmpty())
				{
					Parts.Add(FString::Printf(TEXT("%d dungeons resolved"),
											  Report.Resolved.Num()));
				}
				for (const int32 CityId : Report.Fallen)
				{
					const FCataclysmCity* City = Run->Map
						? Run->Map->Find(CityId) : nullptr;
					Parts.Add(FString::Printf(TEXT("%s FELL"),
											  City ? *City->Name : TEXT("a city")));
				}

				Ar.Logf(TEXT("Day %d: %s"), Report.Day,
						*FString::Join(Parts, TEXT(", ")));
			}

			Ar.Log(*Run->Describe());
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmShowEmpire(
	TEXT("Cataclysm.ShowEmpire"),
	TEXT("Print the empire as it stands: the day, the map, how far the "
		 "Cataclysm is from the Pillar, and what is standing where."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			UCataclysmEmpireRun* Run =
				UCataclysmGameInstance::EmpireRunFor(World, /*bStartIfNone*/ false);
			if (!Run)
			{
				Ar.Log(TEXT("No run has been started. Cataclysm.EmpireBegin "
							"starts one, and Cataclysm.EmpireMap opens the "
							"screen and starts one if there is none."));
				return;
			}

			Ar.Log(*Run->Describe());
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmEnterDungeon(
	TEXT("Cataclysm.EnterDungeon"),
	TEXT("Walk a dungeon that stands on the empire map: Cataclysm.EnterDungeon "
		 "<id>. With no argument, the shallowest one standing. Its timer stops, "
		 "every other one keeps counting, and each floor you go down costs the "
		 "empire a day. Only works in a dungeon level."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			ACataclysmDungeonGameMode* Mode = World
				? World->GetAuthGameMode<ACataclysmDungeonGameMode>() : nullptr;
			if (!Mode)
			{
				Ar.Log(TEXT("This level is not a dungeon, so there is no floor "
							"to walk. Open L_Dungeon and press Play."));
				return;
			}

			UCataclysmEmpireRun* Run =
				UCataclysmGameInstance::EmpireRunFor(World, /*bStartIfNone*/ true);
			if (!Run)
			{
				Ar.Log(TEXT("This world has no Cataclysm game instance, so there "
							"is no empire to walk a dungeon of."));
				return;
			}

			// THE FIRST SURGE HAS TO HAVE LANDED. A run begins on day 0 with an
			// intact empire and nothing standing on it, so a player who asked to
			// enter a dungeon before any day had passed would be told there were
			// none and would reasonably think the command was broken.
			if (Run->DungeonCount() == 0)
			{
				Run->AdvanceDay();
			}

			int32 DungeonId = INDEX_NONE;

			if (Args.Num() >= 1)
			{
				DungeonId = FCString::Atoi(*Args[0]);
			}
			else
			{
				// THE SHALLOWEST ONE STANDING, because it is the one a player
				// with nothing else to go on would pick: fewest floors is fewest
				// days, and the choice is what the strategy layer is about.
				int32 Fewest = MAX_int32;
				for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
				{
					if (Dungeon.Floors < Fewest)
					{
						Fewest = Dungeon.Floors;
						DungeonId = Dungeon.DungeonId;
					}
				}
			}

			const FCataclysmDungeon* Chosen = Run->FindDungeon(DungeonId);
			if (!Chosen)
			{
				Ar.Logf(TEXT("No dungeon %d is standing on the map. %d are: run "
							 "Cataclysm.ShowEmpire to see them."),
						DungeonId, Run->DungeonCount());
				return;
			}

			const FCataclysmCity* City = Run->Map
				? Run->Map->Find(Chosen->CityId) : nullptr;

			if (!Mode->EnterEmpireDungeon(DungeonId))
			{
				Ar.Logf(TEXT("Dungeon %d could not be entered."), DungeonId);
				return;
			}

			Ar.Logf(TEXT("Walking dungeon %d on %s: %d floors, so %d days if you "
						 "clear it. Day %d."),
					DungeonId,
					City ? *City->Name : TEXT("nowhere"),
					Chosen->Floors, Chosen->Floors, Run->Day());
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmLeaveDungeon(
	TEXT("Cataclysm.LeaveDungeon"),
	TEXT("Stop walking the dungeon without clearing it. Its timer starts again "
		 "and it stays on the map."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			ACataclysmDungeonGameMode* Mode = World
				? World->GetAuthGameMode<ACataclysmDungeonGameMode>() : nullptr;
			if (!Mode)
			{
				Ar.Log(TEXT("This level is not a dungeon."));
				return;
			}

			if (Mode->EmpireDungeonId == INDEX_NONE)
			{
				Ar.Log(TEXT("No dungeon of the empire is being walked."));
				return;
			}

			const int32 Was = Mode->EmpireDungeonId;
			Mode->LeaveEmpireDungeon();

			Ar.Logf(TEXT("Left dungeon %d. It is still standing and its timer is "
						 "counting again."), Was);
		}));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmShowResistances(
	TEXT("Cataclysm.ShowResistances"),
	TEXT("Each of the eight resistances, the difficulty tier's penalty, and "
		 "what is left after it. Nothing else in the game shows these."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			using namespace CataclysmEquipConsole;

			// WHY THIS EXISTS. Issue #1229 gives a player's resistance a penalty
			// that grows with the difficulty tier, so the number a player is
			// chasing moves. Before this command nothing in the game printed a
			// resistance at all -- no screen shows one and Cataclysm.ShowAttributes
			// lists the eight attributes rather than these -- so the penalty
			// would have been a difficulty lever nobody could read.
			ACataclysmPlayerCharacter* Character = Player(World, Ar);
			const UAbilitySystemComponent* ASC =
				Character ? Character->GetAbilitySystemComponent() : nullptr;
			if (!ASC)
			{
				Ar.Log(TEXT("No player character."));
				return;
			}

			const int32 Tier = ACataclysmGameMode::DifficultyTierIn(Character);
			const float Penalty =
				UCataclysmDamageCalculation::ResistancePenaltyFor(ASC);

			Ar.Logf(TEXT("Difficulty tier %d takes %.0f resistance off every "
						 "type. The cap is %.0f, so %.0f is what it takes to "
						 "sit at it."),
					Tier, Penalty,
					UCataclysmDamageCalculation::ResistanceCap,
					UCataclysmDamageCalculation::ResistanceCap + Penalty);

			// THE GENERIC FIGURE APPLIES TO EVERY TYPE BY DEFINITION, so it is
			// shown once rather than folded silently into all eight.
			float Generic = 0.0f;
			if (const UCataclysmAllResistanceAttributeSet* All =
					ASC->GetSet<UCataclysmAllResistanceAttributeSet>())
			{
				Generic = All->GetAllResistance();
			}
			Ar.Logf(TEXT("All resistances %.1f, which every type below "
						 "already includes."), Generic);

			for (const FName& Type : UCataclysmItemModifiers::DamageTypeNames())
			{
				const FGameplayAttribute Slot =
					UCataclysmDamageCalculation::ResistanceAttributeFor(Type);
				const float Typed =
					(Slot.IsValid() && ASC->HasAttributeSetForAttribute(Slot))
						? ASC->GetNumericAttribute(Slot)
						: 0.0f;

				// WHAT A HIT WOULD ACTUALLY MEET, worked out the way
				// UCataclysmDamageCalculation::Resolve works it out: the total
				// less the penalty, then bounded. Printing the raw total alone
				// would disagree with what the player takes.
				const float Held = Generic + Typed;
				const float Effective = UCataclysmDamageCalculation::EffectiveResistance(
					Held - Penalty, /*Penetration=*/0.0f);

				Ar.Logf(TEXT("  %-12s held %6.1f, after the penalty %6.1f, "
							 "and a hit meets %6.1f"),
						*Type.ToString(), Held, Held - Penalty, Effective);
			}
		}));
