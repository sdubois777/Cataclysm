// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmEnemyScore.h"
#include "Dungeon/CataclysmPowerGapRewards.h"
#include "Empire/CataclysmCityUpgrade.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Player/CataclysmPowerScore.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A kill's experience and loot by the gap between the dungeon's score and the player's. Issue #2824, slice c: the
 * coordinating session's rule D and the owner's 25% loot floor, both of 2026-11-22, recorded in `docs/DECISIONS.md`.
 * `Cataclysm.PowerGapRewards.` IS A NEW GROUP and selects these four and nothing else.
 *
 * WRITTEN WITHOUT A BUILD. Phase 1: every figure below is the simulation's or reasoned from the code, and the run that
 * proves them is registered in `docs/DECISIONS.md` before it happens.
 */
namespace CataclysmPowerGapRewardsTest
{
	using Rule = FCataclysmPowerGapRule;

	FString Describe(const FCataclysmPowerGapRewards& Rewards)
	{
		return FString::Printf(TEXT("experience x%.4f, loot x%.4f, %s"), Rewards.Experience, Rewards.Loot,
							   Rewards.bScored ? TEXT("scored") : TEXT("not scored"));
	}

	/** A Basic dungeon of 10 floors on the run's first city, with its timer, entered; false after saying why. */
	bool EnterADungeon(FAutomationTestBase& Test, ACataclysmDungeonGameMode& Mode, UCataclysmEmpireRun& Run)
	{
		const FCataclysmCity& City = Run.Map->Cities[0];
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = Run.NextDungeonId;
		Dungeon.Type = ECataclysmDungeonType::Basic;
		Dungeon.CityId = City.CityId;
		Dungeon.CityTier = City.Tier;
		Dungeon.Floors = 10;
		Dungeon.ResolveDays = 40.0f;
		Dungeon.SpawnedDay = Run.Day();
		Run.NextDungeonId = Dungeon.DungeonId + 1;
		Run.Dungeons.Add(Dungeon);
		return Test.TestTrue(TEXT("set-up: its timer"), Run.Clock->AddDungeon(Dungeon.DungeonId, Dungeon.Floors)
								 && Run.Clock->SetResolveDays(Dungeon.DungeonId, Dungeon.ResolveDays))
			&& Test.TestTrue(TEXT("set-up: the dungeon was entered"), Mode.EnterEmpireDungeon(Dungeon.DungeonId));
	}

	/** A creature of this rung killed at this place, and the experience into the level it granted. */
	int64 KillAndMeasure(UWorld* World, ACataclysmPlayerState* State, int32 RarityStep, const FVector& At)
	{
		const int64 Before = State->GetExperienceIntoLevel();
		ACataclysmEnemyCharacter* Victim = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
		if (Victim == nullptr)
		{
			return INDEX_NONE;
		}
		Victim->SetRarityStep(RarityStep);
		Victim->HandleDeath();
		return State->GetExperienceIntoLevel() - Before;
	}

	constexpr int32 BossStep = 4;
}

// --------------------------------------------------------------------------------------------------------------------
// PG1. The rule is the simulation's at eleven gaps.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerGapRewardsRuleTest,
	"Cataclysm.PowerGapRewards.TheRuleIsTheSimulationsAtElevenGaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `sim/analyse_power_gap_rewards.py`'s `RULED.multiplier` at eleven gaps, experience and loot, each figure written out
 * here and checked against the simulation by `tools/tests/test_power_gap_rewards_port.py`, which reads this table back.
 * Two at each floor, one on each ramp, the band's two edges and its middle, and two at the cap. Prints "GAP RULE".
 */
