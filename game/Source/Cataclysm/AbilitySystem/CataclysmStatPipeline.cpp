// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmStatPipeline.h"
// For the Cripple and Weaken tags the three ailment conditions name. Issue
// #1515. The tag names live there beside the other debuff vocabulary rather
// than being spelled again here; this is a .cpp include, so it adds no header
// dependency and the pipeline's own header stays free of it.
#include "AbilitySystem/CataclysmDebuffs.h"
#include "Cataclysm.h"

FGameplayTag UCataclysmStatPipeline::GlobalScopeTag()
{
	// Requested rather than constructed, so that a typo here fails as an
	// invalid tag instead of silently matching nothing. The tag is declared in
	// game/Config/Tags/CataclysmTags.ini, generated from the design workbook.
	static const FGameplayTag Tag =
		FGameplayTag::RequestGameplayTag(FName(TEXT("Scope.Global")),
										 /*ErrorIfNotFound=*/false);
	return Tag;
}

bool UCataclysmStatPipeline::CanGrantMore(ECataclysmModifierSource Source)
{
	switch (Source)
	{
	case ECataclysmModifierSource::Gem:
	case ECataclysmModifierSource::PassiveKeystone:
	case ECataclysmModifierSource::Enchantment:

	// A skill's own buff is authored the way those three are, not rolled, so
	// the readability rule below does not apply to it.
	case ECataclysmModifierSource::SkillBuff:

	// So is a dungeon modifier's rule, and Starvation's share of a maximum is a
	// Less multiplier on the finished figure. Issue #41.
	case ECataclysmModifierSource::DungeonRule:
		return true;

	// An ordinary affix is flat or increased and never more. That is what keeps
	// a rare drop readable, and it is the reason an enchantment is worth a slot
	// that an affix could have taken.
	case ECataclysmModifierSource::GearAffix:
	case ECataclysmModifierSource::GearImplicit:
	case ECataclysmModifierSource::Attribute:
	default:
		return false;
	}
}

namespace
{
	/**
	 * Every condition and scale name a data sheet may use. Issue #45.
	 *
	 * THE SPELLINGS ARE `CONDITIONS` AND `SCALES` IN
	 * `tools/generate_datatables.py`, and
	 * `tools/tests/test_stat_condition_names_match_the_engine.py` reads these
	 * two tables out of this file and fails if either differs from the
	 * generator's. Keep one entry per line in this shape, which is what that test
	 * reads.
	 */
	struct FNamedStatCondition
	{
		const TCHAR* Name;
		ECataclysmStatCondition Condition;
	};

	const FNamedStatCondition NamedStatConditions[] = {
		{ TEXT("health_at_or_below"),           ECataclysmStatCondition::HealthAtOrBelowPercent },
		{ TEXT("health_below"),                 ECataclysmStatCondition::HealthBelowPercent },
		{ TEXT("health_above"),                 ECataclysmStatCondition::HealthAbovePercent },
		{ TEXT("health_at_or_above"),           ECataclysmStatCondition::HealthAtOrAbovePercent },
		{ TEXT("seconds_after_health_cost"),    ECataclysmStatCondition::WithinSecondsOfHealthCost },
		{ TEXT("seconds_after_foreign_damage"), ECataclysmStatCondition::WithinSecondsOfForeignDamage },
		{ TEXT("seconds_after_charge_skill"),   ECataclysmStatCondition::WithinSecondsOfChargeSkill },
		{ TEXT("seconds_after_basic_attack"),   ECataclysmStatCondition::WithinSecondsOfBasicAttack },
		{ TEXT("seconds_after_block"),          ECataclysmStatCondition::WithinSecondsOfBlock },
		{ TEXT("seconds_after_summon"),         ECataclysmStatCondition::WithinSecondsOfSummon },
		{ TEXT("seconds_after_dodge"),          ECataclysmStatCondition::WithinSecondsOfEvade },
		{ TEXT("seconds_after_hit_taken"),      ECataclysmStatCondition::WithinSecondsOfHitTaken },
		{ TEXT("seconds_after_resource_full"),  ECataclysmStatCondition::WithinSecondsOfClassResourceFull },
		{ TEXT("seconds_after_resource_empty"), ECataclysmStatCondition::WithinSecondsOfClassResourceEmpty },
		{ TEXT("skill_health_cost_above"),      ECataclysmStatCondition::SkillHealthCostAbovePercent },
		{ TEXT("while_bleeding"),               ECataclysmStatCondition::WhileBleeding },
		{ TEXT("class_resource_at_maximum"),    ECataclysmStatCondition::ClassResourceAtMaximum },
		{ TEXT("hit_is_melee_attack"),          ECataclysmStatCondition::HitIsMeleeAttack },
		{ TEXT("hit_is_ranged_attack"),         ECataclysmStatCondition::HitIsRangedAttack },
		{ TEXT("hit_is_spell"),                 ECataclysmStatCondition::HitIsSpell },
		{ TEXT("opponent_is_boss"),             ECataclysmStatCondition::OpponentIsBoss },
		{ TEXT("opponent_is_staggered"),        ECataclysmStatCondition::OpponentIsStaggered },
		{ TEXT("target_is_staggered"),          ECataclysmStatCondition::TargetIsStaggered },
		{ TEXT("target_is_boss"),               ECataclysmStatCondition::TargetIsBoss },
		{ TEXT("target_is_not_boss"),           ECataclysmStatCondition::TargetIsNotBoss },
		{ TEXT("while_moving"),                 ECataclysmStatCondition::WhileMoving },
		{ TEXT("while_stationary"),             ECataclysmStatCondition::WhileStationary },
		{ TEXT("stationary_for_seconds"),       ECataclysmStatCondition::StationaryForSeconds },
		{ TEXT("metres_moved_before_attack"),   ECataclysmStatCondition::MetresMovedBeforeAttack },
		{ TEXT("not_attacked_for_seconds"),     ECataclysmStatCondition::NotAttackedForSeconds },
		{ TEXT("attacker_beyond_metres"),       ECataclysmStatCondition::OpponentBeyondMetres },
		{ TEXT("target_within_metres"),         ECataclysmStatCondition::TargetWithinMetres },
		{ TEXT("enemies_in_reach_at_least"),    ECataclysmStatCondition::EnemiesInReachAtLeast },
		{ TEXT("target_carries_cripple"),       ECataclysmStatCondition::TargetCarriesCripple },
		{ TEXT("target_carries_cripple_and_weaken"),
												ECataclysmStatCondition::TargetCarriesCrippleAndWeaken },
		{ TEXT("target_carries_void_splinter"),
												ECataclysmStatCondition::TargetCarriesVoidSplinter },
		{ TEXT("can_cripple_or_weaken"),        ECataclysmStatCondition::CanCrippleOrWeaken },
		{ TEXT("opponent_carries_weaken"),      ECataclysmStatCondition::OpponentCarriesWeaken },
		{ TEXT("target_health_below"),          ECataclysmStatCondition::TargetHealthBelowPercent },
		{ TEXT("energy_shield_at_maximum"),     ECataclysmStatCondition::EnergyShieldAtMaximum },
		{ TEXT("enemies_hit_at_least"),         ECataclysmStatCondition::EnemiesStruckTogetherAtLeast },
		{ TEXT("opponent_within_metres"),       ECataclysmStatCondition::OpponentWithinMetres },
		{ TEXT("moved_within_seconds"),         ECataclysmStatCondition::MovedWithinSeconds },
		{ TEXT("class_resource_above"),         ECataclysmStatCondition::ClassResourceAbovePercent },
		{ TEXT("energy_shield_above_zero"),     ECataclysmStatCondition::EnergyShieldAboveZero },
	};

	struct FNamedStatScale
	{
		const TCHAR* Name;
		ECataclysmStatScale Scale;
	};

	const FNamedStatScale NamedStatScales[] = {
		{ TEXT("health_missing"),      ECataclysmStatScale::PerPercentOfMaximumHealthMissing },
		{ TEXT("class_resource_held"), ECataclysmStatScale::PerPointOfClassResourceHeld },
		{ TEXT("health_owed"),         ECataclysmStatScale::PerPercentOfMaximumHealthOwed },
		{ TEXT("life_leech"),          ECataclysmStatScale::PerPercentOfLifeLeech },
		{ TEXT("momentum_stacks"),     ECataclysmStatScale::PerStackOfSanguineMomentum },
		{ TEXT("bloodlust_stacks"),    ECataclysmStatScale::PerStackOfBloodlust },
		{ TEXT("carnage_stacks"),      ECataclysmStatScale::PerStackOfCarnage },
		{ TEXT("debuffs_carried"),     ECataclysmStatScale::PerDebuffCarried },
		{ TEXT("minions_held"),        ECataclysmStatScale::PerMinionHeld },
		{ TEXT("enemies_in_reach"),    ECataclysmStatScale::PerEnemyInReach },
		{ TEXT("crippled_enemies_in_reach"), ECataclysmStatScale::PerCrippledEnemyInReach },
		{ TEXT("enemies_hit_beyond_the_first"), ECataclysmStatScale::PerEnemyStruckTogetherBeyondTheFirst },
		{ TEXT("damage_reduction"),    ECataclysmStatScale::PerPercentOfDamageReduction },
		{ TEXT("max_mana"),            ECataclysmStatScale::PerPointOfMaximumMana },
		{ TEXT("metres_to_target"),    ECataclysmStatScale::PerMetreToTarget },
		{ TEXT("seconds_stationary"),  ECataclysmStatScale::PerSecondStationary },
	};

	/**
	 * How many hostile characters stand within this many metres.
	 *
	 * ONE COPY, READ BY BOTH THE CONDITION AND THE SCALE. They ask the same
	 * question of the same list and a second loop would be a second answer.
	 *
	 * A NEGATIVE REACH COUNTS NOTHING, which is what a row that is not about
	 * anything nearby carries, and what a row whose radius was never authored
	 * carries too. Both should grant nothing rather than count everybody.
	 */
	int32 CountWithin(const TArray<float>& Distances, float ReachMetres)
	{
		if (ReachMetres < 0.0f)
		{
			return 0;
		}

		int32 Counted = 0;
		for (const float Metres : Distances)
		{
			if (Metres <= ReachMetres)
			{
				++Counted;
			}
		}
		return Counted;
	}

