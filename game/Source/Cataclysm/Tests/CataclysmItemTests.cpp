// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Items/CataclysmItem.h"
#include "Data/CataclysmDataRows.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * Tests for the item: its value curves, its rarity, and what it contributes.
 *
 * The numbers are pinned to `sim/cataclysm_sim/affixes.py`, which is where the
 * curves were argued out. Several are quoted directly from that model's output
 * rather than recomputed here, because a value recomputed by the same reasoning
 * that produced the code would not test anything.
 */

namespace CataclysmItemTest
{
	using FValues = UCataclysmItemValues;

	/** Loads a generated table so tests read the real data, not a fixture. */
	template <typename RowType>
	UDataTable* LoadTable(const TCHAR* FileName)
	{
		FString Contents;
		const FString Path = FPaths::ProjectDir() / TEXT("Data") / FileName;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			return nullptr;
		}

		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = RowType::StaticStruct();
		if (Table->CreateTableFromCSVString(Contents).Num() > 0)
		{
			return nullptr;
		}
		return Table;
	}

	FCataclysmRolledAffix Rolled(const TCHAR* Affix, int32 Tier, float Roll)
	{
		FCataclysmRolledAffix Out;
		Out.Affix = FName(Affix);
		Out.Tier = Tier;
		Out.Roll = Roll;
		return Out;
	}
}

