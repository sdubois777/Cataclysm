// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * Issue #1833 group E part 3, ruled 2026-10-02 under the owner's delegation: a
 * block names the attacker whose blow it blocked and what it removed; a reflect
 * pays a share of that back as retaliation pays; a nearby action may act on the
 * Nth of its events inside a window; and a nearby blow may be a share of the
 * wearer's armour.
 *
 * EVERY BLOW IS REAL: `ApplyHit` through the damage calculation and the
 * attribute set, with the block roll, the negation roll and the critical strike
 * roll pinned. Each action is built by hand, as the item would build it from a
 * row; the rows have tests of their own.
 */
namespace CataclysmBlockReflectTest
{
	/** A bare actor holding the four sets, with a hundred thousand health and no armour. */
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
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
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

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
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

	/** One blow of `Attacker`'s on `Defender` at 100% of its attack; what the defender made of it. */
	FCataclysmDamageResult Blow(const FFighter& Attacker, const FFighter& Defender)
	{
		FCataclysmDamageResult Result;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
			FGameplayTagContainer(), FCataclysmHitDelivery(), &Result);
		return Result;
	}

	/** An action on `block`, as the item would build it from a row. */
	FCataclysmPoolAction OnBlock(const TCHAR* ActionName, float Percent)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("block"));
		Action.Pool = FName(ActionName);
		Action.Percent = Percent;
		Action.TriggerKey = FName(*FString::Printf(TEXT("A_row:%s:block"), ActionName));
		return Action;
	}

	/** A creature of the monsters' side 3 m from `Near`, with no armour, evasion or block. */
	ACataclysmEnemyCharacter* CreatureBeside(UWorld* World, const AActor* Near)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			Near->GetActorLocation() + FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(100000.0f);
			Made->SetArmour(0.0f);
			UAbilitySystemComponent* Its = Made->GetAbilitySystemComponent();
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		}
		return Made;
	}

	float HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_TEST(FCataclysmReflectBlockedTest,
	"Cataclysm.BlockReflect.AReflectPaysItsShareOfWhatTheBlockRemovedToTheAttacker")
{
	using namespace CataclysmBlockReflectTest;
	// A REAL BLOCKED BLOW: the block removes half and records that half as what
	// it removed; a reflect of 60% pays 60% of it to the attacker, through the
	// attacker's own mitigation, which is none here. A negated block removed all
	// of the blow, and reflects 60% of all of it. A blow that is not blocked
	// reflects nothing.
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
		FCataclysmPoolAction Reflect = OnBlock(UCataclysmAbilitySystemComponent::ReflectBlockedAction, 60.0f);
		Reflect.bReflectBlocked = true;
		Defender.AbilitySystem->SetPoolActions({Reflect});

		float Before = Attacker.Health();
		FCataclysmDamageResult Result;
		{
			const FPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
			Result = Blow(Attacker, Defender);
		}
		if (!TestTrue(*FString::Printf(TEXT("set-up: the blow was blocked and removed %.2f"), Result.DamageBlocked),
				Result.bBlocked && Result.DamageBlocked > 0.0f))
		{
			return false;
		}
		TestEqual(TEXT("the block removed half the blow, what reached health again"),
			Result.DamageBlocked, Result.DealtToHealth, 0.01f);
		TestEqual(TEXT("the attacker took 60% of what the block removed"),
			Before - Attacker.Health(), Result.DamageBlocked * 0.6f, 0.01f);

		Before = Attacker.Health();
		{
			const FPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
			const FPinned Negates(TEXT("Cataclysm.BlockNegationRoll"), 0.0f);
			TMap<FName, FCataclysmStatInputs> Lines;
			Lines.FindOrAdd(FName(UCataclysmDamageCalculation::BlockNegationChanceStat)).Base = 100.0f;
			Defender.AbilitySystem->SetStatInputs(MoveTemp(Lines));
			Result = Blow(Attacker, Defender);
			Defender.AbilitySystem->SetStatInputs({});
		}
		TestEqual(TEXT("a negated block reached nothing"), Result.DealtToHealth, 0.0f, 0.001f);
		TestEqual(TEXT("and the attacker took 60% of the whole blow it removed"),
			Before - Attacker.Health(), Result.DamageBlocked * 0.6f, 0.01f);
		TestTrue(TEXT("which is more than half a blow's"), Result.DamageBlocked > 0.0f);

		World->TimeSeconds += 1.0f;
		Before = Attacker.Health();
		{
			const FPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);
			Result = Blow(Attacker, Defender);
		}
		TestFalse(TEXT("set-up: a blow that is not blocked"), Result.bBlocked);
		TestEqual(TEXT("reflects nothing"), Attacker.Health(), Before, 0.001f);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmReflectLikeRetaliationTest,
	"Cataclysm.BlockReflect.AReflectIsNotRetaliatedAgainstAndNoIncreaseScalesIt")
{
	using namespace CataclysmBlockReflectTest;
	// AS RETALIATION PAYS: the attacker retaliates at 100% and its retaliation
	// never answers the reflect, so the defender loses only what the blocked
	// blow left; and the defender's increases to its damage do not scale the
	// reflect.
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
		Attacker.Set(UCataclysmCombatAttributeSet::GetRetaliationAttribute(), 100.0f);
		Defender.Set(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 100.0f);
		FCataclysmPoolAction Reflect = OnBlock(UCataclysmAbilitySystemComponent::ReflectBlockedAction, 100.0f);
		Reflect.bReflectBlocked = true;
		Defender.AbilitySystem->SetPoolActions({Reflect});
		// AN INCREASE OF 100% TO THE DEFENDER'S DAMAGE, which retaliation's amount
		// would carry and a reflect must not.
		{
			TMap<FName, FCataclysmStatInputs> Lines;
			FCataclysmStatModifier Doubled;
			Doubled.Bucket = ECataclysmStatBucket::Increased;
			Doubled.Source = ECataclysmModifierSource::Enchantment;
			Doubled.Value = 100.0f;
			FCataclysmStatInputs& Attack = Lines.FindOrAdd(FName(TEXT("attack_damage")));
			Attack.Base = 0.0f;
			Attack.Modifiers = {Doubled};
			Defender.AbilitySystem->SetStatInputs(MoveTemp(Lines));
		}

		const float AttackerBefore = Attacker.Health();
		const float DefenderBefore = Defender.Health();
		const FCataclysmDamageResult Result = Blow(Attacker, Defender);
		if (!TestTrue(TEXT("set-up: the blow was blocked"), Result.bBlocked && Result.DamageBlocked > 0.0f))
		{
			return false;
		}
		TestEqual(TEXT("the attacker took exactly what the block removed, unscaled"),
			AttackerBefore - Attacker.Health(), Result.DamageBlocked, 0.01f);
		TestEqual(TEXT("and the defender lost only what the blocked blow left: nothing came back"),
			DefenderBefore - Defender.Health(), Result.DealtToHealth, 0.01f);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmBlockCountTest,
	"Cataclysm.BlockReflect.ThreeBlocksInsideThreeSecondsSmiteOnceAndTheCountStartsAgain")
{
	using namespace CataclysmBlockReflectTest;
	// "Every 3 blocks in quick succession", BY HAND: a smite on `block` counting 3
	// inside 3 seconds. Two blocks smite nothing and the third smites the creature
	// nearby; the count starts again, so the fourth and fifth smite nothing and
	// the sixth smites. Two blocks, four seconds, and a third smite nothing: the
	// first two left the window.
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
		const FFighter Defender(World, 100.0f);
		Defender.Set(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 100.0f);
		FCataclysmPoolAction Wave = OnBlock(UCataclysmAbilitySystemComponent::SmiteNearbyAction, 100.0f);
		Wave.Nearby = ECataclysmNearbyAction::Smite;
		Wave.EveryNth = 3;
		Wave.CountWindowSeconds = 3.0f;
		Defender.AbilitySystem->SetPoolActions({Wave});
		ACataclysmEnemyCharacter* Creature = CreatureBeside(World, Defender.Actor);
		if (!TestNotNull(TEXT("a creature three metres away"), Creature))
		{
			return false;
		}

		const auto Smitten = [&]()
		{
			const float Before = HealthOf(Creature);
			if (!Blow(Attacker, Defender).bBlocked)
			{
				AddError(TEXT("a blow was not blocked"));
			}
			return HealthOf(Creature) < Before;
		};
		TestFalse(TEXT("block 1 smites nothing"), Smitten());
		TestFalse(TEXT("block 2 smites nothing"), Smitten());
		TestTrue(TEXT("block 3 smites the creature"), Smitten());
		TestFalse(TEXT("block 4 smites nothing: the count started again"), Smitten());
		TestFalse(TEXT("block 5 smites nothing"), Smitten());
		TestTrue(TEXT("block 6 smites again"), Smitten());

		TestFalse(TEXT("a new block 1 smites nothing"), Smitten());
		TestFalse(TEXT("a new block 2 smites nothing"), Smitten());
		World->TimeSeconds += 4.0f;
		TestFalse(TEXT("four seconds on, a third block smites nothing: the first two left the window"),
			Smitten());
	}
	return true;
}

