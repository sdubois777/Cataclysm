// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
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
			 {HealthDrawback, DrawbackWithNoEffect, SetDrawback})
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

#endif // WITH_AUTOMATION_TESTS
