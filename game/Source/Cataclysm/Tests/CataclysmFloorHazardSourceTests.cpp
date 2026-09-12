// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Dungeon/CataclysmFloorContents.h"
#include "Dungeon/CataclysmFloorHazardSource.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for whose name a floor's hazards are dealt in.
 *
 * WHAT THESE GUARD, AND IT IS A FAILURE THAT LEAVES NO TRACE. Every route that
 * applies anything in `UCataclysmSkillEffects` -- `ApplyDirectDamage`,
 * `ApplyNamedEffect`, `ApplyPin` and `ApplyTagForDuration` -- refuses unless the
 * SOURCE actor resolves to an ability system component. A game mode carries
 * none. So a floor hazard owned by the dungeon game mode would spawn, sweep,
 * find everyone standing in it, and do nothing whatever to any of them, while
 * every part of that read as working.
 *
 * THE FIRST TEST BELOW IS THE FAILURE ITSELF, kept rather than deleted once it
 * was fixed. It is what stops somebody deciding the empty
 * `ACataclysmFloorHazardSource` actor is pointless and having the game mode own
 * hazards instead.
 *
 * THE SWEEP COUNT IS ASSERTED ALONGSIDE THE HEALTH, and that is the whole
 * sharpness of the test. Without it, "the target took no damage" has two
 * possible causes -- the zone never found the target, or it found it and could
 * not act on it -- and only the second is the fault being guarded. Asserting
 * that the sweep found exactly one target and that the target's health did not
 * move isolates it to the effects system.
 */

namespace CataclysmFloorHazardSourceTest
{
	/** Metres, so the tests read like the design document does. */
	constexpr float M = 100.0f;

	static float HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System =
			UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/**
	 * A character on the given side, destroyed when the test leaves scope.
	 *
	 * `ACataclysmEnemyCharacter` is the stand-in for both sides here for the
	 * reason `CataclysmTeamsTests.cpp` gives: it is the only concrete class in
	 * the project carrying a collision capsule, an ability system component and
	 * a side at once, so one class can play a monster or the player's side by
	 * having its side set.
	 */
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
// The failure this actor exists to prevent
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHazardWithNoAbilitySystemSourceDoesNothing,
	"Cataclysm.FloorHazard.AHazardOwnedBySomethingWithNoAbilitySystemAppliesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHazardWithNoAbilitySystemSourceDoesNothing::RunTest(const FString&)
{
	using namespace CataclysmFloorHazardSourceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// A PLAIN ACTOR STANDS IN FOR THE DUNGEON GAME MODE, and the assertion below
	// says why that is the same case rather than merely a similar one: what
	// matters is that `AbilitySystemOf` finds nothing, which is true of a game
	// mode, of an `AInfo`, and of this.
	AActor* NoAbilitySystem = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("a source with no ability system"), NoAbilitySystem))
	{
		return false;
	}

	TestNull(TEXT("the stand-in really has no ability system, which is the whole case"),
		UCataclysmTargeting::AbilitySystemOf(NoAbilitySystem));

	FScopedCharacter Standing(World, FVector::ZeroVector, ECataclysmTeam::Players);

	const float Before = HealthOf(Standing.Actor);

	// A TEST THAT CANNOT SEE DAMAGE PROVES NOTHING, so the health it is about to
	// watch is checked to be a real figure first.
	if (!TestTrue(TEXT("the character standing in it starts with health to lose"),
			Before > 0.0f))
	{
		return false;
	}

	ACataclysmGroundZone* Hazard = ACataclysmGroundZone::Spawn(
		NoAbilitySystem, FVector::ZeroVector, /*RadiusCm=*/5.0f * M,
		/*Duration=*/10.0f, /*DamagePerTick=*/25.0f);
	if (!TestNotNull(TEXT("a hazard was left in the world"), Hazard))
	{
		return false;
	}

	// DRIVEN DIRECTLY, because a zone sweeps on a timer and this world's clock is
	// never run. What one sweep does is the subject here.
	Hazard->Sweep();

	// THIS IS THE HALF THAT MAKES THE TEST MEAN SOMETHING. The sweep found the
	// character; targeting is not what failed.
	TestEqual(TEXT("the sweep did find the character standing in it"),
		Hazard->LastSweepCount, 1);

	TestEqual(TEXT("and yet its health did not move, because the source has no "
				   "ability system and every route in UCataclysmSkillEffects "
				   "refuses one that has none"),
		HealthOf(Standing.Actor), Before, 0.01f);

	return true;
}