	int32 EnemiesInReach(const FCataclysmStatConditions& State, float ReachMetres)
	{
		return CountWithin(State.HostileDistancesMetres, ReachMetres);
	}

	/**
	 * How many CRIPPLED hostile characters stand within this many metres.
	 * Issue #1515.
	 *
	 * THE SAME LOOP AS `EnemiesInReach`, ON THE OTHER LIST. Both call
	 * `CountWithin`, so "within this many metres" has one definition however
	 * many lists there are -- the rule `EnemiesInReach`'s comment states for
	 * the condition and the scale, carried one step further.
	 */
	int32 CrippledEnemiesInReach(const FCataclysmStatConditions& State,
								 float ReachMetres)
	{
		return CountWithin(State.CrippledHostileDistancesMetres, ReachMetres);
	}
}

bool UCataclysmStatPipeline::ConditionNamed(const FString& Name,
											ECataclysmStatCondition& OutCondition)
{
	// IGNORING CASE, as the passive tree's own reading does, so the two answer
	// alike for a sheet cell typed with a capital.
	for (const FNamedStatCondition& Each : NamedStatConditions)
	{
		if (Name.Equals(Each.Name, ESearchCase::IgnoreCase))
		{
			OutCondition = Each.Condition;
			return true;
		}
	}
	return false;
}

void UCataclysmStatPipeline::AllConditionNames(TArray<FString>& OutNames)
{
	OutNames.Reset();
	OutNames.Reserve(UE_ARRAY_COUNT(NamedStatConditions));
	for (const FNamedStatCondition& Each : NamedStatConditions)
	{
		OutNames.Add(Each.Name);
	}
}

bool UCataclysmStatPipeline::ConditionTakesAValue(
	ECataclysmStatCondition Condition)
{
	switch (Condition)
	{
	case ECataclysmStatCondition::WhileBleeding:
	case ECataclysmStatCondition::ClassResourceAtMaximum:
	case ECataclysmStatCondition::EnergyShieldAtMaximum:
	case ECataclysmStatCondition::HitIsMeleeAttack:
	case ECataclysmStatCondition::HitIsRangedAttack:
	case ECataclysmStatCondition::HitIsSpell:
	case ECataclysmStatCondition::OpponentIsBoss:
	case ECataclysmStatCondition::OpponentIsStaggered:
	case ECataclysmStatCondition::TargetIsStaggered:
	case ECataclysmStatCondition::TargetIsBoss:
	case ECataclysmStatCondition::TargetIsNotBoss:
	case ECataclysmStatCondition::WhileMoving:
	case ECataclysmStatCondition::WhileStationary:
	case ECataclysmStatCondition::TargetCarriesCripple:
	case ECataclysmStatCondition::TargetCarriesCrippleAndWeaken:
	case ECataclysmStatCondition::TargetCarriesVoidSplinter:
	case ECataclysmStatCondition::CanCrippleOrWeaken:
	case ECataclysmStatCondition::OpponentCarriesWeaken:
	case ECataclysmStatCondition::EnergyShieldAboveZero:
		// NAMES A STATE OR A KIND OF BLOW RATHER THAN A THRESHOLD, so there is
		// nothing for a number to be compared against. Each of the nineteen says
		// so in its own comment in the header, and
		// `tools/tests/test_the_condition_count_sentences_agree_with_the_code.py`
		// holds this count and the header's to the case labels (issue #1640).
		//
		// THE COUNT IN THAT SENTENCE SAID "TEN" FOR THREE NAMES. It is written
		// out because a list nobody counts is a list somebody extends without
		// reading, and `test_condition_lists_agree_with_the_code.py` holds this
		// list against two others rather than against this number.
		//
		// IT FAILED ON ITS OWN AUTHOR, IN THE COMMIT THAT WROTE IT. Issue #1750
		// added the three ailment conditions above, corrected THIS count from
		// ten to thirteen, wrote the sentence about a list nobody counts, added
		// 131 lines to the header -- and left the header's own count reading
		// "TEN OF THE TWENTY-ONE", which is the sentence the line above points
		// at. Writing the rule down and breaking it were the same commit.
		//
		// AND THE PARAGRAPH BELOW WENT STALE THE SAME WAY, having said "the
		// last two" while those two were last. Two records of drift, in one
		// comment, about itself.
		//
		// THE SUCCESS IS THE CHANGE THAT ADDED `EnergyShieldAtMaximum`. Issue
		// #1515. Somebody reading this switch for an unrelated reason saw the
		// number, counted the case labels, got thirteen, and noticed their own
		// change made it fourteen -- before publishing. That is the job this
		// sentence was written for, and it is the first time it has done it.
		//
		// SO THE HONEST ACCOUNT IS ONE FAILURE AND ONE SUCCESS, AND NEITHER HALF
		// ALONE IS FAIR. Quoting only the failure argues for deleting a defence
		// that has since worked; quoting only the success hides that it failed
		// immediately, on the person who wrote it.
		//
		// SO DO NOT DELETE THE NUMBER ON THE STRENGTH OF THE FAILURE ALONE.
		// A defence is easiest to remove at the moment its record looks worst,
		// which is usually just before anybody has tried using it as intended.
		// Membership is enforced elsewhere --
		// `test_condition_lists_agree_with_the_code.py` holds this list against
		// two others -- so the number protects nothing a test covers, and is
		// kept for the reader rather than for the suite.
		//
		// `WhileMoving` AND `WhileStationary` ARE ISSUE #41'S SLICE 2: whether the
		// character moved in the last sample, and whether it did not. Its other
		// three movement conditions DO compare a number -- a wait in seconds or a
		// distance in metres -- so they belong under the default below and not
		// here. That sentence said "the last two" while they were last, and the
		// three ailment conditions below have since been added after them.
		//
		// THE THREE AILMENT CONDITIONS NAME THEIR AILMENTS, so there is nothing
		// for a number to say. Issue #1515. A condition asking "carries at least
		// N debuffs" would compare one, and is a different question nothing has
		// asked for.
		return false;

	case ECataclysmStatCondition::Always:
		// NO CONDITION AT ALL, SO NO VALUE EITHER. A modifier that applies
		// always compares nothing by definition, and saying so here keeps a
		// caller from copying a stray number onto an unconditional modifier.
		return false;

	default:
		// EVERY THRESHOLD AND EVERY TIMED WINDOW. The default is this way round
		// deliberately: a condition added to the enumerator and forgotten here
		// keeps its value rather than silently losing it, and a value on a
		// predicate that ignores it is the harmless direction of the two.
		return true;
	}
}

bool UCataclysmStatPipeline::ScaleNamed(const FString& Name,
										ECataclysmStatScale& OutScale)
{
	for (const FNamedStatScale& Each : NamedStatScales)
	{
		if (Name.Equals(Each.Name, ESearchCase::IgnoreCase))
		{
			OutScale = Each.Scale;
			return true;
		}
	}
	return false;
}

