// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDeployableSelfBlast.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
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
 * A player's death inside one of the player's own ability activations, issue #2472, STEP A: THE MEASUREMENT, WITH NO
 * FIX. "When you die, all your active traps and gadgets immediately trigger simultaneously" is two rows: its gadget half,
 * `gadgets_cast_now` on `player_death` (#15), and its trap half, `trigger_traps_nearby` on `player_death` (traps layer 5,
 * P083 row #2). Every cast either makes reaches `UCataclysmTriggeredSkill::Trigger`, which the engine refuses while the
 * player's ability list is locked (`ABILITYLIST_SCOPE_LOCK` in `UAbilitySystemComponent::InternalTryActivateAbility`):
 * the grant is only deferred, so `Trigger` takes it back and returns false. "Your deployables and traps deal full damage to
 * you if you are within their blast radius" (`deployable_blasts_strike_self`, #34) lets a skill gadget's area cast, which
 * is the player's own skill activated on the player's ability system, kill the player inside that activation.
 *
 * THE TWO TESTS WHOSE DEATH IS INSIDE THE ACTIVATION ASSERT WHAT THE SENTENCE PROMISES, SO THEY ARE EXPECTED TO FAIL UNTIL
 * STEP B FIXES #2472; their messages say so. The third is the control: the same players and rows, killed by an enemy's
 * blow outside any activation of theirs, which every one of its assertions expects to pass today.
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER ON THE PLAYERS' SIDE HOLDING A DEMONIC WHIP, given a weapon damage of 100 and
 * an enemy's defences (no armour, evasion, block, damage reduction, resistance or energy shield) by hand, never refreshed,
 * 50 health of 10,000 when it is killed. Its Ultimate, The Gathering, a 14 m Strike all round, is a skill gadget standing 3
 * m west of it with an enemy 3 m further west; its Heavy, Searing Lash, a 5 m cone, is a second skill gadget (the gadget
 * half) or two skill traps (the trap half), each 30 m off with an enemy 2.5 m or 3 m east of it, beyond the Gathering's 14
 * m. NO ROW IS AUTHORED, so every stat and both death rows are written by hand as #15's and traps layer 5's tests write
 * them. NO KEY IS PRESSED: every gadget and trap is placed directly, and the killing cast is the Ultimate gadget's own
 * step, a free start that waits for no attack clip, struck inside the activation `Trigger` begins.
 *
 * Every body stands at least 1 m from every other; the creatures are on the monsters' side and hold 50,000 health.
 * NOTHING HERE WAITS ON A TICK. The critical strike roll is pinned at 100, which never critically strikes, after the world
 * is made. Helper types and functions are named after the subject.
 */
namespace CataclysmDeathInActivationTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The health every creature here starts with: no cast here takes it all. */
	constexpr float DeathInActivationEnemyHealth = 50000.0f;

	/** The player's health when it is killed: far less than one blow of The Gathering, 385 on an enemy's defences. */
	constexpr float DeathInActivationLowHealth = 50.0f;

	/** How far apart two groups stand. */
	constexpr float DeathInActivationGroupsApart = 100.0f * M;

	/** A world for one test, destroyed when the test ends. */
	struct FDeathInActivationWorld
	{
		FDeathInActivationWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FDeathInActivationWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/** A creature of the monsters' side with no armour, evasion, block or resistance. It deals nothing of its own. */
	ACataclysmEnemyCharacter* DeathInActivationEnemy(UWorld* World, const FVector& Where)
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
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), DeathInActivationEnemyHealth);
		return Made;
	}

	float DeathInActivationHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** One row of a stat line a test's player holds: a flat figure with one required tag or none. */
	struct FDeathInActivationStat
	{
		FName Stat;
		float Value = 0.0f;
		const TCHAR* RequiredTag = nullptr;
	};

	/** A slot's flag: a flat 1 on `Stat` (`skill_slot_is_a_gadget` or `skill_slot_is_a_trap`) requiring `SlotTag`. */
	FDeathInActivationStat DeathInActivationConverts(const TCHAR* Stat, const TCHAR* SlotTag)
	{
		return {FName(Stat), 1.0f, SlotTag};
	}

	/** The self-blast flag, as #34's row states it: a flat 1 on `deployable_blasts_strike_self` with no tags. */
	FDeathInActivationStat DeathInActivationSelfBlastFlag()
	{
		return {FName(UCataclysmDeployableSelfBlast::FlagStat), 1.0f, nullptr};
	}

	/** The gadget half's row #1, as #15's tests write it: `gadgets_cast_now` 100 on `player_death`. */
	FCataclysmPoolAction DeathInActivationGadgetHalf()
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("player_death"));
		Action.Pool = FName(UCataclysmAbilitySystemComponent::GadgetsCastNowAction);
		Action.Percent = 100.0f;
		Action.bGadgetsCastNow = true;
		Action.TriggerKey = FName(TEXT("DeathInActivationTest:gadgets_cast_now:player_death"));
		return Action;
	}

	/**
	 * The trap half's row #2, as traps layer 5's tests write it: `trigger_traps_nearby` on `player_death`, a radius of
	 * nought (every standing trap), no count, no tag, no wait between firings.
	 */
	FCataclysmPoolAction DeathInActivationTrapHalf()
	{
		FCataclysmPoolAction Row;
		Row.Event = FName(TEXT("player_death"));
		Row.Pool = FName(UCataclysmAbilitySystemComponent::TriggerTrapsNearbyAction);
		Row.TriggerKey = FName(TEXT("DeathInActivationTest:player_death:trigger_traps_nearby"));
		Row.TriggerCooldownSeconds = 0.0f;
		Row.Percent = 0.0f;
		Row.bArmTrapsNearby = false;
		Row.bSetOffTrapsNearby = true;
		Row.bNearestTrapOnly = false;
		return Row;
	}

	/** A possessed player character on the players' side holding a Demonic Whip, its ability system and its slots. */
	struct FDeathInActivationRig
	{
		FDeathInActivationRig(UWorld* World, const FVector& Where)
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
			Slots->EquipWeaponType(TEXT("Whip"));
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			Set(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetDamageReductionAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetDamageReductionMoreAttribute(), 0.0f);
			Set(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 0.0f);
			Set(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);
		}

		/** Whether everything was made and the whip's Heavy and Ultimate are Strikes a gadget may cast. Reports why not. */
		bool IsUsable(FAutomationTestBase& Test) const
		{
			const FCataclysmWeaponSkill* Heavy = HeldRow(ECataclysmAbilitySlot::Heavy);
			const FCataclysmWeaponSkill* Ultimate = HeldRow(ECataclysmAbilitySlot::Ultimate);
			if (!Player || !System || !Slots || !Heavy || !Ultimate || Heavy->Shape != ECataclysmSkillShape::Strike
				|| Ultimate->Shape != ECataclysmSkillShape::Strike || !ACataclysmMinion::SkillGadgetMayCast(*Heavy)
				|| !ACataclysmMinion::SkillGadgetMayCast(*Ultimate))
			{
				Test.AddError(TEXT("set-up: a possessed player holding a Demonic Whip whose Heavy and Ultimate are Strikes a "
								   "gadget may cast. If a row is missing, DT_WeaponSkills may be older than "
								   "game/Data/WeaponSkills.csv: run tools/generate_datatable_assets.py"));
				return false;
			}
			return true;
		}

		/** The row the whip holds for `Slot`, or null. */
		const FCataclysmWeaponSkill* HeldRow(ECataclysmAbilitySlot Slot) const
		{
			return Slots ? Slots->GetAvailableSkills().FindByPredicate(
				[Slot](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == Slot; }) : nullptr;
		}

		/** Sets an attribute, when this ability system carries the set that holds it. */
		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			if (System->HasAttributeSetForAttribute(Attribute))
			{
				System->SetNumericAttributeBase(Attribute, Value);
			}
		}

		/** REPLACES THE WHOLE STAT LINE with these rows. */
		void Holds(const TArray<FDeathInActivationStat>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FDeathInActivationStat& Each : Rows)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
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

		/** Killed by `Killer`'s direct blow, larger than its health, outside any activation of the player's. */
		bool DiesTo(AActor* Killer) const
		{
			UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 1000000.0f);
			return UCataclysmSkillEffects::IsDead(Player);
		}

		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
	};

	/** A skill gadget for `Rig`'s `Slot` at `Where`, or null with the reason reported. */
	ACataclysmMinion* DeathInActivationPlaceGadget(FAutomationTestBase& Test, const FDeathInActivationRig& Rig,
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

	/** A skill trap for `Rig`'s Heavy slot at `Where`, unarmed, or null with the reason reported. */
	ACataclysmMinion* DeathInActivationPlaceTrap(FAutomationTestBase& Test, const FDeathInActivationRig& Rig,
												 const FVector& Where)
	{
		ACataclysmMinion* Made = ACataclysmMinion::PlaceSkillTrap(Rig.Player, ECataclysmAbilitySlot::Heavy, Where);
		if (!Made)
		{
			Test.AddError(TEXT("set-up: no skill trap was placed. DT_MinionTypes needs the SkillTrap row: run "
							   "tools/generate_datatable_assets.py"));
		}
		return Made;
	}

	/** Whether a trap is gone: destroyed, or on its way out. */
	bool DeathInActivationGone(const TWeakObjectPtr<ACataclysmMinion>& Trap)
	{
		return !Trap.IsValid() || Trap->IsActorBeingDestroyed();
	}

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FDeathInActivationEventCount
	{
		FDeathInActivationEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
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
		~FDeathInActivationEventCount()
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
	 * WHAT WAS ACTIVATING WHEN `player_death` WAS RAISED, read in the event's own announcement, which `ActOnEvent` makes
	 * first: whether any of the player's skills was active then, and whether one of them was a cast `Watched` made (its
	 * `FreeRepeatCastBy`). A cast of The Gathering ends inside its own activation, so a cast still active at the death is
	 * a death inside that activation, under the engine's ability-list lock. And `Read`'s health at that moment.
	 */
	struct FDeathInActivationDeathWatch
	{
		FDeathInActivationDeathWatch(UCataclysmAbilitySystemComponent* InSystem, const AActor* InWatched,
									 const AActor* InRead)
			: System(InSystem)
			, Watched(InWatched)
			, Read(InRead)
		{
			if (System)
			{
				Handle = System->OnActionEvent.AddLambda([this](FName Raised)
				{
					if (Raised != FName(TEXT("player_death")))
					{
						return;
					}
					++Count;
					for (const FGameplayAbilitySpec& Spec : System->GetActivatableAbilities())
					{
						if (!Spec.IsActive())
						{
							continue;
						}
						bAnySkillActive = true;
						const UCataclysmSkillTemplate* Running = Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());
						if (Running && Watched && Running->FreeRepeatCastBy.Get() == Watched)
						{
							bWatchedCastActive = true;
						}
					}
					ReadHealthAtTheDeath = DeathInActivationHealthOf(Read);
				});
			}
		}
		~FDeathInActivationDeathWatch()
		{
			if (System)
			{
				System->OnActionEvent.Remove(Handle);
			}
		}
		UCataclysmAbilitySystemComponent* System = nullptr;
		const AActor* Watched = nullptr;
		const AActor* Read = nullptr;
		int32 Count = 0;
		bool bAnySkillActive = false;
		bool bWatchedCastActive = false;
		float ReadHealthAtTheDeath = -1.0f;
		FDelegateHandle Handle;
	};
}

