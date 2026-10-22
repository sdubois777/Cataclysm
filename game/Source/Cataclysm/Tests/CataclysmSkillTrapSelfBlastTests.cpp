// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDeployableSelfBlast.h"
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
 * Skill traps, layer 7: "Your deployables and traps deal full damage to you if you are within their blast radius", its
 * trap half. Ruled 2026-10-22 by the coordinating session under the owner's delegation (K1 to K6): a shot or a landing
 * of a trap already used up is still that trap's blast (K1); a rearmed trap and a trap set off by a row are reached as
 * they stand (K2); a zone's sweep (K3) and the kill explosion of "Enemies killed by traps explode" (K4) never strike the
 * wearer; a skill gadget's bonus shot at a trap's enemy is reached as every gadget cast is (K5). The flag is a flat 1 on
 * `deployable_blasts_strike_self` with no tags, the row #34 authored. In layer 1's group, `Cataclysm.Traps.`, in a file
 * of its own, numbered on from layer 6's last (51).
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER OF THE PLAYERS' SIDE HOLDING A DEMONIC WEAPON, given a weapon damage of
 * 100, 10,000 health and 1,000 mana by hand, AND AN ENEMY'S DEFENCES: no armour, evasion, block, resistance, damage
 * reduction or energy shield, as #34's tests give it, so what it loses can be read against what an enemy loses. A Wand,
 * whose Heavy Malefice is a shot of 2,600 cm a second that goes off in 1 m where it stops; a Whip, whose Heavy Searing
 * Lash is a 5 m cone struck at once and whose Ultimate The Gathering a 14 m ring; a Greataxe, whose Heavy Molten Cleave
 * is a cone that leaves 4 m of burning ground at the trap. NO ROW IS AUTHORED, SO EVERY STAT AND EVERY ACTION IS WRITTEN
 * BY HAND, as the item would build it from its row. NO KEY IS PRESSED: every trap and gadget is placed directly, and
 * every cast is a free start, so nothing here costs a resource. Every body stands at least 1 m from every other, on a
 * real side. NOTHING HERE WAITS ON A TICK: the world's clock is moved by hand, a trap's step is called directly, a shot
 * is stepped by hand and a zone's sweep is called. Blows are struck only with the critical strike roll pinned, after the
 * world is made. Helper types and functions are named after the subject, the trap's self-blast.
 */
namespace CataclysmTrapSelfBlastTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The health every creature here starts with, unless a case says otherwise. */
	constexpr float TrapSelfBlastEnemyHealth = 50000.0f;

	/** The health every player here starts with. */
	constexpr float TrapSelfBlastPlayerHealth = 10000.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FTrapSelfBlastWorld
	{
		FTrapSelfBlastWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FTrapSelfBlastWorld()
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
	ACataclysmEnemyCharacter* TrapSelfBlastEnemy(UWorld* World, const FVector& Where,
												 float Health = TrapSelfBlastEnemyHealth)
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

	/** What a creature has lost since it was made at `Health`; -1 less than nothing for one with no ability system. */
	float TrapSelfBlastTakenFrom(const AActor* Who, float Health = TrapSelfBlastEnemyHealth)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Health - Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** How far `Who` stands from `Place` along the floor, in centimetres, as a float; -1 for nobody. */
	float TrapSelfBlastFloorDistance(const AActor* Who, const FVector& Place)
	{
		return IsValid(Who) ? static_cast<float>(FVector::Dist2D(Who->GetActorLocation(), Place)) : -1.0f;
	}

	/** One row of a stat line a test's player holds. */
	struct FTrapSelfBlastStat
	{
		FName Stat;
		float Value = 0.0f;
		ECataclysmStatBucket Bucket = ECataclysmStatBucket::Flat;
		/** The row's one required tag, or null for none. */
		const TCHAR* RequiredTag = nullptr;
	};

	/** A slot's trap flag: a flat 1 on `skill_slot_is_a_trap` requiring the slot's tag, layers 1 and 2's row. */
	FTrapSelfBlastStat TrapSelfBlastTrapFlag(const TCHAR* SlotTag)
	{
		return {FName(UCataclysmAbilitySystemComponent::SkillSlotIsATrapStat), 1.0f, ECataclysmStatBucket::Flat, SlotTag};
	}

	/** A slot's gadget flag: a flat 1 on `skill_slot_is_a_gadget` requiring the slot's tag, the gadget's row. */
	FTrapSelfBlastStat TrapSelfBlastGadgetFlag(const TCHAR* SlotTag)
	{
		return {FName(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat), 1.0f, ECataclysmStatBucket::Flat,
				SlotTag};
	}

	/** The sentence's row, as #34 authored it: a flat 1 on `deployable_blasts_strike_self` with no tags. */
	FTrapSelfBlastStat TrapSelfBlastDrawback()
	{
		return {FName(UCataclysmDeployableSelfBlast::FlagStat), 1.0f, ECataclysmStatBucket::Flat, nullptr};
	}

	/** "Triggered traps have a 25%-50% chance to reset and rearm instead of being consumed" at 100, layer 4's row. */
	FTrapSelfBlastStat TrapSelfBlastRearmsAlways()
	{
		return {FName(ACataclysmMinion::TrapRearmChanceStat), 100.0f, ECataclysmStatBucket::Flat, TEXT("Type.Trap")};
	}

	/** A possessed player character of the players' side holding a Demonic weapon, with an enemy's defences. */
	struct FTrapSelfBlastRig
	{
		FTrapSelfBlastRig(UWorld* World, const FVector& Where, const TCHAR* Weapon)
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
			Player->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
			System = State->GetCataclysmAbilitySystemComponent();
			Slots = Player->FindComponentByClass<UCataclysmWeaponSlotsComponent>();
			if (!System || !Slots)
			{
				return;
			}
			Slots->SetDamageType(TEXT("Demonic"));
			Slots->EquipWeaponType(Weapon);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), TrapSelfBlastPlayerHealth);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), TrapSelfBlastPlayerHealth);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			// AN ENEMY'S DEFENCES, as `CataclysmDeployableSelfBlastTest`'s rig gives its players.
			Set(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetDamageReductionAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetDamageReductionMoreAttribute(), 0.0f);
			Set(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 0.0f);
			Set(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);
		}

		/** The row this rig's weapon holds for `Slot`, or null. */
		const FCataclysmWeaponSkill* HeldRow(ECataclysmAbilitySlot Slot) const
		{
			return Slots ? Slots->GetAvailableSkills().FindByPredicate(
							   [Slot](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == Slot; })
						 : nullptr;
		}

		/**
		 * Whether everything was made and the weapon holds a row for `Slot` a trap or a gadget may cast, of the shape
		 * `Shape`. Reports why not.
		 */
		bool IsUsable(FAutomationTestBase& Test, ECataclysmAbilitySlot Slot, ECataclysmSkillShape Shape) const
		{
			const FCataclysmWeaponSkill* Held = HeldRow(Slot);
			if (!Player || !System || !Slots || !Held || !ACataclysmMinion::SkillGadgetMayCast(*Held)
				|| Held->Shape != Shape)
			{
				Test.AddError(TEXT("set-up: a possessed player holding a Demonic weapon with a row for the slot a trap "
								   "may cast, of the expected shape. If a row is missing, DT_WeaponSkills may be older "
								   "than game/Data/WeaponSkills.csv: run tools/generate_datatable_assets.py"));
				return false;
			}
			return true;
		}

		/** Sets an attribute, when this ability system carries the set that holds it. */
		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			if (System->HasAttributeSetForAttribute(Attribute))
			{
				System->SetNumericAttributeBase(Attribute, Value);
			}
		}

		/** What the player has lost since it was made. */
		float Lost() const
		{
			return TrapSelfBlastPlayerHealth
				- System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		/** REPLACES THE WHOLE STAT LINE with these rows. */
		void Holds(const TArray<FTrapSelfBlastStat>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FTrapSelfBlastStat& Each : Rows)
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

		/** Move the player to `Where`, where the case wants it standing. */
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
	ACataclysmMinion* TrapSelfBlastPlaceTrap(FAutomationTestBase& Test, const FTrapSelfBlastRig& Rig,
											 ECataclysmAbilitySlot Slot, const FVector& Where)
	{
		ACataclysmMinion* Made = ACataclysmMinion::PlaceSkillTrap(Rig.Player, Slot, Where);
		if (!Made)
		{
			Test.AddError(TEXT("set-up: no skill trap was placed. DT_MinionTypes needs the SkillTrap row: run "
							   "tools/generate_datatable_assets.py"));
		}
		return Made;
	}

	/** A skill gadget for this rig's `Slot` at `Where`, or null with the reason reported. */
	ACataclysmMinion* TrapSelfBlastPlaceGadget(FAutomationTestBase& Test, const FTrapSelfBlastRig& Rig,
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
	void TrapSelfBlastPass(UWorld* World, float Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/** Run a trap's quarter-second step, as `RegenerationStep` calls it: it arms a trap whose time has come. */
	void TrapSelfBlastStep(ACataclysmMinion* Trap)
	{
		if (IsValid(Trap))
		{
			ACataclysmMinion::GadgetStep(Trap);
		}
	}

	/** Whether a trap is gone: destroyed, or on its way out. */
	bool TrapSelfBlastGone(const TWeakObjectPtr<ACataclysmMinion>& Trap)
	{
		return !Trap.IsValid() || Trap->IsActorBeingDestroyed();
	}

	/**
	 * Step every projectile in the world not in `Done` by FIVE THOUSANDTHS of a second until each has finished, adding
	 * each to `Done`: 13 cm of Malefice's flight a step, so it stops within 13 cm of where its sweep first touches its
	 * enemy, and the place of its blast is known to that. A test world never ticks. How many finished in this call.
	 */
	int32 TrapSelfBlastLandEveryShotFinely(UWorld* World, TSet<const ACataclysmProjectile*>& Done)
	{
		int32 Finished = 0;
		for (int32 Round = 0; Round < 4000; ++Round)
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
				if (IsValid(Shot) && !Shot->Step(0.005f))
				{
					Done.Add(Shot);
					++Finished;
				}
			}
		}
		return Finished;
	}

	/** The one projectile `Firer` fired, or null when it fired none or more than one. */
	const ACataclysmProjectile* TrapSelfBlastShotOf(UWorld* World, const AActor* Firer)
	{
		const ACataclysmProjectile* Found = nullptr;
		for (TActorIterator<ACataclysmProjectile> It(World); It; ++It)
		{
			if (IsValid(*It) && It->GetOwner() == Firer)
			{
				if (Found)
				{
					return nullptr;
				}
				Found = *It;
			}
		}
		return Found;
	}

	/** The ground zones `Owner` left, in the world's order. */
	TArray<ACataclysmGroundZone*> TrapSelfBlastZonesOf(UWorld* World, const AActor* Owner)
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

	/** "Traps trigger simultaneously when any one of them is triggered, if within 8-12 meters" at 10 m, layer 5's row. */
	FCataclysmPoolAction TrapSelfBlastChainRow()
	{
		FCataclysmPoolAction Row;
		Row.Event = FName(TEXT("trap_triggered"));
		Row.Pool = FName(UCataclysmAbilitySystemComponent::TriggerTrapsNearbyAction);
		Row.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false));
		Row.TriggerKey = FName(TEXT("TrapSelfBlastTest:trigger_traps_nearby:trap_triggered"));
		Row.TriggerCooldownSeconds = 0.0f;
		Row.Percent = 10.0f;
		Row.bSetOffTrapsNearby = true;
		return Row;
	}

	/**
	 * "When a trap is triggered near an active gadget, the gadget fires an immediate bonus shot at the triggering enemy"
	 * at its normal blow within 10 m, layer 6's row, with no count and no window.
	 */
	FCataclysmPoolAction TrapSelfBlastGadgetShotRow()
	{
		FCataclysmPoolAction Row;
		Row.Event = FName(TEXT("trap_triggered"));
		Row.Pool = FName(UCataclysmAbilitySystemComponent::GadgetFiresAtTrapEnemyAction);
		Row.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false));
		Row.TriggerKey = FName(TEXT("TrapSelfBlastTest:gadget_fires_at_trap_enemy:trap_triggered"));
		Row.TriggerCooldownSeconds = 0.0f;
		Row.Percent = 100.0f;
		Row.EventValue = 10.0f;
		Row.EveryNth = 0;
		Row.CountWindowSeconds = 0.0f;
		Row.bGadgetFiresAtTrapEnemy = true;
		return Row;
	}

	/**
	 * "Enemies killed by traps explode, dealing 50%-100% of the trap's damage to all enemies within 4 meters" at 50, as
	 * the item builds the row: `explode_victim_for_killing_blow` on `kill`, requiring `Type.Trap`, layer 4's row.
	 */
	FCataclysmPoolAction TrapSelfBlastExplosionRow()
	{
		FCataclysmPoolAction Explodes;
		Explodes.Event = FName(TEXT("kill"));
		Explodes.Pool = FName(UCataclysmAbilitySystemComponent::ExplodeVictimForKillingBlowAction);
		Explodes.RequiredTags.AddTag(
			FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false));
		Explodes.TriggerKey = FName(TEXT("TrapSelfBlastTest:kill:explode"));
		Explodes.bExplodeVictimForKillingBlow = true;
		Explodes.Percent = 50.0f;
		return Explodes;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 52. A used-up trap's shot strikes its wearer inside its blast, at the figure an enemy beside the wearer takes (K1)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapSelfBlastUsedUpShotTest,
	"Cataclysm.Traps.AUsedUpTrapsShotStrikesItsSelfBlastWearerInsideItsBlastAsAnEnemyBesideItIsStruckAndNotOneOutside")
{
	using namespace CataclysmTrapSelfBlastTest;

	// THREE PLAYERS 100 M APART, EACH HOLDING A DEMONIC WAND AND THE HEAVY FLAG: a Heavy trap 20 m north with the enemy
	// that sets it off 2.5 m east of it. The trap is used up as its cast starts, so its shot explodes after it is gone.
	// The shot's sweep, 40 cm wide, first touches the enemy when the shot is 2.1 m east of the trap, so stepped 13 cm at a
	// time it stops between 2.1 m and 2.23 m, and its 1 m blast reaches a body whose middle is within 1 m and its own
	// width of that place. ONCE THE SHOTS ARE IN FLIGHT each player is moved: the control, holding no drawback, and the
	// inside wearer to 2.16 m east of the trap and 1.15 m north of the shot's line, 1.2 m from the enemy; the outside
	// wearer to 4 m north of the line. An enemy beside the inside wearer is made then, 1.15 m south of the line, so it is
	// neither the nearest enemy the trap aims at nor in the shot's path.
	FTrapSelfBlastWorld Scope;
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
	const FVector InsideHome(100.0f * M, 0.0f, 0.0f);
	const FVector OutsideHome(200.0f * M, 0.0f, 0.0f);
	FTrapSelfBlastRig Plain(World, FVector::ZeroVector, TEXT("Wand"));
	FTrapSelfBlastRig Inside(World, InsideHome, TEXT("Wand"));
	FTrapSelfBlastRig Outside(World, OutsideHome, TEXT("Wand"));
	if (!Plain.IsUsable(*this, ECataclysmAbilitySlot::Heavy, ECataclysmSkillShape::Projectile)
		|| !Inside.IsUsable(*this, ECataclysmAbilitySlot::Heavy, ECataclysmSkillShape::Projectile)
		|| !Outside.IsUsable(*this, ECataclysmAbilitySlot::Heavy, ECataclysmSkillShape::Projectile))
	{
		return false;
	}
	Plain.Holds({TrapSelfBlastTrapFlag(TEXT("Slot.Heavy"))});
	Inside.Holds({TrapSelfBlastTrapFlag(TEXT("Slot.Heavy")), TrapSelfBlastDrawback()});
	Outside.Holds({TrapSelfBlastTrapFlag(TEXT("Slot.Heavy")), TrapSelfBlastDrawback()});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector Near(2.5f * M, 0.0f, 0.0f);
	const FVector InsideSpot(2.16f * M, 1.15f * M, 0.0f);
	const FVector OutsideSpot(2.16f * M, 4.0f * M, 0.0f);
	const FVector BesideSpot(2.16f * M, -1.15f * M, 0.0f);
	ACataclysmEnemyCharacter* PlainSetter = TrapSelfBlastEnemy(World, North + Near);
	ACataclysmEnemyCharacter* InsideSetter = TrapSelfBlastEnemy(World, InsideHome + North + Near);
	ACataclysmEnemyCharacter* OutsideSetter = TrapSelfBlastEnemy(World, OutsideHome + North + Near);
	if (!TestNotNull(TEXT("set-up: the control's enemy"), PlainSetter)
		|| !TestNotNull(TEXT("set-up: the inside wearer's enemy"), InsideSetter)
		|| !TestNotNull(TEXT("set-up: the outside wearer's enemy"), OutsideSetter))
	{
		return false;
	}
	ACataclysmMinion* PlainTrap = TrapSelfBlastPlaceTrap(*this, Plain, ECataclysmAbilitySlot::Heavy, North);
	ACataclysmMinion* InsideTrap = TrapSelfBlastPlaceTrap(*this, Inside, ECataclysmAbilitySlot::Heavy, InsideHome + North);
	ACataclysmMinion* OutsideTrap =
		TrapSelfBlastPlaceTrap(*this, Outside, ECataclysmAbilitySlot::Heavy, OutsideHome + North);
	if (!PlainTrap || !InsideTrap || !OutsideTrap)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> InsideWatched(InsideTrap);
	const TWeakObjectPtr<AActor> InsideTrapIdentity(InsideTrap);

	// THE THREE TRAPS TRIGGER AND ARE USED UP; THEIR SHOTS ARE STILL IN FLIGHT.
	TrapSelfBlastPass(World, 1.25f);
	TrapSelfBlastStep(PlainTrap);
	TrapSelfBlastStep(InsideTrap);
	TrapSelfBlastStep(OutsideTrap);
	TestTrue(TEXT("set-up: the inside wearer's trap is used up as its cast starts"), TrapSelfBlastGone(InsideWatched));
	TestEqual(TEXT("set-up: and its enemy is untouched: the shot is still in flight"), TrapSelfBlastTakenFrom(InsideSetter),
		0.0f, 0.001f);
	TestTrue(TEXT("set-up: the gone trap's identity names a used-up trap of its wearer's"),
		UCataclysmDeployableSelfBlast::IsTheWearersUsedUpTrap(Inside.Player, InsideTrapIdentity));
	TestFalse(TEXT("control: and no used-up trap of another player's"),
		UCataclysmDeployableSelfBlast::IsTheWearersUsedUpTrap(Plain.Player, InsideTrapIdentity));
	Plain.StandsAt(North + InsideSpot);
	Inside.StandsAt(InsideHome + North + InsideSpot);
	Outside.StandsAt(OutsideHome + North + OutsideSpot);
	ACataclysmEnemyCharacter* Beside = TrapSelfBlastEnemy(World, InsideHome + North + BesideSpot);
	if (!TestNotNull(TEXT("set-up: the enemy beside the inside wearer, made with the shot in flight"), Beside))
	{
		return false;
	}

	// THE SHOTS LAND.
	TSet<const ACataclysmProjectile*> Done;
	const int32 Landed = TrapSelfBlastLandEveryShotFinely(World, Done);
	TestTrue(*FString::Printf(TEXT("set-up: the three shots landed: %d"), Landed), Landed >= 3);
	const ACataclysmProjectile* InsideShot = TrapSelfBlastShotOf(World, Inside.Player);
	if (!TestNotNull(TEXT("set-up: the inside wearer's one shot"), InsideShot))
	{
		return false;
	}
	const float WearerFromBlast = TrapSelfBlastFloorDistance(Inside.Player, InsideShot->GetActorLocation());
	TestTrue(*FString::Printf(TEXT("set-up: the shot went off %.1f cm from the inside wearer, within its %.1f cm blast "
								   "and the wearer's 42 cm"), WearerFromBlast, InsideShot->RadiusCm),
		WearerFromBlast >= 0.0f && WearerFromBlast < InsideShot->RadiusCm + 42.0f);

	// THE CONTROL, READ FIRST: THE SAME PLACE, NO DRAWBACK.
	TestTrue(*FString::Printf(TEXT("control: the control's late shot struck its enemy: %.2f"),
							  TrapSelfBlastTakenFrom(PlainSetter)),
		TrapSelfBlastTakenFrom(PlainSetter) > 0.0f);
	TestEqual(TEXT("control: and the player holding no drawback, standing where the inside wearer stands, loses nothing"),
		Plain.Lost(), 0.0f, 0.001f);

	// THE WEARER OUTSIDE THE BLAST.
	TestTrue(*FString::Printf(TEXT("control: the outside wearer's late shot struck its enemy: %.2f"),
							  TrapSelfBlastTakenFrom(OutsideSetter)),
		TrapSelfBlastTakenFrom(OutsideSetter) > 0.0f);
	TestEqual(TEXT("the wearer 4 m from the shot's line, outside its blast, loses nothing"), Outside.Lost(), 0.0f, 0.001f);

	// THE WEARER INSIDE IT.
	TestTrue(*FString::Printf(TEXT("set-up: the inside wearer's late shot struck its enemy: %.2f"),
							  TrapSelfBlastTakenFrom(InsideSetter)),
		TrapSelfBlastTakenFrom(InsideSetter) > 0.0f);
	const float BesideTook = TrapSelfBlastTakenFrom(Beside);
	TestTrue(*FString::Printf(TEXT("control: the blast struck the enemy beside the wearer: %.2f"), BesideTook),
		BesideTook > 0.0f);
	TestTrue(*FString::Printf(TEXT("the used-up trap's shot struck the wearer inside its blast: it lost %.2f"),
							  Inside.Lost()),
		Inside.Lost() > 0.0f);
	TestEqual(*FString::Printf(TEXT("at the figure the enemy beside it took, the wearer holding an enemy's defences: %.2f "
									"against %.2f"), Inside.Lost(), BesideTook),
		Inside.Lost(), BesideTook, 0.01f);
	return true;
}

