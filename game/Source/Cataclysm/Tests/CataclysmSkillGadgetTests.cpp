// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmFloorContents.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmSkillBar.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Skill gadgets, stack 1: the four keyed slots and the skill bar. Ruled 2026-10-16 by the coordinating session
 * (S1-S14), for "Your heavy attack / special ability / support ability / ultimate ability is converted into a
 * deployable gadget ... Scales off gadget damage." Beside `Cataclysm.GadgetCopiesSkill.`, whose rig this follows.
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER HOLDING A DEMONIC WHIP, given a weapon damage of 100 by hand: Heavy
 * Searing Lash, a 5 m cone; Special Tether, a projectile; Support Coil of Embers, a self buff; Ultimate The Gathering,
 * a 14 m point-blank strike. Two cases hold another weapon: a Demonic Wand, whose Ultimate Anathema is a 12 m
 * point-blank spell, and a War Axe, whose Heavy skill Cleaving Blow has no shape.
 *
 * NO ROW IS AUTHORED, SO EVERY STAT IS GRANTED BY HAND, one stat line per call, replacing the last. The slot's flag is
 * a flat 1 on `skill_slot_is_a_gadget` requiring the slot's tag, which is the row the enchantment session is to write.
 *
 * THE GADGET'S TYPE ROW IS NOT IN THIS BRANCH. `SkillGadget` in `game/Data/MinionTypes.csv` (Family Machine,
 * BaseHealth 200, HealthPerLevel 90, BaseDamage 0, AttackIntervalSeconds 1.0, MoveSpeed 0, ThreatPercent 5, ReachCm
 * 1500, NoticeRadiusCm 1500, TargetMode Nearest, tags `Type.Minion, Type.Deployable, Minion.Machine`) is the
 * enchantment session's, in a data layer below this one; every case that places a gadget says so when it is missing.
 *
 * A CAST IS A FREE START GIVEN A PLACE, so it lands at once and waits for no attack clip; a pressed skill of an
 * unconverted slot does wait for its clip, so what such a press is checked for is what it pays and starts. Actors stand
 * at least 1 m apart, except a gadget the moment it is placed at the player's feet, after which the player is moved.
 * NOTHING HERE WAITS ON A TICK: the world's clock is moved by hand and a gadget's step is called directly.
 */
namespace CataclysmSkillGadgetTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FGadgetTestWorld
	{
		FGadgetTestWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FGadgetTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/** A creature of the monsters' side holding `Health`, with no armour, evasion, block or resistance. It deals nothing. */
	ACataclysmEnemyCharacter* GadgetTestEnemy(UWorld* World, const FVector& Where, float Health = 50000.0f)
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

	float GadgetTestHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** One row of a stat line a test's player holds. */
	struct FGadgetTestRow
	{
		FName Stat;
		float Value = 0.0f;
		ECataclysmStatBucket Bucket = ECataclysmStatBucket::Flat;
		/** The row's one required tag, or null for none. */
		const TCHAR* RequiredTag = nullptr;
		ECataclysmStatCondition Condition = ECataclysmStatCondition::Always;
		ECataclysmStatScale Scale = ECataclysmStatScale::Fixed;
		float ScaleStep = 0.0f;
		int32 ScaleMaxSteps = 0;
	};

	/** The slot's flag: a flat 1 on `skill_slot_is_a_gadget` requiring the slot's tag, the row to be authored. */
	FGadgetTestRow ConvertsSlot(const TCHAR* SlotTag)
	{
		FGadgetTestRow Row;
		Row.Stat = FName(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat);
		Row.Value = 1.0f;
		Row.RequiredTag = SlotTag;
		return Row;
	}

	/** An increase requiring `Type.Deployable`, as every gadget damage row does. */
	FGadgetTestRow GadgetIncrease(const TCHAR* Stat, float Value)
	{
		FGadgetTestRow Row;
		Row.Stat = FName(Stat);
		Row.Value = Value;
		Row.Bucket = ECataclysmStatBucket::Increased;
		Row.RequiredTag = TEXT("Type.Deployable");
		return Row;
	}

	/** A possessed player character holding one weapon, its ability system and its weapon slots. */
	struct FGadgetTestRig
	{
		FGadgetTestRig(UWorld* World, const FVector& Where, const TCHAR* DamageType = TEXT("Demonic"),
					   const TCHAR* WeaponType = TEXT("Whip"))
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
			Slots->SetDamageType(DamageType);
			Slots->EquipWeaponType(WeaponType);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		}

		/** Whether everything was made and the weapon holds a row for `Slot`. Reports why not. */
		bool IsUsable(FAutomationTestBase& Test, ECataclysmAbilitySlot Slot = ECataclysmAbilitySlot::Heavy) const
		{
			FCataclysmWeaponSkill Row;
			if (!Player || !System || !Slots || !HeldRow(Slot, Row))
			{
				Test.AddError(TEXT("set-up: a possessed player holding its weapon and a row for the slot. If a row is "
								   "missing, DT_WeaponSkills may be older than game/Data/WeaponSkills.csv: run "
								   "tools/generate_datatable_assets.py"));
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

		/** REPLACES THE WHOLE STAT LINE with these rows, and these bases for the stats they name. */
		void Holds(const TArray<FGadgetTestRow>& Rows, const TMap<FName, float>& Bases = TMap<FName, float>()) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const TPair<FName, float>& Base : Bases)
			{
				Stats.FindOrAdd(Base.Key).Base = Base.Value;
			}
			for (const FGadgetTestRow& Each : Rows)
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
				Row.Condition = Each.Condition;
				Row.Scale = Each.Scale;
				Row.ScaleStep = Each.ScaleStep;
				Row.ScaleMaxSteps = Each.ScaleMaxSteps;
				FCataclysmStatInputs& Line = Stats.FindOrAdd(Each.Stat);
				if (!Bases.Contains(Each.Stat))
				{
					Line.Base = 0.0f;
				}
				Line.Modifiers.Add(Row);
			}
			System->SetStatInputs(MoveTemp(Stats));
		}

		/** The key of `Slot` goes down: one frame of the press, through the path the controller takes. */
		void KeyDown(ECataclysmAbilitySlot Slot) const
		{
			System->AbilityInputTagPressed(CataclysmAbilitySlots::Tag(Slot));
			System->ProcessAbilityInput();
		}

		/** The key of `Slot` comes up. */
		void KeyUp(ECataclysmAbilitySlot Slot) const
		{
			System->AbilityInputTagReleased(CataclysmAbilitySlots::Tag(Slot));
			System->ProcessAbilityInput();
		}

		/** A press and a release of `Slot`'s key, and the gadget standing for it afterwards, or null. */
		ACataclysmMinion* Taps(ECataclysmAbilitySlot Slot) const
		{
			KeyDown(Slot);
			KeyUp(Slot);
			return System->SkillGadgetIn(Slot);
		}

		/** A free start of this row from the player's own place, at a point: the control cast. */
		bool CastsFromItsOwnPlace(const FCataclysmWeaponSkill& Skill, const FVector& Aim) const
		{
			const FVector Here = Player->GetActorLocation();
			return UCataclysmTriggeredSkill::Trigger(Player, Skill, Aim, /*DamageShare=*/1.0f, /*bPaysCost=*/false,
													 &Here);
		}

		/** Move the player, so it does not stand on a gadget placed at its feet. */
		void StandsAt(const FVector& Where) const
		{
			Player->SetActorLocation(Where, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr,
									 ETeleportType::TeleportPhysics);
		}

		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
	};

	/** A skill gadget for this rig's `Slot` at `Where`, or null with the reason reported. */
	ACataclysmMinion* GadgetTestPlace(FAutomationTestBase& Test, const FGadgetTestRig& Rig, ECataclysmAbilitySlot Slot,
									  const FVector& Where)
	{
		ACataclysmMinion* Made = ACataclysmMinion::PlaceSkillGadget(Rig.Player, Slot, Where);
		if (!Made)
		{
			Test.AddError(TEXT("set-up: no skill gadget was placed. DT_MinionTypes needs the SkillGadget row, which the "
							   "enchantment session authors: run tools/generate_datatable_assets.py"));
		}
		return Made;
	}

	/** Reports a press that placed nothing, for the same reason. */
	bool GadgetTestPlaced(FAutomationTestBase& Test, const ACataclysmMinion* Gadget)
	{
		if (!Gadget)
		{
			Test.AddError(TEXT("set-up: the press placed no skill gadget. DT_MinionTypes needs the SkillGadget row, "
							   "which the enchantment session authors: run tools/generate_datatable_assets.py"));
			return false;
		}
		return true;
	}

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FGadgetTestEventCount
	{
		FGadgetTestEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
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
		~FGadgetTestEventCount()
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
	void GadgetTestPass(UWorld* World, float Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/** Run the gadget's quarter-second step, as `RegenerationStep` calls it. */
	void GadgetTestStep(ACataclysmMinion* Gadget)
	{
		ACataclysmMinion::GadgetStep(Gadget);
	}

	/** How many skill-used notices the world has sent. */
	uint32 GadgetTestNoticesSent(UWorld* World)
	{
		const UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
		return Events ? Events->SkillUsesSent() : 0;
	}

	/** The clock's length of a gadget, nought before its first cast. */
	float GadgetTestClockLength(const ACataclysmMinion* Gadget)
	{
		float Remaining = 0.0f;
		float Duration = 0.0f;
		Gadget->SkillGadgetClock(Remaining, Duration);
		return Duration;
	}

	/** The skill bar's box for `Slot`, or an empty box with `None` for its slot. */
	FCataclysmSkillBarSlot GadgetTestBox(const AActor* Player, ECataclysmAbilitySlot Slot)
	{
		for (const FCataclysmSkillBarSlot& Box : UCataclysmSkillBar::Read(Player))
		{
			if (Box.Slot == Slot)
			{
				return Box;
			}
		}
		return FCataclysmSkillBarSlot();
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 1. The press places a gadget and casts nothing (S7)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetPressPlacesTest,
	"Cataclysm.SkillGadgets.APressPlacesAGadgetAtThePlayersFeetAndCastsNothingFromThePlayer")
{
	using namespace CataclysmSkillGadgetTest;

	// TWO PLAYERS 40 M APART: THE CONTROL HOLDS NO ROW, THE OTHER HOLDS THE HEAVY SLOT'S FLAG.
	FGadgetTestWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FGadgetTestRig Plain(World, FVector::ZeroVector);
	FGadgetTestRig Rig(World, FVector(40.0f * M, 0.0f, 0.0f));
	if (!Plain.IsUsable(*this) || !Rig.IsUsable(*this))
	{
		return false;
	}
	const FGameplayAttribute Mana = UCataclysmVitalAttributeSet::GetManaAttribute();
	const FGameplayTag HeavyCooldown = UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Heavy);

	// THE CONTROL, READ FIRST: the press of a player holding no row casts the skill, which pays and starts the cooldown.
	Plain.KeyDown(ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("control: the Heavy press of a player holding no row starts the Heavy cooldown"),
		Plain.System->HasMatchingGameplayTag(HeavyCooldown));
	TestTrue(TEXT("control: and pays mana"), Plain.Get(Mana) < 1000.0f);
	TestNull(TEXT("control: and no gadget stands for its Heavy slot"),
		Plain.System->SkillGadgetIn(ECataclysmAbilitySlot::Heavy));
	Plain.KeyUp(ECataclysmAbilitySlot::Heavy);

	// THE CONVERTED PRESS.
	Rig.Holds({ConvertsSlot(TEXT("Slot.Heavy"))});
	FGadgetTestEventCount SkillUses(Rig.System, TEXT("skill_use"));
	const uint32 NoticesBefore = GadgetTestNoticesSent(World);
	const FVector Feet = Rig.Player->GetActorLocation();
	Rig.KeyDown(ECataclysmAbilitySlot::Heavy);
	ACataclysmMinion* Gadget = Rig.System->SkillGadgetIn(ECataclysmAbilitySlot::Heavy);
	if (!GadgetTestPlaced(*this, Gadget))
	{
		return false;
	}
	TestTrue(TEXT("the press placed a skill gadget for the Heavy slot"),
		Gadget->IsSkillGadget() && Gadget->SkillGadgetSlot == ECataclysmAbilitySlot::Heavy);
	TestTrue(*FString::Printf(TEXT("at the player's feet: %.3f cm away"), FVector::Dist(Gadget->GetActorLocation(), Feet)),
		FVector::Dist(Gadget->GetActorLocation(), Feet) < 1.0f);
	TestEqual(TEXT("of the SkillGadget type"), Gadget->TypeName, FString(ACataclysmMinion::SkillGadgetTypeName));
	TestTrue(TEXT("outside every summon cap"), Gadget->bOutsideSummonCaps);
	TestEqual(TEXT("with no lifespan"), Gadget->GetLifeSpan(), 0.0f, 0.0001f);
	TestEqual(TEXT("the player's mana is unchanged"), Rig.Get(Mana), 1000.0f, 0.001f);
	TestFalse(TEXT("no Heavy cooldown runs"), Rig.System->HasMatchingGameplayTag(HeavyCooldown));
	TestEqual(TEXT("no skill_use was raised"), SkillUses.Count, 0);
	TestEqual(TEXT("and no skill-used notice was sent"), GadgetTestNoticesSent(World), NoticesBefore);
	TestEqual(TEXT("and the gadget cast nothing as it was placed"), Gadget->SkillGadgetCastsMade, 0);

	// THE KEY HELD FOR ANOTHER FRAME, with the player moved: the same press places nothing more and moves nothing.
	Rig.StandsAt(Feet + FVector(0.0f, 5.0f * M, 0.0f));
	Rig.KeyDown(ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("a frame of the same press keeps the same gadget"),
		Rig.System->SkillGadgetIn(ECataclysmAbilitySlot::Heavy) == Gadget);
	TestTrue(TEXT("and does not move it"), FVector::Dist(Gadget->GetActorLocation(), Feet) < 1.0f);
	Rig.KeyUp(ECataclysmAbilitySlot::Heavy);
	return true;
}

// ==========================================================================
// 2. A second press moves it (S6)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetSecondPressMovesTest,
	"Cataclysm.SkillGadgets.ASecondPressMovesTheSameGadgetKeepsItsHealthAndDoesNotCastAtOnce")
{
	using namespace CataclysmSkillGadgetTest;

	// THE PLAYER AT THE ORIGIN WITH AN ENEMY 3 M ALONG X, AND A SECOND ENEMY 3 M ALONG X FROM THE PLACE 10 M ALONG Y.
	FGadgetTestWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FGadgetTestRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	const FVector Second(0.0f, 10.0f * M, 0.0f);
	ACataclysmEnemyCharacter* First = GadgetTestEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Later = GadgetTestEnemy(World, Second + FVector(3.0f * M, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: two enemies"), First && Later))
	{
		return false;
	}
	Rig.Holds({ConvertsSlot(TEXT("Slot.Heavy"))});
	ACataclysmMinion* Gadget = Rig.Taps(ECataclysmAbilitySlot::Heavy);
	if (!GadgetTestPlaced(*this, Gadget))
	{
		return false;
	}
	Rig.StandsAt(FVector(0.0f, -10.0f * M, 0.0f));

	// THE CONTROL: the gadget casts on its first step with an enemy in reach, and an enemy's blow costs it health.
	GadgetTestStep(Gadget);
	TestEqual(TEXT("control: the gadget casts once on its first step"), Gadget->SkillGadgetCastsMade, 1);
	TestTrue(TEXT("control: and strikes the enemy beside it"), GadgetTestHealthOf(First) < 50000.0f);
	UCataclysmSkillEffects::ApplyDirectDamage(First, Gadget, 50.0f);
	const float Struck = GadgetTestHealthOf(Gadget);
	TestTrue(*FString::Printf(TEXT("control: a blow of 50 cost the gadget health: %.2f left"), Struck),
		Struck > 0.0f && Struck < Gadget->GetAbilitySystemComponent()->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute()));

	// THE SECOND PRESS, FROM 10 M ALONG Y.
	Rig.StandsAt(Second);
	ACataclysmMinion* Moved = Rig.Taps(ECataclysmAbilitySlot::Heavy);
	Rig.StandsAt(FVector(0.0f, -10.0f * M, 0.0f));
	TestTrue(TEXT("the second press keeps the same gadget"), Moved == Gadget);
	TestTrue(*FString::Printf(TEXT("and moves it to where the player stood: %.3f cm away"),
							  FVector::Dist(Gadget->GetActorLocation(), Second)),
		FVector::Dist(Gadget->GetActorLocation(), Second) < 1.0f);
	TestEqual(TEXT("with its health kept"), GadgetTestHealthOf(Gadget), Struck, 0.001f);
	TestFalse(TEXT("and alive"), UCataclysmSkillEffects::IsDead(Gadget));

	// AND ITS CLOCK KEPT: a step at once, with an enemy within reach of the new place, casts nothing.
	GadgetTestStep(Gadget);
	TestEqual(TEXT("a step just after the move casts nothing: the clock is kept"), Gadget->SkillGadgetCastsMade, 1);
	TestEqual(TEXT("and the enemy by the new place keeps its health"), GadgetTestHealthOf(Later), 50000.0f, 0.001f);
	GadgetTestPass(World, 1.5f);
	GadgetTestStep(Gadget);
	TestEqual(TEXT("its clock run, the next step casts from the new place"), Gadget->SkillGadgetCastsMade, 2);
	TestTrue(TEXT("and strikes the enemy there"), GadgetTestHealthOf(Later) < 50000.0f);
	return true;
}

// ==========================================================================
// 3. One gadget per converted slot (S6, S7)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetOnePerSlotTest,
	"Cataclysm.SkillGadgets.TwoConvertedSlotsGiveTwoGadgetsAndAnUnconvertedSlotStillCasts")
{
	using namespace CataclysmSkillGadgetTest;

	FGadgetTestWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FGadgetTestRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this) || !Rig.IsUsable(*this, ECataclysmAbilitySlot::Ultimate))
	{
		return false;
	}
	const FGameplayTag SpecialCooldown = UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Special);

	// THE CONTROL, READ FIRST: holding no row, no slot has a gadget.
	TestNull(TEXT("control: holding no row, no gadget stands for the Heavy slot"),
		Rig.System->SkillGadgetIn(ECataclysmAbilitySlot::Heavy));
	TestFalse(TEXT("control: and the Heavy slot is not converted"),
		Rig.System->SlotIsASkillGadget(ECataclysmAbilitySlot::Heavy));

	Rig.Holds({ConvertsSlot(TEXT("Slot.Heavy")), ConvertsSlot(TEXT("Slot.Ultimate"))});
	TestFalse(TEXT("the Special slot, which no row names, is not converted"),
		Rig.System->SlotIsASkillGadget(ECataclysmAbilitySlot::Special));
	ACataclysmMinion* Heavy = Rig.Taps(ECataclysmAbilitySlot::Heavy);
	Rig.StandsAt(FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmMinion* Ultimate = Rig.Taps(ECataclysmAbilitySlot::Ultimate);
	Rig.StandsAt(FVector(6.0f * M, 0.0f, 0.0f));
	if (!GadgetTestPlaced(*this, Heavy) || !GadgetTestPlaced(*this, Ultimate))
	{
		return false;
	}
	TestTrue(TEXT("two converted slots give two gadgets"), Heavy != Ultimate);
	TestTrue(TEXT("each recorded for its own slot"),
		Rig.System->SkillGadgetIn(ECataclysmAbilitySlot::Heavy) == Heavy
		&& Rig.System->SkillGadgetIn(ECataclysmAbilitySlot::Ultimate) == Ultimate
		&& Heavy->SkillGadgetSlot == ECataclysmAbilitySlot::Heavy
		&& Ultimate->SkillGadgetSlot == ECataclysmAbilitySlot::Ultimate);
	TestTrue(TEXT("a second Heavy press moves the Heavy gadget and leaves the Ultimate one where it was"),
		Rig.Taps(ECataclysmAbilitySlot::Heavy) == Heavy
		&& FVector::Dist(Heavy->GetActorLocation(), Rig.Player->GetActorLocation()) < 1.0f
		&& FVector::Dist(Ultimate->GetActorLocation(), FVector(3.0f * M, 0.0f, 0.0f)) < 1.0f);

	// AND THE UNCONVERTED SPECIAL SLOT'S PRESS STILL CASTS ITS SKILL, which pays and starts its cooldown.
	const float ManaBefore = Rig.Get(UCataclysmVitalAttributeSet::GetManaAttribute());
	Rig.KeyDown(ECataclysmAbilitySlot::Special);
	TestTrue(TEXT("the unconverted Special press starts the Special cooldown"),
		Rig.System->HasMatchingGameplayTag(SpecialCooldown));
	TestTrue(TEXT("and pays mana"), Rig.Get(UCataclysmVitalAttributeSet::GetManaAttribute()) < ManaBefore);
	TestNull(TEXT("and places no gadget"), Rig.System->SkillGadgetIn(ECataclysmAbilitySlot::Special));
	Rig.KeyUp(ECataclysmAbilitySlot::Special);
	return true;
}

