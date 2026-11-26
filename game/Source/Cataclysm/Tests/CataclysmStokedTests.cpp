// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * STOKED. The Ravager tree rework of 2026-11-24.
 *
 * `Ravager_basic_spine_009` Stoked now reads "Your Fervour starts to decay 1 second later per point." Its row is
 * `fervour_decay_grace_seconds`, flat 1 a point, which `UCataclysmFervour::DecayStep` adds to the starting node's 3
 * seconds of grace. Six points make it 9.
 *
 * THE STARTING NODE'S THREE ROWS ARE GIVEN BY HAND, as `UCataclysmPlayerClassStats::ApplyTo` leaves them: 5 a second,
 * a 4 metre radius. The holder is a creature on the players' side, because `DecayStep` takes a character and measures
 * from it; it is not one holding a player build, which `DecayStep` refuses.
 *
 * CONTACT IS STAMPED, NOT STOOD IN: `NoteEnemyInReach` is what `DecayStep` itself calls when contact holds, and the
 * world's clock is then moved by hand, as `CataclysmPassiveTreeTests.cpp`'s grace tests move it. A world that is never
 * ticked cannot be waited on.
 *
 * WRITTEN WITHOUT A BUILD, from the source, on 2026-11-24.
 */
namespace CataclysmStokedTest
{
	/** A creature on the players' side with a Fervour pool holding fifty. */
	struct FStokedHolder
	{
		explicit FStokedHolder(UWorld* World)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
			Actor->SetHealth(1000000.0f);
			Actor->SetAttackDamage(0.0f);
			AbilitySystem = Cast<UCataclysmAbilitySystemComponent>(Actor->GetAbilitySystemComponent());
			if (AbilitySystem)
			{
				AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
				AbilitySystem->SetNumericAttributeBase(
					UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
				AbilitySystem->SetNumericAttributeBase(
					UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 50.0f);
			}
		}

		~FStokedHolder()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		/** The starting node's decay and radius, and `StokedPoints` of Stoked. */
		void Hold(int32 StokedPoints) const
		{
			const auto Flat = [](float Value)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::PassiveKeystone;
				Row.Value = Value;
				return Row;
			};
			TMap<FName, FCataclysmStatInputs> Inputs;
			Inputs.FindOrAdd(FName(UCataclysmFervour::DecayPerSecondStat)).Modifiers = {Flat(5.0f)};
			Inputs.FindOrAdd(FName(UCataclysmFervour::DecayGraceMetresStat)).Modifiers = {Flat(4.0f)};
			if (StokedPoints > 0)
			{
				Inputs.FindOrAdd(FName(UCataclysmFervour::DecayGraceSecondsStat)).Modifiers =
					{Flat(1.0f * static_cast<float>(StokedPoints))};
			}
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		float Fervour() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}

		ACataclysmEnemyCharacter* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

#define CATACLYSM_STOKED_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------------------------------------------------
// ST1. Six points hold the bar for nine seconds, and it decays after them.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Contact at the start; four seconds later, past the starting node's three, the bar is untouched; ten seconds later,
 * past nine, one second takes the starting node's five. Prints "STOKED GRACE".
 */
CATACLYSM_STOKED_TEST(FCataclysmStokedHoldsTheBarTest,
	"Cataclysm.Stoked.SixPointsHoldTheBarForNineSecondsAfterContactIsLost")
{
	using namespace CataclysmStokedTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FStokedHolder Holder(World);
	if (!TestNotNull(TEXT("set-up: a holder with an ability system"), Holder.AbilitySystem))
	{
		return false;
	}
	Holder.Hold(/*StokedPoints=*/6);
	Holder.AbilitySystem->NoteEnemyInReach();

	World->TimeSeconds += 4.0f;
	UCataclysmFervour::DecayStep(Holder.Actor, 1.0f);
	const float AtFour = Holder.Fervour();
	World->TimeSeconds += 6.0f;
	UCataclysmFervour::DecayStep(Holder.Actor, 1.0f);
	const float AtTen = Holder.Fervour();
	AddInfo(FString::Printf(TEXT("STOKED GRACE: at 4 s %.2f, at 10 s %.2f"), AtFour, AtTen));

	TestEqual(TEXT("**four seconds out of contact, inside the nine, the bar is untouched**"), AtFour, 50.0f, 0.001f);
	TestEqual(TEXT("**ten seconds out, past the nine, one second takes five**"), AtTen, 45.0f, 0.001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// ST2. Without the node the same four seconds are past the grace.
// --------------------------------------------------------------------------------------------------------------------
/** The control for ST1: the starting node's three seconds, measured by the same steps. Prints "STOKED CONTROL". */
CATACLYSM_STOKED_TEST(FCataclysmStokedControlTest,
	"Cataclysm.Stoked.WithoutTheNodeTheBarDecaysAfterTheStartingNodesThreeSeconds")
{
	using namespace CataclysmStokedTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FStokedHolder Holder(World);
	if (!TestNotNull(TEXT("set-up: a holder with an ability system"), Holder.AbilitySystem))
	{
		return false;
	}
	Holder.Hold(/*StokedPoints=*/0);
	Holder.AbilitySystem->NoteEnemyInReach();

	World->TimeSeconds += 2.0f;
	UCataclysmFervour::DecayStep(Holder.Actor, 1.0f);
	const float AtTwo = Holder.Fervour();
	World->TimeSeconds += 2.0f;
	UCataclysmFervour::DecayStep(Holder.Actor, 1.0f);
	const float AtFour = Holder.Fervour();
	AddInfo(FString::Printf(TEXT("STOKED CONTROL: at 2 s %.2f, at 4 s %.2f"), AtTwo, AtFour));

	TestEqual(TEXT("inside the three seconds the bar is untouched"), AtTwo, 50.0f, 0.001f);
	TestEqual(TEXT("**four seconds out, past the three, one second takes five**"), AtFour, 45.0f, 0.001f);
	TestTrue(TEXT("and the stat is on the exemption list"),
			 UCataclysmPlayerClassStats::StatsWithNoAttribute().Contains(
				 FString(UCataclysmFervour::DecayGraceSecondsStat)));
	return true;
}

#undef CATACLYSM_STOKED_TEST

#endif // WITH_AUTOMATION_TESTS
