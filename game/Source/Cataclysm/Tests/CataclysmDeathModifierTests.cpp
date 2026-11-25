// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmBonePyreCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Death's eleven enemy modifiers, written 2026-11-24 ahead of their window by the owner's approval ("yes, write the
 * Death enemy modifiers ahead"). One test each, each with a control where the modifier is absent. The rows are
 * `game/Data/EnemyModifiers.csv`; every figure is in `UCataclysmEnemyModifiers` and `docs/DECISIONS.md`.
 *
 * THE RIG IS THE WITHERING TESTS': a possessed player with 1000 health and nothing that could turn a blow aside, and a
 * creature five metres off. A blow is a direct one that can be neither evaded nor critical, so a figure is exact.
 * Undying Will and Haunting need the dungeon game mode, which alone can place a creature, and copy the Dead Rising
 * tests' floor.
 */
namespace CataclysmDeathModifiersTest
{
	using Vital = UCataclysmVitalAttributeSet;

	constexpr float DeathModifiersMostHealth = 1000.0f;

	/** A player the game finds as "the player": a player state, a controller possessing a pawn. */
	struct FDeathModifiersPlayer
	{
		explicit FDeathModifiersPlayer(UWorld* World, const FVector& Where = FVector::ZeroVector)
		{
			State = World->SpawnActor<ACataclysmPlayerState>();
			Controller = World->SpawnActor<APlayerController>(APlayerController::StaticClass());
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
			if (!State || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(State);
			Controller->Possess(Character);
			System = Cast<UCataclysmAbilitySystemComponent>(Character->GetAbilitySystemComponent());
			Expose();
		}

		/**
		 * 1000 health of 1000 and nothing that could turn a blow aside or shrink it: no energy shield, armour, evasion,
		 * block, damage reduction or resistance, and no regeneration. Again after anything that refreshes the player's
		 * stats, which the dungeon floor's set-up does.
		 */
		void Expose() const
		{
			if (!System)
			{
				return;
			}
			System->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), DeathModifiersMostHealth);
			System->SetNumericAttributeBase(Vital::GetHealthAttribute(), DeathModifiersMostHealth);
			System->SetNumericAttributeBase(Vital::GetMaxEnergyShieldAttribute(), 0.0f);
			System->SetNumericAttributeBase(Vital::GetEnergyShieldAttribute(), 0.0f);
			System->SetNumericAttributeBase(Vital::GetHealthRegenAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetDamageReductionAttribute(), 0.0f);
			const FGameplayAttribute Resistances[] = {
				UCataclysmResistanceAttributeSet::GetWarResistanceAttribute(),
				UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute(),
				UCataclysmResistanceAttributeSet::GetDeathResistanceAttribute(),
				UCataclysmResistanceAttributeSet::GetPestilenceResistanceAttribute(),
				UCataclysmResistanceAttributeSet::GetFamineResistanceAttribute(),
				UCataclysmResistanceAttributeSet::GetCelestialResistanceAttribute(),
				UCataclysmResistanceAttributeSet::GetChaosResistanceAttribute(),
				UCataclysmResistanceAttributeSet::GetVoidResistanceAttribute(),
			};
			for (const FGameplayAttribute& Resistance : Resistances)
			{
				if (System->HasAttributeSetForAttribute(Resistance))
				{
					System->SetNumericAttributeBase(Resistance, 0.0f);
				}
			}
		}

		bool IsUsable() const { return Character && System; }
		float Read(const FGameplayAttribute& Attribute) const { return System->GetNumericAttribute(Attribute); }
		float Health() const { return Read(Vital::GetHealthAttribute()); }
		void SetHealth(float NewHealth) const { System->SetNumericAttributeBase(Vital::GetHealthAttribute(), NewHealth); }

