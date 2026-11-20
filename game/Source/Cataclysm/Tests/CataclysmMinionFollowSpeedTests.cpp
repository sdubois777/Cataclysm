// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "Animation/AnimSequence.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmCorruptedSentinelCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmHellhoundCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmSuccubusCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * EVERY MINION THE PLAYER COMMANDS WALKS AT 1.25 TIMES THE PLAYER'S SPEED NOW, AND A TAKEN CORRUPTED SENTINEL FOLLOWS.
 * Issue #2793, from the owner's playtest of 2026-11-20: "Corrupted sentinels don't seem to follow you when subjugated.
 * Your minions movespeed should be equal to 1.25 of your own. That way they actually keep up." Ruled by the coordinating
 * session the same day: EQUAL, not the faster of the two; a thrall's own hastes and slows still multiply it; a summoned
 * minion gets no such path; the Sentinel's jog plays at an unmeasured 1.0.
 *
 * THE SENTINEL'S FAULT WAS ITS SPEED. Its brain ordered it to follow, and `RefreshWalkSpeed` refused every creature
 * designed at nought, so the order moved it nowhere.
 *
 * NO AUTOMATION TEST TICKS A WORLD, so walking itself is not measured here. What is measured is what the movement
 * component is told to walk at, which is what was missing, and the brain's order, which was already given.
 *
 * THE PLAYER IS A SPAWNED `ACataclysmPlayerCharacter` NOBODY POSSESSES, walking at its designed 400 cm/s, so its
 * commanded minions walk at 500. `ApplyMovementSpeed` changes it the way the movement speed stat does.
 */
namespace CataclysmMinionFollowSpeedTest
{
	constexpr float M = 100.0f;

	/** What a player's minions walk at while the player walks at its designed 400: 1.25 x 400 = 500, WRITTEN OUT rather than worked out from the code's own share,
	 *  so a change to the share or to where it is applied is seen here rather than repeated on both sides. */
	constexpr float CommandedAtDesigned = 500.0f;

