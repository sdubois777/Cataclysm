// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Interface/CataclysmPauseScreenWidget.h"
#include "Items/CataclysmDroppedItem.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Pausing. The project owner, 2026-11-24: "Also, i'm pretty sure it's already in the design, but we need to be able to
 * pause. Offline and online, pause is a must". It was not in the design, and nothing in the game read Esc.
 *
 * WHAT IS BUILT. Esc closes every open screen; with none open it pauses through the engine's own pause
 * (`APlayerController::SetPause`) and asks for the pause screen; on a paused game it resumes. The player's frame does
 * nothing to the world while paused. `docs/DECISIONS.md`, 2026-11-24.
 *
 * A WORLD THE ENGINE'S PAUSE WORKS IN. `APlayerController::SetPause` asks the world's authority game mode, and a world
 * from `CataclysmTestWorld::MakeWorldThatHasBegunPlay` has none: `UWorld::SetGameMode` needs a game instance to make
 * one. So these tests give the test world a plain `UGameInstance` and the ENGINE'S OWN `AGameModeBase`, never
 * `ACataclysmGameMode`, which would begin saving a run and supply a difficulty tier of its own (the second is the
 * reason `CataclysmTestWorld.h` gives for having no game mode at all). The controller then gets a player state from
 * that game mode, which is what the pause names as its pauser.
 *
 * THE WORLD IS TICKED BY THE ENGINE, `UWorld::Tick`, which no other test in the project does: `RunClock` moves the
 * clock and fires the timers by hand, and so would move them in a paused game too. The pause is the engine's choice not
 * to tick, so only the engine's tick can show it. Epic's own `System.Engine.TimerManager` test ticks a test world the
 * same way, advancing `GFrameCounter` each frame so the timer manager runs. The controller's own tick is switched off
 * there, so a world's frame is not also the controller's frame, which has its own test below.
 *
 * EACH CLOCK IS READ THREE TIMES: running (the control), paused (it must not move), and running again (it moves on).
 *
 * WHAT NO TEST HERE CAN SEE. The pause screen drawn, its button clicked by a mouse, and Esc reaching the game through
 * Play In Editor, whose Stop command takes Esc first unless `game/Config/DefaultEditorSettings.ini` has moved it to
 * Shift+Esc. A test world has no viewport and no local player; the owner's press is the test of those
 * (`docs/Play_Loop_Checklist.md`, step 28).
 *
 * NOT COMPILED AND NOT RUN BY THE SESSION THAT WROTE IT.
 */
namespace CataclysmPauseTest
{
	/** One frame of the engine's tick, at twenty frames a second. */
	constexpr float PauseFrameSeconds = 0.05f;

	/** What Esc did, by name, for a message. */
	const TCHAR* PauseEscapeName(const ECataclysmEscape Did)
	{
		switch (Did)
		{
			case ECataclysmEscape::ClosedScreens:
				return TEXT("ClosedScreens");
			case ECataclysmEscape::Paused:
				return TEXT("Paused");
			case ECataclysmEscape::Resumed:
				return TEXT("Resumed");
			case ECataclysmEscape::Refused:
				return TEXT("Refused");
		}
		return TEXT("(not an answer)");
	}

