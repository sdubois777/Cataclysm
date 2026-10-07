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
#include "Components/SceneComponent.h"
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

/**
 * Ruled 2026-10-07 under the owner's delegation, for "When you evade a ranged attack, throw an attack dealing
 * 20-70% of your attack damage at that enemy": `dodge` carries the attacker whose blow was evaded and one tag,
 * melee or ranged, and the action `strike_target` deals the other character of its event a direct hit that
 * cannot be evaded.
 *
 * EVERY BLOW IS REAL, as above. AN EVADE IS MADE CERTAIN BY AN EVASION OF 1000 AND IMPOSSIBLE BY AN EVASION OF 0:
 * a live blow's evasion roll is from 0 to 100 and has no console variable to pin, and the attribute is not
 * clamped. The critical strike roll is pinned. Each action is built by hand, as the item would build it from a
 * row.
 *
 * WHERE EACH FIGHTER STANDS is said in each test. A fighter is a bare actor with no body, and `StandAt` gives it
 * a place, so no two share one.
 */
namespace CataclysmStrikeTargetTest
{
	using CataclysmBlockReflectTest::FFighter;
	using CataclysmBlockReflectTest::FPinned;

	/** An evasion above every roll a blow can make, which is from 0 to 100. */
	constexpr float CertainEvasion = 1000.0f;

	/** Puts a fighter on the world's X axis, `MetresEast` metres from the origin. */
	void StandAt(const FFighter& Fighter, float MetresEast)
	{
		USceneComponent* Root = NewObject<USceneComponent>(Fighter.Actor.Get());
		Fighter.Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Fighter.Actor->SetActorLocation(FVector(MetresEast * 100.0f, 0.0f, 0.0f));
	}

	/** What a blow says it is. A spell here carries neither the melee tag nor the ranged one. */
	enum class EBlowKind : uint8
	{
		Melee,
		Ranged,
		Spell,
	};

	/** One blow of `Attacker`'s on `Defender` at 100% of its attack, of the kind asked for. */
	FCataclysmDamageResult BlowOfKind(const FFighter& Attacker, const FFighter& Defender, EBlowKind BlowKind)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bIsMelee = BlowKind == EBlowKind::Melee;
		Delivery.bIsRanged = BlowKind == EBlowKind::Ranged;
		Delivery.bIsSpell = BlowKind == EBlowKind::Spell;
		FCataclysmDamageResult Result;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
			FGameplayTagContainer(), Delivery, &Result);
		return Result;
	}

	/** A `strike_target` action on `Event`, as the item would build it; scoped to `Required` when that is a tag. */
	FCataclysmPoolAction StrikeOn(const TCHAR* Event, float Percent, const FGameplayTag& Required = FGameplayTag())
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(Event);
		Action.Pool = FName(UCataclysmAbilitySystemComponent::StrikeTargetAction);
		Action.Percent = Percent;
		Action.TriggerKey = FName(*FString::Printf(TEXT("A_row:strike_target:%s"), Event));
		Action.bStrikeTarget = true;
		if (Required.IsValid())
		{
			Action.RequiredTags.AddTag(Required);
		}
		return Action;
	}

	/** A stack granted on `Event`, for 5 seconds and up to 10; scoped to `Required` when that is a tag. */
	FCataclysmPoolAction StackOn(const TCHAR* Event, FName Key, const FGameplayTag& Required = FGameplayTag())
	{
		FCataclysmPoolAction Grant;
		Grant.Event = FName(Event);
		Grant.StackKey = Key;
		Grant.StackSeconds = 5.0f;
		Grant.StackCap = 10;
		Grant.TriggerKey = Key;
		if (Required.IsValid())
		{
			Grant.RequiredTags.AddTag(Required);
		}
		return Grant;
	}

	/** What a plain hit of `Percent` of `From`'s attack takes from `Control`, which must not evade or block it. */
	float PlainHitTakes(const FFighter& From, const FFighter& Control, float Percent)
	{
		const float Was = Control.Health();
		UCataclysmSkillEffects::ApplyHit(From.Actor, Control.Actor, Percent);
		return Was - Control.Health();
	}
}

