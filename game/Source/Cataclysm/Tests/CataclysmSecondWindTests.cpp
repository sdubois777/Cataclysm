// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * SECOND WIND. The Ravager tree rework of 2026-11-24.
 *
 * `Ravager_basic_a_b2` Second Wind now reads "Each time your Fervour becomes full, restore 2% of your maximum health
 * per point." Its row is `health_restored_on_fervour_full`, flat 2 a point, 16 at its 8 points. The heal is made on
 * the CROSSING `ACataclysmPlayerCharacter::OnClassResourceChanged` already stamps for `seconds_after_resource_full`,
 * so a pool that sits at its maximum restores nothing more and a pool that empties and fills again restores again.
 *
 * THE CHARACTER IS BUILT THE WAY `CataclysmPlayerMovementTests.cpp` BUILDS ITS: a player state holding the ability
 * system, a pawn given that state, `OnRep_PlayerState` binding the delegates. The pool moves by
 * `ApplyModToAttribute`, the write every gain of Fervour makes, so the delegate fires as it fires in play.
 *
 * WRITTEN WITHOUT A BUILD, from the source, on 2026-11-24.
 */
namespace CataclysmSecondWindTest
{
	/** A player at half of a thousand health with an empty pool of a hundred, holding `Percent` of the node. */
	struct FSecondWindPlayer
	{
		FSecondWindPlayer(UWorld* World, float Percent)
		{
			State = World->SpawnActor<ACataclysmPlayerState>();
			AbilitySystem = State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
			if (!AbilitySystem)
			{
				return;
			}
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 500.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);

			TMap<FName, FCataclysmStatInputs> Inputs;
			if (Percent > 0.0f)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::PassiveKeystone;
				Row.Value = Percent;
				Inputs.FindOrAdd(FName(UCataclysmFervour::HealthRestoredOnFullStat)).Modifiers = {Row};
			}
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));

			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (Character)
			{
				Character->SetPlayerState(State);
				Character->OnRep_PlayerState();
			}
		}

		bool IsComplete() const { return State && AbilitySystem && Character; }

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		void MovePool(float By) const
		{
			AbilitySystem->ApplyModToAttribute(
				UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), EGameplayModOp::Additive, By);
		}

		ACataclysmPlayerState* State = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
	};
}

#define CATACLYSM_SECOND_WIND_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------------------------------------------------
// SW1. Filling the bar restores the share, each time it fills and not while it sits full.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Eight points, 16%: 500 of 1,000 becomes 660 when the pool fills. Falling to 60 restores nothing; filling again
 * restores another 160, to 820. Prints "SECOND WIND".
 */
CATACLYSM_SECOND_WIND_TEST(FCataclysmSecondWindRestoresOnFillingTest,
	"Cataclysm.SecondWind.EachTimeTheBarFillsItRestoresItsShareOfMaximumHealth")
{
	using namespace CataclysmSecondWindTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FSecondWindPlayer Player(World, /*Percent=*/16.0f);
	if (!TestTrue(TEXT("set-up: a player state, an ability system and a pawn"), Player.IsComplete()))
	{
		return false;
	}

	const float Before = Player.Health();
	Player.MovePool(50.0f);
	const float HalfFull = Player.Health();
	Player.MovePool(50.0f);
	const float FirstFill = Player.Health();
	Player.MovePool(-40.0f);
	const float Fallen = Player.Health();
	Player.MovePool(40.0f);
	const float SecondFill = Player.Health();
	AddInfo(FString::Printf(TEXT("SECOND WIND: before %.1f, half full %.1f, filled %.1f, fallen %.1f, filled again %.1f"),
							Before, HalfFull, FirstFill, Fallen, SecondFill));

	TestEqual(TEXT("set-up: half of a thousand"), Before, 500.0f, 0.01f);
	TestEqual(TEXT("a half-full bar restores nothing"), HalfFull, 500.0f, 0.01f);
	TestEqual(TEXT("**filling the bar restores 16% of the maximum: 660**"), FirstFill, 660.0f, 0.01f);
	TestEqual(TEXT("falling from full restores nothing"), Fallen, 660.0f, 0.01f);
	TestEqual(TEXT("**filling it again restores another 160: 820**"), SecondFill, 820.0f, 0.01f);
	TestEqual(TEXT("and the full window opened with it, now"), Player.AbilitySystem->SecondsSinceClassResourceFull(),
			  0.0f, 0.001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// SW2. Without the node a full bar restores nothing.
// --------------------------------------------------------------------------------------------------------------------
/** The control for SW1: the same crossing, which still opens its window, and no heal. Prints "SECOND WIND CONTROL". */
CATACLYSM_SECOND_WIND_TEST(FCataclysmSecondWindControlTest,
	"Cataclysm.SecondWind.WithoutTheNodeAFullBarRestoresNothing")
{
	using namespace CataclysmSecondWindTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FSecondWindPlayer Player(World, /*Percent=*/0.0f);
	if (!TestTrue(TEXT("set-up: a player state, an ability system and a pawn"), Player.IsComplete()))
	{
		return false;
	}

	Player.MovePool(100.0f);
	AddInfo(FString::Printf(TEXT("SECOND WIND CONTROL: %.1f, window %.3f"), Player.Health(),
							Player.AbilitySystem->SecondsSinceClassResourceFull()));
	TestEqual(TEXT("set-up: the crossing happened, so the window opened"),
			  Player.AbilitySystem->SecondsSinceClassResourceFull(), 0.0f, 0.001f);
	TestEqual(TEXT("**and nothing was restored**"), Player.Health(), 500.0f, 0.01f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// SW3. The restoration itself: nothing at full health, nothing with no ability system, and the stat is recorded.
// --------------------------------------------------------------------------------------------------------------------
/** A player's health is not taken to nought here: that would run the player's own death. */
CATACLYSM_SECOND_WIND_TEST(FCataclysmSecondWindRefusalsTest,
	"Cataclysm.SecondWind.TheRestorationRefusesAFullCharacter")
{
	using namespace CataclysmSecondWindTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FSecondWindPlayer Player(World, /*Percent=*/16.0f);
	if (!TestTrue(TEXT("set-up: a player state, an ability system and a pawn"), Player.IsComplete()))
	{
		return false;
	}

	Player.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
	TestEqual(TEXT("at full health nothing is restored"),
			  UCataclysmFervour::RestoreHealthOnFervourFull(Player.AbilitySystem), 0.0f, 0.001f);
	Player.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 900.0f);
	TestEqual(TEXT("CONTROL: a hundred short, the call restores the hundred that fits and no more"),
			  UCataclysmFervour::RestoreHealthOnFervourFull(Player.AbilitySystem), 100.0f, 0.01f);
	TestEqual(TEXT("no ability system restores nothing"), UCataclysmFervour::RestoreHealthOnFervourFull(nullptr), 0.0f);
	TestTrue(TEXT("and the stat is on the exemption list"),
			 UCataclysmPlayerClassStats::StatsWithNoAttribute().Contains(
				 FString(UCataclysmFervour::HealthRestoredOnFullStat)));
	return true;
}

#undef CATACLYSM_SECOND_WIND_TEST

#endif // WITH_AUTOMATION_TESTS
