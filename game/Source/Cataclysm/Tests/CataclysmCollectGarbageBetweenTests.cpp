// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

// TEST-ONLY AND EDITOR-ONLY. `WITH_DEV_AUTOMATION_TESTS` is also true in a
// packaged Development build, and `WITH_EDITOR` is not, so this file compiles to
// nothing in any packaged game. Every automation run this project makes is the
// editor (`tools/unreal_build.py` starts `UnrealEditor-Cmd.exe`).
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreDelegates.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectArray.h"
#include "UObject/UObjectIterator.h"
#include "UObject/WeakObjectPtrTemplates.h"

/**
 * Collect garbage between automation tests, so a destroyed test world is freed.
 *
 * WHY THIS EXISTS. Issue #2485: a whole-suite run grew to about 45 GB of
 * private memory (45,700 MB and 45,644 MB, measured twice on 2026-10-24).
 * `UWorld::DestroyWorld` only cleans a world up and takes it off the root set;
 * the world, its levels and every actor in it stay in memory until a garbage
 * collection runs, and nothing in an automation run collects between tests.
 * The engine's own `UEngine::ConditionalCollectGarbage`, called at the end of
 * `UWorld::Tick`, collects on a request or, on its timer, only when a world in
 * the engine's world list has begun play; a test world is in no list.
 *
 * WHAT IT DOES. After each test it counts the worlds alive beyond what the
 * last collection left, and runs `CollectGarbage`, timed, when either of two
 * limits is reached: `Cataclysm.TestCollectEveryWorlds` worlds left over (16),
 * or used memory above `Cataclysm.TestCollectAboveMb` (10,752 MB) with any
 * world left over. The test has ended by then: the framework broadcasts the
 * end event with its latent commands empty (`AutomationTest.cpp` 1370 and 1386
 * in Unreal Engine 5.8), on the game thread, from the automation worker's tick.
 *
 * EVERY N WORLDS, NOT AFTER EVERY TEST, SINCE ISSUE #2928 SLICE 5. Until then
 * one world left over was enough, and a whole-suite run made about 4,522
 * collections taking about 147 s of its 19 minutes (the survey on issue #2928,
 * section 3; windows dfb-w1, ail-w1 and rost-w1). A collection's cost is mostly
 * marking what is still alive, which is about the same whether it frees one
 * world or sixteen, so fewer collections cost less in all. THE MEMORY LIMIT is
 * what keeps a run of large worlds from piling up before the count is reached:
 * the suite's peak was 9.2 to 9.8 GB with a collection after every test, and
 * the limit is set a little above the 9.0 to 9.7 GB the suite holds at its end.
 * Both limits are console variables, so a window can measure another pair
 * without a build. A world left alive between collections is one `DestroyWorld`
 * has cleaned up and taken off the root set; nothing reaches it through a world
 * list, and the one code path that walks every object of a class skips a world
 * that is tearing down (`ACataclysmPlayerState::StartingLevelChanged`).
 *
 * WHAT IT PRINTS. Always, once when the process exits after a test has run, a
 * line beginning `CataclysmTestMemorySummary:`. With the switch on, a line per
 * test beginning `CataclysmTestMemoryPerTest:`. The switch is the environment
 * variable `CATACLYSM_TEST_MEMORY_LINES=1`, read at the first test, or the
 * console variable `Cataclysm.TestMemoryLines`. Both lines are at Log level in
 * a category of their own; the engine's `LogGarbage` is declared at Warning, so
 * the collections it runs say nothing of their own.
 */

DEFINE_LOG_CATEGORY_STATIC(LogCataclysmTestMemory, Log, All);

static TAutoConsoleVariable<int32> CVarCataclysmTestCollectEveryWorlds(
	TEXT("Cataclysm.TestCollectEveryWorlds"),
	16,
	TEXT("Collect garbage between automation tests once this many destroyed test "
		 "worlds are left over since the last collection. 1 collects after every "
		 "test that left one, as before issue #2928 slice 5."));