bool UCataclysmStatPipeline::ConditionHolds(ECataclysmStatCondition Condition,
										   float Value,
										   const FCataclysmStatConditions& State,
										   float ReachMetres)
{
	switch (Condition)
	{
	case ECataclysmStatCondition::Always:
		return true;

	case ECataclysmStatCondition::HealthAtOrBelowPercent:
		// AN UNKNOWN STATE REFUSES. A caller with no character in hand -- the
		// character sheet, or a test passing plain numbers -- must not be handed
		// a bonus that depends on where the character's health is. Issue #959.
		//
		// AT OR BELOW, NOT BELOW. The seven nodes that take this predicate are
		// all written "While at or below 20% health", so a character sitting
		// exactly on the number gets the bonus.
		//
		// THIS COMMENT USED TO SAY EVERY NODE STATING A HEALTH THRESHOLD WAS
		// WRITTEN THAT WAY, and issue #1051 is where that stopped being true.
		// The Final Vow's first option says "While below 20% health" and takes
		// the predicate below instead. A comment claiming a shape is universal
		// is read as a reason not to look for the exception.
		return State.HealthPercent >= 0.0f && State.HealthPercent <= Value;

	case ECataclysmStatCondition::HealthBelowPercent:
		// STRICTLY BELOW, WHICH IS THE WHOLE DIFFERENCE FROM THE PREDICATE
		// ABOVE. Issue #1051. A character on exactly 20% health gets nothing
		// from The Last Drop and does get the bonus from every "at or below 20%"
		// node, which is what the two sentences say.
		//
		// AN UNKNOWN STATE REFUSES, the same as above and for the same reason.
		// Note that this cannot be folded into the comparison the way "at or
		// below" nearly could: an unknown health reads -1, which IS strictly
		// below every threshold, so dropping the first half here would hand the
		// bonus to the character sheet and to every caller with no character.
		return State.HealthPercent >= 0.0f && State.HealthPercent < Value;

	case ECataclysmStatCondition::HealthAbovePercent:
		// STRICTLY ABOVE, AND THE ONLY HEALTH PREDICATE THAT POINTS UPWARDS.
		// Issue #1070. Ceaseless Penance reads "while you are above 50% health",
		// so a character sitting exactly on half health is not above it and its
		// debuffs expire normally.
		//
		// AN UNKNOWN STATE REFUSES, and the guard is written out rather than
		// left to the comparison. An unknown health reads -1, which is not above
		// any threshold, so the second half alone would already answer no -- by
		// accident. The character sheet has to be refused on purpose, the same
		// way the two predicates above refuse it.
		return State.HealthPercent >= 0.0f && State.HealthPercent > Value;

	case ECataclysmStatCondition::HealthAtOrAbovePercent:
		// AT OR ABOVE, WHICH IS THE WHOLE DIFFERENCE FROM THE PREDICATE ABOVE.
		// Issues #1653 and #41. "Your ultimate ability cannot be used unless you
		// are below 50% HP" locks the skill at exactly half health, where
		// `HealthAbovePercent` would release it.
		//
		// AN UNKNOWN STATE REFUSES, the same as the three above. An unknown
		// health reads -1, which is not at or above any threshold the validator
		// allows, so the comparison alone would already answer no -- by accident,
		// exactly as `HealthAbovePercent` says of itself, and only while the
		// 0-to-100 bound holds.
		//
		// MEASURED, BECAUSE THE FIRST VERSION OF THIS COMMENT CLAIMED THE
		// OPPOSITE. With a reading of -1 the comparison alone answers yes for
		// `HealthAtOrBelowPercent` and `HealthBelowPercent` at every allowed
		// threshold, and no for both upward predicates. So the guard changes an
		// answer for the two that point down and for neither that points up. It
		// is written out in all four so that none depends on a bound enforced in
		// another function.
		return State.HealthPercent >= 0.0f && State.HealthPercent >= Value;

	case ECataclysmStatCondition::WithinSecondsOfHealthCost:
		// A NEGATIVE READING IS "NEVER PAID ONE, OR NOT KNOWN", and both answer
		// no, so they do not have to be told apart. Issue #962.
		//
		// THE WINDOW INCLUDES ITS LAST INSTANT, matching "at or below" above. A
		// node saying "for 2 seconds after" covers the moment exactly two
		// seconds later, which no player can time and which keeps the two
		// predicates from disagreeing about their own boundaries.
		return State.SecondsSinceHealthCost >= 0.0f
			&& State.SecondsSinceHealthCost <= Value;

	case ECataclysmStatCondition::WithinSecondsOfForeignDamage:
		// THE SAME TWO RULES AS THE WINDOW ABOVE: a negative reading means
		// never or not known, and the window includes its last instant.
		// Issue #975.
		return State.SecondsSinceForeignDamage >= 0.0f
			&& State.SecondsSinceForeignDamage <= Value;

	case ECataclysmStatCondition::WithinSecondsOfChargeSkill:
		// THE SAME TWO RULES AS THE TWO WINDOWS ABOVE: a negative reading means
		// never or not known, and the window includes its last instant.
		// Issue #1826.
		return State.SecondsSinceChargeSkill >= 0.0f
			&& State.SecondsSinceChargeSkill <= Value;

	case ECataclysmStatCondition::WithinSecondsOfBasicAttack:
		// The same two rules again. Issue #1826.
		return State.SecondsSinceBasicAttack >= 0.0f
			&& State.SecondsSinceBasicAttack <= Value;

	case ECataclysmStatCondition::WithinSecondsOfBlock:
		// The same two rules again. Issue #1826.
		return State.SecondsSinceBlock >= 0.0f
			&& State.SecondsSinceBlock <= Value;

	case ECataclysmStatCondition::WithinSecondsOfSummon:
		// The same two rules again. Issue #1815.
		return State.SecondsSinceSummon >= 0.0f
			&& State.SecondsSinceSummon <= Value;

	case ECataclysmStatCondition::WithinSecondsOfEvade:
		// The same two rules again. Issue #1815.
		return State.SecondsSinceEvade >= 0.0f
			&& State.SecondsSinceEvade <= Value;

	case ECataclysmStatCondition::WithinSecondsOfHitTaken:
		// The same two rules again. Issue #1815.
		return State.SecondsSinceHitTaken >= 0.0f
			&& State.SecondsSinceHitTaken <= Value;

	case ECataclysmStatCondition::WithinSecondsOfClassResourceFull:
		// The same two rules again. Issue #1815.
		return State.SecondsSinceClassResourceFull >= 0.0f
			&& State.SecondsSinceClassResourceFull <= Value;

	case ECataclysmStatCondition::WithinSecondsOfClassResourceEmpty:
		// The same two rules again. Issue #1815.
		return State.SecondsSinceClassResourceEmpty >= 0.0f
			&& State.SecondsSinceClassResourceEmpty <= Value;

	case ECataclysmStatCondition::SkillHealthCostAbovePercent:
		// STRICTLY ABOVE, WHICH IS THE OPPOSITE BOUNDARY FROM EVERY OTHER
		// PREDICATE HERE, and it is read straight off the design's own words:
		// "A skill whose health cost is above 10% of your maximum health".
		// Issue #983.
		//
		// THE BOUNDARY IS REACHABLE RATHER THAN THEORETICAL. Deeper Cuts at its
		// full ten points adds exactly 10% of maximum health to every skill, so
		// a character with that node and no skill of its own cost sits precisely
		// on the number. Copying the "at or below" form from the neighbouring
		// case would hand that character the bonus the design withholds.
		//
		// A NEGATIVE READING IS "NO SKILL IN HAND" AND REFUSES, the same as
		// every other unknown here. It cannot be confused with a real answer:
		// a cost is never negative, and a threshold is never below zero.
		return State.SkillHealthCostPercent >= 0.0f
			&& State.SkillHealthCostPercent > Value;

	case ECataclysmStatCondition::WhileBleeding:
		// NO THRESHOLD, SO `Value` IS NOT READ. Issue #962. The other four
		// predicates compare a reading against a number; this one asks whether
		// the character is carrying a kind of effect, and the kind is the
		// enumerator. `tools/generate_datatables.py` refuses to write a value on
		// a row carrying this, so ignoring it here cannot hide one.
		//
		// NOTHING TO REFUSE AS UNKNOWN, WHICH IS WHY THERE IS NO NEGATIVE GUARD.
		// A caller with no character in hand is not bleeding and neither is an
		// unhurt character, and a bonus that applies while bleeding is correctly
		// withheld from both. The readings above need the distinction because a
		// health percentage of zero is a corpse and an unknown one is the
		// character sheet; there is no such pair here.
		return State.bIsBleeding;

	case ECataclysmStatCondition::ClassResourceAtMaximum:
		// NO THRESHOLD, SO `Value` IS NOT READ, the same as the predicate above.
		// Issue #1026. "While your Fervour is at maximum" names the top of the
		// bar rather than a number, and the tool refuses a value on a row
		// carrying this.
		//
		// BOTH READINGS HAVE TO BE KNOWN, AND THAT IS WHAT REFUSES AN ENEMY. An
		// ability system with no class resource attribute set leaves both
		// negative, and every pool asking whether it is full has to tell "there
		// is no bar" apart from "the bar is empty": an unknown pair would
		// otherwise compare -1 against -1 and answer yes, handing every enemy in
		// the game a bonus written for a full Masochist.
		//
		// THE POOLS THAT ASK IT ARE THIS ONE, `ClassResourceAbovePercent` AND
		// `EnergyShieldAtMaximum` BELOW. `EnergyShieldAboveZero` deliberately
		// does NOT: its threshold is a fixed zero rather than one a sheet
		// writes, so an unknown reading of -1 refuses by the comparison itself
		// and a separate clause would be dead code. Its own comment says so.
		// A third belongs on that list rather than in a fresh claim about which
		// place is the only one. This sentence read "this is the one place"
		// while that was true, and adding the second pool is what made it false
		// -- with nothing able to notice, because it is prose no test reads and
		// a reviewer comparing the two conditions sees two matching comments
		// rather than one gone stale.
		//
		// A MAXIMUM OF NOTHING REFUSES TOO. A pool that cannot hold anything is
		// not at its maximum in any sense a node means, and answering yes would
		// give the bonus to a character that has never generated a point.
		return State.ClassResourceHeld >= 0.0f
			&& State.ClassResourceMaximum > 0.0f
			&& State.ClassResourceHeld >= State.ClassResourceMaximum;

	case ECataclysmStatCondition::EnergyShieldAtMaximum:
		// NO THRESHOLD, SO `Value` IS NOT READ, the same as the predicate above.
		// Issue #1515. Cold Reading is the node: "+2% increased Spell Damage per
		// point while your Energy Shield is full."
		//
		// THE SAME THREE CLAUSES AS THE CLASS RESOURCE ABOVE, IN THE SAME ORDER.
		// The reading has to be known, the bar has to be able to hold something,
		// and then the shield has to be at the top of it.
		//
		// THE FIRST CLAUSE IS REDUNDANT TODAY AND IS KEPT ANYWAY. Say so rather
		// than let a reader assume all three do work. `CurrentConditions` writes
		// both readings together inside one block or writes neither, so a shield
		// reading below zero always comes with a maximum below zero -- and the
		// middle clause refuses that already. Whenever the middle clause passes,
		// the first has passed too. It cannot be broken in a way any test
		// distinguishes, and no guard proof for it exists, because there is no
		// reachable state where it is the clause doing the refusing.
		//
		// IT IS KEPT FOR TWO REASONS. `ClassResourceAtMaximum` above carries the
		// identical redundancy for the identical reason, and a reader comparing
		// the two should find the same shape rather than wonder which is wrong.
		// And the redundancy is a property of the FILL, not of this predicate: a
		// future fill that wrote the maximum from one attribute set and the
		// shield from another would make the two readings disagree about being
		// known, and this clause is what would refuse the half-read state.
		//
		// THE MIDDLE CLAUSE CARRIES MORE WEIGHT HERE THAN IT DOES ABOVE. Only
		// the Ritualist has a `max_energy_shield` line in
		// `game/Data/ClassStats.csv`, so a maximum of zero is the ordinary state
		// of every other class and every enemy rather than a corner case, and
		// each of them holds zero of zero. Without this clause all of them would
		// satisfy a node written for a full shield.
		//
		// GREATER-OR-EQUAL RATHER THAN EQUAL, AND THE DIFFERENCE IS REACHABLE IN
		// ORDINARY PLAY RATHER THAN DEFENSIVE. `UCataclysmVitalAttributeSet`
		// clamps the shield to its maximum whenever the SHIELD changes -- in
		// `PreAttributeChange`, in `PostGameplayEffectExecute`, and where a blow
		// is absorbed -- and never when the MAXIMUM changes. There is no
		// proportional adjustment on the maximum the way some attribute sets
		// carry one. So lowering `MaxEnergyShield` leaves the current shield
		// standing above it: raise the maximum, fill the shield, lower the
		// maximum, and the character holds more shield than its bar now has.
		// Any effect or item that grants maximum energy shield and then ends
		// produces exactly that.
		//
		// SUCH A CHARACTER IS FULL, AND `==` WOULD ANSWER NO. Its shield is at
		// the top of its bar and over it, which is what the node's sentence
		// means by full, so the test that pins this writes the state the way the
		// game reaches it rather than by writing a raw number.
		return State.EnergyShieldHeld >= 0.0f
			&& State.EnergyShieldMaximum > 0.0f
			&& State.EnergyShieldHeld >= State.EnergyShieldMaximum;

	case ECataclysmStatCondition::HitIsMeleeAttack:
		// NO THRESHOLD, SO `Value` IS NOT READ, and nothing to refuse as unknown.
		// Issue #666. A caller with no blow in hand leaves every fact false, and
		// a bonus for being hit in melee is correctly withheld from it, the same
		// argument `WhileBleeding` makes. An attack is a hit that is not a spell.
		return State.Blow.bIsMelee && !State.Blow.bIsSpell;

	case ECataclysmStatCondition::HitIsRangedAttack:
		// THE SAME TWO RULES AS ABOVE. A projectile has already been counted as
		// ranged where the blow was built, so this reads one fact rather than two.
		return State.Blow.bIsRanged && !State.Blow.bIsSpell;

	case ECataclysmStatCondition::HitIsSpell:
		return State.Blow.bIsSpell;

	case ECataclysmStatCondition::OpponentIsBoss:
		return State.Blow.bOpponentIsBoss;

	case ECataclysmStatCondition::OpponentIsStaggered:
		// THE SAME TWO RULES AS THE BLOW PREDICATES ABOVE. Issue #45. A caller
		// with no blow in hand leaves this false, and a drawback that costs the
		// character something only while its attacker is staggered is correctly
		// withheld from a character sheet that has no attacker at all.
		return State.Blow.bOpponentIsStaggered;

	case ECataclysmStatCondition::TargetIsStaggered:
		// THE MIRROR OF THE CASE ABOVE, READING A DIFFERENT FIELD, and that is the
		// whole safeguard. `Blow` is filled only on the defender's damage taken
		// lookup and `bTargetIsStaggered` only on the attacker's own lookups, so a
		// row carrying the wrong one of this pair reads a field nothing filled and
		// grants nothing. It cannot read the staggered state of the character at
		// the other end of the blow and quietly answer with it.
		//
		// FALSE IS ALSO "NO TARGET IN HAND", and both meanings refuse together. A
		// character sheet built with no blow and no target answers false here, so
		// a bonus conditioned on a staggered target is correctly withheld from it.
		return State.bTargetIsStaggered;

	case ECataclysmStatCondition::TargetIsBoss:
		// THE MIRROR OF `OpponentIsBoss`, READING A DIFFERENT FIELD, for exactly
		// the reason the staggered pair above gives. `Blow.bOpponentIsBoss` is
		// filled from `Hit.bFromBoss` on the defender's damage taken lookup and
		// answers "a boss hit me"; this one is filled from the target on the
		// attacker's own lookup and answers "I am hitting a boss". A row carrying
		// the wrong half of the pair reads a field nothing filled and grants
		// nothing. Issue #1815.
		//
		// FALSE IS ALSO "NO TARGET IN HAND" OR "NOTHING ASKED", and all three
		// meanings refuse together, which is the safe direction: a bonus meant
		// for bosses that fails to read grants nothing rather than applying to
		// every character in the game.
		return State.bTargetIsBossKnown && State.bTargetIsBoss;

	case ECataclysmStatCondition::TargetIsNotBoss:
		// NOT THE PLAIN NEGATION OF THE CASE ABOVE, and that is deliberate.
		// Issue #1815. Both halves ask `bTargetIsBossKnown` first, so both
		// refuse when nothing read the target. Written as `!bTargetIsBoss`
		// alone this would answer TRUE for a character sheet with no target in
		// hand, and for every lookup whose rows asked about something else, so
		// "you deal less damage to non-Boss enemies" would apply to bosses,
		// to sheets, and to blows with no target at all.
		//
		// THREE STATES, NOT TWO: a boss, not a boss, and nothing looked at. One
		// flag cannot carry three, which is why there are two.
		return State.bTargetIsBossKnown && !State.bTargetIsBoss;

	case ECataclysmStatCondition::TargetCarriesCripple:
		// THE EXPLICIT TAG AND NOT AN IMPLIED PARENT. Issue #1515.
		// `UCataclysmDebuffs::TagsOnActor` fills this with what was really
		// applied, one entry per effect, and `HasTagExact` compares those.
		// `HasTag` would also answer yes to a child tag nobody applied, the day
		// the Debuffs branch grows one.
		//
		// AN INVALID TAG REFUSES RATHER THAN MATCHING EVERYTHING. The vocabulary
		// is generated from the workbook, so an effect removed from the sheet
		// leaves `CrippleTag` invalid; `HasTagExact` on an invalid tag is false,
		// which withholds the bonus instead of granting it unconditionally.
		//
		// AN EMPTY CONTAINER REFUSES, AND IT HAS THREE MEANINGS. No target in
		// hand, a target carrying nothing, or no row in this lookup asking about
		// an ailment so the walk was skipped. All three mean no bonus.
		return State.TargetDebuffs.HasTagExact(UCataclysmDebuffs::CrippleTag());

	case ECataclysmStatCondition::TargetCarriesCrippleAndWeaken:
		// BOTH, WHICH IS WHY THIS IS ONE CONDITION AND NOT TWO ROWS. Issue
		// #1515. `Accumulate` sums increases, so a row per ailment would pay on
		// either and pay twice on both; `Ravager_basic_c_c1` Nothing Left In Them
		// pays on both and not otherwise, and a modifier carries one condition.
		return State.TargetDebuffs.HasTagExact(UCataclysmDebuffs::CrippleTag())
			&& State.TargetDebuffs.HasTagExact(UCataclysmDebuffs::WeakenTag());

	case ECataclysmStatCondition::TargetCarriesVoidSplinter:
		// THE SAME WALK AS CRIPPLE ABOVE, READING A TAG ON A DIFFERENT BRANCH.
		// Issue #1642. A void splinter is `Keyword.DoT.VoidSplinter` rather than
		// `Status.Debuff.*`, because it deals damage over time; `DebuffRootNames`
		// names `Keyword.DoT` as a root, so `TagsOn` collects it and this reads
		// it exactly as the three conditions above read theirs.
		//
		// `HasTagExact` AND NOT `HasTag`, for the reason given above: the walk
		// stores one entry per effect, and asking inexactly would answer yes to
		// `Keyword.DoT` itself and pay on any damage over time at all. That is
		// not a hypothetical here -- six other ailments hang off the same
		// parent, so the loose form would make this condition mean "bleeding, or
		// poisoned, or burning, or ...".
		return State.TargetDebuffs.HasTagExact(
			UCataclysmDebuffs::VoidSplinterTag());

	case ECataclysmStatCondition::CanCrippleOrWeaken:
		// THIS CHARACTER'S OWN CHANCES, NOT THE TARGET'S STATE. Issue #1718.
		// Spreading Hurt widens attacks that cripple or weaken, and an area of
		// effect shapes an attack before it lands, so the only knowable reading
		// is whether this character's attacks are ones that do.
		//
		// THE READING IS TAKEN IN `CurrentConditions` AND NOT HERE, the same as
		// every other fact about the character. The pipeline is handed facts and
		// judges them; it has no attribute set to ask.
		//
		// FALSE COVERS BOTH "NO CHANCE" AND "NOTHING TO READ", which is right
		// here and would be wrong for a threshold. A character with no combat
		// attribute set cannot apply either ailment any more than one with two
		// zeroes can, so there is no pair to tell apart -- unlike the health
		// readings above, where a percentage of zero is a corpse and an unknown
		// one is the character sheet.
		return State.bCanCrippleOrWeaken;

	case ECataclysmStatCondition::TargetHealthBelowPercent:
		// STRICTLY BELOW, so a target sitting exactly on the threshold is not
		// below it and earns nothing. Issue #1515. That is the whole reason this
		// is one name rather than a strict-and-inclusive pair: both node
		// sentences say "below".
		//
		// NEGATIVE IS "NOT READ" AND REFUSES, which covers no target in hand, a
		// target whose health cannot be read, and no row in this lookup asking.
		// A bonus granted on an unknown would be worth more than its sentence
		// says; the stagger ceiling refuses in the opposite direction for the
		// same reason, because it is a drawback. See the header.
		//
		// THE VALUE IS A PERCENTAGE OF THE TARGET'S OWN MAXIMUM, bounded 0 to
		// 100 by `ValidateModifier` below.
		return State.TargetHealthPercent >= 0.0f
			&& State.TargetHealthPercent < Value;

	case ECataclysmStatCondition::OpponentCarriesWeaken:
		// THE MIRROR OF THE TWO ABOVE, READING A DIFFERENT FIELD, and that is the
		// safeguard `OpponentIsStaggered` and `TargetIsStaggered` rely on. The
		// blow record is filled only on the defender's damage taken lookup and
		// `TargetDebuffs` only on the attacker's own lookups, so a row carrying
		// the wrong condition of this pair reads a field nothing filled and
		// grants nothing rather than reading the ailments on the character at the
		// other end of the blow.
		return State.Blow.OpponentDebuffs.HasTagExact(
			UCataclysmDebuffs::WeakenTag());

	case ECataclysmStatCondition::WhileMoving:
		// NO THRESHOLD, SO `Value` IS NOT READ. A caller with no character leaves
		// this false and is refused, the argument `WhileBleeding` makes.
		return State.bIsMoving;

	case ECataclysmStatCondition::WhileStationary:
		// THE OPPOSITE OF THE ABOVE WITHOUT BEING ITS NEGATION. A caller with no
		// character must be refused by both, so this asks for a character first:
		// a negative clock is no character to read.
		return State.SecondsSinceMoved >= 0.0f && !State.bIsMoving;

	case ECataclysmStatCondition::StationaryForSeconds:
		// AT LEAST THAT LONG, so three seconds of standing still meets a threshold
		// of three. A negative clock is no character to read and refuses.
		return State.SecondsSinceMoved >= 0.0f && State.SecondsSinceMoved >= Value;

	case ECataclysmStatCondition::MetresMovedBeforeAttack:
		// AT LEAST THAT FAR, measured before the blow in hand. Negative means no
		// blow in hand, the way the skill's cost does, and refuses.
		return State.MetresMovedBeforeBlow >= 0.0f
			&& State.MetresMovedBeforeBlow >= Value;

	case ECataclysmStatCondition::NotAttackedForSeconds:
		// AT LEAST THAT LONG SINCE THE CHARACTER'S OWN ATTACK. Negative is no
		// character to read and refuses.
		return State.SecondsSinceOwnAttack >= 0.0f
			&& State.SecondsSinceOwnAttack >= Value;
	case ECataclysmStatCondition::OpponentBeyondMetres:
		// STRICTLY MORE THAN, BECAUSE THE NODE WRITES "more than". Standing
		// Apart reads "You take 25% less damage from enemies more than 6 metres
		// away from you", so a character standing at exactly 6 metres takes full
		// damage. The same boundary `SkillHealthCostAbovePercent` draws above.
		//
		// A NEGATIVE READING IS "NOT KNOWN" AND REFUSES, and unlike the health
		// readings this one really can be zero: two characters can stand on the
		// same spot, so zero is a real distance and cannot also mean "no
		// answer". A damage over time tick reports -1 deliberately, which is why
		// this grants nothing for a tick; `docs/DECISIONS.md` carries that
		// judgement.
		//
		// THE GUARD CANNOT BE FOLDED INTO THE COMPARISON, for the reason
		// `HealthAbovePercent` gives: -1 is below every threshold a sheet may
		// write, so it would refuse by accident rather than on purpose, and a
		// threshold of -2 would then pass. Saying it outright is what keeps this
		// correct whatever the sheet holds.
		return State.Blow.OpponentDistanceMetres >= 0.0f
			&& State.Blow.OpponentDistanceMetres > Value;

	case ECataclysmStatCondition::TargetWithinMetres:
		// AT OR WITHIN, BECAUSE BOTH ROWS WRITE "within 5 meters". A target
		// standing at exactly 5 metres IS within 5 metres and earns the bonus.
		// That is the OPPOSITE boundary from `OpponentBeyondMetres` directly
		// above, whose node writes "more than", and the two are next to each
		// other on purpose so the difference is read rather than assumed.
		//
		// A DIFFERENT FIELD FROM THE ONE ABOVE, AND THAT IS THE WHOLE SAFEGUARD.
		// The blow context is filled only on the defender's damage taken lookup;
		// `TargetDistanceMetres` is filled only on the attacker's own lookups. So
		// a row that used the wrong one of these two conditions reads -1 and
		// grants nothing, rather than reading a plausible number from the wrong
		// end of the blow.
		//
		// A NEGATIVE READING IS "NOT KNOWN" AND REFUSES, and zero is a real
		// distance here for the same reason it is above: two characters can stand
		// on one spot. So the guard cannot be folded into the comparison -- and
		// folding it would be worse here than above, because -1 is at or within
		// every threshold a sheet may write, so every row would hold on every
		// blow that knew nothing.
		//
		// A MINION'S BLOW REPORTS -1 DELIBERATELY, so this grants nothing for
		// one. `docs/DECISIONS.md` carries that with the genre sources behind it.
		return State.TargetDistanceMetres >= 0.0f
			&& State.TargetDistanceMetres <= Value;

	case ECataclysmStatCondition::EnemiesInReachAtLeast:
		// AT LEAST `Value` OF THEM, WITHIN THE ROW'S OWN REACH. Issue #1597.
		// "While an enemy is within 4 metres" is this with a value of one;
		// "while three or more enemies are within 4 metres" is the same
		// condition with three. One name serves both because the first is a
		// special case of the second.
		//
		// A VALUE BELOW ONE REFUSES RATHER THAN HOLDING ALWAYS. "At least
		// nought enemies" is true of an empty room, so a row whose count was
		// never authored would grant its bonus everywhere. Refusing makes
		// that visible as a row granting nothing instead of invisible as a
		// row granting everything. The generator refuses it on import; this
		// is what happens if one reaches the game anyway.
		return Value >= 1.0f
			&& static_cast<float>(EnemiesInReach(State, ReachMetres)) >= Value;

	case ECataclysmStatCondition::EnemiesStruckTogetherAtLeast:
		// AT LEAST `Value` ENEMIES STRUCK BY THE ATTACK IN HAND. Issue #1515.
		// Sundering's "three or more enemies at once" is this with three.
		//
		// A NEGATIVE COUNT IS NO ATTACK IN HAND AND REFUSES, and a value below
		// one refuses for the reason the reach condition above gives: "at
		// least nought enemies" would hold for every blow in the game.
		return Value >= 1.0f && State.EnemiesStruckTogether >= 0
			&& static_cast<float>(State.EnemiesStruckTogether) >= Value;

	case ECataclysmStatCondition::OpponentWithinMetres:
		// AT OR WITHIN, BECAUSE "WITHIN" IS INCLUSIVE. Issue #1981. "Nearby
		// enemies deal 10%-30% less damage to you" is the row, at 5 metres --
		// the one figure every near row in the game already uses, measured
		// rather than chosen. `TargetWithinMetres` draws the same boundary from
		// the same word, and `OpponentBeyondMetres` directly below reads THIS
		// SAME FIELD with the opposite one, because its node writes "more than".
		//
		// THE BLOW'S DISTANCE AND NOT THE ATTACKER-SIDE ONE. The blow context is
		// filled only on the defender's damage taken lookup, which is where a
		// row about enemies hitting you is asked, and `TargetDistanceMetres`
		// only on the attacker's own. A row using the wrong one reads -1 and
		// grants nothing rather than a plausible number from the wrong end.
		//
		// A NEGATIVE READING IS "NOT KNOWN" AND REFUSES, and zero is a real
		// distance because two characters can stand on one spot. The guard
		// cannot be folded into the comparison: -1 is at or within every
		// threshold a sheet may write, so folding it would let every blow that
		// knew nothing satisfy the row -- the trap `TargetWithinMetres` spells
		// out, and it is worse on this side of the pair than on the other.
		return State.Blow.OpponentDistanceMetres >= 0.0f
			&& State.Blow.OpponentDistanceMetres <= Value;

	case ECataclysmStatCondition::MovedWithinSeconds:
		// AT OR WITHIN THE LAST `Value` SECONDS. Issue #1981. "Strike skills
		// deal 25%-40% less damage if you have moved in the last 2 seconds" is
		// the row.
		//
		// THE SAME FIELD `StationaryForSeconds` READS, COMPARED THE OTHER WAY,
		// and at exactly the threshold BOTH HOLD. A character whose last
		// movement was two seconds ago has been stationary for two seconds and
		// has moved within the last two; both readings of the English are right
		// and the overlap is one instant wide. Said here rather than left for
		// somebody to find in play.
		//
		// A NEGATIVE READING MEANS NO MOVEMENT SAMPLE HAS LOOKED AT THIS
		// CHARACTER AND REFUSES. It has not moved recently in any sense a row
		// means. The guard is separate for the reason the distance above gives:
		// -1 is at or within every threshold a sheet may write.
		return State.SecondsSinceMoved >= 0.0f
			&& State.SecondsSinceMoved <= Value;

	case ECataclysmStatCondition::ClassResourceAbovePercent:
		// STRICTLY ABOVE, the boundary `HealthAbovePercent` draws for the same
		// word. Issue #1981. "When your class resource is above 75%, all skills
		// cost 20%-40% less mana" is the row.
		//
		// THE SAME THREE CLAUSES AS `ClassResourceAtMaximum` ABOVE, IN THE SAME
		// ORDER AND FOR ITS REASONS. The reading has to be known, the bar has to
		// be able to hold something, and only then is the share worth working
		// out. An unknown pair would compare -1 against -1 and hand every enemy
		// a bonus written for a Masochist; a maximum of nothing would divide by
		// it.
		//
		// A SHARE AND NOT A COUNT OF POINTS, unlike the scale that reads the
		// same pool. Classes do not share a maximum, so "above 75%" is the only
		// way to mean the same thing for each of them.
		return State.ClassResourceHeld >= 0.0f
			&& State.ClassResourceMaximum > 0.0f
			&& (State.ClassResourceHeld / State.ClassResourceMaximum) * 100.0f
				   > Value;

	case ECataclysmStatCondition::EnergyShieldAboveZero:
		// NO THRESHOLD, SO `Value` IS NOT READ. Issue #1981. "You take 10%-20%
		// increased damage from all sources while your shield is active" is the
		// row, and "active" was read as held above zero -- a judgement under the
		// project owner's delegation, recorded in `docs/DECISIONS.md`. A shield
		// at nothing absorbs nothing.
		//
		// THE ONLY READING HERE THAT FOLDS ITS GUARD INTO THE COMPARISON, and it
		// is safe only because the threshold is a fixed zero rather than one a
		// sheet writes: an unknown reading of -1 is not above zero, so it
		// refuses on purpose rather than by luck. Every neighbour keeps its
		// guard separate because a sheet could write a negative threshold that
		// -1 would satisfy. Which of those two a condition is in is the thing
		// worth stating, and the pair above shows both.
		return State.EnergyShieldHeld > 0.0f;
	}

	// A CONDITION THIS BUILD DOES NOT KNOW REFUSES rather than applying. A saved
	// or imported modifier naming one is a modifier this build cannot judge, and
	// granting it would be granting something unread.
	return false;
}

