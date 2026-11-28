// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * THE WAR TREES' ENGINE SLICE E4 (window 2, 2026-11-28): the Saboteur's conditions, scales and ability system pieces that
 * `CataclysmStatExemptionTests.cpp`'s probes do not already measure. See the decisions entry "The War passive trees,
 * window 2". A gadget's own behaviours (Overcharge, Jury Rigged, Power Surge) and the traps' are measured by the window.
 */
namespace CataclysmWarTreesE4Test
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	/** A creature with a class resource pool of 100, nothing in it, and the stat lines given. */
	struct FWarTreesE4Holder
	{
		FWarTreesE4Holder(UWorld* World, const FVector& At)
		{
			Holder = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
			System = Holder ? Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent()) : nullptr;
			if (System)
			{
				System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Holder));
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												100.0f);
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);
				System->SetStatInputs({});
			}
		}
		bool IsUsable() const { return Holder && System; }
		void Give(const TCHAR* Stat, float Value)
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::PassiveKeystone;
			Flat.Value = Value;
			Lines.FindOrAdd(FName(Stat)).Modifiers.Add(Flat);
			TMap<FName, FCataclysmStatInputs> Copy = Lines;
			System->SetStatInputs(MoveTemp(Copy));
		}
		ACataclysmEnemyCharacter* Holder = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		TMap<FName, FCataclysmStatInputs> Lines;
	};

	float E4Value(const FCataclysmStatModifier& Row, const FCataclysmStatConditions& State)
	{
		return UCataclysmStatPipeline::Evaluate(0.0f, {Row}, FGameplayTagContainer(), State).Final;
	}

	FCataclysmStatModifier E4Row(float Value)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::Flat;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		return Row;
	}

	FCataclysmStatModifier E4Scaled(ECataclysmStatScale Scale, float Value, float Step)
	{
		FCataclysmStatModifier Row = E4Row(Value);
		Row.Scale = Scale;
		Row.ScaleStep = Step;
		return Row;
	}
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE4ConditionsTest,
	"Cataclysm.WarTreesE4.TheTwoGadgetConditionsReadTheGadgetAndRefuseWithNoneNamed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `gadget_reprieved_below_full_health` (Jury Rigged) and `gadget_fell_below_quarter_within_seconds` (Last Stand
 * Protocol), each with no gadget named first, which refuses.
 */
