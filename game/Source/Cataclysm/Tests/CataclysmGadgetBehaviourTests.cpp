// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * What a summoner's rows make its gadgets do. Ruled 2026-10-13 by the coordinating session under the owner's
 * delegation, one group of tests for each of four sentences of `game/Data/EnchantmentsPositive.csv`.
 *
 * A GADGET IS A BOLT TURRET OR A BALLISTA. A Spike Trap carries `Type.Trap` and is a trap (ruled 2026-10-07), and
 * every group here shows a holder's Spike Trap and a holder's imp doing what they did before.
 *
 * NO ROW IS AUTHORED, SO EACH STAT IS GRANTED BY HAND, as a flat figure requiring `Type.Deployable`, which is the
 * row the enchantment session is to write. So these tests pass with no row at all. The test that closes that reads
 * the ROW, and it lands with the rows.
 *
 * THE SUMMONER IS A REAL CREATURE ON THE PLAYERS' SIDE AND NOT A BARE ACTOR. A bare actor has no side, a minion
 * takes its summoner's side, and having no side is hostile to everything: two gadgets of a bare summoner would
 * find each other, and the summoner, when they search for enemies within reach.
 *
 * NOTHING HERE WAITS ON A TICK. A target is chosen by calling the brain's `ChooseTarget`, and a gadget's step by
 * calling `ACataclysmMinion::GadgetStep` after the world's clock has been moved by hand.
 */
namespace CataclysmGadgetBehaviourTest
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
	 * A real creature on one side, holding `Health` of a maximum of 100,000 and none of the defences that would
	 * make a blow a matter of chance: no armour, no evasion, no block and no resistance. It deals nothing.
	 */
	ACataclysmEnemyCharacter* Stand(UWorld* World, const FVector& Where, ECataclysmTeam Team, float Health)
	{
		ACataclysmEnemyCharacter* Made =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		UAbilitySystemComponent* Its = Made ? Made->GetAbilitySystemComponent() : nullptr;
		if (!Its)
		{
			return nullptr;
		}
		Made->SetGenericTeamId(UCataclysmTeams::IdFor(Team));
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

	/** An enemy of the players' side. */
	ACataclysmEnemyCharacter* Enemy(UWorld* World, const FVector& Where, float Health)
	{
		return Stand(World, Where, ECataclysmTeam::Monsters, Health);
	}

	/** A character on the players' side to summon from. */
	ACataclysmEnemyCharacter* Summoner(UWorld* World, const FVector& Where)
	{
		return Stand(World, Where, ECataclysmTeam::Players, 100000.0f);
	}

	float HealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	void SetHealthOf(AActor* Who, float Health)
	{
		if (UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who))
		{
			Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
		}
	}

	FGameplayTag DeployableTag()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Deployable")), /*ErrorIfNotFound=*/false);
	}

	/**
	 * One row a summoner holds: a stat, a figure, whether it is a flat figure or an increase, and whether it
	 * requires `Type.Deployable`. A row that is not scoped is how a passive keystone's flag is held: Conduit's
	 * `minion_hits_count_as_yours` is asked with no tags, and a row requiring one would not match.
	 */
	struct FGadgetRow
	{
		FName Stat;
		float Value = 0.0f;
		ECataclysmStatBucket Bucket = ECataclysmStatBucket::Flat;
		bool bScoped = true;
	};

	/**
	 * Give a summoner the stat line the proposed rows would: each row requiring `Type.Deployable` and flat unless
	 * it says otherwise. An increase is how "Gadgets fire 20%-40% faster" is written. Two rows on one stat are two
	 * rows. REPLACES THE WHOLE STAT LINE, so everything a test's summoner holds arrives in one call.
	 */
	void GiveGadgetRows(AActor* Who, const TArray<FGadgetRow>& Rows)
	{
		UCataclysmAbilitySystemComponent* Theirs =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Who));
		if (!Theirs)
		{
			return;
		}

		TMap<FName, FCataclysmStatInputs> Stats;
		for (const FGadgetRow& Each : Rows)
		{
			FCataclysmStatModifier Row;
			Row.Bucket = Each.Bucket;
			Row.Source = ECataclysmModifierSource::Enchantment;
			Row.Value = Each.Value;
			if (Each.bScoped)
			{
				Row.RequiredTags.AddTag(DeployableTag());
			}
			FCataclysmStatInputs& Line = Stats.FindOrAdd(Each.Stat);
			Line.Base = 0.0f;
			Line.Modifiers.Add(Row);
		}
		Theirs->SetStatInputs(MoveTemp(Stats));
	}

	/** One minion of a named type standing for ten minutes, or null with the reason already reported. */
	ACataclysmMinion* Place(FAutomationTestBase& Test, AActor* Of, const TCHAR* Type, const FVector& Where)
	{
		ACataclysmMinion* Made = ACataclysmMinion::Spawn(
			Of, Where, /*Lifetime=*/600.0f, /*bBurns=*/false, FString(Type));
		if (!Made || Made->TypeName != FString(Type))
		{
			Test.AddError(FString::Printf(
				TEXT("set-up: DT_MinionTypes could not supply the %s row. Run tools/generate_datatable_assets.py"),
				Type));
			return nullptr;
		}
		return Made;
	}

	/** What this character's brain would attack now, or null; null also for a character with no brain. */
	AActor* ChoiceOf(const APawn* Driven)
	{
		const ACataclysmEnemyController* Brain =
			Driven ? Cast<ACataclysmEnemyController>(Driven->GetController()) : nullptr;
		return Brain ? Brain->ChooseTarget() : nullptr;
	}

	/** Whether this character has the shared brain at all, which every choice above depends on. */
	bool HasABrain(const APawn* Driven)
	{
		return Driven && Cast<ACataclysmEnemyController>(Driven->GetController()) != nullptr;
	}

	/**
	 * Three enemies in a line from a gadget at `Origin`, along X: the nearest at 3 m holding 300, the middle one
	 * holding 900 and the furthest holding 500. So the nearest, the one with the most health and the furthest are
	 * three different enemies. A fourth holding 5,000 stands at `BeyondMetres`, outside the gadget's reach.
	 *
	 * WITH `Everyone` ABOVE NOUGHT ALL FOUR HOLD THAT INSTEAD, for a test that strikes them: a machine's blow
	 * rises with its summoner's level, which for a summoner that is no player is a console variable, so a figure
	 * that must outlast several blows is 50,000 and never a few hundred.
	 */
	struct FLine
	{
		FLine(UWorld* World, const FVector& Origin, float MiddleMetres, float FarMetres, float BeyondMetres,
			  float Everyone = 0.0f)
		{
			const bool bAlike = Everyone > 0.0f;
			Near = Enemy(World, Origin + FVector(3.0f * M, 0.0f, 0.0f), bAlike ? Everyone : 300.0f);
			Middle = Enemy(World, Origin + FVector(MiddleMetres * M, 0.0f, 0.0f), bAlike ? Everyone : 900.0f);
			Far = Enemy(World, Origin + FVector(FarMetres * M, 0.0f, 0.0f), bAlike ? Everyone : 500.0f);
			Beyond = Enemy(World, Origin + FVector(BeyondMetres * M, 0.0f, 0.0f), bAlike ? Everyone : 5000.0f);
		}

		bool IsWhole() const { return Near && Middle && Far && Beyond; }

		ACataclysmEnemyCharacter* Near = nullptr;
		ACataclysmEnemyCharacter* Middle = nullptr;
		ACataclysmEnemyCharacter* Far = nullptr;
		ACataclysmEnemyCharacter* Beyond = nullptr;
	};

	/** How many times one event is raised on a character's ability system while this is alive. */
	struct FEventCount
	{
		FEventCount(AActor* Who, const TCHAR* InEvent)
			: System(Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Who)))
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

	/** Run the one job a gadget has on the quarter-second step, as `RegenerationStep` calls it. */
	void Step(ACataclysmMinion* Gadget)
	{
		ACataclysmMinion::GadgetStep(Gadget);
	}

	/** The health of three enemies of a line together, so a blow on any of them shows as a fall. */
	float HealthOfThoseWithinReach(const FLine& Line)
	{
		return HealthOf(Line.Near) + HealthOf(Line.Middle) + HealthOf(Line.Far);
	}

	bool IsDead(const AActor* Who)
	{
		return UCataclysmSkillEffects::IsDead(Who);
	}

	/** What dealt the last blow on record on this character, or null: what a death names its killer's blow by. */
	AActor* WhatDealtTheLastBlowOn(const AActor* Who)
	{
		const UCataclysmAbilitySystemComponent* Its =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Who));
		return Its && Its->GetLastBlow().IsOnRecord() ? Its->GetLastBlow().DealtBy.Get() : nullptr;
	}

	/** Whom the last blow on record on this character is credited to, or null. */
	AActor* WhoIsCreditedWithTheLastBlowOn(const AActor* Who)
	{
		const UCataclysmAbilitySystemComponent* Its =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Who));
		return Its && Its->GetLastBlow().IsOnRecord() ? Its->GetLastBlow().Attacker.Get() : nullptr;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// "Your gadgets prioritize the highest HP enemy within their range instead of
