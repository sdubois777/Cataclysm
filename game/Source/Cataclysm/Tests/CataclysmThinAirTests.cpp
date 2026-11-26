// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmTargetCandidates.h"
#include "Data/CataclysmDataRows.h"
#include "Interface/CataclysmPlayerEffects.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * THIN AIR AND CALCULATED, the C5 plan, accepted as written by the coordinating session on 2026-11-25.
 *
 * WHAT IS EXERCISED THROUGH ITS REAL SITE: a creature's search for the nearest hostile
 * (`UCataclysmTargetCandidates::NearestHostile`, which `ACataclysmEnemyController::ChooseTarget` asks on every think),
 * and the player's effects line (`UCataclysmPlayerEffects::EntriesFor`). Moving and the landed blow are told to the
 * player's ability system as the movement sampler and the damage step tell it (`NoteMovedMetres`, `NoteDidNotMove`,
 * `NoteHitTaken`). THE ROWS are read through THE GENERATED TABLE and judged by the pipeline.
 *
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmThinAirTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	constexpr float ThinAirMetre = 100.0f;

	/** A player on the players' side at the origin, on its player state. */
	struct FThinAirPlayer
	{
		explicit FThinAirPlayer(UWorld* World)
		{
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!State || !Character)
			{
				return;
			}
			Character->SetPlayerState(State);
			Character->OnRep_PlayerState();
			Character->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
			System = State->GetCataclysmAbilitySystemComponent();
		}

		bool IsUsable() const { return Character && System; }

		/** REPLACES THE WHOLE STAT LINE with Thin Air's flag, when `bFlag`, and its seconds. */
		void Holds(bool bFlag, float BrokenSeconds) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			const auto Add = [&Stats](const TCHAR* Stat, float Value)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::PassiveKeystone;
				Row.Value = Value;
				FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(Stat));
				Line.Base = 0.0f;
				Line.Modifiers.Add(Row);
			};
			if (bFlag)
			{
				Add(UCataclysmAbilitySystemComponent::CannotBeTargetedWhileMovingStat, 1.0f);
			}
			Add(UCataclysmAbilitySystemComponent::ThinAirBrokenSecondsStat, BrokenSeconds);
			System->SetStatInputs(MoveTemp(Stats));
		}

		/** Whether the effects line shows "Thin Air". */
		bool ShowsThinAir() const
		{
			return UCataclysmPlayerEffects::EntriesFor(Character).ContainsByPredicate(
				[](const FCataclysmPlayerEffect& Entry) { return Entry.Name == TEXT("Thin Air"); });
		}

		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** What `Creature`'s search for the nearest hostile within 10 m finds. */
	AActor* ThinAirFoundBy(UWorld* World, const ACataclysmEnemyCharacter* Creature)
	{
		UCataclysmTargetCandidates* Candidates = UCataclysmTargetCandidates::In(World);
		return Candidates ? Candidates->NearestHostile(Creature, Creature->GetActorLocation(), 10.0f * ThinAirMetre)
						  : nullptr;
	}

	/** What the generated rows of `Node` at `Points` give `Stat`, judged by the pipeline. */
	FCataclysmStatBreakdown ThinAirGenerated(const UDataTable* Nodes, const UDataTable* Effects, const TCHAR* Node,
											 int32 Points, const TCHAR* Stat)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(Node), Points);
		const TMap<FName, TArray<FCataclysmStatModifier>> Granted =
			UCataclysmPassiveTree::ModifiersFor(Allocation, Nodes, Effects, {FName(TEXT("War"))});
		const TArray<FCataclysmStatModifier>* Rows = Granted.Find(FName(Stat));
		return Rows ? UCataclysmStatPipeline::Evaluate(0.0f, *Rows, FGameplayTagContainer(), FCataclysmStatConditions())
					: FCataclysmStatBreakdown();
	}
}

#define CATACLYSM_THIN_AIR_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// Thin Air
// ---------------------------------------------------------------------------

