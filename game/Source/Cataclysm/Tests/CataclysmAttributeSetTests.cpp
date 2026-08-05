// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "GameplayEffect.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

/**
 * Tests for the five attribute sets.
 *
 * The rules being checked are design decisions, not implementation details, and
 * several of them are easy to get backwards. The most important is that a SOFT
 * cap must not be enforced as a clamp: clamping resistance at 70 would silently
 * delete over-capping, which the design relies on against enemy penetration.
 */

namespace CataclysmAttributeTest
{
	/** An actor carrying all five attribute sets. */
	struct FScopedFullCharacter
	{
		explicit FScopedFullCharacter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers, not TObjectPtr: AddAttributeSetSubobject is a
			// template and deduces T from the argument, so a TObjectPtr would
			// deduce the wrapper rather than the attribute set.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmPrimaryAttributeSet* NewPrimary = NewObject<UCataclysmPrimaryAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist = NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource = NewObject<UCataclysmClassResourceAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewPrimary);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewResource);

			Vitals = NewVitals;
			Primary = NewPrimary;
			Combat = NewCombat;
			Resistances = NewResist;
			ClassResource = NewResource;

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
		}

		~FScopedFullCharacter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void Add(const FGameplayAttribute& Attribute, float Magnitude) const
		{
			UGameplayEffect* Effect = NewObject<UGameplayEffect>(
				GetTransientPackage(), FName(TEXT("TestEffect")));
			Effect->DurationPolicy = EGameplayEffectDurationType::Instant;

			const int32 Index = Effect->Modifiers.Num();
			Effect->Modifiers.SetNum(Index + 1);
			FGameplayModifierInfo& Info = Effect->Modifiers[Index];
			Info.Attribute = Attribute;
			Info.ModifierOp = EGameplayModOp::Additive;
			Info.ModifierMagnitude = FScalableFloat(Magnitude);

			AbilitySystem->ApplyGameplayEffectToSelf(
				Effect, 1.0f, AbilitySystem->MakeEffectContext());
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmVitalAttributeSet> Vitals = nullptr;
		TObjectPtr<UCataclysmPrimaryAttributeSet> Primary = nullptr;
		TObjectPtr<UCataclysmCombatAttributeSet> Combat = nullptr;
		TObjectPtr<UCataclysmResistanceAttributeSet> Resistances = nullptr;
		TObjectPtr<UCataclysmClassResourceAttributeSet> ClassResource = nullptr;
	};

	static UWorld* MakeWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------
// The sheet is complete
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmSheetIsCompleteTest,
	"Cataclysm.Attributes.CharacterSheetIsComplete")
{
	// 43 stats on the character sheet, plus the four current values that pair
	// with a maximum, plus the damage meta attribute.
	const int32 Vitals = UCataclysmVitalAttributeSet::GetAllAttributes().Num();
	const int32 Primary = UCataclysmPrimaryAttributeSet::GetAllAttributes().Num();
	const int32 Combat = UCataclysmCombatAttributeSet::GetAllAttributes().Num();
	const int32 Resist = UCataclysmResistanceAttributeSet::GetAllAttributes().Num();
	const int32 Resource = UCataclysmClassResourceAttributeSet::GetAllAttributes().Num();

	/**
	 * Attributes that exist but are NOT on the character sheet.
	 *
	 * Attack damage is the only one. `sim/cataclysm_sim/affixes.py` names it
	 * explicitly as an off-sheet stat, because it belongs to the equipped weapon
	 * rather than to the character: a Greataxe supplies 144 and a Fist 30, and
	 * the character has no value of their own. It has to be an attribute anyway,
	 * because every skill's damage is a percentage of it and two affixes add to
	 * it, but it does not make the sheet longer.
	 */
	constexpr int32 OffSheetCombatStats = 1;

	TestEqual(TEXT("Eight primary attributes"), Primary, 8);
	TestEqual(TEXT("Eight resistances, one per damage type"), Resist, 8);
	// Twenty-five since the eight increased-damage-against-a-type stats were
	// added for #213. Seventeen before that.
	TestEqual(TEXT("Twenty-five combat and utility stats, plus attack damage off the sheet"),
		Combat, 25 + OffSheetCombatStats);
	// Thirteen since mana leech and energy shield leech were added for #214.
	TestEqual(TEXT("Thirteen vital attributes including the damage meta"), Vitals, 13);
	TestEqual(TEXT("Two class resource attributes"), Resource, 2);

	// The 43 sheet stats: 3 maxima + 6 recovery from vitals, 25 combat,
	// 8 resistances, 1 class resource maximum. The six recovery stats are the
	// three regenerations and the three leeches. The 25 combat stats include the
	// eight increased-damage-against-a-type figures, which are the offensive
	// mirror of the eight resistances.
	TestEqual(TEXT("Forty-three stats on the character sheet"),
		(Vitals - 3 - 1) + (Combat - OffSheetCombatStats) + Resist + (Resource - 1), 43);
	return true;
}

