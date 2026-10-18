// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmDelayedDamage.h"
#include "AbilitySystem/CataclysmHealthDebt.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveTriggers.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmTestWorld.h"
#include "Misc/ScopeExit.h"

/**
 * A player killed by either drain notes its death's save exactly once. Ruled 2026-10-18 by the coordinating session.
 *
 * WHAT THESE GUARD. `UCataclysmDelayedDamage::DrainStep` and `UCataclysmHealthDebt::DrainWhileDebtExceedsHealth`
 * write health to nought and then call `HandleDeath` on the actor they were given. The write already reaches
 * `HandleDeath`, through `UCataclysmVitalAttributeSet::PostAttributeBaseChange` and `NotifyIfHealthReachedZero`, and
 * `ACataclysmPlayerCharacter::HandleDeath` notes `CharacterDied` to the save writer BEFORE its once-only guard. So a
 * second call notes the death's save a second time.
 *
 * WHAT IS COUNTED. `UCataclysmSaveWriter`, the world's save writer subsystem, counts in `TriggersWithNowhereToWrite`
 * every trigger it is told of while no run has been begun, which in a test world is every one: nothing here calls
 * `BeginRun`, so nothing is written to disk. Every figure is a difference against the count read before.
 *
 * THE PLAYER IS BUILT BY THE CLIENT PATH, `SetPlayerState` and `OnRep_PlayerState`, as `CataclysmDeathTests.cpp`
 * builds one: its ability system lives on the player state and the pawn is the avatar, as in play. It stands alone at
 * the origin; nothing else is spawned.
 *
 * THE CLOCK IS THE WORLD'S, MOVED BY HAND, and each drain is called as `ACataclysmCharacterBase::RegenerationStep`
 * calls it, as `CataclysmDelayedDamageTests.cpp` and `CataclysmHealthDebtTests.cpp` drive them. Nothing waits on a
 * tick, and the save writer is never ticked, so its clock and health checks raise nothing.
 */
namespace CataclysmDrainDeathNotedOnceTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Resource = UCataclysmClassResourceAttributeSet;

	/** How near two figures must be to be read as equal. */
	constexpr float DrainDeathTolerance = 0.01f;

	/** A player pawn on a player state, by the client path. Null when either could not be spawned. */
	ACataclysmPlayerCharacter* SpawnDrainDeathPlayer(UWorld* World)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Player =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!State || !Player)
		{
			return nullptr;
		}
		Player->SetPlayerState(State);
		Player->OnRep_PlayerState();
		return Player;
	}

	/** Maximum health first, then current: the vital set clamps health to the maximum. */
	void SetDrainDeathHealth(UAbilitySystemComponent* AbilitySystem, float Health, float MaxHealth)
	{
		AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), MaxHealth);
		AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), Health);
	}

	float DrainDeathHealthOf(const UAbilitySystemComponent* AbilitySystem)
	{
		return AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
	}

	/** Whether the writer's last trigger is a character's death. */
	bool DrainDeathLastNotedIsADeath(const UCataclysmSaveWriter* Writer)
	{
		return Writer->LastTrigger() == ECataclysmSaveTrigger::CharacterDied;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDrainDeathNotedOnceControlTest,
	"Cataclysm.DrainDeathNotedOnce.AWriteOfAPlayersHealthToNoughtNotesItsDeathOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE CONTROL FOR THE TWO BELOW: the path both drains rely on. A direct write of the player's health to nought reaches
 * `HandleDeath` through the attribute hook, and the death is noted once.
 */
bool FCataclysmDrainDeathNotedOnceControlTest::RunTest(const FString&)
{
	using namespace CataclysmDrainDeathNotedOnceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnDrainDeathPlayer(World);
	UCataclysmSaveWriter* Writer = UCataclysmSaveWriter::In(World);
	if (!TestNotNull(TEXT("set-up: a player on a player state"), Player)
		|| !TestNotNull(TEXT("set-up: the world's save writer"), Writer))
	{
		return false;
	}
	UAbilitySystemComponent* AbilitySystem = Player->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("set-up: the player's ability system"), AbilitySystem)
		|| !TestFalse(TEXT("set-up: no run is begun, so every trigger is counted as having nowhere to go"),
					  Writer->IsWriting()))
	{
		return false;
	}
	SetDrainDeathHealth(AbilitySystem, 400.0f, 1000.0f);
	if (!TestFalse(TEXT("control: the player is alive"), UCataclysmSkillEffects::IsDead(Player)))
	{
		return false;
	}

	const int32 Before = Writer->TriggersWithNowhereToWrite();
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 0.0f);

	TestTrue(TEXT("the write of health to nought kills the player"), UCataclysmSkillEffects::IsDead(Player));
	TestEqual(TEXT("and notes exactly one trigger"), Writer->TriggersWithNowhereToWrite() - Before, 1);
	TestTrue(TEXT("and that trigger is the character's death"), DrainDeathLastNotedIsADeath(Writer));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDrainDeathNotedOnceDelayedDamageTest,
	"Cataclysm.DrainDeathNotedOnce.APlayerKilledByTheDelayedDamageDrainNotesItsDeathOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: the player at 400 of 1000 health. A parcel of 100 over a second is held by hand, as
 * `CataclysmDelayedDamageTests.cpp` holds one on a corpse, and half a second on the drain takes 50 and notes nothing:
 * the control. A parcel of 1000 over a second is then held, and a second later the drain owes more than the 350 left.
 */
