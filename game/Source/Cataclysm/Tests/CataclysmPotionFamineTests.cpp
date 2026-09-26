// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmPotions.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The three Famine dungeon modifiers built on the potions of issue #806: Hard
 * Mode, Recession and Diminishing Returns.
 *
 * EACH ROW REACHES THE PLAYER THE WAY A FLOOR'S RULES DO: a floor without the row
 * first, then `GoToFloor` onto one carrying it, which applies the floor's rules to
 * the player, as `Cataclysm.DungeonModifierEffects.DesperateMeasures...` does.
 * Then a real drink or a real kill says what the row changed.
 *
 * IN A FILE OF ITS OWN rather than at the end of
 * `CataclysmDungeonModifierEffectsTests.cpp`, where the dungeon rules' own work
 * lands, so the two do not collide.
 */
namespace CataclysmPotionFamineTest
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Potions = UCataclysmPotions;

	struct FFloor
	{
		UWorld* World = nullptr;
		ACataclysmDungeonGameMode* Mode = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;

		bool IsUsable() const { return Mode && Character && AbilitySystem; }
	};

	/** A game mode and a possessed player, standing on a floor without the row. */
	FFloor Make(FAutomationTestBase& Test, UWorld* World)
	{
		FFloor Made;
		Made.World = World;
		Made.Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		Made.Character = World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Made.Mode || !State || !Controller || !Made.Character)
		{
			return Made;
		}
		Controller->SetPlayerState(State);
		Controller->Possess(Made.Character);
		Made.AbilitySystem = Cast<UCataclysmAbilitySystemComponent>(
			Made.Character->GetAbilitySystemComponent());

		Made.Mode->DungeonModifiers = {};
		Made.Mode->FloorNumber = 1;
		if (!Test.TestNotNull(TEXT("a floor without the row was built"), Made.Mode->BuildFloor()))
		{
			Made.AbilitySystem = nullptr;
			return Made;
		}
		Made.Mode->ApplyFloorRulesToPlayer();
		return Made;
	}

	/** Down to the next floor, which carries `Row`. */
	bool OntoAFloorCarrying(FAutomationTestBase& Test, const FFloor& Floor, const TCHAR* Row)
	{
		Floor.Mode->DungeonModifiers = {FName(Row)};
		return Test.TestTrue(*FString::Printf(TEXT("the next floor, carrying %s, was reached"), Row),
							 Floor.Mode->GoToFloor(2));
	}

	/** A creature of this rung dies, credited to the player. */
	void Kill(UWorld* World, int32 RarityStep)
	{
		if (ACataclysmEnemyCharacter* Victim = World->SpawnActor<ACataclysmEnemyCharacter>(
				FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator))
		{
			Victim->SetRarityStep(RarityStep);
			Victim->HandleDeath();
		}
	}

	/** Every slot full, and no heal running: the state before a drink is read. */
	void Ready(const FFloor& Floor)
	{
		for (int32 Slot = 0; Slot < Potions::SlotCount; ++Slot)
		{
			Floor.AbilitySystem->SetPotionCharges(Slot, Potions::MaxCharges);
		}
		Floor.AbilitySystem->SetPotionHeal(FCataclysmPotionHeal());
	}

	/** Drink slot 1 and say how much the drink is worth. -1 if it was refused. */
	float DrinkWorth(const FFloor& Floor)
	{
		Ready(Floor);
		return Potions::Drink(Floor.Character, 0) == ECataclysmPotionRefusal::None
			? Floor.AbilitySystem->GetPotionHeal().Remaining
			: -1.0f;
	}

	float MaxHealthOf(const FFloor& Floor)
	{
		return Floor.AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	}
}