	/** One press of Esc, as the game viewport delivers it, with no viewport. */
	bool PressEsc(ACataclysmPlayerController* Controller, const EInputEvent Event = IE_Pressed)
	{
		return Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Escape, Event, 1.0f));
	}

	/**
	 * The engine's whole tick, frame by frame, for `Seconds`. `GFrameCounter` moves first, as `RunClock`'s comment
	 * says why: `FTimerManager::Tick` does nothing twice in one frame.
	 */
	void TickTheWorld(UWorld* World, const float Seconds)
	{
		const int32 Frames = FMath::RoundToInt(Seconds / PauseFrameSeconds);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, PauseFrameSeconds);
		}
	}

	/** A begun world with the engine's game mode in it, and the game's controller holding a player state. */
	struct FPausableWorld
	{
		FPausableWorld()
		{
			World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
			if (!World)
			{
				return;
			}

			// THE ENGINE'S PLAIN GAME MODE, made the way the engine makes one: `UWorld::SetGameMode` asks the world's
			// game instance, and the URL's "game" option names the class.
			World->SetGameInstance(NewObject<UGameInstance>(GEngine));
			FURL WithTheEnginesGameMode;
			WithTheEnginesGameMode.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
			World->SetGameMode(WithTheEnginesGameMode);

			Controller = World->SpawnActor<ACataclysmPlayerController>();
			if (!Controller)
			{
				return;
			}
			// A PLAYER STATE, which a controller is given by the game mode as it is made; one is spawned if not.
			if (!Controller->PlayerState)
			{
				Controller->SetPlayerState(World->SpawnActor<APlayerState>());
			}
			Controller->SetActorTickEnabled(false);
		}

		~FPausableWorld()
		{
			if (World)
			{
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}

		FPausableWorld(const FPausableWorld&) = delete;
		FPausableWorld& operator=(const FPausableWorld&) = delete;

		bool IsUsable() const { return World && World->GetAuthGameMode() && Controller && Controller->PlayerState; }

		UWorld* World = nullptr;
		ACataclysmPlayerController* Controller = nullptr;
	};

	/** A plain actor with an ability system, 1,000 health and no defences: `CataclysmDebuffCutsDamageTests.cpp`'s rig. */
	struct FPauseRig
	{
		explicit FPauseRig(UWorld* World)
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

			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
		}

		~FPauseRig()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		FPauseRig(const FPauseRig&) = delete;
		FPauseRig& operator=(const FPauseRig&) = delete;

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * The game's controller possessing a player character on a player state, as `FPossessedPlayer` in
	 * `CataclysmDungeonModifierEffectsTests.cpp` makes one, in a world with no game mode: the controller's frame is
	 * called by hand, so nothing here needs the world paused.
	 */
	struct FPausePlayer
	{
		explicit FPausePlayer(UWorld* World)
		{
			PlayerState = World->SpawnActor<ACataclysmPlayerState>();
			Controller = World->SpawnActor<ACataclysmPlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!PlayerState || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(PlayerState);
			Controller->Possess(Character);
		}

		bool IsUsable() const { return PlayerState && Controller && Character; }

		ACataclysmPlayerState* PlayerState = nullptr;
		ACataclysmPlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
	};
}

/**
 * The rule itself, in every case: a paused game resumes, an open screen closes, and only Esc with nothing open pauses.
 * A PAUSED GAME RESUMES EVEN WITH A SCREEN OPEN, because the pause screen is on top. Prints "ESC RULE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPauseEscRuleTest,
	"Cataclysm.Pause.EscClosesAScreenFirstPausesWithNothingOpenAndResumesAPausedGame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPauseEscRuleTest::RunTest(const FString&)
{
	using namespace CataclysmPauseTest;
	using Controller = ACataclysmPlayerController;

	const ECataclysmEscape NothingOpen = Controller::EscapeDoes(/*bAScreenIsOpen=*/false, /*bGameIsPaused=*/false);
	const ECataclysmEscape AScreenOpen = Controller::EscapeDoes(true, false);
	const ECataclysmEscape Paused = Controller::EscapeDoes(false, true);
	const ECataclysmEscape PausedAndAScreenOpen = Controller::EscapeDoes(true, true);
	AddInfo(FString::Printf(TEXT("ESC RULE: nothing open %s, a screen open %s, paused %s, paused with a screen open %s"),
							PauseEscapeName(NothingOpen), PauseEscapeName(AScreenOpen), PauseEscapeName(Paused),
							PauseEscapeName(PausedAndAScreenOpen)));

	TestEqual(TEXT("Esc with nothing open pauses"), FString(PauseEscapeName(NothingOpen)), FString(TEXT("Paused")));
	TestEqual(TEXT("Esc with a screen open closes it"), FString(PauseEscapeName(AScreenOpen)),
			  FString(TEXT("ClosedScreens")));
	TestEqual(TEXT("Esc on a paused game resumes it"), FString(PauseEscapeName(Paused)), FString(TEXT("Resumed")));
	TestEqual(TEXT("and resumes it even with a screen open under the pause screen"),
			  FString(PauseEscapeName(PausedAndAScreenOpen)), FString(TEXT("Resumed")));
	return true;
}

