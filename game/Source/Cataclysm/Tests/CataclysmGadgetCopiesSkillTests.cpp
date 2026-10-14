// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * "Gadgets copy the last skill you used and fire it at their target once every 2 seconds". Ruled 2026-10-14: the
 * owner's option B, "the WHOLE skill is cast from the gadget (not hits only)", and the coordinating session's rulings
 * on the enchantment session's plan. Beside the four gadget groups of `CataclysmGadgetBehaviourTests.cpp`.
 *
 * THE SUMMONER IS A POSSESSED PLAYER CHARACTER AND NOT A CREATURE, unlike in those four groups, because only a player
 * holds a skill to copy: a Demonic Whip, given a weapon damage of 100 by hand. Its Heavy skill Searing Lash, a 5 m
 * cone of 45 degrees, is what the copies cast. Its Movement skill Reel and its Support skill Coil of Embers are skills a
 * row may not cast again, and its Special skill Tether is one a row may.
 *
 * A USE IS ANNOUNCED BY HAND, as the repeat tests of `CataclysmEnchantmentEffectTests.cpp` announce one: the notice a
 * paid use sends, which is what records the skill. A PRESSED SKILL OF A PLAYER WAITS FOR ITS ATTACK CLIP, and a test
 * world is never ticked, so no test here relies on a pressed skill's blow landing.
 *
 * THE CONTROL CAST "OF THE SUMMONER'S OWN" IS A FREE START OF THE SAME ROW FROM THE SUMMONER'S OWN PLACE, through the
 * same grant a copy uses, given the summoner's location as its place. A start with no place given waits for the
 * player's attack clip; one given a place does not, because no clip was played for it.
 *
 * NO ROW IS AUTHORED, SO THE FLAG IS GRANTED BY HAND, as a flat 1 requiring `Type.Deployable`, which is the row the
 * enchantment session is to write.
 *
 * NOTHING HERE WAITS ON A TICK. The world's clock is moved by hand and a gadget's step is called directly.
 */
namespace CataclysmGadgetCopiesSkillTest
{
	/** Centimetres in a metre, so a case can place a character in metres. */
	constexpr float M = 100.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FWorld
	{
		FWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/**
	 * A real creature of the monsters' side holding `Health`, with none of the defences that would make a blow a
	 * matter of chance: no armour, no evasion, no block and no resistance. It deals nothing.
	 */
	ACataclysmEnemyCharacter* Enemy(UWorld* World, const FVector& Where, float Health)
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
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
		return Made;
	}

	float HealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** A possessed player character holding a Demonic Whip, its ability system, and the rows of four of its skills. */
	struct FRig
	{
		FRig(UWorld* World, const FVector& Where)
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
			bLash = UCataclysmTriggeredSkill::HeldSkillNamed(Player, FName(TEXT("Searing Lash")), Lash);
			bReel = UCataclysmTriggeredSkill::HeldSkillNamed(Player, FName(TEXT("Reel")), Reel);
			bCoil = UCataclysmTriggeredSkill::HeldSkillNamed(Player, FName(TEXT("Coil of Embers")), Coil);
			bTether = UCataclysmTriggeredSkill::HeldSkillNamed(Player, FName(TEXT("Tether")), Tether);
		}

		/** Whether everything was made and the whip's four rows are held. Reports why not. */
		bool IsUsable(FAutomationTestBase& Test) const
		{
			if (!Player || !System || !Slots || !bLash || !bReel || !bCoil || !bTether)
			{
				Test.AddError(TEXT("set-up: a possessed player holding a Demonic Whip, with Searing Lash, Reel, Coil of "
								   "Embers and Tether. If a row is missing, DT_WeaponSkills may be older than "
								   "game/Data/WeaponSkills.csv: run tools/generate_datatable_assets.py"));
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

		/** Tells the world this player paid for this skill, aimed here, as `CommitAndBegin` does. */
		void Uses(const FCataclysmWeaponSkill& Skill, const FVector& Aim) const
		{
			UCataclysmCombatEvents::NoteSkillUsed(Player, Skill.Name, Skill.Tags, Skill.Slot, &Aim,
												  /*bHasCooldown=*/true);
		}

		/** The skill the gadgets would copy now. */
		FName Record() const
		{
			return System->SkillGadgetsCopy();
		}

		/** An increase a test's player holds beside the flag, requiring `Type.Deployable`. */
		struct FIncrease
		{
			FName Stat;
			float Value = 0.0f;
		};

		/**
		 * The row the gadgets read, at a flat 1 requiring `Type.Deployable`, with `MoreIncreases` beside it. REPLACES
		 * THE WHOLE STAT LINE, so everything a test's player holds arrives in one call.
		 */
		void HoldTheFlag(const TArray<FIncrease>& MoreIncreases = TArray<FIncrease>()) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			const auto Add = [&Stats](FName Stat, float Value, ECataclysmStatBucket Bucket)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = Bucket;
				Row.Source = ECataclysmModifierSource::Enchantment;
				Row.Value = Value;
				Row.RequiredTags.AddTag(
					FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Deployable")), /*ErrorIfNotFound=*/false));
				FCataclysmStatInputs& Line = Stats.FindOrAdd(Stat);
				Line.Base = 0.0f;
				Line.Modifiers.Add(Row);
			};
			Add(FName(ACataclysmMinion::GadgetsCopyYourLastSkillStat), 1.0f, ECataclysmStatBucket::Flat);
			for (const FIncrease& Each : MoreIncreases)
			{
				Add(Each.Stat, Each.Value, ECataclysmStatBucket::Increased);
			}
			System->SetStatInputs(MoveTemp(Stats));
		}

		/** A free start of this row from the player's own place, at a point: the control cast. */
		bool CastsFromItsOwnPlace(const FCataclysmWeaponSkill& Skill, const FVector& Aim) const
		{
			const FVector Here = Player->GetActorLocation();
			return UCataclysmTriggeredSkill::Trigger(Player, Skill, Aim, /*DamageShare=*/1.0f, /*bPaysCost=*/false,
													 &Here);
		}

		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
		FCataclysmWeaponSkill Lash;
		FCataclysmWeaponSkill Reel;
		FCataclysmWeaponSkill Coil;
		FCataclysmWeaponSkill Tether;
		bool bLash = false;
		bool bReel = false;
		bool bCoil = false;
		bool bTether = false;
	};

