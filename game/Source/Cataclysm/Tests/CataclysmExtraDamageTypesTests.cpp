// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmCharacterCreation.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Interface/CataclysmItemTooltip.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmItem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * "This weapon has 2-4 damage types": the damage types a weapon's enchantment adds when it drops. Ruled 2026-10-25
 * (Q-D2 to Q-D6) and, for the tier, by the project owner the same day: no tier caps a weapon's damage types.
 *
 * WHAT IS READ. `UCataclysmDropRoll::DamageTypesCarriedBy` reads the item's drawn enchantments' effect rows on
 * `damage_types_carried` at the item's own roll and rounds; `TopUpDamageTypes` adds types until the weapon holds that
 * many, never taking any away, capped by its base's limit and the types designed for its weapon type; `RollItem` does
 * both last of all when it is handed an effect table.
 *
 * THE ROW IS MADE UP HERE. The real row is the enchantment session's to write; these tests build the row the session
 * will author (flat 2 to 4 on the sentence) in a table of their own, so they stand without it.
 *
 * WHAT IS NOT REACHED. A character wielding such a weapon: nothing in play reads a worn weapon's `DamageTypes` yet (the
 * passive trees and the skills follow the one damage type chosen at creation, issue #857), so the tree test below asks
 * the tree rules about the weapon's own list and says so.
 *
 * EVERY DRAW IS SEEDED.
 */
namespace CataclysmExtraDamageTypesTest
{
	using FDrop = UCataclysmDropRoll;

	/** The sentence's row name, derived by the generator's `row_name` from "This weapon has 2-4 damage types". */
	const TCHAR* SentenceRow = TEXT("Positive_This_weapon_has_2_4_damage_types");

