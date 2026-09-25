// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Misc/ScopeExit.h"

/**
 * A summoner's energy shield: how it refills, and what its minions share of it.
 * Issue #1515.
 *
 * THE STEP IS CALLED DIRECTLY, with the seconds since the summoner was last
 * hurt passed in, which is what the character's own regeneration step does
 * each time it runs. A test world does not run timers.
 */
namespace CataclysmSharedBloodTest
{
	using Vital = UCataclysmVitalAttributeSet;

	constexpr float M = 100.0f;
	constexpr float Step = UCataclysmRegeneration::StepSeconds;

	/** Long enough since the last hurt that the shield refills. */
	constexpr float LongAgo = 1000.0f;

	/** A summoner: an ability system with the vital and combat sets. */
	struct FScopedSummoner
	{
		explicit FScopedSummoner(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template and
			// a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(Vital::GetMaxHealthAttribute(), 1000.0f);
			Set(Vital::GetHealthAttribute(), 1000.0f);
		}

		~FScopedSummoner()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** One modifier on one stat, recorded as the only stat line besides the
	 *  others in `Lines`. `SetStatInputs` replaces every line, so the test
	 *  builds them all at once. */
	FCataclysmStatModifier Modifier(ECataclysmStatBucket Bucket, float Value,
									ECataclysmStatScale Scale = ECataclysmStatScale::Fixed)
	{
		FCataclysmStatModifier Out;
		Out.Bucket = Bucket;
		Out.Source = ECataclysmModifierSource::PassiveKeystone;
		Out.Value = Value;
		Out.Scale = Scale;
		Out.ScaleStep = 1.0f;
		return Out;
	}

	/** A maximum shield of 100 that 25% more per minion held raises: the shape
	 *  of Hollow Crown, issue #1973, which the attribute alone does not hold. */
	void AddScaledShieldLine(TMap<FName, FCataclysmStatInputs>& Lines)
	{
		FCataclysmStatInputs& Line = Lines.FindOrAdd(FName(TEXT("max_energy_shield")));
		Line.Base = 100.0f;
		Line.Modifiers = {Modifier(ECataclysmStatBucket::Increased, 25.0f,
								   ECataclysmStatScale::PerMinionHeld)};
	}