// ==========================================================================
// 4. The cast: the player's skill, from the gadget, at the nearest enemy within the skill's reach (S1, S4)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetCastsTest,
	"Cataclysm.SkillGadgets.TheGadgetCastsThePlayersSkillAtTheNearestEnemyWithinTheSkillsReachOncePerItsClock")
{
	using namespace CataclysmSkillGadgetTest;

	// THE PLAYER AT THE ORIGIN WITH A CONTROL ENEMY 3 M ALONG X; ITS HEAVY GADGET 20 M ALONG Y. AN ENEMY 6 M FROM THE
	// GADGET, beyond Searing Lash's 5 m and within the type row's 15 m, then one 3 m from it.
	FGadgetTestWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FGadgetTestRig Rig(World, FVector::ZeroVector);
	FCataclysmWeaponSkill Lash;
	if (!Rig.IsUsable(*this) || !Rig.HeldRow(ECataclysmAbilitySlot::Heavy, Lash))
	{
		return false;
	}
	const FVector At(0.0f, 20.0f * M, 0.0f);
	Rig.Holds({ConvertsSlot(TEXT("Slot.Heavy"))});
	ACataclysmMinion* Gadget = GadgetTestPlace(*this, Rig, ECataclysmAbilitySlot::Heavy, At);
	ACataclysmEnemyCharacter* Far = GadgetTestEnemy(World, At + FVector(-6.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Control = GadgetTestEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	if (!Gadget || !TestTrue(TEXT("set-up: two enemies"), Far && Control))
	{
		return false;
	}
	TestTrue(*FString::Printf(TEXT("set-up: the gadget aims by Searing Lash's own reach: %.1f cm"),
							  Gadget->SkillGadgetReachCm(Lash)),
		FMath::IsNearlyEqual(Gadget->SkillGadgetReachCm(Lash), 5.0f * M, 0.01f));

	// ONLY AN ENEMY BEYOND THE SKILL'S REACH: nothing is cast.
	GadgetTestStep(Gadget);
	TestEqual(TEXT("an enemy 6 m away, beyond the skill's 5 m, is not aimed at: no cast"), Gadget->SkillGadgetCastsMade, 0);
	TestEqual(TEXT("and it keeps its health"), GadgetTestHealthOf(Far), 50000.0f, 0.001f);

	// THE CONTROL: the player's own free cast of Searing Lash from its own place at the control enemy.
	if (!TestTrue(TEXT("set-up: the player's own cast starts"),
			Rig.CastsFromItsOwnPlace(Lash, Control->GetActorLocation())))
	{
		return false;
	}
	const float OwnTook = 50000.0f - GadgetTestHealthOf(Control);
	TestTrue(*FString::Printf(TEXT("control: the player's own cast struck: %.2f"), OwnTook), OwnTook > 0.0f);

	// AN ENEMY 3 M FROM THE GADGET.
	ACataclysmEnemyCharacter* Near = GadgetTestEnemy(World, At + FVector(3.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: an enemy beside the gadget"), Near))
	{
		return false;
	}
	GadgetTestStep(Gadget);
	TestEqual(TEXT("with an enemy within reach the gadget casts once"), Gadget->SkillGadgetCastsMade, 1);
	const float Took = 50000.0f - GadgetTestHealthOf(Near);
	TestEqual(TEXT("the cast took what the player's own cast takes: the player's skill and numbers"), Took, OwnTook, 0.01f);
	TestEqual(TEXT("the enemy beyond the reach is still untouched"), GadgetTestHealthOf(Far), 50000.0f, 0.001f);
	TestEqual(TEXT("and the cast was not the gadget's own blow"), Gadget->AttacksMade, 0);

	// ONCE PER ITS CLOCK: the Heavy slot's 1.5 s, the row stating none.
	const float Base = UCataclysmSkillSlots::NumbersFor(
		UCataclysmSkillSlots::LoadGeneratedTable(), ECataclysmAbilitySlot::Heavy).Cooldown;
	TestEqual(TEXT("the clock is the Heavy slot's cooldown"), GadgetTestClockLength(Gadget), Base, 0.001f);
	GadgetTestPass(World, Base - 0.1f);
	GadgetTestStep(Gadget);
	TestEqual(TEXT("a tenth of a second short of the clock, nothing"), Gadget->SkillGadgetCastsMade, 1);
	GadgetTestPass(World, 0.1f);
	GadgetTestStep(Gadget);
	TestEqual(TEXT("at the clock, the second cast"), Gadget->SkillGadgetCastsMade, 2);
	return true;
}

// ==========================================================================
// 5. The clock: after the cooldown rows, and shortened by the fire-rate row (S2, S3)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetClockTest,
	"Cataclysm.SkillGadgets.TheClockIsTheSlotsCooldownAfterItsRowsAndTheFireRateRowShortensIt")
{
	using namespace CataclysmSkillGadgetTest;

	FGadgetTestWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FGadgetTestRig Rig(World, FVector::ZeroVector);
	FCataclysmWeaponSkill Lash;
	if (!Rig.IsUsable(*this) || !Rig.HeldRow(ECataclysmAbilitySlot::Heavy, Lash))
	{
		return false;
	}
	const float Base = UCataclysmSkillSlots::NumbersFor(
		UCataclysmSkillSlots::LoadGeneratedTable(), ECataclysmAbilitySlot::Heavy).Cooldown;
	FGadgetTestRow Reduction;
	Reduction.Stat = FName(TEXT("cooldown_reduction"));
	Reduction.Value = 50.0f;

	// THE CONTROL: the flat 50 of cooldown reduction shortens the player's own Heavy cooldown.
	Rig.Holds({ConvertsSlot(TEXT("Slot.Heavy")), Reduction});
	const float AfterRows = UCataclysmGameplayAbility::CooldownAfterReduction(Rig.System, Base, Lash.Tags);
	TestTrue(*FString::Printf(TEXT("control: the reduction row shortens the Heavy cooldown from %.3f to %.3f"),
							  Base, AfterRows),
		AfterRows > 0.0f && AfterRows < Base - 0.01f);

	const FVector At(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* Gadget = GadgetTestPlace(*this, Rig, ECataclysmAbilitySlot::Heavy, At);
	ACataclysmEnemyCharacter* Near = GadgetTestEnemy(World, At + FVector(3.0f * M, 0.0f, 0.0f));
	if (!Gadget || !TestNotNull(TEXT("set-up: an enemy beside the gadget"), Near))
	{
		return false;
	}
	GadgetTestStep(Gadget);
	if (!TestEqual(TEXT("set-up: the first cast"), Gadget->SkillGadgetCastsMade, 1))
	{
		return false;
	}
	TestEqual(TEXT("the gadget's clock is the slot's cooldown after the reduction row"),
		GadgetTestClockLength(Gadget), AfterRows, 0.001f);
	GadgetTestPass(World, AfterRows - 0.05f);
	GadgetTestStep(Gadget);
	TestEqual(TEXT("short of it by a twentieth of a second, nothing"), Gadget->SkillGadgetCastsMade, 1);
	GadgetTestPass(World, 0.05f);
	GadgetTestStep(Gadget);
	TestEqual(TEXT("at it, the second cast"), Gadget->SkillGadgetCastsMade, 2);

	// AND "Gadgets fire 20%-40% faster", AT 50, SHORTENS IT: divided by one and a half.
	Rig.Holds({ConvertsSlot(TEXT("Slot.Heavy")), Reduction, GadgetIncrease(TEXT("minion_attack_speed"), 50.0f)});
	const float Faster = AfterRows / 1.5f;
	TestEqual(TEXT("the fire-rate row shortens the clock to the reduced cooldown over one and a half"),
		GadgetTestClockLength(Gadget), Faster, 0.001f);
	GadgetTestPass(World, Faster - 0.05f);
	GadgetTestStep(Gadget);
	TestEqual(TEXT("short of the shorter clock, nothing"), Gadget->SkillGadgetCastsMade, 2);
	GadgetTestPass(World, 0.05f);
	GadgetTestStep(Gadget);
	TestEqual(TEXT("at it, the third cast"), Gadget->SkillGadgetCastsMade, 3);
	return true;
}

// ==========================================================================
// 6. Mana Weaver's nought does not reach the Ultimate gadget's clock (S2)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetManaWeaverTest,
	"Cataclysm.SkillGadgets.ManaWeaversTenPieceLeavesTheUltimateGadgetWaitingItsBaseCooldown")
{
	using namespace CataclysmSkillGadgetTest;

	FGadgetTestWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FGadgetTestRig Rig(World, FVector::ZeroVector);
	FCataclysmWeaponSkill Gathering;
	if (!Rig.IsUsable(*this, ECataclysmAbilitySlot::Ultimate)
		|| !Rig.HeldRow(ECataclysmAbilitySlot::Ultimate, Gathering))
	{
		return false;
	}
	FGadgetTestRow TenPiece;
	TenPiece.Stat = FName(UCataclysmGameplayAbility::UltimateCostDoublesInsteadOfCooldownStat);
	TenPiece.Value = 1.0f;
	Rig.Holds({ConvertsSlot(TEXT("Slot.Ultimate")), TenPiece});

	// THE CONTROL: the ten-piece flag is live for the player's own Ultimate, whose base cooldown it makes nought.
	const UCataclysmGameplayAbility* HeldUltimate = nullptr;
	for (const FGameplayAbilitySpec& Spec : Rig.System->GetActivatableAbilities())
	{
		if (Spec.GetDynamicSpecSourceTags().HasTagExact(CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::Ultimate)))
		{
			HeldUltimate = Cast<UCataclysmGameplayAbility>(Spec.GetPrimaryInstance());
		}
	}
	if (!TestNotNull(TEXT("set-up: the player's Ultimate is granted"), HeldUltimate))
	{
		return false;
	}
	TestTrue(TEXT("control: Mana Weaver's ten-piece flag holds for the player's own Ultimate"),
		HeldUltimate->UltimateCostDoublesByARow(Rig.System));

	const float Base = UCataclysmSkillSlots::NumbersFor(
		UCataclysmSkillSlots::LoadGeneratedTable(), ECataclysmAbilitySlot::Ultimate).Cooldown;
	const FVector At(0.0f, 30.0f * M, 0.0f);
	ACataclysmMinion* Gadget = GadgetTestPlace(*this, Rig, ECataclysmAbilitySlot::Ultimate, At);
	ACataclysmEnemyCharacter* Near = GadgetTestEnemy(World, At + FVector(4.0f * M, 0.0f, 0.0f));
	if (!Gadget || !TestNotNull(TEXT("set-up: an enemy beside the gadget"), Near))
	{
		return false;
	}
	GadgetTestStep(Gadget);
	if (!TestEqual(TEXT("set-up: the first cast"), Gadget->SkillGadgetCastsMade, 1))
	{
		return false;
	}
	TestTrue(*FString::Printf(TEXT("the Ultimate gadget's clock is the Ultimate slot's %.1f s and not nought: %.3f"),
							  Base, GadgetTestClockLength(Gadget)),
		Base > 0.0f && FMath::IsNearlyEqual(GadgetTestClockLength(Gadget), Base, 0.001f));
	GadgetTestPass(World, Base - 0.1f);
	GadgetTestStep(Gadget);
	TestEqual(TEXT("a tenth of a second short of it, nothing"), Gadget->SkillGadgetCastsMade, 1);
	GadgetTestPass(World, 0.1f);
	GadgetTestStep(Gadget);
	TestEqual(TEXT("at it, the second cast"), Gadget->SkillGadgetCastsMade, 2);
	return true;
}

// ==========================================================================
// 7. "Scales off gadget damage": the 20%-40% row, once on an attack and once on a spell (S5, S5 a)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetDamageRowTest,
	"Cataclysm.SkillGadgets.AGadgetDamageRowRaisesTheCastAndCountsOnceOnASpell")
{
	using namespace CataclysmSkillGadgetTest;

	// A WHIP PLAYER AT THE ORIGIN AND A WAND PLAYER 100 M ALONG X, each with a control enemy 3 m along X and its gadget
	// 30 M ALONG Y with a target 3 m along X from it.
	FGadgetTestWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FGadgetTestRig Whip(World, FVector::ZeroVector);
	const FVector WandHome(100.0f * M, 0.0f, 0.0f);
	FGadgetTestRig Wand(World, WandHome, TEXT("Demonic"), TEXT("Wand"));
	FCataclysmWeaponSkill Lash;
	FCataclysmWeaponSkill Anathema;
	if (!Whip.IsUsable(*this) || !Wand.IsUsable(*this, ECataclysmAbilitySlot::Ultimate)
		|| !Whip.HeldRow(ECataclysmAbilitySlot::Heavy, Lash)
		|| !Wand.HeldRow(ECataclysmAbilitySlot::Ultimate, Anathema))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the wand's Ultimate is a spell and Searing Lash is not"),
			UCataclysmSkillEffects::IsSpell(Anathema.Tags) && !UCataclysmSkillEffects::IsSpell(Lash.Tags)))
	{
		return false;
	}
	const TArray<FGadgetTestRow> TwentyToForty = {
		GadgetIncrease(TEXT("attack_damage"), 30.0f), GadgetIncrease(TEXT("spell_damage"), 30.0f)};

	// THE ATTACK.
	Whip.Holds({ConvertsSlot(TEXT("Slot.Heavy")), TwentyToForty[0], TwentyToForty[1]});
	ACataclysmEnemyCharacter* WhipControl = GadgetTestEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	const FVector WhipAt(0.0f, 30.0f * M, 0.0f);
	ACataclysmMinion* WhipGadget = GadgetTestPlace(*this, Whip, ECataclysmAbilitySlot::Heavy, WhipAt);
	ACataclysmEnemyCharacter* WhipTarget = GadgetTestEnemy(World, WhipAt + FVector(3.0f * M, 0.0f, 0.0f));
	if (!WhipGadget || !TestTrue(TEXT("set-up: two enemies"), WhipControl && WhipTarget))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the player's own cast starts"),
			Whip.CastsFromItsOwnPlace(Lash, WhipControl->GetActorLocation())))
	{
		return false;
	}
	const float OwnAttack = 50000.0f - GadgetTestHealthOf(WhipControl);
	TestTrue(*FString::Printf(TEXT("control: the player's own cast, which the gadget rows do not reach: %.2f"), OwnAttack),
		OwnAttack > 0.0f);
	GadgetTestStep(WhipGadget);
	const float GadgetAttack = 50000.0f - GadgetTestHealthOf(WhipTarget);
	TestEqual(*FString::Printf(TEXT("the gadget's cast of the attack takes 30%% more: %.2f against %.2f"),
							   GadgetAttack, OwnAttack),
		GadgetAttack, OwnAttack * 1.3f, OwnAttack * 0.001f);

	// THE SPELL, WITH A SPELL DAMAGE OF 50: the row counts once, so the whole blow is 30% more and not more than that.
	Wand.Holds({ConvertsSlot(TEXT("Slot.Ultimate")), TwentyToForty[0], TwentyToForty[1]},
			   {{FName(TEXT("spell_damage")), 50.0f}});
	ACataclysmEnemyCharacter* WandControl = GadgetTestEnemy(World, WandHome + FVector(3.0f * M, 0.0f, 0.0f));
	const FVector WandAt = WandHome + FVector(0.0f, 30.0f * M, 0.0f);
	ACataclysmMinion* WandGadget = GadgetTestPlace(*this, Wand, ECataclysmAbilitySlot::Ultimate, WandAt);
	ACataclysmEnemyCharacter* WandTarget = GadgetTestEnemy(World, WandAt + FVector(3.0f * M, 0.0f, 0.0f));
	if (!WandGadget || !TestTrue(TEXT("set-up: two enemies by the wand player"), WandControl && WandTarget))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the wand player's own cast starts"),
			Wand.CastsFromItsOwnPlace(Anathema, WandControl->GetActorLocation())))
	{
		return false;
	}
	const float OwnSpell = 50000.0f - GadgetTestHealthOf(WandControl);
	TestTrue(*FString::Printf(TEXT("control: the wand player's own Anathema: %.2f"), OwnSpell), OwnSpell > 0.0f);
	GadgetTestStep(WandGadget);
	const float GadgetSpell = 50000.0f - GadgetTestHealthOf(WandTarget);
	TestEqual(*FString::Printf(TEXT("the gadget's Anathema takes 30%% more and no more, the spell damage row naming "
									"gadgets left out of the spell part: %.2f against %.2f"),
							   GadgetSpell, OwnSpell),
		GadgetSpell, OwnSpell * 1.3f, OwnSpell * 0.001f);
	return true;
}

