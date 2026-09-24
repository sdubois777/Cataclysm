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

// ---------------------------------------------------------------------------
// Carnivore: a hit taken, and a count with no ceiling
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarnivoreFromHitsTest,
	"Cataclysm.Stacks.TakingAHitBuildsCarnageOnlyForCarnivore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCarnivoreFromHitsTest::RunTest(const FString&)
{
	using namespace CataclysmStackTest;
	using Stacks = UCataclysmStacks;

	// THE FIRST CLAUSE OF CARNIVORE. Issue #1071: "Every hit you take grants a
	// stack of Carnage."
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedHolder Holder(World);

	// WITHOUT THE OPTION A HIT BUILDS BLOODLUST AND NOTHING ELSE. This half is
	// what says the new grant is scoped to the option rather than handed to
	// every character in the game.
	Stacks::NoteDamageTaken(Holder.AbilitySystem);
	TestEqual(TEXT("a hit builds a Bloodlust stack for anybody"),
			  Holder.Held(ECataclysmStackKind::Bloodlust), 1);
	TestEqual(TEXT("and no Carnage for a character without Carnivore"),
			  Holder.Held(ECataclysmStackKind::Carnage), 0);

	// AND WITH IT THE SAME EVENT BUILDS BOTH.
	Holder.AbilitySystem->SetNumericAttributeBase(
		Resource::GetCarnageFromDamageTakenAttribute(), 1.0f);

	Stacks::NoteDamageTaken(Holder.AbilitySystem);
	TestEqual(TEXT("with Carnivore the same hit builds Carnage"),
			  Holder.Held(ECataclysmStackKind::Carnage), 1);
	TestEqual(TEXT("and still builds Bloodlust beside it"),
			  Holder.Held(ECataclysmStackKind::Bloodlust), 2);

	// AND THE TWO KEEP THEIR OWN WINDOWS. Bloodlust runs 5 seconds and Carnage
	// 8, so six seconds after the last hit one is gone and the other is not.
	// This is what says they are still two kinds rather than one counted twice.
	World->TimeSeconds += 6.0f;
	TestEqual(TEXT("Bloodlust has run out after six seconds"),
			  Holder.Held(ECataclysmStackKind::Bloodlust), 0);
	TestEqual(TEXT("and Carnage has not"),
			  Holder.Held(ECataclysmStackKind::Carnage), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarnivoreNoMaximumTest,
	"Cataclysm.Stacks.CarnivoreLiftsTheCarnageCapAndNoOtherCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCarnivoreNoMaximumTest::RunTest(const FString&)
{
	using namespace CataclysmStackTest;
	using Stacks = UCataclysmStacks;

	// THE SECOND CLAUSE OF CARNIVORE. Issue #1071: "Carnage has no maximum."
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedHolder Holder(World);

	// THE DESIGN'S NUMBER IS WHAT EVERY CHARACTER GETS.
	TestEqual(TEXT("Carnage caps at ten without the option"),
			  Stacks::CapForOn(Holder.AbilitySystem,
							   ECataclysmStackKind::Carnage), 10);

	Holder.AbilitySystem->SetNumericAttributeBase(
		Resource::GetCarnageHasNoMaximumAttribute(), 1.0f);

	TestEqual(TEXT("and has no maximum with it"),
			  Stacks::CapForOn(Holder.AbilitySystem,
							   ECataclysmStackKind::Carnage),
			  Stacks::NoMaximum);

	// AND IT LIFTS ONLY CARNAGE'S. A flag that raised every cap would pass a
	// test of Carnage alone, and Blood Offering would silently stop capping.
	TestEqual(TEXT("Bloodlust still caps at five"),
			  Stacks::CapForOn(Holder.AbilitySystem,
							   ECataclysmStackKind::Bloodlust), 5);
	TestEqual(TEXT("and Sanguine Momentum at five"),
			  Stacks::CapForOn(Holder.AbilitySystem,
							   ECataclysmStackKind::SanguineMomentum), 5);

	// AND THE COUNT REALLY PASSES TEN IN PLAY. `CapForOn` answering a large
	// number proves nothing on its own: what matters is that a character being
	// hit over and over holds more than the ten `CapFor` allows. Thirty hits,
	// which is well past it and is a number a crowded room reaches.
	Holder.AbilitySystem->SetNumericAttributeBase(
		Resource::GetCarnageFromDamageTakenAttribute(), 1.0f);
	for (int32 Hit = 0; Hit < 30; ++Hit)
	{
		Stacks::NoteDamageTaken(Holder.AbilitySystem);
	}
	TestEqual(TEXT("thirty hits are thirty stacks of Carnage"),
			  Holder.Held(ECataclysmStackKind::Carnage), 30);

	// AND BLOODLUST, EARNED BY THE SAME THIRTY HITS, IS STILL HELD AT FIVE.
	// The two counts are moved by one event, so a cap lifted for both would
	// look exactly like a cap lifted for one here.
	TestEqual(TEXT("and Bloodlust is still capped at five"),
			  Holder.Held(ECataclysmStackKind::Bloodlust), 5);

	// AND THE WINDOW STILL ENDS IT. "No maximum" is not "for ever": thirty
	// stacks lapse eight seconds after the last hit like any others.
	World->TimeSeconds +=
		Stacks::WindowSecondsFor(ECataclysmStackKind::Carnage) + 0.1f;
	TestEqual(TEXT("and they still lapse when the window passes"),
			  Holder.Held(ECataclysmStackKind::Carnage), 0);

	return true;
}

// ---------------------------------------------------------------------------
// Infernal Brand: a debuff that explodes at five and is spent by exploding
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInfernalBrandSpentOnceTest,
	"Cataclysm.Stacks.InfernalBrandExplodesOnceAtFiveAndStartsCountingAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInfernalBrandSpentOnceTest::RunTest(const FString&)
{
	using namespace CataclysmStackTest;
	using Stacks = UCataclysmStacks;

	// ISSUE #1534. The row says "When the brand reaches 5 stacks, it explodes",
	// and the explosion consumes every stack. The line meant to spend them
	// granted a stack with a cap of zero, which `GrantStack` refuses without a
	// word, so the count stayed at five and every later brand exploded again.
	//
	// THE COUNT ON ITS OWN. Whether a real hit reaches it, and whether the
	// explosion's own damage adds a brand back, is
	// `Cataclysm.EnemyModifiers.InfernalBrandExplodesOnceForEveryFiveHitsThatLand`.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedHolder Holder(World);
	constexpr ECataclysmStackKind Kind = ECataclysmStackKind::InfernalBrand;

	// TEN BRANDS INSIDE ONE WINDOW, recording which exploded and what was held
	// after each. Compared as one line rather than asserted one brand at a
	// time, so a failure prints the whole pattern: a count stuck at five and a
	// count that never reaches five fail differently, and the difference is
	// the diagnosis.
	TArray<FString> Exploded;
	TArray<FString> HeldAfter;
	int32 ExplodedInFirstSix = 0;
	for (int32 Brand = 1; Brand <= 10; ++Brand)
	{
		if (Stacks::NoteInfernalBrand(Holder.AbilitySystem))
		{
			Exploded.Add(FString::FromInt(Brand));
			ExplodedInFirstSix += Brand <= 6 ? 1 : 0;
		}
		HeldAfter.Add(FString::FromInt(Holder.Held(Kind)));
	}

	// SIX BRANDS EXPLODE ONCE, which is the check the issue asks for.
	TestEqual(TEXT("six brands explode exactly once"), ExplodedInFirstSix, 1);

	// AND TEN EXPLODE TWICE, ON THE FIFTH AND THE TENTH. The second explosion
	// is what says the count started again rather than stopping for good: a
	// brand that never exploded a second time would pass the check above.
	TestEqual(TEXT("the brands that exploded, of ten"),
			  FString::Join(Exploded, TEXT(" ")), FString(TEXT("5 10")));

	// AND AN EXPLOSION HAS SPENT EVERY STACK BY THE TIME IT IS REPORTED, so the
	// sixth brand holds one. Spent before `true` comes back is the order that
	// matters: the caller deals the explosion's damage next, and that damage
	// arrives at the target as a hit of its own.
	TestEqual(TEXT("the stacks held after each of the ten brands"),
			  FString::Join(HeldAfter, TEXT(" ")),
			  FString(TEXT("1 2 3 4 0 1 2 3 4 0")));

	return true;
}

// ---------------------------------------------------------------------------
// Removing stacks, and the grant that cannot
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClearStacksTest,
	"Cataclysm.Stacks.ClearingAKindEmptiesItAndNoOtherKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClearStacksTest::RunTest(const FString&)
{
	using namespace CataclysmStackTest;
	using Stacks = UCataclysmStacks;

	// ISSUE #1534. Until `ClearStacks` existed nothing could remove a stack: a
	// count left only when its window ran out, and the one caller that had to
	// spend one wrote a grant that did nothing instead.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedHolder Holder(World);
	constexpr ECataclysmStackKind Cleared = ECataclysmStackKind::Carnage;
	constexpr ECataclysmStackKind Kept = ECataclysmStackKind::Bloodlust;

	for (int32 Grant = 0; Grant < 3; ++Grant)
	{
		Holder.AbilitySystem->GrantStack(Cleared,
										 Stacks::WindowSecondsFor(Cleared),
										 Stacks::CapFor(Cleared));
	}
	for (int32 Grant = 0; Grant < 2; ++Grant)
	{
		Holder.AbilitySystem->GrantStack(Kept, Stacks::WindowSecondsFor(Kept),
										 Stacks::CapFor(Kept));
	}

	// HELD BEFORE THE CLEAR, so the checks after it cannot pass on grants that
	// never landed.
	TestEqual(TEXT("three Carnage stacks before the clear"),
			  Holder.Held(Cleared), 3);
	TestEqual(TEXT("and two Bloodlust"), Holder.Held(Kept), 2);

	Holder.AbilitySystem->ClearStacks(Cleared);

	TestEqual(TEXT("clearing Carnage leaves none"), Holder.Held(Cleared), 0);

	// AND ONLY THAT KIND. A clear that emptied every kind would pass the check
	// above.
	TestEqual(TEXT("and leaves Bloodlust as it was"), Holder.Held(Kept), 2);

	// AND THE NEXT GRANT STARTS AGAIN AT ONE, not at four.
	Holder.AbilitySystem->GrantStack(Cleared, Stacks::WindowSecondsFor(Cleared),
									 Stacks::CapFor(Cleared));
	TestEqual(TEXT("a grant after a clear holds one"), Holder.Held(Cleared), 1);

	// CLEARING A KIND THAT HOLDS NOTHING, OR ONE OUT OF RANGE, TOUCHES NOTHING
	// ELSE.
	Holder.AbilitySystem->ClearStacks(ECataclysmStackKind::SanguineMomentum);
	Holder.AbilitySystem->ClearStacks(ECataclysmStackKind::Count);
	TestEqual(TEXT("clearing other kinds left Carnage alone"),
			  Holder.Held(Cleared), 1);
	TestEqual(TEXT("and Bloodlust too"), Holder.Held(Kept), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmZeroCapGrantTest,
	"Cataclysm.Stacks.AGrantWithACapOfZeroChangesNothingAndSaysSo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmZeroCapGrantTest::RunTest(const FString&)
{
	using namespace CataclysmStackTest;
	using Stacks = UCataclysmStacks;

	// ISSUE #1534 WAS A CALLER READING A CAP OF ZERO AS "CLEAR" AND `GrantStack`
	// READING IT AS "GRANT NOTHING", with nothing to say that the two
	// disagreed. The refusal stays. What this pins is that a zero cap neither
	// clears nor grants, and that the refusal is no longer silent.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedHolder Holder(World);
	constexpr ECataclysmStackKind Kind = ECataclysmStackKind::Bloodlust;
	const float Window = Stacks::WindowSecondsFor(Kind);

	Holder.AbilitySystem->GrantStack(Kind, Window, Stacks::CapFor(Kind));
	Holder.AbilitySystem->GrantStack(Kind, Window, Stacks::CapFor(Kind));
	TestEqual(TEXT("two stacks to start with"), Holder.Held(Kind), 2);

	// ONE WARNING FOR EACH OF THE TWO REFUSALS BELOW, AND EXACTLY THAT MANY.
	// This test fails if either warning is missing.
	AddExpectedError(TEXT("which grants nothing and removes nothing"),
		EAutomationExpectedErrorFlags::Contains, 2);

	Holder.AbilitySystem->GrantStack(Kind, Window, /*Cap=*/0);
	TestEqual(TEXT("a cap of zero does not clear them"), Holder.Held(Kind), 2);

	Holder.AbilitySystem->GrantStack(Kind, Window, /*Cap=*/-1);
	TestEqual(TEXT("and nor does a negative one"), Holder.Held(Kind), 2);

	return true;
}

// ---------------------------------------------------------------------------
// The three numbers every kind carries
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStackKindTableTest,
	"Cataclysm.Stacks.EveryKindsWindowCapAndNameAreTheDesignsOwnNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStackKindTableTest::RunTest(const FString&)
{
	using Stacks = UCataclysmStacks;

	// WHY THIS EXISTS ALONGSIDE THE TESTS ABOVE, WHICH LOOK LIKE THEY COVER IT.
	// They pin the three Masochist kinds' windows and caps as literals, and they
	// pin Infernal Brand's cap through the pattern its explosions make. What
	// none of them pins is Infernal Brand's WINDOW, any kind's NAME, or the
	// answer for a kind the build does not know. Issue #1720 proposes replacing
	// all three switches with a table read from data, and this is the reading
	// such a table would have to reproduce exactly.
	//
	// EVERY OTHER TEST IN THIS FILE READS THESE NUMBERS BACK OUT OF THE SAME
	// SWITCHES IT IS CHECKING whenever it drives a grant -- `GrantStack(Kind,
	// WindowSecondsFor(Kind), CapFor(Kind))`. A switch that answered differently
	// would move both sides of those checks at once and they would still pass.
	// This asserts the numbers against nothing but themselves written down.
	//
	// NO WORLD AND NO CHARACTER, which is the point the class's own header
	// makes about why it is static functions: "the rules are arithmetic on a few
	// numbers, so they can be checked by passing numbers in rather than by
	// building a character, a world and an effect spec for every case."

	// ONE LINE PER COLUMN RATHER THAN TWELVE SEPARATE ASSERTIONS, so a failure
	// prints the whole table and a single changed number can be read off
	// against its neighbours. The same shape the Infernal Brand test above uses
	// and for the same reason.
	TArray<FString> Windows;
	TArray<FString> Caps;
	TArray<FString> Names;
	for (int32 Index = 0; Index < Stacks::KindCount; ++Index)
	{
		const ECataclysmStackKind Kind = static_cast<ECataclysmStackKind>(Index);

		// TWO DECIMAL PLACES, NOT ZERO. Printed with `%.0f` a window that moved
		// from 8 to 8.4 would still read as "8" and this test would pass.
		Windows.Add(
			FString::Printf(TEXT("%.2f"), Stacks::WindowSecondsFor(Kind)));
		Caps.Add(FString::FromInt(Stacks::CapFor(Kind)));
		Names.Add(Stacks::NameOf(Kind));
	}

	// IN ENUM ORDER: Sanguine Momentum, Bloodlust, Carnage, Infernal Brand,
	// Feast.
	//
	// FEAST WAS APPENDED THE DAY AFTER THIS TEST LANDED AND THIS TEST IS WHAT
	// REPORTED IT. That is the whole point of writing the numbers down: a
	// fifth kind cannot arrive without somebody editing these three lines and
	// saying what it answers. Its 5 and 5 are the `Buff_Feasting` row's own
	// words -- "up to 5 stacks" and "A stack lasts 5 seconds".
	// Three of these four windows are their node's own words; Infernal Brand's
	// eight is a judgement recorded at `CataclysmStacks.cpp:24`, and it is the
	// one a table would be likeliest to drop, because no design document states
	// it.
	TestEqual(TEXT("every kind's window in seconds, in enum order"),
			  FString::Join(Windows, TEXT(" ")),
			  FString(TEXT("3.00 5.00 8.00 8.00 5.00")));

	TestEqual(TEXT("every kind's cap, in enum order"),
			  FString::Join(Caps, TEXT(" ")), FString(TEXT("5 5 10 5 5")));

	// THE NAMES ARE ASSERTED BY NOTHING ELSE IN THE PROJECT. `NameOf` has one
	// caller, the `Cataclysm.ShowStacks` console command, and a console command
	// has no test -- so a case dropped from that switch would return
	// "(unknown)" in play and fail nowhere.
	TestEqual(TEXT("every kind's name, in enum order"),
			  FString::Join(Names, TEXT(", ")),
			  FString(TEXT(
				  "Sanguine Momentum, Bloodlust, Carnage, Infernal Brand, "
				  "Feast")));

	// AND THE ARM THAT NO TEST REACHED, WHICH IS THE ONE A TABLE CHANGES. Both
	// switches answer nothing for a kind they do not know and both say why in
	// as many words: a window of nothing makes `Held` answer zero at every
	// instant, and a cap of nothing grants nothing "rather than growing without
	// bound". A table read from data has a miss for the same reason a switch has
	// a default, so the answer to a miss is the part that must not move.
	TestEqual(TEXT("a kind this build does not know lasts no time"),
			  Stacks::WindowSecondsFor(ECataclysmStackKind::Count), 0.0f,
			  0.001f);
	TestEqual(TEXT("and holds nothing"),
			  Stacks::CapFor(ECataclysmStackKind::Count), 0);
	TestEqual(TEXT("and prints as unknown rather than as one of the four"),
			  FString(Stacks::NameOf(ECataclysmStackKind::Count)),
			  FString(TEXT("(unknown)")));

	return true;
}

// ---------------------------------------------------------------------------
// Spending one stack rather than all of them
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpendOneStackTest,
	"Cataclysm.Stacks.SpendingTakesOneAndLeavesTheExpiryWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSpendOneStackTest::RunTest(const FString&)
{
	using namespace CataclysmStackTest;
	using Stacks = UCataclysmStacks;

	// ISSUE #1720. Until this, `ClearStacks` was the only way to remove stacks
	// and it removed all of them. One row in `game/Data/StatusEffects.csv`
	// wants the other half -- Touch of Nothing consumes ONE stack to negate a
	// buff -- and a decrement with no floor is the defect this shape usually
	// ships with.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedHolder Holder(World);
	constexpr ECataclysmStackKind Kind = ECataclysmStackKind::Carnage;
	const float Window = Stacks::WindowSecondsFor(Kind);
	const int32 Cap = Stacks::CapFor(Kind);

	// NO CHARACTER SPENDS NOTHING, the same refusal every function here makes.
	TestFalse(TEXT("no ability system spends nothing"),
			  Stacks::Spend(nullptr, Kind));

	// AND NEITHER DOES A CHARACTER HOLDING NONE. Checked BEFORE anything is
	// granted, because a floor tested only after counting down cannot tell a
	// refusal from a subtraction that happened to land on zero.
	TestFalse(TEXT("a character holding nothing spends nothing"),
			  Stacks::Spend(Holder.AbilitySystem, Kind));
	TestEqual(TEXT("and still holds nothing"), Holder.Held(Kind), 0);

	// THREE, THEN TWO. One stack goes and the rest stay, which is the whole
	// difference from `ClearStacks`.
	for (int32 Grant = 0; Grant < 3; ++Grant)
	{
		Holder.AbilitySystem->GrantStack(Kind, Window, Cap);
	}
	TestEqual(TEXT("three grants are three stacks"), Holder.Held(Kind), 3);

	TestTrue(TEXT("spending one says it took one"),
			 Stacks::Spend(Holder.AbilitySystem, Kind));
	TestEqual(TEXT("three less one is two, not none"), Holder.Held(Kind), 2);

	// AND THE FLOOR. Counted down to nothing and then asked again: the answer
	// is a refusal and the count stays at zero rather than going to minus one.
	TestTrue(TEXT("a second spend takes another"),
			 Stacks::Spend(Holder.AbilitySystem, Kind));
	TestTrue(TEXT("and the third empties it"),
			 Stacks::Spend(Holder.AbilitySystem, Kind));
	TestEqual(TEXT("spent down to nothing"), Holder.Held(Kind), 0);

	TestFalse(TEXT("spending from nothing takes nothing"),
			  Stacks::Spend(Holder.AbilitySystem, Kind));
	TestEqual(TEXT("and the count does not go below zero"),
			  Holder.Held(Kind), 0);

	// ONE KIND AT A TIME. Spending Carnage must not move Bloodlust, which is
	// the failure a single shared counter would produce.
	Holder.AbilitySystem->GrantStack(Kind, Window, Cap);
	Holder.AbilitySystem->GrantStack(Kind, Window, Cap);
	Holder.AbilitySystem->GrantStack(
		ECataclysmStackKind::Bloodlust,
		Stacks::WindowSecondsFor(ECataclysmStackKind::Bloodlust),
		Stacks::CapFor(ECataclysmStackKind::Bloodlust));

	TestTrue(TEXT("spending Carnage takes a Carnage stack"),
			 Stacks::Spend(Holder.AbilitySystem, Kind));
	TestEqual(TEXT("Carnage went down by one"), Holder.Held(Kind), 1);
	TestEqual(TEXT("and Bloodlust did not move"),
			  Holder.Held(ECataclysmStackKind::Bloodlust), 1);

	// A COUNT THAT HAS ALREADY LAPSED HOLDS NOTHING TO SPEND. Without this a
	// spend would take one off a stored number nobody can still see, and the
	// next grant -- which restarts a lapsed count at one -- would disagree with
	// it.
	World->TimeSeconds += Window + 0.1f;
	TestEqual(TEXT("the stacks lapsed"), Holder.Held(Kind), 0);
	TestFalse(TEXT("and a lapsed count has nothing to spend"),
			  Stacks::Spend(Holder.AbilitySystem, Kind));

	// AND THE DECISION THIS TEST EXISTS FOR: SPENDING DOES NOT MOVE THE EXPIRY.
	//
	// Gaining a stack refreshes the whole lot, which is the rule `GrantStack`
	// follows and which this project read off Path of Exile's charges. Spending
	// is not gaining. If spending refreshed the expiry, a debuff could be held
	// open indefinitely by the very thing that is supposed to be using it up.
	//
	// THE CHECK IS BUILT SO THAT THE WRONG ANSWER SURVIVES IT VISIBLY: the
	// spend happens just INSIDE the window, and the clock is then moved just
	// PAST the original expiry. A version that refreshed would still be holding
	// two here rather than none.
	for (int32 Grant = 0; Grant < 3; ++Grant)
	{
		Holder.AbilitySystem->GrantStack(Kind, Window, Cap);
	}
	TestEqual(TEXT("three again, on a fresh window"), Holder.Held(Kind), 3);

	World->TimeSeconds += Window - 0.1f;
	TestTrue(TEXT("spending just inside the window still takes one"),
			 Stacks::Spend(Holder.AbilitySystem, Kind));
	TestEqual(TEXT("two are left"), Holder.Held(Kind), 2);

	World->TimeSeconds += 0.2f;
	TestEqual(
		TEXT("and they lapse on the original expiry rather than a refreshed one"),
		Holder.Held(Kind), 0);

	return true;
}

namespace CataclysmOwnStackTest
{
	/** A row's own stack granted on a critical strike, 5 seconds, up to 5. */
	FCataclysmPoolAction StackOnCrit(FName Key)
	{
		FCataclysmPoolAction Stack;
		Stack.Event = FName(TEXT("critical_strike"));
		Stack.StackKey = Key;
		Stack.StackSeconds = 5.0f;
		Stack.StackCap = 5;
		return Stack;
	}

	/** A row's increase of 10 per stack of its own, on armour. */
	FCataclysmStatModifier TenPerStack(FName Key)
	{
		FCataclysmStatModifier Per;
		Per.Bucket = ECataclysmStatBucket::Increased;
		Per.Source = ECataclysmModifierSource::Enchantment;
		Per.Value = 10.0f;
		Per.Scale = ECataclysmStatScale::PerOwnStack;
		Per.ScaleStep = 1.0f;
		Per.ScaleMaxSteps = 5;
		Per.StackKey = Key;
		return Per;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOwnStacksBuildAndLapseTest,
	"Cataclysm.Stacks.ARowsOwnStacksBuildToTheCapAndLapseTogetherAfterTheWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A row's own stacks, issue #1833. The same shape as the five kinds: each grant
 * restarts the window, the count stops at the cap, and the whole count lapses
 * together once the window passes with no grant. Two rows keep separate counts.
 */
bool FCataclysmOwnStacksBuildAndLapseTest::RunTest(const FString&)
{
	using namespace CataclysmStackTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedHolder Holder(World);
	UCataclysmAbilitySystemComponent* ASC = Holder.AbilitySystem;
	const FName Row(TEXT("A_row:armor"));
	const FName Other(TEXT("Another_row:armor"));

	TestEqual(TEXT("nothing granted, nothing held"), ASC->OwnStacksHeld(Row), 0);
	ASC->GrantOwnStack(Row, 5.0f, 2);
	ASC->GrantOwnStack(Row, 5.0f, 2);
	ASC->GrantOwnStack(Row, 5.0f, 2);
	TestEqual(TEXT("three grants at a cap of two hold two"), ASC->OwnStacksHeld(Row), 2);
	TestEqual(TEXT("and another row holds none of them"), ASC->OwnStacksHeld(Other), 0);

	CataclysmTestWorld::RunClock(World, 3.0f);
	ASC->GrantOwnStack(Row, 5.0f, 2);
	CataclysmTestWorld::RunClock(World, 3.0f);
	TestEqual(TEXT("a grant restarts the window: six seconds in, three since the last"),
		ASC->OwnStacksHeld(Row), 2);
	CataclysmTestWorld::RunClock(World, 3.0f);
	TestEqual(TEXT("and once it passes with no grant, the whole count lapses"),
		ASC->OwnStacksHeld(Row), 0);

	ASC->GrantOwnStack(Other, 5.0f, 0);
	TestEqual(TEXT("a cap of nothing grants nothing"), ASC->OwnStacksHeld(Other), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOwnStacksFromEventsTest,
	"Cataclysm.Stacks.AnEventGrantsARowsOwnStackOncePerEventOnlyWhenItLanded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The row's event grants its stack. Issue #1833, ruled 2026-09-23:
 *   - only its own event grants one, not another;
 *   - only an event that LANDED grants one;
 *   - TWO WORN COPIES of the row grant ONE stack per event, because they share
 *     the count and would otherwise double the rate the sentence states;
 *   - a death ends them, as it ends the five kinds.
 */
bool FCataclysmOwnStacksFromEventsTest::RunTest(const FString&)
{
	using namespace CataclysmStackTest;
	using namespace CataclysmOwnStackTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedHolder Holder(World);
	UCataclysmAbilitySystemComponent* ASC = Holder.AbilitySystem;
	const FName Row(TEXT("A_row:armor"));
	ASC->SetPoolActions({StackOnCrit(Row), StackOnCrit(Row)});

	ASC->ActOnEvent(FName(TEXT("critical_strike")));
	TestEqual(TEXT("two copies, one critical strike: one stack"), ASC->OwnStacksHeld(Row), 1);
	ASC->ActOnEvent(FName(TEXT("kill")));
	TestEqual(TEXT("another event grants none"), ASC->OwnStacksHeld(Row), 1);
	ASC->ActOnEvent(FName(TEXT("critical_strike")), nullptr, 0.0f, /*bLanded=*/false);
	TestEqual(TEXT("an event that did not land grants none"), ASC->OwnStacksHeld(Row), 1);
	ASC->ActOnEvent(FName(TEXT("critical_strike")));
	TestEqual(TEXT("and the next that did grants the second"), ASC->OwnStacksHeld(Row), 2);

	ASC->ClearWhatDeathEnds();
	TestEqual(TEXT("a death ends them"), ASC->OwnStacksHeld(Row), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOwnStacksScaleEachCopyTest,
	"Cataclysm.Stacks.TwoCopiesOfAStackRowShareOneCountAndEachIsScaledByIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two copies of one stack row, as a drawback worn on two pieces makes. Issue
 * #1833, the case the coordinating session asked to be pinned: they SHARE ONE
 * COUNT, keyed by the enchantment and the stat, and EACH COPY'S VALUE IS SCALED
 * BY IT. So two copies at two stacks are 2 x 10 x 2 = 40% increased, double one
 * copy's 20%. A benefit on two pieces never makes two copies: it is granted
 * once, at the higher roll (`UCataclysmItemModifiers::AccumulateEnchantmentsInto`).
 */
bool FCataclysmOwnStacksScaleEachCopyTest::RunTest(const FString&)
{
	using namespace CataclysmStackTest;
	using namespace CataclysmOwnStackTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FName Row(TEXT("A_row:armor"));
	const FName Armour(TEXT("armor"));
	const auto ArmourAtTwoStacks = [&](int32 Copies)
	{
		FScopedHolder Holder(World);
		UCataclysmAbilitySystemComponent* ASC = Holder.AbilitySystem;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(Armour);
		Line.Base = 100.0f;
		TArray<FCataclysmPoolAction> Stacks;
		for (int32 Copy = 0; Copy < Copies; ++Copy)
		{
			Line.Modifiers.Add(TenPerStack(Row));
			Stacks.Add(StackOnCrit(Row));
		}
		ASC->SetStatInputs(MoveTemp(Inputs));
		ASC->SetPoolActions(MoveTemp(Stacks));

		const float None = ASC->StatForSkill(Armour, FGameplayTagContainer(), 0.0f);
		ASC->ActOnEvent(FName(TEXT("critical_strike")));
		ASC->ActOnEvent(FName(TEXT("critical_strike")));
		TestEqual(FString::Printf(TEXT("%d copies: two strikes hold two stacks"), Copies),
			ASC->OwnStacksHeld(Row), 2);
		TestEqual(FString::Printf(TEXT("%d copies: no stacks, no increase"), Copies),
			None, 100.0f, 0.01f);
		return ASC->StatForSkill(Armour, FGameplayTagContainer(), 0.0f);
	};

	TestEqual(TEXT("one copy at two stacks: 20% increased"), ArmourAtTwoStacks(1), 120.0f, 0.01f);
	TestEqual(TEXT("two copies at two stacks: 40%, double one copy"), ArmourAtTwoStacks(2), 140.0f, 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
