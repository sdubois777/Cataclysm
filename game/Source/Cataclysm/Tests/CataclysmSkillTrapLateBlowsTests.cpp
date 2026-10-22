// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmBuriedWeapon.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmProjectile.h"
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
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Skill traps, layer 3b: a trap's late blows. Ruled 2026-10-21 by the coordinating session (#2462): "a trap's late shot,
 * a buried weapon's leap and a zone are priced and announced as the trap's (Type.Trap minion_damage and deployable_hit),
 * carried by the record built here"; and Z1 (a): "A trap's zone is priced as the trap's when it is laid (the Type.Trap
 * minion_damage increases and more), and its sweeps raise no deployable_hit". In layer 1's group, `Cataclysm.Traps.`,
 * whose rig this follows, in a file of its own, numbered on from layer 3's last (25).
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER HOLDING A DEMONIC WEAPON, given a weapon damage of 100, 10,000 health and
 * 1,000 mana by hand: a Wand, whose Heavy Malefice is a shot; a Greataxe, whose Heavy Molten Cleave is a 4 m cone that
 * leaves its ground at once; a Whip, whose Heavy Searing Lash is a 5 m cone. NO ROW IS AUTHORED, SO EVERY STAT AND
 * EVERY ACTION IS WRITTEN BY HAND, as the item would build it from its row. NO KEY IS PRESSED: every trap and gadget is
 * placed directly, and their casts are free starts. Every body stands at least 1 m from every other, on a real side.
 * NOTHING HERE WAITS ON A TICK: the world's clock is moved by hand, a trap's or gadget's step is called directly, shots
 * are stepped by hand and a zone's sweep is called. Blows are struck only with the critical strike roll pinned, after
 * the world is made. "Traps deal 20%-40% increased damage" is written at 50, as layer 1's test writes it; "Traps apply
 * 1-2 bleed stacks to all enemies caught in their blast" at a size of 1, with no wait, as layer 3's test writes it.
 * Helper types and functions are named after the subject, the trap's late blow.
 */
namespace CataclysmTrapLateBlowTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The health every creature here starts with, unless a case says otherwise. */
	constexpr float TrapLateBlowEnemyHealth = 50000.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FTrapLateBlowWorld
	{
		FTrapLateBlowWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FTrapLateBlowWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/**
	 * A creature of the monsters' side with no armour, evasion, block or resistance, at `Health` of `Health`. It deals
	 * nothing of its own.
	 */
	ACataclysmEnemyCharacter* TrapLateBlowEnemy(UWorld* World, const FVector& Where,
												float Health = TrapLateBlowEnemyHealth)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		UAbilitySystemComponent* Its = Made ? Made->GetAbilitySystemComponent() : nullptr;
		if (!Its)
		{
			return nullptr;
		}
		Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Made->SetHealth(Health);
		Made->SetArmour(0.0f);
		Made->SetAttackDamage(0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Health);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
		return Made;
	}

	float TrapLateBlowHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** What a creature has lost since it was made at `Health`. */
	float TrapLateBlowTakenFrom(const AActor* Who, float Health = TrapLateBlowEnemyHealth)
	{
		return Health - TrapLateBlowHealthOf(Who);
	}

	/** Whether `Who` carries `Tag` now. */
	bool TrapLateBlowCarries(const AActor* Who, const FGameplayTag& Tag)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its && Tag.IsValid() && Its->HasMatchingGameplayTag(Tag);
	}

	/** One row of a stat line a test's player holds. */
	struct FTrapLateBlowStat
	{
		FName Stat;
		float Value = 0.0f;
		ECataclysmStatBucket Bucket = ECataclysmStatBucket::Flat;
		/** The row's one required tag, or null for none. */
		const TCHAR* RequiredTag = nullptr;
		/** How the row is scaled, and its step, when it is: `Fixed`, the default, for a row that is not. */
		ECataclysmStatScale Scale = ECataclysmStatScale::Fixed;
		float ScaleStep = 0.0f;
	};

	/** A slot's trap flag: a flat 1 on `skill_slot_is_a_trap` requiring the slot's tag, layers 1 and 2's row. */
	FTrapLateBlowStat TrapLateBlowTrapFlag(const TCHAR* SlotTag)
	{
		return {FName(UCataclysmAbilitySystemComponent::SkillSlotIsATrapStat), 1.0f, ECataclysmStatBucket::Flat, SlotTag};
	}

	/** A slot's gadget flag: a flat 1 on `skill_slot_is_a_gadget` requiring the slot's tag, the gadget's row. */
	FTrapLateBlowStat TrapLateBlowGadgetFlag(const TCHAR* SlotTag)
	{
		return {FName(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat), 1.0f, ECataclysmStatBucket::Flat,
				SlotTag};
	}

	/** "Traps deal 20%-40% increased damage" at 50: 50 increased `minion_damage` requiring `Type.Trap`. */
	FTrapLateBlowStat TrapLateBlowTrapDamage()
	{
		return {FName(TEXT("minion_damage")), 50.0f, ECataclysmStatBucket::Increased, TEXT("Type.Trap")};
	}

	/**
	 * "Gadgets deal bonus damage equal to 3%-6% of your maximum HP per hit" at 3: 3 for every 100 of the player's
	 * 10,000, a flat 300, which only a blow priced as a living gadget's takes (S5 d).
	 */
	FTrapLateBlowStat TrapLateBlowGadgetShareOfHealth()
	{
		return {FName(TEXT("attack_damage")), 3.0f, ECataclysmStatBucket::Flat, TEXT("Type.Deployable"),
				ECataclysmStatScale::PerPointOfMaximumHealth, 100.0f};
	}

	/** A possessed player character holding a Demonic weapon, its ability system and its weapon slots. */
	struct FTrapLateBlowRig
	{
		FTrapLateBlowRig(UWorld* World, const FVector& Where, const TCHAR* Weapon)
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

		/** The row this rig's weapon holds for `Slot`, or null. */
		const FCataclysmWeaponSkill* HeldRow(ECataclysmAbilitySlot Slot) const
		{
			return Slots ? Slots->GetAvailableSkills().FindByPredicate(
							   [Slot](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == Slot; })
						 : nullptr;
		}

		/** Whether everything was made and the weapon holds a Heavy row a trap may cast. Reports why not. */
		bool IsUsable(FAutomationTestBase& Test) const
		{
			const FCataclysmWeaponSkill* Heavy = HeldRow(ECataclysmAbilitySlot::Heavy);
			if (!Player || !System || !Slots || !Heavy || !ACataclysmMinion::SkillGadgetMayCast(*Heavy))
			{
				Test.AddError(TEXT("set-up: a possessed player holding a Demonic weapon with a Heavy row a trap may cast. "
								   "If a row is missing, DT_WeaponSkills may be older than game/Data/WeaponSkills.csv: "
								   "run tools/generate_datatable_assets.py"));
				return false;
			}
			return true;
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			System->SetNumericAttributeBase(Attribute, Value);
		}

		/** REPLACES THE WHOLE STAT LINE with these rows. */
		void Holds(const TArray<FTrapLateBlowStat>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FTrapLateBlowStat& Each : Rows)
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
				if (Each.Scale != ECataclysmStatScale::Fixed)
				{
					Row.Scale = Each.Scale;
					Row.ScaleStep = Each.ScaleStep;
				}
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
	ACataclysmMinion* TrapLateBlowPlaceTrap(FAutomationTestBase& Test, const FTrapLateBlowRig& Rig, const FVector& Where)
	{
		ACataclysmMinion* Made = ACataclysmMinion::PlaceSkillTrap(Rig.Player, ECataclysmAbilitySlot::Heavy, Where);
		if (!Made)
		{
			Test.AddError(TEXT("set-up: no skill trap was placed. DT_MinionTypes needs the SkillTrap row: run "
							   "tools/generate_datatable_assets.py"));
		}
		return Made;
	}

	/** Move the world's clock forward by hand. No timer fires and nobody thinks. */
	void TrapLateBlowPass(UWorld* World, float Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/** Run a trap's or a gadget's quarter-second step, as `RegenerationStep` calls it. */
	void TrapLateBlowStep(ACataclysmMinion* Machine)
	{
		if (IsValid(Machine))
		{
			ACataclysmMinion::GadgetStep(Machine);
		}
	}

	/** Whether a trap or gadget is gone: destroyed, or on its way out. */
	bool TrapLateBlowGone(const TWeakObjectPtr<ACataclysmMinion>& Machine)
	{
		return !Machine.IsValid() || Machine->IsActorBeingDestroyed();
	}

	/** "Traps apply 1-2 bleed stacks to all enemies caught in their blast" at a size of 1, as the item builds it. */
	FCataclysmPoolAction TrapLateBlowBleedRow()
	{
		FCataclysmPoolAction Sized;
		Sized.Event = FName(TEXT("deployable_hit"));
		Sized.Pool = FName(UCataclysmAbilitySystemComponent::ApplyStatusSizedAction);
		Sized.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false));
		Sized.TriggerKey = FName(TEXT("TrapLateBlowTest:deployable_hit:bleed"));
		Sized.TriggerCooldownSeconds = 0.0f;
		Sized.ApplyStatus = ECataclysmApplyStatus::Sized;
		Sized.StatusName = TEXT("Bleed");
		Sized.Percent = 1.0f;
		return Sized;
	}

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FTrapLateBlowEventCount
	{
		FTrapLateBlowEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
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
		~FTrapLateBlowEventCount()
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
	 * Step every projectile in the world not in `Done` by a twentieth of a second until each has finished, adding each to
	 * `Done`, as layer 3's test lands its shots: a test world never ticks. How many finished in this call.
	 */
	int32 TrapLateBlowLandEveryShot(UWorld* World, TSet<const ACataclysmProjectile*>& Done)
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

	/** The ground zones `Owner` left, in the world's order. */
	TArray<ACataclysmGroundZone*> TrapLateBlowZonesOf(UWorld* World, const AActor* Owner)
	{
		TArray<ACataclysmGroundZone*> Zones;
		for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
		{
			if (IsValid(*It) && It->GetOwner() == Owner)
			{
				Zones.Add(*It);
			}
		}
		return Zones;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 26. A projectile trap's late shot is priced as the trap's and announced as its deployable_hit (#2462)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapLateBlowShotTest,
	"Cataclysm.Traps.AProjectileTrapsLateShotIsPricedByATrapDamageRowAndItsDeployableHitBleedsTheEnemyItStrikes")
{
	using namespace CataclysmTrapLateBlowTest;

	// THREE PLAYERS 100 M APART, EACH HOLDING A DEMONIC WAND, WHOSE HEAVY MALEFICE IS A SHOT: a Heavy trap 20 m north
	// with the enemy that sets it off 2.5 m east of it. The control holds the Heavy flag alone; the second also holds
	// "Traps deal 20%-40% increased damage" at 50; the third the bleed row, its creature 1,000 health so the shot takes a
	// tenth of it (the owner's rule of #917). THE TRAP IS USED UP AS ITS CAST STARTS, so every shot lands after it is gone.
	FTrapLateBlowWorld Scope;
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
	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	if (!TestTrue(TEXT("set-up: the Bleed tag exists"), Bleed.IsValid()))
	{
		return false;
	}
	constexpr float Frail = 1000.0f;
	const FVector PricedHome(100.0f * M, 0.0f, 0.0f);
	const FVector BleedingHome(200.0f * M, 0.0f, 0.0f);
	FTrapLateBlowRig Plain(World, FVector::ZeroVector, TEXT("Wand"));
	FTrapLateBlowRig Priced(World, PricedHome, TEXT("Wand"));
	FTrapLateBlowRig Bleeding(World, BleedingHome, TEXT("Wand"));
	if (!Plain.IsUsable(*this) || !Priced.IsUsable(*this) || !Bleeding.IsUsable(*this))
	{
		return false;
	}
	const FCataclysmWeaponSkill* Held = Plain.HeldRow(ECataclysmAbilitySlot::Heavy);
	if (!TestTrue(TEXT("set-up: the wand's Heavy is a shot"), Held && Held->Shape == ECataclysmSkillShape::Projectile))
	{
		return false;
	}
	Plain.Holds({TrapLateBlowTrapFlag(TEXT("Slot.Heavy"))});
	Priced.Holds({TrapLateBlowTrapFlag(TEXT("Slot.Heavy")), TrapLateBlowTrapDamage()});
	Bleeding.Holds({TrapLateBlowTrapFlag(TEXT("Slot.Heavy"))});
	Bleeding.System->SetPoolActions({TrapLateBlowBleedRow()});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector Near(2.5f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* PlainSetter = TrapLateBlowEnemy(World, North + Near);
	ACataclysmEnemyCharacter* PricedSetter = TrapLateBlowEnemy(World, PricedHome + North + Near);
	ACataclysmEnemyCharacter* BleedingSetter = TrapLateBlowEnemy(World, BleedingHome + North + Near, Frail);
	if (!TestNotNull(TEXT("set-up: the control's enemy"), PlainSetter)
		|| !TestNotNull(TEXT("set-up: the priced player's enemy"), PricedSetter)
		|| !TestNotNull(TEXT("set-up: the bleeding player's frail enemy"), BleedingSetter))
	{
		return false;
	}
	ACataclysmMinion* PlainTrap = TrapLateBlowPlaceTrap(*this, Plain, North);
	ACataclysmMinion* PricedTrap = TrapLateBlowPlaceTrap(*this, Priced, PricedHome + North);
	ACataclysmMinion* BleedingTrap = TrapLateBlowPlaceTrap(*this, Bleeding, BleedingHome + North);
	if (!PlainTrap || !PricedTrap || !BleedingTrap)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> PricedWatched(PricedTrap);
	FTrapLateBlowEventCount PlainTrapHits(Plain.System, TEXT("deployable_hit"));
	FTrapLateBlowEventCount PlainBlows(Plain.System, TEXT("hit_dealt"));

	// THE THREE TRAPS TRIGGER; THEIR SHOTS ARE STILL IN FLIGHT.
	TrapLateBlowPass(World, 1.25f);
	TrapLateBlowStep(PlainTrap);
	TrapLateBlowStep(PricedTrap);
	TrapLateBlowStep(BleedingTrap);
	TestTrue(TEXT("set-up: the trap is used up as its cast starts"), TrapLateBlowGone(PricedWatched));
	TestEqual(TEXT("set-up: and its enemy is untouched: the shot is still in flight"), TrapLateBlowTakenFrom(PricedSetter),
		0.0f, 0.001f);
	TestEqual(TEXT("control: with the shot in flight, nothing has been announced"), PlainTrapHits.Count, 0);

	// THE SHOTS LAND.
	TSet<const ACataclysmProjectile*> Done;
	const int32 Landed = TrapLateBlowLandEveryShot(World, Done);
	TestTrue(*FString::Printf(TEXT("set-up: the three shots landed: %d"), Landed), Landed >= 3);
	const float PlainTook = TrapLateBlowTakenFrom(PlainSetter);
	TestTrue(*FString::Printf(TEXT("control: the control's late shot struck its enemy: %.2f"), PlainTook), PlainTook > 0.0f);
	TestFalse(TEXT("control: and with no row it bleeds nothing"), TrapLateBlowCarries(PlainSetter, Bleed));
	TestEqual(*FString::Printf(TEXT("the late shot is priced by the minion_damage row naming Type.Trap, once: 50%% more, "
									"%.2f against %.2f"), TrapLateBlowTakenFrom(PricedSetter), PlainTook),
		TrapLateBlowTakenFrom(PricedSetter), PlainTook * 1.5f, PlainTook * 0.001f);
	TestTrue(*FString::Printf(TEXT("the late shot raised deployable_hit: %d"), PlainTrapHits.Count), PlainTrapHits.Count > 0);
	TestEqual(*FString::Printf(TEXT("once for each blow it dealt, never twice: %d announcements for %d blows"),
							   PlainTrapHits.Count, PlainBlows.Count),
		PlainTrapHits.Count, PlainBlows.Count);
	TestTrue(*FString::Printf(TEXT("set-up: the bleeding player's shot took a tenth or more of its enemy's 1,000: %.2f"),
							  TrapLateBlowTakenFrom(BleedingSetter, Frail)),
		TrapLateBlowTakenFrom(BleedingSetter, Frail) >= Frail / 10.0f);
	TestTrue(TEXT("and its deployable_hit carries Type.Trap, so the trap bleed row bleeds the enemy the late shot struck"),
		TrapLateBlowCarries(BleedingSetter, Bleed));
	return true;
}

// ==========================================================================
// 27. A buried weapon of a trap's cast leaps as the trap's, every leap (#2462)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapLateBlowLeapTest,
	"Cataclysm.Traps.ABuriedWeaponOfAUsedUpTrapsCastLeapsAsTheTrapsAtEveryLeapAndAPlayerThrownAxeDoesNot")
{
	using namespace CataclysmTrapLateBlowTest;

	// ONE PLAYER HOLDING A DEMONIC WHIP, THE HEAVY FLAG, "Traps deal 20%-40% increased damage" at 50 and the bleed row.
	// Its Heavy trap 20 m north with the enemy that sets it off 2.5 m east of it, triggered and used up first. Then two
	// chains of three creatures of 500 health, 50 m apart and 30 m from the trap, each along X at nought, 4 m and 7 m, so
	// a weapon in the first leaps to the second (4 m, the third 7 m) and from the second to the third (3 m, the first
	// 4 m): a weapon buried in the first of one chain as `BuryInStruck` buries a shot's axe, with the used-up trap's
	// identity and the cast's tags; in the first of the other the same weapon with no identity, as a player's own throw
	// buries it. Each leap is a blow of 100% weapon damage. NO HARROWER IS THROWN: a Special trap's cast is made around
	// its enemy, so the throw's aim is not one a test can fix; the burying is called as `BuryInStruck` calls it.
	FTrapLateBlowWorld Scope;
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
	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	const FGameplayTag Trap = FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false);
	if (!TestTrue(TEXT("set-up: the Bleed and Type.Trap tags exist"), Bleed.IsValid() && Trap.IsValid()))
	{
		return false;
	}
	FTrapLateBlowRig Rig(World, FVector::ZeroVector, TEXT("Whip"));
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	Rig.Holds({TrapLateBlowTrapFlag(TEXT("Slot.Heavy")), TrapLateBlowTrapDamage()});
	Rig.System->SetPoolActions({TrapLateBlowBleedRow()});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	ACataclysmEnemyCharacter* Setter = TrapLateBlowEnemy(World, North + FVector(2.5f * M, 0.0f, 0.0f));
	ACataclysmMinion* Placed = Setter ? TrapLateBlowPlaceTrap(*this, Rig, North) : nullptr;
	if (!TestNotNull(TEXT("set-up: the enemy that sets the trap off"), Setter) || !Placed)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> Watched(Placed);
	const TWeakObjectPtr<AActor> UsedUpTrap(Placed);
	TrapLateBlowPass(World, 1.25f);
	TrapLateBlowStep(Placed);
	if (!TestTrue(TEXT("set-up: the trap triggered and was used up"), TrapLateBlowGone(Watched)))
	{
		return false;
	}

	constexpr float Chained = 500.0f;
	const FVector TrapChain(0.0f, 50.0f * M, 0.0f);
	const FVector ThrownChain(0.0f, 100.0f * M, 0.0f);
	const FVector Second(4.0f * M, 0.0f, 0.0f);
	const FVector Third(7.0f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* TrapFirst = TrapLateBlowEnemy(World, TrapChain, Chained);
	ACataclysmEnemyCharacter* TrapSecond = TrapLateBlowEnemy(World, TrapChain + Second, Chained);
	ACataclysmEnemyCharacter* TrapThird = TrapLateBlowEnemy(World, TrapChain + Third, Chained);
	ACataclysmEnemyCharacter* ThrownFirst = TrapLateBlowEnemy(World, ThrownChain, Chained);
	ACataclysmEnemyCharacter* ThrownSecond = TrapLateBlowEnemy(World, ThrownChain + Second, Chained);
	ACataclysmEnemyCharacter* ThrownThird = TrapLateBlowEnemy(World, ThrownChain + Third, Chained);
	if (!TestNotNull(TEXT("set-up: the trap chain's first"), TrapFirst)
		|| !TestNotNull(TEXT("set-up: the trap chain's second"), TrapSecond)
		|| !TestNotNull(TEXT("set-up: the trap chain's third"), TrapThird)
		|| !TestNotNull(TEXT("set-up: the thrown chain's first"), ThrownFirst)
		|| !TestNotNull(TEXT("set-up: the thrown chain's second"), ThrownSecond)
		|| !TestNotNull(TEXT("set-up: the thrown chain's third"), ThrownThird))
	{
		return false;
	}

	// THE SAME TAGS ON BOTH WEAPONS, the trap's cast's own `Type.Trap` among them, so only the identity differs.
	FGameplayTagContainer CastTags;
	CastTags.AddTag(Trap);
	if (!TestNotNull(TEXT("set-up: the trap's weapon is buried with the used-up trap's identity"),
			UCataclysmBuriedWeapon::BuryIn(TrapFirst, Rig.Player, 10.0f * M, 100.0f, CastTags, /*bInBurns=*/false,
										   UsedUpTrap))
		|| !TestNotNull(TEXT("set-up: the thrown weapon is buried with none"),
			UCataclysmBuriedWeapon::BuryIn(ThrownFirst, Rig.Player, 10.0f * M, 100.0f, CastTags, /*bInBurns=*/false)))
	{
		return false;
	}
	FTrapLateBlowEventCount TrapHits(Rig.System, TEXT("deployable_hit"));
	FTrapLateBlowEventCount Blows(Rig.System, TEXT("hit_dealt"));

	// THE CONTROL, READ FIRST: THE THROWN WEAPON'S FIRST LEAP IS THE PLAYER'S OWN BLOW.
	TestTrue(TEXT("control: the thrown weapon leaps from its first host"), UCataclysmBuriedWeapon::LeapFromDying(ThrownFirst));
	const float ThrownLeap = TrapLateBlowTakenFrom(ThrownSecond, Chained);
	TestTrue(*FString::Printf(TEXT("control: and strikes the second: %.2f"), ThrownLeap), ThrownLeap > 0.0f);
	TestEqual(TEXT("control: a player's own leap raises no deployable_hit"), TrapHits.Count, 0);
	TestFalse(TEXT("control: and lays no trap bleed"), TrapLateBlowCarries(ThrownSecond, Bleed));

	// THE TRAP'S WEAPON'S FIRST LEAP, THE TRAP GONE.
	TestTrue(TEXT("the trap's weapon leaps from its first host"), UCataclysmBuriedWeapon::LeapFromDying(TrapFirst));
	TestEqual(*FString::Printf(TEXT("its first leap is priced by the minion_damage row naming Type.Trap: 50%% more, %.2f "
									"against %.2f"), TrapLateBlowTakenFrom(TrapSecond, Chained), ThrownLeap),
		TrapLateBlowTakenFrom(TrapSecond, Chained), ThrownLeap * 1.5f, ThrownLeap * 0.001f);
	TestEqual(TEXT("and raises deployable_hit once"), TrapHits.Count, 1);
	TestTrue(TEXT("whose Type.Trap lets the trap bleed row bleed the enemy it struck"),
		TrapLateBlowCarries(TrapSecond, Bleed));

	// AND PAST ITS FIRST LEAP: BOTH WEAPONS LEAP AGAIN.
	const int32 AnnouncedBeforeTheThrownLeapsAgain = TrapHits.Count;
	TestTrue(TEXT("control: the thrown weapon leaps again"), UCataclysmBuriedWeapon::LeapFromDying(ThrownSecond));
	TestEqual(TEXT("control: its second leap strikes the third as its first struck the second"),
		TrapLateBlowTakenFrom(ThrownThird, Chained), ThrownLeap, ThrownLeap * 0.001f);
	TestEqual(TEXT("control: and raises no deployable_hit"), TrapHits.Count, AnnouncedBeforeTheThrownLeapsAgain);
	TestTrue(TEXT("the trap's weapon leaps again"), UCataclysmBuriedWeapon::LeapFromDying(TrapSecond));
	const UCataclysmBuriedWeapon* Twice = TrapThird->FindComponentByClass<UCataclysmBuriedWeapon>();
	TestTrue(TEXT("set-up: the trap's weapon is in the third, two leaps on"), Twice && Twice->Leaps == 2);
	TestEqual(*FString::Printf(TEXT("its second leap is still the trap's: 50%% more, %.2f against %.2f"),
							   TrapLateBlowTakenFrom(TrapThird, Chained), ThrownLeap),
		TrapLateBlowTakenFrom(TrapThird, Chained), ThrownLeap * 1.5f, ThrownLeap * 0.001f);
	TestEqual(TEXT("and raises deployable_hit once more"), TrapHits.Count, 2);
	TestTrue(TEXT("and bleeds the third"), TrapLateBlowCarries(TrapThird, Bleed));
	TestFalse(TEXT("control: the thrown weapon's third bleeds nothing"), TrapLateBlowCarries(ThrownThird, Bleed));
	TestEqual(*FString::Printf(TEXT("four leaps, four blows, two of them announced: %d"), Blows.Count), Blows.Count, 4);
	return true;
}

// ==========================================================================
// 28. A trap's zone is priced as the trap's when laid, and its sweeps are no hit (Z1 (a))
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapLateBlowZoneTest,
	"Cataclysm.Traps.ATrapsZoneIsPricedByATrapDamageRowWhenLaidAndItsSweepsRaiseNoDeployableHitAndLayNoTrapBleed")
{
	using namespace CataclysmTrapLateBlowTest;

	// FOUR PLAYERS 100 M APART, EACH WITH A HEAVY TRAP 20 M NORTH AND THE ENEMY THAT SETS IT OFF 2.5 M EAST OF IT.
	// THE STRIKE'S KIND, LAID WHILE THE TRAP STANDS: two Demonic Greataxe players, whose Heavy Molten Cleave is a 4 m
	// cone of 120 degrees that leaves 4 m of ground at its cast's place, the trap's; a bystander of 100 health 3.8 m west
	// of the trap, outside the 3 m trigger radius and outside the cone facing east, inside the ground. THE SHOT'S KIND,
	// LAID WHERE THE SHOT ENDS, AFTER THE TRAP IS GONE: two Demonic Wand players holding "your spells leave a persistent
	// AOE zone at the impact point" as `zone_at_impact_seconds` at 4, which gives Malefice, a shot stating no ground, a
	// 1.5 m zone where it ends; a bystander of 100 health made 1.2 m from that zone's middle once it is laid. In each
	// pair the control holds the Heavy flag (and the zone row), the other also "Traps deal 20%-40% increased damage" at
	// 50 and the bleed row. The cleave's enemies hold 1,500 health, so its blow takes a tenth and the bleed row is seen
	// to work on a hit; the shot's 50,000.
	FTrapLateBlowWorld Scope;
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
	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	if (!TestTrue(TEXT("set-up: the Bleed tag exists"), Bleed.IsValid()))
	{
		return false;
	}
	constexpr float Frail = 1500.0f;
	constexpr float Bystanding = 100.0f;
	const FVector CleavePricedHome(100.0f * M, 0.0f, 0.0f);
	const FVector ShotPlainHome(200.0f * M, 0.0f, 0.0f);
	const FVector ShotPricedHome(300.0f * M, 0.0f, 0.0f);
	FTrapLateBlowRig CleavePlain(World, FVector::ZeroVector, TEXT("Greataxe"));
	FTrapLateBlowRig CleavePriced(World, CleavePricedHome, TEXT("Greataxe"));
	FTrapLateBlowRig ShotPlain(World, ShotPlainHome, TEXT("Wand"));
	FTrapLateBlowRig ShotPriced(World, ShotPricedHome, TEXT("Wand"));
	if (!CleavePlain.IsUsable(*this) || !CleavePriced.IsUsable(*this) || !ShotPlain.IsUsable(*this)
		|| !ShotPriced.IsUsable(*this))
	{
		return false;
	}
	const FCataclysmWeaponSkill* Cleave = CleavePlain.HeldRow(ECataclysmAbilitySlot::Heavy);
	const FCataclysmWeaponSkill* Shot = ShotPlain.HeldRow(ECataclysmAbilitySlot::Heavy);
	if (!TestTrue(TEXT("set-up: the greataxe's Heavy is a Strike that leaves ground"),
			Cleave && Cleave->Shape == ECataclysmSkillShape::Strike && Cleave->Params.LeavesGround())
		|| !TestTrue(TEXT("set-up: the wand's Heavy is a shot that states no ground"),
			Shot && Shot->Shape == ECataclysmSkillShape::Projectile && !Shot->Params.LeavesGround()))
	{
		return false;
	}
	const FTrapLateBlowStat ZoneAtImpact{FName(UCataclysmDamageCalculation::ZoneAtImpactSecondsStat), 4.0f};
	CleavePlain.Holds({TrapLateBlowTrapFlag(TEXT("Slot.Heavy"))});
	CleavePriced.Holds({TrapLateBlowTrapFlag(TEXT("Slot.Heavy")), TrapLateBlowTrapDamage()});
	ShotPlain.Holds({TrapLateBlowTrapFlag(TEXT("Slot.Heavy")), ZoneAtImpact});
	ShotPriced.Holds({TrapLateBlowTrapFlag(TEXT("Slot.Heavy")), ZoneAtImpact, TrapLateBlowTrapDamage()});
	CleavePriced.System->SetPoolActions({TrapLateBlowBleedRow()});
	ShotPriced.System->SetPoolActions({TrapLateBlowBleedRow()});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector Near(2.5f * M, 0.0f, 0.0f);
	const FVector West(-3.8f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* CleavePlainSetter = TrapLateBlowEnemy(World, North + Near, Frail);
	ACataclysmEnemyCharacter* CleavePricedSetter = TrapLateBlowEnemy(World, CleavePricedHome + North + Near, Frail);
	ACataclysmEnemyCharacter* CleaveBystander = TrapLateBlowEnemy(World, CleavePricedHome + North + West, Bystanding);
	ACataclysmEnemyCharacter* ShotPlainSetter = TrapLateBlowEnemy(World, ShotPlainHome + North + Near);
	ACataclysmEnemyCharacter* ShotPricedSetter = TrapLateBlowEnemy(World, ShotPricedHome + North + Near);
	if (!TestNotNull(TEXT("set-up: the cleave control's enemy"), CleavePlainSetter)
		|| !TestNotNull(TEXT("set-up: the priced cleave's enemy"), CleavePricedSetter)
		|| !TestNotNull(TEXT("set-up: the priced cleave's bystander"), CleaveBystander)
		|| !TestNotNull(TEXT("set-up: the shot control's enemy"), ShotPlainSetter)
		|| !TestNotNull(TEXT("set-up: the priced shot's enemy"), ShotPricedSetter))
	{
		return false;
	}
	ACataclysmMinion* CleavePlainTrap = TrapLateBlowPlaceTrap(*this, CleavePlain, North);
	ACataclysmMinion* CleavePricedTrap = TrapLateBlowPlaceTrap(*this, CleavePriced, CleavePricedHome + North);
	ACataclysmMinion* ShotPlainTrap = TrapLateBlowPlaceTrap(*this, ShotPlain, ShotPlainHome + North);
	ACataclysmMinion* ShotPricedTrap = TrapLateBlowPlaceTrap(*this, ShotPriced, ShotPricedHome + North);
	if (!CleavePlainTrap || !CleavePricedTrap || !ShotPlainTrap || !ShotPricedTrap)
	{
		return false;
	}

	// THE STRIKE'S KIND: THE TRAPS TRIGGER, AND THE GROUND IS LAID IN THE CAST, THE TRAP STANDING.
	TrapLateBlowPass(World, 1.25f);
	TrapLateBlowStep(CleavePlainTrap);
	TrapLateBlowStep(CleavePricedTrap);
	const TArray<ACataclysmGroundZone*> CleavePlainZones = TrapLateBlowZonesOf(World, CleavePlain.Player);
	const TArray<ACataclysmGroundZone*> CleavePricedZones = TrapLateBlowZonesOf(World, CleavePriced.Player);
	if (!TestEqual(TEXT("set-up: the control's cleave left one zone"), CleavePlainZones.Num(), 1)
		|| !TestEqual(TEXT("set-up: the priced cleave left one zone"), CleavePricedZones.Num(), 1))
	{
		return false;
	}
	const float CleavePlainTick = CleavePlainZones[0]->DamagePerTick;
	TestTrue(*FString::Printf(TEXT("control: the control's ground deals %.2f a sweep"), CleavePlainTick),
		CleavePlainTick > 0.0f);
	TestEqual(*FString::Printf(TEXT("the trap's Strike ground is priced by the minion_damage row naming Type.Trap when "
									"laid: 50%% more, %.2f against %.2f"), CleavePricedZones[0]->DamagePerTick,
							   CleavePlainTick),
		CleavePricedZones[0]->DamagePerTick, CleavePlainTick * 1.5f, CleavePlainTick * 0.001f);
	TestTrue(*FString::Printf(TEXT("control: the cleave's own blow took a tenth of its enemy's 1,500: %.2f"),
							  TrapLateBlowTakenFrom(CleavePricedSetter, Frail)),
		TrapLateBlowTakenFrom(CleavePricedSetter, Frail) >= Frail / 10.0f);
	TestTrue(TEXT("control: and, a hit of the trap's cast, its deployable_hit laid the trap bleed"),
		TrapLateBlowCarries(CleavePricedSetter, Bleed));
	TestEqual(TEXT("set-up: the bystander was not struck by the cleave"), TrapLateBlowTakenFrom(CleaveBystander, Bystanding),
		0.0f, 0.001f);

	// AND ITS SWEEP.
	{
		FTrapLateBlowEventCount SweepHits(CleavePriced.System, TEXT("deployable_hit"));
		CleavePricedZones[0]->Sweep();
		TestTrue(*FString::Printf(TEXT("set-up: the sweep burned the bystander for a tenth or more of its 100: %.2f"),
								  TrapLateBlowTakenFrom(CleaveBystander, Bystanding)),
			TrapLateBlowTakenFrom(CleaveBystander, Bystanding) >= Bystanding / 10.0f);
		TestEqual(TEXT("the Strike ground's sweep raised no deployable_hit: a tick is not a hit"), SweepHits.Count, 0);
		TestFalse(TEXT("so it laid no trap bleed on the bystander"), TrapLateBlowCarries(CleaveBystander, Bleed));
	}

	// THE SHOT'S KIND: THE TRAPS TRIGGER, ARE USED UP, AND THE SHOTS LAND, EACH LEAVING ITS ZONE THEN.
	TrapLateBlowStep(ShotPlainTrap);
	TrapLateBlowStep(ShotPricedTrap);
	TestEqual(TEXT("set-up: no zone is laid while the shots fly"), TrapLateBlowZonesOf(World, ShotPriced.Player).Num(), 0);
	TSet<const ACataclysmProjectile*> Done;
	TrapLateBlowLandEveryShot(World, Done);
	const TArray<ACataclysmGroundZone*> ShotPlainZones = TrapLateBlowZonesOf(World, ShotPlain.Player);
	const TArray<ACataclysmGroundZone*> ShotPricedZones = TrapLateBlowZonesOf(World, ShotPriced.Player);
	if (!TestEqual(TEXT("set-up: the control's shot left one zone where it ended"), ShotPlainZones.Num(), 1)
		|| !TestEqual(TEXT("set-up: the priced shot left one zone where it ended"), ShotPricedZones.Num(), 1))
	{
		return false;
	}
	const float ShotPlainTick = ShotPlainZones[0]->DamagePerTick;
	TestTrue(*FString::Printf(TEXT("control: the control's zone deals %.2f a sweep"), ShotPlainTick), ShotPlainTick > 0.0f);
	TestEqual(*FString::Printf(TEXT("the zone a used-up trap's shot lays is priced by the row when laid: 50%% more, %.2f "
									"against %.2f"), ShotPricedZones[0]->DamagePerTick, ShotPlainTick),
		ShotPricedZones[0]->DamagePerTick, ShotPlainTick * 1.5f, ShotPlainTick * 0.001f);
	ACataclysmEnemyCharacter* ShotBystander = TrapLateBlowEnemy(
		World, ShotPricedZones[0]->GetActorLocation() + FVector(0.0f, 1.2f * M, 0.0f), Bystanding);
	if (!TestNotNull(TEXT("set-up: the bystander made inside the shot's zone"), ShotBystander))
	{
		return false;
	}
	{
		FTrapLateBlowEventCount SweepHits(ShotPriced.System, TEXT("deployable_hit"));
		ShotPricedZones[0]->Sweep();
		TestTrue(*FString::Printf(TEXT("set-up: the sweep burned the bystander for a tenth or more of its 100: %.2f"),
								  TrapLateBlowTakenFrom(ShotBystander, Bystanding)),
			TrapLateBlowTakenFrom(ShotBystander, Bystanding) >= Bystanding / 10.0f);
		TestEqual(TEXT("the shot's zone's sweep raised no deployable_hit"), SweepHits.Count, 0);
		TestFalse(TEXT("so it laid no trap bleed on the bystander"), TrapLateBlowCarries(ShotBystander, Bleed));
	}
	return true;
}

// ==========================================================================
// 29. A skill gadget's late shot after the gadget is gone is the player's (P5 of 2026-10-16 kept)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapLateBlowGadgetTest,
	"Cataclysm.Traps.ASkillGadgetsShotLandingAfterTheGadgetIsGoneIsThePlayersAndNoTrapsLateBlow")
{
	using namespace CataclysmTrapLateBlowTest;

	// TWO PLAYERS 100 M APART, EACH HOLDING A DEMONIC WAND WITH ITS HEAVY CONVERTED INTO A GADGET and "Gadgets deal bonus
	// damage equal to 3%-6% of your maximum HP per hit" at 3, which only a blow priced as a living gadget's takes; and
	// "Traps deal 20%-40% increased damage" at 50 and the trap bleed row, which a trap's late blow would take. A gadget
	// each 20 m north with an enemy 3 m east of it. Both gadgets cast; the second is destroyed before its shot lands.
	FTrapLateBlowWorld Scope;
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
	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	if (!TestTrue(TEXT("set-up: the Bleed tag exists"), Bleed.IsValid()))
	{
		return false;
	}
	const FVector GoneHome(100.0f * M, 0.0f, 0.0f);
	FTrapLateBlowRig Standing(World, FVector::ZeroVector, TEXT("Wand"));
	FTrapLateBlowRig Gone(World, GoneHome, TEXT("Wand"));
	if (!Standing.IsUsable(*this) || !Gone.IsUsable(*this))
	{
		return false;
	}
	for (const FTrapLateBlowRig* Each : {&Standing, &Gone})
	{
		Each->Holds({TrapLateBlowGadgetFlag(TEXT("Slot.Heavy")), TrapLateBlowGadgetShareOfHealth(),
					 TrapLateBlowTrapDamage()});
		Each->System->SetPoolActions({TrapLateBlowBleedRow()});
	}
	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector East(3.0f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* StandingEnemy = TrapLateBlowEnemy(World, North + East);
	ACataclysmEnemyCharacter* GoneEnemy = TrapLateBlowEnemy(World, GoneHome + North + East);
	ACataclysmMinion* StandingGadget = ACataclysmMinion::PlaceSkillGadget(Standing.Player, ECataclysmAbilitySlot::Heavy, North);
	ACataclysmMinion* GoneGadget =
		ACataclysmMinion::PlaceSkillGadget(Gone.Player, ECataclysmAbilitySlot::Heavy, GoneHome + North);
	if (!TestNotNull(TEXT("set-up: the standing gadget's enemy"), StandingEnemy)
		|| !TestNotNull(TEXT("set-up: the other gadget's enemy"), GoneEnemy)
		|| !TestNotNull(TEXT("set-up: the standing gadget. DT_MinionTypes needs the SkillGadget row"), StandingGadget)
		|| !TestNotNull(TEXT("set-up: the other gadget"), GoneGadget))
	{
		return false;
	}
	FTrapLateBlowEventCount StandingHits(Standing.System, TEXT("deployable_hit"));
	FTrapLateBlowEventCount GoneHits(Gone.System, TEXT("deployable_hit"));

	TrapLateBlowStep(StandingGadget);
	TrapLateBlowStep(GoneGadget);
	if (!TestEqual(TEXT("set-up: the standing gadget cast"), StandingGadget->SkillGadgetCastsMade, 1)
		|| !TestEqual(TEXT("set-up: and so did the other"), GoneGadget->SkillGadgetCastsMade, 1))
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> GoneWatched(GoneGadget);
	GoneGadget->Destroy();
	TestTrue(TEXT("set-up: the other gadget is gone, its shot in flight"), TrapLateBlowGone(GoneWatched));
	TSet<const ACataclysmProjectile*> Done;
	TestTrue(TEXT("set-up: both shots landed"), TrapLateBlowLandEveryShot(World, Done) >= 2);

	// THE CONTROL, READ FIRST: A SHOT LANDING WHILE ITS GADGET STANDS IS THE GADGET'S.
	const float StandingTook = TrapLateBlowTakenFrom(StandingEnemy);
	const float GoneTook = TrapLateBlowTakenFrom(GoneEnemy);
	TestTrue(*FString::Printf(TEXT("control: the standing gadget's shot raised deployable_hit: %d"), StandingHits.Count),
		StandingHits.Count > 0);
	TestTrue(*FString::Printf(TEXT("control: and took the flat row a living gadget's blow takes: %.2f against %.2f"),
							  StandingTook, GoneTook),
		StandingTook > GoneTook + 1.0f);

	// THE SHOT LANDING AFTER ITS GADGET IS GONE.
	TestTrue(*FString::Printf(TEXT("set-up: the late shot struck its enemy: %.2f"), GoneTook), GoneTook > 0.0f);
	TestEqual(TEXT("a gadget's late shot raises no deployable_hit: it is the player's (P5), and no trap's late blow"),
		GoneHits.Count, 0);
	TestFalse(TEXT("so no trap bleed is laid by it"), TrapLateBlowCarries(GoneEnemy, Bleed));
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
