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
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
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
 * A buried weapon's leap kill carries its throwing skill's tags. Issue #2465, ruled 2026-10-22 by the coordinating
 * session: N1 (a) "a leap's kill carries the throwing skill's tags", N2 (a) the leap's delivery names the throwing
 * skill's tags (`UCataclysmBuriedWeapon::SkillTags`), so `FCataclysmLastBlow` and the kill notice carry them as a cast's
 * blow does, and N3 (a) the leap's blow takes melee or ranged from the skill's tags. A group of its own,
 * `Cataclysm.BuriedWeaponKills.`, because the change is to the kill a buried weapon's leap makes, a player's own and a
 * trap's alike; the leap's earlier tests are in `Cataclysm.Skills.` and `Cataclysm.Traps.`.
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER HOLDING A DEMONIC WEAPON, given a weapon damage of 100, 10,000 health and
 * 1,000 mana by hand: an Axe, whose Special is Harrower, for a player's own leap; a Whip, whose Heavy Searing Lash a trap
 * may cast, for a trap's (layer 3b's and layer 4's rig). THE WEAPON IS BURIED AS `UCataclysmProjectileSkill::BuryInStruck`
 * BURIES IT, by `UCataclysmBuriedWeapon::BuryIn` with Harrower's own Tags cell, and leaps when the test calls
 * `LeapFromDying` on its host, as the leap's earlier tests do. NO ROW IS AUTHORED, SO EVERY ACTION IS WRITTEN BY HAND, as
 * the item would build it from its row. Every body stands at least 1 m from every other, on a real side. Nothing waits on
 * a tick. Blows are struck only with the critical strike roll pinned, after the world is made. Helper types and functions
 * are named after the subject, the leap's kill.
 */
