// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"

/**
 * The Death classes' kill generator, 2026-11-24 (Death trees, slice B).
 *
 * WHAT IS BUILT. Fervour for each enemy a character kills, by the slain creature's rarity rung
 * (`UCataclysmFervour::GainOnOwnKill`); a stamp at each kill (`UCataclysmFervour::NoteOwnKill`), which a row under
 * `seconds_after_kill` reads and which holds off the decay after a kill for a grace asked against the slain creature;
 * and that decay (`UCataclysmFervour::KillDecayStep`). The Soul Collector's starting node pays 1, 3, 5, 8 and 15 by rung
 * and decays at 5 a second 4 seconds after the last kill, 8 after an Elite or stronger one; the Shadow's pays 10 and 25
 * and drains at 10 a second 4 seconds after.
 *
 * THE REAL ROWS WHERE THE ROWS ARE THE SUBJECT. A test giving a character hand-built modifiers would pass with the
 * workbook saying anything; these read `DT_PassiveEffects` through `UCataclysmPassiveTree::ModifiersFor`, as the game
 * does, and hand the result to the ability system.
 */

namespace CataclysmClassKillFervourTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;
	using CataclysmTestWorld::RunClock;

	/** An actor holding a Fervour pool of 100 and health, able to be given stat lines. */
	struct FKillFervourHolder
	{
		explicit FKillFervourHolder(UWorld* World)
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
		}

		~FKillFervourHolder()
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

	/**
	 * What the real tables grant for an allocation, as stat lines an ability system takes: every stat's modifiers on a
	 * base of nought. Empty when the tables are missing or the tree is dormant for these damage types.
	 */
	TMap<FName, FCataclysmStatInputs> RealLinesFor(const FCataclysmPassiveAllocation& Allocation,
												   const TArray<FName>& DamageTypes)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		const TMap<FName, TArray<FCataclysmStatModifier>> Rows = UCataclysmPassiveTree::ModifiersFor(
			Allocation, UCataclysmPassiveTree::LoadNodeTable(), UCataclysmPassiveTree::LoadEffectTable(), DamageTypes);
		for (const TPair<FName, TArray<FCataclysmStatModifier>>& Row : Rows)
		{
			Lines.FindOrAdd(Row.Key).Modifiers = Row.Value;
		}
		return Lines;
	}

	/** A creature of this rarity rung, placed along a line so no two overlap. */
	ACataclysmEnemyCharacter* CreatureOfRung(UWorld* World, int32 Rung)
	{
		ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(500.0f + 300.0f * Rung, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Creature)
		{
			Creature->SetRarityStep(Rung);
		}
		return Creature;
	}

	/** The Death damage type's name, made when asked rather than at file scope. */
	FName DeathTypeName()
	{
		return FName(TEXT("Death"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClassKillFervourSoulCollectorTest,
	"Cataclysm.ClassKillFervour.AKillPaysTheSoulCollectorByTheSlainCreaturesRarity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClassKillFervourSoulCollectorTest::RunTest(const FString&)
{
	using namespace CataclysmClassKillFervourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("SoulCollector_basic_spine_000")), 1);
	TMap<FName, FCataclysmStatInputs> Lines = RealLinesFor(Allocation, {DeathTypeName()});
	if (!TestTrue(TEXT("set-up: the starting node's rows carry fervour_per_kill"),
				  Lines.Contains(FName(UCataclysmFervour::PerKillStat))))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py tools/generate_datatable_assets.py"));
		return false;
	}

	const FKillFervourHolder Holder(World);
	Holder.AbilitySystem->SetStatInputs(MoveTemp(Lines));

	// "1 for each enemy you kill, 3 for an Elite, 5 for a Legendary, 8 for a Herald and 15 for a Boss", and a
	// Cataclysm Boss paid as a Boss.
	const float Expected[] = {1.0f, 3.0f, 5.0f, 8.0f, 15.0f, 15.0f};
	for (int32 Rung = 0; Rung < 6; ++Rung)
	{
		ACataclysmEnemyCharacter* Creature = CreatureOfRung(World, Rung);
		if (!TestNotNull(*FString::Printf(TEXT("a creature of rung %d"), Rung), Creature))
		{
			return false;
		}
		Holder.SetFervour(0.0f);
		const float Paid = UCataclysmFervour::GainOnOwnKill(Holder.AbilitySystem, Creature);
		TestEqual(*FString::Printf(TEXT("a kill of rung %d pays %.0f"), Rung, Expected[Rung]), Paid, Expected[Rung]);
		TestEqual(*FString::Printf(TEXT("and the pool holds it, rung %d"), Rung), Holder.Fervour(), Expected[Rung]);
	}

	// THE CONTROLS: an actor that is not a creature has no rung, and a character without the node is paid nothing.
	AActor* NotACreature = World->SpawnActor<AActor>();
	Holder.SetFervour(0.0f);
	TestEqual(TEXT("an actor that is not a creature pays nothing"),
			  UCataclysmFervour::GainOnOwnKill(Holder.AbilitySystem, NotACreature), 0.0f);

	const FKillFervourHolder Nobody(World);
	TestEqual(TEXT("a character without the node is paid nothing for an Elite"),
			  UCataclysmFervour::GainOnOwnKill(Nobody.AbilitySystem, CreatureOfRung(World, 1)), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClassKillFervourShadowTest,
	"Cataclysm.ClassKillFervour.TheShadowIsPaidTenOrTwentyFiveAndReapersShareRaisesBoth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClassKillFervourShadowTest::RunTest(const FString&)
{
	using namespace CataclysmClassKillFervourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Common = CreatureOfRung(World, 0);
	ACataclysmEnemyCharacter* Elite = CreatureOfRung(World, 1);
	ACataclysmEnemyCharacter* Boss = CreatureOfRung(World, 4);
	if (!TestNotNull(TEXT("three creatures"), Common) || !Elite || !Boss)
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Shadow_basic_spine_000")), 1);
	{
		const FKillFervourHolder Holder(World);
		Holder.AbilitySystem->SetStatInputs(RealLinesFor(Allocation, {DeathTypeName()}));
		Holder.SetFervour(0.0f);
		TestEqual(TEXT("10 for each enemy you kill"), UCataclysmFervour::GainOnOwnKill(Holder.AbilitySystem, Common),
				  10.0f);
		Holder.SetFervour(0.0f);
		TestEqual(TEXT("25 for an Elite"), UCataclysmFervour::GainOnOwnKill(Holder.AbilitySystem, Elite), 25.0f);
		Holder.SetFervour(0.0f);
		TestEqual(TEXT("and 25 for a Boss, which is stronger than an Elite"),
				  UCataclysmFervour::GainOnOwnKill(Holder.AbilitySystem, Boss), 25.0f);
	}

	// REAPER'S SHARE, FIVE POINTS: "+2% increased Fervour gained from kills per point", so a tenth more of each.
	Allocation.Add(FName(TEXT("Shadow_basic_spine_004")), 5);
	{
		const FKillFervourHolder Holder(World);
		Holder.AbilitySystem->SetStatInputs(RealLinesFor(Allocation, {DeathTypeName()}));
		Holder.SetFervour(0.0f);
		TestEqual(TEXT("with Reaper's Share, 11 for a common kill"),
				  UCataclysmFervour::GainOnOwnKill(Holder.AbilitySystem, Common), 11.0f, 0.001f);
		Holder.SetFervour(0.0f);
		TestEqual(TEXT("and 27.5 for an Elite"), UCataclysmFervour::GainOnOwnKill(Holder.AbilitySystem, Elite),
				  27.5f, 0.001f);
	}

	// THE CONTROL: the same points with a Demonic weapon only. The tree is dormant, so nothing is paid.
	{
		const FKillFervourHolder Holder(World);
		Holder.AbilitySystem->SetStatInputs(RealLinesFor(Allocation, {FName(TEXT("Demonic"))}));
		Holder.SetFervour(0.0f);
		TestEqual(TEXT("with a Demonic weapon the Shadow's points pay nothing"),
				  UCataclysmFervour::GainOnOwnKill(Holder.AbilitySystem, Elite), 0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClassKillFervourDecayTest,
	"Cataclysm.ClassKillFervour.TheDecayWaitsFourSecondsAfterAKillAndEightAfterAnElite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClassKillFervourDecayTest::RunTest(const FString&)
{
	using namespace CataclysmClassKillFervourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Common = CreatureOfRung(World, 0);
	ACataclysmEnemyCharacter* Elite = CreatureOfRung(World, 1);
	if (!TestNotNull(TEXT("two creatures"), Common) || !Elite)
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("SoulCollector_basic_spine_000")), 1);

	// NEVER KILLED: NOTHING HOLDS IT, so Fervour from anywhere else decays at once, 5 a second.
	{
		const FKillFervourHolder Fresh(World);
		Fresh.AbilitySystem->SetStatInputs(RealLinesFor(Allocation, {DeathTypeName()}));
		Fresh.SetFervour(50.0f);
		TestEqual(TEXT("a Soul Collector that has never killed decays at once, 5 a second"),
				  UCataclysmFervour::KillDecayStep(Fresh.AbilitySystem, 1.0f), -5.0f, 0.001f);
	}

	const FKillFervourHolder Holder(World);
	Holder.AbilitySystem->SetStatInputs(RealLinesFor(Allocation, {DeathTypeName()}));
	Holder.SetFervour(50.0f);

	// A COMMON KILL HOLDS IT FOR 4 SECONDS.
	UCataclysmFervour::NoteOwnKill(Holder.AbilitySystem, Common);
	TestEqual(TEXT("just after a common kill nothing decays"),
			  UCataclysmFervour::KillDecayStep(Holder.AbilitySystem, 1.0f), 0.0f);
	RunClock(World, 3.8f);
	TestEqual(TEXT("nor at 3.8 seconds"), UCataclysmFervour::KillDecayStep(Holder.AbilitySystem, 1.0f), 0.0f);
	RunClock(World, 0.4f);
	TestEqual(TEXT("at 4.2 seconds a second of the decay takes 5"),
			  UCataclysmFervour::KillDecayStep(Holder.AbilitySystem, 1.0f), -5.0f, 0.001f);

	// AN ELITE KILL HOLDS IT FOR 8, AND A COMMON KILL A SECOND LATER DOES NOT SHORTEN THAT.
	UCataclysmFervour::NoteOwnKill(Holder.AbilitySystem, Elite);
	RunClock(World, 1.0f);
	UCataclysmFervour::NoteOwnKill(Holder.AbilitySystem, Common);
	RunClock(World, 5.8f);
	TestEqual(TEXT("6.8 seconds after an Elite kill, with a common kill between, nothing decays"),
			  UCataclysmFervour::KillDecayStep(Holder.AbilitySystem, 1.0f), 0.0f);
	RunClock(World, 1.4f);
	TestEqual(TEXT("8.2 seconds after it the decay runs"),
			  UCataclysmFervour::KillDecayStep(Holder.AbilitySystem, 1.0f), -5.0f, 0.001f);

	// THE CONTROL: a character without the node loses nothing, held or not.
	const FKillFervourHolder Nobody(World);
	Nobody.SetFervour(50.0f);
	TestEqual(TEXT("a character without the node loses nothing to the decay"),
			  UCataclysmFervour::KillDecayStep(Nobody.AbilitySystem, 1.0f), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClassKillFervourSlowerDecayTest,
	"Cataclysm.ClassKillFervour.HoarderAndColdStorageSlowTheDecayAndMomentumDelaysTheDrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClassKillFervourSlowerDecayTest::RunTest(const FString&)
{
	using namespace CataclysmClassKillFervourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// HOARDER, "You lose 50% less Fervour to decay": 2.5 a second.
	FCataclysmPassiveAllocation Hoarding;
	Hoarding.Add(FName(TEXT("SoulCollector_basic_spine_000")), 1);
	Hoarding.Add(FName(TEXT("SoulCollector_keystone_a_kB")), 1);
	{
		const FKillFervourHolder Holder(World);
		Holder.AbilitySystem->SetStatInputs(RealLinesFor(Hoarding, {DeathTypeName()}));
		Holder.SetFervour(50.0f);
		TestEqual(TEXT("with Hoarder the decay takes 2.5 a second"),
				  UCataclysmFervour::KillDecayStep(Holder.AbilitySystem, 1.0f), -2.5f, 0.001f);
	}

	// AND COLD STORAGE, EIGHT POINTS, "5% slower per point": 5 x 0.6 x 0.5 = 1.5 a second.
	Hoarding.Add(FName(TEXT("SoulCollector_basic_a_b2")), 8);
	{
		const FKillFervourHolder Holder(World);
		Holder.AbilitySystem->SetStatInputs(RealLinesFor(Hoarding, {DeathTypeName()}));
		Holder.SetFervour(50.0f);
		TestEqual(TEXT("with Cold Storage too it takes 1.5 a second"),
				  UCataclysmFervour::KillDecayStep(Holder.AbilitySystem, 1.0f), -1.5f, 0.001f);
	}

	// MOMENTUM, "2 seconds later after your last kill: 6 seconds rather than 4", on the Shadow.
	FCataclysmPassiveAllocation Pacing;
	Pacing.Add(FName(TEXT("Shadow_basic_spine_000")), 1);
	Pacing.Add(FName(TEXT("Shadow_keystone_spine_001")), 1);
	const FKillFervourHolder Shadow(World);
	Shadow.AbilitySystem->SetStatInputs(RealLinesFor(Pacing, {DeathTypeName()}));
	Shadow.SetFervour(50.0f);
	ACataclysmEnemyCharacter* Common = CreatureOfRung(World, 0);
	if (!TestNotNull(TEXT("a creature"), Common))
	{
		return false;
	}
	UCataclysmFervour::NoteOwnKill(Shadow.AbilitySystem, Common);
	RunClock(World, 5.8f);
	TestEqual(TEXT("with Momentum nothing drains 5.8 seconds after a kill"),
			  UCataclysmFervour::KillDecayStep(Shadow.AbilitySystem, 1.0f), 0.0f);
	RunClock(World, 0.4f);
	TestEqual(TEXT("and at 6.2 seconds the drain takes 10 a second"),
			  UCataclysmFervour::KillDecayStep(Shadow.AbilitySystem, 1.0f), -10.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClassKillFervourWindowTest,
	"Cataclysm.ClassKillFervour.ARowUnderSecondsAfterAKillHoldsForItsSecondsAfterTheKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClassKillFervourWindowTest::RunTest(const FString&)
{
	using namespace CataclysmClassKillFervourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// KILLING PACE, THREE POINTS: "For 4 seconds after you kill an enemy, +1% increased Attack Speed ... per point".
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Shadow_basic_spine_003")), 3);
	TMap<FName, FCataclysmStatInputs> Lines = RealLinesFor(Allocation, {DeathTypeName()});
	if (!TestTrue(TEXT("set-up: Killing Pace's rows carry attack speed"), Lines.Contains(FName(TEXT("attack_speed")))))
	{
		return false;
	}
	Lines.FindOrAdd(FName(TEXT("attack_speed"))).Base = 100.0f;

	const FKillFervourHolder Holder(World);
	Holder.AbilitySystem->SetStatInputs(MoveTemp(Lines));
	const FName Speed(TEXT("attack_speed"));
	const FGameplayTagContainer NoTags;

	TestEqual(TEXT("before any kill, nothing is added"),
			  Holder.AbilitySystem->StatForSkill(Speed, NoTags, 100.0f), 100.0f, 0.001f);
	TestEqual(TEXT("and the kill clock reads never"), Holder.AbilitySystem->SecondsSinceOwnKill(), -1.0f);

	ACataclysmEnemyCharacter* Common = CreatureOfRung(World, 0);
	UCataclysmFervour::NoteOwnKill(Holder.AbilitySystem, Common);
	TestEqual(TEXT("just after a kill, 3% increased"),
			  Holder.AbilitySystem->StatForSkill(Speed, NoTags, 100.0f), 103.0f, 0.001f);
	RunClock(World, 3.8f);
	TestEqual(TEXT("still at 3.8 seconds"), Holder.AbilitySystem->StatForSkill(Speed, NoTags, 100.0f), 103.0f, 0.001f);
	RunClock(World, 0.4f);
	TestEqual(TEXT("and gone at 4.2 seconds"), Holder.AbilitySystem->StatForSkill(Speed, NoTags, 100.0f), 100.0f,
			  0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClassKillFervourRarityTest,
	"Cataclysm.ClassKillFervour.AnEliteOrStrongerRowReadsTheStruckCreaturesRarity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClassKillFervourRarityTest::RunTest(const FString&)
{
	using namespace CataclysmClassKillFervourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// QUARRY, FOUR POINTS: "+2% increased damage per point against Elite and stronger creatures".
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("SoulCollector_basic_b_b0")), 4);
	TMap<FName, FCataclysmStatInputs> Lines = RealLinesFor(Allocation, {DeathTypeName()});
	if (!TestTrue(TEXT("set-up: Quarry's rows carry attack damage"), Lines.Contains(FName(TEXT("attack_damage")))))
	{
		return false;
	}
	Lines.FindOrAdd(FName(TEXT("attack_damage"))).Base = 100.0f;
	const FKillFervourHolder Holder(World);
	Holder.AbilitySystem->SetStatInputs(MoveTemp(Lines));

	const auto DamageAgainst = [&Holder](const AActor* Target)
	{
		return Holder.AbilitySystem->StatForSkill(FName(TEXT("attack_damage")), FGameplayTagContainer(), 100.0f, -1.0f,
												  FCataclysmBlowContext(), -1.0f, -1.0f, false, Target);
	};

	ACataclysmEnemyCharacter* Common = CreatureOfRung(World, 0);
	ACataclysmEnemyCharacter* Elite = CreatureOfRung(World, 1);
	ACataclysmEnemyCharacter* Boss = CreatureOfRung(World, 4);
	AActor* NotACreature = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("three creatures and an actor"), Common) || !Elite || !Boss || !NotACreature)
	{
		return false;
	}

	TestEqual(TEXT("against an Elite, 8% increased"), DamageAgainst(Elite), 108.0f, 0.001f);
	TestEqual(TEXT("against a Boss, which is stronger, 8% increased"), DamageAgainst(Boss), 108.0f, 0.001f);
	TestEqual(TEXT("against a common creature, nothing"), DamageAgainst(Common), 100.0f, 0.001f);
	TestEqual(TEXT("against an actor that is not a creature, nothing"), DamageAgainst(NotACreature), 100.0f, 0.001f);
	TestEqual(TEXT("and with no target in hand, nothing"), DamageAgainst(nullptr), 100.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClassKillFervourHandlerTest,
	"Cataclysm.ClassKillFervour.AKillAnnouncedForThePlayerPaysItsGeneratorAndStartsItsClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClassKillFervourHandlerTest::RunTest(const FString&)
{
	using namespace CataclysmClassKillFervourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Player =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!State || !Controller || !Player)
	{
		AddError(TEXT("A possessed player could not be built."));
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Player);
	UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent();
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	if (!TestNotNull(TEXT("the player's ability system"), System) || !TestNotNull(TEXT("the combat events"), Events))
	{
		return false;
	}

	// THE SOUL COLLECTOR'S STARTING NODE'S ROWS, handed to the player's ability system after possession, so no refresh
	// replaces them before the notice is heard.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("SoulCollector_basic_spine_000")), 1);
	System->SetStatInputs(RealLinesFor(Allocation, {DeathTypeName()}));
	System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
	System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();

	ACataclysmEnemyCharacter* Elite = CreatureOfRung(World, 1);
	AActor* Someone = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("an Elite and someone else"), Elite) || !Someone)
	{
		return false;
	}

	// THE CONTROL FIRST: an Elite somebody else killed pays the player nothing and starts no clock.
	FCataclysmDeathNotice Notice;
	Notice.Victim = Elite;
	Notice.Killer = Someone;
	Events->OnDeath.Broadcast(Notice);
	TestEqual(TEXT("an Elite somebody else killed pays the player nothing"), System->GetNumericAttribute(Pool), 0.0f);
	TestEqual(TEXT("and starts no kill clock"), System->SecondsSinceOwnKill(), -1.0f);

	// THE PLAYER'S OWN KILL OF AN ELITE: 3 Fervour, and the clock starts.
	Notice.Killer = Player;
	Events->OnDeath.Broadcast(Notice);
	TestEqual(TEXT("the player's own Elite kill pays 3"), System->GetNumericAttribute(Pool), 3.0f, 0.001f);
	TestTrue(TEXT("and starts the kill clock"), System->SecondsSinceOwnKill() >= 0.0f);
	TestTrue(TEXT("and holds the decay off"), System->KillDecayIsHeld());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClassKillFervourPreviousKillTest,
	"Cataclysm.ClassKillFervour.AKillsFervourUnderSecondsAfterAKillReadsThePreviousKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClassKillFervourPreviousKillTest::RunTest(const FString&)
{
	using namespace CataclysmClassKillFervourTest;

	// THE PAYMENT IS ASKED BEFORE THE STAMP (ruled 2026-11-25, the Death trees' slice C). A `fervour_per_kill` row under
	// `seconds_after_kill` 2 therefore pays for a kill within 2 seconds of the PREVIOUS kill, and not for the first kill
	// or one after a longer gap. Asked after the stamp, as slice B had it, the window read nought at every kill and the
	// row paid every time. One hand-built row, because the subject is the order in the kill handler, not a table.
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Player =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!State || !Controller || !Player)
	{
		AddError(TEXT("A possessed player could not be built."));
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Player);
	UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent();
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	if (!TestNotNull(TEXT("the player's ability system"), System) || !TestNotNull(TEXT("the combat events"), Events))
	{
		return false;
	}

	FCataclysmStatModifier WithinTwoSeconds;
	WithinTwoSeconds.Bucket = ECataclysmStatBucket::Flat;
	WithinTwoSeconds.Source = ECataclysmModifierSource::PassiveKeystone;
	WithinTwoSeconds.Value = 5.0f;
	WithinTwoSeconds.Condition = ECataclysmStatCondition::WithinSecondsOfKill;
	WithinTwoSeconds.ConditionValue = 2.0f;
	TMap<FName, FCataclysmStatInputs> Inputs;
	FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(UCataclysmFervour::PerKillStat));
	Line.Base = 0.0f;
	Line.Modifiers = {WithinTwoSeconds};
	System->SetStatInputs(MoveTemp(Inputs));
	System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
	System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();

	const auto KillOne = [World, Events, Player](float AlongX)
	{
		ACataclysmEnemyCharacter* Creature =
			World->SpawnActor<ACataclysmEnemyCharacter>(FVector(AlongX, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Creature)
		{
			Creature->SetRarityStep(0);
			FCataclysmDeathNotice Notice;
			Notice.Victim = Creature;
			Notice.Killer = Player;
			Events->OnDeath.Broadcast(Notice);
		}
		return Creature != nullptr;
	};

	// THE FIRST KILL OF ALL: no previous kill, so the window does not hold and nothing is paid. The clock starts.
	if (!TestTrue(TEXT("set-up: a first creature"), KillOne(600.0f)))
	{
		return false;
	}
	TestEqual(TEXT("the first kill, with no kill before it, pays nothing"), System->GetNumericAttribute(Pool), 0.0f,
			  0.001f);
	TestTrue(TEXT("and starts the kill clock"), System->SecondsSinceOwnKill() >= 0.0f);

	// A KILL ONE SECOND LATER: within 2 seconds of the previous one, so the row pays 5.
	RunClock(World, 1.0f);
	if (!TestTrue(TEXT("set-up: a second creature"), KillOne(900.0f)))
	{
		return false;
	}
	TestEqual(TEXT("a kill 1 second after the previous one pays 5"), System->GetNumericAttribute(Pool), 5.0f, 0.001f);

	// AND THE CONTROL ON THE OTHER SIDE: a kill 3 seconds after that one is outside the window and pays nothing more.
	RunClock(World, 3.0f);
	if (!TestTrue(TEXT("set-up: a third creature"), KillOne(1200.0f)))
	{
		return false;
	}
	TestEqual(TEXT("a kill 3 seconds after the previous one pays nothing more"), System->GetNumericAttribute(Pool),
			  5.0f, 0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