bool FCataclysmPowerGapRewardsRuleTest::RunTest(const FString&)
{
	using namespace CataclysmPowerGapRewardsTest;
	struct FCase
	{
		double Gap;
		double Experience;
		double Loot;
	};
	// THE SIMULATION'S FIGURES. tools/tests/test_power_gap_rewards_port.py reads these rows back.
	const FCase Cases[] = {
		{ -2.0, 0.05, 0.25 },
		{ -1.6, 0.05, 0.25 },
		{ -1.1, 0.525, 0.625 },
		{ -0.85, 0.7625, 0.8125 },
		{ -0.6, 1.0, 1.0 },
		{ -0.3, 1.0, 1.0 },
		{ 0.0, 1.0, 1.0 },
		{ 0.25, 1.0, 1.0 },
		{ 0.5, 1.125, 1.125 },
		{ 0.75, 1.25, 1.25 },
		{ 1.0, 1.25, 1.25 },
	};
	TArray<FString> Said;
	for (const FCase& Case : Cases)
	{
		const double Experience = Rule::Multiplier(Case.Gap, /*bLoot=*/false);
		const double Loot = Rule::Multiplier(Case.Gap, /*bLoot=*/true);
		Said.Add(FString::Printf(TEXT("%+.2f: %.6f %.6f"), Case.Gap, Experience, Loot));
		TestEqual(*FString::Printf(TEXT("**at %+.2f, experience the simulation's %.4f**"), Case.Gap, Case.Experience),
				  Experience, Case.Experience, 1.0e-9);
		TestEqual(*FString::Printf(TEXT("**at %+.2f, loot the simulation's %.4f**"), Case.Gap, Case.Loot), Loot,
				  Case.Loot, 1.0e-9);
	}
	AddInfo(FString::Printf(TEXT("GAP RULE: %s"), *FString::Join(Said, TEXT(" | "))));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// PG2. Two scores give both factors and the line the screens show; no score changes nothing.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerGapRewardsScoresTest,
	"Cataclysm.PowerGapRewards.TwoScoresGiveBothFactorsAndTheLineAndNoScoreChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * At tier 4, whose width is 717, a Dungeon Score of 2,009 against five players, each row written out here and checked
 * against the simulation by `tools/tests/test_power_gap_rewards_port.py`: level with it, "Rewards 100%."; a whole width
 * under it, "Rewards 125%."; a width over it, "Rewards: experience 62%, loot 70%."; two widths over it, "Rewards:
 * experience 5%, loot 25%."; and an unknown player, not scored, both 1, no line. The experience figure 137 is 7 at 5%,
 * 171 at 125% and 137 at 1. Prints "GAP SCORES".
 */
bool FCataclysmPowerGapRewardsScoresTest::RunTest(const FString&)
{
	using namespace CataclysmPowerGapRewardsTest;
	struct FCase
	{
		int32 DungeonScore;
		int32 PlayerScore;
		int32 Tier;
		const TCHAR* Text;
	};
	// THE SIMULATION'S LINES. tools/tests/test_power_gap_rewards_port.py reads these rows back.
	const FCase Cases[] = {
		{ 2009, 2009, 4, TEXT("Rewards 100%.") },
		{ 2009, 1292, 4, TEXT("Rewards 125%.") },
		{ 2009, 2726, 4, TEXT("Rewards: experience 62%, loot 70%.") },
		{ 2009, 3443, 4, TEXT("Rewards: experience 5%, loot 25%.") },
	};
	TArray<FString> Said;
	for (const FCase& Case : Cases)
	{
		const FCataclysmPowerGapRewards Rewards = Rule::RewardsFor(Case.DungeonScore, Case.PlayerScore, Case.Tier);
		const FString Text = Rule::RewardsText(Rewards);
		const double Gap = Rule::GapOf(Case.DungeonScore, Case.PlayerScore, Case.Tier);
		Said.Add(FString::Printf(TEXT("%d against %d at tier %d: gap %+.4f, %s, \"%s\""), Case.DungeonScore,
								 Case.PlayerScore, Case.Tier, Gap, *Describe(Rewards), *Text));
		TestTrue(TEXT("scored"), Rewards.bScored);
		TestEqual(TEXT("the gap is the score difference over the tier's width"), Gap,
				  (Case.DungeonScore - Case.PlayerScore) / UCataclysmEnemyScore::TierWidth(Case.Tier), 1.0e-12);
		TestEqual(TEXT("experience is the rule's at that gap"), Rewards.Experience, Rule::Multiplier(Gap, false), 1.0e-12);
		TestEqual(TEXT("loot is the rule's at that gap"), Rewards.Loot, Rule::Multiplier(Gap, true), 1.0e-12);
		TestEqual(*FString::Printf(TEXT("**the line: %s**"), Case.Text), Text, FString(Case.Text));
	}

	const FCataclysmPowerGapRewards Unknown = Rule::RewardsFor(2009, INDEX_NONE, 4);
	const FCataclysmPowerGapRewards NoDungeon = Rule::RewardsFor(INDEX_NONE, 2009, 4);
	const FCataclysmPowerGapRewards AtTheFloor = Rule::RewardsFor(2009, 3443, 4);
	const FCataclysmPowerGapRewards AtTheCap = Rule::RewardsFor(2009, 1292, 4);
	AddInfo(FString::Printf(TEXT("GAP SCORES: %s; tier 4's width %.1f; no player %s; no dungeon %s; 137 becomes %d at the "
								 "floor, %d at the cap, %d with nothing"),
							*FString::Join(Said, TEXT(" | ")), UCataclysmEnemyScore::TierWidth(4), *Describe(Unknown),
							*Describe(NoDungeon), AtTheFloor.ExperienceFrom(137), AtTheCap.ExperienceFrom(137),
							Unknown.ExperienceFrom(137)));

	TestEqual(TEXT("set-up: tier 4's width is 717, the simulation's"), UCataclysmEnemyScore::TierWidth(4), 717.0, 1.0e-9);
	TestFalse(TEXT("**no player score: not scored**"), Unknown.bScored);
	TestTrue(TEXT("**and both factors exactly 1**"), Unknown.Experience == 1.0 && Unknown.Loot == 1.0);
	TestTrue(TEXT("**and no line**"), Rule::RewardsText(Unknown).IsEmpty());
	TestFalse(TEXT("no Dungeon Score: not scored either"), NoDungeon.bScored);
	TestEqual(TEXT("137 at 5%: 7"), AtTheFloor.ExperienceFrom(137), 7);
	TestEqual(TEXT("137 at 125%: 171"), AtTheCap.ExperienceFrom(137), 171);
	TestEqual(TEXT("137 at 1: 137"), Unknown.ExperienceFrom(137), 137);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// PG3. A kill in an empire dungeon grants the factor's experience, and the floor panel's line shows it.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerGapRewardsKillTest,
	"Cataclysm.PowerGapRewards.AKillInAnEmpireDungeonGrantsTheFactorsExperienceAndThePanelShowsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A possessed level 99 player wearing nothing, in a 10-floor Basic dungeon of the empire: its Power Score is more than
 * 0.6 of a tier width above the dungeon's, so the rule cuts. A Boss killed there grants the creature's figure times the
 * experience factor, rounded, through the player's own gain, and the loot factor is above the experience factor. The
 * two scores are the floor panel's, and the panel's line is the scores' line and then the rewards'. CONTROL: with the
 * dungeon left, nothing is scored and the same kill grants the whole figure. Prints "GAP KILL".
 */
bool FCataclysmPowerGapRewardsKillTest::RunTest(const FString&)
{
	using namespace CataclysmPowerGapRewardsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("set-up: the dungeon game mode"), Mode))
	{
		return false;
	}
	Mode->StartPlay();
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->Begin(2824);
	Run->AdvanceDay();
	Run->Dungeons.Empty();
	Run->Clock->Timers.Empty();
	Mode->SetEmpireRunForTests(Run);

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Player =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestTrue(TEXT("set-up: a player state, a controller and a character"), State && Controller && Player))
	{
		return false;
	}
	// POSSESSED, so a kill and the scores find this player through `GetFirstPlayerController`, as in play.
	Controller->SetPlayerState(State);
	Controller->Possess(Player);
	State->SetLevelAndExperience(99, 0);
	Player->ApplyChosenClassStats();

	const FCataclysmPowerGapRewards Unbound = ACataclysmDungeonGameMode::PowerGapRewardsIn(World);
	if (!EnterADungeon(*this, *Mode, *Run))
	{
		return false;
	}
	int32 DungeonScore = INDEX_NONE;
	int32 PlayerScore = INDEX_NONE;
	int32 Tier = 0;
	const bool bScores = Mode->GapScoresHere(DungeonScore, PlayerScore, Tier);
	const FCataclysmPowerGapRewards Here = ACataclysmDungeonGameMode::PowerGapRewardsIn(World);
	const FString Line = Mode->FloorScoreLineHere();
	const FString ScoresPart = UCataclysmEmpireMapLayout::DungeonScoreText(DungeonScore, PlayerScore);
	const FString RewardsPart = FCataclysmPowerGapRule::RewardsText(Here);

	// THE KILL, priced as the death handler prices it: the creature's figure, the city's "more" (none on this run's
	// city, asked rather than assumed), the gap's factor, the player's own gain.
	const int32 Worth = UCataclysmEnemyScore::ScoreFor(UCataclysmEnemyScore::FloorIn(World), BossStep);
	const FCataclysmCityDungeonRewards City = ACataclysmDungeonGameMode::CityRewardsIn(World);
	const int64 Expected = State->ExperienceAfterGain(Here.ExperienceFrom(City.ExperienceFrom(Worth)));
	const int64 Whole = State->ExperienceAfterGain(City.ExperienceFrom(Worth));
	const int64 Gained = KillAndMeasure(World, State, BossStep, FVector(300.0f, 0.0f, 0.0f));
	const int32 LevelAfterTheCut = State->GetCharacterLevel();

	// CONTROL: the dungeon left, the same kill.
	Mode->LeaveEmpireDungeon();
	const FCataclysmPowerGapRewards Left = ACataclysmDungeonGameMode::PowerGapRewardsIn(World);
	const int32 WorthOutside = UCataclysmEnemyScore::ScoreFor(UCataclysmEnemyScore::FloorIn(World), BossStep);
	const int64 ExpectedOutside =
		State->ExperienceAfterGain(ACataclysmDungeonGameMode::CityRewardsIn(World).ExperienceFrom(WorthOutside));
	const int64 GainedOutside = KillAndMeasure(World, State, BossStep, FVector(-300.0f, 0.0f, 0.0f));

	AddInfo(FString::Printf(TEXT("GAP KILL: before entering %s; in the dungeon at tier %d the Dungeon Score %d, the player's "
								 "%d (live %d), gap %+.4f, %s; the line \"%s\"; a Boss worth %d granted %lld of %lld "
								 "expected (the whole figure %lld), level %d after; left, %s, worth %d granted %lld of %lld"),
							*Describe(Unbound), Tier, DungeonScore, PlayerScore, UCataclysmPowerScore::LiveScoreOf(Player),
							FCataclysmPowerGapRule::GapOf(DungeonScore, PlayerScore, Tier), *Describe(Here), *Line, Worth,
							Gained, Expected, Whole, LevelAfterTheCut, *Describe(Left), WorthOutside, GainedOutside,
							ExpectedOutside));

	TestFalse(TEXT("before entering: not scored"), Unbound.bScored);
	TestTrue(TEXT("set-up: inside, the game mode has both scores"), bScores && DungeonScore != INDEX_NONE
				 && PlayerScore != INDEX_NONE);
	TestEqual(TEXT("**the player's score is its live Power Score**"), PlayerScore, UCataclysmPowerScore::LiveScoreOf(Player));
	TestEqual(TEXT("**the dungeon's is its Dungeon Score on this floor**"), DungeonScore,
			  UCataclysmEnemyScore::DungeonScoreFor(UCataclysmEnemyScore::FloorFor(Mode, Tier)));
	TestTrue(TEXT("**scored**"), Here.bScored);
	TestTrue(TEXT("set-up: a level 99 player is more than 0.6 of a width above a 10-floor Basic dungeon, so the rule cuts"),
			 Here.Experience < 1.0);
	TestTrue(TEXT("**and loot is cut less than experience**"), Here.Loot > Here.Experience);
	TestTrue(TEXT("set-up: a Boss is worth something here"), Worth > 0);
	TestEqual(TEXT("set-up: the kill does not cross a level"), LevelAfterTheCut, 99);
	TestEqual(TEXT("**the kill grants the figure times the experience factor**"), Gained, Expected);
	TestTrue(TEXT("**and less than the whole figure**"), Gained < Whole);
	TestEqual(TEXT("**the panel's line is the scores' line and then the rewards'**"), Line,
			  ScoresPart + TEXT(" ") + RewardsPart);
	TestTrue(TEXT("and the rewards say both factors, as they differ"), RewardsPart.StartsWith(TEXT("Rewards: experience ")));
	TestFalse(TEXT("CONTROL: the dungeon left, not scored"), Left.bScored);
	TestEqual(TEXT("**CONTROL: and the same kill grants the whole figure**"), GainedOutside, ExpectedOutside);
	TestTrue(TEXT("CONTROL: the panel has no line with no dungeon bound"), Mode->FloorScoreLineHere().IsEmpty());
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// PG4. The empire map and the city screen show the rewards beside the Dungeon Score.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerGapRewardsMapLineTest,
	"Cataclysm.PowerGapRewards.TheMapAndCityScreenShowTheRewardsBesideTheDungeonScore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A 22-floor Basic Elite dungeon on a run's first city: with a player's score known, the line the empire map's city
 * detail and the city screen show is `DungeonScoreLine`'s and then the rewards at the run's difficulty tier; with none
 * it is `DungeonScoreLine`'s alone; for a dungeon not on the map it is empty. Prints "GAP MAP LINE".
 */
