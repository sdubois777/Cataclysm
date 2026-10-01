// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * Issue #1833 group D part 5, ruled 2026-10-01 under the owner's delegation: two
 * events. `gadget_destroyed` is raised on a summoner when a gadget of theirs is
 * killed; "destroyed" means killed, so a gadget whose lifespan runs out raises
 * nothing. `resource_consumed` is raised when a class-resource cost is paid, once
 * per payment, and when a next-use charge is spent, once per charge. Mana is not a
 * resource here, and the Fervour mechanics' own spends raise nothing.
 *
 * EVERY EVENT IS REACHED THROUGH REAL CODE: a gadget killed by a real blow, an
 * Ultimate and two skills activated through the ability system, a heal through
 * `TopUp`, and a death through `HandleDeath`. Each test counts the event on the
 * component's own `OnActionEvent`.
 */
namespace CataclysmGadgetAndResourceTest
{
	constexpr float M = 100.0f;

	/** A caster with health, mana and a class resource pool, and a body to search from. */
	struct FScopedCaster
	{
		FScopedCaster(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
			check(Actor);
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Where);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		~FScopedCaster()
		{
			if (IsValid(Actor))
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

		/** A skill of class T in the slot named, as the weapon would grant it. */
		template <typename T>
		T* Grant(ECataclysmAbilitySlot Slot, const TCHAR* ParamText, const TCHAR* TagCell)
		{
			const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbilityInSlot(
				T::StaticClass(), Slot, /*Level=*/100, Actor);
			FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
			T* Skill = Spec ? Cast<T>(Spec->GetPrimaryInstance()) : nullptr;
			if (Skill)
			{
				Skill->SkillName = TEXT("Test Skill");
				Skill->Params = UCataclysmSkillShapes::ParseParams(ParamText);
				Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagCell);
			}
			return Skill;
		}

