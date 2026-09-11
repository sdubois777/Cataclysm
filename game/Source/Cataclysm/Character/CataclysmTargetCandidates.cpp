// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmTargetCandidates.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmCharacterBase.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ProfilingDebugging/CsvProfiler.h"

// THE CATEGORY THE THINKING IS RECORDED UNDER, defined in
// CataclysmEnemyController.cpp, so that a capture shows the rebuilds beside the
// thinking passes and the time they took.
CSV_DECLARE_CATEGORY_EXTERN(CataclysmAI);

UCataclysmTargetCandidates* UCataclysmTargetCandidates::In(const UWorld* World)
{
	return World ? World->GetSubsystem<UCataclysmTargetCandidates>() : nullptr;
}

void UCataclysmTargetCandidates::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// A SPAWN MARKS THE LISTS OUT OF DATE, AND THAT IS ALL IT DOES. Nothing is
	// added here: the next search rebuilds the lists by asking the world, so there
	// is one way a character gets into them.
	//
	// A DESTRUCTION MARKS NOTHING. See the class comment: the entry of a destroyed
	// character reads as nothing, so removing a body costs no rebuild.
	if (UWorld* World = GetWorld())
	{
		SpawnedHandle = World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(
				this, &UCataclysmTargetCandidates::NoteCharacterSpawned));
	}
}

void UCataclysmTargetCandidates::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->RemoveOnActorSpawnedHandler(SpawnedHandle);
	}

	Sides.Reset();
	WatchedAbilitySystems.Reset();

	Super::Deinitialize();
}

void UCataclysmTargetCandidates::NoteCharacterSpawned(AActor* Actor)
{
	// ONLY A CHARACTER IS LISTED, so only a character's spawn can make the lists
	// wrong. A Horde fight spawns projectiles, burning ground and loot all the
	// time, and none of those should cost a rebuild.
	if (Cast<ACataclysmCharacterBase>(Actor))
	{
		++Changes;
	}
}

void UCataclysmTargetCandidates::NoteMadnessChanged(const FGameplayTag Tag, int32 NewCount)
{
	++Changes;
}

void UCataclysmTargetCandidates::WatchForMadnessOn(ACataclysmCharacterBase* Character,
												   const FGameplayTag& Madness)
{
	UAbilitySystemComponent* AbilitySystem = UCataclysmTargeting::AbilitySystemOf(Character);
	if (!AbilitySystem || !Madness.IsValid())
	{
		return;
	}

	// ONCE PER ABILITY SYSTEM, WHICH IS NOT ONCE PER CHARACTER. A player
	// character's ability system lives on the player state and is found through
	// the character, so the character is listed before the state exists and is
	// watched from the first rebuild after it does.
	bool bAlreadyWatched = false;
	WatchedAbilitySystems.Add(FObjectKey(AbilitySystem), &bAlreadyWatched);
	if (!bAlreadyWatched)
	{
		AbilitySystem->RegisterGameplayTagEvent(Madness, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &UCataclysmTargetCandidates::NoteMadnessChanged);
	}
}

void UCataclysmTargetCandidates::BuildTheListsIfStale()
{
	UWorld* World = GetWorld();
	const double Seconds = World ? World->GetTimeSeconds() : 0.0;
	if (BuiltOnFrame == GFrameCounter && BuiltAtSeconds == Seconds
		&& BuiltAtChanges == Changes)
	{
		return;
	}

	BuiltOnFrame = GFrameCounter;
	BuiltAtSeconds = Seconds;
	BuiltAtChanges = Changes;
	++ListsBuilt;
	CSV_CUSTOM_STAT(CataclysmAI, TargetListRebuilds, 1, ECsvCustomStatOp::Accumulate);

	// EMPTIED RATHER THAN THROWN AWAY, so a side keeps its place and its memory
	// from one frame to the next.
	for (FCataclysmSideCandidates& List : Sides)
	{
		List.Everyone.Reset();
		List.Maddened.Reset();
	}

	if (!World)
	{
		return;
	}

	const FGameplayTag Madness = UCataclysmTeams::MadnessTag();
	for (TActorIterator<ACataclysmCharacterBase> It(World); It; ++It)
	{
		ACataclysmCharacterBase* Character = *It;
		if (!IsValid(Character))
		{
			continue;
		}

		// THE SAME QUESTION `AttitudeBetween` ASKS, owner chain and all, so a
		// minion or a thrall is listed on the side the attitude test will put it.
		const FGenericTeamId Side = UCataclysmTeams::TeamOf(Character);
		FCataclysmSideCandidates* List = Sides.FindByPredicate(
			[Side](const FCataclysmSideCandidates& Each) { return Each.Side == Side; });
		if (!List)
		{
			List = &Sides.AddDefaulted_GetRef();
			List->Side = Side;
		}

		List->Everyone.Add(Character);
		if (UCataclysmTeams::IsMaddened(Character))
		{
			List->Maddened.Add(Character);
		}

		WatchForMadnessOn(Character, Madness);
	}
}

