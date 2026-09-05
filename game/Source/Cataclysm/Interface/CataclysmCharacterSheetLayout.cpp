// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmCharacterSheetLayout.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Items/CataclysmItem.h"

namespace
{
	using FVital = UCataclysmVitalAttributeSet;
	using FCombat = UCataclysmCombatAttributeSet;
	using FResource = UCataclysmClassResourceAttributeSet;

	/** One attribute off a component, or zero when it has no such set. */
	float Read(const UAbilitySystemComponent* ASC,
			   const FGameplayAttribute& Attribute)
	{
		if (ASC == nullptr || !Attribute.IsValid()
			|| !ASC->HasAttributeSetForAttribute(Attribute))
		{
			return 0.0f;
		}

		return ASC->GetNumericAttribute(Attribute);
	}

	/**
	 * One sheet stat off a component, by the name the class tables use.
	 *
	 * THROUGH `StatToAttribute` AND NOT THROUGH A SECOND LIST. That map is
	 * already the one place a stat name is paired with its attribute -- the
	 * class stat lines, the affixes and the passive effects all go through it --
	 * and a copy here is how the sheet would come to read a different attribute
	 * from the one gear writes.
	 */
	float ReadStat(const UAbilitySystemComponent* ASC, const FString& Stat)
	{
		const TMap<FString, FGameplayAttribute>& Map =
			UCataclysmPlayerClassStats::StatToAttribute();

		if (const FGameplayAttribute* Found = Map.Find(Stat))
		{
			return Read(ASC, *Found);
		}

		return 0.0f;
	}

	/** "412 of 606". */
	FString PoolValue(const UAbilitySystemComponent* ASC,
					  const FGameplayAttribute& Current,
					  const FGameplayAttribute& Maximum)
	{
		return FString::Printf(
			TEXT("%s of %s"),
			*UCataclysmCharacterSheetLayout::Number(Read(ASC, Current)),
			*UCataclysmCharacterSheetLayout::Number(Read(ASC, Maximum)));
	}

	const FString ResistancePrefix = TEXT("resistance_");
	const FString DamageVsPrefix = TEXT("damage_vs_");

	/** "resistance_war" becomes "War"; empty when it is not one of the eight. */
	FName DamageTypeFromStat(const FString& Stat, const FString& Prefix)
	{
		if (!Stat.StartsWith(Prefix))
		{
			return NAME_None;
		}

		const FString Tail = Stat.RightChop(Prefix.Len());
		for (const FName& Type : UCataclysmItemModifiers::DamageTypeNames())
		{
			if (Type.ToString().ToLower() == Tail)
			{
				return Type;
			}
		}

		return NAME_None;
	}
}

FString FCataclysmStatLine::AsLine() const
{
	return Note.IsEmpty()
		? FString::Printf(TEXT("%s: %s"), *Name, *Value)
		: FString::Printf(TEXT("%s: %s (%s)"), *Name, *Value, *Note);
}

// ---------------------------------------------------------------------------
// The groups and what is in them
// ---------------------------------------------------------------------------

const TArray<ECataclysmSheetGroup>& UCataclysmCharacterSheetLayout::Groups()
{
	static const TArray<ECataclysmSheetGroup> All = {
		ECataclysmSheetGroup::Resource,
		ECataclysmSheetGroup::Recovery,
		ECataclysmSheetGroup::Defence,
		ECataclysmSheetGroup::Offence,
		ECataclysmSheetGroup::Utility,
	};

	return All;
}

FString UCataclysmCharacterSheetLayout::HeadingFor(ECataclysmSheetGroup Group)
{
	switch (Group)
	{
	case ECataclysmSheetGroup::Resource:	return TEXT("Pools");
	case ECataclysmSheetGroup::Recovery:	return TEXT("Recovery");
	case ECataclysmSheetGroup::Defence:		return TEXT("Defence");
	case ECataclysmSheetGroup::Offence:		return TEXT("Offence");
	case ECataclysmSheetGroup::Utility:		return TEXT("Utility");
	}

	return FString();
}

