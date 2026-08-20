// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmGameMode.h"
#include "Character/CataclysmEnemyRarity.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Cataclysm.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmHellhoundCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Interface/CataclysmHUD.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Save/CataclysmSaveWriter.h"

/**
 * Play at a difficulty tier without editing the game mode's default.
 *
 * ZERO MEANS "USE THE GAME MODE'S", the same shape as the Brute's two cooldown
 * overrides in `CataclysmBruteCharacter.cpp`. A sentinel rather than a second
 * copy of the default, so the real figure has one home.
 *
 * WHAT IT IS FOR. The tier decides what armour is worth and nothing else, and
 * the difference is large: the Abyssal Warden's 5,954 armour removes 75% of a
 * hit at tier 1 against 48.19% at tier 8. Switching while standing in the
 * sandbox is how that gets judged by play rather than by arithmetic.
 */
static TAutoConsoleVariable<int32> CVarDifficultyTier(
	TEXT("Cataclysm.DifficultyTier"),
	0,
	TEXT("The difficulty tier every hit resolves at, 1 to 8. 0 uses the game "
		 "mode's own DifficultyTier. It decides what armour is worth and "
		 "nothing else: armour removes armor / (armor + 800 x tier) of a hit, "
		 "capped at 75 percent, so the same armour is worth far more at tier 1."),
	ECVF_Default);

int32 ACataclysmGameMode::DifficultyTierFor(const ACataclysmGameMode* Mode)
{
	// THE CONSOLE VARIABLE WINS, so a tier chosen while playing takes effect on
	// the next hit rather than on the next launch.
	const int32 Override = CVarDifficultyTier.GetValueOnAnyThread();
	if (Override > 0)
	{
		return FMath::Clamp(Override, LowestDifficultyTier,
							HighestDifficultyTier);
	}

	if (Mode)
	{
		return FMath::Clamp(Mode->DifficultyTier, LowestDifficultyTier,
							HighestDifficultyTier);
	}

	// No game mode. Every automation test builds a bare world, and a hit
	// resolved there still has to produce a number.
	return LowestDifficultyTier;
}

int32 ACataclysmGameMode::DifficultyTierIn(const UObject* WorldContext)
{
	const ACataclysmGameMode* Mode = nullptr;
	if (WorldContext && GEngine)
	{
		if (const UWorld* World = GEngine->GetWorldFromContextObject(
				WorldContext, EGetWorldErrorMode::ReturnNull))
		{
			Mode = World->GetAuthGameMode<ACataclysmGameMode>();
		}
	}

	return DifficultyTierFor(Mode);
}

ACataclysmGameMode::ACataclysmGameMode()
{
	DefaultPawnClass = ACataclysmPlayerCharacter::StaticClass();
	PlayerControllerClass = ACataclysmPlayerController::StaticClass();

	// The player state is where the ability system component lives, so naming it
	// here is not optional dressing: with the default APlayerState the pawn finds
	// no ability system, InitAbilityActorInfo returns early, and the character
	// spawns with no attributes and no abilities and no error.
	PlayerStateClass = ACataclysmPlayerState::StaticClass();

	// NAMED HERE RATHER THAN IN A CONFIG FILE, so it holds for any level opened
	// directly and for a test world alike, the same as the three above. Without
	// this line the engine spawns a bare AHUD, which draws nothing, and there is
	// no error to follow -- a heads-up display that is simply absent looks
	// exactly like one that is switched off.
	//
	// It draws three things: a bar over creatures that have been hurt, a number
	// where each blow lands, and the player's own health. Issue #518.
	HUDClass = ACataclysmHUD::StaticClass();
}

void ACataclysmGameMode::StartPlay()
{
	Super::StartPlay();

	// After the player start exists and before anything can be pressed. Spawning
	// from here rather than from the level means the sandbox's contents are
	// reviewable text rather than bytes inside L_Sandbox.umap.
	SpawnTrainingDummies();
	SpawnBrutes();
	SpawnAbyssalWardens();
	SpawnHellhounds();
	SpawnImps();

	// AND THE GAME STARTS SAVING ITSELF. Its own method rather than four lines
	// here, so that a test can reach it: StartPlay wants a player controller and
	// a pawn and cannot be called from one.
	BeginSavingThisRun();
}