static TAutoConsoleVariable<int32> CVarCataclysmTestCollectAboveMb(
	TEXT("Cataclysm.TestCollectAboveMb"),
	10752,
	TEXT("Collect garbage between automation tests whenever the process's used "
		 "memory is above this many megabytes and any destroyed test world is "
		 "left over. Issue #2928 slice 5."));

static TAutoConsoleVariable<int32> CVarCataclysmTestMemoryLines(
	TEXT("Cataclysm.TestMemoryLines"),
	0,
	TEXT("1 prints one CataclysmTestMemoryPerTest line after every automation "
		 "test: its memory, the live world and object counts and the collection's "
		 "time. The environment variable CATACLYSM_TEST_MEMORY_LINES=1 turns it on "
		 "as well. Issue #2485."));

namespace CataclysmCollectGarbageBetweenTests
{
	/** What one collection found and how long it took. */
	struct FCollection
	{
		/** Worlds alive just before the collection. */
		int32 WorldsBefore = 0;

		/** Worlds alive just after it. */
		int32 WorldsAfter = 0;

		/** How long `CollectGarbage` took, in seconds. */
		double Seconds = 0.0;
	};

	/**
	 * Every world in memory, the editor's own included.
	 *
	 * A WORLD MARKED AS GARBAGE IS COUNTED. It still holds its memory until a
	 * collection frees it, which is what this count is for. Class default
	 * objects and objects a collection is part way through freeing are left out
	 * by the iterator's own defaults.
	 */
	int32 CountLiveWorlds()
	{
		int32 Count = 0;
		for (TObjectIterator<UWorld> It; It; ++It)
		{
			++Count;
		}
		return Count;
	}

	/**
	 * Collect garbage now, and say how many worlds it left and how long it took.
	 *
	 * THE ONE FUNCTION THE HANDLER AND THE GUARD TEST BOTH CALL, so the test
	 * proves what the handler does rather than a copy of it.
	 * `FTestWorldWrapper::DestroyTestWorld` in the engine's own test helpers
	 * (`AutomationCommon.cpp` 138 to 144) collects in the same way.
	 */
	FCollection CollectDestroyedWorlds()
	{
		FCollection Result;
		Result.WorldsBefore = CountLiveWorlds();
		const double Start = FPlatformTime::Seconds();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		Result.Seconds = FPlatformTime::Seconds() - Start;
		Result.WorldsAfter = CountLiveWorlds();
		return Result;
	}

	/** Bytes to whole megabytes, for the log lines. */
	int64 Megabytes(uint64 InBytes)
	{
		return static_cast<int64>(InBytes / (1024ull * 1024ull));
	}

	/**
	 * Whether to collect now: `LeftOver` worlds alive beyond the last collection's, `UsedMb` the process's used
	 * memory. THE ONE RULE THE HANDLER AND ITS TEST BOTH ASK. Nothing left over never collects; at least `EveryWorlds`
	 * left over always does; one or more left over with used memory above `AboveMb` does. `EveryWorlds` below 1 reads
	 * as 1.
	 */
	bool ShouldCollect(int32 LeftOver, int64 UsedMb, int32 EveryWorlds, int64 AboveMb)
	{
		if (LeftOver <= 0)
		{
			return false;
		}
		return LeftOver >= FMath::Max(1, EveryWorlds) || UsedMb > AboveMb;
	}

	/**
	 * Bound to the automation framework for as long as this module is loaded.
	 *
	 * BOUND BY A STATIC OBJECT, AS THE TESTS THEMSELVES ARE REGISTERED. Every
	 * `IMPLEMENT_SIMPLE_AUTOMATION_TEST` is a static whose constructor calls
	 * `FAutomationTestFramework::Get().RegisterAutomationTest` and whose
	 * destructor unregisters it (`AutomationTest.cpp` 165 and 176), so the
	 * framework is safe to reach at the same moment. This module has no
	 * `StartupModule` of its own (`Cataclysm.cpp` uses `FDefaultGameModuleImpl`).
	 */
	class FTestEndCollector
	{
	public:
		FTestEndCollector()
		{
			FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
			Framework.OnTestStartEvent.AddRaw(this, &FTestEndCollector::OnTestStart);
			Framework.OnTestEndEvent.AddRaw(this, &FTestEndCollector::OnTestEnd);
		}

