// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmEnchantmentRenames.h"

#include "Items/CataclysmItem.h"
#include "UObject/UnrealType.h"

const TMap<FName, FName>& FCataclysmEnchantmentRenames::Aliases()
{
	// OLD NAME, THEN THE ROW IT MEANS NOW. One line per renamed row.
	//
	// THE FIFTEEN OF b628582e, the pass that made "critical hits" read "critical
	// strikes" (#660). Then the three of 10243aa6 (#1512): a number inside the
	// first 48 characters of the retaliation row, and the two class-point rows
	// reworded. Rebuilt on 2026-09-30 by walking both enchantment tables'
	// history and pairing rows by position.
	//
	// NOT HERE, AND ON PURPOSE: the two rows deleted with no successor, in
	// b620c049 (block against area damage) and 261a4b2a (the two minion-count
	// rows merged). There is no row for their names to mean.
	static const TMap<FName, FName> Table = {
		{TEXT("Positive_Critical_hits_grant_a_stack_of_power_increasing"),
		 TEXT("Positive_Critical_strikes_grant_a_stack_of_power_increasi")},
		{TEXT("Positive_Your_first_critical_hit_against_each_enemy_deals"),
		 TEXT("Positive_Your_first_critical_strike_against_each_enemy_de")},
		{TEXT("Positive_When_your_class_resource_is_full_critical_hit_c"),
		 TEXT("Positive_When_your_class_resource_is_full_critical_strik")},
		{TEXT("Positive_Spell_critical_hits_deal_50_100_increased_dama"),
		 TEXT("Positive_Spell_critical_strikes_deal_50_100_increased_d")},
		{TEXT("Positive_Critical_hit_chance_is_increased_by_20_40_agai"),
		 TEXT("Positive_Critical_strike_chance_is_increased_by_20_40_a")},
		{TEXT("Positive_Critical_hits_apply_2_4_additional_bleed_stacks"),
		 TEXT("Positive_Critical_strikes_apply_2_4_additional_bleed_stac")},
		{TEXT("Positive_Your_critical_hits_ignore_20_40_of_enemy_armor"),
		 TEXT("Positive_Your_critical_strikes_ignore_20_40_of_enemy_ar")},
		{TEXT("Positive_Critical_hits_restore_2_4_of_your_maximum_HP"),
		 TEXT("Positive_Critical_strikes_restore_2_4_of_your_maximum_H")},
		{TEXT("Positive_Critical_hits_apply_a_random_DoT_to_the_target"),
		 TEXT("Positive_Critical_strikes_apply_a_random_DoT_to_the_targe")},
		{TEXT("Positive_Critical_hits_have_a_15_30_chance_to_reset_you"),
		 TEXT("Positive_Critical_strikes_have_a_15_30_chance_to_reset")},
		{TEXT("Negative_Critical_hits_trigger_a_0_5_1_second_global_cool"),
		 TEXT("Negative_Critical_strikes_trigger_a_0_5_1_second_global_c")},
		{TEXT("Negative_Non_critical_hits_deal_20_35_less_damage"),
		 TEXT("Negative_Non_critical_strikes_deal_20_35_less_damage")},
		{TEXT("Negative_Your_critical_hit_chance_cannot_exceed_30_50"),
		 TEXT("Negative_Your_critical_strike_chance_cannot_exceed_30_50")},
		{TEXT("Negative_Critical_hits_drain_3_6_of_your_current_HP"),
		 TEXT("Negative_Critical_strikes_drain_3_6_of_your_current_HP")},
		{TEXT("Negative_Critical_hits_have_a_20_35_chance_to_trigger_a"),
		 TEXT("Negative_Critical_strikes_have_a_20_35_chance_to_trigge")},
		{TEXT("Positive_Your_retaliation_damage_is_increased_by_5_10_f"),
		 TEXT("Positive_Your_retaliation_damage_is_increased_by_2_4_fo")},
		{TEXT("Negative_Your_skills_deal_5_10_less_damage_for_every_cl"),
		 TEXT("Negative_Your_skills_deal_1_5_2_5_less_damage_for_every")},
		{TEXT("Negative_Each_class_point_above_50_reduces_your_maximum_H"),
		 TEXT("Negative_Your_maximum_HP_is_reduced_by_1_5_2_5_for_ever")},
	};
	return Table;
}

FName FCataclysmEnchantmentRenames::Current(FName Stored)
{
	const FName* Now = Aliases().Find(Stored);
	return Now ? *Now : Stored;
}

int32 FCataclysmEnchantmentRenames::RenameInStruct(const UStruct* Struct, void* Memory)
{
	if (!Struct || !Memory)
	{
		return 0;
	}

	// A ROLLED ENCHANTMENT ITSELF: its two names, and nothing below them.
	if (Struct == FCataclysmRolledEnchantment::StaticStruct())
	{
		FCataclysmRolledEnchantment& Rolled = *static_cast<FCataclysmRolledEnchantment*>(Memory);
		int32 Changed = 0;
		for (FName* Name : {&Rolled.Positive, &Rolled.Negative})
		{
			const FName Now = Current(*Name);
			if (Now != *Name)
			{
				*Name = Now;
				++Changed;
			}
		}
		return Changed;
	}

	// ANY OTHER STRUCT: every struct field, and every array of structs.
	int32 Changed = 0;
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		if (const FStructProperty* Inner = CastField<FStructProperty>(*It))
		{
			for (int32 Index = 0; Index < Inner->ArrayDim; ++Index)
			{
				Changed += RenameInStruct(Inner->Struct,
										  Inner->ContainerPtrToValuePtr<void>(Memory, Index));
			}
		}
		else if (const FArrayProperty* Array = CastField<FArrayProperty>(*It))
		{
			const FStructProperty* Element = CastField<FStructProperty>(Array->Inner);
			if (!Element)
			{
				continue;
			}
			FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Memory));
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				Changed += RenameInStruct(Element->Struct, Helper.GetRawPtr(Index));
			}
		}
	}
	return Changed;
}

int32 FCataclysmEnchantmentRenames::RenameIn(UObject* Object)
{
	return Object ? RenameInStruct(Object->GetClass(), Object) : 0;
}