	/** One minion of a named type standing for ten minutes, or null with the reason already reported. */
	ACataclysmMinion* Place(FAutomationTestBase& Test, AActor* Of, const TCHAR* Type, const FVector& Where)
	{
		ACataclysmMinion* Made = ACataclysmMinion::Spawn(Of, Where, /*Lifetime=*/600.0f, /*bBurns=*/false, FString(Type));
		if (!Made || Made->TypeName != FString(Type))
		{
			Test.AddError(FString::Printf(
				TEXT("set-up: DT_MinionTypes could not supply the %s row. Run tools/generate_datatable_assets.py"), Type));
			return nullptr;
		}
		return Made;
	}

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FEventCount
	{
		FEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
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
		~FEventCount()
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
	void Pass(UWorld* World, float Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/** Run the gadget's quarter-second step, as `RegenerationStep` calls it. */
	void Step(ACataclysmMinion* Gadget)
	{
		ACataclysmMinion::GadgetStep(Gadget);
	}

	/** How many skill-used notices the world has sent. */
	uint32 NoticesSent(UWorld* World)
	{
		const UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
		return Events ? Events->SkillUsesSent() : 0;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 1. The record: "the last skill you used"
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetCopiesSkillRecordTest,
	"Cataclysm.GadgetCopiesSkill.TheRecordIsTheLastSkillARowMayCastAgainAndADeathOrALeftDungeonClearsIt")
{
	using namespace CataclysmGadgetCopiesSkillTest;

	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	const FVector Aim(5.0f * M, 0.0f, 0.0f);

	// THE CONTROL, READ FIRST: nothing used, nothing on record.
	TestTrue(TEXT("control: before any use nothing is on record"), Rig.Record().IsNone());
	TestTrue(TEXT("control: the whip's Heavy skill is one a row may cast again"),
		UCataclysmTriggeredSkill::RepeatsFromARow(Rig.Lash));
	TestFalse(TEXT("control: and its Movement skill and its Support skill are not"),
		UCataclysmTriggeredSkill::RepeatsFromARow(Rig.Reel) || UCataclysmTriggeredSkill::RepeatsFromARow(Rig.Coil));

	// AN ACCEPTED SKILL'S USE SETS IT.
	Rig.Uses(Rig.Lash, Aim);
	TestEqual(TEXT("a use of Searing Lash sets the record"), Rig.Record(), FName(TEXT("Searing Lash")));

	// A MOVEMENT SKILL'S USE, A SELF BUFF'S AND THE BASIC ATTACK'S CHANGE NOTHING.
	Rig.Uses(Rig.Reel, Aim);
	TestEqual(TEXT("a use of the movement skill Reel leaves it as it was"), Rig.Record(), FName(TEXT("Searing Lash")));
	Rig.Uses(Rig.Coil, Aim);
	TestEqual(TEXT("a use of the self buff Coil of Embers leaves it as it was"), Rig.Record(),
		FName(TEXT("Searing Lash")));
	const FCataclysmWeaponSkill* Basic = UCataclysmTriggeredSkill::HeldBasicAttack(Rig.Player);
	if (TestNotNull(TEXT("set-up: the whip's basic attack is held"), Basic))
	{
		Rig.Uses(*Basic, Aim);
		TestEqual(TEXT("a use of the basic attack leaves it as it was"), Rig.Record(), FName(TEXT("Searing Lash")));
	}

	// ANOTHER ACCEPTED SKILL'S USE MOVES IT, so the record is the LAST such skill.
	Rig.Uses(Rig.Tether, Aim);
	TestEqual(TEXT("a use of Tether moves the record to Tether"), Rig.Record(), FName(TEXT("Tether")));

	// A FREE CAST OF AN ACCEPTED SKILL DOES NOT: it sends no notice.
	const uint32 NoticesBefore = NoticesSent(World);
	TestTrue(TEXT("set-up: a free start of Searing Lash starts"), Rig.CastsFromItsOwnPlace(Rig.Lash, Aim));
	TestEqual(TEXT("and sends no skill-used notice"), NoticesSent(World), NoticesBefore);
	TestEqual(TEXT("so the record is still Tether"), Rig.Record(), FName(TEXT("Tether")));

	// A DEATH CLEARS IT. `ClearWhatDeathEnds` is what the player's revival runs.
	Rig.System->ClearWhatDeathEnds();
	TestTrue(TEXT("what a death ends, cleared, leaves nothing on record"), Rig.Record().IsNone());

	// IN A DUNGEON: A FLOOR CHANGE KEEPS IT, AND LEAVING THE DUNGEON CLEARS IT. The game mode's floor number is
	// written by hand, as `Cataclysm.DelayedDamage.` does: `GoToFloor` builds a floor, and the game mode's file is not
	// this change's to touch. So this shows the record does not read the floor; not what a floor change does in play.
	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("set-up: the dungeon game mode spawned"), Mode))
	{
		return false;
	}
	Mode->EmpireDungeonId = 7;
	Rig.Uses(Rig.Lash, Aim);
	TestEqual(TEXT("in dungeon 7 a use of Searing Lash sets the record"), Rig.Record(), FName(TEXT("Searing Lash")));
	Mode->FloorNumber = Mode->FloorNumber + 1;
	TestEqual(TEXT("a floor of the same dungeon keeps it"), Rig.Record(), FName(TEXT("Searing Lash")));
	Mode->EmpireDungeonId = INDEX_NONE;
	TestTrue(TEXT("the dungeon left, nothing is on record"), Rig.Record().IsNone());
	Mode->EmpireDungeonId = 8;
	TestTrue(TEXT("and another dungeon entered finds nothing on record either"), Rig.Record().IsNone());
	return true;
}

// ==========================================================================
// 2. A copy is cast from the gadget with the summoner's numbers
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetCopiesSkillFromTheGadgetTest,
	"Cataclysm.GadgetCopiesSkill.ACopyStartsFromTheGadgetAndStrikesItsTargetWithTheSummonersOwnNumbers")
{
	using namespace CataclysmGadgetCopiesSkillTest;

	// THE PLAYER AT THE ORIGIN, ITS BOLT TURRET 20 M ALONG Y, AND THREE ENEMIES EACH HOLDING 50,000. The turret's
	// target 3 m along X from it. An enemy BESIDE THE SUMMONER, 4 m from it on the line from the summoner toward the
	// target, within the whip's 5 m of the summoner and 16 m from the turret. A control enemy 3 m along X from the
	// summoner, as the target stands from the turret, for the summoner's own cast.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	const FVector TurretAt(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* Turret = Place(*this, Rig.Player, TEXT("BoltTurret"), TurretAt);
	ACataclysmEnemyCharacter* Target = Enemy(World, TurretAt + FVector(3.0f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* Beside = Enemy(World, FVector(0.6f * M, 4.0f * M, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* Control = Enemy(World, FVector(3.0f * M, 0.0f, 0.0f), 50000.0f);
	if (!Turret || !TestTrue(TEXT("set-up: three enemies"), Target && Beside && Control))
	{
		return false;
	}
	Rig.Uses(Rig.Lash, Target->GetActorLocation());
	Rig.HoldTheFlag();
	if (!TestEqual(TEXT("set-up: Searing Lash is on record"), Rig.Record(), FName(TEXT("Searing Lash")))
		|| !TestTrue(TEXT("set-up: the turret's brain chooses the target"), Turret->SkillCopyTarget() == Target))
	{
		return false;
	}

	// THE COPY, on the turret's first step with a target within reach.
	Step(Turret);
	if (!TestEqual(TEXT("the turret casts one copy on its step"), Turret->SkillCopiesMade, 1))
	{
		return false;
	}
	const float CopyTook = 50000.0f - HealthOf(Target);
	TestTrue(*FString::Printf(TEXT("the copy struck the turret's target, 3 m from the turret: %.2f"), CopyTook),
		CopyTook > 0.0f);
	TestEqual(TEXT("and not the enemy beside the summoner, out of the whip's reach from the turret"),
		HealthOf(Beside), 50000.0f, 0.001f);
	TestEqual(TEXT("nor the control enemy"), HealthOf(Control), 50000.0f, 0.001f);

	// THE SUMMONER'S OWN CAST OF THE SAME SKILL AT THE CONTROL ENEMY, FROM ITS OWN PLACE: the same figure.
	if (!TestTrue(TEXT("set-up: the summoner's own cast starts"),
			Rig.CastsFromItsOwnPlace(Rig.Lash, Control->GetActorLocation())))
	{
		return false;
	}
	const float OwnTook = 50000.0f - HealthOf(Control);
	TestTrue(*FString::Printf(TEXT("control: the summoner's own cast struck its enemy: %.2f"), OwnTook), OwnTook > 0.0f);
	TestEqual(TEXT("the copy took what the summoner's own cast of the same skill takes"), CopyTook, OwnTook, 0.01f);
	TestEqual(TEXT("and the copy is the summoner's: the turret made no attack of its own"), Turret->AttacksMade, 0);
	return true;
}

// ==========================================================================
// 3. A copy costs nothing
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetCopiesSkillCostsNothingTest,
	"Cataclysm.GadgetCopiesSkill.ACopyCostsNothingStartsNoCooldownAndIsMadeWhileTheSkillIsOnCooldown")
{
	using namespace CataclysmGadgetCopiesSkillTest;

	// THE PLAYER AT THE ORIGIN WITH A CONTROL ENEMY 3 M ALONG X, AND ITS TURRET 20 M ALONG Y WITH A TARGET 3 M ALONG X
	// FROM IT. The player holds 500 of 1,000 mana, its whole health, and 40 class resource.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	const FVector TurretAt(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* Turret = Place(*this, Rig.Player, TEXT("BoltTurret"), TurretAt);
	ACataclysmEnemyCharacter* Target = Enemy(World, TurretAt + FVector(3.0f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* Control = Enemy(World, FVector(3.0f * M, 0.0f, 0.0f), 50000.0f);
	if (!Turret || !TestTrue(TEXT("set-up: two enemies"), Target && Control))
	{
		return false;
	}
	const FGameplayAttribute Mana = UCataclysmVitalAttributeSet::GetManaAttribute();
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute Fervour = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	Rig.Set(Mana, 500.0f);
	Rig.Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
	Rig.Set(Fervour, 40.0f);
	const FGameplayTag HeavyCooldown = UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Heavy);
	Rig.Uses(Rig.Lash, Target->GetActorLocation());
	Rig.HoldTheFlag();

	// THE CONTROL: a free start of the same skill from the summoner's own place, which pays nothing by the rule a
	// repeat follows. What it changes of the class resource is what a hit of it earns, if the character earns any.
	const float FervourBeforeControl = Rig.Get(Fervour);
	if (!TestTrue(TEXT("set-up: the control cast starts"), Rig.CastsFromItsOwnPlace(Rig.Lash, Control->GetActorLocation()))
		|| !TestTrue(TEXT("set-up: and strikes"), HealthOf(Control) < 50000.0f))
	{
		return false;
	}
	const float ControlFervour = Rig.Get(Fervour) - FervourBeforeControl;
	const float ManaHeld = Rig.Get(Mana);
	const float HealthHeld = Rig.Get(Health);
	const float FervourHeld = Rig.Get(Fervour);
	TestFalse(TEXT("control: no Heavy cooldown runs"), Rig.System->HasMatchingGameplayTag(HeavyCooldown));

	// THE COPY.
	Step(Turret);
	if (!TestEqual(TEXT("set-up: the turret casts one copy"), Turret->SkillCopiesMade, 1)
		|| !TestTrue(TEXT("set-up: and it strikes"), HealthOf(Target) < 50000.0f))
	{
		return false;
	}
	TestEqual(TEXT("the copy took no mana"), Rig.Get(Mana), ManaHeld, 0.001f);
	TestEqual(TEXT("and no health"), Rig.Get(Health), HealthHeld, 0.001f);
	TestEqual(TEXT("and its class resource moved by what the control's hit earned, so none was paid"),
		Rig.Get(Fervour) - FervourHeld, ControlFervour, 0.001f);
	TestFalse(TEXT("and started no Heavy cooldown"), Rig.System->HasMatchingGameplayTag(HeavyCooldown));

	// THE SKILL ON COOLDOWN. 2.1 seconds on, the held Searing Lash is pressed, which pays and starts the Heavy
	// cooldown; the swing a player's press waits for is let go of afterwards, since nothing ticks this world. Then
	// the turret's next beat.
	Pass(World, 2.1f);
	FGameplayAbilitySpecHandle Held;
	for (const FGameplayAbilitySpec& Spec : Rig.System->GetActivatableAbilities())
	{
		const UCataclysmSkillTemplate* Skill = Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());
		if (Skill && Skill->SkillName == Rig.Lash.Name
			&& Spec.GetDynamicSpecSourceTags().HasTagExact(CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::Heavy)))
		{
			Held = Spec.Handle;
		}
	}
	if (!TestTrue(TEXT("set-up: the held Searing Lash is pressed"), Held.IsValid() && Rig.System->TryActivateAbility(Held))
		|| !TestTrue(TEXT("set-up: and the Heavy cooldown runs"), Rig.System->HasMatchingGameplayTag(HeavyCooldown)))
	{
		return false;
	}
	Rig.System->CancelAbilityHandle(Held);
	if (!TestTrue(TEXT("set-up: the Heavy cooldown still runs once the press is let go of"),
			Rig.System->HasMatchingGameplayTag(HeavyCooldown)))
	{
		return false;
	}
	const float BeforeTheSecond = HealthOf(Target);
	Step(Turret);
	TestEqual(TEXT("a copy is made 2.1 seconds after the first while the skill is on cooldown"),
		Turret->SkillCopiesMade, 2);
	TestTrue(TEXT("and it strikes"), HealthOf(Target) < BeforeTheSecond);
	return true;
}

// ==========================================================================
// 4. A copy is not a use
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetCopiesSkillIsNotAUseTest,
	"Cataclysm.GadgetCopiesSkill.ACopyRaisesNoSkillUseAndNoAttackUseSendsNoNoticeAndLeavesTheRecord")
{
	using namespace CataclysmGadgetCopiesSkillTest;

	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	const FVector TurretAt(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* Turret = Place(*this, Rig.Player, TEXT("BoltTurret"), TurretAt);
	ACataclysmEnemyCharacter* Target = Enemy(World, TurretAt + FVector(3.0f * M, 0.0f, 0.0f), 50000.0f);
	if (!Turret || !TestNotNull(TEXT("set-up: an enemy"), Target))
	{
		return false;
	}
	FEventCount SkillUses(Rig.System, TEXT("skill_use"));
	FEventCount AttackUses(Rig.System, TEXT("attack_use"));

	// THE CONTROL: the announced use raises one of each and sends one notice.
	const uint32 NoticesBeforeTheUse = NoticesSent(World);
	Rig.Uses(Rig.Lash, Target->GetActorLocation());
	if (!TestEqual(TEXT("control: a use raises skill_use once"), SkillUses.Count, 1)
		|| !TestEqual(TEXT("control: and attack_use once"), AttackUses.Count, 1)
		|| !TestEqual(TEXT("control: and sends one notice"), NoticesSent(World), NoticesBeforeTheUse + 1))
	{
		return false;
	}
	Rig.HoldTheFlag();
	const uint32 NoticesBeforeTheCopy = NoticesSent(World);

	Step(Turret);
	if (!TestEqual(TEXT("set-up: the turret casts one copy"), Turret->SkillCopiesMade, 1))
	{
		return false;
	}
	TestEqual(TEXT("the copy raised no skill_use"), SkillUses.Count, 1);
	TestEqual(TEXT("and no attack_use"), AttackUses.Count, 1);
	TestEqual(TEXT("and sent no skill-used notice"), NoticesSent(World), NoticesBeforeTheCopy);
	TestEqual(TEXT("and the record is unchanged"), Rig.Record(), FName(TEXT("Searing Lash")));
	return true;
}

// ==========================================================================
// 5. The clock: 2 seconds
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetCopiesSkillClockTest,
	"Cataclysm.GadgetCopiesSkill.TheFirstCopyComesOnTheFirstStepWithATargetAndOneEveryTwoSecondsAfterUnhurried")
{
	using namespace CataclysmGadgetCopiesSkillTest;

	// THE PLAYER HOLDS THE FLAG AND "GADGETS FIRE FASTER" AT 100%: an increase of 100 on `minion_attack_speed`
	// requiring `Type.Deployable`, which halves the time between a gadget's ordinary shots. The enemy is stood after
	// the turret is placed, so the placing's own step finds nobody.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	Rig.Uses(Rig.Lash, FVector(5.0f * M, 0.0f, 0.0f));
	Rig.HoldTheFlag({FRig::FIncrease{FName(TEXT("minion_attack_speed")), 100.0f}});
	const FVector TurretAt(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* Turret = Place(*this, Rig.Player, TEXT("BoltTurret"), TurretAt);
	if (!Turret)
	{
		return false;
	}

	// THE CONTROL: a step with nobody within reach casts nothing and starts no clock.
	Pass(World, 0.5f);
	Step(Turret);
	TestEqual(TEXT("control: a step with nobody within reach casts no copy"), Turret->SkillCopiesMade, 0);
	TestFalse(TEXT("control: and starts no clock"), Turret->bHasCopiedASkill);

	ACataclysmEnemyCharacter* Target = Enemy(World, TurretAt + FVector(3.0f * M, 0.0f, 0.0f), 50000.0f);
	if (!TestNotNull(TEXT("set-up: an enemy"), Target))
	{
		return false;
	}
	Pass(World, 0.25f);
	Step(Turret);
	TestEqual(TEXT("the first copy comes on the first step with a target within reach"), Turret->SkillCopiesMade, 1);
	Step(Turret);
	TestEqual(TEXT("a second step at the same moment casts no second"), Turret->SkillCopiesMade, 1);

	// 1.1 SECONDS AFTER IT IS PAST HALF OF THE 2. A copy hurried as the ordinary shot is would have been cast.
	Pass(World, 1.1f);
	Step(Turret);
	TestEqual(TEXT("1.1 seconds after the first no second: Gadgets fire faster does not shorten the 2 seconds"),
		Turret->SkillCopiesMade, 1);
	Pass(World, 0.8f);
	Step(Turret);
	TestEqual(TEXT("nor 1.9 seconds after it"), Turret->SkillCopiesMade, 1);
	Pass(World, 0.2f);
	Step(Turret);
	TestEqual(TEXT("2.1 seconds after the first the second is cast"), Turret->SkillCopiesMade, 2);

	// THE BEAT IS KEPT FROM THE FIRST: the second came a tenth late and the third is due 4 seconds after the first.
	Pass(World, 1.85f);
	Step(Turret);
	TestEqual(TEXT("3.95 seconds after the first no third"), Turret->SkillCopiesMade, 2);
	Pass(World, 0.1f);
	Step(Turret);
	TestEqual(TEXT("4.05 seconds after the first, 1.95 after the second, the third: the beat is kept from the first"),
		Turret->SkillCopiesMade, 3);
	return true;
}

// ==========================================================================
// 6. Nothing to strike, or nothing on record
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetCopiesSkillNothingTest,
	"Cataclysm.GadgetCopiesSkill.NoTargetWithinReachOrNoRecordCastsNothingAndABeatThatCastsNothingIsPassedOver")
{
	using namespace CataclysmGadgetCopiesSkillTest;

	// TWO PLAYERS 100 M APART, BOTH HOLDING THE FLAG, EACH WITH A TURRET. The first has Searing Lash on record and an
	// enemy 12 m from its turret, beyond its 10 m reach. The second has nothing on record and an enemy 3 m from its
	// turret.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FRig Recorded(World, FVector::ZeroVector);
	FRig Unrecorded(World, FVector(100.0f * M, 0.0f, 0.0f));
	if (!Recorded.IsUsable(*this) || !Unrecorded.IsUsable(*this))
	{
		return false;
	}
	Recorded.Uses(Recorded.Lash, FVector(5.0f * M, 0.0f, 0.0f));
	Recorded.HoldTheFlag();
	Unrecorded.HoldTheFlag();
	const FVector FarAt(0.0f, 20.0f * M, 0.0f);
	const FVector NearAt(100.0f * M, 20.0f * M, 0.0f);
	ACataclysmMinion* FarTurret = Place(*this, Recorded.Player, TEXT("BoltTurret"), FarAt);
	ACataclysmMinion* NearTurret = Place(*this, Unrecorded.Player, TEXT("BoltTurret"), NearAt);
	ACataclysmEnemyCharacter* Beyond = Enemy(World, FarAt + FVector(12.0f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* Within = Enemy(World, NearAt + FVector(3.0f * M, 0.0f, 0.0f), 50000.0f);
	if (!FarTurret || !NearTurret || !TestTrue(TEXT("set-up: two enemies"), Beyond && Within)
		|| !TestEqual(TEXT("set-up: the first player has Searing Lash on record"), Recorded.Record(),
			FName(TEXT("Searing Lash")))
		|| !TestTrue(TEXT("set-up: and the second nothing"), Unrecorded.Record().IsNone()))
	{
		return false;
	}

	Pass(World, 0.25f);
	Step(FarTurret);
	Step(NearTurret);
	TestEqual(TEXT("with no target within reach nothing is cast"), FarTurret->SkillCopiesMade, 0);
	TestEqual(TEXT("and the enemy beyond reach keeps its health"), HealthOf(Beyond), 50000.0f, 0.001f);
	TestEqual(TEXT("with nothing on record nothing is cast"), NearTurret->SkillCopiesMade, 0);
	TestEqual(TEXT("and the enemy within reach keeps its health"), HealthOf(Within), 50000.0f, 0.001f);

	// A BEAT THAT CASTS NOTHING IS PASSED OVER. The enemy walks into the first turret's reach: the first copy. At
	// the next beat it stands beyond reach again and nothing is cast; back within reach a step later, still nothing
	// until the beat after.
	const FVector Inside = FarAt + FVector(3.0f * M, 0.0f, 0.0f);
	const FVector Outside = FarAt + FVector(12.0f * M, 0.0f, 0.0f);
	Beyond->SetActorLocation(Inside);
	Step(FarTurret);
	if (!TestEqual(TEXT("control: within reach, the first copy is cast"), FarTurret->SkillCopiesMade, 1))
	{
		return false;
	}
	Beyond->SetActorLocation(Outside);
	Pass(World, 2.05f);
	Step(FarTurret);
	TestEqual(TEXT("at the next beat, beyond reach, nothing is cast"), FarTurret->SkillCopiesMade, 1);
	Beyond->SetActorLocation(Inside);
	Pass(World, 0.25f);
	Step(FarTurret);
	TestEqual(TEXT("and back within reach a step later still nothing: the clock did not wait"),
		FarTurret->SkillCopiesMade, 1);
	Pass(World, 1.8f);
	Step(FarTurret);
	TestEqual(TEXT("the beat after that casts the second copy"), FarTurret->SkillCopiesMade, 2);
	return true;
}

// ==========================================================================
// 7. A trap, an imp, and a summoner holding no row
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetCopiesSkillOthersTest,
	"Cataclysm.GadgetCopiesSkill.AHoldersSpikeTrapAndImpCastNothingAndAGadgetOfASummonerHoldingNoRowCastsNothing")
{
	using namespace CataclysmGadgetCopiesSkillTest;

	// ONE PLAYER WITH SEARING LASH ON RECORD, AND A SPIKE TRAP, AN IMP AND A BOLT TURRET, EACH 30 M FROM THE NEXT AND
	// EACH WITH AN ENEMY WITHIN ITS REACH. The flag is granted after the control step.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	Rig.Uses(Rig.Lash, FVector(5.0f * M, 0.0f, 0.0f));
	const FVector TrapAt(0.0f, 30.0f * M, 0.0f);
	const FVector ImpAt(0.0f, 60.0f * M, 0.0f);
	const FVector TurretAt(0.0f, 90.0f * M, 0.0f);
	ACataclysmMinion* Trap = Place(*this, Rig.Player, TEXT("SpikeTrap"), TrapAt);
	ACataclysmMinion* Imp = Place(*this, Rig.Player, TEXT("Imp"), ImpAt);
	ACataclysmMinion* Turret = Place(*this, Rig.Player, TEXT("BoltTurret"), TurretAt);
	ACataclysmEnemyCharacter* AtTrap = Enemy(World, TrapAt + FVector(1.2f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* AtImp = Enemy(World, ImpAt + FVector(1.5f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* AtTurret = Enemy(World, TurretAt + FVector(3.0f * M, 0.0f, 0.0f), 50000.0f);
	if (!Trap || !Imp || !Turret || !TestTrue(TEXT("set-up: three enemies"), AtTrap && AtImp && AtTurret)
		|| !TestTrue(TEXT("set-up: the trap is a trap and not a gadget, and the turret is a gadget"),
			Trap->IsTrap() && !Trap->IsGadget() && Turret->IsGadget()))
	{
		return false;
	}

	// THE CONTROL: the summoner holds no row, and its turret casts nothing.
	Pass(World, 0.25f);
	Step(Turret);
	TestEqual(TEXT("control: a gadget whose summoner holds no row casts nothing"), Turret->SkillCopiesMade, 0);
	TestEqual(TEXT("control: and its enemy keeps its health"), HealthOf(AtTurret), 50000.0f, 0.001f);

	// THE ROW HELD: the turret casts; the trap and the imp cast nothing, stepped or asked outright.
	Rig.HoldTheFlag();
	Step(Trap);
	Step(Imp);
	Step(Turret);
	TestEqual(TEXT("with the row held the turret casts one copy"), Turret->SkillCopiesMade, 1);
	TestEqual(TEXT("the Spike Trap casts none on its step"), Trap->SkillCopiesMade, 0);
	TestEqual(TEXT("and the imp none"), Imp->SkillCopiesMade, 0);
	TestFalse(TEXT("the Spike Trap casts none asked outright"), Trap->CopySummonersLastSkill());
	TestFalse(TEXT("and the imp none"), Imp->CopySummonersLastSkill());
	TestEqual(TEXT("so the trap's enemy keeps its health"), HealthOf(AtTrap), 50000.0f, 0.001f);
	TestEqual(TEXT("and the imp's"), HealthOf(AtImp), 50000.0f, 0.001f);
	return true;
}

// ==========================================================================
// 8. A copy's hit sets off no row that casts, and changes no record
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetCopiesSkillNoChainTest,
	"Cataclysm.GadgetCopiesSkill.ACopyGetsNoRepeatAndNoTriggerFromTheRowsThatCastAndLeavesTheRecord")
{
	using namespace CataclysmGadgetCopiesSkillTest;

	// THE PLAYER WEARS A `repeat_skill` ROW AT A CHANCE OF 100 ON `skill_use` AND A `trigger_held_skill` ROW AT 100 ON
	// `attack_use`. Every authored row that casts a skill fires on one of those two events, read by script over
	// game/Data/EnchantmentEffects.csv; the entry in docs/DECISIONS.md, 2026-10-14, prints it.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	FCataclysmPoolAction RepeatRow;
	RepeatRow.Event = FName(TEXT("skill_use"));
	RepeatRow.Percent = 100.0f;
	RepeatRow.bRepeatSkill = true;
	RepeatRow.RepeatSharePercent = 100.0f;
	RepeatRow.TriggerKey = FName(TEXT("Test:copy:repeat"));
	FCataclysmPoolAction TriggerRow;
	TriggerRow.Event = FName(TEXT("attack_use"));
	TriggerRow.Percent = 100.0f;
	TriggerRow.bTriggerHeldSkill = true;
	TriggerRow.TriggerKey = FName(TEXT("Test:copy:trigger"));
	Rig.System->SetPoolActions({RepeatRow, TriggerRow});
	const FVector TurretAt(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* Turret = Place(*this, Rig.Player, TEXT("BoltTurret"), TurretAt);
	ACataclysmEnemyCharacter* Target = Enemy(World, TurretAt + FVector(3.0f * M, 0.0f, 0.0f), 50000.0f);
	if (!Turret || !TestNotNull(TEXT("set-up: an enemy"), Target))
	{
		return false;
	}

	// THE CONTROL: a use of Searing Lash asks for both, and the two requests are then taken back by hand. Nothing
	// ticks, so neither would have been made.
	Rig.Uses(Rig.Lash, Target->GetActorLocation());
	if (!TestEqual(TEXT("control: a use asks the repeat row for a repeat of it"), Rig.System->PendingRepeatSkill(),
			FName(TEXT("Searing Lash")))
		|| !TestEqual(TEXT("control: and the trigger row for a trigger"), Rig.System->PendingHeldTriggerUsedSkill(),
			FName(TEXT("Searing Lash"))))
	{
		return false;
	}
	Rig.System->ClearPendingRepeat();
	Rig.System->ClearPendingHeldTrigger();
	Rig.HoldTheFlag();

	Step(Turret);
	if (!TestEqual(TEXT("set-up: the turret casts one copy"), Turret->SkillCopiesMade, 1)
		|| !TestTrue(TEXT("set-up: and its hit lands"), HealthOf(Target) < 50000.0f))
	{
		return false;
	}
	TestTrue(TEXT("the copy asks for no repeat"), Rig.System->PendingRepeatSkill().IsNone());
	TestTrue(TEXT("and for no trigger"), Rig.System->PendingHeldTriggerUsedSkill().IsNone());
	TestEqual(TEXT("and the record is unchanged"), Rig.Record(), FName(TEXT("Searing Lash")));
	TestEqual(TEXT("and the turret made one copy and no other"), Turret->SkillCopiesMade, 1);
	return true;
}

// ==========================================================================
// 9. A copy's hit is the summoner's
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetCopiesSkillHitIsTheSummonersTest,
	"Cataclysm.GadgetCopiesSkill.ACopysHitIsTheSummonersAndSetsOffItsHitDealtRow")
{
	using namespace CataclysmGadgetCopiesSkillTest;

	// THE PLAYER WEARS A ROW ON `hit_dealt` THAT RESTORES 10% OF ITS MAXIMUM MANA. It holds half of 1,000.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	FCataclysmPoolAction OnHit;
	OnHit.Event = FName(TEXT("hit_dealt"));
	OnHit.Pool = FName(TEXT("mana"));
	OnHit.Percent = 10.0f;
	OnHit.Base = ECataclysmPoolActionBase::Maximum;
	OnHit.TriggerKey = FName(TEXT("Test:copy:hit"));
	Rig.System->SetPoolActions({OnHit});
	const FVector TurretAt(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* Turret = Place(*this, Rig.Player, TEXT("BoltTurret"), TurretAt);
	ACataclysmEnemyCharacter* Target = Enemy(World, TurretAt + FVector(3.0f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* Control = Enemy(World, FVector(3.0f * M, 0.0f, 0.0f), 50000.0f);
	if (!Turret || !TestTrue(TEXT("set-up: two enemies"), Target && Control))
	{
		return false;
	}
	const FGameplayAttribute Mana = UCataclysmVitalAttributeSet::GetManaAttribute();
	FEventCount HitsDealt(Rig.System, TEXT("hit_dealt"));
	Rig.Uses(Rig.Lash, Target->GetActorLocation());

	// THE CONTROL: the summoner's own cast at its own enemy raises hit_dealt once, and the row restores mana.
	Rig.Set(Mana, 500.0f);
	if (!TestTrue(TEXT("set-up: the control cast starts"), Rig.CastsFromItsOwnPlace(Rig.Lash, Control->GetActorLocation())))
	{
		return false;
	}
	const float ControlGained = Rig.Get(Mana) - 500.0f;
	if (!TestEqual(TEXT("control: the summoner's own cast raised hit_dealt once"), HitsDealt.Count, 1)
		|| !TestTrue(*FString::Printf(TEXT("control: and the row restored mana: %.2f"), ControlGained),
			ControlGained > 0.0f))
	{
		return false;
	}

	// THE COPY.
	Rig.Set(Mana, 500.0f);
	Rig.HoldTheFlag();
	Step(Turret);
	if (!TestEqual(TEXT("set-up: the turret casts one copy"), Turret->SkillCopiesMade, 1))
	{
		return false;
	}
	TestEqual(TEXT("the copy's hit raised hit_dealt on the summoner"), HitsDealt.Count, 2);
	TestEqual(TEXT("and the summoner's hit_dealt row restored what it restores for the summoner's own cast"),
		Rig.Get(Mana) - 500.0f, ControlGained, 0.01f);
	const UCataclysmAbilitySystemComponent* Struck =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Target));
	TestTrue(TEXT("and the blow on record on the target is credited to the summoner"),
		Struck && Struck->GetLastBlow().IsOnRecord() && Struck->GetLastBlow().Attacker.Get() == Rig.Player);
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
