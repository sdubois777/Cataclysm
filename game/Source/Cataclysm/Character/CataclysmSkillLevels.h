// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
// For `ECataclysmAbilitySlot`, which a skill gadget's and a skill trap's reserve is asked by. Issue #2802.
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "CataclysmSkillLevels.generated.h"

class AActor;
class ACataclysmPlayerState;
class UGameplayAbility;

/**
 * A skill's level: what it costs, what caps it, and what it does to the skill's damage. Issue #2764, the shape ruled by
 * the project owner on 2026-11-19 ("I like the idea of skills leveling as you use them") and its numbers by the owner
 * and the coordinating session the same day. `docs/DECISIONS.md`, 2026-11-19, has the research and the rulings.
 *
 * HOW A SKILL EARNS. A creature's experience, the same figure the character is granted, is split at its death among
 * the player's skills that damaged it, by the share of its health each removed (`ACataclysmPlayerState::
 * GrantSkillExperience`). The character's own experience is unchanged. Dungeon Siege's rule, "(damage / max_hitpoints)
 * x exp_value", paid at the death so the total never passes the kill's. THE BASIC ATTACK DOES NOT LEVEL, and a skill
 * that deals no damage earns nothing; damage over time and minions credit their skill in a second slice, so this one
 * counts direct hits only (`RowThatLevels`).
 *
 * WHAT A LEVEL COSTS. Skill level L costs `PriceShareOfTheCharacters` of the character's own experience up to the
 * character level Path of Exile pairs with gem level L: its "Requires Level" column for Fireball and Cleave
 * (poedb.tw), 1, 2, 4, 7, 11, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 67 and 70. 0.25 is a judgement: a
 * skill dealing a quarter of a fight's damage keeps Path of Exile's gem pace. Because the price follows
 * `UCataclysmExperience`, a re-tuned character curve moves it with it.
 *
 * WHAT CAPS IT. 20, Path of Exile's gem cap, Diablo II's 20 hard points and Last Epoch's default; and the character's
 * level, as Path of Exile's required level caps a gem: a skill cannot pass the level paired with the character's level,
 * and the experience above it is kept for when the character reaches it.
 *
 * WHAT A LEVEL DOES. Each level multiplies the skill's damage percentage by `DamageFactorPerLevel`, so level 20 deals
 * 2.85 times level 1: Path of Exile's Cleave, 179.4% of base damage at gem level 1 to 511.2% at 20, the same factor every
 * level. The percentage multiplies the weapon part of the blow before the increases and the "more" multipliers, so the
 * factor sits in the base bucket (`UCataclysmSkillEffects::ApplyHit`). A skill's stated percentage is its level 1
 * figure. The mana cost does not change: it follows the character's level, as before.
 *
 * `tools/tests/test_skill_levels_port.py` holds every constant and the paired levels equal to
 * `sim/cataclysm_sim/skill_levels.py`.
 */
UCLASS()
class CATACLYSM_API UCataclysmSkillLevels : public UObject
{
	GENERATED_BODY()

public:
	/** Every skill starts here. */
	static constexpr int32 FirstLevel = 1;

	/** The cap: Path of Exile's gems, Diablo II's hard points, Last Epoch's default. */
	static constexpr int32 MaxLevel = 20;

	/** What each level multiplies the skill's damage percentage by: level 20 is 1.0567 ^ 19, 2.85 times level 1. */
	static constexpr double DamageFactorPerLevel = 1.0567;

	/**
	 * What each level multiplies a skill's costs by: its mana cost, its Fervour cost and what each thing it makes
	 * reserves. Issue #2802: the owner, 2026-11-21, "make it so leveling the skill lowers the cost and increases the
	 * damage", ruled by the coordinating session the same day. 0.5 to the power of one nineteenth, so level 20 costs half
	 * of level 1: a judgement between Path of Exile's Enlighten Support (92% at its normal top level, poedb.tw) and Diablo
	 * II's Teleport (24 mana at level 1, 5 at 20, classic.battle.net). `docs/DECISIONS.md` has the research.
	 */
	static constexpr double CostFactorPerLevel = 0.9642;

	/** The share of the character's experience to the paired character level that a skill level costs. A judgement. */
	static constexpr double PriceShareOfTheCharacters = 0.25;

	/** The character level Path of Exile pairs with this skill level, clamped to 1 and `MaxLevel`. */
	static int32 PairedCharacterLevel(int32 SkillLevel);

	/** A skill's total experience at the start of this level: 0 at level 1. */
	static double ExperienceToReach(int32 SkillLevel);

	/**
	 * The level a skill holding `Experience` stands at for a character of `CharacterLevel`: the highest level it has paid
	 * for whose paired character level the character has reached. Experience above that is kept, not lost.
	 */
	static int32 LevelFor(double Experience, int32 CharacterLevel);

	/** What a skill at this level multiplies its damage percentage by: 1 at level 1. */
	static double DamageFactor(int32 SkillLevel);

	/** What a skill at this level multiplies its costs by: 1 at level 1, `CostFactorPerLevel` each level, clamped. */
	static double CostFactor(int32 SkillLevel);