		bool Use(UGameplayAbility* Ability) const
		{
			return Ability && AbilitySystem->TryActivateAbility(
				Ability->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

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

	ACataclysmMinion* Summon(AActor* Summoner, const TCHAR* Type, const FVector& Where,
		float Lifetime = 60.0f)
	{
		return ACataclysmMinion::Spawn(Summoner, Where, Lifetime, /*bBurns=*/false, Type);
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_TEST(FCataclysmGadgetKilledTest,
	"Cataclysm.GadgetAndResource.AKilledGadgetTellsItsSummonerOnceAndATrapIsAGadget")
{
	using namespace CataclysmGadgetAndResourceTest;

	// A BOLT TURRET AND A SPIKE TRAP, both `Type.Deployable`, each killed by a real
	// blow: each tells its summoner once, and a second blow on a body tells nothing.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FScopedCaster Summoner(World, FVector::ZeroVector);
	FScopedCaster Killer(World, FVector(0.0f, 10.0f * M, 0.0f));
	ACataclysmMinion* Turret = Summon(Summoner.Actor, TEXT("BoltTurret"), FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmMinion* Trap = Summon(Summoner.Actor, TEXT("SpikeTrap"), FVector(-3.0f * M, 0.0f, 0.0f));
	ON_SCOPE_EXIT { if (IsValid(Turret)) { Turret->Destroy(); } };
	ON_SCOPE_EXIT { if (IsValid(Trap)) { Trap->Destroy(); } };
	if (!TestNotNull(TEXT("set-up: a bolt turret"), Turret) || !TestNotNull(TEXT("set-up: a spike trap"), Trap)
		|| !TestTrue(TEXT("set-up: both are deployable"), Turret->IsDeployable() && Trap->IsDeployable()))
	{
		return false;
	}
	FEventCount Destroyed(Summoner.AbilitySystem, TEXT("gadget_destroyed"));

	UCataclysmSkillEffects::ApplyDirectDamage(Killer.Actor, Turret, 100000.0f);
	TestTrue(TEXT("the turret died"), UCataclysmSkillEffects::IsDead(Turret));
	TestEqual(TEXT("and its summoner was told once"), Destroyed.Count, 1);
	UCataclysmSkillEffects::ApplyDirectDamage(Killer.Actor, Turret, 100000.0f);
	TestEqual(TEXT("a blow on its body tells nothing more"), Destroyed.Count, 1);
	UCataclysmSkillEffects::ApplyDirectDamage(Killer.Actor, Trap, 100000.0f);
	TestTrue(TEXT("the trap died"), UCataclysmSkillEffects::IsDead(Trap));
	TestEqual(TEXT("and a trap is a gadget too"), Destroyed.Count, 2);
	return true;
}

CATACLYSM_TEST(FCataclysmGadgetNotKilledTest,
	"Cataclysm.GadgetAndResource.AGadgetWhoseLifespanEndsAndAKilledImpTellNothing")
{
	using namespace CataclysmGadgetAndResourceTest;

	// "DESTROYED" MEANS KILLED, ruled 2026-10-01. A turret whose one second of life
	// runs out leaves through its lifespan and tells nothing; an Imp, which is not
	// deployable, tells nothing when killed.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FScopedCaster Summoner(World, FVector::ZeroVector);
	FScopedCaster Killer(World, FVector(0.0f, 10.0f * M, 0.0f));
	ACataclysmMinion* Fleeting = Summon(Summoner.Actor, TEXT("BoltTurret"), FVector(3.0f * M, 0.0f, 0.0f),
		/*Lifetime=*/1.0f);
	ACataclysmMinion* Imp = Summon(Summoner.Actor, TEXT("Imp"), FVector(-3.0f * M, 0.0f, 0.0f));
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };
	if (!TestNotNull(TEXT("set-up: a turret living one second"), Fleeting) || !TestNotNull(TEXT("set-up: an Imp"), Imp)
		|| !TestFalse(TEXT("set-up: the Imp is not deployable"), Imp->IsDeployable()))
	{
		return false;
	}
	FEventCount Destroyed(Summoner.AbilitySystem, TEXT("gadget_destroyed"));

	const TWeakObjectPtr<ACataclysmMinion> Watched(Fleeting);
	CataclysmTestWorld::RunClock(World, 1.5f);
	TestFalse(TEXT("set-up: the turret's lifespan ran out and it left"),
		Watched.IsValid() && !Watched->IsActorBeingDestroyed());
	TestEqual(TEXT("and its summoner was told nothing"), Destroyed.Count, 0);

	UCataclysmSkillEffects::ApplyDirectDamage(Killer.Actor, Imp, 100000.0f);
	TestTrue(TEXT("the Imp died"), UCataclysmSkillEffects::IsDead(Imp));
	TestEqual(TEXT("and an Imp is no gadget, so nothing is told"), Destroyed.Count, 0);
	return true;
}

CATACLYSM_TEST(FCataclysmClassResourcePaidTest,
	"Cataclysm.GadgetAndResource.AnUltimatesClassResourceCostRaisesResourceConsumedOnce")
{
	using namespace CataclysmGadgetAndResourceTest;

	// THE ULTIMATE'S FIFTY, PAID THROUGH A REAL ACTIVATION: one payment, one event,
	// however many points it took.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FScopedCaster Caster(World, FVector::ZeroVector);
	UCataclysmSelfBuffSkill* Ultimate = Caster.Grant<UCataclysmSelfBuffSkill>(
		ECataclysmAbilitySlot::Ultimate, TEXT("Duration=6"), TEXT(""));
	if (!TestNotNull(TEXT("set-up: an Ultimate"), Ultimate))
	{
		return false;
	}
	Caster.Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 80.0f);
	FEventCount Consumed(Caster.AbilitySystem, TEXT("resource_consumed"));

	if (!TestTrue(TEXT("the Ultimate is cast"), Caster.Use(Ultimate)))
	{
		return false;
	}
	TestEqual(TEXT("set-up: it took its fifty"),
		Caster.Get(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute()), 30.0f, 0.001f);
	TestEqual(TEXT("and raised resource_consumed once"), Consumed.Count, 1);
	return true;
}

CATACLYSM_TEST(FCataclysmManaAndChargesTest,
	"Cataclysm.GadgetAndResource.ManaAloneRaisesNothingAndTwoChargesSpentRaiseTwo")
{
	using namespace CataclysmGadgetAndResourceTest;

	// MANA IS NOT A RESOURCE HERE, ruled 2026-10-01: a strike paying mana and nothing
	// else raises nothing. A second strike, holding two "next skill" charges, spends
	// both through a real activation and raises the event twice, once per charge.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FScopedCaster Caster(World, FVector::ZeroVector);
	UCataclysmStrikeSkill* Heavy = Caster.Grant<UCataclysmStrikeSkill>(
		ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"), TEXT("Type.Melee"));
	UCataclysmStrikeSkill* Special = Caster.Grant<UCataclysmStrikeSkill>(
		ECataclysmAbilitySlot::Special, TEXT("Radius=4; Angle=360"), TEXT("Type.Melee"));
	if (!TestNotNull(TEXT("set-up: a heavy strike"), Heavy) || !TestNotNull(TEXT("set-up: a special strike"), Special))
	{
		return false;
	}
	FEventCount Consumed(Caster.AbilitySystem, TEXT("resource_consumed"));
	const FGameplayAttribute Mana = UCataclysmVitalAttributeSet::GetManaAttribute();

	const float ManaBefore = Caster.Get(Mana);
	if (!TestTrue(TEXT("the heavy strike is used"), Caster.Use(Heavy))
		|| !TestTrue(TEXT("set-up: and it paid mana"), Caster.Get(Mana) < ManaBefore))
	{
		return false;
	}
	TestEqual(TEXT("a strike paying mana alone raises nothing"), Consumed.Count, 0);

	const FName Charge(TEXT("A_row:next_skill_damage"));
	Caster.AbilitySystem->GrantNextUseCharge(Charge, /*bAttack=*/false, 30.0f, /*Cap=*/5);
	Caster.AbilitySystem->GrantNextUseCharge(Charge, /*bAttack=*/false, 30.0f, /*Cap=*/5);
	if (!TestEqual(TEXT("set-up: two charges held"), Caster.AbilitySystem->NextUseChargesHeld(Charge), 2)
		|| !TestTrue(TEXT("the special strike is used"), Caster.Use(Special)))
	{
		return false;
	}
	TestEqual(TEXT("set-up: it spent both charges"), Caster.AbilitySystem->NextUseChargesHeld(Charge), 0);
	TestEqual(TEXT("and raised resource_consumed once per charge"), Consumed.Count, 2);
	return true;
}

CATACLYSM_TEST(FCataclysmSpellCooldownChargeTest,
	"Cataclysm.GadgetAndResource.ASpellSpendingItsCooldownChargeRaisesOne")
{
	using namespace CataclysmGadgetAndResourceTest;

	// "Each spell cast reduces your next spell cooldown", spent by a real spell's
	// cooldown: one charge spent, one event.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FScopedCaster Caster(World, FVector::ZeroVector);
	UCataclysmSelfBuffSkill* Spell = Caster.Grant<UCataclysmSelfBuffSkill>(
		ECataclysmAbilitySlot::Movement, TEXT("Duration=6"), TEXT("Type.Spell"));
	if (!TestNotNull(TEXT("set-up: a spell"), Spell))
	{
		return false;
	}
	const FName Charge(TEXT("A_row:next_spell_cooldown_reduced"));
	Caster.AbilitySystem->GrantNextUseCharge(Charge, /*bAttack=*/false, 1.0f, /*Cap=*/5,
		/*bEffectiveness=*/false, /*bSpellCooldown=*/true);
	FEventCount Consumed(Caster.AbilitySystem, TEXT("resource_consumed"));

	if (!TestEqual(TEXT("set-up: one charge held"), Caster.AbilitySystem->NextUseChargesHeld(Charge), 1)
		|| !TestTrue(TEXT("the spell is cast"), Caster.Use(Spell)))
	{
		return false;
	}
	TestEqual(TEXT("set-up: its cooldown spent the charge"), Caster.AbilitySystem->NextUseChargesHeld(Charge), 0);
	TestEqual(TEXT("and raised resource_consumed once"), Consumed.Count, 1);
	return true;
}

CATACLYSM_TEST(FCataclysmNotConsumedTest,
	"Cataclysm.GadgetAndResource.FervourLostToHealingAndADeathsClearRaiseNothing")
{
	using namespace CataclysmGadgetAndResourceTest;

	// TWO THINGS THAT EMPTY A POOL AND ARE NOT CONSUMPTION. Fervour lost to a heal
	// through `TopUp`, the Fervour mechanics' own spend: were it consumption, the row
	// that heals on this event would remove Fervour by healing and heal again. And a
	// death clearing the player's charges, which spends nothing.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	ACataclysmPlayerCharacter* Player = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	FScopedCaster Killer(World, FVector(0.0f, 10.0f * M, 0.0f));
	if (!TestNotNull(TEXT("a player state"), State) || !TestNotNull(TEXT("a player"), Player))
	{
		return false;
	}
	Player->SetPlayerState(State);
	Player->OnRep_PlayerState();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(Player->GetAbilitySystemComponent());
	if (!TestNotNull(TEXT("the player's ability system"), AbilitySystem))
	{
		return false;
	}
	const FGameplayAttribute Fervour = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmClassResourceAttributeSet::GetFervourLostToHealingAttribute(), 1.0f);
	AbilitySystem->SetNumericAttributeBase(Fervour, 50.0f);
	AbilitySystem->SetNumericAttributeBase(Health, AbilitySystem->GetNumericAttribute(MaxHealth) / 2.0f);
	FEventCount Consumed(AbilitySystem, TEXT("resource_consumed"));

	UCataclysmRegeneration::TopUp(*AbilitySystem, Health, MaxHealth,
		AbilitySystem->GetNumericAttribute(MaxHealth) / 4.0f, FGameplayTagContainer());
	TestTrue(*FString::Printf(TEXT("set-up: the heal took Fervour: %.2f left of 50"),
		AbilitySystem->GetNumericAttribute(Fervour)), AbilitySystem->GetNumericAttribute(Fervour) < 50.0f);
	TestEqual(TEXT("Fervour lost to healing raises nothing"), Consumed.Count, 0);

	const FName Charge(TEXT("A_row:next_skill_damage"));
	AbilitySystem->GrantNextUseCharge(Charge, /*bAttack=*/false, 30.0f, /*Cap=*/5);
	UCataclysmSkillEffects::ApplyDirectDamage(Killer.Actor, Player, 1000000.0f);
	if (!TestTrue(TEXT("the player died"), UCataclysmSkillEffects::IsDead(Player)))
	{
		return false;
	}
	Player->Revive();
	TestEqual(TEXT("set-up: the respawn cleared the charge"), AbilitySystem->NextUseChargesHeld(Charge), 0);
	TestEqual(TEXT("and a death's clear raises nothing"), Consumed.Count, 0);
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