// the nearest"
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetTargetsMostHealthTest,
	"Cataclysm.GadgetTargets.AHoldersTurretAndBallistaChooseTheMostHealthWithinReachAndTheNearerOfTwoAlike")
{
	using namespace CataclysmGadgetBehaviourTest;

	// TWO SUMMONERS, ALIKE BUT FOR THE FLAG, each with a Bolt Turret and a Ballista, and each gadget with a line
	// of enemies of its own. The four stations are 100 m apart, far outside any reach or notice distance. The
	// control summoner's turret is read first.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector ControlTurretAt(0.0f, 0.0f, 0.0f);
	const FVector HolderTurretAt(0.0f, 100.0f * M, 0.0f);
	const FVector ControlBallistaAt(0.0f, 200.0f * M, 0.0f);
	const FVector HolderBallistaAt(0.0f, 300.0f * M, 0.0f);
	ACataclysmEnemyCharacter* Control = Summoner(World, FVector(-50.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Holder = Summoner(World, FVector(-50.0f * M, 100.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a control summoner"), Control) || !TestNotNull(TEXT("set-up: a holder"), Holder)
		|| !TestTrue(TEXT("set-up: the tag Type.Deployable exists"), DeployableTag().IsValid()))
	{
		return false;
	}
	GiveGadgetRows(Holder, {{FName(ACataclysmMinion::GadgetsTargetMostHealthStat), 1.0f}});

	// A TURRET REACHES 10 M: enemies at 3, 6 and 9 m, and one at 12. A BALLISTA REACHES 15: 3, 7 and 13, and 18.
	ACataclysmMinion* ControlTurret = Place(*this, Control, TEXT("BoltTurret"), ControlTurretAt);
	ACataclysmMinion* HolderTurret = Place(*this, Holder, TEXT("BoltTurret"), HolderTurretAt);
	ACataclysmMinion* ControlBallista = Place(*this, Control, TEXT("Ballista"), ControlBallistaAt);
	ACataclysmMinion* HolderBallista = Place(*this, Holder, TEXT("Ballista"), HolderBallistaAt);
	if (!ControlTurret || !HolderTurret || !ControlBallista || !HolderBallista)
	{
		return false;
	}
	FLine ControlLine(World, ControlTurretAt, 6.0f, 9.0f, 12.0f);
	FLine HolderLine(World, HolderTurretAt, 6.0f, 9.0f, 12.0f);
	FLine ControlBallistaLine(World, ControlBallistaAt, 7.0f, 13.0f, 18.0f);
	FLine HolderBallistaLine(World, HolderBallistaAt, 7.0f, 13.0f, 18.0f);
	if (!TestTrue(TEXT("set-up: sixteen enemies"), ControlLine.IsWhole() && HolderLine.IsWhole()
			&& ControlBallistaLine.IsWhole() && HolderBallistaLine.IsWhole())
		|| !TestTrue(TEXT("set-up: each gadget has the shared brain"), HasABrain(ControlTurret)
			&& HasABrain(HolderTurret) && HasABrain(ControlBallista) && HasABrain(HolderBallista))
		|| !TestTrue(TEXT("set-up: all four are gadgets"), ControlTurret->IsGadget() && HolderTurret->IsGadget()
			&& ControlBallista->IsGadget() && HolderBallista->IsGadget()))
	{
		return false;
	}

	// THE CONTROL: with no row the turret takes the nearest, and is offered no target by health.
	if (!TestTrue(TEXT("control: a turret whose summoner holds no row chooses the nearest enemy"),
			ChoiceOf(ControlTurret) == ControlLine.Near))
	{
		return false;
	}
	TestNull(TEXT("control: and it is offered no target by health"), ControlTurret->MostHealthWithinReach());

	// THE HOLDER'S TURRET: the 900 at 6 m, which is neither the nearest nor the furthest, and not the 5,000 that
	// stands 2 m beyond its reach.
	TestTrue(TEXT("a holder's turret chooses the enemy with the most health within its reach"),
		ChoiceOf(HolderTurret) == HolderLine.Middle);
	TestFalse(TEXT("and not one with more health that stands beyond its reach"),
		ChoiceOf(HolderTurret) == HolderLine.Beyond);

	// CHOSEN AGAIN EACH TIME: the 900 falls to 100, and the 500 at 9 m now holds the most.
	SetHealthOf(HolderLine.Middle, 100.0f);
	TestTrue(TEXT("when that enemy's health falls below another's, the next choice is the other"),
		ChoiceOf(HolderTurret) == HolderLine.Far);

	// TWO HOLDING THE SAME: the nearest is raised to the furthest's 500, and the nearer of the two is taken.
	SetHealthOf(HolderLine.Near, 500.0f);
	TestTrue(TEXT("of two enemies holding the same health it chooses the nearer"),
		ChoiceOf(HolderTurret) == HolderLine.Near);

	// THE BALLISTA, WHOSE TYPE ROW SAYS `Furthest`. The holder's takes the 900 at 7 m; the control's is read only
	// to show it does not, since which of the other two it takes is not this layer's to say.
	TestTrue(TEXT("a holder's ballista chooses the enemy with the most health within its reach"),
		ChoiceOf(HolderBallista) == HolderBallistaLine.Middle);
	TestFalse(TEXT("control: a ballista whose summoner holds no row does not choose by health"),
		ChoiceOf(ControlBallista) == ControlBallistaLine.Middle);

	// NOTHING WITHIN REACH: the three within each turret's reach are removed, which leaves each the enemy 2 m
	// beyond it. The holder's turret is then offered no target by health and chooses what the control's chooses.
	for (FLine* Each : {&ControlLine, &HolderLine})
	{
		Each->Near->Destroy();
		Each->Middle->Destroy();
		Each->Far->Destroy();
	}
	TestNull(TEXT("with nothing within its reach a holder's turret is offered no target by health"),
		HolderTurret->MostHealthWithinReach());
	const bool bHolderTookTheOneBeyond = ChoiceOf(HolderTurret) == HolderLine.Beyond;
	const bool bControlTookTheOneBeyond = ChoiceOf(ControlTurret) == ControlLine.Beyond;
	TestTrue(TEXT("and it then chooses as the control does: each its own enemy beyond reach, or each nothing"),
		bHolderTookTheOneBeyond == bControlTookTheOneBeyond);
	TestTrue(TEXT("and neither chooses anything else"),
		(bHolderTookTheOneBeyond || ChoiceOf(HolderTurret) == nullptr)
		&& (bControlTookTheOneBeyond || ChoiceOf(ControlTurret) == nullptr));
	return true;
}

CATACLYSM_TEST(FCataclysmGadgetTargetsQuarryFirstTest,
	"Cataclysm.GadgetTargets.AQuarryMarkStillComesBeforeTheMostHealth")
{
	using namespace CataclysmGadgetBehaviourTest;

	// ONE HOLDER AND ONE TURRET. Read first with no mark: the 900 at 6 m. Then the holder marks the 300 at 3 m as
	// its quarry, and the turret takes the mark.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	ACataclysmEnemyCharacter* Holder = Summoner(World, FVector(-50.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a holder"), Holder))
	{
		return false;
	}
	GiveGadgetRows(Holder, {{FName(ACataclysmMinion::GadgetsTargetMostHealthStat), 1.0f}});
	ACataclysmMinion* Turret = Place(*this, Holder, TEXT("BoltTurret"), FVector::ZeroVector);
	FLine Line(World, FVector::ZeroVector, 6.0f, 9.0f, 12.0f);
	if (!Turret || !TestTrue(TEXT("set-up: four enemies"), Line.IsWhole())
		|| !TestTrue(TEXT("set-up: the turret has the shared brain"), HasABrain(Turret)))
	{
		return false;
	}

	if (!TestTrue(TEXT("control: with no mark a holder's turret chooses the enemy with the most health"),
			ChoiceOf(Turret) == Line.Middle))
	{
		return false;
	}

	if (!TestTrue(TEXT("set-up: the mark lands"), UCataclysmSkillEffects::ApplyNamedEffect(
			Holder, Line.Near, UCataclysmCommand::QuarryTag(), /*DurationSeconds=*/12.0f, /*Magnitude=*/0.0f,
			NAME_None))
		|| !TestTrue(TEXT("set-up: and the turret is ordered onto the marked enemy"),
			UCataclysmCommand::OrderedTargetFor(Turret) == Line.Near))
	{
		return false;
	}
	TestTrue(TEXT("a quarry mark comes first: the turret takes the marked enemy, which holds the least health"),
		ChoiceOf(Turret) == Line.Near);
	return true;
}

CATACLYSM_TEST(FCataclysmGadgetTargetsOthersUnchangedTest,
	"Cataclysm.GadgetTargets.AHoldersSpikeTrapAndImpAndAMonsterChooseAsTheyDidBefore")
{
	using namespace CataclysmGadgetBehaviourTest;

	// ONE SUMMONER, READ BEFORE IT HOLDS THE ROW AND AGAIN AFTER. Four stations 100 m apart: a Spike Trap, an
	// imp, a Bolt Turret and a monster. The trap and the imp each stand between two enemies, the nearer holding
	// 100 and the further 900, both within reach. The turret's station shows the row is really held: its choice
	// is the one that changes.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector TrapAt(0.0f, 0.0f, 0.0f);
	const FVector ImpAt(0.0f, 100.0f * M, 0.0f);
	const FVector TurretAt(0.0f, 200.0f * M, 0.0f);
	const FVector MonsterAt(0.0f, 300.0f * M, 0.0f);
	ACataclysmEnemyCharacter* Holder = Summoner(World, FVector(-50.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a summoner"), Holder))
	{
		return false;
	}

	// A SPIKE TRAP REACHES 1.5 M AND AN IMP 2 M. Nobody stands nearer than 1 m to anybody.
	ACataclysmMinion* Trap = Place(*this, Holder, TEXT("SpikeTrap"), TrapAt);
	ACataclysmEnemyCharacter* TrapNearer = Enemy(World, TrapAt + FVector(1.1f * M, 0.0f, 0.0f), 100.0f);
	ACataclysmEnemyCharacter* TrapFurther = Enemy(World, TrapAt + FVector(-1.4f * M, 0.0f, 0.0f), 900.0f);
	ACataclysmMinion* Imp = Place(*this, Holder, TEXT("Imp"), ImpAt);
	ACataclysmEnemyCharacter* ImpNearer = Enemy(World, ImpAt + FVector(1.1f * M, 0.0f, 0.0f), 100.0f);
	ACataclysmEnemyCharacter* ImpFurther = Enemy(World, ImpAt + FVector(-1.7f * M, 0.0f, 0.0f), 900.0f);
	ACataclysmMinion* Turret = Place(*this, Holder, TEXT("BoltTurret"), TurretAt);
	FLine TurretLine(World, TurretAt, 6.0f, 9.0f, 12.0f);

	// THE MONSTER: a second turret of the summoner's 3 m from it, holding a turret's small health, and a creature
	// of the players' side 6 m from it holding 100,000.
	ACataclysmEnemyCharacter* Monster = Enemy(World, MonsterAt, 1000.0f);
	ACataclysmMinion* BesideTheMonster =
		Place(*this, Holder, TEXT("BoltTurret"), MonsterAt + FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Bystander = Summoner(World, MonsterAt + FVector(-6.0f * M, 0.0f, 0.0f));
	if (!Trap || !Imp || !Turret || !BesideTheMonster
		|| !TestTrue(TEXT("set-up: the enemies and the bystander"), TrapNearer && TrapFurther && ImpNearer
			&& ImpFurther && TurretLine.IsWhole() && Monster && Bystander)
		|| !TestTrue(TEXT("set-up: each has the shared brain"), HasABrain(Trap) && HasABrain(Imp)
			&& HasABrain(Turret) && HasABrain(Monster))
		|| !TestTrue(TEXT("set-up: the trap is a placed machine carrying the trap tag, and the imp is not placed"),
			Trap->IsDeployable() && Trap->IsTrap() && !Imp->IsDeployable()))
	{
		return false;
	}
	TestFalse(TEXT("a Spike Trap is not a gadget"), Trap->IsGadget());
	TestFalse(TEXT("and neither is an imp"), Imp->IsGadget());

	// BEFORE THE ROW.
	AActor* TrapChoseBefore = ChoiceOf(Trap);
	AActor* ImpChoseBefore = ChoiceOf(Imp);
	AActor* MonsterChoseBefore = ChoiceOf(Monster);
	if (!TestTrue(TEXT("control: before the row the trap chooses the nearer of its two enemies"),
			TrapChoseBefore == TrapNearer)
		|| !TestTrue(TEXT("control: the imp chooses the nearer of its two"), ImpChoseBefore == ImpNearer)
		|| !TestTrue(TEXT("control: the turret chooses the nearest of its line"),
			ChoiceOf(Turret) == TurretLine.Near)
		|| !TestTrue(TEXT("control: and the monster chooses the turret beside it, the nearest thing it can attack"),
			MonsterChoseBefore == BesideTheMonster))
	{
		return false;
	}

	GiveGadgetRows(Holder, {{FName(ACataclysmMinion::GadgetsTargetMostHealthStat), 1.0f}});

	// AFTER IT. The turret's choice moves, so the row is held; nothing else's does.
	if (!TestTrue(TEXT("set-up: with the row the summoner's turret chooses the most health, so the row is held"),
			ChoiceOf(Turret) == TurretLine.Middle))
	{
		return false;
	}
	TestTrue(TEXT("a holder's Spike Trap still chooses the nearer enemy, with one holding more health in reach"),
		ChoiceOf(Trap) == TrapChoseBefore);
	TestNull(TEXT("and it is offered no target by health: a trap is not a gadget"), Trap->MostHealthWithinReach());
	TestTrue(TEXT("a holder's imp still chooses the nearer enemy"), ChoiceOf(Imp) == ImpChoseBefore);
	TestNull(TEXT("and it is offered no target by health either"), Imp->MostHealthWithinReach());
	TestTrue(TEXT("and a monster still chooses the nearest thing it can attack, not the one with the most health"),
		ChoiceOf(Monster) == MonsterChoseBefore);
	return true;
}

// ==========================================================================
// "Gadgets fire at all enemies within range simultaneously once every 5-8
// seconds"
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetVolleyStrikesAllTest,
	"Cataclysm.GadgetVolley.AHoldersTurretStrikesEveryEnemyWithinItsReachWhenItsClockComesRoundAndNoneBeyond")
{
	using namespace CataclysmGadgetBehaviourTest;

	// TWO SUMMONERS, ALIKE BUT FOR THE ROW, which the holder holds at 5 seconds. Each has a Bolt Turret with
	// enemies at 3, 6 and 9 m and one at 12 m, beyond its reach, every one holding 50,000. The stations are 100 m
	// apart. The clock is moved by hand and the gadget's step is called directly; no brain thinks, so no ordinary
	// shot is fired unless the test fires one.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector ControlAt(0.0f, 0.0f, 0.0f);
	const FVector HolderAt(0.0f, 100.0f * M, 0.0f);
	ACataclysmEnemyCharacter* Control = Summoner(World, FVector(-50.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Holder = Summoner(World, FVector(-50.0f * M, 100.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a control summoner"), Control) || !TestNotNull(TEXT("set-up: a holder"), Holder))
	{
		return false;
	}
	GiveGadgetRows(Holder, {{FName(ACataclysmMinion::GadgetVolleyEverySecondsStat), 5.0f}});
	ACataclysmMinion* ControlTurret = Place(*this, Control, TEXT("BoltTurret"), ControlAt);
	ACataclysmMinion* HolderTurret = Place(*this, Holder, TEXT("BoltTurret"), HolderAt);
	FLine ControlLine(World, ControlAt, 6.0f, 9.0f, 12.0f, /*Everyone=*/50000.0f);
	FLine HolderLine(World, HolderAt, 6.0f, 9.0f, 12.0f, /*Everyone=*/50000.0f);
	if (!ControlTurret || !HolderTurret
		|| !TestTrue(TEXT("set-up: eight enemies"), ControlLine.IsWhole() && HolderLine.IsWhole())
		|| !TestEqual(TEXT("set-up: the holder's turret reads its row's 5 seconds"),
			HolderTurret->VolleyEverySeconds(), 5.0f, 0.001f))
	{
		return false;
	}
	FEventCount ControlHits(Control, TEXT("deployable_hit"));
	FEventCount HolderHits(Holder, TEXT("deployable_hit"));

	// THE CONTROL'S ORDINARY BLOW, READ FIRST: one swing at its nearest enemy. Every blow of a volley below is
	// read against it. The two turrets are of one type, their summoners are alike and neither holds a row on
	// damage, so the holder's blow is the same figure.
	const float NearHeld = HealthOf(ControlLine.Near);
	ControlTurret->AttackTarget(ControlLine.Near);
	const float OneBlow = NearHeld - HealthOf(ControlLine.Near);
	if (!TestTrue(*FString::Printf(TEXT("control: a turret's ordinary blow takes health: %.2f"), OneBlow),
			OneBlow > 0.0f)
		|| !TestEqual(TEXT("control: and raises deployable_hit on its summoner once"), ControlHits.Count, 1))
	{
		return false;
	}
	const float ControlHeld = HealthOfThoseWithinReach(ControlLine);
	const float HolderHeld = HealthOfThoseWithinReach(HolderLine);
	const float HolderNearHeld = HealthOf(HolderLine.Near);
	const float HolderMiddleHeld = HealthOf(HolderLine.Middle);
	const float HolderFarHeld = HealthOf(HolderLine.Far);
	const float HolderBeyondHeld = HealthOf(HolderLine.Beyond);

	// 4.9 SECONDS AFTER THE PLACING: the clock has not come round.
	Pass(World, 4.9f);
	Step(ControlTurret);
	Step(HolderTurret);
	TestEqual(TEXT("at 4.9 seconds a holder's turret has fired no volley"), HolderTurret->VolleysFired, 0);
	TestEqual(TEXT("and its enemies have lost nothing"), HealthOfThoseWithinReach(HolderLine), HolderHeld, 0.001f);

	// 5.1 SECONDS: it has.
	Pass(World, 0.2f);
	Step(ControlTurret);
	Step(HolderTurret);
	TestEqual(TEXT("control: at 5.1 seconds the turret of a summoner holding no row has fired no volley"),
		ControlTurret->VolleysFired, 0);
	TestEqual(TEXT("control: and its enemies have lost nothing more"),
		HealthOfThoseWithinReach(ControlLine), ControlHeld, 0.001f);
	TestEqual(TEXT("control: and no further deployable_hit was raised"), ControlHits.Count, 1);
	TestEqual(TEXT("at 5.1 seconds a holder's turret has fired one volley"), HolderTurret->VolleysFired, 1);
	TestEqual(TEXT("the enemy at 3 m took one ordinary blow"),
		HolderNearHeld - HealthOf(HolderLine.Near), OneBlow, 0.05f);
	TestEqual(TEXT("the enemy at 6 m took one ordinary blow"),
		HolderMiddleHeld - HealthOf(HolderLine.Middle), OneBlow, 0.05f);
	TestEqual(TEXT("the enemy at 9 m took one ordinary blow"),
		HolderFarHeld - HealthOf(HolderLine.Far), OneBlow, 0.05f);
	TestEqual(TEXT("and the enemy beyond its reach took nothing"),
		HealthOf(HolderLine.Beyond), HolderBeyondHeld, 0.001f);
	TestEqual(TEXT("each blow of the volley raised deployable_hit on its summoner: three"), HolderHits.Count, 3);
	TestEqual(TEXT("and each was an attack of the turret's own: three"), HolderTurret->AttacksMade, 3);

	// A STEP AT THE SAME MOMENT FIRES NOTHING MORE.
	Step(HolderTurret);
	TestEqual(TEXT("a second step at the same moment fires no second volley"), HolderTurret->VolleysFired, 1);

	// THE BEAT IS KEPT FROM THE PLACING: the first volley fired at 5.1 and the second is due at 10, not at 10.1.
	Pass(World, 4.8f);
	Step(HolderTurret);
	TestEqual(TEXT("at 9.9 seconds the second volley has not fired"), HolderTurret->VolleysFired, 1);
	Pass(World, 0.15f);
	Step(HolderTurret);
	TestEqual(TEXT("at 10.05 seconds it has, 4.95 after the first fired: the clock keeps its beat from the placing"),
		HolderTurret->VolleysFired, 2);
	return true;
}

CATACLYSM_TEST(FCataclysmGadgetVolleyNotHurriedTest,
	"Cataclysm.GadgetVolley.TheVolleyIsNotHurriedByGadgetsFireFasterAndComesInAdditionToAnOrdinaryShot")
{
	using namespace CataclysmGadgetBehaviourTest;

	// THE HOLDER HOLDS THE VOLLEY AT 5 SECONDS AND "GADGETS FIRE FASTER" AT 100%: an increase of 100 on
	// `minion_attack_speed` requiring `Type.Deployable`, which halves the time between a gadget's ordinary shots.
	// The control summoner holds nothing, and its turret's time between shots is read first.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector HolderAt(0.0f, 100.0f * M, 0.0f);
	ACataclysmEnemyCharacter* Control = Summoner(World, FVector(-50.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Holder = Summoner(World, FVector(-50.0f * M, 100.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a control summoner"), Control) || !TestNotNull(TEXT("set-up: a holder"), Holder))
	{
		return false;
	}
	GiveGadgetRows(Holder, {
		{FName(ACataclysmMinion::GadgetVolleyEverySecondsStat), 5.0f},
		{FName(TEXT("minion_attack_speed")), 100.0f, ECataclysmStatBucket::Increased}});
	ACataclysmMinion* ControlTurret = Place(*this, Control, TEXT("BoltTurret"), FVector::ZeroVector);
	ACataclysmMinion* HolderTurret = Place(*this, Holder, TEXT("BoltTurret"), HolderAt);
	FLine HolderLine(World, HolderAt, 6.0f, 9.0f, 12.0f, /*Everyone=*/50000.0f);
	if (!ControlTurret || !HolderTurret || !TestTrue(TEXT("set-up: four enemies"), HolderLine.IsWhole()))
	{
		return false;
	}

	if (!TestEqual(TEXT("control: a turret whose summoner holds nothing waits its whole time between shots"),
			UCataclysmCommand::AttackIntervalScaleFor(ControlTurret, nullptr), 1.0f, 0.001f)
		|| !TestEqual(TEXT("set-up: the holder's turret waits half of it, so Gadgets fire faster is held"),
			UCataclysmCommand::AttackIntervalScaleFor(HolderTurret, nullptr), 0.5f, 0.001f))
	{
		return false;
	}
	TestEqual(TEXT("the holder's turret still reads the row's 5 seconds for its volley"),
		HolderTurret->VolleyEverySeconds(), 5.0f, 0.001f);

	// 2.6 SECONDS IS PAST HALF OF THE 5. A volley hurried as the ordinary shot is would have fired.
	Pass(World, 2.6f);
	Step(HolderTurret);
	TestEqual(TEXT("at 2.6 seconds no volley has fired: the seconds are not shortened by Gadgets fire faster"),
		HolderTurret->VolleysFired, 0);
	Pass(World, 2.3f);
	Step(HolderTurret);
	TestEqual(TEXT("nor at 4.9 seconds"), HolderTurret->VolleysFired, 0);

	// AN ORDINARY SHOT, AND THEN THE CLOCK COMES ROUND. `AttackOnce` is one swing at the nearest enemy in reach.
	const float NearHeld = HealthOf(HolderLine.Near);
	const float MiddleHeld = HealthOf(HolderLine.Middle);
	HolderTurret->AttackOnce();
	const float OneBlow = NearHeld - HealthOf(HolderLine.Near);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the ordinary shot took health from the nearest: %.2f"), OneBlow),
			OneBlow > 0.0f))
	{
		return false;
	}
	Pass(World, 0.2f);
	Step(HolderTurret);
	TestEqual(TEXT("at 5.1 seconds the volley fires, straight after an ordinary shot"),
		HolderTurret->VolleysFired, 1);
	TestEqual(TEXT("the enemy the ordinary shot struck is struck by the volley as well: two blows"),
		NearHeld - HealthOf(HolderLine.Near), OneBlow * 2.0f, 0.05f);
	TestEqual(TEXT("and an enemy the ordinary shot did not strike takes the volley's one"),
		MiddleHeld - HealthOf(HolderLine.Middle), OneBlow, 0.05f);
	TestEqual(TEXT("four attacks in all: the ordinary one and the volley's three"), HolderTurret->AttacksMade, 4);
	return true;
}

CATACLYSM_TEST(FCataclysmGadgetVolleyShorterOfTwoTest,
	"Cataclysm.GadgetVolley.TwoRowsGiveTheShorterSecondsAndARowHeldLateFiresOnTheNextStep")
{
	using namespace CataclysmGadgetBehaviourTest;

	// THREE SUMMONERS, 100 M APART, each with a Bolt Turret and a line of enemies. The first holds one row, of 8
	// seconds, and is the control. The second holds two rows, of 8 and of 5. The third holds nothing until the
	// clock stands at 8.1 seconds.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FName Volley(ACataclysmMinion::GadgetVolleyEverySecondsStat);
	ACataclysmEnemyCharacter* OneRow = Summoner(World, FVector(-50.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* TwoRows = Summoner(World, FVector(-50.0f * M, 100.0f * M, 0.0f));
	ACataclysmEnemyCharacter* Late = Summoner(World, FVector(-50.0f * M, 200.0f * M, 0.0f));
	if (!TestTrue(TEXT("set-up: three summoners"), OneRow && TwoRows && Late))
	{
		return false;
	}
	GiveGadgetRows(OneRow, {{Volley, 8.0f}});
	GiveGadgetRows(TwoRows, {{Volley, 8.0f}, {Volley, 5.0f}});
	ACataclysmMinion* OneRowTurret = Place(*this, OneRow, TEXT("BoltTurret"), FVector::ZeroVector);
	ACataclysmMinion* TwoRowsTurret = Place(*this, TwoRows, TEXT("BoltTurret"), FVector(0.0f, 100.0f * M, 0.0f));
	ACataclysmMinion* LateTurret = Place(*this, Late, TEXT("BoltTurret"), FVector(0.0f, 200.0f * M, 0.0f));
	FLine OneRowLine(World, FVector::ZeroVector, 6.0f, 9.0f, 12.0f, /*Everyone=*/50000.0f);
	FLine TwoRowsLine(World, FVector(0.0f, 100.0f * M, 0.0f), 6.0f, 9.0f, 12.0f, /*Everyone=*/50000.0f);
	FLine LateLine(World, FVector(0.0f, 200.0f * M, 0.0f), 6.0f, 9.0f, 12.0f, /*Everyone=*/50000.0f);
	if (!OneRowTurret || !TwoRowsTurret || !LateTurret
		|| !TestTrue(TEXT("set-up: twelve enemies"), OneRowLine.IsWhole() && TwoRowsLine.IsWhole()
			&& LateLine.IsWhole()))
	{
		return false;
	}

	if (!TestEqual(TEXT("control: one row of 8 is read as 8 seconds"),
			OneRowTurret->VolleyEverySeconds(), 8.0f, 0.001f))
	{
		return false;
	}
	TestEqual(TEXT("rows of 8 and 5 are read as the shorter, 5, and not as their sum"),
		TwoRowsTurret->VolleyEverySeconds(), 5.0f, 0.001f);
	TestEqual(TEXT("and no row is read as nought"), LateTurret->VolleyEverySeconds(), 0.0f, 0.001f);

	Pass(World, 5.1f);
	Step(OneRowTurret);
	Step(TwoRowsTurret);
	Step(LateTurret);
	TestEqual(TEXT("control: at 5.1 seconds the turret under one row of 8 has fired nothing"),
		OneRowTurret->VolleysFired, 0);
	TestEqual(TEXT("and the turret under rows of 8 and 5 has fired its first volley"),
		TwoRowsTurret->VolleysFired, 1);

	Pass(World, 3.0f);
	Step(OneRowTurret);
	Step(TwoRowsTurret);
	Step(LateTurret);
	TestEqual(TEXT("control: at 8.1 seconds the turret under one row of 8 fires its first"),
		OneRowTurret->VolleysFired, 1);
	TestEqual(TEXT("and the other, whose next is due at 10, has fired no second"), TwoRowsTurret->VolleysFired, 1);
	if (!TestEqual(TEXT("control: a turret whose summoner holds no row has fired nothing in 8.1 seconds"),
			LateTurret->VolleysFired, 0))
	{
		return false;
	}

	// THE ROW ARRIVES WHEN THE GADGET HAS STOOD LONGER THAN ITS SECONDS.
	const float LateHeld = HealthOfThoseWithinReach(LateLine);
	GiveGadgetRows(Late, {{Volley, 5.0f}});
	Step(LateTurret);
	TestEqual(TEXT("a gadget that comes to hold the row after standing 8.1 seconds fires on the next step"),
		LateTurret->VolleysFired, 1);
	TestTrue(TEXT("and its enemies lost health to it"), HealthOfThoseWithinReach(LateLine) < LateHeld - 0.5f);
	Step(LateTurret);
	TestEqual(TEXT("and does not fire again at the same moment"), LateTurret->VolleysFired, 1);
	Pass(World, 4.9f);
	Step(LateTurret);
	TestEqual(TEXT("nor 4.9 seconds after that volley"), LateTurret->VolleysFired, 1);
	Pass(World, 0.2f);
	Step(LateTurret);
	TestEqual(TEXT("and fires its second 5.1 seconds after it: the clock counts from that volley"),
		LateTurret->VolleysFired, 2);
	return true;
}

CATACLYSM_TEST(FCataclysmGadgetVolleyOthersTest,
	"Cataclysm.GadgetVolley.AHoldersSpikeTrapAndImpFireNoVolleyAndAnEvadedBlowOfAVolleyTakesNothing")
{
	using namespace CataclysmGadgetBehaviourTest;

	// ONE HOLDER OF THE ROW AT 5 SECONDS, with a Spike Trap, an imp and a Bolt Turret, each 100 m from the next
	// and each with enemies within its reach; and a control summoner holding nothing, with a turret and a line.
	// The enemy at 6 m in the holder's turret's line evades every blow.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector TrapAt(0.0f, 0.0f, 0.0f);
	const FVector ImpAt(0.0f, 100.0f * M, 0.0f);
	const FVector TurretAt(0.0f, 200.0f * M, 0.0f);
	const FVector ControlAt(0.0f, 300.0f * M, 0.0f);
	ACataclysmEnemyCharacter* Holder = Summoner(World, FVector(-50.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Control = Summoner(World, FVector(-50.0f * M, 300.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a holder"), Holder) || !TestNotNull(TEXT("set-up: a control summoner"), Control))
	{
		return false;
	}
	GiveGadgetRows(Holder, {{FName(ACataclysmMinion::GadgetVolleyEverySecondsStat), 5.0f}});
	ACataclysmMinion* Trap = Place(*this, Holder, TEXT("SpikeTrap"), TrapAt);
	ACataclysmEnemyCharacter* TrapNearer = Enemy(World, TrapAt + FVector(1.1f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* TrapFurther = Enemy(World, TrapAt + FVector(-1.4f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmMinion* Imp = Place(*this, Holder, TEXT("Imp"), ImpAt);
	ACataclysmEnemyCharacter* ImpNearer = Enemy(World, ImpAt + FVector(1.1f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* ImpFurther = Enemy(World, ImpAt + FVector(-1.7f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmMinion* Turret = Place(*this, Holder, TEXT("BoltTurret"), TurretAt);
	FLine Line(World, TurretAt, 6.0f, 9.0f, 12.0f, /*Everyone=*/50000.0f);
	ACataclysmMinion* ControlTurret = Place(*this, Control, TEXT("BoltTurret"), ControlAt);
	FLine ControlLine(World, ControlAt, 6.0f, 9.0f, 12.0f, /*Everyone=*/50000.0f);
	if (!Trap || !Imp || !Turret || !ControlTurret
		|| !TestTrue(TEXT("set-up: the enemies"), TrapNearer && TrapFurther && ImpNearer && ImpFurther
			&& Line.IsWhole() && ControlLine.IsWhole())
		|| !TestEqual(TEXT("set-up: the trap has two enemies within its reach"), Trap->HostilesWithinReach().Num(), 2)
		|| !TestEqual(TEXT("set-up: and so has the imp"), Imp->HostilesWithinReach().Num(), 2))
	{
		return false;
	}
	Line.Middle->GetAbilitySystemComponent()->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetEvasionAttribute(), 100.0f);
	const float TrapsHeld = HealthOf(TrapNearer) + HealthOf(TrapFurther);
	const float ImpsHeld = HealthOf(ImpNearer) + HealthOf(ImpFurther);
	const float ControlHeld = HealthOfThoseWithinReach(ControlLine);
	const float NearHeld = HealthOf(Line.Near);
	const float MiddleHeld = HealthOf(Line.Middle);
	const float FarHeld = HealthOf(Line.Far);
	FEventCount Hits(Holder, TEXT("deployable_hit"));

	Pass(World, 5.1f);
	Step(ControlTurret);
	Step(Trap);
	Step(Imp);
	Step(Turret);

	if (!TestEqual(TEXT("control: the turret of a summoner holding no row has fired no volley"),
			ControlTurret->VolleysFired, 0)
		|| !TestEqual(TEXT("control: and its enemies have lost nothing"),
			HealthOfThoseWithinReach(ControlLine), ControlHeld, 0.001f)
		|| !TestEqual(TEXT("set-up: the holder's turret has fired one, so the row is held"),
			Turret->VolleysFired, 1))
	{
		return false;
	}

	TestEqual(TEXT("a holder's Spike Trap fires no volley: a trap is not a gadget"), Trap->VolleysFired, 0);
	TestEqual(TEXT("it reads no seconds for one"), Trap->VolleyEverySeconds(), 0.0f, 0.001f);
	TestEqual(TEXT("and its two enemies have lost nothing"),
		HealthOf(TrapNearer) + HealthOf(TrapFurther), TrapsHeld, 0.001f);
	TestEqual(TEXT("a holder's imp fires no volley"), Imp->VolleysFired, 0);
	TestEqual(TEXT("it reads no seconds for one"), Imp->VolleyEverySeconds(), 0.0f, 0.001f);
	TestEqual(TEXT("and its two enemies have lost nothing"),
		HealthOf(ImpNearer) + HealthOf(ImpFurther), ImpsHeld, 0.001f);

	// EACH BLOW OF A VOLLEY IS A SINGLE BLOW AND CAN BE EVADED.
	const float NearLost = NearHeld - HealthOf(Line.Near);
	TestTrue(*FString::Printf(TEXT("the enemy at 3 m took the volley's blow: %.2f"), NearLost), NearLost > 0.0f);
	TestEqual(TEXT("the enemy at 9 m took the same"), FarHeld - HealthOf(Line.Far), NearLost, 0.05f);
	TestEqual(TEXT("and the enemy at 6 m, which evades, took nothing"), HealthOf(Line.Middle), MiddleHeld, 0.001f);
	TestEqual(TEXT("it was struck at all the same: three attacks"), Turret->AttacksMade, 3);
	TestEqual(TEXT("and deployable_hit was raised for each of the three, the evaded one included"), Hits.Count, 3);
	return true;
}

// ==========================================================================
// "When a gadget kills an enemy, it fires an additional burst at the nearest
// enemy within its range"
// ==========================================================================

CATACLYSM_TEST(FCataclysmGadgetBurstOnAKillTest,
	"Cataclysm.GadgetBurst.AHoldersTurretThatKillsStrikesTheNearestEnemyWithinItsReachOnceMore")
{
	using namespace CataclysmGadgetBehaviourTest;

	// TWO SUMMONERS, ALIKE BUT FOR THE FLAG, each with a Bolt Turret and a line: enemies at 3, 6 and 9 m and one
	// at 12 m beyond its reach, each holding 50,000 but the one at 6 m, which holds 1 and dies to any blow. Each
	// turret is told to strike the one at 6 m. Nothing here moves the clock.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector ControlAt(0.0f, 0.0f, 0.0f);
	const FVector HolderAt(0.0f, 100.0f * M, 0.0f);
	ACataclysmEnemyCharacter* Control = Summoner(World, FVector(-50.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Holder = Summoner(World, FVector(-50.0f * M, 100.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a control summoner"), Control) || !TestNotNull(TEXT("set-up: a holder"), Holder))
	{
		return false;
	}
	GiveGadgetRows(Holder, {{FName(ACataclysmMinion::GadgetsBurstOnKillStat), 1.0f}});
	ACataclysmMinion* ControlTurret = Place(*this, Control, TEXT("BoltTurret"), ControlAt);
	ACataclysmMinion* HolderTurret = Place(*this, Holder, TEXT("BoltTurret"), HolderAt);
	FLine ControlLine(World, ControlAt, 6.0f, 9.0f, 12.0f, /*Everyone=*/50000.0f);
	FLine HolderLine(World, HolderAt, 6.0f, 9.0f, 12.0f, /*Everyone=*/50000.0f);
	if (!ControlTurret || !HolderTurret
		|| !TestTrue(TEXT("set-up: eight enemies"), ControlLine.IsWhole() && HolderLine.IsWhole()))
	{
		return false;
	}
	SetHealthOf(ControlLine.Middle, 1.0f);
	SetHealthOf(HolderLine.Middle, 1.0f);
	FEventCount HolderHits(Holder, TEXT("deployable_hit"));

	// THE CONTROL: its blow kills and nothing follows. Then one ordinary blow at its nearest enemy, to read what
	// a blow of this turret takes; the two turrets and their summoners are alike but for the flag.
	const float ControlNearHeld = HealthOf(ControlLine.Near);
	ControlTurret->AttackTarget(ControlLine.Middle);
	if (!TestTrue(TEXT("control: the blow of a turret whose summoner holds no flag kills the enemy at 6 m"),
			IsDead(ControlLine.Middle))
		|| !TestEqual(TEXT("control: and it fires no burst"), ControlTurret->BurstsFired, 0)
		|| !TestEqual(TEXT("control: its nearest enemy has lost nothing"),
			HealthOf(ControlLine.Near), ControlNearHeld, 0.001f)
		|| !TestEqual(TEXT("control: and it has made the one attack"), ControlTurret->AttacksMade, 1))
	{
		return false;
	}
	ControlTurret->AttackTarget(ControlLine.Near);
	const float OneBlow = ControlNearHeld - HealthOf(ControlLine.Near);
	if (!TestTrue(*FString::Printf(TEXT("control: an ordinary blow takes health: %.2f"), OneBlow), OneBlow > 0.0f))
	{
		return false;
	}

	// THE HOLDER'S TURRET: the same blow, and one more at the nearest.
	const float NearHeld = HealthOf(HolderLine.Near);
	const float FarHeld = HealthOf(HolderLine.Far);
	const float BeyondHeld = HealthOf(HolderLine.Beyond);
	HolderTurret->AttackTarget(HolderLine.Middle);
	if (!TestTrue(TEXT("set-up: the holder's turret kills the enemy at 6 m"), IsDead(HolderLine.Middle)))
	{
		return false;
	}
	TestEqual(TEXT("a holder's turret that kills fires one burst"), HolderTurret->BurstsFired, 1);
	TestEqual(TEXT("the nearest enemy within its reach took one ordinary blow"),
		NearHeld - HealthOf(HolderLine.Near), OneBlow, 0.05f);
	TestEqual(TEXT("the enemy at 9 m, within reach and not the nearest, took nothing"),
		HealthOf(HolderLine.Far), FarHeld, 0.001f);
	TestEqual(TEXT("and the enemy beyond its reach took nothing"),
		HealthOf(HolderLine.Beyond), BeyondHeld, 0.001f);
	TestEqual(TEXT("two attacks: the blow that killed and the burst"), HolderTurret->AttacksMade, 2);
	TestEqual(TEXT("and each raised deployable_hit on its summoner"), HolderHits.Count, 2);

	// A BLOW THAT DOES NOT KILL FIRES NOTHING.
	const float NearHeldNow = HealthOf(HolderLine.Near);
	HolderTurret->AttackTarget(HolderLine.Far);
	TestFalse(TEXT("set-up: a blow on an enemy holding 50,000 does not kill it"), IsDead(HolderLine.Far));
	TestEqual(TEXT("a blow that does not kill fires no burst"), HolderTurret->BurstsFired, 1);
	TestEqual(TEXT("and the nearest enemy took nothing from it"), HealthOf(HolderLine.Near), NearHeldNow, 0.001f);

	// A KILL WITH NOBODY LEFT WITHIN REACH FIRES NOTHING. The two still within reach are removed, and a new
	// enemy holding 1 is stood 6 m away; the one 12 m away is beyond reach.
	HolderLine.Near->Destroy();
	HolderLine.Far->Destroy();
	ACataclysmEnemyCharacter* Alone = Enemy(World, HolderAt + FVector(0.0f, 6.0f * M, 0.0f), 1.0f);
	if (!TestNotNull(TEXT("set-up: one more enemy, 6 m to the side"), Alone))
	{
		return false;
	}
	HolderTurret->AttackTarget(Alone);
	TestTrue(TEXT("set-up: it dies"), IsDead(Alone));
	TestEqual(TEXT("a kill with no other enemy within reach fires no burst"), HolderTurret->BurstsFired, 1);
	TestEqual(TEXT("and the enemy beyond reach still took nothing"),
		HealthOf(HolderLine.Beyond), BeyondHeld, 0.001f);
	return true;
}

CATACLYSM_TEST(FCataclysmGadgetBurstChainTest,
	"Cataclysm.GadgetBurst.AKillByABurstFiresAnotherAndTheChainEndsAtTheFirstEnemyThatLives")
{
	using namespace CataclysmGadgetBehaviourTest;

	// EACH TURRET HAS ENEMIES AT 3, 5 AND 7 M HOLDING 1, one at 9 m holding 50,000 and one at 12 m, beyond its
	// reach. Each is told to strike the one at 7 m. For the holder that kill fires a burst at the nearest, 3 m,
	// which dies; that fires one at the nearest left, 5 m, which dies; and that fires one at 9 m, which lives.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	struct FChain
	{
		FChain(UWorld* InWorld, const FVector& At)
		{
			AtThree = Enemy(InWorld, At + FVector(3.0f * M, 0.0f, 0.0f), 1.0f);
			AtFive = Enemy(InWorld, At + FVector(5.0f * M, 0.0f, 0.0f), 1.0f);
			AtSeven = Enemy(InWorld, At + FVector(7.0f * M, 0.0f, 0.0f), 1.0f);
			AtNine = Enemy(InWorld, At + FVector(9.0f * M, 0.0f, 0.0f), 50000.0f);
			Beyond = Enemy(InWorld, At + FVector(12.0f * M, 0.0f, 0.0f), 50000.0f);
		}
		bool IsWhole() const { return AtThree && AtFive && AtSeven && AtNine && Beyond; }
		ACataclysmEnemyCharacter* AtThree = nullptr;
		ACataclysmEnemyCharacter* AtFive = nullptr;
		ACataclysmEnemyCharacter* AtSeven = nullptr;
		ACataclysmEnemyCharacter* AtNine = nullptr;
		ACataclysmEnemyCharacter* Beyond = nullptr;
	};
	const FVector ControlAt(0.0f, 0.0f, 0.0f);
	const FVector HolderAt(0.0f, 100.0f * M, 0.0f);
	ACataclysmEnemyCharacter* Control = Summoner(World, FVector(-50.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Holder = Summoner(World, FVector(-50.0f * M, 100.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a control summoner"), Control) || !TestNotNull(TEXT("set-up: a holder"), Holder))
	{
		return false;
	}
	GiveGadgetRows(Holder, {{FName(ACataclysmMinion::GadgetsBurstOnKillStat), 1.0f}});
	ACataclysmMinion* ControlTurret = Place(*this, Control, TEXT("BoltTurret"), ControlAt);
	ACataclysmMinion* HolderTurret = Place(*this, Holder, TEXT("BoltTurret"), HolderAt);
	FChain ControlChain(World, ControlAt);
	FChain HolderChain(World, HolderAt);
	if (!ControlTurret || !HolderTurret
		|| !TestTrue(TEXT("set-up: ten enemies"), ControlChain.IsWhole() && HolderChain.IsWhole()))
	{
		return false;
	}

	// THE CONTROL: one kill and no more.
	ControlTurret->AttackTarget(ControlChain.AtSeven);
	if (!TestTrue(TEXT("control: the enemy at 7 m dies"), IsDead(ControlChain.AtSeven))
		|| !TestTrue(TEXT("control: and the two others holding 1 are alive: no burst followed"),
			!IsDead(ControlChain.AtThree) && !IsDead(ControlChain.AtFive))
		|| !TestEqual(TEXT("control: no burst was fired"), ControlTurret->BurstsFired, 0))
	{
		return false;
	}

	const float NineHeld = HealthOf(HolderChain.AtNine);
	const float BeyondHeld = HealthOf(HolderChain.Beyond);
	HolderTurret->AttackTarget(HolderChain.AtSeven);
	if (!TestTrue(TEXT("set-up: the enemy at 7 m dies"), IsDead(HolderChain.AtSeven)))
	{
		return false;
	}
	TestTrue(TEXT("the burst kills the nearest, at 3 m"), IsDead(HolderChain.AtThree));
	TestTrue(TEXT("and the burst that kill fires kills the next nearest, at 5 m"), IsDead(HolderChain.AtFive));
	TestEqual(TEXT("three bursts in all: one for each kill"), HolderTurret->BurstsFired, 3);
	TestEqual(TEXT("four attacks: the first blow and the three bursts"), HolderTurret->AttacksMade, 4);
	const float NineLost = NineHeld - HealthOf(HolderChain.AtNine);
	TestTrue(*FString::Printf(TEXT("the third burst struck the enemy at 9 m: %.2f"), NineLost), NineLost > 0.0f);
	TestFalse(TEXT("which lived, so the chain ended there"), IsDead(HolderChain.AtNine));
	TestEqual(TEXT("and the enemy beyond reach took nothing"), HealthOf(HolderChain.Beyond), BeyondHeld, 0.001f);

	// AND IT WAS STRUCK ONCE: one more ordinary blow takes what the burst took.
	const float NineHeldNow = HealthOf(HolderChain.AtNine);
	HolderTurret->AttackTarget(HolderChain.AtNine);
	TestEqual(TEXT("the burst took one ordinary blow's worth from it and no more"),
		NineLost, NineHeldNow - HealthOf(HolderChain.AtNine), 0.05f);
	return true;
}

CATACLYSM_TEST(FCataclysmGadgetBurstVolleyAndConduitTest,
	"Cataclysm.GadgetBurst.AKillByAVolleysBlowFiresABurstAndUnderConduitAKillCreditedToTheSummonerStillCounts")
{
	using namespace CataclysmGadgetBehaviourTest;

	// THREE SUMMONERS, 100 M APART. The first holds the volley's row at 5 seconds and no flag, and is the
	// control. The second holds the row and the flag. Each has a turret with enemies at 3 m holding 1 and at 6
	// and 9 m holding 50,000. The third holds the flag and Conduit, `minion_hits_count_as_yours`, as a keystone
	// holds it, with no required tag; its turret has an enemy at 6 m holding 1 and one at 3 m holding 50,000.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FName Volley(ACataclysmMinion::GadgetVolleyEverySecondsStat);
	const FName Burst(ACataclysmMinion::GadgetsBurstOnKillStat);
	const FVector ControlAt(0.0f, 0.0f, 0.0f);
	const FVector HolderAt(0.0f, 100.0f * M, 0.0f);
	const FVector ConduitAt(0.0f, 200.0f * M, 0.0f);
	ACataclysmEnemyCharacter* Control = Summoner(World, FVector(-50.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Holder = Summoner(World, FVector(-50.0f * M, 100.0f * M, 0.0f));
	ACataclysmEnemyCharacter* WithConduit = Summoner(World, FVector(-50.0f * M, 200.0f * M, 0.0f));
	if (!TestTrue(TEXT("set-up: three summoners"), Control && Holder && WithConduit))
	{
		return false;
	}
	GiveGadgetRows(Control, {{Volley, 5.0f}});
	GiveGadgetRows(Holder, {{Volley, 5.0f}, {Burst, 1.0f}});
	GiveGadgetRows(WithConduit, {
		{Burst, 1.0f},
		{FName(TEXT("minion_hits_count_as_yours")), 1.0f, ECataclysmStatBucket::Flat, /*bScoped=*/false}});
	ACataclysmMinion* ControlTurret = Place(*this, Control, TEXT("BoltTurret"), ControlAt);
	ACataclysmMinion* HolderTurret = Place(*this, Holder, TEXT("BoltTurret"), HolderAt);
	ACataclysmMinion* ConduitTurret = Place(*this, WithConduit, TEXT("BoltTurret"), ConduitAt);
	ACataclysmEnemyCharacter* ControlWeak = Enemy(World, ControlAt + FVector(3.0f * M, 0.0f, 0.0f), 1.0f);
	ACataclysmEnemyCharacter* ControlSix = Enemy(World, ControlAt + FVector(6.0f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* ControlNine = Enemy(World, ControlAt + FVector(9.0f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* HolderWeak = Enemy(World, HolderAt + FVector(3.0f * M, 0.0f, 0.0f), 1.0f);
	ACataclysmEnemyCharacter* HolderSix = Enemy(World, HolderAt + FVector(6.0f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* HolderNine = Enemy(World, HolderAt + FVector(9.0f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* ConduitSturdy = Enemy(World, ConduitAt + FVector(3.0f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* ConduitWeak = Enemy(World, ConduitAt + FVector(6.0f * M, 0.0f, 0.0f), 1.0f);
	if (!ControlTurret || !HolderTurret || !ConduitTurret
		|| !TestTrue(TEXT("set-up: eight enemies"), ControlWeak && ControlSix && ControlNine && HolderWeak
			&& HolderSix && HolderNine && ConduitSturdy && ConduitWeak))
	{
		return false;
	}

	// THE VOLLEY. At 5.1 seconds each turret's clock comes round. The control's volley kills the enemy at 3 m and
	// strikes the other two once each. The holder's does the same, and the kill fires a burst at the nearest
	// left, at 6 m, before the volley reaches it: two blows there.
	Pass(World, 5.1f);
	Step(ControlTurret);
	Step(HolderTurret);
	const float ControlSixLost = 50000.0f - HealthOf(ControlSix);
	if (!TestEqual(TEXT("control: the turret holding the volley alone fired one"), ControlTurret->VolleysFired, 1)
		|| !TestTrue(TEXT("control: its volley killed the enemy at 3 m"), IsDead(ControlWeak))
		|| !TestEqual(TEXT("control: and fired no burst for it"), ControlTurret->BurstsFired, 0)
		|| !TestTrue(*FString::Printf(TEXT("control: its enemy at 6 m took the volley's one blow: %.2f"),
				ControlSixLost), ControlSixLost > 0.0f)
		|| !TestEqual(TEXT("control: and the one at 9 m the same"),
			50000.0f - HealthOf(ControlNine), ControlSixLost, 0.05f))
	{
		return false;
	}
	TestTrue(TEXT("set-up: the holder's volley killed its enemy at 3 m"), IsDead(HolderWeak));
	TestEqual(TEXT("a kill by a blow of a volley fires one burst"), HolderTurret->BurstsFired, 1);
	TestEqual(TEXT("the enemy at 6 m took the burst and then the volley's own blow: two"),
		50000.0f - HealthOf(HolderSix), ControlSixLost * 2.0f, 0.1f);
	TestEqual(TEXT("and the enemy at 9 m took the volley's one"),
		50000.0f - HealthOf(HolderNine), ControlSixLost, 0.05f);
	TestEqual(TEXT("four attacks: the volley's three and the burst"), HolderTurret->AttacksMade, 4);

	// CONDUIT. The kill is credited to the summoner, and what dealt the blow is still the turret.
	ConduitTurret->AttackTarget(ConduitWeak);
	if (!TestTrue(TEXT("set-up: under Conduit the turret's blow kills the enemy at 6 m"), IsDead(ConduitWeak))
		|| !TestTrue(TEXT("set-up: the kill is credited to the summoner, so Conduit is held"),
			WhoIsCreditedWithTheLastBlowOn(ConduitWeak) == WithConduit)
		|| !TestTrue(TEXT("set-up: and the record says the turret dealt it"),
			WhatDealtTheLastBlowOn(ConduitWeak) == ConduitTurret))
	{
		return false;
	}
	TestEqual(TEXT("a kill credited to the summoner under Conduit still fires the burst"),
		ConduitTurret->BurstsFired, 1);
	TestTrue(TEXT("and the nearest enemy within reach took it"), HealthOf(ConduitSturdy) < 50000.0f - 0.5f);
	return true;
}

CATACLYSM_TEST(FCataclysmGadgetBurstNotCountedTest,
	"Cataclysm.GadgetBurst.AKillByWhatAGadgetsHitSetOffFiresNoBurstAndNeitherDoesATrapsOrAnImpsKill")
{
	using namespace CataclysmGadgetBehaviourTest;

	// ONE HOLDER OF THE FLAG. Four stations 100 m apart: a Bolt Turret whose kill shows the flag is held; a
	// second Bolt Turret, for the kill that is not the gadget's own; a Spike Trap; and an imp.
	FWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector FirstAt(0.0f, 0.0f, 0.0f);
	const FVector SecondAt(0.0f, 100.0f * M, 0.0f);
	const FVector TrapAt(0.0f, 200.0f * M, 0.0f);
	const FVector ImpAt(0.0f, 300.0f * M, 0.0f);
	ACataclysmEnemyCharacter* Holder = Summoner(World, FVector(-50.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a holder"), Holder))
	{
		return false;
	}
	GiveGadgetRows(Holder, {{FName(ACataclysmMinion::GadgetsBurstOnKillStat), 1.0f}});
	ACataclysmMinion* First = Place(*this, Holder, TEXT("BoltTurret"), FirstAt);
	ACataclysmEnemyCharacter* FirstBystander = Enemy(World, FirstAt + FVector(3.0f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* FirstWeak = Enemy(World, FirstAt + FVector(6.0f * M, 0.0f, 0.0f), 1.0f);
	ACataclysmMinion* Second = Place(*this, Holder, TEXT("BoltTurret"), SecondAt);
	ACataclysmEnemyCharacter* SecondBystander = Enemy(World, SecondAt + FVector(3.0f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmEnemyCharacter* SecondSturdy = Enemy(World, SecondAt + FVector(6.0f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmMinion* Trap = Place(*this, Holder, TEXT("SpikeTrap"), TrapAt);
	ACataclysmEnemyCharacter* TrapWeak = Enemy(World, TrapAt + FVector(1.1f * M, 0.0f, 0.0f), 1.0f);
	ACataclysmEnemyCharacter* TrapBystander = Enemy(World, TrapAt + FVector(-1.4f * M, 0.0f, 0.0f), 50000.0f);
	ACataclysmMinion* Imp = Place(*this, Holder, TEXT("Imp"), ImpAt);
	ACataclysmEnemyCharacter* ImpWeak = Enemy(World, ImpAt + FVector(1.1f * M, 0.0f, 0.0f), 1.0f);
	ACataclysmEnemyCharacter* ImpBystander = Enemy(World, ImpAt + FVector(-1.7f * M, 0.0f, 0.0f), 50000.0f);
	UCataclysmAbilitySystemComponent* HolderSystem =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Holder));
	if (!First || !Second || !Trap || !Imp || !TestNotNull(TEXT("set-up: the holder's ability system"), HolderSystem)
		|| !TestTrue(TEXT("set-up: eight enemies"), FirstBystander && FirstWeak && SecondBystander && SecondSturdy
			&& TrapWeak && TrapBystander && ImpWeak && ImpBystander))
	{
		return false;
	}

	// THE CONTROL: the holder's turret does fire a burst for a kill of its own.
	First->AttackTarget(FirstWeak);
	if (!TestTrue(TEXT("control: the holder's first turret kills"), IsDead(FirstWeak))
		|| !TestEqual(TEXT("control: and fires a burst, so the flag is held"), First->BurstsFired, 1))
	{
		return false;
	}

	// A KILL BY WHAT THE GADGET'S HIT SET OFF. No row exists to do it, so the summoner does it by hand at the
	// moment a row would: when `deployable_hit` is raised for the second turret's blow, the summoner strikes the
	// same enemy for far more than it holds. The turret's blow reached health and the enemy is dead when the
	// turret's attack ends, but the blow that killed is the summoner's.
	bool bStruckByHand = false;
	const FDelegateHandle ByHand = HolderSystem->OnActionEvent.AddLambda(
		[&bStruckByHand, Holder, SecondSturdy](FName Raised)
		{
			if (!bStruckByHand && Raised == FName(TEXT("deployable_hit")))
			{
				bStruckByHand = true;
				UCataclysmSkillEffects::ApplyDirectDamage(Holder, SecondSturdy, 10000000.0f);
			}
		});
	const float SecondBystanderHeld = HealthOf(SecondBystander);
	Second->AttackTarget(SecondSturdy);
	HolderSystem->OnActionEvent.Remove(ByHand);
	if (!TestTrue(TEXT("set-up: the summoner struck when deployable_hit was raised"), bStruckByHand)
		|| !TestTrue(TEXT("set-up: the enemy is dead when the turret's attack ends"), IsDead(SecondSturdy))
		|| !TestTrue(TEXT("set-up: and the blow that killed it was the summoner's, not the turret's"),
			WhatDealtTheLastBlowOn(SecondSturdy) == Holder))
	{
		return false;
	}
	TestEqual(TEXT("a kill by what the gadget's hit set off fires no burst"), Second->BurstsFired, 0);
	TestEqual(TEXT("and the other enemy within its reach took nothing"),
		HealthOf(SecondBystander), SecondBystanderHeld, 0.001f);

	// A SPIKE TRAP AND AN IMP OF THE HOLDER KILL AND FIRE NOTHING.
	const float TrapBystanderHeld = HealthOf(TrapBystander);
	Trap->AttackTarget(TrapWeak);
	TestTrue(TEXT("set-up: the holder's Spike Trap kills"), IsDead(TrapWeak));
	TestEqual(TEXT("a holder's Spike Trap that kills fires no burst: a trap is not a gadget"), Trap->BurstsFired, 0);
	TestEqual(TEXT("and the other enemy within its reach took nothing"),
		HealthOf(TrapBystander), TrapBystanderHeld, 0.001f);
	const float ImpBystanderHeld = HealthOf(ImpBystander);
	Imp->AttackTarget(ImpWeak);
	TestTrue(TEXT("set-up: the holder's imp kills"), IsDead(ImpWeak));
	TestEqual(TEXT("a holder's imp that kills fires no burst"), Imp->BurstsFired, 0);
	TestEqual(TEXT("and the other enemy within its reach took nothing"),
		HealthOf(ImpBystander), ImpBystanderHeld, 0.001f);
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
