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

	/**
	 * The two figures of a row by which a hit may make its wearer immune to that hit's damage type, each flat on a
	 * base of nought, as a gear refresh records a row's line. Ruled 2026-10-12. THE WHOLE STAT LINE IS WRITTEN, so
	 * the fighter holds these two lines and no other.
	 */
	void HoldTypeImmunity(const FFighter& Who, float ChancePercent, float Seconds)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		const auto Flat = [&Lines](const TCHAR* Stat, float Value)
		{
			FCataclysmStatModifier Row;
			Row.Bucket = ECataclysmStatBucket::Flat;
			Row.Source = ECataclysmModifierSource::Enchantment;
			Row.Value = Value;
			FCataclysmStatInputs& Inputs = Lines.FindOrAdd(FName(Stat));
			Inputs.Base = 0.0f;
			Inputs.Modifiers = {Row};
		};
		Flat(UCataclysmAbilitySystemComponent::DamageTypeImmunityOnHitChanceStat, ChancePercent);
		Flat(UCataclysmAbilitySystemComponent::DamageTypeImmunityOnHitSecondsStat, Seconds);
		Who.AbilitySystem->SetStatInputs(MoveTemp(Lines));
	}

	/**
	 * What one direct blow worth `Damage` took from `Defender`'s health: of `DamageType`, or of no type for
	 * `NAME_None`; a hit, or a damage over time tick when `bTick`. THE SHAPE A DUNGEON FLOOR RULE SENDS, a stated
	 * amount with the type on the delivery, here from a bare actor and not from a floor's hazard source.
	 */
	float Typed(const FFighter& Attacker, const FFighter& Defender, FName DamageType, bool bTick = false,
		float Damage = 100.0f, FCataclysmDamageResult* OutResolved = nullptr)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.DamageType = DamageType;
		Delivery.bIsDamageOverTime = bTick;
		const float Before = Defender.Get(UCataclysmVitalAttributeSet::GetHealthAttribute());
		UCataclysmSkillEffects::ApplyDirectDamage(Attacker.Actor, Defender.Actor, Damage, Delivery, OutResolved);
		return Before - Defender.Get(UCataclysmVitalAttributeSet::GetHealthAttribute());
	}
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
	// blocked blow keeps nothing on a negation roll of 0, AND ON A ROLL OF EXACTLY
	// 100, because a chance of 100 is compared as at most (issue #2201: the roll
	// can be 100 itself). A defender carrying 50 keeps nothing on a roll of 40 and
	// the ordinary half on a roll of 60.
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
		const FFighter Halfway(World, 0.0f);
		Defender.Set(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 100.0f);
		Defender.Line(UCataclysmDamageCalculation::BlockNegationChanceStat, 100.0f);
		Halfway.Set(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 100.0f);
		Halfway.Line(UCataclysmDamageCalculation::BlockNegationChanceStat, 50.0f);
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
				const FPinned TopOfTheRoll(TEXT("Cataclysm.BlockNegationRoll"), 100.0f);
				TestEqual(TEXT("a chance of 100 negates on a roll of exactly 100 too"),
					Blow(Attacker, Defender), 0.0f, 0.001f);
			}
			{
				const FPinned Under(TEXT("Cataclysm.BlockNegationRoll"), 40.0f);
				TestEqual(TEXT("a chance of 50 negates on a roll of 40"), Blow(Attacker, Halfway), 0.0f, 0.001f);
			}
			{
				const FPinned Over(TEXT("Cataclysm.BlockNegationRoll"), 60.0f);
				TestEqual(TEXT("and keeps the ordinary half on a roll of 60"),
					Blow(Attacker, Halfway), Full * 0.5f, 0.5f);
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
		// FLAT 50 ON A BASE OF 0, the shape the shipped row has: the regeneration
		// step reads the ceiling with `StatAppliedTo`, which ignores a recorded base
		// on purpose and applies only the modifiers to the figure it is handed.
		Ceilinged.Line(UCataclysmRegeneration::EnergyShieldRechargeCeilingReductionStat, 0.0f, 50.0f);
		const FEventCount CeilingRecharged(Ceilinged.AbilitySystem, TEXT("energy_shield_recharged"));
		UCataclysmRegeneration::ApplyStep(Ceilinged.Actor, 2.0f, 100.0f);
		TestEqual(TEXT("set-up: the ceiling held the shield at half"),
			Ceilinged.Get(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute()), 50.0f, 0.01f);
		TestEqual(TEXT("so it never fully recharged and raised nothing"), CeilingRecharged.Count, 0);
	}
	return true;
}

