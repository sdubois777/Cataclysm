// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmRetaliation.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"
#include "HAL/IConsoleManager.h"

/**
 * Issue #1833 group D, part 1: an action row that applies a random damage over
 * time to the other character of its event, the event retaliation raises, and
 * the cooldown an action row waits out after it fires. Ruled 2026-09-30 under
 * the owner's delegation.
 *
 * EACH ACTION IS BUILT BY HAND, as the item would build it from a row, so these
 * tests read the mechanism and not the table. The table's two rows have tests
 * of their own beside the other enchantment rows.
 */
namespace CataclysmTriggerTest
{
	/** A bare actor holding every attribute set, with a thousand health. */
	struct FFighter
	{
		explicit FFighter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template and
			// a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor));
			Vitals = NewVitals;
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Vitals->SetMaxHealth(1000.0f);
			Vitals->SetHealth(1000.0f);
		}

		~FFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** Whether this fighter carries the tag an ailment grants. */
		bool Carries(const FCataclysmAilmentKind& Kind) const
		{
			const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(
				FName(Kind.TagName), /*ErrorIfNotFound=*/false);
			return Tag.IsValid() && AbilitySystem->HasMatchingGameplayTag(Tag);
		}

		/** How many of the random pool's five ailments this fighter carries. */
		int32 DamageOverTimeCarried() const
		{
			int32 Count = 0;
			for (const FCataclysmAilmentKind* Kind :
				 UCataclysmAilments::RandomDamageOverTimePool())
			{
				Count += Carries(*Kind) ? 1 : 0;
			}
			return Count;
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmVitalAttributeSet> Vitals = nullptr;
	};

	/** Pins one integer console variable for the life of this object. */
	struct FPinnedInt
	{
		FPinnedInt(const TCHAR* Name, int32 Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Set(Value);
		}
		~FPinnedInt()
		{
			Set(-1);
		}
		void Set(int32 Value) const
		{
			if (Variable)
			{
				Variable->Set(Value, ECVF_SetByCode);
			}
		}
		IConsoleVariable* Variable = nullptr;
	};

	/** Pins one float console variable for the life of this object. */
	struct FPinnedFloat
	{
		FPinnedFloat(const TCHAR* Name, float Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Set(Value);
		}
		~FPinnedFloat()
		{
			Set(-1.0f);
		}
		void Set(float Value) const
		{
			if (Variable)
			{
				Variable->Set(Value, ECVF_SetByCode);
			}
		}
		IConsoleVariable* Variable = nullptr;
	};