/**
 * Esc with nothing open pauses the world, through the engine's game mode, and asks for the pause screen. The press goes
 * in where the game viewport delivers one, `InputKey`, and the controller takes it. CONTROL: the world ran before it.
 * Prints "ESC PAUSES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPauseEscPausesTest,
	"Cataclysm.Pause.EscWithNothingOpenPausesTheGameAndAsksForThePauseScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPauseEscPausesTest::RunTest(const FString&)
{
	using namespace CataclysmPauseTest;

	const FPausableWorld Pausable;
	if (!TestTrue(TEXT("set-up: a world with the engine's game mode and the game's controller holding a player state"),
				  Pausable.IsUsable()))
	{
		return false;
	}
	UWorld* World = Pausable.World;
	ACataclysmPlayerController* Controller = Pausable.Controller;

	const bool bRanBefore = !World->IsPaused();
	const bool bNothingOpen = !Controller->HasAScreenEscCloses();
	const bool bTaken = PressEsc(Controller);
	AddInfo(FString::Printf(TEXT("ESC PAUSES: ran before %s, nothing open %s; Esc taken %s, world paused %s, pause screen "
								 "wanted %s, cursor over the interface %s"),
							bRanBefore ? TEXT("yes") : TEXT("NO"), bNothingOpen ? TEXT("yes") : TEXT("NO"),
							bTaken ? TEXT("yes") : TEXT("NO"), World->IsPaused() ? TEXT("yes") : TEXT("NO"),
							Controller->IsPauseScreenWanted() ? TEXT("yes") : TEXT("NO"),
							Controller->CursorIsOverInterface() ? TEXT("yes") : TEXT("NO")));

	TestTrue(TEXT("CONTROL: the world ran before Esc"), bRanBefore);
	TestTrue(TEXT("set-up: no screen was open"), bNothingOpen);
	TestTrue(TEXT("the controller takes Esc"), bTaken);
	TestTrue(TEXT("Esc with nothing open pauses the world"), World->IsPaused());
	TestTrue(TEXT("and asks for the pause screen"), Controller->IsPauseScreenWanted());
	TestTrue(TEXT("and the cursor reads as over the interface, so nothing under it is described"),
			 Controller->CursorIsOverInterface());
	return true;
}

/**
 * Esc on the paused game resumes it, and the pause screen is no longer wanted. Prints "ESC RESUMES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPauseEscResumesTest,
	"Cataclysm.Pause.EscOnThePausedGameResumesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPauseEscResumesTest::RunTest(const FString&)
{
	using namespace CataclysmPauseTest;

	const FPausableWorld Pausable;
	if (!TestTrue(TEXT("set-up: a world with the engine's game mode and the game's controller holding a player state"),
				  Pausable.IsUsable()))
	{
		return false;
	}
	UWorld* World = Pausable.World;
	ACataclysmPlayerController* Controller = Pausable.Controller;

	const ECataclysmEscape First = Controller->PressEscape();
	if (!TestTrue(TEXT("set-up: the first Esc paused the world"), First == ECataclysmEscape::Paused && World->IsPaused()))
	{
		return false;
	}

	const ECataclysmEscape Second = Controller->PressEscape();
	AddInfo(FString::Printf(TEXT("ESC RESUMES: first %s, second %s; world paused %s, pause screen wanted %s"),
							PauseEscapeName(First), PauseEscapeName(Second), World->IsPaused() ? TEXT("YES") : TEXT("no"),
							Controller->IsPauseScreenWanted() ? TEXT("YES") : TEXT("no")));

	TestEqual(TEXT("the second Esc resumes"), FString(PauseEscapeName(Second)), FString(TEXT("Resumed")));
	TestFalse(TEXT("the world runs again"), World->IsPaused());
	TestFalse(TEXT("and the pause screen is not wanted"), Controller->IsPauseScreenWanted());
	TestFalse(TEXT("and the cursor is no longer over the interface"), Controller->CursorIsOverInterface());

	// AND A THIRD PAUSES AGAIN: the pause can be taken any number of times.
	TestEqual(TEXT("a third Esc pauses again"), FString(PauseEscapeName(Controller->PressEscape())),
			  FString(TEXT("Paused")));
	TestTrue(TEXT("and the world is paused again"), World->IsPaused());
	return true;
}

/**
 * Esc with a screen open closes it and DOES NOT PAUSE: the control for the two tests above. A test world cannot put a
 * real screen in a viewport, so `SetAScreenOpenForTest` stands in for one; the closing itself is each screen's own
 * toggle, which the owner's press covers. The next Esc, with nothing left open, pauses. Prints "ESC CLOSES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPauseEscClosesTest,
	"Cataclysm.Pause.EscWithAScreenOpenClosesItAndDoesNotPause",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPauseEscClosesTest::RunTest(const FString&)
{
	using namespace CataclysmPauseTest;

	const FPausableWorld Pausable;
	if (!TestTrue(TEXT("set-up: a world with the engine's game mode and the game's controller holding a player state"),
				  Pausable.IsUsable()))
	{
		return false;
	}
	UWorld* World = Pausable.World;
	ACataclysmPlayerController* Controller = Pausable.Controller;

	Controller->SetAScreenOpenForTest(true);
	if (!TestTrue(TEXT("set-up: a screen Esc closes is open"), Controller->HasAScreenEscCloses()))
	{
		return false;
	}

	const bool bTaken = PressEsc(Controller);
	const bool bPausedAfterTheFirst = World->IsPaused();
	const bool bWantedAfterTheFirst = Controller->IsPauseScreenWanted();
	const bool bStillOpen = Controller->HasAScreenEscCloses();
	const ECataclysmEscape Second = Controller->PressEscape();
	AddInfo(FString::Printf(TEXT("ESC CLOSES: with a screen open, Esc taken %s, paused %s, pause screen wanted %s, still "
								 "open %s; the next Esc %s"),
							bTaken ? TEXT("yes") : TEXT("NO"), bPausedAfterTheFirst ? TEXT("YES") : TEXT("no"),
							bWantedAfterTheFirst ? TEXT("YES") : TEXT("no"), bStillOpen ? TEXT("YES") : TEXT("no"),
							PauseEscapeName(Second)));

	TestTrue(TEXT("the controller takes Esc"), bTaken);
	TestFalse(TEXT("Esc with a screen open does not pause"), bPausedAfterTheFirst);
	TestFalse(TEXT("and does not ask for the pause screen"), bWantedAfterTheFirst);
	TestFalse(TEXT("and closes the screen"), bStillOpen);
	TestEqual(TEXT("the next Esc, with nothing open, pauses"), FString(PauseEscapeName(Second)), FString(TEXT("Paused")));
	TestTrue(TEXT("and the world is paused"), World->IsPaused());
	return true;
}

/**
 * The pause screen's Resume button resumes the game, through the same handler the button's click calls. CONTROL: a
 * screen with no controller to resume resumes nothing, and the game stays paused. Prints "RESUME BUTTON".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPauseResumeButtonTest,
	"Cataclysm.Pause.ThePauseScreensResumeButtonResumesTheGame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPauseResumeButtonTest::RunTest(const FString&)
{
	using namespace CataclysmPauseTest;

	const FPausableWorld Pausable;
	if (!TestTrue(TEXT("set-up: a world with the engine's game mode and the game's controller holding a player state"),
				  Pausable.IsUsable()))
	{
		return false;
	}
	UWorld* World = Pausable.World;
	ACataclysmPlayerController* Controller = Pausable.Controller;

	if (!TestTrue(TEXT("set-up: Esc paused the world"),
				  Controller->PressEscape() == ECataclysmEscape::Paused && World->IsPaused()))
	{
		return false;
	}

	UCataclysmPauseScreenWidget* Screen = NewObject<UCataclysmPauseScreenWidget>();
	if (!TestNotNull(TEXT("set-up: a pause screen"), Screen))
	{
		return false;
	}

	const bool bResumedWithNobody = Screen->PressResume();
	const bool bStillPaused = World->IsPaused();
	Screen->SetResumer(Controller);
	const bool bResumed = Screen->PressResume();
	AddInfo(FString::Printf(TEXT("RESUME BUTTON: with no controller it resumed %s (paused %s); with the controller %s, "
								 "paused %s, screen wanted %s; the words '%s', '%s', '%s'"),
							bResumedWithNobody ? TEXT("YES") : TEXT("no"), bStillPaused ? TEXT("yes") : TEXT("NO"),
							bResumed ? TEXT("yes") : TEXT("NO"), World->IsPaused() ? TEXT("YES") : TEXT("no"),
							Controller->IsPauseScreenWanted() ? TEXT("YES") : TEXT("no"),
							UCataclysmPauseScreenWidget::TitleText, UCataclysmPauseScreenWidget::ResumeText,
							UCataclysmPauseScreenWidget::HintText));

	TestFalse(TEXT("CONTROL: a screen with no controller resumes nothing"), bResumedWithNobody);
	TestTrue(TEXT("CONTROL: and the game stays paused"), bStillPaused);
	TestTrue(TEXT("Resume resumes the game"), bResumed);
	TestFalse(TEXT("the world runs again"), World->IsPaused());
	TestFalse(TEXT("and the pause screen is not wanted"), Controller->IsPauseScreenWanted());
	TestEqual(TEXT("the screen says Paused"), FString(UCataclysmPauseScreenWidget::TitleText), FString(TEXT("Paused")));
	TestEqual(TEXT("and its button says Resume"), FString(UCataclysmPauseScreenWidget::ResumeText),
			  FString(TEXT("Resume")));
	return true;
}

/**
 * Only a PRESS of Esc acts, and no Esc event goes on to Enhanced Input: a release and a repeat are taken and do
 * nothing, so holding Esc does not pause and resume over and over. Prints "ESC EVENTS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPauseEscEventsTest,
	"Cataclysm.Pause.OnlyAPressOfEscActsAndNoEscIsPassedOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPauseEscEventsTest::RunTest(const FString&)
{
	using namespace CataclysmPauseTest;

	const FPausableWorld Pausable;
	if (!TestTrue(TEXT("set-up: a world with the engine's game mode and the game's controller holding a player state"),
				  Pausable.IsUsable()))
	{
		return false;
	}
	UWorld* World = Pausable.World;
	ACataclysmPlayerController* Controller = Pausable.Controller;

	const bool bReleaseTaken = PressEsc(Controller, IE_Released);
	const bool bPausedByARelease = World->IsPaused();
	const bool bRepeatTaken = PressEsc(Controller, IE_Repeat);
	const bool bPausedByARepeat = World->IsPaused();
	const bool bPressTaken = PressEsc(Controller, IE_Pressed);
	const bool bPausedByAPress = World->IsPaused();
	const bool bHeldRepeatTaken = PressEsc(Controller, IE_Repeat);
	const bool bStillPausedAfterARepeat = World->IsPaused();
	AddInfo(FString::Printf(TEXT("ESC EVENTS: release taken %s paused %s; repeat taken %s paused %s; press taken %s "
								 "paused %s; a repeat while paused taken %s, still paused %s"),
							bReleaseTaken ? TEXT("yes") : TEXT("NO"), bPausedByARelease ? TEXT("YES") : TEXT("no"),
							bRepeatTaken ? TEXT("yes") : TEXT("NO"), bPausedByARepeat ? TEXT("YES") : TEXT("no"),
							bPressTaken ? TEXT("yes") : TEXT("NO"), bPausedByAPress ? TEXT("yes") : TEXT("NO"),
							bHeldRepeatTaken ? TEXT("yes") : TEXT("NO"), bStillPausedAfterARepeat ? TEXT("yes") : TEXT("NO")));

	TestTrue(TEXT("a release of Esc is taken"), bReleaseTaken);
	TestFalse(TEXT("and does not pause"), bPausedByARelease);
	TestTrue(TEXT("a repeat of Esc is taken"), bRepeatTaken);
	TestFalse(TEXT("and does not pause"), bPausedByARepeat);
	TestTrue(TEXT("CONTROL: a press is taken"), bPressTaken);
	TestTrue(TEXT("CONTROL: and pauses"), bPausedByAPress);
	TestTrue(TEXT("a repeat while paused is taken"), bHeldRepeatTaken);
	TestTrue(TEXT("and does not resume"), bStillPausedAfterARepeat);
	return true;
}

/**
 * A cooldown does not run down while the game is paused. A ten second cooldown on the Special key, the effect
 * `UCataclysmGameplayAbility::ApplyCooldownEffect` builds for every skill. One second running (CONTROL: nine left), one
 * second paused (still nine), one second running again (eight). Prints "PAUSED COOLDOWN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPauseCooldownTest,
	"Cataclysm.Pause.WhilePausedACooldownDoesNotRunDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPauseCooldownTest::RunTest(const FString&)
{
	using namespace CataclysmPauseTest;

	const FPausableWorld Pausable;
	if (!TestTrue(TEXT("set-up: a world with the engine's game mode and the game's controller holding a player state"),
				  Pausable.IsUsable()))
	{
		return false;
	}
	UWorld* World = Pausable.World;
	ACataclysmPlayerController* Controller = Pausable.Controller;

	const FPauseRig Waiting(World);
	const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Special);
	if (!TestTrue(TEXT("set-up: the Special key's cooldown tag"), Tag.IsValid()))
	{
		return false;
	}
	UCataclysmGameplayAbility::ApplyCooldownEffect(Waiting.AbilitySystem, Tag, 10.0f);
	const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag));
	const auto Left = [&Waiting, &Query]()
	{
		float Longest = -1.0f;
		for (const float Remaining : Waiting.AbilitySystem->GetActiveEffectsTimeRemaining(Query))
		{
			Longest = FMath::Max(Longest, Remaining);
		}
		return Longest;
	};

	const float AtStart = Left();
	TickTheWorld(World, 1.0f);
	const float AfterRunning = Left();
	const bool bPaused = Controller->PauseTheGame();
	TickTheWorld(World, 1.0f);
	const float AfterPaused = Left();
	const bool bResumed = Controller->ResumeTheGame();
	TickTheWorld(World, 1.0f);
	const float AfterResumed = Left();
	AddInfo(FString::Printf(TEXT("PAUSED COOLDOWN: %.3f s left at the start, %.3f after a second running, %.3f after a "
								 "second paused (paused %s), %.3f after a second running again (resumed %s)"),
							AtStart, AfterRunning, AfterPaused, bPaused ? TEXT("yes") : TEXT("NO"), AfterResumed,
							bResumed ? TEXT("yes") : TEXT("NO")));

	if (!TestEqual(TEXT("set-up: a ten second cooldown"), AtStart, 10.0f, 0.01f)
		|| !TestTrue(TEXT("set-up: the game paused"), bPaused)
		|| !TestTrue(TEXT("set-up: and resumed"), bResumed))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: a second of a running game takes a second off it"), AfterRunning, 9.0f, 0.06f);
	TestEqual(TEXT("a second of a paused game takes nothing off it"), AfterPaused, AfterRunning, 0.001f);
	TestEqual(TEXT("and once the game runs again it runs down again"), AfterResumed, 8.0f, 0.06f);
	return true;
}

/**
 * A damage over time does not tick while the game is paused. A burn of 10 a tick for twenty seconds, which ticks once a
 * second whether or not its target moves (a bleed would not). Two and a half seconds running (CONTROL: health lost),
 * two and a half paused (nothing lost), two and a half running again (lost again). Prints "PAUSED BURN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPauseBurnTest,
	"Cataclysm.Pause.WhilePausedADamageOverTimeDoesNotTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPauseBurnTest::RunTest(const FString&)
{
	using namespace CataclysmPauseTest;

	const FPausableWorld Pausable;
	if (!TestTrue(TEXT("set-up: a world with the engine's game mode and the game's controller holding a player state"),
				  Pausable.IsUsable()))
	{
		return false;
	}
	UWorld* World = Pausable.World;
	ACataclysmPlayerController* Controller = Pausable.Controller;

	const FPauseRig Burner(World);
	const FPauseRig Burning(World);
	if (!TestTrue(TEXT("set-up: a burn of 10 a tick for twenty seconds lands"),
				  UCataclysmSkillEffects::ApplyDamageOverTime(Burner.Actor, Burning.Actor, 10.0f, 20.0f,
															  UCataclysmSkillEffects::BurnTag(),
															  /*bScalesWithInstigator=*/false)))
	{
		return false;
	}

	const float AtStart = Burning.Health();
	TickTheWorld(World, 2.5f);
	const float AfterRunning = Burning.Health();
	const bool bPaused = Controller->PauseTheGame();
	TickTheWorld(World, 2.5f);
	const float AfterPaused = Burning.Health();
	const bool bResumed = Controller->ResumeTheGame();
	TickTheWorld(World, 2.5f);
	const float AfterResumed = Burning.Health();
	AddInfo(FString::Printf(TEXT("PAUSED BURN: health %.2f at the start, %.2f after 2.5 s running, %.2f after 2.5 s paused "
								 "(paused %s), %.2f after 2.5 s running again (resumed %s)"),
							AtStart, AfterRunning, AfterPaused, bPaused ? TEXT("yes") : TEXT("NO"), AfterResumed,
							bResumed ? TEXT("yes") : TEXT("NO")));

	if (!TestTrue(TEXT("set-up: the game paused"), bPaused) || !TestTrue(TEXT("set-up: and resumed"), bResumed))
	{
		return false;
	}
	TestTrue(TEXT("CONTROL: a running game's burn takes health"), AfterRunning < AtStart);
	TestEqual(TEXT("a paused game's burn takes nothing"), AfterPaused, AfterRunning, 0.001f);
	TestTrue(TEXT("and once the game runs again the burn takes health again"), AfterResumed < AfterPaused);
	return true;
}