#define CATACLYSM_DEATH_IN_ACTIVATION_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 1. The gadget half: killed by its own gadget's area cast, every gadget should still make its death cast (#2472)
// ==========================================================================

CATACLYSM_DEATH_IN_ACTIVATION_TEST(FCataclysmDeathInActivationGadgetHalfTest,
	"Cataclysm.DeathInActivation.KilledByItsOwnGadgetsAreaCastEveryGadgetStillMakesItsDeathCast")
{
	using namespace CataclysmDeathInActivationTest;

	// ONE PLAYER HOLDING THE SELF-BLAST FLAG AND ROW #1: an Ultimate gadget 3 m west of it with an enemy 3 m west of the
	// gadget, and a Heavy gadget 30 m north of it with an enemy 3 m east of the gadget. The player at 50 health.
	FDeathInActivationWorld Scope;
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
	FDeathInActivationRig Wearer(World, FVector::ZeroVector);
	if (!Wearer.IsUsable(*this))
	{
		return false;
	}
	Wearer.Holds({DeathInActivationConverts(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat, TEXT("Slot.Heavy")),
				  DeathInActivationConverts(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat,
											TEXT("Slot.Ultimate")),
				  DeathInActivationSelfBlastFlag()});
	Wearer.System->SetPoolActions({DeathInActivationGadgetHalf()});

	const FVector UltimateAt(-3.0f * M, 0.0f, 0.0f);
	const FVector HeavyAt(0.0f, 30.0f * M, 0.0f);
	ACataclysmMinion* Ultimate = DeathInActivationPlaceGadget(*this, Wearer, ECataclysmAbilitySlot::Ultimate, UltimateAt);
	ACataclysmMinion* Heavy = DeathInActivationPlaceGadget(*this, Wearer, ECataclysmAbilitySlot::Heavy, HeavyAt);
	if (!Ultimate || !Heavy)
	{
		return false;
	}
	ACataclysmEnemyCharacter* UltimateEnemy = DeathInActivationEnemy(World, UltimateAt + FVector(-3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* HeavyEnemy = DeathInActivationEnemy(World, HeavyAt + FVector(3.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the Ultimate gadget's enemy"), UltimateEnemy)
		|| !TestNotNull(TEXT("set-up: the Heavy gadget's enemy"), HeavyEnemy))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: both gadgets are deployables of the player's, and both creatures are its enemies"),
			Ultimate->IsDeployable() && Ultimate->Summoner.Get() == Wearer.Player && Heavy->IsDeployable()
			&& Heavy->Summoner.Get() == Wearer.Player && UCataclysmTargeting::IsHostileTo(UltimateEnemy, Wearer.Player)
			&& UCataclysmTargeting::IsHostileTo(HeavyEnemy, Wearer.Player)))
	{
		return false;
	}
	Wearer.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), DeathInActivationLowHealth);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the player stands alive at %.2f health"),
								   DeathInActivationHealthOf(Wearer.Player)),
			!UCataclysmSkillEffects::IsDead(Wearer.Player)
			&& FMath::IsNearlyEqual(DeathInActivationHealthOf(Wearer.Player), DeathInActivationLowHealth, 0.01f)))
	{
		return false;
	}

	// THE KILLING CAST: THE ULTIMATE GADGET'S OWN STEP. The Gathering strikes its enemy, then the player 3 m away.
	FDeathInActivationDeathWatch Death(Wearer.System, Ultimate, UltimateEnemy);
	ACataclysmMinion::GadgetStep(Ultimate);
	TestEqual(TEXT("set-up: the Ultimate gadget's own clock cast The Gathering once"), Ultimate->SkillGadgetCastsMade, 1);
	TestTrue(*FString::Printf(TEXT("set-up: and it struck its enemy: %.2f left"), DeathInActivationHealthOf(UltimateEnemy)),
		DeathInActivationHealthOf(UltimateEnemy) < DeathInActivationEnemyHealth);
	if (!TestTrue(TEXT("set-up: the player died"), UCataclysmSkillEffects::IsDead(Wearer.Player)))
	{
		return false;
	}
	TestEqual(TEXT("set-up: by that cast's own blow: player_death was raised once, inside the gadget's step"), Death.Count,
		1);
	TestTrue(TEXT("set-up: and when it was raised, the gadget's cast of the player's skill was still activating, so the "
				  "death is inside that activation"), Death.bWatchedCastActive);

	// WHAT THE SENTENCE PROMISES: EACH GADGET MAKES ITS DEATH CAST.
	TestEqual(TEXT("the Heavy gadget made its death cast (expected to fail until #2472 is fixed)"),
		Heavy->SkillGadgetDeathCastsMade, 1);
	TestTrue(*FString::Printf(TEXT("and its death cast struck its enemy: %.2f of %.2f left (expected to fail until #2472 "
								   "is fixed)"), DeathInActivationHealthOf(HeavyEnemy), DeathInActivationEnemyHealth),
		DeathInActivationHealthOf(HeavyEnemy) < DeathInActivationEnemyHealth);
	TestEqual(TEXT("the Ultimate gadget, whose cast killed the player, made its death cast too (expected to fail until "
				   "#2472 is fixed)"), Ultimate->SkillGadgetDeathCastsMade, 1);
	TestTrue(*FString::Printf(TEXT("and its death cast struck its enemy after the killing blow: %.2f left, %.2f at the "
								   "death (expected to fail until #2472 is fixed)"),
							  DeathInActivationHealthOf(UltimateEnemy), Death.ReadHealthAtTheDeath),
		DeathInActivationHealthOf(UltimateEnemy) < Death.ReadHealthAtTheDeath);
	return true;
}