FString UCataclysmCharacterSheetLayout::ModelGroupName(ECataclysmSheetGroup Group)
{
	switch (Group)
	{
	case ECataclysmSheetGroup::Resource:	return TEXT("Resource");
	case ECataclysmSheetGroup::Recovery:	return TEXT("Recovery");
	case ECataclysmSheetGroup::Defence:		return TEXT("Defense");
	case ECataclysmSheetGroup::Offence:		return TEXT("Offense");
	case ECataclysmSheetGroup::Utility:		return TEXT("Utility");
	}

	return FString();
}

const TArray<FString>& UCataclysmCharacterSheetLayout::StatsIn(
	ECataclysmSheetGroup Group)
{
	// THE SAME FIVE LISTS AS `STAT_GROUPS` IN `sim/cataclysm_sim/character.py`,
	// in the same order, down to which group a stat sits in.
	// `tools/tests/test_the_character_sheet_shows_the_model_stats.py` reads both
	// and fails if they ever disagree, so this is a copy that cannot drift
	// quietly rather than a copy nobody checks.
	static const TArray<FString> ResourceStats = {
		TEXT("max_health"), TEXT("max_mana"), TEXT("max_energy_shield"),
		TEXT("class_resource"),
	};

	static const TArray<FString> RecoveryStats = {
		TEXT("health_regen"), TEXT("mana_regen"), TEXT("energy_shield_regen"),
		TEXT("life_leech"), TEXT("mana_leech"), TEXT("energy_shield_leech"),
	};

	static const TArray<FString> DefenceStats = {
		TEXT("armor"), TEXT("evasion"), TEXT("block_chance"),
		TEXT("damage_reduction"), TEXT("retaliation"),
		TEXT("crowd_control_resistance"),
		TEXT("resistance_war"), TEXT("resistance_demonic"),
		TEXT("resistance_death"), TEXT("resistance_pestilence"),
		TEXT("resistance_famine"), TEXT("resistance_celestial"),
		TEXT("resistance_chaos"), TEXT("resistance_void"),
	};

	static const TArray<FString> OffenceStats = {
		TEXT("crit_chance"), TEXT("crit_multiplier"), TEXT("attack_speed"),
		TEXT("area_of_effect"), TEXT("dot_damage"), TEXT("dot_frequency"),
		TEXT("dot_duration"), TEXT("penetration"), TEXT("armor_penetration"),
		TEXT("spell_damage"),
		TEXT("damage_vs_war"), TEXT("damage_vs_demonic"),
		TEXT("damage_vs_death"), TEXT("damage_vs_pestilence"),
		TEXT("damage_vs_famine"), TEXT("damage_vs_celestial"),
		TEXT("damage_vs_chaos"), TEXT("damage_vs_void"),
	};

	static const TArray<FString> UtilityStats = {
		TEXT("movement_speed"), TEXT("cooldown_reduction"),
		TEXT("magic_find"), TEXT("loot_quantity"),
	};

	static const TArray<FString> Nothing;

	switch (Group)
	{
	case ECataclysmSheetGroup::Resource:	return ResourceStats;
	case ECataclysmSheetGroup::Recovery:	return RecoveryStats;
	case ECataclysmSheetGroup::Defence:		return DefenceStats;
	case ECataclysmSheetGroup::Offence:		return OffenceStats;
	case ECataclysmSheetGroup::Utility:		return UtilityStats;
	}

	return Nothing;
}

const TArray<FString>& UCataclysmCharacterSheetLayout::SheetStats()
{
	static const TArray<FString> All = []
	{
		TArray<FString> Built;
		for (const ECataclysmSheetGroup Group : Groups())
		{
			Built.Append(StatsIn(Group));
		}
		return Built;
	}();

	return All;
}

int32 UCataclysmCharacterSheetLayout::SheetStatCount()
{
	return SheetStats().Num();
}

// ---------------------------------------------------------------------------
// What a player calls each one
// ---------------------------------------------------------------------------

