// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Empire/CataclysmDungeonModifier.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/World.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmGameMode.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Whether the game tells an empire run which difficulty tier it is. Issue #1444.
 *
 * WHAT WAS WRONG, AND IT IS A JOIN RATHER THAN A CALCULATION. Eight difficulty
 * tiers exist and everything that consumes one is correct given the tier it is
 * handed: `UCataclysmRoster::ActiveCountFor` returns the tier itself, and
 * `UCataclysmDungeonModifierRules::CountFor` returns one modifier per tier,
 * doubled for a Sacrificial dungeon. **Nothing handed a real one.**
 * `UCataclysmGameInstance::BeginEmpireRun` defaulted the tier to 1 and both
 * paths that start a run took the default, so every run the game had ever
 * started faced one Cataclysm and gave every dungeon one modifier -- at every
 * tier the player could be fighting at. Seven of the eight tiers had never
 * executed outside a test.
 *
 * WHY THIS FILE IS NOT "A RUN AT TIER 4 BEHAVES LIKE TIER 4". That test already
 * existed, several times over, in `CataclysmRosterTests.cpp` and
 * `CataclysmDungeonModifierTests.cpp`, and it passed the whole time the game was
 * broken -- because each of those tests hands the tier to `Begin` itself. A test
 * that supplies the missing step proves the step works and says nothing about
 * whether anything performs it. **So the tests here never name a tier.** They
 * set the same console variable a player types, start a run the way the game
 * starts one, and ask what the run came out at.
 *
 * WHAT "THE WAY THE GAME STARTS ONE" MEANS HERE, EXACTLY. Two functions start
 * every run in the project:
 *
 *   - `UCataclysmGameInstance::GetOrBeginEmpireRun`, which is what
 *     `Cataclysm.EmpireMap` and `Cataclysm.EmpireAdvance` reach through
 *     `UCataclysmGameInstance::EmpireRunFor`.
 *   - `UCataclysmGameInstance::BeginEmpireRun`, which is what
 *     `Cataclysm.EmpireBegin` calls.
 *
 * Both are called below with no tier, which is how the game calls them.
 *
 * The static `UCataclysmGameInstance::EmpireRunFor` that stands between the
 * console commands and the first of those is covered too, in the last test
 * here, on a world that has been given a game instance.
 *
 * WHAT IS NOT COVERED, NAMED RATHER THAN LEFT TO BE ASSUMED. The console
 * command objects themselves are not executed here. What is covered is every
 * line of them below the argument parsing.
 *
 * AND THE GAME MODE BRANCH OF `DifficultyTierIn` IS NOT COVERED EITHER, for the
 * reason `CataclysmDifficultyTierTests.cpp` gives at length: a test world has no
 * authority game mode and cannot be given one. The console variable is the other
 * branch, it is a real one a player uses, and it is the branch these tests take.
 */

namespace CataclysmEmpireRunTierTest
{
	/**
	 * A tier that is not the default and not the top of the range.
	 *
	 * FIVE RATHER THAN 8, so that a run which quietly clamped to the highest
	 * tier would be as visible as one that quietly fell back to the lowest. A
	 * test whose expected value sits at either end of a range cannot tell a
	 * correct answer from a clamped one.
	 */
	constexpr int32 ATierThatIsNotTheDefault = 5;

