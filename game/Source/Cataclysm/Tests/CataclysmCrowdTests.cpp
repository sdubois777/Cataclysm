// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmMinion.h"
#include "AI/NavigationSystemBase.h"
#include "AIController.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmCorruptedSentinelCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmGatekeeperCharacter.h"
#include "Character/CataclysmHellhoundCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmSuccubusCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/CrowdManager.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/UnrealType.h"

/**
 * Every creature walks as a member of a Detour crowd. Issue #761.
 *
 * WHAT WAS WRONG. A pack shoved into one blob instead of ringing the player:
 * `ACataclysmEnemyController` used the default path-following component, so each
 * creature walked its own path and only capsule collision kept bodies apart. The
 * design names the answer in the Imp's "What the engine has to do": "Unreal's
 * Detour Crowd is the one to use rather than the older RVO avoidance on the
 * character movement component", and "The crowd agent radius is the body radius
 * above."
 *
 * WHAT THESE CHECK, ALL OF IT CONFIGURATION. Every creature's brain walks through
 * a crowd-following component; none of them turns on RVO avoidance beside it;
 * each one's crowd radius is its own capsule's; the crowd holds twice the largest
 * floor; separation stays off; a walk at another crowd member is not steered
 * around it while every other walk is; a world with no navigation system leaves
 * path following exactly as it was; and a creature the crowd does not take in
 * walks outside it rather than not at all. `Cataclysm.FloorCrowd.`, in the editor
 * module, checks sixty Imps are each held by the crowd on a real navigation mesh.
 *
 * WHAT THESE DO NOT CHECK, AND WHY. That ten creatures pathing to one point end
 * without overlapping and ring it. That needs the world ticked -- movement, the
 * crowd manager and the brains' timers together -- for seconds of game time, and
 * no test in this project ticks a whole world. The outcome would also depend on
 * frame timing. A test that could be flaky was not written; the owner watching a
 * pack of ten Imps close on the player is the check (`docs/DECISIONS.md`).
 */