// ==========================================================================
// 53. A rearmed trap's cast strikes its wearer inside it, as it stands (K2)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapSelfBlastRearmedTest,
	"Cataclysm.Traps.ARearmedTrapsSecondCastStrikesItsSelfBlastWearerInsideItAndThePlayerWithoutTheDrawbackIsNot")
{
	using namespace CataclysmTrapSelfBlastTest;

	// TWO PLAYERS 100 M APART, EACH HOLDING A DEMONIC WHIP, THE HEAVY FLAG AND THE REARM CHANCE AT 100, so a trap stands
	// after every trigger: a Heavy trap 20 m north with its enemy 2.5 m east, and the player standing 4 m east of the
	// trap, 1.5 m beyond the enemy and inside Searing Lash's 5 m cone aimed at it. The control holds no drawback.
	FTrapSelfBlastWorld Scope;
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
	const FVector WearerHome(100.0f * M, 0.0f, 0.0f);
	FTrapSelfBlastRig Plain(World, FVector::ZeroVector, TEXT("Whip"));
	FTrapSelfBlastRig Wearer(World, WearerHome, TEXT("Whip"));
	if (!Plain.IsUsable(*this, ECataclysmAbilitySlot::Heavy, ECataclysmSkillShape::Strike)
		|| !Wearer.IsUsable(*this, ECataclysmAbilitySlot::Heavy, ECataclysmSkillShape::Strike))
	{
		return false;
	}
	Plain.Holds({TrapSelfBlastTrapFlag(TEXT("Slot.Heavy")), TrapSelfBlastRearmsAlways()});
	Wearer.Holds({TrapSelfBlastTrapFlag(TEXT("Slot.Heavy")), TrapSelfBlastRearmsAlways(), TrapSelfBlastDrawback()});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector Near(2.5f * M, 0.0f, 0.0f);
	const FVector Beyond(4.0f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* PlainSetter = TrapSelfBlastEnemy(World, North + Near);
	ACataclysmEnemyCharacter* WearerSetter = TrapSelfBlastEnemy(World, WearerHome + North + Near);
	if (!TestNotNull(TEXT("set-up: the control's enemy"), PlainSetter)
		|| !TestNotNull(TEXT("set-up: the wearer's enemy"), WearerSetter))
	{
		return false;
	}
	ACataclysmMinion* PlainTrap = TrapSelfBlastPlaceTrap(*this, Plain, ECataclysmAbilitySlot::Heavy, North);
	ACataclysmMinion* WearerTrap = TrapSelfBlastPlaceTrap(*this, Wearer, ECataclysmAbilitySlot::Heavy, WearerHome + North);
	if (!PlainTrap || !WearerTrap)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> PlainWatched(PlainTrap);
	const TWeakObjectPtr<ACataclysmMinion> WearerWatched(WearerTrap);
	Plain.StandsAt(North + Beyond);
	Wearer.StandsAt(WearerHome + North + Beyond);

	// THE FIRST TRIGGERS.
	TrapSelfBlastPass(World, 1.25f);
	TrapSelfBlastStep(PlainTrap);
	TrapSelfBlastStep(WearerTrap);
	TestEqual(TEXT("control: the control's trap triggered once"), PlainTrap->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("control: and its player, holding no drawback, loses nothing"), Plain.Lost(), 0.0f, 0.001f);
	TestEqual(TEXT("set-up: the wearer's trap triggered once"), WearerTrap->SkillTrapTriggersMade, 1);
	const float FirstLoss = Wearer.Lost();
	TestTrue(*FString::Printf(TEXT("set-up: its first cast struck the wearer inside the lash: %.2f"), FirstLoss),
		FirstLoss > 0.0f);
	if (!TestFalse(TEXT("set-up: and at a chance of 100 the trap stands, to arm again"), TrapSelfBlastGone(WearerWatched))
		|| !TestFalse(TEXT("set-up: and so does the control's"), TrapSelfBlastGone(PlainWatched)))
	{
		return false;
	}
	TestEqual(TEXT("set-up: it is not among the used-up traps, so the standing test is what finds it"),
		Wearer.System->SpentSkillTraps.Num(), 0);

	// ARMED AGAIN, EACH TRIGGERS A SECOND TIME.
	TrapSelfBlastPass(World, 1.25f);
	TrapSelfBlastStep(PlainTrap);
	TrapSelfBlastStep(WearerTrap);
	TestEqual(TEXT("control: the control's rearmed trap triggered a second time"), PlainTrap->SkillTrapTriggersMade, 2);
	TestEqual(TEXT("control: and its player still loses nothing"), Plain.Lost(), 0.0f, 0.001f);
	TestEqual(TEXT("set-up: the wearer's rearmed trap triggered a second time"), WearerTrap->SkillTrapTriggersMade, 2);
	TestTrue(*FString::Printf(TEXT("the rearmed trap's cast struck its wearer inside it again: %.2f lost, %.2f after the "
								   "first"), Wearer.Lost(), FirstLoss),
		Wearer.Lost() > FirstLoss + 1.0f);
	return true;
}

// ==========================================================================
// 54. A trap set off by another trap's row strikes its wearer inside its cast, as it stands (K2)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapSelfBlastForcedTest,
	"Cataclysm.Traps.ATrapSetOffByAnotherTrapsRowStrikesItsSelfBlastWearerInsideItsCastAndWithoutTheRowNothingDoes")
{
	using namespace CataclysmTrapSelfBlastTest;

	// TWO PLAYERS 100 M APART, EACH HOLDING A DEMONIC WHIP, THE HEAVY FLAG AND THE DRAWBACK, WITH TWO HEAVY TRAPS: the
	// first 20 m north with its enemy 2.5 m east, a second 8 m north of the first with an enemy of its own 2.5 m east of
	// it, and the player 4 m east of the second, inside its lash and 8.9 m from the first, outside the first's. The second
	// is never stepped, so it never arms and nothing sets it off but a row. The control holds no row; the other holds
	// "Traps trigger simultaneously when any one of them is triggered, if within 8-12 meters" at 10 m.
	FTrapSelfBlastWorld Scope;
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
	FTrapSelfBlastRig Plain(World, FVector::ZeroVector, TEXT("Whip"));
	FTrapSelfBlastRig Chained(World, RowHome, TEXT("Whip"));
	if (!Plain.IsUsable(*this, ECataclysmAbilitySlot::Heavy, ECataclysmSkillShape::Strike)
		|| !Chained.IsUsable(*this, ECataclysmAbilitySlot::Heavy, ECataclysmSkillShape::Strike))
	{
		return false;
	}
	Plain.Holds({TrapSelfBlastTrapFlag(TEXT("Slot.Heavy")), TrapSelfBlastDrawback()});
	Chained.Holds({TrapSelfBlastTrapFlag(TEXT("Slot.Heavy")), TrapSelfBlastDrawback()});
	Chained.System->SetPoolActions({TrapSelfBlastChainRow()});

	const FVector First(0.0f, 20.0f * M, 0.0f);
	const FVector Second(0.0f, 28.0f * M, 0.0f);
	const FVector Near(2.5f * M, 0.0f, 0.0f);
	const FVector Beyond(4.0f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* PlainFirstSetter = TrapSelfBlastEnemy(World, First + Near);
	ACataclysmEnemyCharacter* PlainSecondEnemy = TrapSelfBlastEnemy(World, Second + Near);
	ACataclysmEnemyCharacter* ChainedFirstSetter = TrapSelfBlastEnemy(World, RowHome + First + Near);
	ACataclysmEnemyCharacter* ChainedSecondEnemy = TrapSelfBlastEnemy(World, RowHome + Second + Near);
	if (!TestNotNull(TEXT("set-up: the control's first enemy"), PlainFirstSetter)
		|| !TestNotNull(TEXT("set-up: the control's second enemy"), PlainSecondEnemy)
		|| !TestNotNull(TEXT("set-up: the chained player's first enemy"), ChainedFirstSetter)
		|| !TestNotNull(TEXT("set-up: the chained player's second enemy"), ChainedSecondEnemy))
	{
		return false;
	}
	ACataclysmMinion* PlainFirst = TrapSelfBlastPlaceTrap(*this, Plain, ECataclysmAbilitySlot::Heavy, First);
	ACataclysmMinion* PlainSecond = TrapSelfBlastPlaceTrap(*this, Plain, ECataclysmAbilitySlot::Heavy, Second);
	ACataclysmMinion* ChainedFirst = TrapSelfBlastPlaceTrap(*this, Chained, ECataclysmAbilitySlot::Heavy, RowHome + First);
	ACataclysmMinion* ChainedSecond =
		TrapSelfBlastPlaceTrap(*this, Chained, ECataclysmAbilitySlot::Heavy, RowHome + Second);
	if (!PlainFirst || !PlainSecond || !ChainedFirst || !ChainedSecond)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> PlainSecondWatched(PlainSecond);
	const TWeakObjectPtr<ACataclysmMinion> ChainedSecondWatched(ChainedSecond);
	Plain.StandsAt(Second + Beyond);
	Chained.StandsAt(RowHome + Second + Beyond);

	// THE FIRST TRAPS TRIGGER.
	TrapSelfBlastPass(World, 1.25f);
	TrapSelfBlastStep(PlainFirst);
	TrapSelfBlastStep(ChainedFirst);

	// THE CONTROL, READ FIRST: NO ROW, SO THE SECOND TRAP STANDS AND THE PLAYER BESIDE IT IS UNTOUCHED.
	TestTrue(TEXT("control: the control's first trap struck its enemy"), TrapSelfBlastTakenFrom(PlainFirstSetter) > 0.0f);
	TestFalse(TEXT("control: with no row its second trap is not set off: it stands"),
		TrapSelfBlastGone(PlainSecondWatched));
	TestEqual(TEXT("control: and its enemy is untouched"), TrapSelfBlastTakenFrom(PlainSecondEnemy), 0.0f, 0.001f);
	TestEqual(TEXT("control: so the wearer beside it, outside the first trap's lash, loses nothing"), Plain.Lost(), 0.0f,
		0.001f);

	// THE ROW: THE SECOND TRAP IS SET OFF, AND ITS CAST STRIKES THE WEARER INSIDE IT.
	TestTrue(TEXT("set-up: the chained player's first trap struck its enemy"),
		TrapSelfBlastTakenFrom(ChainedFirstSetter) > 0.0f);
	TestTrue(TEXT("set-up: the row set the second trap off: it is used up"), TrapSelfBlastGone(ChainedSecondWatched));
	TestTrue(TEXT("set-up: and its lash struck its own enemy"), TrapSelfBlastTakenFrom(ChainedSecondEnemy) > 0.0f);
	TestTrue(*FString::Printf(TEXT("the forced trap's cast struck its wearer inside it: %.2f lost"), Chained.Lost()),
		Chained.Lost() > 0.0f);
	return true;
}

// ==========================================================================
// 55. A trap's zone's sweep never strikes its wearer standing in it (K3)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapSelfBlastZoneTest,
	"Cataclysm.Traps.ATrapsZoneSweepBurnsAnEnemyInItAndNeverStrikesItsSelfBlastWearerStandingInIt")
{
	using namespace CataclysmTrapSelfBlastTest;

	// ONE PLAYER HOLDING A DEMONIC GREATAXE, THE HEAVY FLAG AND THE DRAWBACK: a Heavy trap 20 m north with its enemy
	// 2.5 m east of it, so Molten Cleave swings east and leaves 4 m of burning ground at the trap. An enemy stands 3.8 m
	// west of the trap, and the wearer 2 m west and 2 m south of it: both on the ground and behind the cleave.
	FTrapSelfBlastWorld Scope;
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
	FTrapSelfBlastRig Wearer(World, FVector::ZeroVector, TEXT("Greataxe"));
	if (!Wearer.IsUsable(*this, ECataclysmAbilitySlot::Heavy, ECataclysmSkillShape::Strike))
	{
		return false;
	}
	const FCataclysmWeaponSkill* Cleave = Wearer.HeldRow(ECataclysmAbilitySlot::Heavy);
	if (!TestTrue(TEXT("set-up: the greataxe's Heavy is a Strike that leaves ground"),
			Cleave && Cleave->Params.LeavesGround()))
	{
		return false;
	}
	Wearer.Holds({TrapSelfBlastTrapFlag(TEXT("Slot.Heavy")), TrapSelfBlastDrawback()});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	ACataclysmEnemyCharacter* Setter = TrapSelfBlastEnemy(World, North + FVector(2.5f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* OnTheGround = TrapSelfBlastEnemy(World, North + FVector(-3.8f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the trap's enemy"), Setter)
		|| !TestNotNull(TEXT("set-up: the enemy on the ground"), OnTheGround))
	{
		return false;
	}
	ACataclysmMinion* Trap = TrapSelfBlastPlaceTrap(*this, Wearer, ECataclysmAbilitySlot::Heavy, North);
	if (!Trap)
	{
		return false;
	}
	Wearer.StandsAt(North + FVector(-2.0f * M, -2.0f * M, 0.0f));

	// THE TRAP TRIGGERS, AND THE GROUND IS LAID IN ITS CAST.
	TrapSelfBlastPass(World, 1.25f);
	TrapSelfBlastStep(Trap);
	const TArray<ACataclysmGroundZone*> Zones = TrapSelfBlastZonesOf(World, Wearer.Player);
	if (!TestEqual(TEXT("set-up: the cleave left one zone"), Zones.Num(), 1))
	{
		return false;
	}
	const float WearerFromZone = TrapSelfBlastFloorDistance(Wearer.Player, Zones[0]->GetActorLocation());
	TestTrue(*FString::Printf(TEXT("set-up: the wearer stands on the ground, %.1f cm from its middle within its %.1f cm"),
							  WearerFromZone, Zones[0]->RadiusCm),
		WearerFromZone >= 0.0f && WearerFromZone < Zones[0]->RadiusCm);
	TestTrue(TEXT("set-up: the cleave struck the trap's enemy"), TrapSelfBlastTakenFrom(Setter) > 0.0f);
	TestEqual(TEXT("set-up: and not the wearer behind it"), Wearer.Lost(), 0.0f, 0.001f);
	TestEqual(TEXT("set-up: nor the enemy behind it"), TrapSelfBlastTakenFrom(OnTheGround), 0.0f, 0.001f);

	// THE SWEEP.
	Zones[0]->Sweep();
	TestTrue(*FString::Printf(TEXT("control: the sweep burned the enemy on the ground: %.2f"),
							  TrapSelfBlastTakenFrom(OnTheGround)),
		TrapSelfBlastTakenFrom(OnTheGround) > 0.0f);
	TestEqual(TEXT("and never strikes the wearer standing on the same ground (K3)"), Wearer.Lost(), 0.0f, 0.001f);
	return true;
}

// ==========================================================================
// 56. The explosion of an enemy a trap kills never strikes its wearer (K4)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapSelfBlastKillExplosionTest,
	"Cataclysm.Traps.AnEnemyATrapKillsExplodesOnAnEnemyNearItAndNeverOnItsSelfBlastWearerAsNear")
{
	using namespace CataclysmTrapSelfBlastTest;

	// ONE PLAYER HOLDING A DEMONIC WHIP, THE HEAVY FLAG, THE DRAWBACK AND "Enemies killed by traps explode" AT 50: a Heavy
	// trap 20 m north, a doomed enemy of 50 health 2.5 m east of it, which the lash kills, an enemy 6 m east of the trap,
	// 3.5 m from the dead and beyond the lash's 5 m, and the wearer 1.2 m west of the trap, 3.7 m from the dead and
	// behind the lash.
	FTrapSelfBlastWorld Scope;
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
	constexpr float Doomed = 50.0f;
	FTrapSelfBlastRig Wearer(World, FVector::ZeroVector, TEXT("Whip"));
	if (!Wearer.IsUsable(*this, ECataclysmAbilitySlot::Heavy, ECataclysmSkillShape::Strike))
	{
		return false;
	}
	Wearer.Holds({TrapSelfBlastTrapFlag(TEXT("Slot.Heavy")), TrapSelfBlastDrawback()});
	Wearer.System->SetPoolActions({TrapSelfBlastExplosionRow()});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	ACataclysmEnemyCharacter* Dying = TrapSelfBlastEnemy(World, North + FVector(2.5f * M, 0.0f, 0.0f), Doomed);
	ACataclysmEnemyCharacter* Nearby = TrapSelfBlastEnemy(World, North + FVector(6.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the doomed enemy"), Dying) || !TestNotNull(TEXT("set-up: the enemy near it"), Nearby))
	{
		return false;
	}
	ACataclysmMinion* Trap = TrapSelfBlastPlaceTrap(*this, Wearer, ECataclysmAbilitySlot::Heavy, North);
	if (!Trap)
	{
		return false;
	}
	Wearer.StandsAt(North + FVector(-1.2f * M, 0.0f, 0.0f));
	const float WearerFromTheDead = TrapSelfBlastFloorDistance(Wearer.Player, Dying->GetActorLocation());
	TestTrue(*FString::Printf(TEXT("set-up: the wearer stands %.1f cm from the doomed enemy, within the 4 m"),
							  WearerFromTheDead),
		WearerFromTheDead >= 0.0f && WearerFromTheDead < 4.0f * M);

	// THE LASH KILLS, AND THE DEAD EXPLODES.
	TrapSelfBlastPass(World, 1.25f);
	TrapSelfBlastStep(Trap);
	TestTrue(TEXT("set-up: the trap's lash killed the doomed enemy"), UCataclysmSkillEffects::IsDead(Dying));
	TestTrue(*FString::Printf(TEXT("control: its explosion struck the enemy 3.5 m from it: %.2f"),
							  TrapSelfBlastTakenFrom(Nearby)),
		TrapSelfBlastTakenFrom(Nearby) > 0.0f);
	TestEqual(TEXT("and never the wearer 3.7 m from it (K4): neither the lash behind it nor the explosion struck it"),
		Wearer.Lost(), 0.0f, 0.001f);
	return true;
}

// ==========================================================================
// 57. A skill gadget's bonus shot at a trap's enemy strikes its wearer inside its area (K5)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapSelfBlastGadgetShotTest,
	"Cataclysm.Traps.AGadgetsBonusShotAtATrapsEnemyStrikesItsSelfBlastWearerInsideItsRingAndNeitherHalfAloneDoes")
{
	using namespace CataclysmTrapSelfBlastTest;

	// THREE PLAYERS 100 M APART HOLDING DEMONIC WHIPS, EACH WITH THE HEAVY TRAP FLAG AND THE ULTIMATE GADGET FLAG: a trap
	// 20 m north with its enemy 2.5 m east, a skill gadget 6 m west of the trap, whose Ultimate The Gathering is a 14 m
	// ring around the gadget, and the player 3 m west of the trap, between the two and behind the trap's lash. ONE TRAP
	// TRIGGERED AND NOTHING SET OFF (no chain row). The first holds the drawback and no shot row; the second the shot row
	// at 100 within 10 m and no drawback; the third both.
	FTrapSelfBlastWorld Scope;
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
	const FVector Homes[3] = {FVector::ZeroVector, FVector(100.0f * M, 0.0f, 0.0f), FVector(200.0f * M, 0.0f, 0.0f)};
	FTrapSelfBlastRig NoShot(World, Homes[0], TEXT("Whip"));
	FTrapSelfBlastRig NoDrawback(World, Homes[1], TEXT("Whip"));
	FTrapSelfBlastRig Both(World, Homes[2], TEXT("Whip"));
	for (const FTrapSelfBlastRig* Each : {&NoShot, &NoDrawback, &Both})
	{
		if (!Each->IsUsable(*this, ECataclysmAbilitySlot::Heavy, ECataclysmSkillShape::Strike)
			|| !Each->IsUsable(*this, ECataclysmAbilitySlot::Ultimate, ECataclysmSkillShape::Strike))
		{
			return false;
		}
	}
	NoShot.Holds({TrapSelfBlastTrapFlag(TEXT("Slot.Heavy")), TrapSelfBlastGadgetFlag(TEXT("Slot.Ultimate")),
				  TrapSelfBlastDrawback()});
	NoDrawback.Holds({TrapSelfBlastTrapFlag(TEXT("Slot.Heavy")), TrapSelfBlastGadgetFlag(TEXT("Slot.Ultimate"))});
	Both.Holds({TrapSelfBlastTrapFlag(TEXT("Slot.Heavy")), TrapSelfBlastGadgetFlag(TEXT("Slot.Ultimate")),
				TrapSelfBlastDrawback()});
	NoDrawback.System->SetPoolActions({TrapSelfBlastGadgetShotRow()});
	Both.System->SetPoolActions({TrapSelfBlastGadgetShotRow()});

	const FVector TrapAt(0.0f, 20.0f * M, 0.0f);
	const FVector GadgetAt = TrapAt + FVector(-6.0f * M, 0.0f, 0.0f);
	const FVector StandAt = TrapAt + FVector(-3.0f * M, 0.0f, 0.0f);
	const FTrapSelfBlastRig* Rigs[3] = {&NoShot, &NoDrawback, &Both};
	ACataclysmMinion* Traps[3] = {};
	ACataclysmMinion* Gadgets[3] = {};
	for (int32 Who = 0; Who < 3; ++Who)
	{
		Traps[Who] = TrapSelfBlastPlaceTrap(*this, *Rigs[Who], ECataclysmAbilitySlot::Heavy, Homes[Who] + TrapAt);
		if (!Traps[Who])
		{
			return false;
		}
	}
	// ARMED BY A STEP BEFORE ANY ENEMY STANDS NEAR: each arms and finds nobody. NO GADGET IS STEPPED, so no gadget casts
	// by its own clock.
	TrapSelfBlastPass(World, 1.25f);
	for (int32 Who = 0; Who < 3; ++Who)
	{
		TrapSelfBlastStep(Traps[Who]);
		Gadgets[Who] =
			TrapSelfBlastPlaceGadget(*this, *Rigs[Who], ECataclysmAbilitySlot::Ultimate, Homes[Who] + GadgetAt);
		if (!Gadgets[Who]
			|| !TestTrue(*FString::Printf(TEXT("set-up: player %d's trap is armed"), Who), Traps[Who]->bSkillTrapArmed))
		{
			return false;
		}
		Rigs[Who]->StandsAt(Homes[Who] + StandAt);
		if (!TestNotNull(*FString::Printf(TEXT("set-up: player %d's enemy"), Who),
				TrapSelfBlastEnemy(World, Homes[Who] + TrapAt + FVector(2.5f * M, 0.0f, 0.0f))))
		{
			return false;
		}
	}

	// THE CONTROLS, READ FIRST.
	TrapSelfBlastStep(Traps[0]);
	TestEqual(TEXT("control: the drawback alone: the trap triggered once"), Traps[0]->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("control: with no row the gadget fired nothing"), Gadgets[0]->SkillGadgetTrapShotsMade, 0);
	TestEqual(TEXT("control: and the wearer behind the trap's lash loses nothing"), NoShot.Lost(), 0.0f, 0.001f);
	TrapSelfBlastStep(Traps[1]);
	TestEqual(TEXT("control: the shot row alone: the gadget fired one shot"), Gadgets[1]->SkillGadgetTrapShotsMade, 1);
	TestEqual(TEXT("control: and the player inside its ring, holding no drawback, loses nothing"), NoDrawback.Lost(), 0.0f,
		0.001f);

	// BOTH: THE GADGET'S SHOT STRIKES THE WEARER INSIDE ITS RING.
	TrapSelfBlastStep(Traps[2]);
	TestEqual(TEXT("set-up: the gadget 6 m from the trap fired one shot"), Gadgets[2]->SkillGadgetTrapShotsMade, 1);
	TestTrue(*FString::Printf(TEXT("the gadget's bonus shot struck its wearer 3 m from it, inside its ring (K5): %.2f "
								   "lost"), Both.Lost()),
		Both.Lost() > 0.0f);
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
