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
			// 6% per resistance at tier 7 on +10 gear.
			TestTrue(FString::Printf(TEXT("%s is 6 points, got %.2f"),
					 *Stat.ToString(), (*Found)[0].Value),
				FMath::IsNearlyEqual((*Found)[0].Value, 6.0f, 0.05f));
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
		// 20 points for a single resistance at tier 7 on +10 gear, against 6 for
		// the family covering all eight. Concentration is worth more per type.
		TestTrue(FString::Printf(TEXT("a single resistance is 20 points, got %.2f"),
				 (*Demonic)[0].Value),
			FMath::IsNearlyEqual((*Demonic)[0].Value, 20.0f, 0.05f));
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

	// ABOUT 3.52 TIMES, which is the upgrade curve stated on FCataclysmItem
	// itself. Asserted as a ratio rather than as a second literal, so this test
	// does not have to change when the curve is re-tuned -- only when the
	// upgrade level stops reaching the damage at all, which is the fault.
	if (TestTrue(TEXT("a +0 Whip is worth something"), AtZero > 0.0f))
	{
		TestEqual(TEXT("and a +10 Whip is about 3.52 times a +0 one"),
			AtTen / AtZero, 3.52f, 0.05f);
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

	// SEVENTY PER CENT OF WHAT THE WHOLE AFFIX WOULD GRANT, measured against the
	// single affix rather than against a number written here. That is what makes
	// a hybrid a compromise: more in total across two stats, less of either.
	FCataclysmItem WholeHealth;
	WholeHealth.Base = Belt.Base;
	WholeHealth.GearLevel = Belt.GearLevel;
	FCataclysmRolledAffix Single;
	Single.Affix = FName(TEXT("Stat_Flat_maximum_health"));
	Single.Tier = UCataclysmItemValues::MaxAffixTier;
	Single.Roll = 1.0f;
	WholeHealth.Affixes.Add(Single);

	const TMap<FName, TArray<FCataclysmStatModifier>> WholeModifiers =
		UCataclysmItemModifiers::ModifiersFor(WholeHealth, Bases, Affixes);
	const TArray<FCataclysmStatModifier>* WholeHealthMods =
		WholeModifiers.Find(FName(TEXT("max_health")));

	if (TestNotNull(TEXT("the single affix grants maximum health"), WholeHealthMods)
		&& WholeHealthMods->Num() == 1)
	{
		TestEqual(TEXT("and the hybrid's half is 70% of it"),
			(*Health)[0].Value,
			(*WholeHealthMods)[0].Value * UCataclysmItemValues::HybridFraction,
			0.01f);

		// AND STRICTLY LESS THAN THE WHOLE, stated separately so that a
		// HybridFraction accidentally set to 1.0 fails here as well as above.
		TestTrue(TEXT("which is less than the single affix grants"),
			(*Health)[0].Value < (*WholeHealthMods)[0].Value);
	}

	return true;
}

/**
 * The hybrid share is the ratio the project already set, not a number picked.
 *
 * `sim/cataclysm_sim/affixes.py` derives it rather than writing it down, and
 * says why: it is the ratio between the two-resistance affix and the
 * single-resistance one, so the whole pool moves together if that ratio ever
 * changes. The game holds it as a constant for speed, and this is what stops
 * the two drifting apart in silence.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHybridShareIsTheResistanceRatio,
	"Cataclysm.Items.TheHybridShareIsTheResistanceRatio",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHybridShareIsTheResistanceRatio::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemTest;

	UDataTable* Affixes = LoadTable<FCataclysmAffixRow>(TEXT("Affixes.csv"));
	if (!Affixes)
	{
		AddError(TEXT("could not load Affixes.csv"));
		return false;
	}

	const FCataclysmAffixRow* Single = Affixes->FindRow<FCataclysmAffixRow>(
		FName(TEXT("Resistance_Single_resistance")), TEXT("HybridShare"),
		/*bWarnIfMissing=*/false);
	const FCataclysmAffixRow* Two = Affixes->FindRow<FCataclysmAffixRow>(
		FName(TEXT("Resistance_Two_resistances")), TEXT("HybridShare"),
		/*bWarnIfMissing=*/false);

	if (!TestNotNull(TEXT("the single resistance affix is in the data"), Single)
		|| !TestNotNull(TEXT("and the two-resistance one"), Two))
	{
		return false;
	}

	if (!TestTrue(TEXT("the single resistance affix is worth something"),
			Single->TopValue > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("the hybrid share is the two-resistance affix over the single"),
		UCataclysmItemValues::HybridFraction,
		Two->TopValue / Single->TopValue, 0.0001f);

	return true;
}

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
			// AN AILMENT AFFIX GRANTS A CHANCE AT AN EFFECT AND NOT A STAT, so
			// it correctly produces no modifier and is not a failure. It is
			// applied where the hit is resolved.
			if (Row.AffixKind.Equals(TEXT("Ailment"), ESearchCase::IgnoreCase))
			{
				return;
			}

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

#endif // WITH_AUTOMATION_TESTS
