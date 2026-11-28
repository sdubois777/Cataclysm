// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmLeech.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * THE WAR TREES' ENGINE SLICE E1 (2026-11-27): the conditions, scales, events and immunity stats its rows read. See the
 * decisions entry "The War passive trees finished". The generator's half is `tools/tests/test_war_trees_data_rows_scripts.py`
 * and `tools/tests/test_passive_effects_match_the_node_text.py`; the stats asked under a scale or a condition are probed
 * in `CataclysmStatExemptionTests.cpp`.
 */

namespace CataclysmWarTreesE1Test
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	/** One row on one stat line, flat unless a bucket is given. */
	struct FWarTreesE1Row
	{
		const TCHAR* Stat = nullptr;
		float Value = 0.0f;
		ECataclysmStatBucket Bucket = ECataclysmStatBucket::Flat;
		ECataclysmStatScale Scale = ECataclysmStatScale::Fixed;
		float ScaleStep = 0.0f;
	};

	/** REPLACES THE WHOLE STAT LINE of `System` with these rows, each on a base of nought. */
	void HoldWarTreesE1Rows(UCataclysmAbilitySystemComponent* System, const TArray<FWarTreesE1Row>& Rows)
	{
		TMap<FName, FCataclysmStatInputs> Stats;
		for (const FWarTreesE1Row& Each : Rows)
		{
			FCataclysmStatModifier Row;
			Row.Bucket = Each.Bucket;
			Row.Source = ECataclysmModifierSource::PassiveKeystone;
			Row.Value = Each.Value;
			Row.Scale = Each.Scale;
			Row.ScaleStep = Each.ScaleStep;
			FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(Each.Stat));
			Line.Base = 0.0f;
			Line.Modifiers.Add(Row);
		}
		System->SetStatInputs(MoveTemp(Stats));
	}

	/** A creature with a class resource pool of 100 and nothing in it, holding no rows. */
	struct FWarTreesE1Holder
	{
		FWarTreesE1Holder(UWorld* World, const FVector& At)
		{
			Holder = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
			System = Holder ? Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent()) : nullptr;
			if (System)
			{
				System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Holder));
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												100.0f);
				SetFervour(0.0f);
				HoldWarTreesE1Rows(System, {});
			}
		}
		bool IsUsable() const { return Holder && System; }
		void SetFervour(float Value) const
		{
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Value);
		}
		/** An action granting one stack under `Key` on `Event`, so a test can see the event was raised. */
		void StackOn(const TCHAR* Event, const TCHAR* Key) const
		{
			FCataclysmPoolAction Grant;
			Grant.Event = FName(Event);
			Grant.StackKey = FName(Key);
			Grant.StackSeconds = 5.0f;
			Grant.StackCap = 5;
			System->SetPoolActions({Grant});
		}
		int32 Stacks(const TCHAR* Key) const { return System->OwnStacksHeld(FName(Key)); }
		ACataclysmEnemyCharacter* Holder = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** What one modifier on a base of nought gives against this state. */
	float WarTreesE1Value(const FCataclysmStatModifier& Row, const FCataclysmStatConditions& State)
	{
		return UCataclysmStatPipeline::Evaluate(0.0f, {Row}, FGameplayTagContainer(), State).Final;
	}

	FCataclysmStatModifier WarTreesE1Scaled(ECataclysmStatScale Scale, float Value, float Step, int32 MaxSteps = 0,
											float ReachMetres = -1.0f, float Offset = 0.0f)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::Flat;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		Row.Scale = Scale;
		Row.ScaleStep = Step;
		Row.ScaleMaxSteps = MaxSteps;
		Row.ReachMetres = ReachMetres;
		Row.ScaleOffset = Offset;
		return Row;
	}
}

