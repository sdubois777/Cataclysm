// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSurge.h"

/**
 * Which Cataclysms a campaign faces, and the rule that opens the Cataclysm
 * dungeon. Issue #1357.
 *
 * WHAT THESE PIN AND WHAT `tools/tests/test_the_cataclysm_dungeon_unlocks_at_half.py`
 * PINS. These check the behaviour: that a character's draw is a permutation of
 * all eight, that the same seed meets the same Cataclysms, that clearing a Quest
 * dungeon advances the Cataclysm that sent it and no other, that the boss opens
 * at half rounded up. The Python test checks the numbers still match
 * `sim/cataclysm_sim/config.py` and `docs/Cataclysm_GDD_v2.md`. Neither can
 * notice what the other does, which is the arrangement every other port in this
 * module is in.
 *
 * **THE ODD ACTIVE COUNTS ARE WHERE THE ROUNDING IS ACTUALLY TESTED.** The
 * project owner's worked examples were 3, 4, 5, 7 and 8, and floor and ceiling
 * agree at 4 and at 8 -- so a test built only from the even examples passes just
 * as happily on `N / 2`, which is wrong at three of the five.
 * `Cataclysm.Roster.HalfRoundsUpAtEveryOddCount` is the one that separates them
 * and it says so in its own body.
 */

namespace CataclysmRosterTest
{
	UCataclysmEmpireRun* MakeRun(int32 Seed, int32 DifficultyTier)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(Seed, ECataclysmSurgeMode::Static, /* LethalityRung */ 0,
				   DifficultyTier);
		return Run;
	}

	/**
	 * Puts a cleared Quest dungeon belonging to one Cataclysm through the live
	 * path.
	 *
	 * THROUGH `ClearDungeon` AND NOT BY SETTING THE TALLY, which is the whole
	 * point: the thing under test is that the run reads
	 * `FCataclysmDungeon::Cataclysm` off the dungeon and credits the right
	 * Cataclysm. Writing the tally by hand would test the tally.
	 *
	 * IT DOES NOT GO THROUGH THE CLOCK. `ClearDungeon` calls `RemoveDungeon`,
	 * which takes the dungeon out of both lists and does not mind that no timer
	 * was ever started for it.
	 */
	void ClearOneQuestDungeonFor(UCataclysmEmpireRun& Run,
								 ECataclysmType Cataclysm)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = Run.NextDungeonId++;
		Dungeon.Type = ECataclysmDungeonType::Quest;
		Dungeon.Cataclysm = Cataclysm;
		Dungeon.CityId = Run.Map->Cities[0].CityId;

		Run.Dungeons.Add(Dungeon);
		Run.ClearDungeon(Dungeon.DungeonId);
	}

	/** Clears exactly as many as this Cataclysm asks for. */
	void FinishOneCataclysm(UCataclysmEmpireRun& Run, ECataclysmType Cataclysm)
	{
		const int32 Needed = UCataclysmRoster::QuestObjectivesFor(Cataclysm);
		for (int32 Index = 0; Index < Needed; ++Index)
		{
			ClearOneQuestDungeonFor(Run, Cataclysm);
		}
	}
}

