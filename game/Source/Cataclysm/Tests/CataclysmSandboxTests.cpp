// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "GameFramework/PlayerStart.h"
#include "Player/CataclysmGameMode.h"

/**
 * Tests for the sandbox having something in it to fight.
 *
 * WHAT THESE GUARD. Issue #170: game/Content/Maps/L_Sandbox.umap held a floor,
 * a light, a sky, a player start and navigation, and no enemy at all. Every
 * skill behaviour built so far had nothing to act on, so a cone found no
 * targets, a projectile passed through empty space, and burning ground burned
 * nobody. All of it was covered by automation tests that spawn their own
 * actors, and none of it could be seen.
 *
 * WHAT THEY DELIBERATELY CHECK. Not only that actors appear, but that they are
 * actors the combat code can find and hurt -- which is the whole point of
 * putting them there. An enemy that spawns and is invisible to
 * UCataclysmTargeting would satisfy a naive count and change nothing.
 */

namespace CataclysmSandboxTest
{
	/**
	 * A world that has BEGUN PLAY, unlike the ones the other test files build.
	 *
	 * That matters here and nowhere else. An enemy registers its attribute sets
	 * in BeginPlay, and a world that never begins play never calls it -- so a
	 * spawned enemy would have no health attribute to read and this test would
	 * be checking nothing. Actors spawned after this point get their BeginPlay
	 * called as they spawn, which is the same order the real game uses.
	 */
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game,
										   /*bInformEngineOfWorld=*/false);
		if (!World)
		{
			return nullptr;
		}

		FURL URL;
		World->InitializeActorsForPlay(URL);
		World->BeginPlay();
		return World;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTrainingDummiesSpawnTest,
	"Cataclysm.Sandbox.TrainingDummiesAppearAndCanBeFoundAndHurt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTrainingDummiesSpawnTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSandboxTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmGameMode* GameMode = World->SpawnActor<ACataclysmGameMode>();
	if (!GameMode)
	{
		AddError(TEXT("Could not spawn the game mode."));
		return false;
	}

	// Called directly rather than through StartPlay, because StartPlay also
	// wants a player controller and a pawn and this test is about the dummies.
	const int32 Spawned = GameMode->SpawnTrainingDummies();

	TestTrue(FString::Printf(
		TEXT("The sandbox spawns training dummies (%d)"), Spawned), Spawned > 0);
	TestEqual(TEXT("And records every one it made"),
		GameMode->TrainingDummies.Num(), Spawned);

	if (Spawned == 0)
	{
		return false;
	}

	// THEY ARE ACTORS THE COMBAT CODE CAN FIND. A dummy the targeting cannot
	// see is a dummy no skill can hit, which would leave issue #170 open while
	// looking fixed. Searched from the centre of the ring with a radius wide
	// enough to cover it.
	AActor* Caster = World->SpawnActor<AActor>();
	const TArray<AActor*> Found = UCataclysmTargeting::FindEnemiesInSphere(
		World, Caster, GameMode->TrainingDummies[0]->GetActorLocation(),
		/*RadiusCm=*/200.0f);

	TestTrue(TEXT("A skill's targeting finds a dummy standing in front of it"),
		Found.Contains(GameMode->TrainingDummies[0]));

	// AND THEY CAN BE HURT. Health above zero is what makes damage mean
	// anything; a dummy at zero health is already dead.
	ACataclysmEnemyCharacter* First = GameMode->TrainingDummies[0];
	UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(First);
	if (!AbilitySystem)
	{
		AddError(TEXT("A training dummy has no ability system, so nothing can "
					  "damage it."));
		return false;
	}

	const float Health = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetHealthAttribute());
	const float MaxHealth = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute());

	TestTrue(FString::Printf(TEXT("A dummy has health (%.0f)"), Health),
		Health > 0.0f);
	TestEqual(TEXT("And starts at full"), Health, MaxHealth);

	// ENOUGH TO SURVIVE A HEAVY ATTACK, which is what makes the sandbox useful.
	// An unupgraded Greataxe supplies about 41 attack damage and the Heavy slot
	// is 250% of it, so roughly 102. A dummy at the enemy default of 100 health
	// dies to one press and there is nothing to watch.
	TestTrue(FString::Printf(
		TEXT("And enough of it to survive a Heavy Attack (%.0f)"), Health),
		Health > 200.0f);

	// They stand apart from each other, so an area skill hitting one is a
	// choice rather than a certainty.
	if (GameMode->TrainingDummies.Num() >= 2)
	{
		const float Apart = FVector::Dist(
			GameMode->TrainingDummies[0]->GetActorLocation(),
			GameMode->TrainingDummies[1]->GetActorLocation());
		TestTrue(FString::Printf(TEXT("Two dummies stand apart (%.0f cm)"), Apart),
			Apart > 100.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTrainingDummiesRingThePlayerStartTest,
	"Cataclysm.Sandbox.TrainingDummiesRingWhereThePlayerAppears",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTrainingDummiesRingThePlayerStartTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSandboxTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// AROUND THE PLAYER START, NOT THE WORLD ORIGIN. A ring at the origin is
	// useless in a level whose player start is somewhere else: the player spawns
	// with nothing near them and the sandbox looks empty. Placed well away from
	// the origin so the two cannot be confused.
	const FVector StartWhere(3000.0f, -2000.0f, 0.0f);
	APlayerStart* Start = World->SpawnActor<APlayerStart>(
		StartWhere, FRotator::ZeroRotator);
	if (!Start)
	{
		AddError(TEXT("Could not spawn a player start."));
		return false;
	}

	ACataclysmGameMode* GameMode = World->SpawnActor<ACataclysmGameMode>();
	if (!GameMode)
	{
		AddError(TEXT("Could not spawn the game mode."));
		return false;
	}

	const int32 Spawned = GameMode->SpawnTrainingDummies();
	TestTrue(TEXT("Dummies were spawned"), Spawned > 0);

	for (const TObjectPtr<ACataclysmEnemyCharacter>& Dummy : GameMode->TrainingDummies)
	{
		if (!IsValid(Dummy))
		{
			AddError(TEXT("A recorded training dummy is not valid."));
			continue;
		}

		// Compared in the horizontal plane, because a spawn can be pushed
		// upward by the floor and the ring is a horizontal arrangement.
		FVector Offset = Dummy->GetActorLocation() - StartWhere;
		Offset.Z = 0.0f;

		TestTrue(FString::Printf(
			TEXT("A dummy stands near the player start, not the origin "
				 "(%.0f cm away)"), Offset.Size()),
			Offset.Size() < 1000.0f);

		// Nowhere near the world origin, which is 3606 cm from the start.
		TestTrue(TEXT("And not at the world origin"),
			Dummy->GetActorLocation().Size() > 1500.0f);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
