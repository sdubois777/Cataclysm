// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmItem.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * Tests for what a drop rolls: its rarity, its sockets, its affix tiers and its
 * name.
 *
 * THE NUMBERS ARE QUOTED FROM `sim/cataclysm_sim/loot.py`, printed by
 * `loot.rarity_distribution` and `affixes.affix_tier_distribution` rather than
 * recomputed here. A figure recomputed by the same reasoning that produced the
 * code would not test anything; a figure taken from the model tests that the
 * port agrees with it.
 *
 * EVERY DRAW IS SEEDED, so these are deterministic. A sampling test with an
 * unseeded stream is a test that fails once a month and is then ignored.
 */

namespace CataclysmDropRollTest
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

	/** The eight rarities, weakest first. */
	const TArray<ECataclysmRarity>& Ladder()
	{
		static const TArray<ECataclysmRarity> Rarities = {
			ECataclysmRarity::Everyday,	   ECataclysmRarity::Quality,
			ECataclysmRarity::Superb,	   ECataclysmRarity::Masterful,
			ECataclysmRarity::Legendary,   ECataclysmRarity::Mythical,
			ECataclysmRarity::Ascendant,   ECataclysmRarity::Cataclysmic,
		};
		return Rarities;
	}

	FCataclysmRolledAffix Rolled(const TCHAR* Affix, int32 Tier, float Roll)
	{
		FCataclysmRolledAffix Out;
		Out.Affix = FName(Affix);
		Out.Tier = Tier;
		Out.Roll = Roll;
		return Out;
	}

	/** A base row by its key, or null. */
	const FCataclysmItemBaseRow* Base(const UDataTable* Table, const TCHAR* Key)
	{
		return Table ? Table->FindRow<FCataclysmItemBaseRow>(
			FName(Key), TEXT("test"), /*bWarnIfMissing=*/false) : nullptr;
	}
}

// ---------------------------------------------------------------------------
// The ladder, and the join between the table and the enum
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropLadderTest,
	"Cataclysm.Drop.EveryRarityHasARowTheEngineCanFind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropLadderTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDropRollTest;

	UDataTable* Rarities = LoadTable<FCataclysmGearRarityRow>(TEXT("GearRarity.csv"));
	if (!TestNotNull(TEXT("GearRarity.csv loads"), Rarities))
	{
		return false;
	}

	// THE JOIN THIS WHOLE TABLE DEPENDS ON. FCataclysmGearRarityRow carries no
	// ladder column, unlike FCataclysmEnemyRarityRow, because ECataclysmRarity
	// already states the order and each row is keyed on the enum entry's name.
	// A rarity renamed on one side would look up nothing, and a DataTable
	// returning no row is not an error Unreal reports.
	for (ECataclysmRarity Rarity : Ladder())
	{
		const FCataclysmGearRarityRow* Row = FDrop::RarityRow(Rarities, Rarity);
		if (!TestNotNull(*FString::Printf(TEXT("a row for %s"),
										  *FDrop::RowNameFor(Rarity).ToString()),
						 Row))
		{
			continue;
		}
		TestEqual(TEXT("and the row names itself the same way"),
				  Row->Rarity, FDrop::RowNameFor(Rarity).ToString());
	}

	// The gates the design document states, and the only four that are not 0.
	TestEqual(TEXT("Legendary needs +4"),
		FDrop::GearLevelGateFor(Rarities, ECataclysmRarity::Legendary), 4);
	TestEqual(TEXT("Mythical needs +6"),
		FDrop::GearLevelGateFor(Rarities, ECataclysmRarity::Mythical), 6);
	TestEqual(TEXT("Ascendant needs +8"),
		FDrop::GearLevelGateFor(Rarities, ECataclysmRarity::Ascendant), 8);
	TestEqual(TEXT("Cataclysmic needs +10"),
		FDrop::GearLevelGateFor(Rarities, ECataclysmRarity::Cataclysmic), 10);
	TestEqual(TEXT("and Masterful needs nothing"),
		FDrop::GearLevelGateFor(Rarities, ECataclysmRarity::Masterful), 0);

	// The tier cap, quoted from loot.best_rarity_on_a_drop for tiers 1 to 8.
	const ECataclysmRarity Expected[] = {
		ECataclysmRarity::Quality,	   ECataclysmRarity::Superb,
		ECataclysmRarity::Masterful,   ECataclysmRarity::Legendary,
		ECataclysmRarity::Mythical,	   ECataclysmRarity::Ascendant,
		ECataclysmRarity::Cataclysmic, ECataclysmRarity::Cataclysmic,
	};
	for (int32 Tier = 1; Tier <= FDrop::DifficultyTiers; ++Tier)
	{
		TestTrue(*FString::Printf(TEXT("tier %d reaches %s"), Tier,
			*FDrop::RowNameFor(Expected[Tier - 1]).ToString()),
			FDrop::BestRarityOnADrop(Tier) == Expected[Tier - 1]);
	}

	return true;
}

