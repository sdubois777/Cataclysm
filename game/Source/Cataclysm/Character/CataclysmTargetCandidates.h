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

	/**
	 * The hostile character FURTHEST from `Origin` and still within `RadiusCm`,
	 * or null. Issue #340.
	 *
	 * WHAT ASKS FOR IT. `game/Data/MinionTypes.csv` gives every minion a
	 * `TargetMode`, and the Ballista's says `Furthest`. Its row struct has always
	 * said "the Ballista deliberately picks the furthest"; until this existed
	 * nothing read the column and it picked the nearest like everything else.
	 *
	 * THE SAME WALK AS `NearestHostile`, WITH THE COMPARISON REVERSED, and both
	 * go through one private implementation rather than two copies. The two
	 * differ by one operator, and a second copy of "which characters can this one
	 * reach" is how two answers to one question drift apart.
	 *
	 * NOT `HostileDistancesWithinMetres`, AND THE REASON IS IN THAT FUNCTION'S
	 * OWN COMMENT. It returns distances rather than a character, so it cannot
	 * name a target; and it measures centre to centre where this asks whether a
	 * capsule reaches into a sphere, which that comment records as able to
	 * disagree "by about a body's width". Choosing a target with the other
	 * arithmetic would quietly move which enemy every creature picks.
	 */
	AActor* FurthestHostile(const ACataclysmCharacterBase* Searcher,
							const FVector& Origin, float RadiusCm);

	/**
	 * How far away each hostile character within `Metres` is, in metres, for a
	 * passive row that counts the enemies near a character. Issue #1597.
	 *
	 * HERE RATHER THAN IN THE STAT PIPELINE, because the lists this walks are
	 * private to this class and the alternative is a second walk of the level's
	 * characters. A second implementation of "which characters are near this one"
	 * is how two answers to one question drift apart.
	 *
	 * DISTANCES RATHER THAN A COUNT, so one walk serves every radius a character's
	 * rows ask for. Three radii appear in the authored rows -- 3, 4 and 8 metres --
	 * and a count would need one walk each. The caller counts the entries inside
	 * its own radius.
	 *
	 * CENTRE TO CENTRE, AND THAT IS DELIBERATELY NOT WHAT `NearestHostile` ABOVE
	 * USES. That one asks whether a character's CAPSULE reaches into a sphere, and
	 * this class's header records that the two can disagree by about a body's
	 * width. This uses the same arithmetic as
	 * `UCataclysmTargeting::MetresBetween`, which is the one definition in the
	 * game of how far apart two actors are, so a node reading "within 4 metres"
	 * and any other distance the game reports for the same pair agree exactly.
	 *
	 * THE TWO ANSWER DIFFERENT QUESTIONS, which is why the difference is
	 * acceptable: a creature choosing whom to attack cares whether it can reach a
	 * body, and a passive row counting enemies near a character is a number a
	 * player reads off a sentence.
	 */
	void HostileDistancesWithinMetres(const ACataclysmCharacterBase* Searcher,
									  const FVector& Origin, float Metres,
									  TArray<float>& OutMetres);

	/** How many list entries the last search looked at. Read by tests. */
	int32 LookedAtByTheLastSearch() const { return LookedAt; }

	/** How many times this world's lists have been built. Read by tests. */
	int32 ListsBuiltSoFar() const { return ListsBuilt; }

	//~ UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End

private:
	/**
	 * The one walk behind `NearestHostile` and `FurthestHostile`.
	 *
	 * `bFurthest` REVERSES ONE COMPARISON AND NOTHING ELSE. The walk keeps the
	 * cheap test in front of the expensive one either way: a distance is
	 * arithmetic, and the hostility test reads ability systems and walks owner
	 * chains, so it is asked only of a candidate that would beat the best so far.
	 * Reversing the comparison keeps that ordering rather than losing it.
	 */
	AActor* HostileAtOneEnd(const ACataclysmCharacterBase* Searcher,
							const FVector& Origin, float RadiusCm,
							bool bFurthest);

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