FString UCataclysmCharacterSheetLayout::NameFor(const FString& Stat)
{
	// THE EIGHT RESISTANCES AND THE EIGHT DAMAGE BONUSES ARE BUILT FROM THE
	// DAMAGE TYPE LIST rather than written out twice more. There is one list of
	// damage types in this project and it is `DamageTypeNames`.
	if (const FName Type = DamageTypeFromStat(Stat, ResistancePrefix);
		Type != NAME_None)
	{
		return FString::Printf(TEXT("%s resistance"), *Type.ToString());
	}

	if (const FName Type = DamageTypeFromStat(Stat, DamageVsPrefix);
		Type != NAME_None)
	{
		return FString::Printf(TEXT("Damage against %s"), *Type.ToString());
	}

	static const TMap<FString, FString> Names = {
		{TEXT("max_health"), TEXT("Health")},
		{TEXT("max_mana"), TEXT("Mana")},
		{TEXT("max_energy_shield"), TEXT("Energy shield")},
		{TEXT("class_resource"), TEXT("Class resource")},

		{TEXT("health_regen"), TEXT("Health regeneration")},
		{TEXT("mana_regen"), TEXT("Mana regeneration")},
		{TEXT("energy_shield_regen"), TEXT("Energy shield regeneration")},
		{TEXT("life_leech"), TEXT("Life leech")},
		{TEXT("mana_leech"), TEXT("Mana leech")},
		{TEXT("energy_shield_leech"), TEXT("Energy shield leech")},

		{TEXT("armor"), TEXT("Armour")},
		{TEXT("evasion"), TEXT("Evasion")},
		{TEXT("block_chance"), TEXT("Block chance")},
		{TEXT("damage_reduction"), TEXT("Damage reduction")},
		{TEXT("retaliation"), TEXT("Retaliation")},
		{TEXT("crowd_control_resistance"), TEXT("Crowd control resistance")},

		{TEXT("crit_chance"), TEXT("Critical strike chance")},
		{TEXT("crit_multiplier"), TEXT("Critical strike multiplier")},
		{TEXT("attack_speed"), TEXT("Attack speed")},
		{TEXT("area_of_effect"), TEXT("Area of effect")},
		{TEXT("dot_damage"), TEXT("Damage over time, per tick")},
		{TEXT("dot_frequency"), TEXT("Damage over time, ticks per second")},
		{TEXT("dot_duration"), TEXT("Damage over time, duration")},
		{TEXT("penetration"), TEXT("Resistance penetration")},
		{TEXT("armor_penetration"), TEXT("Armour penetration")},
		{TEXT("spell_damage"), TEXT("Spell damage")},

		{TEXT("movement_speed"), TEXT("Movement speed")},
		{TEXT("cooldown_reduction"), TEXT("Cooldown reduction")},
		{TEXT("magic_find"), TEXT("Magic find")},
		{TEXT("loot_quantity"), TEXT("Loot quantity")},
	};

	if (const FString* Found = Names.Find(Stat))
	{
		return *Found;
	}

	// THE STAT'S OWN NAME RATHER THAN NOTHING. A sheet stat added to the model
	// and not named here should show up as an ugly row, not as a missing one.
	return Stat;
}

