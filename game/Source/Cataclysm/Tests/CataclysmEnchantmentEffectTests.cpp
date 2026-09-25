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
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Interface/CataclysmSkillBar.h"
#include "GameplayTagContainer.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
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
	const TCHAR* BenefitWithNoEffect =
		TEXT("Positive_Ultimate_has_1_3_additional_charges");
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
					 "Action,ActionEvent,FractionOf,ScaleMaxSteps,StackSeconds,ScaleOffset,EverySeconds,EveryNth\n"))
		+ FString::Printf(
			TEXT("%s#1,%s,,,4,4,,,0,,0,health,block,maximum,0,0,0,0,0\n"),
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
						 "Action,ActionEvent,FractionOf,ScaleMaxSteps,StackSeconds,ScaleOffset,EverySeconds,EveryNth\n"))
			+ FString::Printf(
				TEXT("%s#1,%s,armor,increased,10,10,,,0,own_stacks,1,,critical_strike,,5,5,0,0,0\n"),
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
 * THE ROW IS THE COMPLEMENT OF ITS SENTENCE: max_health more, -60 to -40. The
 * item text and the value are both taken by POSITION in the range, so at the
 * lowest roll the text shows the sentence's first number, 40, and the value is
 * the row's first, -60, which leaves 40% of the maximum. At the highest roll the
 * text shows 60 and the value -40 leaves 60%. Read in play, off the maximum
 * health a real refresh writes, against the same wearer with nothing on.
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

	TestEqual(TEXT("at the lowest roll the text says 40%"),
		UCataclysmItemValues::EnchantmentTextAtRoll(Sentence, 0.0f),
		FString(TEXT("Your maximum HP cannot exceed 40% of its normal value")));
	TestEqual(TEXT("and 40% of the maximum is what is left"), WornAt(0.0f), 0.4f, 0.0001f);
	TestEqual(TEXT("at the highest roll the text says 60%"),
		UCataclysmItemValues::EnchantmentTextAtRoll(Sentence, 1.0f),
		FString(TEXT("Your maximum HP cannot exceed 60% of its normal value")));
	TestEqual(TEXT("and 60% of the maximum is what is left"), WornAt(1.0f), 0.6f, 0.0001f);
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
		explicit FSummoner(UWorld* InWorld, const TCHAR* Enchantment)
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
					Carrying(TEXT("Head_Helm"), Enchantment, DrawbackWithNoEffect),
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
 * attack_damage and on spell_damage; reading both would make it 1.8.
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

	FSummoner Increased(Scope.World, TEXT("Positive_Gadgets_deal_20_40_increased_damage"));
	TestEqual(TEXT("increased: a ballista's blow is 1.4 times"),
		Increased.Blow(TEXT("Ballista")) / PlainBallista, 1.4f, 0.001f);
	TestEqual(TEXT("increased: an imp's blow is unchanged"),
		Increased.Blow(TEXT("Imp")) / PlainImp, 1.0f, 0.001f);

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

#endif // WITH_AUTOMATION_TESTS