bool ACataclysmGameMode::BeginSavingThisRun()
{
	UCataclysmSaveWriter* Writer = UCataclysmSaveWriter::In(GetWorld());
	if (!Writer)
	{
		// NOT A FAILURE WORTH LOGGING. A world with no save writer is a world
		// built without subsystems, which is a thing a test does deliberately.
		return false;
	}

	// TWO IDENTIFIERS GENERATED HERE AND NOWHERE ELSE. When something chooses
	// between a new run and a saved one -- issue #753 -- this is the line it
	// replaces: the identifiers come out of the character being continued
	// instead of out of thin air.
	//
	// THE FLOOR IS NAMED FOR THE SANDBOX AND NUMBERED 1, because the sandbox is
	// the only level there is and a floor of zero means "nobody is in a
	// dungeon", which would make the record say the fight is not happening.
	Writer->BeginRun(FGuid::NewGuid(), FGuid::NewGuid(),
					 FName(TEXT("Sandbox")), /*Floor=*/1);
	return true;
}

int32 ACataclysmGameMode::RarityStepFor(int32 Setting,
									   const AActor* Spawned) const
{
	if (Setting != UCataclysmEnemyRarity::RollTheRarity)
	{
		// A RUNG WAS ASKED FOR, so it is used exactly. This is what a person
		// testing one rarity sets, and what an automation test passes.
		return Setting;
	}

	const UWorld* World = GetWorld();
	if (!World || !Spawned)
	{
		return 0;
	}

	// A STREAM PER CREATURE, SEEDED FROM ITS OWN IDENTITY AND THE CLOCK. The
	// same shape ACataclysmEnemyCharacter uses to seed its drop roll, and for
	// the same reason it gives: creatures made in the same frame must not all
	// come out the same, and a shared stream would make them do exactly that.
	FRandomStream Stream(Spawned->GetUniqueID()
		^ static_cast<int32>(World->GetTimeSeconds() * 1000.0f));

	// EVERY CREATURE DRAWS ITS OWN, INDEPENDENTLY. See UCataclysmEnemyRarity for
	// why an independent draw rather than a guaranteed count per floor.
	const UDataTable* Table = UCataclysmEnemyRarity::LoadEnemyRarityTable();
	const int32 Step = UCataclysmEnemyRarity::RollRarityStep(Table, Stream);

	// SAID OUT LOUD, BECAUSE NOTHING ON SCREEN SAYS IT. An enemy's rarity has no
	// name plate, no colour and no size, and the three things it changes -- how
	// many items the kill drops, the magic find it adds to its own drops, and
	// whether it can be stunned -- are all invisible until the creature is dead.
	// Without this line the only way to find out what spawned is to kill it and
	// infer from the loot. Issue #740 is the screen work that would make this
	// line unnecessary.
	//
	// Log rather than Verbose, deliberately. It is one line per creature at the
	// start of a session, and it is the only evidence a person has that the draw
	// happened at all.
	UE_LOG(LogCataclysm, Log, TEXT("%s spawned as %s (rarity step %d)."),
		   *GetNameSafe(Spawned),
		   *UCataclysmEnemyRarity::RarityNameForStep(Table, Step), Step);

	return Step;
}