// ---------------------------------------------------------------------------
// The conditions, read from the state
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE1ConditionsTest,
	"Cataclysm.WarTreesE1.TheSevenConditionsReadTheirFieldsAndAnUnknownReadingRefuses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Each of the seven conditions against the field it reads. THE CONTROLS are the unknown readings: -1 shields refuses
 * both shield conditions, -1 for the hit in hand refuses both hit conditions, and an empty debuff container refuses the
 * bleed.
 */
bool FCataclysmWarTreesE1ConditionsTest::RunTest(const FString&)
{
	FCataclysmStatConditions State;
	const auto Holds = [&State](ECataclysmStatCondition Condition)
	{
		return UCataclysmStatPipeline::ConditionHolds(Condition, 0.0f, State);
	};

	TestFalse(TEXT("an unknown shield count holds no shield"), Holds(ECataclysmStatCondition::HoldingShield));
	TestFalse(TEXT("and is not 'no shield' either"), Holds(ECataclysmStatCondition::NotHoldingShield));
	State.ShieldsHeld = 0;
	TestFalse(TEXT("none held: not holding a shield"), Holds(ECataclysmStatCondition::HoldingShield));
	TestTrue(TEXT("none held: 'no shield' holds"), Holds(ECataclysmStatCondition::NotHoldingShield));
	State.ShieldsHeld = 1;
	TestTrue(TEXT("one held: holding a shield"), Holds(ECataclysmStatCondition::HoldingShield));
	TestFalse(TEXT("one held: 'no shield' refuses"), Holds(ECataclysmStatCondition::NotHoldingShield));

	TestFalse(TEXT("no hit in hand is neither critical"), Holds(ECataclysmStatCondition::HitIsCritical));
	TestFalse(TEXT("nor not critical"), Holds(ECataclysmStatCondition::HitIsNotCritical));
	State.HitInHandCritical = 1;
	TestTrue(TEXT("a critical hit in hand"), Holds(ECataclysmStatCondition::HitIsCritical));
	TestFalse(TEXT("is not a non-critical one"), Holds(ECataclysmStatCondition::HitIsNotCritical));
	State.HitInHandCritical = 0;
	TestTrue(TEXT("a non-critical hit in hand"), Holds(ECataclysmStatCondition::HitIsNotCritical));
	TestFalse(TEXT("is not a critical one"), Holds(ECataclysmStatCondition::HitIsCritical));

	TestFalse(TEXT("no snapshot: not an attack at full Fervour"), Holds(ECataclysmStatCondition::AttackAtFullFervour));
	State.bAttackBeganAtFullFervour = true;
	TestTrue(TEXT("the snapshot holds"), Holds(ECataclysmStatCondition::AttackAtFullFervour));

	TestFalse(TEXT("not held by crowd control"), Holds(ECataclysmStatCondition::WhileCrowdControlled));
	State.bSelfCrowdControlled = true;
	TestTrue(TEXT("held by crowd control"), Holds(ECataclysmStatCondition::WhileCrowdControlled));

	TestFalse(TEXT("an empty debuff container is not bleeding"), Holds(ECataclysmStatCondition::TargetCarriesBleed));
	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	if (TestTrue(TEXT("set-up: the vocabulary holds the Bleed's tag"), Bleed.IsValid()))
	{
		State.TargetDebuffs.AddTag(Bleed);
		TestTrue(TEXT("a target carrying the Bleed's tag is bleeding"), Holds(ECataclysmStatCondition::TargetCarriesBleed));
	}
	return true;
}

// ---------------------------------------------------------------------------
// The scales, read from the state
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE1ScalesTest,
	"Cataclysm.WarTreesE1.TheSixScalesCountWholeStepsAndAnUnknownReadingCountsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Each of the six scales against its reading, each with its unknown case first: Thick Skin's 1 per 4% of the maximum
 * held (and an offset of 50 counting only what is above half), Executioner's 1 per 1% of the target's health missing,
 * Iron Will's 1 per 2% increased maximum health, Open Wounds' 3 per bleeding enemy within 4 metres up to 5, and the two
 * "recently" counts.
 */