CATACLYSM_TEST(FCataclysmNoDuplicateAttributesTest,
	"Cataclysm.Attributes.NoAttributeIsDeclaredTwice")
{
	TArray<FGameplayAttribute> All;
	All.Append(UCataclysmVitalAttributeSet::GetAllAttributes());
	All.Append(UCataclysmPrimaryAttributeSet::GetAllAttributes());
	All.Append(UCataclysmCombatAttributeSet::GetAllAttributes());
	All.Append(UCataclysmResistanceAttributeSet::GetAllAttributes());
	All.Append(UCataclysmClassResourceAttributeSet::GetAllAttributes());

	TSet<FString> Names;
	for (const FGameplayAttribute& Attribute : All)
	{
		Names.Add(Attribute.GetName());
	}
	TestEqual(TEXT("Every attribute name is unique"), Names.Num(), All.Num());
	return true;
}

// --------------------------------------------------------------------------
// Hard caps clamp, soft caps do not
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmCritChanceHardCapTest,
	"Cataclysm.Attributes.CritChanceIsHardCappedAtOneHundred")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);
		Fixture.Add(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 500.0f);

		TestEqual(TEXT("Crit chance clamps at 100"),
			Fixture.Combat->GetCritChance(), 100.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmEvasionSoftCapTest,
	"Cataclysm.Attributes.EvasionSoftCapIsNotEnforced")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		// The design states evasion caps at 60% and that gear enchantments may
		// exceed it. Clamping here would delete the over-cap.
		Fixture.Add(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 85.0f);

		TestTrue(TEXT("Evasion is allowed above its soft cap"),
			Fixture.Combat->GetEvasion() > UCataclysmCombatAttributeSet::EvasionSoftCap);
		TestEqual(TEXT("Evasion keeps the value it was given"),
			Fixture.Combat->GetEvasion(), 85.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmResistanceOverCapTest,
	"Cataclysm.Attributes.ResistanceCanExceedSeventyPercent")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		// Over-capping is the point: enemy penetration reduces effective
		// resistance, so headroom above the cap is what keeps a character at the
		// cap in practice. Clamping the attribute would remove that entirely.
		Fixture.Add(UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute(), 150.0f);

		TestEqual(TEXT("Raw resistance keeps its over-capped value"),
			Fixture.Resistances->GetDemonicResistance(), 150.0f);

		// The 70% figure caps what resistance is WORTH, not what it may be.
		TestEqual(TEXT("Effective resistance is capped at seventy"),
			UCataclysmResistanceAttributeSet::EffectiveResistance(150.0f), 70.0f);
		TestEqual(TEXT("Effective resistance below the cap is unchanged"),
			UCataclysmResistanceAttributeSet::EffectiveResistance(45.0f), 45.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmNegativeResistanceTest,
	"Cataclysm.Attributes.ResistanceCanGoNegative")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		// Several enchantments reduce resistance. Taking extra damage from a
		// damage type is a real drawback, not an error state.
		Fixture.Add(UCataclysmResistanceAttributeSet::GetVoidResistanceAttribute(), -40.0f);

		TestEqual(TEXT("Resistance is allowed below zero"),
			Fixture.Resistances->GetVoidResistance(), -40.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmBlockChanceUncappedTest,
	"Cataclysm.Attributes.BlockChanceHasNoCap")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		// A block removes 50% of a hit rather than preventing it, so 100% block
		// chance is 50% damage reduction, not immunity. There is nothing to cap.
		Fixture.Add(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 100.0f);

		TestEqual(TEXT("Block chance reaches one hundred"),
			Fixture.Combat->GetBlockChance(), 100.0f);
		TestEqual(TEXT("A block removes half a hit, not all of it"),
			UCataclysmCombatAttributeSet::BlockDamageReduction, 50.0f);
	}
	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// Vitals
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmManaClampTest,
	"Cataclysm.Attributes.ManaClampsToItsMaximum")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		Fixture.Add(UCataclysmVitalAttributeSet::GetManaAttribute(), 5000.0f);
		TestEqual(TEXT("Mana caps at its maximum"),
			Fixture.Vitals->GetMana(), Fixture.Vitals->GetMaxMana());

		Fixture.Add(UCataclysmVitalAttributeSet::GetManaAttribute(), -5000.0f);
		TestEqual(TEXT("Mana floors at zero"), Fixture.Vitals->GetMana(), 0.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmZeroEnergyShieldTest,
	"Cataclysm.Attributes.AClassMayHaveNoEnergyShield")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		// Two of the three Demonic classes have no energy shield at all, so a
		// maximum of zero is a design position rather than an error. If this
		// floored at one the way MaxHealth does, those classes would gain a
		// phantom point of shield.
		TestEqual(TEXT("Maximum energy shield starts at zero"),
			Fixture.Vitals->GetMaxEnergyShield(), 0.0f);

		// The starting value alone does not test the floor: the constructor's
		// Init call writes the attribute directly and never passes through
		// PreAttributeChange. Only a gameplay effect exercises the clamp, so the
		// maximum is driven up and back down to zero here.
		Fixture.Add(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 500.0f);
		TestEqual(TEXT("Maximum energy shield can be raised"),
			Fixture.Vitals->GetMaxEnergyShield(), 500.0f);

		Fixture.Add(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), -500.0f);
		TestEqual(TEXT("Maximum energy shield returns to exactly zero, not one"),
			Fixture.Vitals->GetMaxEnergyShield(), 0.0f);

		Fixture.Add(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 500.0f);
		TestEqual(TEXT("Energy shield cannot rise above a maximum of zero"),
			Fixture.Vitals->GetEnergyShield(), 0.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmMaxHealthFloorsAtOneTest,
	"Cataclysm.Attributes.MaxHealthFloorsAtOneNotZero")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		Fixture.Add(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), -9999.0f);

		// Unlike every other maximum, this one cannot be zero: it would collapse
		// the health clamp to a single point and make every
		// percentage-of-maximum calculation divide by zero.
		TestTrue(TEXT("Maximum health stays at or above one"),
			Fixture.Vitals->GetMaxHealth() >= 1.0f);
	}
	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// Cooldown reduction divides rather than subtracting
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmCooldownDivisionTest,
	"Cataclysm.Attributes.CooldownReductionDividesAndNeverReachesZero")
{
	using FCombat = UCataclysmCombatAttributeSet;

	// The design document's worked example: a character shown at 25% reduction
	// turns a four second skill into a three second one.
	TestEqual(TEXT("A third of increases shows as 25 percent"),
		FMath::RoundToInt(FCombat::DisplayedCooldownReduction(1.0f / 3.0f)), 25);
	TestTrue(TEXT("Four seconds becomes three"),
		FMath::IsNearlyEqual(FCombat::FinalCooldown(4.0f, 1.0f / 3.0f), 3.0f, 0.001f));

	// One hundred points of Efficacy at one per cent each halves every cooldown.
	TestTrue(TEXT("One hundred Efficacy halves a cooldown"),
		FMath::IsNearlyEqual(FCombat::FinalCooldown(4.0f, 1.0f), 2.0f, 0.001f));

	// And it can never reach zero, which is why no cap is needed.
	for (const float Increases : { 1.0f, 10.0f, 1000.0f, 100000.0f })
	{
		TestTrue(TEXT("Cooldown stays above zero"),
			FCombat::FinalCooldown(4.0f, Increases) > 0.0f);
		TestTrue(TEXT("Displayed reduction stays below one hundred"),
			FCombat::DisplayedCooldownReduction(Increases) < 100.0f);
	}
	return true;
}

