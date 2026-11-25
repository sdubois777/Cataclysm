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
 */
bool FCataclysmRarityCapLiveDropTest::RunTest(const FString&)
{
	using namespace CataclysmRarityCapTest;

	const auto BestOnTheFloor = [this](int32 Tier, int32& OutMasterful) -> int32
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
		}

		int32 Best = -1;
		OutMasterful = 0;
		for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
		{
			const ACataclysmDroppedItem* Drop = *It;
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
	const int32 AtOne = BestOnTheFloor(1, MasterfulAtOne);
	const int32 AtEight = BestOnTheFloor(8, MasterfulAtEight);
	AddInfo(FString::Printf(TEXT("RARITY CAP LIVE: best at tier 1 %d (%d Masterful), best at tier 8 %d"), AtOne,
							MasterfulAtOne, AtEight));
	TestTrue(TEXT("set-up: the tier 1 kills dropped something"), AtOne >= 0);
	TestTrue(*FString::Printf(TEXT("nothing at tier 1 is above Masterful (best %d)"), AtOne),
			 AtOne <= static_cast<int32>(ECataclysmRarity::Masterful));
	TestTrue(*FString::Printf(TEXT("control: Masterful drops at tier 1 (%d)"), MasterfulAtOne), MasterfulAtOne > 0);
	TestTrue(*FString::Printf(TEXT("control: the same kills at tier 8 drop above Masterful (best %d)"), AtEight),
			 AtEight > static_cast<int32>(ECataclysmRarity::Masterful));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
