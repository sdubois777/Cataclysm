// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmRisenImps.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Misc/ScopeExit.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Ritualist's risen imps. Issue #1479.
 *
 * `Ritualist_basic_spine_000`: "An enemy that dies carrying a curse you laid on
 * it rises as a lesser imp that fights for you for 20 seconds, if you hold fewer
 * imps than Summon Imp allows at once."
 *
 * THE RULE'S STAT IS GRANTED BY HAND HERE. The row that grants it from the
 * node is read on a real Ritualist in `CataclysmPassiveTreeTests.cpp`; these
 * cases are about what the rule does, so each states the one thing it varies.
 *
 * EVERY DEATH GOES BY THE ROUTE THE GAME TAKES: the creature's health set to
 * zero, which runs `ACataclysmEnemyCharacter::HandleDeath`.
 *
 * THE IMP'S FIGURES ARE SUMMON IMP'S OWN, read out of the generated weapon skill
 * table by the code under test, so nothing here writes a 3 or a 20 for the code
 * to agree with except as the expected answer.
 */
namespace CataclysmRisenImpTest
{
	constexpr float M = 100.0f;

	/** Summon Imp's figures in `game/Data/WeaponSkills.csv`. */
	constexpr int32 ImpCap = 3;
	constexpr float ImpLifetimeSeconds = 20.0f;

	/** Someone who can lay a curse, hold the rule, and command imps. */
	struct FScopedCurser
	{
		FScopedCurser(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
			check(Actor);

			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Where);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
			Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 150.0f);
		}

		~FScopedCurser()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value)
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		/** The rule, as the node's row states it: a flat one. */
		void HoldTheRule()
		{
			FCataclysmStatModifier Flag;
			Flag.Bucket = ECataclysmStatBucket::Flat;
			Flag.Source = ECataclysmModifierSource::PassiveKeystone;
			Flag.Value = 1.0f;

			TMap<FName, FCataclysmStatInputs> Stats;
			FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(UCataclysmRisenImps::RisesStat));
			Line.Base = 0.0f;
			Line.Modifiers = {Flag};
			AbilitySystem->SetStatInputs(MoveTemp(Stats));
		}

		/** The imps this one commands, oldest first. */
		TArray<ACataclysmMinion*> Imps() const
		{
			return UCataclysmCommand::MinionsOfTypeCommandedBy(Actor, TEXT("Imp"));
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A creature that can carry a curse and die of it. */
	struct FScopedCreature
	{
		FScopedCreature(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Actor->SetHealth(1000.0f);
		}

		~FScopedCreature()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		/** Dies by the route the game takes: its health reaches zero. */
		void Die()
		{
			if (UAbilitySystemComponent* AbilitySystem =
					UCataclysmTargeting::AbilitySystemOf(Actor))
			{
				AbilitySystem->SetNumericAttributeBase(
					UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
			}
		}

		ACataclysmEnemyCharacter* Actor = nullptr;
	};

	FGameplayTag CurseTag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	/** Lay a named curse for ten seconds, asserting it took. */
	bool Curse(FAutomationTestBase& Test, AActor* Applier, AActor* Target,
			   const TCHAR* Name)
	{
		return Test.TestTrue(*FString::Printf(TEXT("set-up: %s is laid"), Name),
			UCataclysmSkillEffects::ApplyTagForDuration(
				Applier, Target, CurseTag(Name), 10.0f));
	}

	/** Every minion in the world, whoever commands it. */
	int32 MinionsInTheWorld(UWorld* World)
	{
		int32 Found = 0;
		for (TActorIterator<ACataclysmMinion> It(World); It; ++It)
		{
			if (IsValid(*It) && !UCataclysmSkillEffects::IsDead(*It))
			{
				++Found;
			}
		}
		return Found;
	}

	/** Imps put into a curser's command directly, standing apart. */
	void GiveImps(FScopedCurser& Curser, int32 Count)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			ACataclysmMinion::Spawn(Curser.Actor, FVector(-10 * M, Index * 2 * M, 0),
									60.0f, /*bBurns=*/false, TEXT("Imp"));
		}
	}

	/** Summon Imp, granted with its shipped parameters. */
	UCataclysmSummonSkill* GrantSummonImp(FScopedCurser& Who)
	{
		const FGameplayAbilitySpecHandle Handle = Who.AbilitySystem->GiveAbilityInSlot(
			UCataclysmSummonSkill::StaticClass(), ECataclysmAbilitySlot::Special,
			/*Level=*/100, Who.Actor);
		FGameplayAbilitySpec* Spec = Who.AbilitySystem->FindAbilitySpecFromHandle(Handle);
		UCataclysmSummonSkill* Skill =
			Spec ? Cast<UCataclysmSummonSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->SkillName = TEXT("Summon Imp");
			Skill->Params = UCataclysmSkillShapes::ParseParams(
				TEXT("Count=1; MaxActive=3; Duration=20; Radius=3; Burn=1; Minions=Imp:1"));
		}
		return Skill;
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is
// built as a unity blob and a `using namespace` at file scope reaches the other
// files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRisenImpShredTest,
	"Cataclysm.RisenImps.ACreatureDyingUnderYourShredRisesAsYourImp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The whole rule once: Shred laid, the creature dies, an imp rises where it fell. */