// A HIT MAY MAKE ITS WEARER IMMUNE TO THAT HIT'S DAMAGE TYPE FOR A TIME. Ruled 2026-10-12 under the owner's
// delegation, for "Shard of Anarchy (6-Piece Bonus): When you take damage, you have a 10% chance to become immune to
// the damage type that hit you for 5 seconds". The five tests below. No row is authored: each wearer holds the row's
// two stats by hand, `damage_type_immunity_on_hit_chance` and `damage_type_immunity_on_hit_seconds`.
//
// EVERY BLOW IS REAL AND TYPED: `ApplyDirectDamage` with the type on the delivery, through the damage calculation
// and the attribute set, which is the shape a dungeon floor rule sends. The critical strike roll and the block roll
// are pinned, and the roll of this mechanism by `Cataclysm.DamageTypeImmunityRoll` wherever a chance is under 100.
// Every figure is compared with what the same blow took from a fighter holding no row, read first.
//
// STANDING: nowhere. Every fighter is a bare actor with no root component, so none has a body to overlap another,
// and a direct blow reads no distance. THE CLOCK is moved by adding to the world's time, as the test of a block
// opening the no-damage window above does; nothing here waits on a tick.

CATACLYSM_TEST(FCataclysmTypeImmunityOpensTest,
	"Cataclysm.BlockShare.AHitMayOpenAWindowAgainstItsOwnDamageTypeAndIsItselfTakenInFull")
{
	using namespace CataclysmBlockShareTest;
	// A WEARER AT A CHANCE OF 50 FOR 5 SECONDS, THE ROLL PINNED TO SUCCEED. Its first War hit is taken in full and
	// opens the window against War, for 5 seconds, and against no other type. A second War hit inside the window
	// takes nothing. With the roll then pinned to fail, a Demonic hit and a hit of no type inside the window are
	// taken in full. Four seconds on a War hit still takes nothing; five and a half seconds on the window has ended
	// and a War hit is taken in full again.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);
	const FName War(TEXT("War"));
	const FName Demonic(TEXT("Demonic"));
	{
		const FFighter Attacker(World, 0.0f);
		const FFighter Plain(World, 0.0f);
		const FFighter Wearer(World, 0.0f);
		HoldTypeImmunity(Wearer, 50.0f, 5.0f);

		const FPinned Succeeds(TEXT("Cataclysm.DamageTypeImmunityRoll"), 0.0f);
		if (!TestTrue(TEXT("set-up: the roll's console variable exists"), Succeeds.Variable != nullptr))
		{
			return false;
		}

		// THE CONTROL: a fighter holding no row, struck by each of the three blows.
		const float Full = Typed(Attacker, Plain, War);
		const float FullDemonic = Typed(Attacker, Plain, Demonic);
		const float FullUntyped = Typed(Attacker, Plain, NAME_None);
		if (!TestTrue(*FString::Printf(TEXT("set-up: with no row a War hit took %.1f, a Demonic one %.1f and one of "
				"no type %.1f"), Full, FullDemonic, FullUntyped),
				Full > 0.0f && FullDemonic > 0.0f && FullUntyped > 0.0f))
		{
			return false;
		}
		TestFalse(TEXT("control: a fighter with no row is not immune to War after a War hit, on a roll of nought"),
			Plain.AbilitySystem->IsImmuneToDamageType(War));
		TestEqual(TEXT("control: and its second War hit takes what its first took"),
			Typed(Attacker, Plain, War), Full, 0.001f);

		TestFalse(TEXT("before any hit the wearer is not immune to War"),
			Wearer.AbilitySystem->IsImmuneToDamageType(War));
		TestEqual(TEXT("the hit that rolls is itself taken in full"), Typed(Attacker, Wearer, War), Full, 0.001f);
		TestTrue(TEXT("and it opened the window against War"), Wearer.AbilitySystem->IsImmuneToDamageType(War));
		TestEqual(TEXT("for the 5 seconds the wearer holds"),
			Wearer.AbilitySystem->DamageTypeImmunitySecondsLeft(War), 5.0f, 0.001f);
		TestFalse(TEXT("and no window against another type"), Wearer.AbilitySystem->IsImmuneToDamageType(Demonic));
		TestEqual(TEXT("a second War hit inside the window takes nothing"),
			Typed(Attacker, Wearer, War), 0.0f, 0.001f);

		{
			// THE ROLL NOW FAILS, so the two hits below are read for what they take and open nothing themselves.
			const FPinned Fails(TEXT("Cataclysm.DamageTypeImmunityRoll"), 99.0f);
			TestEqual(TEXT("a Demonic hit inside the War window is taken in full"),
				Typed(Attacker, Wearer, Demonic), FullDemonic, 0.001f);
			TestEqual(TEXT("and so is a hit of no type"), Typed(Attacker, Wearer, NAME_None), FullUntyped, 0.001f);

			World->TimeSeconds += 4.0f;
			TestEqual(TEXT("four seconds on a War hit still takes nothing"),
				Typed(Attacker, Wearer, War), 0.0f, 0.001f);
			World->TimeSeconds += 1.5f;
			TestFalse(TEXT("five and a half seconds on the window has ended"),
				Wearer.AbilitySystem->IsImmuneToDamageType(War));
			TestEqual(TEXT("and a War hit is taken in full again"), Typed(Attacker, Wearer, War), Full, 0.001f);
		}
	}
	return true;
}

