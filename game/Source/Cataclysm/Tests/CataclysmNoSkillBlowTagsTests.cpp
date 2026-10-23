// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmBuriedWeapon.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
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
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Two blows of a skill that named no skill object now carry that skill's tags. Issue #2475, under the coordinating
 * session's ruling N1 of 2026-10-22 for #2465: "Every other blow a skill delivers carries that skill's tags." A trap's
 * rack axe landing after the trap's one-use grant is removed names the tags it was fired with
 * (`ACataclysmProjectile::HitOne`, `FCataclysmHitDelivery::SkillTagsWithNoSkill` when `FiringSkill` is stale), so its
 * kill carries them; and the hit notice of a blow naming no skill object names the tags its delivery named
 * (`UCataclysmCombatEvents::NoteBlow`), so a buried weapon's leap's hit carries its throwing skill's. A GROUP OF ITS OWN,
 * `Cataclysm.NoSkillBlowTags.`, because the change is to two kinds of blow, a projectile's and a leap's, under one rule;
 * #2465's tests are in `Cataclysm.BuriedWeaponKills.`, the rack's in `Cataclysm.SkillGadgets.` and `Cataclysm.Skills.`.
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER OF THE PLAYERS' SIDE HOLDING A DEMONIC AXE, given a weapon damage of 100,
 * 10,000 health and 1,000 mana by hand: its Ultimate, Butcher's Bill, is a rack of thirty axes at every enemy within
 * 10 m; its Special, Harrower, buries an axe that leaps. THE TRAP IS SET OFF WITH NO ENEMY (`TriggerSkillTrap(nullptr)`,
 * T11), so its cast is thrown from the trap's own place at the one enemy within its reach, and the rack is emptied by
 * hand with `ThrowOne`, as `Cataclysm.SkillGadgets.ARacksAxesLandingAfterItsUseHasEndedArePricedAsTheGadgets` empties
 * it: the last throw ends the use, and its one-use grant is removed while every axe is in the air. THE WEAPON IS BURIED
 * by `UCataclysmBuriedWeapon::BuryIn` with Harrower's own Tags cell and leaps when the test calls `LeapFromDying`, as
 * #2465's tests do. NO ROW IS AUTHORED, SO EVERY ACTION IS WRITTEN BY HAND, as the item would build it from its row.
 * Every body stands at least 1 m from every other, on a real side. NOTHING WAITS ON A TICK: the clock never moves and
 * every axe is stepped by hand. Blows are struck only with the critical strike roll pinned, after the world is made.
 * Helper types and functions are named after the subject, a blow of a skill naming no skill object.
 */
namespace CataclysmNoSkillBlowTagsTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The health every creature here starts with, unless a case says otherwise. */
	constexpr float NoSkillBlowEnemyHealth = 50000.0f;

	/** The health of an enemy one axe of the rack kills. */
	constexpr float NoSkillBlowDoomedHealth = 5.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FNoSkillBlowWorld
	{
		FNoSkillBlowWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FNoSkillBlowWorld()
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
	ACataclysmEnemyCharacter* NoSkillBlowEnemy(UWorld* World, const FVector& Where, float Health = NoSkillBlowEnemyHealth)
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

	/** What a creature has lost since it was made at `Health`; -1 for one with no ability system. */
	float NoSkillBlowTakenFrom(const AActor* Who, float Health = NoSkillBlowEnemyHealth)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Health - Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** A tag by name, or an invalid one. */
	FGameplayTag NoSkillBlowTag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	/**
	 * HARROWER'S OWN TAGS CELL, `Demonic_Axe_Special` in `game/Data/WeaponSkills.csv`, as its skill holds them: every tag
	 * of the cell, so `Type.Ranged` and `Type.Projectile` make the blow ranged and `Slot.Special` names its slot.
	 */
	FGameplayTagContainer NoSkillBlowHarrowerTags()
	{
		FGameplayTagContainer Tags;
		for (const TCHAR* Name : {TEXT("Item.Weapon.Axe"), TEXT("Element.Demonic"), TEXT("Type.Projectile"),
								  TEXT("Keyword.DoT.Burn"), TEXT("Trigger.OnDeath"), TEXT("Type.Ranged"),
								  TEXT("Slot.Special")})
		{
			Tags.AddTag(NoSkillBlowTag(Name));
		}
		return Tags;
	}

	/** Whether every tag these tests name exists. */
	bool NoSkillBlowTagsExist()
	{
		for (const TCHAR* Name : {TEXT("Item.Weapon.Axe"), TEXT("Element.Demonic"), TEXT("Type.Projectile"),
								  TEXT("Keyword.DoT.Burn"), TEXT("Trigger.OnDeath"), TEXT("Type.Ranged"),
								  TEXT("Slot.Special"), TEXT("Slot.Ultimate"), TEXT("Type.Trap")})
		{
			if (!NoSkillBlowTag(Name).IsValid())
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * "Enemies killed by traps explode, dealing 50%-100% of the trap's damage to all enemies within 4 meters" at 50, as
	 * traps layer 4's tests write it: `explode_victim_for_killing_blow` on `kill`, requiring `Type.Trap`.
	 */
	FCataclysmPoolAction NoSkillBlowTrapExplosionRow()
	{
		FCataclysmPoolAction Explodes;
		Explodes.Event = FName(TEXT("kill"));
		Explodes.Pool = FName(UCataclysmAbilitySystemComponent::ExplodeVictimForKillingBlowAction);
		Explodes.RequiredTags.AddTag(NoSkillBlowTag(TEXT("Type.Trap")));
		Explodes.TriggerKey = FName(TEXT("NoSkillBlowTest:kill:explode"));
		Explodes.bExplodeVictimForKillingBlow = true;
		Explodes.Percent = 50.0f;
		return Explodes;
	}

	/**
	 * A TEST-ONLY ROW of a shipped row's shape: `cooldown_reset_special` on `hit_dealt`, requiring `Type.Ranged`, at a
	 * chance of 100. "Your special ability cooldown is reset when you ..." is the shipped row of this action, on `kill`
	 * and requiring no tag; this one is on a hit and requires the tag a leap's hit now carries.
	 */
	FCataclysmPoolAction NoSkillBlowRangedHitResetsSpecialRow()
	{
		FCataclysmPoolAction Reset;
		Reset.Event = FName(TEXT("hit_dealt"));
		Reset.Pool = FName(UCataclysmAbilitySystemComponent::CooldownResetSpecialAction);
		Reset.RequiredTags.AddTag(NoSkillBlowTag(TEXT("Type.Ranged")));
		Reset.Percent = 100.0f;
		Reset.CooldownReset = ECataclysmCooldownReset::Special;
		Reset.ResetKey = FName(TEXT("NoSkillBlowTest:cooldown_reset_special"));
		Reset.TriggerKey = FName(TEXT("NoSkillBlowTest:hit_dealt:reset"));
		return Reset;
	}

	/** One row of a stat line a test's player holds: a flat figure under one required tag. */
	struct FNoSkillBlowStat
	{
		FName Stat;
		float Value = 0.0f;
		const TCHAR* RequiredTag = nullptr;
	};

	/** The Ultimate slot's trap flag: a flat 1 on `skill_slot_is_a_trap` requiring `Slot.Ultimate`, layers 1 and 2's row. */
	FNoSkillBlowStat NoSkillBlowUltimateTrapFlag()
	{
		return {FName(UCataclysmAbilitySystemComponent::SkillSlotIsATrapStat), 1.0f, TEXT("Slot.Ultimate")};
	}

	/** A possessed player character of the players' side holding a Demonic Axe, its ability system and its slots. */
	struct FNoSkillBlowRig
	{
		FNoSkillBlowRig(UWorld* World, const FVector& Where)
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
			Slots->EquipWeaponType(TEXT("Axe"));
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		}

		/** Whether everything was made. Reports why not. */
		bool IsMade(FAutomationTestBase& Test) const
		{
			if (!Player || !System || !Slots)
			{
				Test.AddError(TEXT("set-up: a possessed player holding a Demonic Axe"));
				return false;
			}
			return true;
		}

		/** Whether everything was made and the Axe's Ultimate is a rack of more than two a trap may cast. */
		bool HoldsARackATrapMayCast(FAutomationTestBase& Test) const
		{
			const FCataclysmWeaponSkill* Ultimate = Slots ? Slots->GetAvailableSkills().FindByPredicate(
				[](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == ECataclysmAbilitySlot::Ultimate; }) : nullptr;
			if (!IsMade(Test) || !Ultimate || !ACataclysmMinion::SkillGadgetMayCast(*Ultimate)
				|| Ultimate->Shape != ECataclysmSkillShape::Projectile || Ultimate->Params.Count <= 2)
			{
				Test.AddError(TEXT("set-up: the Demonic Axe's Ultimate is a rack of more than two a trap may cast. If a row "
								   "is missing, DT_WeaponSkills may be older than game/Data/WeaponSkills.csv: run "
								   "tools/generate_datatable_assets.py"));
				return false;
			}
			return true;
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			System->SetNumericAttributeBase(Attribute, Value);
		}

		/** REPLACES THE WHOLE STAT LINE with these rows. */
		void Holds(const TArray<FNoSkillBlowStat>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FNoSkillBlowStat& Each : Rows)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::Enchantment;
				Row.Value = Each.Value;
				if (Each.RequiredTag)
				{
					Row.RequiredTags.AddTag(NoSkillBlowTag(Each.RequiredTag));
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

	/** The running one-use grant of a rack: an active free start of a projectile skill, or null once its use has ended. */
	UCataclysmProjectileSkill* NoSkillBlowRunningRack(const UCataclysmAbilitySystemComponent* System)
	{
		for (const FGameplayAbilitySpec& Spec : System->GetActivatableAbilities())
		{
			UCataclysmProjectileSkill* Rack = Cast<UCataclysmProjectileSkill>(Spec.GetPrimaryInstance());
			if (Rack && Spec.IsActive() && Rack->bFreeRepeat)
			{
				return Rack;
			}
		}
		return nullptr;
	}

	/**
	 * Step every projectile in the world not in `Done` by a twentieth of a second until each has finished, adding each to
	 * `Done`. A test world never ticks. How many finished in this call.
	 */
	int32 NoSkillBlowLandEveryProjectile(UWorld* World, TSet<const ACataclysmProjectile*>& Done)
	{
		int32 Finished = 0;
		for (int32 Round = 0; Round < 400; ++Round)
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

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FNoSkillBlowEventCount
	{
		FNoSkillBlowEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
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
		~FNoSkillBlowEventCount()
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

	/** What one hit notice said, copied while it was handed out. */
	struct FNoSkillBlowHitHeard
	{
		const AActor* Attacker = nullptr;
		const AActor* Target = nullptr;
		bool bDamageOverTime = false;
		bool bHadSkillTags = false;
		FGameplayTagContainer SkillTags;
		FName SkillName;
	};

	/** Every hit notice the world hands out while this is alive. */
	struct FNoSkillBlowHits
	{
		explicit FNoSkillBlowHits(UWorld* InWorld)
			: Events(UCataclysmCombatEvents::In(InWorld))
		{
			if (Events)
			{
				Handle = Events->OnHit.AddLambda([this](const FCataclysmHitNotice& Notice)
				{
					FNoSkillBlowHitHeard& One = Heard.AddDefaulted_GetRef();
					One.Attacker = Notice.Attacker;
					One.Target = Notice.Target;
					One.bDamageOverTime = Notice.bDamageOverTime;
					One.bHadSkillTags = Notice.SkillTags != nullptr;
					if (Notice.SkillTags)
					{
						One.SkillTags = *Notice.SkillTags;
					}
					One.SkillName = Notice.SkillName;
				});
			}
		}
		~FNoSkillBlowHits()
		{
			if (Events)
			{
				Events->OnHit.Remove(Handle);
			}
		}

		/** How many notices of a blow, not a tick, from `Attacker` on `Target`. */
		int32 CountFor(const AActor* Attacker, const AActor* Target) const
		{
			int32 Found = 0;
			for (const FNoSkillBlowHitHeard& One : Heard)
			{
				Found += One.Attacker == Attacker && One.Target == Target && !One.bDamageOverTime ? 1 : 0;
			}
			return Found;
		}

		/** How many of those named skill tags holding `Tag`, and no skill. */
		int32 CountNamingTagAndNoSkill(const AActor* Attacker, const AActor* Target, const FGameplayTag& Tag) const
		{
			int32 Found = 0;
			for (const FNoSkillBlowHitHeard& One : Heard)
			{
				Found += One.Attacker == Attacker && One.Target == Target && !One.bDamageOverTime && One.bHadSkillTags
					&& One.SkillTags.HasTag(Tag) && One.SkillName.IsNone() ? 1 : 0;
			}
			return Found;
		}

		/** The last notice of a blow from `Attacker` on `Target`, or an empty one. */
		FNoSkillBlowHitHeard For(const AActor* Attacker, const AActor* Target) const
		{
			for (int32 Index = Heard.Num() - 1; Index >= 0; --Index)
			{
				if (Heard[Index].Attacker == Attacker && Heard[Index].Target == Target && !Heard[Index].bDamageOverTime)
				{
					return Heard[Index];
				}
			}
			return FNoSkillBlowHitHeard();
		}

		UCataclysmCombatEvents* Events = nullptr;
		TArray<FNoSkillBlowHitHeard> Heard;
		FDelegateHandle Handle;
	};

	/**
	 * Place an Ultimate trap for `Rig` at `TrapAt`, set it off with no enemy so it casts from its own place, and empty its
	 * rack by hand: the first axe leaves as the cast starts, `ThrowOne` throws the rest, and the last throw ends the use.
	 * Reports each set-up step. How many axes were thrown, or nought when a step failed.
	 */
	int32 NoSkillBlowEmptyATrapsRack(FAutomationTestBase& Test, const FNoSkillBlowRig& Rig, const FVector& TrapAt,
									 const TCHAR* Who)
	{
		ACataclysmMinion* Trap = ACataclysmMinion::PlaceSkillTrap(Rig.Player, ECataclysmAbilitySlot::Ultimate, TrapAt);
		if (!Test.TestNotNull(*FString::Printf(TEXT("set-up, %s: the Ultimate trap is placed (DT_MinionTypes needs the "
													 "SkillTrap row)"), Who), Trap))
		{
			return 0;
		}
		const TWeakObjectPtr<ACataclysmMinion> Watched(Trap);
		if (!Test.TestTrue(*FString::Printf(TEXT("set-up, %s: the trap is set off with no enemy and casts from its own "
												  "place"), Who), Trap->TriggerSkillTrap(nullptr)))
		{
			return 0;
		}
		Test.TestTrue(*FString::Printf(TEXT("set-up, %s: and is used up"), Who),
			!Watched.IsValid() || Watched->IsActorBeingDestroyed());
		UCataclysmProjectileSkill* Rack = NoSkillBlowRunningRack(Rig.System);
		if (!Test.TestNotNull(*FString::Printf(TEXT("set-up, %s: its rack runs, its first axe in the air"), Who), Rack))
		{
			return 0;
		}
		const TWeakObjectPtr<UCataclysmProjectileSkill> Grant(Rack);
		const int32 Count = Rack->Params.Count;
		for (int32 Each = 1; Each < Count; ++Each)
		{
			Rack->ThrowOne();
		}
		Rack = nullptr;
		if (!Test.TestNull(*FString::Printf(TEXT("set-up, %s: the last throw ended the rack's use"), Who),
				NoSkillBlowRunningRack(Rig.System))
			|| !Test.TestFalse(*FString::Printf(TEXT("set-up, %s: and the trap's one-use grant is removed, so every axe in "
													  "the air names a firing skill that is gone"), Who), Grant.IsValid()))
		{
			return 0;
		}
		return Count;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 1. A trap's rack axe landing after the trap's use has ended kills, and the dead explodes (traps layer 4's row)
// ==========================================================================

CATACLYSM_TEST(FCataclysmNoSkillBlowLateRackAxeKillTest,
	"Cataclysm.NoSkillBlowTags.ATrapsRackAxeLandingAfterItsUseHasEndedCarriesTheRacksTagsAndItsKillExplodes")
{
	using namespace CataclysmNoSkillBlowTagsTest;

	// THREE AXE PLAYERS 100 M APART, EACH WITH THE ULTIMATE TRAP FLAG: an Ultimate trap 20 m north of each, set off with no
	// enemy, so Butcher's Bill is thrown from the trap at the one enemy within 10 m of it, 3 m east, and the whole rack is
	// emptied with every axe still in the air. THE MEASURE's enemy is of 50,000 health, so each axe is read from what it
	// took. THE CONTROL's and THE ROW's are of 5, which the first axe to land kills; a bystander of 50,000 is made 3 m north
	// of each, once every axe is in the air, so no axe is thrown at it and none flies through it. THE ROW holds "Enemies
	// killed by traps explode" at 50.
	FNoSkillBlowWorld Scope;
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
	if (!TestTrue(TEXT("set-up: the tags these tests name exist"), NoSkillBlowTagsExist()))
	{
		return false;
	}
	const FVector Home[3] = {FVector::ZeroVector, FVector(100.0f * M, 0.0f, 0.0f), FVector(200.0f * M, 0.0f, 0.0f)};
	FNoSkillBlowRig Measure(World, Home[0]);
	FNoSkillBlowRig Plain(World, Home[1]);
	FNoSkillBlowRig Row(World, Home[2]);
	if (!Measure.HoldsARackATrapMayCast(*this) || !Plain.HoldsARackATrapMayCast(*this)
		|| !Row.HoldsARackATrapMayCast(*this))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the measure's and the row's weapon sub-type is Slashing, as a blow's resolution reads it"),
			UCataclysmWeaponSlotsComponent::SubTypeOf(Measure.Player).Equals(TEXT("Slashing"), ESearchCase::IgnoreCase)
			&& UCataclysmWeaponSlotsComponent::SubTypeOf(Row.Player).Equals(TEXT("Slashing"), ESearchCase::IgnoreCase)))
	{
		return false;
	}
	for (const FNoSkillBlowRig* Each : {&Measure, &Plain, &Row})
	{
		Each->Holds({NoSkillBlowUltimateTrapFlag()});
	}
	Row.System->SetPoolActions({NoSkillBlowTrapExplosionRow()});

	const FVector TrapAt(0.0f, 20.0f * M, 0.0f);
	const FVector TargetAt(3.0f * M, 20.0f * M, 0.0f);
	const FVector BystanderAt(3.0f * M, 23.0f * M, 0.0f);
	ACataclysmEnemyCharacter* MeasureTarget = NoSkillBlowEnemy(World, Home[0] + TargetAt);
	ACataclysmEnemyCharacter* PlainDoomed = NoSkillBlowEnemy(World, Home[1] + TargetAt, NoSkillBlowDoomedHealth);
	ACataclysmEnemyCharacter* RowDoomed = NoSkillBlowEnemy(World, Home[2] + TargetAt, NoSkillBlowDoomedHealth);
	const TArray<ACataclysmEnemyCharacter*> Made = {MeasureTarget, PlainDoomed, RowDoomed};
	for (int32 Index = 0; Index < Made.Num(); ++Index)
	{
		if (!TestNotNull(*FString::Printf(TEXT("set-up: creature %d"), Index), Made[Index]))
		{
			return false;
		}
	}
	const FGameplayTag Trap = NoSkillBlowTag(TEXT("Type.Trap"));
	FNoSkillBlowHits Hits(World);
	if (!TestNotNull(TEXT("set-up: the world's combat events"), Hits.Events))
	{
		return false;
	}
	FNoSkillBlowEventCount PlainKills(Plain.System, TEXT("kill"));
	FNoSkillBlowEventCount RowKills(Row.System, TEXT("kill"));
	TSet<const ACataclysmProjectile*> Done;

	// THE MEASURE: EVERY AXE OF THE RACK LANDS AFTER THE USE HAS ENDED, ON AN ENEMY IT DOES NOT KILL.
	const int32 MeasureCount = NoSkillBlowEmptyATrapsRack(*this, Measure, Home[0] + TrapAt, TEXT("the measure"));
	if (MeasureCount <= 0)
	{
		return false;
	}
	const int32 MeasureLanded = NoSkillBlowLandEveryProjectile(World, Done);
	TestEqual(TEXT("set-up: every axe of the measure's rack finished its flight"), MeasureLanded, MeasureCount);
	TestEqual(TEXT("set-up: and each struck its enemy once"), Hits.CountFor(Measure.Player, MeasureTarget), MeasureCount);
	TestEqual(TEXT("each late axe's hit notice names the rack's tags, Type.Trap among them, and no skill"),
		Hits.CountNamingTagAndNoSkill(Measure.Player, MeasureTarget, Trap), MeasureCount);
	const float PerAxe = NoSkillBlowTakenFrom(MeasureTarget) / static_cast<float>(MeasureCount);
	TestTrue(*FString::Printf(TEXT("set-up: one late axe, read off the measure, kills 5 health: %.2f"), PerAxe),
		PerAxe > NoSkillBlowDoomedHealth * 2.0f);

	// THE CONTROL, READ FIRST: THE SAME LATE AXE'S KILL, WITH NO ROW, EXPLODES NOTHING.
	if (NoSkillBlowEmptyATrapsRack(*this, Plain, Home[1] + TrapAt, TEXT("the control")) <= 0)
	{
		return false;
	}
	ACataclysmEnemyCharacter* PlainBystander = NoSkillBlowEnemy(World, Home[1] + BystanderAt);
	if (!TestNotNull(TEXT("set-up: the control's bystander, made with every axe in the air"), PlainBystander))
	{
		return false;
	}
	NoSkillBlowLandEveryProjectile(World, Done);
	TestTrue(TEXT("set-up: a late axe killed the control's enemy"), UCataclysmSkillEffects::IsDead(PlainDoomed));
	TestEqual(TEXT("set-up: and raised kill once on the control"), PlainKills.Count, 1);
	TestEqual(TEXT("control: with no row its bystander 3 m from the dead is untouched"),
		NoSkillBlowTakenFrom(PlainBystander), 0.0f, 0.001f);

	// THE ROW: THE LATE AXE'S KILL CARRIES THE RACK'S TAGS, `Type.Trap` AMONG THEM, SO THE DEAD EXPLODES.
	if (NoSkillBlowEmptyATrapsRack(*this, Row, Home[2] + TrapAt, TEXT("the row")) <= 0)
	{
		return false;
	}
	ACataclysmEnemyCharacter* RowBystander = NoSkillBlowEnemy(World, Home[2] + BystanderAt);
	if (!TestNotNull(TEXT("set-up: the row's bystander, made with every axe in the air"), RowBystander))
	{
		return false;
	}
	NoSkillBlowLandEveryProjectile(World, Done);
	TestTrue(TEXT("set-up: a late axe killed the row's enemy"), UCataclysmSkillEffects::IsDead(RowDoomed));
	TestEqual(TEXT("set-up: and raised kill once on the row's player"), RowKills.Count, 1);
	// THE EXPLOSION IS A SHARE OF THE BLOW AS SENT, BEFORE THE SLASHING BONUS (traps layer 4, F10), while the measure's
	// enemy lost each axe after it (`UCataclysmDamageCalculation::Resolve`, step 9), so the measure is divided by it.
	const float AxeAsSent = PerAxe / (1.0f + UCataclysmDamageCalculation::SubtypeBonus / 100.0f);
	TestEqual(*FString::Printf(TEXT("the late axe's dead explodes: its bystander 3 m away takes half the killing axe's own "
									"figure as sent, before the Slashing bonus the measure lost it with: %.2f against "
									"%.2f"), NoSkillBlowTakenFrom(RowBystander), AxeAsSent * 0.5f),
		NoSkillBlowTakenFrom(RowBystander), AxeAsSent * 0.5f, PerAxe * 0.005f);
	return true;
}

// ==========================================================================
// 2. A buried weapon's leap's hit carries Harrower's tags, so a hit row requiring Type.Ranged answers it
// ==========================================================================

CATACLYSM_TEST(FCataclysmNoSkillBlowLeapHitTest,
	"Cataclysm.NoSkillBlowTags.ALeapsHitCarriesTheThrowingSkillsTagsSoARangedHitRowAnswersItAndASkilllessBlowsHitDoesNot")
{
	using namespace CataclysmNoSkillBlowTagsTest;

	// TWO AXE PLAYERS 100 M APART, EACH HOLDING A TEST-ONLY ROW, `cooldown_reset_special` on `hit_dealt` requiring
	// `Type.Ranged` at 100, and with its Special cooldown running for 10 s. THE CONTROL strikes an enemy of 50,000 with a
	// blow of its own that names no skill and no tags. THE LEAPING PLAYER buries an axe with Harrower's tags in a host
	// 20 m north, and its leap strikes the nearest living enemy, of 50,000, 4 m east of the host, and kills nothing.
	FNoSkillBlowWorld Scope;
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
	const FGameplayTag SpecialCooldown = UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Special);
	if (!TestTrue(TEXT("set-up: the tags these tests name and the Special cooldown's tag exist"),
			NoSkillBlowTagsExist() && SpecialCooldown.IsValid()))
	{
		return false;
	}
	const FVector PlainHome(100.0f * M, 0.0f, 0.0f);
	FNoSkillBlowRig Leaping(World, FVector::ZeroVector);
	FNoSkillBlowRig Plain(World, PlainHome);
	if (!Leaping.IsMade(*this) || !Plain.IsMade(*this))
	{
		return false;
	}
	Leaping.System->SetPoolActions({NoSkillBlowRangedHitResetsSpecialRow()});
	Plain.System->SetPoolActions({NoSkillBlowRangedHitResetsSpecialRow()});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector East(4.0f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* Host = NoSkillBlowEnemy(World, North);
	ACataclysmEnemyCharacter* LeapTarget = NoSkillBlowEnemy(World, North + East);
	ACataclysmEnemyCharacter* PlainTarget = NoSkillBlowEnemy(World, PlainHome + North);
	const TArray<ACataclysmEnemyCharacter*> Made = {Host, LeapTarget, PlainTarget};
	for (int32 Index = 0; Index < Made.Num(); ++Index)
	{
		if (!TestNotNull(*FString::Printf(TEXT("set-up: creature %d"), Index), Made[Index]))
		{
			return false;
		}
	}
	UCataclysmSkillEffects::ApplyTagForDuration(Leaping.Player, Leaping.Player, SpecialCooldown, 10.0f);
	UCataclysmSkillEffects::ApplyTagForDuration(Plain.Player, Plain.Player, SpecialCooldown, 10.0f);
	if (!TestTrue(TEXT("set-up: the leaping player's Special cooldown runs"),
			Leaping.System->HasMatchingGameplayTag(SpecialCooldown))
		|| !TestTrue(TEXT("set-up: and the control's"), Plain.System->HasMatchingGameplayTag(SpecialCooldown)))
	{
		return false;
	}
	const FGameplayTag Ranged = NoSkillBlowTag(TEXT("Type.Ranged"));
	FNoSkillBlowHits Hits(World);
	if (!TestNotNull(TEXT("set-up: the world's combat events"), Hits.Events))
	{
		return false;
	}
	FNoSkillBlowEventCount LeapingHits(Leaping.System, TEXT("hit_dealt"));
	FNoSkillBlowEventCount PlainHits(Plain.System, TEXT("hit_dealt"));

	// THE CONTROL, READ FIRST: A HIT BY A BLOW THAT NAMES NO SKILL CARRIES NO TAGS, SO THE ROW REFUSES IT.
	UCataclysmSkillEffects::ApplyHit(Plain.Player, PlainTarget, 100.0f, FGameplayTagContainer(), FCataclysmHitDelivery());
	TestTrue(TEXT("set-up: the control's own blow struck its enemy"), NoSkillBlowTakenFrom(PlainTarget) > 0.0f);
	TestEqual(TEXT("set-up: and raised hit_dealt once on the control"), PlainHits.Count, 1);
	TestFalse(TEXT("control: its hit notice names no skill tags"), Hits.For(Plain.Player, PlainTarget).bHadSkillTags);
	TestTrue(TEXT("control: so the ranged hit row refuses it and the Special cooldown still runs"),
		Plain.System->HasMatchingGameplayTag(SpecialCooldown));

	// THE PLAYER'S OWN LEAP.
	if (!TestNotNull(TEXT("set-up: the axe is buried in the host with Harrower's tags"),
			UCataclysmBuriedWeapon::BuryIn(Host, Leaping.Player, 10.0f * M, 100.0f, NoSkillBlowHarrowerTags(),
										   /*bInBurns=*/false)))
	{
		return false;
	}
	TestTrue(TEXT("set-up: the axe leaps from its host"), UCataclysmBuriedWeapon::LeapFromDying(Host));
	TestTrue(TEXT("set-up: and its leap struck the enemy 4 m east"), NoSkillBlowTakenFrom(LeapTarget) > 0.0f);
	TestFalse(TEXT("set-up: and killed nothing"), UCataclysmSkillEffects::IsDead(LeapTarget));
	TestEqual(TEXT("set-up: and raised hit_dealt once on the leaping player"), LeapingHits.Count, 1);
	const FNoSkillBlowHitHeard LeapHit = Hits.For(Leaping.Player, LeapTarget);
	TestTrue(TEXT("the leap's hit notice names skill tags though its blow names no skill (#2475)"), LeapHit.bHadSkillTags);
	TestTrue(TEXT("among them Type.Ranged, the throwing skill's"), LeapHit.SkillTags.HasTag(Ranged));
	TestTrue(TEXT("and it names no skill"), LeapHit.SkillName.IsNone());
	TestFalse(TEXT("so the ranged hit row answers the leap's hit and resets the Special cooldown"),
		Leaping.System->HasMatchingGameplayTag(SpecialCooldown));
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