	/**
	 * Saying which tier the game is being played at.
	 *
	 * `CataclysmTestWorld::FScopedDifficultyTier` WRITES AT THE CONSOLE'S OWN
	 * PRIORITY, and its own comment says why that is not optional: a plain write
	 * to this console variable is silently discarded once
	 * `CataclysmDroppedItemTests.cpp` has set it, and `Cataclysm.Drop` runs
	 * before `Cataclysm.EmpireRunTier`. Every test below went green on its own
	 * and red in the full suite until they moved onto this.
	 */
	using FTier = CataclysmTestWorld::FScopedDifficultyTier;
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------
// The test this issue is about
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmRunTakesTheGamesTierTest,
	"Cataclysm.EmpireRunTier.AskingTheGameForARunGivesOneAtTheGamesTier")
{
	using namespace CataclysmEmpireRunTierTest;

	// THE CONTROL FIRST, AND IT IS NOT DECORATION. Without it "the run came out
	// at 5" would also be the reading if the tier were hard-coded to 5, and a
	// test that cannot fail for the reason it names is worth nothing. Zero is
	// what the console variable holds when nobody has typed one, and it is the
	// state every other test in the project runs in.
	const FTier Tier(0);
	if (!TestTrue(TEXT("the Cataclysm.DifficultyTier console variable exists"),
				  Tier.Found()))
	{
		return false;
	}

	UCataclysmGameInstance* Untold = NewObject<UCataclysmGameInstance>();
	UCataclysmEmpireRun* AtTheDefault = Untold->GetOrBeginEmpireRun();

	if (!TestNotNull(TEXT("a run starts when nobody has set a tier"),
					 AtTheDefault))
	{
		return false;
	}

	TestEqual(TEXT("and with nothing set it is the lowest tier"),
			  AtTheDefault->DifficultyTier,
			  ACataclysmGameMode::LowestDifficultyTier);

	// AND NOW THE THING THAT WAS BROKEN. A player types this; nothing in this
	// test hands a tier to a run.
	Tier.Set(ATierThatIsNotTheDefault);

	UCataclysmGameInstance* Told = NewObject<UCataclysmGameInstance>();
	UCataclysmEmpireRun* Run = Told->GetOrBeginEmpireRun();

	if (!TestNotNull(TEXT("a run starts"), Run))
	{
		return false;
	}

	TestEqual(TEXT("and it is at the tier the game is being played at"),
			  Run->DifficultyTier, ATierThatIsNotTheDefault);

	// AND THE TIER REACHED THE COUNT IT DECIDES, which is the half a stored
	// field cannot show. `UCataclysmRoster::ActiveCountFor` is asked once, by
	// `Begin`, so a tier that arrived after the run had started would leave this
	// at one.
	TestEqual(TEXT("so the run faces that many Cataclysms"),
			  Run->ActiveCataclysms.Num(), ATierThatIsNotTheDefault);

	// AND THE OTHER COUNT. `GiveModifiers` asks this every time a dungeon
	// arrives, long after `Begin` has returned, so it reads the field rather
	// than the argument. Checked through the rule rather than through a drawn
	// dungeon because a headless run has no modifier table to draw from.
	TestEqual(TEXT("and every ordinary dungeon in it carries that many "
				   "modifiers"),
			  UCataclysmDungeonModifierRules::CountFor(
				  Run->DifficultyTier, ECataclysmDungeonSubType::None),
			  ATierThatIsNotTheDefault);

	return true;
}

CATACLYSM_TEST(FCataclysmBegunRunTakesTheGamesTierTest,
	"Cataclysm.EmpireRunTier.BeginningARunWithoutNamingATierTakesTheGames")
{
	using namespace CataclysmEmpireRunTierTest;

	const FTier Tier(ATierThatIsNotTheDefault);
	if (!TestTrue(TEXT("the Cataclysm.DifficultyTier console variable exists"),
				  Tier.Found()))
	{
		return false;
	}

	UCataclysmGameInstance* Instance = NewObject<UCataclysmGameInstance>();

	// THE OTHER DOOR. `Cataclysm.EmpireBegin` with a seed, an escalation and a
	// lethality rung and no fourth argument arrives here, and used to arrive
	// with a hard 1 behind it.
	UCataclysmEmpireRun* Run = Instance->BeginEmpireRun(
		/*Seed*/ 42, ECataclysmSurgeMode::Static, /*LethalityRung*/ 0);

	if (!TestNotNull(TEXT("a run begins"), Run))
	{
		return false;
	}

	TestEqual(TEXT("at the tier the game is being played at"),
			  Run->DifficultyTier, ATierThatIsNotTheDefault);
	TestEqual(TEXT("facing that many Cataclysms"),
			  Run->ActiveCataclysms.Num(), ATierThatIsNotTheDefault);

	// A NAMED TIER STILL WINS, so `Cataclysm.EmpireBegin 1 static 0 8` keeps
	// doing what it did. The console variable says 5 and this asks for 8.
	UCataclysmEmpireRun* Named = Instance->BeginEmpireRun(
		/*Seed*/ 42, ECataclysmSurgeMode::Static, /*LethalityRung*/ 0,
		ACataclysmGameMode::HighestDifficultyTier);

	TestEqual(TEXT("a tier asked for outright is the one used"),
			  Named->DifficultyTier,
			  ACataclysmGameMode::HighestDifficultyTier);
	TestEqual(TEXT("and it is not the console variable's"),
			  Named->ActiveCataclysms.Num(),
			  ACataclysmGameMode::HighestDifficultyTier);

	return true;
}

