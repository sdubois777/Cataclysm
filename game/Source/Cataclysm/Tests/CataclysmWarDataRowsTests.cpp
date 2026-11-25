// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"

/**
 * SLICE D OF `plan_death_and_war.md`, 2026-11-24: the War rows that need no code, read from the generated tables.
 *
 * Forty-eight rows on thirty War nodes, written into the design workbook by `tools/reapply_war_data_rows_workbook.py`.
 * Each is a stat, a condition and a scale the game already reads, so nothing here is new machinery; what these tests
 * hold is that the rows as authored reach a character's modifiers the way their sentences say: a threshold row from its
 * threshold and once (or per point, where its sentence says so), a scale with its step and cap, and an Oath's option
 * only when it is the one chosen. The generator's half, that each value and step is the one the sentence states, is
 * `tools/tests/test_passive_effects_match_the_node_text.py`.
 *
 * THESE READ THE GENERATED `DT_PassiveEffects`, so they fail until the editor step has rebuilt it from
 * `game/Data/PassiveEffects.csv`, and say so.
 */

namespace CataclysmWarDataRowsTest
{
	/** What one node grants at this many points, with this option chosen (0 for none), to a War character. */
	TMap<FName, TArray<FCataclysmStatModifier>> GrantedBy(const UDataTable* Nodes, const UDataTable* Effects,
														  const TCHAR* Node, int32 Points, int32 Option = 0)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(Node), Points);
		if (Option > 0)
		{
			Allocation.SetChosenOption(FName(Node), Option);
		}
		const TArray<FName> War = {FName(TEXT("War"))};
		return UCataclysmPassiveTree::ModifiersFor(Allocation, Nodes, Effects, War);
	}

	/** The modifiers on one stat in one bucket. */
	TArray<FCataclysmStatModifier> OnStat(const TMap<FName, TArray<FCataclysmStatModifier>>& Granted,
										  const TCHAR* Stat, ECataclysmStatBucket Bucket)
	{
		TArray<FCataclysmStatModifier> Found;
		if (const TArray<FCataclysmStatModifier>* All = Granted.Find(FName(Stat)))
		{
			for (const FCataclysmStatModifier& Each : *All)
			{
				if (Each.Bucket == Bucket)
				{
					Found.Add(Each);
				}
			}
		}
		return Found;
	}

	/** Their values summed. */
	float Summed(const TArray<FCataclysmStatModifier>& Modifiers)
	{
		float Sum = 0.0f;
		for (const FCataclysmStatModifier& Each : Modifiers)
		{
			Sum += Each.Value;
		}
		return Sum;
	}

	/** The two generated tables, or false with the command that rebuilds them. */
	bool LoadTables(FAutomationTestBase& Test, const UDataTable*& Nodes, const UDataTable*& Effects)
	{
		Nodes = UCataclysmPassiveTree::LoadNodeTable();
		Effects = UCataclysmPassiveTree::LoadEffectTable();
		if (!Test.TestNotNull(TEXT("set-up: the passive node table loads"), Nodes)
			|| !Test.TestNotNull(TEXT("set-up: the passive effect table loads"), Effects))
		{
			Test.AddError(TEXT("Run  python tools/run_editor_python.py tools/generate_datatable_assets.py"));
			return false;
		}
		// THE ROWS ARE IN THE TABLE AT ALL. Without this, a table older than slice D fails every check below with a
		// figure of nought, which reads as a fault in the counting rather than a table to rebuild.
		if (UCataclysmPassiveTree::EffectsFor(Effects, FName(TEXT("Bulwark_basic_tw_t3_001"))).Num() == 0)
		{
			Test.AddError(TEXT("Aura of Pain has no row in DT_PassiveEffects. If it has one in game/Data/PassiveEffects.csv, "
							   "the asset is older than the CSV: run  python tools/run_editor_python.py "
							   "tools/generate_datatable_assets.py"));
			return false;
		}
		return true;
	}
}

