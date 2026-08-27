// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "GameplayTagsManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * Loads every generated CSV through the struct it is supposed to match.
 *
 * This is the half a Python test cannot do. The generator can produce a
 * perfectly well-formed CSV whose columns do not match the USTRUCT, and Unreal
 * will import it without complaint -- the mismatched column simply arrives as
 * its default value. Nothing reports it. The first symptom is a weight of zero
 * or an empty description somewhere far from the cause.
 *
 * CreateTableFromCSVString returns one string per problem, so an unmatched
 * column or a bad value fails the test with the engine's own message.
 */

namespace
{
	FString DataDir()
	{
		return FPaths::ProjectDir() / TEXT("Data");
	}

	/** Loads one CSV through RowStruct. Returns false and fills OutError on failure. */
	template <typename RowType>
	bool LoadTable(const TCHAR* FileName, int32& OutRowCount, FString& OutError)
	{
		const FString Path = DataDir() / FileName;

		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			OutError = FString::Printf(TEXT("could not read %s"), *Path);
			return false;
		}

		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = RowType::StaticStruct();

		const TArray<FString> Problems = Table->CreateTableFromCSVString(Contents);
		if (Problems.Num() > 0)
		{
			OutError = FString::Printf(TEXT("%s: %d import problem(s): %s"),
				FileName, Problems.Num(), *FString::Join(Problems, TEXT(" | ")));
			return false;
		}

