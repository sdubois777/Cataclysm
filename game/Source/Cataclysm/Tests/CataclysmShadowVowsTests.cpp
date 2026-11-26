// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmMovement.h"
#include "AbilitySystem/CataclysmShadowImages.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Shadow's last four vow options and its keystone Ever Moving (the Death line's finishing slice E, 2026-11-26):
 * Afterimage (#2906), Shadow Clone (#2907), Shadow Step (#2909), Flurry of the Dead, and Ever Moving.
 *
 * EVERY FIGURE IS READ FROM THE SHIPPED ROWS (the owner, issue #2928): each test takes its node's rows from the passive
 * effect table through `UCataclysmPassiveTree::ModifiersFor`, as a character spending the node does, and reads the
 * numbers it expects (15 metres, every third kill, 5 seconds, every 10 Fervour up to 10, half) off the same rows. A
 * stale table fails at set-up and names the editor step.
 *
 * NOTHING HERE WAITS ON A TICK. A test world is never ticked, so a clone's step and an afterimage's repeat are called
 * directly with a time of the test's own, as `UCataclysmBasicAttack`'s decisions are tested.
 */
namespace CataclysmShadowVowsTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	constexpr float M = 100.0f;

	FName Death()
	{
		return FName(TEXT("Death"));
	}

	/** The shipped row of `Node` (option `Option`) on `Stat`, or null with the reason added as an error. */
	const FCataclysmPassiveEffectRow* ShippedRow(FAutomationTestBase& Test, const TCHAR* Node, int32 Option,
												 const TCHAR* Stat)
	{
		const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
		if (Effects)
		{
			for (const FCataclysmPassiveEffectRow* Row : UCataclysmPassiveTree::EffectsFor(Effects, FName(Node)))
			{
				if (Row && Row->Option == Option && Row->Stat == Stat)
				{
					return Row;
				}
			}
		}
		Test.AddError(FString::Printf(TEXT("set-up: the shipped passive effect table has no row of %s option %d on %s. "
											"If game/Data/PassiveEffects.csv has it, the asset is stale: run python "
											"tools/run_editor_python.py tools/generate_datatable_assets.py"),
									  Node, Option, Stat));
		return nullptr;
	}

	/**
	 * What the shipped tables grant a character holding one point in `Node`, with `Option` chosen when it is above
	 * nought, as stat lines an ability system takes: every stat's modifiers on a base of nought.
	 */
	TMap<FName, FCataclysmStatInputs> ShippedLines(const TCHAR* Node, int32 Option = 0)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(Node), 1);
		if (Option > 0)
		{
			Allocation.SetChosenOption(FName(Node), Option);
		}
		TMap<FName, FCataclysmStatInputs> Lines;
		for (const TPair<FName, TArray<FCataclysmStatModifier>>& Row : UCataclysmPassiveTree::ModifiersFor(
				 Allocation, UCataclysmPassiveTree::LoadNodeTable(), UCataclysmPassiveTree::LoadEffectTable(), {Death()}))
		{
			Lines.FindOrAdd(Row.Key).Modifiers = Row.Value;
		}
		return Lines;
	}

	float HealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	UCataclysmAbilitySystemComponent* SystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	/** A creature on the players' side at `Where`, its critical strikes off, holding a Fervour pool of 200. */
	ACataclysmEnemyCharacter* WearerAt(UWorld* World, const FVector& Where, float AttackDamage = 100.0f)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		UCataclysmAbilitySystemComponent* System = Made ? SystemOf(Made) : nullptr;
		if (!System)
		{
			return nullptr;
		}
		Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		Made->SetAttackDamage(AttackDamage);
		Made->SetHealth(10'000.0f);
		System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 0.0f);
		System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Made));
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 200.0f);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);
		return Made;
	}

	/**
	 * A creature of the monsters' side at `Where`, facing `Yaw`, holding `Health`, with none of the defences that would
	 * make a blow a matter of chance. It deals nothing.
	 */
	ACataclysmEnemyCharacter* CreatureAt(UWorld* World, const FVector& Where, float Health = 100'000.0f,
										 float Yaw = 0.0f, int32 Rung = 0)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator(0.0f, Yaw, 0.0f));
		UAbilitySystemComponent* Its = Made ? Made->GetAbilitySystemComponent() : nullptr;
		if (!Its)
		{
			return nullptr;
		}
		Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Made->SetHealth(Health);
		Made->SetArmour(0.0f);
		Made->SetAttackDamage(0.0f);
		Made->RarityStep = Rung;
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		return Made;
	}

	/** A box that blocks everything, for a wall between two characters. */
	AActor* WallAt(UWorld* World, const FVector& Where, const FVector& Extent)
	{
		AActor* Solid = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
		if (!Solid)
		{
			return nullptr;
		}
		UBoxComponent* Body = NewObject<UBoxComponent>(Solid);
		Body->InitBoxExtent(Extent);
		Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Body->SetCollisionObjectType(ECC_WorldStatic);
		Body->SetCollisionResponseToAllChannels(ECR_Block);
		Solid->SetRootComponent(Body);
		Body->RegisterComponent();
		Solid->SetActorLocation(Where);
		return Solid;
	}

	/**
	 * A possessed player character holding a Death Dagger, given 100 attack damage and one swing a second by hand, and
	 * the rows of its basic attack and its movement skill, Veilstep (a blink of 12 metres). Its stat lines are the
	 * shipped rows of one node, given by `Holds`.
	 */
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
			Slots->SetDamageType(TEXT("Death"));
			Slots->EquipWeaponType(TEXT("Dagger"));
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 1.0f);
			Set(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 0.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			bStep = UCataclysmTriggeredSkill::HeldSkillNamed(Player, FName(TEXT("Veilstep")), Step);
			// AND THE DAGGER'S HEAVY, Gravepoint, a melee strike that is not a spell: the skill attack an afterimage
			// repeats (the basic attack it never does).
			bAttack = UCataclysmTriggeredSkill::HeldSkillNamed(Player, FName(TEXT("Gravepoint")), Attack);
			const FCataclysmWeaponSkill* Held = UCataclysmTriggeredSkill::HeldBasicAttack(Player);
			bBasic = Held != nullptr;
			if (Held)
			{
				Basic = *Held;
			}
		}

		bool IsUsable(FAutomationTestBase& Test) const
		{
			if (!Player || !System || !Slots || !bStep || !bBasic || !bAttack)
			{
				Test.AddError(TEXT("set-up: a possessed player holding a Death Dagger, with its basic attack, "
								   "Veilstep and Gravepoint. If a row is missing, DT_WeaponSkills may be older than "
								   "game/Data/WeaponSkills.csv: run tools/generate_datatable_assets.py"));
				return false;
			}
			return true;
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			System->SetNumericAttributeBase(Attribute, Value);
		}

		/** The player's stat lines become the shipped rows of `Node`, `Option` chosen; nothing with `bHolds` false. */
		void Holds(const TCHAR* Node, int32 Option, bool bHolds) const
		{
			System->SetStatInputs(bHolds ? ShippedLines(Node, Option) : TMap<FName, FCataclysmStatInputs>());
		}

		/** Tells the world this player paid for this skill, aimed here, as `CommitAndBegin` does. */
		void Uses(const FCataclysmWeaponSkill& Skill, const FVector& Aim) const
		{
			UCataclysmCombatEvents::NoteSkillUsed(Player, Skill.Name, Skill.Tags, Skill.Slot, &Aim,
												  /*bHasCooldown=*/Skill.Slot != ECataclysmAbilitySlot::BasicAttack);
		}

		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
		FCataclysmWeaponSkill Step;
		FCataclysmWeaponSkill Basic;
		FCataclysmWeaponSkill Attack;
		bool bStep = false;
		bool bBasic = false;
		bool bAttack = false;
	};
}

