// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Saboteur's rows of 2026-11-25, on the coordinating session's rulings on the survey of that day: `Type.Gadget` on
 * every machine that is not a trap (question 1), the passive rows' Max Points column and the condition
 * `class_resource_at_or_below`, which together make Durable Modifications' doubling exact (question 4).
 *
 * NOTHING HERE WAITS ON A TICK. Durable Modifications is read through THE GENERATED TABLE, as the game loads it, and
 * judged by the stat pipeline with a state written by hand; the other two are read with rows written by hand. Helper
 * types and functions are named after the subject, the Saboteur's rows.
 */
namespace CataclysmSaboteurRowsTest
{
	/** A possessed player with a pool of 100 Fervour holding 50, made as the trap rows' rig makes one. */
	struct FSaboteurRowsPlayer
	{
		explicit FSaboteurRowsPlayer(UWorld* World)
		{
			ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!PlayerState || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(PlayerState);
			Controller->Possess(Character);
			System = PlayerState->GetCataclysmAbilitySystemComponent();
			if (System)
			{
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 50.0f);
			}
		}

		bool IsUsable() const
		{
			return Character && System;
		}

		/** REPLACES THE WHOLE STAT LINE with one row of `Value` in `Bucket` on `Stat`, requiring `RequiredTag`. */
		void Holds(const TCHAR* Stat, float Value, ECataclysmStatBucket Bucket, const TCHAR* RequiredTag) const
		{
			FCataclysmStatModifier Row;
			Row.Bucket = Bucket;
			Row.Source = ECataclysmModifierSource::Enchantment;
			Row.Value = Value;
			Row.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(RequiredTag), /*ErrorIfNotFound=*/false));
			TMap<FName, FCataclysmStatInputs> Stats;
			FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(Stat));
			Line.Base = 0.0f;
			Line.Modifiers.Add(Row);
			System->SetStatInputs(MoveTemp(Stats));
		}

		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** The "more" multiplier Durable Modifications gives at `Points`, with the pool holding `Held` of 100. */
	float SaboteurRowsDurableMore(const TMap<FName, TArray<FCataclysmStatModifier>>& Granted, float Held)
	{
		const TArray<FCataclysmStatModifier>* Rows = Granted.Find(FName(TEXT("damage_reduction")));
		if (!Rows)
		{
			return 1.0f;
		}
		FCataclysmStatConditions State;
		State.ClassResourceHeld = Held;
		State.ClassResourceMaximum = 100.0f;
		return UCataclysmStatPipeline::Evaluate(0.0f, *Rows, FGameplayTagContainer(), State).MoreMultiplier;
	}
}

#define CATACLYSM_SABOTEUR_ROWS_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// Max Points: a row stops at a number of points
// ---------------------------------------------------------------------------

CATACLYSM_SABOTEUR_ROWS_TEST(FCataclysmSaboteurRowsMaxPointsTest,
	"Cataclysm.SaboteurRows.AMaxPointsRowAppliesBelowItsLimitAndNotAtIt")
{
	// A ROW PAID PER POINT WITH MAX POINTS 4 (exclusive): 3 points paid at 3, nothing at 4 or 6. THE CONTROL is the same
	// row with no limit, which is paid 4 at 4. And a threshold row with Min Points 2 and Max Points 4 applies at 2 and 3
	// and not at 1 or 4.
	FCataclysmPassiveEffectRow PerPoint;
	PerPoint.ValuePerPoint = 3.0f;
	FCataclysmPassiveEffectRow Limited = PerPoint;
	Limited.MaxPoints = 4;

	TestEqual(TEXT("control: with no limit, 4 points are paid at 4"), UCataclysmPassiveTree::PointsPaid(PerPoint, 4), 4);
	TestEqual(TEXT("with Max Points 4, 3 points are paid at 3"), UCataclysmPassiveTree::PointsPaid(Limited, 3), 3);
	TestEqual(TEXT("and nothing at 4"), UCataclysmPassiveTree::PointsPaid(Limited, 4), 0);
	TestEqual(TEXT("or at 6"), UCataclysmPassiveTree::PointsPaid(Limited, 6), 0);

	FCataclysmPassiveEffectRow Window;
	Window.ValuePerPoint = 1.0f;
	Window.MinPoints = 2;
	Window.MaxPoints = 4;
	TestEqual(TEXT("a row from 2 to 4: nothing at 1"), UCataclysmPassiveTree::PointsPaid(Window, 1), 0);
	TestEqual(TEXT("once at 2"), UCataclysmPassiveTree::PointsPaid(Window, 2), 1);
	TestEqual(TEXT("once at 3"), UCataclysmPassiveTree::PointsPaid(Window, 3), 1);
	TestEqual(TEXT("and nothing at 4"), UCataclysmPassiveTree::PointsPaid(Window, 4), 0);
	return true;
}

// ---------------------------------------------------------------------------
// Durable Modifications: exactly doubled above half the pool from 4 points
// ---------------------------------------------------------------------------

