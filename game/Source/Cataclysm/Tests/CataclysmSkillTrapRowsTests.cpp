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
#include "Interface/CataclysmSkillBar.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Skill traps, layer 3: the eight trap rows on what layers 1 and 2 built. Ruled 2026-10-21 by the coordinating session
 * (M1-M6), on top of T1-T19 and L1-L6, for "Traps arm 30%-60% faster", "Traps have 30%-50% increased trigger radius",
 * "Your trap skills get +1-6 to max trap count", "Traps do not expire, they remain armed until triggered", "Traps apply
 * a 20%-35% slow to triggered enemies for 2-4 seconds", "Traps apply 1-2 bleed stacks to all enemies caught in their
 * blast", "Each trap triggered in the last 10 seconds increases your damage by 8%-15%" and "Placing a trap generates a
 * burst of evasion, granting 10%-20% evasion chance for 3 seconds". In layer 1's group, `Cataclysm.Traps.`, whose rig
 * this follows, in a file of its own.
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER HOLDING A DEMONIC WHIP, given a weapon damage of 100, 10,000 health and 1,000
 * mana by hand: Heavy Searing Lash, a 5 m cone; Ultimate The Gathering, a 14 m point-blank strike. NO ROW IS AUTHORED, SO
 * EVERY STAT AND EVERY ACTION IS WRITTEN BY HAND, as the item would build it from the row this layer gives the
 * enchantment session: a stat line per call, replacing the last, and the actions by `SetPoolActions`. NO KEY IS PRESSED:
 * every trap is placed by `ACataclysmMinion::PlaceSkillTrap`, which is free, and a trap's cast is a free start. Every
 * body stands at least 1 m from every other, on a real side. NOTHING HERE WAITS ON A TICK: the world's clock is moved by
 * hand and a trap's step is called directly, a second and a quarter after placing. Blows are struck only with the
 * critical strike roll pinned, after the world is made. Helper types and functions are named after the subject, the
 * trap row.
 */
