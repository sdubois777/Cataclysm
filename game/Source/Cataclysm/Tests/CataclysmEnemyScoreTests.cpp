// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dungeon/CataclysmEnemyScore.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Player/CataclysmPlayerState.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for Enemy Score, the port of `enemy_scores` in
 * `sim/cataclysm_sim/scoring.py`.
 *
 * REGISTERED UNDER `Cataclysm.EnemyScore`, which is not this file's name. A run
 * narrowed with the wrong prefix reports "0 tests performed" and reads exactly
 * like a passing guard.
 *
 * WHAT THESE CHECK AND WHAT `tools/tests/test_enemy_score_port.py` CHECKS. That
 * file reads the constants out of this port's source and compares them with the
 * Python model. These run the code. A test against a constant cannot notice that
 * the code ignores it, and a test that reads a constant out of source cannot
 * notice that the constant is wrong, so both halves are needed.
 *
 * THE EIGHT QUOTED SCORES BELOW WERE COMPUTED BY THE PYTHON MODEL and are typed
 * in on purpose. Everything else in this file recomputes the formula
 * independently, which catches a term applied in the wrong place but would not
 * catch the whole formula being wrong in the same way twice.
 */

namespace CataclysmEnemyScoreTest
{
	/** The formula worked out here, independently of the port's own arithmetic. */
	int32 Expected(const FCataclysmScoredFloor& Floor, int32 RarityStep)
	{
		const int32 Tier = FMath::Clamp(Floor.DifficultyTier, 1, 8);
		const int32 Total = FMath::Max(1, Floor.TotalFloors);
		const int32 Which = FMath::Clamp(Floor.FloorNumber, 1, Total);

		const double Min = UCataclysmEnemyScore::TierAnchors()[Tier - 1];
		const double Max = UCataclysmEnemyScore::TierAnchors()[Tier];
		const double Width = Max - Min;

		const int32 Middle = FMath::CeilToInt(Total / 2.0);
		const double Ratio = static_cast<double>(Which) / static_cast<double>(Total);

		const double Baseline =
			Min + Width * UCataclysmEnemyScore::BaselineWeight * Ratio;
		const double Procedural =
			(UCataclysmEnemyScore::FloorScalingBase(Floor.Type)
			 / UCataclysmEnemyScore::ProceduralDivisor) * Ratio
			+ static_cast<double>(Which) * UCataclysmEnemyScore::ProceduralPerFloor;
		const double Tension = static_cast<double>(Which - Middle)
			* static_cast<double>(Tier) * UCataclysmEnemyScore::DepthTensionPerTier;

		const double Score = Baseline
			+ Width * UCataclysmEnemyScore::TypeWeight(Floor.Type)
			+ Width * UCataclysmEnemyScore::SubTypeWeight(Floor.SubType)
			+ Width * UCataclysmEnemyScore::RarityWeight(RarityStep)
			+ Procedural + Tension + static_cast<double>(Floor.ModifierScore);

		return static_cast<int32>(FMath::FloorToDouble(Score + 0.5));
	}

	FCataclysmScoredFloor Make(int32 Tier, ECataclysmDungeonType Type,
							   ECataclysmDungeonSubType SubType,
							   int32 TotalFloors, int32 FloorNumber,
							   float ModifierScore = 0.0f)
	{
		FCataclysmScoredFloor Floor;
		Floor.DifficultyTier = Tier;
		Floor.Type = Type;
		Floor.SubType = SubType;
		Floor.TotalFloors = TotalFloors;
		Floor.FloorNumber = FloorNumber;
		Floor.ModifierScore = ModifierScore;
		return Floor;
	}
}

