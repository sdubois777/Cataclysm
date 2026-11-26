// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * A Poison that lasts and an evasion answered twice (the Death trees' slice C6b4, 2026-11-25). Twice Answered doubles
 * the critical strike charge an evasion grants, on a roll, whatever the table's order, never past two and never for a
 * full bar's charge. Festering's record times the asker's Poison on a creature without a break, and its scale reads it.
 * Four tests drive the charge with rows given by hand; two lay real Poisons on real creatures; two read the real tables.
 */

namespace CataclysmPoisonAndCritsTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;
	using CataclysmTestWorld::RunClock;

	/** An actor holding health, able to be given actions. */
	struct FCritsHolder
	{
		explicit FCritsHolder(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// A raw pointer, because AddAttributeSetSubobject deduces its type from the argument.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
		}

		~FCritsHolder()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	/** A critical strike charge granted on `Event` for 3 seconds: Slip's shape on `dodge`, Brim's on `resource_full`. */
	FCataclysmPoolAction ChargeOn(const TCHAR* Event)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(Event);
		Action.Pool = FName(TEXT("next_hit_critical"));
		Action.Percent = 3.0f;
		Action.bNextHitCritical = true;
		Action.TriggerKey = FName(*FString::Printf(TEXT("Test:next_hit_critical:%s"), Event));
		return Action;
	}

	/** Twice Answered's shape: on `dodge`, the charge holds two on a roll of `ChancePercent`. */
	FCataclysmPoolAction DoubledOnDodge(float ChancePercent)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("dodge"));
		Action.Pool = FName(TEXT("next_hit_critical_twice"));
		Action.Percent = 2.0f;
		Action.ChancePercent = ChancePercent;
		Action.bNextHitCriticalTwice = true;
		Action.TriggerKey = FName(TEXT("Test:next_hit_critical_twice:dodge"));
		return Action;
	}

	/** Pins the 0-100 roll every Chance Percent row makes (negative rolls for real). */
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

	UCataclysmAbilitySystemComponent* SystemOf(AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	FGameplayTag PoisonTag()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("Keyword.DoT.Poison")), /*ErrorIfNotFound=*/false);
	}

	/** Lays a Poison of `Applier`'s on `Carrier` for `Seconds`. */
	bool Poison(AActor* Applier, AActor* Carrier, float Seconds)
	{
		return UCataclysmSkillEffects::ApplyDamageOverTime(Applier, Carrier, 1.0f, Seconds, PoisonTag(),
														   /*bScalesWithInstigator=*/false);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPoisonAndCritsTwoTest,
	"Cataclysm.PoisonAndCrits.ADoubledEvasionGivesTwoCriticalStrikesThenNoMore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Slip's charge on `dodge` beside Twice Answered's doubling, the roll pinned at 0: the evasion grants a charge holding
 * two. One use leaves one and the charge still held; a second leaves none, so a third hit is not made critical. THE
 * CONTROL: the roll pinned at 99, the same evasion grants one.
 */
bool FCataclysmPoisonAndCritsTwoTest::RunTest(const FString&)
{
	using namespace CataclysmPoisonAndCritsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT
	{
		PinChanceRoll(-1.0f);
		World->DestroyWorld(false);
	};
	const FCritsHolder Holder(World);
	UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;
	System->SetPoolActions({ChargeOn(TEXT("dodge")), DoubledOnDodge(50.0f)});

	PinChanceRoll(0.0f);
	System->ActOnEvent(FName(TEXT("dodge")));
	TestEqual(TEXT("a doubled evasion: two"), System->NextHitCriticalChargesHeld(), 2);
	System->SpendNextHitCritical();
	TestTrue(TEXT("after one critical strike the charge is still held"), System->HoldsNextHitCritical());
	System->SpendNextHitCritical();
	TestFalse(TEXT("after two it is not, so a third hit is rolled as usual"), System->HoldsNextHitCritical());

	PinChanceRoll(99.0f);
	System->ActOnEvent(FName(TEXT("dodge")));
	TestEqual(TEXT("control: the roll fails, one"), System->NextHitCriticalChargesHeld(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPoisonAndCritsBrimTest,
	"Cataclysm.PoisonAndCrits.AFullBarsChargeIsNeverDoubled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Brim's charge on `resource_full` beside Twice Answered's doubling on `dodge`, the roll pinned at 0: a full bar grants
 * one, because only an evasion is doubled. THE CONTROL: an evasion under the same pin, with Slip's charge on `dodge`
 * given too, grants two.
 */
bool FCataclysmPoisonAndCritsBrimTest::RunTest(const FString&)
{
	using namespace CataclysmPoisonAndCritsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT
	{
		PinChanceRoll(-1.0f);
		World->DestroyWorld(false);
	};
	const FCritsHolder Holder(World);
	UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;
	PinChanceRoll(0.0f);

	System->SetPoolActions({ChargeOn(TEXT("resource_full")), DoubledOnDodge(50.0f)});
	System->ActOnEvent(FName(TEXT("resource_full")));
	TestEqual(TEXT("a full bar's charge: one"), System->NextHitCriticalChargesHeld(), 1);

	System->SpendNextHitCritical();
	System->SetPoolActions({ChargeOn(TEXT("dodge")), DoubledOnDodge(50.0f)});
	System->ActOnEvent(FName(TEXT("dodge")));
	TestEqual(TEXT("control: an evasion's, two"), System->NextHitCriticalChargesHeld(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPoisonAndCritsOrderTest,
	"Cataclysm.PoisonAndCrits.TheTablesOrderDoesNotChangeADoubledEvasion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The doubling listed BEFORE Slip's charge, the reverse of the test above, the roll pinned at 0: still two. AND A LATER
 * EVASION THAT IS NOT DOUBLED, while two are held, leaves two rather than adding a third or dropping to one.
 */
bool FCataclysmPoisonAndCritsOrderTest::RunTest(const FString&)
{
	using namespace CataclysmPoisonAndCritsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT
	{
		PinChanceRoll(-1.0f);
		World->DestroyWorld(false);
	};
	const FCritsHolder Holder(World);
	UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;
	System->SetPoolActions({DoubledOnDodge(50.0f), ChargeOn(TEXT("dodge"))});

	PinChanceRoll(0.0f);
	System->ActOnEvent(FName(TEXT("dodge")));
	TestEqual(TEXT("the doubling listed first: two"), System->NextHitCriticalChargesHeld(), 2);
	PinChanceRoll(99.0f);
	System->ActOnEvent(FName(TEXT("dodge")));
	TestEqual(TEXT("a later evasion not doubled, two held: still two, never three"),
			  System->NextHitCriticalChargesHeld(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPoisonAndCritsNoChargeTest,
	"Cataclysm.PoisonAndCrits.AnEvasionThatGrantsNoChargeIsGivenNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Twice Answered's doubling alone, with no charge granted on `dodge`, the roll pinned at 0: nothing is held. */
bool FCataclysmPoisonAndCritsNoChargeTest::RunTest(const FString&)
{
	using namespace CataclysmPoisonAndCritsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT
	{
		PinChanceRoll(-1.0f);
		World->DestroyWorld(false);
	};
	const FCritsHolder Holder(World);
	UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;
	System->SetPoolActions({DoubledOnDodge(50.0f)});
	PinChanceRoll(0.0f);
	System->ActOnEvent(FName(TEXT("dodge")));
	TestEqual(TEXT("no charge granted: none held"), System->NextHitCriticalChargesHeld(), 0);
	TestFalse(TEXT("and none to use"), System->HoldsNextHitCritical());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPoisonAndCritsRecordTest,
	"Cataclysm.PoisonAndCrits.FesteringTimesAPoisonWithoutABreak",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A creature poisoned by an asker for 2 seconds reads 0 at once and about 1.5 after 1.5 seconds. A second Poison laid
 * then, before the first ended, continues the time: about 2.5 a second later. Once every Poison of the asker's has
 * ended, it reads -1, and the next laid starts from 0. THE CONTROL: another character's record on the same creature
 * reads -1 throughout.
 */
bool FCataclysmPoisonAndCritsRecordTest::RunTest(const FString&)
{
	using namespace CataclysmPoisonAndCritsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmEnemyCharacter* Asker = SpawnOn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* Other = SpawnOn(World, FVector(0, 2000.0f, 0), ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* Creature = SpawnOn(World, FVector(300.0f, 0, 0), ECataclysmTeam::Monsters);
	UCataclysmAbilitySystemComponent* Carried = SystemOf(Creature);
	if (!Asker || !Other || !TestNotNull(TEXT("set-up: a creature"), Carried)
		|| !TestTrue(TEXT("set-up: the Poison tag"), PoisonTag().IsValid()))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the asker's Poison is laid"), Poison(Asker, Creature, 2.0f)))
	{
		return false;
	}
	TestEqual(TEXT("at once: 0"), Carried->SecondsPoisonedBy(Asker), 0.0f, 0.001f);
	TestEqual(TEXT("control: another's record, -1"), Carried->SecondsPoisonedBy(Other), -1.0f, 0.001f);
	RunClock(World, 1.5f);
	TestEqual(TEXT("after 1.5 seconds: about 1.5"), Carried->SecondsPoisonedBy(Asker), 1.5f, 0.06f);
	Poison(Asker, Creature, 2.0f);
	RunClock(World, 1.0f);
	TestEqual(TEXT("a second Poison laid before the first ended: about 2.5, unbroken"),
			  Carried->SecondsPoisonedBy(Asker), 2.5f, 0.06f);
	RunClock(World, 3.0f);
	TestEqual(TEXT("every Poison of the asker's ended: -1"), Carried->SecondsPoisonedBy(Asker), -1.0f, 0.001f);
	Poison(Asker, Creature, 2.0f);
	TestEqual(TEXT("the next laid starts from 0"), Carried->SecondsPoisonedBy(Asker), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPoisonAndCritsScaleTest,
	"Cataclysm.PoisonAndCrits.FesteringRaisesPoisonDamageBySecondsUpToTen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A `dot_damage` line of 100 with Festering's row, 1% increased Poison damage per whole second the creature has
 * stayed poisoned by the asker, at most 10, asked with the Poison's tags and the creature: 104 after 4 seconds, and
 * 110 after 15. THE CONTROL: a creature the asker never poisoned, 100.
 */
bool FCataclysmPoisonAndCritsScaleTest::RunTest(const FString&)
{
	using namespace CataclysmPoisonAndCritsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmEnemyCharacter* Asker = SpawnOn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* Creature = SpawnOn(World, FVector(300.0f, 0, 0), ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Untouched = SpawnOn(World, FVector(-300.0f, 0, 0), ECataclysmTeam::Monsters);
	UCataclysmAbilitySystemComponent* System = SystemOf(Asker);
	const FGameplayTag Tag = PoisonTag();
	if (!TestNotNull(TEXT("set-up: an asker"), System) || !Creature || !Untouched
		|| !TestTrue(TEXT("set-up: the Poison tag"), Tag.IsValid()))
	{
		return false;
	}
	FCataclysmStatModifier Row;
	Row.Bucket = ECataclysmStatBucket::Increased;
	Row.Source = ECataclysmModifierSource::PassiveKeystone;
	Row.Value = 1.0f;
	Row.Scale = ECataclysmStatScale::PerSecondPoisonedByYou;
	Row.ScaleStep = 1.0f;
	Row.ScaleMaxSteps = 10;
	Row.RequiredTags.AddTag(Tag);
	TMap<FName, FCataclysmStatInputs> Lines;
	FCataclysmStatInputs& Line = Lines.Add(FName(TEXT("dot_damage")));
	Line.Base = 100.0f;
	Line.Modifiers = {Row};
	System->SetStatInputs(MoveTemp(Lines));
	const FGameplayTagContainer Tags(Tag);
	const auto Against = [System, &Tags](const AActor* Target)
	{
		return System->StatForSkill(FName(TEXT("dot_damage")), Tags, 0.0f, -1.0f, FCataclysmBlowContext(), -1.0f, -1.0f,
									false, Target);
	};

	if (!TestTrue(TEXT("set-up: the asker's Poison is laid for 30 seconds"), Poison(Asker, Creature, 30.0f)))
	{
		return false;
	}
	RunClock(World, 4.05f);
	TestEqual(TEXT("after 4 seconds: 104"), Against(Creature), 104.0f, 0.001f);
	TestEqual(TEXT("control: a creature never poisoned, 100"), Against(Untouched), 100.0f, 0.001f);
	RunClock(World, 11.0f);
	TestEqual(TEXT("after 15 seconds: 110, the cap"), Against(Creature), 110.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPoisonAndCritsCopiesTest,
	"Cataclysm.PoisonAndCrits.TwiceAnsweredsChanceFollowsThePoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The real tables: Twice Answered at 8 points copies one row worth 2 with a chance of 40, and at 1 point a chance of
 * 5. Festering at 5 points copies none of its rows, which are stat rows read by the passive accumulation.
 */
bool FCataclysmPoisonAndCritsCopiesTest::RunTest(const FString&)
{
	using namespace CataclysmPoisonAndCritsTest;

	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}
	const TArray<FName> Death = {FName(TEXT("Death"))};
	for (const int32 Points : {8, 1})
	{
		FCataclysmPassiveAllocation Twice;
		Twice.Add(FName(TEXT("Shadow_basic_d_a1")), Points);
		const TArray<FCataclysmEnchantmentEffectRow> Copied =
			UCataclysmPassiveTree::EnchantmentRowsFor(Twice, Nodes, Effects, Death);
		TestEqual(FString::Printf(TEXT("Twice Answered at %d: one row"), Points),
				  CopiesNamed(Copied, TEXT("Passive_Shadow_basic_d_a1")), 1);
		for (const FCataclysmEnchantmentEffectRow& Row : Copied)
		{
			if (Row.Enchantment == TEXT("Passive_Shadow_basic_d_a1"))
			{
				TestEqual(FString::Printf(TEXT("Twice Answered at %d: worth 2"), Points), Row.ValueLow, 2.0f, 0.001f);
				TestEqual(FString::Printf(TEXT("Twice Answered at %d: a chance of %d"), Points, 5 * Points),
						  Row.ChancePercent, 5.0f * Points, 0.001f);
			}
		}
	}
	FCataclysmPassiveAllocation Festering;
	Festering.Add(FName(TEXT("Shadow_basic_a_a1")), 5);
	TestEqual(TEXT("Festering copies no action row"),
			  CopiesNamed(UCataclysmPassiveTree::EnchantmentRowsFor(Festering, Nodes, Effects, Death),
						  TEXT("Passive_Shadow_basic_a_a1")),
			  0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPoisonAndCritsRealRowsTest,
	"Cataclysm.PoisonAndCrits.TheTwoNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The real rows: Festering's five, each 1 on Poison damage scaled by `seconds_poisoned_by_you`, a step of 1, at most
 * 10, from 2, 4, 6, 8 and 10 points; Twice Answered's one, `next_hit_critical_twice` on `dodge`, worth 2, a chance of 5.
 */
bool FCataclysmPoisonAndCritsRealRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}
	TArray<int32> Steps;
	for (const FCataclysmPassiveEffectRow* Row : UCataclysmPassiveTree::EffectsFor(Effects, FName(TEXT("Shadow_basic_a_a1"))))
	{
		Steps.Add(Row->MinPoints);
		TestEqual(TEXT("Festering: dot_damage"), Row->Stat, FString(TEXT("dot_damage")));
		TestEqual(TEXT("Festering: the scale"), Row->Scale, FString(TEXT("seconds_poisoned_by_you")));
		TestEqual(TEXT("Festering: 1"), Row->ValuePerPoint, 1.0f, 0.001f);
		TestEqual(TEXT("Festering: a step of 1"), Row->ScaleStep, 1.0f, 0.001f);
		TestEqual(TEXT("Festering: at most 10"), Row->ScaleMaxSteps, 10);
	}
	Steps.Sort();
	TestEqual(TEXT("Festering: from 2, 4, 6, 8 and 10 points"), Steps, TArray<int32>({2, 4, 6, 8, 10}));

	int32 Found = 0;
	for (const FCataclysmPassiveEffectRow* Row : UCataclysmPassiveTree::EffectsFor(Effects, FName(TEXT("Shadow_basic_d_a1"))))
	{
		++Found;
		TestEqual(TEXT("Twice Answered: the action"), Row->Action, FString(TEXT("next_hit_critical_twice")));
		TestEqual(TEXT("Twice Answered: on an evasion"), Row->ActionEvent, FString(TEXT("dodge")));
		TestEqual(TEXT("Twice Answered: worth 2"), Row->ValuePerPoint, 2.0f, 0.001f);
		TestEqual(TEXT("Twice Answered: a chance of 5"), Row->ChancePercent, 5.0f, 0.001f);
	}
	TestEqual(TEXT("Twice Answered: one row"), Found, 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
