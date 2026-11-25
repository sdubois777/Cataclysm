// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "CataclysmTestWorld.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmItem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

/**
 * The cap on how rare a drop can roll at a difficulty tier. Issue #2853.
 *
 * THE OWNER'S RULING OF 2026-11-24, "Hard cap by tier (Recommended)": "a tier can drop at most two rarities above its
 * normal reach, so the highest droppable rarity is number tier + 3 (tier 1 Masterful, tier 2 Legendary, tier 3
 * Mythical ...). Everyday, Quality, Superb and Masterful keep their current chances; the halving per rung stays below
 * the cap." Before it, Cataclysmic pieces dropped at tier 1, at +10.
 *
 * THE FIGURES ARE QUOTED FROM `sim/cataclysm_sim/loot.py`, printed by `loot.rarity_distribution`, so a test compares
 * the port with the model rather than with itself. Every draw is seeded.
 */

namespace CataclysmRarityCapTest
{
	using FDrop = UCataclysmDropRoll;

	/** A table read from game/Data, so the tests read the shipped data rather than a fixture. */
	template <typename RowType>
	UDataTable* RarityCapTable(const TCHAR* FileName)
	{
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *(FPaths::ProjectDir() / TEXT("Data") / FileName)))
		{
			return nullptr;
		}
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = RowType::StaticStruct();
		return Table->CreateTableFromCSVString(Contents).Num() > 0 ? nullptr : Table;
	}

	FString RarityCapName(ECataclysmRarity Rarity)
	{
		return FDrop::RowNameFor(Rarity).ToString();
	}

	ECataclysmRarity RarityCapRung(int32 Index)
	{
		return static_cast<ECataclysmRarity>(Index);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRarityCapNumberTest,
	"Cataclysm.RarityCap.TheRarestATierCanDropIsRarityNumberTierPlusThree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * RC1. The cap at every tier, written out as the owner's ruling lists it, so a change to the constant cannot pass by
 * moving its own expectation. Fails if `RaritiesAboveTheReachThatDrop` or `HighestDroppableRarity` moves.
 */
bool FCataclysmRarityCapNumberTest::RunTest(const FString&)
{
	using namespace CataclysmRarityCapTest;

	const ECataclysmRarity Expected[] = {
		ECataclysmRarity::Masterful, ECataclysmRarity::Legendary, ECataclysmRarity::Mythical,
		ECataclysmRarity::Ascendant, ECataclysmRarity::Cataclysmic, ECataclysmRarity::Cataclysmic,
		ECataclysmRarity::Cataclysmic, ECataclysmRarity::Cataclysmic,
	};
	for (int32 Tier = 1; Tier <= FDrop::DifficultyTiers; ++Tier)
	{
		TestEqual(*FString::Printf(TEXT("tier %d drops at most %s"), Tier, *RarityCapName(Expected[Tier - 1])),
				  RarityCapName(FDrop::HighestDroppableRarity(Tier)), RarityCapName(Expected[Tier - 1]));
	}
	TestEqual(TEXT("two rungs above the tier's reach"), FDrop::RaritiesAboveTheReachThatDrop, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRarityCapNothingAboveTest,
	"Cataclysm.RarityCap.NothingAboveTheCapDropsAtAnyMagicFind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * RC2. At tiers 1, 2 and 8, with no magic find, a Boss's 300% and a magic find no character reaches: every rarity
 * above the cap has a share of exactly 0 and the cascade never rolls one in 20,000 draws, and every rarity at or below
 * it has a share above 0. Fails if the cap is not applied, is applied as a penalty that magic find can overcome, or
 * removes a rarity the tier can drop.
 */
bool FCataclysmRarityCapNothingAboveTest::RunTest(const FString&)
{
	using namespace CataclysmRarityCapTest;

	UDataTable* Rarities = RarityCapTable<FCataclysmGearRarityRow>(TEXT("GearRarity.csv"));
	if (!TestNotNull(TEXT("GearRarity.csv loads"), Rarities))
	{
		return false;
	}

	for (const int32 Tier : {1, 2, 8})
	{
		const int32 Cap = static_cast<int32>(FDrop::HighestDroppableRarity(Tier));
		for (const float MagicFind : {0.0f, 300.0f, 5000000.0f})
		{
			TArray<float> Shares;
			FDrop::RarityDistribution(Rarities, Tier, MagicFind, Shares);
			for (int32 Index = 0; Index < FDrop::RarityCount; ++Index)
			{
				const FString What = FString::Printf(TEXT("%s at tier %d with %.0f%% magic find (%.9f)"),
													 *RarityCapName(RarityCapRung(Index)), Tier, MagicFind,
													 Shares[Index]);
				if (Index > Cap)
				{
					TestEqual(*(TEXT("above the cap: ") + What), Shares[Index], 0.0f);
				}
				else
				{
					TestTrue(*(TEXT("at or below the cap: ") + What), Shares[Index] > 0.0f);
				}
			}

			FRandomStream Stream(2853 + Tier);
			int32 Above = 0;
			int32 AtTheCap = 0;
			for (int32 Draw = 0; Draw < 20000; ++Draw)
			{
				const int32 Rolled = static_cast<int32>(FDrop::RollRarity(Rarities, Tier, MagicFind, Stream));
				Above += Rolled > Cap ? 1 : 0;
				AtTheCap += Rolled == Cap ? 1 : 0;
			}
			TestEqual(*FString::Printf(TEXT("the cascade rolled nothing above the cap at tier %d, %.0f%%"), Tier,
									   MagicFind),
					  Above, 0);
			// THE CONTROL: at a magic find no character reaches, the cap itself is rolled, so a zero above it is the
			// cap working and not a draw too short to reach the top.
			if (MagicFind > 1000.0f)
			{
				TestTrue(*FString::Printf(TEXT("control: the cap itself is rolled at tier %d (%d times)"), Tier,
										  AtTheCap),
						 AtTheCap > 0);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRarityCapSharesTest,
	"Cataclysm.RarityCap.TheSharesMatchTheModelAtTiersOneTwoAndEight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * RC3. The exact shares at tier 1 (with no magic find and with a Boss's 300%), tier 2 and tier 8, printed by
 * `loot.rarity_distribution`. The ordinary four keep their weights: at tier 1 Masterful is 1.06952% against 1.06873%
 * before the cap, the 0.00079 points the removed rarities held being shared out. Tier 8 is unchanged by the cap.
 */
bool FCataclysmRarityCapSharesTest::RunTest(const FString&)
{
	using namespace CataclysmRarityCapTest;

	UDataTable* Rarities = RarityCapTable<FCataclysmGearRarityRow>(TEXT("GearRarity.csv"));
	if (!TestNotNull(TEXT("GearRarity.csv loads"), Rarities))
	{
		return false;
	}

	struct FCase
	{
		int32 Tier;
		float MagicFind;
		float Shares[8];
	};
	const FCase Cases[] = {
		{1, 0.0f, {0.6684491979f, 0.2673796791f, 0.0534759358f, 0.0106951872f, 0.0f, 0.0f, 0.0f, 0.0f}},
		{1, 300.0f, {0.4885727534f, 0.3457740627f, 0.1223560888f, 0.0432970951f, 0.0f, 0.0f, 0.0f, 0.0f}},
		{2, 0.0f, {0.5725290931f, 0.2694948137f, 0.1268537363f, 0.0298556217f, 0.0012667351f, 0.0f, 0.0f, 0.0f}},
		{8, 0.0f, {0.1723819674f, 0.2154774593f, 0.2693468241f, 0.3366835302f, 0.0048960088f, 0.0009792018f,
				   0.0001958404f, 0.0000391681f}},
	};
	for (const FCase& Case : Cases)
	{
		TArray<float> Shares;
		FDrop::RarityDistribution(Rarities, Case.Tier, Case.MagicFind, Shares);
		for (int32 Index = 0; Index < FDrop::RarityCount; ++Index)
		{
			const float Want = Case.Shares[Index];
			TestTrue(*FString::Printf(TEXT("%s at tier %d, %.0f%% magic find, is %.10f (%.10f)"),
									  *RarityCapName(RarityCapRung(Index)), Case.Tier, Case.MagicFind, Want,
									  Shares[Index]),
					 Want == 0.0f ? Shares[Index] == 0.0f : FMath::Abs(Shares[Index] - Want) < Want * 1e-4f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRarityCapHighRollerTest,
	"Cataclysm.RarityCap.TheHighRollersItemIsExactlyLegendaryWhereTheCapIsBelowIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * RC4. The High Roller's extra item, rolled from Legendary up (`RollRarityAtLeast`). At tiers 1 and 2 it is exactly
 * Legendary: the node's own minimum, one rung above tier 1's cap of Masterful, and nothing rarer (the owner's ruling
 * of 2026-11-24 on issue #2853, "Exception: exactly Legendary (Recommended)"). At tier 3 it is Legendary or Mythical,
 * both seen, nothing above the cap. Fails if the cap does not reach the High Roller's roll, or if it removes the node's
 * minimum.
 */
bool FCataclysmRarityCapHighRollerTest::RunTest(const FString&)
{
	using namespace CataclysmRarityCapTest;

	UDataTable* Rarities = RarityCapTable<FCataclysmGearRarityRow>(TEXT("GearRarity.csv"));
	if (!TestNotNull(TEXT("GearRarity.csv loads"), Rarities))
	{
		return false;
	}

	for (const int32 Tier : {1, 2, 3})
	{
		FRandomStream Stream(28530 + Tier);
		TMap<int32, int32> Seen;
		for (int32 Draw = 0; Draw < 5000; ++Draw)
		{
			++Seen.FindOrAdd(static_cast<int32>(
				FDrop::RollRarityAtLeast(Rarities, Tier, 300.0f, ECataclysmRarity::Legendary, Stream)));
		}
		const int32 Legendary = static_cast<int32>(ECataclysmRarity::Legendary);
		const int32 Mythical = static_cast<int32>(ECataclysmRarity::Mythical);
		int32 Other = 0;
		for (const TPair<int32, int32>& Each : Seen)
		{
			Other += Each.Key != Legendary && Each.Key != Mythical ? Each.Value : 0;
		}
		TestEqual(*FString::Printf(TEXT("tier %d: nothing below Legendary or above Mythical"), Tier), Other, 0);
		if (Tier < 3)
		{
			TestEqual(*FString::Printf(TEXT("tier %d: every one is Legendary"), Tier), Seen.FindRef(Legendary), 5000);
		}
		else
		{
			TestTrue(*FString::Printf(TEXT("tier 3: Legendary (%d) and Mythical (%d) both drop"),
									  Seen.FindRef(Legendary), Seen.FindRef(Mythical)),
					 Seen.FindRef(Legendary) > 0 && Seen.FindRef(Mythical) > 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRarityCapItemCountTest,
	"Cataclysm.RarityCap.ATierOneItemRollCountsTheModelsShares",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * RC5, THE SEEDED COUNT the coordinating session asked for with issue #2853: whole items rolled through `RollItem`, as
 * a kill rolls them, at tier 1, 50,000 with no magic find and 50,000 at a Boss's 300%. Each item's rarity is read back
 * from what it carries (`RarityOfItem`, the rarity its name shows), not from the roll's answer, and counted against
 * `RarityDistribution`. Every rung with an expected count of 20 or more must land within five standard deviations, and
 * nothing may be above the cap. Fails if anything between the rarity roll and the finished item changes its rarity:
 * the fault the owner's two tier 1 Cataclysmic pieces could have pointed at. The counts are printed.
 */
bool FCataclysmRarityCapItemCountTest::RunTest(const FString&)
{
	using namespace CataclysmRarityCapTest;

	UDataTable* Bases = RarityCapTable<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
	UDataTable* Affixes = RarityCapTable<FCataclysmAffixRow>(TEXT("Affixes.csv"));
	UDataTable* Rarities = RarityCapTable<FCataclysmGearRarityRow>(TEXT("GearRarity.csv"));
	UDataTable* Sockets = RarityCapTable<FCataclysmItemSocketRow>(TEXT("ItemSockets.csv"));
	UDataTable* AffixTiers = RarityCapTable<FCataclysmAffixTierRow>(TEXT("AffixTiers.csv"));
	UDataTable* WeaponSkills = RarityCapTable<FCataclysmWeaponSkillRow>(TEXT("WeaponSkills.csv"));
	UDataTable* Positive = RarityCapTable<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	UDataTable* Negative = RarityCapTable<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
	if (!TestTrue(TEXT("every table loads"),
				  Bases && Affixes && Rarities && Sockets && AffixTiers && WeaponSkills && Positive && Negative))
	{
		return false;
	}

	constexpr int32 Rolls = 50000;
	const int32 Cap = static_cast<int32>(FDrop::HighestDroppableRarity(1));
	for (const float MagicFind : {0.0f, 300.0f})
	{
		TArray<int32> Counts;
		Counts.Init(0, FDrop::RarityCount);
		int32 Failed = 0;
		int32 Unreadable = 0;
		FRandomStream Stream(MagicFind > 0.0f ? 2853300 : 2853000);
		for (int32 Roll = 0; Roll < Rolls; ++Roll)
		{
			const FString Slot = FDrop::RollSlot(Bases, Stream);
			FCataclysmItem Item;
			if (!FDrop::RollItem(Bases, Affixes, Rarities, Sockets, AffixTiers, WeaponSkills, Positive, Negative, Slot,
								 /*DifficultyTier=*/1, MagicFind, Stream, Item))
			{
				++Failed;
				continue;
			}
			ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
			if (!UCataclysmItemModifiers::RarityOfItem(Item, Rarity))
			{
				++Unreadable;
				continue;
			}
			++Counts[static_cast<int32>(Rarity)];
		}

		TArray<float> Shares;
		FDrop::RarityDistribution(Rarities, 1, MagicFind, Shares);
		const int32 Counted = Rolls - Failed - Unreadable;
		AddInfo(FString::Printf(
			TEXT("RARITY COUNT, tier 1, %.0f%% magic find, %d items: %d %d %d %d %d %d %d %d (expected %.0f %.0f %.0f %.0f)"),
			MagicFind, Counted, Counts[0], Counts[1], Counts[2], Counts[3], Counts[4], Counts[5], Counts[6], Counts[7],
			Shares[0] * Counted, Shares[1] * Counted, Shares[2] * Counted, Shares[3] * Counted));
		TestEqual(TEXT("every item rolled"), Failed, 0);
		TestEqual(TEXT("every item's rarity can be read back"), Unreadable, 0);

		for (int32 Index = 0; Index < FDrop::RarityCount; ++Index)
		{
			if (Index > Cap)
			{
				TestEqual(*FString::Printf(TEXT("no %s at tier 1, %.0f%% magic find"),
										   *RarityCapName(RarityCapRung(Index)), MagicFind),
						  Counts[Index], 0);
				continue;
			}
			const double Expected = static_cast<double>(Shares[Index]) * Counted;
			if (Expected < 20.0)
			{
				continue;
			}
			const double Sigma = FMath::Sqrt(Expected * (1.0 - Shares[Index]));
			TestTrue(*FString::Printf(TEXT("%s at tier 1, %.0f%% magic find: %d against %.0f, within five sigma (%.1f)"),
									  *RarityCapName(RarityCapRung(Index)), MagicFind, Counts[Index], Expected, Sigma),
					 FMath::Abs(Counts[Index] - Expected) <= 5.0 * Sigma);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRarityCapLiveDropTest,
	"Cataclysm.RarityCap.AKillAtTierOneDropsNothingAboveMasterful",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * RC6. The live path: Cataclysm Boss kills in a world at tier 1, through `SpawnDropsFor`, the function every new gear
 * drop and gem comes from, at a magic find no character reaches. No gear and no gem on the floor is above Masterful,
 * and Masterful itself is there (the control). The same kills at tier 8 leave a piece above Masterful, so the check
 * can see one. Fails if the drop site rolls past the cap or passes the wrong tier.
 *
 * AND THE CATACLYSM BOSS'S ONE PIECE, SET ASIDE BY NAME (issue #2853, the owner's ruling of 2026-11-24: "The next
 * tier's best (Recommended)"). Each kill also drops it, as the death site does (`SpawnTheCataclysmBossesPieceFor`); it
 * is above the cap on purpose, so it is counted apart, by `bTheCataclysmBossesPiece`: one a kill, each the next tier's
 * best, Legendary at tier 1.
 */
bool FCataclysmRarityCapLiveDropTest::RunTest(const FString&)
{
	using namespace CataclysmRarityCapTest;

	const auto BestOnTheFloor = [this](int32 Tier, int32& OutMasterful, int32& OutPieces, int32& OutPiecesRight) -> int32
	{
		const CataclysmTestWorld::FScopedDifficultyTier Scoped(Tier);
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!World)
		{
			return -1;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };

		FRandomStream Stream(28536);
		constexpr int32 CataclysmBossStep = 5;
		for (int32 Kill = 0; Kill < 20; ++Kill)
		{
			UCataclysmDropSpawner::SpawnDropsFor(World, CataclysmBossStep, /*MagicFind=*/100000.0f,
												 UCataclysmDropRoll::BaselineLootQuantity * 5.0f,
												 FVector(Kill * 400.0f, 0.0f, 40.0f), Stream);
			UCataclysmDropSpawner::SpawnGemsFor(World, CataclysmBossStep, /*MagicFind=*/100000.0f,
												UCataclysmDropRoll::BaselineLootQuantity * 1000.0f,
												FVector(Kill * 400.0f, 800.0f, 40.0f), Stream);
			UCataclysmDropSpawner::SpawnTheCataclysmBossesPieceFor(World, CataclysmBossStep, /*MagicFind=*/100000.0f,
																   FVector(Kill * 400.0f, 1600.0f, 40.0f), Stream);
		}

		int32 Best = -1;
		OutMasterful = 0;
		OutPieces = 0;
		OutPiecesRight = 0;
		for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
		{
			const ACataclysmDroppedItem* Drop = *It;
			if (Drop && Drop->bTheCataclysmBossesPiece)
			{
				// SET ASIDE BY NAME: above the cap on purpose (#2853). Counted, with whether it is the next tier's best.
				ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
				++OutPieces;
				OutPiecesRight += UCataclysmItemModifiers::RarityOfItem(Drop->Item, Rarity)
					&& Rarity == UCataclysmDropRoll::CataclysmBossPieceRarity(Tier) ? 1 : 0;
				continue;
			}
			int32 Rung = -1;
			if (Drop && Drop->IsGem())
			{
				Rung = static_cast<int32>(Drop->GemRarity);
			}
			else if (Drop && !Drop->Item.Base.IsNone())
			{
				ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
				if (UCataclysmItemModifiers::RarityOfItem(Drop->Item, Rarity))
				{
					Rung = static_cast<int32>(Rarity);
				}
			}
			Best = FMath::Max(Best, Rung);
			OutMasterful += Rung == static_cast<int32>(ECataclysmRarity::Masterful) ? 1 : 0;
		}
		return Best;
	};

	int32 MasterfulAtOne = 0;
	int32 MasterfulAtEight = 0;
	int32 PiecesAtOne = 0;
	int32 PiecesAtEight = 0;
	int32 RightAtOne = 0;
	int32 RightAtEight = 0;
	const int32 AtOne = BestOnTheFloor(1, MasterfulAtOne, PiecesAtOne, RightAtOne);
	const int32 AtEight = BestOnTheFloor(8, MasterfulAtEight, PiecesAtEight, RightAtEight);
	AddInfo(FString::Printf(TEXT("RARITY CAP LIVE: best at tier 1 %d (%d Masterful), best at tier 8 %d; the Cataclysm "
								 "Boss's pieces %d at tier 1 (%d the next tier's best), %d at tier 8 (%d)"),
							AtOne, MasterfulAtOne, AtEight, PiecesAtOne, RightAtOne, PiecesAtEight, RightAtEight));
	TestEqual(TEXT("**one Cataclysm Boss's piece a kill at tier 1**"), PiecesAtOne, 20);
	TestEqual(TEXT("**each the next tier's best, Legendary**"), RightAtOne, 20);
	TestEqual(TEXT("and at tier 8, each Cataclysmic"), RightAtEight, 20);
	TestTrue(TEXT("set-up: the tier 1 kills dropped something"), AtOne >= 0);
	TestTrue(*FString::Printf(TEXT("nothing at tier 1 is above Masterful (best %d)"), AtOne),
			 AtOne <= static_cast<int32>(ECataclysmRarity::Masterful));
	TestTrue(*FString::Printf(TEXT("control: Masterful drops at tier 1 (%d)"), MasterfulAtOne), MasterfulAtOne > 0);
	TestTrue(*FString::Printf(TEXT("control: the same kills at tier 8 drop above Masterful (best %d)"), AtEight),
			 AtEight > static_cast<int32>(ECataclysmRarity::Masterful));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// RC7. The Cataclysm Boss's piece is the next tier's best.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRarityCapBossPieceRarityTest,
	"Cataclysm.RarityCap.TheCataclysmBossesPieceIsTheNextTiersBest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * RC7. Issue #2853, the owner's ruling of 2026-11-24, "The next tier's best (Recommended)": the piece's rarity at each
 * tier, written out, tier 1 Legendary, 2 Mythical, 3 Ascendant, 4 to 8 Cataclysmic; and one rung above the tier's own
 * cap wherever the cap is below Cataclysmic. Fails if `CataclysmBossPieceRarity` gives the tier's own cap.
 */
bool FCataclysmRarityCapBossPieceRarityTest::RunTest(const FString&)
{
	using FDrop = UCataclysmDropRoll;
	const ECataclysmRarity Expected[] = {
		ECataclysmRarity::Legendary, ECataclysmRarity::Mythical, ECataclysmRarity::Ascendant,
		ECataclysmRarity::Cataclysmic, ECataclysmRarity::Cataclysmic, ECataclysmRarity::Cataclysmic,
		ECataclysmRarity::Cataclysmic, ECataclysmRarity::Cataclysmic};
	for (int32 Tier = 1; Tier <= FDrop::DifficultyTiers; ++Tier)
	{
		const ECataclysmRarity Piece = FDrop::CataclysmBossPieceRarity(Tier);
		const ECataclysmRarity Cap = FDrop::HighestDroppableRarity(Tier);
		TestEqual(*FString::Printf(TEXT("**tier %d: %s**"), Tier, *FDrop::RowNameFor(Expected[Tier - 1]).ToString()),
				  static_cast<int32>(Piece), static_cast<int32>(Expected[Tier - 1]));
		if (Cap != ECataclysmRarity::Cataclysmic)
		{
			TestEqual(*FString::Printf(TEXT("tier %d: one rung above the tier's cap"), Tier),
					  static_cast<int32>(Piece), static_cast<int32>(Cap) + 1);
		}
	}
	return true;
}

namespace CataclysmRarityCapTest
{
	/** The Cataclysm Boss's piece spawned once at `Tier` for a kill at `Step`: the drops it put on the floor. */
	TArray<const ACataclysmDroppedItem*> BossPiecesFor(FAutomationTestBase& Test, int32 Tier, int32 Step, int32& OutSpawned,
													   UWorld*& OutWorld)
	{
		TArray<const ACataclysmDroppedItem*> Out;
		OutWorld = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("set-up: a world"), OutWorld))
		{
			return Out;
		}
		FRandomStream Stream(2853 + Tier);
		OutSpawned = UCataclysmDropSpawner::SpawnTheCataclysmBossesPieceFor(OutWorld, Step, /*MagicFind=*/0.0f,
																			 FVector(0.0f, 0.0f, 40.0f), Stream);
		for (TActorIterator<ACataclysmDroppedItem> It(OutWorld); It; ++It)
		{
			Out.Add(*It);
		}
		return Out;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// RC8. A Cataclysm Boss's kill drops one piece of the next tier's best, at its rarity's gate.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRarityCapBossPieceDropTest,
	"Cataclysm.RarityCap.ACataclysmBossDropsOnePieceOfTheNextTiersBestAtItsGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * RC8. At tiers 1, 4 and 8, `SpawnTheCataclysmBossesPieceFor` at the Cataclysm Boss's rung, with no magic find, puts
 * exactly one item on the floor: marked `bTheCataclysmBossesPiece`, of the next tier's best rarity (Legendary,
 * Cataclysmic, Cataclysmic), at its rarity's gear level gate as every drop is (Legendary +4, Cataclysmic +10).
 */
bool FCataclysmRarityCapBossPieceDropTest::RunTest(const FString&)
{
	using namespace CataclysmRarityCapTest;
	const UDataTable* Rarities = UCataclysmDropRoll::LoadGearRarityTable();
	if (!TestNotNull(TEXT("set-up: the gear rarity table"), Rarities))
	{
		return false;
	}
	for (const int32 Tier : {1, 4, 8})
	{
		const CataclysmTestWorld::FScopedDifficultyTier Scoped(Tier);
		int32 Spawned = 0;
		UWorld* World = nullptr;
		const TArray<const ACataclysmDroppedItem*> Floor =
			BossPiecesFor(*this, Tier, UCataclysmDropSpawner::CataclysmBossRarityStep, Spawned, World);
		ON_SCOPE_EXIT { if (World) { World->DestroyWorld(false); } };
		const ECataclysmRarity Wanted = UCataclysmDropRoll::CataclysmBossPieceRarity(Tier);
		ECataclysmRarity Read = ECataclysmRarity::Everyday;
		const bool bRead = Floor.Num() == 1 && UCataclysmItemModifiers::RarityOfItem(Floor[0]->Item, Read);
		AddInfo(FString::Printf(TEXT("BOSS PIECE: tier %d, %d spawned, %d on the floor, %s +%d"), Tier, Spawned,
								Floor.Num(), bRead ? *UCataclysmDropRoll::RowNameFor(Read).ToString() : TEXT("unread"),
								Floor.Num() == 1 ? Floor[0]->Item.GearLevel : -1));
		TestEqual(*FString::Printf(TEXT("tier %d: one piece"), Tier), Spawned, 1);
		if (!TestEqual(*FString::Printf(TEXT("tier %d: one item on the floor"), Tier), Floor.Num(), 1))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("tier %d: marked as the Cataclysm Boss's piece"), Tier),
				 Floor[0]->bTheCataclysmBossesPiece);
		TestTrue(*FString::Printf(TEXT("**tier %d: of the next tier's best rarity**"), Tier), bRead && Read == Wanted);
		TestEqual(*FString::Printf(TEXT("tier %d: at its rarity's gear level gate"), Tier), Floor[0]->Item.GearLevel,
				  UCataclysmDropRoll::GearLevelGateFor(Rarities, Wanted));
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// RC9. Only the Cataclysm Boss's rung drops the piece.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRarityCapBossPieceRungTest,
	"Cataclysm.RarityCap.OnlyTheCataclysmBossesRungDropsThePiece",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * RC9. At tier 1, `SpawnTheCataclysmBossesPieceFor` for every rung below the Cataclysm Boss's, the Boss's included,
 * spawns nothing. CONTROL: at the Cataclysm Boss's rung it spawns one.
 */
bool FCataclysmRarityCapBossPieceRungTest::RunTest(const FString&)
{
	using namespace CataclysmRarityCapTest;
	const CataclysmTestWorld::FScopedDifficultyTier Scoped(1);
	for (int32 Step = 0; Step <= UCataclysmDropSpawner::CataclysmBossRarityStep; ++Step)
	{
		int32 Spawned = 0;
		UWorld* World = nullptr;
		const TArray<const ACataclysmDroppedItem*> Floor = BossPiecesFor(*this, 1, Step, Spawned, World);
		ON_SCOPE_EXIT { if (World) { World->DestroyWorld(false); } };
		const bool bTheBossRung = Step == UCataclysmDropSpawner::CataclysmBossRarityStep;
		TestEqual(*FString::Printf(TEXT("%srung %d: %s"), bTheBossRung ? TEXT("CONTROL: ") : TEXT("**"), Step,
								   bTheBossRung ? TEXT("one piece") : TEXT("nothing**")),
				  Floor.Num(), bTheBossRung ? 1 : 0);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