// ==========================================================================
// 8. The gadget's age and stillness reach the conditions (S5 b, c)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetAgeAndStillnessTest,
	"Cataclysm.SkillGadgets.TheAgeRowReadsTheGadgetsAgeAndStillnessIsJudgedOnTheGadget")
{
	using namespace CataclysmSkillGadgetTest;

	FGadgetTestWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FGadgetTestRig Rig(World, FVector::ZeroVector);
	FCataclysmWeaponSkill Lash;
	if (!Rig.IsUsable(*this) || !Rig.HeldRow(ECataclysmAbilitySlot::Heavy, Lash))
	{
		return false;
	}

	// "Gadgets deal 5%-10% increased damage for each second active, up to 30", AT 10.
	FGadgetTestRow PerSecond = GadgetIncrease(TEXT("attack_damage"), 10.0f);
	PerSecond.Scale = ECataclysmStatScale::PerSecondTheMinionHasBeenActive;
	PerSecond.ScaleStep = 1.0f;
	PerSecond.ScaleMaxSteps = 30;
	Rig.Holds({ConvertsSlot(TEXT("Slot.Heavy")), PerSecond});

	ACataclysmEnemyCharacter* Control = GadgetTestEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	const FVector At(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* Gadget = GadgetTestPlace(*this, Rig, ECataclysmAbilitySlot::Heavy, At);
	ACataclysmEnemyCharacter* Target = GadgetTestEnemy(World, At + FVector(3.0f * M, 0.0f, 0.0f));
	if (!Gadget || !TestTrue(TEXT("set-up: two enemies"), Control && Target)
		|| !TestTrue(TEXT("set-up: the player's own cast starts"),
			   Rig.CastsFromItsOwnPlace(Lash, Control->GetActorLocation())))
	{
		return false;
	}
	const float OwnTook = 50000.0f - GadgetTestHealthOf(Control);
	TestTrue(*FString::Printf(TEXT("control: the player's own cast: %.2f"), OwnTook), OwnTook > 0.0f);

	// THREE AND A HALF SECONDS STOOD, THREE WHOLE ONES: 30% more, which an age of nought would not give.
	GadgetTestPass(World, 3.5f);
	GadgetTestStep(Gadget);
	const float AtThree = 50000.0f - GadgetTestHealthOf(Target);
	TestEqual(*FString::Printf(TEXT("a gadget that has stood 3 seconds casts for 30%% more: %.2f against %.2f"),
							   AtThree, OwnTook),
		AtThree, OwnTook * 1.3f, OwnTook * 0.001f);

	// "While stationary, your gadgets deal 20%-40% increased damage", AT 30, WITH THE PLAYER MOVING.
	FGadgetTestRow WhileStill = GadgetIncrease(TEXT("attack_damage"), 30.0f);
	WhileStill.Condition = ECataclysmStatCondition::WhileStationary;
	Rig.Holds({ConvertsSlot(TEXT("Slot.Heavy")), WhileStill});
	Rig.System->NoteMovedMetres(1.0f);
	TestTrue(TEXT("control: the player is moving"), Rig.System->IsMoving());
	GadgetTestPass(World, 1.5f);
	const float Before = GadgetTestHealthOf(Target);
	GadgetTestStep(Gadget);
	const float WhileMoving = Before - GadgetTestHealthOf(Target);
	TestEqual(*FString::Printf(TEXT("the stationary row holds for the still gadget while the player moves: %.2f "
									"against %.2f"), WhileMoving, OwnTook),
		WhileMoving, OwnTook * 1.3f, OwnTook * 0.001f);
	return true;
}

// ==========================================================================
// 9. The flat maximum-health row and `deployable_hit` (S5 d, e)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetFlatAndHitTest,
	"Cataclysm.SkillGadgets.TheFlatMaximumHealthRowAppliesAndTheCastRaisesDeployableHit")
{
	using namespace CataclysmSkillGadgetTest;

	FGadgetTestWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FGadgetTestRig Rig(World, FVector::ZeroVector);
	FCataclysmWeaponSkill Lash;
	if (!Rig.IsUsable(*this) || !Rig.HeldRow(ECataclysmAbilitySlot::Heavy, Lash))
	{
		return false;
	}

	// "Gadgets deal bonus damage equal to 3%-6% of your maximum HP per hit", AT 3: 3 for every 100 of the player's
	// 10,000, which is 300.
	FGadgetTestRow ShareOfHealth;
	ShareOfHealth.Stat = FName(TEXT("attack_damage"));
	ShareOfHealth.Value = 3.0f;
	ShareOfHealth.RequiredTag = TEXT("Type.Deployable");
	ShareOfHealth.Scale = ECataclysmStatScale::PerPointOfMaximumHealth;
	ShareOfHealth.ScaleStep = 100.0f;
	Rig.Holds({ConvertsSlot(TEXT("Slot.Heavy")), ShareOfHealth});
	FGadgetTestEventCount DeployableHits(Rig.System, TEXT("deployable_hit"));

	ACataclysmEnemyCharacter* Control = GadgetTestEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	const FVector At(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* Gadget = GadgetTestPlace(*this, Rig, ECataclysmAbilitySlot::Heavy, At);
	ACataclysmEnemyCharacter* Target = GadgetTestEnemy(World, At + FVector(3.0f * M, 0.0f, 0.0f));
	if (!Gadget || !TestTrue(TEXT("set-up: two enemies"), Control && Target)
		|| !TestTrue(TEXT("set-up: the player's own cast starts"),
			   Rig.CastsFromItsOwnPlace(Lash, Control->GetActorLocation())))
	{
		return false;
	}
	const float OwnTook = 50000.0f - GadgetTestHealthOf(Control);
	TestTrue(*FString::Printf(TEXT("control: the player's own cast: %.2f"), OwnTook), OwnTook > 0.0f);
	TestEqual(TEXT("control: and it raised no deployable_hit"), DeployableHits.Count, 0);

	GadgetTestStep(Gadget);
	const float Took = 50000.0f - GadgetTestHealthOf(Target);
	// THE FLAT 300 TAKES THE WEAPON SUB-TYPE'S BONUS AS THE REST OF THE BLOW DOES. The control's own cast is Searing
	// Lash's 250% of 100 times the whip's Slashing sub-type (+10% to health), so the factor is read back from it,
	// OwnTook over 250. Ruled 2026-10-16 after window g1 measured 330, as row B's store test was ruled.
	const float SubTypeFactor = OwnTook / 250.0f;
	TestEqual(*FString::Printf(TEXT("the gadget's cast takes 300 more, 3%% of the player's 10,000 maximum, times the "
									"weapon sub-type's factor %.3f: %.2f against %.2f"), SubTypeFactor, Took, OwnTook),
		Took - OwnTook, 300.0f * SubTypeFactor, 0.5f);
	TestEqual(TEXT("and its one blow raised deployable_hit once"), DeployableHits.Count, 1);
	return true;
}

// ==========================================================================
// 10. A lock refuses the cast and not the placing (S7)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetLockTest,
	"Cataclysm.SkillGadgets.PlacingUnderTheSlotLockSucceedsAndTheCastIsRefused")
{
	using namespace CataclysmSkillGadgetTest;

	// TWO PLAYERS 60 M APART, EACH WITH AN ENEMY 3 M FROM WHERE ITS GADGET WILL STAND.
	FGadgetTestWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FGadgetTestRig Free(World, FVector::ZeroVector);
	const FVector LockedHome(60.0f * M, 0.0f, 0.0f);
	FGadgetTestRig Locked(World, LockedHome);
	if (!Free.IsUsable(*this) || !Locked.IsUsable(*this))
	{
		return false;
	}
	ACataclysmEnemyCharacter* FreeEnemy = GadgetTestEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* LockedEnemy = GadgetTestEnemy(World, LockedHome + FVector(3.0f * M, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: two enemies"), FreeEnemy && LockedEnemy))
	{
		return false;
	}
	FGadgetTestRow Lock;
	Lock.Stat = FName(UCataclysmSkillSlots::LockedStat);
	Lock.Value = 1.0f;
	Lock.RequiredTag = TEXT("Slot.Heavy");
	Free.Holds({ConvertsSlot(TEXT("Slot.Heavy"))});
	Locked.Holds({ConvertsSlot(TEXT("Slot.Heavy")), Lock});

	// THE CONTROL: unlocked, the gadget is placed and casts.
	ACataclysmMinion* FreeGadget = Free.Taps(ECataclysmAbilitySlot::Heavy);
	Free.StandsAt(FVector(0.0f, -10.0f * M, 0.0f));
	if (!GadgetTestPlaced(*this, FreeGadget))
	{
		return false;
	}
	GadgetTestStep(FreeGadget);
	TestEqual(TEXT("control: an unlocked gadget casts on its step"), FreeGadget->SkillGadgetCastsMade, 1);
	TestTrue(TEXT("control: and strikes"), GadgetTestHealthOf(FreeEnemy) < 50000.0f);

	// LOCKED: placed all the same, and its cast refused.
	ACataclysmMinion* LockedGadget = Locked.Taps(ECataclysmAbilitySlot::Heavy);
	Locked.StandsAt(LockedHome + FVector(0.0f, -10.0f * M, 0.0f));
	TestNotNull(TEXT("under the Heavy slot's lock the press still places the gadget"), LockedGadget);
	if (!LockedGadget)
	{
		return false;
	}
	GadgetTestStep(LockedGadget);
	TestEqual(TEXT("and the lock refuses its cast"), LockedGadget->SkillGadgetCastsMade, 0);
	TestEqual(TEXT("so its enemy keeps its health"), GadgetTestHealthOf(LockedEnemy), 50000.0f, 0.001f);
	return true;
}

// ==========================================================================
// 11. The support self buff reaches the player within 8 m (S13)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetSelfBuffTest,
	"Cataclysm.SkillGadgets.TheSupportSelfBuffReachesThePlayerWithinEightMetresAndNotBeyond")
{
	using namespace CataclysmSkillGadgetTest;

	// THE SUPPORT GADGET 20 M ALONG Y. The player stands 7.5 m from it, then 9 m, then 7.5 m again.
	FGadgetTestWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FGadgetTestRig Rig(World, FVector::ZeroVector);
	FCataclysmWeaponSkill Coil;
	if (!Rig.IsUsable(*this, ECataclysmAbilitySlot::Support) || !Rig.HeldRow(ECataclysmAbilitySlot::Support, Coil)
		|| !TestTrue(TEXT("set-up: the whip's Support skill is a self buff"), Coil.Shape == ECataclysmSkillShape::SelfBuff))
	{
		return false;
	}
	const FVector At(0.0f, 20.0f * M, 0.0f);
	const FVector Within(0.0f, 12.5f * M, 0.0f);
	const FVector Beyond(0.0f, 11.0f * M, 0.0f);
	ACataclysmMinion* Gadget = GadgetTestPlace(*this, Rig, ECataclysmAbilitySlot::Support, At);
	if (!Gadget)
	{
		return false;
	}
	const auto BuffsRunningFromTheGadget = [&Rig, Gadget]()
	{
		int32 Running = 0;
		for (const FGameplayAbilitySpec& Spec : Rig.System->GetActivatableAbilities())
		{
			const UCataclysmSelfBuffSkill* Buff = Cast<UCataclysmSelfBuffSkill>(Spec.GetPrimaryInstance());
			Running += Buff && Spec.IsActive() && Buff->FreeRepeatCastBy.Get() == Gadget ? 1 : 0;
		}
		return Running;
	};

	// THE CONTROL: a player holding no row, 7.5 m from the gadget, is granted nothing.
	Rig.StandsAt(Within);
	GadgetTestStep(Gadget);
	TestEqual(TEXT("control: the gadget of a slot no row converts casts nothing"), Gadget->SkillGadgetCastsMade, 0);

	Rig.Holds({ConvertsSlot(TEXT("Slot.Support"))});
	Rig.StandsAt(Beyond);
	GadgetTestStep(Gadget);
	TestEqual(TEXT("9 m from the gadget, beyond 8 m, the player is granted nothing"), Gadget->SkillGadgetCastsMade, 0);
	TestEqual(TEXT("and no self buff of the gadget's runs"), BuffsRunningFromTheGadget(), 0);

	Rig.StandsAt(Within);
	GadgetTestStep(Gadget);
	TestEqual(TEXT("7.5 m from the gadget, within 8 m, the self buff is cast"), Gadget->SkillGadgetCastsMade, 1);
	TestEqual(TEXT("and runs on the player, cast by the gadget"), BuffsRunningFromTheGadget(), 1);

	// ITS CLOCK RUN WHILE THE FIRST STILL RUNS: no second copy.
	GadgetTestPass(World, 4.5f);
	GadgetTestStep(Gadget);
	TestEqual(TEXT("its clock run while the first buff still runs, no second is granted"), Gadget->SkillGadgetCastsMade, 1);
	TestEqual(TEXT("and one runs"), BuffsRunningFromTheGadget(), 1);
	return true;
}

// ==========================================================================
// 12. Struck and destroyed; the floor clearing ends it (S8)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetDestroyedTest,
	"Cataclysm.SkillGadgets.AStruckGadgetDiesTheNextPressPlacesANewOneAndClearingTheFloorRemovesIt")
{
	using namespace CataclysmSkillGadgetTest;

	FGadgetTestWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FGadgetTestRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Enemy = GadgetTestEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: an enemy"), Enemy))
	{
		return false;
	}
	Rig.Holds({ConvertsSlot(TEXT("Slot.Heavy"))});
	ACataclysmMinion* Gadget = Rig.Taps(ECataclysmAbilitySlot::Heavy);
	Rig.StandsAt(FVector(0.0f, -10.0f * M, 0.0f));
	if (!GadgetTestPlaced(*this, Gadget))
	{
		return false;
	}
	GadgetTestStep(Gadget);

	// THE CONTROL: it stands and has cast.
	TestFalse(TEXT("control: the gadget stands"), UCataclysmSkillEffects::IsDead(Gadget));
	TestTrue(TEXT("control: and has cast, so its clock runs"), Gadget->bSkillGadgetHasCast);

	// STRUCK: an enemy's blow larger than its health.
	UCataclysmSkillEffects::ApplyDirectDamage(Enemy, Gadget, 1000000.0f);
	TestTrue(TEXT("an enemy's blow kills the gadget"), UCataclysmSkillEffects::IsDead(Gadget));
	TestNull(TEXT("and no gadget stands for the slot"), Rig.System->SkillGadgetIn(ECataclysmAbilitySlot::Heavy));

	Rig.StandsAt(FVector::ZeroVector);
	ACataclysmMinion* Fresh = Rig.Taps(ECataclysmAbilitySlot::Heavy);
	Rig.StandsAt(FVector(0.0f, -10.0f * M, 0.0f));
	TestTrue(TEXT("the next press places a new gadget"), Fresh && Fresh != Gadget);
	if (!Fresh)
	{
		return false;
	}
	TestFalse(TEXT("with its clock not started"), Fresh->bSkillGadgetHasCast);
	GadgetTestStep(Fresh);
	TestEqual(TEXT("so it casts on its first step"), Fresh->SkillGadgetCastsMade, 1);

	// THE FLOOR CLEARED, as `GoToFloor` clears it.
	UCataclysmFloorContents::ClearTheFloor(*World, /*bCarryFollowers=*/true);
	TestFalse(TEXT("clearing the floor removes the gadget"), IsValid(Fresh));
	TestNull(TEXT("and no gadget stands for the slot"), Rig.System->SkillGadgetIn(ECataclysmAbilitySlot::Heavy));
	return true;
}