// ---------------------------------------------------------------------------
// The cascade
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropCascadeTest,
	"Cataclysm.Drop.TheRarityCascadeMatchesTheModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropCascadeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDropRollTest;

	UDataTable* Rarities = LoadTable<FCataclysmGearRarityRow>(TEXT("GearRarity.csv"));
	if (!TestNotNull(TEXT("GearRarity.csv loads"), Rarities))
	{
		return false;
	}

	// Printed by loot.rarity_distribution(8, 0.0). Relative rather than
	// absolute, because the shares run from 0.61 down to 0.0000392 and one
	// tolerance cannot serve both ends.
	const float RarityShareAtTier8[] = {
		0.6120010967f, 0.2448004387f, 0.0979201755f, 0.0391680702f,
		0.0048960088f, 0.0009792018f, 0.0001958404f, 0.0000391681f,
	};

	TArray<float> Shares;
	FDrop::RarityDistribution(Rarities, 8, 0.0f, Shares);
	if (!TestEqual(TEXT("eight shares"), Shares.Num(), 8))
	{
		return false;
	}
	for (int32 Index = 0; Index < 8; ++Index)
	{
		TestTrue(*FString::Printf(
			TEXT("%s is %.8f of drops at tier 8 (%.8f)"),
			*FDrop::RowNameFor(Ladder()[Index]).ToString(),
			RarityShareAtTier8[Index], Shares[Index]),
			FMath::Abs(Shares[Index] - RarityShareAtTier8[Index])
				< RarityShareAtTier8[Index] * 1e-4f);
	}

	// THE HEADLINE FIGURE THE DECISION WAS MADE ON: one Cataclysmic in 25,531.
	TestTrue(*FString::Printf(TEXT("a Cataclysmic drop is one in 25,531 (%.0f)"),
							  1.0f / Shares[7]),
		FMath::Abs(1.0f / Shares[7] - 25531.0f) < 5.0f);

	// AND MASTERFUL STAYS COMMON, which is the point the whole weighting rests
	// on: the design fits its affix values against a full set of it, and
	// crafting promotes a piece upward from there.
	TestTrue(*FString::Printf(TEXT("and a Masterful drop is one in 26 (%.1f)"),
							  1.0f / Shares[3]),
		FMath::Abs(1.0f / Shares[3] - 25.5f) < 0.5f);

	// A LOW TIER REACHES ONLY TWO RUNGS. Printed by
	// loot.rarity_distribution(1, 0.0).
	FDrop::RarityDistribution(Rarities, 1, 0.0f, Shares);
	TestTrue(TEXT("tier 1 is five sevenths Everyday"),
		FMath::Abs(Shares[0] - 0.7142857143f) < 1e-5f);
	TestTrue(TEXT("and two sevenths Quality"),
		FMath::Abs(Shares[1] - 0.2857142857f) < 1e-5f);
	for (int32 Index = 2; Index < 8; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("and nothing above Quality (%s)"),
			*FDrop::RowNameFor(Ladder()[Index]).ToString()), Shares[Index], 0.0f);
	}

	// EVERY DISTRIBUTION SUMS TO ONE, at every tier and with magic find on. It
	// is a cascade, so the floor takes whatever fell through; a sum below one
	// would mean drops that rolled nothing at all.
	for (int32 Tier = 1; Tier <= FDrop::DifficultyTiers; ++Tier)
	{
		for (float MagicFind : { 0.0f, 50.0f, 400.0f })
		{
			FDrop::RarityDistribution(Rarities, Tier, MagicFind, Shares);
			float Total = 0.0f;
			for (float Share : Shares)
			{
				Total += Share;
			}
			TestTrue(*FString::Printf(
				TEXT("tier %d at %.0f%% magic find sums to 1 (%.6f)"),
				Tier, MagicFind, Total), FMath::Abs(Total - 1.0f) < 1e-4f);
		}
	}

	// MAGIC FIND MULTIPLIES EACH STEP, which is Path of Exile's stated
	// behaviour. Printed by loot.rarity_distribution(8, 100.0).
	FDrop::RarityDistribution(Rarities, 8, 100.0f, Shares);
	TestTrue(*FString::Printf(
		TEXT("+100%% magic find doubles the Cataclysmic share (%.9f)"), Shares[7]),
		FMath::Abs(Shares[7] - 0.0000783361f) < 0.0000783361f * 1e-3f);
	TestTrue(TEXT("and drops the Everyday share below a third"),
		FMath::Abs(Shares[0] - 0.3099783392f) < 1e-4f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropRollDrawsFromItTest,
	"Cataclysm.Drop.RollingDrawsFromTheStatedDistribution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropRollDrawsFromItTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDropRollTest;

	UDataTable* Rarities = LoadTable<FCataclysmGearRarityRow>(TEXT("GearRarity.csv"));
	if (!TestNotNull(TEXT("GearRarity.csv loads"), Rarities))
	{
		return false;
	}

	// WHY THIS EXISTS SEPARATELY FROM THE TEST ABOVE. RollRarity walks the rungs
	// itself rather than looking up RarityDistribution, so the exact figures
	// checked above describe what really happens only if the two agree. That is
	// deliberate in the model too, and this is the test that ties them together.
	TArray<float> Shares;
	FDrop::RarityDistribution(Rarities, 8, 0.0f, Shares);

	constexpr int32 Draws = 20000;
	FRandomStream Stream(/*InSeed=*/20260818);
	TArray<int32> Counts;
	Counts.Init(0, 8);
	for (int32 Draw = 0; Draw < Draws; ++Draw)
	{
		++Counts[static_cast<int32>(FDrop::RollRarity(Rarities, 8, 0.0f, Stream))];
	}

	// ONLY THE RUNGS WITH ENOUGH MASS TO MEASURE. Legendary is one drop in 204,
	// so twenty thousand draws give about ninety-eight of them and the noise is
	// larger than any defect worth catching. The rare end is checked by
	// saturating it instead, below.
	for (int32 Index = 0; Index <= 3; ++Index)
	{
		const float Seen = static_cast<float>(Counts[Index]) / Draws;
		TestTrue(*FString::Printf(
			TEXT("%s came up %.4f of the time against a stated %.4f"),
			*FDrop::RowNameFor(Ladder()[Index]).ToString(), Seen, Shares[Index]),
			FMath::Abs(Seen - Shares[Index]) < Shares[Index] * 0.1f);
	}

	// NOTHING ABOVE THE TIER CAP, EVER. Checked as a count rather than a share,
	// because one drop over the cap is a defect and not noise.
	FRandomStream Low(/*InSeed=*/7);
	for (int32 Draw = 0; Draw < 5000; ++Draw)
	{
		const ECataclysmRarity Rolled = FDrop::RollRarity(Rarities, 2, 0.0f, Low);
		if (static_cast<int32>(Rolled) > static_cast<int32>(ECataclysmRarity::Superb))
		{
			AddError(FString::Printf(
				TEXT("a tier 2 drop rolled %s, above the Superb cap"),
				*FDrop::RowNameFor(Rolled).ToString()));
			break;
		}
	}

	// A CATACLYSMIC DROP CANNOT BE ROLLED FOR IN A TEST ANY MORE, at one in
	// 25,531. It is forced with a magic find no character could have, which
	// saturates the top rung to certainty and is the real code path rather than
	// a special case.
	FRandomStream Lucky(/*InSeed=*/1);
	TestTrue(TEXT("a saturating magic find always drops Cataclysmic at tier 8"),
		FDrop::RollRarity(Rarities, 8, 5000000.0f, Lucky)
			== ECataclysmRarity::Cataclysmic);
	TestTrue(TEXT("and the step chance it saturates to is exactly 1"),
		FDrop::RarityStepChance(Rarities, ECataclysmRarity::Cataclysmic,
								5000000.0f) == 1.0f);

	return true;
}

