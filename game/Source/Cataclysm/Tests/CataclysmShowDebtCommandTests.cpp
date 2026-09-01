// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmHealthDebt.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Cataclysm.ShowDebt console command. Issue #1100.
 *
 * WHY A CONSOLE COMMAND IS WORTH A TEST WHEN THE OTHER EIGHT HAVE NONE. Two of
 * its failure modes are silent from where this project sits and loud from where
 * the project owner sits. A command registered under a mistyped name simply is
 * not there when they type it, and they find that out by being told
 * "unrecognised command" mid-play; and a command that reads an attribute the
 * character does not carry crashes the editor rather than printing anything.
 * Both are cheap to rule out here and expensive to hit in a play session.
 *
 * WHAT THIS DOES NOT CHECK, said plainly: the wording of any line, or whether
 * the numbers are laid out readably. Those are for a person to look at, and the
 * whole point of the command is that a person looks at it.
 */
namespace CataclysmShowDebtTest
{
	using Resource = UCataclysmClassResourceAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	/**
	 * An output device that keeps what was printed instead of showing it.
	 *
	 * THE COMMAND TAKES AN FOutputDevice AND WRITES NOTHING ELSE, which is what
	 * makes it testable at all: everything it decides comes back through this.
	 */
	struct FCapturedOutput : public FOutputDevice
	{
		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type,
							   const class FName&) override
		{
			Lines.Add(FString(Text));
		}

		/** Everything printed, run together, for a plain substring test. */
		FString All() const
		{
			return FString::Join(Lines, TEXT("\n"));
		}

