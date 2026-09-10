// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

/**
 * Tests that a character's model is on its mesh before anything hanging from
 * the mesh registers. Issue #1542.
 *
 * WHAT THESE GUARD. The Brute's held rock and the player's two weapons are
 * attached to the mesh's hand bone and hand sockets in the constructor. A
 * component attached that way asks the mesh where that bone or socket is when
 * it registers, and until #1542 the model arrived only in BeginPlay, after
 * every component had registered. The engine logged a warning for each of
 * those lookups -- "GetSocketInfoByName(hand_r): No SkeletalMesh for
 * Component(CharacterMesh0)" -- 116 times in the owner's playtest of
 * 2026-09-10, and 5,260 times for 1,728 Brutes in one stress run that day.
 *
 * FIRST IN A WORLD THAT HAS NOT BEGUN PLAY, ON PURPOSE. BeginPlay puts the
 * model on as well, so in a world that ran it a model on the mesh after the
 * spawn could mean either. There only registration has run, and each test
 * asserts that before it reads the mesh.
 *
 * THEN IN A WORLD THAT HAS, spawned the way ACataclysmDungeonGameMode spawns
 * creatures: one SpawnActor into a running world, with BeginPlay and
 * everything before it inside that call. The playtest's Brutes were spawned
 * that way and logged 5.8 of these warnings each; the stress runs' Brutes,
 * spawned without a controller, logged 3.0 each. So the warnings are counted
 * over the whole of that spawn as well.
 *
 * EACH ZERO HAS A CONTROL. No warnings is also what a count that cannot see
 * warnings reports, so each test first provokes the warning on purpose, on a
 * mesh component with no model, and requires the count to see it.
 */

namespace CataclysmBodyBeforeSocketsTest
{
	/** The part of the engine's warning that is the same for every socket. */
	const TCHAR* NoModelWarning = TEXT("No SkeletalMesh for Component");

	/**
	 * Keeps every line written to the log while it is alive that carries the
	 * engine's no-model warning.
	 *
	 * REGISTERED WITH GLog, because UE_LOG writes there rather than to anything
	 * a caller can pass in, and removed again in the destructor so that a
	 * failing test cannot leave a dangling device on the global redirector. The
	 * same shape as the captures in CataclysmWeaponSlotsTests and
	 * CataclysmDamageOverTimeTests.
	 */
	struct FScopedNoModelWarnings : public FOutputDevice
	{
		FScopedNoModelWarnings()
		{
			if (GLog)
			{
				GLog->AddOutputDevice(this);
			}
		}

		virtual ~FScopedNoModelWarnings()
		{
			if (GLog)
			{
				GLog->RemoveOutputDevice(this);
			}
		}

		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity,
							   const class FName& Category) override
		{
			if (Text && FCString::Strstr(Text, NoModelWarning))
			{
				Lines.Add(FString(Text));
			}
		}

		/**
		 * Every line kept so far. Flushes first: FOutputDeviceRedirector buffers
		 * lines written from a thread that is not the primary one, and a test
		 * that read its own captures without flushing could see none of them.
		 */
		TArray<FString> Kept()
		{
			if (GLog)
			{
				GLog->Flush();
			}
			return Lines;
		}

