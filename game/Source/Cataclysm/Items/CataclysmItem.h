// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "CataclysmItem.generated.h"

struct FCataclysmAffixRow;
struct FCataclysmItemBaseRow;
class UDataTable;

/**
 * The eight item rarities, weakest first.
 *
 * RARITY IS NOT STORED ON AN ITEM. It is a label for what fills the item's four
 * slots, so it is computed rather than set: an item carrying an enchantment is a
 * Legendary, and one carrying three regular affixes is a Superb. See
 * UCataclysmItemValues::RarityOf.
 */
UENUM(BlueprintType)
enum class ECataclysmRarity : uint8
{
	Everyday	UMETA(DisplayName = "Everyday"),
	Quality		UMETA(DisplayName = "Quality"),
	Superb		UMETA(DisplayName = "Superb"),
	Masterful	UMETA(DisplayName = "Masterful"),
	Legendary	UMETA(DisplayName = "Legendary"),
	Mythical	UMETA(DisplayName = "Mythical"),
	Ascendant	UMETA(DisplayName = "Ascendant"),
	Cataclysmic	UMETA(DisplayName = "Cataclysmic"),
};

/**
 * One affix as it sits on a particular item.
 *
 * The affix itself -- what stat it grants, whether it is flat or increased, its
 * top value and which slots it may roll on -- lives in game/Data/Affixes.csv.
 * What belongs to the item is only which affix it got, at what tier, and where
 * in that tier's band it landed.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmRolledAffix
{
	GENERATED_BODY()

	/** Row name in the Affixes table. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Item")
	FName Affix;

	/** 1 to 7. Tier N is worth N sevenths of the affix's top value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Item")
	int32 Tier = 1;

	/**
	 * Where in the tier's band the roll landed. 0 is the floor, 1 is the top.
	 *
	 * Stored rather than the resolved number, because the value also depends on
	 * the piece's upgrade level and on whether the piece is a two-handed weapon,
	 * both of which change after the item exists.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Item")
	float Roll = 1.0f;

	/**
	 * Which damage types a resistance affix covers. Empty for every other kind.
	 *
	 * A resistance family says how MANY types it covers and not which: the
	 * single-resistance family covers one, and which one is decided when the
	 * item drops. So the choice belongs to the item rather than to the affix.
	 * Left empty on a family covering all eight, since there is no choice to
	 * make.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Item")
	TArray<FName> DamageTypes;
};

/**
 * One item: which base it is, how far it has been upgraded, and what it rolled.
 *
 * WHAT IS NOT HERE. Rarity, because it is computed from the contents. The affix
 * values, because they depend on the upgrade level and the base. Which gem sits
 * in a socket, which is issue #46, and which enchantments a piece carries, which
 * is issue #45 -- only the COUNT of enchantments is here, because rarity cannot
 * be computed without it.
 *
 * EVERY FIELD ON THIS STRUCT AND ON FCataclysmRolledAffix IS MARKED `SaveGame`,
 * and it has to be, one field at a time. An item is persisted inside a character
 * record, and `FCataclysmSaveStorage` converts only properties carrying that flag
 * -- a rule it applies all the way down, into structs held by structs. A field
 * added here without the flag is dropped from every save with no error and no
 * warning, and the item comes back with that value at its default. Issue #529 and
 * `docs/Save_System_Design.md` section 4.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmItem
{
	GENERATED_BODY()

	/** Row name in the ItemBases table. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Item")
	FName Base;

	/** The +0 to +10 upgrade level. It multiplies every affix and implicit on
	 *  the piece, by about 3.52 at +10. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Item")
	int32 GearLevel = 0;

	/**
	 * The damage types this weapon carries. Empty on anything but a weapon.
	 *
	 * THE WEAPON'S OWN, NOT AN AFFIX'S. FCataclysmRolledAffix above also has
	 * a DamageTypes array and it is a different thing: that one says which
	 * types a resistance affix covers. This one is what the weapon itself
	 * grants, and it is what unlocks class trees and skills.
	 *
	 * ROLLED WHEN THE ITEM DROPS and never afterwards, from one type up to
	 * the lower of the base's MaxDamageTypes, the difficulty tier, and how
	 * many types that weapon type has at all.
	 * UCataclysmDropRoll::RollDamageTypes does it and carries the design
	 * document's wording for the rule.
	 *
	 * NOTHING READS IT YET, AND THAT IS THE REST OF ISSUE #857.
	 * UCataclysmWeaponSlotsComponent still picks a character's skills from a
	 * single damage type held on the component as a stand-in. Moving that
	 * onto the worn weapon needs a rule for which six of many skills fill the
	 * slots, and that chooser is issue #837.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Item")
	TArray<FName> DamageTypes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Item")
	TArray<FCataclysmRolledAffix> Affixes;

	/**
	 * How many of the four slots hold an enchantment rather than an affix.
	 *
	 * The enchantments themselves are not modelled yet. The count is, because
	 * an item carrying one is a Legendary and rarity cannot be read without it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Item")
	int32 EnchantmentCount = 0;

	/**
	 * How many gem sockets the piece has, from none up to its base's maximum.
	 *
	 * THE COUNT, NOT WHAT IS IN THEM. Which gem sits in a socket is issue #46.
	 * A drop rolls this uniformly, so a piece can arrive with none, and the Add
	 * Socket craft only has something to do because of that.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Item")
	int32 Sockets = 0;

	/**
	 * The Cataclysmic Residue the piece carries.
	 *
	 * A COST, NEVER A BENEFIT. It raises what crafting this item charges in gold
	 * and days, and it counts toward the Worn Residue that can get a character
	 * hunted by a corrupted copy of itself. An item carries some from the moment
	 * it drops, in a band set by its rarity.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Item")
	float Residue = 0.0f;
};

/**
 * What an item is worth, ported from `sim/cataclysm_sim/affixes.py`.
 *
 * Static functions over plain numbers, like UCataclysmDamageCalculation and
 * UCataclysmStatPipeline, so every curve can be tested by passing values in.
 *
 * THREE THINGS MULTIPLY AN AFFIX, and they are independent of each other:
 *
 *   TIER      seven of them, on a linear curve. Tier N is worth N sevenths of
 *             the affix's top value. Linear rather than front-loaded, because a
 *             front-loaded curve hands over most of an affix's value in the
 *             first few tiers and makes the later ones easy to skip.
 *
 *   ROLL      every tier is a range reaching 25% below its top, so that the
 *             crafting materials which reroll and perfect a value have
 *             something to do. Bands overlap by exactly one tier: a perfect T6
 *             roll can beat a poor T7 one, and can never beat a T5.
 *
 *   UPGRADE   the piece's +0 to +10 level multiplies everything on it, by about
 *             3.52 at +10. Every affix value stated in the design document is
 *             the +10 figure, so a +0 piece gives a fraction of it.
 *
 * A two-handed weapon then doubles the result, which is what balances its four
 * affix slots against the eight a dual wielder holds.
 */
