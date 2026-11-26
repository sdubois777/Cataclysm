// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"

/**
 * THE WAR VOW OPTIONS' FIFTH SLICE, SECOND HALF, THIRD PART, V5b3, 2026-11-25: Walking Arsenal, on the coordinating
 * session's rulings A1 and A2. War machines stand in for devices, as `CataclysmWarVowsV5aTests.cpp` uses them.
 *
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmWarVowsV5b3Test
{
	constexpr float WarVowsV5b3Metre = 100.0f;

	/** A possessed player at the origin with a pool of 100 holding 100, walking at 600 cm a second. */
	struct FWarVowsV5b3Player
	{
		explicit FWarVowsV5b3Player(UWorld* World)
		{
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!State || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(State);
			Controller->Possess(Character);
			System = State->GetCataclysmAbilitySystemComponent();
			if (System)
			{
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												100.0f);
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 100.0f);
			}
			if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			{
				Movement->MaxWalkSpeed = 600.0f;
			}
		}
		bool IsUsable() const { return Character && System && Character->GetCharacterMovement(); }

		/** Walking Arsenal's reach at `Metres`, or nought for none. Replaces the whole stat line. */
		void FollowsWithin(float Metres) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			if (Metres > 0.0f)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::PassiveKeystone;
				Row.Value = Metres;
				Stats.FindOrAdd(FName(ACataclysmMinion::GadgetFollowWithinMetresStat)).Modifiers.Add(Row);
			}
			System->SetStatInputs(MoveTemp(Stats));
		}

		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	ACataclysmMinion* WarVowsV5b3MachineAt(AActor* Player, const FVector& Where, const TCHAR* Type)
	{
		return ACataclysmMinion::Spawn(Player, Where, /*Lifetime=*/600.0f, /*bBurns=*/false, Type);
	}

	float WarVowsV5b3FromOrigin(const AActor* Actor)
	{
		return FVector::Dist2D(Actor->GetActorLocation(), FVector::ZeroVector);
	}
}

#define CATACLYSM_WAR_VOWS_V5B3_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_WAR_VOWS_V5B3_TEST(FCataclysmWarVowsV5b3WalksTest,
	"Cataclysm.WarVowsV5b3.AGadgetFartherThanEightMetresWalksAfterThePlayerAndStopsAtEight")
{
	using namespace CataclysmWarVowsV5b3Test;
	// A BOLT TURRET 20 m EAST OF THE PLAYER: one step moves it 1.25 times the player's walking speed for a quarter of a
	// second toward the player (1.875 m at 6 m a second), and steps enough bring it to 8 m and no nearer. THE CONTROLS: without the option a step moves nothing; a
	// turret 5 m away does not move; a Spike Trap 20 m away, a trap, does not follow; and the walking turret still stays
	// where it is put to its brain, which is what keeps it firing as it moves (ruled A2).
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5b3Player Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const FVector East(WarVowsV5b3Metre, 0.0f, 0.0f);
	ACataclysmMinion* Far = WarVowsV5b3MachineAt(Player.Character, East * 20.0f, TEXT("BoltTurret"));
	ACataclysmMinion* Near = WarVowsV5b3MachineAt(Player.Character, FVector(0.0f, 5.0f * WarVowsV5b3Metre, 0.0f),
												  TEXT("BoltTurret"));
	ACataclysmMinion* Spike = WarVowsV5b3MachineAt(Player.Character, East * -20.0f, TEXT("SpikeTrap"));
	if (!TestTrue(TEXT("set-up: two turrets and a Spike Trap"), Far && Near && Spike)
		|| !TestTrue(TEXT("set-up: the turrets are gadgets and the Spike Trap a trap"),
					 Far->IsGadget() && Near->IsGadget() && Spike->IsTrap() && !Spike->IsGadget()))
	{
		return false;
	}

	ACataclysmMinion::GadgetStep(Far);
	TestEqual(TEXT("control: without the option a step moves nothing"), WarVowsV5b3FromOrigin(Far),
			  20.0f * WarVowsV5b3Metre, 0.01f);

	Player.FollowsWithin(8.0f);
	// THE SPEED IS READ NOW, because the player writes its own walking speed after its stats change.
	const float Walk = Player.Character->GetCharacterMovement()->MaxWalkSpeed;
	const float OneStep = Walk * UCataclysmCommand::CommandedWalkSpeedShareOfCommander * ACataclysmMinion::DeviceStepSeconds;
	if (!TestTrue(*FString::Printf(TEXT("set-up: the player walks: %.1f cm a second"), Walk), Walk > 0.0f))
	{
		return false;
	}
	ACataclysmMinion::GadgetStep(Far);
	TestEqual(TEXT("with it, one step moves the far turret 1.25 times the player's walk for a quarter second nearer"),
			  WarVowsV5b3FromOrigin(Far), 20.0f * WarVowsV5b3Metre - OneStep, 0.5f);
	ACataclysmMinion::GadgetStep(Near);
	TestEqual(TEXT("control: a turret within 8 m does not move"), WarVowsV5b3FromOrigin(Near), 5.0f * WarVowsV5b3Metre,
			  0.01f);
	ACataclysmMinion::GadgetStep(Spike);
	TestEqual(TEXT("control: a trap does not follow"), WarVowsV5b3FromOrigin(Spike), 20.0f * WarVowsV5b3Metre, 0.01f);
	TestTrue(TEXT("control: the walking turret still stays where it is put to its brain, so it keeps firing"),
			 Far->StaysWhereItIsPut());

	for (int32 Step = 0; Step < 200; ++Step)
	{
		ACataclysmMinion::GadgetStep(Far);
	}
	TestEqual(TEXT("enough steps bring it to 8 m and no nearer"), WarVowsV5b3FromOrigin(Far), 8.0f * WarVowsV5b3Metre,
			  0.5f);
	return true;
}

CATACLYSM_WAR_VOWS_V5B3_TEST(FCataclysmWarVowsV5b3HandedOverTest,
	"Cataclysm.WarVowsV5b3.AWarPlayerTakingWalkingArsenalIsHandedItsReach")
{
	// A LEVEL 100 WAR PLAYER THROUGH `SetPassiveAllocation`: Walking Arsenal (First Oath 2) hands over a reach of 8 m.
	// THE CONTROL: Baited (option 1) hands over none.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestTrue(TEXT("set-up: a player"), State && Controller && Character))
	{
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Character);
	State->SetCreationChoice(FName(TEXT("Greataxe")), FName(TEXT("War")));
	State->SetLevelAndExperience(100, 0);
	UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent();
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	if (!TestTrue(TEXT("set-up: its ability system and equipment"), System && Equipment))
	{
		return false;
	}
	const auto Take = [&](int32 Option)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(TEXT("Saboteur_capstone_25")), 1);
		Allocation.SetChosenOption(FName(TEXT("Saboteur_capstone_25")), Option);
		State->SetPassiveAllocation(Allocation, TArray<FName>());
		Equipment->RefreshAttributes(System);
	};
	Take(1);
	TestEqual(TEXT("control: Baited hands over no reach"), ACataclysmMinion::GadgetFollowWithinCmFor(Character), 0.0f);
	Take(2);
	TestEqual(TEXT("Walking Arsenal: within 8 m"), ACataclysmMinion::GadgetFollowWithinCmFor(Character), 800.0f, 0.01f);
	return true;
}

#undef CATACLYSM_WAR_VOWS_V5B3_TEST

#endif // WITH_AUTOMATION_TESTS
