// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmCharacterSheetLayout.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
// The pipeline the attribute tooltip evaluates a stat through, with and without the attribute. Issue #2815.
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerClassStats.h"
// The rows the attribute tooltip is written from. Issue #2815.
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmEquipmentComponent.h"

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
			// AND WHAT THE STAT REFRESH COULD NOT FOLD INTO THE ATTRIBUTE: a row scaled or conditioned on the
			// character's state now, such as armour per floor cleared. Issue #2183. The attribute stays the starting
			// point, so a Shred or another debuff on it is kept, as the resistance lines keep it (2026-10-18).
			const float Attribute = Read(ASC, *Found);
			const UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(ASC);
			return Cataclysm ? Cataclysm->AttributeWithWhatWasNotFolded(FName(*Stat), Attribute) : Attribute;
		}

		return 0.0f;
	}

	/** "412 of 606": what is in the pool, and the maximum `FigureFor` reads. */
	FString PoolValue(const UAbilitySystemComponent* ASC,
					  const FGameplayAttribute& Current,
					  const FString& MaximumStat)
	{
		return FString::Printf(
			TEXT("%s of %s"),
			*UCataclysmCharacterSheetLayout::Number(Read(ASC, Current)),
			*UCataclysmCharacterSheetLayout::Number(
				UCataclysmCharacterSheetLayout::FigureFor(MaximumStat, ASC)));
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

FString UCataclysmCharacterSheetLayout::KillsLine(int32 RunKills, int32 LifetimeKills)
{
	return FString::Printf(TEXT("Kills this run %d   Kills %d"), RunKills, LifetimeKills);
}

FString UCataclysmCharacterSheetLayout::ConsumptionMarkLine(bool bMarked)
{
	return bMarked ? FString(TEXT("Marked: crossed the Consumption Threshold")) : FString();
}

FString UCataclysmCharacterSheetLayout::WornResidueLine(float WornResidue, int32 DifficultyTier)
{
	const int32 Tier = FMath::Clamp(DifficultyTier, 1, 8);
	return FString::Printf(TEXT("Worn Residue %d of %d (tier %d)"), FMath::RoundToInt(WornResidue),
						   FMath::RoundToInt(UCataclysmEquipmentComponent::ConsumptionThresholdFor(Tier)), Tier);
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

FString UCataclysmCharacterSheetLayout::TemporaryAbsorbNote(float Held)
{
	return Held > 0.0f
		? FString::Printf(TEXT("%s temporary absorb, taken before the shield."), *Number(Held))
		: FString();
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
		// AND WHAT IS RESERVED, said beside the pool rather than as a line of its
		// own. Issue #1833, ruled 2026-09-30: a system needs a visible part. The
		// sheet's lines are the simulation's list, so a new one would be a model
		// change; a note is not.
		const UCataclysmAbilitySystemComponent* Reserving =
			Cast<UCataclysmAbilitySystemComponent>(ASC);
		const float Reserved = Reserving ? Reserving->HealthReserved() : 0.0f;
		return FCataclysmStatLine(Name, PoolValue(ASC, FVital::GetHealthAttribute(), Stat),
								  Reserved > 0.0f
									  ? FString::Printf(TEXT("%s reserved."), *Number(Reserved))
									  : FString());
	}

	if (Stat == TEXT("max_mana"))
	{
		return FCataclysmStatLine(Name, PoolValue(ASC, FVital::GetManaAttribute(), Stat));
	}

	if (Stat == TEXT("max_energy_shield"))
	{
		// AND THE TEMPORARY ABSORB, said beside the shield and not as part of it.
		// The project owner, 2026-10-07: it is separate from the energy shield.
		const UCataclysmAbilitySystemComponent* Absorbing =
			Cast<UCataclysmAbilitySystemComponent>(ASC);
		return FCataclysmStatLine(
			Name, PoolValue(ASC, FVital::GetEnergyShieldAttribute(), Stat),
			TemporaryAbsorbNote(Absorbing ? Absorbing->TemporaryAbsorbHeld() : 0.0f));
	}

	if (Stat == TEXT("class_resource"))
	{
		return FCataclysmStatLine(
			Name, PoolValue(ASC, FResource::GetClassResourceAttribute(), Stat));
	}

	// -- the eight resistances, which show three numbers rather than one
	if (const FName Type = DamageTypeFromStat(Stat, ResistancePrefix);
		Type != NAME_None)
	{
		// THE GENERIC SHARE AND THE TYPE'S OWN, AS `FigureFor` READS THEM, which
		// says why both.
		const float Held = FigureFor(Stat, ASC);
		const float Penalty =
			UCataclysmDamageCalculation::ResistancePenaltyAt(DifficultyTier);
		// UNDER THE CHARACTER'S OWN CAP, the one `Resolve` holds a hit to.
		// Issue #1833.
		const float Met = UCataclysmDamageCalculation::EffectiveResistanceUnderCap(
			Held - Penalty, /*Penetration=*/0.0f,
			UCataclysmDamageCalculation::ResistanceCapOf(ASC));

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
			// THE CHARACTER'S OWN SHARE, since issue #1833 group E part 2 made it a
			// stat: 50 unless a worn row raises it, and never above 85.
			FString::Printf(TEXT("A block removes %s of a hit, not all of it."),
							*Percent(UCataclysmDamageCalculation::BlockShareOf(
								ASC, FCataclysmBlowContext()))));
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
			Percent(FigureFor(Stat, ASC)),
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
	return FCataclysmStatLine(Name, FigureText(Stat, FigureFor(Stat, ASC)));
}

float UCataclysmCharacterSheetLayout::FigureFor(const FString& Stat,
											   const UAbilitySystemComponent* ASC)
{
	// -- the four pools: the maximum, which is what gear moves
	if (Stat == TEXT("max_health"))
	{
		return Read(ASC, FVital::GetMaxHealthAttribute());
	}
	if (Stat == TEXT("max_mana"))
	{
		return Read(ASC, FVital::GetMaxManaAttribute());
	}
	if (Stat == TEXT("max_energy_shield"))
	{
		return Read(ASC, FVital::GetMaxEnergyShieldAttribute());
	}
	if (Stat == TEXT("class_resource"))
	{
		return Read(ASC, FResource::GetMaxClassResourceAttribute());
	}

	// -- a resistance: the generic share and the type's own
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

		// THROUGH `TypedResistanceOf`, AS A BLOW READS IT, since 2026-10-12: a
		// resistance a worn row is holding at nought shows as nought here for as
		// long as it is held, which is the visible part of that row.
		return Generic + UCataclysmDamageCalculation::TypedResistanceOf(ASC, Type);
	}

	// -- cooldown reduction: the percentage shown, not the sum the attribute holds
	if (Stat == TEXT("cooldown_reduction"))
	{
		return FCombat::DisplayedCooldownReduction(ReadStat(ASC, Stat));
	}

	return ReadStat(ASC, Stat);
}