		OutRowCount = Table->GetRowMap().Num();
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDataTablesImportTest,
	"Cataclysm.Data.EveryGeneratedTableImports",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDataTablesImportTest::RunTest(const FString& Parameters)
{
	int32 Rows = 0;
	FString Error;

	// Row counts are pinned. A generator change that silently drops rows -- a
	// header misread, an off-by-one on a column -- shows up here rather than as
	// missing content much later.
	struct FExpectation { const TCHAR* File; int32 ExpectedRows; };

	#define CHECK_TABLE(RowType, File, Expected) \
		Rows = 0; Error.Empty(); \
		if (!LoadTable<RowType>(TEXT(File), Rows, Error)) \
		{ AddError(Error); } \
		else { TestEqual(TEXT(File) TEXT(" row count"), Rows, Expected); }

	// 117, not 116. The Corrupted Stalker was added for issue #504. It was the
	// only dungeon modifier described in prose and missing from the data, and a
	// modifier with no weight cannot contribute to a dungeon's Modifier Score.
	CHECK_TABLE(FCataclysmDungeonModifierRow,   "DungeonModifiers.csv",      117)
	CHECK_TABLE(FCataclysmWeaponSkillRow,       "WeaponSkills.csv",          398)
	// 379, not 380. The two minion count enchantments were merged into one for
	// issue #339: the rarer of the two granted half as much, and they said the
	// same thing in different words.
	//
	// 380, not 381. One weight 1 positive enchantment was removed: it read "Your
	// block chance applies to AOE damage at 50% effectiveness", which became
	// strictly harmful once block was decided to apply to area damage by
	// default at full effectiveness.
	CHECK_TABLE(FCataclysmEnchantmentRow,       "EnchantmentsPositive.csv",  379)
	CHECK_TABLE(FCataclysmEnchantmentRow,       "EnchantmentsNegative.csv",  195)
	CHECK_TABLE(FCataclysmEnemyModifierRow,     "EnemyModifiers.csv",         79)
	// 52, not 50. Stun and Knockdown were added for issue #363. Both are hard
	// stops that section VI of the design document gives three rules to, and
	// neither existed as a named effect the data could reference -- which is
	// why the Brute's stomp states its stun as a standalone StunSeconds rider
	// rather than as Effect=Stun.
	//
	// 50, not 46. Four player-applied debuffs were defined: Madness, Cripple,
	// Shred and Weaken. All four were already applied by gems and by affixes,
	// and none of them said what they did.
	CHECK_TABLE(FCataclysmStatusEffectRow,      "StatusEffects.csv",          52)
	// 27, not 26. The Of Wasting gem was added to apply Necrosis, which was the
	// one status effect in the data that nothing applied, and the Of Embers gem
	// to apply Burn, which every designed Demonic skill applies and which no gem
	// and no affix could reach.
	CHECK_TABLE(FCataclysmGemRow,               "Gems.csv",                   27)
	CHECK_TABLE(FCataclysmCityUpgradeRow,       "CityUpgrades.csv",           24)
	// 46 SINCE 2026-08-23, issue #852, and it was 37. The one row named
	// "Upgrade Stone (x)" was a placeholder that showed on screen that way.
	// It became ten stones, "+1" through "+10", spread two to a rarity tier
	// because ten in one tier would have crowded out everything else in it.
	CHECK_TABLE(FCataclysmCraftingMaterialRow,  "CraftingMaterials.csv",      46)
	// 55 bases across 11 slots, at least three per slot, because one base in a
	// slot is not a choice.
	CHECK_TABLE(FCataclysmItemBaseRow,          "ItemBases.csv",              55)
	// 12: ten gear slots at one row each, and a weapon twice because its
	// maximum depends on how many hands it takes. The four potion slots are
	// not gear and nothing rolls one as a drop, so they are not here.
	CHECK_TABLE(FCataclysmItemSocketRow,        "ItemSockets.csv",            12)
	// 78: 53 single-stat affixes, 3 resistance families, 10 ailments and 12
	// hybrids. The single-stat count rose from 35 on 2026-08-04: eight when
	// gear began granting a percentage increase to each primary attribute, two
	// when mana leech and energy shield leech were added for #214, and eight
	// when increased damage against each enemy damage type was added for #213.
	// 84, not 80. The four minion affixes landed with issue #337: increased
	// minion damage, minion health and minion attack speed, plus the one
	// hybrid, whose slot list is Ring alone because that is the only slot
	// both of its halves roll on.
	// 85, not 84. The chance to stun landed with issue #298 on 2026-08-16:
	// an affix does grant a chance to stun, and a Blunt weapon's 10% is part
	// of the same pool rather than a separate mechanic.
	CHECK_TABLE(FCataclysmAffixRow,             "Affixes.csv",                85)
	// 7: one per affix tier, T1 to T7, holding how heavily each is weighted
	// when a drop rolls an affix. Which tiers are REACHABLE is decided by the
	// difficulty tier instead, so this count does not move with it.
	CHECK_TABLE(FCataclysmAffixTierRow,         "AffixTiers.csv",              7)
	// 8: the eight gear rarities, one row each, in the same order and under
	// the same names as ECataclysmRarity. The engine walks the ladder by that
	// enum and looks each row up by its name, so a row missing here is a
	// rarity a drop cannot roll.
	CHECK_TABLE(FCataclysmGearRarityRow,        "GearRarity.csv",              8)
	// 30: nine stats on the shared default line, plus what the Ravager,
	// Ritualist and Masochist each override.
	CHECK_TABLE(FCataclysmClassStatRow,         "ClassStats.csv",             33)
	// 5. Two summoned creatures and three deployed machines. The Imp, which
	// Summon Imp and Open the Rift both produce, and the Mote from Cinder Swarm
	// landed with issue #336. The bolt turret, ballista and spike trap followed
	// with issue #338, because their numbers lived only in prose and Iron
	// Fortress deploys two ballistae AND three spike traps, which a skill row
	// cannot say on its own.
	CHECK_TABLE(FCataclysmMinionTypeRow,        "MinionTypes.csv",             5)
	// 2, not 4. One row per (attribute, tag, stat) and only damage is decided:
	// Spirit raises a Minion.Creature and Agility a Minion.Machine, both by 1%
	// per point. Health is expressible in the same table and no figure has been
	// chosen for it.
	CHECK_TABLE(FCataclysmMinionScalingRow,     "MinionScaling.csv",           2)
	// 17: eight attributes, each raising two stats, except Efficacy raising
	// three.
	CHECK_TABLE(FCataclysmAttributeEffectRow,   "Attributes.csv",             17)
	// 7: the seven skill slots from the design document's Skill Slots table.
	// Six a player chooses between plus the automatic Basic Attack.
	CHECK_TABLE(FCataclysmSkillSlotRow,         "SkillSlots.csv",              7)
	// 8: the seven Demonic Cataclysm enemies the design names for the vertical
	// slice, plus the Baseline row, which is not a creature anyone fights but
	// the abstract average the rarity ladder is read against. It rises to 15
	// when the second Cataclysm's enemies are designed, so this pin is what
	// notices an archetype silently going missing in the meantime.
	CHECK_TABLE(FCataclysmEnemyArchetypeRow,    "EnemyArchetypes.csv",         8)
	// 6, not 7. The ladder is Common, Elite, Legendary, Herald, Boss and
	// Cataclysm Boss, matching scoring.RARITY_WEIGHTS. There is no "Rare": the
	// design document's older list is superseded, which is issue #30.
	CHECK_TABLE(FCataclysmEnemyRarityRow,       "EnemyRarities.csv",           6)
	// 6, and it must be the same six. EnemyRarities comes from the simulation's
	// enemy model and this comes from the workbook, so the generator
	// cross-checks that they name the same rarities in the same order.
	CHECK_TABLE(FCataclysmEnemyDropRow,         "EnemyDrops.csv",              6)
	// 5: Common, Uncommon, Rare, Very Rare, Extremely Rare, as the Crafting
	// sheet names them. The Materials column on each is checked against how
	// many crafting materials really are in that tier.
	CHECK_TABLE(FCataclysmMaterialTierRow,      "MaterialTiers.csv",           5)
	// 8: one per damage type, and it is the same eight for good. The generator
	// refuses to build this table unless every Element.* tag on the workbook's
	// Tags sheet has exactly one row, so a ninth damage type raises this number
	// and a missing one fails generation before it reaches here.
	CHECK_TABLE(FCataclysmElementVisualRow,     "ElementVisuals.csv",          8)
	// 293: every node of the four class trees that exist. Berserker holds 71
	// and the other three hold 74 each. The design says "approximately 74
	// nodes" per tree; the pin is what notices the generator silently dropping
	// one, which would leave a hole in a tree the game draws. Issue #50.
	//
	// IT RISES A LOT WHEN THE OTHER TWENTY TREES ARRIVE, issue #24, and that
	// is what a pin is for: a jump nobody meant is the same shape as a jump
	// somebody did.
	CHECK_TABLE(FCataclysmPassiveNodeRow,       "PassiveNodes.csv",          293)
	// 278: every dependency edge of those four trees. Fewer than the nodes,
	// because the four capstones of each tree deliberately have none -- a
	// capstone tier is reached by total points spent rather than along a path.
	CHECK_TABLE(FCataclysmPassiveEdgeRow,       "PassiveEdges.csv",          278)
	// 24 of the 293 nodes have an authored effect, and the small share is the
	// point rather than a shortfall to be fixed by typing: most nodes are not
	// stat modifiers under any authoring scheme. Issue #939 measures the gap.
	//
	// IT WAS 26 UNTIL 2026-08-25, when the Masochist's upper-right section was
	// replaced. Three rows went with the nodes holding them -- maximum mana,
	// mana regeneration and area of effect -- and one arrived for maximum
	// health. The rest of the new section is mechanics rather than modifiers, so
	// it adds nothing here. See docs/DECISIONS.md.
	//
	// AND UP TO 34 ON 2026-08-25, when Fervour was given a way of being
	// generated. Ten rows on eight Masochist nodes: three on the tree's starting
	// node, which grants all three rates at once, and one each on the seven
	// nodes that increase or reduce one of them. Issue #954.
	//
	// AND TO 36 THE SAME DAY, for Pain Tolerance, which grants a maximum health
	// increase and an armour increase in one sentence and needed no code at all.
	//
	// AND TO 38, for Last Stand and Desperate Measures, the first two nodes whose
	// bonus depends on where the character's health is. Issue #959.
	//
	// AND TO 40 FOR ONE NODE, Living on the Edge, which grants increased damage
	// below a health threshold. Two rows rather than one because the project
	// owner settled on 2026-08-25 that "increased damage" on a passive node
	// means attack damage and spell damage together, and not damage over time.
	// Issue #958.
	//
	// AND TO 42 FOR ONE MORE, Blood Rush, which grants the same two stats for a
	// short window after a health cost is paid. Issue #962.
	//
	// AND TO 43 FOR VICIOUS ONSLAUGHT, the first node whose bonus GROWS with a
	// state rather than switching on and off with it. One row rather than two,
	// because the node names Attack Damage outright. Issue #968.
	//
	// AND TO 44 FOR DEEPER CUTS, which adds a health cost to every skill the
	// character uses. Issue #970.
	//
	// AND TO 45 FOR THE CATALYST, whose skills sometimes do not go on cooldown
	// at all. Issue #973.
	//
	// AND TO 47 FOR CATACLYSMIC RESONANCE, the same two damage stats under a
	// window opened by taking damage of a foreign Cataclysm type. Issue #975.
	//
	// AND TO 68 FOR THE THREE NODES THAT FINISH THE BLOOD TITHE BRANCH: two
	// rows for Compound Interest, whose damage grows with what the character
	// owes (#994); one for Rolling Debt, the seconds a further payment pushes an
	// outstanding debt out (#995); and four for The Reckoning, which defers the
	// whole of every cost, multiplies damage by what is owed, and carries the
	// flag for a debt that is never taken on a timer (#997). The count also
	// passed 61 for nodes landed between #975 and those three.
	//
	// AND TO 71 FOR THE THREE NODES BUILT ON A COUNT OF STACKS: one row each
	// for Sanguine Momentum, Blood Offering and Carnage. Issues #1002, #1003
	// and #1004.
	//
	// AND TO 75 FOR THE THREE NODES THAT CHANGE HOW FERVOUR MOVES: one row for
	// Wounds That Feed (#1006), two for Sanguine Ledger (#1007) and one for Low
	// Life (#1008).
	//
	// AND TO 77 FOR THE BREAKING POINT, which is two rows on one node. Issue
	// #985. A `flat` flag saying the rule applies at all, and an `increased` of
	// 5 on how long one turn of it lasts. The 3 second base that increase
	// multiplies is NOT a row here and could not be: this sheet carries values
	// PER POINT, and a base is not one.
	//
	// AND TO 91 FOR THE THREE NODES THAT CHANGE HOW MUCH DAMAGE THE CHARACTER
	// TAKES. Issue #1026. Echoes of Agony is one row on `damage_over_time_taken`;
	// Communion of Pain is three, because "deal 20% more damage" is the same two
	// damage stats as ever plus the damage taken; The Edge is two, one per clause
	// of its sentence.
	//
	// AND TO 93 FOR THE FIRST CAPSTONE OPTION EVER AUTHORED. Issue #1029. The
	// Final Vow's second option is two rows, and the sheet gained an `Option`
	// column so a row can say which of a capstone's three it belongs to.
	//
	// AND TO 94 FOR MUTILATION MASTERY. Issue #1032. One row, a flat chance
	// that a melee critical strike applies Bleeding to what it hit.
	//
	// AND BACK DOWN TO 92 WHEN APOTHEOSIS WAS REMOVED. Issue #1031. The project
	// owner rejected the drawback-bearing shape of the Masochist's twelve
	// capstone options and had all twelve rewritten as pure upgrades. Apotheosis
	// was The Final Vow's second option and the only one of the twelve with rows
	// behind it; Carnivore takes that slot now. THE FIRST FALL IN THIS COUNT.
	//
	// AND TO 99 FOR THE FIRST TWO OF THOSE REWRITTEN OPTIONS. Issue #1040.
	// Deficit is four rows, because "damage" is two stats on this sheet and
	// "missing or owe" is two scales; Doctrine Made Flesh is three.
	//
	// AND TO 102 FOR STIGMATIC. Issue #1042. Three rows on The First Vow's
	// third option, all scaled by how many debuffs the character carries.
	//
	// AND TO 106 FOR VESSEL UNBROKEN. Issue #1039. Four rows on The Final
	// Vow's third option: a flag saying damage over time deals the character
	// nothing, two for the damage each debuff multiplies, and the Fervour
	// each grants a second.
	CHECK_TABLE(FCataclysmPassiveEffectRow,     "PassiveEffects.csv",        106)

	#undef CHECK_TABLE

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDataTableValuesTest,
	"Cataclysm.Data.ValuesSurviveImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDataTableValuesTest::RunTest(const FString& Parameters)
{
	// Guards the test above. A row count can be right while every field is
	// empty, because a column whose name does not match the struct imports as a
	// default rather than an error.
	FString Contents;
	const FString Path = DataDir() / TEXT("DungeonModifiers.csv");
	if (!FFileHelper::LoadFileToString(Contents, *Path))
	{
		AddError(FString::Printf(TEXT("could not read %s"), *Path));
		return false;
	}

	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FCataclysmDungeonModifierRow::StaticStruct();
	Table->CreateTableFromCSVString(Contents);

	int32 WithWeight = 0;
	int32 WithDescription = 0;
	int32 WithCataclysm = 0;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const auto* Row = reinterpret_cast<FCataclysmDungeonModifierRow*>(Pair.Value);
		if (Row->Weight > 0.0f)          { ++WithWeight; }
		if (!Row->Description.IsEmpty()) { ++WithDescription; }
		if (!Row->CataclysmType.IsEmpty()) { ++WithCataclysm; }
	}

	// COMPARED AGAINST THE TABLE'S OWN ROW COUNT, not against a literal.
	//
	// All three of these read 116 until issue #363. Adding the Corrupted
	// Stalker for issue #504 took the table to 117 and made all three false,
	// and the number said nothing about what the test is for: the claim is that
	// EVERY modifier carries these, not that 116 of them do. Written this way
	// the assertion states the claim and cannot go stale when a modifier is
	// added.
	//
	// The row count itself is pinned separately, by the CHECK_TABLE line above,
	// so a table that silently lost rows is still caught. The guard here is
	// only against comparing zero with zero if the table failed to load at all.
	const int32 Total = Table->GetRowMap().Num();
	TestTrue(TEXT("the dungeon modifier table loaded rows"), Total > 0);
	TestEqual(TEXT("every dungeon modifier has a non-zero weight"), WithWeight, Total);
	TestEqual(TEXT("every dungeon modifier has a description"), WithDescription, Total);
	TestEqual(TEXT("every dungeon modifier names a Cataclysm"), WithCataclysm, Total);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDataTableTagsTest,
	"Cataclysm.Data.EveryReferencedTagResolves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDataTableTagsTest::RunTest(const FString& Parameters)
{
	// The generator checks tags against the spreadsheet. This checks them against
	// the engine, which is what actually matters at runtime: a tag the engine
	// does not know matches nothing and reports nothing.
	const TCHAR* Files[] = { TEXT("WeaponSkills.csv"),
							 TEXT("EnchantmentsPositive.csv"),
							 TEXT("EnchantmentsNegative.csv") };

	int32 Checked = 0;
	for (const TCHAR* File : Files)
	{
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *(DataDir() / File)))
		{
			AddError(FString::Printf(TEXT("could not read %s"), File));
			continue;
		}

		TArray<FString> Lines;
		Contents.ParseIntoArrayLines(Lines);
		for (int32 Index = 1; Index < Lines.Num(); ++Index)
		{
			// Tags are the trailing quoted field in every one of these files.
			int32 Start = INDEX_NONE;
			if (!Lines[Index].FindLastChar(TEXT('"'), Start))
			{
				continue;
			}
			FString Segment = Lines[Index];
			TArray<FString> Parts;
			Segment.ParseIntoArray(Parts, TEXT(","), true);
			for (FString Part : Parts)
			{
				Part.TrimStartAndEndInline();
				Part.ReplaceInline(TEXT("\""), TEXT(""));
				// Only consider things shaped like a tag: dotted, capitalised.
				if (!Part.Contains(TEXT(".")) || Part.Contains(TEXT(" ")))
				{
					continue;
				}
				if (!FChar::IsUpper(Part[0]))
				{
					continue;
				}
				const FGameplayTag Tag = UGameplayTagsManager::Get()
					.RequestGameplayTag(FName(*Part), /*ErrorIfNotFound=*/false);
				if (!Tag.IsValid())
				{
					AddError(FString::Printf(
						TEXT("%s line %d references unknown tag %s"),
						File, Index + 1, *Part));
				}
				++Checked;
			}
		}
	}