UCLASS()
class CATACLYSM_API UCataclysmItemValues : public UObject
{
	GENERATED_BODY()

public:
	/** Affixes have seven tiers, because the crafting material that raises them
	 *  levels an affix to the seventh. */
	static constexpr int32 MaxAffixTier = 7;

	/** A piece upgrades from +0 to +10. */
	static constexpr int32 MaxGearLevel = 10;

	/** Four slots, holding regular affixes and enchantments between them. */
	static constexpr int32 SlotsPerPiece = 4;

	/** At most two of the four may be prefixes, and at most two suffixes. */
	static constexpr int32 PrefixesPerPiece = 2;
	static constexpr int32 SuffixesPerPiece = 2;

	/**
	 * How far below its top a tier's band reaches. 0.25 is 25%.
	 *
	 * Without a band the crafting materials that reroll and perfect a value are
	 * dead content, because a tier would have exactly one possible value.
	 */
	static constexpr float RollBandFraction = 0.25f;

	/**
	 * Each upgrade level adds this share. The same constant the Power Score
	 * model uses, rather than a second copy of it, so gear level cannot mean two
	 * different things in two places.
	 */
	static constexpr float GearLevelFactor = 0.25245807054891267f;

	/**
	 * What each half of a hybrid affix is worth, against the whole affix.
	 *
	 * THE DESIGN STATES IT TWICE. `docs/Cataclysm_GDD_v2.md`, on why a hybrid
	 * and one of its halves cannot sit on one piece: "A hybrid grants each half
	 * at 70%". And the Hybrid Affixes section: "A hybrid is worth 1.4 affixes
	 * spread across two stats, where a single affix is worth 1.0 concentrated in
	 * one. So it wins a slot when a build needs both and loses when it needs one
	 * badly."
	 *
	 * IT IS DERIVED RATHER THAN CHOSEN, and `sim/cataclysm_sim/affixes.py` says
	 * why: it is the ratio the project owner already set between the
	 * two-resistance affix and the single-resistance one, 14 against 20. Both
	 * rows are in game/Data/Affixes.csv, and
	 * Cataclysm.Items.TheHybridShareIsTheResistanceRatio reads them and fails if
	 * this constant stops matching, rather than the two quietly drifting.
	 */
	static constexpr float HybridFraction = 0.7f;