// ==========================================================================
// Ever Moving
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowVowsEverMovingTest,
	"Cataclysm.ShadowVows.EverMovingEvadesAreaDamageAtItsShareOfTheChanceOnlyWhileMoving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ever Moving's shipped row on a defender of 60 evasion. Moving, an area hit is evaded on a roll just under the row's
 * share of 60 (half: 30) and lands on one just over it, while a direct attack on that same roll is still evaded.
 * CONTROLS: standing still, or without the row, an area hit lands on a roll of nought.
 */
bool FCataclysmShadowVowsEverMovingTest::RunTest(const FString&)
{
	using namespace CataclysmShadowVowsTest;

	const FCataclysmPassiveEffectRow* Shipped =
		ShippedRow(*this, TEXT("Shadow_keystone_d_kB"), 0, UCataclysmDamageCalculation::EvasionAgainstAreaPercentStat);
	if (!Shipped)
	{
		return false;
	}
	TestEqual(TEXT("set-up: the shipped row is asked only while moving"), Shipped->Condition, FString(TEXT("while_moving")));
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmEnemyCharacter* Defender = WearerAt(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* System = SystemOf(Defender);
	if (!TestNotNull(TEXT("set-up: a defender"), System))
	{
		return false;
	}
	constexpr float Evasion = 60.0f;
	System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), Evasion);
	const float Share = Evasion * Shipped->ValuePerPoint / 100.0f;

	FCataclysmIncomingHit Area;
	Area.Damage = 100.0f;
	Area.bIsArea = true;
	FCataclysmIncomingHit Direct = Area;
	Direct.bIsArea = false;
	const auto Evaded = [System](const FCataclysmIncomingHit& Hit, float Roll)
	{
		return UCataclysmDamageCalculation::Resolve(Hit, System, 1, Roll, /*BlockRoll=*/100.0f).bEvaded;
	};

	System->SetStatInputs(ShippedLines(TEXT("Shadow_keystone_d_kB")));
	System->NoteMovedMetres(1.0f);
	const bool bUnder = Evaded(Area, Share - 0.1f);
	const bool bOver = Evaded(Area, Share + 0.1f);
	const bool bDirectOver = Evaded(Direct, Share + 0.1f);
	System->NoteDidNotMove();
	const bool bStanding = Evaded(Area, 0.0f);
	System->SetStatInputs(TMap<FName, FCataclysmStatInputs>());
	System->NoteMovedMetres(1.0f);
	const bool bNoRow = Evaded(Area, 0.0f);

	AddInfo(FString::Printf(TEXT("EVER MOVING: the share %.0f%% of %.0f evasion is %.1f; moving, an area hit rolled under "
								 "it evaded %d and over it %d, a direct hit over it %d; standing %d; without the row %d"),
							Shipped->ValuePerPoint, Evasion, Share, bUnder, bOver, bDirectOver, bStanding, bNoRow));
	TestTrue(TEXT("moving, an area hit rolled under the share of the chance is evaded"), bUnder);
	TestFalse(TEXT("and one rolled over it lands: the share, not the whole chance"), bOver);
	TestTrue(TEXT("CONTROL: a direct attack on that roll is evaded, at the whole chance"), bDirectOver);
	TestFalse(TEXT("CONTROL: standing still, an area hit rolled at nought lands"), bStanding);
	TestFalse(TEXT("CONTROL: without the row, an area hit rolled at nought lands while moving"), bNoRow);
	return true;
}

// ==========================================================================
// Shadow Step
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowVowsShadowStepTargetTest,
	"Cataclysm.ShadowVows.ShadowStepTakesTheEnemyAimedAtWithinItsMetresThatTheMoverCanSee",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Shadow Step's target, with the shipped row: an enemy aimed at 8 metres away is taken; aimed at one a metre past the
 * row's metres, or at bare ground, or at one behind a wall, nothing is. CONTROL: without the row, the enemy at 8 metres
 * is not taken.
 */
