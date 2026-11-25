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
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Press-Ganged and Rekindled for a Wand Ritualist, the demonic tree rework of 2026-11-24. The owner answered that day:
 * "Press-Ganged, Rekindled and the new The Swarm work for a Wand Ritualist (Dominion stays Staff-only)".
 *
 * A WAND HOLDS NO SUMMON SKILL, so its only minions are the imps that rise from cursed deaths, and every commander here
 * holds none. Press-Ganged ("When one of your minions dies, a new minion is summoned where it died at no cost, no more
 * than once every 10 seconds") replaced nothing for such a commander until now; Rekindled is the keystone's new
 * sentence ("When one of your minions reaches the end of its time, it stays for that long again, no more than once
 * every 5 seconds"), read by `ACataclysmMinion::LifeSpanExpired`.
 *
 * THE ROWS ARE GRANTED BY HAND. Every death goes by the route the game takes: the minion's health set to zero.
 */
namespace CataclysmWandMinionKeystoneTest
{
	constexpr float M = 100.0f;

	/** Summon Imp's lifetime and reserve in `game/Data/WeaponSkills.csv`. */
	constexpr float ImpLifetimeSeconds = 20.0f;
	constexpr float ImpReserve = 10.0f;

	struct FWandKeystoneScopedCommander
	{
		explicit FWandKeystoneScopedCommander(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
			check(Actor);

			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// Raw pointers on purpose: AddAttributeSetSubobject is a template and a TObjectPtr deduces the wrapper.
			UCataclysmVitalAttributeSet* Vitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* Combat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* Resource = NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(Vitals);
			AbilitySystem->AddAttributeSetSubobject(Combat);
			AbilitySystem->AddAttributeSetSubobject(Resource);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
		}

		~FWandKeystoneScopedCommander()
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

		/** One flat row, the keystone's own figure, replacing the whole stat line. Nought holds nothing. */
		void Hold(const TCHAR* Stat, float Value)
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			if (Value > 0.0f)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::PassiveKeystone;
				Row.Value = Value;
				FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(Stat));
				Line.Base = 0.0f;
				Line.Modifiers = {Row};
			}
			AbilitySystem->SetStatInputs(MoveTemp(Stats));
		}

		/** A risen imp of this commander's, as a cursed death raises one: Summon Imp's kind with no skill. */
		ACataclysmMinion* RaiseImp(float MetresAway)
		{
			return UCataclysmRisenImps::SummonImpKindFor(Actor, FVector(MetresAway * M, 0, 0), /*bRisen=*/true);
		}

		TArray<ACataclysmMinion*> Imps() const
		{
			TArray<ACataclysmMinion*> Living;
			for (ACataclysmMinion* Imp : UCataclysmCommand::MinionsOfTypeCommandedBy(Actor, TEXT("Imp")))
			{
				if (IsValid(Imp) && !UCataclysmSkillEffects::IsDead(Imp))
				{
					Living.Add(Imp);
				}
			}
			return Living;
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	void Kill(ACataclysmMinion* Minion)
	{
		if (UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Minion))
		{
			Theirs->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
		}
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a
// `using namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPressGangedWandTest,
	"Cataclysm.PressGangedWand.ARisenImpLostWithNoSummonSkillHeldIsReplacedBySummonImpsKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE CONTROL FIRST: a commander holding no summon skill and not Press-Ganged loses a risen imp, and nothing replaces
 * it. Then Press-Ganged is taken: the next lost imp is replaced where it died by one of Summon Imp's kind, holding
 * Summon Imp's reserve, and a third lost at once is not, because the keystone's ten seconds have not passed.
 */
bool FCataclysmPressGangedWandTest::RunTest(const FString&)
{
	using namespace CataclysmWandMinionKeystoneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWandKeystoneScopedCommander Ritualist(World);
	ACataclysmMinion* Unkept = Ritualist.RaiseImp(3.0f);
	if (!TestNotNull(TEXT("set-up: a risen imp"), Unkept)
		|| !TestFalse(TEXT("set-up: it is no summon skill's"), Unkept->SummonedBy.IsValid()))
	{
		return false;
	}
	Kill(Unkept);
	if (!TestTrue(TEXT("set-up: it is recorded as dead"), UCataclysmSkillEffects::IsDead(Unkept))
		|| !TestEqual(TEXT("control: without Press-Ganged nothing replaces it"), Ritualist.Imps().Num(), 0))
	{
		return false;
	}

	Ritualist.Hold(UCataclysmSummonSkill::ReplacedOnDeathStat, 10.0f);
	ACataclysmMinion* Lost = Ritualist.RaiseImp(6.0f);
	ACataclysmMinion* AlsoLost = Ritualist.RaiseImp(12.0f);
	if (!TestNotNull(TEXT("set-up: a second risen imp"), Lost)
		|| !TestNotNull(TEXT("set-up: and a third"), AlsoLost))
	{
		return false;
	}
	const FVector DiedAt = Lost->GetActorLocation();
	Kill(Lost);

	TArray<ACataclysmMinion*> Standing = Ritualist.Imps();
	ACataclysmMinion* Replacement = nullptr;
	for (ACataclysmMinion* Imp : Standing)
	{
		if (Imp != AlsoLost)
		{
			Replacement = Imp;
		}
	}
	if (!TestEqual(TEXT("with Press-Ganged the lost imp is replaced: two stand"), Standing.Num(), 2)
		|| !TestNotNull(TEXT("and the new one is found"), Replacement))
	{
		return false;
	}
	TestEqual(TEXT("it stands where the lost one died"),
			  static_cast<float>(FVector::Dist2D(Replacement->GetActorLocation(), DiedAt)), 0.0f, 1.0f);
	TestEqual(TEXT("it is Summon Imp's kind"), Replacement->TypeName, FString(TEXT("Imp")));
	TestEqual(TEXT("and the two hold Summon Imp's reserve each"), UCataclysmCommand::ReservedFervourOf(Ritualist.Actor),
			  2.0f * ImpReserve, 0.01f);

	Kill(AlsoLost);
	TestEqual(TEXT("a loss within the ten seconds is not replaced"), Ritualist.Imps().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRekindledKeepsTest,
	"Cataclysm.Rekindled.AnImpWhoseTimeRunsOutStaysForItsLifetimeAgainOnceInFiveSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE CONTROL FIRST: without Rekindled a risen imp whose time runs out is removed. Then Rekindled is taken: the next
 * imp to expire stays, for Summon Imp's whole 20 seconds again, keeping its reserve; a second expiring at once is
 * removed, because the keystone's five seconds have not passed. A risen imp is the Wand Ritualist's only minion, so
 * this is the keystone working with no summon skill held.
 */
bool FCataclysmRekindledKeepsTest::RunTest(const FString&)
{
	using namespace CataclysmWandMinionKeystoneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWandKeystoneScopedCommander Ritualist(World);
	ACataclysmMinion* Unkept = Ritualist.RaiseImp(3.0f);
	if (!TestNotNull(TEXT("set-up: a risen imp"), Unkept))
	{
		return false;
	}
	Unkept->LifeSpanExpired();
	if (!TestFalse(TEXT("control: without Rekindled an imp whose time runs out is removed"), IsValid(Unkept)))
	{
		return false;
	}

	Ritualist.Hold(ACataclysmMinion::LifetimeRenewedEverySecondsStat, 5.0f);
	ACataclysmMinion* Kept = Ritualist.RaiseImp(6.0f);
	ACataclysmMinion* NotKept = Ritualist.RaiseImp(9.0f);
	if (!TestNotNull(TEXT("set-up: a second risen imp"), Kept) || !TestNotNull(TEXT("set-up: and a third"), NotKept))
	{
		return false;
	}
	const float ReservedBefore = UCataclysmCommand::ReservedFervourOf(Ritualist.Actor);

	Kept->LifeSpanExpired();
	if (!TestTrue(TEXT("with Rekindled the imp whose time ran out stays"), IsValid(Kept)))
	{
		return false;
	}
	TestEqual(TEXT("for its whole lifetime again"), Kept->GetLifeSpan(), ImpLifetimeSeconds, 0.01f);
	TestEqual(TEXT("once"), Kept->TimesLifetimeRenewed, 1);
	TestEqual(TEXT("and still holding its reserve"), UCataclysmCommand::ReservedFervourOf(Ritualist.Actor),
			  ReservedBefore, 0.01f);

	NotKept->LifeSpanExpired();
	TestFalse(TEXT("a second imp expiring within the five seconds is removed"), IsValid(NotKept));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRekindledBodyTest,
	"Cataclysm.Rekindled.ADeadImpsBodyIsNeverKept",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A minion that died is removed `DeadBodySeconds` later through this same call, and Rekindled keeps "one of your
 * minions" whose TIME ran out, not a corpse: with the keystone held, a dead imp's body is still removed and the
 * keystone's clock is untouched, so the next living imp to expire is kept.
 */
bool FCataclysmRekindledBodyTest::RunTest(const FString&)
{
	using namespace CataclysmWandMinionKeystoneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWandKeystoneScopedCommander Ritualist(World);
	Ritualist.Hold(ACataclysmMinion::LifetimeRenewedEverySecondsStat, 5.0f);
	ACataclysmMinion* Dead = Ritualist.RaiseImp(3.0f);
	ACataclysmMinion* Living = Ritualist.RaiseImp(6.0f);
	if (!TestNotNull(TEXT("set-up: two risen imps"), Dead) || !TestNotNull(TEXT("set-up: two risen imps"), Living))
	{
		return false;
	}
	Kill(Dead);
	if (!TestTrue(TEXT("set-up: the first is recorded as dead"), UCataclysmSkillEffects::IsDead(Dead)))
	{
		return false;
	}

	if (IsValid(Dead))
	{
		Dead->LifeSpanExpired();
	}
	TestFalse(TEXT("a dead imp's body is removed, the keystone held or not"), IsValid(Dead));
	Living->LifeSpanExpired();
	TestTrue(TEXT("and the keystone's clock was not spent on it: the living imp is kept"), IsValid(Living));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
