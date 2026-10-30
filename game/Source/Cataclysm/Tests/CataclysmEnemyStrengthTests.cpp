// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyRarity.h"
#include "Data/CataclysmDataRows.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmEnemyScore.h"
#include "Dungeon/CataclysmEnemyStrength.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

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

	FCataclysmScoredFloor Floor(int32 Tier, ECataclysmDungeonType Type, int32 Total, int32 Which,
								float ModifierScore = 0.0f)
	{
		FCataclysmScoredFloor Out;
		Out.DifficultyTier = Tier;
		Out.Type = Type;
		Out.SubType = ECataclysmDungeonSubType::None;
		Out.TotalFloors = Total;
		Out.FloorNumber = Which;
		Out.ModifierScore = ModifierScore;
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
	for (int32 Index = 1; Index < Lines.Num(); ++Index)
	{
		TArray<FString> Cells;
		Lines[Index].ParseIntoArray(Cells, TEXT(","), false);
		ECataclysmDungeonType Type = ECataclysmDungeonType::Basic;
		if (Cells.Num() != 11 || !TypeNamed(Cells[3], Type))
		{
			AddError(FString::Printf(TEXT("fixture line %d is not a row this test reads: %s"), Index + 1, *Lines[Index]));
			++Wrong;
			continue;
		}

		const FName Row(*Cells[0]);
		const int32 Step = FCString::Atoi(*Cells[1]);
		const FCataclysmScoredFloor Where = Floor(FCString::Atoi(*Cells[2]), Type, FCString::Atoi(*Cells[4]),
												  FCString::Atoi(*Cells[5]), FCString::Atof(*Cells[6]));

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
			&& Near(Strength, FCString::Atod(*Cells[7])) && Near(Health, FCString::Atod(*Cells[8]))
			&& Near(Damage, FCString::Atod(*Cells[9])) && Near(Armour, FCString::Atod(*Cells[10]));
		if (!bRight)
		{
			// THE FIRST FEW ARE NAMED, so a wholesale break says what broke without printing 840 lines.
			if (Wrong < 5)
			{
				AddError(FString::Printf(
					TEXT("%s rung %d, tier %s, %s floor %s of %s, modifiers %s: built strength %.4f health %.4f damage "
						 "%.4f armour %.4f; the model says %s, %s, %s, %s"),
					*Cells[0], Step, *Cells[2], *Cells[3], *Cells[5], *Cells[4], *Cells[6], Strength, Health, Damage,
					Armour, *Cells[7], *Cells[8], *Cells[9], *Cells[10]));
			}
			++Wrong;
		}
		++Checked;
	}

	TestTrue(TEXT("the fixture holds a whole grid of rows"), Checked >= 800);
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

	TestEqual(TEXT("a fifth at tier 1"), UCataclysmEnemyScore::DamageScale(1), 0.20, 1e-9);
	TestEqual(TEXT("one at tier 8"), UCataclysmEnemyScore::DamageScale(8), 1.0, 1e-9);
	TestEqual(TEXT("evenly between"), UCataclysmEnemyScore::DamageScale(4), 0.20 + 0.80 * 3.0 / 7.0, 1e-9);

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
	TestEqual(TEXT("its damage is the score's times a fifth"), static_cast<double>(Built.AttackDamage),
			  Strength * Common->DamagePerScore * Brute->DamageShare * 0.20, 0.01);
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
	// THE CONTROL: not the sandbox's fixed figure, so the floor really built it.
	TestNotEqual(TEXT("and it is not the fixed sandbox Imp"), Imp->GetStartingMaxHealth(), GameMode->ImpHealth);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