	/**
	 * The cost factor of `Ability`'s level, held by its player: 1 for a skill nobody levels -- the basic attack, an
	 * enemy's, a skill with no row. The counterpart of `LevelFactorOf`, read where each cost is worked out
	 * (`UCataclysmGameplayAbility::FervourCostFor` and `ManaCostAtTheHoldersLevel`, and a summon's reserve). Issue #2802.
	 */
	static double CostFactorOf(const UGameplayAbility* Ability);

	/**
	 * The row of the levelling skill `Player` holds in `Slot`: the skill whose spec carries the slot's tag, as a key
	 * press finds it. None for an empty slot, the basic attack, a skill with no row and a non-player. Issue #2802: a
	 * skill gadget and a skill trap are made by their slot's skill, so its level prices their reserve.
	 */
	static FName RowInSlot(const AActor* Player, ECataclysmAbilitySlot Slot);

	/** The cost factor of the level of the skill `Player` holds in `Slot`; 1 where `RowInSlot` is None. Issue #2802. */
	static double CostFactorOfSlot(const AActor* Player, ECataclysmAbilitySlot Slot);

	/**
	 * Re-prices what everything `SkillRow`'s skill made holds back of its commander's pool, at the skill's level now:
	 * the thralls it took (`ACataclysmEnemyCharacter::TakenBySkillRow`) and the minions it summoned
	 * (`ACataclysmMinion::SummoningSkillRow`), each to the skill's `UCataclysmSummonSkill::FervourReserveOfOne`; and
	 * each skill gadget and skill trap standing for a slot that holds the skill, to
	 * `ACataclysmMinion::SkillGadgetReserveFor` or `SkillTrapReserveFor`. A level
	 * never raises a reserve, so a re-price only ever frees room; a reserve already lower is kept. Nothing happens when
	 * the player no longer holds the skill. Issue #2802, ruled by the coordinating session on 2026-11-21: thralls are
	 * taken for good, so a thrall priced once at the take would hold level 1's figure after Subjugate reached level 20.
	 * Called by `ACataclysmPlayerState` wherever a skill's level can rise.
	 */
	static void RepriceReservesOf(const ACataclysmPlayerState* Holder, FName SkillRow);

	/**
	 * How full the skill's bar is, from 0 to 1: the experience into its level over what the next level costs. 1 at the
	 * cap, and 1 for a skill that has paid for the next level and waits on the character's.
	 */
	static float FractionFor(double Experience, int32 CharacterLevel);

	/**
	 * The words for the skill's level: "Level 3: 45.2%", rounded down to a tenth so it never claims a level not
	 * reached; "Level 3: waits for character level 7" when the next level is paid for; "Level 20: maximum".
	 */
	static FString TextFor(double Experience, int32 CharacterLevel);

	/**
	 * The skill row a blow by `Ability` earns experience for and is scaled by, or None: a player's skill template with a
	 * row, and not its basic attack. A creature's ability, the placeholder that deals nothing, the basic attack and an
	 * ability not yet given answer None.
	 */
	static FName RowThatLevels(const UGameplayAbility* Ability);

	/** The player state holding `Ability`, or null: its ability system belongs to the player state. */
	static const ACataclysmPlayerState* PlayerHolding(const UGameplayAbility* Ability);

	/**
	 * What a blow by `Ability` on `Target` multiplies its damage percentage by: the skill's level factor when the blow
	 * is a levelling skill's (`RowThatLevels`) on a creature, and 1 otherwise -- a blow on the skill's own user, an ally
	 * or anything not a creature keeps its stated figure.
	 */
	static double DamageFactorForBlow(const UGameplayAbility* Ability, const AActor* Target);

	/**
	 * `Ability`'s level factor, whatever it strikes: `DamageFactor` of its holder's level in its row
	 * (`RowThatLevels`), and 1 for a skill no row levels or no player holds. Issue #2764, slice 2b: read once, when a
	 * ground zone is laid, a minion summoned or a thrall taken, and kept on it for its blows on creatures.
	 */
	static double LevelFactorOf(const UGameplayAbility* Ability);

	/**
	 * The row a blow or an effect earns experience for and is scaled by: `RowThatLevels(Ability)` while the skill object
	 * is there, and otherwise `RowWithNoSkill`, the row captured from it while it was, which a projectile, a ground zone,
	 * a buried weapon or a trap's damage over time carries after a one-use grant has been removed. Issue #2764, slice 2a.
	 * None for a blow no levelling skill dealt.
	 */
	static FName RowOfBlow(const UGameplayAbility* Ability, FName RowWithNoSkill);

	/** The player state whose ability system `Instigator` acts through, or null: a creature, a prop, nothing. */
	static const ACataclysmPlayerState* PlayerOf(const AActor* Instigator);

	/**
	 * What a blow or an effect of `Row`, held by `Holder`, multiplies its figure by on `Target`: the row's level factor
	 * on a creature, 1 otherwise or with no row or holder. Issue #2764, slice 2a.
	 */
	static double DamageFactorForRow(FName Row, const ACataclysmPlayerState* Holder, const AActor* Target);

	/**
	 * `DamageFactorForBlow` for a blow whose skill object may have gone: the skill's own factor while it is there, and
	 * otherwise the carried row's, read on the player `Instigator` acts for. Issue #2764, slice 2a: a gadget's or a
	 * trap's late blow, a buried axe's leap, keep the level the cast had.
	 */
	static double DamageFactorFor(const UGameplayAbility* Ability, FName RowWithNoSkill, const AActor* Instigator,
								  const AActor* Target);
};
