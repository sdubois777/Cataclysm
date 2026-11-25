// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * THE SABOTEUR'S TRAP TIMING, the C1 plan, accepted as written by the coordinating session on 2026-11-25.
 *
 * WHAT IS EXERCISED THROUGH ITS REAL SITE: placing a skill trap (`ACataclysmMinion::PlaceSkillTrap`), its arming timer
 * and its quarter-second step, both driven by the world's clock (`CataclysmTestWorld::RunClock`), and the forced
 * set-off every row action goes through (`ACataclysmMinion::SetOffSkillTrapsNear`). THE ROWS are read through THE
 * GENERATED TABLE and judged by the pipeline. No skill is cast: a trap's arming and its being chosen are what these
 * nodes change, and the trap tests (`Cataclysm.Traps.`) hold what a trap does once it triggers.
 *
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmSaboteurTrapTimingTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	constexpr float TrapTimingMetre = 100.0f;

	/** One stat line for `SetStatInputs`: a flat figure, or an increase. */
	struct FTrapTimingStat
	{
		const TCHAR* Stat = nullptr;
		float Value = 0.0f;
		ECataclysmStatBucket Bucket = ECataclysmStatBucket::Flat;
	};

	/** A player standing at `Home` that can place skill traps: a pool of 100 holding 100, every slot a trap's. */
	struct FTrapTimingPlayer
	{
		FTrapTimingPlayer(UWorld* World, const FVector& Home)
		{
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(Home, FRotator::ZeroRotator);
			if (!State || !Character)
			{
				return;
			}
			Character->SetPlayerState(State);
			Character->OnRep_PlayerState();
			System = State->GetCataclysmAbilitySystemComponent();
			if (System)
			{
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												100.0f);
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 100.0f);
				Holds({});
			}
		}

		bool IsUsable() const { return Character && System; }

		/** REPLACES THE WHOLE STAT LINE with the trap flag and these rows. */
		void Holds(const TArray<FTrapTimingStat>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			TArray<FTrapTimingStat> All = Rows;
			All.Add({TEXT("skill_slot_is_a_trap"), 1.0f});
			for (const FTrapTimingStat& Each : All)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = Each.Bucket;
				Row.Source = ECataclysmModifierSource::PassiveKeystone;
				Row.Value = Each.Value;
				FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(Each.Stat));
				Line.Base = 0.0f;
				Line.Modifiers.Add(Row);
			}
			System->SetStatInputs(MoveTemp(Stats));
		}

		/** A Heavy trap placed `Ahead` metres east of home, or null. */
		ACataclysmMinion* Place(float Ahead) const
		{
			return ACataclysmMinion::PlaceSkillTrap(Character, ECataclysmAbilitySlot::Heavy,
													Character->GetActorLocation()
														+ FVector(Ahead * TrapTimingMetre, 0.0f, 0.0f));
		}

		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** Whether `Trap` is one of those `System` recorded to be set off in the current chain. */
	bool TrapTimingRecorded(const UCataclysmAbilitySystemComponent* System, const ACataclysmMinion* Trap)
	{
		return System->SkillTrapsSetOffThisChain.ContainsByPredicate(
			[Trap](const TWeakObjectPtr<AActor>& Each) { return Each.Get() == Trap; });
	}

	/** The breakdown the generated rows of `Node` at `Points` give `Stat`, asked with `Tag`. */
	FCataclysmStatBreakdown TrapTimingGenerated(const UDataTable* Nodes, const UDataTable* Effects, const TCHAR* Node,
												int32 Points, const TCHAR* Stat, const TCHAR* Tag)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(Node), Points);
		const TMap<FName, TArray<FCataclysmStatModifier>> Granted =
			UCataclysmPassiveTree::ModifiersFor(Allocation, Nodes, Effects, {FName(TEXT("War"))});
		const TArray<FCataclysmStatModifier>* Rows = Granted.Find(FName(Stat));
		const FGameplayTagContainer Tags(FGameplayTag::RequestGameplayTag(FName(Tag), /*ErrorIfNotFound=*/false));
		return Rows ? UCataclysmStatPipeline::Evaluate(0.0f, *Rows, Tags, FCataclysmStatConditions())
					: FCataclysmStatBreakdown();
	}
}

#define CATACLYSM_TRAP_TIMING_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// Exact-time arming
// ---------------------------------------------------------------------------