CATACLYSM_TEST(FCataclysmArmourNovaTest,
	"Cataclysm.BlockReflect.AnArmourNovaHitsEachEnemyNearbyForItsShareOfTheWearersArmour")
{
	using namespace CataclysmBlockReflectTest;
	// A WEARER WITH 1000 ARMOUR, a nova of 300% of it on `block`: a creature
	// nearby with no armour, evasion, block or resistance loses 3000.
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
		Defender.Set(UCataclysmCombatAttributeSet::GetArmorAttribute(), 1000.0f);
		FCataclysmPoolAction Nova = OnBlock(UCataclysmAbilitySystemComponent::SmiteNearbyByArmourAction, 300.0f);
		Nova.Nearby = ECataclysmNearbyAction::SmiteByArmour;
		Defender.AbilitySystem->SetPoolActions({Nova});
		ACataclysmEnemyCharacter* Creature = CreatureBeside(World, Defender.Actor);
		if (!TestNotNull(TEXT("a creature three metres away"), Creature))
		{
			return false;
		}
		const float Before = HealthOf(Creature);
		if (!TestTrue(TEXT("set-up: the blow was blocked"), Blow(Attacker, Defender).bBlocked))
		{
			return false;
		}
		TestEqual(TEXT("the creature lost 300% of the wearer's 1000 armour"),
			Before - HealthOf(Creature), 3000.0f, 0.5f);
	}
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
