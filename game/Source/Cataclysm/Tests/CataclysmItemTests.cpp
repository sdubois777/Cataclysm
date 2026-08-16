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

#endif // WITH_AUTOMATION_TESTS
