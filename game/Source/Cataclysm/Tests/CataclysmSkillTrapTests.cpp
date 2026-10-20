// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDeployableSelfBlast.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmSkillBar.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Skill traps, layer 1: the mechanism and its source. Ruled 2026-10-19 by the coordinating session (T1-T19), for "Your
 * heavy attack / special ability / ultimate ability is converted into a placeable trap. When triggered, it fires your
 * ... Scales off trap damage. All placed copies share the ... cooldown." Beside `Cataclysm.SkillGadgets.`, whose rig
 * this follows.
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER HOLDING A DEMONIC WHIP, given a weapon damage of 100, 10,000 health and
 * 1,000 mana by hand: Heavy Searing Lash, a 5 m cone; Ultimate The Gathering, a 14 m point-blank strike. NO ROW IS
 * AUTHORED, SO EVERY STAT IS GRANTED BY HAND, one stat line per call, replacing the last. A slot's flag is a flat 1 on
 * `skill_slot_is_a_trap` requiring the slot's tag, the row the enchantment session is to write.
 *
 * THE TRAP'S TYPE ROW IS THE ENCHANTMENT SESSION'S, `SkillTrap` in `game/Data/MinionTypes.csv`, authored on top of this
 * layer. The first test reads it from the table cell by cell; every case that places a trap says so when it is missing.
 *
 * A PRESS PAYS (T5): the two cases that press the Heavy key hold the 1,000 mana that covers its cost, and the Heavy
 * costs no Fervour. Every other trap is placed by `ACataclysmMinion::PlaceSkillTrap` with no key pressed, and a trap's
 * cast is a free start, so nothing else needs a resource. A free start given a place lands at once and waits for no
 * attack clip. Every body stands at least 1 m from every other, on a real side: the players on the players', the traps
 * on their player's, the creatures on the monsters'. NOTHING HERE WAITS ON A TICK: the world's clock is moved by hand and
 * a trap's step is called directly, as `RegenerationStep` calls it, a whole second plus a quarter after placing so that
 * no wait ends on the arming mark. Helper types and functions are named after the subject.
 */
namespace CataclysmSkillTrapTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The health every creature here starts with. */
	constexpr float SkillTrapEnemyHealth = 50000.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FSkillTrapWorld
	{
		FSkillTrapWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FSkillTrapWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/** A creature of the monsters' side with no armour, evasion, block or resistance. It deals nothing of its own. */
	ACataclysmEnemyCharacter* SkillTrapEnemy(UWorld* World, const FVector& Where)
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
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), SkillTrapEnemyHealth);
		return Made;
	}

	float SkillTrapHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** What a creature has lost since it was made. */
	float SkillTrapTakenFrom(const AActor* Who)
	{
		return SkillTrapEnemyHealth - SkillTrapHealthOf(Who);
	}

	/** One row of a stat line a test's player holds. */
	struct FSkillTrapRow
	{
		FName Stat;
		float Value = 0.0f;
		ECataclysmStatBucket Bucket = ECataclysmStatBucket::Flat;
		/** The row's one required tag, or null for none. */
		const TCHAR* RequiredTag = nullptr;
	};

	/** A slot's flag: a flat 1 on `skill_slot_is_a_trap` requiring the slot's tag, the row to be authored. */
	FSkillTrapRow SkillTrapFlag(const TCHAR* SlotTag)
	{
		return {FName(UCataclysmAbilitySystemComponent::SkillSlotIsATrapStat), 1.0f, ECataclysmStatBucket::Flat, SlotTag};
	}

	/** An increase of `Stat` requiring `Tag`. */
	FSkillTrapRow SkillTrapIncrease(const TCHAR* Stat, float Value, const TCHAR* Tag)
	{
		return {FName(Stat), Value, ECataclysmStatBucket::Increased, Tag};
	}

	/** A possessed player character holding a Demonic Whip, its ability system and its weapon slots. */
	struct FSkillTrapRig
	{
		FSkillTrapRig(UWorld* World, const FVector& Where)
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
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		}

		/** Whether everything was made and the whip holds a Heavy and an Ultimate row. Reports why not. */
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

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			System->SetNumericAttributeBase(Attribute, Value);
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			return System->GetNumericAttribute(Attribute);
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

		/** REPLACES THE WHOLE STAT LINE with these rows. */
		void Holds(const TArray<FSkillTrapRow>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FSkillTrapRow& Each : Rows)
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
				FCataclysmStatInputs& Line = Stats.FindOrAdd(Each.Stat);
				Line.Base = 0.0f;
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

		/** Move the player, so it does not stand on a trap placed at its feet. */
		void StandsAt(const FVector& Where) const
		{
			Player->SetActorLocation(Where, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr,
									 ETeleportType::TeleportPhysics);
		}

		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
	};

	/** A skill trap for this rig's `Slot` at `Where`, or null with the reason reported. */
	ACataclysmMinion* SkillTrapPlace(FAutomationTestBase& Test, const FSkillTrapRig& Rig, ECataclysmAbilitySlot Slot,
									 const FVector& Where)
	{
		ACataclysmMinion* Made = ACataclysmMinion::PlaceSkillTrap(Rig.Player, Slot, Where);
		if (!Made)
		{
			Test.AddError(TEXT("set-up: no skill trap was placed. DT_MinionTypes needs the SkillTrap row, which the "
							   "enchantment session authors: run tools/generate_datatable_assets.py"));
		}
		return Made;
	}

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FSkillTrapEventCount
	{
		FSkillTrapEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
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
		~FSkillTrapEventCount()
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
	void SkillTrapPass(UWorld* World, float Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/** Run a trap's quarter-second step, as `RegenerationStep` calls it. */
	void SkillTrapStep(ACataclysmMinion* Trap)
	{
		if (IsValid(Trap))
		{
			ACataclysmMinion::GadgetStep(Trap);
		}
	}

	/** Whether a trap is gone: destroyed, or on its way out. */
	bool SkillTrapGone(const TWeakObjectPtr<ACataclysmMinion>& Trap)
	{
		return !Trap.IsValid() || Trap->IsActorBeingDestroyed();
	}

	/** The skill bar's box for `Slot`, or an empty box with `None` for its slot. */
	FCataclysmSkillBarSlot SkillTrapBox(const AActor* Player, ECataclysmAbilitySlot Slot)
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

	/** Whether a type row's Tags cell names `Tag`. */
	bool SkillTrapRowNames(const FCataclysmMinionTypeRow& Row, const TCHAR* Tag)
	{
		TArray<FString> Named;
		Row.Tags.ParseIntoArray(Named, TEXT(","), /*InCullEmpty=*/true);
		for (FString& Each : Named)
		{
			if (Each.TrimStartAndEnd().Equals(Tag))
			{
				return true;
			}
		}
		return false;
	}

	/** A pool action of a test's own: `Pool` restored by `Percent` of its maximum on `Event`, requiring `Tag`. */
	FCataclysmPoolAction SkillTrapRestores(const TCHAR* Event, const TCHAR* Pool, const TCHAR* Tag)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(Event);
		Action.Pool = FName(Pool);
		Action.Percent = 10.0f;
		Action.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(Tag), /*ErrorIfNotFound=*/false));
		Action.TriggerKey = FName(*FString::Printf(TEXT("SkillTrapTest:%s:%s:%s"), Event, Pool, Tag));
		return Action;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 0. The type row, read from the table (M1, T9)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapTypeRowTest,
	"Cataclysm.Traps.TheSkillTrapRowIsAMachineTrapOfFortyHealthThatDealsDrawsAndWalksNothing")
{
	using namespace CataclysmSkillTrapTest;

	const UDataTable* Table = ACataclysmMinion::LoadTypeTable();
	if (!TestNotNull(TEXT("set-up: the minion type table"), Table))
	{
		return false;
	}
	TestNotNull(TEXT("control: the same lookup finds the SkillGadget row"),
		ACataclysmMinion::FindType(Table, FString(ACataclysmMinion::SkillGadgetTypeName)));
	const FCataclysmMinionTypeRow* Row = ACataclysmMinion::FindType(Table, FString(ACataclysmMinion::SkillTrapTypeName));
	if (!TestNotNull(TEXT("the table holds the SkillTrap row, which the enchantment session authors; if it is "
						  "missing, run tools/generate_datatable_assets.py"), Row))
	{
		return false;
	}
	TestEqual(TEXT("its family is Machine"), Row->Family, FString(TEXT("Machine")));
	TestEqual(TEXT("its health is 40, the Spike Trap's (T9)"), Row->BaseHealth, 40.0f, 0.001f);
	TestEqual(TEXT("and 18 a level, the Spike Trap's"), Row->HealthPerLevel, 18.0f, 0.001f);
	TestEqual(TEXT("it deals nothing of its own"), Row->BaseDamage, 0.0f, 0.001f);
	TestEqual(TEXT("at any level"), Row->DamagePerLevel, 0.0f, 0.001f);
	TestEqual(TEXT("it does not walk"), Row->MoveSpeed, 0.0f, 0.001f);
	TestEqual(TEXT("and draws no enemy (T9)"), Row->ThreatPercent, 0.0f, 0.001f);
	TestEqual(TEXT("its reach is the trigger radius of 3 m (T2)"), Row->ReachCm,
		ACataclysmMinion::SkillTrapTriggerRadiusCm, 0.001f);
	TestEqual(TEXT("and so is its notice radius"), Row->NoticeRadiusCm, ACataclysmMinion::SkillTrapTriggerRadiusCm, 0.001f);
	TestTrue(TEXT("its tags name Type.Minion"), SkillTrapRowNames(*Row, TEXT("Type.Minion")));
	TestTrue(TEXT("Type.Deployable"), SkillTrapRowNames(*Row, TEXT("Type.Deployable")));
	TestTrue(TEXT("Minion.Machine"), SkillTrapRowNames(*Row, TEXT("Minion.Machine")));
	TestTrue(TEXT("and Type.Trap"), SkillTrapRowNames(*Row, TEXT("Type.Trap")));
	return true;
}

