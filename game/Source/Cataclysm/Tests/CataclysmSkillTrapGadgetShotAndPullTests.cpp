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
 * Skill traps, layer 6: a nearby gadget fires at a trap's enemy, every third trap an overcharged shot, and a trap pulls
 * enemies into its blast. Ruled 2026-10-22 by the coordinating session (J1 to J10, J2 changed: SKILL GADGETS ONLY), for
 * "When a trap is triggered near an active gadget, the gadget fires an immediate bonus shot at the triggering enemy",
 * "For every 3 traps triggered, your nearest gadget fires a massive overcharged shot dealing 200%-400% of its normal
 * damage" and "When a trap triggers, all enemies within 4-6 meters are pulled toward the blast center". In layer 1's
 * group, `Cataclysm.Traps.`, in a file of its own, numbered on from layer 5's last (44).
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER HOLDING A DEMONIC WHIP OR A DEMONIC DAGGER, given a weapon damage of 100,
 * 10,000 health and 1,000 mana by hand. The Whip's Heavy, Searing Lash, is a 5 m cone at 45 degrees struck at once, and
 * its Ultimate, The Gathering, a 14 m ring a skill gadget casts at once from its own place, hauling what it strikes to
 * the gadget. The Dagger's Heavy, Emberpierce, is a 2.5 m cone striking one enemy, so an enemy 2.8 m from a trap sets
 * it off and is not struck unless something brings it nearer. NO ROW IS AUTHORED, SO EVERY STAT AND EVERY ACTION IS
 * WRITTEN BY HAND, as the item would build it from its row. NO KEY IS PRESSED: every trap and gadget is placed
 * directly, and every cast is a free start. Every body stands at least 1 m from every other, on a real side: the
 * players and their traps and gadgets on the players' side, the creatures on the monsters'. NOTHING HERE WAITS ON A
 * TICK: the world's clock is moved by hand and a trap's step is called directly; a trap is ARMED only by a step, so a
 * trap that must be armed is stepped once before any enemy stands near it; no gadget is stepped, so no gadget casts by
 * its clock. Blows are struck only with the critical strike roll pinned, after the world is made. Helper types and
 * functions are named after the subject, a gadget's shot and a trap's pull.
 */
namespace CataclysmGadgetShotAndPullTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The health every creature here starts with, unless a case says otherwise: a lash takes more than a tenth of it. */
	constexpr float GadgetShotPullEnemyHealth = 1500.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FGadgetShotPullWorld
	{
		FGadgetShotPullWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FGadgetShotPullWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/**
	 * A creature of the monsters' side with no armour, evasion, block, resistance or crowd control resistance. It deals
	 * nothing of its own. `bBoss` makes it a Boss by its rarity first, which re-applies its starting figures, so the
	 * figures above are written after it.
	 */
	ACataclysmEnemyCharacter* GadgetShotPullEnemy(UWorld* World, const FVector& Where,
												  float Health = GadgetShotPullEnemyHealth, bool bBoss = false)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		UAbilitySystemComponent* Its = Made ? Made->GetAbilitySystemComponent() : nullptr;
		if (!Its)
		{
			return nullptr;
		}
		if (bBoss)
		{
			Made->SetRarityStep(ACataclysmEnemyCharacter::FirstBossRarityStep);
		}
		Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Made->SetHealth(Health);
		Made->SetArmour(0.0f);
		Made->SetAttackDamage(0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetCrowdControlResistanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Health);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
		return Made;
	}

	/** What a creature has lost since it was made at `Health`. */
	float GadgetShotPullTakenFrom(const AActor* Who, float Health = GadgetShotPullEnemyHealth)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Health - Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** How far `Who` stands from `Place` along the floor, in centimetres, as a float; -1 for nobody. */
	float GadgetShotPullFloorDistance(const AActor* Who, const FVector& Place)
	{
		return IsValid(Who) ? static_cast<float>(FVector::Dist2D(Who->GetActorLocation(), Place)) : -1.0f;
	}

	/** One row of a stat line a test's player holds. */
	struct FGadgetShotPullStat
	{
		FName Stat;
		float Value = 0.0f;
		/** The row's one required tag. */
		const TCHAR* RequiredTag = nullptr;
	};

	/** The Heavy's trap flag: a flat 1 on `skill_slot_is_a_trap` requiring `Slot.Heavy`, layer 1's row. */
	FGadgetShotPullStat GadgetShotPullHeavyTrapFlag()
	{
		return {FName(UCataclysmAbilitySystemComponent::SkillSlotIsATrapStat), 1.0f, TEXT("Slot.Heavy")};
	}

	/** A slot's gadget flag: a flat 1 on `skill_slot_is_a_gadget` requiring the slot's tag, the gadget's row. */
	FGadgetShotPullStat GadgetShotPullGadgetFlag(const TCHAR* SlotTag)
	{
		return {FName(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat), 1.0f, SlotTag};
	}

	/** "When a trap triggers, all enemies within 4-6 meters are pulled toward the blast center" rolled at `Metres`. */
	FGadgetShotPullStat GadgetShotPullRadius(float Metres)
	{
		return {FName(ACataclysmMinion::TrapPullRadiusStat), Metres, TEXT("Type.Trap")};
	}

	/** A possessed player character holding a Demonic `Weapon`, its ability system and its weapon slots. */
	struct FGadgetShotPullRig
	{
		FGadgetShotPullRig(UWorld* World, const FVector& Where, const TCHAR* Weapon)
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
			Slots->EquipWeaponType(Weapon);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		}

		/** Whether everything was made and the weapon holds a Heavy Strike a trap may cast. Reports why not. */
		bool IsUsable(FAutomationTestBase& Test) const
		{
			const FCataclysmWeaponSkill* Heavy = Slots ? Slots->GetAvailableSkills().FindByPredicate(
				[](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == ECataclysmAbilitySlot::Heavy; }) : nullptr;
			if (!Player || !System || !Slots || !Heavy || !ACataclysmMinion::SkillGadgetMayCast(*Heavy)
				|| Heavy->Shape != ECataclysmSkillShape::Strike)
			{
				Test.AddError(TEXT("set-up: a possessed player holding a Demonic weapon with a Heavy Strike a trap may "
								   "cast. If a row is missing, DT_WeaponSkills may be older than game/Data/WeaponSkills.csv: "
								   "run tools/generate_datatable_assets.py"));
				return false;
			}
			return true;
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			System->SetNumericAttributeBase(Attribute, Value);
		}

		/** REPLACES THE WHOLE STAT LINE with these rows, each a flat figure. */
		void Holds(const TArray<FGadgetShotPullStat>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FGadgetShotPullStat& Each : Rows)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::Enchantment;
				Row.Value = Each.Value;
				Row.RequiredTags.AddTag(
					FGameplayTag::RequestGameplayTag(FName(Each.RequiredTag), /*ErrorIfNotFound=*/false));
				FCataclysmStatInputs& Line = Stats.FindOrAdd(Each.Stat);
				Line.Base = 0.0f;
				Line.Modifiers.Add(Row);
			}
			System->SetStatInputs(MoveTemp(Stats));
		}

		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
	};

	/** A skill trap for this rig's Heavy slot at `Where`, or null with the reason reported. */
	ACataclysmMinion* GadgetShotPullPlaceTrap(FAutomationTestBase& Test, const FGadgetShotPullRig& Rig, const FVector& Where)
	{
		ACataclysmMinion* Made = ACataclysmMinion::PlaceSkillTrap(Rig.Player, ECataclysmAbilitySlot::Heavy, Where);
		if (!Made)
		{
			Test.AddError(TEXT("set-up: no skill trap was placed. DT_MinionTypes needs the SkillTrap row: run "
							   "tools/generate_datatable_assets.py"));
		}
		return Made;
	}

	/** A skill gadget for this rig's `Slot` at `Where`, or null with the reason reported. */
	ACataclysmMinion* GadgetShotPullPlaceGadget(FAutomationTestBase& Test, const FGadgetShotPullRig& Rig,
												ECataclysmAbilitySlot Slot, const FVector& Where)
	{
		ACataclysmMinion* Made = ACataclysmMinion::PlaceSkillGadget(Rig.Player, Slot, Where);
		if (!Made)
		{
			Test.AddError(TEXT("set-up: no skill gadget was placed. DT_MinionTypes needs the SkillGadget row: run "
							   "tools/generate_datatable_assets.py"));
		}
		return Made;
	}

	/** Move the world's clock forward by hand. No timer fires and nobody thinks. */
	void GadgetShotPullPass(UWorld* World, float Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/** Run a trap's quarter-second step, as `RegenerationStep` calls it: it arms a trap whose time has come. */
	void GadgetShotPullStep(ACataclysmMinion* Trap)
	{
		if (IsValid(Trap))
		{
			ACataclysmMinion::GadgetStep(Trap);
		}
	}

	/** Whether a trap is gone: destroyed, or on its way out. */
	bool GadgetShotPullGone(const TWeakObjectPtr<ACataclysmMinion>& Trap)
	{
		return !Trap.IsValid() || Trap->IsActorBeingDestroyed();
	}

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FGadgetShotPullEventCount
	{
		FGadgetShotPullEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
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
		~FGadgetShotPullEventCount()
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

	/**
	 * This layer's action on `trap_triggered`, as the item builds it from its row, requiring `Type.Trap`: `Percent` the
	 * rolled "more" in per cent, `Metres` the row's Event Value (nought, any distance), `EveryNth` its count (nought,
	 * every trigger) with no window. No wait between firings (J9).
	 */
	FCataclysmPoolAction GadgetShotPullShotRow(float Percent, float Metres, int32 EveryNth)
	{
		FCataclysmPoolAction Row;
		Row.Event = FName(TEXT("trap_triggered"));
		Row.Pool = FName(UCataclysmAbilitySystemComponent::GadgetFiresAtTrapEnemyAction);
		Row.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false));
		Row.TriggerKey = FName(TEXT("GadgetShotPullTest:gadget_fires_at_trap_enemy:trap_triggered"));
		Row.TriggerCooldownSeconds = 0.0f;
		Row.Percent = Percent;
		Row.EventValue = Metres;
		Row.EveryNth = EveryNth;
		Row.CountWindowSeconds = 0.0f;
		Row.bGadgetFiresAtTrapEnemy = true;
		return Row;
	}

	/** "Traps trigger simultaneously when any one of them is triggered, if within 8-12 meters" at 10 m, layer 5's row. */
	FCataclysmPoolAction GadgetShotPullChainRow()
	{
		FCataclysmPoolAction Row;
		Row.Event = FName(TEXT("trap_triggered"));
		Row.Pool = FName(UCataclysmAbilitySystemComponent::TriggerTrapsNearbyAction);
		Row.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false));
		Row.TriggerKey = FName(TEXT("GadgetShotPullTest:trigger_traps_nearby:trap_triggered"));
		Row.TriggerCooldownSeconds = 0.0f;
		Row.Percent = 10.0f;
		Row.bSetOffTrapsNearby = true;
		return Row;
	}

	/** A row of the test's own granting one own stack under `Key` on `deployable_hit` requiring `Type.Deployable`. */
	FCataclysmPoolAction GadgetShotPullGadgetHitStacks(FName Key)
	{
		FCataclysmPoolAction Grant;
		Grant.Event = FName(TEXT("deployable_hit"));
		Grant.RequiredTags.AddTag(
			FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Deployable")), /*ErrorIfNotFound=*/false));
		Grant.StackKey = Key;
		Grant.StackSeconds = 10.0f;
		Grant.StackCap = 10;
		Grant.TriggerKey = FName(*FString::Printf(TEXT("%s:deployable_hit"), *Key.ToString()));
		return Grant;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 45. "When a trap is triggered near an active gadget, the gadget fires an immediate bonus shot at the triggering
// enemy" (J1, J2, J3)
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetShotFiresOnceTest,
	"Cataclysm.Traps.ATrapTriggeredWithinTenMetresOfAGadgetMakesItFireOnceAtTheTriggeringEnemyAsItsOwnHitItsClockUnchanged")
{
	using namespace CataclysmGadgetShotAndPullTest;

	// TWO PLAYERS 100 M APART HOLDING DEMONIC WHIPS, EACH WITH THE HEAVY TRAP FLAG AND THE ULTIMATE GADGET FLAG: a trap 20 m
	// north with its enemy 2.5 m east, and a skill gadget 6 m west of the trap. Both hold a row of the test's own granting
	// one stack on `deployable_hit` requiring `Type.Deployable`, which a trap's own blow does not carry (Q-A of
	// 2026-10-20). The control holds nothing else; the other the shot row at 100, within 10 m.
	FGadgetShotPullWorld Scope;
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
	const FVector RowHome(100.0f * M, 0.0f, 0.0f);
	FGadgetShotPullRig Plain(World, FVector::ZeroVector, TEXT("Whip"));
	FGadgetShotPullRig Shooting(World, RowHome, TEXT("Whip"));
	if (!Plain.IsUsable(*this) || !Shooting.IsUsable(*this))
	{
		return false;
	}
	const FName HitKey(TEXT("GadgetShotPullTest:GadgetHitStacks"));
	for (const FGadgetShotPullRig* Each : {&Plain, &Shooting})
	{
		Each->Holds({GadgetShotPullHeavyTrapFlag(), GadgetShotPullGadgetFlag(TEXT("Slot.Ultimate"))});
	}
	Plain.System->SetPoolActions({GadgetShotPullGadgetHitStacks(HitKey)});
	Shooting.System->SetPoolActions({GadgetShotPullShotRow(100.0f, 10.0f, 0), GadgetShotPullGadgetHitStacks(HitKey)});

	const FVector TrapAt(0.0f, 20.0f * M, 0.0f);
	const FVector GadgetAt = TrapAt + FVector(-6.0f * M, 0.0f, 0.0f);
	ACataclysmMinion* PlainTrap = GadgetShotPullPlaceTrap(*this, Plain, TrapAt);
	ACataclysmMinion* RowTrap = GadgetShotPullPlaceTrap(*this, Shooting, RowHome + TrapAt);
	if (!PlainTrap || !RowTrap)
	{
		return false;
	}
	// ARMED BY A STEP BEFORE ANY ENEMY STANDS NEAR: each arms and finds nobody.
	GadgetShotPullPass(World, 1.25f);
	GadgetShotPullStep(PlainTrap);
	GadgetShotPullStep(RowTrap);
	ACataclysmMinion* PlainGadget = GadgetShotPullPlaceGadget(*this, Plain, ECataclysmAbilitySlot::Ultimate, GadgetAt);
	ACataclysmMinion* RowGadget =
		GadgetShotPullPlaceGadget(*this, Shooting, ECataclysmAbilitySlot::Ultimate, RowHome + GadgetAt);
	if (!PlainGadget || !RowGadget
		|| !TestTrue(TEXT("set-up: each player's trap is armed"), PlainTrap->bSkillTrapArmed && RowTrap->bSkillTrapArmed))
	{
		return false;
	}
	// THE GADGET'S CLOCK AS IT WOULD STAND HALF A SECOND AFTER A CAST OF ITS OWN, written by hand, since no gadget is
	// stepped here: the shot must leave both as they are.
	RowGadget->bSkillGadgetHasCast = true;
	RowGadget->LastSkillGadgetCastAtAgeSeconds = 0.5f;
	const FVector East(2.5f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* PlainSetter = GadgetShotPullEnemy(World, TrapAt + East);
	ACataclysmEnemyCharacter* RowSetter = GadgetShotPullEnemy(World, RowHome + TrapAt + East);
	if (!TestNotNull(TEXT("set-up: the control's enemy"), PlainSetter)
		|| !TestNotNull(TEXT("set-up: the shooting player's enemy"), RowSetter))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: ITS TRAP GOES OFF, ITS GADGET FIRES NOTHING, AND THE TRAP'S OWN BLOW GRANTS NO GADGET STACK.
	GadgetShotPullStep(PlainTrap);
	TestEqual(TEXT("control: the trap triggered once"), PlainTrap->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("control: with no row its gadget fired no shot"), PlainGadget->SkillGadgetTrapShotsMade, 0);
	TestEqual(TEXT("control: and cast nothing"), PlainGadget->SkillGadgetCastsMade, 0);
	const float LashAlone = GadgetShotPullTakenFrom(PlainSetter);
	TestTrue(*FString::Printf(TEXT("control: the trap's lash struck its enemy: %.2f"), LashAlone), LashAlone > 1.0f);
	TestEqual(TEXT("control: the trap's own blow answers no row requiring Type.Deployable"),
		Plain.System->OwnStacksHeld(HitKey), 0);

	// THE ROW: THE GADGET 6 M FROM THE TRAP FIRES ONCE AT THE ENEMY THAT SET IT OFF, AS ITS OWN HIT.
	GadgetShotPullStep(RowTrap);
	TestEqual(TEXT("the shooting player's trap triggered once"), RowTrap->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("its gadget 6 m away fired one shot (J1)"), RowGadget->SkillGadgetTrapShotsMade, 1);
	TestEqual(TEXT("by its clock's own path, counted as one of its casts (J2)"), RowGadget->SkillGadgetCastsMade, 1);
	TestTrue(*FString::Printf(TEXT("the shot struck the enemy that set the trap off, on top of the lash (J3): %.2f "
								   "against the lash alone, %.2f"), GadgetShotPullTakenFrom(RowSetter), LashAlone),
		GadgetShotPullTakenFrom(RowSetter) > LashAlone + 1.0f);
	TestTrue(*FString::Printf(TEXT("the shot is the gadget's hit: its deployable_hit answered the row requiring "
								   "Type.Deployable, at depth nought (J2): %d stacks"), Shooting.System->OwnStacksHeld(HitKey)),
		Shooting.System->OwnStacksHeld(HitKey) >= 1);
	TestTrue(TEXT("its own clock is unchanged: it still counts as having cast"), RowGadget->bSkillGadgetHasCast);
	TestEqual(TEXT("and its last cast is still the one half a second in"), RowGadget->LastSkillGadgetCastAtAgeSeconds, 0.5f);
	return true;
}

// ==========================================================================
// 46. The nearest gadget within 10 m fires, and none 12 m away, nor for a trap with no triggering enemy (J1, J3)
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetShotNearestOnlyTest,
	"Cataclysm.Traps.OnlyTheNearestGadgetWithinTenMetresFiresAndNoneTwelveMetresAwayOrForATrapWithNoTriggeringEnemy")
{
	using namespace CataclysmGadgetShotAndPullTest;

	// TWO PLAYERS 100 M APART HOLDING DEMONIC WHIPS, EACH WITH THE SHOT ROW AT 100 WITHIN 10 M. The near player: a trap 20 m
	// north with its enemy 2.5 m east, an Ultimate gadget 4 m west of it and a Support gadget 7 m south of it, and a
	// second trap 8 m west of the first, 4 m from the Ultimate gadget, with no enemy within 10 m of it. The far player: a
	// trap with its enemy 2.5 m east and an Ultimate gadget 12 m west of it.
	FGadgetShotPullWorld Scope;
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
	const FVector FarHome(100.0f * M, 0.0f, 0.0f);
	FGadgetShotPullRig Near(World, FVector::ZeroVector, TEXT("Whip"));
	FGadgetShotPullRig Far(World, FarHome, TEXT("Whip"));
	if (!Near.IsUsable(*this) || !Far.IsUsable(*this))
	{
		return false;
	}
	Near.Holds({GadgetShotPullHeavyTrapFlag(), GadgetShotPullGadgetFlag(TEXT("Slot.Ultimate")),
				GadgetShotPullGadgetFlag(TEXT("Slot.Support"))});
	Far.Holds({GadgetShotPullHeavyTrapFlag(), GadgetShotPullGadgetFlag(TEXT("Slot.Ultimate"))});
	for (const FGadgetShotPullRig* Each : {&Near, &Far})
	{
		Each->System->SetPoolActions({GadgetShotPullShotRow(100.0f, 10.0f, 0)});
	}

	const FVector TrapAt(0.0f, 20.0f * M, 0.0f);
	const FVector LoneTrapAt = TrapAt + FVector(-8.0f * M, 0.0f, 0.0f);
	ACataclysmMinion* NearTrap = GadgetShotPullPlaceTrap(*this, Near, TrapAt);
	ACataclysmMinion* LoneTrap = GadgetShotPullPlaceTrap(*this, Near, LoneTrapAt);
	ACataclysmMinion* FarTrap = GadgetShotPullPlaceTrap(*this, Far, FarHome + TrapAt);
	if (!NearTrap || !LoneTrap || !FarTrap)
	{
		return false;
	}
	GadgetShotPullPass(World, 1.25f);
	GadgetShotPullStep(NearTrap);
	GadgetShotPullStep(FarTrap);
	ACataclysmMinion* NearUltimate = GadgetShotPullPlaceGadget(*this, Near, ECataclysmAbilitySlot::Ultimate,
															   TrapAt + FVector(-4.0f * M, 0.0f, 0.0f));
	ACataclysmMinion* NearSupport = GadgetShotPullPlaceGadget(*this, Near, ECataclysmAbilitySlot::Support,
															  TrapAt + FVector(0.0f, -7.0f * M, 0.0f));
	ACataclysmMinion* FarUltimate = GadgetShotPullPlaceGadget(*this, Far, ECataclysmAbilitySlot::Ultimate,
															  FarHome + TrapAt + FVector(-12.0f * M, 0.0f, 0.0f));
	if (!NearUltimate || !NearSupport || !FarUltimate
		|| !TestTrue(TEXT("set-up: each player's first trap is armed"), NearTrap->bSkillTrapArmed && FarTrap->bSkillTrapArmed))
	{
		return false;
	}
	const FVector East(2.5f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* NearSetter = GadgetShotPullEnemy(World, TrapAt + East);
	ACataclysmEnemyCharacter* FarSetter = GadgetShotPullEnemy(World, FarHome + TrapAt + East);
	if (!TestNotNull(TEXT("set-up: the near player's enemy"), NearSetter)
		|| !TestNotNull(TEXT("set-up: the far player's enemy"), FarSetter))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: A TRAP SET OFF WITH NO TRIGGERING ENEMY (T11) WITHIN 4 M OF A GADGET: NO SHOT (J3).
	TestTrue(TEXT("control: the second trap went off with no triggering enemy (T11)"), LoneTrap->TriggerSkillTrap(nullptr));
	TestEqual(TEXT("control: with no triggering enemy the gadget 4 m away fired no shot (J3)"),
		NearUltimate->SkillGadgetTrapShotsMade, 0);

	// THE NEAREST OF TWO WITHIN 10 M FIRES, AND THE OTHER DOES NOT (J1: one gadget).
	GadgetShotPullStep(NearTrap);
	TestEqual(TEXT("the near player's first trap triggered once"), NearTrap->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("the Ultimate gadget 4 m away, the nearest, fired one shot"), NearUltimate->SkillGadgetTrapShotsMade, 1);
	TestEqual(TEXT("the Support gadget 7 m away, also within 10 m, fired none: one gadget (J1)"),
		NearSupport->SkillGadgetTrapShotsMade, 0);

	// A GADGET 12 M AWAY IS NOT NEAR (J1).
	GadgetShotPullStep(FarTrap);
	TestEqual(TEXT("the far player's trap triggered once"), FarTrap->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("its gadget 12 m away fired no shot (J1)"), FarUltimate->SkillGadgetTrapShotsMade, 0);
	TestEqual(TEXT("and cast nothing"), FarUltimate->SkillGadgetCastsMade, 0);
	return true;
}

// ==========================================================================
// 47. "For every 3 traps triggered, your nearest gadget fires a massive overcharged shot dealing 200%-400% of its
// normal damage" (J4, J5, J6)
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetShotEveryThirdTest,
	"Cataclysm.Traps.EveryThirdTrapTriggeredFiresTheNearestGadgetAtTheRowsMultipleOfItsNormalBlowAndTheCountStartsAgain")
{
	using namespace CataclysmGadgetShotAndPullTest;

	// ONE PLAYER HOLDING A DEMONIC WHIP WITH THE HEAVY TRAP FLAG, THE ULTIMATE GADGET FLAG AND THE SHOT ROW AT 300, ANY
	// DISTANCE, EVERY THIRD TRAP. A skill gadget 50 m north of the player with a sturdy measure 3 m west of it; four traps,
	// each with its enemy 2.5 m east of it: the first 20 m north of the gadget, the second 30 m north, the third 6 m east
	// of the gadget (its enemy 8.5 m from the gadget, inside The Gathering's 14 m), the fourth 20 m south. Only the third's
	// enemy and the measure stand within 14 m of the gadget.
	FGadgetShotPullWorld Scope;
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
	FGadgetShotPullRig Counting(World, FVector::ZeroVector, TEXT("Whip"));
	if (!Counting.IsUsable(*this))
	{
		return false;
	}
	const FCataclysmPoolAction EveryThird = GadgetShotPullShotRow(300.0f, 0.0f, 3);
	Counting.Holds({GadgetShotPullHeavyTrapFlag(), GadgetShotPullGadgetFlag(TEXT("Slot.Ultimate"))});
	Counting.System->SetPoolActions({EveryThird});

	const FVector GadgetAt(0.0f, 50.0f * M, 0.0f);
	const FVector TrapsAt[] = {GadgetAt + FVector(0.0f, 20.0f * M, 0.0f), GadgetAt + FVector(0.0f, 30.0f * M, 0.0f),
							   GadgetAt + FVector(6.0f * M, 0.0f, 0.0f), GadgetAt + FVector(0.0f, -20.0f * M, 0.0f)};
	TArray<ACataclysmMinion*> Traps;
	for (const FVector& At : TrapsAt)
	{
		ACataclysmMinion* Placed = GadgetShotPullPlaceTrap(*this, Counting, At);
		if (!Placed)
		{
			return false;
		}
		Traps.Add(Placed);
	}
	GadgetShotPullPass(World, 1.25f);
	for (ACataclysmMinion* Each : Traps)
	{
		GadgetShotPullStep(Each);
	}
	if (!TestTrue(TEXT("set-up: all four traps are armed"), Traps[0]->bSkillTrapArmed && Traps[1]->bSkillTrapArmed
			&& Traps[2]->bSkillTrapArmed && Traps[3]->bSkillTrapArmed))
	{
		return false;
	}
	ACataclysmMinion* Gadget = GadgetShotPullPlaceGadget(*this, Counting, ECataclysmAbilitySlot::Ultimate, GadgetAt);
	constexpr float Sturdy = 10000.0f;
	ACataclysmEnemyCharacter* Measure = GadgetShotPullEnemy(World, GadgetAt + FVector(-3.0f * M, 0.0f, 0.0f), Sturdy);
	if (!Gadget || !TestNotNull(TEXT("set-up: the measure"), Measure))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: THE SAME GADGET'S NORMAL BLOW, ITS CLOCK'S OWN CAST, ON THE MEASURE.
	if (!TestTrue(TEXT("control: the gadget cast its skill by its clock's own path"), Gadget->CastPlayersSkillFromHere()))
	{
		return false;
	}
	const float NormalBlow = GadgetShotPullTakenFrom(Measure, Sturdy);
	if (!TestTrue(*FString::Printf(TEXT("control: its normal blow struck the measure: %.2f"), NormalBlow), NormalBlow > 1.0f))
	{
		return false;
	}
	const FVector East(2.5f * M, 0.0f, 0.0f);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (!TestNotNull(*FString::Printf(TEXT("set-up: the enemy of trap %d"), Index + 1),
				GadgetShotPullEnemy(World, TrapsAt[Index] + East)))
		{
			return false;
		}
	}

	// THE FIRST AND SECOND TRAPS TRIGGERED FIRE NOTHING; THE THIRD FIRES THE NEAREST GADGET, AT ANY DISTANCE (J4, J5).
	GadgetShotPullStep(Traps[0]);
	TestEqual(TEXT("the first trap triggered"), Traps[0]->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("after the first, no shot"), Gadget->SkillGadgetTrapShotsMade, 0);
	GadgetShotPullStep(Traps[1]);
	TestEqual(TEXT("the second trap triggered"), Traps[1]->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("after the second, no shot"), Gadget->SkillGadgetTrapShotsMade, 0);
	GadgetShotPullStep(Traps[2]);
	TestEqual(TEXT("the third trap triggered"), Traps[2]->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("after the third, one shot (J4)"), Gadget->SkillGadgetTrapShotsMade, 1);

	// AT THE ROW'S MULTIPLE OF THE SAME GADGET'S NORMAL BLOW (J6), on the same measure. Both are the same blow of the
	// same weapon, so the Whip's Slashing step to health (`UCataclysmDamageCalculation::SubtypeBonus`) multiplies both
	// and cancels in the ratio; the measure has no defences and is far above nought, so nothing else bends it.
	const float Overcharged = GadgetShotPullTakenFrom(Measure, Sturdy) - NormalBlow;
	const float Multiple = EveryThird.Percent / 100.0f;
	TestEqual(*FString::Printf(TEXT("the overcharged shot struck the measure for the row's %.0f%% of the gadget's normal "
									"blow (J6): %.2f against %.2f"), EveryThird.Percent, Overcharged, NormalBlow * Multiple),
		Overcharged, NormalBlow * Multiple, NormalBlow * Multiple * 0.001f);

	// AND THE COUNT STARTS AGAIN (J4): the fourth is the first of the next three.
	GadgetShotPullStep(Traps[3]);
	TestEqual(TEXT("the fourth trap triggered"), Traps[3]->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("after the fourth, still one shot: the count started again after the third"),
		Gadget->SkillGadgetTrapShotsMade, 1);
	return true;
}

