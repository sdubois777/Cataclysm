// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmSkillLevels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A SKILL'S LEVEL LOWERS ITS COSTS, AND WHAT IT MADE IS RE-PRICED WHEN IT LEVELS. Issue #2802.
 *
 * The owner, 2026-11-21: "make it so leveling the skill lowers the cost and increases the damage ... That way, the
 * higher you level Subjugate the more mobs you can control." Ruled by the coordinating session the same day: each
 * level multiplies a skill's mana cost, its Fervour cost and what each thing it makes reserves by 0.9642, so level 20
 * costs half of level 1; and a held reserve is RE-PRICED when its skill levels, not priced once at the take, because a
 * thrall is taken for good.
 *
 * THE PLAYER IS THE ONE `Cataclysm.SkillLevelSources.`'s thrall test makes (`CataclysmSkillLevelSourcesTests.cpp`):
 * possessed, holding a Demonic Staff and the skills it grants with their rows, and a pool of 150, a Ritualist's. Copied
 * into this file's own namespace, because the module is built as a unity blob. Its character is raised to level 32,
 * which is what a skill level of 10 is paired with, so the skill can reach 10.
 */
namespace CataclysmLevelCostsTest
{
	using Levels = UCataclysmSkillLevels;
	constexpr float M = 100.0f;
	const FName Subjugate(TEXT("Demonic_Staff_Ultimate"));
	const FName SummonImp(TEXT("Demonic_Staff_Special"));
	const FName Compel(TEXT("Demonic_Staff_Heavy"));

	/** Level 10's cost factor written out, 0.9642 to the ninth, rather than read back from the code under test. */
	constexpr float AtTen = 0.72028f;

	struct FCostsPlayer
	{
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* Abilities = nullptr;
		bool IsUsable() const { return State && Character && Abilities; }
	};

	FCostsPlayer StaffPlayerIn(UWorld* World)
	{
		FCostsPlayer Out;
		Out.State = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Out.State || !Controller || !Character)
		{
			return Out;
		}
		Controller->SetPlayerState(Out.State);
		Controller->Possess(Character);
		UCataclysmAbilitySystemComponent* System = Out.State->GetCataclysmAbilitySystemComponent();
		UCataclysmWeaponSlotsComponent* Slots = Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>();
		if (!System || !Slots)
		{
			return Out;
		}
		Slots->SetDamageType(TEXT("Demonic"));
		Slots->EquipWeaponType(TEXT("Staff"));
		System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 150.0f);
		Out.State->SetLevelAndExperience(32, 0);
		Out.Character = Character;
		Out.Abilities = System;
		return Out;
	}

	UCataclysmSummonSkill* SummonSkillOfRow(UAbilitySystemComponent* Abilities, FName Row)
	{
		for (const FGameplayAbilitySpec& Spec : Abilities->GetActivatableAbilities())
		{
			UCataclysmSummonSkill* Skill = Cast<UCataclysmSummonSkill>(Spec.GetPrimaryInstance());
			if (Skill && Skill->SkillRow == Row)
			{
				return Skill;
			}
		}
		return nullptr;
	}

	const UCataclysmSkillTemplate* SkillOfRow(UAbilitySystemComponent* Abilities, FName Row)
	{
		for (const FGameplayAbilitySpec& Spec : Abilities->GetActivatableAbilities())
		{
			const UCataclysmSkillTemplate* Skill = Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());
			if (Skill && Skill->SkillRow == Row)
			{
				return Skill;
			}
		}
		return nullptr;
	}

	/** A creature hurt to 520 of 1,000, so Subjugate's own blow carries it under half, as the thrall test's is. */
	ACataclysmEnemyCharacter* CreatureToTake(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetHealth(1'000.0f);
			Made->SetAttackDamage(100.0f);
			Made->GetAbilitySystemComponent()->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(),
																	  520.0f);
		}
		return Made;
	}

	float CeilingOf(const UCataclysmAbilitySystemComponent* Abilities)
	{
		const UCataclysmClassResourceAttributeSet* Resource = Abilities->GetSet<UCataclysmClassResourceAttributeSet>();
		return Resource ? Resource->MaximumClassResourceAsked() : -1.0f;
	}
}