int32 ACataclysmGameMode::SpawnAbyssalWardens()
{
	UWorld* World = GetWorld();
	if (!World || AbyssalWardenCount <= 0)
	{
		return 0;
	}

	FVector Centre = FVector::ZeroVector;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Centre = It->GetActorLocation();
		break;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// RAISED BY THE DIFFERENCE IN CAPSULE HALF-HEIGHT, the same correction
	// SpawnBrutes makes. The player start sits at the height a default capsule
	// needs, and this creature's is 34 cm taller than the base enemy's, so
	// spawning at the same height buries its feet until the movement component
	// pushes it out.
	constexpr float BaseEnemyCapsuleHalfHeight = 80.0f;
	const float RiseCm =
		ACataclysmAbyssalWardenCharacter::WardenCapsuleHalfHeight
		- BaseEnemyCapsuleHalfHeight;

	int32 Spawned = 0;
	for (int32 Index = 0; Index < AbyssalWardenCount; ++Index)
	{
		// Spread around the same centre when there is more than one, and
		// directly in front when there is one.
		const float Angle = 2.0f * PI * static_cast<float>(Index)
						  / static_cast<float>(FMath::Max(AbyssalWardenCount, 1));
		const FVector Where = Centre + FVector(
			FMath::Cos(Angle) * AbyssalWardenDistanceCm,
			FMath::Sin(Angle) * AbyssalWardenDistanceCm,
			RiseCm);

		const FRotator Facing = (Centre - Where).Rotation();

		ACataclysmAbyssalWardenCharacter* Warden =
			World->SpawnActor<ACataclysmAbyssalWardenCharacter>(
				ACataclysmAbyssalWardenCharacter::StaticClass(), Where, Facing,
				SpawnParams);
		if (!Warden)
		{
			continue;
		}

		Warden->SetHealth(AbyssalWardenHealth);
		Warden->SetAttackDamage(AbyssalWardenAttackDamage);

		// ARMOUR ARRIVES FROM THE SPAWNER, which is what the enemy class's own
		// header says it must, and until issue #525 nothing anywhere called this.
		// Every enemy in the sandbox had none, so the difficulty tier divided a
		// zero and made no difference to anything.
		Warden->SetArmour(AbyssalWardenArmour);

		// RARITY ARRIVES FROM THE SPAWNER TOO, and until issue #721 nothing
		// anywhere called this outside the automation tests, so every
		// creature in a play session was Common and dropped 0.16 items.
		Warden->SetRarityStep(RarityStepFor(AbyssalWardenRarityStep, Warden));

		AbyssalWardens.Add(Warden);
		++Spawned;
	}

	// THE LOG SAYS IT CANNOT CHASE, because that is the first thing anybody
	// watching it will notice and it is designed rather than broken.
	UE_LOG(LogCataclysm, Verbose,
		TEXT("Put %d Abyssal Wardens %.0f cm from %s. Each has %.0f health and "
			 "%.0f armour at difficulty tier %d, "
			 "hits for %.0f every %.1f s, walks at %.0f cm/s and never runs. "
			 "Its Molten Roar marks a %.0f cm ring every %.0f s. It has no "
			 "charge and cannot close on a player who walks away: issue #491."),
		Spawned, AbyssalWardenDistanceCm, *Centre.ToCompactString(),
		AbyssalWardenHealth, AbyssalWardenArmour, DifficultyTierFor(this),
		AbyssalWardenAttackDamage,
		ACataclysmAbyssalWardenCharacter::DesignedAttackIntervalSeconds,
		ACataclysmAbyssalWardenCharacter::DesignedWalkSpeedCmPerSecond,
		ACataclysmAbyssalWardenCharacter::MoltenRoarRadiusCm,
		ACataclysmAbyssalWardenCharacter::MoltenRoarCooldownSeconds);

	return Spawned;
}