		~FTestEndCollector()
		{
			FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
			Framework.OnTestStartEvent.RemoveAll(this);
			Framework.OnTestEndEvent.RemoveAll(this);
			if (bBoundToExit)
			{
				FCoreDelegates::GetApplicationWillTerminateDelegate().RemoveAll(this);
			}
		}

		/** Whether the framework holds this object on both of its test events. */
		bool IsBoundToBothTestEvents() const
		{
			FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
			return Framework.OnTestStartEvent.IsBoundToObject(this)
				&& Framework.OnTestEndEvent.IsBoundToObject(this);
		}

	private:
		/**
		 * THE BASELINE IS TAKEN AS THE FIRST TEST STARTS: whatever is alive before
		 * any test has made a world. After that it is whatever the last collection
		 * left, so a world some test leaves rooted raises it once instead of
		 * causing a collection after every later test.
		 *
		 * IN A WHOLE-SUITE RUN THE FIRST TEST IS AN ENGINE SMOKE TEST THAT RUNS
		 * BEFORE THE EDITOR'S OWN WORLD EXISTS, so the baseline does not include
		 * it, and the live count can sit one above the first baseline from about
		 * the 608th test on. Read by the enchantment session from the log of
		 * window t74w (#2485), not measured by any test: the baseline was 0,
		 * tests 1 to 607 ran before "LogEngine: Initializing Engine", and the
		 * count rose by one at test 608. That the extra world is the editor's was
		 * not measured by name.
		 */
		void OnTestStart(FAutomationTestBase* /*Test*/)
		{
			if (Baseline != INDEX_NONE)
			{
				return;
			}
			Baseline = CountLiveWorlds();
			bLinesFromEnvironment = FPlatformMisc::GetEnvironmentVariable(
				TEXT("CATACLYSM_TEST_MEMORY_LINES")) == TEXT("1");

			// THE RUN ENDS BY A FORCED EXIT, SO NO LATER EVENT REACHES A MODULE.
			// `-testexit` calls `FPlatformMisc::RequestExit(true)` straight after
			// the last test, the worker never receives its stop-session message,
			// and the log ends at "Engine exit requested". The Windows exit
			// broadcasts this delegate and then flushes the log before it ends
			// the process (`WindowsPlatformMisc.cpp` 1488 to 1505).
			FCoreDelegates::GetApplicationWillTerminateDelegate().AddRaw(
				this, &FTestEndCollector::PrintSummary);
			bBoundToExit = true;
		}

