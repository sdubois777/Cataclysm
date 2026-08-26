// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmStacks.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"

/**
 * A count that builds on an event and stops counting when the character goes
 * long enough without it. Issues #1002, #1003 and #1004.
 *
 * WHAT IS HERE AND WHAT IS NOT. This file covers the count itself and the two
 * triggers that have nowhere else to live: the chain rule Sanguine Momentum
 * needs and the kill rule Carnage needs. The third trigger, taking damage, is
 * covered in `Cataclysm.ConditionalDamage.TakingDamageBuildsABloodlustStack`,
 * beside the other rule that reads a resolved hit -- the two sit in the same
 * branch of the vital attribute set and the interesting case is the one that
 * tells them apart.
 */
namespace CataclysmStackTest
{
	using Resource = UCataclysmClassResourceAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	/** A character that can hold stacks: an ability system on a plain actor. */
	struct FScopedHolder
	{
		FScopedHolder(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template
			// and a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource =
				NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewResource);

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
		}

		~FScopedHolder()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		int32 Held(ECataclysmStackKind Kind) const
		{
			return UCataclysmStacks::Held(AbilitySystem, Kind);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

// ---------------------------------------------------------------------------
// The count itself
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStackCountBuildsAndExpiresTest,
	"Cataclysm.Stacks.ACountBuildsCapsAndStopsCountingWhenItsWindowPasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStackCountBuildsAndExpiresTest::RunTest(const FString&)
{
	using namespace CataclysmStackTest;

	// THE MECHANIC ALL THREE NODES SHARE, on its own. Issues #1002 to #1004.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedHolder Holder(World);

	constexpr ECataclysmStackKind Kind = ECataclysmStackKind::Carnage;
	const float Window = UCataclysmStacks::WindowSecondsFor(Kind);
	const int32 Cap = UCataclysmStacks::CapFor(Kind);

	// A CHARACTER STARTS WITH NOTHING. Without this the checks below would pass
	// just as well if a character were born holding a full set.
	TestEqual(TEXT("a character that has earned nothing holds nothing"),
			  Holder.Held(Kind), 0);

	// AND EACH GRANT IS ONE MORE.
	Holder.AbilitySystem->GrantStack(Kind, Window, Cap);
	TestEqual(TEXT("one grant is one stack"), Holder.Held(Kind), 1);

	Holder.AbilitySystem->GrantStack(Kind, Window, Cap);
	Holder.AbilitySystem->GrantStack(Kind, Window, Cap);
	TestEqual(TEXT("three grants are three stacks"), Holder.Held(Kind), 3);

	// AND THE CAP HOLDS. Ten is Carnage's own number and this pushes well past
	// it, because a cap that only bites at exactly the limit is a cap nobody has
	// tested.
	for (int32 Extra = 0; Extra < 20; ++Extra)
	{
		Holder.AbilitySystem->GrantStack(Kind, Window, Cap);
	}
	TestEqual(TEXT("twenty-three grants are capped at ten"), Holder.Held(Kind),
			  Cap);

	// THE WINDOW IS MEASURED FROM THE LAST GRANT, NOT THE FIRST, which is what
	// "gaining one refreshes them all" means. Just under eight seconds after the
	// last grant they are all still there, even though the first was granted far
	// earlier.
	World->TimeSeconds += Window - 0.1f;
	TestEqual(TEXT("just inside the window they are all still held"),
			  Holder.Held(Kind), Cap);

	// AND PAST IT THEY ARE ALL GONE AT ONCE, with nothing having run. No timer
	// fired, no step ran; the clock moved and the answer changed.
	World->TimeSeconds += 0.2f;
	TestEqual(TEXT("past the window they are gone"), Holder.Held(Kind), 0);

	// AND A LAPSED COUNT RESTARTS AT ONE rather than continuing from ten. A
	// character that let its stacks run out and then earned another has one.
	Holder.AbilitySystem->GrantStack(Kind, Window, Cap);
	TestEqual(TEXT("a grant after they lapsed starts again at one"),
			  Holder.Held(Kind), 1);

	// AND THE THREE KINDS ARE COUNTED APART. Granting one must not move another,
	// which is the failure a single shared counter would produce and which no
	// test of one kind alone could see.
	TestEqual(TEXT("granting Carnage did not grant Bloodlust"),
			  Holder.Held(ECataclysmStackKind::Bloodlust), 0);
	TestEqual(TEXT("nor Sanguine Momentum"),
			  Holder.Held(ECataclysmStackKind::SanguineMomentum), 0);

	// AND THE THREE WINDOWS AND CAPS ARE THE THREE NODES' OWN NUMBERS.
	TestEqual(TEXT("Sanguine Momentum lasts 3 seconds"),
			  UCataclysmStacks::WindowSecondsFor(
				  ECataclysmStackKind::SanguineMomentum), 3.0f, 0.001f);
	TestEqual(TEXT("Bloodlust lasts 5"),
			  UCataclysmStacks::WindowSecondsFor(
				  ECataclysmStackKind::Bloodlust), 5.0f, 0.001f);
	TestEqual(TEXT("Carnage lasts 8"), Window, 8.0f, 0.001f);
	TestEqual(TEXT("and Carnage is the only one that reaches ten"), Cap, 10);
	TestEqual(TEXT("Sanguine Momentum caps at 5"),
			  UCataclysmStacks::CapFor(ECataclysmStackKind::SanguineMomentum), 5);
	TestEqual(TEXT("Bloodlust caps at 5"),
			  UCataclysmStacks::CapFor(ECataclysmStackKind::Bloodlust), 5);

	return true;
}

// ---------------------------------------------------------------------------
// Sanguine Momentum: a health cost soon after the last
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMomentumChainTest,
	"Cataclysm.Stacks.AHealthCostSoonAfterTheLastBuildsAStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMomentumChainTest::RunTest(const FString&)
{
	using namespace CataclysmStackTest;
	using Stacks = UCataclysmStacks;

	// THE MASOCHIST'S SANGUINE MOMENTUM NODE: "Each health cost paid within 3
	// seconds of the last grants a stack, up to 5 stacks." Issue #1002.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedHolder Holder(World);
	constexpr ECataclysmStackKind Kind = ECataclysmStackKind::SanguineMomentum;

	// THE FIRST COST OF A FIGHT IS NOT WITHIN THREE SECONDS OF ANYTHING, so it
	// grants nothing. This is the half of the sentence that is easiest to drop,
	// and a version granting on every payment would pass every other assertion
	// in this test.
	TestFalse(TEXT("the first health cost grants no stack"),
			  Stacks::NoteHealthCostPaid(Holder.AbilitySystem));
	TestEqual(TEXT("and nothing is held"), Holder.Held(Kind), 0);

	// The cost is recorded, the way `PayHealthCost` records it.
	Holder.AbilitySystem->NoteHealthCostPaid();

	// A SECOND COST TWO SECONDS LATER IS WITHIN THE CHAIN.
	World->TimeSeconds += 2.0f;
	TestTrue(TEXT("a cost two seconds after the last grants one"),
			 Stacks::NoteHealthCostPaid(Holder.AbilitySystem));
	TestEqual(TEXT("and one is held"), Holder.Held(Kind), 1);
	Holder.AbilitySystem->NoteHealthCostPaid();

	// AND A THIRD KEEPS THE CHAIN GOING.
	World->TimeSeconds += 1.0f;
	TestTrue(TEXT("a third keeps it going"),
			 Stacks::NoteHealthCostPaid(Holder.AbilitySystem));
	TestEqual(TEXT("and two are held"), Holder.Held(Kind), 2);
	Holder.AbilitySystem->NoteHealthCostPaid();

	// A GAP LONGER THAN THREE SECONDS BREAKS IT. The payment still happens; it
	// simply grants nothing, and the stacks it would have added to have expired
	// on the same clock.
	World->TimeSeconds += 3.5f;
	TestFalse(TEXT("a cost more than three seconds later grants nothing"),
			  Stacks::NoteHealthCostPaid(Holder.AbilitySystem));
	TestEqual(TEXT("and the old stacks are gone too"), Holder.Held(Kind), 0);
	Holder.AbilitySystem->NoteHealthCostPaid();

	// AND THE CHAIN CAN START AGAIN.
	World->TimeSeconds += 1.0f;
	TestTrue(TEXT("the next cost inside the window starts a new chain"),
			 Stacks::NoteHealthCostPaid(Holder.AbilitySystem));
	TestEqual(TEXT("holding one again"), Holder.Held(Kind), 1);

	return true;
}

// ---------------------------------------------------------------------------
// Carnage: a kill while holding enough of the class resource
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarnageThresholdTest,
	"Cataclysm.Stacks.AKillBuildsAStackOnlyAboveTheFervourThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCarnageThresholdTest::RunTest(const FString&)
{
	using namespace CataclysmStackTest;
	using Stacks = UCataclysmStacks;

	// THE MASOCHIST'S CARNAGE KEYSTONE: "Killing an enemy while above 75 Fervour
	// grants a stack of Carnage for 8 seconds, up to 10 stacks." Issue #1004.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedHolder Holder(World);
	constexpr ECataclysmStackKind Kind = ECataclysmStackKind::Carnage;

	// AN EMPTY BAR GRANTS NOTHING, which is where every character starts.
	Holder.AbilitySystem->SetNumericAttributeBase(
		Resource::GetClassResourceAttribute(), 0.0f);
	TestFalse(TEXT("a kill on an empty bar grants nothing"),
			  Stacks::NoteEnemyKilled(Holder.Actor));
	TestEqual(TEXT("and nothing is held"), Holder.Held(Kind), 0);

	// AND NEITHER DOES EXACTLY THE THRESHOLD, because the design writes "above
	// 75". The boundary is reachable rather than pedantic: a Fervour bar runs
	// 0 to 100 and lands on round numbers often.
	Holder.AbilitySystem->SetNumericAttributeBase(
		Resource::GetClassResourceAttribute(),
		Stacks::CarnageClassResourceAbove);
	TestFalse(TEXT("a kill at exactly 75 grants nothing"),
			  Stacks::NoteEnemyKilled(Holder.Actor));
	TestEqual(TEXT("and still nothing is held"), Holder.Held(Kind), 0);

	// ONE POINT MORE AND IT DOES. Nothing else changed.
	Holder.AbilitySystem->SetNumericAttributeBase(
		Resource::GetClassResourceAttribute(),
		Stacks::CarnageClassResourceAbove + 1.0f);
	TestTrue(TEXT("a kill just above 75 grants one"),
			 Stacks::NoteEnemyKilled(Holder.Actor));
	TestEqual(TEXT("and one is held"), Holder.Held(Kind), 1);

	// AND A SECOND KILL IS A SECOND STACK.
	TestTrue(TEXT("a second kill grants a second"),
			 Stacks::NoteEnemyKilled(Holder.Actor));
	TestEqual(TEXT("holding two"), Holder.Held(Kind), 2);

	// AND THEY GO EIGHT SECONDS AFTER THE LAST KILL.
	World->TimeSeconds += Stacks::WindowSecondsFor(Kind) + 0.1f;
	TestEqual(TEXT("eight seconds after the last kill they are gone"),
			  Holder.Held(Kind), 0);

	// AND A CHARACTER WITH NO CLASS RESOURCE AT ALL GAINS NOTHING, which is
	// every enemy in the game. An enemy killing another creature must not build
	// a keystone's stacks.
	AActor* Bare = World->SpawnActor<AActor>();
	if (TestNotNull(TEXT("an actor with no ability system"), Bare))
	{
		TestFalse(TEXT("something with no ability system gains nothing"),
				  Stacks::NoteEnemyKilled(Bare));
		Bare->Destroy();
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarnageRealKillTest,
	"Cataclysm.Stacks.KillingAnEnemyIsWhatBuildsACarnageStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCarnageRealKillTest::RunTest(const FString&)
{
	using namespace CataclysmStackTest;

	// THE TEST ABOVE PROVES THE RULE AND SAYS NOTHING ABOUT WHETHER A KILL RUNS
	// IT. Without this one the whole grant could be deleted from
	// `ACataclysmEnemyCharacter::HandleDeath` and every other test here would
	// still pass. It is the same separation
	// `Cataclysm.EnemyScore.KillingAnEnemyIsWhatGrantsTheExperience` exists for,
	// and the third thing that now happens on that same death.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Player =
		World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player state"), PlayerState)
		|| !TestNotNull(TEXT("player controller"), Controller)
		|| !TestNotNull(TEXT("player character"), Player))
	{
		return false;
	}

	Controller->SetPlayerState(PlayerState);
	Controller->Possess(Player);

	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(
			Player->GetAbilitySystemComponent());
	if (!TestNotNull(TEXT("the player's ability system"), AbilitySystem))
	{
		return false;
	}

	// A FULL BAR, so the threshold is not what this test is measuring.
	AbilitySystem->SetNumericAttributeBase(
		Resource::GetClassResourceAttribute(), 100.0f);

	TestEqual(TEXT("no stacks before anything dies"),
			  UCataclysmStacks::Held(AbilitySystem,
									 ECataclysmStackKind::Carnage), 0);

	ACataclysmEnemyCharacter* Victim =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature to kill"), Victim))
	{
		return false;
	}

	Victim->HandleDeath();

	TestEqual(TEXT("killing it built a Carnage stack"),
			  UCataclysmStacks::Held(AbilitySystem,
									 ECataclysmStackKind::Carnage), 1);

	// AND A KILL BELOW THE THRESHOLD BUILDS NOTHING, through the same real
	// death. Without this the hook could ignore the Fervour test entirely and
	// the assertion above would still pass.
	AbilitySystem->SetNumericAttributeBase(
		Resource::GetClassResourceAttribute(), 10.0f);

	ACataclysmEnemyCharacter* Second =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(600.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a second creature"), Second))
	{
		return false;
	}

	Second->HandleDeath();

	TestEqual(TEXT("a kill on a near-empty bar built nothing more"),
			  UCataclysmStacks::Held(AbilitySystem,
									 ECataclysmStackKind::Carnage), 1);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
