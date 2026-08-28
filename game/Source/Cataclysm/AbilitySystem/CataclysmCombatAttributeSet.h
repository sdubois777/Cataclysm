// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmAttributeAccessors.h"
#include "CataclysmCombatAttributeSet.generated.h"

/**
 * The derived combat and utility stats: defence, offence, and utility.
 *
 * WHAT IS AND IS NOT CLAMPED HERE. The design has two kinds of cap, and only one
 * of them is a clamp:
 *
 *   HARD -- crit chance at 100%. Above that it means nothing, so it is clamped.
 *
 *   SOFT -- evasion at 60%. Gear enchantments may exceed it, so it is NOT
 *           clamped. Clamping would delete the over-cap the design allows.
 *
 * Block chance has no cap at all, because a block is not a full avoid: it
 * removes 50% of a hit's damage. A character at 100% block chance therefore has
 * 50% damage reduction against what block applies to, which is strong but is not
 * immunity. Cooldown reduction likewise needs no cap, because it is calculated
 * by division and can never reach zero.
 *
 * WHERE THESE VALUES COME FROM. Most have a class base. Attack speed's base
 * comes from the equipped weapon and critical strike chance's from the skill
 * being used; the character contributes only increases to those two. Area of
 * effect and damage over time frequency are percentages of whatever the skill
 * does, so they baseline at 100 rather than zero.
 */
