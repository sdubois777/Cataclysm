// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmPlantedWeapon.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "Cataclysm.h"
// For emptying and refilling the character's hands while the sword is standing
// in the ground. Issue #1141.
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

ACataclysmPlantedWeapon::ACataclysmPlantedWeapon()
{
	// Nothing to do per frame. It counts a second at a time on a timer, and a
	// tick would run that sixty times more often for the same result. The same
	// reason ACataclysmGroundZone and ACataclysmTether give.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// See the header. Without a root component the actor has no position and the
	// eruption would go off at the world origin rather than at the sword.
	Anchor = CreateDefaultSubobject<USceneComponent>(TEXT("Anchor"));
	SetRootComponent(Anchor);
}

ACataclysmPlantedWeapon* ACataclysmPlantedWeapon::Plant(
	AActor* InCaster, const FVector& Where, const FString& InWeaponType,
	ACataclysmGroundZone* InFire, float InMorePerSecond)
{
	if (!IsValid(InCaster))
	{
		return nullptr;
	}

	UWorld* World = InCaster->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// ONE SWORD AT A TIME. A character who somehow reaches here holding one
	// already would otherwise leave the first standing with nothing able to
	// reach it, because `HeldBy` answers with whichever the world iterator found
	// first. Nothing in the game can do this today -- the skill that plants is
	// still running while the sword stands, so a second press goes to
	// `InputPressed` rather than to a second activation -- and it is refused here
	// rather than assumed impossible.
	if (ACataclysmPlantedWeapon* Already = HeldBy(InCaster))
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("%s already has a %s in the ground, so a second was not "
				 "planted."),
			*GetNameSafe(InCaster), *Already->WeaponType);
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = InCaster;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// SPAWNED DEFERRED, FOR THE REASON `ACataclysmTether::Bind` GIVES AND ISSUE
	// #1153 RECORDS. `SpawnActor` runs `BeginPlay` before it returns, so anything
	// written onto the actor on the line after it is written too late for
	// `BeginPlay` to see. This actor's `BeginPlay` starts the counting timer and
	// empties the caster's hands, and both need the caster and the weapon type to
	// be set already.
	ACataclysmPlantedWeapon* Sword =
		World->SpawnActorDeferred<ACataclysmPlantedWeapon>(
			ACataclysmPlantedWeapon::StaticClass(),
			FTransform(FRotator::ZeroRotator, Where),
			InCaster, /*Instigator=*/nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Sword)
	{
		return nullptr;
	}

	Sword->Caster = InCaster;
	Sword->WeaponType = InWeaponType;
	Sword->Fire = InFire;
	Sword->MorePerSecond = InMorePerSecond;
	Sword->FireStartedAt = IsValid(InFire) ? InFire->DamagePerTick : 0.0f;

	Sword->FinishSpawning(FTransform(FRotator::ZeroRotator, Where));

	UE_LOG(LogCataclysm, Verbose,
		TEXT("%s drove a %s into the ground at %s; it fights unarmed until it "
			 "is pulled free."),
		*GetNameSafe(InCaster), *InWeaponType, *Where.ToCompactString());

	return Sword;
}

ACataclysmPlantedWeapon* ACataclysmPlantedWeapon::HeldBy(const AActor* Who)
{
	if (!IsValid(Who))
	{
		return nullptr;
	}

	UWorld* World = Who->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ACataclysmPlantedWeapon> It(World); It; ++It)
	{
		ACataclysmPlantedWeapon* Sword = *It;
		if (IsValid(Sword) && Sword->Caster.Get() == Who)
		{
			return Sword;
		}
	}

	return nullptr;
}

void ACataclysmPlantedWeapon::GrowHotter()
{
	++SecondsStood;

	ACataclysmGroundZone* Patch = Fire.Get();
	if (!IsValid(Patch) || MorePerSecond <= 0.0f || FireStartedAt <= 0.0f)
	{
		// A plant that left no burning ground still counts its seconds, because
		// the eruption reads the same count. Nothing in the design does this --
		// Buried Fire is the only row that plants and it states GroundRadius,
		// GroundDuration and GroundPercent -- so this is the case where the
		// imported table is older than the sheet.
		return;
	}

	// FROM THE FIGURE THE PATCH STARTED AT. See the header: raising the figure
	// it has now would compound, and the design's `more` bucket is a rate
	// multiplied by a count.
	Patch->DamagePerTick =
		FireStartedAt * (1.0f + MorePerSecond * SecondsStood / 100.0f);
}

void ACataclysmPlantedWeapon::BeginPlay()
{
	Super::BeginPlay();

	// EMPTY HANDS ARE THE VISIBLE HALF OF "YOU FIGHT UNARMED UNTIL YOU DO". The
	// refused skills are the half that matters and neither can be seen without
	// the other: a character swinging a greatsword it has left in the ground
	// reads as the skill not having worked.
	RedrawTheHandsOf(Caster.Get());

	if (UWorld* World = GetWorld())
	{
		// FIRST COUNT A FULL SECOND IN, because no time has passed at the moment
		// the sword goes in. Counting at once would make a sword pulled free
		// immediately erupt as though it had stood for a second.
		World->GetTimerManager().SetTimer(
			CountTimer, this, &ACataclysmPlantedWeapon::GrowHotter,
			SecondsPerCount, /*bLoop=*/true, /*InFirstDelay=*/SecondsPerCount);
	}
}

void ACataclysmPlantedWeapon::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CountTimer);
	}

	// THE HANDS ARE FILLED AGAIN WHATEVER ENDED THIS. See the header: a sword
	// destroyed by something the skill never hears about still has to leave the
	// character holding something.
	//
	// THE CASTER IS LET GO OF FIRST, AND THAT ORDER IS REQUIRED RATHER THAN TIDY.
	// Filling the hands goes through `ACataclysmPlayerCharacter::
	// RefreshWeaponMeshes`, which decides what to draw by asking `HeldBy` -- so if
	// this actor were still answering to its caster at that moment it would draw
	// empty hands and nothing would ever fill them again. Clearing the pointer
	// makes the answer no whatever the engine's own view of a dying actor is,
	// which is the half that should not be reasoned about: whether an actor
	// inside `EndPlay` is already skipped by `TActorIterator` is an engine detail
	// and this does not depend on it.
	// NOT CALLED `Owner`. `AActor` has a member of that name and the build
	// refuses a local that hides it, which is the same refusal
	// `ACataclysmPlayerCharacter::RefreshWeaponMeshes` records for `Mesh`.
	AActor* WhoseItWas = Caster.Get();
	Caster.Reset();
	RedrawTheHandsOf(WhoseItWas);

	Super::EndPlay(Reason);
}

void ACataclysmPlantedWeapon::RedrawTheHandsOf(AActor* Who) const
{
	// A CAST RATHER THAN AN INTERFACE, because one class draws held weapons.
	// `ACataclysmPlayerCharacter::RefreshWeaponMeshes` is the only thing in the
	// project that puts a mesh in a hand, and an enemy has no hands to fill --
	// so anything else answering null here is right rather than a gap.
	if (ACataclysmPlayerCharacter* Player = Cast<ACataclysmPlayerCharacter>(Who))
	{
		Player->RefreshWeaponMeshes();
	}
}