CATACLYSM_THIN_AIR_TEST(FCataclysmThinAirSearchTest,
	"Cataclysm.ThinAir.AMovingPlayerIsPassedOverByACreaturesSearchUntilABlowLands")
{
	using namespace CataclysmThinAirTest;
	// A CREATURE 3 M FROM A PLAYER, and nothing else hostile to it. THE CONTROLS: without Thin Air's flag a moving player
	// is found; with it, a player standing still is found. With it and moving, the search finds nothing, and the effects
	// line shows "Thin Air". An evaded blow leaves it whole; a landed blow breaks it, so the player is found 1.5 s later
	// and is hidden again 2.5 s after the blow.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FThinAirPlayer Player(World);
	ACataclysmEnemyCharacter* Creature =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(3.0f * ThinAirMetre, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a creature"), Creature)
		|| !TestNotNull(TEXT("set-up: the world has the target lists"), UCataclysmTargetCandidates::In(World)))
	{
		return false;
	}
	Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	AActor* const Them = Player.Character;

	Player.Holds(/*bFlag=*/false, 2.0f);
	Player.System->NoteMovedMetres(1.0f);
	TestEqual(TEXT("control: without the flag a moving player is found"), ThinAirFoundBy(World, Creature), Them);
	TestFalse(TEXT("control: and nothing is shown"), Player.ShowsThinAir());

	Player.Holds(/*bFlag=*/true, 2.0f);
	Player.System->NoteDidNotMove();
	TestEqual(TEXT("control: with the flag a player standing still is found"), ThinAirFoundBy(World, Creature), Them);

	Player.System->NoteMovedMetres(1.0f);
	TestNull(TEXT("with the flag and moving, the search finds nothing"), ThinAirFoundBy(World, Creature));
	TestTrue(TEXT("and the effects line shows Thin Air"), Player.ShowsThinAir());

	Player.System->NoteHitTaken(/*bLanded=*/false);
	TestNull(TEXT("an evaded blow does not break it"), ThinAirFoundBy(World, Creature));

	Player.System->NoteHitTaken(/*bLanded=*/true);
	World->TimeSeconds += 1.5f;
	TestEqual(TEXT("1.5 s after a landed blow the player is found"), ThinAirFoundBy(World, Creature), Them);
	TestFalse(TEXT("and Thin Air is not shown"), Player.ShowsThinAir());
	World->TimeSeconds += 1.0f;
	TestNull(TEXT("2.5 s after it, the player is hidden again"), ThinAirFoundBy(World, Creature));
	return true;
}

// ---------------------------------------------------------------------------
// The rows, as the game loads them
// ---------------------------------------------------------------------------

CATACLYSM_THIN_AIR_TEST(FCataclysmThinAirRowsTest,
	"Cataclysm.ThinAir.TheGeneratedRowsGiveThinAirAndCalculatedTheirFigures")
{
	using namespace CataclysmThinAirTest;
	// THE GENERATED ROWS, judged by the pipeline. Thin Air at its one point: the flag and 2 seconds. Calculated: 25
	// maximum Fervour and no increase at 5 points (the control), 30 and 10% increased at 6 (the ruled rewording).
	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table loads"), Nodes)
		|| !TestNotNull(TEXT("set-up: the effect table loads"), Effects))
	{
		return false;
	}
	const TCHAR* ThinAir = TEXT("Saboteur_basic_trunk_013");
	const TCHAR* Calculated = TEXT("Saboteur_basic_trunk_003");
	const int32 ThinAirRows = UCataclysmPassiveTree::EffectsFor(Effects, FName(ThinAir)).Num();
	const int32 CalculatedRows = UCataclysmPassiveTree::EffectsFor(Effects, FName(Calculated)).Num();
	if (ThinAirRows != 2 || CalculatedRows != 2)
	{
		AddError(FString::Printf(
			TEXT("DT_PassiveEffects holds %d and %d rows for Thin Air and Calculated, not 2 and 2. If "
				 "game/Data/PassiveEffects.csv has them, the asset is older than the CSV: run  python "
				 "tools/run_editor_python.py tools/generate_datatable_assets.py"),
			ThinAirRows, CalculatedRows));
		return false;
	}
	TestEqual(TEXT("Thin Air: the flag"),
			  ThinAirGenerated(Nodes, Effects, ThinAir, 1, UCataclysmAbilitySystemComponent::CannotBeTargetedWhileMovingStat)
				  .Final,
			  1.0f, 0.001f);
	TestEqual(TEXT("and 2 seconds"),
			  ThinAirGenerated(Nodes, Effects, ThinAir, 1, UCataclysmAbilitySystemComponent::ThinAirBrokenSecondsStat).Final,
			  2.0f, 0.001f);
	const FCataclysmStatBreakdown AtFive = ThinAirGenerated(Nodes, Effects, Calculated, 5, TEXT("class_resource"));
	const FCataclysmStatBreakdown AtSix = ThinAirGenerated(Nodes, Effects, Calculated, 6, TEXT("class_resource"));
	TestEqual(TEXT("control: Calculated at 5 points, 25 maximum Fervour"), AtFive.Flat, 25.0f, 0.001f);
	TestEqual(TEXT("control: and no increase"), AtFive.SumOfIncreases, 0.0f, 0.001f);
	TestEqual(TEXT("Calculated at 6 points, 30 maximum Fervour"), AtSix.Flat, 30.0f, 0.001f);
	TestEqual(TEXT("and 10% increased"), AtSix.SumOfIncreases, 10.0f, 0.001f);
	return true;
}

#undef CATACLYSM_THIN_AIR_TEST

#endif // WITH_AUTOMATION_TESTS
