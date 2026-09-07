// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Interface/CataclysmItemTooltip.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * Tests for which enchantments an item carries and what it says about them.
 *
 * WHAT AN ENCHANTMENT IS HERE. A positive and a negative together, filling one
 * of an item's four slots. The project owner ruled on 2026-09-07 that the two
 * halves come as a pair, which the design document's weight table already
 * described -- "Rare -- very powerful effect, severe consequence".
 *
 * AND THE PAIR IS STRUCK AT ONE WEIGHT. The same owner ruled later on
 * 2026-09-07 that the weight column exists "to determine what benefits go with
 * what negatives ... to ensure you can't get the most powerful benefits with
 * negatives that barely do anything". The two halves used to be drawn
 * independently and the design document used to say so; that sentence was
 * removed, because independent draws defeated the goal stated beside them.
 * APairIsBoughtAtItsOwnWeight below is what holds the rule now, and it is the
 * one test here that could not pass before the rule existed.
 *
 * WHAT IS NOT TESTED HERE BECAUSE IT DOES NOT EXIST. What an enchantment DOES.
 * Not one of the 574 changes a character's stats; the rest of issue #45.
 * Uniqueness across all WORN gear is also absent -- the rule enforced below is
 * the narrower one that neither half repeats on a single piece.
 *
 * EVERY DRAW IS SEEDED. An unseeded sampling test fails once a month and is then
 * ignored.
 */

namespace CataclysmEnchantmentTest
{
	using FDrop = UCataclysmDropRoll;

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

	UDataTable* Positives()
	{
		return LoadTable<FCataclysmEnchantmentRow>(
			TEXT("EnchantmentsPositive.csv"));
	}

	UDataTable* Negatives()
	{
		return LoadTable<FCataclysmEnchantmentRow>(
			TEXT("EnchantmentsNegative.csv"));
	}

	/** Every table a whole-item roll needs. */
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
			return Bases && Affixes && Rarities && Sockets && AffixTiers
				&& WeaponSkills && Positive && Negative;
		}
	};

	FTables LoadEverything()
	{
		FTables Out;
		Out.Bases = LoadTable<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
		Out.Affixes = LoadTable<FCataclysmAffixRow>(TEXT("Affixes.csv"));
		Out.Rarities = LoadTable<FCataclysmGearRarityRow>(TEXT("GearRarity.csv"));
		Out.Sockets = LoadTable<FCataclysmItemSocketRow>(TEXT("ItemSockets.csv"));
		Out.AffixTiers =
			LoadTable<FCataclysmAffixTierRow>(TEXT("AffixTiers.csv"));
		Out.WeaponSkills =
			LoadTable<FCataclysmWeaponSkillRow>(TEXT("WeaponSkills.csv"));
		Out.Positive = Positives();
		Out.Negative = Negatives();
		return Out;
	}

	bool Roll(const FTables& Tables, const FString& Slot, int32 Tier,
			  float MagicFind, FRandomStream& Stream, FCataclysmItem& OutItem)
	{
		return FDrop::RollItem(Tables.Bases, Tables.Affixes, Tables.Rarities,
							   Tables.Sockets, Tables.AffixTiers,
							   Tables.WeaponSkills, Tables.Positive,
							   Tables.Negative, Slot, Tier, MagicFind, Stream,
							   OutItem);
	}

	/** A carried slot holding one item, which is what the tool tip reads. */
	FCataclysmCarriedSlot Carrying(const FCataclysmItem& Item)
	{
		FCataclysmCarriedSlot Slot;
		Slot.Item = Item;
		return Slot;
	}
}