CATACLYSM_TEST(FCataclysmTypeImmunityRollTest,
	"Cataclysm.BlockShare.ARollThatFailsOpensNoWindowAndAStatedHundredNeverFails")
{
	using namespace CataclysmBlockShareTest;
	// THE ROLL AGAINST THE CHANCE. At a chance of 50 a roll of exactly 50 fails: the hit is taken in full, nothing
	// opens and the next hit is taken in full too; a roll of 49 opens the window. AT A STATED 100 A ROLL OF EXACTLY
	// 100 OPENS IT (issue #2201: the roll can be 100 itself, and a chance of 100 is not rolled), where a chance of
	// 50 on that roll opens nothing. On a roll of nought a fighter with no row, one holding a chance and no seconds,
	// and one holding seconds and no chance each open nothing.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);
	const FName War(TEXT("War"));
	const FName Demonic(TEXT("Demonic"));
	{
		const FFighter Attacker(World, 0.0f);
		const FFighter Plain(World, 0.0f);
		const FFighter Half(World, 0.0f);
		const FFighter Certain(World, 0.0f);
		const FFighter NoSeconds(World, 0.0f);
		const FFighter NoChance(World, 0.0f);
		HoldTypeImmunity(Half, 50.0f, 5.0f);
		HoldTypeImmunity(Certain, 100.0f, 5.0f);
		HoldTypeImmunity(NoSeconds, 100.0f, 0.0f);
		HoldTypeImmunity(NoChance, 0.0f, 5.0f);

		const float Full = Typed(Attacker, Plain, War);
		if (!TestTrue(*FString::Printf(TEXT("set-up: with no row a War hit took %.1f"), Full), Full > 0.0f))
		{
			return false;
		}
		{
			const FPinned AtTheChance(TEXT("Cataclysm.DamageTypeImmunityRoll"), 50.0f);
			if (!TestTrue(TEXT("set-up: the roll's console variable exists"), AtTheChance.Variable != nullptr))
			{
				return false;
			}
			TestEqual(TEXT("a hit whose roll fails is taken in full"), Typed(Attacker, Half, War), Full, 0.001f);
			TestFalse(TEXT("a roll equal to the chance fails and opens nothing"),
				Half.AbilitySystem->IsImmuneToDamageType(War));
			TestEqual(TEXT("so the next War hit is taken in full too"), Typed(Attacker, Half, War), Full, 0.001f);
		}
		{
			const FPinned JustUnder(TEXT("Cataclysm.DamageTypeImmunityRoll"), 49.0f);
			Typed(Attacker, Half, War);
			TestTrue(TEXT("a roll below the chance opens the window"),
				Half.AbilitySystem->IsImmuneToDamageType(War));
		}
		{
			const FPinned TopOfTheRoll(TEXT("Cataclysm.DamageTypeImmunityRoll"), 100.0f);
			Typed(Attacker, Certain, War);
			TestTrue(TEXT("a stated 100 opens the window on a roll of exactly 100"),
				Certain.AbilitySystem->IsImmuneToDamageType(War));
			Typed(Attacker, Half, Demonic);
			TestFalse(TEXT("control: on that roll a chance of 50 opens nothing"),
				Half.AbilitySystem->IsImmuneToDamageType(Demonic));
		}
		{
			const FPinned Succeeds(TEXT("Cataclysm.DamageTypeImmunityRoll"), 0.0f);
			Typed(Attacker, Plain, War);
			TestFalse(TEXT("control: a fighter with no row opens nothing on a roll of nought"),
				Plain.AbilitySystem->IsImmuneToDamageType(War));
			Typed(Attacker, NoSeconds, War);
			TestFalse(TEXT("a chance with no seconds opens nothing"),
				NoSeconds.AbilitySystem->IsImmuneToDamageType(War));
			Typed(Attacker, NoChance, War);
			TestFalse(TEXT("and seconds with no chance open nothing"),
				NoChance.AbilitySystem->IsImmuneToDamageType(War));
		}
	}
	return true;
}