		TArray<FString> Lines;
	};

	/** A possessed player character that can owe health. */
	struct FScopedDebtor
	{
		explicit FScopedDebtor(UWorld* World)
		{
			State = World->SpawnActor<ACataclysmPlayerState>();
			Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
			if (!State || !Controller || !Character)
			{
				return;
			}

			Controller->SetPlayerState(State);
			Controller->Possess(Character);

			AbilitySystem = Cast<UCataclysmAbilitySystemComponent>(
				Character->GetAbilitySystemComponent());
			if (!AbilitySystem)
			{
				return;
			}

			// MAXIMUM BEFORE CURRENT, because the vital set clamps health to the
			// maximum, so raising the current value first would clamp it back
			// down to the placeholder the set starts at.
			Set(Vital::GetMaxHealthAttribute(), 1'000.0f);
			Set(Vital::GetHealthAttribute(), 1'000.0f);
		}

		bool IsUsable() const { return Character && AbilitySystem; }

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		ACataclysmPlayerState* State = nullptr;
		APlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** The command itself, or null if nothing is registered under that name. */
	IConsoleCommand* TheCommand()
	{
		IConsoleObject* Object =
			IConsoleManager::Get().FindConsoleObject(TEXT("Cataclysm.ShowDebt"));
		return Object ? Object->AsCommand() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShowDebtIsRegisteredTest,
	"Cataclysm.Console.ShowDebtIsRegisteredUnderThatExactName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmShowDebtIsRegisteredTest::RunTest(const FString&)
{
	using namespace CataclysmShowDebtTest;

	// THE NAME IS THE WHOLE INTERFACE. Nothing in the game calls this; a person
	// types it. A rename, a typo or a file dropped from the build all show up
	// the same way to them -- the command is not there -- and only here.
	if (!TestNotNull(TEXT("Cataclysm.ShowDebt is registered"), TheCommand()))
	{
		return false;
	}

	// AND IT SAYS WHAT IT IS FOR, because the console lists the help text and a
	// command with none is one a player cannot discover.
	IConsoleObject* Object =
		IConsoleManager::Get().FindConsoleObject(TEXT("Cataclysm.ShowDebt"));
	TestTrue(TEXT("and it carries help text"),
			 Object && FString(Object->GetHelp()).Len() > 20);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShowDebtPrintsTheDebtTest,
	"Cataclysm.Console.ShowDebtPrintsWhatIsOwedAndWhetherTheRescueIsReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmShowDebtPrintsTheDebtTest::RunTest(const FString&)
{
	using namespace CataclysmShowDebtTest;

	IConsoleCommand* Command = TheCommand();
	if (!TestNotNull(TEXT("the command"), Command))
	{
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// WITH NO CHARACTER AT ALL IT SAYS SO RATHER THAN CRASHING. This is the
	// state the command is in every time it is typed before Play is pressed,
	// which is when a person is most likely to try it.
	{
		FCapturedOutput Empty;
		Command->Execute(TArray<FString>(), World, Empty);
		TestTrue(TEXT("with no character it prints something"),
				 Empty.Lines.Num() > 0);
	}

	const FScopedDebtor Debtor(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"),
				  Debtor.IsUsable()))
	{
		return false;
	}

	// NOTHING OWED FIRST, which is every character in the game that is not a
	// Masochist a few seconds into a fight.
	{
		FCapturedOutput Out;
		Command->Execute(TArray<FString>(), World, Out);
		const FString Text = Out.All();
		TestTrue(TEXT("it says nothing is owed"), Text.Contains(TEXT("Nothing owed")));
		TestTrue(TEXT("and names the health it read"), Text.Contains(TEXT("1000.0")));
	}

	// A QUARTER OF THE POOL OWED, AND NOT YET LETHAL.
	Debtor.Set(Resource::GetHealthOwedAttribute(), 250.0f);
	{
		FCapturedOutput Out;
		Command->Execute(TArray<FString>(), World, Out);
		const FString Text = Out.All();
		TestTrue(TEXT("it prints what is owed"), Text.Contains(TEXT("250.0")));

		// THE SHARE OF MAXIMUM HEALTH IS THE FIGURE THE DAMAGE BONUSES READ, so
		// it is printed rather than left for a player to divide in their head.
		TestTrue(TEXT("and the share of maximum health that is"),
				 Text.Contains(TEXT("25.0%")));
		TestFalse(TEXT("and it is not bleeding"),
				  Text.Contains(TEXT("BLEEDING OUT")));
	}

	// NOW THE RECKONING, AND A DEBT PAST CURRENT HEALTH.
	Debtor.Set(Resource::GetHealthDebtClearedOnlyByAKillAttribute(), 1.0f);
	Debtor.Set(Resource::GetHealthOwedAttribute(), 1'500.0f);
	{
		FCapturedOutput Out;
		Command->Execute(TArray<FString>(), World, Out);
		const FString Text = Out.All();

		TestTrue(TEXT("it says the debt never falls due"),
				 Text.Contains(TEXT("never falls due")));

		// THE STATE A PLAYER MOST NEEDS TO SEE, and the rate is the arithmetic
		// the drain really uses: 1500 across five seconds is 300 a second, and
		// 1000 health at that rate is 3.3 seconds.
		TestTrue(TEXT("it says the character is bleeding out"),
				 Text.Contains(TEXT("BLEEDING OUT")));
		TestTrue(TEXT("at the rate the drain really uses"),
				 Text.Contains(TEXT("300.0")));
		TestTrue(TEXT("and how many seconds of health are left"),
				 Text.Contains(TEXT("3.3")));
	}

	// AND WHETHER THE WAY OUT EXISTS. Without Rock Bottom it says there is none;
	// with it, it says the rescue is ready. A player asking "am I about to die"
	// is really asking this.
	{
		FCapturedOutput Out;
		Command->Execute(TArray<FString>(), World, Out);
		TestTrue(TEXT("without Rock Bottom it says there is no rescue"),
				 Out.All().Contains(TEXT("No low health rescue")));
	}

	Debtor.Set(Resource::GetDebtClearedOnDroppingLowAttribute(), 1.0f);
	{
		FCapturedOutput Out;
		Command->Execute(TArray<FString>(), World, Out);
		const FString Text = Out.All();
		TestTrue(TEXT("with Rock Bottom it says the rescue is ready"),
				 Text.Contains(TEXT("Rock Bottom is ready")));
		TestTrue(TEXT("and names the threshold it fires on"),
				 Text.Contains(TEXT("20%")));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
