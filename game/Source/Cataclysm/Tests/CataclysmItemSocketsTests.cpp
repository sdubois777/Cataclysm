// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Engine/DataTable.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmGems.h"
#include "Items/CataclysmItem.h"

/**
 * TYPED SOCKETS ON ITEMS, AND THE GEMS IN A WORN ITEM'S SOCKETS. Issue #46, slice G3.
 *
 * Ruled by the coordinating session on 2026-11-21: each socket is Attack, Defense or Utility, rolled evenly on a drop;
 * a gem fits a socket of its own type; a socket saved before sockets had types fits any gem; a gem in a worn item adds
 * its modifier once, and a two-handed weapon does not double it.
 *
 * The worn-item test reads the generated tables and a gem's Stat and ValueKind, which `DT_Gems` carries from slice
 * G1's workbook stage.
 */
namespace CataclysmItemSocketsTest
{
	FCataclysmSocket Socket(ECataclysmSocketType Type, FName Gem = NAME_None,
							ECataclysmRarity Rarity = ECataclysmRarity::Everyday)
	{
		FCataclysmSocket Out;
		Out.Type = Type;
		Out.Gem = Gem;
		Out.GemRarity = Rarity;
		return Out;
	}

	/** The modifiers in `Totals[Stat]` that came from a gem. */
	TArray<FCataclysmStatModifier> FromGems(const TMap<FName, TArray<FCataclysmStatModifier>>& Totals, FName Stat)
	{
		TArray<FCataclysmStatModifier> Out;
		if (const TArray<FCataclysmStatModifier>* Found = Totals.Find(Stat))
		{
			for (const FCataclysmStatModifier& Modifier : *Found)
			{
				if (Modifier.Source == ECataclysmModifierSource::Gem)
				{
					Out.Add(Modifier);
				}
			}
		}
		return Out;
	}
}

