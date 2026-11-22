// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmSkillLevels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmSkillAssignmentWidget.h"
#include "Interface/CataclysmSkillBar.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * THE SKILL-ASSIGNMENT TOOLTIP STATES WHAT A PRESS COSTS, IN THE SKILL BAR'S WORDS. Issue #2804, ruled by the
 * coordinating session on 2026-11-21, replacing Q7's "no numbers": the owner had said of Subjugate's cost "that's all
 * the description tells you". The tooltip prices a skill through a transient template stamped from its row, so the
 * figures are read where the press reads them and the slot's fallback is not written a second time.
 *
 * THE CHECK IS THAT THE TWO SCREENS AGREE: for a skill the Staff player holds, the tooltip's cost line is the skill bar's
 * cost line for the same skill, at level 1 and again after the skill reaches level 10. The player is the possessed Staff
 * player `Cataclysm.LevelCosts.` makes, at character level 32.
 */
namespace CataclysmTooltipCostTest
{
	const FName Subjugate(TEXT("Demonic_Staff_Ultimate"));
	const FName Compel(TEXT("Demonic_Staff_Heavy"));

	struct FTooltipPlayer
	{
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
		bool IsUsable() const { return State && Character && Slots; }
	};

	FTooltipPlayer StaffPlayerIn(UWorld* World)
	{
		FTooltipPlayer Out;
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
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 150.0f);
		Out.State->SetLevelAndExperience(32, 0);
		Out.Character = Character;
		Out.Slots = Slots;
		return Out;
	}

	const FCataclysmWeaponSkill* PoolSkill(const UCataclysmWeaponSlotsComponent* Slots, FName Row)
	{
		return Slots->GetSkillPool().FindByPredicate([Row](const FCataclysmWeaponSkill& Skill) { return Skill.RowName == Row; });
	}

	/** The skill bar's cost line for the box in `Slot`, or "(no box)". */
	FString BarLine(ACataclysmPlayerCharacter* Character, ECataclysmAbilitySlot Slot)
	{
		const TArray<FCataclysmSkillBarSlot> Bar = UCataclysmSkillBar::Read(Character);
		const FCataclysmSkillBarSlot* Box =
			Bar.FindByPredicate([Slot](const FCataclysmSkillBarSlot& One) { return One.Slot == Slot; });
		return Box ? UCataclysmSkillBar::CostLineFor(*Box) : FString(TEXT("(no box)"));
	}

	TArray<FString> LinesOf(const FString& Tip)
	{
		TArray<FString> Lines;
		Tip.ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty=*/false);
		return Lines;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTooltipCostAgreesWithTheBarTest,
	"Cataclysm.TooltipCost.TheAssignmentTooltipStatesTheCostTheSkillBarStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTooltipCostAgreesWithTheBarTest::RunTest(const FString&)
{
	using namespace CataclysmTooltipCostTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FTooltipPlayer Player = StaffPlayerIn(World);
	const FCataclysmWeaponSkill* Take = Player.IsUsable() ? PoolSkill(Player.Slots, Subjugate) : nullptr;
	const FCataclysmWeaponSkill* Lance = Player.IsUsable() ? PoolSkill(Player.Slots, Compel) : nullptr;
	if (!TestTrue(TEXT("set-up: a possessed player whose Staff's pool holds Subjugate and Compel"), Take && Lance))
	{
		return false;
	}

	// AT LEVEL 1: five lines, the cost fourth, the bar's own words.
	const TArray<FString> AtOne =
		LinesOf(UCataclysmSkillAssignmentWidget::ToolTipFor(*Take, ECataclysmAbilitySlot::Ultimate, Player.State));
	const FString BarAtOne = BarLine(Player.Character, ECataclysmAbilitySlot::Ultimate);
	if (!TestEqual(TEXT("the tooltip has five lines: name, slot, weapon, cost, description"), AtOne.Num(), 5))
	{
		return false;
	}
	TestTrue(TEXT("its fourth line is a cost"), AtOne[3].StartsWith(TEXT("Costs ")));
	TestEqual(TEXT("in the skill bar's words for the same skill"), AtOne[3], BarAtOne);
	TestEqual(TEXT("and the description after it"), AtOne[4], Take->Description);
	TestEqual(TEXT("a skill never used is priced at level 1, as the bar prices it"),
			  LinesOf(UCataclysmSkillAssignmentWidget::ToolTipFor(*Lance, ECataclysmAbilitySlot::Heavy, Player.State))[3],
			  BarLine(Player.Character, ECataclysmAbilitySlot::Heavy));

	// AT LEVEL 10: both move, together.
	Player.State->SetSkillExperience(Subjugate, UCataclysmSkillLevels::ExperienceToReach(10));
	TestEqual(TEXT("set-up: Subjugate is at level 10"), Player.State->SkillLevelOf(Subjugate), 10);
	const TArray<FString> AtTen =
		LinesOf(UCataclysmSkillAssignmentWidget::ToolTipFor(*Take, ECataclysmAbilitySlot::Ultimate, Player.State));
	const FString BarAtTen = BarLine(Player.Character, ECataclysmAbilitySlot::Ultimate);
	if (TestEqual(TEXT("still five lines"), AtTen.Num(), 5))
	{
		TestEqual(TEXT("at level 10 the tooltip states the bar's level 10 cost"), AtTen[3], BarAtTen);
		TestFalse(TEXT("which is not level 1's"), AtTen[3] == AtOne[3]);
	}

	// CONTROL: WITH NO HOLDER, THE FOUR LINES AND NO COST.
	const FString Bare = UCataclysmSkillAssignmentWidget::ToolTipFor(*Take, ECataclysmAbilitySlot::Ultimate);
	TestEqual(TEXT("CONTROL: with no holder the tooltip is its four lines"), LinesOf(Bare).Num(), 4);
	TestFalse(TEXT("CONTROL: and names no cost"), Bare.Contains(TEXT("Costs ")));
	AddInfo(FString::Printf(TEXT("TOOLTIP COST: level 1 \"%s\"; level 10 \"%s\""), *AtOne[3], *AtTen[3]));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
