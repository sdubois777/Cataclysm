// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmBerserkerStates.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmWarFervour.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Interface/CataclysmPlayerEffects.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * THE BERSERKER'S STATES: Wrath, Frenzy and Berserking (`plan_death_and_war.md` slice B29, 2026-11-25). See
 * `FCataclysmBerserkerStates` for the three sentences and what is data and what is code. The generator's half is
 * `tools/tests/test_war_berserker_states_scripts.py` and `tools/tests/test_passive_effects_match_the_node_text.py`.
 */

namespace CataclysmBerserkerStatesTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	/** One stat row for `HoldBerserkerRows`: a flat row unless a bucket is named, under a condition, scoped to a tag. */
	struct FBerserkerRow
	{
		const TCHAR* Stat = nullptr;
		float Value = 0.0f;
		ECataclysmStatCondition Condition = ECataclysmStatCondition::Always;
		const TCHAR* RequiredTag = nullptr;
	};

	/** REPLACES THE WHOLE STAT LINE of `System` with these rows. */
	void HoldBerserkerRows(UCataclysmAbilitySystemComponent* System, const TArray<FBerserkerRow>& Rows)
	{
		TMap<FName, FCataclysmStatInputs> Stats;
		for (const FBerserkerRow& Each : Rows)
		{
			FCataclysmStatModifier Row;
			Row.Bucket = ECataclysmStatBucket::Flat;
			Row.Source = ECataclysmModifierSource::PassiveKeystone;
			Row.Value = Each.Value;
			Row.Condition = Each.Condition;
			if (Each.RequiredTag)
			{
				Row.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(Each.RequiredTag), false));
			}
			FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(Each.Stat));
			Line.Base = 0.0f;
			Line.Modifiers.Add(Row);
		}
		System->SetStatInputs(MoveTemp(Stats));
	}

	/** A container holding one tag, or none for a null name. */
	FGameplayTagContainer BerserkerTags(const TCHAR* Tag)
	{
		FGameplayTagContainer Made;
		if (Tag)
		{
			Made.AddTag(FGameplayTag::RequestGameplayTag(FName(Tag), false));
		}
		return Made;
	}

	/** A character with a class resource pool of 100 and nothing in it, holding no rows. No change notice is bound to
	 *  it, so a test calls `Judge` itself, as the regeneration step does. */
	struct FBerserkerHolder
	{
		explicit FBerserkerHolder(UWorld* World)
		{
			Holder = World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			System = Holder ? Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent()) : nullptr;
			if (System)
			{
				System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Holder));
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												100.0f);
				SetFervour(0.0f);
				HoldBerserkerRows(System, {});
			}
		}
		bool IsUsable() const { return Holder && System; }
		float Fervour() const
		{
			return System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}
		void SetFervour(float Value) const
		{
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Value);
		}
		bool InState(ECataclysmBerserkerState State) const { return System->BerserkerState() == State; }
		ACataclysmEnemyCharacter* Holder = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** A possessed player at level 100, its equipment and its ability system. */
	struct FBerserkerPlayer
	{
		explicit FBerserkerPlayer(UWorld* World)
		{
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!State || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(State);
			Controller->Possess(Character);
			State->SetLevelAndExperience(100, 0);
			System = State->GetCataclysmAbilitySystemComponent();
			Equipment = Character->GetEquipment();
		}
		bool IsUsable() const { return Character && System && Equipment; }
		float Fervour() const
		{
			return System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}
		void Pool(float Maximum, float Held) const
		{
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), Maximum);
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Held);
		}
		/** Put a War weapon of this base in this slot through the component's own equip. */
		bool Wear(FAutomationTestBase& Test, const TCHAR* Base, ECataclysmGearSlot Slot) const
		{
			FCataclysmItem Item;
			Item.Base = FName(Base);
			Item.GearLevel = 0;
			Item.DamageTypes = {FName(TEXT("War"))};
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			const ECataclysmEquipResult Result = Equipment->EquipInto(Item, Slot, Removed, AlsoRemoved);
			return Test.TestTrue(*FString::Printf(TEXT("set-up: %s goes on"), Base),
								 Result == ECataclysmEquipResult::Equipped || Result == ECataclysmEquipResult::Swapped);
		}
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
	};
}