		ACataclysmPlayerState* State = nullptr;
		APlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** A plain creature of the monsters' side with 1000 health that cannot dodge, carrying `Row` when one is named. */
	ACataclysmEnemyCharacter* DeathCreatureAt(UWorld* World, const FVector& Where, const TCHAR* Row)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), Where, FRotator::ZeroRotator, Spawn);
		if (!Creature)
		{
			return nullptr;
		}
		Creature->SetHealth(DeathModifiersMostHealth);
		Creature->SetAttackDamage(20.0f);
		if (UAbilitySystemComponent* System = Creature->GetAbilitySystemComponent())
		{
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
		}
		if (Row)
		{
			Creature->ModifierRows.Add(FName(Row));
		}
		return Creature;
	}

	/** A blow that can be neither evaded nor critical, and what it took from `Struck`'s health. */
	float DeathBlowTook(AActor* From, AActor* Struck, float Damage, bool bTick = false)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Struck);
		if (!System)
		{
			return -1.0f;
		}
		const float Before = System->GetNumericAttribute(Vital::GetHealthAttribute());
		FCataclysmHitDelivery Delivery;
		Delivery.bCannotBeEvaded = true;
		Delivery.bCannotCriticallyStrike = true;
		Delivery.bIsDamageOverTime = bTick;
		UCataclysmSkillEffects::ApplyDirectDamage(From, Struck, Damage, Delivery);
		return Before - System->GetNumericAttribute(Vital::GetHealthAttribute());
	}

	/** Steps of the per-character step on `Character`'s modifiers that act on a timer, a quarter second each. */
	void DeathSteps(AActor* Character, int32 Steps)
	{
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			UCataclysmEnemyModifiers::TimedStep(Character, 0.25f);
		}
	}

	FGameplayTag DeathStatus(const TCHAR* Name) { return UCataclysmSkillShapes::StatusTagFor(Name); }

	/** The pyres in the world still to explode. */
	TArray<ACataclysmBonePyreCharacter*> DeathPyres(UWorld* World)
	{
		TArray<ACataclysmBonePyreCharacter*> Pyres;
		for (TActorIterator<ACataclysmBonePyreCharacter> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->bPyreSpent && !UCataclysmSkillEffects::IsDead(*It))
			{
				Pyres.Add(*It);
			}
		}
		return Pyres;
	}

	/** The zones in the world that cut healing. */
	TArray<ACataclysmGroundZone*> DeathDecayZones(UWorld* World)
	{
		TArray<ACataclysmGroundZone*> Zones;
		for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
		{
			if (IsValid(*It) && It->HealingLessPercentInside > 0.0f)
			{
				Zones.Add(*It);
			}
		}
		return Zones;
	}

	/** The Dead Rising tests' floor, with no floor rule: built, emptied, and every Imp a Common. */
	ACataclysmDungeonGameMode* DeathModifiersFloor(FAutomationTestBase& Test, UWorld* World,
												   const FDeathModifiersPlayer& Player)
	{
		ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
		if (!Test.TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
			|| !Test.TestTrue(TEXT("a possessed player with an ability system"), Player.IsUsable()))
		{
			return nullptr;
		}
		Mode->StartPlay();
		if (!Test.TestNotNull(TEXT("the world announces deaths"), UCataclysmCombatEvents::In(World)))
		{
			return nullptr;
		}
		Mode->DungeonModifiers.Reset();
		Mode->FloorNumber = 1;
		Mode->ImpRarityStep = 0;
		if (!Test.TestNotNull(TEXT("the floor was built"), Mode->BuildFloor()))
		{
			return nullptr;
		}
		Mode->ClearFloorEnemies();

		// AND THE PLAYER LAID BARE AGAIN, since the floor's set-up may have refreshed their stats.
		Player.Expose();
		return Mode;
	}

	/** An Imp at a rung with 100 health and no shield that cannot dodge, on the floor's list, carrying `Row`. */
	ACataclysmEnemyCharacter* DeathImpAtRung(UWorld* World, ACataclysmDungeonGameMode* Mode, const FVector& Where,
											 int32 Rung, const TCHAR* Row)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ACataclysmEnemyCharacter* Imp = World->SpawnActor<ACataclysmImpCharacter>(
			ACataclysmImpCharacter::StaticClass(), Where, FRotator::ZeroRotator, Spawn);
		if (!Imp)
		{
			return nullptr;
		}
		Imp->SetAttackDamage(100.0f);
		Imp->SetRarityStep(Rung);
		if (UAbilitySystemComponent* System = Imp->GetAbilitySystemComponent())
		{
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			System->SetNumericAttributeBase(Vital::GetHealthAttribute(), 100.0f);
			System->SetNumericAttributeBase(Vital::GetEnergyShieldAttribute(), 0.0f);
		}
		if (Row)
		{
			Imp->ModifierRows.Add(FName(Row));
		}
		Mode->FloorEnemies.Add(Imp);
		return Imp;
	}

	/** Kill a creature with the player's own blow, a real announced death. */
	bool DeathPlayerKills(FAutomationTestBase& Test, const FDeathModifiersPlayer& Player,
						  ACataclysmEnemyCharacter* Victim)
	{
		UCataclysmSkillEffects::ApplyHit(Player.Character, Victim, 100000.0f);
		return Test.TestTrue(TEXT("the player's blow killed it"), UCataclysmSkillEffects::IsDead(Victim));
	}

	/** The creatures on the floor's list one of the two rules brought back, living or not. */
	TArray<ACataclysmEnemyCharacter*> DeathBroughtBack(ACataclysmDungeonGameMode* Mode, bool bSpectralCopies)
	{
		TArray<ACataclysmEnemyCharacter*> Found;
		for (ACataclysmEnemyCharacter* Creature : Mode->FloorEnemies)
		{
			if (IsValid(Creature)
				&& (bSpectralCopies ? Creature->bIsASpectralCopy : Creature->bRisenByUndyingWill))
			{
				Found.Add(Creature);
			}
		}
		return Found;
	}
}

#define CATACLYSM_DEATH_MODIFIER_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// Change 1: the blow. Grave Touch, Soul Eater, Mortal Reminder.
// ---------------------------------------------------------------------------

CATACLYSM_DEATH_MODIFIER_TEST(FCataclysmDeathModifierGraveTouchTest,
	"Cataclysm.DeathModifiers.GraveTouchHitsHalfAgainHarderBelowHalfHealthAndNoTickIsChanged")
{
	using namespace CataclysmDeathModifiersTest;

	// THE LINE, WITHOUT A WORLD: "less than 50%", strictly.
	const TArray<FName> Grave = {FName(UCataclysmEnemyModifiers::GraveTouchRow)};
	TestEqual(TEXT("at exactly half, no more"), UCataclysmEnemyModifiers::GraveTouchMultiplier(Grave, 500.0f, 1000.0f),
			  1.0f);
	TestEqual(TEXT("just under half, half again"),
			  UCataclysmEnemyModifiers::GraveTouchMultiplier(Grave, 499.0f, 1000.0f), 1.5f);
	TestEqual(TEXT("and without the row, nothing"),
			  UCataclysmEnemyModifiers::GraveTouchMultiplier(TArray<FName>(), 100.0f, 1000.0f), 1.0f);

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FDeathModifiersPlayer Player(World);
	ACataclysmEnemyCharacter* Graver =
		DeathCreatureAt(World, FVector(500.0f, 0.0f, 0.0f), UCataclysmEnemyModifiers::GraveTouchRow);
	ACataclysmEnemyCharacter* Plain = DeathCreatureAt(World, FVector(-500.0f, 0.0f, 0.0f), nullptr);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a Grave Touch creature"), Graver)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Plain))
	{
		return false;
	}

	// EVERY FIGURE A RATIO OF THE SAME CREATURE'S BLOW AT 600 HEALTH AND AT 400.
	Player.SetHealth(600.0f);
	const float GraveAbove = DeathBlowTook(Graver, Player.Character, 100.0f);
	Player.SetHealth(400.0f);
	const float GraveBelow = DeathBlowTook(Graver, Player.Character, 100.0f);
	Player.SetHealth(600.0f);
	const float PlainAbove = DeathBlowTook(Plain, Player.Character, 100.0f);
	Player.SetHealth(400.0f);
	const float PlainBelow = DeathBlowTook(Plain, Player.Character, 100.0f);
	if (!TestTrue(*FString::Printf(TEXT("set-up: each blow took health (%.2f, %.2f)"), GraveAbove, PlainAbove),
				  GraveAbove > 1.0f && PlainAbove > 1.0f))
	{
		return false;
	}
	TestEqual(TEXT("below half, the Grave Touch blow takes half again"), GraveBelow / GraveAbove, 1.5f, 0.01f);
	TestEqual(TEXT("the plain creature's blow is the same either side of the line"), PlainBelow / PlainAbove, 1.0f, 0.01f);
	TestEqual(TEXT("above the line, the two creatures' blows are equal"), GraveAbove / PlainAbove, 1.0f, 0.01f);

	// A TICK IS NOT A HIT, as Executioner's "hit damage" is not.
	Player.SetHealth(400.0f);
	const float GraveTick = DeathBlowTook(Graver, Player.Character, 100.0f, /*bTick=*/true);
	Player.SetHealth(400.0f);
	const float PlainTick = DeathBlowTook(Plain, Player.Character, 100.0f, /*bTick=*/true);
	TestTrue(*FString::Printf(TEXT("set-up: a tick took health (%.2f)"), PlainTick), PlainTick > 1.0f);
	TestEqual(TEXT("below half, a Grave Touch tick is no larger than a plain one"), GraveTick, PlainTick, 0.01f);
	return true;
}

