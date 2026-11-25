// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmRetaliation.generated.h"

class AActor;
class UAbilitySystemComponent;

/**
 * Retaliation: what a defender sends back to whatever lands a hit on it.
 *
 * WHAT THE DESIGN SAYS, at docs/Cataclysm_GDD_v2.md, Retaliation. It is a SHARE
 * of the blow, measured before the defender's own mitigation (a flat amount
 * until 2026-09-03, issue #1227); it answers a hit and never a damage over time
 * tick; only a hit that got through provokes it; a minion's blow provokes it and
 * the minion takes it; and what comes back is dealt through the damage formula
 * as a blow that cannot itself be retaliated against, which is what stops two
 * retaliating characters reflecting at one another without end. This comment
 * said "a FLAT amount", "a minion's blow provokes none" and "IS NOT ITSELF A
 * HIT" until issue #2749 corrected it.
 *
 * AND A CREATURE'S THORNS OF GLASS IS NOT RETALIATION SINCE ISSUE #2749 (B2). It
 * strikes back with the creature's own attack damage, whatever the blow it took
 * was worth: `StrikeBack` below. It is decided at the same place and under the
 * same conditions as retaliation.
 *
 * THE RULES LIVED IN `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`
 * AND STILL START THERE. That function is where a blow is resolved and it is the
 * only place that knows a hit got through, so it is still what decides WHETHER
 * to retaliate. What moved here under issues #1047 and #1048 is HOW MUCH, AT
 * WHOM, and WHAT THE DEFENDER GETS BACK FOR IT -- because two Masochist capstone
 * options change all three, and a rule that can be checked by passing numbers in
 * is worth more than one buried in a six hundred line function.
 *
 * A SEPARATE CLASS OF STATIC FUNCTIONS, like `UCataclysmFervour`,
 * `UCataclysmLeech`, `UCataclysmHealthDebt` and `UCataclysmRegeneration`, and
 * for the reason the first of those gives: the rules are arithmetic on a few
 * numbers and a search, so they can be checked without building a whole hit.
 *
 * THE TWO OPTIONS IT CARRIES.
 *
 *   Reprisal Wave    The First Vow, second option. "Your retaliation damage
 *                    strikes every enemy within 4 metres, not only the one that
 *                    hit you." Issue #1047.
 *   Feeding Wound    The Second Vow, second option. "Your life leech applies to
 *                    your retaliation damage as well as to your attacks."
 *                    Issue #1048.
 *
 * A CHARACTER MAY HOLD BOTH, because they belong to different capstones. Every
 * enemy the wave strikes then contributes to the leech, which is what the design
 * already says of leech generally: it is a percentage of the damage actually
 * dealt.
 */
