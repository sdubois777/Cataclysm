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
 * How Fervour is lost, and what fills past the bar (the Death trees' slice C6c, 2026-11-25, as ruled): Miser's Grip's
 * floor stops only the decay; Soulbound takes Fervour for health lost, keeping the remainder; Overflow turns whole
 * Fervour above the bar into attack speed stacks sharing one window; Unbroken Chain lengthens a passive row's kill
 * stack and Soul Tide's window, and not an enchantment's kill stack.
 */

namespace CataclysmFervourLossAndChainTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;
	using CataclysmTestWorld::RunClock;

	/** An actor holding 1,000 health and a Fervour pool of 100 held at `Held`, able to be given lines and actions. */
	struct FLossHolder
	{
		FLossHolder(UWorld* World, float Held)
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
			SetFervour(Held);
		}

		~FLossHolder()
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

		float Fervour() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	FCataclysmStatModifier LossRow(ECataclysmStatBucket Bucket, float Value,
								   ECataclysmStatScale Scale = ECataclysmStatScale::Fixed, int32 Cap = 0)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = Bucket;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		Row.Scale = Scale;
		Row.ScaleStep = Scale == ECataclysmStatScale::Fixed ? 0.0f : 1.0f;
		Row.ScaleMaxSteps = Cap;
		return Row;
	}

	/** One line a stat, base `Base` and the one row given. */
	void AddLine(TMap<FName, FCataclysmStatInputs>& Lines, const TCHAR* Stat, float Base, const FCataclysmStatModifier& Row)
	{
		FCataclysmStatInputs& Line = Lines.FindOrAdd(FName(Stat));
		Line.Base = Base;
		Line.Modifiers.Add(Row);
	}

	/** An own stack granted on `kill`, keyed `Key`, lasting `Seconds`, at most 5. */
	FCataclysmPoolAction KillStack(const TCHAR* Key, float Seconds)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("kill"));
		Action.StackKey = FName(Key);
		Action.StackSeconds = Seconds;
		Action.StackCap = 5;
		Action.TriggerKey = FName(*FString::Printf(TEXT("%s:kill"), Key));
		return Action;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourLossFloorTest,
	"Cataclysm.FervourLossAndChain.MisersGripStopsOnlyTheDecayAtItsFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A holder that has never killed, holding 50 of 100, decaying 10 a second, with Miser's Grip at 8 points (24%): a 10
 * second decay step stops at 24. THE CONTROL, AS RULED: Soulbound's loss is not held by the floor, so 10% of health lost
 * at 2% a Fervour takes 24 to 19.
 */
