// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Tests/CataclysmTestRun.h"

/**
 * The helpers that let a test name the Cataclysms its run faces (`Tests/CataclysmTestRun.h`). Issue #2928.
 *
 * Every empire run a test begins holds `CataclysmTestRun::FBuiltAsWritten` while it begins, so a change to which
 * Cataclysms the game counts as built moves none of those tests. These three check the helpers themselves: the
 * holder names its list, keeps a list an outer test holds, and `BeginFacing` faces exactly what it is told.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTestRunHoldsTheNamedListTest,
	"Cataclysm.TestRun.TheHolderMakesTheBuiltListTheOneNamedForTheTests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTestRunHoldsTheNamedListTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("set-up: no test holds a built list here"), FCataclysmBuiltCataclysmsForTests::IsHeld());
	{
		const CataclysmTestRun::FBuiltAsWritten Held;
		TestTrue(TEXT("while it lives, a list is held"), FCataclysmBuiltCataclysmsForTests::IsHeld());
		TestTrue(TEXT("and it is the list the tests name"),
				 UCataclysmRoster::Built() == CataclysmTestRun::BuiltOn20261126());
	}
	TestFalse(TEXT("CONTROL: once it is gone, nothing is held"), FCataclysmBuiltCataclysmsForTests::IsHeld());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTestRunKeepsAnOuterListTest,
	"Cataclysm.TestRun.TheHolderKeepsAListAnOuterTestHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTestRunKeepsAnOuterListTest::RunTest(const FString& Parameters)
{
	// THE NINE TESTS WRITTEN WHEN WAR WAS BUILT hold {Demonic, Death, War} themselves. A holder inside one of them, as
	// every shared helper now has, must keep that list rather than put its own in place.
	const TArray<ECataclysmType> AsAnOuterTestHolds = {ECataclysmType::War};
	const FCataclysmBuiltCataclysmsForTests Outer(AsAnOuterTestHolds);
	{
		const CataclysmTestRun::FBuiltAsWritten Inner;
		TestTrue(TEXT("inside an outer hold, the outer list is kept"), UCataclysmRoster::Built() == AsAnOuterTestHolds);
	}
	TestTrue(TEXT("CONTROL: and it is still the outer list afterwards"), UCataclysmRoster::Built() == AsAnOuterTestHolds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTestRunBeginFacingTest,
	"Cataclysm.TestRun.BeginFacingFacesExactlyTheNamedCataclysmsInOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTestRunBeginFacingTest::RunTest(const FString& Parameters)
{
	const TArray<ECataclysmType> Named = {ECataclysmType::Death, ECataclysmType::Demonic};
	for (int32 Seed = 1; Seed <= 5; ++Seed)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		CataclysmTestRun::BeginFacing(*Run, Named, Seed);
		TestTrue(FString::Printf(TEXT("seed %d: the run faces Death then Demonic"), Seed), Run->ActiveCataclysms == Named);
	}
	TestFalse(TEXT("CONTROL: and no list is left held"), FCataclysmBuiltCataclysmsForTests::IsHeld());
	return true;
}