FString UCataclysmCharacterSheetLayout::FigureText(const FString& Stat, float Figure)
{
	return IsPercentage(Stat) ? Percent(Figure) : Number(Figure);
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

// ---------------------------------------------------------------------------
// What hovering an attribute says. Issue #2815.
// ---------------------------------------------------------------------------

const TCHAR* UCataclysmCharacterSheetLayout::AttributeIncreaseSentence =
	TEXT("An increase is a share of what your class, weapon and items already give, added to your items' own "
		 "increases. A stat you have none of gains nothing.");

namespace
{
	/** A percentage as the tooltip writes it: up to two decimal places, none it does not need. "2", "0.5", "0.01". */
	FString TooltipPercent(float Value)
	{
		FString Text = FString::Printf(TEXT("%.2f"), Value);
		while (Text.Contains(TEXT(".")) && (Text.EndsWith(TEXT("0")) || Text.EndsWith(TEXT("."))))
		{
			Text.LeftChopInline(1);
		}
		return Text + TEXT("%");
	}

	/** "a", "a and b", "a, b and c". */
	FString JoinedWithAnd(const TArray<FString>& Parts)
	{
		if (Parts.Num() <= 1)
		{
			return Parts.IsEmpty() ? FString() : Parts[0];
		}
		return FString::Join(TArrayView<const FString>(Parts.GetData(), Parts.Num() - 1), TEXT(", "))
			+ TEXT(" and ") + Parts.Last();
	}

	/** "Spirit". */
	FString Capitalised(const FString& Word)
	{
		return Word.IsEmpty() ? Word : Word.Left(1).ToUpper() + Word.Mid(1);
	}

	/** What a minion scaling row's tag is called in words: the two families the design names. */
	FString MinionFamilyWords(const FString& RequiresTag)
	{
		if (RequiresTag.Equals(TEXT("Minion.Creature"), ESearchCase::IgnoreCase))
		{
			return TEXT("summoned creatures");
		}
		if (RequiresTag.Equals(TEXT("Minion.Machine"), ESearchCase::IgnoreCase))
		{
			return TEXT("deployed machines");
		}
		return FString::Printf(TEXT("minions tagged %s"), *RequiresTag);
	}

	/** Every row of `Table` for `Attribute`, in the table's order. */
	template <typename RowType>
	TArray<const RowType*> RowsFor(const UDataTable* Table, const FString& Attribute)
	{
		TArray<const RowType*> Rows;
		if (Table)
		{
			for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
			{
				const RowType* Row = reinterpret_cast<const RowType*>(Pair.Value);
				if (Row->Attribute.Equals(Attribute, ESearchCase::IgnoreCase))
				{
					Rows.Add(Row);
				}
			}
		}
		return Rows;
	}
}

FString UCataclysmCharacterSheetLayout::AttributeStatWords(const FString& Stat)
{
	if (Stat == TEXT("dot_frequency"))
	{
		return TEXT("damage over time tick rate");
	}
	return NameFor(Stat).ToLower();
}

FString UCataclysmCharacterSheetLayout::AttributePerPointText(const FString& Attribute)
{
	TArray<FString> Parts;
	for (const FCataclysmAttributeEffectRow* Row :
		 RowsFor<FCataclysmAttributeEffectRow>(UCataclysmPlayerClassStats::LoadAttributeTable(), Attribute))
	{
		Parts.Add(FString::Printf(TEXT("%s increased %s"), *TooltipPercent(Row->PercentPerPoint),
								  *AttributeStatWords(Row->Stat)));
	}
	FString Text = FString::Printf(TEXT("Each point: %s."), *JoinedWithAnd(Parts));
	for (const FCataclysmMinionScalingRow* Row :
		 RowsFor<FCataclysmMinionScalingRow>(UCataclysmPlayerClassStats::LoadMinionScalingTable(), Attribute))
	{
		Text += Row->Stat == TEXT("damage")
			? FString::Printf(TEXT(" Your %s deal %s increased damage."), *MinionFamilyWords(Row->RequiresTag),
							  *TooltipPercent(Row->PercentPerPoint))
			: FString::Printf(TEXT(" Your %s have %s increased %s."), *MinionFamilyWords(Row->RequiresTag),
							  *TooltipPercent(Row->PercentPerPoint), *AttributeStatWords(Row->Stat));
	}
	return Text;
}

FString UCataclysmCharacterSheetLayout::AttributeTooltipFor(const FString& Attribute,
															 const UAbilitySystemComponent* ASC, int32 SpentPoints)
{
	const FString Name = Capitalised(Attribute);
	TArray<FString> Lines;
	Lines.Add(Name);
	Lines.Add(AttributePerPointText(Attribute));
	Lines.Add(AttributeIncreaseSentence);

	// WHAT THE STATS READ IS THE ATTRIBUTE'S VALUE, rounded and raised by items, not the points spent.
	const int32 Value = FMath::RoundToInt(ReadStat(ASC, Attribute));
	if (Value <= 0)
	{
		Lines.Add(FString::Printf(TEXT("Now: no %s."), *Name));
		return FString::Join(Lines, TEXT("\n"));
	}
	Lines.Add(Value != SpentPoints
		? FString::Printf(TEXT("Now, at %d %s (%d spent, %d from items):"), Value, *Name, SpentPoints,
						  Value - SpentPoints)
		: FString::Printf(TEXT("Now, at %d %s:"), Value, *Name));

	const UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(ASC);
	for (const FCataclysmAttributeEffectRow* Row :
		 RowsFor<FCataclysmAttributeEffectRow>(UCataclysmPlayerClassStats::LoadAttributeTable(), Attribute))
	{
		const FString Stat = Row->Stat;
		const FCataclysmStatInputs* Inputs = Cataclysm ? Cataclysm->GetStatInputs(FName(*Stat)) : nullptr;
		TArray<FCataclysmStatModifier> Without;
		float FromAttribute = 0.0f;
		if (Inputs)
		{
			for (const FCataclysmStatModifier& Modifier : Inputs->Modifiers)
			{
				if (Modifier.Source == ECataclysmModifierSource::Attribute)
				{
					FromAttribute += Modifier.Value;
				}
				else
				{
					Without.Add(Modifier);
				}
			}
		}
		const FCataclysmStatBreakdown WithIt = Inputs
			? UCataclysmStatPipeline::Evaluate(Inputs->Base, Inputs->Modifiers, FGameplayTagContainer())
			: FCataclysmStatBreakdown();
		const FString StatName = NameFor(Stat);

		// NOTHING TO INCREASE: no base and no flat amount, so the increase multiplies nothing. Ruled by the owner on
		// 2026-11-21, "Keep it, just explain it". A stat with nothing recorded is read from the sheet's figure. A gem's
		// tooltip asks the same of `UCataclysmGems::AmountToIncreaseFor` (issue #2835), which reads the attribute where
		// this reads the figure; the coordinating session ruled on 2026-11-22 that the sheet stays as it is.
		const float Underlying = Inputs ? WithIt.Base + WithIt.Flat : FigureFor(Stat, ASC);
		if (Underlying <= 0.0f)
		{
			Lines.Add(FString::Printf(TEXT("%s: you have none, from your class or your items, so %s does nothing to it yet."),
									  *StatName, *Name));
			continue;
		}

		// THE "TO" FIGURE IS THE SHEET'S, AND THE "FROM" IS IT SCALED BY WHAT THE PIPELINE GIVES WITHOUT THE
		// ATTRIBUTE, so the line shows the same figure the sheet row beside it shows.
		const float WithoutFinal = Inputs
			? UCataclysmStatPipeline::Evaluate(Inputs->Base, Without, FGameplayTagContainer()).Final
			: WithIt.Final;
		const float Share = Inputs && WithIt.Final != 0.0f ? WithoutFinal / WithIt.Final : 1.0f;
		const float To = FigureFor(Stat, ASC);
		const float From = Stat == TEXT("cooldown_reduction")
			? FCombat::DisplayedCooldownReduction(ReadStat(ASC, Stat) * Share)
			: To * Share;
		Lines.Add(FString::Printf(TEXT("%s: %s to %s (%s increased from %s)"), *StatName, *FigureText(Stat, From),
								  *FigureText(Stat, To), *TooltipPercent(FromAttribute), *Name));
	}

	for (const FCataclysmMinionScalingRow* Row :
		 RowsFor<FCataclysmMinionScalingRow>(UCataclysmPlayerClassStats::LoadMinionScalingTable(), Attribute))
	{
		Lines.Add(FString::Printf(TEXT("%s: %s increased %s from %s"), *Capitalised(MinionFamilyWords(Row->RequiresTag)),
								  *TooltipPercent(Row->PercentPerPoint * Value), *AttributeStatWords(Row->Stat), *Name));
	}
	return FString::Join(Lines, TEXT("\n"));
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