// ==========================================================================
// 1. A press places a trap, pays and starts the cooldown; a second press before it places nothing (M2, T3, T5, T17, T19)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapPressTest,
	"Cataclysm.Traps.APressPlacesATrapAtTheFeetPaysTheSkillsCostStartsItsCooldownAndASecondPressBeforeItPlacesNothing")
{
	using namespace CataclysmSkillTrapTest;

	// THREE PLAYERS 100 M APART: THE CONTROL HOLDS NO ROW, THE SECOND THE HEAVY TRAP FLAG, THE THIRD THE HEAVY TRAP FLAG
	// AND THE HEAVY GADGET FLAG.
	FSkillTrapWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FSkillTrapRig Plain(World, FVector::ZeroVector);
	FSkillTrapRig Rig(World, FVector(100.0f * M, 0.0f, 0.0f));
	FSkillTrapRig Both(World, FVector(200.0f * M, 0.0f, 0.0f));
	if (!Plain.IsUsable(*this) || !Rig.IsUsable(*this) || !Both.IsUsable(*this))
	{
		return false;
	}
	const FGameplayAttribute Mana = UCataclysmVitalAttributeSet::GetManaAttribute();
	const FGameplayTag HeavyCooldown = UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Heavy);

	// THE CONTROL, READ FIRST: with the flag at nought the press casts the skill, which pays and starts the cooldown.
	Plain.KeyDown(ECataclysmAbilitySlot::Heavy);
	const float PlainPaid = 1000.0f - Plain.Get(Mana);
	TestTrue(TEXT("control: the Heavy press of a player holding no row starts the Heavy cooldown"),
		Plain.System->HasMatchingGameplayTag(HeavyCooldown));
	TestTrue(*FString::Printf(TEXT("control: and pays %.2f mana"), PlainPaid), PlainPaid > 0.0f);
	TestEqual(TEXT("control: and places no trap"), ACataclysmMinion::SkillTrapsOf(Plain.Player).Num(), 0);
	Plain.KeyUp(ECataclysmAbilitySlot::Heavy);

	// THE CONVERTED PRESS.
	Rig.Holds({SkillTrapFlag(TEXT("Slot.Heavy"))});
	FSkillTrapEventCount SkillUses(Rig.System, TEXT("skill_use"));
	FSkillTrapEventCount Placings(Rig.System, TEXT("trap_placed"));
	const FVector Feet = Rig.Player->GetActorLocation();
	Rig.KeyDown(ECataclysmAbilitySlot::Heavy);
	const TArray<ACataclysmMinion*> Placed = ACataclysmMinion::SkillTrapsOf(Rig.Player);
	if (Placed.Num() != 1)
	{
		AddError(FString::Printf(TEXT("set-up: the press placed %d skill traps, not one. DT_MinionTypes needs the "
									  "SkillTrap row, which the enchantment session authors: run "
									  "tools/generate_datatable_assets.py"), Placed.Num()));
		return false;
	}
	ACataclysmMinion* Trap = Placed[0];
	TestTrue(TEXT("the press placed a skill trap for the Heavy slot"),
		Trap->IsSkillTrap() && Trap->SkillTrapSlot == ECataclysmAbilitySlot::Heavy);
	TestTrue(*FString::Printf(TEXT("at the player's feet (T3): %.3f cm away"), FVector::Dist(Trap->GetActorLocation(), Feet)),
		FVector::Dist(Trap->GetActorLocation(), Feet) < 1.0f);
	TestEqual(TEXT("of the SkillTrap type"), Trap->TypeName, FString(ACataclysmMinion::SkillTrapTypeName));
	TestTrue(TEXT("a trap and not a gadget"), Trap->IsTrap() && !Trap->IsGadget() && !Trap->IsSkillGadget());
	const float RigPaid = 1000.0f - Rig.Get(Mana);
	TestEqual(*FString::Printf(TEXT("the press paid the skill's cost (T5): %.2f, as the control's press paid %.2f"),
							   RigPaid, PlainPaid),
		RigPaid, PlainPaid, 0.01f);
	TestTrue(TEXT("and started the Heavy cooldown (T5)"), Rig.System->HasMatchingGameplayTag(HeavyCooldown));
	TestEqual(TEXT("the press is the skill use (T19): skill_use was raised once"), SkillUses.Count, 1);
	TestEqual(TEXT("trap_placed was raised once"), Placings.Count, 1);
	TestEqual(TEXT("and the trap cast nothing as it was placed"), Trap->SkillTrapTriggersMade, 0);
	Rig.KeyUp(ECataclysmAbilitySlot::Heavy);

	// A SECOND PRESS WHILE THE COOLDOWN RUNS, the player moved off the trap.
	Rig.StandsAt(Feet + FVector(0.0f, 5.0f * M, 0.0f));
	const float ManaBefore = Rig.Get(Mana);
	Rig.KeyDown(ECataclysmAbilitySlot::Heavy);
	TestEqual(TEXT("a second press before the cooldown has run places nothing"),
		ACataclysmMinion::SkillTrapsOf(Rig.Player).Num(), 1);
	TestEqual(TEXT("and pays nothing"), Rig.Get(Mana), ManaBefore, 0.001f);
	TestEqual(TEXT("and is no second skill use"), SkillUses.Count, 1);
	Rig.KeyUp(ECataclysmAbilitySlot::Heavy);

	// A SLOT CONVERTED BOTH WAYS MAKES A TRAP (T17).
	Both.Holds({SkillTrapFlag(TEXT("Slot.Heavy")),
				{FName(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat), 1.0f, ECataclysmStatBucket::Flat,
				 TEXT("Slot.Heavy")}});
	Both.KeyDown(ECataclysmAbilitySlot::Heavy);
	TestEqual(TEXT("a slot converted to both a gadget and a trap places a trap (T17)"),
		ACataclysmMinion::SkillTrapsOf(Both.Player).Num(), 1);
	TestNull(TEXT("and no gadget"), Both.System->SkillGadgetIn(ECataclysmAbilitySlot::Heavy));
	Both.KeyUp(ECataclysmAbilitySlot::Heavy);
	return true;
}