	ACataclysmMinion* SummonImp(FAutomationTestBase& Test, AActor* Summoner,
								const FVector& Where)
	{
		ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
			Summoner, Where, /*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("Imp"));
		if (!Test.TestNotNull(TEXT("an imp"), Imp))
		{
			return nullptr;
		}
		if (Imp->TypeName != FString(TEXT("Imp")))
		{
			Test.AddError(TEXT("DT_MinionTypes could not supply the Imp row. Run "
							   "tools/generate_datatable_assets.py"));
			return nullptr;
		}
		return Imp;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShieldRefillsToAScaledMaximumTest,
	"Cataclysm.EnergyShield.ARefillReachesAMaximumAScaledRowRaised",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A shield whose maximum a scaled row raises above the attribute refills all
 * the way to that raised maximum. Issue #1515, found while building Shared
 * Blood: until 2026-09-24 the refill stopped at the attribute, 100, while the
 * clamp and the bar allowed 125, so a bar showed a maximum refill could never
 * reach.
 */
bool FCataclysmShieldRefillsToAScaledMaximumTest::RunTest(const FString&)
{
	using namespace CataclysmSharedBloodTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedSummoner Summoner(World);
	Summoner.Set(Vital::GetMaxEnergyShieldAttribute(), 100.0f);
	Summoner.Set(Vital::GetEnergyShieldRegenAttribute(), 100.0f);
	TMap<FName, FCataclysmStatInputs> Lines;
	AddScaledShieldLine(Lines);
	Summoner.AbilitySystem->SetStatInputs(MoveTemp(Lines));

	ACataclysmMinion* Imp = SummonImp(*this, Summoner.Actor, FVector(1 * M, 0, 0));
	if (!Imp)
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	const float Raised = Summoner.AbilitySystem->MaximumEnergyShield();
	if (!TestEqual(TEXT("one minion held raises the maximum to 125, above the "
						"attribute's 100"),
				   Raised, 125.0f, 0.01f))
	{
		return false;
	}

	Summoner.Set(Vital::GetEnergyShieldAttribute(), 0.0f);
	for (int32 Each = 0; Each < 8; ++Each)
	{
		UCataclysmRegeneration::ApplyStep(Summoner.Actor, Step, LongAgo);
	}
	TestEqual(TEXT("two seconds of refill at 100 a second fill the shield to the "
				   "raised 125, not the attribute's 100"),
			  Summoner.Get(Vital::GetEnergyShieldAttribute()), Raised, 0.01f);
	return true;
}

namespace CataclysmSharedBloodTest
{
	/** Shared Blood at its designed 20%, as its row will give it. */
	void HoldSharedBlood(FScopedSummoner& Summoner)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		FCataclysmStatInputs& Line = Lines.FindOrAdd(FName(UCataclysmRegeneration::SharedBloodStat));
		Line.Base = 0.0f;
		Line.Modifiers = {Modifier(ECataclysmStatBucket::Flat, 20.0f)};
		Summoner.AbilitySystem->SetStatInputs(MoveTemp(Lines));
	}

	float ImpMaximum(const ACataclysmMinion* Imp)
	{
		return UCataclysmTargeting::AbilitySystemOf(Imp)->GetNumericAttribute(
			Vital::GetMaxEnergyShieldAttribute());
	}

	float ImpShield(const ACataclysmMinion* Imp)
	{
		return UCataclysmTargeting::AbilitySystemOf(Imp)->GetNumericAttribute(
			Vital::GetEnergyShieldAttribute());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSharedBloodFifthTest,
	"Cataclysm.SharedBlood.EachMinionHasAFifthOfItsSummonersShield",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Shared Blood, the Ritualist's `Ritualist_capstone_50` option 1. Issue #1515:
 * "Your minions each have 20% of your Maximum Energy Shield as their own, and
 * it recharges when yours does."
 *
 * THE STAT IS GIVEN BY HAND, as the row will give it: the option has no row
 * yet, so this cannot see a missing or wrong one. The rows change has to add a
 * test that wears the real row.
 */
bool FCataclysmSharedBloodFifthTest::RunTest(const FString&)
{
	using namespace CataclysmSharedBloodTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedSummoner Summoner(World);
	FScopedSummoner Plain(World);
	for (const FScopedSummoner* Each : {&Summoner, &Plain})
	{
		Each->Set(Vital::GetMaxEnergyShieldAttribute(), 500.0f);
	}
	HoldSharedBlood(Summoner);

	ACataclysmMinion* Imp = SummonImp(*this, Summoner.Actor, FVector(1 * M, 0, 0));
	ACataclysmMinion* PlainsImp = SummonImp(*this, Plain.Actor, FVector(1 * M, 20 * M, 0));
	if (!Imp || !PlainsImp)
	{
		return false;
	}
	ON_SCOPE_EXIT { for (ACataclysmMinion* Each : {Imp, PlainsImp}) { if (IsValid(Each)) { Each->Destroy(); } } };

	TestEqual(TEXT("a summoner with a shield of 500 gives its imp a maximum of 100"),
			  ImpMaximum(Imp), 100.0f, 0.01f);
	TestEqual(TEXT("and the imp is summoned with it full"), ImpShield(Imp), 100.0f, 0.01f);
	TestEqual(TEXT("a summoner without the option gives its imp no shield"),
			  ImpMaximum(PlainsImp), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSharedBloodFollowsTest,
	"Cataclysm.SharedBlood.ItFollowsTheSummonersMaximumAndRechargesWhenItsShieldDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The imp's maximum follows the summoner's live, and its shield is clamped when
 * it falls. It refills only while the summoner's shield does, at the same share
 * of its maximum per second, and the imp's own hits do not delay it. Ruled
 * 2026-09-24.
 */
bool FCataclysmSharedBloodFollowsTest::RunTest(const FString&)
{
	using namespace CataclysmSharedBloodTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedSummoner Summoner(World);
	Summoner.Set(Vital::GetMaxEnergyShieldAttribute(), 500.0f);
	Summoner.Set(Vital::GetEnergyShieldAttribute(), 500.0f);
	Summoner.Set(Vital::GetEnergyShieldRegenAttribute(), 100.0f);
	HoldSharedBlood(Summoner);

	ACataclysmMinion* Imp = SummonImp(*this, Summoner.Actor, FVector(1 * M, 0, 0));
	if (!Imp)
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	// THE MAXIMUM RISES WITH THE SUMMONER'S.
	Summoner.Set(Vital::GetMaxEnergyShieldAttribute(), 1000.0f);
	UCataclysmRegeneration::ApplyStep(Imp, Step, LongAgo);
	TestEqual(TEXT("the summoner's maximum raised to 1000 gives the imp 200 after "
				   "one step"),
			  ImpMaximum(Imp), 200.0f, 0.01f);

	// AND FALLS WITH IT, TAKING THE SHIELD DOWN.
	Summoner.Set(Vital::GetMaxEnergyShieldAttribute(), 250.0f);
	UCataclysmRegeneration::ApplyStep(Imp, Step, LongAgo);
	TestEqual(TEXT("lowered to 250, the imp's maximum is 50"), ImpMaximum(Imp), 50.0f, 0.01f);
	TestEqual(TEXT("and its shield is clamped down to it"), ImpShield(Imp), 50.0f, 0.01f);

	// NO REFILL WHILE THE SUMMONER'S SHIELD WAITS.
	Summoner.Set(Vital::GetMaxEnergyShieldAttribute(), 500.0f);
	Summoner.Set(Vital::GetEnergyShieldAttribute(), 500.0f);
	UCataclysmRegeneration::ApplyStep(Imp, Step, LongAgo);
	UCataclysmTargeting::AbilitySystemOf(Imp)->SetNumericAttributeBase(
		Vital::GetEnergyShieldAttribute(), 0.0f);
	UCataclysmRegeneration::ApplyStep(Summoner.Actor, Step, /*SecondsSinceLastDamage=*/1.0f);
	UCataclysmRegeneration::ApplyStep(Imp, Step, LongAgo);
	TestEqual(TEXT("with the summoner hurt a second ago, the imp's shield does not "
				   "refill"),
			  ImpShield(Imp), 0.0f);

	// AND THE SAME SHARE AS THE SUMMONER'S ONCE IT DOES, WHATEVER HIT THE IMP.
	UCataclysmRegeneration::ApplyStep(Summoner.Actor, Step, LongAgo);
	UCataclysmRegeneration::ApplyStep(Imp, Step, /*SecondsSinceLastDamage=*/0.0f);
	TestEqual(TEXT("once the summoner's shield refills, the imp's gains the same "
				   "share of its maximum: 100 x (100 / 500) x a quarter second, even "
				   "though the imp itself was just hit"),
			  ImpShield(Imp), 100.0f * (100.0f / 500.0f) * Step, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSharedBloodShownTest,
	"Cataclysm.SharedBlood.AMinionsShieldIsShownOverItsHead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A character with a shield gets its overhead bar once health OR the shield is
 * below its maximum; one without a shield answers as before. Only the decision
 * is tested: the automation tests run with no renderer.
 */
bool FCataclysmSharedBloodShownTest::RunTest(const FString&)
{
	TestTrue(TEXT("full health and a struck shield shows the bar"),
			 UCataclysmCombatOverlay::ShouldShowBarFor(1000.0f, 1000.0f, 50.0f, 100.0f));
	TestFalse(TEXT("full health and a full shield does not"),
			  UCataclysmCombatOverlay::ShouldShowBarFor(1000.0f, 1000.0f, 100.0f, 100.0f));
	TestTrue(TEXT("struck health shows it, shield or not"),
			 UCataclysmCombatOverlay::ShouldShowBarFor(500.0f, 1000.0f, 100.0f, 100.0f));
	TestFalse(TEXT("a corpse shows nothing"),
			  UCataclysmCombatOverlay::ShouldShowBarFor(0.0f, 1000.0f, 50.0f, 100.0f));
	TestFalse(TEXT("and with no shield, full health still shows nothing"),
			  UCataclysmCombatOverlay::ShouldShowBarFor(1000.0f, 1000.0f, 0.0f, 0.0f));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
