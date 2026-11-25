// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMovement.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTerrain.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
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
 *   That a creature's walking finds no way through a raised wall. A test world is
 *   never ticked and has no navigation mesh, so the walking physics and the
 *   path a creature would choose are not run. What is checked, since issue
 *   #2878, is the move the walking physics makes each frame: a creature moved
 *   into the wall through its movement component's swept move
 *   (`UCataclysmMovement::MoveWithoutSticking`) stops at it, on a floor the test
 *   lays.
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPitResistedTest,
	"Cataclysm.Terrain.APitDoesNotFloorACreatureThatFullyResistsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPitResistedTest::RunTest(const FString&)
{
	using namespace CataclysmTerrainTest;

	// A PIT IS ONE OF THE PLACES THAT CALL `ApplyKnockdown`, and since 2026-09-17
	// a knockdown reads the target's crowd control resistance the way a stun
	// does: shortened, and at 100 not landing at all. Issue #1815.
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

	// TWO CREATURES FALL IN TOGETHER, AND ONLY THE STAT DIFFERS. The one with
	// none is the control. Both start outside and walk in, as in
	// `APitKnocksDownWhateverFallsIntoIt`.
	FScopedFighter Bare(World, FVector(20 * M, 0.0f, 0.0f),
						ECataclysmTeam::Monsters);
	FScopedFighter Immune(World, FVector(-20 * M, 0.0f, 0.0f),
						  ECataclysmTeam::Monsters);
	Immune.Actor->GetAbilitySystemComponent()->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetCrowdControlResistanceAttribute(), 100.0f);

	Bare.MoveTo(FVector(1 * M, 0.0f, 0.0f));
	Immune.MoveTo(FVector(-1 * M, 0.0f, 0.0f));
	Hole.Actor->Sweep();

	TestTrue(TEXT("a creature with no resistance that falls in is knocked down"),
		UCataclysmSkillEffects::IsKnockedDown(Bare.Actor));
	TestFalse(TEXT("and one at 100 resistance is not"),
		UCataclysmSkillEffects::IsKnockedDown(Immune.Actor));
	TestEqual(TEXT("so the sweep floored exactly one"),
		Hole.Actor->LastSweepCount, 1);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFissureResistedTest,
	"Cataclysm.Terrain.AFissureIsNotSpentOnACreatureThatFullyResistsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFissureResistedTest::RunTest(const FString&)
{
	using namespace CataclysmTerrainTest;

	// A FISSURE IS ONE OF THE PLACES THAT CALL `ApplyKnockdown`, and since
	// 2026-09-17 a knockdown reads the target's crowd control resistance the way a
	// stun does. Issue #1815.
	//
	// A FISSURE IS SPENT ONLY BY A KNOCKDOWN THAT LANDS, which is how it already
	// treated a boss, whose knockdown the boss rule refuses. So a creature it
	// cannot floor leaves it for the next creature it can.
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

	FScopedFighter Immune(World, FVector(1 * M, 0.0f, 0.0f),
						  ECataclysmTeam::Monsters);
	Immune.Actor->GetAbilitySystemComponent()->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetCrowdControlResistanceAttribute(), 100.0f);

	Crack->Sweep();

	TestFalse(TEXT("a creature at 100 resistance crossing it is not knocked down"),
		UCataclysmSkillEffects::IsKnockedDown(Immune.Actor));
	if (!TestTrue(TEXT("so the fissure is not spent"), IsValid(Crack)))
	{
		return false;
	}

	// AND THE NEXT CREATURE IT CAN FLOOR SPENDS IT, which is the control: the
	// crack was waiting, not broken.
	FScopedFighter Bare(World, FVector(1 * M, 50.0f, 0.0f),
						ECataclysmTeam::Monsters);

	Crack->Sweep();

	TestTrue(TEXT("a creature with no resistance crossing it next is knocked down"),
		UCataclysmSkillEffects::IsKnockedDown(Bare.Actor));
	TestFalse(TEXT("and that spends the fissure"), IsValid(Crack));

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

