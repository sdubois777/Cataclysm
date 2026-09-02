// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTerrain.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the persistent geometry a skill leaves behind.
 *
 * WHAT THESE GUARD. Five rows across the Spear and the Warhammer state a
 * `Terrain` kind and nothing read the column, so every one of them ran as though
 * the cell were empty. The parameter's own header draws the line these tests are
 * written along: burning ground is a damage patch and terrain "changes where a
 * fight can happen -- one burns you for standing there, the other decides where
 * 'there' is".
 *
 *   Thicket  pins whatever walks in
 *   Pit      floors whatever falls in, and nothing inside may charge or leap
 *   Fissure  floors the first creature to cross it, once, and is then spent
 *   Wall     is solid, and blocks movement and projectiles
 *
 * WHAT THEY DELIBERATELY DO NOT COVER.
 *
 *   That terrain goes away when its duration ends. `SetLifeSpan` runs on the
 *   world's timer manager, and a world built by `UWorld::CreateWorld` is never
 *   ticked. `CataclysmGroundZoneTests.cpp` says the same about the burning
 *   ground beside it and for the same reason. Every sweep below is driven by
 *   calling `Sweep` directly, which is public for exactly this.
 *
 *   That a creature actually fails to walk through a raised wall. Walking is
 *   done by the character movement component against collision, and a test world
 *   has no navigation mesh, so what is checked is that the wall exists, is
 *   solid, and is the length and place its row asked for.
 *
 *   That a pit slows a creature climbing out. It does not, and issue #1152
 *   carries why: nothing in this project can change a character's movement speed.
 */

namespace CataclysmTerrainTest
{
	/** Metres, so the tests read the way the design document does. */
	constexpr float M = 100.0f;

	static UWorld* MakeWorldThatHasBegunPlay()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}

	/** A character with health, on a side, able to be an instigator. */
	struct FScopedFighter
	{
		FScopedFighter(UWorld* World, const FVector& Where, ECataclysmTeam Team,
					   float Health = 1000.0f)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(
				Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(Team));
			Actor->SetHealth(Health);
			Actor->SetAttackDamage(50.0f);
		}

		~FScopedFighter()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void MoveTo(const FVector& Where)
		{
			Actor->SetActorLocation(Where);
		}

		ACataclysmEnemyCharacter* Actor = nullptr;
	};

	/** Terrain that cleans itself up, so one test's pit is not another's. */
	struct FScopedTerrain
	{
		FScopedTerrain(AActor* Owner, ECataclysmTerrainKind Kind,
					   const FVector& Start, const FVector& End, float SizeCm,
					   float Duration, float HoldSeconds)
		{
			Actor = ACataclysmTerrain::Spawn(Owner, Kind, Start, End, SizeCm,
											 Duration, HoldSeconds);
		}

		~FScopedTerrain()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		ACataclysmTerrain* Actor = nullptr;
	};
}

// --------------------------------------------------------------------------
// Reading the sheet's own word for a kind
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTerrainKindFromCellTest,
	"Cataclysm.Terrain.TheFourKindsAreReadFromTheSheetsOwnWords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTerrainKindFromCellTest::RunTest(const FString&)
{
	using namespace CataclysmTerrainTest;

	// THE CLOSED LIST IN `tools/generate_datatables.py` IS `{Pit, Wall, Fissure,
	// Thicket}` AND THIS ENUM HAS TO AGREE WITH IT. When the shape list and the
	// generator disagreed before, on issue #621, the consequence was silent: a
	// row naming the missing value read as None and filled its slot doing
	// nothing.
	TestEqual(TEXT("Pit"),
		static_cast<int32>(ACataclysmTerrain::KindFromCell(TEXT("Pit"))),
		static_cast<int32>(ECataclysmTerrainKind::Pit));
	TestEqual(TEXT("Wall"),
		static_cast<int32>(ACataclysmTerrain::KindFromCell(TEXT("Wall"))),
		static_cast<int32>(ECataclysmTerrainKind::Wall));
	TestEqual(TEXT("Fissure"),
		static_cast<int32>(ACataclysmTerrain::KindFromCell(TEXT("Fissure"))),
		static_cast<int32>(ECataclysmTerrainKind::Fissure));
	TestEqual(TEXT("Thicket"),
		static_cast<int32>(ACataclysmTerrain::KindFromCell(TEXT("Thicket"))),
		static_cast<int32>(ECataclysmTerrainKind::Thicket));

	// AN EMPTY CELL IS THE ORDINARY CASE: 398 of the 403 rows leave no terrain.
	TestEqual(TEXT("an empty cell is no terrain"),
		static_cast<int32>(ACataclysmTerrain::KindFromCell(FString())),
		static_cast<int32>(ECataclysmTerrainKind::None));

	// AND THE PARSER TRIMS, because a Shape Params cell is split on semicolons
	// and every value after the first arrives with a leading space.
	TestEqual(TEXT("a value with spaces round it still reads"),
		static_cast<int32>(ACataclysmTerrain::KindFromCell(TEXT("  Thicket "))),
		static_cast<int32>(ECataclysmTerrainKind::Thicket));

	return true;
}

