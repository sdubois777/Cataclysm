// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Interface/CataclysmItemTooltip.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"

/**
 * Tests for what a carried item says about itself. Issue #733.
 *
 * WHY THE WORDING IS WORTH TESTING AT ALL, which is not obvious for text on a
 * screen. Two of these rules are the kind that go wrong silently:
 *
 *   **A stat name reaching the player unconverted.** `crit_chance` is a column
 *   in a spreadsheet, not a phrase. Nothing would crash if it appeared, and
 *   nobody would see it unless they hovered over the one item that had it.
 *
 *   **An affix shape nobody thought of producing nothing.** `game/Data/Affixes.csv`
 *   holds 85 rows in four kinds and only 58 have the "+N to X" shape. An affix
 *   whose kind the wording does not handle would show a blank line, which reads
 *   as an item with fewer affixes than it has.
 *
 * Both are checked against the tables themselves rather than against examples,
 * so an affix or an implicit added later is covered without anybody adding a
 * test.
 *
 * NOTHING HERE NEEDS A RENDERER. The wording is a static function over plain
 * values, which is the whole reason it lives outside the widget -- the
 * automation test command in `tools/unreal_build.py` passes `-nullrhi`, so
 * anything decided inside a widget cannot be watched.
 */
namespace CataclysmTooltipTest
{
	/** Real rows, so a rename in the data fails a test rather than the screen. */
	const TCHAR* FlatHealthAffix = TEXT("Stat_Flat_maximum_health");
	const TCHAR* IncreasedHealthAffix = TEXT("Stat_Increased_maximum_health");
	const TCHAR* SingleResistanceAffix = TEXT("Resistance_Single_resistance");
	const TCHAR* AllResistancesAffix = TEXT("Resistance_All_resistances");
	const TCHAR* BleedAffix = TEXT("Ailment_Chance_to_bleed");
	const TCHAR* HelmBase = TEXT("Head_Helm");
	const TCHAR* Material = TEXT("Material_Aetherial_Shard");

	const UDataTable* Bases() { return UCataclysmItemModifiers::LoadBaseTable(); }
	const UDataTable* Affixes() { return UCataclysmDropRoll::LoadAffixTable(); }
	const UDataTable* Materials()
	{
		return UCataclysmDropRoll::LoadCraftingMaterialTable();
	}

	/** A carried slot holding one item. */
	FCataclysmCarriedSlot Carrying(const FCataclysmItem& Item)
	{
		FCataclysmCarriedSlot Slot;
		Slot.Item = Item;
		return Slot;
	}

	/** A carried slot holding a stack of a material. */
	FCataclysmCarriedSlot CarryingMaterial(const TCHAR* Name, int32 Quantity)
	{
		FCataclysmCarriedSlot Slot;
		Slot.Material = FName(Name);
		Slot.Quantity = Quantity;
		return Slot;
	}

	/** A helm carrying one affix at the top tier and a perfect roll. */
	FCataclysmItem HelmWith(const TCHAR* Affix,
							const TArray<FName>& DamageTypes = {})
	{
		FCataclysmItem Item;
		Item.Base = FName(HelmBase);

		FCataclysmRolledAffix Rolled;
		Rolled.Affix = FName(Affix);
		Rolled.Tier = UCataclysmItemValues::MaxAffixTier;
		Rolled.Roll = 1.0f;
		Rolled.DamageTypes = DamageTypes;
		Item.Affixes.Add(Rolled);

		return Item;
	}

	/**
	 * Whether one phrase of a tool tip line states a value of zero.
	 *
	 * THE NUMBER IS READ RATHER THAN SEARCHED FOR. An earlier version of
	 * this looked for the substring "0% increased", which is inside
	 * "400% increased" as well, so the eight affixes for damage against
	 * one Cataclysm failed the moment issue #1223 gave them the increased
	 * sentence. Every phrase states its number first, after a leading "+"
	 * or after the colon an ailment writes its own name before.
	 *
	 * ONE PHRASE AND NOT A WHOLE LINE, because a hybrid puts two on one
	 * line joined by ", " and either half could be the zero.
	 */
	bool PhraseStatesZero(const FString& Phrase)
	{
		FString Rest = Phrase.TrimStartAndEnd();

		int32 Colon = INDEX_NONE;
		if (Rest.FindChar(TEXT(':'), Colon))
		{
			Rest = Rest.RightChop(Colon + 1).TrimStart();
		}
		Rest.RemoveFromStart(TEXT("+"));

		int32 End = 0;
		while (End < Rest.Len()
			   && (FChar::IsDigit(Rest[End]) || Rest[End] == TEXT('.')))
		{
			++End;
		}
		if (End == 0)
		{
			// A phrase beginning with a word states no number of its own.
			return false;
		}
		return FCString::Atof(*Rest.Left(End)) == 0.0f;
	}

	/**
	 * Whether a line for this affix has to carry a percent sign.
	 *
	 * ASKED OF THE ROW RATHER THAN LISTED HERE, so an affix added to the
	 * data later is covered without anybody editing this file. An
	 * increase is always a percentage of something; a flat addition is
	 * one only when the stat it adds to is measured in percentage points,
	 * which is what the Percent column says. A hybrid grants two stats and
	 * needs a sign if either half does.
	 */
	bool NeedsAPercentSign(const FCataclysmAffixRow& Row,
						   const UDataTable* AffixTable)
	{
		if (Row.AffixKind.Equals(TEXT("Hybrid"), ESearchCase::IgnoreCase))
		{
			for (const FString& Part : { Row.HybridPart1, Row.HybridPart2 })
			{
				const FCataclysmAffixRow* Half =
					UCataclysmItemModifiers::AffixNamed(AffixTable, Part);
				if (Half && NeedsAPercentSign(*Half, AffixTable))
				{
					return true;
				}
			}
			return false;
		}
		return Row.Percent
			|| Row.ValueKind.Equals(TEXT("increased"), ESearchCase::IgnoreCase);
	}

