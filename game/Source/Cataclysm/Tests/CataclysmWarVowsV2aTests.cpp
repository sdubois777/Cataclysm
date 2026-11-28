// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmBerserkerStates.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Interface/CataclysmPassiveTreeWidget.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * THE WAR VOW OPTIONS' SECOND SLICE, V2a, 2026-11-25: Titan, Unbridled, Bastion, Ragnarok's doubling, Opportunist and
 * Unstoppable's pin, on the coordinating session's rulings that day, and a passive node or capstone option with no row
 * marked "(not built yet: it does nothing)" on the tree screen.
 *
 * WHAT IS EXERCISED: the generated rows through the pipeline, each against a control; the class resource's maximum
 * read with its conditioned rows by the new scale on a real ability system; `frenzy_entered` raised once per entry
 * into Frenzy; Opportunist's reset handed to a real War player; a conditioned `pin_suppressed` refusing a pin; and the
 * tree screen's words for a node and an option with no row, against ones with rows. What a movement-skill reset then
 * does is the cooldown reset action's own tests'.
 *
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmWarVowsV2aTest
{
	/** `Points` in `Node`, with `Option` taken when it is above nought. */
	FCataclysmPassiveAllocation WarVowsV2aTaking(const TCHAR* Node, int32 Option, int32 Points = 1)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(Node), Points);
		if (Option > 0)
		{
			Allocation.SetChosenOption(FName(Node), Option);
		}
		return Allocation;
	}

	/** What the generated stat rows of `Node` give `Stat`, judged by the pipeline from `Base` in `State`. */
	FCataclysmStatBreakdown WarVowsV2aStat(const UDataTable* Nodes, const UDataTable* Effects, const TCHAR* Node,
										   int32 Option, int32 Points, const TCHAR* Stat, float Base,
										   const FCataclysmStatConditions& State)
	{
		const TMap<FName, TArray<FCataclysmStatModifier>> Granted = UCataclysmPassiveTree::ModifiersFor(
			WarVowsV2aTaking(Node, Option, Points), Nodes, Effects, {FName(TEXT("War"))});
		const TArray<FCataclysmStatModifier>* Rows = Granted.Find(FName(Stat));
		if (!Rows)
		{
			FCataclysmStatBreakdown None;
			None.Final = Base;
			return None;
		}
		return UCataclysmStatPipeline::Evaluate(Base, *Rows, FGameplayTagContainer(), State);
	}

	/** One recorded stat row for `WarVowsV2aHold`. */
	struct FWarVowsV2aRow
	{
		const TCHAR* Stat = nullptr;
		float Base = 0.0f;
		ECataclysmStatBucket Bucket = ECataclysmStatBucket::Flat;
		float Value = 0.0f;
		ECataclysmStatCondition Condition = ECataclysmStatCondition::Always;
		float ConditionValue = 0.0f;
		ECataclysmStatScale Scale = ECataclysmStatScale::Fixed;
		float ScaleStep = 0.0f;
	};

	/** REPLACES THE WHOLE STAT LINE of `System` with these rows, each line on the base its first row names. */
	void WarVowsV2aHold(UCataclysmAbilitySystemComponent* System, const TArray<FWarVowsV2aRow>& Rows)
	{
		TMap<FName, FCataclysmStatInputs> Stats;
		for (const FWarVowsV2aRow& Each : Rows)
		{
			FCataclysmStatModifier Row;
			Row.Bucket = Each.Bucket;
			Row.Source = ECataclysmModifierSource::PassiveKeystone;
			Row.Value = Each.Value;
			Row.Condition = Each.Condition;
			Row.ConditionValue = Each.ConditionValue;
			Row.Scale = Each.Scale;
			Row.ScaleStep = Each.ScaleStep;
			const bool bNew = !Stats.Contains(FName(Each.Stat));
			FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(Each.Stat));
			if (bNew)
			{
				Line.Base = Each.Base;
			}
			Line.Modifiers.Add(Row);
		}
		System->SetStatInputs(MoveTemp(Stats));
	}

	/** A creature with a class resource pool of 100 and 1,000 health, holding no rows. Nothing judges its state but
	 *  the test. */
	struct FWarVowsV2aHolder
	{
		explicit FWarVowsV2aHolder(UWorld* World, const FVector& Where = FVector::ZeroVector)
		{
			Holder = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
			System = Holder ? Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent()) : nullptr;
			if (System)
			{
				System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Holder));
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												100.0f);
				SetFervour(0.0f);
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
				SetHealth(1000.0f);
				WarVowsV2aHold(System, {});
			}
		}
		bool IsUsable() const
		{
			return Holder && System && System->HasAttributeSetForAttribute(
				UCataclysmVitalAttributeSet::GetHealthAttribute());
		}
		void SetFervour(float Value) const
		{
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Value);
		}
		void SetHealth(float Value) const
		{
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Value);
		}
		ACataclysmEnemyCharacter* Holder = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** How many rows the generated table holds for `Node`, against what this slice wrote. False when it differs. */
	bool WarVowsV2aTableHolds(FAutomationTestBase& Test, const UDataTable* Effects, const TCHAR* Node, int32 Rows)
	{
		const int32 Held = UCataclysmPassiveTree::EffectsFor(Effects, FName(Node)).Num();
		if (Held == Rows)
		{
			return true;
		}
		Test.AddError(FString::Printf(
			TEXT("DT_PassiveEffects holds %d rows for %s, not %d. If game/Data/PassiveEffects.csv has them, the asset is "
				 "older than the CSV: run  python tools/run_editor_python.py tools/generate_datatable_assets.py"),
			Held, Node, Rows));
		return false;
	}
}

