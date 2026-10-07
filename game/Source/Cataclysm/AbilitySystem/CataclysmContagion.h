// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
// For `UCataclysmSkillEffects::FRunningAilment`, which a blast read at a death holds.
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "CataclysmContagion.generated.h"

class AActor;
class UAbilitySystemComponent;

/**
 * What a blast from a dying enemy needs, READ AT THE DEATH. Ruled 2026-10-07.
 *
 * A blast may run after the death that causes it has been handled: one heard
 * while its wearer is already acting on an event waits until that has finished.
 * So everything is read here, and nothing is read from the dead enemy later.
 */
struct FCataclysmBlastRead
{
	/** Where the body was. */
	FVector Position = FVector::ZeroVector;

	/** The dead enemy, ONLY TO LEAVE IT OUT of those the blast catches. Never read. */
	TWeakObjectPtr<AActor> Dead;

	/** The wearer's ailments that were running on the body, each with what it had left. */
	TArray<UCataclysmSkillEffects::FRunningAilment> Carried;
};

/**
 * A lasting harmful effect passing from the character carrying it to an enemy.
 *
 * WHAT IT IS FOR. Two Masochist nodes, issues #1057 and #1058:
 *
 *   Beacon of Despair     "You radiate an aura that applies a random debuff to
 *                          enemies within 6 metres every 3 seconds. The duration
 *                          of those debuffs is increased by 4% per point."
 *   Contagious Torment    "When a debuff on you deals damage, enemies within 6
 *                          metres have a 1% chance per point to receive a random
 *                          debuff you carry."
 *
 * THEY ARE THE SAME ACTION WITH DIFFERENT TRIGGERS, which is why they are one
 * file. Both take a debuff the character is already carrying and put it on
 * enemies standing within six metres. One fires because three seconds passed and
 * the other because something ticked; everything after that is shared.
 *
 * IT IS THE FIRST THING IN THE GAME THAT PUTS A NAMED STATUS EFFECT ON ANYBODY.
 * `game/Data/StatusEffects.csv` has described 52 effects since the data pipeline
 * was built and burning, bleeding and stunning are the only three anything ever
 * applied, each through a function that names it in C++.
 *
 * WHAT CAN ACTUALLY SPREAD TODAY IS BLEEDING AND BURNING, and that is a fact
 * about the rest of the game rather than a limit here. Two things narrow it:
 *
 *   what a character can CARRY   `UCataclysmDebuffs::DebuffRootNames` admits
 *                                `Keyword.DoT` and `State.Stunned`, because
 *                                nothing in the game puts a `Status.` effect on
 *                                a player yet. Issue #899 is the eleven affixes
 *                                that would.
 *   what a row STATES            `UCataclysmSkillEffects::StatusEffectNumbers`
 *                                refuses a row with no duration, and 41 of the
 *                                52 state neither a duration nor an amount.
 *
 * A STUN DOES NOT PASS ON. A stunned character carries `State.Stunned`, and no
 * row of the status effect table has a name that reduces to `Stunned` -- the row
 * is `Debuff_Stun`. So `UCataclysmSkillEffects::StatusEffectRowForTag` finds
 * nothing and the stun is skipped. That is the right outcome, because a stun
 * applied with no hit behind it would go round both of the design's
 * anti-stun-lock rules, but it follows from the data rather than from a check
 * here.
 *
 * WHAT THE ENEMY RECEIVES IS THE DESIGNED EFFECT, NOT A COPY OF THE ONE ON THE
 * CHARACTER. A burn on the Masochist was made by whoever set it alight and
 * carries their damage over time stats and however much of its duration is
 * left. What the enemy gets is the row's own numbers with the MASOCHIST as the
 * instigator, so the Masochist's own damage over time stats scale it, exactly as
 * they would if the Masochist had applied it with a skill. Reproducing the
 * original would mean a Masochist's aura was as strong as whatever last hurt it.
 */