bool UCataclysmTargetCandidates::CapsuleReachesInto(const ACataclysmCharacterBase* Character,
													const FVector& Origin, float RadiusCm)
{
	// A CHARACTER WITH ITS COLLISION OFF WAS NEVER FOUND, which is how a creature
	// that has died is kept out of every search even before its body is removed.
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	if (!Capsule || !Character->GetActorEnableCollision()
		|| !Capsule->IsQueryCollisionEnabled()
		|| Capsule->GetCollisionObjectType() != ECC_Pawn)
	{
		return false;
	}

	// ANY PART OF IT INSIDE, NOT ONLY ITS CENTRE. A capsule is every point within
	// its radius of the line between its two end centres, so it reaches into the
	// sphere when that line comes within the two radii of the sphere's centre.
	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float HalfLine =
		FMath::Max(0.0f, Capsule->GetScaledCapsuleHalfHeight() - CapsuleRadius);
	const FVector Centre = Capsule->GetComponentLocation();
	const FVector Along = Capsule->GetUpVector() * HalfLine;
	const FVector Closest =
		FMath::ClosestPointOnSegment(Origin, Centre - Along, Centre + Along);

	return FVector::DistSquared(Origin, Closest)
		<= FMath::Square(static_cast<double>(RadiusCm) + CapsuleRadius);
}

AActor* UCataclysmTargetCandidates::NearestHostile(const ACataclysmCharacterBase* Searcher,
												   const FVector& Origin, float RadiusCm)
{
	LookedAt = 0;
	if (!Searcher || RadiusCm <= 0.0f)
	{
		return nullptr;
	}

	BuildTheListsIfStale();

	ACataclysmCharacterBase* Nearest = nullptr;
	double NearestDistanceSquared = TNumericLimits<double>::Max();

	const auto LookAt = [&](const TArray<TWeakObjectPtr<ACataclysmCharacterBase>>& Entries)
	{
		for (const TWeakObjectPtr<ACataclysmCharacterBase>& Entry : Entries)
		{
			++LookedAt;

			// A DESTROYED CHARACTER READS AS NOTHING HERE, which is why removing
			// one needs no rebuild.
			ACataclysmCharacterBase* Candidate = Entry.Get();
			if (!Candidate || Candidate == Searcher)
			{
				continue;
			}

			// CHEAPEST FIRST. A distance is arithmetic and so is a capsule; the
			// hostility test reads ability systems and walks owner chains, so it
			// is asked only of a candidate that would be nearer than the best so
			// far.
			const double DistanceSquared =
				FVector::DistSquared(Candidate->GetActorLocation(), Origin);
			if (DistanceSquared >= NearestDistanceSquared
				|| !CapsuleReachesInto(Candidate, Origin, RadiusCm)
				|| !UCataclysmTargeting::IsHostileTo(Candidate, Searcher))
			{
				continue;
			}

			Nearest = Candidate;
			NearestDistanceSquared = DistanceSquared;
		}
	};

	// READ LIVE, NOT FROM THE LISTS. The searcher's own Madness and side decide
	// which lists it looks at, and a stale answer here would hide a whole side.
	const bool bMaddened = UCataclysmTeams::IsMaddened(Searcher);
	const FGenericTeamId Mine = UCataclysmTeams::TeamOf(Searcher);

	for (const FCataclysmSideCandidates& List : Sides)
	{
		// EVERYONE ON ANOTHER SIDE, AND EVERYONE WHEN EITHER HAS NO SIDE OR THE
		// SEARCHER IS MADDENED. On its own side, only the maddened: Madness makes
		// a pair hostile whichever of the two carries it.
		const bool bWholeList =
			bMaddened || Mine == FGenericTeamId::NoTeam || List.Side != Mine;
		LookAt(bWholeList ? List.Everyone : List.Maddened);
	}

	return Nearest;
}
