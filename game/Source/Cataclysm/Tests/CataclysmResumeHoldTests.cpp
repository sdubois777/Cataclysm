// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Player/CataclysmPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A walk resumed from a save is paused until the player's first press. Issue #2970. The project owner, 2026-11-27,
 * verbatim: "it will also need to freeze all of your minions until you press a button as well. Maybe just make them
 * load in paused?"
 *
 * WHAT IS BUILT. `ACataclysmPlayerController::HoldUntilFirstInput` pauses through the game's own pause
 * (`APlayerController::SetPause`), with no pause screen, and the first press of a key or button, read in `InputKey`,
 * ends the hold and is taken. `FCataclysmSaveLoad::CharacterAndRun` calls it when it resumes a walk inside a dungeon;
 * `Cataclysm.SaveLoad.AWalkResumedFromASaveIsPausedUntilTheFirstPress` tests that half.
 *
 * WHAT THE PAUSE STOPS IS NOT TESTED AGAIN HERE. `CataclysmPauseTests.cpp` shows the engine's pause stops a cooldown,
 * a damage over time and the dungeon's beat. This file ticks one cooldown, as the clock that shows the world held and
 * then running, because the hold is the same pause asked for another way.
 *
 * THE WORLD, AS `CataclysmPauseTests.cpp` MAKES IT: a plain `UGameInstance` and the engine's own `AGameModeBase`, which
 * the engine's pause asks, and the game's controller holding a player state. Its own copy, in its own namespace: the
 * other file's helpers are in a .cpp and a unity build may put both files in one translation unit.
 *
 * WHAT NO TEST HERE CAN SEE. The line drawn on the screen (a test world has no canvas), and a real key reaching
 * `InputKey` through the game viewport. The owner's resume of a saved walk is the test of those.
 *
 * NOT COMPILED AND NOT RUN BY THE SESSION THAT WROTE IT.
 */
namespace CataclysmResumeHoldTest
{
	/** One frame of the engine's tick, at twenty frames a second. */
	constexpr float HoldFrameSeconds = 0.05f;

	/** One key event, as the game viewport delivers it, with no viewport. */
	bool Send(ACataclysmPlayerController* Controller, const FKey& Key, const EInputEvent Event)
	{
		return Controller->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, 1.0f));
	}

	/** The engine's whole tick, frame by frame, for `Seconds`; `GFrameCounter` first, as the pause tests say why. */
	void TickHeldWorld(UWorld* World, const float Seconds)
	{
		const int32 Frames = FMath::RoundToInt(Seconds / HoldFrameSeconds);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, HoldFrameSeconds);
		}
	}

	/** A begun world with the engine's game mode in it, and the game's controller holding a player state. */
	struct FHoldWorld
	{
		FHoldWorld()
		{
			World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
			if (!World)
			{
				return;
			}
			World->SetGameInstance(NewObject<UGameInstance>(GEngine));
			FURL WithTheEnginesGameMode;
			WithTheEnginesGameMode.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
			World->SetGameMode(WithTheEnginesGameMode);

			Controller = World->SpawnActor<ACataclysmPlayerController>();
			if (!Controller)
			{
				return;
			}
			if (!Controller->PlayerState)
			{
				Controller->SetPlayerState(World->SpawnActor<APlayerState>());
			}
			Controller->SetActorTickEnabled(false);
		}

		~FHoldWorld()
		{
			if (World)
			{
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}

		FHoldWorld(const FHoldWorld&) = delete;
		FHoldWorld& operator=(const FHoldWorld&) = delete;

		bool IsUsable() const { return World && World->GetAuthGameMode() && Controller && Controller->PlayerState; }

		UWorld* World = nullptr;
		ACataclysmPlayerController* Controller = nullptr;
	};

	/** A plain actor with an ability system, to carry a cooldown: the pause tests' rig, cut to what this file needs. */
	struct FHoldRig
	{
		explicit FHoldRig(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template and a TObjectPtr deduces the wrapper.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
		}

		~FHoldRig()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		FHoldRig(const FHoldRig&) = delete;
		FHoldRig& operator=(const FHoldRig&) = delete;

		/** The seconds left on the Special key's cooldown; -1 with none. */
		float CooldownLeft() const
		{
			const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Special);
			const FGameplayEffectQuery Query =
				FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag));
			float Longest = -1.0f;
			for (const float Remaining : AbilitySystem->GetActiveEffectsTimeRemaining(Query))
			{
				Longest = FMath::Max(Longest, Remaining);
			}
			return Longest;
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