namespace
{
	/**
	 * What a modifier is worth at this many stacks. Issues #1002 to #1004.
	 *
	 * SHARED BY ALL THREE STACK SCALES, because a stack count needs none of the
	 * arithmetic the other scales do. There is no reading to divide by a step
	 * and nothing to round: the count is already whole.
	 *
	 * THE STEP IS STILL HONOURED, so a bonus written "per 2 stacks" would work
	 * if the design ever asked for one. All three of today's nodes say "each
	 * stack", which is a step of one, and the data check refuses any other step
	 * for a sentence that names none.
	 *
	 * A STEP OF NOTHING IS WORTH NOTHING rather than dividing by zero, the same
	 * rule the three scales above follow.
	 */
	float StackedValue(const FCataclysmStatModifier& Modifier, int32 Stacks)
	{
		if (Stacks <= 0 || Modifier.ScaleStep <= 0.0f)
		{
			return 0.0f;
		}

		const float Steps = FMath::FloorToFloat(
			static_cast<float>(Stacks) / Modifier.ScaleStep);
		return Modifier.Value * FMath::Max(0.0f, Steps);
	}
}

float UCataclysmStatPipeline::ScaledValue(const FCataclysmStatModifier& Modifier,
										  const FCataclysmStatConditions& State)
{
	switch (Modifier.Scale)
	{
	case ECataclysmStatScale::Fixed:
		return Modifier.Value;

	case ECataclysmStatScale::PerPercentOfMaximumHealthMissing:
	{
		// AN UNKNOWN HEALTH READING SCALES TO NOTHING, for the reason it refuses
		// a condition: the character sheet has no character in hand, and a bonus
		// whose size depends on where health is must not be written onto a
		// gameplay attribute. Issue #968.
		//
		// AND A STEP OF NOTHING SCALES TO NOTHING, rather than dividing by zero.
		// `ValidateModifier` refuses it when data is imported; this is what
		// happens if one reaches the game anyway.
		if (State.HealthPercent < 0.0f || Modifier.ScaleStep <= 0.0f)
		{
			return 0.0f;
		}

		// WHOLE STEPS, ROUNDED DOWN. "For every 5% of your maximum health that
		// is missing" is a count of completed blocks: 12% down is two steps, not
		// two and two fifths. See the enumerator for the words and the genre
		// precedent this is read from.
		const float Missing = 100.0f - State.HealthPercent;
		const float Steps = FMath::FloorToFloat(Missing / Modifier.ScaleStep);
		return Modifier.Value * FMath::Max(0.0f, Steps);
	}

	case ECataclysmStatScale::PerPointOfClassResourceHeld:
	{
		// THE SAME TWO REFUSALS AS THE READING ABOVE, and for the same reasons.
		// Issue #980. An ability system with no class resource attribute set --
		// every enemy in the game -- leaves the reading negative, and a step of
		// nothing would divide by zero.
		//
		// A HELD AMOUNT OF ZERO IS NOT A REFUSAL. It falls through and answers
		// zero by the arithmetic, which is the honest answer for an empty bar
		// rather than a special case.
		if (State.ClassResourceHeld < 0.0f || Modifier.ScaleStep <= 0.0f)
		{
			return 0.0f;
		}

		// WHOLE STEPS, ROUNDED DOWN, the same rule as the reading above. The
		// design's own node uses a step of 1, where rounding cannot show, so the
		// rule is read off the other scale rather than off this node's words.
		const float Steps =
			FMath::FloorToFloat(State.ClassResourceHeld / Modifier.ScaleStep);
		return Modifier.Value * FMath::Max(0.0f, Steps);
	}

	case ECataclysmStatScale::PerPercentOfMaximumHealthOwed:
	{
		// THE SAME TWO REFUSALS AGAIN. Issue #994. The reading is negative for a
		// caller with no character in hand, for an ability system with no class
		// resource attribute set, and for one whose maximum health is nothing;
		// all three are cases where the share cannot be worked out at all rather
		// than cases where it is zero.
		//
		// OWING NOTHING IS NOT A REFUSAL. A character at full health that has
		// deferred no cost reads zero and gets nothing by the arithmetic, which
		// is the honest answer and is what makes the node a reward for being in
		// debt rather than a flat bonus.
		if (State.HealthOwedPercent < 0.0f || Modifier.ScaleStep <= 0.0f)
		{
			return 0.0f;
		}

		// WHOLE STEPS, ROUNDED DOWN, the rule the other two follow. Compound
		// Interest reads "for every 5% of your maximum health you currently
		// owe", so owing 12% is two completed blocks rather than two and two
		// fifths.
		const float Steps =
			FMath::FloorToFloat(State.HealthOwedPercent / Modifier.ScaleStep);
		return Modifier.Value * FMath::Max(0.0f, Steps);
	}

	case ECataclysmStatScale::PerPercentOfLifeLeech:
	{
		// THE SAME TWO REFUSALS AS THE READINGS ABOVE. Issue #1045. The reading
		// is negative for a caller with no character in hand and for an ability
		// system with no vital attribute set, which is where life leech lives;
		// and a step of nothing would divide by zero.
		//
		// HAVING NONE IS NOT A REFUSAL. A character with no life leech reads
		// zero and gets nothing by the arithmetic, which is the honest answer
		// and is what makes Glutton a reward for investing in leech rather than
		// a flat bonus.
		if (State.LifeLeechPercent < 0.0f || Modifier.ScaleStep <= 0.0f)
		{
			return 0.0f;
		}

		// WHOLE STEPS, ROUNDED DOWN, the rule every scale here follows. Glutton
		// uses a step of 1, where rounding cannot show, so the rule is read off
		// the other scales rather than off this node's words.
		const float Steps =
			FMath::FloorToFloat(State.LifeLeechPercent / Modifier.ScaleStep);
		return Modifier.Value * FMath::Max(0.0f, Steps);
	}

	case ECataclysmStatScale::PerPercentOfDamageReduction:
	{
		// THE SAME TWO REFUSALS AS THE READINGS ABOVE. Issue #1515. The reading
		// is negative for a caller with no character in hand and for an ability
		// system with no combat attribute set, and a step of nothing would divide
		// by zero.
		//
		// THE CAP IS NOT APPLIED HERE. It is applied where the reading is taken,
		// in `UCataclysmAbilitySystemComponent::CurrentConditions`, by the
		// function a hit uses, so this arithmetic counts whatever it is handed.
		if (State.DamageReductionPercent < 0.0f || Modifier.ScaleStep <= 0.0f)
		{
			return 0.0f;
		}

		// WHOLE STEPS, ROUNDED DOWN, the rule every scale here follows. Weight
		// Against Them's step is 2, so 9% is four steps, not four and a half.
		const float Steps =
			FMath::FloorToFloat(State.DamageReductionPercent / Modifier.ScaleStep);
		return Modifier.Value * FMath::Max(0.0f, Steps);
	}

	case ECataclysmStatScale::PerPointOfMaximumMana:
	{
		// THE SAME TWO REFUSALS AGAIN. Issue #1515. Negative for a caller with no
		// character in hand and for an ability system with no vital attribute
		// set, which is where maximum mana lives.
		if (State.MaximumMana < 0.0f || Modifier.ScaleStep <= 0.0f)
		{
			return 0.0f;
		}

		// WHOLE STEPS, ROUNDED DOWN. Drawn Deep says "every full 200", so 399
		// maximum mana is one step.
		const float Steps =
			FMath::FloorToFloat(State.MaximumMana / Modifier.ScaleStep);
		return Modifier.Value * FMath::Max(0.0f, Steps);
	}

	case ECataclysmStatScale::PerMetreToTarget:
	{
		// THE SAME TWO REFUSALS AGAIN. Issue #1981. Negative for a lookup with
		// no target in hand -- a damage over time tick, a minion's blow, and
		// every lookup made with no blow at all -- and a step of nothing would
		// divide by it. A bonus for being far away must not be handed to a
		// caller that does not know how far away anything is.
		if (State.TargetDistanceMetres < 0.0f || Modifier.ScaleStep <= 0.0f)
		{
			return 0.0f;
		}

		// WHOLE STEPS, ROUNDED DOWN, as every reading here is. "For each meter
		// of distance to the target" with a step of one makes 4.9 metres four.
		const float Steps =
			FMath::FloorToFloat(State.TargetDistanceMetres / Modifier.ScaleStep);
		return Modifier.Value * FMath::Max(0.0f, Steps);
	}

	case ECataclysmStatScale::PerSecondStationary:
	{
		// THE SAME TWO REFUSALS. Issue #1981. Negative means no movement sample
		// has looked at this character, which is not the same as its having
		// stood still for ever, and a step of nothing would divide by it.
		if (State.SecondsSinceMoved < 0.0f || Modifier.ScaleStep <= 0.0f)
		{
			return 0.0f;
		}

		// WHOLE STEPS, ROUNDED DOWN. "For each second you stand still" with a
		// step of one makes 1.9 seconds one.
		//
		// NOTHING CAPS IT HERE AND NOTHING NEEDS TO. Its row is a MULTIPLYING
		// reduction, and `LessMultiplierFloor` stops one at -99%, so one per
		// cent of the hit survives however long the character stands there. An
		// increased row would have had no such floor. Measured and ruled on
		// 2026-09-18; `docs/DECISIONS.md` carries both halves.
		const float Steps =
			FMath::FloorToFloat(State.SecondsSinceMoved / Modifier.ScaleStep);
		return Modifier.Value * FMath::Max(0.0f, Steps);
	}

	// THE THREE STACK COUNTS SHARE ONE PIECE OF ARITHMETIC, because a stack is
	// already a whole thing: there is no reading to divide and nothing to round.
	// Issues #1002, #1003 and #1004.
	case ECataclysmStatScale::PerStackOfSanguineMomentum:
		return StackedValue(Modifier, State.SanguineMomentumStacks);

	case ECataclysmStatScale::PerStackOfBloodlust:
		return StackedValue(Modifier, State.BloodlustStacks);

	case ECataclysmStatScale::PerStackOfCarnage:
		return StackedValue(Modifier, State.CarnageStacks);

	// AND THE DEBUFFS THE CHARACTER IS UNDER, COUNTED THE SAME WAY. Issue #962.
	// A debuff is a whole thing exactly as a stack is, so the arithmetic is
	// shared even though what is being counted is not a stack: a stack is an
	// event this project chose to remember, and a debuff is a gameplay effect
	// somebody applied that the ability system is already holding.
	case ECataclysmStatScale::PerDebuffCarried:
		return StackedValue(Modifier, State.DebuffsCarried);

	// AND THE MINIONS THE CHARACTER IS COMMANDING, COUNTED THE SAME WAY AGAIN.
	// Issue #1518, the Ritualist's generator: "1 per second for each minion you
	// have". A minion is a whole thing exactly as a debuff and a stack are, so
	// the arithmetic is shared for the third time.
	//
	// A CHARACTER COMMANDING NOTHING GETS NOTHING, by the same arithmetic and
	// with no special case: `StackedValue` multiplies by a count of zero. That
	// is the half of the Ritualist's generator a build is most likely to get
	// wrong, because a row read without its scale would grant its bare value to
	// a Ritualist standing alone.
	case ECataclysmStatScale::PerMinionHeld:
		return StackedValue(Modifier, State.MinionsHeld);

	// PER ENEMY STANDING WITHIN THE ROW'S OWN REACH. Issue #1597, and the
	// third scale that is a count of things after debuffs carried and
	// minions held.
	//
	// A CHARACTER ALONE GETS NOTHING, by the same arithmetic and with no
	// special case: `StackedValue` multiplies by a count of zero. That is
	// the case a build that forgets the count gets wrong, in the player's
	// favour and invisibly.
	//
	// UNCAPPED, RULED BY THE PROJECT OWNER ON 2026-09-12. A Horde wave has
	// been measured at 125 to 174 creatures at once, so this grows without
	// bound in the mode the owner plays. `docs/DECISIONS.md` records that a
	// cap was put to them with a recommendation and declined, and quotes the
	// design's existing rule that multiplicative sources need no cap.
	case ECataclysmStatScale::PerEnemyInReach:
		return StackedValue(Modifier, EnemiesInReach(State, Modifier.ReachMetres));

	// AND PER CRIPPLED ENEMY, THE SAME ARITHMETIC ON THE FILTERED LIST. Issue
	// #1515. A character near nobody crippled gets nothing by the same
	// multiplication by zero, with no special case.
	case ECataclysmStatScale::PerCrippledEnemyInReach:
		return StackedValue(Modifier,
							CrippledEnemiesInReach(State, Modifier.ReachMetres));

	// AND PER ENEMY STRUCK BEYOND THE FIRST. Issue #1515. One enemy struck is
	// worth nothing, and an unknown count of -1 is worth nothing too: both
	// reach `StackedValue` as a count of zero or less, which it answers with
	// zero rather than with the row's value.
	case ECataclysmStatScale::PerEnemyStruckTogetherBeyondTheFirst:
		return StackedValue(Modifier,
							FMath::Max(0, State.EnemiesStruckTogether - 1));
	}

	// A SCALE THIS BUILD DOES NOT KNOW IS WORTH NOTHING rather than its full
	// value. The same argument `ConditionHolds` makes: granting the whole thing
	// would be granting something unread, silently and in the player's favour.
	return 0.0f;
}

