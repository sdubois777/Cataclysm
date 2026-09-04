// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmTerrain.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Cataclysm.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/**
	 * The placeholder shape a wall is built out of.
	 *
	 * THE SAME ONE `ACataclysmDungeonFloor` USES, deliberately. This project has
	 * no art content, so both are the engine's own unit cube scaled to size, and
	 * a wall a skill raises looks like a wall the dungeon generated.
	 */
	const TCHAR* const BlockMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

	/** How tall a raised wall stands, in centimetres. */
	constexpr float WallHeightCm = 400.0f;

	/** How thick it is, in centimetres. */
	constexpr float WallThicknessCm = 100.0f;

	/** The engine's unit cube is 100 centimetres on each side. */
	constexpr float CubeSideCm = 100.0f;
}

ACataclysmTerrain::ACataclysmTerrain()
{
	// Nothing to do per frame. A zone sweeps on a timer and a wall does nothing
	// at all after it is raised.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// See the header: without a root the actor has no position and every sweep
	// happens around the world origin.
	Anchor = CreateDefaultSubobject<USceneComponent>(TEXT("Anchor"));
	SetRootComponent(Anchor);

	Blocks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Blocks"));
	Blocks->SetupAttachment(Anchor);

	// MOVABLE, AND `ACataclysmDungeonFloor` RECORDS WHY STATIC IS WRONG HERE. A
	// static component's transform does not follow its actor, so terrain placed
	// anywhere other than the world origin would have its geometry somewhere
	// else entirely. Static mobility buys precomputation, and nothing that does
	// not exist until the game runs can be precomputed against.
	Blocks->SetMobility(EComponentMobility::Movable);

	// COLLISION IS THE WHOLE POINT OF A WALL. Upthrust's ridge "blocks movement
	// and projectiles", and BlockAll is what makes both true at once.
	Blocks->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Blocks->SetCollisionProfileName(TEXT("BlockAll"));

	// AND SAYING SO, BECAUSE COLLISION ALONE IS NOT ENOUGH AND THE DEFAULT IS NO.
	// `bCanEverAffectNavigation` is false in both `UActorComponent`'s constructor
	// and `UPrimitiveComponent`'s, and no mesh component turns it back on, so a
	// component created in C++ is invisible to the navigation system however
	// solid it is. Without this a raised wall would stop the player and let every
	// creature path straight through it.
	Blocks->SetCanEverAffectNavigation(true);

	// EMPTY FOR EVERY KIND BUT A WALL. The component exists on all of them
	// because a constructor cannot know which kind this will be -- `Spawn` sets
	// that afterwards -- and an instanced mesh with no instances draws nothing,
	// collides with nothing and affects no navigation.
	//
	// `ConstructorHelpers::FObjectFinder` RATHER THAN `LoadObject`: a constructor
	// runs while the class default object is being created, and a raw load during
	// that pass can assert with "Illegal call to StaticFindObject() while
	// serializing object data". `ACataclysmDungeonFloor` carries the same note.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BlockMesh(BlockMeshPath);
	if (BlockMesh.Succeeded())
	{
		Blocks->SetStaticMesh(BlockMesh.Object);
	}
}

ECataclysmTerrainKind ACataclysmTerrain::KindFromCell(const FString& Cell)
{
	const FString Word = Cell.TrimStartAndEnd();
	if (Word.Equals(TEXT("Pit"), ESearchCase::IgnoreCase))
	{
		return ECataclysmTerrainKind::Pit;
	}
	if (Word.Equals(TEXT("Wall"), ESearchCase::IgnoreCase))
	{
		return ECataclysmTerrainKind::Wall;
	}
	if (Word.Equals(TEXT("Fissure"), ESearchCase::IgnoreCase))
	{
		return ECataclysmTerrainKind::Fissure;
	}
	if (Word.Equals(TEXT("Thicket"), ESearchCase::IgnoreCase))
	{
		return ECataclysmTerrainKind::Thicket;
	}

	// AN EMPTY CELL IS THE ORDINARY CASE and is not worth a warning: 398 of the
	// 403 rows in the sheet leave no terrain at all. A cell holding a word this
	// build does not know is different, and the generator's closed list is what
	// should have refused it.
	if (!Word.IsEmpty())
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("A skill states Terrain='%s', which is not one of Pit, Wall, "
				 "Fissure or Thicket. No terrain was left."), *Word);
	}
	return ECataclysmTerrainKind::None;
}

