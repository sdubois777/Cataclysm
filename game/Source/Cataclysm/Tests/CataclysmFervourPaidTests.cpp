// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Tests/CataclysmTestWorld.h"
#include "Components/SphereComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "Misc/ScopeExit.h"

/**
 * Three Death nodes that pay with Fervour (the Death trees' slice C3c, 2026-11-25): the Soul Collector's Cinders Kept,
 * "3% per point of the Fervour you spend returns to you 4 seconds later"; its Phylactery, the third vow's first option,
 * "A blow that would kill you instead spends all your Fervour (at least 50) and leaves you at that many percent of your
 * health. Once every 60 seconds."; and the Shadow's Borrowed Step, "Your movement skill can be used while it is on
 * cooldown by spending 40 Fervour, reduced by 2 per point."
 *
 * EACH STAT IS GIVEN BY HAND for the mechanism, so each figure is exact; one test reads the real rows' figures at the
 * points the trees allow.
 */

namespace CataclysmFervourPaidTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;
	using CataclysmTestWorld::RunClock;

	/** An actor with health, mana, the combat set and a Fervour pool of 100, findable by a skill's search. */
	struct FPayer
	{
		FPayer(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Where);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// Raw pointers on purpose: AddAttributeSetSubobject is a template and a TObjectPtr deduces the wrapper.
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 200.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 200.0f);
			Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
		}

		~FPayer()
		{
			if (IsValid(Actor))
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

		float Fervour() const { return Get(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute()); }
		float Health() const { return Get(UCataclysmVitalAttributeSet::GetHealthAttribute()); }

		/** One stat given flat by hand, as its row will give it. */
		void Grant(const TCHAR* Stat, float Value) const
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::PassiveKeystone;
			Flat.Value = Value;
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A blow of this much attack damage from a fresh attacker standing beside `Struck`. */
	void StrikePayer(UWorld* World, const FPayer& Struck, float AttackDamage)
	{
		const FPayer Attacker(World, FVector(200.0f, 0.0f, 0.0f));
		Attacker.Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), AttackDamage);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Struck.Actor, /*DamagePercent=*/100.0f);
	}

	/** The longest time left on the movement key's cooldown, or nought. */
	float MovementSecondsLeft(const UAbilitySystemComponent* System)
	{
		const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Movement);
		float Most = 0.0f;
		for (const float Left : System->GetActiveEffectsTimeRemaining(
				 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag))))
		{
			Most = FMath::Max(Most, Left);
		}
		return Most;
	}

	/** The total of one stat's modifiers that this allocation's real rows grant. */
	float RealRowsTotal(const FName Node, int32 Points, const TCHAR* Stat)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(Node, Points);
		const TMap<FName, TArray<FCataclysmStatModifier>> Rows = UCataclysmPassiveTree::ModifiersFor(
			Allocation, UCataclysmPassiveTree::LoadNodeTable(), UCataclysmPassiveTree::LoadEffectTable(),
			{FName(TEXT("Death"))});
		float Total = 0.0f;
		if (const TArray<FCataclysmStatModifier>* Found = Rows.Find(FName(Stat)))
		{
			for (const FCataclysmStatModifier& Each : *Found)
			{
				Total += Each.Value;
			}
		}
		return Total;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourPaidCindersTest,
	"Cataclysm.FervourPaid.CindersKeptReturnsAShareOfEachSpendFourSecondsLater",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Cinders Kept at six points, 18%: a spend of 50 returns 9 Fervour 4 seconds later, not before, and a return to a bar
 * with too little room is cut to the room left. THE CONTROLS: before the stat a spend leaves nothing to return; at 3.9
 * seconds nothing is due; and a death forgets what was waiting.
 */