CATACLYSM_TEST(FCataclysmRunKeepsItsTierTest,
	"Cataclysm.EmpireRunTier.ARunKeepsTheTierItStartedAtWhenTheConsoleChanges")
{
	using namespace CataclysmEmpireRunTierTest;

	// `docs/Cataclysm_GDD_v2.md` SECTION XII, in as many words: "A run is played
	// at a fixed tier, so a player does not move up the tiers inside a run; they
	// finish a campaign and start the next one higher."
	//
	// WHICH IS WHY THE TIER IS READ ONCE. Typing a new tier part way through a
	// campaign changes what armour is worth, because every hit asks
	// `DifficultyTierIn` afresh, and it must not re-tier the empire underneath
	// the player: the Cataclysms already drawn would change and a dungeon
	// standing on the map would start carrying a different number of modifiers
	// from the one beside it.
	const FTier Tier(2);
	if (!TestTrue(TEXT("the Cataclysm.DifficultyTier console variable exists"),
				  Tier.Found()))
	{
		return false;
	}

	UCataclysmGameInstance* Instance = NewObject<UCataclysmGameInstance>();
	UCataclysmEmpireRun* Run = Instance->GetOrBeginEmpireRun();

	if (!TestNotNull(TEXT("a run starts"), Run))
	{
		return false;
	}

	TestEqual(TEXT("at tier 2"), Run->DifficultyTier, 2);

	Tier.Set(7);
	Run->AdvanceDays(30);

	TestEqual(TEXT("and thirty days later it is still tier 2"),
			  Run->DifficultyTier, 2);
	TestEqual(TEXT("still facing the two Cataclysms it started against"),
			  Run->ActiveCataclysms.Num(), 2);

	// AND ASKING FOR THE RUN AGAIN IS NOT ASKING FOR A NEW ONE, so a screen
	// opened after the console variable moved does not re-tier the campaign
	// either.
	TestTrue(TEXT("asking the instance again gives the same run"),
			 Instance->GetOrBeginEmpireRun() == Run);
	TestEqual(TEXT("at the tier it started at"), Run->DifficultyTier, 2);

	// THE NEXT RUN PICKS THE NEW TIER UP, which is what makes the paragraph
	// above a rule about a run rather than a value nobody can change.
	UCataclysmEmpireRun* Next = Instance->BeginEmpireRun(/*Seed*/ 3);
	TestEqual(TEXT("a fresh run takes the tier now set"),
			  Next->DifficultyTier, 7);

	return true;
}

