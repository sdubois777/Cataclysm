// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Interface/CataclysmSkillBar.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Ultimate slot costs 50 Fervour. Issue #1478.
 *
 * RULED 2026-09-09 by the project owner: "The Ultimate slot costs 50 Fervour",
 * on top of its mana, and no other slot costs any. Kept for every class on
 * 2026-09-27, with the three War classes unable to fill Fervour yet.
 *
 * THE FIGURE IS THE SLOT TABLE'S, read by the code under test from the built
 * `DT_SkillSlots`. The 50 written here is the expected answer, and
 * `TheSlotTableStatesFiftyForTheUltimateAndNothingElse` checks the table says it.
 */
namespace CataclysmUltimateFervourTest
{
	constexpr float UltimateFervour = 50.0f;

	/** A caster with mana, and with or without a Fervour pool. */
	struct FScopedCaster
	{
		FScopedCaster(UWorld* World, const FVector& Where, bool bHasFervourPool = true)
		{
			Actor = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
			check(Actor);

			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Where);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmCombatAttributeSet>(Actor));
			if (bHasFervourPool)
			{
				AbilitySystem->AddAttributeSetSubobject(
					NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			}
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		}

		~FScopedCaster()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value)
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		float Fervour() const
		{
			return Get(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}

		void SetFervour(float Value)
		{
			Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Value);
		}

		float Mana() const { return Get(UCataclysmVitalAttributeSet::GetManaAttribute()); }

		/** A plain self buff in the slot named: it only has to be cast. */
		UCataclysmSelfBuffSkill* Grant(ECataclysmAbilitySlot Slot)
		{
			const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbilityInSlot(
				UCataclysmSelfBuffSkill::StaticClass(), Slot, /*Level=*/100, Actor);
			FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
			UCataclysmSelfBuffSkill* Skill =
				Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (Skill)
			{
				Skill->SkillName = TEXT("Test Skill");
				Skill->Params = UCataclysmSkillShapes::ParseParams(TEXT("Duration=6"));
			}
			return Skill;
		}

		bool Use(UGameplayAbility* Ability)
		{
			return Ability && AbilitySystem->TryActivateAbility(
				Ability->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	const FCataclysmSkillBarSlot* BoxFor(const TArray<FCataclysmSkillBarSlot>& Bar,
										 ECataclysmAbilitySlot Slot)
	{
		return Bar.FindByPredicate(
			[Slot](const FCataclysmSkillBarSlot& One) { return One.Slot == Slot; });
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is
// built as a unity blob and a `using namespace` at file scope reaches the other
// files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUltimateRefusedBelowFiftyTest,
	"Cataclysm.UltimateFervour.AnUltimateIsRefusedBelowFiftyFervourAndTakesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmUltimateRefusedBelowFiftyTest::RunTest(const FString&)
{
	using namespace CataclysmUltimateFervourTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCaster Caster(World, FVector::ZeroVector);
	UCataclysmSelfBuffSkill* Ultimate = Caster.Grant(ECataclysmAbilitySlot::Ultimate);
	if (!TestNotNull(TEXT("set-up: an Ultimate is granted"), Ultimate))
	{
		return false;
	}
	Caster.SetFervour(UltimateFervour - 1.0f);
	const float ManaBefore = Caster.Mana();

	TestFalse(TEXT("an Ultimate with 49 Fervour held is refused"), Caster.Use(Ultimate));
	TestEqual(TEXT("and it took no Fervour"), Caster.Fervour(), UltimateFervour - 1.0f,
			  0.001f);
	TestEqual(TEXT("and no mana"), Caster.Mana(), ManaBefore, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUltimateCastAtFiftyTest,
	"Cataclysm.UltimateFervour.AnUltimateWithEnoughFervourIsCastAndTakesFifty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmUltimateCastAtFiftyTest::RunTest(const FString&)
{
	using namespace CataclysmUltimateFervourTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCaster Caster(World, FVector::ZeroVector);
	UCataclysmSelfBuffSkill* Ultimate = Caster.Grant(ECataclysmAbilitySlot::Ultimate);
	if (!TestNotNull(TEXT("set-up: an Ultimate is granted"), Ultimate))
	{
		return false;
	}
	Caster.SetFervour(80.0f);
	const float ManaBefore = Caster.Mana();
	const float ManaCost = Ultimate->ManaCostFor(Caster.AbilitySystem);
	if (!TestTrue(TEXT("set-up: the Ultimate costs mana as well"), ManaCost > 0.0f))
	{
		return false;
	}

	TestTrue(TEXT("an Ultimate with 80 Fervour held is cast"), Caster.Use(Ultimate));
	TestEqual(TEXT("and it took 50 Fervour, leaving 30"), Caster.Fervour(), 30.0f, 0.001f);
	TestEqual(TEXT("and its mana as well"), Caster.Mana(), ManaBefore - ManaCost, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUltimateFreeManaStillCostsFervourTest,
	"Cataclysm.UltimateFervour.AnUltimateWhoseManaIsFreeStillCostsFifty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A SLOT COST AND NOT A MANA COST, ruled 2026-09-27 under the owner's
 * delegation: an Ultimate a row has made free of mana still pays its Fervour.
 */
bool FCataclysmUltimateFreeManaStillCostsFervourTest::RunTest(const FString&)
{
	using namespace CataclysmUltimateFervourTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCaster Caster(World, FVector::ZeroVector);
	UCataclysmSelfBuffSkill* Ultimate = Caster.Grant(ECataclysmAbilitySlot::Ultimate);
	if (!TestNotNull(TEXT("set-up: an Ultimate is granted"), Ultimate))
	{
		return false;
	}
	Ultimate->ManaCostOverride = 0.0f;
	if (!TestEqual(TEXT("set-up: it costs no mana"),
				   Ultimate->ManaCostFor(Caster.AbilitySystem), 0.0f, 0.001f))
	{
		return false;
	}

	Caster.SetFervour(UltimateFervour - 1.0f);
	TestFalse(TEXT("a mana-free Ultimate with 49 Fervour is still refused"),
			  Caster.Use(Ultimate));

	Caster.SetFervour(UltimateFervour);
	TestTrue(TEXT("and with 50 it is cast"), Caster.Use(Ultimate));
	TestEqual(TEXT("taking the 50"), Caster.Fervour(), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOnlyTheUltimateCostsFervourTest,
	"Cataclysm.UltimateFervour.OnlyTheUltimateSlotCostsFervour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOnlyTheUltimateCostsFervourTest::RunTest(const FString&)
{
	using namespace CataclysmUltimateFervourTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCaster Caster(World, FVector::ZeroVector);
	const ECataclysmAbilitySlot Others[] = {
		ECataclysmAbilitySlot::Heavy, ECataclysmAbilitySlot::Special,
		ECataclysmAbilitySlot::Support, ECataclysmAbilitySlot::Movement};
	UCataclysmSelfBuffSkill* Special = nullptr;
	for (const ECataclysmAbilitySlot Slot : Others)
	{
		UCataclysmSelfBuffSkill* Skill = Caster.Grant(Slot);
		if (!TestNotNull(TEXT("set-up: a skill is granted"), Skill))
		{
			return false;
		}
		TestEqual(TEXT("a skill in another slot costs no Fervour"),
				  Skill->FervourCostFor(Caster.AbilitySystem), 0.0f, 0.001f);
		if (Slot == ECataclysmAbilitySlot::Special)
		{
			Special = Skill;
		}
	}

	Caster.SetFervour(0.0f);
	TestTrue(TEXT("and a Special is cast with no Fervour at all"), Caster.Use(Special));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoPoolPaysNoFervourTest,
	"Cataclysm.UltimateFervour.ACharacterWithNoFervourPoolPaysNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * NOTHING FOR A CHARACTER WITH NO FERVOUR POOL AT ALL, ruled 2026-09-27 under the
 * owner's delegation. An ability system without the class-resource set, as an
 * enemy's is, has no bar to pay from.
 */
bool FCataclysmNoPoolPaysNoFervourTest::RunTest(const FString&)
{
	using namespace CataclysmUltimateFervourTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCaster NoPool(World, FVector::ZeroVector, /*bHasFervourPool=*/false);
	UCataclysmSelfBuffSkill* Ultimate = NoPool.Grant(ECataclysmAbilitySlot::Ultimate);
	if (!TestNotNull(TEXT("set-up: an Ultimate is granted"), Ultimate))
	{
		return false;
	}
	TestEqual(TEXT("a character with no Fervour pool is asked for none"),
			  Ultimate->FervourCostFor(NoPool.AbilitySystem), 0.0f, 0.001f);
	TestTrue(TEXT("and casts its Ultimate"), NoPool.Use(Ultimate));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarNamesFervourTest,
	"Cataclysm.UltimateFervour.TheSkillBarGreysAnUltimateShortOfFervourAndNamesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The skill bar greys the Ultimate AND names it: "Ultimate needs 50 Fervour",
 * ruled 2026-09-27. A War character has no Fervour bar on screen, so grey alone
 * would read as a mana problem.
 */
bool FCataclysmSkillBarNamesFervourTest::RunTest(const FString&)
{
	using namespace CataclysmUltimateFervourTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCaster Caster(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("set-up: an Ultimate is granted"),
					 Caster.Grant(ECataclysmAbilitySlot::Ultimate)))
	{
		return false;
	}

	Caster.SetFervour(0.0f);
	{
		const TArray<FCataclysmSkillBarSlot> Bar = UCataclysmSkillBar::Read(Caster.Actor);
		const FCataclysmSkillBarSlot* Box = BoxFor(Bar, ECataclysmAbilitySlot::Ultimate);
		if (!TestNotNull(TEXT("set-up: the bar has an Ultimate box"), Box))
		{
			return false;
		}
		TestTrue(TEXT("with no Fervour the Ultimate is short of it"), Box->bShortOfFervour);
		TestFalse(TEXT("so its box is greyed out"), Box->bAffordable);
		TestEqual(TEXT("and it says what it needs"), UCataclysmSkillBar::FervourTextFor(*Box),
				  FString(TEXT("Ultimate needs 50 Fervour")));
	}

	Caster.SetFervour(UltimateFervour);
	{
		const TArray<FCataclysmSkillBarSlot> Bar = UCataclysmSkillBar::Read(Caster.Actor);
		const FCataclysmSkillBarSlot* Box = BoxFor(Bar, ECataclysmAbilitySlot::Ultimate);
		if (!TestNotNull(TEXT("set-up: the bar still has an Ultimate box"), Box))
		{
			return false;
		}
		TestFalse(TEXT("with 50 it is not short"), Box->bShortOfFervour);
		TestTrue(TEXT("so its box is not greyed out"), Box->bAffordable);
		TestEqual(TEXT("and it says nothing"), UCataclysmSkillBar::FervourTextFor(*Box),
				  FString());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSlotTableFervourTest,
	"Cataclysm.UltimateFervour.TheSlotTableStatesFiftyForTheUltimateAndNothingElse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Read from the built `DT_SkillSlots`, the table the costs above come from. */
bool FCataclysmSlotTableFervourTest::RunTest(const FString&)
{
	using namespace CataclysmUltimateFervourTest;

	const UDataTable* Table = UCataclysmSkillSlots::LoadGeneratedTable();
	if (!TestNotNull(TEXT("the skill slot table loads"), Table))
	{
		return false;
	}

	const ECataclysmAbilitySlot Slots[] = {
		ECataclysmAbilitySlot::BasicAttack, ECataclysmAbilitySlot::Heavy,
		ECataclysmAbilitySlot::Special, ECataclysmAbilitySlot::Support,
		ECataclysmAbilitySlot::Aura, ECataclysmAbilitySlot::Ultimate,
		ECataclysmAbilitySlot::Movement};
	for (const ECataclysmAbilitySlot Slot : Slots)
	{
		const FCataclysmSkillSlotNumbers Numbers = UCataclysmSkillSlots::NumbersFor(Table, Slot);
		if (!TestTrue(TEXT("set-up: the slot has a row"), Numbers.bFound))
		{
			return false;
		}
		const float Expected = Slot == ECataclysmAbilitySlot::Ultimate ? UltimateFervour : 0.0f;
		TestEqual(*FString::Printf(TEXT("slot %d costs %.0f Fervour"),
								   static_cast<int32>(Slot), Expected),
				  Numbers.FervourCost, Expected, 0.001f);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