bool FCataclysmRisenImpShredTest::RunTest(const FString&)
{
	using namespace CataclysmRisenImpTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCurser Ritualist(World, FVector::ZeroVector);
	Ritualist.HoldTheRule();
	TestTrue(TEXT("the rule's stat is one the engine records"),
			 UCataclysmPlayerClassStats::StatsWithNoAttribute().Contains(
				 FString(UCataclysmRisenImps::RisesStat)));

	const FVector Where(5 * M, 3 * M, 0);
	FScopedCreature Victim(World, Where);
	if (!Curse(*this, Ritualist.Actor, Victim.Actor, TEXT("Status.Debuff.Shred"))
		|| !TestEqual(TEXT("set-up: nothing is held before the death"),
					  Ritualist.Imps().Num(), 0))
	{
		return false;
	}
	const FVector DiedAt = Victim.Actor->GetActorLocation();

	Victim.Die();
	if (!TestTrue(TEXT("set-up: the creature is recorded as dead"),
				  UCataclysmSkillEffects::IsDead(Victim.Actor)))
	{
		return false;
	}

	const TArray<ACataclysmMinion*> Imps = Ritualist.Imps();
	if (!TestEqual(TEXT("one imp rose for the Ritualist"), Imps.Num(), 1))
	{
		return false;
	}
	ACataclysmMinion* Imp = Imps[0];
	TestEqual(TEXT("it is Summon Imp's kind"), Imp->TypeName, FString(TEXT("Imp")));
	TestTrue(TEXT("it stands where the creature died"),
			 FVector::Dist(Imp->GetActorLocation(), DiedAt) < 1.0f);
	TestEqual(TEXT("it lasts Summon Imp's 20 seconds"), Imp->GetLifeSpan(),
			  ImpLifetimeSeconds, 0.01f);
	TestTrue(TEXT("and sets alight what it hits, as Summon Imp's does"),
			 Imp->bBurnsWhatItHits);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRisenImpMadnessTest,
	"Cataclysm.RisenImps.MadnessIsACurseAsWellAsShred",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Any `Status.Debuff.*` is a curse, ruled 2026-09-27. Madness is the Wand's
 * other curse, and it is not Shred, so a check that named Shred alone fails here.
 */
bool FCataclysmRisenImpMadnessTest::RunTest(const FString&)
{
	using namespace CataclysmRisenImpTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCurser Ritualist(World, FVector::ZeroVector);
	Ritualist.HoldTheRule();
	FScopedCreature Victim(World, FVector(5 * M, 0, 0));
	if (!Curse(*this, Ritualist.Actor, Victim.Actor, TEXT("Status.Debuff.Madness")))
	{
		return false;
	}

	Victim.Die();
	TestEqual(TEXT("a creature dying under Madness raises an imp too"),
			  Ritualist.Imps().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRisenImpUncursedTest,
	"Cataclysm.RisenImps.AnUncursedCreatureRaisesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRisenImpUncursedTest::RunTest(const FString&)
{
	using namespace CataclysmRisenImpTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCurser Ritualist(World, FVector::ZeroVector);
	Ritualist.HoldTheRule();
	FScopedCreature Victim(World, FVector(5 * M, 0, 0));

	Victim.Die();
	TestTrue(TEXT("set-up: the creature is recorded as dead"),
			 UCataclysmSkillEffects::IsDead(Victim.Actor));
	TestEqual(TEXT("a creature carrying no curse raises nothing"),
			  MinionsInTheWorld(World), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRisenImpCreatureCurseTest,
	"Cataclysm.RisenImps.ACurseLaidByACreatureRaisesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "A curse YOU laid". Another creature lays Shred while the Ritualist, holding
 * the rule, stands by; the curse is not the Ritualist's, so nothing rises for
 * anyone.
 */
bool FCataclysmRisenImpCreatureCurseTest::RunTest(const FString&)
{
	using namespace CataclysmRisenImpTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCurser Ritualist(World, FVector::ZeroVector);
	Ritualist.HoldTheRule();
	FScopedCreature Other(World, FVector(0, 8 * M, 0));
	FScopedCreature Victim(World, FVector(5 * M, 0, 0));
	if (!Curse(*this, Other.Actor, Victim.Actor, TEXT("Status.Debuff.Shred")))
	{
		return false;
	}

	Victim.Die();
	TestEqual(TEXT("a curse a creature laid raises nothing, for the Ritualist or "
				   "for the creature"),
			  MinionsInTheWorld(World), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRisenImpMinionCurseTest,
	"Cataclysm.RisenImps.ACurseLaidByYourMinionRaisesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A minion's curse is the minion's own, ruled 2026-09-27: the minion does not
 * hold the rule, so what it curses does not rise, for it or for its summoner.
 */
bool FCataclysmRisenImpMinionCurseTest::RunTest(const FString&)
{
	using namespace CataclysmRisenImpTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCurser Ritualist(World, FVector::ZeroVector);
	Ritualist.HoldTheRule();
	GiveImps(Ritualist, 1);
	FScopedCreature Victim(World, FVector(5 * M, 0, 0));
	const TArray<ACataclysmMinion*> Before = Ritualist.Imps();
	if (!TestEqual(TEXT("set-up: the Ritualist holds one imp"), Before.Num(), 1)
		|| !Curse(*this, Before[0], Victim.Actor, TEXT("Status.Debuff.Shred")))
	{
		return false;
	}

	Victim.Die();
	TestEqual(TEXT("the imp's curse raised nothing: the world holds only that imp"),
			  MinionsInTheWorld(World), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRisenImpWithoutTheRuleTest,
	"Cataclysm.RisenImps.WithoutTheRuleYourCurseRaisesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRisenImpWithoutTheRuleTest::RunTest(const FString&)
{
	using namespace CataclysmRisenImpTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCurser Plain(World, FVector::ZeroVector);
	FScopedCreature Victim(World, FVector(5 * M, 0, 0));
	if (!Curse(*this, Plain.Actor, Victim.Actor, TEXT("Status.Debuff.Shred")))
	{
		return false;
	}

	Victim.Die();
	TestEqual(TEXT("a curse laid without the node raises nothing"),
			  MinionsInTheWorld(World), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRisenImpAtTheCapTest,
	"Cataclysm.RisenImps.NothingRisesWhileYouHoldSummonImpsCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "If you hold fewer imps than Summon Imp allows at once." At three, nothing
 * rises, and nothing already held is destroyed to make room: that is what a
 * press of Summon Imp does, not a death.
 */
bool FCataclysmRisenImpAtTheCapTest::RunTest(const FString&)
{
	using namespace CataclysmRisenImpTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCurser Ritualist(World, FVector::ZeroVector);
	Ritualist.HoldTheRule();
	GiveImps(Ritualist, ImpCap - 1);

	FScopedCreature First(World, FVector(5 * M, 0, 0));
	FScopedCreature Second(World, FVector(5 * M, 5 * M, 0));
	if (!Curse(*this, Ritualist.Actor, First.Actor, TEXT("Status.Debuff.Shred"))
		|| !Curse(*this, Ritualist.Actor, Second.Actor, TEXT("Status.Debuff.Shred")))
	{
		return false;
	}

	First.Die();
	const TArray<ACataclysmMinion*> AtTheCap = Ritualist.Imps();
	if (!TestEqual(TEXT("below the cap, an imp rises, making three"),
				   AtTheCap.Num(), ImpCap))
	{
		return false;
	}

	Second.Die();
	TestEqual(TEXT("at the cap, nothing more rises"), Ritualist.Imps().Num(), ImpCap);
	for (ACataclysmMinion* Held : AtTheCap)
	{
		TestTrue(TEXT("and every imp held before is still there"), IsValid(Held));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRisenImpSwarmTest,
	"Cataclysm.RisenImps.TheSwarmRaisesTheCapForRisenImpsToo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The cap is Summon Imp's EFFECTIVE maximum. The Swarm's +2 lets a Ritualist
 * holding three raise a fourth, and one holding five raise nothing.
 */
bool FCataclysmRisenImpSwarmTest::RunTest(const FString&)
{
	using namespace CataclysmRisenImpTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCurser Ritualist(World, FVector::ZeroVector);
	Ritualist.HoldTheRule();
	Ritualist.Set(UCataclysmCombatAttributeSet::GetMinionCapBonusAttribute(), 2.0f);
	GiveImps(Ritualist, ImpCap);

	FScopedCreature First(World, FVector(5 * M, 0, 0));
	if (!Curse(*this, Ritualist.Actor, First.Actor, TEXT("Status.Debuff.Shred")))
	{
		return false;
	}
	First.Die();
	TestEqual(TEXT("holding three under The Swarm, an imp rises"),
			  Ritualist.Imps().Num(), ImpCap + 1);

	GiveImps(Ritualist, 1);
	if (!TestEqual(TEXT("set-up: five are held"), Ritualist.Imps().Num(), ImpCap + 2))
	{
		return false;
	}
	FScopedCreature Second(World, FVector(5 * M, 5 * M, 0));
	if (!Curse(*this, Ritualist.Actor, Second.Actor, TEXT("Status.Debuff.Shred")))
	{
		return false;
	}
	Second.Die();
	TestEqual(TEXT("and holding five, nothing rises"), Ritualist.Imps().Num(), ImpCap + 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRisenImpRefreshTest,
	"Cataclysm.RisenImps.ACreatureRefreshingYourCurseLeavesItYours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A refresh keeps the first applier, ruled 2026-09-27. A creature laying the
 * same Shred again, no stronger, refreshes the running one rather than replacing
 * it, so the curse is still the Ritualist's when the creature dies.
 */
bool FCataclysmRisenImpRefreshTest::RunTest(const FString&)
{
	using namespace CataclysmRisenImpTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCurser Ritualist(World, FVector::ZeroVector);
	Ritualist.HoldTheRule();
	FScopedCreature Other(World, FVector(0, 8 * M, 0));
	FScopedCreature Victim(World, FVector(5 * M, 0, 0));
	if (!Curse(*this, Ritualist.Actor, Victim.Actor, TEXT("Status.Debuff.Shred"))
		|| !Curse(*this, Other.Actor, Victim.Actor, TEXT("Status.Debuff.Shred")))
	{
		return false;
	}

	Victim.Die();
	TestEqual(TEXT("the Ritualist's curse, refreshed by a creature, still raises "
				   "an imp for the Ritualist"),
			  Ritualist.Imps().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRisenImpLabelTest,
	"Cataclysm.RisenImps.RisenShowsOverTheImpForTwoSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Risen" on the imp's status line for 2 seconds, and never on a summoned imp.
 * The two seconds are passed by moving the rise time back, since a test world
 * does not tick.
 */
bool FCataclysmRisenImpLabelTest::RunTest(const FString&)
{
	using namespace CataclysmRisenImpTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCurser Ritualist(World, FVector::ZeroVector);
	Ritualist.HoldTheRule();
	GiveImps(Ritualist, 1);
	ACataclysmMinion* Summoned = Ritualist.Imps()[0];

	FScopedCreature Victim(World, FVector(5 * M, 0, 0));
	if (!Curse(*this, Ritualist.Actor, Victim.Actor, TEXT("Status.Debuff.Shred")))
	{
		return false;
	}
	Victim.Die();
	const TArray<ACataclysmMinion*> Imps = Ritualist.Imps();
	if (!TestEqual(TEXT("set-up: an imp rose beside the one given"), Imps.Num(), 2))
	{
		return false;
	}
	ACataclysmMinion* Risen = Imps[1];

	TestTrue(TEXT("the risen imp's status line says Risen"),
			 UCataclysmCombatOverlay::StatusLineFor(Risen).Contains(TEXT("Risen")));
	TestFalse(TEXT("an imp that did not rise does not"),
			  UCataclysmCombatOverlay::StatusLineFor(Summoned).Contains(TEXT("Risen")));

	Risen->RisenAtSeconds -= UCataclysmRisenImps::RisenLabelSeconds + 0.1f;
	TestFalse(TEXT("and two seconds after it rose, the label is gone"),
			  UCataclysmCombatOverlay::StatusLineFor(Risen).Contains(TEXT("Risen")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRisenImpEvictedTest,
	"Cataclysm.RisenImps.SummoningAtTheCapDestroysARisenImpWhenItIsTheOldest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * One cap for every imp, ruled 2026-09-27 as reading (B). An imp that rose
 * first, then two from Summon Imp, fill the cap; the next press of Summon Imp
 * destroys the oldest, which is the risen one and is in no skill's list.
 */
bool FCataclysmRisenImpEvictedTest::RunTest(const FString&)
{
	using namespace CataclysmRisenImpTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCurser Ritualist(World, FVector::ZeroVector);
	Ritualist.HoldTheRule();
	UCataclysmSummonSkill* Summon = GrantSummonImp(Ritualist);
	if (!TestNotNull(TEXT("set-up: Summon Imp is granted"), Summon))
	{
		return false;
	}

	FScopedCreature Victim(World, FVector(5 * M, 0, 0));
	if (!Curse(*this, Ritualist.Actor, Victim.Actor, TEXT("Status.Debuff.Shred")))
	{
		return false;
	}
	Victim.Die();
	const TArray<ACataclysmMinion*> Risen = Ritualist.Imps();
	if (!TestEqual(TEXT("set-up: one imp rose"), Risen.Num(), 1))
	{
		return false;
	}
	ACataclysmMinion* Oldest = Risen[0];

	Summon->SummonOne();
	Summon->SummonOne();
	if (!TestEqual(TEXT("set-up: the risen imp and two summoned fill the cap"),
				   Ritualist.Imps().Num(), ImpCap)
		|| !TestTrue(TEXT("set-up: the risen imp is still there"), IsValid(Oldest)))
	{
		return false;
	}

	Summon->SummonOne();
	TestFalse(TEXT("a fourth press destroys the oldest imp, the risen one"),
			  IsValid(Oldest));
	TestEqual(TEXT("and three imps are held, all Summon Imp's"),
			  Ritualist.Imps().Num(), ImpCap);
	TestEqual(TEXT("so the skill's own list holds three"), Summon->LivingMinionCount(),
			  ImpCap);
	return true;
}

namespace CataclysmNecrosisRiseTest
{
	using namespace CataclysmRisenImpTest;

	FGameplayTag Ailment(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	ACataclysmPlayerCharacter* SpawnPlayer(UWorld* World)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Actor =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (State && Actor)
		{
			Actor->SetPlayerState(State);
			Actor->OnRep_PlayerState();
			return Actor;
		}
		return nullptr;
	}

	/** Give `Who` the row's stat at `Seconds`, as a worn row writes it. */
	void HoldTheRow(UCataclysmAbilitySystemComponent* Who, float Seconds)
	{
		TMap<FName, FCataclysmStatInputs> Stats;
		Stats.FindOrAdd(FName(UCataclysmRisenImps::NecrosisRiseSecondsStat)).Base = Seconds;
		Who->SetStatInputs(MoveTemp(Stats));
	}

	/**
	 * `Killer` lays `AilmentName` on a creature with 10 health at `Where` and one
	 * tick of it runs, which kills. Returns the creature, or null when the
	 * ailment could not be laid or did not tick.
	 */
	ACataclysmEnemyCharacter* KilledByATickOf(FAutomationTestBase& Test, UWorld* World,
											  AActor* Killer, const TCHAR* AilmentName,
											  const FVector& Where)
	{
		ACataclysmEnemyCharacter* Victim =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("a victim"), Victim))
		{
			return nullptr;
		}
		Victim->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Victim->SetHealth(10.0f);
		UCataclysmAbilitySystemComponent* Its = Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Victim));
		const FGameplayTag Tag = Ailment(AilmentName);
		if (!Test.TestTrue(TEXT("set-up: the ailment is laid"),
				Its && Tag.IsValid() && UCataclysmSkillEffects::ApplyDamageOverTime(
					Killer, Victim, /*DamagePerTick=*/1000.0f, /*DurationSeconds=*/4.0f, Tag,
					/*bScalesWithInstigator=*/false))
			|| !Test.TestEqual(TEXT("set-up: one tick of it ran"),
				Its->ExecutePeriodicEffectsGrantingForTests(Tag), 1)
			|| !Test.TestTrue(TEXT("set-up: the tick killed the victim"),
				UCataclysmSkillEffects::IsDead(Victim)))
		{
			return nullptr;
		}
		return Victim;
	}

	/** The one living minion in the world, or null when there is not exactly one. */
	ACataclysmMinion* TheOnlyMinion(UWorld* World)
	{
		ACataclysmMinion* Found = nullptr;
		int32 Count = 0;
		for (TActorIterator<ACataclysmMinion> It(World); It; ++It)
		{
			if (IsValid(*It) && !UCataclysmSkillEffects::IsDead(*It))
			{
				Found = *It;
				++Count;
			}
		}
		return Count == 1 ? Found : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecrosisRiseTest,
	"Cataclysm.NecrosisRise.AnEnemyAPlayersNecrosisTickKillsRisesAsAnImpForTheStatedSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Enemies killed by necrosis rise as temporary minions for 5-10 seconds". Issue
 * #1833 group E part 4b. A REAL PLAYER, because the rule is called from the
 * player's own handling of a death: its necrosis tick kills a creature, and an
 * imp stands where the creature fell, for the row's seconds, outside every cap.
 */
bool FCataclysmNecrosisRiseTest::RunTest(const FString&)
{
	using namespace CataclysmNecrosisRiseTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	UCataclysmAbilitySystemComponent* Mine = Player
		? Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Player)) : nullptr;
	if (!TestNotNull(TEXT("a player"), Mine))
	{
		return false;
	}
	HoldTheRow(Mine, 10.0f);
	TestTrue(TEXT("the row's stat is one the engine records"),
		UCataclysmPlayerClassStats::StatsWithNoAttribute().Contains(
			FString(UCataclysmRisenImps::NecrosisRiseSecondsStat)));
	TestEqual(TEXT("set-up: no minion before the kill"), MinionsInTheWorld(World), 0);

	const FVector Where(5 * M, 3 * M, 0);
	const ACataclysmEnemyCharacter* Victim =
		KilledByATickOf(*this, World, Player, TEXT("Keyword.DoT.Necrosis"), Where);
	if (!Victim)
	{
		return false;
	}
	ACataclysmMinion* Imp = TheOnlyMinion(World);
	if (!TestNotNull(TEXT("one imp rose"), Imp))
	{
		return false;
	}
	TestEqual(TEXT("it is Summon Imp's kind"), Imp->TypeName, FString(TEXT("Imp")));
	TestTrue(TEXT("it is the player's"), UCataclysmCommand::CommanderOf(Imp) == Player);
	TestTrue(TEXT("it stands where the creature fell"),
		FVector::Dist2D(Imp->GetActorLocation(), Victim->GetActorLocation()) < 1.0f);
	TestEqual(TEXT("it lasts the row's 10 seconds"), Imp->GetLifeSpan(), 10.0f, 0.01f);
	TestTrue(TEXT("and holds no place under a summon cap"), Imp->bOutsideSummonCaps);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecrosisRiseOnlyNecrosisTest,
	"Cataclysm.NecrosisRise.AKillThatIsNotNecrosisRaisesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The same player holding the row: a Bleed tick's kill and a blow's kill raise nothing. */
bool FCataclysmNecrosisRiseOnlyNecrosisTest::RunTest(const FString&)
{
	using namespace CataclysmNecrosisRiseTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	UCataclysmAbilitySystemComponent* Mine = Player
		? Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Player)) : nullptr;
	if (!TestNotNull(TEXT("a player"), Mine))
	{
		return false;
	}
	HoldTheRow(Mine, 10.0f);

	if (!KilledByATickOf(*this, World, Player, TEXT("Keyword.DoT.Bleed"), FVector(5 * M, 0, 0)))
	{
		return false;
	}
	TestEqual(TEXT("a Bleed tick's kill raises nothing"), MinionsInTheWorld(World), 0);

	FScopedCreature Struck(World, FVector(8 * M, 0, 0));
	UCataclysmSkillEffects::ApplyDirectDamage(Player, Struck.Actor, 1000000.0f);
	TestTrue(TEXT("set-up: a blow killed a creature"), UCataclysmSkillEffects::IsDead(Struck.Actor));
	TestEqual(TEXT("a blow's kill raises nothing"), MinionsInTheWorld(World), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecrosisRiseNeedsTheRowTest,
	"Cataclysm.NecrosisRise.WithoutTheRowANecrosisKillRaisesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** A player without the row: its necrosis tick kills, and nothing rises. */
bool FCataclysmNecrosisRiseNeedsTheRowTest::RunTest(const FString&)
{
	using namespace CataclysmNecrosisRiseTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player"), Player)
		|| !KilledByATickOf(*this, World, Player, TEXT("Keyword.DoT.Necrosis"), FVector(5 * M, 0, 0)))
	{
		return false;
	}
	TestEqual(TEXT("nothing rose"), MinionsInTheWorld(World), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecrosisRiseOutsideCapsTest,
	"Cataclysm.NecrosisRise.ARisenImpHoldsNoPlaceUnderSummonImpsCapAndAnAllyRaisesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The rule asked directly, on a character already holding Summon Imp's cap of
 * three: an imp still rises, the cap count stays at three, and a fourth summon
 * is not what destroys it. A victim that was the character's own raises nothing.
 */
bool FCataclysmNecrosisRiseOutsideCapsTest::RunTest(const FString&)
{
	using namespace CataclysmNecrosisRiseTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCurser Holder(World, FVector::ZeroVector);
	HoldTheRow(Holder.AbilitySystem, 5.0f);
	GiveImps(Holder, ImpCap);
	if (!TestEqual(TEXT("set-up: three imps are held"), Holder.Imps().Num(), ImpCap))
	{
		return false;
	}
	FGameplayTagContainer Killing;
	Killing.AddTag(Ailment(TEXT("Keyword.DoT.Necrosis")));

	// ITS OWN IMP AS THE VICTIM: not an enemy, so nothing rises.
	//
	// OWNED BY ITS SUMMONER HERE, BECAUSE THIS TEST'S SUMMONER HAS NO TEAM. A
	// minion takes its summoner's team, and once it is possessed its owner is
	// its controller, so a team-less summoner's imp shares neither a team nor
	// an owner chain with it -- and `AttitudeBetween` answers Hostile for any
	// pair where one side has no team. In play the summoner is a player on the
	// Players team and its imp takes that team. The owner chain says here what
	// the team says there. Without this line the first run of this test raised
	// an imp from the character's own.
	ACataclysmMinion* Own = Holder.Imps()[0];
	Own->SetOwner(Holder.Actor);
	TestNull(TEXT("a kill of the character's own imp raises nothing"),
		UCataclysmRisenImps::RiseOnNecrosisKill(Holder.Actor, Own, FVector::ZeroVector, &Killing));

	FScopedCreature Victim(World, FVector(5 * M, 0, 0));
	ACataclysmMinion* Risen = UCataclysmRisenImps::RiseOnNecrosisKill(
		Holder.Actor, Victim.Actor, Victim.Actor->GetActorLocation(), &Killing);
	if (!TestNotNull(TEXT("an imp rises with the cap full"), Risen))
	{
		return false;
	}
	TestEqual(TEXT("it lasts the row's 5 seconds"), Risen->GetLifeSpan(), 5.0f, 0.01f);
	TestEqual(TEXT("the cap count is still three"), Holder.Imps().Num(), ImpCap);
	TestEqual(TEXT("and four minions stand"), MinionsInTheWorld(World), ImpCap + 1);

	FGameplayTagContainer NotNecrosis;
	NotNecrosis.AddTag(Ailment(TEXT("Keyword.DoT.Bleed")));
	TestNull(TEXT("killing tags naming Bleed raise nothing"),
		UCataclysmRisenImps::RiseOnNecrosisKill(
			Holder.Actor, Victim.Actor, Victim.Actor->GetActorLocation(), &NotNecrosis));

	// AND IT TAKES NO SUMMON IMP SLOT. A second character holds two imps and one
	// risen by necrosis: Summon Imp still summons its third, and a summon at the
	// cap destroys the oldest summoned imp, never the risen one.
	FScopedCurser Summoner(World, FVector(0, 30 * M, 0));
	HoldTheRow(Summoner.AbilitySystem, 5.0f);
	UCataclysmSummonSkill* Skill = GrantSummonImp(Summoner);
	if (!TestNotNull(TEXT("a summon skill"), Skill) || !TestNotNull(TEXT("a first imp"), Skill->SummonOne())
		|| !TestNotNull(TEXT("a second imp"), Skill->SummonOne()))
	{
		return false;
	}
	FScopedCreature Second(World, FVector(5 * M, 30 * M, 0));
	ACataclysmMinion* AlsoRisen = UCataclysmRisenImps::RiseOnNecrosisKill(
		Summoner.Actor, Second.Actor, Second.Actor->GetActorLocation(), &Killing);
	if (!TestNotNull(TEXT("an imp rises beside two summoned"), AlsoRisen))
	{
		return false;
	}
	TestNotNull(TEXT("Summon Imp still summons its third"), Skill->SummonOne());
	TestEqual(TEXT("three are under the cap"), Summoner.Imps().Num(), ImpCap);
	TestNotNull(TEXT("and a summon at the cap goes through"), Skill->SummonOne());
	TestEqual(TEXT("three are still under the cap"), Summoner.Imps().Num(), ImpCap);
	TestTrue(TEXT("and the risen imp was not what it destroyed"),
		IsValid(AlsoRisen) && !UCataclysmSkillEffects::IsDead(AlsoRisen));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