namespace CataclysmTrapRowTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The health every creature here starts with, unless a case says otherwise. */
	constexpr float TrapRowEnemyHealth = 50000.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FTrapRowWorld
	{
		FTrapRowWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FTrapRowWorld()
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
	ACataclysmEnemyCharacter* TrapRowEnemy(UWorld* World, const FVector& Where, float Health = TrapRowEnemyHealth)
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

	float TrapRowHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	float TrapRowMaxHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) : -1.0f;
	}

	/** What a creature has lost since it was made at `Health`. */
	float TrapRowTakenFrom(const AActor* Who, float Health = TrapRowEnemyHealth)
	{
		return Health - TrapRowHealthOf(Who);
	}

	/** Whether `Who` carries `Tag` now. */
	bool TrapRowCarries(const AActor* Who, const FGameplayTag& Tag)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its && Tag.IsValid() && Its->HasMatchingGameplayTag(Tag);
	}

	/** The longest time left on anything granting `Tag` on `Who`, or nought. */
	float TrapRowSecondsLeftOn(const AActor* Who, const FGameplayTag& Tag)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		float Longest = 0.0f;
		if (Its && Tag.IsValid())
		{
			for (const float Seconds : Its->GetActiveEffectsTimeRemaining(
					 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag))))
			{
				Longest = FMath::Max(Longest, Seconds);
			}
		}
		return Longest;
	}

	/** One row of a stat line a test's player holds, with the own-stack key when it is scaled by its own stacks. */
	struct FTrapRowStat
	{
		FName Stat;
		float Value = 0.0f;
		ECataclysmStatBucket Bucket = ECataclysmStatBucket::Flat;
		/** The row's one required tag, or null for none. */
		const TCHAR* RequiredTag = nullptr;
		/** Set, the row is `own_stacks` with a step of 1 under this key. */
		FName OwnStackKey;
	};

	/** A slot's flag: a flat 1 on `skill_slot_is_a_trap` requiring the slot's tag, layers 1 and 2's row. */
	FTrapRowStat TrapRowSlotFlag(const TCHAR* SlotTag)
	{
		return {FName(UCataclysmAbilitySystemComponent::SkillSlotIsATrapStat), 1.0f, ECataclysmStatBucket::Flat, SlotTag};
	}

	/** A flat `Value` of `Stat` requiring `Type.Trap`, as this layer's two flat rows are written. */
	FTrapRowStat TrapRowFlat(const TCHAR* Stat, float Value)
	{
		return {FName(Stat), Value, ECataclysmStatBucket::Flat, TEXT("Type.Trap")};
	}

	/** An increase of `Stat` requiring `Type.Trap`, as the arming and radius rows are written. */
	FTrapRowStat TrapRowIncrease(const TCHAR* Stat, float Value)
	{
		return {FName(Stat), Value, ECataclysmStatBucket::Increased, TEXT("Type.Trap")};
	}

	/** A possessed player character holding a Demonic Whip, its ability system and its weapon slots. */
	struct FTrapRowRig
	{
		FTrapRowRig(UWorld* World, const FVector& Where)
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
			const auto HoldsARowFor = [this](ECataclysmAbilitySlot Slot)
			{
				return Slots && Slots->GetAvailableSkills().FindByPredicate(
					[Slot](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == Slot; }) != nullptr;
			};
			if (!Player || !System || !Slots || !HoldsARowFor(ECataclysmAbilitySlot::Heavy)
				|| !HoldsARowFor(ECataclysmAbilitySlot::Ultimate))
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

		/** REPLACES THE WHOLE STAT LINE with these rows. */
		void Holds(const TArray<FTrapRowStat>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FTrapRowStat& Each : Rows)
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
				if (!Each.OwnStackKey.IsNone())
				{
					Row.Scale = ECataclysmStatScale::PerOwnStack;
					Row.ScaleStep = 1.0f;
					Row.StackKey = Each.OwnStackKey;
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

	/** A skill trap for this rig's `Slot` at `Where`, or null with the reason reported. */
	ACataclysmMinion* TrapRowPlace(FAutomationTestBase& Test, const FTrapRowRig& Rig, ECataclysmAbilitySlot Slot,
								   const FVector& Where)
	{
		ACataclysmMinion* Made = ACataclysmMinion::PlaceSkillTrap(Rig.Player, Slot, Where);
		if (!Made)
		{
			Test.AddError(TEXT("set-up: no skill trap was placed. DT_MinionTypes needs the SkillTrap row: run "
							   "tools/generate_datatable_assets.py"));
		}
		return Made;
	}

	/** Move the world's clock forward by hand. No timer fires and nobody thinks. */
	void TrapRowPass(UWorld* World, float Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/** Run a trap's quarter-second step, as `RegenerationStep` calls it. */
	void TrapRowStep(ACataclysmMinion* Trap)
	{
		if (IsValid(Trap))
		{
			ACataclysmMinion::GadgetStep(Trap);
		}
	}

	/** Whether a trap is gone: destroyed, or on its way out. */
	bool TrapRowGone(const TWeakObjectPtr<ACataclysmMinion>& Trap)
	{
		return !Trap.IsValid() || Trap->IsActorBeingDestroyed();
	}

	/** The skill bar's box for `Slot`, or an empty box with `None` for its slot. */
	FCataclysmSkillBarSlot TrapRowBox(const AActor* Player, ECataclysmAbilitySlot Slot)
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

	/** An action of a test's own on `Event`, requiring `Type.Trap`, with no wait between firings. */
	FCataclysmPoolAction TrapRowAction(const TCHAR* Event, const TCHAR* Key)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(Event);
		Action.Pool = FName(Key);
		Action.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false));
		Action.TriggerKey = FName(*FString::Printf(TEXT("TrapRowTest:%s:%s"), Event, Key));
		Action.TriggerCooldownSeconds = 0.0f;
		return Action;
	}

	/**
	 * The grant of an own-stack row on `Event`, as the item builds it from a row: the key, the window and the cap, and
	 * whether its stacks lapse alone (a cap of nought). An own stack's tags scope its stat and not its grant.
	 */
	FCataclysmPoolAction TrapRowOwnStackGrant(const TCHAR* Event, FName Key, float WindowSeconds, int32 Cap)
	{
		FCataclysmPoolAction Grant;
		Grant.Event = FName(Event);
		Grant.StackKey = Key;
		Grant.StackSeconds = WindowSeconds;
		Grant.StackCap = Cap;
		Grant.bOwnStacksLapseAlone = Cap <= 0;
		Grant.TriggerKey = FName(*FString::Printf(TEXT("%s:%s"), *Key.ToString(), Event));
		return Grant;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 18. The cap bonus raises the one cap, and the bar shows it (M1)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapRowCapBonusTest,
	"Cataclysm.Traps.TheCapBonusRaisesTheOneCapSoASixthTrapStandsAndTheBarShowsTrapsSixOfEight")
{
	using namespace CataclysmTrapRowTest;

	// TWO PLAYERS 100 M APART, EACH PLACING HEAVY TRAPS 10 M APART ALONG Y: the control holds the Heavy flag alone, the
	// other the flag and a flat 3 on trap_cap_bonus requiring Type.Trap ("Your trap skills get +1-6 to max trap count"
	// rolled at 3). No enemy and no step, so nothing triggers.
	FTrapRowWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector BonusHome(100.0f * M, 0.0f, 0.0f);
	FTrapRowRig Plain(World, FVector::ZeroVector);
	FTrapRowRig Bonus(World, BonusHome);
	if (!Plain.IsUsable(*this) || !Bonus.IsUsable(*this))
	{
		return false;
	}
	Plain.Holds({TrapRowSlotFlag(TEXT("Slot.Heavy"))});
	Bonus.Holds({TrapRowSlotFlag(TEXT("Slot.Heavy")), TrapRowFlat(ACataclysmMinion::TrapCapBonusStat, 3.0f)});
	const auto At = [](int32 Index) { return FVector(0.0f, 10.0f * M * static_cast<float>(Index), 0.0f); };

	// THE CONTROL, READ FIRST: FIVE AND NO MORE.
	TestEqual(TEXT("control: with no cap bonus the most traps standing is five (T6)"),
		ACataclysmMinion::SkillTrapsMostFor(Plain.Player), 5);
	TArray<ACataclysmMinion*> PlainTraps;
	TArray<ACataclysmMinion*> BonusTraps;
	for (int32 Index = 1; Index <= 6; ++Index)
	{
		ACataclysmMinion* PlainMade = TrapRowPlace(*this, Plain, ECataclysmAbilitySlot::Heavy, At(Index));
		ACataclysmMinion* BonusMade = TrapRowPlace(*this, Bonus, ECataclysmAbilitySlot::Heavy, BonusHome + At(Index));
		if (!PlainMade || !BonusMade)
		{
			return false;
		}
		PlainTraps.Add(PlainMade);
		BonusTraps.Add(BonusMade);
	}
	const TWeakObjectPtr<ACataclysmMinion> PlainFirst(PlainTraps[0]);
	const TWeakObjectPtr<ACataclysmMinion> BonusFirst(BonusTraps[0]);
	TestEqual(TEXT("control: with a sixth placed, five stand"), ACataclysmMinion::SkillTrapsOf(Plain.Player).Num(), 5);
	TestTrue(TEXT("control: and the first is gone"), TrapRowGone(PlainFirst));
	TestEqual(TEXT("control: the box reads five of five"),
		UCataclysmSkillBar::SkillTrapTextFor(TrapRowBox(Plain.Player, ECataclysmAbilitySlot::Heavy)),
		FString(TEXT("Traps 5/5")));

	// THE BONUS: ONE CAP ACROSS EVERY SLOT, FIVE AND THREE (M1 (a)).
	TestEqual(TEXT("with a cap bonus of 3 the most traps standing is eight"), ACataclysmMinion::SkillTrapsMostFor(Bonus.Player),
		8);
	TestEqual(TEXT("so with a sixth placed, six stand"), ACataclysmMinion::SkillTrapsOf(Bonus.Player).Num(), 6);
	TestFalse(TEXT("and the first still stands"), TrapRowGone(BonusFirst));
	TestEqual(TEXT("and the box reads six of eight"),
		UCataclysmSkillBar::SkillTrapTextFor(TrapRowBox(Bonus.Player, ECataclysmAbilitySlot::Heavy)),
		FString(TEXT("Traps 6/8")));

	// AND PAST THE RAISED CAP THE OLDEST STILL GOES (T6).
	for (int32 Index = 7; Index <= 9; ++Index)
	{
		if (!TrapRowPlace(*this, Bonus, ECataclysmAbilitySlot::Heavy, BonusHome + At(Index)))
		{
			return false;
		}
	}
	TestEqual(TEXT("with a ninth placed, eight stand"), ACataclysmMinion::SkillTrapsOf(Bonus.Player).Num(), 8);
	TestTrue(TEXT("and the first is gone, without triggering"), TrapRowGone(BonusFirst));
	return true;
}