bool UCataclysmStatPipeline::ModifierApplies(const FCataclysmStatModifier& Modifier,
											 const FGameplayTagContainer& SkillTags,
											 const FCataclysmStatConditions& State)
{
	// THE CHARACTER'S STATE FIRST, because it is the cheaper question and
	// because a modifier carrying both a condition and a required tag needs both.
	// Issue #959.
	if (!ConditionHolds(Modifier.Condition, Modifier.ConditionValue, State,
						Modifier.ReachMetres))
	{
		return false;
	}

	const FGameplayTag Global = GlobalScopeTag();

	for (const FGameplayTag& Required : Modifier.RequiredTags)
	{
		if (Global.IsValid() && Required == Global)
		{
			// Applies to everything, so it constrains nothing.
			continue;
		}

		// HasTag matches a held tag against the required tag's children as well,
		// so a skill tagged Type.AOE.PointBlank satisfies a requirement of
		// Type.AOE. That hierarchy is the reason the design's tags are dotted.
		if (!SkillTags.HasTag(Required))
		{
			return false;
		}
	}
	return true;
}

FString UCataclysmStatPipeline::ValidateModifier(const FCataclysmStatModifier& Modifier)
{
	if (Modifier.Bucket == ECataclysmStatBucket::More)
	{
		if (!CanGrantMore(Modifier.Source))
		{
			return FString::Printf(
				TEXT("a More multiplier from %s. Only a gem, a passive keystone, "
					 "an enchantment or a skill's own buff may grant one; "
					 "everything else is flat or increased."),
				*UEnum::GetValueAsString(Modifier.Source));
		}
		if (Modifier.Value <= -100.0f)
		{
			return FString::Printf(
				TEXT("a More multiplier of %.1f%% would zero or invert the "
					 "stat. A Less multiplier cannot reach -100%%."),
				Modifier.Value);
		}
	}

	// A REMOVAL COMES FROM THE SOURCES THAT MAY GRANT A MORE MULTIPLIER, AND
	// FROM NO OTHER. Issue #1791. Taking a stat away entirely is a larger thing
	// than multiplying it, so an ordinary affix may do neither; the readability
	// rule in `CanGrantMore` is the same rule here. Its value is not checked,
	// because nothing reads it.
	if (Modifier.Bucket == ECataclysmStatBucket::Removed
		&& !CanGrantMore(Modifier.Source))
	{
		return FString::Printf(
			TEXT("a removal from %s. Only a gem, a passive keystone, an "
				 "enchantment, a skill's own buff or a dungeon rule may remove "
				 "a stat; everything else is flat or increased."),
			*UEnum::GetValueAsString(Modifier.Source));
	}

	// A HEALTH THRESHOLD OUTSIDE 0 TO 100 IS A MODIFIER THAT NEVER APPLIES OR
	// ALWAYS DOES, and either way it is not what was meant. Issue #959. Zero is
	// legitimate and means "only at exactly no health", which is unreachable in
	// play but is not a data error; 100 is legitimate and means "always", though
	// `Always` says that more plainly.
	//
	// BOTH HEALTH THRESHOLDS ARE BOUNDED HERE, since issue #1051. The strict one
	// reads the same percentage and the bound says the same thing about it. Its
	// endpoints mean the opposite of the other predicate's, which changes
	// nothing about the bound: 0 means "never", because nothing is strictly
	// below no health, and 100 means "always except at full health".
	//
	// ALL THREE, SINCE ISSUE #1070, and the third one is the reason to say this
	// out loud rather than to leave the list to be read. `HealthAbovePercent`
	// points the other way, and a threshold of 150 on it would be a modifier
	// that never applies -- the same silent failure, arrived at from the
	// opposite side. A bound written as a list is a bound somebody has to
	// remember to extend.
	//
	// ALL FOUR SINCE ISSUES #1653 AND #41, AND THAT WARNING WAS EARNED. The
	// sentence above predicted that a list-shaped bound would need extending by
	// hand, and `HealthAtOrAbovePercent` is the extension it predicted. Nothing
	// would have failed had it been left out: an unbounded threshold is exactly
	// the silent failure this check exists to catch, so the omission would have
	// been invisible until a sheet wrote 150.
	// ALL FIVE SINCE ISSUE #1515, AND THE FIFTH IS THE FIRST THAT READS SOMEBODY
	// ELSE'S HEALTH. `TargetHealthBelowPercent` is a percentage of the TARGET's
	// maximum rather than the character's own, which changes nothing about the
	// bound: a share is between 0 and 100 whoever it belongs to. The warning
	// four paragraphs up predicted this list would need extending by hand a
	// second time, and it did.
	if ((Modifier.Condition == ECataclysmStatCondition::HealthAtOrBelowPercent
		 || Modifier.Condition == ECataclysmStatCondition::HealthBelowPercent
		 || Modifier.Condition == ECataclysmStatCondition::HealthAbovePercent
		 || Modifier.Condition
			== ECataclysmStatCondition::HealthAtOrAbovePercent
		 || Modifier.Condition
			== ECataclysmStatCondition::TargetHealthBelowPercent)
		&& (Modifier.ConditionValue < 0.0f || Modifier.ConditionValue > 100.0f))
	{
		return FString::Printf(
			TEXT("a health threshold of %.1f%%. A percentage of maximum health "
				 "is between 0 and 100."),
			Modifier.ConditionValue);
	}

	// AND A SKILL'S COST THRESHOLD IS A PERCENTAGE OF MAXIMUM HEALTH TOO, so it
	// is bounded the same way. Issue #983. A negative threshold would be
	// satisfied by every skill including the ones that cost nothing, since the
	// comparison is strictly greater than; the whole node would become an
	// unconditional bonus.
	//
	// THE UPPER BOUND IS NOT 100. A skill's total cost is its own share of
	// CURRENT health plus the character's added share of MAXIMUM health, and
	// only the second of those is bounded by the maximum, so a cost above 100%
	// of maximum health is arithmetically possible even though nothing in the
	// designed data reaches it. The bound is a sanity limit on the THRESHOLD
	// rather than on the cost: a threshold above 100% is far likelier to be a
	// number in the wrong column than a deliberate design.
	if (Modifier.Condition == ECataclysmStatCondition::SkillHealthCostAbovePercent
		&& (Modifier.ConditionValue < 0.0f || Modifier.ConditionValue > 100.0f))
	{
		return FString::Printf(
			TEXT("a skill cost threshold of %.1f%%. A percentage of maximum "
				 "health is between 0 and 100."),
			Modifier.ConditionValue);
	}

	// A WINDOW OF NO LENGTH NEVER HOLDS, and a negative one is not a shorter
	// window but a nonsensical one. Issue #962. Either is a modifier that grants
	// nothing while looking as though it grants something, which is the same
	// class of silent failure the threshold check above exists for.
	if ((Modifier.Condition == ECataclysmStatCondition::WithinSecondsOfHealthCost
			|| Modifier.Condition
				== ECataclysmStatCondition::WithinSecondsOfForeignDamage)
		&& Modifier.ConditionValue <= 0.0f)
	{
		return FString::Printf(
			TEXT("a window of %.1f seconds. A window has to be longer than "
				 "nothing or the bonus never applies."),
			Modifier.ConditionValue);
	}

	// AND THE THREE MOVEMENT THRESHOLDS ARE THE SAME SHAPE. Issue #41, slice 2.
	// Each compares at least, so a threshold of nothing or less is met by every
	// character at every moment: standing still for at least no time is standing
	// still. The node would read as conditional and be unconditional, which is
	// the silent failure the checks above exist for.
	if ((Modifier.Condition == ECataclysmStatCondition::StationaryForSeconds
			|| Modifier.Condition == ECataclysmStatCondition::NotAttackedForSeconds
			|| Modifier.Condition
				== ECataclysmStatCondition::MetresMovedBeforeAttack)
		&& Modifier.ConditionValue <= 0.0f)
	{
		return FString::Printf(
			TEXT("a movement threshold of %.1f. Each of these compares at least, "
				 "so a threshold of nothing is met always and the bonus would be "
				 "unconditional."),
			Modifier.ConditionValue);
	}

	// A SCALE WITH NO STEP SIZE IS WORTH NOTHING AT EVERY STATE. Issue #968.
	// `ScaledValue` answers zero for it rather than dividing by nothing, so the
	// modifier applies, the arithmetic runs, and the node grants nothing at all
	// -- the same silent shape the two condition checks above exist for.
	if (Modifier.Scale != ECataclysmStatScale::Fixed && Modifier.ScaleStep <= 0.0f)
	{
		return FString::Printf(
			TEXT("a scaling step of %.1f. A modifier that grows with a state "
				 "needs a step larger than nothing, or it is worth nothing at "
				 "every state."),
			Modifier.ScaleStep);
	}

	return FString();
}