// --------------------------------------------------------------------------
// On the floor, whatever height it was handed -- issue #2878
// --------------------------------------------------------------------------

namespace CataclysmTerrainTest
{
	/**
	 * The walking surface's height in the tests that lay a floor.
	 *
	 * NOT ZERO, so nothing that assumes the floor is at the world origin can pass
	 * by accident. The figure `CataclysmEffectPlacementTests.cpp` builds at.
	 */
	constexpr float TerrainTestFloorTopZ = 500.0f;

	/**
	 * Where a player standing on that floor has its capsule centre: half its
	 * 192 cm capsule and the 2 cm the engine's character movement keeps under a
	 * capsule. Upthrust is cast from there -- `CastOrigin` is the caster's actor
	 * location -- and it is where the wall's base stood until issue #2878.
	 */
	constexpr float TerrainTestCasterCentreZ = TerrainTestFloorTopZ + 98.0f;

	/** How tall a raised wall is designed to stand: `WallHeightCm` in `CataclysmTerrain.cpp`. */
	constexpr double TerrainTestWallHeightCm = 400.0;

	/** How thick a raised wall is: `WallThicknessCm` in `CataclysmTerrain.cpp`. */
	constexpr double TerrainTestWallThicknessCm = 100.0;

	/** Out past the edge of the floor below, which is 40 metres square. */
	constexpr float TerrainTestPastTheEdgeX = 5000.0f;

	/**
	 * How far above the floor a body is put down: clear of it, so the swept move
	 * below does not begin touching the ground it walks on.
	 */
	constexpr float TerrainTestStandingGapCm = 3.0f;

	/**
	 * A solid floor 40 metres square and 40 cm thick with its top at
	 * `TerrainTestFloorTopZ`.
	 *
	 * BUILT AS `CataclysmAbyssalWardenTests.cpp` BUILDS ITS GROUND: a box set to
	 * the world-static object type and blocking every channel, because the search
	 * for the floor asks for world-static objects and the dungeon's ground is one.
	 */
	static AActor* LayTheTerrainTestFloor(UWorld* World)
	{
		const FVector Middle(0.0f, 0.0f, TerrainTestFloorTopZ - 20.0f);
		AActor* Floor = World->SpawnActor<AActor>(Middle, FRotator::ZeroRotator);
		if (!Floor)
		{
			return nullptr;
		}

		UBoxComponent* Body = NewObject<UBoxComponent>(Floor);
		Body->InitBoxExtent(FVector(2000.0f, 2000.0f, 20.0f));
		Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Body->SetCollisionObjectType(ECC_WorldStatic);
		Body->SetCollisionResponseToAllChannels(ECR_Block);
		Floor->SetRootComponent(Body);
		Body->RegisterComponent();
		Floor->SetActorLocationAndRotation(Middle, FRotator::ZeroRotator);
		return Floor;
	}

	/**
	 * The bottom and top of a wall's one block, in world space, read off the
	 * instance the collision is made of rather than off the actor.
	 *
	 * @return false when the wall is not exactly one block
	 */
	static bool TerrainTestWallSpan(ACataclysmTerrain* Wall, double& OutBottomZ, double& OutTopZ)
	{
		const UInstancedStaticMeshComponent* Blocks = Wall
			? Wall->FindComponentByClass<UInstancedStaticMeshComponent>()
			: nullptr;
		if (!Blocks || Blocks->GetInstanceCount() != 1)
		{
			return false;
		}

		FTransform Placed;
		if (!Blocks->GetInstanceTransform(0, Placed, /*bWorldSpace=*/true))
		{
			return false;
		}

		// THE ENGINE'S UNIT CUBE IS 100 CM A SIDE AND CENTRED ON ITS ORIGIN, so half
		// its height is fifty times its Z scale.
		const double HalfHeight = Placed.GetScale3D().Z * 50.0;
		OutBottomZ = Placed.GetLocation().Z - HalfHeight;
		OutTopZ = Placed.GetLocation().Z + HalfHeight;
		return true;
	}