// ==========================================================================
// 19. The no-expire flag: no life span, taken at placing; the cap still holds (M2)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapRowNoExpiryTest,
	"Cataclysm.Traps.TheNoExpireFlagKeepsATrapArmedPastTwentySecondsAndWithoutItATrapExpires")
{
	using namespace CataclysmTrapRowTest;

	// TWO PLAYERS 100 M APART, EACH WITH A HEAVY TRAP 20 M NORTH AND AN ENEMY 2.5 M EAST OF IT: the control holds the
	// Heavy flag alone, the other the flag and traps_do_not_expire at a flat 1 requiring Type.Trap. Each trap is placed
	// with its enemy already beside it, and nobody steps until the times below.
	FTrapRowWorld Scope;
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
	const FVector NeverHome(100.0f * M, 0.0f, 0.0f);
	FTrapRowRig Plain(World, FVector::ZeroVector);
	FTrapRowRig Never(World, NeverHome);
	if (!Plain.IsUsable(*this) || !Never.IsUsable(*this))
	{
		return false;
	}
	const FTrapRowStat HeavyFlag = TrapRowSlotFlag(TEXT("Slot.Heavy"));
	Plain.Holds({HeavyFlag});
	Never.Holds({HeavyFlag});
	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector East(2.5f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* PlainEnemy = TrapRowEnemy(World, North + East);
	ACataclysmEnemyCharacter* NeverEnemy = TrapRowEnemy(World, NeverHome + North + East);
	if (!TestNotNull(TEXT("set-up: the control's enemy"), PlainEnemy)
		|| !TestNotNull(TEXT("set-up: the other player's enemy"), NeverEnemy))
	{
		return false;
	}

	// TAKEN AT PLACING: the other player's first trap is placed BEFORE it holds the flag, 40 m south, with no enemy.
	ACataclysmMinion* Before =
		TrapRowPlace(*this, Never, ECataclysmAbilitySlot::Heavy, NeverHome - FVector(0.0f, 40.0f * M, 0.0f));
	Never.Holds({HeavyFlag, TrapRowFlat(ACataclysmMinion::TrapsDoNotExpireStat, 1.0f)});
	ACataclysmMinion* PlainTrap = TrapRowPlace(*this, Plain, ECataclysmAbilitySlot::Heavy, North);
	ACataclysmMinion* NeverTrap = TrapRowPlace(*this, Never, ECataclysmAbilitySlot::Heavy, NeverHome + North);
	if (!Before || !PlainTrap || !NeverTrap)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> PlainWatched(PlainTrap);
	const TWeakObjectPtr<ACataclysmMinion> NeverWatched(NeverTrap);

	// THE CONTROL, READ FIRST: 20 S, AND THE END OF IT ENDS THE TRAP.
	TestEqual(TEXT("control: without the flag a trap lasts 20 s (T4)"), PlainTrap->GetLifeSpan(),
		ACataclysmMinion::SkillTrapSeconds, 0.01f);
	TestEqual(TEXT("and so does the trap placed before the flag was held: it is taken at placing"), Before->GetLifeSpan(),
		ACataclysmMinion::SkillTrapSeconds, 0.01f);
	TestEqual(TEXT("with the flag a trap has no life span (M2 (a))"), NeverTrap->GetLifeSpan(), 0.0f, 0.001f);

	// 25 S ON. A test world's timers never fire, so the control's trap is ended as the engine's lifespan timer ends it.
	TrapRowPass(World, 25.0f);
	PlainTrap->LifeSpanExpired();
	TestTrue(TEXT("control: past its 20 s the control's trap has expired"), TrapRowGone(PlainWatched));
	TestEqual(TEXT("control: and did not trigger: its enemy is untouched (T4)"), TrapRowHealthOf(PlainEnemy),
		TrapRowEnemyHealth, 0.001f);
	TestFalse(TEXT("25 s on, the flagged trap still stands"), TrapRowGone(NeverWatched));
	TrapRowStep(NeverTrap);
	TestTrue(*FString::Printf(TEXT("and is armed and triggers on its enemy: it took %.2f"), TrapRowTakenFrom(NeverEnemy)),
		TrapRowTakenFrom(NeverEnemy) > 0.0f);
	TestTrue(TEXT("and is used up, which is what ends it now"), TrapRowGone(NeverWatched));

	// AND THE CAP STILL DESTROYS THE OLDEST (M2 (a)): six more flagged traps with no enemy, beside the one placed before.
	TArray<ACataclysmMinion*> Six;
	for (int32 Index = 1; Index <= 6; ++Index)
	{
		ACataclysmMinion* Made = TrapRowPlace(*this, Never, ECataclysmAbilitySlot::Heavy,
			NeverHome - FVector(0.0f, (40.0f + 10.0f * static_cast<float>(Index)) * M, 0.0f));
		if (!Made)
		{
			return false;
		}
		Six.Add(Made);
	}
	const TWeakObjectPtr<ACataclysmMinion> FirstOfSix(Six[0]);
	TestEqual(TEXT("with the flag five still stand at most"), ACataclysmMinion::SkillTrapsOf(Never.Player).Num(), 5);
	TestTrue(TEXT("the oldest flagged one is gone"), TrapRowGone(FirstOfSix));
	return true;
}

// ==========================================================================
// 20. The arming and trigger radius rows as written, at their low ends (P319, P320)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapRowArmingAndRadiusTest,
	"Cataclysm.Traps.TheArmingAndTriggerRadiusRowsAtThirtyPerCentArmATrapSoonerAndTriggerItFurtherOut")
{
	using namespace CataclysmTrapRowTest;

	// THREE PLAYERS 100 M APART, EACH WITH A HEAVY TRAP 20 M NORTH AND AN ENEMY 3.75 M EAST OF IT: the control holds the
	// flag alone, the quick one "Traps arm 30%-60% faster" and the wide one "Traps have 30%-50% increased trigger radius",
	// each as its row is written (increased, requiring Type.Trap), rolled at 30.
	FTrapRowWorld Scope;
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
	const FVector QuickHome(100.0f * M, 0.0f, 0.0f);
	const FVector WideHome(200.0f * M, 0.0f, 0.0f);
	FTrapRowRig Plain(World, FVector::ZeroVector);
	FTrapRowRig Quick(World, QuickHome);
	FTrapRowRig Wide(World, WideHome);
	if (!Plain.IsUsable(*this) || !Quick.IsUsable(*this) || !Wide.IsUsable(*this))
	{
		return false;
	}
	const FTrapRowStat HeavyFlag = TrapRowSlotFlag(TEXT("Slot.Heavy"));
	Plain.Holds({HeavyFlag});
	Quick.Holds({HeavyFlag, TrapRowIncrease(ACataclysmMinion::TrapArmingSpeedStat, 30.0f)});
	Wide.Holds({HeavyFlag, TrapRowIncrease(ACataclysmMinion::TrapTriggerRadiusStat, 30.0f)});
	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector East(3.75f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* PlainEnemy = TrapRowEnemy(World, North + East);
	ACataclysmEnemyCharacter* WideEnemy = TrapRowEnemy(World, WideHome + North + East);
	if (!TestNotNull(TEXT("set-up: the control's enemy"), PlainEnemy)
		|| !TestNotNull(TEXT("set-up: the wide player's enemy"), WideEnemy))
	{
		return false;
	}
	ACataclysmMinion* PlainTrap = TrapRowPlace(*this, Plain, ECataclysmAbilitySlot::Heavy, North);
	ACataclysmMinion* QuickTrap = TrapRowPlace(*this, Quick, ECataclysmAbilitySlot::Heavy, QuickHome + North);
	ACataclysmMinion* WideTrap = TrapRowPlace(*this, Wide, ECataclysmAbilitySlot::Heavy, WideHome + North);
	if (!PlainTrap || !QuickTrap || !WideTrap)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> WideWatched(WideTrap);

	// THE FIGURES, THE CONTROL FIRST.
	TestEqual(TEXT("control: the plain trap arms at an age of 1 s (T1)"), PlainTrap->SkillTrapArmsAtAgeSeconds, 1.0f,
		0.001f);
	TestEqual(TEXT("30% faster arms at 1/1.3 of a second"), QuickTrap->SkillTrapArmsAtAgeSeconds, 1.0f / 1.3f, 0.001f);
	TestEqual(TEXT("control: the plain trap triggers within 3 m (T2)"),
		ACataclysmMinion::SkillTrapTriggerRadiusCmFor(Plain.Player, PlainTrap->TypeTags), 300.0f, 0.01f);
	TestEqual(TEXT("30% wider triggers within 3.9 m"),
		ACataclysmMinion::SkillTrapTriggerRadiusCmFor(Wide.Player, WideTrap->TypeTags), 390.0f, 0.01f);

	// SEVEN EIGHTHS OF A SECOND IN, between the two arming marks.
	TrapRowPass(World, 0.875f);
	TrapRowStep(PlainTrap);
	TrapRowStep(QuickTrap);
	TestFalse(TEXT("control: 0.875 s in, the plain trap is not armed"), PlainTrap->bSkillTrapArmed);
	TestTrue(TEXT("the quick trap is"), QuickTrap->bSkillTrapArmed);

	// A SECOND AND A QUARTER IN, all armed.
	TrapRowPass(World, 0.375f);
	TrapRowStep(PlainTrap);
	TrapRowStep(WideTrap);
	TestEqual(TEXT("control: the plain trap does not trigger on its enemy 3.75 m away"), TrapRowHealthOf(PlainEnemy),
		TrapRowEnemyHealth, 0.001f);
	TestTrue(*FString::Printf(TEXT("the wide trap triggers on its enemy 3.75 m away: it took %.2f"),
							  TrapRowTakenFrom(WideEnemy)),
		TrapRowTakenFrom(WideEnemy) > 0.0f);
	TestTrue(TEXT("and was used up"), TrapRowGone(WideWatched));
	return true;
}

// ==========================================================================
// 21. The slow: only the enemy that set the trap off, and only when the cast took a tenth of it (M3 (b), R1)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapRowSlowTest,
	"Cataclysm.Traps.TheSlowRowSlowsOnlyTheEnemyThatSetTheTrapOffByItsStrengthForItsSecondsWhenTheCastTookATenth")
{
	using namespace CataclysmTrapRowTest;

	// THREE PLAYERS 100 M APART, EACH WITH A HEAVY TRAP 20 M NORTH, THE ENEMY THAT SETS IT OFF 2.5 M EAST OF IT AND A
	// SECOND 4 M EAST OF IT, OUTSIDE ITS 3 M AND INSIDE SEARING LASH'S 5 M CONE, SO THE ONE CAST STRIKES BOTH. The control
	// holds the Heavy flag alone; the other two also hold "Traps apply a 20%-35% slow to triggered enemies for 2-4 seconds"
	// as the item builds it from its row (action `slow_for_seconds` on `trap_triggered`, requiring Type.Trap, the strength
	// the value and the seconds Stack Seconds), rolled at 25 and 3. The control's and the second player's creatures hold
	// 1,500 health, so the lash's blow takes more than a tenth of it; the third player's 100,000, so it takes far less (the
	// owner, 2026-10-21: "Follow the tenth rule").
	FTrapRowWorld Scope;
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
	constexpr float Frail = 1500.0f;
	constexpr float Sturdy = 100000.0f;
	const FVector SlowHome(100.0f * M, 0.0f, 0.0f);
	const FVector GlancingHome(200.0f * M, 0.0f, 0.0f);
	FTrapRowRig Plain(World, FVector::ZeroVector);
	FTrapRowRig Slow(World, SlowHome);
	FTrapRowRig Glancing(World, GlancingHome);
	if (!Plain.IsUsable(*this) || !Slow.IsUsable(*this) || !Glancing.IsUsable(*this))
	{
		return false;
	}
	for (const FTrapRowRig* Each : {&Plain, &Slow, &Glancing})
	{
		Each->Holds({TrapRowSlotFlag(TEXT("Slot.Heavy"))});
	}
	FCataclysmPoolAction Slowing = TrapRowAction(TEXT("trap_triggered"), UCataclysmAbilitySystemComponent::SlowForSecondsAction);
	Slowing.bSlowForSeconds = true;
	Slowing.Percent = 25.0f;
	Slowing.StackSeconds = 3.0f;
	Slow.System->SetPoolActions({Slowing});
	Glancing.System->SetPoolActions({Slowing});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector Near(2.5f * M, 0.0f, 0.0f);
	const FVector Behind(4.0f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* PlainSetter = TrapRowEnemy(World, North + Near, Frail);
	ACataclysmEnemyCharacter* PlainOther = TrapRowEnemy(World, North + Behind, Frail);
	ACataclysmEnemyCharacter* SlowSetter = TrapRowEnemy(World, SlowHome + North + Near, Frail);
	ACataclysmEnemyCharacter* SlowOther = TrapRowEnemy(World, SlowHome + North + Behind, Frail);
	ACataclysmEnemyCharacter* GlancingSetter = TrapRowEnemy(World, GlancingHome + North + Near, Sturdy);
	if (!TestNotNull(TEXT("set-up: the control's enemy that sets its trap off"), PlainSetter)
		|| !TestNotNull(TEXT("set-up: the control's second enemy"), PlainOther)
		|| !TestNotNull(TEXT("set-up: the second player's enemy that sets its trap off"), SlowSetter)
		|| !TestNotNull(TEXT("set-up: the second player's second enemy"), SlowOther)
		|| !TestNotNull(TEXT("set-up: the third player's sturdy enemy"), GlancingSetter))
	{
		return false;
	}
	ACataclysmMinion* PlainTrap = TrapRowPlace(*this, Plain, ECataclysmAbilitySlot::Heavy, North);
	ACataclysmMinion* SlowTrap = TrapRowPlace(*this, Slow, ECataclysmAbilitySlot::Heavy, SlowHome + North);
	ACataclysmMinion* GlancingTrap = TrapRowPlace(*this, Glancing, ECataclysmAbilitySlot::Heavy, GlancingHome + North);
	if (!PlainTrap || !SlowTrap || !GlancingTrap)
	{
		return false;
	}

	// THE CONTROL, READ FIRST: THE CAST STRIKES BOTH, EACH FOR A TENTH OR MORE, AND LAYS NO SLOW OF ITS OWN.
	TrapRowPass(World, 1.25f);
	TrapRowStep(PlainTrap);
	TestTrue(*FString::Printf(TEXT("set-up: the control's cast took a tenth or more of the enemy that set it off (%.2f) and "
								   "of the second (%.2f)"), TrapRowTakenFrom(PlainSetter, Frail), TrapRowTakenFrom(PlainOther, Frail)),
		TrapRowTakenFrom(PlainSetter, Frail) >= Frail / 10.0f && TrapRowTakenFrom(PlainOther, Frail) >= Frail / 10.0f);
	TestFalse(TEXT("control: the cast itself cripples nothing: not the enemy that set it off"),
		TrapRowCarries(PlainSetter, Cripple));
	TestFalse(TEXT("control: nor the second"), TrapRowCarries(PlainOther, Cripple));

	// THE ROW, ON A CAST THAT TOOK A TENTH.
	TrapRowStep(SlowTrap);
	TestTrue(*FString::Printf(TEXT("set-up: this cast took a tenth or more of the enemy that set it off (%.2f) and of the "
								   "second (%.2f)"), TrapRowTakenFrom(SlowSetter, Frail), TrapRowTakenFrom(SlowOther, Frail)),
		TrapRowTakenFrom(SlowSetter, Frail) >= Frail / 10.0f && TrapRowTakenFrom(SlowOther, Frail) >= Frail / 10.0f);
	TestTrue(TEXT("the enemy that set the trap off, struck for a tenth, is slowed (M3 (b), R1)"),
		TrapRowCarries(SlowSetter, Cripple));
	TestEqual(TEXT("by the row's 25 per cent, below Cripple's own 30"),
		UCataclysmSkillEffects::StatedStrengthOn(SlowSetter, Cripple), 25.0f, 0.01f);
	TestEqual(TEXT("for the row's 3 seconds"), TrapRowSecondsLeftOn(SlowSetter, Cripple), 3.0f, 0.01f);
	TestFalse(TEXT("and the second enemy, struck for a tenth by the same cast, is not: \"triggered enemies\" is the one "
				   "that set it off"),
		TrapRowCarries(SlowOther, Cripple));

	// AND ON A CAST THAT TOOK LESS THAN A TENTH: NO SLOW (the owner, 2026-10-21).
	TrapRowStep(GlancingTrap);
	TestTrue(*FString::Printf(TEXT("set-up: the third cast struck its enemy for less than a tenth: %.2f of 100,000"),
							  TrapRowTakenFrom(GlancingSetter, Sturdy)),
		TrapRowTakenFrom(GlancingSetter, Sturdy) > 0.0f && TrapRowTakenFrom(GlancingSetter, Sturdy) < Sturdy / 10.0f);
	TestFalse(TEXT("so the enemy that set it off is not slowed: the rule of a tenth"), TrapRowCarries(GlancingSetter, Cripple));
	return true;
}

// ==========================================================================
// 22. The bleed: every enemy the cast strikes, at its size, under the rule of a tenth (P215)
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapRowBleedTest,
	"Cataclysm.Traps.TheBleedRowBleedsEveryEnemyATrapsCastStrikesAtItsSizeWhenTheBlowTookATenth")
{
	using namespace CataclysmTrapRowTest;

	// FOUR PLAYERS 100 M APART, EACH WITH A HEAVY TRAP 20 M NORTH, THE ENEMY THAT SETS IT OFF 2.5 M EAST OF IT AND A
	// SECOND 4 M EAST OF IT, OUTSIDE ITS 3 M AND INSIDE SEARING LASH'S 5 M CONE. The creatures hold 1,500 health, so the
	// lash's blow takes more than a tenth of it (the owner's rule of #917). The control holds the Heavy flag alone;
	// the next two also hold "Traps apply 1-2 bleed stacks to all enemies caught in their blast" as the item builds it
	// from its row (action `apply_status_sized` on `deployable_hit`, requiring Type.Trap, Ailment Bleed, no wait),
	// rolled at 1 and at 2; the fourth holds it at 2 and its creatures 100,000 health, a blow far short of a tenth.
	FTrapRowWorld Scope;
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
	constexpr float Sturdy = 100000.0f;
	const FVector OnceHome(100.0f * M, 0.0f, 0.0f);
	const FVector TwiceHome(200.0f * M, 0.0f, 0.0f);
	const FVector SturdyHome(300.0f * M, 0.0f, 0.0f);
	FTrapRowRig Plain(World, FVector::ZeroVector);
	FTrapRowRig Once(World, OnceHome);
	FTrapRowRig Twice(World, TwiceHome);
	FTrapRowRig AgainstSturdy(World, SturdyHome);
	if (!Plain.IsUsable(*this) || !Once.IsUsable(*this) || !Twice.IsUsable(*this) || !AgainstSturdy.IsUsable(*this))
	{
		return false;
	}
	const auto Bleeding = [](float Size)
	{
		FCataclysmPoolAction Sized = TrapRowAction(TEXT("deployable_hit"), UCataclysmAbilitySystemComponent::ApplyStatusSizedAction);
		Sized.ApplyStatus = ECataclysmApplyStatus::Sized;
		Sized.StatusName = TEXT("Bleed");
		Sized.Percent = Size;
		return Sized;
	};
	for (const FTrapRowRig* Each : {&Plain, &Once, &Twice, &AgainstSturdy})
	{
		Each->Holds({TrapRowSlotFlag(TEXT("Slot.Heavy"))});
	}
	Once.System->SetPoolActions({Bleeding(1.0f)});
	Twice.System->SetPoolActions({Bleeding(2.0f)});
	AgainstSturdy.System->SetPoolActions({Bleeding(2.0f)});

	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector Near(2.5f * M, 0.0f, 0.0f);
	const FVector Behind(4.0f * M, 0.0f, 0.0f);
	struct FTrapRowBleedCase
	{
		const FTrapRowRig* Rig;
		FVector Home;
		float Health;
		ACataclysmEnemyCharacter* Setter = nullptr;
		ACataclysmEnemyCharacter* Other = nullptr;
		ACataclysmMinion* Trap = nullptr;
	};
	TArray<FTrapRowBleedCase> Cases = {{&Plain, FVector::ZeroVector, Frail}, {&Once, OnceHome, Frail},
									   {&Twice, TwiceHome, Frail}, {&AgainstSturdy, SturdyHome, Sturdy}};
	for (int32 Index = 0; Index < Cases.Num(); ++Index)
	{
		FTrapRowBleedCase& Case = Cases[Index];
		Case.Setter = TrapRowEnemy(World, Case.Home + North + Near, Case.Health);
		Case.Other = TrapRowEnemy(World, Case.Home + North + Behind, Case.Health);
		if (!TestNotNull(*FString::Printf(TEXT("set-up: case %d's enemy that sets its trap off"), Index), Case.Setter)
			|| !TestNotNull(*FString::Printf(TEXT("set-up: case %d's second enemy"), Index), Case.Other))
		{
			return false;
		}
		Case.Trap = TrapRowPlace(*this, *Case.Rig, ECataclysmAbilitySlot::Heavy, Case.Home + North);
		if (!Case.Trap)
		{
			return false;
		}
	}
	TestEqual(TEXT("set-up: the frail creatures hold 1,500 health"), TrapRowMaxHealthOf(Cases[0].Setter), Frail, 0.01f);

	TrapRowPass(World, 1.25f);
	for (FTrapRowBleedCase& Case : Cases)
	{
		TrapRowStep(Case.Trap);
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FTrapRowBleedCase& Case = Cases[Index];
		TestTrue(*FString::Printf(TEXT("set-up: case %d's cast took a tenth or more of each enemy's 1,500: %.2f and %.2f"),
								  Index, TrapRowTakenFrom(Case.Setter, Frail), TrapRowTakenFrom(Case.Other, Frail)),
			TrapRowTakenFrom(Case.Setter, Frail) >= Frail / 10.0f && TrapRowTakenFrom(Case.Other, Frail) >= Frail / 10.0f);
	}

	// THE CONTROL, READ FIRST: THE LASH LAYS NO BLEED OF ITS OWN.
	TestFalse(TEXT("control: the cast bleeds nothing without the row: not the enemy that set it off"),
		TrapRowCarries(Cases[0].Setter, Bleed));
	TestFalse(TEXT("control: nor the second"), TrapRowCarries(Cases[0].Other, Bleed));

	// THE ROW: EVERY ENEMY CAUGHT, AT ITS SIZE.
	TestTrue(TEXT("rolled at 1, the enemy that set it off bleeds"), TrapRowCarries(Cases[1].Setter, Bleed));
	TestTrue(TEXT("and so does the second, caught in the same cast"), TrapRowCarries(Cases[1].Other, Bleed));
	TestTrue(TEXT("rolled at 2, the enemy that set it off bleeds"), TrapRowCarries(Cases[2].Setter, Bleed));
	TestTrue(TEXT("and so does the second"), TrapRowCarries(Cases[2].Other, Bleed));
	const float AtOne = UCataclysmSkillEffects::StatedStrengthOn(Cases[1].Setter, Bleed);
	const float AtTwo = UCataclysmSkillEffects::StatedStrengthOn(Cases[2].Setter, Bleed);
	if (TestTrue(*FString::Printf(TEXT("rolled at 1, the bleed states a size: %.4f"), AtOne), AtOne > 0.0f))
	{
		TestEqual(TEXT("rolled at 2, it is twice that: \"1-2 bleed stacks\" is one bleed at 1 to 2 times its size"),
			AtTwo, AtOne * 2.0f, AtOne * 0.001f);
	}

	// AND THE OWNER'S RULE OF A TENTH STILL HOLDS: a blow far short of a tenth lays nothing.
	TestTrue(*FString::Printf(TEXT("set-up: the sturdy case's cast struck its enemy: %.2f of 100,000"),
							  TrapRowTakenFrom(Cases[3].Setter, Sturdy)),
		TrapRowTakenFrom(Cases[3].Setter, Sturdy) > 0.0f);
	TestFalse(TEXT("a blow short of a tenth of the enemy's health lays no bleed (#917)"),
		TrapRowCarries(Cases[3].Setter, Bleed));
	return true;
}