CATACLYSM_DEATH_MODIFIER_TEST(FCataclysmDeathModifierSoulEaterTest,
	"Cataclysm.DeathModifiers.SoulEaterPassesTheEnergyShieldByToHealth")
{
	using namespace CataclysmDeathModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FDeathModifiersPlayer Player(World);
	ACataclysmEnemyCharacter* Eater =
		DeathCreatureAt(World, FVector(500.0f, 0.0f, 0.0f), UCataclysmEnemyModifiers::SoulEaterRow);
	ACataclysmEnemyCharacter* Plain = DeathCreatureAt(World, FVector(-500.0f, 0.0f, 0.0f), nullptr);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a Soul Eater"), Eater)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Plain))
	{
		return false;
	}

	const auto Shielded = [&Player]()
	{
		Player.System->SetNumericAttributeBase(Vital::GetMaxEnergyShieldAttribute(), 500.0f);
		Player.System->SetNumericAttributeBase(Vital::GetEnergyShieldAttribute(), 500.0f);
		Player.SetHealth(DeathModifiersMostHealth);
	};

	// THE CONTROL FIRST: the shield takes a plain creature's blow, and health is untouched.
	Shielded();
	const float PlainToHealth = DeathBlowTook(Plain, Player.Character, 100.0f);
	const float PlainToShield = 500.0f - Player.Read(Vital::GetEnergyShieldAttribute());
	TestTrue(*FString::Printf(TEXT("the plain blow took %.2f of the shield"), PlainToShield), PlainToShield > 1.0f);
	TestEqual(TEXT("and none of the health"), PlainToHealth, 0.0f, 0.01f);

	Shielded();
	const float EaterToHealth = DeathBlowTook(Eater, Player.Character, 100.0f);
	TestEqual(TEXT("the Soul Eater's blow leaves the whole shield"), Player.Read(Vital::GetEnergyShieldAttribute()),
			  500.0f, 0.01f);
	TestEqual(TEXT("and takes from health what the plain blow took from the shield"), EaterToHealth, PlainToShield,
			  0.01f);
	return true;
}

CATACLYSM_DEATH_MODIFIER_TEST(FCataclysmDeathModifierMortalReminderTest,
	"Cataclysm.DeathModifiers.MortalReminderHoldsHealingToTheHealthAHitLeftForFourSeconds")
{
	using namespace CataclysmDeathModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FDeathModifiersPlayer Player(World);
	ACataclysmEnemyCharacter* Reminder =
		DeathCreatureAt(World, FVector(500.0f, 0.0f, 0.0f), UCataclysmEnemyModifiers::MortalReminderRow);
	ACataclysmEnemyCharacter* Plain = DeathCreatureAt(World, FVector(-500.0f, 0.0f, 0.0f), nullptr);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a Mortal Reminder"), Reminder)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Plain))
	{
		return false;
	}
	const auto Heal = [&Player](float Gain)
	{
		UCataclysmRegeneration::TopUp(*Player.System, Vital::GetHealthAttribute(), Vital::GetMaxHealthAttribute(), Gain);
	};

	// THE CONTROL: a plain creature's hit sets no ceiling, and a heal reaches the maximum.
	DeathBlowTook(Plain, Player.Character, 50.0f);
	TestTrue(TEXT("a plain hit sets no ceiling"), UCataclysmEnemyModifiers::RecoveryCeilingOn(*Player.System) < 0.0f);
	Player.SetHealth(600.0f);
	Heal(500.0f);
	TestEqual(TEXT("so a heal of 500 from 600 reaches the maximum"), Player.Health(), DeathModifiersMostHealth, 0.01f);

	// THE HIT, AND THE HEALTH IT LEFT IS THE CEILING.
	const float Took = DeathBlowTook(Reminder, Player.Character, 50.0f);
	const float Left = Player.Health();
	if (!TestTrue(*FString::Printf(TEXT("set-up: the hit took %.2f"), Took), Took > 1.0f))
	{
		return false;
	}
	TestEqual(TEXT("the ceiling is the health the hit left"), UCataclysmEnemyModifiers::RecoveryCeilingOn(*Player.System),
			  Left, 0.01f);
	TestTrue(TEXT("and the status line says so"),
			 UCataclysmCombatOverlay::DeathModifierTextFor(Player.Character).Contains(TEXT("Recovery capped")));

	// LOWER IT BY 300, AS ANOTHER BLOW WOULD, AND HEAL 500: the heal stops at the ceiling.
	Player.SetHealth(Left - 300.0f);
	Heal(500.0f);
	TestEqual(TEXT("a heal of 500 stops at the ceiling"), Player.Health(), Left, 0.01f);

	// A SECOND HIT LOWERS IT.
	const float SecondTook = DeathBlowTook(Reminder, Player.Character, 50.0f);
	TestEqual(TEXT("a second hit sets the ceiling again, lower"),
			  UCataclysmEnemyModifiers::RecoveryCeilingOn(*Player.System), Left - SecondTook, 0.01f);

	// FOUR SECONDS AFTER THE LAST HIT IT IS GONE.
	World->TimeSeconds += 3.9;
	TestTrue(TEXT("3.9 seconds on, it still holds"), UCataclysmEnemyModifiers::RecoveryCeilingOn(*Player.System) >= 0.0f);
	World->TimeSeconds += 0.2;
	TestTrue(TEXT("4.1 seconds on, it has gone"), UCataclysmEnemyModifiers::RecoveryCeilingOn(*Player.System) < 0.0f);
	Heal(500.0f);
	TestEqual(TEXT("and a heal reaches the maximum again"), Player.Health(), DeathModifiersMostHealth, 0.01f);
	return true;
}

