// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSiphon.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * The Soul Collector's Siphon and its limb, with the REAL ROWS (the Death trees' slice C2, 2026-11-25).
 *
 * Each test gives a holder what `DT_PassiveEffects` grants the nodes it names, read through
 * `UCataclysmPassiveTree::ModifiersFor` as the game reads them, and runs `FCataclysmSiphon::Step` a regeneration step
 * at a time with the world's clock moved on a quarter of a second each. The holder is a character on the players'
 * team with health and a Fervour pool; the creatures are hostile and placed along a line from it.
 */

namespace CataclysmSiphonTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	/** Centimetres in a metre. */
	constexpr float SiphonM = 100.0f;

	/** The holder: a character on the players' team, ten thousand health at half, a Fervour pool of 100 at 50. */
	ACataclysmEnemyCharacter* SiphonHolder(FAutomationTestBase& Test, UWorld* World)
	{
		ACataclysmEnemyCharacter* Holder =
			World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("set-up: a holder"), Holder))
		{
			return nullptr;
		}
		Holder->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		Holder->SetAttackDamage(0.0f);
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent());
		if (!Test.TestNotNull(TEXT("set-up: with an ability system"), System))
		{
			return nullptr;
		}
		System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Holder));
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 50.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 5000.0f);
		return Holder;
	}

	/** A hostile creature `Metres` along the line from the holder. */
	ACataclysmEnemyCharacter* SiphonCreature(FAutomationTestBase& Test, UWorld* World, float Metres)
	{
		ACataclysmEnemyCharacter* Creature =
			World->SpawnActor<ACataclysmEnemyCharacter>(FVector(Metres * SiphonM, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Test.TestNotNull(TEXT("set-up: a hostile creature"), Creature))
		{
			Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		}
		return Creature;
	}

	/**
	 * Hand `Holder` what the real tables grant these nodes, each stat's line on the base `Bases` gives it, nought if
	 * none. False when the tables grant nothing at all, which is a set-up failure.
	 */
	bool GiveRealRows(ACataclysmEnemyCharacter* Holder, const TArray<TPair<FName, int32>>& Nodes,
					  const TMap<FName, float>& Bases)
	{
		FCataclysmPassiveAllocation Allocation;
		for (const TPair<FName, int32>& Each : Nodes)
		{
			Allocation.Add(Each.Key, Each.Value);
		}
		const TMap<FName, TArray<FCataclysmStatModifier>> Rows = UCataclysmPassiveTree::ModifiersFor(
			Allocation, UCataclysmPassiveTree::LoadNodeTable(), UCataclysmPassiveTree::LoadEffectTable(),
			{FName(TEXT("Death"))});
		TMap<FName, FCataclysmStatInputs> Lines;
		for (const TPair<FName, TArray<FCataclysmStatModifier>>& Row : Rows)
		{
			FCataclysmStatInputs& Line = Lines.FindOrAdd(Row.Key);
			Line.Modifiers = Row.Value;
		}
		for (const TPair<FName, float>& Base : Bases)
		{
			Lines.FindOrAdd(Base.Key).Base = Base.Value;
		}
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent());
		if (!System)
		{
			return false;
		}
		System->SetStatInputs(MoveTemp(Lines));
		return Rows.Num() > 0;
	}

	/** One regeneration step of the holder's Siphon, after a quarter of a second. */
	int32 SiphonStepOf(UWorld* World, ACataclysmEnemyCharacter* Holder)
	{
		World->TimeSeconds += UCataclysmRegeneration::StepSeconds;
		return FCataclysmSiphon::Step(Holder, UCataclysmRegeneration::StepSeconds);
	}

	/** The holder's ability system. */
	UCataclysmAbilitySystemComponent* SystemOf(ACataclysmEnemyCharacter* Holder)
	{
		return Holder ? Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent()) : nullptr;
	}

	/** The Siphon keystone's row name. */
	FName TheSiphon()
	{
		return FName(TEXT("SoulCollector_basic_c_stem0"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSiphonKeystoneTest,
	"Cataclysm.Siphon.TheKeystonesRealRowTakesACreatureWithinSixMetresAndNotOneAtTen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Siphon: "You siphon every enemy within 6 metres of you." A creature at 5 metres is in it and one at 10 is not.
 * THE CONTROL: without the keystone nobody is, "Without this keystone you have no Siphon".
 */
bool FCataclysmSiphonKeystoneTest::RunTest(const FString&)
{
	using namespace CataclysmSiphonTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Holder = SiphonHolder(*this, World);
	ACataclysmEnemyCharacter* Near = Holder ? SiphonCreature(*this, World, 5.0f) : nullptr;
	ACataclysmEnemyCharacter* Far = Near ? SiphonCreature(*this, World, 10.0f) : nullptr;
	UCataclysmAbilitySystemComponent* System = SystemOf(Holder);
	if (!Far || !System)
	{
		return false;
	}

	TestEqual(TEXT("control: without the keystone nobody is in a Siphon"), SiphonStepOf(World, Holder), 0);

	if (!TestTrue(TEXT("set-up: the keystone's real row"), GiveRealRows(Holder, {{TheSiphon(), 1}}, {})))
	{
		return false;
	}
	TestEqual(TEXT("its radius is 6 metres"), FCataclysmSiphon::RadiusMetresOf(System), 6.0f, 0.001f);
	TestEqual(TEXT("with it one creature is in the Siphon"), SiphonStepOf(World, Holder), 1);
	TestTrue(TEXT("the one at 5 metres"), System->HasInSiphon(Near));
	TestFalse(TEXT("and not the one at 10"), System->HasInSiphon(Far));
	TestEqual(TEXT("and the screen says so"), FCataclysmSiphon::DisplayLine(System),
			  FString(TEXT("Siphon: 1 within 6.0 m")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSiphonSappingColdTest,
	"Cataclysm.Siphon.SappingColdsRealRowsSlowAndCutEachMemberAndRaiseTheHoldersDamageForEach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Sapping Cold, six points: "Each enemy in your Siphon deals 1% reduced damage per point and has 3% reduced Movement
 * Speed and Attack Speed for every 2 points, and you gain +1% increased damage ... for every 2 points for each of
 * them". Two members each deal 6% less and are slowed 9%, and the holder's damage is 6% higher. THE CONTROL: a
 * creature outside the Siphon carries neither.
 */
bool FCataclysmSiphonSappingColdTest::RunTest(const FString&)
{
	using namespace CataclysmSiphonTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Holder = SiphonHolder(*this, World);
	ACataclysmEnemyCharacter* First = Holder ? SiphonCreature(*this, World, 2.0f) : nullptr;
	ACataclysmEnemyCharacter* Second = First ? SiphonCreature(*this, World, 4.0f) : nullptr;
	ACataclysmEnemyCharacter* Outside = Second ? SiphonCreature(*this, World, 12.0f) : nullptr;
	UCataclysmAbilitySystemComponent* System = SystemOf(Holder);
	const FName Damage(TEXT("attack_damage"));
	if (!Outside || !System
		|| !TestTrue(TEXT("set-up: the real rows"),
					 GiveRealRows(Holder, {{TheSiphon(), 1}, {FName(TEXT("SoulCollector_basic_c_stem1")), 6}},
								  {{Damage, 100.0f}})))
	{
		return false;
	}

	TestEqual(TEXT("before the first step nobody is in it, so the damage is the base"),
			  System->StatForSkill(Damage, FGameplayTagContainer(), 0.0f), 100.0f, 0.01f);
	TestEqual(TEXT("two creatures are in it"), SiphonStepOf(World, Holder), 2);
	TestEqual(TEXT("each of the two raises the holder's damage 3%: 106"),
			  System->StatForSkill(Damage, FGameplayTagContainer(), 0.0f), 106.0f, 0.01f);
	for (ACataclysmEnemyCharacter* Member : {First, Second})
	{
		TestEqual(TEXT("a member deals 6% less"),
				  UCataclysmDebuffs::DealLessPercentOn(Member->GetAbilitySystemComponent()), 6.0f, 0.001f);
		TestEqual(TEXT("and is slowed 9%"), Member->GroundDownMultiplier(), 0.91f, 0.001f);
	}
	TestEqual(TEXT("control: the creature outside deals no less"),
			  UCataclysmDebuffs::DealLessPercentOn(Outside->GetAbilitySystemComponent()), 0.0f, 0.001f);
	TestEqual(TEXT("control: and is not slowed"), Outside->GroundDownMultiplier(), 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSiphonHungeringTest,
	"Cataclysm.Siphon.HungeringSiphonsRealRowDrainsMoreForEveryFiveFervourHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Hungering Siphon, the Soul Collector keystone rework (issue #2951): "Your Siphon deals damage over time to every enemy
 * in it: 0.5% of your weapon damage each second for every 5 Fervour you hold". Its real row, on a holder of weapon damage
 * 100 with one member: 10 a second at 100 Fervour and 5 at 50. THE CONTROL: at no Fervour it sends nothing, and the
 * holder has no other row that drains. It replaced Living Harvest, whose row this test read until that day.
 */
bool FCataclysmSiphonHungeringTest::RunTest(const FString&)
{
	using namespace CataclysmSiphonTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Holder = SiphonHolder(*this, World);
	UCataclysmAbilitySystemComponent* System = SystemOf(Holder);
	if (!System || !SiphonCreature(*this, World, 2.0f)
		|| !TestTrue(TEXT("set-up: the real rows. If false, DT_PassiveEffects is older than the CSV"),
					 GiveRealRows(Holder, {{TheSiphon(), 1}, {FName(TEXT("SoulCollector_keystone_c_kA")), 1}}, {})))
	{
		return false;
	}
	Holder->SetAttackDamage(100.0f);
	TestEqual(TEXT("set-up: one creature is in the Siphon"), SiphonStepOf(World, Holder), 1);
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();

	System->SetNumericAttributeBase(Pool, 0.0f);
	TestEqual(TEXT("control: at no Fervour the Siphon drains nothing"), FCataclysmSiphon::DrainOnce(Holder), 0.0f,
			  0.001f);
	System->SetNumericAttributeBase(Pool, 50.0f);
	TestEqual(TEXT("at 50 Fervour it sends 5, ten steps of 0.5% of 100"), FCataclysmSiphon::DrainOnce(Holder), 5.0f,
			  0.01f);
	System->SetNumericAttributeBase(Pool, 100.0f);
	TestEqual(TEXT("and at 100 it sends 10"), FCataclysmSiphon::DrainOnce(Holder), 10.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSiphonSoulHeavyAirTest,
	"Cataclysm.Siphon.SoulHeavyAirsRealRowRaisesTheDamageAMemberTakesAndNotAnOutsiders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Soul-Heavy Air, eight points: "Enemies in your Siphon take 1% increased damage from you per point for every 50
 * Fervour you hold". At 100 Fervour, 16% against a member. THE CONTROL: against a creature outside, nothing.
 */
bool FCataclysmSiphonSoulHeavyAirTest::RunTest(const FString&)
{
	using namespace CataclysmSiphonTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Holder = SiphonHolder(*this, World);
	ACataclysmEnemyCharacter* Member = Holder ? SiphonCreature(*this, World, 2.0f) : nullptr;
	ACataclysmEnemyCharacter* Outside = Member ? SiphonCreature(*this, World, 12.0f) : nullptr;
	UCataclysmAbilitySystemComponent* System = SystemOf(Holder);
	const FName Damage(TEXT("attack_damage"));
	if (!Outside || !System
		|| !TestTrue(TEXT("set-up: the real rows"),
					 GiveRealRows(Holder, {{TheSiphon(), 1}, {FName(TEXT("SoulCollector_basic_c_b2")), 8}},
								  {{Damage, 100.0f}})))
	{
		return false;
	}
	System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 100.0f);
	SiphonStepOf(World, Holder);

	const auto DamageAgainst = [System, &Damage](const AActor* Target)
	{
		return System->StatForSkill(Damage, FGameplayTagContainer(), 0.0f, -1.0f, FCataclysmBlowContext(), -1.0f,
									-1.0f, false, Target);
	};
	TestEqual(TEXT("against a member, 16% increased at 100 Fervour"), DamageAgainst(Member), 116.0f, 0.01f);
	TestEqual(TEXT("control: against a creature outside the Siphon, the base"), DamageAgainst(Outside), 100.0f,
			  0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSiphonLingeringPullTest,
	"Cataclysm.Siphon.LingeringPullsRealRowsKeepALeaverInTheSiphonForTwoSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Lingering Pull, eight points: "Enemies stay in your Siphon for 1 second for every 4 points after they leave its
 * area": 2 seconds. A creature that steps out is still in at 1.75 seconds and gone at 2. THE CONTROL: without the
 * node it is gone on the first step out.
 */
bool FCataclysmSiphonLingeringPullTest::RunTest(const FString&)
{
	using namespace CataclysmSiphonTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Holder = SiphonHolder(*this, World);
	ACataclysmEnemyCharacter* Creature = Holder ? SiphonCreature(*this, World, 2.0f) : nullptr;
	UCataclysmAbilitySystemComponent* System = SystemOf(Holder);
	if (!Creature || !System || !TestTrue(TEXT("set-up: the keystone's row"), GiveRealRows(Holder, {{TheSiphon(), 1}}, {})))
	{
		return false;
	}
	const FVector Near(2.0f * SiphonM, 0.0f, 0.0f);
	const FVector Far(20.0f * SiphonM, 0.0f, 0.0f);

	// THE CONTROL FIRST: the keystone alone.
	TestEqual(TEXT("set-up: the creature is in"), SiphonStepOf(World, Holder), 1);
	Creature->SetActorLocation(Far);
	TestEqual(TEXT("control: without Lingering Pull it is gone on the first step out"), SiphonStepOf(World, Holder), 0);

	Creature->SetActorLocation(Near);
	if (!TestTrue(TEXT("set-up: Lingering Pull's real rows"),
				  GiveRealRows(Holder, {{TheSiphon(), 1}, {FName(TEXT("SoulCollector_basic_c_c0")), 8}}, {})))
	{
		return false;
	}
	TestEqual(TEXT("the creature is in again"), SiphonStepOf(World, Holder), 1);
	Creature->SetActorLocation(Far);
	for (int32 Step = 1; Step <= 7; ++Step)
	{
		SiphonStepOf(World, Holder);
	}
	TestTrue(TEXT("with Lingering Pull it is still in at 1.75 seconds out"), System->HasInSiphon(Creature));
	SiphonStepOf(World, Holder);
	TestFalse(TEXT("and gone at 2"), System->HasInSiphon(Creature));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