bool UCataclysmCharacterSheetLayout::IsPercentage(const FString& Stat)
{
	// EVERY ENTRY HERE HAS EVIDENCE BEHIND IT, and the ones left out are left
	// out because they do not: the attribute set headers say evasion and block
	// are chances, that area of effect and the three damage-over-time levers are
	// percentages where 100 is unchanged, that resistance penetration is in
	// percentage points off a target's resistance and armour penetration is a
	// share of its armour ignored; `DEFAULT_STAT_LINE` in
	// `sim/cataclysm_sim/character.py` gives the critical strike multiplier a
	// base of 150 and movement speed a base of 4 metres per second; the class
	// stat table in `docs/Cataclysm_GDD_v2.md` gives the Masochist 15.8%
	// retaliation; and the three leeches each say "percentage of damage dealt".
	//
	// ARMOUR AND SPELL DAMAGE ARE DELIBERATELY NOT HERE. Armour runs through a
	// curve rather than being a percentage of anything, and nothing in the code
	// or the model states a unit for spell damage, so neither gets a percent
	// sign it has not earned.
	static const TSet<FString> Percentages = []
	{
		TSet<FString> Built = {
			TEXT("life_leech"), TEXT("mana_leech"), TEXT("energy_shield_leech"),
			TEXT("evasion"), TEXT("block_chance"), TEXT("damage_reduction"),
			TEXT("retaliation"), TEXT("crowd_control_resistance"),
			TEXT("crit_chance"), TEXT("crit_multiplier"),
			TEXT("area_of_effect"), TEXT("dot_damage"), TEXT("dot_frequency"),
			TEXT("dot_duration"), TEXT("penetration"), TEXT("armor_penetration"),
			TEXT("cooldown_reduction"), TEXT("magic_find"),
			TEXT("loot_quantity"),
		};

		for (const FName& Type : UCataclysmItemModifiers::DamageTypeNames())
		{
			const FString Lower = Type.ToString().ToLower();
			Built.Add(ResistancePrefix + Lower);
			Built.Add(DamageVsPrefix + Lower);
		}

		return Built;
	}();

	return Percentages.Contains(Stat);
}

// ---------------------------------------------------------------------------
// Figures
// ---------------------------------------------------------------------------

FString UCataclysmCharacterSheetLayout::Number(float Value)
{
	// A DECIMAL PLACE ONLY WHEN IT SAYS SOMETHING. A health pool of 606 reads
	// worse as "606.0", and a health regeneration of 1.15 reads as "1" without
	// one. The test is whether the value is a whole number, not how large it is.
	if (FMath::IsNearlyEqual(Value, FMath::RoundToFloat(Value), 0.05f))
	{
		return FString::Printf(TEXT("%.0f"), Value);
	}

	return FString::Printf(TEXT("%.1f"), Value);
}

FString UCataclysmCharacterSheetLayout::Percent(float Value)
{
	return Number(Value) + TEXT("%");
}

// ---------------------------------------------------------------------------
// One row
// ---------------------------------------------------------------------------

