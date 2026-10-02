// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * Issue #1833 group E part 2, ruled 2026-10-02 under the owner's delegation:
 * the share a block removes is the defender stat `block_damage_reduction`, on a
 * base of 50 and capped at 85; a block may remove all of a hit on the defender's
 * `block_negation_chance`; a block may open the no-damage window a survived
 * lethal hit opens; and regeneration filling the energy shield is the event
 * `energy_shield_recharged`.
 *
 * EVERY BLOW IS REAL: `ApplyHit` through the damage calculation and the
 * attribute set, with the block roll, the negation roll and the critical strike
 * roll pinned. Each action is built by hand, as the item would build it from a
 * row; the rows have tests of their own.
 */
namespace CataclysmBlockShareTest
{
	/** A bare actor holding the four sets, with a thousand health and no armour. */
	struct FFighter
	{
		FFighter(UWorld* World, float AttackDamage)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// Raw pointers on purpose: AddAttributeSetSubobject is a template and
			// a TObjectPtr deduces the wrapper rather than the set.
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmAllResistanceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), AttackDamage);
		}

		~FFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		/** One line, `Base` plus a flat `Flat`, on the stat named. */
		void Line(const TCHAR* Stat, float Base, float Flat = 0.0f) const
		{
			TMap<FName, FCataclysmStatInputs> Lines;
			FCataclysmStatInputs& Inputs = Lines.FindOrAdd(FName(Stat));
			Inputs.Base = Base;
			if (Flat != 0.0f)
			{
				FCataclysmStatModifier Added;
				Added.Bucket = ECataclysmStatBucket::Flat;
				Added.Source = ECataclysmModifierSource::Enchantment;
				Added.Value = Flat;
				Inputs.Modifiers = {Added};
			}
			AbilitySystem->SetStatInputs(MoveTemp(Lines));
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	/** Pins one console variable at the console's priority until it goes out of scope. */
	struct FPinned
	{
		FPinned(const TCHAR* Name, float Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}
		~FPinned()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};

	/** What one blow of `Attacker`'s, at 100% of its attack, took from `Defender`, and whether it was blocked. */
	float Blow(const FFighter& Attacker, const FFighter& Defender, bool* bOutBlocked = nullptr)
	{
		FCataclysmDamageResult Result;
		const float Before = Defender.Get(UCataclysmVitalAttributeSet::GetHealthAttribute());
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
			FGameplayTagContainer(), FCataclysmHitDelivery(), &Result);
		if (bOutBlocked)
		{
			*bOutBlocked = Result.bBlocked;
		}
		return Before - Defender.Get(UCataclysmVitalAttributeSet::GetHealthAttribute());
	}

	/** A no-damage window action, as the item would build it from a row. */
	FCataclysmPoolAction Immunity(const TCHAR* Event, float Seconds, float TriggerCooldownSeconds)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(Event);
		Action.Pool = FName(UCataclysmAbilitySystemComponent::DamageImmunityAction);
		Action.Percent = Seconds;
		Action.bDamageImmunity = true;
		Action.TriggerCooldownSeconds = TriggerCooldownSeconds;
		Action.TriggerKey = FName(*FString::Printf(TEXT("A_row:damage_immunity:%s"), Event));
		return Action;
	}

	/** How many times one event is raised on a component while this is alive. */
	struct FEventCount
	{
		FEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
			: System(InSystem), Event(InEvent)
		{
			Handle = System->OnActionEvent.AddLambda([this](FName Raised)
			{
				Count += Raised == Event ? 1 : 0;
			});
		}
		~FEventCount()
		{
			System->OnActionEvent.Remove(Handle);
		}
		UCataclysmAbilitySystemComponent* System = nullptr;
		FName Event;
		int32 Count = 0;
		FDelegateHandle Handle;
	};
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_TEST(FCataclysmBlockShareStatTest,
	"Cataclysm.BlockShare.TheShareABlockRemovesIsItsStatOnABaseOfFiftyAndStopsAtEightyFive")
{
	using namespace CataclysmBlockShareTest;
	// A BLOCKED BLOW, AGAINST THE SAME BLOW UNBLOCKED. A defender with no stat
	// line removes the base 50 and keeps half; one carrying 50 plus 15 removes 65
	// and keeps 35%; one carrying 50 plus 50 would remove 100 and is held at 85,
	// keeping 15%.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	{
		const FFighter Attacker(World, 100.0f);
		const FFighter Unblocked(World, 0.0f);
		const FFighter Plain(World, 0.0f);
		const FFighter Raised(World, 0.0f);
		const FFighter PastTheCap(World, 0.0f);
		for (const FFighter* Defender : {&Unblocked, &Plain, &Raised, &PastTheCap})
		{
			Defender->Set(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 100.0f);
		}
		float Full = 0.0f;
		{
			const FPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);
			Full = Blow(Attacker, Unblocked);
		}
		if (!TestTrue(*FString::Printf(TEXT("set-up: an unblocked blow took %.1f"), Full), Full > 0.0f))
		{
			return false;
		}
		const FPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
		Raised.Line(UCataclysmDamageCalculation::BlockDamageReductionStat, 50.0f, 15.0f);
		PastTheCap.Line(UCataclysmDamageCalculation::BlockDamageReductionStat, 50.0f, 50.0f);

		TestEqual(TEXT("no stat line: the base 50"),
			UCataclysmDamageCalculation::BlockShareOf(Plain.AbilitySystem, FCataclysmBlowContext()),
			50.0f, 0.001f);
		TestEqual(TEXT("50 plus 15: 65"),
			UCataclysmDamageCalculation::BlockShareOf(Raised.AbilitySystem, FCataclysmBlowContext()),
			65.0f, 0.001f);
		TestEqual(TEXT("50 plus 50: held at 85"),
			UCataclysmDamageCalculation::BlockShareOf(PastTheCap.AbilitySystem, FCataclysmBlowContext()),
			85.0f, 0.001f);

		bool bBlocked = false;
		TestEqual(TEXT("the plain defender keeps half of a blocked blow"),
			Blow(Attacker, Plain, &bBlocked), Full * 0.5f, 0.5f);
		TestTrue(TEXT("and the blow was blocked"), bBlocked);
		TestEqual(TEXT("removing 65 keeps 35%"), Blow(Attacker, Raised), Full * 0.35f, 0.5f);
		TestEqual(TEXT("removing 85, not 100, keeps 15%"), Blow(Attacker, PastTheCap), Full * 0.15f, 0.5f);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmBlockNegationTest,
	"Cataclysm.BlockShare.ABlockNegatesAllOfAHitOnItsChanceAndOnlyWhenItBlocks")
{
	using namespace CataclysmBlockShareTest;
	// A DEFENDER CARRYING A NEGATION CHANCE OF 100. A blow that is not blocked
	// keeps all of itself, because the negation is rolled only inside a block; a
	// blocked blow, its negation roll at 0, keeps nothing; and the same blow with
	// the negation roll at 100 keeps the ordinary half.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	{
		const FFighter Attacker(World, 100.0f);
		const FFighter Defender(World, 0.0f);
		Defender.Set(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 100.0f);
		Defender.Line(UCataclysmDamageCalculation::BlockNegationChanceStat, 100.0f);
		float Full = 0.0f;
		{
			const FPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);
			const FPinned Negates(TEXT("Cataclysm.BlockNegationRoll"), 0.0f);
			bool bBlocked = true;
			Full = Blow(Attacker, Defender, &bBlocked);
			TestTrue(*FString::Printf(TEXT("a blow that is not blocked keeps all of itself: %.1f"), Full),
				Full > 0.0f);
			TestFalse(TEXT("and was not blocked"), bBlocked);
		}
		{
			const FPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
			{
				const FPinned Negates(TEXT("Cataclysm.BlockNegationRoll"), 0.0f);
				bool bBlocked = false;
				TestEqual(TEXT("a negated block keeps nothing"), Blow(Attacker, Defender, &bBlocked), 0.0f, 0.001f);
				TestTrue(TEXT("and the blow was blocked"), bBlocked);
			}
			{
				const FPinned DoesNotNegate(TEXT("Cataclysm.BlockNegationRoll"), 100.0f);
				TestEqual(TEXT("a block whose negation roll fails keeps half"),
					Blow(Attacker, Defender), Full * 0.5f, 0.5f);
			}
		}
	}
	return true;
}

