// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmPlayerClassStats.h"
#include "Cataclysm.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
// For the two damage-taken stat names and their base of 100. Issue #1026.
#include "AbilitySystem/CataclysmDamageCalculation.h"
// For the base of The Breaking Point's conversion window. Issue #1025.
#include "AbilitySystem/CataclysmDamageConversion.h"
// For the per-cast Fervour stat name. Issue #1051.
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
// For the names of the three regeneration rates, shared with the code that asks
// for them rather than spelled a second time here. Issue #1038.
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
// For the three retaliation stat names, shared with the code that reads them
// rather than spelled a second time here. Issues #1047 and #1048.
#include "AbilitySystem/CataclysmRetaliation.h"
// For the stat naming how long a lasting effect on the character runs.
// Issue #1033.
#include "AbilitySystem/CataclysmDebuffs.h"
// For the nova stat name. Issue #1050.
#include "AbilitySystem/CataclysmNova.h"
// For the flag saying a character's skills cost no health. Issue #1051.
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmClassStats.h"
#include "Items/CataclysmItem.h"
#include "Engine/DataTable.h"
#include "HAL/IConsoleManager.h"

const TCHAR* UCataclysmPlayerClassStats::ClassStatsAssetPath =
	TEXT("/Game/Data/DT_ClassStats.DT_ClassStats");

const TCHAR* UCataclysmPlayerClassStats::AttributesAssetPath =
	TEXT("/Game/Data/DT_Attributes.DT_Attributes");

const FString UCataclysmPlayerClassStats::StartingClassName = TEXT("Ravager");

namespace
{
	/**
	 * Which class stat line the player starts on.
	 *
	 * A STRING RATHER THAN AN INDEX, because the row names in
	 * game/Data/ClassStats.csv are what UCataclysmClassStats::BaseFor takes and
	 * an index would be a second ordering that could disagree with the table.
	 *
	 * IT WAS `Default` UNTIL 2026-08-24 AND THAT MADE THE GAME UNPLAYABLE.
	 * `Default` is the shared line every class inherits from, and it carries no
	 * defensive layer at all -- no armour, no resistance, no block, no flat
	 * damage reduction and no leech. There is no class selection screen, so
	 * every character anybody has ever pressed Play on sat on it.
	 *
	 * Measured for issue #806: against the pack of ten Imps the sandbox places,
	 * at the level below, a character on the shared line dies with four Imps
	 * still standing even while using every skill on cooldown. All three real
	 * classes survive the same fight. That is the whole of the difference
	 * between the game being playable and not.
	 *
	 * THE RAVAGER RATHER THAN ONE OF THE OTHER TWO, because a character starts
	 * holding a two-handed Greataxe and the design calls the Ravager "a
	 * frontline aggressor... the most armor of the three, flat damage
	 * reduction, enough leech to hold a line". The starting weapon and the
	 * starting class should not disagree about what the character is.
	 *
	 * `tools/tests/test_the_starting_character_survives_a_pack.py` measures the
	 * fight and fails if this name is changed to a line that cannot win it.
	 */
	// StartingClassName IS DEFINED ABOVE THIS BLOCK ON PURPOSE. Non-local
	// statics in one translation unit are initialised in definition order, so
	// naming it here rather than repeating the string cannot read an empty
	// FString, and the console variable's default and ChosenClass's fallback
	// cannot drift apart.
	TAutoConsoleVariable<FString> CVarPlayerClass(
		TEXT("Cataclysm.PlayerClass"),
		UCataclysmPlayerClassStats::StartingClassName,
		TEXT("Which row of game/Data/ClassStats.csv the player starts on: "
			 "Default, Ravager, Ritualist or Masochist. Default is the shared "
			 "line every class inherits and has no defences of its own. Read "
			 "when the pawn is possessed, so press Play again after changing "
			 "it."),
		ECVF_Default);

	/**
	 * Which level the player's stat line is resolved at.
	 *
	 * IT IS A STAND-IN FOR A LEVELLING SYSTEM THAT DOES NOT EXIST. Nothing in
	 * this project grants experience or raises a level, so without this the
	 * PerLevel column of the class table would never be read by the game at all.
	 */
	TAutoConsoleVariable<int32> CVarPlayerLevel(
		TEXT("Cataclysm.PlayerLevel"),
		UCataclysmPlayerClassStats::DefaultLevel,
		TEXT("The level the player's class stat line is resolved at, 1 to 100. "
			 "A stand-in until levelling exists. Read when the pawn is "
			 "possessed, so press Play again after changing it."),
		ECVF_Default);

	/**
	 * Kept alive deliberately, the same way the damage type colour table is.
	 * Nothing else references the asset, so garbage collection would otherwise
	 * be free to take it back and the next possession would pay the load again.
	 */
	TWeakObjectPtr<const UDataTable> CachedClassStats;

	/** The same, for the table saying what one attribute point is worth. */
	TWeakObjectPtr<const UDataTable> CachedAttributes;
}

const UDataTable* UCataclysmPlayerClassStats::LoadTable()
{
	if (CachedClassStats.IsValid())
	{
		return CachedClassStats.Get();
	}

	const UDataTable* Table = LoadObject<UDataTable>(nullptr, ClassStatsAssetPath);
	if (Table)
	{
		const_cast<UDataTable*>(Table)->AddToRoot();
		CachedClassStats = Table;
	}
	return Table;
}

void UCataclysmPlayerClassStats::MergeAttributeBases(
	const FCataclysmAttributePoints& Points, TMap<FName, float>& Bases)
{
	for (const FString& Name : FCataclysmAttributePoints::Names())
	{
		Bases.Add(FName(*Name), static_cast<float>(Points.PointsIn(Name)));
	}
}