int32 ACataclysmGameMode::SpawnHellhounds()
{
	UWorld* World = GetWorld();
	if (!World || HellhoundCount <= 0)
	{
		return 0;
	}

	FVector Centre = FVector::ZeroVector;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Centre = It->GetActorLocation();
		break;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// RAISED BY THE DIFFERENCE IN CAPSULE HALF-HEIGHT, the same correction the
	// other two spawners make. The player start sits at the height a default
	// capsule needs, and this creature's is 26.25 cm taller than the base
	// enemy's, so spawning at the same height buries its feet until the movement
	// component pushes it out.
	constexpr float BaseEnemyCapsuleHalfHeight = 80.0f;
	const float RiseCm =
		ACataclysmHellhoundCharacter::HellhoundCapsuleHalfHeight
		- BaseEnemyCapsuleHalfHeight;

	// FROM A BEARING RATHER THAN FROM ZERO, and this is the only spawner that
	// does. The header says why: a single creature otherwise goes directly in
	// front at +X, and the Brute and the Abyssal Warden already stand on that
	// line with no floor left beyond them.
	const float BearingRadians = FMath::DegreesToRadians(HellhoundBearingDegrees);

	int32 Spawned = 0;
	for (int32 Index = 0; Index < HellhoundCount; ++Index)
	{
		// Spread around the same centre when there is more than one, starting
		// from the bearing rather than from +X.
		const float Angle = BearingRadians
						  + 2.0f * PI * static_cast<float>(Index)
							  / static_cast<float>(FMath::Max(HellhoundCount, 1));
		const FVector Where = Centre + FVector(
			FMath::Cos(Angle) * HellhoundDistanceCm,
			FMath::Sin(Angle) * HellhoundDistanceCm,
			RiseCm);

		const FRotator Facing = (Centre - Where).Rotation();

		ACataclysmHellhoundCharacter* Hellhound =
			World->SpawnActor<ACataclysmHellhoundCharacter>(
				ACataclysmHellhoundCharacter::StaticClass(), Where, Facing,
				SpawnParams);
		if (!Hellhound)
		{
			continue;
		}

		Hellhound->SetHealth(HellhoundHealth);
		Hellhound->SetAttackDamage(HellhoundAttackDamage);
		Hellhound->SetArmour(HellhoundArmour);
		Hellhound->SetRarityStep(RarityStepFor(HellhoundRarityStep, Hellhound));

		Hellhounds.Add(Hellhound);
		++Spawned;
	}

	// THE LOG SAYS IT CATCHES THE PLAYER, because that is the first thing
	// anybody watching it will notice and it is the design rather than the
	// defect issue #417 records on the Brute.
	//
	// AND THAT ITS FIRE BURNS ITSELF, because a creature losing health while
	// nothing is attacking it looks like a bug and is not one.
	UE_LOG(LogCataclysm, Verbose,
		TEXT("Put %d Hellhounds %.0f cm from %s on a bearing of %.0f degrees. "
			 "Each has %.0f health and %.0f armour at difficulty tier %d, "
			 "bites for %.0f every %.1f s, and moves at %.0f cm/s -- faster "
			 "than any player class, so it cannot be walked away from. Its "
			 "Hellrush charges %.0f cm every %.0f s and leaves that lane "
			 "burning for %.0f s, and THAT FIRE BURNS THE HELLHOUND ITSELF and "
			 "any other enemy standing in it. It is the only thing in the game "
			 "that does."),
		Spawned, HellhoundDistanceCm, *Centre.ToCompactString(),
		HellhoundBearingDegrees,
		HellhoundHealth, HellhoundArmour, DifficultyTierFor(this),
		HellhoundAttackDamage,
		ACataclysmHellhoundCharacter::DesignedAttackIntervalSeconds,
		ACataclysmHellhoundCharacter::DesignedWalkSpeedCmPerSecond,
		ACataclysmHellhoundCharacter::HellrushRangeCm,
		ACataclysmHellhoundCharacter::HellrushCooldownSeconds,
		ACataclysmHellhoundCharacter::HellrushGroundSeconds);

	return Spawned;
}