#define CATACLYSM_POTION_FAMINE_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_POTION_FAMINE_TEST(FCataclysmHardModePotionTest,
	"Cataclysm.PotionFamine.HardModeRefusesEveryDrinkAndTheSlotsStillFill")
{
	// "Players cannot use potions in this dungeon."
	using namespace CataclysmPotionFamineTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FFloor Floor = Make(*this, World);
	if (!TestTrue(TEXT("a game mode and a possessed player"), Floor.IsUsable()))
	{
		return false;
	}

	TestTrue(TEXT("on a floor without the row, a potion is drunk"), DrinkWorth(Floor) > 0.0f);

	if (!OntoAFloorCarrying(*this, Floor, Effects::HardModeKey))
	{
		return false;
	}
	Ready(Floor);
	Floor.AbilitySystem->SetPotionCharges(0, 20.0f);
	TestEqual(TEXT("on a floor carrying it, the drink is refused"),
			  Potions::Drink(Floor.Character, 0), ECataclysmPotionRefusal::Forbidden);
	TestEqual(TEXT("and spends nothing"), Floor.AbilitySystem->GetPotionCharges(0), 20.0f,
			  0.001f);
	TestTrue(TEXT("and the boxes are drawn crossed out"), Potions::AreForbiddenFor(Floor.Character));

	Kill(World, 0);
	TestEqual(TEXT("a kill still fills the slot"), Floor.AbilitySystem->GetPotionCharges(0), 21.0f,
			  0.001f);
	return true;
}

CATACLYSM_POTION_FAMINE_TEST(FCataclysmRecessionPotionTest,
	"Cataclysm.PotionFamine.RecessionMakesAKillAddAQuarterOfItsCharges")
{
	// "Potions take 4x as many kills to fill."
	using namespace CataclysmPotionFamineTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FFloor Floor = Make(*this, World);
	if (!TestTrue(TEXT("a game mode and a possessed player"), Floor.IsUsable()))
	{
		return false;
	}

	Floor.AbilitySystem->SetPotionCharges(0, 0.0f);
	Kill(World, 1);
	TestEqual(TEXT("on a floor without the row, an Elite kill adds 3.5"),
			  Floor.AbilitySystem->GetPotionCharges(0), 3.5f, 0.001f);

	if (!OntoAFloorCarrying(*this, Floor, Effects::RecessionKey))
	{
		return false;
	}
	Floor.AbilitySystem->SetPotionCharges(0, 0.0f);
	Kill(World, 1);
	TestEqual(TEXT("on a floor carrying it, a quarter of that: 0.875"),
			  Floor.AbilitySystem->GetPotionCharges(0), 0.875f, 0.001f);
	for (int32 More = 0; More < 3; ++More)
	{
		Kill(World, 1);
	}
	TestEqual(TEXT("so four kills add what one did"), Floor.AbilitySystem->GetPotionCharges(0),
			  3.5f, 0.001f);
	return true;
}

CATACLYSM_POTION_FAMINE_TEST(FCataclysmDiminishingReturnsPotionTest,
	"Cataclysm.PotionFamine.DiminishingReturnsTakesTenPercentOfAFullHealOffEachDrinkDownToThirty")
{
	// "Potions lose effectiveness over time", read per drink: each drink already
	// taken in the dungeon takes 10% of a full heal off the next, down to 30%.
	using namespace CataclysmPotionFamineTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FFloor Floor = Make(*this, World);
	if (!TestTrue(TEXT("a game mode and a possessed player"), Floor.IsUsable()))
	{
		return false;
	}
	const float Full = MaxHealthOf(Floor) * 0.35f;
	if (!TestTrue(TEXT("set-up: the player has a maximum to heal a share of"), Full > 0.0f))
	{
		return false;
	}

	Floor.AbilitySystem->SetPotionsDrunk(0);
	TestEqual(TEXT("without the row, a first drink is a full heal"), DrinkWorth(Floor), Full, 0.01f);
	TestEqual(TEXT("and so is a second"), DrinkWorth(Floor), Full, 0.01f);

	if (!OntoAFloorCarrying(*this, Floor, Effects::DiminishingReturnsKey))
	{
		return false;
	}
	Floor.AbilitySystem->SetPotionsDrunk(0);
	TestEqual(TEXT("with it, a first drink is still a full heal"), DrinkWorth(Floor), Full, 0.01f);
	TestEqual(TEXT("the second is 90% of one"), DrinkWorth(Floor), Full * 0.9f, 0.01f);
	TestEqual(TEXT("the third 80%"), DrinkWorth(Floor), Full * 0.8f, 0.01f);
	for (int32 More = 0; More < 4; ++More)
	{
		DrinkWorth(Floor);
	}
	TestEqual(TEXT("the eighth has reached 30%"), DrinkWorth(Floor), Full * 0.3f, 0.01f);
	TestEqual(TEXT("and the ninth goes no lower"), DrinkWorth(Floor), Full * 0.3f, 0.01f);

	Potions::RefillAll(Floor.Character);
	TestEqual(TEXT("entering a dungeon starts the count again: a full heal"), DrinkWorth(Floor),
			  Full, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