// ---------------------------------------------------------------------------
// The three conditions
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBerserkerConditionsTest,
	"Cataclysm.BerserkerStates.TheThreeConditionsReadTheWeaponSlotsAndTheHeldState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `dual_wielding` holds for two items in the weapon slots and refuses one, none, and an unknown count; `in_wrath` and
 * `in_frenzy` each hold for their own state only. Then the readings as the game fills them: an enemy's count is
 * unknown, a held state reaches `CurrentConditions`, and a real player's count follows what it wears -- a sword is 1, a
 * sword and an axe 2, a greataxe 1, because a two-handed weapon is stored once.
 */
bool FCataclysmBerserkerConditionsTest::RunTest(const FString&)
{
	using namespace CataclysmBerserkerStatesTest;
	FCataclysmStatConditions State;
	const auto Holds = [&State](ECataclysmStatCondition Condition)
	{
		return UCataclysmStatPipeline::ConditionHolds(Condition, 0.0f, State);
	};
	TestFalse(TEXT("an unknown count of worn weapons refuses dual_wielding"),
			  Holds(ECataclysmStatCondition::DualWielding));
	State.WeaponsWorn = 1;
	TestFalse(TEXT("one item is not dual wielding"), Holds(ECataclysmStatCondition::DualWielding));
	State.WeaponsWorn = 2;
	TestTrue(TEXT("two items are"), Holds(ECataclysmStatCondition::DualWielding));
	TestFalse(TEXT("no state held: not in Wrath"), Holds(ECataclysmStatCondition::InWrath));
	TestFalse(TEXT("no state held: not in Frenzy"), Holds(ECataclysmStatCondition::InFrenzy));
	State.bInWrath = true;
	TestTrue(TEXT("in Wrath"), Holds(ECataclysmStatCondition::InWrath));
	TestFalse(TEXT("Wrath is not Frenzy"), Holds(ECataclysmStatCondition::InFrenzy));
	State.bInWrath = false;
	State.bInFrenzy = true;
	TestTrue(TEXT("in Frenzy"), Holds(ECataclysmStatCondition::InFrenzy));
	TestFalse(TEXT("Frenzy is not Wrath"), Holds(ECataclysmStatCondition::InWrath));

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FBerserkerHolder Enemy(World);
	if (!TestTrue(TEXT("set-up: a character with an ability system"), Enemy.IsUsable()))
	{
		return false;
	}
	TestEqual(TEXT("an enemy has no equipment, so its count of worn weapons is unknown"),
			  Enemy.System->CurrentConditions().WeaponsWorn, -1);
	TestFalse(TEXT("control: in no state, the reading says neither"), Enemy.System->CurrentConditions().bInWrath
			  || Enemy.System->CurrentConditions().bInFrenzy);
	Enemy.System->SetBerserkerState(ECataclysmBerserkerState::Wrath);
	TestTrue(TEXT("a held Wrath reaches the reading"), Enemy.System->CurrentConditions().bInWrath);
	TestFalse(TEXT("and only Wrath"), Enemy.System->CurrentConditions().bInFrenzy);
	Enemy.System->SetBerserkerState(ECataclysmBerserkerState::Frenzy);
	TestTrue(TEXT("a held Frenzy reaches the reading"), Enemy.System->CurrentConditions().bInFrenzy);
	TestFalse(TEXT("and only Frenzy"), Enemy.System->CurrentConditions().bInWrath);

	FBerserkerPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player with equipment"), Player.IsUsable()))
	{
		return false;
	}
	Player.Equipment->UnequipEverything();
	TestEqual(TEXT("a player wearing nothing holds 0 items in its weapon slots"),
			  Player.System->CurrentConditions().WeaponsWorn, 0);
	if (Player.Wear(*this, TEXT("Weapon_Sword"), ECataclysmGearSlot::Weapon1))
	{
		TestEqual(TEXT("a sword is 1"), Player.System->CurrentConditions().WeaponsWorn, 1);
	}
	if (Player.Wear(*this, TEXT("Weapon_Axe"), ECataclysmGearSlot::Weapon2))
	{
		TestEqual(TEXT("a sword and an axe are 2, which is dual wielding"),
				  Player.System->CurrentConditions().WeaponsWorn, 2);
	}
	Player.Equipment->UnequipEverything();
	if (Player.Wear(*this, TEXT("Weapon_Greataxe"), ECataclysmGearSlot::Weapon1))
	{
		TestEqual(TEXT("a greataxe is 1: a two-handed weapon is stored once"),
				  Player.System->CurrentConditions().WeaponsWorn, 1);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Wrath
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBerserkerWrathTest,
	"Cataclysm.BerserkerStates.AFullPoolBeginsWrathItsDrainEmptiesThePoolAndAnEmptyPoolEndsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Wrath node's flag and its drain of 5 under `in_wrath`. THE CONTROLS: a pool one short of full begins nothing, and
 * the drain, being under the state, takes nothing outside it. Then a full pool begins Wrath, the drain takes 5 a second,
 * an emptied pool ends it, and a refilled pool begins it again. A state also ends when its flag stops holding, with
 * Fervour left; and stated under `wielding_two_handed_weapon`, the flag never holds for a character with no weapon.
 */
bool FCataclysmBerserkerWrathTest::RunTest(const FString&)
{
	using namespace CataclysmBerserkerStatesTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FBerserkerHolder Enemy(World);
	if (!TestTrue(TEXT("set-up: a character with a class resource pool"), Enemy.IsUsable()))
	{
		return false;
	}
	using States = FCataclysmBerserkerStates;
	HoldBerserkerRows(Enemy.System, {{States::WrathOnFullFervourStat, 1.0f},
									 {States::DrainPerSecondStat, 5.0f, ECataclysmStatCondition::InWrath}});

	Enemy.SetFervour(99.0f);
	States::Judge(Enemy.System);
	TestTrue(TEXT("control: a pool one short of full begins nothing"), Enemy.InState(ECataclysmBerserkerState::None));
	States::DrainStep(Enemy.Holder, 1.0f);
	TestEqual(TEXT("control: the drain is under in_wrath, so nothing drains outside Wrath"), Enemy.Fervour(), 99.0f,
			  0.001f);

	Enemy.SetFervour(100.0f);
	TestTrue(TEXT("a full pool begins Wrath"), States::Judge(Enemy.System));
	TestTrue(TEXT("and the character is in it"), Enemy.InState(ECataclysmBerserkerState::Wrath));
	TestEqual(TEXT("which the list of what affects the player names"), States::StateTextFor(Enemy.Holder),
			  FString(States::WrathText));
	States::DrainStep(Enemy.Holder, 1.0f);
	TestEqual(TEXT("a second in Wrath drains 5"), Enemy.Fervour(), 95.0f, 0.001f);
	States::DrainStep(Enemy.Holder, 0.25f);
	TestEqual(TEXT("and a quarter-second step a quarter of it"), Enemy.Fervour(), 93.75f, 0.001f);
	TestTrue(TEXT("a pool short of full does not end it"), Enemy.InState(ECataclysmBerserkerState::Wrath));

	Enemy.SetFervour(3.0f);
	States::DrainStep(Enemy.Holder, 1.0f);
	TestEqual(TEXT("the drain stops at nought"), Enemy.Fervour(), 0.0f, 0.001f);
	TestTrue(TEXT("and an empty pool ends Wrath"), Enemy.InState(ECataclysmBerserkerState::None));
	TestEqual(TEXT("and the list names nothing"), States::StateTextFor(Enemy.Holder), FString());

	Enemy.SetFervour(100.0f);
	States::Judge(Enemy.System);
	TestTrue(TEXT("a refilled pool begins Wrath again"), Enemy.InState(ECataclysmBerserkerState::Wrath));

	HoldBerserkerRows(Enemy.System, {{States::DrainPerSecondStat, 5.0f, ECataclysmStatCondition::InWrath}});
	States::Judge(Enemy.System);
	TestTrue(TEXT("a state ends when its flag stops holding, with Fervour left"),
			 Enemy.InState(ECataclysmBerserkerState::None));

	HoldBerserkerRows(Enemy.System, {{States::WrathOnFullFervourStat, 1.0f,
									  ECataclysmStatCondition::WieldingTwoHandedWeapon}});
	Enemy.SetFervour(100.0f);
	States::Judge(Enemy.System);
	TestTrue(TEXT("stated under wielding_two_handed_weapon, the flag never holds for a character with no weapon"),
			 Enemy.InState(ECataclysmBerserkerState::None));
	return true;
}

// ---------------------------------------------------------------------------
// Frenzy, the order, and Berserking armed
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBerserkerFrenzyTest,
	"Cataclysm.BerserkerStates.FrenzyBeginsAsWrathDoesAndEitherStateDisarmsBerserking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Frenzy flag alone begins Frenzy at a full pool; with both flags Wrath is asked first. The Berserking row alone arms
 * Berserking at a full pool, and a pool that then falls keeps it armed for the next melee attack. A state begun while it
 * waits disarms it (the owner's answer to Q21), and a character that no longer holds the row is disarmed.
 */
bool FCataclysmBerserkerFrenzyTest::RunTest(const FString&)
{
	using namespace CataclysmBerserkerStatesTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FBerserkerHolder Enemy(World);
	if (!TestTrue(TEXT("set-up: a character with a class resource pool"), Enemy.IsUsable()))
	{
		return false;
	}
	using States = FCataclysmBerserkerStates;

	HoldBerserkerRows(Enemy.System, {{States::FrenzyOnFullFervourStat, 1.0f}});
	Enemy.SetFervour(100.0f);
	States::Judge(Enemy.System);
	TestTrue(TEXT("the Frenzy flag begins Frenzy at a full pool"), Enemy.InState(ECataclysmBerserkerState::Frenzy));
	TestEqual(TEXT("which the list names"), States::StateTextFor(Enemy.Holder), FString(States::FrenzyText));

	Enemy.System->SetBerserkerState(ECataclysmBerserkerState::None);
	HoldBerserkerRows(Enemy.System, {{States::WrathOnFullFervourStat, 1.0f}, {States::FrenzyOnFullFervourStat, 1.0f}});
	States::Judge(Enemy.System);
	TestTrue(TEXT("with both flags Wrath is asked first"), Enemy.InState(ECataclysmBerserkerState::Wrath));

	Enemy.System->SetBerserkerState(ECataclysmBerserkerState::None);
	HoldBerserkerRows(Enemy.System, {{States::BerserkingDamagePerFervourStat, 10.0f, ECataclysmStatCondition::Always,
									  TEXT("Type.Melee")}});
	Enemy.SetFervour(99.0f);
	States::Judge(Enemy.System);
	TestFalse(TEXT("control: a pool one short of full does not arm Berserking"), Enemy.System->IsBerserkingArmed());
	Enemy.SetFervour(100.0f);
	States::Judge(Enemy.System);
	TestTrue(TEXT("the Berserking row arms it at a full pool"), Enemy.System->IsBerserkingArmed());
	TestTrue(TEXT("and begins no state"), Enemy.InState(ECataclysmBerserkerState::None));
	TestEqual(TEXT("which the list names"), States::StateTextFor(Enemy.Holder), FString(States::BerserkingReadyText));
	Enemy.SetFervour(50.0f);
	States::Judge(Enemy.System);
	TestTrue(TEXT("a pool that falls keeps it armed for the next melee attack"), Enemy.System->IsBerserkingArmed());

	HoldBerserkerRows(Enemy.System, {{States::BerserkingDamagePerFervourStat, 10.0f, ECataclysmStatCondition::Always,
									  TEXT("Type.Melee")},
									 {States::FrenzyOnFullFervourStat, 1.0f}});
	Enemy.SetFervour(100.0f);
	States::Judge(Enemy.System);
	TestTrue(TEXT("a state begun while Berserking waits"), Enemy.InState(ECataclysmBerserkerState::Frenzy));
	TestFalse(TEXT("disarms it: Berserking fires only outside Wrath and Frenzy"), Enemy.System->IsBerserkingArmed());

	Enemy.System->SetBerserkerState(ECataclysmBerserkerState::None);
	Enemy.System->SetBerserkingArmed(true);
	HoldBerserkerRows(Enemy.System, {});
	Enemy.SetFervour(50.0f);
	States::Judge(Enemy.System);
	TestFalse(TEXT("a character that no longer holds the row is disarmed"), Enemy.System->IsBerserkingArmed());
	return true;
}

// ---------------------------------------------------------------------------
// Berserking's spend
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBerserkingSpendTest,
	"Cataclysm.BerserkerStates.BerserkingConsumesThePoolOnTheNextMeleeAttackThatStrikesSomebody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Armed with 10 a point on `Type.Melee` and a full pool of 100: an attack that is not melee spends nothing and leaves it
 * armed (THE CONTROL); a melee attack that struck nobody spends nothing; a melee attack that struck two consumes all 100
 * for 1,000% increased damage and disarms; and the next attack buys nothing. Inside a state it is never armed.
 */
bool FCataclysmBerserkingSpendTest::RunTest(const FString&)
{
	using namespace CataclysmBerserkerStatesTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FBerserkerHolder Enemy(World);
	const FGameplayTagContainer Melee = BerserkerTags(TEXT("Type.Melee"));
	if (!TestTrue(TEXT("set-up: a character with a class resource pool"), Enemy.IsUsable())
		|| !TestEqual(TEXT("set-up: the melee tag exists"), Melee.Num(), 1))
	{
		return false;
	}
	using States = FCataclysmBerserkerStates;
	HoldBerserkerRows(Enemy.System, {{States::BerserkingDamagePerFervourStat, 10.0f, ECataclysmStatCondition::Always,
									  TEXT("Type.Melee")}});
	Enemy.SetFervour(100.0f);
	States::Judge(Enemy.System);
	if (!TestTrue(TEXT("set-up: armed"), Enemy.System->IsBerserkingArmed()))
	{
		return false;
	}

	TestEqual(TEXT("control: an attack that is not melee buys nothing"),
			  States::SpendBerserking(Enemy.System, FGameplayTagContainer(), 1), 0.0f);
	TestEqual(TEXT("and spends nothing"), Enemy.Fervour(), 100.0f, 0.001f);
	TestTrue(TEXT("and leaves it armed"), Enemy.System->IsBerserkingArmed());
	TestEqual(TEXT("a melee attack that struck nobody buys nothing"), States::SpendBerserking(Enemy.System, Melee, 0),
			  0.0f);
	TestEqual(TEXT("and spends nothing"), Enemy.Fervour(), 100.0f, 0.001f);
	TestEqual(TEXT("a melee attack that struck two consumes all 100 for 1,000% increased damage"),
			  States::SpendBerserking(Enemy.System, Melee, 2), 1000.0f, 0.001f);
	TestEqual(TEXT("the pool is empty"), Enemy.Fervour(), 0.0f, 0.001f);
	TestFalse(TEXT("and Berserking is spent"), Enemy.System->IsBerserkingArmed());
	TestEqual(TEXT("the next attack buys nothing"), States::SpendBerserking(Enemy.System, Melee, 2), 0.0f);

	Enemy.SetFervour(100.0f);
	Enemy.System->SetBerserkingArmed(true);
	Enemy.System->SetBerserkerState(ECataclysmBerserkerState::Wrath);
	TestFalse(TEXT("entering a state disarms it"), Enemy.System->IsBerserkingArmed());
	TestEqual(TEXT("so in Wrath a melee attack buys nothing"), States::SpendBerserking(Enemy.System, Melee, 2), 0.0f);
	TestEqual(TEXT("and spends nothing"), Enemy.Fervour(), 100.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// Frenzy's Fervour per melee hit, through a real hit notice
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFrenzyHitFervourTest,
	"Cataclysm.BerserkerStates.InFrenzyARealMeleeHitNoticeGainsTwoFervour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A hit notice announced as the game announces one, on a real possessed player holding Frenzy's 2 a melee hit under
 * `in_frenzy` and the Frenzy flag. THE CONTROL: outside Frenzy a melee hit gains nothing. In Frenzy a melee hit gains 2,
 * and a hit that is not melee nothing.
 */
bool FCataclysmFrenzyHitFervourTest::RunTest(const FString&)
{
	using namespace CataclysmBerserkerStatesTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FBerserkerPlayer Player(World);
	ACataclysmEnemyCharacter* Enemy =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	const FGameplayTagContainer Melee = BerserkerTags(TEXT("Type.Melee"));
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable())
		|| !TestNotNull(TEXT("set-up: an enemy"), Enemy)
		|| !TestNotNull(TEXT("set-up: the announcer"), Events)
		|| !TestEqual(TEXT("set-up: the melee tag exists"), Melee.Num(), 1))
	{
		return false;
	}
	Enemy->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	HoldBerserkerRows(Player.System, {{FCataclysmWarFervour::PerHitDealtStat, 2.0f, ECataclysmStatCondition::InFrenzy,
									   TEXT("Type.Melee")},
									  {FCataclysmBerserkerStates::FrenzyOnFullFervourStat, 1.0f}});
	Player.Pool(100.0f, 10.0f);

	const FGameplayTagContainer NoTags;
	const auto Announce = [&](const FGameplayTagContainer* Tags)
	{
		FCataclysmHitNotice Notice;
		Notice.Attacker = Player.Character;
		Notice.Target = Enemy;
		Notice.SkillTags = Tags;
		Notice.DealtToHealth = 50.0f;
		Events->OnHit.Broadcast(Notice);
	};

	Announce(&Melee);
	TestEqual(TEXT("control: outside Frenzy a melee hit gains nothing"), Player.Fervour(), 10.0f, 0.001f);
	Player.System->SetBerserkerState(ECataclysmBerserkerState::Frenzy);
	Announce(&Melee);
	TestEqual(TEXT("in Frenzy a melee hit gains 2"), Player.Fervour(), 12.0f, 0.001f);
	TestTrue(TEXT("and the gain does not end Frenzy"), Player.System->BerserkerState() == ECataclysmBerserkerState::Frenzy);
	Announce(&NoTags);
	TestEqual(TEXT("a hit that is not melee gains nothing"), Player.Fervour(), 12.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// What the player sees
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBerserkerDisplayTest,
	"Cataclysm.BerserkerStates.AWarRateDrawsTheFervourBarAndTheListNamesTheState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE FERVOUR BAR, which until 2026-11-25 no War character drew: with no generator and nothing reserved there is no bar
 * (THE CONTROL); a War rate with no attribute, here the Berserker's per critical strike, draws one. THE LIST of what
 * affects the player names nothing of the Berserker's in no state (the control), "Wrath" in Wrath, "Frenzy" and not
 * "Wrath" in Frenzy, and "Berserking ready" while Berserking waits.
 */
bool FCataclysmBerserkerDisplayTest::RunTest(const FString&)
{
	using namespace CataclysmBerserkerStatesTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FBerserkerHolder Enemy(World);
	if (!TestTrue(TEXT("set-up: a character with a class resource pool"), Enemy.IsUsable()))
	{
		return false;
	}

	float Fervour = 0.0f;
	float Maximum = 0.0f;
	TestFalse(TEXT("control: no generator and nothing reserved, so no bar"),
			  UCataclysmCombatOverlay::FervourOf(Enemy.Holder, Fervour, Maximum));
	TestFalse(TEXT("control: and no War rate"), FCataclysmWarFervour::HasARate(Enemy.System));
	HoldBerserkerRows(Enemy.System, {{FCataclysmWarFervour::PerCriticalStrikeStat, 1.0f}});
	TestTrue(TEXT("a War rate is a generator"), FCataclysmWarFervour::HasARate(Enemy.System));
	TestTrue(TEXT("so the bar is drawn"), UCataclysmCombatOverlay::FervourOf(Enemy.Holder, Fervour, Maximum));
	TestEqual(TEXT("against the pool's maximum"), Maximum, 100.0f, 0.001f);

	const auto Named = [&Enemy](const TCHAR* Text)
	{
		for (const FCataclysmPlayerEffect& Entry : UCataclysmPlayerEffects::EntriesFor(Enemy.Holder))
		{
			if (Entry.Name == Text)
			{
				return true;
			}
		}
		return false;
	};
	using States = FCataclysmBerserkerStates;
	TestFalse(TEXT("control: in no state the list names no Wrath"), Named(States::WrathText));
	TestFalse(TEXT("control: nor Frenzy"), Named(States::FrenzyText));
	TestFalse(TEXT("control: nor Berserking"), Named(States::BerserkingReadyText));
	Enemy.System->SetBerserkerState(ECataclysmBerserkerState::Wrath);
	TestTrue(TEXT("in Wrath the list names Wrath"), Named(States::WrathText));
	Enemy.System->SetBerserkerState(ECataclysmBerserkerState::Frenzy);
	TestTrue(TEXT("in Frenzy the list names Frenzy"), Named(States::FrenzyText));
	TestFalse(TEXT("and not Wrath"), Named(States::WrathText));
	Enemy.System->SetBerserkerState(ECataclysmBerserkerState::None);
	Enemy.System->SetBerserkingArmed(true);
	TestTrue(TEXT("while Berserking waits the list names it"), Named(States::BerserkingReadyText));
	return true;
}

// ---------------------------------------------------------------------------
// The generated table
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBerserkerRowsTest,
	"Cataclysm.BerserkerStates.WrathFrenzyAndBerserkingGrantTheirRowsFromTheGeneratedTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The generated table, as the game loads it: each node's eight rows, its flag under its weapon rule and every other row
 * under its state. Read after the editor step rebuilds `DT_PassiveEffects`.
 */
bool FCataclysmBerserkerRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effect table loads"), Effects))
	{
		return false;
	}
	struct FExpected
	{
		const TCHAR* Node;
		const TCHAR* Stat;
		const TCHAR* Kind;
		float Value;
		const TCHAR* Condition;
	};
	const TArray<FExpected> Expected = {
		{TEXT("Berserker_twoh_001"), FCataclysmBerserkerStates::WrathOnFullFervourStat, TEXT("flat"), 1.0f,
		 TEXT("wielding_two_handed_weapon")},
		{TEXT("Berserker_twoh_001"), TEXT("attack_damage"), TEXT("increased"), 15.0f, TEXT("in_wrath")},
		{TEXT("Berserker_twoh_001"), FCataclysmBerserkerStates::DrainPerSecondStat, TEXT("flat"), 5.0f, TEXT("in_wrath")},
		{TEXT("Berserker_dw_001"), FCataclysmBerserkerStates::FrenzyOnFullFervourStat, TEXT("flat"), 1.0f,
		 TEXT("dual_wielding")},
		{TEXT("Berserker_dw_001"), TEXT("attack_speed"), TEXT("increased"), 20.0f, TEXT("in_frenzy")},
		{TEXT("Berserker_dw_001"), FCataclysmWarFervour::PerHitDealtStat, TEXT("flat"), 2.0f, TEXT("in_frenzy")},
		{TEXT("Berserker_dw_001"), FCataclysmBerserkerStates::DrainPerSecondStat, TEXT("flat"), 8.0f, TEXT("in_frenzy")},
		{TEXT("Berserker_trunk_009"), FCataclysmBerserkerStates::BerserkingDamagePerFervourStat, TEXT("flat"), 10.0f,
		 TEXT("")},
	};
	for (const FExpected& Each : Expected)
	{
		bool bFound = false;
		for (const FCataclysmPassiveEffectRow* Row : UCataclysmPassiveTree::EffectsFor(Effects, FName(Each.Node)))
		{
			if (Row && Row->Stat == Each.Stat && Row->ValueKind.Equals(Each.Kind)
				&& Row->Condition.Equals(Each.Condition) && FMath::IsNearlyEqual(Row->ValuePerPoint, Each.Value))
			{
				bFound = true;
			}
		}
		if (!TestTrue(*FString::Printf(TEXT("%s grants %s %s %g under '%s'"), Each.Node, Each.Kind, Each.Stat, Each.Value,
									   Each.Condition),
					  bFound))
		{
			AddError(TEXT("If the row is in game/Data/PassiveEffects.csv, DT_PassiveEffects is older than it: run  "
						  "python tools/run_editor_python.py tools/generate_datatable_assets.py"));
		}
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