// ---------------------------------------------------------------------------
// Change 2: the creature's own clocks and what it loses. Spectral Shift, Shadow Step, Soul Tether.
// ---------------------------------------------------------------------------

CATACLYSM_DEATH_MODIFIER_TEST(FCataclysmDeathModifierSpectralShiftTest,
	"Cataclysm.DeathModifiers.SpectralShiftTakesItOutOfExistenceForFiveSecondsInEveryFifteen")
{
	using namespace CataclysmDeathModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FDeathModifiersPlayer Player(World);
	ACataclysmEnemyCharacter* Shifter =
		DeathCreatureAt(World, FVector(500.0f, 0.0f, 0.0f), UCataclysmEnemyModifiers::SpectralShiftRow);
	ACataclysmEnemyCharacter* Plain = DeathCreatureAt(World, FVector(-500.0f, 0.0f, 0.0f), nullptr);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a Spectral Shift"), Shifter)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Plain))
	{
		return false;
	}
	// THE PLAIN CREATURE CARRIES ANOTHER MODIFIER, SO ITS STEP RUNS AT ALL: an empty list returns at once.
	Plain->ModifierRows.Add(FName(UCataclysmEnemyModifiers::GraveTouchRow));

	DeathSteps(Shifter, 59);
	DeathSteps(Plain, 60);
	TestFalse(TEXT("14.75 seconds in, it is still here"), UCataclysmEnemyModifiers::IsPhasedOut(Shifter));
	TestTrue(TEXT("and a blow lands"), DeathBlowTook(Player.Character, Shifter, 100.0f) > 1.0f);
	TestFalse(TEXT("the plain creature never shifts"), UCataclysmEnemyModifiers::IsPhasedOut(Plain));

	DeathSteps(Shifter, 1);
	TestTrue(TEXT("15 seconds in, it is out of existence"), UCataclysmEnemyModifiers::IsPhasedOut(Shifter));
	TestEqual(TEXT("and a blow takes nothing"), DeathBlowTook(Player.Character, Shifter, 100.0f), 0.0f, 0.01f);
	TestTrue(TEXT("and the status line says it is phased"),
			 UCataclysmCombatOverlay::DeathModifierTextFor(Shifter).Contains(TEXT("Phased")));

	DeathSteps(Shifter, 19);
	TestTrue(TEXT("19.75 seconds in, it is still out"), UCataclysmEnemyModifiers::IsPhasedOut(Shifter));
	DeathSteps(Shifter, 1);
	TestFalse(TEXT("20 seconds in, it is back"), UCataclysmEnemyModifiers::IsPhasedOut(Shifter));
	TestTrue(TEXT("and a blow lands again"), DeathBlowTook(Player.Character, Shifter, 100.0f) > 1.0f);
	return true;
}

CATACLYSM_DEATH_MODIFIER_TEST(FCataclysmDeathModifierShadowStepTest,
	"Cataclysm.DeathModifiers.ShadowStepHidesTheCreatureUntilItAttacksAndForASecondAfter")
{
	using namespace CataclysmDeathModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FDeathModifiersPlayer Player(World);
	ACataclysmEnemyCharacter* Stepper =
		DeathCreatureAt(World, FVector(500.0f, 0.0f, 0.0f), UCataclysmEnemyModifiers::ShadowStepRow);
	ACataclysmEnemyCharacter* Plain = DeathCreatureAt(World, FVector(-500.0f, 0.0f, 0.0f), nullptr);
	ACataclysmEnemyCharacter* Medic =
		DeathCreatureAt(World, FVector(0.0f, 500.0f, 0.0f), UCataclysmEnemyModifiers::ShadowStepRow);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a Shadow Step"), Stepper)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Plain) || !TestNotNull(TEXT("set-up: a medic"), Medic))
	{
		return false;
	}
	Plain->ModifierRows.Add(FName(UCataclysmEnemyModifiers::GraveTouchRow));
	Medic->bHealsAlliesForTheFloorRule = true;

	TestTrue(TEXT("set-up: before any step, it can be clicked"),
			 ACataclysmPlayerController::IsClickableEnemy(Stepper, Player.Character));

	DeathSteps(Stepper, 1);
	DeathSteps(Plain, 4);
	DeathSteps(Medic, 4);
	TestTrue(TEXT("not attacking, it is hidden"), Stepper->IsHidden());
	TestTrue(TEXT("by Shadow Step"), UCataclysmEnemyModifiers::IsHiddenByShadowStep(Stepper));
	TestFalse(TEXT("and cannot be clicked"), ACataclysmPlayerController::IsClickableEnemy(Stepper, Player.Character));
	TestFalse(TEXT("the plain creature is never hidden"), Plain->IsHidden());
	TestFalse(TEXT("nor is a medic carrying it, which never attacks"), Medic->IsHidden());

	// AN ATTACK BEGINS: what the brain calls as it orders a swing or starts a wind-up.
	UCataclysmEnemyModifiers::RevealForShadowStep(Stepper);
	TestFalse(TEXT("as its attack begins it is seen"), Stepper->IsHidden());
	TestTrue(TEXT("and can be clicked"), ACataclysmPlayerController::IsClickableEnemy(Stepper, Player.Character));
	DeathSteps(Stepper, 3);
	TestFalse(TEXT("three quarters of a second after, still seen"), Stepper->IsHidden());
	DeathSteps(Stepper, 1);
	TestTrue(TEXT("a second after, hidden again"), Stepper->IsHidden());

	// AND A HIDDEN CREATURE IS SEEN TO FALL.
	Stepper->HandleDeath();
	TestFalse(TEXT("dead, it is drawn again"), Stepper->IsHidden());
	return true;
}

