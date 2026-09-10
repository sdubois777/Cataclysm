// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmClothStress.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmSuccubusCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The reproduction for issue #1545, tested where a test can reach it.
 *
 * WHAT IS COVERED. That a batch takes the three cloth creatures in turn, puts
 * them on the ring, and gives them no brain; that the same seed puts them in the
 * same places and another seed does not; that killing a batch sends every
 * creature through its own death and never kills one twice; and that the count
 * sees only the living and reads each creature's two cloth switches.
 *
 * WHAT CANNOT BE. The failure the command exists to reproduce. The automation
 * command passes -nullrhi, so nothing is drawn and the renderer's cloth check is
 * never reached. That half is run by hand in a game that draws, and the pull
 * request for issue #1545 records what it printed.
 */
namespace CataclysmClothStressTest
{
	/** A batch held the way the console command holds one. */
	static TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> Held(
		const TArray<ACataclysmEnemyCharacter*>& Batch)
	{
		TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> Weak;
		for (ACataclysmEnemyCharacter* Creature : Batch)
		{
			Weak.Add(Creature);
		}
		return Weak;
	}
}

/**
 * A batch takes the Brute, the Abyssal Warden and the Succubus in turn, puts
 * each on the ring, and gives none of them a brain.
 *
 * NO BRAIN IS WHAT MAKES A RUN A REPRODUCTION RATHER THAN A FIGHT. A creature
 * with a controller walks to the player and attacks, and the player's death is
 * a different path through the game from the one being measured.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClothStressBatchTakesTurns,
	"Cataclysm.ClothStress.ABatchTakesTheThreeClothCreaturesInTurnWithNoBrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClothStressBatchTakesTurns::RunTest(const FString&)
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	TestEqual(TEXT("three creatures take turns"),
		CataclysmClothStress::CreatureClasses().Num(), 3);

	const FVector Centre(1000.0f, -2000.0f, 0.0f);
	const TArray<ACataclysmEnemyCharacter*> Batch =
		CataclysmClothStress::SpawnBatch(World, Centre, 6, /*Seed=*/7);

	if (!TestEqual(TEXT("six asked for, six spawned"), Batch.Num(), 6))
	{
		return false;
	}

	TestTrue(TEXT("the first is a Brute"),
		Batch[0]->IsA<ACataclysmBruteCharacter>());
	TestTrue(TEXT("the second an Abyssal Warden"),
		Batch[1]->IsA<ACataclysmAbyssalWardenCharacter>());
	TestTrue(TEXT("the third a Succubus"),
		Batch[2]->IsA<ACataclysmSuccubusCharacter>());
	TestTrue(TEXT("and the fourth starts again with a Brute"),
		Batch[3]->IsA<ACataclysmBruteCharacter>());

	for (const ACataclysmEnemyCharacter* Creature : Batch)
	{
		TestNull(FString::Printf(TEXT("%s has no controller"), *Creature->GetName()),
			Creature->GetController());

		const float Distance =
			FVector::Dist2D(Creature->GetActorLocation(), Centre);
		TestTrue(FString::Printf(TEXT("%s stands on the ring, %.1f cm from the centre"),
				*Creature->GetName(), Distance),
			Distance >= CataclysmClothStress::InnerRingCm - 1.0f
			&& Distance <= CataclysmClothStress::OuterRingCm + 1.0f);
	}

	return true;
}

/**
 * The same seed puts the same creatures in the same places, and another seed
 * does not.
 *
 * THE SECOND HALF IS WHAT MAKES THE FIRST MEAN ANYTHING. Placement that ignored
 * the seed and put every creature in one fixed place would pass the first half
 * on its own.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClothStressSeedRepeats,
	"Cataclysm.ClothStress.TheSameSeedPutsTheSameCreaturesInTheSamePlaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClothStressSeedRepeats::RunTest(const FString&)
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FVector Centre = FVector::ZeroVector;
	const TArray<ACataclysmEnemyCharacter*> First =
		CataclysmClothStress::SpawnBatch(World, Centre, 4, /*Seed=*/11);
	const TArray<ACataclysmEnemyCharacter*> Again =
		CataclysmClothStress::SpawnBatch(World, Centre, 4, /*Seed=*/11);
	const TArray<ACataclysmEnemyCharacter*> Other =
		CataclysmClothStress::SpawnBatch(World, Centre, 4, /*Seed=*/12);

	if (!TestEqual(TEXT("four in the first batch"), First.Num(), 4)
		|| !TestEqual(TEXT("four in the repeat"), Again.Num(), 4)
		|| !TestEqual(TEXT("four with the other seed"), Other.Num(), 4))
	{
		return false;
	}

	float LargestMove = 0.0f;
	for (int32 Index = 0; Index < First.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("creature %d is the same creature"), Index),
			First[Index]->GetClass(), Again[Index]->GetClass());
		TestTrue(FString::Printf(TEXT("creature %d stands in the same place"), Index),
			FVector::Dist2D(First[Index]->GetActorLocation(),
							Again[Index]->GetActorLocation()) < 0.01f);

		LargestMove = FMath::Max(LargestMove, static_cast<float>(FVector::Dist2D(
			First[Index]->GetActorLocation(), Other[Index]->GetActorLocation())));
	}

	TestTrue(FString::Printf(TEXT("another seed moves at least one creature by "
			"more than a metre; the largest move was %.1f cm"), LargestMove),
		LargestMove > 100.0f);

	return true;
}

