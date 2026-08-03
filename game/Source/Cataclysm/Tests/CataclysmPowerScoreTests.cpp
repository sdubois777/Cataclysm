// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Player/CataclysmPowerScore.h"

/**
 * Tests for the player's Power Score.
 *
 * Pinned to `sim/cataclysm_sim/player_power.py`, which is where the model was
 * argued out. The five weights are derived from two anchors and four shares
 * rather than copied, so the tests check the derivation lands on the same
 * numbers the Python model derives, not that two lists of constants match.
 */

namespace CataclysmPowerTest
{
	using FScore = UCataclysmPowerScore;

	FCataclysmScoredCharacter Uniform(int32 Level, int32 Rarity, int32 Upgrade,
									  int32 Gems, float Resistance)
	{
		FCataclysmScoredCharacter Out;
		Out.Level = Level;
		for (int32 Index = 0; Index < FScore::GearPieces; ++Index)
		{
			FCataclysmScoredGear Piece;
			Piece.Rarity = Rarity;
			Piece.Upgrade = Upgrade;
			Out.Gear.Add(Piece);
		}
		Out.Gems.Init(Rarity, Gems);
		Out.Resistances.Init(Resistance, FScore::ResistanceCount);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerWeightsTest,
	"Cataclysm.PowerScore.WeightsDeriveToTheModelsNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPowerWeightsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPowerTest;

	// Printed by sim/cataclysm_sim/player_power.py. These are DERIVED here from
	// the anchors and shares, not copied, so agreeing proves the derivation
	// rather than the typing.
	TestTrue(FString::Printf(TEXT("level weight is 6.327, got %.6f"),
			 FScore::LevelWeight()),
		FMath::IsNearlyEqual(FScore::LevelWeight(), 6.327f, 0.0005f));
	TestTrue(FString::Printf(TEXT("gear weight is 6.23301, got %.6f"),
			 FScore::GearWeight()),
		FMath::IsNearlyEqual(FScore::GearWeight(), 6.2330109f, 0.0005f));
	TestTrue(FString::Printf(TEXT("upgrade factor is 0.2524581, got %.7f"),
			 FScore::UpgradeFactor()),
		FMath::IsNearlyEqual(FScore::UpgradeFactor(), 0.25245807f, 0.0000005f));
	TestTrue(FString::Printf(TEXT("gem weight is 5.2725, got %.6f"),
			 FScore::GemWeight()),
		FMath::IsNearlyEqual(FScore::GemWeight(), 5.2725f, 0.0005f));
	TestTrue(FString::Printf(TEXT("resistance weight is 1.1298214, got %.7f"),
			 FScore::ResistanceWeight()),
		FMath::IsNearlyEqual(FScore::ResistanceWeight(), 1.1298214f, 0.0005f));

	// The four shares must add to one, or the tier 8 anchor cannot be reached.
	TestTrue(TEXT("the four shares add to one"),
		FMath::IsNearlyEqual(FScore::ShareLevel + FScore::ShareGear
							 + FScore::ShareGems + FScore::ShareResistances,
							 1.0f, 0.000001f));

	// The upgrade factor is the SAME constant gear upgrade level uses to
	// multiply every affix on a piece. A second copy of it would let gear level
	// mean two different things in two places.
	TestTrue(TEXT("a +10 piece is worth 3.52 times a +0 piece"),
		FMath::IsNearlyEqual(1.0f + FScore::UpgradeFactor() * FScore::MaxUpgrade,
							 3.5245807f, 0.0001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerAnchorTest,
	"Cataclysm.PowerScore.TheReferenceCharacterHitsBothEndAnchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPowerAnchorTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPowerTest;

	// Every figure below is what sim/cataclysm_sim/player_power.py prints.
	struct FCase { int32 Tier; int32 Expected; int32 Anchor; };
	const FCase Cases[] = {
		{ 1,  384,  385 },
		{ 2,  883,  871 },
		{ 3, 1508, 1457 },
		{ 4, 2225, 2144 },
		{ 5, 3078, 3251 },
		{ 6, 4057, 4166 },
		{ 7, 5120, 5209 },
		{ 8, 6327, 6327 },
	};

	for (const FCase& Case : Cases)
	{
		const FCataclysmScoredCharacter Reference =
			FScore::ReferenceCharacter(Case.Tier);
		const int32 Actual = FScore::Score(Reference);
		TestEqual(FString::Printf(TEXT("tier %d scores %d"), Case.Tier, Case.Expected),
			Actual, Case.Expected);

		TestEqual(FString::Printf(TEXT("tier %d anchor"), Case.Tier),
			FScore::TierAnchors()[Case.Tier], Case.Anchor);
	}

	// The two ends are pinned in the arithmetic and have to be exact. Tier 1
	// comes out one point low only because the reference character's level and
	// filled socket count are whole numbers, so it rounds to level 12 and 6 gems
	// rather than the 12.5 and 5.625 the continuous curve asks for.
	TestEqual(TEXT("tier 8 lands exactly on its anchor"),
		FScore::Score(FScore::ReferenceCharacter(8)), FScore::TierAnchors()[8]);
	TestTrue(TEXT("tier 1 is within one point of its anchor"),
		FMath::Abs(FScore::Score(FScore::ReferenceCharacter(1))
				   - FScore::TierAnchors()[1]) <= 1);

	// The six tiers in between are within 5.33%, and the worst of them is tier 5
	// at 5.3214%. The Python model's docstring rounds that to "5.3%", which is
	// why the bound here is 5.33 and not 5.3 -- a tighter bound fails against
	// the very model it is checking.
	//
	// The residual is the anchor curve itself rather than a defect in the
	// formula: tier 5 is 1,107 points wide where the surrounding trend is about
	// 790, so no smoothly progressing character can pass through that kink.
	float WorstError = 0.0f;
	for (int32 Tier = 2; Tier <= 7; ++Tier)
	{
		const float Anchor = static_cast<float>(FScore::TierAnchors()[Tier]);
		const float Error = FMath::Abs(FScore::Score(FScore::ReferenceCharacter(Tier))
									   - Anchor) / Anchor;
		WorstError = FMath::Max(WorstError, Error);
	}
	TestTrue(FString::Printf(TEXT("the middle tiers stay within 5.33%%, worst was %.4f%%"),
			 WorstError * 100.0f),
		WorstError <= 0.0533f);

	// And the worst one is tier 5, at the figure the model reports.
	const float Tier5Error =
		FMath::Abs(FScore::Score(FScore::ReferenceCharacter(5))
				   - static_cast<float>(FScore::TierAnchors()[5]))
		/ static_cast<float>(FScore::TierAnchors()[5]);
	TestTrue(FString::Printf(TEXT("tier 5 is 5.3214%% low, got %.4f%%"),
			 Tier5Error * 100.0f),
		FMath::IsNearlyEqual(Tier5Error, 0.053214f, 0.0001f));

	// A tier outside 1 to 8 is refused rather than guessed at.
	AddExpectedError(TEXT("outside 1-8"), EAutomationExpectedErrorFlags::Contains, 2);
	TestEqual(TEXT("tier 0 scores nothing"), FScore::Score(FScore::ReferenceCharacter(0)), 0);
	TestEqual(TEXT("tier 9 scores nothing"), FScore::Score(FScore::ReferenceCharacter(9)), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerSharesTest,
	"Cataclysm.PowerScore.EachSourceSuppliesItsShareAtTierEight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPowerSharesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPowerTest;

	// The four shares are the only free choice in the model, so what they say a
	// finished character draws from each source has to actually happen.
	const FCataclysmPowerBreakdown Finished =
		FScore::Breakdown(FScore::ReferenceCharacter(8));
	const float Total = static_cast<float>(Finished.Total);

	struct FCase { const TCHAR* Name; float Part; float Share; };
	const FCase Cases[] = {
		{ TEXT("level"),       Finished.FromLevel,       FScore::ShareLevel },
		{ TEXT("gear"),        Finished.FromGear,        FScore::ShareGear },
		{ TEXT("gems"),        Finished.FromGems,        FScore::ShareGems },
		{ TEXT("resistances"), Finished.FromResistances, FScore::ShareResistances },
	};

	for (const FCase& Case : Cases)
	{
		const float Actual = Case.Part / Total;
		TestTrue(FString::Printf(TEXT("%s supplies %.0f%%, got %.1f%%"),
				 Case.Name, Case.Share * 100.0f, Actual * 100.0f),
			FMath::IsNearlyEqual(Actual, Case.Share, 0.002f));
	}

	TestEqual(TEXT("and the four terms add to the total"),
		FMath::RoundToInt(Finished.FromLevel + Finished.FromGear
						  + Finished.FromGems + Finished.FromResistances),
		Finished.Total);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerRulesTest,
	"Cataclysm.PowerScore.TheFourRulesTheFormulaImplies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPowerRulesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPowerTest;

	// 1. SOCKET COUNT HAS NO WEIGHT OF ITS OWN. It is the number of terms in the
	// gem sum, so filling a socket is what a socket contributes. An empty socket
	// is worth nothing.
	FCataclysmScoredCharacter Empty = Uniform(100, 8, 10, 0, 70.0f);
	FCataclysmScoredCharacter OneGem = Uniform(100, 8, 10, 1, 70.0f);
	TestTrue(TEXT("filling one socket raises the score"),
		FScore::Score(OneGem) > FScore::Score(Empty));
	TestTrue(TEXT("by exactly one gem's worth"),
		FMath::IsNearlyEqual(
			static_cast<float>(FScore::Score(OneGem) - FScore::Score(Empty)),
			FScore::GemWeight() * FScore::MaxRarity, 1.0f));

	// 2. UPGRADE LEVEL MULTIPLIES RARITY RATHER THAN ADDING TO IT. So a +10
	// Cataclysmic piece is worth far more than a +10 Everyday one, and that is
	// the only reason the score curve bends.
	const FCataclysmScoredCharacter TopRarityPlusTen = Uniform(1, 8, 10, 0, 0.0f);
	const FCataclysmScoredCharacter LowRarityPlusTen = Uniform(1, 1, 10, 0, 0.0f);
	const FCataclysmScoredCharacter TopRarityPlusZero = Uniform(1, 8, 0, 0, 0.0f);

	const float FromTopTen = FScore::Breakdown(TopRarityPlusTen).FromGear;
	const float FromLowTen = FScore::Breakdown(LowRarityPlusTen).FromGear;
	const float FromTopZero = FScore::Breakdown(TopRarityPlusZero).FromGear;

	TestTrue(TEXT("a +10 Cataclysmic piece is 8 times a +10 Everyday one"),
		FMath::IsNearlyEqual(FromTopTen / FromLowTen, 8.0f, 0.01f));
	TestTrue(TEXT("and 3.52 times the same piece at +0"),
		FMath::IsNearlyEqual(FromTopTen / FromTopZero, 3.5245807f, 0.001f));

	// 3. RESISTANCE ABOVE THE CAP ADDS NO SCORE. Over-capping stays legal and
	// useful, because enemy penetration eats into it, but it is headroom against
	// penetration rather than power.
	const FCataclysmScoredCharacter Capped = Uniform(100, 8, 10, 45, 70.0f);
	const FCataclysmScoredCharacter OverCapped = Uniform(100, 8, 10, 45, 200.0f);
	TestEqual(TEXT("200% resistance scores the same as 70%"),
		FScore::Score(OverCapped), FScore::Score(Capped));

	// 4. NO VITAL ENTERS THE SCORE. There is nowhere to put health, mana or
	// energy shield, which is why a character can be scored without its stat
	// line. Checked by construction: the shape carries only the four inputs, and
	// the four terms add to the whole score with nothing left over.
	const FCataclysmPowerBreakdown Full = FScore::Breakdown(Capped);
	TestEqual(TEXT("nothing contributes that is not one of the four sources"),
		FMath::RoundToInt(Full.FromLevel + Full.FromGear + Full.FromGems
						  + Full.FromResistances),
		Full.Total);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