CATACLYSM_TRAP_TIMING_TEST(FCataclysmTrapTimingExactArmingTest,
	"Cataclysm.SaboteurTrapTiming.ATrapArmsAtItsExactTimeNotAtTheNextQuarterSecond")
{
	using namespace CataclysmSaboteurTrapTimingTest;
	// TWO TRAPS PLACED AT ONE MOMENT: THE CONTROL with no row, and one with 30% arming speed, which arms at 1/1.3 =
	// 0.769 s. The world clock is run as play runs it, the quarter-second step and the timers together. 0.8 s in, the
	// quick trap is armed and the control is not; until 2026-11-25 the step was the only arming check, so the quick one
	// waited for the step at 1.0 s and a bonus below a third did nothing. 1.05 s in, both are armed.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FTrapTimingPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a player who can place traps"), Player.IsUsable()))
	{
		return false;
	}
	ACataclysmMinion* Plain = Player.Place(3.0f);
	Player.Holds({{ACataclysmMinion::TrapArmingSpeedStat, 30.0f, ECataclysmStatBucket::Increased}});
	ACataclysmMinion* Quick = Player.Place(6.0f);
	if (!TestNotNull(TEXT("set-up: the control's trap is placed"), Plain)
		|| !TestNotNull(TEXT("set-up: the quick trap is placed"), Quick))
	{
		return false;
	}
	TestEqual(TEXT("control: the plain trap arms at an age of 1 s"), Plain->SkillTrapArmsAtAgeSeconds, 1.0f, 0.001f);
	TestEqual(TEXT("30% faster arms at 1/1.3 of a second"), Quick->SkillTrapArmsAtAgeSeconds, 1.0f / 1.3f, 0.001f);

	CataclysmTestWorld::RunClock(World, 0.8f);
	TestFalse(TEXT("control: 0.8 s in, the plain trap is not armed"), Plain->bSkillTrapArmed);
	TestTrue(TEXT("0.8 s in, the quick trap is armed, before the step at 1.0 s"), Quick->bSkillTrapArmed);

	CataclysmTestWorld::RunClock(World, 0.25f);
	TestTrue(TEXT("1.05 s in, the plain trap is armed too"), Plain->bSkillTrapArmed);
	return true;
}

// ---------------------------------------------------------------------------
// Hair Trigger and Delayed Fuse's second
// ---------------------------------------------------------------------------

CATACLYSM_TRAP_TIMING_TEST(FCataclysmTrapTimingInstantAndFuseTest,
	"Cataclysm.SaboteurTrapTiming.HairTriggerArmsAtOnceAndBeatsDelayedFusesExtraSecond")
{
	using namespace CataclysmSaboteurTrapTimingTest;
	// FOUR TRAPS, EACH PLACED UNDER ITS OWN ROWS: THE CONTROL with none (1 s); Delayed Fuse's extra second (2 s);
	// Hair Trigger's flag (armed at placing); and both, where the flag wins (the C1 plan's judgement, ruled). The clock
	// then shows the fused one unarmed at 1.05 s and armed at 2.05 s.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FTrapTimingPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a player who can place traps"), Player.IsUsable()))
	{
		return false;
	}
	ACataclysmMinion* Plain = Player.Place(3.0f);
	Player.Holds({{ACataclysmMinion::TrapArmingSecondsAddedStat, 1.0f}});
	ACataclysmMinion* Fused = Player.Place(6.0f);
	Player.Holds({{ACataclysmMinion::TrapsArmInstantlyStat, 1.0f}});
	ACataclysmMinion* Instant = Player.Place(9.0f);
	Player.Holds({{ACataclysmMinion::TrapsArmInstantlyStat, 1.0f}, {ACataclysmMinion::TrapArmingSecondsAddedStat, 1.0f}});
	ACataclysmMinion* Both = Player.Place(12.0f);
	if (!TestTrue(TEXT("set-up: the four traps are placed"), Plain && Fused && Instant && Both))
	{
		return false;
	}
	TestFalse(TEXT("control: the plain trap is not armed at placing"), Plain->bSkillTrapArmed);
	TestEqual(TEXT("control: it arms at an age of 1 s"), Plain->SkillTrapArmsAtAgeSeconds, 1.0f, 0.001f);
	TestEqual(TEXT("Delayed Fuse's extra second: it arms at 2 s"), Fused->SkillTrapArmsAtAgeSeconds, 2.0f, 0.001f);
	TestTrue(TEXT("Hair Trigger's flag: armed at placing"), Instant->bSkillTrapArmed);
	TestTrue(TEXT("both held: armed at placing, the flag beating the extra second"), Both->bSkillTrapArmed);

	CataclysmTestWorld::RunClock(World, 1.05f);
	TestTrue(TEXT("1.05 s in, the plain trap is armed"), Plain->bSkillTrapArmed);
	TestFalse(TEXT("and the fused one is not"), Fused->bSkillTrapArmed);
	CataclysmTestWorld::RunClock(World, 1.0f);
	TestTrue(TEXT("2.05 s in, the fused one is armed"), Fused->bSkillTrapArmed);
	return true;
}

