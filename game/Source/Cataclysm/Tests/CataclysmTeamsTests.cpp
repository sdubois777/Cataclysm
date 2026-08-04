// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for which side something is on.
 *
 * WHAT THESE GUARD. Issue #162: before this, UCataclysmTargeting::IsHostileTo
 * treated any actor with an ability system that was not the caster and not owned
 * by them as an enemy. That made every enemy a legal target for every other
 * enemy's skills, made a second player in a co-operative session a legal target
 * for the first, and left no way at all to find an ally -- which is why the ally
 * half of Conflagration and Blood and Iron could not be built.
 *
 * WHY ACataclysmEnemyCharacter IS USED AS THE STAND-IN FOR BOTH SIDES. It is the
 * only concrete class in the project that carries a collision capsule, an
 * ability system component and a side all at once, so one class can play an
 * enemy, a second player and an ally by having its side set. SetGenericTeamId is
 * not a test-only door: it is the engine interface's own method and it is what
 * ACataclysmMinion::Spawn calls.
 */

namespace CataclysmTeamsTest
{
	static UWorld* MakeWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	}

	/** Metres, so the tests read like the design document does. */
	constexpr float M = 100.0f;

	/** A character on the given side, destroyed when the test leaves scope. */
	struct FScopedCharacter
	{
		FScopedCharacter(UWorld* World, const FVector& Where, ECataclysmTeam Team)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(
				Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(Team));
		}

		~FScopedCharacter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		ACataclysmEnemyCharacter* Actor = nullptr;
	};
}

