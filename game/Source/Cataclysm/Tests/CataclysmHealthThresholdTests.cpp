// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"

/**
 * Issue #1833 group D part 2, ruled 2026-09-30 under the owner's delegation: a
 * worn row on health falling below a share of its maximum, the lethal blow such
 * a row saves when it heals, an explicit cooldown on an own stack, and health
 * lowered to a share of its maximum when a dungeon floor starts.
 *
 * EACH ACTION IS BUILT BY HAND, as the item would build it from a row, so these
 * tests read the mechanism. The table's rows have tests of their own beside the
 * other enchantment rows. THE FLOOR START IS REACHED THROUGH THE GAME MODE, not
 * raised by hand, so the test sees the event leave `GoToFloor`.
 */
namespace CataclysmHealthThresholdTest
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
			SetHealth(1000.0f, 1000.0f);
		}

		~FFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** Maximum first, then current: the vital set clamps health to it. */
		void SetHealth(float Maximum, float Current) const
		{
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Maximum);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), Current);
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmVitalAttributeSet> Vitals = nullptr;
	};

	/**
	 * The lowest health a character reaches while this is alive, heard on the
	 * component's own value-change delegate, so a value later overwritten is
	 * still seen. A lethal blow saved leaves one point; one not saved reaches
	 * nought, even when a heal on the fall then refills it, which is why the end
	 * value alone cannot show a save.
	 */
	struct FLowestHealth
	{
		explicit FLowestHealth(UAbilitySystemComponent* InSystem)
			: System(InSystem)
		{
			Handle = System->GetGameplayAttributeValueChangeDelegate(
				UCataclysmVitalAttributeSet::GetHealthAttribute()).AddLambda(
				[this](const FOnAttributeChangeData& Change)
				{
					Lowest = FMath::Min(Lowest, Change.NewValue);
				});
		}
		~FLowestHealth()
		{
			System->GetGameplayAttributeValueChangeDelegate(
				UCataclysmVitalAttributeSet::GetHealthAttribute()).Remove(Handle);
		}
		float Lowest = TNumericLimits<float>::Max();
		UAbilitySystemComponent* System = nullptr;
		FDelegateHandle Handle;
	};

	/** "When health falls below `Threshold`%, restore `Percent`% of the maximum." */
	FCataclysmPoolAction HealBelow(float Threshold, float Percent, float CooldownSeconds = 0.0f)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("health_falls_below"));
		Action.Pool = FName(TEXT("health"));
		Action.Percent = Percent;
		Action.EventValue = Threshold;
		Action.TriggerCooldownSeconds = CooldownSeconds;
		Action.TriggerKey = FName(TEXT("A_row:health:health_falls_below"));
		return Action;
	}

	/** A blow from a fresh attacker carrying this much attack damage. */
	void Strike(UWorld* World, const FFighter& Defender, float AttackDamage)
	{
		const FFighter Attacker(World);
		Attacker.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), AttackDamage);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f);
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_TEST(FCataclysmHealthCrossingTest,
	"Cataclysm.HealthThreshold.ARowFiresWhenHealthFallsBelowItsShareAndNotWhileItStaysBelow")
{
	using namespace CataclysmHealthThresholdTest;

	// A ROW ON A FALL BELOW HALF, which restores 5% of the maximum. It fires on
	// the drop that crosses half, not on a drop that stays above it, not on a
	// further drop while still below, and again once health has risen above
	// half and falls again.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		const FFighter Wearer(World);
		Wearer.AbilitySystem->SetPoolActions({HealBelow(50.0f, 5.0f)});

		Wearer.SetHealth(1000.0f, 600.0f);
		TestEqual(TEXT("1000 to 600 stays above half: nothing"), Wearer.Health(), 600.0f, 0.01f);
		Wearer.SetHealth(1000.0f, 400.0f);
		TestEqual(TEXT("600 to 400 crosses half: 400 and 50"), Wearer.Health(), 450.0f, 0.01f);
		Wearer.SetHealth(1000.0f, 300.0f);
		TestEqual(TEXT("450 to 300 was already below: nothing"), Wearer.Health(), 300.0f, 0.01f);
		Wearer.SetHealth(1000.0f, 700.0f);
		Wearer.SetHealth(1000.0f, 400.0f);
		TestEqual(TEXT("risen to 700, then 400 crosses again: 400 and 50"),
			Wearer.Health(), 450.0f, 0.01f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmHealthLethalSaveTest,
	"Cataclysm.HealthThreshold.AHealingRowOnAFallBelowSavesALethalBlowOnceItsCooldownAllows")
{
	using namespace CataclysmHealthThresholdTest;

	// ARCHON'S AEGIS, BY HAND: "When your health falls below 10%, you are
	// instantly healed to 100% of your maximum health. (10 minute cd)". Ruled
	// 2026-09-30: it also saves a blow that would empty health, because 0 is
	// below 10%. The blow leaves one point, the fall below 10% fires the heal,
	// and a second lethal blow inside the cooldown kills.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		// THE RECORDER SEES A VALUE THAT IS LATER OVERWRITTEN, the control without
		// which every lowest below would only be the end value again.
		{
			const FFighter Control(World);
			const FLowestHealth Watching(Control.AbilitySystem);
			Control.SetHealth(1000.0f, 0.0f);
			Control.SetHealth(1000.0f, 500.0f);
			TestEqual(TEXT("the recorder saw the 0 that was overwritten"), Watching.Lowest, 0.0f, 0.01f);
			TestEqual(TEXT("and the end value is the 500"), Control.Health(), 500.0f, 0.01f);
		}

		const FFighter Wearer(World);
		Wearer.AbilitySystem->SetPoolActions({HealBelow(10.0f, 100.0f, 600.0f)});

		{
			const FLowestHealth Watching(Wearer.AbilitySystem);
			Strike(World, Wearer, 5000.0f);
			TestEqual(TEXT("a blow of 5000 against 1000 never takes the wearer below one point"),
				Watching.Lowest, 1.0f, 0.01f);
			TestEqual(TEXT("and it is saved and healed to full"), Wearer.Health(), 1000.0f, 0.01f);
		}
		{
			const FLowestHealth Watching(Wearer.AbilitySystem);
			Strike(World, Wearer, 5000.0f);
			TestEqual(TEXT("a second inside the ten minutes reaches nought"), Watching.Lowest, 0.0f, 0.01f);
			TestEqual(TEXT("and kills"), Wearer.Health(), 0.0f, 0.01f);
		}

		// ONLY A ROW THAT HEALS SAVES: a row on the same fall that grants a
		// stack, as the Demon King's Regalia does, does not cheat death.
		const FFighter Raging(World);
		FCataclysmPoolAction Rage;
		Rage.Event = FName(TEXT("health_falls_below"));
		Rage.EventValue = 25.0f;
		Rage.StackKey = FName(TEXT("A_row:attack_damage"));
		Rage.StackSeconds = 10.0f;
		Rage.StackCap = 1;
		Raging.AbilitySystem->SetPoolActions({Rage});
		Strike(World, Raging, 5000.0f);
		TestEqual(TEXT("a stack row on the fall does not save the blow"),
			Raging.Health(), 0.0f, 0.01f);

		// ONLY A BLOW THAT CROSSES: a wearer already below 10% has nothing left
		// to fall below.
		// AT 5% BEFORE THE ROW IS WORN, so getting there does not itself cross
		// 10% and spend the row: the save is refused for the reason named, not
		// for its cooldown.
		const FFighter Low(World);
		Low.SetHealth(1000.0f, 50.0f);
		Low.AbilitySystem->SetPoolActions({HealBelow(10.0f, 100.0f, 600.0f)});
		Strike(World, Low, 5000.0f);
		TestEqual(TEXT("a wearer already at 5% is not saved"), Low.Health(), 0.0f, 0.01f);

		// ONLY WHEN THE HEAL CAN RESTORE SOMETHING, ruled 2026-10-01: a wearer who
		// cannot be healed at all is not saved, or the save would heal nothing.
		const FFighter Unhealable(World);
		Unhealable.AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealingCeilingReductionAttribute(), 100.0f);
		Unhealable.AbilitySystem->SetPoolActions({HealBelow(10.0f, 100.0f, 600.0f)});
		Strike(World, Unhealable, 5000.0f);
		TestEqual(TEXT("a wearer whose healing ceiling is nothing is not saved"),
			Unhealable.Health(), 0.0f, 0.01f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmOwnStackCooldownTest,
	"Cataclysm.HealthThreshold.AnOwnStackGrantWaitsOutTheCooldownItsRowStates")
{
	using namespace CataclysmHealthThresholdTest;

	// THE DEMON KING'S REGALIA, BY HAND: a stack for 10 seconds on a fall below
	// 25%, with a five minute cooldown. Ruled 2026-09-30: an own stack takes a
	// cooldown only when its row states one.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		const FFighter Wearer(World);
		FCataclysmPoolAction Rage;
		Rage.Event = FName(TEXT("health_falls_below"));
		Rage.EventValue = 25.0f;
		Rage.StackKey = FName(TEXT("A_row:attack_damage"));
		Rage.StackSeconds = 10.0f;
		Rage.StackCap = 1;
		Rage.TriggerCooldownSeconds = 300.0f;
		Rage.TriggerKey = FName(TEXT("A_row:attack_damage:health_falls_below"));
		Wearer.AbilitySystem->SetPoolActions({Rage});

		Wearer.SetHealth(1000.0f, 200.0f);
		TestEqual(TEXT("the first fall below 25% grants the stack"),
			Wearer.AbilitySystem->OwnStacksHeld(Rage.StackKey), 1);
		World->TimeSeconds += 11.0f;
		TestEqual(TEXT("ten seconds later it has lapsed"),
			Wearer.AbilitySystem->OwnStacksHeld(Rage.StackKey), 0);
		Wearer.SetHealth(1000.0f, 800.0f);
		Wearer.SetHealth(1000.0f, 200.0f);
		TestEqual(TEXT("a second fall inside five minutes grants none"),
			Wearer.AbilitySystem->OwnStacksHeld(Rage.StackKey), 0);
		World->TimeSeconds += 300.0f;
		Wearer.SetHealth(1000.0f, 800.0f);
		Wearer.SetHealth(1000.0f, 200.0f);
		TestEqual(TEXT("after the five minutes a fall grants it again"),
			Wearer.AbilitySystem->OwnStacksHeld(Rage.StackKey), 1);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmFloorStartCapTest,
	"Cataclysm.HealthThreshold.AFloorStartLowersThePlayersHealthToItsShareAndNeverRaisesIt")
{
	using namespace CataclysmHealthThresholdTest;

	// "You start every dungeon floor at 30%-50% of your maximum HP". Ruled
	// 2026-09-30: `floor_start` is raised on the player after the floor's rules
	// in `StartPlay` and `GoToFloor`, and `health_capped_at` sets health to the
	// share and never raises it.
	//
	// TWO HALVES, AND WHY. The floor's rules refresh the player's equipment
	// (`UCataclysmDungeonModifierEffects::ApplyToCharacter`), and the refresh
	// rebuilds the action list from what is worn, so an action built by hand on
	// a player does not survive `GoToFloor`; the first run of this test lost its
	// cap that way. So the REAL `GoToFloor` half listens for the event on the
	// component's own `OnActionEvent`, which the refresh does not replace, and
	// the cap half runs on a bare fighter, which nothing refreshes. The row test
	// `TheFloorStartRow` wears the row through a real `GoToFloor`.
	//
	// `AController::Possess` AND NOT `APawn::PossessedBy`, for the reason
	// CataclysmDungeonModifierEffectsTests.cpp gives.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("the dungeon game mode"), Mode)
		|| !TestNotNull(TEXT("a player state"), PlayerState)
		|| !TestNotNull(TEXT("a controller"), Controller)
		|| !TestNotNull(TEXT("a player character"), Character))
	{
		return false;
	}
	Controller->SetPlayerState(PlayerState);
	Controller->Possess(Character);
	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(Character->GetAbilitySystemComponent());
	if (!TestNotNull(TEXT("the player's ability system"), AbilitySystem))
	{
		return false;
	}

	int32 FloorStarts = 0;
	const FDelegateHandle Listening = AbilitySystem->OnActionEvent.AddLambda(
		[&FloorStarts](FName Event)
		{
			FloorStarts += Event == FName(TEXT("floor_start")) ? 1 : 0;
		});
	ON_SCOPE_EXIT { AbilitySystem->OnActionEvent.Remove(Listening); };

	Mode->EnemyScale = 0.1f;
	if (!TestTrue(TEXT("the first floor was reached"), Mode->GoToFloor(1)))
	{
		return false;
	}
	TestEqual(TEXT("going to the first floor raises floor_start once on the player"), FloorStarts, 1);
	if (!TestTrue(TEXT("the second floor was reached"), Mode->GoToFloor(2)))
	{
		return false;
	}
	TestEqual(TEXT("and going to the second raises it once more"), FloorStarts, 2);

	// THE CAP, on a fighter nothing refreshes.
	FCataclysmPoolAction Cap;
	Cap.Event = FName(TEXT("floor_start"));
	Cap.Pool = FName(UCataclysmAbilitySystemComponent::HealthCappedAtAction);
	Cap.Percent = 40.0f;
	Cap.bHealthCap = true;
	Cap.TriggerKey = FName(TEXT("A_row:health_capped_at:floor_start"));

	const FFighter Full(World);
	Full.AbilitySystem->SetPoolActions({Cap});
	Full.AbilitySystem->ActOnEvent(FName(TEXT("floor_start")));
	TestEqual(TEXT("a full fighter starts the floor at 40% of its maximum"),
		Full.Health(), 400.0f, 0.01f);

	const FFighter Hurt(World);
	Hurt.SetHealth(1000.0f, 250.0f);
	Hurt.AbilitySystem->SetPoolActions({Cap});
	Hurt.AbilitySystem->ActOnEvent(FName(TEXT("floor_start")));
	TestEqual(TEXT("a fighter already at a quarter keeps its health"), Hurt.Health(), 250.0f, 0.01f);
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