bool FCataclysmFervourPaidCindersTest::RunTest(const FString&)
{
	using namespace CataclysmFervourPaidTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPayer Payer(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* System = Payer.AbilitySystem;
	Payer.Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 20.0f);

	System->NoteFervourSpent(50.0f);
	TestEqual(TEXT("control: with no Cinders Kept nothing waits to return"), System->FervourWaitingToReturn(), 0.0f);

	Payer.Grant(UCataclysmAbilitySystemComponent::FervourSpentReturnedPercentStat, 18.0f);
	System->NoteFervourSpent(50.0f);
	TestEqual(TEXT("a spend of 50 leaves 9 to return"), System->FervourWaitingToReturn(), 9.0f, 0.001f);
	RunClock(World, 3.9f);
	TestEqual(TEXT("control: at 3.9 seconds none is due"), System->ReturnFervourDue(), 0.0f);
	TestEqual(TEXT("control: and the bar is as it was"), Payer.Fervour(), 20.0f, 0.001f);
	RunClock(World, 0.2f);
	TestEqual(TEXT("at 4.1 seconds the 9 return"), System->ReturnFervourDue(), 9.0f, 0.001f);
	TestEqual(TEXT("to the bar"), Payer.Fervour(), 29.0f, 0.001f);
	TestEqual(TEXT("and nothing waits any more"), System->FervourWaitingToReturn(), 0.0f);

	Payer.Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 95.0f);
	System->NoteFervourSpent(50.0f);
	RunClock(World, 4.1f);
	TestEqual(TEXT("a return to a bar with 5 of room adds 5"), System->ReturnFervourDue(), 5.0f, 0.001f);
	TestEqual(TEXT("and fills it"), Payer.Fervour(), 100.0f, 0.001f);

	System->NoteFervourSpent(50.0f);
	System->ClearWhatDeathEnds();
	TestEqual(TEXT("control: a death forgets what was waiting"), System->FervourWaitingToReturn(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourPaidPhylacteryTest,
	"Cataclysm.FervourPaid.PhylacterySavesALethalBlowBySpendingAllTheFervourHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Phylactery, given by hand at its 60 seconds: holding 70 Fervour, a blow of 5000 against 1000 health takes nothing,
 * leaves 70% of health, empties the bar and is a spend of 70. Holding 60 with a Feast-like row on the spend (0.5% of
 * maximum health for each Fervour), a second holder is left at 600 and the row heals 300 on top: the spend is raised
 * after the health is set. THE CONTROL: the same blow on a holder without the stat kills it.
 */
bool FCataclysmFervourPaidPhylacteryTest::RunTest(const FString&)
{
	using namespace CataclysmFervourPaidTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();

	{
		const FPayer Plain(World, FVector(0.0f, 500.0f, 0.0f));
		Plain.Set(Pool, 70.0f);
		StrikePayer(World, Plain, 5000.0f);
		TestEqual(TEXT("control: without Phylactery the blow kills"), Plain.Health(), 0.0f, 0.001f);
		TestEqual(TEXT("control: and the bar is untouched"), Plain.Fervour(), 70.0f, 0.001f);
	}

	{
		const FPayer Saved(World, FVector::ZeroVector);
		Saved.Set(Pool, 70.0f);
		Saved.Grant(UCataclysmAbilitySystemComponent::FervourLethalSaveEverySecondsStat, 60.0f);
		StrikePayer(World, Saved, 5000.0f);
		TestEqual(TEXT("holding 70, the blow leaves 70% of health"), Saved.Health(), 700.0f, 0.01f);
		TestEqual(TEXT("and empties the bar"), Saved.Fervour(), 0.0f, 0.001f);
		TestEqual(TEXT("and is a spend of Fervour"), Saved.AbilitySystem->SecondsSinceFervourSpent(), 0.0f, 0.001f);
	}

	{
		const FPayer Fed(World, FVector(0.0f, -500.0f, 0.0f));
		Fed.Set(Pool, 60.0f);
		Fed.Grant(UCataclysmAbilitySystemComponent::FervourLethalSaveEverySecondsStat, 60.0f);
		FCataclysmPoolAction Feast;
		Feast.Event = FName(TEXT("fervour_spent"));
		Feast.Pool = FName(TEXT("health"));
		Feast.Percent = 0.5f;
		Feast.Base = ECataclysmPoolActionBase::ForEachOfEventAmount;
		Feast.TriggerKey = FName(TEXT("A_row:health"));
		Fed.AbilitySystem->SetPoolActions({Feast});
		StrikePayer(World, Fed, 5000.0f);
		TestEqual(TEXT("holding 60 with a heal on the spend: 600 left, and 300 healed on top"), Fed.Health(), 900.0f,
				  0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourPaidPhylacteryWaitTest,
	"Cataclysm.FervourPaid.PhylacteryWaitsItsSecondsAndNeedsFiftyHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Phylactery's two limits. A holder saved once and holding 80 again 30 seconds later is not saved a second time; 61
 * seconds after the first save it is. A holder with 49 Fervour is not saved. And a death clears the wait, as it clears
 * Nothing Stops It's.
 */
bool FCataclysmFervourPaidPhylacteryWaitTest::RunTest(const FString&)
{
	using namespace CataclysmFervourPaidTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();

	{
		const FPayer Short(World, FVector(0.0f, 500.0f, 0.0f));
		Short.Set(Pool, 49.0f);
		Short.Grant(UCataclysmAbilitySystemComponent::FervourLethalSaveEverySecondsStat, 60.0f);
		TestFalse(TEXT("holding 49, no save is ready"), Short.AbilitySystem->MaySaveLethalBlowWithFervour());
		StrikePayer(World, Short, 5000.0f);
		TestEqual(TEXT("and the blow kills"), Short.Health(), 0.0f, 0.001f);
	}

	const FPayer Saved(World, FVector::ZeroVector);
	Saved.Grant(UCataclysmAbilitySystemComponent::FervourLethalSaveEverySecondsStat, 60.0f);
	Saved.Set(Pool, 80.0f);
	TestTrue(TEXT("set-up: holding 80, a save is ready"), Saved.AbilitySystem->MaySaveLethalBlowWithFervour());
	TestEqual(TEXT("the first save takes all 80"), Saved.AbilitySystem->TakeFervourForLethalSave(), 80.0f, 0.001f);
	RunClock(World, 30.0f);
	Saved.Set(Pool, 80.0f);
	TestFalse(TEXT("30 seconds later, holding 80 again, none is ready"),
			  Saved.AbilitySystem->MaySaveLethalBlowWithFervour());
	TestEqual(TEXT("and none is taken"), Saved.AbilitySystem->TakeFervourForLethalSave(), 0.0f);
	TestEqual(TEXT("so the bar is untouched"), Saved.Fervour(), 80.0f, 0.001f);
	RunClock(World, 31.0f);
	TestTrue(TEXT("61 seconds after the first, it is ready"), Saved.AbilitySystem->MaySaveLethalBlowWithFervour());

	TestEqual(TEXT("set-up: a second save"), Saved.AbilitySystem->TakeFervourForLethalSave(), 80.0f, 0.001f);
	Saved.AbilitySystem->ClearWhatDeathEnds();
	Saved.Set(Pool, 80.0f);
	TestTrue(TEXT("a death clears the wait"), Saved.AbilitySystem->MaySaveLethalBlowWithFervour());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourPaidBorrowedStepTest,
	"Cataclysm.FervourPaid.BorrowedStepUsesTheMovementSkillOnCooldownForFervour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Borrowed Step at eight points, 24 Fervour: a skill on the movement key, used once, waits for its cooldown; a second
 * later, holding 30 Fervour, it may be used again, which takes 24, is a spend, and leaves the cooldown running as it was
 * rather than starting it again. THE
 * CONTROLS: without the stat the second use is refused, and holding the 6 left the third is refused too.
 */
bool FCataclysmFervourPaidBorrowedStepTest::RunTest(const FString&)
{
	using namespace CataclysmFervourPaidTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPayer Mover(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* System = Mover.AbilitySystem;
	const FGameplayAbilitySpecHandle Handle = System->GiveAbilityInSlot(
		UCataclysmStrikeSkill::StaticClass(), ECataclysmAbilitySlot::Movement, /*Level=*/100, Mover.Actor);
	FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Handle);
	UCataclysmSkillTemplate* Skill = Spec ? Cast<UCataclysmSkillTemplate>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("set-up: a skill on the movement key"), Skill))
	{
		return false;
	}
	Skill->SkillName = TEXT("Test Step");
	Skill->Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=3; Angle=90"));
	Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.Melee"));
	Mover.Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 30.0f);

	if (!TestTrue(TEXT("set-up: the first use"), System->TryActivateAbility(Handle)))
	{
		return false;
	}
	// A SECOND OF IT RUN, so a use that started the cooldown again would show as more left than this.
	RunClock(World, 1.0f);
	const float Left = MovementSecondsLeft(System);
	if (!TestTrue(*FString::Printf(TEXT("set-up: it waits for its cooldown: %.2f"), Left), Left > 0.0f))
	{
		return false;
	}
	TestFalse(TEXT("control: without Borrowed Step the second use is refused"), System->TryActivateAbility(Handle));

	Mover.Grant(UCataclysmGameplayAbility::MovementOnCooldownFervourCostStat, 24.0f);
	TestTrue(TEXT("with it, holding 30, the second use is made"), System->TryActivateAbility(Handle));
	TestEqual(TEXT("and takes 24 Fervour"), Mover.Fervour(), 6.0f, 0.001f);
	TestEqual(TEXT("which is a spend"), System->SecondsSinceFervourSpent(), 0.0f, 0.001f);
	TestEqual(TEXT("and the cooldown runs on as it was"), MovementSecondsLeft(System), Left, 0.01f);
	TestFalse(TEXT("control: holding the 6 left, the third use is refused"), System->TryActivateAbility(Handle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourPaidRealRowsTest,
	"Cataclysm.FervourPaid.TheRealRowsGiveEighteenSixtyAndTwentyFour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The real rows through the tree's own sum: Cinders Kept at six points is 18; Borrowed Step at eight is 40 less 16, 24,
 * and at one 38 (THE CONTROL that the 40 is paid once and the 2 per point); Phylactery's row is the third vow's first
 * option at 60.
 */
bool FCataclysmFervourPaidRealRowsTest::RunTest(const FString&)
{
	using namespace CataclysmFervourPaidTest;

	TestEqual(TEXT("Cinders Kept at six points: 18"),
			  RealRowsTotal(FName(TEXT("SoulCollector_basic_d_c1")), 6,
							UCataclysmAbilitySystemComponent::FervourSpentReturnedPercentStat), 18.0f, 0.001f);
	TestEqual(TEXT("Borrowed Step at eight points: 24"),
			  RealRowsTotal(FName(TEXT("Shadow_basic_c_c0")), 8,
							UCataclysmGameplayAbility::MovementOnCooldownFervourCostStat), 24.0f, 0.001f);
	TestEqual(TEXT("control: Borrowed Step at one point: 38"),
			  RealRowsTotal(FName(TEXT("Shadow_basic_c_c0")), 1,
							UCataclysmGameplayAbility::MovementOnCooldownFervourCostStat), 38.0f, 0.001f);

	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}
	int32 Found = 0;
	for (const FCataclysmPassiveEffectRow* Row :
		 UCataclysmPassiveTree::EffectsFor(Effects, FName(TEXT("SoulCollector_capstone_100"))))
	{
		if (Row->Stat == FString(UCataclysmAbilitySystemComponent::FervourLethalSaveEverySecondsStat))
		{
			++Found;
			TestEqual(TEXT("Phylactery is the first option"), Row->Option, 1);
			TestEqual(TEXT("once every 60 seconds"), Row->ValuePerPoint, 60.0f, 0.001f);
		}
	}
	TestEqual(TEXT("Phylactery has one row"), Found, 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