#define CATACLYSM_WAR_VOWS_V2A_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// The generated rows
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V2A_TEST(FCataclysmWarVowsV2aStatRowsTest,
	"Cataclysm.WarVowsV2a.TitanUnbridledRagnarokAndBastionGiveTheirFiguresAndTheirControlsNone")
{
	using namespace CataclysmWarVowsV2aTest;
	// THE GENERATED STAT ROWS, judged by the pipeline, each against a control: Titan's 50% more maximum health at 100
	// maximum Fervour with two hands, and none with one hand, with the maximum unknown, or with Crimson Pact taken;
	// Ragnarok's doubled maximum Fervour with two hands only; Unbridled's 10 maximum Fervour a point and, from 5 points,
	// 7 damage reduction at 150 maximum Fervour; Bastion's 25% at 25% block chance, and none with the reading unknown
	// or with Bloodied but Unbroken taken.
	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table loads"), Nodes)
		|| !TestNotNull(TEXT("set-up: the effect table loads"), Effects))
	{
		return false;
	}
	// 3, 4 AND 3 SINCE V2b, the same day: Pure Carnage, Ragnarok's shockwaves and Spiteful Guard.
	// AND 6 SINCE V3a, the same day: Eternal Hunger's cap and decay. AND 7 SINCE V3b: Thousand Deaths' chain.
	if (!WarVowsV2aTableHolds(*this, Effects, TEXT("Berserker_trunk_004"), 2)
		|| !WarVowsV2aTableHolds(*this, Effects, TEXT("Berserker_capstone_100"), 3)
		|| !WarVowsV2aTableHolds(*this, Effects, TEXT("Berserker_capstone_200"), 7)
		|| !WarVowsV2aTableHolds(*this, Effects, TEXT("Bulwark_capstone_100"), 3))
	{
		return false;
	}
	const auto Stat = [&](const TCHAR* Node, int32 Option, int32 Points, const TCHAR* Name, float Base,
						  const FCataclysmStatConditions& State)
	{
		return WarVowsV2aStat(Nodes, Effects, Node, Option, Points, Name, Base, State);
	};

	FCataclysmStatConditions TwoHanded;
	TwoHanded.WeaponHands = 2;
	TwoHanded.ClassResourceMaximumNow = 100.0f;
	FCataclysmStatConditions OneHanded = TwoHanded;
	OneHanded.WeaponHands = 1;
	FCataclysmStatConditions MaximumUnknown = TwoHanded;
	MaximumUnknown.ClassResourceMaximumNow = -1.0f;

	const TCHAR* ThirdOath = TEXT("Berserker_capstone_100");
	TestEqual(TEXT("Titan: 50% increased maximum health at 100 maximum Fervour with two hands"),
			  Stat(ThirdOath, 1, 1, TEXT("max_health"), 1000.0f, TwoHanded).SumOfIncreases, 50.0f, 0.001f);
	TestEqual(TEXT("control: none with one hand"),
			  Stat(ThirdOath, 1, 1, TEXT("max_health"), 1000.0f, OneHanded).SumOfIncreases, 0.0f, 0.001f);
	TestEqual(TEXT("control: none with the maximum unknown"),
			  Stat(ThirdOath, 1, 1, TEXT("max_health"), 1000.0f, MaximumUnknown).SumOfIncreases, 0.0f, 0.001f);
	TestEqual(TEXT("control: none with Crimson Pact taken"),
			  Stat(ThirdOath, 3, 1, TEXT("max_health"), 1000.0f, TwoHanded).SumOfIncreases, 0.0f, 0.001f);

	const TCHAR* FinalOath = TEXT("Berserker_capstone_200");
	TestEqual(TEXT("Ragnarok: a maximum Fervour of 100 is 200 with two hands"),
			  Stat(FinalOath, 1, 1, TEXT("class_resource"), 100.0f, TwoHanded).Final, 200.0f, 0.001f);
	TestEqual(TEXT("control: and 100 with one hand"),
			  Stat(FinalOath, 1, 1, TEXT("class_resource"), 100.0f, OneHanded).Final, 100.0f, 0.001f);
	TestEqual(TEXT("control: and 100 with Eternal Hunger taken"),
			  Stat(FinalOath, 3, 1, TEXT("class_resource"), 100.0f, TwoHanded).Final, 100.0f, 0.001f);

	FCataclysmStatConditions At150 = TwoHanded;
	At150.ClassResourceMaximumNow = 150.0f;
	const TCHAR* Unbridled = TEXT("Berserker_trunk_004");
	TestEqual(TEXT("Unbridled at 5 points: 50 maximum Fervour"),
			  Stat(Unbridled, 0, 5, TEXT("class_resource"), 0.0f, At150).Final, 50.0f, 0.001f);
	TestEqual(TEXT("and 7 damage reduction at 150 maximum Fervour, seven whole steps of 20"),
			  Stat(Unbridled, 0, 5, TEXT("damage_reduction"), 0.0f, At150).Final, 7.0f, 0.001f);
	TestEqual(TEXT("control: at 4 points, 40 maximum Fervour"),
			  Stat(Unbridled, 0, 4, TEXT("class_resource"), 0.0f, At150).Final, 40.0f, 0.001f);
	TestEqual(TEXT("control: and no damage reduction below 5 points"),
			  Stat(Unbridled, 0, 4, TEXT("damage_reduction"), 0.0f, At150).Final, 0.0f, 0.001f);

	FCataclysmStatConditions Blocking;
	Blocking.BlockChance = 25.0f;
	FCataclysmStatConditions BlockUnknown;
	const TCHAR* BulwarkThirdOath = TEXT("Bulwark_capstone_100");
	TestEqual(TEXT("Bastion: 25% increased maximum health at 25% block chance"),
			  Stat(BulwarkThirdOath, 1, 1, TEXT("max_health"), 1000.0f, Blocking).SumOfIncreases, 25.0f, 0.001f);
	TestEqual(TEXT("control: none with the block chance unknown"),
			  Stat(BulwarkThirdOath, 1, 1, TEXT("max_health"), 1000.0f, BlockUnknown).SumOfIncreases, 0.0f, 0.001f);
	TestEqual(TEXT("control: none with Bloodied but Unbroken taken"),
			  Stat(BulwarkThirdOath, 2, 1, TEXT("max_health"), 1000.0f, Blocking).SumOfIncreases, 0.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// The maximum the scale reads
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V2A_TEST(FCataclysmWarVowsV2aMaximumNowTest,
	"Cataclysm.WarVowsV2a.TitansScaleReadsTheMaximumFervourItsConditionedRowsMake")
{
	using namespace CataclysmWarVowsV2aTest;
	// A REAL ABILITY SYSTEM: a maximum Fervour attribute of 100 and a `class_resource` line of 100 that doubles at or
	// below 50% health, Ragnarok's shape under a condition a creature can meet; and a `max_health` line of 1,000 with
	// Titan's row, 1% increased for every 2 points of the maximum. At full health the maximum is 100 and the health
	// 1,500; at 40% health it is 200 and the health 2,000. THE CONTROLS: the attribute the scale does not read stays
	// 100, and a lookup whose rows do not ask for the scale leaves the reading unknown.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarVowsV2aHolder Titan(World);
	if (!TestTrue(TEXT("set-up: a creature with a pool and health"), Titan.IsUsable()))
	{
		return false;
	}
	WarVowsV2aHold(Titan.System, {
		{TEXT("class_resource"), 100.0f, ECataclysmStatBucket::More, 100.0f,
		 ECataclysmStatCondition::HealthAtOrBelowPercent, 50.0f},
		{TEXT("max_health"), 1000.0f, ECataclysmStatBucket::Increased, 1.0f, ECataclysmStatCondition::Always, 0.0f,
		 ECataclysmStatScale::PerPointOfMaximumClassResource, 2.0f}});

	TestEqual(TEXT("at full health the maximum Fervour is 100"), Titan.System->MaximumClassResource(), 100.0f, 0.001f);
	TestEqual(TEXT("and the maximum health 1,500"),
			  Titan.System->StatForSkill(FName(TEXT("max_health")), FGameplayTagContainer(), 0.0f), 1500.0f, 0.01f);

	Titan.SetHealth(400.0f);
	TestEqual(TEXT("at 40% health the maximum Fervour is 200"), Titan.System->MaximumClassResource(), 200.0f, 0.001f);
	TestEqual(TEXT("and the maximum health 2,000: the scale reads the doubled maximum"),
			  Titan.System->StatForSkill(FName(TEXT("max_health")), FGameplayTagContainer(), 0.0f), 2000.0f, 0.01f);

	// THE ATTRIBUTE ITSELF (window war2-w1): `CurrentConditions().ClassResourceMaximum` folds the conditioned rows in since
	// V2b, so it reads 200 here by design.
	TestEqual(TEXT("control: the attribute stays 100"),
			  Titan.System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute()),
			  100.0f, 0.001f);
	TestTrue(TEXT("control: a lookup asking no row for the scale leaves the reading unknown"),
			 Titan.System->WithEnemiesInReach(TArray<FCataclysmStatModifier>(), Titan.System->CurrentConditions())
				 .ClassResourceMaximumNow < 0.0f);
	return true;
}