// ---------------------------------------------------------------------------
// Thresholds
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarDataRowsThresholdsTest,
	"Cataclysm.WarDataRows.ThresholdRowsPayFromTheirThresholdAsTheirSentencesSay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Bloodthirst, "+1% life leech on all melee attacks for every 3 points": two rows, from 3 and from 6, each paid once,
 * so 1 at 3 points and 2 at 8. Pain is Power's "At 5 points: This bonus also applies to retaliation damage": nothing at
 * 4, every point from 5. Aura of Pain's "Enemies within 3 meters": 3 metres at one point and at eight. Iron
 * Constitution's "At 5 points: Gain HP equal to 10% of your Armor value" and Walking Fortress's "At 6 points": nothing
 * below, one armour-scaled or health-scaled row from. THE CONTROLS are each node's per-point row, which grows with the
 * points: it shows the points reached the node, so a threshold row's nought is the threshold's doing.
 */
bool FCataclysmWarDataRowsThresholdsTest::RunTest(const FString&)
{
	using namespace CataclysmWarDataRowsTest;
	const UDataTable* Nodes = nullptr;
	const UDataTable* Effects = nullptr;
	if (!LoadTables(*this, Nodes, Effects))
	{
		return false;
	}

	// BLOODTHIRST.
	const TCHAR* Bloodthirst = TEXT("Berserker_trunk_002");
	TestEqual(TEXT("Bloodthirst at 2 points grants no life leech"),
			  OnStat(GrantedBy(Nodes, Effects, Bloodthirst, 2), TEXT("life_leech"), ECataclysmStatBucket::Flat).Num(), 0);
	TestEqual(TEXT("Bloodthirst at 3 points grants 1% life leech"),
			  Summed(OnStat(GrantedBy(Nodes, Effects, Bloodthirst, 3), TEXT("life_leech"), ECataclysmStatBucket::Flat)),
			  1.0f, 0.001f);
	TestEqual(TEXT("Bloodthirst at 5 points still grants 1%"),
			  Summed(OnStat(GrantedBy(Nodes, Effects, Bloodthirst, 5), TEXT("life_leech"), ECataclysmStatBucket::Flat)),
			  1.0f, 0.001f);
	const TArray<FCataclysmStatModifier> AtEight =
		OnStat(GrantedBy(Nodes, Effects, Bloodthirst, 8), TEXT("life_leech"), ECataclysmStatBucket::Flat);
	TestEqual(TEXT("Bloodthirst at 8 points grants 2%, not the 2.4 a 0.3 a point would"), Summed(AtEight), 2.0f, 0.001f);
	const FGameplayTag Melee = FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Melee")), /*ErrorIfNotFound=*/false);
	for (const FCataclysmStatModifier& Each : AtEight)
	{
		TestTrue(TEXT("each Bloodthirst row asks for a melee blow"), Each.RequiredTags.HasTagExact(Melee));
	}

	// PAIN IS POWER. The attack damage row is per point from the first: the control.
	const TCHAR* PainIsPower = TEXT("Bulwark_basic_tw_t2_003");
	TestEqual(TEXT("Pain is Power at 4 points: 8% attack damage, per point (the control)"),
			  Summed(OnStat(GrantedBy(Nodes, Effects, PainIsPower, 4), TEXT("attack_damage"),
							ECataclysmStatBucket::Increased)), 8.0f, 0.001f);
	TestEqual(TEXT("Pain is Power at 4 points grants no retaliation"),
			  OnStat(GrantedBy(Nodes, Effects, PainIsPower, 4), TEXT("retaliation"),
					 ECataclysmStatBucket::Increased).Num(), 0);
	TestEqual(TEXT("Pain is Power at 5 points: 10% retaliation, every point from the first"),
			  Summed(OnStat(GrantedBy(Nodes, Effects, PainIsPower, 5), TEXT("retaliation"),
							ECataclysmStatBucket::Increased)), 10.0f, 0.001f);
	TestEqual(TEXT("Pain is Power at 8 points: 16% retaliation"),
			  Summed(OnStat(GrantedBy(Nodes, Effects, PainIsPower, 8), TEXT("retaliation"),
							ECataclysmStatBucket::Increased)), 16.0f, 0.001f);

	// AURA OF PAIN. Its retaliation is per point: the control.
	const TCHAR* AuraOfPain = TEXT("Bulwark_basic_tw_t3_001");
	for (const int32 Points : {1, 8})
	{
		const TMap<FName, TArray<FCataclysmStatModifier>> Granted = GrantedBy(Nodes, Effects, AuraOfPain, Points);
		TestEqual(*FString::Printf(TEXT("Aura of Pain at %d points: a 3 metre radius"), Points),
				  Summed(OnStat(Granted, TEXT("retaliation_radius_metres"), ECataclysmStatBucket::Flat)), 3.0f, 0.001f);
		TestEqual(*FString::Printf(TEXT("Aura of Pain at %d points: 5%% retaliation a point (the control)"), Points),
				  Summed(OnStat(Granted, TEXT("retaliation"), ECataclysmStatBucket::Increased)),
				  5.0f * static_cast<float>(Points), 0.001f);
	}

	// IRON CONSTITUTION. Its 2% maximum health a point is the control.
	const TCHAR* IronConstitution = TEXT("Bulwark_basic_trunk_002");
	const TMap<FName, TArray<FCataclysmStatModifier>> IronAtFour = GrantedBy(Nodes, Effects, IronConstitution, 4);
	TestEqual(TEXT("Iron Constitution at 4 points: 8% maximum health (the control)"),
			  Summed(OnStat(IronAtFour, TEXT("max_health"), ECataclysmStatBucket::Increased)), 8.0f, 0.001f);
	TestEqual(TEXT("Iron Constitution at 4 points grants no health from armour"),
			  OnStat(IronAtFour, TEXT("max_health"), ECataclysmStatBucket::Flat).Num(), 0);
	const TArray<FCataclysmStatModifier> FromArmour =
		OnStat(GrantedBy(Nodes, Effects, IronConstitution, 8), TEXT("max_health"), ECataclysmStatBucket::Flat);
	if (TestEqual(TEXT("Iron Constitution at 8 points: one health-from-armour row, paid once"), FromArmour.Num(), 1))
	{
		TestEqual(TEXT("it grants 1 maximum health"), FromArmour[0].Value, 1.0f, 0.001f);
		TestTrue(TEXT("for every point of armour"), FromArmour[0].Scale == ECataclysmStatScale::PerPointOfArmor);
		TestEqual(TEXT("in steps of 10, which is 10% of the armour"), FromArmour[0].ScaleStep, 10.0f, 0.001f);
	}

	// WALKING FORTRESS. Its 40 flat maximum health a point is the control.
	const TCHAR* WalkingFortress = TEXT("Bulwark_basic_jg_final_001");
	const TMap<FName, TArray<FCataclysmStatModifier>> FortressAtFive = GrantedBy(Nodes, Effects, WalkingFortress, 5);
	TestEqual(TEXT("Walking Fortress at 5 points: 200 flat maximum health (the control)"),
			  Summed(OnStat(FortressAtFive, TEXT("max_health"), ECataclysmStatBucket::Flat)), 200.0f, 0.001f);
	TestEqual(TEXT("Walking Fortress at 5 points grants no damage reduction"),
			  OnStat(FortressAtFive, TEXT("damage_reduction"), ECataclysmStatBucket::More).Num(), 0);
	const TArray<FCataclysmStatModifier> Fortified =
		OnStat(GrantedBy(Nodes, Effects, WalkingFortress, 8), TEXT("damage_reduction"), ECataclysmStatBucket::More);
	if (TestEqual(TEXT("Walking Fortress at 8 points: one damage reduction row, paid once"), Fortified.Num(), 1))
	{
		TestEqual(TEXT("it grants 1% more damage reduction"), Fortified[0].Value, 1.0f, 0.001f);
		TestTrue(TEXT("for every point of maximum health"),
				 Fortified[0].Scale == ECataclysmStatScale::PerPointOfMaximumHealth);
		TestEqual(TEXT("in steps of 5,000"), Fortified[0].ScaleStep, 5000.0f, 0.001f);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Scales and caps
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarDataRowsScalesTest,
	"Cataclysm.WarDataRows.ScaledRowsCarryTheirStepAndCapIntoTheModifier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Saturation, "+5% evasion chance for each of your active traps, up to 5 traps": a cap of 5 steps on the modifier. THE
 * CONTROL is Crushing Presence's "+1 flat damage per point for every 1,000 Maximum HP", a scaled row with no cap, whose
 * modifier carries none. And the two scales the War rulings put on maximum health, at 1,000 and 5,000, and the
 * gadget count Army of One reads four times: its "+15% damage" is attack damage and spell damage, by the owner's
 * ruling of 2026-08-25 that plain damage on a passive node is both.
 */
bool FCataclysmWarDataRowsScalesTest::RunTest(const FString&)
{
	using namespace CataclysmWarDataRowsTest;
	const UDataTable* Nodes = nullptr;
	const UDataTable* Effects = nullptr;
	if (!LoadTables(*this, Nodes, Effects))
	{
		return false;
	}

	const TArray<FCataclysmStatModifier> Saturation =
		OnStat(GrantedBy(Nodes, Effects, TEXT("Saboteur_basic_trap_qty_003"), 1), TEXT("evasion"),
			   ECataclysmStatBucket::Flat);
	if (TestEqual(TEXT("Saturation: one evasion row"), Saturation.Num(), 1))
	{
		TestEqual(TEXT("5% evasion"), Saturation[0].Value, 5.0f, 0.001f);
		TestTrue(TEXT("for each active trap"), Saturation[0].Scale == ECataclysmStatScale::PerTrapActive);
		TestEqual(TEXT("one trap a step"), Saturation[0].ScaleStep, 1.0f, 0.001f);
		TestEqual(TEXT("up to 5 traps"), Saturation[0].ScaleMaxSteps, 5);
	}

	const TArray<FCataclysmStatModifier> Crushing =
		OnStat(GrantedBy(Nodes, Effects, TEXT("Bulwark_basic_jg_stem_003"), 3), TEXT("attack_damage"),
			   ECataclysmStatBucket::Flat);
	if (TestEqual(TEXT("Crushing Presence: one flat damage row"), Crushing.Num(), 1))
	{
		TestEqual(TEXT("1 flat damage a point, 3 at 3 points"), Crushing[0].Value, 3.0f, 0.001f);
		TestTrue(TEXT("for every point of maximum health"),
				 Crushing[0].Scale == ECataclysmStatScale::PerPointOfMaximumHealth);
		TestEqual(TEXT("in steps of 1,000"), Crushing[0].ScaleStep, 1000.0f, 0.001f);
		TestEqual(TEXT("with no cap (the control)"), Crushing[0].ScaleMaxSteps, 0);
	}

	const TArray<FCataclysmStatModifier> Weight =
		OnStat(GrantedBy(Nodes, Effects, TEXT("Bulwark_basic_jg_t2_002"), 2), TEXT("attack_damage"),
			   ECataclysmStatBucket::More);
	if (TestEqual(TEXT("Weight of the World: one More damage row"), Weight.Num(), 1))
	{
		TestEqual(TEXT("1% a point, 2 at 2 points"), Weight[0].Value, 2.0f, 0.001f);
		TestEqual(TEXT("in steps of 5,000 maximum health"), Weight[0].ScaleStep, 5000.0f, 0.001f);
	}

	const TArray<FCataclysmStatModifier> MeatShield =
		OnStat(GrantedBy(Nodes, Effects, TEXT("Bulwark_basic_jg_stem_001"), 1), TEXT("class_resource"),
			   ECataclysmStatBucket::Flat);
	if (TestEqual(TEXT("Meat Shield: one maximum Fervour row"), MeatShield.Num(), 1))
	{
		TestEqual(TEXT("3 maximum Fervour"), MeatShield[0].Value, 3.0f, 0.001f);
		TestTrue(TEXT("for every point of maximum health"),
				 MeatShield[0].Scale == ECataclysmStatScale::PerPointOfMaximumHealth);
		TestEqual(TEXT("in steps of 1,000"), MeatShield[0].ScaleStep, 1000.0f, 0.001f);
	}

	const TMap<FName, TArray<FCataclysmStatModifier>> Army =
		GrantedBy(Nodes, Effects, TEXT("Saboteur_keystone_gadget_deep_002"), 1);
	struct FArmyRow
	{
		const TCHAR* Stat;
		ECataclysmStatBucket Bucket;
		float Value;
	};
	for (const FArmyRow& Each : {FArmyRow{TEXT("attack_damage"), ECataclysmStatBucket::Increased, 15.0f},
								 FArmyRow{TEXT("spell_damage"), ECataclysmStatBucket::Increased, 15.0f},
								 FArmyRow{TEXT("evasion"), ECataclysmStatBucket::Flat, 8.0f},
								 FArmyRow{TEXT("class_resource"), ECataclysmStatBucket::Flat, 15.0f}})
	{
		const TArray<FCataclysmStatModifier> Found = OnStat(Army, Each.Stat, Each.Bucket);
		if (TestEqual(*FString::Printf(TEXT("Army of One: one %s row"), Each.Stat), Found.Num(), 1))
		{
			TestEqual(*FString::Printf(TEXT("Army of One's %s"), Each.Stat), Found[0].Value, Each.Value, 0.001f);
			TestTrue(*FString::Printf(TEXT("Army of One's %s is for each active gadget"), Each.Stat),
					 Found[0].Scale == ECataclysmStatScale::PerGadgetActive);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Oaths
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarDataRowsOathsTest,
	"Cataclysm.WarDataRows.EachWarOathOptionGrantsItsOwnRowAndNoOther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Bulwark's Second Oath: Iron Aegis, "Your Block Chance is doubled while you have not attacked in the last 3
 * seconds", is option 1; Titan's Blood, "Your HP regeneration is doubled", option 2. Each grants its own row and not the
 * other's. THE CONTROLS are the Oath with nothing chosen and with Thornwall, option 3, which has no row: neither grants
 * either. And the Berserker's First Oath, Vital Strikes (option 3): damage reduction for every 1% of life leech while
 * above 50% Fervour, which Warlord (option 1) does not grant.
 */
bool FCataclysmWarDataRowsOathsTest::RunTest(const FString&)
{
	using namespace CataclysmWarDataRowsTest;
	const UDataTable* Nodes = nullptr;
	const UDataTable* Effects = nullptr;
	if (!LoadTables(*this, Nodes, Effects))
	{
		return false;
	}

	const TCHAR* SecondOath = TEXT("Bulwark_capstone_50");
	for (const int32 Option : {0, 3})
	{
		const TMap<FName, TArray<FCataclysmStatModifier>> Granted = GrantedBy(Nodes, Effects, SecondOath, 1, Option);
		TestEqual(*FString::Printf(TEXT("the Bulwark's Second Oath, option %d: no block chance (a control)"), Option),
				  OnStat(Granted, TEXT("block_chance"), ECataclysmStatBucket::More).Num(), 0);
		TestEqual(*FString::Printf(TEXT("the Bulwark's Second Oath, option %d: no regeneration (a control)"), Option),
				  OnStat(Granted, TEXT("health_regen"), ECataclysmStatBucket::More).Num(), 0);
	}

	const TMap<FName, TArray<FCataclysmStatModifier>> IronAegis = GrantedBy(Nodes, Effects, SecondOath, 1, 1);
	const TArray<FCataclysmStatModifier> Doubled = OnStat(IronAegis, TEXT("block_chance"), ECataclysmStatBucket::More);
	if (TestEqual(TEXT("Iron Aegis: one block chance row"), Doubled.Num(), 1))
	{
		TestEqual(TEXT("100% more, which is doubled"), Doubled[0].Value, 100.0f, 0.001f);
		TestTrue(TEXT("while you have not attacked"),
				 Doubled[0].Condition == ECataclysmStatCondition::NotAttackedForSeconds);
		TestEqual(TEXT("in the last 3 seconds"), Doubled[0].ConditionValue, 3.0f, 0.001f);
	}
	TestEqual(TEXT("Iron Aegis grants no regeneration"),
			  OnStat(IronAegis, TEXT("health_regen"), ECataclysmStatBucket::More).Num(), 0);

	const TMap<FName, TArray<FCataclysmStatModifier>> TitansBlood = GrantedBy(Nodes, Effects, SecondOath, 1, 2);
	TestEqual(TEXT("Titan's Blood: 100% more regeneration"),
			  Summed(OnStat(TitansBlood, TEXT("health_regen"), ECataclysmStatBucket::More)), 100.0f, 0.001f);
	TestEqual(TEXT("Titan's Blood grants no block chance"),
			  OnStat(TitansBlood, TEXT("block_chance"), ECataclysmStatBucket::More).Num(), 0);

	const TCHAR* FirstOath = TEXT("Berserker_capstone_25");
	TestEqual(TEXT("the Berserker's First Oath, Warlord: no damage reduction (a control)"),
			  OnStat(GrantedBy(Nodes, Effects, FirstOath, 1, 1), TEXT("damage_reduction"),
					 ECataclysmStatBucket::Flat).Num(), 0);
	const TArray<FCataclysmStatModifier> VitalStrikes =
		OnStat(GrantedBy(Nodes, Effects, FirstOath, 1, 3), TEXT("damage_reduction"), ECataclysmStatBucket::Flat);
	if (TestEqual(TEXT("Vital Strikes: one damage reduction row"), VitalStrikes.Num(), 1))
	{
		TestEqual(TEXT("1 damage reduction"), VitalStrikes[0].Value, 1.0f, 0.001f);
		TestTrue(TEXT("for every 1% of life leech"),
				 VitalStrikes[0].Scale == ECataclysmStatScale::PerPercentOfLifeLeech);
		TestEqual(TEXT("one percent a step"), VitalStrikes[0].ScaleStep, 1.0f, 0.001f);
		TestTrue(TEXT("while above"), VitalStrikes[0].Condition == ECataclysmStatCondition::ClassResourceAbovePercent);
		TestEqual(TEXT("50% Fervour"), VitalStrikes[0].ConditionValue, 50.0f, 0.001f);
	}
	return true;
}

// ---------------------------------------------------------------------------
// The War riders, 2026-11-25
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarDataRowsRidersTest,
	"Cataclysm.WarDataRows.TheRidersCarryASecondConditionAndPayTheirDrainsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The War riders (`tools/reapply_war_riders_workbook.py`): the first rows with a second condition, and two drains
 * paid once rather than per point.
 *
 *   - DEATH BLOW: its crit chance is under an enemy below 25% health AND a two-handed weapon, both carried into the
 *     modifier.
 *   - HAIR-TRIGGER: two rows of 2 a point in Frenzy, the second also above 75% Fervour.
 *   - TITAN'S GRIP: a drain of 4 at one point and at ten (THE CONTROL beside it is its melee damage, 10 a point).
 *   - STEELED RESOLVE: a drain of 5 at five points, and 5 less 3 at six, which is the "2 Fervour per second" its
 *     sentence states; its damage reduction, 3 a point, is the control.
 *   - RAZOR MIND: 2 Fervour a point per critical strike; from 5 points another 2 a point above 75% Fervour, and none
 *     at 4.
 */
bool FCataclysmWarDataRowsRidersTest::RunTest(const FString&)
{
	using namespace CataclysmWarDataRowsTest;
	const UDataTable* Nodes = nullptr;
	const UDataTable* Effects = nullptr;
	if (!LoadTables(*this, Nodes, Effects))
	{
		return false;
	}
	if (UCataclysmPassiveTree::EffectsFor(Effects, FName(TEXT("Berserker_twoh_007"))).Num() == 0)
	{
		AddError(TEXT("Death Blow has no row in DT_PassiveEffects. If it has one in game/Data/PassiveEffects.csv, the "
					  "asset is older than the CSV: run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const TArray<FCataclysmStatModifier> DeathBlow =
		OnStat(GrantedBy(Nodes, Effects, TEXT("Berserker_twoh_007"), 2), TEXT("crit_chance"), ECataclysmStatBucket::Flat);
	if (TestEqual(TEXT("Death Blow: one crit chance row"), DeathBlow.Num(), 1))
	{
		TestEqual(TEXT("5 a point, 10 at 2 points"), DeathBlow[0].Value, 10.0f, 0.001f);
		TestTrue(TEXT("against an enemy below"), DeathBlow[0].Condition == ECataclysmStatCondition::TargetHealthBelowPercent);
		TestEqual(TEXT("25% health"), DeathBlow[0].ConditionValue, 25.0f, 0.001f);
		TestTrue(TEXT("and, second, two-handed"),
				 DeathBlow[0].Condition2 == ECataclysmStatCondition::WieldingTwoHandedWeapon);
	}

	const TArray<FCataclysmStatModifier> HairTrigger =
		OnStat(GrantedBy(Nodes, Effects, TEXT("Berserker_dw_014"), 3), TEXT("crit_chance"), ECataclysmStatBucket::Flat);
	if (TestEqual(TEXT("Hair-Trigger: two crit chance rows"), HairTrigger.Num(), 2))
	{
		int32 WithASecond = 0;
		for (const FCataclysmStatModifier& Each : HairTrigger)
		{
			TestEqual(TEXT("each 2 a point, 6 at 3 points"), Each.Value, 6.0f, 0.001f);
			TestTrue(TEXT("each in Frenzy"), Each.Condition == ECataclysmStatCondition::InFrenzy);
			if (Each.Condition2 == ECataclysmStatCondition::ClassResourceAbovePercent)
			{
				++WithASecond;
				TestEqual(TEXT("the second above 75% Fervour"), Each.ConditionValue2, 75.0f, 0.001f);
			}
		}
		TestEqual(TEXT("exactly one of them carries the second condition"), WithASecond, 1);
	}

	for (const int32 Points : {1, 10})
	{
		const TMap<FName, TArray<FCataclysmStatModifier>> Grip = GrantedBy(Nodes, Effects, TEXT("Berserker_twoh_011"), Points);
		TestEqual(*FString::Printf(TEXT("Titan's Grip at %d points drains 4, once"), Points),
				  Summed(OnStat(Grip, TEXT("fervour_drain_per_second"), ECataclysmStatBucket::Flat)), 4.0f, 0.001f);
		TestEqual(*FString::Printf(TEXT("and its melee damage is 10 a point (the control): %d"), Points * 10),
				  Summed(OnStat(Grip, TEXT("attack_damage"), ECataclysmStatBucket::Increased)),
				  10.0f * static_cast<float>(Points), 0.001f);
	}

	const TCHAR* SteeledResolve = TEXT("Bulwark_basic_trunk_009");
	const TMap<FName, TArray<FCataclysmStatModifier>> AtFive = GrantedBy(Nodes, Effects, SteeledResolve, 5);
	const TMap<FName, TArray<FCataclysmStatModifier>> AtSix = GrantedBy(Nodes, Effects, SteeledResolve, 6);
	TestEqual(TEXT("Steeled Resolve at 5 points drains 5"),
			  Summed(OnStat(AtFive, TEXT("fervour_drain_per_second"), ECataclysmStatBucket::Flat)), 5.0f, 0.001f);
	TestEqual(TEXT("and at 6 points 2, its sentence's figure"),
			  Summed(OnStat(AtSix, TEXT("fervour_drain_per_second"), ECataclysmStatBucket::Flat)), 2.0f, 0.001f);
	TestEqual(TEXT("its damage reduction is 3 a point (the control)"),
			  Summed(OnStat(AtSix, TEXT("damage_reduction"), ECataclysmStatBucket::Flat)), 18.0f, 0.001f);

	TestEqual(TEXT("Razor Mind at 4 points: 8 Fervour per critical strike, none of the doubling"),
			  Summed(OnStat(GrantedBy(Nodes, Effects, TEXT("Berserker_crit_001"), 4), TEXT("fervour_per_critical_strike"),
							ECataclysmStatBucket::Flat)), 8.0f, 0.001f);
	TestEqual(TEXT("at 5 points: 10, and another 10 above 75% Fervour"),
			  Summed(OnStat(GrantedBy(Nodes, Effects, TEXT("Berserker_crit_001"), 5), TEXT("fervour_per_critical_strike"),
							ECataclysmStatBucket::Flat)), 20.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// The Fervour spends, 2026-11-25
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarDataRowsSpendsTest,
	"Cataclysm.WarDataRows.TheSpendsCarryTheMeleeRangeAndChangeAtTheirPoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Fervour spends' rows (`tools/reapply_war_fervour_spends_workbook.py`):
 *
 *   - WARMONGER AND CUT AND RUN count enemies within melee range, 4 metres by the coordinating session's ruling, carried
 *     into the modifier; Warmonger's drain is 3 at one point and at six, its damage 8 a point (THE CONTROL).
 *   - IRON WILL's cost is 20 less 2 a point: 18 at one point, 4 at eight.
 *   - CLEAVE's arc is 180 and 10 a point: 190 at one, 240 at six.
 *   - BLOOD TITHE's loss is 5 for each 100 below six points and 1 for each 150 from six.
 */
bool FCataclysmWarDataRowsSpendsTest::RunTest(const FString&)
{
	using namespace CataclysmWarDataRowsTest;
	const UDataTable* Nodes = nullptr;
	const UDataTable* Effects = nullptr;
	if (!LoadTables(*this, Nodes, Effects))
	{
		return false;
	}
	if (UCataclysmPassiveTree::EffectsFor(Effects, FName(TEXT("Berserker_twoh_016"))).Num() == 0)
	{
		AddError(TEXT("Warmonger has no row in DT_PassiveEffects. If it has one in game/Data/PassiveEffects.csv, the asset "
					  "is older than the CSV: run  python tools/run_editor_python.py tools/generate_datatable_assets.py"));
		return false;
	}

	for (const int32 Points : {1, 6})
	{
		const TMap<FName, TArray<FCataclysmStatModifier>> Granted =
			GrantedBy(Nodes, Effects, TEXT("Berserker_twoh_016"), Points);
		const TArray<FCataclysmStatModifier> Drain = OnStat(Granted, TEXT("fervour_drain_per_second"),
															ECataclysmStatBucket::Flat);
		if (TestEqual(*FString::Printf(TEXT("Warmonger at %d points: one drain row"), Points), Drain.Num(), 1))
		{
			TestEqual(TEXT("3 for each enemy, once"), Drain[0].Value, 3.0f, 0.001f);
			TestTrue(TEXT("for each enemy in reach"), Drain[0].Scale == ECataclysmStatScale::PerEnemyInReach);
			TestEqual(TEXT("within melee range, 4 metres"), Drain[0].ReachMetres, 4.0f, 0.001f);
		}
		TestEqual(*FString::Printf(TEXT("and its damage is 8 a point (the control): %d"), Points * 8),
				  Summed(OnStat(Granted, TEXT("attack_damage"), ECataclysmStatBucket::Increased)),
				  8.0f * static_cast<float>(Points), 0.001f);
	}

	const TArray<FCataclysmStatModifier> CutAndRun = OnStat(
		GrantedBy(Nodes, Effects, TEXT("Berserker_capstone_50"), 1, 2), TEXT("movement_speed"), ECataclysmStatBucket::Increased);
	if (TestEqual(TEXT("Cut and Run: one movement speed row"), CutAndRun.Num(), 1))
	{
		TestEqual(TEXT("5% for each enemy"), CutAndRun[0].Value, 5.0f, 0.001f);
		TestEqual(TEXT("within 4 metres"), CutAndRun[0].ReachMetres, 4.0f, 0.001f);
		TestTrue(TEXT("in Frenzy"), CutAndRun[0].Condition == ECataclysmStatCondition::InFrenzy);
	}

	const auto FlatSum = [&](const TCHAR* Node, int32 Points, const TCHAR* Stat)
	{
		return Summed(OnStat(GrantedBy(Nodes, Effects, Node, Points), Stat, ECataclysmStatBucket::Flat));
	};
	TestEqual(TEXT("Iron Will at 1 point costs 18"),
			  FlatSum(TEXT("Bulwark_basic_ic_resolve_001"), 1, TEXT("fervour_cost_per_negated_block")), 18.0f, 0.001f);
	TestEqual(TEXT("and at 8 points 4"),
			  FlatSum(TEXT("Bulwark_basic_ic_resolve_001"), 8, TEXT("fervour_cost_per_negated_block")), 4.0f, 0.001f);
	TestEqual(TEXT("Cleave's arc at 1 point is 190 degrees"),
			  FlatSum(TEXT("Berserker_twoh_004"), 1, TEXT("strike_arc_at_least_degrees")), 190.0f, 0.001f);
	TestEqual(TEXT("and at 6 points 240"),
			  FlatSum(TEXT("Berserker_twoh_004"), 6, TEXT("strike_arc_at_least_degrees")), 240.0f, 0.001f);
	TestEqual(TEXT("Blood Tithe at 5 points loses 5"),
			  FlatSum(TEXT("Bulwark_basic_jg_resolve_001"), 5, TEXT("fervour_lost_to_regeneration")), 5.0f, 0.001f);
	TestEqual(TEXT("for each 100 regenerated"),
			  FlatSum(TEXT("Bulwark_basic_jg_resolve_001"), 5, TEXT("regeneration_per_fervour_loss")), 100.0f, 0.001f);
	TestEqual(TEXT("and at 6 points 1"),
			  FlatSum(TEXT("Bulwark_basic_jg_resolve_001"), 6, TEXT("fervour_lost_to_regeneration")), 1.0f, 0.001f);
	TestEqual(TEXT("for each 150"),
			  FlatSum(TEXT("Bulwark_basic_jg_resolve_001"), 6, TEXT("regeneration_per_fervour_loss")), 150.0f, 0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
