// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWarFervour.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Fervour gained from other sources and the hold before decay (the Death trees' slice C4b1, 2026-11-25): the Soul
 * Collector's Any Fuel, Borrowed Time and Kindred Fires, and `UCataclysmFervour::RaisePool`, the one way a gain reaches
 * the pool. The mechanism of each is read with stats given by hand, so each figure is exact; one test reads the three
 * nodes' real rows. `tools/tests/test_every_gain_to_the_pool_goes_through_raise_pool.py` checks that every gain in the
 * source calls the helper these tests drive.
 */

namespace CataclysmFervourGainsTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;
	using CataclysmTestWorld::RunClock;

	/** An actor holding health and an empty Fervour pool of 100, able to be given stat lines. */
	struct FFervourGainsHolder
	{
		explicit FFervourGainsHolder(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// Raw pointers, because AddAttributeSetSubobject deduces its type from the argument.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource = NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewResource);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												   100.0f);
			SetFervour(0.0f);
		}

		~FFervourGainsHolder()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void SetFervour(float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
												   Value);
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	/** A row of a stat line, optionally under a condition. */
	FCataclysmStatModifier GainsRow(ECataclysmStatBucket Bucket, float Value,
									ECataclysmStatCondition Condition = ECataclysmStatCondition::Always,
									float ConditionValue = 0.0f)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = Bucket;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		Row.Condition = Condition;
		Row.ConditionValue = ConditionValue;
		return Row;
	}

	/** A line of flat rows on a stat, with no base. */
	void FlatLine(TMap<FName, FCataclysmStatInputs>& Lines, const TCHAR* Stat, float Value)
	{
		Lines.FindOrAdd(FName(Stat)).Modifiers = {GainsRow(ECataclysmStatBucket::Flat, Value)};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourGainsAnyFuelTest,
	"Cataclysm.FervourGains.AnyFuelRaisesAGainThatIsNotAKillsAndLeavesAKillsAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Any Fuel's shape, given by hand at +50% so each figure is whole: the generation rate (base 100) with an increased row
 * under `fervour_gain_not_from_a_kill`. A cast paying 10 pays 15; a kill paying 10 still pays 10, because the kill's
 * payment is marked. THE CONTROL: without the row both pay 10.
 */
bool FCataclysmFervourGainsAnyFuelTest::RunTest(const FString&)
{
	using namespace CataclysmFervourGainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FFervourGainsHolder Holder(World);

	const auto Lines = [](bool bWithAnyFuel)
	{
		TMap<FName, FCataclysmStatInputs> Made;
		FlatLine(Made, UCataclysmFervour::PerCastStat, 10.0f);
		FlatLine(Made, UCataclysmFervour::PerKillStat, 10.0f);
		FCataclysmStatInputs& Rate = Made.FindOrAdd(FName(UCataclysmAbilitySystemComponent::ClassResourceGenerationStat));
		Rate.Base = UCataclysmAbilitySystemComponent::NormalClassResourceGeneration;
		if (bWithAnyFuel)
		{
			Rate.Modifiers = {GainsRow(ECataclysmStatBucket::Increased, 50.0f,
									   ECataclysmStatCondition::FervourGainNotFromAKill)};
		}
		return Made;
	};
	const auto Paid = [&Holder](bool bKill)
	{
		Holder.SetFervour(0.0f);
		return bKill ? UCataclysmFervour::GainOnOwnKill(Holder.AbilitySystem, nullptr)
					 : UCataclysmFervour::GainForCast(Holder.AbilitySystem);
	};

	Holder.AbilitySystem->SetStatInputs(Lines(false));
	TestEqual(TEXT("control: without Any Fuel a cast pays 10"), Paid(false), 10.0f, 0.001f);
	TestEqual(TEXT("control: and a kill pays 10"), Paid(true), 10.0f, 0.001f);

	Holder.AbilitySystem->SetStatInputs(Lines(true));
	TestEqual(TEXT("with Any Fuel at 50%, a cast pays 15"), Paid(false), 15.0f, 0.001f);
	TestEqual(TEXT("and a kill still pays 10, its payment marked"), Paid(true), 10.0f, 0.001f);
	TestFalse(TEXT("the marker is closed after the kill's payment"), Holder.AbilitySystem->IsPayingForAKill());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourGainsKindredFiresTest,
	"Cataclysm.FervourGains.KindredFiresRestartsTheHoldAtTheLastKillsFullLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Kindred Fires, with the starting node's two grace rows given by hand: 4 seconds for a common kill, 8 for an Elite or
 * stronger. Before any kill, a cast's gain holds decay off for a common kill's 4 seconds: held 3.7 seconds later, not
 * 4.3. After an Elite kill's 8-second hold, a cast 7 seconds in restarts it at 8: held 7.5 seconds later, not 8.5.
 * THE CONTROL: without the node, a cast's gain before any kill holds nothing.
 */
bool FCataclysmFervourGainsKindredFiresTest::RunTest(const FString&)
{
	using namespace CataclysmFervourGainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FFervourGainsHolder Holder(World);
	UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;

	const auto Lines = [](bool bWithKindredFires)
	{
		TMap<FName, FCataclysmStatInputs> Made;
		FlatLine(Made, UCataclysmFervour::PerCastStat, 10.0f);
		Made.FindOrAdd(FName(UCataclysmFervour::KillDecayGraceSecondsStat)).Modifiers = {
			GainsRow(ECataclysmStatBucket::Flat, 4.0f, ECataclysmStatCondition::TargetRarityIs, 0.0f),
			GainsRow(ECataclysmStatBucket::Flat, 8.0f, ECataclysmStatCondition::TargetRarityAtLeast, 1.0f)};
		if (bWithKindredFires)
		{
			FlatLine(Made, UCataclysmFervour::HoldRestartedByAnyGainStat, 1.0f);
		}
		return Made;
	};

	System->SetStatInputs(Lines(false));
	UCataclysmFervour::GainForCast(System);
	TestFalse(TEXT("control: without Kindred Fires a cast before any kill holds nothing"), System->KillDecayIsHeld());

	System->SetStatInputs(Lines(true));
	TestEqual(TEXT("set-up: before any kill the full length is a common kill's 4 seconds"),
			  UCataclysmFervour::KillDecayHoldFullSeconds(System), 4.0f, 0.001f);
	UCataclysmFervour::GainForCast(System);
	RunClock(World, 3.7f);
	TestTrue(TEXT("with it, 3.7 seconds after the cast decay is held"), System->KillDecayIsHeld());
	RunClock(World, 0.6f);
	TestFalse(TEXT("and 4.3 seconds after, it is not"), System->KillDecayIsHeld());

	System->NoteOwnKill(/*GraceSeconds=*/8.0f);
	RunClock(World, 7.0f);
	UCataclysmFervour::GainForCast(System);
	RunClock(World, 7.5f);
	TestTrue(TEXT("after an Elite kill, a cast 7 seconds in restarts the hold at 8: held 7.5 seconds later"),
			 System->KillDecayIsHeld());
	RunClock(World, 1.0f);
	TestFalse(TEXT("and not 8.5 seconds later"), System->KillDecayIsHeld());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourGainsKillPaymentTest,
	"Cataclysm.FervourGains.KindredFiresLeavesAKillsHoldToTheKillsOwnStamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Kindred Fires and 1 Fervour a kill, as the coordinating session ruled on 2026-11-25: a kill's payment does not
 * restart the hold; the kill's own stamp sets it. An Elite kill holds 8 seconds; a common kill 6 seconds in is paid and
 * then stamped with its own 4 seconds, in the order the player's kill handler uses. The hold is then 4 seconds from the
 * common kill: held 3.7 seconds after it, not 4.3. A restart at the Elite's 8 on the payment would still hold. THE
 * CONTROL: a cast once that hold has run out restarts it at its full length, now the common kill's 4: held 3.7 seconds
 * later, not 4.3. AND THE PAYMENT ALONE (issue #2936): an Elite kill's payment 6 seconds into its own 8-second hold, with
 * no stamp after it, leaves the hold to end at 8.
 */
bool FCataclysmFervourGainsKillPaymentTest::RunTest(const FString&)
{
	using namespace CataclysmFervourGainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FFervourGainsHolder Holder(World);
	UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;

	TMap<FName, FCataclysmStatInputs> Lines;
	FlatLine(Lines, UCataclysmFervour::PerCastStat, 10.0f);
	FlatLine(Lines, UCataclysmFervour::PerKillStat, 1.0f);
	FlatLine(Lines, UCataclysmFervour::HoldRestartedByAnyGainStat, 1.0f);
	System->SetStatInputs(MoveTemp(Lines));

	System->NoteOwnKill(/*GraceSeconds=*/8.0f);
	RunClock(World, 6.0f);
	TestEqual(TEXT("set-up: the common kill pays 1"), UCataclysmFervour::GainOnOwnKill(System, nullptr), 1.0f, 0.001f);
	System->NoteOwnKill(/*GraceSeconds=*/4.0f);
	RunClock(World, 3.7f);
	TestTrue(TEXT("a common kill inside an Elite kill's hold: held 3.7 seconds after it"), System->KillDecayIsHeld());
	RunClock(World, 0.6f);
	TestFalse(TEXT("and not 4.3 seconds after: its payment did not restart the hold at the Elite's 8"),
			  System->KillDecayIsHeld());

	UCataclysmFervour::GainForCast(System);
	RunClock(World, 3.7f);
	TestTrue(TEXT("control: a cast restarts the hold at its full length, the common kill's 4: held 3.7 seconds later"),
			 System->KillDecayIsHeld());
	RunClock(World, 0.6f);
	TestFalse(TEXT("and not 4.3 seconds later"), System->KillDecayIsHeld());

	// AND A KILL'S PAYMENT ALONE, WITH NO STAMP AFTER IT, issue #2936. Above, the common kill's own stamp follows its
	// payment, and a stamp that overwrote the hold rather than lengthening it (guard break B-2, applied with C4b1-1 in
	// window death3-w4's proof A) would hide a payment that restarted the hold: that proof's C4b1-1 went unseen here.
	// So the payment is read by itself: an Elite kill holds 8 seconds, its payment 6 seconds in restarts nothing, and the
	// hold ends at the kill's 8, not at a restart's 6 + 8.
	System->NoteOwnKill(/*GraceSeconds=*/8.0f);
	RunClock(World, 6.0f);
	Holder.SetFervour(0.0f);
	TestEqual(TEXT("set-up: a kill's payment alone pays 1"), UCataclysmFervour::GainOnOwnKill(System, nullptr), 1.0f,
			  0.001f);
	RunClock(World, 1.7f);
	TestTrue(TEXT("control: 7.7 seconds after the Elite kill its hold still runs"), System->KillDecayIsHeld());
	RunClock(World, 0.6f);
	TestFalse(TEXT("and 8.3 seconds after, it has ended: the payment alone restarted nothing"),
			  System->KillDecayIsHeld());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourGainsBorrowedTimeTest,
	"Cataclysm.FervourGains.BorrowedTimeLengthensARunningHoldUpToItsFullLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Borrowed Time at its 6 points, 2 seconds, after a kill whose hold is 4 seconds. A cast 1 second in lengthens it to 5
 * seconds from the kill, its full length from the cast, and not 6: held 4.7 seconds after the kill, not 5.3. THE
 * CONTROLS: a cast once decay has begun holds nothing, and a kill's own payment 1 second into another hold lengthens
 * nothing, so that hold ends at 4.
 */
bool FCataclysmFervourGainsBorrowedTimeTest::RunTest(const FString&)
{
	using namespace CataclysmFervourGainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FFervourGainsHolder Holder(World);
	UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;

	TMap<FName, FCataclysmStatInputs> Lines;
	FlatLine(Lines, UCataclysmFervour::PerCastStat, 10.0f);
	FlatLine(Lines, UCataclysmFervour::PerKillStat, 1.0f);
	FlatLine(Lines, UCataclysmFervour::HoldExtendedOnGainSecondsStat, 2.0f);
	System->SetStatInputs(MoveTemp(Lines));

	System->NoteOwnKill(/*GraceSeconds=*/4.0f);
	RunClock(World, 1.0f);
	UCataclysmFervour::GainForCast(System);
	RunClock(World, 3.7f);
	TestTrue(TEXT("a cast 1 second in lengthens the hold: held 4.7 seconds after the kill"), System->KillDecayIsHeld());
	RunClock(World, 0.6f);
	TestFalse(TEXT("but only to its full length, 5 seconds after the kill and not 6"), System->KillDecayIsHeld());

	UCataclysmFervour::GainForCast(System);
	TestFalse(TEXT("control: once decay has begun a cast holds nothing"), System->KillDecayIsHeld());

	System->NoteOwnKill(/*GraceSeconds=*/4.0f);
	RunClock(World, 1.0f);
	UCataclysmFervour::GainOnOwnKill(System, nullptr);
	RunClock(World, 3.3f);
	TestFalse(TEXT("control: a kill's payment 1 second in lengthens nothing, so 4.3 seconds after it is not held"),
			  System->KillDecayIsHeld());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourGainsRaisePoolTest,
	"Cataclysm.FervourGains.RaisePoolClampsAtTheMaximumAndAFullBarRestartsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `UCataclysmFervour::RaisePool` with Kindred Fires and a grace of 4 seconds, on a character that has never killed. At
 * a full bar of 100, a gain of 5 raises nothing and restarts nothing: only an actual rise counts, as ruled. At 95, a
 * gain of 10 raises 5, leaves 100 held, and holds decay off. THE CONTROL is the first: the same gain on a full bar.
 */
bool FCataclysmFervourGainsRaisePoolTest::RunTest(const FString&)
{
	using namespace CataclysmFervourGainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FFervourGainsHolder Holder(World);
	UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;

	TMap<FName, FCataclysmStatInputs> Lines;
	FlatLine(Lines, UCataclysmFervour::KillDecayGraceSecondsStat, 4.0f);
	FlatLine(Lines, UCataclysmFervour::HoldRestartedByAnyGainStat, 1.0f);
	System->SetStatInputs(MoveTemp(Lines));
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();

	Holder.SetFervour(100.0f);
	TestEqual(TEXT("control: at a full bar a gain of 5 raises nothing"), UCataclysmFervour::RaisePool(System, 5.0f),
			  0.0f, 0.001f);
	TestFalse(TEXT("and restarts nothing"), System->KillDecayIsHeld());

	Holder.SetFervour(95.0f);
	TestEqual(TEXT("at 95, a gain of 10 raises 5"), UCataclysmFervour::RaisePool(System, 10.0f), 5.0f, 0.001f);
	TestEqual(TEXT("so 100 is held"), System->GetNumericAttribute(Pool), 100.0f, 0.001f);
	TestTrue(TEXT("and the rise holds decay off"), System->KillDecayIsHeld());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourGainsRealRowsTest,
	"Cataclysm.FervourGains.TheThreeNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The three nodes' real rows, read from the passive effects table: Any Fuel an increased row on the generation rate
 * under the new condition; Borrowed Time a second at each third point; Kindred Fires a switch of 1.
 */
bool FCataclysmFervourGainsRealRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}

	const TArray<const FCataclysmPassiveEffectRow*> Fuel =
		UCataclysmPassiveTree::EffectsFor(Effects, FName(TEXT("SoulCollector_basic_b_c0")));
	if (TestEqual(TEXT("Any Fuel: one row"), Fuel.Num(), 1))
	{
		TestEqual(TEXT("Any Fuel: the generation rate"), Fuel[0]->Stat,
				  FString(UCataclysmAbilitySystemComponent::ClassResourceGenerationStat));
		TestEqual(TEXT("Any Fuel: increased"), Fuel[0]->ValueKind, FString(TEXT("increased")));
		TestEqual(TEXT("Any Fuel: not from a kill"), Fuel[0]->Condition, FString(TEXT("fervour_gain_not_from_a_kill")));
	}

	TArray<int32> Steps;
	for (const FCataclysmPassiveEffectRow* Row :
		 UCataclysmPassiveTree::EffectsFor(Effects, FName(TEXT("SoulCollector_basic_b_c1"))))
	{
		Steps.Add(Row->MinPoints);
		TestEqual(TEXT("Borrowed Time: the hold's seconds"), Row->Stat,
				  FString(UCataclysmFervour::HoldExtendedOnGainSecondsStat));
		TestEqual(TEXT("Borrowed Time: 1 second each"), Row->ValuePerPoint, 1.0f, 0.001f);
	}
	Steps.Sort();
	TestTrue(TEXT("Borrowed Time: a row at 3 points and at 6"), Steps == TArray<int32>({3, 6}));

	const TArray<const FCataclysmPassiveEffectRow*> Kindred =
		UCataclysmPassiveTree::EffectsFor(Effects, FName(TEXT("SoulCollector_keystone_b_kC")));
	if (TestEqual(TEXT("Kindred Fires: one row"), Kindred.Num(), 1))
	{
		TestEqual(TEXT("Kindred Fires: the switch"), Kindred[0]->Stat,
				  FString(UCataclysmFervour::HoldRestartedByAnyGainStat));
		TestEqual(TEXT("Kindred Fires: 1"), Kindred[0]->ValuePerPoint, 1.0f, 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourGainsWarGainsTest,
	"Cataclysm.FervourGains.AWarRateAndCleavesNetGainAreGainsKindredFiresHears",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Death trees' bundle 3 merge (2026-11-25, obligations 14 and 17): the War line's gains go through
 * `UCataclysmFervour::RaisePool`, so Kindred Fires acts on them as on any gain. With the node and a common kill's
 * 4-second grace: a block's 5 Fervour (`FCataclysmWarFervour::GainOnBlock`) holds decay off, and so does Cleave's net
 * gain (3 enemies struck, 5 paid and 8 gained for each of the 2 beyond the first) once the first hold has run out.
 * THE CONTROL: without Kindred Fires the same block holds nothing.
 */
bool FCataclysmFervourGainsWarGainsTest::RunTest(const FString&)
{
	using namespace CataclysmFervourGainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FFervourGainsHolder Holder(World);
	UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;

	const auto Lines = [](bool bWithKindredFires)
	{
		TMap<FName, FCataclysmStatInputs> Made;
		FlatLine(Made, FCataclysmWarFervour::PerBlockStat, 5.0f);
		FlatLine(Made, FCataclysmWarFervour::CostPerExtraEnemyHitStat, 5.0f);
		FlatLine(Made, FCataclysmWarFervour::GainPerExtraEnemyHitPaidStat, 8.0f);
		Made.FindOrAdd(FName(UCataclysmFervour::KillDecayGraceSecondsStat)).Modifiers = {
			GainsRow(ECataclysmStatBucket::Flat, 4.0f, ECataclysmStatCondition::TargetRarityIs, 0.0f)};
		if (bWithKindredFires)
		{
			FlatLine(Made, UCataclysmFervour::HoldRestartedByAnyGainStat, 1.0f);
		}
		return Made;
	};

	System->SetStatInputs(Lines(false));
	TestEqual(TEXT("set-up: a block pays its 5"), FCataclysmWarFervour::GainOnBlock(System), 5.0f, 0.001f);
	TestFalse(TEXT("control: without Kindred Fires a block's gain holds nothing"), System->KillDecayIsHeld());

	System->SetStatInputs(Lines(true));
	Holder.SetFervour(0.0f);
	TestEqual(TEXT("with it, a block still pays its 5"), FCataclysmWarFervour::GainOnBlock(System), 5.0f, 0.001f);
	TestTrue(TEXT("and its gain holds decay off, as any gain does"), System->KillDecayIsHeld());
	RunClock(World, 4.3f);
	TestFalse(TEXT("set-up: 4.3 seconds later the hold has run out"), System->KillDecayIsHeld());

	Holder.SetFervour(50.0f);
	TestEqual(TEXT("Cleave's net gain over 3 enemies: 2 times (8 - 5)"),
			  FCataclysmWarFervour::TradeForEnemiesStruckTogether(System, FGameplayTagContainer(), 3), 6.0f, 0.001f);
	TestTrue(TEXT("and that net gain holds decay off too"), System->KillDecayIsHeld());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
