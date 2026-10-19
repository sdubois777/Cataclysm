// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
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
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tyrant's Chains' ten-piece bonus, the mechanism: "Your Tyrant's Minion now casts your ultimate ability every 3
 * seconds". The coordinating session's ruling #7 of 2026-10-17 (a FREE START, as the skill gadget's) and its rulings
 * T1-T7 of 2026-10-18. Beside `Cataclysm.SetBonus.Tyrant6.`, which made the Tyrant's Minion, and
 * `Cataclysm.SkillGadgets.`, whose cast path this one shares and whose rig this follows.
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER, of the players' side, HOLDING A DEMONIC WHIP, given a weapon damage of 100
 * by hand: its Ultimate is The Gathering, a 14 m point-blank strike. Other cases hold a Demonic Staff (Subjugate, a
 * summon), a Demonic Dagger (Everywhere at Once, a movement skill), a Demonic Greatsword (The Whole Weight, held for 3
 * seconds) and a Celestial Sword (whose Ultimate row has no skill, so the slot holds none). NO SHIPPED ULTIMATE IS A SELF
 * BUFF, so the self-buff case builds a weapon skill table of its own, as `Cataclysm.WeaponSlots.` does, holding Coil of
 * Embers' shape in the Ultimate slot.
 *
 * THE TYRANT'S MINION IS SUMMONED BY THE SIX-PIECE'S OWN CALL, `ACataclysmMinion::SummonTyrantsMinion`, with the
 * six-piece's seconds held for that call. NO ROW IS AUTHORED, SO EVERY STAT IS GRANTED BY HAND, one whole stat line per
 * call, replacing the last. The ten-piece's row is a flat 3 on `tyrants_minion_casts_ultimate_every_seconds` with no
 * tags, the row the enchantment session is to write.
 *
 * ENEMIES ARE OF THE MONSTERS' SIDE, with no defences, 50,000 health, dealing nothing. Every rig and its enemies stand
 * 60 m from every other, beyond the 14 m strike. A CAST IS A FREE START GIVEN A PLACE, so it lands at once and waits
 * for no clip. NOTHING HERE WAITS ON A TICK: the world's clock is moved by hand and the minion's step is called
 * directly, as `ACataclysmCharacterBase` calls it.
 */
namespace CataclysmSetBonusTyrantTenTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** An enemy's health as made. */
	constexpr float TyrantsUltimateEnemyHealth = 50000.0f;

	/** The row's seconds between casts: the sentence's 3. */
	constexpr float TyrantsUltimateSeconds = 3.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FTyrantsUltimateWorld
	{
		FTyrantsUltimateWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FTyrantsUltimateWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/** A creature of the monsters' side with no armour, evasion, block or resistance. It deals nothing. */
	ACataclysmEnemyCharacter* TyrantsUltimateEnemy(UWorld* World, const FVector& Where)
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
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), TyrantsUltimateEnemyHealth);
		return Made;
	}

	float TyrantsUltimateHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** What an enemy has lost since it was made. */
	float TyrantsUltimateLost(const AActor* Who)
	{
		return TyrantsUltimateEnemyHealth - TyrantsUltimateHealthOf(Who);
	}

	/** One row of a stat line a test's player holds. */
	struct FTyrantsUltimateRow
	{
		FName Stat;
		float Value = 0.0f;
		ECataclysmStatBucket Bucket = ECataclysmStatBucket::Flat;
		/** The row's one required tag, or null for none. */
		const TCHAR* RequiredTag = nullptr;
	};

	/** The ten-piece's row: a flat `Seconds` on the stat, with no tags. */
	FTyrantsUltimateRow TyrantsUltimateEvery(float Seconds = TyrantsUltimateSeconds)
	{
		FTyrantsUltimateRow Row;
		Row.Stat = FName(ACataclysmMinion::TyrantsMinionCastsUltimateEverySecondsStat);
		Row.Value = Seconds;
		return Row;
	}

	/** An increase of `Stat`, requiring `Tag` or nothing. */
	FTyrantsUltimateRow TyrantsUltimateIncrease(const TCHAR* Stat, float Value, const TCHAR* Tag = nullptr)
	{
		FTyrantsUltimateRow Row;
		Row.Stat = FName(Stat);
		Row.Value = Value;
		Row.Bucket = ECataclysmStatBucket::Increased;
		Row.RequiredTag = Tag;
		return Row;
	}

	/** The skill gadget's slot flag: a flat 1 on `skill_slot_is_a_gadget` requiring the slot's tag. */
	FTyrantsUltimateRow TyrantsUltimateConverts(const TCHAR* SlotTag)
	{
		FTyrantsUltimateRow Row;
		Row.Stat = FName(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat);
		Row.Value = 1.0f;
		Row.RequiredTag = SlotTag;
		return Row;
	}

	/** A possessed player character holding one weapon, from `Table` when one is given. */
	struct FTyrantsUltimateRig
	{
		FTyrantsUltimateRig(UWorld* World, const FVector& Where, const TCHAR* DamageType = TEXT("Demonic"),
							const TCHAR* WeaponType = TEXT("Whip"), const UDataTable* Table = nullptr)
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
			if (Table)
			{
				Slots->SetWeaponSkillTable(Table);
			}
			Slots->SetDamageType(DamageType);
			Slots->EquipWeaponType(WeaponType);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		}

		/** Whether everything was made. Reports why not. */
		bool IsUsable(FAutomationTestBase& Test) const
		{
			if (!Player || !System || !Slots)
			{
				Test.AddError(TEXT("set-up: a possessed player character with an ability system and weapon slots"));
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
		void Holds(const TArray<FTyrantsUltimateRow>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FTyrantsUltimateRow& Each : Rows)
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

		/**
		 * This player's Tyrant's Minion at `Where`, summoned by the six-piece's own call with its seconds held for that
		 * call alone, or null with the reason reported. The stat line is left empty afterwards.
		 */
		ACataclysmMinion* Summons(FAutomationTestBase& Test, const FVector& Where) const
		{
			FTyrantsUltimateRow Seconds;
			Seconds.Stat = FName(ACataclysmMinion::TyrantsMinionSecondsStat);
			Seconds.Value = 60.0f;
			Holds({Seconds});
			ACataclysmMinion* Made =
				ACataclysmMinion::SummonTyrantsMinion(Player, /*bLostWasTyrantsMinion=*/false, Where);
			Holds({});
			if (!Made || !Made->bIsTyrantsMinion)
			{
				Test.AddError(TEXT("set-up: no Tyrant's Minion was summoned. DT_MinionTypes needs the TyrantsMinion row: "
								   "run tools/generate_datatable_assets.py"));
				return nullptr;
			}
			return Made;
		}

		/** The key of `Slot` goes down: one frame of the press, through the path the controller takes. */
		void KeyDown(ECataclysmAbilitySlot Slot) const
		{
			System->AbilityInputTagPressed(CataclysmAbilitySlots::Tag(Slot));
			System->ProcessAbilityInput();
		}

		/** The key of `Slot` comes up. */
		void KeyUp(ECataclysmAbilitySlot Slot) const
		{
			System->AbilityInputTagReleased(CataclysmAbilitySlots::Tag(Slot));
			System->ProcessAbilityInput();
		}

		/** A free start of this row from the player's own place, at a point: the control cast. */
		bool CastsFromItsOwnPlace(const FCataclysmWeaponSkill& Skill, const FVector& Aim) const
		{
			const FVector Here = Player->GetActorLocation();
			return UCataclysmTriggeredSkill::Trigger(Player, Skill, Aim, /*DamageShare=*/1.0f, /*bPaysCost=*/false,
													 &Here);
		}

		/** Move the player. */
		void StandsAt(const FVector& Where) const
		{
			Player->SetActorLocation(Where, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr,
									 ETeleportType::TeleportPhysics);
		}

		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
	};

	/**
	 * Pins `Cataclysm.CritRoll` for the life of the scope and puts back what it was. Ruled 2026-10-18 by the coordinating
	 * session: a comparison of two blows' figures must not rest on neither of them critically striking. Every blow a
	 * defender takes reads the pin (`UCataclysmVitalAttributeSet`). A roll of 100 never critically strikes.
	 */
	struct FTyrantsUltimateCritRoll
	{
		explicit FTyrantsUltimateCritRoll(float Roll)
			: Variable(IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.CritRoll")))
		{
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}
		~FTyrantsUltimateCritRoll()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FTyrantsUltimateEventCount
	{
		FTyrantsUltimateEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
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
		~FTyrantsUltimateEventCount()
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
	void TyrantsUltimatePass(UWorld* World, float Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/** Run the minion's quarter-second step for the ten-piece, as `ACataclysmCharacterBase` calls it. */
	bool TyrantsUltimateStep(ACataclysmMinion* Minion)
	{
		return ACataclysmMinion::TyrantsMinionStep(Minion);
	}

	/** How many skill-used notices the world has sent. */
	uint32 TyrantsUltimateNoticesSent(UWorld* World)
	{
		const UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
		return Events ? Events->SkillUsesSent() : 0;
	}

	/**
	 * A weapon skill table whose Demonic Whip holds Coil of Embers' self buff in its ULTIMATE slot, or null with the
	 * import's problems reported. No shipped Ultimate is a self buff; the shape and figures are the shipped Coil's.
	 */
	UDataTable* TyrantsUltimateSelfBuffTable(FAutomationTestBase& Test)
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmWeaponSkillRow::StaticStruct();
		const TArray<FString> Problems = Table->CreateTableFromCSVString(TEXT(
			"Name,WeaponType,DamageType,Slot,SkillName,SkillDescription,Tags,Shape,ShapeParams,CritChancePercent,"
			"DamagePercent,Cooldown,ManaCost\r\n"
			"Demonic_Whip_Ultimate,Whip,Demonic,Ultimate,Tyrants Test Coil,A self buff in the Ultimate slot.,"
			"\"Item.Weapon.Whip, Element.Demonic, Type.Buff, Slot.Ultimate\",SelfBuff,"
			"Duration=10; Interval=1; Radius=3; RangeIncrease=30; Knockback=3,-1,-1,-1,-1\r\n"));
		if (Problems.Num() > 0)
		{
			for (const FString& Problem : Problems)
			{
				Test.AddError(FString::Printf(TEXT("set-up: the self-buff table: %s"), *Problem));
			}
			return nullptr;
		}
		return Table;
	}

	/** How many self buffs `Minion` granted to `Rig`'s player still run. */
	int32 TyrantsUltimateBuffsRunningFrom(const FTyrantsUltimateRig& Rig, const ACataclysmMinion* Minion)
	{
		int32 Running = 0;
		for (const FGameplayAbilitySpec& Spec : Rig.System->GetActivatableAbilities())
		{
			const UCataclysmSelfBuffSkill* Buff = Cast<UCataclysmSelfBuffSkill>(Spec.GetPrimaryInstance());
			Running += Buff && Spec.IsActive() && Buff->FreeRepeatCastBy.Get() == Minion ? 1 : 0;
		}
		return Running;
	}
}

// ==========================================================================
// 1. At nought nothing; at 3, the Ultimate from the minion's place at an enemy within its reach, every 3 s (T2, T5, T6)
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTyrantTenCastsEveryThreeSecondsTest,
	"Cataclysm.SetBonus.Tyrant10.AtNoughtNothingIsCastAndAtThreeTheUltimateIsCastFromTheMinionEveryThreeSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmTyrantTenCastsEveryThreeSecondsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSetBonusTyrantTenTest;

	// THE PLAYER AT THE ORIGIN WITH A CONTROL ENEMY 5 M ALONG X; ITS TYRANT'S MINION 30 M ALONG Y, beyond the 14 m strike
	// of the player's own place. AN ENEMY 16 M FROM THE MINION, beyond the strike's 14 m, then one 3 m from it.
	const FTyrantsUltimateCritRoll NeverCritical(100.0f);
	if (!TestNotNull(TEXT("set-up: the critical strike roll is pinned to 100, so no blow here critically strikes"),
			NeverCritical.Variable))
	{
		return false;
	}
	FTyrantsUltimateWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FTyrantsUltimateRig Rig(World, FVector::ZeroVector);
	FCataclysmWeaponSkill Gathering;
	if (!Rig.IsUsable(*this) || !TestTrue(TEXT("set-up: the whip holds an Ultimate"),
			Rig.HeldRow(ECataclysmAbilitySlot::Ultimate, Gathering)))
	{
		return false;
	}
	const FVector At(0.0f, 30.0f * M, 0.0f);
	ACataclysmMinion* Minion = Rig.Summons(*this, At);
	ACataclysmEnemyCharacter* Far = TyrantsUltimateEnemy(World, At + FVector(0.0f, 16.0f * M, 0.0f));
	ACataclysmEnemyCharacter* Control = TyrantsUltimateEnemy(World, FVector(5.0f * M, 0.0f, 0.0f));
	if (!Minion || !TestTrue(TEXT("set-up: two enemies"), Far && Control))
	{
		return false;
	}
	TestTrue(*FString::Printf(TEXT("set-up: the minion aims by The Gathering's own reach: %.1f cm"),
							  Minion->SkillGadgetReachCm(Gathering)),
		FMath::IsNearlyEqual(Minion->SkillGadgetReachCm(Gathering), 14.0f * M, 0.01f));

	// THE CONTROL: with the stat at nought, nothing.
	TestEqual(TEXT("control: holding no row, the minion's seconds between casts are nought"),
		Minion->TyrantsUltimateSecondsBetweenCasts(), 0.0f, 0.0001f);
	TestFalse(TEXT("control: and its step casts nothing"), TyrantsUltimateStep(Minion));
	TestEqual(TEXT("control: no cast is counted"), Minion->TyrantsUltimateCastsMade, 0);

	// THE ROW AT 3, AND ONLY AN ENEMY BEYOND THE STRIKE'S REACH: nothing.
	Rig.Holds({TyrantsUltimateEvery()});
	TestEqual(TEXT("with the row at 3 the minion's seconds between casts are 3"),
		Minion->TyrantsUltimateSecondsBetweenCasts(), TyrantsUltimateSeconds, 0.0001f);
	TyrantsUltimateStep(Minion);
	TestEqual(TEXT("an enemy 16 m away, beyond The Gathering's 14 m, is not aimed at: no cast"),
		Minion->TyrantsUltimateCastsMade, 0);
	TestEqual(TEXT("and it keeps its health"), TyrantsUltimateLost(Far), 0.0f, 0.001f);

	// THE CONTROL: the player's own free cast of The Gathering from its own place.
	if (!TestTrue(TEXT("set-up: the player's own cast starts"),
			Rig.CastsFromItsOwnPlace(Gathering, Control->GetActorLocation())))
	{
		return false;
	}
	const float OwnTook = TyrantsUltimateLost(Control);
	TestTrue(*FString::Printf(TEXT("control: the player's own cast struck: %.2f"), OwnTook), OwnTook > 0.0f);

	// AN ENEMY 3 M FROM THE MINION, AND 30 M FROM THE PLAYER, beyond the strike's reach of the player's own place.
	ACataclysmEnemyCharacter* Near = TyrantsUltimateEnemy(World, At + FVector(3.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: an enemy beside the minion"), Near))
	{
		return false;
	}
	TestTrue(TEXT("set-up: it stands beyond The Gathering's reach of the player"),
		FVector::Dist2D(Near->GetActorLocation(), Rig.Player->GetActorLocation()) > 14.0f * M + 1.0f);
	TestTrue(TEXT("with an enemy within the strike's reach of the minion, its step casts"), TyrantsUltimateStep(Minion));
	TestEqual(TEXT("once"), Minion->TyrantsUltimateCastsMade, 1);
	TestEqual(TEXT("the cast took what the player's own cast takes: the player's Ultimate, at the player's numbers, "
				   "from the minion's place"), TyrantsUltimateLost(Near), OwnTook, 0.01f);
	TestEqual(TEXT("the enemy beyond the reach is still untouched"), TyrantsUltimateLost(Far), 0.0f, 0.001f);
	TestEqual(TEXT("and the cast was not the minion's own blow"), Minion->AttacksMade, 0);

	// AGAIN 3 S AFTER THE LAST CAST STARTED, AND NOT BEFORE.
	TyrantsUltimatePass(World, TyrantsUltimateSeconds - 0.1f);
	TyrantsUltimateStep(Minion);
	TestEqual(TEXT("a tenth of a second short of 3 s, nothing"), Minion->TyrantsUltimateCastsMade, 1);
	TyrantsUltimatePass(World, 0.1f);
	TyrantsUltimateStep(Minion);
	TestEqual(TEXT("at 3 s, the second cast"), Minion->TyrantsUltimateCastsMade, 2);
	return true;
}

// ==========================================================================
// 2. A free start: no cost, no cooldown waited for or started, no skill_use or attack_use (#7, T7)
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTyrantTenFreeStartTest,
	"Cataclysm.SetBonus.Tyrant10.TheCastCostsNothingWaitsForAndStartsNoCooldownAndRaisesNoSkillOrAttackUse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmTyrantTenFreeStartTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSetBonusTyrantTenTest;

	// TWO PLAYERS 60 M APART, each with its minion 20 M ALONG Y and an enemy 3 m from the minion.
	FTyrantsUltimateWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector PressedHome(60.0f * M, 0.0f, 0.0f);
	FTyrantsUltimateRig Free(World, FVector::ZeroVector);
	FTyrantsUltimateRig Pressed(World, PressedHome);
	if (!Free.IsUsable(*this) || !Pressed.IsUsable(*this))
	{
		return false;
	}
	const FVector FreeAt(0.0f, 20.0f * M, 0.0f);
	const FVector PressedAt = PressedHome + FVector(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* FreeMinion = Free.Summons(*this, FreeAt);
	ACataclysmMinion* PressedMinion = Pressed.Summons(*this, PressedAt);
	ACataclysmEnemyCharacter* FreeEnemy = TyrantsUltimateEnemy(World, FreeAt + FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* PressedEnemy = TyrantsUltimateEnemy(World, PressedAt + FVector(3.0f * M, 0.0f, 0.0f));
	if (!FreeMinion || !PressedMinion || !TestTrue(TEXT("set-up: two enemies"), FreeEnemy && PressedEnemy))
	{
		return false;
	}
	Free.Holds({TyrantsUltimateEvery()});
	Pressed.Holds({TyrantsUltimateEvery()});
	const FGameplayAttribute Mana = UCataclysmVitalAttributeSet::GetManaAttribute();
	const FGameplayTag UltimateCooldown = UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Ultimate);

	// THE FREE START, ON A PLAYER WHOSE ULTIMATE HAS NO COOLDOWN RUNNING.
	FTyrantsUltimateEventCount SkillUses(Free.System, TEXT("skill_use"));
	FTyrantsUltimateEventCount AttackUses(Free.System, TEXT("attack_use"));
	FTyrantsUltimateEventCount HitsDealt(Free.System, TEXT("hit_dealt"));
	const uint32 NoticesBefore = TyrantsUltimateNoticesSent(World);
	TestTrue(TEXT("the minion casts the player's Ultimate"), TyrantsUltimateStep(FreeMinion));
	TestTrue(TEXT("and it strikes the enemy"), TyrantsUltimateLost(FreeEnemy) > 0.0f);
	TestEqual(TEXT("the player's mana is unchanged: the cast costs nothing"), Free.Get(Mana), 1000.0f, 0.001f);
	TestFalse(TEXT("no Ultimate cooldown runs on the player: the cast starts none"),
		Free.System->HasMatchingGameplayTag(UltimateCooldown));
	TestEqual(TEXT("no skill_use was raised"), SkillUses.Count, 0);
	TestEqual(TEXT("no attack_use was raised"), AttackUses.Count, 0);
	TestEqual(TEXT("and no skill-used notice was sent"), TyrantsUltimateNoticesSent(World), NoticesBefore);
	TestTrue(*FString::Printf(TEXT("its blow is the player's: hit_dealt was raised on the player %d times"),
							  HitsDealt.Count),
		HitsDealt.Count >= 1);

	// THE CONTROL: the player's own press of the Ultimate pays and starts its cooldown.
	Pressed.KeyDown(ECataclysmAbilitySlot::Ultimate);
	TestTrue(TEXT("control: the player's own press of the Ultimate starts its cooldown"),
		Pressed.System->HasMatchingGameplayTag(UltimateCooldown));
	TestTrue(TEXT("control: and pays mana"), Pressed.Get(Mana) < 1000.0f);
	Pressed.KeyUp(ECataclysmAbilitySlot::Ultimate);

	// AND WITH THAT COOLDOWN RUNNING, THE MINION STILL CASTS: it waits for none.
	TestTrue(TEXT("with the player's Ultimate cooldown running, the minion still casts it"),
		TyrantsUltimateStep(PressedMinion));
	TestTrue(TEXT("and it strikes the enemy"), TyrantsUltimateLost(PressedEnemy) > 0.0f);
	return true;
}

// ==========================================================================
// 3. Its blows are the player's: no minion or gadget row reaches them, and it is no skill gadget (T1)
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTyrantTenPlayersNumbersTest,
	"Cataclysm.SetBonus.Tyrant10.ItsBlowsAreThePlayersAndNoMinionOrGadgetRowReachesThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmTyrantTenPlayersNumbersTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSetBonusTyrantTenTest;

	// TWO PLAYERS 60 M APART, ONE HOLDING THE TWO-PIECE'S ROW (minion_damage 50% increased, no tag) AND THE GADGET DAMAGE
	// ROW ON BOTH STATS (30% increased, requiring Type.Deployable); each minion 20 m along Y with an enemy 1.5 m from it.
	// A THIRD PLAYER 120 M ALONG X places a Heavy skill gadget, the control of `SkillGadgetCasting`.
	const FTyrantsUltimateCritRoll NeverCritical(100.0f);
	if (!TestNotNull(TEXT("set-up: the critical strike roll is pinned to 100, so no blow here critically strikes"),
			NeverCritical.Variable))
	{
		return false;
	}
	FTyrantsUltimateWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector RowedHome(60.0f * M, 0.0f, 0.0f);
	const FVector GadgetHome(120.0f * M, 0.0f, 0.0f);
	FTyrantsUltimateRig Plain(World, FVector::ZeroVector);
	FTyrantsUltimateRig Rowed(World, RowedHome);
	FTyrantsUltimateRig Gadgets(World, GadgetHome);
	if (!Plain.IsUsable(*this) || !Rowed.IsUsable(*this) || !Gadgets.IsUsable(*this))
	{
		return false;
	}
	const FVector PlainAt(0.0f, 20.0f * M, 0.0f);
	const FVector RowedAt = RowedHome + FVector(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* PlainMinion = Plain.Summons(*this, PlainAt);
	ACataclysmMinion* RowedMinion = Rowed.Summons(*this, RowedAt);
	ACataclysmEnemyCharacter* PlainEnemy = TyrantsUltimateEnemy(World, PlainAt + FVector(1.5f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* RowedEnemy = TyrantsUltimateEnemy(World, RowedAt + FVector(1.5f * M, 0.0f, 0.0f));
	if (!PlainMinion || !RowedMinion || !TestTrue(TEXT("set-up: two enemies"), PlainEnemy && RowedEnemy))
	{
		return false;
	}
	Plain.Holds({TyrantsUltimateEvery()});
	Rowed.Holds({TyrantsUltimateEvery(), TyrantsUltimateIncrease(TEXT("minion_damage"), 50.0f),
				 TyrantsUltimateIncrease(TEXT("attack_damage"), 30.0f, TEXT("Type.Deployable")),
				 TyrantsUltimateIncrease(TEXT("spell_damage"), 30.0f, TEXT("Type.Deployable"))});

	// THE CONTROL: the two-piece's row is live, because it raises the Tyrant's Minion's own blow.
	PlainMinion->AttackTarget(PlainEnemy);
	RowedMinion->AttackTarget(RowedEnemy);
	const float PlainBlow = TyrantsUltimateLost(PlainEnemy);
	const float RowedBlow = TyrantsUltimateLost(RowedEnemy);
	TestTrue(*FString::Printf(TEXT("control: the minion's own blow lands: %.2f"), PlainBlow), PlainBlow > 0.0f);
	TestTrue(*FString::Printf(TEXT("control: the two-piece's row raises the minion's own blow: %.2f against %.2f"),
							  RowedBlow, PlainBlow),
		RowedBlow > PlainBlow * 1.4f);

	// THE ULTIMATE: THE SAME FIGURE FOR BOTH.
	TestTrue(TEXT("set-up: the plain player's minion casts"), TyrantsUltimateStep(PlainMinion));
	TestTrue(TEXT("set-up: the rowed player's minion casts"), TyrantsUltimateStep(RowedMinion));
	const float PlainCast = TyrantsUltimateLost(PlainEnemy) - PlainBlow;
	const float RowedCast = TyrantsUltimateLost(RowedEnemy) - RowedBlow;
	TestTrue(*FString::Printf(TEXT("the plain player's Ultimate struck: %.2f"), PlainCast), PlainCast > 0.0f);
	TestEqual(TEXT("the player holding the minion and gadget damage rows gets the same figure: no tag is added and no "
				   "minion row reaches the cast"), RowedCast, PlainCast, 0.01f);

	// AND IT IS NO SKILL GADGET: a blow it carries is answered null, where a skill gadget's is answered with the gadget.
	Gadgets.Holds({TyrantsUltimateConverts(TEXT("Slot.Heavy"))});
	ACataclysmMinion* Gadget =
		ACataclysmMinion::PlaceSkillGadget(Gadgets.Player, ECataclysmAbilitySlot::Heavy,
										   GadgetHome + FVector(0.0f, 20.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a Heavy skill gadget (DT_MinionTypes needs the SkillGadget row)"), Gadget))
	{
		return false;
	}
	FCataclysmHitDelivery FromTheGadget;
	FromTheGadget.CastBy = Gadget;
	TestTrue(TEXT("control: a blow a skill gadget carries is the skill gadget's"),
		ACataclysmMinion::SkillGadgetCasting(FromTheGadget) == Gadget);
	FCataclysmHitDelivery FromTheMinion;
	FromTheMinion.CastBy = RowedMinion;
	TestNull(TEXT("a blow the Tyrant's Minion carries is answered null by SkillGadgetCasting, so it is priced as the "
				  "player's"), ACataclysmMinion::SkillGadgetCasting(FromTheMinion));
	TestFalse(TEXT("and the Tyrant's Minion is no skill gadget"), RowedMinion->IsSkillGadget());
	return true;
}

// ==========================================================================
// 4. An Ultimate converted into a skill gadget is still cast (T3)
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTyrantTenConvertedUltimateTest,
	"Cataclysm.SetBonus.Tyrant10.AnUltimateConvertedIntoASkillGadgetIsStillCast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmTyrantTenConvertedUltimateTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSetBonusTyrantTenTest;

	FTyrantsUltimateWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FTyrantsUltimateRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	const FVector At(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* Minion = Rig.Summons(*this, At);
	ACataclysmEnemyCharacter* Enemy = TyrantsUltimateEnemy(World, At + FVector(3.0f * M, 0.0f, 0.0f));
	if (!Minion || !TestNotNull(TEXT("set-up: an enemy"), Enemy))
	{
		return false;
	}

	// THE CONTROL: the Ultimate slot is not converted while no row says so.
	Rig.Holds({TyrantsUltimateEvery()});
	TestFalse(TEXT("control: holding the ten-piece row alone, the Ultimate slot is not converted"),
		Rig.System->SlotIsASkillGadget(ECataclysmAbilitySlot::Ultimate));

	Rig.Holds({TyrantsUltimateEvery(), TyrantsUltimateConverts(TEXT("Slot.Ultimate"))});
	TestTrue(TEXT("set-up: with the gadget row the Ultimate slot is converted"),
		Rig.System->SlotIsASkillGadget(ECataclysmAbilitySlot::Ultimate));
	TestTrue(TEXT("the converted Ultimate is still cast by the Tyrant's Minion"), TyrantsUltimateStep(Minion));
	TestEqual(TEXT("once"), Minion->TyrantsUltimateCastsMade, 1);
	TestTrue(TEXT("and it strikes the enemy"), TyrantsUltimateLost(Enemy) > 0.0f);
	return true;
}

// ==========================================================================
// 5. A self-buff Ultimate: within 8 m of the minion and not beyond, and not while one it granted runs (T4)
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTyrantTenSelfBuffTest,
	"Cataclysm.SetBonus.Tyrant10.ASelfBuffUltimateIsGrantedWithinEightMetresOfTheMinionAndNotWhileOneItGrantedRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmTyrantTenSelfBuffTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSetBonusTyrantTenTest;

	// THE MINION 20 M ALONG Y. The player stands 7.5 m from it, then 9 m, then 7.5 m again.
	FTyrantsUltimateWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	UDataTable* Table = TyrantsUltimateSelfBuffTable(*this);
	if (!Table)
	{
		return false;
	}
	FTyrantsUltimateRig Rig(World, FVector::ZeroVector, TEXT("Demonic"), TEXT("Whip"), Table);
	FCataclysmWeaponSkill Coil;
	if (!Rig.IsUsable(*this) || !TestTrue(TEXT("set-up: the table's whip holds an Ultimate"),
			Rig.HeldRow(ECataclysmAbilitySlot::Ultimate, Coil))
		|| !TestTrue(TEXT("set-up: and it is a self buff"), Coil.Shape == ECataclysmSkillShape::SelfBuff))
	{
		return false;
	}
	const FVector At(0.0f, 20.0f * M, 0.0f);
	const FVector Within(0.0f, 12.5f * M, 0.0f);
	const FVector Beyond(0.0f, 11.0f * M, 0.0f);
	ACataclysmMinion* Minion = Rig.Summons(*this, At);
	if (!Minion)
	{
		return false;
	}
	TestTrue(TEXT("set-up: a minion may cast a self-buff Ultimate"), ACataclysmMinion::SkillGadgetMayCast(Coil));

	// THE CONTROL: holding no row, 7.5 m from the minion, the player is granted nothing.
	Rig.StandsAt(Within);
	TyrantsUltimateStep(Minion);
	TestEqual(TEXT("control: holding no row, nothing is granted"), Minion->TyrantsUltimateCastsMade, 0);

	Rig.Holds({TyrantsUltimateEvery()});
	Rig.StandsAt(Beyond);
	TyrantsUltimateStep(Minion);
	TestEqual(TEXT("9 m from the minion, beyond 8 m, the player is granted nothing"), Minion->TyrantsUltimateCastsMade, 0);
	TestEqual(TEXT("and no self buff of the minion's runs"), TyrantsUltimateBuffsRunningFrom(Rig, Minion), 0);

	Rig.StandsAt(Within);
	TyrantsUltimateStep(Minion);
	TestEqual(TEXT("7.5 m from the minion, within 8 m, the self buff is cast"), Minion->TyrantsUltimateCastsMade, 1);
	TestEqual(TEXT("and runs on the player, cast by the minion"), TyrantsUltimateBuffsRunningFrom(Rig, Minion), 1);

	// ITS 3 S RUN WHILE THE FIRST STILL RUNS: no second copy.
	TyrantsUltimatePass(World, TyrantsUltimateSeconds + 0.5f);
	TyrantsUltimateStep(Minion);
	TestEqual(TEXT("its 3 s run while the first buff still runs, no second is granted"),
		Minion->TyrantsUltimateCastsMade, 1);
	TestEqual(TEXT("and one runs"), TyrantsUltimateBuffsRunningFrom(Rig, Minion), 1);
	return true;
}

// ==========================================================================
// 6. The 3 s is fixed: no Ultimate cooldown row and no minion_attack_speed row shortens it (T6)
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTyrantTenFixedClockTest,
	"Cataclysm.SetBonus.Tyrant10.TheThreeSecondsAreNotShortenedByACooldownRowOrMinionAttackSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmTyrantTenFixedClockTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSetBonusTyrantTenTest;

	FTyrantsUltimateWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FTyrantsUltimateRig Rig(World, FVector::ZeroVector);
	FCataclysmWeaponSkill Gathering;
	if (!Rig.IsUsable(*this) || !TestTrue(TEXT("set-up: the whip holds an Ultimate"),
			Rig.HeldRow(ECataclysmAbilitySlot::Ultimate, Gathering)))
	{
		return false;
	}
	const FVector At(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* Minion = Rig.Summons(*this, At);
	ACataclysmEnemyCharacter* Enemy = TyrantsUltimateEnemy(World, At + FVector(3.0f * M, 0.0f, 0.0f));
	if (!Minion || !TestNotNull(TEXT("set-up: an enemy"), Enemy))
	{
		return false;
	}
	FTyrantsUltimateRow Reduction;
	Reduction.Stat = FName(TEXT("cooldown_reduction"));
	Reduction.Value = 50.0f;
	Rig.Holds({TyrantsUltimateEvery(), Reduction, TyrantsUltimateIncrease(TEXT("minion_attack_speed"), 50.0f)});

	// THE CONTROLS: the cooldown row shortens the player's own Ultimate, and the attack speed row reaches the minion.
	const float Base = UCataclysmSkillSlots::NumbersFor(
		UCataclysmSkillSlots::LoadGeneratedTable(), ECataclysmAbilitySlot::Ultimate).Cooldown;
	const float AfterRows = UCataclysmGameplayAbility::CooldownAfterReduction(Rig.System, Base, Gathering.Tags);
	TestTrue(*FString::Printf(TEXT("control: the reduction row shortens the Ultimate cooldown from %.3f to %.3f"),
							  Base, AfterRows),
		AfterRows > 0.0f && AfterRows < Base - 0.01f);
	const float Faster = Rig.System->IncreasesForStat(FName(TEXT("minion_attack_speed")), Minion->TypeTags);
	TestTrue(*FString::Printf(TEXT("control: the attack speed row reaches the minion's tags: %.3f"), Faster),
		Faster > 0.4f);

	// AND THE MINION'S 3 S IS NEITHER.
	TestEqual(TEXT("the minion's seconds between casts are still 3"),
		Minion->TyrantsUltimateSecondsBetweenCasts(), TyrantsUltimateSeconds, 0.0001f);
	TestTrue(TEXT("set-up: the first cast"), TyrantsUltimateStep(Minion));
	TyrantsUltimatePass(World, TyrantsUltimateSeconds / 1.5f);
	TyrantsUltimateStep(Minion);
	TestEqual(TEXT("at 3 s over one and a half, which the attack speed row would give, nothing"),
		Minion->TyrantsUltimateCastsMade, 1);
	TyrantsUltimatePass(World, TyrantsUltimateSeconds - TyrantsUltimateSeconds / 1.5f - 0.1f);
	TyrantsUltimateStep(Minion);
	TestEqual(TEXT("a tenth of a second short of 3 s, nothing"), Minion->TyrantsUltimateCastsMade, 1);
	TyrantsUltimatePass(World, 0.1f);
	TyrantsUltimateStep(Minion);
	TestEqual(TEXT("at 3 s, the second cast"), Minion->TyrantsUltimateCastsMade, 2);
	return true;
}

// ==========================================================================
// 7. A summon, a movement skill, a held skill and an empty slot are never cast (T2)
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTyrantTenRefusedTest,
	"Cataclysm.SetBonus.Tyrant10.ASummonAMovementSkillOrAHeldUltimateIsNeverCastAndNeitherIsAnEmptySlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmTyrantTenRefusedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSetBonusTyrantTenTest;

	// FIVE PLAYERS 60 M APART ALONG X, each with its minion 20 m along Y and an enemy 3 m from the minion: the whip, the
	// control; Subjugate, a summon; Everywhere at Once, a movement skill; The Whole Weight, held for 3 s; and a
	// Celestial Sword, whose Ultimate row names no skill.
	FTyrantsUltimateWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const TCHAR* Weapons[5][3] = {
		{TEXT("Demonic"), TEXT("Whip"), TEXT("the whip's The Gathering, the control")},
		{TEXT("Demonic"), TEXT("Staff"), TEXT("the staff's Subjugate, a summon")},
		{TEXT("Demonic"), TEXT("Dagger"), TEXT("the dagger's Everywhere at Once, a movement skill")},
		{TEXT("Demonic"), TEXT("Greatsword"), TEXT("the greatsword's The Whole Weight, held for 3 s")},
		{TEXT("Celestial"), TEXT("Sword"), TEXT("the Celestial Sword, whose Ultimate names no skill")},
	};
	TArray<TUniquePtr<FTyrantsUltimateRig>> Rigs;
	TArray<ACataclysmMinion*> Minions;
	TArray<ACataclysmEnemyCharacter*> Enemies;
	for (int32 Each = 0; Each < 5; ++Each)
	{
		const FVector Home(60.0f * M * Each, 0.0f, 0.0f);
		Rigs.Add(MakeUnique<FTyrantsUltimateRig>(World, Home, Weapons[Each][0], Weapons[Each][1]));
		if (!Rigs.Last()->IsUsable(*this))
		{
			return false;
		}
		const FVector At = Home + FVector(0.0f, 20.0f * M, 0.0f);
		Minions.Add(Rigs.Last()->Summons(*this, At));
		Enemies.Add(TyrantsUltimateEnemy(World, At + FVector(3.0f * M, 0.0f, 0.0f)));
		if (!Minions.Last() || !TestNotNull(TEXT("set-up: an enemy"), Enemies.Last()))
		{
			return false;
		}
		Rigs.Last()->Holds({TyrantsUltimateEvery()});
	}

	// WHAT EACH HOLDS.
	FCataclysmWeaponSkill Held[4];
	for (int32 Each = 0; Each < 4; ++Each)
	{
		if (!TestTrue(*FString::Printf(TEXT("set-up: %s is held in the Ultimate slot"), Weapons[Each][2]),
				Rigs[Each]->HeldRow(ECataclysmAbilitySlot::Ultimate, Held[Each])))
		{
			return false;
		}
	}
	FCataclysmWeaponSkill Nothing;
	TestFalse(TEXT("set-up: the Celestial Sword's Ultimate slot holds no skill"),
		Rigs[4]->HeldRow(ECataclysmAbilitySlot::Ultimate, Nothing));
	TestTrue(TEXT("set-up: Subjugate is a summon"), Held[1].Shape == ECataclysmSkillShape::Summon);
	TestTrue(TEXT("set-up: Everywhere at Once is a movement skill"), Held[2].Shape == ECataclysmSkillShape::Movement);
	TestTrue(TEXT("set-up: The Whole Weight is held before it lands"), Held[3].Params.ChargeTime > 0.0f);

	// THE CONTROL: the whip's Ultimate may be cast, and is.
	TestTrue(TEXT("control: a minion may cast The Gathering"), ACataclysmMinion::SkillGadgetMayCast(Held[0]));
	TestTrue(TEXT("control: and the whip player's minion casts it"), TyrantsUltimateStep(Minions[0]));

	for (int32 Each = 1; Each < 5; ++Each)
	{
		if (Each < 4)
		{
			TestFalse(*FString::Printf(TEXT("%s is refused as a skill gadget refuses it"), Weapons[Each][2]),
				ACataclysmMinion::SkillGadgetMayCast(Held[Each]));
		}
		TyrantsUltimateStep(Minions[Each]);
		TyrantsUltimatePass(World, TyrantsUltimateSeconds);
		TyrantsUltimateStep(Minions[Each]);
		TestEqual(*FString::Printf(TEXT("%s: the minion casts nothing on two steps 3 s apart"), Weapons[Each][2]),
			Minions[Each]->TyrantsUltimateCastsMade, 0);
		TestEqual(*FString::Printf(TEXT("%s: and the enemy keeps its health"), Weapons[Each][2]),
			TyrantsUltimateLost(Enemies[Each]), 0.0f, 0.001f);
	}
	return true;
}

// ==========================================================================
// 8. An Imp and a skill gadget cast nothing new, and the gadget's own casts are unchanged
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTyrantTenOnlyItsMinionTest,
	"Cataclysm.SetBonus.Tyrant10.AnImpAndASkillGadgetCastNothingNewAndTheGadgetsOwnCastsAreUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmTyrantTenOnlyItsMinionTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSetBonusTyrantTenTest;

	// ONE PLAYER AT THE ORIGIN HOLDING THE TEN-PIECE ROW AND THE HEAVY SLOT'S GADGET ROW. ITS TYRANT'S MINION 30 M ALONG
	// Y, AN IMP 30 M ALONG X AND ITS HEAVY SKILL GADGET 30 M ALONG -Y, each with an enemy 3 m from it.
	FTyrantsUltimateWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FTyrantsUltimateRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	const FVector MinionAt(0.0f, 30.0f * M, 0.0f);
	const FVector ImpAt(30.0f * M, 0.0f, 0.0f);
	const FVector GadgetAt(0.0f, -30.0f * M, 0.0f);
	ACataclysmMinion* Minion = Rig.Summons(*this, MinionAt);
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Rig.Player, ImpAt, /*Lifetime=*/60.0f, /*bBurns=*/false,
													FString(TEXT("Imp")));
	Rig.Holds({TyrantsUltimateEvery(), TyrantsUltimateConverts(TEXT("Slot.Heavy"))});
	ACataclysmMinion* Gadget = ACataclysmMinion::PlaceSkillGadget(Rig.Player, ECataclysmAbilitySlot::Heavy, GadgetAt);
	ACataclysmEnemyCharacter* MinionEnemy = TyrantsUltimateEnemy(World, MinionAt + FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* ImpEnemy = TyrantsUltimateEnemy(World, ImpAt + FVector(0.0f, 3.0f * M, 0.0f));
	ACataclysmEnemyCharacter* GadgetEnemy = TyrantsUltimateEnemy(World, GadgetAt + FVector(3.0f * M, 0.0f, 0.0f));
	if (!Minion || !TestNotNull(TEXT("set-up: an imp"), Imp)
		|| !TestNotNull(TEXT("set-up: a Heavy skill gadget (DT_MinionTypes needs the SkillGadget row)"), Gadget)
		|| !TestTrue(TEXT("set-up: three enemies"), MinionEnemy && ImpEnemy && GadgetEnemy))
	{
		return false;
	}
	TestFalse(TEXT("set-up: the imp is no Tyrant's Minion"), Imp->bIsTyrantsMinion);
	TestFalse(TEXT("set-up: nor is the gadget"), Gadget->bIsTyrantsMinion);

	// THE CONTROL: the player's Tyrant's Minion casts.
	TestTrue(TEXT("control: the Tyrant's Minion's step casts"), TyrantsUltimateStep(Minion));

	// THE IMP AND THE GADGET, WITH THE SAME PLAYER HOLDING THE ROW.
	TestFalse(TEXT("the imp's step casts nothing for the ten-piece"), TyrantsUltimateStep(Imp));
	TestEqual(TEXT("its count stays nought"), Imp->TyrantsUltimateCastsMade, 0);
	TestEqual(TEXT("and its enemy keeps its health"), TyrantsUltimateLost(ImpEnemy), 0.0f, 0.001f);
	TestEqual(TEXT("the imp's seconds between casts are nought"), Imp->TyrantsUltimateSecondsBetweenCasts(), 0.0f,
		0.0001f);
	TestFalse(TEXT("the skill gadget's step casts nothing for the ten-piece"), TyrantsUltimateStep(Gadget));
	TestEqual(TEXT("its ten-piece count stays nought"), Gadget->TyrantsUltimateCastsMade, 0);
	TestEqual(TEXT("and it has cast nothing for its slot either"), Gadget->SkillGadgetCastsMade, 0);
	TestEqual(TEXT("so its enemy keeps its health"), TyrantsUltimateLost(GadgetEnemy), 0.0f, 0.001f);

	// AND THE GADGET'S OWN CASTS, COUNTED: once on its first step, again at its Heavy clock and not before.
	const float HeavyBase = UCataclysmSkillSlots::NumbersFor(
		UCataclysmSkillSlots::LoadGeneratedTable(), ECataclysmAbilitySlot::Heavy).Cooldown;
	ACataclysmMinion::GadgetStep(Gadget);
	TestEqual(TEXT("the gadget's own first step casts its Heavy once"), Gadget->SkillGadgetCastsMade, 1);
	TestTrue(TEXT("and strikes its enemy"), TyrantsUltimateLost(GadgetEnemy) > 0.0f);
	TestEqual(TEXT("and still no ten-piece cast"), Gadget->TyrantsUltimateCastsMade, 0);
	float Remaining = 0.0f;
	float Clock = 0.0f;
	Gadget->SkillGadgetClock(Remaining, Clock);
	TestEqual(TEXT("its clock is the Heavy slot's cooldown, as before"), Clock, HeavyBase, 0.001f);
	TyrantsUltimatePass(World, HeavyBase - 0.1f);
	ACataclysmMinion::GadgetStep(Gadget);
	TestEqual(TEXT("a tenth of a second short of it, nothing"), Gadget->SkillGadgetCastsMade, 1);
	TyrantsUltimatePass(World, 0.1f);
	ACataclysmMinion::GadgetStep(Gadget);
	TestEqual(TEXT("at it, the second cast"), Gadget->SkillGadgetCastsMade, 2);
	return true;
}

// ==========================================================================
// 9. Nothing while the minion cannot act, or while the player is dead (T7)
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTyrantTenCannotActTest,
	"Cataclysm.SetBonus.Tyrant10.NothingIsCastWhileTheMinionCannotActOrThePlayerIsDead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmTyrantTenCannotActTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSetBonusTyrantTenTest;

	// TWO PLAYERS 60 M APART, each with its minion 20 m along Y and an enemy 3 m from the minion.
	FTyrantsUltimateWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector DyingHome(60.0f * M, 0.0f, 0.0f);
	FTyrantsUltimateRig Stunned(World, FVector::ZeroVector);
	FTyrantsUltimateRig Dying(World, DyingHome);
	if (!Stunned.IsUsable(*this) || !Dying.IsUsable(*this))
	{
		return false;
	}
	const FVector StunnedAt(0.0f, 20.0f * M, 0.0f);
	const FVector DyingAt = DyingHome + FVector(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* StunnedMinion = Stunned.Summons(*this, StunnedAt);
	ACataclysmMinion* DyingMinion = Dying.Summons(*this, DyingAt);
	ACataclysmEnemyCharacter* StunnedEnemy = TyrantsUltimateEnemy(World, StunnedAt + FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* DyingEnemy = TyrantsUltimateEnemy(World, DyingAt + FVector(3.0f * M, 0.0f, 0.0f));
	if (!StunnedMinion || !DyingMinion || !TestTrue(TEXT("set-up: two enemies"), StunnedEnemy && DyingEnemy))
	{
		return false;
	}
	Stunned.Holds({TyrantsUltimateEvery()});
	Dying.Holds({TyrantsUltimateEvery()});

	// THE MINION THAT CANNOT ACT. The control: it casts while it can.
	TestTrue(TEXT("control: a minion able to act casts"), TyrantsUltimateStep(StunnedMinion));
	const float Struck = TyrantsUltimateLost(StunnedEnemy);
	UCataclysmSkillEffects::ApplyStun(StunnedEnemy, StunnedMinion, /*DurationSeconds=*/10.0f, /*DamageDealt=*/0.0f,
									  /*bStunIsDesigned=*/true);
	if (!TestTrue(TEXT("set-up: the minion is stunned and cannot act"), UCataclysmSkillEffects::CannotAct(StunnedMinion)))
	{
		return false;
	}
	TyrantsUltimatePass(World, TyrantsUltimateSeconds);
	TestFalse(TEXT("its 3 s run, a minion that cannot act casts nothing"), TyrantsUltimateStep(StunnedMinion));
	TestEqual(TEXT("its count stays one"), StunnedMinion->TyrantsUltimateCastsMade, 1);
	TestEqual(TEXT("and its enemy loses nothing more"), TyrantsUltimateLost(StunnedEnemy), Struck, 0.001f);

	// THE DEAD PLAYER.
	UCataclysmSkillEffects::MarkDead(Dying.Player);
	TestFalse(TEXT("a dead player's minion casts nothing"), TyrantsUltimateStep(DyingMinion));
	TestEqual(TEXT("so its enemy keeps its health"), TyrantsUltimateLost(DyingEnemy), 0.0f, 0.001f);
	UCataclysmSkillEffects::ClearDead(Dying.Player);
	TestTrue(TEXT("the player alive again, the minion casts"), TyrantsUltimateStep(DyingMinion));
	TestEqual(TEXT("once"), DyingMinion->TyrantsUltimateCastsMade, 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
