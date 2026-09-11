// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "CataclysmTargetCandidates.generated.h"

class ACataclysmCharacterBase;

/** One side's characters, with the maddened ones among them listed again. */
struct FCataclysmSideCandidates
{
	FGenericTeamId Side = FGenericTeamId::NoTeam;
	TArray<TWeakObjectPtr<ACataclysmCharacterBase>> Everyone;
	TArray<TWeakObjectPtr<ACataclysmCharacterBase>> Maddened;
};

/**
 * The characters a creature's target search looks at, listed by side. Issue
 * #1547.
 *
 * WHAT IT REPLACES. `ACataclysmEnemyController::ChooseTarget` used to ask
 * `UCataclysmTargeting::FindEnemiesInSphere` for the nearest hostile body inside
 * the creature's notice distance. That is a physics sphere query, and in a Horde
 * arena the sphere covers the whole arena, so every search returned every
 * creature there and then refused all but the few on the player's side. The
 * project owner's capture of 2026-09-10, posted on #1547, measured one search at
 * 1.35 ms with 125 to 149 creatures thinking and 1.53 ms with 150 to 174. It was
 * 94% of the time spent thinking, and in the two slowdowns after a wave arrived
 * it took 809 and 855 ms of every second of game time, which is what held the
 * frame rate at 2.5 to 2.7 frames a second.
 *
 * WHAT IT DOES INSTEAD. Which side a character is on, and whether it is maddened,
 * decide what it can attack (`UCataclysmTeams::AttitudeBetween`):
 * - A creature that is not maddened can attack characters on another side,
 *   characters with no side, and maddened characters on its own side. In a Horde
 *   arena with no Madness that is the player and whatever the player summoned or
 *   took.
 * - A maddened creature can attack every character.
 * So the characters are listed by side, with each side's maddened ones listed
 * again, and a search looks only at the lists it could attack from. The final
 * test is unchanged: the nearest candidate that `UCataclysmTargeting::IsHostileTo`
 * accepts, asked live, so a character that died after the lists were made is
 * still refused, and so is one its searcher shares an owner with.
 *
 * THE LISTS ARE ASKED FOR, NOT KEPT. They are rebuilt by walking the level's
 * characters, at the first search after anything they depend on could have
 * changed:
 * - a new frame, or the world clock moving;
 * - a character spawned, which the world reports;
 * - a listed character gaining or losing Madness, which its ability system
 *   reports.
 * The last two are what let a test call `Think` twice without the clock moving
 * and still see what it did in between. `UCataclysmCommand`'s header records why
 * the project prefers asking the world over keeping a register.
 *
 * A DESTROYED CHARACTER NEEDS NO REBUILD. Its entry reads as nothing from the
 * moment it is destroyed, the search passes over it, and the next frame's lists
 * leave it out. A big fight removes bodies all the time, and a rebuild for each
 * removal would make the cost of the lists grow with the fighting.
 *
 * COUNTED IN A CSV PROFILE CAPTURE, as `CataclysmAI/TargetListRebuilds`: how many
 * times the lists were built in a frame. One in every frame in which anything
 * searches, and more only when a character is spawned, or gains or loses
 * Madness, between two searches in the same frame. Nothing is recorded unless a
 * capture is running.
 *
 * WHAT THE SPHERE DID DIFFERENTLY.
 * - IT COUNTED EVERY PAWN-TYPE BODY, and this counts a character's capsule. With
 *   the art loaded a creature's animated model has bodies of its own, so at the
 *   edge of an ordinary floor's notice distance, 10 to 15 metres, the two can
 *   disagree by up to about a body's width. A Horde arena's notice distance is
 *   larger than the arena, so there it cannot arise.
 * - IT COULD FIND AN ACTOR THAT IS NOT A CHARACTER, if the actor carried an
 *   ability system and a pawn-type body. In the game none does: only an enemy, a
 *   minion and the player's state make an ability system, and the player state
 *   has no collision. Some test fixtures build one out of a bare actor, and none
 *   of them is what a creature's search is asked to find.
 * - A SIDE CHANGED PART WAY THROUGH A FRAME is seen at the next rebuild, at the
 *   latest in the next frame. Taking a thrall is the only thing in the game that
 *   changes a living character's side.
 * - TWO CANDIDATES AT EXACTLY THE SAME DISTANCE may be chosen between the other
 *   way. The sphere's order was the physics engine's; this keeps the first found.
 */
UCLASS()
class CATACLYSM_API UCataclysmTargetCandidates : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** This world's lists, or null in a world that has no subsystems. */
	static UCataclysmTargetCandidates* In(const UWorld* World);

	/**
	 * The nearest character reaching into the sphere that the searcher is hostile
	 * to, or null. The answer `FindEnemiesInSphere(World, Searcher, Origin,
	 * RadiusCm, 1)` gives, apart from the cases the class comment lists.
	 *
	 * @param Searcher  the creature looking; its side and Madness are read live
	 * @param Origin    the centre of the sphere, which is where the searcher stands
	 * @param RadiusCm  how far it notices, `ACataclysmCharacterBase::NoticesFromCm`
	 */
	AActor* NearestHostile(const ACataclysmCharacterBase* Searcher,
						   const FVector& Origin, float RadiusCm);

	/** How many list entries the last search looked at. Read by tests. */
	int32 LookedAtByTheLastSearch() const { return LookedAt; }

	/** How many times this world's lists have been built. Read by tests. */
	int32 ListsBuiltSoFar() const { return ListsBuilt; }

	//~ UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End

private:
	void BuildTheListsIfStale();
	void WatchForMadnessOn(ACataclysmCharacterBase* Character, const FGameplayTag& Madness);
	void NoteCharacterSpawned(AActor* Actor);
	void NoteMadnessChanged(const FGameplayTag Tag, int32 NewCount);

	/**
	 * Whether the character's capsule reaches into the sphere, asked the way the
	 * physics query asked it: a pawn-type body with query collision on, counted
	 * when any part of it is inside, not only its centre.
	 */
	static bool CapsuleReachesInto(const ACataclysmCharacterBase* Character,
								   const FVector& Origin, float RadiusCm);

	TArray<FCataclysmSideCandidates> Sides;

	/** Ability systems already reporting their Madness changes here. */
	TSet<FObjectKey> WatchedAbilitySystems;

	uint64 BuiltOnFrame = MAX_uint64;
	double BuiltAtSeconds = -1.0;
	uint32 Changes = 0;
	uint32 BuiltAtChanges = MAX_uint32;

	int32 ListsBuilt = 0;
	int32 LookedAt = 0;

	FDelegateHandle SpawnedHandle;
};
