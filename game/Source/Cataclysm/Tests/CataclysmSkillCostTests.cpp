// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmImpCharacter.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Interface/CataclysmSkillBar.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A SKILL MAY STATE ITS OWN FERVOUR COST, AND ITS POP-UP SAYS WHAT A PRESS COSTS. Issue #2801.
 *
 * The owner, 2026-11-21, on Subjugate: "I assumed it cost 30 fervour because it reserves 30, which would make sense.
 * And that's all the description tells you." Then, ruling: "I think we should actually change it to cost 30 fervor",
 * for Subjugate alone; every other Ultimate keeps its slot's 50 (`game/Data/SkillSlots.csv`). A skill's own figure is
 * stamped from its row by `UCataclysmWeaponSkills::StampOnto` and read by `FervourCostFor`, which every press reads.
 *
 * THE CASTER IS THE ONE `CataclysmUltimateFervourTests.cpp` BUILDS: an actor holding an ability system with a
 * Fervour pool of 100 and ample mana, a plain self buff granted in the slot named. Copied rather than shared, in this
 * file's own namespace, because the module is built as a unity blob.
 */
namespace CataclysmSkillCostTest
{
	constexpr float M = 100.0f;

	/** Subjugate's own Fervour cost, written out rather than read back from the code under test. */
	constexpr float SubjugateFervour = 30.0f;

	/** Every other Ultimate's, the slot table's. */
	constexpr float UltimateFervour = 50.0f;

	struct FScopedCostCaster
	{
		FScopedCostCaster(UWorld* World, const FVector& Where)
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
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		}