bool FCataclysmShadowVowsShadowStepTargetTest::RunTest(const FString&)
{
	using namespace CataclysmShadowVowsTest;

	const FCataclysmPassiveEffectRow* Shipped =
		ShippedRow(*this, TEXT("Shadow_capstone_100"), 1, UCataclysmShadowImages::ShadowStepMetresStat);
	if (!Shipped)
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const float Metres = Shipped->ValuePerPoint;
	ACataclysmEnemyCharacter* Mover = WearerAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Near = CreatureAt(World, FVector(8.0f * M, 0, 0));
	ACataclysmEnemyCharacter* Beyond = CreatureAt(World, FVector(0, (Metres + 1.0f) * M, 0));
	ACataclysmEnemyCharacter* Hidden = CreatureAt(World, FVector(-6.0f * M, 0, 0));
	AActor* Wall = WallAt(World, FVector(-3.0f * M, 0, 0), FVector(20.0f, 300.0f, 300.0f));
	UCataclysmAbilitySystemComponent* System = SystemOf(Mover);
	if (!TestNotNull(TEXT("set-up: a mover, three creatures and a wall"), Near && Beyond && Hidden && Wall ? System : nullptr))
	{
		return false;
	}
	const FGameplayTagContainer Tags;
	const AActor* Plain = UCataclysmShadowImages::ShadowStepTarget(Mover, Near->GetActorLocation(), Tags);
	System->SetStatInputs(ShippedLines(TEXT("Shadow_capstone_100"), 1));
	const AActor* AimedAtNear = UCataclysmShadowImages::ShadowStepTarget(Mover, Near->GetActorLocation(), Tags);
	const AActor* AimedOnItsBody = UCataclysmShadowImages::ShadowStepTarget(
		Mover, Near->GetActorLocation() + FVector(0, UCataclysmTargeting::BodyRadiusOf(Near), 0), Tags);
	const AActor* AimedAtBeyond = UCataclysmShadowImages::ShadowStepTarget(Mover, Beyond->GetActorLocation(), Tags);
	const AActor* AimedAtGround = UCataclysmShadowImages::ShadowStepTarget(Mover, FVector(4.0f * M, 4.0f * M, 0), Tags);
	const AActor* AimedAtHidden = UCataclysmShadowImages::ShadowStepTarget(Mover, Hidden->GetActorLocation(), Tags);

	AddInfo(FString::Printf(TEXT("SHADOW STEP: %.0f metres; near %s, its edge %s, beyond %s, ground %s, hidden %s, "
								 "without the row %s"),
							Metres, *GetNameSafe(AimedAtNear), *GetNameSafe(AimedOnItsBody), *GetNameSafe(AimedAtBeyond),
							*GetNameSafe(AimedAtGround), *GetNameSafe(AimedAtHidden), *GetNameSafe(Plain)));
	TestTrue(TEXT("an enemy aimed at within the row's metres is taken"), AimedAtNear == Near);
	TestTrue(TEXT("and so it is when the aim is on the edge of its body"), AimedOnItsBody == Near);
	TestNull(TEXT("CONTROL: one a metre past the row's metres is not"), AimedAtBeyond);
	TestNull(TEXT("CONTROL: bare ground is not an enemy"), AimedAtGround);
	TestNull(TEXT("CONTROL: one behind a wall cannot be seen and is not taken"), AimedAtHidden);
	TestNull(TEXT("CONTROL: without the row nothing is taken"), Plain);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowVowsShadowStepMovesBehindTest,
	"Cataclysm.ShadowVows.ShadowStepPutsTheMovementSkillBehindTheEnemyFacingIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The movement skill itself: a player holding a Death Dagger uses Veilstep, a 12 metre blink, aimed at a creature 8
 * metres away that faces +Y. With the shipped row the player arrives behind it, on its -Y side, its capsule touching
 * the creature's, and faces it. CONTROL: without the row the same blink stops in front of the creature, on the line it
 * was aimed along.
 */
bool FCataclysmShadowVowsShadowStepMovesBehindTest::RunTest(const FString&)
{
	using namespace CataclysmShadowVowsTest;

	FVector Arrived[2];
	float Touching = 0.0f;
	float Facing = 0.0f;
	for (int32 WithRow = 0; WithRow < 2; ++WithRow)
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FRig Rig(World, FVector::ZeroVector);
		ACataclysmEnemyCharacter* Creature = CreatureAt(World, FVector(8.0f * M, 0, 0), 100'000.0f, /*Yaw=*/90.0f);
		if (!Rig.IsUsable(*this) || !TestNotNull(TEXT("set-up: a creature"), Creature))
		{
			return false;
		}
		Rig.Holds(TEXT("Shadow_capstone_100"), 1, WithRow == 1);
		const FVector Here = Rig.Player->GetActorLocation();
		if (!TestTrue(TEXT("set-up: Veilstep starts"),
					  UCataclysmTriggeredSkill::Trigger(Rig.Player, Rig.Step, Creature->GetActorLocation(),
														/*DamageShare=*/1.0f, /*bPaysCost=*/false, &Here)))
		{
			return false;
		}
		Arrived[WithRow] = Rig.Player->GetActorLocation();
		if (WithRow == 1)
		{
			Touching = UCataclysmTargeting::BodyRadiusOf(Rig.Player) + UCataclysmTargeting::BodyRadiusOf(Creature);
			const FVector Toward = (Creature->GetActorLocation() - Arrived[1]).GetSafeNormal2D();
			Facing = FVector::DotProduct(Rig.Player->GetActorForwardVector().GetSafeNormal2D(), Toward);
			const float Apart = FVector::Dist2D(Arrived[1], Creature->GetActorLocation());
			TestTrue(TEXT("with the row the player stands outside the creature"), Apart >= Touching - 0.5f);
			TestTrue(TEXT("and beside it, within a 10 cm step of touching"),
					 Apart <= Touching + UCataclysmMovement::BehindGapCm + 10.5f);
		}
	}

	AddInfo(FString::Printf(TEXT("SHADOW STEP: without the row the player arrived at (%.0f, %.0f), with it at (%.0f, "
								 "%.0f); facing the creature %.3f"),
							Arrived[0].X, Arrived[0].Y, Arrived[1].X, Arrived[1].Y, Facing));
	TestTrue(TEXT("with the row the player is behind the creature, which faces +Y"), Arrived[1].Y < -Touching * 0.5f);
	TestEqual(TEXT("and straight behind it"), static_cast<float>(Arrived[1].X), 8.0f * M, 5.0f);
	TestTrue(TEXT("and faces it"), Facing > 0.99f);
	TestTrue(TEXT("CONTROL: without the row the blink stops in front of it, on the line it was aimed along"),
			 FMath::Abs(Arrived[0].Y) < 5.0f && Arrived[0].X < 8.0f * M);
	return true;
}

// ==========================================================================
// Afterimage
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowVowsAfterimageTest,
	"Cataclysm.ShadowVows.AfterimageRepeatsTheNextAttackFromWhereTheMovementSkillStarted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Afterimage with the shipped row. Veilstep's use is announced with the player at the origin, and the player is then
 * moved 12 metres away; a creature stands a metre from the origin. A BASIC SWING is announced aimed at it first, and
 * leaves the afterimage waiting, with nothing repeated (the coordinating session's ruling of 2026-11-26: the afterimage
 * repeats the next SKILL that is an attack, never the basic attack). The next skill attack, Gravepoint, is then announced
 * aimed at it, and its repeat, made as the next tick makes it, strikes the creature from the origin, out of the player's
 * own reach. The afterimage is then gone, and a second attack is given to nothing. CONTROL: without the row nothing is
 * left and the creature loses nothing.
 */
bool FCataclysmShadowVowsAfterimageTest::RunTest(const FString&)
{
	using namespace CataclysmShadowVowsTest;

	float Lost[2] = {0.0f, 0.0f};
	bool bLeft[2] = {false, false};
	int32 Repeated[2] = {0, 0};
	bool bSecondGiven = true;
	bool bGoneAfter = false;
	bool bWaitedThroughTheBasic = false;
	int32 RepeatedByTheBasic = -1;
	for (int32 WithRow = 0; WithRow < 2; ++WithRow)
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FRig Rig(World, FVector::ZeroVector);
		ACataclysmEnemyCharacter* Creature = CreatureAt(World, FVector(1.0f * M, 0, 0));
		if (!Rig.IsUsable(*this) || !TestNotNull(TEXT("set-up: a creature"), Creature))
		{
			return false;
		}
		Rig.Holds(TEXT("Shadow_capstone_25"), 2, WithRow == 1);
		Rig.Uses(Rig.Step, FVector(12.0f * M, 0, 0));
		const ACataclysmShadowImage* Standing = UCataclysmShadowImages::StandingAfterimageOf(Rig.Player);
		bLeft[WithRow] = Standing != nullptr;
		if (Standing)
		{
			TestTrue(TEXT("the afterimage stands where the movement skill started"),
					 FVector::Dist2D(Standing->GetActorLocation(), FVector::ZeroVector) < 1.0f);
		}
		Rig.Player->SetActorLocation(FVector(12.0f * M, 0, 0));
		const float Was = HealthOf(Creature);
		// A BASIC SWING FIRST: it gives the afterimage nothing.
		Rig.Uses(Rig.Basic, Creature->GetActorLocation());
		if (WithRow == 1)
		{
			RepeatedByTheBasic = UCataclysmShadowImages::MakeAfterimageRepeats(Rig.Player);
			bWaitedThroughTheBasic = UCataclysmShadowImages::StandingAfterimageOf(Rig.Player) != nullptr;
		}
		// THEN A SKILL ATTACK, which it repeats.
		Rig.Uses(Rig.Attack, Creature->GetActorLocation());
		Repeated[WithRow] = UCataclysmShadowImages::MakeAfterimageRepeats(Rig.Player);
		Lost[WithRow] = Was - HealthOf(Creature);
		if (WithRow == 1)
		{
			bGoneAfter = UCataclysmShadowImages::StandingAfterimageOf(Rig.Player) == nullptr;
			FCataclysmSkillUsedNotice Again;
			Again.User = Rig.Player;
			Again.SkillName = FName(*Rig.Attack.Name);
			Again.Slot = Rig.Attack.Slot;
			Again.Aim = Creature->GetActorLocation();
			bSecondGiven = UCataclysmShadowImages::GiveTheAttackToTheAfterimage(Rig.Player, Again, false) != nullptr;
		}
	}

	AddInfo(FString::Printf(TEXT("AFTERIMAGE: without the row left %d, repeated %d, the creature lost %.1f; with it left "
								 "%d, repeated %d, lost %.1f"),
							bLeft[0], Repeated[0], Lost[0], bLeft[1], Repeated[1], Lost[1]));
	TestTrue(TEXT("with the row the movement skill leaves an afterimage"), bLeft[1]);
	TestEqual(TEXT("A BASIC SWING IS NOT REPEATED"), RepeatedByTheBasic, 0);
	TestTrue(TEXT("and leaves the afterimage waiting"), bWaitedThroughTheBasic);
	TestEqual(TEXT("and the next skill attack is repeated once"), Repeated[1], 1);
	TestTrue(TEXT("from the afterimage's place, 11 metres from the player: the creature is struck"), Lost[1] > 0.0f);
	TestTrue(TEXT("and the afterimage is spent"), bGoneAfter);
	TestFalse(TEXT("so a second attack is given to nothing"), bSecondGiven);
	TestFalse(TEXT("CONTROL: without the row nothing is left"), bLeft[0]);
	TestEqual(TEXT("CONTROL: and the creature out of the player's reach loses nothing"), Lost[0], 0.0f, 0.001f);
	return true;
}

