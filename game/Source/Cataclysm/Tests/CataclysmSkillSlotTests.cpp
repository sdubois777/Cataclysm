// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "GameplayTagsManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CataclysmSkillSlotTest
{
	/** The generated skill slot table, or null with an error added. */
	const UDataTable* LoadTable(FAutomationTestBase& Test)
	{
		const UDataTable* Table = UCataclysmSkillSlots::LoadGeneratedTable();
		if (!Table)
		{
			Test.AddError(TEXT("DT_SkillSlots does not exist. Run "
							   "tools/generate_datatable_assets.py."));
		}
		return Table;
	}

	FString DataDir()
	{
		return FPaths::Combine(FPaths::ProjectDir(), TEXT("..") , TEXT("game"), TEXT("Data"));
	}
}

/**
 * Every slot has numbers, and only the two that should have no cooldown do.
 *
 * A cooldown of zero is also what a forgotten cooldown looks like, which is how
 * issue #155 went unnoticed while the Efficacy attribute, an affix and 41
 * enchantments all scaled a base that no skill supplied.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillSlotNumbersTest,
	"Cataclysm.Skills.EverySlotHasItsNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillSlotNumbersTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = CataclysmSkillSlotTest::LoadTable(*this);
	if (!Table)
	{
		return false;
	}

	for (const ECataclysmAbilitySlot Slot : CataclysmAbilitySlots::All())
	{
		const FCataclysmSkillSlotNumbers Numbers =
			UCataclysmSkillSlots::NumbersFor(Table, Slot);

		const FString Name = StaticEnum<ECataclysmAbilitySlot>()
			->GetNameStringByValue(static_cast<int64>(Slot));

		if (!TestTrue(FString::Printf(TEXT("%s has a row"), *Name), Numbers.bFound))
		{
			continue;
		}

		const bool bMayHaveNoCooldown =
			Slot == ECataclysmAbilitySlot::BasicAttack
			|| Slot == ECataclysmAbilitySlot::Aura;

		if (bMayHaveNoCooldown)
		{
			TestEqual(FString::Printf(
				TEXT("%s has no cooldown; it is automatic or a toggle"), *Name),
				Numbers.Cooldown, 0.0f);
		}
		else
		{
			TestTrue(FString::Printf(
				TEXT("%s waits before it can be used again"), *Name),
				Numbers.Cooldown > 0.0f);
		}

		// Only the Basic Attack is free. Everything a player chooses is paid for,
		// or nothing limits how often it is used.
		if (Slot == ECataclysmAbilitySlot::BasicAttack)
		{
			TestEqual(TEXT("the Basic Attack is free"), Numbers.ManaCostAtLevel100, 0.0f);
			TestTrue(TEXT("and it restores mana on hit"),
				Numbers.ManaOnHitAtLevel100 > 0.0f);
		}
		else
		{
			TestTrue(FString::Printf(TEXT("%s costs mana"), *Name),
				Numbers.ManaCostAtLevel100 > 0.0f);
			TestEqual(FString::Printf(
				TEXT("%s does not restore mana; only the automatic slot does"), *Name),
				Numbers.ManaOnHitAtLevel100, 0.0f);
		}
	}

	return true;
}

/**
 * The five slots that wait have a cooldown tag, and the two that do not, do not.
 *
 * A tag nothing can ever apply is the shape of problem this project keeps
 * finding, so Cooldown.Basic and Cooldown.Aura deliberately do not exist.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillCooldownTagTest,
	"Cataclysm.Skills.EveryWaitingSlotHasACooldownTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillCooldownTagTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = CataclysmSkillSlotTest::LoadTable(*this);
	if (!Table)
	{
		return false;
	}

	int32 WithTag = 0;
	for (const ECataclysmAbilitySlot Slot : CataclysmAbilitySlots::All())
	{
		const FCataclysmSkillSlotNumbers Numbers =
			UCataclysmSkillSlots::NumbersFor(Table, Slot);
		const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(Slot);
		const FString Name = StaticEnum<ECataclysmAbilitySlot>()
			->GetNameStringByValue(static_cast<int64>(Slot));

		if (Numbers.Cooldown > 0.0f)
		{
			TestTrue(FString::Printf(
				TEXT("%s waits %.1fs, so Cooldown.%s must exist in the tag list"),
				*Name, Numbers.Cooldown, *Name), Tag.IsValid());
			++WithTag;
		}
		else
		{
			TestFalse(FString::Printf(
				TEXT("%s never waits, so it needs no cooldown tag"), *Name),
				Tag.IsValid());
		}
	}

	TestEqual(TEXT("five slots wait: Heavy, Special, Support, Ultimate, Movement"),
		WithTag, 5);
	return true;
}

/**
 * A mana cost takes the same share of a pool at level 1 as at level 100.
 *
 * A cost that never moved would be crippling early and beneath notice late: the
 * default mana pool runs from 50 to 644. This is what makes a flat number on the
 * tooltip still mean the same thing at both ends of the game.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmManaCostScalingTest,
	"Cataclysm.Skills.ManaCostRidesTheDefaultManaProgression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmManaCostScalingTest::RunTest(const FString& Parameters)
{
	// The constants the cost scaling uses are duplicated in
	// CataclysmSkillSlots.cpp, because a cost has to resolve before any class is
	// known. Checked against ClassStats.csv here so they cannot drift.
	FString Contents;
	const FString Path = CataclysmSkillSlotTest::DataDir() / TEXT("ClassStats.csv");
	if (!FFileHelper::LoadFileToString(Contents, *Path))
	{
		AddError(FString::Printf(TEXT("could not read %s"), *Path));
		return false;
	}

	TArray<FString> Lines;
	Contents.ParseIntoArrayLines(Lines);

	float SheetBase = -1.0f;
	float SheetPerLevel = -1.0f;
	for (const FString& Line : Lines)
	{
		// Default_max_mana,Default,max_mana,50.0,6.0
		if (!Line.StartsWith(TEXT("Default_max_mana,")))
		{
			continue;
		}
		TArray<FString> Fields;
		Line.ParseIntoArray(Fields, TEXT(","));
		if (Fields.Num() >= 5)
		{
			SheetBase = FCString::Atof(*Fields[3]);
			SheetPerLevel = FCString::Atof(*Fields[4]);
		}
		break;
	}

	if (!TestTrue(TEXT("ClassStats.csv has the default max_mana line"), SheetBase >= 0.0f))
	{
		return false;
	}

	// One level 100 cost, scaled down, must take the same share of the default
	// pool at every level.
	const float CostAt100 = 15.0f;
	const float PoolAt100 = SheetBase + SheetPerLevel * 99.0f;
	const float ShareAt100 = CostAt100 / PoolAt100;

	for (const int32 Level : { 1, 25, 50, 100 })
	{
		const float Cost = UCataclysmSkillSlots::ManaCostAtLevel(CostAt100, Level);
		const float Pool = SheetBase + SheetPerLevel * static_cast<float>(Level - 1);
		TestTrue(FString::Printf(
			TEXT("at level %d a %.0f mana skill takes the same share of the pool"),
			Level, CostAt100),
			FMath::IsNearlyEqual(Cost / Pool, ShareAt100, 0.0001f));
	}

	// And the direction is right: cheaper early, dearer late.
	TestTrue(TEXT("a level 1 cost is below a level 100 cost"),
		UCataclysmSkillSlots::ManaCostAtLevel(CostAt100, 1)
			< UCataclysmSkillSlots::ManaCostAtLevel(CostAt100, 100));

	TestTrue(TEXT("the level 100 cost is the figure in the table"),
		FMath::IsNearlyEqual(
			UCataclysmSkillSlots::ManaCostAtLevel(CostAt100, 100), CostAt100, 0.001f));

	// A free skill stays free at every level rather than becoming cheap.
	TestEqual(TEXT("a free skill costs nothing at level 1"),
		UCataclysmSkillSlots::ManaCostAtLevel(0.0f, 1), 0.0f);

	return true;
}

/**
 * The primary damage button is affordable from regeneration alone.
 *
 * THE RULE THAT KEEPS MANA ON HIT FROM BECOMING A GENERATOR. What players object
 * to in that pattern is casting a weak skill about five times to afford one real
 * one. Here the Heavy Attack works with no basic attacks landing at all, so mana
 * on hit pays for the other slots and is never the thing that makes the main
 * button function.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHeavyAffordableTest,
	"Cataclysm.Skills.TheHeavyAttackNeedsNoBasicAttacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHeavyAffordableTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = CataclysmSkillSlotTest::LoadTable(*this);
	if (!Table)
	{
		return false;
	}

	const FCataclysmSkillSlotNumbers Heavy =
		UCataclysmSkillSlots::NumbersFor(Table, ECataclysmAbilitySlot::Heavy);
	if (!TestTrue(TEXT("the Heavy Attack has a row"), Heavy.bFound))
	{
		return false;
	}
	if (!TestTrue(TEXT("and a cooldown to spend its cost over"), Heavy.Cooldown > 0.0f))
	{
		return false;
	}

	// The default class's mana regeneration at level 100, from the Class Stats
	// line: 1 at level 1 plus 0.1 per level.
	const float RegenAt100 = 1.0f + 0.1f * 99.0f;
	const float SpendPerSecond = Heavy.ManaCostAtLevel100 / Heavy.Cooldown;

	TestTrue(FString::Printf(
		TEXT("the Heavy Attack spends %.1f mana/s on cooldown against %.1f/s of "
			 "regeneration, so it needs no basic attacks"),
		SpendPerSecond, RegenAt100),
		SpendPerSecond <= RegenAt100);

	return true;
}

/**
 * The Heavy Attack out-damages the automatic basic attack.
 *
 * The design calls it "often the primary damage button", which has to be
 * arithmetically true rather than only stated. It was false at the 6 second
 * cooldown first proposed. The fastest weapon in the game is the Dagger at 1.50
 * attacks per second, which is what makes this tight.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHeavyIsPrimaryTest,
	"Cataclysm.Skills.TheHeavyAttackIsThePrimaryDamageButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHeavyIsPrimaryTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = CataclysmSkillSlotTest::LoadTable(*this);
	if (!Table)
	{
		return false;
	}

	const FCataclysmSkillSlotNumbers Heavy =
		UCataclysmSkillSlots::NumbersFor(Table, ECataclysmAbilitySlot::Heavy);
	const FCataclysmSkillSlotNumbers Basic =
		UCataclysmSkillSlots::NumbersFor(Table, ECataclysmAbilitySlot::BasicAttack);

	if (!TestTrue(TEXT("both slots have rows"), Heavy.bFound && Basic.bFound))
	{
		return false;
	}
	if (!TestTrue(TEXT("the Heavy Attack has a cooldown"), Heavy.Cooldown > 0.0f))
	{
		return false;
	}

	const float HeavyPerSecond = Heavy.DamagePercent / Heavy.Cooldown;

	// Every weapon rate in the game, from the attack speed decision of
	// 2026-08-04. The Dagger at 1.50 is the fastest.
	const float Rates[] = { 1.50f, 1.45f, 1.40f, 1.35f, 1.30f, 1.28f, 1.25f, 1.20f };
	for (const float Rate : Rates)
	{
		const float BasicPerSecond = Basic.DamagePercent * Rate;
		TestTrue(FString::Printf(
			TEXT("at %.2f attacks/s the Heavy Attack deals %.0f%%/s against the "
				 "basic attack's %.0f%%/s"),
			Rate, HeavyPerSecond, BasicPerSecond),
			HeavyPerSecond > BasicPerSecond);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
