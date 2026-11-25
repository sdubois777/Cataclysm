// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Which bodies block one another. Issue #2861, the owner's play-test of 2026-11-23: "He can't walk through my
 * minions, and neither can I, which is annoying. There shouldn't be collision between player and minions."
 *
 * WHAT THESE GUARD. The player and what the player commands pass through each other, both ways; a creature still
 * blocks each of them and each of them still blocks a creature; two commanded bodies still block each other, a
 * judgement; and a thrall's body follows its side, when it is taken and when it is given back.
 * `ACataclysmCharacterBase::RefreshPassThroughBody` says how. Demonic Guide's guide, the third body, is in
 * `CataclysmDungeonModifierEffectsTests.cpp`, where a guide floor is built.
 *
 * EACH BODY IS SWEPT ONTO ANOTHER, as a step of its movement sweeps it: `AActor::SetActorLocation` with a sweep moves
 * the capsule through `UPrimitiveComponent::MoveComponent`, which reads the same mask filter
 * (`UPrimitiveComponent::InitSweepCollisionParams`) as the character movement component's own moves. An automation
 * test's world never ticks, so a walk cannot be watched; the sweep it would make can.
 *
 * EVERY BODY STANDS 5 M UP IN AN EMPTY WORLD, 3 M FROM THE ONE IT IS SWEPT ONTO, so nothing else is in the way.
 */
namespace CataclysmPassThroughBodiesTest
{
	/** Where every body here stands: 5 m above the origin of a world with nothing else in it. */
	const FVector PassThroughOrigin(0.0f, 0.0f, 500.0f);

	/**
	 * The player, possessed, with its player state: the body `ACataclysmPlayerCharacter` gives the player, and a
	 * commander with an ability system for `UCataclysmCommand::Subjugate` to read.
	 */
	struct FPassThroughBodiesPlayer
	{
		explicit FPassThroughBodiesPlayer(UWorld* World)
		{
			PlayerState = World->SpawnActor<ACataclysmPlayerState>();
			Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(PassThroughOrigin, FRotator::ZeroRotator);
			if (!PlayerState || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(PlayerState);
			Controller->Possess(Character);
		}

		ACataclysmPlayerState* PlayerState = nullptr;
		APlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
	};

	/** An ordinary Imp, a creature of the monsters' side, standing at `Where`. */
	ACataclysmEnemyCharacter* PassThroughImpAt(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmEnemyCharacter* Imp = World->SpawnActor<ACataclysmImpCharacter>(
			ACataclysmImpCharacter::StaticClass(), Where, FRotator::ZeroRotator, Spawn);
		if (Imp)
		{
			Imp->SetActorLocation(Where);
		}
		return Imp;
	}

	/** A minion of `Summoner`'s standing at `Where`, made as a summoning skill makes one. */
	ACataclysmMinion* PassThroughMinionAt(AActor* Summoner, const FVector& Where)
	{
		ACataclysmMinion* Minion = ACataclysmMinion::Spawn(Summoner, Where, /*Lifetime=*/60.0f, /*bBurns=*/false);
		if (Minion)
		{
			Minion->SetActorLocation(Where);
		}
		return Minion;
	}

	/**
	 * `Mover` put at `From` and swept to `To`, and what stopped it, or null; then put back at `From`, so the next sweep
	 * finds every body where it was. `bOutArrived` says whether it reached `To`.
	 */
	AActor* PassThroughSweep(AActor* Mover, const FVector& From, const FVector& To, bool& bOutArrived)
	{
		Mover->SetActorLocation(From);
		FHitResult Hit;
		Mover->SetActorLocation(To, /*bSweep=*/true, &Hit);
		bOutArrived = FVector::Dist(Mover->GetActorLocation(), To) < 1.0;
		AActor* Met = Hit.bBlockingHit ? Hit.GetActor() : nullptr;
		Mover->SetActorLocation(From);
		return Met;
	}

	/** A capsule's mask filter and its move-ignore mask, as "filter/ignores". */
	FString PassThroughMasksOf(const ACataclysmCharacterBase* Character)
	{
		const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
		return Capsule
			? FString::Printf(TEXT("%d/%d"), static_cast<int32>(Capsule->BodyInstance.GetMaskFilter()),
							  static_cast<int32>(Capsule->GetMoveIgnoreMask()))
			: FString(TEXT("no capsule"));
	}