	/** Put a body down standing on that floor at X and Y, clear of it by the standing gap. */
	static FVector TerrainTestStandingAt(const ACataclysmEnemyCharacter* Body, float X, float Y)
	{
		const UCapsuleComponent* Capsule = Body ? Body->GetCapsuleComponent() : nullptr;
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.0f;
		return FVector(X, Y, TerrainTestFloorTopZ + HalfHeight + TerrainTestStandingGapCm);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWallStandsOnTheFloorTest,
	"Cataclysm.Terrain.AWallRaisedFromACapsuleCentreStandsOnTheFloorUnderIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWallStandsOnTheFloorTest::RunTest(const FString&)
{
	using namespace CataclysmTerrainTest;

	// ISSUE #2878. Upthrust hands `ACataclysmTerrain::Spawn` its caster's capsule
	// centre, and the wall used to be built up from there, so its base stood
	// about 98 cm above the floor and its top about 498. It is collision, so that
	// gap was something a short body or a low shot passed under.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	if (!TestNotNull(TEXT("set-up: a floor to raise it on"), LayTheTerrainTestFloor(World)))
	{
		return false;
	}

	// THE CASTER STANDS AT THE NEAR END, as it does for Upthrust.
	FScopedFighter Hammer(World, FVector(0.0f, 0.0f, TerrainTestCasterCentreZ),
						  ECataclysmTeam::Players);

	// UPTHRUST'S OWN TWO ENDS: the caster's capsule centre, and ten metres along
	// its facing at the same height.
	FScopedTerrain Ridge(Hammer.Actor, ECataclysmTerrainKind::Wall,
						 FVector(0.0f, 0.0f, TerrainTestCasterCentreZ),
						 FVector(10 * M, 0.0f, TerrainTestCasterCentreZ),
						 /*SizeCm=*/10 * M, /*Duration=*/8.0f,
						 /*HoldSeconds=*/0.0f);
	if (!TestNotNull(TEXT("set-up: a wall is raised"), Ridge.Actor))
	{
		return false;
	}

	double BottomZ = 0.0;
	double TopZ = 0.0;
	if (!TestTrue(TEXT("set-up: the wall is one block"),
				  TerrainTestWallSpan(Ridge.Actor, BottomZ, TopZ)))
	{
		return false;
	}

	const double BaseAboveFloor = BottomZ - TerrainTestFloorTopZ;
	const double TopAboveFloor = TopZ - TerrainTestFloorTopZ;
	AddInfo(FString::Printf(
		TEXT("UPTHRUST ON A FLOOR AT %.0f CM, CAST FROM %.0f CM: the wall's base is %.1f cm above the floor and its "
			 "top %.1f cm above it"),
		TerrainTestFloorTopZ, TerrainTestCasterCentreZ, BaseAboveFloor, TopAboveFloor));

	// THE ASSERTION THE ISSUE IS ABOUT.
	TestTrue(FString::Printf(
		TEXT("the wall's base is within 5 cm of the floor's surface, not at the caster's capsule centre: %.1f cm"),
		BaseAboveFloor),
		FMath::Abs(BaseAboveFloor) <= 5.0);

	// AND IT IS STILL ITS DESIGNED HEIGHT, MEASURED FROM THE FLOOR. Moving the base
	// down by shortening the wall would pass the line above and leave the top
	// where it was.
	TestTrue(FString::Printf(
		TEXT("and its top is its designed %.0f cm above the floor, within 5 cm: %.1f cm"),
		TerrainTestWallHeightCm, TopAboveFloor),
		FMath::Abs(TopAboveFloor - TerrainTestWallHeightCm) <= 5.0);

	// THE ACTOR IS WHERE THE WALL IS, so what the outliner reports, `IsStandingIn`
	// and the far end all agree with the collision.
	TestEqual(TEXT("the wall's actor is on the floor too"),
		static_cast<float>(Ridge.Actor->GetActorLocation().Z), TerrainTestFloorTopZ, 0.5f);
	TestEqual(TEXT("and so is its far end"),
		static_cast<float>(Ridge.Actor->FarEnd.Z), TerrainTestFloorTopZ, 0.5f);

	// ONLY THE HEIGHT MOVED. A wall moved sideways or shortened onto the floor
	// would be in the wrong place however well it stood.
	TestEqual(TEXT("it still starts where it was cast: X"),
		static_cast<float>(Ridge.Actor->GetActorLocation().X), 0.0f, 0.01f);
	TestEqual(TEXT("it still starts where it was cast: Y"),
		static_cast<float>(Ridge.Actor->GetActorLocation().Y), 0.0f, 0.01f);
	TestEqual(TEXT("and it is still ten metres long"),
		static_cast<float>(FVector::Dist2D(Ridge.Actor->GetActorLocation(), Ridge.Actor->FarEnd)),
		10 * M, 1.0f);

	// THE CONTROL: with no floor under the near end it is left where it was asked,
	// which is what every test world that builds no geometry relies on.
	FScopedTerrain Unfloored(Hammer.Actor, ECataclysmTerrainKind::Wall,
							 FVector(TerrainTestPastTheEdgeX, 0.0f, TerrainTestCasterCentreZ),
							 FVector(TerrainTestPastTheEdgeX + 10 * M, 0.0f, TerrainTestCasterCentreZ),
							 /*SizeCm=*/10 * M, /*Duration=*/8.0f,
							 /*HoldSeconds=*/0.0f);
	double UnflooredBottomZ = 0.0;
	double UnflooredTopZ = 0.0;
	if (TestNotNull(TEXT("CONTROL: set-up: a wall is raised past the floor's edge"), Unfloored.Actor)
		&& TestTrue(TEXT("CONTROL: set-up: it is one block"),
					TerrainTestWallSpan(Unfloored.Actor, UnflooredBottomZ, UnflooredTopZ)))
	{
		TestTrue(FString::Printf(
			TEXT("CONTROL: a wall with no floor under it stands where it was asked: base at %.1f against %.0f"),
			UnflooredBottomZ, TerrainTestCasterCentreZ),
			FMath::Abs(UnflooredBottomZ - TerrainTestCasterCentreZ) <= 0.5);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWallStopsAWalkerTest,
	"Cataclysm.Terrain.ARaisedWallStopsACreatureWalkingIntoIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWallStopsAWalkerTest::RunTest(const FString&)
{
	using namespace CataclysmTerrainTest;

	// UPTHRUST'S RIDGE "BLOCKS MOVEMENT AND PROJECTILES". Two bodies walk into it:
	// an ordinary creature, 160 cm tall, and one as short as a summoned minion
	// (`CataclysmMinion.cpp`'s capsule, 30 cm by a 45 cm half height), the
	// shortest body in the game. While the wall's base stood at the caster's
	// capsule centre, 98 cm up, the short one walked underneath it. Issue #2878.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	if (!TestNotNull(TEXT("set-up: a floor to walk on"), LayTheTerrainTestFloor(World)))
	{
		return false;
	}

	// THE CASTER, OFF TO ONE SIDE of both walkers' way.
	FScopedFighter Hammer(World, FVector(0.0f, -8 * M, TerrainTestCasterCentreZ),
						  ECataclysmTeam::Players);

	FScopedFighter Walker(World, FVector(-3 * M, 0.0f, TerrainTestCasterCentreZ),
						  ECataclysmTeam::Monsters);
	FScopedFighter ShortOne(World, FVector(-3 * M, 3 * M, TerrainTestCasterCentreZ),
						 ECataclysmTeam::Monsters);
	UCapsuleComponent* WalkerCapsule = Walker.Actor->GetCapsuleComponent();
	UCapsuleComponent* ShortCapsule = ShortOne.Actor->GetCapsuleComponent();
	if (!TestNotNull(TEXT("set-up: the creature has a capsule"), WalkerCapsule)
		|| !TestNotNull(TEXT("set-up: so does the short one"), ShortCapsule))
	{
		return false;
	}
	ShortCapsule->SetCapsuleSize(30.0f, 45.0f);

	const FVector WalkerFrom = TerrainTestStandingAt(Walker.Actor, -3 * M, 0.0f);
	const FVector ShortFrom = TerrainTestStandingAt(ShortOne.Actor, -3 * M, 3 * M);
	Walker.Actor->SetActorLocation(WalkerFrom);
	ShortOne.Actor->SetActorLocation(ShortFrom);

	// SIX METRES STRAIGHT ON, through where the wall will be. Through the
	// movement component's swept move, which is the move the walking physics
	// makes each frame and stops at whatever blocks the capsule.
	const FVector Walk(6 * M, 0.0f, 0.0f);

	// THE CONTROL FIRST, WITH NO WALL. Without it, a body that failed to move for
	// some other reason -- stuck on the floor it starts on -- would read as a
	// wall that blocks.
	UCataclysmMovement::MoveWithoutSticking(Walker.Actor, Walk);
	UCataclysmMovement::MoveWithoutSticking(ShortOne.Actor, Walk);
	TestEqual(TEXT("CONTROL: with no wall the creature walks the whole six metres"),
		static_cast<float>(Walker.Actor->GetActorLocation().X), 3 * M, 1.0f);
	TestEqual(TEXT("CONTROL: and so does the short one"),
		static_cast<float>(ShortOne.Actor->GetActorLocation().X), 3 * M, 1.0f);
	Walker.Actor->SetActorLocation(WalkerFrom);
	ShortOne.Actor->SetActorLocation(ShortFrom);

	// THE WALL, RAISED AS UPTHRUST RAISES IT: from a caster's capsule centre,
	// across both walkers' way, along Y at X = 0.
	FScopedTerrain Ridge(Hammer.Actor, ECataclysmTerrainKind::Wall,
						 FVector(0.0f, -5 * M, TerrainTestCasterCentreZ),
						 FVector(0.0f, 5 * M, TerrainTestCasterCentreZ),
						 /*SizeCm=*/10 * M, /*Duration=*/8.0f,
						 /*HoldSeconds=*/0.0f);
	if (!TestNotNull(TEXT("set-up: a wall is raised across the way"), Ridge.Actor))
	{
		return false;
	}

	UCataclysmMovement::MoveWithoutSticking(Walker.Actor, Walk);
	UCataclysmMovement::MoveWithoutSticking(ShortOne.Actor, Walk);

	// MEASURED AT EACH BODY'S LEADING EDGE against the wall's near face, which is
	// half the wall's thickness short of X = 0.
	const double NearFaceX = -TerrainTestWallThicknessCm * 0.5;
	const double WalkerFront =
		Walker.Actor->GetActorLocation().X + WalkerCapsule->GetScaledCapsuleRadius();
	const double ShortFront =
		ShortOne.Actor->GetActorLocation().X + ShortCapsule->GetScaledCapsuleRadius();
	AddInfo(FString::Printf(
		TEXT("WALKING INTO A RAISED WALL: the creature's front stopped at X %.1f and the short one's at X %.1f; the "
			 "wall's near face is at X %.0f"),
		WalkerFront, ShortFront, NearFaceX));

	// STOPPED AT THE WALL, NOT SHORT OF IT AND NOT THROUGH IT. Up to 10 cm short
	// allows for the engine's pull-back from a blocking hit; any more would be a
	// body stopped by something other than the wall.
	TestTrue(FString::Printf(
		TEXT("a creature walking into the wall is stopped at it: its front is %.1f cm short of the wall's near face"),
		NearFaceX - WalkerFront),
		WalkerFront <= NearFaceX + 1.0 && WalkerFront >= NearFaceX - 10.0);
	TestTrue(FString::Printf(
		TEXT("and so is one as short as a summoned minion, which passed under the wall while its base stood at the "
			 "caster's capsule centre: its front is %.1f cm short of the near face"),
		NearFaceX - ShortFront),
		ShortFront <= NearFaceX + 1.0 && ShortFront >= NearFaceX - 10.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPitOnTheFloorTest,
	"Cataclysm.Terrain.APitLeftAtACapsuleCentreLiesOnTheFloorAndStillFloorsWhatFallsIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPitOnTheFloorTest::RunTest(const FString&)
{
	using namespace CataclysmTerrainTest;

	// EVERY KIND OF TERRAIN IS PUT ON THE FLOOR, not only the wall, so a pit's
	// actor is no longer at its caster's capsule centre. The round kinds hold
	// whoever is inside them by a flat test, so this says the move changed where
	// the pit is and not who it holds -- at its middle and near its rim, for bodies
	// standing on the floor. Issue #2878.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	if (!TestNotNull(TEXT("set-up: a floor to break open"), LayTheTerrainTestFloor(World)))
	{
		return false;
	}

	FScopedFighter Hammer(World, FVector(0.0f, 0.0f, TerrainTestCasterCentreZ),
						  ECataclysmTeam::Players);

	// BREAK THE WORLD AND CRATER BOTH LEAVE THEIR PIT AT A CAPSULE CENTRE.
	FScopedTerrain Hole(Hammer.Actor, ECataclysmTerrainKind::Pit,
						FVector(0.0f, 0.0f, TerrainTestCasterCentreZ),
						FVector(0.0f, 0.0f, TerrainTestCasterCentreZ),
						/*SizeCm=*/5 * M, /*Duration=*/12.0f,
						/*HoldSeconds=*/7.0f);
	if (!TestNotNull(TEXT("set-up: a pit is left"), Hole.Actor))
	{
		return false;
	}

	TestEqual(TEXT("the pit lies on the floor, not at its caster's capsule centre"),
		static_cast<float>(Hole.Actor->GetActorLocation().Z), TerrainTestFloorTopZ, 0.5f);
	TestEqual(TEXT("and it is still round: its far end is its own place"),
		Hole.Actor->FarEnd, Hole.Actor->GetActorLocation());

	// TWO CREATURES OUTSIDE, ON THE FLOOR, then both walk in: one near the middle
	// and one 20 cm inside the five metre rim. The sweep gathers with a sphere
	// centred on the pit's actor, now on the floor rather than at waist height,
	// and the rim is where such a sphere would miss a standing body first if it
	// missed any.
	FScopedFighter NearTheMiddle(World, FVector(20 * M, 0.0f, TerrainTestCasterCentreZ),
								 ECataclysmTeam::Monsters);
	FScopedFighter Rim(World, FVector(-20 * M, 0.0f, TerrainTestCasterCentreZ),
					   ECataclysmTeam::Monsters);
	NearTheMiddle.MoveTo(TerrainTestStandingAt(NearTheMiddle.Actor, 20 * M, 0.0f));
	Rim.MoveTo(TerrainTestStandingAt(Rim.Actor, -20 * M, 0.0f));

	Hole.Actor->Sweep();
	TestEqual(TEXT("standing outside on the floor, nobody is held"),
		Hole.Actor->LastSweepCount, 0);

	NearTheMiddle.MoveTo(TerrainTestStandingAt(NearTheMiddle.Actor, 1 * M, 0.0f));
	Rim.MoveTo(TerrainTestStandingAt(Rim.Actor, -4.8f * M, 0.0f));
	Hole.Actor->Sweep();

	TestTrue(TEXT("a creature on the floor that walks into the middle is knocked down"),
		UCataclysmSkillEffects::IsKnockedDown(NearTheMiddle.Actor));
	TestTrue(TEXT("and so is one that walks in just inside the rim"),
		UCataclysmSkillEffects::IsKnockedDown(Rim.Actor));
	TestEqual(TEXT("so the sweep floored both"),
		Hole.Actor->LastSweepCount, 2);

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
