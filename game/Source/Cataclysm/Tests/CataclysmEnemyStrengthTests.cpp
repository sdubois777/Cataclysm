// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

// For a floor-built Boss Brute's melee critical on a level 6 character. Issue #2874.
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Player/CataclysmPlayerState.h"
#include "Player/CataclysmPowerScore.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyRarity.h"
#include "Data/CataclysmDataRows.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmEnemyScore.h"
#include "Dungeon/CataclysmEnemyStrength.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/UnrealType.h"

/**
 * Tests for the strength a creature on a dungeon floor is built with. Issue #1569.
 *
 * REGISTERED UNDER `Cataclysm.EnemyStrength.`, with the trailing dot when narrowing a run.
 *
 * THE FIRST TEST HOLDS THE GAME TO THE SIMULATION. `game/Tests/EnemyStrength/ExpectedFigures.csv` is written by
 * `tools/generate_enemy_strength_fixture.py` from `enemy_stats.strength_on_floor`, and
 * `tools/tests/test_enemy_strength_fixture_is_current.py` fails when it is stale. The rest each check one ruling of
 * 2026-10-30 on its own, so a failure names which.
 */
namespace CataclysmEnemyStrengthTest
{
	FString FixturePath()
	{
		return FPaths::ProjectDir() / TEXT("Tests") / TEXT("EnemyStrength") / TEXT("ExpectedFigures.csv");
	}

	bool TypeNamed(const FString& Name, ECataclysmDungeonType& Out)
	{
		if (Name == TEXT("Basic")) { Out = ECataclysmDungeonType::Basic; return true; }
		if (Name == TEXT("Quest")) { Out = ECataclysmDungeonType::Quest; return true; }
		if (Name == TEXT("FallenCity")) { Out = ECataclysmDungeonType::FallenCity; return true; }
		if (Name == TEXT("Cataclysm")) { Out = ECataclysmDungeonType::Cataclysm; return true; }
		return false;
	}

	/** The fixture's `CityTier` column, by the enum's value names. Issue #2749's ring slice. */
	bool RingNamed(const FString& Name, ECataclysmCityTier& Out)
	{
		if (Name == TEXT("Outpost")) { Out = ECataclysmCityTier::Outpost; return true; }
		if (Name == TEXT("Bulwark")) { Out = ECataclysmCityTier::Bulwark; return true; }
		if (Name == TEXT("Sanctuary")) { Out = ECataclysmCityTier::Sanctuary; return true; }
		if (Name == TEXT("Pillar")) { Out = ECataclysmCityTier::Pillar; return true; }
		return false;
	}

	FCataclysmScoredFloor Floor(int32 Tier, ECataclysmDungeonType Type, int32 Total, int32 Which,
								float ModifierScore = 0.0f, ECataclysmCityTier Ring = ECataclysmCityTier::Outpost)
	{
		FCataclysmScoredFloor Out;
		Out.DifficultyTier = Tier;
		Out.Type = Type;
		Out.SubType = ECataclysmDungeonSubType::None;
		Out.TotalFloors = Total;
		Out.FloorNumber = Which;
		Out.ModifierScore = ModifierScore;
		Out.CityTier = Ring;
		return Out;
	}

	/** Common's row of `DT_EnemyRarities`, found by its step as the code finds it. */
	const FCataclysmEnemyRarityRow* CommonRow(const UDataTable* Rarities)
	{
		const FCataclysmEnemyRarityRow* Found = nullptr;
		if (Rarities)
		{
			Rarities->ForeachRow<FCataclysmEnemyRarityRow>(TEXT("CataclysmEnemyStrengthTest"),
				[&Found](const FName&, const FCataclysmEnemyRarityRow& Row)
				{
					if (Row.Step == 0)
					{
						Found = &Row;
					}
				});
		}
		return Found;
	}

