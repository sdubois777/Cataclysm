// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Items/CataclysmItem.h"
#include "Data/CataclysmDataRows.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "Character/CataclysmClassStats.h"
#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * The end-to-end check: does the Unreal port agree with the Python model?
 *
 * Every test before this one checks one curve or one rule. This builds the whole
 * reference geared character out of real items read from the generated data
 * tables, runs every defensive stat through the stat pipeline, and then throws a
 * real enemy hit at it through the damage calculation.
 *
 * WHY IT MATTERS. `sim/cataclysm_sim/reference_build.py` is the character the
 * enemy damage constants in `sim/cataclysm_sim/enemy_stats.py` were fitted
 * against. If the game reaches different numbers from the model, every enemy in
 * the project is mistuned and nothing else would say so.
 *
 * WHAT THE CHARACTER IS. A level 100 Ravager with 60 Vitality and 40
 * Constitution, wearing eighteen pieces at affix tier 7 and gear level +10,
 * spending all 36 prefix slots and 23 of the 36 suffix slots: the re-fit for the
 * whole-number affix ranges (issue #2883), with the 13 suffix places it freed
 * left empty by the owner's ruling on issue #2884 (2026-11-24, "A middle build
 * (Recommended)"). In the rarity model that is 8 Masterful, 7 Superb and 3
 * Quality pieces. Until then it spent all 72 and was a full Masterful set.
 *
 * NOTHING IS QUOTED ANY MORE. The Ravager's class base values and what its
 * attribute points are worth used to be literals copied from the Python model,
 * because the Unreal project had neither. Both are now read from
 * game/Data/ClassStats.csv and game/Data/Attributes.csv, so a change to the
 * Ravager's stat line in the design workbook fails this test rather than
 * passing unnoticed. Only the FINAL figures are still stated, which is the
 * point: they are what the model says the character reaches.
 *
 * WHERE THE AFFIXES SIT. Since issue #2884 the Python model places every affix
 * on a named piece (`reference_build.LAYOUT`), and the list below is that
 * layout, piece by piece, respecting every affix's narrowed slots and the two
 * prefix and two suffix caps. The Greatsword carries only offensive affixes,
 * which matters: a two-handed weapon doubles what it carries, and the model does
 * not apply that. Since every affix that touches a defensive stat is barred from
 * weapons anyway, the defensive figures below are unaffected either way.
 */

namespace CataclysmReferenceTest
{
	using FValues = UCataclysmItemValues;
	using FModifiers = UCataclysmItemModifiers;

	constexpr int32 AffixTier = 7;
	constexpr int32 GearLevel = 10;
	constexpr int32 DifficultyTier = 8;

	template <typename RowType>
	UDataTable* LoadTable(const TCHAR* FileName)
	{
		FString Contents;
		const FString Path = FPaths::ProjectDir() / TEXT("Data") / FileName;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			return nullptr;
		}
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = RowType::StaticStruct();
		return Table->CreateTableFromCSVString(Contents).Num() > 0 ? nullptr : Table;
	}

	/** One piece: its base, and the affixes it carries. */
	struct FPiece
	{
		const TCHAR* Base;
		TArray<const TCHAR*> Affixes;
	};

	/**
	 * The eighteen pieces, with all 59 affixes placed and 13 places empty.
	 *
	 * The LAYOUT of `sim/cataclysm_sim/reference_build.py`, written out: the
	 * totals are exactly its PREFIX_SPEND, SUFFIX_SPEND and four all-resistance
	 * affixes.
	 */
	TArray<FPiece> ReferencePieces()
	{
		const TCHAR* Health = TEXT("Stat_Flat_maximum_health");
		const TCHAR* HealthUp = TEXT("Stat_Increased_maximum_health");
		const TCHAR* Armor = TEXT("Stat_Flat_armor");
		const TCHAR* ArmorUp = TEXT("Stat_Increased_armor");
		const TCHAR* Block = TEXT("Stat_Flat_block_chance");
		const TCHAR* Reduction = TEXT("Stat_Flat_damage_reduction");
		const TCHAR* Resist = TEXT("Resistance_All_resistances");
		const TCHAR* Damage = TEXT("Stat_Flat_damage");
		const TCHAR* DamageUp = TEXT("Stat_Increased_damage");
		const TCHAR* Crit = TEXT("Stat_Increased_critical_strike_chance");
		const TCHAR* CritMult = TEXT("Stat_Flat_critical_strike_multiplier");
		const TCHAR* Speed = TEXT("Stat_Increased_attack_speed");
		const TCHAR* Pen = TEXT("Stat_Flat_penetration");

		return {
			{ TEXT("Head_Helm"),            { Health, HealthUp, Resist } },
			{ TEXT("Chest_Cuirass"),        { Health, HealthUp, Resist } },
			{ TEXT("Shoulders_Pauldrons"),  { Health, Armor, Block } },
			{ TEXT("Gloves_Gauntlets"),     { Damage, DamageUp, Crit, Speed } },
			{ TEXT("Pants_Greaves"),        { Health, ArmorUp, Reduction } },
			{ TEXT("Boots_Sabatons"),       { Health, Armor, Resist } },
			{ TEXT("Belt_Girdle"),          { Health, ArmorUp, Block } },
			{ TEXT("Ring_Loop"),            { Health, HealthUp, Resist } },
			{ TEXT("Ring_Loop"),            { Health, HealthUp } },
			{ TEXT("Ring_Loop"),            { HealthUp, Armor } },
			{ TEXT("Ring_Loop"),            { HealthUp, Armor } },
			{ TEXT("Ring_Loop"),            { Armor, ArmorUp, Crit, CritMult } },
			{ TEXT("Ring_Loop"),            { Armor, ArmorUp, Crit, Speed } },
			{ TEXT("Ring_Loop"),            { Damage, DamageUp, CritMult, Speed } },
			{ TEXT("Ring_Loop"),            { Damage, DamageUp, Crit, CritMult } },
			{ TEXT("Necklace_Torc"),        { Damage, DamageUp, CritMult, Speed } },
			{ TEXT("Relic_Idol"),           { Damage, DamageUp, Crit, CritMult } },
			{ TEXT("Weapon_Greatsword"),    { Damage, DamageUp, Pen, Speed } },
		};
	}

	/** Every modifier the eighteen pieces contribute, keyed by stat. */
	TMap<FName, TArray<FCataclysmStatModifier>> GearModifiers(
		const UDataTable* Bases, const UDataTable* Affixes)
	{
		TMap<FName, TArray<FCataclysmStatModifier>> Totals;
		for (const FPiece& Piece : ReferencePieces())
		{
			FCataclysmItem Item;
			Item.Base = FName(Piece.Base);
			Item.GearLevel = GearLevel;
			for (const TCHAR* Affix : Piece.Affixes)
			{
				FCataclysmRolledAffix Rolled;
				Rolled.Affix = FName(Affix);
				Rolled.Tier = AffixTier;
				Rolled.Roll = 1.0f;   // A perfect roll, as the model uses.
				Item.Affixes.Add(Rolled);
			}
			FModifiers::AccumulateInto(Totals, Item, Bases, Affixes);
		}
		return Totals;
	}

	/** The character the model describes: a level 100 Ravager. */
	const TCHAR* ClassName = TEXT("Ravager");
	constexpr int32 Level = 100;

	/** 60 into Vitality for health, 40 into Constitution for armour and block. */
	FCataclysmAttributePoints ReferenceAttributes()
	{
		FCataclysmAttributePoints Points;
		Points.Vitality = 60;
		Points.Constitution = 40;
		return Points;
	}

	/**
	 * What `sim/cataclysm_sim/reference_build.py` reports the character reaches.
	 *
	 * Only the FINAL figures are stated. The class base and the attribute
	 * contribution are read from the generated tables, so this test fails if
	 * either changes rather than silently agreeing with a stale copy.
	 */
	struct FExpectation
	{
		const TCHAR* Stat;
		float Expected;
	};

	// THE RE-FIT OF ISSUE #2884, under the whole-number affix ranges of #2883:
	// `reference_build.defender(8)`. Until then 11,023 health, 7,299.42 armour,
	// 28 block, 23.95 damage reduction and 144 resistance.
	const FExpectation ExpectedStats[] = {
		{ TEXT("max_health"),         20309.50f },
		{ TEXT("armor"),              12549.88f },
		{ TEXT("block_chance"),          98.00f },
		// 23.95 SINCE ISSUE #1230, and it was 15.95. The flat damage
		// reduction affix states a top of 4 rather than 2, so the four slots
		// this build spends on it are worth 16 rather than 8. That is the
		// affix whose worst roll was 0.2 percentage points, which is what
		// that issue was about; the top moved with the floor.
		{ TEXT("damage_reduction"),      42.95f },
		// 160 SINCE ISSUE #2883: four all-resistance affixes at 40 each, which
		// caps all eight at tier 8 (145 needed). It was 144 from twelve.
		{ TEXT("resistance_demonic"),   160.00f },
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReferenceBuildStatsTest,
	"Cataclysm.ReferenceBuild.StatsMatchThePythonModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmReferenceBuildStatsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmReferenceTest;

	UDataTable* Bases = LoadTable<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
	UDataTable* Affixes = LoadTable<FCataclysmAffixRow>(TEXT("Affixes.csv"));
	if (!Bases || !Affixes)
	{
		AddError(TEXT("could not load ItemBases.csv or Affixes.csv"));
		return false;
	}

	const TMap<FName, TArray<FCataclysmStatModifier>> Gear = GearModifiers(Bases, Affixes);

	// The character carries 59 affixes and leaves 13 places empty (issue #2884),
	// so by the rarity rule it wears 8 Masterful, 7 Superb and 3 Quality pieces.
	int32 AffixCount = 0;
	TMap<ECataclysmRarity, int32> Rarities;
	for (const FPiece& Piece : ReferencePieces())
	{
		AffixCount += Piece.Affixes.Num();
		ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
		if (TestTrue(TEXT("a piece's affixes and no enchantment name a rarity"),
				FValues::RarityOf(0, Piece.Affixes.Num(), Rarity)))
		{
			++Rarities.FindOrAdd(Rarity);
		}
	}
	TestEqual(TEXT("the build spends 59 affix slots"), AffixCount, 59);
	TestEqual(TEXT("8 pieces are Masterful"), Rarities.FindRef(ECataclysmRarity::Masterful), 8);
	TestEqual(TEXT("7 are Superb"), Rarities.FindRef(ECataclysmRarity::Superb), 7);
	TestEqual(TEXT("3 are Quality"), Rarities.FindRef(ECataclysmRarity::Quality), 3);

	UDataTable* Classes = LoadTable<FCataclysmClassStatRow>(TEXT("ClassStats.csv"));
	UDataTable* AttributeEffects =
		LoadTable<FCataclysmAttributeEffectRow>(TEXT("Attributes.csv"));
	if (!Classes || !AttributeEffects)
	{
		AddError(TEXT("could not load ClassStats.csv or Attributes.csv"));
		return false;
	}

	const FCataclysmAttributePoints Points = ReferenceAttributes();
	TestEqual(TEXT("the character spends 100 attribute points at level 100"),
		Points.Total(), Level);

	const FGameplayTagContainer NoSkill;

	for (const FExpectation& Case : ExpectedStats)
	{
		const FName Stat(Case.Stat);
		const FString StatName(Case.Stat);

		// The class base, read from the generated table rather than quoted.
		const float ClassBase =
			UCataclysmClassStats::BaseFor(Classes, ClassName, StatName, Level);

		TArray<FCataclysmStatModifier> Modifiers;
		if (const TArray<FCataclysmStatModifier>* Found = Gear.Find(Stat))
		{
			Modifiers = *Found;
		}

		// Attribute points enter the increased bucket, exactly as gear
		// increases do. The pipeline does not care where an increase came from.
		FCataclysmStatModifier FromAttributes;
		if (UCataclysmClassStats::AttributeModifierFor(
				AttributeEffects, Points, StatName, FromAttributes))
		{
			Modifiers.Add(FromAttributes);
		}

		const FCataclysmStatBreakdown Result =
			UCataclysmStatPipeline::Evaluate(ClassBase, Modifiers, NoSkill);

		TestTrue(FString::Printf(
				TEXT("%s: the model says %.2f, the game says %.2f"),
				Case.Stat, Case.Expected, Result.Final),
			FMath::IsNearlyEqual(Result.Final, Case.Expected, 0.5f));

		// Nothing an item grants may be a more multiplier.
		TestTrue(FString::Printf(TEXT("%s took no more multiplier"), Case.Stat),
			FMath::IsNearlyEqual(Result.MoreMultiplier, 1.0f, 0.0001f));
	}

	// The two numbers the class table supplies, checked directly as well, so a
	// failure says which half is wrong rather than only that the total is.
	TestTrue(TEXT("the Ravager has 2,110 maximum health at level 100"),
		FMath::IsNearlyEqual(
			UCataclysmClassStats::BaseFor(Classes, ClassName,
										  TEXT("max_health"), Level),
			2110.0f, 0.01f));
	TestTrue(TEXT("and 371.5 armour"),
		FMath::IsNearlyEqual(
			UCataclysmClassStats::BaseFor(Classes, ClassName,
										  TEXT("armor"), Level),
			371.5f, 0.01f));

	// 60 Vitality at 2% each is 120 percentage points of increased health.
	FCataclysmStatModifier Vitality;
	TestTrue(TEXT("Vitality touches maximum health"),
		UCataclysmClassStats::AttributeModifierFor(
			AttributeEffects, Points, TEXT("max_health"), Vitality));
	TestTrue(TEXT("60 Vitality gives 120 percentage points"),
		FMath::IsNearlyEqual(Vitality.Value, 120.0f, 0.01f));
	TestEqual(TEXT("and it lands in the increased bucket, never flat"),
		static_cast<int32>(Vitality.Bucket),
		static_cast<int32>(ECataclysmStatBucket::Increased));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReferenceBuildSurvivalTest,
	"Cataclysm.ReferenceBuild.SurvivesWhatTheModelSaysItSurvives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmReferenceBuildSurvivalTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmReferenceTest;

	// The four mitigation layers, at the values the stat test above proves the
	// game reaches. Stated here rather than recomputed so this test fails on its
	// own terms if the damage calculation drifts.
	// THE RE-FIT OF ISSUE #2884 (see the expectations above).
	constexpr float Health = 20309.5f;
	constexpr float Armor = 12549.88f;
	constexpr float BlockChance = 98.0f;
	constexpr float DamageReduction = 42.95f;
	constexpr float Resistance = 160.0f;

	// Armour against the tier 8 curve removes 66.2% (53.3% before the re-fit),
	// and resistance is capped at 70% however far over it a character is.
	const float ArmorReduction =
		UCataclysmDamageCalculation::ArmorReduction(Armor, DifficultyTier);
	TestTrue(FString::Printf(TEXT("armour removes 66.2%%, got %.1f%%"), ArmorReduction),
		FMath::IsNearlyEqual(ArmorReduction, 66.2f, 0.2f));

	const float Effective =
		UCataclysmDamageCalculation::EffectiveResistance(Resistance, 0.0f);
	TestTrue(TEXT("160% resistance is capped to 70%"),
		FMath::IsNearlyEqual(Effective, 70.0f, 0.01f));

	// The four layers multiply. Block removes half a hit on 98% of hits, so it
	// is worth 49% on average.
	const float Landing =
		(1.0f - ArmorReduction / 100.0f)
		* (1.0f - Effective / 100.0f)
		* (1.0f - (BlockChance / 100.0f) * 0.5f)
		* (1.0f - DamageReduction / 100.0f);

	// 2.95% SINCE THE RE-FIT OF ISSUE #2884; 9.17% before it, and 10.1% until
	// issue #1230 doubled the flat damage reduction affix.
	TestTrue(FString::Printf(
			TEXT("a hit lands for 2.95%% of itself, got %.2f%%"), Landing * 100.0f),
		FMath::IsNearlyEqual(Landing * 100.0f, 2.95f, 0.05f));

	// A Cataclysm Boss Gatekeeper at difficulty tier 8 hits for 378,027 raw
	// since the enemy rebalance of issue #2884 (65,497 before it). The model
	// reports that landing for 11,144 and killing this character in 2 hits, as
	// it did before: the rebalance was fitted to keep the hits.
	constexpr float GatekeeperHit = 378027.0f;
	const float Landed = GatekeeperHit * Landing;

	TestTrue(FString::Printf(
			TEXT("a Gatekeeper's 378,027 lands for about 11,144, got %.0f"), Landed),
		FMath::IsNearlyEqual(Landed, 11144.0f, 300.0f));

	const int32 HitsToKill = FMath::CeilToInt(Health / Landed);
	TestEqual(TEXT("and two of them kill this character"), HitsToKill, 2);

	// A Common Imp hits for 11,567 raw since issue #2884 (2,016 before it) and
	// still takes 60 hits, which is what makes a pack rather than one enemy the
	// threat. It was 54 until issue #1230.
	constexpr float ImpHit = 11567.0f;
	const int32 ImpHits = FMath::CeilToInt(Health / (ImpHit * Landing));
	TestEqual(TEXT("a Common Imp needs 60 hits"), ImpHits, 60);

	// No combination of layers reaches immunity. Checked rather than asserted,
	// because it is the property the whole defensive model rests on.
	TestTrue(TEXT("something always gets through"), Landing > 0.0f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