// ---------------------------------------------------------------------------
// Volatile Casing
// ---------------------------------------------------------------------------

CATACLYSM_TRAP_TIMING_TEST(FCataclysmTrapTimingVolatileCasingTest,
	"Cataclysm.SaboteurTrapTiming.VolatileCasingShortensArmingForEachOtherTrapDownToATenthOfASecond")
{
	using namespace CataclysmSaboteurTrapTimingTest;
	// FOUR TRAPS STANDING. THE CONTROL: a fifth placed with no row arms at 1 s, whatever stands. With 0.2 s less for each
	// other trap, a sixth (five others) arms at 1 - 1.0, held at the floor of 0.1 s; with 0.15 s less, a seventh (six
	// others) arms at 1 - 0.9 = 0.1 s by the sum; with 0.1 s less, an eighth (seven others) at 0.3 s. On the clock: the
	// floored trap is armed 0.15 s in, well before the first quarter-second step, and the 0.3 s one is not.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FTrapTimingPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a player who can place traps"), Player.IsUsable()))
	{
		return false;
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (!TestNotNull(TEXT("set-up: a trap standing"), Player.Place(3.0f + 3.0f * Index)))
		{
			return false;
		}
	}
	ACataclysmMinion* Plain = Player.Place(15.0f);
	Player.Holds({{ACataclysmMinion::TrapArmingSecondsLessPerOtherTrapStat, 0.2f}});
	ACataclysmMinion* Floored = Player.Place(18.0f);
	Player.Holds({{ACataclysmMinion::TrapArmingSecondsLessPerOtherTrapStat, 0.15f}});
	ACataclysmMinion* AtTheFloor = Player.Place(21.0f);
	Player.Holds({{ACataclysmMinion::TrapArmingSecondsLessPerOtherTrapStat, 0.1f}});
	ACataclysmMinion* Shortened = Player.Place(24.0f);
	if (!TestTrue(TEXT("set-up: the four traps are placed"), Plain && Floored && AtTheFloor && Shortened))
	{
		return false;
	}
	TestEqual(TEXT("control: with four others standing and no row, 1 s"), Plain->SkillTrapArmsAtAgeSeconds, 1.0f, 0.001f);
	TestEqual(TEXT("0.2 s less for each of five others: held at the floor, 0.1 s"), Floored->SkillTrapArmsAtAgeSeconds,
			  ACataclysmMinion::SkillTrapArmingFloorSeconds, 0.001f);
	TestEqual(TEXT("0.15 s less for each of six others: 0.1 s"), AtTheFloor->SkillTrapArmsAtAgeSeconds, 0.1f, 0.001f);
	TestEqual(TEXT("0.1 s less for each of seven others: 0.3 s"), Shortened->SkillTrapArmsAtAgeSeconds, 0.3f, 0.001f);

	CataclysmTestWorld::RunClock(World, 0.15f);
	TestTrue(TEXT("0.15 s in, the floored trap is armed"), Floored->bSkillTrapArmed);
	TestFalse(TEXT("and the 0.3 s one is not"), Shortened->bSkillTrapArmed);
	TestFalse(TEXT("control: nor the plain one"), Plain->bSkillTrapArmed);
	return true;
}

// ---------------------------------------------------------------------------
// Delayed Fuse: a trap still arming is not set off
// ---------------------------------------------------------------------------

