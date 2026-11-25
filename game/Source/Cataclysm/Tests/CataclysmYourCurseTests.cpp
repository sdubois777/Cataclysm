// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmRisenImps.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * `target_carries_your_curse`: the target carries a curse the ASKER laid. The demonic tree rework of 2026-11-24, for
 * the Ritualist's Subjugator ("+20% increased Spell Damage against enemies carrying your curse") and Long Servitude
 * ("your minions deal 20% increased damage to enemies carrying your curse").
 *
 * THE ROWS ARE GRANTED BY HAND, so each case varies one thing. The real rows reach a real Ritualist through the
 * passive tree once the design workbook carries them; these cases are about what the condition judges.
 *
 * EVERY CASE COMPARES TWO TARGETS CURSED ALIKE, ONE BY THE ASKER AND ONE BY A STRANGER, so the only difference is
 * whose curse it is. Shred changes the Demonic resistance of whatever carries it, so a target carrying no curse
 * would take a different blow for a reason that has nothing to do with this condition.
 */
namespace CataclysmYourCurseTest
{
	constexpr float M = 100.0f;

	/** The figure the hand-granted rows add, the rows' own 20. */
	constexpr float CurseIncreasePercent = 20.0f;

	/** Someone who can lay a curse, ask a stat and command a minion. */
	struct FYourCurseScopedCaster
	{
		FYourCurseScopedCaster(UWorld* World, const FVector& Where)
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
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
		}

		~FYourCurseScopedCaster()
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

