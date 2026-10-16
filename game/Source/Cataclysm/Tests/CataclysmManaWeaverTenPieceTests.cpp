// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * "Mana Weaver (10-Piece Bonus): Your ultimate ability no longer has a cooldown. Instead, its mana cost is doubled
 * every time you use it. This cost resets at the beginning of every dungeon". Ruled 2026-10-14 and 2026-10-15 by the
 * coordinating session. Beside `CataclysmUltimateFervourTests.cpp`, whose caster these tests use again.
 *
 * WHAT IS BUILT. One flag stat, `ultimate_cost_doubles_instead_of_cooldown`, read through
 * `UCataclysmGameplayAbility::UltimateCostDoublesByARow` for a skill in the Ultimate slot: `GetBaseCooldown` answers
 * nought, `ManaCostFor` multiplies its figure by 2 for each paid use counted, and
 * `UCataclysmSkillTemplate::CommitAndBegin` counts a paid use on the ability system,
 * `UCataclysmAbilitySystemComponent::UltimateCostDoublingsHeld`. No row is authored, so the flag is written by hand as
 * the `flat` line of 1 a row would be, with no tags.
 *
 * THE CASTER IS A BARE ACTOR, not a player: an ability system with the vital, combat, class resource and resistance
 * sets, 100,000 health, and the mana and Fervour each test gives it. Its skills are the plain self buff of
 * `Cataclysm.UltimateFervour.`, granted at level 100 and stating NO DURATION, so every use is paid and ends inside its
 * own activation: nothing here waits for an attack clip, nothing ticks, and no use has to be cancelled.
 *
 * THE FIGURES ARE READ, NOT TYPED. The Ultimate's cost is what `ManaCostFor` answers for the control caster, checked
 * as a set-up against the slot table's level-100 figure; every expected figure is that cost doubled.
 *
 * A CONTROL CASTER WITHOUT THE ROW IN EVERY TEST, 100 m from the carrier. Its cooldown is ended between uses where it
 * has to use the Ultimate again, as the reset rows end one: every spent charge returned, then every effect granting
 * the slot's cooldown tag removed.
 *
 * NOT COMPILED AND NOT RUN BY THE SESSION THAT WROTE IT.
 */
namespace CataclysmManaWeaverTenPieceTest
{
	using TenPieceVital = UCataclysmVitalAttributeSet;
	using TenPieceResource = UCataclysmClassResourceAttributeSet;

	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The world for one test, destroyed when the test ends. */
	struct FTenPieceWorld
	{
		FTenPieceWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FTenPieceWorld()
		{
			if (World)
			{
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}
		UWorld* World = nullptr;
	};

	/** A bare caster that can use and pay for a skill: mana, Fervour, and a stat line written by hand. */
	struct FTenPieceCaster
	{
		FTenPieceCaster(UWorld* World, const FVector& Where)
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

			// Raw pointers, not TObjectPtr: AddAttributeSetSubobject deduces its type from the argument.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource = NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist = NewObject<UCataclysmResistanceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResource);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(TenPieceVital::GetMaxHealthAttribute(), 100000.0f);
			Set(TenPieceVital::GetHealthAttribute(), 100000.0f);
			HoldMana(1000.0f);
			HoldFervour(1000.0f);
		}

		~FTenPieceCaster()
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

		/** Mana held, with the maximum raised to it first so nothing clamps it. */
		void HoldMana(float Value)
		{
			Set(TenPieceVital::GetMaxManaAttribute(), FMath::Max(Value, 1.0f));
			Set(TenPieceVital::GetManaAttribute(), Value);
		}

		/** Fervour held, with the maximum raised to it first, for the same reason. */
		void HoldFervour(float Value)
		{
			Set(TenPieceResource::GetMaxClassResourceAttribute(), FMath::Max(Value, 100.0f));
			Set(TenPieceResource::GetClassResourceAttribute(), Value);
		}

		float Mana() const { return Get(TenPieceVital::GetManaAttribute()); }
		float Fervour() const { return Get(TenPieceResource::GetClassResourceAttribute()); }

		/** How many times the Ultimate's cost is doubled now, as the component answers. */
		int32 Doublings() const { return AbilitySystem->UltimateCostDoublingsHeld(); }