		TArray<FString> Lines;
	};

	/**
	 * THE CONTROL. Asks a mesh component with no model for a bone, which is
	 * exactly what registering the rock did before #1542, and returns how many
	 * warnings were kept. A count that keeps none here cannot see the warning at
	 * all, and its zero in the tests below would mean nothing.
	 */
	int32 WarningsFromAskingAnEmptyMesh()
	{
		USkeletalMeshComponent* Empty =
			NewObject<USkeletalMeshComponent>(GetTransientPackage());
		FScopedNoModelWarnings Warnings;
		Empty->GetSocketTransform(TEXT("hand_r"));
		return Warnings.Kept().Num();
	}

	/** Spawns one T, keeping the no-model warnings written while it does. */
	template <typename T>
	T* SpawnKeepingWarnings(UWorld* World, TArray<FString>& OutWarnings)
	{
		FScopedNoModelWarnings Warnings;
		T* Spawned = World->SpawnActor<T>(FVector::ZeroVector, FRotator::ZeroRotator);
		OutWarnings = Warnings.Kept();
		return Spawned;
	}

	/** The path of the model on Character's mesh, or empty when it has none. */
	FString ModelOn(const ACharacter* Character)
	{
		const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
		const USkeletalMesh* Model = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
		return Model ? Model->GetPathName() : FString();
	}

	/** Fails What unless Warnings is empty, and lists every line when it is not. */
	void ExpectNone(FAutomationTestBase& Test, const TCHAR* What,
					const TArray<FString>& Warnings)
	{
		if (!Test.TestEqual(What, Warnings.Num(), 0))
		{
			for (const FString& Line : Warnings)
			{
				Test.AddInfo(Line);
			}
		}
	}

	/**
	 * Spawns one T into a world that has begun play and fails unless nothing
	 * asked an empty mesh for a bone or socket during the whole spawn.
	 */
	template <typename T>
	void ExpectNoneWhenSpawnedIntoARunningWorld(FAutomationTestBase& Test)
	{
		UWorld* Running = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world that has begun play"), Running))
		{
			return;
		}
		ON_SCOPE_EXIT { Running->DestroyWorld(false); };

		TArray<FString> Warnings;
		T* Spawned = SpawnKeepingWarnings<T>(Running, Warnings);
		if (!Test.TestNotNull(TEXT("a character spawned into it"), Spawned))
		{
			return;
		}

		if (!Test.TestTrue(TEXT("BeginPlay ran inside the spawn there"),
				Spawned->HasActorBegunPlay()))
		{
			return;
		}

		// RECORDED, NOT ASSERTED. Whether a controller arrives inside the spawn
		// is the character's business, not this test's; the line says which case
		// the count below covered.
		Test.AddInfo(FString::Printf(TEXT("spawned into a running world, it %s"),
			Spawned->GetController() ? TEXT("has a controller")
									 : TEXT("has no controller")));

		ExpectNone(Test, TEXT("and nothing asked an empty mesh for a bone or socket "
							  "during the whole of that spawn either"),
			Warnings);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteModelIsOnBeforeTheRockRegisters,
	"Cataclysm.Brute.ItsModelIsOnBeforeTheRockAsksForItsHand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteModelIsOnBeforeTheRockRegisters::RunTest(const FString&)
{
	using namespace CataclysmBodyBeforeSocketsTest;

	if (!TestTrue(TEXT("the count sees the engine's warning when an empty mesh is "
					   "asked for a bone"),
			WarningsFromAskingAnEmptyMesh() > 0))
	{
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasNotBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	TArray<FString> Warnings;
	ACataclysmBruteCharacter* Brute =
		SpawnKeepingWarnings<ACataclysmBruteCharacter>(World, Warnings);
	if (!TestNotNull(TEXT("brute"), Brute))
	{
		return false;
	}

	TestFalse(TEXT("BeginPlay has not run, so only registration can have put the "
				   "model on"),
		Brute->HasActorBegunPlay());

	// ASKED AFTER THE SPAWN, NOT BEFORE IT. Loading the model first would have
	// let a character that only looked for an already-loaded model pass here,
	// and in a game the first Brute of a session finds nothing loaded.
	if (FSoftObjectPath(ACataclysmBruteCharacter::BodyMeshPath).TryLoad() == nullptr)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this, TEXT("The Paragon Rampage pack "
			"is not installed, so the Brute has no model to put on and its rock "
			"still asks an empty mesh for the hand bone. Neither the model nor the "
			"warnings are checked; that the count sees the warning, and that the "
			"Brute spawns, are."));
		return true;
	}

	TestEqual(TEXT("the Rampage model is on the mesh before BeginPlay"),
		ModelOn(Brute), FString(ACataclysmBruteCharacter::BodyMeshPath));

	ExpectNone(*this, TEXT("and nothing asked an empty mesh for a bone or socket "
						   "while it spawned"),
		Warnings);

	ExpectNoneWhenSpawnedIntoARunningWorld<ACataclysmBruteCharacter>(*this);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmPlayerModelIsOnBeforeTheWeaponsRegister,
	"Cataclysm.PlayerBody.ItsModelIsOnBeforeTheWeaponsAskForTheirSockets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerModelIsOnBeforeTheWeaponsRegister::RunTest(const FString&)
{
	using namespace CataclysmBodyBeforeSocketsTest;

	if (!TestTrue(TEXT("the count sees the engine's warning when an empty mesh is "
					   "asked for a bone"),
			WarningsFromAskingAnEmptyMesh() > 0))
	{
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasNotBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	TArray<FString> Warnings;
	ACataclysmPlayerCharacter* Player =
		SpawnKeepingWarnings<ACataclysmPlayerCharacter>(World, Warnings);
	if (!TestNotNull(TEXT("player"), Player))
	{
		return false;
	}

	TestFalse(TEXT("BeginPlay has not run, so only registration can have put the "
				   "model on"),
		Player->HasActorBegunPlay());

	// ASKED AFTER THE SPAWN, for the reason the Brute's test gives.
	if (FSoftObjectPath(ACataclysmPlayerCharacter::BodyMeshPath).TryLoad() == nullptr)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this, TEXT("game/Content/Characters/"
			"Mannequins/ is not on this machine, so the player has no model to put "
			"on and its weapons still ask an empty mesh for their sockets. Neither "
			"the model nor the warnings are checked; that the count sees the "
			"warning, and that the player spawns, are."));
		return true;
	}

	TestEqual(TEXT("the Mannequin model is on the mesh before BeginPlay"),
		ModelOn(Player), FString(ACataclysmPlayerCharacter::BodyMeshPath));

	ExpectNone(*this, TEXT("and nothing asked an empty mesh for a socket while it "
						   "spawned"),
		Warnings);

	ExpectNoneWhenSpawnedIntoARunningWorld<ACataclysmPlayerCharacter>(*this);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmPlayerModelIsNotPutOnOutsideAGame,
	"Cataclysm.PlayerBody.ItsModelIsNotPutOnOutsideAGameWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerModelIsNotPutOnOutsideAGame::RunTest(const FString&)
{
	using namespace CataclysmBodyBeforeSocketsTest;

	// THE MODEL HAS TO BE THERE TO BE REFUSED. Without it the mesh below would
	// be empty whatever the character did, and the test would prove nothing.
	if (FSoftObjectPath(ACataclysmPlayerCharacter::BodyMeshPath).TryLoad() == nullptr)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this, TEXT("game/Content/Characters/"
			"Mannequins/ is not on this machine, so there is no model the character "
			"could have put on, and nothing here is checked."));
		return true;
	}

	// AN EDITOR PREVIEW WORLD, the kind every asset editor's viewport runs, as
	// the stand-in for an editor level. The character asks only whether the
	// world is a game world, and the answer is no for both.
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview,
										/*bInformEngineOfWorld=*/false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	if (!TestFalse(TEXT("the world is not a game world"), World->IsGameWorld()))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player"), Player))
	{
		return false;
	}

	// A LEVEL SAVED WITH THE MODEL ON WOULD CARRY A HARD REFERENCE TO IT, which
	// is what loading the art by soft path exists to avoid.
	TestEqual(TEXT("the model is not put on in a world that is not a game"),
		ModelOn(Player), FString());

	return true;
}

#endif // WITH_AUTOMATION_TESTS