// --------------------------------------------------------------------------
// Sides
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSameSideIsNotHostileTest,
	"Cataclysm.Teams.TwoCharactersOnTheSameSideAreNotEachOthersEnemies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSameSideIsNotHostileTest::RunTest(const FString&)
{
	using namespace CataclysmTeamsTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCharacter MonsterA(World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	FScopedCharacter MonsterB(World, FVector(2 * M, 0, 0), ECataclysmTeam::Monsters);

	// THIS IS THE BEHAVIOUR THAT CHANGED. Before sides existed both of these were
	// true: two enemies each had an ability system, neither owned the other, so
	// each was a legal target for the other's skills.
	TestFalse(TEXT("One monster is not another monster's enemy"),
		UCataclysmTargeting::IsHostileTo(MonsterB.Actor, MonsterA.Actor));
	TestFalse(TEXT("Nor the other way round"),
		UCataclysmTargeting::IsHostileTo(MonsterA.Actor, MonsterB.Actor));

	TestTrue(TEXT("They are allies of each other"),
		UCataclysmTargeting::IsFriendlyTo(MonsterB.Actor, MonsterA.Actor));

	// Two player characters are the same case. A second player in a co-operative
	// session is not something the first may hit with Molten Cleave.
	FScopedCharacter PlayerA(World, FVector(6 * M, 0, 0), ECataclysmTeam::Players);
	FScopedCharacter PlayerB(World, FVector(8 * M, 0, 0), ECataclysmTeam::Players);

	TestFalse(TEXT("A second player is not a target for the first"),
		UCataclysmTargeting::IsHostileTo(PlayerB.Actor, PlayerA.Actor));
	TestTrue(TEXT("They are allies"),
		UCataclysmTargeting::IsFriendlyTo(PlayerB.Actor, PlayerA.Actor));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOppositeSidesAreHostileTest,
	"Cataclysm.Teams.APlayerAndAMonsterAreEnemies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOppositeSidesAreHostileTest::RunTest(const FString&)
{
	using namespace CataclysmTeamsTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCharacter Player(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedCharacter Monster(World, FVector(2 * M, 0, 0), ECataclysmTeam::Monsters);

	TestTrue(TEXT("A monster is a target for the player"),
		UCataclysmTargeting::IsHostileTo(Monster.Actor, Player.Actor));
	TestTrue(TEXT("And the player is a target for the monster"),
		UCataclysmTargeting::IsHostileTo(Player.Actor, Monster.Actor));

	TestFalse(TEXT("Neither is the other's ally"),
		UCataclysmTargeting::IsFriendlyTo(Monster.Actor, Player.Actor));

	// Nothing is its own enemy, and nothing is its own ally either. An aura that
	// found the caster among "allies within it" would apply its ally benefit to
	// the person casting it, which the design writes as a separate clause where
	// it means it.
	TestFalse(TEXT("A character is not its own enemy"),
		UCataclysmTargeting::IsHostileTo(Player.Actor, Player.Actor));
	TestFalse(TEXT("A character is not its own ally"),
		UCataclysmTargeting::IsFriendlyTo(Player.Actor, Player.Actor));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoSideIsHostileTest,
	"Cataclysm.Teams.SomethingWithNoSideIsHostileRatherThanUntouchable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmNoSideIsHostileTest::RunTest(const FString&)
{
	using namespace CataclysmTeamsTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCharacter Monster(World, FVector::ZeroVector, ECataclysmTeam::Monsters);

	// A bare actor: no team, and nothing that would give it one.
	AActor* Sideless = World->SpawnActor<AActor>();
	AActor* AlsoSideless = World->SpawnActor<AActor>();
	ON_SCOPE_EXIT { Sideless->Destroy(); AlsoSideless->Destroy(); };

	TestEqual(TEXT("An actor with nothing to say has no side"),
		UCataclysmTeams::TeamOf(Sideless).GetId(), FGenericTeamId::NoTeam.GetId());

	// THE CHOICE OF FAILURE MODE, AND THIS IS WHAT PINS IT. A class that forgets
	// to set its side is still something the player can kill. Treating no side as
	// neutral instead would make it silently immune to every skill in the game,
	// which is far harder to notice than being hit by something that should not
	// hit it. Note this is the opposite of the engine's own default solver, which
	// treats two actors that both have no side as equal and therefore friendly.
	TestEqual(TEXT("No side against a real side is hostile"),
		static_cast<int32>(UCataclysmTeams::AttitudeBetween(Monster.Actor, Sideless)),
		static_cast<int32>(ETeamAttitude::Hostile));
	TestEqual(TEXT("And two things with no side are hostile to each other"),
		static_cast<int32>(UCataclysmTeams::AttitudeBetween(Sideless, AlsoSideless)),
		static_cast<int32>(ETeamAttitude::Hostile));

	// Neutral is reserved for a question that cannot be answered. Nothing in the
	// project is a neutral actor yet; the value exists so that one can be added
	// without every caller changing shape.
	TestEqual(TEXT("An attitude toward nothing is neutral"),
		static_cast<int32>(UCataclysmTeams::AttitudeBetween(Monster.Actor, nullptr)),
		static_cast<int32>(ETeamAttitude::Neutral));

	return true;
}

// --------------------------------------------------------------------------
// What a character puts into the world
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSummonTakesItsSummonersSideTest,
	"Cataclysm.Teams.ASummonFightsForWhoeverMadeIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSummonTakesItsSummonersSideTest::RunTest(const FString&)
{
	using namespace CataclysmTeamsTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCharacter Summoner(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedCharacter SecondPlayer(World, FVector(3 * M, 0, 0), ECataclysmTeam::Players);
	FScopedCharacter Monster(World, FVector(6 * M, 0, 0), ECataclysmTeam::Monsters);

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Summoner.Actor, FVector(1 * M, 0, 0), /*Lifetime=*/20.0f, /*bBurns=*/true);
	if (!Imp)
	{
		AddError(TEXT("Could not summon a minion."));
		return false;
	}

	TestEqual(TEXT("An imp is on the side of whoever summoned it"),
		Imp->GetGenericTeamId().GetId(),
		UCataclysmTeams::IdFor(ECataclysmTeam::Players).GetId());

	// OWNERSHIP ALONE COULD NOT SAY THIS, which is why the side is copied rather
	// than left to the owner chain. A second player does not own the first
	// player's imps, so under the old rule each was a legal target for the other.
	TestFalse(TEXT("A second player cannot hit the first player's imp"),
		UCataclysmTargeting::IsHostileTo(Imp, SecondPlayer.Actor));
	TestFalse(TEXT("And the imp does not attack the second player"),
		UCataclysmTargeting::IsHostileTo(SecondPlayer.Actor, Imp));

	TestFalse(TEXT("An imp does not attack the character that made it"),
		UCataclysmTargeting::IsHostileTo(Summoner.Actor, Imp));
	TestTrue(TEXT("An imp does attack a monster"),
		UCataclysmTargeting::IsHostileTo(Monster.Actor, Imp));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundZoneTakesItsOwnersSideTest,
	"Cataclysm.Teams.BurningGroundIsOnTheSideOfWhoeverLeftIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGroundZoneTakesItsOwnersSideTest::RunTest(const FString&)
{
	using namespace CataclysmTeamsTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCharacter Player(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedCharacter SecondPlayer(World, FVector(3 * M, 0, 0), ECataclysmTeam::Players);
	FScopedCharacter Monster(World, FVector(6 * M, 0, 0), ECataclysmTeam::Monsters);

	ACataclysmGroundZone* Zone = ACataclysmGroundZone::Spawn(
		Player.Actor, FVector(1 * M, 0, 0), /*RadiusCm=*/300.0f,
		/*Duration=*/4.0f, /*DamagePerTick=*/10.0f);
	if (!Zone)
	{
		AddError(TEXT("Could not leave a patch of burning ground."));
		return false;
	}

	// A patch of burning ground has no ability system and no side of its own, and
	// it is passed as the instigator when it sweeps for who is standing in it.
	// The owner chain is what puts it on the right side; without that walk it
	// would burn the character who left it and their allies too.
	TestEqual(TEXT("Burning ground is on the side of whoever left it"),
		UCataclysmTeams::TeamOf(Zone).GetId(),
		UCataclysmTeams::IdFor(ECataclysmTeam::Players).GetId());

	TestTrue(TEXT("It burns a monster"),
		UCataclysmTargeting::IsHostileTo(Monster.Actor, Zone));
	TestFalse(TEXT("It does not burn a second player"),
		UCataclysmTargeting::IsHostileTo(SecondPlayer.Actor, Zone));
	TestFalse(TEXT("Nor the character who left it"),
		UCataclysmTargeting::IsHostileTo(Player.Actor, Zone));

	return true;
}

// --------------------------------------------------------------------------
// Searching for allies rather than enemies
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAllySearchTest,
	"Cataclysm.Teams.AnAuraCanFindTheAlliesStandingInIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAllySearchTest::RunTest(const FString&)
{
	using namespace CataclysmTeamsTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCharacter Caster(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedCharacter Ally(World, FVector(2 * M, 0, 0), ECataclysmTeam::Players);
	FScopedCharacter Monster(World, FVector(4 * M, 0, 0), ECataclysmTeam::Monsters);
	FScopedCharacter FarAlly(World, FVector(30 * M, 0, 0), ECataclysmTeam::Players);

	// Conflagration: "allies within it deal 8% increased fire damage". This is
	// the half that finds who those allies are. The benefit itself is a buff
	// magnitude and is still not applied -- that is issue #166.
	const TArray<AActor*> Allies = UCataclysmTargeting::FindAlliesInSphere(
		World, Caster.Actor, FVector::ZeroVector, 10 * M);

	TestEqual(TEXT("One ally stands within ten metres"), Allies.Num(), 1);
	TestTrue(TEXT("And it is the one on the same side"), Allies.Contains(Ally.Actor));
	TestFalse(TEXT("A monster is not an ally"), Allies.Contains(Monster.Actor));
	TestFalse(TEXT("The caster is not its own ally"), Allies.Contains(Caster.Actor));
	TestFalse(TEXT("An ally outside the radius is not found"),
		Allies.Contains(FarAlly.Actor));

	// THE SAME SEARCH WITH THE OTHER ATTITUDE, which is the point of sharing one
	// body between the two: an aura that damages enemies and buffs allies asks
	// the same question twice rather than running two different searches.
	const TArray<AActor*> Enemies = UCataclysmTargeting::FindEnemiesInSphere(
		World, Caster.Actor, FVector::ZeroVector, 10 * M);

	TestEqual(TEXT("One enemy stands within ten metres"), Enemies.Num(), 1);
	TestTrue(TEXT("And it is the monster"), Enemies.Contains(Monster.Actor));
	TestFalse(TEXT("The ally is not among the enemies"), Enemies.Contains(Ally.Actor));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