// ---------------------------------------------------------------------------
// Residue
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropResidueTest,
	"Cataclysm.Drop.EveryDropCarriesResidueInsideItsBand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropResidueTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDropRollTest;

	UDataTable* Rarities = LoadTable<FCataclysmGearRarityRow>(TEXT("GearRarity.csv"));
	if (!TestNotNull(TEXT("GearRarity.csv loads"), Rarities))
	{
		return false;
	}

	// The band the project owner set on 2026-08-18, after rejecting two lighter
	// proposals topping out at 50 and at 100 as too safe.
	float Lowest = 0.0f;
	float Highest = 0.0f;
	TestTrue(TEXT("Cataclysmic has a band"),
		FDrop::ResidueBandFor(Rarities, ECataclysmRarity::Cataclysmic,
							  Lowest, Highest));
	TestEqual(TEXT("and it runs 300 to 500"), Lowest, 300.0f);
	TestEqual(TEXT("and it runs 300 to 500"), Highest, 500.0f);

	FRandomStream Stream(/*InSeed=*/99);
	for (ECataclysmRarity Rarity : Ladder())
	{
		FDrop::ResidueBandFor(Rarities, Rarity, Lowest, Highest);
		for (int32 Draw = 0; Draw < 200; ++Draw)
		{
			const float Residue = FDrop::RollResidue(Rarities, Rarity, Stream);
			if (Residue < Lowest || Residue > Highest)
			{
				AddError(FString::Printf(
					TEXT("a %s drop carried %.0f residue, outside %.0f to %.0f"),
					*FDrop::RowNameFor(Rarity).ToString(), Residue,
					Lowest, Highest));
				break;
			}
			// A WHOLE NUMBER OF POINTS, because residue is counted in points
			// everywhere else: the craft day penalty is CR / 100 rounded down.
			if (Residue != FMath::FloorToFloat(Residue))
			{
				AddError(FString::Printf(TEXT("residue %.4f is not whole"),
										 Residue));
				break;
			}
		}
	}

	// EVERY RARITY ABOVE QUALITY CAN ARRIVE PAST THE CRITICAL RESIDUE OF 100,
	// which is what makes a good item expensive to improve. Treating that
	// threshold as a ceiling was wrong twice before the project owner corrected
	// it, so it is asserted here rather than assumed.
	FDrop::ResidueBandFor(Rarities, ECataclysmRarity::Superb, Lowest, Highest);
	TestTrue(TEXT("a Superb drop can arrive past the 100 critical residue"),
		Highest > 100.0f);

	return true;
}

// ---------------------------------------------------------------------------
// Sockets
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropSocketsTest,
	"Cataclysm.Drop.SocketsRollFromNoneUpToTheBaseMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropSocketsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDropRollTest;

	UDataTable* Sockets = LoadTable<FCataclysmItemSocketRow>(TEXT("ItemSockets.csv"));
	UDataTable* Bases = LoadTable<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
	if (!TestNotNull(TEXT("ItemSockets.csv loads"), Sockets)
		|| !TestNotNull(TEXT("ItemBases.csv loads"), Bases))
	{
		return false;
	}

	const FCataclysmItemBaseRow* Cuirass = Base(Bases, TEXT("Chest_Cuirass"));
	const FCataclysmItemBaseRow* Greataxe = Base(Bases, TEXT("Weapon_Greataxe"));
	const FCataclysmItemBaseRow* Helm = Base(Bases, TEXT("Head_Helm"));
	if (!TestNotNull(TEXT("the Cuirass base"), Cuirass)
		|| !TestNotNull(TEXT("the Greataxe base"), Greataxe)
		|| !TestNotNull(TEXT("the Helm base"), Helm))
	{
		return false;
	}

	TestEqual(TEXT("a Chest holds six, the most of any armour piece"),
		FDrop::MaxSocketsFor(Sockets, *Cuirass), 6);
	TestEqual(TEXT("a Head holds two"), FDrop::MaxSocketsFor(Sockets, *Helm), 2);

	// A WEAPON IS MATCHED ON ITS HAND COUNT, not on its slot alone, which is
	// the whole reason the sheet has two Weapon rows. A Greataxe is two-handed.
	TestEqual(TEXT("a two-handed weapon holds six"),
		FDrop::MaxSocketsFor(Sockets, *Greataxe), 6);

	// EVERY COUNT FROM NONE TO THE MAXIMUM IS REACHABLE, which is the shape the
	// project owner asked for: a socket count carries no progression, so a tier
	// 1 Chest can drop with all six. Weighting the roll toward fewer, and
	// capping it by the difficulty tier the way Diablo 2 does, were both put to
	// them and declined.
	FRandomStream Stream(/*InSeed=*/4242);
	TArray<int32> Seen;
	Seen.Init(0, 7);
	for (int32 Draw = 0; Draw < 4000; ++Draw)
	{
		const int32 Count = FDrop::RollSockets(Sockets, *Cuirass, Stream);
		if (Count < 0 || Count > 6)
		{
			AddError(FString::Printf(TEXT("a Cuirass rolled %d sockets"), Count));
			return false;
		}
		++Seen[Count];
	}
	for (int32 Count = 0; Count <= 6; ++Count)
	{
		TestTrue(*FString::Printf(TEXT("a Cuirass can drop with %d sockets "
									   "(seen %d times in 4000)"),
								  Count, Seen[Count]), Seen[Count] > 0);
	}

	// AND A DROP WITH NONE IS NOT A RUINED ITEM. The Add Socket craft only has
	// something to do because drops arrive below their maximum, so a roll of
	// zero has to be reachable rather than merely not crashing.
	TestTrue(TEXT("and a Cuirass really can drop with none"), Seen[0] > 0);

	// EVERY GEAR SLOT HAS A MAXIMUM. A base whose slot is missing from the
	// sheet would drop with no sockets and nothing would say why.
	Bases->ForeachRow<FCataclysmItemBaseRow>(TEXT("every base has a maximum"),
		[&](const FName& Key, const FCataclysmItemBaseRow& Row)
		{
			if (FDrop::MaxSocketsFor(Sockets, Row) < 1)
			{
				AddError(FString::Printf(
					TEXT("%s is in slot '%s' with %d hand(s) and has no socket "
						 "maximum"), *Key.ToString(), *Row.Slot, Row.Hands));
			}
		});

	return true;
}

