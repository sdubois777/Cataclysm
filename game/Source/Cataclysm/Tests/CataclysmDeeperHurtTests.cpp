// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * DEEPER HURT. The Ravager tree rework of 2026-11-24.
 *
 * `Ravager_basic_c_stem2` Deeper Hurt now reads "When an enemy you have Crippled or Weakened dies, its Cripple and
 * Weaken pass to the nearest enemy within 1 metre per point." Its row is `cripple_and_weaken_spread_on_death_metres`,
 * flat 1 a point, 6 at its 6 points. `UCataclysmDebuffs::PassCrippleAndWeakenOnDeath` reads it off whoever applied
 * each effect, from the dying creature's own death.
 *
 * WHAT PASSES: only an application whose instigator holds the node; to the applier's nearest enemy around the body
 * that does not carry that effect already; at the figure the application stated; for the effect's own designed
 * duration; credited to the applier.
 *
 * THE HARNESS IS `CataclysmContagionTests.cpp`'S, copied under its own name because the module is a unity blob: a
 * bare actor carrying collision on the Pawn channel, because `UCataclysmTargeting::FindEnemiesInSphere` runs a sphere
 * overlap and a bare actor with none is found by nothing.
 *
 * WRITTEN WITHOUT A BUILD, from the source, on 2026-11-24.
 */
namespace CataclysmDeeperHurtTest
{
	constexpr float DeeperHurtMetre = 100.0f;

	/** A combatant that can apply a Cripple, carry one and catch one. */
	struct FDeeperHurtCombatant
	{
		explicit FDeeperHurtCombatant(UWorld* World, const FVector& Where = FVector::ZeroVector)
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
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmAllResistanceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			NewVitals->SetMaxHealth(10000.0f);
			NewVitals->SetHealth(10000.0f);
		}

