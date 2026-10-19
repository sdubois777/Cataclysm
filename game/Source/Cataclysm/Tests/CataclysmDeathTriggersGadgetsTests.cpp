// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * "When you die, all your active traps and gadgets immediately trigger simultaneously": the gadget half. Ruled
 * 2026-10-19 by the coordinating session (C1-C5): the action `gadgets_cast_now` on `player_death`; each of the
 * wearer's skill gadgets casts its skill once, at once, at the target its own clock would choose, at the whole of its
 * damage, ignoring its clock, and with no target within its reach casts nothing (C2); every gadget casts inside the one
 * event (C3); the clock is unchanged (C4). Beside `Cataclysm.SkillGadgets.`, whose rig this follows.
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER HOLDING A DEMONIC WHIP, given a weapon damage of 100 by hand, its Heavy
 * and Ultimate slots converted by the flag rows `skill_slot_is_a_gadget` (flat 1, requiring `Slot.Heavy` and
 * `Slot.Ultimate`): Heavy Searing Lash, a 5 m cone; Ultimate The Gathering, a 14 m point-blank strike. NO ROW IS
 * AUTHORED, so the death row is an action built by hand as the item would build it.
 *
 * THE DEATH IS A REAL ONE: a creature's direct blow larger than the player's health, so `HandleDeath` runs and raises
 * `player_death` as it does in play. The gadgets are placed by `ACataclysmMinion::PlaceSkillGadget` and NO KEY IS
 * PRESSED, so no test needs a resource a skill costs; a gadget's cast is free.
 *
 * Every body stands at least 1 m from every other, on a real side: the players on the players', the gadgets on their
 * player's, the creatures on the monsters'. Each gadget's enemy stands 3 m from it, and every other body at least 14 m
 * further than The Gathering reaches. NOTHING HERE WAITS ON A TICK: the world's clock is moved by hand and a gadget's
 * step is called directly. The critical strike roll is pinned at 100, which never critically strikes, after the world
 * is made. Helper types and functions are named after the subject.
 */
namespace CataclysmDeathTriggersGadgetsTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The health every creature here starts with. */
	constexpr float DeathTriggersGadgetsEnemyHealth = 50000.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FDeathTriggersGadgetsWorld
	{
		FDeathTriggersGadgetsWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FDeathTriggersGadgetsWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/** A creature of the monsters' side, with no armour, evasion, block or resistance. It deals nothing of its own. */
	ACataclysmEnemyCharacter* DeathTriggersGadgetsEnemy(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		UAbilitySystemComponent* Its = Made ? Made->GetAbilitySystemComponent() : nullptr;
		if (!Its)
		{
			return nullptr;
		}
		Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Made->SetHealth(100000.0f);
		Made->SetArmour(0.0f);
		Made->SetAttackDamage(0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), DeathTriggersGadgetsEnemyHealth);
		return Made;
	}

	float DeathTriggersGadgetsHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** The death row, as the item builds it from the row this layer proposes: `gadgets_cast_now` 100 on `player_death`. */
	FCataclysmPoolAction DeathTriggersGadgetsRow()
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("player_death"));
		Action.Pool = FName(UCataclysmAbilitySystemComponent::GadgetsCastNowAction);
		Action.Percent = 100.0f;
		Action.bGadgetsCastNow = true;
		Action.TriggerKey = FName(TEXT("A_row:gadgets_cast_now:player_death"));
		return Action;
	}

	/** A possessed player character holding a Demonic Whip, with its Heavy and Ultimate slots converted. */
	struct FDeathTriggersGadgetsPlayer
	{
		FDeathTriggersGadgetsPlayer(UWorld* World, const FVector& Where)
		{
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Player = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
			if (!State || !Controller || !Player)
			{
				return;
			}
			Controller->SetPlayerState(State);
			Controller->Possess(Player);
			System = State->GetCataclysmAbilitySystemComponent();
			Slots = Player->FindComponentByClass<UCataclysmWeaponSlotsComponent>();
			if (!System || !Slots)
			{
				return;
			}
			Slots->SetDamageType(TEXT("Demonic"));
			Slots->EquipWeaponType(TEXT("Whip"));
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);

			// THE TWO SLOTS' FLAGS, the row the enchantment session wrote for each keyed slot on 2026-10-16.
			TMap<FName, FCataclysmStatInputs> Stats;
			FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat));
			Line.Base = 0.0f;
			for (const TCHAR* SlotTag : {TEXT("Slot.Heavy"), TEXT("Slot.Ultimate")})
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::Enchantment;
				Row.Value = 1.0f;
				Row.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(SlotTag), /*ErrorIfNotFound=*/false));
				Line.Modifiers.Add(Row);
			}
			System->SetStatInputs(MoveTemp(Stats));
		}

		/** Whether everything was made and the whip holds a row for both slots. Reports why not. */
		bool IsUsable(FAutomationTestBase& Test) const
		{
			FCataclysmWeaponSkill Row;
			if (!Player || !System || !Slots || !HeldRow(ECataclysmAbilitySlot::Heavy, Row)
				|| !HeldRow(ECataclysmAbilitySlot::Ultimate, Row))
			{
				Test.AddError(TEXT("set-up: a possessed player holding a Demonic Whip with a Heavy and an Ultimate row. If "
								   "a row is missing, DT_WeaponSkills may be older than game/Data/WeaponSkills.csv: run "
								   "tools/generate_datatable_assets.py"));
				return false;
			}
			return true;
		}

		/** The row the whip holds for `Slot`, or false. */
		bool HeldRow(ECataclysmAbilitySlot Slot, FCataclysmWeaponSkill& Out) const
		{
			const FCataclysmWeaponSkill* Held = Slots ? Slots->GetAvailableSkills().FindByPredicate(
				[Slot](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == Slot; }) : nullptr;
			if (!Held)
			{
				return false;
			}
			Out = *Held;
			return true;
		}

		/** The skill gadget for `Slot` at `Where`, or null with the reason reported. */
		ACataclysmMinion* Places(FAutomationTestBase& Test, ECataclysmAbilitySlot Slot, const FVector& Where) const
		{
			ACataclysmMinion* Made = ACataclysmMinion::PlaceSkillGadget(Player, Slot, Where);
			if (!Made)
			{
				Test.AddError(TEXT("set-up: no skill gadget was placed. DT_MinionTypes needs the SkillGadget row: run "
								   "tools/generate_datatable_assets.py"));
			}
			return Made;
		}

		/** Killed by `Killer`'s direct blow, larger than its health, as a blow kills in play. Whether it died. */
		bool DiesTo(AActor* Killer) const
		{
			UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 1000000.0f);
			return UCataclysmSkillEffects::IsDead(Player);
		}

		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
	};

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FDeathTriggersGadgetsEventCount
	{
		FDeathTriggersGadgetsEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
			: System(InSystem)
			, Event(InEvent)
		{
			if (System)
			{
				Handle = System->OnActionEvent.AddLambda([this](FName Raised)
				{
					Count += Raised == Event ? 1 : 0;
				});
			}
		}
		~FDeathTriggersGadgetsEventCount()
		{
			if (System)
			{
				System->OnActionEvent.Remove(Handle);
			}
		}
		UCataclysmAbilitySystemComponent* System = nullptr;
		FName Event;
		int32 Count = 0;
		FDelegateHandle Handle;
	};

	/** Move the world's clock forward by hand. No timer fires and nobody thinks. */
	void DeathTriggersGadgetsPass(UWorld* World, float Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/** A gadget's clock: the seconds left and its length, both nought before its first cast. */
	void DeathTriggersGadgetsClock(const ACataclysmMinion* Gadget, float& OutRemaining, float& OutDuration)
	{
		Gadget->SkillGadgetClock(OutRemaining, OutDuration);
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 1. Each gadget casts once, at once, before its clock; a player without the row casts nothing (C1, C2, C3)
// ==========================================================================

CATACLYSM_TEST(FCataclysmDeathTriggersGadgetsEachCastsOnceTest,
	"Cataclysm.DeathTriggersGadgets.EachGadgetCastsOnceAtTheDeathBeforeItsClockAndAPlayerWithoutTheRowCastsNothing")
{
	using namespace CataclysmDeathTriggersGadgetsTest;

	// TWO PLAYERS 100 M APART, EACH WITH A HEAVY GADGET 20 M NORTH AND AN ULTIMATE GADGET 30 M SOUTH, AN ENEMY 3 M EAST
	// OF EACH GADGET AND A KILLER 3 M EAST OF EACH PLAYER. Only the wearer holds the death row.
	FDeathTriggersGadgetsWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	if (!TestNotNull(TEXT("set-up: the critical strike roll is pinned"), NeverCritical.Variable))
	{
		return false;
	}
	const FVector PlainHome(100.0f * M, 0.0f, 0.0f);
	FDeathTriggersGadgetsPlayer Wearer(World, FVector::ZeroVector);
	FDeathTriggersGadgetsPlayer Plain(World, PlainHome);
	if (!Wearer.IsUsable(*this) || !Plain.IsUsable(*this))
	{
		return false;
	}
	Wearer.System->SetPoolActions({DeathTriggersGadgetsRow()});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector South(0.0f, -30.0f * M, 0.0f);
	const FVector East(3.0f * M, 0.0f, 0.0f);
	ACataclysmMinion* Heavy = Wearer.Places(*this, ECataclysmAbilitySlot::Heavy, North);
	ACataclysmMinion* Ultimate = Wearer.Places(*this, ECataclysmAbilitySlot::Ultimate, South);
	ACataclysmMinion* PlainHeavy = Plain.Places(*this, ECataclysmAbilitySlot::Heavy, PlainHome + North);
	ACataclysmMinion* PlainUltimate = Plain.Places(*this, ECataclysmAbilitySlot::Ultimate, PlainHome + South);
	if (!Heavy || !Ultimate || !PlainHeavy || !PlainUltimate)
	{
		return false;
	}
	ACataclysmEnemyCharacter* HeavyEnemy = DeathTriggersGadgetsEnemy(World, North + East);
	ACataclysmEnemyCharacter* UltimateEnemy = DeathTriggersGadgetsEnemy(World, South + East);
	ACataclysmEnemyCharacter* PlainHeavyEnemy = DeathTriggersGadgetsEnemy(World, PlainHome + North + East);
	ACataclysmEnemyCharacter* PlainUltimateEnemy = DeathTriggersGadgetsEnemy(World, PlainHome + South + East);
	ACataclysmEnemyCharacter* Killer = DeathTriggersGadgetsEnemy(World, East);
	ACataclysmEnemyCharacter* PlainKiller = DeathTriggersGadgetsEnemy(World, PlainHome + East);
	if (!TestNotNull(TEXT("set-up: the enemy of the wearer's Heavy gadget"), HeavyEnemy)
		|| !TestNotNull(TEXT("set-up: the enemy of the wearer's Ultimate gadget"), UltimateEnemy)
		|| !TestNotNull(TEXT("set-up: the enemy of the other player's Heavy gadget"), PlainHeavyEnemy)
		|| !TestNotNull(TEXT("set-up: the enemy of the other player's Ultimate gadget"), PlainUltimateEnemy)
		|| !TestNotNull(TEXT("set-up: the wearer's killer"), Killer)
		|| !TestNotNull(TEXT("set-up: the other player's killer"), PlainKiller))
	{
		return false;
	}

	// EACH GADGET'S CLOCK STARTED BY ITS OWN FIRST CAST, and a quarter of a second passed: no clock has run.
	for (ACataclysmMinion* Each : {Heavy, Ultimate, PlainHeavy, PlainUltimate})
	{
		ACataclysmMinion::GadgetStep(Each);
	}
	TestTrue(TEXT("set-up: each of the four gadgets cast once on its first step"),
		Heavy->SkillGadgetCastsMade == 1 && Ultimate->SkillGadgetCastsMade == 1
		&& PlainHeavy->SkillGadgetCastsMade == 1 && PlainUltimate->SkillGadgetCastsMade == 1);
	float Remaining = 0.0f;
	float HeavyClock = 0.0f;
	float UltimateClock = 0.0f;
	DeathTriggersGadgetsClock(Heavy, Remaining, HeavyClock);
	DeathTriggersGadgetsClock(Ultimate, Remaining, UltimateClock);
	if (!TestTrue(*FString::Printf(TEXT("set-up: both clocks are longer than the quarter second waited: %.3f and %.3f"),
								   HeavyClock, UltimateClock),
			HeavyClock > 0.5f && UltimateClock > 0.5f))
	{
		return false;
	}
	DeathTriggersGadgetsPass(World, 0.25f);
	const float HeavyEnemyBefore = DeathTriggersGadgetsHealthOf(HeavyEnemy);
	const float UltimateEnemyBefore = DeathTriggersGadgetsHealthOf(UltimateEnemy);
	const float PlainHeavyEnemyBefore = DeathTriggersGadgetsHealthOf(PlainHeavyEnemy);
	const float PlainUltimateEnemyBefore = DeathTriggersGadgetsHealthOf(PlainUltimateEnemy);
	TestTrue(TEXT("set-up: each gadget's first cast struck its enemy"),
		HeavyEnemyBefore < DeathTriggersGadgetsEnemyHealth && UltimateEnemyBefore < DeathTriggersGadgetsEnemyHealth
		&& PlainHeavyEnemyBefore < DeathTriggersGadgetsEnemyHealth
		&& PlainUltimateEnemyBefore < DeathTriggersGadgetsEnemyHealth);

	// THE CONTROL, READ FIRST: the player holding no row dies, and its gadgets cast nothing.
	if (!TestTrue(TEXT("control: the player holding no row died"), Plain.DiesTo(PlainKiller)))
	{
		return false;
	}
	TestEqual(TEXT("control: its Heavy gadget cast nothing more"), PlainHeavy->SkillGadgetCastsMade, 1);
	TestEqual(TEXT("control: its Ultimate gadget cast nothing more"), PlainUltimate->SkillGadgetCastsMade, 1);
	TestEqual(TEXT("control: the Heavy gadget's enemy took nothing more"),
		DeathTriggersGadgetsHealthOf(PlainHeavyEnemy), PlainHeavyEnemyBefore, 0.001f);
	TestEqual(TEXT("control: the Ultimate gadget's enemy took nothing more"),
		DeathTriggersGadgetsHealthOf(PlainUltimateEnemy), PlainUltimateEnemyBefore, 0.001f);

	// THE WEARER DIES THE SAME WAY.
	FDeathTriggersGadgetsEventCount Deaths(Wearer.System, TEXT("player_death"));
	if (!TestTrue(TEXT("the wearer died"), Wearer.DiesTo(Killer)))
	{
		return false;
	}
	TestEqual(TEXT("its death raised player_death once"), Deaths.Count, 1);
	TestEqual(TEXT("its Heavy gadget cast once more, inside the death, before its clock had run"),
		Heavy->SkillGadgetCastsMade, 2);
	TestEqual(TEXT("and that cast was its death cast"), Heavy->SkillGadgetDeathCastsMade, 1);
	TestEqual(TEXT("its Ultimate gadget cast once more, inside the same death"), Ultimate->SkillGadgetCastsMade, 2);
	TestEqual(TEXT("and that cast was its death cast"), Ultimate->SkillGadgetDeathCastsMade, 1);
	TestTrue(*FString::Printf(TEXT("the Heavy gadget's death cast struck its enemy: %.2f, from %.2f"),
							  DeathTriggersGadgetsHealthOf(HeavyEnemy), HeavyEnemyBefore),
		DeathTriggersGadgetsHealthOf(HeavyEnemy) < HeavyEnemyBefore);
	TestTrue(*FString::Printf(TEXT("the Ultimate gadget's death cast struck its enemy: %.2f, from %.2f"),
							  DeathTriggersGadgetsHealthOf(UltimateEnemy), UltimateEnemyBefore),
		DeathTriggersGadgetsHealthOf(UltimateEnemy) < UltimateEnemyBefore);
	TestEqual(TEXT("and the killer, beyond every gadget's reach, took nothing"),
		DeathTriggersGadgetsHealthOf(Killer), DeathTriggersGadgetsEnemyHealth, 0.001f);
	return true;
}

// ==========================================================================
// 2. No enemy within its reach, no cast; one with an enemy casts its whole cast (C2)
// ==========================================================================

CATACLYSM_TEST(FCataclysmDeathTriggersGadgetsReachTest,
	"Cataclysm.DeathTriggersGadgets.AGadgetWithNoEnemyInReachCastsNothingAndOneWithAnEnemyCastsItsWholeCast")
{
	using namespace CataclysmDeathTriggersGadgetsTest;

	// THE WEARER'S HEAVY GADGET 20 M NORTH WITH ITS ONLY ENEMY 6 M EAST OF IT, PAST SEARING LASH'S 5 M; ITS ULTIMATE
	// GADGET 30 M SOUTH WITH AN ENEMY 3 M EAST; A KILLER 3 M EAST OF THE WEARER. A LIVING PLAYER 100 M EAST, HOLDING NO
	// ROW, WITH AN ULTIMATE GADGET LAID OUT AS THE WEARER'S: its clock's cast is the figure the death cast is held to.
	FDeathTriggersGadgetsWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	if (!TestNotNull(TEXT("set-up: the critical strike roll is pinned"), NeverCritical.Variable))
	{
		return false;
	}
	const FVector LivingHome(100.0f * M, 0.0f, 0.0f);
	FDeathTriggersGadgetsPlayer Wearer(World, FVector::ZeroVector);
	FDeathTriggersGadgetsPlayer Living(World, LivingHome);
	if (!Wearer.IsUsable(*this) || !Living.IsUsable(*this))
	{
		return false;
	}
	Wearer.System->SetPoolActions({DeathTriggersGadgetsRow()});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector South(0.0f, -30.0f * M, 0.0f);
	ACataclysmMinion* Heavy = Wearer.Places(*this, ECataclysmAbilitySlot::Heavy, North);
	ACataclysmMinion* Ultimate = Wearer.Places(*this, ECataclysmAbilitySlot::Ultimate, South);
	ACataclysmMinion* LivingUltimate = Living.Places(*this, ECataclysmAbilitySlot::Ultimate, LivingHome + South);
	if (!Heavy || !Ultimate || !LivingUltimate)
	{
		return false;
	}
	ACataclysmEnemyCharacter* FarEnemy = DeathTriggersGadgetsEnemy(World, North + FVector(6.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* UltimateEnemy = DeathTriggersGadgetsEnemy(World, South + FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* LivingEnemy =
		DeathTriggersGadgetsEnemy(World, LivingHome + South + FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Killer = DeathTriggersGadgetsEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the enemy 6 m from the Heavy gadget"), FarEnemy)
		|| !TestNotNull(TEXT("set-up: the enemy of the Ultimate gadget"), UltimateEnemy)
		|| !TestNotNull(TEXT("set-up: the enemy of the living player's Ultimate gadget"), LivingEnemy)
		|| !TestNotNull(TEXT("set-up: the killer"), Killer))
	{
		return false;
	}
	FCataclysmWeaponSkill SearingLash;
	Wearer.HeldRow(ECataclysmAbilitySlot::Heavy, SearingLash);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the Heavy gadget reaches %.2f cm, short of its enemy's 600"),
								   Heavy->SkillGadgetReachCm(SearingLash)),
			Heavy->SkillGadgetReachCm(SearingLash) > 0.0f && Heavy->SkillGadgetReachCm(SearingLash) < 590.0f))
	{
		return false;
	}

	// THE CONTROLS, READ FIRST: the Heavy gadget's own clock casts nothing at an enemy past its reach; the living
	// player's Ultimate gadget's own clock casts at its enemy, and that cast's take is the figure below.
	ACataclysmMinion::GadgetStep(Heavy);
	TestEqual(TEXT("control: the Heavy gadget's own step casts nothing with no enemy in its reach"),
		Heavy->SkillGadgetCastsMade, 0);
	ACataclysmMinion::GadgetStep(LivingUltimate);
	const float LivingTake = DeathTriggersGadgetsEnemyHealth - DeathTriggersGadgetsHealthOf(LivingEnemy);
	TestEqual(TEXT("control: the living player's Ultimate gadget casts once on its own step"),
		LivingUltimate->SkillGadgetCastsMade, 1);
	TestTrue(*FString::Printf(TEXT("control: and its cast takes %.2f from its enemy"), LivingTake), LivingTake > 0.0f);

	// THE WEARER DIES.
	if (!TestTrue(TEXT("the wearer died"), Wearer.DiesTo(Killer)))
	{
		return false;
	}
	TestEqual(TEXT("the Heavy gadget, with no enemy in its reach, cast nothing at the death"),
		Heavy->SkillGadgetCastsMade, 0);
	TestEqual(TEXT("and made no death cast"), Heavy->SkillGadgetDeathCastsMade, 0);
	TestEqual(TEXT("so the enemy past its reach keeps its health"), DeathTriggersGadgetsHealthOf(FarEnemy),
		DeathTriggersGadgetsEnemyHealth, 0.001f);
	TestEqual(TEXT("the Ultimate gadget, with an enemy in its reach, still cast once at the death"),
		Ultimate->SkillGadgetDeathCastsMade, 1);
	const float DeathTake = DeathTriggersGadgetsEnemyHealth - DeathTriggersGadgetsHealthOf(UltimateEnemy);
	TestEqual(*FString::Printf(TEXT("at the whole of its damage: it took %.2f, as the living player's clock cast took "
									"%.2f"), DeathTake, LivingTake),
		DeathTake, LivingTake, 0.01f);
	return true;
}

// ==========================================================================
// 3. The clock is unchanged (C4)
// ==========================================================================

CATACLYSM_TEST(FCataclysmDeathTriggersGadgetsClockTest,
	"Cataclysm.DeathTriggersGadgets.TheDeathCastLeavesEachGadgetsClockAsItWas")
{
	using namespace CataclysmDeathTriggersGadgetsTest;

	// ONE WEARER: A HEAVY GADGET 20 M NORTH THAT HAS CAST, AND AN ULTIMATE GADGET 30 M SOUTH THAT HAS NOT, EACH WITH AN
	// ENEMY 3 M EAST OF IT; A KILLER 3 M EAST OF THE WEARER.
	FDeathTriggersGadgetsWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FDeathTriggersGadgetsPlayer Wearer(World, FVector::ZeroVector);
	if (!Wearer.IsUsable(*this))
	{
		return false;
	}
	Wearer.System->SetPoolActions({DeathTriggersGadgetsRow()});
	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector South(0.0f, -30.0f * M, 0.0f);
	const FVector East(3.0f * M, 0.0f, 0.0f);
	ACataclysmMinion* Heavy = Wearer.Places(*this, ECataclysmAbilitySlot::Heavy, North);
	ACataclysmMinion* Ultimate = Wearer.Places(*this, ECataclysmAbilitySlot::Ultimate, South);
	if (!Heavy || !Ultimate)
	{
		return false;
	}
	ACataclysmEnemyCharacter* HeavyEnemy = DeathTriggersGadgetsEnemy(World, North + East);
	ACataclysmEnemyCharacter* UltimateEnemy = DeathTriggersGadgetsEnemy(World, South + East);
	ACataclysmEnemyCharacter* Killer = DeathTriggersGadgetsEnemy(World, East);
	if (!TestNotNull(TEXT("set-up: the enemy of the Heavy gadget"), HeavyEnemy)
		|| !TestNotNull(TEXT("set-up: the enemy of the Ultimate gadget"), UltimateEnemy)
		|| !TestNotNull(TEXT("set-up: the killer"), Killer))
	{
		return false;
	}

	// THE HEAVY GADGET'S CLOCK STARTED BY ITS FIRST CAST; THE ULTIMATE GADGET LEFT UNSTEPPED.
	ACataclysmMinion::GadgetStep(Heavy);
	float Remaining = 0.0f;
	float Clock = 0.0f;
	DeathTriggersGadgetsClock(Heavy, Remaining, Clock);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the Heavy gadget cast and its clock of %.3f s runs"), Clock),
			Heavy->SkillGadgetCastsMade == 1 && Heavy->bSkillGadgetHasCast && Clock > 0.5f))
	{
		return false;
	}
	TestFalse(TEXT("control: the Ultimate gadget has not cast, so its clock is not running"),
		Ultimate->bSkillGadgetHasCast);
	DeathTriggersGadgetsPass(World, 0.25f);
	const float LastCastAge = Heavy->LastSkillGadgetCastAtAgeSeconds;
	float RemainingBefore = 0.0f;
	DeathTriggersGadgetsClock(Heavy, RemainingBefore, Clock);

	if (!TestTrue(TEXT("the wearer died"), Wearer.DiesTo(Killer)))
	{
		return false;
	}
	TestEqual(TEXT("set-up: the Heavy gadget made its death cast"), Heavy->SkillGadgetDeathCastsMade, 1);
	TestEqual(TEXT("set-up: and the Ultimate gadget its own"), Ultimate->SkillGadgetDeathCastsMade, 1);
	TestEqual(TEXT("the Heavy gadget's last clock cast is where it was"), Heavy->LastSkillGadgetCastAtAgeSeconds,
		LastCastAge, 0.0001f);
	float RemainingAfter = 0.0f;
	DeathTriggersGadgetsClock(Heavy, RemainingAfter, Clock);
	TestEqual(*FString::Printf(TEXT("so its clock has %.3f s left, as before the death"), RemainingBefore),
		RemainingAfter, RemainingBefore, 0.001f);
	TestFalse(TEXT("the Ultimate gadget's clock is still not running"), Ultimate->bSkillGadgetHasCast);

	// STOOD BACK UP, the clocks go on as they were. DRIVEN DIRECTLY, as `CataclysmNearbyActionTests.cpp` does: a world
	// built for a test is never ticked, so the respawn timer never fires.
	Wearer.Player->Revive();
	if (!TestFalse(TEXT("set-up: the wearer stood back up"), UCataclysmSkillEffects::IsDead(Wearer.Player)))
	{
		return false;
	}
	DeathTriggersGadgetsPass(World, Clock - 0.35f);
	ACataclysmMinion::GadgetStep(Heavy);
	TestEqual(TEXT("a tenth of a second short of its clock, counted from its last clock cast, the Heavy gadget casts "
				   "nothing"), Heavy->SkillGadgetCastsMade, 2);
	ACataclysmMinion::GadgetStep(Ultimate);
	TestEqual(TEXT("the Ultimate gadget, whose clock never started, casts on its first step"),
		Ultimate->SkillGadgetCastsMade, 2);
	DeathTriggersGadgetsPass(World, 0.15f);
	ACataclysmMinion::GadgetStep(Heavy);
	TestEqual(TEXT("its clock run, the Heavy gadget casts"), Heavy->SkillGadgetCastsMade, 3);
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
