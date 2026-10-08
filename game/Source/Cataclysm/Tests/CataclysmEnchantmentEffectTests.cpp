// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
// For hearing a death and for which side a creature is on, in the one test that
// wears an authored row on a real player character and kills with it.
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "GameplayTagsManager.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "HAL/IConsoleManager.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayEffect.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmRetaliation.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "AbilitySystem/CataclysmContagion.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmRisenImps.h"
#include "AbilitySystem/CataclysmSharedBuffs.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Interface/CataclysmCharacterSheetLayout.h"
#include "Interface/CataclysmSkillBar.h"
#include "Interface/CataclysmItemTooltip.h"
#include "Items/CataclysmDropRoll.h"
#include "Save/CataclysmSaveStorage.h"
#include "GameplayTagContainer.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for what a worn enchantment does to a character. Issue #45.
 *
 * WHAT WAS WRONG. An item recorded which enchantments it rolled and its hover
 * text stated them, and not one of the 575 changed anything: the only readers of
 * `FCataclysmItem::Enchantments` were the hover text, the drop roll and the
 * tests. Every test in this file fails on that code.
 *
 * WHAT AN ENCHANTMENT GRANTS IS IN `game/Data/EnchantmentEffects.csv`, written
 * in the Enchantment Effects sheet of the design workbook. These tests read the
 * real file. The thresholds a set's rows turn on at are tested with tables made
 * up for the purpose, in CataclysmEnchantmentSetTests.cpp.
 *
 * ONE MODIFIER CANNOT SHOW WHICH BUCKET IT IS IN, because `(base) x 0.8` and
 * `(base) x (1 - 0.2)` are the same number. The test on a worn item therefore
 * wears an increase first, which is the only arrangement in which the "more"
 * bucket and the "increased" bucket give different answers.
 */
namespace CataclysmEnchantmentEffectTest
{
	using FModifiers = UCataclysmItemModifiers;
	using FTotals = TMap<FName, TArray<FCataclysmStatModifier>>;

	/** Real row names. `LoadAll` looks each one up and fails loudly if it left. */
	const TCHAR* ShieldBenefit = TEXT("Positive_Double_your_energy_shield");
	const TCHAR* LowHealthBenefit =
		TEXT("Positive_Your_retaliation_damage_is_tripled_while_below_3");
	const TCHAR* HealthDrawback = TEXT("Negative_You_have_20_less_hp");
	//
	// NOT "Ultimate has 1-3 additional charges" ANY MORE, which this was until
	// issue #1833's skill charges gave that benefit a row.
	const TCHAR* BenefitWithNoEffect =
		TEXT("Positive_Your_ultimate_ability_is_converted_into_a_placea");
	const TCHAR* DrawbackWithNoEffect = TEXT("Negative_Can_t_use_a_basic_attack");

	/**
	 * The benefit that gives a charge skill a knockdown. Its row states one to
	 * two seconds and scopes itself with `RequiredTags=Keyword.Charge`.
	 */
	const TCHAR* ChargeKnockdownBenefit =
		TEXT("Positive_Charge_skills_knock_down_enemies_they_hit_for_1");
	const TCHAR* SetMarker =
		TEXT("Positive_Archon_s_Aegis_2_Piece_Bonus_Your_block_chanc");

	/**
	 * The three benefits whose effect is a stat change inside a window an event
	 * opens. Issue #1826. Each names a different event, and the third is worth
	 * two rows because "increased damage" is authored as attack and spell.
	 */
	const TCHAR* ChargeWindowBenefit =
		TEXT("Positive_After_using_a_charge_skill_gain_20_40_increase");
	const TCHAR* BasicAttackWindowBenefit =
		TEXT("Positive_Gain_5_10_attack_speed_on_basic_attack_for_4_s");
	const TCHAR* BlockWindowBenefit =
		TEXT("Positive_Blocking_an_attack_grants_10_20_increased_dama");

	const TCHAR* SetDrawback = TEXT("Negative_Your_movement_speed_is_reduced_by_10");

	/**
	 * The drawback that locks a slot. Issue #41, and the lock's first source.
	 *
	 * "You cannot use movement abilities while stationary for more than 2
	 * seconds", which the Enchantment Effects sheet writes as the `skill_locked`
	 * stat, flat, 1, required tag `Slot.Movement`, condition
	 * `stationary_for_seconds` with 2.
	 *
	 * NOT THE ENCHANTMENT THE ISSUE NAMED. `Negative_Your_own_ultimate_ability_is_
	 * disabled` is the Null Emperor set's drawback, and a set is written whole or
	 * not at all, so writing it would demand that set's first bonus -- a chance to
	 * silence an ENEMY, which is a mechanism this game has not got.
	 */
	const TCHAR* SlotLockDrawback =
		TEXT("Negative_You_cannot_use_movement_abilities_while_stationa");

	/**
	 * The second drawback that locks a slot, and the first row in the game to use
	 * `health_at_or_above`. Issue #1754.
	 *
	 * "Your ultimate ability cannot be used unless you are below 50% HP", which the
	 * Enchantment Effects sheet writes as the `skill_locked` stat, flat, 1, required
	 * tag `Slot.Ultimate`, condition `health_at_or_above` with 50.
	 *
	 * THE PREDICATE WAS BUILT FOR THIS ROW AND THEN NOTHING USED IT. Issue #1653
	 * added `health_at_or_above` and closed; the row it was named for stayed
	 * unwritten, and `docs/DECISIONS.md` went on saying the predicate did not exist.
	 * So this test is the first thing anywhere to drive that condition through a
	 * real data row rather than through a state built in code.
	 */
	const TCHAR* UltimateLockDrawback =
		TEXT("Negative_Your_ultimate_ability_cannot_be_used_unless_you");

	/**
	 * The two rows that carry a tag scope AND a condition. Issue #1686, the two
	 * rows corrected out of its group C.
	 *
	 * THE FIRST ROWS ON A DAMAGE STAT TO NEED BOTH HALVES AT ONCE. Measured
	 * across `EnchantmentEffects.csv` and `PassiveEffects.csv` together, exactly
	 * one row carried a `RequiredTags` and a `Condition` before these -- the slot
	 * lock above -- and that one is a flag stat rather than a damage stat. So
	 * these are the first data anywhere to ask the pipeline for the AND on a
	 * number, and the two tests at the end of this file assert it rather than
	 * reading it off `FCataclysmStatModifier`'s comment.
	 */
	const TCHAR* SpellsMovingDrawback =
		TEXT("Negative_Spells_deal_20_35_less_damage_while_you_are_mo");
	const TCHAR* RangedCloseDrawback =
		TEXT("Negative_Ranged_skills_deal_15_30_less_damage_at_close");

	/**
	 * Four of the rows written from the survey on issue #1642, each the first
	 * of its kind in this file.
	 *
	 * THE SPECIAL ABILITY ROW IS THE FIRST TAG-AND-CONDITION ROW IN THE
	 * `increased` BUCKET. The two rows above carry a tag and a condition
	 * together and both are `more`. A `more` modifier that does not apply
	 * leaves a multiplier of exactly 1, which is also what no modifier at all
	 * leaves; an `increased` one that does not apply leaves a sum of zero. The
	 * two buckets fail differently, so the AND is worth asserting in both.
	 *
	 * THE HEALING ROW IS THE FIRST DATA ROW ANYWHERE ON ITS STAT.
	 * `healing_received_reduction` had no row in `EnchantmentEffects.csv` or in
	 * `PassiveEffects.csv`. Its only source was C++, the Death's Embrace dungeon
	 * rule at `CataclysmDungeonModifierEffects.cpp:457`.
	 *
	 * THE REFLECT ROW IS THE FIRST `flat` RETALIATION ROW FROM AN ENCHANTMENT.
	 * The stat is already a share of the blow taken, so a flat 20 reflects 20
	 * per cent, and its base is zero for every class but the Masochist -- which
	 * is why the row must be `flat` and could not be `increased`.
	 *
	 * THE BELOW-HALF ROW IS THE FIRST ENCHANTMENT ROW TO USE
	 * `target_health_below`. Two passive nodes use it; no enchantment did, so
	 * nothing proved the condition survives the route an enchantment takes.
	 */
	const TCHAR* SpecialMovingBenefit =
		TEXT("Positive_Your_special_ability_deals_20_40_increased_dam");
	const TCHAR* HealingReducedDrawback =
		TEXT("Negative_Healing_effects_on_you_are_reduced_by_30_50");
	const TCHAR* ReflectBenefit =
		TEXT("Positive_Reflect_20_50_of_damage_taken_back_to_attacker");
	const TCHAR* BelowHalfDrawback =
		TEXT("Negative_You_deal_25_40_less_damage_to_enemies_below_50");

	/** A real affix granting increased maximum health, top value 12. */
	const TCHAR* IncreasedHealthAffix = TEXT("Stat_Increased_maximum_health");

	/**
	 * The nine rows that say you take damage from a named source. Issue #666.
	 *
	 * EACH EXPECTED VALUE IS THE FAR END OF THE ROW'S RANGE, because a rolled
	 * enchantment built in code carries a roll of 1 and
	 * `UCataclysmItemValues::EnchantmentValue` puts a roll of 1 on the second
	 * number. `Cataclysm.Enchantments.ARangeRollsAtThePrecisionItIsWrittenIn`
	 * holds that default.
	 *
	 * THE BUCKET FOLLOWS THE WORDS. "Less" and "more" are multipliers of their
	 * own; "increased" joins the one additive sum.
	 */
	struct FSourceRow
	{
		const TCHAR* Name;
		bool bBenefit;
		float Value;
		ECataclysmStatBucket Bucket;
		ECataclysmStatCondition Condition;
	};

	const FSourceRow SourceRows[] = {
		{TEXT("Positive_You_take_20_40_less_damage_from_Boss_enemies"), true,
		 -40.0f, ECataclysmStatBucket::More,
		 ECataclysmStatCondition::OpponentIsBoss},
		{TEXT("Positive_You_take_20_40_less_damage_from_spells"), true,
		 -40.0f, ECataclysmStatBucket::More, ECataclysmStatCondition::HitIsSpell},
		{TEXT("Positive_You_take_15_30_less_damage_from_melee_attacks"), true,
		 -30.0f, ECataclysmStatBucket::More,
		 ECataclysmStatCondition::HitIsMeleeAttack},
		{TEXT("Positive_You_take_5_20_less_damage_from_melee_attacks"), true,
		 -20.0f, ECataclysmStatBucket::More,
		 ECataclysmStatCondition::HitIsMeleeAttack},
		{TEXT("Negative_You_take_20_35_more_damage_from_melee_attacks"), false,
		 35.0f, ECataclysmStatBucket::More,
		 ECataclysmStatCondition::HitIsMeleeAttack},
		{TEXT("Negative_You_take_20_35_increased_damage_from_spells"), false,
		 35.0f, ECataclysmStatBucket::Increased,
		 ECataclysmStatCondition::HitIsSpell},
		{TEXT("Negative_You_take_15_25_increased_damage_from_Boss_enem"), false,
		 25.0f, ECataclysmStatBucket::Increased,
		 ECataclysmStatCondition::OpponentIsBoss},
		{TEXT("Negative_Take_10_20_more_damage_from_Boss_enemies"), false,
		 20.0f, ECataclysmStatBucket::More,
		 ECataclysmStatCondition::OpponentIsBoss},
		{TEXT("Negative_You_take_10_30_more_damage_from_ranged_attacks"), false,
		 30.0f, ECataclysmStatBucket::More,
		 ECataclysmStatCondition::HitIsRangedAttack},
	};

	/** Loads a generated table so tests read the real data, not a fixture. */
	template <typename RowType>
	UDataTable* LoadCsv(const TCHAR* FileName)
	{
		FString Contents;
		const FString Path = FPaths::ProjectDir() / TEXT("Data") / FileName;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			return nullptr;
		}

		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = RowType::StaticStruct();
		if (Table->CreateTableFromCSVString(Contents).Num() > 0)
		{
			return nullptr;
		}
		return Table;
	}

	struct FTables
	{
		UDataTable* Effects = nullptr;
		UDataTable* Positive = nullptr;
		UDataTable* Negative = nullptr;
	};

	/**
	 * The three real tables, or false with an error saying what is missing.
	 *
	 * EVERY ROW THIS FILE NAMES IS LOOKED UP HERE, so a row that is renamed in
	 * the workbook fails with its name rather than making a test below pass
	 * having looked at nothing.
	 */
	bool LoadAll(FAutomationTestBase& Test, FTables& Out)
	{
		Out.Effects = LoadCsv<FCataclysmEnchantmentEffectRow>(
			TEXT("EnchantmentEffects.csv"));
		Out.Positive =
			LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
		Out.Negative =
			LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
		if (!Out.Effects || !Out.Positive || !Out.Negative)
		{
			Test.AddError(TEXT("An enchantment table could not be read from "
							   "game/Data. Run python tools/generate_datatables.py."));
			return false;
		}

		bool bAllReal = true;
		for (const TCHAR* Name :
			 {ShieldBenefit, LowHealthBenefit, BenefitWithNoEffect, SetMarker,
			  SpecialMovingBenefit, ReflectBenefit})
		{
			if (!Out.Positive->FindRow<FCataclysmEnchantmentRow>(
					FName(Name), TEXT("LoadAll"), /*bWarnIfMissing=*/false))
			{
				Test.AddError(FString::Printf(
					TEXT("%s is not a row of EnchantmentsPositive.csv."), Name));
				bAllReal = false;
			}
		}
		for (const TCHAR* Name :
			 {HealthDrawback, DrawbackWithNoEffect, SetDrawback, SlotLockDrawback,
			  SpellsMovingDrawback, RangedCloseDrawback,
			  HealingReducedDrawback, BelowHalfDrawback})
		{
			if (!Out.Negative->FindRow<FCataclysmEnchantmentRow>(
					FName(Name), TEXT("LoadAll"), /*bWarnIfMissing=*/false))
			{
				Test.AddError(FString::Printf(
					TEXT("%s is not a row of EnchantmentsNegative.csv."), Name));
				bAllReal = false;
			}
		}
		// AND THE NINE ROWS THAT NAME A SOURCE, each looked up in the table its
		// own half belongs to. Issue #666.
		for (const FSourceRow& Row : SourceRows)
		{
			const UDataTable* Table = Row.bBenefit ? Out.Positive : Out.Negative;
			if (!Table->FindRow<FCataclysmEnchantmentRow>(
					FName(Row.Name), TEXT("LoadAll"), /*bWarnIfMissing=*/false))
			{
				Test.AddError(FString::Printf(
					TEXT("%s is not a row of Enchantments%s.csv."), Row.Name,
					Row.bBenefit ? TEXT("Positive") : TEXT("Negative")));
				bAllReal = false;
			}
		}

		return bAllReal;
	}

	/** An item of a real base carrying one enchantment pair. */
	FCataclysmItem Carrying(const TCHAR* Base, const TCHAR* Positive,
							const TCHAR* Negative)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);

		FCataclysmRolledEnchantment Rolled;
		Rolled.Positive = FName(Positive);
		Rolled.Negative = FName(Negative);
		Item.Enchantments.Add(Rolled);
		Item.EnchantmentCount = 1;
		return Item;
	}

	/** What the enchantments on these items grant, and how many modifiers. */
	FTotals Gather(const FTables& Tables, const TArray<FCataclysmItem>& Worn,
				   int32& OutAdded)
	{
		FTotals Totals;
		OutAdded = FModifiers::AccumulateEnchantmentsInto(
			Totals, Worn, Tables.Effects, Tables.Positive, Tables.Negative);
		return Totals;
	}

	/**
	 * An actor carrying all five attribute sets and an equipment component.
	 *
	 * THE SAME SHAPE AS THE EQUIPMENT TESTS' CHARACTER, and for the reasons that
	 * file gives: `InitAbilityActorInfo` needs a real actor, and
	 * `UCataclysmPlayerClassStats::ApplyTo` skips any stat whose set is missing,
	 * so a character holding one set would take a path no real character takes.
	 */
	struct FWearer
	{
		explicit FWearer(UWorld* InWorld)
		{
			Actor = InWorld->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmPrimaryAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmClassResourceAttributeSet>(Actor));

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Equipment = NewObject<UCataclysmEquipmentComponent>(Actor);
			Equipment->RegisterComponent();
		}

		~FWearer()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentEffectBenefitTest,
	"Cataclysm.Enchantments.AWornBenefitBecomesAModifierOfTheStatItNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentEffectBenefitTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentEffectTest;

	FTables Tables;
	if (!LoadAll(*this, Tables))
	{
		return false;
	}

	// "Double your energy shield" is +100% more maximum energy shield. Its pair
	// is a drawback with no effect row, so the shield is all this item grants.
	int32 Added = 0;
	const FTotals Totals = Gather(
		Tables, {Carrying(TEXT("Head_Helm"), ShieldBenefit, DrawbackWithNoEffect)},
		Added);

	TestEqual(TEXT("one modifier"), Added, 1);
	TestEqual(TEXT("on one stat"), Totals.Num(), 1);

	const TArray<FCataclysmStatModifier>* Shield =
		Totals.Find(FName(TEXT("max_energy_shield")));
	if (!TestNotNull(TEXT("maximum energy shield got it"), Shield)
		|| !TestEqual(TEXT("exactly once"), Shield->Num(), 1))
	{
		return false;
	}

	const FCataclysmStatModifier& Modifier = (*Shield)[0];
	TestEqual(TEXT("in the more bucket"), static_cast<int32>(Modifier.Bucket),
			  static_cast<int32>(ECataclysmStatBucket::More));
	TestEqual(TEXT("worth +100%"), Modifier.Value, 100.0f);
	TestEqual(TEXT("from the enchantment source, which may grant more"),
			  static_cast<int32>(Modifier.Source),
			  static_cast<int32>(ECataclysmModifierSource::Enchantment));
	TestTrue(TEXT("applying to every skill"), Modifier.RequiredTags.IsEmpty());
	TestEqual(TEXT("with no condition"), static_cast<int32>(Modifier.Condition),
			  static_cast<int32>(ECataclysmStatCondition::Always));

	// AN ITEM WHOSE TWO ROWS HAVE NO EFFECT WRITTEN GRANTS NOTHING. That is most
	// enchantments today, and it must not read as a fault.
	int32 NoneAdded = 0;
	const FTotals Nothing = Gather(
		Tables,
		{Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, DrawbackWithNoEffect)},
		NoneAdded);
	TestEqual(TEXT("rows with no effect written grant nothing"), NoneAdded, 0);
	TestEqual(TEXT("and touch no stat"), Nothing.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentEffectDrawbackTest,
	"Cataclysm.Enchantments.AWornDrawbackBecomesAModifierOfTheStatItNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentEffectDrawbackTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentEffectTest;

	FTables Tables;
	if (!LoadAll(*this, Tables))
	{
		return false;
	}

	// "You have 20% less hp" is -20% more maximum health: a "less" is a negative
	// value in the more bucket, so it multiplies rather than joining the sum.
	int32 Added = 0;
	const FTotals Totals = Gather(
		Tables, {Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, HealthDrawback)},
		Added);

	TestEqual(TEXT("one modifier"), Added, 1);

	const TArray<FCataclysmStatModifier>* Health =
		Totals.Find(FName(TEXT("max_health")));
	if (!TestNotNull(TEXT("maximum health got it"), Health)
		|| !TestEqual(TEXT("exactly once"), Health->Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("in the more bucket"),
			  static_cast<int32>((*Health)[0].Bucket),
			  static_cast<int32>(ECataclysmStatBucket::More));
	TestEqual(TEXT("worth -20%"), (*Health)[0].Value, -20.0f);
	TestEqual(TEXT("from the enchantment source"),
			  static_cast<int32>((*Health)[0].Source),
			  static_cast<int32>(ECataclysmModifierSource::Enchantment));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentEffectOncePerCharacterTest,
	"Cataclysm.Enchantments.ABenefitOnTwoPiecesAppliesOnceAndADrawbackOnTwoAppliesTwice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentEffectOncePerCharacterTest::RunTest(
	const FString& Parameters)
{
	using namespace CataclysmEnchantmentEffectTest;

	FTables Tables;
	if (!LoadAll(*this, Tables))
	{
		return false;
	}

	// TWO PIECES, BOTH CARRYING THE SAME PAIR. The design says an enchantment
	// "can only appear once across all of a player's equipped gear". Nothing
	// refuses the second piece at equip time yet, so the benefit must still be
	// counted once; and nothing may make a cost smaller than the items say, so
	// the drawback counts for both pieces.
	int32 Added = 0;
	const FTotals Totals = Gather(
		Tables,
		{Carrying(TEXT("Head_Helm"), ShieldBenefit, HealthDrawback),
		 Carrying(TEXT("Boots_Sabatons"), ShieldBenefit, HealthDrawback)},
		Added);

	const TArray<FCataclysmStatModifier>* Shield =
		Totals.Find(FName(TEXT("max_energy_shield")));
	const TArray<FCataclysmStatModifier>* Health =
		Totals.Find(FName(TEXT("max_health")));
	if (!TestNotNull(TEXT("the benefit applied"), Shield)
		|| !TestNotNull(TEXT("the drawback applied"), Health))
	{
		return false;
	}

	TestEqual(TEXT("the benefit once, though two pieces carry it"),
			  Shield->Num(), 1);
	TestEqual(TEXT("the drawback twice, once for each piece"), Health->Num(), 2);
	TestEqual(TEXT("three modifiers in all"), Added, 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentEffectSetPieceTest,
	"Cataclysm.Enchantments.OnePieceOfASetGrantsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentEffectSetPieceTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentEffectTest;

	FTables Tables;
	if (!LoadAll(*this, Tables))
	{
		return false;
	}

	// ONE PIECE IS BELOW EVERY THRESHOLD OF ITS SET, so it grants neither the
	// set's two-piece bonus nor the set's drawback. The row an item records for
	// a set is that set's lowest threshold row, so granting it per piece would
	// hand a single piece the two-piece bonus, and the drawback -- which the
	// owner ruled applies once for the whole set -- would apply per piece.
	//
	// THE REAL ROWS, both sides. Archon's Aegis is one of the four sets whose
	// rows are written, so this reads what the game reads. What two pieces and
	// more grant is in CataclysmEnchantmentSetTests.cpp.
	int32 Added = 0;
	const FTotals FromSet = Gather(
		Tables, {Carrying(TEXT("Head_Helm"), SetMarker, SetDrawback)}, Added);
	TestEqual(TEXT("a set piece grants nothing on its own"), Added, 0);
	TestEqual(TEXT("and touches no stat"), FromSet.Num(), 0);

	// THE CONTROL: an ordinary benefit from the same table does grant, so the
	// zero above is the set rule rather than a table nothing was read from.
	int32 ControlAdded = 0;
	Gather(Tables,
		   {Carrying(TEXT("Head_Helm"), ShieldBenefit, DrawbackWithNoEffect)},
		   ControlAdded);
	TestEqual(TEXT("while an ordinary row in the same table does grant"),
			  ControlAdded, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentEffectConditionTest,
	"Cataclysm.Enchantments.AConditionWrittenOnARowReachesItsModifier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentEffectConditionTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentEffectTest;

	FTables Tables;
	if (!LoadAll(*this, Tables))
	{
		return false;
	}

	// "Your retaliation damage is tripled while below 30% HP" is +200% more
	// retaliation, held while health is strictly below 30%. Without the
	// condition it would triple retaliation at full health, which is the failure
	// that matters: silent, and in the player's favour.
	int32 Added = 0;
	const FTotals Totals = Gather(
		Tables,
		{Carrying(TEXT("Head_Helm"), LowHealthBenefit, DrawbackWithNoEffect)},
		Added);

	const TArray<FCataclysmStatModifier>* Retaliation =
		Totals.Find(FName(TEXT("retaliation")));
	if (!TestNotNull(TEXT("retaliation got a modifier"), Retaliation)
		|| !TestEqual(TEXT("exactly one"), Retaliation->Num(), 1))
	{
		return false;
	}

	const FCataclysmStatModifier& Modifier = (*Retaliation)[0];
	TestEqual(TEXT("tripled is +200% more"), Modifier.Value, 200.0f);
	TestEqual(TEXT("in the more bucket"), static_cast<int32>(Modifier.Bucket),
			  static_cast<int32>(ECataclysmStatBucket::More));
	TestEqual(TEXT("held only strictly below a health share"),
			  static_cast<int32>(Modifier.Condition),
			  static_cast<int32>(ECataclysmStatCondition::HealthBelowPercent));
	TestEqual(TEXT("of 30%"), Modifier.ConditionValue, 30.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentEffectWornTest,
	"Cataclysm.Enchantments.AWornDrawbackLowersTheCharacterAsAMultiplier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentEffectWornTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentEffectTest;

	// THE WHOLE ROUTE: an item put on, the character's stats refreshed, and the
	// attribute read. The tests above check the modifiers; this checks that the
	// function which turns worn gear into stats asks for them at all, which is
	// the step that was missing.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn a character in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWearer Wearer(World);
	UAbilitySystemComponent* ASC = Wearer.AbilitySystem;
	UCataclysmEquipmentComponent* Equipment = Wearer.Equipment;

	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	if (!ASC->HasAttributeSetForAttribute(MaxHealth))
	{
		AddError(TEXT("The test character holds no vital attribute set."));
		return false;
	}

	Equipment->RefreshAttributes(ASC);
	const float Bare = ASC->GetNumericAttribute(MaxHealth);
	if (!TestTrue(TEXT("a character with no gear has some health"), Bare > 0.0f))
	{
		return false;
	}

	// AN INCREASE FIRST, SO THE TWO BUCKETS GIVE DIFFERENT ANSWERS. A helm at +10
	// carrying the top roll of increased maximum health.
	FCataclysmItem Helm;
	Helm.Base = FName(TEXT("Head_Helm"));
	Helm.GearLevel = 10;
	FCataclysmRolledAffix Increase;
	Increase.Affix = FName(IncreasedHealthAffix);
	Increase.Tier = UCataclysmItemValues::MaxAffixTier;
	Increase.Roll = 1.0f;
	Helm.Affixes.Add(Increase);

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot HelmSlot = ECataclysmGearSlot::Count;
	Equipment->Equip(Helm, Removed, AlsoRemoved, HelmSlot);
	Equipment->RefreshAttributes(ASC);

	const float WithIncrease = ASC->GetNumericAttribute(MaxHealth);
	if (!TestTrue(FString::Printf(
			TEXT("the helm's increase raises maximum health: %.2f to %.2f"),
			Bare, WithIncrease), WithIncrease > Bare))
	{
		return false;
	}

	// HOW FAR APART THE TWO BUCKETS PUT THE ANSWER. A multiplier of 0.8 takes a
	// fifth of WithIncrease. The same -20 added to the increases would take a
	// fifth of the base underneath them, and that base is at most Bare. So the
	// two answers differ by at least a fifth of what the helm added, and this
	// asserts that gap is larger than the tolerance used below.
	const float Separation = 0.2f * (WithIncrease - Bare);
	TestTrue(FString::Printf(
		TEXT("the two buckets give answers at least %.2f apart, which the "
			 "tolerance of 0.5 below can tell"), Separation),
		Separation > 1.0f);

	// BOOTS CARRYING "You have 20% less hp", paired with a benefit that has no
	// effect row, so the drawback is the only change.
	ECataclysmGearSlot BootsSlot = ECataclysmGearSlot::Count;
	Equipment->Equip(
		Carrying(TEXT("Boots_Sabatons"), BenefitWithNoEffect, HealthDrawback),
		Removed, AlsoRemoved, BootsSlot);
	Equipment->RefreshAttributes(ASC);

	const float WithDrawback = ASC->GetNumericAttribute(MaxHealth);
	TestEqual(FString::Printf(
				  TEXT("\"You have 20%% less hp\" leaves 80%% of the health the "
					   "increase gave: %.2f against %.2f"),
				  WithDrawback, WithIncrease * 0.8f),
			  WithDrawback, WithIncrease * 0.8f, 0.5f);

	Equipment->Unequip(BootsSlot, Removed);
	Equipment->RefreshAttributes(ASC);
	TestEqual(TEXT("and taking the boots off gives it back exactly"),
			  ASC->GetNumericAttribute(MaxHealth), WithIncrease, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentEffectAttributesTest,
	"Cataclysm.Enchantments.EveryStatAnEnchantmentGrantsHasAnAttributeBehindIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentEffectAttributesTest::RunTest(const FString& Parameters)
{
	// A STAT WITH NO ATTRIBUTE BEHIND IT IS DROPPED IN SILENCE, by
	// `UCataclysmPlayerClassStats::ApplyTo`, which loops over its own map of
	// stats rather than over the modifiers. The generator checks that something
	// supplies each stat; this checks what that cannot, against the asset the
	// game really loads. `Cataclysm.Passives.EveryStatAPassiveNodeGrantsHasAnAttributeBehindIt`
	// is the same check for the passive trees.
	const UDataTable* EffectTable =
		UCataclysmItemModifiers::LoadEnchantmentEffectTable();
	if (!TestNotNull(TEXT("the enchantment effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const TMap<FString, FGameplayAttribute>& Attributes =
		UCataclysmPlayerClassStats::StatToAttribute();

	// AND THE STATS THAT DELIBERATELY HAVE NO ATTRIBUTE, which this test did not
	// know about until an enchantment row needed one.
	//
	// `ApplyTo` STOPPED LOOPING ONLY OVER `StatToAttribute` WHEN #1724 MERGED: a
	// third pass loops over `StatsWithNoAttribute()` and records those stats
	// without writing any attribute, so a row naming one of them is not dropped.
	// Bespoke code reads them directly: the minion stats' increases in
	// `UCataclysmCommand::AttackIntervalScaleFor`,
	// `ACataclysmMinion::AttackTarget` and `ACataclysmMinion::Spawn`, and, since
	// issue #1791, whether `mana_on_hit` is removed in
	// `UCataclysmSkillTemplate::ApplyManaOnHit`.
	//
	// `Cataclysm.Passives.EveryStatAPassiveNodeGrantsHasAnAttributeBehindIt`
	// GAINED THIS IN #1733 AND THIS TEST DID NOT, because no enchantment row
	// granted a minion stat at the time and so nothing failed. "Summoned minions
	// have 30%-50% increased HP" is the first, and it failed here while working
	// perfectly in play. The two checks are the same check on two sheets and
	// they should not disagree about which stats are exempt.
	//
	// READ FROM THE ENGINE'S OWN LIST RATHER THAN RESTATED HERE, for the reason
	// the passive one gives: a copy here could drift from the code it checks.
	//
	// AN EXEMPTION IS A PROMISE AND THIS TEST DOES NOT KEEP IT. All it does is
	// stop refusing them. That every name on that list is really read by code is
	// held by `Cataclysm.StatExemption.EveryStatWithNoAttributeIsActuallyRead`.
	const TArray<FString>& Exempt =
		UCataclysmPlayerClassStats::StatsWithNoAttribute();

	int32 Checked = 0;
	int32 Exempted = 0;
	for (const TPair<FName, uint8*>& Row : EffectTable->GetRowMap())
	{
		const auto* Effect =
			reinterpret_cast<const FCataclysmEnchantmentEffectRow*>(Row.Value);
		if (!Effect || Effect->Stat.IsEmpty())
		{
			continue;
		}

		++Checked;
		if (Exempt.Contains(Effect->Stat))
		{
			++Exempted;
			continue;
		}

		TestTrue(*FString::Printf(
					 TEXT("%s grants '%s', which has an attribute behind it"),
					 *Row.Key.ToString(), *Effect->Stat),
				 Attributes.Contains(Effect->Stat));
	}

	// AND SAY HOW MANY TOOK THE EXEMPTION, so a build where the exemption
	// swallowed everything is visible rather than silently green. A reader who
	// sees this climb without the list growing has found a misspelling that
	// happens to match an exempt name.
	AddInfo(FString::Printf(
		TEXT("%d enchantment rows checked, %d of them exempt from needing an "
			 "attribute"),
		Checked, Exempted));

	// Without this the loop above passes on an empty table, which is what a
	// stale or unbuilt asset looks like.
	TestTrue(TEXT("the effect table has its rows"), Checked >= 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentEffectSourceRowTest,
	"Cataclysm.Enchantments.ARowNamingItsSourceMovesDamageTakenUnderThatCondition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentEffectSourceRowTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentEffectTest;

	FTables Tables;
	if (!LoadAll(*this, Tables))
	{
		return false;
	}

	// EACH ROW WORN ON ITS OWN, paired with a row of the other half that has no
	// effect written. So damage taken gets exactly one modifier, and nothing
	// below can be satisfied by the other half of the pair.
	//
	// WHAT WOULD BE WRONG WITHOUT THE CONDITION: a character wearing "you take
	// 20%-40% less damage from spells" would take less damage from everything,
	// silently and in the player's favour. That is the failure these check for.
	for (const FSourceRow& Row : SourceRows)
	{
		const TCHAR* Positive = Row.bBenefit ? Row.Name : BenefitWithNoEffect;
		const TCHAR* Negative = Row.bBenefit ? DrawbackWithNoEffect : Row.Name;

		int32 Added = 0;
		const FTotals Totals = Gather(
			Tables, {Carrying(TEXT("Head_Helm"), Positive, Negative)}, Added);

		const TArray<FCataclysmStatModifier>* Taken =
			Totals.Find(FName(TEXT("damage_taken")));
		if (!TestNotNull(
				*FString::Printf(TEXT("%s moves damage taken"), Row.Name), Taken)
			|| !TestEqual(*FString::Printf(TEXT("%s exactly once"), Row.Name),
						  Taken->Num(), 1))
		{
			continue;
		}

		const FCataclysmStatModifier& Modifier = (*Taken)[0];
		TestEqual(*FString::Printf(TEXT("%s at the top of its range"), Row.Name),
				  Modifier.Value, Row.Value);
		TestEqual(
			*FString::Printf(TEXT("%s in the bucket its words state"), Row.Name),
			static_cast<int32>(Modifier.Bucket), static_cast<int32>(Row.Bucket));
		TestEqual(
			*FString::Printf(TEXT("%s only for a hit from its source"), Row.Name),
			static_cast<int32>(Modifier.Condition),
			static_cast<int32>(Row.Condition));
		TestEqual(*FString::Printf(TEXT("%s comparing no value"), Row.Name),
				  Modifier.ConditionValue, 0.0f);
		TestEqual(
			*FString::Printf(TEXT("%s from the enchantment source"), Row.Name),
			static_cast<int32>(Modifier.Source),
			static_cast<int32>(ECataclysmModifierSource::Enchantment));
	}

	// Without this the loop above passes having worn nothing at all.
	TestEqual(TEXT("nine rows that name a source are written"),
			  static_cast<int32>(UE_ARRAY_COUNT(SourceRows)), 9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentEffectLockTest,
	"Cataclysm.Enchantments.AWornLockReachesOnlyTheSlotAndTheStateItNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentEffectLockTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentEffectTest;

	// THE ROUTE GEAR REALLY TAKES, WHICH IS THE HALF THE LOCK HAD NO TEST FOR.
	// `Cataclysm.Skills.ALockedSkillIsRefusedAndAnUnlockedOneIsNot` sets the lock
	// by calling `SetStatInputs` itself. That writes the very map `StatForSkill`
	// reads, so it never runs `ApplyTo` and never consults `StatToAttribute()`,
	// and it would pass with the attribute and the name-to-attribute entry both
	// absent. It proves the refusal; this proves there is a way to cause one.
	//
	// WHY THE MAP ENTRY IS LOAD-BEARING AND NOT BOOKKEEPING. `ApplyTo` records a
	// stat's inputs inside a lambda called from two loops, both over
	// `StatToAttribute()`, and there is no pass over the modifier map. A stat
	// missing from it has its modifiers gathered and then dropped, and
	// `StatForSkill` answers its fallback for ever. The second gate is the
	// attribute's SET: the loop skips any stat whose set the component does not
	// hold, which is why the lock needed an attribute as well as a name.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn a character in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FTables Tables;
	if (!LoadAll(*this, Tables))
	{
		return false;
	}

	// THE TAGS ARE READ OFF REAL SKILL ROWS AND NOT TYPED HERE. A lock whose
	// required tag no skill carries is a lock scoped to the empty set, and it
	// would read as a lock that does not work at all. Two tests that both TYPE
	// `Slot.Movement` agree with each other rather than with the game; these
	// containers are whatever `game/Data/WeaponSkills.csv` says a movement skill
	// and a heavy skill hold, parsed the way `EnchantmentModifierFor` parses the
	// row's own required tags.
	const UDataTable* Skills =
		LoadCsv<FCataclysmWeaponSkillRow>(TEXT("WeaponSkills.csv"));
	if (!TestNotNull(TEXT("the weapon skill table reads"), Skills))
	{
		return false;
	}

	const FGameplayTag MovementTag =
		FGameplayTag::RequestGameplayTag(FName(TEXT("Slot.Movement")));
	if (!TestTrue(TEXT("Slot.Movement is a registered tag"), MovementTag.IsValid()))
	{
		return false;
	}

	FGameplayTagContainer MovementSkillTags;
	FGameplayTagContainer HeavySkillTags;
	int32 MovementRows = 0;
	for (const TPair<FName, uint8*>& Row : Skills->GetRowMap())
	{
		const auto* Skill =
			reinterpret_cast<const FCataclysmWeaponSkillRow*>(Row.Value);
		if (!Skill)
		{
			continue;
		}

		FGameplayTagContainer Held;
		TArray<FString> Names;
		Skill->Tags.ParseIntoArray(Names, TEXT(","), /*InCullEmpty=*/true);
		for (FString& Name : Names)
		{
			Name.TrimStartAndEndInline();
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
				FName(*Name), /*ErrorIfNotFound=*/false);
			if (Tag.IsValid())
			{
				Held.AddTag(Tag);
			}
		}

		if (Held.HasTag(MovementTag))
		{
			++MovementRows;
			if (MovementSkillTags.IsEmpty())
			{
				MovementSkillTags = Held;
			}
		}
		else if (HeavySkillTags.IsEmpty()
				 && Held.HasTag(FGameplayTag::RequestGameplayTag(
						FName(TEXT("Slot.Heavy")))))
		{
			HeavySkillTags = Held;
		}
	}

	// Without these the reads below ask about empty containers, and a modifier
	// that requires a tag refuses an empty container, so every figure would be
	// zero and the test would pass having measured nothing.
	if (!TestTrue(FString::Printf(
			TEXT("the data holds movement skills carrying Slot.Movement: %d"),
			MovementRows), MovementRows > 0))
	{
		return false;
	}
	if (!TestFalse(TEXT("and a heavy skill to compare against"),
				   HeavySkillTags.IsEmpty()))
	{
		return false;
	}

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;
	const FName Stat = FName(UCataclysmSkillSlots::LockedStat);

	// STANDING STILL HAS TO BE STARTED. `SecondsSinceMoved` answers -1 for a
	// character that has never moved and `ConditionHolds` refuses an unknown
	// reading, so a character that has stood perfectly still since it was spawned
	// is not stationary as far as the condition is concerned. `NoteDidNotMove` is
	// the first sample, and its own comment says that is when standing still
	// begins.
	ASC->NoteDidNotMove();
	CataclysmTestWorld::RunClock(World, 3.0f);

	Wearer.Equipment->RefreshAttributes(ASC);
	TestEqual(TEXT("a character wearing nothing has no lock on its movement skill"),
			  ASC->StatForSkill(Stat, MovementSkillTags, 0.0f), 0.0f, 0.001f);

	// THE DRAWBACK, PAIRED WITH A BENEFIT THAT HAS NO EFFECT ROW, so the lock is
	// the only thing this item changes.
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Boots_Sabatons"), BenefitWithNoEffect, SlotLockDrawback),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	TestTrue(TEXT("the worn drawback locks a movement skill"),
			 ASC->StatForSkill(Stat, MovementSkillTags, 0.0f) > 0.0f);

	// AND NOTHING ELSE. The row scopes itself to one slot, and the other five
	// slots are what say so.
	TestEqual(TEXT("and leaves a heavy skill alone"),
			  ASC->StatForSkill(Stat, HeavySkillTags, 0.0f), 0.0f, 0.001f);

	// THE CHARACTER SHEET SHOWS NO LOCK EITHER, which is the same reading with no
	// skill in hand. A scoped modifier must not apply to a bare stat, or the
	// sheet would claim a character whose every skill works is locked.
	TestEqual(TEXT("and shows nothing with no skill in hand"),
			  ASC->StatForSkill(Stat, FGameplayTagContainer(), 0.0f), 0.0f, 0.001f);

	// THE CONDITION IS JUDGED AT THE MOMENT OF THE ASK, not when the gear was put
	// on. One step taken is enough: the reading is seconds since the character
	// last moved, and it has just moved.
	ASC->NoteMovedMetres(1.0f);
	TestEqual(TEXT("a step taken unlocks the movement skill at once"),
			  ASC->StatForSkill(Stat, MovementSkillTags, 0.0f), 0.0f, 0.001f);

	// AND STANDING STILL AGAIN LOCKS IT AGAIN, WITH NO REFRESH BETWEEN. That is
	// what makes this a condition rather than a state written onto the character:
	// nothing was applied or removed between these two reads.
	CataclysmTestWorld::RunClock(World, 3.0f);
	TestTrue(TEXT("and standing still again locks it, with no refresh between"),
			 ASC->StatForSkill(Stat, MovementSkillTags, 0.0f) > 0.0f);

	// AND TAKING THE BOOTS OFF GIVES IT BACK, which says the lock came from the
	// item rather than from anything the character did.
	Wearer.Equipment->Unequip(Slot, Removed);
	Wearer.Equipment->RefreshAttributes(ASC);
	TestEqual(TEXT("and taking the boots off leaves the skill free"),
			  ASC->StatForSkill(Stat, MovementSkillTags, 0.0f), 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentUltimateLockTest,
	"Cataclysm.Enchantments.TheUltimateLockHoldsAtExactlyHalfHealthAndReleasesBelowIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentUltimateLockTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentEffectTest;

	// THE BOUNDARY IS THE WHOLE POINT OF THIS ROW. Issue #1754. "Cannot be used
	// unless you are below 50% HP" means locked AT 50 as well as above it, and
	// `health_above` is strictly above -- so a character parked on exactly half
	// health would fire an ultimate the row forbids. `health_at_or_above` exists
	// for that one case, and a test that only checked 100% and 10% would pass with
	// the wrong predicate written on the row.
	//
	// AND IT IS THE FIRST ROW ANYWHERE TO USE THAT PREDICATE. Issue #1653 built it
	// for this sentence and closed with the row unwritten, so until now nothing
	// drove it through a data row at all.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn a character in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FTables Tables;
	if (!LoadAll(*this, Tables))
	{
		return false;
	}

	// THE TAGS COME OFF A REAL SKILL ROW, for the reason the lock test above gives:
	// a lock whose required tag no skill carries is scoped to the empty set and
	// reads as a lock that does not work.
	const UDataTable* Skills =
		LoadCsv<FCataclysmWeaponSkillRow>(TEXT("WeaponSkills.csv"));
	if (!TestNotNull(TEXT("the weapon skill table reads"), Skills))
	{
		return false;
	}

	const FGameplayTag UltimateTag =
		FGameplayTag::RequestGameplayTag(FName(TEXT("Slot.Ultimate")));
	if (!TestTrue(TEXT("Slot.Ultimate is a registered tag"), UltimateTag.IsValid()))
	{
		return false;
	}

	FGameplayTagContainer UltimateSkillTags;
	FGameplayTagContainer OtherSkillTags;
	int32 UltimateRows = 0;
	for (const TPair<FName, uint8*>& Row : Skills->GetRowMap())
	{
		const auto* Skill =
			reinterpret_cast<const FCataclysmWeaponSkillRow*>(Row.Value);
		if (!Skill)
		{
			continue;
		}

		FGameplayTagContainer Held;
		TArray<FString> Names;
		Skill->Tags.ParseIntoArray(Names, TEXT(","), /*InCullEmpty=*/true);
		for (FString& Name : Names)
		{
			Name.TrimStartAndEndInline();
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
				FName(*Name), /*ErrorIfNotFound=*/false);
			if (Tag.IsValid())
			{
				Held.AddTag(Tag);
			}
		}

		if (Held.HasTag(UltimateTag))
		{
			++UltimateRows;
			if (UltimateSkillTags.IsEmpty())
			{
				UltimateSkillTags = Held;
			}
		}
		else if (OtherSkillTags.IsEmpty()
				 && Held.HasTag(FGameplayTag::RequestGameplayTag(
						FName(TEXT("Slot.Movement")))))
		{
			OtherSkillTags = Held;
		}
	}

	// Without these the reads below ask about empty containers, a modifier
	// requiring a tag refuses an empty container, and every figure would be zero
	// while the test reported a pass.
	if (!TestTrue(FString::Printf(
			TEXT("the data holds ultimate skills carrying Slot.Ultimate: %d"),
			UltimateRows), UltimateRows > 0))
	{
		return false;
	}
	if (!TestFalse(TEXT("and a skill in another slot to compare against"),
				   OtherSkillTags.IsEmpty()))
	{
		return false;
	}

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;
	const FName Stat = FName(UCataclysmSkillSlots::LockedStat);

	// A STARTING STATE OF FULL HEALTH, AND NOTHING MORE. This maximum does NOT
	// decide the boundary arithmetic below: equipping the helm runs
	// `RefreshAttributes`, which recomputes maximum health from the gear and
	// replaces this figure. The half-health reads further down take the maximum
	// as it is at that moment instead.
	constexpr float PoolMax = 1000.0f;
	ASC->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), PoolMax);
	ASC->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), PoolMax);

	Wearer.Equipment->RefreshAttributes(ASC);
	TestEqual(TEXT("a character wearing nothing has no lock on its ultimate"),
			  ASC->StatForSkill(Stat, UltimateSkillTags, 0.0f), 0.0f, 0.001f);

	// THE DRAWBACK, PAIRED WITH A BENEFIT THAT HAS NO EFFECT ROW, so the lock is
	// the only thing this item changes.
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, UltimateLockDrawback),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	TestTrue(TEXT("at full health the worn drawback locks the ultimate"),
			 ASC->StatForSkill(Stat, UltimateSkillTags, 0.0f) > 0.0f);

	// AND NOTHING ELSE. The row scopes itself to one slot.
	TestEqual(TEXT("and leaves a skill in another slot alone"),
			  ASC->StatForSkill(Stat, OtherSkillTags, 0.0f), 0.0f, 0.001f);

	// THE CHARACTER SHEET SHOWS NO LOCK, which is the same reading with no skill
	// in hand. A scoped modifier must not apply to a bare stat.
	TestEqual(TEXT("and shows nothing with no skill in hand"),
			  ASC->StatForSkill(Stat, FGameplayTagContainer(), 0.0f), 0.0f, 0.001f);

	// THE MAXIMUM IS READ HERE AND NOT ASSUMED, BECAUSE EQUIPPING CHANGES IT.
	// `RefreshAttributes` recomputes the character's attributes from the gear it
	// is wearing, so a maximum health written before the helm went on does not
	// survive putting it on. The first version of this test wrote 1000 up front
	// and then set health to 500 expecting half; the maximum was 510 by then, so
	// 500 was 98% and the lock held for a reason the test was not testing.
	const float MaxHealthNow = ASC->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	if (!TestTrue(TEXT("the character has a maximum health to take half of"),
				  MaxHealthNow > 1.0f))
	{
		return false;
	}

	// EXACTLY HALF, AND STILL LOCKED. This is the assertion the predicate exists
	// for, and the one that fails if the row is ever written with `health_above`.
	// No refresh between this read and the last: the condition is judged when the
	// question is asked, not when the gear was put on.
	//
	// THE HEALTH IS ASSERTED BEFORE THE LOCK IS, and that is what caught the fault
	// above. A write that does not land leaves the character at full health, where
	// the lock holds for the WRONG reason and the behaviour assertion reads as a
	// pass.
	ASC->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), MaxHealthNow * 0.5f);
	TestEqual(TEXT("the character really is on exactly half health"),
			  ASC->CurrentConditions().HealthPercent, 50.0f, 0.001f);
	TestTrue(TEXT("at exactly half health the ultimate is still locked"),
			 ASC->StatForSkill(Stat, UltimateSkillTags, 0.0f) > 0.0f);

	// ONE POINT BELOW, AND FREE. The row says "unless you are below 50%".
	ASC->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(),
		MaxHealthNow * 0.5f - 1.0f);
	TestTrue(TEXT("the character really is below half health"),
			 ASC->CurrentConditions().HealthPercent < 50.0f);
	TestEqual(TEXT("one point below half health releases the ultimate"),
			  ASC->StatForSkill(Stat, UltimateSkillTags, 0.0f), 0.0f, 0.001f);

	// AND HEALING BACK TO HALF LOCKS IT AGAIN, with no refresh between. That is
	// what makes this a condition rather than a state written onto the character.
	ASC->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), MaxHealthNow * 0.5f);
	TestTrue(TEXT("and healing back to half locks it again"),
			 ASC->StatForSkill(Stat, UltimateSkillTags, 0.0f) > 0.0f);

	// AND TAKING THE HELM OFF GIVES IT BACK, which says the lock came from the
	// item rather than from the character's health.
	Wearer.Equipment->Unequip(Slot, Removed);
	Wearer.Equipment->RefreshAttributes(ASC);
	TestEqual(TEXT("and taking the helm off leaves the ultimate free"),
			  ASC->StatForSkill(Stat, UltimateSkillTags, 0.0f), 0.0f, 0.001f);

	return true;
}


// --------------------------------------------------------------------------
// The two conditions for moving and standing still, which are fully wired and
// which NO DATA ROW HAS EVER USED until issue #1686's first group. These are the
// first rows to rely on them, so the behaviour is asserted rather than inferred
// from the wiring being present.
//
// EACH CONDITION NEEDS A CASE WHERE IT FIRES. A test showing only that the
// reduction is absent cannot tell "correctly scoped" from "never reached" -- the
// charge knockdown row proved that the hard way, with three such tests passing
// while the feature did nothing.
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWhileMovingReductionTest,
	"Cataclysm.Enchantments.TheWhileMovingReductionReachesAMovingCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWhileMovingReductionTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;
	const FName Stat = FName(UCataclysmDamageCalculation::DamageTakenStat);

	// STANDING STILL HAS TO BE STARTED, as the slot-lock test above records: a
	// character that has never moved reads as unknown rather than as stationary.
	ASC->NoteDidNotMove();
	CataclysmTestWorld::RunClock(World, 3.0f);

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"),
				 TEXT("Positive_While_moving_you_take_10_20_less_damage"),
				 DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	const float Normal = UCataclysmDamageCalculation::NormalDamageTaken;

	// STANDING STILL, THE ROW GRANTS NOTHING.
	TestEqual(TEXT("standing still, the wearer takes normal damage"),
			  ASC->StatForSkill(Stat, FGameplayTagContainer(), Normal),
			  Normal, 0.001f);

	// AND ONE STEP TAKEN IS ENOUGH. This is the assertion the row exists for, and
	// the first time any data row has asked this condition for anything.
	ASC->NoteMovedMetres(1.0f);
	const float Moving = ASC->StatForSkill(Stat, FGameplayTagContainer(), Normal);
	TestTrue(*FString::Printf(
				 TEXT("and moving, it takes less: %.2f against a normal %.2f"),
				 Moving, Normal),
			 Moving < Normal);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWhileStationaryReductionTest,
	"Cataclysm.Enchantments.TheWhileStationaryReductionNeedsStandingStillToHaveStarted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The mirror, and it carries the trap the other one only mentions.
 *
 * A CHARACTER THAT HAS NEVER MOVED IS NOT STATIONARY. The reading is seconds
 * since it last moved, which answers unknown for a character that has never
 * moved at all, and an unknown reading is refused. So a freshly spawned wearer
 * gets nothing from a "while stationary" row until it has moved once and stopped
 * -- which is worth pinning, because it is the opposite of what the row's English
 * suggests and a player standing still from the start would see no benefit.
 */
bool FCataclysmWhileStationaryReductionTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;
	const FName Stat = FName(UCataclysmDamageCalculation::DamageTakenStat);
	const float Normal = UCataclysmDamageCalculation::NormalDamageTaken;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"),
				 TEXT("Positive_While_stationary_you_take_15_30_less_damage"),
				 DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	// MOVING, IT GRANTS NOTHING.
	ASC->NoteMovedMetres(1.0f);
	TestEqual(TEXT("moving, the wearer takes normal damage"),
			  ASC->StatForSkill(Stat, FGameplayTagContainer(), Normal),
			  Normal, 0.001f);

	// AND STANDING STILL, ONCE STANDING STILL HAS BEGUN, IT FIRES.
	ASC->NoteDidNotMove();
	CataclysmTestWorld::RunClock(World, 3.0f);
	const float Still = ASC->StatForSkill(Stat, FGameplayTagContainer(), Normal);
	TestTrue(*FString::Printf(
				 TEXT("and standing still, it takes less: %.2f against a normal %.2f"),
				 Still, Normal),
			 Still < Normal);

	return true;
}


// --------------------------------------------------------------------------
// The two rows that need a tag scope AND a condition at once. Issue #1686, the
// two rows its group C listed as blocked and which are not.
//
// WHAT MAKES THEM WORTH A TEST RATHER THAN AN ASSUMPTION.
// `FCataclysmStatModifier` says "BOTH THIS AND `RequiredTags` MUST HOLD", and
// until these rows exactly ONE row in `EnchantmentEffects.csv` and
// `PassiveEffects.csv` together carried both -- the slot lock above, which is a
// flag stat. No row anywhere had asked the pipeline for that AND on a damage
// number, so these are the first, and being the first to rely on something is
// where an unexercised path gets found.
//
// EACH TEST BREAKS THE PAIR BOTH WAYS. The right tag with the condition false,
// and the wrong tag with the condition true, both have to grant nothing: a test
// that only showed the reduction arriving could not tell a correctly scoped row
// from one that applies to every skill.
//
// AND EACH ASKS BOTH BUCKETS, WHICH IS HOW THE BUCKET IS SEEN AT ALL. One
// modifier alone cannot show which bucket it is in, because `base x 0.65` and
// `base x (1 - 0.35)` are the same number. `MoreForSkill` and
// `IncreasesForSkill` read the two buckets separately, so a row that landed in
// the wrong one moves the wrong figure and is caught.
// --------------------------------------------------------------------------

namespace CataclysmEnchantmentEffectTest
{
	/** One skill tag, or an empty container if the vocabulary has lost it. */
	FGameplayTagContainer SkillTagged(const TCHAR* Name)
	{
		FGameplayTagContainer Tags;
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
			FName(Name), /*ErrorIfNotFound=*/false);
		if (Tag.IsValid())
		{
			Tags.AddTag(Tag);
		}
		return Tags;
	}

	/** Whether the character carries any modifier at all on a stat. */
	bool CarriesAModifierOn(const UCataclysmAbilitySystemComponent* System,
							const TCHAR* Stat)
	{
		const FCataclysmStatInputs* Inputs = System->GetStatInputs(FName(Stat));
		return Inputs && Inputs->Modifiers.Num() > 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpellsMovingDrawbackTest,
	"Cataclysm.Enchantments.TheSpellMovingDrawbackNeedsTheTagAndTheMovementTogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Spells deal 20%-35% less damage while you are moving".
 *
 * TWO ROWS AND NOT ONE, on `attack_damage` and on `spell_damage`, because that
 * is what the built "Spells deal 20%-40% increased damage" does: the pair of
 * stats is the pair of damage types a skill can deal, and the skill TYPE is
 * carried by the tag. This test drives the attack damage half.
 */
bool FCataclysmSpellsMovingDrawbackTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	const FGameplayTagContainer Spell = SkillTagged(TEXT("Type.Spell"));
	const FGameplayTagContainer Melee = SkillTagged(TEXT("Type.Melee"));
	if (!TestFalse(TEXT("the Type.Spell tag is in the vocabulary"),
				   Spell.IsEmpty())
		|| !TestFalse(TEXT("and so is Type.Melee"), Melee.IsEmpty()))
	{
		return false;
	}

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, SpellsMovingDrawback),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	// THE ROW REACHED THE CHARACTER AT ALL, asked first so a failure below says
	// which of the two things went wrong. A row that never arrived and a
	// condition that never fires look identical from the number alone.
	if (!TestTrue(
			TEXT("the worn drawback put a modifier on attack damage"),
			CarriesAModifierOn(ASC, UCataclysmItemModifiers::AttackDamageStat)))
	{
		return false;
	}

	// STANDING STILL, A SPELL IS NOT REDUCED. One half of the pair alone.
	ASC->NoteDidNotMove();
	CataclysmTestWorld::RunClock(World, 1.0f);
	TestEqual(TEXT("standing still, a spell is not reduced"),
			  ASC->AttackDamageMoreForSkill(Spell), 1.0f, 0.001f);

	ASC->NoteMovedMetres(1.0f);

	// MOVING, A MELEE SKILL IS NOT REDUCED. The other half alone.
	TestEqual(TEXT("moving, a melee skill is not reduced"),
			  ASC->AttackDamageMoreForSkill(Melee), 1.0f, 0.001f);

	// AND BOTH AT ONCE IS THE ROW. A roll of 1 takes the far end of the range,
	// which is 35% less, so the multiplier is 0.65.
	const float Both = ASC->AttackDamageMoreForSkill(Spell);
	TestTrue(*FString::Printf(
				 TEXT("moving, a spell IS reduced: %.4f against 1.0"), Both),
			 Both < 1.0f);
	TestEqual(TEXT("by 35 per cent, the far end of the row's range"), Both,
			  0.65f, 0.001f);

	// AND IT IS A MULTIPLIER RATHER THAN AN INCREASE, which the number above
	// cannot show on its own. An `increased` row would move this sum instead.
	TestEqual(TEXT("and the increases sum is untouched"),
			  ASC->AttackDamageIncreasesForSkill(Spell), 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRangedCloseRangeDrawbackTest,
	"Cataclysm.Enchantments.TheRangedCloseRangeDrawbackNeedsTheTagAndTheDistanceTogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Ranged skills deal 15%-30% less damage at close range (within 5 meters)".
 *
 * THE SENTENCE STATES THE DISTANCE BECAUSE THE ROW USES ONE. "At close range"
 * alone names no number, and a condition value the player cannot read is a
 * hidden number. `docs/DECISIONS.md` carries why five metres and why the
 * sentence says so rather than the table saying it quietly.
 *
 * AN UNKNOWN DISTANCE REFUSES, pinned below rather than left to the condition's
 * own comment: a minion's blow reports -1 deliberately, so a wearer's minions do
 * not carry this drawback.
 */
bool FCataclysmRangedCloseRangeDrawbackTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	const FGameplayTagContainer Ranged = SkillTagged(TEXT("Type.Ranged"));
	const FGameplayTagContainer Melee = SkillTagged(TEXT("Type.Melee"));
	if (!TestFalse(TEXT("the Type.Ranged tag is in the vocabulary"),
				   Ranged.IsEmpty())
		|| !TestFalse(TEXT("and so is Type.Melee"), Melee.IsEmpty()))
	{
		return false;
	}

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, RangedCloseDrawback),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	if (!TestTrue(
			TEXT("the worn drawback put a modifier on attack damage"),
			CarriesAModifierOn(ASC, UCataclysmItemModifiers::AttackDamageStat)))
	{
		return false;
	}

	// The fourth argument is how far away the target stood, in metres.
	const auto More = [ASC](const FGameplayTagContainer& Tags, float Metres)
	{
		return ASC->AttackDamageMoreForSkill(Tags, -1.0f, -1.0f, Metres);
	};

	// A RANGED SKILL AT TEN METRES IS NOT REDUCED. Outside the distance.
	TestEqual(TEXT("a ranged skill at ten metres is not reduced"),
			  More(Ranged, 10.0f), 1.0f, 0.001f);

	// A MELEE SKILL INSIDE THE DISTANCE IS NOT REDUCED. The wrong tag.
	TestEqual(TEXT("a melee skill at three metres is not reduced"),
			  More(Melee, 3.0f), 1.0f, 0.001f);

	// AN UNKNOWN DISTANCE REFUSES, which is what -1 means and what a minion's
	// blow reports.
	TestEqual(TEXT("and an unknown distance grants nothing"),
			  More(Ranged, -1.0f), 1.0f, 0.001f);

	// BOTH TOGETHER IS THE ROW. A roll of 1 takes the far end, 30 per cent less.
	const float Both = More(Ranged, 3.0f);
	TestTrue(*FString::Printf(
				 TEXT("a ranged skill at three metres IS reduced: %.4f "
					  "against 1.0"),
				 Both),
			 Both < 1.0f);
	TestEqual(TEXT("by 30 per cent, the far end of the row's range"), Both,
			  0.70f, 0.001f);

	// AT OR WITHIN, BECAUSE THE SENTENCE WRITES "within 5 meters". A target at
	// exactly five metres is within five metres, which is the boundary Brute's
	// Heart already draws, so the drawback applies there too.
	TestEqual(TEXT("and at exactly five metres it still applies"),
			  More(Ranged, 5.0f), 0.70f, 0.001f);

	TestEqual(TEXT("and the increases sum is untouched"),
			  ASC->AttackDamageIncreasesForSkill(Ranged, -1.0f, -1.0f, 3.0f),
			  0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentChargeKnockdownTest,
	"Cataclysm.Enchantments.AWornChargeKnockdownReachesOnlyChargeSkills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentChargeKnockdownTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentEffectTest;

	// THE HALF THE FIVE KNOCKDOWN TESTS DO NOT COVER, and it is the half this
	// row adds. `Cataclysm.Skills.ACharge*` in CataclysmSkillTemplateTests.cpp
	// gives the character its knockdown by calling `SetStatInputs` directly --
	// its own helper says so and says why. That writes the map `StatForSkill`
	// reads, so those five never run `ApplyTo`, never consult
	// `StatToAttribute()`, and would all pass with no enchantment row in the
	// game at all. They prove a recorded knockdown scopes to charges; this
	// proves wearing the enchantment is a way to record one.
	//
	// THAT DISTINCTION IS THIS PROJECT'S OWN, NOT MINE.
	// `AWornLockReachesOnlyTheSlotAndTheStateItNames` above was written for
	// exactly this reason about the skill lock, and issue #1659 is what a scoped
	// row shipping without this half costs.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn a character in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE TAGS COME OFF REAL SKILL ROWS RATHER THAN BEING TYPED HERE, for the
	// reason the lock test above gives: a row scoped to a keyword no skill
	// carries is scoped to the empty set, and typing the keyword here would make
	// this test agree with itself instead of with `game/Data/WeaponSkills.csv`.
	const UDataTable* Skills =
		LoadCsv<FCataclysmWeaponSkillRow>(TEXT("WeaponSkills.csv"));
	if (!TestNotNull(TEXT("the weapon skill table reads"), Skills))
	{
		return false;
	}

	const FGameplayTag ChargeTag =
		FGameplayTag::RequestGameplayTag(FName(TEXT("Keyword.Charge")));
	if (!TestTrue(TEXT("Keyword.Charge is a registered tag"), ChargeTag.IsValid()))
	{
		return false;
	}

	FGameplayTagContainer ChargeSkillTags;
	FGameplayTagContainer PlainSkillTags;
	int32 ChargeRows = 0;
	for (const TPair<FName, uint8*>& Row : Skills->GetRowMap())
	{
		const auto* Skill =
			reinterpret_cast<const FCataclysmWeaponSkillRow*>(Row.Value);
		if (!Skill)
		{
			continue;
		}

		FGameplayTagContainer Held;
		TArray<FString> Names;
		Skill->Tags.ParseIntoArray(Names, TEXT(","), /*InCullEmpty=*/true);
		for (FString& Name : Names)
		{
			Name.TrimStartAndEndInline();
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
				FName(*Name), /*ErrorIfNotFound=*/false);
			if (Tag.IsValid())
			{
				Held.AddTag(Tag);
			}
		}

		if (Held.HasTag(ChargeTag))
		{
			++ChargeRows;
			if (ChargeSkillTags.IsEmpty())
			{
				ChargeSkillTags = Held;
			}
		}
		else if (PlainSkillTags.IsEmpty() && !Held.IsEmpty())
		{
			PlainSkillTags = Held;
		}
	}

	// WITHOUT THESE THE READS BELOW ASK ABOUT EMPTY CONTAINERS, and a modifier
	// requiring a tag refuses an empty one -- so every figure would be zero and
	// the test would pass having measured nothing.
	if (!TestTrue(FString::Printf(
			TEXT("the data holds charge skills carrying Keyword.Charge: %d"),
			ChargeRows), ChargeRows > 0))
	{
		return false;
	}
	if (!TestFalse(TEXT("and a skill without the keyword to compare against"),
				   PlainSkillTags.IsEmpty()))
	{
		return false;
	}

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;
	const FName Stat = FName(UCataclysmSkillEffects::KnockdownSecondsStat);

	Wearer.Equipment->RefreshAttributes(ASC);
	TestEqual(
		TEXT("a character wearing nothing knocks nothing down with a charge"),
		ASC->StatForSkill(Stat, ChargeSkillTags, 0.0f), 0.0f, 0.001f);

	// THE BENEFIT, PAIRED WITH A DRAWBACK THAT HAS NO EFFECT ROW, so the
	// knockdown is the only thing this item changes.
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), ChargeKnockdownBenefit, DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	// AT THE TOP OF THE RANGE, BECAUSE THE ROLL DEFAULTS TO ITS HIGHEST.
	// `FCataclysmRolledEnchantment::PositiveRoll` is `1.0f`
	// (`CataclysmItem.h:138`) and `Carrying` does not set it, so this item rolled
	// the high end. `UCataclysmItemValues::EnchantmentValue` says a roll of
	// exactly 1 is held in the last share, "which is where the default of 1 is
	// meant to land". The row states 1 to 2 seconds, so that is two seconds.
	//
	// THE FIRST VERSION OF THIS LINE ASSERTED ONE SECOND, on a comment of mine
	// that said the roll defaults to zero. It does not. The run answered 2.0 and
	// the test was wrong rather than the code -- which is what the restored half
	// of a guard proof is for.
	//
	// ASSERTING THE NUMBER RATHER THAN "MORE THAN NOTHING" IS STILL THE POINT.
	// A row whose two ends were written the wrong way round would answer 1.0
	// here, and no count anywhere would show it.
	TestEqual(TEXT("the worn benefit gives a charge skill its stated high roll"),
			  ASC->StatForSkill(Stat, ChargeSkillTags, 0.0f), 2.0f, 0.001f);

	// AND NOTHING ELSE. `Keyword.Charge` is the only thing scoping this row, and
	// a skill without it is what says the scoping works.
	TestEqual(TEXT("and leaves a skill without the keyword alone"),
			  ASC->StatForSkill(Stat, PlainSkillTags, 0.0f), 0.0f, 0.001f);

	// THE CHARACTER SHEET SHOWS NOTHING EITHER, which is the same reading with no
	// skill in hand. A scoped modifier must not apply to a bare stat.
	TestEqual(TEXT("and shows nothing with no skill in hand"),
			  ASC->StatForSkill(Stat, FGameplayTagContainer(), 0.0f), 0.0f, 0.001f);

	// AND TAKING THE HELM OFF TAKES IT AWAY, which says the knockdown came from
	// the item rather than from anything else about the character.
	Wearer.Equipment->Unequip(Slot, Removed);
	Wearer.Equipment->RefreshAttributes(ASC);
	TestEqual(TEXT("and taking the helm off leaves the charge skill alone"),
			  ASC->StatForSkill(Stat, ChargeSkillTags, 0.0f), 0.0f, 0.001f);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpecialMovingBenefitTest,
	"Cataclysm.Enchantments.TheSpecialAbilityBenefitNeedsTheSlotAndTheMovementTogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your special ability deals 20%-40% increased damage while you are moving".
 *
 * THE SAME SHAPE AS THE SPELLS-MOVING DRAWBACK ABOVE, IN THE OTHER BUCKET.
 * That row is `more`, and a `more` modifier which does not apply leaves a
 * multiplier of exactly 1 -- which is also what carrying no modifier at all
 * leaves. This row is `increased`, so one which does not apply leaves the
 * increases sum at zero instead. Both rows need the tag and the state together
 * and the two buckets fail in different places, so neither test covers the
 * other.
 *
 * `Slot.Special` IS ON 79 OF THE 403 ROWS OF `game/Data/WeaponSkills.csv`, one
 * per weapon type, so this row has real reach. The tag is asked for by name
 * rather than typed into an expectation, so a tag that left the vocabulary
 * fails here rather than silently matching nothing.
 */
bool FCataclysmSpecialMovingBenefitTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	const FGameplayTagContainer Special = SkillTagged(TEXT("Slot.Special"));
	const FGameplayTagContainer Ultimate = SkillTagged(TEXT("Slot.Ultimate"));
	if (!TestFalse(TEXT("the Slot.Special tag is in the vocabulary"),
				   Special.IsEmpty())
		|| !TestFalse(TEXT("and so is Slot.Ultimate"), Ultimate.IsEmpty()))
	{
		return false;
	}

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), SpecialMovingBenefit, DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	// THE ROW REACHED THE CHARACTER AT ALL, asked first so that a zero below
	// says which of the two things went wrong. A row that never arrived and a
	// condition that never fires are the same number.
	if (!TestTrue(
			TEXT("the worn benefit put a modifier on attack damage"),
			CarriesAModifierOn(ASC, UCataclysmItemModifiers::AttackDamageStat)))
	{
		return false;
	}

	// STANDING STILL, THE SPECIAL SLOT GAINS NOTHING. One half of the pair.
	ASC->NoteDidNotMove();
	CataclysmTestWorld::RunClock(World, 1.0f);
	TestEqual(TEXT("standing still, a special ability gains nothing"),
			  ASC->AttackDamageIncreasesForSkill(Special), 0.0f, 0.001f);

	ASC->NoteMovedMetres(1.0f);

	// MOVING, ANOTHER SLOT GAINS NOTHING. The other half.
	TestEqual(TEXT("moving, an ultimate gains nothing"),
			  ASC->AttackDamageIncreasesForSkill(Ultimate), 0.0f, 0.001f);

	// AND BOTH AT ONCE IS THE ROW. A rolled enchantment built in code carries a
	// roll of 1, which `UCataclysmItemValues::EnchantmentValue` puts on the
	// second number, so this is the 40 and not the 20.
	//
	// THE EXACT FIGURE RATHER THAN "MORE THAN NOTHING", which is what catches a
	// row whose two ends were written the wrong way round. It is a fraction
	// because `AttackDamageIncreasesForSkill` divides the pipeline's percentage
	// points by 100.
	TestEqual(TEXT("moving, a special ability gains the far end of the range"),
			  ASC->AttackDamageIncreasesForSkill(Special), 0.4f, 0.001f);

	// AND IT IS AN INCREASE RATHER THAN A MULTIPLIER, which the number above
	// cannot show on its own. A `more` row would move this one instead.
	TestEqual(TEXT("and the more multiplier is untouched"),
			  ASC->AttackDamageMoreForSkill(Special), 1.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBelowHalfDrawbackTest,
	"Cataclysm.Enchantments.TheBelowHalfDrawbackNeedsATargetUnderTheThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "You deal 25%-40% less damage to enemies below 50% HP".
 *
 * THE FIRST ENCHANTMENT ROW TO USE `target_health_below`. Two passive nodes use
 * the condition, so the pipeline's half is covered. What nothing covered is
 * that the condition survives the route an enchantment takes, which runs
 * through `UCataclysmItemModifiers` and `UCataclysmPlayerClassStats::ApplyTo`
 * rather than through a stat line written in code.
 *
 * THE CONDITION IS REFUSED WHEN NO TARGET IS IN HAND, which the first assertion
 * pins. `UCataclysmAbilitySystemComponent::WithTargetState` leaves the reading
 * at -1 for a null target and `UCataclysmStatPipeline::ConditionHolds` refuses
 * a negative reading. So a character asking for its damage with nobody in front
 * of it -- the character sheet, for one -- correctly sees no reduction.
 *
 * STRICTLY BELOW, so a target sitting on exactly half health is not under the
 * threshold. That boundary is asserted rather than assumed, and the health it
 * is asserted at is read back after being written, so a target that failed to
 * take the health it was given cannot pass this as though the condition had
 * done the refusing.
 */
bool FCataclysmBelowHalfDrawbackTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	FWearer Enemy(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, BelowHalfDrawback),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	if (!TestTrue(
			TEXT("the worn drawback put a modifier on attack damage"),
			CarriesAModifierOn(ASC, UCataclysmItemModifiers::AttackDamageStat)))
	{
		return false;
	}

	// NO TARGET IN HAND, NO REDUCTION.
	TestEqual(TEXT("with nobody in front of it, damage is not reduced"),
			  ASC->AttackDamageMoreForSkill(FGameplayTagContainer()), 1.0f,
			  0.001f);

	// A TARGET ON EXACTLY HALF HEALTH IS NOT BELOW HALF. The state is built and
	// then read back before anything is concluded from it.
	UCataclysmAbilitySystemComponent* Theirs = Enemy.AbilitySystem;
	Theirs->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100.0f);
	Theirs->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), 50.0f);
	if (!TestEqual(TEXT("the target really is on exactly half health"),
				   Theirs->GetNumericAttribute(
					   UCataclysmVitalAttributeSet::GetHealthAttribute()),
				   50.0f, 0.001f))
	{
		return false;
	}
	TestEqual(TEXT("a target on exactly half health is not below half"),
			  ASC->AttackDamageMoreForSkill(FGameplayTagContainer(), -1.0f,
											-1.0f, -1.0f, false, Enemy.Actor),
			  1.0f, 0.001f);

	// AND ONE POINT UNDER IT IS. A roll of 1 takes the far end of the range,
	// which is 40% less, so the multiplier is 0.6.
	Theirs->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), 49.0f);
	TestEqual(TEXT("and a target below half takes the far end of the range"),
			  ASC->AttackDamageMoreForSkill(FGameplayTagContainer(), -1.0f,
											-1.0f, -1.0f, false, Enemy.Actor),
			  0.6f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHealingReducedDrawbackTest,
	"Cataclysm.Enchantments.TheHealingReductionCutsWhatATopUpReturns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Healing effects on you are reduced by 30%-50%".
 *
 * THE FIRST DATA ROW ANYWHERE ON `healing_received_reduction`. Until this row
 * the stat's only source was C++ -- the Death's Embrace dungeon rule at
 * `CataclysmDungeonModifierEffects.cpp:457` -- so nothing proved a row in a
 * sheet can reach it.
 *
 * IT MUST BE `flat` AND NOT `increased`. The stat's base is zero, no class line
 * names it and `EngineSuppliedBases` does not either, so an increase would
 * multiply nothing and the wearer would be healed in full.
 *
 * THE STAT DOES MORE THAN THIS SENTENCE SAYS, AND THAT IS DELIBERATE. The
 * project owner ruled on 2026-09-12 that healing received covers regeneration
 * and leech as well as direct healing, and issue #1609 is the standing record
 * that these words do not say so. This test drives the regeneration route,
 * because that is the route `UCataclysmRegeneration::TopUp` takes, and is
 * asserting the ruled behaviour rather than the sentence.
 */
bool FCataclysmHealingReducedDrawbackTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();

	// A POOL WITH ROOM IN IT, so that what is offered is not trimmed by the
	// maximum and the only thing that can shrink it is the row.
	ASC->SetNumericAttributeBase(MaxHealth, 1000.0f);
	ASC->SetNumericAttributeBase(Health, 0.0f);

	// WHAT AN UNREDUCED TOP-UP RETURNS, measured on this character before the
	// item goes on rather than assumed to be the number offered.
	UCataclysmRegeneration::TopUp(*ASC, Health, MaxHealth, 100.0f);
	const float Unreduced = ASC->GetNumericAttribute(Health);
	if (!TestEqual(TEXT("with nothing worn, a top-up of 100 returns 100"),
				   Unreduced, 100.0f, 0.001f))
	{
		return false;
	}

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, HealingReducedDrawback),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	// THE ROW REACHED THE ATTRIBUTE. This stat is read off the attribute rather
	// than asked for through `StatForSkill`, so the attribute is what has to
	// have moved. A roll of 1 takes the far end of the range, which is 50.
	TestEqual(
		TEXT("the worn drawback wrote 50 onto the healing reduction"),
		ASC->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealingReceivedReductionAttribute()),
		50.0f, 0.001f);

	// AND THE SAME TOP-UP NOW RETURNS HALF OF IT.
	ASC->SetNumericAttributeBase(Health, 0.0f);
	UCataclysmRegeneration::TopUp(*ASC, Health, MaxHealth, 100.0f);
	const float Reduced = ASC->GetNumericAttribute(Health);
	TestEqual(TEXT("and wearing it, the same top-up returns 50"), Reduced,
			  50.0f, 0.001f);
	TestTrue(*FString::Printf(
				 TEXT("which is less than the %.1f it returned before"),
				 Unreduced),
			 Reduced < Unreduced);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReflectBenefitTest,
	"Cataclysm.Enchantments.TheReflectBenefitSendsBackAShareOfTheBlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Reflect 20%-50% of damage taken back to attackers".
 *
 * THE STAT IS ALREADY A SHARE OF THE BLOW TAKEN, so the row is `flat` 20 to 50
 * rather than a percentage of some other figure.
 * `UCataclysmRetaliation::AmountFor` divides by 100, so a value of 50 sends
 * back half of what was taken.
 *
 * `flat` AND NOT `increased`, because the stat's base is zero for every class
 * but the Masochist. An increase would multiply nothing.
 *
 * ONE OF TWO NEARLY IDENTICAL ENCHANTMENTS, and they are separate rows of
 * `game/Data/EnchantmentsPositive.csv` rather than one duplicated: this one
 * states 20%-50%, and "Reflect 20%-40% of damage taken back at attackers as
 * retaliation damage" states 20%-40%. Both were written, each with its own
 * range.
 */
bool FCataclysmReflectBenefitTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	// NOTHING COMES BACK BEFORE THE ITEM GOES ON, measured rather than assumed.
	// A class line granting retaliation would make every figure below larger,
	// and this is what would catch it.
	if (!TestEqual(TEXT("wearing nothing, a blow of 200 sends nothing back"),
				   UCataclysmRetaliation::AmountFor(ASC, 200.0f), 0.0f, 0.001f))
	{
		return false;
	}

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), ReflectBenefit, DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	// A ROLL OF 1 TAKES THE FAR END, which is 50, so half of a blow of 200 is
	// 100. The exact figure rather than "more than nothing", because that is
	// what catches a row whose two ends were written the wrong way round.
	TestEqual(TEXT("wearing it, a blow of 200 sends back half of it"),
			  UCataclysmRetaliation::AmountFor(ASC, 200.0f), 100.0f, 0.001f);

	// AND IT SCALES WITH THE BLOW RATHER THAN BEING A FIXED AMOUNT, which one
	// figure on its own cannot show.
	TestEqual(TEXT("and half of a blow of 60 is 30"),
			  UCataclysmRetaliation::AmountFor(ASC, 60.0f), 30.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentEventWindowRowsTest,
	"Cataclysm.Enchantments.AWindowRowCarriesItsOwnEventAndItsOwnSecondsAndNoTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentEventWindowRowsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentEffectTest;

	FTables Tables;
	if (!LoadAll(*this, Tables))
	{
		return false;
	}

	// THE THREE SENTENCES THAT ASKED FOR THESE CONDITIONS, each checked against
	// the row the workbook now holds for it. Issue #1826.
	//
	// THE REQUIRED TAGS MUST BE EMPTY, AND THAT IS AN ASSERTION RATHER THAN AN
	// OMISSION. Attack speed is asked for in CataclysmBasicAttack.cpp with an
	// empty tag container -- its own comment says a modifier scoped to a tag
	// does not reach it -- so a required tag written on either attack-speed row
	// would leave the row validating, passing every other check here, and
	// granting nothing in play.
	struct FCase
	{
		const TCHAR* Enchantment;
		const TCHAR* Stat;
		ECataclysmStatCondition Condition;
		float WindowSeconds;
		float Low;
		float High;
	};

	const FCase Cases[] = {
		{ChargeWindowBenefit, TEXT("attack_speed"),
		 ECataclysmStatCondition::WithinSecondsOfChargeSkill, 4.0f, 20.0f, 40.0f},
		{BasicAttackWindowBenefit, TEXT("attack_speed"),
		 ECataclysmStatCondition::WithinSecondsOfBasicAttack, 4.0f, 5.0f, 10.0f},
		// TWO STATS FOR ONE SENTENCE, which is how every other "increased
		// damage" enchantment in the table is written.
		{BlockWindowBenefit, TEXT("attack_damage"),
		 ECataclysmStatCondition::WithinSecondsOfBlock, 3.0f, 10.0f, 20.0f},
		{BlockWindowBenefit, TEXT("spell_damage"),
		 ECataclysmStatCondition::WithinSecondsOfBlock, 3.0f, 10.0f, 20.0f},
	};

	for (const FCase& Case : Cases)
	{
		int32 Added = 0;
		const FTotals Totals = Gather(
			Tables,
			{Carrying(TEXT("Head_Helm"), Case.Enchantment, DrawbackWithNoEffect)},
			Added);

		const TArray<FCataclysmStatModifier>* Modifiers =
			Totals.Find(FName(Case.Stat));
		if (!TestNotNull(FString::Printf(TEXT("%s granted %s"), Case.Enchantment,
										 Case.Stat),
						 Modifiers)
			|| !TestEqual(FString::Printf(TEXT("%s granted exactly one %s"),
										  Case.Enchantment, Case.Stat),
						  Modifiers->Num(), 1))
		{
			continue;
		}

		const FCataclysmStatModifier& Modifier = (*Modifiers)[0];

		TestEqual(FString::Printf(TEXT("%s: %s is an increase"),
								  Case.Enchantment, Case.Stat),
				  static_cast<int32>(Modifier.Bucket),
				  static_cast<int32>(ECataclysmStatBucket::Increased));
		TestEqual(FString::Printf(TEXT("%s: %s names its own event"),
								  Case.Enchantment, Case.Stat),
				  static_cast<int32>(Modifier.Condition),
				  static_cast<int32>(Case.Condition));
		TestEqual(FString::Printf(TEXT("%s: %s states its own window"),
								  Case.Enchantment, Case.Stat),
				  Modifier.ConditionValue, Case.WindowSeconds);

		// THE ROLLED VALUE SITS INSIDE THE SENTENCE'S OWN RANGE. The roll is
		// what it is, so the assertion is the bracket rather than a number.
		TestTrue(FString::Printf(
					 TEXT("%s: %s rolls between %g and %g, and rolled %g"),
					 Case.Enchantment, Case.Stat, Case.Low, Case.High,
					 Modifier.Value),
				 Modifier.Value >= Case.Low && Modifier.Value <= Case.High);

		TestTrue(FString::Printf(
					 TEXT("%s: %s is scoped to no tag, so it reaches a read "
						  "that asks with none"),
					 Case.Enchantment, Case.Stat),
				 Modifier.RequiredTags.IsEmpty());
	}

	return true;
}


// ---------------------------------------------------------------------------
// A WORN ROW THAT MOVES A POOL WHEN AN EVENT HAPPENS. Issue #1815.
//
// A STAT ROW IS PULLED AND AN ACTION ROW IS PUSHED. The pipeline reads a stat
// modifier when something asks for the stat; an action happens at the moment
// its event does and is then over. Seventeen authored enchantments say
// "restore", "generate" or "drain", and none of them can be written as a
// modifier.
//
// EIGHT DATA ROWS USE THIS SINCE 2026-09-16, and every test below but one still
// writes its own action, so that it can choose figures that tell the answers
// apart. `AnAuthoredBlockRowFromTheBuiltTableRestoresTheHealthItStates` is the
// one that reads a real row, out of the asset the game loads.

namespace CataclysmEnchantmentEffectTest
{
	/** An effect table built from a CSV string, for a row the real data has not got. */
	UDataTable* EffectTableFrom(const FString& Contents)
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmEnchantmentEffectRow::StaticStruct();
		if (Table->CreateTableFromCSVString(Contents).Num() > 0)
		{
			return nullptr;
		}
		return Table;
	}

	/** One action, as the equipment refresh would hand it over. */
	FCataclysmPoolAction PoolAction(const TCHAR* Event, const TCHAR* Pool,
								   float Percent,
					   ECataclysmPoolActionBase Base =
						   ECataclysmPoolActionBase::Maximum)
	{
		FCataclysmPoolAction Out;
		Out.Event = FName(Event);
		Out.Pool = FName(Pool);
		Out.Percent = Percent;
		Out.Base = Base;
		return Out;
	}

	/** A character with the four pools set to known figures. */
	void GivePools(UCataclysmAbilitySystemComponent& AbilitySystem,
				   float Health, float MaxHealth, float Resource = 0.0f,
				   float MaxResource = 0.0f)
	{
		AbilitySystem.SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), MaxHealth);
		AbilitySystem.SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
		AbilitySystem.SetNumericAttributeBase(
			UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
			MaxResource);
		AbilitySystem.SetNumericAttributeBase(
			UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
			Resource);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmAnActionRowIsNotAStatModifier,
	"Cataclysm.Enchantments.AnActionRowBecomesAPoolActionAndNotAStatModifier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAnActionRowIsNotAStatModifier::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// THE BREAK THIS IS FOR: dropping the action branch in
	// `AccumulateEnchantmentsInto`, which would send an action row to the stat
	// totals keyed by an empty stat name -- where nothing would ever read it and
	// nothing would say so.
	UDataTable* Positive =
		LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	UDataTable* Negative =
		LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
	if (!TestNotNull(TEXT("the positive enchantments"), Positive)
		|| !TestNotNull(TEXT("the negative enchantments"), Negative))
	{
		return false;
	}

	// THE ROW NAMES A REAL ENCHANTMENT, because the accumulator asks the real
	// tables whether that name belongs to a set. Only the effect row is invented.
	UDataTable* Effects = EffectTableFrom(
		FString(TEXT("Name,Enchantment,Stat,ValueKind,ValueLow,ValueHigh,"
					 "RequiredTags,Condition,ConditionValue,Scale,ScaleStep,"
					 "Action,ActionEvent,FractionOf,ScaleMaxSteps,StackSeconds,ScaleOffset,EverySeconds,EveryNth,ScaleStepHigh,StackSecondsHigh,Condition2,ConditionValue2,ConditionValueHigh,TriggerCooldown,EventValue,Ailment,DamageShare\n"))
		+ FString::Printf(
			TEXT("%s#1,%s,,,4,4,,,0,,0,health,block,maximum,0,0,0,0,0,0,0,,0,0,0,0,,0\n"),
			ShieldBenefit, ShieldBenefit));
	if (!TestNotNull(TEXT("an effect table holding one action row"), Effects))
	{
		return false;
	}

	const TArray<FCataclysmItem> Worn = {
		// A REAL DRAWBACK NAME RATHER THAN NOTHING. Every other call in this file
		// passes one, `Carrying` does `FName(Negative)` with whatever it is given,
		// and this one has no effect row, so it adds nothing and cannot be what
		// makes the assertions below pass.
		Carrying(TEXT("Head_Helm"), ShieldBenefit, DrawbackWithNoEffect)};

	TMap<FName, TArray<FCataclysmStatModifier>> Totals;
	TArray<FCataclysmPoolAction> Actions;
	UCataclysmItemModifiers::AccumulateEnchantmentsInto(
		Totals, Worn, Effects, Positive, Negative, &Actions);

	TestEqual(TEXT("it did not become a stat modifier"), Totals.Num(), 0);
	if (!TestEqual(TEXT("it became one pool action"), Actions.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("on the event it names"), Actions[0].Event,
			  FName(TEXT("block")));
	TestEqual(TEXT("moving the pool it names"), Actions[0].Pool,
			  FName(TEXT("health")));
	TestEqual(TEXT("by the percentage it states"), Actions[0].Percent, 4.0f,
			  0.001f);
	TestEqual(TEXT("of the maximum"), Actions[0].Base,
			  ECataclysmPoolActionBase::Maximum);

	// AND A CALLER THAT WANTS NO ACTIONS GETS NONE RATHER THAN A STAT MODIFIER,
	// which is what the optional parameter has to mean.
	TMap<FName, TArray<FCataclysmStatModifier>> Ignored;
	UCataclysmItemModifiers::AccumulateEnchantmentsInto(
		Ignored, Worn, Effects, Positive, Negative, nullptr);
	TestEqual(TEXT("and no caller gets it as a stat either way"), Ignored.Num(),
			  0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmAnActionFiresOnItsOwnEventOnly,
	"Cataclysm.Enchantments.AnActionFiresOnItsOwnEventAndOnNoOther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAnActionFiresOnItsOwnEventOnly::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// TWO BREAKS, AND NEITHER CATCHES THE OTHER. Deleting the dispatch line
	// inside `NoteBlocked` fails the first half; firing every action whatever
	// the event fails the second. A version that fires on everything passes any
	// test that only checks the event it was named for.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent& ASC = *Wearer.AbilitySystem;
	GivePools(ASC, /*Health=*/100.0f, /*MaxHealth=*/500.0f);

	ASC.SetPoolActions({PoolAction(TEXT("block"), TEXT("health"), 10.0f)});

	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();

	// NOTHING HAS HAPPENED YET, so a character that was simply healed at birth
	// would fail here rather than passing the assertion below by accident.
	if (!TestEqual(TEXT("health starts where it was put"),
				   ASC.GetNumericAttribute(Health), 100.0f, 0.01f))
	{
		return false;
	}

	ASC.NoteEvaded();
	TestEqual(TEXT("a dodge does not fire a row hung on a block"),
			  ASC.GetNumericAttribute(Health), 100.0f, 0.01f);

	ASC.NoteBlocked();
	TestEqual(TEXT("and a block restores a tenth of the maximum"),
			  ASC.GetNumericAttribute(Health), 150.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmGenerationRateScalesARowsGrant,
	"Cataclysm.Enchantments.GenerationRateScalesAPoolActionThatGrantsClassResourceAndNotOneThatTakesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGenerationRateScalesARowsGrant::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// THE EIGHTH PLACE THE CLASS RESOURCE IS GAINED. Ruled 2026-10-07:
	// `ApplyPoolAction` scales a POSITIVE amount for the pool `class_resource`
	// by `class_resource_generation`, and leaves a row that takes class resource
	// away as it was.
	//
	// ON `block`, AND ONCE FOR ALL THREE AUTHORED ROWS. The rows that grant class
	// resource wait on `block`, `skill_use` and `hit_dealt`; they differ only in
	// the event, and every one reaches the same line in `ApplyPoolAction`.
	//
	// ONE WEARER ALIVE AT A TIME: `FWearer` spawns a plain actor on the origin, so
	// each reading makes its own and destroys it before the next.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const FGameplayAttribute MaximumPool = UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute();
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();

	struct FMoved
	{
		bool bSetUp = false;
		float Resource = 0.0f;
		float Health = 0.0f;
	};

	// A POOL OF 200 SO A PERCENTAGE OF THE MAXIMUM IS NOT THE PERCENTAGE ITSELF,
	// and health at 100 of 500 so a health row has room to restore.
	const auto BlockOnce = [&](const TCHAR* Who, const TOptional<float>& Increase, const TCHAR* PoolName,
							   float Percent, float StartAt) -> FMoved
	{
		FMoved Out;
		FWearer Wearer(World);
		UCataclysmAbilitySystemComponent& ASC = *Wearer.AbilitySystem;
		GivePools(ASC, /*Health=*/100.0f, /*MaxHealth=*/500.0f, /*Resource=*/StartAt, /*MaxResource=*/200.0f);
		if (Increase.IsSet())
		{
			FCataclysmStatModifier Rate;
			Rate.Bucket = ECataclysmStatBucket::Increased;
			Rate.Source = ECataclysmModifierSource::Enchantment;
			Rate.Value = Increase.GetValue();

			TMap<FName, FCataclysmStatInputs> Stats;
			FCataclysmStatInputs& Line = Stats.FindOrAdd(
				FName(UCataclysmAbilitySystemComponent::ClassResourceGenerationStat));
			Line.Base = UCataclysmAbilitySystemComponent::NormalClassResourceGeneration;
			Line.Modifiers = {Rate};
			ASC.SetStatInputs(MoveTemp(Stats));
		}
		ASC.SetPoolActions({PoolAction(TEXT("block"), PoolName, Percent)});

		const bool bMaximum = TestEqual(
			*FString::Printf(TEXT("%s: set-up: the class resource maximum is 200"), Who),
			ASC.GetNumericAttribute(MaximumPool), 200.0f, 0.01f);
		const bool bStart = TestEqual(
			*FString::Printf(TEXT("%s: set-up: the class resource starts where it was put"), Who),
			ASC.GetNumericAttribute(Pool), StartAt, 0.01f);
		const bool bHealth = TestEqual(
			*FString::Printf(TEXT("%s: set-up: health starts at 100"), Who),
			ASC.GetNumericAttribute(Health), 100.0f, 0.01f);
		const bool bRate = TestEqual(
			*FString::Printf(TEXT("%s: set-up: class_resource_generation reads 100 and its increase"), Who),
			ASC.StatForSkill(FName(UCataclysmAbilitySystemComponent::ClassResourceGenerationStat),
							 FGameplayTagContainer(),
							 UCataclysmAbilitySystemComponent::NormalClassResourceGeneration),
			100.0f + Increase.Get(0.0f), 0.01f);
		Out.bSetUp = bMaximum && bStart && bHealth && bRate;
		if (!Out.bSetUp)
		{
			return Out;
		}

		ASC.NoteBlocked();
		Out.Resource = ASC.GetNumericAttribute(Pool) - StartAt;
		Out.Health = ASC.GetNumericAttribute(Health) - 100.0f;
		return Out;
	};

	// A GRANT OF 10% OF THE MAXIMUM, FROM 50 OF 200.
	const FMoved Control = BlockOnce(TEXT("a grant, the control"), TOptional<float>(),
									 TEXT("class_resource"), 10.0f, 50.0f);
	const FMoved Faster = BlockOnce(TEXT("a grant, increased by 40"), TOptional<float>(40.0f),
									TEXT("class_resource"), 10.0f, 50.0f);
	const FMoved Slower = BlockOnce(TEXT("a grant, decreased by 50"), TOptional<float>(-50.0f),
									TEXT("class_resource"), 10.0f, 50.0f);
	if (!Control.bSetUp || !Faster.bSetUp || !Slower.bSetUp)
	{
		return false;
	}
	if (!TestEqual(TEXT("set-up: the control's block grants a tenth of 200"), Control.Resource, 20.0f, 0.01f))
	{
		return false;
	}
	TestEqual(TEXT("increased by 40, the grant is 1.4 times the control's"),
			  Faster.Resource, Control.Resource * 1.4f, 0.01f);
	TestEqual(TEXT("decreased by 50, the grant is half the control's"),
			  Slower.Resource, Control.Resource * 0.5f, 0.01f);

	// A ROW THAT TAKES CLASS RESOURCE AWAY IS NOT SCALED.
	const FMoved DrainControl = BlockOnce(TEXT("a drain, the control"), TOptional<float>(),
										  TEXT("class_resource"), -10.0f, 50.0f);
	const FMoved DrainFaster = BlockOnce(TEXT("a drain, increased by 40"), TOptional<float>(40.0f),
										 TEXT("class_resource"), -10.0f, 50.0f);
	const FMoved DrainSlower = BlockOnce(TEXT("a drain, decreased by 50"), TOptional<float>(-50.0f),
										 TEXT("class_resource"), -10.0f, 50.0f);
	if (!DrainControl.bSetUp || !DrainFaster.bSetUp || !DrainSlower.bSetUp)
	{
		return false;
	}
	if (TestEqual(TEXT("set-up: the control's block takes a tenth of 200"), DrainControl.Resource, -20.0f, 0.01f))
	{
		TestEqual(TEXT("increased by 40, the row takes what the control's took"),
				  DrainFaster.Resource, DrainControl.Resource, 0.01f);
		TestEqual(TEXT("decreased by 50, the row takes what the control's took"),
				  DrainSlower.Resource, DrainControl.Resource, 0.01f);
	}

	// A FASTER GRANT STILL STOPS AT THE MAXIMUM: from 190 of 200, twenty-eight
	// is asked for and ten fit.
	const FMoved NearFull = BlockOnce(TEXT("a grant near the maximum, increased by 40"), TOptional<float>(40.0f),
									  TEXT("class_resource"), 10.0f, 190.0f);
	if (!NearFull.bSetUp)
	{
		return false;
	}
	TestEqual(TEXT("a faster grant stops at the maximum"), NearFull.Resource, 10.0f, 0.01f);

	// AND ONLY THE CLASS RESOURCE POOL. A health row on the same wearer restores
	// what it did.
	const FMoved HealthControl = BlockOnce(TEXT("a health row, the control"), TOptional<float>(),
										   TEXT("health"), 10.0f, 50.0f);
	const FMoved HealthFaster = BlockOnce(TEXT("a health row, increased by 40"), TOptional<float>(40.0f),
										  TEXT("health"), 10.0f, 50.0f);
	if (!HealthControl.bSetUp || !HealthFaster.bSetUp)
	{
		return false;
	}
	if (TestEqual(TEXT("set-up: the control's block restores a tenth of 500 health"),
				  HealthControl.Health, 50.0f, 0.01f))
	{
		TestEqual(TEXT("the generation rate does not reach a health row"),
				  HealthFaster.Health, HealthControl.Health, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmARestoreAddsAndADrainTakes,
	"Cataclysm.Enchantments.ARestoreAddsToThePoolAndADrainTakesFromIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmARestoreAddsAndADrainTakes::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// THE BREAK THIS IS FOR: losing the sign, by taking the absolute value or by
	// sending every action down the restoring path. A drain would then heal, and
	// six of the seventeen authored rows say drain or reduce.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent& ASC = *Wearer.AbilitySystem;
	GivePools(ASC, /*Health=*/300.0f, /*MaxHealth=*/500.0f);

	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();

	ASC.SetPoolActions({PoolAction(TEXT("block"), TEXT("health"), -10.0f)});
	ASC.NoteBlocked();
	if (!TestEqual(TEXT("a negative percentage takes health away"),
				   ASC.GetNumericAttribute(Health), 250.0f, 0.01f))
	{
		return false;
	}

	ASC.SetPoolActions({PoolAction(TEXT("block"), TEXT("health"), 10.0f)});
	ASC.NoteBlocked();
	TestEqual(TEXT("and a positive one gives it back"),
			  ASC.GetNumericAttribute(Health), 300.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmAFractionOfHeldIsNotAFractionOfTheMaximum,
	"Cataclysm.Enchantments.AFractionOfWhatIsHeldIsNotAFractionOfTheMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAFractionOfHeldIsNotAFractionOfTheMaximum::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// THE BREAK THIS IS FOR: ignoring the row's chosen base and always
	// maximum. On a character at full health the two answers are the same, so
	// this one is deliberately hurt -- 200 of 500 -- and the two answers are 20
	// and 50.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent& ASC = *Wearer.AbilitySystem;
	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();

	GivePools(ASC, /*Health=*/200.0f, /*MaxHealth=*/500.0f);
	if (!TestEqual(TEXT("the character is hurt, so the two differ"),
				   ASC.GetNumericAttribute(Health), 200.0f, 0.01f))
	{
		return false;
	}

	ASC.SetPoolActions({PoolAction(TEXT("block"), TEXT("health"), 10.0f,
							   ECataclysmPoolActionBase::Current)});
	ASC.NoteBlocked();
	TestEqual(TEXT("a tenth of what is held is twenty"),
			  ASC.GetNumericAttribute(Health), 220.0f, 0.01f);

	GivePools(ASC, /*Health=*/200.0f, /*MaxHealth=*/500.0f);
	ASC.SetPoolActions({PoolAction(TEXT("block"), TEXT("health"), 10.0f,
							   ECataclysmPoolActionBase::Maximum)});
	ASC.NoteBlocked();
	TestEqual(TEXT("and a tenth of the maximum is fifty"),
			  ASC.GetNumericAttribute(Health), 250.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmADrainCannotKill,
	"Cataclysm.Enchantments.ADrainLeavesACharacterAliveAtOneHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmADrainCannotKill::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// THE BREAK THIS IS FOR: removing the floor, so a drain of the maximum on a
	// nearly dead character takes it to zero. The project owner's delegate ruled
	// on 2026-09-14 that a drain is a cost and not damage, and a cost does not
	// kill.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent& ASC = *Wearer.AbilitySystem;
	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();

	// TEN OF FIVE HUNDRED, AND A DRAIN OF A FIFTH OF THE MAXIMUM. A hundred is
	// far more than is left, so a floor that is missing shows as zero rather
	// than as a number close to the right one.
	GivePools(ASC, /*Health=*/10.0f, /*MaxHealth=*/500.0f);
	ASC.SetPoolActions({PoolAction(TEXT("block"), TEXT("health"), -20.0f)});
	ASC.NoteBlocked();
	TestEqual(TEXT("the drain stops at one health"),
			  ASC.GetNumericAttribute(Health), 1.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmARestoreOfHealthIsHealing,
	"Cataclysm.Enchantments.ARestoreOfHealthIsHealingAndSoCostsFervour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmARestoreOfHealthIsHealing::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// THE BREAK THIS IS FOR: restoring health by writing the attribute instead of
	// going through `UCataclysmRegeneration::TopUp`. The health would still
	// arrive, so every other test here would pass; what would be lost is that the
	// restore counts as healing. The project owner's delegate ruled on 2026-09-14
	// that it does, which means the Masochist's rule that healing removes Fervour
	// applies to it, and that rule is written about healing with no exception for
	// an item.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent& ASC = *Wearer.AbilitySystem;
	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute Resource =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();

	GivePools(ASC, /*Health=*/200.0f, /*MaxHealth=*/500.0f,
			  /*Resource=*/100.0f, /*MaxResource=*/100.0f);
	ASC.SetNumericAttributeBase(
		UCataclysmClassResourceAttributeSet::GetFervourLostToHealingAttribute(),
		1.0f);

	if (!TestEqual(TEXT("the pool is full before anything heals"),
				   ASC.GetNumericAttribute(Resource), 100.0f, 0.01f))
	{
		return false;
	}

	ASC.SetPoolActions({PoolAction(TEXT("block"), TEXT("health"), 20.0f)});
	ASC.NoteBlocked();

	if (!TestEqual(TEXT("the health arrived"), ASC.GetNumericAttribute(Health),
				   300.0f, 0.01f))
	{
		return false;
	}
	TestTrue(TEXT("and it cost Fervour, so it was healing"),
			 ASC.GetNumericAttribute(Resource) < 100.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmARefreshReplacesTheActions,
	"Cataclysm.Enchantments.AnEquipmentRefreshReplacesTheActionsWholesale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmARefreshReplacesTheActions::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// THE BREAK THIS IS FOR: deleting the hand-over inside
	// `UCataclysmEquipmentComponent::RefreshAttributes`. A character would then
	// keep firing rows from gear it has taken off, and nothing else here would
	// notice, because every other test hands the list over by itself.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent& ASC = *Wearer.AbilitySystem;

	ASC.SetPoolActions({PoolAction(TEXT("block"), TEXT("health"), 10.0f)});
	if (!TestEqual(TEXT("the character holds one action to begin with"),
				   ASC.GetPoolActions().Num(), 1))
	{
		return false;
	}

	// AND A REFRESH WEARING NOTHING LEAVES NONE. With nothing worn the real
	// tables hand back an empty list whatever rows they hold, which is exactly
	// the case that proves the list is written rather than added to.
	Wearer.Equipment->RefreshAttributes(&ASC);
	TestEqual(TEXT("and a refresh wearing nothing leaves none"),
			  ASC.GetPoolActions().Num(), 0);
	return true;
}

// ---------------------------------------------------------------------------
// THE THREE RULES THE FIVE CLOCKLESS EVENTS BROUGHT WITH THEM. Issue #1815.
//
// A row may be scoped to the tags of whatever caused the event, gated on a
// condition judged AT THE MOMENT it fires, and may take a fraction of the
// amount the event carried rather than of a pool.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmAScopedActionNeedsItsTag,
	"Cataclysm.Enchantments.AScopedActionNeedsItsTagAndAnUntaggedEventCannotGiveIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAScopedActionNeedsItsTag::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// THE BREAK THIS IS FOR: ignoring the row's required tags, which would let
	// "melee kills restore health" fire on a spell kill. Type.Melee is on 30 of
	// the 403 weapon skills and Type.Strike on 31, measured 2026-09-14.
	//
	// AND THE THIRD CASE IS THE ONE THAT IS EASY TO GET WRONG: an event that
	// carries NO tags must not satisfy a scoped row. A row scoped to melee must
	// not fire on an event that cannot say whether it was melee, so the absence
	// is a refusal rather than a pass.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent& ASC = *Wearer.AbilitySystem;
	GivePools(ASC, /*Health=*/100.0f, /*MaxHealth=*/500.0f);

	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();

	FCataclysmPoolAction Scoped =
		PoolAction(TEXT("kill"), TEXT("health"), 10.0f);
	Scoped.RequiredTags.AddTag(
		FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Melee")),
										 /*ErrorIfNotFound=*/false));
	if (!TestTrue(TEXT("the tag this build is asked about is a real one"),
				  Scoped.RequiredTags.Num() == 1))
	{
		return false;
	}
	ASC.SetPoolActions({Scoped});

	FGameplayTagContainer Melee;
	Melee.AddTag(FGameplayTag::RequestGameplayTag(
		FName(TEXT("Type.Melee")), /*ErrorIfNotFound=*/false));
	FGameplayTagContainer Spell;
	Spell.AddTag(FGameplayTag::RequestGameplayTag(
		FName(TEXT("Type.Spell")), /*ErrorIfNotFound=*/false));

	ASC.ActOnEvent(FName(TEXT("kill")), &Spell);
	TestEqual(TEXT("a kill by a spell does not fire a melee row"),
			  ASC.GetNumericAttribute(Health), 100.0f, 0.01f);

	ASC.ActOnEvent(FName(TEXT("kill")), nullptr);
	TestEqual(TEXT("and a kill that carries no tags at all does not either"),
			  ASC.GetNumericAttribute(Health), 100.0f, 0.01f);

	ASC.ActOnEvent(FName(TEXT("kill")), &Melee);
	TestEqual(TEXT("and a melee kill does"),
			  ASC.GetNumericAttribute(Health), 150.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmAConditionedActionIsJudgedWhenItFires,
	"Cataclysm.Enchantments.AConditionedActionIsJudgedAtTheMomentItFires",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAConditionedActionIsJudgedWhenItFires::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// THE BREAK THIS IS FOR: ignoring the row's condition, which would make
	// "killing an enemy while below 30% HP restores health" restore at any
	// health at all.
	//
	// BOTH WAYS ROUND, because a condition that never holds and one that always
	// holds look the same to a test that only checks the firing half.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent& ASC = *Wearer.AbilitySystem;
	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();

	FCataclysmPoolAction Gated =
		PoolAction(TEXT("kill"), TEXT("health"), 10.0f);
	Gated.Condition = ECataclysmStatCondition::HealthBelowPercent;
	Gated.ConditionValue = 30.0f;
	ASC.SetPoolActions({Gated});

	// FOUR HUNDRED OF FIVE HUNDRED is eighty per cent, well clear of thirty.
	GivePools(ASC, /*Health=*/400.0f, /*MaxHealth=*/500.0f);
	ASC.ActOnEvent(FName(TEXT("kill")));
	if (!TestEqual(TEXT("a kill at full health restores nothing"),
				   ASC.GetNumericAttribute(Health), 400.0f, 0.01f))
	{
		return false;
	}

	// AND A HUNDRED OF FIVE HUNDRED is twenty per cent, under it.
	GivePools(ASC, /*Health=*/100.0f, /*MaxHealth=*/500.0f);
	ASC.ActOnEvent(FName(TEXT("kill")));
	TestEqual(TEXT("and the same kill below thirty per cent restores a tenth"),
			  ASC.GetNumericAttribute(Health), 150.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmAFractionOfTheEventsAmount,
	"Cataclysm.Enchantments.AFractionOfTheEventsAmountIsNotAFractionOfAPool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAFractionOfTheEventsAmount::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// THE BREAK THIS IS FOR: reading a pool when the row asked for the amount
	// the event carried. "Skills that cost HP restore that amount as mana" is a
	// fraction of what the cost took, and the figures are chosen so that the
	// pool answers and the event answer cannot be confused: the amount is 40
	// and the mana pool is 200 of 1000.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent& ASC = *Wearer.AbilitySystem;
	const FGameplayAttribute Mana = UCataclysmVitalAttributeSet::GetManaAttribute();
	ASC.SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
	ASC.SetNumericAttributeBase(Mana, 200.0f);

	FCataclysmPoolAction OfAmount =
		PoolAction(TEXT("health_cost"), TEXT("mana"), 50.0f);
	OfAmount.Base = ECataclysmPoolActionBase::EventAmount;
	ASC.SetPoolActions({OfAmount});

	ASC.ActOnEvent(FName(TEXT("health_cost")), nullptr, /*EventAmount=*/40.0f);

	// HALF OF FORTY IS TWENTY. Half of the maximum would be 500 and half of what
	// is held would be 100, so neither pool reading can produce this number.
	TestEqual(TEXT("half of the amount the event carried"),
			  ASC.GetNumericAttribute(Mana), 220.0f, 0.01f);
	return true;
}

// ---------------------------------------------------------------------------
// AN AUTHORED ROW, READ OUT OF THE ASSET THE GAME LOADS. Issue #1815.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmAnAuthoredBlockRowRestoresTheHealthItStates,
	"Cataclysm.Enchantments.AnAuthoredBlockRowFromTheBuiltTableRestoresTheHealthItStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAnAuthoredBlockRowRestoresTheHealthItStates::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// EVERY OTHER TEST OF AN ACTION WRITES ITS OWN, so all of them pass with the
	// eight authored rows missing from `DT_EnchantmentEffects`, or read at a
	// figure their sentences do not state. This one wears "Blocking an attack
	// restores 3-6% of your maximum health", lets the equipment refresh read what
	// it grants out of the asset, and blocks by the call a blocked blow makes:
	// `UCataclysmVitalAttributeSet` calls `NoteBlocked`.
	//
	// THE BREAK THIS IS FOR: reading a row's range as its first number at both
	// ends, which restores 3% where the item states 6%. The one other test that
	// takes an action row through the accumulator writes 4 at both ends and
	// cannot tell. It also fails until `tools/generate_datatable_assets.py` has
	// rebuilt the asset from a CSV holding the row.
	const TCHAR* BlockRestoresHealth =
		TEXT("Positive_Blocking_an_attack_restores_3_6_of_your_maximu");

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent& ASC = *Wearer.AbilitySystem;
	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();

	// PAIRED WITH A DRAWBACK THAT HAS NO EFFECT ROW, so the benefit is the only
	// thing the helm does. An item built in code carries a roll of 1, which takes
	// the far end of the range: 6.
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BlockRestoresHealth, DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(&ASC);

	if (ASC.GetPoolActions().Num() != 1)
	{
		AddError(FString::Printf(
			TEXT("Wearing %s handed the character %d actions rather than one. "
				 "DT_EnchantmentEffects may be older than the row: run  python "
				 "tools/run_editor_python.py tools/generate_datatable_assets.py"),
			BlockRestoresHealth, ASC.GetPoolActions().Num()));
		return false;
	}

	// HURT, SO THE ROW'S FIGURE AND BASE BOTH SHOW. Written after the helm went on,
	// because the refresh recomputes the maximum from the gear. At a hundred of
	// five hundred, 6% of the maximum is 30; 6% of what is held would be 6, and
	// the first number of the range would give 15.
	GivePools(ASC, /*Health=*/100.0f, /*MaxHealth=*/500.0f);
	if (!TestEqual(TEXT("the maximum is where it was put"),
				   ASC.GetNumericAttribute(MaxHealth), 500.0f, 0.01f)
		|| !TestEqual(TEXT("and so is the health"),
					  ASC.GetNumericAttribute(Health), 100.0f, 0.01f))
	{
		return false;
	}

	ASC.NoteBlocked();
	TestEqual(TEXT("a block restores 6% of the maximum, which is 30"),
			  ASC.GetNumericAttribute(Health), 130.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmAnAuthoredKillRowRestoresOnlyBelowItsLine,
	"Cataclysm.Enchantments.AnAuthoredKillRowFromTheBuiltTableRestoresOnlyBelowItsHealthLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAnAuthoredKillRowRestoresOnlyBelowItsLine::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// THE FIRST AUTHORED ACTION ROW WITH A CONDITION, READ OUT OF THE ASSET THE
	// GAME LOADS. Issue #1815. "Killing an enemy while below 30% HP instantly
	// restores 15%-25% of your maximum HP" is a kill that restores health under
	// `health_below` 30, and the condition is judged at the moment the kill
	// fires rather than when something asks for a stat.
	//
	// THE BREAK THIS IS FOR: reading the row's condition value from anywhere but
	// its own column. The low kill is made at 20%, which lies between the row's
	// 30 and the first number of its range, 15, so a condition read from the
	// wrong column refuses it. The kill at 80% is there so that a condition
	// dropped altogether fails too.
	//
	// A REAL PLAYER CHARACTER, WEARING THE ROW THROUGH ITS OWN EQUIPMENT, AND REAL
	// KILLS. The kill event is raised by the pawn when it hears the death
	// announcement, so a bare ability system would never hear one, and each kill
	// is the wearer's own blow through `ApplyHit`, which credits the death to it.
	//
	// IT FAILS UNTIL `tools/generate_datatable_assets.py` HAS REBUILT THE ASSET
	// FROM A CSV HOLDING THE ROW.
	const TCHAR* KillBelowThirty =
		TEXT("Positive_Killing_an_enemy_while_below_30_HP_instantly_re");

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* ASC =
		PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("the announcements"), Events)
		|| !TestNotNull(TEXT("ability system component"), ASC))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a character"), Character))
	{
		return false;
	}
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	if (!TestNotNull(TEXT("the character's own equipment"), Equipment))
	{
		return false;
	}

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Equipment->Equip(
		Carrying(TEXT("Head_Helm"), KillBelowThirty, DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Equipment->RefreshAttributes(ASC);

	int32 KillActions = 0;
	for (const FCataclysmPoolAction& Action : ASC->GetPoolActions())
	{
		KillActions += Action.Event == FName(TEXT("kill")) ? 1 : 0;
	}
	if (KillActions != 1)
	{
		AddError(FString::Printf(
			TEXT("Wearing %s handed the character %d kill actions rather than "
				 "one. DT_EnchantmentEffects may be older than the row: run  "
				 "python tools/run_editor_python.py "
				 "tools/generate_datatable_assets.py"),
			KillBelowThirty, KillActions));
		return false;
	}

	// WHO KILLED, heard the way the pawn hears it, so that a restore that did not
	// happen cannot be a kill credited to somebody else.
	AActor* LastKiller = nullptr;
	const FDelegateHandle Heard = Events->OnDeath.AddLambda(
		[&LastKiller](const FCataclysmDeathNotice& Notice)
		{
			LastKiller = Notice.Killer;
		});
	ON_SCOPE_EXIT { Events->OnDeath.Remove(Heard); };

	// A CREATURE ON THE OTHER SIDE WITH ONE POINT OF HEALTH, killed by one of the
	// wearer's blows. The body is not read again afterwards: an enemy's own death
	// takes it out of the level.
	const auto KillOneAt = [&](const FVector& Where)
	{
		ACataclysmEnemyCharacter* Victim =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (!Victim)
		{
			return false;
		}
		Victim->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Victim->SetHealth(1.0f);
		LastKiller = nullptr;
		const uint32 DeathsBefore = Events->DeathsSent();
		UCataclysmSkillEffects::ApplyHit(Character, Victim, /*DamagePercent=*/100.0f);
		return Events->DeathsSent() == DeathsBefore + 1 && LastKiller == Character;
	};

	// WRITTEN AFTER THE HELM WENT ON, because the refresh recomputes both from
	// what is worn: the maximum, and an attack damage of nothing for a character
	// holding no weapon.
	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();
	ASC->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

	// A KILL AT EIGHTY PER CENT.
	GivePools(*ASC, /*Health=*/400.0f, /*MaxHealth=*/500.0f);
	if (!TestEqual(TEXT("health starts at four hundred of five hundred"),
				   ASC->GetNumericAttribute(Health), 400.0f, 0.01f)
		|| !TestTrue(TEXT("the first kill was announced as the wearer's"),
					 KillOneAt(FVector(200.0f, 0.0f, 0.0f))))
	{
		return false;
	}
	TestEqual(TEXT("a kill at eighty per cent restores nothing"),
			  ASC->GetNumericAttribute(Health), 400.0f, 0.01f);

	// AND A KILL AT TWENTY PER CENT, WHICH A QUARTER OF THE MAXIMUM ANSWERS. An
	// item built in code carries a roll of 1, which takes the far end of 15-25.
	GivePools(*ASC, /*Health=*/100.0f, /*MaxHealth=*/500.0f);
	if (!TestEqual(TEXT("health is now one hundred"),
				   ASC->GetNumericAttribute(Health), 100.0f, 0.01f)
		|| !TestTrue(TEXT("the second kill was announced as the wearer's"),
					 KillOneAt(FVector(0.0f, 200.0f, 0.0f))))
	{
		return false;
	}
	TestEqual(TEXT("a kill at twenty per cent restores a quarter of the maximum"),
			  ASC->GetNumericAttribute(Health), 225.0f, 0.01f);
	return true;
}

// ---------------------------------------------------------------------------
// AN AUTHORED REMOVAL, READ OUT OF THE ASSET THE GAME LOADS. Issue #1791.

namespace CataclysmEnchantmentEffectTest
{
	/**
	 * The one modifier an enchantment put on a stat, or null when there is not
	 * exactly one. Read from what the refresh recorded, so it is what the game
	 * built out of the asset rather than what a test built.
	 */
	const FCataclysmStatModifier* TheEnchantmentModifierOn(
		const UCataclysmAbilitySystemComponent& AbilitySystem, const TCHAR* Stat)
	{
		const FCataclysmStatInputs* Inputs =
			AbilitySystem.GetStatInputs(FName(Stat));
		if (!Inputs)
		{
			return nullptr;
		}

		// THE RECORDED ARRAY ITSELF AND NOT A COPY, because the pointer handed
		// back points into it.
		const FCataclysmStatModifier* Found = nullptr;
		int32 Count = 0;
		for (const FCataclysmStatModifier& Modifier : Inputs->Modifiers)
		{
			if (Modifier.Source == ECataclysmModifierSource::Enchantment)
			{
				Found = &Modifier;
				++Count;
			}
		}
		return Count == 1 ? Found : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmAnAuthoredRemovalRowTakesArmorToZero,
	"Cataclysm.Enchantments.AnAuthoredRemovalRowFromTheBuiltTableTakesArmorToZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAnAuthoredRemovalRowTakesArmorToZero::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// "You have no armor" IS `armor`, KIND `removed`, and the game multiplies the
	// finished armour by nothing. Issue #1791, and the project owner's mechanic
	// of 2026-09-16.
	//
	// ON A HELM, WHICH IS ARMOUR ITSELF. `Head_Helm` carries an armour implicit,
	// so the item carrying the sentence also grants the armour it takes away, and
	// the character's class line adds whatever armour it has on top.
	//
	// THE BREAK THIS IS FOR: the kind read as an increase, which is what
	// `EnchantmentModifierFor` does with a name it has no case for. The row
	// states 1, so that break is one per cent more armour rather than none.
	//
	// WHAT AN UNARMOURED CHARACTER TAKES IS MEASURED ON THIS ONE, with the same
	// blow ignoring all of its armour. So the class the console variable picks
	// cannot change the answer, and every other step of the blow is the same in
	// both readings.
	//
	// IT FAILS UNTIL `tools/generate_datatable_assets.py` HAS REBUILT THE ASSET
	// FROM A CSV HOLDING THE ROW.
	const TCHAR* NoArmor = TEXT("Negative_You_have_no_armor");

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent& ASC = *Wearer.AbilitySystem;
	const FGameplayAttribute Armor = UCataclysmCombatAttributeSet::GetArmorAttribute();

	// ONE BLOW, WITH EVASION AND BLOCK PINNED OFF, and the same blow ignoring
	// every point of armour.
	FCataclysmIncomingHit Blow;
	Blow.Damage = 400.0f;
	Blow.bIsMelee = true;
	FCataclysmIncomingHit IgnoringArmor = Blow;
	IgnoringArmor.ArmorPenetration = 100.0f;
	const auto Taken = [&ASC](const FCataclysmIncomingHit& Hit)
	{
		return UCataclysmDamageCalculation::Resolve(
			Hit, &ASC, /*Tier=*/1, /*EvasionRoll=*/100.0f, /*BlockRoll=*/100.0f)
			.DealtToHealth;
	};

	// THE HELM WITHOUT THE SENTENCE FIRST, WHICH IS THE CONTROL. Its armour
	// stands, so armour makes the blow smaller. Without this, a character whose
	// armour never reached the damage step at all would pass the last assertion.
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(&ASC);

	// LARGE ENOUGH THAT NO BLOW HERE IS CUT SHORT BY THE HEALTH LEFT. Written
	// after the refresh, which recomputes the maximum from what is worn.
	GivePools(ASC, /*Health=*/10'000.0f, /*MaxHealth=*/10'000.0f);

	const float Armoured = ASC.GetNumericAttribute(Armor);
	if (!TestTrue(FString::Printf(TEXT("the helm gives armour: %.2f"), Armoured),
				  Armoured > 0.0f))
	{
		return false;
	}
	TestTrue(FString::Printf(
				 TEXT("and armour makes a blow smaller: %.2f against %.2f"),
				 Taken(Blow), Taken(IgnoringArmor)),
			 Taken(Blow) < Taken(IgnoringArmor) - 1.0f);

	// THEN THE SAME HELM, CARRYING THE SENTENCE.
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, NoArmor),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(&ASC);
	GivePools(ASC, /*Health=*/10'000.0f, /*MaxHealth=*/10'000.0f);

	const FCataclysmStatModifier* FromRow = TheEnchantmentModifierOn(ASC, TEXT("armor"));
	if (!FromRow)
	{
		AddError(FString::Printf(
			TEXT("Wearing %s did not put exactly one enchantment modifier on "
				 "armour. DT_EnchantmentEffects may be older than the row: run  "
				 "python tools/run_editor_python.py "
				 "tools/generate_datatable_assets.py"),
			NoArmor));
		return false;
	}
	TestEqual(TEXT("the row arrived as a removal"), FromRow->Bucket,
			  ECataclysmStatBucket::Removed);

	TestEqual(TEXT("the armour attribute reads nothing"),
			  ASC.GetNumericAttribute(Armor), 0.0f, 0.0001f);
	TestEqual(TEXT("and so does the armour a blow asks the character for"),
			  ASC.StatForSkill(FName(TEXT("armor")), FGameplayTagContainer(),
							   /*Fallback=*/-1.0f),
			  0.0f, 0.0001f);
	TestEqual(TEXT("so a blow deals what it deals with all armour ignored"),
			  Taken(Blow), Taken(IgnoringArmor), 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmRemovingMaximumManaEmptiesTheManaHeld,
	"Cataclysm.Enchantments.AnAuthoredRowRemovingMaximumManaEmptiesTheManaAlreadyHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRemovingMaximumManaEmptiesTheManaHeld::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// "Your maximum mana is reduced to zero", PUT ON BY A CHARACTER ALREADY IN
	// PLAY. Issue #1791. The removal takes the maximum to nothing through the
	// pipeline like any other. The mana already held is a current value, and
	// nothing lowers a current pool when its maximum falls -- issue #1757 ruled
	// that deliberate -- so `UCataclysmPlayerClassStats::ApplyTo` empties it for
	// this removal in particular.
	//
	// THE REFRESH A HELMET SWAP MAKES, which leaves the pools where they are. A
	// refresh that fills them would empty the mana to its new maximum of nothing
	// anyway, and would pass without the write this test is for.
	//
	// THE CONTROL IS THE SAME REFRESH WITHOUT THE SENTENCE, which leaves the mana
	// where it was. Without it, a refresh that emptied mana for any reason would
	// pass the last assertion.
	//
	// IT FAILS UNTIL `tools/generate_datatable_assets.py` HAS REBUILT THE ASSET
	// FROM A CSV HOLDING THE ROW.
	const TCHAR* NoMaximumMana =
		TEXT("Negative_Your_maximum_mana_is_reduced_to_zero");

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent& ASC = *Wearer.AbilitySystem;
	const FGameplayAttribute Mana = UCataclysmVitalAttributeSet::GetManaAttribute();
	const FGameplayAttribute MaxMana =
		UCataclysmVitalAttributeSet::GetMaxManaAttribute();

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(&ASC, ECataclysmPoolFill::LeaveAsTheyAre);

	const float Maximum = ASC.GetNumericAttribute(MaxMana);
	if (!TestTrue(FString::Printf(TEXT("the class line gives a mana maximum: %.2f"),
								  Maximum),
				  Maximum > 1.0f))
	{
		return false;
	}
	ASC.SetNumericAttributeBase(Mana, Maximum / 2.0f);
	Wearer.Equipment->RefreshAttributes(&ASC, ECataclysmPoolFill::LeaveAsTheyAre);
	TestEqual(TEXT("a refresh without the sentence leaves the mana where it was"),
			  ASC.GetNumericAttribute(Mana), Maximum / 2.0f, 0.01f);

	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, NoMaximumMana),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(&ASC, ECataclysmPoolFill::LeaveAsTheyAre);

	const FCataclysmStatModifier* FromRow =
		TheEnchantmentModifierOn(ASC, TEXT("max_mana"));
	if (!FromRow)
	{
		AddError(FString::Printf(
			TEXT("Wearing %s did not put exactly one enchantment modifier on "
				 "maximum mana. DT_EnchantmentEffects may be older than the row: "
				 "run  python tools/run_editor_python.py "
				 "tools/generate_datatable_assets.py"),
			NoMaximumMana));
		return false;
	}
	TestEqual(TEXT("the row arrived as a removal"), FromRow->Bucket,
			  ECataclysmStatBucket::Removed);
	TestEqual(TEXT("the mana maximum is nothing"),
			  ASC.GetNumericAttribute(MaxMana), 0.0f, 0.0001f);
	TestEqual(TEXT("and the mana already held went with it"),
			  ASC.GetNumericAttribute(Mana), 0.0f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmNoMeleeBlowIsEvadedOrBlocked,
	"Cataclysm.Enchantments.AnAuthoredRowLetsNoMeleeBlowBeEvadedOrBlockedAndLeavesTheRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmNoMeleeBlowIsEvadedOrBlocked::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// "You cannot evade or block melee attacks" IS TWO REMOVALS, of evasion and
	// of block chance, each holding only against a blow `hit_is_melee_attack`
	// names. Ruled 2026-09-17 under the project owner's delegation: a melee
	// attack is a blow whose effect carries Type.Melee, the reading the written
	// "less damage from melee attacks" rows already use.
	//
	// GEAR SUPPLIES BOTH STATS, because a removal takes away what the pipeline
	// resolves: `Chest_Jerkin` carries an evasion implicit and `Weapon_Shield` a
	// block chance implicit. Each roll is pinned to 0, so any evasion or block
	// chance at all evades or blocks, and a blow that is neither is one the stat
	// did not reach.
	//
	// THE CONTROL COMES FIRST: the same gear under a helm without the row evades
	// and blocks a melee blow, so what stops it afterwards is the row. And with
	// the row worn a ranged blow is still evaded and still blocked -- the half a
	// removal that lost its condition would fail.
	//
	// IT FAILS UNTIL `tools/generate_datatable_assets.py` HAS REBUILT THE ASSET
	// FROM A CSV HOLDING THE ROWS.
	const TCHAR* NoMeleeEvasionOrBlock =
		TEXT("Negative_You_cannot_evade_or_block_melee_attacks");

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent& ASC = *Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	const TCHAR* const PlainPieces[] = {
		TEXT("Chest_Jerkin"), TEXT("Weapon_Shield"), TEXT("Head_Helm")};
	for (const TCHAR* Base : PlainPieces)
	{
		Wearer.Equipment->Equip(
			Carrying(Base, BenefitWithNoEffect, DrawbackWithNoEffect),
			Removed, AlsoRemoved, Slot);
	}
	Wearer.Equipment->RefreshAttributes(&ASC);
	GivePools(ASC, /*Health=*/10'000.0f, /*MaxHealth=*/10'000.0f);

	FCataclysmIncomingHit Melee;
	Melee.Damage = 400.0f;
	Melee.bIsMelee = true;
	FCataclysmIncomingHit Ranged;
	Ranged.Damage = 400.0f;
	Ranged.bIsRanged = true;

	// EVASION IS ROLLED BEFORE BLOCK AND AN EVADED BLOW STOPS THERE, so each
	// question pins the other roll out of reach.
	const auto Evades = [&ASC](const FCataclysmIncomingHit& Hit)
	{
		return UCataclysmDamageCalculation::Resolve(
			Hit, &ASC, /*Tier=*/1, /*EvasionRoll=*/0.0f, /*BlockRoll=*/100.0f)
			.bEvaded;
	};
	const auto Blocks = [&ASC](const FCataclysmIncomingHit& Hit)
	{
		return UCataclysmDamageCalculation::Resolve(
			Hit, &ASC, /*Tier=*/1, /*EvasionRoll=*/100.0f, /*BlockRoll=*/0.0f)
			.bBlocked;
	};

	if (!TestTrue(TEXT("the gear evades a melee blow under a plain helm"),
				  Evades(Melee))
		|| !TestTrue(TEXT("and blocks one"), Blocks(Melee)))
	{
		return false;
	}

	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, NoMeleeEvasionOrBlock),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(&ASC);
	GivePools(ASC, /*Health=*/10'000.0f, /*MaxHealth=*/10'000.0f);

	const FCataclysmStatModifier* OnEvasion =
		TheEnchantmentModifierOn(ASC, TEXT("evasion"));
	const FCataclysmStatModifier* OnBlock =
		TheEnchantmentModifierOn(ASC, TEXT("block_chance"));
	if (!OnEvasion || !OnBlock)
	{
		AddError(FString::Printf(
			TEXT("Wearing %s did not put exactly one enchantment modifier on "
				 "evasion and one on block chance. DT_EnchantmentEffects may be "
				 "older than the rows: run  python tools/run_editor_python.py "
				 "tools/generate_datatable_assets.py"),
			NoMeleeEvasionOrBlock));
		return false;
	}
	TestEqual(TEXT("evasion's row arrived as a removal"), OnEvasion->Bucket,
			  ECataclysmStatBucket::Removed);
	TestEqual(TEXT("against melee blows"), OnEvasion->Condition,
			  ECataclysmStatCondition::HitIsMeleeAttack);
	TestEqual(TEXT("block chance's row arrived as a removal"), OnBlock->Bucket,
			  ECataclysmStatBucket::Removed);
	TestEqual(TEXT("against melee blows too"), OnBlock->Condition,
			  ECataclysmStatCondition::HitIsMeleeAttack);

	TestFalse(TEXT("a melee blow is not evaded"), Evades(Melee));
	TestFalse(TEXT("nor blocked"), Blocks(Melee));
	TestTrue(TEXT("a ranged blow is still evaded"), Evades(Ranged));
	TestTrue(TEXT("and still blocked"), Blocks(Ranged));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmLowHealthCrowdControlImmunity,
	"Cataclysm.Enchantments.AnAuthoredRowLetsNoStunOrKnockdownLandOnALowHealthWearer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLowHealthCrowdControlImmunity::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	// "While below 30% HP you are immune to crowd control" IS
	// `crowd_control_resistance`, flat 100, under `health_below` 30. Ruled
	// 2026-09-17 under the project owner's delegation. At 100
	// `UCataclysmSkillEffects::AfterCrowdControlResistance` lets nothing land,
	// which is the one place this game lets a stat reach immunity -- the owner's
	// choice of 2026-09-05.
	//
	// THE KNOCKDOWN HALF IS ONLY TRUE SINCE A KNOCKDOWN READ THE STAT, issue
	// #1815 again. Until then this row would have left a low-health wearer on the
	// floor while saying it was immune, which is why the row waited for it.
	//
	// TWO WEARERS, BECAUSE A HOLD THAT LANDS OPENS A FIVE SECOND WINDOW that
	// refuses the next one. The low-health assertions come first and land
	// nothing, so no window opens on that wearer; the stun control is then the
	// same wearer at full health, and the knockdown control is a second wearer.
	//
	// IT FAILS UNTIL `tools/generate_datatable_assets.py` HAS REBUILT THE ASSET
	// FROM A CSV HOLDING THE ROW.
	const TCHAR* CrowdControlImmunity =
		TEXT("Positive_While_below_30_HP_you_are_immune_to_crowd_contr");

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE ATTACKER NEEDS AN ABILITY SYSTEM OF ITS OWN, because
	// `ApplyTagForDuration` reads one off the instigator to build the effect
	// context, and a hold from an actor without one lands on nobody.
	FWearer Attacker(World);
	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent& ASC = *Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), CrowdControlImmunity, DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(&ASC);

	// A FIFTH OF ITS HEALTH, WHICH IS UNDER THE ROW'S THIRTY PER CENT.
	GivePools(ASC, /*Health=*/200.0f, /*MaxHealth=*/1000.0f);

	const FCataclysmStatModifier* FromRow =
		TheEnchantmentModifierOn(ASC, TEXT("crowd_control_resistance"));
	if (!FromRow)
	{
		AddError(FString::Printf(
			TEXT("Wearing %s did not put exactly one enchantment modifier on "
				 "crowd control resistance. DT_EnchantmentEffects may be older "
				 "than the row: run  python tools/run_editor_python.py "
				 "tools/generate_datatable_assets.py"),
			CrowdControlImmunity));
		return false;
	}
	TestEqual(TEXT("the row arrived as a flat value"), FromRow->Bucket,
			  ECataclysmStatBucket::Flat);
	TestEqual(TEXT("of a hundred"), FromRow->Value, 100.0f, 0.001f);
	TestEqual(TEXT("under a health threshold"), FromRow->Condition,
			  ECataclysmStatCondition::HealthBelowPercent);
	TestEqual(TEXT("of thirty per cent"), FromRow->ConditionValue, 30.0f, 0.001f);

	TestFalse(TEXT("a designed stun does not land below 30% health"),
			  UCataclysmSkillEffects::ApplyStun(
				  Attacker.Actor, Wearer.Actor, /*DurationSeconds=*/1.5f,
				  /*DamageDealt=*/0.0f, /*bStunIsDesigned=*/true));
	TestFalse(TEXT("and the wearer is not stunned"),
			  UCataclysmSkillEffects::IsStunned(Wearer.Actor));

	TestFalse(TEXT("a designed knockdown does not land either"),
			  UCataclysmSkillEffects::ApplyKnockdown(
				  Attacker.Actor, Wearer.Actor, /*DurationSeconds=*/2.0f,
				  /*DamageDealt=*/0.0f, /*bKnockdownIsDesigned=*/true));
	TestFalse(TEXT("nor is the wearer knocked down"),
			  UCataclysmSkillEffects::IsKnockedDown(Wearer.Actor));

	// AND AT FULL HEALTH THE SAME STUN LANDS, which is what says the two refusals
	// above came from the row's condition rather than from the hold path being
	// broken here.
	GivePools(ASC, /*Health=*/1000.0f, /*MaxHealth=*/1000.0f);
	TestTrue(TEXT("the same stun lands at full health"),
			 UCataclysmSkillEffects::ApplyStun(
				 Attacker.Actor, Wearer.Actor, /*DurationSeconds=*/1.5f,
				 /*DamageDealt=*/0.0f, /*bStunIsDesigned=*/true));
	TestTrue(TEXT("and the wearer is stunned"),
			 UCataclysmSkillEffects::IsStunned(Wearer.Actor));

	// THE KNOCKDOWN CONTROL IS A SECOND WEARER, because the stun above opened the
	// window a stun and a knockdown share.
	FWearer Other(World);
	Other.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), CrowdControlImmunity, DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Other.Equipment->RefreshAttributes(Other.AbilitySystem);
	GivePools(*Other.AbilitySystem, /*Health=*/1000.0f, /*MaxHealth=*/1000.0f);

	TestTrue(TEXT("a knockdown lands on a wearer at full health"),
			 UCataclysmSkillEffects::ApplyKnockdown(
				 Attacker.Actor, Other.Actor, /*DurationSeconds=*/2.0f,
				 /*DamageDealt=*/0.0f, /*bKnockdownIsDesigned=*/true));
	TestTrue(TEXT("which knocks it down"),
			 UCataclysmSkillEffects::IsKnockedDown(Other.Actor));

	return true;
}

// ---------------------------------------------------------------------------
// The two cooldown rows of issue #1994, read from the shipped tables.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossCooldownRowTest,
	"Cataclysm.Enchantments.TheBossCooldownRowShortensACooldownOnlyAfterABossIsStruck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your cooldowns reset 50%-100% faster when fighting Boss enemies, for 4
 * seconds after you strike one", worn, reaches a cooldown only inside the
 * window. Issue #1994, which carries the row; the engine half is #2013.
 *
 * THE ROW IS READ, NOT WRITTEN BY HAND. A test that granted the modifier itself
 * would pass with no data row at all. This one equips an item carrying the
 * enchantment, so it fails until the row exists in the imported table.
 *
 * A WORN ITEM ROLLS THE TOP OF ITS RANGE BY DEFAULT, so the row gives 100: a
 * flat 100 divides a four second cooldown by two.
 */
bool FCataclysmBossCooldownRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"),
				 TEXT("Positive_Your_cooldowns_reset_50_100_faster_when_fighti"),
				 DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	TestEqual(TEXT("with no Boss struck, a four second cooldown is four"),
		UCataclysmGameplayAbility::CooldownAfterReduction(ASC, 4.0f), 4.0f, 0.001f);

	ASC->NoteStruckABoss();
	TestEqual(TEXT("and just after striking one it is two"),
		UCataclysmGameplayAbility::CooldownAfterReduction(ASC, 4.0f), 2.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMovementCooldownDrawbackTest,
	"Cataclysm.Enchantments.TheMovementCooldownDrawbackLengthensOnlyMovementSkills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Movement abilities have 50% increased cooldown", worn, makes a movement
 * skill's four second cooldown six and leaves an ultimate's at four. Issue
 * #1994.
 *
 * READ FROM THE SHIPPED ROW, for the reason the test above gives. The row
 * states one number, so the roll does not matter.
 *
 * BOTH TAGS ARE CHECKED TO BE REAL FIRST, or "the ultimate is not lengthened"
 * could pass on an empty container.
 */
bool FCataclysmMovementCooldownDrawbackTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FGameplayTagContainer Movement;
	Movement.AddTag(FGameplayTag::RequestGameplayTag(
		FName(TEXT("Slot.Movement")), /*ErrorIfNotFound=*/false));
	FGameplayTagContainer Ultimate;
	Ultimate.AddTag(FGameplayTag::RequestGameplayTag(
		FName(TEXT("Slot.Ultimate")), /*ErrorIfNotFound=*/false));
	if (!TestEqual(TEXT("Slot.Movement is a real tag in this build"),
				   Movement.Num(), 1)
		|| !TestEqual(TEXT("and so is Slot.Ultimate"), Ultimate.Num(), 1))
	{
		return false;
	}

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect,
				 TEXT("Negative_Movement_abilities_have_50_increased_cooldown")),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	TestEqual(TEXT("a movement skill's four second cooldown becomes six"),
		UCataclysmGameplayAbility::CooldownAfterReduction(ASC, 4.0f, Movement),
		6.0f, 0.001f);
	TestEqual(TEXT("and an ultimate's stays four"),
		UCataclysmGameplayAbility::CooldownAfterReduction(ASC, 4.0f, Ultimate),
		4.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBleedDrawbackReachesTheShieldTest,
	"Cataclysm.Enchantments.TheBleedDrawbackLetsABleedIntoItsWearersShield",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Energy shield can now be effected by bleed", from its row, worn. Issue #2014.
 *
 * THE ONE EXCEPTION TO THE ONE EXCEPTION. The project owner, 2026-09-18:
 * "every DoT except bleed reaches ES." A bleed passes an energy shield to
 * health; this drawback makes its wearer's shield take bleed too, by granting
 * `shield_absorbs_damage_over_time`, the flag Warded grants.
 *
 * READ FROM THE ROW AND WORN, NOT GRANTED BY HAND. A case that wrote the flag
 * itself would pass with no row at all, which is how three nodes on the passive
 * side came to grant nothing. The drawback is paired with a benefit that has
 * no effect row, so the flag is the only thing the item changes.
 *
 * BOTH HALVES, WITHOUT AND WITH, on the same wearer: a bleed passes the shield
 * before the item is worn and is absorbed after.
 */
bool FCataclysmBleedDrawbackReachesTheShieldTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using Vital = UCataclysmVitalAttributeSet;

	FTables Tables;
	if (!LoadAll(*this, Tables))
	{
		return false;
	}

	const TCHAR* BleedDrawback =
		TEXT("Negative_Energy_shield_can_now_be_effected_by_bleed");
	if (!TestNotNull(TEXT("the drawback is a row of EnchantmentsNegative.csv"),
			Tables.Negative->FindRow<FCataclysmEnchantmentRow>(
				FName(BleedDrawback), TEXT("BleedDrawback"),
				/*bWarnIfMissing=*/false)))
	{
		return false;
	}

	const FCataclysmEnchantmentEffectRow* Effect =
		Tables.Effects->FindRow<FCataclysmEnchantmentEffectRow>(
			FName(FString(BleedDrawback) + TEXT("#1")), TEXT("BleedDrawback"),
			/*bWarnIfMissing=*/false);
	if (!TestNotNull(TEXT("the drawback has an effect row"), Effect))
	{
		AddError(TEXT("The drawback grants nothing in play without a row in "
					  "the Enchantment Effects sheet of "
					  "docs/All_Things_Cataclysm.xlsx."));
		return false;
	}
	TestEqual(TEXT("which grants the flag that lets bleed into the shield"),
			  Effect->Stat,
			  FString(UCataclysmDamageCalculation::ShieldAbsorbsDamageOverTimeStat));
	TestEqual(TEXT("stated flat"), Effect->ValueKind, FString(TEXT("flat")));
	TestTrue(TEXT("and above zero, which is what the flag asks"),
			 Effect->ValueLow > 0.0f);

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	// WRITTEN AFTER EVERY REFRESH, because the refresh writes the class line's
	// pools back. A shield of 400 and far more health than a tick can take.
	const auto FillPools = [ASC]()
	{
		ASC->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 100000.0f);
		ASC->SetNumericAttributeBase(Vital::GetHealthAttribute(), 100000.0f);
		ASC->SetNumericAttributeBase(Vital::GetMaxEnergyShieldAttribute(), 400.0f);
		ASC->SetNumericAttributeBase(Vital::GetEnergyShieldAttribute(), 400.0f);
	};
	const auto BleedFor = [ASC](float Damage)
	{
		FCataclysmIncomingHit Tick;
		Tick.Damage = Damage;
		Tick.bIsDamageOverTime = true;
		Tick.bIsBleed = true;
		return UCataclysmDamageCalculation::Resolve(
			Tick, ASC, /*Tier=*/1, /*EvasionRoll=*/100.0f, /*BlockRoll=*/100.0f);
	};

	Wearer.Equipment->RefreshAttributes(ASC);
	FillPools();
	// A TICK FAR LARGER THAN THE SHIELD, because armour and damage reduction
	// act on a tick before the shield does and the helm below carries armour of
	// its own. Whatever they take, what is left is still more than 400, so the
	// shield's share is exactly its whole 400 when it applies at all.
	constexpr float Tick = 100000.0f;

	TestEqual(TEXT("wearing nothing, the shield absorbs none of a bleed"),
			  BleedFor(Tick).AbsorbedByShield, 0.0f, 0.001f);

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, BleedDrawback),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);
	FillPools();

	const FCataclysmDamageResult Worn = BleedFor(Tick);
	TestEqual(TEXT("wearing the drawback, the shield absorbs the whole of itself "
				   "from a bleed"),
			  Worn.AbsorbedByShield, 400.0f, 0.001f);
	TestTrue(TEXT("and the rest still reaches health"), Worn.DealtToHealth > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSupportSpeedRowTest,
	"Cataclysm.Enchantments.TheSupportAbilitySpeedRowRaisesSpeedOnlyAfterTheSupportSkill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Using your support ability grants you 10%-20% increased movement speed for 3
 * seconds", worn, raises the wearer's movement speed only after the support
 * skill is used. Issue #1815, one of the movement rows #1821 unblocked.
 *
 * THE ROW IS READ, NOT WRITTEN BY HAND: the item carrying the enchantment is
 * equipped, so this fails until the row exists in the imported table.
 *
 * A WORN ITEM ROLLS THE TOP OF ITS RANGE BY DEFAULT, so the row gives 20: the
 * speed after the support skill is 1.2 times the speed before it. The speed
 * before it is checked to be above nothing first, or 1.2 times nothing would
 * pass as nothing.
 */
bool FCataclysmSupportSpeedRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"),
				 TEXT("Positive_Using_your_support_ability_grants_you_10_20_in"),
				 DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	const FName Speed(TEXT("movement_speed"));
	const float Before = ASC->StatForSkill(Speed, FGameplayTagContainer(), 0.0f);
	if (!TestTrue(FString::Printf(TEXT("the wearer has a speed to raise: %.3f"),
								  Before),
				  Before > 0.0f))
	{
		return false;
	}

	ASC->NoteSupportSkillUsed();
	TestEqual(TEXT("just after the support skill, the speed is 1.2 times what it was"),
		ASC->StatForSkill(Speed, FGameplayTagContainer(), 0.0f), Before * 1.2f,
		0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFirstHitArmourRowTest,
	"Cataclysm.Enchantments.TheFirstHitArmourRowIgnoresArmourOnlyUntilThatEnemyIsStruck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your first hit against each enemy ignores all armor", worn, gives full armour
 * penetration against an enemy the wearer has not struck, and none once it has.
 * Issue #1815.
 *
 * THE ROW IS READ, NOT WRITTEN BY HAND: the item carrying the enchantment is
 * equipped, so this fails until the row exists in the imported table.
 *
 * THE TARGET IS A SECOND CHARACTER WITH ITS OWN ABILITY SYSTEM, because the
 * record of who has struck a character is kept on the character struck, and
 * the lookup asks it through the target it is handed.
 */
bool FCataclysmFirstHitArmourRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	FWearer Enemy(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"),
				 TEXT("Positive_Your_first_hit_against_each_enemy_ignores_all_ar"),
				 DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	const FName Penetration(TEXT("armor_penetration"));
	const auto Against = [&](AActor* Target)
	{
		return ASC->StatForSkill(Penetration, FGameplayTagContainer(), 0.0f,
								 /*SkillHealthCostPercent=*/-1.0f,
								 FCataclysmBlowContext(),
								 /*MetresMovedBeforeBlow=*/-1.0f,
								 /*TargetDistanceMetres=*/-1.0f,
								 /*bTargetIsStaggered=*/false, Target);
	};

	const float Unstruck = Against(Enemy.Actor);
	const float NoTarget = Against(nullptr);
	TestEqual(TEXT("with no target in hand the row grants nothing"), NoTarget, 0.0f, 0.001f);
	TestEqual(TEXT("against an enemy not yet struck, the row gives all 100"),
		Unstruck - NoTarget, 100.0f, 0.001f);

	Enemy.AbilitySystem->NoteStruckBy(ASC, /*bCritical=*/false);
	TestEqual(TEXT("and once that enemy is struck, nothing"),
		Against(Enemy.Actor), NoTarget, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDisabledRegenerationRowTest,
	"Cataclysm.Enchantments.TheDisabledRegenerationRowRemovesItOnlyInCombat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "HP regeneration is disabled during combat", worn, removes health
 * regeneration while the wearer is in combat and not otherwise. Issue #1815.
 *
 * THE ROW IS READ, NOT WRITTEN BY HAND: the item carrying the enchantment is
 * equipped, so this fails until the row exists in the imported table.
 */
bool FCataclysmDisabledRegenerationRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect,
				 TEXT("Negative_HP_regeneration_is_disabled_during_combat")),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	const FName Regen(TEXT("health_regen"));
	TestFalse(TEXT("out of combat, regeneration is not removed"),
		ASC->IsStatRemoved(Regen, FGameplayTagContainer()));

	ASC->NoteHitTaken();
	TestTrue(TEXT("a hit taken puts the wearer in combat, and it is removed"),
		ASC->IsStatRemoved(Regen, FGameplayTagContainer()));

	World->TimeSeconds += UCataclysmAbilitySystemComponent::CombatLapseSeconds + 0.5f;
	TestFalse(TEXT("and once combat lapses it is back"),
		ASC->IsStatRemoved(Regen, FGameplayTagContainer()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSecondsInCombatRowTest,
	"Cataclysm.Enchantments.TheDamageTakenPerSecondInCombatRowStopsAtTenStacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "You take 10%-20% increased damage for each second you have been in combat,
 * up to 10 stacks", worn, grows with the seconds of the current combat and
 * stops at ten. Issue #1815.
 *
 * A WORN ITEM ROLLS THE TOP OF ITS RANGE BY DEFAULT, so each second is 20%.
 * Five seconds are 100% more of the figure; twenty-five are capped at ten
 * steps, 200%, where an uncapped row would give 500%.
 *
 * THE COMBAT IS KEPT GOING by a hit every two seconds, inside the lapse.
 */
bool FCataclysmSecondsInCombatRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect,
				 TEXT("Negative_You_take_10_20_increased_damage_for_each_secon")),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	const FName Taken(TEXT("damage_taken"));
	const auto TakenNow = [&]()
	{
		return ASC->StatAppliedTo(Taken, FGameplayTagContainer(), 100.0f);
	};

	TestEqual(TEXT("out of combat, nothing is added"), TakenNow(), 100.0f, 0.01f);

	ASC->NoteHitTaken();
	World->TimeSeconds += 2.0f;
	ASC->NoteHitTaken();
	World->TimeSeconds += 2.0f;
	ASC->NoteHitTaken();
	World->TimeSeconds += 1.0f;
	TestEqual(TEXT("five seconds into a combat is five stacks, 100% more"),
		TakenNow(), 200.0f, 0.01f);

	for (int32 Beat = 0; Beat < 10; ++Beat)
	{
		World->TimeSeconds += 2.0f;
		ASC->NoteHitTaken();
	}
	TestEqual(TEXT("twenty-five seconds in stops at ten stacks, 200% more"),
		TakenNow(), 300.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSecondsOutOfCombatRowTest,
	"Cataclysm.Enchantments.TheDamagePerSecondOutOfCombatRowStopsAtTenStacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your damage is reduced by 3%-5% for every second you spend out of combat, up
 * to 10 stacks, less damage the longer you are out of combat", worn, lowers the
 * wearer's damage for each second since combat lapsed, down to ten stacks.
 * Issue #1815.
 *
 * A WORN ITEM ROLLS THE TOP OF ITS RANGE, so each second is 5% less. Two whole
 * seconds out of combat are 10% less; thirty are capped at ten steps, 50% less,
 * where an uncapped row would reach the pipeline's floor of 99% less.
 */
bool FCataclysmSecondsOutOfCombatRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect,
				 TEXT("Negative_Your_damage_is_reduced_by_3_5_for_every_second")),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	const FName Damage(TEXT("attack_damage"));
	const auto DamageNow = [&]()
	{
		return ASC->StatAppliedTo(Damage, FGameplayTagContainer(), 1000.0f);
	};

	ASC->NoteHitTaken();
	TestEqual(TEXT("in combat, nothing is taken away"), DamageNow(), 1000.0f, 0.01f);

	World->TimeSeconds += UCataclysmAbilitySystemComponent::CombatLapseSeconds + 2.5f;
	TestEqual(TEXT("two whole seconds out of combat are 10% less"),
		DamageNow(), 900.0f, 0.01f);

	World->TimeSeconds += 30.0f;
	TestEqual(TEXT("thirty more stop at ten stacks, 50% less"),
		DamageNow(), 500.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLowManaRowTest,
	"Cataclysm.Enchantments.TheLowManaRowRaisesDamageTakenOnlyBelow35PercentMana",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Take 10%-40% more damage when on low mana", worn, raises the damage the
 * wearer takes only while its mana is below 35% of its maximum. Issue #1815;
 * low mana is below 35%, ruled under the owner's delegation on 2026-09-23.
 *
 * THE BOUNDARY IS STRICT: exactly 35% is not low, the reading `mana_below`
 * takes. A worn item rolls the top of its range, so the row is 40% more.
 */
bool FCataclysmLowManaRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect,
				 TEXT("Negative_Take_10_40_more_damage_when_on_low_mana")),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	using Vital = UCataclysmVitalAttributeSet;
	ASC->SetNumericAttributeBase(Vital::GetMaxManaAttribute(), 1000.0f);
	const FName Taken(TEXT("damage_taken"));
	const auto TakenAt = [&](float Mana)
	{
		ASC->SetNumericAttributeBase(Vital::GetManaAttribute(), Mana);
		return ASC->StatAppliedTo(Taken, FGameplayTagContainer(), 100.0f);
	};

	TestEqual(TEXT("at full mana nothing is added"), TakenAt(1000.0f), 100.0f, 0.01f);
	TestEqual(TEXT("at exactly 35% mana nothing is added"), TakenAt(350.0f), 100.0f, 0.01f);
	TestEqual(TEXT("at 30% mana the wearer takes 40% more"), TakenAt(300.0f), 140.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStrikeAgainstADotRowTest,
	"Cataclysm.Enchantments.TheStrikeAgainstADotRowReachesOnlyAStrikeOnAnEnemyWithADot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Strike skills deal 20%-40% increased damage against enemies affected by a
 * DoT", worn, adds 40% to a Strike skill's damage against a bleeding enemy and
 * to nothing else. Issue #1815.
 *
 * TWO REFUSALS BESIDE THE ONE CASE THAT GRANTS: the same Strike against a clean
 * enemy, and a skill that is not a Strike against the bleeding one.
 */
bool FCataclysmStrikeAgainstADotRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	FWearer Bleeding(World);
	FWearer Clean(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;
	Bleeding.AbilitySystem->AddLooseGameplayTag(UCataclysmDebuffs::BleedTag());

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"),
				 TEXT("Positive_Strike_skills_deal_20_40_increased_damage_agai"),
				 DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	FGameplayTagContainer Strike;
	Strike.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Type.Strike")), /*ErrorIfNotFound=*/false));
	const auto Increases = [&](const FGameplayTagContainer& Tags, const AActor* Target)
	{
		return ASC->AttackDamageIncreasesForSkill(Tags, -1.0f, -1.0f, -1.0f, false, Target);
	};

	TestEqual(TEXT("a Strike on a bleeding enemy gains 40%"),
		Increases(Strike, Bleeding.Actor) - Increases(Strike, Clean.Actor), 0.40f, 0.001f);
	TestEqual(TEXT("a skill that is not a Strike gains nothing on the same enemy"),
		Increases(FGameplayTagContainer(), Bleeding.Actor)
			- Increases(FGameplayTagContainer(), Clean.Actor), 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCurrentManaRowTest,
	"Cataclysm.Enchantments.TheCurrentManaRowAddsAShareOfTheManaHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your skills deal 10%-30% of your current mana as more damage", worn, adds
 * 30% of the mana held to the wearer's damage as a flat amount. Issue #1815.
 *
 * THE ROW CARRIES 10 TO 30, THE SENTENCE'S OWN RANGE, and the scale reads it
 * as a percentage of the mana held: 500 mana is 150 more damage.
 */
bool FCataclysmCurrentManaRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"),
				 TEXT("Positive_Your_skills_deal_10_30_of_your_current_mana_as"),
				 DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	using Vital = UCataclysmVitalAttributeSet;
	ASC->SetNumericAttributeBase(Vital::GetMaxManaAttribute(), 1000.0f);
	const FName Damage(TEXT("attack_damage"));
	const auto DamageAt = [&](float Mana)
	{
		ASC->SetNumericAttributeBase(Vital::GetManaAttribute(), Mana);
		return ASC->StatAppliedTo(Damage, FGameplayTagContainer(), 1000.0f);
	};

	TestEqual(TEXT("with no mana nothing is added"), DamageAt(0.0f), 1000.0f, 0.01f);
	TestEqual(TEXT("with 500 mana, 30% of it is added"), DamageAt(500.0f), 1150.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionMaximumHealthRowTest,
	"Cataclysm.Enchantments.TheMinionMaximumHealthRowLowersItForEachMinion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Each active minion reduces your maximum HP by 3%-6%", worn, takes 6% of
 * increases off maximum health for each minion the wearer commands. Issue #1815.
 *
 * INCREASED, NOT MORE, ruled under the owner's delegation on 2026-09-23:
 * "reduces" is this project's word for the increased bucket. So two minions
 * add -12% to the wearer's other increases, which is exactly 120 less on a
 * figure of 1000 whatever those other increases are.
 *
 * AND THE ATTRIBUTE FOLLOWS, once the live refresh has run: that is what every
 * reader of maximum health sees.
 */
bool FCataclysmMinionMaximumHealthRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect,
				 TEXT("Negative_Each_active_minion_reduces_your_maximum_HP_by_3")),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	const FName Maximum(TEXT("max_health"));
	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const float Alone = ASC->StatAppliedTo(Maximum, FGameplayTagContainer(), 1000.0f);
	const float AttributeAlone = ASC->GetNumericAttribute(MaxHealth);

	for (const float Metres : {3.0f, 4.0f})
	{
		if (!TestNotNull(TEXT("an imp"), ACataclysmMinion::Spawn(
				Wearer.Actor, FVector(Metres * 100.0f, 0.0f, 0.0f), /*Lifetime=*/60.0f,
				/*bBurns=*/false, /*TypeName=*/TEXT("Imp"))))
		{
			return false;
		}
	}
	if (!TestEqual(TEXT("the wearer commands two minions"),
				   UCataclysmCommand::ThingsCommandedBy(Wearer.Actor).Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("two minions take 12% of increases off a figure of 1000"),
		Alone - ASC->StatAppliedTo(Maximum, FGameplayTagContainer(), 1000.0f), 120.0f, 0.01f);

	ASC->RefreshLiveMaximumHealth();
	TestTrue(FString::Printf(TEXT("and the maximum health attribute falls: %.2f against %.2f"),
							 ASC->GetNumericAttribute(MaxHealth), AttributeAlone),
		ASC->GetNumericAttribute(MaxHealth) < AttributeAlone - 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOwnStackRowBuildsTest,
	"Cataclysm.Enchantments.AnOwnStackRowBecomesAScaledModifierAndAStackGrant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A hand-made row scaled by `own_stacks`, on an enchantment carried by two worn
 * pieces. Issue #1833, engine-only: the seven real rows wait for the design
 * workbook.
 *
 * ON A DRAWBACK, each piece becomes a stat modifier scaled by the row's own
 * stacks and a grant on the row's event, and the two carry ONE key, so they
 * share one count. ON A BENEFIT, the same two pieces give one of each, because
 * a benefit counts once however many pieces carry it, at the higher of their
 * rolls, while a drawback counts for every piece: the rule in
 * `UCataclysmItemModifiers::AccumulateEnchantmentsInto`. This test first
 * expected two from a benefit, and failed.
 */
bool FCataclysmOwnStackRowBuildsTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UDataTable* Positive =
		LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	UDataTable* Negative =
		LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
	if (!TestNotNull(TEXT("the positive enchantments"), Positive)
		|| !TestNotNull(TEXT("the negative enchantments"), Negative))
	{
		return false;
	}

	// THE SAME ROW, WRITTEN ON WHICHEVER ENCHANTMENT A HALF NEEDS, and the same
	// two pieces, each carrying the benefit and the drawback.
	using FTotalsByStat = TMap<FName, TArray<FCataclysmStatModifier>>;
	const auto Build = [&](const TCHAR* Enchantment, FTotalsByStat& Totals,
						   TArray<FCataclysmPoolAction>& Actions)
	{
		UDataTable* Effects = EffectTableFrom(
			FString(TEXT("Name,Enchantment,Stat,ValueKind,ValueLow,ValueHigh,"
						 "RequiredTags,Condition,ConditionValue,Scale,ScaleStep,"
						 "Action,ActionEvent,FractionOf,ScaleMaxSteps,StackSeconds,ScaleOffset,EverySeconds,EveryNth,ScaleStepHigh,StackSecondsHigh,Condition2,ConditionValue2,ConditionValueHigh,TriggerCooldown,EventValue,Ailment,DamageShare\n"))
			+ FString::Printf(
				TEXT("%s#1,%s,armor,increased,10,10,,,0,own_stacks,1,,critical_strike,,5,5,0,0,0,0,0,,0,0,0,0,,0\n"),
				Enchantment, Enchantment));
		if (!TestNotNull(TEXT("an effect table holding one stack row"), Effects))
		{
			return false;
		}
		const TArray<FCataclysmItem> Worn = {
			Carrying(TEXT("Head_Helm"), ShieldBenefit, DrawbackWithNoEffect),
			Carrying(TEXT("Chest_Cuirass"), ShieldBenefit, DrawbackWithNoEffect)};
		UCataclysmItemModifiers::AccumulateEnchantmentsInto(
			Totals, Worn, Effects, Positive, Negative, &Actions);
		return true;
	};

	// EVERY MODIFIER AND GRANT THE ROW MADE, checked the same way in both halves.
	const auto CheckEach = [&](const TCHAR* Half, const TArray<FCataclysmStatModifier>& Armour,
							   const TArray<FCataclysmPoolAction>& Actions, FName Key)
	{
		for (const FCataclysmStatModifier& Per : Armour)
		{
			TestEqual(*FString::Printf(TEXT("%s: scaled by its own stacks"), Half),
				static_cast<int32>(Per.Scale),
				static_cast<int32>(ECataclysmStatScale::PerOwnStack));
			TestEqual(*FString::Printf(TEXT("%s: under the row's key"), Half),
				Per.StackKey, Key);
			TestEqual(*FString::Printf(TEXT("%s: capped at five"), Half), Per.ScaleMaxSteps, 5);
		}
		for (const FCataclysmPoolAction& Grant : Actions)
		{
			TestEqual(*FString::Printf(TEXT("%s: granted on its event"), Half),
				Grant.Event, FName(TEXT("critical_strike")));
			TestEqual(*FString::Printf(TEXT("%s: under the same key, so the pieces share one count"), Half),
				Grant.StackKey, Key);
			TestEqual(*FString::Printf(TEXT("%s: lasting its Stack Seconds"), Half),
				Grant.StackSeconds, 5.0f, 0.001f);
			TestEqual(*FString::Printf(TEXT("%s: up to its cap"), Half), Grant.StackCap, 5);
			TestTrue(*FString::Printf(TEXT("%s: and moving no pool"), Half), Grant.Pool.IsNone());
		}
	};

	// A DRAWBACK ON TWO PIECES: one modifier and one grant for each piece.
	{
		FTotalsByStat Totals;
		TArray<FCataclysmPoolAction> Actions;
		if (!Build(DrawbackWithNoEffect, Totals, Actions))
		{
			return false;
		}
		const TArray<FCataclysmStatModifier>* Armour = Totals.Find(FName(TEXT("armor")));
		if (!TestNotNull(TEXT("a drawback: it became armour modifiers"), Armour)
			|| !TestEqual(TEXT("a drawback on two pieces: a modifier for each"), Armour->Num(), 2)
			|| !TestEqual(TEXT("a drawback on two pieces: a stack grant for each"), Actions.Num(), 2))
		{
			return false;
		}
		CheckEach(TEXT("a drawback"), *Armour, Actions,
			FName(*FString::Printf(TEXT("%s:armor"), DrawbackWithNoEffect)));
	}

	// A BENEFIT ON THE SAME TWO PIECES: one of each, however many carry it.
	{
		FTotalsByStat Totals;
		TArray<FCataclysmPoolAction> Actions;
		if (!Build(ShieldBenefit, Totals, Actions))
		{
			return false;
		}
		const TArray<FCataclysmStatModifier>* Armour = Totals.Find(FName(TEXT("armor")));
		if (!TestNotNull(TEXT("a benefit: it became an armour modifier"), Armour)
			|| !TestEqual(TEXT("a benefit on two pieces: one modifier"), Armour->Num(), 1)
			|| !TestEqual(TEXT("a benefit on two pieces: one stack grant"), Actions.Num(), 1))
		{
			return false;
		}
		CheckEach(TEXT("a benefit"), *Armour, Actions,
			FName(*FString::Printf(TEXT("%s:armor"), ShieldBenefit)));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStackSecondsRollTest,
	"Cataclysm.Enchantments.AStackTimeStatedAsARangeRollsWithTheItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A row whose stacks last "0.5-1 second" gives a worn item a time inside that
 * range, picked by the item's roll. Issue #1833, ruled 2026-09-30 under the
 * owner's delegation, following Scale Step High: "Critical strikes trigger a
 * 0.5-1 second global cooldown on all your skills".
 *
 * THREE ROLLS, because two ends alone would pass a helper that ignored the
 * roll's middle: the lowest roll gets 0.5, the highest 1, and the middle a
 * time strictly between. AND A ROW STATING ONE TIME keeps it at every roll, so
 * the new column changes nothing for the rows written before it.
 */
bool FCataclysmStackSecondsRollTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UDataTable* Positive =
		LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	UDataTable* Negative =
		LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
	if (!TestNotNull(TEXT("the positive enchantments"), Positive)
		|| !TestNotNull(TEXT("the negative enchantments"), Negative))
	{
		return false;
	}

	// THE TIME ONE DRAWBACK PIECE AT THIS ROLL GIVES ITS GRANT, or -1 when the
	// row made no grant.
	const auto TimeAt = [&](float Roll, float StackSecondsHigh)
	{
		UDataTable* Effects = EffectTableFrom(
			FString(TEXT("Name,Enchantment,Stat,ValueKind,ValueLow,ValueHigh,"
						 "RequiredTags,Condition,ConditionValue,Scale,ScaleStep,"
						 "Action,ActionEvent,FractionOf,ScaleMaxSteps,StackSeconds,ScaleOffset,EverySeconds,EveryNth,ScaleStepHigh,StackSecondsHigh,Condition2,ConditionValue2,ConditionValueHigh,TriggerCooldown,EventValue,Ailment,DamageShare\n"))
			+ FString::Printf(
				TEXT("%s#1,%s,skill_locked,flat,1,1,,,0,own_stacks,1,,critical_strike,,1,0.5,0,0,0,0,%g,,0,0,0,0,,0\n"),
				DrawbackWithNoEffect, DrawbackWithNoEffect, StackSecondsHigh));
		if (!Effects)
		{
			return -1.0f;
		}
		FCataclysmItem Piece = Carrying(TEXT("Head_Helm"), ShieldBenefit, DrawbackWithNoEffect);
		Piece.Enchantments[0].NegativeRoll = Roll;
		TMap<FName, TArray<FCataclysmStatModifier>> Totals;
		TArray<FCataclysmPoolAction> Actions;
		UCataclysmItemModifiers::AccumulateEnchantmentsInto(
			Totals, {Piece}, Effects, Positive, Negative, &Actions);
		return Actions.Num() == 1 ? Actions[0].StackSeconds : -1.0f;
	};

	TestEqual(TEXT("0.5-1 second, the lowest roll: 0.5"), TimeAt(0.0f, 1.0f), 0.5f, 0.001f);
	TestEqual(TEXT("0.5-1 second, the highest roll: 1"), TimeAt(1.0f, 1.0f), 1.0f, 0.001f);
	const float Middle = TimeAt(0.5f, 1.0f);
	TestTrue(FString::Printf(TEXT("0.5-1 second, the middle roll: strictly between, "
								  "and it was %.3f"), Middle),
			 Middle > 0.5f + 0.001f && Middle < 1.0f - 0.001f);
	TestEqual(TEXT("one stated time, the highest roll: still 0.5"), TimeAt(1.0f, 0.0f), 0.5f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraRowTest,
	"Cataclysm.Enchantments.TheAuraRowRaisesDamageTakenWhileAnAuraRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Take 5%-15% more damage per active aura", worn, raises the damage the wearer
 * takes by 15% while an aura skill of its own is running. Issue #1686. A worn
 * item rolls the top of its range.
 */
bool FCataclysmAuraRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect,
				 TEXT("Negative_Take_5_15_more_damage_per_active_aura")),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	const FName Taken(TEXT("damage_taken"));
	TestEqual(TEXT("with no aura running nothing is added"),
		ASC->StatAppliedTo(Taken, FGameplayTagContainer(), 100.0f), 100.0f, 0.01f);

	// A REAL AURA, GRANTED AND SWITCHED ON, and free so the wearer's mana is not
	// what this test measures.
	const FGameplayAbilitySpecHandle Handle = ASC->GiveAbilityInSlot(
		UCataclysmAuraSkill::StaticClass(), ECataclysmAbilitySlot::Aura,
		/*Level=*/100, Wearer.Actor);
	FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
	UCataclysmAuraSkill* Ring = Spec ? Cast<UCataclysmAuraSkill>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("the aura is granted"), Ring))
	{
		return false;
	}
	Ring->ManaCostOverride = 0.0f;
	if (!TestTrue(TEXT("and it runs"), ASC->TryActivateAbility(Handle)))
	{
		return false;
	}

	TestEqual(TEXT("one aura running: the wearer takes 15% more"),
		ASC->StatAppliedTo(Taken, FGameplayTagContainer(), 100.0f), 115.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPointBlankRowTest,
	"Cataclysm.Enchantments.ThePointBlankRowReachesOnlyAPointBlankAttackOnOneEnemy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Point blank AOE skills deal 15%-25% less damage to a single target", worn,
 * takes 25% off a point blank attack that struck one enemy. Issue #1686.
 *
 * TWO REFUSALS BESIDE THE ONE CASE THAT TAKES: the same attack on two enemies,
 * and an attack that is not point blank on one. It drives the attack damage
 * half, as `FCataclysmSpellsMovingDrawbackTest` does, and checks the spell
 * damage half arrived; `SpellDamageIsToldHowManyEnemiesTheAttackStruck`
 * drives that half's count.
 */
bool FCataclysmPointBlankRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	const FGameplayTagContainer PointBlank = SkillTagged(TEXT("Type.AOE.PointBlank"));
	const FGameplayTagContainer Melee = SkillTagged(TEXT("Type.Melee"));
	if (!TestFalse(TEXT("Type.AOE.PointBlank is in the vocabulary"), PointBlank.IsEmpty())
		|| !TestFalse(TEXT("and so is Type.Melee"), Melee.IsEmpty()))
	{
		return false;
	}

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect,
				 TEXT("Negative_Point_blank_AOE_skills_deal_15_25_less_damage")),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	if (!TestTrue(TEXT("the worn drawback put a modifier on attack damage"),
			CarriesAModifierOn(ASC, UCataclysmItemModifiers::AttackDamageStat))
		|| !TestTrue(TEXT("and one on spell damage"),
			CarriesAModifierOn(ASC, TEXT("spell_damage"))))
	{
		return false;
	}

	const auto More = [&](const FGameplayTagContainer& Tags, int32 Enemies)
	{
		return ASC->AttackDamageMoreForSkill(Tags, -1.0f, -1.0f, -1.0f, false, nullptr, Enemies);
	};

	TestEqual(TEXT("a point blank attack on one enemy deals 25% less"),
		More(PointBlank, 1), 0.75f, 0.001f);
	TestEqual(TEXT("on two enemies it deals the full amount"),
		More(PointBlank, 2), 1.0f, 0.001f);
	TestEqual(TEXT("an attack that is not point blank, on one enemy, is untouched"),
		More(Melee, 1), 1.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCrowdControlledAttackerRowTest,
	"Cataclysm.Enchantments.TheCrowdControlRowRaisesOnlyAHitFromACrowdControlledAttacker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "You take 15%-25% more damage from enemies that are currently CC'd", worn,
 * raises a hit from a crowd controlled attacker by 25% and nothing else. Issue
 * #1686. Measured as a ratio of two lookups, so the base damage taken the
 * wearer recorded does not matter.
 */
bool FCataclysmCrowdControlledAttackerRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect,
				 TEXT("Negative_You_take_15_25_more_damage_from_enemies_that_a")),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	const FName Taken(TEXT("damage_taken"));
	FCataclysmBlowContext Free;
	FCataclysmBlowContext Held;
	Held.bOpponentIsCrowdControlled = true;
	FCataclysmBlowContext Staggered;
	Staggered.bOpponentIsStaggered = true;

	const auto TakenFrom = [&](const FCataclysmBlowContext& Blow)
	{
		return ASC->StatForSkill(Taken, FGameplayTagContainer(), 100.0f, -1.0f, Blow);
	};

	const float Plain = TakenFrom(Free);
	if (!TestTrue(TEXT("a hit from a free attacker is priced at something"), Plain > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("a crowd controlled attacker's hit is 25% more"),
		TakenFrom(Held) / Plain, 1.25f, 0.001f);
	TestEqual(TEXT("a staggered attacker's is not, for a stagger is not crowd control"),
		TakenFrom(Staggered) / Plain, 1.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClassPointRowsTest,
	"Cataclysm.Enchantments.TheClassPointRowsCountOnlyThePointsAboveTheirThresholds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The three class point enchantments, worn by a player, at two allocations.
 * Issue #1686. A worn item rolls the top of each range.
 *
 *   "Your skills deal 1.5%-2.5% less damage for every 10 class points spent
 *    above 100": 150 points are five steps, 12.5% less.
 *   "Each class point spent above 100 grants 0.5%-1% increased damage": 150
 *    points are 50 steps, 50% more increases than at 100.
 *   "Your maximum HP is reduced by 1.5%-2.5% for every 10 class points above
 *    50": 100 points are five steps, 12.5% of increases off a figure of 1000
 *    against 50 points.
 *
 * THE POINTS SIT IN A NODE IN NO TREE, so they grant nothing of their own.
 */
bool FCataclysmClassPointRowsTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* ASC =
		PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ability system component"), ASC))
	{
		return false;
	}
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a character"), Character))
	{
		return false;
	}
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	if (!TestNotNull(TEXT("the character's own equipment"), Equipment))
	{
		return false;
	}

	const auto Spend = [&](int32 Points)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(TEXT("Test_node_in_no_tree")), Points);
		PlayerState->SetPassiveAllocation(Allocation, {});
		Equipment->RefreshAttributes(ASC);
	};

	const auto Wear = [&](const TCHAR* Benefit, const TCHAR* Drawback)
	{
		Equipment->UnequipEverything();
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
		Equipment->Equip(Carrying(TEXT("Head_Helm"), Benefit, Drawback),
						 Removed, AlsoRemoved, Slot);
		Equipment->RefreshAttributes(ASC);
	};

	// THE DAMAGE DRAWBACK.
	Wear(BenefitWithNoEffect, TEXT("Negative_Your_skills_deal_1_5_2_5_less_damage_for_every"));
	if (!TestTrue(TEXT("it put a modifier on spell damage as well"),
			CarriesAModifierOn(ASC, TEXT("spell_damage"))))
	{
		return false;
	}
	Spend(100);
	TestEqual(TEXT("at 100 points the drawback takes nothing"),
		ASC->AttackDamageMoreForSkill(FGameplayTagContainer()), 1.0f, 0.001f);
	Spend(150);
	TestEqual(TEXT("at 150 points it takes 12.5%"),
		ASC->AttackDamageMoreForSkill(FGameplayTagContainer()), 0.875f, 0.001f);

	// THE BENEFIT.
	Wear(TEXT("Positive_Each_class_point_spent_above_100_grants_0_5_1"), DrawbackWithNoEffect);
	const auto Increases = [&]()
	{
		return ASC->AttackDamageIncreasesForSkill(FGameplayTagContainer(),
			-1.0f, -1.0f, -1.0f, false, nullptr);
	};
	Spend(100);
	const float AtHundred = Increases();
	Spend(150);
	TestEqual(TEXT("50 points above 100 add 50% to the increases"),
		Increases() - AtHundred, 0.50f, 0.001f);

	// THE HEALTH DRAWBACK.
	Wear(BenefitWithNoEffect, TEXT("Negative_Your_maximum_HP_is_reduced_by_1_5_2_5_for_ever"));
	const FName Maximum(TEXT("max_health"));
	Spend(50);
	const float AtFifty = ASC->StatAppliedTo(Maximum, FGameplayTagContainer(), 1000.0f);
	Spend(100);
	TestEqual(TEXT("50 points above 50 take 12.5% of increases off a figure of 1000"),
		AtFifty - ASC->StatAppliedTo(Maximum, FGameplayTagContainer(), 1000.0f),
		125.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNonCriticalRowTest,
	"Cataclysm.Enchantments.TheNonCriticalRowLeavesANonCriticalHit65PercentOfItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Non-critical strikes deal 20%-35% less damage", worn, leaves the wearer's
 * non-critical share at 65: the base of 100 the stat fold supplies, and 35%
 * less at the top of the range. Issue #1686.
 *
 * AND WITHOUT THE ROW THE SHARE IS 100, which is the line that fails if the
 * base were ever lost: a stat line with no base would read 0 and every
 * non-critical hit the wearer landed would deal nothing.
 */
bool FCataclysmNonCriticalRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;
	const FName Share(UCataclysmDamageCalculation::NonCriticalDamageStat);
	const auto Read = [&]()
	{
		return ASC->StatForSkill(Share, FGameplayTagContainer(),
								 UCataclysmDamageCalculation::NormalNonCriticalDamage);
	};

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect,
				 TEXT("Negative_Non_critical_strikes_deal_20_35_less_damage")),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	TestEqual(TEXT("worn, a non-critical hit keeps 65% of itself"), Read(), 65.0f, 0.01f);

	Wearer.Equipment->UnequipEverything();
	Wearer.Equipment->RefreshAttributes(ASC);
	TestEqual(TEXT("and with nothing worn, all of itself"), Read(), 100.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileLaterHitRowTest,
	"Cataclysm.Enchantments.TheProjectileLaterHitRowLeavesALaterContact65PercentOfItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Projectiles deal 20%-35% less damage on each subsequent hit after the first",
 * worn, leaves a projectile's later contacts 65% of themselves: the base of 100
 * the stat fold supplies, and 35% less at the top of the range. Issue #1686.
 *
 * SCOPED TO `Type.Projectile`, so a skill that is not one keeps 100; and with
 * nothing worn the share is 100, the line that fails if the base were lost.
 */
bool FCataclysmProjectileLaterHitRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;
	const FGameplayTagContainer Projectile = SkillTagged(TEXT("Type.Projectile"));
	if (!TestFalse(TEXT("Type.Projectile is in the vocabulary"), Projectile.IsEmpty()))
	{
		return false;
	}
	const FName Share(UCataclysmDamageCalculation::ProjectileLaterHitDamageStat);
	const auto Read = [&](const FGameplayTagContainer& Tags)
	{
		return ASC->StatForSkill(Share, Tags,
								 UCataclysmDamageCalculation::NormalProjectileLaterHitDamage);
	};

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect,
				 TEXT("Negative_Projectiles_deal_20_35_less_damage_on_each_sub")),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	TestEqual(TEXT("worn, a projectile's later contact keeps 65%"), Read(Projectile), 65.0f, 0.01f);
	TestEqual(TEXT("a skill that is not a projectile keeps all of itself"),
		Read(FGameplayTagContainer()), 100.0f, 0.01f);

	Wearer.Equipment->UnequipEverything();
	Wearer.Equipment->RefreshAttributes(ASC);
	TestEqual(TEXT("and with nothing worn, all of itself"), Read(Projectile), 100.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmZoneFirstSweepRowTest,
	"Cataclysm.Enchantments.TheZoneFirstSweepRowLeavesAFirstSweep65PercentOfATick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Persistent AOE zones deal 20%-35% less damage on initial placement", worn,
 * leaves the wearer's first-sweep share at 65: the base of 100 the stat fold
 * supplies, and 35% less at the top of the range. Issue #1686. With nothing
 * worn the share is 100, the line that fails if the base were lost.
 */
bool FCataclysmZoneFirstSweepRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;
	const FName Share(UCataclysmDamageCalculation::ZoneFirstSweepDamageStat);
	const auto Read = [&]()
	{
		return ASC->StatForSkill(Share, FGameplayTagContainer(),
								 UCataclysmDamageCalculation::NormalZoneFirstSweepDamage);
	};

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect,
				 TEXT("Negative_Persistent_AOE_zones_deal_20_35_less_damage_on")),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	TestEqual(TEXT("worn, a zone's first sweep deals 65% of a tick"), Read(), 65.0f, 0.01f);

	Wearer.Equipment->UnequipEverything();
	Wearer.Equipment->RefreshAttributes(ASC);
	TestEqual(TEXT("and with nothing worn, all of one"), Read(), 100.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMeleeWhileMovingRowTest,
	"Cataclysm.Enchantments.TheMeleeWhileMovingRowRaisesOnlyAMeleeHitWhileMoving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "You take 15%-25% more damage from melee attacks while moving", worn, raises
 * a melee hit on a moving wearer by 25% and nothing else. Issue #1686, ruled on
 * #1697. Both halves are proved: a melee hit on the wearer STANDING and a
 * ranged hit on it MOVING each leave the row inactive. Measured as ratios of
 * two lookups, so the base the wearer recorded does not matter.
 */
bool FCataclysmMeleeWhileMovingRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), BenefitWithNoEffect,
				 TEXT("Negative_You_take_15_25_more_damage_from_melee_attacks")),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(ASC);

	const FName Taken(TEXT("damage_taken"));
	FCataclysmBlowContext Melee;
	Melee.bIsMelee = true;
	FCataclysmBlowContext Ranged;
	Ranged.bIsRanged = true;
	const auto TakenFrom = [&](const FCataclysmBlowContext& Blow)
	{
		return ASC->StatForSkill(Taken, FGameplayTagContainer(), 100.0f, -1.0f, Blow);
	};

	// STANDING, first: standing still has to be started, and then held.
	ASC->NoteDidNotMove();
	CataclysmTestWorld::RunClock(World, 3.0f);
	const float MeleeStanding = TakenFrom(Melee);
	const float RangedStanding = TakenFrom(Ranged);
	if (!TestTrue(TEXT("hits on a standing wearer are priced at something"),
				  MeleeStanding > 0.0f && RangedStanding > 0.0f))
	{
		return false;
	}

	// MOVING: one step taken.
	ASC->NoteMovedMetres(1.0f);
	TestEqual(TEXT("a melee hit while moving is 25% more than while standing"),
		TakenFrom(Melee) / MeleeStanding, 1.25f, 0.001f);
	TestEqual(TEXT("a ranged hit while moving is no more than while standing"),
		TakenFrom(Ranged) / RangedStanding, 1.0f, 0.001f);
	TestEqual(TEXT("and standing, a melee hit is no more than a ranged one"),
		MeleeStanding / RangedStanding, 1.0f, 0.001f);

	return true;
}

namespace CataclysmOwnStackRowTest
{
	/** One of the seven own-stack enchantments, at the top of its range. */
	struct FCase
	{
		const TCHAR* Enchantment = nullptr;
		bool bBenefit = true;
		const TCHAR* Event = nullptr;
		TArray<FName> Stats;
		float PerStack = 0.0f;
		int32 Cap = 0;
		float Seconds = 0.0f;
	};

	/**
	 * Wear the row, fire its event, and read each stat it names as a share of
	 * the same stat with no stacks: after two events, after enough to pass its
	 * cap, just inside its window and just after it. Issue #1833.
	 *
	 * A SHARE OF THE STAT WITH NO STACKS, so a base, a flat addition or a more
	 * multiplier cancels out. AN INCREASE DOES NOT: it sums with the row's. A
	 * real wearer's attributes put one on some lines -- agility on
	 * `movement_speed`, constitution on `armor`, from game/Data/Attributes.csv --
	 * so every other increase on the line is read from it, checked to be
	 * unscaled and unconditioned so it holds still, and the share expected is
	 * (1 + (other + stacks x value) / 100) / (1 + other / 100). The first
	 * version assumed the bucket held the row alone, and the stale-asset run
	 * of 2026-09-24 showed that armor and movement speed do not.
	 */
	void Check(FAutomationTestBase& Test, const FCase& Case)
	{
		using namespace CataclysmEnchantmentEffectTest;

		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };

		FWearer Wearer(World);
		UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
		Wearer.Equipment->Equip(
			Case.bBenefit
				? Carrying(TEXT("Head_Helm"), Case.Enchantment, DrawbackWithNoEffect)
				: Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, Case.Enchantment),
			Removed, AlsoRemoved, Slot);
		Wearer.Equipment->RefreshAttributes(ASC);

		// A STAND-IN FOR ATTRIBUTE POINTS, because this wearer cannot have
		// any. `RefreshAttributes` reads spent points from the pawn's player
		// state, and FWearer is a bare actor, so agility and constitution are
		// nought and the increases they put on `movement_speed` and `armor`
		// are worth nought -- measured on 2026-09-24. One increase of 20 on
		// each line, from the Attribute source, makes the row's increase sum
		// with another, as it does on a real wearer.
		//
		// ONLY THE CASE'S OWN LINES ARE WRITTEN BACK, and they are all this
		// test reads: `SetStatInputs` replaces the whole map.
		constexpr float StandIn = 20.0f;
		{
			TMap<FName, FCataclysmStatInputs> Lines;
			for (const FName& Stat : Case.Stats)
			{
				const FCataclysmStatInputs* Line = ASC->GetStatInputs(Stat);
				if (!Test.TestNotNull(FString::Printf(
						TEXT("'%s' has a line to add the stand-in to"), *Stat.ToString()),
						Line))
				{
					return;
				}
				FCataclysmStatModifier FromAttributes;
				FromAttributes.Bucket = ECataclysmStatBucket::Increased;
				FromAttributes.Source = ECataclysmModifierSource::Attribute;
				FromAttributes.Value = StandIn;
				FromAttributes.Scale = ECataclysmStatScale::Fixed;
				FromAttributes.Condition = ECataclysmStatCondition::Always;
				Test.TestEqual(FString::Printf(
					TEXT("the stand-in on '%s' passes the modifier validator"),
					*Stat.ToString()),
					UCataclysmStatPipeline::ValidateModifier(FromAttributes), FString());

				FCataclysmStatInputs Copy = *Line;
				Copy.Modifiers.Add(FromAttributes);
				Lines.Add(Stat, MoveTemp(Copy));
			}
			ASC->SetStatInputs(MoveTemp(Lines));
		}

		TMap<FName, float> Plain;
		TMap<FName, float> Other;
		for (const FName& Stat : Case.Stats)
		{
			const FCataclysmStatInputs* Line = ASC->GetStatInputs(Stat);
			int32 Stacked = 0;
			int32 Moving = 0;
			float OtherIncreases = 0.0f;
			for (const FCataclysmStatModifier& Modifier :
				 Line ? Line->Modifiers : TArray<FCataclysmStatModifier>())
			{
				if (Modifier.Scale == ECataclysmStatScale::PerOwnStack)
				{
					++Stacked;
					continue;
				}
				if (Modifier.Bucket != ECataclysmStatBucket::Increased)
				{
					continue;
				}
				if (Modifier.Scale != ECataclysmStatScale::Fixed
					|| Modifier.Condition != ECataclysmStatCondition::Always)
				{
					++Moving;
				}
				OtherIncreases += Modifier.Value;
			}
			Test.TestEqual(FString::Printf(
				TEXT("'%s' holds one row scaled by its own stacks"), *Stat.ToString()),
				Stacked, 1);
			Test.TestEqual(FString::Printf(
				TEXT("'%s': every other increase, %.2f in all, is unscaled and "
					 "unconditioned"), *Stat.ToString(), OtherIncreases),
				Moving, 0);
			Test.TestEqual(FString::Printf(
				TEXT("'%s': the line's other increases are the stand-in's %.2f"),
				*Stat.ToString(), StandIn),
				OtherIncreases, StandIn, 0.001f);
			Other.Add(Stat, OtherIncreases);

			// PRINTED WHETHER OR NOT ANYTHING FAILS, because a passing
			// assertion's label never reaches the log, and the figure is the
			// point of this test for armor and movement speed: nought there
			// would mean the attribute increase was not covered after all.
			Test.AddInfo(FString::Printf(
				TEXT("'%s': the line's other increases total %.2f"),
				*Stat.ToString(), OtherIncreases));

			const float None = ASC->StatAppliedTo(Stat, FGameplayTagContainer(), 1000.0f);
			if (!Test.TestTrue(FString::Printf(
					TEXT("'%s' with no stacks is something"), *Stat.ToString()),
					None > 0.0f))
			{
				return;
			}
			Plain.Add(Stat, None);
		}

		const FName Event(Case.Event);
		const auto Expect = [&](const TCHAR* When, int32 Stacks)
		{
			for (const FName& Stat : Case.Stats)
			{
				const float Others = Other[Stat];
				Test.TestEqual(FString::Printf(
					TEXT("'%s', %s (other increases %.2f)"), *Stat.ToString(), When, Others),
					ASC->StatAppliedTo(Stat, FGameplayTagContainer(), 1000.0f) / Plain[Stat],
					(1.0f + (Others + Stacks * Case.PerStack) / 100.0f)
						/ (1.0f + Others / 100.0f),
					0.001f);
			}
		};

		ASC->ActOnEvent(Event);
		ASC->ActOnEvent(Event);
		Expect(TEXT("two events hold two stacks"), 2);

		for (int32 More = 0; More < Case.Cap; ++More)
		{
			ASC->ActOnEvent(Event);
		}
		Expect(TEXT("more events than its cap hold its cap"), Case.Cap);

		World->TimeSeconds += Case.Seconds - 0.1f;
		Expect(TEXT("just inside its window, still its cap"), Case.Cap);

		World->TimeSeconds += 0.2f;
		Expect(TEXT("and just after, none"), 0);
	}

	const FName AttackDamage(TEXT("attack_damage"));
	const FName SpellDamage(TEXT("spell_damage"));
	const FName MovementSpeed(TEXT("movement_speed"));
	const FName Armour(TEXT("armor"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCritStackRowTest,
	"Cataclysm.Enchantments.TheCriticalStrikeStackRowAdds5PercentDamagePerStackUpTo5",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Critical strikes grant a stack of power increasing all damage by 3%-5% for 5 seconds, up to 5 stacks", worn. Issue #1833. */
bool FCataclysmCritStackRowTest::RunTest(const FString&)
{
	using namespace CataclysmOwnStackRowTest;
	FCase Case;
	Case.Enchantment = TEXT("Positive_Critical_strikes_grant_a_stack_of_power_increasi");
	Case.bBenefit = true;
	Case.Event = TEXT("critical_strike");
	Case.Stats = {AttackDamage, SpellDamage};
	Case.PerStack = 5.0f;
	Case.Cap = 5;
	Case.Seconds = 5.0f;
	Check(*this, Case);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDotStackRowTest,
	"Cataclysm.Enchantments.TheDotStackRowAdds10PercentDamagePerStackUpTo5",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Applying a DoT to an enemy grants 5%-10% increased damage for 4 seconds, stacking up to 5 times", worn. Issue #1833. */
bool FCataclysmDotStackRowTest::RunTest(const FString&)
{
	using namespace CataclysmOwnStackRowTest;
	FCase Case;
	Case.Enchantment = TEXT("Positive_Applying_a_DoT_to_an_enemy_grants_5_10_increas");
	Case.bBenefit = true;
	Case.Event = TEXT("dot_applied");
	Case.Stats = {AttackDamage, SpellDamage};
	Case.PerStack = 10.0f;
	Case.Cap = 5;
	Case.Seconds = 4.0f;
	Check(*this, Case);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillSpeedStackRowTest,
	"Cataclysm.Enchantments.TheSkillUseSpeedStackRowAdds5PercentSpeedPerStackUpTo5",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Each skill use increases your movement speed by 3%-5% for 2 seconds, stacking up to 5 times", worn. Issue #1833. */
bool FCataclysmSkillSpeedStackRowTest::RunTest(const FString&)
{
	using namespace CataclysmOwnStackRowTest;
	FCase Case;
	Case.Enchantment = TEXT("Positive_Each_skill_use_increases_your_movement_speed_by");
	Case.bBenefit = true;
	Case.Event = TEXT("skill_use");
	Case.Stats = {MovementSpeed};
	Case.PerStack = 5.0f;
	Case.Cap = 5;
	Case.Seconds = 2.0f;
	Check(*this, Case);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMeleeArmourStackRowTest,
	"Cataclysm.Enchantments.TheMeleeHitTakenStackRowTakes5PercentArmourPerStackUpTo5",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Melee attacks that hit you reduce your armor by 3%-5% for 3 seconds, stacking up to 5 times", worn. Issue #1833. */
bool FCataclysmMeleeArmourStackRowTest::RunTest(const FString&)
{
	using namespace CataclysmOwnStackRowTest;
	FCase Case;
	Case.Enchantment = TEXT("Negative_Melee_attacks_that_hit_you_reduce_your_armor_by");
	Case.bBenefit = false;
	Case.Event = TEXT("melee_hit_taken");
	Case.Stats = {Armour};
	Case.PerStack = -5.0f;
	Case.Cap = 5;
	Case.Seconds = 3.0f;
	Check(*this, Case);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillStackRowTest,
	"Cataclysm.Enchantments.TheKillStackRowTakes4PercentDamagePerStackUpTo5",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Each kill reduces your damage by 2%-4% for 5 seconds, stacking up to 5 times", worn. Issue #1833. */
bool FCataclysmKillStackRowTest::RunTest(const FString&)
{
	using namespace CataclysmOwnStackRowTest;
	FCase Case;
	Case.Enchantment = TEXT("Negative_Each_kill_reduces_your_damage_by_2_4_for_5_sec");
	Case.bBenefit = false;
	Case.Event = TEXT("kill");
	Case.Stats = {AttackDamage, SpellDamage};
	Case.PerStack = -4.0f;
	Case.Cap = 5;
	Case.Seconds = 5.0f;
	Check(*this, Case);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillArmourStackRowTest,
	"Cataclysm.Enchantments.TheSkillUseArmourStackRowTakes2PercentArmourPerStackUpTo10",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Each skill use reduces your armor by 1%-2% for 3 seconds stacking up to 10 times", worn. Issue #1833. */
bool FCataclysmSkillArmourStackRowTest::RunTest(const FString&)
{
	using namespace CataclysmOwnStackRowTest;
	FCase Case;
	Case.Enchantment = TEXT("Negative_Each_skill_use_reduces_your_armor_by_1_2_for_3");
	Case.bBenefit = false;
	Case.Event = TEXT("skill_use");
	Case.Stats = {Armour};
	Case.PerStack = -2.0f;
	Case.Cap = 10;
	Case.Seconds = 3.0f;
	Check(*this, Case);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpellArmourStackRowTest,
	"Cataclysm.Enchantments.TheSpellStackRowTakes4PercentArmourPerStackUpTo5",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Each spell cast reduces your armor by 2%-4% for 3 seconds stacking up to 5 times", worn. Issue #1833. */
bool FCataclysmSpellArmourStackRowTest::RunTest(const FString&)
{
	using namespace CataclysmOwnStackRowTest;
	FCase Case;
	Case.Enchantment = TEXT("Negative_Each_spell_cast_reduces_your_armor_by_2_4_for");
	Case.bBenefit = false;
	Case.Event = TEXT("spell");
	Case.Stats = {Armour};
	Case.PerStack = -4.0f;
	Case.Cap = 5;
	Case.Seconds = 3.0f;
	Check(*this, Case);
	return true;
}

namespace CataclysmNextUseRowTest
{
	/**
	 * Wear one of the four next-use enchantments on a helm, fire its event
	 * `Events` times, and read what the held charges are worth by kind. A worn
	 * item rolls the top of its range. Issue #1833, phase 2.
	 */
	void Check(FAutomationTestBase& Test, const TCHAR* Enchantment,
			   const TCHAR* Event, int32 Events, bool bAttack,
			   float ExpectPercent, int32 ExpectCount)
	{
		using namespace CataclysmEnchantmentEffectTest;

		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };

		FWearer Wearer(World);
		UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
		Wearer.Equipment->Equip(
			Carrying(TEXT("Head_Helm"), Enchantment, DrawbackWithNoEffect),
			Removed, AlsoRemoved, Slot);
		Wearer.Equipment->RefreshAttributes(ASC);

		for (int32 Fired = 0; Fired < Events; ++Fired)
		{
			ASC->ActOnEvent(FName(Event));
		}

		float SkillPercent = 0.0f;
		float AttackPercent = 0.0f;
		int32 SkillCount = 0;
		int32 AttackCount = 0;
		ASC->NextUseChargesByKind(SkillPercent, SkillCount, AttackPercent, AttackCount);

		Test.TestEqual(FString::Printf(TEXT("%d '%s' events: the charges are worth %.0f%%"),
								   Events, Event, ExpectPercent),
			bAttack ? AttackPercent : SkillPercent, ExpectPercent, 0.01f);
		Test.TestEqual(FString::Printf(TEXT("and %d are held"), ExpectCount),
			bAttack ? AttackCount : SkillCount, ExpectCount);
		Test.TestEqual(TEXT("and none of the other kind"),
			bAttack ? SkillCount : AttackCount, 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDodgeNextSkillRowTest,
	"Cataclysm.Enchantments.TheDodgeRowHoldsOneNextSkillChargeOf60",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "When you dodge an attack your next skill deals 30%-60% increased damage":
 *  two dodges hold one charge, ruled 2026-09-24. Issue #1833, phase 2. */
bool FCataclysmDodgeNextSkillRowTest::RunTest(const FString&)
{
	CataclysmNextUseRowTest::Check(*this,
		TEXT("Positive_When_you_dodge_an_attack_your_next_skill_deals_3"),
		TEXT("dodge"), 2, /*bAttack=*/false, 60.0f, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFullResourceNextSkillRowTest,
	"Cataclysm.Enchantments.TheFullResourceRowHoldsOneNextSkillChargeOf60",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "When your class resource is full, your next skill deals 30%-60% increased
 *  damage", on the moment it becomes full. Issue #1833, phase 2. */
bool FCataclysmFullResourceNextSkillRowTest::RunTest(const FString&)
{
	CataclysmNextUseRowTest::Check(*this,
		TEXT("Positive_When_your_class_resource_is_full_your_next_skil"),
		TEXT("resource_full"), 1, /*bAttack=*/false, 60.0f, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMovementNextAttackRowTest,
	"Cataclysm.Enchantments.TheMovementRowHoldsOneNextAttackChargeOf60",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "After using your movement ability your next attack deals 30%-60% increased
 *  damage". Issue #1833, phase 2. */
bool FCataclysmMovementNextAttackRowTest::RunTest(const FString&)
{
	CataclysmNextUseRowTest::Check(*this,
		TEXT("Positive_After_using_your_movement_ability_your_next_atta"),
		TEXT("movement_skill"), 1, /*bAttack=*/true, 60.0f, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBlockNextAttackRowTest,
	"Cataclysm.Enchantments.TheBlockRowStacksNextAttackChargesOf20UpTo5",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Each successful block increases your next attack's damage by 10%-20%,
 *  stacking up to 5 times": six blocks hold five, worth 100. Issue #1833,
 *  phase 2. */
bool FCataclysmBlockNextAttackRowTest::RunTest(const FString&)
{
	CataclysmNextUseRowTest::Check(*this,
		TEXT("Positive_Each_successful_block_increases_your_next_attack"),
		TEXT("block"), 6, /*bAttack=*/true, 100.0f, 5);
	return true;
}

namespace CataclysmTimedRowTest
{
	/**
	 * Wear a timed enchantment on a helm and keep its wearer in combat, a blow a
	 * second, up to `Seconds` into the fight; then hand back the ability system
	 * for the caller to read. Issue #1833, timed grants. A worn item rolls the
	 * top of its range.
	 */
	struct FFight
	{
		FFight(FAutomationTestBase& InTest, const TCHAR* Enchantment)
			: Test(InTest)
		{
			using namespace CataclysmEnchantmentEffectTest;
			World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
			if (!World)
			{
				return;
			}
			Wearer = MakeUnique<FWearer>(World);
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
			Wearer->Equipment->Equip(
				Carrying(TEXT("Head_Helm"), Enchantment, DrawbackWithNoEffect),
				Removed, AlsoRemoved, Slot);
			Wearer->Equipment->RefreshAttributes(Wearer->AbilitySystem);
			Began = World->TimeSeconds;
			Wearer->AbilitySystem->NoteHitDealt();
		}

		~FFight()
		{
			Wearer.Reset();
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		void Until(float Seconds)
		{
			while (World && World->TimeSeconds < Began + Seconds - 0.001f)
			{
				World->TimeSeconds += 1.0f;
				Wearer->AbilitySystem->NoteHitDealt();
				Wearer->AbilitySystem->StepTimedGrants();
			}
		}

		UCataclysmAbilitySystemComponent* ASC() const
		{
			return Wearer ? Wearer->AbilitySystem : nullptr;
		}

		FAutomationTestBase& Test;
		UWorld* World = nullptr;
		TUniquePtr<CataclysmEnchantmentEffectTest::FWearer> Wearer;
		float Began = 0.0f;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMomentumRowTest,
	"Cataclysm.Enchantments.TheMomentumRowGainsAStackEvery10SecondsOfCombatUpTo5",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Every 10 seconds gain a stack of momentum granting 5%-10% increased damage,
 *  up to 5 stacks": one entry on two rows, one stack at 10 seconds of combat and
 *  not at 9, five by 50, still five at 60. Issue #1833, timed grants. */
bool FCataclysmMomentumRowTest::RunTest(const FString&)
{
	CataclysmTimedRowTest::FFight Fight(*this,
		TEXT("Positive_Every_10_seconds_gain_a_stack_of_momentum_granti"));
	if (!TestNotNull(TEXT("a wearer in a world"), Fight.ASC()))
	{
		return false;
	}
	const auto Held = [&]()
	{
		const TArray<UCataclysmAbilitySystemComponent::FHeldOwnStacks> Shown =
			Fight.ASC()->OwnStacksByEnchantment();
		return Shown.Num() == 1 ? Shown[0] : UCataclysmAbilitySystemComponent::FHeldOwnStacks();
	};

	Fight.Until(9.0f);
	TestEqual(TEXT("nine seconds in: no stack"), Held().Held, 0);
	Fight.Until(10.0f);
	TestEqual(TEXT("ten seconds in: one stack"), Held().Held, 1);
	TestEqual(TEXT("shown as one entry on two stats"), Held().Stats.Num(), 2);
	TestEqual(TEXT("with a cap of five"), Held().Cap, 5);
	Fight.Until(50.0f);
	TestEqual(TEXT("fifty seconds in: five"), Held().Held, 5);
	Fight.Until(60.0f);
	TestEqual(TEXT("sixty seconds in: still five"), Held().Held, 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEvery8SecondsRowTest,
	"Cataclysm.Enchantments.TheEvery8SecondsRowHoldsOneNextAttackChargeOf200",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Every 8 seconds your next attack deals 100%-200% increased damage": one
 *  charge worth 200 at 8 seconds of combat and not at 7. Issue #1833. */
bool FCataclysmEvery8SecondsRowTest::RunTest(const FString&)
{
	CataclysmTimedRowTest::FFight Fight(*this,
		TEXT("Positive_Every_8_seconds_your_next_attack_deals_100_200"));
	if (!TestNotNull(TEXT("a wearer in a world"), Fight.ASC()))
	{
		return false;
	}
	float SkillPercent = 0.0f;
	float AttackPercent = 0.0f;
	int32 SkillCount = 0;
	int32 AttackCount = 0;

	Fight.Until(7.0f);
	Fight.ASC()->NextUseChargesByKind(SkillPercent, SkillCount, AttackPercent, AttackCount);
	TestEqual(TEXT("seven seconds in: no charge"), AttackCount, 0);
	Fight.Until(8.0f);
	Fight.ASC()->NextUseChargesByKind(SkillPercent, SkillCount, AttackPercent, AttackCount);
	TestEqual(TEXT("eight seconds in: one next-attack charge"), AttackCount, 1);
	TestEqual(TEXT("worth 200%"), AttackPercent, 200.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEvery30SecondsRowTest,
	"Cataclysm.Enchantments.TheEvery30SecondsRowHoldsA500PercentEffectivenessCharge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Every 30 seconds your next skill is cast at 300%-500% effectiveness": a
 *  charge of 500% effectiveness at 30 seconds of combat and not at 29. Issue
 *  #1833, timed grants. */
bool FCataclysmEvery30SecondsRowTest::RunTest(const FString&)
{
	CataclysmTimedRowTest::FFight Fight(*this,
		TEXT("Positive_Every_30_seconds_your_next_skill_is_cast_at_300"));
	if (!TestNotNull(TEXT("a wearer in a world"), Fight.ASC()))
	{
		return false;
	}
	Fight.Until(29.0f);
	TestEqual(TEXT("twenty-nine seconds in: no effectiveness charge"),
		Fight.ASC()->NextUseEffectivenessHeld(), 0.0f, 0.01f);
	Fight.Until(30.0f);
	TestEqual(TEXT("thirty seconds in: 500% effectiveness held"),
		Fight.ASC()->NextUseEffectivenessHeld(), 500.0f, 0.01f);
	return true;
}

namespace CataclysmConsecutiveRowTest
{
	/**
	 * A real player character wearing one enchantment through its own
	 * equipment, and two creatures with no armour, evasion or block to strike.
	 * Issue #1833, phase 2. `Blow` deals one of the character's own blows
	 * through `ApplyHit`, which the creature resolves and announces, which is
	 * where `hit_dealt` comes from; it returns what the creature lost. Every
	 * blow states a critical strike chance of 0, so no blow's size is a roll.
	 * A worn item rolls the top of its range.
	 */
	struct FStriker
	{
		FStriker(const TCHAR* Positive, const TCHAR* Negative)
		{
			using namespace CataclysmEnchantmentEffectTest;
			World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
			if (!World)
			{
				return;
			}
			ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
			ASC = PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
			if (!ASC || !Character)
			{
				return;
			}
			Character->SetPlayerState(PlayerState);
			Character->OnRep_PlayerState();
			UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
			if (!Equipment)
			{
				return;
			}
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
			Equipment->Equip(Carrying(TEXT("Head_Helm"), Positive, Negative),
							 Removed, AlsoRemoved, Slot);
			Equipment->RefreshAttributes(ASC);
			// AFTER THE REFRESH, which writes an attack damage of nothing for a
			// character holding no weapon.
			ASC->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

			Swing = NewObject<UCataclysmStrikeSkill>(Character);
			Swing->SkillName = TEXT("Test Swing");
			Swing->SkillTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Melee"))));
			Swing->SkillTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Strike"))));

			First = Creature(FVector(200.0f, 0.0f, 0.0f));
			Second = Creature(FVector(-200.0f, 0.0f, 0.0f));

			Events = UCataclysmCombatEvents::In(World);
			if (Events)
			{
				Heard = Events->OnHit.AddLambda([this](const FCataclysmHitNotice& Notice)
				{
					if (Notice.Attacker == Character)
					{
						bLastBlowWasMelee = Notice.SkillTags && Notice.SkillTags->HasTag(
							FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Melee"))));
					}
				});
			}
		}

		~FStriker()
		{
			if (Events)
			{
				Events->OnHit.Remove(Heard);
			}
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool Ready() const
		{
			return ASC && Character && Swing && First && Second && Events;
		}

		ACataclysmEnemyCharacter* Creature(const FVector& Where) const
		{
			ACataclysmEnemyCharacter* Made =
				World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
			if (Made)
			{
				Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
				// TEN THOUSAND, NOT A MILLION: a float near a million holds steps of
				// 0.0625, so a blow read as health before less health after could
				// not meet a tolerance of 0.01. Near ten thousand the step is under
				// 0.001, and no test here deals more than about 2,500 to one creature.
				Made->SetHealth(10000.0f);
				Made->SetAttackDamage(0.0f);
				Made->SetArmour(0.0f);
				UAbilitySystemComponent* Its = Made->GetAbilitySystemComponent();
				Its->SetNumericAttributeBase(
					UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
				Its->SetNumericAttributeBase(
					UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
				Its->SetNumericAttributeBase(
					UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			}
			return Made;
		}

		/** One blow on Target: a melee swing, or a blow with no skill. */
		float Blow(ACataclysmEnemyCharacter* Target, bool bMelee)
		{
			const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
			UAbilitySystemComponent* Its = Target->GetAbilitySystemComponent();
			const float Before = Its->GetNumericAttribute(Health);
			FCataclysmHitDelivery Delivery;
			Delivery.CritChancePercent = 0.0f;
			if (bMelee)
			{
				Delivery.Skill = Swing;
			}
			bLastBlowWasMelee = false;
			UCataclysmSkillEffects::ApplyHit(Character, Target, /*DamagePercent=*/100.0f,
				bMelee ? Swing->SkillTags : FGameplayTagContainer(), Delivery);
			return Before - Its->GetNumericAttribute(Health);
		}

		UWorld* World = nullptr;
		UCataclysmAbilitySystemComponent* ASC = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmStrikeSkill* Swing = nullptr;
		ACataclysmEnemyCharacter* First = nullptr;
		ACataclysmEnemyCharacter* Second = nullptr;
		UCataclysmCombatEvents* Events = nullptr;
		FDelegateHandle Heard;
		bool bLastBlowWasMelee = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsecutiveMeleeRowTest,
	"Cataclysm.Enchantments.TheConsecutiveMeleeRowRaisesDamageOnOneEnemyUpTo8",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Each consecutive melee hit on the same enemy increases damage by 5%-10% up
 * to 8 stacks", worn at 10. Issue #1833, phase 2, ruled 2026-09-24. Each hit
 * on one enemy adds the same amount the second hit added, once for each hit
 * before it, up to 8: the tenth and the eleventh each add 8 times it. The first
 * melee hit on another enemy is plain, and starts the count there. A blow with
 * no skill, which is no melee hit, and an evaded melee swing neither count nor
 * start it again.
 *
 * WRITTEN AS DIFFERENCES FROM THE FIRST HIT, NOT AS MULTIPLES OF IT. A plain
 * blow from an attack damage of 100 deals 110 here, and the reason was not
 * found before this test was written. If that tenth is an increase, the row's
 * 10% adds to it and each hit adds 10 rather than 11; if it is a multiplier,
 * each adds 11. The differences are equal either way. The second hit's step
 * is at most a tenth of the first hit: equal to it if that tenth is a
 * multiplier, under it if it is an increase. The step is printed, so a run
 * says which.
 */
bool FCataclysmConsecutiveMeleeRowTest::RunTest(const FString&)
{
	const TCHAR* Row = TEXT("Positive_Each_consecutive_melee_hit_on_the_same_enemy_inc");
	CataclysmConsecutiveRowTest::FStriker Striker(
		Row, CataclysmEnchantmentEffectTest::DrawbackWithNoEffect);
	if (!TestTrue(TEXT("a wearer, two creatures and the announcements"), Striker.Ready()))
	{
		return false;
	}
	int32 Counting = 0;
	for (const FCataclysmPoolAction& Action : Striker.ASC->GetPoolActions())
	{
		Counting += Action.bConsecutiveHits ? 1 : 0;
	}
	if (!TestEqual(TEXT("the row's two stats each count hits in a row. If not, "
						"DT_EnchantmentEffects may be older than the rows: run "
						"tools/generate_datatable_assets.py"), Counting, 2))
	{
		return false;
	}

	const float Plain = Striker.Blow(Striker.First, /*bMelee=*/true);
	if (!TestTrue(TEXT("the swing was announced as a melee hit"), Striker.bLastBlowWasMelee)
		|| !TestTrue(TEXT("and dealt damage"), Plain > 0.0f))
	{
		return false;
	}
	const float Step = Striker.Blow(Striker.First, true) - Plain;
	AddInfo(FString::Printf(TEXT("the first hit dealt %.4f and the second added %.4f"),
		Plain, Step));
	TestTrue(TEXT("the second hit adds something"), Step > 0.0f);
	TestTrue(TEXT("and no more than a tenth of the first"), Step <= 0.1f * Plain + 0.01f);
	TestEqual(TEXT("the third adds twice that"),
		Striker.Blow(Striker.First, true) - Plain, 2.0f * Step, 0.02f);

	// NEITHER COUNTS NOR STARTS THE COUNT AGAIN.
	TestTrue(TEXT("a blow with no skill on the second enemy lands"),
		Striker.Blow(Striker.Second, /*bMelee=*/false) > 0.0f);
	UAbilitySystemComponent* SecondIts = Striker.Second->GetAbilitySystemComponent();
	SecondIts->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetEvasionAttribute(), 100.0f);

	// WHAT THE EVADE BELOW DEPENDS ON, printed on every run. It passed on one
	// whole suite and failed on another ("it was 110"), and reading the code
	// did not find why, so each run now says. `Resolve` evades when a roll in
	// 0..100 is below the evasion the pipeline answers for the creature, unless
	// the blow is an area one or cannot be evaded, which the swinger's
	// `melee_evasion_suppressed` decides for a melee blow.
	//
	// AND EACH IS ASSERTED AS SET-UP, returning early, so a failure names the
	// input that let the swing land rather than reading as "110". Ruled
	// 2026-09-25 by the coordinating session.
	const UCataclysmAbilitySystemComponent* Creature =
		Cast<UCataclysmAbilitySystemComponent>(SecondIts);
	const float EvasionAttribute = SecondIts->GetNumericAttribute(
		UCataclysmCombatAttributeSet::GetEvasionAttribute());
	const float PipelineEvasion = Creature
		? Creature->StatForSkill(FName(TEXT("evasion")), FGameplayTagContainer(), EvasionAttribute)
		: -1.0f;
	const FGameplayAttribute Suppressed =
		UCataclysmCombatAttributeSet::GetMeleeEvasionSuppressedAttribute();
	const float Suppression = Striker.ASC->HasAttributeSetForAttribute(Suppressed)
		? Striker.ASC->StatForSkill(
			  FName(UCataclysmDamageCalculation::MeleeEvasionSuppressedStat),
			  Striker.Swing->SkillTags, Striker.ASC->GetNumericAttribute(Suppressed))
		: 0.0f;
	AddInfo(FString::Printf(
		TEXT("evade inputs: creature evasion attribute %.3f; the pipeline's answer %.3f; "
			 "creature stat line for evasion %s; swinger's melee_evasion_suppressed %.3f"),
		EvasionAttribute, PipelineEvasion,
		Creature && Creature->GetStatInputs(FName(TEXT("evasion"))) ? TEXT("held") : TEXT("none"),
		Suppression));
	if (!TestTrue(TEXT("set-up: the pipeline's evasion for the creature is at least 100"),
			PipelineEvasion >= 100.0f)
		|| !TestTrue(TEXT("set-up: the swinger's melee_evasion_suppressed is nought"),
			Suppression <= 0.0f))
	{
		return false;
	}
	int32 HitsHeard = 0;
	const FDelegateHandle HitListener = Striker.Events->OnHit.AddLambda(
		[&HitsHeard, &Striker](const FCataclysmHitNotice& Notice)
		{
			HitsHeard += Notice.Attacker == Striker.Character ? 1 : 0;
		});
	const float Evaded = Striker.Blow(Striker.Second, true);
	Striker.Events->OnHit.Remove(HitListener);
	AddInfo(FString::Printf(TEXT("the evaded swing dealt %.3f and %d hit notice(s) were heard"),
		Evaded, HitsHeard));
	TestEqual(TEXT("an evaded melee swing on the second enemy deals nothing"),
		Evaded, 0.0f, 0.01f);
	SecondIts->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
	TestEqual(TEXT("so the fourth on the first enemy adds three times it"),
		Striker.Blow(Striker.First, true) - Plain, 3.0f * Step, 0.02f);

	for (int32 Fifth = 5; Fifth <= 9; ++Fifth)
	{
		Striker.Blow(Striker.First, true);
	}
	TestEqual(TEXT("nine hits hold a count of 8, the cap"),
		Striker.ASC->ConsecutiveHitsOn(
			FName(*FString::Printf(TEXT("%s:attack_damage"), Row)), Striker.First), 8);
	TestEqual(TEXT("the tenth hit adds 8 times it"),
		Striker.Blow(Striker.First, true) - Plain, 8.0f * Step, 0.02f);
	TestEqual(TEXT("and so does the eleventh"),
		Striker.Blow(Striker.First, true) - Plain, 8.0f * Step, 0.02f);

	const TArray<UCataclysmAbilitySystemComponent::FHeldOwnStacks> Shown =
		Striker.ASC->OwnStacksByEnchantment();
	TestTrue(TEXT("shown as one entry of hits in a row, 8 of 8, on two stats"),
		Shown.Num() == 1 && Shown[0].bConsecutiveHits && Shown[0].Held == 8
			&& Shown[0].Cap == 8 && Shown[0].Stats.Num() == 2);

	TestEqual(TEXT("the first melee hit on the second enemy is plain"),
		Striker.Blow(Striker.Second, true), Plain, 0.01f);
	TestEqual(TEXT("and the second on it adds it once"),
		Striker.Blow(Striker.Second, true) - Plain, Step, 0.02f);
	TestEqual(TEXT("back on the first enemy, the count started again: plain"),
		Striker.Blow(Striker.First, true), Plain, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsecutiveDrawbackRowTest,
	"Cataclysm.Enchantments.TheConsecutiveHitDrawbackCutsDamageOnOneEnemyUpTo10",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Each consecutive hit against the same enemy deals 5%-8% less damage, up to
 * 10 stacks", worn at 8. Issue #1833, phase 2. Any hit counts, a blow with no
 * skill included. Hit N on one enemy deals the first hit's damage less 8% for
 * each of the N-1 before it, up to 10: the eleventh and twelfth deal a fifth.
 * The first hit on another enemy is plain.
 */
bool FCataclysmConsecutiveDrawbackRowTest::RunTest(const FString&)
{
	CataclysmConsecutiveRowTest::FStriker Striker(
		CataclysmEnchantmentEffectTest::BenefitWithNoEffect,
		TEXT("Negative_Each_consecutive_hit_against_the_same_enemy_deal"));
	if (!TestTrue(TEXT("a wearer, two creatures and the announcements"), Striker.Ready()))
	{
		return false;
	}

	const float Plain = Striker.Blow(Striker.First, /*bMelee=*/false);
	if (!TestTrue(TEXT("the blow dealt damage"), Plain > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the second hit deals 8% less"),
		Striker.Blow(Striker.First, false), Plain * 0.92f, 0.01f);
	for (int32 Third = 3; Third <= 10; ++Third)
	{
		Striker.Blow(Striker.First, false);
	}
	TestEqual(TEXT("the eleventh deals a fifth"),
		Striker.Blow(Striker.First, false), Plain * 0.2f, 0.01f);
	TestEqual(TEXT("and so does the twelfth"),
		Striker.Blow(Striker.First, false), Plain * 0.2f, 0.01f);
	TestEqual(TEXT("the first hit on the second enemy is plain"),
		Striker.Blow(Striker.Second, false), Plain, 0.01f);
	return true;
}

namespace CataclysmPlacedRowTest
{
	/** How many of the wearer's actions place a stack on another character. */
	int32 PlacingActions(const UCataclysmAbilitySystemComponent* ASC)
	{
		int32 Placing = 0;
		for (const FCataclysmPoolAction& Action : ASC->GetPoolActions())
		{
			Placing += Action.PlacedKey.IsNone() ? 0 : 1;
		}
		return Placing;
	}

	const TCHAR* StaleAsset =
		TEXT("the row places a stack. If not, DT_EnchantmentEffects may be older "
			 "than the rows: run tools/generate_datatable_assets.py");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStrikeArmourRowTest,
	"Cataclysm.Enchantments.TheStrikeArmourRowTakes6PercentPerHitUpTo6",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Strike skills reduce enemy armor by 3%-6% per hit for 5 seconds, up to 6
 * stacks", worn at 6, by a real player character striking real creatures.
 * Issue #1833, phase 2. Seven strike hits take 36% of the creature's armour,
 * and its line says "Armor -36%"; a blow with no skill, which is no strike,
 * takes none. With 500 armour, the eighth hit deals more than the first.
 */
bool FCataclysmStrikeArmourRowTest::RunTest(const FString&)
{
	CataclysmConsecutiveRowTest::FStriker Striker(
		TEXT("Positive_Strike_skills_reduce_enemy_armor_by_3_6_per_hi"),
		CataclysmEnchantmentEffectTest::DrawbackWithNoEffect);
	if (!TestTrue(TEXT("a wearer, two creatures and the announcements"), Striker.Ready())
		|| !TestEqual(CataclysmPlacedRowTest::StaleAsset,
			CataclysmPlacedRowTest::PlacingActions(Striker.ASC), 1))
	{
		return false;
	}
	Striker.First->GetAbilitySystemComponent()->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetArmorAttribute(), 500.0f);
	const UCataclysmAbilitySystemComponent* First =
		Cast<UCataclysmAbilitySystemComponent>(Striker.First->GetAbilitySystemComponent());
	const UCataclysmAbilitySystemComponent* Second =
		Cast<UCataclysmAbilitySystemComponent>(Striker.Second->GetAbilitySystemComponent());

	const float FirstHit = Striker.Blow(Striker.First, /*bMelee=*/true);
	TestEqual(TEXT("one strike hit takes 6%"), First->ArmourRemovedPercentNow(), 6.0f, 0.001f);
	for (int32 Hit = 2; Hit <= 7; ++Hit)
	{
		Striker.Blow(Striker.First, true);
	}
	TestEqual(TEXT("seven take 36%, the cap of six stacks"),
		First->ArmourRemovedPercentNow(), 36.0f, 0.001f);
	TestEqual(TEXT("and the creature's line says so"),
		UCataclysmCombatOverlay::StatusLineFor(Striker.First), FString(TEXT("Armor -36%")));
	const float EighthHit = Striker.Blow(Striker.First, true);
	TestTrue(FString::Printf(TEXT("the eighth hit deals more than the first: %.3f against %.3f"),
			EighthHit, FirstHit),
		EighthHit > FirstHit);

	TestTrue(TEXT("a blow with no skill lands"), Striker.Blow(Striker.Second, false) > 0.0f);
	TestEqual(TEXT("and takes none of its armour"),
		Second->ArmourRemovedPercentNow(), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAnyHitArmourRowTest,
	"Cataclysm.Enchantments.TheAnyHitArmourRowTakes4PercentPerHitWithNoCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Hitting an enemy reduces their armor by 2%-4% for 5 seconds, stacking
 * indefinitely", worn at 4. Issue #1833, phase 2, ruled 2026-09-24: no cap on
 * the count, and all armour removed clamped at 100. Ten hits take 40, twenty-
 * five take 100, and twenty-six still take 100. Five seconds and a tenth after
 * the last hit, the whole count has lapsed.
 */
bool FCataclysmAnyHitArmourRowTest::RunTest(const FString&)
{
	CataclysmConsecutiveRowTest::FStriker Striker(
		TEXT("Positive_Hitting_an_enemy_reduces_their_armor_by_2_4_fo"),
		CataclysmEnchantmentEffectTest::DrawbackWithNoEffect);
	if (!TestTrue(TEXT("a wearer, two creatures and the announcements"), Striker.Ready())
		|| !TestEqual(CataclysmPlacedRowTest::StaleAsset,
			CataclysmPlacedRowTest::PlacingActions(Striker.ASC), 1))
	{
		return false;
	}
	const UCataclysmAbilitySystemComponent* First =
		Cast<UCataclysmAbilitySystemComponent>(Striker.First->GetAbilitySystemComponent());

	for (int32 Hit = 1; Hit <= 10; ++Hit)
	{
		Striker.Blow(Striker.First, /*bMelee=*/false);
	}
	TestEqual(TEXT("ten hits take 40, past any cap of six"),
		First->ArmourRemovedPercentNow(), 40.0f, 0.001f);
	for (int32 Hit = 11; Hit <= 25; ++Hit)
	{
		Striker.Blow(Striker.First, false);
	}
	TestEqual(TEXT("twenty-five take 100"), First->ArmourRemovedPercentNow(), 100.0f, 0.001f);
	Striker.Blow(Striker.First, false);
	TestEqual(TEXT("and twenty-six are clamped at 100"),
		First->ArmourRemovedPercentNow(), 100.0f, 0.001f);
	TestEqual(TEXT("the creature's line says so"),
		UCataclysmCombatOverlay::StatusLineFor(Striker.First), FString(TEXT("Armor -100%")));

	Striker.World->TimeSeconds += 5.1f;
	TestEqual(TEXT("5.1 seconds after the last hit the whole count has lapsed"),
		First->ArmourRemovedPercentNow(), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttackerDamageRowTest,
	"Cataclysm.Enchantments.TheMeleeHitTakenRowCutsTheAttackersDamage5PercentUpTo5",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Each melee hit you take reduces the attacker's damage by 3%-5% for 3
 * seconds, stacking up to 5 times", worn at 5. Issue #1833, phase 2. A
 * creature's melee blows on the wearer: the second deals 95% of the first, and
 * the sixth and seventh deal 75%. The creature's line says "Damage -25%". A
 * blow that is not melee places nothing on its attacker.
 */
bool FCataclysmAttackerDamageRowTest::RunTest(const FString&)
{
	CataclysmConsecutiveRowTest::FStriker Striker(
		TEXT("Positive_Each_melee_hit_you_take_reduces_the_attacker_s_d"),
		CataclysmEnchantmentEffectTest::DrawbackWithNoEffect);
	if (!TestTrue(TEXT("a wearer, two creatures and the announcements"), Striker.Ready())
		|| !TestEqual(CataclysmPlacedRowTest::StaleAsset,
			CataclysmPlacedRowTest::PlacingActions(Striker.ASC), 1))
	{
		return false;
	}

	// TEN THOUSAND HEALTH ON THE WEARER, for the reason the creatures have it:
	// a blow read as health before less health after, to better than 0.01.
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	Striker.ASC->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
	Striker.ASC->SetNumericAttributeBase(Health, 10000.0f);
	Striker.First->SetAttackDamage(100.0f);
	Striker.Second->SetAttackDamage(100.0f);
	const UCataclysmAbilitySystemComponent* First =
		Cast<UCataclysmAbilitySystemComponent>(Striker.First->GetAbilitySystemComponent());
	const UCataclysmAbilitySystemComponent* Second =
		Cast<UCataclysmAbilitySystemComponent>(Striker.Second->GetAbilitySystemComponent());

	FGameplayTagContainer Melee;
	Melee.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Melee"))));
	const auto Struck = [&](ACataclysmEnemyCharacter* By, const FGameplayTagContainer& Tags)
	{
		const float Before = Striker.ASC->GetNumericAttribute(Health);
		FCataclysmHitDelivery Delivery;
		Delivery.CritChancePercent = 0.0f;
		UCataclysmSkillEffects::ApplyHit(By, Striker.Character, /*DamagePercent=*/100.0f,
			Tags, Delivery);
		return Before - Striker.ASC->GetNumericAttribute(Health);
	};

	const float FirstBlow = Struck(Striker.First, Melee);
	if (!TestTrue(TEXT("the creature's melee blow lands"), FirstBlow > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("and places a 5% cut on it"), First->DamageCutPercentNow(), 5.0f, 0.001f);
	TestEqual(TEXT("its second blow deals 95% of the first"),
		Struck(Striker.First, Melee), FirstBlow * 0.95f, 0.01f);
	for (int32 Blow = 3; Blow <= 5; ++Blow)
	{
		Struck(Striker.First, Melee);
	}
	TestEqual(TEXT("its sixth deals 75%"), Struck(Striker.First, Melee), FirstBlow * 0.75f, 0.01f);
	TestEqual(TEXT("and its seventh, the cap of five stacks"),
		Struck(Striker.First, Melee), FirstBlow * 0.75f, 0.01f);
	TestEqual(TEXT("the creature's line says so"),
		UCataclysmCombatOverlay::StatusLineFor(Striker.First), FString(TEXT("Damage -25%")));

	TestTrue(TEXT("a blow that is not melee lands"),
		Struck(Striker.Second, FGameplayTagContainer()) > 0.0f);
	TestEqual(TEXT("and places nothing on its attacker"),
		Second->DamageCutPercentNow(), 0.0f, 0.001f);
	return true;
}

namespace CataclysmEveryNthRowTest
{
	/**
	 * A bare wearer carrying one real every-Nth drawback on a helm, beside a
	 * benefit with no effect row. Issue #1833, every Nth. A worn item rolls the
	 * top of its range. Out of combat throughout, where a count is kept.
	 */
	struct FWorn
	{
		explicit FWorn(const TCHAR* Drawback)
		{
			using namespace CataclysmEnchantmentEffectTest;
			World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
			if (!World)
			{
				return;
			}
			Wearer = MakeUnique<FWearer>(World);
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
			Wearer->Equipment->Equip(
				Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, Drawback),
				Removed, AlsoRemoved, Slot);
			Wearer->Equipment->RefreshAttributes(Wearer->AbilitySystem);
		}

		~FWorn()
		{
			Wearer.Reset();
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		UCataclysmAbilitySystemComponent* ASC() const
		{
			return Wearer ? Wearer->AbilitySystem : nullptr;
		}

		/** The one every-Nth action the row gave, or null. */
		const FCataclysmPoolAction* NthAction() const
		{
			const FCataclysmPoolAction* Found = nullptr;
			int32 Count = 0;
			for (const FCataclysmPoolAction& Action : ASC()->GetPoolActions())
			{
				if (Action.NthKind != ECataclysmEveryNth::None)
				{
					Found = &Action;
					++Count;
				}
			}
			return Count == 1 ? Found : nullptr;
		}

		UWorld* World = nullptr;
		TUniquePtr<CataclysmEnchantmentEffectTest::FWearer> Wearer;
	};

	const TCHAR* StaleAsset =
		TEXT("the row gives one every-Nth action. If not, DT_EnchantmentEffects may "
			 "be older than the rows: run tools/generate_datatable_assets.py");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEvery5thHitTakenRowTest,
	"Cataclysm.Enchantments.TheEvery5thHitTakenRowAdds100PercentOnTheFifth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Every 5th hit you take deals 50%-100% bonus damage", worn at 100: nothing
 *  due for four hits, 100 due on the fifth, and nothing on the sixth. Issue
 *  #1833, every Nth. */
bool FCataclysmEvery5thHitTakenRowTest::RunTest(const FString&)
{
	CataclysmEveryNthRowTest::FWorn Worn(
		TEXT("Negative_Every_5th_hit_you_take_deals_50_100_bonus_dama"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(CataclysmEveryNthRowTest::StaleAsset, Worn.NthAction()))
	{
		return false;
	}
	TestEqual(TEXT("it counts hits taken"),
		static_cast<int32>(Worn.NthAction()->NthKind),
		static_cast<int32>(ECataclysmEveryNth::HitTaken));
	TestEqual(TEXT("every 5th"), Worn.NthAction()->EveryNth, 5);

	UCataclysmAbilitySystemComponent* ASC = Worn.ASC();
	for (int32 Hit = 1; Hit <= 3; ++Hit)
	{
		ASC->NoteNthEvent(ECataclysmEveryNth::HitTaken);
	}
	TestEqual(TEXT("three hits in, the fourth is not due"),
		ASC->NthHitTakenBonusPercent(), 0.0f, 0.001f);
	ASC->NoteNthEvent(ECataclysmEveryNth::HitTaken);
	TestEqual(TEXT("four hits in, the fifth takes 100% more"),
		ASC->NthHitTakenBonusPercent(), 100.0f, 0.001f);
	ASC->NoteNthEvent(ECataclysmEveryNth::HitTaken);
	TestEqual(TEXT("and after the fifth, the sixth is not due"),
		ASC->NthHitTakenBonusPercent(), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEveryThirdSpellRowTest,
	"Cataclysm.Enchantments.TheEveryThirdSpellRowAdds80PercentOfManaHeldToTheThird",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Every third cast of your spells cost 20%-80% of your current mana", worn at
 *  80: nothing added for the second cast, 80 for the third. Issue #1833. */
bool FCataclysmEveryThirdSpellRowTest::RunTest(const FString&)
{
	CataclysmEveryNthRowTest::FWorn Worn(
		TEXT("Negative_Every_third_cast_of_your_spells_cost_20_80_of"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(CataclysmEveryNthRowTest::StaleAsset, Worn.NthAction()))
	{
		return false;
	}
	TestEqual(TEXT("it counts spells"),
		static_cast<int32>(Worn.NthAction()->NthKind),
		static_cast<int32>(ECataclysmEveryNth::SpellCast));
	TestEqual(TEXT("every 3rd"), Worn.NthAction()->EveryNth, 3);

	UCataclysmAbilitySystemComponent* ASC = Worn.ASC();
	ASC->NoteNthEvent(ECataclysmEveryNth::SpellCast);
	TestEqual(TEXT("one cast in, the second adds nothing"),
		ASC->NthSpellExtraManaPercent(), 0.0f, 0.001f);
	ASC->NoteNthEvent(ECataclysmEveryNth::SpellCast);
	TestEqual(TEXT("two casts in, the third adds 80% of the mana held"),
		ASC->NthSpellExtraManaPercent(), 80.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEvery10thAttackRowTest,
	"Cataclysm.Enchantments.TheEvery10thAttackRowDealsNothingOnTheTenth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Every 10th attack deals no damage": the ninth attack is not the Nth and
 *  the tenth is, shown as "Attack 9/10" before it. Issue #1833. */
bool FCataclysmEvery10thAttackRowTest::RunTest(const FString&)
{
	CataclysmEveryNthRowTest::FWorn Worn(TEXT("Negative_Every_10th_attack_deals_no_damage"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(CataclysmEveryNthRowTest::StaleAsset, Worn.NthAction()))
	{
		return false;
	}
	TestEqual(TEXT("it counts attacks"),
		static_cast<int32>(Worn.NthAction()->NthKind),
		static_cast<int32>(ECataclysmEveryNth::Attack));
	TestEqual(TEXT("every 10th"), Worn.NthAction()->EveryNth, 10);

	UCataclysmAbilitySystemComponent* ASC = Worn.ASC();
	for (int32 Attack = 1; Attack <= 8; ++Attack)
	{
		ASC->NoteNthEvent(ECataclysmEveryNth::Attack);
	}
	TestFalse(TEXT("eight attacks in, the ninth is not the Nth"), ASC->NextAttackIsNth());
	ASC->NoteNthEvent(ECataclysmEveryNth::Attack);
	TestTrue(TEXT("nine attacks in, the tenth is"), ASC->NextAttackIsNth());
	TestEqual(TEXT("and the line says so"),
		UCataclysmSkillBar::NthEntry(ECataclysmEveryNth::Attack,
			ASC->NthCountsForDisplay().Num() == 1 ? ASC->NthCountsForDisplay()[0].Count : -1,
			10),
		FString(TEXT("Attack 9/10")));
	return true;
}

namespace CataclysmRowsOnlyTest
{
	/**
	 * A bare wearer carrying one real enchantment on a helm, beside a partner
	 * with no effect row, kept in combat a blow a second by `Until`. Issue
	 * #1833, the rows-only batch. A worn item rolls the top of its range, which
	 * for a drawback is its largest loss.
	 *
	 * THE MAXIMUMS ARE WRITTEN AFTER THE HELM GOES ON, for the reason the
	 * ultimate-lock test gives: `RefreshAttributes` recomputes them from the
	 * gear, and a figure written before it does not survive.
	 */
	struct FWorn
	{
		FWorn(const TCHAR* Enchantment, bool bBenefit)
		{
			using namespace CataclysmEnchantmentEffectTest;
			World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
			if (!World)
			{
				return;
			}
			Wearer = MakeUnique<FWearer>(World);
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
			Wearer->Equipment->Equip(
				bBenefit
					? Carrying(TEXT("Head_Helm"), Enchantment, DrawbackWithNoEffect)
					: Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, Enchantment),
				Removed, AlsoRemoved, Slot);
			Wearer->Equipment->RefreshAttributes(Wearer->AbilitySystem);
		}

		~FWorn()
		{
			Wearer.Reset();
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		UCataclysmAbilitySystemComponent* ASC() const
		{
			return Wearer ? Wearer->AbilitySystem : nullptr;
		}

		/** Maximum first, then current: the vital set clamps health to it. */
		void SetHealth(float Maximum, float Current)
		{
			ASC()->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Maximum);
			ASC()->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), Current);
		}

		float Health() const
		{
			return ASC()->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		/** Start a fight now: the combat clock runs from this blow. */
		void BeginFight()
		{
			Began = World->TimeSeconds;
			ASC()->NoteHitDealt();
		}

		/** A blow a second, with the timed grants stepped, to `Seconds` in. */
		void Until(float Seconds)
		{
			while (World && World->TimeSeconds < Began + Seconds - 0.001f)
			{
				World->TimeSeconds += 1.0f;
				ASC()->NoteHitDealt();
				ASC()->StepTimedGrants();
			}
		}

		/** A stat applied to a figure of 1000 with no skill in hand. */
		float On1000(const TCHAR* Stat) const
		{
			return ASC()->StatAppliedTo(FName(Stat), FGameplayTagContainer(), 1000.0f);
		}

		UWorld* World = nullptr;
		TUniquePtr<CataclysmEnchantmentEffectTest::FWearer> Wearer;
		float Began = 0.0f;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTimedHealthRowsTest,
	"Cataclysm.Enchantments.TheTimedHealthRowsRestoreAndDrainOnTheirPeriods",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Three timed pool rows, each worn alone, each at 1000 maximum health. Issue
 * #1833, the rows-only batch. The clock counts only in combat, ruled
 * 2026-09-24.
 *
 * "Every 15 seconds regenerate 10%-20% of your maximum HP instantly", at 20:
 * from 500, nothing at 14 seconds, 700 at 15 and 900 at 30.
 * "You lose 15% of your max hp every 5 seconds": from 1000, nothing at 4
 * seconds, 850 at 5 and 700 at 10.
 * "Every 10 seconds you lose 5%-10% of your current HP", at 10: from 1000,
 * nothing at 9 seconds, 900 at 10 and 810 at 20 -- a share of what is held,
 * not of the maximum, which would give 800.
 */
bool FCataclysmTimedHealthRowsTest::RunTest(const FString&)
{
	using CataclysmRowsOnlyTest::FWorn;
	{
		FWorn Worn(TEXT("Positive_Every_15_seconds_regenerate_10_20_of_your_maxi"), true);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		Worn.SetHealth(1000.0f, 500.0f);
		Worn.BeginFight();
		Worn.Until(14.0f);
		TestEqual(TEXT("restore: fourteen seconds in, still 500"), Worn.Health(), 500.0f, 0.01f);
		Worn.Until(15.0f);
		TestEqual(TEXT("restore: fifteen seconds in, 20% of 1000 restored"), Worn.Health(), 700.0f, 0.01f);
		Worn.Until(30.0f);
		TestEqual(TEXT("restore: thirty seconds in, twice"), Worn.Health(), 900.0f, 0.01f);
	}
	{
		FWorn Worn(TEXT("Negative_You_lose_15_of_your_max_hp_every_5_seconds"), false);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		Worn.SetHealth(1000.0f, 1000.0f);
		Worn.BeginFight();
		Worn.Until(4.0f);
		TestEqual(TEXT("maximum drain: four seconds in, still 1000"), Worn.Health(), 1000.0f, 0.01f);
		Worn.Until(5.0f);
		TestEqual(TEXT("maximum drain: five seconds in, 15% of 1000 lost"), Worn.Health(), 850.0f, 0.01f);
		Worn.Until(10.0f);
		TestEqual(TEXT("maximum drain: ten seconds in, twice"), Worn.Health(), 700.0f, 0.01f);
	}
	{
		FWorn Worn(TEXT("Negative_Every_10_seconds_you_lose_5_10_of_your_current"), false);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		Worn.SetHealth(1000.0f, 1000.0f);
		Worn.BeginFight();
		Worn.Until(9.0f);
		TestEqual(TEXT("current drain: nine seconds in, still 1000"), Worn.Health(), 1000.0f, 0.01f);
		Worn.Until(10.0f);
		TestEqual(TEXT("current drain: ten seconds in, 10% of 1000 lost"), Worn.Health(), 900.0f, 0.01f);
		Worn.Until(20.0f);
		TestEqual(TEXT("current drain: twenty seconds in, 10% of 900 lost"), Worn.Health(), 810.0f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRegenerationShareRowsTest,
	"Cataclysm.Enchantments.TheRegenerationRowsAddTheirShareOfEachWhole100OfTheMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Regenerate 1%-3% of your maximum HP per second" at 3 and "Regenerate 5%-10%
 * of your maximum mana per second" at 10, each worn alone. Issue #1833, the
 * rows-only batch.
 *
 * READ AS A RATIO AGAINST A FIGURE OF 100, so that neither the line's own base
 * nor any increase on it decides the answer. Below 100 of the maximum the row
 * adds nothing, so the figure reads 100 times the line's multiplier; at 1000 it
 * reads 100 plus the row's flat, times the same multiplier. The ratio is the
 * row's flat over 100, and whole steps only: 1099 reads as 1000 does.
 */
bool FCataclysmRegenerationShareRowsTest::RunTest(const FString&)
{
	using CataclysmRowsOnlyTest::FWorn;
	struct FCase
	{
		const TCHAR* Enchantment;
		const TCHAR* Stat;
		FGameplayAttribute Maximum;
		float PerHundred;
	};
	const FCase Cases[] = {
		{TEXT("Positive_Regenerate_1_3_of_your_maximum_HP_per_second"),
		 TEXT("health_regen"), UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 3.0f},
		{TEXT("Positive_Regenerate_5_10_of_your_maximum_mana_per_secon"),
		 TEXT("mana_regen"), UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 10.0f},
	};
	for (const FCase& Case : Cases)
	{
		FWorn Worn(Case.Enchantment, true);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		const auto At = [&](float Maximum)
		{
			Worn.ASC()->SetNumericAttributeBase(Case.Maximum, Maximum);
			return Worn.ASC()->StatAppliedTo(FName(Case.Stat), FGameplayTagContainer(), 100.0f);
		};
		const float Below = At(99.0f);
		if (!TestTrue(FString::Printf(TEXT("'%s' below one step reads something"), Case.Stat),
				Below > 0.0f))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("'%s' at 1000: ten steps"), Case.Stat),
			At(1000.0f) / Below, 1.0f + 10.0f * Case.PerHundred / 100.0f, 0.0001f);
		TestEqual(FString::Printf(TEXT("'%s' at 1099: still ten"), Case.Stat),
			At(1099.0f) / Below, 1.0f + 10.0f * Case.PerHundred / 100.0f, 0.0001f);
		TestEqual(FString::Printf(TEXT("'%s' at 1100: eleven"), Case.Stat),
			At(1100.0f) / Below, 1.0f + 11.0f * Case.PerHundred / 100.0f, 0.0001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTimedStackRowsTest,
	"Cataclysm.Enchantments.TheTimedStackRowsHoldTheirEffectForTheirWindowOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two timed own-stack rows, each worn alone. Issue #1833, the rows-only batch.
 *
 * "Every 10 seconds your movement speed is increased by 30%-50% for 3 seconds",
 * at 50: nothing at 9 seconds of combat, half as much again at 10, still at
 * 12.9, and gone at 13.1.
 * "Every 20 seconds your damage is halved for 5 seconds": nothing at 19, half
 * of both attack and spell damage at 20, still at 24.9, and gone at 25.1.
 *
 * READ AS A RATIO against the same stat before the window, so a line's other
 * increases cannot decide the answer. A "more" row multiplies whatever else is
 * there; an increase adds to the others, and a bare wearer's movement line
 * holds only an attribute increase of nought, so its ratio is 1.5 exactly.
 */
bool FCataclysmTimedStackRowsTest::RunTest(const FString&)
{
	using CataclysmRowsOnlyTest::FWorn;
	{
		FWorn Worn(TEXT("Positive_Every_10_seconds_your_movement_speed_is_increase"), true);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		Worn.BeginFight();
		Worn.Until(9.0f);
		const float Plain = Worn.On1000(TEXT("movement_speed"));
		if (!TestTrue(TEXT("movement speed reads something"), Plain > 0.0f))
		{
			return false;
		}
		Worn.Until(10.0f);
		TestEqual(TEXT("ten seconds in: 50% increased"),
			Worn.On1000(TEXT("movement_speed")) / Plain, 1.5f, 0.0001f);
		Worn.World->TimeSeconds += 2.9f;
		TestEqual(TEXT("just inside its three seconds: still 50%"),
			Worn.On1000(TEXT("movement_speed")) / Plain, 1.5f, 0.0001f);
		Worn.World->TimeSeconds += 0.2f;
		TestEqual(TEXT("just after: gone"),
			Worn.On1000(TEXT("movement_speed")) / Plain, 1.0f, 0.0001f);
	}
	{
		FWorn Worn(TEXT("Negative_Every_20_seconds_your_damage_is_halved_for_5_sec"), false);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		Worn.BeginFight();
		Worn.Until(19.0f);
		const float Attack = Worn.On1000(TEXT("attack_damage"));
		const float Spell = Worn.On1000(TEXT("spell_damage"));
		if (!TestTrue(TEXT("both damage lines read something"), Attack > 0.0f && Spell > 0.0f))
		{
			return false;
		}
		Worn.Until(20.0f);
		TestEqual(TEXT("twenty seconds in: attack damage halved"),
			Worn.On1000(TEXT("attack_damage")) / Attack, 0.5f, 0.0001f);
		TestEqual(TEXT("twenty seconds in: spell damage halved"),
			Worn.On1000(TEXT("spell_damage")) / Spell, 0.5f, 0.0001f);
		Worn.World->TimeSeconds += 4.9f;
		TestEqual(TEXT("just inside its five seconds: still halved"),
			Worn.On1000(TEXT("attack_damage")) / Attack, 0.5f, 0.0001f);
		Worn.World->TimeSeconds += 0.2f;
		TestEqual(TEXT("just after: attack damage whole again"),
			Worn.On1000(TEXT("attack_damage")) / Attack, 1.0f, 0.0001f);
		TestEqual(TEXT("just after: spell damage whole again"),
			Worn.On1000(TEXT("spell_damage")) / Spell, 1.0f, 0.0001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillUseArmourOnceRowTest,
	"Cataclysm.Enchantments.TheSkillUseArmourRowTakes20PercentFor3SecondsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Lose 10%-20% armor when you use a skill", at 20. Issue #1833, the rows-only
 * batch. The sentence states no duration and no cap; 3 seconds and one stack
 * are the labelled judgement recorded in docs/DECISIONS.md, from the sibling
 * row "Each skill use reduces your armor by 1%-2% for 3 seconds".
 *
 * A bare wearer's armour line holds only an attribute increase of nought, so
 * one stack reads 0.8 of the plain figure, and a second skill use does not
 * take it to 0.6.
 */
bool FCataclysmSkillUseArmourOnceRowTest::RunTest(const FString&)
{
	using CataclysmRowsOnlyTest::FWorn;
	FWorn Worn(TEXT("Negative_Lose_10_20_armor_when_you_use_a_skill"), false);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const float Plain = Worn.On1000(TEXT("armor"));
	if (!TestTrue(TEXT("armour reads something"), Plain > 0.0f))
	{
		return false;
	}
	Worn.ASC()->ActOnEvent(FName(TEXT("skill_use")));
	TestEqual(TEXT("one skill use: 20% less armour"),
		Worn.On1000(TEXT("armor")) / Plain, 0.8f, 0.0001f);
	Worn.ASC()->ActOnEvent(FName(TEXT("skill_use")));
	TestEqual(TEXT("a second: still 20%, one stack at most"),
		Worn.On1000(TEXT("armor")) / Plain, 0.8f, 0.0001f);
	Worn.World->TimeSeconds += 2.9f;
	TestEqual(TEXT("just inside three seconds: still 20%"),
		Worn.On1000(TEXT("armor")) / Plain, 0.8f, 0.0001f);
	Worn.World->TimeSeconds += 0.2f;
	TestEqual(TEXT("just after: whole again"),
		Worn.On1000(TEXT("armor")) / Plain, 1.0f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourDecayRowTest,
	"Cataclysm.Enchantments.TheFervourDecayRowDoublesTheDecayANodeSupplies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your class resource decays twice as fast". Issue #1833, the rows-only batch.
 *
 * THE DECAY RATE COMES FROM A PASSIVE NODE, `Ravager_basic_spine_000` at 5 a
 * second, and a bare wearer has none, so the row is applied to a figure of 5,
 * which is the node's flat. `UCataclysmFervour` asks the stat with a fallback of
 * nought, so a character with no decay keeps none: twice nothing.
 */
bool FCataclysmFervourDecayRowTest::RunTest(const FString&)
{
	using CataclysmRowsOnlyTest::FWorn;
	FWorn Worn(TEXT("Negative_Your_class_resource_decays_twice_as_fast"), false);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const FName Decay(UCataclysmFervour::DecayPerSecondStat);
	TestEqual(TEXT("a node's 5 a second decays at 10"),
		Worn.ASC()->StatAppliedTo(Decay, FGameplayTagContainer(), 5.0f), 10.0f, 0.0001f);
	TestEqual(TEXT("and no decay stays none"),
		Worn.ASC()->StatForSkill(Decay, FGameplayTagContainer(), 0.0f), 0.0f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionCountRowTest,
	"Cataclysm.Enchantments.TheMinionCountRowTakes4FromTheCapBonus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Minus 2-4 to your max minion count", at 4. Issue #1833, the rows-only batch.
 *
 * READ THE WAY THE CAP READS IT, through `StatForSkill`, which is what
 * `MinionCapFor` in CataclysmSkillTemplates.cpp asks. The floor of one minion
 * that function applies is not reached from here: it is file-local and runs only
 * when a summoning skill is cast.
 */
bool FCataclysmMinionCountRowTest::RunTest(const FString&)
{
	using CataclysmRowsOnlyTest::FWorn;
	FWorn Worn(TEXT("Negative_Minus_2_4_to_your_max_minion_count"), false);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	TestEqual(TEXT("the cap bonus is minus four"),
		Worn.ASC()->StatForSkill(FName(UCataclysmCommand::MinionCapBonusStat),
								 FGameplayTagContainer(), 0.0f),
		-4.0f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMaximumHealthShareRowTest,
	"Cataclysm.Enchantments.TheMaximumHealthShareRowLeavesTheShareItsTextShows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your maximum HP cannot exceed 40%-60% of its normal value". Issue #1833, the
 * rows-only batch.
 *
 * THE ROW IS THE COMPLEMENT OF ITS SENTENCE, AND ITS RANGE ROLLS DOWN. Ruled
 * 2026-10-05: a roll of 1 gives a drawback its harshest figure, which here is
 * the sentence's FIRST number, 40. The sentence marks its range, so the text
 * rolls from 60 to 40 and the row is written max_health more, -40 to -60. At
 * the lowest roll the text shows 60 and the value -40 leaves 60% of the
 * maximum; at the highest the text shows 40 and -60 leaves 40%. Until that
 * ruling the roll ran the other way. Read in play, off the maximum health a
 * real refresh writes, against the same wearer with nothing on.
 */
bool FCataclysmMaximumHealthShareRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	const TCHAR* Enchantment = TEXT("Negative_Your_maximum_HP_cannot_exceed_40_60_of_its_nor");
	const FString Sentence(TEXT("Your maximum HP cannot exceed 40%-60% of its normal value"));

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);
	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();

	Wearer.Equipment->RefreshAttributes(Wearer.AbilitySystem);
	const float Plain = Wearer.AbilitySystem->GetNumericAttribute(MaxHealth);
	if (!TestTrue(TEXT("a bare wearer has a maximum health"), Plain > 1.0f))
	{
		return false;
	}

	const auto WornAt = [&](float Roll)
	{
		FCataclysmItem Item = Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, Enchantment);
		Item.Enchantments[0].NegativeRoll = Roll;
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
		Wearer.Equipment->Equip(Item, Removed, AlsoRemoved, Slot);
		Wearer.Equipment->RefreshAttributes(Wearer.AbilitySystem);
		const float Share = Wearer.AbilitySystem->GetNumericAttribute(MaxHealth) / Plain;
		Wearer.Equipment->Unequip(Slot, Removed);
		Wearer.Equipment->RefreshAttributes(Wearer.AbilitySystem);
		return Share;
	};

	// THE MARK THE SHIPPED SENTENCE ROW CARRIES, read from the table, so the
	// text below is rolled the way the hover text rolls it.
	const UDataTable* Sentences = UCataclysmDropRoll::LoadNegativeEnchantmentTable();
	const FCataclysmEnchantmentRow* SentenceRow = Sentences
		? Sentences->FindRow<FCataclysmEnchantmentRow>(FName(Enchantment), TEXT("test"), false) : nullptr;
	if (!TestNotNull(TEXT("the sentence row"), SentenceRow)
		|| !TestEqual(TEXT("the sentence marks its one range as rolling down. If not, "
						   "DT_EnchantmentsNegative may be older than the sheet: run "
						   "tools/generate_datatable_assets.py"),
				SentenceRow->RollsDown, FString(TEXT("1"))))
	{
		return false;
	}
	TestEqual(TEXT("at the lowest roll the text says 60%"),
		UCataclysmItemValues::EnchantmentTextFor(Sentence, SentenceRow->RollsDown, 0.0f),
		FString(TEXT("Your maximum HP cannot exceed 60% of its normal value")));
	TestEqual(TEXT("and 60% of the maximum is what is left"), WornAt(0.0f), 0.6f, 0.0001f);
	TestEqual(TEXT("at the highest roll the text says 40%, the harshest"),
		UCataclysmItemValues::EnchantmentTextFor(Sentence, SentenceRow->RollsDown, 1.0f),
		FString(TEXT("Your maximum HP cannot exceed 40% of its normal value")));
	TestEqual(TEXT("and 40% of the maximum is what is left"), WornAt(1.0f), 0.4f, 0.0001f);
	TestEqual(TEXT("and with it off, the whole maximum again"),
		Wearer.AbilitySystem->GetNumericAttribute(MaxHealth) / Plain, 1.0f, 0.0001f);
	return true;
}

namespace CataclysmSmallHalvesTest
{
	/**
	 * A bare wearer in its own world, carrying one real enchantment on a helm
	 * beside a partner with no effect row. Issue #1833, the small engine halves.
	 * A worn item rolls the top of its range.
	 */
	struct FWorn
	{
		FWorn(const TCHAR* Enchantment, bool bBenefit)
		{
			using namespace CataclysmEnchantmentEffectTest;
			World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
			if (!World)
			{
				return;
			}
			Wearer = MakeUnique<FWearer>(World);
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
			Wearer->Equipment->Equip(
				bBenefit
					? Carrying(TEXT("Head_Helm"), Enchantment, DrawbackWithNoEffect)
					: Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, Enchantment),
				Removed, AlsoRemoved, Slot);
			Wearer->Equipment->RefreshAttributes(Wearer->AbilitySystem);
		}

		~FWorn()
		{
			Wearer.Reset();
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		UCataclysmAbilitySystemComponent* ASC() const
		{
			return Wearer ? Wearer->AbilitySystem : nullptr;
		}

		UWorld* World = nullptr;
		TUniquePtr<CataclysmEnchantmentEffectTest::FWearer> Wearer;
	};

	FGameplayTag Keyword(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(Name),
															  /*ErrorIfNotFound=*/false);
	}

	/**
	 * What one tick of a damage over time effect the wearer applies takes from
	 * a fresh creature, applied through `ApplyDamageOverTime` with this
	 * ailment's tag, which is the route every ailment in the game takes.
	 */
	float OneTick(const FWorn& Worn, const FGameplayTag& Ailment, float Along)
	{
		ACataclysmEnemyCharacter* Victim = Worn.World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(Along, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (!Victim)
		{
			return -1.0f;
		}
		Victim->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Victim->SetHealth(10000.0f);
		UCataclysmAbilitySystemComponent* Its =
			Cast<UCataclysmAbilitySystemComponent>(Victim->GetAbilitySystemComponent());
		if (!Its)
		{
			return -1.0f;
		}
		const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
		const float Before = Its->GetNumericAttribute(Health);
		if (!UCataclysmSkillEffects::ApplyDamageOverTime(
				Worn.Wearer->Actor, Victim, /*DamagePerTick=*/100.0f,
				/*DurationSeconds=*/6.0f, Ailment)
			|| Its->ExecutePeriodicEffectsGrantingForTests(Ailment) != 1)
		{
			return -1.0f;
		}
		return Before - Its->GetNumericAttribute(Health);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAilmentScopedDotRowsTest,
	"Cataclysm.Enchantments.TheAilmentScopedDotRowsReachOnlyTheirOwnAilment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Four rows scoped to one ailment each, worn at 60. Issue #1833, the small
 * engine halves: until the damage over time asks were handed the ailment's own
 * tag, a row scoped to `Keyword.DoT.Burn` applied to nothing.
 *
 * "Burn effects you apply deal 30%-60% increased damage per second" and "Poison
 * effects you apply deal 30%-60% increased damage per second": one tick applied
 * through `ApplyDamageOverTime`, against a disease tick from the same wearer on
 * a creature just like it, is 1.6 times as large. Disease ticks plainly, and the
 * two creatures are alike, so everything but the row cancels.
 *
 * "Bleed stacks you apply deal 30%-60% increased damage" and "Poison stacks you
 * apply have 30%-60% increased duration": read from `DamageOverTimeNumbers`
 * asked with the ailment's tag and with another's, because a bleed ticks only
 * while its target moves and a duration is not a tick.
 */
bool FCataclysmAilmentScopedDotRowsTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	const FGameplayTag Burn = Keyword(TEXT("Keyword.DoT.Burn"));
	const FGameplayTag Poison = Keyword(TEXT("Keyword.DoT.Poison"));
	const FGameplayTag Bleed = Keyword(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTag Disease = Keyword(TEXT("Keyword.DoT.Disease"));
	if (!TestTrue(TEXT("the four ailment tags are in the vocabulary"),
			Burn.IsValid() && Poison.IsValid() && Bleed.IsValid() && Disease.IsValid()))
	{
		return false;
	}

	struct FTickCase
	{
		const TCHAR* Enchantment;
		FGameplayTag Ailment;
	};
	for (const FTickCase& Case : {
			 FTickCase{TEXT("Positive_Burn_effects_you_apply_deal_30_60_increased_da"), Burn},
			 FTickCase{TEXT("Positive_Poison_effects_you_apply_deal_30_60_increased"), Poison}})
	{
		FWorn Worn(Case.Enchantment, true);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		const float Own = OneTick(Worn, Case.Ailment, 300.0f);
		const float Other = OneTick(Worn, Disease, -300.0f);
		if (!TestTrue(FString::Printf(TEXT("%s: both ticks landed"), *Case.Ailment.ToString()),
				Own > 0.0f && Other > 0.0f))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: its own tick is 60%% larger than a disease tick"),
				*Case.Ailment.ToString()),
			Own / Other, 1.6f, 0.001f);
	}

	{
		FWorn Worn(TEXT("Positive_Bleed_stacks_you_apply_deal_30_60_increased_da"), true);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		const float Own = UCataclysmSkillEffects::DamageOverTimeNumbers(
			Worn.ASC(), 100.0f, 5.0f, FGameplayTagContainer(Bleed)).DamagePerTick;
		const float Other = UCataclysmSkillEffects::DamageOverTimeNumbers(
			Worn.ASC(), 100.0f, 5.0f, FGameplayTagContainer(Disease)).DamagePerTick;
		if (TestTrue(TEXT("bleed: both ask something"), Own > 0.0f && Other > 0.0f))
		{
			TestEqual(TEXT("bleed: 60% more per tick, and only for a bleed"),
				Own / Other, 1.6f, 0.001f);
		}
	}
	{
		FWorn Worn(TEXT("Positive_Poison_stacks_you_apply_have_30_60_increased_d"), true);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		const float Own = UCataclysmSkillEffects::DamageOverTimeNumbers(
			Worn.ASC(), 100.0f, 8.0f, FGameplayTagContainer(Poison)).DurationSeconds;
		const float Other = UCataclysmSkillEffects::DamageOverTimeNumbers(
			Worn.ASC(), 100.0f, 8.0f, FGameplayTagContainer(Disease)).DurationSeconds;
		if (TestTrue(TEXT("poison duration: both ask something"), Own > 0.0f && Other > 0.0f))
		{
			TestEqual(TEXT("poison duration: 60% longer, and only for a poison"),
				Own / Other, 1.6f, 0.001f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFirstHitResistanceRowTest,
	"Cataclysm.Enchantments.TheFirstHitResistanceRowIgnoresResistanceOnTheFirstBlowOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your first hit against each enemy in a combat ignores all resistances", worn,
 * through real blows. Issue #1833, the small engine halves: the penetration ask
 * was handed no target, so the first-hit condition refused every time.
 *
 * A CREATURE WITH 50 RESISTANCE, STRUCK THREE TIMES. The first blow ignores
 * its resistance and the second does not, so the second is half the first; the
 * third is the same as the second, because the enemy stays struck. "In a
 * combat" is read as the existing first-hit rows read it, ruled 2026-09-25: the
 * record is per enemy.
 */
bool FCataclysmFirstHitResistanceRowTest::RunTest(const FString&)
{
	CataclysmConsecutiveRowTest::FStriker Striker(
		TEXT("Positive_Your_first_hit_against_each_enemy_in_a_combat_ig"),
		CataclysmEnchantmentEffectTest::DrawbackWithNoEffect);
	if (!TestTrue(TEXT("a wearer, two creatures and the announcements"), Striker.Ready()))
	{
		return false;
	}
	// FIFTY OF ALL RESISTANCE, which is the one resistance a creature holds
	// whatever the blow's damage type. A creature takes no difficulty penalty
	// (`ResistancePenaltyFor` applies it to players only), so fifty is fifty.
	Striker.First->GetAbilitySystemComponent()->SetNumericAttributeBase(
		UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 50.0f);

	const float FirstBlow = Striker.Blow(Striker.First, /*bMelee=*/true);
	const float SecondBlow = Striker.Blow(Striker.First, /*bMelee=*/true);
	const float ThirdBlow = Striker.Blow(Striker.First, /*bMelee=*/true);
	AddInfo(FString::Printf(TEXT("blows on the resisting creature: %.3f, %.3f, %.3f"),
		FirstBlow, SecondBlow, ThirdBlow));
	if (!TestTrue(TEXT("every blow landed"), FirstBlow > 0.0f && SecondBlow > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the first blow ignores the fifty, so the second is half of it"),
		SecondBlow / FirstBlow, 0.5f, 0.001f);
	TestEqual(TEXT("and the third meets resistance as the second did"),
		ThirdBlow, SecondBlow, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEveryHitTakenDrainRowTest,
	"Cataclysm.Enchantments.TheEveryHitTakenDrainRowTakes10PercentOnlyWhenTheBlowLands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Every hit you take deals an additional 5%-10% of your maximum HP as bonus
 * damage", worn at 10. Issue #1833, the small engine halves: a pool action on
 * `hit_taken` fired on an evaded blow too, and an evaded blow is not a hit,
 * ruled 2026-09-23. At 1000 maximum health, an evaded blow takes nothing and a
 * landed one takes 100. A drain cannot kill, ruled 2026-09-14.
 */
bool FCataclysmEveryHitTakenDrainRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	FWorn Worn(TEXT("Negative_Every_hit_you_take_deals_an_additional_5_10_of"), false);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	Worn.ASC()->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
	Worn.ASC()->SetNumericAttributeBase(Health, 1000.0f);

	Worn.ASC()->NoteHitTaken(/*bLanded=*/false);
	TestEqual(TEXT("an evaded blow takes nothing"),
		Worn.ASC()->GetNumericAttribute(Health), 1000.0f, 0.01f);
	Worn.ASC()->NoteHitTaken(/*bLanded=*/true);
	TestEqual(TEXT("a landed blow takes 10% of the maximum"),
		Worn.ASC()->GetNumericAttribute(Health), 900.0f, 0.01f);
	// AND A SECOND IN THE SAME MOMENT TAKES ANOTHER 100. Issue #1833 group D,
	// ruled 2026-09-30: the sentence says every hit, so its Trigger Cooldown is
	// an explicit 0 rather than the quarter second other hit-fired rows wait.
	Worn.ASC()->NoteHitTaken(/*bLanded=*/true);
	TestEqual(TEXT("a second landed blow in the same moment takes another 10%"),
		Worn.ASC()->GetNumericAttribute(Health), 800.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoShieldDelayRowTest,
	"Cataclysm.Enchantments.TheNoShieldDelayRowSetsItsFlag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Energy shield regeneration begins immediately after taking damage with no
 * delay", worn, sets the flag the regeneration step reads, as the attribute and
 * as the stat. Issue #1833, the small engine halves. What the flag does to the
 * recharge is `Cataclysm.ShieldKeystones.NoDelayRechargesAtTheFullRateInsideTheWait`.
 */
bool FCataclysmNoShieldDelayRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	FWorn Worn(TEXT("Positive_Energy_shield_regeneration_begins_immediately_af"), true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	TestEqual(TEXT("the attribute holds the flag"),
		Worn.ASC()->GetNumericAttribute(
			UCataclysmCombatAttributeSet::GetShieldRechargeHasNoDelayAttribute()),
		1.0f, 0.0001f);
	TestEqual(TEXT("and so does the stat the regeneration step asks"),
		Worn.ASC()->StatForSkill(FName(UCataclysmRegeneration::ShieldRechargeHasNoDelayStat),
								 FGameplayTagContainer(), 0.0f),
		1.0f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLessMinionHealthRowTest,
	"Cataclysm.Enchantments.TheLessMinionHealthRowHalvesAnImpsHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your minions have 20%-50% less hp", worn at 50. Issue #1833, the small engine
 * halves: minion health read the increased bucket only, so a "less" row did
 * nothing. An imp summoned by the wearer has half the health of one summoned
 * by the same kind of summoner wearing nothing that touches minions.
 */
bool FCataclysmLessMinionHealthRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	FWorn Worn(TEXT("Negative_Your_minions_have_20_50_less_hp"), false);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	CataclysmEnchantmentEffectTest::FWearer Plain(Worn.World);
	Plain.Equipment->RefreshAttributes(Plain.AbilitySystem);

	const auto ImpHealth = [&](AActor* Summoner, float Along)
	{
		ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
			Summoner, FVector(Along, 0.0f, 0.0f), /*Lifetime=*/20.0f,
			/*bBurns=*/false, /*TypeName=*/TEXT("Imp"));
		const UAbilitySystemComponent* Its = Imp ? Imp->GetAbilitySystemComponent() : nullptr;
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute())
				   : -1.0f;
	};
	const float Worse = ImpHealth(Worn.Wearer->Actor, 400.0f);
	const float Usual = ImpHealth(Plain.Actor, -400.0f);
	if (!TestTrue(TEXT("both imps have health"), Worse > 0.0f && Usual > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the wearer's imp has half the health"), Worse / Usual, 0.5f, 0.0001f);
	return true;
}

namespace CataclysmCooldownResetTest
{
	/**
	 * A bare wearer in its own world, carrying one real cooldown reset
	 * enchantment on a helm beside a drawback with no effect row, with every
	 * slot on a thirty-second cooldown. Issue #1833, the cooldown reset action.
	 * A worn item rolls the top of its range, which for a chance row is the top
	 * chance.
	 *
	 * THE COOLDOWNS ARE BUILT AS `UCataclysmGameplayAbility::ApplyCooldown`
	 * BUILDS THEM: a duration effect granting the slot's `Cooldown.*` tag. The
	 * test world runs no timers, so none runs out by itself.
	 */
	struct FWorn
	{
		explicit FWorn(const TCHAR* Enchantment)
		{
			using namespace CataclysmEnchantmentEffectTest;
			World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
			if (!World)
			{
				return;
			}
			Wearer = MakeUnique<FWearer>(World);
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
			Wearer->Equipment->Equip(
				Carrying(TEXT("Head_Helm"), Enchantment, DrawbackWithNoEffect),
				Removed, AlsoRemoved, Slot);
			Wearer->Equipment->RefreshAttributes(Wearer->AbilitySystem);
			for (const ECataclysmAbilitySlot Each : CataclysmAbilitySlots::All())
			{
				PutOnCooldown(Each);
			}
		}

		~FWorn()
		{
			Wearer.Reset();
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		UCataclysmAbilitySystemComponent* ASC() const
		{
			return Wearer ? Wearer->AbilitySystem : nullptr;
		}

		void PutOnCooldown(ECataclysmAbilitySlot Slot) const
		{
			const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(Slot);
			if (!Tag.IsValid() || ASC()->HasMatchingGameplayTag(Tag))
			{
				return;
			}
			UGameplayEffect* Effect = NewObject<UGameplayEffect>(
				GetTransientPackage(),
				MakeUniqueObjectName(GetTransientPackage(), UGameplayEffect::StaticClass(),
									 FName(TEXT("TestCooldown"))));
			Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
			Effect->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(30.0f));
			FInheritedTagContainer Granted;
			Granted.Added.AddTag(Tag);
			Effect->FindOrAddComponent<UTargetTagsGameplayEffectComponent>()
				.SetAndApplyTargetTagChanges(Granted);
			ASC()->ApplyGameplayEffectToSelf(Effect, 1.0f, ASC()->MakeEffectContext());
		}

		bool Waiting(ECataclysmAbilitySlot Slot) const
		{
			return ASC()->HasMatchingGameplayTag(UCataclysmSkillSlots::CooldownTag(Slot));
		}

		UWorld* World = nullptr;
		TUniquePtr<CataclysmEnchantmentEffectTest::FWearer> Wearer;
	};

	FGameplayTagContainer TagsOf(std::initializer_list<FGameplayTag> Tags)
	{
		FGameplayTagContainer Out;
		for (const FGameplayTag& Tag : Tags)
		{
			Out.AddTag(Tag);
		}
		return Out;
	}

	/** Pins `Cataclysm.CooldownResetRoll` for the life of this object. */
	struct FPinnedRoll
	{
		explicit FPinnedRoll(float Roll)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(
				TEXT("Cataclysm.CooldownResetRoll"));
			Set(Roll);
		}
		~FPinnedRoll()
		{
			Set(-1.0f);
		}
		void Set(float Roll) const
		{
			if (Variable)
			{
				Variable->Set(Roll, ECVF_SetByCode);
			}
		}
		IConsoleVariable* Variable = nullptr;
	};

	using ESlot = ECataclysmAbilitySlot;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCooldownResetOnKillAndUltimateTest,
	"Cataclysm.Enchantments.TheKillAndUltimateResetRowsClearTheCooldownsTheyName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your special ability cooldown is reset when you kill an enemy": a kill clears
 * the special slot and no other. "Using your ultimate ability resets all other
 * skill cooldowns": using the ultimate clears every slot but the ultimate, ruled
 * 2026-09-25, and using the heavy attack clears nothing. Issue #1833, the
 * cooldown reset action.
 */
bool FCataclysmCooldownResetOnKillAndUltimateTest::RunTest(const FString&)
{
	using namespace CataclysmCooldownResetTest;
	{
		FWorn Worn(TEXT("Positive_Your_special_ability_cooldown_is_reset_when_you"));
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
			|| !TestTrue(TEXT("special starts waiting"), Worn.Waiting(ESlot::Special)))
		{
			return false;
		}
		Worn.ASC()->ActOnEvent(FName(TEXT("kill")));
		TestFalse(TEXT("kill: special is ready"), Worn.Waiting(ESlot::Special));
		TestTrue(TEXT("kill: heavy still waits"), Worn.Waiting(ESlot::Heavy));
		TestTrue(TEXT("kill: the ultimate still waits"), Worn.Waiting(ESlot::Ultimate));
	}
	{
		FWorn Worn(TEXT("Positive_Using_your_ultimate_ability_resets_all_other_ski"));
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		const FGameplayTagContainer Heavy = TagsOf({CataclysmAbilitySlots::Tag(ESlot::Heavy)});
		Worn.ASC()->ActOnEvent(FName(TEXT("skill_use")), &Heavy);
		TestTrue(TEXT("a heavy attack used: heavy still waits"), Worn.Waiting(ESlot::Heavy));
		TestTrue(TEXT("a heavy attack used: special still waits"), Worn.Waiting(ESlot::Special));

		const FGameplayTagContainer Ultimate = TagsOf({CataclysmAbilitySlots::Tag(ESlot::Ultimate)});
		Worn.ASC()->ActOnEvent(FName(TEXT("skill_use")), &Ultimate);
		TestTrue(TEXT("the ultimate used: the ultimate still waits"), Worn.Waiting(ESlot::Ultimate));
		for (const ESlot Slot : {ESlot::Heavy, ESlot::Special, ESlot::Support,
								 ESlot::Aura, ESlot::Movement})
		{
			TestFalse(FString::Printf(TEXT("the ultimate used: slot %d is ready"),
									  static_cast<int32>(Slot)),
				Worn.Waiting(Slot));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCooldownResetEvery20SecondsTest,
	"Cataclysm.Enchantments.TheEvery20SecondsResetRowClearsEverySlotAt20SecondsOfCombat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Every 20 seconds all your skill cooldowns are instantly reset": nothing at 19
 * seconds of combat, every slot at 20, the ultimate included, ruled 2026-09-25.
 * The clock counts only in combat, ruled 2026-09-24. Issue #1833, the cooldown
 * reset action.
 */
bool FCataclysmCooldownResetEvery20SecondsTest::RunTest(const FString&)
{
	using namespace CataclysmCooldownResetTest;
	FWorn Worn(TEXT("Positive_Every_20_seconds_all_your_skill_cooldowns_are_in"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const float Began = Worn.World->TimeSeconds;
	Worn.ASC()->NoteHitDealt();
	const auto Until = [&](float Seconds)
	{
		while (Worn.World->TimeSeconds < Began + Seconds - 0.001f)
		{
			Worn.World->TimeSeconds += 1.0f;
			Worn.ASC()->NoteHitDealt();
			Worn.ASC()->StepTimedGrants();
		}
	};
	Until(19.0f);
	TestTrue(TEXT("nineteen seconds in: the ultimate still waits"), Worn.Waiting(ESlot::Ultimate));
	TestTrue(TEXT("nineteen seconds in: heavy still waits"), Worn.Waiting(ESlot::Heavy));
	Until(20.0f);
	for (const ESlot Slot : CataclysmAbilitySlots::All())
	{
		TestFalse(FString::Printf(TEXT("twenty seconds in: slot %d is ready"),
								  static_cast<int32>(Slot)),
			Worn.Waiting(Slot));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCooldownResetChanceRowsTest,
	"Cataclysm.Enchantments.TheChanceResetRowsResetBelowTheirChanceAndNotAbove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Three chance rows, each at the top of its range, with the roll pinned. Issue
 * #1833, the cooldown reset action.
 *
 * "Blocking an attack has a 20%-40% chance to reset your heavy attack cooldown"
 * at 40, "Critical strikes have a 15%-30% chance to reset your movement ability
 * cooldown" at 30 and "Dodging an attack has a 20%-40% chance to reset your
 * movement ability cooldown" at 40. A roll of 41 is above all three and resets
 * nothing; a roll of 19 is below all three and resets the slot each names, and
 * only that slot.
 */
bool FCataclysmCooldownResetChanceRowsTest::RunTest(const FString&)
{
	using namespace CataclysmCooldownResetTest;
	struct FCase
	{
		const TCHAR* Enchantment;
		const TCHAR* Event;
		ESlot Resets;
		ESlot Other;
	};
	const FCase Cases[] = {
		{TEXT("Positive_Blocking_an_attack_has_a_20_40_chance_to_reset"),
		 TEXT("block"), ESlot::Heavy, ESlot::Movement},
		{TEXT("Positive_Critical_strikes_have_a_15_30_chance_to_reset"),
		 TEXT("critical_strike"), ESlot::Movement, ESlot::Heavy},
		{TEXT("Positive_Dodging_an_attack_has_a_20_40_chance_to_reset"),
		 TEXT("dodge"), ESlot::Movement, ESlot::Heavy},
	};
	for (const FCase& Case : Cases)
	{
		FWorn Worn(Case.Enchantment);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		FPinnedRoll Roll(41.0f);
		if (!TestNotNull(TEXT("the roll can be pinned"), Roll.Variable))
		{
			return false;
		}
		Worn.ASC()->ActOnEvent(FName(Case.Event));
		TestTrue(FString::Printf(TEXT("%s, rolled 41: still waiting"), Case.Event),
			Worn.Waiting(Case.Resets));
		Roll.Set(19.0f);
		Worn.ASC()->ActOnEvent(FName(Case.Event));
		TestFalse(FString::Printf(TEXT("%s, rolled 19: ready"), Case.Event),
			Worn.Waiting(Case.Resets));
		TestTrue(FString::Printf(TEXT("%s, rolled 19: the other slot still waits"), Case.Event),
			Worn.Waiting(Case.Other));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCooldownResetStaggeredHitTest,
	"Cataclysm.Enchantments.TheStaggeredHitResetRowResetsHeavyOnlyOnAStaggeredEnemy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Hitting a staggered enemy resets your heavy attack cooldown": a hit on an
 * enemy that is not staggered clears nothing, and a hit on one that is clears
 * heavy. The condition sees the enemy struck because a pool action's condition
 * is now judged against the event's other character. Issue #1833, the cooldown
 * reset action.
 */
bool FCataclysmCooldownResetStaggeredHitTest::RunTest(const FString&)
{
	using namespace CataclysmCooldownResetTest;
	FWorn Worn(TEXT("Positive_Hitting_a_staggered_enemy_resets_your_heavy_atta"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	CataclysmEnchantmentEffectTest::FWearer Enemy(Worn.World);
	const FGameplayTag Staggered = UCataclysmSkillEffects::StaggeredTag();
	if (!TestTrue(TEXT("the stagger tag is in the vocabulary"), Staggered.IsValid()))
	{
		return false;
	}

	Worn.ASC()->ActOnEvent(FName(TEXT("hit_dealt")), nullptr, 0.0f, true, Enemy.Actor);
	TestTrue(TEXT("a hit on an enemy not staggered: heavy still waits"),
		Worn.Waiting(ESlot::Heavy));

	Enemy.AbilitySystem->AddLooseGameplayTag(Staggered);
	if (!TestTrue(TEXT("the enemy reads as staggered"),
			UCataclysmSkillEffects::IsStaggered(Enemy.Actor)))
	{
		return false;
	}
	Worn.ASC()->ActOnEvent(FName(TEXT("hit_dealt")), nullptr, 0.0f, true, Enemy.Actor);
	TestFalse(TEXT("a hit on a staggered enemy: heavy is ready"), Worn.Waiting(ESlot::Heavy));
	TestTrue(TEXT("and special still waits"), Worn.Waiting(ESlot::Special));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCooldownResetRangedKillTest,
	"Cataclysm.Enchantments.TheRangedKillResetRowRefundsTheKillingSkillsOwnSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Ranged kills have a 20%-40% chance to refund the skill cooldown", at 40, with
 * the roll pinned at 19: a ranged kill by a skill in the special slot clears
 * special and no other, and a kill by a skill that is not ranged clears nothing.
 * "Refund" is a reset, ruled 2026-09-25. Issue #1833, the cooldown reset action.
 */
bool FCataclysmCooldownResetRangedKillTest::RunTest(const FString&)
{
	using namespace CataclysmCooldownResetTest;
	FWorn Worn(TEXT("Positive_Ranged_kills_have_a_20_40_chance_to_refund_the"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	FPinnedRoll Roll(19.0f);
	const FGameplayTag Ranged = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Type.Ranged")), /*ErrorIfNotFound=*/false);
	const FGameplayTag Special = CataclysmAbilitySlots::Tag(ESlot::Special);
	if (!TestTrue(TEXT("the tags are in the vocabulary"), Ranged.IsValid() && Special.IsValid()))
	{
		return false;
	}

	const FGameplayTagContainer NotRanged = TagsOf({Special});
	Worn.ASC()->ActOnEvent(FName(TEXT("kill")), &NotRanged);
	TestTrue(TEXT("a kill that is not ranged: special still waits"), Worn.Waiting(ESlot::Special));

	const FGameplayTagContainer RangedSpecial = TagsOf({Ranged, Special});
	Worn.ASC()->ActOnEvent(FName(TEXT("kill")), &RangedSpecial);
	TestFalse(TEXT("a ranged kill by the special slot: special is ready"),
		Worn.Waiting(ESlot::Special));
	TestTrue(TEXT("and heavy still waits"), Worn.Waiting(ESlot::Heavy));
	return true;
}

namespace CataclysmCooldownReduceTest
{
	/** The time left on one slot's cooldown, or 0 when it is not cooling down. */
	float SecondsLeft(const UCataclysmAbilitySystemComponent* ASC, ECataclysmAbilitySlot Slot)
	{
		const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(Slot);
		float Most = 0.0f;
		for (const float Left : ASC->GetActiveEffectsTimeRemaining(
				 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag))))
		{
			Most = FMath::Max(Most, Left);
		}
		return Most;
	}

	using ESlot = ECataclysmAbilitySlot;

	/**
	 * A bare wearer carrying one real enchantment, with two real strike skills
	 * granted: a spell in the special slot, tagged `Type.Spell`, and a heavy
	 * attack that is not a spell. Each has a stated ten-second cooldown and
	 * nothing starts cooling down. Issue #1833, the cooldown reduction action.
	 */
	struct FSpellcaster
	{
		explicit FSpellcaster(const TCHAR* Enchantment)
		{
			using namespace CataclysmEnchantmentEffectTest;
			World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
			if (!World)
			{
				return;
			}
			Wearer = MakeUnique<FWearer>(World);
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot GearSlot = ECataclysmGearSlot::Count;
			Wearer->Equipment->Equip(
				Carrying(TEXT("Head_Helm"), Enchantment, DrawbackWithNoEffect),
				Removed, AlsoRemoved, GearSlot);
			Wearer->Equipment->RefreshAttributes(Wearer->AbilitySystem);
			Wearer->AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 100000.0f);
			Wearer->AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetManaAttribute(), 100000.0f);
			Spell = Grant(ESlot::Special, TEXT("Type.Spell"));
			Heavy = Grant(ESlot::Heavy, FString());
		}

		~FSpellcaster()
		{
			Wearer.Reset();
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		UCataclysmStrikeSkill* Grant(ESlot Slot, const FString& Tags) const
		{
			const FGameplayAbilitySpecHandle Handle = Wearer->AbilitySystem->GiveAbilityInSlot(
				UCataclysmStrikeSkill::StaticClass(), Slot, /*Level=*/1, Wearer->Actor);
			FGameplayAbilitySpec* Spec = Handle.IsValid()
				? Wearer->AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
			UCataclysmStrikeSkill* Skill =
				Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (Skill)
			{
				Skill->SkillName = TEXT("Test Strike");
				Skill->Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=2"));
				Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(Tags);
				Skill->CooldownOverride = 10.0f;
			}
			return Skill;
		}

		bool Ready() const
		{
			return Wearer && Spell && Heavy;
		}

		bool Use(UGameplayAbility* Skill) const
		{
			return Skill && Wearer->AbilitySystem->TryActivateAbility(
				Skill->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
		}

		void Clear(ESlot Slot) const
		{
			Wearer->AbilitySystem->RemoveActiveEffectsWithGrantedTags(
				FGameplayTagContainer(UCataclysmSkillSlots::CooldownTag(Slot)));
		}

		UCataclysmAbilitySystemComponent* ASC() const
		{
			return Wearer ? Wearer->AbilitySystem : nullptr;
		}

		UWorld* World = nullptr;
		TUniquePtr<CataclysmEnchantmentEffectTest::FWearer> Wearer;
		UCataclysmStrikeSkill* Spell = nullptr;
		UCataclysmStrikeSkill* Heavy = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCooldownReduceAllRowsTest,
	"Cataclysm.Enchantments.TheReduceAllRowsTakeTheirSecondsOffEveryRunningCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two rows that take seconds off every running cooldown, each worn alone with
 * every slot that has a cooldown on a thirty-second one; the aura, a toggle,
 * has none by design and is checked to be left alone. Issue #1833, the cooldown reduction
 * action. "When your class resource hits zero, all skill cooldowns are reduced
 * by 2-4 seconds" at 4 leaves 26 on every slot, the ultimate included, ruled
 * 2026-09-25; "Each summon reduces all your skill cooldowns by 1-2 seconds" at 2
 * leaves 28.
 */
bool FCataclysmCooldownReduceAllRowsTest::RunTest(const FString&)
{
	using namespace CataclysmCooldownReduceTest;
	struct FCase
	{
		const TCHAR* Enchantment;
		const TCHAR* Event;
		float Left;
	};
	const FCase Cases[] = {
		{TEXT("Positive_When_your_class_resource_hits_zero_all_skill_co"),
		 TEXT("resource_empty"), 26.0f},
		{TEXT("Positive_Each_summon_reduces_all_your_skill_cooldowns_by"),
		 TEXT("summon"), 28.0f},
	};
	for (const FCase& Case : Cases)
	{
		CataclysmCooldownResetTest::FWorn Worn(Case.Enchantment);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
			|| !TestEqual(TEXT("the ultimate starts at thirty"),
					SecondsLeft(Worn.ASC(), ESlot::Ultimate), 30.0f, 0.01f))
		{
			return false;
		}
		Worn.ASC()->ActOnEvent(FName(Case.Event));
		for (const ESlot Slot : {ESlot::Heavy, ESlot::Special, ESlot::Support,
								 ESlot::Ultimate, ESlot::Movement})
		{
			TestEqual(FString::Printf(TEXT("%s: slot %d has %.0f left"), Case.Event,
									  static_cast<int32>(Slot), Case.Left),
				SecondsLeft(Worn.ASC(), Slot), Case.Left, 0.01f);
		}
		// THE AURA IS LEFT ALONE: it is a toggle with no cooldown tag by design
		// (`UCataclysmSkillSlots::CooldownTag`), so the row has nothing on it
		// to shorten and places nothing on it either.
		TestFalse(FString::Printf(TEXT("%s: the aura slot has no cooldown tag"), Case.Event),
			UCataclysmSkillSlots::CooldownTag(ESlot::Aura).IsValid());
		TestEqual(FString::Printf(TEXT("%s: the aura slot is not cooling down"), Case.Event),
			SecondsLeft(Worn.ASC(), ESlot::Aura), 0.0f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCooldownReduceHeavyRowTest,
	"Cataclysm.Enchantments.TheCritHeavyReduceRowTakesItsSecondsOffHeavyOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your heavy attack cooldown is reduced by 0.5-1.5 seconds each time you land a
 * critical strike", at 1.5: a critical strike, by any skill, leaves heavy at
 * 28.5 and special at 30. Issue #1833, the cooldown reduction action.
 */
bool FCataclysmCooldownReduceHeavyRowTest::RunTest(const FString&)
{
	using namespace CataclysmCooldownReduceTest;
	CataclysmCooldownResetTest::FWorn Worn(
		TEXT("Positive_Your_heavy_attack_cooldown_is_reduced_by_0_5_1_5"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	Worn.ASC()->ActOnEvent(FName(TEXT("critical_strike")));
	TestEqual(TEXT("heavy has 28.5 left"), SecondsLeft(Worn.ASC(), ESlot::Heavy), 28.5f, 0.01f);
	TestEqual(TEXT("special still has 30"), SecondsLeft(Worn.ASC(), ESlot::Special), 30.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCooldownReduceOverflowTest,
	"Cataclysm.Enchantments.AReductionLargerThanTheTimeLeftEndsTheCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Overflow ends a cooldown and carries nothing over, ruled 2026-09-25. Issue
 * #1833, the cooldown reduction action. Worn at 4, the class-resource row takes
 * a thirty-second cooldown to 2 after seven reductions, and the eighth, larger
 * than what is left, ends it.
 */
bool FCataclysmCooldownReduceOverflowTest::RunTest(const FString&)
{
	using namespace CataclysmCooldownReduceTest;
	CataclysmCooldownResetTest::FWorn Worn(
		TEXT("Positive_When_your_class_resource_hits_zero_all_skill_co"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	for (int32 Time = 0; Time < 7; ++Time)
	{
		Worn.ASC()->ActOnEvent(FName(TEXT("resource_empty")));
	}
	TestEqual(TEXT("seven reductions of 4: 2 left"),
		SecondsLeft(Worn.ASC(), ESlot::Heavy), 2.0f, 0.01f);
	Worn.ASC()->ActOnEvent(FName(TEXT("resource_empty")));
	TestFalse(TEXT("the eighth ends it"), Worn.Waiting(ESlot::Heavy));
	TestEqual(TEXT("and nothing is left on it"),
		SecondsLeft(Worn.ASC(), ESlot::Heavy), 0.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNextSpellCooldownRowTest,
	"Cataclysm.Enchantments.TheNextSpellCooldownRowShortensOnlyTheNextSpellsCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Each spell cast reduces your next spell cooldown by 0.5-1.5 seconds", at 1.5,
 * with real skills granted and used. Issue #1833, the cooldown reduction action.
 *
 * A spell's first cast starts its full ten seconds, because the charge it
 * grants is granted after its own cooldown. It then holds one charge, and a
 * second spell event still holds one, the cap of 1. A heavy attack that is not
 * a spell starts its full ten and leaves the charge. The next spell starts 8.5.
 *
 * "SPELL" FIRES ONLY FOR A SKILL TAGGED `Type.Spell`, which today is only the
 * Demonic caster skills, so the row does nothing for a character without one.
 */
bool FCataclysmNextSpellCooldownRowTest::RunTest(const FString&)
{
	using namespace CataclysmCooldownReduceTest;
	FSpellcaster Caster(
		TEXT("Positive_Each_spell_cast_reduces_your_next_spell_cooldown"));
	if (!TestTrue(TEXT("a wearer with a spell and a heavy attack"), Caster.Ready()))
	{
		return false;
	}
	UCataclysmAbilitySystemComponent* ASC = Caster.ASC();

	TestTrue(TEXT("the spell is used"), Caster.Use(Caster.Spell));
	TestEqual(TEXT("the first spell starts its full ten"),
		SecondsLeft(ASC, ESlot::Special), 10.0f, 0.01f);
	TestEqual(TEXT("and one charge of 1.5 is held"), ASC->NextSpellCooldownSecondsHeld(), 1.5f, 0.001f);

	ASC->ActOnEvent(FName(TEXT("spell")));
	TestEqual(TEXT("a second spell event still holds one"),
		ASC->NextSpellCooldownSecondsHeld(), 1.5f, 0.001f);

	TestTrue(TEXT("the heavy attack is used"), Caster.Use(Caster.Heavy));
	TestEqual(TEXT("a heavy attack that is no spell starts its full ten"),
		SecondsLeft(ASC, ESlot::Heavy), 10.0f, 0.01f);
	TestEqual(TEXT("and leaves the charge"), ASC->NextSpellCooldownSecondsHeld(), 1.5f, 0.001f);

	Caster.Clear(ESlot::Special);
	TestTrue(TEXT("the spell is used again"), Caster.Use(Caster.Spell));
	TestEqual(TEXT("the next spell starts 8.5"),
		SecondsLeft(ASC, ESlot::Special), 8.5f, 0.01f);
	return true;
}

namespace CataclysmDeployableTest
{
	/**
	 * A summoner and what its machines and imps do. Issue #1833, deployable Part
	 * 1. The summoner is a bare wearer carrying one real enchantment, or nothing
	 * that touches minions; `Blow` spawns one minion of a type and has it strike a
	 * fresh creature with no armour, evasion, block or resistance, returning the
	 * health it took. A worn item rolls the top of its range.
	 */
	struct FSummoner
	{
		explicit FSummoner(UWorld* InWorld, const TCHAR* Enchantment,
						   const TCHAR* Drawback = nullptr)
			: World(InWorld)
		{
			using namespace CataclysmEnchantmentEffectTest;
			Wearer = MakeUnique<FWearer>(World);
			if (Enchantment)
			{
				FCataclysmItem Removed;
				FCataclysmItem AlsoRemoved;
				ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
				Wearer->Equipment->Equip(
					Carrying(TEXT("Head_Helm"), Enchantment,
							 Drawback ? Drawback : DrawbackWithNoEffect),
					Removed, AlsoRemoved, Slot);
			}
			Wearer->Equipment->RefreshAttributes(Wearer->AbilitySystem);
		}

		ACataclysmMinion* Make(const TCHAR* Type)
		{
			Along += 400.0f;
			return ACataclysmMinion::Spawn(Wearer->Actor, FVector(Along, 0.0f, 0.0f),
										   /*Lifetime=*/20.0f, /*bBurns=*/false, Type);
		}

		float Blow(const TCHAR* Type)
		{
			ACataclysmMinion* Minion = Make(Type);
			ACataclysmEnemyCharacter* Victim = World->SpawnActor<ACataclysmEnemyCharacter>(
				FVector(Along, 150.0f, 0.0f), FRotator::ZeroRotator);
			if (!Minion || !Victim)
			{
				return -1.0f;
			}
			Victim->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Victim->SetHealth(10000.0f);
			Victim->SetArmour(0.0f);
			UAbilitySystemComponent* Its = Victim->GetAbilitySystemComponent();
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Its->SetNumericAttributeBase(
				UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
			const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
			const float Before = Its->GetNumericAttribute(Health);
			Minion->AttackTarget(Victim);
			return Before - Its->GetNumericAttribute(Health);
		}

		float MaxHealthOf(const TCHAR* Type)
		{
			ACataclysmMinion* Minion = Make(Type);
			const UAbilitySystemComponent* Its = Minion ? Minion->GetAbilitySystemComponent() : nullptr;
			return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute())
					   : -1.0f;
		}

		float LifeOf(const TCHAR* Type)
		{
			ACataclysmMinion* Minion = Make(Type);
			return Minion ? Minion->GetLifeSpan() : -1.0f;
		}

		float IntervalScaleOf(const TCHAR* Type)
		{
			ACataclysmMinion* Minion = Make(Type);
			return Minion ? UCataclysmCommand::AttackIntervalScaleFor(Minion, nullptr) : -1.0f;
		}

		UCataclysmAbilitySystemComponent* ASC() const { return Wearer->AbilitySystem; }

		UWorld* World = nullptr;
		TUniquePtr<CataclysmEnchantmentEffectTest::FWearer> Wearer;
		float Along = 0.0f;
	};

	/** A world for one test, destroyed when the test ends. */
	struct FWorld
	{
		FWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGadgetDamageRowsTest,
	"Cataclysm.Enchantments.TheGadgetDamageRowsReachAMachinesBlowAndNotAnImps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1833, deployable Part 1: a machine's blow reads its summoner's
 * attack_damage rows that name `Type.Deployable`, and an imp's does not.
 *
 * "Gadgets deal 20%-40% increased damage", at 40: a ballista's blow is 1.4 times
 * a plain summoner's ballista, and an imp's is unchanged. The row is written on
 * attack_damage and on spell_damage; reading both would make it 1.8. The same
 * summoner also wears "Your direct damage is reduced by 25%", an attack_damage
 * row that names no gadget, and it does not reach the machine. THAT DOES NOT
 * TEST THE NAMING FILTER: the row has no condition and no scale, so it is
 * folded into the attribute and never recorded as a stat line, and removing the
 * filter changes nothing here (measured 2026-09-26, proof B of deployable Part 1).
 * THE FILTER IS TESTED BY THE NEXT SUMMONER: it wears "While moving, your
 * skills deal 15%-25% less damage", an attack_damage "more" of -25 that names
 * no gadget and has a condition, so it IS recorded as a stat line. Moving, it
 * must still not reach the machine: 1.4 times, where without the filter the
 * blow would be 1.4 x 0.75 = 1.05 times.
 * "While stationary, your gadgets deal 20%-40% increased damage", at 40: 1.4
 * times once the summoner is recorded as not moving.
 * "Gadgets deal bonus damage equal to 3%-6% of your maximum HP per hit", at 6:
 * with the summoner at 1000 maximum health, 60 added to the ballista's own
 * figure before anything multiplies it.
 */
bool FCataclysmGadgetDamageRowsTest::RunTest(const FString&)
{
	using namespace CataclysmDeployableTest;
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	FSummoner Plain(Scope.World, nullptr);
	const float PlainBallista = Plain.Blow(TEXT("Ballista"));
	const float PlainImp = Plain.Blow(TEXT("Imp"));
	if (!TestTrue(TEXT("both plain blows landed"), PlainBallista > 0.0f && PlainImp > 0.0f))
	{
		return false;
	}

	FSummoner Increased(Scope.World, TEXT("Positive_Gadgets_deal_20_40_increased_damage"),
		TEXT("Negative_Your_direct_damage_is_reduced_by_25"));
	TestEqual(TEXT("increased: a ballista's blow is 1.4 times"),
		Increased.Blow(TEXT("Ballista")) / PlainBallista, 1.4f, 0.001f);
	TestEqual(TEXT("increased: an imp's blow is unchanged"),
		Increased.Blow(TEXT("Imp")) / PlainImp, 1.0f, 0.001f);

	// AN UNSCOPED ROW RECORDED AS A STAT LINE, which is what the naming filter
	// keeps off a machine's blow. First that the row is live on the summoner:
	// moving, its own attack_damage is 0.75 times, since the gadget row names
	// Type.Deployable and is not asked here.
	FSummoner Moving(Scope.World, TEXT("Positive_Gadgets_deal_20_40_increased_damage"),
		TEXT("Negative_While_moving_your_skills_deal_15_25_less_damag"));
	Moving.ASC()->NoteMovedMetres(1.0f);
	if (TestEqual(TEXT("moving: the summoner's own attack_damage is 0.75 times"),
			Moving.ASC()->MultiplierForStatAgainst(
				FName(TEXT("attack_damage")), FGameplayTagContainer(), nullptr),
			0.75f, 0.001f))
	{
		TestEqual(TEXT("moving: a ballista's blow is still 1.4 times, the unscoped row "
					   "kept off it (1.05 if it reached it)"),
			Moving.Blow(TEXT("Ballista")) / PlainBallista, 1.4f, 0.001f);
	}

	FSummoner Stationary(Scope.World,
		TEXT("Positive_While_stationary_your_gadgets_deal_20_40_incr"));
	Stationary.ASC()->NoteDidNotMove();
	TestEqual(TEXT("stationary: a ballista's blow is 1.4 times"),
		Stationary.Blow(TEXT("Ballista")) / PlainBallista, 1.4f, 0.001f);

	FSummoner Bonus(Scope.World,
		TEXT("Positive_Gadgets_deal_bonus_damage_equal_to_3_6_of_your"));
	Bonus.ASC()->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
	// AS A RATIO AGAINST THE BALLISTA'S OWN FIGURE, so whatever else scales a
	// blow the same way on both sides cancels: (own + 60) / own.
	ACataclysmMinion* Measured = Plain.Make(TEXT("Ballista"));
	const float Own = Measured ? Measured->OwnDamagePerHit : 0.0f;
	if (TestTrue(TEXT("a ballista has a figure of its own"), Own > 0.0f))
	{
		TestEqual(TEXT("bonus: a ballista's blow carries 60 more at 1000 maximum health"),
			Bonus.Blow(TEXT("Ballista")) / PlainBallista, (Own + 60.0f) / Own, 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGadgetBodyRowsTest,
	"Cataclysm.Enchantments.TheGadgetHealthDurationAndSpeedRowsReachMachinesOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1833, deployable Part 1: three rows scoped to `Type.Deployable` on the
 * summoner's minion stats reach a ballista and not an imp, because a minion is
 * asked with its own type tags.
 *
 * "Gadgets have 40%-70% increased HP" at 70: 1.7 times the maximum health.
 * "Gadgets last 30%-60% longer" at 60: 1.6 times the life span.
 * "Gadgets fire 20%-40% faster" at 40: the attack interval scaled by 1 / 1.4.
 */
bool FCataclysmGadgetBodyRowsTest::RunTest(const FString&)
{
	using namespace CataclysmDeployableTest;
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	FSummoner Plain(Scope.World, nullptr);

	FSummoner Health(Scope.World, TEXT("Positive_Gadgets_have_40_70_increased_HP"));
	TestEqual(TEXT("health: a ballista has 1.7 times"),
		Health.MaxHealthOf(TEXT("Ballista")) / Plain.MaxHealthOf(TEXT("Ballista")), 1.7f, 0.001f);
	TestEqual(TEXT("health: an imp is unchanged"),
		Health.MaxHealthOf(TEXT("Imp")) / Plain.MaxHealthOf(TEXT("Imp")), 1.0f, 0.001f);

	FSummoner Longer(Scope.World, TEXT("Positive_Gadgets_last_30_60_longer"));
	TestEqual(TEXT("duration: a ballista lasts 1.6 times"),
		Longer.LifeOf(TEXT("Ballista")) / Plain.LifeOf(TEXT("Ballista")), 1.6f, 0.001f);
	TestEqual(TEXT("duration: an imp is unchanged"),
		Longer.LifeOf(TEXT("Imp")) / Plain.LifeOf(TEXT("Imp")), 1.0f, 0.001f);

	FSummoner Faster(Scope.World, TEXT("Positive_Gadgets_fire_20_40_faster"));
	TestEqual(TEXT("speed: a ballista's interval is scaled by 1 / 1.4"),
		Faster.IntervalScaleOf(TEXT("Ballista")), 1.0f / 1.4f, 0.001f);
	TestEqual(TEXT("speed: an imp's is unchanged"),
		Faster.IntervalScaleOf(TEXT("Imp")), 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeployableCapRowTest,
	"Cataclysm.Enchantments.TheDeployableCapRowRaisesTheCapOfDeployablesOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1833, the deployable cap. "Add +1-3 to your max deployable count", at
 * 3: minion_cap_bonus 3 for a skill tagged Type.Deployable, and nothing for a
 * summon, whose tags do not name it -- so Summon Imp keeps its cap of 3.
 */
bool FCataclysmDeployableCapRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(
		Carrying(TEXT("Head_Helm"), TEXT("Positive_Add_1_3_to_your_max_deployable_count"),
				 DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(Wearer.AbilitySystem);
	const float Attribute = Wearer.AbilitySystem->GetNumericAttribute(
		UCataclysmCombatAttributeSet::GetMinionCapBonusAttribute());
	const auto Bonus = [&Wearer, Attribute](const TCHAR* Tags)
	{
		return Wearer.AbilitySystem->StatForSkill(
			FName(UCataclysmCommand::MinionCapBonusStat),
			UCataclysmSkillShapes::TagsFromCell(Tags), Attribute);
	};
	TestEqual(TEXT("a deployable skill's cap rises by 3. If not, DT_EnchantmentEffects may "
				   "be older than the rows: run tools/generate_datatable_assets.py"),
		Bonus(TEXT("Type.Deployable, Type.Minion")), 3.0f, 0.001f);
	TestEqual(TEXT("and a summon's by nothing"),
		Bonus(TEXT("Type.Minion, Type.Summon")), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAurasEndAtDeathRowTest,
	"Cataclysm.Enchantments.TheBuffsRemovedOnDeathRowEndsARunningAura",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "When you die all your buffs are removed", worn, ends a running aura when a
 * death is cleared, and a wearer without it keeps its aura. Issue #1833, kept by
 * the owner on 2026-09-25: death already ends every self buff, and leaves an
 * aura running, which is what this drawback changes.
 */
bool FCataclysmAurasEndAtDeathRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const auto AuraRunsAfterDeath = [&](const TCHAR* Drawback, int32& OutEnded) -> int32
	{
		FWearer Wearer(World);
		UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
		Wearer.Equipment->Equip(Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, Drawback),
								Removed, AlsoRemoved, Slot);
		Wearer.Equipment->RefreshAttributes(ASC);

		const FGameplayAbilitySpecHandle Handle = ASC->GiveAbilityInSlot(
			UCataclysmAuraSkill::StaticClass(), ECataclysmAbilitySlot::Aura,
			/*Level=*/100, Wearer.Actor);
		FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
		UCataclysmAuraSkill* Ring = Spec ? Cast<UCataclysmAuraSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Ring)
		{
			return -1;
		}
		Ring->ManaCostOverride = 0.0f;
		if (!ASC->TryActivateAbility(Handle))
		{
			return -1;
		}
		OutEnded = ASC->ClearWhatDeathEnds().AurasEnded;
		const FGameplayAbilitySpec* After = ASC->FindAbilitySpecFromHandle(Handle);
		return After && After->IsActive() ? 1 : 0;
	};

	int32 Ended = -1;
	TestEqual(TEXT("without the drawback the aura still runs after a death"),
		AuraRunsAfterDeath(DrawbackWithNoEffect, Ended), 1);
	TestEqual(TEXT("and no aura was counted as ended"), Ended, 0);

	Ended = -1;
	TestEqual(TEXT("with it the aura has ended"),
		AuraRunsAfterDeath(TEXT("Negative_When_you_die_all_your_buffs_are_removed"), Ended), 0);
	TestEqual(TEXT("and one aura was counted as ended"), Ended, 1);
	return true;
}

namespace CataclysmSkillChargesTest
{
	using ESlot = ECataclysmAbilitySlot;

	/**
	 * A bare wearer carrying one real charges enchantment, with two real strike
	 * skills granted and tagged with their slots as the skill data tags them: a
	 * heavy attack and a special, each with a stated ten-second cooldown. Issue
	 * #1833, skill charges. A worn item rolls the top of its range.
	 */
	struct FCharger
	{
		explicit FCharger(const TCHAR* Enchantment)
		{
			using namespace CataclysmEnchantmentEffectTest;
			World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
			if (!World)
			{
				return;
			}
			Wearer = MakeUnique<FWearer>(World);
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			Wearer->Equipment->Equip(
				Carrying(TEXT("Head_Helm"), Enchantment, DrawbackWithNoEffect),
				Removed, AlsoRemoved, GearSlot);
			Wearer->Equipment->RefreshAttributes(Wearer->AbilitySystem);
			Wearer->AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 100000.0f);
			Wearer->AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetManaAttribute(), 100000.0f);
			Heavy = Grant(ESlot::Heavy, TEXT("Slot.Heavy"));
			Special = Grant(ESlot::Special, TEXT("Slot.Special"));
		}

		~FCharger()
		{
			Wearer.Reset();
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		UCataclysmStrikeSkill* Grant(ESlot Slot, const FString& Tags) const
		{
			const FGameplayAbilitySpecHandle Handle = Wearer->AbilitySystem->GiveAbilityInSlot(
				UCataclysmStrikeSkill::StaticClass(), Slot, /*Level=*/1, Wearer->Actor);
			FGameplayAbilitySpec* Spec = Handle.IsValid()
				? Wearer->AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
			UCataclysmStrikeSkill* Skill =
				Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (Skill)
			{
				Skill->SkillName = TEXT("Test Strike");
				Skill->Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=2"));
				Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(Tags);
				Skill->CooldownOverride = 10.0f;
			}
			return Skill;
		}

		bool Ready() const
		{
			return Wearer && Heavy && Special;
		}

		bool Use(UGameplayAbility* Skill) const
		{
			return Skill && Wearer->AbilitySystem->TryActivateAbility(
				Skill->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
		}

		/** Uses held by the heavy attack now. */
		int32 HeavyHeld() const
		{
			return ASC()->SkillChargesHeld(ESlot::Heavy, Heavy->SkillTags);
		}

		/**
		 * The heavy attack's running recharge ends, as it does when its time is
		 * up: the effect goes, and nothing refilled the charges first.
		 */
		void RechargeHeavy() const
		{
			Wearer->AbilitySystem->RemoveActiveEffectsWithGrantedTags(
				FGameplayTagContainer(UCataclysmSkillSlots::CooldownTag(ESlot::Heavy)));
		}

		/** Take the enchanted item off, and the rows with it. */
		void TakeOff() const
		{
			FCataclysmItem Removed;
			Wearer->Equipment->Unequip(GearSlot, Removed);
			Wearer->Equipment->RefreshAttributes(Wearer->AbilitySystem);
		}

		UCataclysmAbilitySystemComponent* ASC() const
		{
			return Wearer ? Wearer->AbilitySystem : nullptr;
		}

		float HeavyLeft() const
		{
			return CataclysmCooldownReduceTest::SecondsLeft(ASC(), ESlot::Heavy);
		}

		UWorld* World = nullptr;
		TUniquePtr<CataclysmEnchantmentEffectTest::FWearer> Wearer;
		ECataclysmGearSlot GearSlot = ECataclysmGearSlot::Count;
		UCataclysmStrikeSkill* Heavy = nullptr;
		UCataclysmStrikeSkill* Special = nullptr;
	};

	const TCHAR* HeavyCharges =
		TEXT("Positive_Your_heavy_attack_has_1_2_additional_charges");

	const TCHAR* StaleAsset =
		TEXT("the row adds its charges. If not, DT_EnchantmentEffects may be "
			 "older than the rows: run tools/generate_datatable_assets.py");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHeavyChargesRowTest,
	"Cataclysm.Enchantments.TheHeavyChargesRowGivesThreeUsesThatRechargeOneAtATime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your heavy attack has 1-2 additional charges", at 2: the heavy attack is used
 * three times running and refused the fourth, while the special beside it holds
 * its one. Issue #1833, skill charges.
 *
 * ONE RECHARGE RUNS AT A TIME, ruled 2026-09-25 under the owner's delegation.
 * The second and third uses start no clock of their own, so ten seconds are
 * left after all three; each recharge that ends returns one use and starts the
 * next ten, and the last returns the third and starts nothing.
 */
bool FCataclysmHeavyChargesRowTest::RunTest(const FString&)
{
	using namespace CataclysmSkillChargesTest;
	FCharger Charger(HeavyCharges);
	if (!TestTrue(TEXT("a wearer with a heavy attack and a special"), Charger.Ready()))
	{
		return false;
	}
	if (!TestEqual(StaleAsset, Charger.HeavyHeld(), 3))
	{
		return false;
	}

	TestTrue(TEXT("the first use"), Charger.Use(Charger.Heavy));
	TestEqual(TEXT("which starts ten seconds"), Charger.HeavyLeft(), 10.0f, 0.01f);
	TestTrue(TEXT("the second use, while that runs"), Charger.Use(Charger.Heavy));
	TestTrue(TEXT("the third"), Charger.Use(Charger.Heavy));
	TestFalse(TEXT("and the fourth is refused"), Charger.Use(Charger.Heavy));
	TestEqual(TEXT("nothing held"), Charger.HeavyHeld(), 0);
	TestEqual(TEXT("and still one ten-second recharge, not restarted"),
		Charger.HeavyLeft(), 10.0f, 0.01f);

	TestTrue(TEXT("the special is used once"), Charger.Use(Charger.Special));
	TestFalse(TEXT("and refused the second time: the row names the heavy slot"),
		Charger.Use(Charger.Special));

	Charger.RechargeHeavy();
	TestEqual(TEXT("one recharge ends: one use back"), Charger.HeavyHeld(), 1);
	TestEqual(TEXT("and the next ten seconds start"), Charger.HeavyLeft(), 10.0f, 0.01f);
	Charger.RechargeHeavy();
	TestEqual(TEXT("two back"), Charger.HeavyHeld(), 2);
	TestEqual(TEXT("and the last ten start"), Charger.HeavyLeft(), 10.0f, 0.01f);
	Charger.RechargeHeavy();
	TestEqual(TEXT("all three back"), Charger.HeavyHeld(), 3);
	TestEqual(TEXT("and no recharge runs"), Charger.HeavyLeft(), 0.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChargeRowsReachTheirSlotsTest,
	"Cataclysm.Enchantments.EachChargesRowAddsUsesToTheSkillsItNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The other five charges rows, each worn alone at the top of its range, asked
 * for the most uses a skill of each slot holds. Issue #1833, skill charges.
 *
 * "Ultimate has 1-3 additional charges": the ultimate holds 4. "Gain 1-3
 * additional charges for your cooldown abilities": every slot holds 4. "Your
 * special ability ..." and "Your movement ability ... has 1-2 additional
 * charges": 3 in that slot. Every other slot holds 1.
 *
 * "Your movement ability has 2 charges": 2 in the Movement slot, the one a
 * skill always has and one more. Issue #1833, 2026-10-05: the sentence was
 * reworded on 2026-09-30 and had no effect row until then.
 *
 * "Skills have 1-2 additional charges when fighting Boss enemies": 1 everywhere
 * until a Boss is struck, then 3 everywhere -- within four seconds of striking
 * one, the owner's boss clock of 2026-09-18.
 */
bool FCataclysmChargeRowsReachTheirSlotsTest::RunTest(const FString&)
{
	using namespace CataclysmSkillChargesTest;
	const ESlot Slots[] = {ESlot::Heavy, ESlot::Special, ESlot::Support,
						   ESlot::Ultimate, ESlot::Movement};
	const auto TagsOf = [](ESlot Slot)
	{
		return FGameplayTagContainer(CataclysmAbilitySlots::Tag(Slot));
	};
	struct FCase
	{
		const TCHAR* Enchantment;
		ESlot Named;
		int32 InNamed;
	};
	const FCase Cases[] = {
		{TEXT("Positive_Ultimate_has_1_3_additional_charges"), ESlot::Ultimate, 4},
		{TEXT("Positive_Gain_1_3_additional_charges_for_your_cooldown_ab"), ESlot::None, 4},
		{TEXT("Positive_Your_special_ability_has_1_2_additional_charges"), ESlot::Special, 3},
		{TEXT("Positive_Your_movement_ability_has_1_2_additional_charges"), ESlot::Movement, 3},
		{TEXT("Positive_Your_movement_ability_has_2_charges"), ESlot::Movement, 2},
	};
	for (const FCase& Case : Cases)
	{
		FCharger Charger(Case.Enchantment);
		if (!TestNotNull(TEXT("a wearer in a world"), Charger.ASC()))
		{
			return false;
		}
		for (const ESlot Slot : Slots)
		{
			const int32 Expected =
				(Case.Named == ESlot::None || Case.Named == Slot) ? Case.InNamed : 1;
			TestEqual(FString::Printf(TEXT("%s: slot %d holds %d"), Case.Enchantment,
									  static_cast<int32>(Slot), Expected),
				Charger.ASC()->SkillChargesMaximum(TagsOf(Slot)), Expected);
		}
	}

	FCharger Boss(TEXT("Positive_Skills_have_1_2_additional_charges_when_fighting"));
	if (!TestNotNull(TEXT("a wearer in a world"), Boss.ASC()))
	{
		return false;
	}
	for (const ESlot Slot : Slots)
	{
		TestEqual(FString::Printf(TEXT("no Boss struck: slot %d holds 1"),
								  static_cast<int32>(Slot)),
			Boss.ASC()->SkillChargesMaximum(TagsOf(Slot)), 1);
	}
	Boss.ASC()->NoteStruckABoss();
	for (const ESlot Slot : Slots)
	{
		TestEqual(FString::Printf(TEXT("a Boss just struck: slot %d holds 3"),
								  static_cast<int32>(Slot)),
			Boss.ASC()->SkillChargesMaximum(TagsOf(Slot)), 3);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChargeMaximumFallsTest,
	"Cataclysm.Enchantments.AChargeMaximumThatFallsTakesTheUsesAboveIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * All three heavy uses spent, then the item giving two of them taken off: the
 * heavy attack now holds at most one, and it is spent. When the recharge
 * running ends it returns that one and starts nothing, because the two uses the
 * row gave went with the row. Issue #1833, skill charges; the clamp ruled
 * 2026-09-25 under the owner's delegation, for the Boss row's lapsing clock.
 */
bool FCataclysmChargeMaximumFallsTest::RunTest(const FString&)
{
	using namespace CataclysmSkillChargesTest;
	FCharger Charger(HeavyCharges);
	if (!TestTrue(TEXT("a wearer with a heavy attack"), Charger.Ready())
		|| !TestEqual(StaleAsset, Charger.HeavyHeld(), 3))
	{
		return false;
	}
	for (int32 Time = 0; Time < 3; ++Time)
	{
		Charger.Use(Charger.Heavy);
	}
	TestEqual(TEXT("three spent"), Charger.HeavyHeld(), 0);

	Charger.TakeOff();
	TestEqual(TEXT("taken off: the most it holds is one"),
		Charger.ASC()->SkillChargesMaximum(Charger.Heavy->SkillTags), 1);
	TestEqual(TEXT("and none is held"), Charger.HeavyHeld(), 0);
	TestFalse(TEXT("so it is refused"), Charger.Use(Charger.Heavy));

	Charger.RechargeHeavy();
	TestEqual(TEXT("the recharge ends: its one use is back"), Charger.HeavyHeld(), 1);
	TestEqual(TEXT("and no further recharge starts"), Charger.HeavyLeft(), 0.0f, 0.01f);
	TestTrue(TEXT("so it is used"), Charger.Use(Charger.Heavy));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChargesResetDeathReductionTest,
	"Cataclysm.Enchantments.AResetOrADeathRefillsChargesAndAReductionShortensOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * With the heavy charges row worn and two of its three uses spent: a cooldown
 * reset refills all three, and so does a death; a reduction takes its seconds
 * off the recharge running and returns nothing by itself. Issue #1833, skill
 * charges, ruled 2026-09-25 under the owner's delegation.
 */
bool FCataclysmChargesResetDeathReductionTest::RunTest(const FString&)
{
	using namespace CataclysmSkillChargesTest;
	FCharger Charger(HeavyCharges);
	if (!TestTrue(TEXT("a wearer with a heavy attack"), Charger.Ready())
		|| !TestEqual(StaleAsset, Charger.HeavyHeld(), 3))
	{
		return false;
	}
	UCataclysmAbilitySystemComponent* ASC = Charger.ASC();

	Charger.Use(Charger.Heavy);
	Charger.Use(Charger.Heavy);
	TestEqual(TEXT("two spent"), Charger.HeavyHeld(), 1);

	FCataclysmPoolAction Reduce;
	Reduce.CooldownReduce = ECataclysmCooldownReset::Heavy;
	Reduce.Percent = 4.0f;
	ASC->ReduceCooldowns(Reduce);
	TestEqual(TEXT("a reduction of four leaves six on the recharge"),
		Charger.HeavyLeft(), 6.0f, 0.01f);
	TestEqual(TEXT("and returns nothing by itself"), Charger.HeavyHeld(), 1);

	IConsoleVariable* Roll =
		IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.CooldownResetRoll"));
	if (!TestNotNull(TEXT("the reset roll can be pinned"), Roll))
	{
		return false;
	}
	Roll->Set(0.0f, ECVF_SetByCode);
	ON_SCOPE_EXIT { Roll->Set(-1.0f, ECVF_SetByCode); };
	FCataclysmPoolAction Reset;
	Reset.CooldownReset = ECataclysmCooldownReset::Heavy;
	Reset.Percent = 100.0f;
	ASC->RollAndResetCooldowns(Reset, nullptr);
	TestEqual(TEXT("a reset refills all three"), Charger.HeavyHeld(), 3);
	TestEqual(TEXT("and no recharge runs"), Charger.HeavyLeft(), 0.0f, 0.01f);

	Charger.Use(Charger.Heavy);
	Charger.Use(Charger.Heavy);
	TestEqual(TEXT("two spent again"), Charger.HeavyHeld(), 1);
	ASC->ClearWhatDeathEnds();
	TestEqual(TEXT("a death refills all three"), Charger.HeavyHeld(), 3);
	TestEqual(TEXT("and no recharge runs"), Charger.HeavyLeft(), 0.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarChargesTest,
	"Cataclysm.Enchantments.TheSkillBarShowsTheChargesASkillHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The heavy attack's box says how many uses it holds, and the special's, which
 * holds one, says nothing. Issue #1833, skill charges: with a second charge a
 * skill can be used while its cooldown sweep runs, so the sweep alone would call
 * a usable skill waiting.
 */
bool FCataclysmSkillBarChargesTest::RunTest(const FString&)
{
	using namespace CataclysmSkillChargesTest;
	TestEqual(TEXT("one of one says nothing"), UCataclysmSkillBar::ChargesTextFor(1, 1), FString());
	TestEqual(TEXT("two of three says x2"), UCataclysmSkillBar::ChargesTextFor(2, 3),
		FString(TEXT("x2")));
	TestEqual(TEXT("none of three says x0"), UCataclysmSkillBar::ChargesTextFor(0, 3),
		FString(TEXT("x0")));

	FCharger Charger(HeavyCharges);
	if (!TestTrue(TEXT("a wearer with a heavy attack"), Charger.Ready())
		|| !TestEqual(StaleAsset, Charger.HeavyHeld(), 3))
	{
		return false;
	}
	Charger.Use(Charger.Heavy);

	bool bSawHeavy = false;
	bool bSawSpecial = false;
	for (const FCataclysmSkillBarSlot& Box : UCataclysmSkillBar::Read(Charger.Wearer->Actor))
	{
		if (Box.Slot == ESlot::Heavy)
		{
			bSawHeavy = true;
			TestEqual(TEXT("the heavy box holds two"), Box.Charges, 2);
			TestEqual(TEXT("of three"), Box.MaxCharges, 3);
			TestTrue(TEXT("while its recharge runs"), Box.CooldownRemaining > 0.0f);
		}
		else if (Box.Slot == ESlot::Special)
		{
			bSawSpecial = true;
			TestEqual(TEXT("the special box holds one of one"), Box.MaxCharges, 1);
		}
	}
	TestTrue(TEXT("the bar has a heavy box"), bSawHeavy);
	TestTrue(TEXT("and a special box"), bSawSpecial);
	return true;
}

namespace CataclysmKillCounterTest
{
	/** A possessed player character in this world, or null. */
	ACataclysmPlayerCharacter* SpawnPossessedPlayer(UWorld* World)
	{
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
		if (!PlayerState || !Controller || !Character)
		{
			return nullptr;
		}
		Controller->SetPlayerState(PlayerState);
		Controller->Possess(Character);
		return Character;
	}

	/** A hostile body that can be killed. */
	ACataclysmEnemyCharacter* SpawnEnemy(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetRarityStep(0);
			Made->SetHealth(1000.0f);
			Made->SetAttackDamage(0.0f);
		}
		return Made;
	}

	/** A world for one test, destroyed when the test ends. */
	struct FWorld
	{
		FWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/** One flat resistance_cap modifier of `Points`, recorded on `System`. */
	void GrantResistanceCap(UCataclysmAbilitySystemComponent* System, float Points)
	{
		FCataclysmStatModifier Flat;
		Flat.Bucket = ECataclysmStatBucket::Flat;
		Flat.Source = ECataclysmModifierSource::Enchantment;
		Flat.Value = Points;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line =
			Inputs.FindOrAdd(FName(UCataclysmDamageCalculation::ResistanceCapStat));
		Line.Base = 0.0f;
		Line.Modifiers = {Flat};
		System->SetStatInputs(MoveTemp(Inputs));
	}

	const TCHAR* StaleAsset =
		TEXT("the row adds its share. If not, DT_EnchantmentEffects may be "
			 "older than the rows: run tools/generate_datatable_assets.py");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillCounterCountsAPlayersKillTest,
	"Cataclysm.KillCounter.APlayersKillRaisesBothCountsAndTheConditionsReadThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1833, the kill counter. A real blow from a possessed player, then the
 * death announced the way play announces it: the notice names the player as
 * the killer, and this run's count and the character's both read one. The
 * player's ability system reads both counts into its stat conditions; one no
 * player state owns reads -1 for both, which gives a kill row nothing.
 */
bool FCataclysmKillCounterCountsAPlayersKillTest::RunTest(const FString&)
{
	using namespace CataclysmKillCounterTest;
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	ACataclysmPlayerCharacter* Player = SpawnPossessedPlayer(Scope.World);
	ACataclysmPlayerState* State =
		Player ? Player->GetPlayerState<ACataclysmPlayerState>() : nullptr;
	UCataclysmAbilitySystemComponent* ASC =
		State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
	ACataclysmEnemyCharacter* Enemy = SpawnEnemy(Scope.World, FVector(150.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("a possessed player with an ability system"), ASC)
		|| !TestNotNull(TEXT("an enemy"), Enemy))
	{
		return false;
	}

	TestEqual(TEXT("no kill yet this run"), State->GetRunKills(), 0);
	TestEqual(TEXT("and none ever"), State->GetLifetimeKills(), 0);
	TestEqual(TEXT("the conditions read nought this run"), ASC->CurrentConditions().RunKills, 0);

	UCataclysmSkillEffects::ApplyHit(Player, Enemy, 100.0f, FGameplayTagContainer());
	UCataclysmCombatEvents::NoteDeath(Enemy);
	TestEqual(TEXT("the player's kill: one this run"), State->GetRunKills(), 1);
	TestEqual(TEXT("and one ever"), State->GetLifetimeKills(), 1);
	TestEqual(TEXT("the conditions read one this run"), ASC->CurrentConditions().RunKills, 1);
	TestEqual(TEXT("and one ever"), ASC->CurrentConditions().CharacterKills, 1);

	// A SAVED LIFETIME PUT BACK MOVES ONLY THAT COUNT, and a negative is nought.
	State->SetLifetimeKills(100);
	TestEqual(TEXT("a saved lifetime of 100 reads 100"), ASC->CurrentConditions().CharacterKills, 100);
	TestEqual(TEXT("and leaves this run's one"), ASC->CurrentConditions().RunKills, 1);
	State->SetLifetimeKills(-5);
	TestEqual(TEXT("a negative saved count reads nought"), State->GetLifetimeKills(), 0);

	AActor* Creature = Scope.World->SpawnActor<AActor>();
	UCataclysmAbilitySystemComponent* Other =
		Creature ? NewObject<UCataclysmAbilitySystemComponent>(Creature) : nullptr;
	if (TestNotNull(TEXT("an ability system with no player state"), Other))
	{
		Other->RegisterComponent();
		Other->InitAbilityActorInfo(Creature, Creature);
		TestEqual(TEXT("no player state: this run reads -1"), Other->CurrentConditions().RunKills, -1);
		TestEqual(TEXT("and ever reads -1"), Other->CurrentConditions().CharacterKills, -1);
	}

	// THE TWO SCALES BY NAME, AND WHOLE STEPS ONLY.
	ECataclysmStatScale Scale = ECataclysmStatScale::Fixed;
	TestTrue(TEXT("run_kills names the run count"),
		UCataclysmStatPipeline::ScaleNamed(TEXT("run_kills"), Scale)
		&& Scale == ECataclysmStatScale::PerKillThisRun);
	TestTrue(TEXT("character_kills names the character's"),
		UCataclysmStatPipeline::ScaleNamed(TEXT("character_kills"), Scale)
		&& Scale == ECataclysmStatScale::PerKillOfTheCharacter);
	FCataclysmStatModifier PerThousand;
	PerThousand.Value = 0.05f;
	PerThousand.Scale = ECataclysmStatScale::PerKillThisRun;
	PerThousand.ScaleStep = 1000.0f;
	FCataclysmStatConditions Conditions;
	Conditions.RunKills = 2999;
	TestEqual(TEXT("2999 kills are two whole steps"),
		UCataclysmStatPipeline::ScaledValue(PerThousand, Conditions), 0.1f, 0.00001f);
	Conditions.RunKills = -1;
	TestEqual(TEXT("an unknown count is worth nothing"),
		UCataclysmStatPipeline::ScaledValue(PerThousand, Conditions), 0.0f, 0.00001f);

	TestEqual(TEXT("the sheet's header line"),
		UCataclysmCharacterSheetLayout::KillsLine(12, 340),
		FString(TEXT("Kills this run 12   Kills 340")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillCounterIsSavedTest,
	"Cataclysm.KillCounter.TheSaveRecordsCarryBothCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1833, the kill counter. The character record carries the character's
 * count, gathered beside its level; the run record carries this run's. A saved
 * lifetime of 1000 and five kills this run write 1005 and 5.
 */
bool FCataclysmKillCounterIsSavedTest::RunTest(const FString&)
{
	using namespace CataclysmKillCounterTest;
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	ACataclysmPlayerCharacter* Player = SpawnPossessedPlayer(Scope.World);
	ACataclysmPlayerState* State =
		Player ? Player->GetPlayerState<ACataclysmPlayerState>() : nullptr;
	UCataclysmCharacterSave* Character = NewObject<UCataclysmCharacterSave>();
	UCataclysmRunSave* Run = NewObject<UCataclysmRunSave>();
	if (!TestNotNull(TEXT("a possessed player with a player state"), State))
	{
		return false;
	}
	State->SetLifetimeKills(1000);
	for (int32 Kill = 0; Kill < 5; ++Kill)
	{
		State->NoteKill();
	}
	TestTrue(TEXT("the character was gathered"),
		FCataclysmSaveGather::CharacterFrom(*Player, *Character));
	TestEqual(TEXT("the character record holds 1005"), Character->LifetimeKills, 1005);
	TestTrue(TEXT("the run's kills were gathered"),
		FCataclysmSaveGather::RunKillsFrom(*Player, *Run));
	TestEqual(TEXT("the run record holds 5"), Run->RunKills, 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmResistanceCapStatTest,
	"Cataclysm.KillCounter.TheResistanceCapMovesWithItsRowsBetweenNoughtAndNinety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1833: `resistance_cap` moves the 70 a resistance is held to. With no
 * row, 70; +10, 80; +40, 90 and no further, the design's hard ceiling; -80,
 * nought. A blow on a defender at 85 resistance meets 70 without the row and 80
 * with +10, so it deals 0.2 / 0.3 of what it did.
 */
bool FCataclysmResistanceCapStatTest::RunTest(const FString&)
{
	using namespace CataclysmKillCounterTest;
	using namespace CataclysmEnchantmentEffectTest;
	using FCalc = UCataclysmDamageCalculation;
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	FWearer Wearer(Scope.World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	TestEqual(TEXT("nothing to ask: 70"), FCalc::ResistanceCapOf(nullptr), 70.0f, 0.001f);
	TestEqual(TEXT("no row: 70"), FCalc::ResistanceCapOf(ASC), 70.0f, 0.001f);

	// THE BLOW WITHOUT THE ROW, which is the control: 85 held, 70 met.
	const FName Void(TEXT("Void"));
	ASC->SetNumericAttributeBase(FCalc::ResistanceAttributeFor(Void), 85.0f);
	ASC->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);

	// HEALTH ENOUGH THAT NO BLOW IS CLIPPED. `Resolve` reports
	// `DealtToHealth` as the smaller of the damage and the health held, and a
	// bare wearer holds the default 100; at that, 300 under a cap of 70 and 200
	// under 80 both read 100, and the ratio below is 1.0 whatever the cap does.
	// Found by this test's first Unreal run, 2026-09-26.
	ASC->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
	ASC->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
	TestEqual(TEXT("the defender has 10000 health, so no blow is clipped"),
		ASC->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()),
		10000.0f, 0.001f);

	FCataclysmIncomingHit Blow;
	Blow.Damage = 1000.0f;
	Blow.DamageType = Void;
	const auto Taken = [ASC](const FCataclysmIncomingHit& Hit)
	{
		return FCalc::Resolve(Hit, ASC, /*Tier=*/1, /*EvasionRoll=*/100.0f,
							  /*BlockRoll=*/100.0f).DealtToHealth;
	};
	const float AtSeventy = Taken(Blow);
	TestTrue(TEXT("the blow lands at all"), AtSeventy > 0.0f);

	GrantResistanceCap(ASC, 10.0f);
	TestEqual(TEXT("+10: 80"), FCalc::ResistanceCapOf(ASC), 80.0f, 0.001f);
	if (AtSeventy > 0.0f)
	{
		TestEqual(TEXT("and the blow meets 80, not 70: 0.2 / 0.3 of it"),
			Taken(Blow) / AtSeventy, 0.2f / 0.3f, 0.001f);
	}

	GrantResistanceCap(ASC, 40.0f);
	TestEqual(TEXT("+40: 90, the ceiling, not 110"), FCalc::ResistanceCapOf(ASC), 90.0f, 0.001f);
	GrantResistanceCap(ASC, -80.0f);
	TestEqual(TEXT("-80: nought, not -10"), FCalc::ResistanceCapOf(ASC), 0.0f, 0.001f);
	TestEqual(TEXT("under a cap of 80, 85 held meets 80"),
		FCalc::EffectiveResistanceUnderCap(85.0f, 0.0f, 80.0f), 80.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillsThisRunRowsTest,
	"Cataclysm.Enchantments.TheKillsThisRunRowsGrowWithEachThousandKills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1833, the kill counter. Worn at the top of their ranges:
 * "Your damage is increased by 0.01%-0.05% permanently for every 1000 enemies
 * killed this run" adds 0.05% to attack and spell damage for each whole 1000,
 * and "Your maximum HP is increased by 0.01%-0.05% permanently for every 1000
 * enemies killed this run" adds 0.05% to maximum health. 999 kills are
 * nothing; 1000 are one step; 2999 are two.
 */
bool FCataclysmKillsThisRunRowsTest::RunTest(const FString&)
{
	using namespace CataclysmKillCounterTest;
	using namespace CataclysmEnchantmentEffectTest;
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	ACataclysmPlayerCharacter* Player = SpawnPossessedPlayer(Scope.World);
	ACataclysmPlayerState* State =
		Player ? Player->GetPlayerState<ACataclysmPlayerState>() : nullptr;
	UCataclysmAbilitySystemComponent* ASC =
		State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
	UCataclysmEquipmentComponent* Equipment = Player ? Player->GetEquipment() : nullptr;
	if (!TestNotNull(TEXT("a possessed player"), ASC) || !TestNotNull(TEXT("its equipment"), Equipment))
	{
		return false;
	}
	Equipment->UnequipEverything();

	struct FCase
	{
		const TCHAR* Enchantment;
		const TCHAR* Stats[2];
	};
	const FCase Cases[] = {
		{TEXT("Positive_Your_damage_is_increased_by_0_01_0_05_permanen"),
		 {TEXT("attack_damage"), TEXT("spell_damage")}},
		{TEXT("Positive_Your_maximum_HP_is_increased_by_0_01_0_05_perm"),
		 {TEXT("max_health"), nullptr}},
	};
	const FGameplayTagContainer NoTags;
	int32 Killed = 0;
	const auto KillUpTo = [State, &Killed](int32 Count)
	{
		for (; Killed < Count; ++Killed)
		{
			State->NoteKill();
		}
	};
	for (const FCase& Case : Cases)
	{
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
		Equipment->Equip(Carrying(TEXT("Head_Helm"), Case.Enchantment, DrawbackWithNoEffect),
						 Removed, AlsoRemoved, Slot);
		Equipment->RefreshAttributes(ASC);

		// EVERY STAT AT THE SAME COUNTS, from the kills already made, since a
		// count only rises: one short of the next thousand, the thousand, and
		// two thousand more less one.
		const int32 Next = (State->GetRunKills() / 1000 + 1) * 1000;
		TMap<FName, float> Before;
		for (const TCHAR* Stat : Case.Stats)
		{
			if (Stat)
			{
				Before.Add(FName(Stat), ASC->IncreasesForStat(FName(Stat), NoTags));
			}
		}
		const auto Check = [this, ASC, &NoTags, &Before](const TCHAR* When, float Added)
		{
			for (const TPair<FName, float>& Each : Before)
			{
				TestEqual(FString::Printf(TEXT("%s, %s: %.2f%% added. %s"), *Each.Key.ToString(),
										  When, Added * 100.0f, StaleAsset),
					ASC->IncreasesForStat(Each.Key, NoTags) - Each.Value, Added, 0.000001f);
			}
		};
		KillUpTo(Next - 1);
		Check(TEXT("one short of the next thousand"), 0.0f);
		KillUpTo(Next);
		Check(TEXT("the next thousand"), 0.0005f);
		KillUpTo(Next + 2000 - 1);
		Check(TEXT("two thousand more less one"), 0.0005f + 0.0005f);
		Equipment->Unequip(Slot, Removed);
	}
	return true;
}

namespace CataclysmWeaponKillTest
{
	const TCHAR* WeaponRow = TEXT("Positive_This_weapon_has_5_20_more_damage_for_every_100");
	const TCHAR* LifetimeRow = TEXT("Negative_You_lose_1_4_max_resistances_for_every_100_000");

	/** A one-handed sword carrying the kill row at a roll, with a count already made. */
	FCataclysmItem Sword(float Roll, int32 Kills)
	{
		using namespace CataclysmEnchantmentEffectTest;
		FCataclysmItem Item = Carrying(TEXT("Weapon_Sword"), WeaponRow, DrawbackWithNoEffect);
		Item.Enchantments[0].PositiveRoll = Roll;
		Item.Kills = Kills;
		return Item;
	}

	/** The one enchantment "more" modifier on a stat, or -1 when there is not exactly one. */
	float OnlyEnchantmentMore(const TMap<FName, TArray<FCataclysmStatModifier>>& Totals,
							  const TCHAR* Stat)
	{
		const TArray<FCataclysmStatModifier>* On = Totals.Find(FName(Stat));
		float Found = -1.0f;
		int32 Count = 0;
		for (const FCataclysmStatModifier& Each : On ? *On : TArray<FCataclysmStatModifier>())
		{
			if (Each.Source == ECataclysmModifierSource::Enchantment
				&& Each.Bucket == ECataclysmStatBucket::More)
			{
				Found = Each.Value;
				++Count;
			}
		}
		return Count == 1 ? Found : -1.0f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillCountsOnEachWornWeaponTest,
	"Cataclysm.KillCounter.EveryPlayerKillCountsOnEachWornWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1833, the kill counter, window B; ruled 2026-09-25 under the owner's
 * delegation. A possessed player's kill counts on the one two-handed weapon it
 * starts with; once it holds a sword and a dagger, the next kill counts once on
 * each, because both weapons' damage is in every blow.
 */
bool FCataclysmKillCountsOnEachWornWeaponTest::RunTest(const FString&)
{
	using namespace CataclysmKillCounterTest;
	using namespace CataclysmEnchantmentEffectTest;
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	ACataclysmPlayerCharacter* Player = SpawnPossessedPlayer(Scope.World);
	UCataclysmEquipmentComponent* Equipment = Player ? Player->GetEquipment() : nullptr;
	if (!TestNotNull(TEXT("a possessed player's equipment"), Equipment)
		|| !TestNotNull(TEXT("holding a weapon"), Equipment->EquippedAt(ECataclysmGearSlot::Weapon1)))
	{
		return false;
	}
	TestTrue(TEXT("a two-handed weapon leaves the second slot empty"),
		Equipment->SlotIsEmpty(ECataclysmGearSlot::Weapon2));

	const auto Kill = [&Scope, Player](float Along)
	{
		ACataclysmEnemyCharacter* Enemy = SpawnEnemy(Scope.World, FVector(Along, 0.0f, 0.0f));
		if (Enemy)
		{
			UCataclysmSkillEffects::ApplyHit(Player, Enemy, 100.0f, FGameplayTagContainer());
			UCataclysmCombatEvents::NoteDeath(Enemy);
		}
		return Enemy != nullptr;
	};
	if (!TestTrue(TEXT("an enemy killed"), Kill(150.0f)))
	{
		return false;
	}
	TestEqual(TEXT("the two-handed weapon counts one"),
		Equipment->EquippedAt(ECataclysmGearSlot::Weapon1)->Kills, 1);

	Equipment->UnequipEverything();
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	FCataclysmItem Blade;
	Blade.Base = FName(TEXT("Weapon_Sword"));
	FCataclysmItem Knife;
	Knife.Base = FName(TEXT("Weapon_Dagger"));
	Equipment->EquipInto(Blade, ECataclysmGearSlot::Weapon1, Removed, AlsoRemoved);
	Equipment->EquipInto(Knife, ECataclysmGearSlot::Weapon2, Removed, AlsoRemoved);
	const FCataclysmItem* First = Equipment->EquippedAt(ECataclysmGearSlot::Weapon1);
	const FCataclysmItem* Second = Equipment->EquippedAt(ECataclysmGearSlot::Weapon2);
	if (!TestNotNull(TEXT("a sword in the first hand"), First)
		|| !TestNotNull(TEXT("and a dagger in the second"), Second)
		|| !TestTrue(TEXT("another enemy killed"), Kill(300.0f)))
	{
		return false;
	}
	TestEqual(TEXT("the sword counts one"), Equipment->EquippedAt(ECataclysmGearSlot::Weapon1)->Kills, 1);
	TestEqual(TEXT("and so does the dagger"), Equipment->EquippedAt(ECataclysmGearSlot::Weapon2)->Kills, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWeaponKeepsItsKillsTest,
	"Cataclysm.KillCounter.AWeaponKeepsItsKillsThroughUnequipTheStashAndASave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1833, the kill counter, window B. A sword worn for three kills comes
 * off holding three; put in the private stash and worn in a character record,
 * it still holds three after the record is written to JSON and read back.
 */
bool FCataclysmWeaponKeepsItsKillsTest::RunTest(const FString&)
{
	using namespace CataclysmKillCounterTest;
	using namespace CataclysmEnchantmentEffectTest;
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	FWearer Wearer(Scope.World);
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	FCataclysmItem Blade;
	Blade.Base = FName(TEXT("Weapon_Sword"));
	Wearer.Equipment->EquipInto(Blade, ECataclysmGearSlot::Weapon1, Removed, AlsoRemoved);
	for (int32 Kill = 0; Kill < 3; ++Kill)
	{
		Wearer.Equipment->NoteKillOnWornWeapons(nullptr);
	}
	FCataclysmItem TakenOff;
	if (!TestTrue(TEXT("the sword comes off"),
			Wearer.Equipment->Unequip(ECataclysmGearSlot::Weapon1, TakenOff)))
	{
		return false;
	}
	TestEqual(TEXT("holding its three kills"), TakenOff.Kills, 3);

	UCataclysmCharacterSave* Record = NewObject<UCataclysmCharacterSave>();
	// STAMPED AS THE GAME'S WRITER STAMPS IT (CataclysmSaveWriter.cpp), because a
	// record left at version 0 is one "nobody filled in" and the loader refuses
	// it: "the record says it is version 0; the first real version is 1".
	Record->SchemaVersion = UCataclysmCharacterSave::SchemaVersionNow;
	FCataclysmCarriedSlot Stashed;
	Stashed.Item = TakenOff;
	Record->PrivateStash.Add(Stashed);
	FCataclysmWornItem Worn;
	Worn.Slot = ECataclysmGearSlot::Weapon1;
	Worn.Item = TakenOff;
	Record->WornGear.Add(Worn);

	FString Json;
	FString Error;
	if (!TestTrue(TEXT("the record writes"), FCataclysmSaveStorage::ToJson(Record, Json, Error)))
	{
		AddError(Error);
		return false;
	}
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmCharacterSave* Read = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		Json, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message));
	// SAYS WHY IF IT DOES NOT READ BACK, as the fixture test does: the loader
	// hands its verdict and message over, and a bare "not null" hides both.
	if (!TestNotNull(*FString::Printf(TEXT("and reads back (%s -- %s)"),
				FCataclysmSaveStorage::Describe(Result), *Message), Read)
		|| !TestEqual(TEXT("one stashed"), Read->PrivateStash.Num(), 1)
		|| !TestEqual(TEXT("one worn"), Read->WornGear.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("the stashed sword still holds three"), Read->PrivateStash[0].Item.Kills, 3);
	TestEqual(TEXT("and the worn one"), Read->WornGear[0].Item.Kills, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWeaponTooltipKillsTest,
	"Cataclysm.KillCounter.AWeaponsTooltipShowsItsKills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Issue #1833: "Kills: 7" on a sword with seven, and no such line on one with none. */
bool FCataclysmWeaponTooltipKillsTest::RunTest(const FString&)
{
	FCataclysmCarriedSlot Slot;
	Slot.Item.Base = FName(TEXT("Weapon_Sword"));
	const auto Lines = [&Slot]()
	{
		return UCataclysmItemTooltip::LinesFor(
			Slot, UCataclysmItemModifiers::LoadBaseTable(), UCataclysmDropRoll::LoadAffixTable(),
			UCataclysmDropRoll::LoadCraftingMaterialTable(),
			UCataclysmDropRoll::LoadPositiveEnchantmentTable(),
			UCataclysmDropRoll::LoadNegativeEnchantmentTable());
	};
	TestFalse(TEXT("a sword with no kill states none"),
		Lines().ContainsByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("Kills")); }));
	Slot.Item.Kills = 7;
	TestTrue(TEXT("one with seven says so"), Lines().Contains(TEXT("Kills: 7")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWeaponKillRowStepsTest,
	"Cataclysm.Enchantments.TheWeaponKillRowGrowsAtItsRolledStepAndRefreshesOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1833, the kill counter, window B. "This weapon has 5-20% more damage
 * for every 100,000-500,000 kills", worn on a sword at the top roll: 20% more
 * per 100,000 kills. RULED 2026-10-05: the best roll gives the best of BOTH
 * ranges, so the step rolls down, from 500,000 to 100,000, while the value
 * rolls up; until then the top roll gave 20% per 500,000. A sword that has made
 * 99,998 grants nothing; the kill that reaches 100,000 refreshes the grant,
 * once, and attack and spell damage are each 1.2 times; the kills either side
 * refresh nothing.
 */
bool FCataclysmWeaponKillRowStepsTest::RunTest(const FString&)
{
	using namespace CataclysmKillCounterTest;
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmWeaponKillTest;
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	FWearer Wearer(Scope.World);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	Wearer.Equipment->EquipInto(Sword(1.0f, 99'998), ECataclysmGearSlot::Weapon1,
								Removed, AlsoRemoved);
	Wearer.Equipment->RefreshAttributes(ASC);

	const FGameplayTagContainer NoTags;
	const auto Multiplier = [ASC, &NoTags](const TCHAR* Stat)
	{
		return ASC->MultiplierForStatAgainst(FName(Stat), NoTags, nullptr);
	};
	const float Attack = Multiplier(TEXT("attack_damage"));
	const float Spell = Multiplier(TEXT("spell_damage"));

	TestFalse(TEXT("the 99,999th kill crosses no step"),
		Wearer.Equipment->NoteKillOnWornWeapons(ASC));
	TestEqual(TEXT("and grants nothing yet"), Multiplier(TEXT("attack_damage")), Attack, 0.0001f);
	TestTrue(TEXT("the 100,000th crosses the rolled step and refreshes the grant"),
		Wearer.Equipment->NoteKillOnWornWeapons(ASC));
	TestEqual(FString::Printf(TEXT("attack damage 1.2 times. %s"), StaleAsset),
		Multiplier(TEXT("attack_damage")) / Attack, 1.2f, 0.0001f);
	TestEqual(TEXT("and spell damage"), Multiplier(TEXT("spell_damage")) / Spell, 1.2f, 0.0001f);
	TestFalse(TEXT("the next kill refreshes nothing"),
		Wearer.Equipment->NoteKillOnWornWeapons(ASC));
	TestEqual(TEXT("and changes nothing"), Multiplier(TEXT("attack_damage")) / Attack, 1.2f, 0.0001f);

	// AT THE BOTTOM ROLL THE STEP IS 500,000 AND THE VALUE 5, the worst of both.
	// A sword at 499,999 grants nothing, and its next kill is a step.
	Wearer.Equipment->UnequipEverything();
	Wearer.Equipment->EquipInto(Sword(0.0f, 499'999), ECataclysmGearSlot::Weapon1,
								Removed, AlsoRemoved);
	Wearer.Equipment->RefreshAttributes(ASC);
	const float Low = Multiplier(TEXT("attack_damage"));
	TestTrue(TEXT("at the bottom roll the 500,000th kill is a step"),
		Wearer.Equipment->NoteKillOnWornWeapons(ASC));
	TestEqual(TEXT("worth 5%"), Multiplier(TEXT("attack_damage")) / Low, 1.05f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTwoWeaponsKillRowTest,
	"Cataclysm.Enchantments.TwoWeaponsWithTheKillRowGrantItOnceAtTheHigherRollsCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1833, the kill counter, window B; ruled 2026-09-25 under the owner's
 * delegation. Two worn swords carry "This weapon has 5-20% more damage for
 * every 100,000-500,000 kills": the benefit is granted once, at the higher
 * roll, with THAT sword's count. A top-roll sword at 200,000 kills and a
 * bottom-roll one at 900,000 grant 40 (two steps of 100,000 at 20), not 180
 * (the other's 900,000 at the top step) and not 45. On a tie of rolls, the
 * larger count. The counts are for the steps as ruled on 2026-10-05: 100,000 at
 * the top roll and 500,000 at the bottom.
 */
bool FCataclysmTwoWeaponsKillRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmWeaponKillTest;
	FTables Tables;
	if (!LoadAll(*this, Tables))
	{
		return false;
	}
	int32 Added = 0;
	TestEqual(TEXT("the higher roll's own count: 40 more"),
		OnlyEnchantmentMore(Gather(Tables, {Sword(1.0f, 200'000), Sword(0.0f, 900'000)}, Added),
							TEXT("attack_damage")), 40.0f, 0.001f);
	TestEqual(TEXT("whichever sword comes first"),
		OnlyEnchantmentMore(Gather(Tables, {Sword(0.0f, 900'000), Sword(1.0f, 200'000)}, Added),
							TEXT("attack_damage")), 40.0f, 0.001f);
	TestEqual(TEXT("a tie of rolls takes the larger count"),
		OnlyEnchantmentMore(Gather(Tables, {Sword(1.0f, 100'000), Sword(1.0f, 200'000)}, Added),
							TEXT("attack_damage")), 40.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLifetimeKillRowTest,
	"Cataclysm.Enchantments.TheLifetimeKillsRowLowersTheCapAtItsRolledStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1833, the kill counter, window B. "You lose 1-4% max resistances for
 * every 100,000 - 500,000 kills", worn at the top roll: 4 off the 70 cap per
 * 100,000 kills the character has made. RULED 2026-10-05: a roll of 1 gives a
 * drawback its harshest figure in BOTH ranges, so the step rolls down to
 * 100,000; until then the top roll took 4 per 500,000. 99,999 take nothing,
 * 100,000 take 4 and 200,000 take 8.
 */
bool FCataclysmLifetimeKillRowTest::RunTest(const FString&)
{
	using namespace CataclysmKillCounterTest;
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmWeaponKillTest;
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	ACataclysmPlayerCharacter* Player = SpawnPossessedPlayer(Scope.World);
	ACataclysmPlayerState* State =
		Player ? Player->GetPlayerState<ACataclysmPlayerState>() : nullptr;
	UCataclysmAbilitySystemComponent* ASC =
		State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
	UCataclysmEquipmentComponent* Equipment = Player ? Player->GetEquipment() : nullptr;
	if (!TestNotNull(TEXT("a possessed player"), ASC) || !TestNotNull(TEXT("its equipment"), Equipment))
	{
		return false;
	}
	Equipment->UnequipEverything();
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Equipment->Equip(Carrying(TEXT("Head_Helm"), BenefitWithNoEffect, LifetimeRow),
					 Removed, AlsoRemoved, Slot);
	Equipment->RefreshAttributes(ASC);

	using FCalc = UCataclysmDamageCalculation;
	State->SetLifetimeKills(50'000);
	TestEqual(TEXT("50,000 kills take nothing"), FCalc::ResistanceCapOf(ASC), 70.0f, 0.001f);
	State->SetLifetimeKills(99'999);
	TestEqual(TEXT("99,999 take nothing: the step rolled to 100,000"),
		FCalc::ResistanceCapOf(ASC), 70.0f, 0.001f);
	State->SetLifetimeKills(100'000);
	TestEqual(FString::Printf(TEXT("100,000 take 4. %s"), CataclysmKillCounterTest::StaleAsset),
		FCalc::ResistanceCapOf(ASC), 66.0f, 0.001f);
	State->SetLifetimeKills(200'000);
	TestEqual(TEXT("200,000 take 8"), FCalc::ResistanceCapOf(ASC), 62.0f, 0.001f);
	return true;
}

namespace CataclysmDeployablePart2Test
{
	/**
	 * A fresh creature with no armour, evasion, block or resistance, beside the
	 * last minion made, and the summoner's own ability system on it. Issue #1833,
	 * deployable Part 2.
	 */
	ACataclysmEnemyCharacter* Victim(CataclysmDeployableTest::FSummoner& Summoner,
									 float Evasion = 0.0f)
	{
		ACataclysmEnemyCharacter* Made = Summoner.World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(Summoner.Along, 150.0f, 0.0f), FRotator::ZeroRotator);
		if (!Made)
		{
			return nullptr;
		}
		Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Made->SetHealth(10000.0f);
		Made->SetArmour(0.0f);
		UAbilitySystemComponent* Its = Made->GetAbilitySystemComponent();
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), Evasion);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(
			UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		return Made;
	}

	/** What the stacks placed on this creature take off its armour now. */
	float ArmourRemoved(const ACataclysmEnemyCharacter* Creature)
	{
		const UCataclysmAbilitySystemComponent* Its = Creature
			? Cast<UCataclysmAbilitySystemComponent>(Creature->GetAbilitySystemComponent())
			: nullptr;
		return Its ? Its->ArmourRemovedPercentNow() : -1.0f;
	}

	/** A minion of this type made now, aged `Seconds`, striking a fresh creature: what it took. */
	float BlowAtAge(CataclysmDeployableTest::FSummoner& Summoner, const TCHAR* Type, float Seconds)
	{
		ACataclysmMinion* Minion = Summoner.Make(Type);
		if (!Minion)
		{
			return -1.0f;
		}
		Summoner.World->TimeSeconds += Seconds;
		ACataclysmEnemyCharacter* Target = Victim(Summoner);
		if (!Target)
		{
			return -1.0f;
		}
		const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
		UAbilitySystemComponent* Its = Target->GetAbilitySystemComponent();
		const float Before = Its->GetNumericAttribute(Health);
		Minion->AttackTarget(Target);
		return Before - Its->GetNumericAttribute(Health);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGadgetArmorRowTest,
	"Cataclysm.Enchantments.TheGadgetArmorRowStripsArmorFromWhatAMachineHitsFor3Seconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1833, deployable Part 2. "Gadgets apply a 15%-25% armor reduction to
 * enemies they hit for 3 seconds", at 25: a ballista's landed blow places 25%
 * on the creature it hits, a second blow refreshes it rather than adding
 * (a cap of 1, ruled 2026-09-25 under the owner's delegation), and it has
 * lapsed 3 seconds later. An evaded blow places nothing, and neither does an
 * imp's, which is no gadget.
 */
bool FCataclysmGadgetArmorRowTest::RunTest(const FString&)
{
	using namespace CataclysmDeployableTest;
	using namespace CataclysmDeployablePart2Test;
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	FSummoner Summoner(Scope.World, TEXT("Positive_Gadgets_apply_a_15_25_armor_reduction_to_enemi"));
	ACataclysmMinion* Ballista = Summoner.Make(TEXT("Ballista"));
	ACataclysmEnemyCharacter* Target = Victim(Summoner);
	if (!TestNotNull(TEXT("a ballista"), Ballista) || !TestNotNull(TEXT("a creature"), Target))
	{
		return false;
	}
	TestEqual(TEXT("nothing removed before the blow"), ArmourRemoved(Target), 0.0f, 0.001f);
	Ballista->AttackTarget(Target);
	TestEqual(TEXT("a ballista's landed blow removes 25%. If not, DT_EnchantmentEffects "
				   "may be older than the rows: run tools/generate_datatable_assets.py"),
		ArmourRemoved(Target), 25.0f, 0.001f);
	Ballista->AttackTarget(Target);
	TestEqual(TEXT("a second blow refreshes it: still 25%"), ArmourRemoved(Target), 25.0f, 0.001f);
	Scope.World->TimeSeconds += 3.5f;
	TestEqual(TEXT("and 3 seconds on, nothing"), ArmourRemoved(Target), 0.0f, 0.001f);

	ACataclysmEnemyCharacter* Evading = Victim(Summoner, /*Evasion=*/100.0f);
	if (TestNotNull(TEXT("an evading creature"), Evading))
	{
		Ballista->AttackTarget(Evading);
		TestEqual(TEXT("an evaded blow removes nothing"), ArmourRemoved(Evading), 0.0f, 0.001f);
	}
	// THE IMP IS KEPT OFF BY THE ROW'S REQUIRED TAGS, NOT BY THE `IsDeployable()`
	// GATE IN FRONT OF `deployable_hit`. The event carries the striking minion's
	// own type tags, and `PoolActionAllowed` refuses this row, which requires
	// `Type.Deployable`, for an imp either way; removing the gate fails nothing
	// here (measured 2026-09-27, proof C of deployable Part 2). This check does
	// not prove the gate.
	ACataclysmMinion* Imp = Summoner.Make(TEXT("Imp"));
	ACataclysmEnemyCharacter* ImpTarget = Victim(Summoner);
	if (TestNotNull(TEXT("an imp"), Imp) && TestNotNull(TEXT("its creature"), ImpTarget))
	{
		Imp->AttackTarget(ImpTarget);
		TestEqual(TEXT("an imp's blow removes nothing"), ArmourRemoved(ImpTarget), 0.0f, 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGadgetAgeRowsTest,
	"Cataclysm.Enchantments.TheGadgetAgeRowsGrowWithTheMachinesSecondsActive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1833, deployable Part 2, against a plain summoner's ballista of the
 * same age.
 *
 * "Gadgets deal 5%-10% increased damage for each second they have been active,
 * up to 30 seconds", at 10: a new ballista is plain, one 5.5 seconds old deals
 * 1.5 times, and one 40 seconds old 4 times, the 30 seconds' cap.
 * "Gadgets that survive for 15 seconds gain a permanent 20%-40% damage bonus
 * for the rest of their duration", at 40: 14.5 seconds is plain, 15.5 is 1.4
 * times and 50 is still 1.4.
 */
bool FCataclysmGadgetAgeRowsTest::RunTest(const FString&)
{
	using namespace CataclysmDeployableTest;
	using namespace CataclysmDeployablePart2Test;
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	FSummoner Plain(Scope.World, nullptr);
	const float Base = BlowAtAge(Plain, TEXT("Ballista"), 0.0f);
	if (!TestTrue(TEXT("a plain ballista's blow lands"), Base > 0.0f))
	{
		return false;
	}
	const auto Ratio = [&Scope, Base](const TCHAR* Enchantment, float Seconds)
	{
		FSummoner Wearing(Scope.World, Enchantment);
		return BlowAtAge(Wearing, TEXT("Ballista"), Seconds) / Base;
	};
	const TCHAR* PerSecond = TEXT("Positive_Gadgets_deal_5_10_increased_damage_for_each_se");
	const TCHAR* Survive = TEXT("Positive_Gadgets_that_survive_for_15_seconds_gain_a_perma");
	TestEqual(TEXT("per second: a new ballista is plain"), Ratio(PerSecond, 0.0f), 1.0f, 0.001f);
	TestEqual(TEXT("per second: 5.5 seconds old deals 1.5 times. If not, DT_EnchantmentEffects "
				   "may be older than the rows: run tools/generate_datatable_assets.py"),
		Ratio(PerSecond, 5.5f), 1.5f, 0.001f);
	TestEqual(TEXT("per second: 40 seconds old deals 4 times, the 30 seconds' cap"),
		Ratio(PerSecond, 40.0f), 4.0f, 0.001f);
	TestEqual(TEXT("survived: 14.5 seconds is plain"), Ratio(Survive, 14.5f), 1.0f, 0.001f);
	TestEqual(TEXT("survived: 15.5 seconds deals 1.4 times"), Ratio(Survive, 15.5f), 1.4f, 0.001f);
	TestEqual(TEXT("survived: 50 seconds still 1.4 times"), Ratio(Survive, 50.0f), 1.4f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGadgetEvasionRowTest,
	"Cataclysm.Enchantments.TheGadgetEvasionRowCountsEachMachineCommanded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1833, deployable Part 3. "Each active gadget increases your evasion
 * chance by 5%-10%", at 10: 10% increased evasion per deployable machine the
 * summoner commands. None is nothing, one ballista is 0.10 and two are 0.20;
 * an imp beside them adds nothing, because it is no gadget. Written as
 * increased, as "While moving, your evasion chance is increased by 10%-20%"
 * is.
 */
bool FCataclysmGadgetEvasionRowTest::RunTest(const FString&)
{
	using namespace CataclysmDeployableTest;
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	FSummoner Summoner(Scope.World, TEXT("Positive_Each_active_gadget_increases_your_evasion_chance"));
	const FGameplayTagContainer NoTags;
	const auto Increase = [&Summoner, &NoTags]()
	{
		return Summoner.ASC()->IncreasesForStat(FName(TEXT("evasion")), NoTags);
	};
	TestEqual(TEXT("no machine: nothing"), Increase(), 0.0f, 0.0001f);
	if (!TestNotNull(TEXT("a ballista"), Summoner.Make(TEXT("Ballista"))))
	{
		return false;
	}
	TestEqual(TEXT("one ballista: 10% increased. If not, DT_EnchantmentEffects may be "
				   "older than the rows: run tools/generate_datatable_assets.py"),
		Increase(), 0.10f, 0.0001f);
	Summoner.Make(TEXT("Ballista"));
	TestEqual(TEXT("two: 20%"), Increase(), 0.20f, 0.0001f);
	Summoner.Make(TEXT("Imp"));
	TestEqual(TEXT("and an imp beside them adds nothing"), Increase(), 0.20f, 0.0001f);
	return true;
}

namespace CataclysmRowsNowTest
{
	const TCHAR* MaximumResists = TEXT("Positive_You_have_10_maximum_resists");
	const TCHAR* BleedDuration = TEXT("Positive_Bleed_stacks_you_apply_have_50_100_increased_d");
	const TCHAR* KillLock = TEXT("Negative_Killing_an_enemy_triggers_a_1_2_second_global_co");
	const TCHAR* CritLock = TEXT("Negative_Critical_strikes_trigger_a_0_5_1_second_global_c");

	/** A skill's tags for one slot, which is all the lock is asked with. */
	FGameplayTagContainer SlotTags(const TCHAR* Slot)
	{
		return FGameplayTagContainer(FGameplayTag::RequestGameplayTag(FName(Slot)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMaximumResistsRowTest,
	"Cataclysm.Enchantments.TheMaximumResistsRowRaisesTheCapBy10",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "You have +10 maximum resists", worn, raises the resistance cap from 70 to
 * 80. Issue #1833, the rows written after the survey of 2026-09-30.
 * `UCataclysmDamageCalculation::ResistanceCapOf` asks `resistance_cap` on every
 * blow and on the sheet. A wearer carrying a pair with no effect row is the
 * control, so the 10 is the row's and nothing else's.
 */
bool FCataclysmMaximumResistsRowTest::RunTest(const FString&)
{
	using namespace CataclysmRowsNowTest;
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Plain(CataclysmEnchantmentEffectTest::BenefitWithNoEffect, true);
	FWorn Worn(MaximumResists, true);
	if (!TestNotNull(TEXT("a plain wearer"), Plain.ASC())
		|| !TestNotNull(TEXT("a wearer of the row"), Worn.ASC()))
	{
		return false;
	}
	TestEqual(TEXT("a pair with no effect leaves the cap at 70"),
			  UCataclysmDamageCalculation::ResistanceCapOf(Plain.ASC()), 70.0f, 0.001f);
	TestEqual(TEXT("+10 maximum resists: 80"),
			  UCataclysmDamageCalculation::ResistanceCapOf(Worn.ASC()), 80.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBleedDurationRowTest,
	"Cataclysm.Enchantments.TheBleedDurationRowLengthensOnlyABleed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Bleed stacks you apply have 50%-100% increased duration", worn at the top of
 * its range, doubles a bleed's duration and leaves a disease's alone. Issue
 * #1833. The same reading the shipped poison-duration row is measured by in
 * `TheAilmentScopedDotRowsReachOnlyTheirOwnAilment`: `DamageOverTimeNumbers`
 * asked with the ailment's own tag and with another's.
 */
bool FCataclysmBleedDurationRowTest::RunTest(const FString&)
{
	using namespace CataclysmRowsNowTest;
	using namespace CataclysmSmallHalvesTest;

	const FGameplayTag Bleed = Keyword(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTag Disease = Keyword(TEXT("Keyword.DoT.Disease"));
	if (!TestTrue(TEXT("both ailment tags are in the vocabulary"),
				  Bleed.IsValid() && Disease.IsValid()))
	{
		return false;
	}
	FWorn Worn(BleedDuration, true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const float Own = UCataclysmSkillEffects::DamageOverTimeNumbers(
		Worn.ASC(), 100.0f, 8.0f, FGameplayTagContainer(Bleed)).DurationSeconds;
	const float Other = UCataclysmSkillEffects::DamageOverTimeNumbers(
		Worn.ASC(), 100.0f, 8.0f, FGameplayTagContainer(Disease)).DurationSeconds;
	if (TestTrue(TEXT("bleed duration: both ask something"), Own > 0.0f && Other > 0.0f))
	{
		TestEqual(TEXT("bleed duration: 100% longer, and only for a bleed"),
				  Own / Other, 2.0f, 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLockRowsTest,
	"Cataclysm.Enchantments.TheKillAndCritLockRowsLockEverySkillForTheirTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Killing an enemy triggers a 1-2 second global cooldown on all your skills"
 * and "Critical strikes trigger a 0.5-1 second global cooldown on all your
 * skills", each worn at the top of its range: 2 seconds and 1 second. Issue
 * #1833. Each is `skill_locked` flat 1 scaled by the row's own stacks, granted
 * on its event and lasting the rolled time; the skill template refuses any
 * skill that is not a basic attack while the lock reads above nought.
 *
 * NOT LOCKED BEFORE THE EVENT, locked after it in two slots, still locked just
 * before the time runs out and free just after it. Each drawback is worn on its
 * own, so one row's stack cannot be what the other's reading shows.
 */
bool FCataclysmSkillLockRowsTest::RunTest(const FString&)
{
	using namespace CataclysmRowsNowTest;
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	const FName Locked(UCataclysmSkillSlots::LockedStat);
	const FGameplayTagContainer Movement = SlotTags(TEXT("Slot.Movement"));
	const FGameplayTagContainer Special = SlotTags(TEXT("Slot.Special"));

	struct FCase
	{
		const TCHAR* Drawback;
		const TCHAR* Event;
		float Seconds;
	};
	for (const FCase& Case : {FCase{KillLock, TEXT("kill"), 2.0f},
							  FCase{CritLock, TEXT("critical_strike"), 1.0f}})
	{
		FWorn Worn(Case.Drawback, false);
		UCataclysmAbilitySystemComponent* ASC = Worn.ASC();
		if (!TestNotNull(TEXT("a wearer in a world"), ASC))
		{
			return false;
		}
		UWorld* World = ASC->GetWorld();
		const auto LockOn = [&](const FGameplayTagContainer& Tags)
		{
			return ASC->StatForSkill(Locked, Tags, 0.0f);
		};

		TestEqual(FString::Printf(TEXT("%s: nothing is locked before a %s"), Case.Event, Case.Event),
				  LockOn(Movement), 0.0f, 0.001f);

		ASC->ActOnEvent(FName(Case.Event));
		TestTrue(FString::Printf(TEXT("%s: the movement skill is locked after it"), Case.Event),
				 LockOn(Movement) > 0.0f);
		TestTrue(FString::Printf(TEXT("%s: and so is the special skill"), Case.Event),
				 LockOn(Special) > 0.0f);

		World->TimeSeconds += Case.Seconds - 0.1f;
		TestTrue(FString::Printf(TEXT("%s: still locked 0.1 s before %.1f s"), Case.Event, Case.Seconds),
				 LockOn(Movement) > 0.0f);

		World->TimeSeconds += 0.2f;
		TestEqual(FString::Printf(TEXT("%s: free 0.1 s after %.1f s"), Case.Event, Case.Seconds),
				  LockOn(Movement), 0.0f, 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReservedHealthRowTest,
	"Cataclysm.Enchantments.TheReservedHealthRowReservesItsShareOfTheMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "80%-99% of your health is reserved", worn at the top of its range, reserves
 * 99% of maximum health and leaves the maximum where it was. Issue #1833, health
 * reservation, ruled 2026-09-30.
 */
bool FCataclysmReservedHealthRowTest::RunTest(const FString&)
{
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Plain(CataclysmEnchantmentEffectTest::DrawbackWithNoEffect, false);
	FWorn Worn(TEXT("Negative_80_99_of_your_health_is_reserved"), false);
	if (!TestNotNull(TEXT("a plain wearer"), Plain.ASC())
		|| !TestNotNull(TEXT("a wearer of the row"), Worn.ASC()))
	{
		return false;
	}
	const FGameplayAttribute Maximum = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const float Whole = Worn.ASC()->GetNumericAttribute(Maximum);
	TestEqual(TEXT("the maximum is the plain wearer's: reservation lowers nothing"),
			  Whole, Plain.ASC()->GetNumericAttribute(Maximum), 0.01f);
	TestEqual(TEXT("99% of it is reserved"), Worn.ASC()->HealthReserved(), Whole * 0.99f, 0.01f);
	TestEqual(TEXT("and nothing is reserved without the row"), Plain.ASC()->HealthReserved(), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionReserveRowTest,
	"Cataclysm.Enchantments.TheMinionReserveRowReserves500ForEachMinion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Each minion reserves 100-500 hp", worn at the top of its range, reserves
 * nothing alone, 500 with one minion commanded and 1000 with two. Issue #1833,
 * health reservation. The maximum is set to 5000 after the helm goes on, so the
 * rule that 1 point stays unreserved cannot be what either figure shows.
 */
bool FCataclysmMinionReserveRowTest::RunTest(const FString&)
{
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Worn(TEXT("Negative_Each_minion_reserves_100_500_hp"), false);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	Worn.ASC()->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 5000.0f);
	TestEqual(TEXT("no minion: nothing reserved"), Worn.ASC()->HealthReserved(), 0.0f, 0.001f);

	AActor* Summoner = Worn.Wearer->Actor;
	for (int32 Count = 1; Count <= 2; ++Count)
	{
		if (!TestNotNull(TEXT("an imp"), ACataclysmMinion::Spawn(
				Summoner, FVector(300.0f * Count, 0.0f, 0.0f), /*Lifetime=*/60.0f,
				/*bBurns=*/false, /*TypeName=*/TEXT("Imp"))))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("%d minions: %d reserved"), Count, 500 * Count),
				  Worn.ASC()->HealthReserved(), 500.0f * Count, 0.01f);
	}
	return true;
}

namespace CataclysmDurationRowsTest
{
	/** A self-buff skill granted to this wearer in this slot, with that slot's tag. */
	UCataclysmSelfBuffSkill* BuffOn(UCataclysmAbilitySystemComponent* ASC, AActor* Avatar,
									ECataclysmAbilitySlot Slot, const TCHAR* SlotTag)
	{
		const FGameplayAbilitySpecHandle Handle =
			ASC->GiveAbilityInSlot(UCataclysmSelfBuffSkill::StaticClass(), Slot, /*Level=*/1, Avatar);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmSelfBuffSkill* Skill =
			Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(SlotTag);
		}
		return Skill;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSupportDurationRowTest,
	"Cataclysm.Enchantments.TheSupportDurationRowDoublesOnlyASupportSkillsDurations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your support ability duration is increased by 50%-100%", worn at the top of
 * its range, doubles every duration a Support skill sets and leaves a Special
 * skill's alone. Issue #1833, ruled 2026-09-30: `skill_duration` scoped to
 * `Slot.Support`, read by the buff, the effect, the mark and the terrain.
 */
bool FCataclysmSupportDurationRowTest::RunTest(const FString&)
{
	using namespace CataclysmDurationRowsTest;
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Worn(TEXT("Positive_Your_support_ability_duration_is_increased_by_50"), true);
	if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
	{
		return false;
	}
	UCataclysmSelfBuffSkill* Support =
		BuffOn(Worn.ASC(), Worn.Wearer->Actor, ECataclysmAbilitySlot::Support, TEXT("Slot.Support"));
	UCataclysmSelfBuffSkill* Special =
		BuffOn(Worn.ASC(), Worn.Wearer->Actor, ECataclysmAbilitySlot::Special, TEXT("Slot.Special"));
	if (!TestNotNull(TEXT("a Support buff"), Support) || !TestNotNull(TEXT("a Special buff"), Special))
	{
		return false;
	}
	TestEqual(TEXT("a Support skill's own durations: doubled"),
			  Support->OwnDurationMultiplier(false), 2.0f, 0.001f);
	TestEqual(TEXT("and its buff: doubled"), Support->OwnDurationMultiplier(true), 2.0f, 0.001f);
	TestEqual(TEXT("a Special skill's: unchanged"), Special->OwnDurationMultiplier(false), 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBuffDurationRowsTest,
	"Cataclysm.Enchantments.TheBuffDurationRowsLengthenAndShortenABuffOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Buff effects you apply last 30%-60% longer", worn at the top, makes a buff
 * 1.6 times as long and leaves the skill's other durations alone; "Buff effects
 * applied to you last 30%-50% less time", worn at the top, makes it half as
 * long. Issue #1833. The second reads as the wearer's own buffs in a
 * one-player game, a judgement ruled 2026-09-30 that co-op would change.
 */
bool FCataclysmBuffDurationRowsTest::RunTest(const FString&)
{
	using namespace CataclysmDurationRowsTest;
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	struct FCase
	{
		const TCHAR* Enchantment;
		bool bBenefit;
		float Buff;
	};
	for (const FCase& Case : {FCase{TEXT("Positive_Buff_effects_you_apply_last_30_60_longer"), true, 1.6f},
							  FCase{TEXT("Negative_Buff_effects_applied_to_you_last_30_50_less_ti"), false, 0.5f}})
	{
		FWorn Worn(Case.Enchantment, Case.bBenefit);
		if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
		{
			return false;
		}
		UCataclysmSelfBuffSkill* Skill =
			BuffOn(Worn.ASC(), Worn.Wearer->Actor, ECataclysmAbilitySlot::Support, TEXT("Slot.Support"));
		if (!TestNotNull(TEXT("a buff"), Skill))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("%s: the buff"), Case.Enchantment),
				  Skill->OwnDurationMultiplier(true), Case.Buff, 0.001f);
		TestEqual(FString::Printf(TEXT("%s: the skill's other durations"), Case.Enchantment),
				  Skill->OwnDurationMultiplier(false), 1.0f, 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDebuffDurationRowTest,
	"Cataclysm.Enchantments.TheDebuffDurationRowLengthensWhatYouApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Debuff effects you apply last 30%-60% longer", worn at the top, makes every
 * debuff the wearer places on an enemy 1.6 times as long. Issue #1833.
 */
bool FCataclysmDebuffDurationRowTest::RunTest(const FString&)
{
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Worn(TEXT("Positive_Debuff_effects_you_apply_last_30_60_longer"), true);
	if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
	{
		return false;
	}
	TestEqual(TEXT("debuffs the wearer applies: 1.6 times as long"),
			  UCataclysmSkillEffects::DebuffDurationMultiplierOf(Worn.ASC()), 1.6f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChronomancerSetTest,
	"Cataclysm.Enchantments.ChronomancersTwoPiecesLengthenDebuffsAndShortenBuffs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Chronomancer's Time-Lock, written whole at its first threshold. Issue #1833.
 * Two pieces each carrying the 2-piece bonus, "All debuffs you apply to enemies
 * now last 50% longer", and the set's drawback, "All buffs you apply to
 * yourself now last 25% less time": debuffs 1.5 times as long and a buff 0.75.
 * One piece is below the threshold, so neither applies.
 */
bool FCataclysmChronomancerSetTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmDurationRowsTest;

	const TCHAR* Bonus = TEXT("Positive_Chronomancer_s_Time_Lock_2_Piece_Bonus_All_de");
	const TCHAR* Drawback = TEXT("Negative_All_buffs_you_apply_to_yourself_now_last_25_les");

	for (const int32 Pieces : {1, 2})
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		FWearer Wearer(World);
		const TCHAR* Bases[] = {TEXT("Head_Helm"), TEXT("Chest_Cuirass")};
		for (int32 Index = 0; Index < Pieces; ++Index)
		{
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
			Wearer.Equipment->Equip(Carrying(Bases[Index], Bonus, Drawback), Removed, AlsoRemoved, Slot);
		}
		Wearer.Equipment->RefreshAttributes(Wearer.AbilitySystem);
		UCataclysmSelfBuffSkill* Skill = BuffOn(Wearer.AbilitySystem, Wearer.Actor,
												ECataclysmAbilitySlot::Support, TEXT("Slot.Support"));
		if (!TestNotNull(TEXT("a buff"), Skill))
		{
			return false;
		}
		const bool bWhole = Pieces >= 2;
		TestEqual(FString::Printf(TEXT("%d piece(s): debuffs"), Pieces),
				  UCataclysmSkillEffects::DebuffDurationMultiplierOf(Wearer.AbilitySystem),
				  bWhole ? 1.5f : 1.0f, 0.001f);
		TestEqual(FString::Printf(TEXT("%d piece(s): a buff"), Pieces),
				  Skill->OwnDurationMultiplier(true), bWhole ? 0.75f : 1.0f, 0.001f);
	}
	return true;
}

namespace CataclysmProjectileRangeRowsTest
{
	/** A skill of this template granted to the wearer, stating these params and tags. */
	template <typename T>
	T* SkillOn(UCataclysmAbilitySystemComponent* ASC, AActor* Avatar,
			   const TCHAR* ParamText, const TCHAR* TagCell)
	{
		const FGameplayAbilitySpecHandle Handle = ASC->GiveAbilityInSlot(
			T::StaticClass(), ECataclysmAbilitySlot::Special, /*Level=*/1, Avatar);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
		T* Skill = Spec ? Cast<T>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->Params = UCataclysmSkillShapes::ParseParams(ParamText);
			Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagCell);
		}
		return Skill;
	}

	constexpr const TCHAR* Bolt = TEXT("Range=10; Radius=1; Speed=2000");
	constexpr const TCHAR* RangedTags = TEXT("Type.Ranged, Type.Projectile");
	constexpr const TCHAR* ThrownTags = TEXT("Type.Melee, Type.Projectile");

	/** A Bolt Turret summoned by this actor, or null with an error if the row is missing. */
	ACataclysmMinion* TurretFrom(FAutomationTestBase& Test, AActor* Summoner)
	{
		ACataclysmMinion* Turret = ACataclysmMinion::Spawn(
			Summoner, FVector(300.0f, 0.0f, 0.0f), /*Lifetime=*/20.0f,
			/*bBurns=*/false, TEXT("BoltTurret"));
		if (Turret && Turret->TypeName != FString(TEXT("BoltTurret")))
		{
			Test.AddError(TEXT("DT_MinionTypes could not supply the BoltTurret row. Run "
							   "tools/generate_datatable_assets.py"));
			return nullptr;
		}
		return Turret;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileSpeedRowTest,
	"Cataclysm.Enchantments.TheProjectileSpeedRowQuickensOnlyARangedSkillsShots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Ranged skills have 30%-60% increased projectile speed", worn at the top,
 * makes a ranged skill's twenty metres a second into thirty-two and leaves a
 * melee skill's alone. Issue #1833, `projectile_speed` scoped to `Type.Ranged`.
 */
bool FCataclysmProjectileSpeedRowTest::RunTest(const FString&)
{
	using namespace CataclysmProjectileRangeRowsTest;
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Worn(TEXT("Positive_Ranged_skills_have_30_60_increased_projectile"), true);
	if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
	{
		return false;
	}
	const UCataclysmProjectileSkill* Ranged =
		SkillOn<UCataclysmProjectileSkill>(Worn.ASC(), Worn.Wearer->Actor, Bolt, RangedTags);
	const UCataclysmProjectileSkill* Thrown =
		SkillOn<UCataclysmProjectileSkill>(Worn.ASC(), Worn.Wearer->Actor, Bolt, ThrownTags);
	if (!TestNotNull(TEXT("a ranged skill"), Ranged) || !TestNotNull(TEXT("a melee skill"), Thrown))
	{
		return false;
	}
	TestEqual(TEXT("a ranged skill's shot: 60% faster"), Ranged->ScaledProjectileSpeed(), 3200.0f, 0.01f);
	TestEqual(TEXT("a melee skill's: unchanged"), Thrown->ScaledProjectileSpeed(), 2000.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSlowerProjectileRowsTest,
	"Cataclysm.Enchantments.TheTwoSlowerProjectileRowsSlowTheShotsTheyName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The two negatives, each worn at the top. "Ranged skills have 20%-35% reduced
 * projectile speed" slows a ranged skill's shot to 0.65 and leaves a melee
 * skill's alone; "Projectiles travel 30%-50% slower" slows any projectile
 * skill's to 0.5. Issue #1833.
 */
bool FCataclysmSlowerProjectileRowsTest::RunTest(const FString&)
{
	using namespace CataclysmProjectileRangeRowsTest;
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	{
		FWorn Worn(TEXT("Negative_Ranged_skills_have_20_35_reduced_projectile_sp"), false);
		if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
		{
			return false;
		}
		const UCataclysmProjectileSkill* Ranged =
			SkillOn<UCataclysmProjectileSkill>(Worn.ASC(), Worn.Wearer->Actor, Bolt, RangedTags);
		const UCataclysmProjectileSkill* Thrown =
			SkillOn<UCataclysmProjectileSkill>(Worn.ASC(), Worn.Wearer->Actor, Bolt, ThrownTags);
		if (TestNotNull(TEXT("a ranged skill"), Ranged) && TestNotNull(TEXT("a melee skill"), Thrown))
		{
			TestEqual(TEXT("35% reduced: a ranged skill's shot at 0.65"),
					  Ranged->ScaledProjectileSpeed(), 1300.0f, 0.01f);
			TestEqual(TEXT("and a melee skill's unchanged"),
					  Thrown->ScaledProjectileSpeed(), 2000.0f, 0.01f);
		}
	}
	{
		FWorn Worn(TEXT("Negative_Projectiles_travel_30_50_slower"), false);
		if (!TestNotNull(TEXT("a second wearer"), Worn.ASC()))
		{
			return false;
		}
		const UCataclysmProjectileSkill* Thrown =
			SkillOn<UCataclysmProjectileSkill>(Worn.ASC(), Worn.Wearer->Actor, Bolt, ThrownTags);
		if (TestNotNull(TEXT("a projectile skill"), Thrown))
		{
			TestEqual(TEXT("50% slower: any projectile skill's shot at half speed"),
					  Thrown->ScaledProjectileSpeed(), 1000.0f, 0.01f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRangeRowTest,
	"Cataclysm.Enchantments.TheRangeRowLengthensOnlyARangedSkill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your ranged skills have 20%-40% increased range", worn at the top, makes a
 * ranged skill's ten metres fourteen and leaves a melee skill's alone. Issue
 * #1833, `skill_range` scoped to `Type.Ranged`.
 */
bool FCataclysmRangeRowTest::RunTest(const FString&)
{
	using namespace CataclysmProjectileRangeRowsTest;
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Worn(TEXT("Positive_Your_ranged_skills_have_20_40_increased_range"), true);
	if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
	{
		return false;
	}
	const UCataclysmProjectileSkill* Ranged =
		SkillOn<UCataclysmProjectileSkill>(Worn.ASC(), Worn.Wearer->Actor, Bolt, RangedTags);
	const UCataclysmProjectileSkill* Thrown =
		SkillOn<UCataclysmProjectileSkill>(Worn.ASC(), Worn.Wearer->Actor, Bolt, ThrownTags);
	if (!TestNotNull(TEXT("a ranged skill"), Ranged) || !TestNotNull(TEXT("a melee skill"), Thrown))
	{
		return false;
	}
	TestEqual(TEXT("a ranged skill's range: 40% longer"), Ranged->ScaledRangeCm(), 1400.0f, 0.01f);
	TestEqual(TEXT("a melee skill's: unchanged"), Thrown->ScaledRangeCm(), 1000.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGadgetRangeRowTest,
	"Cataclysm.Enchantments.TheGadgetRangeRowLengthensAGadgetsReachAndNotice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Gadgets have 20%-40% increased attack range", worn at the top: a Bolt Turret
 * the wearer summons reaches and notices 40% further than one summoned by a
 * character wearing nothing. Issue #1833, `minion_range` scoped to
 * `Type.Deployable`.
 */
bool FCataclysmGadgetRangeRowTest::RunTest(const FString&)
{
	using namespace CataclysmProjectileRangeRowsTest;
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Worn(TEXT("Positive_Gadgets_have_20_40_increased_attack_range"), true);
	if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
	{
		return false;
	}
	AActor* Bare = Worn.World->SpawnActor<AActor>();
	ACataclysmMinion* Geared = TurretFrom(*this, Worn.Wearer->Actor);
	ACataclysmMinion* Plain = TurretFrom(*this, Bare);
	if (!TestNotNull(TEXT("the wearer's turret"), Geared) || !TestNotNull(TEXT("a plain turret"), Plain))
	{
		return false;
	}
	TestEqual(TEXT("the wearer's turret reaches 40% further"), Geared->ReachCm, Plain->ReachCm * 1.4f, 0.01f);
	TestEqual(TEXT("and notices 40% further"), Geared->NoticeRadiusCm, Plain->NoticeRadiusCm * 1.4f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMeleeReachRowTest,
	"Cataclysm.Enchantments.TheMeleeReachRowAddsAMetreToAMeleeStrike",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your melee skills have +0.5-1 metre reach", worn at the top, adds a metre to
 * a melee strike's reach and nothing to a ranged one's. Issue #1833, on
 * Overreach's `melee_reach_metres` (issue #1515) scoped to `Type.Melee`.
 */
bool FCataclysmMeleeReachRowTest::RunTest(const FString&)
{
	using namespace CataclysmProjectileRangeRowsTest;
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Worn(TEXT("Positive_Your_melee_skills_have_0_5_1_metre_reach"), true);
	if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
	{
		return false;
	}
	const UCataclysmStrikeSkill* Melee = SkillOn<UCataclysmStrikeSkill>(
		Worn.ASC(), Worn.Wearer->Actor, TEXT("Radius=2; Angle=90"), TEXT("Type.Melee, Type.Strike"));
	const UCataclysmStrikeSkill* Ranged = SkillOn<UCataclysmStrikeSkill>(
		Worn.ASC(), Worn.Wearer->Actor, TEXT("Radius=2; Angle=90"), TEXT("Type.Ranged, Type.Strike"));
	if (!TestNotNull(TEXT("a melee strike"), Melee) || !TestNotNull(TEXT("a ranged strike"), Ranged))
	{
		return false;
	}
	TestEqual(TEXT("a melee strike reaches a metre further"), Melee->MeleeReachBonusCm(), 100.0f, 0.01f);
	TestEqual(TEXT("a ranged one no further"), Ranged->MeleeReachBonusCm(), 0.0f, 0.01f);
	return true;
}

namespace CataclysmConditionRowsTest
{
	/** The wearer's damage taken, asked with a blow of this kind. */
	float DamageTakenFrom(UCataclysmAbilitySystemComponent* ASC, bool bMelee)
	{
		FCataclysmBlowContext Blow;
		Blow.bIsMelee = bMelee;
		Blow.bIsRanged = !bMelee;
		return ASC->StatForSkill(FName(UCataclysmDamageCalculation::DamageTakenStat),
								 FGameplayTagContainer(),
								 UCataclysmDamageCalculation::NormalDamageTaken,
								 /*SkillHealthCostPercent=*/-1.0f, Blow);
	}

	/** Maximum first, then current: the vital set clamps health to it. */
	void SetHealth(UCataclysmAbilitySystemComponent* ASC, float Maximum, float Current)
	{
		ASC->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Maximum);
		ASC->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Current);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCriticalArmourRowTest,
	"Cataclysm.Enchantments.TheCriticalArmourRowGivesItsShareToCriticalStrikes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your critical strikes ignore 20%-40% of enemy armor", worn at the top, gives
 * the wearer 40 `critical_armor_penetration`, the share
 * `UCataclysmDamageCalculation::Resolve` adds once a blow critically strikes.
 * Issue #1833, ruled 2026-09-30.
 */
bool FCataclysmCriticalArmourRowTest::RunTest(const FString&)
{
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Worn(TEXT("Positive_Your_critical_strikes_ignore_20_40_of_enemy_ar"), true);
	if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
	{
		return false;
	}
	TestEqual(TEXT("critical strikes ignore 40% of armour"),
			  Worn.ASC()->StatForSkill(FName(UCataclysmDamageCalculation::CriticalArmorPenetrationStat),
									   FGameplayTagContainer(), 0.0f),
			  40.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStationaryRangedRowTest,
	"Cataclysm.Enchantments.TheStationaryRangedRowNeedsBothStandingStillAndARangedHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "While stationary you take 20%-35% increased damage from ranged attacks",
 * worn at the top: a ranged hit on a wearer standing still is taken at 1.35. A
 * melee hit, or a ranged hit while moving, is taken at 1. Issue #1833, the row's
 * two conditions, ruled 2026-09-30 to mean both.
 */
bool FCataclysmStationaryRangedRowTest::RunTest(const FString&)
{
	using namespace CataclysmConditionRowsTest;
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Worn(TEXT("Negative_While_stationary_you_take_20_35_increased_dama"), false);
	UCataclysmAbilitySystemComponent* ASC = Worn.ASC();
	if (!TestNotNull(TEXT("a wearer"), ASC))
	{
		return false;
	}

	// A CHARACTER THAT HAS NEVER MOVED IS NOT STATIONARY, so it moves once first.
	ASC->NoteMovedMetres(1.0f);
	TestEqual(TEXT("moving, a ranged hit: 100"), DamageTakenFrom(ASC, false), 100.0f, 0.001f);

	ASC->NoteDidNotMove();
	CataclysmTestWorld::RunClock(Worn.World, 3.0f);
	TestEqual(TEXT("standing still, a ranged hit: 135"), DamageTakenFrom(ASC, false), 135.0f, 0.001f);
	TestEqual(TEXT("standing still, a melee hit: 100"), DamageTakenFrom(ASC, true), 100.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMeleeHighHealthRowTest,
	"Cataclysm.Enchantments.TheMeleeHighHealthRowNeedsBothAMeleeHitAndHealthAboveThreeQuarters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "You take 20%-35% increased damage from melee attacks while your HP is above
 * 75%", worn at the top: a melee hit at 80% health is taken at 1.35; a ranged
 * hit at 80%, or a melee hit at half health, at 1. Issue #1833.
 */
bool FCataclysmMeleeHighHealthRowTest::RunTest(const FString&)
{
	using namespace CataclysmConditionRowsTest;
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Worn(TEXT("Negative_You_take_20_35_increased_damage_from_melee_att"), false);
	UCataclysmAbilitySystemComponent* ASC = Worn.ASC();
	if (!TestNotNull(TEXT("a wearer"), ASC))
	{
		return false;
	}

	SetHealth(ASC, 1000.0f, 800.0f);
	TestEqual(TEXT("80% health, a melee hit: 135"), DamageTakenFrom(ASC, true), 135.0f, 0.001f);
	TestEqual(TEXT("80% health, a ranged hit: 100"), DamageTakenFrom(ASC, false), 100.0f, 0.001f);
	SetHealth(ASC, 1000.0f, 500.0f);
	TestEqual(TEXT("half health, a melee hit: 100"), DamageTakenFrom(ASC, true), 100.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFreeAboveRowTest,
	"Cataclysm.Enchantments.TheFreeAbilitiesRowUsesTheThresholdItRolled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your abilities are free when above 80%-95% hp". At 90% health a skill is
 * free for a piece rolled at the top, whose threshold is 80, and costs its mana
 * for a piece rolled at the bottom, whose threshold is 95. Issue #1833: the
 * threshold rolls with the value. RULED 2026-10-05: the best roll gives the
 * best outcome, so the top roll is the EASIEST threshold. That replaces the
 * labelled judgement of 2026-09-30, under which a higher roll was a harder
 * threshold.
 */
bool FCataclysmFreeAboveRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmConditionRowsTest;

	const auto CostAt = [](float Roll, float Health) -> float
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!World)
		{
			return -1.0f;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		FWearer Wearer(World);
		FCataclysmItem Piece = Carrying(TEXT("Head_Helm"),
										TEXT("Positive_Your_abilities_are_free_when_above_80_95_hp"),
										DrawbackWithNoEffect);
		Piece.Enchantments[0].PositiveRoll = Roll;
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
		Wearer.Equipment->Equip(Piece, Removed, AlsoRemoved, Slot);
		Wearer.Equipment->RefreshAttributes(Wearer.AbilitySystem);
		SetHealth(Wearer.AbilitySystem, 1000.0f, Health);

		const FGameplayAbilitySpecHandle Handle = Wearer.AbilitySystem->GiveAbilityInSlot(
			UCataclysmStrikeSkill::StaticClass(), ECataclysmAbilitySlot::Special, /*Level=*/1,
			Wearer.Actor);
		FGameplayAbilitySpec* Spec =
			Handle.IsValid() ? Wearer.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		const UCataclysmStrikeSkill* Skill =
			Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
		return Skill ? Skill->ManaCostFor(Wearer.AbilitySystem) : -1.0f;
	};

	const float Top = CostAt(1.0f, 900.0f);
	const float TopBelow = CostAt(1.0f, 700.0f);
	const float Bottom = CostAt(0.0f, 900.0f);
	TestEqual(TEXT("rolled at the top, a threshold of 80: free at 90% health"), Top, 0.0f, 0.001f);
	// AND THE SAME PIECE BELOW ITS THRESHOLD PAYS: a conditioned removal
	// removes the cost only while its condition holds.
	TestTrue(*FString::Printf(TEXT("the same piece at 70%% health, below 80: it costs its mana, %.2f"), TopBelow),
			 TopBelow > 0.0f);
	TestTrue(*FString::Printf(TEXT("rolled at the bottom, a threshold of 95: it costs its mana at 90%%, %.2f"), Bottom),
			 Bottom > 0.0f);
	return true;
}

namespace CataclysmCeilingRowsTest
{
	/** Pin the critical strike roll for the life of this object. */
	struct FPinnedCritRoll
	{
		explicit FPinnedCritRoll(float Roll)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.CritRoll"));
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}

		~FPinnedCritRoll()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCritCeilingRowTest,
	"Cataclysm.Enchantments.TheCritCeilingRowStopsACriticalStrikeAboveIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your critical strike chance cannot exceed 30%-50%", worn at the top of its
 * roll, is a ceiling of 30: a wearer at 100% chance critically strikes on a
 * roll of 20 and not on a roll of 40. Issue #1833, `max_crit_chance` flat -70,
 * the complement of the sentence's 30. RULED 2026-10-05: a roll of 1 gives a
 * drawback its harshest figure; until then the top roll was a ceiling of 50.
 */
bool FCataclysmCritCeilingRowTest::RunTest(const FString&)
{
	using namespace CataclysmCeilingRowsTest;
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Worn(TEXT("Negative_Your_critical_strike_chance_cannot_exceed_30_50"), false);
	UCataclysmAbilitySystemComponent* ASC = Worn.ASC();
	if (!TestNotNull(TEXT("a wearer"), ASC))
	{
		return false;
	}
	// AFTER THE REFRESH, which writes nothing a wearer holding no weapon strikes with.
	ASC->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

	// THE STAT LINES A BLOW READS, NOT THE ATTRIBUTES. After the refresh the wearer
	// carries a crit_chance line of base 0 (the attribute effect's), and a blow asks
	// the line: the first run of this test wrote the attribute and read a chance of
	// 0. So the line is written here, with the worn row's own ceiling line kept.
	const FCataclysmStatInputs* Ceiling =
		ASC->GetStatInputs(FName(UCataclysmCombatAttributeSet::MaxCritChanceStat));
	if (!TestNotNull(TEXT("the worn row's ceiling line"), Ceiling))
	{
		return false;
	}
	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.Add(FName(UCataclysmCombatAttributeSet::MaxCritChanceStat), *Ceiling);
	Lines.FindOrAdd(FName(TEXT("crit_chance"))).Base = 100.0f;
	Lines.FindOrAdd(FName(TEXT("crit_multiplier"))).Base = 200.0f;
	ASC->SetStatInputs(MoveTemp(Lines));

	// A TARGET THE FIRST BLOW CANNOT KILL, so the second is a real blow. The first
	// run's target died to the first and the "no critical strike" check passed
	// against a corpse.
	CataclysmEnchantmentEffectTest::FWearer Target(Worn.World);
	Target.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
	Target.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);

	const auto CriticalOn = [&](float Roll)
	{
		const FPinnedCritRoll Pinned(Roll);
		FCataclysmDamageResult Result;
		UCataclysmSkillEffects::ApplyHit(Worn.Wearer->Actor, Target.Actor, 100.0f,
										 FGameplayTagContainer(), FCataclysmHitDelivery(), &Result);
		return Result.bWasCritical;
	};
	TestTrue(TEXT("a roll of 20 is under the ceiling of 30: a critical strike"), CriticalOn(20.0f));
	TestFalse(TEXT("a roll of 40 is over it: no critical strike, though the chance is 100%"), CriticalOn(40.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShieldCeilingRowTest,
	"Cataclysm.Enchantments.TheShieldCeilingRowStopsRegenerationAtHalf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your energy shield cannot recharge above 50% of its maximum": a shield of
 * 400 of 1000 regenerating 1000 a second stops at 500. Issue #1833,
 * `energy_shield_recharge_ceiling_reduction` flat 50, leaving 50 of 100.
 */
bool FCataclysmShieldCeilingRowTest::RunTest(const FString&)
{
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Worn(TEXT("Negative_Your_energy_shield_cannot_recharge_above_50_of"), false);
	UCataclysmAbilitySystemComponent* ASC = Worn.ASC();
	if (!TestNotNull(TEXT("a wearer"), ASC))
	{
		return false;
	}
	ASC->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 1000.0f);
	ASC->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 400.0f);

	// THE REGENERATION RATE THROUGH ITS STAT LINE, NOT THE ATTRIBUTE: after the
	// refresh the wearer carries an energy_shield_regen line of base 0, and the step
	// asks the line. The first run wrote the attribute and regenerated nothing. The
	// worn row's own reduction line is kept.
	const FCataclysmStatInputs* Reduction =
		ASC->GetStatInputs(FName(UCataclysmRegeneration::EnergyShieldRechargeCeilingReductionStat));
	if (!TestNotNull(TEXT("the worn row's reduction line"), Reduction))
	{
		return false;
	}
	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.Add(FName(UCataclysmRegeneration::EnergyShieldRechargeCeilingReductionStat), *Reduction);
	Lines.FindOrAdd(FName(UCataclysmRegeneration::EnergyShieldRegenStat)).Base = 1000.0f;
	ASC->SetStatInputs(MoveTemp(Lines));
	UCataclysmRegeneration::ApplyStep(Worn.Wearer->Actor, 1.0f, 100.0f);
	TestEqual(TEXT("a second of regeneration stops at half the maximum, 500"),
			  ASC->GetNumericAttribute(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute()),
			  500.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoExperienceRowTest,
	"Cataclysm.Enchantments.TheNoExperienceRowRemovesExperienceGain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Kills no longer generate any experience" removes `experience_gain`, so the
 * share a kill grants, asked over its base of 100, is nothing. Issue #1833.
 * `ACataclysmPlayerState::ExperienceAfterGain` is what asks it, measured by the
 * stat's probe; a wearer here is no player state.
 */
bool FCataclysmNoExperienceRowTest::RunTest(const FString&)
{
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Worn(TEXT("Negative_Kills_no_longer_generate_any_experience"), false);
	if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
	{
		return false;
	}
	TestEqual(TEXT("the share of experience a kill grants: nothing"),
			  Worn.ASC()->StatAppliedTo(FName(ACataclysmPlayerState::ExperienceGainStat),
										FGameplayTagContainer(), 100.0f),
			  0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMovementManaRowTest,
	"Cataclysm.Enchantments.TheMovementManaRowAddsHalfTheMaximumToAMovementSkillOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Movement abilities cost 20%-50% of your maximum mana", worn at the top: a
 * Movement skill costs its own mana plus 500 of a maximum of 1000, and a
 * Special skill only its own. Issue #1833, `mana_cost_as_maximum_mana_percent`
 * scoped to Slot.Movement, PLUS the normal cost.
 */
bool FCataclysmMovementManaRowTest::RunTest(const FString&)
{
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Worn(TEXT("Negative_Movement_abilities_cost_20_50_of_your_maximum"), false);
	UCataclysmAbilitySystemComponent* ASC = Worn.ASC();
	if (!TestNotNull(TEXT("a wearer"), ASC))
	{
		return false;
	}
	ASC->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);

	const auto SkillIn = [&](ECataclysmAbilitySlot Slot, const TCHAR* SlotTag) -> UCataclysmStrikeSkill*
	{
		const FGameplayAbilitySpecHandle Handle = ASC->GiveAbilityInSlot(
			UCataclysmStrikeSkill::StaticClass(), Slot, /*Level=*/1, Worn.Wearer->Actor);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmStrikeSkill* Skill = Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(SlotTag);
		}
		return Skill;
	};
	const UCataclysmStrikeSkill* Dash = SkillIn(ECataclysmAbilitySlot::Movement, TEXT("Slot.Movement"));
	const UCataclysmStrikeSkill* Strike = SkillIn(ECataclysmAbilitySlot::Special, TEXT("Slot.Special"));
	if (!TestNotNull(TEXT("a movement skill"), Dash) || !TestNotNull(TEXT("a special skill"), Strike))
	{
		return false;
	}
	TestEqual(TEXT("a movement skill: its own cost plus half the maximum mana"),
			  Dash->ManaCostFor(ASC), Dash->GetManaCost() + 500.0f, 0.01f);
	TestEqual(TEXT("a special skill: its own cost only"),
			  Strike->ManaCostFor(ASC), Strike->GetManaCost(), 0.01f);
	return true;
}

namespace CataclysmFloorRowsTest
{
	/** A possessed player wearing one helm carrying these two enchantments, refreshed. */
	struct FWearingPlayer
	{
		FWearingPlayer(UWorld* World, const TCHAR* Positive, const TCHAR* Negative)
		{
			using namespace CataclysmEnchantmentEffectTest;
			Player = CataclysmKillCounterTest::SpawnPossessedPlayer(World);
			State = Player ? Player->GetPlayerState<ACataclysmPlayerState>() : nullptr;
			ASC = State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
			UCataclysmEquipmentComponent* Equipment = Player ? Player->GetEquipment() : nullptr;
			if (!ASC || !Equipment)
			{
				ASC = nullptr;
				return;
			}
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
			Equipment->Equip(Carrying(TEXT("Head_Helm"), Positive, Negative), Removed, AlsoRemoved, Slot);
			Equipment->RefreshAttributes(ASC);
		}

		ACataclysmPlayerCharacter* Player = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmAbilitySystemComponent* ASC = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorTimeRowTest,
	"Cataclysm.Enchantments.TheFloorTimeRowRaisesDamageTakenEveryFifteenSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Enemies deal 5%-8% increased damage for every 15 seconds spent on the same
 * dungeon floor", worn at the top: before any floor begins the wearer takes
 * normal damage, and 31 seconds into a floor, two whole steps, it takes 1.16.
 * Issue #1833, `damage_taken` scaled by `seconds_on_floor`. No cap, a labelled
 * judgement of 2026-09-30 on the owner's play-check list.
 */
bool FCataclysmFloorTimeRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmFloorRowsTest;

	CataclysmKillCounterTest::FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	FWearingPlayer Wearing(Scope.World, BenefitWithNoEffect,
						   TEXT("Negative_Enemies_deal_5_8_increased_damage_for_every_15"));
	if (!TestNotNull(TEXT("a possessed player wearing the row"), Wearing.ASC))
	{
		return false;
	}
	const FName Stat(UCataclysmDamageCalculation::DamageTakenStat);
	const float Normal = UCataclysmDamageCalculation::NormalDamageTaken;

	TestEqual(TEXT("before any floor began: normal damage"),
			  Wearing.ASC->StatForSkill(Stat, FGameplayTagContainer(), Normal), 100.0f, 0.001f);
	Wearing.State->NoteFloorBegan(Scope.World->GetTimeSeconds());
	CataclysmTestWorld::RunClock(Scope.World, 31.0f);
	TestEqual(TEXT("31 seconds into the floor, two steps of 8%: 116"),
			  Wearing.ASC->StatForSkill(Stat, FGameplayTagContainer(), Normal), 116.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorsClearedRowTest,
	"Cataclysm.Enchantments.TheFloorsClearedRowRaisesTheArmorABlowReads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your armor is increased by 1%-2% for every dungeon floor cleared this run",
 * worn at the top on a helm granting 200 armour: two clears add 4 points to
 * armour's increases, and the armour a blow asks for rises with them. Issue
 * #1833, `armor` scaled by `floors_cleared`.
 *
 * THE BLOW'S FIGURE AND NOT THE SHEET'S, ruled 2026-09-30 after this test's first
 * run. The character sheet reads the Armor attribute, and
 * `UCataclysmPlayerClassStats::ApplyTo` resolves each stat with no state, so no
 * refresh can put a state-scaled row on the sheet: a known limit.
 */
bool FCataclysmFloorsClearedRowTest::RunTest(const FString&)
{
	using namespace CataclysmFloorRowsTest;

	CataclysmKillCounterTest::FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	FWearingPlayer Wearing(Scope.World, TEXT("Positive_Your_armor_is_increased_by_1_2_for_every_dunge"),
						   CataclysmEnchantmentEffectTest::DrawbackWithNoEffect);
	if (!TestNotNull(TEXT("a possessed player wearing the row"), Wearing.ASC))
	{
		return false;
	}
	const FName Armor(TEXT("armor"));
	FCataclysmStatBreakdown Before;
	if (!TestTrue(TEXT("armour has a stat line"),
				  Wearing.ASC->StatBreakdownForSkill(Armor, FGameplayTagContainer(), Before)))
	{
		return false;
	}

	Wearing.State->NoteFloorCleared();
	Wearing.State->NoteFloorCleared();

	FCataclysmStatBreakdown After;
	Wearing.ASC->StatBreakdownForSkill(Armor, FGameplayTagContainer(), After);
	TestEqual(TEXT("two clears at 2% a floor: 4 more points of increase"),
			  After.SumOfIncreases - Before.SumOfIncreases, 4.0f, 0.001f);
	TestTrue(*FString::Printf(TEXT("and the armour a blow reads rose: %.2f to %.2f"), Before.Final, After.Final),
			 After.Final > Before.Final + 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRetaliationPerArmorRowTest,
	"Cataclysm.Enchantments.TheRetaliationPerArmorRowCountsWholeHundreds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your retaliation damage is increased by 2%-4% for every 100 points of armor
 * you have", worn at the top: 250 armour is two whole hundreds, 8% increased
 * retaliation. Issue #1833, `retaliation` scaled by the Armor attribute.
 */
bool FCataclysmRetaliationPerArmorRowTest::RunTest(const FString&)
{
	using FWorn = CataclysmSmallHalvesTest::FWorn;

	FWorn Worn(TEXT("Positive_Your_retaliation_damage_is_increased_by_2_4_fo"), true);
	UCataclysmAbilitySystemComponent* ASC = Worn.ASC();
	if (!TestNotNull(TEXT("a wearer"), ASC))
	{
		return false;
	}
	ASC->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 250.0f);
	TestEqual(TEXT("250 armour: 8% increased retaliation"),
			  ASC->IncreasesForStat(FName(TEXT("retaliation")), FGameplayTagContainer()), 0.08f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossesRowTest,
	"Cataclysm.Enchantments.TheBossRowGivesMoreDamagePerUniqueBossDefeated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "For every unique Cataclysm boss defeated, gain 5%-10% more damage
 * permanently", worn at the top: two unique bosses, one of them beaten twice,
 * are 20% more attack damage. Issue #1833, `cataclysm_bosses_defeated`. Through
 * the record alone: it reaches nothing in play until a unique boss exists.
 */
bool FCataclysmBossesRowTest::RunTest(const FString&)
{
	using namespace CataclysmFloorRowsTest;

	CataclysmKillCounterTest::FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	FWearingPlayer Wearing(Scope.World, TEXT("Positive_For_every_unique_Cataclysm_boss_defeated_gain_5"),
						   CataclysmEnchantmentEffectTest::DrawbackWithNoEffect);
	if (!TestNotNull(TEXT("a possessed player wearing the row"), Wearing.ASC))
	{
		return false;
	}
	Wearing.State->RecordCataclysmBossDefeat(FName(TEXT("Test_Boss_A")));
	Wearing.State->RecordCataclysmBossDefeat(FName(TEXT("Test_Boss_B")));
	Wearing.State->RecordCataclysmBossDefeat(FName(TEXT("Test_Boss_A")));

	FCataclysmStatBreakdown Breakdown;
	if (!TestTrue(TEXT("attack damage has a stat line"),
				  Wearing.ASC->StatBreakdownForSkill(FName(TEXT("attack_damage")), FGameplayTagContainer(), Breakdown)))
	{
		return false;
	}
	TestEqual(TEXT("two unique bosses: 20% more"), Breakdown.MoreMultiplier, 1.2f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMaxHealthPerKillAttributeTest,
	"Cataclysm.KillCounter.TheMaxHealthPerKillRowReachesTheHealthBarAtTheNextRegenerationStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your maximum HP is increased by 0.01%-0.05% permanently for every 1000
 * enemies killed this run", worn at the top: 100,000 kills move the MaxHealth
 * ATTRIBUTE, the health bar's figure, at the next regeneration step, to what
 * the stat line answers. Issue #1833 group C part 3c: the row's own test reads
 * `IncreasesForStat` and could not see the bar. The step is what keeps it live
 * (`UCataclysmRegeneration::ApplyStep` and `RefreshLiveMaximumHealth`, issue
 * #1815); a kill refreshes nothing itself.
 */
bool FCataclysmMaxHealthPerKillAttributeTest::RunTest(const FString&)
{
	using namespace CataclysmFloorRowsTest;

	CataclysmKillCounterTest::FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	FWearingPlayer Wearing(Scope.World, TEXT("Positive_Your_maximum_HP_is_increased_by_0_01_0_05_perm"),
						   CataclysmEnchantmentEffectTest::DrawbackWithNoEffect);
	if (!TestNotNull(TEXT("a possessed player wearing the row"), Wearing.ASC))
	{
		return false;
	}
	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const float Before = Wearing.ASC->GetNumericAttribute(MaxHealth);
	for (int32 Kill = 0; Kill < 100000; ++Kill)
	{
		Wearing.State->NoteKill();
	}
	TestEqual(TEXT("kills alone write nothing to the bar"),
			  Wearing.ASC->GetNumericAttribute(MaxHealth), Before, 0.01f);

	UCataclysmRegeneration::ApplyStep(Wearing.Player, 1.0f, 100.0f);
	const float After = Wearing.ASC->GetNumericAttribute(MaxHealth);
	TestTrue(*FString::Printf(TEXT("after a regeneration step the bar rose: %.2f to %.2f"), Before, After),
			 After > Before + 1.0f);
	TestEqual(TEXT("to what the stat line answers"),
			  After, Wearing.ASC->StatForSkill(FName(TEXT("max_health")), FGameplayTagContainer(), After), 0.01f);
	return true;
}


namespace CataclysmRandomDotRowTest
{
	/** Whether this character carries the tag one of the random pool's ailments grants. */
	bool Carries(const UAbilitySystemComponent* Character, const FCataclysmAilmentKind& Kind)
	{
		const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(
			FName(Kind.TagName), /*ErrorIfNotFound=*/false);
		return Character && Tag.IsValid() && Character->HasMatchingGameplayTag(Tag);
	}

	/** A thousand health, so a tenth of the maximum is 100. */
	void GiveAThousand(UAbilitySystemComponent* Character)
	{
		Character->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
		Character->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
	}

	/**
	 * Pins `Cataclysm.RandomDotPick` for the life of this object, at the
	 * console's own priority, restoring the previous value the same way, as
	 * `CataclysmTestWorld::FScopedCritRoll` does: a write from code is discarded
	 * once an earlier test has set the variable at console priority.
	 */
	struct FPinnedPick
	{
		explicit FPinnedPick(int32 Pick)
			: Variable(IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.RandomDotPick")))
		{
			if (Variable)
			{
				Previous = Variable->GetInt();
				Variable->Set(Pick, ECVF_SetByConsole);
			}
		}
		~FPinnedPick()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
		int32 Previous = -1;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCriticalStrikeRandomDotRowTest,
	"Cataclysm.Enchantments.TheCriticalStrikeRandomDotRowAppliesOneAndWaitsAQuarterSecond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Critical strikes apply a random DoT to the target". Issue #1833 group D,
 * ruled 2026-09-30: one of Bleed, Poison, Disease, Necrosis and Burn on the
 * enemy struck, from a blow that took a tenth of its maximum health (#917). Its
 * Trigger Cooldown cell is empty, so the generator wrote the quarter second a
 * critical strike's trigger waits, and a second critical strike in the same
 * moment applies nothing.
 */
bool FCataclysmCriticalStrikeRandomDotRowTest::RunTest(const FString&)
{
	using namespace CataclysmRowsOnlyTest;
	using namespace CataclysmRandomDotRowTest;
	FWorn Worn(TEXT("Positive_Critical_strikes_apply_a_random_DoT_to_the_targe"), true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const TArray<const FCataclysmAilmentKind*> Pool = UCataclysmAilments::RandomDamageOverTimePool();
	if (!TestEqual(TEXT("five ailments to choose among"), Pool.Num(), 5))
	{
		return false;
	}
	const FPinnedPick Pinned(1);
	CataclysmEnchantmentEffectTest::FWearer First(Worn.World);
	CataclysmEnchantmentEffectTest::FWearer Second(Worn.World);
	GiveAThousand(First.AbilitySystem);
	GiveAThousand(Second.AbilitySystem);
	const FName Crit(TEXT("critical_strike"));

	Worn.ASC()->ActOnEvent(Crit, nullptr, 100.0f, true, First.Actor);
	TestTrue(TEXT("a critical strike taking a tenth applies the pinned Poison"),
		Carries(First.AbilitySystem, *Pool[1]));
	Worn.ASC()->ActOnEvent(Crit, nullptr, 100.0f, true, Second.Actor);
	TestFalse(TEXT("a second in the same moment applies nothing: the row waits"),
		Carries(Second.AbilitySystem, *Pool[1]));
	Worn.World->TimeSeconds += 0.25f;
	Worn.ASC()->ActOnEvent(Crit, nullptr, 100.0f, true, Second.Actor);
	TestTrue(TEXT("a quarter of a second later it applies again"),
		Carries(Second.AbilitySystem, *Pool[1]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRetaliationRandomDotRowTest,
	"Cataclysm.Enchantments.TheRetaliationRandomDotRowAppliesOneToTheAttacker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your retaliation damage also applies a random DoT to attackers". Issue #1833
 * group D: `retaliation_dealt`, raised by `UCataclysmRetaliation::Pay` for each
 * attacker paid, with what reached its health.
 */
bool FCataclysmRetaliationRandomDotRowTest::RunTest(const FString&)
{
	using namespace CataclysmRowsOnlyTest;
	using namespace CataclysmRandomDotRowTest;
	FWorn Worn(TEXT("Positive_Your_retaliation_damage_also_applies_a_random_Do"), true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const TArray<const FCataclysmAilmentKind*> Pool = UCataclysmAilments::RandomDamageOverTimePool();
	if (!TestEqual(TEXT("five ailments to choose among"), Pool.Num(), 5))
	{
		return false;
	}
	// RETALIATION OF A HUNDRED PER CENT, AS A STAT LINE AND AN ATTRIBUTE. The
	// refresh leaves stat lines that a reader may ask before the attribute,
	// which is why both are written. The row is an action, kept apart from the
	// stat lines, so replacing them does not remove it.
	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.FindOrAdd(FName(UCataclysmRetaliation::AmountStat)).Base = 100.0f;
	Worn.ASC()->SetStatInputs(MoveTemp(Lines));
	Worn.ASC()->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetRetaliationAttribute(), 100.0f);

	const FPinnedPick Pinned(3);
	CataclysmEnchantmentEffectTest::FWearer Attacker(Worn.World);
	GiveAThousand(Attacker.AbilitySystem);
	const float Paid = UCataclysmRetaliation::Pay(
		Worn.ASC(), Worn.Wearer->Actor, Attacker.Actor, 500.0f);
	TestTrue(*FString::Printf(TEXT("a blow worth 500 paid at least a tenth back: %.1f"), Paid),
		Paid >= 100.0f);
	TestTrue(TEXT("and the attacker carries the pinned Necrosis"),
		Carries(Attacker.AbilitySystem, *Pool[3]));
	return true;
}


namespace CataclysmHealthThresholdRowTest
{
	/** Ten bases in ten different slots, so a whole set can be worn. */
	const TCHAR* const TenBases[] = {
		TEXT("Head_Helm"), TEXT("Chest_Cuirass"), TEXT("Shoulders_Pauldrons"),
		TEXT("Gloves_Gauntlets"), TEXT("Pants_Greaves"), TEXT("Boots_Sabatons"),
		TEXT("Belt_Girdle"), TEXT("Necklace_Amulet"), TEXT("Relic_Idol"), TEXT("Ring_Band")};

	/** Wear `Pieces` items of the set whose first bonus is `SetBonus`, then refresh. */
	void WearSet(CataclysmEnchantmentEffectTest::FWearer& Wearer, const TCHAR* SetBonus, int32 Pieces)
	{
		for (int32 Index = 0; Index < Pieces; ++Index)
		{
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
			Wearer.Equipment->Equip(CataclysmEnchantmentEffectTest::Carrying(
				TenBases[Index], SetBonus, CataclysmEnchantmentEffectTest::DrawbackWithNoEffect),
				Removed, AlsoRemoved, Slot);
		}
		Wearer.Equipment->RefreshAttributes(Wearer.AbilitySystem);
	}

	/** Maximum first, then current: the vital set clamps health to it. */
	void SetHealth(UAbilitySystemComponent* Character, float Maximum, float Current)
	{
		Character->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Maximum);
		Character->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Current);
	}

	float HealthOf(const UAbilitySystemComponent* Character)
	{
		return Character->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArchonsAegisRowTest,
	"Cataclysm.Enchantments.ArchonsAegisTenPiecesSaveALethalBlowAndHealToFullOnceInTenMinutes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Archon's Aegis (10-Piece Bonus): When your health falls below 10%, you are
 * instantly healed to 100% of your maximum health. (10 minute cd)". Issue #1833
 * group D part 2, ruled 2026-09-30: it saves a lethal blow too, and the heal is
 * an ordinary heal (`TopUp`). Nine pieces do not save.
 */
bool FCataclysmArchonsAegisRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmHealthThresholdRowTest;
	const TCHAR* Aegis = TEXT("Positive_Archon_s_Aegis_2_Piece_Bonus_Your_block_chanc");

	for (const int32 Pieces : {9, 10})
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		FWearer Wearer(World);
		WearSet(Wearer, Aegis, Pieces);
		SetHealth(Wearer.AbilitySystem, 1000.0f, 1000.0f);

		FWearer Attacker(World);
		Attacker.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 5000.0f);
		// THE LOWEST HEALTH REACHED, heard as it changes: the end value alone
		// cannot show a save, because a heal on the fall refills health either way.
		float Lowest = TNumericLimits<float>::Max();
		const FDelegateHandle Watching = Wearer.AbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UCataclysmVitalAttributeSet::GetHealthAttribute()).AddLambda(
			[&Lowest](const FOnAttributeChangeData& Change) { Lowest = FMath::Min(Lowest, Change.NewValue); });
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Wearer.Actor, /*DamagePercent=*/100.0f);
		Wearer.AbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UCataclysmVitalAttributeSet::GetHealthAttribute()).Remove(Watching);
		if (Pieces < 10)
		{
			TestEqual(TEXT("nine pieces: the blow reaches nought"), Lowest, 0.0f, 0.01f);
			TestEqual(TEXT("nine pieces: the blow kills"), HealthOf(Wearer.AbilitySystem), 0.0f, 0.01f);
			continue;
		}
		TestEqual(TEXT("ten pieces: the blow never takes the wearer below one point"), Lowest, 1.0f, 0.01f);
		TestEqual(TEXT("ten pieces: the blow is saved and healed to full"),
			HealthOf(Wearer.AbilitySystem), 1000.0f, 0.01f);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Wearer.Actor, /*DamagePercent=*/100.0f);
		TestEqual(TEXT("a second inside ten minutes kills"), HealthOf(Wearer.AbilitySystem), 0.0f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonKingsRegaliaRowTest,
	"Cataclysm.Enchantments.DemonKingsRegaliaSixPiecesGrantTenSecondsOfImmunityOnceInFiveMinutes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Demon King's Regalia (6-Piece Bonus): When your health falls below 25%, you
 * enter a 'Demonic Rage' becoming immune to all crowd control and dealing 100%
 * more damage for 10 seconds. (5 minute cd)". Issue #1833 group D part 2: three
 * own-stack rows on `health_falls_below` at 25, each lasting 10 seconds with a
 * stated 300 second cooldown. Read through the immunity row.
 */
bool FCataclysmDemonKingsRegaliaRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmHealthThresholdRowTest;
	const TCHAR* Regalia = TEXT("Positive_Demon_King_s_Regalia_2_Piece_Bonus_You_deal_2");

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);
	WearSet(Wearer, Regalia, 6);
	SetHealth(Wearer.AbilitySystem, 1000.0f, 1000.0f);
	const auto Immunity = [&Wearer]()
	{
		return Wearer.AbilitySystem->StatForSkill(
			FName(TEXT("crowd_control_resistance")), FGameplayTagContainer(), 0.0f);
	};

	// THE WEARER'S OWN RESISTANCE FIRST, with no stack held. The refresh gives it
	// a crowd_control_resistance line of its own -- 7.85 when this test was
	// written -- and the first run of this test assumed none. Asserting that no
	// stack is held when the base is read stops a stack hiding inside it, which
	// would let every "+100" check below pass while measuring nothing.
	int32 StacksAtFull = -1;
	for (const FCataclysmPoolAction& Action : Wearer.AbilitySystem->GetPoolActions())
	{
		if (!Action.StackKey.IsNone())
		{
			StacksAtFull = FMath::Max(StacksAtFull, 0) + Wearer.AbilitySystem->OwnStacksHeld(Action.StackKey);
		}
	}
	if (!TestEqual(TEXT("at full health the Regalia holds no stack"), StacksAtFull, 0))
	{
		return false;
	}
	const float Base = Immunity();

	SetHealth(Wearer.AbilitySystem, 1000.0f, 200.0f);
	TestEqual(TEXT("falling below 25%: immune, 100 above its own"), Immunity(), Base + 100.0f, 0.01f);
	World->TimeSeconds += 11.0f;
	TestEqual(TEXT("eleven seconds later: lapsed to its own"), Immunity(), Base, 0.01f);
	SetHealth(Wearer.AbilitySystem, 1000.0f, 800.0f);
	SetHealth(Wearer.AbilitySystem, 1000.0f, 200.0f);
	TestEqual(TEXT("a second fall inside five minutes: nothing added"), Immunity(), Base, 0.01f);
	World->TimeSeconds += 300.0f;
	SetHealth(Wearer.AbilitySystem, 1000.0f, 800.0f);
	SetHealth(Wearer.AbilitySystem, 1000.0f, 200.0f);
	TestEqual(TEXT("after five minutes: immune again, 100 above its own"),
		Immunity(), Base + 100.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorStartRowTest,
	"Cataclysm.Enchantments.TheFloorStartRowLowersHealthToItsRolledShareThroughARealFloorChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "You start every dungeon floor at 30%-50% of your maximum HP". Issue #1833
 * group D part 2: `health_capped_at` 30 to 50 on `floor_start`.
 *
 * THE PLAY PATH, MEASURED: the row is WORN by a possessed player and the floor
 * is entered through the real `GoToFloor`. The floor's rules refresh the
 * player's equipment, which rebuilds the action list from what is worn, before
 * `floor_start` is raised, so this is the test that the worn row survives that
 * refresh and caps. Read against the roll the item gave the row.
 */
bool FCataclysmFloorStartRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("the dungeon game mode"), Mode)
		|| !TestNotNull(TEXT("a player state"), PlayerState)
		|| !TestNotNull(TEXT("a controller"), Controller)
		|| !TestNotNull(TEXT("a player character"), Character))
	{
		return false;
	}
	Controller->SetPlayerState(PlayerState);
	Controller->Possess(Character);
	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(Character->GetAbilitySystemComponent());
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	if (!TestNotNull(TEXT("the player's ability system"), AbilitySystem)
		|| !TestNotNull(TEXT("the player's equipment"), Equipment))
	{
		return false;
	}

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Equipment->Equip(Carrying(TEXT("Head_Helm"), BenefitWithNoEffect,
		TEXT("Negative_You_start_every_dungeon_floor_at_30_50_of_your")),
		Removed, AlsoRemoved, Slot);
	Equipment->RefreshAttributes(AbilitySystem);
	float Share = -1.0f;
	for (const FCataclysmPoolAction& Action : AbilitySystem->GetPoolActions())
	{
		if (Action.bHealthCap)
		{
			Share = Action.Percent;
		}
	}
	if (!TestTrue(*FString::Printf(TEXT("the worn row gave a cap between 30 and 50: %.2f"), Share),
			Share >= 30.0f && Share <= 50.0f))
	{
		return false;
	}

	// FULL, AT THE MAXIMUM AS IT STANDS.
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	AbilitySystem->SetNumericAttributeBase(Health, AbilitySystem->GetNumericAttribute(MaxHealth));

	Mode->EnemyScale = 0.1f;
	if (!TestTrue(TEXT("the floor was reached"), Mode->GoToFloor(1)))
	{
		return false;
	}
	const float Maximum = AbilitySystem->GetNumericAttribute(MaxHealth);
	TestTrue(TEXT("the player has a maximum"), Maximum > 0.0f);
	TestEqual(TEXT("a full player wearing the row starts the floor at its share"),
		AbilitySystem->GetNumericAttribute(Health), Maximum * Share / 100.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDivineRetributionRowTest,
	"Cataclysm.Enchantments.DivineRetributionSixPiecesSmiteNearbyEnemiesWhenTheShieldBreaks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Divine Retribution (6-Piece Bonus): When your energy shield is broken, you
 * smite all nearby enemies". Issue #1833 group D part 3: `smite_nearby` 100 on
 * `energy_shield_broken`, the 2026-09-11 judgement "a nova at 100% of weapon
 * damage" within five metres. THROUGH A REAL BLOW that empties the wearer's
 * shield. Five pieces do not smite.
 */
bool FCataclysmDivineRetributionRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmHealthThresholdRowTest;
	const TCHAR* Retribution = TEXT("Positive_Divine_Retribution_2_Piece_Bonus_Your_energy");

	for (const int32 Pieces : {5, 6})
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		FWearer Wearer(World);
		WearSet(Wearer, Retribution, Pieces);
		SetHealth(Wearer.AbilitySystem, 1000.0f, 1000.0f);
		Wearer.AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 100.0f);
		Wearer.AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 100.0f);
		Wearer.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 200.0f);
		if (!TestEqual(TEXT("set-up: the wearer holds a hundred shield"),
				Wearer.AbilitySystem->GetNumericAttribute(
					UCataclysmVitalAttributeSet::GetEnergyShieldAttribute()), 100.0f, 0.01f))
		{
			return false;
		}

		ACataclysmEnemyCharacter* Near = World->SpawnActor<ACataclysmEnemyCharacter>(
			Wearer.Actor->GetActorLocation() + FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("an enemy three metres away"), Near))
		{
			return false;
		}
		Near->SetHealth(1000.0f);
		const UAbilitySystemComponent* NearSystem = UCataclysmTargeting::AbilitySystemOf(Near);
		if (!TestNotNull(TEXT("the enemy's ability system"), NearSystem))
		{
			return false;
		}
		const float Before = HealthOf(NearSystem);

		FWearer Attacker(World);
		Attacker.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 500.0f);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Wearer.Actor, /*DamagePercent=*/100.0f);
		if (!TestEqual(TEXT("set-up: the blow emptied the shield"),
				Wearer.AbilitySystem->GetNumericAttribute(
					UCataclysmVitalAttributeSet::GetEnergyShieldAttribute()), 0.0f, 0.01f))
		{
			return false;
		}
		if (Pieces < 6)
		{
			TestEqual(TEXT("five pieces: the enemy nearby is not smitten"), HealthOf(NearSystem), Before, 0.01f);
			continue;
		}
		TestTrue(*FString::Printf(TEXT("six pieces: the enemy nearby is smitten: %.2f to %.2f"),
			Before, HealthOf(NearSystem)), HealthOf(NearSystem) < Before);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathHealsEnemiesRowTest,
	"Cataclysm.Enchantments.TheDeathRowHealsNearbyEnemiesByItsRolledShareThroughARealDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "On death all nearby enemies are healed for 10%-20% of their maximum HP".
 * Issue #1833 group D part 3: `heal_nearby_enemies` 10 to 20 on `player_death`.
 * WORN BY A PLAYER who dies through a real blow, read against the roll the item
 * gave the row.
 */
bool FCataclysmDeathHealsEnemiesRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Killer = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a player state"), PlayerState)
		|| !TestNotNull(TEXT("a player character"), Character)
		|| !TestNotNull(TEXT("an enemy three metres away"), Killer))
	{
		return false;
	}
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(Character->GetAbilitySystemComponent());
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	if (!TestNotNull(TEXT("the player's ability system"), AbilitySystem)
		|| !TestNotNull(TEXT("the player's equipment"), Equipment))
	{
		return false;
	}

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Equipment->Equip(Carrying(TEXT("Head_Helm"), BenefitWithNoEffect,
		TEXT("Negative_On_death_all_nearby_enemies_are_healed_for_10_2")),
		Removed, AlsoRemoved, Slot);
	Equipment->RefreshAttributes(AbilitySystem);
	float Share = -1.0f;
	for (const FCataclysmPoolAction& Action : AbilitySystem->GetPoolActions())
	{
		if (Action.Nearby == ECataclysmNearbyAction::HealEnemies)
		{
			Share = Action.Percent;
		}
	}
	if (!TestTrue(*FString::Printf(TEXT("the worn row gave a heal between 10 and 20: %.2f"), Share),
			Share >= 10.0f && Share <= 20.0f))
	{
		return false;
	}

	Killer->SetHealth(1000.0f);
	UAbilitySystemComponent* KillerSystem = UCataclysmTargeting::AbilitySystemOf(Killer);
	if (!TestNotNull(TEXT("the enemy's ability system"), KillerSystem))
	{
		return false;
	}
	KillerSystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 500.0f);

	UCataclysmSkillEffects::ApplyDirectDamage(Killer, Character, 100000.0f);
	if (!TestTrue(TEXT("the player died"), UCataclysmSkillEffects::IsDead(Character)))
	{
		return false;
	}
	TestEqual(TEXT("the enemy nearby is healed by the rolled share of its maximum"),
		KillerSystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()),
		500.0f + 1000.0f * Share / 100.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathDealsRemainingRowTest,
	"Cataclysm.Enchantments.TheDeathRowDealsTheRemainingDamageOfTheWearersDoTsThroughARealDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "When you die, all active DoTs on nearby enemies instantly deal their remaining
 * damage". Issue #1833 group D part 4: `dot_remaining_nearby` 100 on
 * `player_death`. WORN BY A PLAYER whose poison has run two of its ten ticks on an
 * enemy three metres away; the player dies through a real blow, and the enemy
 * takes the eight ticks left at once and the poison ends.
 */
bool FCataclysmDeathDealsRemainingRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Near = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	const FGameplayTag Poison = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Keyword.DoT.Poison")), /*ErrorIfNotFound=*/false);
	if (!TestNotNull(TEXT("a player state"), PlayerState)
		|| !TestNotNull(TEXT("a player character"), Character)
		|| !TestNotNull(TEXT("an enemy three metres away"), Near)
		|| !TestTrue(TEXT("the poison tag exists"), Poison.IsValid()))
	{
		return false;
	}
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();
	Near->SetHealth(1000.0f);
	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(Character->GetAbilitySystemComponent());
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	if (!TestNotNull(TEXT("the player's ability system"), AbilitySystem)
		|| !TestNotNull(TEXT("the player's equipment"), Equipment))
	{
		return false;
	}

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Equipment->Equip(Carrying(TEXT("Head_Helm"),
		TEXT("Positive_When_you_die_all_active_DoTs_on_nearby_enemies"), DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Equipment->RefreshAttributes(AbilitySystem);
	float Share = -1.0f;
	for (const FCataclysmPoolAction& Action : AbilitySystem->GetPoolActions())
	{
		if (Action.RemainingDamage == ECataclysmRemainingDamage::Nearby)
		{
			Share = Action.Percent;
		}
	}
	if (!TestEqual(TEXT("the worn row deals all of the remaining damage"), Share, 100.0f, 0.01f)
		|| !TestTrue(TEXT("the player poisons the enemy"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Character, Near, /*DamagePerTick=*/10.0f,
				/*DurationSeconds=*/10.0f, Poison, /*bScalesWithInstigator=*/false)))
	{
		return false;
	}

	CataclysmTestWorld::RunClock(World, 2.5f);
	const UAbilitySystemComponent* NearSystem = UCataclysmTargeting::AbilitySystemOf(Near);
	const float Before = NearSystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	const float EachTick = (1000.0f - Before) / 2.0f;
	if (!TestTrue(*FString::Printf(TEXT("two ticks landed, %.2f each"), EachTick), EachTick > 0.0f))
	{
		return false;
	}

	UCataclysmSkillEffects::ApplyDirectDamage(Near, Character, 100000.0f);
	if (!TestTrue(TEXT("the player died"), UCataclysmSkillEffects::IsDead(Character)))
	{
		return false;
	}
	TestEqual(TEXT("the enemy takes the eight ticks left at once"),
		NearSystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()),
		Before - 8.0f * EachTick, 0.05f);
	TestEqual(TEXT("and the poison ends"),
		NearSystem->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
			FGameplayTagContainer(Poison))).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecrosisShareRowTest,
	"Cataclysm.Enchantments.TheNecrosisRowDealsItsRolledShareOfTheTargetsNecrosisOnACriticalStrike",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Necrosis effects deal 20%-40% of their remaining damage instantly when you
 * land a critical strike". Issue #1833 group D part 4: `dot_remaining_target` 20
 * to 40 on `critical_strike`, Ailment Necrosis. WORN BY A PLAYER who lands a REAL
 * critical strike through the damage path, with the roll pinned at the console's
 * own priority, so `critical_strike` is raised by the game with the creature
 * struck: the coordinating session's condition of 2026-10-01, because a hand-raised
 * event hides the play path.
 *
 * THE BLOW DEALS DAMAGE OF ITS OWN, so a CONTROL creature carrying no Necrosis takes
 * the same critical blow first, and what the Necrosis creature loses beyond that is
 * the burst: the rolled share of the eight ticks it has left, each worth what one of
 * its two landed ticks took. The Necrosis keeps all of its remaining damage.
 */
bool FCataclysmNecrosisShareRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayTag Necrosis = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Keyword.DoT.Necrosis")), /*ErrorIfNotFound=*/false);
	if (!TestTrue(TEXT("the Necrosis tag exists"), Necrosis.IsValid()))
	{
		return false;
	}

	UCataclysmCombatEvents::In(World);
	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a player state"), PlayerState)
		|| !TestNotNull(TEXT("a player character"), Character))
	{
		return false;
	}
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(Character->GetAbilitySystemComponent());
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	if (!TestNotNull(TEXT("the player's ability system"), AbilitySystem)
		|| !TestNotNull(TEXT("the player's equipment"), Equipment))
	{
		return false;
	}

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Equipment->Equip(Carrying(TEXT("Head_Helm"),
		TEXT("Positive_Necrosis_effects_deal_20_40_of_their_remaining"), DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Equipment->RefreshAttributes(AbilitySystem);
	float Share = -1.0f;
	FGameplayTag Ailment;
	for (const FCataclysmPoolAction& Action : AbilitySystem->GetPoolActions())
	{
		if (Action.RemainingDamage == ECataclysmRemainingDamage::Target)
		{
			Share = Action.Percent;
			Ailment = Action.Ailment;
		}
	}
	if (!TestTrue(*FString::Printf(TEXT("the worn row gave a share between 20 and 40: %.2f"), Share),
			Share >= 20.0f && Share <= 40.0f)
		|| !TestTrue(TEXT("and limits it to Necrosis"), Ailment == Necrosis))
	{
		return false;
	}

	// THE STAT LINES A BLOW READS, written after the refresh, which leaves a
	// crit_chance line of base 0 that a blow asks before the attribute; the same
	// trap and the same answer as the player critical strike test in
	// CataclysmPlayerMovementTests.cpp. The worn row is an action, kept apart from
	// the stat lines, so replacing them does not remove it.
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		Lines.FindOrAdd(FName(TEXT("crit_chance"))).Base = 50.0f;
		Lines.FindOrAdd(FName(TEXT("crit_multiplier"))).Base = 150.0f;
		Lines.FindOrAdd(FName(TEXT("attack_damage"))).Base = 300.0f;
		AbilitySystem->SetStatInputs(MoveTemp(Lines));
	}
	AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 300.0f);
	AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 50.0f);
	AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetCritMultiplierAttribute(), 150.0f);

	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	// `SetHealth` SETS THE STARTING MAXIMUM, and the current value follows it, so a
	// later `ApplyStartingAttributes` writes the same thousand back rather than the
	// class's default, which the maximum-health clamp of #2193 would take health
	// down to.
	const auto MakeCreature = [World](float AlongMetres) -> UAbilitySystemComponent*
	{
		ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(AlongMetres * 100.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Creature)
		{
			Creature->SetHealth(1000.0f);
		}
		return UCataclysmTargeting::AbilitySystemOf(Creature);
	};
	UAbilitySystemComponent* Rotting = MakeCreature(2.0f);
	UAbilitySystemComponent* Control = MakeCreature(-2.0f);
	if (!TestNotNull(TEXT("a creature carrying the Necrosis"), Rotting)
		|| !TestNotNull(TEXT("a control creature carrying none"), Control)
		|| !TestTrue(TEXT("the player's Necrosis lands"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Character, Rotting->GetAvatarActor(),
				/*DamagePerTick=*/10.0f, /*DurationSeconds=*/10.0f, Necrosis,
				/*bScalesWithInstigator=*/false)))
	{
		return false;
	}
	// THE CONTROL MATCHES THE CREATURE IT STANDS IN FOR: one class, built the same
	// way, at the same rarity step and armour, so the difference between their losses
	// is the burst and nothing about the two creatures. A bare SpawnActor draws no
	// rarity; only the dungeon game mode does, which is why both read 0.
	const ACataclysmEnemyCharacter* RottingBody = Cast<ACataclysmEnemyCharacter>(Rotting->GetAvatarActor());
	const ACataclysmEnemyCharacter* ControlBody = Cast<ACataclysmEnemyCharacter>(Control->GetAvatarActor());
	if (!TestTrue(TEXT("the two creatures are one class at one rarity step"),
			RottingBody && ControlBody && RottingBody->GetClass() == ControlBody->GetClass()
			&& RottingBody->RarityStep == ControlBody->RarityStep)
		|| !TestEqual(TEXT("and carry the same armour"),
			Rotting->GetNumericAttribute(UCataclysmCombatAttributeSet::GetArmorAttribute()),
			Control->GetNumericAttribute(UCataclysmCombatAttributeSet::GetArmorAttribute()), 0.001f))
	{
		return false;
	}
	CataclysmTestWorld::RunClock(World, 2.5f);
	const float Before = Rotting->GetNumericAttribute(Health);
	const float EachTick = (1000.0f - Before) / 2.0f;
	if (!TestTrue(*FString::Printf(TEXT("two ticks landed, %.2f each"), EachTick), EachTick > 0.0f))
	{
		return false;
	}

	// A ROLL OF 0 ALWAYS CRITICALLY STRIKES. The Necrosis creature is struck first,
	// so the row's quarter second cooldown starts on the blow that matters.
	{
		const CataclysmTestWorld::FScopedCritRoll AlwaysCritical(0.0f);
		UCataclysmSkillEffects::ApplyHit(Character, Rotting->GetAvatarActor(), /*DamagePercent=*/100.0f);
		UCataclysmSkillEffects::ApplyHit(Character, Control->GetAvatarActor(), /*DamagePercent=*/100.0f);
	}
	const float BlowAlone = 1000.0f - Control->GetNumericAttribute(Health);
	if (!TestTrue(*FString::Printf(TEXT("the critical blow alone took %.2f from the control"), BlowAlone),
			BlowAlone > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("beyond the blow, the Necrosis creature lost the rolled share of its eight ticks left"),
		Before - Rotting->GetNumericAttribute(Health) - BlowAlone, Share / 100.0f * 8.0f * EachTick, 0.05f);
	const TArray<FActiveGameplayEffectHandle> Still = Rotting->GetActiveEffects(
		FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Necrosis)));
	TestEqual(TEXT("the Necrosis still runs"), Still.Num(), 1);
	if (Still.Num() == 1)
	{
		TestEqual(TEXT("with all eight of its ticks of ten left"),
			UCataclysmSkillEffects::RemainingDamageOverTime(Rotting, Still[0]), 80.0f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGadgetDestroyedRowTest,
	"Cataclysm.Enchantments.AKilledGadgetGivesTheWearersGadgetsItsRolledDamageForFiveSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "When any of your gadgets is destroyed, all remaining gadgets gain 30%-50%
 * increased damage for 5 seconds". Issue #1833 group D part 5: two own stacks,
 * attack and spell damage, scoped to `Type.Deployable`, 5 s, cap 1, on
 * `gadget_destroyed`. WORN, and a real spike trap of the wearer's killed by a real
 * blow: the wearer's damage for a deployable rises by the rolled share, and for
 * anything else does not, and it lapses after five seconds.
 */
bool FCataclysmGadgetDestroyedRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(Carrying(TEXT("Head_Helm"),
		TEXT("Positive_When_any_of_your_gadgets_is_destroyed_all_remai"), DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(Wearer.AbilitySystem);

	const FGameplayTag Deployable = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Type.Deployable")), /*ErrorIfNotFound=*/false);
	if (!TestTrue(TEXT("the deployable tag exists"), Deployable.IsValid()))
	{
		return false;
	}
	const FGameplayTagContainer Gadget(Deployable);
	const FName Attack(TEXT("attack_damage"));
	const float GadgetBefore = Wearer.AbilitySystem->IncreasesForStat(Attack, Gadget);
	const float PlainBefore = Wearer.AbilitySystem->IncreasesForStat(Attack, FGameplayTagContainer());

	ACataclysmMinion* Trap = ACataclysmMinion::Spawn(Wearer.Actor, FVector(300.0f, 0.0f, 0.0f),
		/*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("SpikeTrap"));
	ON_SCOPE_EXIT { if (IsValid(Trap)) { Trap->Destroy(); } };
	FWearer Killer(World);
	if (!TestNotNull(TEXT("a spike trap of the wearer's"), Trap))
	{
		return false;
	}
	// THE BLOW BELOW CANNOT BE DODGED: a minion holds no combat attribute set, and
	// the damage calculation rolls evasion only against a defender holding one.
	const UAbilitySystemComponent* TrapSystem = UCataclysmTargeting::AbilitySystemOf(Trap);
	if (!TestTrue(TEXT("the trap holds no combat set, so it cannot evade"),
			TrapSystem && TrapSystem->GetSet<UCataclysmCombatAttributeSet>() == nullptr))
	{
		return false;
	}
	UCataclysmSkillEffects::ApplyDirectDamage(Killer.Actor, Trap, 100000.0f);
	if (!TestTrue(TEXT("the trap died"), UCataclysmSkillEffects::IsDead(Trap)))
	{
		return false;
	}

	const float Gained = Wearer.AbilitySystem->IncreasesForStat(Attack, Gadget) - GadgetBefore;
	TestTrue(*FString::Printf(TEXT("a deployable's damage rose by the rolled 30%% to 50%%: %.3f"), Gained),
		Gained >= 0.30f - 0.0001f && Gained <= 0.50f + 0.0001f);
	TestEqual(TEXT("and damage for anything else did not"),
		Wearer.AbilitySystem->IncreasesForStat(Attack, FGameplayTagContainer()), PlainBefore, 0.0001f);
	World->TimeSeconds += 6.0f;
	TestEqual(TEXT("six seconds later it has lapsed"),
		Wearer.AbilitySystem->IncreasesForStat(Attack, Gadget), GadgetBefore, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmResourceConsumedRowsTest,
	"Cataclysm.Enchantments.AnUltimatesFervourCostHealsAndRaisesDamageThroughBothConsumedRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Consuming a resource stack or charge grants 5%-10% increased damage for 3
 * seconds" and "Each resource or charge consumed restores 1%-3% of your maximum
 * HP". Issue #1833 group D part 5: own stacks and a health pool action on
 * `resource_consumed`. WORN, and a real Ultimate cast paying its fifty Fervour:
 * the wearer is healed by the rolled share of its maximum, once, and its damage
 * rises by the rolled share.
 */
bool FCataclysmResourceConsumedRowsTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWearer Wearer(World);
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(Carrying(TEXT("Head_Helm"),
		TEXT("Positive_Consuming_a_resource_stack_or_charge_grants_5_1"), DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->Equip(Carrying(TEXT("Chest_Cuirass"),
		TEXT("Positive_Each_resource_or_charge_consumed_restores_1_3"), DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(Wearer.AbilitySystem);
	float Heal = -1.0f;
	for (const FCataclysmPoolAction& Action : Wearer.AbilitySystem->GetPoolActions())
	{
		if (Action.Event == FName(TEXT("resource_consumed")) && Action.Pool == FName(TEXT("health")))
		{
			Heal = Action.Percent;
		}
	}
	if (!TestTrue(*FString::Printf(TEXT("the worn heal rolled 1 to 3: %.2f"), Heal), Heal >= 1.0f && Heal <= 3.0f))
	{
		return false;
	}

	UAbilitySystemComponent* System = Wearer.AbilitySystem;
	System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
	System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 500.0f);
	System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
	System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
	System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
	System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 80.0f);
	const FName Attack(TEXT("attack_damage"));
	const float DamageBefore = Wearer.AbilitySystem->IncreasesForStat(Attack, FGameplayTagContainer());

	const FGameplayAbilitySpecHandle Handle = Wearer.AbilitySystem->GiveAbilityInSlot(
		UCataclysmSelfBuffSkill::StaticClass(), ECataclysmAbilitySlot::Ultimate, /*Level=*/100, Wearer.Actor);
	FGameplayAbilitySpec* Spec = Wearer.AbilitySystem->FindAbilitySpecFromHandle(Handle);
	UCataclysmSelfBuffSkill* Ultimate = Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("an Ultimate"), Ultimate))
	{
		return false;
	}
	Ultimate->SkillName = TEXT("Test Skill");
	Ultimate->Params = UCataclysmSkillShapes::ParseParams(TEXT("Duration=6"));
	if (!TestTrue(TEXT("the Ultimate is cast"),
			Wearer.AbilitySystem->TryActivateAbility(Handle, /*bAllowRemoteActivation=*/false)))
	{
		return false;
	}
	if (!TestEqual(TEXT("it paid its fifty Fervour"),
			System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute()),
			30.0f, 0.001f))
	{
		return false;
	}

	TestEqual(TEXT("the payment healed the rolled share of 1000, once"),
		System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()),
		500.0f + 1000.0f * Heal / 100.0f, 0.01f);
	const float Gained = Wearer.AbilitySystem->IncreasesForStat(Attack, FGameplayTagContainer()) - DamageBefore;
	TestTrue(*FString::Printf(TEXT("and damage rose by the rolled 5%% to 10%%: %.3f"), Gained),
		Gained >= 0.05f - 0.0001f && Gained <= 0.10f + 0.0001f);
	return true;
}

namespace CataclysmApplyStatusRowTest
{
	/**
	 * Pins one console variable for the life of this object, at the console's
	 * own priority, restoring the previous value the same way, as
	 * `CataclysmTestWorld::FScopedCritRoll` does.
	 */
	struct FPinned
	{
		FPinned(const TCHAR* Name, float Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}
		~FPinned()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};

	FGameplayTag TagNamed(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	bool Carries(const AActor* Actor, const FGameplayTag& Granted)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System && Granted.IsValid() && System->HasMatchingGameplayTag(Granted);
	}

	/** The longest time left on anything granting `Granted` on `Actor`, or zero. */
	float SecondsLeftOn(const AActor* Actor, const FGameplayTag& Granted)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		float Longest = 0.0f;
		if (System && Granted.IsValid())
		{
			for (const float Seconds : System->GetActiveEffectsTimeRemaining(
					 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Granted))))
			{
				Longest = FMath::Max(Longest, Seconds);
			}
		}
		return Longest;
	}

	/** A blow of the striker's character on `Target`: critical or not, and a Strike skill's or not. */
	float Blow(CataclysmConsecutiveRowTest::FStriker& Striker, ACataclysmEnemyCharacter* Target,
		bool bCritical, bool bStrike)
	{
		const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
		UAbilitySystemComponent* Its = Target->GetAbilitySystemComponent();
		const float Before = Its->GetNumericAttribute(Health);
		FCataclysmHitDelivery Delivery;
		Delivery.CritChancePercent = bCritical ? 100.0f : 0.0f;
		if (bStrike)
		{
			Delivery.Skill = Striker.Swing;
		}
		const CataclysmTestWorld::FScopedCritRoll Roll(0.0f);
		UCataclysmSkillEffects::ApplyHit(Striker.Character, Target, /*DamagePercent=*/100.0f,
			bStrike ? Striker.Swing->SkillTags : FGameplayTagContainer(), Delivery);
		return Before - Its->GetNumericAttribute(Health);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCriticalBleedRowTest,
	"Cataclysm.Enchantments.TheCriticalBleedRowBleedsWhatARealCriticalStrikeHits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your critical strikes always cause bleeding". Issue #1833 group E part 1: a
 * status action on `critical_strike`, Bleed, 100. WORN by a real player, whose
 * real critical strike on a creature bleeds it, and whose ordinary blow on
 * another bleeds nothing. Each blow takes over a tenth of the creature's ten
 * thousand, so the owner's rule of #917 is met and is not what decides it.
 */
bool FCataclysmCriticalBleedRowTest::RunTest(const FString&)
{
	using namespace CataclysmApplyStatusRowTest;
	CataclysmConsecutiveRowTest::FStriker Striker(
		TEXT("Positive_Your_critical_strikes_always_cause_bleeding"),
		CataclysmEnchantmentEffectTest::DrawbackWithNoEffect);
	const FGameplayTag Bleed = TagNamed(TEXT("Keyword.DoT.Bleed"));
	if (!TestTrue(TEXT("a striker and two creatures"), Striker.Ready())
		|| !TestTrue(TEXT("the bleed tag exists"), Bleed.IsValid()))
	{
		return false;
	}
	Striker.ASC->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 3000.0f);

	const float Plain = Blow(Striker, Striker.Second, /*bCritical=*/false, /*bStrike=*/false);
	if (!TestTrue(*FString::Printf(TEXT("an ordinary blow took over a tenth: %.1f"), Plain), Plain >= 1000.0f))
	{
		return false;
	}
	TestFalse(TEXT("an ordinary blow bleeds nothing"), Carries(Striker.Second, Bleed));

	const float Critical = Blow(Striker, Striker.First, /*bCritical=*/true, /*bStrike=*/false);
	if (!TestTrue(*FString::Printf(TEXT("a critical blow landed, larger: %.1f"), Critical), Critical > Plain))
	{
		return false;
	}
	TestTrue(TEXT("a critical strike bleeds what it hits. If not, DT_EnchantmentEffects may be older "
				  "than the rows: run tools/generate_datatable_assets.py"),
		Carries(Striker.First, Bleed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRetaliationStatusRowsTest,
	"Cataclysm.Enchantments.TheThreeRetaliationStatusRowsReachTheAttackerOfARealBlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your retaliation damage also applies a bleed stack to the attacker",
 * "Retaliation damage has a 20%-40% chance to stagger the attacker" and
 * "Retaliation damage applies a 2-4 second slow to the attacker". Issue #1833
 * group E part 1: status actions on `retaliation_dealt`. WORN together, worn
 * rolls at the top of their ranges, and a creature's real blow on the wearer
 * pays retaliation through the attribute set, which raises the event. The stagger
 * roll is pinned at 0, under its chance; the slow is the Cripple row's own 30%
 * for the top roll's 4 seconds.
 */
bool FCataclysmRetaliationStatusRowsTest::RunTest(const FString&)
{
	using namespace CataclysmApplyStatusRowTest;
	using namespace CataclysmEnchantmentEffectTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayTag Bleed = TagNamed(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTag Cripple = UCataclysmSkillShapes::StatusTagFor(TEXT("Cripple"));
	const FGameplayTag Staggered = UCataclysmSkillEffects::StaggeredTag();
	if (!TestTrue(TEXT("the three tags exist"), Bleed.IsValid() && Cripple.IsValid() && Staggered.IsValid()))
	{
		return false;
	}

	FWearer Wearer(World);
	const TPair<const TCHAR*, const TCHAR*> Worn[] = {
		{TEXT("Head_Helm"), TEXT("Positive_Your_retaliation_damage_also_applies_a_bleed_sta")},
		{TEXT("Chest_Cuirass"), TEXT("Positive_Retaliation_damage_has_a_20_40_chance_to_stagg")},
		{TEXT("Shoulders_Pauldrons"), TEXT("Positive_Retaliation_damage_applies_a_2_4_second_slow_to")}};
	for (const TPair<const TCHAR*, const TCHAR*>& Piece : Worn)
	{
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
		Wearer.Equipment->Equip(Carrying(Piece.Key, Piece.Value, DrawbackWithNoEffect),
			Removed, AlsoRemoved, Slot);
	}
	Wearer.Equipment->RefreshAttributes(Wearer.AbilitySystem);
	int32 Statuses = 0;
	for (const FCataclysmPoolAction& Action : Wearer.AbilitySystem->GetPoolActions())
	{
		Statuses += Action.ApplyStatus != ECataclysmApplyStatus::None ? 1 : 0;
	}
	if (!TestEqual(TEXT("the three worn rows are three status actions"), Statuses, 3))
	{
		return false;
	}
	// RETALIATION OF 300 PER CENT, as a stat line and an attribute, written after
	// the refresh. The rows are actions, kept apart from the stat lines.
	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.FindOrAdd(FName(UCataclysmRetaliation::AmountStat)).Base = 300.0f;
	Wearer.AbilitySystem->SetStatInputs(MoveTemp(Lines));
	Wearer.AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetRetaliationAttribute(), 300.0f);
	Wearer.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
	Wearer.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);

	ACataclysmEnemyCharacter* Attacker = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector(200.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("an attacker"), Attacker))
	{
		return false;
	}
	Attacker->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Attacker->SetHealth(10000.0f);
	Attacker->SetAttackDamage(500.0f);
	Attacker->SetArmour(0.0f);
	UAbilitySystemComponent* Its = Attacker->GetAbilitySystemComponent();
	Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
	Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);

	const FPinned Roll(TEXT("Cataclysm.StatusRoll"), 0.0f);
	const float WearerBefore = Wearer.AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	const float AttackerBefore = Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	UCataclysmSkillEffects::ApplyHit(Attacker, Wearer.Actor, /*DamagePercent=*/100.0f);
	const float Taken = WearerBefore - Wearer.AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	const float Paid = AttackerBefore - Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	if (!TestTrue(*FString::Printf(TEXT("the creature's blow landed: %.1f"), Taken), Taken > 0.0f)
		|| !TestTrue(*FString::Printf(TEXT("and retaliation paid over a tenth of its ten thousand back: %.1f"), Paid),
			Paid >= 1000.0f))
	{
		return false;
	}
	TestTrue(TEXT("the attacker bleeds"), Carries(Attacker, Bleed));
	TestTrue(TEXT("the attacker is staggered"), Carries(Attacker, Staggered));
	TestTrue(TEXT("the attacker is crippled"), Carries(Attacker, Cripple));
	TestEqual(TEXT("for the top roll's 4 seconds"), SecondsLeftOn(Attacker, Cripple), 4.0f, 0.01f);
	TestEqual(TEXT("at the Cripple row's own slow of 30"),
		UCataclysmSkillEffects::StatedStrengthOn(Attacker, Cripple), 30.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStrikeRandomDebuffRowTest,
	"Cataclysm.Enchantments.TheStrikeRandomDebuffRowReachesAStrikeSkillsHitAndNotAPlainBlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Strike skills have a 15%-30% chance to apply a random debuff on hit". Issue
 * #1833 group E part 1: a status action on `hit_dealt`, Required Tags
 * `Type.Strike`. WORN by a real player: a blow carrying a Strike skill puts the
 * pinned debuff, Cripple, on the creature it hits; a plain blow puts nothing. The
 * roll is pinned at 0, under the chance.
 */
bool FCataclysmStrikeRandomDebuffRowTest::RunTest(const FString&)
{
	using namespace CataclysmApplyStatusRowTest;
	CataclysmConsecutiveRowTest::FStriker Striker(
		TEXT("Positive_Strike_skills_have_a_15_30_chance_to_apply_a_r"),
		CataclysmEnchantmentEffectTest::DrawbackWithNoEffect);
	const FGameplayTag Cripple = UCataclysmSkillShapes::StatusTagFor(TEXT("Cripple"));
	if (!TestTrue(TEXT("a striker and two creatures"), Striker.Ready())
		|| !TestTrue(TEXT("the Cripple tag exists"), Cripple.IsValid()))
	{
		return false;
	}
	Striker.ASC->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 3000.0f);
	const FPinned Roll(TEXT("Cataclysm.StatusRoll"), 0.0f);
	const FPinned Pick(TEXT("Cataclysm.RandomDebuffPick"), 1.0f);

	const float Plain = Blow(Striker, Striker.Second, /*bCritical=*/false, /*bStrike=*/false);
	if (!TestTrue(*FString::Printf(TEXT("a plain blow took over a tenth: %.1f"), Plain), Plain >= 1000.0f))
	{
		return false;
	}
	TestFalse(TEXT("a plain blow applies no debuff"), Carries(Striker.Second, Cripple));

	const float Struck = Blow(Striker, Striker.First, /*bCritical=*/false, /*bStrike=*/true);
	if (!TestTrue(*FString::Printf(TEXT("a Strike skill's blow took over a tenth: %.1f"), Struck),
			Struck >= 1000.0f))
	{
		return false;
	}
	TestTrue(TEXT("a Strike skill's blow puts the pinned Cripple on what it hits. If not, "
				  "DT_EnchantmentEffects may be older than the rows: run tools/generate_datatable_assets.py"),
		Carries(Striker.First, Cripple));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFirstHitStaggerRowTest,
	"Cataclysm.Enchantments.TheFirstHitStaggerRowStaggersOnEachEnemysFirstBlowOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your first hit against each enemy has a 50%-100% chance to stagger them".
 * Issue #1833 group E part 1: a status action on `first_hit_dealt`. WORN by a
 * real player at the top of its roll, 100: its first blow on a creature staggers
 * it; its second, once the stagger and the quarter second have passed, does not;
 * and its first on another creature does.
 */
bool FCataclysmFirstHitStaggerRowTest::RunTest(const FString&)
{
	using namespace CataclysmApplyStatusRowTest;
	CataclysmConsecutiveRowTest::FStriker Striker(
		TEXT("Positive_Your_first_hit_against_each_enemy_has_a_50_100"),
		CataclysmEnchantmentEffectTest::DrawbackWithNoEffect);
	const FGameplayTag Staggered = UCataclysmSkillEffects::StaggeredTag();
	if (!TestTrue(TEXT("a striker and two creatures"), Striker.Ready()))
	{
		return false;
	}
	if (!TestTrue(TEXT("the first blow on a creature lands"),
			Blow(Striker, Striker.First, false, false) > 0.0f))
	{
		return false;
	}
	TestTrue(TEXT("and staggers it. If not, DT_EnchantmentEffects may be older than the rows: "
				  "run tools/generate_datatable_assets.py"),
		Carries(Striker.First, Staggered));

	CataclysmTestWorld::RunClock(Striker.World, 1.5f);
	if (!TestFalse(TEXT("set-up: 1.5 seconds on the stagger has ended"), Carries(Striker.First, Staggered))
		|| !TestTrue(TEXT("the second blow on it lands"), Blow(Striker, Striker.First, false, false) > 0.0f))
	{
		return false;
	}
	TestFalse(TEXT("and staggers nothing"), Carries(Striker.First, Staggered));

	if (!TestTrue(TEXT("the first blow on another creature lands"),
			Blow(Striker, Striker.Second, false, false) > 0.0f))
	{
		return false;
	}
	TestTrue(TEXT("and staggers it"), Carries(Striker.Second, Staggered));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGadgetStaggerRowTest,
	"Cataclysm.Enchantments.TheGadgetStaggerRowStaggersWhatABallistaHitsOnceEveryFiveSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Gadgets apply a 1-2 second stagger to enemies they hit, once every 5
 * seconds". Issue #1833 group E part 1: a seconds status action on
 * `deployable_hit`, Trigger Cooldown 5. WORN at the top of its roll, 2 seconds: a
 * ballista's evaded blow staggers nothing and starts no cooldown; its landed blow
 * staggers for 2 seconds; its next, at once, staggers nothing; and one 5.5
 * seconds later staggers again.
 */
bool FCataclysmGadgetStaggerRowTest::RunTest(const FString&)
{
	using namespace CataclysmDeployableTest;
	using namespace CataclysmDeployablePart2Test;
	using namespace CataclysmApplyStatusRowTest;
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	FSummoner Summoner(Scope.World, TEXT("Positive_Gadgets_apply_a_1_2_second_stagger_to_enemies_th"));
	ACataclysmMinion* Ballista = Summoner.Make(TEXT("Ballista"));
	ACataclysmEnemyCharacter* Evading = Victim(Summoner, /*Evasion=*/100.0f);
	ACataclysmEnemyCharacter* First = Victim(Summoner);
	ACataclysmEnemyCharacter* Next = Victim(Summoner);
	ACataclysmEnemyCharacter* Later = Victim(Summoner);
	const FGameplayTag Staggered = UCataclysmSkillEffects::StaggeredTag();
	if (!TestNotNull(TEXT("a ballista"), Ballista) || !TestNotNull(TEXT("an evading creature"), Evading)
		|| !TestNotNull(TEXT("three creatures"), First) || !TestNotNull(TEXT("three creatures"), Next)
		|| !TestNotNull(TEXT("three creatures"), Later))
	{
		return false;
	}
	Ballista->AttackTarget(Evading);
	TestFalse(TEXT("an evaded blow staggers nothing"), Carries(Evading, Staggered));

	Ballista->AttackTarget(First);
	TestTrue(TEXT("a landed blow staggers. If not, DT_EnchantmentEffects may be older than the rows: "
				  "run tools/generate_datatable_assets.py"),
		Carries(First, Staggered));
	TestEqual(TEXT("for the top roll's 2 seconds"), SecondsLeftOn(First, Staggered), 2.0f, 0.01f);

	Ballista->AttackTarget(Next);
	TestFalse(TEXT("the next blow at once staggers nothing: five seconds run"), Carries(Next, Staggered));

	Scope.World->TimeSeconds += 5.5f;
	Ballista->AttackTarget(Later);
	TestTrue(TEXT("5.5 seconds later a blow staggers again"), Carries(Later, Staggered));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDotMoreRowTest,
	"Cataclysm.Enchantments.TheDotMoreRowDoublesATickAtTheTopOfItsRoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your damage over time effects deal 50%-100% more damage". Issue #1833 group E
 * part 1: `dot_damage` more, 50 to 100. WORN at the top of its roll, 100: one
 * tick of a damage over time the wearer applies takes twice what a wearer of a
 * benefit with no effect row takes from a creature of its own.
 */
bool FCataclysmDotMoreRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	const FGameplayTag Poison = Keyword(TEXT("Keyword.DoT.Poison"));
	float Worn = -1.0f;
	float Plain = -1.0f;
	{
		FWorn Wearing(TEXT("Positive_Your_damage_over_time_effects_deal_50_100_more"), true);
		if (!TestNotNull(TEXT("a wearer"), Wearing.ASC()))
		{
			return false;
		}
		Worn = OneTick(Wearing, Poison, 300.0f);
	}
	{
		FWorn Control(CataclysmEnchantmentEffectTest::BenefitWithNoEffect, true);
		if (!TestNotNull(TEXT("a control wearer"), Control.ASC()))
		{
			return false;
		}
		Plain = OneTick(Control, Poison, 300.0f);
	}
	if (!TestTrue(*FString::Printf(TEXT("a plain tick took %.2f"), Plain), Plain > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the wearer's tick is twice the plain one"), Worn, 2.0f * Plain, 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidSplinterTargetRowTest,
	"Cataclysm.Enchantments.TheVoidSplinterRowAddsItsMoreDamageOnlyToACreatureCarryingOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "You deal 3%-5% more damage to an enemy carrying a void splinter". Issue #1833
 * group E part 1: `attack_damage` and `spell_damage` more, 3 to 5, under
 * `target_carries_void_splinter`. WORN by a real player at the top of its roll, 5:
 * its blow on a creature carrying a void splinter is 1.05 times its blow on one
 * carrying none.
 */
bool FCataclysmVoidSplinterTargetRowTest::RunTest(const FString&)
{
	using namespace CataclysmApplyStatusRowTest;
	CataclysmConsecutiveRowTest::FStriker Striker(
		TEXT("Positive_You_deal_3_5_more_damage_to_an_enemy_carrying"),
		CataclysmEnchantmentEffectTest::DrawbackWithNoEffect);
	const FCataclysmAilmentKind* Splinter = UCataclysmAilments::KindNamed(TEXT("Void Splinter"));
	if (!TestTrue(TEXT("a striker and two creatures"), Striker.Ready())
		|| !TestNotNull(TEXT("the void splinter ailment"), Splinter)
		|| !TestTrue(TEXT("the first creature carries a void splinter"),
			UCataclysmAilments::Apply(Striker.Character, Striker.First, *Splinter, /*Magnitude=*/1.0f)))
	{
		return false;
	}
	const float Carrying = Blow(Striker, Striker.First, false, false);
	const float NotCarrying = Blow(Striker, Striker.Second, false, false);
	if (!TestTrue(*FString::Printf(TEXT("a blow on the creature carrying none took %.2f"), NotCarrying),
			NotCarrying > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the blow on the creature carrying one is 1.05 times it. If not, DT_EnchantmentEffects "
				   "may be older than the rows: run tools/generate_datatable_assets.py"),
		Carrying, 1.05f * NotCarrying, 0.05f);
	return true;
}

namespace CataclysmBlockRowTest
{
	/**
	 * The stat lines a blow reads, written after the refresh: each worn line named
	 * in `Keep` copied as it is, and the lines in `Bases` at those bases. A refresh
	 * may leave a line at base 0 that a reader asks before the attribute, which is
	 * why a block chance is written here and not only as an attribute.
	 */
	void WriteLines(UCataclysmAbilitySystemComponent* System, TArray<FName> Keep,
		TMap<FName, float> Bases)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		for (const FName& Stat : Keep)
		{
			if (const FCataclysmStatInputs* Worn = System->GetStatInputs(Stat))
			{
				Lines.Add(Stat, *Worn);
			}
		}
		for (const TPair<FName, float>& Base : Bases)
		{
			Lines.FindOrAdd(Base.Key).Base = Base.Value;
		}
		System->SetStatInputs(MoveTemp(Lines));
	}

	/** What one blow of a thousand-strong attacker took from the wearer. */
	float BlowOn(UWorld* World, CataclysmEnchantmentEffectTest::FWearer& Wearer,
		CataclysmEnchantmentEffectTest::FWearer& Attacker, bool* bOutBlocked = nullptr)
	{
		const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
		const float Before = Wearer.AbilitySystem->GetNumericAttribute(Health);
		FCataclysmDamageResult Result;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Wearer.Actor, 100.0f,
			FGameplayTagContainer(), FCataclysmHitDelivery(), &Result);
		if (bOutBlocked)
		{
			*bOutBlocked = Result.bBlocked;
		}
		return Before - Wearer.AbilitySystem->GetNumericAttribute(Health);
	}

	/** A wearer of one helm, with a hundred thousand health, and a thousand-strong attacker. */
	struct FBlockFight
	{
		FBlockFight(UWorld* World, const TCHAR* Enchantment)
			: Wearer(World), Attacker(World)
		{
			using namespace CataclysmEnchantmentEffectTest;
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
			Wearer.Equipment->Equip(Carrying(TEXT("Head_Helm"), Enchantment, DrawbackWithNoEffect),
				Removed, AlsoRemoved, Slot);
			Wearer.Equipment->RefreshAttributes(Wearer.AbilitySystem);
			Wearer.AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			Wearer.AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			Attacker.AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 1000.0f);
		}
		CataclysmEnchantmentEffectTest::FWearer Wearer;
		CataclysmEnchantmentEffectTest::FWearer Attacker;
	};

	const FName BlockChance(TEXT("block_chance"));
	const FName BlockShare(TEXT("block_damage_reduction"));
	const FName Negation(TEXT("block_negation_chance"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBlockShareRowTest,
	"Cataclysm.Enchantments.TheBlockShareRowMakesABlockRemoveSeventyFivePercent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "You block for 65%-75% of damage instead of the normal 50%". Issue #1833 group
 * E part 2: `block_damage_reduction` flat 15 to 25 on its base of 50. WORN at the
 * top of its roll: the share is 75, and a real blocked blow keeps a quarter of
 * the same blow unblocked.
 */
bool FCataclysmBlockShareRowTest::RunTest(const FString&)
{
	using namespace CataclysmBlockRowTest;
	using namespace CataclysmApplyStatusRowTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	FBlockFight Fight(World, TEXT("Positive_You_block_for_65_75_of_damage_instead_of_the_n"));
	WriteLines(Fight.Wearer.AbilitySystem, {BlockShare}, {{BlockChance, 100.0f}});
	TestEqual(TEXT("the worn share is 75. If it is 50, DT_EnchantmentEffects may be older than the "
				   "rows: run tools/generate_datatable_assets.py"),
		UCataclysmDamageCalculation::BlockShareOf(Fight.Wearer.AbilitySystem, FCataclysmBlowContext()),
		75.0f, 0.001f);

	float Full = 0.0f;
	{
		const FPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);
		Full = BlowOn(World, Fight.Wearer, Fight.Attacker);
	}
	if (!TestTrue(*FString::Printf(TEXT("set-up: an unblocked blow took %.1f"), Full), Full > 0.0f))
	{
		return false;
	}
	const FPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
	bool bBlocked = false;
	TestEqual(TEXT("a blocked blow keeps a quarter"), BlowOn(World, Fight.Wearer, Fight.Attacker, &bBlocked),
		Full * 0.25f, 0.5f);
	TestTrue(TEXT("and was blocked"), bBlocked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBlockNegationRowTest,
	"Cataclysm.Enchantments.TheNegationRowNegatesABlockedBlowUnderItsRolledChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Blocking an attack has a 20%-40% chance to fully negate all damage". Issue
 * #1833 group E part 2: `block_negation_chance` flat 20 to 40. WORN at the top of
 * its roll, 40: a blocked blow whose negation roll is 39 keeps nothing, and one
 * whose roll is 41 keeps the ordinary half.
 */
bool FCataclysmBlockNegationRowTest::RunTest(const FString&)
{
	using namespace CataclysmBlockRowTest;
	using namespace CataclysmApplyStatusRowTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	FBlockFight Fight(World, TEXT("Positive_Blocking_an_attack_has_a_20_40_chance_to_fully"));
	WriteLines(Fight.Wearer.AbilitySystem, {Negation}, {{BlockChance, 100.0f}});

	float Full = 0.0f;
	{
		const FPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);
		Full = BlowOn(World, Fight.Wearer, Fight.Attacker);
	}
	if (!TestTrue(*FString::Printf(TEXT("set-up: an unblocked blow took %.1f"), Full), Full > 0.0f))
	{
		return false;
	}
	const FPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
	{
		const FPinned Under(TEXT("Cataclysm.BlockNegationRoll"), 39.0f);
		TestEqual(TEXT("a roll of 39, under 40, negates the blocked blow. If not, DT_EnchantmentEffects "
					   "may be older than the rows: run tools/generate_datatable_assets.py"),
			BlowOn(World, Fight.Wearer, Fight.Attacker), 0.0f, 0.001f);
	}
	{
		const FPinned Over(TEXT("Cataclysm.BlockNegationRoll"), 41.0f);
		TestEqual(TEXT("a roll of 41 keeps the ordinary half"),
			BlowOn(World, Fight.Wearer, Fight.Attacker), Full * 0.5f, 0.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsecutiveBlockRowTest,
	"Cataclysm.Enchantments.TheConsecutiveBlockRowAddsTenPointsPerBlockUpToEightyFive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Consecutive blocks within 3 seconds each block 5%-10% more damage". Issue
 * #1833 group E part 2: own stacks of `block_damage_reduction` flat 5 to 10 on
 * `block`, 3 s, cap 7. WORN at the top of its roll, 10: five blocked blows in a
 * row keep 50%, 40%, 30%, 20% and then 15%, the 85 cap; 3.5 seconds later the
 * stacks are gone and a block keeps half again.
 */
bool FCataclysmConsecutiveBlockRowTest::RunTest(const FString&)
{
	using namespace CataclysmBlockRowTest;
	using namespace CataclysmApplyStatusRowTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	FBlockFight Fight(World, TEXT("Positive_Consecutive_blocks_within_3_seconds_each_block_5"));
	WriteLines(Fight.Wearer.AbilitySystem, {BlockShare},
		{{BlockChance, 100.0f}, {BlockShare, UCataclysmDamageCalculation::BlockDamageReduction}});

	float Full = 0.0f;
	{
		const FPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);
		Full = BlowOn(World, Fight.Wearer, Fight.Attacker);
	}
	if (!TestTrue(*FString::Printf(TEXT("set-up: an unblocked blow took %.1f"), Full), Full > 0.0f))
	{
		return false;
	}
	const FPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
	const float Kept[] = {0.50f, 0.40f, 0.30f, 0.20f, 0.15f};
	for (int32 Index = 0; Index < 5; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("block %d keeps %.0f%%. If every block keeps half, "
				"DT_EnchantmentEffects may be older than the rows: run tools/generate_datatable_assets.py"),
				Index + 1, Kept[Index] * 100.0f),
			BlowOn(World, Fight.Wearer, Fight.Attacker), Full * Kept[Index], 0.5f);
	}
	World->TimeSeconds += 3.5f;
	TestEqual(TEXT("3.5 seconds later the stacks are gone and a block keeps half"),
		BlowOn(World, Fight.Wearer, Fight.Attacker), Full * 0.5f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArchonsAegisSixRowTest,
	"Cataclysm.Enchantments.ArchonsAegisSixPiecesMakeABlockOpenThreeSecondsOfImmunityOnceInTen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Archon's Aegis (6-Piece Bonus): When you block an attack, you become immune to
 * all damage for 3 seconds. (10s cd)". Issue #1833 group E part 2:
 * `damage_immunity` 3 on `block`, Trigger Cooldown 10. Six pieces: a real block
 * opens the window, the next blow takes nothing, a block four seconds on opens
 * nothing, and one eleven seconds on opens it again. Five pieces open nothing.
 */
bool FCataclysmArchonsAegisSixRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmHealthThresholdRowTest;
	using namespace CataclysmBlockRowTest;
	using namespace CataclysmApplyStatusRowTest;
	const TCHAR* Aegis = TEXT("Positive_Archon_s_Aegis_6_Piece_Bonus_When_you_block_a");
	for (const int32 Pieces : {5, 6})
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
		const FPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
		FWearer Wearer(World);
		WearSet(Wearer, Aegis, Pieces);
		SetHealth(Wearer.AbilitySystem, 100000.0f, 100000.0f);
		WriteLines(Wearer.AbilitySystem, {}, {{BlockChance, 100.0f}});
		FWearer Attacker(World);
		Attacker.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 1000.0f);

		bool bBlocked = false;
		const float First = BlowOn(World, Wearer, Attacker, &bBlocked);
		if (!TestTrue(*FString::Printf(TEXT("set-up, %d pieces: the first blow was blocked and landed %.1f"),
				Pieces, First), bBlocked && First > 0.0f))
		{
			return false;
		}
		if (Pieces < 6)
		{
			TestFalse(TEXT("five pieces: the block opened nothing"), Wearer.AbilitySystem->IsDamageImmune());
			continue;
		}
		TestTrue(TEXT("six pieces: the block opened the window. If not, DT_EnchantmentEffects may be "
					  "older than the rows: run tools/generate_datatable_assets.py"),
			Wearer.AbilitySystem->IsDamageImmune());
		TestEqual(TEXT("so the next blow takes nothing"), BlowOn(World, Wearer, Attacker), 0.0f, 0.001f);
		World->TimeSeconds += 4.0f;
		TestTrue(TEXT("four seconds on, a blocked blow lands"), BlowOn(World, Wearer, Attacker) > 0.0f);
		TestFalse(TEXT("and opens nothing: ten seconds have not passed"), Wearer.AbilitySystem->IsDamageImmune());
		World->TimeSeconds += 7.0f;
		BlowOn(World, Wearer, Attacker);
		TestTrue(TEXT("eleven seconds on a block opens it again"), Wearer.AbilitySystem->IsDamageImmune());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRechargeNovaRowTest,
	"Cataclysm.Enchantments.TheRechargeNovaRowSmitesNearbyEnemiesWhenRegenerationFillsTheShield",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "When your energy shield fully recharges, release a nova dealing 50%-100%
 * weapon damage to nearby enemies". Issue #1833 group E part 2: `smite_nearby`
 * 50 to 100 on `energy_shield_recharged`. WORN at the top of its roll: a
 * regeneration step that leaves the shield below its maximum smites nothing, and
 * the step that fills it smites an enemy three metres away.
 */
bool FCataclysmRechargeNovaRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmHealthThresholdRowTest;
	using namespace CataclysmBlockRowTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Wearer.Equipment->Equip(Carrying(TEXT("Head_Helm"),
		TEXT("Positive_When_your_energy_shield_fully_recharges_release"), DrawbackWithNoEffect),
		Removed, AlsoRemoved, Slot);
	Wearer.Equipment->RefreshAttributes(Wearer.AbilitySystem);
	SetHealth(Wearer.AbilitySystem, 1000.0f, 1000.0f);
	Wearer.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 100.0f);
	Wearer.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 40.0f);
	Wearer.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetEnergyShieldRegenAttribute(), 100.0f);
	Wearer.AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 200.0f);
	WriteLines(Wearer.AbilitySystem, {},
		{{FName(UCataclysmRegeneration::EnergyShieldRegenStat), 100.0f}, {FName(TEXT("attack_damage")), 200.0f}});

	ACataclysmEnemyCharacter* Near = World->SpawnActor<ACataclysmEnemyCharacter>(
		Wearer.Actor->GetActorLocation() + FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("an enemy three metres away"), Near))
	{
		return false;
	}
	Near->SetHealth(10000.0f);
	const UAbilitySystemComponent* NearSystem = UCataclysmTargeting::AbilitySystemOf(Near);
	if (!TestNotNull(TEXT("the enemy's ability system"), NearSystem))
	{
		return false;
	}
	const float Before = HealthOf(NearSystem);

	UCataclysmRegeneration::ApplyStep(Wearer.Actor, /*SecondsInStep=*/0.25f, /*SecondsSinceLastDamage=*/100.0f);
	const float Partly = Wearer.AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute());
	if (!TestTrue(*FString::Printf(TEXT("set-up: a quarter second refilled part of the shield: %.1f"), Partly),
			Partly > 40.0f && Partly < 100.0f))
	{
		return false;
	}
	TestEqual(TEXT("a step that leaves it below its maximum smites nothing"), HealthOf(NearSystem), Before, 0.01f);

	UCataclysmRegeneration::ApplyStep(Wearer.Actor, 1.0f, 100.0f);
	TestEqual(TEXT("set-up: a second filled it"),
		Wearer.AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute()), 100.0f, 0.01f);
	TestTrue(*FString::Printf(TEXT("the step that fills it smites the enemy nearby: %.2f to %.2f. If not, "
			"DT_EnchantmentEffects may be older than the rows: run tools/generate_datatable_assets.py"),
			Before, HealthOf(NearSystem)),
		HealthOf(NearSystem) < Before);
	return true;
}

namespace CataclysmBlockPartTwoRowTest
{
	/** A creature of the monsters' side 3 m from `Near`, with no armour, evasion, block or resistance. */
	ACataclysmEnemyCharacter* CreatureBeside(UWorld* World, const AActor* Near)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			Near->GetActorLocation() + FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(100000.0f);
			Made->SetArmour(0.0f);
			UAbilitySystemComponent* Its = Made->GetAbilitySystemComponent();
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		}
		return Made;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReflectRowTest,
	"Cataclysm.Enchantments.TheReflectRowPaysAllOfWhatABlockRemovedToTheAttacker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Reflect 20%-100% of damage blocked back at attackers". Issue #1833 group E
 * part 3: `reflect_blocked` 20 to 100 on `block`. WORN at the top of its roll,
 * 100: a real blocked blow costs the attacker exactly what the block removed.
 */
bool FCataclysmReflectRowTest::RunTest(const FString&)
{
	using namespace CataclysmBlockRowTest;
	using namespace CataclysmApplyStatusRowTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
	FBlockFight Fight(World, TEXT("Positive_Reflect_20_100_of_damage_blocked_back_at_attac"));
	WriteLines(Fight.Wearer.AbilitySystem, {}, {{BlockChance, 100.0f}});
	Fight.Attacker.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
	Fight.Attacker.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);

	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const float Before = Fight.Attacker.AbilitySystem->GetNumericAttribute(Health);
	FCataclysmDamageResult Result;
	UCataclysmSkillEffects::ApplyHit(Fight.Attacker.Actor, Fight.Wearer.Actor, 100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery(), &Result);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the blow was blocked and removed %.1f"), Result.DamageBlocked),
			Result.bBlocked && Result.DamageBlocked > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the attacker lost all of what the block removed. If nothing, DT_EnchantmentEffects may be "
				   "older than the rows: run tools/generate_datatable_assets.py"),
		Before - Fight.Attacker.AbilitySystem->GetNumericAttribute(Health), Result.DamageBlocked, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBlockShockwaveRowTest,
	"Cataclysm.Enchantments.TheShockwaveRowSmitesOnTheThirdBlockInsideThreeSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Every 3 blocks in quick succession triggers a shockwave dealing 100%-200%
 * weapon damage to nearby enemies". Issue #1833 group E part 3: `smite_nearby`
 * 100 to 200 on `block`, Every Nth 3, Stack Seconds 3. WORN: the first two real
 * blocks smite nothing, and the third smites a creature three metres away.
 */
bool FCataclysmBlockShockwaveRowTest::RunTest(const FString&)
{
	using namespace CataclysmBlockRowTest;
	using namespace CataclysmApplyStatusRowTest;
	using namespace CataclysmBlockPartTwoRowTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
	FBlockFight Fight(World, TEXT("Positive_Every_3_blocks_in_quick_succession_triggers_a_sh"));
	WriteLines(Fight.Wearer.AbilitySystem, {},
		{{BlockChance, 100.0f}, {FName(TEXT("attack_damage")), 100.0f}});
	Fight.Wearer.AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
	ACataclysmEnemyCharacter* Creature = CreatureBeside(World, Fight.Wearer.Actor);
	if (!TestNotNull(TEXT("a creature three metres away"), Creature))
	{
		return false;
	}
	const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Creature);
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const auto Smitten = [&]()
	{
		const float Before = Its->GetNumericAttribute(Health);
		bool bBlocked = false;
		BlowOn(World, Fight.Wearer, Fight.Attacker, &bBlocked);
		if (!bBlocked)
		{
			AddError(TEXT("a blow was not blocked"));
		}
		// PAST THE ROW'S QUARTER SECOND, so the count, not the cooldown, decides.
		World->TimeSeconds += 0.3f;
		return Its->GetNumericAttribute(Health) < Before;
	};
	TestFalse(TEXT("block 1 smites nothing"), Smitten());
	TestFalse(TEXT("block 2 smites nothing"), Smitten());
	TestTrue(TEXT("block 3 smites the creature. If not, DT_EnchantmentEffects may be older than the rows: "
				  "run tools/generate_datatable_assets.py"),
		Smitten());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmourNovaRowTest,
	"Cataclysm.Enchantments.TheArmourNovaRowHitsNearbyForFourTimesTheWearersArmour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Blocking attacks deals 200%-400% of your armor as damage to nearby enemies".
 * Issue #1833 group E part 3: `smite_nearby_by_armor` 200 to 400 on `block`.
 * WORN at the top of its roll, 400, by a wearer with 1000 armour: a real block
 * costs a creature three metres away, with no mitigation, 4000.
 */
bool FCataclysmArmourNovaRowTest::RunTest(const FString&)
{
	using namespace CataclysmBlockRowTest;
	using namespace CataclysmApplyStatusRowTest;
	using namespace CataclysmBlockPartTwoRowTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
	FBlockFight Fight(World, TEXT("Positive_Blocking_attacks_deals_200_400_of_your_armor_a"));
	WriteLines(Fight.Wearer.AbilitySystem, {}, {{BlockChance, 100.0f}, {FName(TEXT("armor")), 1000.0f}});
	Fight.Wearer.AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 1000.0f);
	ACataclysmEnemyCharacter* Creature = CreatureBeside(World, Fight.Wearer.Actor);
	if (!TestNotNull(TEXT("a creature three metres away"), Creature))
	{
		return false;
	}
	const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Creature);
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const float Before = Its->GetNumericAttribute(Health);
	bool bBlocked = false;
	BlowOn(World, Fight.Wearer, Fight.Attacker, &bBlocked);
	if (!TestTrue(TEXT("set-up: the blow was blocked"), bBlocked))
	{
		return false;
	}
	TestEqual(TEXT("the creature lost 400% of the wearer's 1000 armour. If nothing, DT_EnchantmentEffects may "
				   "be older than the rows: run tools/generate_datatable_assets.py"),
		Before - Its->GetNumericAttribute(Health), 4000.0f, 0.5f);
	return true;
}

namespace CataclysmSharedBuffRowTest
{
	/**
	 * A creature `Metres` along X that is the wearer's ally: OWNED BY THE WEARER,
	 * because the bare wearer these row tests use carries no team, and an owner
	 * chain is what makes two such actors friendly.
	 */
	ACataclysmEnemyCharacter* AllyOf(UWorld* World, AActor* Wearer, float Metres)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(Metres * 100.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetOwner(Wearer);
			Made->SetHealth(100000.0f);
		}
		return Made;
	}

	/** The More damage `Ally` carries on its own ability system. */
	float MoreCarried(const AActor* Ally)
	{
		float More = 0.0f;
		if (const UCataclysmAbilitySystemComponent* Its =
				Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Ally)))
		{
			for (const FCataclysmStatModifier& Modifier : Its->GetStatModifiers())
			{
				More += Modifier.Bucket == ECataclysmStatBucket::More ? Modifier.Value : 0.0f;
			}
		}
		return More;
	}

	/**
	 * The wearer casts an unscoped Burning Wrath in the Support slot, with one
	 * enemy burning 3 m away, so it grants 4% More. Returns that buff, or null.
	 */
	UCataclysmSelfBuffSkill* SupportBuffOn(UWorld* World, UCataclysmAbilitySystemComponent* ASC, AActor* Wearer)
	{
		ASC->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 10000.0f);
		ASC->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 10000.0f);
		ACataclysmEnemyCharacter* Alight = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(-300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (!Alight)
		{
			return nullptr;
		}
		Alight->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Alight->SetHealth(100000.0f);
		UCataclysmSkillEffects::ApplyBurn(Wearer, Alight, 100.0f,
			/*bScalesWithInstigator=*/true, /*bBurnIsDesigned=*/true);
		const FGameplayAbilitySpecHandle Handle = ASC->GiveAbilityInSlot(
			UCataclysmSelfBuffSkill::StaticClass(), ECataclysmAbilitySlot::Support, /*Level=*/1, Wearer);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmSelfBuffSkill* Buff = Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Buff)
		{
			return nullptr;
		}
		Buff->SkillName = TEXT("Burning Wrath");
		Buff->Params = UCataclysmSkillShapes::ParseParams(
			TEXT("Duration=10; Radius=15; MoreDamagePer=4; ScalingSource=Burning"));
		Buff->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Slot.Support"));
		ASC->TryActivateAbility(Buff->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
		return Buff;
	}

	const TCHAR* StaleAsset =
		TEXT(" If nothing, DT_EnchantmentEffects may be older than the rows: run "
			 "tools/generate_datatable_assets.py");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSupportSharedRowTest,
	"Cataclysm.Enchantments.TheSupportShareRowGivesASupportBuffsMoreToAnAllyTwelveMetresAway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your support ability affects all allies within 15 meters instead of just
 * yourself". Issue #1833 group E part 4a: `support_buff_shared_within_metres`
 * flat 15. WORN: a Support buff granting 4% More reaches an ally 12 m away.
 */
bool FCataclysmSupportSharedRowTest::RunTest(const FString&)
{
	using namespace CataclysmSharedBuffRowTest;
	CataclysmSmallHalvesTest::FWorn Worn(TEXT("Positive_Your_support_ability_affects_all_allies_within_1"), true);
	if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
	{
		return false;
	}
	TestEqual(*FString(TEXT("the row gives a reach of 15 m.") + FString(StaleAsset)),
		Worn.ASC()->StatForSkill(FName(UCataclysmAbilitySystemComponent::SupportBuffSharedWithinMetresStat),
								 FGameplayTagContainer(), 0.0f), 15.0f, 0.001f);
	ACataclysmEnemyCharacter* Ally = AllyOf(Worn.World, Worn.Wearer->Actor, 12.0f);
	const UCataclysmSelfBuffSkill* Buff = SupportBuffOn(Worn.World, Worn.ASC(), Worn.Wearer->Actor);
	if (!TestNotNull(TEXT("an ally"), Ally)
		|| !TestTrue(TEXT("set-up: the Support buff runs and grants 4% More"),
			Buff && Buff->IsActive() && FMath::IsNearlyEqual(Buff->GrantedIncrease, 4.0f)))
	{
		return false;
	}
	UCataclysmSharedBuffs::Step(Worn.Wearer->Actor);
	TestEqual(TEXT("the ally 12 m away carries the buff's 4% More"), MoreCarried(Ally), 4.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSelfSharedRowTest,
	"Cataclysm.Enchantments.TheSelfBuffShareRowReachesAnAllySixMetresAwayAndNotTwelve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Applying a buff to yourself also applies it to all allies within 8 meters".
 * Issue #1833 group E part 4a: `self_buff_shared_within_metres` flat 8. WORN:
 * a buff granting 4% More reaches an ally 6 m away and not one 12 m away.
 */
bool FCataclysmSelfSharedRowTest::RunTest(const FString&)
{
	using namespace CataclysmSharedBuffRowTest;
	CataclysmSmallHalvesTest::FWorn Worn(TEXT("Positive_Applying_a_buff_to_yourself_also_applies_it_to_a"), true);
	if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
	{
		return false;
	}
	TestEqual(*FString(TEXT("the row gives a reach of 8 m.") + FString(StaleAsset)),
		Worn.ASC()->StatForSkill(FName(UCataclysmAbilitySystemComponent::SelfBuffSharedWithinMetresStat),
								 FGameplayTagContainer(), 0.0f), 8.0f, 0.001f);
	ACataclysmEnemyCharacter* Near = AllyOf(Worn.World, Worn.Wearer->Actor, 6.0f);
	ACataclysmEnemyCharacter* Far = AllyOf(Worn.World, Worn.Wearer->Actor, 12.0f);
	const UCataclysmSelfBuffSkill* Buff = SupportBuffOn(Worn.World, Worn.ASC(), Worn.Wearer->Actor);
	if (!TestNotNull(TEXT("an ally at 6 m"), Near) || !TestNotNull(TEXT("an ally at 12 m"), Far)
		|| !TestTrue(TEXT("set-up: the buff runs and grants 4% More"),
			Buff && Buff->IsActive() && FMath::IsNearlyEqual(Buff->GrantedIncrease, 4.0f)))
	{
		return false;
	}
	UCataclysmSharedBuffs::Step(Worn.Wearer->Actor);
	TestEqual(TEXT("the ally 6 m away carries the buff's 4% More"), MoreCarried(Near), 4.0f, 0.001f);
	TestEqual(TEXT("and the ally 12 m away nothing"), MoreCarried(Far), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNearbyAlliesRowTest,
	"Cataclysm.Enchantments.TheNearbyAlliesRowRaisesAnImpsBlowByTwentyPerCent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Nearby allies gain 10-20% more damage". Issue #1833 group E part 4a:
 * `nearby_allies_more_damage` flat 10 to 20. WORN at the top of its roll, 20:
 * the wearer's imp 3 m away hits for 120 where it hit for 100. The minion's blow
 * goes through `ModifiedDamage` on its own ability system, which is what this
 * reads.
 */
bool FCataclysmNearbyAlliesRowTest::RunTest(const FString&)
{
	using namespace CataclysmSharedBuffRowTest;
	CataclysmSmallHalvesTest::FWorn Worn(TEXT("Positive_Nearby_allies_gain_10_20_more_damage"), true);
	if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
	{
		return false;
	}
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Worn.Wearer->Actor, FVector(300.0f, 0.0f, 0.0f), /*Lifetime=*/20.0f, /*bBurns=*/false, TEXT("Imp"));
	if (!TestNotNull(TEXT("an imp"), Imp))
	{
		return false;
	}
	Imp->SetOwner(Worn.Wearer->Actor);
	const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Imp);
	TestEqual(TEXT("set-up: the imp's blow of 100 is 100"),
		UCataclysmSkillEffects::ModifiedDamage(Its, 100.0f, FGameplayTagContainer()), 100.0f, 0.01f);
	UCataclysmSharedBuffs::Step(Worn.Wearer->Actor);
	TestEqual(*FString(TEXT("the imp's blow of 100 is 120.") + FString(StaleAsset)),
		UCataclysmSkillEffects::ModifiedDamage(Its, 100.0f, FGameplayTagContainer()), 120.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecrosisRiseRowTest,
	"Cataclysm.Enchantments.TheNecrosisRiseRowRaisesAnImpForTenSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Enemies killed by necrosis rise as temporary minions for 5-10 seconds". Issue
 * #1833 group E part 4b: `necrosis_kill_raises_imp_seconds` flat 5 to 10. WORN
 * at the top of its roll, 10: the rule, asked as the wearer's necrosis kill of a
 * creature asks it, raises an imp that lasts 10 seconds. The call from a real
 * player's handling of a death is `Cataclysm.NecrosisRise.`'s.
 */
bool FCataclysmNecrosisRiseRowTest::RunTest(const FString&)
{
	CataclysmSmallHalvesTest::FWorn Worn(TEXT("Positive_Enemies_killed_by_necrosis_rise_as_temporary_min"), true);
	if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Victim = Worn.World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector(500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature"), Victim))
	{
		return false;
	}
	Victim->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	FGameplayTagContainer Killing;
	Killing.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Keyword.DoT.Necrosis")), /*ErrorIfNotFound=*/false));

	const ACataclysmMinion* Imp = UCataclysmRisenImps::RiseOnNecrosisKill(
		Worn.Wearer->Actor, Victim, Victim->GetActorLocation(), &Killing);
	if (!TestNotNull(TEXT("an imp rises. If none, DT_EnchantmentEffects may be older than the rows: "
						  "run tools/generate_datatable_assets.py"), Imp))
	{
		return false;
	}
	TestEqual(TEXT("it lasts the 10 seconds the row rolled"), Imp->GetLifeSpan(), 10.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmResummonRowTest,
	"Cataclysm.Enchantments.TheResummonRowBringsALostImpBackAfterThreeSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "When a minion dies it automatically re-summons after 3-6 seconds". Issue
 * #1833 group E part 4b: `minion_resummoned_after_seconds`, 3 to 6 seconds.
 * WORN at the top of its roll, which is 3, the shortest wait: ruled 2026-10-05,
 * the best roll gives the best outcome, so this range rolls down. The wearer's
 * summoned imp dies, nothing has come back at 2.9 seconds, and one imp has at
 * 3.1.
 */
bool FCataclysmResummonRowTest::RunTest(const FString&)
{
	CataclysmSmallHalvesTest::FWorn Worn(TEXT("Positive_When_a_minion_dies_it_automatically_re_summons_a"), true);
	if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
	{
		return false;
	}
	const FGameplayAbilitySpecHandle Handle = Worn.ASC()->GiveAbilityInSlot(
		UCataclysmSummonSkill::StaticClass(), ECataclysmAbilitySlot::Special, /*Level=*/1, Worn.Wearer->Actor);
	FGameplayAbilitySpec* Spec = Handle.IsValid() ? Worn.ASC()->FindAbilitySpecFromHandle(Handle) : nullptr;
	UCataclysmSummonSkill* Skill = Spec ? Cast<UCataclysmSummonSkill>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("a summon skill"), Skill))
	{
		return false;
	}
	Skill->SkillName = TEXT("Summon Imp");
	Skill->Params = UCataclysmSkillShapes::ParseParams(
		TEXT("Count=1; MaxActive=3; Duration=20; Radius=3; Minions=Imp:1"));
	ACataclysmMinion* First = Skill->SummonOne();
	if (!TestNotNull(TEXT("an imp"), First))
	{
		return false;
	}
	UCataclysmTargeting::AbilitySystemOf(First)->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
	TestEqual(TEXT("set-up: the imp died and nothing replaced it at once"), Skill->LivingMinionCount(), 0);
	TestEqual(TEXT("one wait is started. If none, DT_EnchantmentEffects may be older than the rows: "
				   "run tools/generate_datatable_assets.py"),
		Worn.ASC()->PendingResummons.Num(), 1);

	CataclysmTestWorld::RunClock(Worn.World, 2.9f);
	TestEqual(TEXT("at 2.9 seconds nothing has come back"), Skill->LivingMinionCount(), 0);
	CataclysmTestWorld::RunClock(Worn.World, 0.2f);
	TestEqual(TEXT("at 3.1 seconds one imp has"), Skill->LivingMinionCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraSharesImmunitiesRowTest,
	"Cataclysm.Enchantments.TheAuraShareRowMakesAnImpInsideLivingPyreImmuneToAStun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your aura also applies its effect to all allies within range". Issue #1833
 * group E part 4c: `aura_shares_immunities_with_allies` flat 1. WORN: the wearer
 * casts Living Pyre, whose row names `Immune=Stun, Slow, Displacement`, and after
 * one pulse its imp 2 m away is immune to a stun and not to a knockdown.
 */
bool FCataclysmAuraSharesImmunitiesRowTest::RunTest(const FString&)
{
	CataclysmSmallHalvesTest::FWorn Worn(TEXT("Positive_Your_aura_also_applies_its_effect_to_all_allies"), true);
	if (!TestNotNull(TEXT("a wearer"), Worn.ASC()))
	{
		return false;
	}
	Worn.ASC()->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 10000.0f);
	Worn.ASC()->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 10000.0f);
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Worn.Wearer->Actor, FVector(200.0f, 0.0f, 0.0f), /*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("Imp"));
	if (!TestNotNull(TEXT("an imp"), Imp))
	{
		return false;
	}
	Imp->SetOwner(Worn.Wearer->Actor);

	// THE AURA SLOT, WHICH ASKS NO FERVOUR OF A BARE WEARER. The row reads the
	// aura's own `Immune=`, whichever slot it sits in.
	const FGameplayAbilitySpecHandle Handle = Worn.ASC()->GiveAbilityInSlot(
		UCataclysmAuraSkill::StaticClass(), ECataclysmAbilitySlot::Aura, /*Level=*/1, Worn.Wearer->Actor);
	FGameplayAbilitySpec* Spec = Handle.IsValid() ? Worn.ASC()->FindAbilitySpecFromHandle(Handle) : nullptr;
	UCataclysmAuraSkill* Pyre = Spec ? Cast<UCataclysmAuraSkill>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("the aura"), Pyre))
	{
		return false;
	}
	Pyre->SkillName = TEXT("Living Pyre");
	Pyre->Params = UCataclysmSkillShapes::ParseParams(
		TEXT("Radius=4; Interval=1; Burn=1; Immune=Stun, Slow, Displacement"));
	if (!TestTrue(TEXT("set-up: it activates"),
			Worn.ASC()->TryActivateAbility(Handle, /*bAllowRemoteActivation=*/false)))
	{
		return false;
	}
	Pyre->Pulse();
	if (!TestTrue(TEXT("set-up: it is still running after a pulse"), Pyre->IsActive()))
	{
		return false;
	}
	TestTrue(TEXT("the imp inside is immune to a stun. If not, DT_EnchantmentEffects may be older than "
				  "the rows: run tools/generate_datatable_assets.py"),
		UCataclysmSkillTemplate::IsImmuneTo(Imp, TEXT("Stun")));
	TestFalse(TEXT("and not to a knockdown, which the aura's row does not name"),
		UCataclysmSkillTemplate::IsImmuneTo(Imp, TEXT("Knockdown")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTimedCleanseRowTest,
	"Cataclysm.Enchantments.TheCleanseRowRemovesABurnFiveSecondsIntoAFightAndNotBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "You are cleansed every 5 seconds". Issue #1833: the action `cleanse` on
 * `every_seconds`, every 5. WORN, so the row is read from the built table: a
 * burn another character put on the wearer is still there four seconds into a
 * fight and gone at five. THE TIMER COUNTS ONLY IN COMBAT, as every timed row's
 * does, so fifty seconds out of one remove nothing.
 */
bool FCataclysmTimedCleanseRowTest::RunTest(const FString&)
{
	CataclysmTimedRowTest::FFight Fight(*this, TEXT("Positive_You_are_cleansed_every_5_seconds"));
	if (!TestNotNull(TEXT("a wearer in a world"), Fight.ASC()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Burner = Fight.World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature to put the burn there"), Burner))
	{
		return false;
	}
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	UCataclysmSkillEffects::ApplyDamageOverTime(Burner, Fight.Wearer->Actor, 1.0f, 600.0f, Burn,
		/*bScalesWithInstigator=*/false);
	if (!TestTrue(TEXT("set-up: the wearer burns"), Fight.ASC()->HasMatchingGameplayTag(Burn)))
	{
		return false;
	}
	int32 Cleanses = 0;
	for (const FCataclysmPoolAction& Action : Fight.ASC()->GetPoolActions())
	{
		Cleanses += Action.bCleanse ? 1 : 0;
	}
	TestEqual(TEXT("the worn row became one cleanse. If none, DT_EnchantmentEffects may be older than the "
				   "rows: run tools/generate_datatable_assets.py"),
		Cleanses, 1);
	Fight.Until(4.0f);
	TestTrue(TEXT("four seconds into the fight: still burning"), Fight.ASC()->HasMatchingGameplayTag(Burn));
	Fight.Until(5.0f);
	TestFalse(TEXT("five seconds in: cleansed"), Fight.ASC()->HasMatchingGameplayTag(Burn));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOwnPointBlankDamageRowTest,
	"Cataclysm.Enchantments.TheOwnPointBlankDamageRowTakesItsShareOfEveryPointBlankHitAndCannotKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "You take 10%-20% of the damage dealt by your own point blank AOE skills".
 * Issue #1833, 2026-10-05: `health` at -10 to -20 per cent of `event_amount` on
 * `hit_dealt`, scoped to `Type.AOE.PointBlank`, with a stated trigger cooldown
 * of nought. WORN at the top of its roll, 20, by a wearer with 1000 health: a
 * hit of 200 by a strike takes nothing, one by a point blank skill takes 40,
 * and a second in the same moment takes 40 more, because a burst that strikes
 * five enemies is five hits. A DRAIN CANNOT KILL, ruled 2026-09-14.
 *
 * THE EVENT IS RAISED BY HAND with the amount and tags the player character
 * passes from a real hit; `test_pool_action_names_match_the_engine.py` holds
 * that call to passing an amount.
 */
bool FCataclysmOwnPointBlankDamageRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	FWorn Worn(TEXT("Negative_You_take_10_20_of_the_damage_dealt_by_your_own"), false);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	Worn.ASC()->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
	Worn.ASC()->SetNumericAttributeBase(Health, 1000.0f);
	const FGameplayTagContainer PointBlank = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.AOE.PointBlank"));
	const FGameplayTagContainer Strike = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.Strike, Type.Melee"));
	if (!TestFalse(TEXT("Type.AOE.PointBlank is in the vocabulary"), PointBlank.IsEmpty()))
	{
		return false;
	}
	const FName HitDealt(TEXT("hit_dealt"));

	Worn.ASC()->ActOnEvent(HitDealt, &Strike, 200.0f, /*bLanded=*/true, nullptr);
	TestEqual(TEXT("a strike's hit of 200 takes nothing"),
		Worn.ASC()->GetNumericAttribute(Health), 1000.0f, 0.01f);
	Worn.ASC()->ActOnEvent(HitDealt, &PointBlank, 200.0f, /*bLanded=*/true, nullptr);
	TestEqual(TEXT("a point blank hit of 200 takes 20% of it. If nothing, DT_EnchantmentEffects may be older "
				   "than the rows: run tools/generate_datatable_assets.py"),
		Worn.ASC()->GetNumericAttribute(Health), 960.0f, 0.01f);
	Worn.ASC()->ActOnEvent(HitDealt, &PointBlank, 200.0f, /*bLanded=*/true, nullptr);
	TestEqual(TEXT("a second in the same moment takes 40 more"),
		Worn.ASC()->GetNumericAttribute(Health), 920.0f, 0.01f);
	Worn.ASC()->ActOnEvent(HitDealt, &PointBlank, 100000.0f, /*bLanded=*/true, nullptr);
	TestEqual(TEXT("and a hit whose share is past all the health left leaves 1"),
		Worn.ASC()->GetNumericAttribute(Health), 1.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRetaliationSelfDamageRowsTest,
	"Cataclysm.Enchantments.TheThreeRetaliationSelfDamageRowsTakeTheirShareOfWhatRetaliationDealt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your retaliation damage also applies to you at 25%-50% effectiveness", "Your
 * retaliation damage also damages you at 20%-40% effectiveness" and "You take
 * 10%-20% of the damage you reflect". Issue #1833, 2026-10-05: each is `health`
 * at its negative share of `event_amount` on `retaliation_dealt`, with a stated
 * trigger cooldown of nought. "The damage you reflect" is retaliation in this
 * game, ruled the same day. Each WORN at the top of its roll by a wearer with
 * 1000 health: a payment of 300 that took no health takes nothing, one that took
 * 300 takes the share, and a second in the same moment takes it again, because
 * retaliation with a radius pays several enemies at once.
 */
bool FCataclysmRetaliationSelfDamageRowsTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	struct FCase
	{
		const TCHAR* Enchantment;
		float Share;
	};
	const FName RetaliationDealt(TEXT("retaliation_dealt"));
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	for (const FCase& Case : {
			 FCase{TEXT("Negative_Your_retaliation_damage_also_applies_to_you_at_2"), 50.0f},
			 FCase{TEXT("Negative_Your_retaliation_damage_also_damages_you_at_20"), 40.0f},
			 FCase{TEXT("Negative_You_take_10_20_of_the_damage_you_reflect"), 20.0f}})
	{
		FWorn Worn(Case.Enchantment, false);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		Worn.ASC()->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
		Worn.ASC()->SetNumericAttributeBase(Health, 1000.0f);
		const float Each = 300.0f * Case.Share / 100.0f;

		Worn.ASC()->ActOnEvent(RetaliationDealt, nullptr, 300.0f, /*bLanded=*/false, nullptr);
		TestEqual(FString::Printf(TEXT("%s: a payment that took no health takes nothing"), Case.Enchantment),
			Worn.ASC()->GetNumericAttribute(Health), 1000.0f, 0.01f);
		Worn.ASC()->ActOnEvent(RetaliationDealt, nullptr, 300.0f, /*bLanded=*/true, nullptr);
		TestEqual(FString::Printf(TEXT("%s: a payment of 300 takes %.0f%% of it. If nothing, DT_EnchantmentEffects "
									   "may be older than the rows: run tools/generate_datatable_assets.py"),
					  Case.Enchantment, Case.Share),
			Worn.ASC()->GetNumericAttribute(Health), 1000.0f - Each, 0.01f);
		Worn.ASC()->ActOnEvent(RetaliationDealt, nullptr, 300.0f, /*bLanded=*/true, nullptr);
		TestEqual(FString::Printf(TEXT("%s: a second in the same moment takes it again"), Case.Enchantment),
			Worn.ASC()->GetNumericAttribute(Health), 1000.0f - 2.0f * Each, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStarvationTenRowTest,
	"Cataclysm.Enchantments.StarvationTenPiecesGainAFamishedStackPerKillRaisingAllLeechUpToTen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Starvation (10-Piece Bonus): When you kill an enemy, gain a stack of
 * 'Famished.' Each stack increases all of your leech by 5%. Stacks last 4
 * seconds". Issue #1833, 2026-10-05: three own-stack rows on `kill`, one for
 * each of `life_leech`, `mana_leech` and `energy_shield_leech`, 5 increased a
 * stack for 4 seconds. THE CAP OF 10 IS A LABELLED JUDGEMENT of that day: the
 * sentence states none and an own-stack row must.
 *
 * EACH STAT IS READ AS A SHARE OF ITSELF WITH NO STACK, so the set's two-piece
 * 5% of leech cancels out. Six pieces hold no stack whatever is killed.
 */
bool FCataclysmStarvationTenRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmHealthThresholdRowTest;
	const TCHAR* Starvation = TEXT("Positive_Starvation_2_Piece_Bonus_You_have_5_life_m");
	const FName Kill(TEXT("kill"));
	const TCHAR* const Stats[] = {TEXT("life_leech"), TEXT("mana_leech"), TEXT("energy_shield_leech")};

	for (const int32 Pieces : {6, 10})
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		FWearer Wearer(World);
		WearSet(Wearer, Starvation, Pieces);
		UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;
		const auto Leech = [ASC](const TCHAR* Stat)
		{
			return ASC->StatAppliedTo(FName(Stat), FGameplayTagContainer(), 100.0f);
		};
		TMap<FString, float> Plain;
		for (const TCHAR* Stat : Stats)
		{
			Plain.Add(Stat, Leech(Stat));
			if (!TestTrue(FString::Printf(TEXT("%d pieces: %s is something before any kill"), Pieces, Stat),
					Plain[Stat] > 0.0f))
			{
				return false;
			}
		}
		const auto Expect = [&](const TCHAR* What, int32 Stacks)
		{
			for (const TCHAR* Stat : Stats)
			{
				TestEqual(FString::Printf(TEXT("%d pieces, %s: %s is %d%% above itself. If not at ten pieces, "
											   "DT_EnchantmentEffects may be older than the rows: run "
											   "tools/generate_datatable_assets.py"),
							  Pieces, What, Stat, 5 * Stacks),
					Leech(Stat) / Plain[Stat], 1.0f + 0.05f * Stacks, 0.0005f);
			}
		};

		ASC->ActOnEvent(Kill);
		ASC->ActOnEvent(Kill);
		Expect(TEXT("two kills"), Pieces == 10 ? 2 : 0);
		for (int32 More = 0; More < 10; ++More)
		{
			ASC->ActOnEvent(Kill);
		}
		Expect(TEXT("twelve kills"), Pieces == 10 ? 10 : 0);
		World->TimeSeconds += 3.9f;
		Expect(TEXT("just inside four seconds"), Pieces == 10 ? 10 : 0);
		World->TimeSeconds += 0.2f;
		Expect(TEXT("just after four seconds"), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBrutesHeartSixRowTest,
	"Cataclysm.Enchantments.BrutesHeartSixPiecesRaiseArmourByHalfOnlyBelowHalfHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Brute's Heart (6-Piece Bonus): When your health falls below 50%, you gain a
 * powerful aura that taunts all nearby enemies and increases your armor and
 * resistances by 50%". Issue #1833, ruled 2026-10-05: PARTLY BUILT. This is its
 * armour: `armor` increased 50 under `health_below` 50. No taunt exists, and the
 * resistances wait for a blow to ask for a resistance through the stat pipeline;
 * `docs/DECISIONS.md` records both.
 *
 * WORN, AND READ AS A SHARE OF THE ARMOUR AT FULL HEALTH: two pieces add nothing
 * at any health, six add nothing at half health exactly and half again below it.
 */
bool FCataclysmBrutesHeartSixRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmHealthThresholdRowTest;
	const TCHAR* BrutesHeart = TEXT("Positive_Brute_s_Heart_2_Piece_Bonus_You_gain_25_incr");

	for (const int32 Pieces : {2, 6})
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		FWearer Wearer(World);
		WearSet(Wearer, BrutesHeart, Pieces);
		const auto Armour = [&Wearer]()
		{
			return Wearer.AbilitySystem->StatAppliedTo(FName(TEXT("armor")), FGameplayTagContainer(), 1000.0f);
		};
		SetHealth(Wearer.AbilitySystem, 1000.0f, 1000.0f);
		const float AtFull = Armour();
		if (!TestTrue(TEXT("armour is something at full health"), AtFull > 0.0f))
		{
			return false;
		}
		SetHealth(Wearer.AbilitySystem, 1000.0f, 500.0f);
		TestEqual(FString::Printf(TEXT("%d pieces at half health exactly: no more armour"), Pieces),
			Armour() / AtFull, 1.0f, 0.0005f);
		SetHealth(Wearer.AbilitySystem, 1000.0f, 490.0f);
		TestEqual(FString::Printf(TEXT("%d pieces below half health. If six add nothing, DT_EnchantmentEffects "
									   "may be older than the rows: run tools/generate_datatable_assets.py"),
					  Pieces),
			Armour() / AtFull, Pieces == 6 ? 1.5f : 1.0f, 0.0005f);
	}
	return true;
}

// MECHANISM B2: a row action that repeats the skill just used, free. "Every skill use has a 5%-15% chance to cast a
// second time for free." The rows here are made by hand, as a row of the effect table would give them: no authored
// row carries the action yet. Issue #1833.
namespace CataclysmSkillRepeatTest
{
	/** A repeat row on `skill_use`: its chance, the share its repeat deals, and the skill tags it asks for. */
	FCataclysmPoolAction ARepeatRow(const TCHAR* Key, float Chance, float SharePercent, const TCHAR* TagCell = TEXT(""))
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("skill_use"));
		Action.Percent = Chance;
		Action.bRepeatSkill = true;
		Action.RepeatSharePercent = SharePercent;
		Action.TriggerKey = FName(Key);
		Action.RequiredTags = UCataclysmSkillShapes::TagsFromCell(TagCell);
		return Action;
	}

	/** The repeat roll pinned, and put back to "rolled" afterwards. */
	struct FRepeatRollPinned
	{
		explicit FRepeatRollPinned(const TCHAR* Roll)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.RepeatSkillRoll"));
			Set(Roll);
		}

		~FRepeatRollPinned()
		{
			Set(TEXT("-1"));
		}

		void Set(const TCHAR* Roll)
		{
			if (Variable)
			{
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
	};

	/** Every named Demonic skill, from the real table. The test player's damage type is Demonic. */
	TArray<FCataclysmWeaponSkill> EveryDemonicSkill()
	{
		return UCataclysmWeaponSkills::SkillsOfDamageType(UCataclysmWeaponSkills::LoadGeneratedTable(), TEXT("Demonic"));
	}

	const FCataclysmWeaponSkill* TheSkillNamed(const TArray<FCataclysmWeaponSkill>& Skills, const TCHAR* Name)
	{
		return Skills.FindByPredicate([Name](const FCataclysmWeaponSkill& Skill) { return Skill.Name == Name; });
	}

	/** Tells the world the player paid for this skill, aimed here, as `CommitAndBegin` does. */
	void ThePlayerUses(ACataclysmPlayerCharacter* Player, const FString& Name, const FGameplayTagContainer& Tags,
					   ECataclysmAbilitySlot Slot, const FVector& Aim)
	{
		UCataclysmCombatEvents::NoteSkillUsed(Player, Name, Tags, Slot, &Aim);
	}

	/** How many running skills of this name the character holds that no key finds: the repeats. */
	int32 RepeatsRunning(const UCataclysmAbilitySystemComponent* System, const FString& Name,
						 const UCataclysmSkillTemplate** OutOne = nullptr)
	{
		int32 Count = 0;
		for (const FGameplayAbilitySpec& Spec : System->GetActivatableAbilities())
		{
			const UCataclysmSkillTemplate* Skill = Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());
			if (Skill && Spec.IsActive() && Skill->SkillName == Name
				&& !Spec.GetDynamicSpecSourceTags().HasTagExact(CataclysmAbilitySlots::Tag(Skill->Slot)))
			{
				++Count;
				if (OutOne)
				{
					*OutOne = Skill;
				}
			}
		}
		return Count;
	}

	/** A possessed player, its ability system, and the projectile skill the tests repeat. */
	struct FRepeatRig
	{
		explicit FRepeatRig(UWorld* World)
		{
			Player = CataclysmKillCounterTest::SpawnPossessedPlayer(World);
			ACataclysmPlayerState* State = Player ? Player->GetPlayerState<ACataclysmPlayerState>() : nullptr;
			System = State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
			Skills = EveryDemonicSkill();
			Projectile = TheSkillNamed(Skills, TEXT("Carom"));
		}

		bool IsUsable() const { return Player && System && Projectile; }

		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		TArray<FCataclysmWeaponSkill> Skills;
		const FCataclysmWeaponSkill* Projectile = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRepeatRowRepeatsTest,
	"Cataclysm.Enchantments.ARepeatRowRepeatsTheSkillJustUsedFreeAtTheSameAim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRepeatRowRepeatsTest::RunTest(const FString&)
{
	using namespace CataclysmSkillRepeatTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRepeatRig Rig(World);
	FRepeatRollPinned Pinned(TEXT("0"));
	if (!TestTrue(TEXT("set-up: a possessed player and the Carom row"), Rig.IsUsable())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Pinned.Variable))
	{
		return false;
	}
	Rig.System->SetPoolActions({ARepeatRow(TEXT("Test:repeat"), 15.0f, 100.0f)});

	// THE USE RECORDS ONE REPEAT, OF THAT SKILL, AT THAT AIM. Nothing is started inside the use.
	const FVector Aim = Rig.Player->GetActorLocation() + FVector(900.0f, 300.0f, 0.0f);
	ThePlayerUses(Rig.Player, Rig.Projectile->Name, Rig.Projectile->Tags, ECataclysmAbilitySlot::Heavy, Aim);
	TestEqual(TEXT("the used skill is the one to repeat"), Rig.System->PendingRepeatSkill(), FName(*Rig.Projectile->Name));
	TestTrue(TEXT("at the point the use was aimed"), Rig.System->PendingRepeatAim().Equals(Aim, 0.01f));
	TestEqual(TEXT("at the whole of its damage"), Rig.System->PendingRepeatShare(), 1.0f, 0.001f);
	TestEqual(TEXT("and nothing is started yet"), RepeatsRunning(Rig.System, Rig.Projectile->Name), 0);

	// MADE: FREE, NOT A USE, AIMED AT THE SAME POINT.
	const float ManaBefore = Rig.System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetManaAttribute());
	const uint32 UsesBefore = UCataclysmCombatEvents::In(World)->SkillUsesSent();
	if (!TestTrue(TEXT("the repeat is made"), UCataclysmTriggeredSkill::MakePendingRepeat(Rig.Player)))
	{
		return false;
	}
	const UCataclysmSkillTemplate* Running = nullptr;
	if (TestEqual(TEXT("one repeat is running"), RepeatsRunning(Rig.System, Rig.Projectile->Name, &Running), 1) && Running)
	{
		TestTrue(TEXT("it is a free start"), Running->bFreeRepeat);
		TestTrue(TEXT("aimed where the use was aimed"), Running->FreeRepeatAim.Equals(Aim, 0.01f));
		TestEqual(TEXT("at the whole of its damage"), Running->FreeRepeatDamageShare, 1.0f, 0.001f);
	}
	TestEqual(TEXT("it paid no mana"), Rig.System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetManaAttribute()),
			  ManaBefore);
	TestEqual(TEXT("and sent no skill-used notice, so a repeat is not repeated"),
			  UCataclysmCombatEvents::In(World)->SkillUsesSent(), UsesBefore);
	TestTrue(TEXT("nothing is pending any more"), Rig.System->PendingRepeatSkill().IsNone());
	TestFalse(TEXT("and with nothing pending nothing is made"), UCataclysmTriggeredSkill::MakePendingRepeat(Rig.Player));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTwoRepeatRowsTest,
	"Cataclysm.Enchantments.TwoRepeatRowsOnOneUseMakeOneRepeatAtTheHigherShare",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTwoRepeatRowsTest::RunTest(const FString&)
{
	using namespace CataclysmSkillRepeatTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRepeatRig Rig(World);
	FRepeatRollPinned Pinned(TEXT("0"));
	if (!TestTrue(TEXT("set-up: a possessed player and the Carom row"), Rig.IsUsable())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Pinned.Variable))
	{
		return false;
	}
	const FVector Aim = Rig.Player->GetActorLocation() + FVector(900.0f, 0.0f, 0.0f);
	const FCataclysmPoolAction Half = ARepeatRow(TEXT("Test:half"), 40.0f, 50.0f);
	const FCataclysmPoolAction Whole = ARepeatRow(TEXT("Test:whole"), 15.0f, 100.0f);

	// BOTH ROWS PASS THEIR ROLL ON ONE USE. One repeat is kept, at the higher share, whichever row is asked first.
	Rig.System->SetPoolActions({Half, Whole});
	ThePlayerUses(Rig.Player, Rig.Projectile->Name, Rig.Projectile->Tags, ECataclysmAbilitySlot::Heavy, Aim);
	TestEqual(TEXT("the half row first, the whole row second: the repeat is whole"), Rig.System->PendingRepeatShare(),
			  1.0f, 0.001f);
	Rig.System->SetPoolActions({Whole, Half});
	ThePlayerUses(Rig.Player, Rig.Projectile->Name, Rig.Projectile->Tags, ECataclysmAbilitySlot::Heavy, Aim);
	TestEqual(TEXT("the whole row first, the half row second: the repeat is still whole"),
			  Rig.System->PendingRepeatShare(), 1.0f, 0.001f);

	// AND IT IS ONE REPEAT, NOT TWO.
	TestTrue(TEXT("the repeat is made"), UCataclysmTriggeredSkill::MakePendingRepeat(Rig.Player));
	TestFalse(TEXT("and there is no second one to make"), UCataclysmTriggeredSkill::MakePendingRepeat(Rig.Player));
	TestEqual(TEXT("one repeat is running"), RepeatsRunning(Rig.System, Rig.Projectile->Name), 1);

	// THE HALF ROW ALONE GIVES A REPEAT AT HALF.
	Rig.System->SetPoolActions({Half});
	ThePlayerUses(Rig.Player, Rig.Projectile->Name, Rig.Projectile->Tags, ECataclysmAbilitySlot::Heavy, Aim);
	TestEqual(TEXT("the half row alone: the repeat is at half"), Rig.System->PendingRepeatShare(), 0.5f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRepeatRowScopeTest,
	"Cataclysm.Enchantments.ARepeatRowKeepsToItsChanceAndItsTagsAndIgnoresTheBasicAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRepeatRowScopeTest::RunTest(const FString&)
{
	using namespace CataclysmSkillRepeatTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRepeatRig Rig(World);
	FRepeatRollPinned Pinned(TEXT("15"));
	if (!TestTrue(TEXT("set-up: a possessed player and the Carom row"), Rig.IsUsable())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Pinned.Variable))
	{
		return false;
	}
	const FVector Aim = Rig.Player->GetActorLocation() + FVector(900.0f, 0.0f, 0.0f);
	const FGameplayTagContainer Melee = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.Melee"));
	const FGameplayTagContainer Spell = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.Spell"));
	if (!TestTrue(TEXT("set-up: both tags exist"), Melee.Num() == 1 && Spell.Num() == 1))
	{
		return false;
	}

	// EACH CASE BELOW RECORDS NOTHING; the last is the control that the same use, in scope, records one.
	Rig.System->SetPoolActions({ARepeatRow(TEXT("Test:chance"), 15.0f, 100.0f)});
	ThePlayerUses(Rig.Player, Rig.Projectile->Name, Melee, ECataclysmAbilitySlot::Heavy, Aim);
	TestTrue(TEXT("a roll of 15 against a chance of 15 repeats nothing"), Rig.System->PendingRepeatSkill().IsNone());

	Pinned.Set(TEXT("0"));
	Rig.System->SetPoolActions({ARepeatRow(TEXT("Test:spells"), 20.0f, 100.0f, TEXT("Type.Spell"))});
	ThePlayerUses(Rig.Player, Rig.Projectile->Name, Melee, ECataclysmAbilitySlot::Heavy, Aim);
	TestTrue(TEXT("a row for spells repeats nothing for a melee skill"), Rig.System->PendingRepeatSkill().IsNone());

	ThePlayerUses(Rig.Player, Rig.Projectile->Name, Spell, ECataclysmAbilitySlot::BasicAttack, Aim);
	TestTrue(TEXT("a basic attack raises no skill use, so nothing is repeated"), Rig.System->PendingRepeatSkill().IsNone());

	ThePlayerUses(Rig.Player, Rig.Projectile->Name, Spell, ECataclysmAbilitySlot::Heavy, Aim);
	TestEqual(TEXT("control: the same row repeats a skill that carries its tag"), Rig.System->PendingRepeatSkill(),
			  FName(*Rig.Projectile->Name));

	// AND A SKILL THE TABLE DOES NOT HOLD IS RECORDED AND THEN NOT MADE.
	ThePlayerUses(Rig.Player, TEXT("No Such Skill"), Spell, ECataclysmAbilitySlot::Heavy, Aim);
	TestFalse(TEXT("a skill the table does not hold is not made"), UCataclysmTriggeredSkill::MakePendingRepeat(Rig.Player));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRepeatRowSelfBuffTest,
	"Cataclysm.Enchantments.ASelfBuffIsNotRepeatedByARowAndAMovementSkillIsNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRepeatRowSelfBuffTest::RunTest(const FString&)
{
	using namespace CataclysmSkillRepeatTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRepeatRig Rig(World);
	FRepeatRollPinned Pinned(TEXT("0"));
	const FCataclysmWeaponSkill* SelfBuff = TheSkillNamed(Rig.Skills, TEXT("Ashen Edge"));
	const FCataclysmWeaponSkill* Movement = TheSkillNamed(Rig.Skills, TEXT("Flashpoint"));
	if (!TestTrue(TEXT("set-up: a possessed player and the Carom row"), Rig.IsUsable())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Pinned.Variable)
		|| !TestNotNull(TEXT("set-up: Ashen Edge"), SelfBuff) || !TestNotNull(TEXT("set-up: Flashpoint"), Movement))
	{
		return false;
	}

	// THE RULE, ON ROWS OF THE REAL TABLE.
	TestTrue(TEXT("a projectile may be repeated"), UCataclysmTriggeredSkill::RepeatsFromARow(*Rig.Projectile));
	TestFalse(TEXT("a movement skill may not: Wild Magic's rule leaves it out"),
			  UCataclysmTriggeredSkill::RepeatsFromARow(*Movement));
	TestFalse(TEXT("a self buff may not, though Wild Magic's pool holds it"),
			  UCataclysmTriggeredSkill::RepeatsFromARow(*SelfBuff));
	TestEqual(TEXT("control: Wild Magic's own rule still lets the self buff through"),
			  static_cast<int32>(UCataclysmDungeonModifierEffects::WildMagicLeavesOut(*SelfBuff)),
			  static_cast<int32>(ECataclysmWildMagicLeftOut::InThePool));

	// AND IN PLAY: the use of a self buff is recorded, and the repeat is refused when it comes to be made.
	Rig.System->SetPoolActions({ARepeatRow(TEXT("Test:repeat"), 15.0f, 100.0f)});
	const FVector Aim = Rig.Player->GetActorLocation();
	ThePlayerUses(Rig.Player, SelfBuff->Name, SelfBuff->Tags, ECataclysmAbilitySlot::Support, Aim);
	TestFalse(TEXT("the self buff's repeat is not made"), UCataclysmTriggeredSkill::MakePendingRepeat(Rig.Player));
	TestEqual(TEXT("and no second copy of it runs"), RepeatsRunning(Rig.System, SelfBuff->Name), 0);
	return true;
}

namespace CataclysmAilmentRiderRowTest
{
	/** A creature three metres along X with a great deal of health and an attack worth 100. */
	ACataclysmEnemyCharacter* Creature(UWorld* World, float Metres = 3.0f)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(Metres * 100.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetHealth(100000.0f);
			Made->SetAttackDamage(100.0f);
		}
		return Made;
	}

	UCataclysmAbilitySystemComponent* SystemOf(const AActor* Who)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Who));
	}

	FGameplayTag Ailment(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	/** `By` applies ten seconds of an ailment to `On`, at one point a tick. */
	bool Ail(AActor* By, AActor* On, const FGameplayTag& Tag)
	{
		return UCataclysmSkillEffects::ApplyDamageOverTime(By, On, 1.0f, 10.0f, Tag,
			/*bScalesWithInstigator=*/false);
	}

	/** What a plain blow of 100 does to this character's health: no evasion, no block, no critical strike. */
	float TakenFromABlow(const UAbilitySystemComponent* Defender)
	{
		FCataclysmIncomingHit Blow;
		Blow.Damage = 100.0f;
		return UCataclysmDamageCalculation::Resolve(Blow, Defender, /*Tier=*/1,
			/*EvasionRoll=*/100.0f, /*BlockRoll=*/100.0f, /*CritRoll=*/100.0f).DealtToHealth;
	}

	const TCHAR* StaleAsset =
		TEXT(" If nothing, DT_EnchantmentEffects may be older than the rows: run "
			 "tools/generate_datatable_assets.py");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBleedingDamageTakenRowTest,
	"Cataclysm.Enchantments.TheBleedingRowRaisesWhatAnEnemyTakesOnlyWhileItCarriesTheWearersBleed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Bleeding enemies take 20%-40% increased damage from all sources". Issue #1833,
 * ruled 2026-10-06: a rider on Bleed, `ailment_damage_taken` 20 to 40, WORN at
 * the top of its roll. THE WHOLE LIFE OF A RIDER, read as what a plain blow of
 * 100 takes from the creature, as a share of what it took with no bleed:
 *
 *   a bleed another character applied adds nothing;
 *   the wearer's application adds 40, though it only refreshes that bleed;
 *   a cleanse ends the bleed and the rider with it;
 *   the wearer's bleed alone adds 40, and another character refreshing it leaves the 40;
 *   and with the row gone from the wearer, its next application takes the 40 down.
 */
bool FCataclysmBleedingDamageTakenRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmAilmentRiderRowTest;
	FWorn Worn(TEXT("Positive_Bleeding_enemies_take_20_40_increased_damage_f"), true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	AActor* Wearer = Worn.Wearer->Actor;
	ACataclysmEnemyCharacter* Enemy = Creature(Worn.World);
	ACataclysmEnemyCharacter* Other = Creature(Worn.World, 6.0f);
	const FGameplayTag Bleed = Ailment(TEXT("Keyword.DoT.Bleed"));
	const UCataclysmAbilitySystemComponent* Its = Enemy ? SystemOf(Enemy) : nullptr;
	if (!TestTrue(TEXT("two creatures and the bleed tag"), Its && Other && Bleed.IsValid()))
	{
		return false;
	}
	const float Plain = TakenFromABlow(Its);
	if (!TestTrue(TEXT("a plain blow takes something"), Plain > 0.0f))
	{
		return false;
	}
	const auto Share = [&]() { return TakenFromABlow(Its) / Plain; };

	TestTrue(TEXT("set-up: another character's bleed lands"), Ail(Other, Enemy, Bleed));
	TestEqual(TEXT("a bleed another character applied adds nothing"), Share(), 1.0f, 0.001f);
	TestTrue(TEXT("set-up: the wearer's bleed lands"), Ail(Wearer, Enemy, Bleed));
	TestEqual(*(FString(TEXT("the wearer's application adds 40, though it only refreshes that bleed.")) + StaleAsset),
		Share(), 1.4f, 0.001f);
	UCataclysmDebuffs::Cleanse(Enemy);
	if (!TestFalse(TEXT("set-up: a cleanse ends the bleed"), Its->HasMatchingGameplayTag(Bleed)))
	{
		return false;
	}
	TestEqual(TEXT("and the rider ends with it"), Share(), 1.0f, 0.001f);
	TestTrue(TEXT("set-up: another character's bleed lands again"), Ail(Other, Enemy, Bleed));
	TestEqual(TEXT("and the ended rider does not come back under somebody else's bleed"), Share(), 1.0f, 0.001f);
	UCataclysmDebuffs::Cleanse(Enemy);

	Ail(Wearer, Enemy, Bleed);
	TestEqual(TEXT("the wearer's bleed alone adds 40"), Share(), 1.4f, 0.001f);
	Ail(Other, Enemy, Bleed);
	TestEqual(TEXT("and another character refreshing it leaves the 40"), Share(), 1.4f, 0.001f);
	Worn.ASC()->SetPoolActions({});
	Ail(Wearer, Enemy, Bleed);
	TestEqual(TEXT("with the row gone from the wearer, its next application takes the 40 down"),
		Share(), 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAilmentArmourRowsTest,
	"Cataclysm.Enchantments.TheBurnAndDiseaseArmourRowsRemoveTheirShareOfAnAilingEnemysArmour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Enemies affected by your burn effects have 10%-20% reduced armor" and "Disease
 * effects you apply also reduce enemy armor by 5%-15%". Issue #1833, ruled
 * 2026-10-06: riders on Burn and on Disease, `ailment_armor_removed`, each WORN at
 * the top of its roll. Read as the armour the creature has had removed, which is
 * the one figure every armour reduction on a character adds into: nothing before,
 * the row's share while it carries the wearer's ailment, nothing under the OTHER
 * ailment, and nothing once the ailment is cleansed. A stack another row placed
 * on it adds to the rider.
 */
bool FCataclysmAilmentArmourRowsTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmAilmentRiderRowTest;
	struct FCase
	{
		const TCHAR* Enchantment;
		const TCHAR* Own;
		const TCHAR* Unrelated;
		float Share;
	};
	for (const FCase& Case : {
			 FCase{TEXT("Positive_Enemies_affected_by_your_burn_effects_have_10_2"),
				   TEXT("Keyword.DoT.Burn"), TEXT("Keyword.DoT.Disease"), 20.0f},
			 FCase{TEXT("Positive_Disease_effects_you_apply_also_reduce_enemy_armo"),
				   TEXT("Keyword.DoT.Disease"), TEXT("Keyword.DoT.Burn"), 15.0f}})
	{
		FWorn Worn(Case.Enchantment, true);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		ACataclysmEnemyCharacter* Enemy = Creature(Worn.World);
		UCataclysmAbilitySystemComponent* Its = Enemy ? SystemOf(Enemy) : nullptr;
		const FGameplayTag Own = Ailment(Case.Own);
		const FGameplayTag Unrelated = Ailment(Case.Unrelated);
		if (!TestTrue(TEXT("a creature and both tags"), Its && Own.IsValid() && Unrelated.IsValid()))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("%s: nothing removed before any ailment"), Case.Own),
			Its->ArmourRemovedPercentNow(), 0.0f, 0.001f);
		Ail(Worn.Wearer->Actor, Enemy, Unrelated);
		TestEqual(FString::Printf(TEXT("%s: nothing under the other ailment"), Case.Own),
			Its->ArmourRemovedPercentNow(), 0.0f, 0.001f);
		Ail(Worn.Wearer->Actor, Enemy, Own);
		TestEqual(FString::Printf(TEXT("%s: the row's share while it carries the wearer's ailment.%s"),
					  Case.Own, StaleAsset),
			Its->ArmourRemovedPercentNow(), Case.Share, 0.001f);
		Its->ReceivePlacedStack(FName(TEXT("Test:armour")), 30.0f, 5.0f, /*Cap=*/1, /*bCutsDamage=*/false);
		TestEqual(FString::Printf(TEXT("%s: a placed stack of 30 adds to it"), Case.Own),
			Its->ArmourRemovedPercentNow(), Case.Share + 30.0f, 0.001f);
		UCataclysmDebuffs::Cleanse(Enemy);
		TestEqual(FString::Printf(TEXT("%s: cleansed, only the placed stack is left"), Case.Own),
			Its->ArmourRemovedPercentNow(), 30.0f, 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPoisonedDamageDealtRowTest,
	"Cataclysm.Enchantments.ThePoisonedRowTakesItsShareOffWhatAPoisonedEnemysAttacksAreWorth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Poisoned enemies deal 2%-4% less damage". Issue #1833, ruled 2026-10-06: a
 * rider on Poison, `ailment_damage_dealt` 2 to 4, WORN at the top of its roll.
 * Read as what the creature's attacks are worth, `WeaponDamageOf`, which is the
 * figure a blow of its is priced from: 96% of what they were worth while it
 * carries the wearer's poison, and the whole again once cleansed.
 */
bool FCataclysmPoisonedDamageDealtRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmAilmentRiderRowTest;
	FWorn Worn(TEXT("Positive_Poisoned_enemies_deal_2_4_less_damage"), true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Enemy = Creature(Worn.World);
	const UCataclysmAbilitySystemComponent* Its = Enemy ? SystemOf(Enemy) : nullptr;
	const FGameplayTag Poison = Ailment(TEXT("Keyword.DoT.Poison"));
	if (!TestTrue(TEXT("a creature and the poison tag"), Its && Poison.IsValid()))
	{
		return false;
	}
	const float Plain = UCataclysmSkillEffects::WeaponDamageOf(Its);
	if (!TestTrue(TEXT("set-up: its attacks are worth something"), Plain > 0.0f))
	{
		return false;
	}
	Ail(Worn.Wearer->Actor, Enemy, Poison);
	TestEqual(*(FString(TEXT("poisoned by the wearer, its attacks are worth 96% of that.")) + StaleAsset),
		UCataclysmSkillEffects::WeaponDamageOf(Its) / Plain, 0.96f, 0.0005f);
	UCataclysmDebuffs::Cleanse(Enemy);
	TestEqual(TEXT("cleansed, the whole again"), UCataclysmSkillEffects::WeaponDamageOf(Its) / Plain, 1.0f, 0.0005f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDiseaseHealingRowTest,
	"Cataclysm.Enchantments.TheDiseaseHealingRowStopsADiseasedEnemyBeingHealedAtItsTopRoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Disease effects reduce enemy healing by 50%-100%". Issue #1833, ruled
 * 2026-10-06: a rider on Disease, `ailment_healing_received` 50 to 100, WORN at
 * the top of its roll, 100, where the healing a diseased creature receives is
 * nothing. Read through `UCataclysmRegeneration::TopUp`, the path regeneration,
 * leech and an enemy's heals all take: a top-up of 1000 restores nothing while
 * the creature carries the wearer's disease, and what it restored before once
 * cleansed.
 */
bool FCataclysmDiseaseHealingRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmAilmentRiderRowTest;
	FWorn Worn(TEXT("Positive_Disease_effects_reduce_enemy_healing_by_50_100"), true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Enemy = Creature(Worn.World);
	UCataclysmAbilitySystemComponent* Its = Enemy ? SystemOf(Enemy) : nullptr;
	const FGameplayTag Disease = Ailment(TEXT("Keyword.DoT.Disease"));
	if (!TestTrue(TEXT("a creature and the disease tag"), Its && Disease.IsValid()))
	{
		return false;
	}
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute Maximum = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const auto Restored = [&]()
	{
		Its->SetNumericAttributeBase(Health, 50000.0f);
		UCataclysmRegeneration::TopUp(*Its, Health, Maximum, 1000.0f);
		return Its->GetNumericAttribute(Health) - 50000.0f;
	};
	const float Plain = Restored();
	if (!TestTrue(TEXT("set-up: a top-up of 1000 restores something"), Plain > 0.0f))
	{
		return false;
	}
	Ail(Worn.Wearer->Actor, Enemy, Disease);
	TestEqual(*(FString(TEXT("diseased by the wearer at the top roll, a top-up restores nothing.")) + StaleAsset),
		Restored(), 0.0f, 0.5f);
	UCataclysmDebuffs::Cleanse(Enemy);
	TestEqual(TEXT("cleansed, what it restored before"), Restored(), Plain, 0.5f);
	return true;
}

namespace CataclysmAilmentSpeedRowTest
{
	/** An Imp at the origin, which has a designed walk speed and a designed attack interval. */
	ACataclysmImpCharacter* AnImp(UWorld* World)
	{
		return World->SpawnActor<ACataclysmImpCharacter>(FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	}

	/** Its walk speed as the movement component holds it, refreshed first. */
	float WalkOf(ACataclysmEnemyCharacter* Creature)
	{
		Creature->RefreshWalkSpeed();
		return Creature->GetCharacterMovement()->MaxWalkSpeed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPoisonedSlowRowTest,
	"Cataclysm.Enchantments.ThePoisonedSlowRowSlowsAPoisonedEnemysWalkingAndItsAttackingAlike",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Poisoned enemies are slowed by 30%-50%". Issue #1833, ruled 2026-10-06: a
 * rider on Poison, `ailment_speed` 30 to 50, WORN at the top of its roll. BOTH
 * SPEEDS, as Cripple was read: an Imp carrying the wearer's poison walks at half
 * its speed and takes twice as long between attacks, and both come back when
 * the poison is cleansed.
 */
bool FCataclysmPoisonedSlowRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmAilmentRiderRowTest;
	using namespace CataclysmAilmentSpeedRowTest;
	FWorn Worn(TEXT("Positive_Poisoned_enemies_are_slowed_by_30_50"), true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	ACataclysmImpCharacter* Imp = AnImp(Worn.World);
	const FGameplayTag Poison = Ailment(TEXT("Keyword.DoT.Poison"));
	if (!TestTrue(TEXT("an Imp and the poison tag"), Imp && Poison.IsValid()))
	{
		return false;
	}
	const float Walk = WalkOf(Imp);
	const float Interval = Imp->SecondsBetweenAttacks();
	if (!TestTrue(TEXT("set-up: the Imp walks and attacks at something"), Walk > 0.0f && Interval > 0.0f))
	{
		return false;
	}
	Ail(Worn.Wearer->Actor, Imp, Poison);
	TestEqual(*(FString(TEXT("poisoned by the wearer, it walks at half its speed.")) + StaleAsset),
		WalkOf(Imp) / Walk, 0.5f, 0.001f);
	TestEqual(TEXT("and takes twice as long between attacks"),
		Imp->SecondsBetweenAttacks() / Interval, 2.0f, 0.001f);
	UCataclysmDebuffs::Cleanse(Imp);
	TestEqual(TEXT("cleansed, it walks as before"), WalkOf(Imp) / Walk, 1.0f, 0.001f);
	TestEqual(TEXT("and attacks as before"), Imp->SecondsBetweenAttacks() / Interval, 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBleedingMoveRowTest,
	"Cataclysm.Enchantments.TheBleedingMoveRowSlowsABleedingEnemysWalkingAndNotItsAttacking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Bleeding enemies move 5%-10% slower". Issue #1833, ruled 2026-10-06: a rider
 * on Bleed, `ailment_movement_speed` 5 to 10, WORN at the top of its roll.
 * MOVEMENT ALONE, as written: an Imp carrying the wearer's bleed walks at nine
 * tenths of its speed and attacks on the interval it had, and the walk comes
 * back when the bleed is cleansed.
 */
bool FCataclysmBleedingMoveRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmAilmentRiderRowTest;
	using namespace CataclysmAilmentSpeedRowTest;
	FWorn Worn(TEXT("Positive_Bleeding_enemies_move_5_10_slower"), true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	ACataclysmImpCharacter* Imp = AnImp(Worn.World);
	const FGameplayTag Bleed = Ailment(TEXT("Keyword.DoT.Bleed"));
	if (!TestTrue(TEXT("an Imp and the bleed tag"), Imp && Bleed.IsValid()))
	{
		return false;
	}
	const float Walk = WalkOf(Imp);
	const float Interval = Imp->SecondsBetweenAttacks();
	if (!TestTrue(TEXT("set-up: the Imp walks and attacks at something"), Walk > 0.0f && Interval > 0.0f))
	{
		return false;
	}
	Ail(Worn.Wearer->Actor, Imp, Bleed);
	TestEqual(*(FString(TEXT("bleeding from the wearer, it walks at nine tenths of its speed.")) + StaleAsset),
		WalkOf(Imp) / Walk, 0.9f, 0.001f);
	TestEqual(TEXT("and attacks on the interval it had"),
		Imp->SecondsBetweenAttacks() / Interval, 1.0f, 0.001f);
	UCataclysmDebuffs::Cleanse(Imp);
	TestEqual(TEXT("cleansed, it walks as before"), WalkOf(Imp) / Walk, 1.0f, 0.001f);
	return true;
}

// THE `attack_use` EVENT: every paid use, the basic attack included, with the skill in hand. Ruled 2026-10-06, for
// "Your melee attacks have a 12%-15% chance to trigger twice". The rows are made by hand, as above. Issue #1833.
namespace CataclysmAttackUseTest
{
	using namespace CataclysmSkillRepeatTest;

	/** A repeat row on `attack_use`. */
	FCataclysmPoolAction AnAttackRow(const TCHAR* Key, float SharePercent, const TCHAR* TagCell = TEXT(""))
	{
		FCataclysmPoolAction Action = ARepeatRow(Key, 15.0f, SharePercent, TagCell);
		Action.Event = FName(TEXT("attack_use"));
		return Action;
	}

	/** Gives the player this weapon and hands back a copy of the basic attack it then holds. */
	bool HoldTheBasicAttackOf(ACataclysmPlayerCharacter* Player, const TCHAR* WeaponType, FCataclysmWeaponSkill& OutBasic)
	{
		UCataclysmWeaponSlotsComponent* Slots =
			Player ? Player->FindComponentByClass<UCataclysmWeaponSlotsComponent>() : nullptr;
		if (!Slots)
		{
			return false;
		}
		Slots->EquipWeaponType(WeaponType);
		const FCataclysmWeaponSkill* Held = UCataclysmTriggeredSkill::HeldBasicAttack(Player);
		if (!Held)
		{
			return false;
		}
		OutBasic = *Held;
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttackUseBasicAttackTest,
	"Cataclysm.Enchantments.AnAttackUseRowRepeatsTheBasicAttackFree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAttackUseBasicAttackTest::RunTest(const FString&)
{
	using namespace CataclysmAttackUseTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRepeatRig Rig(World);
	FRepeatRollPinned Pinned(TEXT("0"));
	FCataclysmWeaponSkill Basic;
	if (!TestTrue(TEXT("set-up: a possessed player and the Carom row"), Rig.IsUsable())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Pinned.Variable)
		|| !TestTrue(TEXT("set-up: a Sword and its basic attack"), HoldTheBasicAttackOf(Rig.Player, TEXT("Sword"), Basic))
		|| !TestEqual(TEXT("set-up: it carries the one name every basic attack has"), Basic.Name,
					  FString(UCataclysmWeaponSkills::BasicAttackName)))
	{
		return false;
	}
	const FVector Aim = Rig.Player->GetActorLocation() + FVector(200.0f, 50.0f, 0.0f);

	// A ROW ON `skill_use` STILL IGNORES THE BASIC ATTACK. This is the 2026-09-14 ruling, unchanged.
	Rig.System->SetPoolActions({ARepeatRow(TEXT("Test:skill"), 15.0f, 100.0f)});
	ThePlayerUses(Rig.Player, Basic.Name, Basic.Tags, ECataclysmAbilitySlot::BasicAttack, Aim);
	if (!TestTrue(TEXT("a row on skill_use records nothing for the basic attack"), Rig.System->PendingRepeatSkill().IsNone()))
	{
		return false;
	}

	// A ROW ON `attack_use` RECORDS IT, AT ITS AIM.
	Rig.System->SetPoolActions({AnAttackRow(TEXT("Test:attack"), 100.0f, TEXT("Type.Melee"))});
	ThePlayerUses(Rig.Player, Basic.Name, Basic.Tags, ECataclysmAbilitySlot::BasicAttack, Aim);
	if (!TestEqual(TEXT("a row on attack_use records the basic attack"), Rig.System->PendingRepeatSkill(),
				   FName(UCataclysmWeaponSkills::BasicAttackName)))
	{
		return false;
	}
	TestTrue(TEXT("at the point the swing was aimed"), Rig.System->PendingRepeatAim().Equals(Aim, 0.01f));

	// AND IT IS MADE, FROM THE WEAPON'S OWN ROW, FREE AND NOT AS A USE.
	const float ManaBefore = Rig.System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetManaAttribute());
	const uint32 UsesBefore = UCataclysmCombatEvents::In(World)->SkillUsesSent();
	if (!TestTrue(TEXT("the repeat of the basic attack is made"), UCataclysmTriggeredSkill::MakePendingRepeat(Rig.Player)))
	{
		return false;
	}
	TestEqual(TEXT("it paid no mana"), Rig.System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetManaAttribute()),
			  ManaBefore);
	TestEqual(TEXT("and sent no skill-used notice, so a repeat is not repeated"),
			  UCataclysmCombatEvents::In(World)->SkillUsesSent(), UsesBefore);
	TestFalse(TEXT("and there is no second one to make"), UCataclysmTriggeredSkill::MakePendingRepeat(Rig.Player));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttackUseEverySkillTest,
	"Cataclysm.Enchantments.ASkillUseAlsoRaisesAttackUseAndOneUseStillGivesOneRepeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAttackUseEverySkillTest::RunTest(const FString&)
{
	using namespace CataclysmAttackUseTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRepeatRig Rig(World);
	FRepeatRollPinned Pinned(TEXT("0"));
	if (!TestTrue(TEXT("set-up: a possessed player and the Carom row"), Rig.IsUsable())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Pinned.Variable))
	{
		return false;
	}
	const FVector Aim = Rig.Player->GetActorLocation() + FVector(900.0f, 0.0f, 0.0f);

	// A SKILL THAT IS NOT THE BASIC ATTACK RAISES `attack_use` TOO.
	Rig.System->SetPoolActions({AnAttackRow(TEXT("Test:attack"), 100.0f)});
	ThePlayerUses(Rig.Player, Rig.Projectile->Name, Rig.Projectile->Tags, ECataclysmAbilitySlot::Heavy, Aim);
	if (!TestEqual(TEXT("a row on attack_use records a heavy skill"), Rig.System->PendingRepeatSkill(),
				   FName(*Rig.Projectile->Name)))
	{
		return false;
	}

	// A ROW ON EACH EVENT, BOTH PASSING ON ONE USE: one repeat, at the higher share, whichever event holds it.
	Rig.System->SetPoolActions({ARepeatRow(TEXT("Test:skill"), 15.0f, 50.0f), AnAttackRow(TEXT("Test:attack"), 100.0f)});
	ThePlayerUses(Rig.Player, Rig.Projectile->Name, Rig.Projectile->Tags, ECataclysmAbilitySlot::Heavy, Aim);
	TestEqual(TEXT("half on skill_use and whole on attack_use: the repeat is whole"), Rig.System->PendingRepeatShare(),
			  1.0f, 0.001f);
	Rig.System->SetPoolActions({ARepeatRow(TEXT("Test:skill"), 15.0f, 100.0f), AnAttackRow(TEXT("Test:attack"), 50.0f)});
	ThePlayerUses(Rig.Player, Rig.Projectile->Name, Rig.Projectile->Tags, ECataclysmAbilitySlot::Heavy, Aim);
	TestEqual(TEXT("whole on skill_use and half on attack_use: the repeat is still whole"),
			  Rig.System->PendingRepeatShare(), 1.0f, 0.001f);

	TestTrue(TEXT("the repeat is made"), UCataclysmTriggeredSkill::MakePendingRepeat(Rig.Player));
	TestFalse(TEXT("and there is no second one to make"), UCataclysmTriggeredSkill::MakePendingRepeat(Rig.Player));
	TestEqual(TEXT("one repeat is running"), RepeatsRunning(Rig.System, Rig.Projectile->Name), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttackUseShapeTagTest,
	"Cataclysm.Enchantments.ABasicAttackIsAskedWithTheMeleeOrRangedTagOfItsShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAttackUseShapeTagTest::RunTest(const FString&)
{
	using namespace CataclysmAttackUseTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRepeatRig Rig(World);
	FRepeatRollPinned Pinned(TEXT("0"));
	FCataclysmWeaponSkill Basic;
	const FGameplayTag Ranged = UCataclysmDamageCalculation::RangedTag();
	const FName BasicName(UCataclysmWeaponSkills::BasicAttackName);
	if (!TestTrue(TEXT("set-up: a possessed player and the Carom row"), Rig.IsUsable())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Pinned.Variable)
		|| !TestTrue(TEXT("set-up: the ranged tag exists"), Ranged.IsValid())
		|| !TestTrue(TEXT("set-up: a Wand and its basic attack"), HoldTheBasicAttackOf(Rig.Player, TEXT("Wand"), Basic))
		|| !TestTrue(TEXT("set-up: a Wand's basic attack is a projectile"), Basic.Shape == ECataclysmSkillShape::Projectile)
		|| !TestFalse(TEXT("set-up: and carries no ranged tag of its own"), Basic.Tags.HasTag(Ranged)))
	{
		return false;
	}
	const FVector Aim = Rig.Player->GetActorLocation() + FVector(900.0f, 0.0f, 0.0f);

	// A PROJECTILE BASIC ATTACK IS A RANGED ATTACK TO A ROW ON `attack_use`, AND NOT A MELEE ONE.
	Rig.System->SetPoolActions({AnAttackRow(TEXT("Test:ranged"), 100.0f, TEXT("Type.Ranged"))});
	ThePlayerUses(Rig.Player, Basic.Name, Basic.Tags, ECataclysmAbilitySlot::BasicAttack, Aim);
	if (!TestEqual(TEXT("a row for ranged attacks records a Wand's basic attack"), Rig.System->PendingRepeatSkill(),
				   BasicName))
	{
		return false;
	}
	Rig.System->SetPoolActions({AnAttackRow(TEXT("Test:melee"), 100.0f, TEXT("Type.Melee"))});
	ThePlayerUses(Rig.Player, Basic.Name, Basic.Tags, ECataclysmAbilitySlot::BasicAttack, Aim);
	TestTrue(TEXT("a row for melee attacks records nothing for it"), Rig.System->PendingRepeatSkill().IsNone());

	// THE ATTACK'S OWN TAGS ARE UNTOUCHED: the tag was added for the question only.
	const FCataclysmWeaponSkill* Held = UCataclysmTriggeredSkill::HeldBasicAttack(Rig.Player);
	TestTrue(TEXT("the held basic attack still carries no ranged tag"), Held && !Held->Tags.HasTag(Ranged));

	// A STRIKE BASIC ATTACK IS A MELEE ATTACK AND NOT A RANGED ONE.
	if (!TestTrue(TEXT("set-up: a Sword and its basic attack"), HoldTheBasicAttackOf(Rig.Player, TEXT("Sword"), Basic)))
	{
		return false;
	}
	Rig.System->SetPoolActions({AnAttackRow(TEXT("Test:ranged"), 100.0f, TEXT("Type.Ranged"))});
	ThePlayerUses(Rig.Player, Basic.Name, Basic.Tags, ECataclysmAbilitySlot::BasicAttack, Aim);
	TestTrue(TEXT("a row for ranged attacks records nothing for a Sword's basic attack"),
			 Rig.System->PendingRepeatSkill().IsNone());
	Rig.System->SetPoolActions({AnAttackRow(TEXT("Test:melee"), 100.0f, TEXT("Type.Melee"))});
	ThePlayerUses(Rig.Player, Basic.Name, Basic.Tags, ECataclysmAbilitySlot::BasicAttack, Aim);
	TestEqual(TEXT("a row for melee attacks records it"), Rig.System->PendingRepeatSkill(), BasicName);
	return true;
}

// A ROW THAT TRIGGERS A DIFFERENT HELD SKILL. Ruled 2026-10-06, for Spellblade's Will: "Your melee attacks have a 25%
// chance to trigger an ability with a cooldown" and "Your melee attacks have a 25% of triggering one of your spells.
// This does not put the spell on cooldown but does use it's mana cost". The rows are made by hand. Issue #1833.
namespace CataclysmHeldTriggerTest
{
	using namespace CataclysmAttackUseTest;

	/** A trigger row on `attack_use`, at a chance of 25: a spell row, or a row for a skill with a cooldown. */
	FCataclysmPoolAction ATriggerRow(const TCHAR* Key, bool bSpell, const TCHAR* TagCell = TEXT(""))
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("attack_use"));
		Action.Percent = 25.0f;
		Action.bTriggerHeldSpell = bSpell;
		Action.bTriggerHeldSkill = !bSpell;
		Action.TriggerKey = FName(Key);
		Action.RequiredTags = UCataclysmSkillShapes::TagsFromCell(TagCell);
		return Action;
	}

	/** One console variable pinned, and put back to "for real" afterwards. */
	struct FHeldTriggerPinned
	{
		FHeldTriggerPinned(const TCHAR* Name, const TCHAR* Value)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(Name);
			Set(Value);
		}

		~FHeldTriggerPinned()
		{
			Set(TEXT("-1"));
		}

		void Set(const TCHAR* Value)
		{
			if (Variable)
			{
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
	};

	/** The names of a pool, in its order, joined for one comparison and one message. */
	FString NamesOf(const TArray<FCataclysmWeaponSkill>& Pool)
	{
		TArray<FString> Names;
		for (const FCataclysmWeaponSkill& Skill : Pool)
		{
			Names.Add(Skill.Name);
		}
		return FString::Join(Names, TEXT(", "));
	}

	/** Where this name stands in the pool, as the text the pick variable takes; "-1" when it is not there. */
	FString PickOf(const TArray<FCataclysmWeaponSkill>& Pool, const TCHAR* Name)
	{
		return FString::FromInt(
			Pool.IndexOfByPredicate([Name](const FCataclysmWeaponSkill& Skill) { return Skill.Name == Name; }));
	}

	float ManaOf(const UCataclysmAbilitySystemComponent* System)
	{
		return System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetManaAttribute());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHeldTriggerCooldownSkillTest,
	"Cataclysm.Enchantments.AnAttackTriggersADifferentHeldSkillWithACooldownFree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHeldTriggerCooldownSkillTest::RunTest(const FString&)
{
	using namespace CataclysmHeldTriggerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRepeatRig Rig(World);
	FHeldTriggerPinned Roll(TEXT("Cataclysm.TriggerHeldSkillRoll"), TEXT("0"));
	FHeldTriggerPinned Pick(TEXT("Cataclysm.TriggerHeldSkillPick"), TEXT("0"));
	FCataclysmWeaponSkill Basic;
	if (!TestTrue(TEXT("set-up: a possessed player"), Rig.IsUsable())
		|| !TestTrue(TEXT("set-up: the roll and the pick can be pinned"), Roll.Variable && Pick.Variable)
		|| !TestTrue(TEXT("set-up: a Sword and its basic attack"), HoldTheBasicAttackOf(Rig.Player, TEXT("Sword"), Basic)))
	{
		return false;
	}
	const FName BasicName(*Basic.Name);
	const FVector Aim = Rig.Player->GetActorLocation() + FVector(200.0f, 50.0f, 0.0f);

	// THE POOL IS WHAT THE CHARACTER HOLDS THAT HAS A COOLDOWN AND MAY BE TRIGGERED. A Demonic Sword holds six: the
	// basic attack has no cooldown, Ashen Edge is a self buff, Flashpoint is a movement skill and Touch Off needs a
	// burning enemy. Two are left.
	const TArray<FCataclysmWeaponSkill> Pool =
		UCataclysmTriggeredSkill::HeldSkillsToTrigger(Rig.Player, BasicName, /*bSpells=*/false);
	if (!TestEqual(TEXT("the skills a Sword's basic attack may trigger"), NamesOf(Pool), FString(TEXT("Quench, Extinction"))))
	{
		return false;
	}
	// NEVER THE SKILL JUST USED.
	TestEqual(TEXT("after a use of Quench, Quench is not in the pool"),
			  NamesOf(UCataclysmTriggeredSkill::HeldSkillsToTrigger(Rig.Player, FName(TEXT("Quench")), false)),
			  FString(TEXT("Extinction")));
	TestEqual(TEXT("and a Sword holds no spell to trigger"),
			  UCataclysmTriggeredSkill::HeldSkillsToTrigger(Rig.Player, BasicName, /*bSpells=*/true).Num(), 0);

	// A ROLL AT THE CHANCE TRIGGERS NOTHING.
	Rig.System->SetPoolActions({ATriggerRow(TEXT("Test:two"), /*bSpell=*/false, TEXT("Type.Melee"))});
	Roll.Set(TEXT("25"));
	ThePlayerUses(Rig.Player, Basic.Name, Basic.Tags, ECataclysmAbilitySlot::BasicAttack, Aim);
	TestTrue(TEXT("a roll of 25 against a chance of 25 records nothing"), Rig.System->PendingHeldTriggerUsedSkill().IsNone());

	// A ROLL BELOW IT RECORDS ONE, FOR THE SKILL USED, AND STARTS NOTHING INSIDE THE USE.
	Roll.Set(TEXT("0"));
	ThePlayerUses(Rig.Player, Basic.Name, Basic.Tags, ECataclysmAbilitySlot::BasicAttack, Aim);
	if (!TestEqual(TEXT("a melee basic attack records a trigger"), Rig.System->PendingHeldTriggerUsedSkill(), BasicName))
	{
		return false;
	}
	TestTrue(TEXT("for a skill with a cooldown, and not for a spell"),
			 Rig.System->PendingHeldTriggerWantsACooldownSkill() && !Rig.System->PendingHeldTriggerWantsASpell());
	TestTrue(TEXT("at the point the swing was aimed"), Rig.System->PendingHeldTriggerAim().Equals(Aim, 0.01f));

	// MADE: FREE, NOT A USE, AND NO COOLDOWN STARTED.
	const float ManaBefore = ManaOf(Rig.System);
	const uint32 UsesBefore = UCataclysmCombatEvents::In(World)->SkillUsesSent();
	if (!TestTrue(TEXT("the trigger is made"), UCataclysmTriggeredSkill::MakePendingHeldTrigger(Rig.Player)))
	{
		return false;
	}
	TestEqual(TEXT("it paid no mana"), ManaOf(Rig.System), ManaBefore);
	TestEqual(TEXT("it sent no skill-used notice"), UCataclysmCombatEvents::In(World)->SkillUsesSent(), UsesBefore);
	TestFalse(TEXT("it started no cooldown on the heavy slot"),
			  Rig.System->HasMatchingGameplayTag(UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Heavy)));
	TestFalse(TEXT("and there is no second one to make"), UCataclysmTriggeredSkill::MakePendingHeldTrigger(Rig.Player));

	// A HELD SKILL WHOSE COOLDOWN IS RUNNING IS STILL TRIGGERED, AND ITS COOLDOWN IS UNCHANGED. Ruled 2026-10-06. The
	// pick is pinned to Quench, the heavy skill; a held cooldown tag with no charge recorded is a running cooldown.
	const FGameplayTag HeavyCooldown = UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Heavy);
	Rig.System->AddLooseGameplayTag(HeavyCooldown);
	if (!TestEqual(TEXT("control: with its cooldown running Quench has no use left"),
				   Rig.System->SkillChargesHeld(ECataclysmAbilitySlot::Heavy, Pool[0].Tags), 0))
	{
		return false;
	}
	ThePlayerUses(Rig.Player, Basic.Name, Basic.Tags, ECataclysmAbilitySlot::BasicAttack, Aim);
	TestTrue(TEXT("Quench is triggered while its cooldown runs"), UCataclysmTriggeredSkill::MakePendingHeldTrigger(Rig.Player));
	TestTrue(TEXT("and its cooldown is unchanged: still running, and still no use left"),
			 Rig.System->HasMatchingGameplayTag(HeavyCooldown)
				 && Rig.System->SkillChargesHeld(ECataclysmAbilitySlot::Heavy, Pool[0].Tags) == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHeldTriggerSpellPaysTest,
	"Cataclysm.Enchantments.ATriggeredSpellPaysItsCostStartsNoCooldownAndIsRefusedWithTooLittle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHeldTriggerSpellPaysTest::RunTest(const FString&)
{
	using namespace CataclysmHeldTriggerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRepeatRig Rig(World);
	FHeldTriggerPinned Roll(TEXT("Cataclysm.TriggerHeldSkillRoll"), TEXT("0"));
	FHeldTriggerPinned Pick(TEXT("Cataclysm.TriggerHeldSkillPick"), TEXT("0"));
	FCataclysmWeaponSkill Basic;
	if (!TestTrue(TEXT("set-up: a possessed player"), Rig.IsUsable())
		|| !TestTrue(TEXT("set-up: the roll and the pick can be pinned"), Roll.Variable && Pick.Variable)
		|| !TestTrue(TEXT("set-up: a Wand and its basic attack"), HoldTheBasicAttackOf(Rig.Player, TEXT("Wand"), Basic)))
	{
		return false;
	}
	const FName BasicName(*Basic.Name);
	const FVector Aim = Rig.Player->GetActorLocation() + FVector(900.0f, 0.0f, 0.0f);

	// THE SPELLS A DEMONIC WAND HOLDS THAT MAY BE TRIGGERED: four of its five. Foul Wake is a movement skill.
	const TArray<FCataclysmWeaponSkill> Spells =
		UCataclysmTriggeredSkill::HeldSkillsToTrigger(Rig.Player, BasicName, /*bSpells=*/true);
	if (!TestEqual(TEXT("the spells a Wand's basic attack may trigger"), NamesOf(Spells),
				   FString(TEXT("Hex of Cinders, Malefice, Anathema, Whisper of Madness"))))
	{
		return false;
	}
	Pick.Set(*PickOf(Spells, TEXT("Malefice")));

	Rig.System->SetPoolActions({ATriggerRow(TEXT("Test:ten"), /*bSpell=*/true)});
	ThePlayerUses(Rig.Player, Basic.Name, Basic.Tags, ECataclysmAbilitySlot::BasicAttack, Aim);
	if (!TestTrue(TEXT("a basic attack records a trigger of a spell"),
				  Rig.System->PendingHeldTriggerUsedSkill() == BasicName && Rig.System->PendingHeldTriggerWantsASpell()))
	{
		return false;
	}

	// MADE: IT PAYS ITS COST, AND NOTHING ELSE OF A USE.
	const float ManaBefore = ManaOf(Rig.System);
	const uint32 UsesBefore = UCataclysmCombatEvents::In(World)->SkillUsesSent();
	if (!TestTrue(TEXT("the trigger is made"), UCataclysmTriggeredSkill::MakePendingHeldTrigger(Rig.Player)))
	{
		return false;
	}
	const UCataclysmSkillTemplate* Running = nullptr;
	if (!TestEqual(TEXT("one triggered Malefice is running"), RepeatsRunning(Rig.System, TEXT("Malefice"), &Running), 1)
		|| !Running)
	{
		return false;
	}
	TestTrue(TEXT("it paid mana"), ManaOf(Rig.System) < ManaBefore);
	TestEqual(TEXT("and what it paid is what Malefice costs this character"), ManaBefore - ManaOf(Rig.System),
			  Running->ManaCostFor(Rig.System), 0.01f);
	TestEqual(TEXT("it sent no skill-used notice"), UCataclysmCombatEvents::In(World)->SkillUsesSent(), UsesBefore);
	TestFalse(TEXT("it started no cooldown on the heavy slot"),
			  Rig.System->HasMatchingGameplayTag(UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Heavy)));

	// WITH TOO LITTLE TO PAY, THE TRIGGER IS REFUSED AND NOTHING ELSE HAPPENS.
	Rig.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 0.0f);
	ThePlayerUses(Rig.Player, Basic.Name, Basic.Tags, ECataclysmAbilitySlot::BasicAttack, Aim);
	TestFalse(TEXT("with no mana the trigger is refused"), UCataclysmTriggeredSkill::MakePendingHeldTrigger(Rig.Player));
	TestEqual(TEXT("and no second Malefice runs"), RepeatsRunning(Rig.System, TEXT("Malefice")), 1);
	TestEqual(TEXT("and no mana is taken"), ManaOf(Rig.System), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHeldTriggerBothRowsTest,
	"Cataclysm.Enchantments.WhenBothTriggerRowsPassOneSkillIsTriggeredAndTheSpellRowWinsWhenASpellIsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHeldTriggerBothRowsTest::RunTest(const FString&)
{
	using namespace CataclysmHeldTriggerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRepeatRig Rig(World);
	FHeldTriggerPinned Roll(TEXT("Cataclysm.TriggerHeldSkillRoll"), TEXT("0"));
	FHeldTriggerPinned Pick(TEXT("Cataclysm.TriggerHeldSkillPick"), TEXT("0"));
	FCataclysmWeaponSkill Basic;
	if (!TestTrue(TEXT("set-up: a possessed player"), Rig.IsUsable())
		|| !TestTrue(TEXT("set-up: the roll and the pick can be pinned"), Roll.Variable && Pick.Variable)
		|| !TestTrue(TEXT("set-up: a Wand and its basic attack"), HoldTheBasicAttackOf(Rig.Player, TEXT("Wand"), Basic)))
	{
		return false;
	}
	const FName BasicName(*Basic.Name);
	const FVector Aim = Rig.Player->GetActorLocation() + FVector(900.0f, 0.0f, 0.0f);
	const FCataclysmPoolAction Two = ATriggerRow(TEXT("Test:two"), /*bSpell=*/false);
	const FCataclysmPoolAction Ten = ATriggerRow(TEXT("Test:ten"), /*bSpell=*/true);

	// A WAND HOLDS SPELLS, AND THEY ARE ALSO ITS SKILLS WITH A COOLDOWN. Malefice stands at the same place in both
	// pools, so the pick below takes it whichever row is acted on, and what tells the two apart is the mana.
	const TArray<FCataclysmWeaponSkill> Spells = UCataclysmTriggeredSkill::HeldSkillsToTrigger(Rig.Player, BasicName, true);
	const TArray<FCataclysmWeaponSkill> WithACooldown =
		UCataclysmTriggeredSkill::HeldSkillsToTrigger(Rig.Player, BasicName, false);
	if (!TestTrue(TEXT("set-up: Malefice is in both pools at one place"),
				  PickOf(Spells, TEXT("Malefice")) != TEXT("-1")
					  && PickOf(Spells, TEXT("Malefice")) == PickOf(WithACooldown, TEXT("Malefice"))))
	{
		return false;
	}
	Pick.Set(*PickOf(Spells, TEXT("Malefice")));

	// BOTH ROWS PASS ON ONE USE: ONE TRIGGER, AND IT IS THE SPELL ROW'S, WHICH PAYS.
	Rig.System->SetPoolActions({Two, Ten});
	ThePlayerUses(Rig.Player, Basic.Name, Basic.Tags, ECataclysmAbilitySlot::BasicAttack, Aim);
	if (!TestTrue(TEXT("both rows are recorded for the one use"),
				  Rig.System->PendingHeldTriggerWantsASpell() && Rig.System->PendingHeldTriggerWantsACooldownSkill()))
	{
		return false;
	}
	const float ManaBefore = ManaOf(Rig.System);
	TestTrue(TEXT("a trigger is made"), UCataclysmTriggeredSkill::MakePendingHeldTrigger(Rig.Player));
	TestTrue(TEXT("it is the spell row's: it paid mana"), ManaOf(Rig.System) < ManaBefore);
	TestEqual(TEXT("one Malefice runs, not two"), RepeatsRunning(Rig.System, TEXT("Malefice")), 1);
	TestFalse(TEXT("and there is no second trigger to make"), UCataclysmTriggeredSkill::MakePendingHeldTrigger(Rig.Player));

	// A SWORD HOLDS NO SPELL. Both rows pass: the trigger is the cooldown row's, and it is free.
	if (!TestTrue(TEXT("set-up: a Sword and its basic attack"), HoldTheBasicAttackOf(Rig.Player, TEXT("Sword"), Basic)))
	{
		return false;
	}
	Pick.Set(TEXT("0"));
	Rig.System->SetPoolActions({Two, Ten});
	ThePlayerUses(Rig.Player, Basic.Name, Basic.Tags, ECataclysmAbilitySlot::BasicAttack, Aim);
	const float SwordManaBefore = ManaOf(Rig.System);
	TestTrue(TEXT("with no spell held the cooldown row's trigger is made"),
			 UCataclysmTriggeredSkill::MakePendingHeldTrigger(Rig.Player));
	TestEqual(TEXT("and it is free"), ManaOf(Rig.System), SwordManaBefore);

	// AND THE SPELL ROW ALONE DOES NOTHING FOR A CHARACTER THAT HOLDS NO SPELL.
	Rig.System->SetPoolActions({Ten});
	ThePlayerUses(Rig.Player, Basic.Name, Basic.Tags, ECataclysmAbilitySlot::BasicAttack, Aim);
	TestTrue(TEXT("the spell row is recorded"), Rig.System->PendingHeldTriggerWantsASpell());
	TestFalse(TEXT("and nothing is triggered, because no spell is held"),
			  UCataclysmTriggeredSkill::MakePendingHeldTrigger(Rig.Player));
	return true;
}

// THE AUTHORED ROWS THAT REPEAT A SKILL. Mechanism B2, issue #1833. Each test wears the real row at the top of its
// roll and hands its ability system the skill use the player character hands it (`ActOnSkillUse`), with the roll
// pinned. What a recorded repeat then does is `CataclysmSkillRepeatTest`'s, above.
namespace CataclysmRepeatRowsTest
{
	const TCHAR* const OlderAsset =
		TEXT(" If nothing, DT_EnchantmentEffects may be older than the rows: run tools/generate_datatable_assets.py");

	/** The tags of one skill, as a cell of the skill table states them. */
	FGameplayTagContainer Tagged(const TCHAR* Cell)
	{
		return UCataclysmSkillShapes::TagsFromCell(Cell);
	}

	/** Hands the wearer one use of a skill carrying these tags, aimed ahead of it. */
	void Uses(UCataclysmAbilitySystemComponent* System, const FGameplayTagContainer& Tags)
	{
		System->ActOnSkillUse(FName(TEXT("Carom")), &Tags, FVector(900.0f, 300.0f, 0.0f));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEverySkillUseRepeatRowTest,
	"Cataclysm.Enchantments.TheEverySkillUseRowRepeatsAnySkillUnderItsTopRollAtTheWholeOfItsDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Every skill use has a 5%-15% chance to cast a second time for free".
 * `repeat_skill` on `skill_use`, 5 to 15, no scope and no share, WORN at the top
 * of its roll: a roll under 15 records a repeat of the skill used, at the whole
 * of its damage, whatever the skill's tags; a roll of 15 records none.
 */
bool FCataclysmEverySkillUseRepeatRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmSkillRepeatTest;
	using namespace CataclysmRepeatRowsTest;
	FWorn Worn(TEXT("Positive_Every_skill_use_has_a_5_15_chance_to_cast_a_se"), true);
	FRepeatRollPinned Pinned(TEXT("14.9"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Pinned.Variable))
	{
		return false;
	}
	const FGameplayTagContainer Melee = Tagged(TEXT("Type.Strike, Type.Melee"));
	const FGameplayTagContainer Spell = Tagged(TEXT("Type.Spell"));
	if (!TestTrue(TEXT("set-up: the tags exist"), Melee.Num() == 2 && Spell.Num() == 1))
	{
		return false;
	}

	Uses(Worn.ASC(), Melee);
	TestEqual(*(FString(TEXT("a roll of 14.9 against the top roll of 15 repeats a melee skill.")) + OlderAsset),
		Worn.ASC()->PendingRepeatSkill(), FName(TEXT("Carom")));
	TestEqual(TEXT("at the whole of its damage"), Worn.ASC()->PendingRepeatShare(), 1.0f, 0.001f);
	Uses(Worn.ASC(), Spell);
	TestEqual(TEXT("and a spell alike"), Worn.ASC()->PendingRepeatSkill(), FName(TEXT("Carom")));

	Pinned.Set(TEXT("15"));
	Uses(Worn.ASC(), Melee);
	TestTrue(TEXT("a roll of 15 repeats nothing"), Worn.ASC()->PendingRepeatSkill().IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpellsRepeatRowTest,
	"Cataclysm.Enchantments.TheSpellsCastASecondTimeRowRepeatsASpellUnderItsTopRollAndNothingElse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Spells have a 10%-20% chance to cast a second time for free". `repeat_skill`
 * on `skill_use`, 10 to 20, scoped to `Type.Spell`, WORN at the top of its roll.
 */
bool FCataclysmSpellsRepeatRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmSkillRepeatTest;
	using namespace CataclysmRepeatRowsTest;
	FWorn Worn(TEXT("Positive_Spells_have_a_10_20_chance_to_cast_a_second_ti"), true);
	FRepeatRollPinned Pinned(TEXT("19.9"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Pinned.Variable))
	{
		return false;
	}
	const FGameplayTagContainer Melee = Tagged(TEXT("Type.Strike, Type.Melee"));
	const FGameplayTagContainer Spell = Tagged(TEXT("Type.Spell, Type.Projectile"));
	if (!TestTrue(TEXT("set-up: the tags exist"), Melee.Num() == 2 && Spell.Num() == 2))
	{
		return false;
	}

	Uses(Worn.ASC(), Spell);
	TestEqual(*(FString(TEXT("a roll of 19.9 against the top roll of 20 repeats a spell.")) + OlderAsset),
		Worn.ASC()->PendingRepeatSkill(), FName(TEXT("Carom")));
	TestEqual(TEXT("at the whole of its damage"), Worn.ASC()->PendingRepeatShare(), 1.0f, 0.001f);
	Uses(Worn.ASC(), Melee);
	TestTrue(TEXT("a melee skill is not a spell, and is not repeated"), Worn.ASC()->PendingRepeatSkill().IsNone());
	Pinned.Set(TEXT("20"));
	Uses(Worn.ASC(), Spell);
	TestTrue(TEXT("a roll of 20 repeats nothing"), Worn.ASC()->PendingRepeatSkill().IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDuplicateAtHalfRowTest,
	"Cataclysm.Enchantments.TheDuplicateRowRepeatsAnySkillUnderItsTopRollAtHalfItsDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Each skill has a 20%-40% chance to cast a duplicate at 50% damage".
 * `repeat_skill` on `skill_use`, 20 to 40, with a Damage Share of 50, WORN at the
 * top of its roll. The share is the column this row needed.
 */
bool FCataclysmDuplicateAtHalfRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmSkillRepeatTest;
	using namespace CataclysmRepeatRowsTest;
	FWorn Worn(TEXT("Positive_Each_skill_has_a_20_40_chance_to_cast_a_duplic"), true);
	FRepeatRollPinned Pinned(TEXT("39.9"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Pinned.Variable))
	{
		return false;
	}
	const FGameplayTagContainer Melee = Tagged(TEXT("Type.Strike, Type.Melee"));
	if (!TestTrue(TEXT("set-up: the tags exist"), Melee.Num() == 2))
	{
		return false;
	}

	Uses(Worn.ASC(), Melee);
	TestEqual(*(FString(TEXT("a roll of 39.9 against the top roll of 40 repeats the skill.")) + OlderAsset),
		Worn.ASC()->PendingRepeatSkill(), FName(TEXT("Carom")));
	TestEqual(TEXT("at half its damage"), Worn.ASC()->PendingRepeatShare(), 0.5f, 0.001f);
	Pinned.Set(TEXT("40"));
	Uses(Worn.ASC(), Melee);
	TestTrue(TEXT("a roll of 40 repeats nothing"), Worn.ASC()->PendingRepeatSkill().IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHeavyTwiceRowTest,
	"Cataclysm.Enchantments.TheHeavyAttackTwiceRowRepeatsAHeavyAttackOnlyAfterTwoSecondsStandingStill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your heavy attack applies its full effect twice if you have not moved in the
 * last 2 seconds". `repeat_skill` on `skill_use`, always, scoped to `Slot.Heavy`
 * under `stationary_for_seconds` 2. THE ROLL IS PINNED AT 100, the one roll a
 * chance of 100 used to lose: "always" is compared and not rolled.
 */
bool FCataclysmHeavyTwiceRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmSkillRepeatTest;
	using namespace CataclysmRepeatRowsTest;
	FWorn Worn(TEXT("Positive_Your_heavy_attack_applies_its_full_effect_twice"), true);
	FRepeatRollPinned Pinned(TEXT("100"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Pinned.Variable))
	{
		return false;
	}
	const FGameplayTagContainer Heavy = Tagged(TEXT("Slot.Heavy, Type.Strike, Type.Melee"));
	const FGameplayTagContainer Special = Tagged(TEXT("Slot.Special, Type.Strike, Type.Melee"));
	if (!TestTrue(TEXT("set-up: the tags exist"), Heavy.Num() == 3 && Special.Num() == 3))
	{
		return false;
	}

	// STANDING STILL HAS TO BE STARTED: the first sample is when it begins.
	Worn.ASC()->NoteDidNotMove();
	CataclysmTestWorld::RunClock(Worn.World, 1.0f);
	Uses(Worn.ASC(), Heavy);
	TestTrue(TEXT("one second standing still is not two: nothing is repeated"),
		Worn.ASC()->PendingRepeatSkill().IsNone());

	CataclysmTestWorld::RunClock(Worn.World, 1.5f);
	Uses(Worn.ASC(), Heavy);
	TestEqual(*(FString(TEXT("two and a half seconds standing still: the heavy attack is repeated, at a roll of 100.")) + OlderAsset),
		Worn.ASC()->PendingRepeatSkill(), FName(TEXT("Carom")));
	TestEqual(TEXT("at the whole of its damage"), Worn.ASC()->PendingRepeatShare(), 1.0f, 0.001f);
	Uses(Worn.ASC(), Special);
	TestTrue(TEXT("a special skill is not the heavy attack, and is not repeated"),
		Worn.ASC()->PendingRepeatSkill().IsNone());

	Worn.ASC()->NoteMovedMetres(1.0f);
	Uses(Worn.ASC(), Heavy);
	TestTrue(TEXT("a step taken: the heavy attack is not repeated"), Worn.ASC()->PendingRepeatSkill().IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpellsEchoRowTest,
	"Cataclysm.Enchantments.TheSpellsEchoRowRepeatsEverySpellAndNothingElse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your spells echo +1 time". `repeat_skill` on `skill_use`, always, scoped to
 * `Type.Spell`. The roll is pinned at 100, as for the heavy attack row.
 */
bool FCataclysmSpellsEchoRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmSkillRepeatTest;
	using namespace CataclysmRepeatRowsTest;
	FWorn Worn(TEXT("Positive_Your_spells_echo_1_time"), true);
	FRepeatRollPinned Pinned(TEXT("100"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Pinned.Variable))
	{
		return false;
	}
	const FGameplayTagContainer Melee = Tagged(TEXT("Type.Strike, Type.Melee"));
	const FGameplayTagContainer Spell = Tagged(TEXT("Type.Spell, Type.Projectile"));
	if (!TestTrue(TEXT("set-up: the tags exist"), Melee.Num() == 2 && Spell.Num() == 2))
	{
		return false;
	}

	Uses(Worn.ASC(), Spell);
	TestEqual(*(FString(TEXT("a spell is repeated, at a roll of 100.")) + OlderAsset),
		Worn.ASC()->PendingRepeatSkill(), FName(TEXT("Carom")));
	TestEqual(TEXT("at the whole of its damage"), Worn.ASC()->PendingRepeatShare(), 1.0f, 0.001f);
	Uses(Worn.ASC(), Melee);
	TestTrue(TEXT("a melee skill is not a spell, and is not repeated"), Worn.ASC()->PendingRepeatSkill().IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUltimateTwiceRowTest,
	"Cataclysm.Enchantments.TheUltimateTwiceRowRepeatsEveryUltimateAndNothingElse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your ultimate ability applies its effect twice". `repeat_skill` on
 * `skill_use`, always, scoped to `Slot.Ultimate`. The roll is pinned at 100, as
 * for the heavy attack row.
 */
bool FCataclysmUltimateTwiceRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmSkillRepeatTest;
	using namespace CataclysmRepeatRowsTest;
	FWorn Worn(TEXT("Positive_Your_ultimate_ability_applies_its_effect_twice"), true);
	FRepeatRollPinned Pinned(TEXT("100"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Pinned.Variable))
	{
		return false;
	}
	const FGameplayTagContainer Ultimate = Tagged(TEXT("Slot.Ultimate, Type.AOE.PointBlank"));
	const FGameplayTagContainer Heavy = Tagged(TEXT("Slot.Heavy, Type.Strike, Type.Melee"));
	if (!TestTrue(TEXT("set-up: the tags exist"), Ultimate.Num() == 2 && Heavy.Num() == 3))
	{
		return false;
	}

	Uses(Worn.ASC(), Ultimate);
	TestEqual(*(FString(TEXT("an ultimate is repeated, at a roll of 100.")) + OlderAsset),
		Worn.ASC()->PendingRepeatSkill(), FName(TEXT("Carom")));
	TestEqual(TEXT("at the whole of its damage"), Worn.ASC()->PendingRepeatShare(), 1.0f, 0.001f);
	Uses(Worn.ASC(), Heavy);
	TestTrue(TEXT("a heavy attack is not an ultimate, and is not repeated"),
		Worn.ASC()->PendingRepeatSkill().IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMeleeAttacksTwiceRowTest,
	"Cataclysm.Enchantments.TheMeleeAttacksTwiceRowRepeatsAMeleeBasicAttackAndAMeleeSkillUnderItsTopRoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your melee attacks have a 12%-15% chance to trigger twice". `repeat_skill`
 * on `attack_use`, the event for every paid use with the basic attack included,
 * 12 to 15, scoped to `Type.Melee`, WORN at the top of its roll. The basic
 * attack is handed over as the player character hands it, with `bBasicAttack`,
 * which raises `attack_use` alone.
 */
bool FCataclysmMeleeAttacksTwiceRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmSkillRepeatTest;
	using namespace CataclysmRepeatRowsTest;
	FWorn Worn(TEXT("Positive_Your_melee_attacks_have_a_12_15_chance_to_trig"), true);
	FRepeatRollPinned Pinned(TEXT("14.9"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Pinned.Variable))
	{
		return false;
	}
	const FGameplayTagContainer Melee = Tagged(TEXT("Type.Strike, Type.Melee"));
	const FGameplayTagContainer Ranged = Tagged(TEXT("Type.Projectile, Type.Ranged"));
	if (!TestTrue(TEXT("set-up: the tags exist"), Melee.Num() == 2 && Ranged.Num() == 2))
	{
		return false;
	}
	const FVector Aim(900.0f, 300.0f, 0.0f);
	const FName Swing(TEXT("Carom"));

	Worn.ASC()->ActOnSkillUse(Swing, &Melee, Aim, /*bBasicAttack=*/true);
	TestEqual(*(FString(TEXT("a roll of 14.9 against the top roll of 15 repeats a melee basic attack.")) + OlderAsset),
		Worn.ASC()->PendingRepeatSkill(), Swing);
	TestEqual(TEXT("at the whole of its damage"), Worn.ASC()->PendingRepeatShare(), 1.0f, 0.001f);
	Worn.ASC()->ActOnSkillUse(Swing, &Melee, Aim);
	TestEqual(TEXT("and a melee skill that is not the basic attack"), Worn.ASC()->PendingRepeatSkill(), Swing);

	Worn.ASC()->ActOnSkillUse(Swing, &Ranged, Aim, /*bBasicAttack=*/true);
	TestTrue(TEXT("a ranged basic attack is not a melee attack, and is not repeated"),
		Worn.ASC()->PendingRepeatSkill().IsNone());
	Pinned.Set(TEXT("15"));
	Worn.ASC()->ActOnSkillUse(Swing, &Melee, Aim, /*bBasicAttack=*/true);
	TestTrue(TEXT("a roll of 15 repeats nothing"), Worn.ASC()->PendingRepeatSkill().IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpellbladesWillTwoRowTest,
	"Cataclysm.Enchantments.SpellbladesWillTwoPiecesTriggerAHeldSkillOnAMeleeAttackAndHalveMeleeDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Spellblade's Will (2-Piece Bonus): Your melee attacks have a 25% chance to
 * trigger an ability with a cooldown", with the set's drawback, "Melee damage is
 * reduced by 50%". Issue #1833, ruled 2026-10-06. The bonus is
 * `trigger_held_skill` on `attack_use`, 25, scoped to `Type.Melee`; the
 * drawback is `attack_damage` at -50 scoped to `Type.Melee`. A set is written
 * whole, and both turn on at two pieces: one piece does neither.
 *
 * THE BONUS IS READ AT THE RECORDED TRIGGER. Which held skill is then picked and
 * made is `CataclysmHeldTriggerTest`'s, above, with rows made by hand.
 */
bool FCataclysmSpellbladesWillTwoRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmHealthThresholdRowTest;
	const TCHAR* Bonus = TEXT("Positive_Spellblade_s_Will_2_Piece_Bonus_Your_melee_at");
	const TCHAR* Drawback = TEXT("Negative_Melee_damage_is_reduced_by_50");
	const FGameplayTagContainer Melee = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Strike, Type.Melee"));
	const FGameplayTagContainer Ranged = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Projectile, Type.Ranged"));
	if (!TestTrue(TEXT("set-up: the tags exist"), Melee.Num() == 2 && Ranged.Num() == 2))
	{
		return false;
	}
	const FVector Aim(900.0f, 300.0f, 0.0f);
	const FName Swing(TEXT("Quench"));
	const FName Damage(TEXT("attack_damage"));

	for (const int32 Pieces : {1, 2})
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		FWearer Wearer(World);
		for (int32 Index = 0; Index < Pieces; ++Index)
		{
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
			Wearer.Equipment->Equip(Carrying(TenBases[Index], Bonus, Drawback), Removed, AlsoRemoved, Slot);
		}
		Wearer.Equipment->RefreshAttributes(Wearer.AbilitySystem);
		UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;
		CataclysmHeldTriggerTest::FHeldTriggerPinned Roll(TEXT("Cataclysm.TriggerHeldSkillRoll"), TEXT("24.9"));
		if (!TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
		{
			return false;
		}

		// THE BONUS: a melee basic attack, as the player character hands it over.
		ASC->ActOnSkillUse(Swing, &Melee, Aim, /*bBasicAttack=*/true);
		TestTrue(FString::Printf(TEXT("%d pieces, a roll of 24.9 against 25, a melee basic attack: a trigger of a "
									  "skill with a cooldown is recorded only at two.%s"), Pieces, CataclysmRepeatRowsTest::OlderAsset),
			ASC->PendingHeldTriggerWantsACooldownSkill() == (Pieces == 2));
		TestTrue(FString::Printf(TEXT("%d pieces: and it records the attack that was used only at two"), Pieces),
			ASC->PendingHeldTriggerUsedSkill() == (Pieces == 2 ? Swing : FName()));
		TestFalse(FString::Printf(TEXT("%d pieces: it is not a spell trigger"), Pieces),
			ASC->PendingHeldTriggerWantsASpell());
		ASC->ActOnSkillUse(Swing, &Ranged, Aim, /*bBasicAttack=*/true);
		TestFalse(FString::Printf(TEXT("%d pieces: a ranged basic attack records nothing"), Pieces),
			ASC->PendingHeldTriggerWantsACooldownSkill());
		Roll.Set(TEXT("25"));
		ASC->ActOnSkillUse(Swing, &Melee, Aim, /*bBasicAttack=*/true);
		TestFalse(FString::Printf(TEXT("%d pieces: a roll of 25 records nothing"), Pieces),
			ASC->PendingHeldTriggerWantsACooldownSkill());

		// THE DRAWBACK: half of what a melee skill's attack damage is, and all of a ranged skill's.
		// `StatAppliedTo`, AND NOT `StatForSkill`: the 100 handed in has to BE the base. `StatForSkill`'s third
		// argument is only a fallback for a stat nothing is recorded for, and this bare wearer's recorded base is 0.
		TestEqual(FString::Printf(TEXT("%d pieces: a melee skill's attack damage of 100 is halved only at two.%s"),
					  Pieces, CataclysmRepeatRowsTest::OlderAsset),
			ASC->StatAppliedTo(Damage, Melee, 100.0f), Pieces == 2 ? 50.0f : 100.0f, 0.01f);
		TestEqual(FString::Printf(TEXT("%d pieces: a ranged skill's attack damage of 100 is left alone"), Pieces),
			ASC->StatAppliedTo(Damage, Ranged, 100.0f), 100.0f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReapersEmbraceTwoRowTest,
	"Cataclysm.Enchantments.ReapersEmbraceTwoPiecesRaiseEveryHealByATenthAndHalveHealthRegeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Reaper's Embrace (2-Piece Bonus): You gain 10% more life from all sources",
 * with the set's drawback, "All of your life regeneration effects are reduced by
 * 50%". Issue #1833, ruled 2026-10-06: the bonus is read as healing, by the
 * drawback beside it, and is `healing_received` more 10; the drawback is
 * `health_regen` increased -50. A set is written whole, and both turn on at two
 * pieces: one piece does neither.
 *
 * THE BONUS IS MEASURED ON A HEAL: `UCataclysmRegeneration::TopUp`, the function
 * regeneration, leech, potions and worn rows all restore health through. THE
 * DRAWBACK IS MEASURED ON A FIGURE HANDED IN, through `StatAppliedTo`, so the
 * 100 is the base and not a fallback.
 */
bool FCataclysmReapersEmbraceTwoRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmHealthThresholdRowTest;
	const TCHAR* Bonus = TEXT("Positive_Reaper_s_Embrace_2_Piece_Bonus_You_gain_10_m");
	const TCHAR* Drawback = TEXT("Negative_All_of_your_life_regeneration_effects_are_reduce");
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();

	for (const int32 Pieces : {1, 2})
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		FWearer Wearer(World);
		for (int32 Index = 0; Index < Pieces; ++Index)
		{
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
			Wearer.Equipment->Equip(Carrying(TenBases[Index], Bonus, Drawback), Removed, AlsoRemoved, Slot);
		}
		Wearer.Equipment->RefreshAttributes(Wearer.AbilitySystem);
		UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;

		// THE BONUS: 100 of 1,000 health, and a heal of 100.
		SetHealth(ASC, 1000.0f, 100.0f);
		UCataclysmRegeneration::TopUp(*ASC, Health, MaxHealth, 100.0f);
		TestEqual(FString::Printf(TEXT("%d pieces: a heal of 100 restores 110 only at two.%s"),
					  Pieces, CataclysmRepeatRowsTest::OlderAsset),
			HealthOf(ASC) - 100.0f, Pieces == 2 ? 110.0f : 100.0f, 0.01f);

		// THE DRAWBACK: half of a regeneration of 100.
		TestEqual(FString::Printf(TEXT("%d pieces: a health regeneration of 100 is 50 only at two.%s"),
					  Pieces, CataclysmRepeatRowsTest::OlderAsset),
			ASC->StatAppliedTo(FName(UCataclysmRegeneration::HealthRegenStat), FGameplayTagContainer(), 100.0f),
			Pieces == 2 ? 50.0f : 100.0f, 0.01f);
	}
	return true;
}

// THE AUTHORED ROWS THAT ROLL FOR A USE TO DEAL NO DAMAGE. Issue #1833, on the actions of the entry "A row can
// roll, once for a use". Each test wears the real drawback at the top of its roll, which for a drawback is its
// harshest figure, and hands its ability system a use as the player character hands it (`ActOnSkillUse`), with
// `Cataclysm.UseOutcomeRoll` pinned. Each stops at the answer the rows recorded; what a use then does with it is
// that entry's tests', with rows made by hand.
namespace CataclysmUseOutcomeRowsTest
{
	/** One use of a skill carrying these tags, handed to the wearer's rows. */
	void Use(UCataclysmAbilitySystemComponent* System, const FGameplayTagContainer& Tags, bool bBasicAttack,
			 bool bHasCooldown)
	{
		System->ActOnSkillUse(FName(TEXT("Carom")), &Tags, FVector(900.0f, 300.0f, 0.0f), bBasicAttack, bHasCooldown);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCooldownAbilitiesNoDamageRowTest,
	"Cataclysm.Enchantments.TheCooldownAbilitiesNoDamageRowRollsOnlyForASkillWithACooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your cooldown abilities have a 25% chance to deal no damage".
 * `cooldown_use_no_damage` on `skill_use`, 25, no scope.
 */
bool FCataclysmCooldownAbilitiesNoDamageRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmUseOutcomeRowsTest;
	FWorn Worn(TEXT("Negative_Your_cooldown_abilities_have_a_25_chance_to_dea"), false);
	CataclysmHeldTriggerTest::FHeldTriggerPinned Roll(TEXT("Cataclysm.UseOutcomeRoll"), TEXT("24.9"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
	{
		return false;
	}
	const FGameplayTagContainer Melee = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Strike, Type.Melee"));
	if (!TestTrue(TEXT("set-up: the tags exist"), Melee.Num() == 2))
	{
		return false;
	}

	Use(Worn.ASC(), Melee, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestTrue(*(FString(TEXT("a roll of 24.9 against 25, a skill with a cooldown: the use deals no damage.")) +
			   CataclysmRepeatRowsTest::OlderAsset),
		Worn.ASC()->PendingUseNoDamage());
	Use(Worn.ASC(), Melee, /*bBasicAttack=*/false, /*bHasCooldown=*/false);
	TestFalse(TEXT("a skill with no cooldown is not rolled for"), Worn.ASC()->PendingUseNoDamage());
	Roll.Set(TEXT("25"));
	Use(Worn.ASC(), Melee, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestFalse(TEXT("a roll of 25 leaves the use its damage"), Worn.ASC()->PendingUseNoDamage());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStrikeSkillsMissRowTest,
	"Cataclysm.Enchantments.TheStrikeSkillsMissRowRollsForAStrikeSkillAndNotForASpellOrABasicAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Strike skills have a 10%-20% chance to miss entirely regardless of other
 * stats". `use_no_damage` on `skill_use`, 10 to 20, scoped to `Type.Strike`.
 * "Skills" is `skill_use`, so the basic attack does not roll.
 */
bool FCataclysmStrikeSkillsMissRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmUseOutcomeRowsTest;
	FWorn Worn(TEXT("Negative_Strike_skills_have_a_10_20_chance_to_miss_enti"), false);
	CataclysmHeldTriggerTest::FHeldTriggerPinned Roll(TEXT("Cataclysm.UseOutcomeRoll"), TEXT("19.9"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
	{
		return false;
	}
	const FGameplayTagContainer Strike = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Strike, Type.Melee"));
	const FGameplayTagContainer Spell = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Spell, Type.Projectile"));
	if (!TestTrue(TEXT("set-up: the tags exist"), Strike.Num() == 2 && Spell.Num() == 2))
	{
		return false;
	}

	Use(Worn.ASC(), Strike, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestTrue(*(FString(TEXT("a roll of 19.9 against the harshest roll of 20, a strike skill: it misses.")) +
			   CataclysmRepeatRowsTest::OlderAsset),
		Worn.ASC()->PendingUseNoDamage());
	Use(Worn.ASC(), Spell, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestFalse(TEXT("a spell is not a strike skill"), Worn.ASC()->PendingUseNoDamage());
	Use(Worn.ASC(), Strike, /*bBasicAttack=*/true, /*bHasCooldown=*/false);
	TestFalse(TEXT("a basic attack is not a skill, and does not roll"), Worn.ASC()->PendingUseNoDamage());
	Roll.Set(TEXT("20"));
	Use(Worn.ASC(), Strike, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestFalse(TEXT("a roll of 20 does not miss"), Worn.ASC()->PendingUseNoDamage());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectilesExplodeRowTest,
	"Cataclysm.Enchantments.TheProjectilesExplodePrematurelyRowRollsForEveryProjectileUseTheBasicAttackIncluded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Projectiles have a 20%-35% chance to explode prematurely dealing no damage".
 * `use_no_damage` on `attack_use`, 20 to 35, scoped to `Type.Projectile`. The
 * sentence names no skill, so it is every paid use and a projectile basic attack
 * rolls.
 */
bool FCataclysmProjectilesExplodeRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmUseOutcomeRowsTest;
	FWorn Worn(TEXT("Negative_Projectiles_have_a_20_35_chance_to_explode_pre"), false);
	CataclysmHeldTriggerTest::FHeldTriggerPinned Roll(TEXT("Cataclysm.UseOutcomeRoll"), TEXT("34.9"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
	{
		return false;
	}
	const FGameplayTagContainer Strike = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Strike, Type.Melee"));
	const FGameplayTagContainer Thrown = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Projectile, Type.Ranged"));
	if (!TestTrue(TEXT("set-up: the tags exist"), Strike.Num() == 2 && Thrown.Num() == 2))
	{
		return false;
	}

	Use(Worn.ASC(), Thrown, /*bBasicAttack=*/true, /*bHasCooldown=*/false);
	TestTrue(*(FString(TEXT("a roll of 34.9 against the harshest roll of 35, a projectile basic attack: no damage.")) +
			   CataclysmRepeatRowsTest::OlderAsset),
		Worn.ASC()->PendingUseNoDamage());
	Use(Worn.ASC(), Thrown, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestTrue(TEXT("and a projectile skill alike"), Worn.ASC()->PendingUseNoDamage());
	Use(Worn.ASC(), Strike, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestFalse(TEXT("a strike is not a projectile"), Worn.ASC()->PendingUseNoDamage());
	Roll.Set(TEXT("35"));
	Use(Worn.ASC(), Thrown, /*bBasicAttack=*/true, /*bHasCooldown=*/false);
	TestFalse(TEXT("a roll of 35 leaves the projectile its damage"), Worn.ASC()->PendingUseNoDamage());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpellsAbsorbedRowTest,
	"Cataclysm.Enchantments.TheSpellsAbsorbedRowAbsorbsASpellUnderItsTopRollAndNeverAMeleeBlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Spells that hit you have a 15%-30% chance to be absorbed dealing no damage".
 * `spell_absorb_chance` flat 15 to 30, WORN at the top of its roll and asked the
 * question the damage step asks, `UCataclysmDamageCalculation::SpellIsAbsorbed`,
 * with `Cataclysm.SpellAbsorbRoll` pinned. What an absorbed blow then does is
 * `Cataclysm.StatExemption.ASpellIsAbsorbedOnItsRollAndABlowThatIsNotASpellNever`'s.
 */
bool FCataclysmSpellsAbsorbedRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	FWorn Worn(TEXT("Positive_Spells_that_hit_you_have_a_15_30_chance_to_be"), true);
	CataclysmHeldTriggerTest::FHeldTriggerPinned Roll(TEXT("Cataclysm.SpellAbsorbRoll"), TEXT("29.9"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
	{
		return false;
	}
	FCataclysmIncomingHit Spell;
	Spell.Damage = 100.0f;
	Spell.bIsSpell = true;
	FCataclysmIncomingHit Melee;
	Melee.Damage = 100.0f;
	Melee.bIsMelee = true;

	TestTrue(*(FString(TEXT("a roll of 29.9 against the top roll of 30: a spell is absorbed.")) +
			   CataclysmRepeatRowsTest::OlderAsset),
		UCataclysmDamageCalculation::SpellIsAbsorbed(Worn.ASC(), Spell));
	TestFalse(TEXT("a melee blow is never absorbed"),
		UCataclysmDamageCalculation::SpellIsAbsorbed(Worn.ASC(), Melee));
	Roll.Set(TEXT("30"));
	TestFalse(TEXT("a roll of 30 absorbs nothing"),
		UCataclysmDamageCalculation::SpellIsAbsorbed(Worn.ASC(), Spell));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMeleeReflectedRowTest,
	"Cataclysm.Enchantments.TheMeleeAttacksReflectedRowReflectsAMeleeHitUnderItsTopRollAndNeverASpell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Melee attacks that hit you have a 10%-20% chance to be reflected back as
 * retaliation damage". `melee_reflect_chance` flat 10 to 20, WORN at the top of
 * its roll and asked `UCataclysmDamageCalculation::MeleeIsReflected`, with
 * `Cataclysm.MeleeReflectRoll` pinned. What a reflected hit pays back is
 * `Cataclysm.StatExemption.AReflectedMeleeHitIsNotTakenAndIsPaidBackWholeAsRetaliationIsPriced`'s.
 */
bool FCataclysmMeleeReflectedRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	FWorn Worn(TEXT("Positive_Melee_attacks_that_hit_you_have_a_10_20_chance"), true);
	CataclysmHeldTriggerTest::FHeldTriggerPinned Roll(TEXT("Cataclysm.MeleeReflectRoll"), TEXT("19.9"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
	{
		return false;
	}
	FCataclysmIncomingHit Spell;
	Spell.Damage = 100.0f;
	Spell.bIsSpell = true;
	FCataclysmIncomingHit Melee;
	Melee.Damage = 100.0f;
	Melee.bIsMelee = true;

	TestTrue(*(FString(TEXT("a roll of 19.9 against the top roll of 20: a melee hit is reflected.")) +
			   CataclysmRepeatRowsTest::OlderAsset),
		UCataclysmDamageCalculation::MeleeIsReflected(Worn.ASC(), Melee));
	TestFalse(TEXT("a spell is never reflected"),
		UCataclysmDamageCalculation::MeleeIsReflected(Worn.ASC(), Spell));
	Roll.Set(TEXT("20"));
	TestFalse(TEXT("a roll of 20 reflects nothing"),
		UCataclysmDamageCalculation::MeleeIsReflected(Worn.ASC(), Melee));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMeleeSkillsHitAllRowTest,
	"Cataclysm.Enchantments.TheMeleeSkillsHitAllRowRollsThreeMetresForAMeleeSkillAndNotForABasicAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Melee skills have a 10%-20% chance to hit all enemies within 3 meters".
 * `use_hits_all_nearby` on `skill_use`, 10 to 20, scoped to `Type.Melee`, WORN
 * at the top of its roll with `Cataclysm.UseOutcomeRoll` pinned. What a strike
 * does with the 300 centimetres is
 * `Cataclysm.Skills.AStrikeARowRolledToHitAllNearbyHitsEveryEnemyAroundItsUserAndTheNextStrikeDoesNot`'s.
 */
bool FCataclysmMeleeSkillsHitAllRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmUseOutcomeRowsTest;
	FWorn Worn(TEXT("Positive_Melee_skills_have_a_10_20_chance_to_hit_all_en"), true);
	CataclysmHeldTriggerTest::FHeldTriggerPinned Roll(TEXT("Cataclysm.UseOutcomeRoll"), TEXT("19.9"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
	{
		return false;
	}
	const FGameplayTagContainer Melee = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Strike, Type.Melee"));
	const FGameplayTagContainer Spell = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Spell, Type.Projectile"));
	if (!TestTrue(TEXT("set-up: the tags exist"), Melee.Num() == 2 && Spell.Num() == 2))
	{
		return false;
	}

	Use(Worn.ASC(), Melee, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestEqual(*(FString(TEXT("a roll of 19.9 against the top roll of 20, a melee skill: every enemy within 3 metres.")) +
				CataclysmRepeatRowsTest::OlderAsset),
		Worn.ASC()->PendingUseHitsAll(), 300.0f, 0.01f);
	Use(Worn.ASC(), Spell, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestEqual(TEXT("a spell is not a melee skill"), Worn.ASC()->PendingUseHitsAll(), 0.0f, 0.01f);
	Use(Worn.ASC(), Melee, /*bBasicAttack=*/true, /*bHasCooldown=*/false);
	TestEqual(TEXT("a melee basic attack is not a skill, and does not roll"),
		Worn.ASC()->PendingUseHitsAll(), 0.0f, 0.01f);
	Roll.Set(TEXT("20"));
	Use(Worn.ASC(), Melee, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestEqual(TEXT("a roll of 20 hits only what the strike reaches"), Worn.ASC()->PendingUseHitsAll(), 0.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpellbladesWillTenRowTest,
	"Cataclysm.Enchantments.SpellbladesWillTenPiecesRollToTriggerAHeldSpellOnAMeleeAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Spellblade's Will (10-Piece Bonus): Your melee attacks have a 25% of
 * triggering one of your spells. This does not put the spell on cooldown but
 * does use it's mana cost". `trigger_held_spell` on `attack_use`, 25, scoped to
 * `Type.Melee`. THE OWNER'S ANSWER OF 2026-10-06: "your spells" are the spells
 * the character is running, and with none it does nothing; so the row is
 * written and a character holding no spell gets nothing from it.
 *
 * NINE PIECES AND THEN TEN, each carrying the set's real first bonus and real
 * drawback. At nine the two-piece row rolls and the ten-piece row does not; at
 * ten both do. READ AT THE RECORDED TRIGGER, as the two-piece test is: which
 * spell is picked, that it pays and that a character with no spell gets nothing
 * are `CataclysmHeldTriggerTest`'s, with rows made by hand.
 */
bool FCataclysmSpellbladesWillTenRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmHealthThresholdRowTest;
	const TCHAR* Bonus = TEXT("Positive_Spellblade_s_Will_2_Piece_Bonus_Your_melee_at");
	const TCHAR* Drawback = TEXT("Negative_Melee_damage_is_reduced_by_50");
	const FGameplayTagContainer Melee = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Strike, Type.Melee"));
	const FGameplayTagContainer Ranged = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Projectile, Type.Ranged"));
	if (!TestTrue(TEXT("set-up: the tags exist"), Melee.Num() == 2 && Ranged.Num() == 2))
	{
		return false;
	}
	const FVector Aim(900.0f, 300.0f, 0.0f);
	const FName Swing(TEXT("Quench"));

	for (const int32 Pieces : {9, 10})
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		FWearer Wearer(World);
		for (int32 Index = 0; Index < Pieces; ++Index)
		{
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
			Wearer.Equipment->Equip(Carrying(TenBases[Index], Bonus, Drawback), Removed, AlsoRemoved, Slot);
		}
		Wearer.Equipment->RefreshAttributes(Wearer.AbilitySystem);
		UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;
		CataclysmHeldTriggerTest::FHeldTriggerPinned Roll(TEXT("Cataclysm.TriggerHeldSkillRoll"), TEXT("24.9"));
		if (!TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
		{
			return false;
		}

		ASC->ActOnSkillUse(Swing, &Melee, Aim, /*bBasicAttack=*/true);
		TestTrue(FString::Printf(TEXT("%d pieces: the two-piece row records a trigger of a skill with a cooldown"),
					 Pieces),
			ASC->PendingHeldTriggerWantsACooldownSkill());
		TestTrue(FString::Printf(TEXT("%d pieces, a roll of 24.9 against 25, a melee basic attack: a trigger of a "
									  "spell is recorded only at ten.%s"), Pieces, CataclysmRepeatRowsTest::OlderAsset),
			ASC->PendingHeldTriggerWantsASpell() == (Pieces == 10));
		ASC->ActOnSkillUse(Swing, &Ranged, Aim, /*bBasicAttack=*/true);
		TestFalse(FString::Printf(TEXT("%d pieces: a ranged basic attack records no spell trigger"), Pieces),
			ASC->PendingHeldTriggerWantsASpell());
		Roll.Set(TEXT("25"));
		ASC->ActOnSkillUse(Swing, &Melee, Aim, /*bBasicAttack=*/true);
		TestFalse(FString::Printf(TEXT("%d pieces: a roll of 25 records no spell trigger"), Pieces),
			ASC->PendingHeldTriggerWantsASpell());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemiesWithinFiveMetresResistRowTest,
	"Cataclysm.Enchantments.TheEnemiesWithinFiveMetresGainResistancesRowTakesTheWearersPenetrationOnlyThatNear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Enemies within 5 metres gain 20%-40% resistances", which read "Nearby
 * enemies gain 20%-40% resistances" until the owner approved the reword on
 * 2026-10-06. Ruled 2026-10-05: read on the wearer's side, as less penetration
 * against a character within 5 metres; `penetration` flat -20 to -40 under
 * `target_within_metres` 5, WORN at the top of its roll, a drawback's harshest.
 *
 * ASKED AS THE DAMAGE STEP ASKS IT, with the distance to the character struck,
 * AND READ AS A DIFFERENCE between a target 3 metres away and one 8 metres
 * away, so the wearer's own penetration cancels and a wearer with no row reads
 * no difference at all.
 */
bool FCataclysmEnemiesWithinFiveMetresResistRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	FWorn Worn(TEXT("Negative_Enemies_within_5_metres_gain_20_40_resistances"), false);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const FName Stat(TEXT("penetration"));
	const auto At = [&Worn, &Stat](float Metres)
	{
		return Worn.ASC()->StatForSkill(Stat, FGameplayTagContainer(), 0.0f, /*SkillHealthCostPercent=*/-1.0f,
										FCataclysmBlowContext(), /*MetresMovedBeforeBlow=*/-1.0f, Metres);
	};

	const float Far = At(8.0f);
	TestEqual(*(FString(TEXT("against a target 3 metres away the wearer has 40 less penetration than against one 8 "
							 "metres away.")) + CataclysmRepeatRowsTest::OlderAsset),
		At(3.0f) - Far, -40.0f, 0.01f);
	TestEqual(TEXT("and with no distance known, none is taken"), At(-1.0f) - Far, 0.0f, 0.01f);

	// WHAT THAT IS WORTH TO THE ENEMY: its resistance is that much higher, up to its cap.
	TestEqual(TEXT("an enemy with 20 resistance, 40 penetration taken away: it stands at 60"),
		UCataclysmDamageCalculation::EffectiveResistanceUnderCap(20.0f, -40.0f, 70.0f), 60.0f, 0.01f);
	TestEqual(TEXT("and one with 50 stops at its cap of 70"),
		UCataclysmDamageCalculation::EffectiveResistanceUnderCap(50.0f, -40.0f, 70.0f), 70.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFourZoneRowsTest,
	"Cataclysm.Enchantments.TheFourPersistentAreaRowsEachGiveTheStatAZoneReadsWhenItIsLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The four enchantment rows on the stats a persistent area reads where a skill
 * leaves it, each WORN at the top of its roll (a drawback's harshest) and asked
 * as `UCataclysmSkillTemplate` asks it, through `StatForSkill` with the base the
 * engine supplies as the fallback:
 *
 *   "Persistent AOE effects expire 40%-60% faster"              `persistent_area_duration` 100 becomes 40
 *   "Persistent AOE zones deal 10%-20% increased damage for
 *    each enemy standing in them"                               `zone_damage_per_enemy_inside` is 20
 *   "Your persistent AOE zones also slow enemies within them
 *    by 20%-35%"                                                `zone_slow_percent` is 35
 *   "You can only have 1 persistent AOE effect active at a time"  `only_one_persistent_area` is above nought
 *
 * EACH IS READ AGAINST A WEARER OF NOTHING IN THE SAME TEST, so a stat that came
 * back as its fallback either way would show as no difference. What a zone then
 * does with each figure is covered by the four probes of the entry that built
 * the stats, in `Cataclysm.StatExemption.`.
 */
bool FCataclysmFourZoneRowsTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	struct FCase
	{
		const TCHAR* Row;
		bool bBenefit;
		const TCHAR* Stat;
		float Fallback;
		float Worn;
	};
	const FCase Cases[] = {
		{TEXT("Negative_Persistent_AOE_effects_expire_40_60_faster"), false,
		 UCataclysmDamageCalculation::PersistentAreaDurationStat,
		 UCataclysmDamageCalculation::NormalPersistentAreaDuration, 40.0f},
		{TEXT("Positive_Persistent_AOE_zones_deal_10_20_increased_dama"), true,
		 UCataclysmDamageCalculation::ZoneDamagePerEnemyInsideStat, 0.0f, 20.0f},
		{TEXT("Positive_Your_persistent_AOE_zones_also_slow_enemies_with"), true,
		 UCataclysmDamageCalculation::ZoneSlowPercentStat, 0.0f, 35.0f},
		{TEXT("Negative_You_can_only_have_1_persistent_AOE_effect_active"), false,
		 UCataclysmDamageCalculation::OnlyOnePersistentAreaStat, 0.0f, 1.0f},
	};

	for (const FCase& Case : Cases)
	{
		FWorn Worn(Case.Row, Case.bBenefit);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		const FName Stat(Case.Stat);
		TestEqual(FString::Printf(TEXT("%s, worn: %s is %.0f.%s"), Case.Row, Case.Stat, Case.Worn,
					  CataclysmRepeatRowsTest::OlderAsset),
			Worn.ASC()->StatForSkill(Stat, FGameplayTagContainer(), Case.Fallback), Case.Worn, 0.01f);

		Worn.Wearer->Equipment->UnequipEverything();
		Worn.Wearer->Equipment->RefreshAttributes(Worn.ASC());
		TestEqual(FString::Printf(TEXT("%s, taken off: %s is %.0f again"), Case.Row, Case.Stat, Case.Fallback),
			Worn.ASC()->StatForSkill(Stat, FGameplayTagContainer(), Case.Fallback), Case.Fallback, 0.01f);
	}
	return true;
}

// AN AILMENT THAT PASSES ON WHEN ITS CARRIER DIES. Issue #919, the owner's "build it" of 2026-10-06, and issue
// #1833 for the two rows that add to it. A line of creatures along X: the one that dies stands 10 metres out, and
// the others are placed by how far they stand from its body.
namespace CataclysmSpreadOnDeathTest
{
	using namespace CataclysmAilmentRiderRowTest;

	/** A creature of the monsters' side, `FromTheBody` metres beyond the one that dies. */
	ACataclysmEnemyCharacter* Beside(UWorld* World, float FromTheBody)
	{
		ACataclysmEnemyCharacter* Made = Creature(World, 10.0f + FromTheBody);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		}
		return Made;
	}

	bool Carries(const AActor* Who, const FGameplayTag& Tag)
	{
		const UCataclysmAbilitySystemComponent* System = SystemOf(Who);
		return System && System->HasMatchingGameplayTag(Tag);
	}

	/** How many of these carry the ailment. */
	int32 Carrying(const TArray<ACataclysmEnemyCharacter*>& Line, const FGameplayTag& Tag)
	{
		int32 Count = 0;
		for (const ACataclysmEnemyCharacter* One : Line)
		{
			Count += Carries(One, Tag) ? 1 : 0;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDiseaseSpreadsOnDeathTest,
	"Cataclysm.Enchantments.ADiseasedEnemysDeathPassesItsDiseaseToTheTwoNearestWithinFiveMetresForTheTimeItHadLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * DISEASE'S OWN BEHAVIOUR, WITH NO ROW WORN. Issue #919, the design document:
 * "on the target's death it spreads its remaining duration to the 2 nearest
 * enemies within 5 metres". Ruled 2026-10-06: two, nearest first, the time the
 * original had left at the same damage, with its applier as the source.
 *
 * THROUGH A REAL DEATH, `ACataclysmEnemyCharacter::HandleDeath`, so the place
 * the spread is called from is what is tested. Four creatures stand 1, 2, 3 and
 * 6 metres from the body: the first two receive the disease, the third is not
 * among the two nearest, and the fourth is out of reach. A bleed on the same
 * body passes to nobody, because no row is worn and only Disease spreads by
 * itself. And the copies are not the applier applying anything: its
 * `dot_applied` event is not raised by them.
 */
bool FCataclysmDiseaseSpreadsOnDeathTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmSpreadOnDeathTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);
	const FGameplayTag Disease = Ailment(TEXT("Keyword.DoT.Disease"));
	const FGameplayTag Bleed = Ailment(TEXT("Keyword.DoT.Bleed"));
	ACataclysmEnemyCharacter* Dying = Beside(World, 0.0f);
	ACataclysmEnemyCharacter* One = Beside(World, 1.0f);
	ACataclysmEnemyCharacter* Two = Beside(World, 2.0f);
	ACataclysmEnemyCharacter* Three = Beside(World, 3.0f);
	ACataclysmEnemyCharacter* Six = Beside(World, 6.0f);
	if (!TestTrue(TEXT("set-up: five creatures and two ailment tags"),
				  Dying && One && Two && Three && Six && Disease.IsValid() && Bleed.IsValid()))
	{
		return false;
	}

	// TEN SECONDS OF EACH AT ONE POINT A SECOND, AND FOUR SECONDS PASS.
	if (!TestTrue(TEXT("set-up: the wearer diseases and bleeds the one that will die"),
				  Ail(Wearer.Actor, Dying, Disease) && Ail(Wearer.Actor, Dying, Bleed)))
	{
		return false;
	}
	CataclysmTestWorld::RunClock(World, 4.0f);

	int32 ApplicationsAnnounced = 0;
	const FDelegateHandle Listening = Wearer.AbilitySystem->OnActionEvent.AddLambda(
		[&ApplicationsAnnounced](FName Event)
		{
			ApplicationsAnnounced += Event == FName(TEXT("dot_applied")) ? 1 : 0;
		});
	Dying->HandleDeath();
	Wearer.AbilitySystem->OnActionEvent.Remove(Listening);

	TestTrue(TEXT("the nearest creature, 1 metre from the body, carries the disease"), Carries(One, Disease));
	TestTrue(TEXT("and the second nearest, 2 metres away"), Carries(Two, Disease));
	TestFalse(TEXT("the third nearest does not: two is the count"), Carries(Three, Disease));
	TestFalse(TEXT("nor the one 6 metres away"), Carries(Six, Disease));
	TestEqual(TEXT("the bleed passed to nobody: no row is worn, and only Disease spreads by itself"),
		Carrying({One, Two, Three, Six}, Bleed), 0);
	TestEqual(TEXT("a copy is not the wearer applying a damage over time: dot_applied was not raised"),
		ApplicationsAnnounced, 0);

	// WHAT THE COPY IS: the same damage a second, for the six seconds the original had left, from the wearer.
	UCataclysmSkillEffects::FRunningAilment Copy;
	if (TestTrue(TEXT("the copy is a running disease"), UCataclysmSkillEffects::RunningAilmentOn(One, Disease, Copy)))
	{
		TestEqual(TEXT("at the original's one point a second"), Copy.DamagePerSecond, 1.0f, 0.001f);
		TestEqual(TEXT("for the six seconds the original had left"), Copy.SecondsLeft, 6.0f, 0.3f);
		TestTrue(TEXT("with the wearer as its source"), Copy.Applier.Get() == Wearer.Actor);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDiseaseSpreadPassesOverCarriersTest,
	"Cataclysm.Enchantments.ADiseaseSpreadPassesOverAnEnemyThatAlreadyCarriesItAndACopyPassesOnAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * TWO RULINGS OF 2026-10-06, with no row worn.
 *
 * AN ENEMY THAT ALREADY CARRIES THE AILMENT IS PASSED OVER, NOT REFRESHED. The
 * creature nearest the body is diseased four seconds after the one that dies,
 * so it has ten seconds left where a copy would bring six. The two copies go to
 * the next two nearest, and the nearest keeps its own ten.
 *
 * A COPY PASSES ON AGAIN WHEN ITS OWN CARRIER DIES. A fifth creature stands 6.5
 * metres from the first body, out of its reach, and 3.5 metres from the third
 * creature. When the third creature dies carrying its copy, the fifth is the
 * only enemy near it that does not carry the disease, and receives it.
 */
bool FCataclysmDiseaseSpreadPassesOverCarriersTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmSpreadOnDeathTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);
	const FGameplayTag Disease = Ailment(TEXT("Keyword.DoT.Disease"));
	ACataclysmEnemyCharacter* Dying = Beside(World, 0.0f);
	ACataclysmEnemyCharacter* One = Beside(World, 1.0f);
	ACataclysmEnemyCharacter* Two = Beside(World, 2.0f);
	ACataclysmEnemyCharacter* Three = Beside(World, 3.0f);
	ACataclysmEnemyCharacter* Far = Beside(World, 6.5f);
	if (!TestTrue(TEXT("set-up: five creatures and the disease tag"),
				  Dying && One && Two && Three && Far && Disease.IsValid())
		|| !TestTrue(TEXT("set-up: the wearer diseases the one that will die"), Ail(Wearer.Actor, Dying, Disease)))
	{
		return false;
	}
	CataclysmTestWorld::RunClock(World, 4.0f);
	if (!TestTrue(TEXT("set-up: four seconds later the wearer diseases the nearest one too"),
				  Ail(Wearer.Actor, One, Disease)))
	{
		return false;
	}

	Dying->HandleDeath();
	TestTrue(TEXT("the second nearest receives a copy"), Carries(Two, Disease));
	TestTrue(TEXT("and the third nearest, because the nearest already carried it and was passed over"),
		Carries(Three, Disease));
	UCataclysmSkillEffects::FRunningAilment Own;
	if (TestTrue(TEXT("the nearest still carries its own"), UCataclysmSkillEffects::RunningAilmentOn(One, Disease, Own)))
	{
		TestEqual(TEXT("with the ten seconds it had, not the copy's six"), Own.SecondsLeft, 10.0f, 0.3f);
	}
	TestFalse(TEXT("the one 6.5 metres from the body is out of its reach"), Carries(Far, Disease));

	// THE COPY PASSES ON AGAIN.
	Three->HandleDeath();
	UCataclysmSkillEffects::FRunningAilment Again;
	if (TestTrue(TEXT("when the third dies carrying its copy, the one 3.5 metres from it receives the disease"),
				 UCataclysmSkillEffects::RunningAilmentOn(Far, Disease, Again)))
	{
		TestEqual(TEXT("for the six seconds that copy had left"), Again.SecondsLeft, 6.0f, 0.3f);
		TestTrue(TEXT("with the wearer as its source still"), Again.Applier.Get() == Wearer.Actor);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDiseaseSpreadRowTest,
	"Cataclysm.Enchantments.TheDiseaseSpreadRowAddsItsThreeToTheTwoADiseasedEnemysDeathAlreadyPassesTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Disease effects you apply spread to 1-3 nearby enemies when the afflicted
 * enemy dies". Issue #1833, ruled 2026-10-06: the row ADDS its 1 to 3 to the 2
 * Disease passes to by itself. WORN at the top of its roll, so 2 and 3: of six
 * creatures within 5 metres of the body the five nearest receive it and the
 * sixth does not. The count rides on the disease from when the wearer applied
 * it, as every number hung on an ailment does.
 */
bool FCataclysmDiseaseSpreadRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmSpreadOnDeathTest;
	FWorn Worn(TEXT("Positive_Disease_effects_you_apply_spread_to_1_3_nearby_e"), true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const FGameplayTag Disease = Ailment(TEXT("Keyword.DoT.Disease"));
	ACataclysmEnemyCharacter* Dying = Beside(Worn.World, 0.0f);
	TArray<ACataclysmEnemyCharacter*> Line;
	// ON BOTH SIDES OF THE BODY, each at least a metre from its neighbour, at 1, 1.5, 2, 2.5, 3 and 3.5 metres.
	// The first run of this test stood them half a metre apart on one side and its set-up failed.
	for (const float FromTheBody : {-1.0f, 1.5f, -2.0f, 2.5f, -3.0f, 3.5f})
	{
		Line.Add(Beside(Worn.World, FromTheBody));
	}
	bool bAllStand = TestNotNull(TEXT("set-up: the creature that will die was spawned"), Dying);
	for (int32 Index = 0; Index < Line.Num(); ++Index)
	{
		bAllStand &= TestNotNull(
			*FString::Printf(TEXT("set-up: creature %d of the six beside the body was spawned"), Index), Line[Index]);
	}
	if (!TestTrue(TEXT("set-up: the disease tag is registered"), Disease.IsValid()) || !bAllStand
		|| !TestTrue(TEXT("set-up: the wearer diseases the one that will die"),
					 Ail(Worn.Wearer->Actor, Dying, Disease)))
	{
		return false;
	}

	TestEqual(*(FString(TEXT("the death passes the disease to five: the two it passes to by itself and the row's "
							 "three.")) + CataclysmRepeatRowsTest::OlderAsset),
		UCataclysmContagion::SpreadFromTheDying(Dying), 5);
	TestEqual(TEXT("five of the six carry it"), Carrying(Line, Disease), 5);
	TestFalse(TEXT("and the one left out is the furthest"), Carries(Line.Last(), Disease));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBleedSpreadRowTest,
	"Cataclysm.Enchantments.TheBleedSpreadRowPassesADyingEnemysBleedToTheOneNearestWithinFiveMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "When a bleeding enemy dies, its bleed spreads to the nearest enemy within 5
 * metres". Issue #1833, ruled 2026-10-06: one, the nearest. A bleed passes to
 * nobody by itself, so the one copy is the row's. Three creatures stand 2, 3 and
 * 6 metres from the body: the first receives it and the other two do not.
 */
bool FCataclysmBleedSpreadRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmSpreadOnDeathTest;
	FWorn Worn(TEXT("Positive_When_a_bleeding_enemy_dies_its_bleed_spreads_to"), true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const FGameplayTag Bleed = Ailment(TEXT("Keyword.DoT.Bleed"));
	ACataclysmEnemyCharacter* Dying = Beside(Worn.World, 0.0f);
	ACataclysmEnemyCharacter* Two = Beside(Worn.World, 2.0f);
	ACataclysmEnemyCharacter* Three = Beside(Worn.World, 3.0f);
	ACataclysmEnemyCharacter* Six = Beside(Worn.World, 6.0f);
	if (!TestTrue(TEXT("set-up: four creatures and the bleed tag"), Dying && Two && Three && Six && Bleed.IsValid())
		|| !TestTrue(TEXT("set-up: the wearer bleeds the one that will die"), Ail(Worn.Wearer->Actor, Dying, Bleed)))
	{
		return false;
	}

	TestEqual(*(FString(TEXT("the death passes the bleed to one.")) + CataclysmRepeatRowsTest::OlderAsset),
		UCataclysmContagion::SpreadFromTheDying(Dying), 1);
	TestTrue(TEXT("the nearest, 2 metres from the body, carries it"), Carries(Two, Bleed));
	TestFalse(TEXT("the next does not"), Carries(Three, Bleed));
	TestFalse(TEXT("nor the one 6 metres away"), Carries(Six, Bleed));
	return true;
}

// THE TWO AUTHORED ROWS THAT ROLL FOR A USE TO HIT ITS OWN USER. Issue #1833, on the actions of the entry "A row can
// roll for a use to hit its own user instead of any enemy, whole or by half", by the owner's decision of 2026-10-06.
// Each test wears the real drawback at the top of its roll, which for a drawback is its harshest figure, and hands
// its ability system a use as the player character hands it, with `Cataclysm.UseOutcomeRoll` pinned. Each stops at
// the share the rows recorded for the use; what a use then does with it is that entry's test, with a row made by hand.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMeleeSkillsHitYouRowTest,
	"Cataclysm.Enchantments.TheMeleeSkillsHitYouInsteadRowRollsForAMeleeSkillAndRecordsTheWholeHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Melee skills have a 10%-20% chance to hit you instead of the enemy".
 * `use_hits_its_user` on `skill_use`, 10 to 20, scoped to `Type.Melee`. The
 * share recorded is 100: the whole hit. "Skills" is `skill_use`, so the basic
 * attack does not roll.
 */
bool FCataclysmMeleeSkillsHitYouRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmUseOutcomeRowsTest;
	FWorn Worn(TEXT("Negative_Melee_skills_have_a_10_20_chance_to_hit_you_in"), false);
	CataclysmHeldTriggerTest::FHeldTriggerPinned Roll(TEXT("Cataclysm.UseOutcomeRoll"), TEXT("19.9"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
	{
		return false;
	}
	const FGameplayTagContainer Strike = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Strike, Type.Melee"));
	const FGameplayTagContainer Spell = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Spell, Type.Projectile"));
	if (!TestTrue(TEXT("set-up: the tags exist"), Strike.Num() == 2 && Spell.Num() == 2))
	{
		return false;
	}

	Use(Worn.ASC(), Strike, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestEqual(*(FString(TEXT("a roll of 19.9 against the harshest roll of 20, a melee skill: the whole hit is its "
							 "user's.")) + CataclysmRepeatRowsTest::OlderAsset),
		Worn.ASC()->PendingUseSelfHitShare(), 100.0f, 0.001f);
	Use(Worn.ASC(), Spell, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestEqual(TEXT("a spell is not a melee skill"), Worn.ASC()->PendingUseSelfHitShare(), 0.0f, 0.001f);
	Use(Worn.ASC(), Strike, /*bBasicAttack=*/true, /*bHasCooldown=*/false);
	TestEqual(TEXT("a melee basic attack is not a skill, and does not roll"),
		Worn.ASC()->PendingUseSelfHitShare(), 0.0f, 0.001f);
	Roll.Set(TEXT("20"));
	Use(Worn.ASC(), Strike, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestEqual(TEXT("a roll of 20 leaves the skill to hit the enemy"),
		Worn.ASC()->PendingUseSelfHitShare(), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpellsBackfireRowTest,
	"Cataclysm.Enchantments.TheSpellsBackfireRowRollsForASpellAndRecordsHalfTheHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Spells have a 15%-25% chance to backfire dealing half damage to you".
 * `use_backfires` on `skill_use`, 15 to 25, scoped to `Type.Spell`. The share
 * recorded is `BackfireSharePercent`, half.
 */
bool FCataclysmSpellsBackfireRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmUseOutcomeRowsTest;
	FWorn Worn(TEXT("Negative_Spells_have_a_15_25_chance_to_backfire_dealing"), false);
	CataclysmHeldTriggerTest::FHeldTriggerPinned Roll(TEXT("Cataclysm.UseOutcomeRoll"), TEXT("24.9"));
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC())
		|| !TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
	{
		return false;
	}
	const FGameplayTagContainer Strike = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Strike, Type.Melee"));
	const FGameplayTagContainer Spell = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Spell, Type.Projectile"));
	if (!TestTrue(TEXT("set-up: the tags exist"), Strike.Num() == 2 && Spell.Num() == 2))
	{
		return false;
	}

	Use(Worn.ASC(), Spell, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestEqual(*(FString(TEXT("a roll of 24.9 against the harshest roll of 25, a spell: half the hit is its "
							 "caster's.")) + CataclysmRepeatRowsTest::OlderAsset),
		Worn.ASC()->PendingUseSelfHitShare(), UCataclysmAbilitySystemComponent::BackfireSharePercent, 0.001f);
	Use(Worn.ASC(), Strike, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestEqual(TEXT("a melee skill is not a spell"), Worn.ASC()->PendingUseSelfHitShare(), 0.0f, 0.001f);
	Roll.Set(TEXT("25"));
	Use(Worn.ASC(), Spell, /*bBasicAttack=*/false, /*bHasCooldown=*/true);
	TestEqual(TEXT("a roll of 25 leaves the spell to hit the enemy"),
		Worn.ASC()->PendingUseSelfHitShare(), 0.0f, 0.001f);
	return true;
}

// A VOID SPLINTER THAT DETONATES WHEN IT IS APPLIED AGAIN. Issue #1833, ruled 2026-10-06 for "Void splinter stacks
// detonate for 50%-100% increased damage": the stack is the one running effect; the character whose application is
// running applies it again; what the running one had left is dealt at once, raised by the row; and with no row a
// second application does what it always did. The rows here are made by hand; the authored row has its own test.
namespace CataclysmDetonationTest
{
	using namespace CataclysmSpreadOnDeathTest;

	/** One per cent of current health a tick for four seconds, which is the ailment's own row. */
	bool Splinter(AActor* By, AActor* On, const FGameplayTag& Tag)
	{
		return UCataclysmSkillEffects::ApplyShareOfHealthOverTime(By, On, 0.01f, 4.0f, Tag);
	}

	float HealthOf(const AActor* Who)
	{
		const UCataclysmAbilitySystemComponent* System = SystemOf(Who);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : 0.0f;
	}

	/** What `On` loses AT ONCE when `By` applies a Void Splinter to it. No clock runs, so no tick lands. */
	float LostAtOnceWhenApplied(AActor* By, AActor* On, const FGameplayTag& Tag)
	{
		const float Before = HealthOf(On);
		Splinter(By, On, Tag);
		return Before - HealthOf(On);
	}

	/** A hand-made row that hangs this increase on the ailment's detonation. */
	FCataclysmPoolAction Detonates(const FGameplayTag& Tag, float Increase)
	{
		FCataclysmPoolAction Row;
		Row.Rider = ECataclysmAilmentRider::DetonatesWhenReapplied;
		Row.Ailment = Tag;
		Row.Percent = Increase;
		return Row;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidSplinterDetonatesTest,
	"Cataclysm.Enchantments.AVoidSplinterAppliedAgainByItsApplierDealsWhatWasLeftAtOnceOnlyWhenARowSaysSo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Four creatures built alike, each carrying a Void Splinter that has dealt
 * nothing yet, so each has the same amount left. With no row, applying it again
 * takes nothing at once. With a row of 50 it takes one and a half times what
 * was left, and with a row of 100 twice, so the second is four thirds of the
 * first: a comparison that holds whatever a creature's own defences take off.
 * Another character's Void Splinter is not detonated. And the detonated one is
 * replaced by a new one.
 */
bool FCataclysmVoidSplinterDetonatesTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmDetonationTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);
	FWearer Other(World);
	const FGameplayTag Tag = Ailment(TEXT("Keyword.DoT.VoidSplinter"));
	ACataclysmEnemyCharacter* Plain = Beside(World, 0.0f);
	ACataclysmEnemyCharacter* Half = Beside(World, 2.0f);
	ACataclysmEnemyCharacter* Whole = Beside(World, 4.0f);
	ACataclysmEnemyCharacter* Theirs = Beside(World, 6.0f);
	if (!TestTrue(TEXT("set-up: the void splinter tag is registered"), Tag.IsValid())
		|| !TestNotNull(TEXT("set-up: the first creature"), Plain)
		|| !TestNotNull(TEXT("set-up: the second creature"), Half)
		|| !TestNotNull(TEXT("set-up: the third creature"), Whole)
		|| !TestNotNull(TEXT("set-up: the fourth creature"), Theirs))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the wearer splinters three and the other character the fourth"),
				  Splinter(Wearer.Actor, Plain, Tag) && Splinter(Wearer.Actor, Half, Tag)
					  && Splinter(Wearer.Actor, Whole, Tag) && Splinter(Other.Actor, Theirs, Tag)))
	{
		return false;
	}
	TestEqual(TEXT("with no row the percent is nought: the ailment does not detonate by itself"),
		UCataclysmSkillEffects::DetonationPercentWhenReapplied(Wearer.AbilitySystem, Tag), 0.0f, 0.001f);

	// NO ROW: a second application takes nothing at once.
	TestEqual(TEXT("with no row, applying it again takes nothing at once"),
		LostAtOnceWhenApplied(Wearer.Actor, Plain, Tag), 0.0f, 0.001f);

	// A ROW OF 50: one and a half times what was left.
	Wearer.AbilitySystem->SetPoolActions({Detonates(Tag, 50.0f)});
	TestEqual(TEXT("a row of 50 makes the percent 150"),
		UCataclysmSkillEffects::DetonationPercentWhenReapplied(Wearer.AbilitySystem, Tag), 150.0f, 0.001f);
	const float HealthAtFirst = HealthOf(Half);
	const float AtHalf = LostAtOnceWhenApplied(Wearer.Actor, Half, Tag);
	if (!TestTrue(TEXT("set-up: the creature has health to lose"), HealthAtFirst > 0.0f)
		|| !TestTrue(TEXT("with a row, applying it again takes something at once"), AtHalf > 0.0f))
	{
		return false;
	}
	TestTrue(TEXT("and the creature carries a void splinter still: a new one runs"), Carries(Half, Tag));
	// THE NEW ONE HAS ALL ITS TICKS LEFT, as the first had, so a third application takes the same share of the
	// health the creature has by then.
	const float HealthAtSecond = HealthOf(Half);
	const float AtHalfAgain = LostAtOnceWhenApplied(Wearer.Actor, Half, Tag);
	TestEqual(TEXT("a third application detonates the new one for the same share of the health then held"),
		AtHalfAgain / HealthAtSecond, AtHalf / HealthAtFirst, 0.0001f);

	// A ROW OF 100: twice what was left, so four thirds of the row of 50.
	Wearer.AbilitySystem->SetPoolActions({Detonates(Tag, 100.0f)});
	const float AtWhole = LostAtOnceWhenApplied(Wearer.Actor, Whole, Tag);
	TestEqual(TEXT("a row of 100 takes four thirds of what a row of 50 took from a creature built alike"),
		AtWhole, AtHalf * 4.0f / 3.0f, AtHalf * 0.005f);

	// ANOTHER CHARACTER'S IS NOT DETONATED.
	TestEqual(TEXT("the wearer's row does not detonate a void splinter another character applied"),
		LostAtOnceWhenApplied(Wearer.Actor, Theirs, Tag), 0.0f, 0.001f);

	// A ROW ON ANOTHER AILMENT DOES NOT DETONATE THIS ONE.
	Wearer.AbilitySystem->SetPoolActions({Detonates(Ailment(TEXT("Keyword.DoT.Disease")), 100.0f)});
	TestEqual(TEXT("a row hung on another ailment leaves this one's percent at nought"),
		UCataclysmSkillEffects::DetonationPercentWhenReapplied(Wearer.AbilitySystem, Tag), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidSplinterDetonationRowTest,
	"Cataclysm.Enchantments.TheVoidSplinterDetonationRowDealsTwiceWhatWasLeftAtTheTopOfItsRoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Void splinter stacks detonate for 50%-100% increased damage". Issue #1833,
 * ruled 2026-10-06. `ailment_detonates_when_reapplied` on Void Splinter, 50 to
 * 100, WORN at the top of its roll, so a second application deals twice what
 * was left. Compared with a character that wears nothing and carries a
 * hand-made row of 100, on a creature built alike; and taken off, a further
 * application takes nothing at once.
 */
bool FCataclysmVoidSplinterDetonationRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmDetonationTest;
	FWorn Worn(TEXT("Positive_Void_splinter_stacks_detonate_for_50_100_incre"), true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	CataclysmEnchantmentEffectTest::FWearer ByHand(Worn.World);
	const FGameplayTag Tag = Ailment(TEXT("Keyword.DoT.VoidSplinter"));
	ACataclysmEnemyCharacter* Theirs = Beside(Worn.World, 0.0f);
	ACataclysmEnemyCharacter* Twin = Beside(Worn.World, 2.0f);
	if (!TestTrue(TEXT("set-up: the void splinter tag is registered"), Tag.IsValid())
		|| !TestNotNull(TEXT("set-up: the wearer's creature"), Theirs)
		|| !TestNotNull(TEXT("set-up: its twin"), Twin)
		|| !TestTrue(TEXT("set-up: each character splinters its creature"),
					 Splinter(Worn.Wearer->Actor, Theirs, Tag) && Splinter(ByHand.Actor, Twin, Tag)))
	{
		return false;
	}
	ByHand.AbilitySystem->SetPoolActions({Detonates(Tag, 100.0f)});
	const float ByHandLost = LostAtOnceWhenApplied(ByHand.Actor, Twin, Tag);
	if (!TestTrue(TEXT("set-up: the hand-made row of 100 detonates the twin's"), ByHandLost > 0.0f))
	{
		return false;
	}

	TestEqual(*(FString(TEXT("the wearer's percent is 200: the whole of what was left, raised by the row's 100.")) +
				CataclysmRepeatRowsTest::OlderAsset),
		UCataclysmSkillEffects::DetonationPercentWhenReapplied(Worn.ASC(), Tag), 200.0f, 0.001f);
	TestEqual(TEXT("applying it again takes at once what the hand-made row of 100 took from the twin"),
		LostAtOnceWhenApplied(Worn.Wearer->Actor, Theirs, Tag), ByHandLost, ByHandLost * 0.005f);

	Worn.Wearer->Equipment->UnequipEverything();
	Worn.Wearer->Equipment->RefreshAttributes(Worn.ASC());
	TestEqual(TEXT("taken off, a further application takes nothing at once"),
		LostAtOnceWhenApplied(Worn.Wearer->Actor, Theirs, Tag), 0.0f, 0.001f);
	return true;
}

// THREE MORE PERSISTENT AREA ROWS. Issue #1833, on the two flag stats and the condition of the dungeon session's
// entries of 2026-10-06, "A zone can stagger whoever enters it" and "A row can ask whether the target is standing
// in one of your ground zones". What a zone then does with each flag is those entries' tests, with rows made by hand.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmZoneFlagRowsTest,
	"Cataclysm.Enchantments.TheZoneStaggerRowAndTheZoneAilmentRowEachSetTheFlagAZoneReads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Enemies that enter your persistent AOE zones are briefly staggered" is
 * `zone_staggers_on_entry` flat 1, and "Persistent AOE zones apply a DoT to
 * enemies standing in them" is `zone_applies_own_ailment` flat 1. Each is WORN
 * and read as a zone reads it, and reads nought again when taken off.
 */
bool FCataclysmZoneFlagRowsTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	struct FCase
	{
		const TCHAR* Row;
		const TCHAR* Stat;
	};
	const FCase Cases[] = {
		{TEXT("Positive_Enemies_that_enter_your_persistent_AOE_zones_are"), TEXT("zone_staggers_on_entry")},
		{TEXT("Positive_Persistent_AOE_zones_apply_a_DoT_to_enemies_stan"), TEXT("zone_applies_own_ailment")},
	};
	for (const FCase& Case : Cases)
	{
		FWorn Worn(Case.Row, true);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		const FName Stat(Case.Stat);
		TestEqual(FString::Printf(TEXT("%s, worn: %s is 1.%s"), Case.Row, Case.Stat, CataclysmRepeatRowsTest::OlderAsset),
			Worn.ASC()->StatForSkill(Stat, FGameplayTagContainer(), 0.0f), 1.0f, 0.01f);

		Worn.Wearer->Equipment->UnequipEverything();
		Worn.Wearer->Equipment->RefreshAttributes(Worn.ASC());
		TestEqual(FString::Printf(TEXT("%s, taken off: %s is 0 again"), Case.Row, Case.Stat),
			Worn.ASC()->StatForSkill(Stat, FGameplayTagContainer(), 0.0f), 0.0f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDamageInYourZonesRowTest,
	"Cataclysm.Enchantments.TheDamageInYourZonesRowRaisesABlowOnlyOnACreatureStandingInTheWearersZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "You deal 15%-30% increased damage to enemies standing in your persistent AOE
 * zones". Issue #1833, ruled 2026-10-06: `attack_damage` and `spell_damage`
 * increased, 15 to 30, under `target_in_your_zone`. WORN by a real player at
 * the top of its roll, 30: its blow on a creature standing in a zone the wearer
 * owns is 1.3 times its blow on a creature standing in none.
 */
bool FCataclysmDamageInYourZonesRowTest::RunTest(const FString&)
{
	using namespace CataclysmApplyStatusRowTest;
	CataclysmConsecutiveRowTest::FStriker Striker(
		TEXT("Positive_You_deal_15_30_increased_damage_to_enemies_sta"),
		CataclysmEnchantmentEffectTest::DrawbackWithNoEffect);
	if (!TestTrue(TEXT("a striker and two creatures"), Striker.Ready()))
	{
		return false;
	}
	ACataclysmGroundZone* Zone = ACataclysmGroundZone::Spawn(
		Striker.Character, Striker.First->GetActorLocation(), /*RadiusCm=*/150.0f, /*Duration=*/10.0f,
		/*DamagePerTick=*/1.0f);
	if (!TestNotNull(TEXT("set-up: the wearer's zone"), Zone)
		|| !TestTrue(TEXT("set-up: the first creature stands in it"), Zone->Covers(Striker.First->GetActorLocation()))
		|| !TestFalse(TEXT("set-up: the second does not"), Zone->Covers(Striker.Second->GetActorLocation())))
	{
		return false;
	}
	const float InTheZone = Blow(Striker, Striker.First, false, false);
	const float Outside = Blow(Striker, Striker.Second, false, false);
	if (!TestTrue(*FString::Printf(TEXT("a blow on the creature outside took %.2f"), Outside), Outside > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the blow on the creature in the wearer's zone is 1.3 times it. If not, DT_EnchantmentEffects "
				   "may be older than the rows: run tools/generate_datatable_assets.py"),
		InTheZone, 1.3f * Outside, 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidSplinterSpreadsOnDeathTest,
	"Cataclysm.Enchantments.AVoidSplinterPassesToEveryEnemyARowCountsWithinFiveMetresForTheTimeItHadLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Void splinter stacks spread to nearby enemies when the afflicted enemy
 * dies", with a row made by hand. Issue #1833, ruled 2026-10-06. A Void Splinter
 * passes to nobody by itself. With a count of 100 hung on it, the one on a dying
 * creature passes to every enemy within 5 metres of the body: three stand 1, 2
 * and 3 metres away and receive it, and one 6 metres away does not. A copy is
 * the same share a second for the seconds the original had left, and carries
 * the count again, so it passes on when its own carrier dies.
 */
bool FCataclysmVoidSplinterSpreadsOnDeathTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmDetonationTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);
	const FGameplayTag Tag = Ailment(TEXT("Keyword.DoT.VoidSplinter"));
	ACataclysmEnemyCharacter* Plain = Beside(World, -8.0f);
	ACataclysmEnemyCharacter* Dying = Beside(World, 0.0f);
	ACataclysmEnemyCharacter* One = Beside(World, 1.0f);
	ACataclysmEnemyCharacter* Two = Beside(World, 2.0f);
	ACataclysmEnemyCharacter* Three = Beside(World, 3.0f);
	ACataclysmEnemyCharacter* Six = Beside(World, 6.0f);
	if (!TestTrue(TEXT("set-up: the void splinter tag is registered"), Tag.IsValid())
		|| !TestNotNull(TEXT("set-up: the creature splintered with no row"), Plain)
		|| !TestNotNull(TEXT("set-up: the creature that will die"), Dying)
		|| !TestNotNull(TEXT("set-up: the creature 1 metre from it"), One)
		|| !TestNotNull(TEXT("set-up: the creature 2 metres from it"), Two)
		|| !TestNotNull(TEXT("set-up: the creature 3 metres from it"), Three)
		|| !TestNotNull(TEXT("set-up: the creature 6 metres from it"), Six))
	{
		return false;
	}

	// WITH NO ROW, a void splinter passes to nobody. This creature stands 8 metres the other side of the body,
	// so nothing below reaches it either.
	if (!TestTrue(TEXT("set-up: the wearer splinters a creature with no row"), Splinter(Wearer.Actor, Plain, Tag)))
	{
		return false;
	}
	TestEqual(TEXT("with no row, a void splinter passes to nobody"),
		UCataclysmContagion::SpreadFromTheDying(Plain), 0);

	FCataclysmPoolAction Row;
	Row.Rider = ECataclysmAilmentRider::SpreadOnDeath;
	Row.Ailment = Tag;
	Row.Percent = 100.0f;
	Wearer.AbilitySystem->SetPoolActions({Row});
	if (!TestTrue(TEXT("set-up: with the row, the wearer splinters the one that will die"),
				  Splinter(Wearer.Actor, Dying, Tag)))
	{
		return false;
	}
	// A SECOND AND A HALF PASSES: one tick has landed and two and a half seconds are left.
	CataclysmTestWorld::RunClock(World, 1.5f);

	if (!TestEqual(TEXT("the death passes the void splinter to the three within 5 metres"),
				   UCataclysmContagion::SpreadFromTheDying(Dying), 3))
	{
		return false;
	}
	TestTrue(TEXT("the creature 1 metre away carries it"), Carries(One, Tag));
	TestTrue(TEXT("and the one 2 metres away"), Carries(Two, Tag));
	TestTrue(TEXT("and the one 3 metres away"), Carries(Three, Tag));
	TestFalse(TEXT("the one 6 metres away does not"), Carries(Six, Tag));

	UCataclysmSkillEffects::FRunningAilment Copy;
	if (!TestTrue(TEXT("the copy is a running void splinter"), UCataclysmSkillEffects::RunningAilmentOn(One, Tag, Copy)))
	{
		return false;
	}
	TestTrue(TEXT("it takes a share of health"), Copy.bShareOfHealth);
	TestEqual(TEXT("the same share a second as the original, one per cent"), Copy.DamagePerSecond, 0.01f, 0.0001f);
	TestEqual(TEXT("for the two and a half seconds the original had left"), Copy.SecondsLeft, 2.5f, 0.1f);
	TestTrue(TEXT("with the wearer as its source"), Copy.Applier.Get() == Wearer.Actor);
	const UCataclysmAbilitySystemComponent* Its = SystemOf(One);
	TestEqual(TEXT("and the copy carries the row's count, so it passes on again when its own carrier dies"),
		Its ? Its->AilmentRiderPercentCarriedOn(Tag, ECataclysmAilmentRider::SpreadOnDeath) : -1.0f, 100.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidSplinterSpreadRowTest,
	"Cataclysm.Enchantments.TheVoidSplinterSpreadRowPassesADyingEnemysVoidSplinterToEveryEnemyWithinFiveMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Void splinter stacks spread to nearby enemies when the afflicted enemy
 * dies". Issue #1833, ruled 2026-10-06: every enemy within 5 metres.
 * `ailment_spread_on_death` on Void Splinter, 100. The real row WORN: of six
 * creatures within 5 metres of the body all six receive it, and one 6 metres
 * away does not.
 */
bool FCataclysmVoidSplinterSpreadRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmDetonationTest;
	FWorn Worn(TEXT("Positive_Void_splinter_stacks_spread_to_nearby_enemies_wh"), true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const FGameplayTag Tag = Ailment(TEXT("Keyword.DoT.VoidSplinter"));
	ACataclysmEnemyCharacter* Dying = Beside(Worn.World, 0.0f);
	TArray<ACataclysmEnemyCharacter*> Near;
	for (const float FromTheBody : {-1.0f, 1.5f, -2.0f, 2.5f, -3.0f, 3.5f})
	{
		Near.Add(Beside(Worn.World, FromTheBody));
	}
	ACataclysmEnemyCharacter* Six = Beside(Worn.World, 6.0f);
	bool bAllStand = TestNotNull(TEXT("set-up: the creature that will die was spawned"), Dying)
		&& TestNotNull(TEXT("set-up: the creature 6 metres from the body was spawned"), Six);
	for (int32 Index = 0; Index < Near.Num(); ++Index)
	{
		bAllStand &= TestNotNull(
			*FString::Printf(TEXT("set-up: creature %d of the six near the body was spawned"), Index), Near[Index]);
	}
	if (!TestTrue(TEXT("set-up: the void splinter tag is registered"), Tag.IsValid()) || !bAllStand
		|| !TestTrue(TEXT("set-up: the wearer splinters the one that will die"),
					 Splinter(Worn.Wearer->Actor, Dying, Tag)))
	{
		return false;
	}

	TestEqual(*(FString(TEXT("the death passes the void splinter to all six within 5 metres.")) +
				CataclysmRepeatRowsTest::OlderAsset),
		UCataclysmContagion::SpreadFromTheDying(Dying), 6);
	TestEqual(TEXT("all six carry it"), Carrying(Near, Tag), 6);
	TestFalse(TEXT("the one 6 metres away does not"), Carries(Six, Tag));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlagueDoctorSixRowTest,
	"Cataclysm.Enchantments.PlagueDoctorsSixPiecesSetTheFlagThatRefreshesTheWearersOtherDamageOverTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Plague Doctor (6-Piece Bonus): When you apply a DoT to an enemy, all other
 * DoTs you have on that enemy have their duration refreshed". Issue #1833,
 * ruled 2026-10-06: `dot_application_refreshes_others` flat 1. FIVE PIECES AND
 * THEN SIX of the real set: at five the stat reads nought and at six it reads
 * 1, as the place that applies a damage over time effect asks it. What that
 * place then does is `Cataclysm.RemainingDamage.`'s test, with the stat held by
 * hand.
 */
bool FCataclysmPlagueDoctorSixRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmHealthThresholdRowTest;
	const TCHAR* Bonus = TEXT("Positive_Plague_Doctor_2_Piece_Bonus_Your_DoT_effects");
	const FName Stat(UCataclysmSkillEffects::DotApplicationRefreshesOthersStat);
	const FGameplayTag Poison =
		FGameplayTag::RequestGameplayTag(FName(TEXT("Keyword.DoT.Poison")), /*ErrorIfNotFound=*/false);
	if (!TestTrue(TEXT("set-up: the poison tag exists"), Poison.IsValid()))
	{
		return false;
	}

	for (const int32 Pieces : {5, 6})
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		FWearer Wearer(World);
		WearSet(Wearer, Bonus, Pieces);
		const float Read = Wearer.AbilitySystem->StatForSkill(Stat, FGameplayTagContainer(Poison), 0.0f);
		if (Pieces == 5)
		{
			TestEqual(TEXT("five pieces: the flag reads nought"), Read, 0.0f, 0.001f);
		}
		else
		{
			TestEqual(*(FString(TEXT("six pieces: the flag reads 1, asked with an ailment's tag.")) +
						CataclysmRepeatRowsTest::OlderAsset),
				Read, 1.0f, 0.001f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSixZoneRowsTest,
	"Cataclysm.Enchantments.TheSixRowsOnZonesASkillLeavesAndWhatAZoneDoesEachGiveTheStatTheGameReads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Six rows on the stats of the dungeon session's four zone entries of
 * 2026-10-06. Issue #1833. Each is WORN at the top of its roll and read as the
 * game reads it: the three that give a skill a zone are scoped to a kind of
 * skill, so each is read with that kind's tag and reads nought with none. Each
 * reads nought again when taken off. What a skill or a zone then does with the
 * stat is those entries' tests, with a stat line made by hand.
 */
bool FCataclysmSixZoneRowsTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	struct FCase
	{
		const TCHAR* Row;
		bool bBenefit;
		const TCHAR* Stat;
		/** The tag the row is scoped to, or nothing for a row with no scope. */
		const TCHAR* Scope;
		float Worn;
	};
	const FCase Cases[] = {
		{TEXT("Positive_Your_movement_ability_leaves_a_persistent_AOE_zo"), true,
		 TEXT("zone_at_start_and_end_seconds"), TEXT("Slot.Movement"), 5.0f},
		{TEXT("Positive_Charge_skills_leave_a_persistent_AOE_zone_at_the"), true,
		 TEXT("zone_at_impact_seconds"), TEXT("Keyword.Charge"), 6.0f},
		{TEXT("Positive_Your_spells_leave_a_persistent_AOE_zone_at_the_i"), true,
		 TEXT("zone_at_impact_seconds"), TEXT("Type.Spell"), 4.0f},
		{TEXT("Negative_Persistent_AOE_zones_also_damage_you_if_you_stan"), false,
		 TEXT("zone_damages_its_owner"), nullptr, 1.0f},
		{TEXT("Positive_Your_persistent_AOE_zones_follow_you_as_you_move"), true,
		 TEXT("zone_follows_owner_percent"), nullptr, 50.0f},
		{TEXT("Positive_Your_minions_leave_behind_chaos_pools_when_they"), true,
		 TEXT("minions_leave_chaos_pools"), nullptr, 1.0f},
	};

	for (const FCase& Case : Cases)
	{
		FWorn Worn(Case.Row, Case.bBenefit);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		const FName Stat(Case.Stat);
		const FGameplayTagContainer Asked =
			Case.Scope ? CataclysmRepeatRowsTest::Tagged(Case.Scope) : FGameplayTagContainer();
		if (Case.Scope && !TestTrue(FString::Printf(TEXT("set-up: the tag %s exists"), Case.Scope), Asked.Num() == 1))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("%s, worn: %s is %.0f.%s"), Case.Row, Case.Stat, Case.Worn,
					  CataclysmRepeatRowsTest::OlderAsset),
			Worn.ASC()->StatForSkill(Stat, Asked, 0.0f), Case.Worn, 0.01f);
		if (Case.Scope)
		{
			TestEqual(FString::Printf(TEXT("%s: asked for a skill with no tag, %s is 0"), Case.Row, Case.Stat),
				Worn.ASC()->StatForSkill(Stat, FGameplayTagContainer(), 0.0f), 0.0f, 0.01f);
		}

		Worn.Wearer->Equipment->UnequipEverything();
		Worn.Wearer->Equipment->RefreshAttributes(Worn.ASC());
		TestEqual(FString::Printf(TEXT("%s, taken off: %s is 0 again"), Case.Row, Case.Stat),
			Worn.ASC()->StatForSkill(Stat, Asked, 0.0f), 0.0f, 0.01f);
	}
	return true;
}

// TWO EVENTS THAT CARRY WHO DIED AND HOW MUCH HEALTH IT HAD. Ruled 2026-10-07. Issue #1833. The rows here are made
// by hand: a pool action that takes a share of the event's amount shows the amount, and a stack granted only on
// an event carrying a tag shows the tags.
namespace CataclysmDeathEventsTest
{
	using namespace CataclysmSpreadOnDeathTest;

	/** A stack granted on `Event` only when the event carries `Required`. */
	FCataclysmPoolAction GrantOn(const TCHAR* Event, FName Key, const FGameplayTag& Required)
	{
		FCataclysmPoolAction Grant;
		Grant.Event = FName(Event);
		Grant.StackKey = Key;
		Grant.StackSeconds = 5.0f;
		Grant.StackCap = 5;
		Grant.TriggerKey = Key;
		Grant.RequiredTags = FGameplayTagContainer(Required);
		return Grant;
	}

	float MaximumHealthOf(const AActor* Who)
	{
		const UCataclysmAbilitySystemComponent* System = SystemOf(Who);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) : 0.0f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAfflictedDeathEventTest,
	"Cataclysm.Enchantments.AnEnemysDeathIsHeardByWhoeverHasAnAilmentOnItWithItsMaximumHealthAndTheirOwnAilments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `afflicted_death`, through a real death. A creature carries the wearer's
 * poison and another character's burn, and dies. EACH of the two hears the
 * event once. The wearer's row that restores the whole of the event's amount
 * restores the dead creature's maximum health, so the amount is that. The
 * wearer's stack kept to poison is granted and its stack kept to burn is not,
 * so the tags are the wearer's own ailments on the body; the other character's
 * stack kept to burn is granted. A creature that dies carrying nothing is heard
 * by nobody.
 */
bool FCataclysmAfflictedDeathEventTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmDeathEventsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);
	FWearer Other(World);
	const FGameplayTag Poison = Ailment(TEXT("Keyword.DoT.Poison"));
	const FGameplayTag Burn = Ailment(TEXT("Keyword.DoT.Burn"));
	ACataclysmEnemyCharacter* Clean = Beside(World, -6.0f);
	ACataclysmEnemyCharacter* Dying = Beside(World, 0.0f);
	if (!TestTrue(TEXT("set-up: the poison tag is registered"), Poison.IsValid())
		|| !TestTrue(TEXT("set-up: the burn tag is registered"), Burn.IsValid())
		|| !TestNotNull(TEXT("set-up: the creature that dies carrying nothing"), Clean)
		|| !TestNotNull(TEXT("set-up: the creature that dies afflicted"), Dying))
	{
		return false;
	}
	const float Maximum = MaximumHealthOf(Dying);
	if (!TestTrue(TEXT("set-up: the creature has a maximum health"), Maximum > 0.0f)
		|| !TestTrue(TEXT("set-up: the wearer poisons it and the other character burns it"),
					 Ail(Wearer.Actor, Dying, Poison) && Ail(Other.Actor, Dying, Burn)))
	{
		return false;
	}

	const FName OnPoison(TEXT("Test:poison"));
	const FName OnBurn(TEXT("Test:burn"));
	Wearer.AbilitySystem->SetPoolActions({
		PoolAction(TEXT("afflicted_death"), TEXT("health"), 100.0f, ECataclysmPoolActionBase::EventAmount),
		GrantOn(TEXT("afflicted_death"), OnPoison, Poison), GrantOn(TEXT("afflicted_death"), OnBurn, Burn)});
	Other.AbilitySystem->SetPoolActions({GrantOn(TEXT("afflicted_death"), OnBurn, Burn)});
	// ONE POINT OF HEALTH IN A POOL TEN TIMES THE CREATURE'S MAXIMUM, so the whole amount fits.
	GivePools(*Wearer.AbilitySystem, /*Health=*/1.0f, /*MaxHealth=*/Maximum * 10.0f);
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();

	// A CREATURE CARRYING NOTHING IS HEARD BY NOBODY.
	TestEqual(TEXT("a creature that dies carrying no ailment is heard by nobody"),
		UCataclysmContagion::AnnounceAfflictedDeath(Clean), 0);
	TestEqual(TEXT("and the wearer's row restores nothing"),
		Wearer.AbilitySystem->GetNumericAttribute(Health), 1.0f, 0.01f);

	Dying->HandleDeath();
	TestEqual(TEXT("the amount is the dead creature's maximum health: a row taking the whole of it restores that"),
		Wearer.AbilitySystem->GetNumericAttribute(Health), 1.0f + Maximum, Maximum * 0.001f);
	TestEqual(TEXT("the wearer hears it once, with its own poison among the tags"),
		Wearer.AbilitySystem->OwnStacksHeld(OnPoison), 1);
	TestEqual(TEXT("and without the other character's burn"), Wearer.AbilitySystem->OwnStacksHeld(OnBurn), 0);
	TestEqual(TEXT("the other character hears it once too, with its burn"),
		Other.AbilitySystem->OwnStacksHeld(OnBurn), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillEventCarriesTheSlainTest,
	"Cataclysm.Enchantments.AKillIsHeardWithTheSlainEnemysMaximumHealthAsItsAmount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The `kill` event, through a real player character and a real kill, as the
 * pawn raises it when it hears the death announced. A hand-made row that
 * restores the whole of the event's amount restores the slain creature's
 * maximum health. Before 2026-10-07 the event carried no amount and such a row
 * restored nothing.
 */
bool FCataclysmKillEventCarriesTheSlainTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmDeathEventsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* ASC =
		PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("the announcements"), Events) || !TestNotNull(TEXT("ability system component"), ASC)
		|| !TestNotNull(TEXT("a character"), Character))
	{
		return false;
	}
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();
	ASC->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

	ACataclysmEnemyCharacter* Victim =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(200.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("set-up: a creature to kill"), Victim))
	{
		return false;
	}
	Victim->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	// A MAXIMUM OF FIFTY, which one blow of a hundred takes. `SetHealth` sets the maximum and fills it.
	Victim->SetHealth(50.0f);
	const float Maximum = MaximumHealthOf(Victim);
	if (!TestEqual(TEXT("set-up: the creature's maximum health is fifty"), Maximum, 50.0f, 0.01f))
	{
		return false;
	}

	ASC->SetPoolActions({PoolAction(TEXT("kill"), TEXT("health"), 100.0f, ECataclysmPoolActionBase::EventAmount)});
	GivePools(*ASC, /*Health=*/1.0f, /*MaxHealth=*/Maximum * 10.0f);
	const uint32 DeathsBefore = Events->DeathsSent();
	UCataclysmSkillEffects::ApplyHit(Character, Victim, /*DamagePercent=*/100.0f);
	if (!TestTrue(TEXT("set-up: the kill was announced"), Events->DeathsSent() == DeathsBefore + 1))
	{
		return false;
	}
	TestEqual(TEXT("a row taking the whole of the kill's amount restores the slain creature's maximum health"),
		ASC->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()), 1.0f + Maximum,
		Maximum * 0.001f);
	return true;
}

// THE BLAST FROM AN ENEMY THAT DIED CARRYING THE WEARER'S AILMENT. Ruled 2026-10-07, for "Plague Doctor (10-Piece
// Bonus): When an enemy dies while affected by a DoT from you, it explodes and applies all of your DoTs to all
// nearby enemies". Issue #1833. The row is made by hand; the authored row has its own test.
namespace CataclysmBlastFromTheDyingTest
{
	using namespace CataclysmDeathEventsTest;

	/** A hand-made row that blasts for this share of the dead enemy's maximum health. */
	FCataclysmPoolAction Blasts(float Percent)
	{
		FCataclysmPoolAction Row;
		Row.Event = FName(TEXT("afflicted_death"));
		Row.bBlastFromTheDying = true;
		Row.Percent = Percent;
		Row.TriggerKey = FName(TEXT("Test:blast"));
		return Row;
	}

	float HealthNow(const AActor* Who)
	{
		const UCataclysmAbilitySystemComponent* System = SystemOf(Who);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : 0.0f;
	}

	/** What `Damage` takes from `On` as the blast delivers it: the control every blast is compared with. */
	float TakenByABlastOf(AActor* By, AActor* On, float Damage)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bIsArea = true;
		Delivery.bCannotBeRetaliatedAgainst = true;
		Delivery.bCannotCriticallyStrike = true;
		Delivery.bCarriesNoWeaponSubType = true;
		Delivery.bCannotLeech = true;
		const float Before = HealthNow(On);
		UCataclysmSkillEffects::ApplyDirectDamage(By, On, Damage, Delivery);
		return Before - HealthNow(On);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBlastFromTheDyingTest,
	"Cataclysm.Enchantments.AnAfflictedEnemysDeathBlastsThoseWithinFiveMetresAndPassesTheWearersAilmentsToThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A creature of 100,000 maximum health carries the wearer's poison and another
 * character's burn, and dies two and a half seconds in. With a row of 20, the
 * creatures 2 and 4 metres from the body each lose what a direct area hit of
 * 20,000 takes from a creature built alike, and the one 7 metres away loses
 * nothing. Those two then carry the wearer's poison, for the seven and a half
 * seconds it had left, and not the other character's burn. No `dot_applied` is
 * raised: a copy is not an application.
 */
bool FCataclysmBlastFromTheDyingTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmBlastFromTheDyingTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);
	FWearer Other(World);
	const FGameplayTag Poison = Ailment(TEXT("Keyword.DoT.Poison"));
	const FGameplayTag Burn = Ailment(TEXT("Keyword.DoT.Burn"));
	ACataclysmEnemyCharacter* Control = Beside(World, -8.0f);
	ACataclysmEnemyCharacter* Dying = Beside(World, 0.0f);
	ACataclysmEnemyCharacter* Two = Beside(World, 2.0f);
	ACataclysmEnemyCharacter* Four = Beside(World, 4.0f);
	ACataclysmEnemyCharacter* Seven = Beside(World, 7.0f);
	if (!TestTrue(TEXT("set-up: the poison tag is registered"), Poison.IsValid())
		|| !TestTrue(TEXT("set-up: the burn tag is registered"), Burn.IsValid())
		|| !TestNotNull(TEXT("set-up: the control creature, 8 metres the other side"), Control)
		|| !TestNotNull(TEXT("set-up: the creature that will die"), Dying)
		|| !TestNotNull(TEXT("set-up: the creature 2 metres from it"), Two)
		|| !TestNotNull(TEXT("set-up: the creature 4 metres from it"), Four)
		|| !TestNotNull(TEXT("set-up: the creature 7 metres from it"), Seven))
	{
		return false;
	}
	const float Maximum = MaximumHealthOf(Dying);
	if (!TestTrue(TEXT("set-up: the creature that will die has a maximum health"), Maximum > 0.0f)
		|| !TestTrue(TEXT("set-up: the wearer poisons it and the other character burns it"),
					 Ail(Wearer.Actor, Dying, Poison) && Ail(Other.Actor, Dying, Burn)))
	{
		return false;
	}
	Wearer.AbilitySystem->SetPoolActions({Blasts(20.0f)});
	// TWO AND A HALF SECONDS, BETWEEN TICKS: the poison has seven and a half left.
	CataclysmTestWorld::RunClock(World, 2.5f);

	const float ByControl = TakenByABlastOf(Wearer.Actor, Control, Maximum * 0.2f);
	if (!TestTrue(TEXT("set-up: a direct area hit of a fifth of that maximum takes something from a creature built "
					   "alike"), ByControl > 0.0f))
	{
		return false;
	}
	int32 ApplicationsAnnounced = 0;
	const FDelegateHandle Listening = Wearer.AbilitySystem->OnActionEvent.AddLambda(
		[&ApplicationsAnnounced](FName Event)
		{
			ApplicationsAnnounced += Event == FName(TEXT("dot_applied")) ? 1 : 0;
		});
	const float TwoBefore = HealthNow(Two);
	const float FourBefore = HealthNow(Four);
	const float SevenBefore = HealthNow(Seven);
	Dying->HandleDeath();
	Wearer.AbilitySystem->OnActionEvent.Remove(Listening);

	TestEqual(TEXT("the creature 2 metres from the body loses what that hit took from the control"),
		TwoBefore - HealthNow(Two), ByControl, ByControl * 0.005f);
	TestEqual(TEXT("and so does the one 4 metres from it"), FourBefore - HealthNow(Four), ByControl,
		ByControl * 0.005f);
	TestEqual(TEXT("the one 7 metres from it loses nothing"), SevenBefore - HealthNow(Seven), 0.0f, 0.001f);

	TestTrue(TEXT("the creature 2 metres away now carries the wearer's poison"), Carries(Two, Poison));
	TestTrue(TEXT("and the one 4 metres away"), Carries(Four, Poison));
	TestFalse(TEXT("the one 7 metres away does not"), Carries(Seven, Poison));
	TestFalse(TEXT("the other character's burn is not passed on by the wearer's row"), Carries(Two, Burn));
	UCataclysmSkillEffects::FRunningAilment Copy;
	if (TestTrue(TEXT("the copy is a running poison"), UCataclysmSkillEffects::RunningAilmentOn(Two, Poison, Copy)))
	{
		TestEqual(TEXT("for the seven and a half seconds the original had left"), Copy.SecondsLeft, 7.5f, 0.1f);
		TestTrue(TEXT("with the wearer as its source"), Copy.Applier.Get() == Wearer.Actor);
	}
	TestEqual(TEXT("a copy is not the wearer applying a damage over time: dot_applied was not raised"),
		ApplicationsAnnounced, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBlastChainOfThreeTest,
	"Cataclysm.Enchantments.ThreeDeathsInARowEachBlastForAShareOfTheirOwnMaximumHealthAndStrikeEachEnemyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Five creatures in a row at 0, 3, 6, 7.5 and 9 metres. The first has 100,000
 * maximum health, the second 1,000 and the third 100; all three carry the
 * wearer's poison. The first dies. Its blast of 20,000 reaches only the second
 * and kills it. THE SECOND'S DEATH IS HEARD WHILE THE WEARER IS ACTING ON THE
 * FIRST, so it waits, and then blasts for a fifth of 1,000: that reaches the
 * third, which dies, and the witness at 7.5 metres. The third's death waits in
 * its turn and blasts for a fifth of 100: that reaches the witness and the
 * fourth creature at 9 metres. So the witness loses one hit of 200 and one of
 * 20, the fourth one hit of 20, and each carries the poison of the death that
 * reached it.
 */
bool FCataclysmBlastChainOfThreeTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmBlastFromTheDyingTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);
	const FGameplayTag Poison = Ailment(TEXT("Keyword.DoT.Poison"));
	ACataclysmEnemyCharacter* Control = Beside(World, -8.0f);
	ACataclysmEnemyCharacter* First = Beside(World, 0.0f);
	ACataclysmEnemyCharacter* Second = Beside(World, 3.0f);
	ACataclysmEnemyCharacter* Third = Beside(World, 6.0f);
	ACataclysmEnemyCharacter* Witness = Beside(World, 7.5f);
	ACataclysmEnemyCharacter* Fourth = Beside(World, 9.0f);
	if (!TestTrue(TEXT("set-up: the poison tag is registered"), Poison.IsValid())
		|| !TestNotNull(TEXT("set-up: the control creature, 8 metres the other side"), Control)
		|| !TestNotNull(TEXT("set-up: the first creature"), First)
		|| !TestNotNull(TEXT("set-up: the second creature, 3 metres on"), Second)
		|| !TestNotNull(TEXT("set-up: the third creature, 6 metres on"), Third)
		|| !TestNotNull(TEXT("set-up: the witness, 7.5 metres on"), Witness)
		|| !TestNotNull(TEXT("set-up: the fourth creature, 9 metres on"), Fourth))
	{
		return false;
	}
	Second->SetHealth(1000.0f);
	Third->SetHealth(100.0f);
	if (!TestEqual(TEXT("set-up: the first creature's maximum health"), MaximumHealthOf(First), 100000.0f, 0.5f)
		|| !TestEqual(TEXT("set-up: the second creature's maximum health"), MaximumHealthOf(Second), 1000.0f, 0.5f)
		|| !TestEqual(TEXT("set-up: the third creature's maximum health"), MaximumHealthOf(Third), 100.0f, 0.5f)
		|| !TestTrue(TEXT("set-up: the wearer poisons the first"), Ail(Wearer.Actor, First, Poison))
		|| !TestTrue(TEXT("set-up: the wearer poisons the second"), Ail(Wearer.Actor, Second, Poison))
		|| !TestTrue(TEXT("set-up: the wearer poisons the third"), Ail(Wearer.Actor, Third, Poison)))
	{
		return false;
	}
	Wearer.AbilitySystem->SetPoolActions({Blasts(20.0f)});
	const float ByTwoHundred = TakenByABlastOf(Wearer.Actor, Control, 200.0f);
	const float ByTwenty = TakenByABlastOf(Wearer.Actor, Control, 20.0f);
	if (!TestTrue(TEXT("set-up: a direct area hit of 200 takes at least the third creature's 100"),
				  ByTwoHundred >= 100.0f)
		|| !TestTrue(TEXT("set-up: a direct area hit of 20 takes something"), ByTwenty > 0.0f))
	{
		return false;
	}

	const TWeakObjectPtr<ACataclysmEnemyCharacter> SecondKept(Second);
	const TWeakObjectPtr<ACataclysmEnemyCharacter> ThirdKept(Third);
	const float WitnessBefore = HealthNow(Witness);
	const float FourthBefore = HealthNow(Fourth);
	First->HandleDeath();

	TestTrue(TEXT("the first blast killed the second creature"),
		!SecondKept.IsValid() || UCataclysmSkillEffects::IsDead(SecondKept.Get()));
	TestTrue(TEXT("the second's blast killed the third creature"),
		!ThirdKept.IsValid() || UCataclysmSkillEffects::IsDead(ThirdKept.Get()));
	TestFalse(TEXT("the witness is alive"), UCataclysmSkillEffects::IsDead(Witness));
	TestFalse(TEXT("the fourth creature is alive"), UCataclysmSkillEffects::IsDead(Fourth));
	TestEqual(TEXT("the witness loses one hit of a fifth of the second's maximum and one of a fifth of the "
				   "third's, each once, and nothing of the first blast"),
		WitnessBefore - HealthNow(Witness), ByTwoHundred + ByTwenty, (ByTwoHundred + ByTwenty) * 0.005f);
	TestEqual(TEXT("the fourth loses one hit of a fifth of the THIRD's maximum, once, and nothing of the two "
				   "blasts it stood outside"),
		FourthBefore - HealthNow(Fourth), ByTwenty, ByTwenty * 0.005f);
	TestTrue(TEXT("the witness carries the wearer's poison, from the second's death"), Carries(Witness, Poison));
	TestTrue(TEXT("the fourth carries the wearer's poison, from the third's death"), Carries(Fourth, Poison));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBlastChainsTest,
	"Cataclysm.Enchantments.AnEnemyTheBlastKillsBlastsInItsTurnForAShareOfItsOwnMaximumHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Three creatures in a row, 3 metres apart, so the third is 6 metres from the
 * first and out of its reach. The first has 100,000 maximum health and the
 * second 1,000; both carry the wearer's poison, each from an application of the
 * wearer's own. The first dies: its blast of a fifth of 100,000 kills the
 * second, whose death is heard in its turn, and ITS blast is a fifth of 1,000.
 * The third loses what a direct area hit of 200 takes from a creature built
 * alike, once, and nothing of the first blast; and it receives the second's
 * poison.
 */
bool FCataclysmBlastChainsTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmBlastFromTheDyingTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);
	const FGameplayTag Poison = Ailment(TEXT("Keyword.DoT.Poison"));
	ACataclysmEnemyCharacter* Control = Beside(World, -8.0f);
	ACataclysmEnemyCharacter* First = Beside(World, 0.0f);
	ACataclysmEnemyCharacter* Second = Beside(World, 3.0f);
	ACataclysmEnemyCharacter* Third = Beside(World, 6.0f);
	if (!TestTrue(TEXT("set-up: the poison tag is registered"), Poison.IsValid())
		|| !TestNotNull(TEXT("set-up: the control creature, 8 metres the other side"), Control)
		|| !TestNotNull(TEXT("set-up: the first creature"), First)
		|| !TestNotNull(TEXT("set-up: the second creature, 3 metres on"), Second)
		|| !TestNotNull(TEXT("set-up: the third creature, 6 metres on"), Third))
	{
		return false;
	}
	Second->SetHealth(1000.0f);
	if (!TestEqual(TEXT("set-up: the first creature's maximum health"), MaximumHealthOf(First), 100000.0f, 0.5f)
		|| !TestEqual(TEXT("set-up: the second creature's maximum health"), MaximumHealthOf(Second), 1000.0f, 0.5f)
		|| !TestTrue(TEXT("set-up: the wearer poisons the first and the second"),
					 Ail(Wearer.Actor, First, Poison) && Ail(Wearer.Actor, Second, Poison)))
	{
		return false;
	}
	Wearer.AbilitySystem->SetPoolActions({Blasts(20.0f)});
	const float ByControl = TakenByABlastOf(Wearer.Actor, Control, 200.0f);
	if (!TestTrue(TEXT("set-up: a direct area hit of 200 takes something from a creature built alike"),
				  ByControl > 0.0f))
	{
		return false;
	}

	const TWeakObjectPtr<ACataclysmEnemyCharacter> SecondKept(Second);
	const float ThirdBefore = HealthNow(Third);
	First->HandleDeath();

	TestTrue(TEXT("the first blast killed the second creature"),
		!SecondKept.IsValid() || UCataclysmSkillEffects::IsDead(SecondKept.Get()));
	TestFalse(TEXT("the third creature is alive"), UCataclysmSkillEffects::IsDead(Third));
	TestEqual(TEXT("the third loses what a hit of a fifth of the SECOND's maximum takes, once, and nothing of the "
				   "first blast, which it stood outside"),
		ThirdBefore - HealthNow(Third), ByControl, ByControl * 0.005f);
	TestTrue(TEXT("and it receives the second's poison"), Carries(Third, Poison));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlagueDoctorTenRowTest,
	"Cataclysm.Enchantments.PlagueDoctorsTenPiecesBlastFromAnEnemyThatDiesCarryingTheWearersAilment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Plague Doctor (10-Piece Bonus): When an enemy dies while affected by a DoT
 * from you, it explodes and applies all of your DoTs to all nearby enemies".
 * Issue #1833, ruled 2026-10-07: `blast_from_the_dying` on `afflicted_death`,
 * 20, a judged number. NINE PIECES AND THEN TEN of the real set. At nine a
 * creature that dies carrying the wearer's poison takes nothing from its
 * neighbour; at ten the neighbour loses what a direct area hit of a fifth of
 * the dead creature's maximum health takes from a creature built alike, and
 * carries the poison.
 */
bool FCataclysmPlagueDoctorTenRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmHealthThresholdRowTest;
	using namespace CataclysmBlastFromTheDyingTest;
	const TCHAR* Bonus = TEXT("Positive_Plague_Doctor_2_Piece_Bonus_Your_DoT_effects");
	const FGameplayTag Poison = Ailment(TEXT("Keyword.DoT.Poison"));
	if (!TestTrue(TEXT("set-up: the poison tag is registered"), Poison.IsValid()))
	{
		return false;
	}

	for (const int32 Pieces : {9, 10})
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		FWearer Wearer(World);
		WearSet(Wearer, Bonus, Pieces);
		ACataclysmEnemyCharacter* Control = Beside(World, -8.0f);
		ACataclysmEnemyCharacter* Dying = Beside(World, 0.0f);
		ACataclysmEnemyCharacter* Near = Beside(World, 2.0f);
		if (!TestNotNull(TEXT("set-up: the control creature, 8 metres the other side"), Control)
			|| !TestNotNull(TEXT("set-up: the creature that will die"), Dying)
			|| !TestNotNull(TEXT("set-up: the creature 2 metres from it"), Near)
			|| !TestTrue(TEXT("set-up: the wearer poisons the one that will die"), Ail(Wearer.Actor, Dying, Poison)))
		{
			return false;
		}
		const float ByControl = TakenByABlastOf(Wearer.Actor, Control, MaximumHealthOf(Dying) * 0.2f);
		if (!TestTrue(TEXT("set-up: a direct area hit of a fifth of that maximum takes something"), ByControl > 0.0f))
		{
			return false;
		}
		const float Before = HealthNow(Near);
		Dying->HandleDeath();
		if (Pieces == 9)
		{
			TestEqual(TEXT("nine pieces: the death takes nothing from the creature beside it"),
				Before - HealthNow(Near), 0.0f, 0.001f);
			TestFalse(TEXT("nine pieces: and passes no poison on"), Carries(Near, Poison));
		}
		else
		{
			TestEqual(*(FString(TEXT("ten pieces: the creature beside the body loses what that hit took from the "
									 "control.")) + CataclysmRepeatRowsTest::OlderAsset),
				Before - HealthNow(Near), ByControl, ByControl * 0.005f);
			TestTrue(TEXT("ten pieces: and carries the wearer's poison"), Carries(Near, Poison));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKilledEnemiesExplodeOnYouRowTest,
	"Cataclysm.Enchantments.TheKilledEnemiesExplodeOnYouRowTakesItsShareOfTheSlainEnemysMaximumHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Enemies you kill explode and deal 5%-10% of their maximum HP as damage to
 * you". Issue #1833, ruled 2026-10-07: `health` at -5 to -10 per cent of
 * `event_amount` on `kill`, whose amount is the slain enemy's maximum health,
 * with a stated trigger cooldown of nought. Nothing is done to enemies. WORN at
 * the top of its roll, 10, by a wearer with 1000 health: a kill of an enemy
 * with 2000 maximum health takes 200, a second in the same moment takes 200
 * more, and a kill whose share is past all the health left leaves 1. A DRAIN
 * CANNOT KILL, ruled 2026-09-14.
 *
 * THE EVENT IS RAISED BY HAND with the amount the player character passes from
 * a real kill; `AKillIsHeardWithTheSlainEnemysMaximumHealthAsItsAmount` holds
 * that the real kill passes it.
 */
bool FCataclysmKilledEnemiesExplodeOnYouRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	FWorn Worn(TEXT("Negative_Enemies_you_kill_explode_and_deal_5_10_of_thei"), false);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	Worn.ASC()->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
	Worn.ASC()->SetNumericAttributeBase(Health, 1000.0f);
	const FName Kill(TEXT("kill"));

	Worn.ASC()->ActOnEvent(Kill, nullptr, 0.0f, /*bLanded=*/true, nullptr);
	TestEqual(TEXT("a kill that carries no amount takes nothing"),
		Worn.ASC()->GetNumericAttribute(Health), 1000.0f, 0.01f);
	Worn.ASC()->ActOnEvent(Kill, nullptr, 2000.0f, /*bLanded=*/true, nullptr);
	TestEqual(TEXT("a kill of an enemy with 2000 maximum health takes 10% of that. If nothing, "
				   "DT_EnchantmentEffects may be older than the rows: run tools/generate_datatable_assets.py"),
		Worn.ASC()->GetNumericAttribute(Health), 800.0f, 0.01f);
	Worn.ASC()->ActOnEvent(Kill, nullptr, 2000.0f, /*bLanded=*/true, nullptr);
	TestEqual(TEXT("a second in the same moment takes 200 more"),
		Worn.ASC()->GetNumericAttribute(Health), 600.0f, 0.01f);
	Worn.ASC()->ActOnEvent(Kill, nullptr, 1000000.0f, /*bLanded=*/true, nullptr);
	TestEqual(TEXT("and a kill whose share is past all the health left leaves 1"),
		Worn.ASC()->GetNumericAttribute(Health), 1.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpreadOnApplicationTest,
	"Cataclysm.Enchantments.AnAilmentAppliedPassesToTheNearestEnemiesARowCountsAndACopyPassesNoFurther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Burn effects you apply spread to 1-2 nearby enemies", with a row made by
 * hand. Issue #1833, ruled 2026-10-06. With no row a burn goes to its target and
 * nobody else. With a count of 2 hung on burn, a burn applied to a creature
 * also goes to the two nearest within 5 metres of it, 1 and 2 metres away, and
 * not to the ones 3 and 6 metres away: A COPY SPREADS NO FURTHER, or the one 3
 * metres away would have received it from a copy. A copy is the same damage a
 * second for the same ten seconds and raises no `dot_applied`. Applied again,
 * the two that carry it are passed over and the one 3 metres away receives it.
 */
bool FCataclysmSpreadOnApplicationTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmSpreadOnDeathTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);
	const FGameplayTag Burn = Ailment(TEXT("Keyword.DoT.Burn"));
	ACataclysmEnemyCharacter* Lone = Beside(World, -8.0f);
	ACataclysmEnemyCharacter* BesideLone = Beside(World, -7.0f);
	ACataclysmEnemyCharacter* Struck = Beside(World, 0.0f);
	ACataclysmEnemyCharacter* One = Beside(World, 1.0f);
	ACataclysmEnemyCharacter* Two = Beside(World, 2.0f);
	ACataclysmEnemyCharacter* Three = Beside(World, 3.0f);
	ACataclysmEnemyCharacter* Six = Beside(World, 6.0f);
	if (!TestTrue(TEXT("set-up: the burn tag is registered"), Burn.IsValid())
		|| !TestNotNull(TEXT("set-up: the creature burned with no row"), Lone)
		|| !TestNotNull(TEXT("set-up: the creature 1 metre from that one"), BesideLone)
		|| !TestNotNull(TEXT("set-up: the creature burned with the row"), Struck)
		|| !TestNotNull(TEXT("set-up: the creature 1 metre from it"), One)
		|| !TestNotNull(TEXT("set-up: the creature 2 metres from it"), Two)
		|| !TestNotNull(TEXT("set-up: the creature 3 metres from it"), Three)
		|| !TestNotNull(TEXT("set-up: the creature 6 metres from it"), Six))
	{
		return false;
	}

	// WITH NO ROW, a burn goes to its target and nobody else.
	if (!TestTrue(TEXT("set-up: with no row, the wearer burns a creature"), Ail(Wearer.Actor, Lone, Burn)))
	{
		return false;
	}
	TestFalse(TEXT("with no row, the creature 1 metre from it is not burned"), Carries(BesideLone, Burn));

	FCataclysmPoolAction Row;
	Row.Rider = ECataclysmAilmentRider::SpreadOnApplication;
	Row.Ailment = Burn;
	Row.Percent = 2.0f;
	Wearer.AbilitySystem->SetPoolActions({Row});
	int32 ApplicationsAnnounced = 0;
	const FDelegateHandle Listening = Wearer.AbilitySystem->OnActionEvent.AddLambda(
		[&ApplicationsAnnounced](FName Event)
		{
			ApplicationsAnnounced += Event == FName(TEXT("dot_applied")) ? 1 : 0;
		});
	const bool bApplied = Ail(Wearer.Actor, Struck, Burn);
	Wearer.AbilitySystem->OnActionEvent.Remove(Listening);
	if (!TestTrue(TEXT("with the row, the wearer burns a creature"), bApplied))
	{
		return false;
	}

	TestTrue(TEXT("the creature 1 metre from it is burned too"), Carries(One, Burn));
	TestTrue(TEXT("and the one 2 metres from it"), Carries(Two, Burn));
	TestFalse(TEXT("the one 3 metres from it is not: the count is two, and a copy passes no further"),
		Carries(Three, Burn));
	TestFalse(TEXT("nor the one 6 metres from it"), Carries(Six, Burn));
	TestEqual(TEXT("one application was announced, the wearer's own; the two copies announced none"),
		ApplicationsAnnounced, 1);
	UCataclysmSkillEffects::FRunningAilment Copy;
	if (TestTrue(TEXT("the copy is a running burn"), UCataclysmSkillEffects::RunningAilmentOn(One, Burn, Copy)))
	{
		TestEqual(TEXT("at the one point a second the wearer's burn deals"), Copy.DamagePerSecond, 1.0f, 0.001f);
		TestEqual(TEXT("for the same ten seconds"), Copy.SecondsLeft, 10.0f, 0.1f);
		TestTrue(TEXT("with the wearer as its source"), Copy.Applier.Get() == Wearer.Actor);
	}

	// APPLIED AGAIN: those that carry it are passed over, so the next nearest receives it.
	TestTrue(TEXT("the wearer burns the same creature again"), Ail(Wearer.Actor, Struck, Burn));
	TestTrue(TEXT("the one 3 metres from it is burned now, the nearest that did not carry it"), Carries(Three, Burn));
	TestFalse(TEXT("the one 6 metres from it is still out of reach"), Carries(Six, Burn));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBurnSpreadRowTest,
	"Cataclysm.Enchantments.TheBurnSpreadRowPassesABurnTheWearerAppliesToTheTwoNearestWithinFiveMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Burn effects you apply spread to 1-2 nearby enemies". Issue #1833, ruled
 * 2026-10-06: `ailment_spread_on_application` on Burn, 1 to 2. The real row
 * WORN at the top of its roll, 2: a burn the wearer applies to a creature also
 * goes to the creatures 1 and 2 metres from it, and not to the ones 3 and 6
 * metres from it.
 */
bool FCataclysmBurnSpreadRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	using namespace CataclysmSpreadOnDeathTest;
	// THE NAME WORN IS LOOKED UP IN THE TABLE FIRST. This test first ran with
	// the name one letter short: the wearer wore an enchantment no table holds,
	// nothing spread, and the failure read exactly as an asset older than the
	// row does. Found by the window's whole suite, 2026-10-07.
	const TCHAR* const RowName = TEXT("Positive_Burn_effects_you_apply_spread_to_1_2_nearby_enem");
	const UDataTable* Positive =
		CataclysmEnchantmentEffectTest::LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	if (!TestNotNull(TEXT("set-up: EnchantmentsPositive.csv can be read"), Positive)
		|| !TestTrue(TEXT("set-up: the name this test wears is a row of EnchantmentsPositive.csv"),
					 Positive->GetRowMap().Contains(FName(RowName))))
	{
		return false;
	}
	FWorn Worn(RowName, true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const FGameplayTag Burn = Ailment(TEXT("Keyword.DoT.Burn"));
	ACataclysmEnemyCharacter* Struck = Beside(Worn.World, 0.0f);
	ACataclysmEnemyCharacter* One = Beside(Worn.World, 1.0f);
	ACataclysmEnemyCharacter* Two = Beside(Worn.World, 2.0f);
	ACataclysmEnemyCharacter* Three = Beside(Worn.World, 3.0f);
	ACataclysmEnemyCharacter* Six = Beside(Worn.World, 6.0f);
	if (!TestTrue(TEXT("set-up: the burn tag is registered"), Burn.IsValid())
		|| !TestNotNull(TEXT("set-up: the creature the wearer burns"), Struck)
		|| !TestNotNull(TEXT("set-up: the creature 1 metre from it"), One)
		|| !TestNotNull(TEXT("set-up: the creature 2 metres from it"), Two)
		|| !TestNotNull(TEXT("set-up: the creature 3 metres from it"), Three)
		|| !TestNotNull(TEXT("set-up: the creature 6 metres from it"), Six)
		|| !TestTrue(TEXT("set-up: the wearer burns the creature"), Ail(Worn.Wearer->Actor, Struck, Burn)))
	{
		return false;
	}

	TestTrue(*(FString(TEXT("the creature 1 metre from it is burned too.")) + CataclysmRepeatRowsTest::OlderAsset),
		Carries(One, Burn));
	TestTrue(TEXT("and the one 2 metres from it"), Carries(Two, Burn));
	TestFalse(TEXT("the one 3 metres from it is not: the row's count is two at the top of its roll"),
		Carries(Three, Burn));
	TestFalse(TEXT("nor the one 6 metres from it"), Carries(Six, Burn));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBlockValueRowTest,
	"Cataclysm.Enchantments.TheBlockValueRowPaysTheAttackerAllOfWhatABlockRemoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "100% of your block value is added to your retaliation damage". Issue #1833,
 * ruled 2026-10-07: "block value" is what the block removed from that blow, and
 * the row is `reflect_blocked` on `block` at 100. WORN: a real blocked blow
 * costs the attacker exactly what the block removed, as the top roll of
 * "Reflect 20%-100% of damage blocked back at attackers" does.
 */
bool FCataclysmBlockValueRowTest::RunTest(const FString&)
{
	using namespace CataclysmBlockRowTest;
	using namespace CataclysmApplyStatusRowTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
	FBlockFight Fight(World, TEXT("Positive_100_of_your_block_value_is_added_to_your_retali"));
	WriteLines(Fight.Wearer.AbilitySystem, {}, {{BlockChance, 100.0f}});
	Fight.Attacker.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
	Fight.Attacker.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);

	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const float Before = Fight.Attacker.AbilitySystem->GetNumericAttribute(Health);
	FCataclysmDamageResult Result;
	UCataclysmSkillEffects::ApplyHit(Fight.Attacker.Actor, Fight.Wearer.Actor, 100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery(), &Result);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the blow was blocked and removed %.1f"), Result.DamageBlocked),
			Result.bBlocked && Result.DamageBlocked > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the attacker lost all of what the block removed. If nothing, DT_EnchantmentEffects may be "
				   "older than the rows: run tools/generate_datatable_assets.py"),
		Before - Fight.Attacker.AbilitySystem->GetNumericAttribute(Health), Result.DamageBlocked, 0.5f);
	return true;
}

// TWELVE ROWS ON THE DUNGEON SESSION'S STACK OF 2026-10-07. Issue #1833. Each is WORN and read as far as the row
// goes: a stat row by the figure the game asks for, an action row by the action it hands its wearer. What each
// stat and action then does is those entries' tests, with a line or a row made by hand.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearerDamageOverTimeRowsTest,
	"Cataclysm.Enchantments.TheSixStatRowsOnDamageOverTimeAndZonesOnTheWearerEachGiveTheFigureTheGameAsksFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Six stat rows. The two that raise a figure are read against a figure of
 * 100, with the bleed tag and with the burn tag, since each is scoped to bleed:
 * "Bleed effects applied to you deal 30%-50% increased damage" makes 150 of a
 * bleed and leaves a burn at 100, and "Bleeding on you lasts 50%-100% longer"
 * makes 200. A drawback is worn at its harshest roll. The four flat rows read
 * their own figure: 1, 20, 1 and 1, the bleed immunity only with the bleed tag.
 */
bool FCataclysmWearerDamageOverTimeRowsTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	const FGameplayTagContainer Bleed = CataclysmRepeatRowsTest::Tagged(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTagContainer Burn = CataclysmRepeatRowsTest::Tagged(TEXT("Keyword.DoT.Burn"));
	if (!TestTrue(TEXT("set-up: the bleed tag exists"), Bleed.Num() == 1)
		|| !TestTrue(TEXT("set-up: the burn tag exists"), Burn.Num() == 1))
	{
		return false;
	}

	struct FRaised
	{
		const TCHAR* Row;
		const TCHAR* Stat;
		float OfABleed;
	};
	const FRaised Raised[] = {
		{TEXT("Negative_Bleed_effects_applied_to_you_deal_30_50_increa"), TEXT("damage_over_time_taken"), 150.0f},
		{TEXT("Negative_Bleeding_on_you_lasts_50_100_longer"), TEXT("debuff_duration_taken"), 200.0f},
	};
	for (const FRaised& Case : Raised)
	{
		FWorn Worn(Case.Row, false);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		const FName Stat(Case.Stat);
		TestEqual(FString::Printf(TEXT("%s, worn: 100 of a bleed's %s becomes %.0f.%s"), Case.Row, Case.Stat,
					  Case.OfABleed, CataclysmRepeatRowsTest::OlderAsset),
			Worn.ASC()->StatAppliedTo(Stat, Bleed, 100.0f), Case.OfABleed, 0.01f);
		TestEqual(FString::Printf(TEXT("%s: a burn's stays 100"), Case.Row),
			Worn.ASC()->StatAppliedTo(Stat, Burn, 100.0f), 100.0f, 0.01f);
	}

	struct FFlat
	{
		const TCHAR* Row;
		bool bBenefit;
		const TCHAR* Stat;
		/** Whether the row is scoped to bleed, so it is read with the bleed tag and reads nought with the burn tag. */
		bool bBleedOnly;
		float Worn;
	};
	const FFlat Flats[] = {
		{TEXT("Positive_Unaffected_by_bleeding"), true, TEXT("ailment_immunity"), true, 1.0f},
		{TEXT("Positive_10_20_of_bleed_damage_you_take_is_taken_from_y"), true,
		 TEXT("bleed_damage_taken_from_energy_shield"), false, 20.0f},
		{TEXT("Positive_DoTs_deal_damage_to_your_mana_pool_first"), true,
		 TEXT("damage_over_time_taken_from_mana_first"), false, 1.0f},
		{TEXT("Negative_Your_persistent_AOE_zones_apply_their_effects_to"), false,
		 TEXT("zone_applies_effects_to_owner"), false, 1.0f},
	};
	for (const FFlat& Case : Flats)
	{
		FWorn Worn(Case.Row, Case.bBenefit);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		const FName Stat(Case.Stat);
		const FGameplayTagContainer Asked = Case.bBleedOnly ? Bleed : FGameplayTagContainer();
		TestEqual(FString::Printf(TEXT("%s, worn: %s is %.0f.%s"), Case.Row, Case.Stat, Case.Worn,
					  CataclysmRepeatRowsTest::OlderAsset),
			Worn.ASC()->StatForSkill(Stat, Asked, 0.0f), Case.Worn, 0.01f);
		if (Case.bBleedOnly)
		{
			TestEqual(FString::Printf(TEXT("%s: asked about a burn, %s is 0"), Case.Row, Case.Stat),
				Worn.ASC()->StatForSkill(Stat, Burn, 0.0f), 0.0f, 0.01f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearerStatusRowsTest,
	"Cataclysm.Enchantments.TheSixActionRowsOnAStatusOnTheWearerAndACooldownAbilityEachHandOverTheirAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Six action rows. Each is WORN and its wearer's actions are read: exactly one
 * of the kind the row states, on the event it states. The five that lay a
 * status on the wearer are marked as such; the timed one states fifteen seconds;
 * the cooldown ability row is the one that may deal increased damage and is
 * kept to a skill with a cooldown.
 */
bool FCataclysmWearerStatusRowsTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	struct FCase
	{
		const TCHAR* Row;
		bool bBenefit;
		const TCHAR* Event;
		/** Seconds between two, for the timed row; nought for the others. */
		float EverySeconds;
		bool bOnTheWearer;
	};
	const FCase Cases[] = {
		{TEXT("Negative_Taking_a_hit_has_a_15_25_chance_to_trigger_a_r"), false, TEXT("hit_taken"), 0.0f, true},
		{TEXT("Negative_Critical_strikes_have_a_20_35_chance_to_trigge"), false, TEXT("critical_strike"), 0.0f, true},
		{TEXT("Negative_Every_15_seconds_a_random_debuff_is_applied_to_y"), false, TEXT("every_seconds"), 15.0f, true},
		{TEXT("Negative_After_using_a_charge_skill_you_are_briefly_stunn"), false, TEXT("skill_end"), 0.0f, true},
		{TEXT("Negative_When_you_apply_a_DOT_1_4_stacks_are_applied_to"), false, TEXT("dot_applied"), 0.0f, true},
		{TEXT("Positive_Your_cooldown_abilities_have_a_5_20_chance_to"), true, TEXT("skill_use"), 0.0f, false},
	};
	for (const FCase& Case : Cases)
	{
		FWorn Worn(Case.Row, Case.bBenefit);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		int32 OfItsKind = 0;
		float Seconds = -1.0f;
		for (const FCataclysmPoolAction& Action : Worn.ASC()->GetPoolActions())
		{
			const bool bItsKind = Case.bOnTheWearer
				? Action.bStatusOnTheWearer
				: Action.bUseDealsIncreasedDamage && Action.bOnlyASkillWithACooldown;
			if (bItsKind && Action.Event == FName(Case.Event))
			{
				++OfItsKind;
				Seconds = Action.EverySeconds;
			}
		}
		TestEqual(FString::Printf(TEXT("%s, worn: one action of its kind on %s.%s"), Case.Row, Case.Event,
					  CataclysmRepeatRowsTest::OlderAsset),
			OfItsKind, 1);
		if (Case.EverySeconds > 0.0f)
		{
			TestEqual(FString::Printf(TEXT("%s: it comes every %.0f seconds"), Case.Row, Case.EverySeconds), Seconds,
				Case.EverySeconds, 0.01f);
		}

		Worn.Wearer->Equipment->UnequipEverything();
		Worn.Wearer->Equipment->RefreshAttributes(Worn.ASC());
		int32 Left = 0;
		for (const FCataclysmPoolAction& Action : Worn.ASC()->GetPoolActions())
		{
			Left += Action.Event == FName(Case.Event) ? 1 : 0;
		}
		TestEqual(FString::Printf(TEXT("%s, taken off: no action on %s is left"), Case.Row, Case.Event), Left, 0);
	}
	return true;
}

// A LEECH ROW SCOPED TO AN AILMENT READS THAT AILMENT'S TICKS. Issue #1833, ruled 2026-10-07, for "Bleed damage you
// deal also leeches 10%-20% of its value as HP". A tick's asset tags carry the skill's tags and the bare
// `Keyword.DoT`; the ailment is a GRANTED tag, so until this change a leech row scoped to `Keyword.DoT.Bleed` was
// discarded on every tick.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLeechScopedToAnAilmentTest,
	"Cataclysm.Enchantments.ALeechRowScopedToAnAilmentLeechesFromThatAilmentsTicksAndNoOthers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A wearer with 10% life leech scoped to bleed, made by hand as a row would
 * give it. It burns a creature: two and a half seconds of burn ticks take
 * health from the creature and start no leech payment. It then bleeds the same
 * creature: two and a half seconds on, at least one payment has started.
 */
bool FCataclysmLeechScopedToAnAilmentTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmSpreadOnDeathTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);
	const FGameplayTag Bleed = Ailment(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTag Burn = Ailment(TEXT("Keyword.DoT.Burn"));
	ACataclysmEnemyCharacter* Target = Beside(World, 0.0f);
	if (!TestTrue(TEXT("set-up: the bleed tag is registered"), Bleed.IsValid())
		|| !TestTrue(TEXT("set-up: the burn tag is registered"), Burn.IsValid())
		|| !TestNotNull(TEXT("set-up: the creature"), Target))
	{
		return false;
	}

	FCataclysmStatModifier Scoped;
	Scoped.Bucket = ECataclysmStatBucket::Flat;
	Scoped.Source = ECataclysmModifierSource::PassiveKeystone;
	Scoped.Value = 10.0f;
	Scoped.RequiredTags = FGameplayTagContainer(Bleed);
	TMap<FName, FCataclysmStatInputs> Inputs;
	FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(TEXT("life_leech")));
	Line.Base = 0.0f;
	Line.Modifiers = {Scoped};
	Wearer.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
	// ROOM TO BE HEALED, maximum first: the vital set clamps health to it.
	Wearer.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
	Wearer.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 5000.0f);

	const UCataclysmAbilitySystemComponent* Its = SystemOf(Target);
	const auto Health = [Its]()
	{
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : 0.0f;
	};

	// A BURN FIRST. TWO AND A HALF SECONDS, BETWEEN TICKS.
	const float BeforeTheBurn = Health();
	if (!TestTrue(TEXT("set-up: the wearer burns the creature"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Wearer.Actor, Target, 100.0f, 10.0f, Burn,
														/*bScalesWithInstigator=*/false)))
	{
		return false;
	}
	CataclysmTestWorld::RunClock(World, 2.5f);
	if (!TestTrue(TEXT("set-up: the burn's ticks took health from the creature"), Health() < BeforeTheBurn))
	{
		return false;
	}
	TestEqual(TEXT("a burn's ticks start no payment for a row scoped to bleed"),
		Wearer.AbilitySystem->GetLeechPayments().Num(), 0);

	// THEN A BLEED ON THE SAME CREATURE.
	if (!TestTrue(TEXT("set-up: the wearer bleeds the creature"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Wearer.Actor, Target, 100.0f, 10.0f, Bleed,
														/*bScalesWithInstigator=*/false)))
	{
		return false;
	}
	CataclysmTestWorld::RunClock(World, 2.5f);
	TestTrue(TEXT("a bleed's ticks start a payment"), Wearer.AbilitySystem->GetLeechPayments().Num() > 0);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBleedLeechRowTest,
	"Cataclysm.Enchantments.TheBleedLeechRowLeechesFromABleedsTicksAndNotFromABurns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Bleed damage you deal also leeches 10%-20% of its value as HP". Issue #1833,
 * ruled 2026-10-07: `life_leech` flat 10 to 20, scoped to `Keyword.DoT.Bleed`.
 * The real row WORN at its best roll, and the stat read as the leech code asks
 * it: with the bleed tag it is 20 above what it is with the burn tag, and with
 * the burn tag it is what it is with no tag.
 *
 * A DIFFERENCE AND NOT A FIGURE, because the wearer this helper builds has a
 * life leech of its own: its equipment refresh applies the starting class's
 * lines, and the Ravager's include one. The first form of this test ran ticks and
 * asserted that a burn's started no payment; the window's first run printed
 * "Expected 'a burn's ticks start no payment' to be 0, but it was 2". That a
 * tick of a bleed reaches the leech code with its ailment is the test of the
 * layer below, on a wearer whose every stat line is set by hand.
 */
bool FCataclysmBleedLeechRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	// THE NAME WORN IS LOOKED UP IN THE TABLE FIRST, so a name that is not a row fails here and says so.
	const TCHAR* const RowName = TEXT("Positive_Bleed_damage_you_deal_also_leeches_10_20_of_it");
	const UDataTable* Positive =
		CataclysmEnchantmentEffectTest::LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	if (!TestNotNull(TEXT("set-up: EnchantmentsPositive.csv can be read"), Positive)
		|| !TestTrue(TEXT("set-up: the name this test wears is a row of EnchantmentsPositive.csv"),
					 Positive->GetRowMap().Contains(FName(RowName))))
	{
		return false;
	}
	const FGameplayTagContainer Bleed = CataclysmRepeatRowsTest::Tagged(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTagContainer Burn = CataclysmRepeatRowsTest::Tagged(TEXT("Keyword.DoT.Burn"));
	if (!TestTrue(TEXT("set-up: the bleed tag exists"), Bleed.Num() == 1)
		|| !TestTrue(TEXT("set-up: the burn tag exists"), Burn.Num() == 1))
	{
		return false;
	}
	FWorn Worn(RowName, true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const FName Stat(TEXT("life_leech"));
	const float OfABleed = Worn.ASC()->StatForSkill(Stat, Bleed, 0.0f);
	const float OfABurn = Worn.ASC()->StatForSkill(Stat, Burn, 0.0f);
	const float OfNoAilment = Worn.ASC()->StatForSkill(Stat, FGameplayTagContainer(), 0.0f);
	TestEqual(*(FString(TEXT("worn: life leech asked about a bleed is 20 above life leech asked about a burn.")) +
				CataclysmRepeatRowsTest::OlderAsset),
		OfABleed - OfABurn, 20.0f, 0.01f);
	TestEqual(TEXT("asked about a burn it is what it is asked about no ailment"), OfABurn, OfNoAilment, 0.01f);

	Worn.Wearer->Equipment->UnequipEverything();
	Worn.Wearer->Equipment->RefreshAttributes(Worn.ASC());
	TestEqual(TEXT("taken off: a bleed and a burn are asked the same"),
		Worn.ASC()->StatForSkill(Stat, Bleed, 0.0f), Worn.ASC()->StatForSkill(Stat, Burn, 0.0f), 0.01f);
	return true;
}
// HOW MANY POOLS A CHARACTER IS LEECHING INTO IS A SCALE. Issue #1833, ruled 2026-10-07, for "Starvation (6-Piece
// Bonus): You gain 5% damage reduction for each active unique instance of leech". ONE PER POOL: health, mana and
// energy shield, so at most three. Two payments into one pool are one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLeechPoolsScaleTest,
	"Cataclysm.Enchantments.TheLeechPoolsScaleCountsEachPoolBeingLeechedIntoOnceAndAtMostThree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A wearer with a hand-made damage reduction of 5 for each pool being leeched
 * into. With no payment it reads 0; with two payments into health, 5; with
 * health and mana, 10; with all three and a second into health, 15. A payment
 * with nothing left to pay is not counted. ONLY A POOL THE WEARER HAS COUNTS:
 * with no energy shield, payments into all three read 10, and a full pool
 * still counts.
 */
bool FCataclysmLeechPoolsScaleTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	ECataclysmStatScale Named = ECataclysmStatScale::Fixed;
	if (!TestTrue(TEXT("set-up: leech_pools_in_flight is a scale this build knows"),
			UCataclysmStatPipeline::ScaleNamed(TEXT("leech_pools_in_flight"), Named)
				&& Named == ECataclysmStatScale::PerLeechPoolInFlight))
	{
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);

	FCataclysmStatModifier PerPool;
	PerPool.Bucket = ECataclysmStatBucket::Flat;
	PerPool.Source = ECataclysmModifierSource::PassiveKeystone;
	PerPool.Value = 5.0f;
	PerPool.Scale = ECataclysmStatScale::PerLeechPoolInFlight;
	PerPool.ScaleStep = 1.0f;
	TMap<FName, FCataclysmStatInputs> Inputs;
	FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(TEXT("damage_reduction")));
	Line.Base = 0.0f;
	Line.Modifiers = {PerPool};
	Wearer.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
	// THE THREE POOLS, each with a maximum above nought, health and mana full.
	const auto SetPool = [&Wearer](const FGameplayAttribute& Maximum, float Value)
	{
		Wearer.AbilitySystem->SetNumericAttributeBase(Maximum, Value);
	};
	SetPool(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
	SetPool(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
	SetPool(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 100.0f);
	SetPool(UCataclysmVitalAttributeSet::GetManaAttribute(), 100.0f);
	SetPool(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 100.0f);
	if (!TestTrue(TEXT("set-up: the wearer has all three pools"),
			Wearer.AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) > 0.0f
				&& Wearer.AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxManaAttribute()) > 0.0f
				&& Wearer.AbilitySystem->GetNumericAttribute(
					   UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()) > 0.0f))
	{
		return false;
	}

	const auto Payment = [](ECataclysmLeechPool Pool, float Remaining)
	{
		FCataclysmLeechPayment One;
		One.Pool = Pool;
		One.Remaining = Remaining;
		One.SecondsLeft = 3.0f;
		return One;
	};
	const auto Reduction = [&Wearer]()
	{
		return Wearer.AbilitySystem->StatForSkill(FName(TEXT("damage_reduction")), FGameplayTagContainer(), 0.0f);
	};

	TestEqual(TEXT("no payment in flight: nothing"), Reduction(), 0.0f, 0.001f);

	Wearer.AbilitySystem->SetLeechPayments(TArray<FCataclysmLeechPayment>{Payment(ECataclysmLeechPool::Health, 10.0f), Payment(ECataclysmLeechPool::Health, 20.0f)});
	TestEqual(TEXT("two payments into health are one pool: 5"), Reduction(), 5.0f, 0.001f);

	Wearer.AbilitySystem->SetLeechPayments(TArray<FCataclysmLeechPayment>{Payment(ECataclysmLeechPool::Health, 10.0f), Payment(ECataclysmLeechPool::Mana, 10.0f)});
	TestEqual(TEXT("health and mana are two pools: 10"), Reduction(), 10.0f, 0.001f);

	Wearer.AbilitySystem->SetLeechPayments(TArray<FCataclysmLeechPayment>{Payment(ECataclysmLeechPool::Health, 10.0f), Payment(ECataclysmLeechPool::Mana, 10.0f),
		 Payment(ECataclysmLeechPool::EnergyShield, 10.0f), Payment(ECataclysmLeechPool::Health, 5.0f)});
	TestEqual(TEXT("all three pools, one of them twice: 15"), Reduction(), 15.0f, 0.001f);

	Wearer.AbilitySystem->SetLeechPayments(TArray<FCataclysmLeechPayment>{Payment(ECataclysmLeechPool::Health, 10.0f), Payment(ECataclysmLeechPool::Mana, 0.0f)});
	TestEqual(TEXT("a payment with nothing left to pay is not counted: 5"), Reduction(), 5.0f, 0.001f);

	// A WEARER WITH NO ENERGY SHIELD: a payment into it is not a pool being leeched into.
	SetPool(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 0.0f);
	if (!TestEqual(TEXT("set-up: the wearer now has no energy shield"),
			Wearer.AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()),
			0.0f, 0.001f))
	{
		return false;
	}
	Wearer.AbilitySystem->SetLeechPayments(TArray<FCataclysmLeechPayment>{
		Payment(ECataclysmLeechPool::Health, 10.0f), Payment(ECataclysmLeechPool::Mana, 10.0f),
		 Payment(ECataclysmLeechPool::EnergyShield, 10.0f)});
	TestEqual(TEXT("payments into all three, and no energy shield: 10, and the full pools still count"),
		Reduction(), 10.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStarvationSixRowTest,
	"Cataclysm.Enchantments.StarvationsSixPiecesGiveFiveDamageReductionForEachPoolBeingLeechedInto",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The real row from the built table. Five pieces give nothing; six give 5 for
 * each pool the wearer has and is leeching into: 0 with no payment, 5 with one
 * into health, 10 with one into each of the three and no energy shield, 15
 * with one into each of the three and an energy shield.
 */
bool FCataclysmStarvationSixRowTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;
	using namespace CataclysmHealthThresholdRowTest;

	// A SET IS WORN BY ITS FIRST BONUS'S NAME. BOTH NAMES ARE LOOKED UP IN THE TABLE FIRST, so a name that is not a
	// row fails here and says so.
	const TCHAR* const Bonus = TEXT("Positive_Starvation_2_Piece_Bonus_You_have_5_life_m");
	const TCHAR* const SixPieces = TEXT("Positive_Starvation_6_Piece_Bonus_You_gain_5_damage_r");
	const UDataTable* Positive = LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	if (!TestNotNull(TEXT("set-up: EnchantmentsPositive.csv can be read"), Positive)
		|| !TestTrue(TEXT("set-up: the set's first bonus, which this test wears, is a row of the table"),
					 Positive->GetRowMap().Contains(FName(Bonus)))
		|| !TestTrue(TEXT("set-up: the six-piece bonus is a row of the table"),
					 Positive->GetRowMap().Contains(FName(SixPieces))))
	{
		return false;
	}

	const auto Payment = [](ECataclysmLeechPool Pool)
	{
		FCataclysmLeechPayment One;
		One.Pool = Pool;
		One.Remaining = 10.0f;
		One.SecondsLeft = 3.0f;
		return One;
	};
	for (const int32 Pieces : {5, 6})
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		FWearer Wearer(World);
		WearSet(Wearer, Bonus, Pieces);
		// THE POOLS ARE SET AFTER THE SET IS WORN, since wearing refreshes the attributes. Health and mana, and
		// no energy shield until the last reading.
		const auto SetPool = [&Wearer](const FGameplayAttribute& Maximum, float Value)
		{
			Wearer.AbilitySystem->SetNumericAttributeBase(Maximum, Value);
		};
		const auto PoolIs = [&Wearer](const FGameplayAttribute& Maximum)
		{
			return Wearer.AbilitySystem->GetNumericAttribute(Maximum);
		};
		SetPool(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
		SetPool(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 100.0f);
		SetPool(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 0.0f);
		if (!TestTrue(TEXT("set-up: the wearer has health and mana and no energy shield"),
				PoolIs(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) > 0.0f
					&& PoolIs(UCataclysmVitalAttributeSet::GetMaxManaAttribute()) > 0.0f
					&& PoolIs(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()) <= 0.0f))
		{
			return false;
		}
		const auto Reduction = [&Wearer]()
		{
			return Wearer.AbilitySystem->StatForSkill(FName(TEXT("damage_reduction")), FGameplayTagContainer(),
													 0.0f);
		};
		const float AtSix = Pieces == 6 ? 1.0f : 0.0f;

		// EVERY FIGURE IS READ AS A DIFFERENCE from this wearer's own reading with no payment in flight. The
		// wearer this helper builds is of the starting class, and the Ravager's own lines include damage
		// reduction, so an absolute figure would be the class's and the row's together.
		const float WithNoPayment = Reduction();
		Wearer.AbilitySystem->SetLeechPayments(TArray<FCataclysmLeechPayment>{Payment(ECataclysmLeechPool::Health)});
		TestEqual(FString::Printf(TEXT("%d pieces, leeching into health, above the reading with no payment.%s"),
					  Pieces, CataclysmRepeatRowsTest::OlderAsset),
			Reduction() - WithNoPayment, 5.0f * AtSix, 0.001f);
		Wearer.AbilitySystem->SetLeechPayments(TArray<FCataclysmLeechPayment>{Payment(ECataclysmLeechPool::Health),
			Payment(ECataclysmLeechPool::Mana), Payment(ECataclysmLeechPool::EnergyShield)});
		TestEqual(FString::Printf(TEXT("%d pieces, payments into all three and no energy shield"), Pieces),
			Reduction() - WithNoPayment, 10.0f * AtSix, 0.001f);
		SetPool(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 100.0f);
		if (!TestTrue(TEXT("set-up: the wearer now has an energy shield"),
				PoolIs(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()) > 0.0f))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("%d pieces, payments into all three and an energy shield"), Pieces),
			Reduction() - WithNoPayment, 15.0f * AtSix, 0.001f);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAbsorbedDamageRowsTest,
	"Cataclysm.Enchantments.TheTwoAbsorbedDamageRowsEachGiveTheCapTheGameAsksFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Damage absorbed by your energy shield is converted to bonus damage on your
 * next attack" and "Absorbed spell damage is converted to bonus damage on your
 * next attack". Issue #1833, ruled 2026-10-07: each is a cap of 100 on its own
 * store, a stat with no attribute. Each real row is WORN and its stat read:
 * 100 worn, and nought when taken off. What the two stores then do is the
 * tests of the entry that built them.
 */
bool FCataclysmAbsorbedDamageRowsTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	struct FCase
	{
		const TCHAR* Row;
		const TCHAR* Stat;
	};
	const FCase Cases[] = {
		{TEXT("Positive_Damage_absorbed_by_your_energy_shield_is_convert"),
		 TEXT("shield_absorbed_damage_added_to_next_attack_cap_percent")},
		{TEXT("Positive_Absorbed_spell_damage_is_converted_to_bonus_dama"),
		 TEXT("spell_absorbed_damage_added_to_next_attack_cap_percent")},
	};
	// EVERY NAME WORN IS LOOKED UP IN THE TABLE FIRST, so a name that is not a row fails here and says so.
	const UDataTable* Positive =
		CataclysmEnchantmentEffectTest::LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	if (!TestNotNull(TEXT("set-up: EnchantmentsPositive.csv can be read"), Positive))
	{
		return false;
	}
	for (const FCase& Case : Cases)
	{
		if (!TestTrue(FString::Printf(TEXT("set-up: %s is a row of EnchantmentsPositive.csv"), Case.Row),
				Positive->GetRowMap().Contains(FName(Case.Row))))
		{
			return false;
		}
		FWorn Worn(Case.Row, true);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		const FName Stat(Case.Stat);
		TestEqual(FString::Printf(TEXT("%s, worn: %s is 100.%s"), Case.Row, Case.Stat,
					  CataclysmRepeatRowsTest::OlderAsset),
			Worn.ASC()->StatForSkill(Stat, FGameplayTagContainer(), 0.0f), 100.0f, 0.01f);

		Worn.Wearer->Equipment->UnequipEverything();
		Worn.Wearer->Equipment->RefreshAttributes(Worn.ASC());
		TestEqual(FString::Printf(TEXT("%s, taken off: %s is 0"), Case.Row, Case.Stat),
			Worn.ASC()->StatForSkill(Stat, FGameplayTagContainer(), 0.0f), 0.0f, 0.01f);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverkillExplosionRowTest,
	"Cataclysm.Enchantments.TheOverkillExplosionRowHandsItsWearerTheWholeOfTheOverkillOnAKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Enemies killed by you explode for the overkill amount". Issue #1833, ruled
 * 2026-10-07: `explode_victim_for_overkill` on `kill`, 100. The real row WORN:
 * its wearer holds exactly one action of that kind on `kill`, with a share of
 * 100, and none when the item is taken off. What the explosion then does is
 * the tests of the entries that built it.
 */
bool FCataclysmOverkillExplosionRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	// THE NAME WORN IS LOOKED UP IN THE TABLE FIRST, so a name that is not a row fails here and says so.
	const TCHAR* const RowName = TEXT("Positive_Enemies_killed_by_you_explode_for_the_overkill_a");
	const UDataTable* Positive =
		CataclysmEnchantmentEffectTest::LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	if (!TestNotNull(TEXT("set-up: EnchantmentsPositive.csv can be read"), Positive)
		|| !TestTrue(TEXT("set-up: the name this test wears is a row of EnchantmentsPositive.csv"),
					 Positive->GetRowMap().Contains(FName(RowName))))
	{
		return false;
	}
	FWorn Worn(RowName, true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const FName Kill(TEXT("kill"));
	int32 OfItsKind = 0;
	float Share = -1.0f;
	for (const FCataclysmPoolAction& Action : Worn.ASC()->GetPoolActions())
	{
		if (Action.bExplodeVictimForOverkill && Action.Event == Kill)
		{
			++OfItsKind;
			Share = Action.Percent;
		}
	}
	TestEqual(*(FString(TEXT("worn: one action that explodes the victim for its overkill, on a kill.")) +
				CataclysmRepeatRowsTest::OlderAsset),
		OfItsKind, 1);
	TestEqual(TEXT("and its share is the whole of the overkill, 100"), Share, 100.0f, 0.01f);

	Worn.Wearer->Equipment->UnequipEverything();
	Worn.Wearer->Equipment->RefreshAttributes(Worn.ASC());
	int32 Left = 0;
	for (const FCataclysmPoolAction& Action : Worn.ASC()->GetPoolActions())
	{
		Left += Action.bExplodeVictimForOverkill ? 1 : 0;
	}
	TestEqual(TEXT("taken off: no such action is left"), Left, 0);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDotDurationOnTheWearerRowTest,
	"Cataclysm.Enchantments.TheDotDurationRowMultipliesHowLongAnyDamageOverTimeLastsOnItsWearer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "DoTs on you have 100%-300% more duration", the owner's reword of 2026-10-07
 * of "DoTs last 2x-4x as long on you": `debuff_duration_taken` more 100 to
 * 300, scoped to `Keyword.DoT`. The real row WORN at its harshest roll, as a
 * drawback is: a duration of 100 asked about a bleed becomes 400, and so does
 * one asked about a burn; asked about nothing that is damage over time it
 * stays 100.
 */
bool FCataclysmDotDurationOnTheWearerRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	// THE NAME WORN IS LOOKED UP IN THE TABLE FIRST, so a name that is not a row fails here and says so. The name
	// is the one the reword gave the row, written here by the script that derived it.
	const TCHAR* const RowName = TEXT("Negative_DoTs_on_you_have_100_300_more_duration");
	const UDataTable* Negative =
		CataclysmEnchantmentEffectTest::LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
	if (!TestNotNull(TEXT("set-up: EnchantmentsNegative.csv can be read"), Negative)
		|| !TestTrue(TEXT("set-up: the name this test wears is a row of EnchantmentsNegative.csv"),
					 Negative->GetRowMap().Contains(FName(RowName))))
	{
		return false;
	}
	const FGameplayTagContainer Bleed = CataclysmRepeatRowsTest::Tagged(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTagContainer Burn = CataclysmRepeatRowsTest::Tagged(TEXT("Keyword.DoT.Burn"));
	if (!TestTrue(TEXT("set-up: the bleed tag exists"), Bleed.Num() == 1)
		|| !TestTrue(TEXT("set-up: the burn tag exists"), Burn.Num() == 1))
	{
		return false;
	}
	FWorn Worn(RowName, false);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const FName Stat(TEXT("debuff_duration_taken"));
	TestEqual(*(FString(TEXT("worn: 100 of a bleed's duration becomes 400.")) + CataclysmRepeatRowsTest::OlderAsset),
		Worn.ASC()->StatAppliedTo(Stat, Bleed, 100.0f), 400.0f, 0.01f);
	TestEqual(TEXT("and 100 of a burn's becomes 400"), Worn.ASC()->StatAppliedTo(Stat, Burn, 100.0f), 400.0f, 0.01f);
	TestEqual(TEXT("asked about nothing that is damage over time, it stays 100"),
		Worn.ASC()->StatAppliedTo(Stat, FGameplayTagContainer(), 100.0f), 100.0f, 0.01f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClassResourceGenerationRowsTest,
	"Cataclysm.Enchantments.TheTwoClassResourceGenerationRowsRaiseAndLowerTheRateTheGameAsksFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your class resource generates 20%-40% faster" and "Your class resource
 * generates 30%-50% slower". Issue #1833, ruled 2026-10-07:
 * `class_resource_generation` increased 20 to 40, and increased -30 to -50.
 * Each real row is WORN, the benefit at its best roll and the drawback at its
 * harshest, and the stat is applied to a rate of 100: 140 for the first and 50
 * for the second. What the rate then does to a gain is the tests of the entry
 * that built the stat.
 */
bool FCataclysmClassResourceGenerationRowsTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	struct FCase
	{
		const TCHAR* Table;
		const TCHAR* Row;
		bool bBenefit;
		float OfAHundred;
	};
	const FCase Cases[] = {
		{TEXT("EnchantmentsPositive.csv"), TEXT("Positive_Your_class_resource_generates_20_40_faster"), true, 140.0f},
		{TEXT("EnchantmentsNegative.csv"), TEXT("Negative_Your_class_resource_generates_30_50_slower"), false, 50.0f},
	};
	const FName Stat(TEXT("class_resource_generation"));
	for (const FCase& Case : Cases)
	{
		// THE NAME WORN IS LOOKED UP IN ITS TABLE FIRST, so a name that is not a row fails here and says so.
		const UDataTable* Table = CataclysmEnchantmentEffectTest::LoadCsv<FCataclysmEnchantmentRow>(Case.Table);
		if (!TestNotNull(FString::Printf(TEXT("set-up: %s can be read"), Case.Table), Table)
			|| !TestTrue(FString::Printf(TEXT("set-up: %s is a row of %s"), Case.Row, Case.Table),
						 Table->GetRowMap().Contains(FName(Case.Row))))
		{
			return false;
		}
		FWorn Worn(Case.Row, Case.bBenefit);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("%s, worn: a rate of 100 becomes %.0f.%s"), Case.Row, Case.OfAHundred,
					  CataclysmRepeatRowsTest::OlderAsset),
			Worn.ASC()->StatAppliedTo(Stat, FGameplayTagContainer(), 100.0f), Case.OfAHundred, 0.01f);

		Worn.Wearer->Equipment->UnequipEverything();
		Worn.Wearer->Equipment->RefreshAttributes(Worn.ASC());
		TestEqual(FString::Printf(TEXT("%s, taken off: a rate of 100 stays 100"), Case.Row),
			Worn.ASC()->StatAppliedTo(Stat, FGameplayTagContainer(), 100.0f), 100.0f, 0.01f);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDotsTickFasterWhileMovingRowTest,
	"Cataclysm.Enchantments.TheTickTwiceAsFastWhileMovingRowDoublesDamageOverTimeTakenOnlyWhileItsWearerMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "DoTs on you tick twice as fast while moving". Issue #1833, ruled 2026-10-06
 * and 2026-10-07: `damage_over_time_taken` more 100 under `while_moving`. The
 * real row WORN: a figure of 100 is 200 while the wearer moves, and 100 before
 * it has moved and after it stands again. Moving and standing are set with the
 * two calls the movement sampler makes.
 */
bool FCataclysmDotsTickFasterWhileMovingRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	// THE NAME WORN IS LOOKED UP IN THE TABLE FIRST, so a name that is not a row fails here and says so.
	const TCHAR* const RowName = TEXT("Negative_DoTs_on_you_tick_twice_as_fast_while_moving");
	const UDataTable* Negative =
		CataclysmEnchantmentEffectTest::LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
	if (!TestNotNull(TEXT("set-up: EnchantmentsNegative.csv can be read"), Negative)
		|| !TestTrue(TEXT("set-up: the name this test wears is a row of EnchantmentsNegative.csv"),
					 Negative->GetRowMap().Contains(FName(RowName))))
	{
		return false;
	}
	FWorn Worn(RowName, false);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const FName Stat(TEXT("damage_over_time_taken"));
	const FGameplayTagContainer Burn = CataclysmRepeatRowsTest::Tagged(TEXT("Keyword.DoT.Burn"));
	if (!TestTrue(TEXT("set-up: the burn tag exists"), Burn.Num() == 1))
	{
		return false;
	}
	Worn.ASC()->NoteDidNotMove();
	TestEqual(TEXT("standing: 100 of a burn's damage stays 100"),
		Worn.ASC()->StatAppliedTo(Stat, Burn, 100.0f), 100.0f, 0.01f);
	Worn.ASC()->NoteMovedMetres(1.0f);
	TestEqual(*(FString(TEXT("moving: 100 becomes 200.")) + CataclysmRepeatRowsTest::OlderAsset),
		Worn.ASC()->StatAppliedTo(Stat, Burn, 100.0f), 200.0f, 0.01f);
	Worn.ASC()->NoteDidNotMove();
	TestEqual(TEXT("standing again: 100 stays 100"),
		Worn.ASC()->StatAppliedTo(Stat, Burn, 100.0f), 100.0f, 0.01f);
	return true;
}
namespace CataclysmTrapLayerTest
{
	/**
	 * `Type.Trap`. Ruled 2026-10-07. No minion type row carried it on that day, so
	 * each test below gives it to a machine by hand.
	 */
	FGameplayTag TheTrapTag()
	{
		return FGameplayTag::RequestGameplayTag(
			FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false);
	}

	/** A spike trap of this summoner's, carrying `Type.Trap` by hand. */
	ACataclysmMinion* TrapOf(CataclysmDeployableTest::FSummoner& Summoner)
	{
		ACataclysmMinion* Made = Summoner.Make(TEXT("SpikeTrap"));
		if (Made)
		{
			Made->TypeTags.AddTag(TheTrapTag());
		}
		return Made;
	}

	/**
	 * A spike trap of this summoner's with `Type.Trap` TAKEN OFF by hand: the
	 * control that is a machine and no trap. Since issue #2284 the minion types
	 * table gives every summoned Spike Trap the tag, so a control that only
	 * summoned one stopped being a control.
	 */
	ACataclysmMinion* SpikeTrapWithNoTrapTagOf(CataclysmDeployableTest::FSummoner& Summoner)
	{
		ACataclysmMinion* Made = Summoner.Make(TEXT("SpikeTrap"));
		if (Made)
		{
			Made->TypeTags.RemoveTag(TheTrapTag());
		}
		return Made;
	}

	/** One modifier requiring `Type.Trap`, with a step of 1, as a row of this layer would be written. */
	FCataclysmStatModifier TrapRow(ECataclysmStatBucket Bucket, float Value,
								   ECataclysmStatScale Scale, float Offset = 0.0f)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = Bucket;
		Row.Source = ECataclysmModifierSource::Enchantment;
		Row.Value = Value;
		Row.Scale = Scale;
		Row.ScaleStep = 1.0f;
		Row.ScaleOffset = Offset;
		Row.RequiredTags.AddTag(TheTrapTag());
		return Row;
	}

	/** Gives this summoner one stat line, `minion_damage`, holding this one row and nothing else. */
	void GiveMinionDamageRow(CataclysmDeployableTest::FSummoner& Summoner,
							 const FCataclysmStatModifier& Row)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		FCataclysmStatInputs& Line = Lines.FindOrAdd(FName(TEXT("minion_damage")));
		Line.Base = 0.0f;
		Line.Modifiers = {Row};
		Summoner.ASC()->SetStatInputs(MoveTemp(Lines));
	}

	/**
	 * Creatures to strike, one at a time. Each has the armour asked for and no
	 * evasion, block or resistance, is struck once, and is destroyed before the
	 * next is made, so no two ever stand on one spot.
	 */
	struct FTrapTestRange
	{
		explicit FTrapTestRange(UWorld* InWorld) : World(InWorld) {}

		ACataclysmEnemyCharacter* Creature(float Armour) const
		{
			ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
				FVector(-2000.0f, 150.0f, 0.0f), FRotator::ZeroRotator);
			if (!Made)
			{
				return nullptr;
			}
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(10000.0f);
			Made->SetArmour(Armour);
			UAbilitySystemComponent* Its = Made->GetAbilitySystemComponent();
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), Armour);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Its->SetNumericAttributeBase(
				UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
			return Made;
		}

		static float HealthOf(const ACataclysmEnemyCharacter* Of)
		{
			return Of->GetAbilitySystemComponent()->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		/** The health one blow from this machine takes off a fresh creature with this armour. */
		float BlowFrom(ACataclysmMinion* Machine, float Armour) const
		{
			ACataclysmEnemyCharacter* Struck = Creature(Armour);
			if (!Machine || !Struck)
			{
				return -1.0f;
			}
			const float Before = HealthOf(Struck);
			Machine->AttackTarget(Struck);
			const float Taken = Before - HealthOf(Struck);
			Struck->Destroy();
			return Taken;
		}

		/**
		 * The control: the health a plain blow of this size takes off a fresh
		 * creature with this armour, struck by a character whose armour
		 * penetration ATTRIBUTE is this much and whose blow may penetrate. It
		 * cannot critically strike, carries no weapon, and leeches nothing, as a
		 * machine's blow does not.
		 */
		float PlainBlow(float Damage, float ArmourPenetration, float Armour) const
		{
			ACataclysmEnemyCharacter* Struck = Creature(Armour);
			if (!Struck)
			{
				return -1.0f;
			}
			CataclysmEnchantmentEffectTest::FWearer Striker(World);
			Striker.AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetArmorPenetrationAttribute(), ArmourPenetration);
			FCataclysmHitDelivery Plain;
			Plain.bCannotCriticallyStrike = true;
			Plain.bCarriesNoWeaponSubType = true;
			Plain.bCannotLeech = true;
			Plain.bCarriesNoAilmentChance = true;
			const float Before = HealthOf(Struck);
			UCataclysmSkillEffects::ApplyDirectDamage(Striker.Actor, Struck, Damage, Plain);
			const float Taken = Before - HealthOf(Struck);
			Struck->Destroy();
			return Taken;
		}

		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTrapAndGadgetScaleArithmeticTest,
	"Cataclysm.Enchantments.TheTrapScaleCountsPastAnOffsetAndTheGadgetScaleLeavesTrapsOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `traps_active` and `gadgets_active`, ruled 2026-10-07, in the pipeline alone.
 * A state of three traps and two gadgets. `traps_active` counts three, and two
 * with an offset of 1, which is how "for each other trap" is written. One trap
 * with that offset is nothing, and so is none: never a negative count.
 * `gadgets_active` counts two and `deployables_active` still counts five. A
 * "more" row of 100 per gadget gives one sum: three gadgets make a base of 100
 * into 400, which is four times and not the eight that doubling three times
 * over would give. An offset is valid on `traps_active` and is still refused on
 * `gadgets_active`.
 */
bool FCataclysmTrapAndGadgetScaleArithmeticTest::RunTest(const FString&)
{
	using namespace CataclysmTrapLayerTest;
	using FPipeline = UCataclysmStatPipeline;
	if (!TestTrue(TEXT("the trap tag exists"), TheTrapTag().IsValid()))
	{
		return false;
	}

	ECataclysmStatScale Read = ECataclysmStatScale::Fixed;
	TestTrue(TEXT("a row may name traps_active"),
		FPipeline::ScaleNamed(TEXT("traps_active"), Read) && Read == ECataclysmStatScale::PerTrapActive);
	Read = ECataclysmStatScale::Fixed;
	TestTrue(TEXT("a row may name gadgets_active"),
		FPipeline::ScaleNamed(TEXT("gadgets_active"), Read) && Read == ECataclysmStatScale::PerGadgetActive);

	FCataclysmStatConditions Field;
	Field.TrapsActive = 3;
	Field.GadgetsActive = 2;
	Field.DeployablesActive = 5;

	const FCataclysmStatModifier PerTrap =
		TrapRow(ECataclysmStatBucket::Increased, 30.0f, ECataclysmStatScale::PerTrapActive);
	const FCataclysmStatModifier PerOtherTrap =
		TrapRow(ECataclysmStatBucket::Increased, 30.0f, ECataclysmStatScale::PerTrapActive, /*Offset=*/1.0f);
	const FCataclysmStatModifier PerGadget =
		TrapRow(ECataclysmStatBucket::Increased, 20.0f, ECataclysmStatScale::PerGadgetActive);
	const FCataclysmStatModifier PerMachine =
		TrapRow(ECataclysmStatBucket::Increased, 10.0f, ECataclysmStatScale::PerDeployableActive);

	TestEqual(TEXT("three traps, no offset: three steps of 30"),
		FPipeline::ScaledValue(PerTrap, Field), 90.0f, 0.001f);
	TestEqual(TEXT("three traps, an offset of 1: two others, two steps of 30"),
		FPipeline::ScaledValue(PerOtherTrap, Field), 60.0f, 0.001f);
	TestEqual(TEXT("two gadgets: two steps of 20, the three traps left out"),
		FPipeline::ScaledValue(PerGadget, Field), 40.0f, 0.001f);
	TestEqual(TEXT("deployables_active is unchanged: all five machines, five steps of 10"),
		FPipeline::ScaledValue(PerMachine, Field), 50.0f, 0.001f);

	FCataclysmStatConditions OneTrap = Field;
	OneTrap.TrapsActive = 1;
	TestEqual(TEXT("one trap, an offset of 1: no other trap, nothing"),
		FPipeline::ScaledValue(PerOtherTrap, OneTrap), 0.0f, 0.001f);
	TestEqual(TEXT("and with no offset that one trap is one step"),
		FPipeline::ScaledValue(PerTrap, OneTrap), 30.0f, 0.001f);
	FCataclysmStatConditions NoTrap = Field;
	NoTrap.TrapsActive = 0;
	TestEqual(TEXT("no trap, an offset of 1: nothing, not a negative number of steps"),
		FPipeline::ScaledValue(PerOtherTrap, NoTrap), 0.0f, 0.001f);

	// "DOUBLES", RULED ADDITIVE ON 2026-10-07. One "more" row of 100 per gadget
	// is one sum, so three gadgets are four times. The control is the same row
	// with one gadget, which is twice.
	const FCataclysmStatModifier Doubles =
		TrapRow(ECataclysmStatBucket::More, 100.0f, ECataclysmStatScale::PerGadgetActive);
	const FGameplayTagContainer AsATrap(TheTrapTag());
	FCataclysmStatConditions Gadgets;
	Gadgets.GadgetsActive = 1;
	TestEqual(TEXT("one gadget doubles: a base of 100 is 200"),
		FPipeline::Evaluate(100.0f, {Doubles}, AsATrap, Gadgets).Final, 200.0f, 0.01f);
	Gadgets.GadgetsActive = 3;
	TestEqual(TEXT("three gadgets are four times and not eight: a base of 100 is 400"),
		FPipeline::Evaluate(100.0f, {Doubles}, AsATrap, Gadgets).Final, 400.0f, 0.01f);
	Gadgets.GadgetsActive = 0;
	TestEqual(TEXT("no gadget changes nothing: a base of 100 is 100"),
		FPipeline::Evaluate(100.0f, {Doubles}, AsATrap, Gadgets).Final, 100.0f, 0.01f);

	TestTrue(TEXT("an offset on traps_active is valid"),
		FPipeline::ValidateModifier(PerOtherTrap).IsEmpty());
	FCataclysmStatModifier OffsetGadget = PerGadget;
	OffsetGadget.ScaleOffset = 1.0f;
	TestFalse(TEXT("an offset on gadgets_active is still refused"),
		FPipeline::ValidateModifier(OffsetGadget).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTrapAndGadgetScalesOnABlowTest,
	"Cataclysm.Enchantments.TheTrapAndGadgetScalesCountTheSummonersOwnMachinesOnATrapsBlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The two scales on a real machine's blow. Ruled 2026-10-07. Each summoner
 * carries one `minion_damage` row requiring `Type.Trap`, written by hand because
 * no data row names either scale yet, and a spike trap given `Type.Trap` by
 * hand strikes a creature with no armour. Every figure is a ratio against the
 * blow of the same trap made by a summoner with no row.
 *
 * "For each other trap", 30 increased with an offset of 1: one trap alone is
 * unchanged, two are 1.3 times, three are 1.6 times. A ballista and a spike trap
 * with no trap tag are not traps and add nothing.
 * "Each active gadget", 20 increased: a ballista makes it 1.2 times, a second
 * trap adds nothing, a bolt turret makes it 1.4.
 * "Doubles", 100 more per gadget: one ballista twice, two three times, three
 * four times.
 *
 * THE COUNT IS THE SUMMONER'S. The plain summoner commands machines of its own
 * throughout, and each other summoner's first trap is still unchanged.
 */
bool FCataclysmTrapAndGadgetScalesOnABlowTest::RunTest(const FString&)
{
	using namespace CataclysmTrapLayerTest;
	CataclysmDeployableTest::FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World)
		|| !TestTrue(TEXT("the trap tag exists"), TheTrapTag().IsValid()))
	{
		return false;
	}
	const FTrapTestRange Range(Scope.World);

	CataclysmDeployableTest::FSummoner Plain(Scope.World, nullptr);
	ACataclysmMinion* PlainTrap = TrapOf(Plain);
	const float PlainBlow = Range.BlowFrom(PlainTrap, /*Armour=*/0.0f);
	if (!TestTrue(TEXT("a plain summoner's trap lands a blow"), PlainBlow > 0.0f))
	{
		return false;
	}
	const float PlainBallistaBlow = Range.BlowFrom(Plain.Make(TEXT("Ballista")), /*Armour=*/0.0f);
	SpikeTrapWithNoTrapTagOf(Plain);

	// WHAT THE STATE HOLDS, on the summoner that commands a tagged trap, a
	// ballista and a spike trap with no trap tag.
	const FCataclysmStatConditions PlainState = Plain.ASC()->CurrentConditions();
	TestEqual(TEXT("three machines commanded"), PlainState.DeployablesActive, 3);
	TestEqual(TEXT("one of them is a trap: the one carrying Type.Trap"), PlainState.TrapsActive, 1);
	TestEqual(TEXT("two are gadgets: the ballista and the spike trap with no trap tag"),
		PlainState.GadgetsActive, 2);

	{
		CataclysmDeployableTest::FSummoner EachOther(Scope.World, nullptr);
		EachOther.Along = 10000.0f;
		GiveMinionDamageRow(EachOther, TrapRow(ECataclysmStatBucket::Increased, 30.0f,
			ECataclysmStatScale::PerTrapActive, /*Offset=*/1.0f));
		ACataclysmMinion* First = TrapOf(EachOther);
		TestEqual(TEXT("each other trap: one trap alone is unchanged, whatever the plain summoner commands"),
			Range.BlowFrom(First, 0.0f) / PlainBlow, 1.0f, 0.001f);
		TrapOf(EachOther);
		TestEqual(TEXT("each other trap: two traps, one other, 1.3 times"),
			Range.BlowFrom(First, 0.0f) / PlainBlow, 1.3f, 0.001f);
		ACataclysmMinion* ItsBallista = EachOther.Make(TEXT("Ballista"));
		SpikeTrapWithNoTrapTagOf(EachOther);
		TestEqual(TEXT("each other trap: a ballista and a spike trap with no trap tag are no traps, still 1.3"),
			Range.BlowFrom(First, 0.0f) / PlainBlow, 1.3f, 0.001f);
		TrapOf(EachOther);
		TestEqual(TEXT("each other trap: three traps, two others, 1.6 times"),
			Range.BlowFrom(First, 0.0f) / PlainBlow, 1.6f, 0.001f);
		if (TestTrue(TEXT("a plain ballista lands a blow"), PlainBallistaBlow > 0.0f))
		{
			TestEqual(TEXT("each other trap: the row requires Type.Trap, so the ballista's own blow is unchanged"),
				Range.BlowFrom(ItsBallista, 0.0f) / PlainBallistaBlow, 1.0f, 0.001f);
		}
	}
	{
		CataclysmDeployableTest::FSummoner PerGadget(Scope.World, nullptr);
		PerGadget.Along = 20000.0f;
		GiveMinionDamageRow(PerGadget, TrapRow(ECataclysmStatBucket::Increased, 20.0f,
			ECataclysmStatScale::PerGadgetActive));
		ACataclysmMinion* ItsTrap = TrapOf(PerGadget);
		TestEqual(TEXT("each gadget: a trap with no gadget is unchanged"),
			Range.BlowFrom(ItsTrap, 0.0f) / PlainBlow, 1.0f, 0.001f);
		PerGadget.Make(TEXT("Ballista"));
		TestEqual(TEXT("each gadget: one ballista, 1.2 times"),
			Range.BlowFrom(ItsTrap, 0.0f) / PlainBlow, 1.2f, 0.001f);
		TrapOf(PerGadget);
		TestEqual(TEXT("each gadget: a second trap is not a gadget, still 1.2"),
			Range.BlowFrom(ItsTrap, 0.0f) / PlainBlow, 1.2f, 0.001f);
		PerGadget.Make(TEXT("BoltTurret"));
		TestEqual(TEXT("each gadget: a bolt turret as well, 1.4 times"),
			Range.BlowFrom(ItsTrap, 0.0f) / PlainBlow, 1.4f, 0.001f);
	}
	{
		CataclysmDeployableTest::FSummoner Doubling(Scope.World, nullptr);
		Doubling.Along = 30000.0f;
		GiveMinionDamageRow(Doubling, TrapRow(ECataclysmStatBucket::More, 100.0f,
			ECataclysmStatScale::PerGadgetActive));
		ACataclysmMinion* ItsTrap = TrapOf(Doubling);
		TestEqual(TEXT("doubles: a trap with no gadget is unchanged"),
			Range.BlowFrom(ItsTrap, 0.0f) / PlainBlow, 1.0f, 0.001f);
		Doubling.Make(TEXT("Ballista"));
		TestEqual(TEXT("doubles: one gadget, twice"),
			Range.BlowFrom(ItsTrap, 0.0f) / PlainBlow, 2.0f, 0.001f);
		Doubling.Make(TEXT("Ballista"));
		TestEqual(TEXT("doubles: two gadgets, three times and not four"),
			Range.BlowFrom(ItsTrap, 0.0f) / PlainBlow, 3.0f, 0.001f);
		Doubling.Make(TEXT("BoltTurret"));
		TestEqual(TEXT("doubles: three gadgets, four times and not eight"),
			Range.BlowFrom(ItsTrap, 0.0f) / PlainBlow, 4.0f, 0.001f);
		const FCataclysmStatConditions ItsState = Doubling.ASC()->CurrentConditions();
		TestEqual(TEXT("doubles: that summoner commands three gadgets"), ItsState.GadgetsActive, 3);
		TestEqual(TEXT("and one trap"), ItsState.TrapsActive, 1);
		TestEqual(TEXT("which deployables_active still counts together: four"), ItsState.DeployablesActive, 4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTrapArmourRowTest,
	"Cataclysm.Enchantments.ATrapsBlowIgnoresArmourOnlyByItsSummonersRowNamingTraps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your traps ignore 20%-40% of enemy armor", WORN at the top of its roll, 40,
 * and a spike trap given `Type.Trap` by hand striking a creature of 800 armour.
 * Ruled 2026-10-07: a machine's blow takes its summoner's armour penetration
 * only from a modifier naming the machine's kind.
 *
 * THE EXPECTED FIGURE IS MEASURED, NOT WORKED OUT. A plain character whose
 * armour penetration attribute is the row's figure strikes an equal creature
 * with a blow of the trap's own figure, `OwnDamagePerHit`, and the trap's blow
 * must take what that blow takes. That control is itself checked first: at no
 * armour penetration it must take what the plain summoner's trap takes, against
 * no armour and against 800, or the two are not comparable and the test says
 * so and stops.
 *
 * AND WHAT MUST NOT CHANGE. A ballista of the same summoner, which is no trap. A
 * spike trap with the trap tag taken off by hand (the data gives every Spike
 * Trap the tag since issue #2284). A
 * trap whose summoner wears "Your skills ignore 10%-25% of enemy armor", a row
 * on the same stat with no required tag. A trap whose summoner's own armour
 * penetration attribute is 50.
 */
bool FCataclysmTrapArmourRowTest::RunTest(const FString&)
{
	using namespace CataclysmTrapLayerTest;
	CataclysmDeployableTest::FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World)
		|| !TestTrue(TEXT("the trap tag exists"), TheTrapTag().IsValid()))
	{
		return false;
	}
	const FTrapTestRange Range(Scope.World);
	const float Armour = 800.0f;
	const FName ArmourPenetration(TEXT("armor_penetration"));
	const FGameplayTagContainer AsATrap(TheTrapTag());

	// THE PLAIN SUMMONER'S TRAP AND BALLISTA, which are what "unchanged" means.
	CataclysmDeployableTest::FSummoner Plain(Scope.World, nullptr);
	ACataclysmMinion* PlainTrap = TrapOf(Plain);
	const float Unarmoured = Range.BlowFrom(PlainTrap, 0.0f);
	const float PlainTrapBlow = Range.BlowFrom(PlainTrap, Armour);
	const float PlainBallistaBlow = Range.BlowFrom(Plain.Make(TEXT("Ballista")), Armour);
	if (!TestTrue(
			FString::Printf(TEXT("the armour bites: a plain trap takes %.3f with none and %.3f through 800"),
				Unarmoured, PlainTrapBlow),
			PlainTrapBlow > 0.0f && PlainTrapBlow < Unarmoured - 0.5f)
		|| !TestTrue(TEXT("a plain ballista lands a blow"), PlainBallistaBlow > 0.0f))
	{
		return false;
	}

	// THE ROW, WORN, AND WHAT IT COMES TO FOR A TRAP.
	CataclysmDeployableTest::FSummoner Rowed(Scope.World,
		TEXT("Positive_Your_traps_ignore_20_40_of_enemy_armor"));
	Rowed.Along = 10000.0f;
	const float Named = Rowed.ASC()->StatNamingTagAppliedTo(
		ArmourPenetration, TheTrapTag(), /*Figure=*/0.0f, AsATrap, /*Target=*/nullptr);
	if (!TestEqual(TEXT("the worn row comes to 40 for a trap. If not, regenerate the DataTable assets"),
			Named, 40.0f, 0.001f))
	{
		return false;
	}

	// THE CONTROL, AND THE CHECK ON THE CONTROL.
	const float TrapsOwnFigure = PlainTrap->OwnDamagePerHit;
	const float ControlUnarmoured = Range.PlainBlow(TrapsOwnFigure, /*ArmourPenetration=*/0.0f, 0.0f);
	const float ControlWithNone = Range.PlainBlow(TrapsOwnFigure, /*ArmourPenetration=*/0.0f, Armour);
	const float ControlWithTheRows = Range.PlainBlow(TrapsOwnFigure, Named, Armour);
	if (!TestEqual(TEXT("the control is comparable against no armour: a plain blow of the trap's own "
						"figure takes what the plain trap's blow takes"),
			ControlUnarmoured, Unarmoured, 0.01f)
		|| !TestEqual(TEXT("the control is comparable against 800 armour: with no armour penetration "
						   "it takes what the plain trap's blow takes"),
			ControlWithNone, PlainTrapBlow, 0.01f)
		|| !TestTrue(
			FString::Printf(TEXT("the control moves: 40 armour penetration takes %.3f against %.3f"),
				ControlWithTheRows, ControlWithNone),
			ControlWithTheRows > ControlWithNone + 0.5f))
	{
		return false;
	}

	ACataclysmMinion* RowedTrap = TrapOf(Rowed);
	const float RowedTrapBlow = Range.BlowFrom(RowedTrap, Armour);
	TestTrue(
		FString::Printf(TEXT("with the row a trap's blow takes more through armour: %.3f against %.3f"),
			RowedTrapBlow, PlainTrapBlow),
		RowedTrapBlow > PlainTrapBlow + 0.5f);
	TestEqual(TEXT("and it takes what a plain blow with 40 armour penetration takes"),
		RowedTrapBlow, ControlWithTheRows, 0.01f);
	TestEqual(TEXT("against no armour the row changes nothing"),
		Range.BlowFrom(RowedTrap, 0.0f), Unarmoured, 0.01f);

	TestEqual(TEXT("a ballista of the same summoner is no trap: its blow is unchanged"),
		Range.BlowFrom(Rowed.Make(TEXT("Ballista")), Armour), PlainBallistaBlow, 0.01f);
	TestEqual(TEXT("a spike trap with the trap tag taken off by hand is unchanged"),
		Range.BlowFrom(SpikeTrapWithNoTrapTagOf(Rowed), Armour), PlainTrapBlow, 0.01f);

	// A ROW ON THE SAME STAT THAT NAMES NO TRAP. First that it is live on its
	// wearer: it has no condition, so it stands on the attribute at 25.
	CataclysmDeployableTest::FSummoner Unscoped(Scope.World,
		TEXT("Positive_Your_skills_ignore_10_25_of_enemy_armor"));
	Unscoped.Along = 20000.0f;
	if (TestEqual(TEXT("the row with no required tag is live on its wearer: 25 armour penetration"),
			Unscoped.ASC()->GetNumericAttribute(
				UCataclysmCombatAttributeSet::GetArmorPenetrationAttribute()),
			25.0f, 0.001f))
	{
		TestEqual(TEXT("and it does not reach that wearer's trap"),
			Range.BlowFrom(TrapOf(Unscoped), Armour), PlainTrapBlow, 0.01f);
	}

	// AND THE SUMMONER'S OWN ATTRIBUTE, set by hand on a summoner with no row.
	CataclysmDeployableTest::FSummoner Sharp(Scope.World, nullptr);
	Sharp.Along = 30000.0f;
	Sharp.ASC()->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetArmorPenetrationAttribute(), 50.0f);
	if (TestEqual(TEXT("a summoner whose own armour penetration is 50"),
			Sharp.ASC()->GetNumericAttribute(
				UCataclysmCombatAttributeSet::GetArmorPenetrationAttribute()),
			50.0f, 0.001f))
	{
		TestEqual(TEXT("gives its trap none of it"),
			Range.BlowFrom(TrapOf(Sharp), Armour), PlainTrapBlow, 0.01f);
	}
	return true;
}

// THE SPIKE TRAP IS A TRAP. The owner allowed the tag on 2026-10-07: `Type.Trap` on the Spike Trap's row of the
// minion types. A summoned minion carries its row's tags, and a summoner's row scoped to traps is matched against
// them.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpikeTrapIsATrapTest,
	"Cataclysm.Enchantments.ASummonedSpikeTrapCarriesTheTrapTagAndABallistaDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Spike Trap and a Ballista are summoned from the built table. Both are
 * deployables; only the Spike Trap carries `Type.Trap`.
 *
 * AND THE CONTROL TWO OTHER TESTS USE IS SEEN FROM ITS OTHER SIDE: the helper
 * `CataclysmTrapLayerTest::SpikeTrapWithNoTrapTagOf` summons a Spike Trap and
 * takes the tag off by hand, and that one does not carry it.
 */
bool FCataclysmSpikeTrapIsATrapTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWearer Wearer(World);
	const FGameplayTag TrapTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false);
	if (!TestTrue(TEXT("set-up: the trap tag is registered"), TrapTag.IsValid()))
	{
		return false;
	}
	// THREE METRES APART, so neither spawn is refused for standing in the other.
	ACataclysmMinion* Trap = ACataclysmMinion::Spawn(Wearer.Actor, FVector(300.0f, 0.0f, 0.0f),
		/*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("SpikeTrap"));
	ACataclysmMinion* Ballista = ACataclysmMinion::Spawn(Wearer.Actor, FVector(600.0f, 0.0f, 0.0f),
		/*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("Ballista"));
	ON_SCOPE_EXIT
	{
		if (IsValid(Trap)) { Trap->Destroy(); }
		if (IsValid(Ballista)) { Ballista->Destroy(); }
	};
	if (!TestNotNull(TEXT("set-up: a spike trap of the wearer's"), Trap)
		|| !TestNotNull(TEXT("set-up: a ballista of the wearer's"), Ballista)
		|| !TestTrue(TEXT("set-up: the spike trap is a deployable"), Trap->IsDeployable())
		|| !TestTrue(TEXT("set-up: the ballista is a deployable"), Ballista->IsDeployable()))
	{
		return false;
	}
	TestTrue(TEXT("the spike trap carries Type.Trap. If not, DT_MinionTypes may be older than the workbook: run "
				  "tools/generate_datatable_assets.py"),
		Trap->TypeTags.HasTagExact(TrapTag));
	TestFalse(TEXT("the ballista does not"), Ballista->TypeTags.HasTagExact(TrapTag));

	// A SECOND SUMMONER, A HUNDRED METRES ALONG, so its machine stands in nobody's place.
	CataclysmDeployableTest::FSummoner Other(World, nullptr);
	Other.Along = 10000.0f;
	ACataclysmMinion* Untagged = CataclysmTrapLayerTest::SpikeTrapWithNoTrapTagOf(Other);
	ON_SCOPE_EXIT { if (IsValid(Untagged)) { Untagged->Destroy(); } };
	if (TestNotNull(TEXT("set-up: the helper's spike trap"), Untagged))
	{
		TestFalse(TEXT("the helper's spike trap, with the tag taken off by hand, does not carry Type.Trap"),
			Untagged->TypeTags.HasTagExact(TrapTag));
		TestFalse(TEXT("and the game does not count it a trap"), Untagged->IsTrap());
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTrapDamageRowTest,
	"Cataclysm.Enchantments.TheTrapDamageRowRaisesWhatASpikeTrapDealsAndNotWhatABallistaDeals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Traps deal 20%-40% increased damage", written again on 2026-10-07 as one
 * row: `minion_damage` increased 20 to 40, Required Tags `Type.Trap`. The real
 * row WORN at its best roll. A Spike Trap and a Ballista of the wearer's are
 * summoned from the built table, and the summoner's multiplier on minion
 * damage is asked with each one's own tags, as a minion's swing asks it: the
 * Spike Trap's is 1.4 times the Ballista's, and the Ballista's is what it is
 * with the item taken off.
 */
bool FCataclysmTrapDamageRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	// THE NAME WORN IS LOOKED UP IN THE TABLE FIRST, so a name that is not a row fails here and says so.
	const TCHAR* const RowName = TEXT("Positive_Traps_deal_20_40_increased_damage");
	const UDataTable* Positive =
		CataclysmEnchantmentEffectTest::LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	if (!TestNotNull(TEXT("set-up: EnchantmentsPositive.csv can be read"), Positive)
		|| !TestTrue(TEXT("set-up: the name this test wears is a row of EnchantmentsPositive.csv"),
					 Positive->GetRowMap().Contains(FName(RowName))))
	{
		return false;
	}
	FWorn Worn(RowName, true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const FGameplayTag TrapTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false);
	// THREE METRES APART, so neither spawn is refused for standing in the other.
	ACataclysmMinion* Trap = ACataclysmMinion::Spawn(Worn.Wearer->Actor, FVector(300.0f, 0.0f, 0.0f),
		/*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("SpikeTrap"));
	ACataclysmMinion* Ballista = ACataclysmMinion::Spawn(Worn.Wearer->Actor, FVector(600.0f, 0.0f, 0.0f),
		/*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("Ballista"));
	ON_SCOPE_EXIT
	{
		if (IsValid(Trap)) { Trap->Destroy(); }
		if (IsValid(Ballista)) { Ballista->Destroy(); }
	};
	if (!TestTrue(TEXT("set-up: the trap tag is registered"), TrapTag.IsValid())
		|| !TestNotNull(TEXT("set-up: a spike trap of the wearer's"), Trap)
		|| !TestNotNull(TEXT("set-up: a ballista of the wearer's"), Ballista)
		|| !TestTrue(TEXT("set-up: the spike trap carries Type.Trap"), Trap->TypeTags.HasTagExact(TrapTag))
		|| !TestFalse(TEXT("set-up: the ballista does not"), Ballista->TypeTags.HasTagExact(TrapTag)))
	{
		return false;
	}
	const auto MultiplierFor = [&Worn](const ACataclysmMinion* Minion)
	{
		return UCataclysmCommand::SummonerMultiplierFor(Worn.Wearer->Actor, TEXT("minion_damage"), Minion->TypeTags);
	};
	const float OfTheBallista = MultiplierFor(Ballista);
	if (!TestTrue(TEXT("set-up: the ballista's multiplier is above nought"), OfTheBallista > 0.0f))
	{
		return false;
	}
	TestEqual(*(FString(TEXT("worn: the spike trap's multiplier is 1.4 times the ballista's.")) +
				CataclysmRepeatRowsTest::OlderAsset),
		MultiplierFor(Trap) / OfTheBallista, 1.4f, 0.001f);

	Worn.Wearer->Equipment->UnequipEverything();
	Worn.Wearer->Equipment->RefreshAttributes(Worn.ASC());
	TestEqual(TEXT("taken off: the ballista's multiplier is what it was with the row worn"),
		MultiplierFor(Ballista), OfTheBallista, 0.001f);
	TestEqual(TEXT("and the spike trap's is the ballista's"), MultiplierFor(Trap), MultiplierFor(Ballista), 0.001f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTrapCountRowsTest,
	"Cataclysm.Enchantments.TheThreeTrapRowsOnOtherTrapsAndGadgetsEachRaiseWhatASpikeTrapDealsByItsCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Three rows on `minion_damage`, each requiring `Type.Trap`. Issue #2284, ruled
 * 2026-10-07. Each real row is WORN at its best roll by a wearer that then
 * commands three Spike Traps and two Ballistas from the built table, and the
 * summoner's multiplier on minion damage is asked with a Spike Trap's tags and
 * with a Ballista's, as each machine's swing asks it. A RATIO, never a figure:
 *   "for each other trap": two other traps at 30, the trap's is 1.6 times;
 *   "each active gadget": two gadgets at 20, 1.4 times;
 *   "each gadget ... doubles": two gadgets, three times, since each adds 100
 *   to one sum.
 * A Ballista is no trap, so its own multiplier is what it is with the item
 * taken off.
 */
bool FCataclysmTrapCountRowsTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	struct FCase
	{
		const TCHAR* Row;
		float TrapOverBallista;
	};
	const FCase Cases[] = {
		{TEXT("Positive_Traps_deal_15_30_increased_damage_for_each_oth"), 1.6f},
		{TEXT("Positive_Each_active_gadget_increases_trap_damage_by_10"), 1.4f},
		{TEXT("Positive_Each_gadget_on_the_battlefield_doubles_the_damag"), 3.0f},
	};
	const FGameplayTag TrapTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false);
	// EVERY NAME WORN IS LOOKED UP IN THE TABLE FIRST, so a name that is not a row fails here and says so.
	const UDataTable* Positive =
		CataclysmEnchantmentEffectTest::LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	if (!TestNotNull(TEXT("set-up: EnchantmentsPositive.csv can be read"), Positive)
		|| !TestTrue(TEXT("set-up: the trap tag is registered"), TrapTag.IsValid()))
	{
		return false;
	}
	for (const FCase& Case : Cases)
	{
		if (!TestTrue(FString::Printf(TEXT("set-up: %s is a row of EnchantmentsPositive.csv"), Case.Row),
				Positive->GetRowMap().Contains(FName(Case.Row))))
		{
			return false;
		}
		FWorn Worn(Case.Row, true);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		// FIVE MACHINES, FOUR METRES APART, so no spawn is refused for standing in another.
		TArray<ACataclysmMinion*> Machines;
		const TCHAR* const Types[] = {TEXT("SpikeTrap"), TEXT("SpikeTrap"), TEXT("SpikeTrap"), TEXT("Ballista"),
									  TEXT("Ballista")};
		float Along = 0.0f;
		bool bAllSpawned = true;
		for (const TCHAR* Type : Types)
		{
			Along += 400.0f;
			ACataclysmMinion* Made = ACataclysmMinion::Spawn(Worn.Wearer->Actor, FVector(Along, 0.0f, 0.0f),
				/*Lifetime=*/60.0f, /*bBurns=*/false, Type);
			bAllSpawned = bAllSpawned
				&& TestNotNull(FString::Printf(TEXT("set-up: %s, the machine %.0f metres along"), Case.Row,
								   Along / 100.0f), Made);
			Machines.Add(Made);
		}
		ON_SCOPE_EXIT
		{
			for (ACataclysmMinion* Made : Machines)
			{
				if (IsValid(Made)) { Made->Destroy(); }
			}
		};
		if (!bAllSpawned)
		{
			return false;
		}
		const ACataclysmMinion* Trap = Machines[0];
		const ACataclysmMinion* Ballista = Machines[3];
		const FCataclysmStatConditions State = Worn.ASC()->CurrentConditions();
		if (!TestTrue(TEXT("set-up: the spike trap carries Type.Trap"), Trap->TypeTags.HasTagExact(TrapTag))
			|| !TestEqual(TEXT("set-up: three traps are counted"), State.TrapsActive, 3)
			|| !TestEqual(TEXT("set-up: two gadgets are counted"), State.GadgetsActive, 2))
		{
			return false;
		}
		const auto MultiplierFor = [&Worn](const ACataclysmMinion* Minion)
		{
			return UCataclysmCommand::SummonerMultiplierFor(Worn.Wearer->Actor, TEXT("minion_damage"),
				Minion->TypeTags);
		};
		const float OfTheBallista = MultiplierFor(Ballista);
		if (!TestTrue(TEXT("set-up: the ballista's multiplier is above nought"), OfTheBallista > 0.0f))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("%s, worn: the spike trap's multiplier over the ballista's is %.1f.%s"),
					  Case.Row, Case.TrapOverBallista, CataclysmRepeatRowsTest::OlderAsset),
			MultiplierFor(Trap) / OfTheBallista, Case.TrapOverBallista, 0.001f);

		Worn.Wearer->Equipment->UnequipEverything();
		Worn.Wearer->Equipment->RefreshAttributes(Worn.ASC());
		TestEqual(FString::Printf(TEXT("%s, taken off: the ballista's multiplier is what it was"), Case.Row),
			MultiplierFor(Ballista), OfTheBallista, 0.001f);
		TestEqual(FString::Printf(TEXT("%s, taken off: the spike trap's is the ballista's"), Case.Row),
			MultiplierFor(Trap), MultiplierFor(Ballista), 0.001f);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStrikeOnARangedDodgeRowTest,
	"Cataclysm.Enchantments.TheEvadedRangedAttackRowHandsItsWearerAStrikeOnADodgeScopedToRanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "When you evade a ranged attack, throw an attack dealing 20-70% of your
 * attack damage at that enemy". Issue #1833, ruled 2026-10-07: `strike_target`
 * on `dodge`, 20 to 70, Required Tags `Type.Ranged`. The real row WORN at its
 * best roll: its wearer holds exactly one action of that kind on `dodge`, at
 * 70, requiring `Type.Ranged` and nothing else, and none when the item is
 * taken off. What the strike then does is the tests of the entry that built
 * the action.
 */
bool FCataclysmStrikeOnARangedDodgeRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	// THE NAME WORN IS LOOKED UP IN THE TABLE FIRST, so a name that is not a row fails here and says so.
	const TCHAR* const RowName = TEXT("Positive_When_you_evade_a_ranged_attack_throw_an_attack");
	const UDataTable* Positive =
		CataclysmEnchantmentEffectTest::LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	if (!TestNotNull(TEXT("set-up: EnchantmentsPositive.csv can be read"), Positive)
		|| !TestTrue(TEXT("set-up: the name this test wears is a row of EnchantmentsPositive.csv"),
					 Positive->GetRowMap().Contains(FName(RowName))))
	{
		return false;
	}
	const FGameplayTagContainer Ranged = CataclysmRepeatRowsTest::Tagged(TEXT("Type.Ranged"));
	if (!TestTrue(TEXT("set-up: the ranged tag exists"), Ranged.Num() == 1))
	{
		return false;
	}
	FWorn Worn(RowName, true);
	if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
	{
		return false;
	}
	const FName Dodge(TEXT("dodge"));
	int32 OfItsKind = 0;
	float Share = -1.0f;
	bool bScopedToRangedAlone = false;
	for (const FCataclysmPoolAction& Action : Worn.ASC()->GetPoolActions())
	{
		if (Action.bStrikeTarget && Action.Event == Dodge)
		{
			++OfItsKind;
			Share = Action.Percent;
			bScopedToRangedAlone = Action.RequiredTags == Ranged;
		}
	}
	TestEqual(*(FString(TEXT("worn: one action that strikes the event's other character, on a dodge.")) +
				CataclysmRepeatRowsTest::OlderAsset),
		OfItsKind, 1);
	TestEqual(TEXT("at the row's best roll, 70"), Share, 70.0f, 0.01f);
	TestTrue(TEXT("requiring Type.Ranged and nothing else"), bScopedToRangedAlone);

	Worn.Wearer->Equipment->UnequipEverything();
	Worn.Wearer->Equipment->RefreshAttributes(Worn.ASC());
	int32 Left = 0;
	for (const FCataclysmPoolAction& Action : Worn.ASC()->GetPoolActions())
	{
		Left += Action.bStrikeTarget ? 1 : 0;
	}
	TestEqual(TEXT("taken off: no such action is left"), Left, 0);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShieldEveryTwelveSecondsRowTest,
	"Cataclysm.Enchantments.TheShieldEveryTwelveSecondsRowHandsItsWearerATimedTemporaryAbsorb",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Every 12 seconds gain a shield absorbing 15%-25% of your maximum HP in
 * damage". Issue #1833, ruled 2026-10-07: `temporary_absorb` on `every_seconds`,
 * every 12 seconds, 15 to 25. The real row WORN at its best roll: its wearer
 * holds exactly one action of that kind on the timed event, every 12 seconds,
 * at 25, and none when the item is taken off. What a temporary absorb then
 * does is the tests of the entry that built it.
 */
bool FCataclysmShieldEveryTwelveSecondsRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	// THE NAME WORN IS LOOKED UP IN THE TABLE FIRST, so a name that is not a row fails here and says so.
	const TCHAR* const TimedRow = TEXT("Positive_Every_12_seconds_gain_a_shield_absorbing_15_25");
	const UDataTable* Positive =
		CataclysmEnchantmentEffectTest::LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	if (!TestNotNull(TEXT("set-up: EnchantmentsPositive.csv can be read"), Positive)
		|| !TestTrue(TEXT("set-up: the name this test wears is a row of EnchantmentsPositive.csv"),
					 Positive->GetRowMap().Contains(FName(TimedRow))))
	{
		return false;
	}

	{
		FWorn Worn(TimedRow, true);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		const FName Timed(TEXT("every_seconds"));
		int32 OfItsKind = 0;
		float Share = -1.0f;
		float Seconds = -1.0f;
		for (const FCataclysmPoolAction& Action : Worn.ASC()->GetPoolActions())
		{
			if (Action.bTemporaryAbsorb && Action.Event == Timed)
			{
				++OfItsKind;
				Share = Action.Percent;
				Seconds = Action.EverySeconds;
			}
		}
		TestEqual(*(FString(TEXT("the timed row, worn: one action that grants a temporary absorb, on the timed event.")) +
					CataclysmRepeatRowsTest::OlderAsset),
			OfItsKind, 1);
		TestEqual(TEXT("at its best roll, 25"), Share, 25.0f, 0.01f);
		TestEqual(TEXT("every 12 seconds"), Seconds, 12.0f, 0.01f);

		Worn.Wearer->Equipment->UnequipEverything();
		Worn.Wearer->Equipment->RefreshAttributes(Worn.ASC());
		int32 Left = 0;
		for (const FCataclysmPoolAction& Action : Worn.ASC()->GetPoolActions())
		{
			Left += Action.bTemporaryAbsorb ? 1 : 0;
		}
		TestEqual(TEXT("the timed row, taken off: no such action is left"), Left, 0);
	}

	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShieldFromOverhealRowTest,
	"Cataclysm.Enchantments.TheOverhealShieldRowRaisesTheShareOfMaximumHealthThatOverhealMayKeep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Overheal converts to a temporary shield absorbing up to 10%-20% of your max
 * HP". Issue #1833, ruled 2026-10-07:
 * `overheal_absorb_percent_of_maximum_health` flat 10 to 20. The real row WORN
 * at its best roll raises the stat by 20 over what it reads with the item
 * taken off. What overheal then does is the tests of the entry that built it.
 */
bool FCataclysmShieldFromOverhealRowTest::RunTest(const FString&)
{
	using namespace CataclysmSmallHalvesTest;
	// THE NAME WORN IS LOOKED UP IN THE TABLE FIRST, so a name that is not a row fails here and says so.
	const TCHAR* const OverhealRow = TEXT("Positive_Overheal_converts_to_a_temporary_shield_absorbin");
	const UDataTable* Positive =
		CataclysmEnchantmentEffectTest::LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	if (!TestNotNull(TEXT("set-up: EnchantmentsPositive.csv can be read"), Positive)
		|| !TestTrue(TEXT("set-up: the name this test wears is a row of EnchantmentsPositive.csv"),
					 Positive->GetRowMap().Contains(FName(OverhealRow))))
	{
		return false;
	}

	{
		FWorn Worn(OverhealRow, true);
		if (!TestNotNull(TEXT("a wearer in a world"), Worn.ASC()))
		{
			return false;
		}
		const FName Stat(TEXT("overheal_absorb_percent_of_maximum_health"));
		const float WornReading = Worn.ASC()->StatForSkill(Stat, FGameplayTagContainer(), 0.0f);
		Worn.Wearer->Equipment->UnequipEverything();
		Worn.Wearer->Equipment->RefreshAttributes(Worn.ASC());
		const float OffReading = Worn.ASC()->StatForSkill(Stat, FGameplayTagContainer(), 0.0f);
		// A DIFFERENCE, as every reading of a stat on a refreshed wearer is in this file since 2026-10-07.
		TestEqual(*(FString(TEXT("the overheal row, worn: the stat is 20 above what it is with the item taken off.")) +
					CataclysmRepeatRowsTest::OlderAsset),
			WornReading - OffReading, 20.0f, 0.01f);
	}
	return true;
}
// TRAPS LAST LONGER. Ruled 2026-10-07: the gadget duration row with the trap tag in place of the deployable tag.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTrapDurationRowTest,
	"Cataclysm.Enchantments.TheTrapDurationRowLengthensASpikeTrapsLifeAndNoOtherMachines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Traps last 50%-100% longer before expiring". Issue #1833, ruled 2026-10-07:
 * `minion_duration` increased 50 to 100, requiring `Type.Trap`. A minion is
 * asked for its life span with its own type tags, so the real row WORN at its
 * best roll doubles the life span of a Spike Trap summoned from the built
 * table and leaves a Ballista's and an Imp's what they were. Every figure is a
 * ratio against the same machine of a summoner wearing nothing.
 */
bool FCataclysmTrapDurationRowTest::RunTest(const FString&)
{
	using namespace CataclysmDeployableTest;
	// THE NAME WORN IS LOOKED UP IN THE TABLE FIRST, so a name that is not a row fails here and says so.
	const TCHAR* const RowName = TEXT("Positive_Traps_last_50_100_longer_before_expiring");
	const UDataTable* Positive =
		CataclysmEnchantmentEffectTest::LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	if (!TestNotNull(TEXT("set-up: EnchantmentsPositive.csv can be read"), Positive)
		|| !TestTrue(TEXT("set-up: the name this test wears is a row of EnchantmentsPositive.csv"),
					 Positive->GetRowMap().Contains(FName(RowName))))
	{
		return false;
	}
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	// TWO SUMMONERS A HUNDRED METRES APART, each placing its machines four metres from the last.
	FSummoner Plain(Scope.World, nullptr);
	FSummoner Longer(Scope.World, RowName);
	Longer.Along = 10000.0f;

	const float PlainTrap = Plain.LifeOf(TEXT("SpikeTrap"));
	const float PlainBallista = Plain.LifeOf(TEXT("Ballista"));
	const float PlainImp = Plain.LifeOf(TEXT("Imp"));
	if (!TestTrue(TEXT("set-up: a plain spike trap has a life span"), PlainTrap > 0.0f)
		|| !TestTrue(TEXT("set-up: a plain ballista has a life span"), PlainBallista > 0.0f)
		|| !TestTrue(TEXT("set-up: a plain imp has a life span"), PlainImp > 0.0f))
	{
		return false;
	}
	TestEqual(*(FString(TEXT("worn: a spike trap lasts twice as long.")) + CataclysmRepeatRowsTest::OlderAsset),
		Longer.LifeOf(TEXT("SpikeTrap")) / PlainTrap, 2.0f, 0.001f);
	TestEqual(TEXT("worn: a ballista is no trap and lasts what it did"),
		Longer.LifeOf(TEXT("Ballista")) / PlainBallista, 1.0f, 0.001f);
	TestEqual(TEXT("worn: an imp lasts what it did"), Longer.LifeOf(TEXT("Imp")) / PlainImp, 1.0f, 0.001f);
	return true;
}
#endif // WITH_AUTOMATION_TESTS