	FString PassThroughNameOf(const AActor* Actor)
	{
		return Actor ? Actor->GetName() : FString(TEXT("nothing"));
	}
}

// EACH BODY CARRIES ITS BIT AND MOVES THROUGH THE OTHER TWO: the player 1 and through 6, a minion 2 and through 5, a
// creature nought and through nothing. Prints "BODY MASKS".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassThroughMasksTest,
	"Cataclysm.PassThroughBodies.EachBodyCarriesItsBitAndMovesThroughTheOtherTwo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassThroughMasksTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPassThroughBodiesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FPassThroughBodiesPlayer Player(World);
	if (!TestNotNull(TEXT("set-up: a player"), Player.Character))
	{
		return false;
	}
	ACataclysmMinion* Minion = PassThroughMinionAt(Player.Character, PassThroughOrigin + FVector(300.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Creature = PassThroughImpAt(World, PassThroughOrigin + FVector(0.0f, 300.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a minion"), Minion) || !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("BODY MASKS: the player %s, the minion %s, the creature %s"),
							*PassThroughMasksOf(Player.Character), *PassThroughMasksOf(Minion),
							*PassThroughMasksOf(Creature)));

	using Base = ACataclysmCharacterBase;
	TestEqual(TEXT("the three bits are one, two and four"), static_cast<int32>(Base::PassThroughBodyMaskBits), 7);
	TestEqual(TEXT("the player is the player's body"),
			  static_cast<int32>(Player.Character->PassThroughBodyMaskBit()), static_cast<int32>(Base::PlayerBodyMaskBit));
	TestEqual(TEXT("a minion is a commanded body"),
			  static_cast<int32>(Minion->PassThroughBodyMaskBit()), static_cast<int32>(Base::CommandedBodyMaskBit));
	TestEqual(TEXT("a creature is no such body"), static_cast<int32>(Creature->PassThroughBodyMaskBit()), 0);
	TestEqual(TEXT("the player's capsule carries it"), PassThroughMasksOf(Player.Character), FString(TEXT("1/6")));
	TestEqual(TEXT("the minion's capsule carries it"), PassThroughMasksOf(Minion), FString(TEXT("2/5")));
	TestEqual(TEXT("the creature's capsule carries nothing and ignores nothing"), PassThroughMasksOf(Creature),
			  FString(TEXT("0/0")));
	TestEqual(TEXT("a body of no bit moves through nothing"), static_cast<int32>(Base::MovesThroughMaskFor(0)), 0);
	return true;
}

// THE PLAYER AND ITS MINION WALK THROUGH EACH OTHER, BOTH WAYS. CONTROL: the same sweep onto a creature is stopped by
// it, so the sweep is one that stops at a body. Prints "PLAYER AND MINION".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassThroughPlayerAndMinionTest,
	"Cataclysm.PassThroughBodies.ThePlayerAndItsMinionWalkThroughEachOther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassThroughPlayerAndMinionTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPassThroughBodiesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FPassThroughBodiesPlayer Player(World);
	if (!TestNotNull(TEXT("set-up: a player"), Player.Character))
	{
		return false;
	}
	const FVector PlayerAt = PassThroughOrigin;
	const FVector MinionAt = PassThroughOrigin + FVector(300.0f, 0.0f, 0.0f);
	const FVector CreatureAt = PassThroughOrigin + FVector(-300.0f, 0.0f, 0.0f);
	ACataclysmMinion* Minion = PassThroughMinionAt(Player.Character, MinionAt);
	ACataclysmEnemyCharacter* Creature = PassThroughImpAt(World, CreatureAt);
	if (!TestNotNull(TEXT("set-up: a minion"), Minion) || !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	Player.Character->SetActorLocation(PlayerAt);

	bool bMinionArrived = false;
	bool bPlayerArrived = false;
	bool bPlayerReachedTheCreature = false;
	const AActor* MinionMet = PassThroughSweep(Minion, MinionAt, PlayerAt, bMinionArrived);
	const AActor* PlayerMet = PassThroughSweep(Player.Character, PlayerAt, MinionAt, bPlayerArrived);
	const AActor* PlayerMetTheCreature = PassThroughSweep(Player.Character, PlayerAt, CreatureAt, bPlayerReachedTheCreature);

	AddInfo(FString::Printf(TEXT("PLAYER AND MINION: the minion met %s, the player met %s; CONTROL: the player going to "
								 "the creature met %s"),
							*PassThroughNameOf(MinionMet), *PassThroughNameOf(PlayerMet),
							*PassThroughNameOf(PlayerMetTheCreature)));

	TestTrue(TEXT("the minion walks through the player"), MinionMet == nullptr && bMinionArrived);
	TestTrue(TEXT("and the player through the minion"), PlayerMet == nullptr && bPlayerArrived);
	TestTrue(TEXT("CONTROL: the player is stopped by a creature"),
			 PlayerMetTheCreature == Creature && !bPlayerReachedTheCreature);
	return true;
}

// A CREATURE STILL BLOCKS THE PLAYER'S MINION AND THE PLAYER, AND IS STILL BLOCKED BY THEM: "Enemies still collide with
// minions and the player as before" (issue #2861). Prints "CREATURE BLOCKS".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassThroughCreatureBlocksTest,
	"Cataclysm.PassThroughBodies.ACreatureStillBlocksTheMinionAndThePlayerAndTheyBlockIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassThroughCreatureBlocksTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPassThroughBodiesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FPassThroughBodiesPlayer Player(World);
	if (!TestNotNull(TEXT("set-up: a player"), Player.Character))
	{
		return false;
	}
	const FVector CreatureAt = PassThroughOrigin;
	const FVector MinionAt = PassThroughOrigin + FVector(300.0f, 0.0f, 0.0f);
	const FVector PlayerAt = PassThroughOrigin + FVector(-300.0f, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* Creature = PassThroughImpAt(World, CreatureAt);
	ACataclysmMinion* Minion = PassThroughMinionAt(Player.Character, MinionAt);
	if (!TestNotNull(TEXT("set-up: a minion"), Minion) || !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	Player.Character->SetActorLocation(PlayerAt);

	bool bMinionArrived = true;
	bool bPlayerArrived = true;
	bool bCreatureReachedTheMinion = true;
	bool bCreatureReachedThePlayer = true;
	const AActor* MinionMet = PassThroughSweep(Minion, MinionAt, CreatureAt, bMinionArrived);
	const AActor* PlayerMet = PassThroughSweep(Player.Character, PlayerAt, CreatureAt, bPlayerArrived);
	const AActor* CreatureMetTheMinion = PassThroughSweep(Creature, CreatureAt, MinionAt, bCreatureReachedTheMinion);
	const AActor* CreatureMetThePlayer = PassThroughSweep(Creature, CreatureAt, PlayerAt, bCreatureReachedThePlayer);

	AddInfo(FString::Printf(TEXT("CREATURE BLOCKS: the minion met %s, the player %s; the creature met %s going to the "
								 "minion and %s going to the player"),
							*PassThroughNameOf(MinionMet), *PassThroughNameOf(PlayerMet),
							*PassThroughNameOf(CreatureMetTheMinion), *PassThroughNameOf(CreatureMetThePlayer)));

	TestTrue(TEXT("a creature stops the minion"), MinionMet == Creature && !bMinionArrived);
	TestTrue(TEXT("and the player"), PlayerMet == Creature && !bPlayerArrived);
	TestTrue(TEXT("the minion stops a creature"), CreatureMetTheMinion == Minion && !bCreatureReachedTheMinion);
	TestTrue(TEXT("and so does the player"), CreatureMetThePlayer == Player.Character && !bCreatureReachedThePlayer);
	return true;
}

// TWO OF THE PLAYER'S MINIONS STILL BLOCK EACH OTHER, a judgement of issue #2861: the owner asked for the player and
// the guide, and a commanded body keeps the other commanded bodies' block it had. Prints "TWO MINIONS".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassThroughTwoMinionsTest,
	"Cataclysm.PassThroughBodies.TwoMinionsStillBlockEachOther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassThroughTwoMinionsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPassThroughBodiesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FPassThroughBodiesPlayer Player(World);
	if (!TestNotNull(TEXT("set-up: a player"), Player.Character))
	{
		return false;
	}
	Player.Character->SetActorLocation(PassThroughOrigin + FVector(0.0f, -600.0f, 0.0f));
	const FVector FirstAt = PassThroughOrigin;
	const FVector SecondAt = PassThroughOrigin + FVector(300.0f, 0.0f, 0.0f);
	ACataclysmMinion* First = PassThroughMinionAt(Player.Character, FirstAt);
	ACataclysmMinion* Second = PassThroughMinionAt(Player.Character, SecondAt);
	if (!TestNotNull(TEXT("set-up: a minion"), First) || !TestNotNull(TEXT("set-up: another"), Second))
	{
		return false;
	}

	bool bArrived = true;
	const AActor* Met = PassThroughSweep(First, FirstAt, SecondAt, bArrived);
	AddInfo(FString::Printf(TEXT("TWO MINIONS: the first met %s"), *PassThroughNameOf(Met)));

	TestTrue(TEXT("one minion stops another"), Met == Second && !bArrived);
	return true;
}

// A THRALL'S BODY FOLLOWS ITS SIDE: an Imp blocks the player; taken by the player, it and the player walk through each
// other; given back to the monsters' side, it blocks the player again. "A thrall changes sides when taken, so its
// collision must change with it" (issue #2861). Prints "THRALL BODY".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassThroughThrallTest,
	"Cataclysm.PassThroughBodies.AThrallsBodyFollowsItsSideWhenTakenAndWhenGivenBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassThroughThrallTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPassThroughBodiesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FPassThroughBodiesPlayer Player(World);
	if (!TestNotNull(TEXT("set-up: a player"), Player.Character))
	{
		return false;
	}
	const FVector PlayerAt = PassThroughOrigin;
	const FVector ImpAt = PassThroughOrigin + FVector(300.0f, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* Imp = PassThroughImpAt(World, ImpAt);
	if (!TestNotNull(TEXT("set-up: an Imp"), Imp))
	{
		return false;
	}
	Player.Character->SetActorLocation(PlayerAt);

	bool bBeforeArrived = true;
	const AActor* BeforeMet = PassThroughSweep(Player.Character, PlayerAt, ImpAt, bBeforeArrived);

	const bool bTaken = UCataclysmCommand::Subjugate(Player.Character, Imp);
	const FString TakenMasks = PassThroughMasksOf(Imp);
	bool bPlayerArrived = false;
	bool bThrallArrived = false;
	const AActor* PlayerMetTheThrall = PassThroughSweep(Player.Character, PlayerAt, ImpAt, bPlayerArrived);
	const AActor* ThrallMetThePlayer = PassThroughSweep(Imp, ImpAt, PlayerAt, bThrallArrived);

	// GIVEN BACK, as nothing in the game does yet: the monsters' side, and nobody's.
	Imp->SetOwner(nullptr);
	Imp->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	const FString GivenBackMasks = PassThroughMasksOf(Imp);
	bool bAfterArrived = true;
	const AActor* AfterMet = PassThroughSweep(Player.Character, PlayerAt, ImpAt, bAfterArrived);

	AddInfo(FString::Printf(TEXT("THRALL BODY: before it was taken the player met %s; taken %s, masks %s, the player met "
								 "%s and the thrall %s; given back, masks %s, the player met %s"),
							*PassThroughNameOf(BeforeMet), bTaken ? TEXT("yes") : TEXT("no"), *TakenMasks,
							*PassThroughNameOf(PlayerMetTheThrall), *PassThroughNameOf(ThrallMetThePlayer),
							*GivenBackMasks, *PassThroughNameOf(AfterMet)));

	TestTrue(TEXT("before it is taken, the Imp stops the player"), BeforeMet == Imp && !bBeforeArrived);
	if (!TestTrue(TEXT("set-up: the player takes it"), bTaken))
	{
		return false;
	}
	TestEqual(TEXT("taken, it carries a commanded body"), TakenMasks, FString(TEXT("2/5")));
	TestTrue(TEXT("the player walks through the thrall"), PlayerMetTheThrall == nullptr && bPlayerArrived);
	TestTrue(TEXT("and the thrall through the player"), ThrallMetThePlayer == nullptr && bThrallArrived);
	TestEqual(TEXT("given back, it carries no such body"), GivenBackMasks, FString(TEXT("0/0")));
	TestTrue(TEXT("and stops the player again"), AfterMet == Imp && !bAfterArrived);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