// ---------------------------------------------------------------------------
// Affix tiers
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropAffixTierTest,
	"Cataclysm.Drop.AffixTiersHalveAndAreCappedByTheDifficultyTier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropAffixTierTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDropRollTest;

	UDataTable* Tiers = LoadTable<FCataclysmAffixTierRow>(TEXT("AffixTiers.csv"));
	if (!TestNotNull(TEXT("AffixTiers.csv loads"), Tiers))
	{
		return false;
	}

	// Printed by affixes.max_affix_tier_on_a_drop for tiers 1 to 8. The cap
	// reaches T7 at difficulty tier 6 and stays there.
	const int32 Caps[] = { 2, 3, 4, 5, 6, 7, 7, 7 };
	for (int32 Tier = 1; Tier <= FDrop::DifficultyTiers; ++Tier)
	{
		TestEqual(*FString::Printf(TEXT("tier %d reaches T%d"),
								   Tier, Caps[Tier - 1]),
			FDrop::MaxAffixTierOnADrop(Tier), Caps[Tier - 1]);
	}

	// Printed by affixes.affix_tier_distribution(8). Half of every affix that
	// drops is a T1, and a T7 is one in 127.
	const float AffixTierShareAtTier8[] = {
		0.5039370079f, 0.2519685039f, 0.1259842520f, 0.0629921260f,
		0.0314960630f, 0.0157480315f, 0.0078740157f,
	};

	constexpr int32 Draws = 40000;
	FRandomStream Stream(/*InSeed=*/818);
	TArray<int32> Counts;
	Counts.Init(0, UCataclysmItemValues::MaxAffixTier + 1);
	for (int32 Draw = 0; Draw < Draws; ++Draw)
	{
		const int32 Tier = FDrop::RollAffixTier(Tiers, 8, Stream);
		if (Tier < 1 || Tier > UCataclysmItemValues::MaxAffixTier)
		{
			AddError(FString::Printf(TEXT("an affix rolled tier %d"), Tier));
			return false;
		}
		++Counts[Tier];
	}
	for (int32 Tier = 1; Tier <= 5; ++Tier)
	{
		const float Seen = static_cast<float>(Counts[Tier]) / Draws;
		TestTrue(*FString::Printf(
			TEXT("T%d came up %.4f of the time against a stated %.4f"),
			Tier, Seen, AffixTierShareAtTier8[Tier - 1]),
			FMath::Abs(Seen - AffixTierShareAtTier8[Tier - 1])
				< AffixTierShareAtTier8[Tier - 1] * 0.12f);
	}
	TestTrue(TEXT("and the two rarest tiers still turn up"),
		Counts[6] > 0 && Counts[7] > 0);

	// NOTHING ABOVE THE CAP. At difficulty tier 2 the cap is T3, and a drop
	// reaching T4 there would hand a low-tier player something the forge should
	// have been the only route to.
	FRandomStream Shallow(/*InSeed=*/3);
	for (int32 Draw = 0; Draw < 4000; ++Draw)
	{
		const int32 Tier = FDrop::RollAffixTier(Tiers, 2, Shallow);
		if (Tier > 3)
		{
			AddError(FString::Printf(
				TEXT("a tier 2 drop rolled a T%d affix, above the T3 cap"), Tier));
			break;
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// What a kill drops
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillDropRatesTest,
	"Cataclysm.Drop.AKillDropsAtTheRateItsRarityStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmKillDropRatesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDropRollTest;

	UDataTable* Drops = LoadTable<FCataclysmEnemyDropRow>(TEXT("EnemyDrops.csv"));
	UDataTable* Rarities =
		LoadTable<FCataclysmEnemyRarityRow>(TEXT("EnemyRarities.csv"));
	if (!TestNotNull(TEXT("EnemyDrops.csv loads"), Drops)
		|| !TestNotNull(TEXT("EnemyRarities.csv loads"), Rarities))
	{
		return false;
	}

	// THE TWO TABLES JOIN ON THE ROW KEY. EnemyRarities is generated from the
	// simulation's enemy model and EnemyDrops is typed into the workbook, so a
	// rarity in one and not the other would be a creature that drops nothing,
	// or a drop rate for a creature that cannot exist.
	const TArray<FName> Known = Rarities->GetRowNames();
	for (const FName& Key : Known)
	{
		TestNotNull(*FString::Printf(TEXT("a drop row for %s"), *Key.ToString()),
			FDrop::EnemyDropRow(Drops, Key));
	}
	TestEqual(TEXT("and no drop rows for creatures that do not exist"),
		Drops->GetRowNames().Num(), Known.Num());

	// The figures the workbook states, quoted rather than recomputed.
	struct FExpected { const TCHAR* Key; float Gear; float Materials; float Find; };
	const FExpected Rows[] = {
		{ TEXT("Common"),         0.16f,  0.32f,   0.0f },
		{ TEXT("Elite"),          0.5f,   1.0f,   50.0f },
		{ TEXT("Legendary"),      1.0f,   2.0f,  100.0f },
		{ TEXT("Herald"),         2.0f,   4.0f,  150.0f },
		{ TEXT("Boss"),           5.0f,  10.0f,  300.0f },
		{ TEXT("Cataclysm_Boss"), 12.0f, 24.0f,  500.0f },
	};

	for (const FExpected& Row : Rows)
	{
		const FName Key(Row.Key);
		TestEqual(*FString::Printf(TEXT("%s drops %.2f gear"), Row.Key, Row.Gear),
			FDrop::ExpectedGearDrops(Drops, Key, FDrop::BaselineLootQuantity),
			Row.Gear);
		TestEqual(*FString::Printf(TEXT("%s drops %.2f materials"), Row.Key,
								   Row.Materials),
			FDrop::ExpectedMaterialDrops(Drops, Key,
										 FDrop::BaselineLootQuantity),
			Row.Materials);
		TestEqual(*FString::Printf(TEXT("%s adds %.0f percent magic find"),
								   Row.Key, Row.Find),
			FDrop::MagicFindFrom(Drops, Key), Row.Find);
	}

	// LOOT QUANTITY IS A PERCENTAGE OF WHAT WOULD OTHERWISE DROP, so 100 leaves
	// it unchanged. An increase applied to a baseline of zero would be worth
	// nothing, which is why the baseline is 100.
	TestEqual(TEXT("400 percent loot quantity quadruples a Boss's gear"),
		FDrop::ExpectedGearDrops(Drops, TEXT("Boss"), 400.0f), 20.0f);
	TestEqual(TEXT("and its materials"),
		FDrop::ExpectedMaterialDrops(Drops, TEXT("Boss"), 400.0f), 40.0f);

	// AN UNKNOWN RARITY DROPS NOTHING rather than crashing. "Rare" is the one
	// that would be typed by mistake: the design document's older enemy list
	// had it and this ladder does not.
	TestEqual(TEXT("an unknown enemy rarity drops nothing"),
		FDrop::ExpectedGearDrops(Drops, TEXT("Rare"),
								 FDrop::BaselineLootQuantity), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropCountTest,
	"Cataclysm.Drop.AFractionalRateIsRolledRatherThanRounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropCountTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDropRollTest;

	// ROUNDING WOULD MAKE A COMMON ENEMY DROP NOTHING, EVER. 0.16 to the
	// nearest whole number is zero, so the fraction has to be a probability.
	FRandomStream Stream(/*InSeed=*/20260818);
	constexpr int32 Draws = 20000;

	int32 Total = 0;
	int32 MostAtOnce = 0;
	for (int32 Draw = 0; Draw < Draws; ++Draw)
	{
		const int32 Count = FDrop::RollDropCount(0.16f, Stream);
		Total += Count;
		MostAtOnce = FMath::Max(MostAtOnce, Count);
	}
	const float Mean = static_cast<float>(Total) / Draws;
	TestTrue(*FString::Printf(TEXT("0.16 averages 0.16 over %d draws (%.4f)"),
							  Draws, Mean),
		FMath::Abs(Mean - 0.16f) < 0.16f * 0.1f);
	TestEqual(TEXT("and never gives two at once"), MostAtOnce, 1);

	// A WHOLE NUMBER HAS NO FRACTION LEFT TO ROLL, so it cannot vary. Without
	// this the fractional draw could be adding a spurious extra at every whole
	// number.
	Total = 0;
	for (int32 Draw = 0; Draw < 500; ++Draw)
	{
		Total += FDrop::RollDropCount(5.0f, Stream);
	}
	TestEqual(TEXT("a rate of exactly 5 always gives 5"), Total, 500 * 5);

	TestEqual(TEXT("and a rate of nothing gives nothing"),
		FDrop::RollDropCount(0.0f, Stream), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMaterialTierTest,
	"Cataclysm.Drop.MaterialTiersMatchTheModelAndMagicFindRaisesThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMaterialTierTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDropRollTest;

	UDataTable* Tiers =
		LoadTable<FCataclysmMaterialTierRow>(TEXT("MaterialTiers.csv"));
	if (!TestNotNull(TEXT("MaterialTiers.csv loads"), Tiers))
	{
		return false;
	}

	// Printed by loot.material_tier_distribution(0.0). Common down to
	// Extremely Rare, each tier four times rarer than the one below.
	const float MaterialShareAtNoMagicFind[] = {
		0.7507331378f, 0.1876832845f, 0.0469208211f, 0.0117302053f,
		0.0029325513f,
	};

	TArray<float> Shares;
	FDrop::MaterialTierDistribution(Tiers, 0.0f, Shares);
	if (!TestEqual(TEXT("five tiers"), Shares.Num(), 5))
	{
		return false;
	}
	for (int32 Index = 0; Index < 5; ++Index)
	{
		TestTrue(*FString::Printf(
			TEXT("tier %d is %.7f of material drops (%.7f)"), Index + 1,
			MaterialShareAtNoMagicFind[Index], Shares[Index]),
			FMath::Abs(Shares[Index] - MaterialShareAtNoMagicFind[Index])
				< MaterialShareAtNoMagicFind[Index] * 1e-4f);
	}

	// THE FIGURE THE WEIGHTS WERE CHOSEN AGAINST. Three materials share the top
	// tier, and Purified Essence is the only thing that clears the Consumption
	// Threshold, so how often it turns up is what mattered.
	TestTrue(*FString::Printf(
		TEXT("an Extremely Rare material is one drop in 341 (%.0f)"),
		1.0f / Shares[4]), FMath::Abs(1.0f / Shares[4] - 341.0f) < 1.0f);

	int32 InTopTier = 0;
	Tiers->ForeachRow<FCataclysmMaterialTierRow>(TEXT("top tier count"),
		[&](const FName&, const FCataclysmMaterialTierRow& Row)
		{
			if (Row.Tier == 5)
			{
				InTopTier = Row.Materials;
			}
		});
	TestEqual(TEXT("three materials share the top tier"), InTopTier, 3);
	TestTrue(*FString::Printf(
		TEXT("so a named one is one drop in 1,023 (%.0f)"),
		1.0f / Shares[4] * InTopTier),
		FMath::Abs(1.0f / Shares[4] * InTopTier - 1023.0f) < 4.0f);

	// EVERY DISTRIBUTION SUMS TO ONE. It is a cascade, so the commonest tier
	// takes whatever fell through; a sum below one would be material drops that
	// rolled no tier at all.
	for (float MagicFind : { 0.0f, 100.0f, 300.0f, 500.0f, 5000.0f })
	{
		FDrop::MaterialTierDistribution(Tiers, MagicFind, Shares);
		float Total = 0.0f;
		for (float Share : Shares)
		{
			Total += Share;
		}
		TestTrue(*FString::Printf(
			TEXT("%.0f percent magic find sums to 1 (%.6f)"), MagicFind, Total),
			FMath::Abs(Total - 1.0f) < 1e-4f);
	}

	// MAGIC FIND RAISES THE TIER, which departs from the genre on purpose.
	// Printed by loot.material_tier_distribution(300.0).
	FDrop::MaterialTierDistribution(Tiers, 300.0f, Shares);
	TestTrue(*FString::Printf(
		TEXT("300 percent magic find quadruples the top tier (%.7f)"),
		Shares[4]),
		FMath::Abs(Shares[4] - 0.0117302053f) < 0.0117302053f * 1e-3f);

	// AND AT 500 PERCENT THE SECOND RUNG SATURATES, so nothing falls through to
	// the commonest tier. A Cataclysm Boss adds exactly 500%, so it drops no
	// Common materials at all. That is the intended shape -- the ordinary
	// supply comes from ordinary enemies, which add none -- and it is sharp
	// enough to pin here rather than leave to be discovered.
	FDrop::MaterialTierDistribution(Tiers, 500.0f, Shares);
	TestEqual(TEXT("a 500 percent magic find kill drops no Common materials"),
		Shares[0], 0.0f);

	// AND THE ROLL DRAWS FROM THAT DISTRIBUTION.
	FRandomStream Stream(/*InSeed=*/818);
	FDrop::MaterialTierDistribution(Tiers, 0.0f, Shares);
	TArray<int32> Counts;
	Counts.Init(0, 6);
	constexpr int32 Draws = 40000;
	for (int32 Draw = 0; Draw < Draws; ++Draw)
	{
		const int32 Tier = FDrop::RollMaterialTier(Tiers, 0.0f, Stream);
		if (Tier < 1 || Tier > 5)
		{
			AddError(FString::Printf(TEXT("a material rolled tier %d"), Tier));
			return false;
		}
		++Counts[Tier];
	}
	for (int32 Tier = 1; Tier <= 3; ++Tier)
	{
		const float Seen = static_cast<float>(Counts[Tier]) / Draws;
		TestTrue(*FString::Printf(
			TEXT("tier %d came up %.4f against a stated %.4f"), Tier, Seen,
			Shares[Tier - 1]),
			FMath::Abs(Seen - Shares[Tier - 1]) < Shares[Tier - 1] * 0.1f);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Rolling a whole item
// ---------------------------------------------------------------------------

namespace CataclysmDropRollTest
{
	/** Every table the whole-item roll needs, loaded from the CSVs. */
	struct FTables
	{
		UDataTable* Bases = nullptr;
		UDataTable* Affixes = nullptr;
		UDataTable* Rarities = nullptr;
		UDataTable* Sockets = nullptr;
		UDataTable* AffixTiers = nullptr;

		bool AllPresent() const
		{
			return Bases && Affixes && Rarities && Sockets && AffixTiers;
		}
	};

	FTables LoadEverything()
	{
		FTables Out;
		Out.Bases = LoadTable<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
		Out.Affixes = LoadTable<FCataclysmAffixRow>(TEXT("Affixes.csv"));
		Out.Rarities = LoadTable<FCataclysmGearRarityRow>(TEXT("GearRarity.csv"));
		Out.Sockets = LoadTable<FCataclysmItemSocketRow>(TEXT("ItemSockets.csv"));
		Out.AffixTiers = LoadTable<FCataclysmAffixTierRow>(TEXT("AffixTiers.csv"));
		return Out;
	}

	/** Roll one item, or leave OutItem untouched and return false. */
	bool Roll(const FTables& Tables, const FString& Slot, int32 Tier,
			  float MagicFind, FRandomStream& Stream, FCataclysmItem& OutItem)
	{
		return FDrop::RollItem(Tables.Bases, Tables.Affixes, Tables.Rarities,
							   Tables.Sockets, Tables.AffixTiers, Slot, Tier,
							   MagicFind, Stream, OutItem);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWholeItemRollTest,
	"Cataclysm.Drop.AWholeItemRollsToSomethingValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWholeItemRollTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDropRollTest;

	const FTables Tables = LoadEverything();
	if (!TestTrue(TEXT("every table loads"), Tables.AllPresent()))
	{
		return false;
	}

	FRandomStream Stream(/*InSeed=*/20260818);
	constexpr int32 Rolls = 3000;
	int32 Rolled = 0;

	for (int32 Attempt = 0; Attempt < Rolls; ++Attempt)
	{
		const FString Slot = FDrop::RollSlot(Tables.Bases, Stream);
		FCataclysmItem Item;
		if (!TestTrue(*FString::Printf(TEXT("a %s rolls"), *Slot),
					  Roll(Tables, Slot, 8, 0.0f, Stream, Item)))
		{
			return false;
		}
		++Rolled;

		// THE CONTENTS HAVE TO BE A RARITY. RarityOf accepts eight combinations
		// of enchantment and affix count and rejects every other, so an item
		// that is not any rarity is malformed rather than merely thin -- and
		// nothing downstream could name it or price it.
		ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
		if (!TestTrue(TEXT("the contents are a rarity"),
			UCataclysmItemValues::RarityOf(Item.EnchantmentCount,
										   Item.Affixes.Num(), Rarity)))
		{
			AddError(FString::Printf(
				TEXT("a %s rolled %d enchantments and %d affixes, which is no "
					 "rarity"), *Slot, Item.EnchantmentCount,
				Item.Affixes.Num()));
			return false;
		}

		// AND THE PIECES AGREE WITH THAT RARITY.
		if (Item.GearLevel != FDrop::GearLevelGateFor(Tables.Rarities, Rarity))
		{
			AddError(FString::Printf(
				TEXT("a %s arrived at +%d and its rarity needs +%d"), *Slot,
				Item.GearLevel,
				FDrop::GearLevelGateFor(Tables.Rarities, Rarity)));
			return false;
		}

		float Lowest = 0.0f;
		float Highest = 0.0f;
		FDrop::ResidueBandFor(Tables.Rarities, Rarity, Lowest, Highest);
		if (Item.Residue < Lowest || Item.Residue > Highest)
		{
			AddError(FString::Printf(
				TEXT("a %s carries %.0f residue, outside its rarity's %.0f to "
					 "%.0f"), *Slot, Item.Residue, Lowest, Highest));
			return false;
		}

		const FCataclysmItemBaseRow* BaseRow =
			Tables.Bases->FindRow<FCataclysmItemBaseRow>(
				Item.Base, TEXT("test"), /*bWarnIfMissing=*/false);
		if (!BaseRow || !BaseRow->Slot.Equals(Slot))
		{
			AddError(FString::Printf(
				TEXT("a %s roll produced the base %s"), *Slot,
				*Item.Base.ToString()));
			return false;
		}
		const int32 Maximum = FDrop::MaxSocketsFor(Tables.Sockets, *BaseRow);
		if (Item.Sockets < 0 || Item.Sockets > Maximum)
		{
			AddError(FString::Printf(
				TEXT("a %s has %d sockets and its base allows %d"), *Slot,
				Item.Sockets, Maximum));
			return false;
		}

		// NO TWO AFFIXES SHARE A STAT GROUP. This is the rule the whole draw
		// exists to enforce, and it is the one that would fail silently: an
		// item with two ways to grant maximum health looks perfectly ordinary.
		TSet<FString> Taken;
		TSet<FString> Groups;
		for (const FCataclysmRolledAffix& Affix : Item.Affixes)
		{
			const FCataclysmAffixRow* Row =
				Tables.Affixes->FindRow<FCataclysmAffixRow>(
					Affix.Affix, TEXT("test"), /*bWarnIfMissing=*/false);
			if (!Row)
			{
				AddError(FString::Printf(TEXT("rolled an unknown affix %s"),
										 *Affix.Affix.ToString()));
				return false;
			}

			// AND EVERY AFFIX IS ALLOWED ON THIS SLOT.
			if (!Row->AllowedSlots.Contains(Slot))
			{
				AddError(FString::Printf(
					TEXT("a %s rolled %s, which allows only %s"), *Slot,
					*Row->AffixName, *Row->AllowedSlots));
				return false;
			}

			if (Affix.Tier < 1 || Affix.Tier > UCataclysmItemValues::MaxAffixTier)
			{
				AddError(FString::Printf(TEXT("%s rolled at tier %d"),
										 *Row->AffixName, Affix.Tier));
				return false;
			}

			FDrop::GroupsOf(Tables.Affixes, *Row, Affix.DamageTypes, Groups);
			if (Groups.Intersect(Taken).Num() > 0)
			{
				AddError(FString::Printf(
					TEXT("a %s carries two affixes in one stat group; %s "
						 "repeats one"), *Slot, *Row->AffixName));
				return false;
			}
			Taken.Append(Groups);
		}
	}

	TestEqual(TEXT("every attempt produced an item"), Rolled, Rolls);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAffixGroupsTest,
	"Cataclysm.Drop.AnAffixOccupiesTheStatGroupsItGrants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAffixGroupsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDropRollTest;

	UDataTable* Affixes = LoadTable<FCataclysmAffixRow>(TEXT("Affixes.csv"));
	if (!TestNotNull(TEXT("Affixes.csv loads"), Affixes))
	{
		return false;
	}

	const auto GroupsFor = [&](const TCHAR* Key, const TArray<FName>& Types)
	{
		TSet<FString> Groups;
		const FCataclysmAffixRow* Row = Affixes->FindRow<FCataclysmAffixRow>(
			FName(Key), TEXT("test"), /*bWarnIfMissing=*/false);
		if (Row)
		{
			FDrop::GroupsOf(Affixes, *Row, Types, Groups);
		}
		return Groups;
	};

	// A STAT AFFIX IS ONE GROUP, its stat and its kind together. Flat and
	// increased maximum health are different groups on purpose: one adds and
	// one multiplies, so a piece may carry both.
	TSet<FString> Flat = GroupsFor(TEXT("Stat_Flat_maximum_health"), {});
	TestEqual(TEXT("a stat affix is one group"), Flat.Num(), 1);
	TestTrue(TEXT("and it names the stat and the kind"),
		Flat.Contains(TEXT("max_health.flat")));

	TSet<FString> Increased =
		GroupsFor(TEXT("Stat_Increased_maximum_health"), {});
	TestTrue(TEXT("flat and increased are different groups"),
		Increased.Intersect(Flat).Num() == 0);

	// AN AILMENT AFFIX GRANTS NO STAT, so its group is the effect it applies.
	TSet<FString> Bleed = GroupsFor(TEXT("Ailment_Chance_to_bleed"), {});
	TestEqual(TEXT("an ailment affix is one group"), Bleed.Num(), 1);
	TestTrue(TEXT("named for the effect rather than a stat"),
		Bleed.Contains(TEXT("ailment.Bleed")));

	// A HYBRID OCCUPIES BOTH ITS PARTS' GROUPS, which is what stops it landing
	// beside either half on its own.
	TSet<FString> Hybrid =
		GroupsFor(TEXT("Hybrid_Magic_find_and_loot_quantity"), {});
	TestEqual(TEXT("a hybrid is two groups"), Hybrid.Num(), 2);
	TestTrue(TEXT("and one of them is its first part's"),
		Hybrid.Intersect(GroupsFor(TEXT("Stat_Flat_magic_find"), {})).Num() == 1);

	// A RESISTANCE FAMILY IS ONE GROUP PER DAMAGE TYPE IT LANDED ON.
	TSet<FString> One = GroupsFor(TEXT("Resistance_Single_resistance"),
								  { TEXT("War") });
	TestEqual(TEXT("a single resistance is one group"), One.Num(), 1);
	TestTrue(TEXT("named for the damage type"),
		One.Contains(TEXT("resistance_war.flat")));

	TSet<FString> Two = GroupsFor(TEXT("Resistance_Two_resistances"),
								  { TEXT("War"), TEXT("Void") });
	TestEqual(TEXT("two resistances are two groups"), Two.Num(), 2);

	// AND AN EMPTY LIST MEANS ALL EIGHT, which is the convention the rolled
	// affix already uses for a family that has no choice to make.
	TSet<FString> All = GroupsFor(TEXT("Resistance_All_resistances"), {});
	TestEqual(TEXT("all resistances is eight groups"), All.Num(),
		UCataclysmItemModifiers::DamageTypeNames().Num());

	// SO TWO FAMILIES THAT BOTH LANDED ON WAR CLASH, and two that did not do
	// not. That is the whole reason the damage types are drawn before the affix
	// draw rather than after it.
	TestTrue(TEXT("two families on the same damage type clash"),
		One.Intersect(GroupsFor(TEXT("Resistance_Two_resistances"),
							  { TEXT("War"), TEXT("Chaos") })).Num() > 0);
	TestTrue(TEXT("and two on different ones do not"),
		One.Intersect(GroupsFor(TEXT("Resistance_Two_resistances"),
							  { TEXT("Void"), TEXT("Chaos") })).Num() == 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPrefixSuffixSplitTest,
	"Cataclysm.Drop.AnOddAffixCountGoesBothWays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPrefixSuffixSplitTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDropRollTest;

	// WITHOUT THIS EVERY THREE-AFFIX ITEM IN THE GAME WOULD CARRY TWO PREFIXES
	// AND ONE SUFFIX, a bias nobody chose, and the affix pool is not built
	// around it: there are 31 prefixes and 54 suffixes.
	FRandomStream Stream(/*InSeed=*/3);
	int32 TwoPrefixes = 0;
	int32 TwoSuffixes = 0;
	constexpr int32 Draws = 4000;

	for (int32 Draw = 0; Draw < Draws; ++Draw)
	{
		int32 Prefixes = 0;
		int32 Suffixes = 0;
		FDrop::SplitForADrop(3, Stream, Prefixes, Suffixes);
		TestEqual(TEXT("three affixes stay three"), Prefixes + Suffixes, 3);
		if (Prefixes == 2) { ++TwoPrefixes; }
		if (Suffixes == 2) { ++TwoSuffixes; }
	}

	TestTrue(*FString::Printf(
		TEXT("two prefixes came up %d times in %d"), TwoPrefixes, Draws),
		FMath::Abs(TwoPrefixes - Draws / 2) < Draws / 10);
	TestTrue(*FString::Printf(
		TEXT("and two suffixes %d times in %d"), TwoSuffixes, Draws),
		FMath::Abs(TwoSuffixes - Draws / 2) < Draws / 10);

	// AN EVEN COUNT HAS NO SIDE TO PICK, so it cannot vary.
	for (int32 Draw = 0; Draw < 200; ++Draw)
	{
		int32 Prefixes = 0;
		int32 Suffixes = 0;
		FDrop::SplitForADrop(4, Stream, Prefixes, Suffixes);
		if (Prefixes != 2 || Suffixes != 2)
		{
			AddError(FString::Printf(TEXT("four affixes split %d and %d"),
									 Prefixes, Suffixes));
			return false;
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRolledItemHasAReadableNameTest,
	"Cataclysm.Item.EveryRolledItemHasAReadableName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRolledItemHasAReadableNameTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDropRollTest;

	const FTables Tables = LoadEverything();
	if (!TestTrue(TEXT("every table loads"), Tables.AllPresent()))
	{
		return false;
	}

	// THIS IS WHAT A PLAYER SEES ON THE GROUND. A drop is shown as its own name
	// rather than as a model, so an item that rolls to an empty name is a drop
	// nobody can identify or click.
	FRandomStream Stream(/*InSeed=*/707);
	for (int32 Attempt = 0; Attempt < 500; ++Attempt)
	{
		const FString Slot = FDrop::RollSlot(Tables.Bases, Stream);
		FCataclysmItem Item;
		if (!Roll(Tables, Slot, 8, 0.0f, Stream, Item))
		{
			AddError(TEXT("a roll produced no item"));
			return false;
		}

		const FString Name =
			UCataclysmItemName::NameOf(Item, Tables.Bases, Tables.Affixes);
		if (Name.IsEmpty())
		{
			AddError(FString::Printf(
				TEXT("a %s with %d affixes and %d enchantments has no name"),
				*Slot, Item.Affixes.Num(), Item.EnchantmentCount));
			return false;
		}

		// AND IT STARTS WITH THE RARITY, which is the first word of the format.
		ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
		UCataclysmItemValues::RarityOf(Item.EnchantmentCount,
									   Item.Affixes.Num(), Rarity);
		if (!Name.StartsWith(UCataclysmItemName::RarityWord(Rarity)))
		{
			AddError(FString::Printf(TEXT("'%s' does not begin with its rarity"),
									 *Name));
			return false;
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// The name
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemNameTest,
	"Cataclysm.Item.ADropIsNamedAfterItsStrongestSuffix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemNameTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDropRollTest;

	UDataTable* Bases = LoadTable<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
	UDataTable* Affixes = LoadTable<FCataclysmAffixRow>(TEXT("Affixes.csv"));
	if (!TestNotNull(TEXT("ItemBases.csv loads"), Bases)
		|| !TestNotNull(TEXT("Affixes.csv loads"), Affixes))
	{
		return false;
	}

	TestEqual(TEXT("a suffix gives its word"),
		UCataclysmItemName::WordFor(Affixes, TEXT("Stat_Flat_life_leech")),
		FString(TEXT("the Leech")));
	TestEqual(TEXT("and a prefix gives none"),
		UCataclysmItemName::WordFor(Affixes, TEXT("Stat_Flat_maximum_health")),
		FString());

	// TWO SUFFIXES, AND THE HIGHER TIER NAMES THE ITEM. Two affixes and no
	// enchantment is a Quality piece.
	FCataclysmItem Item;
	Item.Base = TEXT("Weapon_Greataxe");
	Item.Affixes = {
		Rolled(TEXT("Resistance_Single_resistance"), 3, 0.9f),
		Rolled(TEXT("Stat_Flat_life_leech"), 6, 0.1f),
	};
	TestEqual(TEXT("the higher tier names it, not the better roll"),
		UCataclysmItemName::NameOf(Item, Bases, Affixes),
		FString(TEXT("Quality Greataxe of the Leech")));

	// TIER BEFORE ROLL, checked the other way round so the assertion above is
	// not passing by accident of the order they were listed in.
	Item.Affixes = {
		Rolled(TEXT("Stat_Flat_life_leech"), 6, 0.1f),
		Rolled(TEXT("Resistance_Single_resistance"), 3, 0.9f),
	};
	TestEqual(TEXT("and the order they were drawn in does not decide it"),
		UCataclysmItemName::NameOf(Item, Bases, Affixes),
		FString(TEXT("Quality Greataxe of the Leech")));

	// A TIE ON TIER IS BROKEN BY THE ROLL.
	Item.Affixes = {
		Rolled(TEXT("Resistance_Single_resistance"), 5, 0.2f),
		Rolled(TEXT("Stat_Flat_life_leech"), 5, 0.8f),
	};
	TestEqual(TEXT("a tie on tier goes to the better roll"),
		UCataclysmItemName::NameOf(Item, Bases, Affixes),
		FString(TEXT("Quality Greataxe of the Leech")));

	// AND A TIE ON BOTH GOES TO THE FIRST, so the same item is called the same
	// thing on every run. Without this the name would depend on nothing.
	Item.Affixes = {
		Rolled(TEXT("Resistance_Single_resistance"), 5, 0.5f),
		Rolled(TEXT("Stat_Flat_life_leech"), 5, 0.5f),
	};
	TestEqual(TEXT("and a tie on both goes to the first on the item"),
		UCataclysmItemName::NameOf(Item, Bases, Affixes),
		FString(TEXT("Quality Greataxe of Warding")));

	// A PREFIX ONLY, SO THE NAME STOPS AFTER THE BASE. One affix and no
	// enchantment is an Everyday piece, and the missing words are themselves a
	// signal that the item is thin.
	FCataclysmItem Thin;
	Thin.Base = TEXT("Chest_Cuirass");
	Thin.Affixes = { Rolled(TEXT("Stat_Flat_maximum_health"), 2, 0.4f) };
	TestEqual(TEXT("a piece with only a prefix stops after the base"),
		UCataclysmItemName::NameOf(Thin, Bases, Affixes),
		FString(TEXT("Everyday Cuirass")));

	// A CATACLYSMIC PIECE CARRIES FOUR ENCHANTMENTS AND NO REGULAR AFFIX, so it
	// has no suffix to be named after either. The rarity still leads.
	FCataclysmItem Top;
	Top.Base = TEXT("Chest_Cuirass");
	Top.EnchantmentCount = 4;
	TestEqual(TEXT("and a Cataclysmic piece has no suffix to take a word from"),
		UCataclysmItemName::NameOf(Top, Bases, Affixes),
		FString(TEXT("Cataclysmic Cuirass")));

	// AN ITEM WHOSE CONTENTS ARE NOT ANY RARITY HAS NO NAME. Five affixes is
	// not a thin item, it is a malformed one, and calling it Everyday would
	// hide that.
	FCataclysmItem Broken;
	Broken.Base = TEXT("Chest_Cuirass");
	Broken.Affixes = {
		Rolled(TEXT("Stat_Flat_maximum_health"), 1, 0.0f),
		Rolled(TEXT("Stat_Flat_life_leech"), 1, 0.0f),
		Rolled(TEXT("Resistance_Single_resistance"), 1, 0.0f),
		Rolled(TEXT("Stat_Flat_maximum_health"), 1, 0.0f),
		Rolled(TEXT("Stat_Flat_life_leech"), 1, 0.0f),
	};
	TestEqual(TEXT("an item that is not any rarity is not named"),
		UCataclysmItemName::NameOf(Broken, Bases, Affixes), FString());

	// AND EVERY SUFFIX IN THE TABLE HAS A WORD, so no drop can roll one and
	// have nothing to be called after. The generator refuses to write the CSV
	// otherwise; this is the same claim read back out of the imported table.
	Affixes->ForeachRow<FCataclysmAffixRow>(TEXT("every suffix has a word"),
		[&](const FName& Key, const FCataclysmAffixRow& Row)
		{
			const bool bSuffix =
				Row.Position.Equals(TEXT("suffix"), ESearchCase::IgnoreCase);
			if (bSuffix && Row.NameWord.IsEmpty())
			{
				AddError(FString::Printf(
					TEXT("the suffix %s has no name word"), *Key.ToString()));
			}
			if (!bSuffix && !Row.NameWord.IsEmpty())
			{
				AddError(FString::Printf(
					TEXT("the prefix %s carries the name word '%s', which the "
						 "first word of a name being the rarity makes "
						 "unreachable"), *Key.ToString(), *Row.NameWord));
			}
		});

	return true;
}

#endif // WITH_AUTOMATION_TESTS