namespace CataclysmCrowdTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/**
	 * The ruled crowd size and the measurement it was set against.
	 *
	 * COPIES, HELD TO THEIR SOURCES BY `tools/tests/test_the_crowd_holds_every_creature_a_floor_can.py`:
	 * `MaxAgents` in `game/Config/DefaultEngine.ini`, and the largest count in the measured table in
	 * `game/Source/Cataclysm/Dungeon/CataclysmFloorPopulation.h` (Caverns, 84 to 510 creatures).
	 */
	constexpr int32 RuledMaxAgents = 1024;
	constexpr int32 LargestMeasuredFloor = 510;

	/** One creature class the brain drives, and what a message calls it. */
	struct FCrowdTestKind
	{
		const TCHAR* Name;
		UClass* Class;
	};

	/** One spawned body, and what a message calls it. */
	struct FCrowdTestBody
	{
		FString Name;
		ACataclysmCharacterBase* Body = nullptr;
	};

	/**
	 * The creatures a player meets, one of each. The Corrupted Stalker is left out because it is raised from an
	 * authored entry rather than spawned bare; its class sets no controller and no movement setting of its own, so
	 * it walks through the same brain as the base enemy here.
	 */
	static TArray<FCrowdTestKind> Roster()
	{
		return {
			{TEXT("the base enemy"), ACataclysmEnemyCharacter::StaticClass()},
			{TEXT("the Brute"), ACataclysmBruteCharacter::StaticClass()},
			{TEXT("the Imp"), ACataclysmImpCharacter::StaticClass()},
			{TEXT("the Hellhound"), ACataclysmHellhoundCharacter::StaticClass()},
			{TEXT("the Abyssal Warden"), ACataclysmAbyssalWardenCharacter::StaticClass()},
			{TEXT("the Corrupted Sentinel"), ACataclysmCorruptedSentinelCharacter::StaticClass()},
			{TEXT("the Gatekeeper"), ACataclysmGatekeeperCharacter::StaticClass()},
			{TEXT("the Succubus"), ACataclysmSuccubusCharacter::StaticClass()},
		};
	}

	static ACataclysmEnemyCharacter* SpawnKind(UWorld* World, UClass* Class, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACataclysmEnemyCharacter>(Class, Where, FRotator::ZeroRotator, Params);
	}

	/**
	 * Every creature on the roster, five metres apart along X, and an imp a creature summoned, at the end of the row.
	 * Nine bodies when everything spawned; a body that did not spawn is left out, and the callers check the count.
	 */
	static TArray<FCrowdTestBody> SpawnEveryKind(UWorld* World)
	{
		TArray<FCrowdTestBody> Bodies;
		int32 Index = 0;
		ACataclysmEnemyCharacter* Summoner = nullptr;
		for (const FCrowdTestKind& Kind : Roster())
		{
			ACataclysmEnemyCharacter* Made = SpawnKind(World, Kind.Class, FVector(5.0f * M * Index, 0.0f, 0.0f));
			++Index;
			if (Made)
			{
				Bodies.Add(FCrowdTestBody{Kind.Name, Made});
				Summoner = Summoner ? Summoner : Made;
			}
		}

		// A SUMMONED IMP, which is an `ACataclysmMinion` and not an enemy, and is driven by the same brain: its class
		// sets `AIControllerClass` to `ACataclysmEnemyController` in its own constructor.
		if (Summoner)
		{
			if (ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Summoner, FVector(5.0f * M * Index, 0.0f, 0.0f),
																/*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("Imp")))
			{
				Bodies.Add(FCrowdTestBody{TEXT("a summoned imp"), Imp});
			}
		}
		return Bodies;
	}

	/** The crowd-following component that walks this pawn, or null when its brain walks it any other way. */
	static UCrowdFollowingComponent* CrowdOf(const APawn* Pawn)
	{
		const AAIController* Brain = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
		return Brain ? Cast<UCrowdFollowingComponent>(Brain->GetPathFollowingComponent()) : nullptr;
	}

	/** How many of these bodies walk through a crowd-following component. */
	static int32 CountCrowdAgents(const TArray<FCrowdTestBody>& Bodies)
	{
		int32 Agents = 0;
		for (const FCrowdTestBody& Each : Bodies)
		{
			Agents += CrowdOf(Each.Body) ? 1 : 0;
		}
		return Agents;
	}

	/** The nine bodies `SpawnEveryKind` makes: eight kinds and the summoned imp. */
	constexpr int32 EveryKind = 9;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCrowdEveryCreatureIsAnAgentTest,
	"Cataclysm.Crowd.EveryCreatureIsDrivenAsACrowdAgent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCrowdEveryCreatureIsAnAgentTest::RunTest(const FString&)
{
	using namespace CataclysmCrowdTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const TArray<FCrowdTestBody> Bodies = SpawnEveryKind(World);
	if (!TestEqual(TEXT("set-up: eight kinds of creature and a summoned imp spawned"), Bodies.Num(), EveryKind))
	{
		return false;
	}

	// EACH BY NAME, so a creature that left the crowd says which it is.
	for (const FCrowdTestBody& Each : Bodies)
	{
		TestTrue(FString::Printf(TEXT("%s is driven by the creatures' brain"), *Each.Name),
				 Cast<ACataclysmEnemyController>(Each.Body->GetController()) != nullptr);
		TestTrue(FString::Printf(TEXT("%s walks through a crowd-following component"), *Each.Name),
				 CrowdOf(Each.Body) != nullptr);
	}

	TestEqual(TEXT("every one of the nine walks as a crowd agent"), CountCrowdAgents(Bodies), EveryKind);

	// AND A BRAIN MADE ON ITS OWN, so the answer is the controller's and not something a creature's class added.
	const ACataclysmEnemyController* Bare = World->SpawnActor<ACataclysmEnemyController>();
	TestTrue(TEXT("a creature brain made with no body walks through a crowd-following component"),
			 Bare && Cast<UCrowdFollowingComponent>(Bare->GetPathFollowingComponent()) != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCrowdNoRvoTest,
	"Cataclysm.Crowd.NoCreatureAvoidsByRvoBesideTheCrowd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * RVO avoidance stays off on every creature. Ruled 2026-11-02 by the coordinating session, from the design:
 * "running both at once causes the agents to jitter".
 *
 * WHY A GUARD AND NOT A DEFAULT LEFT ALONE. Issue #761 says it: turning RVO on "looks like the obvious fix for exactly
 * the symptom this is meant to cure". Unreal's character movement component defaults it to false; nothing in this
 * project sets it. This is what notices the first thing that does.
 */
bool FCataclysmCrowdNoRvoTest::RunTest(const FString&)
{
	using namespace CataclysmCrowdTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const TArray<FCrowdTestBody> Bodies = SpawnEveryKind(World);
	if (!TestEqual(TEXT("set-up: eight kinds of creature and a summoned imp spawned"), Bodies.Num(), EveryKind))
	{
		return false;
	}

	int32 UsingRvo = 0;
	for (const FCrowdTestBody& Each : Bodies)
	{
		const UCharacterMovementComponent* Movement = Each.Body->GetCharacterMovement();
		const bool bRvo = Movement && Movement->bUseRVOAvoidance;
		TestFalse(FString::Printf(TEXT("%s does not use RVO avoidance"), *Each.Name), bRvo);
		UsingRvo += bRvo ? 1 : 0;
	}
	TestEqual(TEXT("no creature uses RVO avoidance beside the crowd"), UsingRvo, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCrowdRadiusIsTheCapsuleTest,
	"Cataclysm.Crowd.EachCreaturesCrowdRadiusIsItsOwnCapsule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "The crowd agent radius is the body radius above." Ruled 2026-11-02: each creature's agent radius is its own capsule
 * radius.
 *
 * NOTHING IN THIS PROJECT SETS IT, AND THAT IS THE POINT BEING CHECKED. `UCrowdManager::GetAgentParams` takes the
 * radius from `ICrowdAgentInterface::GetCrowdAgentCollisions`, and `UCrowdFollowingComponent`'s answer is
 * `CalcBoundingCylinder` of the movement component's updated component -- the capsule -- which for an upright capsule
 * is its scaled radius (Runtime/AIModule/Private/Navigation/CrowdManager.cpp and CrowdFollowingComponent.cpp, engine
 * 5.8). So a rarer, bigger creature's crowd radius grows with it, and this asks the same function the crowd asks.
 */
bool FCataclysmCrowdRadiusIsTheCapsuleTest::RunTest(const FString&)
{
	using namespace CataclysmCrowdTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const TArray<FCrowdTestBody> Bodies = SpawnEveryKind(World);
	if (!TestEqual(TEXT("set-up: eight kinds of creature and a summoned imp spawned"), Bodies.Num(), EveryKind)
		|| !TestEqual(TEXT("set-up: every creature walks through a crowd-following component"),
					  CountCrowdAgents(Bodies), EveryKind))
	{
		return false;
	}

	for (const FCrowdTestBody& Each : Bodies)
	{
		float Radius = 0.0f;
		float HalfHeight = 0.0f;
		CrowdOf(Each.Body)->GetCrowdAgentCollisions(Radius, HalfHeight);
		TestEqual(FString::Printf(TEXT("%s's crowd radius is its capsule's"), *Each.Name), Radius,
				  Each.Body->GetCapsuleComponent()->GetScaledCapsuleRadius(), 0.01f);
	}

	// THE ISSUE'S OWN CASE, BY THE FIGURE: the Imp's body is the one measured radius in the roster, 30 cm, and the
	// pack's two rings are built on it. 48 is the default the other creatures carry.
	const FCrowdTestBody* Imp = Bodies.FindByPredicate(
		[](const FCrowdTestBody& Each) { return Each.Name == TEXT("the Imp"); });
	if (TestNotNull(TEXT("set-up: the Imp is among them"), Imp))
	{
		float Radius = 0.0f;
		float HalfHeight = 0.0f;
		CrowdOf(Imp->Body)->GetCrowdAgentCollisions(Radius, HalfHeight);
		TestEqual(TEXT("the Imp's crowd radius is its designed 30 cm body"), Radius,
				  ACataclysmImpCharacter::ImpCapsuleRadius, 0.01f);
	}

	// AND A BODY MADE BIGGER, as a rarer creature is: `SetRarityStep` scales the actor, not the capsule.
	const FCrowdTestBody* Brute = Bodies.FindByPredicate(
		[](const FCrowdTestBody& Each) { return Each.Name == TEXT("the Brute"); });
	if (TestNotNull(TEXT("set-up: the Brute is among them"), Brute))
	{
		Brute->Body->SetActorScale3D(FVector(2.0f));
		float Radius = 0.0f;
		float HalfHeight = 0.0f;
		CrowdOf(Brute->Body)->GetCrowdAgentCollisions(Radius, HalfHeight);
		TestEqual(TEXT("a Brute twice the size has a crowd radius of twice its 48 cm"), Radius, 96.0f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCrowdMaxAgentsTest,
	"Cataclysm.Crowd.TheCrowdHoldsTwiceTheLargestFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The crowd's `MaxAgents`, as the engine read it from `game/Config/DefaultEngine.ini`. Ruled 2026-11-02.
 *
 * WHY IT MATTERS. The engine's default is 50, and a creature the crowd has no room for is not simulated by it.
 * `CataclysmFloorPopulation.h` measured floors of up to 510 creatures.
 *
 * READ THROUGH REFLECTION, BECAUSE THE PROPERTY IS PROTECTED. The class default object is what every crowd manager a
 * navigation system creates copies its configured values from. `Cataclysm.FloorCrowd.` reads the live one.
 */
bool FCataclysmCrowdMaxAgentsTest::RunTest(const FString&)
{
	using namespace CataclysmCrowdTest;

	const FIntProperty* Property = FindFProperty<FIntProperty>(UCrowdManager::StaticClass(), TEXT("MaxAgents"));
	if (!TestNotNull(TEXT("set-up: the crowd manager has a whole-number property called MaxAgents"), Property))
	{
		return false;
	}
	const int32 MaxAgents = Property->GetPropertyValue_InContainer(GetDefault<UCrowdManager>());

	TestEqual(TEXT("the crowd holds the ruled 1024 creatures"), MaxAgents, RuledMaxAgents);
	TestTrue(FString::Printf(TEXT("which is at least twice the largest floor measured, %d creatures; it is %d"),
							 LargestMeasuredFloor, MaxAgents),
			 MaxAgents >= 2 * LargestMeasuredFloor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCrowdNoSeparationTest,
	"Cataclysm.Crowd.NoCreatureIsPushedApartByCrowdSeparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Crowd separation stays off. Issue #761's interaction review, recorded in `docs/DECISIONS.md`.
 *
 * WHY. Separation pushes an agent away from every neighbour within its collision query range, 400 cm by default, at
 * a weight that falls off with distance (`dtCrowd::updateStepSteering`). It is off by default in
 * `UCrowdFollowingComponent`'s constructor, and on it would hold a pack apart -- metres apart, not the
 * shoulder-to-shoulder rings the design counts -- and keep a melee creature off a target ringed by others.
 */
bool FCataclysmCrowdNoSeparationTest::RunTest(const FString&)
{
	using namespace CataclysmCrowdTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const TArray<FCrowdTestBody> Bodies = SpawnEveryKind(World);
	if (!TestEqual(TEXT("set-up: eight kinds of creature and a summoned imp spawned"), Bodies.Num(), EveryKind)
		|| !TestEqual(TEXT("set-up: every creature walks through a crowd-following component"),
					  CountCrowdAgents(Bodies), EveryKind))
	{
		return false;
	}

	for (const FCrowdTestBody& Each : Bodies)
	{
		TestFalse(FString::Printf(TEXT("%s is not pushed apart from its neighbours by separation"), *Each.Name),
				  CrowdOf(Each.Body)->IsCrowdSeparationEnabled());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCrowdWalkAtACreatureTest,
	"Cataclysm.Crowd.AWalkAtAnotherCreatureIsNotSteeredAroundIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A walk at another crowd member turns crowd steering off; every other walk turns it on. Issue #761.
 *
 * WHY. Detour's obstacle avoidance scores each velocity by its time to impact with every neighbouring agent, the one
 * being chased included, so a creature walking at another slows as the gap closes and can stop short of its own
 * reach. `ACataclysmEnemyController::MoveTo` says what is done about it and why only there.
 *
 * READ FROM THE FLAG, BECAUSE NOTHING MOVES. A world built for a test has no navigation system, so every walk fails
 * to find a path and nothing is ticked; the flag is set before the walk is asked for, which is what can be read.
 */
bool FCataclysmCrowdWalkAtACreatureTest::RunTest(const FString&)
{
	using namespace CataclysmCrowdTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Walker = SpawnKind(World, ACataclysmBruteCharacter::StaticClass(), FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature =
		SpawnKind(World, ACataclysmImpCharacter::StaticClass(), FVector(5.0f * M, 0.0f, 0.0f));

	// A BODY WALKED BY A BRAIN THAT IS NOT A CROWD AGENT, which is the player's case: the player controller's path
	// following is the engine's default class. A plain character given the engine's own AI controller is the same.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* NotInTheCrowd =
		World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FVector(-5.0f * M, 0.0f, 0.0f), FRotator::ZeroRotator,
									  Params);
	if (NotInTheCrowd)
	{
		NotInTheCrowd->AIControllerClass = AAIController::StaticClass();
		NotInTheCrowd->SpawnDefaultController();
	}
	const AActor* NotAPawn = World->SpawnActor<AActor>();

	ACataclysmEnemyController* Brain = Walker ? Cast<ACataclysmEnemyController>(Walker->GetController()) : nullptr;
	UCrowdFollowingComponent* Crowd = CrowdOf(Walker);
	if (!TestNotNull(TEXT("set-up: a Brute with its brain"), Brain)
		|| !TestNotNull(TEXT("set-up: an Imp"), Creature)
		|| !TestTrue(TEXT("set-up: a character with the engine's own AI controller"),
					 NotInTheCrowd && Cast<AAIController>(NotInTheCrowd->GetController()) != nullptr)
		|| !TestNotNull(TEXT("set-up: an actor that is not a pawn"), NotAPawn)
		|| !TestNotNull(TEXT("set-up: the Brute walks through a crowd-following component"), Crowd))
	{
		return false;
	}

	// WHO IS A MEMBER OF THE CROWD.
	TestTrue(TEXT("another creature is driven by a crowd agent"),
			 ACataclysmEnemyController::IsDrivenByACrowdAgent(Creature));
	TestFalse(TEXT("a character the engine's own AI controller walks is not"),
			  ACataclysmEnemyController::IsDrivenByACrowdAgent(NotInTheCrowd));
	TestFalse(TEXT("an actor that is not a pawn is not"), ACataclysmEnemyController::IsDrivenByACrowdAgent(NotAPawn));
	TestFalse(TEXT("nothing is not"), ACataclysmEnemyController::IsDrivenByACrowdAgent(nullptr));

	// AND WHAT EACH WALK DOES TO THE STEERING, IN AN ORDER THAT TURNS IT BOTH WAYS.
	TestFalse(TEXT("set-up: a Brute that has walked nowhere is steered"), Crowd->IsCrowdSimulatioSuspended());

	Brain->MoveToActor(Creature);
	TestTrue(TEXT("a walk at another creature is not steered around it"), Crowd->IsCrowdSimulatioSuspended());

	Brain->MoveToLocation(FVector(0.0f, 10.0f * M, 0.0f));
	TestFalse(TEXT("a walk to a point is steered again"), Crowd->IsCrowdSimulatioSuspended());

	Brain->MoveToActor(Creature);
	TestTrue(TEXT("CONTROL: and a second walk at the creature is not steered again"),
			 Crowd->IsCrowdSimulatioSuspended());

	Brain->MoveToActor(NotInTheCrowd);
	TestFalse(TEXT("a walk at a body that is not in the crowd, as the player is not, is steered"),
			  Crowd->IsCrowdSimulatioSuspended());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCrowdNoNavigationTest,
	"Cataclysm.Crowd.WithoutANavigationSystemPathFollowingIsAsBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A world with no navigation system leaves path following exactly as it was before crowds.
 *
 * WHY THIS IS WORTH A TEST. Almost every automation test runs in a world from `UWorld::CreateWorld` of the Game type,
 * which creates no navigation system, and hundreds of them drive a creature's brain. `UCrowdFollowingComponent::
 * Initialize` finds no crowd manager there and sets its simulation state to `Disabled`, after which every override it
 * has hands straight to `UPathFollowingComponent`. This is what says so.
 */
bool FCataclysmCrowdNoNavigationTest::RunTest(const FString&)
{
	using namespace CataclysmCrowdTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	TestNull(TEXT("set-up: a test world has no crowd manager"), UCrowdManager::GetCurrent(World));

	ACataclysmEnemyCharacter* Creature =
		SpawnKind(World, ACataclysmEnemyCharacter::StaticClass(), FVector::ZeroVector);
	const UCrowdFollowingComponent* Crowd = CrowdOf(Creature);
	if (!TestNotNull(TEXT("set-up: the creature walks through a crowd-following component"), Crowd))
	{
		return false;
	}
	TestFalse(TEXT("with no crowd to join, its crowd simulation is off, so it follows paths as before"),
			  Crowd->IsCrowdSimulationEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCrowdNotTakenInTest,
	"Cataclysm.Crowd.ACreatureTheCrowdCannotHoldWalksOutsideIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A creature registered with a crowd manager that gives it no slot is taken back out of crowd mode. Issue #761.
 *
 * WHY. In crowd mode only the crowd moves a body, and the manager moves only agents its Detour crowd holds. A
 * navigation system with no navigation mesh -- a broken level -- is one way to be refused; a full crowd is the other.
 * Left in, the creature would never take a step, and the chase's straight-line fallback for a level with no mesh,
 * which `ACataclysmEnemyController::Think` keeps on purpose, would do nothing.
 *
 * A NAVIGATION SYSTEM AND NO MESH IS WHAT THIS BUILDS: one added to a test world with no bounds volume, so it builds
 * no navigation data and its crowd manager has nothing to simulate on.
 */
bool FCataclysmCrowdNotTakenInTest::RunTest(const FString&)
{
	using namespace CataclysmCrowdTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::GameMode,
												  /*NavigationSystemConfig=*/nullptr,
												  /*bInitializeForWorld=*/true,
												  /*bOverridePreviousNavSys=*/true);
	const UCrowdManager* Manager = UCrowdManager::GetCurrent(World);
	if (!TestNotNull(TEXT("set-up: the navigation system made a crowd manager"), Manager)
		|| !TestNull(TEXT("set-up: and it has no navigation mesh to simulate on"), Manager->GetNavData()))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Creature =
		SpawnKind(World, ACataclysmEnemyCharacter::StaticClass(), FVector::ZeroVector);
	const UCrowdFollowingComponent* Crowd = CrowdOf(Creature);
	if (!TestNotNull(TEXT("set-up: the creature walks through a crowd-following component"), Crowd))
	{
		return false;
	}
	TestFalse(TEXT("set-up: the crowd gave it no slot"), Manager->IsAgentValid(Crowd));
	TestFalse(TEXT("so it is taken out of crowd mode and walks as it did before crowds"),
			  Crowd->IsCrowdSimulationEnabled());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
