// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"

/**
 * The record of Fervour spent (the Death trees' slice C3a, 2026-11-25): `UCataclysmAbilitySystemComponent::
 * NoteFervourSpent` stamps the clock `seconds_after_fervour_spent` and raises `fervour_spent` with the amount, where a
 * skill pays its Fervour cost. A row on the event with an Event Value of N acts only on a spend of N or more at once.
 */

namespace CataclysmFervourSpentTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;
	using CataclysmTestWorld::RunClock;

	/** An actor holding health and a Fervour pool of 100, able to be given stat lines. */
	struct FSpentHolder
	{
		explicit FSpentHolder(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource = NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewResource);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												   100.0f);
		}

		~FSpentHolder()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	/** The Death damage type's name, made when asked rather than at file scope. */
	FName SpentDeathType()
	{
		return FName(TEXT("Death"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourSpentClockTest,
	"Cataclysm.FervourSpent.ASpendStampsTheClockAndAChargeSpentDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A spend of Fervour stamps the clock; a next-use charge spent, which raises `resource_consumed` with nothing, does not,
 * and neither does a spend of nothing. THE CONTROLS are those two.
 */
bool FCataclysmFervourSpentClockTest::RunTest(const FString&)
{
	using namespace CataclysmFervourSpentTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSpentHolder Holder(World);

	TestEqual(TEXT("never spent reads never"), Holder.AbilitySystem->SecondsSinceFervourSpent(), -1.0f);
	Holder.AbilitySystem->NoteResourceConsumed(0.0f, nullptr, 1);
	TestEqual(TEXT("control: a next-use charge spent stamps nothing"),
			  Holder.AbilitySystem->SecondsSinceFervourSpent(), -1.0f);
	Holder.AbilitySystem->NoteFervourSpent(0.0f);
	TestEqual(TEXT("control: and nor does a spend of nothing"), Holder.AbilitySystem->SecondsSinceFervourSpent(),
			  -1.0f);
	Holder.AbilitySystem->NoteFervourSpent(10.0f);
	TestEqual(TEXT("a spend of 10 stamps it now"), Holder.AbilitySystem->SecondsSinceFervourSpent(), 0.0f, 0.001f);
	RunClock(World, 1.0f);
	TestEqual(TEXT("and it reads a second later"), Holder.AbilitySystem->SecondsSinceFervourSpent(), 1.0f, 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourSpentWindowRowsTest,
	"Cataclysm.FervourSpent.BurntOfferingAndQuickKindlingHoldForFourSecondsAfterASpend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The real rows of Burnt Offering, "You take 1% reduced damage per point for 4 seconds after you spend Fervour", and
 * Quick Kindling, "+1% increased Cooldown Reduction per point for 4 seconds after you spend Fervour", eight points
 * each: 8% less damage taken and 8 Cooldown Reduction for 4 seconds after a spend. THE CONTROL: before any spend, and
 * after the 4 seconds.
 */
bool FCataclysmFervourSpentWindowRowsTest::RunTest(const FString&)
{
	using namespace CataclysmFervourSpentTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSpentHolder Holder(World);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("SoulCollector_basic_d_b0")), 8);
	Allocation.Add(FName(TEXT("SoulCollector_basic_d_stem2")), 8);
	const TMap<FName, TArray<FCataclysmStatModifier>> Rows = UCataclysmPassiveTree::ModifiersFor(
		Allocation, UCataclysmPassiveTree::LoadNodeTable(), UCataclysmPassiveTree::LoadEffectTable(),
		{SpentDeathType()});
	const FName Taken(TEXT("damage_taken"));
	const FName Reduction(TEXT("cooldown_reduction"));
	if (!TestTrue(TEXT("set-up: Burnt Offering's row"), Rows.Contains(Taken))
		|| !TestTrue(TEXT("set-up: Quick Kindling's row"), Rows.Contains(Reduction)))
	{
		return false;
	}
	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.FindOrAdd(Taken).Base = 100.0f;
	Lines.FindOrAdd(Taken).Modifiers = Rows[Taken];
	Lines.FindOrAdd(Reduction).Base = 0.0f;
	Lines.FindOrAdd(Reduction).Modifiers = Rows[Reduction];
	Holder.AbilitySystem->SetStatInputs(MoveTemp(Lines));
	const FGameplayTagContainer NoTags;

	TestEqual(TEXT("control: before any spend, damage taken is the base"),
			  Holder.AbilitySystem->StatForSkill(Taken, NoTags, 0.0f), 100.0f, 0.01f);
	TestEqual(TEXT("control: and no Cooldown Reduction"), Holder.AbilitySystem->StatForSkill(Reduction, NoTags, 0.0f),
			  0.0f, 0.01f);
	Holder.AbilitySystem->NoteFervourSpent(5.0f);
	TestEqual(TEXT("just after a spend, 8% less damage taken"),
			  Holder.AbilitySystem->StatForSkill(Taken, NoTags, 0.0f), 92.0f, 0.01f);
	TestEqual(TEXT("and 8 Cooldown Reduction"), Holder.AbilitySystem->StatForSkill(Reduction, NoTags, 0.0f), 8.0f,
			  0.01f);
	RunClock(World, 3.8f);
	TestEqual(TEXT("still at 3.8 seconds"), Holder.AbilitySystem->StatForSkill(Taken, NoTags, 0.0f), 92.0f, 0.01f);
	RunClock(World, 0.4f);
	TestEqual(TEXT("control: gone at 4.2 seconds"), Holder.AbilitySystem->StatForSkill(Taken, NoTags, 0.0f), 100.0f,
			  0.01f);
	TestEqual(TEXT("control: Cooldown Reduction too"), Holder.AbilitySystem->StatForSkill(Reduction, NoTags, 0.0f),
			  0.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourSpentAtOnceTest,
	"Cataclysm.FervourSpent.SpentBreathsRealRowsWantTenOrMoreSpentAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Spent Breath, six points: "+3% increased damage per point for 4 seconds after you spend 10 or more Fervour at once".
 * Its real rows reach a real level 10 Death character through the port's copy and builder; a spend of 5 grants
 * nothing, a spend of 10 grants the window, and it lapses 4 seconds later. THE CONTROL is the spend of 5.
 */
bool FCataclysmFervourSpentAtOnceTest::RunTest(const FString&)
{
	using namespace CataclysmFervourSpentTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* SpawnedState = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!SpawnedState || !Controller || !Character)
	{
		AddError(TEXT("A possessed player could not be built."));
		return false;
	}
	Controller->SetPlayerState(SpawnedState);
	Controller->Possess(Character);
	ACataclysmPlayerState* State = Character->GetPlayerState<ACataclysmPlayerState>();
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	UCataclysmAbilitySystemComponent* System = State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("set-up: the player state"), State) || !TestNotNull(TEXT("set-up: equipment"), Equipment)
		|| !TestNotNull(TEXT("set-up: the ability system"), System))
	{
		return false;
	}
	State->SetCreationChoice(FName(TEXT("Dagger")), SpentDeathType());
	State->SetLevelAndExperience(10, 0);
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("SoulCollector_basic_spine_000")), 1);
	Allocation.Add(FName(TEXT("SoulCollector_basic_d_stem0")), 6);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(System);

	const FName StackKey(TEXT("Passive_SoulCollector_basic_d_stem0:attack_damage"));
	bool bGranting = false;
	for (const FCataclysmPoolAction& Action : System->GetPoolActions())
	{
		if (Action.StackKey == StackKey)
		{
			bGranting = true;
			TestEqual(TEXT("granted on a spend of Fervour"), Action.Event, FName(TEXT("fervour_spent")));
			TestEqual(TEXT("of 10 or more at once"), Action.EventValue, 10.0f, 0.001f);
		}
	}
	if (!TestTrue(TEXT("Spent Breath's rows are handed over as an action under its key"), bGranting))
	{
		return false;
	}

	System->NoteFervourSpent(5.0f);
	TestEqual(TEXT("control: a spend of 5 grants nothing"), System->OwnStacksHeld(StackKey), 0);
	System->NoteFervourSpent(10.0f);
	TestEqual(TEXT("a spend of 10 at once grants the window"), System->OwnStacksHeld(StackKey), 1);
	RunClock(World, 4.5f);
	TestEqual(TEXT("and it lapses 4 seconds later"), System->OwnStacksHeld(StackKey), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