// ---------------------------------------------------------------------------
// The weight ladder
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentWeightLadder,
	"Cataclysm.Enchantments.WeightOneIsSixtyFourTimesRarerThanWeightFour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentWeightLadder::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	// THE OWNER'S RULING OF 2026-09-07, as four numbers rather than a shape.
	// Writing them out is the point: a step of 4 applied in the wrong direction
	// gives 64, 16, 4, 1, which is also "a ladder" and is exactly backwards.
	TestEqual(TEXT("weight 1 is the rarest"),
			  FDrop::EnchantmentDrawWeight(1.0f), 1.0f);
	TestEqual(TEXT("weight 2 is four times as common"),
			  FDrop::EnchantmentDrawWeight(2.0f), 4.0f);
	TestEqual(TEXT("weight 3 is sixteen times as common"),
			  FDrop::EnchantmentDrawWeight(3.0f), 16.0f);
	TestEqual(TEXT("weight 4 is sixty-four times as common"),
			  FDrop::EnchantmentDrawWeight(4.0f), 64.0f);

	// A ROW THIS CANNOT PRICE IS TAKEN OUT OF THE DRAW RATHER THAN GUESSED AT.
	// 5 and 18 are the range the 55 set rows carry a set identifier in, which is
	// issue #1443.
	TestEqual(TEXT("weight 0 cannot be drawn"),
			  FDrop::EnchantmentDrawWeight(0.0f), 0.0f);
	TestEqual(TEXT("a set identifier of 5 cannot be drawn"),
			  FDrop::EnchantmentDrawWeight(5.0f), 0.0f);
	TestEqual(TEXT("a set identifier of 18 cannot be drawn"),
			  FDrop::EnchantmentDrawWeight(18.0f), 0.0f);
	TestEqual(TEXT("a fractional weight cannot be drawn"),
			  FDrop::EnchantmentDrawWeight(2.5f), 0.0f);
	TestEqual(TEXT("a negative weight cannot be drawn"),
			  FDrop::EnchantmentDrawWeight(-1.0f), 0.0f);

	return true;
}

