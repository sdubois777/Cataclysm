// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmClothStress.h"

#include "Cataclysm.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmSuccubusCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Items/CataclysmDroppedItem.h"
#include "Math/RandomStream.h"

TArray<TSubclassOf<ACataclysmEnemyCharacter>> CataclysmClothStress::CreatureClasses()
{
	return {
		ACataclysmBruteCharacter::StaticClass(),
		ACataclysmAbyssalWardenCharacter::StaticClass(),
		ACataclysmSuccubusCharacter::StaticClass(),
	};
}

namespace
{
	/**
	 * The height a creature of `Class` stands at over `Where`: its capsule's
	 * bottom on the floor below. The height it was asked for when there is no
	 * floor below, which is the case in every automation test world.
	 *
	 * WHY IT ASKS THE FLOOR. The three creatures' capsules are different heights
	 * -- the Brute's half-height is 110 cm against the base enemy's 80 -- so one
	 * spawn height would bury some of them and float others.
	 */
	float StandingHeightAt(const UWorld& World, const FVector& Where,
						   TSubclassOf<ACataclysmEnemyCharacter> Class)
	{
		const ACataclysmEnemyCharacter* Default = Class ? Class.GetDefaultObject() : nullptr;
		const UCapsuleComponent* Capsule = Default ? Default->GetCapsuleComponent() : nullptr;
		const float HalfHeight = Capsule ? Capsule->GetUnscaledCapsuleHalfHeight() : 0.0f;

		// FROM JUST ABOVE THE CENTRE'S HEIGHT rather than from high above it, so
		// a ceiling over the arena is not taken for the floor.
		FHitResult Hit;
		const FVector From = Where + FVector(0.0f, 0.0f, 100.0f);
		const FVector To = Where - FVector(0.0f, 0.0f, 2000.0f);
		const FCollisionQueryParams Params(FName(TEXT("CataclysmClothStress")),
										   /*bInTraceComplex=*/false);
		if (World.LineTraceSingleByChannel(Hit, From, To, ECC_WorldStatic, Params))
		{
			return Hit.ImpactPoint.Z + HalfHeight;
		}

		return Where.Z;
	}
}