CATACLYSM_SABOTEUR_ROWS_TEST(FCataclysmSaboteurRowsDurableModificationsTest,
	"Cataclysm.SaboteurRows.DurableModificationsIsExactlyDoubledAboveHalfFromFourPoints")
{
	using namespace CataclysmSaboteurRowsTest;
	// THE GENERATED ROWS, at every point from 1 to 6, judged by the pipeline with the pool at 50 of 100 (at or below
	// half) and at 51 (above): 3% a point at or below half at every point, and above half at 1 to 3 points; 6% a point
	// above half from 4 points. EXACTLY: one row holds at a time, so the multiplier is 1 + 3p% or 1 + 6p%, never a
	// product. THE CONTROLS are the neighbours of each edge: at 4 points at or below half it is 1.12, not 1.24; at 3
	// points above half it is 1.09, not 1.18.
	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table loads"), Nodes) || !TestNotNull(TEXT("set-up: the effect table loads"),
																				  Effects))
	{
		return false;
	}
	const FName Node(TEXT("Saboteur_basic_trunk_014"));
	if (UCataclysmPassiveTree::EffectsFor(Effects, Node).Num() != 3)
	{
		AddError(TEXT("Durable Modifications does not have its three rows in DT_PassiveEffects. If it has them in "
					  "game/Data/PassiveEffects.csv, the asset is older than the CSV: run  python "
					  "tools/run_editor_python.py tools/generate_datatable_assets.py"));
		return false;
	}
	for (int32 Points = 1; Points <= 6; ++Points)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(Node, Points);
		const TMap<FName, TArray<FCataclysmStatModifier>> Granted =
			UCataclysmPassiveTree::ModifiersFor(Allocation, Nodes, Effects, {FName(TEXT("War"))});
		const float Low = SaboteurRowsDurableMore(Granted, 50.0f);
		const float High = SaboteurRowsDurableMore(Granted, 51.0f);
		TestEqual(*FString::Printf(TEXT("at %d points, at or below half: 3%% a point"), Points), Low,
				  1.0f + 0.03f * Points, 0.0001f);
		TestEqual(*FString::Printf(TEXT("at %d points, above half: %s"), Points,
								   Points >= 4 ? TEXT("6% a point") : TEXT("3% a point")),
				  High, 1.0f + (Points >= 4 ? 0.06f : 0.03f) * Points, 0.0001f);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Type.Gadget: a row meaning gadgets reaches gadgets and not traps
// ---------------------------------------------------------------------------

CATACLYSM_SABOTEUR_ROWS_TEST(FCataclysmSaboteurRowsGadgetTagTest,
	"Cataclysm.SaboteurRows.AGadgetRowReachesGadgetsAndNotTraps")
{
	using namespace CataclysmSaboteurRowsTest;
	// THE TYPES: the three machines that are not traps carry `Type.Gadget`, the two traps do not (the controls). A
	// MACHINE'S OWN BLOW: 50% increased attack damage on `Type.Gadget` raises a ballista's 100 to 150 and leaves a spike
	// trap's at 100, read as `ACataclysmMinion` reads it, over the rows naming either tag. A SKILL GADGET'S RESERVE: 50%
	// less on `Type.Gadget` halves it and leaves a skill trap's as it was.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FSaboteurRowsPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const FGameplayTag Gadget = FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Gadget")), /*ErrorIfNotFound=*/false);
	if (!TestTrue(TEXT("set-up: the tag Type.Gadget exists (game/Config/Tags/CataclysmTags.ini)"), Gadget.IsValid()))
	{
		return false;
	}
	for (const TCHAR* Type : {TEXT("BoltTurret"), TEXT("Ballista"), TEXT("SkillGadget")})
	{
		TestTrue(*FString::Printf(TEXT("%s carries Type.Gadget"), Type),
				 ACataclysmMinion::TagsOfType(Type).HasTagExact(Gadget));
	}
	for (const TCHAR* Type : {TEXT("SpikeTrap"), TEXT("SkillTrap")})
	{
		TestFalse(*FString::Printf(TEXT("control: %s does not"), Type), ACataclysmMinion::TagsOfType(Type).HasTagExact(Gadget));
	}

	FGameplayTagContainer NamingMachines;
	NamingMachines.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Deployable"))));
	NamingMachines.AddTag(Gadget);
	Player.Holds(TEXT("attack_damage"), 50.0f, ECataclysmStatBucket::Increased, TEXT("Type.Gadget"));
	TestEqual(TEXT("a ballista's blow of 100 takes the gadget row: 150"),
			  Player.System->StatNamingTagAppliedTo(FName(TEXT("attack_damage")), NamingMachines, 100.0f,
													ACataclysmMinion::TagsOfType(TEXT("Ballista")), nullptr),
			  150.0f, 0.01f);
	TestEqual(TEXT("control: a spike trap's blow of 100 does not: 100"),
			  Player.System->StatNamingTagAppliedTo(FName(TEXT("attack_damage")), NamingMachines, 100.0f,
													ACataclysmMinion::TagsOfType(TEXT("SpikeTrap")), nullptr),
			  100.0f, 0.01f);

	Player.Holds(TEXT("minion_damage"), 0.0f, ECataclysmStatBucket::Flat, TEXT("Type.Gadget"));
	const float GadgetBefore = ACataclysmMinion::SkillGadgetReserveFor(Player.Character, ECataclysmAbilitySlot::Special);
	const float TrapBefore = ACataclysmMinion::SkillTrapReserveFor(Player.Character, ECataclysmAbilitySlot::Heavy);
	Player.Holds(UCataclysmCommand::MinionReservePercentLessStat, 50.0f, ECataclysmStatBucket::Flat, TEXT("Type.Gadget"));
	TestEqual(TEXT("a skill gadget's reserve is halved by 50% less on Type.Gadget"),
			  ACataclysmMinion::SkillGadgetReserveFor(Player.Character, ECataclysmAbilitySlot::Special),
			  FMath::Max(UCataclysmCommand::SmallestMinionReserve, GadgetBefore * 0.5f), 0.01f);
	TestEqual(TEXT("control: a skill trap's reserve is as it was"),
			  ACataclysmMinion::SkillTrapReserveFor(Player.Character, ECataclysmAbilitySlot::Heavy), TrapBefore, 0.01f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
