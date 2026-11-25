// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Items/CataclysmItem.h"
#include "Items/CataclysmDropRoll.h"
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
// The value rule: whole numbers from seven tier ranges. Issue #2883
// ---------------------------------------------------------------------------
//
// UNTIL ISSUE #2883 THIS SECTION TESTED A FLOATING-POINT CURVE: a stated top
// value times a geometric tier fraction, a 75%-to-100% band, an upgrade ladder
// and a stated floor. Five tests pinned that curve (ValueCurvesMatchTheModel,
// BandsOverlapByExactlyOneTier, AStatedFloorIsTheWorstAPlayerCanBeHanded,
// AStatedFloorKeepsTheOneTierOverlap, EveryStatedFloorIsWhatAnAffixActuallyGives).
// The owner approved the replacement on 2026-11-24 ("Approve as written
// (Recommended)"): each affix states seven whole-number ranges at +0, each end
// is upgraded on its own in integers, and the roll picks one of the whole
// numbers between them. The tests below pin that rule. The expected figures
// are printed by `sim/cataclysm_sim/affix_ranges.py`, not recomputed here.

namespace CataclysmItemTest
{
	/** An affix row holding Flat armor's seven ranges, as Affixes.csv states them. */
	FCataclysmAffixRow FlatArmor()
	{
		FCataclysmAffixRow Row;
		Row.AffixName = TEXT("Flat armor");
		Row.AffixKind = TEXT("Stat");
		Row.T1Min = 25;  Row.T1Max = 29;
		Row.T2Min = 30;  Row.T2Max = 35;
		Row.T3Min = 36;  Row.T3Max = 42;
		Row.T4Min = 43;  Row.T4Max = 49;
		Row.T5Min = 50;  Row.T5Max = 59;
		Row.T6Min = 60;  Row.T6Max = 74;
		Row.T7Min = 75;  Row.T7Max = 100;
		return Row;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemCurvesTest,
	"Cataclysm.Item.ValueCurvesMatchTheModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemCurvesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	// THE UPGRADE MULTIPLIER IS UNCHANGED: 1 at +0, 2.5 at +10 (issue #1179).
	TestTrue(TEXT("a +0 piece multiplies by 1"),
		FMath::IsNearlyEqual(FValues::GearLevelMultiplier(0), 1.0f, 0.0001f));
	TestTrue(TEXT("a +10 piece multiplies by 2.5"),
		FMath::IsNearlyEqual(FValues::GearLevelMultiplier(10), 2.5f, 0.0001f));

	// RULE 3, IN INTEGERS. A half goes up, and 10 x 1.15 is exactly 11.5 here,
	// which floating point holds as 11.4999... and would round down.
	TestEqual(TEXT("7 at +10 is 17.5, a half, so 18"), FValues::UpgradedWhole(7, 10), 18);
	TestEqual(TEXT("10 at +1 is 11.5, a half, so 12"), FValues::UpgradedWhole(10, 1), 12);
	TestEqual(TEXT("11 at +1 is 12.65, so 13"), FValues::UpgradedWhole(11, 1), 13);
	TestEqual(TEXT("1 at +10 on a two-handed weapon is 5, doubled inside the rounding"),
		FValues::UpgradedWhole(1, 10, /*bTwoHanded=*/true), 5);
	TestEqual(TEXT("1 at +3 is still 1"), FValues::UpgradedWhole(1, 3), 1);
	TestEqual(TEXT("1 at +4 is 2"), FValues::UpgradedWhole(1, 4), 2);
	TestEqual(TEXT("a level above 10 is held at 10"), FValues::UpgradedWhole(7, 15), 18);

	// RULE 4: every whole number in the range is equally likely.
	TestEqual(TEXT("1 to 2, roll 0: 1"), FValues::WholeAtRoll(1, 2, 0.0f), 1);
	TestEqual(TEXT("1 to 2, roll 0.49: 1"), FValues::WholeAtRoll(1, 2, 0.49f), 1);
	TestEqual(TEXT("1 to 2, roll 0.5: 2"), FValues::WholeAtRoll(1, 2, 0.5f), 2);
	TestEqual(TEXT("1 to 2, roll 1: 2"), FValues::WholeAtRoll(1, 2, 1.0f), 2);
	TestEqual(TEXT("10 to 13, roll 0.25: 11"), FValues::WholeAtRoll(10, 13, 0.25f), 11);
	TestEqual(TEXT("10 to 13, roll 0.75: 13"), FValues::WholeAtRoll(10, 13, 0.75f), 13);

	// FLAT ARMOR, BOTH ENDS OF EVERY AXIS. Printed by affix_ranges.value.
	const FCataclysmAffixRow Armor = FlatArmor();
	TestEqual(TEXT("T7, perfect roll, +10: 250"), FValues::AffixValue(Armor, 7, 1.0f, 10), 250.0f);
	TestEqual(TEXT("T7, worst roll, +10: 188"), FValues::AffixValue(Armor, 7, 0.0f, 10), 188.0f);
	TestEqual(TEXT("T7, middle roll, +10: 219"), FValues::AffixValue(Armor, 7, 0.5f, 10), 219.0f);
	TestEqual(TEXT("T4, middle roll, +5: 81"), FValues::AffixValue(Armor, 4, 0.5f, 5), 81.0f);
	TestEqual(TEXT("T1, worst roll, +0: 25"), FValues::AffixValue(Armor, 1, 0.0f, 0), 25.0f);

	// THE TWO LADDERS TOGETHER span 10x, as they did since issue #1179.
	TestEqual(TEXT("the finished best is ten times the fresh worst"),
		FValues::AffixValue(Armor, 7, 1.0f, FValues::MaxGearLevel)
			/ FValues::AffixValue(Armor, 1, 0.0f, 0), 10.0f, 0.0001f);
	TestTrue(TEXT("the tier ladder still does something"),
		FValues::AffixValue(Armor, 7, 1.0f, 10) > FValues::AffixValue(Armor, 1, 1.0f, 10) * 2.0f);
	TestTrue(TEXT("the gear level ladder still does something"),
		FValues::AffixValue(Armor, 7, 1.0f, 10) > FValues::AffixValue(Armor, 7, 1.0f, 0) * 2.0f);

	// A roll outside [0, 1] is clamped rather than escaping the range.
	TestEqual(TEXT("a roll above 1 does not exceed the range"), FValues::AffixValue(Armor, 7, 5.0f, 10), 250.0f);
	TestEqual(TEXT("a roll below 0 does not fall under it"), FValues::AffixValue(Armor, 7, -5.0f, 10), 188.0f);

	// A HYBRID ROW STATES NO RANGE, so asking it directly gives nothing.
	FCataclysmAffixRow Hybrid;
	Hybrid.AffixKind = TEXT("Hybrid");
	TestEqual(TEXT("a hybrid row on its own gives 0"), FValues::AffixValue(Hybrid, 7, 1.0f, 10), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemTiersNeverOverlapTest,
	"Cataclysm.Item.EveryAffixsTiersAreWholeAndSeparateAtEveryUpgradeLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * EVERY AFFIX IN THE DATA, at every upgrade level, on a two-handed weapon or
 * not: each tier shows at least two whole numbers and starts above where the
 * tier below it ends. The approved proposal's check 2 (issue #2883), and the
 * opposite of the old one-tier overlap the curve had on purpose: the proposal
 * removes the overlap so a higher tier is never worse at any roll.
 *
 * AND THE ENDS ARE THE ROW'S OWN: the worst roll of tier 1 at +0 is T1Min and
 * the best roll of tier 7 at +10 is T7Max upgraded, so no affix's top or bottom
 * moved on the way through the function.
 */
bool FCataclysmItemTiersNeverOverlapTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
	if (!TestNotNull(TEXT("the affix table loaded"), AffixTable))
	{
		return false;
	}

	int32 Ranged = 0;
	for (const TPair<FName, uint8*>& Row : AffixTable->GetRowMap())
	{
		const FCataclysmAffixRow* Affix = reinterpret_cast<const FCataclysmAffixRow*>(Row.Value);
		if (!Affix || Affix->AffixKind.Equals(TEXT("Hybrid"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		++Ranged;
		for (int32 Level = 0; Level <= FValues::MaxGearLevel; ++Level)
		{
			for (const bool bTwoHanded : {false, true})
			{
				int32 PreviousHigh = 0;
				for (int32 Tier = 1; Tier <= FValues::MaxAffixTier; ++Tier)
				{
					int32 Low = 0;
					int32 High = 0;
					FValues::AffixRangeAt(*Affix, Tier, Level, bTwoHanded, Low, High);
					if (High <= Low || Low <= PreviousHigh)
					{
						AddError(FString::Printf(
							TEXT("%s, tier %d at +%d%s: %d to %d after a tier ending at %d"),
							*Row.Key.ToString(), Tier, Level, bTwoHanded ? TEXT(" two-handed") : TEXT(""),
							Low, High, PreviousHigh));
					}
					PreviousHigh = High;
				}
			}
		}
		TestEqual(FString::Printf(TEXT("%s: the worst roll of tier 1 at +0 is T1Min"), *Row.Key.ToString()),
			FValues::AffixValue(*Affix, 1, 0.0f, 0), static_cast<float>(Affix->T1Min));
		TestEqual(FString::Printf(TEXT("%s: the best roll of tier 7 at +10 is T7Max upgraded"), *Row.Key.ToString()),
			FValues::AffixValue(*Affix, 7, 1.0f, FValues::MaxGearLevel),
			static_cast<float>(FValues::UpgradedWhole(Affix->T7Max, FValues::MaxGearLevel)));
	}

	// 73 SINCE ISSUE #2883, the non-hybrid rows; tools/tests/test_affix_values_port.py
	// counts the same rows in Affixes.csv.
	TestEqual(TEXT("every non-hybrid affix was read"), Ranged, 73);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemHybridHalfTest,
	"Cataclysm.Item.AHybridHalfIsItsOwnAffixTwoTiersDownAndATierBelowThreeReadsAsThree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * D6 of the approved proposal (issue #2883): a hybrid at tier N grants each half
 * as that half's own affix at tier N - 2, so a hybrid rolls tiers 3 to 7. A
 * hybrid saved at tier 1 or 2, which the drop roll made before the change, is
 * read as tier 3: ruled by the coordinating session on 2026-11-25, with no save
 * step. Figures printed by `affix_ranges.hybrid_half`.
 */
bool FCataclysmItemHybridHalfTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;
	const FCataclysmAffixRow Armor = FlatArmor();

	for (int32 Tier = FValues::LowestHybridTier; Tier <= FValues::MaxAffixTier; ++Tier)
	{
		for (const float Roll : {0.0f, 0.5f, 1.0f})
		{
			TestEqual(FString::Printf(TEXT("a hybrid at tier %d, roll %.1f, is its half at tier %d"),
								  Tier, Roll, Tier - 2),
				FValues::HybridHalfValue(Armor, Tier, Roll, 10), FValues::AffixValue(Armor, Tier - 2, Roll, 10));
		}
	}
	TestEqual(TEXT("tier 7 at +10, best: 148, Flat armor's tier 5"), FValues::HybridHalfValue(Armor, 7, 1.0f, 10), 148.0f);
	TestEqual(TEXT("a saved tier 1, worst at +0, reads as tier 3: 25"), FValues::HybridHalfValue(Armor, 1, 0.0f, 0), 25.0f);
	TestEqual(TEXT("a saved tier 2, best at +0, reads as tier 3: 29"), FValues::HybridHalfValue(Armor, 2, 1.0f, 0), 29.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemImplicitWholeTest,
	"Cataclysm.Item.AnImplicitIsItsWholeValueAtNoughtUpgraded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * D9 of the approved proposal (issue #2883): an implicit is a whole number at
 * +0, read from the base row's ImplicitNAtZero, upgraded by rule 3. The
 * Greatsword's stated 78 at +10 is 31 at +0. AND EVERY BASE IN THE DATA states a
 * whole +0 value for each implicit it has, and none for one it does not.
 */
bool FCataclysmItemImplicitWholeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	TestEqual(TEXT("31 at +0 is 31"), FValues::ImplicitValueAtLevel(31, 0), 31.0f);
	TestEqual(TEXT("31 at +10 is 77.5, a half, so 78"), FValues::ImplicitValueAtLevel(31, 10), 78.0f);
	TestEqual(TEXT("and 155 on a two-handed weapon"), FValues::ImplicitValueAtLevel(31, 10, true), 155.0f);

	const UDataTable* Bases = LoadTable<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
	if (!TestNotNull(TEXT("ItemBases.csv loaded"), Bases))
	{
		return false;
	}
	int32 Implicits = 0;
	Bases->ForeachRow<FCataclysmItemBaseRow>(TEXT("AnImplicitIsItsWholeValueAtNoughtUpgraded"),
		[this, &Implicits](const FName& Name, const FCataclysmItemBaseRow& Row)
		{
			const TPair<const FString*, int32> Each[] = {{&Row.Implicit1Stat, Row.Implicit1AtZero},
														 {&Row.Implicit2Stat, Row.Implicit2AtZero}};
			for (const TPair<const FString*, int32>& Implicit : Each)
			{
				if (Implicit.Key->IsEmpty())
				{
					TestEqual(FString::Printf(TEXT("%s: no implicit, no value"), *Name.ToString()), Implicit.Value, 0);
					continue;
				}
				++Implicits;
				TestTrue(FString::Printf(TEXT("%s %s: at least 1 at +0"), *Name.ToString(), **Implicit.Key),
					Implicit.Value >= 1);
			}
		});
	TestEqual(TEXT("every implicit in the data was read"), Implicits, 76);
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

	// Reads one stat's single modifier, so a test says which stat it means
	// rather than relying on the order they came back in.
	const auto Only = [this](const TMap<FName, TArray<FCataclysmStatModifier>>& Map,
							 const TCHAR* Stat, FCataclysmStatModifier& Out) -> bool
	{
		const TArray<FCataclysmStatModifier>* Found = Map.Find(FName(Stat));
		if (!Found || Found->Num() != 1)
		{
			AddError(FString::Printf(TEXT("expected exactly one modifier for %s"), Stat));
			return false;
		}
		Out = (*Found)[0];
		return true;
	};

	// A Helm gives 200 flat armor at +10, from game/Data/ItemBases.csv.
	FCataclysmItem Helm;
	Helm.Base = TEXT("Head_Helm");
	Helm.GearLevel = 10;
	Helm.Affixes.Add(Rolled(TEXT("Stat_Flat_maximum_health"), 7, 1.0f));

	const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
		UCataclysmItemModifiers::ModifiersFor(Helm, Bases, Affixes);

	TestEqual(TEXT("a Helm with one health affix touches two stats"),
		Modifiers.Num(), 2);

	FCataclysmStatModifier Armor;
	if (Only(Modifiers, TEXT("armor"), Armor))
	{
		TestTrue(TEXT("the implicit is 200 armor"),
			FMath::IsNearlyEqual(Armor.Value, 200.0f, 0.05f));
		TestEqual(TEXT("and it comes from the base"),
			static_cast<int32>(Armor.Source),
			static_cast<int32>(ECataclysmModifierSource::GearImplicit));
	}

	FCataclysmStatModifier Life;
	if (Only(Modifiers, TEXT("max_health"), Life))
	{
		TestTrue(TEXT("the affix is a perfect T7 120 health"),
			FMath::IsNearlyEqual(Life.Value, 120.0f, 0.05f));
		TestEqual(TEXT("and it comes from an affix"),
			static_cast<int32>(Life.Source),
			static_cast<int32>(ECataclysmModifierSource::GearAffix));
	}

	// NOTHING AN ITEM PRODUCES IS EVER A MORE MULTIPLIER. That bucket belongs to
	// gems, passive keystones and enchantments.
	for (const TPair<FName, TArray<FCataclysmStatModifier>>& Pair : Modifiers)
	{
		for (const FCataclysmStatModifier& Modifier : Pair.Value)
		{
			TestTrue(TEXT("an item never produces a more multiplier"),
				Modifier.Bucket != ECataclysmStatBucket::More);
			TestFalse(TEXT("and its source could not grant one anyway"),
				UCataclysmStatPipeline::CanGrantMore(Modifier.Source));
		}
	}

	// A two-handed weapon doubles both its implicit and its affixes. Quoted from
	// the model: a Greatsword's stated 78 becomes 156, and a flat damage affix
	// worth 22 on a one-hander is worth 44 here.
	//
	// THE 22 IS READ OUT OF THE DATA TABLE, not stated in the engine, so it moves
	// when the design does. It was 18 until issue #511 raised the damage target by
	// applying the enemy's own mitigation to it; the flat damage affix was
	// re-derived against the corrected target and the design workbook now says 22.
	FCataclysmItem Greatsword;
	Greatsword.Base = TEXT("Weapon_Greatsword");
	Greatsword.GearLevel = 10;
	Greatsword.Affixes.Add(Rolled(TEXT("Stat_Flat_damage"), 7, 1.0f));

	const TMap<FName, TArray<FCataclysmStatModifier>> WeaponModifiers =
		UCataclysmItemModifiers::ModifiersFor(Greatsword, Bases, Affixes);

	TestTrue(TEXT("the Greatsword is recognised as two-handed"),
		UCataclysmItemModifiers::IsTwoHanded(Greatsword, Bases));

	// Its implicit and its affix are the same stat, so both land together.
	const TArray<FCataclysmStatModifier>* WeaponDamage =
		WeaponModifiers.Find(FName(TEXT("attack_damage")));
	if (WeaponDamage && WeaponDamage->Num() == 2)
	{
		TestTrue(FString::Printf(TEXT("its 78 implicit doubles to 156, got %.2f"),
				 (*WeaponDamage)[0].Value),
			FMath::IsNearlyEqual((*WeaponDamage)[0].Value, 156.0f, 0.05f));
		TestTrue(FString::Printf(TEXT("its 22 affix doubles to 44, got %.2f"),
				 (*WeaponDamage)[1].Value),
			FMath::IsNearlyEqual((*WeaponDamage)[1].Value, 44.0f, 0.05f));
	}
	else
	{
		AddError(TEXT("expected two attack_damage modifiers on the Greatsword"));
	}

	// The same affix on a one-handed weapon is not doubled.
	FCataclysmItem Axe;
	Axe.Base = TEXT("Weapon_Axe");
	Axe.GearLevel = 10;
	Axe.Affixes.Add(Rolled(TEXT("Stat_Flat_damage"), 7, 1.0f));

	const TMap<FName, TArray<FCataclysmStatModifier>> AxeModifiers =
		UCataclysmItemModifiers::ModifiersFor(Axe, Bases, Affixes);
	TestFalse(TEXT("an Axe is not two-handed"),
		UCataclysmItemModifiers::IsTwoHanded(Axe, Bases));

	const TArray<FCataclysmStatModifier>* AxeDamage =
		AxeModifiers.Find(FName(TEXT("attack_damage")));
	if (AxeDamage && AxeDamage->Num() == 2)
	{
		TestTrue(FString::Printf(TEXT("its 46 implicit stays 46, got %.2f"),
				 (*AxeDamage)[0].Value),
			FMath::IsNearlyEqual((*AxeDamage)[0].Value, 46.0f, 0.05f));
		TestTrue(FString::Printf(TEXT("its affix stays 22, got %.2f"),
				 (*AxeDamage)[1].Value),
			FMath::IsNearlyEqual((*AxeDamage)[1].Value, 22.0f, 0.05f));
	}
	else
	{
		AddError(TEXT("expected two attack_damage modifiers on the Axe"));
	}

	// An affix the table does not have is reported and skipped, not guessed at.
	FCataclysmItem Broken;
	Broken.Base = TEXT("Head_Helm");
	Broken.GearLevel = 10;
	Broken.Affixes.Add(Rolled(TEXT("Stat_This_Affix_Does_Not_Exist"), 7, 1.0f));

	AddExpectedError(TEXT("not in the affix table"),
		EAutomationExpectedErrorFlags::Contains, 1);
	const TMap<FName, TArray<FCataclysmStatModifier>> Partial =
		UCataclysmItemModifiers::ModifiersFor(Broken, Bases, Affixes);
	TestEqual(TEXT("only the implicit survives an unknown affix"), Partial.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemResistanceTest,
	"Cataclysm.Item.AResistanceFamilyBecomesOneModifierPerDamageType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemResistanceTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	UDataTable* Bases = LoadTable<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
	UDataTable* Affixes = LoadTable<FCataclysmAffixRow>(TEXT("Affixes.csv"));
	if (!Bases || !Affixes)
	{
		AddError(TEXT("could not load ItemBases.csv or Affixes.csv"));
		return false;
	}

	// An all-resistance affix covers all eight damage types, so it produces
	// eight modifiers rather than one. The affix says HOW MANY types; the item
	// says which, and a family covering all eight has no choice to make.
	FCataclysmItem Belt;
	Belt.Base = TEXT("Belt_Girdle");
	Belt.GearLevel = 10;
	Belt.Affixes.Add(Rolled(TEXT("Resistance_All_resistances"), 7, 1.0f));

	const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
		UCataclysmItemModifiers::ModifiersFor(Belt, Bases, Affixes);

	for (const FName& Type : UCataclysmItemModifiers::DamageTypeNames())
	{
		const FName Stat = UCataclysmItemModifiers::ResistanceStatFor(Type);
		const TArray<FCataclysmStatModifier>* Found = Modifiers.Find(Stat);
		TestTrue(FString::Printf(TEXT("%s got a modifier"), *Stat.ToString()),
			Found != nullptr && Found->Num() == 1);
		if (Found && Found->Num() == 1)
		{
			// 12% per resistance at tier 7 on +10 gear. It was 6 until issue
			// #1229 doubled the three resistance affixes, because the
			// difficulty tier now takes resistance off a player and a tier 8
			// character needs 145 rather than 70 to sit at the cap.
			TestTrue(FString::Printf(TEXT("%s is 12 points, got %.2f"),
					 *Stat.ToString(), (*Found)[0].Value),
				FMath::IsNearlyEqual((*Found)[0].Value, 12.0f, 0.05f));
		}
	}

	TestEqual(TEXT("the stat name is built from the damage type"),
		UCataclysmItemModifiers::ResistanceStatFor(TEXT("Demonic")),
		FName(TEXT("resistance_demonic")));

	// A narrower family must name its damage types, because which ones it covers
	// is decided when the item drops rather than by the affix.
	FCataclysmItem Ring;
	Ring.Base = TEXT("Ring_Loop");
	Ring.GearLevel = 10;
	FCataclysmRolledAffix Single = Rolled(TEXT("Resistance_Single_resistance"), 7, 1.0f);
	Ring.Affixes.Add(Single);

	AddExpectedError(TEXT("damage types but the item names"),
		EAutomationExpectedErrorFlags::Contains, 1);
	const TMap<FName, TArray<FCataclysmStatModifier>> Unnamed =
		UCataclysmItemModifiers::ModifiersFor(Ring, Bases, Affixes);
	TestFalse(TEXT("an unnamed single resistance grants nothing"),
		Unnamed.Contains(FName(TEXT("resistance_demonic"))));

	Single.DamageTypes.Add(TEXT("Demonic"));
	Ring.Affixes.Empty();
	Ring.Affixes.Add(Single);

	const TMap<FName, TArray<FCataclysmStatModifier>> Named =
		UCataclysmItemModifiers::ModifiersFor(Ring, Bases, Affixes);
	const TArray<FCataclysmStatModifier>* Demonic =
		Named.Find(FName(TEXT("resistance_demonic")));
	TestTrue(TEXT("naming the type makes it apply"),
		Demonic != nullptr && Demonic->Num() == 1);
	if (Demonic && Demonic->Num() == 1)
	{
		// 40 points for a single resistance at tier 7 on +10 gear, against 12
		// for the family covering all eight. Concentration is worth more per
		// type, and the RATIO is what this line is really about: both doubled
		// together for issue #1229, so it is unchanged at 3.33 to 1.
		TestTrue(FString::Printf(TEXT("a single resistance is 40 points, got %.2f"),
				 (*Demonic)[0].Value),
			FMath::IsNearlyEqual((*Demonic)[0].Value, 40.0f, 0.05f));
	}

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


// ---------------------------------------------------------------------------
// A weapon's own damage, looked up by weapon TYPE
// ---------------------------------------------------------------------------

/**
 * The number every skill's damage is a percentage of.
 *
 * WHAT THIS GUARDS. Issue #173: UCataclysmCombatAttributeSet::AttackDamage was
 * initialised to zero and never set, so a Heavy Attack at 250% of it dealt
 * nothing and the burn rider, being a share of the hit, applied nothing either.
 * The game ran normally and killed nothing.
 *
 * The two-handed doubling is checked explicitly, because leaving it out is a
 * silent halving whose only symptom is that everything hits softly.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWeaponDamageByTypeTest,
	"Cataclysm.Item.AWeaponTypeSuppliesItsOwnAttackDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWeaponDamageByTypeTest::RunTest(const FString& Parameters)
{
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!Bases)
	{
		AddError(TEXT("DT_ItemBases does not exist. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	// The three weapons the vertical slice designs, and their stated implicits
	// from the Item Bases sheet. Written here rather than read from the same
	// table under test, so a table that lost a value fails rather than agreeing
	// with itself.
	struct FCase
	{
		const TCHAR* Type;
		float Stated;
		bool bTwoHanded;
	};
	const FCase Cases[] = {
		{ TEXT("Greataxe"), 72.0f, true  },
		{ TEXT("Staff"),    66.0f, true  },
		{ TEXT("Fist"),     30.0f, false },
	};

	for (const FCase& Case : Cases)
	{
		// At gear level 10 the stated figure IS the answer, before doubling.
		const float Expected = Case.Stated * (Case.bTwoHanded ? 2.0f : 1.0f);
		const float Actual = UCataclysmItemModifiers::WeaponDamageForType(
			Bases, Case.Type, /*GearLevel=*/10);

		TestTrue(FString::Printf(
			TEXT("a +10 %s supplies %.0f attack damage, got %.2f"),
			Case.Type, Expected, Actual),
			FMath::IsNearlyEqual(Actual, Expected, 0.05f));

		// And a lower gear level supplies strictly less, which is what makes
		// upgrading a weapon worth anything.
		const float AtZero = UCataclysmItemModifiers::WeaponDamageForType(
			Bases, Case.Type, /*GearLevel=*/0);
		TestTrue(FString::Printf(
			TEXT("an unupgraded %s supplies less than a +10 one (%.2f < %.2f)"),
			Case.Type, AtZero, Actual), AtZero < Actual);
		TestTrue(FString::Printf(
			TEXT("and more than nothing (%.2f)"), AtZero), AtZero > 0.0f);
	}

	// A weapon type that is not a weapon base supplies nothing, rather than
	// returning an arbitrary row's value.
	TestEqual(TEXT("a type that is not a weapon supplies nothing"),
		UCataclysmItemModifiers::WeaponDamageForType(Bases, TEXT("Trombone"), 10),
		0.0f);
	TestEqual(TEXT("an empty type supplies nothing"),
		UCataclysmItemModifiers::WeaponDamageForType(Bases, FString(), 10), 0.0f);
	TestEqual(TEXT("no table supplies nothing"),
		UCataclysmItemModifiers::WeaponDamageForType(nullptr, TEXT("Greataxe"), 10),
		0.0f);

	return true;
}

// ---------------------------------------------------------------------------
// What the weapons actually worn are worth. Issue #840
// ---------------------------------------------------------------------------

/**
 * Two weapons SUM their damage, and each is read at its own upgrade level.
 *
 * THE FIGURES BELOW ARE QUOTED FROM THE DESIGN DOCUMENT, not recomputed here.
 * The Dual Wielding section of `docs/Cataclysm_GDD_v2.md` states: "an Axe with
 * an Axe at 92 and an Axe with a Sword at 86". Recomputing them from the same
 * reasoning the code uses would test nothing.
 *
 * WHAT WENT WRONG WITHOUT THIS. Issue #840, reported from play: equipping a
 * second whip changed nothing at all, because the game asked what one weapon
 * TYPE was worth and took the first occupied weapon slot's answer.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTwoWeaponsSumTheirDamage,
	"Cataclysm.Items.TwoWornWeaponsSumTheirDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTwoWeaponsSumTheirDamage::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	UDataTable* Bases = LoadTable<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
	if (!Bases)
	{
		AddError(TEXT("could not load ItemBases.csv"));
		return false;
	}

	// The stated figure in the sheet is the +10 one, so a fully upgraded weapon
	// is worth exactly what the design document quotes.
	const auto At10 = [](const TCHAR* Base)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.GearLevel = 10;
		return Item;
	};

	const FCataclysmItem Axe = At10(TEXT("Weapon_Axe"));
	const FCataclysmItem Sword = At10(TEXT("Weapon_Sword"));

	TestEqual(TEXT("an Axe alone carries the 46 the sheet states"),
		UCataclysmItemModifiers::WeaponDamageForItem(Axe, Bases), 46.0f, 0.01f);
	TestEqual(TEXT("a Sword alone carries 40"),
		UCataclysmItemModifiers::WeaponDamageForItem(Sword, Bases), 40.0f, 0.01f);

	// THE TWO FIGURES THE DESIGN DOCUMENT STATES.
	TestEqual(TEXT("an Axe with a Sword gives 86, as the design document says"),
		UCataclysmItemModifiers::BlendedWeaponDamage({Axe, Sword}, Bases),
		86.0f, 0.01f);
	TestEqual(TEXT("an Axe with an Axe gives 92, as the design document says"),
		UCataclysmItemModifiers::BlendedWeaponDamage({Axe, Axe}, Bases),
		92.0f, 0.01f);

	// A TWO-HANDED WEAPON IS WORTH DOUBLE ITS STATED FIGURE, and is one entry
	// rather than two, because it is stored in the first weapon slot alone.
	const FCataclysmItem Greatsword = At10(TEXT("Weapon_Greatsword"));
	TestEqual(TEXT("a Greatsword's stated 78 doubles to 156"),
		UCataclysmItemModifiers::BlendedWeaponDamage({Greatsword}, Bases),
		156.0f, 0.01f);

	// HOLDING NOTHING IS WORTH NOTHING, rather than being an error.
	TestEqual(TEXT("no weapons at all are worth nothing"),
		UCataclysmItemModifiers::BlendedWeaponDamage({}, Bases), 0.0f, 0.01f);

	return true;
}

/**
 * A worn weapon's upgrade level reaches its damage.
 *
 * WHAT WENT WRONG WITHOUT THIS. Issue #840: the component held a
 * `WeaponGearLevel` that nothing outside the automation tests ever set, so every
 * worn weapon was computed as a +0 one however upgraded it really was.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUpgradeLevelReachesWeaponDamage,
	"Cataclysm.Items.AWornWeaponsUpgradeLevelReachesItsDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmUpgradeLevelReachesWeaponDamage::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	UDataTable* Bases = LoadTable<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
	if (!Bases)
	{
		AddError(TEXT("could not load ItemBases.csv"));
		return false;
	}

	FCataclysmItem Fresh;
	Fresh.Base = FName(TEXT("Weapon_Whip"));
	Fresh.GearLevel = 0;

	FCataclysmItem Upgraded = Fresh;
	Upgraded.GearLevel = 10;

	const float AtZero = UCataclysmItemModifiers::WeaponDamageForItem(Fresh, Bases);
	const float AtTen = UCataclysmItemModifiers::WeaponDamageForItem(Upgraded, Bases);

	// The sheet states the +10 figure, and a Whip's is 32.
	TestEqual(TEXT("a +10 Whip carries the 32 the sheet states"),
		AtTen, 32.0f, 0.01f);

	// ABOUT 2.5 TIMES, which is the upgrade curve stated on FCataclysmItem
	// itself. It was 3.52 until 2026-09-03, when the gear level ladder was eased
	// for issue #1179; an implicit uses the same multiplier as an affix, so a
	// weapon's base damage moved with it.
	//
	// Asserted as a ratio rather than as a second literal, so this test does not
	// have to change when the curve is re-tuned -- only when the upgrade level
	// stops reaching the damage at all, which is the fault it is written for.
	// The figure itself is quoted because a curve that silently flattened to 1.0
	// would still be "a ratio".
	if (TestTrue(TEXT("a +0 Whip is worth something"), AtZero > 0.0f))
	{
		TestEqual(TEXT("and a +10 Whip is about 2.5 times a +0 one"),
			AtTen / AtZero, 2.5f, 0.05f);
	}

	// THE TWO WEAPONS IN A PAIR ARE READ SEPARATELY, so a player holding one
	// upgraded weapon and one fresh one gets the sum of those two and not twice
	// either of them.
	TestEqual(TEXT("a +10 Whip with a +0 Whip is the sum of the two"),
		UCataclysmItemModifiers::BlendedWeaponDamage({Upgraded, Fresh}, Bases),
		AtTen + AtZero, 0.01f);

	return true;
}

/**
 * Attack speed AVERAGES, and a weapon that arms nothing is left out of it.
 *
 * AVERAGING IS WHAT STOPS SUMMED DAMAGE BEING A STRICT ADVANTAGE. A character
 * holding two weapons hits harder per swing and does not also swing at the
 * faster weapon's rate. `attack_speed_of` in `sim/cataclysm_sim/affixes.py`
 * records that Path of Exile and Last Epoch both resolve it this way.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttackSpeedAverages,
	"Cataclysm.Items.TwoWornWeaponsAverageTheirAttackSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAttackSpeedAverages::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	UDataTable* Bases = LoadTable<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
	if (!Bases)
	{
		AddError(TEXT("could not load ItemBases.csv"));
		return false;
	}

	const auto Of = [](const TCHAR* Base)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		return Item;
	};

	const FCataclysmItem Axe = Of(TEXT("Weapon_Axe"));       // states 1.25
	const FCataclysmItem Sword = Of(TEXT("Weapon_Sword"));   // states 1.3
	const FCataclysmItem Shield = Of(TEXT("Weapon_Shield")); // states 1.2, arms nothing

	TestEqual(TEXT("one Axe swings at its own stated 1.25"),
		UCataclysmItemModifiers::BlendedAttackSpeed({Axe}, Bases), 1.25f, 0.001f);

	// THE MEAN OF THE TWO, NOT THE SUM AND NOT THE FASTER ONE.
	TestEqual(TEXT("an Axe with a Sword swings at 1.275, the mean of 1.25 and 1.3"),
		UCataclysmItemModifiers::BlendedAttackSpeed({Axe, Sword}, Bases),
		1.275f, 0.001f);

	// A SHIELD IS NOT SWUNG, so it neither adds damage nor drags the rate down.
	// This is the half a test checking only two real weapons would miss: an
	// average taken over everything worn would answer 1.25 here.
	TestFalse(TEXT("a Shield arms nothing"),
		UCataclysmItemModifiers::WeaponIsArmed(Shield, Bases));
	TestTrue(TEXT("a Sword does arm"),
		UCataclysmItemModifiers::WeaponIsArmed(Sword, Bases));

	TestEqual(TEXT("a Sword with a Shield still swings at the Sword's 1.3"),
		UCataclysmItemModifiers::BlendedAttackSpeed({Sword, Shield}, Bases),
		1.3f, 0.001f);
	TestEqual(TEXT("and is worth the Sword's damage alone"),
		UCataclysmItemModifiers::BlendedWeaponDamage({Sword, Shield}, Bases),
		UCataclysmItemModifiers::WeaponDamageForItem(Sword, Bases), 0.01f);

	// NOTHING ARMED ANSWERS ZERO rather than dividing by zero. The automatic
	// basic attack reads zero as never swinging.
	TestEqual(TEXT("a Shield on its own has no rate at all"),
		UCataclysmItemModifiers::BlendedAttackSpeed({Shield}, Bases), 0.0f, 0.001f);
	TestEqual(TEXT("and no weapons at all have none either"),
		UCataclysmItemModifiers::BlendedAttackSpeed({}, Bases), 0.0f, 0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// Hybrid affixes grant both of their halves. Issue #847
// ---------------------------------------------------------------------------

/**
 * A hybrid affix grants BOTH stats it names, each at seventy per cent.
 *
 * WHAT WENT WRONG WITHOUT THIS. Reported from play on 2026-08-23 as an affix
 * showing a value of zero. A hybrid row has no stat, no value kind and a top
 * value of 0.0 -- it names its two halves in two other columns -- so
 * `AccumulateInto` refused its empty value kind and skipped it in SILENCE. A
 * hybrid on a piece of gear granted the player nothing at all.
 *
 * THIRTEEN OF THE EIGHTY-FIVE AFFIXES IN THE DATA ARE HYBRIDS, so this was not
 * a rare corner.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHybridAffixGrantsBothHalves,
	"Cataclysm.Items.AHybridAffixGrantsBothOfItsHalves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHybridAffixGrantsBothHalves::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	UDataTable* Bases = LoadTable<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
	UDataTable* Affixes = LoadTable<FCataclysmAffixRow>(TEXT("Affixes.csv"));
	if (!Bases || !Affixes)
	{
		AddError(TEXT("could not load ItemBases.csv or Affixes.csv"));
		return false;
	}

	// A REAL HYBRID FROM THE DATA rather than an invented one, so a hybrid whose
	// halves stopped resolving would fail here rather than being tested against
	// a fixture that cannot go wrong. It pairs flat maximum health with flat
	// armour and rolls on a Belt among others.
	FCataclysmItem Belt;
	Belt.Base = FName(TEXT("Belt_Sash"));
	Belt.GearLevel = 10;

	FCataclysmRolledAffix Rolled;
	Rolled.Affix = FName(TEXT("Hybrid_Health_and_armor"));
	Rolled.Tier = UCataclysmItemValues::MaxAffixTier;
	Rolled.Roll = 1.0f;
	Belt.Affixes.Add(Rolled);

	const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
		UCataclysmItemModifiers::ModifiersFor(Belt, Bases, Affixes);

	const TArray<FCataclysmStatModifier>* Health =
		Modifiers.Find(FName(TEXT("max_health")));
	const TArray<FCataclysmStatModifier>* Armour =
		Modifiers.Find(FName(TEXT("armor")));

	if (!TestNotNull(TEXT("the hybrid grants maximum health"), Health)
		|| !TestNotNull(TEXT("and armour"), Armour))
	{
		return false;
	}

	if (!TestEqual(TEXT("one modifier for health"), Health->Num(), 1)
		|| !TestEqual(TEXT("and one for armour"), Armour->Num(), 1))
	{
		return false;
	}

	// NEITHER IS ZERO, which is the whole report. Before this they were not
	// small, they were absent.
	TestTrue(FString::Printf(TEXT("the health half is worth something, got %.2f"),
							 (*Health)[0].Value),
		(*Health)[0].Value > 0.0f);
	TestTrue(FString::Printf(TEXT("and so is the armour half, got %.2f"),
							 (*Armour)[0].Value),
		(*Armour)[0].Value > 0.0f);

	// THE SINGLE AFFIX TWO TIERS DOWN, since issue #2883 (D6), measured against
	// the single affix rather than against a number written here. It was 70%
	// of the single affix at the same tier; two tiers down on each half's own
	// whole-number ladder is the same share within a few per cent, and it keeps
	// a hybrid a compromise: more in total across two stats, less of either.
	const auto SingleHealthAt = [&](int32 Tier)
	{
		FCataclysmItem WholeHealth;
		WholeHealth.Base = Belt.Base;
		WholeHealth.GearLevel = Belt.GearLevel;
		FCataclysmRolledAffix Single;
		Single.Affix = FName(TEXT("Stat_Flat_maximum_health"));
		Single.Tier = Tier;
		Single.Roll = 1.0f;
		WholeHealth.Affixes.Add(Single);
		const TMap<FName, TArray<FCataclysmStatModifier>> WholeModifiers =
			UCataclysmItemModifiers::ModifiersFor(WholeHealth, Bases, Affixes);
		const TArray<FCataclysmStatModifier>* WholeHealthMods =
			WholeModifiers.Find(FName(TEXT("max_health")));
		return WholeHealthMods && WholeHealthMods->Num() == 1 ? (*WholeHealthMods)[0].Value : -1.0f;
	};

	const float TwoTiersDown = SingleHealthAt(
		UCataclysmItemValues::MaxAffixTier - UCataclysmItemValues::HybridTierDrop);
	const float SameTier = SingleHealthAt(UCataclysmItemValues::MaxAffixTier);
	if (TestTrue(TEXT("the single affix grants maximum health"), TwoTiersDown > 0.0f && SameTier > 0.0f))
	{
		TestEqual(TEXT("and the hybrid's half is the single affix two tiers down"),
			(*Health)[0].Value, TwoTiersDown, 0.001f);

		// AND STRICTLY LESS THAN THE WHOLE AT THE SAME TIER, stated separately
		// so that a tier drop accidentally set to 0 fails here as well as above.
		TestTrue(TEXT("which is less than the single affix grants at the same tier"),
			(*Health)[0].Value < SameTier);
	}

	return true;
}

// THE HYBRID SHARE TEST IS RETIRED WITH THE SHARE. `Cataclysm.Items.TheHybridShareIsTheResistanceRatio`
// held `UCataclysmItemValues::HybridFraction`, the 70% a hybrid half was worth, to the two-resistance affix's top
// value over the single one's. Since issue #2883 (D6) a hybrid half is its own affix two tiers down
// (`UCataclysmItemValues::HybridTierDrop`), and nothing reads HybridFraction; issue #2904 retires it with
// TopValue and Floor. `Cataclysm.Item.AHybridHalfIsItsOwnAffixTwoTiersDownAndATierBelowThreeReadsAsThree` and the
// test above hold the new rule.

/**
 * NO AFFIX IN THE DATA GRANTS NOTHING AT ALL.
 *
 * THIS IS THE GUARD THAT WOULD HAVE CAUGHT ISSUE #847 BEFORE A PLAY TEST DID.
 * Thirteen hybrid affixes granted no modifier whatsoever and nothing noticed,
 * because every test that existed asked what a PARTICULAR affix was worth and
 * none asked whether every affix was worth anything.
 *
 * ROLLED AT THE TOP TIER ON A FULLY UPGRADED PIECE, so a value of zero here
 * means the affix is broken rather than merely small. Issue #858 is the
 * separate question of affixes that are worth very little at the bottom.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEveryAffixGrantsSomething,
	"Cataclysm.Items.EveryAffixInTheDataGrantsSomething",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEveryAffixGrantsSomething::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	UDataTable* Bases = LoadTable<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
	UDataTable* Affixes = LoadTable<FCataclysmAffixRow>(TEXT("Affixes.csv"));
	if (!Bases || !Affixes)
	{
		AddError(TEXT("could not load ItemBases.csv or Affixes.csv"));
		return false;
	}

	// WHAT THE BARE PIECE IS WORTH, MEASURED FIRST. ModifiersFor returns the
	// base's own implicits as well as its affixes, and a Ring_Band carries a
	// flat attack damage implicit of 10. The first version of this test looked
	// for any non-zero modifier and therefore always found that implicit, so it
	// could not fail -- it passed while thirteen hybrid affixes granted nothing
	// at all, which is the exact fault it was written to catch.
	FCataclysmItem BareRing;
	BareRing.Base = FName(TEXT("Ring_Band"));
	BareRing.GearLevel = UCataclysmItemValues::MaxGearLevel;

	const auto TotalOf =
		[](const TMap<FName, TArray<FCataclysmStatModifier>>& Modifiers)
	{
		float Total = 0.0f;
		int32 Count = 0;
		for (const TPair<FName, TArray<FCataclysmStatModifier>>& Pair : Modifiers)
		{
			for (const FCataclysmStatModifier& Modifier : Pair.Value)
			{
				Total += Modifier.Value;
				++Count;
			}
		}
		return TPair<float, int32>(Total, Count);
	};

	const TPair<float, int32> Bare = TotalOf(
		UCataclysmItemModifiers::ModifiersFor(BareRing, Bases, Affixes));

	int32 Checked = 0;
	int32 HybridsChecked = 0;

	Affixes->ForeachRow<FCataclysmAffixRow>(
		TEXT("EveryAffixInTheDataGrantsSomething"),
		[&](const FName& Key, const FCataclysmAffixRow& Row)
		{
			// AN AILMENT AFFIX IS NO LONGER SKIPPED. Its chance has been a flat
			// modifier on a stat since issue #899, so it has to add one here like
			// any other affix. This said it "correctly produces no modifier".

			// A ring takes almost every affix in the pool and is not two-handed,
			// so nothing here is doubled and the figures are the plain ones.
			FCataclysmItem Ring;
			Ring.Base = FName(TEXT("Ring_Band"));
			Ring.GearLevel = UCataclysmItemValues::MaxGearLevel;

			FCataclysmRolledAffix Rolled;
			Rolled.Affix = Key;
			Rolled.Tier = UCataclysmItemValues::MaxAffixTier;
			Rolled.Roll = 1.0f;

			// A resistance family says how many types it covers and the item
			// says which, so it has to be given as many as it expects.
			if (Row.Breadth > 0)
			{
				const TArray<FName>& AllTypes =
					UCataclysmItemModifiers::DamageTypeNames();
				for (int32 Index = 0;
					 Index < Row.Breadth && Index < AllTypes.Num(); ++Index)
				{
					Rolled.DamageTypes.Add(AllTypes[Index]);
				}
			}

			Ring.Affixes.Add(Rolled);

			const TPair<float, int32> WithAffix = TotalOf(
				UCataclysmItemModifiers::ModifiersFor(Ring, Bases, Affixes));

			++Checked;
			if (Row.AffixKind.Equals(TEXT("Hybrid"), ESearchCase::IgnoreCase))
			{
				++HybridsChecked;
			}

			// MEASURED AGAINST THE BARE RING, so what is being asserted is what
			// the AFFIX added and not what the piece was already worth.
			TestTrue(FString::Printf(
				TEXT("%s adds a modifier, bare ring had %d and with it %d"),
				*Key.ToString(), Bare.Value, WithAffix.Value),
				WithAffix.Value > Bare.Value);

			TestTrue(FString::Printf(
				TEXT("%s adds some value at top tier on a +10 piece, %.4f to %.4f"),
				*Key.ToString(), Bare.Key, WithAffix.Key),
				WithAffix.Key > Bare.Key + 0.0001f);
		});

	// Without this the loop above passes on an empty table, which is what a
	// stale or unbuilt asset actually looks like.
	TestTrue(FString::Printf(TEXT("most of the affix pool was checked, %d of it"),
							 Checked),
		Checked >= 60);

	// AND THE HYBRIDS SPECIFICALLY WERE AMONG THEM. There are thirteen in the
	// data and they are the ones that were granting nothing, so a run that
	// quietly skipped them would report success having checked the affixes that
	// already worked.
	TestTrue(FString::Printf(TEXT("the hybrid affixes were checked, %d of them"),
							 HybridsChecked),
		HybridsChecked >= 13);

	return true;
}

/**
 * AN AILMENT AFFIX GRANTS ITS CHANCE, AND A TWO-HANDED WEAPON DOUBLES IT. Issue
 * #899.
 *
 * THE FIGURE IS THE DESIGN DOCUMENT'S, "Chance to bleed | 15%" at the top tier,
 * on a fully upgraded piece. Until #899 the affix granted no modifier at all.
 *
 * AND THE DOUBLING IS THE DESIGN DOCUMENT'S TOO: "A two-handed weapon multiplies
 * both its implicit values and every affix rolled on it by 2." The tooltip
 * printed the doubled chance already. The simulation does not double it, which
 * is issue #1575.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAilmentAffixGrantsItsChance,
	"Cataclysm.Items.AnAilmentAffixGrantsItsChanceAndATwoHandedWeaponDoublesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAilmentAffixGrantsItsChance::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	UDataTable* Bases = LoadTable<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
	UDataTable* Affixes = LoadTable<FCataclysmAffixRow>(TEXT("Affixes.csv"));
	if (!Bases || !Affixes)
	{
		AddError(TEXT("could not load ItemBases.csv or Affixes.csv"));
		return false;
	}

	// ONE PIECE CARRYING ONE TOP-TIER CHANCE TO BLEED, and what it grants.
	const auto ChanceToBleedOn = [this, Bases, Affixes](const TCHAR* Base)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.GearLevel = UCataclysmItemValues::MaxGearLevel;

		FCataclysmRolledAffix Rolled;
		Rolled.Affix = FName(TEXT("Ailment_Chance_to_bleed"));
		Rolled.Tier = UCataclysmItemValues::MaxAffixTier;
		Rolled.Roll = 1.0f;
		Item.Affixes.Add(Rolled);

		float Total = 0.0f;
		const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
			UCataclysmItemModifiers::ModifiersFor(Item, Bases, Affixes);
		if (const TArray<FCataclysmStatModifier>* OnBleed =
				Modifiers.Find(FName(TEXT("bleed_chance"))))
		{
			for (const FCataclysmStatModifier& Each : *OnBleed)
			{
				TestTrue(FString::Printf(TEXT("the chance on %s is a flat modifier"),
										 Base),
					Each.Bucket == ECataclysmStatBucket::Flat);
				Total += Each.Value;
			}
		}
		return Total;
	};

	TestEqual(TEXT("a top-tier Chance to bleed on a +10 ring grants 15"),
		ChanceToBleedOn(TEXT("Ring_Band")), 15.0f, 0.01f);

	TestEqual(TEXT("and on a +10 Greatsword, which is two-handed, 30"),
		ChanceToBleedOn(TEXT("Weapon_Greatsword")),
		15.0f * UCataclysmItemValues::TwoHandedMultiplier, 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