// ==========================================================================
// 2. Arming, and triggering on the first enemy within 3 m (M3, M4, T1, T2)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapArmsAndTriggersTest,
	"Cataclysm.Traps.ATrapArmsAfterASecondThenTriggersOnTheFirstEnemyWithinThreeMetresAndNotOneAtFour")
{
	using namespace CataclysmSkillTrapTest;

	// ONE PLAYER AT THE ORIGIN: A HEAVY TRAP 20 M NORTH WITH AN ENEMY 2.5 M EAST OF IT, AND A HEAVY TRAP 20 M SOUTH WITH AN
	// ENEMY 4 M EAST OF IT.
	FSkillTrapWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FSkillTrapRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	Rig.Holds({SkillTrapFlag(TEXT("Slot.Heavy"))});
	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector South(0.0f, -20.0f * M, 0.0f);
	ACataclysmEnemyCharacter* Near = SkillTrapEnemy(World, North + FVector(2.5f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Far = SkillTrapEnemy(World, South + FVector(4.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the enemy 2.5 m from the north trap"), Near)
		|| !TestNotNull(TEXT("set-up: the enemy 4 m from the south trap"), Far))
	{
		return false;
	}
	ACataclysmMinion* NorthTrap = SkillTrapPlace(*this, Rig, ECataclysmAbilitySlot::Heavy, North);
	ACataclysmMinion* SouthTrap = SkillTrapPlace(*this, Rig, ECataclysmAbilitySlot::Heavy, South);
	if (!NorthTrap || !SouthTrap)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> NorthWatched(NorthTrap);
	const TWeakObjectPtr<ACataclysmMinion> SouthWatched(SouthTrap);
	FSkillTrapEventCount Triggers(Rig.System, TEXT("trap_triggered"));

	// THE CONTROL, READ FIRST: HALF A SECOND IN, NEITHER IS ARMED, and the enemy within 3 m is untouched.
	SkillTrapPass(World, 0.5f);
	SkillTrapStep(NorthTrap);
	SkillTrapStep(SouthTrap);
	TestFalse(TEXT("control: half a second after placing, the north trap is not armed (T1)"), NorthTrap->bSkillTrapArmed);
	TestEqual(TEXT("control: so the enemy 2.5 m from it is untouched"), SkillTrapHealthOf(Near), SkillTrapEnemyHealth,
		0.001f);
	TestEqual(TEXT("control: and no trap_triggered was raised"), Triggers.Count, 0);

	// A SECOND AND A QUARTER IN, BOTH ARE ARMED.
	SkillTrapPass(World, 0.75f);
	SkillTrapStep(NorthTrap);
	TestTrue(*FString::Printf(TEXT("armed, the north trap triggered on the enemy 2.5 m away (T2): it took %.2f"),
							  SkillTrapTakenFrom(Near)),
		SkillTrapTakenFrom(Near) > 0.0f);
	TestEqual(TEXT("trap_triggered was raised once"), Triggers.Count, 1);
	TestTrue(TEXT("and the trap is used up"), SkillTrapGone(NorthWatched));
	SkillTrapStep(SouthTrap);
	TestTrue(TEXT("the south trap is armed"), SouthTrap->bSkillTrapArmed);
	TestEqual(TEXT("and does not trigger on the enemy 4 m away"), SkillTrapHealthOf(Far), SkillTrapEnemyHealth, 0.001f);
	TestFalse(TEXT("so it still stands"), SkillTrapGone(SouthWatched));
	TestEqual(TEXT("and no second trap_triggered was raised"), Triggers.Count, 1);
	return true;
}

// ==========================================================================
// 3. The arming-speed and trigger-radius increases (M3, M4)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapIncreasesTest,
	"Cataclysm.Traps.TheArmingSpeedIncreaseArmsATrapSoonerAndTheTriggerRadiusIncreaseWidensItsReach")
{
	using namespace CataclysmSkillTrapTest;

	// THREE PLAYERS 100 M APART, EACH WITH A HEAVY TRAP 20 M NORTH AND AN ENEMY 4 M EAST OF IT: the control holds the flag
	// alone, the quick one 100% increased trap_arming_speed, the wide one 50% increased trap_trigger_radius, each
	// requiring Type.Trap.
	FSkillTrapWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector QuickHome(100.0f * M, 0.0f, 0.0f);
	const FVector WideHome(200.0f * M, 0.0f, 0.0f);
	FSkillTrapRig Plain(World, FVector::ZeroVector);
	FSkillTrapRig Quick(World, QuickHome);
	FSkillTrapRig Wide(World, WideHome);
	if (!Plain.IsUsable(*this) || !Quick.IsUsable(*this) || !Wide.IsUsable(*this))
	{
		return false;
	}
	Plain.Holds({SkillTrapFlag(TEXT("Slot.Heavy"))});
	Quick.Holds({SkillTrapFlag(TEXT("Slot.Heavy")),
				 SkillTrapIncrease(ACataclysmMinion::TrapArmingSpeedStat, 100.0f, TEXT("Type.Trap"))});
	Wide.Holds({SkillTrapFlag(TEXT("Slot.Heavy")),
				SkillTrapIncrease(ACataclysmMinion::TrapTriggerRadiusStat, 50.0f, TEXT("Type.Trap"))});
	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector East(4.0f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* PlainEnemy = SkillTrapEnemy(World, North + East);
	ACataclysmEnemyCharacter* QuickEnemy = SkillTrapEnemy(World, QuickHome + North + East);
	ACataclysmEnemyCharacter* WideEnemy = SkillTrapEnemy(World, WideHome + North + East);
	if (!TestNotNull(TEXT("set-up: the control's enemy"), PlainEnemy)
		|| !TestNotNull(TEXT("set-up: the quick player's enemy"), QuickEnemy)
		|| !TestNotNull(TEXT("set-up: the wide player's enemy"), WideEnemy))
	{
		return false;
	}
	ACataclysmMinion* PlainTrap = SkillTrapPlace(*this, Plain, ECataclysmAbilitySlot::Heavy, North);
	ACataclysmMinion* QuickTrap = SkillTrapPlace(*this, Quick, ECataclysmAbilitySlot::Heavy, QuickHome + North);
	ACataclysmMinion* WideTrap = SkillTrapPlace(*this, Wide, ECataclysmAbilitySlot::Heavy, WideHome + North);
	if (!PlainTrap || !QuickTrap || !WideTrap)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> WideWatched(WideTrap);

	// THE FIGURES EACH TRAP WAS GIVEN, THE CONTROL FIRST.
	TestEqual(TEXT("control: the plain trap arms at an age of 1 s (T1)"), PlainTrap->SkillTrapArmsAtAgeSeconds, 1.0f, 0.001f);
	TestEqual(TEXT("the quick trap, 100% faster, arms at half a second"), QuickTrap->SkillTrapArmsAtAgeSeconds, 0.5f, 0.001f);
	TestEqual(TEXT("the wide trap arms at 1 s: its row is not about arming"), WideTrap->SkillTrapArmsAtAgeSeconds, 1.0f,
		0.001f);
	TestEqual(TEXT("control: the plain trap triggers within 3 m (T2)"),
		ACataclysmMinion::SkillTrapTriggerRadiusCmFor(Plain.Player, PlainTrap->TypeTags), 300.0f, 0.01f);
	TestEqual(TEXT("the wide trap, 50% wider, within 4.5 m"),
		ACataclysmMinion::SkillTrapTriggerRadiusCmFor(Wide.Player, WideTrap->TypeTags), 450.0f, 0.01f);

	// THREE QUARTERS OF A SECOND IN.
	SkillTrapPass(World, 0.75f);
	SkillTrapStep(PlainTrap);
	SkillTrapStep(QuickTrap);
	SkillTrapStep(WideTrap);
	TestFalse(TEXT("control: 0.75 s in, the plain trap is not armed"), PlainTrap->bSkillTrapArmed);
	TestTrue(TEXT("the quick trap is"), QuickTrap->bSkillTrapArmed);
	TestFalse(TEXT("and the wide trap is not"), WideTrap->bSkillTrapArmed);

	// A SECOND AND A QUARTER IN.
	SkillTrapPass(World, 0.5f);
	SkillTrapStep(PlainTrap);
	SkillTrapStep(QuickTrap);
	SkillTrapStep(WideTrap);
	TestTrue(TEXT("control: 1.25 s in, the plain trap is armed"), PlainTrap->bSkillTrapArmed);
	TestEqual(TEXT("control: and does not trigger on its enemy 4 m away"), SkillTrapHealthOf(PlainEnemy),
		SkillTrapEnemyHealth, 0.001f);
	TestTrue(*FString::Printf(TEXT("the wide trap triggered on its enemy 4 m away: it took %.2f"),
							  SkillTrapTakenFrom(WideEnemy)),
		SkillTrapTakenFrom(WideEnemy) > 0.0f);
	TestTrue(TEXT("and was used up"), SkillTrapGone(WideWatched));
	TestEqual(TEXT("the quick trap, armed half a second before, does not trigger at 4 m"), SkillTrapHealthOf(QuickEnemy),
		SkillTrapEnemyHealth, 0.001f);
	return true;
}

// ==========================================================================
// 4. The cast: at the enemy for the Heavy, around it for the Ultimate, priced by a trap row and not a gadget row (M5, T10)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapCastTest,
	"Cataclysm.Traps.ATriggerCastsTheHeavyAtItsEnemyAndTheUltimateAroundItPricedByATrapRowAndNotAGadgetRow")
{
	using namespace CataclysmSkillTrapTest;

	// THREE PLAYERS 100 M APART, EACH WITH A HEAVY TRAP 20 M NORTH AND ITS ENEMY 2.5 M EAST OF IT: the control holds the
	// flags alone, the second 50% increased minion_damage requiring Type.Trap ("Traps deal 20%-40% increased damage" is
	// such a row), the third 30% increased attack_damage requiring Type.Deployable (a gadget damage row). THE CONTROL
	// ALSO HAS AN ULTIMATE TRAP 30 M SOUTH, ITS ENEMY 2.5 M EAST OF IT AND A SECOND ENEMY 15 M EAST OF IT, and a lone
	// enemy 60 M NORTH for its own cast of Searing Lash from 2.5 m west of it.
	FSkillTrapWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	if (!TestNotNull(TEXT("set-up: the critical strike roll is pinned at 100"), NeverCritical.Variable))
	{
		return false;
	}
	const FVector TrapRowHome(100.0f * M, 0.0f, 0.0f);
	const FVector GadgetRowHome(200.0f * M, 0.0f, 0.0f);
	FSkillTrapRig Plain(World, FVector::ZeroVector);
	FSkillTrapRig TrapRow(World, TrapRowHome);
	FSkillTrapRig GadgetRow(World, GadgetRowHome);
	FCataclysmWeaponSkill Lash;
	FCataclysmWeaponSkill Gathering;
	if (!Plain.IsUsable(*this) || !TrapRow.IsUsable(*this) || !GadgetRow.IsUsable(*this)
		|| !Plain.HeldRow(ECataclysmAbilitySlot::Heavy, Lash) || !Plain.HeldRow(ECataclysmAbilitySlot::Ultimate, Gathering))
	{
		return false;
	}
	const FSkillTrapRow HeavyFlag = SkillTrapFlag(TEXT("Slot.Heavy"));
	const FSkillTrapRow UltimateFlag = SkillTrapFlag(TEXT("Slot.Ultimate"));
	Plain.Holds({HeavyFlag, UltimateFlag});
	TrapRow.Holds({HeavyFlag, SkillTrapIncrease(TEXT("minion_damage"), 50.0f, TEXT("Type.Trap"))});
	GadgetRow.Holds({HeavyFlag, SkillTrapIncrease(TEXT("attack_damage"), 30.0f, TEXT("Type.Deployable"))});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector South(0.0f, -30.0f * M, 0.0f);
	const FVector East(2.5f * M, 0.0f, 0.0f);
	const FVector Lone(0.0f, 60.0f * M, 0.0f);
	ACataclysmEnemyCharacter* LoneEnemy = SkillTrapEnemy(World, Lone);
	ACataclysmEnemyCharacter* PlainEnemy = SkillTrapEnemy(World, North + East);
	ACataclysmEnemyCharacter* TrapRowEnemy = SkillTrapEnemy(World, TrapRowHome + North + East);
	ACataclysmEnemyCharacter* GadgetRowEnemy = SkillTrapEnemy(World, GadgetRowHome + North + East);
	ACataclysmEnemyCharacter* AroundEnemy = SkillTrapEnemy(World, South + East);
	ACataclysmEnemyCharacter* FarEnemy = SkillTrapEnemy(World, South + FVector(15.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the lone enemy for the player's own cast"), LoneEnemy)
		|| !TestNotNull(TEXT("set-up: the control's Heavy trap's enemy"), PlainEnemy)
		|| !TestNotNull(TEXT("set-up: the trap row player's enemy"), TrapRowEnemy)
		|| !TestNotNull(TEXT("set-up: the gadget row player's enemy"), GadgetRowEnemy)
		|| !TestNotNull(TEXT("set-up: the Ultimate trap's enemy"), AroundEnemy)
		|| !TestNotNull(TEXT("set-up: the enemy 15 m from the Ultimate trap"), FarEnemy))
	{
		return false;
	}
	ACataclysmMinion* PlainHeavy = SkillTrapPlace(*this, Plain, ECataclysmAbilitySlot::Heavy, North);
	ACataclysmMinion* TrapRowHeavy = SkillTrapPlace(*this, TrapRow, ECataclysmAbilitySlot::Heavy, TrapRowHome + North);
	ACataclysmMinion* GadgetRowHeavy =
		SkillTrapPlace(*this, GadgetRow, ECataclysmAbilitySlot::Heavy, GadgetRowHome + North);
	ACataclysmMinion* PlainUltimate = SkillTrapPlace(*this, Plain, ECataclysmAbilitySlot::Ultimate, South);
	if (!PlainHeavy || !TrapRowHeavy || !GadgetRowHeavy || !PlainUltimate)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> PlainHeavyWatched(PlainHeavy);
	const TWeakObjectPtr<ACataclysmMinion> PlainUltimateWatched(PlainUltimate);

	// WHICH SLOTS CAST AROUND THE ENEMY, AND WHERE THE FAR ENEMY STANDS.
	TestFalse(TEXT("set-up: a Heavy trap fires at its enemy from the trap"),
		ACataclysmMinion::SkillTrapCastsAroundItsEnemy(ECataclysmAbilitySlot::Heavy));
	TestTrue(TEXT("set-up: a Special trap casts around its enemy"),
		ACataclysmMinion::SkillTrapCastsAroundItsEnemy(ECataclysmAbilitySlot::Special));
	TestTrue(TEXT("set-up: an Ultimate trap casts around its enemy"),
		ACataclysmMinion::SkillTrapCastsAroundItsEnemy(ECataclysmAbilitySlot::Ultimate));
	TestTrue(*FString::Printf(TEXT("set-up: The Gathering reaches %.0f cm, short of the far enemy's 1500 from the trap and "
								   "past its 1250 from the trap's enemy"), Gathering.Params.RadiusCm),
		Gathering.Params.RadiusCm < 1450.0f && Gathering.Params.RadiusCm > 1300.0f);

	// THE CONTROL, READ FIRST: THE PLAYER'S OWN CAST OF SEARING LASH, FROM 2.5 M WEST OF THE LONE ENEMY AT IT.
	const FVector OwnFrom = Lone - East;
	if (!TestTrue(TEXT("control: the player's own cast starts"),
			UCataclysmTriggeredSkill::Trigger(Plain.Player, Lash, Lone, /*DamageShare=*/1.0f, /*bPaysCost=*/false,
											  &OwnFrom)))
	{
		return false;
	}
	const float Own = SkillTrapTakenFrom(LoneEnemy);
	TestTrue(*FString::Printf(TEXT("control: it takes %.2f"), Own), Own > 0.0f);

	// THE HEAVY TRAPS, ARMED.
	SkillTrapPass(World, 1.25f);
	SkillTrapStep(PlainHeavy);
	TestEqual(*FString::Printf(TEXT("the control's Heavy trap cast Searing Lash once at its enemy, at the player's own "
									"figure with no trap row: %.2f against %.2f"), SkillTrapTakenFrom(PlainEnemy), Own),
		SkillTrapTakenFrom(PlainEnemy), Own, Own * 0.001f);
	TestTrue(TEXT("and was used up"), SkillTrapGone(PlainHeavyWatched));
	SkillTrapStep(TrapRowHeavy);
	TestEqual(*FString::Printf(TEXT("a minion_damage row naming Type.Trap prices the trap's cast (T10): 50%% more, %.2f "
									"against %.2f"), SkillTrapTakenFrom(TrapRowEnemy), Own),
		SkillTrapTakenFrom(TrapRowEnemy), Own * 1.5f, Own * 0.001f);
	SkillTrapStep(GadgetRowHeavy);
	TestEqual(*FString::Printf(TEXT("a gadget damage row does not reach a trap's cast, which carries no Type.Deployable: "
									"%.2f against %.2f"), SkillTrapTakenFrom(GadgetRowEnemy), Own),
		SkillTrapTakenFrom(GadgetRowEnemy), Own, Own * 0.001f);

	// THE ULTIMATE TRAP, ARMED.
	SkillTrapStep(PlainUltimate);
	TestTrue(*FString::Printf(TEXT("the Ultimate trap's cast struck its enemy: %.2f"), SkillTrapTakenFrom(AroundEnemy)),
		SkillTrapTakenFrom(AroundEnemy) > 0.0f);
	TestTrue(*FString::Printf(TEXT("and the enemy 15 m from the trap, 12.5 m from the trap's enemy, so the cast was "
								   "centered on the triggering enemy: %.2f"), SkillTrapTakenFrom(FarEnemy)),
		SkillTrapTakenFrom(FarEnemy) > 0.0f);
	TestTrue(TEXT("and the Ultimate trap was used up"), SkillTrapGone(PlainUltimateWatched));
	return true;
}

// ==========================================================================
// 5. The cap and the expiry (T4, T6)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapCapAndExpiryTest,
	"Cataclysm.Traps.ASixthTrapDestroysTheOldestWithoutTriggeringAndATrapThatExpiresDoesNotTrigger")
{
	using namespace CataclysmSkillTrapTest;

	// A PLAYER AT THE ORIGIN WITH FIVE HEAVY TRAPS 10 M APART ALONG Y, AN ENEMY 2 M EAST OF THE FIRST AND ANOTHER 2 M EAST
	// OF THE SECOND; A SECOND PLAYER 100 M EAST HOLDING 50% INCREASED minion_duration REQUIRING Type.Trap ("Traps last
	// 50%-100% longer before expiring" is such a row), WITH ONE TRAP. No trap is stepped, so none triggers by its step.
	FSkillTrapWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector LongHome(100.0f * M, 0.0f, 0.0f);
	FSkillTrapRig Rig(World, FVector::ZeroVector);
	FSkillTrapRig Long(World, LongHome);
	if (!Rig.IsUsable(*this) || !Long.IsUsable(*this))
	{
		return false;
	}
	Rig.Holds({SkillTrapFlag(TEXT("Slot.Heavy"))});
	Long.Holds({SkillTrapFlag(TEXT("Slot.Heavy")), SkillTrapIncrease(TEXT("minion_duration"), 50.0f, TEXT("Type.Trap"))});
	const auto At = [](int32 Index) { return FVector(0.0f, 10.0f * M * static_cast<float>(Index), 0.0f); };
	ACataclysmEnemyCharacter* ByTheOldest = SkillTrapEnemy(World, At(1) + FVector(2.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* ByTheSecond = SkillTrapEnemy(World, At(2) + FVector(2.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the enemy 2 m from the oldest trap"), ByTheOldest)
		|| !TestNotNull(TEXT("set-up: the enemy 2 m from the second trap"), ByTheSecond))
	{
		return false;
	}
	TArray<ACataclysmMinion*> Five;
	for (int32 Index = 1; Index <= 5; ++Index)
	{
		ACataclysmMinion* Made = SkillTrapPlace(*this, Rig, ECataclysmAbilitySlot::Heavy, At(Index));
		if (!Made)
		{
			return false;
		}
		Five.Add(Made);
	}
	ACataclysmMinion* LongTrap = SkillTrapPlace(*this, Long, ECataclysmAbilitySlot::Heavy, LongHome + At(1));
	if (!LongTrap)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> Oldest(Five[0]);
	const TWeakObjectPtr<ACataclysmMinion> Second(Five[1]);
	FSkillTrapEventCount Triggers(Rig.System, TEXT("trap_triggered"));

	// THE CONTROL, READ FIRST: FIVE STAND, THE OLDEST FIRST.
	TestEqual(TEXT("control: five traps stand, the most (T6)"), ACataclysmMinion::SkillTrapsOf(Rig.Player).Num(), 5);
	TestTrue(TEXT("control: the oldest is the first placed"), ACataclysmMinion::SkillTrapsOf(Rig.Player)[0] == Five[0]);

	// THE SIXTH.
	SkillTrapPass(World, 1.25f);
	ACataclysmMinion* Sixth = SkillTrapPlace(*this, Rig, ECataclysmAbilitySlot::Heavy, At(6));
	if (!Sixth)
	{
		return false;
	}
	const TArray<ACataclysmMinion*> After = ACataclysmMinion::SkillTrapsOf(Rig.Player);
	TestEqual(TEXT("with a sixth placed, five still stand"), After.Num(), 5);
	TestTrue(TEXT("the oldest is gone"), SkillTrapGone(Oldest));
	TestTrue(TEXT("and the sixth stands"), After.Contains(Sixth));
	TestEqual(TEXT("the oldest did not trigger as it went: the enemy 2 m from it is untouched"),
		SkillTrapHealthOf(ByTheOldest), SkillTrapEnemyHealth, 0.001f);
	TestEqual(TEXT("and no trap_triggered was raised"), Triggers.Count, 0);

	// THE EXPIRY (T4): the second trap, old enough to be armed and with an enemy within 3 m, reaches the end of its life,
	// as the engine's lifespan timer would end it: a test world's timers never fire.
	TestEqual(TEXT("control: a trap lasts 20 s (T4)"), Five[1]->GetLifeSpan(), ACataclysmMinion::SkillTrapSeconds, 0.01f);
	TestEqual(TEXT("and the minion_duration row naming Type.Trap lengthens it to 30 s"), LongTrap->GetLifeSpan(),
		ACataclysmMinion::SkillTrapSeconds * 1.5f, 0.01f);
	Five[1]->LifeSpanExpired();
	TestTrue(TEXT("the second trap expired and is gone"), SkillTrapGone(Second));
	TestEqual(TEXT("A TRAP THAT EXPIRES DOES NOT TRIGGER (T4): the enemy 2 m from it is untouched"),
		SkillTrapHealthOf(ByTheSecond), SkillTrapEnemyHealth, 0.001f);
	TestEqual(TEXT("and no trap_triggered was raised"), Triggers.Count, 0);
	return true;
}

// ==========================================================================
// 6. The two events carry the trap's tags; a trap's death raises no gadget_destroyed (M2, M5, T14)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapEventsTest,
	"Cataclysm.Traps.ThePlacedAndTriggeredEventsAreRaisedOnceWithTheTrapsTagsAndATrapsDeathRaisesNoGadgetDestroyed")
{
	using namespace CataclysmSkillTrapTest;

	// TWO PLAYERS 100 M APART AT HALF MANA AND HALF HEALTH, EACH WITH A HEAVY TRAP 20 M NORTH AND AN ENEMY 2.5 M EAST OF
	// IT. Each holds two actions of a test's own: 10% of its mana on trap_placed and 10% of its health on trap_triggered.
	// The tagged player's require Type.Trap; the control's require Type.Summon, which no trap carries. A KILLER 30 M SOUTH
	// OF THE TAGGED PLAYER, with a skill gadget and a third trap of that player's 3 m either side of it.
	FSkillTrapWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector ControlHome(100.0f * M, 0.0f, 0.0f);
	FSkillTrapRig Tagged(World, FVector::ZeroVector);
	FSkillTrapRig Control(World, ControlHome);
	if (!Tagged.IsUsable(*this) || !Control.IsUsable(*this))
	{
		return false;
	}
	const FGameplayAttribute Mana = UCataclysmVitalAttributeSet::GetManaAttribute();
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	for (const FSkillTrapRig* Each : {&Tagged, &Control})
	{
		Each->Holds({SkillTrapFlag(TEXT("Slot.Heavy"))});
		Each->Set(Mana, 500.0f);
		Each->Set(Health, 5000.0f);
	}
	Tagged.System->SetPoolActions({SkillTrapRestores(TEXT("trap_placed"), TEXT("mana"), TEXT("Type.Trap")),
								   SkillTrapRestores(TEXT("trap_triggered"), TEXT("health"), TEXT("Type.Trap"))});
	Control.System->SetPoolActions({SkillTrapRestores(TEXT("trap_placed"), TEXT("mana"), TEXT("Type.Summon")),
									SkillTrapRestores(TEXT("trap_triggered"), TEXT("health"), TEXT("Type.Summon"))});
	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector East(2.5f * M, 0.0f, 0.0f);
	const FVector South(0.0f, -30.0f * M, 0.0f);
	ACataclysmEnemyCharacter* TaggedEnemy = SkillTrapEnemy(World, North + East);
	ACataclysmEnemyCharacter* ControlEnemy = SkillTrapEnemy(World, ControlHome + North + East);
	ACataclysmEnemyCharacter* Killer = SkillTrapEnemy(World, South);
	if (!TestNotNull(TEXT("set-up: the tagged player's enemy"), TaggedEnemy)
		|| !TestNotNull(TEXT("set-up: the control's enemy"), ControlEnemy)
		|| !TestNotNull(TEXT("set-up: the killer"), Killer))
	{
		return false;
	}
	FSkillTrapEventCount TaggedPlaced(Tagged.System, TEXT("trap_placed"));
	FSkillTrapEventCount TaggedTriggered(Tagged.System, TEXT("trap_triggered"));
	FSkillTrapEventCount ControlPlaced(Control.System, TEXT("trap_placed"));
	FSkillTrapEventCount ControlTriggered(Control.System, TEXT("trap_triggered"));
	FSkillTrapEventCount Destroyed(Tagged.System, TEXT("gadget_destroyed"));

	// THE CONTROL, READ FIRST.
	ACataclysmMinion* ControlTrap = SkillTrapPlace(*this, Control, ECataclysmAbilitySlot::Heavy, ControlHome + North);
	ACataclysmMinion* TaggedTrap = SkillTrapPlace(*this, Tagged, ECataclysmAbilitySlot::Heavy, North);
	if (!ControlTrap || !TaggedTrap)
	{
		return false;
	}
	TestEqual(TEXT("control: placing raised trap_placed once"), ControlPlaced.Count, 1);
	TestEqual(TEXT("control: and its row requiring Type.Summon did not fire"), Control.Get(Mana), 500.0f, 0.01f);
	TestEqual(TEXT("placing raised trap_placed once on the tagged player"), TaggedPlaced.Count, 1);
	TestEqual(TEXT("and its row requiring Type.Trap fired: the event carries the trap's tags"), Tagged.Get(Mana), 600.0f,
		0.01f);

	SkillTrapPass(World, 1.25f);
	SkillTrapStep(ControlTrap);
	SkillTrapStep(TaggedTrap);
	TestEqual(TEXT("control: triggering raised trap_triggered once"), ControlTriggered.Count, 1);
	TestEqual(TEXT("control: and its row requiring Type.Summon did not fire"), Control.Get(Health), 5000.0f, 0.01f);
	TestEqual(TEXT("triggering raised trap_triggered once on the tagged player"), TaggedTriggered.Count, 1);
	TestEqual(TEXT("and its row requiring Type.Trap fired: the event carries the trap's tags"), Tagged.Get(Health),
		6000.0f, 0.01f);

	// A KILLED SKILL GADGET RAISES gadget_destroyed (THE CONTROL); A KILLED SKILL TRAP RAISES NOTHING (T14).
	ACataclysmMinion* Gadget =
		ACataclysmMinion::PlaceSkillGadget(Tagged.Player, ECataclysmAbilitySlot::Ultimate, South + FVector(-3.0f * M, 0.0f, 0.0f));
	ACataclysmMinion* Doomed = SkillTrapPlace(*this, Tagged, ECataclysmAbilitySlot::Heavy, South + FVector(3.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a skill gadget, which needs the SkillGadget row"), Gadget) || !Doomed)
	{
		return false;
	}
	UCataclysmSkillEffects::ApplyDirectDamage(Killer, Gadget, 1000000.0f);
	TestTrue(TEXT("control: the skill gadget died"), UCataclysmSkillEffects::IsDead(Gadget));
	TestEqual(TEXT("control: and raised gadget_destroyed once"), Destroyed.Count, 1);
	UCataclysmSkillEffects::ApplyDirectDamage(Killer, Doomed, 1000000.0f);
	TestTrue(TEXT("the skill trap died"), UCataclysmSkillEffects::IsDead(Doomed));
	TestEqual(TEXT("and raised no gadget_destroyed (T14)"), Destroyed.Count, 1);
	TestEqual(TEXT("nor trap_triggered: a trap killed is not set off"), TaggedTriggered.Count, 1);
	return true;
}

// ==========================================================================
// 7. The deployable self-blast reaches a trap's area cast (interaction 1, B2 extended to traps)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapSelfBlastTest,
	"Cataclysm.Traps.ATrapsAreaCastStrikesItsSelfBlastWearerBesideTheTriggeringEnemyAndNotOneOutside")
{
	using namespace CataclysmSkillTrapTest;

	// TWO WEARERS 100 M APART, EACH HOLDING THE ULTIMATE TRAP FLAG AND deployable_blasts_strike_self AT 1. The inside
	// wearer's Ultimate trap stands 5 m north of it with its enemy 2.5 m east of the trap, 5.6 m from the wearer; the
	// outside wearer's stands 20 m north, its enemy 20.2 m from the wearer. The Gathering reaches 14 m around the enemy.
	FSkillTrapWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	if (!TestNotNull(TEXT("set-up: the critical strike roll is pinned at 100"), NeverCritical.Variable))
	{
		return false;
	}
	const FVector OutsideHome(100.0f * M, 0.0f, 0.0f);
	FSkillTrapRig Inside(World, FVector::ZeroVector);
	FSkillTrapRig Outside(World, OutsideHome);
	if (!Inside.IsUsable(*this) || !Outside.IsUsable(*this))
	{
		return false;
	}
	const FSkillTrapRow SelfBlast = {FName(UCataclysmDeployableSelfBlast::FlagStat), 1.0f, ECataclysmStatBucket::Flat,
									 nullptr};
	Inside.Holds({SkillTrapFlag(TEXT("Slot.Ultimate")), SelfBlast});
	Outside.Holds({SkillTrapFlag(TEXT("Slot.Ultimate")), SelfBlast});
	const FVector Near(0.0f, 5.0f * M, 0.0f);
	const FVector Far(0.0f, 20.0f * M, 0.0f);
	const FVector East(2.5f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* InsideEnemy = SkillTrapEnemy(World, Near + East);
	ACataclysmEnemyCharacter* OutsideEnemy = SkillTrapEnemy(World, OutsideHome + Far + East);
	if (!TestNotNull(TEXT("set-up: the inside trap's enemy"), InsideEnemy)
		|| !TestNotNull(TEXT("set-up: the outside trap's enemy"), OutsideEnemy))
	{
		return false;
	}
	ACataclysmMinion* InsideTrap = SkillTrapPlace(*this, Inside, ECataclysmAbilitySlot::Ultimate, Near);
	ACataclysmMinion* OutsideTrap = SkillTrapPlace(*this, Outside, ECataclysmAbilitySlot::Ultimate, OutsideHome + Far);
	if (!InsideTrap || !OutsideTrap)
	{
		return false;
	}
	TestTrue(TEXT("set-up: the inside wearer holds the flag"), UCataclysmDeployableSelfBlast::HeldBy(Inside.Player));
	TestTrue(TEXT("set-up: the outside wearer holds it"), UCataclysmDeployableSelfBlast::HeldBy(Outside.Player));
	TestTrue(TEXT("set-up: the inside trap is its wearer's deployable, which the blast sites ask"),
		UCataclysmDeployableSelfBlast::IsTheWearersDeployable(Inside.Player, InsideTrap));
	TestTrue(TEXT("set-up: and so is the outside trap"),
		UCataclysmDeployableSelfBlast::IsTheWearersDeployable(Outside.Player, OutsideTrap));
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();

	// THE CONTROL, READ FIRST: THE OUTSIDE TRAP.
	SkillTrapPass(World, 1.25f);
	SkillTrapStep(OutsideTrap);
	TestTrue(TEXT("control: the outside trap's cast struck its enemy"), SkillTrapTakenFrom(OutsideEnemy) > 0.0f);
	TestEqual(TEXT("control: and the wearer 20.2 m from that enemy loses nothing"), Outside.Get(Health), 10000.0f,
		0.001f);

	// THE INSIDE TRAP.
	SkillTrapStep(InsideTrap);
	TestTrue(TEXT("the inside trap's cast struck its enemy"), SkillTrapTakenFrom(InsideEnemy) > 0.0f);
	TestTrue(*FString::Printf(TEXT("and struck the wearer 5.6 m from that enemy, inside the cast centered on it: it "
								   "has %.2f of 10000"), Inside.Get(Health)),
		Inside.Get(Health) < 10000.0f);
	return true;
}

// ==========================================================================
// 8. What the player sees: the box and the tint (M7, T8, T12)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapBarAndTintTest,
	"Cataclysm.Traps.TheSkillBarBoxReadsTrapCountsTheTrapsSweepsWithTheCooldownAndATrapIsDimUntilArmed")
{
	using namespace CataclysmSkillTrapTest;

	// ONE PLAYER AT THE ORIGIN, NO ENEMY. Its Heavy press pays from its 1,000 mana.
	FSkillTrapWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FSkillTrapRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: THE BOX OF A SLOT NO ROW CONVERTS.
	const FCataclysmSkillBarSlot Plain = SkillTrapBox(Rig.Player, ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("control: the Heavy box is drawn"), Plain.Slot == ECataclysmAbilitySlot::Heavy && Plain.bFilled);
	TestFalse(TEXT("control: it is not a trap's"), Plain.bIsASkillTrap);
	TestEqual(TEXT("control: and has no words over it"), UCataclysmSkillBar::SkillTrapTextFor(Plain), FString());

	Rig.Holds({SkillTrapFlag(TEXT("Slot.Heavy"))});
	const FCataclysmSkillBarSlot Converted = SkillTrapBox(Rig.Player, ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("converted, the Heavy box is a trap's"), Converted.bIsASkillTrap);
	TestEqual(TEXT("it reads Trap"), Converted.Name, UCataclysmSkillBar::SkillTrapName());
	TestEqual(TEXT("and the traps standing over the most, none yet"), UCataclysmSkillBar::SkillTrapTextFor(Converted),
		FString(TEXT("Traps 0/5")));
	TestEqual(TEXT("with no sweep before a press"), Converted.CooldownRemaining, 0.0f, 0.001f);
	TestFalse(TEXT("the Special box, which no row converts, is not a trap's"),
		SkillTrapBox(Rig.Player, ECataclysmAbilitySlot::Special).bIsASkillTrap);

	// A PRESS.
	Rig.KeyDown(ECataclysmAbilitySlot::Heavy);
	Rig.KeyUp(ECataclysmAbilitySlot::Heavy);
	const TArray<ACataclysmMinion*> Placed = ACataclysmMinion::SkillTrapsOf(Rig.Player);
	if (Placed.Num() != 1)
	{
		AddError(TEXT("set-up: the press placed no skill trap. DT_MinionTypes needs the SkillTrap row, which the "
					  "enchantment session authors: run tools/generate_datatable_assets.py"));
		return false;
	}
	const FCataclysmSkillBarSlot Pressed = SkillTrapBox(Rig.Player, ECataclysmAbilitySlot::Heavy);
	TestEqual(TEXT("after a press it counts one"), UCataclysmSkillBar::SkillTrapTextFor(Pressed), FString(TEXT("Traps 1/5")));
	TestTrue(*FString::Printf(TEXT("and sweeps with the Heavy cooldown the press started: %.2f of %.2f s left"),
							  Pressed.CooldownRemaining, Pressed.CooldownDuration),
		Pressed.CooldownRemaining > 0.0f && FMath::IsNearlyEqual(Pressed.CooldownRemaining, Pressed.CooldownDuration, 0.01f));
	FCataclysmSkillBarSlot Never;
	Never.bIsASkillTrap = true;
	Never.bSkillTrapCastsNothing = true;
	TestEqual(TEXT("a trap's box whose skill can never be cast reads so (T8)"), UCataclysmSkillBar::SkillTrapTextFor(Never),
		FString(TEXT("Trap casts nothing")));

	// THE TINT (T12).
	ACataclysmMinion* Trap = Placed[0];
	TestFalse(TEXT("control: the two tints differ"),
		ACataclysmMinion::SkillTrapTintFor(true).Equals(ACataclysmMinion::SkillTrapTintFor(false)));
	TestTrue(TEXT("placed, the trap is drawn dim while it arms"),
		Trap->SkillTrapTintShown.Equals(ACataclysmMinion::SkillTrapTintFor(false)));
	SkillTrapPass(World, 1.25f);
	SkillTrapStep(Trap);
	TestTrue(TEXT("a second and a quarter on, it is armed"), Trap->bSkillTrapArmed);
	TestTrue(TEXT("and drawn lit"), Trap->SkillTrapTintShown.Equals(ACataclysmMinion::SkillTrapTintFor(true)));
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