	/** A generated table read from `game/Data/`, so the tests read the real data. */
	template <typename RowType>
	UDataTable* LoadCsv(const TCHAR* FileName)
	{
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *(FPaths::ProjectDir() / TEXT("Data") / FileName)))
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

	/** The row the enchantment session will author: flat 2 to 4 on `damage_types_carried`, for the sentence. */
	UDataTable* EffectTableWithTheRow()
	{
		UDataTable* Effects = NewObject<UDataTable>();
		Effects->RowStruct = FCataclysmEnchantmentEffectRow::StaticStruct();
		FCataclysmEnchantmentEffectRow Row;
		Row.Enchantment = SentenceRow;
		Row.Stat = FDrop::DamageTypesCarriedStat;
		Row.ValueKind = TEXT("flat");
		Row.ValueLow = 2.0f;
		Row.ValueHigh = 4.0f;
		Effects->AddRow(FName(*FString::Printf(TEXT("%s#1"), SentenceRow)), Row);
		return Effects;
	}

	/** An effect table with no row at all, the control. */
	UDataTable* EmptyEffectTable()
	{
		UDataTable* Effects = NewObject<UDataTable>();
		Effects->RowStruct = FCataclysmEnchantmentEffectRow::StaticStruct();
		return Effects;
	}

	/** One enchantment whose positive half is the sentence, at a roll. */
	TArray<FCataclysmRolledEnchantment> TheSentenceAt(float Roll)
	{
		FCataclysmRolledEnchantment Rolled;
		Rolled.Positive = FName(SentenceRow);
		Rolled.Negative = FName(TEXT("Negative_Made_up"));
		Rolled.PositiveRoll = Roll;
		return {Rolled};
	}

	/** A weapon base row by its weapon type, out of the real bases. */
	const FCataclysmItemBaseRow* BaseFor(const UDataTable* Bases, const FString& WeaponType)
	{
		const FCataclysmItemBaseRow* Found = nullptr;
		if (Bases)
		{
			Bases->ForeachRow<FCataclysmItemBaseRow>(TEXT("BaseFor"),
				[&](const FName&, const FCataclysmItemBaseRow& Row)
				{
					if (!Found && Row.WeaponType == WeaponType)
					{
						Found = &Row;
					}
				});
		}
		return Found;
	}

	/** Everything an item holds, as one line, so two rolls can be compared whole. */
	FString Describe(const FCataclysmItem& Item, bool bWithDamageTypes = true)
	{
		FString Out = FString::Printf(TEXT("%s +%d sockets %d residue %.4f enchantments %d:"), *Item.Base.ToString(),
									  Item.GearLevel, Item.Sockets, Item.Residue, Item.EnchantmentCount);
		if (bWithDamageTypes)
		{
			for (const FName& Each : Item.DamageTypes)
			{
				Out += TEXT(" ") + Each.ToString();
			}
		}
		for (const FCataclysmRolledAffix& Affix : Item.Affixes)
		{
			Out += FString::Printf(TEXT(" | %s T%d %.6f"), *Affix.Affix.ToString(), Affix.Tier, Affix.Roll);
			for (const FName& Each : Affix.DamageTypes)
			{
				Out += TEXT(" ") + Each.ToString();
			}
		}
		for (const FCataclysmRolledEnchantment& Rolled : Item.Enchantments)
		{
			Out += FString::Printf(TEXT(" | %s %.6f / %s %.6f"), *Rolled.Positive.ToString(), Rolled.PositiveRoll,
								   *Rolled.Negative.ToString(), Rolled.NegativeRoll);
		}
		return Out;
	}

	/** Every table a whole-item roll needs, from `game/Data/`. */
	struct FTables
	{
		UDataTable* Bases = nullptr;
		UDataTable* Affixes = nullptr;
		UDataTable* Rarities = nullptr;
		UDataTable* Sockets = nullptr;
		UDataTable* AffixTiers = nullptr;
		UDataTable* WeaponSkills = nullptr;
		UDataTable* Positive = nullptr;
		UDataTable* Negative = nullptr;

		bool AllPresent() const
		{
			return Bases && Affixes && Rarities && Sockets && AffixTiers && WeaponSkills && Positive && Negative;
		}
	};

	FTables LoadEverything()
	{
		FTables Out;
		Out.Bases = LoadCsv<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
		Out.Affixes = LoadCsv<FCataclysmAffixRow>(TEXT("Affixes.csv"));
		Out.Rarities = LoadCsv<FCataclysmGearRarityRow>(TEXT("GearRarity.csv"));
		Out.Sockets = LoadCsv<FCataclysmItemSocketRow>(TEXT("ItemSockets.csv"));
		Out.AffixTiers = LoadCsv<FCataclysmAffixTierRow>(TEXT("AffixTiers.csv"));
		Out.WeaponSkills = LoadCsv<FCataclysmWeaponSkillRow>(TEXT("WeaponSkills.csv"));
		Out.Positive = LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
		Out.Negative = LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
		return Out;
	}

	/**
	 * A positive table holding the real sentence and three copies of it under made-up names, so every enchantment a
	 * weapon draws is weight 1 and one in four of them is the sentence. The copies have no effect row.
	 */
	UDataTable* PositivesHoldingTheSentence(const UDataTable* RealPositives)
	{
		const FCataclysmEnchantmentRow* Sentence = RealPositives
			? RealPositives->FindRow<FCataclysmEnchantmentRow>(FName(SentenceRow), TEXT("PositivesHoldingTheSentence"),
															   /*bWarnIfMissing=*/false)
			: nullptr;
		if (!Sentence)
		{
			return nullptr;
		}
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmEnchantmentRow::StaticStruct();
		Table->AddRow(FName(SentenceRow), *Sentence);
		for (int32 Copy = 1; Copy <= 3; ++Copy)
		{
			Table->AddRow(FName(*FString::Printf(TEXT("Positive_Made_up_weight_one_%d"), Copy)), *Sentence);
		}
		return Table;
	}

	bool Roll(const FTables& Tables, const UDataTable* Positive, int32 Seed, int32 Tier, const UDataTable* Effects,
			  FCataclysmItem& OutItem)
	{
		FRandomStream Stream(Seed);
		return FDrop::RollItem(Tables.Bases, Tables.Affixes, Tables.Rarities, Tables.Sockets, Tables.AffixTiers,
							   Tables.WeaponSkills, Positive, Tables.Negative, TEXT("Weapon"), Tier,
							   /*MagicFind=*/500.0f, Stream, OutItem, /*bChaoticTiers=*/false, Effects);
	}

	/** Whether every type is designed for the weapon type, and none repeats. */
	bool AllDesignedAndDistinct(const TArray<FName>& Types, const TArray<FName>& Designed)
	{
		TSet<FName> Seen;
		for (const FName& Each : Types)
		{
			if (!Designed.Contains(Each) || Seen.Contains(Each))
			{
				return false;
			}
			Seen.Add(Each);
		}
		return true;
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY: this module is built as a unity blob.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExtraDamageTypesRoundedCountTest,
	"Cataclysm.ExtraDamageTypes.AWeaponDrawnWithTheSentenceHoldsTheRoundedCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The count is the row's figure at the item's own roll, rounded (Q-D2), and a weapon drawn with the sentence holds it,
 * at tier 1 as at any tier (the owner's ruling). Fails if the row is not read, the roll not used, the rounding not
 * applied, the top-up missing, or a tier capping it.
 */
bool FCataclysmExtraDamageTypesRoundedCountTest::RunTest(const FString&)
{
	using namespace CataclysmExtraDamageTypesTest;

	const FTables Tables = LoadEverything();
	if (!TestTrue(TEXT("every table loads"), Tables.AllPresent()))
	{
		return false;
	}
	UDataTable* Effects = EffectTableWithTheRow();

	// THE CONTROL: no row stating the stat asks for no damage types.
	TestEqual(TEXT("control: with no row the sentence asks for none"),
			  FDrop::DamageTypesCarriedBy(TheSentenceAt(0.0f), EmptyEffectTable()), 0);
	TestEqual(TEXT("at a roll of 0 it asks for 2"), FDrop::DamageTypesCarriedBy(TheSentenceAt(0.0f), Effects), 2);
	TestEqual(TEXT("at a roll of 0.5 it asks for 3"), FDrop::DamageTypesCarriedBy(TheSentenceAt(0.5f), Effects), 3);
	TestEqual(TEXT("at a roll of 1 it asks for 4"), FDrop::DamageTypesCarriedBy(TheSentenceAt(1.0f), Effects), 4);

	// A SWORD HOLDING ONE TYPE, TOPPED UP TO EACH COUNT; NO TIER IS ASKED. A Sword's base allows 4 and 7 are designed.
	const FCataclysmItemBaseRow* Sword = BaseFor(Tables.Bases, TEXT("Sword"));
	if (!TestNotNull(TEXT("a Sword base"), Sword))
	{
		return false;
	}
	const TArray<FName> Designed = FDrop::DamageTypesAvailableTo(Tables.WeaponSkills, Sword->WeaponType);
	TestTrue(TEXT("set-up: more types are designed for a Sword than its base allows"), Designed.Num() > 4);
	for (int32 Count = 2; Count <= 4; ++Count)
	{
		TArray<FName> Types = {Designed[0]};
		FRandomStream Stream(1815 + Count);
		FDrop::TopUpDamageTypes(Tables.WeaponSkills, *Sword, Count, Stream, Types);
		TestEqual(*FString::Printf(TEXT("a Sword topped up to %d holds %d"), Count, Count), Types.Num(),
				  Count);
		TestEqual(*FString::Printf(TEXT("and keeps the type it held first, at %d"), Count), Types[0], Designed[0]);
		TestTrue(*FString::Printf(TEXT("and every type is designed for it and none repeats, at %d"), Count),
				 AllDesignedAndDistinct(Types, Designed));
	}

	// AND A WHOLE DROP AT TIER 1. The same seed rolled with the row and without it: the first is the second with types
	// added, never fewer, to the count its own roll asks for, capped by the base and the designed types.
	UDataTable* Positive = PositivesHoldingTheSentence(Tables.Positive);
	if (!TestNotNull(TEXT("set-up: the sentence is in the real positive table"), Positive))
	{
		return false;
	}
	int32 Found = 0;
	for (int32 Seed = 1; Seed <= 20000 && Found < 3; ++Seed)
	{
		FCataclysmItem With;
		FCataclysmItem Without;
		if (!Roll(Tables, Positive, Seed, 1, Effects, With) || !Roll(Tables, Positive, Seed, 1, nullptr, Without))
		{
			AddError(FString::Printf(TEXT("seed %d failed to roll a weapon"), Seed));
			return false;
		}
		const int32 Count = FDrop::DamageTypesCarriedBy(With.Enchantments, Effects);
		if (Count <= 0)
		{
			continue;
		}
		++Found;
		const FCataclysmItemBaseRow* Base = Tables.Bases->FindRow<FCataclysmItemBaseRow>(
			With.Base, TEXT("ExtraDamageTypes"), /*bWarnIfMissing=*/false);
		if (!TestNotNull(TEXT("the drop's base is in the table"), Base))
		{
			return false;
		}
		const TArray<FName> ForIt = FDrop::DamageTypesAvailableTo(Tables.WeaponSkills, Base->WeaponType);
		const int32 Expected =
			FMath::Max(Without.DamageTypes.Num(), FMath::Min(Count, FDrop::MaxDamageTypesFor(*Base, ForIt.Num())));
		TestEqual(*FString::Printf(TEXT("seed %d, a %s asking for %d holds %d"), Seed, *Base->WeaponType, Count,
								   Expected),
				  With.DamageTypes.Num(), Expected);
		bool bPrefix = With.DamageTypes.Num() >= Without.DamageTypes.Num();
		for (int32 Index = 0; bPrefix && Index < Without.DamageTypes.Num(); ++Index)
		{
			bPrefix = With.DamageTypes[Index] == Without.DamageTypes[Index];
		}
		TestTrue(*FString::Printf(TEXT("seed %d: the types it rolled first are kept, in order"), Seed), bPrefix);
		TestEqual(*FString::Printf(TEXT("seed %d: nothing else about the item differs"), Seed),
				  Describe(With, /*bWithDamageTypes=*/false), Describe(Without, /*bWithDamageTypes=*/false));
	}
	TestTrue(*FString::Printf(TEXT("the search found drops carrying the sentence (%d)"), Found), Found > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExtraDamageTypesNeverLowerTest,
	"Cataclysm.ExtraDamageTypes.AWeaponAlreadyHoldingTheCountIsUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Never lower (Q-D3), and nothing is drawn when nothing is added. Fails if the top-up replaces or trims. */
bool FCataclysmExtraDamageTypesNeverLowerTest::RunTest(const FString&)
{
	using namespace CataclysmExtraDamageTypesTest;

	const FTables Tables = LoadEverything();
	if (!TestTrue(TEXT("every table loads"), Tables.AllPresent()))
	{
		return false;
	}
	const FCataclysmItemBaseRow* Sword = BaseFor(Tables.Bases, TEXT("Sword"));
	if (!TestNotNull(TEXT("a Sword base"), Sword))
	{
		return false;
	}
	const TArray<FName> Designed = FDrop::DamageTypesAvailableTo(Tables.WeaponSkills, Sword->WeaponType);
	const TArray<FName> Four = {Designed[3], Designed[1], Designed[0], Designed[2]};

	// THE CONTROL: a Sword holding two, asked for four, does change.
	TArray<FName> Two = {Designed[0], Designed[1]};
	FRandomStream ControlStream(2026);
	FDrop::TopUpDamageTypes(Tables.WeaponSkills, *Sword, 4, ControlStream, Two);
	TestEqual(TEXT("control: a Sword holding two and asked for four holds four"), Two.Num(), 4);

	for (int32 Count = 2; Count <= 4; ++Count)
	{
		TArray<FName> Held = Four;
		FRandomStream Stream(2026);
		FRandomStream Untouched(2026);
		FDrop::TopUpDamageTypes(Tables.WeaponSkills, *Sword, Count, Stream, Held);
		TestTrue(*FString::Printf(TEXT("a Sword holding four and asked for %d holds the same four, in order"), Count),
				 Held == Four);
		TestEqual(*FString::Printf(TEXT("and takes nothing off the stream, at %d"), Count), Stream.GetCurrentSeed(),
				  Untouched.GetCurrentSeed());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExtraDamageTypesDesignedCapTest,
	"Cataclysm.ExtraDamageTypes.AShieldWithThreeDesignedTypesHoldsThreeAtACountOfFour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Capped by the designed types. Fails if the top-up is missing (1) or passes the designed types (4). */
bool FCataclysmExtraDamageTypesDesignedCapTest::RunTest(const FString&)
{
	using namespace CataclysmExtraDamageTypesTest;

	const FTables Tables = LoadEverything();
	if (!TestTrue(TEXT("every table loads"), Tables.AllPresent()))
	{
		return false;
	}
	const FCataclysmItemBaseRow* Shield = BaseFor(Tables.Bases, TEXT("Shield"));
	const FCataclysmItemBaseRow* Sword = BaseFor(Tables.Bases, TEXT("Sword"));
	if (!TestNotNull(TEXT("a Shield base"), Shield) || !TestNotNull(TEXT("a Sword base"), Sword))
	{
		return false;
	}
	const TArray<FName> ForAShield = FDrop::DamageTypesAvailableTo(Tables.WeaponSkills, Shield->WeaponType);
	TestEqual(TEXT("set-up: three damage types are designed for a Shield"), ForAShield.Num(), 3);
	TestEqual(TEXT("set-up: and its base allows four"), Shield->MaxDamageTypes, 4);

	TArray<FName> Types = {ForAShield[0]};
	FRandomStream Stream(3);
	FDrop::TopUpDamageTypes(Tables.WeaponSkills, *Shield, 4, Stream, Types);
	TestEqual(TEXT("a Shield holding one and asked for four holds three"), Types.Num(), 3);
	TestTrue(TEXT("and they are its three designed types"), AllDesignedAndDistinct(Types, ForAShield));

	// THE CONTROL: a Sword asked for four does hold four, so three above is the Shield's cap and not a cap of three.
	const TArray<FName> ForASword = FDrop::DamageTypesAvailableTo(Tables.WeaponSkills, Sword->WeaponType);
	TArray<FName> SwordTypes = {ForASword[0]};
	FRandomStream SwordStream(3);
	FDrop::TopUpDamageTypes(Tables.WeaponSkills, *Sword, 4, SwordStream, SwordTypes);
	TestEqual(TEXT("control: a Sword holding one and asked for four holds four"), SwordTypes.Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExtraDamageTypesSameSeedTest,
	"Cataclysm.ExtraDamageTypes.WithoutTheSentenceASeedGivesTheSameItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The real tables, with the effect table handed over and without it: every weapon that did not draw the sentence is
 * the same item, types, affixes and enchantments. Fails if the change draws anything for a drop without the sentence.
 */
bool FCataclysmExtraDamageTypesSameSeedTest::RunTest(const FString&)
{
	using namespace CataclysmExtraDamageTypesTest;

	const FTables Tables = LoadEverything();
	if (!TestTrue(TEXT("every table loads"), Tables.AllPresent()))
	{
		return false;
	}
	UDataTable* Effects = EffectTableWithTheRow();

	int32 Compared = 0;
	int32 Different = 0;
	int32 WithEnchantments = 0;
	for (int32 Tier : {1, 8})
	{
		for (int32 Seed = 1; Seed <= 300; ++Seed)
		{
			FCataclysmItem With;
			FCataclysmItem Without;
			if (!Roll(Tables, Tables.Positive, Seed, Tier, Effects, With)
				|| !Roll(Tables, Tables.Positive, Seed, Tier, nullptr, Without))
			{
				AddError(FString::Printf(TEXT("seed %d failed to roll a weapon at tier %d"), Seed, Tier));
				return false;
			}
			if (FDrop::DamageTypesCarriedBy(With.Enchantments, Effects) > 0)
			{
				continue;
			}
			++Compared;
			WithEnchantments += With.Enchantments.Num() > 0 ? 1 : 0;
			if (Describe(With) != Describe(Without))
			{
				++Different;
				AddError(FString::Printf(TEXT("seed %d at tier %d differs: %s against %s"), Seed, Tier,
										 *Describe(With), *Describe(Without)));
			}
		}
	}
	TestEqual(TEXT("every weapon without the sentence is the same item with the effect table as without"), Different,
			  0);
	// THE RUN HAS TO HAVE COMPARED ENCHANTED WEAPONS, or it compared nothing the change could touch.
	TestTrue(*FString::Printf(TEXT("it compared weapons carrying enchantments (%d of %d)"), WithEnchantments, Compared),
			 WithEnchantments > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExtraDamageTypesTreesTest,
	"Cataclysm.ExtraDamageTypes.TheTreeRulesOpenTheAddedTypesThreeTreesForTheWeaponsList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The added type's three class trees are reachable for the weapon's own list of types, and were not before it.
 *
 * THE TREE RULES ONLY, NOT A CHARACTER. Nothing in play hands a worn weapon's `DamageTypes` to the trees or the
 * skills yet; they follow the damage type chosen at creation (`ACataclysmPlayerState::ReachableTrees`, issue #857).
 * Fails if the top-up adds a name the tree rules do not know.
 */
bool FCataclysmExtraDamageTypesTreesTest::RunTest(const FString&)
{
	using namespace CataclysmExtraDamageTypesTest;

	const FTables Tables = LoadEverything();
	if (!TestTrue(TEXT("every table loads"), Tables.AllPresent()))
	{
		return false;
	}
	const FCataclysmItemBaseRow* Sword = BaseFor(Tables.Bases, TEXT("Sword"));
	if (!TestNotNull(TEXT("a Sword base"), Sword))
	{
		return false;
	}
	const TArray<FName> Designed = FDrop::DamageTypesAvailableTo(Tables.WeaponSkills, Sword->WeaponType);
	const TArray<FName> Before = {Designed[0]};
	TArray<FName> After = Before;
	FRandomStream Stream(857);
	FDrop::TopUpDamageTypes(Tables.WeaponSkills, *Sword, 2, Stream, After);
	if (!TestEqual(TEXT("set-up: the Sword holds two"), After.Num(), 2))
	{
		return false;
	}
	const FName Added = After[1];
	const TArray<FName>& Classes = UCataclysmCharacterCreation::ClassesFor(Added);
	TestEqual(*FString::Printf(TEXT("set-up: %s unlocks three classes"), *Added.ToString()), Classes.Num(), 3);
	for (const FName& Class : Classes)
	{
		TestFalse(*FString::Printf(TEXT("control: before the top-up the %s tree is not reachable"), *Class.ToString()),
				  UCataclysmPassiveTree::TreeIsReachable(Class.ToString(), Before));
		TestTrue(*FString::Printf(TEXT("after it the %s tree is reachable"), *Class.ToString()),
				 UCataclysmPassiveTree::TreeIsReachable(Class.ToString(), After));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExtraDamageTypesTooltipTest,
	"Cataclysm.ExtraDamageTypes.TheTooltipListsEveryTypeTheWeaponHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The tool tip's weapon lines (`UCataclysmItemTooltip::WeaponLines`, which already lists a weapon's damage types) list
 * every type the top-up added. Fails if the added types are not on the item the tool tip reads.
 */
bool FCataclysmExtraDamageTypesTooltipTest::RunTest(const FString&)
{
	using namespace CataclysmExtraDamageTypesTest;

	const FTables Tables = LoadEverything();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestTrue(TEXT("every table loads"), Tables.AllPresent()) || !TestNotNull(TEXT("the base table asset"), Bases))
	{
		return false;
	}
	const FCataclysmItemBaseRow* Sword = BaseFor(Tables.Bases, TEXT("Sword"));
	if (!TestNotNull(TEXT("a Sword base"), Sword))
	{
		return false;
	}
	FName SwordBase;
	Tables.Bases->ForeachRow<FCataclysmItemBaseRow>(TEXT("ExtraDamageTypes"),
		[&](const FName& Name, const FCataclysmItemBaseRow& Row)
		{
			if (SwordBase.IsNone() && Row.WeaponType == TEXT("Sword"))
			{
				SwordBase = Name;
			}
		});
	const TArray<FName> Designed = FDrop::DamageTypesAvailableTo(Tables.WeaponSkills, Sword->WeaponType);

	FCataclysmItem Item;
	Item.Base = SwordBase;
	Item.DamageTypes = {Designed[0]};
	const auto Joined = [](const TArray<FName>& Types)
	{
		return FString::JoinBy(Types, TEXT(", "), [](const FName& Name) { return Name.ToString(); });
	};
	TestTrue(*FString::Printf(TEXT("control: before the top-up it lists the one type, %s"), *Joined(Item.DamageTypes)),
			 UCataclysmItemTooltip::WeaponLines(Item, Bases).Contains(Joined(Item.DamageTypes)));

	FRandomStream Stream(733);
	FDrop::TopUpDamageTypes(Tables.WeaponSkills, *Sword, 4, Stream, Item.DamageTypes);
	if (!TestEqual(TEXT("set-up: the Sword holds four"), Item.DamageTypes.Num(), 4))
	{
		return false;
	}
	const TArray<FString> Lines = UCataclysmItemTooltip::WeaponLines(Item, Bases);
	TestTrue(*FString::Printf(TEXT("after it the tool tip lists all four, %s: %s"), *Joined(Item.DamageTypes),
							  *FString::Join(Lines, TEXT(" | "))),
			 Lines.Contains(Joined(Item.DamageTypes)));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