/**
 * Which key events end the hold: a press of any key or button, and nothing else. Prints "HOLD RULE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmResumeHoldRuleTest,
	"Cataclysm.ResumeHold.APressOfAnyKeyOrButtonEndsTheHoldAndNothingElseDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmResumeHoldRuleTest::RunTest(const FString&)
{
	using Controller = ACataclysmPlayerController;

	struct FCase
	{
		const TCHAR* Name;
		FKey Key;
		EInputEvent Event;
		bool bEnds;
	};
	const FCase Cases[] = {
		{TEXT("a letter key pressed"), EKeys::A, IE_Pressed, true},
		{TEXT("the left mouse button pressed"), EKeys::LeftMouseButton, IE_Pressed, true},
		{TEXT("the right mouse button pressed"), EKeys::RightMouseButton, IE_Pressed, true},
		{TEXT("a gamepad face button pressed"), EKeys::Gamepad_FaceButton_Bottom, IE_Pressed, true},
		{TEXT("Esc pressed"), EKeys::Escape, IE_Pressed, true},
		{TEXT("a letter key released"), EKeys::A, IE_Released, false},
		{TEXT("a letter key repeating"), EKeys::A, IE_Repeat, false},
		{TEXT("the mouse wheel scrolled up"), EKeys::MouseScrollUp, IE_Pressed, false},
		{TEXT("the mouse moved"), EKeys::MouseX, IE_Axis, false},
		{TEXT("a gamepad stick's axis reported as pressed"), EKeys::Gamepad_LeftX, IE_Pressed, false},
	};

	FString Line;
	for (const FCase& Case : Cases)
	{
		const bool bEnds = Controller::PressEndsTheHold(Case.Key, Case.Event);
		Line += FString::Printf(TEXT("%s%s %s"), Line.IsEmpty() ? TEXT("") : TEXT("; "), Case.Name,
								bEnds ? TEXT("ends it") : TEXT("does not"));
		TestTrue(FString::Printf(TEXT("%s %s the hold"), Case.Name, Case.bEnds ? TEXT("ends") : TEXT("does not end")),
				 bEnds == Case.bEnds);
	}
	AddInfo(FString::Printf(TEXT("HOLD RULE: %s"), *Line));
	return true;
}

/**
 * The hold pauses the world, with no pause screen, until a press. A ten second cooldown is the clock: one second
 * running (CONTROL: nine left), the hold, one second held (still nine), events that are not presses (still held), a
 * click (the hold ends, and the click is taken), one second running again (eight). Prints "HOLD".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmResumeHoldPausesTest,
	"Cataclysm.ResumeHold.TheHoldPausesTheWorldUntilAPressAndTheWorldRunsAfterIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmResumeHoldPausesTest::RunTest(const FString&)
{
	using namespace CataclysmResumeHoldTest;

	const FHoldWorld Hold;
	if (!TestTrue(TEXT("set-up: a world with the engine's game mode and the game's controller holding a player state"),
				  Hold.IsUsable()))
	{
		return false;
	}
	UWorld* World = Hold.World;
	ACataclysmPlayerController* Controller = Hold.Controller;

	const FHoldRig Waiting(World);
	const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Special);
	if (!TestTrue(TEXT("set-up: the Special key's cooldown tag"), Tag.IsValid()))
	{
		return false;
	}
	UCataclysmGameplayAbility::ApplyCooldownEffect(Waiting.AbilitySystem, Tag, 10.0f);

	const float AtStart = Waiting.CooldownLeft();
	const bool bRanBefore = !World->IsPaused() && !Controller->IsHeldUntilFirstInput();
	TickHeldWorld(World, 1.0f);
	const float AfterRunning = Waiting.CooldownLeft();

	const bool bPaused = Controller->HoldUntilFirstInput();
	TickHeldWorld(World, 1.0f);
	const float AfterHeld = Waiting.CooldownLeft();
	const bool bScreenWanted = Controller->IsPauseScreenWanted();

	// NOT PRESSES: a release, the mouse moving and the wheel. The hold takes each and stays.
	const bool bReleaseTaken = Send(Controller, EKeys::LeftMouseButton, IE_Released);
	const bool bMoveTaken = Send(Controller, EKeys::MouseX, IE_Axis);
	const bool bScrollTaken = Send(Controller, EKeys::MouseScrollUp, IE_Pressed);
	const bool bStillHeld = Controller->IsHeldUntilFirstInput() && World->IsPaused();

	// THE PRESS.
	const bool bClickTaken = Send(Controller, EKeys::LeftMouseButton, IE_Pressed);
	const bool bHeldAfter = Controller->IsHeldUntilFirstInput();
	const bool bPausedAfter = World->IsPaused();
	TickHeldWorld(World, 1.0f);
	const float AfterResumed = Waiting.CooldownLeft();

	AddInfo(FString::Printf(TEXT("HOLD: %.3f s left at the start, %.3f after a second running (ran before %s); hold "
								 "paused %s, %.3f after a second held, pause screen wanted %s; release/move/scroll taken "
								 "%s/%s/%s, still held %s; click taken %s, held after %s, paused after %s; %.3f after a "
								 "second running again"),
							AtStart, AfterRunning, bRanBefore ? TEXT("yes") : TEXT("NO"),
							bPaused ? TEXT("yes") : TEXT("NO"), AfterHeld, bScreenWanted ? TEXT("YES") : TEXT("no"),
							bReleaseTaken ? TEXT("yes") : TEXT("NO"), bMoveTaken ? TEXT("yes") : TEXT("NO"),
							bScrollTaken ? TEXT("yes") : TEXT("NO"), bStillHeld ? TEXT("yes") : TEXT("NO"),
							bClickTaken ? TEXT("yes") : TEXT("NO"), bHeldAfter ? TEXT("YES") : TEXT("no"),
							bPausedAfter ? TEXT("YES") : TEXT("no"), AfterResumed));

	if (!TestEqual(TEXT("set-up: a ten second cooldown"), AtStart, 10.0f, 0.01f))
	{
		return false;
	}
	TestTrue(TEXT("CONTROL: the world ran, unheld, before the hold"), bRanBefore);
	TestEqual(TEXT("CONTROL: a second of a running world takes a second off the cooldown"), AfterRunning, 9.0f, 0.06f);
	TestTrue(TEXT("the hold pauses the world"), bPaused);
	TestEqual(TEXT("a second of a held world takes nothing off it"), AfterHeld, AfterRunning, 0.001f);
	TestFalse(TEXT("the hold asks for no pause screen"), bScreenWanted);
	TestTrue(TEXT("events that are not presses are taken"), bReleaseTaken && bMoveTaken && bScrollTaken);
	TestTrue(TEXT("and leave the world held"), bStillHeld);
	TestTrue(TEXT("the press is taken, so it does nothing else"), bClickTaken);
	TestFalse(TEXT("the press ends the hold"), bHeldAfter);
	TestFalse(TEXT("and the world runs"), bPausedAfter);
	TestEqual(TEXT("and a second of it takes a second off the cooldown again"), AfterResumed, 8.0f, 0.06f);
	return true;
}

/**
 * Esc pressed during the hold ends it like any other key, and does not pause the game again or put up the pause
 * screen. CONTROL: Esc with no hold pauses. Prints "HOLD ESC".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmResumeHoldEscTest,
	"Cataclysm.ResumeHold.EscDuringTheHoldEndsItAndPutsUpNoPauseScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmResumeHoldEscTest::RunTest(const FString&)
{
	using namespace CataclysmResumeHoldTest;

	const FHoldWorld Hold;
	if (!TestTrue(TEXT("set-up: a world with the engine's game mode and the game's controller holding a player state"),
				  Hold.IsUsable()))
	{
		return false;
	}
	UWorld* World = Hold.World;
	ACataclysmPlayerController* Controller = Hold.Controller;

	if (!TestTrue(TEXT("set-up: the hold paused the world"), Controller->HoldUntilFirstInput()))
	{
		return false;
	}
	const bool bTaken = Send(Controller, EKeys::Escape, IE_Pressed);
	const bool bHeld = Controller->IsHeldUntilFirstInput();
	const bool bPaused = World->IsPaused();
	const bool bScreenWanted = Controller->IsPauseScreenWanted();

	// CONTROL: the next Esc, with no hold, is the ordinary Esc and pauses.
	Send(Controller, EKeys::Escape, IE_Released);
	Send(Controller, EKeys::Escape, IE_Pressed);
	const bool bNextPauses = World->IsPaused() && Controller->IsPauseScreenWanted();

	AddInfo(FString::Printf(TEXT("HOLD ESC: taken %s; held after %s, paused after %s, pause screen wanted %s; the next "
								 "Esc pauses with the screen %s"),
							bTaken ? TEXT("yes") : TEXT("NO"), bHeld ? TEXT("YES") : TEXT("no"),
							bPaused ? TEXT("YES") : TEXT("no"), bScreenWanted ? TEXT("YES") : TEXT("no"),
							bNextPauses ? TEXT("yes") : TEXT("NO")));

	TestTrue(TEXT("Esc during the hold is taken"), bTaken);
	TestFalse(TEXT("and ends the hold"), bHeld);
	TestFalse(TEXT("and the world runs"), bPaused);
	TestFalse(TEXT("with no pause screen asked for"), bScreenWanted);
	TestTrue(TEXT("CONTROL: the next Esc, with no hold, pauses and asks for the pause screen"), bNextPauses);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