UCLASS()
class CATACLYSM_API UCataclysmCombatAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UCataclysmCombatAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** Hard cap. Above 100% a critical strike chance means nothing. */
	static constexpr float CritChanceCap = 100.0f;

	/** Soft cap. Recorded, and deliberately not enforced as a clamp. */
	static constexpr float EvasionSoftCap = 60.0f;

	/** A successful block removes this share of the hit's damage. */
	static constexpr float BlockDamageReduction = 50.0f;

	// -- Defence ----------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_Armor)
	FGameplayAttributeData Armor;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, Armor)

	/** Chance to avoid a direct attack entirely. Does not apply to area damage. */
	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_Evasion)
	FGameplayAttributeData Evasion;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, Evasion)

	/** Chance a hit is blocked. Applies to area damage as well as direct hits. */
	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_BlockChance)
	FGameplayAttributeData BlockChance;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, BlockChance)

	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_DamageReduction)
	FGameplayAttributeData DamageReduction;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DamageReduction)

	/**
	 * Damage reduction that multiplies rather than joining the pool above,
	 * already combined into one percentage.
	 *
	 * WHY A SECOND ATTRIBUTE. Twelve passive tree nodes grant damage reduction
	 * and call it "(multiplicative)". The project owner confirmed on 2026-08-17
	 * that multiplicative means "more", the same word Path of Exile and Last
	 * Epoch use, so each source removes a share of what the ones before it left
	 * rather than adding into `DamageReduction`. The 75% cap binds the additive
	 * pool only; this bucket cannot reach 100% however many sources feed it.
	 * Issue #665.
	 *
	 * ALREADY COMBINED, which is what "one percentage" means. Several sources
	 * multiply together and the product is what is stored here, because an
	 * attribute is one number.
	 * `UCataclysmDamageCalculation::CombinedMoreDamageReduction` is the one place
	 * that multiplication is written, and whoever writes this must use it rather
	 * than adding the sources up.
	 *
	 * NOTHING WRITES IT YET, AND THAT IS SAID HERE RATHER THAN LEFT TO BE FOUND.
	 * Passive trees are not implemented at all -- no code in `game/Source` loads
	 * a class tree -- so there is no source for this today, every character sits
	 * at zero, and no existing behaviour changes. This project has been bitten
	 * three times by an attribute whose comment described an intention nobody had
	 * built, in issues #647, #649 and #520, so to be exact about which half this
	 * is: the arithmetic that consumes this IS built and IS tested, and what does
	 * not exist is the passive tree that would supply a value.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_DamageReductionMore)
	FGameplayAttributeData DamageReductionMore;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DamageReductionMore)

	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_Retaliation)
	FGameplayAttributeData Retaliation;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, Retaliation)

	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_CrowdControlResistance)
	FGameplayAttributeData CrowdControlResistance;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, CrowdControlResistance)

	// -- Offence ----------------------------------------------------------

	/**
	 * What one basic attack deals before any skill multiplier, in points.
	 *
	 * THE NUMBER EVERY SKILL IS A PERCENTAGE OF. The Skill Slots sheet quotes a
	 * Heavy Attack at 250% and an Ultimate at 400%, and the basic attack is
	 * 100% because it IS weapon damage. Without this attribute all of those were
	 * percentages of nothing.
	 *
	 * ITS BASE COMES FROM THE EQUIPPED WEAPON, like attack speed and unlike most
	 * stats. The Item Bases sheet carries it as the `attack_damage` implicit: a
	 * Greataxe reads 72, a Staff 66 and a Fist 30, and a two-handed base doubles
	 * its implicit so the Greataxe supplies 144 in play. The affix pool adds to
	 * the same stat, both flat and increased, which is why it has to be one
	 * attribute rather than a number read off the weapon at the moment of use.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_AttackDamage)
	FGameplayAttributeData AttackDamage;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, AttackDamage)

	/** Base comes from the skill being used, not from the character. */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_CritChance)
	FGameplayAttributeData CritChance;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, CritChance)

	/**
	 * The most critical strike chance THIS character may have. 100 by default.
	 *
	 * WHY A CAP THAT VARIES PER CHARACTER. Every other cap in this project is one
	 * constant shared by everyone. This one has a downside enchantment that
	 * lowers it: the Enchantments sheet of `docs/All_Things_Cataclysm.xlsx`
	 * carries "Your critical hit chance cannot exceed 30%-50%", and there was
	 * nowhere for a personal ceiling to live, so the enchantment could not do
	 * anything. Issue #680.
	 *
	 * NOTHING RAISES IT, AND `CritChanceCap` IS WHY. The project owner ruled on
	 * 2026-08-17 that critical strike chance is hard-capped at 100% and nothing
	 * raises it, so this attribute is itself clamped to that constant. It can
	 * only ever be lowered. That is the opposite of maximum resistance, where one
	 * enchantment raises the cap to a ceiling of 90%.
	 *
	 * IT BOUNDS TWO THINGS, because the chance arrives by two routes since issue
	 * #657. `PreAttributeChange` holds the attribute itself under it, and
	 * `UCataclysmVitalAttributeSet` holds a skill's own stated chance under it
	 * when a blow lands -- a skill stating 80% on a character capped at 30% deals
	 * its blow at 30%.
	 *
	 * NOTHING LOWERS IT YET. No code reads an enchantment's text into a number;
	 * that is issue #45. Every character sits at 100 and no behaviour changes.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_MaxCritChance)
	FGameplayAttributeData MaxCritChance;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, MaxCritChance)

	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_CritMultiplier)
	FGameplayAttributeData CritMultiplier;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, CritMultiplier)

	/** Base comes from the equipped weapon, not from the character. */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_AttackSpeed)
	FGameplayAttributeData AttackSpeed;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, AttackSpeed)

	/** A percentage of whatever the skill does, so 100 means unchanged. */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_AreaOfEffect)
	FGameplayAttributeData AreaOfEffect;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, AreaOfEffect)

	// -- The three damage over time levers ---------------------------------
	//
	// A damage over time effect deals a FIXED AMOUNT PER TICK, so its total is
	// damage per tick x ticks per second x seconds. Each of those three is
	// scalable on its own and all three multiply the same output. One set is
	// shared by every damage over time effect rather than each ailment carrying
	// three of its own. Issues #205 and #220.
	//
	// Each is a percentage of whatever the effect itself does, so 100 means
	// unchanged, which is the shape AreaOfEffect already has.

	/** Damage per tick, as a percentage of the effect's own. 100 unchanged. */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_DotDamage)
	FGameplayAttributeData DotDamage;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DotDamage)

	/** Ticks per second, as a percentage of the effect's own. 100 unchanged. */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_DotFrequency)
	FGameplayAttributeData DotFrequency;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DotFrequency)

	/** How long it runs, as a percentage of the effect's own. 100 unchanged. */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_DotDuration)
	FGameplayAttributeData DotDuration;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DotDuration)

	/** Percentage points subtracted from a target's RESISTANCE. */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_Penetration)
	FGameplayAttributeData Penetration;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, Penetration)

	/**
	 * Percentage of a target's ARMOR ignored. A different stat from the
	 * resistance penetration above, and not interchangeable with it.
	 *
	 * TWO STATS BECAUSE THE DESIGN HAS TWO SOURCES AND TWO STEPS. The enchantment
	 * tables have always separated them -- "Your skills ignore 10%-25% of enemy
	 * resistances" against "Your skills ignore 10%-25% of enemy armor" -- and the
	 * damage calculation applies armor at step 3 and resistance at step 4.
	 * `Attacker.armor_penetration` in `sim/cataclysm_sim/damage.py` has taken
	 * them as two parameters since it was written.
	 *
	 * NOTHING HELD ONE UNTIL ISSUE #520. `FCataclysmIncomingHit::ArmorPenetration`
	 * was applied correctly by `UCataclysmDamageCalculation::Resolve` and was
	 * never set, because there was no attribute to read. Three enchantments in
	 * `game/Data/EnchantmentsPositive.csv` grant it and none could do anything.
	 *
	 * IT MATTERS MORE THAN IT DID. Enemy armour reached no arithmetic at all
	 * until issue #481 and is now the largest single mitigation layer on the most
	 * armoured creatures: the Abyssal Warden's 5,954 armour removes 48.19% of a
	 * hit at difficulty tier 8.
	 *
	 * A PIERCING WEAPON'S FLAT 20% IS NOT HERE. That belongs to the blow rather
	 * than to the character, because it depends on what is in the hand at the
	 * moment of the hit, and it ADDS to whatever this holds. Carrying the weapon
	 * sub-type to a hit is separate work, split out of #520.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_ArmorPenetration)
	FGameplayAttributeData ArmorPenetration;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, ArmorPenetration)

	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_SpellDamage)
	FGameplayAttributeData SpellDamage;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, SpellDamage)

	// -- Offence against one type of enemy ---------------------------------
	//
	// EIGHT FIGURES, ONE PER DAMAGE TYPE, and the offensive mirror of the eight
	// resistances. Each is a percentage that joins the increases bracket, and it
	// applies only when the TARGET's own damage type is that one. An enemy has a
	// damage type of its own, which is its Cataclysm's.
	//
	// THEY ARE INCREASES, NOT MULTIPLIERS. The damage pipeline is
	// (base + flat) x (1 + increases) x more1 x more2, and these add into the
	// same increases bracket as Increased Damage rather than becoming a third
	// multiplier. That is what Diablo 4 and Last Epoch both do with conditional
	// damage: a bonus with a stated condition is additive.
	//
	// NOTHING READS THEM YET. The affix that grants them exists in the data,
	// but applying a conditional increase needs the damage calculation to know
	// the target's type, which belongs with loot generation and the damage
	// pipeline. Issue #213 added the stat and the affix; issue #44 implements
	// the affix system that fills them.

	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_DamageVsWar)
	FGameplayAttributeData DamageVsWar;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DamageVsWar)

	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_DamageVsDemonic)
	FGameplayAttributeData DamageVsDemonic;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DamageVsDemonic)

	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_DamageVsDeath)
	FGameplayAttributeData DamageVsDeath;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DamageVsDeath)

	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_DamageVsPestilence)
	FGameplayAttributeData DamageVsPestilence;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DamageVsPestilence)

	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_DamageVsFamine)
	FGameplayAttributeData DamageVsFamine;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DamageVsFamine)

	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_DamageVsCelestial)
	FGameplayAttributeData DamageVsCelestial;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DamageVsCelestial)

	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_DamageVsChaos)
	FGameplayAttributeData DamageVsChaos;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DamageVsChaos)

	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_DamageVsVoid)
	FGameplayAttributeData DamageVsVoid;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DamageVsVoid)

	// -- Utility ----------------------------------------------------------

	/** Metres per second. */
	UPROPERTY(BlueprintReadOnly, Category = "Utility", ReplicatedUsing = OnRep_MovementSpeed)
	FGameplayAttributeData MovementSpeed;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, MovementSpeed)

	/**
	 * The accumulated sum of cooldown increases, NOT the displayed percentage.
	 *
	 * A skill's final cooldown is its base divided by (1 + this). The displayed
	 * reduction is this divided by (1 + this). Storing the sum rather than the
	 * displayed figure is what keeps cooldowns from ever reaching zero.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Utility", ReplicatedUsing = OnRep_CooldownReduction)
	FGameplayAttributeData CooldownReduction;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, CooldownReduction)

	/**
	 * The chance, in percent, that a skill does not go on cooldown at all.
	 *
	 * THE MASOCHIST'S THE CATALYST NODE IS ITS ONLY SOURCE. Issue #973. "While
	 * at or below 5% health, your skills have a 5% chance per point not to go on
	 * cooldown." It holds eight points, so 40% at most.
	 *
	 * IT IS NOT ON THE CHARACTER SHEET, for the reason the maximum critical
	 * strike chance is not: no affix grants it, nothing scales it, and it has no
	 * baseline of its own. Every class starts at zero.
	 *
	 * THIS ATTRIBUTE HOLDS ZERO AT ALL TIMES BY DESIGN, and a reader that took
	 * its value would find the node doing nothing. The only source carries a
	 * health condition, and a conditional bonus is deliberately never written
	 * onto a gameplay attribute -- it would be stale the moment health moved.
	 * `UCataclysmGameplayAbility::ApplyCooldown` asks
	 * `UCataclysmAbilitySystemComponent::StatForSkill` for it instead, which
	 * runs the pipeline again with the character's health in hand. The attribute
	 * exists because `UCataclysmPlayerClassStats::ApplyTo` writes one per stat
	 * and drops any stat that has none.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Utility", ReplicatedUsing = OnRep_CooldownSkipChance)
	FGameplayAttributeData CooldownSkipChance;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, CooldownSkipChance)

	/**
	 * The chance, in percent, that a melee critical strike applies Bleeding to
	 * what it hit. Issue #1032.
	 *
	 * THE MASOCHIST'S MUTILATION MASTERY IS ITS ONLY SOURCE: "Your melee
	 * critical strikes have a 5% chance per point to apply Bleeding." It holds
	 * eight points, so 40% at most.
	 *
	 * IT BELONGS TO WHOEVER SWUNG, unlike every other stat read while a blow is
	 * resolved. Armour, resistance and damage taken are the defender's;
	 * `UCataclysmVitalAttributeSet` reads this off the INSTIGATOR, the same way
	 * it already reads the two penetration stats and the critical strike
	 * multiplier.
	 *
	 * IT IS NOT ON THE CHARACTER SHEET, for the reason the cooldown skip chance
	 * above is not: no affix grants it, nothing scales it, it has no baseline of
	 * its own, and one passive node supplies it. Every class starts at zero.
	 *
	 * READ STRAIGHT OFF THE ATTRIBUTE, which is safe only because its one row
	 * carries no condition and no scale, so it IS folded in. A later node
	 * conditioning it would need that read to become a `StatForSkill` call or
	 * the row would be dropped in silence. Issue #1022 is what that looks like.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Utility", ReplicatedUsing = OnRep_BleedOnCritChance)
	FGameplayAttributeData BleedOnCritChance;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, BleedOnCritChance)

	/**
	 * What share of an incoming hit this character actually takes, in percent.
	 *
	 * A HUNDRED IS NORMAL, so 120 is a fifth more and 75 is a quarter less. It is
	 * the same "100 means unchanged" shape `AreaOfEffect` and the three damage
	 * over time stats already use, and it is what lets a node say "20% more" and
	 * another say "25% less" and have the two multiply rather than cancel into a
	 * sum. Issue #1026.
	 *
	 * READ AT STEP 6 OF THE DAMAGE CALCULATION, after every mitigation layer and
	 * before the mana and energy shield steps. `docs/DECISIONS.md` carries why
	 * that position and not another: the layers before it are all multiplications
	 * so their order does not matter, and the energy shield is a minimum, so
	 * being before it is what makes a bigger blow spend more shield.
	 *
	 * ITS BASE COMES FROM `UCataclysmPlayerClassStats::EngineSuppliedBases` AND
	 * NOT FROM A CLASS LINE. No affix grants it, no class differs on it, and
	 * passive tree nodes are its only source, which is exactly the rule
	 * `Cataclysm.Attributes.CharacterSheetIsComplete` uses to decide that a stat
	 * is off the character sheet. Promote it to a class line the day an affix
	 * grants it or a class differs on it.
	 *
	 * THIS ATTRIBUTE HOLDS 100 AT ALL TIMES FOR EVERY CHARACTER TODAY, because
	 * all three nodes that move it carry a condition and a conditional bonus is
	 * never folded into a gameplay attribute. `UCataclysmDamageCalculation`
	 * asks through `StatForSkill` rather than reading this, which is what runs
	 * the pipeline again with the character's state in hand.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_DamageTaken)
	FGameplayAttributeData DamageTaken;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DamageTaken)

	/**
	 * The same, for a hit that is damage over time, and it applies AS WELL.
	 *
	 * TWO STATS AND NOT ONE, because a modifier cannot be scoped to the kind of
	 * blow arriving. `RequiredTags` on a modifier means the skill in the
	 * CHARACTER'S OWN hand, and a character being hit is not swinging;
	 * `DefenderStat` in `CataclysmDamageCalculation.cpp` passes an empty tag
	 * container deliberately and says why. So the hit's own nature picks which
	 * stats are read, the way `ResistanceFor` in that file already picks a
	 * resistance slot from the hit's damage type. Issue #1026.
	 *
	 * BOTH APPLY TO A BLEED TICK, multiplying. `DamageTaken` above is every hit;
	 * this is the extra one a damage over time tick also meets. The Masochist's
	 * Echoes of Agony is its only source: "Damage taken from damage over time
	 * effects is reduced by 1% per point."
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_DamageOverTimeTaken)
	FGameplayAttributeData DamageOverTimeTaken;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DamageOverTimeTaken)

	/**
	 * Whether damage over time deals this character nothing at all. Issue #1039.
	 * Zero for no, above zero for yes.
	 *
	 * A FLAG AND NOT A REDUCTION, AND THAT IS FORCED RATHER THAN CHOSEN. The
	 * Masochist's Vessel Unbroken says "Debuffs on you deal no damage at all",
	 * which as a multiplier would be -100%, and
	 * `UCataclysmStatPipeline::LessMultiplierFloor` clamps a Less multiplier to
	 * -99 on purpose. Ninety-nine per cent less is not none. So a rule that says
	 * "no damage" has to say so as a flag, exactly as
	 * `fervour_loss_suppressed` does for "your Fervour does not decrease".
	 *
	 * IT ZEROES THE DAMAGE AND DOES NOT REMOVE THE DEBUFF, which matters because
	 * the option's own other two clauses count debuffs: "each one grants 5% more
	 * damage and 5 Fervour per second". Removing the debuff would make the
	 * option cancel itself. `UCataclysmDamageCalculation::Resolve` applies this
	 * at the damage over time step and touches nothing else, so the effect
	 * carries on running, keeps its tag, and stays in
	 * `UCataclysmDebuffs::CountOn`.
	 *
	 * IT IS NOT ON THE CHARACTER SHEET, for the reason the two damage taken
	 * stats above are not: no affix grants it, nothing scales it, no class line
	 * names it, and one passive option supplies it. Every class starts at zero.
	 *
	 * ASKED FOR RATHER THAN READ, through `DefenderStat`, so that a future row
	 * carrying a condition works. Its one row today carries none, so it IS
	 * folded into this attribute and both routes give the same answer.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_DebuffDamageSuppressed)
	FGameplayAttributeData DebuffDamageSuppressed;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DebuffDamageSuppressed)

	/**
	 * How far from this character its retaliation reaches, in METRES. Issue
	 * #1047. Zero means it reaches only whatever hit it, which is every
	 * character in the game except one holding a single capstone option.
	 *
	 * METRES AND NOT CENTIMETRES, WHICH IS THE OPPOSITE OF EVERY OTHER DISTANCE
	 * IN THE ENGINE. The design document and the passive tree both speak in
	 * metres -- Reprisal Wave reads "strikes every enemy within 4 metres" -- and
	 * the number is authored on the Passive Effects sheet of the design
	 * workbook, where a designer writes what the node says. The one conversion
	 * to the centimetres Unreal works in happens where the search is run.
	 * Putting the conversion in the sheet instead would mean writing 400 in a
	 * cell beside a node that says 4, and
	 * `test_every_value_appears_in_the_nodes_own_description` would then have
	 * nothing to tie the two together with.
	 *
	 * A DISTANCE AND NOT A FLAG, THOUGH ONE ROW SUPPLIES IT. A flag beside a
	 * constant in C++ would work today and would let the sentence and the code
	 * drift apart in silence; a radius the sheet states cannot, because that
	 * check matches the 4 in the cell against the 4 in the node's own text.
	 *
	 * IT IS NOT ON THE CHARACTER SHEET, for the reason the flag above is not:
	 * no affix grants it, nothing scales it, no class line names it, and one
	 * passive option supplies it. Every class starts at zero.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_RetaliationRadiusMetres)
	FGameplayAttributeData RetaliationRadiusMetres;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, RetaliationRadiusMetres)

	/**
	 * Whether this character's life leech applies to its retaliation. Issue
	 * #1048. Zero for no, above zero for yes.
	 *
	 * A FLAG, AND HERE THERE IS NOTHING ELSE IT COULD BE. The Masochist's
	 * Feeding Wound reads "Your life leech applies to your retaliation damage as
	 * well as to your attacks", which states no quantity at all: how much is
	 * leeched is whatever life leech the character already has. There is no
	 * magnitude to write, so the row is a 1 meaning "on", and `VALUE_IN_WORDS`
	 * in `tools/tests/test_passive_effects_match_the_node_text.py` carries the
	 * words that stand in for the digit the sentence does not have.
	 *
	 * WHAT IT SWITCHES ON IS AN EXCEPTION TO A RULE THE WHOLE GENRE SHARES.
	 * Retaliation deliberately does not hit -- it writes health directly, so
	 * that it cannot critically strike, cannot apply an ailment and cannot be
	 * retaliated against in turn -- and leech is worked out where a hit lands.
	 * So retaliation leeches nothing for everybody, and this option buys the
	 * exception. `docs/Cataclysm_GDD_v2.md` says so in the Retaliation section.
	 *
	 * LIFE LEECH ONLY. Mana leech and energy shield leech are not named by the
	 * node and stay on attacks alone.
	 *
	 * IT IS NOT ON THE CHARACTER SHEET, for the same reason as the two above.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_RetaliationLeeches)
	FGameplayAttributeData RetaliationLeeches;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, RetaliationLeeches)

	/**
	 * What share of this character's MISSING health one nova deals, as a
	 * percentage. Issue #1050. Zero for a character that releases none.
	 *
	 * THE MASOCHIST'S Unstable Aura IS ITS ONLY SOURCE: "While at or below 10%
	 * health, you release a nova every 5 seconds dealing damage equal to 1% of
	 * your missing health per point to enemies within 6 metres." Eight points
	 * make it 8, so a character with 1,000 maximum health standing at 80 deals
	 * 8% of the 920 it is missing, which is 73.6.
	 *
	 * MISSING HEALTH AND NOT MAXIMUM OR CURRENT, which is what makes the node
	 * strongest exactly where its own condition puts the character. At or below
	 * a tenth of its health, a Masochist is missing at least nine tenths of it.
	 *
	 * ONLY THE PER-POINT SHARE IS HERE. How often a nova comes and how far it
	 * reaches are constants of the mechanic on `UCataclysmNova`, which is the
	 * same division The Breaking Point already uses: its 5% per point is a sheet
	 * row and its 50%, its 5 seconds of Bleeding and its 10 second cooldown are
	 * all C++ constants.
	 *
	 * ASKED FOR RATHER THAN READ, because its one row carries a health
	 * condition, so this attribute is zero even for a character holding the
	 * node. A plain read would answer zero for ever and release no nova.
	 *
	 * IT IS NOT ON THE CHARACTER SHEET, for the reason its neighbours above are
	 * not: no affix grants it, nothing scales it, no class line names it, and
	 * one passive node supplies it.
	 */
	/**
	 * How long a lasting harmful effect on this character runs, as a
	 * percentage where 100 is normal. Issue #1033.
	 *
	 * THE DEFENDER'S SIDE OF A QUESTION THE ATTACKER ALREADY HAD ONE OF.
	 * `DotDuration` is how long the character makes its OWN damage over time
	 * last on what it hits. This is how long anything lasts on the character
	 * itself, and it applies to stuns as well as to damaging effects, which
	 * `DotDuration` does not.
	 *
	 * BOTH OF ITS SOURCES LENGTHEN RATHER THAN SHORTEN, which reads backwards
	 * until you know the class. The Masochist's Symphony of Pain adds 2% a
	 * point and its Vessel of Plagues adds 50%, because eleven nodes in that
	 * branch pay the character for each harmful effect it is carrying.
	 *
	 * A HUNDRED FOR EVERY CHARACTER, from
	 * `UCataclysmPlayerClassStats::EngineSuppliedBases`, the same route the
	 * two damage-taken stats above take. A base of zero would make every
	 * lasting effect in the game end instantly.
	 *
	 * IT IS NOT ON THE CHARACTER SHEET, for the reason its neighbours are not:
	 * no affix grants it, nothing scales it, no class line names it, and two
	 * passive nodes of one tree supply it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_DebuffDurationTaken)
	FGameplayAttributeData DebuffDurationTaken;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DebuffDurationTaken)

	/**
	 * Whether the debuffs on this character stop counting down. Issue #1070.
	 * Zero for no, above zero for yes.
	 *
	 * THE MASOCHIST'S Ceaseless Penance IS ITS ONLY SOURCE, the third option of
	 * The Second Vow: "Debuffs on you no longer expire while you are above 50%
	 * health."
	 *
	 * A SEPARATE STAT FROM `DebuffDurationTaken` ABOVE, THOUGH BOTH ARE ABOUT
	 * HOW LONG A DEBUFF LASTS. That one is a percentage applied ONCE, when the
	 * effect is applied, and 100 means unchanged; there is no number in it that
	 * means "for ever". This one is a rule that holds or does not hold from
	 * moment to moment, and `UCataclysmDebuffs::HoldStep` acts on it several
	 * times a second.
	 *
	 * ASKED FOR THROUGH THE STAT PIPELINE AND NEVER READ OFF THE ATTRIBUTE. Its
	 * row carries a health condition, so the attribute holds zero even for a
	 * character that took the option, exactly as The Last Drop's two do.
	 *
	 * IT IS NOT ON THE CHARACTER SHEET, for the reason its neighbours are not:
	 * no affix grants it, nothing scales it, no class line names it, and one
	 * capstone option of one tree supplies it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_DebuffsDoNotExpire)
	FGameplayAttributeData DebuffsDoNotExpire;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DebuffsDoNotExpire)

	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_NovaDamageOfMissingHealth)
	FGameplayAttributeData NovaDamageOfMissingHealth;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, NovaDamageOfMissingHealth)

	/**
	 * How much longer a debuff this character's aura applies lasts, as a
	 * percentage added on top of the effect's own stated duration.
	 *
	 * THE MASOCHIST'S Beacon of Despair IS ITS ONLY SOURCE, at 4% a point.
	 * Issue #1057.
	 *
	 * ZERO ALSO MEANS THE CHARACTER HAS NO AURA, and `UCataclysmContagion::
	 * AuraStep` reads it that way rather than asking a second question. The node
	 * cannot be held at zero points, so the two readings cannot be confused.
	 * That is the shape `NovaDamageOfMissingHealth` above already uses.
	 *
	 * NOT ON THE CHARACTER SHEET, for the reason its neighbours are not: no
	 * affix grants it, nothing scales it, no class line names it, and one
	 * passive node of one tree supplies it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_AuraDebuffDuration)
	FGameplayAttributeData AuraDebuffDuration;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, AuraDebuffDuration)

	/**
	 * The chance one nearby enemy has of catching a debuff when a debuff on this
	 * character deals damage, as a percentage.
	 *
	 * THE MASOCHIST'S Contagious Torment IS ITS ONLY SOURCE, at 1% a point, so
	 * 8% at full investment. Issue #1058. It is rolled once per enemy rather
	 * than once for the group, because the node's sentence makes the chance a
	 * property of each enemy.
	 *
	 * NOT ON THE CHARACTER SHEET, for the same reason as the one above.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_DebuffSpreadChance)
	FGameplayAttributeData DebuffSpreadChance;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DebuffSpreadChance)

	/**
	 * The chance each debuff on a dying enemy has of passing to another enemy
	 * near it, as a percentage.
	 *
	 * THE MASOCHIST'S Empathic Link IS ITS ONLY SOURCE, at 2% a point, so 16% at
	 * full investment. Issue #1060.
	 *
	 * IT IS THE PLAYER'S STAT ABOUT SOMEBODY ELSE'S DEBUFFS, which is what makes
	 * it different from `DebuffSpreadChance` beside it. That one spreads what the
	 * character itself is carrying; this one spreads what a creature that just
	 * died was carrying, and the character holding the node need not be near it.
	 *
	 * NOT ON THE CHARACTER SHEET, for the reason its neighbours are not: no
	 * affix grants it, nothing scales it, it has no baseline of its own, and one
	 * passive node of one tree supplies it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_DeathSpreadChance)
	FGameplayAttributeData DeathSpreadChance;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DeathSpreadChance)

	/**
	 * Increased damage against a target carrying a debuff the attacker also
	 * carries, as a percentage.
	 *
	 * THE MASOCHIST'S Wound Channeling IS ITS ONLY SOURCE, at 1% a point, so 8%
	 * at full investment. Issue #1061.
	 *
	 * THE SECOND STAT IN THE GAME DECIDED BY THE TARGET rather than by the
	 * attacker alone, after the eight increased-damage-against-a-damage-type
	 * stats above. It is read at the moment of a hit by
	 * `UCataclysmDebuffs::DamageAgainstSharedDebuff` and joins the same increases
	 * bracket those eight do.
	 *
	 * NOT ON THE CHARACTER SHEET, and here the reason is sharper than usual: a
	 * sheet has no target in hand, so a bonus that exists only against a
	 * particular enemy cannot be shown on one as though it applied to everything.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_DamageVsSharedDebuff)
	FGameplayAttributeData DamageVsSharedDebuff;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DamageVsSharedDebuff)

	UPROPERTY(BlueprintReadOnly, Category = "Utility", ReplicatedUsing = OnRep_MagicFind)
	FGameplayAttributeData MagicFind;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, MagicFind)

	UPROPERTY(BlueprintReadOnly, Category = "Utility", ReplicatedUsing = OnRep_LootQuantity)
	FGameplayAttributeData LootQuantity;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, LootQuantity)

	static TArray<FGameplayAttribute> GetAllAttributes();

	/**
	 * A skill's cooldown after this character's accumulated increases.
	 *
	 * `MoreMultiplier` is the product of every "more" cooldown source reaching
	 * this skill, as UCataclysmStatPipeline computes it. It DIVIDES rather than
	 * multiplying, because a cooldown is a rate: a source that makes the
	 * interval shorter has to divide, or a cooldown reduction gem would make the
	 * cooldown longer. Pass 1.0 when there are none.
	 */
	static float FinalCooldown(float BaseCooldown, float CooldownIncreases,
							   float MoreMultiplier = 1.0f);

	/** What the interface shows the player, as a percentage. Never reaches 100. */
	static float DisplayedCooldownReduction(float CooldownIncreases,
											float MoreMultiplier = 1.0f);

	/** What a base cooldown is divided by. Never zero, so a cooldown never is. */
	static float CooldownDivisor(float CooldownIncreases, float MoreMultiplier);

protected:
	UFUNCTION() void OnRep_Armor(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_Evasion(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_BlockChance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageReduction(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageReductionMore(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_Retaliation(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_CrowdControlResistance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_AttackDamage(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_CritChance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MaxCritChance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_CritMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_AttackSpeed(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_AreaOfEffect(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DotDamage(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DotFrequency(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DotDuration(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_Penetration(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_ArmorPenetration(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_SpellDamage(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageVsWar(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageVsDemonic(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageVsDeath(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageVsPestilence(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageVsFamine(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageVsCelestial(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageVsChaos(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageVsVoid(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MovementSpeed(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_CooldownReduction(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_CooldownSkipChance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_BleedOnCritChance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageTaken(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageOverTimeTaken(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DebuffDamageSuppressed(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_RetaliationRadiusMetres(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_RetaliationLeeches(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DebuffDurationTaken(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DebuffsDoNotExpire(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_NovaDamageOfMissingHealth(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_AuraDebuffDuration(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DebuffSpreadChance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DeathSpreadChance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageVsSharedDebuff(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MagicFind(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_LootQuantity(const FGameplayAttributeData& OldValue);
};