// ---------------------------------------------------------------------------
// Entering Frenzy
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V2A_TEST(FCataclysmWarVowsV2aFrenzyEnteredTest,
	"Cataclysm.WarVowsV2a.EnteringFrenzyRaisesFrenzyEnteredOncePerEntry")
{
	using namespace CataclysmWarVowsV2aTest;
	// A CREATURE ALLOWED FRENZY, judged as the regeneration step judges it. A full pool begins Frenzy and raises
	// `frenzy_entered` once; judging again in Frenzy raises nothing more; an empty pool ends it and a full one begins it
	// again, which raises it again. THE CONTROL: a creature allowed Wrath begins Wrath at a full pool and raises none.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarVowsV2aHolder Frenzied(World);
	FWarVowsV2aHolder Wrathful(World, FVector(500.0f, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: two creatures with pools"), Frenzied.IsUsable() && Wrathful.IsUsable()))
	{
		return false;
	}
	WarVowsV2aHold(Frenzied.System, {{FCataclysmBerserkerStates::FrenzyOnFullFervourStat, 0.0f,
									  ECataclysmStatBucket::Flat, 1.0f}});
	WarVowsV2aHold(Wrathful.System, {{FCataclysmBerserkerStates::WrathOnFullFervourStat, 0.0f,
									  ECataclysmStatBucket::Flat, 1.0f}});

	int32 Raised = 0;
	const auto Count = [&Raised](FName Event)
	{
		if (Event == FName(TEXT("frenzy_entered")))
		{
			++Raised;
		}
	};
	const FDelegateHandle OnFrenzied = Frenzied.System->OnActionEvent.AddLambda(Count);
	const FDelegateHandle OnWrathful = Wrathful.System->OnActionEvent.AddLambda(Count);
	ON_SCOPE_EXIT
	{
		Frenzied.System->OnActionEvent.Remove(OnFrenzied);
		Wrathful.System->OnActionEvent.Remove(OnWrathful);
	};

	Frenzied.SetFervour(100.0f);
	FCataclysmBerserkerStates::Judge(Frenzied.System);
	TestTrue(TEXT("a full pool begins Frenzy"), Frenzied.System->BerserkerState() == ECataclysmBerserkerState::Frenzy);
	TestEqual(TEXT("and raises frenzy_entered once"), Raised, 1);
	FCataclysmBerserkerStates::Judge(Frenzied.System);
	TestEqual(TEXT("judging again in Frenzy raises nothing more"), Raised, 1);

	Frenzied.SetFervour(0.0f);
	FCataclysmBerserkerStates::Judge(Frenzied.System);
	TestTrue(TEXT("an empty pool ends Frenzy"), Frenzied.System->BerserkerState() == ECataclysmBerserkerState::None);
	Frenzied.SetFervour(100.0f);
	FCataclysmBerserkerStates::Judge(Frenzied.System);
	TestEqual(TEXT("and entering it again raises it again"), Raised, 2);

	Wrathful.SetFervour(100.0f);
	FCataclysmBerserkerStates::Judge(Wrathful.System);
	TestTrue(TEXT("control: a full pool allowed Wrath begins Wrath"),
			 Wrathful.System->BerserkerState() == ECataclysmBerserkerState::Wrath);
	TestEqual(TEXT("control: and raises no frenzy_entered"), Raised, 2);
	return true;
}

CATACLYSM_WAR_VOWS_V2A_TEST(FCataclysmWarVowsV2aOpportunistTest,
	"Cataclysm.WarVowsV2a.AWarPlayerTakingOpportunistIsHandedAMovementResetOnEnteringFrenzy")
{
	using namespace CataclysmWarVowsV2aTest;
	// A LEVEL 10 WAR PLAYER, through `SetPassiveAllocation`, the way the game hands a build over: Opportunist hands
	// over one action on `frenzy_entered` resetting the movement skill always. THE CONTROL: Vital Strikes, the same
	// capstone's third option, hands over none.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!State || !Controller || !Character)
	{
		AddError(TEXT("set-up: a player state, a controller and a character"));
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Character);
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent();
	if (!TestNotNull(TEXT("set-up: equipment"), Equipment) || !TestNotNull(TEXT("set-up: an ability system"), System))
	{
		return false;
	}
	State->SetCreationChoice(FName(TEXT("Greataxe")), FName(TEXT("War")));
	State->SetLevelAndExperience(10, 0);
	Equipment->RefreshAttributes(System);
	if (!TestTrue(TEXT("set-up: it reaches the Berserker tree"), State->ReachableTrees().Contains(TEXT("Berserker"))))
	{
		return false;
	}
	const auto Resets = [&]()
	{
		TArray<FCataclysmPoolAction> Found;
		for (const FCataclysmPoolAction& Action : System->GetPoolActions())
		{
			if (Action.Event == FName(TEXT("frenzy_entered")))
			{
				Found.Add(Action);
			}
		}
		return Found;
	};
	const auto Take = [&](int32 Option)
	{
		State->SetPassiveAllocation(WarVowsV2aTaking(TEXT("Berserker_capstone_25"), Option), TArray<FName>());
		Equipment->RefreshAttributes(System);
	};

	Take(3);
	TestEqual(TEXT("control: Vital Strikes hands over no action on frenzy_entered"), Resets().Num(), 0);

	Take(2);
	const TArray<FCataclysmPoolAction> Opportunist = Resets();
	if (TestEqual(TEXT("Opportunist hands over one action on frenzy_entered"), Opportunist.Num(), 1))
	{
		TestTrue(TEXT("which resets the movement skill"),
				 Opportunist[0].CooldownReset == ECataclysmCooldownReset::Movement);
		TestEqual(TEXT("always"), Opportunist[0].Percent, 100.0f, 0.001f);
	}
	return true;
}

// ---------------------------------------------------------------------------
// A pin, counted as the root
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V2A_TEST(FCataclysmWarVowsV2aPinTest,
	"Cataclysm.WarVowsV2a.UnstoppableRefusesAPinOnlyWhileItsConditionHolds")
{
	using namespace CataclysmWarVowsV2aTest;
	// THE GENERATED ROW: Unstoppable grants `pin_suppressed` through `ModifiersFor`, a flag of 1 above 75% Fervour with
	// two hands; Thrill of the Hunt grants none. A stat row with a second condition is a modifier, not an enchantment
	// copy (`IsBuiltAsAnEnchantmentRow`): CORRECTED ON 2026-11-25 BEFORE ANY WINDOW RAN IT, as the first slice's test
	// was. AND THE ENGINE: a creature holding the flag above 75% Fervour is not pinned at 100 Fervour, and is pinned once
	// it drops to 50 (THE CONTROL, the same creature with the condition no longer holding).
	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table loads"), Nodes)
		|| !TestNotNull(TEXT("set-up: the effect table loads"), Effects)
		|| !WarVowsV2aTableHolds(*this, Effects, TEXT("Berserker_capstone_50"), 5))
	{
		return false;
	}
	const auto Granted = [&](int32 Option)
	{
		const TMap<FName, TArray<FCataclysmStatModifier>> All = UCataclysmPassiveTree::ModifiersFor(
			WarVowsV2aTaking(TEXT("Berserker_capstone_50"), Option), Nodes, Effects, {FName(TEXT("War"))});
		const TArray<FCataclysmStatModifier>* Rows = All.Find(FName(UCataclysmSkillEffects::PinSuppressedStat));
		return Rows ? *Rows : TArray<FCataclysmStatModifier>();
	};
	const TArray<FCataclysmStatModifier> Taken = Granted(1);
	if (TestEqual(TEXT("Unstoppable grants pin_suppressed once"), Taken.Num(), 1))
	{
		TestEqual(TEXT("as a flag of 1"), Taken[0].Value, 1.0f, 0.001f);
		TestTrue(TEXT("above"), Taken[0].Condition == ECataclysmStatCondition::ClassResourceAbovePercent);
		TestEqual(TEXT("75% Fervour"), Taken[0].ConditionValue, 75.0f, 0.001f);
		TestTrue(TEXT("with two hands"), Taken[0].Condition2 == ECataclysmStatCondition::WieldingTwoHandedWeapon);
	}
	TestEqual(TEXT("control: Thrill of the Hunt grants none"), Granted(3).Num(), 0);

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarVowsV2aHolder Pinner(World);
	FWarVowsV2aHolder Unstoppable(World, FVector(200.0f, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: two creatures"), Pinner.IsUsable() && Unstoppable.IsUsable()))
	{
		return false;
	}
	WarVowsV2aHold(Unstoppable.System, {{UCataclysmSkillEffects::PinSuppressedStat, 0.0f, ECataclysmStatBucket::Flat,
										 1.0f, ECataclysmStatCondition::ClassResourceAbovePercent, 75.0f}});

	Unstoppable.SetFervour(100.0f);
	TestFalse(TEXT("above 75% Fervour the pin is refused"),
			  UCataclysmSkillEffects::ApplyPin(Pinner.Holder, Unstoppable.Holder, 2.0f));
	TestFalse(TEXT("and the creature is not pinned"), UCataclysmSkillEffects::IsPinned(Unstoppable.Holder));

	Unstoppable.SetFervour(50.0f);
	TestTrue(TEXT("control: at 50% Fervour the pin takes hold"),
			 UCataclysmSkillEffects::ApplyPin(Pinner.Holder, Unstoppable.Holder, 2.0f));
	TestTrue(TEXT("control: and the creature is pinned"), UCataclysmSkillEffects::IsPinned(Unstoppable.Holder));
	return true;
}

// ---------------------------------------------------------------------------
// The tree screen
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V2A_TEST(FCataclysmWarVowsV2aNotBuiltYetTest,
	"Cataclysm.WarVowsV2a.APassiveNodeOrOptionWithNoRowSaysItIsNotBuiltYet")
{
	using namespace CataclysmWarVowsV2aTest;
	// THE WORDS THE FLOOR MODIFIER PANEL USES, "(not built yet: it does nothing)", on a node and an option with no row:
	// Colossus (`Berserker_twoh_ks_001`, see below) and Bolted Together (the Saboteur's Third Oath, option 2). The First Oath's
	// Warlord was the option until V2b built it, Thousand Deaths until V3b, Magnum Opus until V4b and Crossfire until
	// V5a, the same day. BOLTED TOGETHER WAS BUILT LAST, BY V5c, AND NO OPTION IN ANY TREE IS LEFT WITHOUT A ROW: so its
	// rows are taken out of a copy of the effect table here, and the copy is what is asked. THE CONTROL for that: the
	// table as loaded holds them.
	// THE CONTROLS: Wrath (`Berserker_twoh_001`) and Thousand Deaths have rows and are not marked, and with no effect
	// table nothing is.
	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* Loaded = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table loads"), Nodes)
		|| !TestNotNull(TEXT("set-up: the effect table loads"), Loaded))
	{
		return false;
	}
	using FTree = UCataclysmPassiveTree;
	// COLOSSUS (`Berserker_twoh_ks_001`) SINCE THE WAR TREES' ENGINE SLICE E3 (2026-11-27), which gave Open Wounds
	// (`Berserker_trunk_006`) rows: Colossus waits on the Earthquake skill (#2972) and has none. The screen's tool tip
	// below reads the loaded table, so the node has to be one with no row there.
	const FName Unbuilt(TEXT("Berserker_twoh_ks_001"));
	const FName Built(TEXT("Berserker_twoh_001"));
	const FName FinalOath(TEXT("Saboteur_capstone_100"));
	if (!TestTrue(TEXT("control: the table as loaded holds Bolted Together's rows"),
				  FTree::HasEffectRows(Loaded, FinalOath, 2)))
	{
		return false;
	}
	UDataTable* Effects = DuplicateObject<UDataTable>(Loaded, GetTransientPackage());
	if (!TestNotNull(TEXT("set-up: a copy of the effect table"), Effects))
	{
		return false;
	}
	TArray<FName> Removing;
	for (const TPair<FName, uint8*>& Each : Effects->GetRowMap())
	{
		const FCataclysmPassiveEffectRow* Row = reinterpret_cast<const FCataclysmPassiveEffectRow*>(Each.Value);
		if (Row && Row->Node == FinalOath.ToString() && Row->Option == 2)
		{
			Removing.Add(Each.Key);
		}
	}
	for (const FName& Name : Removing)
	{
		Effects->RemoveRow(Name);
	}
	const FName BuiltOath(TEXT("Berserker_capstone_200"));
	const FCataclysmPassiveNodeRow* UnbuiltRow = FTree::FindNode(Nodes, Unbuilt);
	const FCataclysmPassiveNodeRow* BuiltRow = FTree::FindNode(Nodes, Built);
	if (!TestNotNull(TEXT("set-up: Colossus is a node"), UnbuiltRow)
		|| !TestNotNull(TEXT("set-up: Wrath is a node"), BuiltRow)
		|| !TestFalse(TEXT("set-up: Colossus has no row"), FTree::HasEffectRows(Effects, Unbuilt))
		|| !TestTrue(TEXT("set-up: Wrath has a row"), FTree::HasEffectRows(Effects, Built))
		|| !TestFalse(TEXT("set-up: Bolted Together has no row"), FTree::HasEffectRows(Effects, FinalOath, 2))
		|| !TestTrue(TEXT("set-up: Thousand Deaths has a row"), FTree::HasEffectRows(Effects, BuiltOath, 2)))
	{
		return false;
	}
	const FString Mark(TEXT(" (not built yet: it does nothing)"));

	// THE DESCRIPTION UNDER THE TREE.
	TestEqual(TEXT("Colossus's words are marked"), FTree::ShortDescriptionOf(Nodes, Unbuilt, Effects),
			  UnbuiltRow->Description + Mark);
	TestEqual(TEXT("control: Wrath's are not"), FTree::ShortDescriptionOf(Nodes, Built, Effects), BuiltRow->Description);
	TestEqual(TEXT("control: with no table, Colossus's are not"), FTree::ShortDescriptionOf(Nodes, Unbuilt),
			  UnbuiltRow->Description);
	const FString Full = FTree::FullDescriptionOf(Nodes, FinalOath, Effects);
	TestTrue(TEXT("the Saboteur's Third Oath's full text marks Bolted Together's heading"),
			 Full.Contains(TEXT("2  Bolted Together") + Mark));
	const FString BuiltFull = FTree::FullDescriptionOf(Nodes, BuiltOath, Effects);
	TestTrue(TEXT("control: and the Berserker's does not mark Thousand Deaths'"),
			 BuiltFull.Contains(TEXT("2  Thousand Deaths\n")) && !BuiltFull.Contains(TEXT("2  Thousand Deaths") + Mark));
	TestTrue(TEXT("the Saboteur's Third Oath's short text marks Bolted Together"),
			 FTree::ShortDescriptionOf(Nodes, FinalOath, Effects).Contains(TEXT("2 Bolted Together") + Mark));

	// THE OPTION BUTTONS WHILE THE CHOICE IS ON OFFER.
	TestEqual(TEXT("Bolted Together's button is marked"),
			  UCataclysmPassiveTreeWidget::OptionLabelFor(Nodes, Effects, FinalOath, 2),
			  FString(TEXT("2  Bolted Together")) + Mark);
	TestEqual(TEXT("control: Thousand Deaths' is not"),
			  UCataclysmPassiveTreeWidget::OptionLabelFor(Nodes, Effects, BuiltOath, 2),
			  FString(TEXT("2  Thousand Deaths")));
	TestEqual(TEXT("control: with no table, Bolted Together's is not"),
			  UCataclysmPassiveTreeWidget::OptionLabelFor(Nodes, nullptr, FinalOath, 2),
			  FString(TEXT("2  Bolted Together")));

	// AND A NODE'S TOOL TIP ON THE GRAPH, from the function the screen's panel runs, with no character shown.
	UCataclysmPassiveTreeWidget* Screen = NewObject<UCataclysmPassiveTreeWidget>();
	if (!TestNotNull(TEXT("set-up: the screen was created"), Screen))
	{
		return false;
	}
	UCataclysmChoiceButton* Marked = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeButtonForTests(*Marked, Unbuilt, false);
	TestEqual(TEXT("Colossus's tool tip says it is not built yet"), Marked->GetToolTipText().ToString(),
			  UnbuiltRow->NodeName + Mark);
	UCataclysmChoiceButton* Unmarked = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeButtonForTests(*Unmarked, Built, false);
	TestTrue(TEXT("control: Wrath's has no tool tip"), Unmarked->GetToolTipText().IsEmpty());
	return true;
}

#undef CATACLYSM_WAR_VOWS_V2A_TEST

#endif // WITH_AUTOMATION_TESTS
