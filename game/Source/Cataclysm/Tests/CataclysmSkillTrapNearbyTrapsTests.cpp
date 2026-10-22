// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDebuffs.h"
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
 * Skill traps, layer 5: two actions on the player's OTHER traps. Ruled 2026-10-22 by the coordinating session (H1 to
 * H11) and, on H5, by the owner ("Chain the group"), for "When a trap is triggered, the nearest unarmed trap within 10
 * meters arms instantly", "When a trap triggers it releases a shockwave that arms all traps within 15 meters instantly",
 * "When a gadget is destroyed it arms all traps within 12 meters instantly", "Traps trigger simultaneously when any one
 * of them is triggered, if within 8-12 meters of each other", "When a gadget is destroyed, the nearest active trap within
 * 10 meters is triggered immediately" and the trap half of "When you die, all your active traps and gadgets immediately
 * trigger simultaneously". In layer 1's group, `Cataclysm.Traps.`, in a file of its own, numbered on from layer 4's last
 * (35).
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER HOLDING A DEMONIC WHIP, given a weapon damage of 100, 10,000 health and
 * 1,000 mana by hand, whose Heavy Searing Lash is a 5 m cone struck at once from the trap. NO ROW IS AUTHORED, SO EVERY
 * STAT AND EVERY ACTION IS WRITTEN BY HAND, as the item would build it from its row. NO KEY IS PRESSED: every trap and
 * gadget is placed directly, and every cast is a free start. Every body stands at least 1 m from every other, on a real
 * side; no trap stands within 5 m of an enemy other than its own, so each lash strikes only the enemy it is meant to.
 * NOTHING HERE WAITS ON A TICK: the world's clock is moved by hand and a trap's step is called directly; a trap is ARMED
 * only by a step, so a trap that must be armed is stepped once before any enemy stands near it. Blows are struck only
 * with the critical strike roll pinned, after the world is made. Helper types and functions are named after the subject,
 * the player's nearby traps.
 */
namespace CataclysmNearbyTrapsTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The health every creature here starts with, unless a case says otherwise: a lash takes more than a tenth of it. */
	constexpr float NearbyTrapsEnemyHealth = 1500.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FNearbyTrapsWorld
	{
		FNearbyTrapsWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FNearbyTrapsWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/** A creature of the monsters' side with no armour, evasion, block or resistance. It deals nothing of its own. */
	ACataclysmEnemyCharacter* NearbyTrapsEnemy(UWorld* World, const FVector& Where, float Health = NearbyTrapsEnemyHealth)
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
	float NearbyTrapsTakenFrom(const AActor* Who, float Health = NearbyTrapsEnemyHealth)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Health - Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** Whether `Who` carries `Tag` now. */
	bool NearbyTrapsCarries(const AActor* Who, const FGameplayTag& Tag)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its && Tag.IsValid() && Its->HasMatchingGameplayTag(Tag);
	}

	/** One row of a stat line a test's player holds. */
	struct FNearbyTrapsStat
	{
		FName Stat;
		float Value = 0.0f;
		/** The row's one required tag. */
		const TCHAR* RequiredTag = nullptr;
	};

	/** The Heavy's trap flag: a flat 1 on `skill_slot_is_a_trap` requiring `Slot.Heavy`, layer 1's row. */
	FNearbyTrapsStat NearbyTrapsHeavyFlag()
	{
		return {FName(UCataclysmAbilitySystemComponent::SkillSlotIsATrapStat), 1.0f, TEXT("Slot.Heavy")};
	}

	/** The Ultimate's gadget flag: a flat 1 on `skill_slot_is_a_gadget` requiring `Slot.Ultimate`, the gadget's row. */
	FNearbyTrapsStat NearbyTrapsUltimateGadgetFlag()
	{
		return {FName(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat), 1.0f, TEXT("Slot.Ultimate")};
	}

	/** "Triggered traps have a 25%-50% chance to reset and rearm instead of being consumed" at 100: every trap rearms. */
	FNearbyTrapsStat NearbyTrapsRearmAlways()
	{
		return {FName(ACataclysmMinion::TrapRearmChanceStat), 100.0f, TEXT("Type.Trap")};
	}

	/** A possessed player character holding a Demonic Whip, its ability system and its weapon slots. */
	struct FNearbyTrapsRig
	{
		FNearbyTrapsRig(UWorld* World, const FVector& Where)
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

		/** Whether everything was made and the whip holds a Heavy Strike a trap may cast. Reports why not. */
		bool IsUsable(FAutomationTestBase& Test) const
		{
			const FCataclysmWeaponSkill* Heavy = Slots ? Slots->GetAvailableSkills().FindByPredicate(
				[](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == ECataclysmAbilitySlot::Heavy; }) : nullptr;
			if (!Player || !System || !Slots || !Heavy || !ACataclysmMinion::SkillGadgetMayCast(*Heavy)
				|| Heavy->Shape != ECataclysmSkillShape::Strike)
			{
				Test.AddError(TEXT("set-up: a possessed player holding a Demonic Whip with a Heavy Strike a trap may cast. "
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

		/** REPLACES THE WHOLE STAT LINE with these rows, each a flat figure. */
		void Holds(const TArray<FNearbyTrapsStat>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FNearbyTrapsStat& Each : Rows)
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

		/** Killed by `Killer`'s direct blow, larger than its health, as a blow kills in play. Whether it died. */
		bool DiesTo(AActor* Killer) const
		{
			UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 1000000.0f);
			return UCataclysmSkillEffects::IsDead(Player);
		}

		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
	};

	/** A skill trap for this rig's Heavy slot at `Where`, or null with the reason reported. */
	ACataclysmMinion* NearbyTrapsPlace(FAutomationTestBase& Test, const FNearbyTrapsRig& Rig, const FVector& Where)
	{
		ACataclysmMinion* Made = ACataclysmMinion::PlaceSkillTrap(Rig.Player, ECataclysmAbilitySlot::Heavy, Where);
		if (!Made)
		{
			Test.AddError(TEXT("set-up: no skill trap was placed. DT_MinionTypes needs the SkillTrap row: run "
							   "tools/generate_datatable_assets.py"));
		}
		return Made;
	}

	/** A skill gadget for this rig's Ultimate slot at `Where`, or null with the reason reported. */
	ACataclysmMinion* NearbyTrapsPlaceGadget(FAutomationTestBase& Test, const FNearbyTrapsRig& Rig, const FVector& Where)
	{
		ACataclysmMinion* Made = ACataclysmMinion::PlaceSkillGadget(Rig.Player, ECataclysmAbilitySlot::Ultimate, Where);
		if (!Made)
		{
			Test.AddError(TEXT("set-up: no skill gadget was placed. DT_MinionTypes needs the SkillGadget row: run "
							   "tools/generate_datatable_assets.py"));
		}
		return Made;
	}

	/** Move the world's clock forward by hand. No timer fires and nobody thinks. */
	void NearbyTrapsPass(UWorld* World, float Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/** Run a trap's quarter-second step, as `RegenerationStep` calls it: it arms a trap whose time has come. */
	void NearbyTrapsStep(ACataclysmMinion* Trap)
	{
		if (IsValid(Trap))
		{
			ACataclysmMinion::GadgetStep(Trap);
		}
	}

	/** Whether a trap is gone: destroyed, or on its way out. */
	bool NearbyTrapsGone(const TWeakObjectPtr<ACataclysmMinion>& Trap)
	{
		return !Trap.IsValid() || Trap->IsActorBeingDestroyed();
	}

	/** Whether a trap stands armed and drawn armed. */
	bool NearbyTrapsArmed(const ACataclysmMinion* Trap)
	{
		return Trap && Trap->bSkillTrapArmed
			&& Trap->SkillTrapTintShown.Equals(ACataclysmMinion::SkillTrapTintFor(/*bArmed=*/true));
	}

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FNearbyTrapsEventCount
	{
		FNearbyTrapsEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
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
		~FNearbyTrapsEventCount()
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
	 * One of this layer's two actions on `Event`, as the item builds it from its row: `bArms` for `arm_traps_nearby`,
	 * else `trigger_traps_nearby`; `Metres` the rolled value; `bNearestOnly` a Scale Max Steps of 1; `RequiredTag`
	 * the row's, or null for none. No wait between firings (H9).
	 */
	FCataclysmPoolAction NearbyTrapsRow(const TCHAR* Event, bool bArms, float Metres, bool bNearestOnly,
										const TCHAR* RequiredTag)
	{
		FCataclysmPoolAction Row;
		Row.Event = FName(Event);
		Row.Pool = FName(bArms ? UCataclysmAbilitySystemComponent::ArmTrapsNearbyAction
							   : UCataclysmAbilitySystemComponent::TriggerTrapsNearbyAction);
		if (RequiredTag)
		{
			Row.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(RequiredTag), /*ErrorIfNotFound=*/false));
		}
		Row.TriggerKey = FName(*FString::Printf(TEXT("NearbyTrapsTest:%s:%s"), Event, *Row.Pool.ToString()));
		Row.TriggerCooldownSeconds = 0.0f;
		Row.Percent = Metres;
		Row.bArmTrapsNearby = bArms;
		Row.bSetOffTrapsNearby = !bArms;
		Row.bNearestTrapOnly = bNearestOnly;
		return Row;
	}

	/**
	 * "Traps apply a 20%-35% slow to triggered enemies for 2-4 seconds" at 25 for 3 s, as layer 3's tests write it:
	 * `slow_for_seconds` on `trap_triggered`, requiring `Type.Trap`.
	 */
	FCataclysmPoolAction NearbyTrapsSlowRow()
	{
		FCataclysmPoolAction Slowing;
		Slowing.Event = FName(TEXT("trap_triggered"));
		Slowing.Pool = FName(UCataclysmAbilitySystemComponent::SlowForSecondsAction);
		Slowing.RequiredTags.AddTag(
			FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false));
		Slowing.TriggerKey = FName(TEXT("NearbyTrapsTest:trap_triggered:slow"));
		Slowing.TriggerCooldownSeconds = 0.0f;
		Slowing.bSlowForSeconds = true;
		Slowing.Percent = 25.0f;
		Slowing.StackSeconds = 3.0f;
		return Slowing;
	}

	/**
	 * The grant of "Each trap triggered in the last 10 seconds increases your damage by 8%-15%", as the item builds it:
	 * one own stack on `trap_triggered` under `Key`, each lasting 10 s, with no cap (layer 3, M4).
	 */
	FCataclysmPoolAction NearbyTrapsStackGrant(FName Key)
	{
		FCataclysmPoolAction Grant;
		Grant.Event = FName(TEXT("trap_triggered"));
		Grant.StackKey = Key;
		Grant.StackSeconds = 10.0f;
		Grant.StackCap = 0;
		Grant.bOwnStacksLapseAlone = true;
		Grant.TriggerKey = FName(*FString::Printf(TEXT("%s:trap_triggered"), *Key.ToString()));
		return Grant;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 36. "When a trap is triggered, the nearest unarmed trap within 10 meters arms instantly" (H2, H3)
// ==========================================================================

CATACLYSM_TEST(FCataclysmNearbyTrapsArmTheNearestTest,
	"Cataclysm.Traps.WhenATrapTriggersTheNearestUnarmedTrapWithinTenMetresArmsAndNoFartherOneAndNoArmedOneIsTouched")
{
	using namespace CataclysmNearbyTrapsTest;

	// TWO PLAYERS 100 M APART, EACH WITH THE HEAVY FLAG AND FOUR TRAPS ON A LINE NORTH: the first 20 m north with its enemy
	// 2.5 m east, which sets it off; one 4 m beyond it, armed; one 6 m short of it and one 9 m beyond it, both unarmed.
	// The armed one is nearer than either unarmed one. The control holds no row; the other holds the arm row at 10 m,
	// the nearest one alone.
	FNearbyTrapsWorld Scope;
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
	FNearbyTrapsRig Plain(World, FVector::ZeroVector);
	FNearbyTrapsRig Arming(World, RowHome);
	if (!Plain.IsUsable(*this) || !Arming.IsUsable(*this))
	{
		return false;
	}
	Plain.Holds({NearbyTrapsHeavyFlag()});
	Arming.Holds({NearbyTrapsHeavyFlag()});
	Arming.System->SetPoolActions({NearbyTrapsRow(TEXT("trap_triggered"), /*bArms=*/true, 10.0f,
												  /*bNearestOnly=*/true, TEXT("Type.Trap"))});

	const FVector First(0.0f, 20.0f * M, 0.0f);
	const FVector ArmedAt(0.0f, 24.0f * M, 0.0f);
	const FVector NearAt(0.0f, 14.0f * M, 0.0f);
	const FVector FarAt(0.0f, 29.0f * M, 0.0f);
	ACataclysmMinion* PlainFirst = NearbyTrapsPlace(*this, Plain, First);
	ACataclysmMinion* PlainArmed = NearbyTrapsPlace(*this, Plain, ArmedAt);
	ACataclysmMinion* RowFirst = NearbyTrapsPlace(*this, Arming, RowHome + First);
	ACataclysmMinion* RowArmed = NearbyTrapsPlace(*this, Arming, RowHome + ArmedAt);
	if (!PlainFirst || !PlainArmed || !RowFirst || !RowArmed)
	{
		return false;
	}
	// ARMED BY A STEP BEFORE ANY ENEMY STANDS NEAR: it arms and finds nobody.
	NearbyTrapsPass(World, 1.25f);
	NearbyTrapsStep(PlainArmed);
	NearbyTrapsStep(RowArmed);
	if (!TestTrue(TEXT("set-up: the trap 4 m beyond the first is armed, for each player"),
			NearbyTrapsArmed(PlainArmed) && NearbyTrapsArmed(RowArmed)))
	{
		return false;
	}
	ACataclysmEnemyCharacter* PlainSetter = NearbyTrapsEnemy(World, First + FVector(2.5f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* RowSetter = NearbyTrapsEnemy(World, RowHome + First + FVector(2.5f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the control's enemy"), PlainSetter)
		|| !TestNotNull(TEXT("set-up: the arming player's enemy"), RowSetter))
	{
		return false;
	}
	ACataclysmMinion* PlainNear = NearbyTrapsPlace(*this, Plain, NearAt);
	ACataclysmMinion* PlainFar = NearbyTrapsPlace(*this, Plain, FarAt);
	ACataclysmMinion* RowNear = NearbyTrapsPlace(*this, Arming, RowHome + NearAt);
	ACataclysmMinion* RowFar = NearbyTrapsPlace(*this, Arming, RowHome + FarAt);
	if (!PlainNear || !PlainFar || !RowNear || !RowFar)
	{
		return false;
	}
	FNearbyTrapsEventCount RowPlaced(Arming.System, TEXT("trap_placed"));
	const TWeakObjectPtr<ACataclysmMinion> PlainFirstWatched(PlainFirst);
	const TWeakObjectPtr<ACataclysmMinion> RowFirstWatched(RowFirst);

	// THE CONTROL, READ FIRST: THE FIRST TRAP TRIGGERS AND ARMS NOTHING.
	NearbyTrapsStep(PlainFirst);
	TestTrue(TEXT("control: the control's first trap triggered and was used up"), NearbyTrapsGone(PlainFirstWatched));
	TestFalse(TEXT("control: and with no row its unarmed trap 6 m away stays unarmed"), PlainNear->bSkillTrapArmed);

	// THE ROW: THE NEAREST UNARMED TRAP ARMS; THE FARTHER ONE DOES NOT; THE ARMED ONE IS LEFT ALONE.
	NearbyTrapsStep(RowFirst);
	TestTrue(TEXT("the arming player's first trap triggered and was used up"), NearbyTrapsGone(RowFirstWatched));
	TestTrue(TEXT("the unarmed trap 6 m away, the nearest unarmed one, is armed now and drawn armed (H2)"),
		NearbyTrapsArmed(RowNear));
	TestTrue(*FString::Printf(TEXT("as if its arming time had passed: at the age %.3f, now %.3f"),
							  RowNear->SkillTrapArmsAtAgeSeconds, RowNear->GetGameTimeSinceCreation()),
		RowNear->SkillTrapArmsAtAgeSeconds <= RowNear->GetGameTimeSinceCreation());
	TestEqual(TEXT("it is armed and not set off"), RowNear->SkillTrapTriggersMade, 0);
	TestFalse(TEXT("the unarmed trap 9 m away, within the 10 m but not the nearest, is not armed"), RowFar->bSkillTrapArmed);
	TestTrue(TEXT("the armed trap 4 m away, nearer than either, is left armed"), NearbyTrapsArmed(RowArmed));
	TestEqual(TEXT("and is not set off"), RowArmed->SkillTrapTriggersMade, 0);
	TestEqual(TEXT("arming raises no trap_placed (H2)"), RowPlaced.Count, 0);
	TestEqual(TEXT("and changes nothing of the cap: the player's other three traps stand"),
		ACataclysmMinion::SkillTrapsOf(Arming.Player).Num(), 3);
	return true;
}

// ==========================================================================
// 37. "...a shockwave that arms all traps within 15 meters instantly" and "When a gadget is destroyed it arms all traps
// within 12 meters instantly" (H2, H3, H7)
// ==========================================================================

CATACLYSM_TEST(FCataclysmNearbyTrapsArmAllTest,
	"Cataclysm.Traps.TheShockwaveAndAGadgetKilledArmEveryUnarmedTrapWithinTheirMetresAndNoneBeyondAndATrapUsedUpArmsNothing")
{
	using namespace CataclysmNearbyTrapsTest;

	// THREE PLAYERS 100 M APART, EACH WITH THE HEAVY FLAG. The control and the shockwave player each have a first trap 20 m
	// north with its enemy 2.5 m east, and three unarmed traps on the line: 6 m and 12 m beyond it, inside 15 m, and 17 m
	// short of it, beyond. The control holds no row; the other holds the shockwave row at 15 m, every trap. The gadget
	// player holds the gadget row at 12 m, every trap: a skill gadget 30 m south with three unarmed traps 5 m, 10 m and
	// 14 m west of it, and a fourth trap 6 m south of the gadget with an enemy 2.5 m east, which sets it off and uses it
	// up before the gadget is killed.
	FNearbyTrapsWorld Scope;
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
	const FVector WaveHome(100.0f * M, 0.0f, 0.0f);
	const FVector GadgetHome(200.0f * M, 0.0f, 0.0f);
	FNearbyTrapsRig Plain(World, FVector::ZeroVector);
	FNearbyTrapsRig Wave(World, WaveHome);
	FNearbyTrapsRig Gadgets(World, GadgetHome);
	if (!Plain.IsUsable(*this) || !Wave.IsUsable(*this) || !Gadgets.IsUsable(*this))
	{
		return false;
	}
	for (const FNearbyTrapsRig* Each : {&Plain, &Wave, &Gadgets})
	{
		Each->Holds({NearbyTrapsHeavyFlag()});
	}
	Wave.System->SetPoolActions({NearbyTrapsRow(TEXT("trap_triggered"), /*bArms=*/true, 15.0f, /*bNearestOnly=*/false,
												TEXT("Type.Trap"))});
	Gadgets.System->SetPoolActions({NearbyTrapsRow(TEXT("gadget_destroyed"), /*bArms=*/true, 12.0f,
												   /*bNearestOnly=*/false, TEXT("Type.Deployable"))});

	const FVector First(0.0f, 20.0f * M, 0.0f);
	const FVector East(2.5f * M, 0.0f, 0.0f);
	const FVector Gadget(0.0f, -30.0f * M, 0.0f);
	const FVector UsedUpAt = Gadget + FVector(0.0f, -6.0f * M, 0.0f);
	ACataclysmMinion* PlainFirst = NearbyTrapsPlace(*this, Plain, First);
	ACataclysmMinion* WaveFirst = NearbyTrapsPlace(*this, Wave, WaveHome + First);
	ACataclysmMinion* UsedUp = NearbyTrapsPlace(*this, Gadgets, GadgetHome + UsedUpAt);
	if (!PlainFirst || !WaveFirst || !UsedUp)
	{
		return false;
	}
	ACataclysmEnemyCharacter* PlainSetter = NearbyTrapsEnemy(World, First + East);
	ACataclysmEnemyCharacter* WaveSetter = NearbyTrapsEnemy(World, WaveHome + First + East);
	ACataclysmEnemyCharacter* UsedUpSetter = NearbyTrapsEnemy(World, GadgetHome + UsedUpAt + East);
	ACataclysmEnemyCharacter* Killer = NearbyTrapsEnemy(World, GadgetHome + FVector(0.0f, -50.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: the control's enemy"), PlainSetter)
		|| !TestNotNull(TEXT("set-up: the shockwave player's enemy"), WaveSetter)
		|| !TestNotNull(TEXT("set-up: the enemy that uses up the gadget player's fourth trap"), UsedUpSetter)
		|| !TestNotNull(TEXT("set-up: the gadget's killer"), Killer))
	{
		return false;
	}
	NearbyTrapsPass(World, 1.25f);

	// THE UNARMED TRAPS, PLACED NOW, SO NONE HAS ARMED.
	TArray<ACataclysmMinion*> PlainInside;
	TArray<ACataclysmMinion*> WaveInside;
	for (const float Metres : {6.0f, 12.0f})
	{
		PlainInside.Add(NearbyTrapsPlace(*this, Plain, First + FVector(0.0f, Metres * M, 0.0f)));
		WaveInside.Add(NearbyTrapsPlace(*this, Wave, WaveHome + First + FVector(0.0f, Metres * M, 0.0f)));
	}
	ACataclysmMinion* PlainBeyond = NearbyTrapsPlace(*this, Plain, First + FVector(0.0f, -17.0f * M, 0.0f));
	ACataclysmMinion* WaveBeyond = NearbyTrapsPlace(*this, Wave, WaveHome + First + FVector(0.0f, -17.0f * M, 0.0f));
	TArray<ACataclysmMinion*> GadgetsInside;
	for (const float Metres : {5.0f, 10.0f})
	{
		GadgetsInside.Add(NearbyTrapsPlace(*this, Gadgets, GadgetHome + Gadget + FVector(-Metres * M, 0.0f, 0.0f)));
	}
	ACataclysmMinion* GadgetsBeyond = NearbyTrapsPlace(*this, Gadgets, GadgetHome + Gadget + FVector(-14.0f * M, 0.0f, 0.0f));
	ACataclysmMinion* Placed = NearbyTrapsPlaceGadget(*this, Gadgets, GadgetHome + Gadget);
	if (PlainInside.Contains(nullptr) || WaveInside.Contains(nullptr) || GadgetsInside.Contains(nullptr) || !PlainBeyond
		|| !WaveBeyond || !GadgetsBeyond || !Placed)
	{
		return false;
	}

	// THE CONTROL, READ FIRST: THE FIRST TRAP TRIGGERS AND ARMS NOTHING.
	const TWeakObjectPtr<ACataclysmMinion> PlainFirstWatched(PlainFirst);
	const TWeakObjectPtr<ACataclysmMinion> WaveFirstWatched(WaveFirst);
	NearbyTrapsStep(PlainFirst);
	TestTrue(TEXT("control: the control's first trap triggered and was used up"), NearbyTrapsGone(PlainFirstWatched));
	TestFalse(TEXT("control: and with no row its unarmed trap 6 m away stays unarmed"), PlainInside[0]->bSkillTrapArmed);
	TestFalse(TEXT("control: and the one 12 m away"), PlainInside[1]->bSkillTrapArmed);

	// THE SHOCKWAVE: EVERY UNARMED TRAP WITHIN 15 M ARMS; THE ONE BEYOND DOES NOT.
	NearbyTrapsStep(WaveFirst);
	TestTrue(TEXT("the shockwave player's first trap triggered and was used up"), NearbyTrapsGone(WaveFirstWatched));
	TestTrue(TEXT("its unarmed trap 6 m away is armed"), NearbyTrapsArmed(WaveInside[0]));
	TestTrue(TEXT("and the one 12 m away: every trap within 15 m, not only the nearest"), NearbyTrapsArmed(WaveInside[1]));
	TestFalse(TEXT("and the one 17 m away, beyond the 15 m, is not"), WaveBeyond->bSkillTrapArmed);
	TestEqual(TEXT("none of them is set off"),
		WaveInside[0]->SkillTrapTriggersMade + WaveInside[1]->SkillTrapTriggersMade + WaveBeyond->SkillTrapTriggersMade, 0);

	// A TRAP USED UP IS NO GADGET DESTROYED (H7): THE GADGET ROW ARMS NOTHING.
	FNearbyTrapsEventCount Destroyed(Gadgets.System, TEXT("gadget_destroyed"));
	const TWeakObjectPtr<ACataclysmMinion> UsedUpWatched(UsedUp);
	NearbyTrapsStep(UsedUp);
	TestTrue(TEXT("control: the gadget player's fourth trap triggered and was used up"), NearbyTrapsGone(UsedUpWatched));
	TestEqual(TEXT("control: and raised no gadget_destroyed"), Destroyed.Count, 0);
	TestFalse(TEXT("control: so its unarmed trap 5 m from the gadget stays unarmed"), GadgetsInside[0]->bSkillTrapArmed);

	// THE GADGET KILLED: EVERY UNARMED TRAP WITHIN 12 M OF IT ARMS; THE ONE BEYOND DOES NOT.
	UCataclysmSkillEffects::ApplyDirectDamage(Killer, Placed, 1000000.0f);
	if (!TestTrue(TEXT("set-up: the gadget died"), UCataclysmSkillEffects::IsDead(Placed)))
	{
		return false;
	}
	TestEqual(TEXT("and raised gadget_destroyed once"), Destroyed.Count, 1);
	TestTrue(TEXT("the gadget player's unarmed trap 5 m from the gadget is armed"), NearbyTrapsArmed(GadgetsInside[0]));
	TestTrue(TEXT("and the one 10 m from it"), NearbyTrapsArmed(GadgetsInside[1]));
	TestFalse(TEXT("and the one 14 m from it, beyond the 12 m, is not"), GadgetsBeyond->bSkillTrapArmed);
	return true;
}

// ==========================================================================
// 38. "Traps trigger simultaneously when any one of them is triggered, if within 8-12 meters of each other" (H3, H4,
// H5 (b), H6)
// ==========================================================================

CATACLYSM_TEST(FCataclysmNearbyTrapsChainTest,
	"Cataclysm.Traps.TrapsTriggerTogetherThroughTheConnectedGroupWithinTheRadiusEachOnceEachCountingAsTriggered")
{
	using namespace CataclysmNearbyTrapsTest;

	// THREE PLAYERS 100 M APART, EACH WITH THE HEAVY FLAG AND THE SAME FOUR TRAPS ON A LINE NORTH: the first 20 m north,
	// a second 8 m beyond it, a third 16 m beyond it (8 m beyond the second) and a fourth 15 m short of it; each of the
	// first three with an enemy of its own 2.5 m east, and every other enemy more than 5 m from it, so each lash strikes
	// its own enemy alone. The third and fourth are armed by a step before any enemy stands; the second is placed
	// unarmed. The radius is 10 m, a roll inside 8-12: the second is within it of the first, the third only of the second,
	// the fourth of none. The control holds the slow row and the damage stacks and no chain row; the chain player holds
	// the same and the chain row; the rearming player holds the chain row and the rearm chance at 100, so every trap
	// stands after it goes off.
	FNearbyTrapsWorld Scope;
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
	const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();
	if (!TestTrue(TEXT("set-up: the Cripple tag exists"), Cripple.IsValid()))
	{
		return false;
	}
	const FVector ChainHome(100.0f * M, 0.0f, 0.0f);
	const FVector RearmHome(200.0f * M, 0.0f, 0.0f);
	FNearbyTrapsRig Plain(World, FVector::ZeroVector);
	FNearbyTrapsRig Chain(World, ChainHome);
	FNearbyTrapsRig Rearming(World, RearmHome);
	if (!Plain.IsUsable(*this) || !Chain.IsUsable(*this) || !Rearming.IsUsable(*this))
	{
		return false;
	}
	Plain.Holds({NearbyTrapsHeavyFlag()});
	Chain.Holds({NearbyTrapsHeavyFlag()});
	Rearming.Holds({NearbyTrapsHeavyFlag(), NearbyTrapsRearmAlways()});
	const FName PlainKey(TEXT("NearbyTrapsTest:PlainStacks"));
	const FName ChainKey(TEXT("NearbyTrapsTest:ChainStacks"));
	const FCataclysmPoolAction Together =
		NearbyTrapsRow(TEXT("trap_triggered"), /*bArms=*/false, 10.0f, /*bNearestOnly=*/false, TEXT("Type.Trap"));
	Plain.System->SetPoolActions({NearbyTrapsSlowRow(), NearbyTrapsStackGrant(PlainKey)});
	Chain.System->SetPoolActions({Together, NearbyTrapsSlowRow(), NearbyTrapsStackGrant(ChainKey)});
	Rearming.System->SetPoolActions({Together});

	const FVector Spots[4] = {FVector(0.0f, 20.0f * M, 0.0f), FVector(0.0f, 28.0f * M, 0.0f),
							  FVector(0.0f, 36.0f * M, 0.0f), FVector(0.0f, 5.0f * M, 0.0f)};
	const FVector East(2.5f * M, 0.0f, 0.0f);
	const FNearbyTrapsRig* Rigs[3] = {&Plain, &Chain, &Rearming};
	const FVector Homes[3] = {FVector::ZeroVector, ChainHome, RearmHome};
	// [player][trap]
	ACataclysmMinion* Traps[3][4] = {};
	ACataclysmEnemyCharacter* Enemies[3][3] = {};
	for (int32 Who = 0; Who < 3; ++Who)
	{
		for (const int32 Which : {0, 2, 3})
		{
			Traps[Who][Which] = NearbyTrapsPlace(*this, *Rigs[Who], Homes[Who] + Spots[Which]);
			if (!Traps[Who][Which])
			{
				return false;
			}
		}
	}
	// THE THIRD AND FOURTH ARMED BY A STEP, BEFORE ANY ENEMY STANDS NEAR.
	NearbyTrapsPass(World, 1.25f);
	for (int32 Who = 0; Who < 3; ++Who)
	{
		NearbyTrapsStep(Traps[Who][2]);
		NearbyTrapsStep(Traps[Who][3]);
		if (!TestTrue(*FString::Printf(TEXT("set-up: player %d's third and fourth traps are armed"), Who),
				Traps[Who][2]->bSkillTrapArmed && Traps[Who][3]->bSkillTrapArmed))
		{
			return false;
		}
	}
	for (int32 Who = 0; Who < 3; ++Who)
	{
		for (int32 Which = 0; Which < 3; ++Which)
		{
			Enemies[Who][Which] = NearbyTrapsEnemy(World, Homes[Who] + Spots[Which] + East);
			if (!TestNotNull(*FString::Printf(TEXT("set-up: player %d's enemy %d"), Who, Which), Enemies[Who][Which]))
			{
				return false;
			}
		}
		Traps[Who][1] = NearbyTrapsPlace(*this, *Rigs[Who], Homes[Who] + Spots[1]);
		if (!Traps[Who][1])
		{
			return false;
		}
	}
	TWeakObjectPtr<ACataclysmMinion> Watched[3][4];
	for (int32 Who = 0; Who < 3; ++Who)
	{
		for (int32 Which = 0; Which < 4; ++Which)
		{
			Watched[Who][Which] = Traps[Who][Which];
		}
	}
	FNearbyTrapsEventCount PlainTriggers(Plain.System, TEXT("trap_triggered"));
	FNearbyTrapsEventCount ChainTriggers(Chain.System, TEXT("trap_triggered"));
	FNearbyTrapsEventCount RearmTriggers(Rearming.System, TEXT("trap_triggered"));

	// THE CONTROL, READ FIRST: ITS FIRST TRAP GOES OFF ALONE, SLOWS ITS OWN ENEMY AND ADDS ONE STACK.
	NearbyTrapsStep(Traps[0][0]);
	TestEqual(TEXT("control: one trap_triggered"), PlainTriggers.Count, 1);
	TestTrue(TEXT("control: its first trap was used up"), NearbyTrapsGone(Watched[0][0]));
	TestFalse(TEXT("control: its second trap, 8 m away, stands: no row sets it off"), NearbyTrapsGone(Watched[0][1]));
	TestTrue(TEXT("control: the first trap's enemy is slowed"), NearbyTrapsCarries(Enemies[0][0], Cripple));
	TestFalse(TEXT("control: the second trap's enemy is not"), NearbyTrapsCarries(Enemies[0][1], Cripple));
	TestEqual(TEXT("control: one damage stack"), Plain.System->OwnStacksHeld(PlainKey), 1);

	// THE CHAIN: THE FIRST SETS OFF THE SECOND, WITHIN 10 M; THE SECOND, COUNTING AS TRIGGERED, SETS OFF THE THIRD; THE
	// FOURTH, WITHIN 10 M OF NONE, STANDS.
	NearbyTrapsStep(Traps[1][0]);
	TestEqual(TEXT("three trap_triggered, each announced once: the first and the two it chained"), ChainTriggers.Count, 3);
	TestTrue(TEXT("the first trap was used up"), NearbyTrapsGone(Watched[1][0]));
	TestTrue(TEXT("the second, unarmed and 8 m from the first, went off with it and was used up (H4)"),
		NearbyTrapsGone(Watched[1][1]));
	TestTrue(TEXT("the third, 16 m from the first but 8 m from the second, went off too: the chain (H5 (b))"),
		NearbyTrapsGone(Watched[1][2]));
	if (!TestFalse(TEXT("the fourth, 15 m from the first and farther from the others, stands"),
			NearbyTrapsGone(Watched[1][3])))
	{
		return false;
	}
	TestEqual(TEXT("and was not set off"), Traps[1][3]->SkillTrapTriggersMade, 0);
	TestTrue(*FString::Printf(TEXT("the second trap's cast struck its own enemy: %.2f"),
							  NearbyTrapsTakenFrom(Enemies[1][1])),
		NearbyTrapsTakenFrom(Enemies[1][1]) >= NearbyTrapsEnemyHealth / 10.0f);
	TestTrue(*FString::Printf(TEXT("and the third trap's its own: %.2f"), NearbyTrapsTakenFrom(Enemies[1][2])),
		NearbyTrapsTakenFrom(Enemies[1][2]) >= NearbyTrapsEnemyHealth / 10.0f);
	TestTrue(TEXT("the first trap's enemy is slowed"), NearbyTrapsCarries(Enemies[1][0], Cripple));
	TestTrue(TEXT("and the second trap's own enemy, which no step found: the second counts as triggered, at its own enemy "
				  "(H6)"),
		NearbyTrapsCarries(Enemies[1][1], Cripple));
	TestTrue(TEXT("and the third trap's own enemy"), NearbyTrapsCarries(Enemies[1][2], Cripple));
	TestEqual(TEXT("by the row's 25 per cent"), UCataclysmSkillEffects::StatedStrengthOn(Enemies[1][2], Cripple), 25.0f,
		0.01f);
	TestEqual(TEXT("three damage stacks, one for each trap that went off"), Chain.System->OwnStacksHeld(ChainKey), 3);

	// EACH ONCE, A REARMED TRAP INCLUDED: every trap stands after it goes off, and none goes off twice in the chain.
	NearbyTrapsStep(Traps[2][0]);
	TestEqual(TEXT("with every trap rearming, still three trap_triggered"), RearmTriggers.Count, 3);
	bool bAllStand = true;
	for (int32 Which = 0; Which < 4; ++Which)
	{
		bAllStand &= !NearbyTrapsGone(Watched[2][Which]);
	}
	if (!TestTrue(TEXT("set-up: every trap of the rearming player stands, rearmed or never set off"), bAllStand))
	{
		return false;
	}
	TestEqual(TEXT("its first trap went off once, though it stood rearmed within reach of the second (H5 (b))"),
		Traps[2][0]->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("its second went off once, though it stood rearmed within reach of the first and the third"),
		Traps[2][1]->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("its third went off once"), Traps[2][2]->SkillTrapTriggersMade, 1);
	TestEqual(TEXT("and its fourth not at all"), Traps[2][3]->SkillTrapTriggersMade, 0);
	return true;
}

// ==========================================================================
// 39. "When a gadget is destroyed, the nearest active trap within 10 meters is triggered immediately" (H3, H4, H7)
// ==========================================================================

CATACLYSM_TEST(FCataclysmNearbyTrapsGadgetSetsOffTheNearestTest,
	"Cataclysm.Traps.AGadgetKilledSetsOffTheNearestStandingTrapWithinTenMetresArmedOrNotAndNoOther")
{
	using namespace CataclysmNearbyTrapsTest;

	// TWO PLAYERS 100 M APART, EACH WITH THE HEAVY FLAG, A SKILL GADGET 30 M SOUTH AND THREE TRAPS WEST OF IT: 4 m away,
	// unarmed, with an enemy 2.5 m north of it; 7 m away, armed by a step before any enemy stands; and 12 m away, beyond
	// the 10 m. The control holds no row; the other the gadget row at 10 m, the nearest trap alone.
	FNearbyTrapsWorld Scope;
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
	FNearbyTrapsRig Plain(World, FVector::ZeroVector);
	FNearbyTrapsRig Row(World, RowHome);
	if (!Plain.IsUsable(*this) || !Row.IsUsable(*this))
	{
		return false;
	}
	Plain.Holds({NearbyTrapsHeavyFlag()});
	Row.Holds({NearbyTrapsHeavyFlag()});
	Row.System->SetPoolActions({NearbyTrapsRow(TEXT("gadget_destroyed"), /*bArms=*/false, 10.0f, /*bNearestOnly=*/true,
											   TEXT("Type.Deployable"))});

	const FVector Gadget(0.0f, -30.0f * M, 0.0f);
	const FVector Nearest = Gadget + FVector(-4.0f * M, 0.0f, 0.0f);
	const FVector Farther = Gadget + FVector(-7.0f * M, 0.0f, 0.0f);
	const FVector Beyond = Gadget + FVector(-12.0f * M, 0.0f, 0.0f);
	ACataclysmMinion* PlainFarther = NearbyTrapsPlace(*this, Plain, Farther);
	ACataclysmMinion* RowFarther = NearbyTrapsPlace(*this, Row, RowHome + Farther);
	if (!PlainFarther || !RowFarther)
	{
		return false;
	}
	NearbyTrapsPass(World, 1.25f);
	NearbyTrapsStep(PlainFarther);
	NearbyTrapsStep(RowFarther);
	if (!TestTrue(TEXT("set-up: the trap 7 m from each gadget is armed"),
			RowFarther->bSkillTrapArmed && PlainFarther->bSkillTrapArmed))
	{
		return false;
	}
	ACataclysmMinion* PlainNearest = NearbyTrapsPlace(*this, Plain, Nearest);
	ACataclysmMinion* PlainBeyond = NearbyTrapsPlace(*this, Plain, Beyond);
	ACataclysmMinion* RowNearest = NearbyTrapsPlace(*this, Row, RowHome + Nearest);
	ACataclysmMinion* RowBeyond = NearbyTrapsPlace(*this, Row, RowHome + Beyond);
	ACataclysmMinion* PlainGadget = NearbyTrapsPlaceGadget(*this, Plain, Gadget);
	ACataclysmMinion* RowGadget = NearbyTrapsPlaceGadget(*this, Row, RowHome + Gadget);
	if (!PlainNearest || !PlainBeyond || !RowNearest || !RowBeyond || !PlainGadget || !RowGadget)
	{
		return false;
	}
	const FVector North(0.0f, 2.5f * M, 0.0f);
	ACataclysmEnemyCharacter* PlainEnemy = NearbyTrapsEnemy(World, Nearest + North);
	ACataclysmEnemyCharacter* RowEnemy = NearbyTrapsEnemy(World, RowHome + Nearest + North);
	ACataclysmEnemyCharacter* PlainKiller = NearbyTrapsEnemy(World, Gadget + FVector(0.0f, -20.0f * M, 0.0f));
	ACataclysmEnemyCharacter* RowKiller = NearbyTrapsEnemy(World, RowHome + Gadget + FVector(0.0f, -20.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: the control's enemy by its nearest trap"), PlainEnemy)
		|| !TestNotNull(TEXT("set-up: the row player's enemy by its nearest trap"), RowEnemy)
		|| !TestNotNull(TEXT("set-up: the control's killer"), PlainKiller)
		|| !TestNotNull(TEXT("set-up: the row player's killer"), RowKiller))
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> RowNearestWatched(RowNearest);

	// THE CONTROL, READ FIRST: ITS GADGET KILLED SETS NOTHING OFF.
	UCataclysmSkillEffects::ApplyDirectDamage(PlainKiller, PlainGadget, 1000000.0f);
	TestTrue(TEXT("control: the control's gadget died"), UCataclysmSkillEffects::IsDead(PlainGadget));
	TestEqual(TEXT("control: and with no row its nearest trap was not set off"), PlainNearest->SkillTrapTriggersMade, 0);
	TestEqual(TEXT("control: and its enemy took nothing"), NearbyTrapsTakenFrom(PlainEnemy), 0.0f, 0.01f);

	// THE ROW: THE NEAREST STANDING TRAP, UNARMED, GOES OFF AT ITS OWN ENEMY; THE ARMED ONE FARTHER AND THE ONE BEYOND DO NOT.
	FNearbyTrapsEventCount RowTriggers(Row.System, TEXT("trap_triggered"));
	UCataclysmSkillEffects::ApplyDirectDamage(RowKiller, RowGadget, 1000000.0f);
	if (!TestTrue(TEXT("set-up: the row player's gadget died"), UCataclysmSkillEffects::IsDead(RowGadget)))
	{
		return false;
	}
	TestEqual(TEXT("one trap_triggered"), RowTriggers.Count, 1);
	TestTrue(TEXT("the nearest trap, 4 m from the gadget and unarmed, went off and was used up (\"active\" is standing, H4)"),
		NearbyTrapsGone(RowNearestWatched));
	TestTrue(*FString::Printf(TEXT("its cast struck its own enemy: %.2f"), NearbyTrapsTakenFrom(RowEnemy)),
		NearbyTrapsTakenFrom(RowEnemy) > 1.0f);
	TestEqual(TEXT("the armed trap 7 m away, within the 10 m but not the nearest, was not set off"),
		RowFarther->SkillTrapTriggersMade, 0);
	TestEqual(TEXT("nor the one 12 m away, beyond the 10 m"), RowBeyond->SkillTrapTriggersMade, 0);
	return true;
}

// ==========================================================================
// 40. The trap half of "When you die, all your active traps and gadgets immediately trigger simultaneously" (H8)
// ==========================================================================

CATACLYSM_TEST(FCataclysmNearbyTrapsDeathSetsOffEveryTrapTest,
	"Cataclysm.Traps.AtThePlayersDeathEveryStandingTrapGoesOffWhereverItStandsAndTheGadgetHalfIsUnchanged")
{
	using namespace CataclysmNearbyTrapsTest;

	// TWO PLAYERS 100 M APART, EACH WITH THE HEAVY TRAP FLAG AND THE ULTIMATE GADGET FLAG: a trap 20 m north with an enemy
	// 2.5 m east of it, armed; a trap 40 m south with no enemy within its reach, unarmed; a skill gadget 40 m east with an
	// enemy 3 m east of it; and a killer 3 m west of the player. Both hold the gadget half's row; the wearer also holds
	// the trap half's, with a radius of nought, every trap.
	FNearbyTrapsWorld Scope;
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
	FNearbyTrapsRig Plain(World, FVector::ZeroVector);
	FNearbyTrapsRig Wearer(World, WearerHome);
	if (!Plain.IsUsable(*this) || !Wearer.IsUsable(*this))
	{
		return false;
	}
	FCataclysmPoolAction GadgetHalf;
	GadgetHalf.Event = FName(TEXT("player_death"));
	GadgetHalf.Pool = FName(UCataclysmAbilitySystemComponent::GadgetsCastNowAction);
	GadgetHalf.Percent = 100.0f;
	GadgetHalf.bGadgetsCastNow = true;
	GadgetHalf.TriggerKey = FName(TEXT("NearbyTrapsTest:gadgets_cast_now:player_death"));
	for (const FNearbyTrapsRig* Each : {&Plain, &Wearer})
	{
		Each->Holds({NearbyTrapsHeavyFlag(), NearbyTrapsUltimateGadgetFlag()});
	}
	Plain.System->SetPoolActions({GadgetHalf});
	Wearer.System->SetPoolActions({GadgetHalf, NearbyTrapsRow(TEXT("player_death"), /*bArms=*/false, 0.0f,
															  /*bNearestOnly=*/false, nullptr)});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector South(0.0f, -40.0f * M, 0.0f);
	const FVector Gadget(40.0f * M, 0.0f, 0.0f);
	ACataclysmMinion* PlainNorth = NearbyTrapsPlace(*this, Plain, North);
	ACataclysmMinion* WearerNorth = NearbyTrapsPlace(*this, Wearer, WearerHome + North);
	if (!PlainNorth || !WearerNorth)
	{
		return false;
	}
	NearbyTrapsPass(World, 1.25f);
	NearbyTrapsStep(PlainNorth);
	NearbyTrapsStep(WearerNorth);
	ACataclysmMinion* PlainSouth = NearbyTrapsPlace(*this, Plain, South);
	ACataclysmMinion* WearerSouth = NearbyTrapsPlace(*this, Wearer, WearerHome + South);
	ACataclysmMinion* PlainGadget = NearbyTrapsPlaceGadget(*this, Plain, Gadget);
	ACataclysmMinion* WearerGadget = NearbyTrapsPlaceGadget(*this, Wearer, WearerHome + Gadget);
	if (!PlainSouth || !WearerSouth || !PlainGadget || !WearerGadget)
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: each player's north trap is armed and its south trap is not"),
			PlainNorth->bSkillTrapArmed && WearerNorth->bSkillTrapArmed && !PlainSouth->bSkillTrapArmed
			&& !WearerSouth->bSkillTrapArmed))
	{
		return false;
	}
	const FVector East(2.5f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* PlainTrapsEnemy = NearbyTrapsEnemy(World, North + East);
	ACataclysmEnemyCharacter* WearerTrapsEnemy = NearbyTrapsEnemy(World, WearerHome + North + East);
	ACataclysmEnemyCharacter* PlainGadgetsEnemy = NearbyTrapsEnemy(World, Gadget + FVector(3.0f * M, 0.0f, 0.0f), 100000.0f);
	ACataclysmEnemyCharacter* WearerGadgetsEnemy =
		NearbyTrapsEnemy(World, WearerHome + Gadget + FVector(3.0f * M, 0.0f, 0.0f), 100000.0f);
	ACataclysmEnemyCharacter* PlainKiller = NearbyTrapsEnemy(World, FVector(-3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* WearerKiller = NearbyTrapsEnemy(World, WearerHome + FVector(-3.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the control's trap's enemy"), PlainTrapsEnemy)
		|| !TestNotNull(TEXT("set-up: the wearer's trap's enemy"), WearerTrapsEnemy)
		|| !TestNotNull(TEXT("set-up: the control's gadget's enemy"), PlainGadgetsEnemy)
		|| !TestNotNull(TEXT("set-up: the wearer's gadget's enemy"), WearerGadgetsEnemy)
		|| !TestNotNull(TEXT("set-up: the control's killer"), PlainKiller)
		|| !TestNotNull(TEXT("set-up: the wearer's killer"), WearerKiller))
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> WearerNorthWatched(WearerNorth);
	const TWeakObjectPtr<ACataclysmMinion> WearerSouthWatched(WearerSouth);

	// THE CONTROL, READ FIRST: AT ITS DEATH ITS GADGET FIRES AND ITS TRAPS DO NOT.
	FNearbyTrapsEventCount PlainTriggers(Plain.System, TEXT("trap_triggered"));
	if (!TestTrue(TEXT("control: the player holding the gadget half alone died"), Plain.DiesTo(PlainKiller)))
	{
		return false;
	}
	TestEqual(TEXT("control: its gadget cast once at the death"), PlainGadget->SkillGadgetDeathCastsMade, 1);
	TestEqual(TEXT("control: no trap_triggered"), PlainTriggers.Count, 0);
	TestEqual(TEXT("control: its north trap was not set off"), PlainNorth->SkillTrapTriggersMade, 0);
	TestEqual(TEXT("control: nor its south trap"), PlainSouth->SkillTrapTriggersMade, 0);

	// THE WEARER: EVERY STANDING TRAP GOES OFF, ARMED OR NOT, NEAR OR FAR, AND THE GADGET FIRES AS BEFORE.
	FNearbyTrapsEventCount WearerTriggers(Wearer.System, TEXT("trap_triggered"));
	if (!TestTrue(TEXT("the wearer died"), Wearer.DiesTo(WearerKiller)))
	{
		return false;
	}
	TestEqual(TEXT("two trap_triggered, one for each standing trap"), WearerTriggers.Count, 2);
	TestTrue(TEXT("its north trap, 20 m from it and armed, went off at the death and was used up"),
		NearbyTrapsGone(WearerNorthWatched));
	TestTrue(*FString::Printf(TEXT("its cast struck its own enemy, at the death of the player whose skill it is: %.2f"),
							  NearbyTrapsTakenFrom(WearerTrapsEnemy)),
		NearbyTrapsTakenFrom(WearerTrapsEnemy) > 1.0f);
	TestTrue(TEXT("its south trap, 40 m from it, unarmed and with no enemy near, went off too and was used up (T11)"),
		NearbyTrapsGone(WearerSouthWatched));
	TestEqual(TEXT("its gadget cast once at the death, as the gadget half alone does"), WearerGadget->SkillGadgetDeathCastsMade,
		1);
	return true;
}

// ==========================================================================
// 41. A trap set off by a row: its own triggering enemy, and with none, its own place (H6, T11)
// ==========================================================================

CATACLYSM_TEST(FCataclysmNearbyTrapsForcedTriggeringEnemyTest,
	"Cataclysm.Traps.ATrapSetOffByARowHasTheNearestEnemyWithinItsOwnRadiusAsItsTriggeringEnemyAndWithNoneCastsAtItsPlace")
{
	using namespace CataclysmNearbyTrapsTest;

	// ONE PLAYER WITH THE HEAVY FLAG, THE CHAIN ROW AT 10 M AND THE REARM CHANCE AT 100, so each trap stands to be read
	// after it goes off. Three traps on a line north: the first 20 m north with its enemy 2.5 m east; a second 6 m beyond
	// it with two enemies of its own, one 2 m east of it and one 2.8 m west, both within its 3 m; and a third 6 m short of
	// the first with no enemy within its 3 m. Every enemy is more than 5 m from every trap but its own.
	FNearbyTrapsWorld Scope;
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
	FNearbyTrapsRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	Rig.Holds({NearbyTrapsHeavyFlag(), NearbyTrapsRearmAlways()});
	Rig.System->SetPoolActions({NearbyTrapsRow(TEXT("trap_triggered"), /*bArms=*/false, 10.0f, /*bNearestOnly=*/false,
											   TEXT("Type.Trap"))});

	const FVector First(0.0f, 20.0f * M, 0.0f);
	const FVector Second(0.0f, 26.0f * M, 0.0f);
	const FVector Third(0.0f, 14.0f * M, 0.0f);
	ACataclysmMinion* FirstTrap = NearbyTrapsPlace(*this, Rig, First);
	ACataclysmMinion* SecondTrap = NearbyTrapsPlace(*this, Rig, Second);
	ACataclysmMinion* ThirdTrap = NearbyTrapsPlace(*this, Rig, Third);
	if (!FirstTrap || !SecondTrap || !ThirdTrap)
	{
		return false;
	}
	ACataclysmEnemyCharacter* FirstsEnemy = NearbyTrapsEnemy(World, First + FVector(2.5f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* SecondsNearer = NearbyTrapsEnemy(World, Second + FVector(2.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* SecondsFarther = NearbyTrapsEnemy(World, Second + FVector(-2.8f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the first trap's enemy"), FirstsEnemy)
		|| !TestNotNull(TEXT("set-up: the second trap's nearer enemy"), SecondsNearer)
		|| !TestNotNull(TEXT("set-up: the second trap's farther enemy"), SecondsFarther))
	{
		return false;
	}
	NearbyTrapsPass(World, 1.25f);

	// THE CONTROL, READ FIRST: THE TRAP AN ENEMY SET OFF HAS THAT ENEMY.
	const TWeakObjectPtr<ACataclysmMinion> FirstWatched(FirstTrap);
	const TWeakObjectPtr<ACataclysmMinion> SecondWatched(SecondTrap);
	const TWeakObjectPtr<ACataclysmMinion> ThirdWatched(ThirdTrap);
	NearbyTrapsStep(FirstTrap);
	if (!TestTrue(TEXT("set-up: all three traps stand after going off, rearmed, so each can be read"),
			!NearbyTrapsGone(FirstWatched) && !NearbyTrapsGone(SecondWatched) && !NearbyTrapsGone(ThirdWatched)))
	{
		return false;
	}
	TestEqual(TEXT("control: the first trap went off once"), FirstTrap->SkillTrapTriggersMade, 1);
	TestTrue(TEXT("control: its triggering enemy is the one that walked within its 3 m"),
		FirstTrap->SkillTrapTriggeringEnemy.Get() == FirstsEnemy);

	// THE SECOND, SET OFF BY THE ROW: THE NEAREST LIVING ENEMY WITHIN ITS OWN RADIUS, AND NOT THE FIRST TRAP'S (H6 (a)).
	TestEqual(TEXT("the second trap went off with it"), SecondTrap->SkillTrapTriggersMade, 1);
	TestTrue(TEXT("its triggering enemy is the nearer of its own two enemies, not the first trap's (H6 (a))"),
		SecondTrap->SkillTrapTriggeringEnemy.Get() == SecondsNearer);
	TestTrue(*FString::Printf(TEXT("and its cast struck that enemy: %.2f"), NearbyTrapsTakenFrom(SecondsNearer)),
		NearbyTrapsTakenFrom(SecondsNearer) > 1.0f);

	// THE THIRD, SET OFF WITH NO ENEMY WITHIN ITS RADIUS: IT CASTS AT ITS OWN PLACE, WITH NO TRIGGERING ENEMY (T11).
	TestEqual(TEXT("the third trap, with no enemy within its 3 m, went off with it all the same (T11)"),
		ThirdTrap->SkillTrapTriggersMade, 1);
	TestTrue(TEXT("and has no triggering enemy, so no row on its enemy adds anything for it"),
		ThirdTrap->SkillTrapTriggeringEnemy.Get() == nullptr);
	return true;
}

// ==========================================================================
// 42. A trap set off by "Traps trigger simultaneously" is a whole trigger: its instant blow lays the trap bleed (G-1 (a))
// ==========================================================================

CATACLYSM_TEST(FCataclysmNearbyTrapsChainedBleedTest,
	"Cataclysm.Traps.AChainedTrapsInstantBlowLaysTheTrapBleedAsTheFirstTrapsDoes")
{
	using namespace CataclysmNearbyTrapsTest;

	// TWO PLAYERS 100 M APART, EACH WITH THE HEAVY FLAG AND THE CHAIN ROW AT 10 M: a first trap 20 m north with its enemy
	// 2.5 m east, which sets it off, and a second trap 8 m beyond it, unarmed, with its own enemy 2.5 m east. The control
	// holds no bleed row; the other holds "Traps apply 1-2 bleed stacks to all enemies caught in their blast" at 1, as
	// layer 3's tests write it: `apply_status_sized` on `deployable_hit`, requiring `Type.Trap`, the Ailment Bleed.
	FNearbyTrapsWorld Scope;
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
	const FVector RowHome(100.0f * M, 0.0f, 0.0f);
	FNearbyTrapsRig Plain(World, FVector::ZeroVector);
	FNearbyTrapsRig Bleeding(World, RowHome);
	if (!Plain.IsUsable(*this) || !Bleeding.IsUsable(*this))
	{
		return false;
	}
	Plain.Holds({NearbyTrapsHeavyFlag()});
	Bleeding.Holds({NearbyTrapsHeavyFlag()});
	const FCataclysmPoolAction Together =
		NearbyTrapsRow(TEXT("trap_triggered"), /*bArms=*/false, 10.0f, /*bNearestOnly=*/false, TEXT("Type.Trap"));
	FCataclysmPoolAction BleedRow;
	BleedRow.Event = FName(TEXT("deployable_hit"));
	BleedRow.Pool = FName(UCataclysmAbilitySystemComponent::ApplyStatusSizedAction);
	BleedRow.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false));
	BleedRow.TriggerKey = FName(TEXT("NearbyTrapsTest:deployable_hit:bleed"));
	BleedRow.TriggerCooldownSeconds = 0.0f;
	BleedRow.ApplyStatus = ECataclysmApplyStatus::Sized;
	BleedRow.StatusName = TEXT("Bleed");
	BleedRow.Percent = 1.0f;
	Plain.System->SetPoolActions({Together});
	Bleeding.System->SetPoolActions({Together, BleedRow});

	const FVector First(0.0f, 20.0f * M, 0.0f);
	const FVector Second(0.0f, 28.0f * M, 0.0f);
	const FVector East(2.5f * M, 0.0f, 0.0f);
	ACataclysmMinion* PlainFirst = NearbyTrapsPlace(*this, Plain, First);
	ACataclysmMinion* RowFirst = NearbyTrapsPlace(*this, Bleeding, RowHome + First);
	if (!PlainFirst || !RowFirst)
	{
		return false;
	}
	NearbyTrapsPass(World, 1.25f);
	ACataclysmEnemyCharacter* PlainFirstsEnemy = NearbyTrapsEnemy(World, First + East);
	ACataclysmEnemyCharacter* PlainSecondsEnemy = NearbyTrapsEnemy(World, Second + East);
	ACataclysmEnemyCharacter* RowFirstsEnemy = NearbyTrapsEnemy(World, RowHome + First + East);
	ACataclysmEnemyCharacter* RowSecondsEnemy = NearbyTrapsEnemy(World, RowHome + Second + East);
	if (!TestNotNull(TEXT("set-up: the control's first trap's enemy"), PlainFirstsEnemy)
		|| !TestNotNull(TEXT("set-up: the control's second trap's enemy"), PlainSecondsEnemy)
		|| !TestNotNull(TEXT("set-up: the bleeding player's first trap's enemy"), RowFirstsEnemy)
		|| !TestNotNull(TEXT("set-up: the bleeding player's second trap's enemy"), RowSecondsEnemy))
	{
		return false;
	}
	ACataclysmMinion* PlainSecond = NearbyTrapsPlace(*this, Plain, Second);
	ACataclysmMinion* RowSecond = NearbyTrapsPlace(*this, Bleeding, RowHome + Second);
	if (!PlainSecond || !RowSecond)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> PlainSecondWatched(PlainSecond);
	const TWeakObjectPtr<ACataclysmMinion> RowSecondWatched(RowSecond);

	// THE CONTROL, READ FIRST: THE CHAIN SETS THE SECOND OFF, AND WITH NO ROW NOTHING BLEEDS.
	NearbyTrapsStep(PlainFirst);
	TestTrue(TEXT("control: the second trap went off with the first and was used up"), NearbyTrapsGone(PlainSecondWatched));
	TestTrue(*FString::Printf(TEXT("control: its cast struck its own enemy for a tenth: %.2f"),
							  NearbyTrapsTakenFrom(PlainSecondsEnemy)),
		NearbyTrapsTakenFrom(PlainSecondsEnemy) >= NearbyTrapsEnemyHealth / 10.0f);
	TestFalse(TEXT("control: and with no row its enemy does not bleed"), NearbyTrapsCarries(PlainSecondsEnemy, Bleed));

	// THE ROW: THE FIRST TRAP'S BLOW BLEEDS ITS ENEMY, AS LAYER 3 BUILT; AND THE CHAINED TRAP'S BLOW BLEEDS ITS OWN.
	NearbyTrapsStep(RowFirst);
	TestTrue(TEXT("the first trap's own blow bleeds its enemy"), NearbyTrapsCarries(RowFirstsEnemy, Bleed));
	TestTrue(TEXT("the second trap went off with it and was used up"), NearbyTrapsGone(RowSecondWatched));
	TestTrue(*FString::Printf(TEXT("its cast struck its own enemy for a tenth: %.2f"), NearbyTrapsTakenFrom(RowSecondsEnemy)),
		NearbyTrapsTakenFrom(RowSecondsEnemy) >= NearbyTrapsEnemyHealth / 10.0f);
	TestTrue(TEXT("and that instant blow of the chained trap bleeds its enemy: a whole trigger (G-1 (a))"),
		NearbyTrapsCarries(RowSecondsEnemy, Bleed));
	return true;
}

// ==========================================================================
// 43. A chained trap's kill explodes, "Enemies killed by traps explode" (G-1 (a))
// ==========================================================================

CATACLYSM_TEST(FCataclysmNearbyTrapsChainedKillExplodesTest,
	"Cataclysm.Traps.AnEnemyAChainedTrapsBlowKillsExplodesForItsShareOfTheBlowAsSent")
{
	using namespace CataclysmNearbyTrapsTest;

	// TWO PLAYERS 100 M APART, EACH WITH THE HEAVY FLAG AND THE CHAIN ROW AT 10 M: a first trap 20 m north with its enemy
	// 2.5 m east, whose lash is the measure; and a second trap 8 m beyond it, unarmed, with a doomed enemy of 50 health 2.5
	// m east of it and a bystander 3.5 m north of the doomed, inside 4 m of it and outside the second lash's cone. The
	// control holds no explosion row; the other holds "Enemies killed by traps explode" at 50, as layer 4's tests write
	// it: `explode_victim_for_killing_blow` on `kill`, requiring `Type.Trap`.
	FNearbyTrapsWorld Scope;
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
	FNearbyTrapsRig Plain(World, FVector::ZeroVector);
	FNearbyTrapsRig Exploding(World, RowHome);
	if (!Plain.IsUsable(*this) || !Exploding.IsUsable(*this))
	{
		return false;
	}
	// THE WHIP IS SLASHING, so what the measure loses is the lash as sent and its 10% to health (`SubtypeBonus`), while
	// the explosion is a share of the lash as sent (layer 4, R-ii).
	if (!TestTrue(TEXT("set-up: the Whip is a Slashing weapon, as a blow's resolution reads it"),
			UCataclysmWeaponSlotsComponent::SubTypeOf(Exploding.Player) == TEXT("Slashing")))
	{
		return false;
	}
	Plain.Holds({NearbyTrapsHeavyFlag()});
	Exploding.Holds({NearbyTrapsHeavyFlag()});
	const FCataclysmPoolAction Together =
		NearbyTrapsRow(TEXT("trap_triggered"), /*bArms=*/false, 10.0f, /*bNearestOnly=*/false, TEXT("Type.Trap"));
	FCataclysmPoolAction Explodes;
	Explodes.Event = FName(TEXT("kill"));
	Explodes.Pool = FName(UCataclysmAbilitySystemComponent::ExplodeVictimForKillingBlowAction);
	Explodes.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false));
	Explodes.TriggerKey = FName(TEXT("NearbyTrapsTest:kill:explode"));
	Explodes.bExplodeVictimForKillingBlow = true;
	Explodes.Percent = 50.0f;
	Plain.System->SetPoolActions({Together});
	Exploding.System->SetPoolActions({Together, Explodes});

	const FVector First(0.0f, 20.0f * M, 0.0f);
	const FVector Second(0.0f, 28.0f * M, 0.0f);
	const FVector East(2.5f * M, 0.0f, 0.0f);
	const FVector BeyondTheDoomed(0.0f, 3.5f * M, 0.0f);
	constexpr float Doomed = 50.0f;
	ACataclysmMinion* PlainFirst = NearbyTrapsPlace(*this, Plain, First);
	ACataclysmMinion* RowFirst = NearbyTrapsPlace(*this, Exploding, RowHome + First);
	if (!PlainFirst || !RowFirst)
	{
		return false;
	}
	NearbyTrapsPass(World, 1.25f);
	ACataclysmEnemyCharacter* PlainMeasure = NearbyTrapsEnemy(World, First + East);
	ACataclysmEnemyCharacter* PlainDoomed = NearbyTrapsEnemy(World, Second + East, Doomed);
	ACataclysmEnemyCharacter* PlainBystander = NearbyTrapsEnemy(World, Second + East + BeyondTheDoomed);
	ACataclysmEnemyCharacter* RowMeasure = NearbyTrapsEnemy(World, RowHome + First + East);
	ACataclysmEnemyCharacter* RowDoomed = NearbyTrapsEnemy(World, RowHome + Second + East, Doomed);
	ACataclysmEnemyCharacter* RowBystander = NearbyTrapsEnemy(World, RowHome + Second + East + BeyondTheDoomed);
	if (!TestNotNull(TEXT("set-up: the control's measure"), PlainMeasure)
		|| !TestNotNull(TEXT("set-up: the control's doomed enemy"), PlainDoomed)
		|| !TestNotNull(TEXT("set-up: the control's bystander"), PlainBystander)
		|| !TestNotNull(TEXT("set-up: the exploding player's measure"), RowMeasure)
		|| !TestNotNull(TEXT("set-up: the exploding player's doomed enemy"), RowDoomed)
		|| !TestNotNull(TEXT("set-up: the exploding player's bystander"), RowBystander))
	{
		return false;
	}
	ACataclysmMinion* PlainSecond = NearbyTrapsPlace(*this, Plain, Second);
	ACataclysmMinion* RowSecond = NearbyTrapsPlace(*this, Exploding, RowHome + Second);
	if (!PlainSecond || !RowSecond)
	{
		return false;
	}

	// THE CONTROL, READ FIRST: THE CHAINED TRAP'S LASH KILLS, AND WITH NO ROW NOTHING EXPLODES.
	NearbyTrapsStep(PlainFirst);
	TestTrue(TEXT("control: the chained trap's lash killed the doomed enemy"), UCataclysmSkillEffects::IsDead(PlainDoomed));
	TestEqual(TEXT("control: and with no row its bystander 3.5 m away is untouched"), NearbyTrapsTakenFrom(PlainBystander),
		0.0f, 0.01f);

	// THE ROW: THE CHAINED TRAP'S KILL EXPLODES FOR HALF THE KILLING LASH AS SENT.
	NearbyTrapsStep(RowFirst);
	const float LashTook = NearbyTrapsTakenFrom(RowMeasure);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the first trap's lash struck the measure: %.2f"), LashTook), LashTook > 1.0f))
	{
		return false;
	}
	TestTrue(TEXT("the chained trap's lash killed the doomed enemy"), UCataclysmSkillEffects::IsDead(RowDoomed));
	const float LashAsSent = LashTook / (1.0f + UCataclysmDamageCalculation::SubtypeBonus / 100.0f);
	const float Expected = LashAsSent * 0.5f;
	TestEqual(*FString::Printf(TEXT("and that kill explodes: its bystander 3.5 m away takes half the killing lash as sent, "
									"before the Slashing step the measure lost it with (G-1 (a)): %.2f against %.2f"),
							   NearbyTrapsTakenFrom(RowBystander), Expected),
		NearbyTrapsTakenFrom(RowBystander), Expected, Expected * 0.001f);
	return true;
}

// ==========================================================================
// 44. At the player's death the traps' blows answer a hit_dealt row, and the gadget half's still do not (G-1 (a), H8)
// ==========================================================================

CATACLYSM_TEST(FCataclysmNearbyTrapsDeathTrapsAnswerHitDealtTest,
	"Cataclysm.Traps.AtThePlayersDeathATrapsBlowAnswersAHitDealtRowWhereTheGadgetHalfsDoesNot")
{
	using namespace CataclysmNearbyTrapsTest;

	// TWO PLAYERS 100 M APART, EACH WITH THE HEAVY TRAP FLAG AND THE ULTIMATE GADGET FLAG: a trap 20 m north, armed, its
	// enemy 2.5 m east; a skill gadget 40 m east with a sturdy enemy 3 m east; a killer 3 m west. Both hold the gadget
	// half's row and a row of their own granting one stack on `hit_dealt`; the wearer also the trap half's row. The control
	// shows the gadget half's death cast, made inside the death's row action, answering no `hit_dealt` row (#15 as built).
	FNearbyTrapsWorld Scope;
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
	FNearbyTrapsRig Plain(World, FVector::ZeroVector);
	FNearbyTrapsRig Wearer(World, WearerHome);
	if (!Plain.IsUsable(*this) || !Wearer.IsUsable(*this))
	{
		return false;
	}
	FCataclysmPoolAction GadgetHalf;
	GadgetHalf.Event = FName(TEXT("player_death"));
	GadgetHalf.Pool = FName(UCataclysmAbilitySystemComponent::GadgetsCastNowAction);
	GadgetHalf.Percent = 100.0f;
	GadgetHalf.bGadgetsCastNow = true;
	GadgetHalf.TriggerKey = FName(TEXT("NearbyTrapsTest:gadgets_cast_now:player_death"));
	const FName HitKey(TEXT("NearbyTrapsTest:HitDealtStacks"));
	FCataclysmPoolAction OnHit;
	OnHit.Event = FName(TEXT("hit_dealt"));
	OnHit.StackKey = HitKey;
	OnHit.StackSeconds = 10.0f;
	OnHit.StackCap = 10;
	OnHit.TriggerKey = FName(TEXT("NearbyTrapsTest:HitDealtStacks:hit_dealt"));
	for (const FNearbyTrapsRig* Each : {&Plain, &Wearer})
	{
		Each->Holds({NearbyTrapsHeavyFlag(), NearbyTrapsUltimateGadgetFlag()});
	}
	Plain.System->SetPoolActions({GadgetHalf, OnHit});
	Wearer.System->SetPoolActions({GadgetHalf, NearbyTrapsRow(TEXT("player_death"), /*bArms=*/false, 0.0f,
															  /*bNearestOnly=*/false, nullptr), OnHit});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector Gadget(40.0f * M, 0.0f, 0.0f);
	ACataclysmMinion* PlainTrap = NearbyTrapsPlace(*this, Plain, North);
	ACataclysmMinion* WearerTrap = NearbyTrapsPlace(*this, Wearer, WearerHome + North);
	if (!PlainTrap || !WearerTrap)
	{
		return false;
	}
	NearbyTrapsPass(World, 1.25f);
	NearbyTrapsStep(PlainTrap);
	NearbyTrapsStep(WearerTrap);
	ACataclysmMinion* PlainGadget = NearbyTrapsPlaceGadget(*this, Plain, Gadget);
	ACataclysmMinion* WearerGadget = NearbyTrapsPlaceGadget(*this, Wearer, WearerHome + Gadget);
	if (!PlainGadget || !WearerGadget
		|| !TestTrue(TEXT("set-up: each player's trap is armed"), PlainTrap->bSkillTrapArmed && WearerTrap->bSkillTrapArmed))
	{
		return false;
	}
	constexpr float Sturdy = 100000.0f;
	const FVector East(2.5f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* PlainTrapsEnemy = NearbyTrapsEnemy(World, North + East);
	ACataclysmEnemyCharacter* WearerTrapsEnemy = NearbyTrapsEnemy(World, WearerHome + North + East);
	ACataclysmEnemyCharacter* PlainGadgetsEnemy = NearbyTrapsEnemy(World, Gadget + FVector(3.0f * M, 0.0f, 0.0f), Sturdy);
	ACataclysmEnemyCharacter* WearerGadgetsEnemy =
		NearbyTrapsEnemy(World, WearerHome + Gadget + FVector(3.0f * M, 0.0f, 0.0f), Sturdy);
	ACataclysmEnemyCharacter* PlainKiller = NearbyTrapsEnemy(World, FVector(-3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* WearerKiller = NearbyTrapsEnemy(World, WearerHome + FVector(-3.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the control's trap's enemy"), PlainTrapsEnemy)
		|| !TestNotNull(TEXT("set-up: the wearer's trap's enemy"), WearerTrapsEnemy)
		|| !TestNotNull(TEXT("set-up: the control's gadget's enemy"), PlainGadgetsEnemy)
		|| !TestNotNull(TEXT("set-up: the wearer's gadget's enemy"), WearerGadgetsEnemy)
		|| !TestNotNull(TEXT("set-up: the control's killer"), PlainKiller)
		|| !TestNotNull(TEXT("set-up: the wearer's killer"), WearerKiller))
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> WearerTrapWatched(WearerTrap);

	// THE CONTROL, READ FIRST: AT ITS DEATH ITS GADGET'S CAST STRIKES AND ANSWERS NO hit_dealt ROW.
	TestEqual(TEXT("control: before the death, no stack"), Plain.System->OwnStacksHeld(HitKey), 0);
	if (!TestTrue(TEXT("control: the player holding the gadget half died"), Plain.DiesTo(PlainKiller)))
	{
		return false;
	}
	TestEqual(TEXT("control: its gadget cast once at the death"), PlainGadget->SkillGadgetDeathCastsMade, 1);
	TestTrue(*FString::Printf(TEXT("control: that cast struck its enemy: %.2f"), NearbyTrapsTakenFrom(PlainGadgetsEnemy, Sturdy)),
		NearbyTrapsTakenFrom(PlainGadgetsEnemy, Sturdy) > 1.0f);
	TestEqual(TEXT("control: and its blow, made inside the death's row action, granted no hit_dealt stack"),
		Plain.System->OwnStacksHeld(HitKey), 0);

	// THE WEARER: ITS TRAP GOES OFF AT THE DEATH, A WHOLE TRIGGER, AND ITS BLOW ANSWERS THE hit_dealt ROW.
	if (!TestTrue(TEXT("the wearer died"), Wearer.DiesTo(WearerKiller)))
	{
		return false;
	}
	TestTrue(TEXT("its trap went off at the death and was used up"), NearbyTrapsGone(WearerTrapWatched));
	TestTrue(*FString::Printf(TEXT("its cast struck its enemy: %.2f"), NearbyTrapsTakenFrom(WearerTrapsEnemy)),
		NearbyTrapsTakenFrom(WearerTrapsEnemy) > 1.0f);
	TestEqual(TEXT("one hit_dealt stack: the trap's blow answered the row (G-1 (a)), and the gadget's did not"),
		Wearer.System->OwnStacksHeld(HitKey), 1);
	TestEqual(TEXT("its gadget cast once at the death, the gadget half unchanged"), WearerGadget->SkillGadgetDeathCastsMade, 1);
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