		/** One row on one stat: `CurseIncreasePercent` increased, under the condition, as Subjugator's is. */
		void HoldTheRow(const TCHAR* Stat)
		{
			FCataclysmStatModifier Row;
			Row.Bucket = ECataclysmStatBucket::Increased;
			Row.Source = ECataclysmModifierSource::PassiveKeystone;
			Row.Value = CurseIncreasePercent;
			Row.Condition = ECataclysmStatCondition::TargetCarriesYourCurse;

			TMap<FName, FCataclysmStatInputs> Stats;
			FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(Stat));
			Line.Base = 0.0f;
			Line.Modifiers = {Row};
			AbilitySystem->SetStatInputs(MoveTemp(Stats));
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	FGameplayTag ShredTag()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("Status.Debuff.Shred")), /*ErrorIfNotFound=*/false);
	}

	/** Lay Shred for half a minute, asserting it took. */
	bool LayShred(FAutomationTestBase& Test, AActor* Applier, AActor* Target, const TCHAR* Who)
	{
		return Test.TestTrue(*FString::Printf(TEXT("set-up: %s lays Shred"), Who),
							 UCataclysmSkillEffects::ApplyTagForDuration(Applier, Target, ShredTag(), 30.0f));
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a
// `using namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmYourCurseHoldsOnlyForYoursTest,
	"Cataclysm.YourCurse.ARowHoldsAgainstATargetCarryingYourCurseAndNotAStrangersCurse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Subjugator's shape: `spell_damage` increased 20 under the condition, read against the target in hand through
 * `MultiplierForStatAgainst`, the same reading a blow makes. A target the asker cursed gives 1.2; one a stranger cursed
 * alike, one carrying nothing, and a lookup with no target give 1.
 */
bool FCataclysmYourCurseHoldsOnlyForYoursTest::RunTest(const FString&)
{
	using namespace CataclysmYourCurseTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FYourCurseScopedCaster Ritualist(World, FVector::ZeroVector);
	FYourCurseScopedCaster Stranger(World, FVector(-10 * M, 0, 0));
	FYourCurseScopedCaster Yours(World, FVector(5 * M, 0, 0));
	FYourCurseScopedCaster Theirs(World, FVector(5 * M, 5 * M, 0));
	FYourCurseScopedCaster Clean(World, FVector(5 * M, -5 * M, 0));
	Ritualist.HoldTheRow(TEXT("spell_damage"));

	if (!LayShred(*this, Ritualist.Actor, Yours.Actor, TEXT("the Ritualist"))
		|| !LayShred(*this, Stranger.Actor, Theirs.Actor, TEXT("a stranger")))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the query says the first target carries the Ritualist's curse"),
				  UCataclysmRisenImps::CarriesCurseLaidBy(Yours.Actor, Ritualist.Actor))
		|| !TestFalse(TEXT("set-up: and that the second carries none of the Ritualist's"),
					  UCataclysmRisenImps::CarriesCurseLaidBy(Theirs.Actor, Ritualist.Actor)))
	{
		return false;
	}

	const FGameplayTagContainer NoTags;
	TestEqual(TEXT("against a target carrying your curse the row holds: 1.2"),
			  Ritualist.AbilitySystem->MultiplierForStatAgainst(FName(TEXT("spell_damage")), NoTags, Yours.Actor),
			  1.0f + CurseIncreasePercent / 100.0f, 0.0001f);
	TestEqual(TEXT("against one carrying a stranger's curse, alike, it does not"),
			  Ritualist.AbilitySystem->MultiplierForStatAgainst(FName(TEXT("spell_damage")), NoTags, Theirs.Actor),
			  1.0f, 0.0001f);
	TestEqual(TEXT("nor against one carrying nothing"),
			  Ritualist.AbilitySystem->MultiplierForStatAgainst(FName(TEXT("spell_damage")), NoTags, Clean.Actor),
			  1.0f, 0.0001f);
	TestEqual(TEXT("nor with no target in hand, the refusal every target condition makes"),
			  Ritualist.AbilitySystem->MultiplierForStatAgainst(FName(TEXT("spell_damage")), NoTags, nullptr),
			  1.0f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmYourCurseMinionTest,
	"Cataclysm.YourCurse.AMinionsBlowReadsItsSummonersCurse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Long Servitude's shape: `minion_damage` increased 20 under the condition, on the SUMMONER. "Your" is the summoner:
 * an imp's blow is read through its summoner's component, so a target the summoner cursed takes 1.2 times the blow a
 * target a stranger cursed alike takes. The imp lays nothing itself.
 */
bool FCataclysmYourCurseMinionTest::RunTest(const FString&)
{
	using namespace CataclysmYourCurseTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FYourCurseScopedCaster Summoner(World, FVector::ZeroVector);
	FYourCurseScopedCaster Stranger(World, FVector(-10 * M, 0, 0));
	FYourCurseScopedCaster Yours(World, FVector(5 * M, 0, 0));
	FYourCurseScopedCaster Theirs(World, FVector(5 * M, 5 * M, 0));
	Summoner.HoldTheRow(TEXT("minion_damage"));

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Summoner.Actor, FVector(2 * M, 0, 0), /*Lifetime=*/60.0f,
													/*bBurns=*/false, TEXT("Imp"));
	if (!TestNotNull(TEXT("set-up: an imp of the Imp row"), Imp)
		|| !TestEqual(TEXT("set-up: it is the Imp row's"), Imp->TypeName, FString(TEXT("Imp"))))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	if (!LayShred(*this, Summoner.Actor, Yours.Actor, TEXT("the summoner"))
		|| !LayShred(*this, Stranger.Actor, Theirs.Actor, TEXT("a stranger")))
	{
		return false;
	}

	const auto BlowOn = [Imp](FYourCurseScopedCaster& Target)
	{
		const float Before = Target.Health();
		Imp->AttackTarget(Target.Actor);
		return Before - Target.Health();
	};
	const float OnYours = BlowOn(Yours);
	const float OnTheirs = BlowOn(Theirs);
	if (!TestTrue(TEXT("set-up: the imp's blow on a stranger's cursed target lands"), OnTheirs > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the imp's blow on a target its summoner cursed is 1.2 times its blow on one a stranger cursed"),
			  OnYours / OnTheirs, 1.0f + CurseIncreasePercent / 100.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmYourCurseBurnTest,
	"Cataclysm.YourCurse.ABurnYouLaidIsNotACurse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A curse is a `Status.Debuff.*` and a burn is damage over time, so a target burning by the asker's own hand carries no
 * curse of its: the row does not hold. THE DIFFERENCE FROM `target_carries_any_debuff`, which a burn satisfies, and the
 * reason the rework asked for a condition of its own. The set-up asserts the burn took.
 */
bool FCataclysmYourCurseBurnTest::RunTest(const FString&)
{
	using namespace CataclysmYourCurseTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FYourCurseScopedCaster Ritualist(World, FVector::ZeroVector);
	FYourCurseScopedCaster Burning(World, FVector(5 * M, 0, 0));
	Ritualist.HoldTheRow(TEXT("spell_damage"));

	if (!TestTrue(TEXT("set-up: the Ritualist sets the target alight"),
				  UCataclysmSkillEffects::ApplyDamageOverTime(Ritualist.Actor, Burning.Actor, /*DamagePerTick=*/1.0f,
															  /*DurationSeconds=*/30.0f,
															  UCataclysmSkillEffects::BurnTag())))
	{
		return false;
	}

	TestFalse(TEXT("a target burning by your hand carries no curse of yours"),
			  UCataclysmRisenImps::CarriesCurseLaidBy(Burning.Actor, Ritualist.Actor));
	TestEqual(TEXT("so the row does not hold against it"),
			  Ritualist.AbilitySystem->MultiplierForStatAgainst(FName(TEXT("spell_damage")), FGameplayTagContainer(),
																 Burning.Actor),
			  1.0f, 0.0001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
