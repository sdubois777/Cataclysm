// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
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
 * Skill traps, layer 4: five hooks on a trap's own blow. Ruled 2026-10-21 by the coordinating session (F1 to F12), for
 * "Triggered traps have a 25%-50% chance to reset and rearm instead of being consumed", "Traps deal 100%-200% increased
 * damage to the first enemy that triggers them", "Traps deal bonus damage equal to 5%-10% of the triggering enemy's
 * maximum HP", "Traps deal 30%-50% of their damage as a DoT over 3 seconds to triggered enemies" and "Enemies killed by
 * traps explode, dealing 50%-100% of the trap's damage to all enemies within 4 meters". In layer 1's group,
 * `Cataclysm.Traps.`, whose rig this follows, in a file of its own, numbered on from layer 3b's last (29).
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER HOLDING A DEMONIC WEAPON, given a weapon damage of 100, 10,000 health and
 * 1,000 mana by hand: a Whip, whose Heavy Searing Lash is a 5 m cone struck at once while the trap stands; or a Wand,
 * whose Heavy Malefice is a shot that lands after the trap is used up. NO ROW IS AUTHORED, SO EVERY STAT AND EVERY
 * ACTION IS WRITTEN BY HAND, as the item would build it from its row. NO KEY IS PRESSED: every trap is placed directly,
 * and its cast is a free start. Every body stands at least 1 m from every other, on a real side. NOTHING HERE WAITS ON A
 * TICK: the world's clock is moved by hand, a trap's step is called directly and shots are stepped by hand. Blows are
 * struck only with the critical strike roll pinned, after the world is made. Each sentence's row is written at a figure
 * inside or at the end of its range, or at 100 and nought for the rearm chance so no roll decides a test. Helper types
 * and functions are named after the subject, a trap's blow and its hooks.
 */
namespace CataclysmTrapBlowHookTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The health every creature here starts with, unless a case says otherwise. */
	constexpr float TrapBlowHookEnemyHealth = 50000.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FTrapBlowHookWorld
	{
		FTrapBlowHookWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FTrapBlowHookWorld()
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
	ACataclysmEnemyCharacter* TrapBlowHookEnemy(UWorld* World, const FVector& Where,
												float Health = TrapBlowHookEnemyHealth)
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

	float TrapBlowHookHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** What a creature has lost since it was made at `Health`. */
	float TrapBlowHookTakenFrom(const AActor* Who, float Health = TrapBlowHookEnemyHealth)
	{
		return Health - TrapBlowHookHealthOf(Who);
	}

	/** The tag of a generic damage over time, the one "as a DoT" lays. */
	FGameplayTag TrapBlowHookGenericDot()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("Keyword.DoT.Generic")), /*ErrorIfNotFound=*/false);
	}

	/** How many running effects grant `Granted` on `Who`. */
	int32 TrapBlowHookEffectsGranting(const AActor* Who, const FGameplayTag& Granted)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its && Granted.IsValid()
			? Its->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Granted))).Num()
			: -1;
	}

	/** What the one running effect granting `Granted` on `Who` has left to deal, or -1 for none or more than one. */
	float TrapBlowHookRemainingOn(const AActor* Who, const FGameplayTag& Granted)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		if (!Its || !Granted.IsValid())
		{
			return -1.0f;
		}
		const TArray<FActiveGameplayEffectHandle> Found =
			Its->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Granted)));
		return Found.Num() == 1 ? UCataclysmSkillEffects::RemainingDamageOverTime(Its, Found[0]) : -1.0f;
	}

	/** The longest time left on anything granting `Granted` on `Who`, or nought. */
	float TrapBlowHookSecondsLeftOn(const AActor* Who, const FGameplayTag& Granted)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		float Longest = 0.0f;
		if (Its && Granted.IsValid())
		{
			for (const float Seconds : Its->GetActiveEffectsTimeRemaining(
					 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Granted))))
			{
				Longest = FMath::Max(Longest, Seconds);
			}
		}
		return Longest;
	}

	/** One row of a stat line a test's player holds. */
	struct FTrapBlowHookStat
	{
		FName Stat;
		float Value = 0.0f;
		ECataclysmStatBucket Bucket = ECataclysmStatBucket::Flat;
		/** The row's one required tag, or null for none. */
		const TCHAR* RequiredTag = nullptr;
	};

	/** The Heavy's trap flag: a flat 1 on `skill_slot_is_a_trap` requiring `Slot.Heavy`, layer 1's row. */
	FTrapBlowHookStat TrapBlowHookHeavyFlag()
	{
		return {FName(UCataclysmAbilitySystemComponent::SkillSlotIsATrapStat), 1.0f, ECataclysmStatBucket::Flat,
				TEXT("Slot.Heavy")};
	}

	/** One of the four stats of this layer at `Value`, requiring `Type.Trap`, as its row is proposed. */
	FTrapBlowHookStat TrapBlowHookRow(const TCHAR* Stat, float Value, ECataclysmStatBucket Bucket = ECataclysmStatBucket::Flat)
	{
		return {FName(Stat), Value, Bucket, TEXT("Type.Trap")};
	}

	/** A possessed player character holding a Demonic weapon, its ability system and its weapon slots. */
	struct FTrapBlowHookRig
	{
		FTrapBlowHookRig(UWorld* World, const FVector& Where, const TCHAR* Weapon)
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

		/** The row this rig's weapon holds for its Heavy, or null. */
		const FCataclysmWeaponSkill* HeavyRow() const
		{
			return Slots ? Slots->GetAvailableSkills().FindByPredicate(
							   [](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == ECataclysmAbilitySlot::Heavy; })
						 : nullptr;
		}

		/**
		 * Whether everything was made and the weapon holds a Heavy row a trap may cast, of the shape `Shape`. Reports
		 * why not.
		 */
		bool IsUsable(FAutomationTestBase& Test, ECataclysmSkillShape Shape) const
		{
			const FCataclysmWeaponSkill* Heavy = HeavyRow();
			if (!Player || !System || !Slots || !Heavy || !ACataclysmMinion::SkillGadgetMayCast(*Heavy)
				|| Heavy->Shape != Shape)
			{
				Test.AddError(TEXT("set-up: a possessed player holding a Demonic weapon with a Heavy row a trap may cast, "
								   "of the expected shape. If a row is missing, DT_WeaponSkills may be older than "
								   "game/Data/WeaponSkills.csv: run tools/generate_datatable_assets.py"));
				return false;
			}
			return true;
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			System->SetNumericAttributeBase(Attribute, Value);
		}

		/** REPLACES THE WHOLE STAT LINE with these rows. */
		void Holds(const TArray<FTrapBlowHookStat>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FTrapBlowHookStat& Each : Rows)
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

		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
	};

	/** A skill trap for this rig's Heavy slot at `Where`, or null with the reason reported. */
	ACataclysmMinion* TrapBlowHookPlaceTrap(FAutomationTestBase& Test, const FTrapBlowHookRig& Rig, const FVector& Where)
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
	void TrapBlowHookPass(UWorld* World, float Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/** Run a trap's quarter-second step, as `RegenerationStep` calls it. */
	void TrapBlowHookStep(ACataclysmMinion* Trap)
	{
		if (IsValid(Trap))
		{
			ACataclysmMinion::GadgetStep(Trap);
		}
	}

	/** Whether a trap is gone: destroyed, or on its way out. */
	bool TrapBlowHookGone(const TWeakObjectPtr<ACataclysmMinion>& Trap)
	{
		return !Trap.IsValid() || Trap->IsActorBeingDestroyed();
	}

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FTrapBlowHookEventCount
	{
		FTrapBlowHookEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
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
		~FTrapBlowHookEventCount()
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
	 * `Done`, as layer 3b's test lands its shots: a test world never ticks. How many finished in this call.
	 */
	int32 TrapBlowHookLandEveryShot(UWorld* World, TSet<const ACataclysmProjectile*>& Done)
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

	/**
	 * "Enemies killed by traps explode, dealing 50%-100% of the trap's damage to all enemies within 4 meters" at 50, as
	 * the item builds the row: `explode_victim_for_killing_blow` on `kill`, requiring `Type.Trap`.
	 */
	FCataclysmPoolAction TrapBlowHookExplosionRow()
	{
		FCataclysmPoolAction Explodes;
		Explodes.Event = FName(TEXT("kill"));
		Explodes.Pool = FName(UCataclysmAbilitySystemComponent::ExplodeVictimForKillingBlowAction);
		Explodes.RequiredTags.AddTag(
			FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false));
		Explodes.TriggerKey = FName(TEXT("TrapBlowHookTest:kill:explode"));
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
// 30. "Triggered traps have a 25%-50% chance to reset and rearm instead of being consumed" (F1 to F3)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapBlowHookRearmTest,
	"Cataclysm.Traps.TheRearmRowAtAHundredKeepsATriggeredTrapStandingToArmAgainAndTriggerTwiceAndAtNoughtItIsUsedUp")
{
	using namespace CataclysmTrapBlowHookTest;

	// TWO PLAYERS 100 M APART, EACH HOLDING A DEMONIC WHIP AND THE HEAVY FLAG: a Heavy trap 20 m north with the enemy
	// that sets it off 2.5 m east of it, which stays within the 3 m trigger radius. The control holds the rearm chance at
	// nought (no row); the other at 100, so no roll decides the test.
	FTrapBlowHookWorld Scope;
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
	const FVector RearmingHome(100.0f * M, 0.0f, 0.0f);
	FTrapBlowHookRig Plain(World, FVector::ZeroVector, TEXT("Whip"));
	FTrapBlowHookRig Rearming(World, RearmingHome, TEXT("Whip"));
	if (!Plain.IsUsable(*this, ECataclysmSkillShape::Strike) || !Rearming.IsUsable(*this, ECataclysmSkillShape::Strike))
	{
		return false;
	}
	Plain.Holds({TrapBlowHookHeavyFlag()});
	Rearming.Holds({TrapBlowHookHeavyFlag(), TrapBlowHookRow(ACataclysmMinion::TrapRearmChanceStat, 100.0f)});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector Near(2.5f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* PlainSetter = TrapBlowHookEnemy(World, North + Near);
	ACataclysmEnemyCharacter* RearmingSetter = TrapBlowHookEnemy(World, RearmingHome + North + Near);
	if (!TestNotNull(TEXT("set-up: the control's enemy"), PlainSetter)
		|| !TestNotNull(TEXT("set-up: the rearming player's enemy"), RearmingSetter))
	{
		return false;
	}
	ACataclysmMinion* PlainTrap = TrapBlowHookPlaceTrap(*this, Plain, North);
	ACataclysmMinion* RearmingTrap = TrapBlowHookPlaceTrap(*this, Rearming, RearmingHome + North);
	if (!PlainTrap || !RearmingTrap)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> PlainWatched(PlainTrap);
	const TWeakObjectPtr<ACataclysmMinion> RearmingWatched(RearmingTrap);
	FTrapBlowHookEventCount PlainTriggers(Plain.System, TEXT("trap_triggered"));
	FTrapBlowHookEventCount RearmingTriggers(Rearming.System, TEXT("trap_triggered"));

	// THE CONTROL, READ FIRST: AT NOUGHT A TRIGGERED TRAP IS USED UP.
	TrapBlowHookPass(World, 1.25f);
	TrapBlowHookStep(PlainTrap);
	TestEqual(TEXT("control: the control's trap triggered once"), PlainTriggers.Count, 1);
	TestTrue(TEXT("control: and at a chance of nought it is used up"), TrapBlowHookGone(PlainWatched));
	TestEqual(TEXT("control: and kept among its player's used-up traps"), Plain.System->SpentSkillTraps.Num(), 1);

	// AT 100: IT TRIGGERS, AND STAYS, UNARMED.
	TrapBlowHookStep(RearmingTrap);
	TestEqual(TEXT("at 100 the trap triggered, raising trap_triggered"), RearmingTriggers.Count, 1);
	if (!TestFalse(TEXT("and it is not used up: it stands"), TrapBlowHookGone(RearmingWatched)))
	{
		return false;
	}
	TestFalse(TEXT("it is disarmed, to arm again"), RearmingTrap->bSkillTrapArmed);
	TestTrue(TEXT("and drawn dim as an arming trap is"),
		RearmingTrap->SkillTrapTintShown.Equals(ACataclysmMinion::SkillTrapTintFor(/*bArmed=*/false)));
	TestEqual(TEXT("it is not kept among the used-up traps (F3)"), Rearming.System->SpentSkillTraps.Num(), 0);
	TestEqual(TEXT("and it still counts against the cap: one trap stands for its player"),
		ACataclysmMinion::SkillTrapsOf(Rearming.Player).Num(), 1);
	TestTrue(*FString::Printf(TEXT("it arms again after the ordinary arming time: at the age %.3f, now %.3f"),
							  RearmingTrap->SkillTrapArmsAtAgeSeconds, RearmingTrap->GetGameTimeSinceCreation()),
		RearmingTrap->SkillTrapArmsAtAgeSeconds > RearmingTrap->GetGameTimeSinceCreation()
			+ ACataclysmMinion::SkillTrapArmSeconds - 0.01f);

	// NOT TWICE ON ONE STEP, AND NOT BEFORE IT HAS ARMED.
	TrapBlowHookStep(RearmingTrap);
	TestEqual(TEXT("a second step at the same moment does not trigger it again"), RearmingTriggers.Count, 1);
	TrapBlowHookPass(World, 0.5f);
	TrapBlowHookStep(RearmingTrap);
	TestEqual(TEXT("nor half a second later, still arming"), RearmingTriggers.Count, 1);

	// ARMED AGAIN, IT TRIGGERS A SECOND TIME, AND AT 100 STANDS AGAIN.
	TrapBlowHookPass(World, 0.75f);
	TrapBlowHookStep(RearmingTrap);
	TestEqual(TEXT("armed again a second after the rearm, it triggers a second time, raising trap_triggered again"),
		RearmingTriggers.Count, 2);
	TestEqual(TEXT("it has triggered twice"), RearmingTrap->SkillTrapTriggersMade, 2);
	TestFalse(TEXT("and stands again"), TrapBlowHookGone(RearmingWatched));
	TestTrue(*FString::Printf(TEXT("set-up: its enemy was struck by both casts: %.2f"),
							  TrapBlowHookTakenFrom(RearmingSetter)),
		TrapBlowHookTakenFrom(RearmingSetter) > TrapBlowHookTakenFrom(PlainSetter) + 1.0f);
	return true;
}

// ==========================================================================
// 31. "Traps deal 100%-200% increased damage to the first enemy that triggers them" (F4, T13)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapBlowHookFirstEnemyTest,
	"Cataclysm.Traps.TheFirstEnemyRowRaisesEveryBlowOfATriggerOnTheEnemyThatSetItOffAndNoOtherEnemyALateShotIncluded")
{
	using namespace CataclysmTrapBlowHookTest;

	// FOUR PLAYERS 100 M APART, EACH WITH THE HEAVY FLAG AND A HEAVY TRAP 20 M NORTH, ITS ENEMY 2.5 M EAST OF IT. Two hold
	// a Demonic Whip, whose Searing Lash strikes the enemy that set the trap off and a second enemy 4 m east of the trap,
	// in the same cone, while the trap stands; two hold a Demonic Wand, whose Malefice shot lands after the trap is used
	// up. In each pair the control holds the flag alone, the other also the row at 100 increased, so "100%" doubles a
	// blow priced with no other increase.
	FTrapBlowHookWorld Scope;
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
	const FVector LashRowHome(100.0f * M, 0.0f, 0.0f);
	const FVector ShotPlainHome(200.0f * M, 0.0f, 0.0f);
	const FVector ShotRowHome(300.0f * M, 0.0f, 0.0f);
	FTrapBlowHookRig LashPlain(World, FVector::ZeroVector, TEXT("Whip"));
	FTrapBlowHookRig LashRow(World, LashRowHome, TEXT("Whip"));
	FTrapBlowHookRig ShotPlain(World, ShotPlainHome, TEXT("Wand"));
	FTrapBlowHookRig ShotRow(World, ShotRowHome, TEXT("Wand"));
	if (!LashPlain.IsUsable(*this, ECataclysmSkillShape::Strike) || !LashRow.IsUsable(*this, ECataclysmSkillShape::Strike)
		|| !ShotPlain.IsUsable(*this, ECataclysmSkillShape::Projectile)
		|| !ShotRow.IsUsable(*this, ECataclysmSkillShape::Projectile))
	{
		return false;
	}
	const FTrapBlowHookStat FirstEnemy = TrapBlowHookRow(ACataclysmMinion::TrapDamageToTriggeringEnemyStat, 100.0f,
														 ECataclysmStatBucket::Increased);
	LashPlain.Holds({TrapBlowHookHeavyFlag()});
	LashRow.Holds({TrapBlowHookHeavyFlag(), FirstEnemy});
	ShotPlain.Holds({TrapBlowHookHeavyFlag()});
	ShotRow.Holds({TrapBlowHookHeavyFlag(), FirstEnemy});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector Near(2.5f * M, 0.0f, 0.0f);
	const FVector Further(4.0f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* LashPlainSetter = TrapBlowHookEnemy(World, North + Near);
	ACataclysmEnemyCharacter* LashPlainOther = TrapBlowHookEnemy(World, North + Further);
	ACataclysmEnemyCharacter* LashRowSetter = TrapBlowHookEnemy(World, LashRowHome + North + Near);
	ACataclysmEnemyCharacter* LashRowOther = TrapBlowHookEnemy(World, LashRowHome + North + Further);
	ACataclysmEnemyCharacter* ShotPlainSetter = TrapBlowHookEnemy(World, ShotPlainHome + North + Near);
	ACataclysmEnemyCharacter* ShotRowSetter = TrapBlowHookEnemy(World, ShotRowHome + North + Near);
	if (!TestNotNull(TEXT("set-up: the lash control's enemy"), LashPlainSetter)
		|| !TestNotNull(TEXT("set-up: the lash control's second enemy"), LashPlainOther)
		|| !TestNotNull(TEXT("set-up: the lash row's enemy"), LashRowSetter)
		|| !TestNotNull(TEXT("set-up: the lash row's second enemy"), LashRowOther)
		|| !TestNotNull(TEXT("set-up: the shot control's enemy"), ShotPlainSetter)
		|| !TestNotNull(TEXT("set-up: the shot row's enemy"), ShotRowSetter))
	{
		return false;
	}
	ACataclysmMinion* LashPlainTrap = TrapBlowHookPlaceTrap(*this, LashPlain, North);
	ACataclysmMinion* LashRowTrap = TrapBlowHookPlaceTrap(*this, LashRow, LashRowHome + North);
	ACataclysmMinion* ShotPlainTrap = TrapBlowHookPlaceTrap(*this, ShotPlain, ShotPlainHome + North);
	ACataclysmMinion* ShotRowTrap = TrapBlowHookPlaceTrap(*this, ShotRow, ShotRowHome + North);
	if (!LashPlainTrap || !LashRowTrap || !ShotPlainTrap || !ShotRowTrap)
	{
		return false;
	}

	// THE LASH, WHILE THE TRAP STANDS.
	TrapBlowHookPass(World, 1.25f);
	TrapBlowHookStep(LashPlainTrap);
	TrapBlowHookStep(LashRowTrap);
	const float PlainSetterTook = TrapBlowHookTakenFrom(LashPlainSetter);
	const float PlainOtherTook = TrapBlowHookTakenFrom(LashPlainOther);
	TestTrue(*FString::Printf(TEXT("control: the control's lash struck the enemy that set it off: %.2f"), PlainSetterTook),
		PlainSetterTook > 0.0f);
	TestTrue(*FString::Printf(TEXT("control: and the second enemy in its cone: %.2f"), PlainOtherTook),
		PlainOtherTook > 0.0f);
	TestEqual(*FString::Printf(TEXT("the enemy that set the trap off takes the row's 100%% increased: %.2f against %.2f"),
							   TrapBlowHookTakenFrom(LashRowSetter), PlainSetterTook),
		TrapBlowHookTakenFrom(LashRowSetter), PlainSetterTook * 2.0f, PlainSetterTook * 0.001f);
	TestEqual(*FString::Printf(TEXT("the second enemy the same cast strikes does not: %.2f against %.2f"),
							   TrapBlowHookTakenFrom(LashRowOther), PlainOtherTook),
		TrapBlowHookTakenFrom(LashRowOther), PlainOtherTook, PlainOtherTook * 0.001f);

	// THE SHOT, LANDING AFTER THE TRAP IS USED UP (layer 3b): THE USED-UP TRAP KEEPS ITS TRIGGERING ENEMY.
	const TWeakObjectPtr<ACataclysmMinion> ShotRowWatched(ShotRowTrap);
	TrapBlowHookStep(ShotPlainTrap);
	TrapBlowHookStep(ShotRowTrap);
	TestTrue(TEXT("set-up: the shot's trap is used up as its cast starts"), TrapBlowHookGone(ShotRowWatched));
	TestEqual(TEXT("set-up: and its enemy is untouched: the shot is still in flight"),
		TrapBlowHookTakenFrom(ShotRowSetter), 0.0f, 0.001f);
	TSet<const ACataclysmProjectile*> Done;
	TestTrue(TEXT("set-up: both shots landed"), TrapBlowHookLandEveryShot(World, Done) >= 2);
	const float PlainShotTook = TrapBlowHookTakenFrom(ShotPlainSetter);
	TestTrue(*FString::Printf(TEXT("control: the control's late shot struck its enemy: %.2f"), PlainShotTook),
		PlainShotTook > 0.0f);
	TestEqual(*FString::Printf(TEXT("a late shot on the enemy that set the trap off takes the increase too: %.2f against "
									"%.2f"), TrapBlowHookTakenFrom(ShotRowSetter), PlainShotTook),
		TrapBlowHookTakenFrom(ShotRowSetter), PlainShotTook * 2.0f, PlainShotTook * 0.001f);
	return true;
}

// ==========================================================================
// 32. "Traps deal bonus damage equal to 5%-10% of the triggering enemy's maximum HP" (F5 to F7)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapBlowHookHealthShareTest,
	"Cataclysm.Traps.TheHealthShareRowAddsItsShareOfTheTriggeringEnemysMaximumHealthToItsFirstBlowOncePerTrigger")
{
	using namespace CataclysmTrapBlowHookTest;

	// FOUR PLAYERS 100 M APART, AS IN TEST 31, the row now "bonus damage equal to 5%-10% of the triggering enemy's
	// maximum HP" at 10: a tenth of each 50,000 creature's maximum health, 5,000, added flat to the first blow of the
	// trigger's cast on the enemy that set it off. After the shots land, ONE MORE BLOW OF EACH USED-UP TRAP'S CAST on
	// its enemy, struck as the use's own `HitTargets` would strike one, by `ApplyHit` with the trap's identity on the
	// delivery: the second blow of the same trigger.
	FTrapBlowHookWorld Scope;
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
	constexpr float ShareOfAMaximum = 10.0f;
	const float Bonus = TrapBlowHookEnemyHealth * ShareOfAMaximum / 100.0f;
	const FVector LashRowHome(100.0f * M, 0.0f, 0.0f);
	const FVector ShotPlainHome(200.0f * M, 0.0f, 0.0f);
	const FVector ShotRowHome(300.0f * M, 0.0f, 0.0f);
	FTrapBlowHookRig LashPlain(World, FVector::ZeroVector, TEXT("Whip"));
	FTrapBlowHookRig LashRow(World, LashRowHome, TEXT("Whip"));
	FTrapBlowHookRig ShotPlain(World, ShotPlainHome, TEXT("Wand"));
	FTrapBlowHookRig ShotRow(World, ShotRowHome, TEXT("Wand"));
	if (!LashPlain.IsUsable(*this, ECataclysmSkillShape::Strike) || !LashRow.IsUsable(*this, ECataclysmSkillShape::Strike)
		|| !ShotPlain.IsUsable(*this, ECataclysmSkillShape::Projectile)
		|| !ShotRow.IsUsable(*this, ECataclysmSkillShape::Projectile))
	{
		return false;
	}
	const FTrapBlowHookStat HealthShare =
		TrapBlowHookRow(ACataclysmMinion::TrapTriggeringEnemyMaxHealthDamageStat, ShareOfAMaximum);
	LashPlain.Holds({TrapBlowHookHeavyFlag()});
	LashRow.Holds({TrapBlowHookHeavyFlag(), HealthShare});
	ShotPlain.Holds({TrapBlowHookHeavyFlag()});
	ShotRow.Holds({TrapBlowHookHeavyFlag(), HealthShare});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector Near(2.5f * M, 0.0f, 0.0f);
	const FVector Further(4.0f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* LashPlainSetter = TrapBlowHookEnemy(World, North + Near);
	ACataclysmEnemyCharacter* LashPlainOther = TrapBlowHookEnemy(World, North + Further);
	ACataclysmEnemyCharacter* LashRowSetter = TrapBlowHookEnemy(World, LashRowHome + North + Near);
	ACataclysmEnemyCharacter* LashRowOther = TrapBlowHookEnemy(World, LashRowHome + North + Further);
	ACataclysmEnemyCharacter* ShotPlainSetter = TrapBlowHookEnemy(World, ShotPlainHome + North + Near);
	ACataclysmEnemyCharacter* ShotRowSetter = TrapBlowHookEnemy(World, ShotRowHome + North + Near);
	if (!TestNotNull(TEXT("set-up: the lash control's enemy"), LashPlainSetter)
		|| !TestNotNull(TEXT("set-up: the lash control's second enemy"), LashPlainOther)
		|| !TestNotNull(TEXT("set-up: the lash row's enemy"), LashRowSetter)
		|| !TestNotNull(TEXT("set-up: the lash row's second enemy"), LashRowOther)
		|| !TestNotNull(TEXT("set-up: the shot control's enemy"), ShotPlainSetter)
		|| !TestNotNull(TEXT("set-up: the shot row's enemy"), ShotRowSetter))
	{
		return false;
	}
	ACataclysmMinion* LashPlainTrap = TrapBlowHookPlaceTrap(*this, LashPlain, North);
	ACataclysmMinion* LashRowTrap = TrapBlowHookPlaceTrap(*this, LashRow, LashRowHome + North);
	ACataclysmMinion* ShotPlainTrap = TrapBlowHookPlaceTrap(*this, ShotPlain, ShotPlainHome + North);
	ACataclysmMinion* ShotRowTrap = TrapBlowHookPlaceTrap(*this, ShotRow, ShotRowHome + North);
	if (!LashPlainTrap || !LashRowTrap || !ShotPlainTrap || !ShotRowTrap)
	{
		return false;
	}
	const TWeakObjectPtr<AActor> ShotPlainUsedUp(ShotPlainTrap);
	const TWeakObjectPtr<AActor> ShotRowUsedUp(ShotRowTrap);

	// THE LASH, WHILE THE TRAP STANDS.
	TrapBlowHookPass(World, 1.25f);
	TrapBlowHookStep(LashPlainTrap);
	TrapBlowHookStep(LashRowTrap);
	const float PlainSetterTook = TrapBlowHookTakenFrom(LashPlainSetter);
	const float PlainOtherTook = TrapBlowHookTakenFrom(LashPlainOther);
	TestTrue(*FString::Printf(TEXT("control: the control's lash struck the enemy that set it off: %.2f"), PlainSetterTook),
		PlainSetterTook > 0.0f);
	TestEqual(*FString::Printf(TEXT("the enemy that set the trap off takes a tenth of its 50,000 maximum on top: %.2f "
									"against %.2f"), TrapBlowHookTakenFrom(LashRowSetter), PlainSetterTook),
		TrapBlowHookTakenFrom(LashRowSetter), PlainSetterTook + Bonus, 0.5f);
	TestEqual(*FString::Printf(TEXT("the second enemy the same cast strikes takes none: %.2f against %.2f"),
							   TrapBlowHookTakenFrom(LashRowOther), PlainOtherTook),
		TrapBlowHookTakenFrom(LashRowOther), PlainOtherTook, 0.5f);

	// THE SHOT, LANDING AFTER THE TRAP IS USED UP: THE TRIGGER'S FIRST BLOW ON ITS ENEMY, SO IT TAKES THE SHARE.
	TrapBlowHookStep(ShotPlainTrap);
	TrapBlowHookStep(ShotRowTrap);
	TestEqual(TEXT("set-up: the shot's enemy is untouched while the shot flies"), TrapBlowHookTakenFrom(ShotRowSetter),
		0.0f, 0.001f);
	TSet<const ACataclysmProjectile*> Done;
	TestTrue(TEXT("set-up: both shots landed"), TrapBlowHookLandEveryShot(World, Done) >= 2);
	const float PlainShotTook = TrapBlowHookTakenFrom(ShotPlainSetter);
	const float RowShotTook = TrapBlowHookTakenFrom(ShotRowSetter);
	TestTrue(*FString::Printf(TEXT("control: the control's late shot struck its enemy: %.2f"), PlainShotTook),
		PlainShotTook > 0.0f);
	TestEqual(*FString::Printf(TEXT("a late shot, the trigger's first blow on its enemy, takes the share too: %.2f against "
									"%.2f"), RowShotTook, PlainShotTook),
		RowShotTook, PlainShotTook + Bonus, 0.5f);

	// AND A SECOND BLOW OF THE SAME TRIGGER ON THE SAME ENEMY TAKES IT NO MORE (F6): each used-up trap's cast strikes
	// its enemy once more, at 100% of the weapon's damage.
	FCataclysmHitDelivery PlainLater;
	PlainLater.CastBy = ShotPlainUsedUp;
	FCataclysmHitDelivery RowLater;
	RowLater.CastBy = ShotRowUsedUp;
	UCataclysmSkillEffects::ApplyHit(ShotPlain.Player, ShotPlainSetter, 100.0f, FGameplayTagContainer(), PlainLater);
	UCataclysmSkillEffects::ApplyHit(ShotRow.Player, ShotRowSetter, 100.0f, FGameplayTagContainer(), RowLater);
	const float PlainLaterTook = TrapBlowHookTakenFrom(ShotPlainSetter) - PlainShotTook;
	const float RowLaterTook = TrapBlowHookTakenFrom(ShotRowSetter) - RowShotTook;
	TestTrue(*FString::Printf(TEXT("control: the control's later blow struck: %.2f"), PlainLaterTook),
		PlainLaterTook > 0.0f);
	TestEqual(*FString::Printf(TEXT("the second blow of the same trigger adds no second share: %.2f against %.2f"),
							   RowLaterTook, PlainLaterTook),
		RowLaterTook, PlainLaterTook, 0.5f);
	return true;
}

// ==========================================================================
// 33. "Traps deal 30%-50% of their damage as a DoT over 3 seconds to triggered enemies" (F8, F9)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapBlowHookDamageOverTimeTest,
	"Cataclysm.Traps.TheDamageOverTimeRowLaysItsShareOverThreeSecondsOnTheEnemyThatSetTheTrapOffWhenTheBlowTookATenth")
{
	using namespace CataclysmTrapBlowHookTest;

	// FOUR PLAYERS 100 M APART, EACH HOLDING A DEMONIC WHIP AND THE HEAVY FLAG, a Heavy trap 20 m north with the enemy
	// that sets it off 2.5 m east of it. The control holds the flag alone; the other three also the row at 50. THE
	// CONTROL'S AND THE ROW'S: a second enemy 4 m east of the trap, in the same cone; their creatures 1,500 health, so the
	// lash takes a tenth of each. THE STURDY ONE'S: its enemy 100,000 health, so the lash takes less than a tenth. THE
	// REFRESHED ONE'S: a second trap 2.5 m east of its enemy, so two casts strike the one enemy.
	FTrapBlowHookWorld Scope;
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
	const FGameplayTag Generic = TrapBlowHookGenericDot();
	if (!TestTrue(TEXT("set-up: the Keyword.DoT.Generic tag exists"), Generic.IsValid()))
	{
		return false;
	}
	constexpr float Frail = 1500.0f;
	constexpr float Sturdy = 100000.0f;
	const FVector RowHome(100.0f * M, 0.0f, 0.0f);
	const FVector SturdyHome(200.0f * M, 0.0f, 0.0f);
	const FVector RefreshedHome(300.0f * M, 0.0f, 0.0f);
	FTrapBlowHookRig Plain(World, FVector::ZeroVector, TEXT("Whip"));
	FTrapBlowHookRig Row(World, RowHome, TEXT("Whip"));
	FTrapBlowHookRig SturdyRow(World, SturdyHome, TEXT("Whip"));
	FTrapBlowHookRig Refreshed(World, RefreshedHome, TEXT("Whip"));
	if (!Plain.IsUsable(*this, ECataclysmSkillShape::Strike) || !Row.IsUsable(*this, ECataclysmSkillShape::Strike)
		|| !SturdyRow.IsUsable(*this, ECataclysmSkillShape::Strike)
		|| !Refreshed.IsUsable(*this, ECataclysmSkillShape::Strike))
	{
		return false;
	}
	const FTrapBlowHookStat OverTime = TrapBlowHookRow(ACataclysmMinion::TrapDamageOverTimeShareStat, 50.0f);
	Plain.Holds({TrapBlowHookHeavyFlag()});
	Row.Holds({TrapBlowHookHeavyFlag(), OverTime});
	SturdyRow.Holds({TrapBlowHookHeavyFlag(), OverTime});
	Refreshed.Holds({TrapBlowHookHeavyFlag(), OverTime});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector Near(2.5f * M, 0.0f, 0.0f);
	const FVector Further(4.0f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* PlainSetter = TrapBlowHookEnemy(World, North + Near, Frail);
	ACataclysmEnemyCharacter* PlainOther = TrapBlowHookEnemy(World, North + Further, Frail);
	ACataclysmEnemyCharacter* RowSetter = TrapBlowHookEnemy(World, RowHome + North + Near, Frail);
	ACataclysmEnemyCharacter* RowOther = TrapBlowHookEnemy(World, RowHome + North + Further, Frail);
	ACataclysmEnemyCharacter* SturdySetter = TrapBlowHookEnemy(World, SturdyHome + North + Near, Sturdy);
	ACataclysmEnemyCharacter* RefreshedSetter = TrapBlowHookEnemy(World, RefreshedHome + North + Near, Frail);
	if (!TestNotNull(TEXT("set-up: the control's enemy"), PlainSetter)
		|| !TestNotNull(TEXT("set-up: the control's second enemy"), PlainOther)
		|| !TestNotNull(TEXT("set-up: the row's enemy"), RowSetter)
		|| !TestNotNull(TEXT("set-up: the row's second enemy"), RowOther)
		|| !TestNotNull(TEXT("set-up: the sturdy enemy"), SturdySetter)
		|| !TestNotNull(TEXT("set-up: the refreshed player's enemy"), RefreshedSetter))
	{
		return false;
	}
	ACataclysmMinion* PlainTrap = TrapBlowHookPlaceTrap(*this, Plain, North);
	ACataclysmMinion* RowTrap = TrapBlowHookPlaceTrap(*this, Row, RowHome + North);
	ACataclysmMinion* SturdyTrap = TrapBlowHookPlaceTrap(*this, SturdyRow, SturdyHome + North);
	ACataclysmMinion* RefreshedFirst = TrapBlowHookPlaceTrap(*this, Refreshed, RefreshedHome + North);
	ACataclysmMinion* RefreshedSecond =
		TrapBlowHookPlaceTrap(*this, Refreshed, RefreshedHome + North + Near + Near);
	if (!PlainTrap || !RowTrap || !SturdyTrap || !RefreshedFirst || !RefreshedSecond)
	{
		return false;
	}

	TrapBlowHookPass(World, 1.25f);
	TrapBlowHookStep(PlainTrap);
	TrapBlowHookStep(RowTrap);
	TrapBlowHookStep(SturdyTrap);
	TestTrue(*FString::Printf(TEXT("set-up: the row's lash took a tenth or more of its enemy's 1,500: %.2f"),
							  TrapBlowHookTakenFrom(RowSetter, Frail)),
		TrapBlowHookTakenFrom(RowSetter, Frail) >= Frail / 10.0f);

	// THE CONTROL, READ FIRST: THE LASH LAYS NO GENERIC DAMAGE OVER TIME OF ITS OWN.
	TestEqual(TEXT("control: the control's lash lays no generic damage over time on the enemy that set it off"),
		TrapBlowHookEffectsGranting(PlainSetter, Generic), 0);
	TestEqual(TEXT("control: nor on its second enemy"), TrapBlowHookEffectsGranting(PlainOther, Generic), 0);

	// THE ROW: ON THE ENEMY THAT SET IT OFF, AN EXTRA HALF OF WHAT THE BLOW TOOK, OVER 3 S.
	const float RowBlowTook = TrapBlowHookTakenFrom(RowSetter, Frail);
	TestEqual(TEXT("the row lays one generic damage over time on the enemy that set the trap off"),
		TrapBlowHookEffectsGranting(RowSetter, Generic), 1);
	TestEqual(*FString::Printf(TEXT("worth the row's half of what the blow took from health: %.2f against %.2f"),
							   TrapBlowHookRemainingOn(RowSetter, Generic), RowBlowTook * 0.5f),
		TrapBlowHookRemainingOn(RowSetter, Generic), RowBlowTook * 0.5f, RowBlowTook * 0.001f);
	TestEqual(TEXT("over the sentence's 3 seconds"), TrapBlowHookSecondsLeftOn(RowSetter, Generic), 3.0f, 0.01f);
	TestEqual(*FString::Printf(TEXT("on top of the blow, which is unchanged: %.2f against the control's %.2f"),
							   RowBlowTook, TrapBlowHookTakenFrom(PlainSetter, Frail)),
		RowBlowTook, TrapBlowHookTakenFrom(PlainSetter, Frail), RowBlowTook * 0.001f);
	TestTrue(*FString::Printf(TEXT("set-up: the second enemy was struck for a tenth or more by the same cast: %.2f"),
							  TrapBlowHookTakenFrom(RowOther, Frail)),
		TrapBlowHookTakenFrom(RowOther, Frail) >= Frail / 10.0f);
	TestEqual(TEXT("but only the enemy that set the trap off is given it, not the second"),
		TrapBlowHookEffectsGranting(RowOther, Generic), 0);

	// THE RULE OF A TENTH: A BLOW TAKING LESS LAYS NOTHING.
	TestTrue(*FString::Printf(TEXT("set-up: the sturdy enemy was struck for less than a tenth of its 100,000: %.2f"),
							  TrapBlowHookTakenFrom(SturdySetter, Sturdy)),
		TrapBlowHookTakenFrom(SturdySetter, Sturdy) > 0.0f && TrapBlowHookTakenFrom(SturdySetter, Sturdy) < Sturdy / 10.0f);
	TestEqual(TEXT("so it is given no damage over time: the owner's rule of a tenth"),
		TrapBlowHookEffectsGranting(SturdySetter, Generic), 0);

	// REFRESHED, NOT STACKED: TWO CASTS ON THE ONE ENEMY.
	TrapBlowHookStep(RefreshedFirst);
	TestEqual(TEXT("set-up: the first cast laid one"), TrapBlowHookEffectsGranting(RefreshedSetter, Generic), 1);
	TrapBlowHookStep(RefreshedSecond);
	TestTrue(*FString::Printf(TEXT("set-up: the second trap's cast struck the same enemy again: %.2f"),
							  TrapBlowHookTakenFrom(RefreshedSetter, Frail)),
		TrapBlowHookTakenFrom(RefreshedSetter, Frail) > RowBlowTook * 1.5f);
	TestEqual(TEXT("a second cast's blow on it within the 3 s lays over the first and does not stack"),
		TrapBlowHookEffectsGranting(RefreshedSetter, Generic), 1);
	return true;
}

// ==========================================================================
// 34. "Enemies killed by traps explode, dealing 50%-100% of the trap's damage to all enemies within 4 meters" (F10-F12)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapBlowHookKillExplosionTest,
	"Cataclysm.Traps.AnEnemyKilledByATrapsBlowExplodesForItsShareWithinFourMetresAndNoKillOfAnotherKindExplodes")
{
	using namespace CataclysmTrapBlowHookTest;

	// SEVEN PLAYERS 100 M APART, EACH WITH THE HEAVY FLAG; a Heavy trap 20 m north of each with its enemy 2.5 m east of
	// it. THE TWO MEASURES hold the flag alone and a 50,000 enemy, so the whole of each blow is read from what it took: a
	// Whip's lash and a Wand's shot. EVERY OTHER PLAYER'S DOOMED ENEMY HOLDS 50 HEALTH, which one blow kills. Bystanders
	// of 50,000 stand east of the trap beyond the lash's 5 m: one 6 m out, 3.5 m from the dead, inside the 4 m; one 7.5 m
	// out, 5 m from it, outside. THE CONTROL (a Whip) holds no explosion row; THE ROW (a Whip) holds it at 50; THE
	// CHAIN (a Whip, the row) has a bystander of 10 health 6 m out, which the explosion kills, and a second 9 m out, 3 m
	// from it and 6.5 m from the first dead; THE OWN BLOW (the row) kills its doomed enemy with a blow of its own, no
	// trap's; THE LATE SHOT (a Wand, the row) kills with a shot landing after the trap is used up, its bystander 3 m
	// north of the doomed, off the shot's line.
	FTrapBlowHookWorld Scope;
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
	constexpr float Brittle = 10.0f;
	const FVector Home[7] = {FVector::ZeroVector, FVector(100.0f * M, 0.0f, 0.0f), FVector(200.0f * M, 0.0f, 0.0f),
							 FVector(300.0f * M, 0.0f, 0.0f), FVector(400.0f * M, 0.0f, 0.0f),
							 FVector(500.0f * M, 0.0f, 0.0f), FVector(600.0f * M, 0.0f, 0.0f)};
	FTrapBlowHookRig LashMeasure(World, Home[0], TEXT("Whip"));
	FTrapBlowHookRig ShotMeasure(World, Home[1], TEXT("Wand"));
	FTrapBlowHookRig Plain(World, Home[2], TEXT("Whip"));
	FTrapBlowHookRig Row(World, Home[3], TEXT("Whip"));
	FTrapBlowHookRig Chain(World, Home[4], TEXT("Whip"));
	FTrapBlowHookRig OwnBlow(World, Home[5], TEXT("Whip"));
	FTrapBlowHookRig LateShot(World, Home[6], TEXT("Wand"));
	if (!LashMeasure.IsUsable(*this, ECataclysmSkillShape::Strike)
		|| !ShotMeasure.IsUsable(*this, ECataclysmSkillShape::Projectile)
		|| !Plain.IsUsable(*this, ECataclysmSkillShape::Strike) || !Row.IsUsable(*this, ECataclysmSkillShape::Strike)
		|| !Chain.IsUsable(*this, ECataclysmSkillShape::Strike) || !OwnBlow.IsUsable(*this, ECataclysmSkillShape::Strike)
		|| !LateShot.IsUsable(*this, ECataclysmSkillShape::Projectile))
	{
		return false;
	}
	for (const FTrapBlowHookRig* Each : {&LashMeasure, &ShotMeasure, &Plain, &Row, &Chain, &OwnBlow, &LateShot})
	{
		Each->Holds({TrapBlowHookHeavyFlag()});
	}
	for (const FTrapBlowHookRig* Each : {&Row, &Chain, &OwnBlow, &LateShot})
	{
		Each->System->SetPoolActions({TrapBlowHookExplosionRow()});
	}

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector Near(2.5f * M, 0.0f, 0.0f);
	const FVector Inside(6.0f * M, 0.0f, 0.0f);
	const FVector Outside(7.5f * M, 0.0f, 0.0f);
	const FVector BeyondTheBrittle(9.0f * M, 0.0f, 0.0f);
	const FVector OffTheLine(2.5f * M, 3.0f * M, 0.0f);
	ACataclysmEnemyCharacter* LashMeasured = TrapBlowHookEnemy(World, Home[0] + North + Near);
	ACataclysmEnemyCharacter* ShotMeasured = TrapBlowHookEnemy(World, Home[1] + North + Near);
	ACataclysmEnemyCharacter* PlainDoomed = TrapBlowHookEnemy(World, Home[2] + North + Near, Doomed);
	ACataclysmEnemyCharacter* PlainInside = TrapBlowHookEnemy(World, Home[2] + North + Inside);
	ACataclysmEnemyCharacter* RowDoomed = TrapBlowHookEnemy(World, Home[3] + North + Near, Doomed);
	ACataclysmEnemyCharacter* RowInside = TrapBlowHookEnemy(World, Home[3] + North + Inside);
	ACataclysmEnemyCharacter* RowOutside = TrapBlowHookEnemy(World, Home[3] + North + Outside);
	ACataclysmEnemyCharacter* ChainDoomed = TrapBlowHookEnemy(World, Home[4] + North + Near, Doomed);
	ACataclysmEnemyCharacter* ChainBrittle = TrapBlowHookEnemy(World, Home[4] + North + Inside, Brittle);
	ACataclysmEnemyCharacter* ChainBeyond = TrapBlowHookEnemy(World, Home[4] + North + BeyondTheBrittle);
	ACataclysmEnemyCharacter* OwnDoomed = TrapBlowHookEnemy(World, Home[5] + North + Near, Doomed);
	ACataclysmEnemyCharacter* OwnInside = TrapBlowHookEnemy(World, Home[5] + North + Inside);
	ACataclysmEnemyCharacter* LateDoomed = TrapBlowHookEnemy(World, Home[6] + North + Near, Doomed);
	ACataclysmEnemyCharacter* LateInside = TrapBlowHookEnemy(World, Home[6] + North + OffTheLine);
	const TArray<ACataclysmEnemyCharacter*> Made = {LashMeasured, ShotMeasured, PlainDoomed, PlainInside, RowDoomed,
		RowInside, RowOutside, ChainDoomed, ChainBrittle, ChainBeyond, OwnDoomed, OwnInside, LateDoomed, LateInside};
	for (int32 Index = 0; Index < Made.Num(); ++Index)
	{
		if (!TestNotNull(*FString::Printf(TEXT("set-up: creature %d"), Index), Made[Index]))
		{
			return false;
		}
	}
	ACataclysmMinion* LashMeasureTrap = TrapBlowHookPlaceTrap(*this, LashMeasure, Home[0] + North);
	ACataclysmMinion* ShotMeasureTrap = TrapBlowHookPlaceTrap(*this, ShotMeasure, Home[1] + North);
	ACataclysmMinion* PlainTrap = TrapBlowHookPlaceTrap(*this, Plain, Home[2] + North);
	ACataclysmMinion* RowTrap = TrapBlowHookPlaceTrap(*this, Row, Home[3] + North);
	ACataclysmMinion* ChainTrap = TrapBlowHookPlaceTrap(*this, Chain, Home[4] + North);
	ACataclysmMinion* LateTrap = TrapBlowHookPlaceTrap(*this, LateShot, Home[6] + North);
	if (!LashMeasureTrap || !ShotMeasureTrap || !PlainTrap || !RowTrap || !ChainTrap || !LateTrap)
	{
		return false;
	}

	// THE LASHES, WHILE THE TRAPS STAND.
	TrapBlowHookPass(World, 1.25f);
	TrapBlowHookStep(LashMeasureTrap);
	TrapBlowHookStep(PlainTrap);
	TrapBlowHookStep(RowTrap);
	TrapBlowHookStep(ChainTrap);
	const float Lash = TrapBlowHookTakenFrom(LashMeasured);
	TestTrue(*FString::Printf(TEXT("set-up: the lash's whole blow, read off the measure, kills 50 health: %.2f"), Lash),
		Lash > Doomed * 2.0f);
	TestTrue(TEXT("set-up: the control's lash killed its doomed enemy"), UCataclysmSkillEffects::IsDead(PlainDoomed));
	TestEqual(TEXT("control: with no row its bystander 3.5 m from the dead is untouched"),
		TrapBlowHookTakenFrom(PlainInside), 0.0f, 0.001f);
	TestTrue(TEXT("set-up: the row's lash killed its doomed enemy"), UCataclysmSkillEffects::IsDead(RowDoomed));
	TestEqual(*FString::Printf(TEXT("the dead enemy explodes: its bystander 3.5 m away takes half the killing blow's own "
									"figure: %.2f against %.2f"), TrapBlowHookTakenFrom(RowInside), Lash * 0.5f),
		TrapBlowHookTakenFrom(RowInside), Lash * 0.5f, Lash * 0.001f);
	TestEqual(TEXT("and one 5 m away, beyond the 4 m, takes nothing"), TrapBlowHookTakenFrom(RowOutside), 0.0f, 0.001f);

	// A KILL BY THE EXPLOSION DOES NOT EXPLODE AGAIN (F11).
	TestTrue(TEXT("set-up: the chain's lash killed its doomed enemy"), UCataclysmSkillEffects::IsDead(ChainDoomed));
	TestTrue(TEXT("set-up: and the explosion killed the brittle bystander 3.5 m from it"),
		UCataclysmSkillEffects::IsDead(ChainBrittle));
	TestEqual(TEXT("whose own death explodes nothing: the bystander 3 m from it and 6.5 m from the first is untouched"),
		TrapBlowHookTakenFrom(ChainBeyond), 0.0f, 0.001f);

	// A KILL BY THE PLAYER'S OWN BLOW, NO TRAP'S, DOES NOT EXPLODE (F12).
	UCataclysmSkillEffects::ApplyHit(OwnBlow.Player, OwnDoomed, 100.0f, FGameplayTagContainer(), FCataclysmHitDelivery());
	TestTrue(TEXT("set-up: the player's own blow killed its doomed enemy"), UCataclysmSkillEffects::IsDead(OwnDoomed));
	TestEqual(TEXT("and no explosion followed: its bystander 3.5 m away is untouched"), TrapBlowHookTakenFrom(OwnInside),
		0.0f, 0.001f);

	// A LATE SHOT'S KILL, THE TRAP USED UP, EXPLODES (F12).
	const TWeakObjectPtr<ACataclysmMinion> LateWatched(LateTrap);
	TrapBlowHookStep(ShotMeasureTrap);
	TrapBlowHookStep(LateTrap);
	TestTrue(TEXT("set-up: the late shot's trap is used up as its cast starts"), TrapBlowHookGone(LateWatched));
	TSet<const ACataclysmProjectile*> Done;
	TestTrue(TEXT("set-up: both shots landed"), TrapBlowHookLandEveryShot(World, Done) >= 2);
	const float Shot = TrapBlowHookTakenFrom(ShotMeasured);
	TestTrue(*FString::Printf(TEXT("set-up: the shot's whole blow, read off the measure, kills 50 health: %.2f"), Shot),
		Shot > Doomed * 2.0f);
	TestTrue(TEXT("set-up: the late shot killed its doomed enemy"), UCataclysmSkillEffects::IsDead(LateDoomed));
	TestEqual(*FString::Printf(TEXT("a late shot's kill explodes too: its bystander 3 m away takes half the shot's figure: "
									"%.2f against %.2f"), TrapBlowHookTakenFrom(LateInside), Shot * 0.5f),
		TrapBlowHookTakenFrom(LateInside), Shot * 0.5f, Shot * 0.001f);
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