		~FScopedCostCaster()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value) { AbilitySystem->SetNumericAttributeBase(Attribute, Value); }

		float Fervour() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}

		void SetFervour(float Value) { Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Value); }

		/**
		 * A plain self buff in the slot named, stamped from a skill as granting stamps one. `FervourCost` is the
		 * skill's own figure, or -1 for its slot's, as a row's cell is.
		 */
		UCataclysmSelfBuffSkill* Grant(ECataclysmAbilitySlot Slot, float FervourCost)
		{
			const FGameplayAbilitySpecHandle Handle =
				AbilitySystem->GiveAbilityInSlot(UCataclysmSelfBuffSkill::StaticClass(), Slot, /*Level=*/100, Actor);
			FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
			UCataclysmSelfBuffSkill* Skill = Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (Skill)
			{
				FCataclysmWeaponSkill Row;
				Row.Slot = Slot;
				Row.Name = TEXT("Test Skill");
				Row.Params = UCataclysmSkillShapes::ParseParams(TEXT("Duration=6"));
				Row.FervourCost = FervourCost;
				UCataclysmWeaponSkills::StampOnto(*Skill, Row);
			}
			return Skill;
		}

		bool Use(UGameplayAbility* Ability)
		{
			return Ability
				&& AbilitySystem->TryActivateAbility(Ability->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
		}

		const FCataclysmSkillBarSlot* BoxFor(TArray<FCataclysmSkillBarSlot>& Bar, ECataclysmAbilitySlot Slot) const
		{
			return Bar.FindByPredicate([Slot](const FCataclysmSkillBarSlot& One) { return One.Slot == Slot; });
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

// --------------------------------------------------------------------------------------------------------------------
// S1. A skill's own Fervour cost is what its press asks and takes; a skill stating none takes its slot's.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillCostOwnFervourTest,
	"Cataclysm.SkillCost.ASkillsOwnFervourCostIsWhatItsPressAsksAndTakes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillCostOwnFervourTest::RunTest(const FString&)
{
	using namespace CataclysmSkillCostTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCostCaster Subjugator(World, FVector::ZeroVector);
	FScopedCostCaster Other(World, FVector(50 * M, 0.0f, 0.0f));
	UCataclysmSelfBuffSkill* Own = Subjugator.Grant(ECataclysmAbilitySlot::Ultimate, SubjugateFervour);
	UCataclysmSelfBuffSkill* Slots = Other.Grant(ECataclysmAbilitySlot::Ultimate, /*FervourCost=*/-1.0f);
	if (!TestNotNull(TEXT("set-up: an Ultimate stating 30"), Own) || !TestNotNull(TEXT("set-up: and one stating none"), Slots))
	{
		return false;
	}
	TestEqual(TEXT("the skill's own figure is its cost"), Own->FervourCostFor(Subjugator.AbilitySystem), SubjugateFervour);
	TestEqual(TEXT("CONTROL: an Ultimate stating none costs its slot's 50"), Slots->FervourCostFor(Other.AbilitySystem),
			  UltimateFervour);

	Subjugator.SetFervour(SubjugateFervour - 1.0f);
	TestFalse(TEXT("at 29 Fervour the press is refused"), Subjugator.Use(Own));
	TestEqual(TEXT("and takes nothing"), Subjugator.Fervour(), SubjugateFervour - 1.0f, 0.001f);

	Subjugator.SetFervour(SubjugateFervour);
	TestTrue(TEXT("at 30 it is cast"), Subjugator.Use(Own));
	TestEqual(TEXT("and takes 30"), Subjugator.Fervour(), 0.0f, 0.001f);

	Other.SetFervour(SubjugateFervour);
	TestFalse(TEXT("CONTROL: the other Ultimate at 30 is refused, needing 50"), Other.Use(Slots));
	TestEqual(TEXT("CONTROL: and takes nothing"), Other.Fervour(), SubjugateFervour, 0.001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// S2. Two thralls holding 60 of a pool of 100 leave a ceiling of 40, and a skill costing 30 is still paid.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillCostUnderTwoThrallsTest,
	"Cataclysm.SkillCost.ThirtyIsPaidWithTwoThrallsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillCostUnderTwoThrallsTest::RunTest(const FString&)
{
	using namespace CataclysmSkillCostTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCostCaster Caster(World, FVector::ZeroVector);
	UCataclysmSelfBuffSkill* Own = Caster.Grant(ECataclysmAbilitySlot::Ultimate, SubjugateFervour);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACataclysmImpCharacter* First =
		World->SpawnActor<ACataclysmImpCharacter>(FVector(10 * M, 0.0f, 0.0f), FRotator::ZeroRotator, Params);
	ACataclysmImpCharacter* Second =
		World->SpawnActor<ACataclysmImpCharacter>(FVector(10 * M, 10 * M, 0.0f), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("set-up: an Ultimate stating 30"), Own) || !TestNotNull(TEXT("set-up: a creature"), First)
		|| !TestNotNull(TEXT("set-up: and another"), Second))
	{
		return false;
	}
	First->SetHealth(100.0f);
	Second->SetHealth(100.0f);
	if (!TestTrue(TEXT("set-up: the caster takes the first"), UCataclysmCommand::Subjugate(Caster.Actor, First))
		|| !TestTrue(TEXT("set-up: and the second"), UCataclysmCommand::Subjugate(Caster.Actor, Second)))
	{
		return false;
	}
	First->ReservedFervour = 30.0f;
	Second->ReservedFervour = 30.0f;

	// THE POOL ASKED TO FILL STOPS AT 40, the 100 less the two thralls' 60.
	Caster.SetFervour(100.0f);
	TestEqual(TEXT("set-up: two thralls holding 60 leave the pool at 40"), Caster.Fervour(), 40.0f, 0.001f);
	// AND THE STORED BASE IS WRITTEN TO THE 40 IT SHOWS. `SetNumericAttributeBase` keeps the 100 it was given as the
	// base and clamps only the current value. In play the base cannot stay above the ceiling, because
	// `UCataclysmClassResourceAttributeSet::PostGameplayEffectExecute` writes the pool back through its setter after
	// every gameplay effect, and Fervour is gained through effects. Window subjugate-w1 read 40 after the press: the 30
	// had come off the hidden 100.
	Caster.SetFervour(Caster.Fervour());
	TestTrue(TEXT("a press costing 30 is accepted at a ceiling of 40"), Caster.Use(Own));
	TestEqual(TEXT("and takes 30, leaving 10"), Caster.Fervour(), 10.0f, 0.001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// S3. The pop-up's cost line: its words by case, Subjugate's 30 read from the skill, a skill with no Fervour cost
// naming none, and a reduction to the mana cost showing in the words.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillCostPopupLineTest,
	"Cataclysm.SkillCost.ThePopupSaysWhatAPressCosts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillCostPopupLineTest::RunTest(const FString&)
{
	using namespace CataclysmSkillCostTest;

	// THE WORDS, BY CASE, on boxes built by hand.
	FCataclysmSkillBarSlot Box;
	Box.bFilled = true;
	Box.Name = TEXT("Subjugate");
	Box.FervourCost = 30.0f;
	Box.ManaCost = 14.0f;
	Box.CooldownLength = 20.0f;
	TestEqual(TEXT("Fervour, mana and a cooldown"), UCataclysmSkillBar::CostLineFor(Box),
			  FString(TEXT("Costs 30 Fervour and 14 mana. 20 s cooldown.")));
	Box.FervourCost = 0.0f;
	Box.CooldownLength = 6.0f;
	TestEqual(TEXT("no Fervour, no Fervour words"), UCataclysmSkillBar::CostLineFor(Box),
			  FString(TEXT("Costs 14 mana. 6 s cooldown.")));
	Box.FervourCost = 30.0f;
	Box.ManaCost = 0.0f;
	Box.CooldownLength = 0.0f;
	TestEqual(TEXT("Fervour alone and no cooldown"), UCataclysmSkillBar::CostLineFor(Box),
			  FString(TEXT("Costs 30 Fervour. No cooldown.")));
	Box.FervourCost = 0.0f;
	TestEqual(TEXT("nothing"), UCataclysmSkillBar::CostLineFor(Box), FString(TEXT("Costs nothing. No cooldown.")));
	Box.FervourCost = 30.0f;
	Box.ManaCost = 14.0f;
	Box.HealthCostPercent = 12.0f;
	Box.CooldownLength = 12.5f;
	Box.MaxCharges = 2;
	TestEqual(TEXT("a cost moved onto health, a part second and two charges"), UCataclysmSkillBar::CostLineFor(Box),
			  FString(TEXT("Costs 30 Fervour and 12% of current health. 12.5 s cooldown, 2 charges.")));
	Box.HealthCostPercent = 0.0f;
	Box.bCostPaidFromHealth = true;
	Box.MaxCharges = 1;
	TestEqual(TEXT("a cost paid from the health pool names health"), UCataclysmSkillBar::CostLineFor(Box),
			  FString(TEXT("Costs 30 Fervour and 14 health. 12.5 s cooldown.")));
	Box.bIsASkillGadget = true;
	Box.ReserveEach = 15.0f;
	TestEqual(TEXT("a skill gadget, whose placing costs nothing, says what each gadget reserves and the gadget rule (V5g)"),
			  UCataclysmSkillBar::CostLineFor(Box),
			  FString(TEXT("Each gadget reserves 15 Fervour. Press again to place another while your Fervour can hold it; "
						   "with no room, every gadget moves to where you aim.")));

	// THE LINE SITS UNDER THE NAME AND ABOVE THE DESCRIPTION.
	FCataclysmSkillBarSlot Described;
	Described.bFilled = true;
	Described.Name = TEXT("Subjugate");
	Described.FervourCost = 30.0f;
	Described.Description = TEXT("Holding a thrall reserves 30 Fervour.");
	const TArray<FString> Lines = UCataclysmSkillBar::PopupLinesFor(Described);
	if (TestEqual(TEXT("the pop-up has the name, the cost and the description"), Lines.Num(), 3))
	{
		TestEqual(TEXT("the cost second"), Lines[1], FString(TEXT("Costs 30 Fervour. No cooldown.")));
		TestEqual(TEXT("and the description, with its reserve, after it"), Lines[2],
				  FString(TEXT("Holding a thrall reserves 30 Fervour.")));
	}

	// FROM A GRANTED SKILL, THROUGH THE BAR: Subjugate's own 30, and a Heavy skill naming no Fervour.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FScopedCostCaster Caster(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("set-up: an Ultimate stating 30"), Caster.Grant(ECataclysmAbilitySlot::Ultimate, SubjugateFervour))
		|| !TestNotNull(TEXT("set-up: and a Heavy skill"), Caster.Grant(ECataclysmAbilitySlot::Heavy, -1.0f)))
	{
		return false;
	}
	TArray<FCataclysmSkillBarSlot> Bar = UCataclysmSkillBar::Read(Caster.Actor);
	const FCataclysmSkillBarSlot* Ultimate = Caster.BoxFor(Bar, ECataclysmAbilitySlot::Ultimate);
	const FCataclysmSkillBarSlot* Heavy = Caster.BoxFor(Bar, ECataclysmAbilitySlot::Heavy);
	if (!TestNotNull(TEXT("set-up: the bar has an Ultimate box"), Ultimate) || !TestNotNull(TEXT("and a Heavy one"), Heavy))
	{
		return false;
	}
	const float ManaBefore = Ultimate->ManaCost;
	const FString UltimateLine = UCataclysmSkillBar::CostLineFor(*Ultimate);
	TestTrue(TEXT("the Ultimate's line names its own 30 Fervour"), UltimateLine.StartsWith(TEXT("Costs 30 Fervour and ")));
	TestFalse(TEXT("CONTROL: the Heavy skill's line names no Fervour"),
			  UCataclysmSkillBar::CostLineFor(*Heavy).Contains(TEXT("Fervour")));
	if (!TestTrue(TEXT("set-up: the Ultimate costs mana, more than the 5 taken off below"), ManaBefore > 5.0f))
	{
		return false;
	}

	// A REDUCTION TO THE MANA COST SHOWS IN THE WORDS: 5 flat off `mana_cost`.
	FCataclysmStatModifier Cheaper;
	Cheaper.Bucket = ECataclysmStatBucket::Flat;
	Cheaper.Value = -5.0f;
	TMap<FName, FCataclysmStatInputs> Inputs;
	Inputs.FindOrAdd(FName(UCataclysmSkillSlots::ManaCostStat)).Modifiers.Add(Cheaper);
	Caster.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
	Bar = UCataclysmSkillBar::Read(Caster.Actor);
	Ultimate = Caster.BoxFor(Bar, ECataclysmAbilitySlot::Ultimate);
	if (!TestNotNull(TEXT("set-up: the bar still has an Ultimate box"), Ultimate))
	{
		return false;
	}
	TestEqual(TEXT("the reduction reaches the box's mana cost"), Ultimate->ManaCost, ManaBefore - 5.0f, 0.01f);
	TestTrue(TEXT("and the words name the lower figure"),
			 UCataclysmSkillBar::CostLineFor(*Ultimate).Contains(
				 FString::Printf(TEXT("and %d mana."), FMath::CeilToInt(ManaBefore - 5.0f - 0.001f))));
	AddInfo(FString::Printf(TEXT("COST LINE: %s"), *UltimateLine));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
