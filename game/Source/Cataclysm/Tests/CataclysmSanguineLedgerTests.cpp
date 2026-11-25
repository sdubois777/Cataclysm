// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Sanguine Ledger, the Masochist's spine keystone, as the project owner approved
 * it on 2026-11-24: "Healing never takes your Fervour below 50."
 *
 * WHAT IT REPLACED. "Health regeneration no longer removes Fervour, but your
 * Health Regeneration is reduced by 50% (multiplicative)": a trade, and the
 * owner's rule of 2026-11-24 makes keystones upgrades by default. Its row is now
 * one flat 50 on `fervour_floor_against_healing`, which
 * `UCataclysmFervour::FloorAgainstHealing` reads where healing removes Fervour.
 *
 * THE HOLDER IS BUILT BY HAND, with the removal rate on its attribute and the
 * floor recorded the way `UCataclysmPlayerClassStats::ApplyTo` records a stat
 * with no attribute. The real row on a real Masochist is
 * `Cataclysm.MasochistHealingRows.SanguineLedgerKeepsARealMasochistsFervourAtFiftyThroughHealing`.
 */
namespace CataclysmSanguineLedgerTest
{
	using Resource = UCataclysmClassResourceAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	/** A Masochist's pools on a plain actor: 1,000 health, 100 Fervour, and the
	 *  starting node's removal rate of 1 Fervour per 1% healed. */
	struct FLedgerHolder
	{
		explicit FLedgerHolder(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// Raw pointers on purpose: AddAttributeSetSubobject is a template and
			// a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource =
				NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewResource);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(Resource::GetMaxClassResourceAttribute(), 100.0f);
			AbilitySystem->SetNumericAttributeBase(Resource::GetFervourLostToHealingAttribute(), 1.0f);
			AbilitySystem->SetNumericAttributeBase(Resource::GetFervourFromDamageAttribute(), 1.0f);
		}

		~FLedgerHolder()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** Sanguine Ledger's row: a flat 50 on the floor, with no tag. */
		void HoldTheLedger() const
		{
			FCataclysmStatModifier Floor;
			Floor.Bucket = ECataclysmStatBucket::Flat;
			Floor.Source = ECataclysmModifierSource::PassiveKeystone;
			Floor.Value = 50.0f;
			TMap<FName, FCataclysmStatInputs> Inputs;
			Inputs.FindOrAdd(FName(UCataclysmFervour::FloorAgainstHealingStat)).Modifiers = {Floor};
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		void SetFervour(float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Resource::GetClassResourceAttribute(), Value);
		}

		float Fervour() const
		{
			return AbilitySystem->GetNumericAttribute(Resource::GetClassResourceAttribute());
		}

		/** Heal this many points of the 1,000, as regeneration would: 1 Fervour a point of 10. */
		float Heal(float Health) const
		{
			FGameplayTagContainer Regeneration;
			Regeneration.AddTag(UCataclysmFervour::RegenerationTag());
			return UCataclysmFervour::RemoveForHealing(AbilitySystem, Health, Regeneration);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSanguineLedgerFloorTest,
	"Cataclysm.SanguineLedger.HealingNeverTakesTheFervourBelowFifty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Healing never takes your Fervour below 50." From 90, a heal of 300 (30% of
 * the 1,000) takes 30 and leaves 60, above the floor, as the class rule says;
 * the next heal of 300 would take the pool to 30 and stops at 50; and a third
 * takes nothing at all.
 */
bool FCataclysmSanguineLedgerFloorTest::RunTest(const FString&)
{
	using namespace CataclysmSanguineLedgerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FLedgerHolder Holder(World);
	Holder.HoldTheLedger();
	Holder.SetFervour(90.0f);
	TestEqual(TEXT("set-up: the floor is 50"),
			  UCataclysmFervour::FloorAgainstHealing(Holder.AbilitySystem, FGameplayTagContainer()), 50.0f, 0.001f);

	TestEqual(TEXT("above the floor the class rule holds: a 30% heal takes 30"), Holder.Heal(300.0f), -30.0f, 0.01f);
	TestEqual(TEXT("leaving 60"), Holder.Fervour(), 60.0f, 0.01f);

	TestEqual(TEXT("the next 30% heal stops at the floor: 10 taken, not 30"), Holder.Heal(300.0f), -10.0f, 0.01f);
	TestEqual(TEXT("leaving 50"), Holder.Fervour(), 50.0f, 0.01f);

	TestEqual(TEXT("and at the floor healing takes nothing"), Holder.Heal(300.0f), 0.0f, 0.01f);
	TestEqual(TEXT("so the pool is still 50"), Holder.Fervour(), 50.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSanguineLedgerBelowTheFloorTest,
	"Cataclysm.SanguineLedger.APoolAlreadyBelowFiftyLosesNothingToHealingAndStillFills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The floor keeps what is there and does not lift it. A pool at 20 loses
 * nothing to a heal and is not raised to 50; damage still fills it, since the
 * floor is about healing only.
 */
bool FCataclysmSanguineLedgerBelowTheFloorTest::RunTest(const FString&)
{
	using namespace CataclysmSanguineLedgerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FLedgerHolder Holder(World);
	Holder.HoldTheLedger();
	Holder.SetFervour(20.0f);

	TestEqual(TEXT("below the floor a heal takes nothing"), Holder.Heal(300.0f), 0.0f, 0.01f);
	TestEqual(TEXT("and does not lift the pool to the floor either"), Holder.Fervour(), 20.0f, 0.01f);

	TestEqual(TEXT("damage still fills it: 10% of health lost is 10 Fervour"),
			  UCataclysmFervour::GainFromDamage(Holder.AbilitySystem, 100.0f, FGameplayTagContainer()), 10.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSanguineLedgerControlTest,
	"Cataclysm.SanguineLedger.WithoutItHealingEmptiesThePool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** THE CONTROL: the same holder and heals with no floor, which is every
 *  character without the keystone. From 90, a heal of 900 takes it all. */
bool FCataclysmSanguineLedgerControlTest::RunTest(const FString&)
{
	using namespace CataclysmSanguineLedgerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FLedgerHolder Holder(World);
	Holder.SetFervour(90.0f);
	TestEqual(TEXT("no floor is recorded"),
			  UCataclysmFervour::FloorAgainstHealing(Holder.AbilitySystem, FGameplayTagContainer()), 0.0f, 0.001f);
	TestEqual(TEXT("a heal of 90% takes all 90"), Holder.Heal(900.0f), -90.0f, 0.01f);
	TestEqual(TEXT("and the pool is empty"), Holder.Fervour(), 0.0f, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
