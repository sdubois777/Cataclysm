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
	//
	// AND ONE ON 2026-10-06, the owner's approved reword: "Nearby enemies gain
	// 20%-40% resistances" became "Enemies within 5 metres gain 20%-40%
	// resistances", so that its row could state the distance it acts on.
	static const TMap<FName, FName> Table = {
		{TEXT("Negative_Nearby_enemies_gain_20_40_resistances"),
		 TEXT("Negative_Enemies_within_5_metres_gain_20_40_resistances")},
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
		 TEXT("Positive_Your_critical_strikes_always_cause_bleeding")},
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
		// THE THIRTEEN REWORDS the owner approved on 2026-09-30, from the #1833
		// survey: sentences that counted stacks the one-stack rule does not allow,
		// or that said something the engine already does. One of them renames a
		// row the "critical strikes" pass had renamed already; its earlier line
		// above now points at the new name too, so nothing chains.
		{TEXT("Positive_Poison_stacks_on_an_enemy_reduce_their_damage_ou"),
		 TEXT("Positive_Poisoned_enemies_deal_2_4_less_damage")},
		{TEXT("Positive_Enemies_with_5_or_more_bleed_stacks_take_20_40"),
		 TEXT("Positive_Bleeding_enemies_take_20_40_increased_damage_f")},
		{TEXT("Positive_Critical_strikes_apply_2_4_additional_bleed_stac"),
		 TEXT("Positive_Your_critical_strikes_always_cause_bleeding")},
		{TEXT("Positive_When_an_enemy_dies_with_bleed_stacks_the_stacks"),
		 TEXT("Positive_When_a_bleeding_enemy_dies_its_bleed_spreads_to")},
		{TEXT("Positive_Enemies_with_5_or_more_poison_stacks_are_slowed"),
		 TEXT("Positive_Poisoned_enemies_are_slowed_by_30_50")},
		{TEXT("Positive_Your_bleed_stacks_also_reduce_enemy_movement_spe"),
		 TEXT("Positive_Bleeding_enemies_move_5_10_slower")},
		{TEXT("Positive_Every_time_you_apply_a_DOT_instead_add_2_4_stac"),
		 TEXT("Positive_Your_damage_over_time_effects_deal_50_100_more")},
		{TEXT("Positive_Necrosis_stacks_reduce_enemy_maximum_HP_by_1_2"),
		 TEXT("Positive_Enemies_with_Necrosis_have_1_2_less_maximum_he")},
		{TEXT("Positive_Each_void_splinter_stack_on_an_enemy_increases_y"),
		 TEXT("Positive_You_deal_3_5_more_damage_to_an_enemy_carrying")},
		{TEXT("Positive_Your_energy_shield_absorbs_10_20_of_HP_damage"),
		 TEXT("Positive_10_20_of_bleed_damage_you_take_is_taken_from_y")},
		{TEXT("Positive_Your_movement_abilities_no_longer_share_a_cooldo"),
		 TEXT("Positive_Your_movement_ability_has_2_charges")},
		{TEXT("Positive_Your_melee_skills_have_20_40_increased_reach"),
		 TEXT("Positive_Your_melee_skills_have_0_5_1_metre_reach")},
		{TEXT("Negative_You_cannot_cure_or_reduce_bleed_stacks_on_yourse"),
		 TEXT("Negative_Bleeding_on_you_lasts_50_100_longer")},
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
