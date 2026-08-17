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
	UFUNCTION() void OnRep_MagicFind(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_LootQuantity(const FGameplayAttributeData& OldValue);
};
