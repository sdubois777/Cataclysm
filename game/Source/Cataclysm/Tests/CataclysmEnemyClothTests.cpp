// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmCorruptedSentinelCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmGatekeeperCharacter.h"
#include "Character/CataclysmHellhoundCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmSuccubusCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * No enemy creates cloth or simulates it. Issue #1545.
 *
 * WHY. The project owner's Horde playtest on 2026-09-10 stopped the editor with
 * "Assertion failed: bPrevious" in the renderer's handling of simulated cloth,
 * and three creatures wear models that carry cloth. With cloth switched off on
 * an enemy's mesh the renderer is not handed cloth data for it. The project
 * owner approved the visible cost, cloth that follows the animation instead of
 * swinging; docs/DECISIONS.md has the ruling.
 *
 * WHAT IS COVERED. Both switches on every enemy class, and, where the art is
 * present, that the three models the switches matter for do carry cloth, that
 * the others do not, and that the engine refuses to simulate cloth for any of
 * them.
 *
 * WHAT CANNOT BE. That the renderer no longer reaches the failed check. The
 * automation command passes -nullrhi and draws nothing. The pull request for
 * issue #1545 records the reproduction, Cataclysm.Debug.ClothStress, run in a
 * game that draws, before and after.
 */
namespace CataclysmEnemyClothTest
{
	struct FEnemyKind
	{
		const TCHAR* Name;
		TSubclassOf<ACataclysmEnemyCharacter> Class;

		/** False for the base class, which never wears a model: the sandbox's
		 *  training dummies are the base class and stay placeholder cylinders. */
		bool bWearsAModel;

		/**
		 * Whether the model this creature wears carries cloth. Read on 2026-09-10
		 * from each model's .uasset name table, which names ClothConfig,
		 * ClothConstraintSetup and ClothCollisionData for exactly these three and
		 * for none of the other models the game uses.
		 */
		bool bModelCarriesCloth;
	};

	static TArray<FEnemyKind> EveryEnemy()
	{
		return {
			{ TEXT("the base enemy"), ACataclysmEnemyCharacter::StaticClass(), false, false },
			{ TEXT("the Brute"), ACataclysmBruteCharacter::StaticClass(), true, true },
			{ TEXT("the Abyssal Warden"), ACataclysmAbyssalWardenCharacter::StaticClass(), true, true },
			{ TEXT("the Succubus"), ACataclysmSuccubusCharacter::StaticClass(), true, true },
			{ TEXT("the Hellhound"), ACataclysmHellhoundCharacter::StaticClass(), true, false },
			{ TEXT("the Imp"), ACataclysmImpCharacter::StaticClass(), true, false },
			{ TEXT("the Corrupted Sentinel"), ACataclysmCorruptedSentinelCharacter::StaticClass(), true, false },
			{ TEXT("the Gatekeeper"), ACataclysmGatekeeperCharacter::StaticClass(), true, false },
		};
	}
}

/**
 * Every enemy has both of the engine's cloth switches off, and the engine agrees
 * that none of them can simulate cloth.
 *
 * BOTH SWITCHES, FOR DIFFERENT REASONS. `bAllowClothActors` off means no cloth
 * simulation is ever created for the mesh. `bDisableClothSimulation` on is what
 * USkeletalMeshComponent::GetUpdateClothSimulationData_AnyThread reads before it
 * hands the renderer anything, so no cloth reaches the renderer even if
 * something creates a simulation later.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoEnemySimulatesCloth,
	"Cataclysm.EnemyCloth.NoEnemyCreatesClothActorsOrSimulatesCloth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmNoEnemySimulatesCloth::RunTest(const FString&)
{
	using namespace CataclysmEnemyClothTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	TArray<FString> Undressed;
	int32 Index = 0;

	for (const FEnemyKind& Kind : EveryEnemy())
	{
		// FAR APART, because capsules spawned at contact distance push each
		// other apart.
		const FVector Where(3000.0f * Index++, 0.0f, 0.0f);
		ACataclysmEnemyCharacter* Enemy = World->SpawnActor<ACataclysmEnemyCharacter>(
			Kind.Class.Get(), Where, FRotator::ZeroRotator, Params);
		if (!TestNotNull(FString::Printf(TEXT("%s spawns"), Kind.Name), Enemy))
		{
			continue;
		}

		USkeletalMeshComponent* Mesh = Enemy->GetMesh();
		if (!TestNotNull(FString::Printf(TEXT("%s has a mesh component"), Kind.Name), Mesh))
		{
			continue;
		}

		TestFalse(FString::Printf(TEXT("%s does not allow clothing actors"), Kind.Name),
			Mesh->GetAllowClothActors());
		TestTrue(FString::Printf(TEXT("%s has cloth simulation disabled"), Kind.Name),
			static_cast<bool>(Mesh->bDisableClothSimulation));

		if (!Kind.bWearsAModel)
		{
			continue;
		}

		const USkeletalMesh* Model = Mesh->GetSkeletalMeshAsset();
		if (!Model)
		{
			Undressed.Add(Kind.Name);
			continue;
		}

		// THE CONTROL FOR THE CHECK BELOW. It shows the three models the switches
		// matter for do carry cloth, and that the probe tells them from the rest.
		TestEqual(FString::Printf(TEXT("%s's model %s"), Kind.Name,
				Kind.bModelCarriesCloth ? TEXT("carries cloth") : TEXT("carries no cloth")),
			Model->GetMeshClothingAssets().Num() > 0, Kind.bModelCarriesCloth);

		// THE ENGINE'S OWN ANSWER to whether this component may run a cloth
		// simulation. It is false for any component without a model, so it is
		// asked only of a dressed one, where it reads bAllowClothActors.
		TestFalse(FString::Printf(TEXT("the engine will not simulate cloth for %s"), Kind.Name),
			Mesh->CanSimulateClothing());
	}

	if (!Undressed.IsEmpty())
	{
		CataclysmTestSkip::ReportSkippedHalf(*this, FString::Printf(
			TEXT("%s wore no model, because the Paragon pack is not present. "
				 "Both switches were checked on every enemy; which models carry "
				 "cloth, and whether the engine will simulate it, were not "
				 "checked for those."),
			*FString::Join(Undressed, TEXT(", "))));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