// ---------------------------------------------------------------------------
// Against the Python model
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyScoreMatchesTheModel,
	"Cataclysm.EnemyScore.EightScoresMatchWhatThePythonModelComputes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyScoreMatchesTheModel::RunTest(const FString&)
{
	using namespace CataclysmEnemyScoreTest;

	struct FCase
	{
		int32 Tier;
		ECataclysmDungeonType Type;
		ECataclysmDungeonSubType SubType;
		int32 TotalFloors;
		int32 FloorNumber;
		float ModifierScore;
		int32 RarityStep;
		int32 Expected;
	};

	// COMPUTED BY sim/cataclysm_sim/scoring.py AND TYPED IN. Chosen to move
	// every term at least once: both ends of the tier range, all four dungeon
	// types, a sub-type that carries weight and one that deliberately does not,
	// a floor above the middle and one below it, a non-zero modifier score, and
	// a dungeon short enough that its middle floor rounds up.
	const FCase Cases[] = {
		{ 1, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None,
		  50, 50, 0.0f, 0, 407 },
		{ 1, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None,
		  50, 1, 0.0f, 0, -21 },
		{ 8, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None,
		  50, 50, 0.0f, 5, 7080 },
		{ 4, ECataclysmDungeonType::Cataclysm, ECataclysmDungeonSubType::Sacrificial,
		  125, 63, 0.0f, 4, 2377 },
		{ 5, ECataclysmDungeonType::Quest, ECataclysmDungeonSubType::Horde,
		  30, 15, 0.0f, 2, 2792 },
		{ 8, ECataclysmDungeonType::FallenCity, ECataclysmDungeonSubType::Volatile,
		  100, 100, 0.0f, 3, 7258 },
		{ 3, ECataclysmDungeonType::Quest, ECataclysmDungeonSubType::Timed,
		  20, 7, 25.0f, 1, 1164 },
		{ 2, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::CowLevel,
		  9, 9, 0.0f, 5, 1151 },
	};

	for (const FCase& Case : Cases)
	{
		const FCataclysmScoredFloor Floor = Make(Case.Tier, Case.Type, Case.SubType,
			Case.TotalFloors, Case.FloorNumber, Case.ModifierScore);
		TestEqual(FString::Printf(
				TEXT("tier %d, %d floors, floor %d, rarity step %d"),
				Case.Tier, Case.TotalFloors, Case.FloorNumber, Case.RarityStep),
			UCataclysmEnemyScore::ScoreFor(Floor, Case.RarityStep), Case.Expected);
	}

	return true;
}