const UDataTable* UCataclysmPlayerClassStats::LoadAttributeTable()
{
	if (CachedAttributes.IsValid())
	{
		return CachedAttributes.Get();
	}

	const UDataTable* Table = LoadObject<UDataTable>(nullptr, AttributesAssetPath);
	if (!Table)
	{
		// LOUD, BECAUSE THE SYMPTOM IS SILENT. Without this table every one of
		// the eight attributes still resolves and still gets written, and every
		// stat they scale simply comes out at its base -- a character that
		// spent a hundred points and shows nothing for them. Issue #897 is what
		// that looked like when the wiring was missing instead of the asset.
		UE_LOG(LogCataclysm, Warning,
			TEXT("%s could not be loaded, so attribute points will contribute "
				 "nothing to any stat. It is generated from the Attributes "
				 "sheet into game/Data/Attributes.csv."), AttributesAssetPath);
		return nullptr;
	}

	const_cast<UDataTable*>(Table)->AddToRoot();
	CachedAttributes = Table;
	return Table;
}

const TMap<FString, FGameplayAttribute>&
UCataclysmPlayerClassStats::StatToAttribute()
{
	// BUILT ONCE ON FIRST USE RATHER THAN AS A FILE-SCOPE STATIC. An
	// FGameplayAttribute wraps an FProperty found by reflection, and the
	// reflection data for these attribute sets is not ready during static
	// initialisation.
	static const TMap<FString, FGameplayAttribute> Map = []
	{
		using Vital = UCataclysmVitalAttributeSet;
		using Combat = UCataclysmCombatAttributeSet;
		using Resource = UCataclysmClassResourceAttributeSet;
		using Resistance = UCataclysmResistanceAttributeSet;
		using Primary = UCataclysmPrimaryAttributeSet;

		return TMap<FString, FGameplayAttribute>{
			// THE EIGHT PRIMARY ATTRIBUTES, ADDED FOR ISSUES #50 AND #897.
			// The attribute set existed and every player carried it, the table
			// saying what a point is worth existed, and the arithmetic existed
			// -- and nothing wrote any of the eight, so the eight affixes that
			// increase them printed a number on a tool tip and changed nothing.
			//
			// THEY ARE RESOLVED BEFORE EVERY OTHER STAT, and ApplyTo does that
			// deliberately rather than relying on this map's order, which is a
			// hash order and says nothing. An attribute has to be finished
			// before the sixteen stats it scales can be started.
			{TEXT("agility"), Primary::GetAgilityAttribute()},
			{TEXT("ferocity"), Primary::GetFerocityAttribute()},
			{TEXT("constitution"), Primary::GetConstitutionAttribute()},
			{TEXT("vitality"), Primary::GetVitalityAttribute()},
			{TEXT("mind"), Primary::GetMindAttribute()},
			{TEXT("spirit"), Primary::GetSpiritAttribute()},
			{TEXT("efficacy"), Primary::GetEfficacyAttribute()},
			{TEXT("luck"), Primary::GetLuckAttribute()},

			// The pools and what refills them.
			{TEXT("max_health"), Vital::GetMaxHealthAttribute()},
			{TEXT("max_mana"), Vital::GetMaxManaAttribute()},
			{TEXT("max_energy_shield"), Vital::GetMaxEnergyShieldAttribute()},
			// THE THREE RATES, UNDER THE NAMES THEIR READER USES. Issue #1038.
			// `UCataclysmRegeneration::ApplyStep` asks for each of these by name
			// rather than reading the attribute, so that a bonus carrying a
			// condition or a scale reaches it; a name written twice and spelled
			// differently would fall back to the attribute in silence, which is
			// the argument `UCataclysmItemModifiers::AttackDamageStat` makes.
			{FString(UCataclysmRegeneration::HealthRegenStat),
			 Vital::GetHealthRegenAttribute()},
			{FString(UCataclysmRegeneration::ManaRegenStat),
			 Vital::GetManaRegenAttribute()},
			{FString(UCataclysmRegeneration::EnergyShieldRegenStat),
			 Vital::GetEnergyShieldRegenAttribute()},
			{TEXT("life_leech"), Vital::GetLifeLeechAttribute()},

			// HOW FAR HEALING MAY TAKE THIS CHARACTER, as a reduction of the
			// ceiling. Issue #988. Zero for every class, and the Masochist's
			// Point of No Return keystone is its only source. Here for the
			// same reason every other node-supplied stat is: a passive node
			// granting a stat this map does not name grants nothing and
			// reports nothing.
			{TEXT("healing_ceiling_reduction"),
			 Vital::GetHealingCeilingReductionAttribute()},

			// THE OTHER TWO LEECHES JOINED IT IN ISSUE #895. Life leech had an
			// entry here and the other two did not, so two of the three affixes
			// were dropped before they reached an attribute. All three then
			// reached one nothing read, which is what that issue was about.
			//
			// NO CLASS LINE NAMES EITHER. game/Data/ClassStats.csv gives the
			// Ravager 3% life leech and no class any mana or energy shield
			// leech, so gear is the only source of those two.
			{TEXT("mana_leech"), Vital::GetManaLeechAttribute()},
			{TEXT("energy_shield_leech"),
			 Vital::GetEnergyShieldLeechAttribute()},

			// The class's own resource. Its maximum only -- see the header.
			{TEXT("class_resource"), Resource::GetMaxClassResourceAttribute()},

			// THE THREE RATES THAT MOVE FERVOUR. Issue #954, and they are here
			// for the reason every other entry is: a passive node granting a
			// stat with no attribute behind it grants nothing and reports
			// nothing. `ApplyTo` loops over THIS map, so a stat missing from it
			// is dropped before it reaches a character.
			//
			// A BASE OF ZERO FOR EVERY CLASS, from `game/Data/ClassStats.csv`,
			// and that is the design rather than a gap. A character gains no
			// Fervour and loses none until it spends a point on a tree's
			// generator node -- the Masochist's grants 1 to each of the three --
			// which is what makes that node worth a point.
			{TEXT("fervour_from_damage"),
			 Resource::GetFervourFromDamageAttribute()},
			{TEXT("fervour_from_cost"),
			 Resource::GetFervourFromCostAttribute()},
			{TEXT("fervour_lost_to_healing"),
			 Resource::GetFervourLostToHealingAttribute()},

			// WHAT EVERY SKILL COSTS IN HEALTH BEYOND ITS OWN COST. Issue #970,
			// and here for the same reason as the three rates above: a passive
			// node granting a stat this map does not name grants nothing and
			// reports nothing. The Masochist's Deeper Cuts node is its only
			// source, and it is zero for every class until a point is spent
			// there.
			{TEXT("added_health_cost"),
			 Resource::GetAddedHealthCostAttribute()},

			// AND THE SAME THING MEASURED AGAINST CURRENT HEALTH INSTEAD.
			// Issue #986. Two stats and not one, because a share of current
			// health cannot kill and a share of maximum health can; the
			// project owner drew that line and `docs/DECISIONS.md` records
			// it. The Masochist's Exsanguinate keystone is its only source.
			{TEXT("added_health_cost_of_current"),
			 Resource::GetAddedHealthCostOfCurrentAttribute()},

			// AND WHAT SHARE OF A COST IS TAKEN LATER RATHER THAN NOW.
			// Issue #991. Zero for every class, and the Masochist's Deferred
			// Payment node is its only source. Here for the same reason every
			// other node-supplied stat is: a passive node granting a stat this
			// map does not name grants nothing and reports nothing.
			{TEXT("deferred_health_cost_share"),
			 Resource::GetDeferredHealthCostShareAttribute()},

			// AND HOW FAR ONE FURTHER PAYMENT PUSHES A DEBT OUT, in seconds.
			// Issue #995. Zero for every class, and the Masochist's Rolling
			// Debt node is its only source.
			{TEXT("health_debt_delay_extension"),
			 Resource::GetHealthDebtDelayExtensionAttribute()},

			// AND WHETHER A DEBT IS NEVER TAKEN ON A TIMER AT ALL. Issue #997.
			// Zero for every class, and the Masochist's The Reckoning keystone
			// is its only source.
			{TEXT("health_debt_cleared_only_by_a_kill"),
			 Resource::GetHealthDebtClearedOnlyByAKillAttribute()},

			// AND WHETHER HEALING STOPS REMOVING FERVOUR AT ALL. Issue #1006.
			// Zero for every class. Two Masochist keystones set it, for two
			// different kinds of healing, and the row's required tags are what
			// tell those apart.
			{TEXT("fervour_loss_suppressed"),
			 Resource::GetFervourLossSuppressedAttribute()},

			// AND HOW MUCH FERVOUR ARRIVES EVERY SECOND FROM NOTHING HAPPENING.
			// Issue #1008. Zero for every class, and the Masochist's Low Life
			// keystone is its only source.
			{TEXT("fervour_per_second"),
			 Resource::GetFervourPerSecondAttribute()},

			// AND BOTH HALVES OF THE LAST DROP. Issue #1051. Zero for every class,
			// and the first option of the Masochist's The Final Vow is the only
			// source of either. Both rows carry a health condition, so both
			// attributes stay at zero even for a character holding the option and
			// both are asked for through the stat pipeline rather than read. The
			// entries are still needed, because `ApplyTo` loops over THIS map and a
			// stat missing from it is dropped before `StatForSkill` could be asked.
			{FString(UCataclysmFervour::PerCastStat),
			 Resource::GetFervourPerCastAttribute()},
			{FString(UCataclysmSkillTemplate::HealthCostSuppressedStat),
			 Resource::GetHealthCostSuppressedAttribute()},

			// AND WHETHER DROPPING BELOW HALF HEALTH TURNS DAMAGE INTO BLEEDING,
			// with how long one turn of that lasts beside it. Issue #985. The
			// Masochist's The Breaking Point is the flag's only source; the
			// window has a base of 3 seconds on the Masochist's own class stat
			// line, because "lasts 3 seconds, increased by 5% per point" is a
			// base plus an increase and an increase needs something under it.
			{TEXT("damage_to_bleeding_on_low_health"),
			 Resource::GetDamageToBleedingOnLowHealthAttribute()},
			{TEXT("damage_to_bleeding_window"),
			 Resource::GetDamageToBleedingWindowAttribute()},

			// What keeps a hit from landing in full.
			{TEXT("armor"), Combat::GetArmorAttribute()},

			// EVASION AND BLOCK, WHICH NO CLASS LINE NAMES AND GEAR SUPPLIES
			// ENTIRELY. UCataclysmDamageCalculation rolls both -- evasion at
			// line 278 and a block at line 323 -- and until issue #894 neither
			// attribute could ever hold anything but the zero it starts at,
			// because the four affixes granting them were dropped here.
			//
			// A ZERO BASE IS THE DESIGN AND NOT A GAP. The design document says
			// a class "does not need a base above zero for every stat" and that
			// a class with no base evasion is "how a class declines to care
			// about a stat". An INCREASED evasion affix on a character with no
			// flat evasion therefore still grants nothing, which is the
			// pipeline working rather than this entry failing.
			{TEXT("evasion"), Combat::GetEvasionAttribute()},
			{TEXT("block_chance"), Combat::GetBlockChanceAttribute()},
			{TEXT("damage_reduction"), Combat::GetDamageReductionAttribute()},
			{TEXT("crowd_control_resistance"),
			 Combat::GetCrowdControlResistanceAttribute()},
			{FString(UCataclysmRetaliation::AmountStat),
			 Combat::GetRetaliationAttribute()},

			// AND HOW FAR THAT REACHES, AND WHETHER IT LEECHES. Issues #1047 and
			// #1048. Zero for every class, and one capstone option is the only
			// source of each: The First Vow's Reprisal Wave and The Second Vow's
			// Feeding Wound. Here for the reason every other node-supplied stat
			// is: `ApplyTo` loops over this map, so a stat missing from it is
			// dropped before it reaches a character.
			//
			// A RADIUS IN METRES AND A FLAG. Both rows carry no condition and no
			// scale, so both ARE folded into their attributes, and
			// `UCataclysmRetaliation` asking for the stat and reading the
			// attribute give the same answer today. It asks anyway, so that a
			// future row carrying a condition works.
			{FString(UCataclysmRetaliation::RadiusMetresStat),
			 Combat::GetRetaliationRadiusMetresAttribute()},
			{FString(UCataclysmRetaliation::LeechesStat),
			 Combat::GetRetaliationLeechesAttribute()},

			// AND WHAT SHARE OF ITS MISSING HEALTH ONE NOVA DEALS. Issue #1050.
			// Zero for every class, and the Masochist's Unstable Aura is its only
			// source. Its row carries a health condition, so the attribute stays at
			// zero even for a character holding the node and `UCataclysmNova` asks
			// for the stat rather than reading it.
			{FString(UCataclysmNova::DamageStat),
			 Combat::GetNovaDamageOfMissingHealthAttribute()},

			// AND HOW LONG A LASTING HARMFUL EFFECT ON THE CHARACTER RUNS. Issue
			// #1033. A hundred for every class, from `EngineSuppliedBases` below,
			// and two Masochist nodes raise it. Here for the reason every other
			// node-supplied stat is: `ApplyTo` loops over this map, so a stat
			// missing from it is dropped before it reaches a character.
			{FString(UCataclysmDebuffs::DurationStat),
			 Combat::GetDebuffDurationTakenAttribute()},

			// THE EIGHT RESISTANCES, AND GEAR IS THE ONLY SOURCE OF ANY OF
			// THEM. The three resistance families in game/Data/Affixes.csv are
			// the only thing in the game that grants resistance and no class
			// line names one, so before issue #894 every hit a player took was
			// resolved against a resistance of zero however much resistance
			// gear they were wearing.
			//
			// THE KEYS MATCH UCataclysmItemModifiers::ResistanceStatFor, which
			// builds "resistance_<type>" in lower case out of the damage type
			// name. A key that stopped matching would go back to dropping the
			// affix in silence, which is what
			// Cataclysm.Equipment.EveryStatAnAffixGrantsHasAnAttributeBehindIt
			// fails on.
			//
			// A PLAYER CARRIES THE PER-TYPE SET AND AN ENEMY CARRIES ONE
			// FIGURE. UCataclysmDamageCalculation reads both and adds them, so
			// writing these eight changes nothing about an enemy, which holds
			// UCataclysmAllResistanceAttributeSet instead and is skipped by the
			// HasAttributeSetForAttribute check below.
			{TEXT("resistance_war"), Resistance::GetWarResistanceAttribute()},
			{TEXT("resistance_demonic"),
			 Resistance::GetDemonicResistanceAttribute()},
			{TEXT("resistance_death"), Resistance::GetDeathResistanceAttribute()},
			{TEXT("resistance_pestilence"),
			 Resistance::GetPestilenceResistanceAttribute()},
			{TEXT("resistance_famine"),
			 Resistance::GetFamineResistanceAttribute()},
			{TEXT("resistance_celestial"),
			 Resistance::GetCelestialResistanceAttribute()},
			{TEXT("resistance_chaos"), Resistance::GetChaosResistanceAttribute()},
			{TEXT("resistance_void"), Resistance::GetVoidResistanceAttribute()},

			// What a hit is worth.
			//
			// THE TWO WEAPON STATS ARE HERE AND NO CLASS LINE NAMES THEM, which
			// is why they were absent until issue #845. game/Data/ClassStats.csv
			// has no attack damage or attack speed column, so BaseFor answers
			// zero for both, and that is the right base rather than a gap:
			//
			//   attack_damage  is entirely supplied by what is worn. A weapon's
			//     damage is an `attack_damage` implicit on its base, so
			//     GatherModifiers already hands it over as a flat modifier. Two
			//     worn weapons therefore SUM without anything here summing them,
			//     a two-handed weapon doubles because ImplicitValue doubles it,
			//     and a Shield contributes nothing because it carries no such
			//     implicit. The design's blend rule falls out of the pipeline.
			//
			//   attack_speed  cannot work that way. A weapon's swing rate is its
			//     own column rather than an implicit, and rates AVERAGE rather
			//     than summing, so a base has to be supplied. That is what the
			//     BaseOverrides argument is for and it is the only stat using it.
			//
			// BEFORE THIS THEY REACHED NOTHING. Flat damage, increased damage and
			// increased attack speed all roll on Gloves, Necklace, Relic, Ring and
			// Weapon, so a player could wear five of each and none of them did
			// anything at all. UCataclysmEquipmentComponent::GatherModifiers
			// gathered them and this loop, being over the attribute map rather
			// than over the modifiers, silently dropped them.
			{TEXT("attack_damage"), Combat::GetAttackDamageAttribute()},
			{TEXT("attack_speed"), Combat::GetAttackSpeedAttribute()},

			// CRITICAL STRIKE CHANCE IS THE SKILL'S BASE SCALED BY GEAR, which
			// is what the design says in as many words: "Critical strike chance
			// belongs to the skill, not the character. Each skill carries its
			// own base chance, and the character's gear and attributes scale
			// it." So the base arrives through BaseOverrides exactly as attack
			// speed does, from UCataclysmEquipmentComponent::
			// StatBasesFromWeapons, and the four affixes that name it are
			// ordinary modifiers on top.
			//
			// UNTIL ISSUE #894 UCataclysmWeaponSlotsComponent WROTE IT INSTEAD,
			// with SetNumericAttributeBase, so a gear affix could not have
			// scaled it even had it arrived: the write replaced the whole value
			// every time a weapon was equipped. That write is gone and this is
			// the only writer, which is the same resolution issue #845 reached
			// for attack damage when two places were writing one attribute.
			{TEXT("crit_chance"), Combat::GetCritChanceAttribute()},
			{TEXT("crit_multiplier"), Combat::GetCritMultiplierAttribute()},

			// Penetration cuts into a defender's resistance, and is read at
			// CataclysmVitalAttributeSet.cpp where the hit is resolved.
			{TEXT("penetration"), Combat::GetPenetrationAttribute()},
			{TEXT("spell_damage"), Combat::GetSpellDamageAttribute()},

			// THE EIGHT CONDITIONAL DAMAGE STATS, one per damage type. Issue
			// #895. Each applies only when the target IS that type: "They read
			// the target, not the weapon. An enemy has a damage type of its
			// own, which is its Cataclysm's." No class line names any of the
			// eight, so gear is their only source.
			{TEXT("damage_vs_war"), Combat::GetDamageVsWarAttribute()},
			{TEXT("damage_vs_demonic"), Combat::GetDamageVsDemonicAttribute()},
			{TEXT("damage_vs_death"), Combat::GetDamageVsDeathAttribute()},
			{TEXT("damage_vs_pestilence"),
			 Combat::GetDamageVsPestilenceAttribute()},
			{TEXT("damage_vs_famine"), Combat::GetDamageVsFamineAttribute()},
			{TEXT("damage_vs_celestial"),
			 Combat::GetDamageVsCelestialAttribute()},
			{TEXT("damage_vs_chaos"), Combat::GetDamageVsChaosAttribute()},
			{TEXT("damage_vs_void"), Combat::GetDamageVsVoidAttribute()},
			{TEXT("area_of_effect"), Combat::GetAreaOfEffectAttribute()},
			{TEXT("dot_damage"), Combat::GetDotDamageAttribute()},
			{TEXT("dot_frequency"), Combat::GetDotFrequencyAttribute()},
			{TEXT("dot_duration"), Combat::GetDotDurationAttribute()},

			// Everything else the class table names.
			{TEXT("movement_speed"), Combat::GetMovementSpeedAttribute()},

			// COOLDOWN REDUCTION JOINED THEM IN ISSUE #895. No class line names
			// it, so gear and the Efficacy attribute are its only sources, and
			// without an entry here the affix granting it was dropped before it
			// reached the attribute UCataclysmGameplayAbility::ApplyCooldown
			// now reads.
			{TEXT("cooldown_reduction"),
			 Combat::GetCooldownReductionAttribute()},

			// THE CHANCE A SKILL DOES NOT GO ON COOLDOWN AT ALL. Issue #973.
			// The Masochist's The Catalyst node is its only source and it is
			// zero for every class. The attribute it writes here stays at zero
			// too, because that node's bonus carries a health condition and a
			// conditional bonus is never folded into an attribute -- but the
			// entry is still needed, because `ApplyTo` loops over this map and
			// a stat missing from it is dropped before `StatForSkill` could
			// ever be asked for it.
			{TEXT("cooldown_skip_chance"),
			 Combat::GetCooldownSkipChanceAttribute()},

			{TEXT("loot_quantity"), Combat::GetLootQuantityAttribute()},

			// MAGIC FIND JOINED LOOT QUANTITY IN ISSUE #896. No class line
			// names it -- the design gives it a baseline of zero, since it is
			// "an added percentage rather than a percentage of something" --
			// so gear is its only source, and without an entry here the two
			// affixes granting it were dropped before they reached anything.
			{TEXT("magic_find"), Combat::GetMagicFindAttribute()},

			// HOW MUCH DAMAGE THE CHARACTER TAKES, at 100 for normal, and the
			// same again for a hit that is damage over time. Issue #1026. No
			// class line names either and none should: no affix grants them, no
			// class differs on them, and passive nodes are their only source,
			// which is the rule `Cataclysm.Attributes.CharacterSheetIsComplete`
			// uses to decide a stat is off the character sheet. Their base of
			// 100 comes from `EngineSuppliedBases` below.
			//
			// BOTH ATTRIBUTES STAY AT 100 FOR EVERY CHARACTER TODAY, because all
			// three nodes that move them carry a condition and a conditional
			// bonus is never folded into an attribute.
			// `UCataclysmDamageCalculation::Resolve` asks for the stat rather
			// than reading the attribute. The entries are still needed, because
			// `ApplyTo` loops over THIS map and a stat missing from it is dropped
			// before `StatForSkill` could ever be asked for it.
			{TEXT("damage_taken"), Combat::GetDamageTakenAttribute()},
			{TEXT("damage_over_time_taken"),
			 Combat::GetDamageOverTimeTakenAttribute()},

			// AND WHETHER DAMAGE OVER TIME DEALS THIS CHARACTER NOTHING AT ALL.
			// Issue #1039. Zero for every class, and the Masochist's Vessel
			// Unbroken capstone option is its only source. Here for the reason
			// every other node-supplied stat is: `ApplyTo` loops over this map,
			// so a stat missing from it is dropped before it reaches a
			// character.
			{FString(UCataclysmDamageCalculation::DebuffDamageSuppressedStat),
			 Combat::GetDebuffDamageSuppressedAttribute()},

			// THE CHANCE A MELEE CRITICAL STRIKE APPLIES BLEEDING. Issue #1032.
			// Zero for every class, and the Masochist's Mutilation Mastery is
			// its only source. Here for the reason every other node-supplied
			// stat is: `ApplyTo` loops over this map, so a stat missing from it
			// is dropped before it reaches a character.
			{TEXT("bleed_on_crit_chance"),
			 Combat::GetBleedOnCritChanceAttribute()},
		};
	}();

	return Map;
}