FCataclysmStatBreakdown UCataclysmStatPipeline::Accumulate(
	float Base,
	const TArray<FCataclysmStatModifier>& Modifiers,
	const FGameplayTagContainer& SkillTags,
	const FCataclysmStatConditions& State)
{
	FCataclysmStatBreakdown Out;
	Out.Base = Base;
	Out.MoreMultiplier = 1.0f;

	for (const FCataclysmStatModifier& Modifier : Modifiers)
	{
		if (!ModifierApplies(Modifier, SkillTags, State))
		{
			continue;
		}

		// WHAT IT IS WORTH RIGHT NOW, WHICH IS NOT ALWAYS WHAT IT SAYS. Issue
		// #968. A modifier whose size grows with the character's state -- "+1%
		// increased Attack Damage per point for every 5% of your maximum health
		// that is missing" -- is worth its value times however many whole steps
		// of that state the character currently has.
		//
		// ONE PLACE, BEFORE THE BUCKETS, so all three get the same treatment and
		// none of them has to know that a value can scale.
		//
		// A FIXED MODIFIER ANSWERS ITS OWN VALUE, so nothing that existed before
		// that issue changes by a single number.
		const float Value = ScaledValue(Modifier, State);

		switch (Modifier.Bucket)
		{
		case ECataclysmStatBucket::Flat:
			Out.Flat += Value;
			break;

		case ECataclysmStatBucket::Increased:
			// Everything here adds together and multiplies exactly once, which
			// is what gives this bucket its diminishing returns.
			Out.SumOfIncreases += Value;
			break;

		case ECataclysmStatBucket::More:
			if (!CanGrantMore(Modifier.Source))
			{
				// Ignored rather than clamped. Honouring it would break the rule
				// the whole three-bucket split rests on.
				++Out.RejectedMoreCount;
				UE_LOG(LogCataclysm, Warning,
					   TEXT("Stat pipeline ignored a More multiplier: %s"),
					   *ValidateModifier(Modifier));
				break;
			}
			{
				float MoreValue = Value;
				if (MoreValue < LessMultiplierFloor)
				{
					// Clamped rather than ignored. Ignoring would make the stat
					// larger than the data asked for; clamping keeps the
					// direction and preserves the invariant that no Less
					// multiplier zeroes or inverts a stat.
					++Out.ClampedLessCount;
					UE_LOG(LogCataclysm, Warning,
						   TEXT("Stat pipeline clamped a Less multiplier from "
								"%.1f%% to %.1f%%: %s"),
						   MoreValue, LessMultiplierFloor,
						   *ValidateModifier(Modifier));
					MoreValue = LessMultiplierFloor;
				}
				// Each source multiplies on its own. They are NOT summed first,
				// which is the whole difference from the bucket above: two 50%
				// More multipliers give 2.25x where two 50% increases give 2.0x.
				Out.MoreMultiplier *= 1.0f + MoreValue / 100.0f;
				++Out.MoreSourceCount;
			}
			break;

		case ECataclysmStatBucket::Removed:
			// A REMOVAL CARRIES NO AMOUNT, SO ONLY THAT ONE ARRIVED IS KEPT.
			// Issue #1791. `Evaluate` reads the count; the value worked out
			// above is not read.
			if (!CanGrantMore(Modifier.Source))
			{
				// Ignored, for the reason a refused More multiplier is: an
				// ordinary affix may not take a stat away any more than it may
				// multiply one.
				UE_LOG(LogCataclysm, Warning,
					   TEXT("Stat pipeline ignored a removal: %s"),
					   *ValidateModifier(Modifier));
				break;
			}
			++Out.RemovedCount;
			break;
		}
	}

	return Out;
}

