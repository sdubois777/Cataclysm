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
 * AND THE DRAWBACK IS NEVER MILDER THAN THE BENEFIT. The same owner ruled later
 * on 2026-09-07 that the weight column exists "to determine what benefits go
 * with what negatives ... to ensure you can't get the most powerful benefits
 * with negatives that barely do anything", and then chose a FLOOR rather than an
 * exact match: the drawback is drawn from weight 1 up to the benefit's weight.
 * The two halves used to be drawn independently and the design document used to
 * say so; that sentence was removed, because independent draws defeated the goal
 * stated beside them. ADrawbackIsNeverMilderThanItsBenefit below is what holds
 * the rule, and it is the one test here that could not pass before the rule
 * existed. It also fails an exact match, which was offered and rejected.
 *
 * WHAT IS NOT TESTED HERE. What an enchantment DOES, which is tested in
 * CataclysmEnchantmentEffectTests.cpp since issue #45. Uniqueness across all
 * WORN gear is not enforced at equip time -- the rule enforced below is the
 * narrower one that neither half repeats on a single piece.
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

	/**
	 * Which weight BAND a drawn row belongs to, 1 to 4.
	 *
	 * NOT `FMath::RoundToInt(Row.Weight)`, WHICH IS WRONG FOR A SET ROW. A set
	 * row carries its set identifier of 5 to 18 in the Weight column -- issue
	 * #1443 -- so reading the column directly files a set under a band that does
	 * not exist, and it vanishes from whichever band it should have counted in.
	 *
	 * A SET COUNTS AS WEIGHT 1 BECAUSE THAT IS THE BAND IT SITS IN. The project
	 * owner ruled on 2026-09-08 that sets are drawn "in the same bucket as t1
	 * enchantments", and RollEnchantments puts them inside band 1 rather than
	 * beside it, which is what keeps the band's designed 1.2% share true. A set
	 * drawback counts there too: it is the drawback that arrived with a band 1
	 * benefit, even though it was guaranteed rather than drawn.
	 */
	int32 DrawnBand(const FCataclysmEnchantmentRow& Row)
	{
		return UCataclysmDropRoll::EnchantmentSetId(Row) > 0
			? 1
			: FMath::RoundToInt(Row.Weight);
	}

	/** The row an item records for each set, as a set for fast lookup. */
	TSet<FName> RepresentativesOf(const TArray<FCataclysmEnchantmentSet>& Sets)
	{
		TSet<FName> Out;
		for (const FCataclysmEnchantmentSet& Each : Sets)
		{
			Out.Add(Each.Representative);
		}
		return Out;
	}

	/**
	 * Every set identifier written in one table.
	 *
	 * READ THROUGH EnchantmentSetId RATHER THAN OFF THE Weight COLUMN, so the
	 * tests keep working when issue #1443 moves the identifier into a column of
	 * its own. Reading the column here would make these tests the thing that
	 * blocks that move.
	 */
	TSet<int32> SetIdsIn(const UDataTable* Table)
	{
		TSet<int32> Out;
		if (!Table)
		{
			return Out;
		}
		Table->ForeachRow<FCataclysmEnchantmentRow>(TEXT("SetIdsIn"),
			[&](const FName&, const FCataclysmEnchantmentRow& Row)
			{
				const int32 SetId = UCataclysmDropRoll::EnchantmentSetId(Row);
				if (SetId > 0)
				{
					Out.Add(SetId);
				}
			});
		return Out;
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
	// 5 and 18 are the range the 56 set rows carry a set identifier in, which is
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
						 "a set enters the draw through EnchantmentSetsFor as "
						 "one option, not as loose rows priced by a Weight "
						 "column that holds its set identifier."),
					*Candidate.ToString()));
				return false;
			}
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Sets
//
// THE DEFECT THESE WERE WRITTEN FOR. 55 authored rows -- 42 positives and 13
// negatives typed `Set` -- could not appear in the game at all. Nothing granted
// them and the draw refused them, on the reading that "paired and guaranteed"
// meant some other mechanism handed a set out whole. No such mechanism was ever
// written.
//
// WHAT A SET IS, ruled by the project owner on 2026-09-08: an enchantment, not
// an item. "It's like giving the player the ability to build a custom set piece
// instead of having it be a specific item." An item that rolls Archon's Aegis
// becomes a piece of it, and wearing two, six or ten such pieces turns on that
// set's 2-piece, 6-piece and 10-piece bonus.
//
// SO A DROP RECORDS MEMBERSHIP AND NOT THE BONUSES. Handing out all three
// positive rows would give a 10-piece bonus to a player wearing one piece.
// Counting worn pieces is not written yet, and neither is what any enchantment
// DOES; both are the rest of issue #45.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentASetCanBeRolled,
	"Cataclysm.Enchantments.ASetCanBeRolled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentASetCanBeRolled::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	UDataTable* Positive = Positives();
	UDataTable* Negative = Negatives();
	if (!Positive || !Negative)
	{
		AddError(TEXT("Could not load the enchantment tables from game/Data/."));
		return false;
	}

	TArray<FCataclysmEnchantmentSet> Sets;
	FDrop::EnchantmentSetsFor(Positive, Negative, TEXT("Chest"), Sets);

	// THE POSITIVE CONTROL. Without it, "a set was rolled" could pass on a
	// table holding no sets by rolling nothing and finding nothing.
	if (!TestTrue(TEXT("the data really does hold complete sets to draw"),
				  Sets.Num() > 0))
	{
		return false;
	}

	const TSet<FName> SetRepresentatives = RepresentativesOf(Sets);

	// ENOUGH DRAWS THAT ZERO WOULD MEAN SOMETHING. A set is about one draw in
	// 150, so 4,000 pairs expects about 26. Seeded, because an unseeded
	// sampling test fails once a month and is then ignored.
	FRandomStream Stream(20260908);
	int32 SetsSeen = 0;
	int32 PairsDrawn = 0;
	for (int32 Item = 0; Item < 4000; ++Item)
	{
		TArray<FCataclysmRolledEnchantment> Rolled;
		if (!FDrop::RollEnchantments(Positive, Negative, TEXT("Chest"), 1,
									 Stream, Rolled))
		{
			AddError(TEXT("A one-enchantment roll on a chest failed."));
			return false;
		}
		for (const FCataclysmRolledEnchantment& Each : Rolled)
		{
			++PairsDrawn;
			if (SetRepresentatives.Contains(Each.Positive))
			{
				++SetsSeen;
			}
		}
	}

	// THE DEFECT ITSELF. Before 2026-09-08 this was zero however many were
	// drawn, because EnchantmentSuitsSlot refused every set row.
	if (!TestTrue(
			FString::Printf(
				TEXT("a set can be rolled (saw %d in %d pairs drawn from %d "
					 "complete sets)"),
				SetsSeen, PairsDrawn, Sets.Num()),
			SetsSeen > 0))
	{
		return false;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentSetBringsItsOwnDrawback,
	"Cataclysm.Enchantments.ASetArrivesWithItsOwnDrawbackAndNotADrawnOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentSetBringsItsOwnDrawback::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	UDataTable* Positive = Positives();
	UDataTable* Negative = Negatives();
	if (!Positive || !Negative)
	{
		AddError(TEXT("Could not load the enchantment tables from game/Data/."));
		return false;
	}

	TArray<FCataclysmEnchantmentSet> Sets;
	FDrop::EnchantmentSetsFor(Positive, Negative, TEXT("Chest"), Sets);
	if (!TestTrue(TEXT("the data really does hold complete sets to draw"),
				  Sets.Num() > 0))
	{
		return false;
	}

	// WHICH DRAWBACK EACH SET OWES, so a wrong pairing is caught by name rather
	// than by the pair merely being non-empty.
	TMap<FName, FName> DrawbackFor;
	for (const FCataclysmEnchantmentSet& Each : Sets)
	{
		DrawbackFor.Add(Each.Representative, Each.Negative);
	}

	FRandomStream Stream(775533);
	int32 Checked = 0;
	for (int32 Item = 0; Item < 4000; ++Item)
	{
		TArray<FCataclysmRolledEnchantment> Rolled;
		if (!FDrop::RollEnchantments(Positive, Negative, TEXT("Chest"), 4,
									 Stream, Rolled))
		{
			AddError(TEXT("A four-enchantment roll on a chest failed."));
			return false;
		}
		for (const FCataclysmRolledEnchantment& Each : Rolled)
		{
			const FName* Owed = DrawbackFor.Find(Each.Positive);
			if (!Owed)
			{
				continue;
			}
			++Checked;
			if (Each.Negative != *Owed)
			{
				AddError(FString::Printf(
					TEXT("'%s' is a set and arrived with drawback '%s'. A set "
						 "is paired and guaranteed, so it owes '%s' and never "
						 "a drawback drawn from the ordinary pool."),
					*Each.Positive.ToString(), *Each.Negative.ToString(),
					*Owed->ToString()));
				return false;
			}
		}
	}

	// THE POSITIVE CONTROL AGAIN, in its own right: a loop that never entered
	// its body would report every pairing correct.
	if (!TestTrue(
			FString::Printf(TEXT("at least one set was drawn to check (%d)"),
							Checked),
			Checked > 0))
	{
		return false;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentSetNeedsBothHalves,
	"Cataclysm.Enchantments.ASetWithNoDrawbackWrittenIsNotOffered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentSetNeedsBothHalves::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	UDataTable* Positive = Positives();
	UDataTable* Negative = Negatives();
	if (!Positive || !Negative)
	{
		AddError(TEXT("Could not load the enchantment tables from game/Data/."));
		return false;
	}

	// WRITTEN AGAINST THE RULE AND NOT AGAINST TODAY'S DATA. Set 15, Shard of
	// Anarchy, has three positive rows and no negative row, in
	// game/Data/EnchantmentsNegative.csv and in the design workbook alike, and
	// issue #1494 is that drawback being written. Naming 15 here would make
	// this test fail on the day the gap is closed, which is exactly backwards.
	// So it compares the offered sets against the identifiers present in both
	// tables, and closing the gap moves both sides together.
	const TSet<int32> WithPositives = SetIdsIn(Positive);
	const TSet<int32> WithNegatives = SetIdsIn(Negative);
	const TSet<int32> Complete = WithPositives.Intersect(WithNegatives);

	if (!TestTrue(TEXT("the data really does hold set rows on both sides"),
				  WithPositives.Num() > 0 && WithNegatives.Num() > 0))
	{
		return false;
	}

	TArray<FCataclysmEnchantmentSet> Sets;
	FDrop::EnchantmentSetsFor(Positive, Negative, TEXT("Chest"), Sets);

	TSet<int32> Offered;
	for (const FCataclysmEnchantmentSet& Each : Sets)
	{
		Offered.Add(Each.SetId);

		// EVERY OFFERED SET IS WHOLE. This is the half that would still hold if
		// the intersection above were computed wrongly.
		TestTrue(FString::Printf(
					 TEXT("set %d is offered and carries a drawback"), Each.SetId),
				 !Each.Negative.IsNone());
		TestTrue(FString::Printf(
					 TEXT("set %d is offered and carries positives"), Each.SetId),
				 Each.Positives.Num() > 0);
		TestTrue(FString::Printf(
					 TEXT("set %d records one of its own rows"), Each.SetId),
				 Each.Positives.Contains(Each.Representative));
	}

	TestTrue(FString::Printf(
				 TEXT("exactly the sets written on both sides are offered "
					  "(%d offered, %d complete in the data)"),
				 Offered.Num(), Complete.Num()),
			 Offered.Difference(Complete).Num() == 0
				 && Complete.Difference(Offered).Num() == 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentEverySetIsReachable,
	"Cataclysm.Enchantments.EveryCompleteSetCanBeDrawnAndNotJustSome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentEverySetIsReachable::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	UDataTable* Positive = Positives();
	UDataTable* Negative = Negatives();
	if (!Positive || !Negative)
	{
		AddError(TEXT("Could not load the enchantment tables from game/Data/."));
		return false;
	}

	TArray<FCataclysmEnchantmentSet> Sets;
	FDrop::EnchantmentSetsFor(Positive, Negative, TEXT("Chest"), Sets);
	if (!TestTrue(TEXT("the data really does hold complete sets to draw"),
				  Sets.Num() > 0))
	{
		return false;
	}

	// ONE SET BEING REACHABLE IS NOT ALL OF THEM BEING REACHABLE. A draw that
	// always picked the first option would pass ASetCanBeRolled and fail here.
	TMap<FName, int32> SeenByRepresentative;
	for (const FCataclysmEnchantmentSet& Each : Sets)
	{
		SeenByRepresentative.Add(Each.Representative, 0);
	}

	FRandomStream Stream(4242);
	for (int32 Item = 0; Item < 30000; ++Item)
	{
		TArray<FCataclysmRolledEnchantment> Rolled;
		if (!FDrop::RollEnchantments(Positive, Negative, TEXT("Chest"), 1,
									 Stream, Rolled))
		{
			AddError(TEXT("A one-enchantment roll on a chest failed."));
			return false;
		}
		for (const FCataclysmRolledEnchantment& Each : Rolled)
		{
			if (int32* Count = SeenByRepresentative.Find(Each.Positive))
			{
				++(*Count);
			}
		}
	}

	for (const TPair<FName, int32>& Each : SeenByRepresentative)
	{
		TestTrue(FString::Printf(TEXT("'%s' was drawn at least once (%d)"),
								 *Each.Key.ToString(), Each.Value),
				 Each.Value > 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentSetsShareTheWeightOneBand,
	"Cataclysm.Enchantments.SetsShareTheWeightOneBandRatherThanAddingAFifth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentSetsShareTheWeightOneBand::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	UDataTable* Positive = Positives();
	UDataTable* Negative = Negatives();
	if (!Positive || !Negative)
	{
		AddError(TEXT("Could not load the enchantment tables from game/Data/."));
		return false;
	}

	TArray<FCataclysmEnchantmentSet> Sets;
	FDrop::EnchantmentSetsFor(Positive, Negative, TEXT("Chest"), Sets);
	TArray<TArray<FName>> ByWeight;
	FDrop::EnchantmentCandidatesByWeight(Positive, TEXT("Chest"), ByWeight);
	const int32 OrdinaryWeightOne = ByWeight[0].Num();

	if (!TestTrue(TEXT("there are both sets and ordinary weight 1 rows"),
				  Sets.Num() > 0 && OrdinaryWeightOne > 0))
	{
		return false;
	}

	const TSet<FName> SetRepresentatives = RepresentativesOf(Sets);
	TSet<FName> WeightOneRows(ByWeight[0]);

	FRandomStream Stream(90807);
	int32 SetsSeen = 0;
	int32 WeightOneSeen = 0;
	const int32 Draws = 60000;
	for (int32 Item = 0; Item < Draws; ++Item)
	{
		TArray<FCataclysmRolledEnchantment> Rolled;
		if (!FDrop::RollEnchantments(Positive, Negative, TEXT("Chest"), 1,
									 Stream, Rolled))
		{
			AddError(TEXT("A one-enchantment roll on a chest failed."));
			return false;
		}
		for (const FCataclysmRolledEnchantment& Each : Rolled)
		{
			if (SetRepresentatives.Contains(Each.Positive))
			{
				++SetsSeen;
			}
			else if (WeightOneRows.Contains(Each.Positive))
			{
				++WeightOneSeen;
			}
		}
	}

	// THE WHOLE BAND STILL TAKES ITS DESIGNED SHARE. `docs/Cataclysm_GDD_v2.md`
	// states weight 1 at 1.2% of draws, and putting sets INSIDE that band
	// rather than beside it is what keeps that number true. A fifth band would
	// push the band's own share down and every other weight's with it.
	const float BandShare =
		static_cast<float>(SetsSeen + WeightOneSeen) / static_cast<float>(Draws);
	TestTrue(FString::Printf(
				 TEXT("the weight 1 band still takes about 1.2%% of draws "
					  "(%.2f%% over %d)"),
				 BandShare * 100.0f, Draws),
			 BandShare > 0.008f && BandShare < 0.017f);

	// AND THE BAND IS SHARED IN PROPORTION TO THE OPTIONS IN IT. Uniform inside
	// the band means a set is exactly as likely as any one weight 1 row, so the
	// split follows the counts and not a separate frequency.
	const float ExpectedSetShare = static_cast<float>(Sets.Num())
		/ static_cast<float>(Sets.Num() + OrdinaryWeightOne);
	const float ActualSetShare = SetsSeen + WeightOneSeen > 0
		? static_cast<float>(SetsSeen)
			/ static_cast<float>(SetsSeen + WeightOneSeen)
		: 0.0f;
	TestTrue(FString::Printf(
				 TEXT("sets take their share of the band by count "
					  "(expected %.2f, saw %.2f from %d sets and %d rows)"),
				 ExpectedSetShare, ActualSetShare, Sets.Num(),
				 OrdinaryWeightOne),
			 FMath::Abs(ActualSetShare - ExpectedSetShare) < 0.08f);

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
		++DrawsByWeight.FindOrAdd(DrawnBand(*Row));
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
	auto CheckShare = [&](const TCHAR* Half, int32 Weight, int32 Observed,
						  float ExpectedShare, float Tolerance)
	{
		const float Expected = ExpectedShare * static_cast<float>(Draws);
		const float Low = Expected * (1.0f - Tolerance);
		const float High = Expected * (1.0f + Tolerance);
		TestTrue(FString::Printf(
					 TEXT("weight %d %s took %d of %d draws (%.2f%%), and the "
						  "design says %.2f%% -- expected %.0f to %.0f"),
					 Weight, Half, Observed, Draws,
					 100.0f * static_cast<float>(Observed)
						 / static_cast<float>(Draws),
					 100.0f * ExpectedShare, Low, High),
				 static_cast<float>(Observed) >= Low
					 && static_cast<float>(Observed) <= High);
	};

	// 1, 4, 16 and 64 over a total of 85.
	CheckShare(TEXT("benefits"), 1, Rarest, 1.0f / 85.0f, 0.40f);
	CheckShare(TEXT("benefits"), 2, Uncommon, 4.0f / 85.0f, 0.40f);
	CheckShare(TEXT("benefits"), 3, Moderate, 16.0f / 85.0f, 0.25f);
	CheckShare(TEXT("benefits"), 4, Commonest, 64.0f / 85.0f, 0.25f);

	// THE DRAWBACK SIDE IS A DIFFERENT DISTRIBUTION AND IT IS DERIVED, NOT
	// CHOSEN. A drawback is drawn from weight 1 up to the benefit's weight, so
	// every benefit tier can reach a weight 1 drawback and only the top tier can
	// reach a weight 1 benefit. Summing the same ladder renormalised over each
	// allowed range gives 3.90%, 10.89%, 28.51% and 56.69%.
	//
	// SO HARSH DRAWBACKS ARE 3.31 TIMES COMMONER THAN POWERFUL BENEFITS, which
	// is a consequence of the floor rather than a decision anyone took, and is
	// the reason this is measured here rather than asserted in a document.
	TMap<int32, int32> DrawbacksByWeight;
	FRandomStream DrawbackStream(90713);
	for (int32 Try = 0; Try < Draws; ++Try)
	{
		TArray<FCataclysmRolledEnchantment> Rolled;
		if (!FDrop::RollEnchantments(Positive, Negative, TEXT("Chest"), 1,
									 DrawbackStream, Rolled))
		{
			AddError(TEXT("A single enchantment could not be drawn."));
			return false;
		}
		const FCataclysmEnchantmentRow* Row =
			Negative->FindRow<FCataclysmEnchantmentRow>(
				Rolled[0].Negative, TEXT("Test"), /*bWarnIfMissing=*/false);
		if (!Row)
		{
			AddError(TEXT("A drawn drawback is not in the table."));
			return false;
		}
		++DrawbacksByWeight.FindOrAdd(DrawnBand(*Row));
	}

	CheckShare(TEXT("drawbacks"), 1, DrawbacksByWeight.FindRef(1), 0.0390f, 0.30f);
	CheckShare(TEXT("drawbacks"), 2, DrawbacksByWeight.FindRef(2), 0.1089f, 0.30f);
	CheckShare(TEXT("drawbacks"), 3, DrawbacksByWeight.FindRef(3), 0.2851f, 0.25f);
	CheckShare(TEXT("drawbacks"), 4, DrawbacksByWeight.FindRef(4), 0.5669f, 0.25f);

	return true;
}

// ---------------------------------------------------------------------------
// The drawback is never milder than the benefit
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentDrawbackIsNeverMilder,
	"Cataclysm.Enchantments.ADrawbackIsNeverMilderThanItsBenefit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentDrawbackIsNeverMilder::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentTest;

	UDataTable* Positive = Positives();
	UDataTable* Negative = Negatives();
	if (!Positive || !Negative)
	{
		AddError(TEXT("Could not load the enchantment tables."));
		return false;
	}

	// THE RULE. A LOWER WEIGHT IS THE HARSHER ONE, so the drawback's weight must
	// be at or below the benefit's. Weight 1 benefit takes a weight 1 drawback
	// only; weight 4 takes anything.
	//
	// THIS IS THE TEST THE INDEPENDENT DRAW COULD NOT PASS. The two halves used
	// to be drawn on their own tables with nothing relating them, so a weight 1
	// benefit came with a milder drawback 99.2% of the time -- this fails on the
	// first handful of rolls rather than needing an unlucky sample.
	//
	// EVERY SLOT, because the pools are filtered per slot and a rule that held
	// only for a chest piece would be no rule.
	static const TCHAR* EverySlot[] = {
		TEXT("Belt"), TEXT("Boots"), TEXT("Chest"), TEXT("Gloves"),
		TEXT("Head"), TEXT("Necklace"), TEXT("Pants"), TEXT("Relic"),
		TEXT("Ring"), TEXT("Shoulders"), TEXT("Weapon") };

	FRandomStream Stream(14530);
	TSet<int32> BenefitWeightsSeen;
	int32 PairsChecked = 0;
	int32 StrictlyHarsher = 0;
	int32 CheapAndCursed = 0;

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
				const FCataclysmEnchantmentRow* Benefit =
					Positive->FindRow<FCataclysmEnchantmentRow>(
						One.Positive, TEXT("Test"), /*bWarnIfMissing=*/false);
				const FCataclysmEnchantmentRow* Drawback =
					Negative->FindRow<FCataclysmEnchantmentRow>(
						One.Negative, TEXT("Test"), /*bWarnIfMissing=*/false);
				if (!Benefit || !Drawback)
				{
					AddError(TEXT("A drawn row is not in its table."));
					return false;
				}

				const int32 BenefitWeight = DrawnBand(*Benefit);
				const int32 DrawbackWeight = DrawnBand(*Drawback);
				++PairsChecked;
				BenefitWeightsSeen.Add(BenefitWeight);
				if (DrawbackWeight < BenefitWeight)
				{
					++StrictlyHarsher;
				}
				if (BenefitWeight == 4 && DrawbackWeight == 1)
				{
					++CheapAndCursed;
				}

				if (DrawbackWeight > BenefitWeight)
				{
					AddError(FString::Printf(
						TEXT("On a '%s', the weight %d benefit '%s' was bought "
							 "with a MILDER weight %d drawback '%s'. The owner "
							 "chose on 2026-09-07 that a drawback is never "
							 "milder than its benefit, so that you cannot get "
							 "the most powerful benefits with negatives that "
							 "barely do anything."),
						Slot, BenefitWeight, *One.Positive.ToString(),
						DrawbackWeight, *One.Negative.ToString()));
					return false;
				}
			}
		}
	}

	// A RULE NOTHING EXERCISES IS NOT PROVED. If every pair drawn happened to be
	// weight 4, the loop above would pass without ever testing the case the
	// ruling is about. 2,640 pairs at the designed frequencies reach weight 1
	// about 31 times, so all four appearing is the expected outcome.
	TestEqual(FString::Printf(
				  TEXT("all four benefit weights were drawn across %d pairs"),
				  PairsChecked),
			  BenefitWeightsSeen.Num(), 4);

	// A FLOOR AND NOT A MATCH, WHICH IS THE HALF ABOVE CANNOT SEE. An
	// implementation that drew both halves at exactly one weight would satisfy
	// every assertion so far, and the owner rejected exactly that on 2026-09-07
	// in favour of a floor. About 24.0% of pairs should take a drawback harsher
	// than their benefit -- roughly 634 of 2,640 -- so a floor of a tenth of the
	// pairs is clear of sampling noise and of zero.
	TestTrue(FString::Printf(
				 TEXT("%d of %d pairs took a drawback harsher than their "
					  "benefit, which an exact match would never produce"),
				 StrictlyHarsher, PairsChecked),
			 StrictlyHarsher > PairsChecked / 10);

	// AND THE CASE THE OWNER ASKED ROOM FOR ACTUALLY HAPPENS: a modest weight 4
	// benefit carrying the harshest weight 1 drawback, the "genuinely cursed
	// low-value item". About 0.9% of pairs, so roughly 23 of 2,640. This is also
	// what separates the floor from a within-one-step rule, which was the third
	// option offered and would make this outcome impossible.
	TestTrue(FString::Printf(
				 TEXT("%d of %d pairs were a weight 4 benefit with a weight 1 "
					  "drawback"),
				 CheapAndCursed, PairsChecked),
			 CheapAndCursed > 0);

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