const TMap<FName, float>& UCataclysmPlayerClassStats::EngineSuppliedBases()
{
	// A PLAIN FILE-SCOPE STATIC WOULD DO, unlike StatToAttribute above, because
	// these are floats rather than reflected properties. It is written the same
	// way so the two read alike and neither has to be reasoned about twice.
	static const TMap<FName, float> Map = {
		// HOW LONG ONE TURN OF THE BREAKING POINT'S CONVERSION LASTS, before any
		// points are spent. Issue #1025. The node reads "The conversion lasts 3
		// seconds, increased by 5% per point", so the 3 is a base and the 5 is
		// the per-point value on the passive effects sheet. Eight points make it
		// 4.2 seconds.
		//
		// UNTIL THIS EXISTED THE BASE WAS ZERO AND THE NODE DID NOTHING. The
		// increase multiplied nothing, the attribute was written as zero, and
		// `UCataclysmAbilitySystemComponent::NoteDamageConversionStarted` refuses
		// a window of zero, so no turn of the conversion ever began.
		{FName(UCataclysmDamageConversion::WindowStat),
		 UCataclysmDamageConversion::BaseWindowSeconds},

		// AND WHAT SHARE OF A HIT A CHARACTER TAKES, at 100 for normal, plus the
		// same again for a hit that is damage over time. Issue #1026.
		//
		// A HUNDRED BECAUSE IT IS THE IDENTITY FOR A MULTIPLIER. Three Masochist
		// nodes move these, and every one of them is written as a percentage of
		// what would otherwise arrive: "reduced by 1% per point", "take 20% more
		// damage", "take 25% less damage". A base of zero would make an increase
		// worth nothing and a `more` worth nothing, which is what
		// `test_every_stat_is_one_the_game_supplies` exists to refuse.
		//
		// HERE RATHER THAN ON THE `Default` CLASS LINE, though five stats of
		// exactly this shape sit there. `Cataclysm.Attributes.CharacterSheetIsComplete`
		// gives the rule: a stat is off the character sheet when no affix grants
		// it, nothing scales it, it has no baseline of its own and one passive
		// node is its only source. The five on that line all fail that rule --
		// affixes grant them and the Ritualist starts at 110 area of effect --
		// and these two meet it. `docs/DECISIONS.md` records that promoting them
		// to a class line is right the day an affix grants one.
		{FName(UCataclysmDamageCalculation::DamageTakenStat),
		 UCataclysmDamageCalculation::NormalDamageTaken},
		{FName(UCataclysmDamageCalculation::DamageOverTimeTakenStat),
		 UCataclysmDamageCalculation::NormalDamageTaken},

		// AND HOW LONG A LASTING HARMFUL EFFECT ON THE CHARACTER RUNS, at 100
		// for normal. Issue #1033. The THIRD stat of this shape and it meets the
		// same rule the two above do: no affix grants it, nothing scales it, no
		// class differs on it, and two passive nodes of one tree are its only
		// sources.
		//
		// A BASE IS LOAD-BEARING HERE IN A WAY IT IS NOT FOR A BONUS. Both rows
		// that move it are `increased`, so with no base under them the stat
		// would resolve to zero and every stun and every burn in the game would
		// end the instant it landed. Issue #1025 records exactly that failure
		// happening to the conversion window.
		{FName(UCataclysmDebuffs::DurationStat),
		 UCataclysmDebuffs::NormalDuration},
	};

	return Map;
}

