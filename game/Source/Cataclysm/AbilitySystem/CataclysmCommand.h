// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmCommand.generated.h"

class ACataclysmCharacterBase;
class ACataclysmMinion;

/**
 * Why `UCataclysmCommand::Subjugate` would refuse to take a creature, one value for each reason it can refuse, in the
 * order it asks them. Issue #2856.
 *
 * ONE LIST, SO THE REFUSAL AND ITS LOG LINE CANNOT PART COMPANY. The line in `UCataclysmSummonSkill::Possess` used to
 * name three of the six reasons by asking them again for itself, so a creature the blow had killed printed "boss no,
 * cannot be hurt no, already on its side no" and its real reason nowhere. Now `Subjugate` refuses by
 * `UCataclysmCommand::WhySubjugateRefuses` and the line prints that answer and every reason on this list.
 *
 * NOT A UENUM, because nothing outside C++ reads it.
 */
enum class ECataclysmSubjugateRefusal : uint8
{
	/** Nothing refuses: the creature can be taken. */
	None,
	/** No commander, no creature, or the commander asked to take itself. */
	NothingToTake,
	/** An actor that is not a character: ground, a projectile, terrain. */
	NotACharacter,
	/** Dead already: the blow killed it, or something before the blow did. */
	Dead,
	/** A boss, by its rarity: "Bosses cannot be taken". */
	Boss,
	/** A creature that cannot be hurt: The Reaper. */
	CannotBeHurt,
	/** On the commander's side already. */
	AlreadyOnItsSide,
	/** Not a reason: one past the last, so a walk over every reason ends here. */
	Count
};

/**
 * What a character commands, what it is ordered to attack, and taking an enemy
 * into that command.
 *
 * THREE OF THE STAFF'S FIVE ROWS ASK THE SAME QUESTION AND NOTHING COULD ANSWER
 * IT. Quarry: "everything you command breaks off and attacks it". Compel:
 * "everything you command strikes that same enemy at once". Vesselstep: "trade
 * places with a creature you command up to 14 meters away."
 *
 * EACH SUMMON SKILL KEEPS ITS OWN PRIVATE LIST, WHICH IS THE WRONG SHAPE FOR
 * THIS. `UCataclysmSummonSkill::Minions` holds what that one skill made, so a
 * character holding three imps from one skill and two thralls from another has
 * two lists and no way to ask about both. Worse, a thrall is not a minion actor
 * at all: Subjugate takes an enemy and changes which side it is on, so it stays
 * the creature it was and would never appear in any summon skill's list.
 *
 * SO THE WORLD IS ASKED RATHER THAN A REGISTER KEPT. Walking the characters in
 * the level and asking each who it follows needs no bookkeeping, cannot fall out
 * of step when a minion expires or a thrall dies, and gets a possessed enemy and
 * a summoned imp with the same question. The design caps what this can ever
 * return at a handful: three imps and, at 30 Fervour each from a pool of 150,
 * five thralls.
 *
 * A REGISTER WAS THE OTHER OPTION AND IT HAS TO BE MAINTAINED IN FOUR PLACES --
 * when a minion is spawned, when it expires, when it is killed, and when a
 * thrall dies -- and the fourth of those is a path this class would have had to
 * add anyway. The project already prefers asking: `HeldConsumeSpreadRadiusCm`
 * asks the caster's running abilities rather than recording what they granted.
 */
