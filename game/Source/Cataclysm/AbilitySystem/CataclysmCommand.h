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

	/** The `Status.Quarry` tag, requested by name. Invalid if the sheet drops it. */
	static FGameplayTag QuarryTag();

	/**
	 * What this creature should attack because its commander said so, or null.
	 *
	 * THE STAFF'S QUARRY: "everything you command breaks off and attacks it."
	 * `Status.Quarry` was applied for twelve seconds and read by nothing, so the
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
	 * because nothing the Ritualist has spends it: the class has no passive tree
	 * and no designed generator, which is issue #950. When it does, this is where
	 * the reserved amount comes from.
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