ACataclysmTerrain* ACataclysmTerrain::Spawn(
	AActor* Owner, ECataclysmTerrainKind Kind, const FVector& Start,
	const FVector& End, float SizeCm, float Duration, float HoldSeconds)
{
	if (!IsValid(Owner) || Kind == ECataclysmTerrainKind::None
		|| SizeCm <= 0.0f || Duration <= 0.0f)
	{
		return nullptr;
	}

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// SPAWNED IN TWO STEPS, AND A ONE-STEP SPAWN IS WRONG HERE IN A WAY THAT IS
	// SILENT. `UWorld::SpawnActor` runs `BeginPlay` before it returns, in any
	// world that has already begun play -- which is every world the game runs in.
	// So every property set on the line after it is set too late for `BeginPlay`
	// to see, and `BuildWall` read a kind of `None`, a far end at the world
	// origin and a radius of zero, and raised nothing at all.
	//
	// IT COST ONE TEST RUN TO FIND AND WOULD HAVE COST NOTHING TO MISS. The three
	// sweeping kinds work either way, because their timer only reads these
	// members when it fires, long after `Spawn` has returned. Only the wall does
	// work at `BeginPlay`, so only the wall noticed.
	const FTransform Where(FRotator::ZeroRotator, Start);
	ACataclysmTerrain* Terrain = World->SpawnActorDeferred<ACataclysmTerrain>(
		ACataclysmTerrain::StaticClass(), Where, Owner, /*Instigator=*/nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Terrain)
	{
		return nullptr;
	}

	Terrain->Kind = Kind;

	// A WALL'S SIZE IS ITS LENGTH AND EVERY OTHER KIND'S IS ITS RADIUS, which is
	// what the parameter's own header says: "radius for a pit, fissure or
	// thicket, length for a wall". So a wall runs from Start to End and is as
	// thick as it is, and the rest are circles of SizeCm.
	Terrain->RadiusCm = Kind == ECataclysmTerrainKind::Wall
		? WallThicknessCm / 2.0f
		: SizeCm;

	// READ BACK FROM THE ACTOR RATHER THAN TRUSTING `Start`, so the near end is
	// wherever the actor actually is and the two ends cannot disagree.
	//
	// AND THAT IS EXACT RATHER THAN A GUARD. `AlwaysSpawn` does no adjusting, and
	// a deferred spawn has not reached the point where it would anyway, so this
	// reads back the position it was just given. It is written this way so this
	// actor and `ACataclysmGroundZone::SpawnAlong` read alike rather than because
	// either one needs it. That was the wording's original reason and it held
	// while the ground zone still spawned in one step; since issue #1153 both
	// spawn in two.
	Terrain->FarEnd = Terrain->GetActorLocation() + (End - Start);

	Terrain->HoldSeconds = HoldSeconds > 0.0f ? HoldSeconds : DefaultHoldSeconds;

	Terrain->SetLifeSpan(Duration);

	// AND NOW IT BEGINS PLAY, with everything above already set. This is the
	// second half of the deferred spawn and the reason for it: `BuildWall` runs
	// inside here and needs the kind, the two ends and the duration.
	Terrain->FinishSpawning(Where);

	return Terrain;
}

bool ACataclysmTerrain::IsStandingIn(const AActor* Actor,
									 ECataclysmTerrainKind Kind)
{
	if (!IsValid(Actor) || Kind == ECataclysmTerrainKind::None)
	{
		return false;
	}

	const UWorld* World = Actor->GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector Where = Actor->GetActorLocation();
	for (TActorIterator<ACataclysmTerrain> It(World); It; ++It)
	{
		const ACataclysmTerrain* Terrain = *It;
		if (!IsValid(Terrain) || Terrain->Kind != Kind)
		{
			continue;
		}

		// THE SAME CONTAINMENT TEST THE SWEEP USES, so a creature the sweep
		// floored cannot also be one this says is standing outside.
		if (UCataclysmTargeting::IsInLine(Terrain->GetActorLocation(),
										  Terrain->FarEnd, Where,
										  Terrain->RadiusCm))
		{
			return true;
		}
	}

	return false;
}

