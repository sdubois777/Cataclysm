// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interface/CataclysmSkillBar.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Skill gadgets, stack 2: the basic attack's click, its clock, the highest-health aim and the late landings. Ruled
 * 2026-10-16 by the coordinating session ("Stack 2 rulings", items 1-7 and P1-P9), for "Your basic attack is converted
 * into a deployable gadget that fires your basic attack at the nearest enemy within range, once per basic attack
 * cooldown. Scales off gadget damage." In the group of stack 1, `Cataclysm.SkillGadgets.`, whose rig this follows.
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER WITH THE GAME'S OWN CONTROLLER, holding a Demonic Whip unless a case says
 * otherwise, given a weapon damage of 100 by hand: its basic attack is a 3 m Strike (`ItemBases.csv`: Radius=3;
 * Angle=45; MaxTargets=1), its Heavy Searing Lash a 5 m cone. Two cases hold another weapon: a War Shield, which has
 * no basic attack, and a Demonic Axe, whose Ultimate Butcher's Bill is a rack of thirty axes.
 *
 * NO ROW IS AUTHORED, SO EVERY STAT IS GRANTED BY HAND, one stat line per call, replacing the last. The basic slot's
 * flag is a flat 1 on `skill_slot_is_a_gadget` requiring `Slot.Basic`, the row the enchantment session is to write.
 * The gadget's type row, `SkillGadget`, is in `DT_MinionTypes` since stack 1's window.
 *
 * THE CLICK IS DRIVEN FROM WHERE THE CURSOR'S ENEMY IS KNOWN, `ACataclysmPlayerController::PressOnAnEnemyForTest`:
 * a test world has no cursor to trace, so `Input_MoveToCursorStarted` itself, its interface test and its stop of a walk
 * are not reached, and nor is the walk a floor click orders. NOTHING HERE WAITS ON A TICK: the world's clock is moved by
 * hand, a gadget's step is called directly, and a projectile is stepped by hand.
 */
namespace CataclysmSkillGadgetBasicTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FBasicGadgetWorld
	{
		FBasicGadgetWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FBasicGadgetWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/** A creature of the monsters' side holding `Health`, with no armour, evasion, block or resistance. It deals nothing. */
	ACataclysmEnemyCharacter* BasicGadgetEnemy(UWorld* World, const FVector& Where, float Health = 50000.0f)
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
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
		return Made;
	}

	float BasicGadgetHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** One row of a stat line a test's player holds: a flat figure or an increase, with one required tag or none. */
	struct FBasicGadgetRow
	{
		FName Stat;
		float Value = 0.0f;
		ECataclysmStatBucket Bucket = ECataclysmStatBucket::Flat;
		const TCHAR* RequiredTag = nullptr;
		ECataclysmStatScale Scale = ECataclysmStatScale::Fixed;
		float ScaleStep = 0.0f;
	};

	/** The slot's flag: a flat 1 on `skill_slot_is_a_gadget` requiring the slot's tag, the row to be authored. */
	FBasicGadgetRow BasicGadgetConverts(const TCHAR* SlotTag)
	{
		FBasicGadgetRow Row;
		Row.Stat = FName(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat);
		Row.Value = 1.0f;
		Row.RequiredTag = SlotTag;
		return Row;
	}

	/** An increase requiring `Type.Deployable`, as every gadget row does. */
	FBasicGadgetRow BasicGadgetIncrease(const TCHAR* Stat, float Value)
	{
		FBasicGadgetRow Row;
		Row.Stat = FName(Stat);
		Row.Value = Value;
		Row.Bucket = ECataclysmStatBucket::Increased;
		Row.RequiredTag = TEXT("Type.Deployable");
		return Row;
	}

	/** A possessed player character with the game's own controller, holding one weapon. */
	struct FBasicGadgetRig
	{
		FBasicGadgetRig(UWorld* World, const FVector& Where, const TCHAR* DamageType = TEXT("Demonic"),
						const TCHAR* WeaponType = TEXT("Whip"))
		{
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
			Controller = World->SpawnActor<ACataclysmPlayerController>();
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
			Slots->SetDamageType(DamageType);
			Slots->EquipWeaponType(WeaponType);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		}

		/** Whether everything was made and the weapon holds a row for `Slot`. Reports why not. */
		bool IsUsable(FAutomationTestBase& Test, ECataclysmAbilitySlot Slot = ECataclysmAbilitySlot::BasicAttack) const
		{
			FCataclysmWeaponSkill Row;
			if (!Player || !Controller || !System || !Slots || !HeldRow(Slot, Row))
			{
				Test.AddError(TEXT("set-up: a possessed player with the game's controller, holding its weapon and a row "
								   "for the slot. If a row is missing, DT_WeaponSkills or DT_ItemBases may be older "
								   "than game/Data: run tools/generate_datatable_assets.py"));
				return false;
			}
			return true;
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			System->SetNumericAttributeBase(Attribute, Value);
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			return System->GetNumericAttribute(Attribute);
		}

		/** The row the weapon holds for `Slot`, or false. */
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

		/** REPLACES THE WHOLE STAT LINE with these rows. */
		void Holds(const TArray<FBasicGadgetRow>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FBasicGadgetRow& Each : Rows)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = Each.Bucket;
				Row.Source = ECataclysmModifierSource::Enchantment;
				Row.Value = Each.Value;
				if (Each.RequiredTag)
				{
					Row.RequiredTags.AddTag(
						FGameplayTag::RequestGameplayTag(FName(Each.RequiredTag), /*ErrorIfNotFound=*/false));
				}
				Row.Scale = Each.Scale;
				Row.ScaleStep = Each.ScaleStep;
				FCataclysmStatInputs& Line = Stats.FindOrAdd(Each.Stat);
				Line.Base = 0.0f;
				Line.Modifiers.Add(Row);
			}
			System->SetStatInputs(MoveTemp(Stats));
		}

		/** Move the player, so it does not stand on a gadget placed at its feet. */
		void StandsAt(const FVector& Where) const
		{
			Player->SetActorLocation(Where, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr,
									 ETeleportType::TeleportPhysics);
		}

		ACataclysmPlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
	};

	/** A skill gadget for this rig's `Slot` at `Where`, or null with the reason reported. */
	ACataclysmMinion* BasicGadgetPlace(FAutomationTestBase& Test, const FBasicGadgetRig& Rig, ECataclysmAbilitySlot Slot,
									   const FVector& Where)
	{
		ACataclysmMinion* Made = ACataclysmMinion::PlaceSkillGadget(Rig.Player, Slot, Where);
		if (!Made)
		{
			Test.AddError(TEXT("set-up: no skill gadget was placed. DT_MinionTypes needs the SkillGadget row: run "
							   "tools/generate_datatable_assets.py"));
		}
		return Made;
	}

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FBasicGadgetEventCount
	{
		FBasicGadgetEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
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
		~FBasicGadgetEventCount()
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
	void BasicGadgetPass(UWorld* World, float Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/** Run the gadget's quarter-second step, as `RegenerationStep` calls it. */
	void BasicGadgetStep(ACataclysmMinion* Gadget)
	{
		ACataclysmMinion::GadgetStep(Gadget);
	}

	/** The clock's length of a gadget, nought before its first cast. */
	float BasicGadgetClockLength(const ACataclysmMinion* Gadget)
	{
		float Remaining = 0.0f;
		float Duration = 0.0f;
		Gadget->SkillGadgetClock(Remaining, Duration);
		return Duration;
	}

	/** `Steps` quarter-second steps of the gadget, the world's clock moved before each. */
	void BasicGadgetQuarterSteps(UWorld* World, ACataclysmMinion* Gadget, int32 Steps)
	{
		for (int32 Each = 0; Each < Steps; ++Each)
		{
			BasicGadgetPass(World, 0.25f);
			BasicGadgetStep(Gadget);
		}
	}

	/**
	 * Step every projectile in the world not in `Done` by a twentieth of a second until each has finished, adding each
	 * to `Done`. How many finished in this call. `Done` is the test's own, because a finished projectile lingers in the
	 * world for a while and a later call must not count it again.
	 */
	int32 BasicGadgetLandEveryProjectile(UWorld* World, TSet<const ACataclysmProjectile*>& Done)
	{
		int32 Finished = 0;
		for (int32 Round = 0; Round < 200; ++Round)
		{
			TArray<ACataclysmProjectile*> Flying;
			for (TActorIterator<ACataclysmProjectile> It(World); It; ++It)
			{
				if (IsValid(*It) && !Done.Contains(*It))
				{
					Flying.Add(*It);
				}
			}
			if (Flying.IsEmpty())
			{
				break;
			}
			for (ACataclysmProjectile* Shot : Flying)
			{
				if (IsValid(Shot) && !Shot->Step(0.05f))
				{
					Done.Add(Shot);
					++Finished;
				}
			}
		}
		return Finished;
	}

	/** The running one-use grant of a rack cast by `Gadget`, or null once its use has ended. */
	UCataclysmProjectileSkill* BasicGadgetRunningRack(const UCataclysmAbilitySystemComponent* System,
													  const ACataclysmMinion* Gadget)
	{
		for (const FGameplayAbilitySpec& Spec : System->GetActivatableAbilities())
		{
			UCataclysmProjectileSkill* Rack = Cast<UCataclysmProjectileSkill>(Spec.GetPrimaryInstance());
			if (Rack && Spec.IsActive() && Rack->FreeRepeatCastBy.Get() == Gadget)
			{
				return Rack;
			}
		}
		return nullptr;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 1. A click on an enemy places the basic gadget and does not swing (S15; stack 2, item 2)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetBasicClickTest,
	"Cataclysm.SkillGadgets.AClickOnAnEnemyPlacesTheBasicGadgetSwingsNothingAndTheNextClickMovesIt")
{
	using namespace CataclysmSkillGadgetBasicTest;

	// THE PLAYER AT THE ORIGIN WITH AN ENEMY 2 M ALONG X, within the whip's 3 m basic reach.
	FBasicGadgetWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FBasicGadgetRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Enemy = BasicGadgetEnemy(World, FVector(2.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: an enemy within the whip's reach"), Enemy))
	{
		return false;
	}
	const FGameplayAttribute Mana = UCataclysmVitalAttributeSet::GetManaAttribute();

	// THE CONTROL, READ FIRST: holding no row, a click on the enemy swings at it.
	const float NeverSwung = Rig.Controller->LastSwingSecondsForTest();
	Rig.Controller->PressOnAnEnemyForTest(Enemy);
	Rig.Controller->ReleaseTheButtonForTest();
	TestTrue(TEXT("control: holding no row, a click on an enemy in reach starts a swing"),
		Rig.Controller->LastSwingSecondsForTest() > NeverSwung);
	TestNull(TEXT("control: and places no gadget"), Rig.System->SkillGadgetIn(ECataclysmAbilitySlot::BasicAttack));

	// CONVERTED, TWO SECONDS ON, so a swing would be allowed again.
	BasicGadgetPass(World, 2.0f);
	Rig.Holds({BasicGadgetConverts(TEXT("Slot.Basic"))});
	Rig.Set(Mana, 500.0f);
	TestTrue(TEXT("set-up: the basic slot is converted"),
		Rig.System->SlotIsASkillGadget(ECataclysmAbilitySlot::BasicAttack));

	// A PRESS ON THE FLOOR, WHICH NAMES NO ENEMY: nothing is placed.
	Rig.Controller->PressOnAnEnemyForTest(nullptr);
	TestNull(TEXT("a press on the floor places no basic gadget"),
		Rig.System->SkillGadgetIn(ECataclysmAbilitySlot::BasicAttack));

	const float SwungAt = Rig.Controller->LastSwingSecondsForTest();
	const float EnemyBefore = BasicGadgetHealthOf(Enemy);
	const FVector Feet = Rig.Player->GetActorLocation();
	Rig.Controller->PressOnAnEnemyForTest(Enemy);
	ACataclysmMinion* Gadget = Rig.System->SkillGadgetIn(ECataclysmAbilitySlot::BasicAttack);
	if (!TestNotNull(TEXT("the click on the enemy placed a basic gadget (DT_MinionTypes needs the SkillGadget row)"),
			Gadget))
	{
		return false;
	}
	TestTrue(TEXT("a skill gadget of the basic slot"),
		Gadget->IsSkillGadget() && Gadget->SkillGadgetSlot == ECataclysmAbilitySlot::BasicAttack);
	TestTrue(*FString::Printf(TEXT("at the player's feet: %.3f cm away"), FVector::Dist(Gadget->GetActorLocation(), Feet)),
		FVector::Dist(Gadget->GetActorLocation(), Feet) < 1.0f);
	TestEqual(TEXT("the click started no swing"), Rig.Controller->LastSwingSecondsForTest(), SwungAt, 0.0001f);
	TestEqual(TEXT("the enemy took nothing from the click"), BasicGadgetHealthOf(Enemy), EnemyBefore, 0.001f);
	TestEqual(TEXT("no mana on hit was paid"), Rig.Get(Mana), 500.0f, 0.001f);
	TestEqual(TEXT("and the gadget cast nothing as it was placed"), Gadget->SkillGadgetCastsMade, 0);

	// WITHOUT THE RELEASE, A SECOND CLICK FROM 5 M ALONG Y IS THE SAME PRESS: nothing moves and nothing swings.
	const FVector Second(0.0f, 5.0f * M, 0.0f);
	Rig.StandsAt(Second);
	Rig.Controller->PressOnAnEnemyForTest(Enemy);
	TestTrue(TEXT("before the release a second click moves nothing"),
		FVector::Dist(Gadget->GetActorLocation(), Feet) < 1.0f);
	TestEqual(TEXT("and swings nothing"), Rig.Controller->LastSwingSecondsForTest(), SwungAt, 0.0001f);

	// RELEASED, THE NEXT CLICK MOVES THE SAME GADGET.
	Rig.Controller->ReleaseTheButtonForTest();
	Rig.Controller->PressOnAnEnemyForTest(Enemy);
	Rig.Controller->ReleaseTheButtonForTest();
	TestTrue(TEXT("after the release the next click keeps the same gadget"),
		Rig.System->SkillGadgetIn(ECataclysmAbilitySlot::BasicAttack) == Gadget);
	TestTrue(*FString::Printf(TEXT("and moves it to the player's feet: %.3f cm away"),
							  FVector::Dist(Gadget->GetActorLocation(), Second)),
		FVector::Dist(Gadget->GetActorLocation(), Second) < 1.0f);
	TestEqual(TEXT("and still swings nothing"), Rig.Controller->LastSwingSecondsForTest(), SwungAt, 0.0001f);
	return true;
}

// ==========================================================================
// 2. The basic gadget casts the basic attack within its reach, on a kept beat (stack 2, items 1 and P1)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetBasicClockTest,
	"Cataclysm.SkillGadgets.TheBasicGadgetCastsTheBasicAttackWithinItsReachOnTheSwingIntervalKeptAsABeat")
{
	using namespace CataclysmSkillGadgetBasicTest;

	// TWO WHIP PLAYERS 60 M APART, EACH WITH AN ATTACK SPEED OF 1.3, a swing every 0.769 s. The second holds "Gadgets
	// fire 20%-40% faster" at 50. Each gadget 20 m along Y from its player, an enemy 2 m from it along X, and for the
	// first, one 4 m from it along -X, beyond the whip's 3 m.
	FBasicGadgetWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FBasicGadgetRig Rig(World, FVector::ZeroVector);
	const FVector FasterHome(60.0f * M, 0.0f, 0.0f);
	FBasicGadgetRig Faster(World, FasterHome);
	FCataclysmWeaponSkill Basic;
	if (!Rig.IsUsable(*this) || !Faster.IsUsable(*this) || !Rig.HeldRow(ECataclysmAbilitySlot::BasicAttack, Basic))
	{
		return false;
	}
	Rig.Set(UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 1.3f);
	Faster.Set(UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 1.3f);

	// THE CONTROL, READ FIRST: the player's own swing interval.
	const float Interval = UCataclysmBasicAttack::SecondsBetweenSwingsFor(Rig.System);
	TestEqual(TEXT("control: an attack speed of 1.3 is a swing every 0.769 s"), Interval, 1.0f / 1.3f, 0.0001f);

	Rig.Holds({BasicGadgetConverts(TEXT("Slot.Basic"))});
	Faster.Holds({BasicGadgetConverts(TEXT("Slot.Basic")), BasicGadgetIncrease(TEXT("minion_attack_speed"), 50.0f)});
	const FVector At(0.0f, 20.0f * M, 0.0f);
	const FVector FasterAt = FasterHome + At;
	ACataclysmMinion* Gadget = BasicGadgetPlace(*this, Rig, ECataclysmAbilitySlot::BasicAttack, At);
	ACataclysmMinion* FasterGadget = BasicGadgetPlace(*this, Faster, ECataclysmAbilitySlot::BasicAttack, FasterAt);
	ACataclysmEnemyCharacter* Beyond = BasicGadgetEnemy(World, At + FVector(-4.0f * M, 0.0f, 0.0f));
	if (!Gadget || !FasterGadget || !TestNotNull(TEXT("set-up: an enemy beyond the reach"), Beyond))
	{
		return false;
	}
	TestTrue(*FString::Printf(TEXT("set-up: the gadget aims by the whip's basic reach, 3 m: %.1f cm"),
							  Gadget->SkillGadgetReachCm(Basic)),
		FMath::IsNearlyEqual(Gadget->SkillGadgetReachCm(Basic), 3.0f * M, 0.01f));

	// ONLY AN ENEMY BEYOND THE REACH: nothing is cast.
	BasicGadgetStep(Gadget);
	TestEqual(TEXT("an enemy 4 m away, beyond the basic's 3 m, is not aimed at: no cast"), Gadget->SkillGadgetCastsMade, 0);

	ACataclysmEnemyCharacter* Near = BasicGadgetEnemy(World, At + FVector(2.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* FasterNear = BasicGadgetEnemy(World, FasterAt + FVector(2.0f * M, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: an enemy beside each gadget"), Near && FasterNear))
	{
		return false;
	}
	BasicGadgetStep(Gadget);
	BasicGadgetStep(FasterGadget);
	TestEqual(TEXT("with an enemy 2 m away the gadget casts once"), Gadget->SkillGadgetCastsMade, 1);
	TestTrue(TEXT("it struck the enemy within reach"), BasicGadgetHealthOf(Near) < 50000.0f);
	TestEqual(TEXT("and was not the gadget's own blow"), Gadget->AttacksMade, 0);
	TestEqual(TEXT("the clock is the player's swing interval"), BasicGadgetClockLength(Gadget), Interval, 0.0001f);
	TestEqual(TEXT("and the fire-rate row at 50 makes it the interval over one and a half"),
		BasicGadgetClockLength(FasterGadget), Interval / 1.5f, 0.0001f);

	// THIRTY-EIGHT QUARTER-SECOND STEPS, 9.5 SECONDS: the kept beat casts 1 + 12 = 13 in all, the number the 0.769 s
	// interval gives over 9.5 s, where a clock run as a cooldown would cast once a second, 10. With the row, 0.513 s:
	// 1 + 18 = 19. NOT TEN SECONDS, as the first build had it: thirteen 0.769 s intervals end at exactly 10.0 s, so the
	// count sat on a boundary only the clock's 1 ms allowance kept. At 9.5 s the last due cast of each clock (9.23 s,
	// cast on the 9.25 s step) and the next (10.0 s and 9.74 s) are a quarter of a second or more from the end. Ruled
	// 2026-10-16 by the coordinating session.
	for (int32 Each = 0; Each < 38; ++Each)
	{
		BasicGadgetPass(World, 0.25f);
		BasicGadgetStep(Gadget);
		BasicGadgetStep(FasterGadget);
	}
	TestEqual(TEXT("over 9.5 seconds of quarter-second steps the gadget casts 13 times, not once a second (10)"),
		Gadget->SkillGadgetCastsMade, 13);
	TestEqual(TEXT("and never at the enemy beyond its reach"), BasicGadgetHealthOf(Beyond), 50000.0f, 0.001f);
	TestEqual(TEXT("the fire-rate row raises that to 19"), FasterGadget->SkillGadgetCastsMade, 19);
	return true;
}

// ==========================================================================
// 3. An interval of nought never casts (stack 2, item 1)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetBasicNeverTest,
	"Cataclysm.SkillGadgets.ABasicGadgetWhosePlayerNeverSwingsNeverCasts")
{
	using namespace CataclysmSkillGadgetBasicTest;

	// TWO WHIP PLAYERS 60 M APART: THE CONTROL WITH AN ATTACK SPEED OF 1.3, THE OTHER WITH NOUGHT.
	FBasicGadgetWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FBasicGadgetRig Swings(World, FVector::ZeroVector);
	const FVector StillHome(60.0f * M, 0.0f, 0.0f);
	FBasicGadgetRig Still(World, StillHome);
	if (!Swings.IsUsable(*this) || !Still.IsUsable(*this))
	{
		return false;
	}
	Swings.Set(UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 1.3f);
	Still.Set(UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 0.0f);
	Swings.Holds({BasicGadgetConverts(TEXT("Slot.Basic"))});
	Still.Holds({BasicGadgetConverts(TEXT("Slot.Basic"))});
	const FVector At(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* Control = BasicGadgetPlace(*this, Swings, ECataclysmAbilitySlot::BasicAttack, At);
	ACataclysmMinion* Gadget = BasicGadgetPlace(*this, Still, ECataclysmAbilitySlot::BasicAttack, StillHome + At);
	ACataclysmEnemyCharacter* ControlEnemy = BasicGadgetEnemy(World, At + FVector(2.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Enemy = BasicGadgetEnemy(World, StillHome + At + FVector(2.0f * M, 0.0f, 0.0f));
	if (!Control || !Gadget || !TestTrue(TEXT("set-up: two enemies"), ControlEnemy && Enemy))
	{
		return false;
	}

	// THE CONTROL: a player who swings has a gadget that casts on its first step.
	BasicGadgetStep(Control);
	TestEqual(TEXT("control: the gadget of a player who swings casts on its first step"), Control->SkillGadgetCastsMade, 1);

	TestEqual(TEXT("set-up: an attack speed of nought is a swing interval of nought, never"),
		UCataclysmBasicAttack::SecondsBetweenSwingsFor(Still.System), 0.0f, 0.0001f);
	BasicGadgetStep(Gadget);
	BasicGadgetQuarterSteps(World, Gadget, 8);
	TestEqual(TEXT("the gadget of a player who never swings casts nothing over two seconds, the first cast included"),
		Gadget->SkillGadgetCastsMade, 0);
	TestEqual(TEXT("so its enemy keeps its health"), BasicGadgetHealthOf(Enemy), 50000.0f, 0.001f);
	return true;
}

// ==========================================================================
// 4. A Shield's basic gadget casts nothing (S10)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetBasicShieldTest,
	"Cataclysm.SkillGadgets.AShieldsBasicGadgetIsPlacedAndCastsNothing")
{
	using namespace CataclysmSkillGadgetBasicTest;

	// A WAR SHIELD PLAYER, WHOSE WEAPON HAS NO BASIC ATTACK (`ItemBases.csv`: an empty BasicShape), AND A WHIP PLAYER
	// 60 M AWAY FOR THE CONTROL.
	FBasicGadgetWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FBasicGadgetRig Whip(World, FVector(60.0f * M, 0.0f, 0.0f));
	FBasicGadgetRig Rig(World, FVector::ZeroVector, TEXT("War"), TEXT("Shield"));
	FCataclysmWeaponSkill Unheld;
	if (!Whip.IsUsable(*this) || !Rig.IsUsable(*this, ECataclysmAbilitySlot::Heavy)
		|| !TestFalse(TEXT("set-up: the shield holds no basic attack row"),
			   Rig.HeldRow(ECataclysmAbilitySlot::BasicAttack, Unheld)))
	{
		return false;
	}
	ACataclysmEnemyCharacter* WhipEnemy = BasicGadgetEnemy(World, FVector(62.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Enemy = BasicGadgetEnemy(World, FVector(1.5f * M, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: two enemies"), WhipEnemy && Enemy))
	{
		return false;
	}
	const FGameplayTag BasicTag = CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::BasicAttack);

	// THE CONTROL: the whip player's basic gadget casts on its first step.
	Whip.Holds({BasicGadgetConverts(TEXT("Slot.Basic"))});
	Whip.System->PressOfASkillGadgetSlot(BasicTag);
	Whip.System->ReleaseOfASkillGadgetSlot(BasicTag);
	ACataclysmMinion* WhipGadget = Whip.System->SkillGadgetIn(ECataclysmAbilitySlot::BasicAttack);
	if (!TestNotNull(TEXT("set-up: the whip player's basic gadget is placed"), WhipGadget))
	{
		return false;
	}
	Whip.StandsAt(FVector(60.0f * M, -10.0f * M, 0.0f));
	BasicGadgetStep(WhipGadget);
	TestEqual(TEXT("control: a whip's basic gadget casts on its first step"), WhipGadget->SkillGadgetCastsMade, 1);

	Rig.Holds({BasicGadgetConverts(TEXT("Slot.Basic"))});
	TestTrue(TEXT("the shield's basic slot is converted all the same"),
		Rig.System->SlotIsASkillGadget(ECataclysmAbilitySlot::BasicAttack));
	Rig.System->PressOfASkillGadgetSlot(BasicTag);
	Rig.System->ReleaseOfASkillGadgetSlot(BasicTag);
	ACataclysmMinion* Gadget = Rig.System->SkillGadgetIn(ECataclysmAbilitySlot::BasicAttack);
	TestNotNull(TEXT("the shield's press still places a basic gadget"), Gadget);
	if (!Gadget)
	{
		return false;
	}
	Rig.StandsAt(FVector(0.0f, -10.0f * M, 0.0f));
	BasicGadgetStep(Gadget);
	BasicGadgetQuarterSteps(World, Gadget, 4);
	TestEqual(TEXT("which casts nothing"), Gadget->SkillGadgetCastsMade, 0);
	TestEqual(TEXT("so the enemy beside it keeps its health"), BasicGadgetHealthOf(Enemy), 50000.0f, 0.001f);
	TestEqual(TEXT("and the line above the bar says so"), UCataclysmSkillBar::BasicGadgetTextOf(Rig.Player),
		FString(TEXT("Basic gadget casts nothing")));
	return true;
}

// ==========================================================================
// 5. "Your gadgets prioritize the highest HP enemy" moves a skill gadget's aim (stack 2, item 4; S9)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetMostHealthTest,
	"Cataclysm.SkillGadgets.TheHighestHealthRowAimsASkillGadgetAtTheHealthierEnemyWithinTheSkillsReach")
{
	using namespace CataclysmSkillGadgetBasicTest;

	// A WHIP PLAYER'S HEAVY GADGET 20 M ALONG Y. Within Searing Lash's 5 m: an enemy 2 m along -X holding 20,000 and one
	// 4 m along +X holding 40,000, on opposite sides so the 45-degree cone aimed at one cannot reach the other. Beyond
	// it and within the type row's 15 m: one 7 m along Y holding 50,000.
	FBasicGadgetWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FBasicGadgetRig Rig(World, FVector::ZeroVector);
	FCataclysmWeaponSkill Lash;
	if (!Rig.IsUsable(*this, ECataclysmAbilitySlot::Heavy) || !Rig.HeldRow(ECataclysmAbilitySlot::Heavy, Lash))
	{
		return false;
	}
	const FVector At(0.0f, 20.0f * M, 0.0f);
	Rig.Holds({BasicGadgetConverts(TEXT("Slot.Heavy"))});
	ACataclysmMinion* Gadget = BasicGadgetPlace(*this, Rig, ECataclysmAbilitySlot::Heavy, At);
	ACataclysmEnemyCharacter* Weaker = BasicGadgetEnemy(World, At + FVector(-2.0f * M, 0.0f, 0.0f), 20000.0f);
	ACataclysmEnemyCharacter* Healthier = BasicGadgetEnemy(World, At + FVector(4.0f * M, 0.0f, 0.0f), 40000.0f);
	ACataclysmEnemyCharacter* Beyond = BasicGadgetEnemy(World, At + FVector(0.0f, 7.0f * M, 0.0f), 50000.0f);
	if (!Gadget || !TestTrue(TEXT("set-up: three enemies"), Weaker && Healthier && Beyond))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: without the row the gadget aims at the nearer.
	BasicGadgetStep(Gadget);
	TestEqual(TEXT("control: the gadget casts"), Gadget->SkillGadgetCastsMade, 1);
	const float WeakerAfter = BasicGadgetHealthOf(Weaker);
	TestTrue(TEXT("control: without the row the nearer enemy is struck"), WeakerAfter < 20000.0f);
	TestEqual(TEXT("control: and the healthier one is not"), BasicGadgetHealthOf(Healthier), 40000.0f, 0.001f);

	// THE ROW: a flat 1 on `gadgets_target_most_health` requiring `Type.Deployable`.
	FBasicGadgetRow MostHealth;
	MostHealth.Stat = FName(ACataclysmMinion::GadgetsTargetMostHealthStat);
	MostHealth.Value = 1.0f;
	MostHealth.RequiredTag = TEXT("Type.Deployable");
	Rig.Holds({BasicGadgetConverts(TEXT("Slot.Heavy")), MostHealth});
	TestTrue(TEXT("within the skill's 5 m the most health is the healthier enemy's"),
		Gadget->MostHealthWithin(Gadget->SkillGadgetReachCm(Lash)) == Healthier);
	TestTrue(TEXT("and within the type row's reach it is the one beyond the skill's"),
		Gadget->MostHealthWithinReach() == Beyond);
	BasicGadgetPass(World, 1.5f);
	BasicGadgetStep(Gadget);
	TestEqual(TEXT("its clock run, the gadget casts again"), Gadget->SkillGadgetCastsMade, 2);
	TestTrue(TEXT("with the row the healthier enemy within the skill's reach is struck"),
		BasicGadgetHealthOf(Healthier) < 40000.0f);
	TestEqual(TEXT("and the nearer keeps what it had"), BasicGadgetHealthOf(Weaker), WeakerAfter, 0.001f);
	TestEqual(TEXT("and the healthiest, beyond the skill's reach, is not aimed at"),
		BasicGadgetHealthOf(Beyond), 50000.0f, 0.001f);
	return true;
}

// ==========================================================================
// 6. A rack's axes landing after the use has ended are the gadget's (stack 2, item 3)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetLateLandingTest,
	"Cataclysm.SkillGadgets.ARacksAxesLandingAfterItsUseHasEndedArePricedAsTheGadgets")
{
	using namespace CataclysmSkillGadgetBasicTest;

	// A DEMONIC AXE PLAYER, ITS ULTIMATE BUTCHER'S BILL CONVERTED, holding "Gadgets deal bonus damage equal to 3%-6%
	// of your maximum HP per hit" at 3: 300 a hit of its 10,000, which only a blow priced as the gadget's takes (S5 d).
	// NOT AN INCREASE ROW: an increase naming `Type.Deployable` reaches any blow carrying the grant's tags, priced as
	// the gadget's or not, so it could not tell the two apart. The gadget 20 m along Y with an enemy 3 m from it.
	FBasicGadgetWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FBasicGadgetRig Rig(World, FVector::ZeroVector, TEXT("Demonic"), TEXT("Axe"));
	FCataclysmWeaponSkill Bill;
	if (!Rig.IsUsable(*this, ECataclysmAbilitySlot::Ultimate) || !Rig.HeldRow(ECataclysmAbilitySlot::Ultimate, Bill)
		|| !TestTrue(TEXT("set-up: the axe's Ultimate is a rack of more than two"),
			   Bill.Shape == ECataclysmSkillShape::Projectile && Bill.Params.Count > 2))
	{
		return false;
	}
	FBasicGadgetRow ShareOfHealth;
	ShareOfHealth.Stat = FName(TEXT("attack_damage"));
	ShareOfHealth.Value = 3.0f;
	ShareOfHealth.RequiredTag = TEXT("Type.Deployable");
	ShareOfHealth.Scale = ECataclysmStatScale::PerPointOfMaximumHealth;
	ShareOfHealth.ScaleStep = 100.0f;
	Rig.Holds({BasicGadgetConverts(TEXT("Slot.Ultimate")), ShareOfHealth});
	FBasicGadgetEventCount DeployableHits(Rig.System, TEXT("deployable_hit"));
	const FVector At(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* Gadget = BasicGadgetPlace(*this, Rig, ECataclysmAbilitySlot::Ultimate, At);
	ACataclysmEnemyCharacter* Enemy = BasicGadgetEnemy(World, At + FVector(3.0f * M, 0.0f, 0.0f));
	if (!Gadget || !TestNotNull(TEXT("set-up: an enemy beside the gadget"), Enemy))
	{
		return false;
	}

	// THE CAST: the rack's first axe leaves at once, and its use runs.
	BasicGadgetStep(Gadget);
	UCataclysmProjectileSkill* Rack = BasicGadgetRunningRack(Rig.System, Gadget);
	if (!TestEqual(TEXT("set-up: the gadget casts"), Gadget->SkillGadgetCastsMade, 1)
		|| !TestNotNull(TEXT("set-up: and its rack is running"), Rack))
	{
		return false;
	}
	const int32 Count = Rack->Params.Count;

	// THE CONTROL: AXES LANDING WHILE THE USE RUNS are priced as the gadget's, through the skill they carry.
	TSet<const ACataclysmProjectile*> Done;
	BasicGadgetLandEveryProjectile(World, Done);
	TestEqual(TEXT("control: the first axe, landing while the use runs, raised deployable_hit"), DeployableHits.Count, 1);
	Rack->ThrowOne();
	const float BeforeSecond = BasicGadgetHealthOf(Enemy);
	BasicGadgetLandEveryProjectile(World, Done);
	const float SecondTook = BeforeSecond - BasicGadgetHealthOf(Enemy);
	TestEqual(TEXT("control: and so did the second"), DeployableHits.Count, 2);
	TestTrue(*FString::Printf(TEXT("control: the second axe struck with the flat 300 in it: %.2f"), SecondTook),
		SecondTook > 300.0f);

	// THE REST THROWN, NONE LANDED: the last throw ends the use, and every axe is still in the air.
	const int32 Late = Count - 2;
	for (int32 Each = 0; Each < Late; ++Each)
	{
		Rack->ThrowOne();
	}
	Rack = nullptr;
	TestNull(TEXT("set-up: the last throw ended the rack's use"), BasicGadgetRunningRack(Rig.System, Gadget));
	const float BeforeLate = BasicGadgetHealthOf(Enemy);
	const int32 Landed = BasicGadgetLandEveryProjectile(World, Done);
	const float LateTook = BeforeLate - BasicGadgetHealthOf(Enemy);
	TestEqual(TEXT("set-up: every late axe finished its flight"), Landed, Late);
	TestEqual(*FString::Printf(TEXT("each of the %d axes landing after the use ended raised deployable_hit"), Late),
		DeployableHits.Count, 2 + Late);
	TestEqual(*FString::Printf(TEXT("and took what an axe of the running use took, the flat 300 included: %.2f "
									"against %d times %.2f"), LateTook, Late, SecondTook),
		LateTook, SecondTook * Late, SecondTook * Late * 0.005f);
	return true;
}

// ==========================================================================
// 7. The line above the bar (stack 2, item 6)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetBasicLineTest,
	"Cataclysm.SkillGadgets.TheBasicGadgetsLineAboveTheBarIsDrawnOnlyWhileTheBasicAttackIsConverted")
{
	using namespace CataclysmSkillGadgetBasicTest;

	// THE PURE FUNCTION, EVERY STATE.
	TestTrue(TEXT("not converted, there is no line"),
		UCataclysmSkillBar::BasicGadgetTextFor(false, false, true, 0.4f).IsEmpty());
	TestEqual(TEXT("converted with nothing to cast, it says so"),
		UCataclysmSkillBar::BasicGadgetTextFor(true, true, true, 0.4f), FString(TEXT("Basic gadget casts nothing")));
	TestEqual(TEXT("converted with none placed, it says how to place one"),
		UCataclysmSkillBar::BasicGadgetTextFor(true, false, false, 0.0f),
		FString(TEXT("Basic gadget: click an enemy to place")));
	TestEqual(TEXT("placed and waiting, its seconds as a box writes them"),
		UCataclysmSkillBar::BasicGadgetTextFor(true, false, true, 0.44f), FString(TEXT("Basic gadget 0.4s")));
	TestEqual(TEXT("placed and due, ready"),
		UCataclysmSkillBar::BasicGadgetTextFor(true, false, true, 0.0f), FString(TEXT("Basic gadget ready")));

	// AND READ OFF A PLAYER.
	FBasicGadgetWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FBasicGadgetRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	Rig.Set(UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 1.3f);
	TestTrue(TEXT("control: a player holding no row has no line"), UCataclysmSkillBar::BasicGadgetTextOf(Rig.Player).IsEmpty());
	Rig.Holds({BasicGadgetConverts(TEXT("Slot.Basic"))});
	TestEqual(TEXT("converted with no gadget standing, the line says how to place one"),
		UCataclysmSkillBar::BasicGadgetTextOf(Rig.Player), FString(TEXT("Basic gadget: click an enemy to place")));
	const FGameplayTag BasicTag = CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::BasicAttack);
	Rig.System->PressOfASkillGadgetSlot(BasicTag);
	Rig.System->ReleaseOfASkillGadgetSlot(BasicTag);
	ACataclysmMinion* Gadget = Rig.System->SkillGadgetIn(ECataclysmAbilitySlot::BasicAttack);
	ACataclysmEnemyCharacter* Enemy = BasicGadgetEnemy(World, FVector(2.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the press placed a basic gadget (DT_MinionTypes needs the SkillGadget row)"), Gadget)
		|| !TestNotNull(TEXT("set-up: an enemy beside the gadget"), Enemy))
	{
		return false;
	}
	Rig.StandsAt(FVector(0.0f, -10.0f * M, 0.0f));
	TestEqual(TEXT("placed and not yet cast, ready"), UCataclysmSkillBar::BasicGadgetTextOf(Rig.Player),
		FString(TEXT("Basic gadget ready")));
	BasicGadgetStep(Gadget);
	TestEqual(TEXT("set-up: the gadget casts"), Gadget->SkillGadgetCastsMade, 1);
	TestEqual(TEXT("just after a cast, the whole 0.769 s interval written as 0.8"),
		UCataclysmSkillBar::BasicGadgetTextOf(Rig.Player), FString(TEXT("Basic gadget 0.8s")));
	BasicGadgetPass(World, 1.0f);
	TestEqual(TEXT("a second on, ready again"), UCataclysmSkillBar::BasicGadgetTextOf(Rig.Player),
		FString(TEXT("Basic gadget ready")));
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