	TestTrue(TEXT("some tags were actually checked"), Checked > 100);
	return true;
}


/**
 * Every generated CSV has a DataTable asset, and the two agree.
 *
 * WHY THIS IS THE ONLY GUARANTEE THERE IS. tools/generate_datatable_assets.py
 * imports each CSV under game/Data/ into an asset under /Game/Data/. It cannot
 * offer a --check mode the way tools/generate_datatables.py does, because a
 * .uasset carries generated identifiers that differ between runs, so two runs
 * over unchanged input do not produce identical bytes and a byte comparison
 * would always report a difference.
 *
 * So the asset is compared against the CSV it came from instead. An asset that
 * was never regenerated after a workbook change has the wrong rows, and the
 * game would load stale data with nothing reporting it -- the CSV in a pull
 * request would show the change and the game would not have it.
 *
 * Row NAMES are compared and not merely counts. A table that gained one row and
 * lost another has the same count.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDataTableAssetsTest,
	"Cataclysm.Data.EveryGeneratedTableHasAnAssetThatMatchesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDataTableAssetsTest::RunTest(const FString& Parameters)
{
	// Asset name against CSV file. Every table the generator produces is here;
	// one missing from this list would be one nothing ever checks.
	//
	// TWO WERE MISSING FOR THREE DAYS. DT_MinionTypes and DT_MinionScaling
	// landed with issues #337 and #338 and were never added here, so neither
	// asset was ever compared against its CSV and every test still passed. The
	// sibling list of CHECK_TABLE lines above did not have the same hole,
	// because tools/tests/test_unreal_pinned_row_counts.py fails when a
	// generated CSV has no pin. This list now has a matching guard,
	// tools/tests/test_unreal_asset_check_covers_every_table.py. Issue #702.
	struct FPair { const TCHAR* Asset; const TCHAR* File; };
	const FPair Tables[] = {
		{ TEXT("DT_Affixes"),               TEXT("Affixes.csv") },
		{ TEXT("DT_AffixTiers"),            TEXT("AffixTiers.csv") },
		{ TEXT("DT_Attributes"),            TEXT("Attributes.csv") },
		{ TEXT("DT_CityUpgrades"),          TEXT("CityUpgrades.csv") },
		{ TEXT("DT_ClassStats"),            TEXT("ClassStats.csv") },
		{ TEXT("DT_CraftingMaterials"),     TEXT("CraftingMaterials.csv") },
		{ TEXT("DT_DungeonModifiers"),      TEXT("DungeonModifiers.csv") },
		{ TEXT("DT_ElementVisuals"),        TEXT("ElementVisuals.csv") },
		{ TEXT("DT_EnchantmentsNegative"),  TEXT("EnchantmentsNegative.csv") },
		{ TEXT("DT_EnchantmentsPositive"),  TEXT("EnchantmentsPositive.csv") },
		{ TEXT("DT_EnemyArchetypes"),       TEXT("EnemyArchetypes.csv") },
		{ TEXT("DT_EnemyDrops"),            TEXT("EnemyDrops.csv") },
		{ TEXT("DT_EnemyModifiers"),        TEXT("EnemyModifiers.csv") },
		{ TEXT("DT_EnemyRarities"),         TEXT("EnemyRarities.csv") },
		{ TEXT("DT_GearRarity"),            TEXT("GearRarity.csv") },
		{ TEXT("DT_Gems"),                  TEXT("Gems.csv") },
		{ TEXT("DT_ItemBases"),             TEXT("ItemBases.csv") },
		{ TEXT("DT_ItemSockets"),           TEXT("ItemSockets.csv") },
		{ TEXT("DT_MaterialTiers"),         TEXT("MaterialTiers.csv") },
		{ TEXT("DT_MinionScaling"),         TEXT("MinionScaling.csv") },
		{ TEXT("DT_PassiveEdges"),          TEXT("PassiveEdges.csv") },
		{ TEXT("DT_PassiveEffects"),        TEXT("PassiveEffects.csv") },
		{ TEXT("DT_PassiveNodes"),          TEXT("PassiveNodes.csv") },
		{ TEXT("DT_MinionTypes"),           TEXT("MinionTypes.csv") },
		{ TEXT("DT_SkillSlots"),            TEXT("SkillSlots.csv") },
		{ TEXT("DT_StatusEffects"),         TEXT("StatusEffects.csv") },
		{ TEXT("DT_WeaponSkills"),          TEXT("WeaponSkills.csv") },
	};

	for (const FPair& Pair : Tables)
	{
		const FString AssetPath =
			FString::Printf(TEXT("/Game/Data/%s.%s"), Pair.Asset, Pair.Asset);

		const UDataTable* Table = LoadObject<UDataTable>(nullptr, *AssetPath);
		if (!Table)
		{
			AddError(FString::Printf(
				TEXT("%s does not exist. Run tools/generate_datatable_assets.py."),
				*AssetPath));
			continue;
		}

		// The CSV it should have come from, read the same way the test above
		// reads it, so the two halves cannot disagree about what the file is.
		const FString Path = DataDir() / Pair.File;
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			AddError(FString::Printf(TEXT("could not read %s"), *Path));
			continue;
		}

		UDataTable* FromCsv = NewObject<UDataTable>();
		FromCsv->RowStruct = Table->RowStruct;
		FromCsv->CreateTableFromCSVString(Contents);

		TSet<FName> InAsset;
		for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
		{
			InAsset.Add(Row.Key);
		}

		TSet<FName> InCsv;
		for (const TPair<FName, uint8*>& Row : FromCsv->GetRowMap())
		{
			InCsv.Add(Row.Key);
		}

		const TSet<FName> MissingFromAsset = InCsv.Difference(InAsset);
		const TSet<FName> NotInCsv = InAsset.Difference(InCsv);

		TestEqual(FString::Printf(
			TEXT("%s has every row %s has"), Pair.Asset, Pair.File),
			MissingFromAsset.Num(), 0);
		TestEqual(FString::Printf(
			TEXT("%s has no row %s does not"), Pair.Asset, Pair.File),
			NotInCsv.Num(), 0);

		if (MissingFromAsset.Num() > 0 || NotInCsv.Num() > 0)
		{
			AddError(FString::Printf(
				TEXT("%s is stale. Run tools/generate_datatable_assets.py. ")
				TEXT("%d row(s) only in the CSV, %d only in the asset."),
				Pair.Asset, MissingFromAsset.Num(), NotInCsv.Num()));
			continue;
		}

		// THE ROW NAMES MATCHING IS NOT ENOUGH, and this half was missing.
		//
		// Comparing only the key set says nothing about what is in the rows. It
		// let a real staleness through: the sixteen Demonic skills landed in the
		// CSV, the DataTable asset was never regenerated, and every row name
		// still matched because the rows had always existed -- they were simply
		// empty. The asset shipped with no Demonic skill in it and this test
		// passed.
		//
		// Both sides are re-exported through the same function so the comparison
		// is of contents rather than of file formatting.
		const FString AssetAsCsv = Table->GetTableAsCSV();
		const FString FileAsCsv = FromCsv->GetTableAsCSV();
		if (AssetAsCsv != FileAsCsv)
		{
			// Name the first row that differs. "Something differs somewhere in
			// 398 rows" is not actionable.
			TArray<FString> AssetLines, FileLines;
			AssetAsCsv.ParseIntoArrayLines(AssetLines);
			FileAsCsv.ParseIntoArrayLines(FileLines);

			FString FirstDifference = TEXT("(could not isolate a line)");
			for (int32 Line = 0; Line < FMath::Min(AssetLines.Num(), FileLines.Num()); ++Line)
			{
				if (AssetLines[Line] != FileLines[Line])
				{
					FirstDifference = FString::Printf(
						TEXT("line %d:\n    asset: %s\n    csv:   %s"),
						Line + 1,
						*AssetLines[Line].Left(200),
						*FileLines[Line].Left(200));
					break;
				}
			}

			AddError(FString::Printf(
				TEXT("%s has the right rows but different contents from %s, so ")
				TEXT("it is stale. Run tools/generate_datatable_assets.py. ")
				TEXT("First difference at %s"),
				Pair.Asset, Pair.File, *FirstDifference));
		}
	}

	return true;
}

/**
 * The DataTable asset holds a damage type LIMIT on every weapon, not a count.
 *
 * The column was renamed from DamageTypeSlots to MaxDamageTypes for #218,
 * because the old name read as a count and the value has meant a maximum since
 * #217. A rename like that can go wrong in a way nothing else here notices: the
 * import test above loads the CSV through the struct and would catch a column
 * with no matching property, but the asset comparison would not, because both
 * halves would import the same missing column the same way and agree.
 *
 * So this reads the shipped asset and checks the numbers are the design's
 * limits. Four for a one-hander and eight for a two-hander, from the Weapon
 * Bases table in docs/Cataclysm_GDD_v2.md, and zero on anything that is not a
 * weapon. Written here rather than read from the table under test.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemBaseMaxDamageTypesTest,
	"Cataclysm.Data.ItemBasesHoldADamageTypeLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemBaseMaxDamageTypesTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = LoadObject<UDataTable>(
		nullptr, TEXT("/Game/Data/DT_ItemBases.DT_ItemBases"));
	if (!Table)
	{
		AddError(TEXT("DT_ItemBases does not exist. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	int32 OneHanders = 0;
	int32 TwoHanders = 0;
	int32 NotWeapons = 0;

	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FCataclysmItemBaseRow* Row =
			reinterpret_cast<const FCataclysmItemBaseRow*>(Pair.Value);

		if (Row->Hands == 1)
		{
			++OneHanders;
			TestEqual(FString::Printf(
				TEXT("%s is one-handed and tops out at four damage types"),
				*Row->BaseName), Row->MaxDamageTypes, 4);
		}
		else if (Row->Hands == 2)
		{
			++TwoHanders;
			TestEqual(FString::Printf(
				TEXT("%s is two-handed and tops out at eight damage types"),
				*Row->BaseName), Row->MaxDamageTypes, 8);
		}
		else
		{
			++NotWeapons;
			TestEqual(FString::Printf(
				TEXT("%s is not a weapon and holds no damage types"),
				*Row->BaseName), Row->MaxDamageTypes, 0);
		}
	}

	// Without these the loop above passes on an empty table, which is the
	// failure the rename could actually cause.
	TestTrue(TEXT("the table has one-handed weapons"), OneHanders > 0);
	TestTrue(TEXT("the table has two-handed weapons"), TwoHanders > 0);
	TestTrue(TEXT("the table has bases that are not weapons"), NotWeapons > 0);

	return true;
}

/**
 * The effect palette arrives in the engine as colours and tags, not as defaults.
 *
 * WHAT THIS ADDS THAT THE PYTHON TESTS CANNOT. Two things, and both need the
 * engine.
 *
 * FIRST, THE TYPES. FCataclysmElementVisualRow is the only row struct in this
 * project holding an FLinearColor or an FGameplayTag, and both are imported from
 * text by code that reports nothing when it fails: a cell Unreal cannot parse
 * leaves the property at its C++ default and the row still imports. Comparing
 * CSV headers against property names, which is what
 * tools/tests/test_csv_columns_match_their_row_structs.py does, cannot see that
 * -- the names match perfectly and the values are gone. So every default on that
 * struct is deliberately a value the table never carries, and this test is what
 * makes that arrangement worth having.
 *
 * SECOND, THE CONVERSION. tools/generate_datatables.py converts the design
 * document's sRGB hex to linear in Python. The engine has its own conversion,
 * FLinearColor::FromSRGBColor, which is what ACataclysmTelegraphMarker uses on
 * the same document's hex. Those are two implementations of one curve, and
 * nothing compared them until here. One row is checked in full against the
 * engine's own function, which is all it takes to catch a curve that disagrees;
 * tools/tests/test_element_visuals_match_the_design.py pins all eight rows to
 * the design document, so the palette itself is covered there and is not copied
 * into this file.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmElementVisualsTest,
	"Cataclysm.Data.ElementVisualsCarryTheDesignedValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmElementVisualsTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = LoadObject<UDataTable>(
		nullptr, TEXT("/Game/Data/DT_ElementVisuals.DT_ElementVisuals"));
	if (!Table)
	{
		AddError(TEXT("DT_ElementVisuals does not exist. Run "
					  "tools/generate_datatable_assets.py through "
					  "tools/run_editor_python.py."));
		return false;
	}

	// The CSV is written to six decimal places, so the engine's own conversion
	// and the generator's agree to about a millionth. A tolerance and not an
	// equality for that reason alone.
	const float Tolerance = 1.0e-4f;

	const TCHAR* ElementPrefix = TEXT("Element.");

	TSet<FString> PrimariesSeen;
	int32 Checked = 0;

	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FCataclysmElementVisualRow* Row =
			reinterpret_cast<const FCataclysmElementVisualRow*>(Pair.Value);
		const FString Key = Pair.Key.ToString();

		// An FGameplayTag that failed to import, or that names something the
		// engine does not declare, is invalid. A skill asking an invalid tag for
		// its colours matches no row and still plays, with no complaint.
		if (!Row->ElementTag.IsValid())
		{
			AddError(FString::Printf(
				TEXT("row %s has no valid ElementTag, so nothing holding a "
					 "damage type tag can find it."), *Key));
			continue;
		}

		const FString TagText = Row->ElementTag.ToString();
		TestTrue(FString::Printf(TEXT("%s is keyed on a damage type"), *Key),
			TagText.StartsWith(ElementPrefix));

		// The row key is the tag's leaf, which is what lets anything holding a
		// tag reach its row without a second lookup table.
		FString Leaf;
		TagText.Split(TEXT("."), nullptr, &Leaf, ESearchCase::CaseSensitive,
					  ESearchDir::FromEnd);
		TestEqual(FString::Printf(
			TEXT("row %s is named after the leaf of %s"), *Key, *TagText),
			Leaf, Key);

		// EVERY ROW HAVING A DIFFERENT PRIMARY IS THE CHECK THAT CATCHES THE
		// WHOLE TABLE FAILING TO IMPORT. Eight rows all sitting on the struct's
		// white default would pass every per-row check that only asks whether a
		// colour is present.
		const FString Primary = Row->PrimaryColour.ToString();
		TestFalse(FString::Printf(
			TEXT("%s has a primary colour no other damage type has"), *Key),
			PrimariesSeen.Contains(Primary));
		PrimariesSeen.Add(Primary);

		// The secondary's job is to stay readable where the primary matches the
		// floor, which it can only do by being much darker.
		TestTrue(FString::Printf(
			TEXT("%s's secondary is darker than its primary"), *Key),
			Row->SecondaryColour.GetLuminance() < Row->PrimaryColour.GetLuminance());

		// 1.0 and not 0.0. The struct defaults these to zero precisely so that
		// this assertion can fail: the generator refuses a zero, so a row
		// reading zero here did not import.
		TestEqual(FString::Printf(TEXT("%s emissive multiplier"), *Key),
			Row->EmissiveMultiplier, 1.0f, Tolerance);
		TestEqual(FString::Printf(TEXT("%s spawn rate scale"), *Key),
			Row->SpawnRateScale, 1.0f, Tolerance);
		TestEqual(FString::Printf(TEXT("%s velocity scale"), *Key),
			Row->VelocityScale, 1.0f, Tolerance);

		++Checked;
	}

	// Without this the loop above passes on an empty table, which is what a
	// stale or unbuilt asset actually looks like.
	TestEqual(TEXT("every damage type was checked"), Checked, 8);

	// The two conversions compared. Demonic, from the effect palette table in
	// section XIII of docs/Cataclysm_GDD_v2.md.
	const FCataclysmElementVisualRow* Demonic =
		Table->FindRow<FCataclysmElementVisualRow>(
			FName(TEXT("Demonic")), TEXT("ElementVisuals conversion check"));
	if (!Demonic)
	{
		AddError(TEXT("DT_ElementVisuals has no row named Demonic."));
		return false;
	}

	const FLinearColor ExpectedPrimary =
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("FF7A2E")));
	const FLinearColor ExpectedSecondary =
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("3A0A02")));

	TestTrue(FString::Printf(
		TEXT("Demonic's primary is #FF7A2E converted to linear. Expected %s, "
			 "got %s"), *ExpectedPrimary.ToString(),
		*Demonic->PrimaryColour.ToString()),
		Demonic->PrimaryColour.Equals(ExpectedPrimary, Tolerance));

	TestTrue(FString::Printf(
		TEXT("Demonic's secondary is #3A0A02 converted to linear. Expected %s, "
			 "got %s"), *ExpectedSecondary.ToString(),
		*Demonic->SecondaryColour.ToString()),
		Demonic->SecondaryColour.Equals(ExpectedSecondary, Tolerance));

	return true;
}
#endif // WITH_AUTOMATION_TESTS
