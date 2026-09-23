// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmCommand.generated.h"

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
	 * Who this creature follows, or null if it follows nobody.
	 *
	 * THE OTHER DIRECTION OF `ThingsCommandedBy` ABOVE, and it is asked from the
	 * creature's own side: a minion choosing what to attack needs to know whose
	 * orders to take, and walking every character in the level to find out would
	 * be the wrong way round.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Command")
	static AActor* CommanderOf(const AActor* Follower);

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
	 * Whether this character's resource pool has room to hold another thrall.
	 *
	 * THE STAFF'S SUBJUGATE: "holding a thrall reserves 30 Fervour, so your army
	 * is only as large as your pool." The Ritualist's pool is 150 in
	 * `game/Data/ClassStats.csv`, so five, and every point of maximum resource a
	 * passive tree ever grants is progress toward a sixth. That is the design's
	 * own arithmetic, recorded in `docs/DECISIONS.md` on 2026-09-01.
	 *
	 * THE CAP IS THE WHOLE OF WHAT RESERVATION DOES TODAY, and that is worth
	 * saying plainly. Nothing is subtracted from the character's usable pool,
	 * which is issue #1160, and so far that has no visible effect: the
	 * Ritualist fills the pool (its tree's first node, Fervour, generates it
	 * from minions) but no node or skill of the Ritualist's spends it. The two
	 * nodes that spend Fervour today, Wrung Out and Bought With Ruin, are the
	 * Ravager's. Issue #1478 is the question of what should spend it. When
	 * something the Ritualist holds does, this is where the reserved amount
	 * comes from.
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
	 * A REDUCTION AND NOT THE RESERVE ITSELF. Five skills state a reserve of
	 * their own -- Subjugate 30, Summon Imp 10, three deployables 5 -- so a
	 * stat holding the figure would state one of them twice and be wrong for
	 * the other four.
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
	 * The stat holding how many more minions a skill that states a cap may keep
	 * active. Issue #1718.
	 *
	 * `Ritualist_keystone_b_kA` The Swarm is the only source: "Each skill that
	 * limits how many of its minions may be active allows 2 more." Its row is
	 * flat 2.
	 *
	 * IT REACHES ONLY A SKILL WHOSE ROW STATES A CAP. A skill stating none has
	 * no limit at all, and a bonus added to a cap of zero would give it one it
	 * was never designed to have.
	 */
	static const TCHAR* MinionCapBonusStat;

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
	 * shape as the stun's boss immunity and read off the same rarity.
	 *
	 * IT DOES NOT CHECK THE HEALTH THRESHOLD OR THE RESERVE. Those belong to the
	 * skill: `HealthThresholdPercent` is a number on one row and the reserve is
	 * paid out of the caster's own pool, and neither is a fact about the creature
	 * being taken.
	 *
	 * @return whether the enemy was taken. False for anything already on the
	 *         commander's side, for a boss, and for anything dead
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Command")
	static bool Subjugate(AActor* Commander, AActor* Enemy);
};
