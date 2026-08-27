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
 * WHAT THE DESIGN SAYS, at docs/Cataclysm_GDD_v2.md, Retaliation. It is a FLAT
 * amount rather than a share of the hit; it answers a hit and never a damage
 * over time tick; only a hit that got through provokes it; a minion's blow
 * provokes none; and what comes back IS NOT ITSELF A HIT, which is what stops
 * two retaliating characters reflecting at one another without end.
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
	 * What one metre is in the centimetres Unreal measures in.
	 *
	 * THE ONE CONVERSION, AND IT IS HERE BECAUSE THE STAT IS IN METRES.
	 * `RetaliationRadiusMetres` says why: the design document and the passive
	 * tree both speak in metres, and the number is authored in the design
	 * workbook where a designer writes what the node says.
	 */
	static constexpr float CentimetresPerMetre = 100.0f;

	/**
	 * How much this character strikes back for.
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
	static float AmountFor(const UAbilitySystemComponent* Defender);

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
	 * EACH TARGET TAKES THE WHOLE AMOUNT RATHER THAN A SHARE OF IT. Retaliation
	 * is a flat amount by design, and nothing in Reprisal Wave's sentence
	 * divides it. Splitting it would leave the total unchanged and so make the
	 * option worth nothing, and all twelve capstone options were rewritten on
	 * 2026-08-27 as pure upgrades with no drawbacks. Issue #1031.
	 *
	 * WHAT IS RETURNED IS WHAT WAS TAKEN, NOT WHAT WAS SENT, and the difference
	 * is the design's overkill rule: "An enemy with 25 health left, hit for 400,
	 * contributes 25 to the leech calculation and not 400." It is measured off
	 * each target's health rather than assumed.
	 *
	 * STILL NOT A HIT, FOR EVERY TARGET.
	 * `UCataclysmSkillEffects::ReduceHealthDirectly` writes the `Health`
	 * attribute rather than the `Damage` meta attribute, so none of the
	 * mitigation order runs and none of the targets retaliates back.
	 */
	static float Pay(UAbilitySystemComponent* Defender, AActor* Instigator,
					 AActor* Attacker);
};