// --------------------------------------------------------------------------------------------------------------------
// LC1. The factor: 1 at level 1, 0.9642 a level, half at level 20, and clamped at both ends.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLevelCostsFactorTest,
	"Cataclysm.LevelCosts.EachLevelLowersACostAndLevelTwentyHalvesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLevelCostsFactorTest::RunTest(const FString&)
{
	using namespace CataclysmLevelCostsTest;
	TestEqual(TEXT("each level multiplies a cost by 0.9642"), Levels::CostFactorPerLevel, 0.9642);
	TestEqual(TEXT("level 1 costs what the skill states"), Levels::CostFactor(1), 1.0);
	TestEqual(TEXT("level 10 costs 0.9642 to the ninth of it"), static_cast<float>(Levels::CostFactor(10)), AtTen, 1.0e-4f);
	TestEqual(TEXT("level 20 costs half"), static_cast<float>(Levels::CostFactor(20)), 0.5f, 1.0e-3f);
	TestEqual(TEXT("clamped above 20"), Levels::CostFactor(25), Levels::CostFactor(20));
	TestEqual(TEXT("and below 1"), Levels::CostFactor(0), 1.0);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// LC2. A skill's Fervour and mana costs fall with its level; another skill's do not.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLevelCostsCostsFallTest,
	"Cataclysm.LevelCosts.ASkillsManaAndFervourCostsFallWithItsLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLevelCostsCostsFallTest::RunTest(const FString&)
{
	using namespace CataclysmLevelCostsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FCostsPlayer Player = StaffPlayerIn(World);
	const UCataclysmSkillTemplate* Take = Player.IsUsable() ? SkillOfRow(Player.Abilities, Subjugate) : nullptr;
	const UCataclysmSkillTemplate* Lance = Player.IsUsable() ? SkillOfRow(Player.Abilities, Compel) : nullptr;
	if (!TestTrue(TEXT("set-up: a possessed player holding the Staff's Subjugate and Compel"), Take && Lance))
	{
		return false;
	}
	const float FervourAtOne = Take->FervourCostFor(Player.Abilities);
	const float ManaAtOne = Take->ManaCostFor(Player.Abilities);
	const float CompelManaBefore = Lance->ManaCostFor(Player.Abilities);
	if (!TestTrue(TEXT("set-up: Subjugate costs Fervour and mana at level 1"), FervourAtOne > 0.0f && ManaAtOne > 0.0f))
	{
		return false;
	}

	Player.State->SetSkillExperience(Subjugate, Levels::ExperienceToReach(10));
	if (!TestEqual(TEXT("set-up: Subjugate is at level 10"), Player.State->SkillLevelOf(Subjugate), 10))
	{
		return false;
	}
	TestEqual(TEXT("its Fervour cost is level 10's factor times level 1's"), Take->FervourCostFor(Player.Abilities),
			  FervourAtOne * AtTen, 0.01f);
	TestEqual(TEXT("and its mana cost too"), Take->ManaCostFor(Player.Abilities), ManaAtOne * AtTen, 0.01f);
	TestEqual(TEXT("CONTROL: Compel, not levelled, costs what it did"), Lance->ManaCostFor(Player.Abilities),
			  CompelManaBefore, 0.001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// LC3. A thrall taken at level 1 holds 30; another skill's level leaves it; its own skill reaching level 10 re-prices
// it to 21.6 with no new take, and the ceiling rises by the difference.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLevelCostsRepriceTest,
	"Cataclysm.LevelCosts.AHeldThrallIsRepricedWhenItsSkillLevels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLevelCostsRepriceTest::RunTest(const FString&)
{
	using namespace CataclysmLevelCostsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FCostsPlayer Player = StaffPlayerIn(World);
	UCataclysmSummonSkill* Take = Player.IsUsable() ? SummonSkillOfRow(Player.Abilities, Subjugate) : nullptr;
	ACataclysmEnemyCharacter* Thrall = CreatureToTake(World, FVector(3.0f * M, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: a possessed player holding the Staff's Subjugate, and a wounded creature"),
				  Take && Thrall && SummonSkillOfRow(Player.Abilities, SummonImp)))
	{
		return false;
	}
	// NEVER A CRITICAL STRIKE, so the blow cannot take more than the 520 the creature has left and kill it.
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	if (!TestTrue(TEXT("set-up: Subjugate takes it at level 1"), Take->Possess()))
	{
		return false;
	}
	TestEqual(TEXT("taken at level 1, it holds the row's 30"), Thrall->ReservedFervour, 30.0f, 0.001f);
	const float CeilingAtOne = CeilingOf(Player.Abilities);
	TestEqual(TEXT("set-up: so the pool of 150 can fill to 120"), CeilingAtOne, 120.0f, 0.001f);

	// CONTROL: ANOTHER SKILL'S LEVEL LEAVES IT.
	Player.State->SetSkillExperience(SummonImp, Levels::ExperienceToReach(10));
	TestEqual(TEXT("set-up: Summon Imp is at level 10"), Player.State->SkillLevelOf(SummonImp), 10);
	TestEqual(TEXT("CONTROL: Summon Imp's level leaves the thrall at 30"), Thrall->ReservedFervour, 30.0f, 0.001f);

	// ITS OWN SKILL'S LEVEL RE-PRICES IT, WITH NO NEW TAKE.
	Player.State->SetSkillExperience(Subjugate, Levels::ExperienceToReach(10));
	TestEqual(TEXT("set-up: Subjugate is at level 10"), Player.State->SkillLevelOf(Subjugate), 10);
	TestEqual(TEXT("at Subjugate's level 10 the same thrall holds 30 x 0.72028, 21.6"), Thrall->ReservedFervour,
			  30.0f * AtTen, 0.01f);
	TestEqual(TEXT("and the ceiling rises by the 8.4 it no longer holds"), CeilingOf(Player.Abilities) - CeilingAtOne,
			  30.0f - 30.0f * AtTen, 0.01f);
	AddInfo(FString::Printf(TEXT("REPRICED: the thrall held 30 at level 1 and %.2f at level 10; the ceiling %.2f to %.2f"),
							Thrall->ReservedFervour, CeilingAtOne, CeilingOf(Player.Abilities)));
	return true;
}


// --------------------------------------------------------------------------------------------------------------------
// LC4. A skill gadget and a skill trap reserve at the level of the skill in their slot: 15 and 5 at level 1, and when
// that skill reaches level 10 the standing ones are re-priced to 15 x 0.72028 and 5 x 0.72028. A trap for a slot holding
// another skill keeps its 5.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLevelCostsGadgetAndTrapTest,
	"Cataclysm.LevelCosts.ASkillGadgetAndASkillTrapReserveAtTheirSlotSkillsLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The slot's skill makes a skill gadget and a skill trap, so its level prices their reserve, as a summon's level prices
 * its minions'. Issue #2802's own commit after the rebase onto branch B of #2787, which wrote these two reserves. The
 * Staff's Heavy slot holds Compel, its Ultimate Subjugate. Placed directly, as `Cataclysm.KindReserves.`'s trap test
 * places its traps, since the press is not what is checked here.
 */
bool FCataclysmLevelCostsGadgetAndTrapTest::RunTest(const FString&)
{
	using namespace CataclysmLevelCostsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FCostsPlayer Player = StaffPlayerIn(World);
	const ECataclysmAbilitySlot Heavy = ECataclysmAbilitySlot::Heavy;
	const ECataclysmAbilitySlot Ultimate = ECataclysmAbilitySlot::Ultimate;
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable())
		|| !TestEqual(TEXT("set-up: the Heavy slot holds Compel"), Levels::RowInSlot(Player.Character, Heavy), Compel)
		|| !TestEqual(TEXT("set-up: the Ultimate slot holds Subjugate"), Levels::RowInSlot(Player.Character, Ultimate),
					  Subjugate))
	{
		return false;
	}

	// AT LEVEL 1, THE STATED FIGURES.
	ACataclysmMinion* Gadget = ACataclysmMinion::PlaceSkillGadget(Player.Character, Heavy, FVector(3.0f * M, 0, 0));
	ACataclysmMinion* Trap = ACataclysmMinion::PlaceSkillTrap(Player.Character, Heavy, FVector(0, 3.0f * M, 0));
	ACataclysmMinion* OtherTrap =
		ACataclysmMinion::PlaceSkillTrap(Player.Character, Ultimate, FVector(0, -3.0f * M, 0));
	if (!TestTrue(TEXT("set-up: a gadget and a trap for the Heavy slot and a trap for the Ultimate are placed"),
				  Gadget && Trap && OtherTrap))
	{
		return false;
	}
	TestEqual(TEXT("at Compel's level 1 its gadget holds 15"), Gadget->ReservedFervour,
			  ACataclysmMinion::SkillGadgetFervourReserve, 0.001f);
	TestEqual(TEXT("and its trap 5"), Trap->ReservedFervour, ACataclysmMinion::SkillTrapFervourReserve, 0.001f);

	// COMPEL'S LEVEL RE-PRICES WHAT ITS SLOT MADE, WITH NOTHING PLACED AGAIN.
	Player.State->SetSkillExperience(Compel, Levels::ExperienceToReach(10));
	TestEqual(TEXT("set-up: Compel is at level 10"), Player.State->SkillLevelOf(Compel), 10);
	TestEqual(TEXT("at Compel's level 10 the same gadget holds 15 x 0.72028, 10.8"), Gadget->ReservedFervour,
			  15.0f * AtTen, 0.01f);
	TestEqual(TEXT("and the same trap 5 x 0.72028, 3.6"), Trap->ReservedFervour, 5.0f * AtTen, 0.01f);
	TestEqual(TEXT("CONTROL: the Ultimate slot's trap, Subjugate's, keeps 5"), OtherTrap->ReservedFervour,
			  ACataclysmMinion::SkillTrapFervourReserve, 0.001f);

	// AND ONE PLACED NOW IS PRICED AT THE LEVEL.
	const ACataclysmMinion* NewTrap = ACataclysmMinion::PlaceSkillTrap(Player.Character, Heavy, FVector(0, 6.0f * M, 0));
	if (TestNotNull(TEXT("a second Heavy trap is placed"), NewTrap))
	{
		TestEqual(TEXT("and holds 3.6 from the start"), NewTrap->ReservedFervour, 5.0f * AtTen, 0.01f);
	}
	AddInfo(FString::Printf(TEXT("GADGET AND TRAP: the gadget held 15 at level 1 and %.2f at level 10; the trap 5 and %.2f"),
							Gadget->ReservedFervour, Trap->ReservedFervour));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
