// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TimerManager.h"
#include "CataclysmBoneGiants.generated.h"

class ACataclysmMinion;
class ACataclysmRemainsMarker;
class UWorld;
struct FCataclysmDeathNotice;

/** What one player's giants and tally hold on the floor being walked. */
struct FCataclysmGiantsOfAPlayer
{
	/** Kills, by the player or anything it commands, of creatures the floor started with. */
	int32 KilledOfStart = 0;

	/** Every creature the player or anything it commands killed on this floor: their maximum health and their blows. */
	float SummedHealth = 0.0f;
	float SummedDamage = 0.0f;

	/** The Abomination, while it stands, and whether one has risen on this floor (it rises once). */
	TWeakObjectPtr<ACataclysmMinion> Abomination;
	bool bAbominationRose = false;

	/** The Bone Colossus, while it stands, and the health and blow it rose with. */
	TWeakObjectPtr<ACataclysmMinion> Colossus;
	float ColossusStartHealth = 0.0f;
	float ColossusStartDamage = 0.0f;
};

/**
 * The Necromancer's two giants and the floor tally the first reads. The Death finish's slice D (2026-11-26), on the
 * remains design the coordinating session approved that day and its answers Q1 to Q4.
 *
 * ABOMINATION (The Final Rite, option 1): "When you have killed at least 50% of the creatures the floor started with,
 * their remains combine into an Abomination that fights for you until you leave the floor. It reserves 100 Fervour and
 * releases whatever else you have out that would stop it being raised. Its health and damage are a share of every
 * creature killed on the floor so far, and it grows with each kill after it rises, so a floor of Elites makes a
 * stronger one. Creatures a rule keeps sending (rift hunters, Horde waves) do not count toward the 50%. With Lone
 * Revenant, the Abomination has Lone Revenant's 400% more health and damage, and still reserves 100 Fervour."
 *   - THE FLOOR STARTED WITH every creature the game mode puts into the floor's population on an arena it carved
 *     (`NoteStartedWith`, from the two places a population creature joins the wave); not a rule's arrival (a rift
 *     hunter, every `bRaisedByARule` creature) and not a kept Horde arena's later wave.
 *   - KILLED BY the player or anything it commands. 50% less Many Made One's 2 a point, of that count, rounded up.
 *   - IT RISES where the kill that reached the count fell, needing no remains on the floor and no Bone Fuse, as a
 *     Barrow Guard's body drawn larger (the coordinating session: it may reuse a creature's body; no new art).
 *   - HEALTH 10% AND DAMAGE 5% (Q1, both play-test figures) of the summed maximum health and blows of every creature
 *     the player or its followers killed on the floor, rule-sent ones included, and the same shares of each kill after.
 *     Its health is raised by the player's minion health rows as any minion's is; its blow by minion damage at each
 *     swing, as any minion's is.
 *   - RESERVES 100, a flat figure that is not `UCataclysmCommand::ReserveFor`'s, so no reserve row reaches it, Lone
 *     Revenant's included; the player's followers are released oldest first (`UCataclysmCommand::ReleasableOldestFirst`)
 *     until it fits. When it could not fit with all of them gone, nothing is released and it does not rise; it is asked
 *     again at the next kill.
 *   - LONE REVENANT (`LoneRevenantHeldStat`, slice B's flag): 400% more health and damage.
 *   - GONE WHEN THE FLOOR CHANGES (`EndTheFloor`); it stays through a kept Horde arena's waves. Killed, it does not rise
 *     again on that floor.
 *
 * BONE COLOSSUS (The First Rite, option 2): "One giant you feed remains to over a floor; it grows with each creature's
 * remains and shrinks when hurt."
 *   - IT RISES on the first remains of the player's on a floor (the vow makes remains appear, as Bone Fuse says), and
 *     consumes them; a Barrow Guard's figures to start (Q3). It reserves 20 Fervour (Q4); with no room it does not rise.
 *   - FED: every remains of the player's that lands within `ColossusFeedReachCm` of it is consumed, adding 10% of that
 *     creature's maximum health to its maximum and current health. A fed remains is not a raise (Q4), and not one of
 *     Picked Clean's "raising a minion on them or detonating them", so it raises no event.
 *   - SHRINKS WHEN HURT: its blow and its drawn size follow its health against the health it rose with, stepped every
 *     `ColossusStepSeconds`. Being fed grows it back.
 *   - ONE AT A TIME. Killed, the next remains raises a new one at the start. Gone when the floor changes.
 */