bool FCataclysmDrainDeathNotedOnceDelayedDamageTest::RunTest(const FString&)
{
	using namespace CataclysmDrainDeathNotedOnceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnDrainDeathPlayer(World);
	UCataclysmSaveWriter* Writer = UCataclysmSaveWriter::In(World);
	if (!TestNotNull(TEXT("set-up: a player on a player state"), Player)
		|| !TestNotNull(TEXT("set-up: the world's save writer"), Writer))
	{
		return false;
	}
	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(Player->GetAbilitySystemComponent());
	if (!TestNotNull(TEXT("set-up: the player's ability system"), AbilitySystem)
		|| !TestFalse(TEXT("set-up: no run is begun, so every trigger is counted as having nowhere to go"),
					  Writer->IsWriting()))
	{
		return false;
	}
	SetDrainDeathHealth(AbilitySystem, 400.0f, 1000.0f);
	const int32 Before = Writer->TriggersWithNowhereToWrite();

	// THE CONTROL: a drain that does not kill writes health and notes nothing.
	if (!TestTrue(TEXT("set-up: a parcel of 100 over a second can be held by hand"),
				  AbilitySystem->HoldDelayedDamage(100.0f, 1.0f)))
	{
		return false;
	}
	World->TimeSeconds += 0.5f;
	TestEqual(TEXT("control: half a second on, the drain takes half the parcel"),
			  UCataclysmDelayedDamage::DrainStep(Player), 50.0f, DrainDeathTolerance);
	TestEqual(TEXT("control: and notes no trigger"), Writer->TriggersWithNowhereToWrite() - Before, 0);
	if (!TestFalse(TEXT("control: and the player is alive"), UCataclysmSkillEffects::IsDead(Player)))
	{
		return false;
	}

	// THE DRAIN THAT KILLS.
	if (!TestTrue(TEXT("set-up: a parcel of 1000 over a second can be held by hand"),
				  AbilitySystem->HoldDelayedDamage(1000.0f, 1.0f)))
	{
		return false;
	}
	const float Left = DrainDeathHealthOf(AbilitySystem);
	World->TimeSeconds += 1.0f;
	TestEqual(TEXT("a second on, the drain takes the health that is left"),
			  UCataclysmDelayedDamage::DrainStep(Player), Left, DrainDeathTolerance);
	TestTrue(TEXT("and the player is dead of it"), UCataclysmSkillEffects::IsDead(Player));
	TestEqual(TEXT("and the death notes exactly one trigger"), Writer->TriggersWithNowhereToWrite() - Before, 1);
	TestTrue(TEXT("and that trigger is the character's death"), DrainDeathLastNotedIsADeath(Writer));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDrainDeathNotedOnceHealthDebtTest,
	"Cataclysm.DrainDeathNotedOnce.APlayerKilledByTheHealthDebtDrainNotesItsDeathOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: the player at 400 of 1000 health, owing 401 under The Reckoning's flag, as
 * `Cataclysm.HealthDebt.ADebtPastCurrentHealthKillsUnderTheReckoning` sets it. One regeneration step bleeds 20.05 and
 * notes nothing: the control. A step of the whole drain span then wants 401, more than the health left.
 */
bool FCataclysmDrainDeathNotedOnceHealthDebtTest::RunTest(const FString&)
{
	using namespace CataclysmDrainDeathNotedOnceTest;
	using Debt = UCataclysmHealthDebt;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnDrainDeathPlayer(World);
	UCataclysmSaveWriter* Writer = UCataclysmSaveWriter::In(World);
	if (!TestNotNull(TEXT("set-up: a player on a player state"), Player)
		|| !TestNotNull(TEXT("set-up: the world's save writer"), Writer))
	{
		return false;
	}
	UAbilitySystemComponent* AbilitySystem = Player->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("set-up: the player's ability system"), AbilitySystem)
		|| !TestFalse(TEXT("set-up: no run is begun, so every trigger is counted as having nowhere to go"),
					  Writer->IsWriting()))
	{
		return false;
	}
	SetDrainDeathHealth(AbilitySystem, 400.0f, 1000.0f);
	AbilitySystem->SetNumericAttributeBase(Resource::GetHealthDebtClearedOnlyByAKillAttribute(), 1.0f);
	AbilitySystem->SetNumericAttributeBase(Resource::GetHealthOwedAttribute(), 401.0f);
	const int32 Before = Writer->TriggersWithNowhereToWrite();

	// THE CONTROL: 401 over five seconds is 80.2 a second, and a quarter second of that is 20.05.
	TestEqual(TEXT("control: one regeneration step bleeds 20.05"),
			  Debt::DrainWhileDebtExceedsHealth(Player, UCataclysmRegeneration::StepSeconds), 20.05f,
			  DrainDeathTolerance);
	TestEqual(TEXT("control: and notes no trigger"), Writer->TriggersWithNowhereToWrite() - Before, 0);
	if (!TestFalse(TEXT("control: and the player is alive"), UCataclysmSkillEffects::IsDead(Player)))
	{
		return false;
	}

	// THE DRAIN THAT KILLS: a step of the whole span wants the whole 401.
	const float Left = DrainDeathHealthOf(AbilitySystem);
	TestEqual(TEXT("a step of the whole drain span takes the health that is left"),
			  Debt::DrainWhileDebtExceedsHealth(Player, Debt::DrainSeconds), Left, DrainDeathTolerance);
	TestTrue(TEXT("and the player is dead of it"), UCataclysmSkillEffects::IsDead(Player));
	TestEqual(TEXT("and the death notes exactly one trigger"), Writer->TriggersWithNowhereToWrite() - Before, 1);
	TestTrue(TEXT("and that trigger is the character's death"), DrainDeathLastNotedIsADeath(Writer));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