void ACataclysmTerrain::BeginPlay()
{
	Super::BeginPlay();

	if (Kind == ECataclysmTerrainKind::Wall)
	{
		// A WALL STOPS THINGS BY BEING SOLID AND NEEDS NO SWEEP AT ALL. Raising
		// it is the whole of what it does.
		BuildWall();
		return;
	}

	if (UWorld* World = GetWorld())
	{
		// FIRST SWEEP A FULL TICK IN RATHER THAN AT ONCE, the same as the burning
		// ground beside it. Terrain is left by a skill that has usually just hit
		// everyone standing there, and a pit that floored them again in the same
		// instant would spend its knockdown on the blow that made it.
		//
		// AND THAT DELAY IS WHAT MAKES `InsideLastSweep` START EMPTY MEAN THE
		// RIGHT THING. Everything found on the first sweep counts as having
		// entered, which is correct for a fissure and for a thicket. For a pit it
		// means whatever is still standing in the hole half a second after it
		// opened is knocked down once, which is what "the floor collapses ...
		// knocking down everything standing on it" describes.
		World->GetTimerManager().SetTimer(
			SweepTimer, this, &ACataclysmTerrain::Sweep,
			TickSeconds, /*bLoop=*/true, /*InFirstDelay=*/TickSeconds);
	}
}

void ACataclysmTerrain::BuildWall()
{
	if (!Blocks || !Blocks->GetStaticMesh())
	{
		// The engine's basic shapes are missing, which is a broken installation
		// rather than a case to handle. Saying so beats raising nothing quietly.
		UE_LOG(LogCataclysm, Warning,
			TEXT("A wall could not be raised: the placeholder cube at '%s' did "
				 "not load."), BlockMeshPath);
		return;
	}

	const FVector Start = GetActorLocation();
	const FVector Along = FarEnd - Start;
	const float LengthCm = Along.Size2D();
	if (LengthCm <= 0.0f)
	{
		// A wall of no length is not a wall. Upthrust states a ten metre line, so
		// reaching here means the caller passed one point twice.
		UE_LOG(LogCataclysm, Warning,
			TEXT("A wall was asked for with both ends at the same point, so it "
				 "has no length and was not raised."));
		return;
	}

	// ONE STRETCHED CUBE RATHER THAN A ROW OF THEM. A row would collide with
	// itself at every seam and would put as many instances in the world as the
	// wall is long over the block size, for a shape a single scaled box already
	// describes exactly. `ACataclysmDungeonFloor` places a row because its wall
	// follows a grid of cells and turns corners; a raised ridge is one straight
	// line and does not.
	const FVector Middle = Start + Along * 0.5f;
	const FRotator Facing = Along.GetSafeNormal2D().Rotation();
	const FVector Scale(LengthCm / CubeSideCm,
						WallThicknessCm / CubeSideCm,
						WallHeightCm / CubeSideCm);

	// RAISED FROM THE GROUND UP, so the ridge stands on the floor rather than
	// being buried to its middle in it. The cube's origin is its centre, so half
	// its height is where the bottom face sits.
	const FVector Lifted(Middle.X, Middle.Y, Middle.Z + WallHeightCm * 0.5f);

	// RELATIVE TO THE ACTOR, because the component is attached to it. Passing a
	// world transform here would place the wall at that offset from itself.
	const FTransform Local(Facing, Lifted - Start, Scale);
	Blocks->AddInstance(Local);

	UE_LOG(LogCataclysm, Verbose,
		TEXT("A wall %.0f cm long was raised for %.1f seconds."),
		LengthCm, GetLifeSpan());
}

