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
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Open Channel, the Ritualist's First Pact's third option since the demonic tree rework of 2026-11-24: "An enemy that
 * dies carrying your curse raises two imps rather than one." Its row is `curse_death_extra_imps`, a flat 1, read by
 * `UCataclysmRisenImps::RiseOnDeath`.
 *
 * EVERY DEATH GOES BY THE ROUTE THE GAME TAKES: the creature's health set to zero, which runs
 * `ACataclysmEnemyCharacter::HandleDeath`. The rows are granted by hand, so each case states the one thing it varies.
 */
namespace CataclysmOpenChannelTest
{
	constexpr float M = 100.0f;

	/** Summon Imp's reserve in `game/Data/WeaponSkills.csv`, which every risen imp holds. */
	constexpr float ImpReserve = 10.0f;

	/** Someone who can lay a curse, hold the rule and command imps. */
	struct FOpenChannelScopedCurser
	{
		FOpenChannelScopedCurser(UWorld* World, const FVector& Where, float Pool)
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

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), Pool);
			Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Pool);
		}

		~FOpenChannelScopedCurser()
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

		/** The starting node's rule, and Open Channel's extra imps when `ExtraImps` is above nought. */
		void Hold(float ExtraImps)
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			const auto Flat = [&Stats](const TCHAR* Stat, float Value)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::PassiveKeystone;
				Row.Value = Value;
				FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(Stat));
				Line.Base = 0.0f;
				Line.Modifiers = {Row};
			};
			Flat(UCataclysmRisenImps::RisesStat, 1.0f);
			if (ExtraImps > 0.0f)
			{
				Flat(UCataclysmRisenImps::ExtraImpsStat, ExtraImps);
			}
			AbilitySystem->SetStatInputs(MoveTemp(Stats));
		}

		TArray<ACataclysmMinion*> Imps() const
		{
			return UCataclysmCommand::MinionsOfTypeCommandedBy(Actor, TEXT("Imp"));
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A creature that can carry a curse and die of it. */
	struct FOpenChannelScopedCreature
	{
		FOpenChannelScopedCreature(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Actor->SetHealth(1000.0f);
		}

		~FOpenChannelScopedCreature()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Die()
		{
			if (UAbilitySystemComponent* AbilitySystem = UCataclysmTargeting::AbilitySystemOf(Actor))
			{
				AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
			}
		}

		ACataclysmEnemyCharacter* Actor = nullptr;
	};

	bool CurseWithShred(FAutomationTestBase& Test, AActor* Applier, AActor* Target)
	{
		return Test.TestTrue(TEXT("set-up: Shred is laid"),
			UCataclysmSkillEffects::ApplyTagForDuration(
				Applier, Target,
				FGameplayTag::RequestGameplayTag(FName(TEXT("Status.Debuff.Shred")), /*ErrorIfNotFound=*/false),
				10.0f));
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a
// `using namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOpenChannelTwoImpsTest,
	"Cataclysm.OpenChannel.ACursedDeathRaisesTwoImpsApartAndWithoutTheRowOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole option once. THE CONTROL FIRST: a creature dying under the curser's Shred, the rule held and Open Channel
 * not, raises one imp. Then Open Channel is taken and a second creature dying the same way raises two: one where it
 * fell, as every risen imp stands, and one `ExtraImpSpacingCm` from it, so the two are not made in one place. Each
 * holds Summon Imp's reserve.
 */
bool FCataclysmOpenChannelTwoImpsTest::RunTest(const FString&)
{
	using namespace CataclysmOpenChannelTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FOpenChannelScopedCurser Ritualist(World, FVector::ZeroVector, /*Pool=*/150.0f);
	Ritualist.Hold(/*ExtraImps=*/0.0f);

	FOpenChannelScopedCreature Plain(World, FVector(5 * M, 8 * M, 0));
	if (!CurseWithShred(*this, Ritualist.Actor, Plain.Actor))
	{
		return false;
	}
	Plain.Die();
	if (!TestTrue(TEXT("set-up: the first creature is recorded as dead"), UCataclysmSkillEffects::IsDead(Plain.Actor))
		|| !TestEqual(TEXT("control: without Open Channel a cursed death raises one imp"), Ritualist.Imps().Num(), 1))
	{
		return false;
	}

	Ritualist.Hold(/*ExtraImps=*/1.0f);
	const FVector Where(5 * M, -8 * M, 0);
	FOpenChannelScopedCreature Opened(World, Where);
	if (!CurseWithShred(*this, Ritualist.Actor, Opened.Actor))
	{
		return false;
	}
	const FVector DiedAt = Opened.Actor->GetActorLocation();
	Opened.Die();

	TArray<ACataclysmMinion*> Risen;
	for (ACataclysmMinion* Imp : Ritualist.Imps())
	{
		if (FVector::Dist2D(Imp->GetActorLocation(), DiedAt) < 3 * M)
		{
			Risen.Add(Imp);
		}
	}
	if (!TestEqual(TEXT("with Open Channel the next cursed death raises two imps by where it fell"), Risen.Num(), 2))
	{
		return false;
	}
	const float First = static_cast<float>(FVector::Dist2D(Risen[0]->GetActorLocation(), DiedAt));
	const float Second = static_cast<float>(FVector::Dist2D(Risen[1]->GetActorLocation(), DiedAt));
	const float Nearest = FMath::Min(First, Second);
	const float Furthest = FMath::Max(First, Second);
	TestTrue(TEXT("one stands where the creature died"), Nearest < 1.0f);
	TestEqual(TEXT("and the other a metre from it"), Furthest, UCataclysmRisenImps::ExtraImpSpacingCm, 1.0f);
	TestEqual(TEXT("each holds Summon Imp's reserve, three in all"),
			  UCataclysmCommand::ReservedFervourOf(Ritualist.Actor), 3.0f * ImpReserve, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOpenChannelRoomTest,
	"Cataclysm.OpenChannel.ThePoolRefusesTheSecondImpWhenItHasRoomForOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Each imp asks the pool for its own reserve, and the reserve is the only limit (the owner, 2026-11-20). A pool of
 * exactly one imp's reserve raises one under Open Channel, and the second is refused with nothing destroyed.
 */
bool FCataclysmOpenChannelRoomTest::RunTest(const FString&)
{
	using namespace CataclysmOpenChannelTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FOpenChannelScopedCurser Ritualist(World, FVector::ZeroVector, /*Pool=*/ImpReserve);
	Ritualist.Hold(/*ExtraImps=*/1.0f);

	FOpenChannelScopedCreature Victim(World, FVector(5 * M, 0, 0));
	if (!CurseWithShred(*this, Ritualist.Actor, Victim.Actor))
	{
		return false;
	}
	Victim.Die();

	TestEqual(TEXT("a pool with room for one imp raises one, and the second is refused"), Ritualist.Imps().Num(), 1);
	TestEqual(TEXT("and what stands holds the one reserve"), UCataclysmCommand::ReservedFervourOf(Ritualist.Actor),
			  ImpReserve, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