// ==========================================================================
// 48. The count of traps triggered is emptied at death (J4)
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetShotCountEndsAtDeathTest,
	"Cataclysm.Traps.TheCountOfTrapsTriggeredTowardsAGadgetsShotIsEmptiedAtDeath")
{
	using namespace CataclysmGadgetShotAndPullTest;

	// ONE PLAYER HOLDING A DEMONIC WHIP WITH THE SHOT ROW AT 300, ANY DISTANCE, EVERY THIRD TRAP: a skill gadget 50 m north
	// and five traps on a line north of it, 10 m apart from 20 m, each with its enemy 2.5 m east. Two traps go off, then
	// what a death ends is ended as the respawn ends it (`ClearWhatDeathEnds`), then three more.
	FGadgetShotPullWorld Scope;
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
	FGadgetShotPullRig Counting(World, FVector::ZeroVector, TEXT("Whip"));
	if (!Counting.IsUsable(*this))
	{
		return false;
	}
	Counting.Holds({GadgetShotPullHeavyTrapFlag(), GadgetShotPullGadgetFlag(TEXT("Slot.Ultimate"))});
	Counting.System->SetPoolActions({GadgetShotPullShotRow(300.0f, 0.0f, 3)});

	const FVector GadgetAt(0.0f, 50.0f * M, 0.0f);
	TArray<ACataclysmMinion*> Traps;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		ACataclysmMinion* Placed =
			GadgetShotPullPlaceTrap(*this, Counting, GadgetAt + FVector(0.0f, (20.0f + 10.0f * Index) * M, 0.0f));
		if (!Placed)
		{
			return false;
		}
		Traps.Add(Placed);
	}
	GadgetShotPullPass(World, 1.25f);
	for (ACataclysmMinion* Each : Traps)
	{
		GadgetShotPullStep(Each);
	}
	ACataclysmMinion* Gadget = GadgetShotPullPlaceGadget(*this, Counting, ECataclysmAbilitySlot::Ultimate, GadgetAt);
	if (!Gadget || !TestTrue(TEXT("set-up: all five traps stand armed"), Traps.Num() == 5 && Traps[0]->bSkillTrapArmed
			&& Traps[1]->bSkillTrapArmed && Traps[2]->bSkillTrapArmed && Traps[3]->bSkillTrapArmed
			&& Traps[4]->bSkillTrapArmed))
	{
		return false;
	}
	for (int32 Index = 0; Index < 5; ++Index)
	{
		if (!TestNotNull(*FString::Printf(TEXT("set-up: the enemy of trap %d"), Index + 1),
				GadgetShotPullEnemy(World, Traps[Index]->GetActorLocation() + FVector(2.5f * M, 0.0f, 0.0f))))
		{
			return false;
		}
	}

	// TWO COUNTED, THEN THE DEATH EMPTIES THE COUNT: THE THIRD TRAP IS THE FIRST OF THREE AGAIN.
	GadgetShotPullStep(Traps[0]);
	GadgetShotPullStep(Traps[1]);
	TestEqual(TEXT("control: two traps triggered, no shot"), Gadget->SkillGadgetTrapShotsMade, 0);
	Counting.System->ClearWhatDeathEnds();
	GadgetShotPullStep(Traps[2]);
	TestEqual(TEXT("the third trap triggered"), Traps[2]->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("and fired no shot: the death emptied the count (J4)"), Gadget->SkillGadgetTrapShotsMade, 0);
	GadgetShotPullStep(Traps[3]);
	TestEqual(TEXT("after the fourth, still none"), Gadget->SkillGadgetTrapShotsMade, 0);
	GadgetShotPullStep(Traps[4]);
	TestEqual(TEXT("the fifth, the third since the death, fired one shot"), Gadget->SkillGadgetTrapShotsMade, 1);
	return true;
}