// ==========================================================================
// 23. The damage row: a stack per trap triggered, each lasting its own 10 s, no cap (M4 (a))
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapRowDamageStacksTest,
	"Cataclysm.Traps.TheDamageRowGivesAStackForEachTrapTriggeredInTheLastTenSecondsEachLapsingAloneWithNoCap")
{
	using namespace CataclysmTrapRowTest;

	// TWO PLAYERS 100 M APART, EACH TRIGGERING SIX HEAVY TRAPS, ONE AT A TIME, ON AN ENEMY 2.5 M EAST OF EACH TRAP: three
	// at the start and three 6 s later. Each holds 10% increased attack_damage scaled by its own stacks. The row's player
	// grants them as the item builds "Each trap triggered in the last 10 seconds increases your damage by 8%-15%" from
	// its row (own_stacks on trap_triggered, Stack Seconds 10, no cap, so each lapses alone), rolled at 10. THE CONTROL
	// grants the same stacks as every own stack did before: a cap of 5, the window restarted by each grant.
	FTrapRowWorld Scope;
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
	FTrapRowRig Together(World, FVector::ZeroVector);
	FTrapRowRig Alone(World, RowHome);
	if (!Together.IsUsable(*this) || !Alone.IsUsable(*this))
	{
		return false;
	}
	const FName TogetherKey(TEXT("TrapRowTest:Together:attack_damage"));
	const FName AloneKey(TEXT("Positive_Each_trap_triggered_in_the_last_10_seconds_incre:attack_damage"));
	Together.Holds({TrapRowSlotFlag(TEXT("Slot.Heavy")),
					{FName(TEXT("attack_damage")), 10.0f, ECataclysmStatBucket::Increased, nullptr, TogetherKey}});
	Alone.Holds({TrapRowSlotFlag(TEXT("Slot.Heavy")),
				 {FName(TEXT("attack_damage")), 10.0f, ECataclysmStatBucket::Increased, nullptr, AloneKey}});
	Together.System->SetPoolActions({TrapRowOwnStackGrant(TEXT("trap_triggered"), TogetherKey, 10.0f, 5)});
	Alone.System->SetPoolActions({TrapRowOwnStackGrant(TEXT("trap_triggered"), AloneKey, 10.0f, 0)});
	const FGameplayTagContainer NoTags;

	// SIX TRAPS EACH, 10 M APART ALONG Y, EACH WITH ITS ENEMY 2.5 M EAST; EACH SET OFF BY NAMING ITS ENEMY.
	const auto TriggerOne = [this, World](const FTrapRowRig& Rig, const FVector& Home, int32 Index)
	{
		const FVector Where = Home + FVector(0.0f, 10.0f * M * static_cast<float>(Index), 0.0f);
		ACataclysmEnemyCharacter* Enemy = TrapRowEnemy(World, Where + FVector(2.5f * M, 0.0f, 0.0f));
		ACataclysmMinion* Trap = TrapRowPlace(*this, Rig, ECataclysmAbilitySlot::Heavy, Where);
		return Enemy && Trap && Trap->TriggerSkillTrap(Enemy);
	};
	TestEqual(TEXT("control: before any trigger, no stack"), Alone.System->OwnStacksHeld(AloneKey), 0);
	TestEqual(TEXT("control: and no increase"), Alone.System->IncreasesForStat(FName(TEXT("attack_damage")), NoTags), 0.0f,
		0.0001f);
	for (int32 Index = 1; Index <= 3; ++Index)
	{
		if (!TestTrue(*FString::Printf(TEXT("set-up: the first three traps triggered (%d)"), Index),
				TriggerOne(Together, FVector::ZeroVector, Index) && TriggerOne(Alone, RowHome, Index)))
		{
			return false;
		}
	}
	TestEqual(TEXT("three triggers, three stacks"), Alone.System->OwnStacksHeld(AloneKey), 3);
	TrapRowPass(World, 6.0f);
	for (int32 Index = 4; Index <= 6; ++Index)
	{
		if (!TestTrue(*FString::Printf(TEXT("set-up: the next three traps triggered (%d)"), Index),
				TriggerOne(Together, FVector::ZeroVector, Index) && TriggerOne(Alone, RowHome, Index)))
		{
			return false;
		}
	}
	TestEqual(TEXT("control: a capped own stack holds its cap of five"), Together.System->OwnStacksHeld(TogetherKey), 5);
	TestEqual(TEXT("six triggers, six stacks: no cap (M4 (a))"), Alone.System->OwnStacksHeld(AloneKey), 6);
	TestEqual(TEXT("each 10% more damage: 60% increased"),
		Alone.System->IncreasesForStat(FName(TEXT("attack_damage")), NoTags), 0.6f, 0.0001f);

	// 10.5 S AFTER THE FIRST THREE, 4.5 AFTER THE NEXT.
	TrapRowPass(World, 4.5f);
	TestEqual(TEXT("control: the capped stacks lapse together, restarted by the last grant: five still"),
		Together.System->OwnStacksHeld(TogetherKey), 5);
	TestEqual(TEXT("each of the first three lapsed alone, 10 s after its trigger: three stand"),
		Alone.System->OwnStacksHeld(AloneKey), 3);
	TestEqual(TEXT("so 30% increased"), Alone.System->IncreasesForStat(FName(TEXT("attack_damage")), NoTags), 0.3f, 0.0001f);

	// WHAT THE LINE ABOVE THE BAR SHOWS: a capped row against its cap, THE CONTROL; a row with no cap, its count alone.
	const TArray<FName> Damage = {FName(TEXT("attack_damage")), FName(TEXT("spell_damage"))};
	TestEqual(TEXT("control: a capped row's entry reads its count against its cap"),
		UCataclysmSkillBar::OwnStacksEntry(Damage, 3, 5), FString(TEXT("attack/spell damage 3/5")));
	TestEqual(TEXT("a row with no cap reads its count alone"), UCataclysmSkillBar::OwnStacksEntry(Damage, 3, 0),
		FString(TEXT("attack/spell damage 3")));

	// 10.5 S AFTER THE LAST.
	TrapRowPass(World, 6.0f);
	TestEqual(TEXT("and 10 s after the last trigger, none"), Alone.System->OwnStacksHeld(AloneKey), 0);
	return true;
}