// ==========================================================================
// Shadow Clone
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowVowsShadowCloneCountTest,
	"Cataclysm.ShadowVows.ShadowCloneIsLeftByEveryThirdEliteOrStrongerKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Shadow Clone's count with the shipped rows: Elite kills one short of the row's count, with a Common kill among them,
 * leave no clone; the next Elite (or stronger: a Boss) kill does, for the row's seconds, and the count starts again.
 * CONTROL: without the rows, as many Elite kills leave nothing.
 */
bool FCataclysmShadowVowsShadowCloneCountTest::RunTest(const FString&)
{
	using namespace CataclysmShadowVowsTest;

	const FCataclysmPassiveEffectRow* Every =
		ShippedRow(*this, TEXT("Shadow_capstone_50"), 2, UCataclysmShadowImages::CloneEveryEliteKillsStat);
	const FCataclysmPassiveEffectRow* Seconds =
		ShippedRow(*this, TEXT("Shadow_capstone_50"), 2, UCataclysmShadowImages::CloneSecondsStat);
	if (!Every || !Seconds)
	{
		return false;
	}
	const int32 Count = FMath::RoundToInt(Every->ValuePerPoint);
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmEnemyCharacter* Killer = WearerAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Elite = CreatureAt(World, FVector(5.0f * M, 0, 0), 1000.0f, 0.0f, /*Rung=*/1);
	ACataclysmEnemyCharacter* Boss = CreatureAt(World, FVector(5.0f * M, 3.0f * M, 0), 1000.0f, 0.0f, /*Rung=*/4);
	ACataclysmEnemyCharacter* Common = CreatureAt(World, FVector(5.0f * M, -3.0f * M, 0), 1000.0f, 0.0f, /*Rung=*/0);
	ACataclysmEnemyCharacter* Other = WearerAt(World, FVector(-5.0f * M, 0, 0));
	UCataclysmAbilitySystemComponent* System = SystemOf(Killer);
	if (!TestNotNull(TEXT("set-up: two killers and three creatures"), Elite && Boss && Common && Other ? System : nullptr))
	{
		return false;
	}

	// CONTROL FIRST, on another killer holding no row.
	int32 MadeWithoutTheRows = 0;
	for (int32 Kill = 0; Kill < Count * 2; ++Kill)
	{
		MadeWithoutTheRows += UCataclysmShadowImages::NoteKillForShadowClone(Other, Elite) ? 1 : 0;
	}

	System->SetStatInputs(ShippedLines(TEXT("Shadow_capstone_50"), 2));
	int32 MadeEarly = 0;
	for (int32 Kill = 0; Kill < Count - 1; ++Kill)
	{
		MadeEarly += UCataclysmShadowImages::NoteKillForShadowClone(Killer, Elite) ? 1 : 0;
		MadeEarly += UCataclysmShadowImages::NoteKillForShadowClone(Killer, Common) ? 1 : 0;
	}
	const int32 Toward = UCataclysmShadowImages::EliteKillsTowardTheNextClone(Killer);
	const ACataclysmShadowImage* Clone = UCataclysmShadowImages::NoteKillForShadowClone(Killer, Boss);
	const int32 TowardAfter = UCataclysmShadowImages::EliteKillsTowardTheNextClone(Killer);

	AddInfo(FString::Printf(TEXT("SHADOW CLONE: every %d Elite kills for %.0f seconds; %d made early, %d counted before "
								 "the last, %s made by it, %d counted after; %d made without the rows"),
							Count, Seconds->ValuePerPoint, MadeEarly, Toward, Clone ? TEXT("one") : TEXT("none"),
							TowardAfter, MadeWithoutTheRows));
	TestEqual(TEXT("Elite kills one short of the count leave no clone, a Common kill among them"), MadeEarly, 0);
	TestEqual(TEXT("and a Common kill is not counted"), Toward, Count - 1);
	if (TestNotNull(TEXT("the next Elite-or-stronger kill (a Boss's) leaves a clone"), Clone))
	{
		TestTrue(TEXT("it is a clone of the killer"),
				 Clone->Kind == ECataclysmShadowImageKind::Clone && Clone->Maker.Get() == Killer);
		TestEqual(TEXT("for the row's seconds"), Clone->ExpiresAtSeconds - World->GetTimeSeconds(),
				  static_cast<double>(Seconds->ValuePerPoint), 0.001);
	}
	TestEqual(TEXT("and the count starts again"), TowardAfter, 0);
	TestEqual(TEXT("CONTROL: without the rows twice as many Elite kills leave nothing"), MadeWithoutTheRows, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowVowsShadowCloneFightsTest,
	"Cataclysm.ShadowVows.AShadowCloneSwingsYourBasicAttackAtYourRateUntilItsSecondsRunOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A clone the shipped rows left, of a player holding a Death Dagger who swings once a second. A creature a metre from
 * the clone, 11 metres from the player, is struck by the clone's first step; half a second later it is not struck
 * (CONTROL: the player's rate holds the clone); a second later it is struck again; and at the row's seconds the clone is
 * gone and strikes nothing. A creature 6 metres from a second clone is walked toward, not struck.
 */
bool FCataclysmShadowVowsShadowCloneFightsTest::RunTest(const FString&)
{
	using namespace CataclysmShadowVowsTest;

	const FCataclysmPassiveEffectRow* Seconds =
		ShippedRow(*this, TEXT("Shadow_capstone_50"), 2, UCataclysmShadowImages::CloneSecondsStat);
	const FCataclysmPassiveEffectRow* Every =
		ShippedRow(*this, TEXT("Shadow_capstone_50"), 2, UCataclysmShadowImages::CloneEveryEliteKillsStat);
	if (!Seconds || !Every)
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FRig Rig(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Elite = CreatureAt(World, FVector(-20.0f * M, 0, 0), 1000.0f, 0.0f, /*Rung=*/1);
	if (!Rig.IsUsable(*this) || !TestNotNull(TEXT("set-up: an Elite to kill"), Elite))
	{
		return false;
	}
	Rig.Holds(TEXT("Shadow_capstone_50"), 2, true);
	ACataclysmShadowImage* Clone = nullptr;
	for (int32 Kill = 0; Kill < FMath::RoundToInt(Every->ValuePerPoint) && !Clone; ++Kill)
	{
		Clone = UCataclysmShadowImages::NoteKillForShadowClone(Rig.Player, Elite);
	}
	if (!TestNotNull(TEXT("set-up: the kills leave a clone"), Clone))
	{
		return false;
	}
	Clone->SetActorLocation(FVector(11.0f * M, 0, 0));
	ACataclysmEnemyCharacter* Creature = CreatureAt(World, FVector(12.0f * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a creature beside the clone"), Creature))
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	const auto Struck = [Clone, Creature](double At)
	{
		const float Was = HealthOf(Creature);
		const bool bSwung = Clone->CloneStep(At);
		return TPair<bool, float>(bSwung, Was - HealthOf(Creature));
	};
	const TPair<bool, float> First = Struck(Now);
	const TPair<bool, float> TooSoon = Struck(Now + 0.5);
	const TPair<bool, float> Second = Struck(Now + 1.0);
	const int32 Swings = Clone->AttacksStarted;
	const double Expiry = Clone->ExpiresAtSeconds;
	const TPair<bool, float> Expired = Struck(Expiry);
	const bool bGone = !IsValid(Clone) || Clone->IsActorBeingDestroyed();

	AddInfo(FString::Printf(TEXT("SHADOW CLONE: first step swung %d (%.1f), half a second on %d (%.1f), a second on %d "
								 "(%.1f), at its %.0f seconds %d (%.1f), gone %d"),
							First.Key, First.Value, TooSoon.Key, TooSoon.Value, Second.Key, Second.Value,
							Seconds->ValuePerPoint, Expired.Key, Expired.Value, bGone));
	TestTrue(TEXT("the clone's first step swings the player's basic attack"), First.Key);
	TestTrue(TEXT("and strikes the creature from the clone's place, 11 metres from the player"), First.Value > 0.0f);
	TestFalse(TEXT("CONTROL: half a second later it waits for the player's rate"), TooSoon.Key);
	TestEqual(TEXT("CONTROL: and strikes nothing"), TooSoon.Value, 0.0f, 0.001f);
	TestTrue(TEXT("a second later it swings again"), Second.Key && Second.Value > 0.0f);
	TestEqual(TEXT("two swings in all"), Swings, 2);
	TestEqual(TEXT("it lasts the row's seconds"), Expiry - Now, static_cast<double>(Seconds->ValuePerPoint), 0.001);
	TestFalse(TEXT("at its seconds it swings no more"), Expired.Key);
	TestEqual(TEXT("and strikes nothing"), Expired.Value, 0.0f, 0.001f);
	TestTrue(TEXT("and is gone"), bGone);

	// A SECOND CLONE, WITH ITS ENEMY BEYOND THE BASIC ATTACK'S REACH: it walks toward it a quarter of a second's worth.
	ACataclysmShadowImage* Walker = nullptr;
	for (int32 Kill = 0; Kill < FMath::RoundToInt(Every->ValuePerPoint) && !Walker; ++Kill)
	{
		Walker = UCataclysmShadowImages::NoteKillForShadowClone(Rig.Player, Elite);
	}
	if (!TestNotNull(TEXT("set-up: a second clone"), Walker))
	{
		return false;
	}
	Walker->SetActorLocation(FVector(0, 30.0f * M, 0));
	ACataclysmEnemyCharacter* Far = CreatureAt(World, FVector(6.0f * M, 30.0f * M, 0));
	if (!TestNotNull(TEXT("set-up: a creature 6 metres from it"), Far))
	{
		return false;
	}
	const float FarWas = HealthOf(Far);
	const bool bWalkerSwungFirst = Walker->CloneStep(Now);
	const bool bWalkerSwung = Walker->CloneStep(Now + ACataclysmShadowImage::LongestStepSeconds);
	const float Walked = Walker->GetActorLocation().X;
	TestFalse(TEXT("a clone whose enemy is beyond its reach does not swing"), bWalkerSwungFirst || bWalkerSwung);
	TestEqual(TEXT("and strikes nothing"), FarWas - HealthOf(Far), 0.0f, 0.001f);
	TestTrue(TEXT("it walks toward it"), Walked > 1.0f);
	return true;
}

// ==========================================================================
// Flurry of the Dead
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowVowsFlurryTest,
	"Cataclysm.ShadowVows.FlurryOfTheDeadStrikesAgainOnceForEveryTenFervourHeldUpToTen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Flurry of the Dead with the shipped row: one hit of a wearer holding a step's worth of Fervour less one takes what
 * one hit takes; holding three and a half steps' worth, four hits' worth (one and three extra); holding past the cap,
 * the cap and one. CONTROL: a blow a row struck in answer to an event strikes no more, whatever is held.
 */
bool FCataclysmShadowVowsFlurryTest::RunTest(const FString&)
{
	using namespace CataclysmShadowVowsTest;

	const FCataclysmPassiveEffectRow* Shipped =
		ShippedRow(*this, TEXT("Shadow_capstone_200"), 3, UCataclysmShadowImages::ExtraStrikesStat);
	if (!Shipped)
	{
		return false;
	}
	TestEqual(TEXT("set-up: the shipped row scales by the Fervour held"), Shipped->Scale,
			  FString(TEXT("class_resource_held")));
	const float Step = Shipped->ScaleStep;
	const int32 Cap = Shipped->ScaleMaxSteps;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmEnemyCharacter* Wearer = WearerAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature = CreatureAt(World, FVector(1.0f * M, 0, 0), 100'000.0f);
	UCataclysmAbilitySystemComponent* System = SystemOf(Wearer);
	if (!TestNotNull(TEXT("set-up: a wearer and a creature"), Creature ? System : nullptr)
		|| !TestTrue(TEXT("set-up: a step and a cap"), Step > 0.0f && Cap > 0))
	{
		return false;
	}
	System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
									Step * (Cap + 5));
	System->SetStatInputs(ShippedLines(TEXT("Shadow_capstone_200"), 3));
	FCataclysmHitDelivery Delivery;
	Delivery.bCannotBeEvaded = true;
	Delivery.bCannotCriticallyStrike = true;
	const auto LostToOneHit = [System, Wearer, Creature](float Fervour, const FCataclysmHitDelivery& How)
	{
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Fervour);
		const float Was = HealthOf(Creature);
		UCataclysmSkillEffects::ApplyHit(Wearer, Creature, 100.0f, FGameplayTagContainer(), How);
		return Was - HealthOf(Creature);
	};
	const float One = LostToOneHit(Step - 1.0f, Delivery);
	const float ThreeAndAHalf = LostToOneHit(Step * 3.5f, Delivery);
	const float PastTheCap = LostToOneHit(Step * (Cap + 4), Delivery);
	FCataclysmHitDelivery Answer = Delivery;
	Answer.bIsARowsAnswerToAnEvent = true;
	const float AnAnswer = LostToOneHit(Step * 3.5f, Answer);

	AddInfo(FString::Printf(TEXT("FLURRY OF THE DEAD: 1 for every %.0f Fervour up to %d; one hit took %.2f under a step, "
								 "%.2f at three and a half steps, %.2f past the cap; a row's answer %.2f"),
							Step, Cap, One, ThreeAndAHalf, PastTheCap, AnAnswer));
	if (!TestTrue(TEXT("set-up: a hit takes something"), One > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("three and a half steps of Fervour: the hit and three more"), ThreeAndAHalf / One, 4.0f, 0.001f);
	TestEqual(TEXT("past the cap: the hit and the cap's strikes, no more"), PastTheCap / One,
			  static_cast<float>(Cap + 1), 0.001f);
	TestEqual(TEXT("CONTROL: a row's answer to an event strikes once"), AnAnswer / One, 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowVowsFlurryPaysOnHitEffectsTest,
	"Cataclysm.ShadowVows.FlurryOfTheDeadsExtraStrikesPayOnHitEffectsLikeAnyHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The project owner's ruling of 2026-11-26 ("yes"): Flurry of the Dead's extra strikes trigger on-hit effects as any hit
 * does. A possessed player holding Flurry of the Dead's shipped row and one flat row of `fervour_per_critical_strike`
 * (5), every blow a critical strike, holds three and a half steps' worth of Fervour and lands one hit on a creature: it
 * deals four hits' worth (the hit and three extra strikes, every one a critical strike) and gains the critical strike's
 * Fervour FOUR TIMES. THE CONTROL: the same player without Flurry's row deals one hit's worth and gains it once.
 *
 * AND AN EXTRA STRIKE STRIKES NO FURTHER EXTRAS: a hit delivered as `UCataclysmShadowImages::StrikeAgain` delivers each
 * extra strike (a row's answer to an event, marked an extra strike), by the player holding Flurry, deals one hit's worth
 * and gains the critical strike's Fervour once. Were the extra strikes to strike again, the four above would be more.
 */
bool FCataclysmShadowVowsFlurryPaysOnHitEffectsTest::RunTest(const FString&)
{
	using namespace CataclysmShadowVowsTest;

	const FCataclysmPassiveEffectRow* Shipped =
		ShippedRow(*this, TEXT("Shadow_capstone_200"), 3, UCataclysmShadowImages::ExtraStrikesStat);
	if (!Shipped || !TestTrue(TEXT("set-up: a step"), Shipped->ScaleStep > 0.0f))
	{
		return false;
	}
	const float Step = Shipped->ScaleStep;
	constexpr float PerCritical = 5.0f;
	// EVERY BLOW A CRITICAL STRIKE: each delivery states a chance of 100, the cap is raised to 100, and the roll is pinned
	// at 50. The character's own chance attribute alone did not do it: in window dfb-w1 every hit landed for 100, none
	// critical, and no Fervour was paid.
	const CataclysmTestWorld::FScopedCritRoll Pinned(50.0f);

	// 0: without Flurry; 1: with it; 2: with it, the hit delivered as an extra strike is.
	float Lost[3] = {0.0f, 0.0f, 0.0f};
	float Gained[3] = {0.0f, 0.0f, 0.0f};
	for (int32 Case = 0; Case < 3; ++Case)
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FRig Rig(World, FVector::ZeroVector);
		ACataclysmEnemyCharacter* Creature = CreatureAt(World, FVector(1.0f * M, 0, 0), 100'000.0f);
		if (!Rig.IsUsable(*this) || !TestNotNull(TEXT("set-up: a creature"), Creature))
		{
			return false;
		}
		TMap<FName, FCataclysmStatInputs> Lines =
			Case > 0 ? ShippedLines(TEXT("Shadow_capstone_200"), 3) : TMap<FName, FCataclysmStatInputs>();
		FCataclysmStatModifier PerCrit;
		PerCrit.Bucket = ECataclysmStatBucket::Flat;
		PerCrit.Source = ECataclysmModifierSource::PassiveKeystone;
		PerCrit.Value = PerCritical;
		Lines.FindOrAdd(FName(UCataclysmFervour::PerCriticalStrikeStat)).Modifiers.Add(PerCrit);
		Rig.System->SetStatInputs(MoveTemp(Lines));
		Rig.Set(UCataclysmCombatAttributeSet::GetMaxCritChanceAttribute(), 100.0f);
		Rig.Set(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 100.0f);
		// AND A MULTIPLIER: the damage calculation rolls no critical strike for a multiplier of nought, the test player's.
		Rig.Set(UCataclysmCombatAttributeSet::GetCritMultiplierAttribute(), 200.0f);
		Rig.Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), Step * 10.0f);
		Rig.Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Step * 3.5f);

		FCataclysmHitDelivery Delivery;
		Delivery.bCannotBeEvaded = true;
		Delivery.CritChancePercent = 100.0f;
		if (Case == 2)
		{
			Delivery.bIsARowsAnswerToAnEvent = true;
			Delivery.bIsAnExtraStrike = true;
		}
		const float Was = HealthOf(Creature);
		const float FervourWas = Rig.System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		UCataclysmSkillEffects::ApplyHit(Rig.Player, Creature, 100.0f, FGameplayTagContainer(), Delivery);
		Lost[Case] = Was - HealthOf(Creature);
		Gained[Case] =
			Rig.System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute()) - FervourWas;
	}

	AddInfo(FString::Printf(TEXT("FLURRY ON-HIT: without the row lost %.1f, gained %.1f Fervour; with it lost %.1f, gained "
								 "%.1f; an extra strike alone lost %.1f, gained %.1f"),
							Lost[0], Gained[0], Lost[1], Gained[1], Lost[2], Gained[2]));
	if (!TestTrue(TEXT("set-up: the control's hit landed and paid its critical strike's Fervour"),
				  Lost[0] > 0.0f && Gained[0] > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("THE EXTRA STRIKES ARE FULL HITS: four hits' worth"), Lost[1] / Lost[0], 4.0f, 0.001f);
	TestEqual(TEXT("AND EACH PAYS THE CRITICAL STRIKE'S FERVOUR: four times the control's"), Gained[1], Gained[0] * 4.0f,
			  0.001f);
	TestEqual(TEXT("AN EXTRA STRIKE STRIKES NO FURTHER EXTRAS: one hit's worth"), Lost[2] / Lost[0], 1.0f, 0.001f);
	TestEqual(TEXT("and pays its critical strike's Fervour once"), Gained[2], Gained[0], 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowVowsFlurryCallsTheCrowsTest,
	"Cataclysm.ShadowVows.AFlurryExtraStrikesCriticalStrikeCallsTheCrowsWithinTheirQuarterSecond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The coordinating session's ruling of 2026-11-26, within the project owner's "yes" to Flurry of the Dead's extra strikes
 * triggering on-hit effects: an extra strike that critically strikes calls the crows as any critical strike does, and the
 * crows' own quarter second still holds. A possessed player whose every blow is a critical strike, holding Flurry of the
 * Dead's shipped row and a running buff stating `Duration=8; Radius=10; StrikesOnCritical=1` (Carrion Crows'):
 *   - THE CONTROL: a hit delivered as a worn row's answer to an event, and not as an extra strike, calls no crow;
 *   - a hit delivered as `UCataclysmShadowImages::StrikeAgain` delivers an extra strike calls one;
 *   - another in the same instant calls none: one a `CrowCooldownSeconds`;
 *   - 0.3 s later, with three and a half steps of Fervour, one ordinary hit and its three extra strikes, all in one
 *     instant and all critical strikes, call one more crow and not four.
 */
bool FCataclysmShadowVowsFlurryCallsTheCrowsTest::RunTest(const FString&)
{
	using namespace CataclysmShadowVowsTest;

	const FCataclysmPassiveEffectRow* Shipped =
		ShippedRow(*this, TEXT("Shadow_capstone_200"), 3, UCataclysmShadowImages::ExtraStrikesStat);
	if (!Shipped || !TestTrue(TEXT("set-up: a step"), Shipped->ScaleStep > 0.0f))
	{
		return false;
	}
	const float Step = Shipped->ScaleStep;
	// EVERY BLOW A CRITICAL STRIKE: each delivery states a chance of 100, the cap is raised to 100, and the roll is pinned
	// at 50, as `FlurryOfTheDeadsExtraStrikesPayOnHitEffectsLikeAnyHit` does.
	const CataclysmTestWorld::FScopedCritRoll Pinned(50.0f);

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FRig Rig(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature = CreatureAt(World, FVector(1.0f * M, 0, 0), 100'000.0f);
	if (!Rig.IsUsable(*this) || !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	Rig.System->SetStatInputs(ShippedLines(TEXT("Shadow_capstone_200"), 3));
	Rig.Set(UCataclysmCombatAttributeSet::GetMaxCritChanceAttribute(), 100.0f);
	Rig.Set(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 100.0f);
	// AND A MULTIPLIER: the damage calculation rolls no critical strike for a multiplier of nought, the test player's.
	Rig.Set(UCataclysmCombatAttributeSet::GetCritMultiplierAttribute(), 200.0f);
	Rig.Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), Step * 10.0f);
	Rig.Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);

	const FGameplayAbilitySpecHandle Handle =
		Rig.System->GiveAbilityInSlot(UCataclysmSelfBuffSkill::StaticClass(), ECataclysmAbilitySlot::Support,
									  /*Level=*/100, Rig.Player);
	FGameplayAbilitySpec* Spec = Handle.IsValid() ? Rig.System->FindAbilitySpecFromHandle(Handle) : nullptr;
	UCataclysmSelfBuffSkill* Crows = Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("set-up: the crows' buff is granted"), Crows))
	{
		return false;
	}
	Crows->SkillName = TEXT("A murder of crows");
	Crows->Params = UCataclysmSkillShapes::ParseParams(TEXT("Duration=8; Radius=10; StrikesOnCritical=1"));
	Crows->DamagePercentOverride = 40.0f;
	if (!TestTrue(TEXT("set-up: the buff goes up"),
				  Rig.System->TryActivateAbility(Crows->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false)))
	{
		return false;
	}

	FCataclysmHitDelivery AnAnswer;
	AnAnswer.bCannotBeEvaded = true;
	AnAnswer.CritChancePercent = 100.0f;
	AnAnswer.bIsARowsAnswerToAnEvent = true;
	FCataclysmHitDelivery AnExtraStrike = AnAnswer;
	AnExtraStrike.bIsAnExtraStrike = true;

	UCataclysmSkillEffects::ApplyHit(Rig.Player, Creature, 100.0f, FGameplayTagContainer(), AnAnswer);
	const int32 AfterTheAnswer = Crows->CrowsSent;
	UCataclysmSkillEffects::ApplyHit(Rig.Player, Creature, 100.0f, FGameplayTagContainer(), AnExtraStrike);
	const int32 AfterTheExtraStrike = Crows->CrowsSent;
	UCataclysmSkillEffects::ApplyHit(Rig.Player, Creature, 100.0f, FGameplayTagContainer(), AnExtraStrike);
	const int32 AfterTheSecondAtOnce = Crows->CrowsSent;

	World->TimeSeconds += 0.3f;
	Rig.Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Step * 3.5f);
	FCataclysmHitDelivery Ordinary;
	Ordinary.bCannotBeEvaded = true;
	Ordinary.CritChancePercent = 100.0f;
	UCataclysmSkillEffects::ApplyHit(Rig.Player, Creature, 100.0f, FGameplayTagContainer(), Ordinary);
	const int32 AfterTheFlurry = Crows->CrowsSent;

	AddInfo(FString::Printf(TEXT("FLURRY AND THE CROWS: %d after a row's answer, %d after an extra strike, %d after a "
								 "second at once, %d after a flurry 0.3 s later"),
							AfterTheAnswer, AfterTheExtraStrike, AfterTheSecondAtOnce, AfterTheFlurry));
	TestEqual(TEXT("CONTROL: a row's answer to an event calls no crow"), AfterTheAnswer, 0);
	TestEqual(TEXT("AN EXTRA STRIKE'S CRITICAL STRIKE CALLS ONE"), AfterTheExtraStrike, 1);
	TestEqual(TEXT("ANOTHER IN THE SAME INSTANT CALLS NONE: one a quarter second"), AfterTheSecondAtOnce, 1);
	TestEqual(TEXT("A FLURRY 0.3 S LATER, FOUR CRITICAL STRIKES IN ONE INSTANT, CALLS ONE MORE AND NOT FOUR"),
			  AfterTheFlurry, 2);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