CATACLYSM_TEST(FCataclysmRunForAWorldTakesTheGamesTierTest,
	"Cataclysm.EmpireRunTier.AskingAWorldForARunGivesOneAtTheGamesTier")
{
	using namespace CataclysmEmpireRunTierTest;

	// THE STATIC FUNCTION THE CONSOLE COMMANDS ACTUALLY CALL.
	// `Cataclysm.EmpireMap` and `Cataclysm.EmpireAdvance` both hold a world and
	// call `UCataclysmGameInstance::EmpireRunFor(World, true)`, which walks to
	// the game instance and asks it. The tests above start at the game instance;
	// this one starts where the game does.
	const FTier Tier(ATierThatIsNotTheDefault);
	if (!TestTrue(TEXT("the Cataclysm.DifficultyTier console variable exists"),
				  Tier.Found()))
	{
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	// A GAME INSTANCE ON THE WORLD, WHICH IS WHAT A TEST WORLD LACKS. Without
	// this `EmpireRunFor` answers null, honestly, because the engine's own
	// `UGameInstance` is not this one. `CataclysmSaveWriterTests.cpp` attaches
	// one the same way and for the same reason.
	UCataclysmGameInstance* Instance = NewObject<UCataclysmGameInstance>();
	World->SetGameInstance(Instance);

	UCataclysmEmpireRun* Run =
		UCataclysmGameInstance::EmpireRunFor(World, /*bStartIfNone*/ true);

	if (!TestNotNull(TEXT("asking the world for a run starts one"), Run))
	{
		World->DestroyWorld(false);
		return false;
	}

	TestEqual(TEXT("at the tier the game is being played at"),
			  Run->DifficultyTier, ATierThatIsNotTheDefault);
	TestEqual(TEXT("facing that many Cataclysms"),
			  Run->ActiveCataclysms.Num(), ATierThatIsNotTheDefault);

	// AND THE WORLD'S GAME INSTANCE IS HOLDING IT, so this really did go through
	// the same object the console command would have reached rather than making
	// a run of its own.
	TestTrue(TEXT("and the world's game instance holds that run"),
			 Instance->EmpireRun.Get() == Run);

	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// What a run says about itself
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmRunDescribesItsTierTest,
	"Cataclysm.EmpireRunTier.ARunSaysWhichTierItIsAndWhoItIsAgainst")
{
	using namespace CataclysmEmpireRunTierTest;

	// THE ONLY PLACE A PERSON SEES THIS. `Describe` is what `Cataclysm.EmpireBegin`,
	// `Cataclysm.ShowEmpire` and `Cataclysm.EmpireAdvance` print, and it said
	// nothing about the tier while the tier was always 1 and there was nothing
	// to look at. Now that it comes from somewhere, somebody typing a command
	// has to be able to see which tier a run took, or the fix is invisible.
	const FTier Tier(ATierThatIsNotTheDefault);
	if (!TestTrue(TEXT("the Cataclysm.DifficultyTier console variable exists"),
				  Tier.Found()))
	{
		return false;
	}

	UCataclysmGameInstance* Instance = NewObject<UCataclysmGameInstance>();
	UCataclysmEmpireRun* Run = Instance->GetOrBeginEmpireRun();

	if (!TestNotNull(TEXT("a run starts"), Run))
	{
		return false;
	}

	const FString Description = Run->Describe();

	TestTrue(TEXT("the description names the tier the run is at"),
			 Description.Contains(
				 FString::Printf(TEXT("Difficulty tier %d"),
								 ATierThatIsNotTheDefault),
				 ESearchCase::CaseSensitive));

	TestTrue(TEXT("and how many Cataclysms it is against"),
			 Description.Contains(
				 FString::Printf(TEXT("facing %d"), ATierThatIsNotTheDefault),
				 ESearchCase::CaseSensitive));

	// AND NAMES THEM, so a person can see which ones this seed drew rather than
	// only how many. Checked against the run's own list so that the test does
	// not have to re-derive the draw.
	for (const ECataclysmType Cataclysm : Run->ActiveCataclysms)
	{
		const FString Name = UCataclysmRoster::NameFor(Cataclysm).ToString();
		TestTrue(FString::Printf(TEXT("and names %s"), *Name),
				 Description.Contains(Name, ESearchCase::CaseSensitive));
	}

	// AND IT REALLY WAS MORE THAN ONE, so the loop above is a check and not an
	// empty pass.
	TestEqual(TEXT("and there were five of them to name"),
			  Run->ActiveCataclysms.Num(), ATierThatIsNotTheDefault);

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