UCLASS()
class CATACLYSM_API UCataclysmContagion : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** How far both nodes reach, in metres. Both say six. */
	static constexpr float RadiusMetres = 6.0f;

	/** Unreal works in centimetres and the design is written in metres. */
	static constexpr float CentimetresPerMetre = 100.0f;

	/**
	 * Put the effect this tag names on the target.
	 *
	 * IT PICKS THE APPLIER FROM WHAT THE ROW STATES rather than from which tag
	 * branch it came out of. A row with a per-tick amount is a damage over time
	 * effect and goes through `ApplyDamageOverTime`; a row with a duration and no
	 * amount is a debuff whose magnitude this project has no hook for yet and
	 * goes through `ApplyTagForDuration`, which is what that function exists for.
	 * Six rows are the first kind and five are the second. A row stating a share
	 * of the target's current health, which only Void Splinter's does, goes
	 * through `ApplyShareOfHealthOverTime`. Issue #915.
	 *
	 * THE TAG IT GRANTS IS THE ONE PASSED IN, not one worked out from the row.
	 * The two are the same effect under different branches -- `Status.DoT.Bleed` and
	 * `Keyword.DoT.Bleed` -- and granting the branch the character was carrying
	 * keeps `UCataclysmDebuffs::CountOn` able to see it on the enemy too.
	 *
	 * @param ExtraDurationPercent  what Beacon of Despair adds, as a percentage
	 *                              on top of the row's stated duration. Zero for
	 *                              Contagious Torment, whose sentence says
	 *                              nothing about duration.
	 * @return false when the tag names no row, the row states nothing usable, or
	 *         either actor has no ability system
	 */
	static bool SpreadOne(AActor* Instigator, AActor* Target,
						  const FGameplayTag& EffectTag,
						  float ExtraDurationPercent = 0.0f);

	/**
	 * One of the debuffs this character carries that could be put on somebody
	 * else, chosen at random, or an invalid tag if it carries none.
	 *
	 * BOTH SENTENCES SAY "A RANDOM DEBUFF", so the choice is made here rather
	 * than by taking the first. A tag container's order is the engine's and is
	 * stable enough to look deliberate without being it: a character bleeding and
	 * burning would spread whichever the container happened to hold first, every
	 * time, for ever.
	 *
	 * ONLY THE ONES THAT COULD ACTUALLY LAND ARE CANDIDATES. A debuff whose tag
	 * names no row -- a stun -- is filtered out BEFORE the roll rather than after
	 * it, so a bleeding and stunned character always spreads its bleed instead of
	 * spreading nothing half the time.
	 *
	 * @param PinnedIndex  which of the candidates to take, for a test that
	 *                     needs a known answer. Negative rolls for real.
	 */
	static FGameplayTag PickSpreadable(const UAbilitySystemComponent* Carrier,
									   int32 PinnedIndex = -1);

	/**
	 * EVERY debuff this character carries that could be put on somebody else, in the
	 * order the ability system holds them, or an empty list if it carries none.
	 *
	 * THE SAME FILTER `PickSpreadable` USES, AND THE ONLY COPY OF IT. That function now
	 * chooses from this list, so a change to what counts as spreadable reaches both.
	 *
	 * WHAT ASKS FOR IT. The dungeon rule `Pestilence_Epidemic`: "applying all of the dead
	 * enemy's remaining debuffs". Asking `PickSpreadable` for index 0, then 1, then 2
	 * cannot answer that -- an index past the end falls back to a RANDOM candidate rather
	 * than to nothing, so the walk would never end and would apply the same debuff again
	 * and again. Issues #1820 and #41.
	 *
	 * THE ORDER IS THE ABILITY SYSTEM'S AND IS NOT PROMISED, which is what
	 * `UCataclysmDebuffs::TagsOn` says about the container this reads. A caller wanting
	 * one at random must still choose, which is what `PickSpreadable` does.
	 */
	static TArray<FGameplayTag> EverySpreadable(
		const UAbilitySystemComponent* Carrier);

	// --- Beacon of Despair ----------------------------------------------------

	/**
	 * The stat naming how much longer the aura's debuffs last, as a percentage
	 * added on top. As `game/Data/PassiveEffects.csv` spells it.
	 *
	 * IT IS ALSO WHAT SAYS THE CHARACTER HOLDS THE NODE. Zero means no points,
	 * because the node cannot be held at zero points, so `AuraStep` can refuse
	 * every character in the game with one read and never touch the clock. That
	 * is the shape `UCataclysmNova::DamageStat` already uses.
	 */
	static const TCHAR* AuraDurationStat;

	/** Seconds between one application of the aura and the next. */
	static constexpr float AuraIntervalSeconds = 3.0f;

	/**
	 * Apply the aura to everything standing in it, if three seconds have passed.
	 *
	 * A JOB ON THE PER-CHARACTER STEP RATHER THAN A TIMER OF ITS OWN, for the
	 * reason `UCataclysmNova::Step` gives: `ACataclysmCharacterBase::
	 * RegenerationStep` already runs several times a second and a timer per
	 * character is one more thing to cancel when one dies. The three second
	 * interval is kept by a timestamp on the ability system component.
	 *
	 * THE SAME DEBUFF FOR EVERY ENEMY IN ONE PULSE, not one roll each. "applies a
	 * random debuff to enemies within 6 metres" makes the debuff the property of
	 * the pulse and the enemies its targets, which is the opposite reading from
	 * Contagious Torment's sentence and is why the two are not one function.
	 *
	 * @param PinnedIndex  which debuff to spread, for a test. Negative rolls.
	 * @return how many enemies received one
	 */
	static int32 AuraStep(AActor* Character, int32 PinnedIndex = -1);

	// --- Contagious Torment ---------------------------------------------------

	/**
	 * The stat naming the chance one enemy has of catching a debuff when one on
	 * the character deals damage, as a percentage. As
	 * `game/Data/PassiveEffects.csv` spells it.
	 */
	static const TCHAR* TormentChanceStat;

	/**
	 * Roll for every enemy in range, because a debuff on the character just
	 * dealt damage.
	 *
	 * ONE ROLL PER ENEMY AND A FRESH DEBUFF CHOSEN FOR EACH. "enemies within 6
	 * metres have a 1% chance per point to receive a random debuff you carry"
	 * makes both the chance and the debuff a property of the enemy, so two
	 * enemies can catch two different things from one tick.
	 *
	 * CALLED FROM WHERE THE TICK LANDS. `UCataclysmVitalAttributeSet::
	 * PostGameplayEffectExecute` is the only place that knows a hit was damage
	 * over time, and it is already where retaliation and the Breaking Point
	 * conversion decide whether they fire.
	 *
	 * PINNED ROLLS FOR A TEST, the shape `UCataclysmDamageCalculation::Resolve`
	 * already uses for evasion, blocking and critical strikes: a negative value
	 * rolls for real and anything else is taken as the roll.
	 *
	 * @param PinnedRoll   0 to 100, or negative to roll
	 * @param PinnedIndex  which debuff to spread, or negative to roll
	 * @return how many enemies received one
	 */
	static int32 SpreadOnDebuffDamage(AActor* Character,
									  float PinnedRoll = -1.0f,
									  int32 PinnedIndex = -1);

	// --- Empathic Link --------------------------------------------------------

	/**
	 * The stat naming the chance each debuff on a dying enemy has of passing to
	 * another enemy near it, as a percentage. As
	 * `game/Data/PassiveEffects.csv` spells it.
	 */
	static const TCHAR* DeathChanceStat;

	/**
	 * Roll for each debuff a creature that has just died was carrying, and pass
	 * the ones that succeed to one other creature standing near its body.
	 *
	 * THE DEBUFFS ARE THE DYING CREATURE'S AND THE STAT IS THE PLAYER'S, which
	 * is what makes this different from everything else in this file. Every other
	 * function here spreads what the character holding the node is carrying. The
	 * player need not be anywhere near the body.
	 *
	 * ONE ROLL PER DEBUFF, AND ONE RANDOM RECIPIENT FOR EACH THAT PASSES. "its
	 * debuffs have a 2% chance per point to pass to a random enemy within 6
	 * metres" makes the chance a property of each debuff and names a single
	 * recipient. That is a third shape: Beacon of Despair picks one debuff for
	 * everybody, Contagious Torment rolls per enemy, and this rolls per debuff.
	 *
	 * WHO COUNTS AS AN ENEMY IS ASKED OF THE PLAYER AND NOT OF THE CORPSE.
	 * `UCataclysmTargeting::FindEnemiesInSphere` decides sides from the actor
	 * passed to it, so passing the dying creature would find the player. The
	 * search runs from the player with the body's location as its centre.
	 *
	 * THE BODY IS NEVER A RECIPIENT. `ACataclysmEnemyCharacter::HandleDeath`
	 * marks it dead and switches its collision off before this runs, and that
	 * search refuses the dead.
	 *
	 * @param Dying    the creature whose debuffs are passing on
	 * @param Killer   the character holding the node, whose stat decides the
	 *                 chance and who the applied effects are credited to
	 * @param PinnedRoll  0 to 100, or negative to roll
	 * @return how many debuffs passed
	 */
	static int32 SpreadOnDeath(AActor* Dying, AActor* Killer,
							   float PinnedRoll = -1.0f);

	/** How many enemies a Disease passes to by itself when its carrier dies. Issue #919. */
	static constexpr int32 DiseaseSpreadsToByItself = 2;

	/** How near the body a creature must stand to receive one, in metres: the project's "nearby". */
	static constexpr float SpreadFromTheDyingMetres = 5.0f;

	/**
	 * Pass a dying creature's damage over time ailments to the enemies nearest
	 * its body. Issue #919, the owner's "build it" of 2026-10-06, and issue
	 * #1833 for the rows.
	 *
	 * THE DESIGN DOCUMENT'S OWN RULE FOR DISEASE, and for nothing else by
	 * itself: "on the target's death it spreads its remaining duration to the 2
	 * nearest enemies within 5 metres". A row adds to that count, and gives a
	 * count to an ailment that has none: `ECataclysmAilmentRider::SpreadOnDeath`,
	 * read off the DYING creature, where the applier's rows hung it when the
	 * ailment was applied.
	 *
	 * A DIFFERENT THING FROM `SpreadOnDeath` ABOVE, AND BOTH RUN. That one is a
	 * passive's chance, rolls per debuff, picks one creature at random within 6
	 * metres and applies the designed effect afresh. This one is certain, picks
	 * the nearest, and hands over a COPY: the same damage a second for the time
	 * the original had left, from whoever applied it. So each hop is shorter
	 * than the last, which is what ends a chain.
	 *
	 * WHO APPLIED IT DECIDES WHO IS AN ENEMY, and need not be a player or be
	 * near. The search is asked of the applier with the body as its centre.
	 *
	 * THE BODY IS NEVER A RECIPIENT, whether or not it has been marked dead yet.
	 *
	 * NOR IS AN ENEMY THAT ALREADY CARRIES THE AILMENT. Ruled 2026-10-06: it is
	 * passed over, not refreshed, and the next nearest takes its place.
	 *
	 * @return how many copies were applied
	 */
	static int32 SpreadFromTheDying(AActor* Dying);

	/**
	 * Raise `afflicted_death` on every character that has a damage over time
	 * ailment running on `Dying`. Ruled 2026-10-07, for "Plague Doctor (10-Piece
	 * Bonus): When an enemy dies while affected by a DoT from you".
	 *
	 * ON WHOEVER APPLIED THE AILMENT, NOT ON THE KILLER AS SUCH: whoever landed
	 * the last blow hears it only if an ailment of theirs is on the body. Once
	 * for each such character, however many of its ailments the body carries.
	 *
	 * IT CARRIES the dead enemy as the event's target, its MAXIMUM health as the
	 * amount, and as tags the ailments of that character's that were running on
	 * the body. One application of an ailment runs on a character at a time, so
	 * an ailment counts for the one whose application is running.
	 *
	 * @return how many characters heard it
	 */
	static int32 AnnounceAfflictedDeath(AActor* Dying);

	/**
	 * The blast of "Plague Doctor (10-Piece Bonus)". Ruled 2026-10-07.
	 *
	 * FIRST THE BLAST: `Damage` to every enemy of `Wearer` within 5 metres of
	 * `Dead`, the body left out, each struck once. It is the wearer's direct
	 * area damage, so it goes through the target's defences as any area hit
	 * does; it is not retaliated against, does not critically strike, does not
	 * leech, and is raised by none of the wearer's increases: the figure is a
	 * share of the dead enemy's health and nothing of the wearer's.
	 *
	 * THEN THE COPIES: each ailment in `TheirAilments` that `Wearer` had running
	 * on the body is put on each of those enemies still alive, as a spread copy
	 * with what it had left. An enemy already carrying that ailment is passed
	 * over. An enemy the blast killed receives nothing.
	 *
	 * A COPY IS A SPREAD COPY: it raises no `dot_applied`, refreshes nothing, and
	 * a Void Splinter copy detonates nothing.
	 *
	 * @return how many enemies the blast struck
	 */
	static int32 BlastFromTheDying(AActor* Wearer, AActor* Dead, float Damage,
								   const FGameplayTagContainer* TheirAilments);

	/**
	 * The first half of `BlastFromTheDying`: where `Dead` is and which of
	 * `TheirAilments` `Wearer` has running on it, with what each has left.
	 * Called at the death.
	 */
	static FCataclysmBlastRead ReadForABlast(AActor* Wearer, AActor* Dead,
											 const FGameplayTagContainer* TheirAilments);

	/**
	 * The second half: the blast and then the copies, from what was read. It
	 * reads nothing from the dead enemy.
	 *
	 * @return how many enemies the blast struck
	 */
	static int32 BlastAt(AActor* Wearer, const FCataclysmBlastRead& Read, float Damage);
};