// ==========================================================================
// 2. The trap half: killed by its own gadget's area cast, every standing trap should still go off (#2472)
// ==========================================================================

CATACLYSM_DEATH_IN_ACTIVATION_TEST(FCataclysmDeathInActivationTrapHalfTest,
	"Cataclysm.DeathInActivation.KilledByItsOwnGadgetsAreaCastEveryStandingTrapStillGoesOff")
{
	using namespace CataclysmDeathInActivationTest;

	// ONE PLAYER HOLDING THE SELF-BLAST FLAG AND ROW #2: an Ultimate gadget 3 m west of it with an enemy 3 m west of the
	// gadget; two Heavy traps, unarmed, 30 m north and 30 m east of it, each with an enemy 2.5 m east of it. The player
	// at 50 health.
	FDeathInActivationWorld Scope;
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
	FDeathInActivationRig Wearer(World, FVector::ZeroVector);
	if (!Wearer.IsUsable(*this))
	{
		return false;
	}
	Wearer.Holds({DeathInActivationConverts(UCataclysmAbilitySystemComponent::SkillSlotIsATrapStat, TEXT("Slot.Heavy")),
				  DeathInActivationConverts(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat,
											TEXT("Slot.Ultimate")),
				  DeathInActivationSelfBlastFlag()});
	Wearer.System->SetPoolActions({DeathInActivationTrapHalf()});

	const FVector UltimateAt(-3.0f * M, 0.0f, 0.0f);
	const FVector NorthTrapAt(0.0f, 30.0f * M, 0.0f);
	const FVector EastTrapAt(30.0f * M, 0.0f, 0.0f);
	const FVector TrapsEnemyOffset(2.5f * M, 0.0f, 0.0f);
	ACataclysmMinion* Ultimate = DeathInActivationPlaceGadget(*this, Wearer, ECataclysmAbilitySlot::Ultimate, UltimateAt);
	ACataclysmMinion* NorthTrap = DeathInActivationPlaceTrap(*this, Wearer, NorthTrapAt);
	ACataclysmMinion* EastTrap = DeathInActivationPlaceTrap(*this, Wearer, EastTrapAt);
	if (!Ultimate || !NorthTrap || !EastTrap)
	{
		return false;
	}
	ACataclysmEnemyCharacter* UltimateEnemy = DeathInActivationEnemy(World, UltimateAt + FVector(-3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* NorthEnemy = DeathInActivationEnemy(World, NorthTrapAt + TrapsEnemyOffset);
	ACataclysmEnemyCharacter* EastEnemy = DeathInActivationEnemy(World, EastTrapAt + TrapsEnemyOffset);
	if (!TestNotNull(TEXT("set-up: the Ultimate gadget's enemy"), UltimateEnemy)
		|| !TestNotNull(TEXT("set-up: the north trap's enemy"), NorthEnemy)
		|| !TestNotNull(TEXT("set-up: the east trap's enemy"), EastEnemy))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the gadget is a deployable of the player's, and the three creatures are its enemies"),
			Ultimate->IsDeployable() && Ultimate->Summoner.Get() == Wearer.Player
			&& UCataclysmTargeting::IsHostileTo(UltimateEnemy, Wearer.Player)
			&& UCataclysmTargeting::IsHostileTo(NorthEnemy, Wearer.Player)
			&& UCataclysmTargeting::IsHostileTo(EastEnemy, Wearer.Player)))
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> NorthWatched(NorthTrap);
	const TWeakObjectPtr<ACataclysmMinion> EastWatched(EastTrap);
	if (!TestTrue(TEXT("set-up: both traps stand, and neither has gone off"),
			!DeathInActivationGone(NorthWatched) && !DeathInActivationGone(EastWatched)
			&& NorthTrap->SkillTrapTriggersMade == 0 && EastTrap->SkillTrapTriggersMade == 0))
	{
		return false;
	}
	Wearer.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), DeathInActivationLowHealth);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the player stands alive at %.2f health"),
								   DeathInActivationHealthOf(Wearer.Player)),
			!UCataclysmSkillEffects::IsDead(Wearer.Player)
			&& FMath::IsNearlyEqual(DeathInActivationHealthOf(Wearer.Player), DeathInActivationLowHealth, 0.01f)))
	{
		return false;
	}

	// THE KILLING CAST: THE ULTIMATE GADGET'S OWN STEP.
	FDeathInActivationDeathWatch Death(Wearer.System, Ultimate, UltimateEnemy);
	FDeathInActivationEventCount Triggers(Wearer.System, TEXT("trap_triggered"));
	ACataclysmMinion::GadgetStep(Ultimate);
	TestEqual(TEXT("set-up: the Ultimate gadget's own clock cast The Gathering once"), Ultimate->SkillGadgetCastsMade, 1);
	TestTrue(*FString::Printf(TEXT("set-up: and it struck its enemy: %.2f left"), DeathInActivationHealthOf(UltimateEnemy)),
		DeathInActivationHealthOf(UltimateEnemy) < DeathInActivationEnemyHealth);
	if (!TestTrue(TEXT("set-up: the player died"), UCataclysmSkillEffects::IsDead(Wearer.Player)))
	{
		return false;
	}
	TestEqual(TEXT("set-up: by that cast's own blow: player_death was raised once, inside the gadget's step"), Death.Count,
		1);
	TestTrue(TEXT("set-up: and when it was raised, the gadget's cast of the player's skill was still activating, so the "
				  "death is inside that activation"), Death.bWatchedCastActive);

	// WHAT THE SENTENCE PROMISES: EVERY STANDING TRAP GOES OFF.
	TestEqual(TEXT("two trap_triggered, one for each standing trap (expected to fail until #2472 is fixed)"),
		Triggers.Count, 2);
	TestTrue(TEXT("the north trap went off at the death and was used up (expected to fail until #2472 is fixed)"),
		DeathInActivationGone(NorthWatched));
	TestTrue(*FString::Printf(TEXT("and its cast struck its enemy: %.2f of %.2f left (expected to fail until #2472 is "
								   "fixed)"), DeathInActivationHealthOf(NorthEnemy), DeathInActivationEnemyHealth),
		DeathInActivationHealthOf(NorthEnemy) < DeathInActivationEnemyHealth);
	TestTrue(TEXT("the east trap went off at the death and was used up (expected to fail until #2472 is fixed)"),
		DeathInActivationGone(EastWatched));
	TestTrue(*FString::Printf(TEXT("and its cast struck its enemy: %.2f of %.2f left (expected to fail until #2472 is "
								   "fixed)"), DeathInActivationHealthOf(EastEnemy), DeathInActivationEnemyHealth),
		DeathInActivationHealthOf(EastEnemy) < DeathInActivationEnemyHealth);
	return true;
}

