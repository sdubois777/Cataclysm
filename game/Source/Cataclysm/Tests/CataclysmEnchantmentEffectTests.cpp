// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
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
	const TCHAR* SetMarker =
		TEXT("Positive_Archon_s_Aegis_2_Piece_Bonus_Your_block_chanc");
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
			 {ShieldBenefit, LowHealthBenefit, BenefitWithNoEffect, SetMarker})
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
			  SpellsMovingDrawback, RangedCloseDrawback})
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

	int32 Checked = 0;
	for (const TPair<FName, uint8*>& Row : EffectTable->GetRowMap())
	{
		const auto* Effect =
			reinterpret_cast<const FCataclysmEnchantmentEffectRow*>(Row.Value);
		if (!Effect || Effect->Stat.IsEmpty())
		{
			continue;
		}

		++Checked;
		TestTrue(*FString::Printf(
					 TEXT("%s grants '%s', which has an attribute behind it"),
					 *Row.Key.ToString(), *Effect->Stat),
				 Attributes.Contains(Effect->Stat));
	}

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

#endif // WITH_AUTOMATION_TESTS