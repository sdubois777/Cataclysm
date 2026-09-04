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
 * spending all 36 prefix and all 36 suffix slots. In the rarity model that is a
 * full set of MASTERFUL gear: four regular affixes on every piece and no
 * enchantments.
 *
 * NOTHING IS QUOTED ANY MORE. The Ravager's class base values and what its
 * attribute points are worth used to be literals copied from the Python model,
 * because the Unreal project had neither. Both are now read from
 * game/Data/ClassStats.csv and game/Data/Attributes.csv, so a change to the
 * Ravager's stat line in the design workbook fails this test rather than
 * passing unnoticed. Only the FINAL figures are still stated, which is the
 * point: they are what the model says the character reaches.
 *
 * WHERE THE AFFIXES SIT. The Python model spends 72 affix slots without saying
 * which piece each is on. The assignment below was solved so that the totals
 * match exactly while respecting every affix's slot restrictions and the two
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
	 * The eighteen pieces, with all 72 affixes placed.
	 *
	 * Solved against `sim/cataclysm_sim/reference_build.py`: the totals are
	 * exactly its PREFIX_SPEND, SUFFIX_SPEND and twelve all-resistance affixes.
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
			{ TEXT("Head_Helm"),           { Health, Health, Block, Block } },
			{ TEXT("Chest_Cuirass"),       { Health, Health, Block, Block } },
			{ TEXT("Shoulders_Pauldrons"), { Health, Health, Reduction, Reduction } },
			{ TEXT("Gloves_Gauntlets"),    { Damage, Damage, Crit, Crit } },
			{ TEXT("Pants_Greaves"),       { Health, Health, Reduction, Reduction } },
			{ TEXT("Boots_Sabatons"),      { HealthUp, HealthUp, Resist, Resist } },
			{ TEXT("Belt_Girdle"),         { HealthUp, HealthUp, Resist, Resist } },
			{ TEXT("Ring_Loop"),           { HealthUp, HealthUp, CritMult, CritMult } },
			{ TEXT("Ring_Loop"),           { Armor, Armor, CritMult, CritMult } },
			{ TEXT("Ring_Loop"),           { Armor, Armor, Speed, Speed } },
			{ TEXT("Ring_Loop"),           { Armor, Armor, Speed, Speed } },
			{ TEXT("Ring_Loop"),           { ArmorUp, ArmorUp, Pen, Pen } },
			{ TEXT("Ring_Loop"),           { ArmorUp, ArmorUp, Pen, Pen } },
			{ TEXT("Ring_Loop"),           { DamageUp, DamageUp, Resist, Resist } },
			{ TEXT("Ring_Loop"),           { DamageUp, DamageUp, Resist, Resist } },
			{ TEXT("Necklace_Torc"),       { Damage, Damage, Resist, Resist } },
			{ TEXT("Relic_Idol"),          { Damage, Damage, Resist, Resist } },
			{ TEXT("Weapon_Greatsword"),   { DamageUp, DamageUp, Crit, Crit } },
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

	const FExpectation ExpectedStats[] = {
		{ TEXT("max_health"),         11023.00f },
		{ TEXT("armor"),               7299.42f },
		{ TEXT("block_chance"),          28.00f },
		{ TEXT("damage_reduction"),      15.95f },
		// 144 SINCE ISSUE #1229, and it was 72. The twelve all-resistance
		// affixes this build spends are worth 12 each rather than 6. Note what
		// that leaves: a tier 8 character needs 145 to sit at the 70 cap, so
		// this build is one point short of capping at the tier it is built for.
		{ TEXT("resistance_demonic"),   144.00f },
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

	// The character carries all 72 affixes, so it is a full set of Masterful
	// gear: four regular affixes on every piece and no enchantments.
	int32 AffixCount = 0;
	for (const FPiece& Piece : ReferencePieces())
	{
		AffixCount += Piece.Affixes.Num();
	}
	TestEqual(TEXT("the build spends all 72 affix slots"), AffixCount, 72);

	ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
	TestTrue(TEXT("four affixes and no enchantment name a rarity"),
		FValues::RarityOf(0, 4, Rarity));
	TestEqual(TEXT("so every piece is Masterful"),
		static_cast<int32>(Rarity), static_cast<int32>(ECataclysmRarity::Masterful));

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
	constexpr float Health = 11023.0f;
	constexpr float Armor = 7299.42f;
	constexpr float BlockChance = 28.0f;
	constexpr float DamageReduction = 15.95f;
	constexpr float Resistance = 144.0f;

	// Armour against the tier 8 curve removes 53.3%, and resistance is capped at
	// 70% however far over it a character is.
	const float ArmorReduction =
		UCataclysmDamageCalculation::ArmorReduction(Armor, DifficultyTier);
	TestTrue(FString::Printf(TEXT("armour removes 53.3%%, got %.1f%%"), ArmorReduction),
		FMath::IsNearlyEqual(ArmorReduction, 53.3f, 0.2f));

	const float Effective =
		UCataclysmDamageCalculation::EffectiveResistance(Resistance, 0.0f);
	TestTrue(TEXT("72% resistance is capped to 70%"),
		FMath::IsNearlyEqual(Effective, 70.0f, 0.01f));

	// The four layers multiply. Block removes half a hit on 28% of hits, so it
	// is worth 14% on average.
	const float Landing =
		(1.0f - ArmorReduction / 100.0f)
		* (1.0f - Effective / 100.0f)
		* (1.0f - (BlockChance / 100.0f) * 0.5f)
		* (1.0f - DamageReduction / 100.0f);

	TestTrue(FString::Printf(
			TEXT("a hit lands for 10.1%% of itself, got %.2f%%"), Landing * 100.0f),
		FMath::IsNearlyEqual(Landing * 100.0f, 10.1f, 0.2f));

	// A Cataclysm Boss Gatekeeper at difficulty tier 8 hits for 65,497 raw.
	// `sim/cataclysm_sim/reference_build.py` reports that landing for 6,635 and
	// killing this character in 2 hits.
	constexpr float GatekeeperHit = 65497.0f;
	const float Landed = GatekeeperHit * Landing;

	TestTrue(FString::Printf(
			TEXT("a Gatekeeper's 65,497 lands for about 6,635, got %.0f"), Landed),
		FMath::IsNearlyEqual(Landed, 6635.0f, 150.0f));

	const int32 HitsToKill = FMath::CeilToInt(Health / Landed);
	TestEqual(TEXT("and two of them kill this character"), HitsToKill, 2);

	// A Common Imp hits for 2,016 raw and takes 54 hits, which is what makes a
	// pack rather than one enemy the threat.
	constexpr float ImpHit = 2016.0f;
	const int32 ImpHits = FMath::CeilToInt(Health / (ImpHit * Landing));
	TestEqual(TEXT("a Common Imp needs 54 hits"), ImpHits, 54);

	// No combination of layers reaches immunity. Checked rather than asserted,
	// because it is the property the whole defensive model rests on.
	TestTrue(TEXT("something always gets through"), Landing > 0.0f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