UCLASS()
class CATACLYSM_API UCataclysmRetaliation : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ The stat names, as `game/Data/PassiveEffects.csv` spells them. They are
	//~ the keys `UCataclysmPlayerClassStats::StatToAttribute` is looked up by,
	//~ so they exist once here rather than as a literal at each use.

	/** How much this character strikes back for. */
	static const TCHAR* AmountStat;

	/** How far that reaches, in metres. Zero for only whatever hit it. */
	static const TCHAR* RadiusMetresStat;

	/** Whether life leech applies to it. Zero for no. */
	static const TCHAR* LeechesStat;

	/**
	 * `retaliation_cannot_be_mitigated`: whether whatever this character's retaliation strikes cannot reduce or
	 * mitigate it. A flag: a row states it flat, 1 to 1, and any value above nought is yes. Ruled 2026-10-12 under
	 * the owner's delegation, for "Retaliation damage cannot be reduced or mitigated by the attacker".
	 *
	 * NO GAMEPLAY ATTRIBUTE AND NO BASE, so it is on `UCataclysmPlayerClassStats::StatsWithNoAttribute()`, and
	 * `tools/generate_datatables.py` holds the same name in `RETALIATION_CANNOT_BE_MITIGATED_STAT`. See
	 * `CannotBeMitigatedFor`.
	 */
	static const TCHAR* CannotBeMitigatedStat;

	/**
	 * `retaliation_penetration`: resistance points this character's retaliation ignores, beside the ordinary
	 * `penetration`. The War vow options' second slice, V2b, 2026-11-25: Blood Oath, "Your retaliation damage ignores
	 * 25% of enemy resistances", ruled a retaliation-only penetration stat. Read by
	 * `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` for a blow stamped as retaliation
	 * (`UCataclysmSkillEffects::RetaliationDataName`), asked of the attacker with no tags. No attribute and no base.
	 */
	static const TCHAR* PenetrationStat;

	/**
	 * `blocked_damage_added_to_next_retaliation`: a flag. While it holds, what a block removes is stored, and the next
	 * retaliation payment adds the whole store to its first blow and empties it. The War vow options' second slice,
	 * V2b, 2026-11-25: Spiteful Guard, "Blocked damage is stored and added to your next retaliation hit". See
	 * `UCataclysmAbilitySystemComponent::NoteBlockedDamageForRetaliation`. No attribute and no base.
	 */
	static const TCHAR* BlockedDamageAddedStat;

	/**
	 * Whether this character's retaliation cannot be reduced or mitigated by what it strikes.
	 *
	 * ASKED WITH NO TAGS AND A FALLBACK OF NOUGHT, where retaliation is paid. Retaliation is not a skill and
	 * carries no tags, as `AmountFor` says, so a row requiring a tag would never match. False for every character
	 * with no such row and for an ability system this project did not make.
	 *
	 * WHAT IT CHANGES IS ONE FLAG ON THE BLOW `Pay` SENDS, `FCataclysmHitDelivery::bCannotBeMitigated`, which
	 * lists what the struck character then does not apply and what it still does. It changes nothing about how
	 * much is sent: `AmountFor` is untouched.
	 */
	static bool CannotBeMitigatedFor(const UAbilitySystemComponent* Defender);

	/**
	 * What one metre is in the centimetres Unreal measures in.
	 *
	 * THE ONE CONVERSION, AND IT IS HERE BECAUSE THE STAT IS IN METRES.
	 * `RetaliationRadiusMetres` says why: the design document and the passive
	 * tree both speak in metres, and the number is authored in the design
	 * workbook where a designer writes what the node says.
	 */
	static constexpr float CentimetresPerMetre = 100.0f;

	/**
	 * How much this character strikes back for, given the blow it took.
	 *
	 * A SHARE OF THE BLOW SINCE ISSUE #1227, NOT A FLAT AMOUNT. The stat is
	 * percentage points now, so a character with 15 who is struck for 400
	 * sends back 60. It is measured against what the blow was worth BEFORE
	 * this character's own armour, resistance and reductions took anything
	 * off, so being well defended does not also make you retaliate for less.
	 *
	 * WHY IT HAD TO CHANGE. A flat amount could not follow enemy health
	 * upwards. The Masochist carried 158 and the affix added 9.5 a piece,
	 * against enemies whose health runs from 3,238 to 40,048 at difficulty
	 * tier 8; a character wearing the affix everywhere reached 291 per blow,
	 * which is 137 blows taken to kill one Boss.
	 *
	 * IT COUNTS AS DAMAGE, so the character's increases reach it. That is
	 * what UCataclysmSkillEffects::ModifiedDamage is for, and it is applied
	 * here rather than by the caller so every route gets it.
	 *
	 * ASKED FOR RATHER THAN READ, and that is not a preference. A bonus whose
	 * SIZE grows with a state -- Reciprocity gives "+1% for each point of
	 * Fervour you currently hold" -- is never written onto the gameplay
	 * attribute, because it would be stale the moment the bar moved. Reading the
	 * attribute would drop it in silence and the node would grant nothing.
	 * Issue #980.
	 *
	 * NO SKILL TAGS. Retaliation is not a skill and carries none of its own, and
	 * scoping the defender's retaliation by the attacker's skill tags would be
	 * the wrong question.
	 *
	 * THE ATTRIBUTE IS THE FALLBACK, so a character with no such bonus, and an
	 * ability system this project did not make, both get exactly what they got
	 * before any of this existed.
	 */
	static float AmountFor(const UAbilitySystemComponent* Defender,
						   float DamageTaken, float PercentInPlaceOfTheStat = -1.0f);

	/**
	 * How far this character's retaliation reaches, in metres.
	 *
	 * ZERO FOR EVERY CHARACTER WITHOUT REPRISAL WAVE, which means it reaches
	 * only whatever hit it.
	 */
	static float RadiusMetresFor(const UAbilitySystemComponent* Defender);

	/** Whether this character's life leech applies to its retaliation. */
	static bool LeechesFor(const UAbilitySystemComponent* Defender);

	/**
	 * Everything one payment of retaliation strikes.
	 *
	 * THE ATTACKER IS ALWAYS FIRST AND IS ALWAYS IN THE LIST, whether or not it
	 * is inside the radius. Reprisal Wave reads "strikes every enemy within 4
	 * metres, NOT ONLY the one that hit you", which adds to that target rather
	 * than replacing it. A ranged attacker twenty metres away therefore still
	 * takes retaliation, exactly as it did before the option existed.
	 *
	 * THE SPHERE IS CENTRED ON THE RETALIATING CHARACTER. The sentence names no
	 * other anchor, and its contrast with "the one that hit you" makes the
	 * character the subject.
	 *
	 * ENEMIES ONLY, AND NOT THE CHARACTER ITSELF.
	 * `UCataclysmTargeting::FindEnemiesInSphere` decides both, and it also
	 * refuses scenery and the dead.
	 *
	 * NOBODY IS STRUCK TWICE. The attacker is normally inside the sphere as
	 * well, so it is filtered out of what the search returned.
	 */
	static TArray<AActor*> TargetsOf(const UAbilitySystemComponent* Defender,
									 AActor* Attacker);

	/**
	 * Strike back, and take the leech if this character has bought it.
	 *
	 * @param Defender    the retaliating character's ability system
	 * @param Instigator  who the health loss is credited to. The attribute set
	 *                    passes its owning actor, which for the player is the
	 *                    player state.
	 * @param Attacker    what hit the character, which is the effect causer
	 * @return how much health was actually taken, across every target
	 *
	 * EACH TARGET TAKES THE WHOLE AMOUNT RATHER THAN A SHARE OF IT. Nothing in
	 * Reprisal Wave's sentence divides it. Splitting it would leave the total unchanged and so make the
	 * option worth nothing, and all twelve capstone options were rewritten on
	 * 2026-08-27 as pure upgrades with no drawbacks. Issue #1031.
	 *
	 * WHAT IS RETURNED IS WHAT WAS TAKEN, NOT WHAT WAS SENT, and the difference
	 * is the design's overkill rule: "An enemy with 25 health left, hit for 400,
	 * contributes 25 to the leech calculation and not 400." It is measured off
	 * each target's health rather than assumed.
	 *
	 * IT RUNS THROUGH THE ORDINARY DAMAGE FORMULA SINCE ISSUE #1227, and it
	 * used to bypass it entirely. `ReduceHealthDirectly` wrote the `Health`
	 * attribute rather than the `Damage` meta attribute, so the target's
	 * armour, resistance, evasion and block did nothing to it. It goes
	 * through `ApplyDirectDamage` now, so all of them apply.
	 *
	 * UNLESS THE DEFENDER'S ROWS SAY THE STRUCK CHARACTER CANNOT MITIGATE IT. Ruled 2026-10-12. For a defender
	 * that `CannotBeMitigatedFor` answers yes for, the blow still goes through `ApplyDirectDamage` and carries
	 * `FCataclysmHitDelivery::bCannotBeMitigated`, and the calculation skips evasion, block, armour, resistance,
	 * damage reduction and `damage_taken` for it, the fourth and the last only where they would lower the blow:
	 * the flag never lowers a payment. It is not a return to `ReduceHealthDirectly`: the blow still
	 * meets the temporary absorb and the energy shield, still does nothing to what takes no damage, and still
	 * raises what a blow raises. A reflected hit, `PayReflected` below, is paid through here and takes the flag too.
	 *
	 * AND IT IS STILL NOT A HIT, WHICH IS FOUR SEPARATE FLAGS ON THE BLOW.
	 * Going through the damage formula would otherwise make it one:
	 *
	 *   bCannotBeRetaliatedAgainst  load-bearing. Without it two characters
	 *                               who both retaliate reflect at one another
	 *                               without end.
	 *   bCannotCriticallyStrike     Path of Exile's rule for reflected
	 *                               damage. This is not a blow you aimed.
	 *   bCarriesNoWeaponSubType     retaliation is not a weapon swing, so the
	 *                               weapon's slashing or piercing bonus is not
	 *                               its to carry.
	 *   bCannotLeech                the ordinary leech path must not fire.
	 *                               The Masochist's Feeding Wound has its own
	 *                               route below and is untouched.
	 *
	 * @param DamageTaken  what the blow was worth before this character's own
	 *                     mitigation, which is what the share is taken of
	 */
	static float Pay(UAbilitySystemComponent* Defender, AActor* Instigator,
					 AActor* Attacker, float DamageTaken, float PercentInPlaceOfTheStat = -1.0f);

	/**
	 * Pays a REFLECTED hit back: the whole of `DamageTaken`, which is the hit before the defender's mitigation,
	 * priced and delivered exactly as retaliation is, with 100 in place of the defender's retaliation stat. Ruled
	 * 2026-10-06, for "Melee attacks that hit you have a 10%-20% chance to be reflected back as retaliation
	 * damage". The defender's own retaliation stat is not also paid for that hit: it took none of it.
	 */
	static float PayReflected(UAbilitySystemComponent* Defender, AActor* Instigator, AActor* Attacker,
							  float DamageTaken);

	/**
	 * A retaliation pulse: every enemy within `RadiusMetres` of `Instigator` is paid the defender's retaliation, sized as
	 * a blow's would be by the last blow the defender took this floor (`LastBlowTakenThisFloor`), priced and delivered
	 * as `Pay` delivers. Nothing when no blow was taken this floor. Vengeful Discharge (the War rulings' R7): "When this
	 * drain reduces you to 0 Fervour, you retaliate against every enemy within 4 metres for 100% of your retaliation
	 * damage. At 5 points: this also applies Bleed to every enemy it strikes." The size is the coordinating session's
	 * ruling of 2026-11-25. `bApplyBleed` lays Bleed on every enemy the pulse took health from.
	 *
	 * @return the health taken from every enemy struck, altogether
	 */
	static float PayPulse(UAbilitySystemComponent* Defender, AActor* Instigator, float RadiusMetres, bool bApplyBleed);

	/**
	 * A creature carrying Thorns of Glass strikes back at whatever hit it: one blow of its own attack damage, sized by
	 * its own strength and reduced by the attacker's defences, whatever the blow it took was worth. Issue #2749 (B2),
	 * the owner's ruling of 2026-11-18: "When struck, a reflecting creature hits back for its own attack damage, sized
	 * by its strength like any other hit and reduced by your defences. Your own damage no longer matters to it."
	 *
	 * DEALT THROUGH `UCataclysmSkillEffects::ApplyHit` AT `StrikeBackDamagePercent` OF THE CREATURE'S ATTACK DAMAGE,
	 * the pipeline its own basic attack takes (`ACataclysmEnemyCharacter::AttackTarget`). Its attack damage attribute
	 * already holds its rarity's scale, any rule's part and the floor's multipliers, so the figure is the creature's
	 * own current one. No skill tags: it is no skill.
	 *
	 * A RETURNED BLOW, WITH THE FOUR FLAGS EVERY RETURNED BLOW CARRIES (see `Pay`): it cannot be retaliated against,
	 * so a retaliating player does not answer it; it cannot critically strike; it carries no weapon sub-type; it cannot
	 * leech. And it is marked `FCataclysmHitDelivery::bIsRetaliation`, so the player's hit log says it was returned.
	 * Ruled by the coordinating session on 2026-11-18 (Q2).
	 *
	 * CALLED BY `UCataclysmVitalAttributeSet` where it decides retaliation, under the same conditions: a hit that got
	 * through, not a damage over time tick, and not a blow that cannot be retaliated against. So once per hit, and a
	 * minion's blow is struck back at the minion. A hit that kills the creature is still struck back, because that
	 * branch runs before the hit is written to health, as retaliation's does: a judgement, recorded in
	 * `docs/DECISIONS.md`.
	 *
	 * @return how much health the attacker lost, measured off its health
	 */
	static float StrikeBack(AActor* Creature, AActor* Attacker);

	/** How much of the creature's own attack damage a strike back is: all of it. Issue #2749 (B2). */
	static constexpr float StrikeBackDamagePercent = 100.0f;
};