bool FCataclysmFervourLossFloorTest::RunTest(const FString&)
{
	using namespace CataclysmFervourLossAndChainTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FLossHolder Holder(World, 50.0f);
	TMap<FName, FCataclysmStatInputs> Lines;
	AddLine(Lines, UCataclysmFervour::KillDecayPerSecondStat, 0.0f, LossRow(ECataclysmStatBucket::Flat, 10.0f));
	AddLine(Lines, UCataclysmFervour::DecayFloorPercentStat, 0.0f, LossRow(ECataclysmStatBucket::Flat, 24.0f));
	AddLine(Lines, UCataclysmFervour::HealthPercentLostPerFervourLostStat, 0.0f,
			LossRow(ECataclysmStatBucket::Flat, 2.0f));
	Holder.AbilitySystem->SetStatInputs(MoveTemp(Lines));

	UCataclysmFervour::KillDecayStep(Holder.AbilitySystem, 10.0f);
	TestEqual(TEXT("the decay stops at the floor, 24"), Holder.Fervour(), 24.0f, 0.001f);
	UCataclysmFervour::LoseFromDamage(Holder.AbilitySystem, 100.0f);
	TestEqual(TEXT("control: Soulbound's loss goes below it, 19"), Holder.Fervour(), 19.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourLossSoulboundTest,
	"Cataclysm.FervourLossAndChain.SoulboundTakesAFervourForEachTwoPercentAndKeepsTheRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Soulbound at 2% a Fervour, 1,000 maximum health, holding 50: 30 health lost (3%) takes 1 and keeps 1%; 10 more (1%)
 * makes 2% and takes 1 more. THE CONTROL: 5 health lost (0.5%) takes nothing, and at 0 held nothing goes below 0.
 */
bool FCataclysmFervourLossSoulboundTest::RunTest(const FString&)
{
	using namespace CataclysmFervourLossAndChainTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FLossHolder Holder(World, 50.0f);
	TMap<FName, FCataclysmStatInputs> Lines;
	AddLine(Lines, UCataclysmFervour::HealthPercentLostPerFervourLostStat, 0.0f,
			LossRow(ECataclysmStatBucket::Flat, 2.0f));
	Holder.AbilitySystem->SetStatInputs(MoveTemp(Lines));

	UCataclysmFervour::LoseFromDamage(Holder.AbilitySystem, 30.0f);
	TestEqual(TEXT("3% lost: 1 taken"), Holder.Fervour(), 49.0f, 0.001f);
	UCataclysmFervour::LoseFromDamage(Holder.AbilitySystem, 10.0f);
	TestEqual(TEXT("1% more, the 1% kept makes 2%: 1 more taken"), Holder.Fervour(), 48.0f, 0.001f);
	UCataclysmFervour::LoseFromDamage(Holder.AbilitySystem, 5.0f);
	TestEqual(TEXT("control: 0.5% takes nothing"), Holder.Fervour(), 48.0f, 0.001f);
	Holder.SetFervour(0.0f);
	UCataclysmFervour::LoseFromDamage(Holder.AbilitySystem, 100.0f);
	TestEqual(TEXT("control: at 0 held, nothing below 0"), Holder.Fervour(), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourLossOverflowTest,
	"Cataclysm.FervourLossAndChain.OverflowGivesAStackForEachWholeFervourAboveTheBar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Overflow with a 5 second window and Overflow's attack speed row (1% a stack, at most 30) on a line of 100. At 98
 * held, a gain of 4.6 fills 2 and overflows 2.6: two stacks, 102 (the 0.6 lost). 5.1 seconds later the window has run
 * out: 100. At a full bar, a gain of 40: 40 stacks, held at 130 by the cap. THE CONTROL is the second reading.
 */
bool FCataclysmFervourLossOverflowTest::RunTest(const FString&)
{
	using namespace CataclysmFervourLossAndChainTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FLossHolder Holder(World, 98.0f);
	TMap<FName, FCataclysmStatInputs> Lines;
	AddLine(Lines, UCataclysmFervour::OverflowAttackSpeedSecondsStat, 0.0f, LossRow(ECataclysmStatBucket::Flat, 5.0f));
	AddLine(Lines, TEXT("attack_speed"), 100.0f,
			LossRow(ECataclysmStatBucket::Increased, 1.0f, ECataclysmStatScale::PerFervourOverflowStack, 30));
	Holder.AbilitySystem->SetStatInputs(MoveTemp(Lines));
	const auto AttackSpeed = [&Holder]()
	{
		return Holder.AbilitySystem->StatForSkill(FName(TEXT("attack_speed")), FGameplayTagContainer(), 0.0f);
	};

	UCataclysmFervour::RaisePool(Holder.AbilitySystem, 4.6f);
	TestEqual(TEXT("2.6 above the bar: two stacks"), Holder.AbilitySystem->FervourOverflowStacksHeld(), 2);
	TestEqual(TEXT("so 102"), AttackSpeed(), 102.0f, 0.001f);
	RunClock(World, 5.1f);
	TestEqual(TEXT("control: the window has run out, 100"), AttackSpeed(), 100.0f, 0.001f);
	UCataclysmFervour::RaisePool(Holder.AbilitySystem, 40.0f);
	TestEqual(TEXT("40 above a full bar: 40 stacks"), Holder.AbilitySystem->FervourOverflowStacksHeld(), 40);
	TestEqual(TEXT("held at 130 by the cap"), AttackSpeed(), 130.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourLossChainTest,
	"Cataclysm.FervourLossAndChain.UnbrokenChainLengthensAPassiveKillStackAndNotAnEnchantments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two own stacks granted on `kill`, each lasting 5 seconds: one keyed as a passive row's, and one as the real
 * enchantment "Each kill reduces your damage by 2-4% for 5 seconds". With Unbroken Chain's 1 second, 5.5 seconds after
 * the kill the passive stack is still held and the enchantment's is not, as ruled. THE CONTROL: without the chain both
 * have lapsed.
 */
bool FCataclysmFervourLossChainTest::RunTest(const FString&)
{
	using namespace CataclysmFervourLossAndChainTest;

	const TCHAR* PassiveKey = TEXT("Passive_SoulCollector_basic_b_a0:attack_damage");
	const TCHAR* EnchantmentKey = TEXT("Negative_Each_kill_reduces_your_damage_by_2_4_for_5_sec:attack_damage");
	for (const float Chain : {1.0f, 0.0f})
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		const FLossHolder Holder(World, 0.0f);
		TMap<FName, FCataclysmStatInputs> Lines;
		AddLine(Lines, UCataclysmFervour::KillChainSecondsStat, 0.0f, LossRow(ECataclysmStatBucket::Flat, Chain));
		Holder.AbilitySystem->SetStatInputs(MoveTemp(Lines));
		Holder.AbilitySystem->SetPoolActions({KillStack(PassiveKey, 5.0f), KillStack(EnchantmentKey, 5.0f)});
		Holder.AbilitySystem->ActOnEvent(FName(TEXT("kill")));
		RunClock(World, 5.5f);
		TestEqual(*FString::Printf(TEXT("chain %g: the passive stack"), Chain),
				  Holder.AbilitySystem->OwnStacksHeld(FName(PassiveKey)), Chain > 0.0f ? 1 : 0);
		TestEqual(*FString::Printf(TEXT("chain %g: the enchantment's stack has lapsed"), Chain),
				  Holder.AbilitySystem->OwnStacksHeld(FName(EnchantmentKey)), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourLossSoulTideTest,
	"Cataclysm.FervourLossAndChain.SoulTidesWindowIsLengthenedAndAKillWindowIsNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 2.5 seconds after a kill, with Unbroken Chain's 1 second: a row under `kill_chain_within_seconds` 2 (Soul Tide's)
 * holds, and a row under `seconds_after_kill` 2 (a "for 2 seconds after a kill" row) does not. Asked of the pipeline
 * with the holder's own conditions.
 */
bool FCataclysmFervourLossSoulTideTest::RunTest(const FString&)
{
	using namespace CataclysmFervourLossAndChainTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FLossHolder Holder(World, 0.0f);
	TMap<FName, FCataclysmStatInputs> Lines;
	AddLine(Lines, UCataclysmFervour::KillChainSecondsStat, 0.0f, LossRow(ECataclysmStatBucket::Flat, 1.0f));
	Holder.AbilitySystem->SetStatInputs(MoveTemp(Lines));
	Holder.AbilitySystem->NoteOwnKill(/*GraceSeconds=*/0.0f);
	RunClock(World, 2.5f);
	const FCataclysmStatConditions State = Holder.AbilitySystem->CurrentConditions();
	TestEqual(TEXT("set-up: the chain's 1 second is in the conditions"), State.KillChainSeconds, 1.0f, 0.001f);
	TestTrue(TEXT("Soul Tide's window, 2 and 1 more: holds"),
			 UCataclysmStatPipeline::ConditionHolds(ECataclysmStatCondition::KillChainWithinSeconds, 2.0f, State));
	TestFalse(TEXT("control: a plain kill window of 2 does not"),
			  UCataclysmStatPipeline::ConditionHolds(ECataclysmStatCondition::WithinSecondsOfKill, 2.0f, State));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourLossRealRowsTest,
	"Cataclysm.FervourLossAndChain.TheFourNodesRowsAndSoulTidesAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The real rows of the four nodes and Soul Tide's moved condition, read from the passive effects table. */
bool FCataclysmFervourLossRealRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}
	struct FExpected
	{
		const TCHAR* Node;
		int32 Option;
		const TCHAR* Stat;
		const TCHAR* Kind;
		float Value;
		int32 Count;
	};
	const FExpected Expected[] = {
		{TEXT("SoulCollector_basic_a_b1"), 0, TEXT("fervour_decay_floor_percent"), TEXT("flat"), 3.0f, 1},
		{TEXT("SoulCollector_keystone_spine_003"), 0, TEXT("fervour_kill_decay_per_second"), TEXT("removed"), 1.0f, 1},
		{TEXT("SoulCollector_keystone_spine_003"), 0, TEXT("health_percent_lost_per_fervour_lost"), TEXT("flat"), 2.0f, 1},
		{TEXT("SoulCollector_keystone_a_kA"), 0, TEXT("fervour_overflow_attack_speed_seconds"), TEXT("flat"), 5.0f, 1},
		{TEXT("SoulCollector_keystone_a_kA"), 0, TEXT("attack_speed"), TEXT("increased"), 1.0f, 1},
		{TEXT("SoulCollector_basic_b_a1"), 0, TEXT("kill_chain_seconds"), TEXT("flat"), 1.0f, 2},
		{TEXT("SoulCollector_capstone_25"), 1, TEXT("fervour_per_kill"), TEXT("flat"), 1.0f, 1},
	};
	for (const FExpected& Want : Expected)
	{
		int32 Found = 0;
		for (const FCataclysmPassiveEffectRow* Row : UCataclysmPassiveTree::EffectsFor(Effects, FName(Want.Node)))
		{
			if (Row->Option != Want.Option || Row->Stat != Want.Stat)
			{
				continue;
			}
			++Found;
			TestEqual(*FString::Printf(TEXT("%s %s: the kind"), Want.Node, Want.Stat), Row->ValueKind, FString(Want.Kind));
			TestEqual(*FString::Printf(TEXT("%s %s: the value"), Want.Node, Want.Stat), Row->ValuePerPoint, Want.Value,
					  0.001f);
			if (FString(Want.Node) == TEXT("SoulCollector_capstone_25"))
			{
				TestEqual(TEXT("Soul Tide: the chain's window"), Row->Condition, FString(TEXT("kill_chain_within_seconds")));
			}
			if (FString(Want.Stat) == TEXT("attack_speed"))
			{
				TestEqual(TEXT("Overflow: the scale"), Row->Scale, FString(TEXT("fervour_overflow_stacks")));
				TestEqual(TEXT("Overflow: at most 30"), Row->ScaleMaxSteps, 30);
			}
		}
		TestEqual(*FString::Printf(TEXT("%s %s: rows"), Want.Node, Want.Stat), Found, Want.Count);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourLossOverflowRealGainsTest,
	"Cataclysm.FervourLossAndChain.AGainPastTheBarThroughARealPathGivesOverflowItsExcess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Window death3-w1's second pass (2026-11-25), as ruled: every gain used to stop itself at the bar before it reached
 * `RaisePool`, so Overflow, which counts what did not fit, heard nothing from a real gain. A cast paying 10 Fervour
 * (`UCataclysmFervour::GainForCast`) at 98 of 100 fills 2 and gives 8 stacks; a War block paying 5 at the full bar
 * (`FCataclysmWarFervour::GainOnBlock`) gives 5 more inside the same window: 13. THE CONTROL: the same cast at 80 fits
 * and gives none.
 */
bool FCataclysmFervourLossOverflowRealGainsTest::RunTest(const FString&)
{
	using namespace CataclysmFervourLossAndChainTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FLossHolder Holder(World, 80.0f);
	TMap<FName, FCataclysmStatInputs> Lines;
	AddLine(Lines, UCataclysmFervour::OverflowAttackSpeedSecondsStat, 0.0f, LossRow(ECataclysmStatBucket::Flat, 5.0f));
	AddLine(Lines, UCataclysmFervour::PerCastStat, 0.0f, LossRow(ECataclysmStatBucket::Flat, 10.0f));
	AddLine(Lines, FCataclysmWarFervour::PerBlockStat, 0.0f, LossRow(ECataclysmStatBucket::Flat, 5.0f));
	Holder.AbilitySystem->SetStatInputs(MoveTemp(Lines));

	TestEqual(TEXT("control: a cast at 80 pays its 10"), UCataclysmFervour::GainForCast(Holder.AbilitySystem), 10.0f,
			  0.001f);
	TestEqual(TEXT("control: and, fitting, gives no stack"), Holder.AbilitySystem->FervourOverflowStacksHeld(), 0);

	Holder.SetFervour(98.0f);
	TestEqual(TEXT("a cast at 98 fills 2"), UCataclysmFervour::GainForCast(Holder.AbilitySystem), 2.0f, 0.001f);
	TestEqual(TEXT("and its other 8 are Overflow's: 8 stacks"), Holder.AbilitySystem->FervourOverflowStacksHeld(), 8);
	TestEqual(TEXT("a War block at the full bar fills nothing"), FCataclysmWarFervour::GainOnBlock(Holder.AbilitySystem),
			  0.0f, 0.001f);
	TestEqual(TEXT("and its 5 are Overflow's too: 13 stacks"), Holder.AbilitySystem->FervourOverflowStacksHeld(), 13);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