bool FCataclysmWarTreesE4ConditionsTest::RunTest(const FString&)
{
	using namespace CataclysmWarTreesE4Test;
	FCataclysmStatModifier Reprieved = E4Row(10.0f);
	Reprieved.Condition = ECataclysmStatCondition::GadgetReprievedBelowFullHealth;
	FCataclysmStatConditions State;
	TestEqual(TEXT("no gadget named: Jury Rigged's row refuses"), E4Value(Reprieved, State), 0.0f);
	State.GadgetHealthPercent = 50.0f;
	TestEqual(TEXT("a gadget at half health that was never saved: refuses"), E4Value(Reprieved, State), 0.0f);
	State.bGadgetReprieved = true;
	TestEqual(TEXT("saved and at half health: holds"), E4Value(Reprieved, State), 10.0f);
	State.GadgetHealthPercent = 100.0f;
	TestEqual(TEXT("saved and back at full health: refuses"), E4Value(Reprieved, State), 0.0f);

	FCataclysmStatModifier LastStand = E4Row(8.0f);
	LastStand.Condition = ECataclysmStatCondition::GadgetFellBelowQuarterWithinSeconds;
	LastStand.ConditionValue = 5.0f;
	FCataclysmStatConditions Fallen;
	TestEqual(TEXT("never fallen, or no gadget named: Last Stand refuses"), E4Value(LastStand, Fallen), 0.0f);
	Fallen.SecondsSinceGadgetFellBelowQuarter = 3.0f;
	TestEqual(TEXT("fallen 3 s ago, within 5: holds"), E4Value(LastStand, Fallen), 8.0f);
	Fallen.SecondsSinceGadgetFellBelowQuarter = 6.0f;
	TestEqual(TEXT("fallen 6 s ago: refuses"), E4Value(LastStand, Fallen), 0.0f);
	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE4ScalesTest,
	"Cataclysm.WarTreesE4.TheThreeNewestScalesCountWholeStepsAndNothingUnread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `gadget_empowered_fervour` (Overcharge), `gadget_overclock_fervour` (Overclock) and `traps_maximum_fervour_could_hold`
 * (Precision Placement): whole steps of the reading, and nothing for an unread one (-1).
 */
bool FCataclysmWarTreesE4ScalesTest::RunTest(const FString&)
{
	using namespace CataclysmWarTreesE4Test;
	FCataclysmStatConditions State;
	const FCataclysmStatModifier Empowered = E4Scaled(ECataclysmStatScale::PerFervourGadgetEmpowered, 5.0f, 1.0f);
	const FCataclysmStatModifier Overclocked = E4Scaled(ECataclysmStatScale::PerFervourGadgetOverclocked, 5.0f, 1.0f);
	const FCataclysmStatModifier CouldHold = E4Scaled(ECataclysmStatScale::PerTrapMaximumFervourCouldHold, 10.0f, 4.0f);
	TestEqual(TEXT("Overcharge, unread: nothing"), E4Value(Empowered, State), 0.0f);
	TestEqual(TEXT("Overclock, unread: nothing"), E4Value(Overclocked, State), 0.0f);
	TestEqual(TEXT("Precision Placement, unread: nothing"), E4Value(CouldHold, State), 0.0f);
	State.GadgetEmpoweredFervour = 4.0f;
	State.GadgetOverclockFervour = 2.5f;
	State.TrapsMaximumFervourCouldHold = 9.0f;
	TestEqual(TEXT("Overcharge, 4 Fervour empowered: 5 for each, 20"), E4Value(Empowered, State), 20.0f);
	TestEqual(TEXT("Overclock, 2.5 Fervour drained: 2 whole, 10"), E4Value(Overclocked, State), 10.0f);
	TestEqual(TEXT("Precision Placement, 9 traps: 2 whole fours, 20"), E4Value(CouldHold, State), 20.0f);
	State.TrapsMaximumFervourCouldHold = 3.0f;
	TestEqual(TEXT("Precision Placement, 3 traps: no whole four, nothing"), E4Value(CouldHold, State), 0.0f);
	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE4LastSurpriseTest,
	"Cataclysm.WarTreesE4.TheLastSurpriseDealsTheTotalOnceToEachEnemyAndWaitsThirtySeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Last Surprise on the ability system: two trap blows noted, 30 and 20; the trap that arms it gives each enemy 50
 * once; another trap gives nothing; a second arming within 30 s is refused; and without the stat nothing arms.
 */
bool FCataclysmWarTreesE4LastSurpriseTest::RunTest(const FString&)
{
	using namespace CataclysmWarTreesE4Test;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FWarTreesE4Holder Player(World, FVector::ZeroVector);
	FWarTreesE4Holder Trap(World, FVector(500.0f, 0.0f, 0.0f));
	FWarTreesE4Holder OtherTrap(World, FVector(1000.0f, 0.0f, 0.0f));
	FWarTreesE4Holder Enemy(World, FVector(1500.0f, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: four creatures"),
				  Player.IsUsable() && Trap.IsUsable() && OtherTrap.IsUsable() && Enemy.IsUsable()))
	{
		return false;
	}
	Player.System->NoteTrapDamageDealt(30.0f);
	Player.System->NoteTrapDamageDealt(20.0f);
	TestFalse(TEXT("control: without trap_last_surprise nothing arms"), Player.System->ArmLastSurprise(Trap.Holder));
	Player.Give(UCataclysmAbilitySystemComponent::TrapLastSurpriseStat, 1.0f);
	TestEqual(TEXT("the last 10 s hold 50"), Player.System->TrapDamageDealtWithin(10.0f), 50.0f, 0.001f);
	TestTrue(TEXT("with it, the trap arms the surprise"), Player.System->ArmLastSurprise(Trap.Holder));
	TestEqual(TEXT("another trap's blow deals nothing of it"),
			  Player.System->TakeLastSurpriseFor(OtherTrap.Holder, Enemy.Holder), 0.0f);
	TestEqual(TEXT("the arming trap's blow deals the enemy 50"),
			  Player.System->TakeLastSurpriseFor(Trap.Holder, Enemy.Holder), 50.0f, 0.001f);
	TestEqual(TEXT("and a second blow on the same enemy nothing"),
			  Player.System->TakeLastSurpriseFor(Trap.Holder, Enemy.Holder), 0.0f);
	TestEqual(TEXT("another enemy that blow reaches takes 50 too"),
			  Player.System->TakeLastSurpriseFor(Trap.Holder, OtherTrap.Holder), 50.0f, 0.001f);
	TestFalse(TEXT("a second arming within 30 s is refused"), Player.System->ArmLastSurprise(OtherTrap.Holder));
	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE4ReinforcedAndScrapTest,
	"Cataclysm.WarTreesE4.ReinforcedHousingCompoundsEachTriggeringAndScrapAndRebuildCutsTheMoveCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Reinforced Housing's "20% more damage than the last", read by `TrapDamageNamingTrap` with no trap row at all: 1 on a
 * trap's first triggering, 1.44 on its third. Scrap and Rebuild's cut: held for the next move with none running, and
 * taken off a running one with nothing held.
 */
bool FCataclysmWarTreesE4ReinforcedAndScrapTest::RunTest(const FString&)
{
	using namespace CataclysmWarTreesE4Test;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FWarTreesE4Holder Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a creature"), Player.IsUsable()))
	{
		return false;
	}
	float Increases = 0.0f;
	float More = 0.0f;
	Player.System->TrapDamageNamingTrap(FGameplayTagContainer(), nullptr, Increases, More, nullptr, 3);
	TestEqual(TEXT("control: without trap_reuse_more_percent the third triggering is unchanged"), More, 1.0f, 0.0001f);
	Player.Give(ACataclysmMinion::TrapReuseMorePercentStat, 20.0f);
	Player.System->TrapDamageNamingTrap(FGameplayTagContainer(), nullptr, Increases, More, nullptr, 1);
	TestEqual(TEXT("with it, the first triggering is unchanged"), More, 1.0f, 0.0001f);
	Player.System->TrapDamageNamingTrap(FGameplayTagContainer(), nullptr, Increases, More, nullptr, 3);
	TestEqual(TEXT("and the third is 1.2 squared, 1.44"), More, 1.44f, 0.0001f);

	Player.System->CutNextGadgetMoveCooldown(4.0f);
	TestEqual(TEXT("no move cooldown running: the cut is held for the next"),
			  Player.System->PendingGadgetMoveCooldownCut, 4.0f, 0.0001f);
	const float Now = World->GetTimeSeconds();
	Player.System->PendingGadgetMoveCooldownCut = 0.0f;
	Player.System->SkillGadgetMoveReadyAtSeconds.Add(ECataclysmAbilitySlot::Heavy, Now + 10.0f);
	Player.System->CutNextGadgetMoveCooldown(4.0f);
	TestEqual(TEXT("one running: it is 4 s sooner"),
			  Player.System->SkillGadgetMoveReadyAtSeconds.FindRef(ECataclysmAbilitySlot::Heavy), Now + 6.0f, 0.0001f);
	TestEqual(TEXT("and nothing is held"), Player.System->PendingGadgetMoveCooldownCut, 0.0f, 0.0001f);
	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE4EfficientDesignTest,
	"Cataclysm.WarTreesE4.EfficientDesignLeavesTheFirstGadgetsReserveWholeAndLowersTheSecondsByOnePercent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Efficient Design (ruled 2026-11-28, read at placing): "A gadget you place reserves 1% reduced Fervour per point for each
 * other gadget you have active". Its row at one point, `minion_reserve_percent_less` 1 naming `Type.Gadget` scaled by
 * `gadgets_active`, asked by `UCataclysmCommand::ReserveFor` as `ACataclysmMinion::SkillGadgetReserveFor` asks it before
 * the new gadget is made. With none standing a stated 100 is reserved whole; with one standing, 99.
 */
bool FCataclysmWarTreesE4EfficientDesignTest::RunTest(const FString&)
{
	using namespace CataclysmWarTreesE4Test;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FWarTreesE4Holder Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a creature"), Player.IsUsable()))
	{
		return false;
	}
	const FGameplayTagContainer GadgetTags = ACataclysmMinion::TagsOfType(ACataclysmMinion::SkillGadgetTypeName);
	FCataclysmStatModifier PerOther = E4Scaled(ECataclysmStatScale::PerGadgetActive, 1.0f, 1.0f);
	PerOther.RequiredTags.AddTag(
		FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Gadget")), /*ErrorIfNotFound=*/false));
	Player.Lines.FindOrAdd(FName(UCataclysmCommand::MinionReservePercentLessStat)).Modifiers.Add(PerOther);
	TMap<FName, FCataclysmStatInputs> Copy = Player.Lines;
	Player.System->SetStatInputs(MoveTemp(Copy));
	TestEqual(TEXT("the first gadget: none standing, a stated 100 reserved whole"),
			  UCataclysmCommand::ReserveFor(Player.Holder, 100.0f, GadgetTags), 100.0f, 0.0001f);
	ACataclysmMinion* First = ACataclysmMinion::Spawn(Player.Holder, FVector(300.0f, 0.0f, 0.0f), /*Lifetime=*/20.0f,
													  /*bBurns=*/false, FString(ACataclysmMinion::SkillGadgetTypeName));
	if (!TestTrue(TEXT("set-up: one gadget standing"), First && First->IsGadget()))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(First)) { First->Destroy(); } };
	TestEqual(TEXT("the second gadget: one other standing, 1% less, 99"),
			  UCataclysmCommand::ReserveFor(Player.Holder, 100.0f, GadgetTags), 99.0f, 0.0001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
