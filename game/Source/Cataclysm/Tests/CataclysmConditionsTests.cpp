// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Items/CataclysmItem.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Two conditions on one modifier, a threshold that rolls, and the share of
 * armour a critical strike ignores. Issue #1833 group C part 3a, ruled
 * 2026-09-30 under the owner's delegation.
 *
 * EACH TEST STATES THE MODIFIER OR THE ROW DIRECTLY, because what is under test
 * is what the engine does with it. The rows are measured in
 * CataclysmEnchantmentEffectTests.cpp, where a real enchantment is worn.
 */
namespace CataclysmConditionsTest
{
	/** A bare actor with an ability system and the sets a blow needs at both ends. */
	struct FCritStriker
	{
		explicit FCritStriker(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 100.0f);
			// A MULTIPLIER OF 100, so a critical strike changes no damage and
			// only the armour it ignores tells the two blows apart.
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetCritMultiplierAttribute(), 100.0f);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTwoConditionsTest,
	"Cataclysm.Conditions.AModifierWithTwoConditionsAppliesOnlyWhenBothHold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "You take 20%-35% increased damage from melee attacks while your HP is above
 * 75%": 35% increased on a base of 100, asked with a melee blow above 75%
 * health, then with each of the two conditions failing in turn.
 */
bool FCataclysmTwoConditionsTest::RunTest(const FString&)
{
	FCataclysmStatModifier Row;
	Row.Bucket = ECataclysmStatBucket::Increased;
	Row.Source = ECataclysmModifierSource::Enchantment;
	Row.Value = 35.0f;
	Row.Condition = ECataclysmStatCondition::HitIsMeleeAttack;
	Row.Condition2 = ECataclysmStatCondition::HealthAbovePercent;
	Row.ConditionValue2 = 75.0f;

	TestEqual(TEXT("the modifier with a second condition validates"),
			  UCataclysmStatPipeline::ValidateModifier(Row), FString());

	const auto Asked = [&Row](float HealthPercent, bool bMelee)
	{
		FCataclysmStatConditions State = FCataclysmStatConditions::FromHealth(HealthPercent, 100.0f);
		State.Blow.bIsMelee = bMelee;
		State.Blow.bIsRanged = !bMelee;
		return UCataclysmStatPipeline::Evaluate(100.0f, {Row}, FGameplayTagContainer(), State).Final;
	};

	TestEqual(TEXT("a melee hit above 75% health: both hold, 135"), Asked(80.0f, true), 135.0f, 0.001f);
	TestEqual(TEXT("a melee hit at half health: the second fails, 100"), Asked(50.0f, true), 100.0f, 0.001f);
	TestEqual(TEXT("a ranged hit above 75% health: the first fails, 100"), Asked(80.0f, false), 100.0f, 0.001f);

	FCataclysmStatModifier Bounded = Row;
	Bounded.ConditionValue2 = 150.0f;
	TestFalse(TEXT("a second health threshold of 150 is refused as the first would be"),
			  UCataclysmStatPipeline::ValidateModifier(Bounded).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRolledThresholdTest,
	"Cataclysm.Conditions.AThresholdStatedAsARangeRollsWithTheValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your abilities are free when above 80%-95% hp" writes 80 and 95, and the
 * item's roll picks the threshold where it picks the value. A row stating one
 * threshold keeps it at every roll.
 */
bool FCataclysmRolledThresholdTest::RunTest(const FString&)
{
	FCataclysmEnchantmentEffectRow Ranged;
	Ranged.ConditionValue = 80.0f;
	Ranged.ConditionValueHigh = 95.0f;
	TestEqual(TEXT("the lowest roll: 80"), UCataclysmItemModifiers::RolledConditionValue(Ranged, 0.0f), 80.0f, 0.001f);
	TestEqual(TEXT("the highest roll: 95"), UCataclysmItemModifiers::RolledConditionValue(Ranged, 1.0f), 95.0f, 0.001f);

	FCataclysmEnchantmentEffectRow Single;
	Single.ConditionValue = 75.0f;
	TestEqual(TEXT("one stated threshold at the highest roll: still 75"),
			  UCataclysmItemModifiers::RolledConditionValue(Single, 1.0f), 75.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCriticalArmourTest,
	"Cataclysm.Conditions.ACriticalStrikeIgnoresItsShareOfArmourAndAPlainHitDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * An attacker carrying 100 flat `critical_armor_penetration` strikes an
 * armoured defender twice: once with the roll pinned so it critically strikes,
 * once so it does not. Only the critical blow ignores the armour.
 */
bool FCataclysmCriticalArmourTest::RunTest(const FString&)
{
	using namespace CataclysmConditionsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	IConsoleVariable* Roll = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.CritRoll"));
	if (!TestNotNull(TEXT("the critical strike roll can be pinned"), Roll))
	{
		return false;
	}
	const float PreviousRoll = Roll->GetFloat();
	ON_SCOPE_EXIT { Roll->Set(PreviousRoll, ECVF_SetByConsole); };

	FCritStriker Attacker(World);
	FCritStriker Defender(World);
	Defender.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetArmorAttribute(), 1000.0f);

	FCataclysmStatModifier All;
	All.Bucket = ECataclysmStatBucket::Flat;
	All.Source = ECataclysmModifierSource::Enchantment;
	All.Value = 100.0f;
	TMap<FName, FCataclysmStatInputs> Inputs;
	FCataclysmStatInputs& Line =
		Inputs.FindOrAdd(FName(UCataclysmDamageCalculation::CriticalArmorPenetrationStat));
	Line.Base = 0.0f;
	Line.Modifiers = {All};
	Attacker.AbilitySystem->SetStatInputs(MoveTemp(Inputs));

	Roll->Set(0.0f, ECVF_SetByConsole);
	FCataclysmDamageResult Critical;
	UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
									 FGameplayTagContainer(), FCataclysmHitDelivery(), &Critical);
	Roll->Set(100.0f, ECVF_SetByConsole);
	FCataclysmDamageResult Plain;
	UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
									 FGameplayTagContainer(), FCataclysmHitDelivery(), &Plain);

	if (!TestTrue(TEXT("the first blow critically struck and the second did not"),
				  Critical.bWasCritical && !Plain.bWasCritical))
	{
		return false;
	}
	TestEqual(TEXT("the critical blow ignores all the armour"), Critical.RemovedByArmour, 0.0f, 0.01f);
	TestTrue(*FString::Printf(TEXT("and a plain hit loses its share to armour: %.2f"), Plain.RemovedByArmour),
			 Plain.RemovedByArmour > 0.0f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