		void OnTestEnd(FAutomationTestBase* Test)
		{
			++TestsEnded;
			if (Baseline == INDEX_NONE)
			{
				Baseline = CountLiveWorlds();
			}

			int32 WorldsNow = CountLiveWorlds();
			double CollectionSeconds = -1.0;
			// MEMORY READ ONLY WHEN A WORLD IS LEFT OVER, so a test that left none costs nothing more than before.
			const int32 LeftOver = WorldsNow - Baseline;
			const int64 UsedMb = LeftOver > 0 ? Megabytes(FPlatformMemory::GetStats().UsedVirtual) : 0;
			if (ShouldCollect(LeftOver, UsedMb, CVarCataclysmTestCollectEveryWorlds.GetValueOnGameThread(),
							  CVarCataclysmTestCollectAboveMb.GetValueOnGameThread()))
			{
				const FCollection Collection = CollectDestroyedWorlds();
				CollectionSeconds = Collection.Seconds;
				WorldsNow = Collection.WorldsAfter;
				Baseline = Collection.WorldsAfter;
				++Collections;
				CollectionTotalSeconds += Collection.Seconds;
			}

			if (!bLinesFromEnvironment && CVarCataclysmTestMemoryLines.GetValueOnGameThread() == 0)
			{
				return;
			}
			const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
			const FString Duration = CollectionSeconds < 0.0
				? FString(TEXT("none"))
				: FString::Printf(TEXT("%.1f"), CollectionSeconds * 1000.0);
			const FString Name = Test ? Test->GetTestFullName() : FString(TEXT("unknown"));
			UE_LOG(LogCataclysmTestMemory, Log,
				TEXT("CataclysmTestMemoryPerTest: index=%d name=%s used_virtual_mb=%lld used_physical_mb=%lld worlds=%d objects=%d collection_ms=%s"),
				TestsEnded,
				*Name,
				Megabytes(Stats.UsedVirtual),
				Megabytes(Stats.UsedPhysical),
				WorldsNow,
				GUObjectArray.GetObjectArrayNumMinusAvailable(),
				*Duration);
		}

		/**
		 * PEAK IS THE OPERATING SYSTEM'S OWN FIGURE. On Windows `UsedVirtual` is
		 * the process's commit charge and `PeakUsedVirtual` its peak
		 * (`WindowsPlatformMemory.cpp` 345 and 346), so the peak includes any
		 * moment inside a test, which no sample taken between tests could see.
		 */
		void PrintSummary()
		{
			if (TestsEnded == 0 || bSummaryPrinted)
			{
				return;
			}
			bSummaryPrinted = true;
			const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
			UE_LOG(LogCataclysmTestMemory, Log,
				TEXT("CataclysmTestMemorySummary: tests=%d peak_used_virtual_mb=%lld final_used_virtual_mb=%lld collections=%d collection_total_s=%.3f worlds_at_last_collection=%d collect_every_worlds=%d collect_above_mb=%d"),
				TestsEnded,
				Megabytes(Stats.PeakUsedVirtual),
				Megabytes(Stats.UsedVirtual),
				Collections,
				CollectionTotalSeconds,
				Baseline,
				CVarCataclysmTestCollectEveryWorlds.GetValueOnGameThread(),
				CVarCataclysmTestCollectAboveMb.GetValueOnGameThread());
		}

		int32 Baseline = INDEX_NONE;
		int32 TestsEnded = 0;
		int32 Collections = 0;
		double CollectionTotalSeconds = 0.0;
		bool bLinesFromEnvironment = false;
		bool bBoundToExit = false;
		bool bSummaryPrinted = false;
	};

	FTestEndCollector GTestEndCollector;

	/** Whether the collection between tests is bound, read from the framework. */
	bool IsHandlerRegistered()
	{
		return GTestEndCollector.IsBoundToBothTestEvents();
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_TEST(FCataclysmDestroyedWorldIsFreedTest,
	"Cataclysm.TestWorld.ADestroyedWorldIsFreedByTheCollectionBetweenTests")
{
	// TEN WORLDS, EACH WITH A REAL CREATURE IN IT, MADE AND DESTROYED THE WAY
	// EVERY TEST DOES. Weak pointers are kept, because a raw pointer on the stack
	// is no reference to the collector and a weak one says whether the object is
	// still there without keeping it.
	using namespace CataclysmCollectGarbageBetweenTests;
	constexpr int32 Cycles = 10;
	const int32 Baseline = CollectDestroyedWorlds().WorldsAfter;

	TArray<TWeakObjectPtr<UWorld>> Worlds;
	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> Creatures;
	for (int32 Index = 0; Index < Cycles; ++Index)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(*FString::Printf(TEXT("world %d"), Index), World))
		{
			return false;
		}
		ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>();
		TestNotNull(*FString::Printf(TEXT("the creature in world %d"), Index), Creature);
		Worlds.Add(World);
		Creatures.Add(Creature);
		World->DestroyWorld(false);
	}

