// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmContagion.generated.h"

class AActor;
class UAbilitySystemComponent;

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
	 * Six rows are the first kind and five are the second.
	 *
	 * THE TAG IT GRANTS IS THE ONE PASSED IN, not one worked out from the row.
	 * The two are the same effect under different branches -- `Status.Bleed` and
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
};
