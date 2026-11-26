// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmDeviceWire.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * THE WAR VOW OPTIONS' FIFTH SLICE, SECOND HALF, SECOND PART, V5b2, 2026-11-25: Cat's Cradle, on the coordinating
 * session's rulings C1 and C2. War machines stand in for devices, as `CataclysmWarVowsV5aTests.cpp` uses them: a Spike
 * Trap and a Bolt Turret, whose blows differ, so the damage an enemy takes says which end answered.
 *
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmWarVowsV5b2Test
{
	constexpr float WarVowsV5b2Metre = 100.0f;

	/** A possessed player at the origin with a pool of 100 holding 100. */
	struct FWarVowsV5b2Player
	{
		explicit FWarVowsV5b2Player(UWorld* World)
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
		}
		bool IsUsable() const { return Character && System; }

		/** Cat's Cradle's three flat stats at these figures, or none at all. Replaces the whole stat line. */
		void HoldsTheCradle(bool bHolds, float ReachMetres = 10.0f) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			if (bHolds)
			{
				const TPair<const TCHAR*, float> Rows[] = {
					{ACataclysmMinion::DeviceWireReachMetresStat, ReachMetres},
					{ACataclysmMinion::DeviceWirePinSecondsStat, 1.0f},
					{ACataclysmMinion::DeviceWireRepinSecondsStat, 4.0f}};
				for (const TPair<const TCHAR*, float>& Each : Rows)
				{
					FCataclysmStatModifier Row;
					Row.Bucket = ECataclysmStatBucket::Flat;
					Row.Source = ECataclysmModifierSource::PassiveKeystone;
					Row.Value = Each.Value;
					Stats.FindOrAdd(FName(Each.Key)).Modifiers.Add(Row);
				}
			}
			System->SetStatInputs(MoveTemp(Stats));
		}

		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	ACataclysmEnemyCharacter* WarVowsV5b2CreatureAt(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(100000.0f);
		}
		return Made;
	}

	float WarVowsV5b2HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	ACataclysmMinion* WarVowsV5b2MachineAt(AActor* Player, const FVector& Where, const TCHAR* Type)
	{
		return ACataclysmMinion::Spawn(Player, Where, /*Lifetime=*/600.0f, /*bBurns=*/false, Type);
	}

	/** What one blow of `Machine` takes off a fresh creature at `Where`, far from every wire. */
	float WarVowsV5b2OneBlowOf(UWorld* World, ACataclysmMinion* Machine, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Measure = WarVowsV5b2CreatureAt(World, Where);
		if (!Machine || !Measure)
		{
			return -1.0f;
		}
		const float Before = WarVowsV5b2HealthOf(Measure);
		Machine->AttackTarget(Measure);
		const float Taken = Before - WarVowsV5b2HealthOf(Measure);
		Measure->Destroy();
		return Taken;
	}
}