// ---------------------------------------------------------------------------
// The value curves
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemCurvesTest,
	"Cataclysm.Item.ValueCurvesMatchTheModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemCurvesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	// Printed by sim/cataclysm_sim/affixes.py.
	TestTrue(TEXT("a +0 piece multiplies by 1"),
		FMath::IsNearlyEqual(FValues::GearLevelMultiplier(0), 1.0f, 0.0001f));
	TestTrue(TEXT("a +10 piece multiplies by 3.5245807"),
		FMath::IsNearlyEqual(FValues::GearLevelMultiplier(10), 3.5245807f, 0.0001f));

	// Tier N is worth N sevenths. Linear, not front-loaded, so the later tiers
	// stay worth crafting for.
	for (int32 Tier = 1; Tier <= FValues::MaxAffixTier; ++Tier)
	{
		TestTrue(FString::Printf(TEXT("tier %d is %d sevenths"), Tier, Tier),
			FMath::IsNearlyEqual(FValues::TierFraction(Tier),
								 static_cast<float>(Tier) / 7.0f, 0.0001f));
	}

	// The band for a top value of 120, quoted from the model.
	float Low = 0.0f, High = 0.0f;
	FValues::TierBand(120.0f, 7, Low, High);
	TestTrue(TEXT("tier 7 of 120 runs 90 to 120"),
		FMath::IsNearlyEqual(Low, 90.0f, 0.001f)
		&& FMath::IsNearlyEqual(High, 120.0f, 0.001f));

	FValues::TierBand(120.0f, 1, Low, High);
	TestTrue(TEXT("tier 1 of 120 runs 12.857 to 17.143"),
		FMath::IsNearlyEqual(Low, 12.8571f, 0.001f)
		&& FMath::IsNearlyEqual(High, 17.1429f, 0.001f));

	// Four values quoted from the model, covering both ends of every axis.
	TestTrue(TEXT("T7, perfect roll, +10 gives the stated top value of 120"),
		FMath::IsNearlyEqual(FValues::AffixValue(120.0f, 7, 1.0f, 10), 120.0f, 0.01f));
	TestTrue(TEXT("T7, worst roll, +10 gives 90"),
		FMath::IsNearlyEqual(FValues::AffixValue(120.0f, 7, 0.0f, 10), 90.0f, 0.01f));
	TestTrue(TEXT("T7, perfect roll, +0 gives 34.0466"),
		FMath::IsNearlyEqual(FValues::AffixValue(120.0f, 7, 1.0f, 0), 34.0466f, 0.01f));
	TestTrue(TEXT("T4, middle roll, +5 gives 38.5117"),
		FMath::IsNearlyEqual(FValues::AffixValue(120.0f, 4, 0.5f, 5), 38.5117f, 0.01f));

	// A roll outside the band is clamped rather than escaping it.
	TestTrue(TEXT("a roll above 1 does not exceed the band"),
		FMath::IsNearlyEqual(FValues::AffixValue(120.0f, 7, 5.0f, 10), 120.0f, 0.01f));
	TestTrue(TEXT("a roll below 0 does not fall under the band"),
		FMath::IsNearlyEqual(FValues::AffixValue(120.0f, 7, -5.0f, 10), 90.0f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemBandOverlapTest,
	"Cataclysm.Item.BandsOverlapByExactlyOneTier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemBandOverlapTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	// A perfect roll at one tier can beat a poor roll at the tier above, and can
	// never beat the one above that. The bound is provable rather than tuned: a
	// tier's floor is 0.75 of its own fraction, so tier N is undercut by N-1
	// only when N is above 4 and by N-2 only when N is above 8, which seven
	// tiers cannot reach.
	const float Top = 120.0f;
	for (int32 Tier = 3; Tier <= FValues::MaxAffixTier; ++Tier)
	{
		const float PerfectTwoBelow = FValues::AffixValue(Top, Tier - 2, 1.0f, 10);
		const float WorstHere = FValues::AffixValue(Top, Tier, 0.0f, 10);
		TestTrue(FString::Printf(
				TEXT("a perfect T%d (%.2f) cannot beat the worst T%d (%.2f)"),
				Tier - 2, PerfectTwoBelow, Tier, WorstHere),
			PerfectTwoBelow < WorstHere);
	}

	// And that the one-tier overlap really is there, so the reroll and perfect
	// crafting materials have something to do.
	bool bFoundOverlap = false;
	for (int32 Tier = 2; Tier <= FValues::MaxAffixTier; ++Tier)
	{
		if (FValues::AffixValue(Top, Tier - 1, 1.0f, 10)
			> FValues::AffixValue(Top, Tier, 0.0f, 10))
		{
			bFoundOverlap = true;
			break;
		}
	}
	TestTrue(TEXT("a perfect roll can beat the tier above somewhere"), bFoundOverlap);

	return true;
}

// ---------------------------------------------------------------------------
// Rarity
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemRarityTest,
	"Cataclysm.Item.RarityIsWhatFillsTheSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemRarityTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	// Mirrors RARITY_COMPOSITION in sim/cataclysm_sim/affixes.py.
	struct FCase { int32 Enchantments; int32 Affixes; ECataclysmRarity Rarity; };
	const FCase Cases[] = {
		{ 0, 1, ECataclysmRarity::Everyday },
		{ 0, 2, ECataclysmRarity::Quality },
		{ 0, 3, ECataclysmRarity::Superb },
		{ 0, 4, ECataclysmRarity::Masterful },
		{ 1, 3, ECataclysmRarity::Legendary },
		{ 2, 2, ECataclysmRarity::Mythical },
		{ 3, 1, ECataclysmRarity::Ascendant },
		{ 4, 0, ECataclysmRarity::Cataclysmic },
	};

	for (const FCase& Case : Cases)
	{
		ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
		const bool bFound = FValues::RarityOf(Case.Enchantments, Case.Affixes, Rarity);
		TestTrue(FString::Printf(TEXT("%d enchantments and %d affixes name a rarity"),
				 Case.Enchantments, Case.Affixes), bFound);
		TestEqual(TEXT("and it is the right one"),
			static_cast<int32>(Rarity), static_cast<int32>(Case.Rarity));

		// And the reverse: the composition tables agree in both directions.
		TestEqual(TEXT("affix count round-trips"),
			FValues::AffixSlotsFor(Case.Rarity), Case.Affixes);
		TestEqual(TEXT("enchantment count round-trips"),
			FValues::EnchantmentsFor(Case.Rarity), Case.Enchantments);
	}

	// A Cataclysmic item has no regular affixes at all, which is why the 72
	// affix budget belongs to Masterful gear. See issue #125.
	TestEqual(TEXT("Cataclysmic carries no regular affixes"),
		FValues::AffixSlotsFor(ECataclysmRarity::Cataclysmic), 0);
	TestEqual(TEXT("Masterful carries the most"),
		FValues::AffixSlotsFor(ECataclysmRarity::Masterful), 4);

	// An enchantment takes an affix's slot rather than adding one.
	for (int32 Index = 4; Index < 8; ++Index)
	{
		const ECataclysmRarity Rarity = static_cast<ECataclysmRarity>(Index);
		TestEqual(TEXT("an enchantable rarity fills all four slots"),
			FValues::AffixSlotsFor(Rarity) + FValues::EnchantmentsFor(Rarity),
			FValues::SlotsPerPiece);
	}

	// Combinations no item can have are refused rather than guessed at.
	ECataclysmRarity Unused = ECataclysmRarity::Everyday;
	TestFalse(TEXT("an enchantment with slots left empty is not a rarity"),
		FValues::RarityOf(1, 1, Unused));
	TestFalse(TEXT("nothing at all is not a rarity"),
		FValues::RarityOf(0, 0, Unused));
	TestFalse(TEXT("more than four filled slots is not a rarity"),
		FValues::RarityOf(3, 3, Unused));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemSplitTest,
	"Cataclysm.Item.PrefixSuffixSplitRespectsBothCaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemSplitTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	struct FCase { int32 Slots; int32 Prefixes; int32 Suffixes; };
	const FCase Cases[] = { {1, 1, 0}, {2, 1, 1}, {3, 2, 1}, {4, 2, 2} };

	for (const FCase& Case : Cases)
	{
		int32 Prefixes = 0, Suffixes = 0;
		FValues::PrefixSuffixSplit(Case.Slots, Prefixes, Suffixes);
		TestEqual(FString::Printf(TEXT("%d slots gives prefixes"), Case.Slots),
			Prefixes, Case.Prefixes);
		TestEqual(FString::Printf(TEXT("%d slots gives suffixes"), Case.Slots),
			Suffixes, Case.Suffixes);
		TestEqual(TEXT("and the two add back"), Prefixes + Suffixes, Case.Slots);
		TestTrue(TEXT("neither exceeds its cap"),
			Prefixes <= FValues::PrefixesPerPiece
			&& Suffixes <= FValues::SuffixesPerPiece);
	}

	return true;
}