	/** A random damage over time on an event, always, with no cooldown. */
	FCataclysmPoolAction RandomDotOn(const TCHAR* Event)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(Event);
		Action.Pool = FName(UCataclysmAbilitySystemComponent::ApplyRandomDotAction);
		Action.Percent = 100.0f;
		Action.bRandomDamageOverTime = true;
		Action.TriggerKey = FName(FString::Printf(TEXT("A_row:%s:%s"),
			UCataclysmAbilitySystemComponent::ApplyRandomDotAction, Event));
		return Action;
	}

	/** "Blocking restores 10% of your maximum HP", with a trigger cooldown. */
	FCataclysmPoolAction HealOnBlock(float CooldownSeconds)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("block"));
		Action.Pool = FName(TEXT("health"));
		Action.Percent = 10.0f;
		Action.TriggerCooldownSeconds = CooldownSeconds;
		Action.TriggerKey = FName(TEXT("A_row:health:block"));
		return Action;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_TEST(FCataclysmRandomDotPicksAmongTheFiveTest,
	"Cataclysm.Triggers.ARandomDotRowAppliesEachOfTheFiveByItsPickAndNothingElse")
{
	using namespace CataclysmTriggerTest;

	// THE POOL IS THE RULING'S FIVE, in the order the pin counts them, and Void
	// Splinter is not among them.
	const TArray<const FCataclysmAilmentKind*> Pool =
		UCataclysmAilments::RandomDamageOverTimePool();
	TArray<FString> Names;
	for (const FCataclysmAilmentKind* Kind : Pool)
	{
		Names.Add(Kind->Ailment);
	}
	TestEqual(TEXT("the pool is Bleed, Poison, Disease, Necrosis and Burn"),
		FString::Join(Names, TEXT(", ")),
		FString(TEXT("Bleed, Poison, Disease, Necrosis, Burn")));
	if (Pool.Num() != 5)
	{
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		const FFighter Striker(World);
		Striker.AbilitySystem->SetPoolActions({RandomDotOn(TEXT("critical_strike"))});

		// ONE TARGET FOR EACH PICK, so each test of a pick starts clean.
		for (int32 Pick = 0; Pick < Pool.Num(); ++Pick)
		{
			const FFighter Target(World);
			const FPinnedInt Pinned(TEXT("Cataclysm.RandomDotPick"), Pick);
			// A TENTH OF A THOUSAND reached health, the least the rule allows.
			Striker.AbilitySystem->ActOnEvent(FName(TEXT("critical_strike")), nullptr,
				/*EventAmount=*/100.0f, /*bLanded=*/true, Target.Actor);
			TestTrue(FString::Printf(TEXT("pick %d applies %s"), Pick, Pool[Pick]->Ailment),
				Target.Carries(*Pool[Pick]));
			TestEqual(FString::Printf(TEXT("pick %d applies only that one"), Pick),
				Target.DamageOverTimeCarried(), 1);
		}
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmRandomDotNeedsATenthTest,
	"Cataclysm.Triggers.ARandomDotNeedsALandedBlowThatTookATenthAndALivingTarget")
{
	using namespace CataclysmTriggerTest;

	// THE OWNER'S RULE OF 2026-09-02 (#917): an ailment that does not come from
	// the skill's own row lands only from a blow that took a tenth of the
	// target's maximum health, and never on a corpse. And the action's own
	// rules: an event that landed, naming who was struck.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		const FPinnedInt Pinned(TEXT("Cataclysm.RandomDotPick"), 0);
		const FFighter Striker(World);
		Striker.AbilitySystem->SetPoolActions({RandomDotOn(TEXT("critical_strike"))});
		const FName Crit(TEXT("critical_strike"));

		const FFighter Target(World);
		Striker.AbilitySystem->ActOnEvent(Crit, nullptr, 99.0f, true, Target.Actor);
		TestEqual(TEXT("99 of a thousand applies nothing"), Target.DamageOverTimeCarried(), 0);
		Striker.AbilitySystem->ActOnEvent(Crit, nullptr, 500.0f, /*bLanded=*/false, Target.Actor);
		TestEqual(TEXT("an event that did not land applies nothing"),
			Target.DamageOverTimeCarried(), 0);
		Striker.AbilitySystem->ActOnEvent(Crit, nullptr, 500.0f, true, nullptr);
		TestEqual(TEXT("an event naming nobody applies nothing to anyone"),
			Target.DamageOverTimeCarried(), 0);
		Striker.AbilitySystem->ActOnEvent(Crit, nullptr, 100.0f, true, Target.Actor);
		TestEqual(TEXT("100 of a thousand applies one"), Target.DamageOverTimeCarried(), 1);

		const FFighter Dead(World);
		Dead.Vitals->SetHealth(0.0f);
		Striker.AbilitySystem->ActOnEvent(Crit, nullptr, 1000.0f, true, Dead.Actor);
		TestEqual(TEXT("a target the blow killed carries nothing"),
			Dead.DamageOverTimeCarried(), 0);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmTriggerCooldownTest,
	"Cataclysm.Triggers.ARowWithATriggerCooldownFiresOnceInItsWindowAndAgainAfter")
{
	using namespace CataclysmTriggerTest;

	// "Blocking an attack restores 3%-6% of your maximum HP" now waits a quarter
	// of a second after it fires: ruled 2026-09-30, building the judgement of
	// 2026-09-11. A block that did not land does not start the wait.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		const FFighter Wearer(World);
		const FCataclysmPoolAction Heal = HealOnBlock(0.25f);
		Wearer.AbilitySystem->SetPoolActions({Heal});
		Wearer.Vitals->SetHealth(500.0f);
		const FName Block(TEXT("block"));

		Wearer.AbilitySystem->ActOnEvent(Block, nullptr, 0.0f, /*bLanded=*/false);
		TestFalse(TEXT("a block that did not land starts no wait"),
			Wearer.AbilitySystem->TriggerCoolingDown(Heal.TriggerKey));

		Wearer.AbilitySystem->ActOnEvent(Block);
		Wearer.AbilitySystem->ActOnEvent(Block);
		TestEqual(TEXT("two blocks in one moment heal once: 500 and 100"),
			Wearer.Vitals->GetHealth(), 600.0f, 0.01f);
		TestTrue(TEXT("and the row is waiting"),
			Wearer.AbilitySystem->TriggerCoolingDown(Heal.TriggerKey));

		World->TimeSeconds += 0.2f;
		Wearer.AbilitySystem->ActOnEvent(Block);
		TestEqual(TEXT("a fifth of a second later it is still waiting"),
			Wearer.Vitals->GetHealth(), 600.0f, 0.01f);

		World->TimeSeconds += 0.05f;
		Wearer.AbilitySystem->ActOnEvent(Block);
		TestEqual(TEXT("a quarter of a second after it fired, it fires again"),
			Wearer.Vitals->GetHealth(), 700.0f, 0.01f);

		// AN EXPLICIT NOUGHT IS NO WAIT, for a sentence that says every hit.
		const FFighter Unwaiting(World);
		Unwaiting.AbilitySystem->SetPoolActions({HealOnBlock(0.0f)});
		Unwaiting.Vitals->SetHealth(500.0f);
		Unwaiting.AbilitySystem->ActOnEvent(Block);
		Unwaiting.AbilitySystem->ActOnEvent(Block);
		TestEqual(TEXT("with no cooldown, two blocks in one moment heal twice"),
			Unwaiting.Vitals->GetHealth(), 700.0f, 0.01f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmTriggerCooldownAfterTheRollTest,
	"Cataclysm.Triggers.AChanceThatDoesNotComeUpStartsNoTriggerCooldown")
{
	using namespace CataclysmTriggerTest;

	// THE COOLDOWN STARTS WHEN THE ROW FIRES, NOT WHEN IT IS TRIED: a labelled
	// judgement of 2026-09-30, following the genre. "Blocking an attack has a
	// 20%-40% chance to reset your heavy attack cooldown" that rolls and misses
	// may roll again on the next block.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		const FFighter Wearer(World);
		FCataclysmPoolAction Reset;
		Reset.Event = FName(TEXT("block"));
		Reset.Pool = FName(UCataclysmAbilitySystemComponent::CooldownResetHeavyAction);
		Reset.Percent = 30.0f;
		Reset.CooldownReset = ECataclysmCooldownReset::Heavy;
		Reset.ResetKey = FName(TEXT("A_row:cooldown_reset_heavy"));
		Reset.TriggerCooldownSeconds = 0.25f;
		Reset.TriggerKey = FName(TEXT("A_row:cooldown_reset_heavy:block"));
		Wearer.AbilitySystem->SetPoolActions({Reset});
		const FName Block(TEXT("block"));

		FPinnedFloat Roll(TEXT("Cataclysm.CooldownResetRoll"), 41.0f);
		Wearer.AbilitySystem->ActOnEvent(Block);
		TestFalse(TEXT("a roll of 41 against 30 misses and starts no wait"),
			Wearer.AbilitySystem->TriggerCoolingDown(Reset.TriggerKey));

		Roll.Set(19.0f);
		Wearer.AbilitySystem->ActOnEvent(Block);
		TestTrue(TEXT("a roll of 19 in the same moment comes up and starts it"),
			Wearer.AbilitySystem->TriggerCoolingDown(Reset.TriggerKey));
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmRetaliationDealtTest,
	"Cataclysm.Triggers.RetaliationRaisesItsEventWithTheAttackerAndWhatReachedItsHealth")
{
	using namespace CataclysmTriggerTest;

	// "Your retaliation damage also applies a random DoT to attackers". Issue
	// #1833 group D: `UCataclysmRetaliation::Pay` raises `retaliation_dealt` on
	// the retaliating character for each target it paid, naming that target and
	// what reached its health, which the random damage over time reads.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		const FPinnedInt Pinned(TEXT("Cataclysm.RandomDotPick"), 4);
		const FFighter Defender(World);
		const FFighter Attacker(World);
		Defender.AbilitySystem->SetPoolActions({RandomDotOn(TEXT("retaliation_dealt"))});
		// RETALIATION OF A HUNDRED PER CENT, so a blow worth 50 pays 50 back,
		// under a tenth of the attacker's thousand, and one worth 500 pays 500.
		Defender.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetRetaliationAttribute(), 100.0f);

		const float Small = UCataclysmRetaliation::Pay(
			Defender.AbilitySystem, Defender.Actor, Attacker.Actor, 50.0f);
		TestTrue(TEXT("a blow worth 50 paid something back"), Small > 0.0f);
		TestEqual(TEXT("and under a tenth it applied nothing"),
			Attacker.DamageOverTimeCarried(), 0);

		const float Large = UCataclysmRetaliation::Pay(
			Defender.AbilitySystem, Defender.Actor, Attacker.Actor, 500.0f);
		TestTrue(TEXT("a blow worth 500 paid at least a tenth back"), Large >= 100.0f);
		const TArray<const FCataclysmAilmentKind*> Pool =
			UCataclysmAilments::RandomDamageOverTimePool();
		if (Pool.Num() == 5)
		{
			TestTrue(TEXT("and the attacker carries the pinned Burn"),
				Attacker.Carries(*Pool[4]));
		}
	}
	World->DestroyWorld(false);
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