CATACLYSM_TEST(FCataclysmStrikeTargetOnAnEvadedRangedBlowTest,
	"Cataclysm.StrikeTarget.AnEvadedRangedBlowStrikesItsAttackerForTheRowsShareAndAMeleeOneStrikesNobody")
{
	using namespace CataclysmStrikeTargetTest;
	// THE WEARER STANDS AT THE ORIGIN, THE ATTACKER 4 M EAST OF IT AND THE CONTROL 8 M EAST.
	//
	// The wearer has an attack of 200 and always evades. The control is a fighter made as the attacker is, and
	// what a plain hit of 40% of the wearer's attack takes from it is the amount every case is measured against.
	//   - with no row worn, an evaded ranged blow costs the attacker nothing
	//   - with a 40% row scoped to ranged, an evaded ranged blow costs the attacker that amount
	//   - an evaded melee blow costs it nothing
	//   - an evaded spell, which is neither melee nor ranged by its tags, costs it that amount: "ranged" is any
	//     attack that is not melee
	//   - a ranged blow that is not evaded costs it nothing
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	{
		const FFighter Wearer(World, 200.0f);
		const FFighter Attacker(World, 100.0f);
		const FFighter Control(World, 100.0f);
		StandAt(Wearer, 0.0f);
		StandAt(Attacker, 4.0f);
		StandAt(Control, 8.0f);
		Wearer.Set(UCataclysmCombatAttributeSet::GetEvasionAttribute(), CertainEvasion);

		const float Plain = PlainHitTakes(Wearer, Control, 40.0f);
		if (!TestTrue(*FString::Printf(TEXT("set-up: a plain hit of 40%% of the wearer's attack takes %.2f"), Plain),
				Plain > 0.0f))
		{
			return false;
		}

		// WHAT ONE BLOW OF THE ATTACKER'S ON THE WEARER COSTS THE ATTACKER.
		const auto CostOf = [&](EBlowKind BlowKind, bool bShouldBeEvaded, const TCHAR* What)
		{
			const float Was = Attacker.Health();
			const FCataclysmDamageResult Result = BlowOfKind(Attacker, Wearer, BlowKind);
			if (Result.bEvaded != bShouldBeEvaded)
			{
				AddError(FString::Printf(TEXT("set-up: %s was %s"), What,
					Result.bEvaded ? TEXT("evaded") : TEXT("not evaded")));
			}
			return Was - Attacker.Health();
		};

		TestEqual(TEXT("with no row worn, an evaded ranged blow costs the attacker nothing"),
			CostOf(EBlowKind::Ranged, true, TEXT("the ranged blow with no row")), 0.0f, 0.001f);

		Wearer.AbilitySystem->SetPoolActions(
			{StrikeOn(TEXT("dodge"), 40.0f, UCataclysmDamageCalculation::RangedTag())});
		if (!TestTrue(TEXT("set-up: the row is scoped to the ranged tag"),
				UCataclysmDamageCalculation::RangedTag().IsValid()))
		{
			return false;
		}

		TestEqual(TEXT("an evaded ranged blow costs the attacker what a plain hit of 40% takes"),
			CostOf(EBlowKind::Ranged, true, TEXT("the ranged blow")), Plain, 0.01f);
		TestEqual(TEXT("an evaded melee blow costs the attacker nothing"),
			CostOf(EBlowKind::Melee, true, TEXT("the melee blow")), 0.0f, 0.001f);
		TestEqual(TEXT("an evaded spell, which is not melee, costs the attacker the same as the ranged blow"),
			CostOf(EBlowKind::Spell, true, TEXT("the spell")), Plain, 0.01f);

		Wearer.Set(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
		TestEqual(TEXT("a ranged blow that is not evaded costs the attacker nothing"),
			CostOf(EBlowKind::Ranged, false, TEXT("the ranged blow at no evasion")), 0.0f, 0.001f);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmStrikeTargetCannotBeEvadedTest,
	"Cataclysm.StrikeTarget.TheHitLandsOnAnAttackerThatEvadesAnOrdinaryBlowAndRaisesNoDodgeOfItsOwn")
{
	using namespace CataclysmStrikeTargetTest;
	// THE WEARER STANDS AT THE ORIGIN, THE ATTACKER 4 M EAST OF IT AND THE CONTROL 8 M EAST.
	//
	// The attacker always evades too, and counts each dodge of its own with a stack. THE CONTROL COMES FIRST: an
	// ordinary hit of 40% of the wearer's attack on the attacker is evaded, takes nothing and is counted as one
	// dodge. Then the attacker's ranged blow is evaded by the wearer, and the row's hit takes from the attacker
	// what a plain hit of 40% takes from a fighter with no evasion, and the attacker's count of dodges stays one.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	{
		const FFighter Wearer(World, 200.0f);
		const FFighter Attacker(World, 100.0f);
		const FFighter Control(World, 100.0f);
		StandAt(Wearer, 0.0f);
		StandAt(Attacker, 4.0f);
		StandAt(Control, 8.0f);
		Wearer.Set(UCataclysmCombatAttributeSet::GetEvasionAttribute(), CertainEvasion);
		Attacker.Set(UCataclysmCombatAttributeSet::GetEvasionAttribute(), CertainEvasion);
		Wearer.AbilitySystem->SetPoolActions(
			{StrikeOn(TEXT("dodge"), 40.0f, UCataclysmDamageCalculation::RangedTag())});
		const FName Dodges(TEXT("A_row:dodges_counted"));
		Attacker.AbilitySystem->SetPoolActions({StackOn(TEXT("dodge"), Dodges)});

		const float Plain = PlainHitTakes(Wearer, Control, 40.0f);
		if (!TestTrue(*FString::Printf(TEXT("set-up: a plain hit of 40%% of the wearer's attack takes %.2f"), Plain),
				Plain > 0.0f))
		{
			return false;
		}

		// THE CONTROL: an ordinary hit of the same size, at the same attacker.
		float Was = Attacker.Health();
		FCataclysmDamageResult Ordinary;
		UCataclysmSkillEffects::ApplyHit(Wearer.Actor, Attacker.Actor, 40.0f, FGameplayTagContainer(),
			FCataclysmHitDelivery(), &Ordinary);
		TestTrue(TEXT("control: the attacker evades an ordinary hit of the wearer's"), Ordinary.bEvaded);
		TestEqual(TEXT("control: which takes nothing from it"), Was - Attacker.Health(), 0.0f, 0.001f);
		TestEqual(TEXT("control: and the attacker counted that dodge"),
			Attacker.AbilitySystem->OwnStacksHeld(Dodges), 1);

		Was = Attacker.Health();
		const FCataclysmDamageResult Result = BlowOfKind(Attacker, Wearer, EBlowKind::Ranged);
		if (!TestTrue(TEXT("set-up: the wearer evaded the attacker's ranged blow"), Result.bEvaded))
		{
			return false;
		}
		TestEqual(TEXT("the row's hit took from the attacker what a plain hit takes from a fighter with no evasion"),
			Was - Attacker.Health(), Plain, 0.01f);
		TestEqual(TEXT("and the attacker counted no dodge for it"),
			Attacker.AbilitySystem->OwnStacksHeld(Dodges), 1);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmDodgeCarriesItsKindAndItsAttackerTest,
	"Cataclysm.StrikeTarget.ADodgeRowScopedToRangedCountsARangedEvadeAndNotAMeleeOne")
{
	using namespace CataclysmStrikeTargetTest;
	// THE WEARER STANDS AT THE ORIGIN, THE ATTACKER 4 M EAST OF IT AND THE CONTROL 8 M EAST.
	//
	// THE TAGS: the wearer holds three stack rows on `dodge`, one scoped to ranged, one scoped to melee and one
	// with no scope. Each evaded blow is counted by the rows its kind allows, and the count each row holds is
	// compared with the count before. The form of the notice that carries nothing is counted by the unscoped
	// row alone.
	//
	// THE CHARACTER: with a strike row added, a notice naming nobody costs the attacker nothing, and the same
	// notice naming the attacker costs it what a plain hit takes.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	{
		const FFighter Wearer(World, 200.0f);
		const FFighter Attacker(World, 100.0f);
		const FFighter Control(World, 100.0f);
		StandAt(Wearer, 0.0f);
		StandAt(Attacker, 4.0f);
		StandAt(Control, 8.0f);
		Wearer.Set(UCataclysmCombatAttributeSet::GetEvasionAttribute(), CertainEvasion);

		const FGameplayTag Melee = UCataclysmDamageCalculation::MeleeTag();
		const FGameplayTag Ranged = UCataclysmDamageCalculation::RangedTag();
		if (!TestTrue(TEXT("set-up: both tags are known"), Melee.IsValid() && Ranged.IsValid()))
		{
			return false;
		}
		const FName OnRanged(TEXT("A_row:on_ranged"));
		const FName OnMelee(TEXT("A_row:on_melee"));
		const FName OnAny(TEXT("A_row:on_any"));
		Wearer.AbilitySystem->SetPoolActions({StackOn(TEXT("dodge"), OnRanged, Ranged),
			StackOn(TEXT("dodge"), OnMelee, Melee), StackOn(TEXT("dodge"), OnAny)});

		const auto Counts = [&]()
		{
			return FString::Printf(TEXT("%d %d %d"), Wearer.AbilitySystem->OwnStacksHeld(OnRanged),
				Wearer.AbilitySystem->OwnStacksHeld(OnMelee), Wearer.AbilitySystem->OwnStacksHeld(OnAny));
		};
		TestEqual(TEXT("before any blow: ranged, melee, any"), Counts(), FString(TEXT("0 0 0")));

		if (!TestTrue(TEXT("set-up: the melee blow was evaded"),
				BlowOfKind(Attacker, Wearer, EBlowKind::Melee).bEvaded))
		{
			return false;
		}
		TestEqual(TEXT("an evaded melee blow: ranged, melee, any"), Counts(), FString(TEXT("0 1 1")));

		if (!TestTrue(TEXT("set-up: the ranged blow was evaded"),
				BlowOfKind(Attacker, Wearer, EBlowKind::Ranged).bEvaded))
		{
			return false;
		}
		TestEqual(TEXT("an evaded ranged blow: ranged, melee, any"), Counts(), FString(TEXT("1 1 2")));

		if (!TestTrue(TEXT("set-up: the spell was evaded"),
				BlowOfKind(Attacker, Wearer, EBlowKind::Spell).bEvaded))
		{
			return false;
		}
		TestEqual(TEXT("an evaded spell is ranged: ranged, melee, any"), Counts(), FString(TEXT("2 1 3")));

		Wearer.AbilitySystem->NoteEvaded();
		TestEqual(TEXT("a notice carrying no tags reaches the unscoped row alone: ranged, melee, any"),
			Counts(), FString(TEXT("2 1 4")));

		// AND THE CHARACTER THE EVENT CARRIES.
		const float Plain = PlainHitTakes(Wearer, Control, 40.0f);
		if (!TestTrue(*FString::Printf(TEXT("set-up: a plain hit of 40%% of the wearer's attack takes %.2f"), Plain),
				Plain > 0.0f))
		{
			return false;
		}
		Wearer.AbilitySystem->SetPoolActions({StrikeOn(TEXT("dodge"), 40.0f, Ranged)});
		float Was = Attacker.Health();
		Wearer.AbilitySystem->NoteEvaded(nullptr, /*bMelee=*/false);
		TestEqual(TEXT("a ranged dodge naming nobody costs the attacker nothing"),
			Was - Attacker.Health(), 0.0f, 0.001f);
		Was = Attacker.Health();
		Wearer.AbilitySystem->NoteEvaded(Attacker.Actor, /*bMelee=*/false);
		TestEqual(TEXT("the same dodge naming the attacker costs it what a plain hit of 40% takes"),
			Was - Attacker.Health(), Plain, 0.01f);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmStrikeTargetOnABlockTest,
	"Cataclysm.StrikeTarget.TheSameActionOnABlockStrikesTheAttackerWhoseBlowWasBlocked")
{
	using namespace CataclysmStrikeTargetTest;
	// THE WEARER STANDS AT THE ORIGIN, THE ATTACKER 4 M EAST OF IT AND THE CONTROL 8 M EAST.
	//
	// THE ACTION IS NOT THE DODGE'S. `block` names its attacker too, so a 50% row on `block` costs an attacker
	// whose blow was blocked what a plain hit of 50% of the wearer's attack takes from the control. A blow that
	// is not blocked costs the attacker nothing. `block` passes no tags, so the row states no scope.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	{
		const FFighter Wearer(World, 200.0f);
		const FFighter Attacker(World, 100.0f);
		const FFighter Control(World, 100.0f);
		StandAt(Wearer, 0.0f);
		StandAt(Attacker, 4.0f);
		StandAt(Control, 8.0f);
		Wearer.Set(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 100.0f);
		Wearer.AbilitySystem->SetPoolActions({StrikeOn(TEXT("block"), 50.0f)});

		const float Plain = PlainHitTakes(Wearer, Control, 50.0f);
		if (!TestTrue(*FString::Printf(TEXT("set-up: a plain hit of 50%% of the wearer's attack takes %.2f"), Plain),
				Plain > 0.0f))
		{
			return false;
		}

		float Was = Attacker.Health();
		{
			const FPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);
			if (!TestFalse(TEXT("set-up: a blow that is not blocked"),
					BlowOfKind(Attacker, Wearer, EBlowKind::Melee).bBlocked))
			{
				return false;
			}
		}
		TestEqual(TEXT("a blow that is not blocked costs the attacker nothing"),
			Was - Attacker.Health(), 0.0f, 0.001f);

		Was = Attacker.Health();
		{
			const FPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
			if (!TestTrue(TEXT("set-up: the blow was blocked"),
					BlowOfKind(Attacker, Wearer, EBlowKind::Melee).bBlocked))
			{
				return false;
			}
		}
		TestEqual(TEXT("a blocked blow costs the attacker what a plain hit of 50% takes"),
			Was - Attacker.Health(), Plain, 0.01f);
	}
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