namespace CataclysmLeapKillTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The health every creature here starts with, unless a case says otherwise. */
	constexpr float LeapKillEnemyHealth = 50000.0f;

	/** The health of an enemy one blow kills. */
	constexpr float LeapKillDoomedHealth = 50.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FLeapKillWorld
	{
		FLeapKillWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FLeapKillWorld()
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
	ACataclysmEnemyCharacter* LeapKillEnemy(UWorld* World, const FVector& Where, float Health = LeapKillEnemyHealth)
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

	/** What a creature has lost since it was made at `Health`. */
	float LeapKillTakenFrom(const AActor* Who, float Health = LeapKillEnemyHealth)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Health - Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** A tag by name, or an invalid one. */
	FGameplayTag LeapKillTag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	/**
	 * HARROWER'S OWN TAGS CELL, `Demonic_Axe_Special` in `game/Data/WeaponSkills.csv`, as its skill holds them, and
	 * `Type.Trap` too for a trap's cast of it, which `TriggerSkillTrap` stamps. Every tag of the cell, so `Slot.Special`
	 * names the slot a cooldown refund clears and `Type.Ranged` and `Type.Projectile` make the blow ranged.
	 */
	FGameplayTagContainer LeapKillHarrowerTags(bool bAsATrapsCast)
	{
		FGameplayTagContainer Tags;
		for (const TCHAR* Name : {TEXT("Item.Weapon.Axe"), TEXT("Element.Demonic"), TEXT("Type.Projectile"),
								  TEXT("Keyword.DoT.Burn"), TEXT("Trigger.OnDeath"), TEXT("Type.Ranged"),
								  TEXT("Slot.Special")})
		{
			Tags.AddTag(LeapKillTag(Name));
		}
		if (bAsATrapsCast)
		{
			Tags.AddTag(LeapKillTag(TEXT("Type.Trap")));
		}
		return Tags;
	}

	/** Whether every tag `LeapKillHarrowerTags` names exists. */
	bool LeapKillHarrowerTagsExist()
	{
		for (const TCHAR* Name : {TEXT("Item.Weapon.Axe"), TEXT("Element.Demonic"), TEXT("Type.Projectile"),
								  TEXT("Keyword.DoT.Burn"), TEXT("Trigger.OnDeath"), TEXT("Type.Ranged"),
								  TEXT("Slot.Special"), TEXT("Type.Trap"), TEXT("Type.Melee")})
		{
			if (!LeapKillTag(Name).IsValid())
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * "Ranged kills have a 20%-40% chance to refund the cooldown of the skill used" at 100, as the item builds the row:
	 * `cooldown_reset_event_skill` on `kill`, requiring `Type.Ranged`, its chance the value.
	 */
	FCataclysmPoolAction LeapKillRangedRefundRow()
	{
		FCataclysmPoolAction Refund;
		Refund.Event = FName(TEXT("kill"));
		Refund.Pool = FName(UCataclysmAbilitySystemComponent::CooldownResetEventSkillAction);
		Refund.RequiredTags.AddTag(LeapKillTag(TEXT("Type.Ranged")));
		Refund.Percent = 100.0f;
		Refund.CooldownReset = ECataclysmCooldownReset::EventSkill;
		Refund.ResetKey = FName(TEXT("LeapKillTest:cooldown_reset_event_skill"));
		Refund.TriggerKey = FName(TEXT("LeapKillTest:kill:refund"));
		return Refund;
	}

	/**
	 * "Enemies killed by traps explode, dealing 50%-100% of the trap's damage to all enemies within 4 meters" at 50, as
	 * traps layer 4's tests write it: `explode_victim_for_killing_blow` on `kill`, requiring `Type.Trap`.
	 */
	FCataclysmPoolAction LeapKillTrapExplosionRow()
	{
		FCataclysmPoolAction Explodes;
		Explodes.Event = FName(TEXT("kill"));
		Explodes.Pool = FName(UCataclysmAbilitySystemComponent::ExplodeVictimForKillingBlowAction);
		Explodes.RequiredTags.AddTag(LeapKillTag(TEXT("Type.Trap")));
		Explodes.TriggerKey = FName(TEXT("LeapKillTest:kill:explode"));
		Explodes.bExplodeVictimForKillingBlow = true;
		Explodes.Percent = 50.0f;
		return Explodes;
	}

	/** One row of a stat line a test's player holds: a flat figure under one required tag. */
	struct FLeapKillStat
	{
		FName Stat;
		float Value = 0.0f;
		const TCHAR* RequiredTag = nullptr;
	};

	/** The Heavy slot's trap flag: a flat 1 on `skill_slot_is_a_trap` requiring `Slot.Heavy`, layers 1 and 2's row. */
	FLeapKillStat LeapKillHeavyTrapFlag()
	{
		return {FName(UCataclysmAbilitySystemComponent::SkillSlotIsATrapStat), 1.0f, TEXT("Slot.Heavy")};
	}

	/** A possessed player character holding a Demonic weapon, its ability system and its weapon slots. */
	struct FLeapKillRig
	{
		FLeapKillRig(UWorld* World, const FVector& Where, const TCHAR* Weapon)
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

		/** Whether everything was made. Reports why not. */
		bool IsMade(FAutomationTestBase& Test) const
		{
			if (!Player || !System || !Slots)
			{
				Test.AddError(TEXT("set-up: a possessed player holding a Demonic weapon"));
				return false;
			}
			return true;
		}

		/** Whether everything was made and the weapon holds a Heavy row a trap may cast. Reports why not. */
		bool CanPlaceAHeavyTrap(FAutomationTestBase& Test) const
		{
			const FCataclysmWeaponSkill* Heavy = Slots ? Slots->GetAvailableSkills().FindByPredicate(
				[](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == ECataclysmAbilitySlot::Heavy; }) : nullptr;
			if (!IsMade(Test) || !Heavy || !ACataclysmMinion::SkillGadgetMayCast(*Heavy))
			{
				Test.AddError(TEXT("set-up: the weapon holds a Heavy row a trap may cast. If a row is missing, "
								   "DT_WeaponSkills may be older than game/Data/WeaponSkills.csv: run "
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
		void Holds(const TArray<FLeapKillStat>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FLeapKillStat& Each : Rows)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::Enchantment;
				Row.Value = Each.Value;
				if (Each.RequiredTag)
				{
					Row.RequiredTags.AddTag(LeapKillTag(Each.RequiredTag));
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

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FLeapKillEventCount
	{
		FLeapKillEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
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
		~FLeapKillEventCount()
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

	/** What one death notice said, copied while it was handed out. */
	struct FLeapKillNoticeHeard
	{
		const AActor* Victim = nullptr;
		bool bIsMelee = false;
		bool bIsRanged = false;
		bool bByDamageOverTime = false;
		bool bHadSkillTags = false;
		FGameplayTagContainer SkillTags;
		FName SkillName;
	};

	/** Every death notice the world hands out while this is alive. */
	struct FLeapKillNotices
	{
		explicit FLeapKillNotices(UWorld* InWorld)
			: Events(UCataclysmCombatEvents::In(InWorld))
		{
			if (Events)
			{
				Handle = Events->OnDeath.AddLambda([this](const FCataclysmDeathNotice& Notice)
				{
					FLeapKillNoticeHeard& One = Heard.AddDefaulted_GetRef();
					One.Victim = Notice.Victim;
					One.bIsMelee = Notice.bIsMelee;
					One.bIsRanged = Notice.bIsRanged;
					One.bByDamageOverTime = Notice.bByDamageOverTime;
					One.bHadSkillTags = Notice.KillingSkillTags != nullptr;
					if (Notice.KillingSkillTags)
					{
						One.SkillTags = *Notice.KillingSkillTags;
					}
					One.SkillName = Notice.KillingSkillName;
				});
			}
		}
		~FLeapKillNotices()
		{
			if (Events)
			{
				Events->OnDeath.Remove(Handle);
			}
		}

		/** How many notices named `Victim`. */
		int32 CountFor(const AActor* Victim) const
		{
			int32 Found = 0;
			for (const FLeapKillNoticeHeard& One : Heard)
			{
				Found += One.Victim == Victim ? 1 : 0;
			}
			return Found;
		}

		/** The last notice naming `Victim`, or an empty one. */
		FLeapKillNoticeHeard For(const AActor* Victim) const
		{
			for (int32 Index = Heard.Num() - 1; Index >= 0; --Index)
			{
				if (Heard[Index].Victim == Victim)
				{
					return Heard[Index];
				}
			}
			return FLeapKillNoticeHeard();
		}

		UCataclysmCombatEvents* Events = nullptr;
		TArray<FLeapKillNoticeHeard> Heard;
		FDelegateHandle Handle;
	};
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 1. A player's own Harrower leap kill answers a kill row requiring Type.Ranged (N1, N2)
// ==========================================================================

CATACLYSM_TEST(FCataclysmLeapKillRangedRowTest,
	"Cataclysm.BuriedWeaponKills.APlayersOwnHarrowerLeapKillRefundsTheSpecialCooldownByTheRangedKillRowAndASkilllessBlowsKillDoesNot")
{
	using namespace CataclysmLeapKillTest;

	// TWO AXE PLAYERS 100 M APART, EACH HOLDING "Ranged kills have a 20%-40% chance to refund the cooldown of the skill
	// used" at 100 and with its Special cooldown running for 10 s. THE CONTROL kills an enemy of 50 health with a blow of
	// its own that names no skill and no tags; THE LEAPING PLAYER buries an axe with Harrower's tags in a host 20 m north
	// and its leap kills an enemy of 50 health 4 m east of the host, the nearest living enemy to it.
	FLeapKillWorld Scope;
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
	if (!TestTrue(TEXT("set-up: Harrower's tags, Type.Trap, Type.Melee and the Special cooldown's tag exist"),
			LeapKillHarrowerTagsExist() && SpecialCooldown.IsValid()))
	{
		return false;
	}
	const FVector PlainHome(100.0f * M, 0.0f, 0.0f);
	FLeapKillRig Leaping(World, FVector::ZeroVector, TEXT("Axe"));
	FLeapKillRig Plain(World, PlainHome, TEXT("Axe"));
	if (!Leaping.IsMade(*this) || !Plain.IsMade(*this))
	{
		return false;
	}
	Leaping.System->SetPoolActions({LeapKillRangedRefundRow()});
	Plain.System->SetPoolActions({LeapKillRangedRefundRow()});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector East(4.0f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* Host = LeapKillEnemy(World, North);
	ACataclysmEnemyCharacter* LeapDoomed = LeapKillEnemy(World, North + East, LeapKillDoomedHealth);
	ACataclysmEnemyCharacter* PlainDoomed = LeapKillEnemy(World, PlainHome + North, LeapKillDoomedHealth);
	const TArray<ACataclysmEnemyCharacter*> Made = {Host, LeapDoomed, PlainDoomed};
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
	FLeapKillEventCount LeapingKills(Leaping.System, TEXT("kill"));
	FLeapKillEventCount PlainKills(Plain.System, TEXT("kill"));

	// THE CONTROL, READ FIRST: A KILL BY A BLOW THAT NAMES NO SKILL CARRIES NO TAGS, SO THE ROW REFUSES IT.
	UCataclysmSkillEffects::ApplyHit(Plain.Player, PlainDoomed, 100.0f, FGameplayTagContainer(), FCataclysmHitDelivery());
	TestTrue(TEXT("set-up: the control's own blow killed its enemy"), UCataclysmSkillEffects::IsDead(PlainDoomed));
	TestEqual(TEXT("set-up: and raised kill once on the control"), PlainKills.Count, 1);
	TestTrue(TEXT("control: a kill whose blow names no skill carries no tags, so the ranged kill row refuses it and the "
				  "Special cooldown still runs"),
		Plain.System->HasMatchingGameplayTag(SpecialCooldown));

	// THE PLAYER'S OWN LEAP.
	if (!TestNotNull(TEXT("set-up: the axe is buried in the host with Harrower's tags and no caster"),
			UCataclysmBuriedWeapon::BuryIn(Host, Leaping.Player, 10.0f * M, 100.0f, LeapKillHarrowerTags(false),
										   /*bInBurns=*/false)))
	{
		return false;
	}
	TestTrue(TEXT("set-up: the axe leaps from its host"), UCataclysmBuriedWeapon::LeapFromDying(Host));
	TestTrue(TEXT("set-up: and its leap killed the enemy 4 m east"), UCataclysmSkillEffects::IsDead(LeapDoomed));
	TestEqual(TEXT("set-up: and raised kill once on the leaping player"), LeapingKills.Count, 1);
	TestFalse(TEXT("the leap's kill carries Harrower's tags, Type.Ranged and Slot.Special among them, so the ranged kill "
				   "row refunds the Special cooldown"),
		Leaping.System->HasMatchingGameplayTag(SpecialCooldown));
	return true;
}

// ==========================================================================
// 2. A trap's buried weapon's leap kill explodes (traps layer 4's row), and the same leap with no row does not
// ==========================================================================

CATACLYSM_TEST(FCataclysmLeapKillTrapExplosionTest,
	"Cataclysm.BuriedWeaponKills.ATrapsBuriedWeaponLeapKillExplodesForItsShareAndTheExplosionsOwnKillDoesNot")
{
	using namespace CataclysmLeapKillTest;

	// THREE WHIP PLAYERS 100 M APART, EACH WITH THE HEAVY TRAP FLAG; a Heavy trap 20 m north of each, set off by an
	// enemy 2.5 m east of it, and used up. Then an axe with Harrower's tags and `Type.Trap`, as a trap's cast of it carries
	// them, is buried with that used-up trap's identity in a host 50 m north of each player, and leaps to the nearest
	// living enemy, 4 m east of the host. THE MEASURE's is of 50,000 health, so the whole leap is read from what it took.
	// THE CONTROL's and THE ROW's are of 50 health, which the leap kills; each has a bystander of 50,000 3 m south of the
	// dead, inside the 4 m. THE ROW holds "Enemies killed by traps explode" at 50; and a brittle bystander of 10 health 3 m
	// north of the dead, which the explosion kills, with a third 3 m beyond it and 6 m from the dead.
	FLeapKillWorld Scope;
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
	if (!TestTrue(TEXT("set-up: Harrower's tags, Type.Trap and Type.Melee exist"), LeapKillHarrowerTagsExist()))
	{
		return false;
	}
	constexpr float Brittle = 10.0f;
	const FVector Home[3] = {FVector::ZeroVector, FVector(100.0f * M, 0.0f, 0.0f), FVector(200.0f * M, 0.0f, 0.0f)};
	FLeapKillRig Measure(World, Home[0], TEXT("Whip"));
	FLeapKillRig Plain(World, Home[1], TEXT("Whip"));
	FLeapKillRig Row(World, Home[2], TEXT("Whip"));
	if (!Measure.CanPlaceAHeavyTrap(*this) || !Plain.CanPlaceAHeavyTrap(*this) || !Row.CanPlaceAHeavyTrap(*this))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the measure's and the row's weapon sub-type is Slashing, as a blow's resolution reads it"),
			UCataclysmWeaponSlotsComponent::SubTypeOf(Measure.Player).Equals(TEXT("Slashing"), ESearchCase::IgnoreCase)
			&& UCataclysmWeaponSlotsComponent::SubTypeOf(Row.Player).Equals(TEXT("Slashing"), ESearchCase::IgnoreCase)))
	{
		return false;
	}
	for (const FLeapKillRig* Each : {&Measure, &Plain, &Row})
	{
		Each->Holds({LeapKillHeavyTrapFlag()});
	}
	Row.System->SetPoolActions({LeapKillTrapExplosionRow()});

	const FVector TrapAt(0.0f, 20.0f * M, 0.0f);
	const FVector SetterAt(2.5f * M, 20.0f * M, 0.0f);
	const FVector HostAt(0.0f, 50.0f * M, 0.0f);
	const FVector NextAt(4.0f * M, 50.0f * M, 0.0f);
	const FVector InsideAt(4.0f * M, 47.0f * M, 0.0f);
	const FVector BrittleAt(4.0f * M, 53.0f * M, 0.0f);
	const FVector BeyondAt(4.0f * M, 56.0f * M, 0.0f);
	ACataclysmEnemyCharacter* MeasureSetter = LeapKillEnemy(World, Home[0] + SetterAt);
	ACataclysmEnemyCharacter* PlainSetter = LeapKillEnemy(World, Home[1] + SetterAt);
	ACataclysmEnemyCharacter* RowSetter = LeapKillEnemy(World, Home[2] + SetterAt);
	ACataclysmEnemyCharacter* MeasureHost = LeapKillEnemy(World, Home[0] + HostAt);
	ACataclysmEnemyCharacter* MeasureNext = LeapKillEnemy(World, Home[0] + NextAt);
	ACataclysmEnemyCharacter* PlainHost = LeapKillEnemy(World, Home[1] + HostAt);
	ACataclysmEnemyCharacter* PlainDoomed = LeapKillEnemy(World, Home[1] + NextAt, LeapKillDoomedHealth);
	ACataclysmEnemyCharacter* PlainInside = LeapKillEnemy(World, Home[1] + InsideAt);
	ACataclysmEnemyCharacter* RowHost = LeapKillEnemy(World, Home[2] + HostAt);
	ACataclysmEnemyCharacter* RowDoomed = LeapKillEnemy(World, Home[2] + NextAt, LeapKillDoomedHealth);
	ACataclysmEnemyCharacter* RowInside = LeapKillEnemy(World, Home[2] + InsideAt);
	ACataclysmEnemyCharacter* RowBrittle = LeapKillEnemy(World, Home[2] + BrittleAt, Brittle);
	ACataclysmEnemyCharacter* RowBeyond = LeapKillEnemy(World, Home[2] + BeyondAt);
	const TArray<ACataclysmEnemyCharacter*> Made = {MeasureSetter, PlainSetter, RowSetter, MeasureHost, MeasureNext,
		PlainHost, PlainDoomed, PlainInside, RowHost, RowDoomed, RowInside, RowBrittle, RowBeyond};
	for (int32 Index = 0; Index < Made.Num(); ++Index)
	{
		if (!TestNotNull(*FString::Printf(TEXT("set-up: creature %d"), Index), Made[Index]))
		{
			return false;
		}
	}

	// THE THREE TRAPS SET OFF AND USED UP, so each axe below is a used-up trap's, as layer 3b's leap test buries it.
	TArray<TWeakObjectPtr<AActor>> UsedUp;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FLeapKillRig& Rig = Index == 0 ? Measure : (Index == 1 ? Plain : Row);
		ACataclysmMinion* Placed = ACataclysmMinion::PlaceSkillTrap(Rig.Player, ECataclysmAbilitySlot::Heavy,
																	 Home[Index] + TrapAt);
		if (!TestNotNull(*FString::Printf(TEXT("set-up: trap %d is placed (DT_MinionTypes needs the SkillTrap row)"),
										  Index), Placed))
		{
			return false;
		}
		UsedUp.Add(TWeakObjectPtr<AActor>(Placed));
	}
	World->TimeSeconds += 1.25f;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		ACataclysmMinion::GadgetStep(UsedUp[Index].Get());
		if (!TestTrue(*FString::Printf(TEXT("set-up: trap %d triggered and was used up"), Index),
				!UsedUp[Index].IsValid() || UsedUp[Index]->IsActorBeingDestroyed()))
		{
			return false;
		}
	}
	const FGameplayTagContainer CastTags = LeapKillHarrowerTags(true);
	if (!TestNotNull(TEXT("set-up: the measure's axe is buried with its used-up trap's identity"),
			UCataclysmBuriedWeapon::BuryIn(MeasureHost, Measure.Player, 10.0f * M, 100.0f, CastTags, false, UsedUp[0]))
		|| !TestNotNull(TEXT("set-up: the control's"),
			UCataclysmBuriedWeapon::BuryIn(PlainHost, Plain.Player, 10.0f * M, 100.0f, CastTags, false, UsedUp[1]))
		|| !TestNotNull(TEXT("set-up: the row's"),
			UCataclysmBuriedWeapon::BuryIn(RowHost, Row.Player, 10.0f * M, 100.0f, CastTags, false, UsedUp[2])))
	{
		return false;
	}
	FLeapKillEventCount PlainKills(Plain.System, TEXT("kill"));
	FLeapKillEventCount RowKills(Row.System, TEXT("kill"));

	// THE MEASURE: THE WHOLE OF A TRAP'S LEAP, READ OFF AN ENEMY IT DOES NOT KILL.
	TestTrue(TEXT("set-up: the measure's axe leaps"), UCataclysmBuriedWeapon::LeapFromDying(MeasureHost));
	const float Leap = LeapKillTakenFrom(MeasureNext);
	TestTrue(*FString::Printf(TEXT("set-up: the leap's whole blow, read off the measure, kills 50 health: %.2f"), Leap),
		Leap > LeapKillDoomedHealth * 2.0f);

	// THE CONTROL, READ FIRST: THE SAME LEAP'S KILL, WITH NO ROW, EXPLODES NOTHING.
	TestTrue(TEXT("set-up: the control's axe leaps"), UCataclysmBuriedWeapon::LeapFromDying(PlainHost));
	TestTrue(TEXT("set-up: and kills the enemy 4 m east"), UCataclysmSkillEffects::IsDead(PlainDoomed));
	TestEqual(TEXT("set-up: and raised kill once on the control"), PlainKills.Count, 1);
	TestEqual(TEXT("control: with no row its bystander 3 m from the dead is untouched"),
		LeapKillTakenFrom(PlainInside), 0.0f, 0.001f);

	// THE ROW: THE LEAP'S KILL CARRIES THE TRAP'S CAST'S TAGS, `Type.Trap` AMONG THEM, SO THE DEAD EXPLODES.
	TestTrue(TEXT("set-up: the row's axe leaps"), UCataclysmBuriedWeapon::LeapFromDying(RowHost));
	TestTrue(TEXT("set-up: and kills the enemy 4 m east"), UCataclysmSkillEffects::IsDead(RowDoomed));
	// THE EXPLOSION IS A SHARE OF THE BLOW AS SENT, BEFORE THE SLASHING BONUS (traps layer 4, F10 and R-ii), while the
	// measure enemy lost the blow after it (`UCataclysmDamageCalculation::Resolve`, step 9), so the measure is divided by it.
	const float LeapAsSent = Leap / (1.0f + UCataclysmDamageCalculation::SubtypeBonus / 100.0f);
	TestEqual(*FString::Printf(TEXT("the leap's dead explodes: its bystander 3 m away takes half the killing leap's own "
									"figure as sent, before the Slashing bonus the measure lost it with: %.2f against "
									"%.2f"), LeapKillTakenFrom(RowInside), LeapAsSent * 0.5f),
		LeapKillTakenFrom(RowInside), LeapAsSent * 0.5f, Leap * 0.001f);
	// AND NO OTHER BLOW TAKES THE LEAP'S TAGS: the explosion's kill, made while the leap is still being dealt, carries none.
	TestTrue(TEXT("set-up: the explosion killed the brittle bystander 3 m from the dead"),
		UCataclysmSkillEffects::IsDead(RowBrittle));
	TestEqual(TEXT("whose own death explodes nothing: the bystander 3 m beyond it and 6 m from the leap's dead is "
				   "untouched"),
		LeapKillTakenFrom(RowBeyond), 0.0f, 0.001f);
	TestEqual(TEXT("set-up: the row's player raised kill twice, the leap's and the explosion's"), RowKills.Count, 2);
	return true;
}

// ==========================================================================
// 3. The leap's kill is ranged and not melee, from the skill's tags, as a melee kill is melee (N3)
// ==========================================================================

CATACLYSM_TEST(FCataclysmLeapKillRangedNotMeleeTest,
	"Cataclysm.BuriedWeaponKills.AHarrowerLeapsKillNoticeIsRangedAndNotMeleeAndCarriesTheThrowingSkillsTagsAndAMeleeKillsIsMelee")
{
	using namespace CataclysmLeapKillTest;

	// TWO AXE PLAYERS 100 M APART. THE LEAPING PLAYER's axe, buried with Harrower's tags in a host 20 m north, leaps to
	// and kills an enemy of 50 health 4 m east of it. THE CONTROL kills an enemy of 50 health with a blow of its own
	// carrying `Type.Melee` and naming no skill. Every death notice the world hands out is copied as it is heard.
	FLeapKillWorld Scope;
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
	if (!TestTrue(TEXT("set-up: Harrower's tags, Type.Trap and Type.Melee exist"), LeapKillHarrowerTagsExist()))
	{
		return false;
	}
	const FVector MeleeHome(100.0f * M, 0.0f, 0.0f);
	FLeapKillRig Leaping(World, FVector::ZeroVector, TEXT("Axe"));
	FLeapKillRig Melee(World, MeleeHome, TEXT("Axe"));
	if (!Leaping.IsMade(*this) || !Melee.IsMade(*this))
	{
		return false;
	}
	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector East(4.0f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* Host = LeapKillEnemy(World, North);
	ACataclysmEnemyCharacter* LeapDoomed = LeapKillEnemy(World, North + East, LeapKillDoomedHealth);
	ACataclysmEnemyCharacter* MeleeDoomed = LeapKillEnemy(World, MeleeHome + North, LeapKillDoomedHealth);
	const TArray<ACataclysmEnemyCharacter*> Made = {Host, LeapDoomed, MeleeDoomed};
	for (int32 Index = 0; Index < Made.Num(); ++Index)
	{
		if (!TestNotNull(*FString::Printf(TEXT("set-up: creature %d"), Index), Made[Index]))
		{
			return false;
		}
	}
	const FGameplayTag Ranged = LeapKillTag(TEXT("Type.Ranged"));
	const FGameplayTag Slot = LeapKillTag(TEXT("Slot.Special"));
	FLeapKillNotices Notices(World);
	if (!TestNotNull(TEXT("set-up: the world's combat events"), Notices.Events))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: A MELEE BLOW'S KILL.
	FGameplayTagContainer MeleeTags;
	MeleeTags.AddTag(LeapKillTag(TEXT("Type.Melee")));
	UCataclysmSkillEffects::ApplyHit(Melee.Player, MeleeDoomed, 100.0f, MeleeTags, FCataclysmHitDelivery());
	TestTrue(TEXT("set-up: the control's melee blow killed its enemy"), UCataclysmSkillEffects::IsDead(MeleeDoomed));
	TestEqual(TEXT("set-up: one death notice named it"), Notices.CountFor(MeleeDoomed), 1);
	const FLeapKillNoticeHeard MeleeKill = Notices.For(MeleeDoomed);
	TestTrue(TEXT("control: a melee blow's kill is melee"), MeleeKill.bIsMelee);
	TestFalse(TEXT("control: and not ranged"), MeleeKill.bIsRanged);
	TestFalse(TEXT("control: and a blow naming no skill gives its kill no skill's tags"), MeleeKill.bHadSkillTags);

	// THE LEAP'S KILL.
	if (!TestNotNull(TEXT("set-up: the axe is buried in the host with Harrower's tags"),
			UCataclysmBuriedWeapon::BuryIn(Host, Leaping.Player, 10.0f * M, 100.0f, LeapKillHarrowerTags(false),
										   /*bInBurns=*/false)))
	{
		return false;
	}
	TestTrue(TEXT("set-up: the axe leaps from its host"), UCataclysmBuriedWeapon::LeapFromDying(Host));
	TestTrue(TEXT("set-up: and its leap killed the enemy 4 m east"), UCataclysmSkillEffects::IsDead(LeapDoomed));
	TestEqual(TEXT("set-up: one death notice named it"), Notices.CountFor(LeapDoomed), 1);
	const FLeapKillNoticeHeard LeapKill = Notices.For(LeapDoomed);
	TestTrue(TEXT("the leap's kill is ranged, from Harrower's Type.Ranged and Type.Projectile (N3)"), LeapKill.bIsRanged);
	TestFalse(TEXT("and not melee: Harrower's tags carry no Type.Melee (N3)"), LeapKill.bIsMelee);
	TestFalse(TEXT("set-up: and not by damage over time"), LeapKill.bByDamageOverTime);
	TestTrue(TEXT("the leap's kill carries skill tags though its blow names no skill (N1, N2)"), LeapKill.bHadSkillTags);
	TestTrue(TEXT("among them Type.Ranged, the throwing skill's"), LeapKill.SkillTags.HasTag(Ranged));
	TestTrue(TEXT("and Slot.Special, the throwing skill's slot"), LeapKill.SkillTags.HasTag(Slot));
	TestTrue(TEXT("and it names no skill, the leap carrying tags and no skill object"), LeapKill.SkillName.IsNone());
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