bool FCataclysmWarTreesE1ScalesTest::RunTest(const FString&)
{
	using namespace CataclysmWarTreesE1Test;
	FCataclysmStatConditions State;

	const FCataclysmStatModifier Share = WarTreesE1Scaled(ECataclysmStatScale::PerPercentOfClassResourceHeld, 1.0f, 4.0f);
	TestEqual(TEXT("an unknown pool scales Thick Skin to nothing"), WarTreesE1Value(Share, State), 0.0f);
	State.ClassResourceHeld = 50.0f;
	State.ClassResourceMaximum = 200.0f;
	TestEqual(TEXT("50 of 200 is 25%: six whole steps of 4%"), WarTreesE1Value(Share, State), 6.0f, 0.001f);
	TestEqual(TEXT("and with an offset of 50%, nothing below half counts"),
			  WarTreesE1Value(WarTreesE1Scaled(ECataclysmStatScale::PerPercentOfClassResourceHeld, 1.0f, 1.0f, 0, -1.0f,
											   50.0f), State), 0.0f, 0.001f);
	State.ClassResourceMaximum = 0.0f;
	TestEqual(TEXT("a maximum of nothing scales to nothing"), WarTreesE1Value(Share, State), 0.0f);

	const FCataclysmStatModifier Missing = WarTreesE1Scaled(ECataclysmStatScale::PerPercentOfTargetHealthMissing, 1.0f, 1.0f);
	TestEqual(TEXT("an unread target scales Executioner to nothing"), WarTreesE1Value(Missing, State), 0.0f);
	State.TargetHealthPercent = 70.0f;
	TestEqual(TEXT("a target at 70% is missing 30%"), WarTreesE1Value(Missing, State), 30.0f, 0.001f);

	const FCataclysmStatModifier Increased = WarTreesE1Scaled(ECataclysmStatScale::PerPercentOfIncreasedMaximumHealth, 1.0f, 2.0f);
	TestEqual(TEXT("unread increases scale Iron Will to nothing"), WarTreesE1Value(Increased, State), 0.0f);
	State.IncreasedMaximumHealthPercent = 31.0f;
	TestEqual(TEXT("31% increased is fifteen whole steps of 2%"), WarTreesE1Value(Increased, State), 15.0f, 0.001f);

	const FCataclysmStatModifier Bleeding =
		WarTreesE1Scaled(ECataclysmStatScale::PerBleedingEnemyInReach, 3.0f, 1.0f, /*MaxSteps=*/5, /*Reach=*/4.0f);
	TestEqual(TEXT("nobody bleeding is near: nothing"), WarTreesE1Value(Bleeding, State), 0.0f);
	State.BleedingHostileDistancesMetres = {1.0f, 3.5f, 6.0f};
	TestEqual(TEXT("two of three bleeding enemies are within 4 metres"), WarTreesE1Value(Bleeding, State), 6.0f, 0.001f);
	State.BleedingHostileDistancesMetres = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
	TestEqual(TEXT("seven within reach count as five, the cap"), WarTreesE1Value(Bleeding, State), 15.0f, 0.001f);

	const FCataclysmStatModifier Struck = WarTreesE1Scaled(ECataclysmStatScale::PerEnemyStruckRecently, 2.0f, 1.0f);
	TestEqual(TEXT("an unread count of enemies struck counts none"), WarTreesE1Value(Struck, State), 0.0f);
	State.EnemiesStruckRecently = 3;
	TestEqual(TEXT("three struck recently: 6"), WarTreesE1Value(Struck, State), 6.0f, 0.001f);

	const FCataclysmStatModifier StruckYou = WarTreesE1Scaled(ECataclysmStatScale::PerEnemyThatStruckYouRecently, 2.0f, 1.0f);
	TestEqual(TEXT("an unread count of attackers counts none"), WarTreesE1Value(StruckYou, State), 0.0f);
	State.EnemiesThatStruckYouRecently = 2;
	TestEqual(TEXT("two attackers recently: 4"), WarTreesE1Value(StruckYou, State), 4.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// The readings, as the game fills them
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE1ShieldsTest,
	"Cataclysm.WarTreesE1.APlayersShieldCountFollowsWhatItWearsAndAnEnemysIsUnknown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** An enemy has no equipment, so its count is unknown; a player wearing a sword holds 0 shields, and a sword and a
 *  shield 1. */
bool FCataclysmWarTreesE1ShieldsTest::RunTest(const FString&)
{
	using namespace CataclysmWarTreesE1Test;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWarTreesE1Holder Enemy(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a creature"), Enemy.IsUsable()))
	{
		return false;
	}
	TestEqual(TEXT("an enemy's shield count is unknown"), Enemy.System->CurrentConditions().ShieldsHeld, -1);

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector(0.0f, 2000.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestTrue(TEXT("set-up: a player"), State && Controller && Character))
	{
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Character);
	State->SetLevelAndExperience(100, 0);
	UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent();
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	if (!TestTrue(TEXT("set-up: its ability system and equipment"), System && Equipment))
	{
		return false;
	}
	Equipment->UnequipEverything();
	const auto Wear = [this, Equipment](const TCHAR* Base, ECataclysmGearSlot Slot)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.DamageTypes = {FName(TEXT("War"))};
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		const ECataclysmEquipResult Result = Equipment->EquipInto(Item, Slot, Removed, AlsoRemoved);
		return TestTrue(*FString::Printf(TEXT("set-up: %s goes on"), Base),
						Result == ECataclysmEquipResult::Equipped || Result == ECataclysmEquipResult::Swapped);
	};
	TestEqual(TEXT("a player wearing nothing holds no shield"), System->CurrentConditions().ShieldsHeld, 0);
	if (Wear(TEXT("Weapon_Sword"), ECataclysmGearSlot::Weapon1))
	{
		TestEqual(TEXT("a sword is no shield"), System->CurrentConditions().ShieldsHeld, 0);
	}
	if (Wear(TEXT("Weapon_Shield"), ECataclysmGearSlot::Weapon2))
	{
		TestEqual(TEXT("a sword and a shield hold one shield"), System->CurrentConditions().ShieldsHeld, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE1FullPoolTest,
	"Cataclysm.WarTreesE1.AFullPoolIsJudgedAsTheFullBarAndTheSnapshotReachesTheReading",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `IsClassResourceFull` says no at 99 of 100 and yes at 100; a snapshot set reaches `CurrentConditions` and a cleared
 *  one leaves it. */
bool FCataclysmWarTreesE1FullPoolTest::RunTest(const FString&)
{
	using namespace CataclysmWarTreesE1Test;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarTreesE1Holder Holder(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a creature with a pool"), Holder.IsUsable()))
	{
		return false;
	}
	Holder.SetFervour(99.0f);
	TestFalse(TEXT("99 of 100 is not full"), Holder.System->IsClassResourceFull());
	Holder.SetFervour(100.0f);
	TestTrue(TEXT("100 of 100 is full"), Holder.System->IsClassResourceFull());
	TestFalse(TEXT("control: with no snapshot taken the reading says no"),
			  Holder.System->CurrentConditions().bAttackBeganAtFullFervour);
	Holder.System->SetAttackBeganAtFullFervour(true);
	TestTrue(TEXT("a snapshot taken reaches the reading"), Holder.System->CurrentConditions().bAttackBeganAtFullFervour);
	Holder.System->SetAttackBeganAtFullFervour(false);
	TestFalse(TEXT("and a cleared one leaves it"), Holder.System->CurrentConditions().bAttackBeganAtFullFervour);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE1ImmunityTest,
	"Cataclysm.WarTreesE1.TheThreeImmunityStatsRefuseTheirEffectsAndNoOthers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `immune_to_stun` refuses a stun and not a knockdown; `immune_to_stagger` refuses a stagger and not a stun;
 * `immune_to_crowd_control` refuses all three. THE CONTROL is the character with no row, immune to nothing.
 */
bool FCataclysmWarTreesE1ImmunityTest::RunTest(const FString&)
{
	using namespace CataclysmWarTreesE1Test;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarTreesE1Holder Holder(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a creature"), Holder.IsUsable()))
	{
		return false;
	}
	const auto Immune = [&Holder](const TCHAR* Effect)
	{
		return UCataclysmSkillTemplate::IsImmuneTo(Holder.Holder, Effect);
	};
	TestFalse(TEXT("control: no row, a stun takes hold"), Immune(TEXT("Stun")));
	TestFalse(TEXT("control: no row, a stagger takes hold"), Immune(TEXT("Stagger")));

	HoldWarTreesE1Rows(Holder.System, {{TEXT("immune_to_stun"), 1.0f}});
	TestTrue(TEXT("immune_to_stun refuses a stun"), Immune(TEXT("Stun")));
	TestFalse(TEXT("and not a knockdown"), Immune(TEXT("Knockdown")));

	HoldWarTreesE1Rows(Holder.System, {{TEXT("immune_to_stagger"), 1.0f}});
	TestTrue(TEXT("immune_to_stagger refuses a stagger"), Immune(TEXT("Stagger")));
	TestFalse(TEXT("and not a stun"), Immune(TEXT("Stun")));

	HoldWarTreesE1Rows(Holder.System, {{TEXT("immune_to_crowd_control"), 1.0f}});
	TestTrue(TEXT("immune_to_crowd_control refuses a stun"), Immune(TEXT("Stun")));
	TestTrue(TEXT("a knockdown"), Immune(TEXT("Knockdown")));
	TestTrue(TEXT("and a stagger"), Immune(TEXT("Stagger")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE1CrowdControlEventsTest,
	"Cataclysm.WarTreesE1.AStunLandedRaisesCrowdControlTakenAndOneRefusedRaisesCrowdControlRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A designed stun that lands on a creature raises `crowd_control_taken` on it and leaves it held, so
 * `while_crowd_controlled`'s reading says yes; one refused by `immune_to_stun` raises `crowd_control_refused` instead and
 * not `crowd_control_taken`. Each event grants a stack under its own key, which is what is counted.
 */
bool FCataclysmWarTreesE1CrowdControlEventsTest::RunTest(const FString&)
{
	using namespace CataclysmWarTreesE1Test;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarTreesE1Holder Stunner(World, FVector::ZeroVector);
	FWarTreesE1Holder Taken(World, FVector(300.0f, 0.0f, 0.0f));
	FWarTreesE1Holder Refusing(World, FVector(-300.0f, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: three creatures"), Stunner.IsUsable() && Taken.IsUsable() && Refusing.IsUsable()))
	{
		return false;
	}
	Taken.StackOn(TEXT("crowd_control_taken"), TEXT("Test:taken"));
	TestFalse(TEXT("control: not held before the stun"), Taken.System->CurrentConditions().bSelfCrowdControlled);
	UCataclysmSkillEffects::ApplyStun(Stunner.Holder, Taken.Holder, /*Duration=*/2.0f, /*Damage=*/0.0f,
									  /*bStunIsDesigned=*/true);
	TestEqual(TEXT("a landed stun raises crowd_control_taken on the character it landed on"),
			  Taken.Stacks(TEXT("Test:taken")), 1);
	TestTrue(TEXT("and leaves it held, which while_crowd_controlled reads"),
			 Taken.System->CurrentConditions().bSelfCrowdControlled);

	HoldWarTreesE1Rows(Refusing.System, {{TEXT("immune_to_stun"), 1.0f}});
	FCataclysmPoolAction Refused;
	Refused.Event = FName(TEXT("crowd_control_refused"));
	Refused.StackKey = FName(TEXT("Test:refused"));
	Refused.StackSeconds = 5.0f;
	Refused.StackCap = 5;
	FCataclysmPoolAction NotTaken = Refused;
	NotTaken.Event = FName(TEXT("crowd_control_taken"));
	NotTaken.StackKey = FName(TEXT("Test:not_taken"));
	Refusing.System->SetPoolActions({Refused, NotTaken});
	UCataclysmSkillEffects::ApplyStun(Stunner.Holder, Refusing.Holder, /*Duration=*/2.0f, /*Damage=*/0.0f,
									  /*bStunIsDesigned=*/true);
	TestEqual(TEXT("a stun refused by immunity raises crowd_control_refused"), Refusing.Stacks(TEXT("Test:refused")), 1);
	TestEqual(TEXT("and not crowd_control_taken"), Refusing.Stacks(TEXT("Test:not_taken")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE1RecordsTest,
	"Cataclysm.WarTreesE1.StrikesLeechAndIncreasedHealthReachTheirReadings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Three readings filled by the game: a character struck by two others counts two that struck it recently; a health
 * leech payment raises `life_leeched` and a mana one does not; and Iron Will's maximum Fervour row reads the increased
 * maximum health line, 30% giving 15 at a step of 2.
 */
bool FCataclysmWarTreesE1RecordsTest::RunTest(const FString&)
{
	using namespace CataclysmWarTreesE1Test;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarTreesE1Holder Struck(World, FVector::ZeroVector);
	FWarTreesE1Holder First(World, FVector(300.0f, 0.0f, 0.0f));
	FWarTreesE1Holder Second(World, FVector(-300.0f, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: three creatures"), Struck.IsUsable() && First.IsUsable() && Second.IsUsable()))
	{
		return false;
	}
	TestEqual(TEXT("control: struck by nobody"), Struck.System->CurrentConditions().EnemiesThatStruckYouRecently, 0);
	Struck.System->NoteStruckBy(First.System, /*bCritical=*/false);
	Struck.System->NoteStruckBy(Second.System, /*bCritical=*/false);
	Struck.System->NoteStruckBy(First.System, /*bCritical=*/false);
	TestEqual(TEXT("struck by two characters, one of them twice: two"),
			  Struck.System->CurrentConditions().EnemiesThatStruckYouRecently, 2);

	Struck.StackOn(TEXT("life_leeched"), TEXT("Test:leeched"));
	FCataclysmLeechPayment Mana;
	Mana.Pool = ECataclysmLeechPool::Mana;
	Mana.Remaining = 10.0f;
	Mana.SecondsLeft = 3.0f;
	Struck.System->AddLeechPayment(Mana);
	TestEqual(TEXT("control: a mana payment is not a life leech"), Struck.Stacks(TEXT("Test:leeched")), 0);
	FCataclysmLeechPayment Health = Mana;
	Health.Pool = ECataclysmLeechPool::Health;
	Struck.System->AddLeechPayment(Health);
	TestEqual(TEXT("a health payment raises life_leeched"), Struck.Stacks(TEXT("Test:leeched")), 1);

	HoldWarTreesE1Rows(Struck.System, {
		{TEXT("max_health"), 30.0f, ECataclysmStatBucket::Increased},
		{TEXT("class_resource"), 1.0f, ECataclysmStatBucket::Flat, ECataclysmStatScale::PerPercentOfIncreasedMaximumHealth, 2.0f}});
	TestEqual(TEXT("30% increased maximum health is 15 maximum Fervour at 1 per 2%"),
			  Struck.System->StatForSkill(FName(TEXT("class_resource")), FGameplayTagContainer(), 0.0f), 15.0f, 0.001f);
	HoldWarTreesE1Rows(Struck.System, {
		{TEXT("class_resource"), 1.0f, ECataclysmStatBucket::Flat, ECataclysmStatScale::PerPercentOfIncreasedMaximumHealth, 2.0f}});
	TestEqual(TEXT("and with no increase on the line, nothing"),
			  Struck.System->StatForSkill(FName(TEXT("class_resource")), FGameplayTagContainer(), 0.0f), 0.0f, 0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
