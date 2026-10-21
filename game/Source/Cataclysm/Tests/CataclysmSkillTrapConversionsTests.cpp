// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "Interface/CataclysmSkillBar.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Skill traps, layer 2: the Support's and the basic attack's conversions, and the basic and Heavy traps' aim at the
 * nearest living enemy. Ruled 2026-10-21 by the coordinating session (L1-L6, and the heavy trap's aim), on top of T1-T19,
 * for "Your support ability is converted into a placeable trap. When triggered, it activates your support ability
 * centered on the triggering enemy. Scales off trap damage. All placed copies share the support ability cooldown." and
 * "Your basic attack is converted into a placeable trap. When triggered, it fires your basic attack at the nearest
 * enemy. Scales off trap damage. Shares your basic attack cooldown across all placed copies." In layer 1's group,
 * `Cataclysm.Traps.`, whose rig this follows, in a file of its own.
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER WITH THE GAME'S OWN CONTROLLER, holding a Demonic Whip, given a weapon
 * damage of 100, 10,000 health, 1,000 mana and an attack speed of 1.25 (a swing every 0.8 s) by hand: its basic attack a
 * 3 m Strike of 45 degrees (`ItemBases.csv`), its Heavy Searing Lash a 5 m cone, its Support Coil of Embers, a self buff
 * whose repeat shoves an enemy within 3 m. One case holds a War Shield, which has no basic attack. NO ROW IS AUTHORED, SO
 * EVERY STAT IS GRANTED BY HAND, one stat line per call, replacing the last; a slot's flag is a flat 1 on
 * `skill_slot_is_a_trap` requiring the slot's tag, the row the enchantment session is to write.
 *
 * NO KEY IS PRESSED. The click on an enemy, `ACataclysmPlayerController::PressOnAnEnemyForTest`, is the basic attack's
 * press, and the basic attack costs no mana and no Fervour (`SkillSlots.csv`); every other trap is placed by
 * `ACataclysmMinion::PlaceSkillTrap`, and a trap's cast is a free start, which lands at once from its place and waits for
 * no attack clip. Every body stands at least 1 m from every other, on a real side. NOTHING HERE WAITS ON A TICK: the
 * world's clock is moved by hand and a trap's step is called directly, a second and a quarter after placing. Blows'
 * figures are compared only with the critical strike roll pinned, after the world is made. Helper types and functions
 * are named after the subject, the converted trap.
 */
namespace CataclysmSkillTrapConversionsTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The health every creature here starts with. */
	constexpr float ConvertedTrapEnemyHealth = 50000.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FConvertedTrapWorld
	{
		FConvertedTrapWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FConvertedTrapWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/** A creature of the monsters' side with no armour, evasion, block or resistance. It deals nothing of its own. */
	ACataclysmEnemyCharacter* ConvertedTrapEnemy(UWorld* World, const FVector& Where)
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
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), ConvertedTrapEnemyHealth);
		return Made;
	}

	float ConvertedTrapHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** What a creature has lost since it was made. */
	float ConvertedTrapTakenFrom(const AActor* Who)
	{
		return ConvertedTrapEnemyHealth - ConvertedTrapHealthOf(Who);
	}

	/** One row of a stat line a test's player holds. */
	struct FConvertedTrapRow
	{
		FName Stat;
		float Value = 0.0f;
		ECataclysmStatBucket Bucket = ECataclysmStatBucket::Flat;
		/** The row's one required tag, or null for none. */
		const TCHAR* RequiredTag = nullptr;
	};

	/** A slot's trap flag: a flat 1 on `skill_slot_is_a_trap` requiring the slot's tag, the row to be authored. */
	FConvertedTrapRow ConvertedTrapFlag(const TCHAR* SlotTag)
	{
		return {FName(UCataclysmAbilitySystemComponent::SkillSlotIsATrapStat), 1.0f, ECataclysmStatBucket::Flat, SlotTag};
	}

	/** "Traps deal 20%-40% increased damage" at 50: 50 increased `minion_damage` requiring `Type.Trap`. */
	FConvertedTrapRow ConvertedTrapDamageRow()
	{
		return {FName(TEXT("minion_damage")), 50.0f, ECataclysmStatBucket::Increased, TEXT("Type.Trap")};
	}

	/** A possessed player character with the game's controller holding a weapon, its ability system and its slots. */
	struct FConvertedTrapRig
	{
		FConvertedTrapRig(UWorld* World, const FVector& Where, const TCHAR* DamageType = TEXT("Demonic"),
						  const TCHAR* WeaponType = TEXT("Whip"))
		{
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
			Controller = World->SpawnActor<ACataclysmPlayerController>();
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
			Slots->SetDamageType(DamageType);
			Slots->EquipWeaponType(WeaponType);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 1.25f);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		}

		/** Whether everything was made and the weapon holds a row for `Slot`. Reports why not. */
		bool IsUsable(FAutomationTestBase& Test, ECataclysmAbilitySlot Slot = ECataclysmAbilitySlot::BasicAttack) const
		{
			FCataclysmWeaponSkill Row;
			if (!Player || !Controller || !System || !Slots || !HeldRow(Slot, Row))
			{
				Test.AddError(TEXT("set-up: a possessed player with the game's controller, holding its weapon and a row "
								   "for the slot. If a row is missing, DT_WeaponSkills or DT_ItemBases may be older "
								   "than game/Data: run tools/generate_datatable_assets.py"));
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

		/** The row the weapon holds for `Slot`, or false. */
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
		void Holds(const TArray<FConvertedTrapRow>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FConvertedTrapRow& Each : Rows)
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

		/** A click of the left button on `Enemy` and its release, as play runs them from the cursor's enemy. */
		void Clicks(AActor* Enemy) const
		{
			Controller->PressOnAnEnemyForTest(Enemy);
			Controller->ReleaseTheButtonForTest();
		}

		/** Move the player, so it does not stand on a trap placed at its feet. */
		void StandsAt(const FVector& Where) const
		{
			Player->SetActorLocation(Where, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr,
									 ETeleportType::TeleportPhysics);
		}

		/** The player moved to `Where`, clear of every trap it has placed, and a click on `Enemy` from there. */
		void ClicksFrom(const FVector& Where, AActor* Enemy) const
		{
			StandsAt(Where);
			Clicks(Enemy);
		}

		ACataclysmPlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
	};

	/** A skill trap for this rig's `Slot` at `Where`, or null with the reason reported. */
	ACataclysmMinion* ConvertedTrapPlace(FAutomationTestBase& Test, const FConvertedTrapRig& Rig,
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

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FConvertedTrapEventCount
	{
		FConvertedTrapEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
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
		~FConvertedTrapEventCount()
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
	void ConvertedTrapPass(UWorld* World, float Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/** Run a trap's quarter-second step, as `RegenerationStep` calls it. */
	void ConvertedTrapStep(ACataclysmMinion* Trap)
	{
		if (IsValid(Trap))
		{
			ACataclysmMinion::GadgetStep(Trap);
		}
	}

	/** Whether a trap is gone: destroyed, or on its way out. */
	bool ConvertedTrapGone(const TWeakObjectPtr<ACataclysmMinion>& Trap)
	{
		return !Trap.IsValid() || Trap->IsActorBeingDestroyed();
	}

	/**
	 * The self buffs running on a player that were cast from a place a free start was given: a trap's, since nothing
	 * else here gives one. The trap is used up as its cast starts, so the buff's `FreeRepeatCastBy` cannot name it.
	 */
	TArray<UCataclysmSelfBuffSkill*> ConvertedTrapSelfBuffsRunning(const UCataclysmAbilitySystemComponent* System)
	{
		TArray<UCataclysmSelfBuffSkill*> Running;
		for (const FGameplayAbilitySpec& Spec : System->GetActivatableAbilities())
		{
			UCataclysmSelfBuffSkill* Buff = Cast<UCataclysmSelfBuffSkill>(Spec.GetPrimaryInstance());
			if (Buff && Spec.IsActive() && Buff->bFreeRepeatHasOrigin)
			{
				Running.Add(Buff);
			}
		}
		return Running;
	}

	/** The skill bar's box for `Slot`, or an empty box with `None` for its slot. */
	FCataclysmSkillBarSlot ConvertedTrapBox(const AActor* Player, ECataclysmAbilitySlot Slot)
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

	/** How many of `Player`'s standing skill traps cast `Slot`. */
	int32 ConvertedTrapsOfSlot(const AActor* Player, ECataclysmAbilitySlot Slot)
	{
		int32 Found = 0;
		for (const ACataclysmMinion* Trap : ACataclysmMinion::SkillTrapsOf(Player))
		{
			Found += Trap->SkillTrapSlot == Slot ? 1 : 0;
		}
		return Found;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 10. The Support trap's self buff reaches the player only within 8 m of the trap (L5, T7)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapSupportReachTest,
	"Cataclysm.Traps.ASupportTrapGrantsItsSelfBuffToThePlayerWithinEightMetresOfTheTrapAndNotBeyond")
{
	using namespace CataclysmSkillTrapConversionsTest;

	// ONE PLAYER WITH A SUPPORT TRAP 20 M ALONG Y AND AN ENEMY 2.5 M EAST OF IT. The player stands 9 m west of the trap,
	// then 7.5 m.
	FConvertedTrapWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FConvertedTrapRig Rig(World, FVector::ZeroVector);
	FCataclysmWeaponSkill Coil;
	if (!Rig.IsUsable(*this, ECataclysmAbilitySlot::Support) || !Rig.HeldRow(ECataclysmAbilitySlot::Support, Coil)
		|| !TestTrue(TEXT("set-up: the whip's Support skill, Coil of Embers, is a self buff"),
			   Coil.Shape == ECataclysmSkillShape::SelfBuff))
	{
		return false;
	}
	const FVector At(0.0f, 20.0f * M, 0.0f);
	ACataclysmEnemyCharacter* Enemy = ConvertedTrapEnemy(World, At + FVector(2.5f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the enemy 2.5 m from the trap"), Enemy))
	{
		return false;
	}

	// THE BOX FIRST: the Support's, converted, is a trap's (layer 1's box, reached by this layer's flag).
	TestFalse(TEXT("control: the Support box, before the row, is not a trap's"),
		ConvertedTrapBox(Rig.Player, ECataclysmAbilitySlot::Support).bIsASkillTrap);
	Rig.Holds({ConvertedTrapFlag(TEXT("Slot.Support"))});
	TestTrue(TEXT("with the Support flag, the Support box is a trap's"),
		ConvertedTrapBox(Rig.Player, ECataclysmAbilitySlot::Support).bIsASkillTrap);

	ACataclysmMinion* Trap = ConvertedTrapPlace(*this, Rig, ECataclysmAbilitySlot::Support, At);
	if (!Trap)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> Watched(Trap);
	FConvertedTrapEventCount Triggers(Rig.System, TEXT("trap_triggered"));

	// THE CONTROL, READ FIRST: 9 M FROM THE TRAP, BEYOND 8 M. Armed, the enemy within 3 m, and nothing granted.
	Rig.StandsAt(At + FVector(-9.0f * M, 0.0f, 0.0f));
	ConvertedTrapPass(World, 1.25f);
	ConvertedTrapStep(Trap);
	TestTrue(TEXT("control: a second and a quarter on, the Support trap is armed"), Trap->bSkillTrapArmed);
	TestEqual(TEXT("control: 9 m from the trap, beyond 8 m, the player is granted no self buff (T7)"),
		ConvertedTrapSelfBuffsRunning(Rig.System).Num(), 0);
	TestEqual(TEXT("control: so the trap did not trigger"), Trap->SkillTrapTriggersMade, 0);
	TestFalse(TEXT("control: and stands, armed, as a trap whose skill is refused does (layer 1)"),
		ConvertedTrapGone(Watched));
	TestEqual(TEXT("control: and no trap_triggered was raised"), Triggers.Count, 0);

	// 7.5 M FROM THE TRAP, WITHIN 8 M.
	Rig.StandsAt(At + FVector(-7.5f * M, 0.0f, 0.0f));
	ConvertedTrapStep(Trap);
	TestEqual(TEXT("7.5 m from the trap, within 8 m, its trigger grants the player the self buff"),
		ConvertedTrapSelfBuffsRunning(Rig.System).Num(), 1);
	TestEqual(TEXT("trap_triggered was raised once"), Triggers.Count, 1);
	TestTrue(TEXT("and the trap is used up"), ConvertedTrapGone(Watched));
	return true;
}

// ==========================================================================
// 11. The Support trap's self buff measures its enemy areas from the triggering enemy (L5)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapSupportAreasTest,
	"Cataclysm.Traps.ASupportTrapsSelfBuffMeasuresItsEnemyAreasFromTheTriggeringEnemy")
{
	using namespace CataclysmSkillTrapConversionsTest;

	// A SUPPORT TRAP 20 M ALONG Y; THE TRIGGERING ENEMY 2.5 M EAST OF IT; A BYSTANDER 2 M FURTHER EAST, 4.5 M FROM THE
	// TRAP, SO IT SETS NOTHING OFF; THE PLAYER 7.5 M WEST OF THE TRAP; AND AN ENEMY 2 M SOUTH OF THE PLAYER, 7.76 M FROM
	// THE TRAP AND 10.2 M FROM THE TRIGGERING ENEMY. Coil of Embers' repeat shoves an enemy within 3 m.
	FConvertedTrapWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FConvertedTrapRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this, ECataclysmAbilitySlot::Support))
	{
		return false;
	}
	Rig.Holds({ConvertedTrapFlag(TEXT("Slot.Support"))});
	const FVector At(0.0f, 20.0f * M, 0.0f);
	const FVector PlayerAt = At + FVector(-7.5f * M, 0.0f, 0.0f);
	ACataclysmEnemyCharacter* Triggering = ConvertedTrapEnemy(World, At + FVector(2.5f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* ByTheEnemy = ConvertedTrapEnemy(World, At + FVector(4.5f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* ByThePlayer = ConvertedTrapEnemy(World, PlayerAt + FVector(0.0f, -2.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: the triggering enemy"), Triggering)
		|| !TestNotNull(TEXT("set-up: the enemy 2 m from it"), ByTheEnemy)
		|| !TestNotNull(TEXT("set-up: the enemy 2 m from the player"), ByThePlayer))
	{
		return false;
	}
	Rig.StandsAt(PlayerAt);
	ACataclysmMinion* Trap = ConvertedTrapPlace(*this, Rig, ECataclysmAbilitySlot::Support, At);
	if (!Trap)
	{
		return false;
	}

	ConvertedTrapPass(World, 1.25f);
	ConvertedTrapStep(Trap);
	const TArray<UCataclysmSelfBuffSkill*> Running = ConvertedTrapSelfBuffsRunning(Rig.System);
	if (!TestEqual(TEXT("set-up: the trap triggered and its self buff runs on the player"), Running.Num(), 1))
	{
		return false;
	}
	UCataclysmSelfBuffSkill* Buff = Running[0];
	const FVector EnemyAt = Triggering->GetActorLocation();
	TestTrue(*FString::Printf(TEXT("the buff is cast from the triggering enemy's place, %.2f cm from it, and not the "
								   "trap's, 250 cm off"), FVector::Dist(Buff->FreeRepeatOrigin, EnemyAt)),
		FVector::Dist(Buff->FreeRepeatOrigin, EnemyAt) < 1.0f);

	// ITS REPEAT: THE CONTROL, READ FIRST, IS THE ENEMY BESIDE THE PLAYER.
	const FVector ByThePlayerBefore = ByThePlayer->GetActorLocation();
	const float ByTheEnemyBefore = FVector::Dist2D(ByTheEnemy->GetActorLocation(), EnemyAt);
	Buff->RepeatTick();
	TestEqual(*FString::Printf(TEXT("control: the enemy 2 m from the player, 10.2 m from the triggering enemy, is not "
									"shoved: it moved %.2f cm"),
							   FVector::Dist(ByThePlayer->GetActorLocation(), ByThePlayerBefore)),
		FVector::Dist(ByThePlayer->GetActorLocation(), ByThePlayerBefore), 0.0f, 1.0f);
	TestTrue(*FString::Printf(TEXT("the enemy 2 m from the triggering enemy is shoved away from it (L5): %.2f cm from it, "
								   "from %.2f"),
							  FVector::Dist2D(ByTheEnemy->GetActorLocation(), EnemyAt), ByTheEnemyBefore),
		FVector::Dist2D(ByTheEnemy->GetActorLocation(), EnemyAt) > ByTheEnemyBefore + 1.0f);
	return true;
}

// ==========================================================================
// 12. A click on an enemy places a basic trap at the feet and swings nothing; both ways, a trap (L1, T3, T17, T19)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapBasicClickTest,
	"Cataclysm.Traps.AClickOnAnEnemyPlacesABasicTrapAtTheFeetSwingsNothingAndABothWaysBasicMakesATrap")
{
	using namespace CataclysmSkillTrapConversionsTest;

	// THREE PLAYERS 100 M APART, EACH WITH AN ENEMY 2 M EAST, within the whip's 3 m basic reach: the control holds no
	// row, the second the basic trap flag, the third the basic trap flag and the basic gadget flag.
	FConvertedTrapWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector RigHome(100.0f * M, 0.0f, 0.0f);
	const FVector BothHome(200.0f * M, 0.0f, 0.0f);
	const FVector East(2.0f * M, 0.0f, 0.0f);
	FConvertedTrapRig Plain(World, FVector::ZeroVector);
	FConvertedTrapRig Rig(World, RigHome);
	FConvertedTrapRig Both(World, BothHome);
	if (!Plain.IsUsable(*this) || !Rig.IsUsable(*this) || !Both.IsUsable(*this))
	{
		return false;
	}
	ACataclysmEnemyCharacter* PlainEnemy = ConvertedTrapEnemy(World, East);
	ACataclysmEnemyCharacter* RigEnemy = ConvertedTrapEnemy(World, RigHome + East);
	ACataclysmEnemyCharacter* BothEnemy = ConvertedTrapEnemy(World, BothHome + East);
	if (!TestNotNull(TEXT("set-up: the control's enemy"), PlainEnemy)
		|| !TestNotNull(TEXT("set-up: the converted player's enemy"), RigEnemy)
		|| !TestNotNull(TEXT("set-up: the both-ways player's enemy"), BothEnemy))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: holding no row, a click on the enemy swings at it.
	const float NeverSwung = Plain.Controller->LastSwingSecondsForTest();
	Plain.Clicks(PlainEnemy);
	TestTrue(TEXT("control: holding no row, a click on an enemy in reach starts a swing"),
		Plain.Controller->LastSwingSecondsForTest() > NeverSwung);
	TestEqual(TEXT("control: and places no trap"), ACataclysmMinion::SkillTrapsOf(Plain.Player).Num(), 0);

	// CONVERTED.
	Rig.Holds({ConvertedTrapFlag(TEXT("Slot.Basic"))});
	TestTrue(TEXT("set-up: the basic slot is converted into a trap"),
		Rig.System->SlotIsASkillTrap(ECataclysmAbilitySlot::BasicAttack));
	FConvertedTrapEventCount AttackUses(Rig.System, TEXT("attack_use"));
	FConvertedTrapEventCount SkillUses(Rig.System, TEXT("skill_use"));
	FConvertedTrapEventCount Placings(Rig.System, TEXT("trap_placed"));

	// A PRESS ON THE FLOOR, WHICH NAMES NO ENEMY: nothing is placed (and a floor click still moves, S15).
	Rig.Controller->PressOnAnEnemyForTest(nullptr);
	TestEqual(TEXT("a press on the floor places no trap"), ACataclysmMinion::SkillTrapsOf(Rig.Player).Num(), 0);

	const float SwungAt = Rig.Controller->LastSwingSecondsForTest();
	const float EnemyBefore = ConvertedTrapHealthOf(RigEnemy);
	const FVector Feet = Rig.Player->GetActorLocation();
	Rig.Controller->PressOnAnEnemyForTest(RigEnemy);
	const TArray<ACataclysmMinion*> Placed = ACataclysmMinion::SkillTrapsOf(Rig.Player);
	if (Placed.Num() != 1)
	{
		AddError(FString::Printf(TEXT("the click on the enemy placed %d skill traps, not one. DT_MinionTypes needs the "
									  "SkillTrap row: run tools/generate_datatable_assets.py"), Placed.Num()));
		return false;
	}
	ACataclysmMinion* Trap = Placed[0];
	TestTrue(TEXT("the click on the enemy placed a skill trap of the basic slot (L1)"),
		Trap->IsSkillTrap() && Trap->SkillTrapSlot == ECataclysmAbilitySlot::BasicAttack);
	TestTrue(*FString::Printf(TEXT("at the player's feet (T3): %.3f cm away"), FVector::Dist(Trap->GetActorLocation(), Feet)),
		FVector::Dist(Trap->GetActorLocation(), Feet) < 1.0f);
	TestEqual(TEXT("the click started no swing"), Rig.Controller->LastSwingSecondsForTest(), SwungAt, 0.0001f);
	TestEqual(TEXT("the enemy took nothing from the click"), ConvertedTrapHealthOf(RigEnemy), EnemyBefore, 0.001f);
	TestEqual(TEXT("the click is the basic attack's use (T19): attack_use was raised once"), AttackUses.Count, 1);
	TestEqual(TEXT("and, as for any basic attack, no skill_use (ruled 2026-09-14)"), SkillUses.Count, 0);
	TestEqual(TEXT("trap_placed was raised once"), Placings.Count, 1);
	TestEqual(TEXT("and the trap cast nothing as it was placed"), Trap->SkillTrapTriggersMade, 0);
	Rig.Controller->ReleaseTheButtonForTest();

	// A BASIC ATTACK CONVERTED BOTH WAYS MAKES A TRAP (T17).
	Both.Holds({ConvertedTrapFlag(TEXT("Slot.Basic")),
				{FName(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat), 1.0f, ECataclysmStatBucket::Flat,
				 TEXT("Slot.Basic")}});
	const float BothSwungAt = Both.Controller->LastSwingSecondsForTest();
	Both.Clicks(BothEnemy);
	TestEqual(TEXT("a basic attack converted to both a gadget and a trap: the click places a trap (T17)"),
		ACataclysmMinion::SkillTrapsOf(Both.Player).Num(), 1);
	TestNull(TEXT("and no gadget"), Both.System->SkillGadgetIn(ECataclysmAbilitySlot::BasicAttack));
	TestEqual(TEXT("and no swing"), Both.Controller->LastSwingSecondsForTest(), BothSwungAt, 0.0001f);
	TestEqual(TEXT("and the basic gadget's line is not drawn for it"), UCataclysmSkillBar::BasicGadgetTextOf(Both.Player),
		FString());
	return true;
}

// ==========================================================================
// 13. A basic trap at most once per swing interval, on one clock for every basic trap (L2)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapBasicClockTest,
	"Cataclysm.Traps.ABasicTrapIsPlacedAtMostOncePerSwingIntervalOnOneClockForEveryBasicTrap")
{
	using namespace CataclysmSkillTrapConversionsTest;

	// TWO PLAYERS 100 M APART, EACH HOLDING THE BASIC TRAP FLAG, WITH AN ENEMY 2 M EAST TO CLICK: the slow one at the rig's
	// attack speed of 1.25 (0.8 s), the quick one at 2.5 (0.4 s). Each click is a press and its release, made from a spot
	// of its own 2 m further along -Y, so no trap is placed on another or on the player.
	FConvertedTrapWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector QuickHome(100.0f * M, 0.0f, 0.0f);
	const FVector East(2.0f * M, 0.0f, 0.0f);
	FConvertedTrapRig Slow(World, FVector::ZeroVector);
	FConvertedTrapRig Quick(World, QuickHome);
	if (!Slow.IsUsable(*this) || !Quick.IsUsable(*this))
	{
		return false;
	}
	Quick.Set(UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 2.5f);
	Slow.Holds({ConvertedTrapFlag(TEXT("Slot.Basic"))});
	Quick.Holds({ConvertedTrapFlag(TEXT("Slot.Basic"))});
	ACataclysmEnemyCharacter* SlowEnemy = ConvertedTrapEnemy(World, East);
	ACataclysmEnemyCharacter* QuickEnemy = ConvertedTrapEnemy(World, QuickHome + East);
	if (!TestNotNull(TEXT("set-up: the slow player's enemy"), SlowEnemy)
		|| !TestNotNull(TEXT("set-up: the quick player's enemy"), QuickEnemy))
	{
		return false;
	}
	const auto Standing = [](const FConvertedTrapRig& Of) { return ACataclysmMinion::SkillTrapsOf(Of.Player).Num(); };
	const auto Spot = [](const FVector& Home, int32 Index)
	{
		return Home + FVector(0.0f, -2.0f * M * static_cast<float>(Index), 0.0f);
	};

	// THE CONTROL, READ FIRST: THE TWO SWING INTERVALS.
	TestEqual(TEXT("control: the slow player swings every 0.8 s"),
		UCataclysmBasicAttack::SecondsBetweenSwingsFor(Slow.System), 0.8f, 0.001f);
	TestEqual(TEXT("and the quick player, at twice the attack speed, every 0.4 s"),
		UCataclysmBasicAttack::SecondsBetweenSwingsFor(Quick.System), 0.4f, 0.001f);

	Slow.ClicksFrom(Spot(FVector::ZeroVector, 0), SlowEnemy);
	Quick.ClicksFrom(Spot(QuickHome, 0), QuickEnemy);
	TestEqual(TEXT("the slow player's first click places a basic trap"), Standing(Slow), 1);
	TestEqual(TEXT("and so does the quick player's"), Standing(Quick), 1);

	ConvertedTrapPass(World, 0.25f);
	Slow.ClicksFrom(Spot(FVector::ZeroVector, 1), SlowEnemy);
	Quick.ClicksFrom(Spot(QuickHome, 1), QuickEnemy);
	TestEqual(TEXT("a quarter of a second on, the slow player's next click places nothing (L2)"), Standing(Slow), 1);
	TestEqual(TEXT("nor the quick player's, short of its 0.4 s"), Standing(Quick), 1);

	ConvertedTrapPass(World, 0.25f);
	Slow.ClicksFrom(Spot(FVector::ZeroVector, 2), SlowEnemy);
	Quick.ClicksFrom(Spot(QuickHome, 2), QuickEnemy);
	TestEqual(TEXT("half a second on, the slow player's click, short of its 0.8 s, still places nothing"),
		Standing(Slow), 1);
	TestEqual(TEXT("and the quick player's, past its 0.4 s, places a second: attack speed shortens the clock"),
		Standing(Quick), 2);

	// ONE CLOCK FOR EVERY BASIC TRAP: the slow player's trap gone, the clock still runs.
	const TArray<ACataclysmMinion*> SlowTraps = ACataclysmMinion::SkillTrapsOf(Slow.Player);
	if (!TestEqual(TEXT("set-up: the slow player has its one basic trap to take away"), SlowTraps.Num(), 1))
	{
		return false;
	}
	SlowTraps[0]->Destroy();
	TestEqual(TEXT("set-up: the slow player's basic trap is gone"), Standing(Slow), 0);
	ConvertedTrapPass(World, 0.1f);
	Slow.ClicksFrom(Spot(FVector::ZeroVector, 3), SlowEnemy);
	TestEqual(TEXT("with no basic trap standing, a click 0.6 s after the last placing still places nothing: the clock is "
				   "the player's, shared by every basic trap"), Standing(Slow), 0);
	ConvertedTrapPass(World, 0.35f);
	Slow.ClicksFrom(Spot(FVector::ZeroVector, 4), SlowEnemy);
	TestEqual(TEXT("0.95 s after the last placing, past 0.8 s, a click places one"), Standing(Slow), 1);
	return true;
}

// ==========================================================================
// 14. A basic trap fires the basic attack at the nearest living enemy, free and priced as the trap's (L3)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapBasicNearestTest,
	"Cataclysm.Traps.ABasicTrapFiresTheBasicAttackAtTheNearestLivingEnemyFreeAndPricedAsTheTraps")
{
	using namespace CataclysmSkillTrapConversionsTest;

	// ONE PLAYER HOLDING THE BASIC TRAP FLAG AND 50% INCREASED minion_damage REQUIRING Type.Trap. A BASIC TRAP 20 M ALONG
	// Y: THE ENEMY THAT SETS IT OFF 2.5 M EAST OF IT, AND A NEARER ONE 1.5 M WEST. A lone enemy 60 m along Y for the
	// player's own cast of the basic attack from 1.5 m. THE TRAP IS SET OFF BY NAMING ITS ENEMY: its own step finds the
	// nearest within 3 m, which is the nearer one, so through the step the two never differ.
	FConvertedTrapWorld Scope;
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
	FConvertedTrapRig Rig(World, FVector::ZeroVector);
	FCataclysmWeaponSkill Basic;
	if (!Rig.IsUsable(*this) || !Rig.HeldRow(ECataclysmAbilitySlot::BasicAttack, Basic)
		|| !TestTrue(TEXT("set-up: the whip's basic attack is a Strike"), Basic.Shape == ECataclysmSkillShape::Strike))
	{
		return false;
	}
	Rig.Holds({ConvertedTrapFlag(TEXT("Slot.Basic")), ConvertedTrapDamageRow()});
	const FVector At(0.0f, 20.0f * M, 0.0f);
	const FVector Lone(0.0f, 60.0f * M, 0.0f);
	ACataclysmEnemyCharacter* LoneEnemy = ConvertedTrapEnemy(World, Lone);
	ACataclysmEnemyCharacter* WalkedIn = ConvertedTrapEnemy(World, At + FVector(2.5f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Nearer = ConvertedTrapEnemy(World, At + FVector(-1.5f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the lone enemy for the player's own cast"), LoneEnemy)
		|| !TestNotNull(TEXT("set-up: the enemy that sets the trap off, 2.5 m from it"), WalkedIn)
		|| !TestNotNull(TEXT("set-up: the nearer enemy, 1.5 m from it"), Nearer))
	{
		return false;
	}
	TestTrue(TEXT("set-up: a basic trap fires at the nearest enemy"),
		ACataclysmMinion::SkillTrapFiresAtTheNearestEnemy(ECataclysmAbilitySlot::BasicAttack));

	// THE CONTROL, READ FIRST: THE PLAYER'S OWN CAST OF THE BASIC ATTACK, FROM 1.5 M WEST OF THE LONE ENEMY AT IT.
	const FVector OwnFrom = Lone - FVector(1.5f * M, 0.0f, 0.0f);
	if (!TestTrue(TEXT("control: the player's own cast starts"),
			UCataclysmTriggeredSkill::Trigger(Rig.Player, Basic, Lone, /*DamageShare=*/1.0f, /*bPaysCost=*/false,
											  &OwnFrom)))
	{
		return false;
	}
	const float Own = ConvertedTrapTakenFrom(LoneEnemy);
	TestTrue(*FString::Printf(TEXT("control: it takes %.2f"), Own), Own > 0.0f);

	ACataclysmMinion* Trap = ConvertedTrapPlace(*this, Rig, ECataclysmAbilitySlot::BasicAttack, At);
	if (!Trap)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> Watched(Trap);
	FConvertedTrapEventCount AttackUses(Rig.System, TEXT("attack_use"));
	FConvertedTrapEventCount Triggers(Rig.System, TEXT("trap_triggered"));
	const float PlacedAt = Rig.System->BasicSkillTrapPlacedAtSeconds;
	ConvertedTrapPass(World, 1.25f);
	TestTrue(TEXT("set off by the enemy 2.5 m away, the basic trap casts"), Trap->TriggerSkillTrap(WalkedIn));
	TestEqual(TEXT("control: the enemy that set it off, on the far side, is untouched"), ConvertedTrapHealthOf(WalkedIn),
		ConvertedTrapEnemyHealth, 0.001f);
	TestEqual(*FString::Printf(TEXT("the cast went at the nearest living enemy, 1.5 m away (L3), priced by the Type.Trap "
									"row: 50%% more than the player's own, %.2f against %.2f"),
							   ConvertedTrapTakenFrom(Nearer), Own),
		ConvertedTrapTakenFrom(Nearer), Own * 1.5f, Own * 0.001f);
	TestEqual(TEXT("the cast is free and no use (T19): no attack_use was raised"), AttackUses.Count, 0);
	TestEqual(TEXT("nor did it move the basic trap clock"), Rig.System->BasicSkillTrapPlacedAtSeconds, PlacedAt, 0.0001f);
	TestEqual(TEXT("trap_triggered was raised once"), Triggers.Count, 1);
	TestTrue(TEXT("and the trap is used up"), ConvertedTrapGone(Watched));
	return true;
}

// ==========================================================================
// 15. The heavy trap's aim, changed in this layer: the nearest living enemy, not the one that set it off
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapHeavyNearestTest,
	"Cataclysm.Traps.AHeavyTrapFiresAtTheNearestLivingEnemyFreeAndPricedAsTheTraps")
{
	using namespace CataclysmSkillTrapConversionsTest;

	// THE SAME LAYOUT AS THE BASIC TRAP'S, WITH A HEAVY TRAP: Searing Lash, a 5 m cone of 45 degrees, so a cast at the
	// nearer enemy 1.5 m west cannot reach the one 2.5 m east.
	FConvertedTrapWorld Scope;
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
	FConvertedTrapRig Rig(World, FVector::ZeroVector);
	FCataclysmWeaponSkill Lash;
	if (!Rig.IsUsable(*this, ECataclysmAbilitySlot::Heavy) || !Rig.HeldRow(ECataclysmAbilitySlot::Heavy, Lash))
	{
		return false;
	}
	Rig.Holds({ConvertedTrapFlag(TEXT("Slot.Heavy")), ConvertedTrapDamageRow()});
	const FVector At(0.0f, 20.0f * M, 0.0f);
	const FVector Lone(0.0f, 60.0f * M, 0.0f);
	ACataclysmEnemyCharacter* LoneEnemy = ConvertedTrapEnemy(World, Lone);
	ACataclysmEnemyCharacter* WalkedIn = ConvertedTrapEnemy(World, At + FVector(2.5f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Nearer = ConvertedTrapEnemy(World, At + FVector(-1.5f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the lone enemy for the player's own cast"), LoneEnemy)
		|| !TestNotNull(TEXT("set-up: the enemy that sets the trap off, 2.5 m from it"), WalkedIn)
		|| !TestNotNull(TEXT("set-up: the nearer enemy, 1.5 m from it"), Nearer))
	{
		return false;
	}
	TestTrue(TEXT("set-up: a Heavy trap fires at the nearest enemy"),
		ACataclysmMinion::SkillTrapFiresAtTheNearestEnemy(ECataclysmAbilitySlot::Heavy));
	TestFalse(TEXT("set-up: and an Ultimate trap stays on its triggering enemy"),
		ACataclysmMinion::SkillTrapFiresAtTheNearestEnemy(ECataclysmAbilitySlot::Ultimate));
	const FGameplayAttribute Mana = UCataclysmVitalAttributeSet::GetManaAttribute();
	const FGameplayTag HeavyCooldown = UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Heavy);

	// THE CONTROL, READ FIRST: THE PLAYER'S OWN CAST OF SEARING LASH, FROM 1.5 M WEST OF THE LONE ENEMY AT IT.
	const FVector OwnFrom = Lone - FVector(1.5f * M, 0.0f, 0.0f);
	if (!TestTrue(TEXT("control: the player's own cast starts"),
			UCataclysmTriggeredSkill::Trigger(Rig.Player, Lash, Lone, /*DamageShare=*/1.0f, /*bPaysCost=*/false,
											  &OwnFrom)))
	{
		return false;
	}
	const float Own = ConvertedTrapTakenFrom(LoneEnemy);
	TestTrue(*FString::Printf(TEXT("control: it takes %.2f"), Own), Own > 0.0f);

	ACataclysmMinion* Trap = ConvertedTrapPlace(*this, Rig, ECataclysmAbilitySlot::Heavy, At);
	if (!Trap)
	{
		return false;
	}
	const TWeakObjectPtr<ACataclysmMinion> Watched(Trap);
	FConvertedTrapEventCount SkillUses(Rig.System, TEXT("skill_use"));
	FConvertedTrapEventCount Triggers(Rig.System, TEXT("trap_triggered"));
	const float ManaBefore = Rig.Get(Mana);
	ConvertedTrapPass(World, 1.25f);
	TestTrue(TEXT("set off by the enemy 2.5 m away, the Heavy trap casts"), Trap->TriggerSkillTrap(WalkedIn));
	TestEqual(TEXT("control: the enemy that set it off, on the far side, is untouched"), ConvertedTrapHealthOf(WalkedIn),
		ConvertedTrapEnemyHealth, 0.001f);
	TestEqual(*FString::Printf(TEXT("the cast went at the nearest living enemy, 1.5 m away (ruled 2026-10-21), priced by "
									"the Type.Trap row: 50%% more than the player's own, %.2f against %.2f"),
							   ConvertedTrapTakenFrom(Nearer), Own),
		ConvertedTrapTakenFrom(Nearer), Own * 1.5f, Own * 0.001f);
	TestEqual(TEXT("the cast is free: the player's mana is as it was"), Rig.Get(Mana), ManaBefore, 0.001f);
	TestFalse(TEXT("and no Heavy cooldown runs"), Rig.System->HasMatchingGameplayTag(HeavyCooldown));
	TestEqual(TEXT("and it is no use (T19): no skill_use was raised"), SkillUses.Count, 0);
	TestEqual(TEXT("trap_triggered was raised once"), Triggers.Count, 1);
	TestTrue(TEXT("and the trap is used up"), ConvertedTrapGone(Watched));
	return true;
}

// ==========================================================================
// 16. The cap across a basic, a Support and a Heavy trap together (T6)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapMixedCapTest,
	"Cataclysm.Traps.FiveTrapsStandAcrossTheBasicSupportAndHeavySlotsAndANewOneDestroysTheOldest")
{
	using namespace CataclysmSkillTrapConversionsTest;

	// ONE PLAYER HOLDING THE BASIC, SUPPORT AND HEAVY FLAGS; TRAPS 10 M APART ALONG Y, NO ENEMY. Placed in the order
	// basic, Support, Heavy, basic, Support; then a Heavy and a Support more.
	FConvertedTrapWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FConvertedTrapRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this) || !Rig.IsUsable(*this, ECataclysmAbilitySlot::Support)
		|| !Rig.IsUsable(*this, ECataclysmAbilitySlot::Heavy))
	{
		return false;
	}
	Rig.Holds({ConvertedTrapFlag(TEXT("Slot.Basic")), ConvertedTrapFlag(TEXT("Slot.Support")),
			   ConvertedTrapFlag(TEXT("Slot.Heavy"))});
	const auto At = [](int32 Index) { return FVector(0.0f, 10.0f * M * static_cast<float>(Index), 0.0f); };
	const ECataclysmAbilitySlot Order[] = {ECataclysmAbilitySlot::BasicAttack, ECataclysmAbilitySlot::Support,
										   ECataclysmAbilitySlot::Heavy, ECataclysmAbilitySlot::BasicAttack,
										   ECataclysmAbilitySlot::Support};
	TArray<ACataclysmMinion*> Five;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		ACataclysmMinion* Made = ConvertedTrapPlace(*this, Rig, Order[Index], At(Index + 1));
		if (!Made)
		{
			return false;
		}
		Five.Add(Made);
	}
	const TWeakObjectPtr<ACataclysmMinion> FirstBasic(Five[0]);
	const TWeakObjectPtr<ACataclysmMinion> FirstSupport(Five[1]);

	// THE CONTROL, READ FIRST: FIVE STAND ACROSS THE THREE SLOTS, THE FIRST BASIC TRAP THE OLDEST.
	const TArray<ACataclysmMinion*> Standing = ACataclysmMinion::SkillTrapsOf(Rig.Player);
	TestEqual(TEXT("control: five traps stand across the basic, Support and Heavy slots (T6)"), Standing.Num(), 5);
	TestTrue(TEXT("control: the oldest is the first basic trap"), !Standing.IsEmpty() && Standing[0] == Five[0]);

	ACataclysmMinion* SixthHeavy = ConvertedTrapPlace(*this, Rig, ECataclysmAbilitySlot::Heavy, At(6));
	ACataclysmMinion* SeventhSupport = ConvertedTrapPlace(*this, Rig, ECataclysmAbilitySlot::Support, At(7));
	if (!SixthHeavy || !SeventhSupport)
	{
		return false;
	}
	TestEqual(TEXT("with a Heavy and a Support more, five still stand"), ACataclysmMinion::SkillTrapsOf(Rig.Player).Num(),
		5);
	TestTrue(TEXT("the Heavy, placed sixth, destroyed the oldest, the first basic trap"), ConvertedTrapGone(FirstBasic));
	TestTrue(TEXT("the Support, placed seventh, destroyed the next oldest, the first Support trap"),
		ConvertedTrapGone(FirstSupport));
	TestEqual(TEXT("one basic trap stands"), ConvertedTrapsOfSlot(Rig.Player, ECataclysmAbilitySlot::BasicAttack), 1);
	TestEqual(TEXT("two Support traps"), ConvertedTrapsOfSlot(Rig.Player, ECataclysmAbilitySlot::Support), 2);
	TestEqual(TEXT("and two Heavy traps"), ConvertedTrapsOfSlot(Rig.Player, ECataclysmAbilitySlot::Heavy), 2);
	return true;
}

// ==========================================================================
// 17. The basic trap's line above the bar (L2, the standing UI rule)
// ==========================================================================

CATACLYSM_TEST(FCataclysmSkillTrapBasicLineTest,
	"Cataclysm.Traps.TheBasicTrapsLineAboveTheBarCountsTheTrapsAndSweepsTheSwingIntervalLeft")
{
	using namespace CataclysmSkillTrapConversionsTest;

	// THE WORDS FIRST, THEN READ OFF A WHIP PLAYER AT THE RIG'S 0.8 S WITH AN ENEMY 2 M EAST TO CLICK, AND A WAR SHIELD
	// PLAYER 100 M AWAY, WHOSE WEAPON HAS NO BASIC ATTACK.
	TestEqual(TEXT("not converted, no line"), UCataclysmSkillBar::BasicTrapTextFor(false, false, 0.0f, 0, 5), FString());
	TestEqual(TEXT("a basic trap that can cast nothing says so"), UCataclysmSkillBar::BasicTrapTextFor(true, true, 0.0f, 0, 5),
		FString(TEXT("Basic trap casts nothing")));
	TestEqual(TEXT("ready, with none standing"), UCataclysmSkillBar::BasicTrapTextFor(true, false, 0.0f, 0, 5),
		FString(TEXT("Basic trap ready, traps 0/5")));
	TestEqual(TEXT("waiting 0.44 s with two standing"), UCataclysmSkillBar::BasicTrapTextFor(true, false, 0.44f, 2, 5),
		FString(TEXT("Basic trap 0.4s, traps 2/5")));

	FConvertedTrapWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FConvertedTrapRig Rig(World, FVector::ZeroVector);
	FConvertedTrapRig Shield(World, FVector(100.0f * M, 0.0f, 0.0f), TEXT("War"), TEXT("Shield"));
	FCataclysmWeaponSkill Unheld;
	if (!Rig.IsUsable(*this) || !Shield.IsUsable(*this, ECataclysmAbilitySlot::Heavy)
		|| !TestFalse(TEXT("set-up: the shield holds no basic attack row"),
			   Shield.HeldRow(ECataclysmAbilitySlot::BasicAttack, Unheld)))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Enemy = ConvertedTrapEnemy(World, FVector(2.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the enemy to click"), Enemy))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: HOLDING NO ROW, NO LINE.
	TestEqual(TEXT("control: holding no row, no line"), UCataclysmSkillBar::BasicTrapTextOf(Rig.Player), FString());
	Rig.Holds({ConvertedTrapFlag(TEXT("Slot.Basic"))});
	TestEqual(TEXT("converted, none placed: ready"), UCataclysmSkillBar::BasicTrapTextOf(Rig.Player),
		FString(TEXT("Basic trap ready, traps 0/5")));
	Rig.Clicks(Enemy);
	if (!TestEqual(TEXT("set-up: the click placed a basic trap"), ACataclysmMinion::SkillTrapsOf(Rig.Player).Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("just after the click, the whole swing interval left"), UCataclysmSkillBar::BasicTrapTextOf(Rig.Player),
		FString(TEXT("Basic trap 0.8s, traps 1/5")));
	ConvertedTrapPass(World, 0.5f);
	TestEqual(TEXT("half a second on, 0.3 s left"), UCataclysmSkillBar::BasicTrapTextOf(Rig.Player),
		FString(TEXT("Basic trap 0.3s, traps 1/5")));
	ConvertedTrapPass(World, 0.5f);
	TestEqual(TEXT("a second on, ready again"), UCataclysmSkillBar::BasicTrapTextOf(Rig.Player),
		FString(TEXT("Basic trap ready, traps 1/5")));

	Shield.Holds({ConvertedTrapFlag(TEXT("Slot.Basic"))});
	TestEqual(TEXT("the shield player's basic trap would cast nothing, and the line says so (T8)"),
		UCataclysmSkillBar::BasicTrapTextOf(Shield.Player), FString(TEXT("Basic trap casts nothing")));
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
