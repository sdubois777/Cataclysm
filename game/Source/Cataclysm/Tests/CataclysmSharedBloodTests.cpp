// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
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
 * Blood: until 2026-09-25 the refill stopped at the attribute, 100, while the
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

#endif // WITH_AUTOMATION_TESTS
