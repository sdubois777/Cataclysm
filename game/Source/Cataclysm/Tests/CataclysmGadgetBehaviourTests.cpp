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
	 * Give a summoner the stat line the proposed rows would: each of `Flats` a flat figure requiring
	 * `Type.Deployable`, and each of `Increases` an increase requiring the same, as "Gadgets fire 20%-40% faster"
	 * is written. REPLACES THE WHOLE STAT LINE, so everything a test's summoner holds arrives in one call.
	 */
	void GiveGadgetRows(AActor* Who, const TMap<FName, float>& Flats,
						const TMap<FName, float>& Increases = TMap<FName, float>())
	{
		UCataclysmAbilitySystemComponent* Theirs =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Who));
		if (!Theirs)
		{
			return;
		}

		TMap<FName, FCataclysmStatInputs> Stats;
		const auto Add = [&Stats](const TMap<FName, float>& Rows, ECataclysmStatBucket Bucket)
		{
			for (const TPair<FName, float>& Each : Rows)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = Bucket;
				Row.Source = ECataclysmModifierSource::Enchantment;
				Row.Value = Each.Value;
				Row.RequiredTags.AddTag(DeployableTag());
				FCataclysmStatInputs& Line = Stats.FindOrAdd(Each.Key);
				Line.Base = 0.0f;
				Line.Modifiers.Add(Row);
			}
		};
		Add(Flats, ECataclysmStatBucket::Flat);
		Add(Increases, ECataclysmStatBucket::Increased);
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
	 */
	struct FLine
	{
		FLine(UWorld* World, const FVector& Origin, float MiddleMetres, float FarMetres, float BeyondMetres)
		{
			Near = Enemy(World, Origin + FVector(3.0f * M, 0.0f, 0.0f), 300.0f);
			Middle = Enemy(World, Origin + FVector(MiddleMetres * M, 0.0f, 0.0f), 900.0f);
			Far = Enemy(World, Origin + FVector(FarMetres * M, 0.0f, 0.0f), 500.0f);
			Beyond = Enemy(World, Origin + FVector(BeyondMetres * M, 0.0f, 0.0f), 5000.0f);
		}

		bool IsWhole() const { return Near && Middle && Far && Beyond; }

		ACataclysmEnemyCharacter* Near = nullptr;
		ACataclysmEnemyCharacter* Middle = nullptr;
		ACataclysmEnemyCharacter* Far = nullptr;
		ACataclysmEnemyCharacter* Beyond = nullptr;
	};
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

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