FCataclysmStatLine UCataclysmCharacterSheetLayout::LineFor(
	const FString& Stat, const UAbilitySystemComponent* ASC,
	int32 DifficultyTier)
{
	const FString Name = NameFor(Stat);

	// -- the four pools, which show what is in them as well as how big they are
	if (Stat == TEXT("max_health"))
	{
		return FCataclysmStatLine(Name, PoolValue(ASC, FVital::GetHealthAttribute(),
												  FVital::GetMaxHealthAttribute()));
	}

	if (Stat == TEXT("max_mana"))
	{
		return FCataclysmStatLine(Name, PoolValue(ASC, FVital::GetManaAttribute(),
												  FVital::GetMaxManaAttribute()));
	}

	if (Stat == TEXT("max_energy_shield"))
	{
		return FCataclysmStatLine(
			Name, PoolValue(ASC, FVital::GetEnergyShieldAttribute(),
							FVital::GetMaxEnergyShieldAttribute()));
	}

	if (Stat == TEXT("class_resource"))
	{
		return FCataclysmStatLine(
			Name, PoolValue(ASC, FResource::GetClassResourceAttribute(),
							FResource::GetMaxClassResourceAttribute()));
	}

	// -- the eight resistances, which show three numbers rather than one
	if (const FName Type = DamageTypeFromStat(Stat, ResistancePrefix);
		Type != NAME_None)
	{
		// THE GENERIC FIGURE IS PART OF WHAT THE CHARACTER HOLDS, exactly as
		// `Cataclysm.ShowResistances` treats it: All Resistance applies to every
		// type by definition, so a per-type row that left it out would disagree
		// with what the player takes.
		float Generic = 0.0f;
		if (ASC)
		{
			if (const UCataclysmAllResistanceAttributeSet* All =
					ASC->GetSet<UCataclysmAllResistanceAttributeSet>())
			{
				Generic = All->GetAllResistance();
			}
		}

		const float Held =
			Generic + Read(ASC, UCataclysmDamageCalculation::ResistanceAttributeFor(Type));
		const float Penalty =
			UCataclysmDamageCalculation::ResistancePenaltyAt(DifficultyTier);
		const float Met = UCataclysmDamageCalculation::EffectiveResistance(
			Held - Penalty, /*Penetration=*/0.0f);

		// WORKED OUT THE WAY `Resolve` WORKS IT OUT: the total less the penalty,
		// then bounded by the cap. Showing the first figure alone is what issue
		// #1233 refuses, because a player at 70 on a tier 8 character is at 0
		// once the penalty lands and the sheet would say they were at the cap.
		return FCataclysmStatLine(
			Name, Percent(Held),
			FString::Printf(
				TEXT("Difficulty tier %d takes %s off, leaving %s. A hit meets %s."),
				DifficultyTier, *Percent(Penalty), *Percent(Held - Penalty),
				*Percent(Met)));
	}

	if (const FName Type = DamageTypeFromStat(Stat, DamageVsPrefix);
		Type != NAME_None)
	{
		return FCataclysmStatLine(
			Name, Percent(ReadStat(ASC, Stat)),
			FString::Printf(TEXT("Against an enemy of the %s Cataclysm only."),
							*Type.ToString()));
	}

	// -- the stats whose figure does not say what it does
	if (Stat == TEXT("armor"))
	{
		const float Armor = ReadStat(ASC, Stat);
		const float Stops =
			UCataclysmDamageCalculation::ArmorReduction(Armor, DifficultyTier);

		FString Note = FString::Printf(
			TEXT("Stops %s of a hit at difficulty tier %d."), *Percent(Stops),
			DifficultyTier);

		if (Stops >= UCataclysmDamageCalculation::ArmorReductionCap - 0.05f)
		{
			Note += FString::Printf(TEXT(" That is the %s cap."),
									*Percent(UCataclysmDamageCalculation::ArmorReductionCap));
		}

		return FCataclysmStatLine(Name, Number(Armor), Note);
	}

	if (Stat == TEXT("evasion"))
	{
		return FCataclysmStatLine(
			Name, Percent(ReadStat(ASC, Stat)),
			TEXT("Direct hits only. Area damage always lands."));
	}

	if (Stat == TEXT("block_chance"))
	{
		return FCataclysmStatLine(
			Name, Percent(ReadStat(ASC, Stat)),
			FString::Printf(TEXT("A block removes %s of a hit, not all of it."),
							*Percent(UCataclysmDamageCalculation::BlockDamageReduction)));
	}

	if (Stat == TEXT("damage_reduction"))
	{
		const float Held = ReadStat(ASC, Stat);
		const float Applied =
			UCataclysmDamageCalculation::EffectiveDamageReduction(Held);

		return FCataclysmStatLine(
			Name, Percent(Held),
			FString::Printf(TEXT("Takes %s off a hit. The cap is %s."),
							*Percent(Applied),
							*Percent(UCataclysmDamageCalculation::DamageReductionCap)));
	}

	if (Stat == TEXT("retaliation"))
	{
		return FCataclysmStatLine(
			Name, Percent(ReadStat(ASC, Stat)),
			TEXT("Damage dealt back to whatever lands a hit."));
	}

	if (Stat == TEXT("crit_chance"))
	{
		const float Ceiling = Read(ASC, FCombat::GetMaxCritChanceAttribute());

		return FCataclysmStatLine(
			Name, Percent(ReadStat(ASC, Stat)),
			Ceiling > 0.0f
				? FString::Printf(TEXT("Capped at %s."), *Percent(Ceiling))
				: FString());
	}

	if (Stat == TEXT("attack_speed"))
	{
		return FCataclysmStatLine(
			Name, Number(ReadStat(ASC, Stat)) + TEXT(" per second"),
			TEXT("From the equipped weapon rather than from the character."));
	}

	if (Stat == TEXT("movement_speed"))
	{
		return FCataclysmStatLine(
			Name, Number(ReadStat(ASC, Stat)) + TEXT(" metres per second"));
	}

	if (Stat == TEXT("cooldown_reduction"))
	{
		// THE ATTRIBUTE IS THE ACCUMULATED SUM OF INCREASES AND NOT THE
		// PERCENTAGE. Its own header says so: a skill's cooldown is its base
		// divided by one plus this, and the displayed reduction is this divided
		// by one plus this. Showing the raw attribute would say 100% reduction
		// to a character whose cooldowns had merely halved.
		const float Increases = ReadStat(ASC, Stat);

		return FCataclysmStatLine(
			Name,
			Percent(FCombat::DisplayedCooldownReduction(Increases)),
			FString::Printf(TEXT("From increases totalling %s. It never reaches "
								 "100%%."),
						   *Percent(100.0f * Increases)));
	}

	if (Stat == TEXT("penetration"))
	{
		return FCataclysmStatLine(
			Name, Percent(ReadStat(ASC, Stat)),
			TEXT("Percentage points taken off a target's resistance."));
	}

	if (Stat == TEXT("armor_penetration"))
	{
		return FCataclysmStatLine(Name, Percent(ReadStat(ASC, Stat)),
								  TEXT("Of a target's armour, ignored."));
	}

	if (Stat == TEXT("life_leech") || Stat == TEXT("mana_leech")
		|| Stat == TEXT("energy_shield_leech"))
	{
		return FCataclysmStatLine(Name, Percent(ReadStat(ASC, Stat)),
								  TEXT("Of damage dealt."));
	}

	if (Stat == TEXT("health_regen") || Stat == TEXT("mana_regen")
		|| Stat == TEXT("energy_shield_regen"))
	{
		return FCataclysmStatLine(
			Name, Number(ReadStat(ASC, Stat)) + TEXT(" per second"));
	}

	if (Stat == TEXT("area_of_effect") || Stat == TEXT("dot_damage")
		|| Stat == TEXT("dot_frequency") || Stat == TEXT("dot_duration"))
	{
		return FCataclysmStatLine(Name, Percent(ReadStat(ASC, Stat)),
								  TEXT("100% is unchanged."));
	}

	// -- everything else: the figure and nothing else to say about it
	const float Value = ReadStat(ASC, Stat);

	return FCataclysmStatLine(Name,
							  IsPercentage(Stat) ? Percent(Value) : Number(Value));
}