	/** Whether a line for this affix must carry NO percent sign.
	 *
	 * NOT THE OPPOSITE OF THE ABOVE FOR A HYBRID, whose two halves can
	 * disagree: `Magic find and loot quantity` grants one percentage and
	 * one increase, so its line must have a sign and may not be forbidden
	 * one. Only a hybrid whose halves are both plain is checked.
	 */
	bool MustCarryNoPercentSign(const FCataclysmAffixRow& Row,
								const UDataTable* AffixTable)
	{
		if (Row.AffixKind.Equals(TEXT("Hybrid"), ESearchCase::IgnoreCase))
		{
			for (const FString& Part : { Row.HybridPart1, Row.HybridPart2 })
			{
				const FCataclysmAffixRow* Half =
					UCataclysmItemModifiers::AffixNamed(AffixTable, Part);
				if (!Half || NeedsAPercentSign(*Half, AffixTable))
				{
					return false;
				}
			}
			return true;
		}
		return !NeedsAPercentSign(Row, AffixTable);
	}

	/** The one affix line a helm carrying one affix produces. */
	FString OneAffixLine(const FCataclysmItem& Item)
	{
		return Item.Affixes.Num() == 1
			? UCataclysmItemTooltip::AffixLine(Item.Affixes[0], Item,
											   Bases(), Affixes())
			: FString();
	}

	// Three weapons chosen for what they differ in. An Axe is an ordinary
	// one-hander. A Shield is the one weapon supplying no attack damage. A
	// Greataxe is two-handed and its base states more damage types than are
	// designed for it.
	const TCHAR* AxeBase = TEXT("Weapon_Axe");
	const TCHAR* ShieldBase = TEXT("Weapon_Shield");
	const TCHAR* GreataxeBase = TEXT("Weapon_Greataxe");

	/** A weapon of one base, carrying the damage types given. */
	FCataclysmItem WeaponOf(const TCHAR* Base,
							const TArray<FName>& DamageTypes = {})
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.DamageTypes = DamageTypes;
		return Item;
	}

	/** The base row behind one of the names above. */
	const FCataclysmItemBaseRow* RowFor(const TCHAR* Base)
	{
		const UDataTable* Table = Bases();
		return Table ? Table->FindRow<FCataclysmItemBaseRow>(
						   FName(Base), TEXT("CataclysmTooltipTest"),
						   /*bWarnIfMissing=*/false)
					 : nullptr;
	}

	/** Whether any line is exactly this text. */
	bool Says(const TArray<FString>& Lines, const FString& Text)
	{
		return Lines.Contains(Text);
	}

	/** Whether any line contains this text. */
	bool Mentions(const TArray<FString>& Lines, const TCHAR* Text)
	{
		return Lines.ContainsByPredicate([Text](const FString& Line)
			{ return Line.Contains(Text); });
	}
}

// ---------------------------------------------------------------------------
// Words and numbers
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipStatsReadAsWords,
	"Cataclysm.Tooltip.EveryStatInTheDataReadsAsWords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipStatsReadAsWords::RunTest(const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	// EVERY STAT THAT ACTUALLY APPEARS, rather than a handful of examples. The
	// transformation is underscores to spaces plus four word expansions, and the
	// thing that would go wrong is a stat added later that it reads badly --
	// `crit_multiplier` arriving before `crit` was expanded, say. Reading the
	// tables means that is a test failure rather than a phrase nobody sees until
	// they hover over the one item carrying it.
	TSet<FString> Stats;

	if (const UDataTable* BaseTable = Bases())
	{
		for (const TPair<FName, uint8*>& Row : BaseTable->GetRowMap())
		{
			const FCataclysmItemBaseRow* Base =
				reinterpret_cast<const FCataclysmItemBaseRow*>(Row.Value);
			if (!Base)
			{
				continue;
			}
			if (!Base->Implicit1Stat.IsEmpty()) { Stats.Add(Base->Implicit1Stat); }
			if (!Base->Implicit2Stat.IsEmpty()) { Stats.Add(Base->Implicit2Stat); }
		}
	}

	if (const UDataTable* AffixTable = Affixes())
	{
		for (const TPair<FName, uint8*>& Row : AffixTable->GetRowMap())
		{
			const FCataclysmAffixRow* Affix =
				reinterpret_cast<const FCataclysmAffixRow*>(Row.Value);
			if (Affix && !Affix->Stat.IsEmpty())
			{
				Stats.Add(Affix->Stat);
			}
		}
	}

	if (!TestTrue(TEXT("some stats were found in the tables"), Stats.Num() > 0))
	{
		return false;
	}

	for (const FString& Stat : Stats)
	{
		const FString Words = UCataclysmItemTooltip::StatInWords(Stat);

		if (Words.IsEmpty())
		{
			AddError(FString::Printf(TEXT("%s reads as nothing."), *Stat));
			continue;
		}

		if (Words.Contains(TEXT("_")))
		{
			AddError(FString::Printf(
				TEXT("%s reads as '%s', which still has an underscore in it."),
				*Stat, *Words));
		}

		// A SHORTENING LEFT WHOLE IS THE FAILURE THIS IS FOR. `crit chance`
		// would pass an underscore check and still be a spreadsheet column
		// rather than a phrase.
		TArray<FString> Parts;
		Words.ParseIntoArray(Parts, TEXT(" "), /*InCullEmpty=*/true);
		for (const TCHAR* Short : {TEXT("max"), TEXT("crit"), TEXT("regen"),
								   TEXT("dot")})
		{
			if (Parts.Contains(FString(Short)))
			{
				AddError(FString::Printf(
					TEXT("%s reads as '%s', which still contains the "
						 "shortening '%s'."), *Stat, *Words, Short));
			}
		}
	}

	AddInfo(FString::Printf(TEXT("%d stats checked."), Stats.Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipNumbers,
	"Cataclysm.Tooltip.AWholeNumberLosesItsDecimalPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipNumbers::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("a whole number has no decimal point"),
		UCataclysmItemTooltip::NumberInWords(120.0f), FString(TEXT("120")));
	TestEqual(TEXT("and neither does one that rounds to whole"),
		UCataclysmItemTooltip::NumberInWords(120.001f), FString(TEXT("120")));
	TestEqual(TEXT("a fraction keeps one place"),
		UCataclysmItemTooltip::NumberInWords(2.5f), FString(TEXT("2.5")));
	TestEqual(TEXT("and zero reads as zero"),
		UCataclysmItemTooltip::NumberInWords(0.0f), FString(TEXT("0")));

	return true;
}