FCataclysmStatBreakdown UCataclysmStatPipeline::Evaluate(
	float Base,
	const TArray<FCataclysmStatModifier>& Modifiers,
	const FGameplayTagContainer& SkillTags,
	const FCataclysmStatConditions& State)
{
	FCataclysmStatBreakdown Out = Accumulate(Base, Modifiers, SkillTags, State);

	Out.Final = (Out.Base + Out.Flat)
			  * (1.0f + Out.SumOfIncreases / 100.0f)
			  * Out.MoreMultiplier;

	// A REMOVED STAT IS WORTH NOTHING, WHATEVER THE BUCKETS MADE OF IT. Issue
	// #1791, in the project owner's words: "Just multiply the final number of
	// the original formula by 0." LAST, so every step above stays in the
	// breakdown and a character sheet can show what the stat would have been.
	if (Out.RemovedCount > 0)
	{
		Out.Final *= 0.0f;
	}

	return Out;
}

FCataclysmStatBreakdown UCataclysmStatPipeline::EvaluateRate(
	float Base,
	const TArray<FCataclysmStatModifier>& Modifiers,
	const FGameplayTagContainer& SkillTags,
	const FCataclysmStatConditions& State)
{
	FCataclysmStatBreakdown Out = Accumulate(Base, Modifiers, SkillTags, State);

	// A rate divides by both buckets. Dividing is what stops any amount of
	// cooldown reduction reaching zero, so the stat needs no cap.
	const float Divisor = (1.0f + Out.SumOfIncreases / 100.0f) * Out.MoreMultiplier;

	// The floor under a Less multiplier already keeps MoreMultiplier above
	// zero, and increases are not permitted to be negative enough to invert the
	// first bracket in any designed data. This guard is here so that a divisor
	// at or below zero produces the unmodified base rather than a negative or
	// infinite interval.
	Out.Final = Divisor > UE_SMALL_NUMBER ? Out.Base / Divisor : Out.Base;

	// A REMOVAL LEAVES NO REDUCTION AT ALL, SO THE INTERVAL IS ITS BASE. Issue
	// #1791. What a rate divides by is the reduction, and removing it says "you
	// have no cooldown reduction", whatever increases and More multipliers
	// reached it. It is not a cooldown of no length.
	//
	// THE SAME ANSWER THE GAME GIVES BY THE OTHER ROUTE. `cooldown_reduction`
	// reaches a skill as an attribute that `UCataclysmPlayerClassStats::ApplyTo`
	// writes through `Evaluate`, which takes a removed stat to nothing, and a
	// reduction of nothing leaves every cooldown at its base length.
	if (Out.RemovedCount > 0)
	{
		Out.Final = Out.Base;
	}

	return Out;
}

