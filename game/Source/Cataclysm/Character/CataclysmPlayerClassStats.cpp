// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmPlayerClassStats.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmClassStats.h"
#include "Items/CataclysmItem.h"
#include "Engine/DataTable.h"
#include "HAL/IConsoleManager.h"

const TCHAR* UCataclysmPlayerClassStats::ClassStatsAssetPath =
	TEXT("/Game/Data/DT_ClassStats.DT_ClassStats");

namespace
{
	/**
	 * Which class stat line the player starts on.
	 *
	 * A STRING RATHER THAN AN INDEX, because the row names in
	 * game/Data/ClassStats.csv are what UCataclysmClassStats::BaseFor takes and
	 * an index would be a second ordering that could disagree with the table.
	 * `Default` is the shared line every class inherits from, and it is a
	 * legitimate answer rather than a placeholder: there is no class selection
	 * screen, so a character that has chosen nothing sits on it.
	 */
	TAutoConsoleVariable<FString> CVarPlayerClass(
		TEXT("Cataclysm.PlayerClass"),
		TEXT("Default"),
		TEXT("Which row of game/Data/ClassStats.csv the player starts on: "
			 "Default, Ravager, Ritualist or Masochist. Read when the pawn is "
			 "possessed, so press Play again after changing it."),
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

		return TMap<FString, FGameplayAttribute>{
			// The pools and what refills them.
			{TEXT("max_health"), Vital::GetMaxHealthAttribute()},
			{TEXT("max_mana"), Vital::GetMaxManaAttribute()},
			{TEXT("max_energy_shield"), Vital::GetMaxEnergyShieldAttribute()},
			{TEXT("health_regen"), Vital::GetHealthRegenAttribute()},
			{TEXT("mana_regen"), Vital::GetManaRegenAttribute()},
			{TEXT("energy_shield_regen"), Vital::GetEnergyShieldRegenAttribute()},
			{TEXT("life_leech"), Vital::GetLifeLeechAttribute()},

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
			{TEXT("retaliation"), Combat::GetRetaliationAttribute()},

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
			{TEXT("loot_quantity"), Combat::GetLootQuantityAttribute()},

			// MAGIC FIND JOINED LOOT QUANTITY IN ISSUE #896. No class line
			// names it -- the design gives it a baseline of zero, since it is
			// "an added percentage rather than a percentage of something" --
			// so gear is its only source, and without an entry here the two
			// affixes granting it were dropped before they reached anything.
			{TEXT("magic_find"), Combat::GetMagicFindAttribute()},
		};
	}();

	return Map;
}

FString UCataclysmPlayerClassStats::ChosenClass()
{
	const FString Asked = CVarPlayerClass.GetValueOnGameThread();
	return Asked.IsEmpty() ? UCataclysmClassStats::DefaultClassName : Asked;
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

	for (const TPair<FString, FGameplayAttribute>& Pair : StatToAttribute())
	{
		// THE CHECK IS NOT OPTIONAL. Writing to an attribute whose set the
		// component does not hold raises an engine ensure rather than failing
		// quietly. The player's component holds all three of these sets, but a
		// test may build one that does not.
		if (!AbilitySystem->HasAttributeSetForAttribute(Pair.Value))
		{
			continue;
		}

		// BaseFor falls back to the shared Default line when the class does not
		// name the stat, and to zero when neither does. Zero is the right answer
		// there and not a failure: most classes leave most stats alone, and a
		// Ritualist really does have no armour.
		float Base = UCataclysmClassStats::BaseFor(
			ClassTable, ClassName, Pair.Key, Level);

		// A SUPPLIED BASE REPLACES THE CLASS LINE RATHER THAN ADDING TO IT, for
		// a stat no class line can state. Attack speed is the only one: a
		// character's swing rate comes from the weapons it holds, and two
		// weapons average their rates, which neither the class table nor the
		// modifier pipeline can express. See the header for why attack damage
		// deliberately does not use this.
		if (BaseOverrides)
		{
			if (const float* Supplied = BaseOverrides->Find(FName(*Pair.Key)))
			{
				Base = *Supplied;
			}
		}

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
		float Value = Base;
		FCataclysmStatBreakdown Breakdown;
		Breakdown.Base = Base;
		Breakdown.Final = Base;

		if (Modifiers)
		{
			if (const TArray<FCataclysmStatModifier>* ForStat =
					Modifiers->Find(FName(*Pair.Key)))
			{
				Breakdown = UCataclysmStatPipeline::Evaluate(
					Base, *ForStat, FGameplayTagContainer());
				Value = Breakdown.Final;
			}
		}

		// THE ATTACK DAMAGE BRACKET IS REMEMBERED, and only that one. Issue
		// #895. A finished attribute has its increases already applied and no
		// longer visible, and the eight conditional damage stats have to join
		// that same bracket rather than becoming a second multiplier. A hit
		// cannot reopen a bracket it cannot see.
		if (Pair.Key == FString(UCataclysmItemModifiers::AttackDamageStat))
		{
			if (UCataclysmAbilitySystemComponent* Cataclysm =
					Cast<UCataclysmAbilitySystemComponent>(AbilitySystem))
			{
				Cataclysm->SetAttackDamageIncreases(Breakdown.SumOfIncreases);
			}
		}

		AbilitySystem->SetNumericAttributeBase(Pair.Value, Value);
		++Written;
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