// ==========================================================================
// 24. The evasion burst: flat points for 3 s on placing, refreshed and not stacked (M5 (a))
// ==========================================================================

CATACLYSM_TEST(FCataclysmTrapRowEvasionTest,
	"Cataclysm.Traps.TheEvasionRowGivesItsPointsForThreeSecondsOnPlacingATrapRefreshedAndNotStacked")
{
	using namespace CataclysmTrapRowTest;

	// ONE PLAYER, NO ENEMY. It holds a flat 15 evasion scaled by its own stacks and grants them as the item builds
	// "Placing a trap generates a burst of evasion, granting 10%-20% evasion chance for 3 seconds" from its row
	// (own_stacks on trap_placed, Stack Seconds 3, a cap of 1), rolled at 15. Evasion is a chance in points, read as
	// the blow reads it, by `StatForSkill`.
	FTrapRowWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FTrapRowRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	const FName Key(TEXT("Positive_Placing_a_trap_generates_a_burst_of_evasion_gra:evasion"));
	Rig.Holds({TrapRowSlotFlag(TEXT("Slot.Heavy")), {FName(TEXT("evasion")), 15.0f, ECataclysmStatBucket::Flat, nullptr, Key}});
	Rig.System->SetPoolActions({TrapRowOwnStackGrant(TEXT("trap_placed"), Key, 3.0f, 1)});
	const FGameplayTagContainer NoTags;
	const auto Evasion = [&Rig, &NoTags]() { return Rig.System->StatForSkill(FName(TEXT("evasion")), NoTags, 0.0f); };

	// THE CONTROL, READ FIRST.
	const float Before = Evasion();
	TestEqual(TEXT("control: before any trap is placed, no burst"), Rig.System->OwnStacksHeld(Key), 0);
	if (!TrapRowPlace(*this, Rig, ECataclysmAbilitySlot::Heavy, FVector(0.0f, 20.0f * M, 0.0f)))
	{
		return false;
	}
	TestEqual(TEXT("placing a trap grants 15 points of evasion chance"), Evasion() - Before, 15.0f, 0.001f);

	// A SECOND PLACING 2 S LATER REFRESHES THE ONE BURST AND DOES NOT ADD A SECOND (M5 (a)).
	TrapRowPass(World, 2.0f);
	if (!TrapRowPlace(*this, Rig, ECataclysmAbilitySlot::Heavy, FVector(0.0f, 30.0f * M, 0.0f)))
	{
		return false;
	}
	TestEqual(TEXT("a second placing within the 3 s does not stack: still 15"), Evasion() - Before, 15.0f, 0.001f);
	TrapRowPass(World, 2.5f);
	TestEqual(TEXT("4.5 s after the first and 2.5 after the second it holds: the second refreshed the 3 s"),
		Evasion() - Before, 15.0f, 0.001f);
	TrapRowPass(World, 1.0f);
	TestEqual(TEXT("3.5 s after the second, the burst is over"), Evasion() - Before, 0.0f, 0.001f);
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