bool FCataclysmPowerGapRewardsMapLineTest::RunTest(const FString&)
{
	using namespace CataclysmPowerGapRewardsTest;
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->Begin(2824);
	if (!TestNotNull(TEXT("set-up: the run has a map"), Run->Map.Get())
		|| !TestNotNull(TEXT("set-up: and a clock"), Run->Clock.Get()))
	{
		return false;
	}
	FCataclysmDungeon Dungeon;
	Dungeon.DungeonId = 961;
	Dungeon.CityId = Run->Map->Cities[0].CityId;
	Dungeon.Floors = 22;
	Dungeon.ResolveDays = 9.0f;
	Dungeon.Type = ECataclysmDungeonType::Basic;
	Dungeon.SubType = ECataclysmDungeonSubType::Elite;
	Run->Dungeons.Add(Dungeon);
	Run->Clock->AddDungeon(961, 22);
	Run->Clock->SetResolveDays(961, 9.0f);

	const int32 DungeonScore = UCataclysmEnemyScore::DungeonScoreOf(Run, 961);
	// A PLAYER TWO WIDTHS ABOVE IT, so the line says both floors whatever the dungeon scores.
	const int32 PlayerScore = DungeonScore + static_cast<int32>(2.0 * UCataclysmEnemyScore::TierWidth(Run->DifficultyTier));
	const FString Line = UCataclysmEmpireMapLayout::DungeonScoreAndRewardsLine(Run, 961, PlayerScore);
	const FString Unknown = UCataclysmEmpireMapLayout::DungeonScoreAndRewardsLine(Run, 961, INDEX_NONE);
	const FString Missing = UCataclysmEmpireMapLayout::DungeonScoreAndRewardsLine(Run, 999, PlayerScore);
	AddInfo(FString::Printf(TEXT("GAP MAP LINE: tier %d, Dungeon Score %d, player %d; \"%s\" | \"%s\" | \"%s\""),
							Run->DifficultyTier, DungeonScore, PlayerScore, *Line, *Unknown, *Missing));

	TestTrue(TEXT("set-up: the dungeon is scored"), DungeonScore != INDEX_NONE);
	TestEqual(TEXT("**the line is the scores' and then the rewards'**"), Line,
			  UCataclysmEmpireMapLayout::DungeonScoreLine(Run, 961, PlayerScore) + TEXT(" ")
				  + FCataclysmPowerGapRule::RewardsText(
					  FCataclysmPowerGapRule::RewardsFor(DungeonScore, PlayerScore, Run->DifficultyTier)));
	TestTrue(TEXT("**two widths above: both floors**"), Line.EndsWith(TEXT(" Rewards: experience 5%, loot 25%.")));
	TestEqual(TEXT("with no player score, the scores' line alone"), Unknown,
			  UCataclysmEmpireMapLayout::DungeonScoreLine(Run, 961, INDEX_NONE));
	TestTrue(TEXT("CONTROL: a dungeon not on the map gives no line"), Missing.IsEmpty());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