/**
 * Killing a batch sends each creature through its own death, once.
 *
 * THROUGH HandleDeath AND NOT BY REMOVING THE ACTOR, because the crash came
 * while creatures were dying: a death clip plays on the mesh and the body is
 * removed after it, and a run that skipped both would not be the run that
 * crashed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClothStressKillsOnce,
	"Cataclysm.ClothStress.KillingABatchSendsEachCreatureThroughItsDeathOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClothStressKillsOnce::RunTest(const FString&)
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const TArray<ACataclysmEnemyCharacter*> Batch =
		CataclysmClothStress::SpawnBatch(World, FVector::ZeroVector, 3, /*Seed=*/3);
	if (!TestEqual(TEXT("three spawned"), Batch.Num(), 3))
	{
		return false;
	}

	// THE CONTROL FOR THE TICK CHECK BELOW. Only a creature's own death switches
	// its tick off, so the check means something only if it was on.
	for (const ACataclysmEnemyCharacter* Creature : Batch)
	{
		TestTrue(FString::Printf(TEXT("%s is ticking before it dies"), *Creature->GetName()),
			Creature->IsActorTickEnabled());
	}

	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> Creatures =
		CataclysmClothStressTest::Held(Batch);
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> SameAgain = Creatures;

	TestEqual(TEXT("all three are killed"),
		CataclysmClothStress::KillAll(Creatures), 3);
	TestEqual(TEXT("and forgotten"), Creatures.Num(), 0);

	for (const ACataclysmEnemyCharacter* Creature : Batch)
	{
		TestTrue(FString::Printf(TEXT("%s is marked dead"), *Creature->GetName()),
			UCataclysmSkillEffects::IsDead(Creature));
		TestFalse(FString::Printf(TEXT("%s stopped ticking, which its death does"),
				*Creature->GetName()),
			Creature->IsActorTickEnabled());
	}

	TestEqual(TEXT("killing the same creatures again kills nobody"),
		CataclysmClothStress::KillAll(SameAgain), 0);

	return true;
}

/**
 * The count sees only the living, and reads both cloth switches.
 *
 * EACH SWITCH IS SET BY HAND, so this checks the counting and not whatever a
 * creature's defaults happen to be. The defaults are the subject of issue
 * #1545's fix and are checked where the fix is.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClothStressCounts,
	"Cataclysm.ClothStress.TheCountSeesOnlyTheLivingAndReadsBothClothSwitches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClothStressCounts::RunTest(const FString&)
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const TArray<ACataclysmEnemyCharacter*> Batch =
		CataclysmClothStress::SpawnBatch(World, FVector::ZeroVector, 3, /*Seed=*/5);
	if (!TestEqual(TEXT("three spawned"), Batch.Num(), 3))
	{
		return false;
	}

	USkeletalMeshComponent* SwitchedOn = Batch[0]->GetMesh();
	USkeletalMeshComponent* NoClothActors = Batch[1]->GetMesh();
	USkeletalMeshComponent* SimulationOff = Batch[2]->GetMesh();
	if (!TestNotNull(TEXT("the first has a mesh component"), SwitchedOn)
		|| !TestNotNull(TEXT("the second has a mesh component"), NoClothActors)
		|| !TestNotNull(TEXT("the third has a mesh component"), SimulationOff))
	{
		return false;
	}

	SwitchedOn->SetAllowClothActors(true);
	SwitchedOn->bDisableClothSimulation = false;

	NoClothActors->SetAllowClothActors(false);
	NoClothActors->bDisableClothSimulation = false;

	SimulationOff->SetAllowClothActors(true);
	SimulationOff->bDisableClothSimulation = true;

	const TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> Creatures =
		CataclysmClothStressTest::Held(Batch);

	const CataclysmClothStress::FClothCount Before =
		CataclysmClothStress::CountCloth(Creatures);
	TestEqual(TEXT("three living creatures are counted"), Before.Creatures, 3);
	TestEqual(TEXT("only the one with both switches on counts as switched on"),
		Before.ClothSwitchedOn, 1);

	Batch[0]->HandleDeath();

	const CataclysmClothStress::FClothCount After =
		CataclysmClothStress::CountCloth(Creatures);
	TestEqual(TEXT("a dead creature is not counted"), After.Creatures, 2);
	TestEqual(TEXT("so neither is its switch"), After.ClothSwitchedOn, 0);

	// THE MODELS, WHICH ONLY A MACHINE WITH THE ART CAN CHECK.
	int32 Dressed = 0;
	for (const ACataclysmEnemyCharacter* Creature : Batch)
	{
		if (Creature->GetMesh() && Creature->GetMesh()->GetSkeletalMeshAsset())
		{
			++Dressed;
		}
	}

	if (Dressed < Batch.Num())
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			FString::Printf(TEXT("only %d of the three creatures is wearing its "
				"model, because the Paragon Rampage, Grux and Countess packs are "
				"not all present. That the count finds cloth on all three models "
				"is not checked; the switches and the living are."), Dressed));
		return true;
	}

	TestEqual(TEXT("all three models carry cloth"), Before.WearingClothModels, 3);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