// ---------------------------------------------------------------------------
// The requirement -- half, rounded up
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRosterOwnersExamplesTest,
	"Cataclysm.Roster.TheRequirementIsTheOwnersOwnWorkedTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRosterOwnersExamplesTest::RunTest(const FString& Parameters)
{
	// EVERY EXAMPLE THE OWNER GAVE, quoted from the ruling of 2026-09-06: "if
	// you're facing 4, you have to complete 2 quests, 8 would be 4. For odd
	// numbers do something like 3 you need 2, 5 you need 3, 7 you need 4?"
	const TMap<int32, int32> Table = {
		{ 3, 2 }, { 4, 2 }, { 5, 3 }, { 7, 4 }, { 8, 4 },
	};

	for (const TPair<int32, int32>& Row : Table)
	{
		TestEqual(FString::Printf(
			TEXT("facing %d Cataclysms asks for %d"), Row.Key, Row.Value),
			UCataclysmRoster::CataclysmsRequiredFor(Row.Key), Row.Value);
	}

	// AND THE TWO COUNTS THE OWNER DID NOT WORK THROUGH. One is the one the
	// design already describes -- a single Cataclysm, where half rounded down
	// would be none and the boss would open before the player had cleared
	// anything.
	TestEqual(TEXT("one Cataclysm asks for one, not none"),
			  UCataclysmRoster::CataclysmsRequiredFor(1), 1);
	TestEqual(TEXT("two Cataclysms ask for one"),
			  UCataclysmRoster::CataclysmsRequiredFor(2), 1);
	TestEqual(TEXT("six Cataclysms ask for three"),
			  UCataclysmRoster::CataclysmsRequiredFor(6), 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRosterHalfRoundsUpTest,
	"Cataclysm.Roster.HalfRoundsUpAtEveryOddCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRosterHalfRoundsUpTest::RunTest(const FString& Parameters)
{
	// **THE ONLY TEST HERE THAT CAN TELL CEILING FROM FLOOR.** At 4 and at 8 --
	// two of the five worked examples the owner gave -- `N / 2` and
	// `(N + 1) / 2` are the same number, so the test above passes on both
	// implementations. These four counts are where they differ, and the second
	// assertion in each pair is what makes this a comparison rather than a
	// restatement of the first.
	for (const int32 Active : { 1, 3, 5, 7 })
	{
		const int32 Required = UCataclysmRoster::CataclysmsRequiredFor(Active);

		TestEqual(FString::Printf(
			TEXT("%d Cataclysms round UP to %d"), Active, (Active + 1) / 2),
			Required, (Active + 1) / 2);

		TestNotEqual(FString::Printf(
			TEXT("and %d is not the %d that rounding DOWN would give"),
			Active, Active / 2),
			Required, Active / 2);
	}

	// AND ROUNDING UP IS NOT SIMPLY "ONE MORE THAN HALF". At the even counts it
	// must agree with the floor, or the rule would ask for 3 of 4 rather than
	// the 2 the owner stated.
	for (const int32 Active : { 2, 4, 6, 8 })
	{
		TestEqual(FString::Printf(
			TEXT("%d Cataclysms ask for exactly half"), Active),
			UCataclysmRoster::CataclysmsRequiredFor(Active), Active / 2);
	}

	return true;
}

// ---------------------------------------------------------------------------
// What each Cataclysm asks for
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRosterObjectiveCountsTest,
	"Cataclysm.Roster.EachCataclysmAsksForItsOwnNumberOfQuestDungeons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRosterObjectiveCountsTest::RunTest(const FString& Parameters)
{
	// `docs/Cataclysm_GDD_v2.md` SECTION XI, all eight. The owner was asked
	// whether to keep them or settle on one number and answered "Keep the
	// per-Cataclysm numbers".
	const TMap<ECataclysmType, int32> Expected = {
		{ ECataclysmType::Demonic,    10 },
		{ ECataclysmType::Death,       5 },
		{ ECataclysmType::War,        10 },
		{ ECataclysmType::Pestilence,  5 },
		{ ECataclysmType::Famine,      5 },
		{ ECataclysmType::Celestial,  10 },
		{ ECataclysmType::Chaos,       8 },
		{ ECataclysmType::Void,        5 },
	};

	for (const TPair<ECataclysmType, int32>& Row : Expected)
	{
		TestEqual(FString::Printf(TEXT("%s asks for %d"),
								  *UCataclysmRoster::NameFor(Row.Key).ToString(),
								  Row.Value),
				  UCataclysmRoster::QuestObjectivesFor(Row.Key), Row.Value);
	}

	// **THEY ARE NOT ALL THE SAME, AND THAT IS THE DESIGN.** Without this the
	// table above would pass on an implementation that returned one flat number
	// if the flat number happened to be right for some of them -- and a flat 8
	// is exactly what the model held until this issue.
	TestNotEqual(TEXT("Death asks for less than Demonic"),
				 UCataclysmRoster::QuestObjectivesFor(ECataclysmType::Death),
				 UCataclysmRoster::QuestObjectivesFor(ECataclysmType::Demonic));

	// `None` IS NOT A CATACLYSM AND ASKS FOR NOTHING.
	TestEqual(TEXT("an unassigned dungeon's Cataclysm asks for nothing"),
			  UCataclysmRoster::QuestObjectivesFor(ECataclysmType::None), 0);

	return true;
}

// ---------------------------------------------------------------------------
// Which Cataclysms a character faces
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRosterOrderIsAPermutationTest,
	"Cataclysm.Roster.ACharactersOrderHoldsAllEightCataclysmsExactlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRosterOrderIsAPermutationTest::RunTest(const FString& Parameters)
{
	for (int32 Seed = 0; Seed < 200; ++Seed)
	{
		const TArray<ECataclysmType> Order = UCataclysmRoster::OrderFor(Seed);

		TestEqual(FString::Printf(TEXT("seed %d draws eight"), Seed),
				  Order.Num(), UCataclysmRoster::Count);

		// A SHUFFLE THAT DROPPED OR DUPLICATED AN ENTRY WOULD STILL BE EIGHT
		// LONG if it duplicated one and lost another, which is exactly what a
		// Fisher-Yates written with the wrong bound does.
		TSet<ECataclysmType> Seen(Order);
		TestEqual(FString::Printf(TEXT("seed %d draws eight DIFFERENT ones"),
								  Seed),
				  Seen.Num(), UCataclysmRoster::Count);

		TestFalse(FString::Printf(
			TEXT("and seed %d does not draw the unassigned value"), Seed),
			Seen.Contains(ECataclysmType::None));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRosterSeedIsTheCharacterTest,
	"Cataclysm.Roster.TheSameSeedMeetsTheSameCataclysmsAndAHigherTierAddsToThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRosterSeedIsTheCharacterTest::RunTest(const FString& Parameters)
{
	// THE PROJECT OWNER'S RULING OF 2026-09-06, verbatim: "if they are on t3
	// with demonic/war/death they restart with those same cataclysms". Replaying
	// is re-running the seed, so the seed has to decide the set.
	for (int32 Seed = 0; Seed < 50; ++Seed)
	{
		const TArray<ECataclysmType> Order = UCataclysmRoster::OrderFor(Seed);

		// **THE ACTIVE SET IS A PREFIX OF THE DRAW AND NOT A SECOND DRAW.** That
		// is the whole reason a character's Cataclysms survive a restart: a
		// separate roll per tier would give a replay a different world even from
		// the same seed. Asserting that `OrderFor` returns the same thing twice
		// would prove only that a pure function is pure.
		for (int32 Tier = 1; Tier <= 8; ++Tier)
		{
			const TArray<ECataclysmType> Active =
				UCataclysmRoster::ActiveFor(Seed, Tier);

			for (int32 Index = 0; Index < Tier; ++Index)
			{
				TestEqual(FString::Printf(
					TEXT("seed %d's tier %d set is the first %d of its draw"),
					Seed, Tier, Tier), Active[Index], Order[Index]);
			}
		}

		// AND CLIMBING A TIER ADDS ONE RATHER THAN RE-ROLLING THE WORLD. That is
		// one character getting further, not two unrelated campaigns.
		for (int32 Tier = 1; Tier < 8; ++Tier)
		{
			const TArray<ECataclysmType> Lower =
				UCataclysmRoster::ActiveFor(Seed, Tier);
			const TArray<ECataclysmType> Higher =
				UCataclysmRoster::ActiveFor(Seed, Tier + 1);

			TestEqual(FString::Printf(
				TEXT("seed %d faces %d Cataclysms at tier %d"),
				Seed, Tier, Tier), Lower.Num(), Tier);

			for (const ECataclysmType Cataclysm : Lower)
			{
				TestTrue(FString::Printf(
					TEXT("seed %d keeps %s when it climbs past tier %d"),
					Seed, *UCataclysmRoster::NameFor(Cataclysm).ToString(),
					Tier),
					Higher.Contains(Cataclysm));
			}
		}
	}

	// THE CLAMP AT BOTH ENDS. A tier this table has no ninth Cataclysm for gets
	// all eight, and a tier of nothing still faces one.
	TestEqual(TEXT("tier 0 still faces one Cataclysm"),
			  UCataclysmRoster::ActiveCountFor(0), 1);
	TestEqual(TEXT("tier 9 faces the eight that exist"),
			  UCataclysmRoster::ActiveCountFor(9), 8);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRosterAdjacentSeedsTest,
	"Cataclysm.Roster.AdjacentSeedsDrawUnrelatedOrders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRosterAdjacentSeedsTest::RunTest(const FString& Parameters)
{
	// WHY THIS IS WORTH A TEST. `FRandomStream` is a linear congruential
	// generator, so two streams initialised one apart produce closely related
	// first draws. `UCataclysmRoster::MixedSeed` exists to break that, and
	// without it the first Cataclysm of seed N and of seed N+1 would march in
	// step -- every character in a sweep block facing nearly the same campaign.
	int32 SameFirst = 0;
	TSet<ECataclysmType> Firsts;

	for (int32 Seed = 0; Seed < 400; ++Seed)
	{
		const ECataclysmType First = UCataclysmRoster::OrderFor(Seed)[0];
		const ECataclysmType Next = UCataclysmRoster::OrderFor(Seed + 1)[0];

		Firsts.Add(First);
		if (First == Next)
		{
			++SameFirst;
		}
	}

	// EVERY CATACLYSM IS SOMEBODY'S FIRST. A draw that always began with the
	// same one would be no draw at all, which is the state the model was in
	// before issue #1338: every campaign it ever ran faced Demonic first.
	TestEqual(TEXT("all eight are drawn first by some character"),
			  Firsts.Num(), UCataclysmRoster::Count);

	// ONE IN EIGHT NEIGHBOURING PAIRS SHARE A FIRST BY CHANCE, so 50 of 400 is
	// the expectation. A generator that had not been mixed would give 400.
	TestTrue(FString::Printf(
		TEXT("neighbouring seeds share a first Cataclysm %d times in 400, "
			 "which is near the 50 chance alone gives"), SameFirst),
		SameFirst < 120);

	return true;
}

// ---------------------------------------------------------------------------
// A run counting objectives per Cataclysm
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRosterRunKnowsItsCataclysmsTest,
	"Cataclysm.Roster.ARunFacesAsManyCataclysmsAsItsDifficultyTier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRosterRunKnowsItsCataclysmsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRosterTest;

	for (int32 Tier = 1; Tier <= 8; ++Tier)
	{
		UCataclysmEmpireRun* Run = MakeRun(/* Seed */ 20 + Tier, Tier);

		TestEqual(FString::Printf(TEXT("a tier %d run faces %d"), Tier, Tier),
				  Run->ActiveCataclysms.Num(), Tier);

		// AND EVERY ONE OF THEM IS A KEY BEFORE ANYTHING HAS BEEN CLEARED, so a
		// Cataclysm that never sends a quest dungeon still reads as zero rather
		// than as missing.
		TestEqual(FString::Printf(
			TEXT("and holds a tally for each of the %d"), Tier),
			Run->QuestObjectivesByCataclysm.Num(), Tier);

		for (const ECataclysmType Cataclysm : Run->ActiveCataclysms)
		{
			TestEqual(TEXT("which starts at nothing"),
					  Run->QuestObjectivesFor(Cataclysm), 0);
		}

		TestEqual(FString::Printf(
			TEXT("a tier %d run needs %d Cataclysms finished"), Tier,
			(Tier + 1) / 2),
			Run->CataclysmDungeonRequirement(), (Tier + 1) / 2);

		TestFalse(TEXT("and the Cataclysm dungeon is shut to begin with"),
				  Run->IsCataclysmDungeonUnlocked());
	}

	// A RUN THAT NEVER BEGAN FACES NOTHING AND IS NOT UNLOCKED. Half of nothing
	// must not be a win.
	UCataclysmEmpireRun* Unstarted = NewObject<UCataclysmEmpireRun>();
	TestFalse(TEXT("a run that never began has not unlocked anything"),
			  Unstarted->IsCataclysmDungeonUnlocked());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRosterObjectiveGoesToItsSenderTest,
	"Cataclysm.Roster.AClearedQuestDungeonCountsForTheCataclysmThatSentIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRosterObjectiveGoesToItsSenderTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRosterTest;

	// FOUR ACTIVE, so there is somewhere for a miscounted objective to go. With
	// one active Cataclysm every credit lands on it whether the code reads the
	// dungeon or not, and the test would prove nothing.
	UCataclysmEmpireRun* Run = MakeRun(/* Seed */ 77, /* DifficultyTier */ 4);

	const ECataclysmType Credited = Run->ActiveCataclysms[0];
	const ECataclysmType Other = Run->ActiveCataclysms[1];

	ClearOneQuestDungeonFor(*Run, Credited);
	ClearOneQuestDungeonFor(*Run, Credited);
	ClearOneQuestDungeonFor(*Run, Other);

	TestEqual(TEXT("the Cataclysm that sent two got two"),
			  Run->QuestObjectivesFor(Credited), 2);
	TestEqual(TEXT("the one that sent one got one"),
			  Run->QuestObjectivesFor(Other), 1);

	for (int32 Index = 2; Index < Run->ActiveCataclysms.Num(); ++Index)
	{
		TestEqual(TEXT("and the ones that sent nothing got nothing"),
				  Run->QuestObjectivesFor(Run->ActiveCataclysms[Index]), 0);
	}

	// THE RUN'S TOTAL IS STILL THE TOTAL. It is what the empire screen shows and
	// it is deliberately kept; what it cannot do is answer the unlock rule.
	TestEqual(TEXT("and the run's own total is all three"),
			  Run->QuestObjectives, 3);

	// **AN ORDINARY DUNGEON EARNS NOTHING EVEN THOUGH IT NAMES A CATACLYSM.**
	// Every dungeon a surge lands carries one, so crediting on the field alone
	// rather than on the field AND the kind would pay the player for clearing
	// anything at all.
	FCataclysmDungeon Basic;
	Basic.DungeonId = Run->NextDungeonId++;
	Basic.Type = ECataclysmDungeonType::Basic;
	Basic.Cataclysm = Credited;
	Basic.CityId = Run->Map->Cities[0].CityId;
	Run->Dungeons.Add(Basic);
	Run->ClearDungeon(Basic.DungeonId);

	TestEqual(TEXT("clearing an ordinary dungeon earns its Cataclysm nothing"),
			  Run->QuestObjectivesFor(Credited), 2);

	// AND A QUEST DUNGEON NOBODY SENT RAISES THE TOTAL AND NO CATACLYSM. That is
	// the one case where the total and the sum of the map come apart, and it is
	// reachable: a dungeon built by hand carries `None`.
	ClearOneQuestDungeonFor(*Run, ECataclysmType::None);

	TestEqual(TEXT("an unsent quest dungeon still raises the run's total"),
			  Run->QuestObjectives, 4);
	TestFalse(TEXT("but adds no key for the unassigned value"),
			  Run->QuestObjectivesByCataclysm.Contains(ECataclysmType::None));

	return true;
}

// ---------------------------------------------------------------------------
// The gate
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRosterUnlockAtHalfTest,
	"Cataclysm.Roster.TheCataclysmDungeonOpensAtHalfTheActiveCataclysms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRosterUnlockAtHalfTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRosterTest;

	// EVERY ACTIVE COUNT, THE ODD ONES INCLUDED, played out through the live
	// path rather than asserted about the arithmetic. Tier 5 is where a floor
	// would open the boss one Cataclysm early.
	for (int32 Tier = 1; Tier <= 8; ++Tier)
	{
		UCataclysmEmpireRun* Run = MakeRun(/* Seed */ 400 + Tier, Tier);

		const int32 Required = Run->CataclysmDungeonRequirement();

		TestEqual(FString::Printf(
			TEXT("tier %d asks for half of %d rounded up"), Tier, Tier),
			Required, (Tier + 1) / 2);

		for (int32 Finished = 0; Finished < Required; ++Finished)
		{
			// ONE SHORT OF THE REQUIREMENT IS STILL SHUT, checked before each
			// step rather than only once, so an implementation that opened at
			// the first finished Cataclysm fails here at every tier above 2.
			TestFalse(FString::Printf(
				TEXT("tier %d is shut with %d of %d finished"),
				Tier, Finished, Required),
				Run->IsCataclysmDungeonUnlocked());

			TestEqual(FString::Printf(
				TEXT("and reports %d finished"), Finished),
				Run->CataclysmsComplete(), Finished);

			FinishOneCataclysm(*Run, Run->ActiveCataclysms[Finished]);
		}

		TestTrue(FString::Printf(
			TEXT("tier %d opens once %d are finished"), Tier, Required),
			Run->IsCataclysmDungeonUnlocked());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRosterCountsAreNotTotalsTest,
	"Cataclysm.Roster.ATotalOfObjectivesIsNotEnoughToOpenTheCataclysmDungeon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRosterCountsAreNotTotalsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRosterTest;

	// **THE CASE THE OLD RULE GOT WRONG, AND THE REASON THE COUNTER HAD TO
	// BECOME PER-CATACLYSM.** A player facing four Cataclysms who clears twenty
	// quest dungeons all belonging to one of them has finished one Cataclysm and
	// the rule asks for two. The run's total says twenty; the gate must still be
	// shut.
	UCataclysmEmpireRun* Run = MakeRun(/* Seed */ 909, /* DifficultyTier */ 4);

	const ECataclysmType Only = Run->ActiveCataclysms[0];

	for (int32 Index = 0; Index < 20; ++Index)
	{
		ClearOneQuestDungeonFor(*Run, Only);
	}

	TestEqual(TEXT("the run's total is twenty"), Run->QuestObjectives, 20);
	TestEqual(TEXT("but only one Cataclysm is finished"),
			  Run->CataclysmsComplete(), 1);
	TestEqual(TEXT("against a requirement of two"),
			  Run->CataclysmDungeonRequirement(), 2);
	TestFalse(TEXT("so the Cataclysm dungeon is still shut"),
			  Run->IsCataclysmDungeonUnlocked());

	// AND FINISHING A SECOND ONE OPENS IT, however few dungeons that took. Death
	// asks for five where Demonic asks for ten, so the second Cataclysm can cost
	// half what the first did -- which is the design, not a loophole.
	FinishOneCataclysm(*Run, Run->ActiveCataclysms[1]);

	TestTrue(TEXT("finishing a second Cataclysm opens it"),
			  Run->IsCataclysmDungeonUnlocked());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRosterWaveIsStampedTest,
	"Cataclysm.Roster.EveryDungeonASurgeLandsNamesTheCataclysmThatSentIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRosterWaveIsStampedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRosterTest;

	// FOUR ACTIVE AND LONG ENOUGH FOR CITIES TO FALL, so the draw has room to
	// produce more than one answer AND the run builds some Fallen City dungeons,
	// which are the kind nobody sends. Nothing is cleared, so this is the
	// unattended empire `Cataclysm.EmpireRun.AnUnattendedEmpireAlwaysLosesThe
	// PathToThePillar` describes, and it uses the same 2000-day horizon.
	UCataclysmEmpireRun* Run = MakeRun(/* Seed */ 31, /* DifficultyTier */ 4);

	// **EVERY DUNGEON THAT EVER STOOD, AND NOT THE ONES LEFT STANDING AT THE
	// END.** This test asked `Run->Dungeons` once, after 2000 days, and found
	// nothing a surge had landed: `CityFell` removes every dungeon standing on a
	// city it takes and leaves one Fallen City dungeon in their place, so by the
	// time an unattended empire has lost every city, every dungeon on the board
	// is the kind nobody sends. The two controls below caught it -- they read
	// "0 dungeons were sent by somebody" -- which is what they are for.
	//
	// SO THE BOARD IS READ EVERY DAY AND WHAT IT HELD IS ACCUMULATED. A dungeon
	// is examined the first day it appears and never again, which is what the
	// test's own name asks about: what a surge LANDS, not what survives. It is
	// the same correction `analyse_siege_dose.py` makes at length in the
	// simulation -- a census of survivors under-counts whatever destroys its own
	// host.
	//
	// AN IDENTIFIER IS NEVER REUSED, so `Seen` cannot confuse two dungeons:
	// `NextDungeonId` only ever counts up, which `UCataclysmEmpireRun` states
	// and `FireSurge` relies on.
	TSet<ECataclysmType> Senders;
	TSet<int32> Seen;
	int32 Sent = 0;
	int32 FallenCities = 0;
	TArray<FCataclysmDungeon> Everything;

	for (int32 Elapsed = 0; Elapsed < 2000; ++Elapsed)
	{
		Run->AdvanceDay();

		for (const FCataclysmDungeon& Standing : Run->Dungeons)
		{
			bool bAlreadySeen = false;
			Seen.Add(Standing.DungeonId, &bAlreadySeen);
			if (!bAlreadySeen)
			{
				Everything.Add(Standing);
			}
		}
	}

	TestTrue(TEXT("some dungeons landed"), Everything.Num() > 0);

	for (const FCataclysmDungeon& Dungeon : Everything)
	{
		// A FALLEN CITY NAMES NOBODY, because it is made from what a city was
		// carrying rather than sent by anyone. It is the one kind the game
		// itself builds unassigned, which is why the unassigned value has to
		// stay reachable rather than being tidied away.
		if (Dungeon.Type == ECataclysmDungeonType::FallenCity)
		{
			++FallenCities;
			TestEqual(TEXT("a fallen city was sent by nobody"),
					  Dungeon.Cataclysm, ECataclysmType::None);
			continue;
		}

		++Sent;

		TestNotEqual(FString::Printf(
			TEXT("dungeon %d names a Cataclysm"), Dungeon.DungeonId),
			Dungeon.Cataclysm, ECataclysmType::None);

		// **AND IT IS ONE THE RUN ACTUALLY FACES.** A stamp drawn from the whole
		// roster rather than from the active set would hand the player
		// objectives towards a Cataclysm that is not in the campaign, which
		// would make the requirement unmeetable rather than merely wrong.
		TestTrue(FString::Printf(
			TEXT("and dungeon %d's Cataclysm is one of the four active"),
			Dungeon.DungeonId),
			Run->ActiveCataclysms.Contains(Dungeon.Cataclysm));

		Senders.Add(Dungeon.Cataclysm);
	}

	// THE TWO CONTROLS. Both loops above are inside an `if`, so without these a
	// run that landed nothing of one kind would report a check that never ran as
	// a check that passed.
	TestTrue(FString::Printf(TEXT("%d dungeons were sent by somebody"), Sent),
			 Sent > 0);
	TestTrue(FString::Printf(TEXT("and %d cities fell"), FallenCities),
			 FallenCities > 0);

	// MORE THAN ONE OF THEM SENT SOMETHING. Without this the checks above would
	// pass on an implementation that always answered the first active Cataclysm,
	// which is the shape a broken draw takes.
	TestTrue(FString::Printf(
		TEXT("more than one Cataclysm sent something; %d did"), Senders.Num()),
		Senders.Num() > 1);

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