// --------------------------------------------------------------------------
// And the same hazard with a source that does carry one
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHazardWithAFloorSourceApplies,
	"Cataclysm.FloorHazard.TheSameHazardOwnedByAFloorHazardSourceAppliesItsDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHazardWithAFloorSourceApplies::RunTest(const FString&)
{
	using namespace CataclysmFloorHazardSourceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmFloorHazardSource* Source =
		ACataclysmFloorHazardSource::ForFloor(World);
	if (!TestNotNull(TEXT("the floor has a hazard source"), Source))
	{
		return false;
	}

	TestNotNull(TEXT("and it carries an ability system, which is why it exists"),
		UCataclysmTargeting::AbilitySystemOf(Source));

	FScopedCharacter Standing(World, FVector::ZeroVector, ECataclysmTeam::Players);

	const float Before = HealthOf(Standing.Actor);
	if (!TestTrue(TEXT("the character standing in it starts with health to lose"),
			Before > 0.0f))
	{
		return false;
	}

	// THE SAME CALL AS THE TEST ABOVE, with only the owner different. That is
	// what makes the pair a measurement rather than two separate assertions.
	ACataclysmGroundZone* Hazard = ACataclysmGroundZone::Spawn(
		Source, FVector::ZeroVector, /*RadiusCm=*/5.0f * M,
		/*Duration=*/10.0f, /*DamagePerTick=*/25.0f);
	if (!TestNotNull(TEXT("a hazard was left in the world"), Hazard))
	{
		return false;
	}

	Hazard->Sweep();

	TestEqual(TEXT("the sweep found the character standing in it"),
		Hazard->LastSweepCount, 1);

	TestTrue(FString::Printf(
			TEXT("and this time its health fell, from %.1f to %.1f"),
			Before, HealthOf(Standing.Actor)),
		HealthOf(Standing.Actor) < Before);

	return true;
}

// --------------------------------------------------------------------------
// Its side, and that a hazard reads it
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorHazardSourceIsOnTheMonstersSide,
	"Cataclysm.FloorHazard.AHazardTakesTheMonstersSideFromItsFloorSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorHazardSourceIsOnTheMonstersSide::RunTest(const FString&)
{
	using namespace CataclysmFloorHazardSourceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmFloorHazardSource* Source =
		ACataclysmFloorHazardSource::ForFloor(World);
	if (!TestNotNull(TEXT("the floor has a hazard source"), Source))
	{
		return false;
	}

	// STATED RATHER THAN LEFT TO THE DEFAULT, and this is the assertion that
	// says so. `FGenericTeamId::NoTeam` would not make a hazard neutral:
	// `UCataclysmTeams` records that no side means hostile to everything, which
	// would have a floor's hazards burning the creatures that floor spawned.
	TestEqual(TEXT("a floor's hazard source is on the monsters' side"),
		Source->GetGenericTeamId().GetId(),
		UCataclysmTeams::IdFor(ECataclysmTeam::Monsters).GetId());

	ACataclysmGroundZone* Hazard = ACataclysmGroundZone::Spawn(
		Source, FVector::ZeroVector, /*RadiusCm=*/5.0f * M,
		/*Duration=*/10.0f, /*DamagePerTick=*/25.0f);
	if (!TestNotNull(TEXT("a hazard was left in the world"), Hazard))
	{
		return false;
	}

	// A HAZARD HAS NO SIDE OF ITS OWN. `UCataclysmTeams::TeamOf` walks the owner
	// chain, which is the whole reason the source's side decides this.
	TestEqual(TEXT("and a hazard it owns reads that side through the owner chain"),
		UCataclysmTeams::TeamOf(Hazard).GetId(),
		UCataclysmTeams::IdFor(ECataclysmTeam::Monsters).GetId());

	FScopedCharacter Player(World, FVector(1.0f * M, 0.0f, 0.0f),
							ECataclysmTeam::Players);
	FScopedCharacter Creature(World, FVector(2.0f * M, 0.0f, 0.0f),
							  ECataclysmTeam::Monsters);

	TestTrue(TEXT("so the player is an enemy of it"),
		UCataclysmTargeting::IsHostileTo(Player.Actor, Hazard));
	TestFalse(TEXT("and a creature the floor spawned is not"),
		UCataclysmTargeting::IsHostileTo(Creature.Actor, Hazard));

	return true;
}

// --------------------------------------------------------------------------
// One per floor, and it goes when the floor does
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorHazardSourceLivesForOneFloor,
	"Cataclysm.FloorHazard.ThereIsOnePerFloorAndClearingTheFloorTakesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorHazardSourceLivesForOneFloor::RunTest(const FString&)
{
	using namespace CataclysmFloorHazardSourceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	TestNull(TEXT("a fresh floor has no hazard source until something wants one"),
		ACataclysmFloorHazardSource::Existing(World));

	ACataclysmFloorHazardSource* First =
		ACataclysmFloorHazardSource::ForFloor(World);
	if (!TestNotNull(TEXT("asking for one makes it"), First))
	{
		return false;
	}

	// ONE PER FLOOR. A second hazard on the same floor must be dealt in the same
	// name, or two hazards on one floor would be two different attackers.
	TestEqual(TEXT("and asking again gives the same one, not a second"),
		ACataclysmFloorHazardSource::ForFloor(World), First);

	UCataclysmFloorContents::ClearTheFloor(*World);

	TestNull(TEXT("leaving the floor takes it"),
		ACataclysmFloorHazardSource::Existing(World));

	// AND THE NEXT FLOOR MAKES ITS OWN, which is why nothing has to spawn one at
	// floor creation time and `ACataclysmDungeonGameMode::BuildFloor` is
	// untouched by any of this.
	ACataclysmFloorHazardSource* Next =
		ACataclysmFloorHazardSource::ForFloor(World);
	TestNotNull(TEXT("and the next floor makes its own"), Next);
	TestNotEqual(TEXT("which is a different actor from the last floor's"),
		Next, First);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
