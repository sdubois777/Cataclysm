// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
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
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect.h"
#include "Misc/ScopeExit.h"

/**
 * Rows sized by what one spend of Fervour carried (the Death trees' slice C3b, 2026-11-25). The Fraction Of values
 * `for_each_of_event_amount` and `for_each_ten_of_event_amount` make `UCataclysmAbilitySystemComponent::ActOnEvent`
 * multiply a row's value by the whole units, or whole tens, of its event's amount before any action reads it: Feast,
 * Spirit to Mana, Ashen Ward, Soul Furnace and Turn of the Wheel. And Bound Offering: a share of each reserve held,
 * through `UCataclysmCommand::HoldReserve`, counts as a spend.
 *
 * THE MECHANISM IS READ ON ACTIONS BUILT BY HAND, as the item would build them, so each figure is exact; one test reads
 * the five real rows off a real Soul Collector to show the data reaches that shape.
 */

namespace CataclysmSpendSizedTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	constexpr float Metre = 100.0f;

	/** A bare actor holding the vital pools, each with a maximum of a thousand. */
	struct FSizedHolder
	{
		explicit FSizedHolder(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// Raw pointers on purpose: AddAttributeSetSubobject is a template and a TObjectPtr deduces the wrapper.
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 1000.0f);
		}

		~FSizedHolder()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	/** An action on `fervour_spent`, as the item would build it from a row. */
	FCataclysmPoolAction OnASpend(const TCHAR* Pool, float Percent, ECataclysmPoolActionBase Base)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("fervour_spent"));
		Action.Pool = FName(Pool);
		Action.Percent = Percent;
		Action.Base = Base;
		Action.TriggerKey = FName(*FString::Printf(TEXT("A_row:%s"), Pool));
		return Action;
	}

	/** The seconds left on the longest cooldown running on a slot's own key, nought when none runs. */
	float SecondsLeftOnSlot(const UCataclysmAbilitySystemComponent* System, ECataclysmAbilitySlot Slot)
	{
		const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(Slot);
		float Most = 0.0f;
		for (const float Left : System->GetActiveEffectsTimeRemaining(
				 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag))))
		{
			Most = FMath::Max(Most, Left);
		}
		return Most;
	}

	/** A player pawn with its ability system on a player state, the shape `CataclysmNearbyActionTests.cpp` uses. */
	ACataclysmPlayerCharacter* SpawnSizedPlayer(UWorld* World)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Actor =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (State && Actor)
		{
			Actor->SetPlayerState(State);
			Actor->OnRep_PlayerState();
			return Actor;
		}
		return nullptr;
	}

	/** A creature of the monsters' side with a thousand health. */
	ACataclysmEnemyCharacter* SpawnSizedEnemy(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Actor = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Actor)
		{
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Actor->SetHealth(1000.0f);
		}
		return Actor;
	}

	float HealthOfSized(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpendSizedTimesTest,
	"Cataclysm.SpendSized.ARowActsForEachWholeFervourOrEachWholeTen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `TimesSizedByEventAmount`: whole units and whole tens, rounded down, with the allowance that reads 2.9999998 as three.
 * A spend of 19 pays once, the coordinating session's ruling 3 of 2026-11-25. THE CONTROL: every other base acts once.
 */
bool FCataclysmSpendSizedTimesTest::RunTest(const FString&)
{
	using EBase = ECataclysmPoolActionBase;
	const auto Times = &UCataclysmAbilitySystemComponent::TimesSizedByEventAmount;
	TestEqual(TEXT("for each: a spend of 50 is fifty"), Times(EBase::ForEachOfEventAmount, 50.0f), 50.0f);
	TestEqual(TEXT("for each: 3.75 is three"), Times(EBase::ForEachOfEventAmount, 3.75f), 3.0f);
	TestEqual(TEXT("for each: 2.9999998 is three"), Times(EBase::ForEachOfEventAmount, 2.9999998f), 3.0f);
	TestEqual(TEXT("for each ten: 19 is one"), Times(EBase::ForEachTenOfEventAmount, 19.0f), 1.0f);
	TestEqual(TEXT("for each ten: 20 is two"), Times(EBase::ForEachTenOfEventAmount, 20.0f), 2.0f);
	TestEqual(TEXT("for each ten: 29.999998 is three"), Times(EBase::ForEachTenOfEventAmount, 29.999998f), 3.0f);
	TestEqual(TEXT("for each ten: 9 is none"), Times(EBase::ForEachTenOfEventAmount, 9.0f), 0.0f);
	TestEqual(TEXT("control: the maximum acts once"), Times(EBase::Maximum, 50.0f), 1.0f);
	TestEqual(TEXT("control: the event's amount acts once"), Times(EBase::EventAmount, 50.0f), 1.0f);
	TestEqual(TEXT("control: what is held acts once, even on nothing"), Times(EBase::Current, 0.0f), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpendSizedPoolsTest,
	"Cataclysm.SpendSized.FeastSpiritToManaAndAshenWardRestoreWhatASpendSizes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * By hand, at Feast's 1 and six points of Spirit to Mana and of Ashen Ward: 1% of maximum health for each Fervour spent,
 * 6% of maximum mana and of maximum energy shield for every 10. Each maximum is a thousand. A spend of 19 heals 190 and
 * restores 60 of each; a spend of 9 heals 90 and restores none (THE CONTROL, no whole ten); a spend of 30 heals 300 and
 * restores 180 of each.
 */
bool FCataclysmSpendSizedPoolsTest::RunTest(const FString&)
{
	using namespace CataclysmSpendSizedTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSizedHolder Holder(World);
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute Mana = UCataclysmVitalAttributeSet::GetManaAttribute();
	const FGameplayAttribute Shield = UCataclysmVitalAttributeSet::GetEnergyShieldAttribute();
	Holder.Set(Health, 100.0f);
	Holder.Set(Mana, 0.0f);
	Holder.Set(Shield, 0.0f);
	Holder.AbilitySystem->SetPoolActions({OnASpend(TEXT("health"), 1.0f, ECataclysmPoolActionBase::ForEachOfEventAmount),
		OnASpend(TEXT("mana"), 6.0f, ECataclysmPoolActionBase::ForEachTenOfEventAmount),
		OnASpend(TEXT("energy_shield"), 6.0f, ECataclysmPoolActionBase::ForEachTenOfEventAmount)});

	Holder.AbilitySystem->NoteFervourSpent(19.0f);
	TestEqual(TEXT("a spend of 19 heals 19% of a thousand"), Holder.Get(Health), 290.0f, 0.01f);
	TestEqual(TEXT("and restores 6% of the mana once: 19 is one ten"), Holder.Get(Mana), 60.0f, 0.01f);
	TestEqual(TEXT("and 6% of the shield once"), Holder.Get(Shield), 60.0f, 0.01f);

	Holder.AbilitySystem->NoteFervourSpent(9.0f);
	TestEqual(TEXT("a spend of 9 heals 9%"), Holder.Get(Health), 380.0f, 0.01f);
	TestEqual(TEXT("control: and restores no mana, with no whole ten in it"), Holder.Get(Mana), 60.0f, 0.01f);
	TestEqual(TEXT("control: nor any shield"), Holder.Get(Shield), 60.0f, 0.01f);

	Holder.AbilitySystem->NoteFervourSpent(30.0f);
	TestEqual(TEXT("a spend of 30 heals 30%"), Holder.Get(Health), 680.0f, 0.01f);
	TestEqual(TEXT("and restores 6% of the mana three times"), Holder.Get(Mana), 240.0f, 0.01f);
	TestEqual(TEXT("and of the shield three times"), Holder.Get(Shield), 240.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpendSizedSmiteTest,
	"Cataclysm.SpendSized.SoulFurnaceSmitesForEachFervourFromTenSpentAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * By hand at Soul Furnace's figures: a smite of 10% of weapon damage for each Fervour spent, on a spend of 10 or more at
 * once, on every enemy within five metres. A spend of 9 strikes nothing (THE CONTROL, under the ten); a spend of 40
 * strikes the near enemy exactly as an unsized smite of 400% does (THE SECOND CONTROL, which sizes the first); the enemy
 * eight metres off is never struck. Critical strikes are pinned off, so the two blows are the same blow.
 */
bool FCataclysmSpendSizedSmiteTest::RunTest(const FString&)
{
	using namespace CataclysmSpendSizedTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	ACataclysmPlayerCharacter* Player = SpawnSizedPlayer(World);
	ACataclysmEnemyCharacter* Near = SpawnSizedEnemy(World, FVector(3.0f * Metre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Far = SpawnSizedEnemy(World, FVector(8.0f * Metre, 0.0f, 0.0f));
	UCataclysmAbilitySystemComponent* System =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Player));
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: an enemy within five metres"), Near)
		|| !TestNotNull(TEXT("set-up: an enemy beyond them"), Far)
		|| !TestNotNull(TEXT("set-up: the player's ability system"), System))
	{
		return false;
	}
	System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 20.0f);

	FCataclysmPoolAction Furnace = OnASpend(UCataclysmAbilitySystemComponent::SmiteNearbyAction, 10.0f,
		ECataclysmPoolActionBase::ForEachOfEventAmount);
	Furnace.Nearby = ECataclysmNearbyAction::Smite;
	Furnace.EventValue = 10.0f;
	System->SetPoolActions({Furnace});
	const float FarBefore = HealthOfSized(Far);

	const float BeforeNine = HealthOfSized(Near);
	System->NoteFervourSpent(9.0f);
	TestEqual(TEXT("control: a spend of 9 strikes nothing"), HealthOfSized(Near), BeforeNine, 0.01f);

	const float BeforeForty = HealthOfSized(Near);
	System->NoteFervourSpent(40.0f);
	const float SizedBlow = BeforeForty - HealthOfSized(Near);
	if (!TestTrue(*FString::Printf(TEXT("a spend of 40 strikes the near enemy: %.2f"), SizedBlow), SizedBlow > 0.0f))
	{
		return false;
	}

	FCataclysmPoolAction Plain = OnASpend(UCataclysmAbilitySystemComponent::SmiteNearbyAction, 400.0f,
		ECataclysmPoolActionBase::Maximum);
	Plain.Nearby = ECataclysmNearbyAction::Smite;
	System->SetPoolActions({Plain});
	const float BeforePlain = HealthOfSized(Near);
	System->NoteFervourSpent(40.0f);
	TestEqual(TEXT("the second control: the sized blow is an unsized 400% smite's"),
			  SizedBlow, BeforePlain - HealthOfSized(Near), 0.01f);
	TestEqual(TEXT("and the enemy eight metres away was never struck"), HealthOfSized(Far), FarBefore, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpendSizedCooldownTest,
	"Cataclysm.SpendSized.TurnOfTheWheelCutsEveryCooldownButTheSpendersASecondForEachTen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * By hand at Turn of the Wheel's figure: 1 second off the cooldowns of the skills that did not spend, for every 10
 * Fervour spent. Three cooldowns of 10 seconds run, the heavy attack's, the special's and the Ultimate's. The Ultimate
 * spends 25: the other two lose 2 seconds and the Ultimate's keeps its 10. THE CONTROL: a spend of 9 from it cuts
 * nothing. THE JUDGEMENT: a spend naming no skill, 10 held back for a minion under Bound Offering, cuts every one.
 */
bool FCataclysmSpendSizedCooldownTest::RunTest(const FString&)
{
	using namespace CataclysmSpendSizedTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSizedHolder Holder(World);
	UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;
	for (const ECataclysmAbilitySlot Slot :
		 {ECataclysmAbilitySlot::Heavy, ECataclysmAbilitySlot::Special, ECataclysmAbilitySlot::Ultimate})
	{
		UCataclysmGameplayAbility::ApplyCooldownEffect(System, UCataclysmSkillSlots::CooldownTag(Slot), 10.0f);
	}
	if (!TestEqual(TEXT("set-up: the heavy attack waits 10"),
			SecondsLeftOnSlot(System, ECataclysmAbilitySlot::Heavy), 10.0f, 0.01f)
		|| !TestEqual(TEXT("set-up: the Ultimate waits 10"),
			SecondsLeftOnSlot(System, ECataclysmAbilitySlot::Ultimate), 10.0f, 0.01f))
	{
		return false;
	}

	FCataclysmPoolAction Wheel = OnASpend(UCataclysmAbilitySystemComponent::CooldownReduceOthersAction, 1.0f,
		ECataclysmPoolActionBase::ForEachTenOfEventAmount);
	Wheel.CooldownReduce = ECataclysmCooldownReset::Others;
	Wheel.ResetKey = Wheel.TriggerKey;
	System->SetPoolActions({Wheel});
	FGameplayTagContainer TheUltimate;
	TheUltimate.AddTag(CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::Ultimate));

	System->NoteFervourSpent(9.0f, &TheUltimate);
	TestEqual(TEXT("control: a spend of 9 cuts nothing"),
			  SecondsLeftOnSlot(System, ECataclysmAbilitySlot::Heavy), 10.0f, 0.01f);

	System->NoteFervourSpent(25.0f, &TheUltimate);
	TestEqual(TEXT("the Ultimate spends 25: the heavy attack loses 2 seconds"),
			  SecondsLeftOnSlot(System, ECataclysmAbilitySlot::Heavy), 8.0f, 0.01f);
	TestEqual(TEXT("and the special 2"), SecondsLeftOnSlot(System, ECataclysmAbilitySlot::Special), 8.0f, 0.01f);
	TestEqual(TEXT("and the Ultimate, which spent it, none"),
			  SecondsLeftOnSlot(System, ECataclysmAbilitySlot::Ultimate), 10.0f, 0.01f);

	System->NoteFervourSpent(10.0f);
	TestEqual(TEXT("the judgement: a spend naming no skill cuts the heavy attack's"),
			  SecondsLeftOnSlot(System, ECataclysmAbilitySlot::Heavy), 7.0f, 0.01f);
	TestEqual(TEXT("and the Ultimate's, since none of them spent it"),
			  SecondsLeftOnSlot(System, ECataclysmAbilitySlot::Ultimate), 9.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpendSizedReserveTest,
	"Cataclysm.SpendSized.BoundOfferingCountsAShareOfEachReserveHeldAsASpend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Bound Offering at six points, 90%: holding back 20 for a follower counts as a spend of 18, which a Feast-like row
 * reads as 18% of maximum health. Nothing is taken from the pool. THE CONTROLS: the same hold before the stat is granted
 * raises no spend, and a hold of nothing raises none with it.
 */
bool FCataclysmSpendSizedReserveTest::RunTest(const FString&)
{
	using namespace CataclysmSpendSizedTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSizedHolder Holder(World);
	ACataclysmEnemyCharacter* Follower = SpawnSizedEnemy(World, FVector(3.0f * Metre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a follower"), Follower))
	{
		return false;
	}
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	Holder.Set(Health, 100.0f);
	Holder.AbilitySystem->SetPoolActions(
		{OnASpend(TEXT("health"), 1.0f, ECataclysmPoolActionBase::ForEachOfEventAmount)});

	UCataclysmCommand::HoldReserve(Holder.Actor, Follower, 20.0f);
	TestEqual(TEXT("control: with no Bound Offering a hold is no spend"),
			  Holder.AbilitySystem->SecondsSinceFervourSpent(), -1.0f);
	TestEqual(TEXT("control: and heals nothing"), Holder.Get(Health), 100.0f, 0.01f);
	TestEqual(TEXT("the reserve is held all the same"), Follower->ReservedFervour, 20.0f, 0.001f);

	FCataclysmStatModifier Offering;
	Offering.Bucket = ECataclysmStatBucket::Flat;
	Offering.Source = ECataclysmModifierSource::PassiveKeystone;
	Offering.Value = 90.0f;
	TMap<FName, FCataclysmStatInputs> Lines;
	FCataclysmStatInputs& Line = Lines.FindOrAdd(FName(UCataclysmCommand::ReservedFervourSpentPercentStat));
	Line.Base = 0.0f;
	Line.Modifiers = {Offering};
	Holder.AbilitySystem->SetStatInputs(MoveTemp(Lines));

	UCataclysmCommand::HoldReserve(Holder.Actor, Follower, 0.0f);
	TestEqual(TEXT("control: a hold of nothing is no spend"), Holder.AbilitySystem->SecondsSinceFervourSpent(), -1.0f);

	UCataclysmCommand::HoldReserve(Holder.Actor, Follower, 20.0f);
	TestEqual(TEXT("holding back 20 at 90% is a spend now"), Holder.AbilitySystem->SecondsSinceFervourSpent(), 0.0f,
			  0.001f);
	TestEqual(TEXT("of 18, which heals 18% of a thousand"), Holder.Get(Health), 280.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpendSizedRealRowsTest,
	"Cataclysm.SpendSized.TheFiveRealRowsReachASoulCollectorSizedAsTheirSentencesSay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The five real rows reach a real level 30 Death character through the port's copy and builder: Feast and Soul Furnace
 * at one point, for each Fervour; Spirit to Mana and Ashen Ward at six, 6% for each ten; Turn of the Wheel at one, a
 * second for each ten off every skill but the spender's. Soul Furnace's Event Value is its ten at once. THE CONTROL: the
 * same character with only the tree's starting node has no action on a spend.
 */
bool FCataclysmSpendSizedRealRowsTest::RunTest(const FString&)
{
	using namespace CataclysmSpendSizedTest;

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
	State->SetCreationChoice(FName(TEXT("Dagger")), FName(TEXT("Death")));
	State->SetLevelAndExperience(30, 0);
	const FName Spent(TEXT("fervour_spent"));
	const auto OnASpendCount = [System, Spent]()
	{
		int32 Count = 0;
		for (const FCataclysmPoolAction& Action : System->GetPoolActions())
		{
			Count += Action.Event == Spent ? 1 : 0;
		}
		return Count;
	};

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("SoulCollector_basic_spine_000")), 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(System);
	TestEqual(TEXT("control: the starting node alone acts on no spend"), OnASpendCount(), 0);

	Allocation.Add(FName(TEXT("SoulCollector_keystone_d_kB")), 1);
	Allocation.Add(FName(TEXT("SoulCollector_basic_d_b1")), 6);
	Allocation.Add(FName(TEXT("SoulCollector_basic_d_b2")), 6);
	Allocation.Add(FName(TEXT("SoulCollector_keystone_d_kA")), 1);
	Allocation.Add(FName(TEXT("SoulCollector_keystone_d_kC")), 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(System);
	TestEqual(TEXT("the five act on a spend"), OnASpendCount(), 5);

	using EBase = ECataclysmPoolActionBase;
	for (const FCataclysmPoolAction& Action : System->GetPoolActions())
	{
		if (Action.Event != Spent)
		{
			continue;
		}
		if (Action.Nearby == ECataclysmNearbyAction::Smite)
		{
			TestEqual(TEXT("Soul Furnace: 10% for each Fervour"), Action.Percent, 10.0f, 0.001f);
			TestTrue(TEXT("Soul Furnace: sized for each"), Action.Base == EBase::ForEachOfEventAmount);
			TestEqual(TEXT("Soul Furnace: from 10 at once"), Action.EventValue, 10.0f, 0.001f);
		}
		else if (Action.CooldownReduce != ECataclysmCooldownReset::None)
		{
			TestTrue(TEXT("Turn of the Wheel: every skill but the spender's"),
					 Action.CooldownReduce == ECataclysmCooldownReset::Others);
			TestEqual(TEXT("Turn of the Wheel: a second"), Action.Percent, 1.0f, 0.001f);
			TestTrue(TEXT("Turn of the Wheel: for each ten"), Action.Base == EBase::ForEachTenOfEventAmount);
		}
		else if (Action.Pool == FName(TEXT("health")))
		{
			TestEqual(TEXT("Feast: 1% for each Fervour"), Action.Percent, 1.0f, 0.001f);
			TestTrue(TEXT("Feast: sized for each"), Action.Base == EBase::ForEachOfEventAmount);
		}
		else if (Action.Pool == FName(TEXT("mana")) || Action.Pool == FName(TEXT("energy_shield")))
		{
			TestEqual(*FString::Printf(TEXT("%s: 6%% at six points"), *Action.Pool.ToString()), Action.Percent, 6.0f,
					  0.001f);
			TestTrue(*FString::Printf(TEXT("%s: for each ten"), *Action.Pool.ToString()),
					 Action.Base == EBase::ForEachTenOfEventAmount);
		}
		else
		{
			AddError(FString::Printf(TEXT("An action on a spend that is none of the five: '%s'"),
									 *Action.Pool.ToString()));
		}
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