	ACataclysmPlayerCharacter* SpawnFollowSpeedPlayer(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACataclysmPlayerCharacter>(ACataclysmPlayerCharacter::StaticClass(),
															 FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}

	template <typename TCreature>
	TCreature* SpawnFollowSpeedCreature(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<TCreature>(TCreature::StaticClass(), Where, FRotator::ZeroRotator, Params);
	}

	/** Take it, as the overlay test does: its health first, because Subjugate refuses the dead. */
	bool TakeForFollowSpeed(AActor* Commander, ACataclysmEnemyCharacter* Creature)
	{
		Creature->SetHealth(100.0f);
		return UCataclysmCommand::Subjugate(Commander, Creature);
	}

	/** What its movement component is told to walk at, or -1 when it has none. */
	float FollowSpeedWalkOf(const ACharacter* Body)
	{
		const UCharacterMovementComponent* Movement = Body ? Body->GetCharacterMovement() : nullptr;
		return Movement ? Movement->MaxWalkSpeed : -1.0f;
	}

	/** The designed figure the creature read in BeginPlay: the BASE member, which each creature's own static constant
	 *  of the same name hides. */
	float FollowSpeedDesignOf(const ACataclysmEnemyCharacter* Creature)
	{
		return Creature->DesignedWalkSpeedCmPerSecond;
	}
}

// --------------------------------------------------------------------------------------------------------------------
// T1. The fault: a taken Sentinel is given its commander's pace; nobody's stays rooted; and it roots again when nobody
// commands it any more.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFollowSpeedTakenSentinelTest,
	"Cataclysm.MinionFollowSpeed.ATakenSentinelWalksAtItsCommandersPaceAndNobodysStaysRooted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFollowSpeedTakenSentinelTest::RunTest(const FString&)
{
	using namespace CataclysmMinionFollowSpeedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnFollowSpeedPlayer(World);
	ACataclysmCorruptedSentinelCharacter* Taken =
		SpawnFollowSpeedCreature<ACataclysmCorruptedSentinelCharacter>(World, FVector(10 * M, 0.0f, 0.0f));
	ACataclysmCorruptedSentinelCharacter* Nobodys =
		SpawnFollowSpeedCreature<ACataclysmCorruptedSentinelCharacter>(World, FVector(-10 * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a player"), Player)
		|| !TestNotNull(TEXT("set-up: a Sentinel to take"), Taken)
		|| !TestNotNull(TEXT("set-up: a Sentinel nobody takes"), Nobodys))
	{
		return false;
	}

	TestEqual(TEXT("set-up: the player walks at its designed 400 cm/s"), FollowSpeedWalkOf(Player), 400.0f);
	TestEqual(TEXT("the share of its commander's pace a minion walks at is the owner's 1.25"),
			  UCataclysmCommand::CommandedWalkSpeedShareOfCommander, 1.25f);
	TestEqual(TEXT("set-up: a Sentinel is designed to walk at nought"), FollowSpeedDesignOf(Taken), 0.0f);
	TestEqual(TEXT("set-up: and is told to walk at nought"), FollowSpeedWalkOf(Taken), 0.0f);
	if (!TestTrue(TEXT("set-up: the player takes one Sentinel"), TakeForFollowSpeed(Player, Taken)))
	{
		return false;
	}

	Taken->RefreshWalkSpeed();
	Nobodys->RefreshWalkSpeed();

	TestEqual(TEXT("the taken Sentinel walks at 1.25 times its commander's speed"), FollowSpeedWalkOf(Taken),
			  CommandedAtDesigned, 0.01f);
	TestTrue(TEXT("and records that its speed follows a commander"), Taken->bWalkSpeedFollowsACommander);
	TestEqual(TEXT("CONTROL: the Sentinel nobody took stays rooted"), FollowSpeedWalkOf(Nobodys), 0.0f);
	TestFalse(TEXT("CONTROL: and follows nobody's pace"), Nobodys->bWalkSpeedFollowsACommander);
	TestEqual(TEXT("the taken Sentinel's designed figure is still nought, for everything that reads the design"),
			  FollowSpeedDesignOf(Taken), 0.0f);

	// NOBODY'S AGAIN. A thrall names its commander as its owner, so an owner of nothing is no commander; it must root
	// again rather than walk on at its last commander's pace.
	Taken->SetOwner(nullptr);
	Taken->RefreshWalkSpeed();
	TestEqual(TEXT("once nobody commands it, it is rooted again"), FollowSpeedWalkOf(Taken), 0.0f);
	TestFalse(TEXT("and no longer follows a commander's pace"), Taken->bWalkSpeedFollowsACommander);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// T2. The brain's half, which was never the fault: with nothing to fight, a taken Sentinel is ordered to follow, and
// now it has a speed to follow with.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFollowSpeedSentinelIsOrderedToFollowTest,
	"Cataclysm.MinionFollowSpeed.ATakenSentinelIsOrderedToFollowAndHasASpeedToFollowWith",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFollowSpeedSentinelIsOrderedToFollowTest::RunTest(const FString&)
{
	using namespace CataclysmMinionFollowSpeedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// TEN METRES APART, further than the three it stands at, so following means walking.
	ACataclysmPlayerCharacter* Player = SpawnFollowSpeedPlayer(World);
	ACataclysmCorruptedSentinelCharacter* Taken =
		SpawnFollowSpeedCreature<ACataclysmCorruptedSentinelCharacter>(World, FVector(10 * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: a Sentinel"), Taken)
		|| !TestTrue(TEXT("set-up: the player takes it"), TakeForFollowSpeed(Player, Taken)))
	{
		return false;
	}
	ACataclysmEnemyController* Brain = Cast<ACataclysmEnemyController>(Taken->GetController());
	if (!TestNotNull(TEXT("set-up: the Sentinel has a brain"), Brain))
	{
		return false;
	}

	TestFalse(TEXT("set-up: a Sentinel is not something that stays where it is put"), Taken->StaysWhereItIsPut());
	TestEqual(TEXT("with nothing to fight, the taken Sentinel is ordered to follow its commander"),
			  static_cast<int32>(Brain->Think()), static_cast<int32>(ECataclysmBrainAction::Following));
	Taken->RefreshWalkSpeed();
	TestTrue(TEXT("and it has a speed to follow with"), FollowSpeedWalkOf(Taken) > 0.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// T3. Every kind the player commands walks at 1.25 times the player's speed: the summoned kinds and the thralls.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFollowSpeedEveryKindTest,
	"Cataclysm.MinionFollowSpeed.EveryKindTheCommanderCommandsWalksAtOneAndAQuarterOfItsSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE SUMMONED KINDS ARE THE MINION TABLE'S WALKING ROWS, made by `ACataclysmMinion::Spawn` as every summon is: Imp,
 * Mote, Warhound and TyrantsMinion, and one made with no type. A RISEN IMP AND A NECROSIS IMP are that same Spawn with
 * the Imp row (`CataclysmRisenImps.cpp`), so the Imp row is them too. Spawn sets the pace itself, so nothing here
 * refreshes a summoned minion before asking.
 *
 * THE THRALLS: an Imp, a Hellhound, a Succubus, an Abyssal Warden, a Brute (walking; its chase speed reaches the same
 * function through its own override) and a Corrupted Sentinel. A Hellhound and an Imp walk faster than 500 untaken and
 * are slowed to it: EQUAL, as ruled.
 */
bool FCataclysmFollowSpeedEveryKindTest::RunTest(const FString&)
{
	using namespace CataclysmMinionFollowSpeedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnFollowSpeedPlayer(World);
	if (!TestNotNull(TEXT("set-up: a player"), Player))
	{
		return false;
	}

	// THE SUMMONED KINDS, five metres apart on one line ten metres out.
	const TCHAR* Rows[] = {TEXT("Imp"), TEXT("Mote"), TEXT("Warhound"), TEXT("TyrantsMinion"), TEXT("")};
	int32 At = 0;
	for (const TCHAR* Row : Rows)
	{
		ACataclysmMinion* Minion = ACataclysmMinion::Spawn(Player, FVector(10 * M, (5 * At++) * M, 0.0f),
														   /*Lifetime=*/20.0f, /*bBurns=*/false, Row);
		if (!TestNotNull(*FString::Printf(TEXT("set-up: a minion of row '%s'"), Row), Minion))
		{
			continue;
		}
		ON_SCOPE_EXIT { if (IsValid(Minion)) { Minion->Destroy(); } };
		TestEqual(*FString::Printf(TEXT("set-up: it was made from row '%s'"), Row), Minion->TypeName, FString(Row));
		TestEqual(*FString::Printf(TEXT("the summoned '%s' walks at 1.25 times its summoner's speed"), Row),
				  FollowSpeedWalkOf(Minion), CommandedAtDesigned, 0.01f);
	}

	// THE THRALLS, five metres apart on another line twenty metres out.
	ACataclysmEnemyCharacter* Thralls[] = {
		SpawnFollowSpeedCreature<ACataclysmImpCharacter>(World, FVector(20 * M, 0.0f, 0.0f)),
		SpawnFollowSpeedCreature<ACataclysmHellhoundCharacter>(World, FVector(20 * M, 5 * M, 0.0f)),
		SpawnFollowSpeedCreature<ACataclysmSuccubusCharacter>(World, FVector(20 * M, 10 * M, 0.0f)),
		SpawnFollowSpeedCreature<ACataclysmAbyssalWardenCharacter>(World, FVector(20 * M, 15 * M, 0.0f)),
		SpawnFollowSpeedCreature<ACataclysmBruteCharacter>(World, FVector(20 * M, 20 * M, 0.0f)),
		SpawnFollowSpeedCreature<ACataclysmCorruptedSentinelCharacter>(World, FVector(20 * M, 25 * M, 0.0f)),
	};
	const TCHAR* Names[] = {TEXT("Imp"), TEXT("Hellhound"), TEXT("Succubus"), TEXT("Abyssal Warden"), TEXT("Brute"),
							TEXT("Corrupted Sentinel")};
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Thralls)); ++Index)
	{
		ACataclysmEnemyCharacter* Thrall = Thralls[Index];
		if (!TestNotNull(*FString::Printf(TEXT("set-up: a %s"), Names[Index]), Thrall)
			|| !TestTrue(*FString::Printf(TEXT("set-up: the player takes the %s"), Names[Index]),
						 TakeForFollowSpeed(Player, Thrall)))
		{
			continue;
		}
		Thrall->RefreshWalkSpeed();
		TestEqual(*FString::Printf(TEXT("the %s thrall walks at 1.25 times its commander's speed"), Names[Index]),
				  FollowSpeedWalkOf(Thrall), CommandedAtDesigned, 0.01f);
	}
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// T4. The pace follows the commander's as it changes: faster and slower, a summoned minion and a thrall alike.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFollowSpeedFollowsTheChangeTest,
	"Cataclysm.MinionFollowSpeed.ThePaceFollowsTheCommandersSpeedAsItChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFollowSpeedFollowsTheChangeTest::RunTest(const FString&)
{
	using namespace CataclysmMinionFollowSpeedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnFollowSpeedPlayer(World);
	ACataclysmMinion* Imp = Player ? ACataclysmMinion::Spawn(Player, FVector(10 * M, 0.0f, 0.0f), /*Lifetime=*/20.0f,
															  /*bBurns=*/false, TEXT("Imp"))
								   : nullptr;
	ACataclysmImpCharacter* Thrall = SpawnFollowSpeedCreature<ACataclysmImpCharacter>(World, FVector(20 * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: a summoned imp"), Imp)
		|| !TestNotNull(TEXT("set-up: an imp creature"), Thrall)
		|| !TestTrue(TEXT("set-up: the player takes it"), TakeForFollowSpeed(Player, Thrall)))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	// EVERY FRAME, FOR A SUMMONED MINION THAT WALKS: that is what makes it follow a change.
	TestTrue(TEXT("a summoned imp ticks"), Imp->IsActorTickEnabled());

	// FASTER: 6 metres a second, as a movement speed buff would make it.
	Player->ApplyMovementSpeed(6.0f);
	Imp->RefreshWalkSpeed();
	Thrall->RefreshWalkSpeed();
	TestEqual(TEXT("set-up: the player now walks at 600"), FollowSpeedWalkOf(Player), 600.0f, 0.01f);
	TestEqual(TEXT("the summoned imp follows to 750"), FollowSpeedWalkOf(Imp), 750.0f, 0.01f);
	TestEqual(TEXT("and the thrall too"), FollowSpeedWalkOf(Thrall), 750.0f, 0.01f);

	// SLOWER: 2 metres a second, as a slow on the player would make it.
	Player->ApplyMovementSpeed(2.0f);
	Imp->RefreshWalkSpeed();
	Thrall->RefreshWalkSpeed();
	TestEqual(TEXT("set-up: the player now walks at 200"), FollowSpeedWalkOf(Player), 200.0f, 0.01f);
	TestEqual(TEXT("the summoned imp follows down to 250"), FollowSpeedWalkOf(Imp), 250.0f, 0.01f);
	TestEqual(TEXT("and the thrall too"), FollowSpeedWalkOf(Thrall), 250.0f, 0.01f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// T5. CONTROL: a deployable stays where it is put. It is not given a pace and does not tick.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFollowSpeedDeployablesStayPutTest,
	"Cataclysm.MinionFollowSpeed.ADeployableStaysWhereItIsPutAndDoesNotTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFollowSpeedDeployablesStayPutTest::RunTest(const FString&)
{
	using namespace CataclysmMinionFollowSpeedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnFollowSpeedPlayer(World);
	if (!TestNotNull(TEXT("set-up: a player"), Player))
	{
		return false;
	}

	const TCHAR* Rows[] = {TEXT("BoltTurret"), TEXT("Ballista"), TEXT("SpikeTrap")};
	int32 At = 0;
	for (const TCHAR* Row : Rows)
	{
		ACataclysmMinion* Machine = ACataclysmMinion::Spawn(Player, FVector(10 * M, (5 * At++) * M, 0.0f),
															/*Lifetime=*/20.0f, /*bBurns=*/false, Row);
		if (!TestNotNull(*FString::Printf(TEXT("set-up: a %s"), Row), Machine))
		{
			continue;
		}
		ON_SCOPE_EXIT { if (IsValid(Machine)) { Machine->Destroy(); } };
		TestEqual(*FString::Printf(TEXT("set-up: it was made from row '%s'"), Row), Machine->TypeName, FString(Row));
		TestTrue(*FString::Printf(TEXT("set-up: a %s stays where it is put"), Row), Machine->StaysWhereItIsPut());
		Machine->RefreshWalkSpeed();
		TestEqual(*FString::Printf(TEXT("a %s is given no pace"), Row), FollowSpeedWalkOf(Machine), 0.0f);
		TestEqual(*FString::Printf(TEXT("and no commanded speed is worked out for it (%s)"), Row),
				  UCataclysmCommand::CommandedWalkSpeedCmPerSecond(Machine), 0.0f);
		TestFalse(*FString::Printf(TEXT("and a %s does not tick"), Row), Machine->IsActorTickEnabled());
	}
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// T6. CONTROL: a creature nobody has taken walks as it was designed.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFollowSpeedNobodysCreaturesTest,
	"Cataclysm.MinionFollowSpeed.ACreatureNobodyHasTakenWalksAsItWasDesigned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFollowSpeedNobodysCreaturesTest::RunTest(const FString&)
{
	using namespace CataclysmMinionFollowSpeedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// A PLAYER IN THE WORLD, so a creature that wrongly followed somebody would have somebody to follow.
	ACataclysmPlayerCharacter* Player = SpawnFollowSpeedPlayer(World);
	ACataclysmImpCharacter* Imp = SpawnFollowSpeedCreature<ACataclysmImpCharacter>(World, FVector(10 * M, 0.0f, 0.0f));
	ACataclysmHellhoundCharacter* Hellhound =
		SpawnFollowSpeedCreature<ACataclysmHellhoundCharacter>(World, FVector(10 * M, 5 * M, 0.0f));
	ACataclysmBruteCharacter* Brute =
		SpawnFollowSpeedCreature<ACataclysmBruteCharacter>(World, FVector(10 * M, 10 * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: an Imp"), Imp)
		|| !TestNotNull(TEXT("set-up: a Hellhound"), Hellhound) || !TestNotNull(TEXT("set-up: a Brute"), Brute))
	{
		return false;
	}

	Imp->RefreshWalkSpeed();
	Hellhound->RefreshWalkSpeed();
	Brute->RefreshWalkSpeed();
	TestEqual(TEXT("nobody's Imp walks at its designed 650"), FollowSpeedWalkOf(Imp),
			  ACataclysmImpCharacter::DesignedWalkSpeedCmPerSecond, 0.01f);
	TestEqual(TEXT("nobody's Hellhound at its designed 750"), FollowSpeedWalkOf(Hellhound),
			  ACataclysmHellhoundCharacter::DesignedWalkSpeedCmPerSecond, 0.01f);
	TestEqual(TEXT("nobody's Brute, not chasing, at its designed 250"), FollowSpeedWalkOf(Brute),
			  ACataclysmBruteCharacter::DesignedWalkSpeedCmPerSecond, 0.01f);
	TestFalse(TEXT("and none of them follows a commander's pace"), Imp->bWalkSpeedFollowsACommander
			  || Hellhound->bWalkSpeedFollowsACommander || Brute->bWalkSpeedFollowsACommander);
	TestEqual(TEXT("no commanded speed is worked out for a creature nobody commands"),
			  UCataclysmCommand::CommandedWalkSpeedCmPerSecond(Imp), 0.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// T7. A thrall's own slow still applies, on top of its commander's pace. Ground Down's 15% is the slow.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFollowSpeedThrallsOwnSlowTest,
	"Cataclysm.MinionFollowSpeed.AThrallsOwnSlowStillAppliesOnTopOfItsCommandersPace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFollowSpeedThrallsOwnSlowTest::RunTest(const FString&)
{
	using namespace CataclysmMinionFollowSpeedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnFollowSpeedPlayer(World);
	ACataclysmImpCharacter* Thrall = SpawnFollowSpeedCreature<ACataclysmImpCharacter>(World, FVector(10 * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: an Imp"), Thrall)
		|| !TestTrue(TEXT("set-up: the player takes it"), TakeForFollowSpeed(Player, Thrall)))
	{
		return false;
	}

	// THE WORLD'S CLOCK IS A double IN UNREAL ENGINE 5.8, and the slow's end is a float.
	Thrall->NoteGroundDown(15.0f, static_cast<float>(World->GetTimeSeconds()) + 30.0f);
	TestEqual(TEXT("set-up: Ground Down takes 15% off its speed"), Thrall->SpeedMultiplier(), 0.85f, 0.0001f);
	Thrall->RefreshWalkSpeed();
	TestEqual(TEXT("the slowed thrall walks at 0.85 times its commander's pace of 500"), FollowSpeedWalkOf(Thrall),
			  0.85f * CommandedAtDesigned, 0.01f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// T8. A commanded creature's jog plays at the rate for the speed it walks at; nobody's at its designed rate.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFollowSpeedJogRateTest,
	"Cataclysm.MinionFollowSpeed.ACommandedCreaturesJogRateFollowsTheSpeedItWalksAt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFollowSpeedJogRateTest::RunTest(const FString&)
{
	using namespace CataclysmMinionFollowSpeedTest;
	using Imp_t = ACataclysmImpCharacter;
	using Hellhound_t = ACataclysmHellhoundCharacter;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnFollowSpeedPlayer(World);
	Imp_t* Taken = SpawnFollowSpeedCreature<Imp_t>(World, FVector(10 * M, 0.0f, 0.0f));
	Imp_t* Nobodys = SpawnFollowSpeedCreature<Imp_t>(World, FVector(10 * M, 5 * M, 0.0f));
	Hellhound_t* Hound = SpawnFollowSpeedCreature<Hellhound_t>(World, FVector(10 * M, 10 * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: an Imp to take"), Taken)
		|| !TestNotNull(TEXT("set-up: an Imp nobody takes"), Nobodys) || !TestNotNull(TEXT("set-up: a Hellhound"), Hound)
		|| !TestTrue(TEXT("set-up: the player takes the Imp"), TakeForFollowSpeed(Player, Taken))
		|| !TestTrue(TEXT("set-up: and the Hellhound"), TakeForFollowSpeed(Player, Hound)))
	{
		return false;
	}
	Taken->RefreshWalkSpeed();
	Nobodys->RefreshWalkSpeed();
	Hound->RefreshWalkSpeed();

	const float ImpRateTaken = Taken->JogPlayRateAtItsWalkingSpeed(
		Imp_t::JogPlayRate(), Imp_t::DesignedWalkSpeedCmPerSecond, Imp_t::MinimumPlayRate, Imp_t::MaximumPlayRate);
	const float ImpRateNobodys = Nobodys->JogPlayRateAtItsWalkingSpeed(
		Imp_t::JogPlayRate(), Imp_t::DesignedWalkSpeedCmPerSecond, Imp_t::MinimumPlayRate, Imp_t::MaximumPlayRate);
	const float HoundRateTaken = Hound->JogPlayRateAtItsWalkingSpeed(Hellhound_t::JogPlayRate(),
		Hellhound_t::DesignedWalkSpeedCmPerSecond, Hellhound_t::MinimumPlayRate, Hellhound_t::MaximumPlayRate);

	TestEqual(TEXT("the taken Imp's jog plays at its designed rate times 500 over 650"), ImpRateTaken,
			  FMath::Clamp(Imp_t::JogPlayRate() * CommandedAtDesigned / Imp_t::DesignedWalkSpeedCmPerSecond,
						   Imp_t::MinimumPlayRate, Imp_t::MaximumPlayRate),
			  0.0001f);
	TestTrue(TEXT("which is slower than its designed rate, as it walks slower than designed"),
			 ImpRateTaken < Imp_t::JogPlayRate());
	TestEqual(TEXT("CONTROL: nobody's Imp jogs at exactly its designed rate"), ImpRateNobodys, Imp_t::JogPlayRate());
	TestTrue(TEXT("and the taken Hellhound, slowed from 750 to 500, jogs slower than its designed rate"),
			 HoundRateTaken < Hellhound_t::JogPlayRate());
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// T9. A taken Sentinel jogs while it walks and stands rooted when it stops. Needs the Paragon Minions pack.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFollowSpeedSentinelJogsTest,
	"Cataclysm.MinionFollowSpeed.ATakenSentinelJogsWhileItWalksAndStandsRootedWhenItStops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFollowSpeedSentinelJogsTest::RunTest(const FString&)
{
	using namespace CataclysmMinionFollowSpeedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnFollowSpeedPlayer(World);
	ACataclysmCorruptedSentinelCharacter* Taken =
		SpawnFollowSpeedCreature<ACataclysmCorruptedSentinelCharacter>(World, FVector(10 * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: a Sentinel"), Taken)
		|| !TestTrue(TEXT("set-up: the player takes it"), TakeForFollowSpeed(Player, Taken)))
	{
		return false;
	}

	TestEqual(TEXT("the jog's play rate is the registered, unmeasured 1.0"),
			  ACataclysmCorruptedSentinelCharacter::UnmeasuredJogPlayRate, 1.0f);

	USkeletalMeshComponent* MeshComponent = Taken->GetMesh();
	if (!Taken->JogAnimation || !Taken->IdleAnimation || !MeshComponent)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Minions pack is not present, so the jog and the rooted idle were not loaded, and which "
				 "clip a walking and a standing taken Sentinel plays is NOT checked; the registered rate is."));
		return true;
	}

	// WALKING: faster across the ground than the threshold, which is what following looks like to it.
	Taken->GetCharacterMovement()->Velocity = FVector(300.0f, 0.0f, 0.0f);
	Taken->UpdateLoopingAnimation();
	TestTrue(TEXT("walking, it plays the standing jog"), Taken->CurrentLoopingAnimation == Taken->JogAnimation);
	TestEqual(TEXT("at the unmeasured rate"), MeshComponent->GetPlayRate(),
			  ACataclysmCorruptedSentinelCharacter::UnmeasuredJogPlayRate);

	// STOPPED: back to the rooted pose.
	Taken->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	Taken->UpdateLoopingAnimation();
	TestTrue(TEXT("stopped, it stands rooted again"), Taken->CurrentLoopingAnimation == Taken->IdleAnimation);
	TestEqual(TEXT("at a rate of 1"), MeshComponent->GetPlayRate(), 1.0f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