	bool Near(double A, double B)
	{
		return FMath::Abs(A - B) <= FMath::Max(0.01, 1e-4 * FMath::Abs(B));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyStrengthEveryFigureIsTheModels,
	"Cataclysm.EnemyStrength.EveryFigureIsTheModels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyStrengthEveryFigureIsTheModels::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnemyStrengthTest;

	const UDataTable* Rarities = UCataclysmEnemyRarity::LoadEnemyRarityTable();
	const UDataTable* Archetypes = UCataclysmEnemyStrength::LoadEnemyArchetypeTable();
	if (!TestNotNull(TEXT("DT_EnemyRarities"), Rarities) || !TestNotNull(TEXT("DT_EnemyArchetypes"), Archetypes))
	{
		return false;
	}

	FString Text;
	if (!TestTrue(FString::Printf(TEXT("the fixture at %s can be read"), *FixturePath()),
				  FFileHelper::LoadFileToString(Text, *FixturePath())))
	{
		return false;
	}

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	int32 Checked = 0;
	int32 Wrong = 0;
	int32 OffTheOutposts = 0;
	for (int32 Index = 1; Index < Lines.Num(); ++Index)
	{
		// TWELVE CELLS SINCE ISSUE #2749'S RING SLICE: the ring the dungeon was rolled on is the eighth, after the
		// modifier score, and the four figures follow it.
		TArray<FString> Cells;
		Lines[Index].ParseIntoArray(Cells, TEXT(","), false);
		ECataclysmDungeonType Type = ECataclysmDungeonType::Basic;
		ECataclysmCityTier Ring = ECataclysmCityTier::Outpost;
		if (Cells.Num() != 12 || !TypeNamed(Cells[3], Type) || !RingNamed(Cells[7], Ring))
		{
			AddError(FString::Printf(TEXT("fixture line %d is not a row this test reads: %s"), Index + 1, *Lines[Index]));
			++Wrong;
			continue;
		}

		const FName Row(*Cells[0]);
		const int32 Step = FCString::Atoi(*Cells[1]);
		const FCataclysmScoredFloor Where = Floor(FCString::Atoi(*Cells[2]), Type, FCString::Atoi(*Cells[4]),
												  FCString::Atoi(*Cells[5]), FCString::Atof(*Cells[6]), Ring);
		OffTheOutposts += Ring != ECataclysmCityTier::Outpost ? 1 : 0;

		FCataclysmStartingFigures Built;
		float HealthScale = 1.0f;
		float DamageScale = 1.0f;
		float ArmourScale = 1.0f;
		const bool bBuilt = UCataclysmEnemyStrength::StartingFiguresFor(Rarities, Archetypes, Row, Where, Step, Built);
		UCataclysmEnemyRarity::ScalingFromCommon(Rarities, Step, HealthScale, DamageScale, ArmourScale);

		// THE CREATURE AS IT STANDS: its Starting figures times its rarity's ratio, which is what
		// `ApplyStartingAttributes` applies and what the fixture holds.
		const double Strength = UCataclysmEnemyScore::StrengthScoreFor(Where, Step);
		const double Health = static_cast<double>(Built.Health) * HealthScale;
		const double Damage = static_cast<double>(Built.AttackDamage) * DamageScale;
		const double Armour = static_cast<double>(Built.Armour) * ArmourScale;
		const bool bRight = bBuilt
			&& Near(Strength, FCString::Atod(*Cells[8])) && Near(Health, FCString::Atod(*Cells[9]))
			&& Near(Damage, FCString::Atod(*Cells[10])) && Near(Armour, FCString::Atod(*Cells[11]));
		if (!bRight)
		{
			// THE FIRST FEW ARE NAMED, so a wholesale break says what broke without printing 973 lines.
			if (Wrong < 5)
			{
				AddError(FString::Printf(
					TEXT("%s rung %d, tier %s, %s floor %s of %s, modifiers %s, %s: built strength %.4f health %.4f "
						 "damage %.4f armour %.4f; the model says %s, %s, %s, %s"),
					*Cells[0], Step, *Cells[2], *Cells[3], *Cells[5], *Cells[4], *Cells[6], *Cells[7], Strength, Health,
					Damage, Armour, *Cells[8], *Cells[9], *Cells[10], *Cells[11]));
			}
			++Wrong;
		}
		++Checked;
	}

	TestTrue(TEXT("the fixture holds a whole grid of rows"), Checked >= 800);
	// 133 SINCE ISSUE #2749'S RING SLICE: tier 1 on a Bulwark, a Sanctuary and the Pillar for every creature and
	// rarity, and tier 2 on a Sanctuary for each creature's Common.
	TestEqual(TEXT("rows on a ring other than an Outpost"), OffTheOutposts, 133);
	TestEqual(TEXT("rows where the game builds what the model builds"), Checked - Wrong, Checked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyStrengthReliefOnTheStrengthPath,
	"Cataclysm.EnemyStrength.TheReliefComesOffTheScoreACreatureIsBuiltFromAndNotOffWhatItIsWorth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyStrengthReliefOnTheStrengthPath::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnemyStrengthTest;

	// THE MIDDLE FLOOR OF A DEEP TIER 4 DUNGEON, where the minimum does not reach, so only the relief is at work.
	const FCataclysmScoredFloor Where = Floor(4, ECataclysmDungeonType::Basic, 50, 25);
	const double Worth = UCataclysmEnemyScore::ScoreFor(Where, 0);
	const double Relief = UCataclysmEnemyScore::ContentRelief(4) * UCataclysmEnemyScore::TierWidth(4);

	TestTrue(TEXT("tier 4 has a relief"), Relief > 0.0);
	TestEqual(TEXT("a creature is built from the score less 0.45 tier widths"),
			  UCataclysmEnemyScore::StrengthScoreFor(Where, 0), Worth - Relief, 1e-6);
	TestEqual(TEXT("the relief at tier 4 is 0.45 tier widths"), UCataclysmEnemyScore::ContentRelief(4), 0.45, 1e-9);

	// THE CONTROL: tier 1 has none, so there the two scores are the same.
	const FCataclysmScoredFloor TierOne = Floor(1, ECataclysmDungeonType::Basic, 50, 25);
	TestEqual(TEXT("at tier 1 a creature is built from the score it is worth"),
			  UCataclysmEnemyScore::StrengthScoreFor(TierOne, 0),
			  static_cast<double>(UCataclysmEnemyScore::ScoreFor(TierOne, 0)), 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyStrengthTheMinimum,
	"Cataclysm.EnemyStrength.ADeepDungeonsFirstFloorIsBuiltAsFloorOneOfATwelveFloorDungeon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyStrengthTheMinimum::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnemyStrengthTest;

	const FCataclysmScoredFloor Deep = Floor(1, ECataclysmDungeonType::Basic, 50, 1);
	const FCataclysmScoredFloor Twelve = Floor(1, ECataclysmDungeonType::Basic, 12, 1);

	TestTrue(TEXT("the design scores floor 1 of a 50-floor tier 1 dungeon below nothing"),
			 UCataclysmEnemyScore::ScoreFor(Deep, 0) < 0);
	TestEqual(TEXT("and the game builds it as floor 1 of a 12-floor dungeon"),
			  UCataclysmEnemyScore::StrengthScoreFor(Deep, 0),
			  static_cast<double>(UCataclysmEnemyScore::ScoreFor(Twelve, 0)), 1e-6);

	// THE CONTROL: a floor above the minimum is built from its own score.
	const FCataclysmScoredFloor Tenth = Floor(1, ECataclysmDungeonType::Basic, 50, 10);
	TestTrue(TEXT("floor 10 scores above the minimum"),
			 UCataclysmEnemyScore::ScoreFor(Tenth, 0) > UCataclysmEnemyScore::ScoreFor(Twelve, 0));
	TestEqual(TEXT("so it is built from its own score"), UCataclysmEnemyScore::StrengthScoreFor(Tenth, 0),
			  static_cast<double>(UCataclysmEnemyScore::ScoreFor(Tenth, 0)), 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyStrengthDamageScale,
	"Cataclysm.EnemyStrength.DamageAloneIsScaledByTheTier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyStrengthDamageScale::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnemyStrengthTest;

	// ISSUE #2749, ruled 2026-11-19: tier 1 is a twentieth, refitted to a character that starts at level 1, and
	// tiers 2 to 8 stay on the line rising evenly from a fifth at tier 1 to one at tier 8, where they were.
	TestEqual(TEXT("a twentieth at tier 1"), UCataclysmEnemyScore::DamageScale(1), 0.05, 1e-9);
	TestEqual(TEXT("tier 2 where it was, on the line from a fifth"), UCataclysmEnemyScore::DamageScale(2),
			  0.20 + 0.80 / 7.0, 1e-9);
	TestEqual(TEXT("one at tier 8"), UCataclysmEnemyScore::DamageScale(8), 1.0, 1e-9);
	TestEqual(TEXT("evenly between"), UCataclysmEnemyScore::DamageScale(4), 0.20 + 0.80 * 3.0 / 7.0, 1e-9);
	TestEqual(TEXT("held at tier 1 below the tiers there are"), UCataclysmEnemyScore::DamageScale(0), 0.05, 1e-9);

	const UDataTable* Rarities = UCataclysmEnemyRarity::LoadEnemyRarityTable();
	const UDataTable* Archetypes = UCataclysmEnemyStrength::LoadEnemyArchetypeTable();
	const FCataclysmEnemyRarityRow* Common = CommonRow(Rarities);
	const FCataclysmEnemyArchetypeRow* Brute = Archetypes
		? Archetypes->FindRow<FCataclysmEnemyArchetypeRow>(TEXT("Brute"), TEXT("CataclysmEnemyStrengthTest"), false)
		: nullptr;
	if (!TestNotNull(TEXT("Common's row"), Common) || !TestNotNull(TEXT("the Brute's row"), Brute))
	{
		return false;
	}

	const FCataclysmScoredFloor Where = Floor(1, ECataclysmDungeonType::Basic, 12, 6);
	FCataclysmStartingFigures Built;
	if (!TestTrue(TEXT("a tier 1 Brute is built"),
				  UCataclysmEnemyStrength::StartingFiguresFor(Rarities, Archetypes, TEXT("Brute"), Where, 0, Built)))
	{
		return false;
	}
	const double Strength = UCataclysmEnemyScore::StrengthScoreFor(Where, 0);
	TestEqual(TEXT("its damage is the score's times a twentieth"), static_cast<double>(Built.AttackDamage),
			  Strength * Common->DamagePerScore * Brute->DamageShare * 0.05, 0.01);
	TestEqual(TEXT("and its health is the score's, unscaled"), static_cast<double>(Built.Health),
			  Strength * Common->HealthPerScore * Brute->HealthShare, 0.01);
	TestEqual(TEXT("and its armour too"), static_cast<double>(Built.Armour),
			  Strength * Common->ArmorPerScore * Brute->ArmorShare, 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyStrengthRarityOnce,
	"Cataclysm.EnemyStrength.TheRarityRaisesTheScoreAndTheCreatureScalesItOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyStrengthRarityOnce::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnemyStrengthTest;

	const UDataTable* Rarities = UCataclysmEnemyRarity::LoadEnemyRarityTable();
	const UDataTable* Archetypes = UCataclysmEnemyStrength::LoadEnemyArchetypeTable();
	const FCataclysmEnemyRarityRow* Common = CommonRow(Rarities);
	const FCataclysmEnemyArchetypeRow* Imp = Archetypes
		? Archetypes->FindRow<FCataclysmEnemyArchetypeRow>(TEXT("Imp"), TEXT("CataclysmEnemyStrengthTest"), false)
		: nullptr;
	if (!TestNotNull(TEXT("Common's row"), Common) || !TestNotNull(TEXT("the Imp's row"), Imp))
	{
		return false;
	}

	// A LEGENDARY: its score includes the rarity's weight, and its Starting figures do NOT include the rarity's ratio,
	// which the creature applies itself. Both halves are the model's.
	const FCataclysmScoredFloor Where = Floor(2, ECataclysmDungeonType::Basic, 12, 6);
	constexpr int32 Legendary = 2;
	FCataclysmStartingFigures Built;
	if (!TestTrue(TEXT("a Legendary Imp is built"),
				  UCataclysmEnemyStrength::StartingFiguresFor(Rarities, Archetypes, TEXT("Imp"), Where, Legendary, Built)))
	{
		return false;
	}
	TestTrue(TEXT("a Legendary scores above a Common on the same floor"),
			 UCataclysmEnemyScore::StrengthScoreFor(Where, Legendary) > UCataclysmEnemyScore::StrengthScoreFor(Where, 0));
	TestEqual(TEXT("its Starting health is its score at Common's share, with no rarity ratio in it"),
			  static_cast<double>(Built.Health),
			  UCataclysmEnemyScore::StrengthScoreFor(Where, Legendary) * Common->HealthPerScore * Imp->HealthShare, 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyStrengthAFloorBuildsIt,
	"Cataclysm.EnemyStrength.ACreaturePlacedOnAFloorIsBuiltFromTheScore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyStrengthAFloorBuildsIt::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnemyStrengthTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* GameMode = World->SpawnActor<ACataclysmDungeonGameMode>();
	ACataclysmEnemyCharacter* Imp = World->SpawnActor<ACataclysmEnemyCharacter>(
		ACataclysmDungeonGameMode::ClassFor(ECataclysmDungeonCreature::Imp), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a dungeon game mode"), GameMode) || !TestNotNull(TEXT("an Imp"), Imp))
	{
		return false;
	}

	// A LEGENDARY, SO THE RUNG IS IN THE SCORE, set the way a person testing one rarity sets it.
	GameMode->ImpRarityStep = 2;
	GameMode->ApplyDesignedStats(Imp, ECataclysmDungeonCreature::Imp);

	FCataclysmStartingFigures Expected;
	if (!TestTrue(TEXT("the figures for this floor can be built"),
				  UCataclysmEnemyStrength::StartingFiguresFor(
					  UCataclysmEnemyRarity::LoadEnemyRarityTable(), UCataclysmEnemyStrength::LoadEnemyArchetypeTable(),
					  TEXT("Imp"), UCataclysmEnemyScore::FloorFor(GameMode, ACataclysmGameMode::DifficultyTierIn(GameMode)),
					  2, Expected)))
	{
		return false;
	}

	TestEqual(TEXT("the Imp is a Legendary"), Imp->RarityStep, 2);
	TestEqual(TEXT("its Starting health is the score's"), Imp->GetStartingMaxHealth(), Expected.Health, 0.01f);
	TestEqual(TEXT("its Starting damage is the score's, scaled by the tier"), Imp->GetStartingAttackDamage(),
			  Expected.AttackDamage, 0.01f);
	// THE CONTROL: not the sandbox's fixed figure, so the floor really built it. `ImpHealth` is protected, so it is read
	// through its reflected property rather than by name.
	const FFloatProperty* Fixed = FindFProperty<FFloatProperty>(ACataclysmGameMode::StaticClass(), TEXT("ImpHealth"));
	if (TestNotNull(TEXT("the sandbox's ImpHealth setting"), Fixed))
	{
		TestNotEqual(TEXT("and it is not the fixed sandbox Imp"), Imp->GetStartingMaxHealth(),
					 Fixed->GetPropertyValue_InContainer(GameMode));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyStrengthDamageByRing,
	"Cataclysm.EnemyStrength.AtTierOneDamageRisesByTheRingTheDungeonWasRolledOnAndAtTiersTwoToEightItDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyStrengthDamageByRing::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnemyStrengthTest;

	// ISSUE #2749'S RING SLICE. The project owner, 2026-11-19, "Rise by city ring"; the figures are the coordinating
	// session's of the same day. Typed, so a moved constant fails here.
	TestEqual(TEXT("an Outpost's at tier 1 is a twentieth"),
			  UCataclysmEnemyScore::DamageScale(1, ECataclysmCityTier::Outpost), 0.05, 1e-9);
	TestEqual(TEXT("a Bulwark's is 0.11"), UCataclysmEnemyScore::DamageScale(1, ECataclysmCityTier::Bulwark), 0.11, 1e-9);
	TestEqual(TEXT("a Sanctuary's is 0.13"),
			  UCataclysmEnemyScore::DamageScale(1, ECataclysmCityTier::Sanctuary), 0.13, 1e-9);
	TestEqual(TEXT("the Pillar's, where only the boss dungeon stands, is 0.10"),
			  UCataclysmEnemyScore::DamageScale(1, ECataclysmCityTier::Pillar), 0.10, 1e-9);
	TestEqual(TEXT("with no ring named, an Outpost's"), UCataclysmEnemyScore::DamageScale(1), 0.05, 1e-9);
	for (int32 Tier = 2; Tier <= 8; ++Tier)
	{
		TestEqual(FString::Printf(TEXT("tier %d: a Sanctuary's is an Outpost's"), Tier),
				  UCataclysmEnemyScore::DamageScale(Tier, ECataclysmCityTier::Sanctuary),
				  UCataclysmEnemyScore::DamageScale(Tier, ECataclysmCityTier::Outpost), 1e-9);
	}

	// AND IT MOVES THE DAMAGE ALONE: the same Brute on the same floor of an Outpost's and a Sanctuary's dungeon.
	const UDataTable* Rarities = UCataclysmEnemyRarity::LoadEnemyRarityTable();
	const UDataTable* Archetypes = UCataclysmEnemyStrength::LoadEnemyArchetypeTable();
	FCataclysmStartingFigures OnAnOutpost;
	FCataclysmStartingFigures OnASanctuary;
	if (!TestTrue(TEXT("a tier 1 Brute is built on both rings"),
				  UCataclysmEnemyStrength::StartingFiguresFor(
					  Rarities, Archetypes, TEXT("Brute"),
					  Floor(1, ECataclysmDungeonType::Basic, 12, 6, 0.0f, ECataclysmCityTier::Outpost), 0, OnAnOutpost)
				  && UCataclysmEnemyStrength::StartingFiguresFor(
					  Rarities, Archetypes, TEXT("Brute"),
					  Floor(1, ECataclysmDungeonType::Basic, 12, 6, 0.0f, ECataclysmCityTier::Sanctuary), 0,
					  OnASanctuary)))
	{
		return false;
	}
	TestEqual(TEXT("the Sanctuary's hits 0.13 / 0.05 times as hard"), OnASanctuary.AttackDamage,
			  OnAnOutpost.AttackDamage * 2.6f, 0.001f);
	TestEqual(TEXT("with the same health"), OnASanctuary.Health, OnAnOutpost.Health, 0.001f);
	TestEqual(TEXT("and the same armour"), OnASanctuary.Armour, OnAnOutpost.Armour, 0.001f);
	TestEqual(TEXT("and it is worth the same: the score a kill grants does not read the ring"),
			  UCataclysmEnemyScore::ScoreFor(Floor(1, ECataclysmDungeonType::Basic, 12, 6, 0.0f,
												   ECataclysmCityTier::Sanctuary), 0),
			  UCataclysmEnemyScore::ScoreFor(Floor(1, ECataclysmDungeonType::Basic, 12, 6), 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyStrengthABulwarksWalk,
	"Cataclysm.EnemyStrength.AWalkInADungeonRolledOnABulwarkBuildsItsCreaturesWithTheBulwarksDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyStrengthABulwarksWalk::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnemyStrengthTest;

	// THE WHOLE ROUTE: the empire dungeon's ring, carried across the module line by `BindEmpireDungeon`, read back by
	// `FloorFor`, and built into a creature by `ApplyDesignedStats`. Issue #2749's ring slice.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("a dungeon game mode"), Mode))
	{
		return false;
	}

	// A TIER 1 RUN, whose first surge lands on the Outposts, the only front line there is at the start.
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->Begin(/* Seed */ 1, ECataclysmSurgeMode::Static, /* LethalityRung */ 0, /* DifficultyTier */ 1);
	Run->AdvanceDay();
	if (!TestTrue(TEXT("set-up: the first surge put a dungeon on the map"), Run->Dungeons.Num() > 0))
	{
		return false;
	}
	Mode->SetEmpireRunForTests(Run);

	// ITS DEPTH ROLLED ON A BULWARK, AND STANDING ON A SANCTUARY, as a Quest dungeon that moved inward would: the ring it
	// was rolled on is the one that counts, ruled by the coordinating session on 2026-11-19.
	int32 SanctuaryId = INDEX_NONE;
	for (const FCataclysmCity& City : Run->Map->Cities)
	{
		if (City.Tier == ECataclysmCityTier::Sanctuary)
		{
			SanctuaryId = City.CityId;
			break;
		}
	}
	if (!TestNotEqual(TEXT("set-up: the map has a Sanctuary"), SanctuaryId, static_cast<int32>(INDEX_NONE)))
	{
		return false;
	}
	TestEqual(TEXT("set-up: the surge rolled it on an Outpost"), static_cast<int32>(Run->Dungeons[0].CityTier),
			  static_cast<int32>(ECataclysmCityTier::Outpost));
	Run->Dungeons[0].CityTier = ECataclysmCityTier::Bulwark;
	Run->Dungeons[0].CityId = SanctuaryId;
	const int32 DungeonId = Run->Dungeons[0].DungeonId;

	TestEqual(TEXT("before entering, a walk is on an Outpost"), static_cast<int32>(Mode->RunCityTier()),
			  static_cast<int32>(ECataclysmCityTier::Outpost));
	if (!TestTrue(TEXT("the dungeon is entered"), Mode->EnterEmpireDungeon(DungeonId)))
	{
		return false;
	}
	TestEqual(TEXT("entering carries the ring its depth was rolled on, not the one it stands on"),
			  static_cast<int32>(Mode->RunCityTier()), static_cast<int32>(ECataclysmCityTier::Bulwark));

	const int32 Tier = ACataclysmGameMode::DifficultyTierIn(Mode);
	TestEqual(TEXT("set-up: the walk is at tier 1"), Tier, 1);
	const FCataclysmScoredFloor Read = UCataclysmEnemyScore::FloorFor(Mode, Tier);
	TestEqual(TEXT("and the score model reads the ring off the game mode"), static_cast<int32>(Read.CityTier),
			  static_cast<int32>(ECataclysmCityTier::Bulwark));

	// THE SAME FLOOR ON EACH RING, set by hand, so the figures expected do not come through the reading under test.
	FCataclysmScoredFloor OnAnOutpost = Read;
	OnAnOutpost.CityTier = ECataclysmCityTier::Outpost;
	FCataclysmScoredFloor OnABulwark = Read;
	OnABulwark.CityTier = ECataclysmCityTier::Bulwark;
	const UDataTable* Rarities = UCataclysmEnemyRarity::LoadEnemyRarityTable();
	const UDataTable* Archetypes = UCataclysmEnemyStrength::LoadEnemyArchetypeTable();
	FCataclysmStartingFigures AtAnOutpost;
	FCataclysmStartingFigures AtABulwark;
	if (!TestTrue(TEXT("set-up: a Common Imp is built for this floor on both rings"),
				  UCataclysmEnemyStrength::StartingFiguresFor(Rarities, Archetypes, TEXT("Imp"), OnAnOutpost, 0,
															  AtAnOutpost)
				  && UCataclysmEnemyStrength::StartingFiguresFor(Rarities, Archetypes, TEXT("Imp"), OnABulwark, 0,
																 AtABulwark)))
	{
		return false;
	}
	TestEqual(TEXT("on this floor a Bulwark's Imp hits 0.11 / 0.05 times as hard as an Outpost's"),
			  AtABulwark.AttackDamage, AtAnOutpost.AttackDamage * 2.2f, 0.001f);

	// AND A CREATURE PLACED ON THE FLOOR IS BUILT WITH THE BULWARK'S.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACataclysmEnemyCharacter* Imp = World->SpawnActor<ACataclysmEnemyCharacter>(
		ACataclysmDungeonGameMode::ClassFor(ECataclysmDungeonCreature::Imp), FVector::ZeroVector, FRotator::ZeroRotator,
		SpawnParams);
	if (!TestNotNull(TEXT("an Imp"), Imp))
	{
		return false;
	}
	Mode->ImpRarityStep = 0;
	Mode->ApplyDesignedStats(Imp, ECataclysmDungeonCreature::Imp);
	TestEqual(TEXT("set-up: the Imp is a Common"), Imp->RarityStep, 0);
	TestEqual(TEXT("its Starting damage is the Bulwark's"), Imp->GetStartingAttackDamage(), AtABulwark.AttackDamage,
			  0.001f);
	TestEqual(TEXT("and its Starting health the same as on an Outpost, which the ring does not move"),
			  Imp->GetStartingMaxHealth(), AtAnOutpost.Health, 0.01f);
	// THE CONTROL: not an Outpost's damage, so the ring really reached the creature.
	TestTrue(FString::Printf(TEXT("CONTROL: and not an Outpost's %.3f"), AtAnOutpost.AttackDamage),
			 Imp->GetStartingAttackDamage() > AtAnOutpost.AttackDamage * 1.5f);

	// LEAVING PUTS IT BACK: a plain floor walked afterwards is built as every floor was before the ring.
	Mode->LeaveEmpireDungeon();
	TestEqual(TEXT("leaving the dungeon puts the walk back on an Outpost"), static_cast<int32>(Mode->RunCityTier()),
			  static_cast<int32>(ECataclysmCityTier::Outpost));
	return true;
}

namespace CataclysmEnemyStrengthTest
{
	/**
	 * The design's hit for the owner's Boss Brute of 2026-11-24, before mitigation: `enemy_stats.strength_on_floor(
	 * "Boss", 1, "Basic", "Timed", 10, 6, kind="Brute", ring="Outpost").damage_per_hit`, floor 6 of a 10-floor Timed
	 * dungeon on an Outpost at tier 1 with no modifier score. Issue #2874. Held to the simulation's figure by
	 * `tools/tests/test_a_brand_is_built_by_hits_and_a_dead_creature_lands_nothing.py`.
	 */
	constexpr double OutpostBossBruteDesignHit = 72.3205;

	/** The level the owner's character held when the Brute killed it, and the reference character's here. */
	constexpr int32 MeleeCriticalReferenceLevel = 6;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyStrengthBossBruteMeleeCritical,
	"Cataclysm.EnemyStrength.ATierOneOutpostBossBrutesMeleeCriticalOnALevelSixCharacterIsTheDesigns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * ISSUE #2874, THE MEASUREMENT. The owner was killed on 2026-11-24 by a blow from a tier 1 Boss Brute that took 396 of
 * 430 health, read at first as the Brute's melee critical. It was not -- it was its Infernal Brand's explosion, which
 * `Cataclysm.EnemyModifiers.` covers -- and this measures the melee critical anyway, so the question is answered by a
 * run and not by reading.
 *
 * THE BRUTE AS THE FLOOR BUILDS IT, the steps of `ACataclysmDungeonGameMode::ApplyDesignedStats` in its order: the rung,
 * then `UCataclysmEnemyStrength::StartingFiguresFor` on the floor, then its health, damage and armour. The floor is set
 * by hand to the owner's: floor 6 of a 10-floor Timed Basic dungeon on an Outpost at tier 1, no modifier score
 * (`Cataclysm.EnemyStrength.ACreaturePlacedOnAFloorIsBuiltFromTheScore` holds that the floor reaches the same function).
 * No modifier is drawn: the four it carried change no figure of its swing (Hellfire Aura, Infernal Brand, Bloodthirsty's
 * leech and Perfect Aim's "cannot be evaded").
 *
 * THE LEVEL 6 REFERENCE CHARACTER: the starting class at level 6, possessed and wearing what a new character is given,
 * which is the character `tools/tests/test_the_starting_character_survives_a_pack.py` and `sim/analyse_opening.py`
 * measure. Every roll pinned: always critical, never blocked; the hit at tier 1.
 *
 * WHAT IS ASSERTED. Before mitigation: the Brute's attack damage is the design's 72.32 and its multiplier the Brute's
 * 200, so its critical is the design's 144.64. After it: one critical melee swing lands, within 1%, the design's
 * critical times what the design's layers leave of a hit for this character -- armour at tier 1 and resistance, each
 * less the Overwhelm this Brute applies (issue #2824 slice b), and the flat damage reduction -- and it is less than the
 * character's whole health. Prints "BOSS BRUTE MELEE CRITICAL".
 */
bool FCataclysmEnemyStrengthBossBruteMeleeCritical::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnemyStrengthTest;
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	// EVERY HIT AT TIER 1, the owner's tier, and never blocked. Put back however the test returns.
	IConsoleVariable* Tier = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.DifficultyTier"));
	IConsoleVariable* BlockRoll = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.BlockRoll"));
	if (!TestNotNull(TEXT("set-up: the difficulty tier console variable"), Tier)
		|| !TestNotNull(TEXT("set-up: the block roll console variable"), BlockRoll))
	{
		return false;
	}
	const int32 TierBefore = Tier->GetInt();
	const float BlockRollBefore = BlockRoll->GetFloat();
	Tier->Set(1, ECVF_SetByConsole);
	BlockRoll->Set(100.0f, ECVF_SetByConsole);
	ON_SCOPE_EXIT
	{
		Tier->Set(TierBefore, ECVF_SetByConsole);
		BlockRoll->Set(BlockRollBefore, ECVF_SetByConsole);
	};

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	// AFTER THE WORLD, which silences the critical roll as it is made.
	const CataclysmTestWorld::FScopedCritRoll EveryRollCritical(0.0f);

	// THE BRUTE, AS THE FLOOR BUILDS IT.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		ACataclysmBruteCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	UAbilitySystemComponent* BrutesSystem = Brute ? Brute->GetAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("set-up: a Brute with an ability system"), BrutesSystem))
	{
		return false;
	}
	// ITS BRAIN IS TAKEN AWAY, so nothing but the test orders a swing.
	if (AController* Brain = Brute->GetController())
	{
		Brain->UnPossess();
	}

	FCataclysmScoredFloor Where = Floor(1, ECataclysmDungeonType::Basic, 10, 6, 0.0f, ECataclysmCityTier::Outpost);
	Where.SubType = ECataclysmDungeonSubType::Timed;
	Brute->SetRarityStep(ACataclysmEnemyCharacter::FirstBossRarityStep);
	FCataclysmStartingFigures Built;
	if (!TestTrue(TEXT("set-up: a Boss Brute's figures are built for the owner's floor"),
				  UCataclysmEnemyStrength::StartingFiguresFor(UCataclysmEnemyRarity::LoadEnemyRarityTable(),
															  UCataclysmEnemyStrength::LoadEnemyArchetypeTable(),
															  Brute->ArchetypeRow, Where, Brute->RarityStep, Built)))
	{
		return false;
	}
	Brute->SetHealth(Built.Health);
	Brute->SetAttackDamage(Built.AttackDamage);
	Brute->SetArmour(Built.Armour);

	const float AttackDamage = BrutesSystem->GetNumericAttribute(Combat::GetAttackDamageAttribute());
	const float CritMultiplier = BrutesSystem->GetNumericAttribute(Combat::GetCritMultiplierAttribute());
	const double DesignCritical =
		OutpostBossBruteDesignHit * ACataclysmBruteCharacter::DesignedCritMultiplierPercent / 100.0;

	// THE LEVEL 6 CHARACTER, two metres away.
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Player = World->SpawnActor<ACataclysmPlayerCharacter>(
		ACataclysmPlayerCharacter::StaticClass(), FVector(200.0f, 0.0f, 0.0f), FRotator::ZeroRotator, SpawnParams);
	if (!TestTrue(TEXT("set-up: a player state, a controller and a character"), State && Controller && Player))
	{
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Player);
	State->SetLevelAndExperience(MeleeCriticalReferenceLevel, 0);
	// ITS STAT LINE AGAIN AT LEVEL 6: possession applied it at the level held then. The game's own path, which fills
	// its pools.
	Player->ApplyChosenClassStats();
	UAbilitySystemComponent* PlayersSystem = Player->GetAbilitySystemComponent();
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	if (!TestNotNull(TEXT("set-up: the character's ability system"), PlayersSystem)
		|| !TestNotNull(TEXT("set-up: the announcer"), Events))
	{
		return false;
	}
	const auto Read = [PlayersSystem](const FGameplayAttribute& Attribute)
	{
		return PlayersSystem->HasAttributeSetForAttribute(Attribute) ? PlayersSystem->GetNumericAttribute(Attribute)
																	 : 0.0f;
	};
	const float MaxHealth = Read(Vital::GetMaxHealthAttribute());
	const float Armour = Read(Combat::GetArmorAttribute());
	const float Reduction = Read(Combat::GetDamageReductionAttribute());
	const float Resistance = Read(UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute());
	const float Overwhelm = UCataclysmPowerScore::OverwhelmPointsAgainst(Player, Brute);

	// WHAT THE DESIGN'S LAYERS LEAVE OF A HIT FOR THIS CHARACTER, from its own figures and the design's formulas: the
	// layers the reference character has (armour at tier 1 and the flat reduction, the pack test's
	// `share_of_a_hit_taken`), resistance, which it holds none of, and the Overwhelm off armour and resistance.
	const float ArmourKeeps =
		1.0f - FMath::Max(0.0f, UCataclysmDamageCalculation::ArmorReduction(Armour, 1) - Overwhelm) / 100.0f;
	const float ResistanceKeeps = 1.0f - UCataclysmDamageCalculation::EffectiveResistance(Resistance, Overwhelm) / 100.0f;
	const float ReductionKeeps = 1.0f - UCataclysmDamageCalculation::EffectiveDamageReduction(Reduction) / 100.0f;
	const double DesignLanded = DesignCritical * ArmourKeeps * ResistanceKeeps * ReductionKeeps;

	// THE SWING, heard as the character resolved it.
	int32 Swings = 0;
	float Sent = 0.0f;
	float Landed = 0.0f;
	bool bCritical = false;
	bool bMelee = false;
	const FDelegateHandle Listening = Events->OnHit.AddLambda(
		[&Swings, &Sent, &Landed, &bCritical, &bMelee, Player, Brute](const FCataclysmHitNotice& Notice)
		{
			if (Notice.Target == Player && Notice.Attacker == Brute && !Notice.bDamageOverTime)
			{
				++Swings;
				Sent = Notice.Sent;
				Landed = Notice.Landed;
				bCritical = Notice.bCritical;
				bMelee = Notice.bIsMelee;
			}
		});
	ON_SCOPE_EXIT { Events->OnHit.Remove(Listening); };

	// ORDERED, AND LANDED WHEN THE SWING CONNECTS, as the creature's own frame lands it. The clock is moved by hand; no
	// world in a test is ticked.
	Brute->AttackTarget(Player);
	if (!TestTrue(TEXT("set-up: the swing started and its blow waits"), Brute->HasPendingBasicAttack()))
	{
		return false;
	}
	World->TimeSeconds = static_cast<double>(Brute->PendingBasicAttackLandsAtSeconds) + 0.01;
	TestTrue(TEXT("set-up: the swing connected and its blow was dealt"), Brute->LandPendingBasicAttack());

	AddInfo(FString::Printf(
		TEXT("BOSS BRUTE MELEE CRITICAL: Starting damage %.4f, attack damage %.4f (design %.4f), critical multiplier "
			 "%.0f; level %d character: health %.1f, armour %.2f, damage reduction %.2f, Demonic resistance %.2f, "
			 "Overwhelm %.2f points; keeps %.4f of a hit; %d swing(s) heard, sent %.4f, landed %.4f, %s, %s; the design "
			 "lands %.4f, %.1f%% of the character's health. Its Infernal Brand would explode for %.2f before mitigation "
			 "(five of its hits, never a critical strike)."),
		Brute->GetStartingAttackDamage(), AttackDamage, OutpostBossBruteDesignHit, CritMultiplier,
		MeleeCriticalReferenceLevel, MaxHealth, Armour, Reduction, Resistance, Overwhelm,
		ArmourKeeps * ResistanceKeeps * ReductionKeeps, Swings, Sent, Landed,
		bCritical ? TEXT("critical") : TEXT("not critical"), bMelee ? TEXT("melee") : TEXT("not melee"), DesignLanded,
		MaxHealth > 0.0f ? 100.0 * DesignLanded / MaxHealth : -1.0, AttackDamage * 5.0f));

	// BEFORE MITIGATION: THE DESIGN'S HIT AND THE BRUTE'S MULTIPLIER.
	TestEqual(TEXT("the floor-built Boss Brute's attack damage is the design's, within 0.5%"),
			  static_cast<double>(AttackDamage), OutpostBossBruteDesignHit, OutpostBossBruteDesignHit * 0.005);
	TestEqual(TEXT("and its critical multiplier is the Brute's 200"), CritMultiplier,
			  ACataclysmBruteCharacter::DesignedCritMultiplierPercent, 0.01f);

	// THE SWING.
	TestEqual(TEXT("set-up: one swing reached the character"), Swings, 1);
	TestTrue(TEXT("set-up: it was a critical strike"), bCritical);
	TestTrue(TEXT("and a melee one, as the log would call it"), bMelee);
	TestTrue(TEXT("set-up: the character outlived it, so what landed was not cut to the health left"),
			 !UCataclysmSkillEffects::IsDead(Player));

	// AFTER MITIGATION: THE DESIGN'S FIGURE, WITHIN 1%.
	TestEqual(TEXT("THE CRITICAL SWING LANDS THE DESIGN'S CRITICAL AFTER THE DESIGN'S LAYERS, within 1%"),
			  static_cast<double>(Landed), DesignLanded, DesignLanded * 0.01);
	TestTrue(FString::Printf(TEXT("AND IT DOES NOT ONE-SHOT A LEVEL 6 CHARACTER: %.1f of %.1f"), Landed, MaxHealth),
			 Landed > 0.0f && Landed < MaxHealth);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