// ==========================================================================
// 49. "When a trap triggers, all enemies within 4-6 meters are pulled toward the blast center": pulled before the cast,
// so the cast strikes (J7, J8)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapPullBeforeItsCastTest,
	"Cataclysm.Traps.ATrapPullsAnEnemyWithinTheRolledMetresToOneMetreBeforeItsCastSoTheCastStrikesItAndNoneBeyond")
{
	using namespace CataclysmGadgetShotAndPullTest;

	// TWO PLAYERS 100 M APART HOLDING DEMONIC DAGGERS WITH THE HEAVY TRAP FLAG, whose Emberpierce strikes 2.5 m: a trap
	// 20 m north with its enemy 2.8 m east of it, inside the 3 m that sets it off and outside the 2.5 m it strikes, and a
	// bystander 6.5 m north of it. The control holds nothing else; the other the pull at 5 m.
	FGadgetShotPullWorld Scope;
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
	const FVector RowHome(100.0f * M, 0.0f, 0.0f);
	FGadgetShotPullRig Plain(World, FVector::ZeroVector, TEXT("Dagger"));
	FGadgetShotPullRig Pulling(World, RowHome, TEXT("Dagger"));
	if (!Plain.IsUsable(*this) || !Pulling.IsUsable(*this))
	{
		return false;
	}
	Plain.Holds({GadgetShotPullHeavyTrapFlag()});
	Pulling.Holds({GadgetShotPullHeavyTrapFlag(), GadgetShotPullRadius(5.0f)});

	const FVector TrapAt(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* PlainTrap = GadgetShotPullPlaceTrap(*this, Plain, TrapAt);
	ACataclysmMinion* RowTrap = GadgetShotPullPlaceTrap(*this, Pulling, RowHome + TrapAt);
	if (!PlainTrap || !RowTrap)
	{
		return false;
	}
	GadgetShotPullPass(World, 1.25f);
	GadgetShotPullStep(PlainTrap);
	GadgetShotPullStep(RowTrap);
	if (!TestTrue(TEXT("set-up: each player's trap is armed"), PlainTrap->bSkillTrapArmed && RowTrap->bSkillTrapArmed))
	{
		return false;
	}
	const FVector East(2.8f * M, 0.0f, 0.0f);
	const FVector North(0.0f, 6.5f * M, 0.0f);
	ACataclysmEnemyCharacter* PlainSetter = GadgetShotPullEnemy(World, TrapAt + East);
	ACataclysmEnemyCharacter* PlainBeyond = GadgetShotPullEnemy(World, TrapAt + North);
	ACataclysmEnemyCharacter* RowSetter = GadgetShotPullEnemy(World, RowHome + TrapAt + East);
	ACataclysmEnemyCharacter* RowBeyond = GadgetShotPullEnemy(World, RowHome + TrapAt + North);
	if (!TestNotNull(TEXT("set-up: the control's enemy"), PlainSetter)
		|| !TestNotNull(TEXT("set-up: the control's bystander"), PlainBeyond)
		|| !TestNotNull(TEXT("set-up: the pulling player's enemy"), RowSetter)
		|| !TestNotNull(TEXT("set-up: the pulling player's bystander"), RowBeyond))
	{
		return false;
	}
	FGadgetShotPullEventCount PlainControls(Plain.System, TEXT("crowd_control"));
	FGadgetShotPullEventCount RowControls(Pulling.System, TEXT("crowd_control"));

	// THE CONTROL, READ FIRST: THE TRAP GOES OFF, AND ITS 2.5 M STRIKE DOES NOT REACH THE ENEMY 2.8 M AWAY THAT SET IT OFF.
	GadgetShotPullStep(PlainTrap);
	TestEqual(TEXT("control: the trap triggered once"), PlainTrap->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("control: its enemy still stands 2.8 m from the trap"),
		GadgetShotPullFloorDistance(PlainSetter, TrapAt), 2.8f * M, 1.0f);
	TestEqual(TEXT("control: so the 2.5 m strike did not reach it"), GadgetShotPullTakenFrom(PlainSetter), 0.0f, 0.01f);
	TestEqual(TEXT("control: no crowd control was applied"), PlainControls.Count, 0);

	// THE ROW: THE ENEMY IS PULLED TO 1 M OF THE TRAP BEFORE ITS CAST, AND THE CAST STRIKES IT; THE ONE BEYOND STAYS.
	GadgetShotPullStep(RowTrap);
	const FVector RowTrapAt = RowHome + TrapAt;
	TestEqual(TEXT("the pulling player's trap triggered once"), RowTrap->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("its enemy, which set it off, was pulled to 1 m of the trap (J8)"),
		GadgetShotPullFloorDistance(RowSetter, RowTrapAt), ACataclysmMinion::SkillTrapPullsToCm, 1.0f);
	TestTrue(*FString::Printf(TEXT("and the trap's cast struck it there: pulled before the cast (J7): %.2f"),
								GadgetShotPullTakenFrom(RowSetter)), GadgetShotPullTakenFrom(RowSetter) > 1.0f);
	TestEqual(TEXT("the bystander 6.5 m away, beyond the 5 m, was not pulled"),
		GadgetShotPullFloorDistance(RowBeyond, RowTrapAt), 6.5f * M, 1.0f);
	TestEqual(TEXT("nor struck"), GadgetShotPullTakenFrom(RowBeyond), 0.0f, 0.01f);
	TestEqual(TEXT("one crowd control applied: the one pull"), RowControls.Count, 1);
	return true;
}

// ==========================================================================
// 50. Every enemy within the rolled metres is pulled, the one that set the trap off and a boss among them (J8)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapPullEveryEnemyTest,
	"Cataclysm.Traps.ATrapPullsEveryEnemyWithinTheRolledMetresTheOneThatSetItOffAndABossAmongThem")
{
	using namespace CataclysmGadgetShotAndPullTest;

	// ONE PLAYER HOLDING A DEMONIC DAGGER WITH THE HEAVY TRAP FLAG AND THE PULL AT 5 M: a trap 20 m north with its enemy
	// 2.8 m east, a Boss 4 m west and a bystander 6.5 m north. The Boss's body is larger (its rarity's body scale), so the
	// bodies may stop it short of 1 m: the rules of every displacement move a boss as any other (no boss exemption,
	// section VI of the design document), and stop at what a swept move meets.
	FGadgetShotPullWorld Scope;
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
	FGadgetShotPullRig Pulling(World, FVector::ZeroVector, TEXT("Dagger"));
	if (!Pulling.IsUsable(*this))
	{
		return false;
	}
	Pulling.Holds({GadgetShotPullHeavyTrapFlag(), GadgetShotPullRadius(5.0f)});

	const FVector TrapAt(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* Trap = GadgetShotPullPlaceTrap(*this, Pulling, TrapAt);
	if (!Trap)
	{
		return false;
	}
	GadgetShotPullPass(World, 1.25f);
	GadgetShotPullStep(Trap);
	if (!TestTrue(TEXT("set-up: the trap is armed"), Trap->bSkillTrapArmed))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Setter = GadgetShotPullEnemy(World, TrapAt + FVector(2.8f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Boss =
		GadgetShotPullEnemy(World, TrapAt + FVector(-4.0f * M, 0.0f, 0.0f), GadgetShotPullEnemyHealth, /*bBoss=*/true);
	ACataclysmEnemyCharacter* Beyond = GadgetShotPullEnemy(World, TrapAt + FVector(0.0f, 6.5f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: the enemy that sets it off"), Setter) || !TestNotNull(TEXT("set-up: the Boss"), Boss)
		|| !TestNotNull(TEXT("set-up: the bystander"), Beyond)
		|| !TestTrue(TEXT("set-up: the Boss is a boss by its rarity"), Boss->IsBoss()))
	{
		return false;
	}
	FGadgetShotPullEventCount Controls(Pulling.System, TEXT("crowd_control"));

	// THE CONTROL, READ FIRST: WHERE EACH STANDS BEFORE THE TRAP GOES OFF.
	TestEqual(TEXT("control: the Boss stands 4 m from the trap"), GadgetShotPullFloorDistance(Boss, TrapAt), 4.0f * M, 1.0f);
	TestEqual(TEXT("control: no crowd control yet"), Controls.Count, 0);

	GadgetShotPullStep(Trap);
	TestEqual(TEXT("the trap triggered once"), Trap->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("the enemy that set it off was pulled to 1 m of it, as the others are (J8)"),
		GadgetShotPullFloorDistance(Setter, TrapAt), ACataclysmMinion::SkillTrapPullsToCm, 1.0f);
	const float BossNow = GadgetShotPullFloorDistance(Boss, TrapAt);
	TestTrue(*FString::Printf(TEXT("the Boss 4 m away was pulled too, as every displacement moves a boss: %.1f cm from the "
								   "trap, from 400"), BossNow), BossNow >= 0.0f && BossNow <= 1.5f * M);
	TestEqual(TEXT("the bystander beyond the 5 m was not pulled"), GadgetShotPullFloorDistance(Beyond, TrapAt), 6.5f * M,
		1.0f);
	TestEqual(TEXT("two crowd controls applied: the two pulls"), Controls.Count, 2);
	return true;
}

// ==========================================================================
// 51. A trap set off by a row pulls its own enemies and counts towards the third trap's shot (J4, J8; layer 5's H5 (b))
// ==========================================================================

CATACLYSM_TEST(FCataclysmForcedTrapPullsAndCountsTest,
	"Cataclysm.Traps.ATrapSetOffByARowPullsItsOwnEnemiesAndCountsTowardsTheThirdTrapsShot")
{
	using namespace CataclysmGadgetShotAndPullTest;

	// THREE PLAYERS 100 M APART HOLDING DEMONIC WHIPS, EACH WITH THE HEAVY TRAP FLAG, THE ULTIMATE GADGET FLAG AND THE SHOT
	// ROW AT 300, ANY DISTANCE, EVERY THIRD TRAP. Each: traps 20 m, 28 m and 36 m north, each enemy 2.8 m east of its own
	// trap, and a skill gadget 64 m north, which no enemy stands within 14 m of. Only the first trap is
	// armed; the other two are set off by layer 5's "Traps trigger simultaneously" at 10 m, the second by the first and
	// the third by the second. The lone player holds no chain row; the counting player the chain row; the pulling player
	// the chain row and the pull at 5 m.
	FGadgetShotPullWorld Scope;
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
	const FVector Homes[] = {FVector::ZeroVector, FVector(100.0f * M, 0.0f, 0.0f), FVector(200.0f * M, 0.0f, 0.0f)};
	FGadgetShotPullRig Lone(World, Homes[0], TEXT("Whip"));
	FGadgetShotPullRig Counting(World, Homes[1], TEXT("Whip"));
	FGadgetShotPullRig Pulling(World, Homes[2], TEXT("Whip"));
	if (!Lone.IsUsable(*this) || !Counting.IsUsable(*this) || !Pulling.IsUsable(*this))
	{
		return false;
	}
	Lone.Holds({GadgetShotPullHeavyTrapFlag(), GadgetShotPullGadgetFlag(TEXT("Slot.Ultimate"))});
	Counting.Holds({GadgetShotPullHeavyTrapFlag(), GadgetShotPullGadgetFlag(TEXT("Slot.Ultimate"))});
	Pulling.Holds({GadgetShotPullHeavyTrapFlag(), GadgetShotPullGadgetFlag(TEXT("Slot.Ultimate")),
				   GadgetShotPullRadius(5.0f)});
	Lone.System->SetPoolActions({GadgetShotPullShotRow(300.0f, 0.0f, 3)});
	for (const FGadgetShotPullRig* Each : {&Counting, &Pulling})
	{
		Each->System->SetPoolActions({GadgetShotPullShotRow(300.0f, 0.0f, 3), GadgetShotPullChainRow()});
	}

	const FVector TrapsAt[] = {FVector(0.0f, 20.0f * M, 0.0f), FVector(0.0f, 28.0f * M, 0.0f),
							   FVector(0.0f, 36.0f * M, 0.0f)};
	// 64 M NORTH, 28 M FROM THE THIRD ENEMY (traps layer 6b, #2471): no early shot's Gathering may haul the third enemy out
	// of its trap's 3 m, so a skipped count shows as three shots in the counting line, not as one.
	const FVector GadgetAt(0.0f, 64.0f * M, 0.0f);
	const FVector East(2.8f * M, 0.0f, 0.0f);
	const FGadgetShotPullRig* Rigs[] = {&Lone, &Counting, &Pulling};
	ACataclysmMinion* Firsts[3] = {};
	ACataclysmMinion* Gadgets[3] = {};
	ACataclysmEnemyCharacter* FirstEnemies[3] = {};
	ACataclysmEnemyCharacter* SecondEnemies[3] = {};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Firsts[Index] = GadgetShotPullPlaceTrap(*this, *Rigs[Index], Homes[Index] + TrapsAt[0]);
		if (!Firsts[Index])
		{
			return false;
		}
	}
	// ONLY THE FIRST TRAPS ARE STEPPED, SO ONLY THEY ARM; the others are placed after.
	GadgetShotPullPass(World, 1.25f);
	for (ACataclysmMinion* Each : Firsts)
	{
		GadgetShotPullStep(Each);
	}
	for (int32 Index = 1; Index < 3; ++Index)
	{
		if (!GadgetShotPullPlaceTrap(*this, *Rigs[Index], Homes[Index] + TrapsAt[1])
			|| !GadgetShotPullPlaceTrap(*this, *Rigs[Index], Homes[Index] + TrapsAt[2]))
		{
			return false;
		}
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Gadgets[Index] =
			GadgetShotPullPlaceGadget(*this, *Rigs[Index], ECataclysmAbilitySlot::Ultimate, Homes[Index] + GadgetAt);
		FirstEnemies[Index] = GadgetShotPullEnemy(World, Homes[Index] + TrapsAt[0] + East);
		if (!Gadgets[Index] || !TestNotNull(*FString::Printf(TEXT("set-up: player %d's first enemy"), Index + 1),
				FirstEnemies[Index]))
		{
			return false;
		}
		if (Index > 0)
		{
			SecondEnemies[Index] = GadgetShotPullEnemy(World, Homes[Index] + TrapsAt[1] + East);
			if (!TestNotNull(*FString::Printf(TEXT("set-up: player %d's second enemy"), Index + 1), SecondEnemies[Index])
				|| !TestNotNull(*FString::Printf(TEXT("set-up: player %d's third enemy"), Index + 1),
					GadgetShotPullEnemy(World, Homes[Index] + TrapsAt[2] + East)))
			{
				return false;
			}
		}
	}
	if (!TestTrue(TEXT("set-up: each player's first trap is armed"),
			Firsts[0]->bSkillTrapArmed && Firsts[1]->bSkillTrapArmed && Firsts[2]->bSkillTrapArmed))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: ONE TRAP TRIGGERED COUNTS ONE, AND FIRES NOTHING.
	{
		FGadgetShotPullEventCount Triggered(Lone.System, TEXT("trap_triggered"));
		GadgetShotPullStep(Firsts[0]);
		TestEqual(TEXT("control: the lone player's one trap triggered once"), Triggered.Count, 1);
		TestEqual(TEXT("control: one trigger, no shot"), Gadgets[0]->SkillGadgetTrapShotsMade, 0);
	}

	// THE COUNTING PLAYER: THE FIRST TRAP SETS OFF THE SECOND, WHICH SETS OFF THE THIRD; THE THIRD, A FORCED TRAP, IS THE
	// THIRD TRIGGERED, AND ITS TRIGGER FIRES THE GADGET (J4: forced triggers counted). No pull without the row.
	{
		FGadgetShotPullEventCount Triggered(Counting.System, TEXT("trap_triggered"));
		GadgetShotPullStep(Firsts[1]);
		TestEqual(TEXT("the counting player's three traps triggered, two of them set off by the row"), Triggered.Count, 3);
		TestEqual(TEXT("and the third, a forced trap, fired the gadget once (J4)"), Gadgets[1]->SkillGadgetTrapShotsMade, 1);
		TestEqual(TEXT("with no pull row its second trap's enemy still stands 2.8 m from that trap"),
			GadgetShotPullFloorDistance(SecondEnemies[1], Homes[1] + TrapsAt[1]), 2.8f * M, 1.0f);
	}

	// THE PULLING PLAYER: THE SAME, AND EACH TRAP, FORCED OR NOT, PULLS ITS OWN ENEMY TO 1 M (J8).
	{
		FGadgetShotPullEventCount Triggered(Pulling.System, TEXT("trap_triggered"));
		GadgetShotPullStep(Firsts[2]);
		TestEqual(TEXT("the pulling player's three traps triggered"), Triggered.Count, 3);
		TestEqual(TEXT("and the third fired the gadget once"), Gadgets[2]->SkillGadgetTrapShotsMade, 1);
		TestEqual(TEXT("its first trap pulled its enemy to 1 m"),
			GadgetShotPullFloorDistance(FirstEnemies[2], Homes[2] + TrapsAt[0]), ACataclysmMinion::SkillTrapPullsToCm, 1.0f);
		TestEqual(TEXT("its second trap, set off by the row, pulled its own enemy to 1 m too: forced traps pull (J8)"),
			GadgetShotPullFloorDistance(SecondEnemies[2], Homes[2] + TrapsAt[1]), ACataclysmMinion::SkillTrapPullsToCm, 1.0f);
	}
	return true;
}

// ==========================================================================
// 52. A shot earned by a trap that sets off its neighbours is not lost inside their casts (traps layer 6b, #2471)
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetShotSurvivesTheChainTest,
	"Cataclysm.Traps.AGadgetsShotEarnedByATrapThatSetsOffItsNeighboursIsNotLostInsideTheirCasts")
{
	using namespace CataclysmGadgetShotAndPullTest;

	// TWO PLAYERS 100 M APART HOLDING DEMONIC WHIPS, EACH WITH THE HEAVY TRAP FLAG, THE ULTIMATE GADGET FLAG AND THE SHOT
	// ROW AT 100 WITHIN 10 M: a first trap 20 m north, two neighbours 6 m north and 6 m south of it, each enemy 2.5 m
	// east of its own trap, and a skill gadget 5 m west of the first trap, within 10 m of all three. The control holds no
	// chain row; the other also layer 5's "Traps trigger simultaneously" at 10 m and no pull row, so the first trap sets off
	// both neighbours. EVERY ENEMY RESISTS CROWD CONTROL WHOLLY, so no shot's Gathering hauls a neighbour's enemy out of
	// its trap's 3 m before that trap goes off: a shot then fires for each trigger, and only the lost one is missing.
	FGadgetShotPullWorld Scope;
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
	const FVector Homes[] = {FVector::ZeroVector, FVector(100.0f * M, 0.0f, 0.0f)};
	FGadgetShotPullRig Plain(World, Homes[0], TEXT("Whip"));
	FGadgetShotPullRig Chaining(World, Homes[1], TEXT("Whip"));
	if (!Plain.IsUsable(*this) || !Chaining.IsUsable(*this))
	{
		return false;
	}
	for (const FGadgetShotPullRig* Each : {&Plain, &Chaining})
	{
		Each->Holds({GadgetShotPullHeavyTrapFlag(), GadgetShotPullGadgetFlag(TEXT("Slot.Ultimate"))});
	}
	Plain.System->SetPoolActions({GadgetShotPullShotRow(100.0f, 10.0f, 0)});
	Chaining.System->SetPoolActions({GadgetShotPullShotRow(100.0f, 10.0f, 0), GadgetShotPullChainRow()});

	const FVector TrapsAt[] = {FVector(0.0f, 20.0f * M, 0.0f), FVector(0.0f, 26.0f * M, 0.0f),
							   FVector(0.0f, 14.0f * M, 0.0f)};
	const FVector GadgetAt = TrapsAt[0] + FVector(-5.0f * M, 0.0f, 0.0f);
	const FVector East(2.5f * M, 0.0f, 0.0f);
	const FGadgetShotPullRig* Rigs[] = {&Plain, &Chaining};
	ACataclysmMinion* Traps[2][3] = {};
	ACataclysmMinion* Gadgets[2] = {};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Traps[Index][0] = GadgetShotPullPlaceTrap(*this, *Rigs[Index], Homes[Index] + TrapsAt[0]);
		if (!Traps[Index][0])
		{
			return false;
		}
	}
	// ONLY THE FIRST TRAPS ARE STEPPED, SO ONLY THEY ARM; the neighbours are placed after and set off unarmed (H4).
	GadgetShotPullPass(World, 1.25f);
	GadgetShotPullStep(Traps[0][0]);
	GadgetShotPullStep(Traps[1][0]);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Traps[Index][1] = GadgetShotPullPlaceTrap(*this, *Rigs[Index], Homes[Index] + TrapsAt[1]);
		Traps[Index][2] = GadgetShotPullPlaceTrap(*this, *Rigs[Index], Homes[Index] + TrapsAt[2]);
		Gadgets[Index] =
			GadgetShotPullPlaceGadget(*this, *Rigs[Index], ECataclysmAbilitySlot::Ultimate, Homes[Index] + GadgetAt);
		if (!Traps[Index][1] || !Traps[Index][2] || !Gadgets[Index])
		{
			return false;
		}
		for (int32 Which = 0; Which < 3; ++Which)
		{
			ACataclysmEnemyCharacter* Made = GadgetShotPullEnemy(World, Homes[Index] + TrapsAt[Which] + East);
			if (!TestNotNull(*FString::Printf(TEXT("set-up: player %d's enemy of trap %d"), Index + 1, Which + 1), Made))
			{
				return false;
			}
			Made->GetAbilitySystemComponent()->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetCrowdControlResistanceAttribute(), 100.0f);
		}
	}
	if (!TestTrue(TEXT("set-up: each player's first trap is armed"),
			Traps[0][0]->bSkillTrapArmed && Traps[1][0]->bSkillTrapArmed))
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> PlainNorth(Traps[0][1]);
	const TWeakObjectPtr<ACataclysmMinion> PlainSouth(Traps[0][2]);
	const TWeakObjectPtr<ACataclysmMinion> RowNorth(Traps[1][1]);
	const TWeakObjectPtr<ACataclysmMinion> RowSouth(Traps[1][2]);

	// THE CONTROL, READ FIRST: WITH NO CHAIN ROW ONE TRAP GOES OFF AND ONE SHOT FIRES; ITS NEIGHBOURS STAND.
	{
		FGadgetShotPullEventCount Triggered(Plain.System, TEXT("trap_triggered"));
		GadgetShotPullStep(Traps[0][0]);
		TestEqual(TEXT("control: one trap triggered"), Triggered.Count, 1);
		TestEqual(TEXT("control: its gadget fired one shot"), Gadgets[0]->SkillGadgetTrapShotsMade, 1);
		TestFalse(TEXT("control: the north neighbour still stands"), GadgetShotPullGone(PlainNorth));
		TestFalse(TEXT("control: and the south one"), GadgetShotPullGone(PlainSouth));
	}

	// THE ROW: THE FIRST TRAP SETS OFF BOTH NEIGHBOURS, AND EACH OF THE THREE TRIGGERS FIRES A SHOT. The first trap's shot
	// is recorded before the neighbours go off and waits through their casts, whose blows' events end in the drain while
	// the ability list is locked (#2471).
	{
		FGadgetShotPullEventCount Triggered(Chaining.System, TEXT("trap_triggered"));
		GadgetShotPullStep(Traps[1][0]);
		TestEqual(TEXT("three traps triggered, two of them set off by the row"), Triggered.Count, 3);
		TestTrue(TEXT("the north neighbour went off and was used up"), GadgetShotPullGone(RowNorth));
		TestTrue(TEXT("and the south one"), GadgetShotPullGone(RowSouth));
		TestEqual(TEXT("the gadget fired three shots, one for each trigger: the first trap's was not lost inside its "
					   "neighbours' casts (#2471)"), Gadgets[1]->SkillGadgetTrapShotsMade, 3);
		TestEqual(TEXT("each by its clock's own path"), Gadgets[1]->SkillGadgetCastsMade, 3);
	}
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