/**
 * The dungeon's beat does not advance while the game is paused. The beat is the clock every timed floor rule counts
 * by: each beat hands its rules the time it covered, and The Reaper comes when ten seconds of beats have passed on a
 * floor. A dungeon game mode in the world, ticked by the engine; a tenth of a second running (CONTROL: the beat's
 * count moves), a tenth paused (it does not), a tenth running again (it moves on). Under a quarter of a second in all,
 * so no beat fires and the rules, which want a floor, are not asked. Prints "PAUSED BEAT".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPauseBeatTest,
	"Cataclysm.Pause.WhilePausedTheDungeonsBeatDoesNotAdvance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPauseBeatTest::RunTest(const FString&)
{
	using namespace CataclysmPauseTest;

	const FPausableWorld Pausable;
	if (!TestTrue(TEXT("set-up: a world with the engine's game mode and the game's controller holding a player state"),
				  Pausable.IsUsable()))
	{
		return false;
	}
	UWorld* World = Pausable.World;
	ACataclysmPlayerController* Controller = Pausable.Controller;

	ACataclysmDungeonGameMode* Dungeon = World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("set-up: a dungeon game mode in the world"), Dungeon))
	{
		return false;
	}

	const float AtStart = Dungeon->SecondsTowardTheNextBeat();
	TickTheWorld(World, 0.1f);
	const float AfterRunning = Dungeon->SecondsTowardTheNextBeat();
	const bool bPaused = Controller->PauseTheGame();
	TickTheWorld(World, 0.1f);
	const float AfterPaused = Dungeon->SecondsTowardTheNextBeat();
	const bool bResumed = Controller->ResumeTheGame();
	TickTheWorld(World, 0.1f);
	const float AfterResumed = Dungeon->SecondsTowardTheNextBeat();
	AddInfo(FString::Printf(TEXT("PAUSED BEAT: %.3f s toward the next beat at the start, %.3f after 0.1 s running, %.3f "
								 "after 0.1 s paused (paused %s), %.3f after 0.1 s running again (resumed %s)"),
							AtStart, AfterRunning, AfterPaused, bPaused ? TEXT("yes") : TEXT("NO"), AfterResumed,
							bResumed ? TEXT("yes") : TEXT("NO")));

	if (!TestTrue(TEXT("set-up: the game paused"), bPaused) || !TestTrue(TEXT("set-up: and resumed"), bResumed))
	{
		return false;
	}
	TestTrue(TEXT("CONTROL: a running game's beat moves"), AfterRunning > AtStart + 0.05f);
	TestEqual(TEXT("a paused game's beat does not move"), AfterPaused, AfterRunning, 0.0001f);
	TestTrue(TEXT("and once the game runs again it moves on"), AfterResumed > AfterPaused + 0.05f);
	return true;
}

/**
 * The player's frame does nothing to the world while the game is paused: a crafting material at the player's feet,
 * which every frame's sweep takes, is left lying by a paused frame and taken by the next one that runs (CONTROL). The
 * frame is called by hand, as the engine calls it, with the game paused and then not; no automation test has a frame of
 * its own. Prints "PAUSED FRAME".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPauseFrameTest,
	"Cataclysm.Pause.APausedFrameTakesNoMaterialAndAnUnpausedOneDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPauseFrameTest::RunTest(const FString&)
{
	using namespace CataclysmPauseTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FPausePlayer Player(World);
	if (!TestTrue(TEXT("set-up: the game's controller possessing a player character"), Player.IsUsable()))
	{
		return false;
	}

	// A CRAFTING MATERIAL A METRE FROM THE PLAYER, well inside the sweep's reach.
	ACataclysmDroppedItem* Material = World->SpawnActor<ACataclysmDroppedItem>(
		Player.Character->GetActorLocation() + FVector(100.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("set-up: a material to sweep"), Material))
	{
		return false;
	}
	Material->Material = FName(TEXT("IronOre"));
	Material->MaterialQuantity = 3;
	Material->MaterialTier = 2;
	Material->DisplayName = TEXT("Iron Ore");

	Player.Controller->PostProcessInput(PauseFrameSeconds, /*bGamePaused=*/true);
	const bool bLyingAfterAPausedFrame = IsValid(Material);
	Player.Controller->PostProcessInput(PauseFrameSeconds, /*bGamePaused=*/false);
	const bool bLyingAfterARunningFrame = IsValid(Material);
	AddInfo(FString::Printf(TEXT("PAUSED FRAME: the material lies after a paused frame %s, after a running frame %s"),
							bLyingAfterAPausedFrame ? TEXT("yes") : TEXT("NO"),
							bLyingAfterARunningFrame ? TEXT("YES") : TEXT("no")));

	TestTrue(TEXT("a paused frame takes no material"), bLyingAfterAPausedFrame);
	TestFalse(TEXT("CONTROL: a running frame takes it"), bLyingAfterARunningFrame);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