CATACLYSM_TEST(FCataclysmTypeImmunityTickTest,
	"Cataclysm.BlockShare.ATickNeverRollsAndATickOfTheTypeInsideItsWindowTakesNothingAsInTheNoDamageWindow")
{
	using namespace CataclysmBlockShareTest;
	// A WEARER AT A CHANCE OF 100, so nothing is rolled. A War damage over time tick on it is taken in full and
	// opens nothing: a tick does not roll. A War HIT then opens the window, and a War tick inside it takes nothing,
	// which is what a tick takes from a fighter inside the no-damage window. A Demonic tick inside the War window
	// is taken in full and opens nothing. After the window a War tick is taken again.
	//
	// THE TICK IS A DIRECT ONE marked as damage over time, as a dungeon floor rule sends one; no lasting effect
	// is applied and no timer runs.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);
	const FName War(TEXT("War"));
	const FName Demonic(TEXT("Demonic"));
	{
		const FFighter Attacker(World, 0.0f);
		const FFighter Plain(World, 0.0f);
		const FFighter Wearer(World, 0.0f);
		const FFighter Guarded(World, 0.0f);
		HoldTypeImmunity(Wearer, 100.0f, 5.0f);

		const float TickFull = Typed(Attacker, Plain, War, /*bTick=*/true);
		const float TickFullDemonic = Typed(Attacker, Plain, Demonic, /*bTick=*/true);
		if (!TestTrue(*FString::Printf(TEXT("set-up: with no row a War tick took %.1f and a Demonic one %.1f"),
				TickFull, TickFullDemonic), TickFull > 0.0f && TickFullDemonic > 0.0f))
		{
			return false;
		}

		TestEqual(TEXT("a War tick on the wearer is taken in full"),
			Typed(Attacker, Wearer, War, /*bTick=*/true), TickFull, 0.001f);
		TestFalse(TEXT("and a tick does not roll: no window, at a chance of 100"),
			Wearer.AbilitySystem->IsImmuneToDamageType(War));

		Typed(Attacker, Wearer, War);
		if (!TestTrue(TEXT("set-up: a War hit opened the window"), Wearer.AbilitySystem->IsImmuneToDamageType(War)))
		{
			return false;
		}
		TestEqual(TEXT("a War tick inside the window takes nothing"),
			Typed(Attacker, Wearer, War, /*bTick=*/true), 0.0f, 0.001f);
		Guarded.AbilitySystem->GrantDamageImmunity(5.0f);
		TestEqual(TEXT("which is what a tick takes inside the no-damage window"),
			Typed(Attacker, Guarded, War, /*bTick=*/true), 0.0f, 0.001f);
		TestEqual(TEXT("a Demonic tick inside the War window is taken in full"),
			Typed(Attacker, Wearer, Demonic, /*bTick=*/true), TickFullDemonic, 0.001f);
		TestFalse(TEXT("and opens nothing"), Wearer.AbilitySystem->IsImmuneToDamageType(Demonic));

		World->TimeSeconds += 5.5f;
		TestEqual(TEXT("after the window a War tick is taken again"),
			Typed(Attacker, Wearer, War, /*bTick=*/true), TickFull, 0.001f);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmTypeImmunityTwoTypesTest,
	"Cataclysm.BlockShare.WindowsForTwoTypesRunSideBySideAndALaterGrantNeverShortensOne")
{
	using namespace CataclysmBlockShareTest;
	// A WEARER AT A CHANCE OF 100 FOR 5 SECONDS. A War hit, and two seconds later a Demonic hit: both windows are
	// open, War with 3 seconds left and Demonic with 5, and a hit of either type takes nothing and extends nothing.
	// Five and a half seconds after the War hit its window has ended and the Demonic one has 1.5 seconds left: a
	// War hit is taken in full again and a Demonic hit still takes nothing.
	//
	// THEN THE GRANT BY HAND, because a hit inside its own window deals nothing and so cannot roll again: a grant
	// of 1 second inside one of 5 leaves 5, a grant of 8 makes it 8, and a grant for no type opens nothing. Last,
	// `ClearWhatDeathEnds` ends every window.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);
	const FName War(TEXT("War"));
	const FName Demonic(TEXT("Demonic"));
	const FName OfVoid(TEXT("Void"));
	{
		const FFighter Attacker(World, 0.0f);
		const FFighter Plain(World, 0.0f);
		const FFighter Wearer(World, 0.0f);
		HoldTypeImmunity(Wearer, 100.0f, 5.0f);
		UCataclysmAbilitySystemComponent* Its = Wearer.AbilitySystem.Get();

		const float Full = Typed(Attacker, Plain, War);
		if (!TestTrue(*FString::Printf(TEXT("set-up: with no row a War hit took %.1f"), Full), Full > 0.0f))
		{
			return false;
		}

		Typed(Attacker, Wearer, War);
		World->TimeSeconds += 2.0f;
		Typed(Attacker, Wearer, Demonic);
		if (!TestTrue(TEXT("set-up: both windows are open"),
				Its->IsImmuneToDamageType(War) && Its->IsImmuneToDamageType(Demonic)))
		{
			return false;
		}
		TestEqual(TEXT("the War window has 3 seconds left"), Its->DamageTypeImmunitySecondsLeft(War), 3.0f, 0.001f);
		TestEqual(TEXT("and the Demonic window its whole 5"),
			Its->DamageTypeImmunitySecondsLeft(Demonic), 5.0f, 0.001f);
		TestEqual(TEXT("a War hit takes nothing"), Typed(Attacker, Wearer, War), 0.0f, 0.001f);
		TestEqual(TEXT("and a Demonic hit takes nothing"), Typed(Attacker, Wearer, Demonic), 0.0f, 0.001f);
		TestEqual(TEXT("a hit its window refused did not roll: War still has 3 seconds"),
			Its->DamageTypeImmunitySecondsLeft(War), 3.0f, 0.001f);

		World->TimeSeconds += 3.5f;
		TestFalse(TEXT("five and a half seconds after the War hit its window has ended"),
			Its->IsImmuneToDamageType(War));
		TestEqual(TEXT("and the Demonic window has 1.5 seconds left"),
			Its->DamageTypeImmunitySecondsLeft(Demonic), 1.5f, 0.001f);
		TestEqual(TEXT("a War hit is taken in full again"), Typed(Attacker, Wearer, War), Full, 0.001f);
		TestEqual(TEXT("while a Demonic hit still takes nothing"), Typed(Attacker, Wearer, Demonic), 0.0f, 0.001f);

		// THE GRANT BY HAND.
		Its->GrantDamageTypeImmunity(OfVoid, 5.0f);
		Its->GrantDamageTypeImmunity(OfVoid, 1.0f);
		TestEqual(TEXT("a shorter grant does not shorten a window"),
			Its->DamageTypeImmunitySecondsLeft(OfVoid), 5.0f, 0.001f);
		Its->GrantDamageTypeImmunity(OfVoid, 8.0f);
		TestEqual(TEXT("and a longer one extends it to the later end"),
			Its->DamageTypeImmunitySecondsLeft(OfVoid), 8.0f, 0.001f);
		Its->GrantDamageTypeImmunity(NAME_None, 5.0f);
		TestFalse(TEXT("a grant for no type opens nothing"), Its->IsImmuneToDamageType(NAME_None));

		// AND WHAT A DEATH CLEARS. The War hit above was taken at a chance of 100, so War is open again.
		if (!TestTrue(TEXT("set-up: three windows are open before the clearing"),
				Its->IsImmuneToDamageType(War) && Its->IsImmuneToDamageType(Demonic)
					&& Its->IsImmuneToDamageType(OfVoid)))
		{
			return false;
		}
		Its->ClearWhatDeathEnds();
		TestFalse(TEXT("ClearWhatDeathEnds ends every window"),
			Its->IsImmuneToDamageType(War) || Its->IsImmuneToDamageType(Demonic)
				|| Its->IsImmuneToDamageType(OfVoid));
	}
	return true;
}

CATACLYSM_TEST(FCataclysmTypeImmunityWhichHitsRollTest,
	"Cataclysm.BlockShare.ABlockedHitAndAHitTheShieldTakesWholeRollAndAHitThatDealsNothingOrKillsDoesNot")
{
	using namespace CataclysmBlockShareTest;
	// WHICH HITS ROLL, each on a wearer of its own at a chance of 100 for 5 seconds. A blocked hit that kept part
	// of itself rolls. A hit the energy shield took whole rolls. A hit inside the no-damage window takes nothing and
	// does not roll, and once that window has ended a hit is taken in full and rolls. A hit that empties its
	// taker's health opens nothing.
	//
	// NOT HERE: an evaded hit. Evasion is rolled inside the damage calculation with no console variable to pin it.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);
	const FName War(TEXT("War"));
	{
		const FFighter Attacker(World, 0.0f);
		const FFighter Plain(World, 0.0f);
		const FFighter Blocker(World, 0.0f);
		const FFighter Shielded(World, 0.0f);
		const FFighter Guarded(World, 0.0f);
		const FFighter Dying(World, 0.0f);
		for (const FFighter* Wearer : {&Blocker, &Shielded, &Guarded, &Dying})
		{
			HoldTypeImmunity(*Wearer, 100.0f, 5.0f);
		}

		const float Full = Typed(Attacker, Plain, War);
		if (!TestTrue(*FString::Printf(TEXT("set-up: with no row a War hit took %.1f"), Full), Full > 0.0f))
		{
			return false;
		}

		// A BLOCKED HIT.
		Blocker.Set(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 100.0f);
		{
			const FPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
			FCataclysmDamageResult Blocked;
			const float Kept = Typed(Attacker, Blocker, War, /*bTick=*/false, 100.0f, &Blocked);
			if (!TestTrue(*FString::Printf(TEXT("set-up: the hit was blocked and kept part of itself: %.1f of %.1f"),
					Kept, Full), Blocked.bBlocked && Kept > 0.0f && Kept < Full))
			{
				return false;
			}
			TestTrue(TEXT("a blocked hit that dealt damage rolls"), Blocker.AbilitySystem->IsImmuneToDamageType(War));
		}

		// A HIT THE ENERGY SHIELD TOOK WHOLE.
		Shielded.Set(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 500.0f);
		Shielded.Set(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 500.0f);
		const float ShieldBefore = Shielded.Get(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute());
		const float FromHealth = Typed(Attacker, Shielded, War);
		const float FromShield =
			ShieldBefore - Shielded.Get(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute());
		if (!TestTrue(*FString::Printf(TEXT("set-up: a shield of %.1f took the hit whole: %.1f from the shield and "
				"%.1f from health"), ShieldBefore, FromShield, FromHealth),
				FromShield > 0.0f && FMath::IsNearlyZero(FromHealth, 0.001f)))
		{
			return false;
		}
		TestTrue(TEXT("a hit the energy shield took whole rolls"), Shielded.AbilitySystem->IsImmuneToDamageType(War));

		// A HIT INSIDE THE NO-DAMAGE WINDOW.
		Guarded.AbilitySystem->GrantDamageImmunity(3.0f);
		TestEqual(TEXT("a War hit inside the no-damage window takes nothing"),
			Typed(Attacker, Guarded, War), 0.0f, 0.001f);
		TestFalse(TEXT("and does not roll"), Guarded.AbilitySystem->IsImmuneToDamageType(War));
		World->TimeSeconds += 3.5f;
		TestEqual(TEXT("once that window has ended a War hit is taken in full"),
			Typed(Attacker, Guarded, War), Full, 0.001f);
		TestTrue(TEXT("and rolls"), Guarded.AbilitySystem->IsImmuneToDamageType(War));

		// A HIT THAT KILLS.
		Typed(Attacker, Dying, War, /*bTick=*/false, 5000.0f);
		if (!TestTrue(TEXT("set-up: the hit emptied its taker's health"),
				Dying.Get(UCataclysmVitalAttributeSet::GetHealthAttribute()) <= 0.0f))
		{
			return false;
		}
		TestFalse(TEXT("a hit that kills opens nothing"), Dying.AbilitySystem->IsImmuneToDamageType(War));
	}
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