CATACLYSM_TEST(FCataclysmGrantedImmunityTest,
	"Cataclysm.BlockShare.AGrantedImmunityEmptiesEveryBlowAndTickUntilItsLaterEnd")
{
	using namespace CataclysmBlockShareTest;
	// THE WINDOW A SURVIVED LETHAL HIT OPENS, opened by `GrantDamageImmunity`: a
	// blow and a damage over time tick inside it take nothing; a shorter grant
	// inside a longer one does not cut it short; and after its end a blow lands.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);
	const FGameplayTag Poison = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Keyword.DoT.Poison")), /*ErrorIfNotFound=*/false);
	if (!TestTrue(TEXT("set-up: the poison tag exists"), Poison.IsValid()))
	{
		return false;
	}
	{
		const FFighter Attacker(World, 100.0f);
		const FFighter Defender(World, 0.0f);
		Defender.AbilitySystem->GrantDamageImmunity(3.0f);
		TestTrue(TEXT("the window is open"), Defender.AbilitySystem->IsDamageImmune());
		TestEqual(TEXT("a blow inside it takes nothing"), Blow(Attacker, Defender), 0.0f, 0.001f);
		if (!TestTrue(TEXT("set-up: a poison lands"),
				UCataclysmSkillEffects::ApplyDamageOverTime(Attacker.Actor, Defender.Actor,
					/*DamagePerTick=*/10.0f, /*DurationSeconds=*/10.0f, Poison,
					/*bScalesWithInstigator=*/false)))
		{
			return false;
		}
		CataclysmTestWorld::RunClock(World, 1.5f);
		TestEqual(TEXT("and a tick inside it takes nothing"),
			Defender.Get(UCataclysmVitalAttributeSet::GetHealthAttribute()), 1000.0f, 0.001f);

		Defender.AbilitySystem->GrantDamageImmunity(0.5f);
		CataclysmTestWorld::RunClock(World, 1.0f);
		TestTrue(TEXT("a shorter grant did not cut the window short"), Defender.AbilitySystem->IsDamageImmune());
		CataclysmTestWorld::RunClock(World, 1.0f);
		TestFalse(TEXT("3.5 seconds on the window has ended"), Defender.AbilitySystem->IsDamageImmune());
		TestTrue(TEXT("and a blow lands"), Blow(Attacker, Defender) > 0.0f);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmBlockOpensImmunityTest,
	"Cataclysm.BlockShare.ABlockOpensTheWindowThroughTheActionOnceInItsCooldown")
{
	using namespace CataclysmBlockShareTest;
	// "When you block an attack, you become immune to all damage for 3 seconds.
	// (10s cd)", BY HAND: a real blocked blow raises `block`, which opens the
	// window; four seconds on, the window has ended and another block opens
	// nothing, because ten seconds have not passed; eleven seconds on, a block
	// opens it again.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
	{
		const FFighter Attacker(World, 100.0f);
		const FFighter Defender(World, 0.0f);
		Defender.Set(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 100.0f);
		Defender.AbilitySystem->SetPoolActions({Immunity(TEXT("block"), 3.0f, 10.0f)});
		const FEventCount Blocks(Defender.AbilitySystem, TEXT("block"));

		bool bBlocked = false;
		const float First = Blow(Attacker, Defender, &bBlocked);
		if (!TestTrue(TEXT("set-up: the first blow was blocked and landed"), bBlocked && First > 0.0f)
			|| !TestEqual(TEXT("set-up: it raised block"), Blocks.Count, 1))
		{
			return false;
		}
		TestTrue(TEXT("the block opened the window"), Defender.AbilitySystem->IsDamageImmune());
		TestEqual(TEXT("so the next blow takes nothing"), Blow(Attacker, Defender), 0.0f, 0.001f);

		World->TimeSeconds += 4.0f;
		if (!TestFalse(TEXT("set-up: four seconds on the window has ended"),
				Defender.AbilitySystem->IsDamageImmune()))
		{
			return false;
		}
		TestTrue(TEXT("a block then lands its share"), Blow(Attacker, Defender) > 0.0f);
		TestFalse(TEXT("and opens nothing: ten seconds have not passed"),
			Defender.AbilitySystem->IsDamageImmune());

		World->TimeSeconds += 7.0f;
		Blow(Attacker, Defender);
		TestTrue(TEXT("eleven seconds on a block opens it again"), Defender.AbilitySystem->IsDamageImmune());
	}
	return true;
}

CATACLYSM_TEST(FCataclysmShieldRechargedTest,
	"Cataclysm.BlockShare.RegenerationFillingTheShieldRaisesRechargedOnceAndACeilingNever")
{
	using namespace CataclysmBlockShareTest;
	// A SHIELD OF 100 AT 40, regenerating 100 a second: a step that leaves it below
	// its maximum raises nothing, the step that fills it raises
	// `energy_shield_recharged` once, and a step at full raises nothing more. A
	// character whose recharge ceiling is half its maximum never fills it, and
	// never raises it.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	{
		const FFighter Shielded(World, 0.0f);
		Shielded.Set(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 100.0f);
		Shielded.Set(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 40.0f);
		Shielded.Set(UCataclysmVitalAttributeSet::GetEnergyShieldRegenAttribute(), 100.0f);
		const FEventCount Recharged(Shielded.AbilitySystem, TEXT("energy_shield_recharged"));

		UCataclysmRegeneration::ApplyStep(Shielded.Actor, /*SecondsInStep=*/0.25f, /*SecondsSinceLastDamage=*/100.0f);
		const float Partly = Shielded.Get(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute());
		if (!TestTrue(*FString::Printf(TEXT("set-up: a quarter second refilled part of it: %.1f"), Partly),
				Partly > 40.0f && Partly < 100.0f))
		{
			return false;
		}
		TestEqual(TEXT("a step that leaves it below its maximum raises nothing"), Recharged.Count, 0);

		UCataclysmRegeneration::ApplyStep(Shielded.Actor, 1.0f, 100.0f);
		TestEqual(TEXT("set-up: a second filled it"),
			Shielded.Get(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute()), 100.0f, 0.01f);
		TestEqual(TEXT("the step that fills it raises recharged once"), Recharged.Count, 1);
		UCataclysmRegeneration::ApplyStep(Shielded.Actor, 1.0f, 100.0f);
		TestEqual(TEXT("a step at full raises nothing more"), Recharged.Count, 1);

		const FFighter Ceilinged(World, 0.0f);
		Ceilinged.Set(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 100.0f);
		Ceilinged.Set(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 10.0f);
		Ceilinged.Set(UCataclysmVitalAttributeSet::GetEnergyShieldRegenAttribute(), 100.0f);
		Ceilinged.Line(UCataclysmRegeneration::EnergyShieldRechargeCeilingReductionStat, 50.0f);
		const FEventCount CeilingRecharged(Ceilinged.AbilitySystem, TEXT("energy_shield_recharged"));
		UCataclysmRegeneration::ApplyStep(Ceilinged.Actor, 2.0f, 100.0f);
		TestEqual(TEXT("set-up: the ceiling held the shield at half"),
			Ceilinged.Get(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute()), 50.0f, 0.01f);
		TestEqual(TEXT("so it never fully recharged and raised nothing"), CeilingRecharged.Count, 0);
	}
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
