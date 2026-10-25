// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * A DIAGNOSTIC: can a creature carry a player's numbers? The Corrupted Stalker, part B layer 1 (issue #1308).
 *
 * WHY. The Stalker is "rebuilt to the peak of player power for this dungeon tier". Every player attribute is written
 * by `UCataclysmPlayerClassStats::ApplyTo`, which takes any ability system, an explicit class, a level and the
 * modifiers. By reading, three things stand in a creature's way: it holds three of the five attribute sets (Vital,
 * Combat and AllResistance, not Primary, the typed Resistance set or ClassResource); its archetype code
 * (`ApplyStartingAttributes`) writes armour, crit, attack damage, regeneration and shield again whenever one of its
 * setters runs; and the passive tree reaches a player only through a player state. The coordinating session ruled on
 * 2026-10-25 that this is measured before layer 1 is written.
 *
 * IT MEASURES AND DOES NOT JUDGE. A level 100 Ravager with thirteen points in its tree is applied to a creature, after
 * the creature is given the three sets it lacks, and to a control actor holding all five sets from the start, as
 * `CataclysmEquipmentTests.cpp`'s `FScopedCharacter` does. Both are printed in the engine log line beginning
 * "PLAYER BUILD:", and the creature's figures again, after one archetype setter has run, in the line beginning "AFTER
 * A SETTER:". What it asserts is its set-up. Layer 1 replaces it with tests that assert what the Stalker must hold.
 */
namespace CataclysmCreatureBuildTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;
	using Primary = UCataclysmPrimaryAttributeSet;
	using Typed = UCataclysmResistanceAttributeSet;

	float Read(const UAbilitySystemComponent* System, const FGameplayAttribute& Attribute)
	{
		return System && System->HasAttributeSetForAttribute(Attribute) ? System->GetNumericAttribute(Attribute) : -1.0f;
	}

	FString Figures(const UAbilitySystemComponent* System)
	{
		return FString::Printf(TEXT("max health %.1f, mana %.1f, armour %.1f, attack damage %.1f, crit %.2f, "
									"constitution %.1f, demonic resistance %.1f"),
							   Read(System, Vital::GetMaxHealthAttribute()), Read(System, Vital::GetMaxManaAttribute()),
							   Read(System, Combat::GetArmorAttribute()), Read(System, Combat::GetAttackDamageAttribute()),
							   Read(System, Combat::GetCritChanceAttribute()),
							   Read(System, Primary::GetConstitutionAttribute()),
							   Read(System, Typed::GetDemonicResistanceAttribute()));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreatureWearsAPlayerBuildTest,
	"Cataclysm.CorruptedStalker.WhatAPlayerBuildGivesACreature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreatureWearsAPlayerBuildTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCreatureBuildTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const UDataTable* ClassTable = UCataclysmPlayerClassStats::LoadTable();
	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the class table"), ClassTable) || !TestNotNull(TEXT("set-up: the node table"), Nodes)
		|| !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}

	// THIRTEEN POINTS: the Ravager's root and Thick Hide in full, +1% armour and +0.5% maximum health a point.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Ravager_basic_spine_000")), 1);
	Allocation.Add(FName(TEXT("Ravager_basic_spine_001")), 12);
	const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers = UCataclysmPassiveTree::ModifiersFor(
		Allocation, Nodes, Effects, { FName(TEXT("Demonic")) });
	AddInfo(FString::Printf(TEXT("PLAYER BUILD: the allocation gives modifiers on %d stats"), Modifiers.Num()));

	// THE CONTROL: an actor holding all five sets from the start.
	AActor* Control = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("set-up: a control actor"), Control))
	{
		return false;
	}
	UCataclysmAbilitySystemComponent* ControlSystem = NewObject<UCataclysmAbilitySystemComponent>(Control);
	ControlSystem->RegisterComponent();
	ControlSystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Control));
	ControlSystem->AddAttributeSetSubobject(NewObject<UCataclysmPrimaryAttributeSet>(Control));
	ControlSystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Control));
	ControlSystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Control));
	ControlSystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Control));
	ControlSystem->InitAbilityActorInfo(Control, Control);
	const int32 ControlWritten = UCataclysmPlayerClassStats::ApplyTo(ControlSystem, ClassTable, TEXT("Ravager"), 100,
																	 &Modifiers);

	// THE CREATURE, after its BeginPlay, given the three sets it lacks.
	ACataclysmEnemyCharacter* Creature =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	UAbilitySystemComponent* CreatureSystem = UCataclysmTargeting::AbilitySystemOf(Creature);
	UCataclysmAbilitySystemComponent* CataclysmSystem = Cast<UCataclysmAbilitySystemComponent>(CreatureSystem);
	if (!TestNotNull(TEXT("set-up: the creature's ability system"), CataclysmSystem))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("PLAYER BUILD: the creature as spawned: %s"), *Figures(CreatureSystem)));
	CataclysmSystem->AddAttributeSetSubobject(NewObject<UCataclysmPrimaryAttributeSet>(Creature));
	CataclysmSystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Creature));
	CataclysmSystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Creature));
	const int32 CreatureWritten = UCataclysmPlayerClassStats::ApplyTo(CreatureSystem, ClassTable, TEXT("Ravager"), 100,
																	  &Modifiers);

	AddInfo(FString::Printf(TEXT("PLAYER BUILD: control, %d stats written: %s"), ControlWritten,
							*Figures(ControlSystem)));
	AddInfo(FString::Printf(TEXT("PLAYER BUILD: creature, %d stats written: %s"), CreatureWritten,
							*Figures(CreatureSystem)));

	// ONE ARCHETYPE SETTER, which runs `ApplyStartingAttributes` again.
	Creature->SetRarityStep(0);
	AddInfo(FString::Printf(TEXT("AFTER A SETTER: creature: %s"), *Figures(CreatureSystem)));

	TestTrue(TEXT("set-up: the control was given a maximum health"),
			 Read(ControlSystem, Vital::GetMaxHealthAttribute()) > 0.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