// --------------------------------------------------------------------------
// Thicket -- pins whatever walks in
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmThicketPinsWhatWalksInTest,
	"Cataclysm.Terrain.AThicketPinsWhateverWalksIntoIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmThicketPinsWhatWalksInTest::RunTest(const FString&)
{
	using namespace CataclysmTerrainTest;

	// THE SPEAR'S THICKET: "The spears stand for 12 seconds afterward, and
	// anything that walks into them is pinned as well."
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Spearman(World, FVector::ZeroVector, ECataclysmTeam::Players);

	// SEVEN SECONDS, WHICH NO ROW STATES. Thicket's own hold is 6, so a duration
	// read out of the wrong cell could not produce this one by accident.
	FScopedTerrain Spears(Spearman.Actor, ECataclysmTerrainKind::Thicket,
						  FVector::ZeroVector, FVector::ZeroVector,
						  /*SizeCm=*/4 * M, /*Duration=*/12.0f,
						  /*HoldSeconds=*/7.0f);
	if (!Spears.Actor)
	{
		AddError(TEXT("A thicket should have spawned."));
		return false;
	}

	// TWENTY METRES OUT: well outside the four metre thicket.
	FScopedFighter Wanderer(World, FVector(20 * M, 0.0f, 0.0f),
							ECataclysmTeam::Monsters);

	Spears.Actor->Sweep();
	TestFalse(TEXT("something standing outside is not pinned"),
		UCataclysmSkillEffects::IsPinned(Wanderer.Actor));
	TestEqual(TEXT("and the sweep held nobody"),
		Spears.Actor->LastSweepCount, 0);

	// AND NOW IT WALKS IN.
	Wanderer.MoveTo(FVector(1 * M, 0.0f, 0.0f));
	Spears.Actor->Sweep();

	TestTrue(TEXT("walking into the spears pins it"),
		UCataclysmSkillEffects::IsPinned(Wanderer.Actor));
	TestEqual(TEXT("and the sweep says it held one"),
		Spears.Actor->LastSweepCount, 1);

	// IT IS NOT RE-PINNED FOR STANDING STILL, which is the half that stops a
	// twelve second thicket holding a creature for twelve seconds when its row
	// says six. A pin is single stack, so refreshing it every half second would
	// do exactly that.
	Spears.Actor->Sweep();
	TestEqual(TEXT("standing still in it holds nobody again"),
		Spears.Actor->LastSweepCount, 0);

	return true;
}