	/**
	 * What a two-handed weapon multiplies its own implicits and affixes by.
	 *
	 * DERIVED, NOT CHOSEN. Two one-handed weapons hold eight affix slots against
	 * a two-hander's four, so 2 is the figure that makes the two loadouts worth
	 * the same in affixes -- which the design requires, because the Power Score
	 * model counts two one-handed weapons as one equipped piece so that dual
	 * wielding is not worth free power.
	 *
	 * It reaches the implicits as well as the affixes. Without that the
	 * two-hander loses outright: two one-handed bases SUM their damage, so an
	 * Axe and a Sword give 86 against a Greatsword's stated 78.
	 */
	static constexpr float TwoHandedMultiplier = 2.0f;

	/** What a piece at this upgrade level does to everything it carries. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static float GearLevelMultiplier(int32 GearLevel);

	/** Tier N is worth N sevenths of an affix's top value. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static float TierFraction(int32 Tier);

	/** The lowest and highest an affix can roll at a tier, at +10. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static void TierBand(float TopValue, int32 Tier, float& OutLow, float& OutHigh);

	/**
	 * One affix's value: its tier band, where in it the roll landed, the
	 * piece's upgrade level, and whether the piece is a two-handed weapon.
	 *
	 * The stated top values are the +10 figures, so this divides by the +10
	 * multiplier before applying the piece's own.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static float AffixValue(float TopValue, int32 Tier, float Roll,
							int32 GearLevel, bool bTwoHanded = false);

	/** An implicit's value. It does not roll, so it has no tier and no band. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static float ImplicitValue(float StatedValue, int32 GearLevel,
							   bool bTwoHanded = false);

	/**
	 * What an item carrying this many enchantments and affixes IS.
	 *
	 * The definition of rarity, not a lookup on a stored field. Returns false
	 * for a combination no item can have -- an enchantment with slots left
	 * empty, or more than four filled.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static bool RarityOf(int32 EnchantmentCount, int32 AffixCount,
						 ECataclysmRarity& OutRarity);

	/** How many regular affixes a piece of this rarity carries. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static int32 AffixSlotsFor(ECataclysmRarity Rarity);

	/** How many of the four slots hold an enchantment. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static int32 EnchantmentsFor(ECataclysmRarity Rarity);

	/** A slot count divided into prefixes and suffixes, at its most even. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static void PrefixSuffixSplit(int32 Slots, int32& OutPrefixes,
								  int32& OutSuffixes);
};

/**
 * Turning an item into the modifiers a character's stats are computed from.
 *
 * Separated from the value curves above because this one needs the data tables
 * and those need nothing. Every function here takes the tables as arguments
 * rather than finding them, so a test can build its own.
 */
UCLASS()
class CATACLYSM_API UCataclysmItemModifiers : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Every modifier one item contributes, KEYED BY THE STAT IT AFFECTS.
	 *
	 * Keyed rather than returned as one flat list because
	 * UCataclysmStatPipeline::Evaluate answers "what is this ONE stat worth",
	 * so it needs the modifiers for that stat and no others. A flat list would
	 * have to be filtered by something the modifier does not carry.
	 *
	 * Stat names are the character sheet's own, matching
	 * `sim/cataclysm_sim/character.py`: `max_health`, `armor`,
	 * `resistance_demonic` and so on.
	 *
	 * A RESISTANCE FAMILY BECOMES ONE MODIFIER PER DAMAGE TYPE. The affix says
	 * how many types it covers; the item says which. A family covering all eight
	 * needs no choice, so an empty list on the item means all of them.
	 *
	 * AN ITEM NEVER PRODUCES A "MORE" MULTIPLIER. Affixes and implicits are
	 * flat or increased; the multiplicative bucket belongs to gems, passive
	 * keystones and enchantments. The pipeline enforces that too, but producing
	 * one here would be caught only as a warning at evaluation time.
	 */
	static TMap<FName, TArray<FCataclysmStatModifier>> ModifiersFor(
		const FCataclysmItem& Item,
		const UDataTable* BaseTable,
		const UDataTable* AffixTable);