UCLASS()
class CATACLYSM_API UCataclysmBoneGiants : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UCataclysmBoneGiants* In(const UWorld* World);

	/** The stats the rows write, each read off the player with no tags. */
	static const TCHAR* AbominationRisesStat;
	static const TCHAR* AbominationKillPercentLessStat;
	static const TCHAR* BoneColossusStat;
	/** Slice B's flag for Lone Revenant, named by the coordinating session on 2026-11-26. */
	static const TCHAR* LoneRevenantHeldStat;

	/** The sentence's numbers. */
	static constexpr float AbominationKillPercent = 50.0f;
	static constexpr float AbominationReserve = 100.0f;
	static constexpr float AbominationHealthSharePercent = 10.0f;
	static constexpr float AbominationDamageSharePercent = 5.0f;
	static constexpr float LoneRevenantMorePercent = 400.0f;
	static constexpr float AbominationDrawnScale = 1.8f;

	static constexpr float ColossusReserve = 20.0f;
	static constexpr float ColossusFeedReachCm = 600.0f;
	static constexpr float ColossusFedHealthSharePercent = 10.0f;
	static constexpr float ColossusStepSeconds = 0.25f;
	static constexpr float ColossusSmallestScale = 0.6f;
	static constexpr float ColossusLargestScale = 3.0f;

	/** The body both giants are drawn with. */
	static const TCHAR* GiantTypeName;

	/** A new floor: every tally forgotten and every giant ended. Called by the game mode on an arena it carved. */
	static void BeginFloor(UWorld* World);

	/** A creature the floor started with. Called by the game mode as a population creature joins a carved arena. */
	static void NoteStartedWith(AActor* Creature);

	/** How many creatures the floor started with. */
	static int32 StartedWithCount(const UWorld* World);

	/** How many of them `Player` and its followers have killed. */
	static int32 KilledOfStartBy(const AActor* Player);

	/** How many must be killed for `Player`'s Abomination: the share Many Made One leaves, rounded up. Unreachable when
	 *  the floor started with none. */
	static int32 KillsNeededFor(const AActor* Player);

	/** A death `Player`'s character heard: the tally, the Abomination's rise or growth. */
	static void NoteDeath(AActor* Player, const FCataclysmDeathNotice& Notice);

	/** Raise `Player`'s Abomination at `Where` from its tally, releasing followers to make room. Null when it cannot. */
	static ACataclysmMinion* RaiseAbomination(AActor* Player, const FVector& Where);

	/** Remains of `Player`'s just landed: the Bone Colossus rises on them or is fed them. */
	static void RemainsLanded(AActor* Player, ACataclysmRemainsMarker* Remains);

	/** `Player`'s Abomination or Bone Colossus while it stands, or null. */
	static ACataclysmMinion* AbominationOf(const AActor* Player);
	static ACataclysmMinion* ColossusOf(const AActor* Player);

	/**
	 * Whether `Minion` is its summoner's Abomination or Bone Colossus, as this floor holds them. Read by
	 * `UCataclysmDetonation`, which detonates neither: the coordinating session's ruling of 2026-11-26 at the bundle's
	 * assembly, "They are vow giants, and a skill landing on them must not destroy them". ASKED OF THE STORED GIANTS, not
	 * the minion's type: both are drawn as `GiantTypeName`, which an ordinary Barrow Guard also is.
	 */
	static bool IsAGiant(const ACataclysmMinion* Minion);

	/** The Colossus's blow and size from its health, now. Called on the step, and by tests. */
	static void StepColossusOf(const AActor* Player);

	/** The floor is left: every giant destroyed and every tally forgotten. Called by `ClearTheFloor`. */
	static int32 EndTheFloor(UWorld& World);

private:
	FCataclysmGiantsOfAPlayer& TallyOf(const AActor* Player);
	const FCataclysmGiantsOfAPlayer* FindTallyOf(const AActor* Player) const;
	void StepEveryColossus();
	void StartColossusStep();

	TSet<TWeakObjectPtr<AActor>> StartedWith;
	int32 StartedCount = 0;
	TMap<TWeakObjectPtr<const AActor>, FCataclysmGiantsOfAPlayer> Players;
	FTimerHandle ColossusStepTimer;
};