CATACLYSM_DEATH_MODIFIER_TEST(FCataclysmDeathModifierSoulTetherTest,
	"Cataclysm.DeathModifiers.SoulTetherSendsATenthOfTheHealthItLostBackOverFourSeconds")
{
	using namespace CataclysmDeathModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FDeathModifiersPlayer Player(World);
	ACataclysmEnemyCharacter* Tethered =
		DeathCreatureAt(World, FVector(500.0f, 0.0f, 0.0f), UCataclysmEnemyModifiers::SoulTetherRow);
	ACataclysmEnemyCharacter* Frail =
		DeathCreatureAt(World, FVector(0.0f, 500.0f, 0.0f), UCataclysmEnemyModifiers::SoulTetherRow);
	ACataclysmEnemyCharacter* Plain = DeathCreatureAt(World, FVector(-500.0f, 0.0f, 0.0f), nullptr);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a Soul Tether"), Tethered)
		|| !TestNotNull(TEXT("set-up: a frail one"), Frail) || !TestNotNull(TEXT("set-up: a plain creature"), Plain))
	{
		return false;
	}
	const FGameplayTag Tether = DeathStatus(TEXT("Soul Tether"));
	const auto StillToCome = [&Player, &Tether]()
	{
		UCataclysmSkillEffects::FRunningAilment Running;
		return UCataclysmSkillEffects::RunningAilmentOn(Player.Character, Tether, Running)
			? Running.DamagePerSecond * Running.SecondsLeft
			: 0.0f;
	};

	// THE CONTROL: hitting a plain creature tethers nothing.
	DeathBlowTook(Player.Character, Plain, 200.0f);
	TestFalse(TEXT("a plain creature's loss tethers nothing"), UCataclysmSkillEffects::HasTag(Player.Character, Tether));

	const float FirstLost = DeathBlowTook(Player.Character, Tethered, 200.0f);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the blow took %.2f"), FirstLost), FirstLost > 1.0f))
	{
		return false;
	}
	TestTrue(TEXT("the player carries Soul Tether"), UCataclysmSkillEffects::HasTag(Player.Character, Tether));
	TestEqual(TEXT("and a tenth of what the creature lost is to come"), StillToCome(), FirstLost * 0.1f, 0.05f);

	const float SecondLost = DeathBlowTook(Player.Character, Tethered, 300.0f);
	TestEqual(TEXT("a second blow's tenth is added, not lost"), StillToCome(), (FirstLost + SecondLost) * 0.1f, 0.05f);

	// A BLOW FAR PAST WHAT THE FRAIL ONE HOLDS SENDS BACK A TENTH OF WHAT IT HELD, NOT OF THE BLOW.
	if (UAbilitySystemComponent* FrailSystem = Frail->GetAbilitySystemComponent())
	{
		FrailSystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 100.0f);
	}
	const float Before = StillToCome();
	DeathBlowTook(Player.Character, Frail, 100000.0f);
	TestTrue(TEXT("set-up: the huge blow killed it"), UCataclysmSkillEffects::IsDead(Frail));
	TestEqual(TEXT("and sent back a tenth of its 100 health"), StillToCome() - Before, 10.0f, 0.05f);

	// AND IT ARRIVES: over four seconds the player loses all of it.
	const float Owed = StillToCome();
	const float HealthBefore = Player.Health();
	CataclysmTestWorld::RunClock(World, 4.5f);
	TestEqual(TEXT("four seconds later the player has lost what was owed"), HealthBefore - Player.Health(), Owed,
			  Owed * 0.02f);
	return true;
}

// ---------------------------------------------------------------------------
// Change 3: a death. Final Curse, Undying Will, Haunting.
// ---------------------------------------------------------------------------