// ==========================================================================
// 3. The control: the same players and rows, killed by an enemy's blow outside any activation of theirs
// ==========================================================================

CATACLYSM_DEATH_IN_ACTIVATION_TEST(FCataclysmDeathInActivationControlTest,
	"Cataclysm.DeathInActivation.KilledByAnEnemysBlowOutsideAnyActivationEveryGadgetCastsAndEveryTrapGoesOff")
{
	using namespace CataclysmDeathInActivationTest;

	// TWO GROUPS 100 M APART, LAID OUT AS TESTS 1 AND 2: the gadget-half player and the trap-half player, each holding the
	// same flags and row, at 50 health, each with a killer 30 m south of it. NO GADGET IS STEPPED: each dies to its
	// killer's direct blow, which no skill of the player's casts.
	FDeathInActivationWorld Scope;
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
	const FVector TrapsHome(DeathInActivationGroupsApart, 0.0f, 0.0f);
	FDeathInActivationRig GadgetsPlayer(World, FVector::ZeroVector);
	FDeathInActivationRig TrapsPlayer(World, TrapsHome);
	if (!GadgetsPlayer.IsUsable(*this) || !TrapsPlayer.IsUsable(*this))
	{
		return false;
	}
	GadgetsPlayer.Holds(
		{DeathInActivationConverts(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat, TEXT("Slot.Heavy")),
		 DeathInActivationConverts(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat, TEXT("Slot.Ultimate")),
		 DeathInActivationSelfBlastFlag()});
	GadgetsPlayer.System->SetPoolActions({DeathInActivationGadgetHalf()});
	TrapsPlayer.Holds(
		{DeathInActivationConverts(UCataclysmAbilitySystemComponent::SkillSlotIsATrapStat, TEXT("Slot.Heavy")),
		 DeathInActivationConverts(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat, TEXT("Slot.Ultimate")),
		 DeathInActivationSelfBlastFlag()});
	TrapsPlayer.System->SetPoolActions({DeathInActivationTrapHalf()});

	const FVector UltimateAt(-3.0f * M, 0.0f, 0.0f);
	const FVector UltimatesEnemyAt(-6.0f * M, 0.0f, 0.0f);
	const FVector HeavyAt(0.0f, 30.0f * M, 0.0f);
	const FVector NorthTrapAt(0.0f, 30.0f * M, 0.0f);
	const FVector EastTrapAt(30.0f * M, 0.0f, 0.0f);
	const FVector TrapsEnemyOffset(2.5f * M, 0.0f, 0.0f);
	const FVector KillerAt(0.0f, -30.0f * M, 0.0f);
	ACataclysmMinion* Ultimate =
		DeathInActivationPlaceGadget(*this, GadgetsPlayer, ECataclysmAbilitySlot::Ultimate, UltimateAt);
	ACataclysmMinion* Heavy = DeathInActivationPlaceGadget(*this, GadgetsPlayer, ECataclysmAbilitySlot::Heavy, HeavyAt);
	ACataclysmMinion* TrapsUltimate =
		DeathInActivationPlaceGadget(*this, TrapsPlayer, ECataclysmAbilitySlot::Ultimate, TrapsHome + UltimateAt);
	ACataclysmMinion* NorthTrap = DeathInActivationPlaceTrap(*this, TrapsPlayer, TrapsHome + NorthTrapAt);
	ACataclysmMinion* EastTrap = DeathInActivationPlaceTrap(*this, TrapsPlayer, TrapsHome + EastTrapAt);
	if (!Ultimate || !Heavy || !TrapsUltimate || !NorthTrap || !EastTrap)
	{
		return false;
	}
	ACataclysmEnemyCharacter* UltimateEnemy = DeathInActivationEnemy(World, UltimatesEnemyAt);
	ACataclysmEnemyCharacter* HeavyEnemy = DeathInActivationEnemy(World, HeavyAt + FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* GadgetsKiller = DeathInActivationEnemy(World, KillerAt);
	ACataclysmEnemyCharacter* TrapsUltimateEnemy = DeathInActivationEnemy(World, TrapsHome + UltimatesEnemyAt);
	ACataclysmEnemyCharacter* NorthEnemy = DeathInActivationEnemy(World, TrapsHome + NorthTrapAt + TrapsEnemyOffset);
	ACataclysmEnemyCharacter* EastEnemy = DeathInActivationEnemy(World, TrapsHome + EastTrapAt + TrapsEnemyOffset);
	ACataclysmEnemyCharacter* TrapsKiller = DeathInActivationEnemy(World, TrapsHome + KillerAt);
	if (!TestNotNull(TEXT("set-up: the gadget-half player's Ultimate gadget's enemy"), UltimateEnemy)
		|| !TestNotNull(TEXT("set-up: the gadget-half player's Heavy gadget's enemy"), HeavyEnemy)
		|| !TestNotNull(TEXT("set-up: the gadget-half player's killer"), GadgetsKiller)
		|| !TestNotNull(TEXT("set-up: the trap-half player's Ultimate gadget's enemy"), TrapsUltimateEnemy)
		|| !TestNotNull(TEXT("set-up: the trap-half player's north trap's enemy"), NorthEnemy)
		|| !TestNotNull(TEXT("set-up: the trap-half player's east trap's enemy"), EastEnemy)
		|| !TestNotNull(TEXT("set-up: the trap-half player's killer"), TrapsKiller))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the gadget-half player's gadgets are its deployables, and its creatures its enemies"),
			Ultimate->IsDeployable() && Ultimate->Summoner.Get() == GadgetsPlayer.Player && Heavy->IsDeployable()
			&& Heavy->Summoner.Get() == GadgetsPlayer.Player
			&& UCataclysmTargeting::IsHostileTo(UltimateEnemy, GadgetsPlayer.Player)
			&& UCataclysmTargeting::IsHostileTo(HeavyEnemy, GadgetsPlayer.Player)
			&& UCataclysmTargeting::IsHostileTo(GadgetsKiller, GadgetsPlayer.Player))
		|| !TestTrue(TEXT("set-up: the trap-half player's gadget is its deployable, and its creatures its enemies"),
			TrapsUltimate->IsDeployable() && TrapsUltimate->Summoner.Get() == TrapsPlayer.Player
			&& UCataclysmTargeting::IsHostileTo(TrapsUltimateEnemy, TrapsPlayer.Player)
			&& UCataclysmTargeting::IsHostileTo(NorthEnemy, TrapsPlayer.Player)
			&& UCataclysmTargeting::IsHostileTo(EastEnemy, TrapsPlayer.Player)
			&& UCataclysmTargeting::IsHostileTo(TrapsKiller, TrapsPlayer.Player)))
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> NorthWatched(NorthTrap);
	const TWeakObjectPtr<ACataclysmMinion> EastWatched(EastTrap);
	if (!TestTrue(TEXT("set-up: both of the trap-half player's traps stand, and neither has gone off"),
			!DeathInActivationGone(NorthWatched) && !DeathInActivationGone(EastWatched)
			&& NorthTrap->SkillTrapTriggersMade == 0 && EastTrap->SkillTrapTriggersMade == 0))
	{
		return false;
	}
	GadgetsPlayer.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), DeathInActivationLowHealth);
	TrapsPlayer.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), DeathInActivationLowHealth);
	if (!TestTrue(TEXT("set-up: both players stand alive at 50 health, as in tests 1 and 2"),
			!UCataclysmSkillEffects::IsDead(GadgetsPlayer.Player) && !UCataclysmSkillEffects::IsDead(TrapsPlayer.Player)
			&& FMath::IsNearlyEqual(DeathInActivationHealthOf(GadgetsPlayer.Player), DeathInActivationLowHealth, 0.01f)
			&& FMath::IsNearlyEqual(DeathInActivationHealthOf(TrapsPlayer.Player), DeathInActivationLowHealth, 0.01f)))
	{
		return false;
	}

	// THE GADGET HALF, KILLED OUTSIDE ANY ACTIVATION.
	FDeathInActivationDeathWatch GadgetsDeath(GadgetsPlayer.System, Ultimate, UltimateEnemy);
	if (!TestTrue(TEXT("set-up: the gadget-half player died to its killer's blow"), GadgetsPlayer.DiesTo(GadgetsKiller)))
	{
		return false;
	}
	TestEqual(TEXT("set-up: its death raised player_death once"), GadgetsDeath.Count, 1);
	TestFalse(TEXT("set-up: and no skill of the player's was activating when it was raised"), GadgetsDeath.bAnySkillActive);
	TestEqual(TEXT("its Heavy gadget made its death cast"), Heavy->SkillGadgetDeathCastsMade, 1);
	TestTrue(*FString::Printf(TEXT("and its death cast struck its enemy: %.2f of %.2f left"),
							  DeathInActivationHealthOf(HeavyEnemy), DeathInActivationEnemyHealth),
		DeathInActivationHealthOf(HeavyEnemy) < DeathInActivationEnemyHealth);
	TestEqual(TEXT("its Ultimate gadget made its death cast"), Ultimate->SkillGadgetDeathCastsMade, 1);
	TestTrue(*FString::Printf(TEXT("and its death cast struck its enemy after the killing blow: %.2f left, %.2f at the "
								   "death"), DeathInActivationHealthOf(UltimateEnemy), GadgetsDeath.ReadHealthAtTheDeath),
		DeathInActivationHealthOf(UltimateEnemy) < GadgetsDeath.ReadHealthAtTheDeath);

	// THE TRAP HALF, KILLED OUTSIDE ANY ACTIVATION.
	FDeathInActivationDeathWatch TrapsDeath(TrapsPlayer.System, TrapsUltimate, TrapsUltimateEnemy);
	FDeathInActivationEventCount Triggers(TrapsPlayer.System, TEXT("trap_triggered"));
	if (!TestTrue(TEXT("set-up: the trap-half player died to its killer's blow"), TrapsPlayer.DiesTo(TrapsKiller)))
	{
		return false;
	}
	TestEqual(TEXT("set-up: its death raised player_death once"), TrapsDeath.Count, 1);
	TestFalse(TEXT("set-up: and no skill of the player's was activating when it was raised"), TrapsDeath.bAnySkillActive);
	TestEqual(TEXT("two trap_triggered, one for each standing trap"), Triggers.Count, 2);
	TestTrue(TEXT("the north trap went off at the death and was used up"), DeathInActivationGone(NorthWatched));
	TestTrue(*FString::Printf(TEXT("and its cast struck its enemy: %.2f of %.2f left"),
							  DeathInActivationHealthOf(NorthEnemy), DeathInActivationEnemyHealth),
		DeathInActivationHealthOf(NorthEnemy) < DeathInActivationEnemyHealth);
	TestTrue(TEXT("the east trap went off at the death and was used up"), DeathInActivationGone(EastWatched));
	TestTrue(*FString::Printf(TEXT("and its cast struck its enemy: %.2f of %.2f left"),
							  DeathInActivationHealthOf(EastEnemy), DeathInActivationEnemyHealth),
		DeathInActivationHealthOf(EastEnemy) < DeathInActivationEnemyHealth);
	return true;
}

#undef CATACLYSM_DEATH_IN_ACTIVATION_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