#define CATACLYSM_WAR_VOWS_V5B2_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_WAR_VOWS_V5B2_TEST(FCataclysmWarVowsV5b2CrossingTest,
	"Cataclysm.WarVowsV5b2.AnEnemyCrossingAWireIsPinnedAndTheNearerEndAnswers")
{
	using namespace CataclysmWarVowsV5b2Test;
	// A SPIKE TRAP 20 m EAST AND A BOLT TURRET 6 m BEYOND IT: one wire. A creature 30 cm off the wire nearer the Spike Trap
	// is pinned and takes one Spike Trap blow; one nearer the turret takes one turret blow. THE CONTROLS: a creature 2 m off
	// the wire is not caught; without the option nothing is; the caught are not caught again within 4 s, and are after.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5b2Player Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const FVector East(WarVowsV5b2Metre, 0.0f, 0.0f);
	const FVector North(0.0f, WarVowsV5b2Metre, 0.0f);
	ACataclysmMinion* Spike = WarVowsV5b2MachineAt(Player.Character, East * 20.0f, TEXT("SpikeTrap"));
	ACataclysmMinion* Turret = WarVowsV5b2MachineAt(Player.Character, East * 26.0f, TEXT("BoltTurret"));
	if (!TestTrue(TEXT("set-up: a Spike Trap and a Bolt Turret"), Spike && Turret))
	{
		return false;
	}
	const float SpikeBlow = WarVowsV5b2OneBlowOf(World, Spike, North * 40.0f);
	const float TurretBlow = WarVowsV5b2OneBlowOf(World, Turret, North * -40.0f);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the two blows take something and differ: %.2f and %.2f"), SpikeBlow,
								   TurretBlow),
				  SpikeBlow > 0.0f && TurretBlow > 0.0f && FMath::Abs(SpikeBlow - TurretBlow) > 1.0f))
	{
		return false;
	}

	ACataclysmEnemyCharacter* NearSpike = WarVowsV5b2CreatureAt(World, East * 21.0f + North * 0.3f);
	ACataclysmEnemyCharacter* NearTurret = WarVowsV5b2CreatureAt(World, East * 25.0f - North * 0.3f);
	ACataclysmEnemyCharacter* Aside = WarVowsV5b2CreatureAt(World, East * 23.0f + North * 2.0f);
	if (!TestTrue(TEXT("set-up: three creatures"), NearSpike && NearTurret && Aside))
	{
		return false;
	}
	const float NearSpikeBefore = WarVowsV5b2HealthOf(NearSpike);
	const float NearTurretBefore = WarVowsV5b2HealthOf(NearTurret);
	const float AsideBefore = WarVowsV5b2HealthOf(Aside);

	TestEqual(TEXT("control: without the option a step catches nobody"), Player.System->StepCatsCradle(), 0);
	TestFalse(TEXT("control: and pins nobody"), UCataclysmSkillEffects::IsPinned(NearSpike));

	Player.HoldsTheCradle(true);
	TestEqual(TEXT("with it, a step catches the two on the wire"), Player.System->StepCatsCradle(), 2);
	TestTrue(TEXT("the one near the Spike Trap is pinned"), UCataclysmSkillEffects::IsPinned(NearSpike));
	TestTrue(TEXT("and the one near the turret"), UCataclysmSkillEffects::IsPinned(NearTurret));
	TestEqual(TEXT("the nearer end answers: the Spike Trap's blow"), NearSpikeBefore - WarVowsV5b2HealthOf(NearSpike),
			  SpikeBlow, 0.5f);
	TestEqual(TEXT("and the turret's"), NearTurretBefore - WarVowsV5b2HealthOf(NearTurret), TurretBlow, 0.5f);
	TestFalse(TEXT("control: a creature 2 m off the wire is not pinned"), UCataclysmSkillEffects::IsPinned(Aside));
	TestEqual(TEXT("control: and takes nothing"), WarVowsV5b2HealthOf(Aside), AsideBefore, 0.01f);

	World->TimeSeconds += 2.0f;
	TestEqual(TEXT("control: 2 s later neither is caught again"), Player.System->StepCatsCradle(), 0);
	World->TimeSeconds += 2.5f;
	TestEqual(TEXT("4.5 s after, both are"), Player.System->StepCatsCradle(), 2);
	return true;
}