// ---------------------------------------------------------------------------
// What an item contributes, read from the real generated tables
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemModifiersTest,
	"Cataclysm.Item.ContributesTheRightModifiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemModifiersTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	UDataTable* Bases = LoadTable<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
	UDataTable* Affixes = LoadTable<FCataclysmAffixRow>(TEXT("Affixes.csv"));
	if (!Bases || !Affixes)
	{
		AddError(TEXT("could not load ItemBases.csv or Affixes.csv"));
		return false;
	}

	// A Helm gives 200 flat armor at +10, from game/Data/ItemBases.csv.
	FCataclysmItem Helm;
	Helm.Base = TEXT("Head_Helm");
	Helm.GearLevel = 10;
	Helm.Affixes.Add(Rolled(TEXT("Stat_Flat_maximum_health"), 7, 1.0f));

	TArray<FCataclysmStatModifier> Modifiers =
		UCataclysmItemModifiers::ModifiersFor(Helm, Bases, Affixes);

	TestEqual(TEXT("a Helm with one affix gives two modifiers"), Modifiers.Num(), 2);
	if (Modifiers.Num() == 2)
	{
		TestTrue(TEXT("the implicit is 200 armor"),
			FMath::IsNearlyEqual(Modifiers[0].Value, 200.0f, 0.05f));
		TestEqual(TEXT("and it comes from the base"),
			static_cast<int32>(Modifiers[0].Source),
			static_cast<int32>(ECataclysmModifierSource::GearImplicit));

		TestTrue(TEXT("the affix is a perfect T7 120 health"),
			FMath::IsNearlyEqual(Modifiers[1].Value, 120.0f, 0.05f));
		TestEqual(TEXT("and it comes from an affix"),
			static_cast<int32>(Modifiers[1].Source),
			static_cast<int32>(ECataclysmModifierSource::GearAffix));
	}

	// NOTHING AN ITEM PRODUCES IS EVER A MORE MULTIPLIER. That bucket belongs to
	// gems, passive keystones and enchantments.
	for (const FCataclysmStatModifier& Modifier : Modifiers)
	{
		TestTrue(TEXT("an item never produces a more multiplier"),
			Modifier.Bucket != ECataclysmStatBucket::More);
		TestFalse(TEXT("and its source could not grant one anyway"),
			UCataclysmStatPipeline::CanGrantMore(Modifier.Source));
	}

	// A two-handed weapon doubles both its implicit and its affixes. Quoted from
	// the model: a Greatsword's stated 78 becomes 156, and a flat damage affix
	// worth 18 on a one-hander is worth 36 here.
	FCataclysmItem Greatsword;
	Greatsword.Base = TEXT("Weapon_Greatsword");
	Greatsword.GearLevel = 10;
	Greatsword.Affixes.Add(Rolled(TEXT("Stat_Flat_damage"), 7, 1.0f));

	TArray<FCataclysmStatModifier> WeaponModifiers =
		UCataclysmItemModifiers::ModifiersFor(Greatsword, Bases, Affixes);

	TestTrue(TEXT("the Greatsword is recognised as two-handed"),
		UCataclysmItemModifiers::IsTwoHanded(Greatsword, Bases));
	TestEqual(TEXT("it gives two modifiers"), WeaponModifiers.Num(), 2);
	if (WeaponModifiers.Num() == 2)
	{
		TestTrue(FString::Printf(TEXT("its 78 implicit doubles to 156, got %.2f"),
				 WeaponModifiers[0].Value),
			FMath::IsNearlyEqual(WeaponModifiers[0].Value, 156.0f, 0.05f));
		TestTrue(FString::Printf(TEXT("its 18 affix doubles to 36, got %.2f"),
				 WeaponModifiers[1].Value),
			FMath::IsNearlyEqual(WeaponModifiers[1].Value, 36.0f, 0.05f));
	}

	// The same affix on a one-handed weapon is not doubled.
	FCataclysmItem Axe;
	Axe.Base = TEXT("Weapon_Axe");
	Axe.GearLevel = 10;
	Axe.Affixes.Add(Rolled(TEXT("Stat_Flat_damage"), 7, 1.0f));

	TArray<FCataclysmStatModifier> AxeModifiers =
		UCataclysmItemModifiers::ModifiersFor(Axe, Bases, Affixes);
	TestFalse(TEXT("an Axe is not two-handed"),
		UCataclysmItemModifiers::IsTwoHanded(Axe, Bases));
	if (AxeModifiers.Num() == 2)
	{
		TestTrue(FString::Printf(TEXT("its 46 implicit stays 46, got %.2f"),
				 AxeModifiers[0].Value),
			FMath::IsNearlyEqual(AxeModifiers[0].Value, 46.0f, 0.05f));
		TestTrue(FString::Printf(TEXT("its affix stays 18, got %.2f"),
				 AxeModifiers[1].Value),
			FMath::IsNearlyEqual(AxeModifiers[1].Value, 18.0f, 0.05f));
	}

	// An affix the table does not have is reported and skipped, not guessed at.
	FCataclysmItem Broken;
	Broken.Base = TEXT("Head_Helm");
	Broken.GearLevel = 10;
	Broken.Affixes.Add(Rolled(TEXT("Stat_This_Affix_Does_Not_Exist"), 7, 1.0f));

	AddExpectedError(TEXT("not in the affix table"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TArray<FCataclysmStatModifier> Partial =
		UCataclysmItemModifiers::ModifiersFor(Broken, Bases, Affixes);
	TestEqual(TEXT("only the implicit survives an unknown affix"), Partial.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemRarityFromContentsTest,
	"Cataclysm.Item.AnItemsRarityComesFromItsContents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemRarityFromContentsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	// Three affixes and no enchantment is a Superb, and adding a fourth
	// promotes it to Masterful. Rarity is stored nowhere.
	FCataclysmItem Item;
	Item.Base = TEXT("Head_Helm");
	Item.GearLevel = 10;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Item.Affixes.Add(Rolled(TEXT("Stat_Flat_maximum_health"), 7, 1.0f));
	}

	ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
	TestTrue(TEXT("three affixes name a rarity"),
		UCataclysmItemModifiers::RarityOfItem(Item, Rarity));
	TestEqual(TEXT("and it is Superb"),
		static_cast<int32>(Rarity), static_cast<int32>(ECataclysmRarity::Superb));

	Item.Affixes.Add(Rolled(TEXT("Stat_Flat_armor"), 7, 1.0f));
	TestTrue(TEXT("a fourth affix still names a rarity"),
		UCataclysmItemModifiers::RarityOfItem(Item, Rarity));
	TestEqual(TEXT("adding an affix promoted it to Masterful"),
		static_cast<int32>(Rarity), static_cast<int32>(ECataclysmRarity::Masterful));

	// An enchantment takes an affix's slot, so the same item with one
	// enchantment and three affixes is a Legendary.
	Item.Affixes.Pop();
	Item.EnchantmentCount = 1;
	TestTrue(TEXT("an enchantment still names a rarity"),
		UCataclysmItemModifiers::RarityOfItem(Item, Rarity));
	TestEqual(TEXT("an item carrying an enchantment is a Legendary"),
		static_cast<int32>(Rarity), static_cast<int32>(ECataclysmRarity::Legendary));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