CATACLYSM_DEATH_MODIFIER_TEST(FCataclysmDeathModifierFinalCurseTest,
	"Cataclysm.DeathModifiers.FinalCurseMakesEachSkillCostFivePercentForFiveSecondsAfterTheDeath")
{
	using namespace CataclysmDeathModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FDeathModifiersPlayer Player(World);
	ACataclysmEnemyCharacter* Curser =
		DeathCreatureAt(World, FVector(500.0f, 0.0f, 0.0f), UCataclysmEnemyModifiers::FinalCurseRow);
	ACataclysmEnemyCharacter* Plain = DeathCreatureAt(World, FVector(-500.0f, 0.0f, 0.0f), nullptr);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a Final Curse"), Curser)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Plain))
	{
		return false;
	}
	const FGameplayTag Curse = DeathStatus(TEXT("Final Curse"));
	const FGameplayTagContainer NoTags;
	// NOUGHT DEATH RESISTANCE, so the figure is the curse's own 5%.
	Player.System->SetNumericAttributeBase(UCataclysmResistanceAttributeSet::GetDeathResistanceAttribute(), 0.0f);
	const auto SkillCost = [&Player, &NoTags](ECataclysmAbilitySlot Slot)
	{
		const float Before = Player.Health();
		UCataclysmCombatEvents::NoteSkillUsed(Player.Character, TEXT("A test skill"), NoTags, Slot);
		return Before - Player.Health();
	};

	// THE CONTROL: a plain creature's death curses nobody, and a skill costs nothing.
	Plain->HandleDeath();
	TestFalse(TEXT("a plain death curses nobody"), UCataclysmSkillEffects::HasTag(Player.Character, Curse));
	TestEqual(TEXT("so a skill costs nothing"), SkillCost(ECataclysmAbilitySlot::Special), 0.0f, 0.01f);

	Curser->HandleDeath();
	TestTrue(TEXT("its death curses the player"), UCataclysmSkillEffects::HasTag(Player.Character, Curse));
	TestTrue(TEXT("and the status line says so"),
			 UCataclysmCombatOverlay::DeathModifierTextFor(Player.Character).Contains(TEXT("Final Curse")));
	TestEqual(TEXT("a skill costs 5% of 1000 health"), SkillCost(ECataclysmAbilitySlot::Special), 50.0f, 0.5f);
	TestEqual(TEXT("so does a movement skill"), SkillCost(ECataclysmAbilitySlot::Movement), 50.0f, 0.5f);
	TestEqual(TEXT("the basic attack costs nothing"), SkillCost(ECataclysmAbilitySlot::BasicAttack), 0.0f, 0.01f);

	CataclysmTestWorld::RunClock(World, 5.5f);
	TestFalse(TEXT("5.5 seconds after, the curse has gone"), UCataclysmSkillEffects::HasTag(Player.Character, Curse));
	TestEqual(TEXT("and a skill costs nothing again"), SkillCost(ECataclysmAbilitySlot::Special), 0.0f, 0.01f);
	return true;
}

CATACLYSM_DEATH_MODIFIER_TEST(FCataclysmDeathModifierUndyingWillTest,
	"Cataclysm.DeathModifiers.UndyingWillRaisesItOnceAtHalfHealthWithFiftyPercentIncreasedDamage")
{
	using namespace CataclysmDeathModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FDeathModifiersPlayer Player(World);
	ACataclysmDungeonGameMode* Mode = DeathModifiersFloor(*this, World, Player);
	if (!Mode)
	{
		return false;
	}

	// THE CONTROL: an Elite without the row stays dead.
	ACataclysmEnemyCharacter* Plain = DeathImpAtRung(World, Mode, FVector(-400.0f, 0.0f, 0.0f), 1, nullptr);
	if (!TestNotNull(TEXT("set-up: a plain Elite"), Plain) || !DeathPlayerKills(*this, Player, Plain))
	{
		return false;
	}
	TestEqual(TEXT("nothing rose from the plain Elite"), DeathBroughtBack(Mode, false).Num(), 0);

	ACataclysmEnemyCharacter* Undying =
		DeathImpAtRung(World, Mode, FVector(400.0f, 0.0f, 0.0f), 1, UCataclysmEnemyModifiers::UndyingWillRow);
	if (!TestNotNull(TEXT("set-up: an Elite carrying Undying Will"), Undying)
		|| !DeathPlayerKills(*this, Player, Undying))
	{
		return false;
	}
	const TArray<ACataclysmEnemyCharacter*> Risen = DeathBroughtBack(Mode, false);
	if (!TestEqual(TEXT("one creature rose in the same death notice"), Risen.Num(), 1))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Again = Risen[0];
	TestFalse(TEXT("it is alive"), UCataclysmSkillEffects::IsDead(Again));
	TestEqual(TEXT("at the Elite's rung"), Again->RarityStep, 1);
	TestFalse(TEXT("and its death pays nothing"), Again->PaysForItsDeath());

	const UAbilitySystemComponent* System = Again->GetAbilitySystemComponent();
	if (TestNotNull(TEXT("it has an ability system"), System))
	{
		const float Maximum = System->GetNumericAttribute(Vital::GetMaxHealthAttribute());
		TestTrue(FString::Printf(TEXT("it has a maximum: %.1f"), Maximum), Maximum > 0.0f);
		TestEqual(TEXT("and stands at half of it"), System->GetNumericAttribute(Vital::GetHealthAttribute()),
				  Maximum * 0.5f, 0.5f);
		const float Own = Again->OwnAttackDamage() * Again->DamageMultiplierProduct();
		TestTrue(FString::Printf(TEXT("set-up: its own damage is %.2f"), Own), Own > 0.0f);
		TestEqual(TEXT("its damage is half again its own"),
				  System->GetNumericAttribute(UCataclysmCombatAttributeSet::GetAttackDamageAttribute()) / Own, 1.5f,
				  0.01f);
	}
	TestEqual(TEXT("through Undying Will's part, 50 increased"),
			  Again->RuleDamagePartFrom(ACataclysmEnemyCharacter::UndyingWillDamageSource).IncreasedPercent, 50.0f,
			  0.01f);
	TestTrue(TEXT("and the status line says Undying"),
			 UCataclysmCombatOverlay::DeathModifierTextFor(Again).Contains(TEXT("Undying")));

	// ONCE: killed again, it stays dead, whatever it drew. Its drawn rows are cleared so no other row decides the kill.
	Again->ModifierRows.Reset();
	Again->ModifierRows.Add(FName(UCataclysmEnemyModifiers::UndyingWillRow));
	if (UAbilitySystemComponent* Writable = Again->GetAbilitySystemComponent())
	{
		Writable->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
	}
	if (!DeathPlayerKills(*this, Player, Again))
	{
		return false;
	}
	TestEqual(TEXT("killed again, nothing more rose"), DeathBroughtBack(Mode, false).Num(), 1);
	return true;
}