	/** Merge one item's modifiers into a character's running totals. */
	static void AccumulateInto(TMap<FName, TArray<FCataclysmStatModifier>>& Totals,
							   const FCataclysmItem& Item,
							   const UDataTable* BaseTable,
							   const UDataTable* AffixTable);

	/** The eight damage types, in the order the design document lists them. */
	static const TArray<FName>& DamageTypeNames();

	/** The character sheet stat holding resistance to a damage type. */
	static FName ResistanceStatFor(FName DamageType);

	/** The rarity an item is, from what fills its slots. */
	static bool RarityOfItem(const FCataclysmItem& Item,
							 ECataclysmRarity& OutRarity);

	/** Whether the base is a two-handed weapon, which doubles its values. */
	static bool IsTwoHanded(const FCataclysmItem& Item,
							const UDataTable* BaseTable);

	/**
	 * The Affixes table row whose AffixName is this, or null.
	 *
	 * BY DISPLAY NAME RATHER THAN BY ROW KEY, because a hybrid affix names its
	 * two halves as the affix names they are ("Flat magic find") and the row key
	 * is decorated with the kind ("Stat_Flat_magic_find").
	 *
	 * SHARED RATHER THAN COPIED, and that is not tidiness. It lived in an
	 * anonymous namespace in CataclysmDropRoll.cpp, and a second copy in another
	 * file of this module would compile alone and collide the moment both files
	 * were clean at once, because Unreal's unity build merges them and excludes
	 * only the files you have modified. That has broken `development` once
	 * already.
	 */
	static const FCataclysmAffixRow* AffixNamed(const UDataTable* AffixTable,
												const FString& AffixName);

	/** The stat name a weapon's own damage is carried under, in every sheet. */
	static const TCHAR* AttackDamageStat;

	/**
	 * The stat name a swing rate is carried under, in every sheet.
	 *
	 * NOT WHERE A WEAPON'S OWN RATE LIVES, unlike the damage above. A base
	 * states its rate in its own AttackSpeed column; this name is what an
	 * INCREASE to that rate is carried under, whether it comes from an affix or
	 * from a base's implicit. A Sword carries `attack_speed increased 5` as its
	 * second implicit, for example.
	 */
	static const TCHAR* AttackSpeedStat;

	/** Where the imported item base table lives. */
	static const TCHAR* BaseTableAssetPath;

	/** The item base table, or null with the reason logged. */
	static const UDataTable* LoadBaseTable();

	/**
	 * What a weapon of this TYPE supplies as attack damage.
	 *
	 * BY TYPE RATHER THAN BY BASE NAME, because that is what the game currently
	 * has to ask with. UCataclysmWeaponSlotsComponent equips a weapon TYPE --
	 * "Greataxe" -- rather than a rolled item, since items that carry a rolled
	 * damage type and a gear level do not exist yet. Every other function on
	 * this class takes an FCataclysmItem and should be preferred once they do.
	 *
	 * THE TWO-HANDED MULTIPLIER IS APPLIED HERE, and leaving it out would be a
	 * silent halving. The sheets state a Greataxe at 72 and that is the figure
	 * BEFORE doubling, so a Greataxe supplies 144 at gear level 10 and about 41
	 * at gear level 0. FCataclysmItemBaseRow says so, and TwoHandedMultiplier
	 * explains why two-handers double.
	 *
	 * @param GearLevel  0 to 10. The stated values are the +10 figures.
	 * @return 0 when the table is missing, the type is not a weapon, or the
	 *         base carries no attack damage implicit
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static float WeaponDamageForType(const UDataTable* BaseTable,
									 const FString& WeaponType,
									 int32 GearLevel);

	/**
	 * How many times a second a weapon type swings.
	 *
	 * NOT AN IMPLICIT, UNLIKE THE DAMAGE ABOVE, which is why this reads its own
	 * column rather than searching the two implicit slots. A weapon's rate is an
	 * intrinsic property of the base, listed apart from its modifiers -- Path of
	 * Exile and Last Epoch both treat it that way, and FCataclysmItemBaseRow
	 * says so on the field itself.
	 *
	 * NO GEAR LEVEL, ALSO UNLIKE THE DAMAGE. A weapon does not swing faster for
	 * being a better example of itself. Increased attack speed comes from
	 * affixes, which multiply this base -- and multiplied nothing until issue
	 * #647, because nothing ever wrote this onto a character.
	 *
	 * @return 0 when the table is missing or the type is not a weapon. Zero
	 *         means "no rate at all", which the automatic basic attack reads as
	 *         never swinging rather than as swinging infinitely fast.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static float WeaponAttackSpeedForType(const UDataTable* BaseTable,
										  const FString& WeaponType);

	// -- what the weapons actually worn are worth. Issue #840 --------------
	//
	// PREFER THESE OVER THE TWO BY TYPE ABOVE. Those take a weapon TYPE because
	// that is all the game could ask with when they were written, and their own
	// header says every function taking an FCataclysmItem should be preferred
	// once rolled items exist. They exist, since issues #828 and #830, and a
	// type cannot answer at what upgrade level the weapon is held, nor which of
	// two worn weapons is meant.

	/**
	 * What one worn weapon supplies as attack damage, at its own upgrade level.
	 *
	 * THE UPGRADE LEVEL COMES FROM THE ITEM, which is the whole difference from
	 * WeaponDamageForType. That function is given a level by its caller, and
	 * UCataclysmWeaponSlotsComponent passed 0 every time because nothing ever
	 * set it, so a worn +5 whip was computed as a +0 whip. Issue #840.
	 *
	 * IMPLICITS ONLY, NOT AFFIXES, matching `weapon_base_damage` in
	 * `sim/cataclysm_sim/player_damage.py`, which is the model these numbers
	 * were tuned against. A weapon's attack damage AFFIXES are a separate
	 * question and are not answered here or anywhere else yet.
	 *
	 * @return 0 when the table is missing, the base is not in it, or the base
	 *         carries no attack damage implicit. A Shield returns 0.
	 */
	static float WeaponDamageForItem(const FCataclysmItem& Item,
									 const UDataTable* BaseTable);