		/**
		 * Write the whole stat line: the flag at a flat 1 with no tags when `bFlag`, and a More multiplier of
		 * `ManaCostMorePercent` on `mana_cost` when it is not nought. WHOLESALE, because `SetStatInputs` is.
		 */
		void HoldTheRow(bool bFlag, float ManaCostMorePercent = 0.0f)
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			if (bFlag)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::Enchantment;
				Row.Value = 1.0f;
				FCataclysmStatInputs& Line =
					Stats.FindOrAdd(FName(UCataclysmGameplayAbility::UltimateCostDoublesInsteadOfCooldownStat));
				Line.Base = 0.0f;
				Line.Modifiers.Add(Row);
			}
			if (ManaCostMorePercent != 0.0f)
			{
				FCataclysmStatModifier Cheaper;
				Cheaper.Bucket = ECataclysmStatBucket::More;
				Cheaper.Source = ECataclysmModifierSource::Enchantment;
				Cheaper.Value = ManaCostMorePercent;
				FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(UCataclysmSkillSlots::ManaCostStat));
				Line.Base = 0.0f;
				Line.Modifiers.Add(Cheaper);
			}
			AbilitySystem->SetStatInputs(MoveTemp(Stats));
		}

		/** A plain self buff with no duration in the slot named, at level 100: every use is paid and ends at once. */
		UCataclysmSelfBuffSkill* Grant(ECataclysmAbilitySlot Slot)
		{
			const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbilityInSlot(
				UCataclysmSelfBuffSkill::StaticClass(), Slot, /*Level=*/100, Actor);
			FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
			UCataclysmSelfBuffSkill* Skill = Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (Skill)
			{
				Skill->SkillName = Slot == ECataclysmAbilitySlot::Ultimate ? TEXT("Test Ultimate") : TEXT("Test Skill");
			}
			return Skill;
		}

		/** A press of the skill, as `ProcessAbilityInput` makes one. */
		bool Use(UGameplayAbility* Ability)
		{
			return Ability && AbilitySystem->TryActivateAbility(
				Ability->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
		}

		/**
		 * A free start of the skill already held: `bFreeRepeat` set before the activation, as
		 * `UCataclysmFollowThrough` sets it, with `bFreeRepeatPaysCost` as given. The use clears both as it ends.
		 */
		bool StartFree(UCataclysmGameplayAbility* Ability, bool bPaysCost)
		{
			if (!Ability)
			{
				return false;
			}
			Ability->bFreeRepeat = true;
			Ability->bFreeRepeatPaysCost = bPaysCost;
			const bool bStarted = Use(Ability);
			if (!bStarted)
			{
				Ability->bFreeRepeat = false;
				Ability->bFreeRepeatPaysCost = false;
			}
			return bStarted;
		}

		/** One frame of a held key: the slot's press recorded and processed, as the player controller does each frame. */
		void HoldTheKeyForAFrame(ECataclysmAbilitySlot Slot)
		{
			AbilitySystem->AbilityInputTagPressed(CataclysmAbilitySlots::Tag(Slot));
			AbilitySystem->ProcessAbilityInput();
		}

		/** Whether the slot's cooldown runs now. */
		bool OnCooldown(ECataclysmAbilitySlot Slot) const
		{
			return AbilitySystem->HasMatchingGameplayTag(UCataclysmSkillSlots::CooldownTag(Slot));
		}

		/** End the slot's cooldown as a reset row ends one: every spent charge returned, then the effect removed. */
		void EndCooldown(ECataclysmAbilitySlot Slot)
		{
			const FGameplayTagContainer Tags(UCataclysmSkillSlots::CooldownTag(Slot));
			AbilitySystem->RefillSkillCharges(Tags);
			AbilitySystem->RemoveActiveEffectsWithGrantedTags(Tags);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** What one use did: whether it was allowed, and the mana and Fervour it took. */
	struct FTenPieceUse
	{
		bool bUsed = false;
		float ManaTaken = 0.0f;
		float FervourTaken = 0.0f;
	};

	/** Use the skill once and say what it took. */
	FTenPieceUse UseAndMeasure(FTenPieceCaster& Who, UGameplayAbility* Ability)
	{
		FTenPieceUse Made;
		const float ManaBefore = Who.Mana();
		const float FervourBefore = Who.Fervour();
		Made.bUsed = Who.Use(Ability);
		Made.ManaTaken = ManaBefore - Who.Mana();
		Made.FervourTaken = FervourBefore - Who.Fervour();
		return Made;
	}

	/**
	 * The set-up every test makes: a granted Ultimate for each caster, the row on the carrier only, and the Ultimate's
	 * cost read off the control and checked against the slot table's level-100 figure. False, with the reason reported,
	 * if any of it is missing.
	 */
	bool GrantBoth(FAutomationTestBase& Test, FTenPieceCaster& Control, FTenPieceCaster& Carrier,
				   UCataclysmSelfBuffSkill*& ControlUltimate, UCataclysmSelfBuffSkill*& CarrierUltimate, float& Cost,
				   float CarrierManaCostMorePercent = 0.0f)
	{
		ControlUltimate = Control.Grant(ECataclysmAbilitySlot::Ultimate);
		CarrierUltimate = Carrier.Grant(ECataclysmAbilitySlot::Ultimate);
		if (!Test.TestNotNull(TEXT("set-up: the control caster's Ultimate is granted"), ControlUltimate)
			|| !Test.TestNotNull(TEXT("set-up: and the carrier's"), CarrierUltimate))
		{
			return false;
		}
		Control.HoldTheRow(/*bFlag=*/false);
		Carrier.HoldTheRow(/*bFlag=*/true, CarrierManaCostMorePercent);

		const FCataclysmSkillSlotNumbers Numbers =
			UCataclysmSkillSlots::NumbersFor(UCataclysmSkillSlots::LoadGeneratedTable(), ECataclysmAbilitySlot::Ultimate);
		Cost = ControlUltimate->ManaCostFor(Control.AbilitySystem);
		return Test.TestTrue(TEXT("set-up: the slot table has an Ultimate row"), Numbers.bFound)
			&& Test.TestTrue(FString::Printf(TEXT("set-up: the Ultimate costs mana: %.2f"), Cost), Cost > 0.0f)
			&& Test.TestEqual(TEXT("set-up: and at level 100 it costs the slot table's level-100 figure"), Cost,
							  Numbers.ManaCostAtLevel100, 0.01f)
			&& Test.TestTrue(TEXT("set-up: and the slot states a cooldown"), Numbers.Cooldown > 0.0f)
			&& Test.TestEqual(TEXT("set-up: neither caster has counted a use"), Control.Doublings() + Carrier.Doublings(),
							  0);
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a
// `using namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmManaWeaverTenPieceNoCooldownTest,
	"Cataclysm.ManaWeaverTenPiece.WithTheRowAPaidUseOfTheUltimateStartsNoCooldownAndWithoutItOneStarts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Test 1. THE CONTROL IS READ FIRST: the caster without the row starts its Ultimate's cooldown on a paid use. */
bool FCataclysmManaWeaverTenPieceNoCooldownTest::RunTest(const FString&)
{
	using namespace CataclysmManaWeaverTenPieceTest;

	FTenPieceWorld Scope;
	if (!TestNotNull(TEXT("set-up: a world"), Scope.World))
	{
		return false;
	}
	FTenPieceCaster Control(Scope.World, FVector::ZeroVector);
	FTenPieceCaster Carrier(Scope.World, FVector(0.0f, 100.0f * M, 0.0f));
	UCataclysmSelfBuffSkill* ControlUltimate = nullptr;
	UCataclysmSelfBuffSkill* CarrierUltimate = nullptr;
	float Cost = 0.0f;
	if (!GrantBoth(*this, Control, Carrier, ControlUltimate, CarrierUltimate, Cost))
	{
		return false;
	}
	const ECataclysmAbilitySlot Ultimate = ECataclysmAbilitySlot::Ultimate;

	// THE CONTROL.
	TestTrue(TEXT("control: without the row the Ultimate's base cooldown is above nought"),
		ControlUltimate->GetBaseCooldown() > 0.0f);
	TestTrue(TEXT("control: a paid use is allowed"), Control.Use(ControlUltimate));
	TestTrue(TEXT("control: and starts the Ultimate's cooldown"), Control.OnCooldown(Ultimate));
	TestFalse(TEXT("control: so a second press is refused"), Control.Use(ControlUltimate));

	// THE ROW.
	TestEqual(TEXT("with the row the Ultimate's base cooldown is nought"), CarrierUltimate->GetBaseCooldown(), 0.0f,
		0.0001f);
	TestTrue(TEXT("a paid use is allowed"), Carrier.Use(CarrierUltimate));
	TestFalse(TEXT("and starts no cooldown"), Carrier.OnCooldown(Ultimate));
	TestTrue(TEXT("so a second press at the same moment is allowed too"), Carrier.Use(CarrierUltimate));
	TestFalse(TEXT("and starts none either"), Carrier.OnCooldown(Ultimate));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmManaWeaverTenPieceDoublingTest,
	"Cataclysm.ManaWeaverTenPiece.TheFirstThreePaidUsesCostTheCostItsDoubleAndItsQuadruple",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Test 2. 150, 300 and 600 mana at level 100, read and not typed: the cost, twice it and four times it. THE CONTROL
 * pays the cost each time it can, its cooldown ended between uses.
 */
bool FCataclysmManaWeaverTenPieceDoublingTest::RunTest(const FString&)
{
	using namespace CataclysmManaWeaverTenPieceTest;

	FTenPieceWorld Scope;
	if (!TestNotNull(TEXT("set-up: a world"), Scope.World))
	{
		return false;
	}
	FTenPieceCaster Control(Scope.World, FVector::ZeroVector);
	FTenPieceCaster Carrier(Scope.World, FVector(0.0f, 100.0f * M, 0.0f));
	UCataclysmSelfBuffSkill* ControlUltimate = nullptr;
	UCataclysmSelfBuffSkill* CarrierUltimate = nullptr;
	float Cost = 0.0f;
	if (!GrantBoth(*this, Control, Carrier, ControlUltimate, CarrierUltimate, Cost))
	{
		return false;
	}
	Control.HoldMana(Cost * 10.0f);
	Carrier.HoldMana(Cost * 10.0f);

	// THE CONTROL, THREE TIMES.
	for (int32 Use = 1; Use <= 3; ++Use)
	{
		const FTenPieceUse Paid = UseAndMeasure(Control, ControlUltimate);
		TestTrue(*FString::Printf(TEXT("control: use %d is allowed"), Use), Paid.bUsed);
		TestEqual(*FString::Printf(TEXT("control: and use %d costs the cost"), Use), Paid.ManaTaken, Cost, 0.01f);
		Control.EndCooldown(ECataclysmAbilitySlot::Ultimate);
	}
	TestEqual(TEXT("control: and the caster without the row counts nothing"), Control.Doublings(), 0);

	// THE ROW, THREE TIMES. The figure the skill bar shows, `ManaCostFor`, is asked before each use.
	float Expected = Cost;
	for (int32 Use = 1; Use <= 3; ++Use)
	{
		TestEqual(*FString::Printf(TEXT("before use %d the Ultimate says it costs %.0f"), Use, Expected),
			CarrierUltimate->ManaCostFor(Carrier.AbilitySystem), Expected, 0.01f);
		const FTenPieceUse Paid = UseAndMeasure(Carrier, CarrierUltimate);
		TestTrue(*FString::Printf(TEXT("use %d is allowed"), Use), Paid.bUsed);
		TestEqual(*FString::Printf(TEXT("and use %d takes %.0f"), Use, Expected), Paid.ManaTaken, Expected, 0.01f);
		TestEqual(*FString::Printf(TEXT("and after use %d the count is %d"), Use, Use), Carrier.Doublings(), Use);
		Expected *= 2.0f;
	}
	TestEqual(TEXT("and after three the Ultimate says it costs eight times the cost"),
		CarrierUltimate->ManaCostFor(Carrier.AbilitySystem), Cost * 8.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmManaWeaverTenPieceFreeStartTest,
	"Cataclysm.ManaWeaverTenPiece.AFreeStartOfTheUltimateLeavesTheCountAndOneThatPaysPaysTheDoubledCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Test 3. A free start costs nothing and leaves the count. And a free start that pays, as Spellblade's Will's does,
 * pays the doubled figure and leaves the count: a judgement of the writing session, labelled in the entry.
 */
bool FCataclysmManaWeaverTenPieceFreeStartTest::RunTest(const FString&)
{
	using namespace CataclysmManaWeaverTenPieceTest;

	FTenPieceWorld Scope;
	if (!TestNotNull(TEXT("set-up: a world"), Scope.World))
	{
		return false;
	}
	FTenPieceCaster Control(Scope.World, FVector::ZeroVector);
	FTenPieceCaster Carrier(Scope.World, FVector(0.0f, 100.0f * M, 0.0f));
	UCataclysmSelfBuffSkill* ControlUltimate = nullptr;
	UCataclysmSelfBuffSkill* CarrierUltimate = nullptr;
	float Cost = 0.0f;
	if (!GrantBoth(*this, Control, Carrier, ControlUltimate, CarrierUltimate, Cost))
	{
		return false;
	}
	Carrier.HoldMana(Cost * 10.0f);

	// THE CONTROL: one paid use counts one.
	const FTenPieceUse First = UseAndMeasure(Carrier, CarrierUltimate);
	if (!TestTrue(TEXT("control: a paid use is allowed"), First.bUsed)
		|| !TestEqual(TEXT("control: and counts one"), Carrier.Doublings(), 1))
	{
		return false;
	}

	// A FREE START.
	const float ManaBefore = Carrier.Mana();
	const float FervourBefore = Carrier.Fervour();
	TestTrue(TEXT("a free start of the Ultimate starts"), Carrier.StartFree(CarrierUltimate, /*bPaysCost=*/false));
	TestEqual(TEXT("and takes no mana"), Carrier.Mana(), ManaBefore, 0.001f);
	TestEqual(TEXT("and no Fervour"), Carrier.Fervour(), FervourBefore, 0.001f);
	TestEqual(TEXT("and leaves the count at one"), Carrier.Doublings(), 1);
	TestFalse(TEXT("and the flag it was started with is cleared as it ends"), CarrierUltimate->bFreeRepeat);

	// A FREE START THAT PAYS.
	const float Doubled = CarrierUltimate->ManaCostFor(Carrier.AbilitySystem);
	TestEqual(TEXT("set-up: the Ultimate costs twice the cost now"), Doubled, Cost * 2.0f, 0.01f);
	const float ManaBeforePaying = Carrier.Mana();
	const float FervourBeforePaying = Carrier.Fervour();
	TestTrue(TEXT("a free start that pays starts"), Carrier.StartFree(CarrierUltimate, /*bPaysCost=*/true));
	TestEqual(TEXT("and pays the doubled figure"), ManaBeforePaying - Carrier.Mana(), Doubled, 0.01f);
	TestEqual(TEXT("and the Fervour a press pays"), FervourBeforePaying - Carrier.Fervour(), First.FervourTaken, 0.001f);
	TestEqual(TEXT("and still leaves the count at one"), Carrier.Doublings(), 1);

	// AND THE NEXT PRESS PAYS WHAT THE COUNT SAYS.
	const FTenPieceUse Next = UseAndMeasure(Carrier, CarrierUltimate);
	TestTrue(TEXT("the next paid use is allowed"), Next.bUsed);
	TestEqual(TEXT("and takes twice the cost, not four times"), Next.ManaTaken, Cost * 2.0f, 0.01f);
	TestEqual(TEXT("and counts two"), Carrier.Doublings(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmManaWeaverTenPieceRefusedTest,
	"Cataclysm.ManaWeaverTenPiece.AUseRefusedForWantOfManaTakesNothingAndLeavesTheCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Test 4. A use the doubled cost cannot be paid for is refused, takes nothing and is not counted. */
bool FCataclysmManaWeaverTenPieceRefusedTest::RunTest(const FString&)
{
	using namespace CataclysmManaWeaverTenPieceTest;

	FTenPieceWorld Scope;
	if (!TestNotNull(TEXT("set-up: a world"), Scope.World))
	{
		return false;
	}
	FTenPieceCaster Control(Scope.World, FVector::ZeroVector);
	FTenPieceCaster Carrier(Scope.World, FVector(0.0f, 100.0f * M, 0.0f));
	UCataclysmSelfBuffSkill* ControlUltimate = nullptr;
	UCataclysmSelfBuffSkill* CarrierUltimate = nullptr;
	float Cost = 0.0f;
	if (!GrantBoth(*this, Control, Carrier, ControlUltimate, CarrierUltimate, Cost))
	{
		return false;
	}

	// THE CONTROL: one paid use, counted.
	Carrier.HoldMana(Cost * 10.0f);
	if (!TestTrue(TEXT("control: a paid use is allowed"), Carrier.Use(CarrierUltimate))
		|| !TestEqual(TEXT("control: and counts one"), Carrier.Doublings(), 1))
	{
		return false;
	}

	// ONE MANA SHORT OF THE DOUBLED COST, which is more than the undoubled one.
	Carrier.HoldMana(Cost * 2.0f - 1.0f);
	const float FervourBefore = Carrier.Fervour();
	const FTenPieceUse Refused = UseAndMeasure(Carrier, CarrierUltimate);
	TestFalse(TEXT("one mana short of twice the cost, the use is refused"), Refused.bUsed);
	TestEqual(TEXT("and takes no mana"), Refused.ManaTaken, 0.0f, 0.001f);
	TestEqual(TEXT("and no Fervour"), Carrier.Fervour(), FervourBefore, 0.001f);
	TestEqual(TEXT("and leaves the count at one"), Carrier.Doublings(), 1);

	// WITH ONE MORE MANA IT IS PAID.
	Carrier.HoldMana(Cost * 2.0f);
	const FTenPieceUse Paid = UseAndMeasure(Carrier, CarrierUltimate);
	TestTrue(TEXT("with exactly twice the cost the use is allowed"), Paid.bUsed);
	TestEqual(TEXT("and takes all of it"), Paid.ManaTaken, Cost * 2.0f, 0.01f);
	TestEqual(TEXT("and counts two"), Carrier.Doublings(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmManaWeaverTenPieceResetTest,
	"Cataclysm.ManaWeaverTenPiece.ADeathClearsTheCountAnotherDungeonReadsNoughtAndAFloorChangeKeepsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Test 5. THE DUNGEON AND THE FLOOR ARE WRITTEN BY HAND on a dungeon game mode spawned into the world, as
 * `Cataclysm.GadgetCopiesSkill.` writes `EmpireDungeonId` and `FloorNumber`: `GoToFloor` builds a floor, and the game
 * mode's files are not this change's to touch. So this shows the count does not read the floor and does read the
 * dungeon; not what entering a dungeon or a floor change does in play.
 */
bool FCataclysmManaWeaverTenPieceResetTest::RunTest(const FString&)
{
	using namespace CataclysmManaWeaverTenPieceTest;

	FTenPieceWorld Scope;
	if (!TestNotNull(TEXT("set-up: a world"), Scope.World))
	{
		return false;
	}
	FTenPieceCaster Control(Scope.World, FVector::ZeroVector);
	FTenPieceCaster Carrier(Scope.World, FVector(0.0f, 100.0f * M, 0.0f));
	UCataclysmSelfBuffSkill* ControlUltimate = nullptr;
	UCataclysmSelfBuffSkill* CarrierUltimate = nullptr;
	float Cost = 0.0f;
	if (!GrantBoth(*this, Control, Carrier, ControlUltimate, CarrierUltimate, Cost))
	{
		return false;
	}
	Carrier.HoldMana(Cost * 100.0f);
	ACataclysmDungeonGameMode* Mode = Scope.World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("set-up: the dungeon game mode spawned"), Mode))
	{
		return false;
	}
	Mode->EmpireDungeonId = 7;

	// THE CONTROL: two paid uses in dungeon 7 count two.
	TestTrue(TEXT("control: in dungeon 7 a first paid use is allowed"), Carrier.Use(CarrierUltimate));
	TestTrue(TEXT("control: and a second"), Carrier.Use(CarrierUltimate));
	if (!TestEqual(TEXT("control: two are counted"), Carrier.Doublings(), 2))
	{
		return false;
	}

	// A FLOOR CHANGE KEEPS IT.
	Mode->FloorNumber = Mode->FloorNumber + 1;
	TestEqual(TEXT("a floor of the same dungeon keeps the count"), Carrier.Doublings(), 2);
	TestEqual(TEXT("and the cost at four times"), CarrierUltimate->ManaCostFor(Carrier.AbilitySystem), Cost * 4.0f,
		0.01f);

	// ANOTHER DUNGEON READS NOUGHT, AND COUNTS FROM ONE.
	Mode->EmpireDungeonId = 8;
	TestEqual(TEXT("in dungeon 8 the count reads nought"), Carrier.Doublings(), 0);
	TestEqual(TEXT("and the Ultimate costs the cost"), CarrierUltimate->ManaCostFor(Carrier.AbilitySystem), Cost, 0.01f);
	const FTenPieceUse InEight = UseAndMeasure(Carrier, CarrierUltimate);
	TestTrue(TEXT("a paid use in dungeon 8 is allowed"), InEight.bUsed);
	TestEqual(TEXT("and takes the cost"), InEight.ManaTaken, Cost, 0.01f);
	TestEqual(TEXT("and counts one, not three"), Carrier.Doublings(), 1);

	// NO DUNGEON AT ALL READS NOUGHT TOO.
	Mode->EmpireDungeonId = INDEX_NONE;
	TestEqual(TEXT("with no dungeon walked the count reads nought"), Carrier.Doublings(), 0);

	// A DEATH CLEARS IT. `ClearWhatDeathEnds` is what the player's revival runs.
	Mode->EmpireDungeonId = 8;
	if (!TestEqual(TEXT("set-up: back in dungeon 8 its one use is counted"), Carrier.Doublings(), 1))
	{
		return false;
	}
	Carrier.AbilitySystem->ClearWhatDeathEnds();
	TestEqual(TEXT("what a death ends, cleared, leaves the count at nought"), Carrier.Doublings(), 0);
	TestEqual(TEXT("and the Ultimate costs the cost"), CarrierUltimate->ManaCostFor(Carrier.AbilitySystem), Cost, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmManaWeaverTenPieceManaCostRowTest,
	"Cataclysm.ManaWeaverTenPiece.AManaCostRowThatHalvesTheCostHalvesTheDoubledCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Test 6. A `mana_cost` More of -50, from an enchantment, beside the flag: half the cost, then the cost, then twice
 * it. THE CONTROL holds the same halving and no flag, and pays half the cost each time.
 */
bool FCataclysmManaWeaverTenPieceManaCostRowTest::RunTest(const FString&)
{
	using namespace CataclysmManaWeaverTenPieceTest;

	FTenPieceWorld Scope;
	if (!TestNotNull(TEXT("set-up: a world"), Scope.World))
	{
		return false;
	}
	FTenPieceCaster Control(Scope.World, FVector::ZeroVector);
	FTenPieceCaster Carrier(Scope.World, FVector(0.0f, 100.0f * M, 0.0f));
	UCataclysmSelfBuffSkill* ControlUltimate = nullptr;
	UCataclysmSelfBuffSkill* CarrierUltimate = nullptr;
	float Cost = 0.0f;
	if (!GrantBoth(*this, Control, Carrier, ControlUltimate, CarrierUltimate, Cost, /*CarrierManaCostMorePercent=*/-50.0f))
	{
		return false;
	}
	Control.HoldTheRow(/*bFlag=*/false, /*ManaCostMorePercent=*/-50.0f);
	Control.HoldMana(Cost * 10.0f);
	Carrier.HoldMana(Cost * 10.0f);

	// THE CONTROL: the halving alone, twice.
	for (int32 Use = 1; Use <= 2; ++Use)
	{
		const FTenPieceUse Paid = UseAndMeasure(Control, ControlUltimate);
		TestTrue(*FString::Printf(TEXT("control: use %d is allowed"), Use), Paid.bUsed);
		TestEqual(*FString::Printf(TEXT("control: and use %d takes half the cost"), Use), Paid.ManaTaken, Cost * 0.5f,
			0.01f);
		Control.EndCooldown(ECataclysmAbilitySlot::Ultimate);
	}

	// THE HALVING AND THE FLAG.
	float Expected = Cost * 0.5f;
	for (int32 Use = 1; Use <= 3; ++Use)
	{
		const FTenPieceUse Paid = UseAndMeasure(Carrier, CarrierUltimate);
		TestTrue(*FString::Printf(TEXT("use %d is allowed"), Use), Paid.bUsed);
		TestEqual(*FString::Printf(TEXT("and use %d takes half of the doubled cost, %.0f"), Use, Expected), Paid.ManaTaken,
			Expected, 0.01f);
		Expected *= 2.0f;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmManaWeaverTenPieceFervourTest,
	"Cataclysm.ManaWeaverTenPiece.EveryPaidUseStillPaysTheUltimatesFervour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Test 7. The Ultimate's Fervour is a slot cost, not a mana cost, ruled 2026-09-27, and the row does not touch it.
 * THE CONTROL is what one use of the caster without the row takes; each of three uses with the row takes the same.
 */
bool FCataclysmManaWeaverTenPieceFervourTest::RunTest(const FString&)
{
	using namespace CataclysmManaWeaverTenPieceTest;

	FTenPieceWorld Scope;
	if (!TestNotNull(TEXT("set-up: a world"), Scope.World))
	{
		return false;
	}
	FTenPieceCaster Control(Scope.World, FVector::ZeroVector);
	FTenPieceCaster Carrier(Scope.World, FVector(0.0f, 100.0f * M, 0.0f));
	UCataclysmSelfBuffSkill* ControlUltimate = nullptr;
	UCataclysmSelfBuffSkill* CarrierUltimate = nullptr;
	float Cost = 0.0f;
	if (!GrantBoth(*this, Control, Carrier, ControlUltimate, CarrierUltimate, Cost))
	{
		return false;
	}
	Carrier.HoldMana(Cost * 10.0f);

	const FTenPieceUse ControlUse = UseAndMeasure(Control, ControlUltimate);
	if (!TestTrue(TEXT("control: the caster without the row uses its Ultimate"), ControlUse.bUsed)
		|| !TestTrue(FString::Printf(TEXT("control: and pays Fervour for it: %.1f"), ControlUse.FervourTaken),
					 ControlUse.FervourTaken > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("control: what it pays is the Ultimate's own Fervour"), ControlUse.FervourTaken,
		ControlUltimate->FervourCostFor(Control.AbilitySystem), 0.001f);

	for (int32 Use = 1; Use <= 3; ++Use)
	{
		const FTenPieceUse Paid = UseAndMeasure(Carrier, CarrierUltimate);
		TestTrue(*FString::Printf(TEXT("use %d with the row is allowed"), Use), Paid.bUsed);
		TestEqual(*FString::Printf(TEXT("and use %d pays the same Fervour as the control's use"), Use),
			Paid.FervourTaken, ControlUse.FervourTaken, 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmManaWeaverTenPieceOtherSlotTest,
	"Cataclysm.ManaWeaverTenPiece.ASkillInAnotherSlotKeepsItsCooldownAndItsCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Test 8. The same self buff in the Special slot of the carrier: its base cooldown and its cost are the control's,
 * before and after the carrier's Ultimate has been used twice; a use of it starts its cooldown and counts nothing.
 */
bool FCataclysmManaWeaverTenPieceOtherSlotTest::RunTest(const FString&)
{
	using namespace CataclysmManaWeaverTenPieceTest;

	FTenPieceWorld Scope;
	if (!TestNotNull(TEXT("set-up: a world"), Scope.World))
	{
		return false;
	}
	FTenPieceCaster Control(Scope.World, FVector::ZeroVector);
	FTenPieceCaster Carrier(Scope.World, FVector(0.0f, 100.0f * M, 0.0f));
	UCataclysmSelfBuffSkill* ControlUltimate = nullptr;
	UCataclysmSelfBuffSkill* CarrierUltimate = nullptr;
	float Cost = 0.0f;
	if (!GrantBoth(*this, Control, Carrier, ControlUltimate, CarrierUltimate, Cost))
	{
		return false;
	}
	UCataclysmSelfBuffSkill* ControlSpecial = Control.Grant(ECataclysmAbilitySlot::Special);
	UCataclysmSelfBuffSkill* CarrierSpecial = Carrier.Grant(ECataclysmAbilitySlot::Special);
	if (!TestNotNull(TEXT("set-up: a Special for the control"), ControlSpecial)
		|| !TestNotNull(TEXT("set-up: and one for the carrier"), CarrierSpecial))
	{
		return false;
	}
	Carrier.HoldMana(Cost * 10.0f);

	// THE CONTROL'S SPECIAL.
	const float SpecialCooldown = ControlSpecial->GetBaseCooldown();
	const float SpecialCost = ControlSpecial->ManaCostFor(Control.AbilitySystem);
	if (!TestTrue(TEXT("control: the Special waits a cooldown"), SpecialCooldown > 0.0f)
		|| !TestTrue(TEXT("control: and costs mana"), SpecialCost > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("the carrier's Special waits the control's cooldown"), CarrierSpecial->GetBaseCooldown(),
		SpecialCooldown, 0.0001f);
	TestEqual(TEXT("and costs the control's mana"), CarrierSpecial->ManaCostFor(Carrier.AbilitySystem), SpecialCost,
		0.01f);

	TestTrue(TEXT("set-up: the carrier's Ultimate is used once"), Carrier.Use(CarrierUltimate));
	TestTrue(TEXT("set-up: and again"), Carrier.Use(CarrierUltimate));
	TestEqual(TEXT("set-up: two are counted"), Carrier.Doublings(), 2);

	TestEqual(TEXT("after two uses of the Ultimate the Special still waits the control's cooldown"),
		CarrierSpecial->GetBaseCooldown(), SpecialCooldown, 0.0001f);
	TestEqual(TEXT("and still costs the control's mana"), CarrierSpecial->ManaCostFor(Carrier.AbilitySystem),
		SpecialCost, 0.01f);
	const FTenPieceUse Special = UseAndMeasure(Carrier, CarrierSpecial);
	TestTrue(TEXT("a use of the Special is allowed"), Special.bUsed);
	TestEqual(TEXT("and takes the control's cost"), Special.ManaTaken, SpecialCost, 0.01f);
	TestTrue(TEXT("and starts the Special's cooldown"), Carrier.OnCooldown(ECataclysmAbilitySlot::Special));
	TestEqual(TEXT("and leaves the count at two"), Carrier.Doublings(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmManaWeaverTenPieceHeldKeyTest,
	"Cataclysm.ManaWeaverTenPiece.AHeldKeyCastsNoMoreThanTheManaAndTheFervourAllow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Test 9, the coordinating session's addition. A HELD KEY IS DRIVEN AS THE PLAYER CONTROLLER DRIVES ONE, below its
 * private handler: `ACataclysmPlayerController::Input_AbilitySlotPressed` is bound to `ETriggerEvent::Triggered`, which
 * fires every frame the key is held, and calls `AbilityInputTagPressed` with the slot's tag; the controller then calls
 * `ProcessAbilityInput` once a frame. The handler itself is private to the controller, and its two refusals (a press
 * on an open screen, a stunned pawn) do not apply to a bare caster, so the test makes the two calls it makes. Ten
 * frames each time, at one moment of the world's clock.
 *
 * THE CONTROL: without the row, the held key casts once and the cooldown refuses the rest. With the row and mana for
 * the cost and twice it, two casts and no third. With the row, mana to spare and Fervour for two, two casts and no
 * third.
 */
bool FCataclysmManaWeaverTenPieceHeldKeyTest::RunTest(const FString&)
{
	using namespace CataclysmManaWeaverTenPieceTest;

	FTenPieceWorld Scope;
	if (!TestNotNull(TEXT("set-up: a world"), Scope.World))
	{
		return false;
	}
	FTenPieceCaster Control(Scope.World, FVector::ZeroVector);
	FTenPieceCaster Carrier(Scope.World, FVector(0.0f, 100.0f * M, 0.0f));
	FTenPieceCaster FervourShort(Scope.World, FVector(100.0f * M, 0.0f, 0.0f));
	UCataclysmSelfBuffSkill* ControlUltimate = nullptr;
	UCataclysmSelfBuffSkill* CarrierUltimate = nullptr;
	float Cost = 0.0f;
	if (!GrantBoth(*this, Control, Carrier, ControlUltimate, CarrierUltimate, Cost))
	{
		return false;
	}
	UCataclysmSelfBuffSkill* ShortUltimate = FervourShort.Grant(ECataclysmAbilitySlot::Ultimate);
	if (!TestNotNull(TEXT("set-up: the third caster's Ultimate is granted"), ShortUltimate))
	{
		return false;
	}
	FervourShort.HoldTheRow(/*bFlag=*/true);
	const float Fervour = ControlUltimate->FervourCostFor(Control.AbilitySystem);
	if (!TestTrue(FString::Printf(TEXT("set-up: the Ultimate costs Fervour: %.1f"), Fervour), Fervour > 0.0f))
	{
		return false;
	}
	const int32 Frames = 10;

	// THE CONTROL.
	Control.HoldMana(Cost * 10.0f);
	const float ControlFervourBefore = Control.Fervour();
	for (int32 Frame = 0; Frame < Frames; ++Frame)
	{
		Control.HoldTheKeyForAFrame(ECataclysmAbilitySlot::Ultimate);
	}
	TestEqual(TEXT("control: without the row ten frames of the held key cast once"),
		ControlFervourBefore - Control.Fervour(), Fervour, 0.001f);
	TestTrue(TEXT("control: and its cooldown runs"), Control.OnCooldown(ECataclysmAbilitySlot::Ultimate));

	// MANA FOR TWO: the cost and twice it.
	Carrier.HoldMana(Cost * 3.0f);
	const float CarrierFervourBefore = Carrier.Fervour();
	for (int32 Frame = 0; Frame < Frames; ++Frame)
	{
		Carrier.HoldTheKeyForAFrame(ECataclysmAbilitySlot::Ultimate);
	}
	TestEqual(TEXT("with the row and mana for the cost and twice it, ten frames cast twice"), Carrier.Doublings(), 2);
	TestEqual(TEXT("and paid two casts of Fervour"), CarrierFervourBefore - Carrier.Fervour(), Fervour * 2.0f, 0.001f);
	TestEqual(TEXT("and all the mana"), Carrier.Mana(), 0.0f, 0.01f);

	// FERVOUR FOR TWO, mana to spare.
	FervourShort.HoldMana(Cost * 100.0f);
	FervourShort.HoldFervour(Fervour * 2.0f);
	for (int32 Frame = 0; Frame < Frames; ++Frame)
	{
		FervourShort.HoldTheKeyForAFrame(ECataclysmAbilitySlot::Ultimate);
	}
	TestEqual(TEXT("with the row and Fervour for two, ten frames cast twice"), FervourShort.Doublings(), 2);
	TestEqual(TEXT("and leave no Fervour"), FervourShort.Fervour(), 0.0f, 0.001f);
	TestEqual(TEXT("and took the cost and twice it in mana"), Cost * 100.0f - FervourShort.Mana(), Cost * 3.0f, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