// ---------------------------------------------------------------------------
// The shape of the formula
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyScoreEveryTermIsRecomputed,
	"Cataclysm.EnemyScore.EveryTermIsRecomputedAcrossASweep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyScoreEveryTermIsRecomputed::RunTest(const FString&)
{
	using namespace CataclysmEnemyScoreTest;

	// EVERY COMBINATION OF THE THINGS THAT ARE SMALL ENOUGH TO ENUMERATE, which
	// is 8 tiers x 4 types x 8 sub-types x 6 rarities, and three floor positions
	// each: the first, the middle and the last. Eight typed cases can be made to
	// pass by a formula that is wrong in a way they happen not to touch.
	const ECataclysmDungeonType Types[] = {
		ECataclysmDungeonType::Basic, ECataclysmDungeonType::Quest,
		ECataclysmDungeonType::FallenCity, ECataclysmDungeonType::Cataclysm };
	const ECataclysmDungeonSubType SubTypes[] = {
		ECataclysmDungeonSubType::None, ECataclysmDungeonSubType::Timed,
		ECataclysmDungeonSubType::Horde, ECataclysmDungeonSubType::Siege,
		ECataclysmDungeonSubType::CowLevel, ECataclysmDungeonSubType::Elite,
		ECataclysmDungeonSubType::Volatile, ECataclysmDungeonSubType::Sacrificial };

	int32 Checked = 0;
	for (int32 Tier = 1; Tier <= 8; ++Tier)
	{
		for (ECataclysmDungeonType Type : Types)
		{
			for (ECataclysmDungeonSubType SubType : SubTypes)
			{
				for (const int32 Total : { 9, 50, 125 })
				{
					for (const int32 Which : { 1, Total / 2, Total })
					{
						const FCataclysmScoredFloor Floor =
							Make(Tier, Type, SubType, Total, Which);
						for (int32 Step = 0;
							 Step < UCataclysmEnemyScore::RarityStepCount; ++Step)
						{
							if (UCataclysmEnemyScore::ScoreFor(Floor, Step)
								!= Expected(Floor, Step))
							{
								AddError(FString::Printf(
									TEXT("tier %d, %d floors, floor %d, step %d: "
										 "got %d, recomputed %d"),
									Tier, Total, Which, Step,
									UCataclysmEnemyScore::ScoreFor(Floor, Step),
									Expected(Floor, Step)));
								return false;
							}
							++Checked;
						}
					}
				}
			}
		}
	}

	TestEqual(TEXT("the sweep covered 8 tiers x 4 types x 8 sub-types x 3 depths "
				   "x 3 positions x 6 rarities"),
		Checked, 8 * 4 * 8 * 3 * 3 * 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyScoreRisesWithDepth,
	"Cataclysm.EnemyScore.ADeeperFloorIsWorthMoreThanAShallowerOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyScoreRisesWithDepth::RunTest(const FString&)
{
	using namespace CataclysmEnemyScoreTest;

	// THE PROPERTY THE EXPERIENCE CURVE RESTS ON. Since 2026-08-24 an enemy's
	// Enemy Score IS the experience it grants, and the whole size of the climb
	// was measured by summing a dungeon floor by floor. If depth stopped raising
	// a creature's score, every figure in that measurement would be wrong.
	for (int32 Tier = 1; Tier <= 8; ++Tier)
	{
		const FCataclysmScoredFloor First =
			Make(Tier, ECataclysmDungeonType::Basic,
				 ECataclysmDungeonSubType::None, 50, 1);
		const FCataclysmScoredFloor Last =
			Make(Tier, ECataclysmDungeonType::Basic,
				 ECataclysmDungeonSubType::None, 50, 50);
		TestTrue(FString::Printf(
				TEXT("tier %d: floor 50 is worth more than floor 1"), Tier),
			UCataclysmEnemyScore::ScoreFor(Last, 0)
				> UCataclysmEnemyScore::ScoreFor(First, 0));
	}

	// AND EVERY STEP OF THE WAY, not just the two ends.
	int32 Previous = TNumericLimits<int32>::Lowest();
	for (int32 Which = 1; Which <= 50; ++Which)
	{
		const FCataclysmScoredFloor Floor =
			Make(4, ECataclysmDungeonType::Basic,
				 ECataclysmDungeonSubType::None, 50, Which);
		const int32 Score = UCataclysmEnemyScore::ScoreFor(Floor, 0);
		TestTrue(FString::Printf(TEXT("floor %d is worth more than floor %d"),
				 Which, Which - 1), Score > Previous);
		Previous = Score;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyScoreRisesWithRarity,
	"Cataclysm.EnemyScore.ARarerEnemyIsWorthMoreOnTheSameFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyScoreRisesWithRarity::RunTest(const FString&)
{
	using namespace CataclysmEnemyScoreTest;

	// A COMMON IS THE BASELINE AND ADDS NOTHING, which is what makes a rarity's
	// weight the share of mitigation Overwhelm strips. Section X of
	// `docs/Cataclysm_GDD_v2.md` relies on that, so it is asserted rather than
	// left implied.
	TestEqual(TEXT("a Common adds nothing to the score"),
		UCataclysmEnemyScore::RarityWeight(0), 0.0);

	const FCataclysmScoredFloor Floor =
		Make(6, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, 50, 25);

	const TArray<int32> Scores = UCataclysmEnemyScore::ScoresFor(Floor);
	TestEqual(TEXT("one score per rarity"), Scores.Num(),
		UCataclysmEnemyScore::RarityStepCount);

	for (int32 Step = 1; Step < Scores.Num(); ++Step)
	{
		TestTrue(FString::Printf(
				TEXT("rarity step %d is worth more than step %d"), Step, Step - 1),
			Scores[Step] > Scores[Step - 1]);
		TestEqual(FString::Printf(TEXT("and ScoresFor agrees with ScoreFor at %d"),
				 Step),
			Scores[Step], UCataclysmEnemyScore::ScoreFor(Floor, Step));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyScoreCanBeNegativeNearAnEntrance,
	"Cataclysm.EnemyScore.AShallowFloorAtTheLowestTierCanScoreBelowZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyScoreCanBeNegativeNearAnEntrance::RunTest(const FString&)
{
	using namespace CataclysmEnemyScoreTest;

	// THIS IS THE MODEL BEHAVING, NOT A FAULT, and it is asserted so nobody
	// "fixes" it. The depth tension term is large and negative near an entrance,
	// and at difficulty tier 1 the tier is only 385 points wide, so it can carry
	// a Common enemy below zero.
	//
	// IT MATTERS BECAUSE ENEMY SCORE IS EXPERIENCE. A kill worth zero or less
	// grants nothing -- `ACataclysmPlayerState::GrantExperience` ignores it --
	// so the first floors of a tier 1 dungeon pay nothing for the two lowest
	// rarities. Measured against the Python model: three floors of fifty for a
	// Common, one for an Elite, and none at all at any tier above 1.
	int32 NonPositiveCommons = 0;
	for (int32 Which = 1; Which <= 50; ++Which)
	{
		const FCataclysmScoredFloor Floor =
			Make(1, ECataclysmDungeonType::Basic,
				 ECataclysmDungeonSubType::None, 50, Which);
		if (UCataclysmEnemyScore::ScoreFor(Floor, 0) <= 0)
		{
			++NonPositiveCommons;
		}
	}
	TestEqual(TEXT("three floors of fifty pay a Common nothing at tier 1"),
		NonPositiveCommons, 3);

	// AND IT IS CONFINED TO THE LOWEST TIER. If it ever reached tier 2 the
	// early game would have a dead stretch nobody designed.
	for (int32 Tier = 2; Tier <= 8; ++Tier)
	{
		for (int32 Which = 1; Which <= 50; ++Which)
		{
			const FCataclysmScoredFloor Floor =
				Make(Tier, ECataclysmDungeonType::Basic,
					 ECataclysmDungeonSubType::None, 50, Which);
			if (UCataclysmEnemyScore::ScoreFor(Floor, 0) <= 0)
			{
				AddError(FString::Printf(
					TEXT("a Common on floor %d of a tier %d dungeon scores %d, "
						 "which pays no experience. That used to happen only at "
						 "tier 1."),
					Which, Tier, UCataclysmEnemyScore::ScoreFor(Floor, 0)));
				return false;
			}
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Nonsense input
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyScoreClampsNonsense,
	"Cataclysm.EnemyScore.ANonsenseFloorIsClampedRatherThanRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyScoreClampsNonsense::RunTest(const FString&)
{
	using namespace CataclysmEnemyScoreTest;

	// REACHED FROM A RUNNING GAME where `Cataclysm.DifficultyTier` can be typed
	// at and a dungeon's length is a setting. A creature worth nothing because
	// somebody typed a nine is worse than the nearest sensible answer.
	const FCataclysmScoredFloor Sane =
		Make(8, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, 50, 50);

	TestEqual(TEXT("a tier above 8 is treated as 8"),
		UCataclysmEnemyScore::ScoreFor(
			Make(99, ECataclysmDungeonType::Basic,
				 ECataclysmDungeonSubType::None, 50, 50), 0),
		UCataclysmEnemyScore::ScoreFor(Sane, 0));

	const FCataclysmScoredFloor One =
		Make(1, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, 50, 50);
	TestEqual(TEXT("a tier below 1 is treated as 1"),
		UCataclysmEnemyScore::ScoreFor(
			Make(-4, ECataclysmDungeonType::Basic,
				 ECataclysmDungeonSubType::None, 50, 50), 0),
		UCataclysmEnemyScore::ScoreFor(One, 0));

	// A FLOOR NUMBER BEYOND THE DUNGEON IS THE LAST FLOOR, not a ratio above
	// one. Without the clamp the baseline term would keep climbing past what
	// the tier is meant to contain.
	TestEqual(TEXT("a floor past the end is the last floor"),
		UCataclysmEnemyScore::ScoreFor(
			Make(8, ECataclysmDungeonType::Basic,
				 ECataclysmDungeonSubType::None, 50, 900), 0),
		UCataclysmEnemyScore::ScoreFor(Sane, 0));

	// A DUNGEON WITH NO FLOORS WOULD DIVIDE BY ZERO. One floor is the nearest
	// dungeon that exists.
	const int32 Single = UCataclysmEnemyScore::ScoreFor(
		Make(4, ECataclysmDungeonType::Basic,
			 ECataclysmDungeonSubType::None, 0, 1), 0);
	TestEqual(TEXT("a dungeon of no floors is treated as one floor"),
		Single,
		UCataclysmEnemyScore::ScoreFor(
			Make(4, ECataclysmDungeonType::Basic,
				 ECataclysmDungeonSubType::None, 1, 1), 0));
	TestTrue(TEXT("and that is a real number rather than an infinity"),
		Single > -100000 && Single < 100000);

	// A RARITY STEP OUTSIDE 0 TO 5 ADDS NOTHING rather than reading off the end
	// of the weight table.
	TestEqual(TEXT("a rarity step above the last adds nothing"),
		UCataclysmEnemyScore::RarityWeight(99), 0.0);
	TestEqual(TEXT("and so does a negative one"),
		UCataclysmEnemyScore::RarityWeight(-1), 0.0);
	TestEqual(TEXT("so an unknown rarity scores as a Common"),
		UCataclysmEnemyScore::ScoreFor(Sane, 99),
		UCataclysmEnemyScore::ScoreFor(Sane, 0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyScoreTierAnchorsAreTheDesigns,
	"Cataclysm.EnemyScore.TheTierAnchorsAreTheDesignedPowerScoreRanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyScoreTierAnchorsAreTheDesigns::RunTest(const FString&)
{
	// THE EIGHT NUMBERS EVERY OTHER FIGURE IS A FRACTION OF. They are the
	// "Power Score Ranges by Tier" table in section XII of
	// `docs/Cataclysm_GDD_v2.md`, and `tools/tests/test_enemy_score_port.py`
	// compares them with `sim/cataclysm_sim/scoring.py`. Here they are only
	// checked for the properties the formula needs.
	const TArray<double>& Anchors = UCataclysmEnemyScore::TierAnchors();
	TestEqual(TEXT("nine anchors, so tier N has a floor and a ceiling"),
		Anchors.Num(), 9);
	TestEqual(TEXT("tier 1 starts at zero"), Anchors[0], 0.0);

	for (int32 Index = 1; Index < Anchors.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("anchor %d is above anchor %d"),
				 Index, Index - 1), Anchors[Index] > Anchors[Index - 1]);
	}

	// EVERY TIER IS WIDER THAN THE ONE BELOW IT. This was not always true:
	// tier 6 was once narrower than tier 5, which is why the anchors were reset
	// upstream on 2026-08-05. A tier that narrows makes every fractional term
	// shrink with it, so a rarer enemy would be worth less than it was a tier
	// earlier.
	for (int32 Tier = 2; Tier <= 8; ++Tier)
	{
		TestTrue(FString::Printf(TEXT("tier %d is wider than tier %d"),
				 Tier, Tier - 1),
			UCataclysmEnemyScore::TierWidth(Tier)
				> UCataclysmEnemyScore::TierWidth(Tier - 1));
	}

	return true;
}


// ---------------------------------------------------------------------------
// Where a fight is happening, and what a kill is worth there
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyScoreFloorInHasSaneDefaults,
	"Cataclysm.EnemyScore.AWorldWithNoGameModeStillGivesARealFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyScoreFloorInHasSaneDefaults::RunTest(const FString&)
{
	// AN AUTOMATION TEST THAT KILLS A CREATURE IN A BARE WORLD MUST STILL GET A
	// SCORE. Fifteen existing tests do exactly that, and if this answered with
	// nothing they would silently start awarding no experience.
	const FCataclysmScoredFloor Nowhere = UCataclysmEnemyScore::FloorIn(nullptr);

	TestEqual(TEXT("difficulty tier 1"), Nowhere.DifficultyTier, 1);
	TestEqual(TEXT("one floor"), Nowhere.TotalFloors, 1);
	TestEqual(TEXT("standing on it"), Nowhere.FloorNumber, 1);
	TestTrue(TEXT("a Basic dungeon"),
		Nowhere.Type == ECataclysmDungeonType::Basic);
	TestTrue(TEXT("with no sub-type"),
		Nowhere.SubType == ECataclysmDungeonSubType::None);
	TestEqual(TEXT("and no modifiers, because none exist"),
		Nowhere.ModifierScore, 0.0f);

	// AND THAT FLOOR IS WORTH SOMETHING. One floor of one makes the floor ratio
	// 1, so a creature there scores as though it stood on a dungeon's last
	// floor, which is what a training target ought to be worth.
	TestTrue(TEXT("a Common on it is worth more than nothing"),
		UCataclysmEnemyScore::ScoreFor(Nowhere, 0) > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyScoreReadsTheDungeon,
	"Cataclysm.EnemyScore.TheDungeonGameModeDecidesWhereTheFightIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyScoreReadsTheDungeon::RunTest(const FString&)
{
	// THE JOIN BETWEEN THE MODEL AND THE GAME, which is the part a pure
	// arithmetic test cannot reach. A dungeon that reported the wrong depth
	// would give every creature in it the wrong score, and every other test in
	// this file would still pass.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode =
		World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("dungeon game mode"), Mode))
	{
		return false;
	}

	Mode->TotalFloors = 40;
	Mode->FloorNumber = 12;
	Mode->DungeonType = ECataclysmDungeonType::FallenCity;
	Mode->DungeonSubType = ECataclysmDungeonSubType::Elite;

	TestEqual(TEXT("the dungeon reports its length"), Mode->RunTotalFloors(), 40);
	TestEqual(TEXT("and which floor is being walked"), Mode->RunFloorNumber(), 12);
	TestTrue(TEXT("and its kind"),
		Mode->RunDungeonType() == ECataclysmDungeonType::FallenCity);
	TestTrue(TEXT("and its sub-type"),
		Mode->RunDungeonSubType() == ECataclysmDungeonSubType::Elite);

	// THE LENGTH IS NEVER REPORTED BELOW THE FLOOR BEING WALKED. The stairs
	// descend for ever -- there is no bottom until issue #41 -- so a player can
	// stand on floor 90 of a dungeon set to 40. A floor ratio above one is
	// outside anything the model was fitted for.
	Mode->FloorNumber = 90;
	TestEqual(TEXT("walking past the end stretches the dungeon to reach"),
		Mode->RunTotalFloors(), 90);
	TestEqual(TEXT("so the deepest floor is the last floor"),
		Mode->ChooseTotalFloors(), Mode->ChooseFloorNumber());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyScoreKillingGrantsExperience,
	"Cataclysm.EnemyScore.KillingACreatureGrantsItsScoreAsExperience",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyScoreKillingGrantsExperience::RunTest(const FString&)
{
	// WHAT ISSUE #926 WAS ABOUT. The design says an enemy's Enemy Score IS the
	// experience it grants; this is the only test that checks a kill actually
	// hands one over.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	if (!TestNotNull(TEXT("player state"), State))
	{
		return false;
	}

	// SPENT THROUGH THE PLAYER STATE RATHER THAN BY KILLING A REAL CREATURE.
	// Standing a creature up, giving it a controller and a player to reach, and
	// killing it needs a possessed pawn, which a test world has no way to make.
	// What is checked here is that the number handed over is the creature's
	// score and that the character's level moves because of it.
	const FCataclysmScoredFloor Floor = UCataclysmEnemyScore::FloorIn(World);
	const int32 CommonWorth = UCataclysmEnemyScore::ScoreFor(Floor, 0);
	const int32 BossWorth = UCataclysmEnemyScore::ScoreFor(Floor, 4);

	TestTrue(TEXT("a Common is worth something"), CommonWorth > 0);
	TestTrue(TEXT("and a Boss is worth more"), BossWorth > CommonWorth);

	const int64 Before = State->GetExperienceIntoLevel();
	State->GrantExperience(CommonWorth);
	TestEqual(TEXT("killing a Common banks its score"),
		State->GetExperienceIntoLevel() - Before, static_cast<int64>(CommonWorth));

	State->GrantExperience(BossWorth);
	TestEqual(TEXT("and killing a Boss banks its own"),
		State->GetExperienceIntoLevel() - Before,
		static_cast<int64>(CommonWorth) + static_cast<int64>(BossWorth));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyScoreAWholeDungeonPaysALevel,
	"Cataclysm.EnemyScore.ClearingADungeonMovesTheCharactersLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyScoreAWholeDungeonPaysALevel::RunTest(const FString&)
{
	// THE TWO HALVES MEETING. `Cataclysm.Experience` checks the curve as
	// arithmetic and the tests above check what a creature is worth. Neither
	// notices if the two are on different scales -- a creature worth a thousand
	// times too little would pass both. This walks a whole dungeon's worth of
	// kills and checks the character gains roughly what the balance work said it
	// should.
	//
	// THE FIGURE IT IS CHECKED AGAINST comes from sim/analyse_experience_curve.py
	// and docs/DECISIONS.md: a 50-floor Basic dungeon at difficulty tier 1,
	// fully cleared, is worth 2,649,059, and level 2 costs 230,000. So one
	// dungeon at the shallowest tier is worth about eleven levels at the start
	// of the climb. Anything within a factor of two of that is the two halves
	// agreeing; a factor of a thousand is not.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	if (!TestNotNull(TEXT("player state"), State))
	{
		return false;
	}
	State->SetLevelAndExperience(1, 0);

	// 258 creatures a floor is the density measured over 1,000 seeds per layout
	// and recorded in docs/DECISIONS.md under 2026-08-22.
	const int32 CreaturesPerFloor = 258;
	const int32 TotalFloors = 50;

	int64 Whole = 0;
	for (int32 Which = 1; Which <= TotalFloors; ++Which)
	{
		FCataclysmScoredFloor Floor;
		Floor.DifficultyTier = 1;
		Floor.TotalFloors = TotalFloors;
		Floor.FloorNumber = Which;

		// Weighted by what actually spawns, from the SpawnWeight column of
		// game/Data/EnemyRarities.csv: Common 0.6, Elite 0.2, Legendary 0.15,
		// Herald 0.04, Boss 0.01.
		const double Weights[] = { 0.6, 0.2, 0.15, 0.04, 0.01, 0.0 };
		double PerCreature = 0.0;
		for (int32 Step = 0; Step < UCataclysmEnemyScore::RarityStepCount; ++Step)
		{
			PerCreature += Weights[Step]
				* static_cast<double>(UCataclysmEnemyScore::ScoreFor(Floor, Step));
		}
		Whole += static_cast<int64>(PerCreature * CreaturesPerFloor);
	}

	// The measured figure is 2,649,059. This is built from integer scores rather
	// than the model's floats, so it will not land on it exactly.
	TestTrue(FString::Printf(
			TEXT("a fully cleared tier 1 dungeon is worth %lld, and the balance "
				 "work measured 2,649,059"), Whole),
		Whole > 2400000 && Whole < 2900000);

	const int32 Gained = State->GrantExperience(Whole);
	TestTrue(FString::Printf(
			TEXT("clearing it takes a fresh character to level %d, and the "
				 "curve says about 11"), State->GetCharacterLevel()),
		Gained >= 8 && Gained <= 14);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyScoreAKillIsWhatGrantsIt,
	"Cataclysm.EnemyScore.KillingAnEnemyIsWhatGrantsTheExperience",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyScoreAKillIsWhatGrantsIt::RunTest(const FString&)
{
	// THE TEST ABOVE PROVES THE ARITHMETIC AND SAYS NOTHING ABOUT WHETHER A KILL
	// DOES IT. Without this one the whole award could be deleted from
	// `ACataclysmEnemyCharacter::HandleDeath` and every other test in this file
	// would still pass. It is the same separation
	// `Cataclysm.Drop.KillingAnEnemyIsWhatPutsLootOnTheFloor` exists for, and
	// this file follows it because the drop roll and the experience award happen
	// on the same death for the same reason.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();

	// A PLAYER CONTROLLER AND NOT A BARE AController, WHICH IS ABSTRACT.
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Player =
		World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player state"), PlayerState)
		|| !TestNotNull(TEXT("player controller"), Controller)
		|| !TestNotNull(TEXT("player character"), Player))
	{
		return false;
	}

	// AController::Possess AND NOT APawn::PossessedBy. Possess runs both halves:
	// the pawn learns its controller and the controller learns its pawn. A kill
	// finds the player through `GetFirstPlayerController`, and the award reads
	// the player state off that controller, so only the controller's half
	// matters here -- but possessing properly is what the neighbouring drop
	// tests do and copying half of it is how those went wrong once already.
	Controller->SetPlayerState(PlayerState);
	Controller->Possess(Player);

	PlayerState->SetLevelAndExperience(1, 0);
	const int64 Before = PlayerState->GetExperienceIntoLevel();

	// A BOSS, so the amount is large enough that nothing else could account for
	// it, and so this does not depend on where the sandbox's single floor sits.
	constexpr int32 BossStep = 4;
	const int32 Worth = UCataclysmEnemyScore::ScoreFor(
		UCataclysmEnemyScore::FloorIn(World), BossStep);
	TestTrue(TEXT("a Boss is worth something to begin with"), Worth > 0);

	ACataclysmEnemyCharacter* Victim =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature to kill"), Victim))
	{
		return false;
	}
	Victim->SetRarityStep(BossStep);
	Victim->HandleDeath();

	TestEqual(TEXT("killing it granted exactly its Enemy Score"),
		PlayerState->GetExperienceIntoLevel() - Before,
		static_cast<int64>(Worth));

	// AND A RARER CREATURE GRANTS MORE, which is what stops the award reading a
	// fixed number or the wrong creature's rarity.
	const int64 AfterBoss = PlayerState->GetExperienceIntoLevel();
	ACataclysmEnemyCharacter* Lesser =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(600.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a second creature"), Lesser))
	{
		return false;
	}
	Lesser->SetRarityStep(0);
	Lesser->HandleDeath();

	const int64 FromCommon = PlayerState->GetExperienceIntoLevel() - AfterBoss;
	TestTrue(FString::Printf(
			TEXT("a Common granted %lld, less than the Boss's %d"),
			FromCommon, Worth),
		FromCommon > 0 && FromCommon < Worth);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
