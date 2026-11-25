// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmRetaliation.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Reprisal Wave, The First Vow's second option, as the project owner approved it
 * on 2026-11-24: "Your retaliation damage strikes every enemy within 4 metres,
 * not only the one that hit you, and 1 metre further for every 25 Fervour you
 * hold."
 *
 * NO NEW CODE WAS NEEDED FOR THE NEW CLAUSE, and this is what says so.
 * `UCataclysmRetaliation::RadiusMetresFor` has asked `retaliation_radius_metres`
 * through the stat pipeline since issue #1047, so a second row scaled by the
 * Fervour held is worked out where the radius is used. What was added is the
 * stat's place on `STATS_WITH_AN_ASKER` in `tools/generate_datatables.py` and its
 * scaled probe, without which the generator refuses the row.
 *
 * MEASURED ON `TargetsOf`, which is what one payment of retaliation strikes:
 * enemies at 5.5 and 7.5 metres, out of reach at 4 metres and reached at 6 and 8.
 */
namespace CataclysmReprisalWaveTest
{
	using Resource = UCataclysmClassResourceAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	constexpr float WaveMetre = 100.0f;

	/** A combatant with collision, so the wave's sphere search finds it. */
	struct FReprisalWaveCombatant
	{
		explicit FReprisalWaveCombatant(UWorld* World, const FVector& Where = FVector::ZeroVector)
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
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource =
				NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResource);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(Resource::GetMaxClassResourceAttribute(), 100.0f);
		}

		~FReprisalWaveCombatant()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** The option's two rows: 4 metres, and with the new clause 1 more for every 25 Fervour. */
		void HoldTheWave(bool bAndTheNewClause) const
		{
			FCataclysmStatModifier Four;
			Four.Bucket = ECataclysmStatBucket::Flat;
			Four.Source = ECataclysmModifierSource::PassiveKeystone;
			Four.Value = 4.0f;
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(UCataclysmRetaliation::RadiusMetresStat));
			Line.Modifiers = {Four};
			if (bAndTheNewClause)
			{
				FCataclysmStatModifier Further = Four;
				Further.Value = 1.0f;
				Further.Scale = ECataclysmStatScale::PerPointOfClassResourceHeld;
				Further.ScaleStep = 25.0f;
				Line.Modifiers.Add(Further);
			}
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		void SetFervour(float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Resource::GetClassResourceAttribute(), Value);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReprisalWaveReachTest,
	"Cataclysm.ReprisalWave.TheWaveReachesAMetreFurtherForEveryTwentyFiveFervourHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 4 metres on an empty pool, so the attacker alone; 6 at 50 Fervour, so the
 * enemy at 5.5 metres too; 8 at 100, so the one at 7.5 as well.
 */
bool FCataclysmReprisalWaveReachTest::RunTest(const FString&)
{
	using namespace CataclysmReprisalWaveTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FReprisalWaveCombatant Masochist(World, FVector::ZeroVector);
	FReprisalWaveCombatant Attacker(World, FVector(1 * WaveMetre, 0.0f, 0.0f));
	FReprisalWaveCombatant Middle(World, FVector(0.0f, 5.5f * WaveMetre, 0.0f));
	FReprisalWaveCombatant Outer(World, FVector(-7.5f * WaveMetre, 0.0f, 0.0f));
	Masochist.HoldTheWave(/*bAndTheNewClause=*/true);

	Masochist.SetFervour(0.0f);
	TestEqual(TEXT("an empty pool reaches 4 metres"), UCataclysmRetaliation::RadiusMetresFor(Masochist.AbilitySystem),
			  4.0f, 0.001f);
	TestEqual(TEXT("so the attacker alone is struck"),
			  UCataclysmRetaliation::TargetsOf(Masochist.AbilitySystem, Attacker.Actor).Num(), 1);

	Masochist.SetFervour(50.0f);
	TestEqual(TEXT("50 Fervour reaches 6 metres"), UCataclysmRetaliation::RadiusMetresFor(Masochist.AbilitySystem),
			  6.0f, 0.001f);
	const TArray<AActor*> AtFifty = UCataclysmRetaliation::TargetsOf(Masochist.AbilitySystem, Attacker.Actor);
	TestEqual(TEXT("so the attacker and the enemy at 5.5 metres"), AtFifty.Num(), 2);
	TestTrue(TEXT("the enemy at 5.5 metres is among them"), AtFifty.Contains(Middle.Actor));

	Masochist.SetFervour(100.0f);
	TestEqual(TEXT("100 Fervour reaches 8 metres"), UCataclysmRetaliation::RadiusMetresFor(Masochist.AbilitySystem),
			  8.0f, 0.001f);
	const TArray<AActor*> AtHundred = UCataclysmRetaliation::TargetsOf(Masochist.AbilitySystem, Attacker.Actor);
	TestEqual(TEXT("so the enemy at 7.5 metres as well"), AtHundred.Num(), 3);
	TestTrue(TEXT("the enemy at 7.5 metres is among them"), AtHundred.Contains(Outer.Actor));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReprisalWaveControlTest,
	"Cataclysm.ReprisalWave.WithoutTheNewClauseAFullPoolStillReachesFourMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** THE CONTROL: the option as it was before 2026-11-24, at 100 Fervour. */
bool FCataclysmReprisalWaveControlTest::RunTest(const FString&)
{
	using namespace CataclysmReprisalWaveTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FReprisalWaveCombatant Masochist(World, FVector::ZeroVector);
	FReprisalWaveCombatant Attacker(World, FVector(1 * WaveMetre, 0.0f, 0.0f));
	FReprisalWaveCombatant Middle(World, FVector(0.0f, 5.5f * WaveMetre, 0.0f));
	Masochist.HoldTheWave(/*bAndTheNewClause=*/false);
	Masochist.SetFervour(100.0f);
	TestEqual(TEXT("4 metres however much Fervour is held"),
			  UCataclysmRetaliation::RadiusMetresFor(Masochist.AbilitySystem), 4.0f, 0.001f);
	TestFalse(TEXT("so the enemy at 5.5 metres is not struck"),
			  UCataclysmRetaliation::TargetsOf(Masochist.AbilitySystem, Attacker.Actor).Contains(Middle.Actor));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