CATACLYSM_TRAP_TIMING_TEST(FCataclysmTrapTimingSparesTheArmingTest,
	"Cataclysm.SaboteurTrapTiming.DelayedFuseKeepsATrapStillArmingFromBeingSetOff")
{
	using namespace CataclysmSaboteurTrapTimingTest;
	// TWO PLAYERS 100 M APART, EACH WITH ONE ARMED TRAP AND ONE STILL ARMING. A forced set-off of all of a player's
	// traps, as a row action makes one: THE CONTROL, without the flag, records both; with Delayed Fuse's flag, the armed
	// trap is recorded and the arming one is passed over.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FTrapTimingPlayer Plain(World, FVector::ZeroVector);
	const FTrapTimingPlayer Fused(World, FVector(100.0f * TrapTimingMetre, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: two players who can place traps"), Plain.IsUsable() && Fused.IsUsable()))
	{
		return false;
	}
	ACataclysmMinion* PlainArmed = Plain.Place(3.0f);
	ACataclysmMinion* FusedArmed = Fused.Place(3.0f);
	CataclysmTestWorld::RunClock(World, 1.05f);
	ACataclysmMinion* PlainArming = Plain.Place(6.0f);
	ACataclysmMinion* FusedArming = Fused.Place(6.0f);
	if (!TestTrue(TEXT("set-up: four traps placed"), PlainArmed && FusedArmed && PlainArming && FusedArming)
		|| !TestTrue(TEXT("set-up: the first of each armed, the second still arming"),
					 PlainArmed->bSkillTrapArmed && FusedArmed->bSkillTrapArmed && !PlainArming->bSkillTrapArmed
						 && !FusedArming->bSkillTrapArmed))
	{
		return false;
	}
	Fused.Holds({{ACataclysmMinion::TrapsNotSetOffWhileArmingStat, 1.0f}});

	const int32 PlainCount = ACataclysmMinion::SetOffSkillTrapsNear(Plain.Character, /*Place=*/nullptr, 0.0f,
																	/*bNearestOnly=*/false, /*Except=*/nullptr,
																	/*bEvenForADeadPlayer=*/false);
	const int32 FusedCount = ACataclysmMinion::SetOffSkillTrapsNear(Fused.Character, /*Place=*/nullptr, 0.0f,
																	/*bNearestOnly=*/false, /*Except=*/nullptr,
																	/*bEvenForADeadPlayer=*/false);
	TestEqual(TEXT("control: without the flag both traps are recorded"), PlainCount, 2);
	TestTrue(TEXT("control: the arming one among them"), TrapTimingRecorded(Plain.System, PlainArming));
	TestEqual(TEXT("with the flag one trap is recorded"), FusedCount, 1);
	TestTrue(TEXT("the armed one"), TrapTimingRecorded(Fused.System, FusedArmed));
	TestFalse(TEXT("and not the one still arming"), TrapTimingRecorded(Fused.System, FusedArming));
	return true;
}

// ---------------------------------------------------------------------------
// The rows, as the game loads them
// ---------------------------------------------------------------------------

CATACLYSM_TRAP_TIMING_TEST(FCataclysmTrapTimingRowsTest,
	"Cataclysm.SaboteurTrapTiming.TheGeneratedRowsGiveEachFigureAtItsPoints")
{
	using namespace CataclysmSaboteurTrapTimingTest;
	// THE GENERATED ROWS, judged by the pipeline with a trap's or a gadget's tag. Hair Trigger: no flag at 4 points (the
	// control), the flag at 5. Hair Wire: no arming speed at 3 points (the control), 20% at 4. Volatile Casing: 0.1 s a
	// point, 0.8 at 8. Minefield: 10% arming speed a point, 80% at 8. Field Engineer: 12% gadget attack speed a point,
	// 96% at 8. Delayed Fuse at 6 points: 90% trap damage, one extra second, the flag.
	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table loads"), Nodes)
		|| !TestNotNull(TEXT("set-up: the effect table loads"), Effects))
	{
		return false;
	}
	struct FNodeRows
	{
		const TCHAR* Node;
		int32 Rows;
	};
	const TArray<FNodeRows> Expected = {
		{TEXT("Saboteur_basic_trunk_001"), 2}, {TEXT("Saboteur_basic_trap_stem_002"), 2},
		{TEXT("Saboteur_basic_trap_stem_003"), 1}, {TEXT("Saboteur_basic_trap_qty_001"), 2},
		{TEXT("Saboteur_basic_gadget_stem_001"), 1}, {TEXT("Saboteur_basic_trap_ql_003"), 3}};
	for (const FNodeRows& Each : Expected)
	{
		const int32 Held = UCataclysmPassiveTree::EffectsFor(Effects, FName(Each.Node)).Num();
		if (Held != Each.Rows)
		{
			AddError(FString::Printf(
				TEXT("DT_PassiveEffects holds %d rows for %s, not %d. If game/Data/PassiveEffects.csv has them, the "
					 "asset is older than the CSV: run  python tools/run_editor_python.py "
					 "tools/generate_datatable_assets.py"),
				Held, Each.Node, Each.Rows));
			return false;
		}
	}
	const TCHAR* Trap = TEXT("Type.Trap");
	const auto Flat = [&](const TCHAR* Node, int32 Points, const TCHAR* Stat, const TCHAR* Tag)
	{
		return TrapTimingGenerated(Nodes, Effects, Node, Points, Stat, Tag).Final;
	};
	const auto Increases = [&](const TCHAR* Node, int32 Points, const TCHAR* Stat, const TCHAR* Tag)
	{
		return TrapTimingGenerated(Nodes, Effects, Node, Points, Stat, Tag).SumOfIncreases;
	};

	TestEqual(TEXT("control: Hair Trigger at 4 points, no flag"),
			  Flat(TEXT("Saboteur_basic_trunk_001"), 4, ACataclysmMinion::TrapsArmInstantlyStat, Trap), 0.0f, 0.001f);
	TestEqual(TEXT("Hair Trigger at 5 points, the flag"),
			  Flat(TEXT("Saboteur_basic_trunk_001"), 5, ACataclysmMinion::TrapsArmInstantlyStat, Trap), 1.0f, 0.001f);
	TestEqual(TEXT("control: Hair Wire at 3 points, no arming speed"),
			  Increases(TEXT("Saboteur_basic_trap_stem_002"), 3, ACataclysmMinion::TrapArmingSpeedStat, Trap), 0.0f,
			  0.001f);
	TestEqual(TEXT("Hair Wire at 4 points, 20% arming speed"),
			  Increases(TEXT("Saboteur_basic_trap_stem_002"), 4, ACataclysmMinion::TrapArmingSpeedStat, Trap), 20.0f,
			  0.001f);
	TestEqual(TEXT("Volatile Casing at 8 points, 0.8 s less for each other trap"),
			  Flat(TEXT("Saboteur_basic_trap_stem_003"), 8, ACataclysmMinion::TrapArmingSecondsLessPerOtherTrapStat,
				   Trap),
			  0.8f, 0.001f);
	TestEqual(TEXT("Minefield at 8 points, 80% arming speed"),
			  Increases(TEXT("Saboteur_basic_trap_qty_001"), 8, ACataclysmMinion::TrapArmingSpeedStat, Trap), 80.0f,
			  0.001f);
	TestEqual(TEXT("Field Engineer at 8 points, 96% gadget attack speed"),
			  Increases(TEXT("Saboteur_basic_gadget_stem_001"), 8, TEXT("minion_attack_speed"), TEXT("Type.Gadget")),
			  96.0f, 0.001f);
	TestEqual(TEXT("control: Field Engineer's row does not reach a trap"),
			  Increases(TEXT("Saboteur_basic_gadget_stem_001"), 8, TEXT("minion_attack_speed"), Trap), 0.0f, 0.001f);
	TestEqual(TEXT("Delayed Fuse at 6 points, 90% trap damage"),
			  Increases(TEXT("Saboteur_basic_trap_ql_003"), 6, TEXT("minion_damage"), Trap), 90.0f, 0.001f);
	TestEqual(TEXT("one extra second"),
			  Flat(TEXT("Saboteur_basic_trap_ql_003"), 6, ACataclysmMinion::TrapArmingSecondsAddedStat, Trap), 1.0f,
			  0.001f);
	TestEqual(TEXT("and the flag"),
			  Flat(TEXT("Saboteur_basic_trap_ql_003"), 6, ACataclysmMinion::TrapsNotSetOffWhileArmingStat, Trap), 1.0f,
			  0.001f);
	return true;
}

#undef CATACLYSM_TRAP_TIMING_TEST

#endif // WITH_AUTOMATION_TESTS