CATACLYSM_DEATH_MODIFIER_TEST(FCataclysmDeathModifierHauntingTest,
	"Cataclysm.DeathModifiers.HauntingLeavesACommonCopyAtHalfHealthWhoseHitsCutEveryResistance")
{
	using namespace CataclysmDeathModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FDeathModifiersPlayer Player(World);
	ACataclysmDungeonGameMode* Mode = DeathModifiersFloor(*this, World, Player);
	if (!Mode)
	{
		return false;
	}
	const FGameplayAttribute Death = UCataclysmResistanceAttributeSet::GetDeathResistanceAttribute();
	const FGameplayAttribute War = UCataclysmResistanceAttributeSet::GetWarResistanceAttribute();
	Player.System->SetNumericAttributeBase(Death, 40.0f);
	Player.System->SetNumericAttributeBase(War, 10.0f);

	// THE CONTROL: a plain creature's death leaves nothing, and its hit cuts nothing.
	ACataclysmEnemyCharacter* Plain = DeathImpAtRung(World, Mode, FVector(-400.0f, 0.0f, 0.0f), 1, nullptr);
	if (!TestNotNull(TEXT("set-up: a plain Elite"), Plain))
	{
		return false;
	}
	TestTrue(TEXT("set-up: a plain hit takes health"), DeathBlowTook(Plain, Player.Character, 20.0f) > 0.0f);
	TestEqual(TEXT("and leaves Death resistance at 40"), Player.Read(Death), 40.0f, 0.01f);
	if (!DeathPlayerKills(*this, Player, Plain))
	{
		return false;
	}
	TestEqual(TEXT("and its death leaves no copy"), DeathBroughtBack(Mode, true).Num(), 0);

	ACataclysmEnemyCharacter* Haunter =
		DeathImpAtRung(World, Mode, FVector(400.0f, 0.0f, 0.0f), 1, UCataclysmEnemyModifiers::HauntingRow);
	if (!TestNotNull(TEXT("set-up: an Elite carrying Haunting"), Haunter) || !DeathPlayerKills(*this, Player, Haunter))
	{
		return false;
	}
	const TArray<ACataclysmEnemyCharacter*> Copies = DeathBroughtBack(Mode, true);
	if (!TestEqual(TEXT("one spectral copy was left"), Copies.Num(), 1))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Copy = Copies[0];
	TestEqual(TEXT("a Common"), Copy->RarityStep, 0);
	TestFalse(TEXT("whose death pays nothing"), Copy->PaysForItsDeath());
	TestTrue(TEXT("and the status line says Spectral"),
			 UCataclysmCombatOverlay::DeathModifierTextFor(Copy).Contains(TEXT("Spectral")));
	if (UAbilitySystemComponent* CopySystem = Copy->GetAbilitySystemComponent())
	{
		const float Maximum = CopySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute());
		TestEqual(TEXT("standing at half its health"), CopySystem->GetNumericAttribute(Vital::GetHealthAttribute()),
				  Maximum * 0.5f, 0.5f);
		CopySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
	}

	// ITS HIT CUTS EACH RESISTANCE BY 25, NEVER BELOW NOUGHT, AND A SECOND HIT REPLACES THE FIRST.
	TestTrue(TEXT("set-up: the copy's hit takes health"), DeathBlowTook(Copy, Player.Character, 20.0f) > 0.0f);
	TestEqual(TEXT("Death resistance 40 is cut to 15"), Player.Read(Death), 15.0f, 0.01f);
	TestEqual(TEXT("War resistance 10 is cut to nought, not below"), Player.Read(War), 0.0f, 0.01f);
	TestTrue(TEXT("the player is haunted"), UCataclysmSkillEffects::HasTag(Player.Character, DeathStatus(TEXT("Haunting"))));
	TestTrue(TEXT("and the status line says so"),
			 UCataclysmCombatOverlay::DeathModifierTextFor(Player.Character).Contains(TEXT("Haunted")));
	DeathBlowTook(Copy, Player.Character, 20.0f);
	TestEqual(TEXT("a second hit leaves Death resistance at 15, not 0"), Player.Read(Death), 15.0f, 0.01f);

	// A COPY LEAVES NO COPY.
	Copy->ModifierRows.Reset();
	if (!DeathPlayerKills(*this, Player, Copy))
	{
		return false;
	}
	TestEqual(TEXT("killed, the copy leaves no copy of its own"), DeathBroughtBack(Mode, true).Num(), 1);
	return true;
}

// ---------------------------------------------------------------------------
// Change 4: what is left on the floor. Lingering Decay, Funeral Pyre.
// ---------------------------------------------------------------------------