// ---------------------------------------------------------------------------
// What may be drawn
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentSetRowsAreNeverDrawn,
	"Cataclysm.Enchantments.ASetRowIsNeverDrawnFromTheOrdinaryPool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentSetRowsAreNeverDrawn::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	UDataTable* Positive = Positives();
	UDataTable* Negative = Negatives();
	if (!Positive || !Negative)
	{
		AddError(TEXT("Could not load the enchantment tables from game/Data/."));
		return false;
	}

	for (UDataTable* Table : { Positive, Negative })
	{
		// THE POSITIVE CONTROL FIRST. A search that finds nothing proves nothing
		// until the same search has found something: if the tables held no set
		// rows at all, "no set row is a candidate" would pass while testing
		// nothing.
		int32 SetRows = 0;
		Table->ForeachRow<FCataclysmEnchantmentRow>(TEXT("Test"),
			[&](const FName&, const FCataclysmEnchantmentRow& Row)
			{
				if (Row.EnchantmentType.Equals(TEXT("Set"),
											   ESearchCase::IgnoreCase))
				{
					++SetRows;
				}
			});
		if (!TestTrue(TEXT("the table really does hold set rows to exclude"),
					  SetRows > 0))
		{
			return false;
		}

		TArray<FName> Candidates;
		FDrop::EnchantmentCandidatesFor(Table, TEXT("Chest"), Candidates);
		TestTrue(TEXT("excluding them did not empty the pool"),
				 Candidates.Num() > 0);

		for (const FName& Candidate : Candidates)
		{
			const FCataclysmEnchantmentRow* Row =
				Table->FindRow<FCataclysmEnchantmentRow>(
					Candidate, TEXT("Test"), /*bWarnIfMissing=*/false);
			if (!Row)
			{
				continue;
			}
			if (Row->EnchantmentType.Equals(TEXT("Set"),
											ESearchCase::IgnoreCase))
			{
				AddError(FString::Printf(
					TEXT("'%s' is a set row and is in the ordinary draw. Set "
						 "positives and negatives are paired and guaranteed, so "
						 "a set is handed out whole rather than half-drawn."),
					*Candidate.ToString()));
				return false;
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentSlotTagBinds,
	"Cataclysm.Enchantments.AWeaponOnlyEnchantmentCannotRollOnABelt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentSlotTagBinds::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	UDataTable* Positive = Positives();
	if (!Positive)
	{
		AddError(TEXT("Could not load EnchantmentsPositive.csv."));
		return false;
	}

	// THE THREE ROWS THAT CARRY A SLOT TAG, found rather than named, so this
	// keeps working if a fourth is written. The project owner ruled on
	// 2026-09-07 that tags say what an enchantment AFFECTS and not where it may
	// sit; an `Item.Slot.` tag is the exception and there are three of them.
	// ONLY ROWS THAT COULD BE DRAWN AT ALL. A set row is excluded for its own
	// reason and would otherwise be counted here and missing from both pools,
	// which would break the arithmetic at the end of this test with a message
	// about slots that had nothing to do with slots. No set row carries a slot
	// tag today; this is so that one added later fails somewhere that explains
	// itself.
	TArray<FName> WeaponOnly;
	Positive->ForeachRow<FCataclysmEnchantmentRow>(TEXT("Test"),
		[&](const FName& Key, const FCataclysmEnchantmentRow& Row)
		{
			const bool bDrawable =
				!Row.EnchantmentType.Equals(TEXT("Set"),
											ESearchCase::IgnoreCase)
				&& FDrop::EnchantmentDrawWeight(Row.Weight) > 0.0f;
			if (bDrawable
				&& Row.Tags.Contains(TEXT("Item.Slot.Weapon"),
									 ESearchCase::IgnoreCase))
			{
				WeaponOnly.Add(Key);
			}
		});

	// THE POSITIVE CONTROL. Without it this passes on an empty list.
	if (!TestTrue(TEXT("the data really does carry weapon-only enchantments"),
				  WeaponOnly.Num() > 0))
	{
		return false;
	}

	TArray<FName> OnAWeapon;
	TArray<FName> OnABelt;
	FDrop::EnchantmentCandidatesFor(Positive, TEXT("Weapon"), OnAWeapon);
	FDrop::EnchantmentCandidatesFor(Positive, TEXT("Belt"), OnABelt);

	for (const FName& Row : WeaponOnly)
	{
		TestTrue(FString::Printf(TEXT("'%s' can roll on a weapon"),
								 *Row.ToString()),
				 OnAWeapon.Contains(Row));
		TestFalse(FString::Printf(TEXT("'%s' cannot roll on a belt"),
								  *Row.ToString()),
				  OnABelt.Contains(Row));
	}

	// AND THE REST OF THE POOL IS NOT RESTRICTED BY THIS. 571 of 574 rows carry
	// no slot tag, so a belt has nearly as much to draw from as a weapon; if
	// this read every tag as a slot restriction the belt's pool would collapse.
	TestTrue(TEXT("a belt still draws from nearly the whole pool"),
			 OnABelt.Num() == OnAWeapon.Num() - WeaponOnly.Num());

	return true;
}

// ---------------------------------------------------------------------------
// What a drop carries
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentCountMatchesRarity,
	"Cataclysm.Enchantments.ADroppedItemCarriesAsManyEnchantmentsAsItsRaritySays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentCountMatchesRarity::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	const FTables Tables = LoadEverything();
	if (!Tables.AllPresent())
	{
		AddError(TEXT("Could not load every table a drop needs."));
		return false;
	}

	// HIGH MAGIC FIND AND THE DEEPEST TIER, so the enchanted rarities actually
	// appear. Measured at zero magic find on tier 1 this test would roll almost
	// nothing but Everyday items and prove nothing about enchantments at all.
	FRandomStream Stream(20260907);
	int32 WithEnchantments = 0;

	for (int32 Try = 0; Try < 400; ++Try)
	{
		FCataclysmItem Item;
		if (!Roll(Tables, TEXT("Chest"), 8, 500.0f, Stream, Item))
		{
			AddError(FString::Printf(TEXT("Roll %d failed outright."), Try));
			return false;
		}

		if (Item.Enchantments.Num() != Item.EnchantmentCount)
		{
			AddError(FString::Printf(
				TEXT("An item says it carries %d enchantments and names %d. "
					 "The count is what rarity is read from, so the two cannot "
					 "disagree on a dropped item."),
				Item.EnchantmentCount, Item.Enchantments.Num()));
			return false;
		}

		if (Item.EnchantmentCount > 0)
		{
			++WithEnchantments;
		}

		for (const FCataclysmRolledEnchantment& Rolled : Item.Enchantments)
		{
			TestFalse(TEXT("the positive half is named"),
					  Rolled.Positive.IsNone());
			TestFalse(TEXT("the negative half is named"),
					  Rolled.Negative.IsNone());

			if (!Tables.Positive->FindRow<FCataclysmEnchantmentRow>(
					Rolled.Positive, TEXT("Test"), /*bWarnIfMissing=*/false))
			{
				AddError(FString::Printf(
					TEXT("'%s' is not a row of EnchantmentsPositive.csv."),
					*Rolled.Positive.ToString()));
				return false;
			}
			if (!Tables.Negative->FindRow<FCataclysmEnchantmentRow>(
					Rolled.Negative, TEXT("Test"), /*bWarnIfMissing=*/false))
			{
				AddError(FString::Printf(
					TEXT("'%s' is not a row of EnchantmentsNegative.csv."),
					*Rolled.Negative.ToString()));
				return false;
			}
		}
	}

	// THE CONTROL: without this the whole loop passes on 400 Everyday items,
	// every one of which carries no enchantment and agrees with itself.
	TestTrue(TEXT("at least some of the 400 drops carried an enchantment"),
			 WithEnchantments > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentNoHalfRepeats,
	"Cataclysm.Enchantments.NeitherHalfRepeatsOnOnePiece",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentNoHalfRepeats::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	UDataTable* Positive = Positives();
	UDataTable* Negative = Negatives();
	if (!Positive || !Negative)
	{
		AddError(TEXT("Could not load the enchantment tables."));
		return false;
	}

	// FOUR PAIRS IS WHAT A CATACLYSMIC PIECE HOLDS, which is the most any item
	// can, so it is the case where a repeat is likeliest.
	FRandomStream Stream(4041);
	for (int32 Try = 0; Try < 500; ++Try)
	{
		TArray<FCataclysmRolledEnchantment> Rolled;
		if (!FDrop::RollEnchantments(Positive, Negative, TEXT("Chest"), 4,
									 Stream, Rolled))
		{
			AddError(FString::Printf(
				TEXT("Roll %d could not fill four enchantments."), Try));
			return false;
		}

		if (Rolled.Num() != 4)
		{
			AddError(FString::Printf(TEXT("Asked for four and got %d."),
									 Rolled.Num()));
			return false;
		}

		TSet<FName> SeenPositives;
		TSet<FName> SeenNegatives;
		for (const FCataclysmRolledEnchantment& One : Rolled)
		{
			bool bAlready = false;
			SeenPositives.Add(One.Positive, &bAlready);
			if (bAlready)
			{
				AddError(FString::Printf(
					TEXT("'%s' appears twice on one piece. A player reading the "
						 "same line twice would think the item was broken."),
					*One.Positive.ToString()));
				return false;
			}
			SeenNegatives.Add(One.Negative, &bAlready);
			if (bAlready)
			{
				AddError(FString::Printf(
					TEXT("The drawback '%s' appears twice on one piece."),
					*One.Negative.ToString()));
				return false;
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentWeightIsApplied,
	"Cataclysm.Enchantments.ACommonEnchantmentIsDrawnFarMoreOftenThanARareOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentWeightIsApplied::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	UDataTable* Positive = Positives();
	UDataTable* Negative = Negatives();
	if (!Positive || !Negative)
	{
		AddError(TEXT("Could not load the enchantment tables."));
		return false;
	}

	// WHAT THIS CATCHES THAT THE LADDER TEST DOES NOT. EnchantmentDrawWeight can
	// be perfectly right while RollEnchantments ignores it and picks uniformly.
	// Only a sample can tell those apart.
	FRandomStream Stream(90712);
	TMap<int32, int32> DrawsByWeight;
	constexpr int32 Draws = 4000;

	for (int32 Try = 0; Try < Draws; ++Try)
	{
		TArray<FCataclysmRolledEnchantment> Rolled;
		if (!FDrop::RollEnchantments(Positive, Negative, TEXT("Chest"), 1,
									 Stream, Rolled))
		{
			AddError(TEXT("A single enchantment could not be drawn."));
			return false;
		}

		const FCataclysmEnchantmentRow* Row =
			Positive->FindRow<FCataclysmEnchantmentRow>(
				Rolled[0].Positive, TEXT("Test"), /*bWarnIfMissing=*/false);
		if (!Row)
		{
			AddError(TEXT("A drawn row is not in the table."));
			return false;
		}
		++DrawsByWeight.FindOrAdd(FMath::RoundToInt(Row->Weight));
	}

	const int32 Rarest = DrawsByWeight.FindRef(1);
	const int32 Commonest = DrawsByWeight.FindRef(4);

	// THE BAND IS PRICED, NOT THE ROW, so the expected ratio IS 64 and no longer
	// depends on how many rows the sheet carries at each weight. Ten is a floor
	// well clear of sampling noise at 4,000 draws and well clear of the 0.7 a
	// uniform draw over the four bands would give.
	TestTrue(FString::Printf(
				 TEXT("weight 4 was drawn far more than weight 1: %d against %d"),
				 Commonest, Rarest),
			 Commonest > Rarest * 10);

	// AND THE RARE ONE IS STILL REACHABLE. A ladder steep enough to make weight
	// 1 unreachable would pass the line above and be wrong.
	TestTrue(TEXT("weight 1 is rare and not impossible"), Rarest > 0);

	// EVERY RUNG STEPS, WHICH THE ROW COUNTS USED TO CANCEL. This is the half of
	// the ruling the shipped code did not deliver: pricing each ROW at 1/4/16/64
	// meant 113 weight 3 rows against 28 weight 4 rows came out at 42.5% and
	// 42.1% of draws, so a "Moderate" enchantment and a "Common" one were met
	// equally often. Pricing the BAND makes weight 4 four times weight 3
	// whatever the sheet does. Two is a floor with room for sampling noise
	// under the expected four.
	const int32 Moderate = DrawsByWeight.FindRef(3);
	const int32 Uncommon = DrawsByWeight.FindRef(2);
	TestTrue(FString::Printf(
				 TEXT("weight 4 outnumbers weight 3: %d against %d"),
				 Commonest, Moderate),
			 Commonest > Moderate * 2);
	TestTrue(FString::Printf(
				 TEXT("weight 3 outnumbers weight 2: %d against %d"),
				 Moderate, Uncommon),
			 Moderate > Uncommon * 2);

	// AND THE FOUR SHARES ARE THE DESIGNED ONES, which is what lets
	// docs/DECISIONS.md and the design document state 1.2%, 4.7%, 18.8% and
	// 75.3% as facts about the game rather than as arithmetic nobody ran. The
	// expected count is Draws * step^(weight-1) / (1+4+16+64), and every band
	// is always available -- every weight holds at least 22 rows on each side
	// for every gear slot -- so the denominator is the whole ladder.
	//
	// THE TOLERANCES ARE WIDE ENOUGH FOR SAMPLING NOISE AND NARROW ENOUGH TO
	// EXCLUDE THE TWO WRONG ANSWERS. Pricing each row instead of each band
	// would give 36, 580, 1700 and 1684; drawing the bands uniformly would give
	// 1000 each. Both fall outside every band below except weight 1's, which is
	// why weight 1 is not the rung doing the work.
	auto CheckShare = [&](int32 Weight, int32 Observed, float ExpectedShare,
						  float Tolerance)
	{
		const float Expected = ExpectedShare * static_cast<float>(Draws);
		const float Low = Expected * (1.0f - Tolerance);
		const float High = Expected * (1.0f + Tolerance);
		TestTrue(FString::Printf(
					 TEXT("weight %d took %d of %d draws (%.1f%%), and the "
						  "design says %.1f%% -- expected %.0f to %.0f"),
					 Weight, Observed, Draws,
					 100.0f * static_cast<float>(Observed)
						 / static_cast<float>(Draws),
					 100.0f * ExpectedShare, Low, High),
				 static_cast<float>(Observed) >= Low
					 && static_cast<float>(Observed) <= High);
	};

	// 1, 4, 16 and 64 over a total of 85.
	CheckShare(1, Rarest, 1.0f / 85.0f, 0.40f);
	CheckShare(2, Uncommon, 4.0f / 85.0f, 0.40f);
	CheckShare(3, Moderate, 16.0f / 85.0f, 0.25f);
	CheckShare(4, Commonest, 64.0f / 85.0f, 0.25f);

	return true;
}

// ---------------------------------------------------------------------------
// The pair is struck at one weight
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentPairSharesAWeight,
	"Cataclysm.Enchantments.APairIsBoughtAtItsOwnWeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentPairSharesAWeight::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	UDataTable* Positive = Positives();
	UDataTable* Negative = Negatives();
	if (!Positive || !Negative)
	{
		AddError(TEXT("Could not load the enchantment tables."));
		return false;
	}

	// THIS IS THE TEST THE INDEPENDENT DRAW COULD NOT PASS. Before the pairing
	// rule the two halves were drawn on their own tables with nothing relating
	// them, and 61.4% of pairs came out at different weights -- so this fails on
	// the first handful of rolls rather than needing an unlucky sample.
	//
	// EVERY SLOT, because the pools are filtered per slot and a rule that held
	// only for a chest piece would be no rule.
	static const TCHAR* EverySlot[] = {
		TEXT("Belt"), TEXT("Boots"), TEXT("Chest"), TEXT("Gloves"),
		TEXT("Head"), TEXT("Necklace"), TEXT("Pants"), TEXT("Relic"),
		TEXT("Ring"), TEXT("Shoulders"), TEXT("Weapon") };

	FRandomStream Stream(14530);
	TSet<int32> WeightsSeen;
	int32 PairsChecked = 0;

	for (const TCHAR* Slot : EverySlot)
	{
		// FOUR PAIRS AT A TIME, which is what a Cataclysmic piece holds and the
		// case where the pools have been drawn down furthest.
		for (int32 Try = 0; Try < 60; ++Try)
		{
			TArray<FCataclysmRolledEnchantment> Rolled;
			if (!FDrop::RollEnchantments(Positive, Negative, Slot, 4, Stream,
										 Rolled))
			{
				AddError(FString::Printf(
					TEXT("A '%s' could not fill four enchantments."), Slot));
				return false;
			}

			for (const FCataclysmRolledEnchantment& One : Rolled)
			{
				const FCataclysmEnchantmentRow* Good =
					Positive->FindRow<FCataclysmEnchantmentRow>(
						One.Positive, TEXT("Test"), /*bWarnIfMissing=*/false);
				const FCataclysmEnchantmentRow* Bad =
					Negative->FindRow<FCataclysmEnchantmentRow>(
						One.Negative, TEXT("Test"), /*bWarnIfMissing=*/false);
				if (!Good || !Bad)
				{
					AddError(TEXT("A drawn row is not in its table."));
					return false;
				}

				const int32 GoodWeight = FMath::RoundToInt(Good->Weight);
				const int32 BadWeight = FMath::RoundToInt(Bad->Weight);
				++PairsChecked;
				WeightsSeen.Add(GoodWeight);

				if (GoodWeight != BadWeight)
				{
					AddError(FString::Printf(
						TEXT("On a '%s', the weight %d benefit '%s' was bought "
							 "with a weight %d drawback '%s'. The owner ruled on "
							 "2026-09-07 that a weight decides what a benefit is "
							 "paired with, so that you cannot get the most "
							 "powerful benefits with negatives that barely do "
							 "anything."),
						Slot, GoodWeight, *One.Positive.ToString(),
						BadWeight, *One.Negative.ToString()));
					return false;
				}
			}
		}
	}

	// A RULE NOTHING EXERCISES IS NOT PROVED. If every pair drawn happened to be
	// weight 4, the loop above would pass without ever testing the case the
	// ruling is about. 2,640 pairs at the designed frequencies reach weight 1
	// about 31 times, so all four bands appearing is the expected outcome and
	// its absence is a real failure.
	TestEqual(FString::Printf(TEXT("all four weights were drawn across %d pairs"),
							  PairsChecked),
			  WeightsSeen.Num(), 4);

	return true;
}

// ---------------------------------------------------------------------------
// What the item says about itself
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentTooltipStatesBothHalves,
	"Cataclysm.Enchantments.TheToolTipStatesBothHalvesAndMarksTheDrawback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentTooltipStatesBothHalves::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	UDataTable* Positive = Positives();
	UDataTable* Negative = Negatives();
	if (!Positive || !Negative)
	{
		AddError(TEXT("Could not load the enchantment tables."));
		return false;
	}

	FCataclysmRolledEnchantment Rolled;
	Rolled.Positive = TEXT("Positive_Additional_100_300_crit_multiplier");
	Rolled.Negative = TEXT("Negative_You_have_no_armor");

	// THE ROWS EXIST, checked rather than assumed. A renamed row would otherwise
	// make this test pass by producing no lines and finding nothing wrong.
	const FCataclysmEnchantmentRow* PositiveRow =
		Positive->FindRow<FCataclysmEnchantmentRow>(
			Rolled.Positive, TEXT("Test"), /*bWarnIfMissing=*/false);
	const FCataclysmEnchantmentRow* NegativeRow =
		Negative->FindRow<FCataclysmEnchantmentRow>(
			Rolled.Negative, TEXT("Test"), /*bWarnIfMissing=*/false);
	if (!PositiveRow || !NegativeRow)
	{
		AddError(TEXT("The two rows this test names are no longer in the data. "
					  "Pick two that are; the point is the wording, not these."));
		return false;
	}

	const TArray<FString> Lines = UCataclysmItemTooltip::EnchantmentLines(
		Rolled, Positive, Negative);

	if (!TestEqual(TEXT("one enchantment reads as two lines"), Lines.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("the positive is the sheet's own wording, unmarked"),
			  Lines[0], PositiveRow->Effect);
	TestEqual(TEXT("the negative is marked as a drawback"), Lines[1],
			  FString(UCataclysmItemTooltip::DrawbackPrefix)
				  + NegativeRow->Effect);

	// THE POSITIVE MUST NOT CARRY THE MARK. Issue #45 asks for the negatives to
	// be impossible to miss, which fails if both lines are marked.
	TestFalse(TEXT("the positive carries no drawback mark"),
			  Lines[0].StartsWith(UCataclysmItemTooltip::DrawbackPrefix));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentCataclysmicIsNotBlank,
	"Cataclysm.Enchantments.ACataclysmicItemStatesItsFourEnchantments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentCataclysmicIsNotBlank::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	const FTables Tables = LoadEverything();
	if (!Tables.AllPresent())
	{
		AddError(TEXT("Could not load every table the tool tip needs."));
		return false;
	}

	// THE REGRESSION THIS WHOLE CHANGE IS ABOUT. A Cataclysmic piece holds four
	// enchantments and, by RARITY_COMPOSITION, no regular affixes at all. Before
	// this the tool tip printed one line per affix and nothing for enchantments,
	// so the best item in the game hovered as a name, an upgrade level and a
	// residue line with nothing in between.
	FCataclysmItem Item;
	// THE ROW NAME, NOT THE BASE NAME. ItemBases.csv keys a row as
	// "<Slot>_<BaseName>", so the Circlet is "Head_Circlet". A base the
	// table cannot find produces no implicit lines and no name, and
	// LinesFor then returns nothing at all.
	Item.Base = TEXT("Head_Circlet");
	Item.GearLevel = 10;
	Item.EnchantmentCount = 4;

	FRandomStream Stream(451);
	if (!FDrop::RollEnchantments(Tables.Positive, Tables.Negative,
								 TEXT("Head"), 4, Stream, Item.Enchantments))
	{
		AddError(TEXT("Could not roll four enchantments for a Cataclysmic."));
		return false;
	}

	ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
	UCataclysmItemValues::RarityOf(Item.EnchantmentCount, Item.Affixes.Num(),
								   Rarity);
	if (!TestEqual(TEXT("four enchantments and no affixes is a Cataclysmic"),
				   static_cast<int32>(Rarity),
				   static_cast<int32>(ECataclysmRarity::Cataclysmic)))
	{
		return false;
	}

	const TArray<FString> Lines = UCataclysmItemTooltip::LinesFor(
		Carrying(Item), Tables.Bases, Tables.Affixes, nullptr, Tables.Positive,
		Tables.Negative);

	int32 Drawbacks = 0;
	for (const FString& Line : Lines)
	{
		if (Line.StartsWith(UCataclysmItemTooltip::DrawbackPrefix))
		{
			++Drawbacks;
		}
	}

	TestEqual(TEXT("all four drawbacks are stated"), Drawbacks, 4);

	// EIGHT LINES OF CONTENT: four positives and four drawbacks. Counting them
	// rather than checking "more than none" is what stops a change that prints
	// one enchantment and drops three.
	int32 Effects = 0;
	for (const FCataclysmRolledEnchantment& Rolled : Item.Enchantments)
	{
		Effects += UCataclysmItemTooltip::EnchantmentLines(
			Rolled, Tables.Positive, Tables.Negative).Num();
	}
	TestEqual(TEXT("all eight halves reach the tool tip"), Effects, 8);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentOldSaveIsNotACrash,
	"Cataclysm.Enchantments.AnItemSavedBeforeThisSaysNothingRatherThanBreaking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentOldSaveIsNotACrash::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	const FTables Tables = LoadEverything();
	if (!Tables.AllPresent())
	{
		AddError(TEXT("Could not load every table the tool tip needs."));
		return false;
	}

	// WHAT A SAVE WRITTEN BEFORE THIS FIELD EXISTED LOADS AS: a count with no
	// array behind it. The count is what rarity is read from, so the item is
	// still a Cataclysmic and still has to draw.
	FCataclysmItem Item;
	// THE ROW NAME, NOT THE BASE NAME. ItemBases.csv keys a row as
	// "<Slot>_<BaseName>", so the Circlet is "Head_Circlet". A base the
	// table cannot find produces no implicit lines and no name, and
	// LinesFor then returns nothing at all.
	Item.Base = TEXT("Head_Circlet");
	Item.EnchantmentCount = 4;

	const TArray<FString> Lines = UCataclysmItemTooltip::LinesFor(
		Carrying(Item), Tables.Bases, Tables.Affixes, nullptr, Tables.Positive,
		Tables.Negative);

	TestTrue(TEXT("it still names itself"), Lines.Num() > 0);

	for (const FString& Line : Lines)
	{
		TestFalse(TEXT("and states no drawback it does not have"),
				  Line.StartsWith(UCataclysmItemTooltip::DrawbackPrefix));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