FString UCataclysmPlayerClassStats::ChosenClass()
{
	const FString Asked = CVarPlayerClass.GetValueOnGameThread();

	// FALLING BACK TO StartingClassName AND NOT TO DefaultClassName. Someone
	// typing `Cataclysm.PlayerClass ""` should get the class a character starts
	// on, not the shared line every class inherits stats from -- that line has
	// no armour, no resistance, no block, no flat damage reduction and no leech,
	// and a character sitting on it loses the fight the sandbox puts in front of
	// them. Issue #806. Naming `Default` outright still works and is still a
	// legitimate thing to ask for.
	return Asked.IsEmpty() ? StartingClassName : Asked;
}

int32 UCataclysmPlayerClassStats::ChosenLevel()
{
	// CLAMPED RATHER THAN REFUSED, because this is a console variable a person
	// types at and a nonsensical one should still leave a playable character.
	// Level 0 would resolve every per-level term to minus one level's worth.
	return FMath::Clamp(CVarPlayerLevel.GetValueOnGameThread(), 1,
						UCataclysmClassStats::MaxLevel);
}

int32 UCataclysmPlayerClassStats::ApplyTo(
	UAbilitySystemComponent* AbilitySystem,
	const UDataTable* ClassTable,
	const FString& ClassName, int32 Level,
	const TMap<FName, TArray<FCataclysmStatModifier>>* Modifiers,
	ECataclysmPoolFill PoolFill,
	const TMap<FName, float>* BaseOverrides)
{
	if (!AbilitySystem || !ClassTable)
	{
		return 0;
	}

	int32 Written = 0;

	// WHAT EVERY STAT WAS WORKED OUT FROM, KEPT FOR THE SKILLS. Issue #943.
	//
	// A modifier naming a required tag cannot be judged here, because a
	// character sheet has no skill in hand, so it contributes nothing below.
	// Keeping the base and the modifier list is what lets a skill work the stat
	// out again with its own tags, through
	// `UCataclysmAbilitySystemComponent::StatForSkill`. Before this both were
	// local variables that went out of scope, and every scoped modifier in the
	// game was discarded and never seen again.
	TMap<FName, FCataclysmStatInputs> Inputs;

	// RESOLVING ONE STAT, USED BY BOTH PASSES BELOW. `Extra` is a modifier the
	// caller wants applied on top of whatever `Modifiers` holds for the stat,
	// and is only ever what the character's attributes contribute.
	const auto Resolve = [&](const FString& Stat,
							 const FCataclysmStatModifier* Extra,
							 FCataclysmStatBreakdown& OutBreakdown) -> float
	{
		// BaseFor falls back to the shared Default line when the class does not
		// name the stat, and to zero when neither does. Zero is the right answer
		// there and not a failure: most classes leave most stats alone, and a
		// Ritualist really does have no armour.
		float Base = UCataclysmClassStats::BaseFor(
			ClassTable, ClassName, Stat, Level);

		// AND A BASE THE ENGINE STATES IN C++, for a stat no class line should
		// name. Issue #1025. See `EngineSuppliedBases` in the header for why
		// there is a third place a base can come from at all.
		//
		// UNCONDITIONAL RATHER THAN ONLY WHEN THE CLASS TABLE IS SILENT, because
		// BaseFor cannot tell "no row names it" from "a row names it as zero",
		// and because no class line may name any of these anyway:
		// `Cataclysm.PlayerStats.EveryClassStatDrivesAnAttribute` fails if one
		// does. Making it conditional would need a reading BaseFor cannot give,
		// to guard against a case that test already forbids.
		if (const float* Stated = EngineSuppliedBases().Find(FName(*Stat)))
		{
			Base = *Stated;
		}

		// A SUPPLIED BASE REPLACES THE CLASS LINE RATHER THAN ADDING TO IT, for
		// a stat no class line can state. Attack speed was the first: a
		// character's swing rate comes from the weapons it holds, and two
		// weapons average their rates, which neither the class table nor the
		// modifier pipeline can express. The eight primary attributes are the
		// others, and for the same reason -- a class line cannot state how many
		// points a particular character has spent. See the header for why attack
		// damage deliberately does not use this.
		if (BaseOverrides)
		{
			if (const float* Supplied = BaseOverrides->Find(FName(*Stat)))
			{
				Base = *Supplied;
			}
		}

		OutBreakdown = FCataclysmStatBreakdown();
		OutBreakdown.Base = Base;
		OutBreakdown.Final = Base;

		TArray<FCataclysmStatModifier> ForStat;
		if (Modifiers)
		{
			if (const TArray<FCataclysmStatModifier>* Found =
					Modifiers->Find(FName(*Stat)))
			{
				ForStat = *Found;
			}
		}
		if (Extra)
		{
			ForStat.Add(*Extra);
		}
		if (ForStat.IsEmpty())
		{
			return Base;
		}

		// KEPT SO A SKILL CAN WORK THIS OUT AGAIN WITH ITS OWN TAGS. Issue #943.
		//
		// ONLY A STAT THAT HAS MODIFIERS IS RECORDED. With none the pipeline
		// returns the base, which is exactly what the attribute below ends up
		// holding, so `StatForSkill` falling back to the attribute gives the
		// same answer and the map stays small.
		//
		// THE WHOLE LIST, INCLUDING THE MODIFIERS THE LINE BELOW IS ABOUT TO
		// DISCARD. Those are the scoped ones, and they are the only reason this
		// exists.
		FCataclysmStatInputs& Recorded = Inputs.FindOrAdd(FName(*Stat));
		Recorded.Base = Base;
		Recorded.Modifiers = ForStat;

		// THE THREE-BUCKET PIPELINE, NOT ADDITION. A gear affix can be flat or
		// increased, and the design's whole damage model is
		// (base + flat) x (1 + increases) x more, so adding the numbers up here
		// would give a different answer from the one every skill already uses.
		//
		// AN EMPTY TAG CONTAINER, AND THAT IS THE POINT OF PASSING ONE. A stat
		// on the character sheet has no skill in hand, so a modifier scoped to
		// something -- "increased damage against Demons" -- must not apply.
		// UCataclysmStatPipeline::ModifierApplies is what enforces that; only
		// globally scoped modifiers survive an empty container.
		OutBreakdown = UCataclysmStatPipeline::Evaluate(
			Base, ForStat, FGameplayTagContainer());
		return OutBreakdown.Final;
	};

	// PASS ONE: THE EIGHT PRIMARY ATTRIBUTES, BEFORE ANYTHING THEY SCALE.
	//
	// THE ORDER IS THE WHOLE REASON THIS IS TWO PASSES. Every one of the eight
	// scales other stats through game/Data/Attributes.csv, so an attribute
	// resolved after the stat it scales contributes nothing at all -- and
	// nothing would report an error, because the stat would simply come out at
	// its base. StatToAttribute is a TMap and its order is a hash order, so
	// relying on it would make the answer depend on where a name happens to
	// land. Issues #50 and #897.
	//
	// GEAR REACHES THEM HERE, and that is what makes the eight attribute
	// affixes work: `docs/Cataclysm_GDD_v2.md` says "Gear does not grant
	// attribute points. It increases the attribute the character already has",
	// so the spent points arrive as the base through BaseOverrides and the
	// affixes arrive as increases through Modifiers, exactly like any other stat.
	FCataclysmAttributeValues Attributes;
	const TArray<FString> AttributeNames = FCataclysmAttributePoints::Names();
	for (const FString& Name : AttributeNames)
	{
		const FGameplayAttribute* Attribute = StatToAttribute().Find(Name);
		if (!Attribute || !AbilitySystem->HasAttributeSetForAttribute(*Attribute))
		{
			continue;
		}

		FCataclysmStatBreakdown Breakdown;
		const float Value = Resolve(Name, nullptr, Breakdown);
		Attributes.SetIn(Name, Value);
		AbilitySystem->SetNumericAttributeBase(*Attribute, Value);
		++Written;
	}

	// PASS TWO: EVERY OTHER STAT, WITH WHAT THE ATTRIBUTES CONTRIBUTE.
	const UDataTable* AttributeTable = LoadAttributeTable();

	for (const TPair<FString, FGameplayAttribute>& Pair : StatToAttribute())
	{
		if (AttributeNames.Contains(Pair.Key))
		{
			continue;   // already written by pass one
		}

		// THE CHECK IS NOT OPTIONAL. Writing to an attribute whose set the
		// component does not hold raises an engine ensure rather than failing
		// quietly. The player's component holds all five of these sets, but a
		// test may build one that does not.
		if (!AbilitySystem->HasAttributeSetForAttribute(Pair.Value))
		{
			continue;
		}

		// WHAT THE CHARACTER'S ATTRIBUTES ARE WORTH TO THIS STAT, in the
		// increased bucket alongside every gear increase. Returns false when no
		// attribute touches the stat at all, which is most of them.
		FCataclysmStatModifier FromAttributes;
		const bool bAttributesApply =
			UCataclysmClassStats::AttributeModifierForValues(
				AttributeTable, Attributes, Pair.Key, FromAttributes);

		FCataclysmStatBreakdown Breakdown;
		const float Value = Resolve(
			Pair.Key, bAttributesApply ? &FromAttributes : nullptr, Breakdown);

		// THE ATTACK DAMAGE BRACKET IS REMEMBERED, and only that one. Issue
		// #895. A finished attribute has its increases already applied and no
		// longer visible, and the eight conditional damage stats have to join
		// that same bracket rather than becoming a second multiplier. A hit
		// cannot reopen a bracket it cannot see.
		//
		// DIVIDED BY 100 BECAUSE THE TWO SIDES USE DIFFERENT UNITS, and passing
		// the figure across unchanged was issue #963. `SumOfIncreases` is in
		// percentage points -- 125 for +125% -- and every reader of
		// `GetAttackDamageIncreases` treats the answer as a fraction. Without
		// this the stored figure was a hundred times too large, and the eight
		// increased-damage-against-a-type affixes were worth a few percent of
		// what they say.
		if (Pair.Key == FString(UCataclysmItemModifiers::AttackDamageStat))
		{
			if (UCataclysmAbilitySystemComponent* Cataclysm =
					Cast<UCataclysmAbilitySystemComponent>(AbilitySystem))
			{
				Cataclysm->SetAttackDamageIncreases(
					Breakdown.SumOfIncreases / 100.0f);
			}
		}

		AbilitySystem->SetNumericAttributeBase(Pair.Value, Value);
		++Written;
	}

	// AND THE INPUTS GO ACROSS, SO A SKILL CAN ASK AGAIN. Issue #943.
	//
	// HERE RATHER THAN AFTER THE POOLS, because the pools are skipped entirely
	// for a character already in play -- the early return just below -- and a
	// character that swapped a helmet needs its scoped modifiers refreshed just
	// as much as one arriving in the world does.
	//
	// WHOLESALE, NOT MERGED. This is the character's whole standing stat line;
	// a stat that no longer has any modifier must lose its entry rather than
	// keep the one from before the gear came off.
	if (UCataclysmAbilitySystemComponent* Cataclysm =
			Cast<UCataclysmAbilitySystemComponent>(AbilitySystem))
	{
		Cataclysm->SetStatInputs(MoveTemp(Inputs));
	}

	// A CHARACTER ALREADY IN PLAY KEEPS ITS POOLS WHERE THEY ARE. Filling
	// them below is right for a character arriving in the world and is a free
	// heal for one that only changed a helmet. See ECataclysmPoolFill.
	if (PoolFill == ECataclysmPoolFill::LeaveAsTheyAre)
	{
		return Written;
	}

	// THE POOLS COME LAST, AND THAT IS THE WHOLE REASON THIS IS TWO STEPS.
	// UCataclysmVitalAttributeSet clamps current health to maximum health, so a
	// character whose current health was filled before its maximum was raised
	// would be clamped straight back to the placeholder 100 and none of the
	// above would show on screen.
	const TPair<FGameplayAttribute, FGameplayAttribute> Pools[] = {
		{UCataclysmVitalAttributeSet::GetHealthAttribute(),
		 UCataclysmVitalAttributeSet::GetMaxHealthAttribute()},
		{UCataclysmVitalAttributeSet::GetManaAttribute(),
		 UCataclysmVitalAttributeSet::GetMaxManaAttribute()},
		{UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(),
		 UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()},
	};

	for (const TPair<FGameplayAttribute, FGameplayAttribute>& Pool : Pools)
	{
		if (!AbilitySystem->HasAttributeSetForAttribute(Pool.Key))
		{
			continue;
		}

		AbilitySystem->SetNumericAttributeBase(
			Pool.Key, AbilitySystem->GetNumericAttribute(Pool.Value));
	}

	return Written;
}