float UCataclysmStatPipeline::DisplayedRateReduction(
	const FCataclysmStatBreakdown& Breakdown)
{
	// NONE IS SHOWN FOR A REMOVED REDUCTION, because `EvaluateRate` applies
	// none. Issue #1791. The increases stay in the breakdown and must not be
	// shown as though they still counted.
	if (Breakdown.RemovedCount > 0)
	{
		return 0.0f;
	}

	const float Divisor = (1.0f + Breakdown.SumOfIncreases / 100.0f)
						* Breakdown.MoreMultiplier;

	if (Divisor <= UE_SMALL_NUMBER)
	{
		return 0.0f;
	}

	// Storing the sum of increases rather than the displayed figure is what
	// keeps this below 100 however large the sum gets: a character with +100%
	// worth of increases is shown 50%, not 100%.
	//
	// One limit, deliberately not papered over. Past a divisor of about 8.4
	// million this expression rounds to exactly 1.0 in single precision and a
	// player is shown 100% while the interval itself is still above zero. The
	// mechanical guarantee is unaffected -- EvaluateRate divides, so it cannot
	// reach zero -- and the magnitude needed is not reachable, requiring roughly
	// 34 compounding 50% sources on one stat against six gem sockets. No clamp
	// is applied here because inventing a display ceiling is the interface
	// work's decision, not this class's.
	return 100.0f * (Divisor - 1.0f) / Divisor;
}