// --------------------------------------------------------------------------
// Class resource
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmClassResourceClampTest,
	"Cataclysm.Attributes.ClassResourceClampsToItsPool")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		// Most class resources build from nothing during a fight.
		TestEqual(TEXT("Class resource starts empty"),
			Fixture.ClassResource->GetClassResource(), 0.0f);

		Fixture.Add(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 500.0f);
		TestEqual(TEXT("Class resource caps at its pool"),
			Fixture.ClassResource->GetClassResource(),
			Fixture.ClassResource->GetMaxClassResource());
	}
	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// Attributes that scale a skill or a weapon start empty
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmExternallyBasedStatsTest,
	"Cataclysm.Attributes.SkillAndWeaponBasedStatsStartAtZero")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		// A character has no critical strike chance in the abstract: the base
		// comes from the skill being used. Attack speed's comes from the weapon.
		TestEqual(TEXT("Crit chance starts at zero, supplied by the skill"),
			Fixture.Combat->GetCritChance(), 0.0f);
		TestEqual(TEXT("Attack speed starts at zero, supplied by the weapon"),
			Fixture.Combat->GetAttackSpeed(), 0.0f);

		// Area of effect and damage over time frequency are percentages of what
		// the skill does, so their baseline is 100 rather than zero. A zero here
		// would leave Efficacy nothing to scale.
		TestEqual(TEXT("Area of effect baselines at one hundred per cent"),
			Fixture.Combat->GetAreaOfEffect(), 100.0f);
		TestEqual(TEXT("Damage over time frequency baselines at one hundred"),
			Fixture.Combat->GetDotFrequency(), 100.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmPrimaryAttributesStartEmptyTest,
	"Cataclysm.Attributes.PrimaryAttributesStartAtZeroAndCannotGoNegative")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		for (const FGameplayAttribute& Attribute :
			 UCataclysmPrimaryAttributeSet::GetAllAttributes())
		{
			TestEqual(*FString::Printf(TEXT("%s starts at zero"), *Attribute.GetName()),
				Attribute.GetNumericValue(Fixture.Primary), 0.0f);
		}

		Fixture.Add(UCataclysmPrimaryAttributeSet::GetVitalityAttribute(), -50.0f);
		TestEqual(TEXT("An attribute cannot be spent below zero"),
			Fixture.Primary->GetVitality(), 0.0f);
	}
	World->DestroyWorld(false);
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_AUTOMATION_TESTS