// ---------------------------------------------------------------------------
// One affix at a time
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipEveryAffixReads,
	"Cataclysm.Tooltip.EveryAffixInTheDataReads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipEveryAffixReads::RunTest(const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	// ALL 85 ROWS, IN FOUR KINDS. Only 58 have the "+N to X" shape; an Ailment
	// carries no value kind and a Hybrid grants two stats named in other
	// columns. An affix whose shape the wording does not handle would produce a
	// blank line, and a blank line reads as an item with fewer affixes than it
	// has rather than as a fault.
	const UDataTable* AffixTable = Affixes();
	if (!AffixTable)
	{
		AddError(TEXT("The affix table could not be loaded."));
		return false;
	}

	int32 Checked = 0;
	for (const TPair<FName, uint8*>& Row : AffixTable->GetRowMap())
	{
		const FCataclysmAffixRow* Affix =
			reinterpret_cast<const FCataclysmAffixRow*>(Row.Value);
		if (!Affix)
		{
			continue;
		}

		// A resistance family says how many damage types it covers and the item
		// says which, so one is made up here to match its breadth.
		TArray<FName> Types;
		for (int32 Index = 0; Index < Affix->Breadth
			 && Index < UCataclysmItemModifiers::DamageTypeNames().Num(); ++Index)
		{
			Types.Add(UCataclysmItemModifiers::DamageTypeNames()[Index]);
		}
		if (Affix->Breadth == UCataclysmItemModifiers::DamageTypeNames().Num())
		{
			// The all-eight family leaves the list empty, because there was no
			// choice to make when it rolled.
			Types.Empty();
		}

		const FCataclysmItem Item = HelmWith(*Row.Key.ToString(), Types);
		const FString Line = OneAffixLine(Item);

		if (Line.IsEmpty())
		{
			AddError(FString::Printf(
				TEXT("%s (kind %s, value kind '%s') reads as nothing."),
				*Row.Key.ToString(), *Affix->AffixKind, *Affix->ValueKind));
		}

		// UNTIL ISSUE #1223 THIS CHECKED ONLY THAT THE LINE WAS NOT EMPTY,
		// and nine rows read "+160 to Increased damage against War enemies"
		// unnoticed. A line that still carries the affix name's own leading
		// word has fallen through to the wrong sentence.
		if (Line.Contains(TEXT(" to Increased"))
			|| Line.Contains(TEXT(" to Flat")))
		{
			AddError(FString::Printf(
				TEXT("%s reads '%s', which keeps a word the sentence already "
					 "supplies."), *Row.Key.ToString(), *Line));
		}

		// AND UNTIL ISSUE #1224 ELEVEN LINES NAMED A PERCENTAGE STAT AND
		// PRINTED NO SIGN, so "+0.2 to life leech" did not say whether that
		// was 0.2 per cent or 0.2 health.
		if (NeedsAPercentSign(*Affix, AffixTable) && !Line.Contains(TEXT("%")))
		{
			AddError(FString::Printf(
				TEXT("%s grants a percentage and reads '%s', with no percent "
					 "sign."), *Row.Key.ToString(), *Line));
		}
		if (MustCarryNoPercentSign(*Affix, AffixTable)
			&& Line.Contains(TEXT("%")))
		{
			AddError(FString::Printf(
				TEXT("%s grants no percentage and reads '%s', which claims "
					 "one."), *Row.Key.ToString(), *Line));
		}
		++Checked;
	}

	AddInfo(FString::Printf(TEXT("%d affixes checked."), Checked));
	TestTrue(TEXT("affixes were found to check"), Checked > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipAffixWording,
	"Cataclysm.Tooltip.AFlatAffixAddsAndAnIncreasedOneIsAPercentage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipAffixWording::RunTest(const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	// THE TWO SHAPES PATH OF EXILE, LAST EPOCH AND DIABLO IV ALL USE. A player
	// coming from any of them reads "+120 to maximum health" and "12% increased
	// maximum health" without being taught, which is why the project does not
	// invent a third way of saying it.
	const FString Flat = OneAffixLine(HelmWith(FlatHealthAffix));
	TestTrue(FString::Printf(TEXT("a flat affix adds to a stat: '%s'"), *Flat),
		Flat.StartsWith(TEXT("+")) && Flat.Contains(TEXT("to maximum health")));
	TestFalse(TEXT("and does not call itself a percentage"),
		Flat.Contains(TEXT("%")));

	const FString Increased = OneAffixLine(HelmWith(IncreasedHealthAffix));
	TestTrue(FString::Printf(TEXT("an increased affix is a percentage: '%s'"),
							 *Increased),
		Increased.Contains(TEXT("% increased maximum health")));
	TestFalse(TEXT("and does not begin with a plus"),
		Increased.StartsWith(TEXT("+")));

	// THE SHEET'S OWN PHRASE LOSES ITS LEADING WORD, because "+120 to Flat
	// maximum health" is not a sentence.
	TestFalse(TEXT("the word 'Flat' does not reach the player"),
		Flat.Contains(TEXT("Flat")));

	// EVERY LINE STATES ITS TIER. Two items of one base differ mostly by tier,
	// and it is what a player compares.
	for (const FString& Line : {Flat, Increased})
	{
		TestTrue(FString::Printf(TEXT("'%s' states its tier"), *Line),
			Line.Contains(TEXT("(tier 7)")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipResistanceWording,
	"Cataclysm.Tooltip.AResistanceAffixNamesTheDamageTypesItCovers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipResistanceWording::RunTest(const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	// THE AFFIX SAYS HOW MANY AND THE ITEM SAYS WHICH, which is the split
	// UCataclysmItemModifiers::AccumulateInto already relies on. A tool tip that
	// read "single resistance" without naming it would be useless: which
	// resistance is the entire content of the affix.
	const TArray<FName>& Types = UCataclysmItemModifiers::DamageTypeNames();
	if (!TestTrue(TEXT("there are damage types to name"), Types.Num() > 0))
	{
		return false;
	}

	const FString One = OneAffixLine(HelmWith(SingleResistanceAffix, {Types[0]}));
	TestTrue(FString::Printf(TEXT("one resistance names it: '%s'"), *One),
		One.Contains(Types[0].ToString()) && One.Contains(TEXT("resistance")));

	// ALL EIGHT LEAVES THE LIST EMPTY AND MUST NOT READ AS " resistance" WITH
	// NOTHING IN FRONT OF IT.
	const FString All = OneAffixLine(HelmWith(AllResistancesAffix));
	TestTrue(FString::Printf(TEXT("all eight says so: '%s'"), *All),
		All.Contains(TEXT("all resistances")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipAilmentWording,
	"Cataclysm.Tooltip.AnAilmentUsesTheSheetsOwnPhrase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipAilmentWording::RunTest(const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	// NOT A PLACEHOLDER, AND THE TEST SAYS SO. An Ailment affix carries no value
	// kind at all, so there is no "+N to X" shape to put it in. The sheet names
	// it clearly -- "Chance to bleed" -- and that phrase with the number is the
	// honest line. 24 of the 85 rows take this path.
	const FString Line = OneAffixLine(HelmWith(BleedAffix));
	TestTrue(FString::Printf(TEXT("an ailment names itself: '%s'"), *Line),
		Line.Contains(TEXT("Chance to bleed")));
	TestTrue(TEXT("and states a number"), Line.Contains(TEXT(":")));

	return true;
}

// ---------------------------------------------------------------------------
// The whole panel
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipEmptySlot,
	"Cataclysm.Tooltip.AnEmptySlotSaysNothingAtAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipEmptySlot::RunTest(const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	// A TOOL TIP WITH NO TEXT DOES NOT APPEAR. Saying "empty" would put a box
	// over 30-odd cells of a fresh inventory every time the cursor crossed one.
	const FCataclysmCarriedSlot Nothing;
	TestEqual(TEXT("an empty slot produces no lines"),
		UCataclysmItemTooltip::LinesFor(Nothing, Bases(), Affixes(),
										Materials()).Num(), 0);
	TestTrue(TEXT("and no text"),
		UCataclysmItemTooltip::TextFor(Nothing, Bases(), Affixes(),
									   Materials()).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipMaterial,
	"Cataclysm.Tooltip.AMaterialSaysWhatItIsForAndHowManyAreCarried",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipMaterial::RunTest(const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	const TArray<FString> Many = UCataclysmItemTooltip::LinesFor(
		CarryingMaterial(Material, 7), Bases(), Affixes(), Materials());

	if (!TestTrue(TEXT("a material produces lines"), Many.Num() >= 2))
	{
		return false;
	}
	TestTrue(TEXT("it names itself"), Many[0].Contains(TEXT("Aetherial Shard")));
	TestTrue(TEXT("and says how many are carried"),
		Many.ContainsByPredicate([](const FString& Line)
			{ return Line.Contains(TEXT("7 carried")); }));

	// ONE OF SOMETHING DOES NOT SAY "1 carried", because a count of one is what
	// a slot holding a single thing already looks like.
	const TArray<FString> Single = UCataclysmItemTooltip::LinesFor(
		CarryingMaterial(Material, 1), Bases(), Affixes(), Materials());
	TestFalse(TEXT("a single one does not state a count"),
		Single.ContainsByPredicate([](const FString& Line)
			{ return Line.Contains(TEXT("carried")); }));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipWholeItem,
	"Cataclysm.Tooltip.AnItemNamesItselfAndStatesEverythingOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipWholeItem::RunTest(const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	FCataclysmItem Item = HelmWith(FlatHealthAffix);
	Item.GearLevel = 5;
	Item.Sockets = 2;
	Item.Residue = 3.0f;

	const TArray<FString> Lines = UCataclysmItemTooltip::LinesFor(
		Carrying(Item), Bases(), Affixes(), Materials());

	const FString All = FString::Join(Lines, TEXT(" | "));

	// THE WHOLE NAME, NOT THE BASE NAME. The grid cell already shows `Helm`;
	// what the player cannot see anywhere else is that this one is a Quality
	// Helm rather than a Cataclysmic one.
	TestTrue(FString::Printf(TEXT("it names itself: %s"), *All),
		Lines.Num() > 0 && Lines[0].Contains(TEXT("Helm")));

	TestTrue(TEXT("it states its upgrade level"),
		Lines.ContainsByPredicate([](const FString& Line)
			{ return Line == TEXT("+5"); }));

	// THE HELM'S IMPLICIT, which is armour and is on every Head base.
	TestTrue(TEXT("it states the implicit its base grants"),
		Lines.ContainsByPredicate([](const FString& Line)
			{ return Line.Contains(TEXT("armor")); }));

	TestTrue(TEXT("it states its affix"),
		Lines.ContainsByPredicate([](const FString& Line)
			{ return Line.Contains(TEXT("maximum health")); }));

	TestTrue(TEXT("it states its sockets, and says 'sockets' for two"),
		Lines.ContainsByPredicate([](const FString& Line)
			{ return Line == TEXT("2 sockets"); }));

	TestTrue(TEXT("it states the residue it carries"),
		Lines.ContainsByPredicate([](const FString& Line)
			{ return Line.Contains(TEXT("Cataclysmic Residue")); }));

	// A PIECE AT +0 DOES NOT STATE AN UPGRADE LEVEL, because every piece that
	// has never been upgraded is +0 and a line saying so on all of them is noise.
	FCataclysmItem Fresh = HelmWith(FlatHealthAffix);
	const TArray<FString> FreshLines = UCataclysmItemTooltip::LinesFor(
		Carrying(Fresh), Bases(), Affixes(), Materials());
	TestFalse(TEXT("a piece at +0 states no upgrade level"),
		FreshLines.ContainsByPredicate([](const FString& Line)
			{ return Line == TEXT("+0"); }));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipImplicitFollowsUpgrade,
	"Cataclysm.Tooltip.AnUpgradedPieceStatesTheBiggerImplicit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipImplicitFollowsUpgrade::RunTest(const FString& Parameters)
{
	// THE NUMBER THIS PIECE HAS, NOT THE NUMBER IN THE TABLE. An implicit does
	// not roll, so it is tempting to print the base's stated value; it does
	// scale with the upgrade level, so a +10 helm's armour is over three times a
	// +0 helm's. Printing the stated value would make every helm look identical,
	// which is exactly the problem this whole tool tip exists to solve.
	const FString AtZero =
		UCataclysmItemTooltip::ImplicitLine(TEXT("armor"), TEXT("flat"),
											200.0f, 0, /*bTwoHanded=*/false,
											CataclysmTooltipTest::Affixes());
	const FString AtTen =
		UCataclysmItemTooltip::ImplicitLine(TEXT("armor"), TEXT("flat"),
											200.0f, 10, /*bTwoHanded=*/false,
											CataclysmTooltipTest::Affixes());

	TestTrue(TEXT("a +0 piece states an implicit"), !AtZero.IsEmpty());
	TestTrue(FString::Printf(TEXT("and a +10 piece states a different one: "
								  "'%s' against '%s'"), *AtZero, *AtTen),
		AtZero != AtTen);

	// AN EMPTY STAT IS HOW A BASE SAYS IT HAS ONLY ONE IMPLICIT.
	TestTrue(TEXT("a base with no second implicit produces no second line"),
		UCataclysmItemTooltip::ImplicitLine(FString(), TEXT("flat"), 5.0f, 0,
											false,
											CataclysmTooltipTest::Affixes()).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipImplicitStatesItsUnit,
	"Cataclysm.Tooltip.AnImplicitSaysWhetherItIsAPercentage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipImplicitStatesItsUnit::RunTest(const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	// A HELM'S IMPLICIT EVASION AND AN EVASION AFFIX SIT TWO LINES APART.
	// Evasion is rolled against a number from 0 to 100, so it is a
	// percentage; armour is a rating fed to `100 x A / (A + K)`, so it is
	// not. Issue #1224. Real stats rather than invented ones, so a stat
	// leaving the data fails here.
	const FString Evasion = UCataclysmItemTooltip::ImplicitLine(
		TEXT("evasion"), TEXT("flat"), 4.0f, 0, /*bTwoHanded=*/false,
		Affixes());
	const FString Armour = UCataclysmItemTooltip::ImplicitLine(
		TEXT("armor"), TEXT("flat"), 200.0f, 0, /*bTwoHanded=*/false,
		Affixes());

	TestTrue(FString::Printf(TEXT("implicit evasion says it is a "
								  "percentage: '%s'"), *Evasion),
		Evasion.Contains(TEXT("%")));
	TestFalse(FString::Printf(TEXT("implicit armour does not: '%s'"),
							  *Armour),
		Armour.Contains(TEXT("%")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipDamageAgainstReadsAsIncrease,
	"Cataclysm.Tooltip.DamageAgainstACataclysmReadsAsAnIncrease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipDamageAgainstReadsAsIncrease::RunTest(
	const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	// THE ROW IS FLAT AND THE SENTENCE IS AN INCREASE, which is the case
	// issue #1223 reports. The affix adds a flat amount to a stat that is
	// itself the increases bucket for damage against one Cataclysm, so
	// "+160 to Increased damage against War enemies" was a third sentence
	// shape the project does not use.
	const FCataclysmItem Item =
		HelmWith(TEXT("Stat_Increased_damage_against_War_enemies"));
	const FString Line = OneAffixLine(Item);

	TestTrue(FString::Printf(TEXT("it reads as an increase: '%s'"), *Line),
		Line.Contains(TEXT("% increased damage against War enemies")));
	TestFalse(FString::Printf(TEXT("and keeps no leading word: '%s'"), *Line),
		Line.Contains(TEXT("to Increased")));

	// AND THE ONE ROW RENAMED WITH IT. `Increased cooldown reduction` was
	// the ninth line in that shape and is now `Cooldown reduction`, the
	// project owner's decision of 2026-09-04, because the stat divides
	// rather than subtracting and "increased" said the wrong thing about
	// it either way.
	const FString Cooldown =
		OneAffixLine(HelmWith(TEXT("Stat_Flat_cooldown_reduction")));
	TestTrue(FString::Printf(TEXT("cooldown reduction reads as a "
								  "percentage: '%s'"), *Cooldown),
		Cooldown.Contains(TEXT("% to cooldown reduction")));

	return true;
}


// ---------------------------------------------------------------------------
// What a weapon says it is. Issue #856.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipWeaponSaysWhatItIs,
	"Cataclysm.Tooltip.AWeaponStatesItsTypeSubTypeAndSwingRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipWeaponSaysWhatItIs::RunTest(const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	const FCataclysmItemBaseRow* Axe = RowFor(AxeBase);
	if (!TestNotNull(TEXT("the Axe base is in the table"), Axe))
	{
		return false;
	}

	const TArray<FString> Lines =
		UCataclysmItemTooltip::WeaponLines(WeaponOf(AxeBase), Bases());

	// HOW MANY HANDS, WHICH WEAPON, AND ITS SUB TYPE, on one line. The sub type
	// is not trivia: a hit's sub type is the one every weapon swung agrees on and
	// a mixed pair carries none.
	TestTrue(FString::Printf(TEXT("it says what it is: %s"),
			*FString::Join(Lines, TEXT(" | "))),
		Says(Lines, FString::Printf(TEXT("One-handed %s, %s"),
			*Axe->WeaponType, *Axe->SubType)));

	// HOW FAST IT SWINGS, to two decimals. The designed rates are 1.20, 1.25 and
	// 1.28, so one decimal place would make three different weapons read alike.
	TestTrue(TEXT("it says how fast it swings"),
		Says(Lines, FString::Printf(TEXT("%.2f attacks per second"),
			Axe->AttackSpeed)));

	// A TWO-HANDER SAYS SO, because that is what it costs to carry one.
	const TArray<FString> Greataxe =
		UCataclysmItemTooltip::WeaponLines(WeaponOf(GreataxeBase), Bases());
	TestTrue(TEXT("a two-handed weapon says it is two-handed"),
		Mentions(Greataxe, TEXT("Two-handed Greataxe")));

	// AND NOTHING THAT IS NOT A WEAPON SAYS ANY OF IT.
	const TArray<FString> Helm =
		UCataclysmItemTooltip::WeaponLines(HelmWith(FlatHealthAffix), Bases());
	TestEqual(TEXT("a helm produces no weapon lines at all"), Helm.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipShieldStatesNoSwingRate,
	"Cataclysm.Tooltip.AShieldStatesNoSwingRateBecauseAShieldIsNotSwung",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipShieldStatesNoSwingRate::RunTest(const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	const FCataclysmItemBaseRow* Shield = RowFor(ShieldBase);
	if (!TestNotNull(TEXT("the Shield base is in the table"), Shield))
	{
		return false;
	}

	// THE TRAP THIS GUARDS. A Shield is a one-handed weapon and its base row
	// carries an AttackSpeed like any other, so the obvious implementation prints
	// it. But a Shield carries no attack damage implicit, so it contributes
	// neither damage nor swing rate to a hit, and a tool tip saying it swings at
	// 1.20 a second would be telling the player something untrue.
	TestTrue(TEXT("the Shield's base really does carry a swing rate to be tempted by"),
		Shield->AttackSpeed > 0.0f);
	TestEqual(TEXT("and it really does supply no attack damage"),
		UCataclysmItemModifiers::WeaponDamageForItem(WeaponOf(ShieldBase), Bases()),
		0.0f);

	const TArray<FString> Lines =
		UCataclysmItemTooltip::WeaponLines(WeaponOf(ShieldBase), Bases());

	TestFalse(FString::Printf(TEXT("a Shield states no swing rate: %s"),
			*FString::Join(Lines, TEXT(" | "))),
		Mentions(Lines, TEXT("attacks per second")));

	// IT STILL SAYS WHAT IT IS, because a Shield is a one-handed weapon and the
	// player still has to know it occupies a weapon slot.
	TestTrue(TEXT("a Shield still says what it is"),
		Mentions(Lines, TEXT("One-handed Shield")));

	// AND AN ORDINARY WEAPON STILL DOES STATE ONE, so the check above is not
	// passing because the line was dropped for every weapon.
	const TArray<FString> Axe =
		UCataclysmItemTooltip::WeaponLines(WeaponOf(AxeBase), Bases());
	TestTrue(TEXT("an Axe does state a swing rate"),
		Mentions(Axe, TEXT("attacks per second")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipWeaponDamageTypes,
	"Cataclysm.Tooltip.AWeaponStatesTheDamageTypesItCarriesAndHowManyItCouldHold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipWeaponDamageTypes::RunTest(const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	const FCataclysmItemBaseRow* Greataxe = RowFor(GreataxeBase);
	if (!TestNotNull(TEXT("the Greataxe base is in the table"), Greataxe))
	{
		return false;
	}

	// WHAT IT CARRIES, in the order it carries them. Both are designed for a
	// Greataxe.
	const TArray<FString> Carrying = UCataclysmItemTooltip::WeaponLines(
		WeaponOf(GreataxeBase, { FName(TEXT("War")), FName(TEXT("Death")) }),
		Bases());
	TestTrue(FString::Printf(TEXT("it lists the damage types it carries: %s"),
			*FString::Join(Carrying, TEXT(" | "))),
		Says(Carrying, TEXT("War, Death")));

	// A WEAPON CARRYING NONE LISTS NONE rather than an empty line. An item rolled
	// before issue #857 has an empty list and a blank line reads as a fault.
	const TArray<FString> Bare =
		UCataclysmItemTooltip::WeaponLines(WeaponOf(GreataxeBase), Bases());
	TestFalse(TEXT("no line is blank when it carries no damage types"),
		Bare.ContainsByPredicate([](const FString& Line) { return Line.IsEmpty(); }));

	// THE CEILING IS WHAT IT CAN REACH, NOT WHAT ITS BASE STATES. This is the
	// whole point of the line. Every two-handed base states 8 and not one has 8
	// damage types designed for it, so printing the base's own figure would tell
	// a player a Greataxe can hold twice what it can. Issue #875 is whether the
	// design intends that; either way the number shown has to be reachable.
	const int32 Designed = UCataclysmDropRoll::DamageTypesAvailableTo(
		UCataclysmWeaponSkills::LoadGeneratedTable(), Greataxe->WeaponType).Num();
	TestTrue(TEXT("a Greataxe really does have fewer designed than its base states"),
		Designed < Greataxe->MaxDamageTypes);

	TestTrue(FString::Printf(TEXT("it states the ceiling it can reach: %s"),
			*FString::Join(Bare, TEXT(" | "))),
		Says(Bare, FString::Printf(TEXT("Holds up to %d damage types"), Designed)));
	TestFalse(TEXT("and not the higher figure its base row states"),
		Says(Bare, FString::Printf(TEXT("Holds up to %d damage types"),
			Greataxe->MaxDamageTypes)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipWholeWeapon,
	"Cataclysm.Tooltip.AWholeWeaponsToolTipCarriesTheWeaponLines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipWholeWeapon::RunTest(const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	// THROUGH LinesFor, not WeaponLines, because a function nothing calls helps
	// nobody. This is what a player hovering over a carried Axe actually sees.
	FCataclysmItem Item = WeaponOf(AxeBase, { FName(TEXT("War")) });
	Item.GearLevel = 4;

	const TArray<FString> Lines = UCataclysmItemTooltip::LinesFor(
		Carrying(Item), Bases(), Affixes(), Materials());
	const FString All = FString::Join(Lines, TEXT(" | "));

	TestTrue(FString::Printf(TEXT("it says what it is: %s"), *All),
		Mentions(Lines, TEXT("One-handed Axe, Slashing")));
	TestTrue(TEXT("it says how fast it swings"),
		Mentions(Lines, TEXT("attacks per second")));
	TestTrue(TEXT("it says the damage type it carries"), Says(Lines, TEXT("War")));
	TestTrue(TEXT("it says how many it could hold"),
		Mentions(Lines, TEXT("Holds up to")));

	// AND THE ORDER READS AS A DESCRIPTION. The name, then the upgrade level,
	// then what the weapon is, and only then what carrying it grants. An implicit
	// appearing above the weapon's own description would read as a stat block
	// with the item's identity buried in it.
	const int32 Upgrade = Lines.IndexOfByKey(TEXT("+4"));
	const int32 What = Lines.IndexOfByPredicate([](const FString& Line)
		{ return Line.Contains(TEXT("One-handed Axe")); });
	const int32 Implicit = Lines.IndexOfByPredicate([](const FString& Line)
		{ return Line.Contains(TEXT("attack damage")); });

	TestTrue(FString::Printf(TEXT("the upgrade level comes before what it is: %s"), *All),
		Upgrade != INDEX_NONE && What != INDEX_NONE && Upgrade < What);
	TestTrue(FString::Printf(TEXT("what it is comes before what it grants: %s"), *All),
		Implicit != INDEX_NONE && What < Implicit);

	return true;
}

// ---------------------------------------------------------------------------
// A hybrid affix says what it grants rather than zero. Issue #1177
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipHybridReadsBothHalves,
	"Cataclysm.Tooltip.AHybridAffixReadsAsBothOfItsHalves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipHybridReadsBothHalves::RunTest(const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	/**
	 * REPORTED FROM PLAY on 2026-09-02: some affixes on gear show a value of 0.
	 *
	 * WHY. All thirteen hybrid rows carry no stat, no value kind and a top value
	 * of 0.0, because what they grant is named in HybridPart1 and HybridPart2
	 * instead. The line was built from the hybrid row's own top value, so it read
	 * "Health and armor: 0" on an item that was granting both halves correctly.
	 *
	 * ISSUE #847 FIXED WHAT A HYBRID IS WORTH AND LEFT WHAT IT SAYS. Its fourth
	 * listed piece of work was a test that no affix resolves to a zero, and that
	 * test is the one below this.
	 */
	const UDataTable* AffixTable = Affixes();
	if (!TestNotNull(TEXT("the affix table loaded"), AffixTable))
	{
		return false;
	}

	const FCataclysmItem Item = HelmWith(TEXT("Hybrid_Health_and_armor"));
	const FString Line = OneAffixLine(Item);

	// THE EXPECTED NUMBERS ARE WORKED OUT FROM THE DATA rather than written down,
	// so a change to either part's top value fails on the wording rather than on
	// a number somebody forgot to update here.
	const FCataclysmAffixRow* Health =
		UCataclysmItemModifiers::AffixNamed(AffixTable, TEXT("Flat maximum health"));
	const FCataclysmAffixRow* Armour =
		UCataclysmItemModifiers::AffixNamed(AffixTable, TEXT("Flat armor"));
	if (!TestNotNull(TEXT("the health half is in the table"), Health)
		|| !TestNotNull(TEXT("the armour half is in the table"), Armour))
	{
		return false;
	}

	auto HalfInWords = [&Item](const FCataclysmAffixRow& Part)
	{
		return UCataclysmItemTooltip::NumberInWords(
			UCataclysmItemValues::AffixValue(Part.TopValue, Part.Floor,
											 Item.Affixes[0].Tier,
											 Item.Affixes[0].Roll,
											 Item.GearLevel,
											 /*bTwoHanded=*/false)
			* UCataclysmItemValues::HybridFraction);
	};

	const FString HealthPhrase =
		FString::Printf(TEXT("+%s to maximum health"), *HalfInWords(*Health));
	const FString ArmourPhrase =
		FString::Printf(TEXT("+%s to armor"), *HalfInWords(*Armour));

	TestTrue(FString::Printf(
		TEXT("the health half is named with its own value: '%s' in '%s'"),
		*HealthPhrase, *Line), Line.Contains(HealthPhrase));
	TestTrue(FString::Printf(
		TEXT("the armour half is named with its own value: '%s' in '%s'"),
		*ArmourPhrase, *Line), Line.Contains(ArmourPhrase));

	// BOTH ON ONE LINE, because one affix fills one of the item's four slots and
	// two lines would make the item look richer than it is.
	TestTrue(FString::Printf(TEXT("both halves are on one line: '%s'"), *Line),
			 Line.Contains(TEXT(", ")));

	// AND THE TIER IS SAID ONCE, not once per half.
	int32 FirstTier = INDEX_NONE;
	int32 LastTier = INDEX_NONE;
	Line.FindChar(TEXT('('), FirstTier);
	Line.FindLastChar(TEXT('('), LastTier);
	TestEqual(TEXT("the tier is written once for the pair"), FirstTier, LastTier);

	// THE FAULT ITSELF, NAMED. This is the string a player saw.
	TestFalse(FString::Printf(TEXT("it no longer reads as a bare zero: '%s'"), *Line),
			  Line.Contains(TEXT("Health and armor: 0")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipNoAffixReadsZero,
	"Cataclysm.Tooltip.NoAffixInTheDataReadsAsZeroOnAFullyUpgradedItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipNoAffixReadsZero::RunTest(const FString& Parameters)
{
	using namespace CataclysmTooltipTest;

	/**
	 * THE GUARD ISSUE #847 ASKED FOR AND DID NOT GET. It walks all 85 rows and
	 * fails on any whose line states a value of zero, which is what a hybrid did
	 * for thirteen of them.
	 *
	 * AT THE TOP TIER ON A FULLY UPGRADED ITEM, WHICH IS NOT AN EVASION. Three
	 * affixes really are worth less than a twentieth of a point at tier 1 on an
	 * un-upgraded item and really do print 0.0 there -- flat life leech, flat
	 * mana leech and flat energy shield leech. That is issue #858, it is a
	 * separate fault with a separate answer, and a check written at tier 1 would
	 * fail for it and hide anything else. The figures in
	 * `game/Data/Affixes.csv` are stated for tier 7 on a +10 item, so that is
	 * the one place every affix must have something to say.
	 */
	const UDataTable* AffixTable = Affixes();
	if (!TestNotNull(TEXT("the affix table loaded"), AffixTable))
	{
		return false;
	}

	int32 Checked = 0;
	for (const TPair<FName, uint8*>& Row : AffixTable->GetRowMap())
	{
		const FCataclysmAffixRow* Affix =
			reinterpret_cast<const FCataclysmAffixRow*>(Row.Value);
		if (!Affix)
		{
			continue;
		}

		// A resistance family says how many damage types it covers and the item
		// says which, so one is made up here to match its breadth. The same
		// arrangement Cataclysm.Tooltip.EveryAffixInTheDataReads uses.
		TArray<FName> Types;
		for (int32 Index = 0; Index < Affix->Breadth
			 && Index < UCataclysmItemModifiers::DamageTypeNames().Num(); ++Index)
		{
			Types.Add(UCataclysmItemModifiers::DamageTypeNames()[Index]);
		}
		if (Affix->Breadth == UCataclysmItemModifiers::DamageTypeNames().Num())
		{
			Types.Empty();
		}

		FCataclysmItem Item = HelmWith(*Row.Key.ToString(), Types);
		Item.GearLevel = UCataclysmItemValues::MaxGearLevel;

		const FString Line = OneAffixLine(Item);

		// A HYBRID PUTS TWO PHRASES ON ONE LINE, and either can be the
		// zero, so each is read separately.
		TArray<FString> Phrases;
		Line.ParseIntoArray(Phrases, TEXT(", "), /*CullEmpty=*/true);
		for (const FString& Phrase : Phrases)
		{
			if (PhraseStatesZero(Phrase))
			{
				AddError(FString::Printf(
					TEXT("%s (kind %s) reads as zero at tier %d on a +%d item: '%s'"),
					*Row.Key.ToString(), *Affix->AffixKind,
					UCataclysmItemValues::MaxAffixTier,
					UCataclysmItemValues::MaxGearLevel, *Line));
				break;
			}
		}
		++Checked;
	}

	AddInfo(FString::Printf(TEXT("%d affixes checked."), Checked));
	TestTrue(TEXT("affixes were found to check"), Checked > 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