// --------------------------------------------------------------------------------------------------------------------
// IS1. A new socket's type is rolled evenly among Attack, Defense and Utility, and is never Any.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemSocketsRollTest,
	"Cataclysm.ItemSockets.ASocketsTypeIsRolledEvenlyAndNeverAny",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemSocketsRollTest::RunTest(const FString&)
{
	FRandomStream Stream(2815046);
	int32 Counts[4] = {0, 0, 0, 0};
	constexpr int32 Draws = 3000;
	for (int32 Index = 0; Index < Draws; ++Index)
	{
		++Counts[static_cast<int32>(UCataclysmGems::RollSocketType(Stream))];
	}
	// A THIRD EACH, 1,000, WITHIN 100: over four standard deviations (sqrt(3000 x 1/3 x 2/3) is 25.8).
	TestTrue(*FString::Printf(TEXT("Attack about a third: %d"), Counts[0]), FMath::Abs(Counts[0] - 1000) <= 100);
	TestTrue(*FString::Printf(TEXT("Defense about a third: %d"), Counts[1]), FMath::Abs(Counts[1] - 1000) <= 100);
	TestTrue(*FString::Printf(TEXT("Utility about a third: %d"), Counts[2]), FMath::Abs(Counts[2] - 1000) <= 100);
	TestEqual(TEXT("and never Any"), Counts[3], 0);
	TestEqual(TEXT("a socket saved before types fits any gem: the default is Any"), FCataclysmSocket().Type,
			  ECataclysmSocketType::Any);
	AddInfo(FString::Printf(TEXT("SOCKET TYPES: Attack %d, Defense %d, Utility %d, Any %d of %d"), Counts[0], Counts[1],
							Counts[2], Counts[3], Draws));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// IS2. A gem fits a socket of its own type, and a socket of Any type fits every gem.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemSocketsFitTest,
	"Cataclysm.ItemSockets.AGemFitsASocketOfItsOwnTypeAndAnyFitsEveryGem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemSocketsFitTest::RunTest(const FString&)
{
	TestTrue(TEXT("an Attack gem fits an Attack socket"), UCataclysmGems::GemFits(ECataclysmSocketType::Attack,
																				  TEXT("Attack")));
	TestFalse(TEXT("a Defense gem does not fit an Attack socket"),
			  UCataclysmGems::GemFits(ECataclysmSocketType::Attack, TEXT("Defense")));
	TestFalse(TEXT("an Attack gem does not fit a Utility socket"),
			  UCataclysmGems::GemFits(ECataclysmSocketType::Utility, TEXT("Attack")));
	TestTrue(TEXT("a Utility gem fits a socket for any gem"),
			 UCataclysmGems::GemFits(ECataclysmSocketType::Any, TEXT("Utility")));
	TestTrue(TEXT("the sheet's type is read in any case, spaces round it"),
			 UCataclysmGems::GemFits(ECataclysmSocketType::Defense, TEXT(" defense ")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// IS3. A worn item's gems add their modifiers once each, an empty socket adds nothing, and a two-handed weapon does not
// double its gem.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemSocketsWornTest,
	"Cataclysm.ItemSockets.AWornItemsGemsAddTheirModifiersOnceAndATwoHanderDoesNotDoubleThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemSocketsWornTest::RunTest(const FString&)
{
	using namespace CataclysmItemSocketsTest;
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* Affixes = UCataclysmDropRoll::LoadAffixTable();
	if (!TestTrue(TEXT("set-up: the generated base, affix and gem tables"),
				  Bases && Affixes && UCataclysmGems::LoadGeneratedTable()))
	{
		return false;
	}

	// A HELM: OF LIFE (SUPERB, 0.25) IN ITS DEFENSE SOCKET, AND AN EMPTY ATTACK SOCKET.
	FCataclysmItem Helm;
	Helm.Base = FName(TEXT("Head_Helm"));
	Helm.Sockets = {Socket(ECataclysmSocketType::Defense, FName(TEXT("Gem_Of_Life")), ECataclysmRarity::Superb),
					Socket(ECataclysmSocketType::Attack)};
	const TMap<FName, TArray<FCataclysmStatModifier>> HelmTotals =
		UCataclysmItemModifiers::ModifiersFor(Helm, Bases, Affixes);
	const TArray<FCataclysmStatModifier> Life = FromGems(HelmTotals, FName(TEXT("max_health")));
	if (TestEqual(TEXT("Of Life adds one modifier to maximum health"), Life.Num(), 1))
	{
		TestEqual(TEXT("of 25, its Superb 0.25"), Life[0].Value, 25.0f, 0.001f);
		TestTrue(TEXT("increased"), Life[0].Bucket == ECataclysmStatBucket::Increased);
	}
	int32 GemModifiers = 0;
	for (const TPair<FName, TArray<FCataclysmStatModifier>>& Each : HelmTotals)
	{
		GemModifiers += FromGems(HelmTotals, Each.Key).Num();
	}
	TestEqual(TEXT("and the empty socket adds nothing: one gem modifier in all"), GemModifiers, 1);

	// A GREATAXE, TWO-HANDED: OF THE VIPER (SUPERB, 0.6) IN ITS ATTACK SOCKET. Its affixes would double; its gem does not.
	FCataclysmItem Greataxe;
	Greataxe.Base = FName(TEXT("Weapon_Greataxe"));
	Greataxe.Sockets = {Socket(ECataclysmSocketType::Attack, FName(TEXT("Gem_Of_The_Viper")), ECataclysmRarity::Superb)};
	const TArray<FCataclysmStatModifier> Poison =
		FromGems(UCataclysmItemModifiers::ModifiersFor(Greataxe, Bases, Affixes), FName(TEXT("poison_chance")));
	if (TestEqual(TEXT("Of The Viper adds one modifier to poison chance"), Poison.Num(), 1))
	{
		TestEqual(TEXT("of 60, its Superb 0.6, not doubled on a two-handed weapon"), Poison[0].Value, 60.0f, 0.001f);
		TestTrue(TEXT("flat"), Poison[0].Bucket == ECataclysmStatBucket::Flat);
	}
	AddInfo(FString::Printf(TEXT("WORN GEMS: Of Life in a helm %.1f; Of The Viper in a greataxe %.1f"),
							Life.Num() ? Life[0].Value : -1.0f, Poison.Num() ? Poison[0].Value : -1.0f));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// IS4. An item's tooltip names each socket's type and the gem in it.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemSocketsLinesTest,
	"Cataclysm.ItemSockets.AnItemsSocketLinesNameEachTypeAndGem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemSocketsLinesTest::RunTest(const FString&)
{
	using namespace CataclysmItemSocketsTest;
	FCataclysmItem Item;
	Item.Base = FName(TEXT("Head_Helm"));
	Item.Sockets = {Socket(ECataclysmSocketType::Attack, FName(TEXT("Gem_Of_The_Viper")), ECataclysmRarity::Superb),
					Socket(ECataclysmSocketType::Defense), Socket(ECataclysmSocketType::Any)};
	const TArray<FString> Lines = UCataclysmGems::SocketLinesFor(Item);
	const TArray<FString> Expected = {TEXT("Attack socket: Of The Viper (Superb)"), TEXT("Defense socket: empty"),
									  TEXT("Socket for any gem: empty")};
	if (TestEqual(TEXT("one line per socket"), Lines.Num(), Expected.Num()))
	{
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("socket %d"), Index + 1), Lines[Index], Expected[Index]);
		}
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
