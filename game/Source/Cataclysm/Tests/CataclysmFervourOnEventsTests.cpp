// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * Fervour on events and decay held by who is near (the Death trees' slice C6b3, 2026-11-25). A `points` row pays its
 * value, scaled by `class_resource_generation`; every class resource gain a pool action makes stops at the bar after its
 * conditioned maximum and is seen by Kindred Fires; a row with a chance rolls before it pays and before it waits; the
 * three conditions read their state, two of them from a real walk; and the real rows and copies.
 */

namespace CataclysmFervourOnEventsTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	constexpr float EventMetre = 100.0f;

	/** An actor holding health and a Fervour pool of 100 held at `Held`, able to be given stat lines and actions. */
	struct FFervourOnEventsHolder
	{
		FFervourOnEventsHolder(UWorld* World, float Held)
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

		~FFervourOnEventsHolder()
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

		float FervourBase() const
		{
			return AbilitySystem->GetNumericAttributeBase(
				UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	/** A class resource row on `dodge`, worth `Value` on `Base`, waiting nothing between firings unless told. */
	FCataclysmPoolAction FervourOnDodge(float Value, ECataclysmPoolActionBase Base, float ChancePercent = 0.0f,
										float WaitSeconds = 0.0f)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("dodge"));
		Action.Pool = FName(TEXT("class_resource"));
		Action.Percent = Value;
		Action.Base = Base;
		Action.ChancePercent = ChancePercent;
		Action.TriggerCooldownSeconds = WaitSeconds;
		Action.TriggerKey = FName(TEXT("Test:class_resource:dodge"));
		return Action;
	}

	FCataclysmStatModifier EventsRow(ECataclysmStatBucket Bucket, float Value,
									 ECataclysmStatCondition Condition = ECataclysmStatCondition::Always,
									 float ConditionValue = 0.0f, float ReachMetres = -1.0f)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = Bucket;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		Row.Condition = Condition;
		Row.ConditionValue = ConditionValue;
		Row.ReachMetres = ReachMetres;
		return Row;
	}

	/** Pins `Cataclysm.ClassResourceChanceRoll` at `Roll` (negative rolls for real). */
	void PinChanceRoll(float Roll)
	{
		if (IConsoleVariable* Pin = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.ClassResourceChanceRoll")))
		{
			Pin->Set(Roll, ECVF_SetByConsole);
		}
	}

	/** A character of `Side` at `Where`, harmless and hard to kill. */
	ACataclysmEnemyCharacter* SpawnOn(UWorld* World, const FVector& Where, ECataclysmTeam Side)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(Side));
			Made->SetHealth(1'000'000.0f);
			Made->SetAttackDamage(0.0f);
		}
		return Made;
	}

	/** How many of `Rows` are named `Name`. */
	int32 CopiesNamed(const TArray<FCataclysmEnchantmentEffectRow>& Rows, const TCHAR* Name)
	{
		int32 Found = 0;
		for (const FCataclysmEnchantmentEffectRow& Row : Rows)
		{
			Found += Row.Enchantment == Name ? 1 : 0;
		}
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourOnEventsPointsTest,
	"Cataclysm.FervourOnEvents.APointsRowPaysItsValueScaledByGeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A `points` row of 4 on `dodge`, from 0 held: 4 at the normal generation, and 6 with `class_resource_generation`
 * raised by half (100 and 50% increased). THE CONTROL is the first: the same row without the raise.
 */
bool FCataclysmFervourOnEventsPointsTest::RunTest(const FString&)
{
	using namespace CataclysmFervourOnEventsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FFervourOnEventsHolder Holder(World, 0.0f);
	UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;
	System->SetPoolActions({FervourOnDodge(4.0f, ECataclysmPoolActionBase::Points)});

	System->ActOnEvent(FName(TEXT("dodge")));
	TestEqual(TEXT("control: at the normal generation, 4"), Holder.Fervour(), 4.0f, 0.001f);

	Holder.SetFervour(0.0f);
	TMap<FName, FCataclysmStatInputs> Lines;
	FCataclysmStatInputs& Generation =
		Lines.Add(FName(UCataclysmAbilitySystemComponent::ClassResourceGenerationStat));
	Generation.Base = UCataclysmAbilitySystemComponent::NormalClassResourceGeneration;
	Generation.Modifiers = {EventsRow(ECataclysmStatBucket::Increased, 50.0f)};
	System->SetStatInputs(MoveTemp(Lines));
	System->ActOnEvent(FName(TEXT("dodge")));
	TestEqual(TEXT("with generation raised by half, 6"), Holder.Fervour(), 6.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourOnEventsBarTest,
	"Cataclysm.FervourOnEvents.AClassResourceGainStopsAtTheBarAndKindredFiresSeesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The bar is the maximum attribute of 100 lowered to 80 by a conditioned row on `class_resource` that holds once the
 * movement skill has ended, so `MaximumClassResourceAsked` is 80 and the attribute 100. From 70, an enchantment-shaped
 * row of 50% of the maximum (50) stops at 80, in the current value AND THE STORED BASE, which a payment through `TopUp`
 * would have written to 100; and Kindred Fires, given by hand with a grace of 4 seconds, holds decay off, which a payment
 * through `TopUp` would not. A `points` row of 50 does the same. THE CONTROL: before the row, nothing holds decay off.
 */
bool FCataclysmFervourOnEventsBarTest::RunTest(const FString&)
{
	using namespace CataclysmFervourOnEventsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	for (const ECataclysmPoolActionBase Base : {ECataclysmPoolActionBase::Maximum, ECataclysmPoolActionBase::Points})
	{
		const TCHAR* Kind = Base == ECataclysmPoolActionBase::Maximum ? TEXT("50% of the maximum") : TEXT("50 flat");
		const FFervourOnEventsHolder Holder(World, 70.0f);
		UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;
		TMap<FName, FCataclysmStatInputs> Lines;
		FCataclysmStatInputs& Maximum = Lines.Add(FName(TEXT("class_resource")));
		Maximum.Base = 100.0f;
		Maximum.Modifiers = {EventsRow(ECataclysmStatBucket::Flat, -20.0f,
									   ECataclysmStatCondition::WithinSecondsOfMovementSkillEnd, 10.0f)};
		Lines.Add(FName(UCataclysmFervour::KillDecayGraceSecondsStat)).Modifiers = {
			EventsRow(ECataclysmStatBucket::Flat, 4.0f)};
		Lines.Add(FName(UCataclysmFervour::HoldRestartedByAnyGainStat)).Modifiers = {
			EventsRow(ECataclysmStatBucket::Flat, 1.0f)};
		System->SetStatInputs(MoveTemp(Lines));
		System->NoteMovementSkillEnded();
		const UCataclysmClassResourceAttributeSet* Resource = System->GetSet<UCataclysmClassResourceAttributeSet>();
		if (!TestNotNull(TEXT("set-up: the class resource set"), Resource)
			|| !TestEqual(FString::Printf(TEXT("set-up (%s): the bar is 80"), Kind), Resource->MaximumClassResourceAsked(),
						  80.0f, 0.001f))
		{
			return false;
		}
		TestFalse(FString::Printf(TEXT("control (%s): before the row, decay is not held"), Kind),
				  System->KillDecayIsHeld());

		System->SetPoolActions({FervourOnDodge(50.0f, Base)});
		System->ActOnEvent(FName(TEXT("dodge")));
		TestEqual(FString::Printf(TEXT("%s from 70: 80 held"), Kind), Holder.Fervour(), 80.0f, 0.001f);
		TestEqual(FString::Printf(TEXT("%s: and 80 stored"), Kind), Holder.FervourBase(), 80.0f, 0.001f);
		TestTrue(FString::Printf(TEXT("%s: Kindred Fires holds decay off"), Kind), System->KillDecayIsHeld());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourOnEventsChanceTest,
	"Cataclysm.FervourOnEvents.AChanceRowPaysOnARollUnderItAndWaitsOnlyAfterItPays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A `points` row of 1 with a chance of 2 and a wait of 10 seconds, the roll pinned: at 2.0 (a roll equal to the chance
 * fails) nothing is paid; at 1.9, straight after, 1 is paid, so the failed roll started no wait. THE CONTROL: a row with
 * no chance pays on a roll pinned at 99, as every row written before the column does.
 */
bool FCataclysmFervourOnEventsChanceTest::RunTest(const FString&)
{
	using namespace CataclysmFervourOnEventsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT
	{
		PinChanceRoll(-1.0f);
		World->DestroyWorld(false);
	};
	const FFervourOnEventsHolder Holder(World, 0.0f);
	UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;

	System->SetPoolActions({FervourOnDodge(1.0f, ECataclysmPoolActionBase::Points, /*ChancePercent=*/2.0f,
										   /*WaitSeconds=*/10.0f)});
	PinChanceRoll(2.0f);
	System->ActOnEvent(FName(TEXT("dodge")));
	TestEqual(TEXT("a roll of 2.0 against a chance of 2: nothing"), Holder.Fervour(), 0.0f, 0.001f);
	PinChanceRoll(1.9f);
	System->ActOnEvent(FName(TEXT("dodge")));
	TestEqual(TEXT("a roll of 1.9, straight after: 1, so the failed roll started no wait"), Holder.Fervour(), 1.0f,
			  0.001f);

	Holder.SetFervour(0.0f);
	System->SetPoolActions({FervourOnDodge(1.0f, ECataclysmPoolActionBase::Points)});
	PinChanceRoll(99.0f);
	System->ActOnEvent(FName(TEXT("dodge")));
	TestEqual(TEXT("control: no chance, a roll of 99, 1"), Holder.Fervour(), 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourOnEventsConditionsTest,
	"Cataclysm.FervourOnEvents.TheRungAndReachConditionsJudgeTheirState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The three conditions asked of the pipeline with a state given by hand. The attacker's rung: an Elite (1) holds at 1,
 * a Common (0) and no creature (-1) do not. An enemy of a rung in reach (15 metres, rung 1): an Elite at 10 metres
 * holds; a Common at 10 and an Elite at 20 do not. No enemy in reach (20 metres): nobody, and somebody at 25, hold once
 * the walk was made; somebody at 10 does not, and nobody walked does not.
 */
bool FCataclysmFervourOnEventsConditionsTest::RunTest(const FString&)
{
	using C = ECataclysmStatCondition;
	const auto Holds = [](C Condition, float Value, const FCataclysmStatConditions& State, float Reach = -1.0f)
	{
		return UCataclysmStatPipeline::ConditionHolds(Condition, Value, State, Reach);
	};

	FCataclysmStatConditions Blow;
	Blow.Blow.OpponentRarityStep = 1;
	TestTrue(TEXT("an Elite's attack: holds"), Holds(C::OpponentRarityAtLeast, 1.0f, Blow));
	Blow.Blow.OpponentRarityStep = 0;
	TestFalse(TEXT("control: a Common's, no"), Holds(C::OpponentRarityAtLeast, 1.0f, Blow));
	Blow.Blow.OpponentRarityStep = -1;
	TestFalse(TEXT("control: not a creature's, no"), Holds(C::OpponentRarityAtLeast, 0.0f, Blow));

	FCataclysmStatConditions Near;
	Near.HostileRarityDistancesMetres = {10.0f};
	Near.HostileRarityRungs = {1};
	TestTrue(TEXT("an Elite at 10 metres, reach 15: holds"), Holds(C::EnemyOfRarityInReach, 1.0f, Near, 15.0f));
	Near.HostileRarityRungs = {0};
	TestFalse(TEXT("control: a Common at 10, no"), Holds(C::EnemyOfRarityInReach, 1.0f, Near, 15.0f));
	Near.HostileRarityDistancesMetres = {20.0f};
	Near.HostileRarityRungs = {1};
	TestFalse(TEXT("control: an Elite at 20, no"), Holds(C::EnemyOfRarityInReach, 1.0f, Near, 15.0f));

	FCataclysmStatConditions Alone;
	Alone.bHostileDistancesRead = true;
	TestTrue(TEXT("nobody, walked, reach 20: holds"), Holds(C::NoEnemyInReach, 0.0f, Alone, 20.0f));
	Alone.HostileDistancesMetres = {25.0f};
	TestTrue(TEXT("somebody at 25: holds"), Holds(C::NoEnemyInReach, 0.0f, Alone, 20.0f));
	Alone.HostileDistancesMetres = {10.0f};
	TestFalse(TEXT("control: somebody at 10, no"), Holds(C::NoEnemyInReach, 0.0f, Alone, 20.0f));
	FCataclysmStatConditions NotWalked;
	TestFalse(TEXT("control: nobody walked, no"), Holds(C::NoEnemyInReach, 0.0f, NotWalked, 20.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourOnEventsWalkTest,
	"Cataclysm.FervourOnEvents.VigilAndHuntingGroundReadARealWalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A character of the players' side with a decay line of 10 carrying Vigil Over the Strong's row (5% increased decay
 * taken off while an Elite or stronger is within 15 metres) and Hunting Ground's (decay removed while no enemy is within
 * 20 metres). Alone: Hunting Ground holds, so 0. A Common creature at 10 metres: 10, neither row. The same creature made
 * Elite: 9.5, Vigil's row.
 */
bool FCataclysmFervourOnEventsWalkTest::RunTest(const FString&)
{
	using namespace CataclysmFervourOnEventsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmEnemyCharacter* Holder = SpawnOn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	UCataclysmAbilitySystemComponent* System =
		Holder ? Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent()) : nullptr;
	if (!TestNotNull(TEXT("set-up: a character"), System))
	{
		return false;
	}
	TMap<FName, FCataclysmStatInputs> Lines;
	FCataclysmStatInputs& Decay = Lines.Add(FName(UCataclysmFervour::KillDecayPerSecondStat));
	Decay.Base = 10.0f;
	Decay.Modifiers = {
		EventsRow(ECataclysmStatBucket::Increased, -5.0f, ECataclysmStatCondition::EnemyOfRarityInReach, 1.0f, 15.0f),
		EventsRow(ECataclysmStatBucket::Removed, 1.0f, ECataclysmStatCondition::NoEnemyInReach, 0.0f, 20.0f)};
	System->SetStatInputs(MoveTemp(Lines));
	const auto DecayNow = [System]()
	{
		return System->StatForSkill(FName(UCataclysmFervour::KillDecayPerSecondStat), FGameplayTagContainer(), 0.0f);
	};

	TestEqual(TEXT("alone: no decay"), DecayNow(), 0.0f, 0.001f);
	ACataclysmEnemyCharacter* Creature = SpawnOn(World, FVector(10.0f * EventMetre, 0, 0), ECataclysmTeam::Monsters);
	if (!TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	Creature->SetRarityStep(0);
	TestEqual(TEXT("control: a Common at 10 metres, 10"), DecayNow(), 10.0f, 0.001f);
	Creature->SetRarityStep(1);
	TestEqual(TEXT("an Elite at 10 metres, 9.5"), DecayNow(), 9.5f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourOnEventsCopiesTest,
	"Cataclysm.FervourOnEvents.OpeningFoundsSecondRowAndGleaningsChanceAreCopiedApart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The real tables. Opening Found at 3 points copies two rows, one under the node's own name and one named `_Row2`, each
 * worth 3, so they keep two keys. Gleaning at 6 points copies one row worth 50 with a chance of 30. THE CONTROL: Gleaning
 * at 1 point has a chance of 5, so the chance, not the amount, follows the points. Tear Loose was the row with a chance
 * here until the Soul Collector keystone rework (issue #2951) gave it a row without one.
 */
bool FCataclysmFervourOnEventsCopiesTest::RunTest(const FString&)
{
	using namespace CataclysmFervourOnEventsTest;

	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}
	const TArray<FName> Death = {FName(TEXT("Death"))};

	FCataclysmPassiveAllocation Opening;
	Opening.Add(FName(TEXT("Shadow_basic_d_a2")), 3);
	const TArray<FCataclysmEnchantmentEffectRow> Found =
		UCataclysmPassiveTree::EnchantmentRowsFor(Opening, Nodes, Effects, Death);
	TestEqual(TEXT("Opening Found: the first row under the node's name"),
			  CopiesNamed(Found, TEXT("Passive_Shadow_basic_d_a2")), 1);
	TestEqual(TEXT("and the second under its own"), CopiesNamed(Found, TEXT("Passive_Shadow_basic_d_a2_Row2")), 1);
	for (const FCataclysmEnchantmentEffectRow& Row : Found)
	{
		if (Row.Enchantment.StartsWith(TEXT("Passive_Shadow_basic_d_a2")))
		{
			TestEqual(TEXT("each worth 3 at 3 points"), Row.ValueLow, 3.0f, 0.001f);
		}
	}

	for (const int32 Points : {6, 1})
	{
		FCataclysmPassiveAllocation Gleaning;
		Gleaning.Add(FName(TEXT("SoulCollector_basic_b_stem0")), Points);
		const TArray<FCataclysmEnchantmentEffectRow> Copied =
			UCataclysmPassiveTree::EnchantmentRowsFor(Gleaning, Nodes, Effects, Death);
		int32 Rows = 0;
		for (const FCataclysmEnchantmentEffectRow& Row : Copied)
		{
			if (Row.Enchantment == TEXT("Passive_SoulCollector_basic_b_stem0"))
			{
				++Rows;
				TestEqual(FString::Printf(TEXT("Gleaning at %d: worth 50"), Points), Row.ValueLow, 50.0f, 0.001f);
				TestEqual(FString::Printf(TEXT("Gleaning at %d: a chance of %d"), Points, 5 * Points),
						  Row.ChancePercent, 5.0f * Points, 0.001f);
			}
		}
		TestEqual(FString::Printf(TEXT("Gleaning at %d: one row"), Points), Rows, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourOnEventsRealRowsTest,
	"Cataclysm.FervourOnEvents.TheSixNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The six nodes' real rows, read from the passive effects table: each stat or action, base, value and condition. */
bool FCataclysmFervourOnEventsRealRowsTest::RunTest(const FString&)
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
		const TCHAR* StatOrAction;
		const TCHAR* Condition;
		float Value;
		float Chance;
		int32 Count;
	};
	const FExpected Expected[] = {
		{TEXT("Shadow_basic_d_a2"), 0, TEXT("class_resource"), TEXT(""), 1.0f, 0.0f, 1},
		{TEXT("Shadow_basic_d_a2"), 0, TEXT("class_resource"), TEXT("target_rarity_at_least"), 1.0f, 0.0f, 1},
		{TEXT("SoulCollector_capstone_25"), 2, TEXT("class_resource"), TEXT("target_is_boss"), 5.0f, 0.0f, 1},
		{TEXT("SoulCollector_capstone_25"), 2, TEXT("fervour_kill_decay_per_second"), TEXT("enemy_of_rarity_in_reach"),
		 1.0f, 0.0f, 1},
		{TEXT("SoulCollector_basic_b_b1"), 0, TEXT("health"), TEXT("target_rarity_at_least"), 1.0f, 0.0f, 1},
		{TEXT("Shadow_basic_d_a0"), 0, TEXT("evasion"), TEXT("opponent_rarity_at_least"), 1.0f, 0.0f, 1},
		{TEXT("Shadow_keystone_spine_002"), 0, TEXT("fervour_kill_decay_per_second"), TEXT("no_enemy_in_reach"), 1.0f,
		 0.0f, 1},
		{TEXT("SoulCollector_basic_b_b2"), 0, TEXT("attack_speed"), TEXT("enemy_of_rarity_in_reach"), 1.0f, 0.0f, 1},
	};
	for (const FExpected& Want : Expected)
	{
		int32 Found = 0;
		for (const FCataclysmPassiveEffectRow* Row : UCataclysmPassiveTree::EffectsFor(Effects, FName(Want.Node)))
		{
			const FString& Named = Row->Action.IsEmpty() ? Row->Stat : Row->Action;
			if (Row->Option != Want.Option || Named != Want.StatOrAction || Row->Condition != Want.Condition)
			{
				continue;
			}
			++Found;
			TestEqual(*FString::Printf(TEXT("%s %s: the value"), Want.Node, Want.StatOrAction), Row->ValuePerPoint,
					  Want.Value, 0.001f);
			TestEqual(*FString::Printf(TEXT("%s %s: the chance"), Want.Node, Want.StatOrAction), Row->ChancePercent,
					  Want.Chance, 0.001f);
			if (!Row->Action.IsEmpty())
			{
				TestEqual(*FString::Printf(TEXT("%s: a flat amount"), Want.Node), Row->FractionOf, FString(TEXT("points")));
			}
		}
		TestEqual(*FString::Printf(TEXT("%s %s %s: rows"), Want.Node, Want.StatOrAction, Want.Condition), Found,
				  Want.Count);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