// ==========================================================================
// 13. Nothing is cast while the player is dead (S11)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetDeadPlayerTest,
	"Cataclysm.SkillGadgets.NothingIsCastWhileThePlayerIsDeadAndTheGadgetStays")
{
	using namespace CataclysmSkillGadgetTest;

	FGadgetTestWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FGadgetTestRig Alive(World, FVector::ZeroVector);
	const FVector DeadHome(60.0f * M, 0.0f, 0.0f);
	FGadgetTestRig Dying(World, DeadHome);
	if (!Alive.IsUsable(*this) || !Dying.IsUsable(*this))
	{
		return false;
	}
	Alive.Holds({ConvertsSlot(TEXT("Slot.Heavy"))});
	Dying.Holds({ConvertsSlot(TEXT("Slot.Heavy"))});
	const FVector AliveAt(0.0f, 20.0f * M, 0.0f);
	const FVector DyingAt = DeadHome + FVector(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* AliveGadget = GadgetTestPlace(*this, Alive, ECataclysmAbilitySlot::Heavy, AliveAt);
	ACataclysmMinion* DyingGadget = GadgetTestPlace(*this, Dying, ECataclysmAbilitySlot::Heavy, DyingAt);
	ACataclysmEnemyCharacter* AliveEnemy = GadgetTestEnemy(World, AliveAt + FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* DyingEnemy = GadgetTestEnemy(World, DyingAt + FVector(3.0f * M, 0.0f, 0.0f));
	if (!AliveGadget || !DyingGadget || !TestTrue(TEXT("set-up: two enemies"), AliveEnemy && DyingEnemy))
	{
		return false;
	}

	// THE CONTROL: a living player's gadget casts.
	GadgetTestStep(AliveGadget);
	TestEqual(TEXT("control: a living player's gadget casts"), AliveGadget->SkillGadgetCastsMade, 1);

	UCataclysmSkillEffects::MarkDead(Dying.Player);
	GadgetTestStep(DyingGadget);
	TestEqual(TEXT("a dead player's gadget casts nothing"), DyingGadget->SkillGadgetCastsMade, 0);
	TestEqual(TEXT("so its enemy keeps its health"), GadgetTestHealthOf(DyingEnemy), 50000.0f, 0.001f);
	TestFalse(TEXT("and the gadget stays"), UCataclysmSkillEffects::IsDead(DyingGadget) || !IsValid(DyingGadget));

	UCataclysmSkillEffects::ClearDead(Dying.Player);
	GadgetTestStep(DyingGadget);
	TestEqual(TEXT("the player alive again, the gadget casts"), DyingGadget->SkillGadgetCastsMade, 1);
	return true;
}

// ==========================================================================
// 14. A shapeless skill's gadget is placed and casts nothing (S10)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetShapelessTest,
	"Cataclysm.SkillGadgets.AShapelessSkillsPressPlacesAGadgetThatCastsNothing")
{
	using namespace CataclysmSkillGadgetTest;

	// A PLAYER HOLDING A WAR AXE, whose Heavy skill Cleaving Blow is named and states no shape, and a whip player 60 m
	// away for the control. NOT A CELESTIAL SWORD, as the first build had it: its Heavy row has no SkillName, and
	// `UCataclysmWeaponSkills::SkillsFor` skips a row with none, so that slot holds no skill at all and the rig refuses
	// it. Ruled 2026-10-16 after window g1 stopped on it.
	FGadgetTestWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FGadgetTestRig Rig(World, FVector::ZeroVector, TEXT("War"), TEXT("Axe"));
	FGadgetTestRig Whip(World, FVector(60.0f * M, 0.0f, 0.0f));
	FCataclysmWeaponSkill Shapeless;
	FCataclysmWeaponSkill Lash;
	if (!Rig.IsUsable(*this) || !Whip.IsUsable(*this) || !Rig.HeldRow(ECataclysmAbilitySlot::Heavy, Shapeless)
		|| !Whip.HeldRow(ECataclysmAbilitySlot::Heavy, Lash))
	{
		return false;
	}

	// THE CONTROL: a skill with a shape may be cast by a gadget; this one has none.
	TestTrue(TEXT("control: a gadget may cast Searing Lash"), ACataclysmMinion::SkillGadgetMayCast(Lash));
	TestTrue(TEXT("set-up: the War Axe's Heavy row, Cleaving Blow, states no shape"),
		Shapeless.Shape == ECataclysmSkillShape::None);
	TestFalse(TEXT("and a gadget may not cast it"), ACataclysmMinion::SkillGadgetMayCast(Shapeless));

	ACataclysmEnemyCharacter* Enemy = GadgetTestEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: an enemy"), Enemy))
	{
		return false;
	}
	Rig.Holds({ConvertsSlot(TEXT("Slot.Heavy"))});
	ACataclysmMinion* Gadget = Rig.Taps(ECataclysmAbilitySlot::Heavy);
	Rig.StandsAt(FVector(0.0f, -10.0f * M, 0.0f));
	TestNotNull(TEXT("the shapeless skill's press still places a gadget"), Gadget);
	if (!Gadget)
	{
		return false;
	}
	GadgetTestStep(Gadget);
	TestEqual(TEXT("which casts nothing"), Gadget->SkillGadgetCastsMade, 0);
	TestEqual(TEXT("so the enemy beside it keeps its health"), GadgetTestHealthOf(Enemy), 50000.0f, 0.001f);
	return true;
}

// ==========================================================================
// 15. The skill bar (S14, S10)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillGadgetBarTest,
	"Cataclysm.SkillGadgets.TheSkillBarBoxOfAConvertedSlotSaysGadgetAndSweepsWithItsClock")
{
	using namespace CataclysmSkillGadgetTest;

	FGadgetTestWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FGadgetTestRig Rig(World, FVector::ZeroVector);
	FCataclysmWeaponSkill Lash;
	if (!Rig.IsUsable(*this) || !Rig.HeldRow(ECataclysmAbilitySlot::Heavy, Lash))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: holding no row, the Heavy box names the skill and is no gadget's.
	const FCataclysmSkillBarSlot Before = GadgetTestBox(Rig.Player, ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("control: the Heavy box is drawn"), Before.Slot == ECataclysmAbilitySlot::Heavy && Before.bFilled);
	TestFalse(TEXT("control: and is not a gadget's"), Before.bIsASkillGadget);
	TestFalse(TEXT("control: and does not read Gadget"), Before.Name == UCataclysmSkillBar::SkillGadgetName());

	Rig.Holds({ConvertsSlot(TEXT("Slot.Heavy"))});
	const FCataclysmSkillBarSlot Converted = GadgetTestBox(Rig.Player, ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("converted, the Heavy box is a gadget's"), Converted.bIsASkillGadget);
	TestEqual(TEXT("and reads Gadget"), Converted.Name, FString(TEXT("Gadget")));
	TestEqual(TEXT("with no sweep before a gadget has cast"), Converted.CooldownDuration, 0.0f, 0.0001f);
	TestTrue(TEXT("and affordable, since placing costs nothing"), Converted.bAffordable);
	TestFalse(TEXT("and it does not say its gadget casts nothing, since Searing Lash can be cast"),
		Converted.bSkillGadgetCastsNothing);
	TestTrue(TEXT("so no words go over it"), UCataclysmSkillBar::SkillGadgetTextFor(Converted).IsEmpty());
	TestFalse(TEXT("the unconverted Special box is not a gadget's"),
		GadgetTestBox(Rig.Player, ECataclysmAbilitySlot::Special).bIsASkillGadget);

	// THE SWEEP FOLLOWS THE GADGET'S CLOCK.
	ACataclysmMinion* Gadget = Rig.Taps(ECataclysmAbilitySlot::Heavy);
	Rig.StandsAt(FVector(0.0f, -10.0f * M, 0.0f));
	ACataclysmEnemyCharacter* Enemy = GadgetTestEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	if (!GadgetTestPlaced(*this, Gadget) || !TestNotNull(TEXT("set-up: an enemy"), Enemy))
	{
		return false;
	}
	GadgetTestStep(Gadget);
	if (!TestEqual(TEXT("set-up: the gadget casts"), Gadget->SkillGadgetCastsMade, 1))
	{
		return false;
	}
	const float Clock = GadgetTestClockLength(Gadget);
	const FCataclysmSkillBarSlot JustCast = GadgetTestBox(Rig.Player, ECataclysmAbilitySlot::Heavy);
	TestEqual(TEXT("just after the cast the box's wait is the gadget's whole clock"), JustCast.CooldownDuration, Clock, 0.001f);
	TestEqual(TEXT("and all of it remains"), JustCast.CooldownRemaining, Clock, 0.001f);
	GadgetTestPass(World, 0.5f);
	const FCataclysmSkillBarSlot Later = GadgetTestBox(Rig.Player, ECataclysmAbilitySlot::Heavy);
	TestEqual(TEXT("half a second on, half a second less remains"), Later.CooldownRemaining, Clock - 0.5f, 0.001f);
	TestEqual(TEXT("and the sweep covers that share of the box"),
		UCataclysmSkillBar::CooldownFractionFor(Later.CooldownRemaining, Later.CooldownDuration),
		(Clock - 0.5f) / Clock, 0.001f);

	// AND A BOX WHOSE GADGET CAN NEVER CAST SAYS SO, over the box.
	FCataclysmSkillBarSlot Idle = Converted;
	Idle.bSkillGadgetCastsNothing = true;
	TestEqual(TEXT("a gadget's box whose skill cannot be cast says so"), UCataclysmSkillBar::SkillGadgetTextFor(Idle),
		FString(TEXT("Gadget casts nothing")));
	Idle.bIsASkillGadget = false;
	TestTrue(TEXT("and a box that is not a gadget's says nothing"), UCataclysmSkillBar::SkillGadgetTextFor(Idle).IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