	// THE CONTROL, READ FIRST. Destroying a world does not free it: it is
	// cleaned up and taken off the root set, and nothing is marked as garbage
	// (`World.cpp` 2770 to 2802). "Even if garbage" is asked, so the question
	// is only whether the object is still in memory.
	for (int32 Index = 0; Index < Cycles; ++Index)
	{
		TestTrue(*FString::Printf(TEXT("before the collection, destroyed world %d is still in memory"), Index),
			Worlds[Index].IsValid(/*bEvenIfPendingKill=*/true));
		TestTrue(*FString::Printf(TEXT("before the collection, the creature in world %d is still in memory"), Index),
			Creatures[Index].IsValid(/*bEvenIfPendingKill=*/true));
	}

	const FCollection Collection = CollectDestroyedWorlds();
	TestTrue(TEXT("before the collection, the ten destroyed worlds were counted as alive"),
		Collection.WorldsBefore >= Baseline + Cycles);

	for (int32 Index = 0; Index < Cycles; ++Index)
	{
		TestFalse(*FString::Printf(TEXT("after the collection, destroyed world %d is freed"), Index),
			Worlds[Index].IsValid(/*bEvenIfPendingKill=*/true));
		TestFalse(*FString::Printf(TEXT("after the collection, the creature in world %d is freed"), Index),
			Creatures[Index].IsValid(/*bEvenIfPendingKill=*/true));
	}
	TestEqual(TEXT("after the collection, the live world count is back at its baseline"),
		Collection.WorldsAfter, Baseline);

	return true;
}

CATACLYSM_TEST(FCataclysmCollectionRuleTest,
	"Cataclysm.TestWorld.TheCollectionBetweenTestsWaitsForSixteenWorldsOrTheMemoryLimit")
{
	// ISSUE #2928 SLICE 5: the rule the handler asks, at the defaults (16 worlds, 10,752 MB) and at their edges.
	using namespace CataclysmCollectGarbageBetweenTests;
	TestFalse(TEXT("CONTROL: nothing left over never collects, whatever the memory"), ShouldCollect(0, 99999, 16, 10752));
	TestFalse(TEXT("one world left over at low memory waits"), ShouldCollect(1, 9000, 16, 10752));
	TestFalse(TEXT("fifteen left over at low memory still waits"), ShouldCollect(15, 9000, 16, 10752));
	TestTrue(TEXT("sixteen left over collects"), ShouldCollect(16, 9000, 16, 10752));
	TestTrue(TEXT("one left over above the memory limit collects"), ShouldCollect(1, 10753, 16, 10752));
	TestFalse(TEXT("one left over exactly at the memory limit waits"), ShouldCollect(1, 10752, 16, 10752));
	TestTrue(TEXT("a count of 1 collects after every test that left a world, as before slice 5"),
		ShouldCollect(1, 0, 1, 10752));
	TestTrue(TEXT("a count below 1 reads as 1"), ShouldCollect(1, 0, 0, 10752));
	return true;
}

CATACLYSM_TEST(FCataclysmCollectionBetweenTestsIsRegisteredTest,
	"Cataclysm.TestWorld.TheCollectionBetweenTestsIsBoundToEveryTestEnd")
{
	// THE CONTROL, READ FIRST: the framework's question answers no for an object
	// that never bound itself, so a yes below is about the handler and not a
	// question that always says yes.
	int32 NeverBound = 0;
	TestFalse(TEXT("an object that never bound itself is not on the test end event"),
		FAutomationTestFramework::Get().OnTestEndEvent.IsBoundToObject(&NeverBound));

	TestTrue(TEXT("the collection between tests is bound to the test start and test end events"),
		CataclysmCollectGarbageBetweenTests::IsHandlerRegistered());

	return true;
}

#undef CATACLYSM_TEST

#endif  // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
