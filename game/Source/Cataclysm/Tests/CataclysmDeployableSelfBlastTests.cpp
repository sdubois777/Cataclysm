// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDeployableSelfBlast.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
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
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * "Your deployables and traps deal full damage to you if you are within their blast radius", the deployable half.
 * Ruled 2026-10-19 by the coordinating session, B1 to B6 in the entry of that date in `docs/DECISIONS.md`; the code is
 * `UCataclysmDeployableSelfBlast`. Beside `Cataclysm.SkillGadgets.`, whose rig this follows. THE SKILL GADGET ONLY: no
 * War skill places anything here.
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER HOLDING A DEMONIC WHIP, given a weapon damage of 100 by hand: its Ultimate
 * The Gathering is a 14 m Strike all round, and its basic attack a 3 m Strike of 45 degrees limited to one target.
 *
 * NO ROW IS AUTHORED, SO EVERY STAT IS GRANTED BY HAND, one stat line per call, replacing the last. The flag is a flat
 * 1 on `deployable_blasts_strike_self` with no tags, the row the enchantment session is to write.
 *
 * EACH CASE IS ONE OR MORE GROUPS 100 M APART, each a player, its gadget and its enemies, so no gadget reaches another
 * group. Every body stands at least 1 m from every other, the player's own gadget included; every enemy is on the
 * monsters' side, and every player, and so its gadget, on the players'. EVERY PLAYER HOLDS AN ENEMY'S DEFENCES: no armour,
 * evasion, block, resistance, damage reduction or energy shield, unless a case gives it some. A CAST IS THE GADGET'S OWN STEP, a free start
 * given its place, which waits for no attack clip. NOTHING WAITS ON A TICK. The critical strike roll is pinned in every
 * case, after the world is made.
 */
namespace CataclysmDeployableSelfBlastTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** How far apart two groups stand. */
	constexpr float GroupApart = 100.0f * M;

	/** A world for one test, destroyed when the test ends. */
	struct FDeployableSelfBlastWorld
	{
		FDeployableSelfBlastWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FDeployableSelfBlastWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/** A creature of the monsters' side with no armour, evasion, block or resistance, holding 50000. It deals nothing. */
	ACataclysmEnemyCharacter* DeployableSelfBlastEnemy(UWorld* World, const FVector& Where)
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
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 50000.0f);
		return Made;
	}

	float DeployableSelfBlastHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** One row of a stat line a test's player holds: a flat figure with one required tag or none. */
	struct FDeployableSelfBlastRow
	{
		FName Stat;
		float Value = 0.0f;
		const TCHAR* RequiredTag = nullptr;
	};

	/** The flag, as the row to be authored states it: a flat 1 with no tags. */
	FDeployableSelfBlastRow DeployableSelfBlastFlag()
	{
		FDeployableSelfBlastRow Row;
		Row.Stat = FName(UCataclysmDeployableSelfBlast::FlagStat);
		Row.Value = 1.0f;
		return Row;
	}

	/** The slot's gadget flag: a flat 1 on `skill_slot_is_a_gadget` requiring the slot's tag. */
	FDeployableSelfBlastRow DeployableSelfBlastConverts(const TCHAR* SlotTag)
	{
		FDeployableSelfBlastRow Row;
		Row.Stat = FName(UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat);
		Row.Value = 1.0f;
		Row.RequiredTag = SlotTag;
		return Row;
	}

	/** A flat figure with no tags. */
	FDeployableSelfBlastRow DeployableSelfBlastFlat(const TCHAR* Stat, float Value)
	{
		FDeployableSelfBlastRow Row;
		Row.Stat = FName(Stat);
		Row.Value = Value;
		return Row;
	}

	/** A possessed player character holding one weapon, its ability system and its weapon slots. */
	struct FDeployableSelfBlastRig
	{
		FDeployableSelfBlastRig(UWorld* World, const FVector& Where)
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
			Set(UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 1.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			HoldsNoDefences();
		}

		/** Whether everything was made and the weapon holds a row for `Slot`. Reports why not. */
		bool IsUsable(FAutomationTestBase& Test, ECataclysmAbilitySlot Slot) const
		{
			FCataclysmWeaponSkill Row;
			if (!Player || !System || !Slots || !HeldRow(Slot, Row))
			{
				Test.AddError(TEXT("set-up: a possessed player holding its weapon and a row for the slot. If a row is "
								   "missing, DT_WeaponSkills or DT_ItemBases may be older than game/Data: run "
								   "tools/generate_datatable_assets.py"));
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

		/**
		 * The defences an enemy of `DeployableSelfBlastEnemy` holds: no armour, evasion, block, resistance, damage
		 * reduction or energy shield. So what the player takes can be read against what such an enemy takes.
		 */
		void HoldsNoDefences() const
		{
			Set(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetDamageReductionAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetDamageReductionMoreAttribute(), 0.0f);
			Set(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 0.0f);
			Set(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);
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
		void Holds(const TArray<FDeployableSelfBlastRow>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FDeployableSelfBlastRow& Each : Rows)
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

		/** A free start of this row from the player's own place, at a point: the player's own cast, no gadget's. */
		bool CastsFromItsOwnPlace(const FCataclysmWeaponSkill& Skill, const FVector& Aim) const
		{
			const FVector Here = Player->GetActorLocation();
			return UCataclysmTriggeredSkill::Trigger(Player, Skill, Aim, /*DamageShare=*/1.0f, /*bPaysCost=*/false,
													 &Here);
		}

		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
	};

	/** A skill gadget for this rig's `Slot` at `Where`, or null with the reason reported. */
	ACataclysmMinion* DeployableSelfBlastPlace(FAutomationTestBase& Test, const FDeployableSelfBlastRig& Rig,
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

	/**
	 * Whether the gadget is a deployable on its player's side and the enemy on the other, one assertion each with the
	 * group's name in its message.
	 */
	bool DeployableSelfBlastSides(FAutomationTestBase& Test, const TCHAR* Group, const FDeployableSelfBlastRig& Rig,
								  const ACataclysmMinion* Gadget, const AActor* Enemy)
	{
		const bool bDeployable = Test.TestTrue(
			*FString::Printf(TEXT("set-up (%s): the gadget is a deployable of its player's"), Group),
			Gadget->IsDeployable() && Gadget->Summoner.Get() == Rig.Player);
		const bool bAlly = Test.TestTrue(
			*FString::Printf(TEXT("set-up (%s): the gadget is its player's ally"), Group),
			UCataclysmTargeting::IsFriendlyTo(Gadget, Rig.Player));
		const bool bEnemy = Test.TestTrue(
			*FString::Printf(TEXT("set-up (%s): the enemy is its player's enemy"), Group),
			UCataclysmTargeting::IsHostileTo(Enemy, Rig.Player));
		return bDeployable && bAlly && bEnemy;
	}

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FDeployableSelfBlastEventCount
	{
		FDeployableSelfBlastEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
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
		~FDeployableSelfBlastEventCount()
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

	/** Run the gadget's quarter-second step, as `RegenerationStep` calls it. */
	void DeployableSelfBlastStep(ACataclysmMinion* Gadget)
	{
		ACataclysmMinion::GadgetStep(Gadget);
	}
}

#define CATACLYSM_DEPLOYABLE_SELF_BLAST_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 1. Inside its gadget's area strike, the wearer is struck; outside it, or without the row, nobody is (B1, B2)
// ==========================================================================

CATACLYSM_DEPLOYABLE_SELF_BLAST_TEST(FCataclysmDeployableSelfBlastInsideTest,
	"Cataclysm.DeployableSelfBlast.AWearerInsideItsGadgetsAreaStrikeIsStruckAndOneOutsideItOrWithoutTheRowIsNot")
{
	using namespace CataclysmDeployableSelfBlastTest;

	// THREE GROUPS: THE PLAIN PLAYER, HOLDING NO FLAG; THE WEARER INSIDE; THE WEARER OUTSIDE. Each converts its
	// Ultimate, The Gathering, a 14 m Strike all round, and its gadget stands at the group's place. The enemy is 3 m
	// along -X from the gadget; the plain player and the wearer inside 3 m along X, the wearer outside 20 m along X.
	FDeployableSelfBlastWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	const FVector PlainAt(0.0f, 0.0f, 0.0f);
	const FVector InsideAt(GroupApart, 0.0f, 0.0f);
	const FVector OutsideAt(2.0f * GroupApart, 0.0f, 0.0f);
	FDeployableSelfBlastRig Plain(World, PlainAt + FVector(3.0f * M, 0.0f, 0.0f));
	FDeployableSelfBlastRig Inside(World, InsideAt + FVector(3.0f * M, 0.0f, 0.0f));
	FDeployableSelfBlastRig Outside(World, OutsideAt + FVector(20.0f * M, 0.0f, 0.0f));
	if (!Plain.IsUsable(*this, ECataclysmAbilitySlot::Ultimate) || !Inside.IsUsable(*this, ECataclysmAbilitySlot::Ultimate)
		|| !Outside.IsUsable(*this, ECataclysmAbilitySlot::Ultimate))
	{
		return false;
	}
	Plain.Holds({DeployableSelfBlastConverts(TEXT("Slot.Ultimate"))});
	Inside.Holds({DeployableSelfBlastConverts(TEXT("Slot.Ultimate")), DeployableSelfBlastFlag()});
	Outside.Holds({DeployableSelfBlastConverts(TEXT("Slot.Ultimate")), DeployableSelfBlastFlag()});
	ACataclysmMinion* PlainGadget = DeployableSelfBlastPlace(*this, Plain, ECataclysmAbilitySlot::Ultimate, PlainAt);
	ACataclysmMinion* InsideGadget = DeployableSelfBlastPlace(*this, Inside, ECataclysmAbilitySlot::Ultimate, InsideAt);
	ACataclysmMinion* OutsideGadget =
		DeployableSelfBlastPlace(*this, Outside, ECataclysmAbilitySlot::Ultimate, OutsideAt);
	if (!PlainGadget || !InsideGadget || !OutsideGadget)
	{
		return false;
	}
	ACataclysmEnemyCharacter* PlainEnemy = DeployableSelfBlastEnemy(World, PlainAt + FVector(-3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* InsideEnemy = DeployableSelfBlastEnemy(World, InsideAt + FVector(-3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* OutsideEnemy =
		DeployableSelfBlastEnemy(World, OutsideAt + FVector(-3.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the plain group's enemy"), PlainEnemy)
		|| !TestNotNull(TEXT("set-up: the inside group's enemy"), InsideEnemy)
		|| !TestNotNull(TEXT("set-up: the outside group's enemy"), OutsideEnemy))
	{
		return false;
	}
	if (!DeployableSelfBlastSides(*this, TEXT("plain"), Plain, PlainGadget, PlainEnemy)
		|| !DeployableSelfBlastSides(*this, TEXT("inside"), Inside, InsideGadget, InsideEnemy)
		|| !DeployableSelfBlastSides(*this, TEXT("outside"), Outside, OutsideGadget, OutsideEnemy))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: the plain player's gadget strikes its enemy, and the plain player inside the area loses
	// nothing.
	DeployableSelfBlastStep(PlainGadget);
	TestEqual(TEXT("control: the plain player's gadget casts once"), PlainGadget->SkillGadgetCastsMade, 1);
	TestTrue(TEXT("control: and strikes its enemy"), DeployableSelfBlastHealthOf(PlainEnemy) < 50000.0f);
	TestEqual(TEXT("control: the plain player 3 m from its gadget, holding no flag, loses nothing"),
		DeployableSelfBlastHealthOf(Plain.Player), 10000.0f, 0.001f);

	// THE WEARER INSIDE.
	DeployableSelfBlastStep(InsideGadget);
	TestTrue(TEXT("set-up: the inside wearer's gadget struck its enemy"), DeployableSelfBlastHealthOf(InsideEnemy) < 50000.0f);
	TestTrue(*FString::Printf(TEXT("the wearer 3 m from its gadget is struck too: %.2f left of 10000"),
							  DeployableSelfBlastHealthOf(Inside.Player)),
		DeployableSelfBlastHealthOf(Inside.Player) < 10000.0f);

	// THE WEARER OUTSIDE, 20 M FROM ITS GADGET, beyond the 14 m.
	DeployableSelfBlastStep(OutsideGadget);
	TestTrue(TEXT("set-up: the outside wearer's gadget struck its enemy"),
		DeployableSelfBlastHealthOf(OutsideEnemy) < 50000.0f);
	TestEqual(TEXT("the wearer 20 m from its gadget, outside the 14 m, loses nothing"),
		DeployableSelfBlastHealthOf(Outside.Player), 10000.0f, 0.001f);
	return true;
}

// ==========================================================================
// 2. The figure: what an enemy standing beside the wearer takes, and then the wearer's own defences (B3)
// ==========================================================================

CATACLYSM_DEPLOYABLE_SELF_BLAST_TEST(FCataclysmDeployableSelfBlastFigureTest,
	"Cataclysm.DeployableSelfBlast.TheWearerTakesWhatAnEnemyBesideItTakesAndItsOwnArmourThenReducesIt")
{
	using namespace CataclysmDeployableSelfBlastTest;

	// TWO GROUPS, BOTH HOLDING THE FLAG: a bare wearer with an enemy's defences, and an armoured one with 800 armour.
	// Each gadget's target 3 m along -X; the wearer 3 m along X; a second enemy 1.5 m along Y from the wearer.
	FDeployableSelfBlastWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	if (!TestNotNull(TEXT("set-up: the critical strike roll is pinned to 100, so no blow here critically strikes"),
			NeverCritical.Variable))
	{
		return false;
	}
	const FVector BareAt(0.0f, 0.0f, 0.0f);
	const FVector ArmouredAt(GroupApart, 0.0f, 0.0f);
	FDeployableSelfBlastRig Bare(World, BareAt + FVector(3.0f * M, 0.0f, 0.0f));
	FDeployableSelfBlastRig Armoured(World, ArmouredAt + FVector(3.0f * M, 0.0f, 0.0f));
	if (!Bare.IsUsable(*this, ECataclysmAbilitySlot::Ultimate) || !Armoured.IsUsable(*this, ECataclysmAbilitySlot::Ultimate))
	{
		return false;
	}
	Armoured.Set(UCataclysmCombatAttributeSet::GetArmorAttribute(), 800.0f);
	Bare.Holds({DeployableSelfBlastConverts(TEXT("Slot.Ultimate")), DeployableSelfBlastFlag()});
	Armoured.Holds({DeployableSelfBlastConverts(TEXT("Slot.Ultimate")), DeployableSelfBlastFlag()});
	ACataclysmMinion* BareGadget = DeployableSelfBlastPlace(*this, Bare, ECataclysmAbilitySlot::Ultimate, BareAt);
	ACataclysmMinion* ArmouredGadget =
		DeployableSelfBlastPlace(*this, Armoured, ECataclysmAbilitySlot::Ultimate, ArmouredAt);
	if (!BareGadget || !ArmouredGadget)
	{
		return false;
	}
	ACataclysmEnemyCharacter* BareTarget = DeployableSelfBlastEnemy(World, BareAt + FVector(-3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* BareBeside = DeployableSelfBlastEnemy(World, BareAt + FVector(3.0f * M, 1.5f * M, 0.0f));
	ACataclysmEnemyCharacter* ArmouredTarget =
		DeployableSelfBlastEnemy(World, ArmouredAt + FVector(-3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* ArmouredBeside =
		DeployableSelfBlastEnemy(World, ArmouredAt + FVector(3.0f * M, 1.5f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: the bare group's target"), BareTarget)
		|| !TestNotNull(TEXT("set-up: the enemy beside the bare wearer"), BareBeside)
		|| !TestNotNull(TEXT("set-up: the armoured group's target"), ArmouredTarget)
		|| !TestNotNull(TEXT("set-up: the enemy beside the armoured wearer"), ArmouredBeside))
	{
		return false;
	}
	if (!DeployableSelfBlastSides(*this, TEXT("bare"), Bare, BareGadget, BareBeside)
		|| !DeployableSelfBlastSides(*this, TEXT("armoured"), Armoured, ArmouredGadget, ArmouredBeside))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: the enemy beside the bare wearer is struck.
	DeployableSelfBlastStep(BareGadget);
	const float BesideLost = 50000.0f - DeployableSelfBlastHealthOf(BareBeside);
	if (!TestTrue(*FString::Printf(TEXT("control: the enemy beside the bare wearer is struck for %.2f"), BesideLost),
			BesideLost > 0.0f))
	{
		return false;
	}
	const float BareLost = 10000.0f - DeployableSelfBlastHealthOf(Bare.Player);
	TestEqual(*FString::Printf(TEXT("the bare wearer is struck for what the enemy beside it took: %.2f against %.2f"),
							   BareLost, BesideLost),
		BareLost, BesideLost, 0.01f);

	// AND ITS OWN ARMOUR MEETS IT.
	DeployableSelfBlastStep(ArmouredGadget);
	const float ArmouredBesideLost = 50000.0f - DeployableSelfBlastHealthOf(ArmouredBeside);
	const float ArmouredLost = 10000.0f - DeployableSelfBlastHealthOf(Armoured.Player);
	TestTrue(*FString::Printf(TEXT("set-up: the enemy beside the armoured wearer is struck for %.2f"), ArmouredBesideLost),
		ArmouredBesideLost > 0.0f);
	TestTrue(*FString::Printf(TEXT("the armoured wearer is struck: %.2f"), ArmouredLost), ArmouredLost > 0.0f);
	TestTrue(*FString::Printf(TEXT("and for less than the enemy beside it, its 800 armour taking part: %.2f against %.2f"),
							  ArmouredLost, ArmouredBesideLost),
		ArmouredLost < ArmouredBesideLost - 0.01f);
	return true;
}

// ==========================================================================
// 3. A single-target blow the gadget casts does not strike the wearer (B2)
// ==========================================================================

CATACLYSM_DEPLOYABLE_SELF_BLAST_TEST(FCataclysmDeployableSelfBlastSingleTargetTest,
	"Cataclysm.DeployableSelfBlast.ASingleTargetBasicSwingTheGadgetCastsDoesNotStrikeTheWearerInsideItsCone")
{
	using namespace CataclysmDeployableSelfBlastTest;

	// TWO GROUPS, BOTH HOLDING THE FLAG AND STANDING ALIKE: the gadget at the group's place, its enemy 2.5 m along X, and
	// the wearer 1.5 m along X and 0.5 m along Y, inside the basic attack's 3 m cone of 45 degrees aimed at the enemy,
	// 1.58 m from the gadget and 1.12 m from the enemy. The first converts its Ultimate, the second its basic attack.
	FDeployableSelfBlastWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	const FVector AreaAt(0.0f, 0.0f, 0.0f);
	const FVector BasicAt(GroupApart, 0.0f, 0.0f);
	const FVector WearerOffset(1.5f * M, 0.5f * M, 0.0f);
	const FVector EnemyOffset(2.5f * M, 0.0f, 0.0f);
	FDeployableSelfBlastRig Area(World, AreaAt + WearerOffset);
	FDeployableSelfBlastRig Basic(World, BasicAt + WearerOffset);
	FCataclysmWeaponSkill BasicRow;
	if (!Area.IsUsable(*this, ECataclysmAbilitySlot::Ultimate) || !Basic.IsUsable(*this, ECataclysmAbilitySlot::BasicAttack)
		|| !Basic.HeldRow(ECataclysmAbilitySlot::BasicAttack, BasicRow))
	{
		return false;
	}
	Area.Holds({DeployableSelfBlastConverts(TEXT("Slot.Ultimate")), DeployableSelfBlastFlag()});
	Basic.Holds({DeployableSelfBlastConverts(TEXT("Slot.Basic")), DeployableSelfBlastFlag()});
	ACataclysmMinion* AreaGadget = DeployableSelfBlastPlace(*this, Area, ECataclysmAbilitySlot::Ultimate, AreaAt);
	ACataclysmMinion* BasicGadget = DeployableSelfBlastPlace(*this, Basic, ECataclysmAbilitySlot::BasicAttack, BasicAt);
	if (!AreaGadget || !BasicGadget)
	{
		return false;
	}
	ACataclysmEnemyCharacter* AreaEnemy = DeployableSelfBlastEnemy(World, AreaAt + EnemyOffset);
	ACataclysmEnemyCharacter* BasicEnemy = DeployableSelfBlastEnemy(World, BasicAt + EnemyOffset);
	if (!TestNotNull(TEXT("set-up: the area group's enemy"), AreaEnemy)
		|| !TestNotNull(TEXT("set-up: the basic group's enemy"), BasicEnemy))
	{
		return false;
	}
	if (!DeployableSelfBlastSides(*this, TEXT("area"), Area, AreaGadget, AreaEnemy)
		|| !DeployableSelfBlastSides(*this, TEXT("basic"), Basic, BasicGadget, BasicEnemy))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: the same place, inside the Ultimate gadget's area strike, is struck.
	DeployableSelfBlastStep(AreaGadget);
	TestTrue(TEXT("control: the area group's gadget struck its enemy"), DeployableSelfBlastHealthOf(AreaEnemy) < 50000.0f);
	TestTrue(TEXT("control: and the wearer standing 1.5 m along X and 0.5 m along Y from it"),
		DeployableSelfBlastHealthOf(Area.Player) < 10000.0f);

	// THE BASIC ATTACK'S SWING, ONE TARGET, WITH THE WEARER INSIDE ITS CONE.
	TestEqual(TEXT("set-up: the whip's basic attack is limited to one target"), BasicRow.Params.MaxTargets, 1);
	TestTrue(TEXT("set-up: the wearer stands inside the basic attack's cone, aimed from the gadget at its enemy"),
		UCataclysmTargeting::IsInCone(BasicAt, EnemyOffset.GetSafeNormal(), Basic.Player->GetActorLocation(),
									  BasicRow.Params.RadiusCm, BasicRow.Params.AngleDegrees));
	DeployableSelfBlastStep(BasicGadget);
	TestEqual(TEXT("set-up: the basic gadget casts once"), BasicGadget->SkillGadgetCastsMade, 1);
	TestTrue(TEXT("control: and strikes its enemy"), DeployableSelfBlastHealthOf(BasicEnemy) < 50000.0f);
	TestEqual(TEXT("the wearer inside the single-target swing's cone loses nothing"),
		DeployableSelfBlastHealthOf(Basic.Player), 10000.0f, 0.001f);
	return true;
}

// ==========================================================================
// 4. The wearer's own cast of the skill, from its own place, does not strike it (B5)
// ==========================================================================

CATACLYSM_DEPLOYABLE_SELF_BLAST_TEST(FCataclysmDeployableSelfBlastOwnCastTest,
	"Cataclysm.DeployableSelfBlast.TheWearersOwnCastOfTheSkillFromItsOwnPlaceDoesNotStrikeIt")
{
	using namespace CataclysmDeployableSelfBlastTest;

	// ONE WEARER HOLDING THE FLAG AND NO CONVERSION, an enemy 3 m along X. THE CAST IS A FREE START FROM ITS OWN PLACE
	// AND NOT A PRESS: a pressed Strike waits for its attack clip and a test world is never ticked (ruled by the
	// coordinating session on 2026-10-16 for the same case of `Cataclysm.AoeStrikesAllies.`). It names no actor cast
	// from, as a press names none.
	FDeployableSelfBlastWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	FDeployableSelfBlastRig Rig(World, FVector::ZeroVector);
	FCataclysmWeaponSkill Gathering;
	if (!Rig.IsUsable(*this, ECataclysmAbilitySlot::Ultimate) || !Rig.HeldRow(ECataclysmAbilitySlot::Ultimate, Gathering))
	{
		return false;
	}
	Rig.Holds({DeployableSelfBlastFlag()});
	ACataclysmEnemyCharacter* Enemy = DeployableSelfBlastEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: an enemy"), Enemy))
	{
		return false;
	}
	TestTrue(TEXT("set-up: the enemy is the wearer's enemy"), UCataclysmTargeting::IsHostileTo(Enemy, Rig.Player));
	TestTrue(TEXT("set-up: the wearer holds the flag"), UCataclysmDeployableSelfBlast::HeldBy(Rig.Player));

	TestTrue(TEXT("set-up: the wearer's own cast of The Gathering starts"),
		Rig.CastsFromItsOwnPlace(Gathering, Enemy->GetActorLocation()));
	TestTrue(TEXT("control: it strikes the enemy"), DeployableSelfBlastHealthOf(Enemy) < 50000.0f);
	TestEqual(TEXT("the wearer at the centre of its own swing loses nothing"),
		DeployableSelfBlastHealthOf(Rig.Player), 10000.0f, 0.001f);
	return true;
}

// ==========================================================================
// 5. What the blow raises: a blow taken, and no hit dealt (B4)
// ==========================================================================

CATACLYSM_DEPLOYABLE_SELF_BLAST_TEST(FCataclysmDeployableSelfBlastEventsTest,
	"Cataclysm.DeployableSelfBlast.TheBlowRaisesHitTakenAndNoHitDealtFirstHitCriticalStrikeOrDeployableHit")
{
	using namespace CataclysmDeployableSelfBlastTest;

	// TWO GROUPS LAID OUT AS IN THE FIRST CASE: THE PLAIN PLAYER AND THE WEARER, each 3 m from its Ultimate gadget with
	// its enemy 3 m on the gadget's other side. EVERY ROLL CRITICALLY STRIKES and each player has a chance of 50.
	FDeployableSelfBlastWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const CataclysmTestWorld::FScopedCritRoll AlwaysCrits(0.0f);
	if (!TestNotNull(TEXT("set-up: the critical strike roll is pinned to 0"), AlwaysCrits.Variable))
	{
		return false;
	}
	const FVector PlainAt(0.0f, 0.0f, 0.0f);
	const FVector WearerAt(GroupApart, 0.0f, 0.0f);
	FDeployableSelfBlastRig Plain(World, PlainAt + FVector(3.0f * M, 0.0f, 0.0f));
	FDeployableSelfBlastRig Wearer(World, WearerAt + FVector(3.0f * M, 0.0f, 0.0f));
	if (!Plain.IsUsable(*this, ECataclysmAbilitySlot::Ultimate) || !Wearer.IsUsable(*this, ECataclysmAbilitySlot::Ultimate))
	{
		return false;
	}
	for (const FDeployableSelfBlastRig* Each : {&Plain, &Wearer})
	{
		Each->Set(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 50.0f);
		Each->Set(UCataclysmCombatAttributeSet::GetCritMultiplierAttribute(), 150.0f);
	}
	Plain.Holds({DeployableSelfBlastConverts(TEXT("Slot.Ultimate"))});
	Wearer.Holds({DeployableSelfBlastConverts(TEXT("Slot.Ultimate")), DeployableSelfBlastFlag()});
	ACataclysmMinion* PlainGadget = DeployableSelfBlastPlace(*this, Plain, ECataclysmAbilitySlot::Ultimate, PlainAt);
	ACataclysmMinion* WearerGadget = DeployableSelfBlastPlace(*this, Wearer, ECataclysmAbilitySlot::Ultimate, WearerAt);
	if (!PlainGadget || !WearerGadget)
	{
		return false;
	}
	ACataclysmEnemyCharacter* PlainEnemy = DeployableSelfBlastEnemy(World, PlainAt + FVector(-3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* WearerEnemy = DeployableSelfBlastEnemy(World, WearerAt + FVector(-3.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the plain group's enemy"), PlainEnemy)
		|| !TestNotNull(TEXT("set-up: the wearer's group's enemy"), WearerEnemy))
	{
		return false;
	}
	if (!DeployableSelfBlastSides(*this, TEXT("plain"), Plain, PlainGadget, PlainEnemy)
		|| !DeployableSelfBlastSides(*this, TEXT("wearer"), Wearer, WearerGadget, WearerEnemy))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: the plain player's gadget's cast, one critical blow on one enemy and none on the player.
	{
		FDeployableSelfBlastEventCount HitsDealt(Plain.System, TEXT("hit_dealt"));
		FDeployableSelfBlastEventCount FirstHits(Plain.System, TEXT("first_hit_dealt"));
		FDeployableSelfBlastEventCount Crits(Plain.System, TEXT("critical_strike"));
		FDeployableSelfBlastEventCount DeployableHits(Plain.System, TEXT("deployable_hit"));
		FDeployableSelfBlastEventCount HitsTaken(Plain.System, TEXT("hit_taken"));
		DeployableSelfBlastStep(PlainGadget);
		TestEqual(TEXT("control: the plain cast raises one hit_dealt, for its enemy"), HitsDealt.Count, 1);
		TestEqual(TEXT("control: one first_hit_dealt"), FirstHits.Count, 1);
		TestEqual(TEXT("control: one critical_strike"), Crits.Count, 1);
		TestEqual(TEXT("control: one deployable_hit"), DeployableHits.Count, 1);
		TestEqual(TEXT("control: and no hit_taken, the plain player being struck by nothing"), HitsTaken.Count, 0);
	}

	// THE WEARER'S: the same for its enemy, and for the blow on itself one hit_taken and nothing else.
	FDeployableSelfBlastEventCount HitsDealt(Wearer.System, TEXT("hit_dealt"));
	FDeployableSelfBlastEventCount FirstHits(Wearer.System, TEXT("first_hit_dealt"));
	FDeployableSelfBlastEventCount Crits(Wearer.System, TEXT("critical_strike"));
	FDeployableSelfBlastEventCount DeployableHits(Wearer.System, TEXT("deployable_hit"));
	FDeployableSelfBlastEventCount HitsTaken(Wearer.System, TEXT("hit_taken"));
	DeployableSelfBlastStep(WearerGadget);
	if (!TestTrue(TEXT("set-up: the wearer was struck"), DeployableSelfBlastHealthOf(Wearer.Player) < 10000.0f))
	{
		return false;
	}
	TestEqual(TEXT("one hit_dealt, its enemy's: the blow on the wearer is not answered as a hit dealt"), HitsDealt.Count, 1);
	TestEqual(TEXT("one first_hit_dealt, its enemy's"), FirstHits.Count, 1);
	TestEqual(TEXT("one critical_strike, its enemy's: the blow on the wearer never critically strikes"), Crits.Count, 1);
	TestEqual(TEXT("one deployable_hit, its enemy's"), DeployableHits.Count, 1);
	TestEqual(TEXT("and one hit_taken, the blow on the wearer: it raises what a blow taken raises"), HitsTaken.Count, 1);
	return true;
}

// ==========================================================================
// 6. A gadget's own death blast strikes its wearer inside the radius (B2, B3)
// ==========================================================================

CATACLYSM_DEPLOYABLE_SELF_BLAST_TEST(FCataclysmDeployableSelfBlastDeathBlastTest,
	"Cataclysm.DeployableSelfBlast.AGadgetsDeathBlastStrikesItsWearerInsideTheRadiusForWhatAnEnemyBesideItTakes")
{
	using namespace CataclysmDeployableSelfBlastTest;

	// THREE GROUPS, EACH HOLDING SHARED RUIN'S TWO STATS, 10% of the dying minion's maximum health within 5 m: the plain
	// player, holding no flag, and the wearer inside, each 3 m along X from its Ultimate gadget; and the wearer outside,
	// 8 m along X. In each, an enemy 3 m along X and 1.5 m along Y from the gadget, 1.5 m from the player inside. THE
	// BLAST IS CALLED AS THE DEATH HOOK CALLS IT, on a gadget still standing.
	FDeployableSelfBlastWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	if (!TestNotNull(TEXT("set-up: the critical strike roll is pinned to 100"), NeverCritical.Variable))
	{
		return false;
	}
	const FVector PlainAt(0.0f, 0.0f, 0.0f);
	const FVector InsideAt(GroupApart, 0.0f, 0.0f);
	const FVector OutsideAt(2.0f * GroupApart, 0.0f, 0.0f);
	FDeployableSelfBlastRig Plain(World, PlainAt + FVector(3.0f * M, 0.0f, 0.0f));
	FDeployableSelfBlastRig Inside(World, InsideAt + FVector(3.0f * M, 0.0f, 0.0f));
	FDeployableSelfBlastRig Outside(World, OutsideAt + FVector(8.0f * M, 0.0f, 0.0f));
	if (!Plain.IsUsable(*this, ECataclysmAbilitySlot::Ultimate) || !Inside.IsUsable(*this, ECataclysmAbilitySlot::Ultimate)
		|| !Outside.IsUsable(*this, ECataclysmAbilitySlot::Ultimate))
	{
		return false;
	}
	const FDeployableSelfBlastRow Share =
		DeployableSelfBlastFlat(ACataclysmMinion::DeathBlastPercentOfMaximumHealthStat, 10.0f);
	const FDeployableSelfBlastRow Radius = DeployableSelfBlastFlat(ACataclysmMinion::DeathBlastRadiusMetresStat, 5.0f);
	Plain.Holds({Share, Radius});
	Inside.Holds({Share, Radius, DeployableSelfBlastFlag()});
	Outside.Holds({Share, Radius, DeployableSelfBlastFlag()});
	ACataclysmMinion* PlainGadget = DeployableSelfBlastPlace(*this, Plain, ECataclysmAbilitySlot::Ultimate, PlainAt);
	ACataclysmMinion* InsideGadget = DeployableSelfBlastPlace(*this, Inside, ECataclysmAbilitySlot::Ultimate, InsideAt);
	ACataclysmMinion* OutsideGadget =
		DeployableSelfBlastPlace(*this, Outside, ECataclysmAbilitySlot::Ultimate, OutsideAt);
	if (!PlainGadget || !InsideGadget || !OutsideGadget)
	{
		return false;
	}
	const FVector EnemyOffset(3.0f * M, 1.5f * M, 0.0f);
	ACataclysmEnemyCharacter* PlainEnemy = DeployableSelfBlastEnemy(World, PlainAt + EnemyOffset);
	ACataclysmEnemyCharacter* InsideEnemy = DeployableSelfBlastEnemy(World, InsideAt + EnemyOffset);
	ACataclysmEnemyCharacter* OutsideEnemy = DeployableSelfBlastEnemy(World, OutsideAt + EnemyOffset);
	if (!TestNotNull(TEXT("set-up: the plain group's enemy"), PlainEnemy)
		|| !TestNotNull(TEXT("set-up: the inside group's enemy"), InsideEnemy)
		|| !TestNotNull(TEXT("set-up: the outside group's enemy"), OutsideEnemy))
	{
		return false;
	}
	if (!DeployableSelfBlastSides(*this, TEXT("plain"), Plain, PlainGadget, PlainEnemy)
		|| !DeployableSelfBlastSides(*this, TEXT("inside"), Inside, InsideGadget, InsideEnemy)
		|| !DeployableSelfBlastSides(*this, TEXT("outside"), Outside, OutsideGadget, OutsideEnemy))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: the plain player's gadget's blast strikes its enemy and not the plain player.
	TestEqual(TEXT("control: the plain gadget's blast catches one enemy"), ACataclysmMinion::DeathBlast(PlainGadget, Plain.Player), 1);
	TestTrue(TEXT("control: and strikes it"), DeployableSelfBlastHealthOf(PlainEnemy) < 50000.0f);
	TestEqual(TEXT("control: the plain player 3 m away, holding no flag, loses nothing"),
		DeployableSelfBlastHealthOf(Plain.Player), 10000.0f, 0.001f);

	// THE WEARER INSIDE.
	TestEqual(TEXT("the inside gadget's blast still counts one enemy caught: the wearer is not in the count"),
		ACataclysmMinion::DeathBlast(InsideGadget, Inside.Player), 1);
	const float EnemyLost = 50000.0f - DeployableSelfBlastHealthOf(InsideEnemy);
	const float WearerLost = 10000.0f - DeployableSelfBlastHealthOf(Inside.Player);
	TestTrue(*FString::Printf(TEXT("set-up: the enemy beside the wearer is struck for %.2f"), EnemyLost), EnemyLost > 0.0f);
	TestEqual(*FString::Printf(TEXT("the wearer 3 m from its gadget is struck for what the enemy beside it took: %.2f "
									"against %.2f"),
							   WearerLost, EnemyLost),
		WearerLost, EnemyLost, 0.01f);

	// THE WEARER OUTSIDE, 8 M FROM ITS GADGET, beyond the 5 m.
	TestEqual(TEXT("set-up: the outside gadget's blast catches one enemy"),
		ACataclysmMinion::DeathBlast(OutsideGadget, Outside.Player), 1);
	TestEqual(TEXT("the wearer 8 m from its gadget, outside the 5 m, loses nothing"),
		DeployableSelfBlastHealthOf(Outside.Player), 10000.0f, 0.001f);
	return true;
}

#undef CATACLYSM_DEPLOYABLE_SELF_BLAST_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