void ACataclysmTerrain::Sweep()
{
	// Named Source rather than Instigator: AActor already has a member of that
	// name, and shadowing it is an error at this project's warning level.
	AActor* Source = GetOwner();
	if (!IsValid(Source))
	{
		LastSweepCount = 0;
		return;
	}

	// ASKED AFRESH EVERY SWEEP, the same as the burning ground. Who is standing
	// in terrain is a question about now: walking in has to start it and walking
	// out has to stop it.
	//
	// ONE SEARCH FOR BOTH SHAPES. `FindEnemiesInLine` with two ends at the same
	// point is a circle, because `IsInLine` treats a segment of no length that
	// way, so a round kind and a long one cannot drift apart in behaviour.
	const TArray<AActor*> Inside = UCataclysmTargeting::FindEnemiesInLine(
		GetWorld(), Source, GetActorLocation(), FarEnd, RadiusCm);

	int32 Held = 0;
	for (AActor* Target : Inside)
	{
		// WAS IT ALREADY IN HERE LAST TIME? A pit floors "anything that falls
		// back in", so a creature that has been standing in the hole since it
		// opened must not be floored twice a second for its whole twelve. This is
		// what turns "is inside" into "just entered".
		const bool bWasInside = InsideLastSweep.ContainsByPredicate(
			[Target](const TWeakObjectPtr<AActor>& Was)
			{
				return Was.Get() == Target;
			});

		switch (Kind)
		{
		case ECataclysmTerrainKind::Thicket:
			// "ANYTHING THAT WALKS INTO THEM IS PINNED AS WELL." Applied on
			// entry rather than on every sweep, so a creature that fights its
			// way out and comes back is pinned again while one that stays put
			// keeps the pin it already has and is not silently refreshed for
			// ever. A pin is single stack, so refreshing would hold a creature
			// standing still for the whole twelve seconds rather than the six
			// its row states.
			if (!bWasInside
				&& UCataclysmSkillEffects::ApplyPin(Source, Target, HoldSeconds))
			{
				++Held;
			}
			break;

		case ECataclysmTerrainKind::Pit:
			// "ANYTHING THAT FALLS BACK IN IS KNOCKED DOWN AGAIN." A designed
			// knockdown, so it skips the damage threshold; it still takes the
			// five second window it shares with the stun, which is what stops a
			// creature at the bottom of a pit being floored for ever even if it
			// keeps crossing the rim.
			if (!bWasInside
				&& UCataclysmSkillEffects::ApplyKnockdown(
					Source, Target, HoldSeconds, /*DamageDealt=*/0.0f,
					/*bKnockdownIsDesigned=*/true))
			{
				++Held;
			}
			break;

		case ECataclysmTerrainKind::Fissure:
			// "KNOCKS DOWN THE NEXT ENEMY TO CROSS IT" -- one creature, and then
			// the crack is spent. Groundbreaker opens one under every blow it
			// lands over ten seconds and says "there is no limit to how many you
			// may open", so each one being worth a single knockdown is what
			// stops a Support skill flooring a room continuously.
			if (UCataclysmSkillEffects::ApplyKnockdown(
					Source, Target, HoldSeconds, /*DamageDealt=*/0.0f,
					/*bKnockdownIsDesigned=*/true))
			{
				++Held;
				LastSweepCount = Held;
				++TicksElapsed;

				// SPENT, SO IT GOES. Destroying rather than waiting out the
				// duration is what "the NEXT enemy" means: a fissure that stayed
				// would floor a second creature the moment the window allowed.
				Destroy();
				return;
			}
			break;

		case ECataclysmTerrainKind::Wall:
		case ECataclysmTerrainKind::None:
		default:
			// A wall has no timer, so this is unreachable for one. Listed rather
			// than left to the default so that adding a fifth kind is a compiler
			// error here instead of a kind that silently does nothing.
			break;
		}
	}

	// REMEMBERED AFTER ACTING, not before, so this sweep's decisions are made
	// against the last one.
	InsideLastSweep.Reset();
	InsideLastSweep.Reserve(Inside.Num());
	for (AActor* Target : Inside)
	{
		InsideLastSweep.Add(Target);
	}

	LastSweepCount = Held;
	++TicksElapsed;
}