	/**
	 * Whether a worn weapon supplies any attack damage at all.
	 *
	 * A SHIELD DOES NOT, and it is the only weapon today that does not. The rule
	 * reads the base's own implicits rather than naming the Shield, so another
	 * weapon of the same kind needs no change here. `armed_weapons_in` in
	 * `sim/cataclysm_sim/player_damage.py` says the same thing the same way:
	 * a weapon granting no damage contributes nothing to the basic attack,
	 * neither damage nor swing rate.
	 */
	static bool WeaponIsArmed(const FCataclysmItem& Item,
							  const UDataTable* BaseTable);

	/**
	 * The attack damage a character's worn weapons supply between them. SUMMED.
	 *
	 * THE DESIGN SAYS SUM, and this is the code that makes it true. From the
	 * Dual Wielding section of `docs/Cataclysm_GDD_v2.md`: "this game blends two
	 * weapons into one swing, summing their base damage and averaging their
	 * attack speed, so there is no first weapon for the swing to belong to."
	 * Before issue #840 the game answered with one weapon's damage, taken from
	 * whichever weapon slot happened to be occupied first, so a second weapon
	 * changed nothing at all.
	 *
	 * Weapons supplying no damage are skipped, so a weapon with a Shield is
	 * worth the weapon.
	 */
	static float BlendedWeaponDamage(const TArray<FCataclysmItem>& Weapons,
									 const UDataTable* BaseTable);

	/**
	 * The rate a character's worn weapons swing at. AVERAGED.
	 *
	 * AVERAGED RATHER THAN SUMMED, AND THAT ASYMMETRY IS THE POINT. It is what
	 * stops summing damage being a strict advantage: a character holding two
	 * weapons deals more per swing than either alone but does not also swing at
	 * the faster weapon's rate. `attack_speed_of` in
	 * `sim/cataclysm_sim/affixes.py` records that both Path of Exile and Last
	 * Epoch resolve it the same way, Path of Exile by alternating hands.
	 *
	 * AVERAGED OVER THE WEAPONS THAT ARM, so a weapon held with a Shield swings
	 * at its own rate rather than at the mean of itself and a shield that is
	 * never swung.
	 *
	 * NOT MULTIPLIED BY ANYTHING. The two-handed multiplier applies to a
	 * weapon's implicits and affixes, and a rate is neither.
	 *
	 * @return 0 when nothing armed is worn, which the automatic basic attack
	 *         reads as never swinging rather than as swinging infinitely fast.
	 */
	static float BlendedAttackSpeed(const TArray<FCataclysmItem>& Weapons,
									const UDataTable* BaseTable);
};