int32 ACataclysmGameMode::SpawnImps()
{
	UWorld* World = GetWorld();
	if (!World || ImpCount <= 0)
	{
		return 0;
	}

	FVector Centre = FVector::ZeroVector;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Centre = It->GetActorLocation();
		break;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// RAISED BY THE DIFFERENCE IN CAPSULE HALF-HEIGHT, the same correction the
	// other three spawners make. This creature's capsule is 7.95 cm taller than
	// the base enemy's, which is the smallest difference of the four.
	constexpr float BaseEnemyCapsuleHalfHeight = 80.0f;
	const float RiseCm =
		ACataclysmImpCharacter::ImpCapsuleHalfHeight - BaseEnemyCapsuleHalfHeight;

	// THE MIDDLE OF THE PACK, NOT WHERE ANY ONE OF THEM STANDS. Every other
	// spawner spreads its creatures around the player start; this one spreads
	// them around a point of their own, so ten arrive as a group standing
	// together rather than as a ring already surrounding the player.
	const float BearingRadians = FMath::DegreesToRadians(ImpBearingDegrees);
	const FVector PackCentre = Centre + FVector(
		FMath::Cos(BearingRadians) * ImpDistanceCm,
		FMath::Sin(BearingRadians) * ImpDistanceCm,
		RiseCm);

	int32 Spawned = 0;
	for (int32 Index = 0; Index < ImpCount; ++Index)
	{
		const float Angle = 2.0f * PI * static_cast<float>(Index)
						  / static_cast<float>(FMath::Max(ImpCount, 1));
		const FVector Where = PackCentre + FVector(
			FMath::Cos(Angle) * ImpPackRadiusCm,
			FMath::Sin(Angle) * ImpPackRadiusCm,
			0.0f);

		// EACH FACES THE PLAYER START rather than the middle of its own pack,
		// which is what makes ten of them read as one group looking the same way.
		const FRotator Facing = (Centre - Where).Rotation();

		ACataclysmImpCharacter* Imp = World->SpawnActor<ACataclysmImpCharacter>(
			ACataclysmImpCharacter::StaticClass(), Where, Facing, SpawnParams);
		if (!Imp)
		{
			continue;
		}

		Imp->SetHealth(ImpHealth);
		Imp->SetAttackDamage(ImpAttackDamage);

		// NO SetArmour CALL, AND IT IS DELIBERATE. See the header: this
		// creature's designed armour share is exactly zero and it is the only
		// one in the roster with none. Calling SetArmour(0) would look like a
		// figure somebody chose rather than one the design refuses to give.
		Imp->SetRarityStep(RarityStepFor(ImpRarityStep, Imp));

		Imps.Add(Imp);
		++Spawned;
	}

	// THE LOG SAYS WHAT THE PACK IS WORTH TOGETHER, because no single one of
	// these creatures is worth anything and reading ten separate spawn lines
	// does not add up to that.
	UE_LOG(LogCataclysm, Verbose,
		TEXT("Put %d Imps around a point %.0f cm from %s on a bearing of %.0f "
			 "degrees, each %.0f cm from the middle of the pack. Each has %.0f "
			 "health, NO ARMOUR, and 25%% evasion, and swipes for %.0f every "
			 "%.1f s at a reach of %.0f cm -- which is the SECOND rank of a "
			 "crowd, so twenty can hit a player at once. Together they are "
			 "%.0f health and %.0f damage a second. They move at %.0f cm/s, "
			 "faster than any player class, so walking away is not an escape. "
			 "They will not queue into rings until issue #761 lands."),
		Spawned, ImpDistanceCm, *Centre.ToCompactString(), ImpBearingDegrees,
		ImpPackRadiusCm, ImpHealth, ImpAttackDamage,
		ACataclysmImpCharacter::DesignedAttackIntervalSeconds,
		ACataclysmImpCharacter::DesignedMeleeReachCm,
		ImpHealth * Spawned,
		ImpAttackDamage * Spawned
			/ ACataclysmImpCharacter::DesignedAttackIntervalSeconds,
		ACataclysmImpCharacter::DesignedWalkSpeedCmPerSecond);

	return Spawned;
}

