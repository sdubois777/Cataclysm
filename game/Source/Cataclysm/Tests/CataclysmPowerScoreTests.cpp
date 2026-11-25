// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Player/CataclysmPowerScore.h"

/**
 * Tests for the player's Power Score.
 *
 * Pinned to `sim/cataclysm_sim/player_power.py`, which is where the model was
 * argued out. SINCE THE OWNER'S RULING OF 2026-11-24 (issue #2853) the five
 * weights are plain numbers written out on both sides -- 10 a level, 2 a gear
 * rarity step, an upgrade factor of 1, 5 a gem rarity step and 1 a resistance
 * percent -- and `tools/tests/test_power_score_port.py` holds the header's
 * literals to the model's. Until then they were derived from two anchors and
 * four shares, and these tests checked the derivation.
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
	"Cataclysm.PowerScore.TheWeightsAreTheOwnersPlainNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Adopt, and look at drop supply (Recommended)", the owner, 2026-11-24 (issue
 * #2853): 10 a level, 2 a gear rarity step, an upgrade factor of 1, 5 a gem
 * rarity step, 1 a resistance percent. Written out here as the ruling states
 * them, so a re-tune has to change this test as well as the header. Prints
 * "POWER WEIGHTS".
 */
bool FCataclysmPowerWeightsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPowerTest;

	AddInfo(FString::Printf(TEXT("POWER WEIGHTS: level %.4f, gear %.4f, upgrade factor %.4f, gem %.4f, resistance %.4f"),
							FScore::LevelWeight(), FScore::GearWeight(), FScore::UpgradeFactor(), FScore::GemWeight(),
							FScore::ResistanceWeight()));

	TestEqual(TEXT("**10 a level**"), FScore::LevelWeight(), 10.0f);
	TestEqual(TEXT("**2 a gear rarity step**"), FScore::GearWeight(), 2.0f);
	TestEqual(TEXT("**an upgrade factor of 1**"), FScore::UpgradeFactor(), 1.0f);
	TestEqual(TEXT("**5 a gem rarity step**"), FScore::GemWeight(), 5.0f);
	TestEqual(TEXT("**1 a resistance percent**"), FScore::ResistanceWeight(), 1.0f);

	// THE FUNCTIONS ARE THE CONSTANTS, not a second copy of them.
	TestEqual(TEXT("LevelWeight is PointsPerLevel"), FScore::LevelWeight(), FScore::PointsPerLevel);
	TestEqual(TEXT("GearWeight is PointsPerGearRarityStep"), FScore::GearWeight(), FScore::PointsPerGearRarityStep);
	TestEqual(TEXT("UpgradeFactor is UpgradeFactorPerGearLevel"), FScore::UpgradeFactor(),
			  FScore::UpgradeFactorPerGearLevel);
	TestEqual(TEXT("GemWeight is PointsPerGemRarityStep"), FScore::GemWeight(), FScore::PointsPerGemRarityStep);
	TestEqual(TEXT("ResistanceWeight is PointsPerResistancePercent"), FScore::ResistanceWeight(),
			  FScore::PointsPerResistancePercent);

	// A +10 piece against the same piece at +0: 11 times, where the derived factor made it 3.52 times.
	TestEqual(TEXT("a +10 piece is worth 11 times a +0 piece"),
			  1.0f + FScore::UpgradeFactor() * FScore::MaxUpgrade, 11.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerLevelSixTest,
	"Cataclysm.PowerScore.ALevelSixCharacterScoresSixtyFromItsLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The owner's screen of 2026-11-24 read 38 from level for a level 6 character
 * (6.327 a level) and asked why. Under the ruled weights a level 6 character
 * wearing nothing scores 60, all of it from its level. CONTROL: level 7 scores
 * 70, so the 60 is ten a level and not a constant. Prints "POWER LEVEL SIX".
 */
bool FCataclysmPowerLevelSixTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPowerTest;

	FCataclysmScoredCharacter Six;
	Six.Level = 6;
	FCataclysmScoredCharacter Seven;
	Seven.Level = 7;
	const FCataclysmPowerBreakdown AtSix = FScore::Breakdown(Six);
	const FCataclysmPowerBreakdown AtSeven = FScore::Breakdown(Seven);
	AddInfo(FString::Printf(TEXT("POWER LEVEL SIX: level 6 from level %.4f, total %d; level 7 from level %.4f, total %d"),
							AtSix.FromLevel, AtSix.Total, AtSeven.FromLevel, AtSeven.Total));

	TestEqual(TEXT("**a level 6 character's level part is 60**"), AtSix.FromLevel, 60.0f);
	TestEqual(TEXT("and with nothing worn that is its whole score"), AtSix.Total, 60);
	TestEqual(TEXT("nothing from gear"), AtSix.FromGear, 0.0f);
	TestEqual(TEXT("CONTROL: level 7 is 70"), AtSeven.Total, 70);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerAnchorTest,
	"Cataclysm.PowerScore.TheReferenceCharacterLandsNearEveryTiersAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The proposal's "Recommended" column, which the owner ruled on: the reference
 * character scores 364 at tier 1 (-5.5% of 385) to 6,528 at tier 8 (+3.2% of
 * 6,327), every tier within 6%. The anchors themselves did not move. Prints
 * "POWER ANCHORS".
 */
bool FCataclysmPowerAnchorTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPowerTest;

	// Every figure below is what sim/cataclysm_sim/player_power.py prints.
	struct FCase { int32 Tier; int32 Expected; int32 Anchor; };
	const FCase Cases[] = {
		{ 1,  364,  385 },
		{ 2,  860,  883 },
		{ 3, 1493, 1508 },
		{ 4, 2228, 2225 },
		{ 5, 3110, 3078 },
		{ 6, 4134, 4057 },
		{ 7, 5255, 5120 },
		{ 8, 6528, 6327 },
	};

	TArray<FString> Said;
	for (const FCase& Case : Cases)
	{
		const FCataclysmScoredCharacter Reference =
			FScore::ReferenceCharacter(Case.Tier);
		const int32 Actual = FScore::Score(Reference);
		const float Off = 100.0f * static_cast<float>(Actual - Case.Anchor) / static_cast<float>(Case.Anchor);
		Said.Add(FString::Printf(TEXT("T%d %d against %d (%+.1f%%)"), Case.Tier, Actual, Case.Anchor, Off));
		TestEqual(FString::Printf(TEXT("tier %d scores %d"), Case.Tier, Case.Expected),
			Actual, Case.Expected);

		TestEqual(FString::Printf(TEXT("tier %d anchor"), Case.Tier),
			FScore::TierAnchors()[Case.Tier], Case.Anchor);

		// THE BAND THE OWNER WAS SHOWN: -5.5% at tier 1 to +3.2% at tier 8.
		TestTrue(FString::Printf(TEXT("tier %d is within -5.5%% to +3.2%% of its anchor, %+.2f%%"), Case.Tier, Off),
			Off >= -5.5f && Off <= 3.2f);
	}
	AddInfo(FString::Printf(TEXT("POWER ANCHORS: %s"), *FString::Join(Said, TEXT(" | "))));

	// THE WEIGHTS ARE CHOSEN, NOT FITTED, so no tier is pinned to its anchor. Until 2026-11-24 the weights were solved
	// from the tier 1 and tier 8 anchors, tier 8 landed exactly on 6,327 and the six between were exact too, because on
	// 2026-08-05 those six anchors were reset to what that formula predicted (DungeonSimulator commit 6c9be8b, issue #7).
	TestTrue(TEXT("tier 8 is above its anchor, as the proposal measured"),
		FScore::Score(FScore::ReferenceCharacter(8)) > FScore::TierAnchors()[8]);
	TestTrue(TEXT("tier 1 is below its anchor, as the proposal measured"),
		FScore::Score(FScore::ReferenceCharacter(1)) < FScore::TierAnchors()[1]);

	// Tier width climbs at every tier. This is the defect issue #7 was filed for,
	// asserted directly rather than through the residual it produced. Tier width
	// multiplies every weighted term in the Enemy Score model, so a tier narrower
	// than the one below compresses that tier's whole rarity spread.
	for (int32 Tier = 2; Tier <= 8; ++Tier)
	{
		const int32 Below = FScore::TierAnchors()[Tier - 1] - FScore::TierAnchors()[Tier - 2];
		const int32 Here = FScore::TierAnchors()[Tier] - FScore::TierAnchors()[Tier - 1];
		TestTrue(FString::Printf(TEXT("tier %d is wider than tier %d, %d against %d"),
				 Tier, Tier - 1, Here, Below),
			Here > Below);
	}

	// A tier outside 1 to 8 is refused rather than guessed at.
	AddExpectedError(TEXT("outside 1-8"), EAutomationExpectedErrorFlags::Contains, 2);
	TestEqual(TEXT("tier 0 scores nothing"), FScore::Score(FScore::ReferenceCharacter(0)), 0);
	TestEqual(TEXT("tier 9 scores nothing"), FScore::Score(FScore::ReferenceCharacter(9)), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerSharesTest,
	"Cataclysm.PowerScore.TheSharesFallOutAtFifteenFortyNineTwentyEightAndNine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE SHARES ARE AN OUTCOME OF THE WEIGHTS SINCE 2026-11-24, not an input: the
 * finished tier 8 character draws 15% from level, 49% from gear, 28% from gems
 * and 9% from resistances (10/50/30/10 before, when the weights were derived
 * from those), and the finished tier 1 character 33/40/8/19, the owner's "more
 * points on your level instead of gear". Whole percents of the four terms'
 * sum. Prints "POWER SHARES".
 */
bool FCataclysmPowerSharesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPowerTest;

	struct FCase { int32 Tier; int32 Level; int32 Gear; int32 Gems; int32 Resistances; };
	const FCase Cases[] = {
		{ 8, 15, 49, 28, 9 },
		{ 1, 33, 40, 8, 19 },
	};

	TArray<FString> Said;
	for (const FCase& Case : Cases)
	{
		const FCataclysmPowerBreakdown Finished = FScore::Breakdown(FScore::ReferenceCharacter(Case.Tier));
		const float Sum = Finished.FromLevel + Finished.FromGear + Finished.FromGems + Finished.FromResistances;
		const auto Percent = [Sum](float Part) { return FMath::RoundToInt(100.0f * Part / Sum); };
		Said.Add(FString::Printf(TEXT("T%d %d / %d / %d / %d"), Case.Tier, Percent(Finished.FromLevel),
								 Percent(Finished.FromGear), Percent(Finished.FromGems),
								 Percent(Finished.FromResistances)));

		TestEqual(FString::Printf(TEXT("**tier %d: level supplies %d%%**"), Case.Tier, Case.Level),
				  Percent(Finished.FromLevel), Case.Level);
		TestEqual(FString::Printf(TEXT("tier %d: gear supplies %d%%"), Case.Tier, Case.Gear),
				  Percent(Finished.FromGear), Case.Gear);
		TestEqual(FString::Printf(TEXT("tier %d: gems supply %d%%"), Case.Tier, Case.Gems),
				  Percent(Finished.FromGems), Case.Gems);
		TestEqual(FString::Printf(TEXT("tier %d: resistances supply %d%%"), Case.Tier, Case.Resistances),
				  Percent(Finished.FromResistances), Case.Resistances);
		TestEqual(FString::Printf(TEXT("tier %d: and the four terms add to the total"), Case.Tier),
				  FMath::RoundToInt(Sum), Finished.Total);
	}
	AddInfo(FString::Printf(TEXT("POWER SHARES: %s"), *FString::Join(Said, TEXT(" | "))));

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
	TestTrue(TEXT("and 11 times the same piece at +0"),
		FMath::IsNearlyEqual(FromTopTen / FromTopZero, 11.0f, 0.001f));

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