UCLASS()
class CATACLYSM_API UCataclysmCommand : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Everything this character commands: its summoned minions and its thralls.
	 *
	 * A MINION FOLLOWS ITS `Summoner` AND A THRALL FOLLOWS ITS OWNER, and both
	 * are answered here so a caller never has to know which it is holding. That
	 * is the point: "everything you command" is one phrase in three rows and it
	 * means both kinds.
	 *
	 * NEAREST FIRST, so a caller that wants one -- Vesselstep trades places with
	 * a creature and the row does not say which -- takes the front of the list
	 * without sorting it again.
	 *
	 * ANYTHING DEAD IS LEFT OUT. A creature at no health is not going to break
	 * off onto anything, and a caller that had to filter would be the third place
	 * in the project remembering that a death is recorded before the actor goes.
	 *
	 * @param Commander  whose creatures to find. Null answers empty
	 * @param WithinCm   how far to look, or zero for no limit. Vesselstep states
	 *                   14 metres and the other two rows state no distance at all
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Command")
	static TArray<AActor*> ThingsCommandedBy(const AActor* Commander,
											 float WithinCm = 0.0f);

	/**
	 * Of `ThingsCommandedBy(Commander, WithinCm)`, the ones the commander can see: nothing that blocks the
	 * visibility channel lies on the straight line from the commander's middle to the creature's. Nearest first, as
	 * that list is. Issue #2792.
	 *
	 * FOR VESSELSTEP, whose trade is an exchange of places and so crosses whatever stands between the two. A wall or
	 * a pillar blocks the channel, so a creature behind one is left out, and a trade cannot carry the player past a
	 * wall by leaving a creature on its far side. The coordinating session's judgement, 2026-11-20: "up to 14
	 * meters away" does not say through walls, and the dungeon is to have gates and gated shortcuts.
	 *
	 * WHAT DOES NOT BLOCK IT, by the engine's collision profiles (`BaseEngine.ini`): a character, because the
	 * `Pawn` profile of its capsule and the `CharacterMesh` profile of its mesh both ignore the visibility channel,
	 * so a creature standing between is no cover; and a pit's blocker, whose profile `InvisibleWall` ignores it
	 * too, so a creature across a pit can be seen. The floor's walls and pillars use `BlockAll`, which blocks it.
	 */
	static TArray<AActor*> ThingsCommandedInSightOf(const AActor* Commander, float WithinCm);

	/**
	 * Of everything this character commands, the one with the least health
	 * remaining, in points; null if it commands nothing alive. Issue #1515,
	 * Sacrificial Ward: "destroys the minion with the least health remaining".
	 *
	 * POINTS AND NOT A SHARE OF MAXIMUM, ruled on 2026-09-23 under the owner's
	 * delegation, because the sentence says "least health remaining". A tie
	 * goes to the nearer, since `ThingsCommandedBy` answers nearest first.
	 */
	static AActor* LeastHealthCommandedBy(const AActor* Commander);

	/**
	 * The living minions of one type this character commands, oldest first.
	 * Issue #1479.
	 *
	 * EVERY SOURCE OF THE TYPE, a summon skill's, a risen imp's, a necrosis kill's.
	 * It was written for the count cap Summon Imp and the Ritualist's risen imps
	 * shared (ruled 2026-09-27); issue #2787 removed every count cap on what
	 * reserves the class resource, so nothing in the game reads it for a cap now,
	 * and it counts every minion of the type.
	 *
	 * DEAD AND DESTROYED ONES ARE LEFT OUT, as `LivingMinionCount` leaves them
	 * out of a skill's own list (issue #1957). `ThingsCommandedBy` drops the
	 * dead; a thrall is not a minion actor, so it is never in this list.
	 *
	 * @param Commander  whose minions. Null answers empty
	 * @param TypeName   a row of game/Data/MinionTypes.csv, such as "Imp"
	 */
	static TArray<ACataclysmMinion*> MinionsOfTypeCommandedBy(const AActor* Commander,
															  const FString& TypeName);

	/**
	 * Who this creature follows, or null if it follows nobody.
	 *
	 * THE OTHER DIRECTION OF `ThingsCommandedBy` ABOVE, and it is asked from the
	 * creature's own side: a minion choosing what to attack needs to know whose
	 * orders to take, and walking every character in the level to find out would
	 * be the wrong way round.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Command")
	static AActor* CommanderOf(const AActor* Follower);

	/**
	 * Who stands at the top of this creature's chain of command: its commander, that commander's commander, and so on,
	 * up to the one that answers to nobody. Issue #2867.
	 *
	 * NULL FOR A CREATURE THAT ANSWERS TO NOBODY, AND FOR ONE WHOSE CHAIN REACHES A COMMANDER THAT NO LONGER EXISTS.
	 * The second is a taken Gatekeeper's Imps once the Gatekeeper is gone: their owner is the destroyed Gatekeeper until
	 * the engine clears it (`UWorld::DestroyActor` clears the destroyed actor's own owner and not its children's), and
	 * they stay on its commander's side, ruled for issue #2791. A caller that needs to tell those two apart asks the
	 * creature's side.
	 *
	 * WHY A CHAIN AT ALL. A thrall's own abilities can make creatures, and Call the Damned does: a taken Gatekeeper's
	 * Imps follow the Gatekeeper, not the player (issue #2791), so `CommanderOf` answers the Gatekeeper and the player is
	 * one step further up.
	 *
	 * AT MOST `MostLinksOfCommand` STEPS, and null past them, so a chain that loops cannot hang the frame. None loops
	 * today: only `Subjugate`, a summoning and a taken Gatekeeper's Call the Damned name a commander, and none of the
	 * three takes or makes its own commander.
	 */
	static AActor* TopCommanderOf(const AActor* Follower);

	/** How many steps `TopCommanderOf` climbs. A thrall's Imp is two from the player; eight is room to spare. */
	static constexpr int32 MostLinksOfCommand = 8;

	/**
	 * How fast a commanded creature walks, in centimetres a second: its commander's walking speed now, times
	 * `CommandedWalkSpeedShareOfCommander`. Nought when it has no commander, when it stays where it is put, or when
	 * its commander is not a character that walks.
	 *
	 * THE OWNER'S RULING OF 2026-11-20, issue #2793: "Your minions movespeed should be equal to 1.25 of your own. That
	 * way they actually keep up." EQUAL, AND NOT THE FASTER OF THE TWO, as the coordinating session ruled the same day:
	 * a thrall that walked faster than this before -- a Hellhound, an Imp, a Brute chasing -- is slowed to it, and so
	 * is a Mote, whose type row says 5.5 metres a second.
	 *
	 * THE COMMANDER'S FIGURE IS ITS MOVEMENT COMPONENT'S `MaxWalkSpeed`, READ NOW. The player writes its speed there
	 * after every modifier -- its movement speed stat, a Cripple, a channel -- so a minion follows its buffs and slows.
	 * It is the figure `UCataclysmGroundZone::WalkSpeedOf` reads for a patch that follows its owner.
	 *
	 * A BASE FOR A THRALL, NOT ITS FINAL SPEED. Its own hastes and slows still multiply it, as they multiplied its
	 * designed speed: see `ACataclysmEnemyCharacter::RefreshWalkSpeed`. A summoned minion has no such path and walks at
	 * this figure (`ACataclysmMinion::RefreshWalkSpeed`).
	 */
	static float CommandedWalkSpeedCmPerSecond(const AActor* Follower);

	/** The share of its commander's walking speed a commanded creature walks at. The owner's 1.25, issue #2793. */
	static constexpr float CommandedWalkSpeedShareOfCommander = 1.25f;

	/** The `Status.Debuff.Quarry` tag, requested by name. Invalid if the sheet drops it. */
	static FGameplayTag QuarryTag();

	/**
	 * What this creature should attack because its commander said so, or null.
	 *
	 * THE STAFF'S QUARRY: "everything you command breaks off and attacks it."
	 * `Status.Debuff.Quarry` was applied for twelve seconds and read by nothing, so the
	 * mark landed, lasted, and ordered nobody anywhere.
	 *
	 * "BREAKS OFF" IS WHY THIS OVERRIDES RATHER THAN SUGGESTS. A creature already
	 * fighting something drops it, which is the whole value of the skill: it is
	 * how a Ritualist aims an army that otherwise attacks whatever is nearest.
	 *
	 * NULL FOR A CREATURE THAT FOLLOWS NOBODY, and null when its commander has
	 * marked nothing, which together is every creature in the game but a
	 * Ritualist's.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Command")
	static AActor* OrderedTargetFor(const AActor* Follower);

	/**
	 * The minion that draws this creature off the character it would otherwise
	 * attack, or null when nothing changes.
	 *
	 * BEHIND THE VEIL, the Ritualist keystone `Ritualist_keystone_spine_002`:
	 * "Enemies within 10 metres attack your minions rather than you, while you
	 * have three or more minions."
	 *
	 * THE TEN AND THE THREE ARE ROWS ON THE NODE, not numbers here, ruled on
	 * 2026-09-18: every other node's numbers live in the data and a reader of the
	 * rows must see them. `minions_draw_nearby_enemies_metres` carries the reach
	 * and `minions_draw_nearby_enemies_minimum` the count, and neither has an
	 * attribute behind it.
	 *
	 * A REACH ABOVE ZERO IS THE KEYSTONE BEING PRESENT, so there is no separate
	 * flag. A character without the node has no row, the lookup answers zero, and
	 * this returns null before asking anything else.
	 *
	 * A BOSS IGNORES IT, ruled by the project owner on 2026-09-18. It reads the
	 * rarity the way `UCataclysmSkillEffects::ApplyStun` reads stun immunity and
	 * the subjugation rule reads "bosses cannot be taken", as that rule's own
	 * comment asks, so the three cannot drift apart.
	 *
	 * IT IS NOT CROWD CONTROL and no resistance shortens or refuses it, ruled at
	 * the same time. The resistance machinery scales a duration and this has none.
	 *
	 * WHICH MINION: the eligible one drawing most attention, by `ThreatPercent`
	 * on its own minion type row, ties broken by whichever stands nearest this
	 * creature. A minion whose type states zero draws nobody -- that is what makes
	 * a turret and a decoy one number rather than two behaviours -- so it is not
	 * eligible. A subjugated enemy COUNTS towards the three, because it is
	 * commanded, and is eligible only if its own type row states a threat above
	 * zero.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Command")
	static AActor* MinionDrawingEnemyFrom(const AActor* Defender,
										  const AActor* Deciding);

	/**
	 * What to multiply this creature's attack interval by, right now.
	 *
	 * ONE THING CHANGES IT: attacking its commander's quarry. Quarry grants "30%
	 * attack speed while the mark holds", and attacking faster is a shorter
	 * interval, so a 30% bonus is a multiplier of 1 / 1.30 rather than of 0.70.
	 * Those differ -- 0.769 against 0.700 -- and only the first means "30% more
	 * attacks in the same time", which is what the sentence says.
	 *
	 * THE FIGURE COMES FROM THE STATUS EFFECTS SHEET AND NOT FROM CODE.
	 * `Debuff_Quarry`'s Strength column carries it, which is where Shred's ten
	 * already lives, so moving the balance number does not need a build.
	 *
	 * ONE, WHICH CHANGES NOTHING, FOR EVERY OTHER CASE. A creature following
	 * nobody, a commander with no mark, and a creature swinging at something that
	 * is not the mark all take the plain interval.
	 *
	 * @param Follower  the creature about to swing
	 * @param Target    what it is swinging at. The bonus is for hitting the mark,
	 *                  not for the mark existing somewhere
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Command")
	static float AttackIntervalScaleFor(const AActor* Follower,
										const AActor* Target);

	/**
	 * What this character has ordered its creatures onto, or null.
	 *
	 * READ OFF THE MARKED ENEMY RATHER THAN HELD ON THE COMMANDER. Quarry is a
	 * debuff on one enemy for twelve seconds, so the mark IS the state and asking
	 * which enemy carries it needs nothing else to be kept in step with it. A
	 * pointer on the character would have to be cleared when the mark expired,
	 * when the enemy died, and when a second cast moved it.
	 *
	 * IT COSTS A SWEEP OF THE LEVEL AND IS ASKED ONLY BY A MINION CHOOSING A
	 * TARGET, which happens on the brain's own beat rather than every frame.
	 *
	 * ONE MARK AT A TIME, WHICH THE ROW STATES BY SAYING "AN ENEMY". If two
	 * carry it -- which a second cast before the first expired would do -- the
	 * nearest to the commander wins, so the answer is stable rather than
	 * whichever the level happened to list first.
	 *
	 * @param Commander  whose mark to look for. A mark is credited to whoever
	 *                   applied it, so one player's quarry does not order
	 *                   another player's minions
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Command")
	static AActor* QuarryOf(const AActor* Commander);

	/**
	 * How many of this character's commanded creatures are thralls.
	 *
	 * A THRALL IS ONE THAT WAS TAKEN, AND A MINION IS ONE THAT WAS MADE, which
	 * is the whole of the distinction and needs no flag to record it: a summoned
	 * creature is an `ACataclysmMinion` and a subjugated one is whatever it
	 * already was. Anything commanded that is not a minion was taken.
	 *
	 * IT IS COUNTED RATHER THAN TALLIED, so nothing has to be decremented when a
	 * thrall dies. A count that had to be kept in step would need a hook on every
	 * death, and getting that wrong would leave a Ritualist unable to take a
	 * sixth thrall after its fifth had already been killed.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Command")
	static int32 ThrallCountOf(const AActor* Commander);

	/**
	 * What a summoner's gear and passives do to one of its minions' own figures, as a
	 * multiplier, with no target: health, range, duration, explosion damage. Moved here
	 * from `CataclysmMinion.cpp` unchanged, for issue #1715, so a thrall reads it too; the
	 * definition carries the reasoning.
	 */
	static float SummonerMultiplierFor(const AActor* Summoner, const TCHAR* Stat,
									   const FGameplayTagContainer& MinionTags,
									   const AActor* AskingMinion = nullptr);

	/** `SummonerMultiplierFor` against the character a minion is striking. Moved with it. */
	static float SummonerMultiplierAgainst(const AActor* Summoner, const TCHAR* Stat,
										   const AActor* Target,
										   const FGameplayTagContainer& MinionTags,
										   const AActor* AskingMinion = nullptr);

	/**
	 * What a blow from `Striker` is multiplied by because a thrall struck it: its commander's
	 * minion damage against `Target`. One for anything that is not a thrall. Issue #1715.
	 */
	static float ThrallDamageMultiplierAgainst(const AActor* Striker, const AActor* Target);

	/**
	 * The type tags a thrall's minion damage is asked with: `Minion.Creature` and nothing else. Issue #340, ruled
	 * 2026-11-02 by the coordinating session: a thrall counts as a summoned creature, so its commander's Spirit reaches
	 * its blow and its explosion through `game/Data/MinionScaling.csv`, and Agility does not. Empty if the tag is not
	 * registered, which reaches only the rows that name no tag -- what a thrall was asked with before.
	 */
	static FGameplayTagContainer ThrallMinionTags();

	/**
	 * Whether this character's resource pool has room to hold another thrall.
	 *
	 * THE STAFF'S SUBJUGATE: "holding a thrall reserves 30 Fervour, so your army
	 * is only as large as your pool." The Ritualist's pool is 150 in
	 * `game/Data/ClassStats.csv`, so five, and every point of maximum resource a
	 * passive tree ever grants is progress toward a sixth. That is the design's
	 * own arithmetic, recorded in `docs/DECISIONS.md` on 2026-09-01.
	 *
	 * THE SAME TOTAL THE POOL IS REDUCED BY, since issues #1160 and #1934: what
	 * everything commanded already reserves, from `ReservedFervourOf`, plus the
	 * new thrall's share, against the whole maximum. Imps count as well as
	 * thralls, so one pool bounds the whole army, ruled 2026-09-30 under the
	 * owner's delegation. A Second Self's chosen one counts twice in the total;
	 * when nothing is chosen yet, the new thrall would be, so its share counts
	 * twice here.
	 *
	 * MEASURED AGAINST THE MAXIMUM AND NOT THE CURRENT VALUE. A reservation is a
	 * standing claim on the pool rather than a payment out of it, which is how
	 * Path of Exile's mana reservation behaves, so a Ritualist that has just
	 * spent its Fervour has not thereby lost a thrall.
	 *
	 * @param PerThrall  what one thrall claims. `FervourReserve`, which states 30
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Command")
	static bool HasRoomForAnotherThrall(const AActor* Commander,
										float PerThrall);

	/**
	 * What this commander may release to make room for a thrall, oldest first: every minion and thrall it commands,
	 * and no deployable machine. Issue #2856, the owner's ruling of 2026-11-24 for a take the pool has no room for,
	 * "Replace the oldest": "the oldest minion (an Imp or thrall) is released to make room, and the take happens".
	 *
	 * OLDEST BY `ACataclysmCharacterBase::CommandedSinceSeconds`, when it was summoned or taken, whichever kind it is;
	 * one never stamped counts as the newest, as A Second Self counts it. A tie goes to the nearer, since
	 * `ThingsCommandedBy` answers nearest first and the sort keeps that order.
	 *
	 * NOT A MACHINE, A JUDGEMENT: the ruling names an Imp or a thrall, and a turret, a ballista or a trap is placed
	 * where it stands for a fight; A Second Self chooses among the same followers (`ACataclysmMinion::bIsMachine`).
	 */
	static TArray<ACataclysmCharacterBase*> ReleasableOldestFirst(const AActor* Commander);

	/**
	 * What a thrall's stated Fervour reserve is multiplied by for the rarity of the creature taken. The owner's ruling of
	 * 2026-11-24, "Reserve grows with rarity (Recommended)": "A thrall's Fervour reserve rises with its rarity, e.g.
	 * Common 30, Elite 45, Legendary 60, Herald 90. A Herald thrall takes most of your pool, so it is a choice between
	 * one monster and an army. Exact figures set by the builder."
	 *
	 * COMMON 1, ELITE 1.5, LEGENDARY 2, HERALD 3, A JUDGEMENT THAT KEEPS THE RULING'S EXAMPLE. Subjugate's row states a
	 * Common's 30, so these give 30, 45, 60 and 90, and a Herald holds back 60% of the Ritualist's 150. Each rung costs
	 * less than it is worth: by the rarity table a rung multiplies health by 1.85 and damage by 1.4, so an Elite is
	 * about 1.6 Commons, a Legendary 2.6 and a Herald 4.2 (the square root of the two multiples, health times damage),
	 * against 1.5, 2 and 3 here; taking a rarer creature stays worth its cost, and the pool still bounds it. Read in
	 * `CataclysmCommand.cpp` and `docs/DECISIONS.md`.
	 *
	 * A BOSS CANNOT BE TAKEN, so its rungs are never asked; they answer the Herald's figure rather than invent one.
	 * ONE for anything that is not a creature of the enemy class, which has no rung.
	 */
	static float ThrallReserveScaleFor(const AActor* Taken);

	/**
	 * Whether a thrall reserving `PerThrall` fits now, or would fit once every follower `ReleasableOldestFirst` names
	 * were released. Issue #2856. What `UCataclysmSummonSkill::Possess` asks before its blow, to know whether the
	 * target is one it could take.
	 *
	 * WHAT STAYS IS THE MACHINES, which A Second Self never chooses, so with every releasable follower gone nothing is
	 * chosen and the new thrall would be, counting twice while the passive is held, as `HasRoomForAnotherThrall` counts
	 * it. When even that does not fit, nothing is released for a take that could not happen.
	 */
	static bool RoomCanBeMadeForAThrall(const AActor* Commander, float PerThrall);

	/**
	 * Release this commander's followers, oldest first, until a thrall reserving `PerThrall` fits. Issue #2856,
	 * "Replace the oldest". Answers how many were released: none when there is room already, or when
	 * `RoomCanBeMadeForAThrall` says none can be made.
	 *
	 * RELEASED MEANS GONE, A JUDGEMENT: the follower leaves the world as it does when its commander leaves the dungeon
	 * (`ACataclysmDungeonGameMode::EndEveryPlayersFollowers`) or a summoned minion's time runs out. It does not die, so
	 * nothing a death sets off answers it -- no explosion, no replacement, no Fervour for losing a minion, no soul, no
	 * loot -- and it does not turn back into an enemy beside the player. Each release is logged at `Log`, "RELEASED:".
	 *
	 * ONE AT A TIME, ASKING `HasRoomForAnotherThrall` AFTER EACH, so A Second Self's double count, which moves to the
	 * next oldest when the oldest goes, is counted as that function counts it.
	 */
	static int32 ReleaseOldestForAThrall(const AActor* Commander, float PerThrall);

	/**
	 * The Fervour everything this character commands holds back from its pool.
	 * Issues #1160 and #1934.
	 *
	 * THE SUM OF EACH LIVING FOLLOWER'S `ReservedFervour`, fixed when it was made
	 * or taken, with A Second Self's chosen one counted twice: "reserves twice
	 * the Fervour it would". A dead follower reserves nothing, because
	 * `ThingsCommandedBy` leaves the dead out.
	 *
	 * WHAT THE POOL IS REDUCED BY. `UCataclysmClassResourceAttributeSet::
	 * MaximumClassResourceAsked` subtracts it, so every gain and every spend,
	 * which all clamp to that figure, respects it, and the bar shows it.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Command")
	static float ReservedFervourOf(const AActor* Commander);

	/**
	 * `ReservedFervourOf`, from a list of what `Commander` commands that the caller has already walked
	 * (`ThingsCommandedBy(Commander)`). The one sum both answer from, so the two cannot disagree.
	 *
	 * FOR A CALLER THAT HAS THE LIST IN HAND: `UCataclysmAbilitySystemComponent::CurrentConditions` walks it on every
	 * call to count minions, and asks this for the reserve a full bar is judged against (ruled 2026-11-25, the Death
	 * trees' slice C), so the world is not walked twice.
	 */
	static float ReservedFervourAmong(const AActor* Commander, const TArray<AActor*>& Commanded);

	/**
	 * Whether a new follower reserving `NewReserve` fits in this character's
	 * pool. Issue #1934, ruling R1, and since issue #2787 the only limit on how
	 * many of a kind stand: the owner's ruling of 2026-11-20 removed every count
	 * cap on what reserves the class resource. (A `Freed` figure, what a cap was
	 * about to evict, went with the caps.)
	 */
	static bool HasRoomToReserve(const AActor* Commander, float NewReserve);

	/**
	 * Take current Fervour down to what the reserve leaves spendable. Issue
	 * #1160, ruled 2026-09-30: when a reservation starts, the excess is lost at
	 * once, as in Path of Exile, so the bar never reads above its own maximum.
	 * Called whenever a follower that reserves something is made or taken.
	 */
	static void ClampFervourToSpendable(const AActor* Commander);

	/**
	 * What one follower of a kind reserves for this commander: `Stated` less
	 * Crowned's reduction, times one less the percent `MinionReservePercentLessStat`
	 * reads, never below `SmallestMinionReserve`. Issue #2787.
	 *
	 * EVERY KIND READS ITS RESERVE HERE, whether a skill's row states it or the
	 * kind's own constant does: a thrall and a summoned imp from their skill's
	 * row, a risen imp from Summon Imp's row, and the necrosis imp, the Warhound,
	 * Tyrant's Minion, the skill gadget and the skill trap from the constants
	 * beside their other numbers. Crowned says "each minion", so it reaches all
	 * of them alike.
	 *
	 * ZERO OR LESS STAYS AS STATED. A stat that adjusts a designed figure must
	 * not bring one into existence: zero means "this reserves nothing", and the
	 * floor would otherwise turn it into one.
	 *
	 * @param Tags  what the reduction is asked with: the making skill's tags, or
	 *              the kind's type row tags (`ACataclysmMinion::TagsOfType`), so a
	 *              later row scoped to a keyword reaches only what it names
	 */
	static float ReserveFor(const AActor* Commander, float Stated,
							const FGameplayTagContainer& Tags);

	/**
	 * Record `Reserve` on a follower just made or taken, and cut its commander's
	 * Fervour to what is left spendable. Issue #2787. The two steps every maker
	 * takes once the follower exists, after it has asked `HasRoomToReserve`.
	 *
	 * AND, WHEN THE COMMANDER HOLDS `ReservedFervourSpentPercentStat`, A SPEND OF
	 * THAT SHARE OF THE RESERVE: Bound Offering, "Fervour you hold back for a
	 * minion, gadget or trap counts as Fervour spent, at 15% per point of what is
	 * held back". The Death trees' slice C3b. Raised through
	 * `UCataclysmAbilitySystemComponent::NoteFervourSpent`, so it stamps the clock
	 * and every row on `fervour_spent` reads it; nothing is taken from the pool,
	 * since what is held back is already out of it. So Cinders Kept returns none
	 * of it: ruled by the coordinating session on 2026-11-25 (slice C3c).
	 *
	 * EVERY NEW HOLD IS ONE, a summon, a replacement, a thrall taken, an imp risen,
	 * a skill gadget or trap placed. A level that lowers a reserve already held
	 * (`UCataclysmSkillLevels`) is not a hold and raises nothing.
	 *
	 * @param SpendingTags the making skill's tags, which name the slot Turn of the
	 *        Wheel leaves out; none when no skill made it (a warhound, a risen imp)
	 */
	static void HoldReserve(const AActor* Commander, ACataclysmCharacterBase* Follower,
							float Reserve, const FGameplayTagContainer* SpendingTags = nullptr);

	/**
	 * The stat holding percentage points ADDED to the health threshold a blow
	 * must leave a target under for it to be taken. Issue #1718.
	 *
	 * `Ritualist_keystone_a_kA` Dominion is the only source: "A blow that leaves
	 * a target below 65% health can take it, rather than below half."
	 *
	 * HERE RATHER THAN WHERE IT IS READ, so the name is written once. It is
	 * needed by the comparison in `UCataclysmSummonSkill::Possess` and by
	 * `UCataclysmPlayerClassStats::StatToAttribute`, and a second spelling is how
	 * a renamed stat leaves a keystone granting nothing while every test passes.
	 *
	 * A BONUS AND NOT THE THRESHOLD. The Subjugate skill's row states the
	 * threshold itself, as `HealthThresholdPercent`, and is the only place that
	 * number appears; this is added to it so a re-tune of the row follows
	 * through.
	 */
	static const TCHAR* PossessionThresholdBonusStat;

	/**
	 * The stat holding Fervour points TAKEN OFF what one minion reserves. Issue
	 * #1718.
	 *
	 * `Ritualist_keystone_a_kC` Crowned is the only source: "Each minion
	 * reserves 5 less Fervour, never less than 1." Its row is flat **5**, a
	 * positive number that is subtracted.
	 *
	 * POSITIVE, AND THAT IS FORCED RATHER THAN CHOSEN. This stat was first
	 * written as a bonus of -5 and did nothing:
	 * `UCataclysmCombatAttributeSet::PreAttributeChange` floors every attribute
	 * in that set at zero, so -5 was stored as 0. `docs/DECISIONS.md` carries
	 * the measurement and the rule it produced -- a stat that lowers a figure
	 * names the size of the reduction.
	 *
	 * A REDUCTION AND NOT THE RESERVE ITSELF. Each kind states a reserve of its
	 * own -- Subjugate 30, Summon Imp 10, three deployables 5, and since issue
	 * #2787 a figure for every minion no skill makes -- so a stat holding the
	 * figure would state one of them twice and be wrong for the others.
	 * `ReserveFor` applies it.
	 */
	static const TCHAR* MinionReserveReductionStat;

	/**
	 * The least a minion may reserve however much is taken off it: the "never
	 * less than 1" of Crowned's sentence.
	 *
	 * `HasRoomForAnotherThrall` reads a reserve of zero or less as "capped by
	 * nothing" and returns true for every thrall, so a reduction reaching zero
	 * would remove the army limit rather than lower it. Crowned alone takes a
	 * deployable's 5 to zero.
	 */
	static constexpr float SmallestMinionReserve = 1.0f;

	/**
	 * The stat holding how many percent LESS one minion, gadget or trap reserves; a
	 * negative figure is that many percent MORE. Issue #2787, the owner's ruling
	 * of 2026-11-20 that what raised a count cap lowers the reserve instead ("Lower
	 * the reserve instead"), since the count caps went.
	 *
	 * FIVE SOURCES, each sized to let the player hold about as many more as its old
	 * cap bonus did (reduction = N / (C + N), a judgement): The Swarm, "Your minions
	 * reserve 12% reduced Fervour"; "Your minions reserve 15%-30% reduced Fervour";
	 * "Your minions reserve 25%-65% increased Fervour", the negative one; "Your
	 * deployables reserve 15%-30% reduced Fervour" (`Type.Deployable`); and "Your
	 * traps reserve 5%-25% reduced Fervour" (`Type.Trap`).
	 *
	 * READ BY `ReserveFor` WITH THE KIND'S TAGS, the percentages that reach a kind
	 * adding into one figure as Path of Exile adds increased and reduced
	 * reservation efficiency. No gameplay attribute and no base: it is listed in
	 * `UCataclysmPlayerClassStats::StatsWithNoAttribute`, because a negative
	 * figure would be floored at nought in the combat attribute set.
	 */
	static const TCHAR* MinionReservePercentLessStat;

	/**
	 * The stat holding the share of a reserve held that counts as Fervour spent,
	 * in per cent: Bound Offering, 15 a point. The Death trees' slice C3b. Read by
	 * `HoldReserve` with the making skill's tags. No gameplay attribute and no
	 * base: it is listed in `UCataclysmPlayerClassStats::StatsWithNoAttribute`.
	 */
	static const TCHAR* ReservedFervourSpentPercentStat;

	/**
	 * Take an enemy permanently into this character's command.
	 *
	 * THE STAFF'S SUBJUGATE: "if the blow leaves it below half health you take it
	 * permanently: it fights for you until it dies, keeps its own abilities, and
	 * sets alight what it strikes."
	 *
	 * IT STAYS THE CREATURE IT WAS, WHICH IS WHAT "KEEPS ITS OWN ABILITIES"
	 * MEANS. Nothing is destroyed and nothing is spawned: the enemy changes which
	 * side it is on and gains an owner, so its own brain, its own abilities and
	 * its own numbers all come with it. Replacing it with a minion would have
	 * given every thrall in the game the same three attacks.
	 *
	 * THE OWNER IS SET AS WELL AS THE SIDE, and both are load-bearing.
	 * `UCataclysmTeams::TeamOf` walks the owner chain, and `ThingsCommandedBy`
	 * above finds a thrall by asking who owns it.
	 *
	 * A BOSS IS REFUSED. The row says "bosses cannot be taken", which is the same
	 * shape as the stun's boss immunity and read off the same rarity. SO IS A CREATURE
	 * THAT CANNOT BE HURT, The Reaper, by ruling: see `bCannotBeHurt`.
	 *
	 * EVERY REFUSAL IS `WhySubjugateRefuses`'S, since issue #2856, so the line that
	 * reports a refusal reads the same answer this acted on.
	 *
	 * IT DOES NOT CHECK THE HEALTH THRESHOLD OR THE RESERVE. Those belong to the
	 * skill: `HealthThresholdPercent` is a number on one row and the reserve is
	 * paid out of the caster's own pool, and neither is a fact about the creature
	 * being taken.
	 *
	 * @return whether the enemy was taken. False whenever `WhySubjugateRefuses`
	 *         names a reason: anything already on the commander's side, a boss,
	 *         a creature that cannot be hurt, and anything dead
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Command")
	static bool Subjugate(AActor* Commander, AActor* Enemy);

	/**
	 * The first reason `Subjugate` would refuse to take `Enemy` for `Commander`, in the order of
	 * `ECataclysmSubjugateRefusal`, or `None` when it would take it. Issue #2856.
	 *
	 * WHAT `Subjugate` ITSELF REFUSES BY, and what `UCataclysmSummonSkill::Possess` asks BEFORE its blow as well as
	 * after: a target nothing here refuses, with room in the pool, is one the blow must not kill. The owner's ruling of
	 * 2026-11-22: "ok, well it should still take them even if it would kill".
	 */
	static ECataclysmSubjugateRefusal WhySubjugateRefuses(const AActor* Commander, const AActor* Enemy);

	/**
	 * Whether this one reason holds for this pair, asked on its own: the answer for one reason does not depend on
	 * whether an earlier one holds. False for `None` and `Count`. Issue #2856.
	 */
	static bool SubjugateRefusalHolds(ECataclysmSubjugateRefusal Reason, const AActor* Commander,
									  const AActor* Enemy);

	/** A reason in a few words, as the play log prints it: "dead", "boss", "already on its side". Issue #2856. */
	static const TCHAR* SubjugateRefusalName(ECataclysmSubjugateRefusal Reason);

	/**
	 * Every reason `Subjugate` can refuse, each with "yes" or "no" for this pair, in order:
	 * "nothing to take no, not a character no, dead yes, boss no, cannot be hurt no, already on its side no". Issue
	 * #2856.
	 *
	 * WHAT THE PLAY LOG PRINTS WHEN A TAKE IS REFUSED, so a refusal always shows a "yes": `Subjugate` refuses only when
	 * `WhySubjugateRefuses` names one of these, and each is asked here by the same `SubjugateRefusalHolds`.
	 */
	static FString SubjugateRefusalsText(const AActor* Commander, const AActor* Enemy);

	/**
	 * Whether a blow may not kill this creature now, because Subjugate is taking it: its
	 * `ACataclysmCharacterBase::bHeldAliveForATake`. False for anything that is not a character. Issue #2856.
	 *
	 * ASKED BY `UCataclysmVitalAttributeSet` BESIDE A FRIENDLY UNHOLY SIGIL'S HOLD, and held the same way: a write that
	 * would take health to nothing leaves one point, so the blow is never the creature's death.
	 */
	static bool IsHeldAliveForATake(const AActor* Creature);
};