int32 ACataclysmGameMode::SpawnBrutes()
{
	UWorld* World = GetWorld();
	if (!World || BruteCount <= 0)
	{
		return 0;
	}

	FVector Centre = FVector::ZeroVector;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Centre = It->GetActorLocation();
		break;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// RAISED BY THE DIFFERENCE IN CAPSULE HALF-HEIGHT. The player start sits at
	// the height a default capsule needs, and the Brute's is 30 cm taller than
	// the base enemy's, so spawning at the same Z buries its feet in the floor
	// until the movement component pushes it out.
	constexpr float BaseEnemyCapsuleHalfHeight = 80.0f;
	const float RiseCm =
		ACataclysmBruteCharacter::BruteCapsuleHalfHeight - BaseEnemyCapsuleHalfHeight;

	int32 Spawned = 0;
	for (int32 Index = 0; Index < BruteCount; ++Index)
	{
		// Spread around the same centre when there is more than one, and
		// directly in front when there is one.
		const float Angle = 2.0f * PI * static_cast<float>(Index)
						  / static_cast<float>(FMath::Max(BruteCount, 1));
		const FVector Where = Centre + FVector(
			FMath::Cos(Angle) * BruteDistanceCm,
			FMath::Sin(Angle) * BruteDistanceCm,
			RiseCm);

		const FRotator Facing = (Centre - Where).Rotation();

		ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
			ACataclysmBruteCharacter::StaticClass(), Where, Facing, SpawnParams);
		if (!Brute)
		{
			continue;
		}

		Brute->SetHealth(BruteHealth);
		Brute->SetAttackDamage(BruteAttackDamage);

		// See the note beside the Abyssal Warden's call. This is the creature the
		// design calls heavily armoured, and it had no armour at all until
		// issue #525.
		Brute->SetArmour(BruteArmour);

		// See the note beside the Abyssal Warden's call.
		Brute->SetRarityStep(RarityStepFor(BruteRarityStep, Brute));

		Brutes.Add(Brute);
		++Spawned;
	}

	UE_LOG(LogCataclysm, Verbose,
		TEXT("Put %d Brutes %.0f cm from %s. Each has %.0f health and %.0f "
			 "armour at difficulty tier %d, hits for "
			 "%.0f every %.1f s, walks at %.0f cm/s and turns at %.0f deg/s."),
		Spawned, BruteDistanceCm, *Centre.ToCompactString(),
		BruteHealth, BruteArmour, DifficultyTierFor(this), BruteAttackDamage,
		ACataclysmBruteCharacter::DesignedAttackIntervalSeconds,
		ACataclysmBruteCharacter::DesignedWalkSpeedCmPerSecond,
		ACataclysmBruteCharacter::DesignedTurnRateDegreesPerSecond);

	return Spawned;
}

int32 ACataclysmGameMode::SpawnTrainingDummies()
{
	UWorld* World = GetWorld();
	if (!World || TrainingDummyCount <= 0)
	{
		return 0;
	}

	// Around the player start rather than around the world origin, so the ring
	// is where the player actually appears whatever the level looks like. The
	// origin is the fallback when a level has no player start at all.
	FVector Centre = FVector::ZeroVector;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Centre = It->GetActorLocation();
		break;
	}

	FActorSpawnParameters SpawnParams;

	// AlwaysSpawn, because a dummy that overlaps the floor or another dummy
	// should still exist. The alternative silently produces fewer than asked
	// for, and a sandbox that sometimes has four enemies and sometimes five is
	// worse than one that always has five.
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	int32 Spawned = 0;
	for (int32 Index = 0; Index < TrainingDummyCount; ++Index)
	{
		const float Angle = 2.0f * PI * static_cast<float>(Index)
						  / static_cast<float>(TrainingDummyCount);
		const FVector Where = Centre + FVector(
			FMath::Cos(Angle) * TrainingDummyRingRadius,
			FMath::Sin(Angle) * TrainingDummyRingRadius,
			0.0f);

		// Facing the centre, so a ring of them looks deliberate rather than
		// scattered.
		const FRotator Facing = (Centre - Where).Rotation();

		ACataclysmEnemyCharacter* Dummy = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), Where, Facing, SpawnParams);
		if (!Dummy)
		{
			continue;
		}

		// Both remembered and applied, so it does not matter whether the ability
		// system has been wired up yet. See ACataclysmEnemyCharacter::SetHealth.
		Dummy->SetHealth(TrainingDummyHealth);
		Dummy->SetAttackDamage(TrainingDummyAttackDamage);
		Dummy->SetRarityStep(RarityStepFor(TrainingDummyRarityStep, Dummy));

		TrainingDummies.Add(Dummy);
		++Spawned;
	}

	UE_LOG(LogCataclysm, Verbose,
		TEXT("Put %d training dummies in a ring of %.0f cm around %s. "
			 "Each has %.0f health and hits for %.0f."),
		Spawned, TrainingDummyRingRadius, *Centre.ToCompactString(),
		TrainingDummyHealth, TrainingDummyAttackDamage);

	return Spawned;
}