TArray<ACataclysmEnemyCharacter*> CataclysmClothStress::SpawnBatch(
	UWorld* World, const FVector& Centre, int32 Count, int32 Seed)
{
	TArray<ACataclysmEnemyCharacter*> Spawned;
	if (!World || Count <= 0)
	{
		return Spawned;
	}

	const TArray<TSubclassOf<ACataclysmEnemyCharacter>> Classes = CreatureClasses();
	FRandomStream Stream(Seed);
	Spawned.Reserve(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const TSubclassOf<ACataclysmEnemyCharacter> Class =
			Classes[Index % Classes.Num()];

		// BOTH DRAWN FOR EVERY CREATURE, WHETHER OR NOT IT SPAWNS, so one that
		// fails to spawn does not move every creature after it.
		const float Angle = Stream.FRandRange(0.0f, 2.0f * PI);
		const float Distance = Stream.FRandRange(InnerRingCm, OuterRingCm);

		FVector Where = Centre + FVector(FMath::Cos(Angle) * Distance,
										 FMath::Sin(Angle) * Distance, 0.0f);
		Where.Z = StandingHeightAt(*World, Where, Class);

		// FACING THE CENTRE, which is where the player stands.
		const FRotator Facing(0.0f, (Centre - Where).Rotation().Yaw, 0.0f);
		const FTransform Transform(Facing, Where);

		ACataclysmEnemyCharacter* Creature =
			World->SpawnActorDeferred<ACataclysmEnemyCharacter>(
				Class.Get(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Creature)
		{
			continue;
		}

		// NO BRAIN. Set before FinishSpawning because that is when a pawn is
		// given its controller. A creature with one walks to the player and
		// attacks, and the player's death is a different path through the game
		// from the one this is reproducing.
		Creature->AutoPossessAI = EAutoPossessAI::Disabled;
		Creature->FinishSpawning(Transform);

		Spawned.Add(Creature);
	}

	return Spawned;
}

int32 CataclysmClothStress::KillAll(
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>>& Creatures)
{
	int32 Killed = 0;

	for (const TWeakObjectPtr<ACataclysmEnemyCharacter>& Held : Creatures)
	{
		ACataclysmEnemyCharacter* Creature = Held.Get();
		if (!Creature || UCataclysmSkillEffects::IsDead(Creature))
		{
			continue;
		}

		// THE ENGINE CHOOSES ITS LEVEL OF DETAIL AGAIN, so a body playing its
		// death clip is not held at whichever level the `lod` option left it on.
		if (USkeletalMeshComponent* Mesh = Creature->GetMesh())
		{
			Mesh->SetForcedLOD(0);
		}

		Creature->HandleDeath();
		++Killed;
	}

	Creatures.Reset();
	return Killed;
}

CataclysmClothStress::FClothCount CataclysmClothStress::CountCloth(
	const TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>>& Creatures)
{
	FClothCount Count;

	for (const TWeakObjectPtr<ACataclysmEnemyCharacter>& Held : Creatures)
	{
		const ACataclysmEnemyCharacter* Creature = Held.Get();
		if (!Creature || UCataclysmSkillEffects::IsDead(Creature))
		{
			continue;
		}

		const USkeletalMeshComponent* Mesh = Creature->GetMesh();
		if (!Mesh)
		{
			continue;
		}

		++Count.Creatures;

		const USkeletalMesh* Model = Mesh->GetSkeletalMeshAsset();
		if (Model && Model->GetMeshClothingAssets().Num() > 0)
		{
			++Count.WearingClothModels;
		}

		if (Mesh->GetAllowClothActors() && !Mesh->bDisableClothSimulation)
		{
			++Count.ClothSwitchedOn;
		}

		// WHAT THE RENDERER WOULD BE HANDED FOR THIS CREATURE NOW. See the note on
		// FClothCount::CarryingClothData for why this call and not another.
		TMap<int32, FClothSimulData> ClothData;
		FMatrix LocalToWorld = FMatrix::Identity;
		float BlendWeight = 0.0f;
		Mesh->GetUpdateClothSimulationData_AnyThread(ClothData, LocalToWorld,
													 BlendWeight);
		if (ClothData.Num() > 0)
		{
			++Count.CarryingClothData;
		}
	}

	return Count;
}

#if !UE_BUILD_SHIPPING

namespace
{
	/** The run in progress. There is at most one; starting another replaces it. */
	struct FClothStressRun
	{
		TWeakObjectPtr<UWorld> World;
		int32 PerCycle = 0;
		float SecondsPerCycle = 0.0f;
		bool bChurnDetail = false;

		int32 Cycle = 0;
		int32 Frame = 0;
		int32 TotalSpawned = 0;
		float SecondsIntoCycle = 0.0f;
		double StartedAt = 0.0;

		TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> Alive;
		FTSTicker::FDelegateHandle Ticker;
	};

	TUniquePtr<FClothStressRun> GClothStressRun;

	constexpr int32 DefaultPerCycle = 24;
	constexpr float DefaultSecondsPerCycle = 3.0f;

	/** More than this in one batch is a different test: one of frame time. */
	constexpr int32 MostPerCycle = 90;

	/** The player's position, or the world's origin when there is no player. */
	FVector CentreIn(UWorld& World)
	{
		if (const APlayerController* Player = World.GetFirstPlayerController())
		{
			if (const APawn* Pawn = Player->GetPawn())
			{
				return Pawn->GetActorLocation();
			}
		}

		return FVector::ZeroVector;
	}

	/**
	 * Remove every item lying on the floor.
	 *
	 * WHY A STRESS RUN THROWS LOOT AWAY. Every death rolls drops, and a run kills
	 * thousands of creatures in a quarter of an hour. Left alone the floor fills
	 * with items, and the run ends up measuring a level that grows heavier every
	 * cycle rather than the one it started in.
	 */
	int32 SweepDrops(UWorld& World)
	{
		TArray<ACataclysmDroppedItem*> Drops;
		for (TActorIterator<ACataclysmDroppedItem> It(&World); It; ++It)
		{
			Drops.Add(*It);
		}

		for (ACataclysmDroppedItem* Drop : Drops)
		{
			Drop->Destroy();
		}

		return Drops.Num();
	}

	/** Put each living creature on a different level of detail from last frame. */
	void ChurnDetail(FClothStressRun& Run)
	{
		int32 Index = 0;
		for (const TWeakObjectPtr<ACataclysmEnemyCharacter>& Held : Run.Alive)
		{
			ACataclysmEnemyCharacter* Creature = Held.Get();
			USkeletalMeshComponent* Mesh = Creature ? Creature->GetMesh() : nullptr;
			const int32 Levels = Mesh ? Mesh->GetNumLODs() : 0;

			if (Levels > 1)
			{
				// SetForcedLOD COUNTS FROM ONE. Zero hands the choice back to the
				// engine, which is what KillAll does.
				Mesh->SetForcedLOD(1 + (Run.Frame + Index) % Levels);
			}

			++Index;
		}
	}

	void BeginCycle(FClothStressRun& Run, UWorld& World)
	{
		++Run.Cycle;
		Run.SecondsIntoCycle = 0.0f;

		const TArray<ACataclysmEnemyCharacter*> Batch =
			CataclysmClothStress::SpawnBatch(&World, CentreIn(World),
											 Run.PerCycle, /*Seed=*/Run.Cycle);
		for (ACataclysmEnemyCharacter* Creature : Batch)
		{
			Run.Alive.Add(Creature);
		}

		Run.TotalSpawned += Batch.Num();
	}

	void FinishCycle(FClothStressRun& Run, UWorld& World)
	{
		// COUNTED BEFORE THE KILLING, while the cloth has had the whole cycle to
		// simulate.
		const CataclysmClothStress::FClothCount Count =
			CataclysmClothStress::CountCloth(Run.Alive);

		UE_LOG(LogCataclysm, Log,
			TEXT("Cloth stress cycle %d: %d creatures alive, %d wear a model with "
				 "cloth, %d have cloth switched on, %d are handing the renderer "
				 "cloth data. %d spawned in %.0f s so far."),
			Run.Cycle, Count.Creatures, Count.WearingClothModels,
			Count.ClothSwitchedOn, Count.CarryingClothData, Run.TotalSpawned,
			FPlatformTime::Seconds() - Run.StartedAt);

		CataclysmClothStress::KillAll(Run.Alive);
		SweepDrops(World);
	}

	bool TickRun(float DeltaSeconds)
	{
		if (!GClothStressRun)
		{
			return false;
		}

		FClothStressRun& Run = *GClothStressRun;
		UWorld* World = Run.World.Get();
		if (!World || World->bIsTearingDown)
		{
			UE_LOG(LogCataclysm, Log,
				TEXT("Cloth stress stopped after %d cycles and %d creatures: its "
					 "world ended."),
				Run.Cycle, Run.TotalSpawned);

			// NOT FTSTicker::RemoveTicker. This is running inside the ticker, and
			// removing a ticker from its own delegate waits for that delegate to
			// finish, which is this call. Answering false removes it instead.
			GClothStressRun.Reset();
			return false;
		}

		++Run.Frame;
		Run.SecondsIntoCycle += DeltaSeconds;

		if (Run.bChurnDetail)
		{
			ChurnDetail(Run);
		}

		if (Run.SecondsIntoCycle >= Run.SecondsPerCycle)
		{
			FinishCycle(Run, *World);
			BeginCycle(Run, *World);
		}

		return true;
	}

	/** End the run in progress, if there is one, from outside its ticker. */
	void StopRun(const TCHAR* Why)
	{
		if (!GClothStressRun)
		{
			return;
		}

		FTSTicker::RemoveTicker(GClothStressRun->Ticker);
		const int32 Killed = CataclysmClothStress::KillAll(GClothStressRun->Alive);

		UE_LOG(LogCataclysm, Log,
			TEXT("Cloth stress stopped after %d cycles and %d creatures: %s. "
				 "Killed the %d still standing."),
			GClothStressRun->Cycle, GClothStressRun->TotalSpawned, Why, Killed);

		GClothStressRun.Reset();
	}
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmClothStress(
	TEXT("Cataclysm.Debug.ClothStress"),
	TEXT("Reproduce issue #1545, the renderer's cloth check. Kills every enemy in "
		 "the level, then repeatedly spawns Brutes, Abyssal Wardens and Succubi "
		 "around the player with no brain and kills them a few seconds later, "
		 "writing one log line a cycle and throwing away the loot. "
		 "Cataclysm.Debug.ClothStress [creatures per cycle, default 24] [seconds "
		 "per cycle, default 3] [lod]. `lod` puts every creature on a different "
		 "level of detail every frame. Cataclysm.Debug.ClothStress stop ends it."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			if (Args.Num() > 0 && Args[0].Equals(TEXT("stop"), ESearchCase::IgnoreCase))
			{
				if (!GClothStressRun)
				{
					Ar.Log(TEXT("No cloth stress is running."));
					return;
				}

				StopRun(TEXT("asked to stop"));
				Ar.Log(TEXT("Cloth stress stopped."));
				return;
			}

			if (!World || !World->IsGameWorld())
			{
				Ar.Log(TEXT("Run this while playing. It needs a game world to "
							"spawn creatures into."));
				return;
			}

			int32 PerCycle = DefaultPerCycle;
			float SecondsPerCycle = DefaultSecondsPerCycle;
			bool bChurnDetail = false;
			int32 NumbersRead = 0;

			for (const FString& Arg : Args)
			{
				if (Arg.Equals(TEXT("lod"), ESearchCase::IgnoreCase))
				{
					bChurnDetail = true;
				}
				else if (Arg.IsNumeric())
				{
					if (NumbersRead == 0)
					{
						PerCycle = FMath::Clamp(FCString::Atoi(*Arg), 1, MostPerCycle);
					}
					else if (NumbersRead == 1)
					{
						SecondsPerCycle = FMath::Clamp(FCString::Atof(*Arg), 0.5f, 60.0f);
					}
					++NumbersRead;
				}
			}

			// ONE AT A TIME. A second start replaces the first rather than
			// running beside it.
			StopRun(TEXT("replaced by a new run"));

			// THE LEVEL'S OWN ENEMIES GO FIRST, so nothing with a brain attacks
			// the player while the run lasts. Collected before any of them dies,
			// because a death spawns loot into the level being walked.
			TArray<ACataclysmEnemyCharacter*> AlreadyHere;
			for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
			{
				AlreadyHere.Add(*It);
			}

			int32 Cleared = 0;
			for (ACataclysmEnemyCharacter* Enemy : AlreadyHere)
			{
				if (IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy))
				{
					Enemy->HandleDeath();
					++Cleared;
				}
			}
			SweepDrops(*World);

			GClothStressRun = MakeUnique<FClothStressRun>();
			FClothStressRun& Run = *GClothStressRun;
			Run.World = World;
			Run.PerCycle = PerCycle;
			Run.SecondsPerCycle = SecondsPerCycle;
			Run.bChurnDetail = bChurnDetail;
			Run.StartedAt = FPlatformTime::Seconds();

			BeginCycle(Run, *World);
			Run.Ticker = FTSTicker::GetCoreTicker().AddTicker(
				TEXT("CataclysmClothStress"), 0.0f,
				[](float DeltaSeconds) { return TickRun(DeltaSeconds); });

			UE_LOG(LogCataclysm, Log,
				TEXT("Cloth stress started: %d creatures every %.1f s around %s, "
					 "level of detail changed every frame: %s. Killed %d enemies "
					 "that were already in the level."),
				PerCycle, SecondsPerCycle, *CentreIn(*World).ToCompactString(),
				bChurnDetail ? TEXT("yes") : TEXT("no"), Cleared);

			Ar.Logf(TEXT("Cloth stress started: %d creatures every %.1f s. "
						 "Cataclysm.Debug.ClothStress stop ends it."),
					PerCycle, SecondsPerCycle);
		}));

#endif // !UE_BUILD_SHIPPING