		~FDeeperHurtCombatant()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		/** Deeper Hurt at `Metres`, a flat row, or nothing for nought. */
		void HoldDeeperHurt(float Metres) const
		{
			TMap<FName, FCataclysmStatInputs> Inputs;
			if (Metres > 0.0f)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::PassiveKeystone;
				Row.Value = Metres;
				Inputs.FindOrAdd(FName(UCataclysmDebuffs::SpreadOnDeathMetresStat)).Modifiers = {Row};
			}
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		bool Carries(const FGameplayTag& Tag) const
		{
			return Tag.IsValid() && AbilitySystem->HasMatchingGameplayTag(Tag);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** Lay one of the two the way a melee hit lays it, at `Magnitude`. */
	bool LayDeeperHurtAilment(AActor* Applier, AActor* Target, const TCHAR* Ailment, float Magnitude)
	{
		const FCataclysmAilmentKind* Kind = UCataclysmAilments::KindNamed(Ailment);
		return Kind && UCataclysmAilments::Apply(Applier, Target, *Kind, Magnitude);
	}
}

#define CATACLYSM_DEEPER_HURT_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------------------------------------------------
// DH1. A Cripple and a Weaken pass to the nearest enemy within the radius, as strong as they were.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Six points, six metres. The body carries the holder's Cripple at 1.74 times (52.2, the Attrition figure) and its
 * Weaken at the row's 20. The creature two metres off catches both, the Cripple at 52.2, for the rows' own four and five
 * seconds; the one nine metres off catches nothing. Prints "DEEPER HURT PASS".
 */
CATACLYSM_DEEPER_HURT_TEST(FCataclysmDeeperHurtPassesTest,
	"Cataclysm.DeeperHurt.ACrippleAndAWeakenPassToTheNearestEnemyWhenTheirCarrierDies")
{
	using namespace CataclysmDeeperHurtTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	constexpr float M = DeeperHurtMetre;
	const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();
	const FGameplayTag Weaken = UCataclysmDebuffs::WeakenTag();

	// THE HOLDER STANDS WELL AWAY FROM THE BODY, so a search centred on it rather than the body would find nobody.
	const FDeeperHurtCombatant Ravager(World, FVector(0.0f, 0.0f, 40 * M));
	const FDeeperHurtCombatant Dying(World, FVector::ZeroVector);
	const FDeeperHurtCombatant Beside(World, FVector(2 * M, 0.0f, 0.0f));
	const FDeeperHurtCombatant Away(World, FVector(9 * M, 0.0f, 0.0f));
	Ravager.HoldDeeperHurt(6.0f);
	if (!TestTrue(TEXT("set-up: the body carries the holder's Cripple and Weaken"),
				  LayDeeperHurtAilment(Ravager.Actor, Dying.Actor, TEXT("Cripple"), 1.74f)
					  && LayDeeperHurtAilment(Ravager.Actor, Dying.Actor, TEXT("Weaken"), 1.0f)
					  && Dying.Carries(Cripple) && Dying.Carries(Weaken)))
	{
		return false;
	}
	UCataclysmSkillEffects::MarkDead(Dying.Actor);

	const int32 Passed = UCataclysmDebuffs::PassCrippleAndWeakenOnDeath(Dying.Actor);
	const float Strength = UCataclysmSkillEffects::StatedStrengthOn(Beside.Actor, Cripple);
	AddInfo(FString::Printf(TEXT("DEEPER HURT PASS: %d passed; beside cripple %d weaken %d strength %.2f; away %d %d"),
							Passed, Beside.Carries(Cripple), Beside.Carries(Weaken), Strength, Away.Carries(Cripple),
							Away.Carries(Weaken)));

	TestEqual(TEXT("**two effects passed**"), Passed, 2);
	TestTrue(TEXT("**the creature two metres from the body is Crippled**"), Beside.Carries(Cripple));
	TestTrue(TEXT("**and Weakened**"), Beside.Carries(Weaken));
	TestEqual(TEXT("**at the strength the body's Cripple stated, 52.2**"), Strength, 52.2f, 0.05f);
	TestFalse(TEXT("the creature nine metres away caught no Cripple"), Away.Carries(Cripple));
	TestFalse(TEXT("nor a Weaken"), Away.Carries(Weaken));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// DH2. Nothing passes without the node, nor for an effect someone else applied.
// --------------------------------------------------------------------------------------------------------------------
/** The controls for DH1. Prints "DEEPER HURT CONTROL". */
CATACLYSM_DEEPER_HURT_TEST(FCataclysmDeeperHurtControlTest,
	"Cataclysm.DeeperHurt.NothingPassesWithoutTheNodeOrForAnotherAppliersCripple")
{
	using namespace CataclysmDeeperHurtTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	constexpr float M = DeeperHurtMetre;
	const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();

	const FDeeperHurtCombatant Ravager(World, FVector(0.0f, 0.0f, 40 * M));
	const FDeeperHurtCombatant Other(World, FVector(0.0f, 0.0f, 80 * M));
	const FDeeperHurtCombatant Dying(World, FVector::ZeroVector);
	const FDeeperHurtCombatant Beside(World, FVector(2 * M, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: the body carries a Cripple the holder laid"),
				  LayDeeperHurtAilment(Ravager.Actor, Dying.Actor, TEXT("Cripple"), 1.0f)))
	{
		return false;
	}
	UCataclysmSkillEffects::MarkDead(Dying.Actor);

	Ravager.HoldDeeperHurt(0.0f);
	const int32 WithoutTheNode = UCataclysmDebuffs::PassCrippleAndWeakenOnDeath(Dying.Actor);
	const bool bCaughtWithout = Beside.Carries(Cripple);

	// THE NODE HELD BY SOMEBODY ELSE, WHOSE CRIPPLE THE BODY DOES NOT CARRY. The holder of the Cripple has no node.
	Other.HoldDeeperHurt(6.0f);
	const int32 ForAnotherApplier = UCataclysmDebuffs::PassCrippleAndWeakenOnDeath(Dying.Actor);
	const bool bCaughtForAnother = Beside.Carries(Cripple);

	// AND THE NODE HELD BY THE ONE WHO LAID IT: it passes. The control that the two above were refusals.
	Ravager.HoldDeeperHurt(6.0f);
	const int32 WithTheNode = UCataclysmDebuffs::PassCrippleAndWeakenOnDeath(Dying.Actor);
	AddInfo(FString::Printf(TEXT("DEEPER HURT CONTROL: without %d, for another %d, with %d"), WithoutTheNode,
							ForAnotherApplier, WithTheNode));

	TestEqual(TEXT("**without the node nothing passes**"), WithoutTheNode, 0);
	TestFalse(TEXT("so nothing was caught"), bCaughtWithout);
	TestEqual(TEXT("**the node on someone who did not lay the Cripple passes nothing**"), ForAnotherApplier, 0);
	TestFalse(TEXT("so nothing was caught either"), bCaughtForAnother);
	TestEqual(TEXT("CONTROL: the node on the one who laid it passes the Cripple"), WithTheNode, 1);
	TestTrue(TEXT("CONTROL: and it is caught"), Beside.Carries(Cripple));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// DH3. An enemy already carrying it is passed over for the next nearest.
// --------------------------------------------------------------------------------------------------------------------
/** Prints "DEEPER HURT SKIP". */
CATACLYSM_DEEPER_HURT_TEST(FCataclysmDeeperHurtSkipsCarriersTest,
	"Cataclysm.DeeperHurt.AnEnemyAlreadyCarryingItIsPassedOverForTheNextNearest")
{
	using namespace CataclysmDeeperHurtTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	constexpr float M = DeeperHurtMetre;
	const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();

	const FDeeperHurtCombatant Ravager(World, FVector(0.0f, 0.0f, 40 * M));
	const FDeeperHurtCombatant Dying(World, FVector::ZeroVector);
	const FDeeperHurtCombatant Nearest(World, FVector(1.5f * M, 0.0f, 0.0f));
	const FDeeperHurtCombatant Next(World, FVector(-4 * M, 0.0f, 0.0f));
	Ravager.HoldDeeperHurt(6.0f);
	if (!TestTrue(TEXT("set-up: the body and the nearest both carry the holder's Cripple, the next none"),
				  LayDeeperHurtAilment(Ravager.Actor, Dying.Actor, TEXT("Cripple"), 1.0f)
					  && LayDeeperHurtAilment(Ravager.Actor, Nearest.Actor, TEXT("Cripple"), 1.0f)
					  && !Next.Carries(Cripple)))
	{
		return false;
	}
	UCataclysmSkillEffects::MarkDead(Dying.Actor);

	const int32 Passed = UCataclysmDebuffs::PassCrippleAndWeakenOnDeath(Dying.Actor);
	AddInfo(FString::Printf(TEXT("DEEPER HURT SKIP: %d passed, next %d"), Passed, Next.Carries(Cripple)));
	TestEqual(TEXT("one effect passed"), Passed, 1);
	TestTrue(TEXT("**to the next nearest, four metres off, because the nearest already carries one**"),
			 Next.Carries(Cripple));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// DH4. A real creature's death passes it.
// --------------------------------------------------------------------------------------------------------------------
/**
 * THE WIRING. A creature on the monsters' side, Crippled by the holder, dies by the route the game takes -- its health
 * set to nought, which runs `ACataclysmEnemyCharacter::HandleDeath` -- and the monster beside it is Crippled. The
 * harness of `CataclysmRisenImpTests.cpp`. Prints "DEEPER HURT DEATH".
 */
CATACLYSM_DEEPER_HURT_TEST(FCataclysmDeeperHurtOnARealDeathTest,
	"Cataclysm.DeeperHurt.ARealCreaturesDeathPassesItsCripple")
{
	using namespace CataclysmDeeperHurtTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	constexpr float M = DeeperHurtMetre;
	const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();

	const FDeeperHurtCombatant Ravager(World, FVector(0.0f, 0.0f, 40 * M));
	Ravager.HoldDeeperHurt(6.0f);
	ACataclysmEnemyCharacter* Victim =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Neighbour =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(2 * M, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestTrue(TEXT("set-up: two monsters"), Victim && Neighbour))
	{
		return false;
	}
	for (ACataclysmEnemyCharacter* One : {Victim, Neighbour})
	{
		One->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		One->SetHealth(1000.0f);
	}
	if (!TestTrue(TEXT("set-up: the victim carries the holder's Cripple"),
				  LayDeeperHurtAilment(Ravager.Actor, Victim, TEXT("Cripple"), 1.0f)
					  && UCataclysmSkillEffects::HasTag(Victim, Cripple)))
	{
		return false;
	}

	if (UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Victim))
	{
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
	}
	const bool bVictimDead = UCataclysmSkillEffects::IsDead(Victim);
	const bool bNeighbourCrippled = UCataclysmSkillEffects::HasTag(Neighbour, Cripple);
	AddInfo(FString::Printf(TEXT("DEEPER HURT DEATH: victim dead %d, neighbour crippled %d"), bVictimDead,
							bNeighbourCrippled));

	TestTrue(TEXT("set-up: the victim died"), bVictimDead);
	TestTrue(TEXT("**its death Crippled the monster beside it**"), bNeighbourCrippled);
	TestTrue(TEXT("and the stat is on the exemption list"),
			 UCataclysmPlayerClassStats::StatsWithNoAttribute().Contains(
				 FString(UCataclysmDebuffs::SpreadOnDeathMetresStat)));
	if (IsValid(Neighbour))
	{
		Neighbour->Destroy();
	}
	return true;
}

#undef CATACLYSM_DEEPER_HURT_TEST

#endif // WITH_AUTOMATION_TESTS