TArray<FCataclysmStatLine> UCataclysmCharacterSheetLayout::LinesIn(
	ECataclysmSheetGroup Group, const UAbilitySystemComponent* ASC,
	int32 DifficultyTier)
{
	TArray<FCataclysmStatLine> Lines;

	for (const FString& Stat : StatsIn(Group))
	{
		Lines.Add(LineFor(Stat, ASC, DifficultyTier));
	}

	return Lines;
}

// ---------------------------------------------------------------------------
// The eight attributes
// ---------------------------------------------------------------------------

TArray<FCataclysmStatLine> UCataclysmCharacterSheetLayout::AttributeLines(
	const FCataclysmAttributePoints& Points)
{
	TArray<FCataclysmStatLine> Lines;

	for (const FString& Attribute : FCataclysmAttributePoints::Names())
	{
		Lines.Add(FCataclysmStatLine(
			Attribute, FString::FromInt(Points.PointsIn(Attribute))));
	}

	return Lines;
}

FString UCataclysmCharacterSheetLayout::UnspentPointsText(int32 Unspent)
{
	if (Unspent <= 0)
	{
		return TEXT("No points to spend.");
	}

	return FString::Printf(TEXT("%d point%s to spend."), Unspent,
						   Unspent == 1 ? TEXT("") : TEXT("s"));
}