// --------------------------------------------------------------------------
// Pit -- floors what falls in, and holds it there
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPitFloorsWhatFallsInTest,
	"Cataclysm.Terrain.APitKnocksDownWhateverFallsIntoIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPitFloorsWhatFallsInTest::RunTest(const FString&)
{
	using namespace CataclysmTerrainTest;

	// THE WARHAMMER'S BREAK THE WORLD: "What is left is a broken bowl for 12
	// seconds ... anything that falls back in is knocked down again."
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Hammer(World, FVector::ZeroVector, ECataclysmTeam::Players);

	FScopedTerrain Hole(Hammer.Actor, ECataclysmTerrainKind::Pit,
						FVector::ZeroVector, FVector::ZeroVector,
						/*SizeCm=*/5 * M, /*Duration=*/12.0f,
						/*HoldSeconds=*/7.0f);
	if (!Hole.Actor)
	{
		AddError(TEXT("A pit should have spawned."));
		return false;
	}

	FScopedFighter Faller(World, FVector(20 * M, 0.0f, 0.0f),
						  ECataclysmTeam::Monsters);

	Hole.Actor->Sweep();
	TestFalse(TEXT("something standing outside is not knocked down"),
		UCataclysmSkillEffects::IsKnockedDown(Faller.Actor));

	Faller.MoveTo(FVector(1 * M, 0.0f, 0.0f));
	Hole.Actor->Sweep();

	TestTrue(TEXT("falling in knocks it down"),
		UCataclysmSkillEffects::IsKnockedDown(Faller.Actor));
	TestTrue(TEXT("so it cannot act at all"),
		UCataclysmSkillEffects::CannotAct(Faller.Actor));

	// AND NOT PINNED, which says the pit took the knockdown branch and not the
	// thicket's. The two kinds differ in one line and share everything else.
	TestFalse(TEXT("but it is not pinned"),
		UCataclysmSkillEffects::IsPinned(Faller.Actor));

	// IT IS NOT FLOORED AGAIN FOR LYING THERE. "Anything that FALLS BACK IN",
	// not anything inside: a creature at the bottom of a twelve second pit must
	// not be knocked down twice a second for the whole twelve.
	Hole.Actor->Sweep();
	TestEqual(TEXT("lying in it floors nobody again"),
		Hole.Actor->LastSweepCount, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPitRefusesACharge,
	"Cataclysm.Terrain.ACreatureInAPitCannotChargeOutOfIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPitRefusesACharge::RunTest(const FString&)
{
	using namespace CataclysmTerrainTest;

	// THE WARHAMMER'S CRATER: "anything inside has to climb out: enemies in the
	// pit cannot charge or leap."
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Hammer(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter Trapped(World, FVector(1 * M, 0.0f, 0.0f),
						   ECataclysmTeam::Monsters);

	// THE CONTROL FIRST, WITH NO PIT AT ALL. Without it, a creature that failed
	// to charge for some entirely different reason would read as a working pit.
	Trapped.Actor->BeginCharge(FVector(10 * M, 0.0f, 0.0f),
							   /*SpeedCmPerSecond=*/800.0f,
							   /*HalfWidthCm=*/100.0f,
							   /*DamagePercent=*/100.0f,
							   /*KnockbackCm=*/0.0f);
	TestTrue(TEXT("with no pit it charges"), Trapped.Actor->IsCharging());
	Trapped.Actor->CancelCharge();
	TestFalse(TEXT("and the charge is cancelled again for the real test"),
		Trapped.Actor->IsCharging());

	FScopedTerrain Hole(Hammer.Actor, ECataclysmTerrainKind::Pit,
						FVector::ZeroVector, FVector::ZeroVector,
						/*SizeCm=*/5 * M, /*Duration=*/12.0f,
						/*HoldSeconds=*/7.0f);
	if (!Hole.Actor)
	{
		AddError(TEXT("A pit should have spawned."));
		return false;
	}

	TestTrue(TEXT("the creature is standing in the pit"),
		ACataclysmTerrain::IsStandingIn(Trapped.Actor,
										ECataclysmTerrainKind::Pit));

	Trapped.Actor->BeginCharge(FVector(10 * M, 0.0f, 0.0f),
							   /*SpeedCmPerSecond=*/800.0f,
							   /*HalfWidthCm=*/100.0f,
							   /*DamagePercent=*/100.0f,
							   /*KnockbackCm=*/0.0f);
	TestFalse(TEXT("standing in a pit it refuses to charge"),
		Trapped.Actor->IsCharging());

	// AND WALKING OUT GIVES THE CHARGE BACK, which says the refusal is about
	// where the creature is standing rather than a flag it now carries for ever.
	Trapped.MoveTo(FVector(20 * M, 0.0f, 0.0f));
	TestFalse(TEXT("out of the pit it is no longer standing in one"),
		ACataclysmTerrain::IsStandingIn(Trapped.Actor,
									   ECataclysmTerrainKind::Pit));

	Trapped.Actor->BeginCharge(FVector(30 * M, 0.0f, 0.0f),
							   /*SpeedCmPerSecond=*/800.0f,
							   /*HalfWidthCm=*/100.0f,
							   /*DamagePercent=*/100.0f,
							   /*KnockbackCm=*/0.0f);
	TestTrue(TEXT("and it charges again"), Trapped.Actor->IsCharging());
	Trapped.Actor->CancelCharge();

	return true;
}

// --------------------------------------------------------------------------
// Fissure -- one creature, once
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFissureIsSpentByOneCreatureTest,
	"Cataclysm.Terrain.AFissureFloorsTheNextCreatureToCrossItAndIsThenSpent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFissureIsSpentByOneCreatureTest::RunTest(const FString&)
{
	using namespace CataclysmTerrainTest;

	// THE WARHAMMER'S GROUNDBREAKER: "leaving a fissure that knocks down the NEXT
	// enemy to cross it". One creature, and then the crack is used up. A fissure
	// that stayed would floor a second creature the moment the shared stun
	// immunity window allowed, and the row opens one under every blow.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Hammer(World, FVector::ZeroVector, ECataclysmTeam::Players);

	ACataclysmTerrain* Crack = ACataclysmTerrain::Spawn(
		Hammer.Actor, ECataclysmTerrainKind::Fissure, FVector::ZeroVector,
		FVector::ZeroVector, /*SizeCm=*/2 * M, /*Duration=*/6.0f,
		/*HoldSeconds=*/0.0f);
	if (!Crack)
	{
		AddError(TEXT("A fissure should have spawned."));
		return false;
	}

	// A ROW THAT STATES NO HOLD TAKES THE DEFAULT, and Groundbreaker states none
	// because the duration it does state belongs to the self buff.
	TestEqual(TEXT("a fissure with no stated hold takes the default"),
		Crack->HoldSeconds, ACataclysmTerrain::DefaultHoldSeconds, 0.01f);

	FScopedFighter First(World, FVector(1 * M, 0.0f, 0.0f),
						 ECataclysmTeam::Monsters);
	FScopedFighter Second(World, FVector(1 * M, 50.0f, 0.0f),
						  ECataclysmTeam::Monsters);

	Crack->Sweep();

	// EXACTLY ONE OF THE TWO IS FLOORED. Which one is not the point and is not
	// asserted: the search answers nearest first, and both are inside.
	const int32 Floored =
		(UCataclysmSkillEffects::IsKnockedDown(First.Actor) ? 1 : 0)
		+ (UCataclysmSkillEffects::IsKnockedDown(Second.Actor) ? 1 : 0);
	TestEqual(TEXT("a fissure floors exactly one of the two standing on it"),
		Floored, 1);

	// AND IT IS GONE. Destroying rather than waiting out its six seconds is what
	// "the next enemy" means.
	TestFalse(TEXT("and the fissure is spent"), IsValid(Crack));

	return true;
}

// --------------------------------------------------------------------------
// Wall -- solid, and the length its row asked for
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWallIsSolidTest,
	"Cataclysm.Terrain.AWallIsRaisedSolidAndTheLengthItsRowAsksFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWallIsSolidTest::RunTest(const FString&)
{
	using namespace CataclysmTerrainTest;

	// THE WARHAMMER'S UPTHRUST: "drive a ridge of broken rock up out of the
	// ground along a 10 meter line. The ridge blocks movement and projectiles for
	// 8 seconds."
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Hammer(World, FVector::ZeroVector, ECataclysmTeam::Players);

	const FVector Start(0.0f, 0.0f, 0.0f);
	const FVector End(10 * M, 0.0f, 0.0f);
	FScopedTerrain Ridge(Hammer.Actor, ECataclysmTerrainKind::Wall, Start, End,
						 /*SizeCm=*/10 * M, /*Duration=*/8.0f,
						 /*HoldSeconds=*/0.0f);
	if (!Ridge.Actor)
	{
		AddError(TEXT("A wall should have spawned."));
		return false;
	}

	// IT HAS GEOMETRY. An instanced mesh with no instances draws nothing,
	// collides with nothing and affects no navigation, which is exactly what
	// every other kind's wall component is and what a failed raise would leave.
	const UInstancedStaticMeshComponent* Blocks =
		Ridge.Actor->FindComponentByClass<UInstancedStaticMeshComponent>();
	if (!Blocks)
	{
		AddError(TEXT("A wall should carry an instanced mesh component."));
		return false;
	}
	TestEqual(TEXT("a wall is one stretched block"), Blocks->GetInstanceCount(), 1);

	// AND IT IS SOLID. `BlockAll` is what makes "blocks movement AND projectiles"
	// true at once, and query-only collision would stop a trace and let a body
	// walk through.
	TestEqual(TEXT("and it blocks physically as well as by query"),
		static_cast<int32>(Blocks->GetCollisionEnabled()),
		static_cast<int32>(ECollisionEnabled::QueryAndPhysics));

	// AND PATHFINDING CAN SEE IT. This is the trap `ACataclysmDungeonFloor`
	// records: `bCanEverAffectNavigation` is false by default on every mesh
	// component created in C++, so a solid wall is invisible to the navigation
	// system and every creature paths straight through it while the player is
	// stopped.
	TestTrue(TEXT("and the navigation system can see it"),
		Blocks->CanEverAffectNavigation());

	// AND IT IS THE LENGTH THE ROW ASKED FOR. The instance is one unit cube
	// scaled, so its X scale is the length in hundreds of centimetres.
	FTransform Placed;
	Blocks->GetInstanceTransform(0, Placed, /*bWorldSpace=*/true);
	TestEqual(TEXT("it is ten metres long"),
		static_cast<float>(Placed.GetScale3D().X) * 100.0f, 10 * M, 1.0f);

	// AND IT STANDS BETWEEN THE TWO ENDS RATHER THAN ON ONE OF THEM. A wall
	// placed at Start would leave the far half of the line open.
	TestEqual(TEXT("and its middle is halfway along the line"),
		static_cast<float>(Placed.GetLocation().X), 5 * M, 10.0f);

	// A WALL SWEEPS FOR NOBODY. It stops things by being solid, so it holds
	// nothing and needs no timer. `Sweep` is called here to say that even driven
	// by hand it does nothing, which is what makes the switch's Wall branch
	// deliberate rather than forgotten.
	FScopedFighter Bystander(World, FVector(2 * M, 0.0f, 0.0f),
							 ECataclysmTeam::Monsters);
	Ridge.Actor->Sweep();
	TestFalse(TEXT("a wall pins nobody"),
		UCataclysmSkillEffects::IsPinned(Bystander.Actor));
	TestFalse(TEXT("and floors nobody"),
		UCataclysmSkillEffects::IsKnockedDown(Bystander.Actor));

	return true;
}

// --------------------------------------------------------------------------
// It never affects its own side
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTerrainSparesItsOwnSideTest,
	"Cataclysm.Terrain.TerrainHoldsEnemiesAndSparesItsOwnSide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTerrainSparesItsOwnSideTest::RunTest(const FString&)
{
	using namespace CataclysmTerrainTest;

	// A PLAYER WHO RAISES A THICKET MUST BE ABLE TO STAND IN IT. Every skill that
	// leaves terrain is a player skill aimed at a crowd, and one that pinned its
	// own caster the instant it landed would be unusable rather than merely
	// awkward. The burning ground beside it takes the same rule, and the project
	// owner set it as a general one on 2026-08-20: a creature does not burn
	// itself or its own side.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Spearman(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter Ally(World, FVector(1 * M, 0.0f, 0.0f), ECataclysmTeam::Players);
	FScopedFighter Foe(World, FVector(2 * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);

	FScopedTerrain Spears(Spearman.Actor, ECataclysmTerrainKind::Thicket,
						  FVector::ZeroVector, FVector::ZeroVector,
						  /*SizeCm=*/6 * M, /*Duration=*/12.0f,
						  /*HoldSeconds=*/7.0f);
	if (!Spears.Actor)
	{
		AddError(TEXT("A thicket should have spawned."));
		return false;
	}

	Spears.Actor->Sweep();

	TestTrue(TEXT("the enemy standing in it is pinned"),
		UCataclysmSkillEffects::IsPinned(Foe.Actor));
	TestFalse(TEXT("the caster standing in its own thicket is not"),
		UCataclysmSkillEffects::IsPinned(Spearman.Actor));
	TestFalse(TEXT("nor is an ally"),
		UCataclysmSkillEffects::IsPinned(Ally.Actor));
	TestEqual(TEXT("so the sweep held exactly one"),
		Spears.Actor->LastSweepCount, 1);

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
