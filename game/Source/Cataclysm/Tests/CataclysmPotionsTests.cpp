// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmPotions.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmHUD.h"
#include "Interface/CataclysmSkillBar.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The four potion slots. Issue #806.
 *
 * ON A REAL, POSSESSED PLAYER CHARACTER, because only a player character holds
 * potions and a kill finds its killer through the first player controller.
 *
 * THE HEAL IS STEPPED BY HAND with `UCataclysmPotions::HealStep`, a quarter-second
 * at a time as the player's regeneration step pays it, and the clock is never
 * run, so no regeneration of the character's own adds to what is measured.
 */
namespace CataclysmPotionsTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Potions = UCataclysmPotions;

	constexpr float MaximumHealth = 1'000.0f;
	constexpr float Step = 0.25f;

	struct FPlayer
	{
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;

		bool IsUsable() const { return Character && AbilitySystem; }

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		}

		void SetHealth(float Health) const
		{
			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(),
												   MaximumHealth);
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), Health);
		}

		void EmptySlots() const
		{
			for (int32 Slot = 0; Slot < Potions::SlotCount; ++Slot)
			{
				AbilitySystem->SetPotionCharges(Slot, 0.0f);
			}
		}
	};

	/** A player character, possessed the way the running game wires one. */
	FPlayer SpawnPlayer(UWorld* World)
	{
		FPlayer Made;
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		Made.Character = World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
		if (!State || !Controller || !Made.Character)
		{
			Made.Character = nullptr;
			return Made;
		}
		Controller->SetPlayerState(State);
		Controller->Possess(Made.Character);
		Made.AbilitySystem = Cast<UCataclysmAbilitySystemComponent>(
			Made.Character->GetAbilitySystemComponent());
		if (Made.AbilitySystem)
		{
			Made.SetHealth(MaximumHealth);
		}
		return Made;
	}

	/** A creature of this rarity rung dies, credited to the player. */
	bool Kill(UWorld* World, int32 RarityStep, bool bDiesUnpaid = false)
	{
		ACataclysmEnemyCharacter* Victim = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (!Victim)
		{
			return false;
		}
		Victim->SetRarityStep(RarityStep);
		Victim->bDiesUnpaid = bDiesUnpaid;
		Victim->HandleDeath();
		return true;
	}

	/** Pay the running heal for this many seconds, a step at a time. */
	void Heal(const FPlayer& Player, float Seconds)
	{
		for (float Paid = 0.0f; Paid < Seconds - KINDA_SMALL_NUMBER; Paid += Step)
		{
			Potions::HealStep(Player.Character, Step);
		}
	}
}