CATACLYSM_WAR_VOWS_V5B2_TEST(FCataclysmWarVowsV5b2WiresTest,
	"Cataclysm.WarVowsV5b2.AWireJoinsDevicesWithinReachIsSeenAndComesDownWithoutTheOption")
{
	using namespace CataclysmWarVowsV5b2Test;
	// THREE BOLT TURRETS at 20, 28 and 40 m east: only the first two are within 10 m, so one wire is seen, 8 m long. THE
	// CONTROLS: before the option no wire stands; a creature on the line between the second and third is not caught; and
	// with the option gone the wire comes down.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5b2Player Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const FVector East(WarVowsV5b2Metre, 0.0f, 0.0f);
	ACataclysmMinion* First = WarVowsV5b2MachineAt(Player.Character, East * 20.0f, TEXT("BoltTurret"));
	ACataclysmMinion* Second = WarVowsV5b2MachineAt(Player.Character, East * 28.0f, TEXT("BoltTurret"));
	ACataclysmMinion* Third = WarVowsV5b2MachineAt(Player.Character, East * 40.0f, TEXT("BoltTurret"));
	ACataclysmEnemyCharacter* Between = WarVowsV5b2CreatureAt(World, East * 34.0f);
	if (!TestTrue(TEXT("set-up: three turrets and a creature"), First && Second && Third && Between))
	{
		return false;
	}
	Player.System->StepCatsCradle();
	TestEqual(TEXT("control: without the option no wire is seen"), Player.System->GetDeviceWires().Num(), 0);

	Player.HoldsTheCradle(true);
	TestEqual(TEXT("control: a creature between devices 12 m apart is not caught"), Player.System->StepCatsCradle(), 0);
	const TArray<TObjectPtr<ACataclysmDeviceWire>> Wires = Player.System->GetDeviceWires();
	if (!TestEqual(TEXT("one wire is seen, between the two within 10 m"), Wires.Num(), 1))
	{
		return false;
	}
	ACataclysmDeviceWire* Wire = Wires[0];
	TestEqual(TEXT("8 m long"), Wire->GetLengthCm(), 8.0f * WarVowsV5b2Metre, 1.0f);
	TestTrue(TEXT("halfway between them"),
			 FVector::Dist2D(Wire->GetActorLocation(), East * 24.0f) < 1.0f);
	TestTrue(TEXT("and drawn"), Wire->GetLine() && Wire->GetLine()->GetStaticMesh() != nullptr);

	Player.HoldsTheCradle(true, /*ReachMetres=*/13.0f);
	Player.System->StepCatsCradle();
	TestEqual(TEXT("with a reach of 13 m, the second and third are joined too"), Player.System->GetDeviceWires().Num(), 2);

	Player.HoldsTheCradle(false);
	Player.System->StepCatsCradle();
	TestEqual(TEXT("with the option gone, no wire is held"), Player.System->GetDeviceWires().Num(), 0);
	TestTrue(TEXT("and the one seen is taken down"), !IsValid(Wire) || Wire->IsActorBeingDestroyed());
	return true;
}

CATACLYSM_WAR_VOWS_V5B2_TEST(FCataclysmWarVowsV5b2HandedOverTest,
	"Cataclysm.WarVowsV5b2.AWarPlayerTakingCatsCradleIsHandedItsThreeFigures")
{
	// A LEVEL 100 WAR PLAYER THROUGH `SetPassiveAllocation`: Cat's Cradle (Second Oath 3) hands over a reach of 10 m, a
	// pin of 1 s and 4 s between pins. THE CONTROL: Trail of Wire (option 1) hands over none of them.
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
		Allocation.Add(FName(TEXT("Saboteur_capstone_50")), 1);
		Allocation.SetChosenOption(FName(TEXT("Saboteur_capstone_50")), Option);
		State->SetPassiveAllocation(Allocation, TArray<FName>());
		Equipment->RefreshAttributes(System);
	};
	Take(1);
	TestEqual(TEXT("control: Trail of Wire hands over no reach"), ACataclysmMinion::DeviceWireReachCmFor(Character), 0.0f);
	Take(3);
	TestEqual(TEXT("Cat's Cradle: a reach of 10 m"), ACataclysmMinion::DeviceWireReachCmFor(Character), 1000.0f, 0.01f);
	TestEqual(TEXT("a pin of 1 s"), ACataclysmMinion::DeviceWirePinSecondsFor(Character), 1.0f, 0.001f);
	TestEqual(TEXT("and 4 s between pins"), ACataclysmMinion::DeviceWireRepinSecondsFor(Character), 4.0f, 0.001f);
	return true;
}

#undef CATACLYSM_WAR_VOWS_V5B2_TEST

#endif // WITH_AUTOMATION_TESTS