CATACLYSM_DEATH_MODIFIER_TEST(FCataclysmDeathModifierLingeringDecayTest,
	"Cataclysm.DeathModifiers.LingeringDecayLeavesAZoneEveryFiveSecondsThatHalvesHealingInside")
{
	using namespace CataclysmDeathModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE PLAYER TWO METRES FROM THE CREATURE, inside a three metre zone at its feet, and far enough that the two
	// bodies do not overlap and the spawn moves neither.
	const FDeathModifiersPlayer Player(World, FVector(200.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Decayer =
		DeathCreatureAt(World, FVector::ZeroVector, UCataclysmEnemyModifiers::LingeringDecayRow);
	ACataclysmEnemyCharacter* Plain = DeathCreatureAt(World, FVector(0.0f, 2000.0f, 0.0f), nullptr);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a Lingering Decay"), Decayer)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Plain))
	{
		return false;
	}
	Plain->ModifierRows.Add(FName(UCataclysmEnemyModifiers::GraveTouchRow));
	const auto Restored = [&Player]()
	{
		Player.SetHealth(100.0f);
		UCataclysmRegeneration::TopUp(*Player.System, Vital::GetHealthAttribute(), Vital::GetMaxHealthAttribute(),
									  100.0f);
		return Player.Health() - 100.0f;
	};

	DeathSteps(Plain, 20);
	DeathSteps(Decayer, 19);
	TestEqual(TEXT("4.75 seconds in, no zone, and the plain creature never leaves one"), DeathDecayZones(World).Num(), 0);
	TestEqual(TEXT("so a heal of 100 restores 100"), Restored(), 100.0f, 0.01f);

	DeathSteps(Decayer, 1);
	const TArray<ACataclysmGroundZone*> Zones = DeathDecayZones(World);
	if (!TestEqual(TEXT("5 seconds in, one zone"), Zones.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("cutting healing by 50"), Zones[0]->HealingLessPercentInside, 50.0f, 0.01f);
	TestEqual(TEXT("three metres across its radius"), Zones[0]->RadiusCm, 300.0f, 0.01f);
	TestEqual(TEXT("the player inside is cut by 50"),
			  UCataclysmEnemyModifiers::LingeringDecayHealingLessPercentAt(Player.Character), 50.0f, 0.01f);
	TestEqual(TEXT("and a heal of 100 restores 50"), Restored(), 50.0f, 0.01f);
	TestTrue(TEXT("and the status line says Decaying"),
			 UCataclysmCombatOverlay::DeathModifierTextFor(Player.Character).Contains(TEXT("Decaying")));
	TestEqual(TEXT("the creature standing in it is not cut: it is not the players' side"),
			  UCataclysmEnemyModifiers::LingeringDecayHealingLessPercentAt(Decayer), 0.0f, 0.01f);

	Player.Character->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	TestEqual(TEXT("ten metres away, nothing is cut"),
			  UCataclysmEnemyModifiers::LingeringDecayHealingLessPercentAt(Player.Character), 0.0f, 0.01f);
	TestEqual(TEXT("and a heal of 100 restores 100"), Restored(), 100.0f, 0.01f);
	return true;
}

CATACLYSM_DEATH_MODIFIER_TEST(FCataclysmDeathModifierFuneralPyreTest,
	"Cataclysm.DeathModifiers.FuneralPyreRaisesAPyreAtTheStrikerThatExplodesInThreeSecondsUnlessBroken")
{
	using namespace CataclysmDeathModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FDeathModifiersPlayer Player(World);
	ACataclysmEnemyCharacter* Kindler =
		DeathCreatureAt(World, FVector(500.0f, 0.0f, 0.0f), UCataclysmEnemyModifiers::FuneralPyreRow);
	ACataclysmEnemyCharacter* Plain = DeathCreatureAt(World, FVector(-500.0f, 0.0f, 0.0f), nullptr);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a Funeral Pyre"), Kindler)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Plain))
	{
		return false;
	}
	const UAbilitySystemComponent* KindlerSystem = Kindler->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("set-up: it has an ability system"), KindlerSystem))
	{
		return false;
	}

	// THE CONTROL: striking a plain creature raises nothing.
	DeathBlowTook(Player.Character, Plain, 100.0f);
	TestEqual(TEXT("a plain creature struck raises no pyre"), DeathPyres(World).Num(), 0);

	TestTrue(TEXT("set-up: the blow took health"), DeathBlowTook(Player.Character, Kindler, 100.0f) > 0.0f);
	TArray<ACataclysmBonePyreCharacter*> Pyres = DeathPyres(World);
	if (!TestEqual(TEXT("struck, it raises one pyre"), Pyres.Num(), 1))
	{
		return false;
	}
	ACataclysmBonePyreCharacter* Pyre = Pyres[0];
	TestTrue(*FString::Printf(TEXT("at the striker's feet: %.0f cm away"),
							  FVector::Dist2D(Pyre->GetActorLocation(), Player.Character->GetActorLocation())),
			 FVector::Dist2D(Pyre->GetActorLocation(), Player.Character->GetActorLocation()) < 300.0f);
	if (const UAbilitySystemComponent* PyreSystem = Pyre->GetAbilitySystemComponent())
	{
		TestEqual(TEXT("holding a tenth of the creature's maximum health"),
				  PyreSystem->GetNumericAttribute(Vital::GetMaxHealthAttribute()),
				  KindlerSystem->GetNumericAttribute(Vital::GetMaxHealthAttribute()) * 0.1f, 0.5f);
	}
	TestTrue(TEXT("and the status line names it"),
			 UCataclysmCombatOverlay::DeathModifierTextFor(Pyre).Contains(TEXT("Bone Pyre")));
	DeathBlowTook(Player.Character, Kindler, 100.0f);
	TestEqual(TEXT("struck again at once, no second pyre"), DeathPyres(World).Num(), 1);

	// THREE SECONDS, THEN THREE OF THE CREATURE'S HITS ON THE PLAYER BESIDE IT.
	const float Expected =
		KindlerSystem->GetNumericAttribute(UCataclysmCombatAttributeSet::GetAttackDamageAttribute()) * 3.0f;
	const float Before = Player.Health();
	DeathSteps(Pyre, 11);
	TestFalse(TEXT("2.75 seconds in, it has not gone off"), Pyre->bPyreSpent);
	TestEqual(TEXT("and the player has lost nothing"), Player.Health(), Before, 0.01f);
	DeathSteps(Pyre, 1);
	TestTrue(TEXT("3 seconds in, it has gone off"), Pyre->bPyreSpent);
	TestTrue(*FString::Printf(TEXT("set-up: the blast is worth %.2f"), Expected), Expected > 1.0f);
	TestEqual(TEXT("and the player lost three of the creature's hits"), Before - Player.Health(), Expected,
			  Expected * 0.01f);

	// BROKEN FIRST, A PYRE NEVER EXPLODES. Past the eight second cooldown for the next.
	World->TimeSeconds += 9.0;
	DeathBlowTook(Player.Character, Kindler, 100.0f);
	Pyres = DeathPyres(World);
	if (!TestEqual(TEXT("struck after the cooldown, a new pyre"), Pyres.Num(), 1))
	{
		return false;
	}
	ACataclysmBonePyreCharacter* Broken = Pyres[0];
	DeathBlowTook(Player.Character, Broken, 100000.0f);
	TestTrue(TEXT("the player broke it"), UCataclysmSkillEffects::IsDead(Broken));
	const float BeforeBroken = Player.Health();
	DeathSteps(Broken, 12);
	TestFalse(TEXT("a broken pyre does not go off"), Broken->bPyreSpent);
	TestEqual(TEXT("and the player loses nothing"), Player.Health(), BeforeBroken, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