#define CATACLYSM_POTIONS_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_POTIONS_TEST(FCataclysmPotionKillChargesTest,
	"Cataclysm.Potions.AKillFillsEverySlotByTheCreaturesRarity")
{
	using namespace CataclysmPotionsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FPlayer Player = SpawnPlayer(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"), Player.IsUsable()))
	{
		return false;
	}

	const auto Every = [&Player](float Expected)
	{
		for (int32 Slot = 0; Slot < Potions::SlotCount; ++Slot)
		{
			if (!FMath::IsNearlyEqual(Player.AbilitySystem->GetPotionCharges(Slot), Expected,
									  0.001f))
			{
				return false;
			}
		}
		return true;
	};

	TestTrue(TEXT("every slot starts full"), Every(Potions::MaxCharges));
	Player.EmptySlots();

	// Common 1, Elite 3.5, Legendary 6, Herald 11, into every slot at once.
	if (!TestTrue(TEXT("a Common creature killed"), Kill(World, 0)))
	{
		return false;
	}
	TestTrue(TEXT("a Common kill puts 1 charge in every slot"), Every(1.0f));
	Kill(World, 1);
	TestTrue(TEXT("an Elite adds 3.5 to every slot, 4.5 in all"), Every(4.5f));
	Kill(World, 2);
	TestTrue(TEXT("a Legendary adds 6, 10.5 in all"), Every(10.5f));
	TestEqual(TEXT("which is one drink in each"), Potions::DrinksIn(10.5f), 1);
	Kill(World, 3);
	TestTrue(TEXT("a Herald adds 11, 21.5 in all"), Every(21.5f));
	Kill(World, 4);
	TestTrue(TEXT("a Boss's 11 fills every slot and stops at 30"), Every(Potions::MaxCharges));

	// A DEATH THAT PAYS NOTHING FILLS NOTHING, like the experience and the drops.
	Player.EmptySlots();
	Kill(World, 4, /*bDiesUnpaid=*/true);
	TestTrue(TEXT("a creature marked to die unpaid adds nothing"), Every(0.0f));
	return true;
}

CATACLYSM_POTIONS_TEST(FCataclysmPotionDrinkHealsTest,
	"Cataclysm.Potions.ADrinkSpendsTenChargesAndHeals35PercentOfMaximumHealthOverThreeSeconds")
{
	using namespace CataclysmPotionsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FPlayer Player = SpawnPlayer(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"), Player.IsUsable()))
	{
		return false;
	}

	Player.SetHealth(100.0f);
	if (!TestEqual(TEXT("slot 1 is drunk"), Potions::Drink(Player.Character, 0),
				   ECataclysmPotionRefusal::None))
	{
		return false;
	}
	TestEqual(TEXT("it spent 10 of its 30 charges"), Player.AbilitySystem->GetPotionCharges(0),
			  20.0f, 0.001f);
	TestEqual(TEXT("and heals nothing until a step pays it"), Player.Health(), 100.0f, 0.001f);

	// ONE HEAL AT A TIME, AND A REFUSAL SPENDS NOTHING.
	TestEqual(TEXT("slot 2 is refused while the first heal runs"),
			  Potions::Drink(Player.Character, 1), ECataclysmPotionRefusal::AHealIsRunning);
	TestEqual(TEXT("and keeps its 30 charges"), Player.AbilitySystem->GetPotionCharges(1),
			  30.0f, 0.001f);

	Heal(Player, 1.0f);
	TestEqual(TEXT("one second in: a third of 350 restored"), Player.Health(),
			  100.0f + 350.0f / 3.0f, 0.01f);
	Heal(Player, 2.0f);
	TestEqual(TEXT("three seconds in: 35% of 1000 restored in all"), Player.Health(), 450.0f,
			  0.01f);
	TestFalse(TEXT("and the heal has ended"), Player.AbilitySystem->GetPotionHeal().IsRunning());
	TestEqual(TEXT("a further step restores nothing"),
			  Potions::HealStep(Player.Character, Step), 0.0f, 0.001f);
	TestEqual(TEXT("once it has ended, slot 2 can be drunk"),
			  Potions::Drink(Player.Character, 1), ECataclysmPotionRefusal::None);
	return true;
}

CATACLYSM_POTIONS_TEST(FCataclysmPotionRefusedTest,
	"Cataclysm.Potions.ADrinkIsRefusedWithTooFewChargesOrNoLivingPlayerAndSpendsNothing")
{
	using namespace CataclysmPotionsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FPlayer Player = SpawnPlayer(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"), Player.IsUsable()))
	{
		return false;
	}

	Player.AbilitySystem->SetPotionCharges(0, 9.5f);
	TestEqual(TEXT("9.5 charges are too few for a drink"), Potions::Drink(Player.Character, 0),
			  ECataclysmPotionRefusal::TooFewCharges);
	TestEqual(TEXT("and are all still there"), Player.AbilitySystem->GetPotionCharges(0), 9.5f,
			  0.001f);
	TestFalse(TEXT("and no heal started"), Player.AbilitySystem->GetPotionHeal().IsRunning());

	TestEqual(TEXT("there is no fifth slot"), Potions::Drink(Player.Character, 4),
			  ECataclysmPotionRefusal::NoSuchSlot);

	ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	TestEqual(TEXT("a creature holds no potions"), Potions::Drink(Creature, 0),
			  ECataclysmPotionRefusal::NoCharacter);

	Player.AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 0.0f);
	if (!TestTrue(TEXT("set-up: the player is dead"),
				  UCataclysmSkillEffects::IsDead(Player.Character)))
	{
		return false;
	}
	TestEqual(TEXT("a dead player cannot drink"), Potions::Drink(Player.Character, 1),
			  ECataclysmPotionRefusal::Dead);
	TestEqual(TEXT("and spends nothing"), Player.AbilitySystem->GetPotionCharges(1), 30.0f,
			  0.001f);
	return true;
}

CATACLYSM_POTIONS_TEST(FCataclysmPotionCeilingTest,
	"Cataclysm.Potions.APotionHealStopsAtTheHealingCeilingPointOfNoReturnLowers")
{
	// POINT OF NO RETURN, `Masochist_keystone_ll_kB`, writes 50 to
	// `healing_ceiling_reduction`: "You cannot be healed above 50% of your
	// maximum health". Ruled on 2026-09-25 that a potion obeys it, where Path of
	// Exile's Petrified Blood exempts flasks. The attribute is written here as the
	// row writes it.
	using namespace CataclysmPotionsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FPlayer Player = SpawnPlayer(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"), Player.IsUsable()))
	{
		return false;
	}

	Player.SetHealth(300.0f);
	Player.AbilitySystem->SetNumericAttributeBase(
		Vital::GetHealingCeilingReductionAttribute(), 50.0f);
	if (!TestEqual(TEXT("slot 1 is drunk"), Potions::Drink(Player.Character, 0),
				   ECataclysmPotionRefusal::None))
	{
		return false;
	}
	Heal(Player, 3.0f);
	TestEqual(TEXT("a drink worth 350 from 300 stops at 500, half of 1000"), Player.Health(),
			  500.0f, 0.01f);
	return true;
}

CATACLYSM_POTIONS_TEST(FCataclysmPotionDeathTest,
	"Cataclysm.Potions.ADeathEndsARunningHealAndKeepsTheCharges")
{
	using namespace CataclysmPotionsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FPlayer Player = SpawnPlayer(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"), Player.IsUsable()))
	{
		return false;
	}

	Player.SetHealth(100.0f);
	if (!TestEqual(TEXT("slot 1 is drunk"), Potions::Drink(Player.Character, 0),
				   ECataclysmPotionRefusal::None))
	{
		return false;
	}
	Player.AbilitySystem->ClearWhatDeathEnds();
	TestFalse(TEXT("what a death ends includes the heal"),
			  Player.AbilitySystem->GetPotionHeal().IsRunning());
	TestEqual(TEXT("and leaves the slot's charges as they were"),
			  Player.AbilitySystem->GetPotionCharges(0), 20.0f, 0.001f);
	return true;
}

CATACLYSM_POTIONS_TEST(FCataclysmPotionDungeonEntryTest,
	"Cataclysm.Potions.EnteringADungeonFillsEverySlot")
{
	using namespace CataclysmPotionsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->Begin(1);
	Run->AdvanceDay();
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("and the first surge put dungeons on the map"),
					 Run->DungeonCount() > 0))
	{
		return false;
	}
	Mode->SetEmpireRunForTests(Run);

	const FPlayer Player = SpawnPlayer(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"), Player.IsUsable()))
	{
		return false;
	}
	Player.EmptySlots();

	if (!TestTrue(TEXT("a dungeon is entered"),
				  Mode->EnterEmpireDungeon(Run->Dungeons[0].DungeonId)))
	{
		return false;
	}
	for (int32 Slot = 0; Slot < Potions::SlotCount; ++Slot)
	{
		TestEqual(*FString::Printf(TEXT("slot %d is full"), Slot + 1),
				  Player.AbilitySystem->GetPotionCharges(Slot), Potions::MaxCharges, 0.001f);
	}
	return true;
}

CATACLYSM_POTIONS_TEST(FCataclysmPotionLayoutTest,
	"Cataclysm.Potions.TheBoxesDoNotRunIntoTheSkillBarOrTheVitals")
{
	// THE SAME FAULT `Cataclysm.SkillBar.TheBarDoesNotRunIntoTheHealthAndManaBars`
	// rules out, on the narrowest screen that test checks: the vitals stack up
	// from the bottom left, the skill bar is centred, and the potions sit in the
	// bottom right.
	const float Width = UCataclysmSkillBar::NarrowestCheckedViewportPx;
	const float Height = 768.0f;
	const int32 Skills = UCataclysmSkillBar::SlotsShown().Num();

	const FVector2D LastSkill = UCataclysmSkillBar::BoxOriginFor(Skills - 1, Skills, Width, Height);
	const float SkillBarRight = LastSkill.X + UCataclysmSkillBar::BoxSizePx;
	const FVector2D FirstPotion = UCataclysmPotions::BoxOriginFor(0, Width, Height);
	const FVector2D LastPotion =
		UCataclysmPotions::BoxOriginFor(UCataclysmPotions::SlotCount - 1, Width, Height);
	const float VitalsRight = ACataclysmHUD::PlayerBarMarginPx + ACataclysmHUD::PlayerBarWidthPx;

	TestTrue(FString::Printf(TEXT("the first potion box starts at %.1f, right of the skill "
								  "bar's end at %.1f"),
							 FirstPotion.X, SkillBarRight),
			 FirstPotion.X > SkillBarRight);
	TestTrue(TEXT("and right of the player's bars"), FirstPotion.X > VitalsRight);
	TestTrue(FString::Printf(TEXT("the last potion box ends at %.1f, inside the %.0f pixel "
								  "screen"),
							 LastPotion.X + UCataclysmPotions::BoxSizePx, Width),
			 LastPotion.X + UCataclysmPotions::BoxSizePx <= Width);
	TestTrue(TEXT("and above its bottom edge"),
			 FirstPotion.Y + UCataclysmPotions::BoxSizePx <= Height);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
