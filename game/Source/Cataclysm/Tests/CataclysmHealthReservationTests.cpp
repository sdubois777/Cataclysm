// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "Interface/CataclysmCharacterSheetLayout.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveApply.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Health reservation. Issue #1833, ruled 2026-09-30 under the owner's
 * delegation: the maximum stays; current health may not stand above the
 * maximum less what is reserved, and every heal stops there; conditions reading
 * health against the maximum keep reading the whole of it.
 *
 * EACH TEST WRITES THE RESERVATION STRAIGHT ONTO THE STAT LINES, because what
 * is under test is what the engine does with a reservation, not which rows grant
 * one. The two rows are measured in CataclysmEnchantmentEffectTests.cpp.
 */
namespace CataclysmHealthReservationTest
{
	using FVital = UCataclysmVitalAttributeSet;

	/** A player in a world, the only kind of character that wears a reservation. */
	struct FReserving
	{
		FReserving()
		{
			World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
			if (!World)
			{
				return;
			}
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
			Player = World->SpawnActor<ACataclysmPlayerCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
			if (State && Player)
			{
				Player->SetPlayerState(State);
				Player->OnRep_PlayerState();
				ASC = Cast<UCataclysmAbilitySystemComponent>(
					UCataclysmTargeting::AbilitySystemOf(Player));
			}
		}

		~FReserving()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		/** A maximum of 1000, this much health, and this share reserved. */
		void Hold(float Health, float SharePercent)
		{
			ASC->SetNumericAttributeBase(FVital::GetMaxHealthAttribute(), 1000.0f);
			ASC->SetNumericAttributeBase(FVital::GetHealthAttribute(), Health);
			Reserve(SharePercent);
		}

		/** Replace the stat lines with a reservation of this share. */
		void Reserve(float SharePercent)
		{
			FCataclysmStatModifier Share;
			Share.Bucket = ECataclysmStatBucket::Flat;
			Share.Source = ECataclysmModifierSource::Enchantment;
			Share.Value = SharePercent;
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(
				FName(UCataclysmAbilitySystemComponent::HealthReservedPercentStat));
			Line.Base = 0.0f;
			Line.Modifiers = {Share};
			ASC->SetStatInputs(MoveTemp(Inputs));
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			return ASC->GetNumericAttribute(Attribute);
		}

		UWorld* World = nullptr;
		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* ASC = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReservationCapsHealsTest,
	"Cataclysm.HealthReservation.EveryHealStopsAtTheUnreservedMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * With 80% of a 1000 maximum reserved, a heal of 500 from 100 stops at 200, and
 * the maximum is still 1000. `UCataclysmRegeneration::TopUp` is the one route
 * regeneration, leech, a potion and an enchantment's restore all take.
 *
 * THE CONTROL FIRST: with nothing reserved the same heal reaches 600, so the
 * stop at 200 is the reservation's.
 */
bool FCataclysmReservationCapsHealsTest::RunTest(const FString&)
{
	using namespace CataclysmHealthReservationTest;

	FReserving Plain;
	FReserving Reserved;
	if (!TestNotNull(TEXT("a plain player"), Plain.ASC)
		|| !TestNotNull(TEXT("a reserving player"), Reserved.ASC))
	{
		return false;
	}
	Plain.Hold(/*Health=*/100.0f, /*SharePercent=*/0.0f);
	Reserved.Hold(/*Health=*/100.0f, /*SharePercent=*/80.0f);

	TestEqual(TEXT("80% of 1000 is reserved"), Reserved.ASC->HealthReserved(), 800.0f, 0.01f);
	TestEqual(TEXT("so 200 may be held"), Reserved.ASC->UnreservedMaximumHealth(), 200.0f, 0.01f);

	for (FReserving* Each : {&Plain, &Reserved})
	{
		UCataclysmRegeneration::TopUp(*Each->ASC, FVital::GetHealthAttribute(),
									  FVital::GetMaxHealthAttribute(), 500.0f);
	}
	TestEqual(TEXT("nothing reserved: a heal of 500 from 100 reaches 600"),
			  Plain.Get(FVital::GetHealthAttribute()), 600.0f, 0.01f);
	TestEqual(TEXT("80% reserved: the same heal stops at 200"),
			  Reserved.Get(FVital::GetHealthAttribute()), 200.0f, 0.01f);
	TestEqual(TEXT("and the maximum is still 1000"),
			  Reserved.Get(FVital::GetMaxHealthAttribute()), 1000.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReservationGrowsTest,
	"Cataclysm.HealthReservation.AGrowingReservationTakesHealthDownAtTheNextStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A player at a full 1000 who comes to have 80% reserved is at 200 after one
 * regeneration step, which is how "each minion reserves" takes its share of a
 * count no event announces. And with nothing reserved the same step leaves a
 * full character full.
 */
bool FCataclysmReservationGrowsTest::RunTest(const FString&)
{
	using namespace CataclysmHealthReservationTest;

	FReserving Player;
	if (!TestNotNull(TEXT("a player"), Player.ASC))
	{
		return false;
	}
	Player.Hold(/*Health=*/1000.0f, /*SharePercent=*/0.0f);
	UCataclysmRegeneration::ApplyStep(Player.Player, 1.0f, 60.0f);
	TestEqual(TEXT("nothing reserved: a full character stays full"),
			  Player.Get(FVital::GetHealthAttribute()), 1000.0f, 0.01f);

	Player.Reserve(80.0f);
	TestEqual(TEXT("the reservation alone moves nothing before the step"),
			  Player.Get(FVital::GetHealthAttribute()), 1000.0f, 0.01f);
	UCataclysmRegeneration::ApplyStep(Player.Player, 1.0f, 60.0f);
	TestEqual(TEXT("80% reserved: one step later health is 200"),
			  Player.Get(FVital::GetHealthAttribute()), 200.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReservationDirectWritesTest,
	"Cataclysm.HealthReservation.AWriteToFullStopsAtTheUnreservedMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The writes that put health back to a figure rather than healing it stop at
 * the unreserved maximum too: a save loaded at a full 1000 with 80% reserved
 * arrives at 200. The respawn is measured in CataclysmDeathTests.cpp, beside the
 * other respawn tests.
 */
bool FCataclysmReservationDirectWritesTest::RunTest(const FString&)
{
	using namespace CataclysmHealthReservationTest;

	FReserving Player;
	if (!TestNotNull(TEXT("a player"), Player.ASC))
	{
		return false;
	}
	Player.Hold(/*Health=*/100.0f, /*SharePercent=*/80.0f);
	FCataclysmSaveApply::VitalsInto(*Player.Player, /*Health=*/1000.0f, /*Mana=*/0.0f,
									/*EnergyShield=*/0.0f);
	TestEqual(TEXT("a save loaded at a full 1000 arrives at 200"),
			  Player.Get(FVital::GetHealthAttribute()), 200.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReservationReadsTheWholeMaximumTest,
	"Cataclysm.HealthReservation.LowHealthConditionsReadTheWholeMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * With 80% reserved and health held at 200 of 1000, a row conditioned on health
 * below 50% is on, and one conditioned on health above 50% is off, for good.
 * That is the genre's "low life" build, and it is why a reserving character
 * never trips the two "while above 50%/75% HP" drawbacks or
 * `Masochist_capstone_50#5`. Accepted 2026-09-30.
 */
bool FCataclysmReservationReadsTheWholeMaximumTest::RunTest(const FString&)
{
	using namespace CataclysmHealthReservationTest;

	FReserving Player;
	if (!TestNotNull(TEXT("a player"), Player.ASC))
	{
		return false;
	}
	Player.Hold(/*Health=*/1000.0f, /*SharePercent=*/80.0f);
	Player.ASC->HoldHealthToUnreserved();

	// TWO CONDITIONED ROWS ON A STAT OF THE TEST'S OWN, beside the reservation,
	// so nothing else the character carries can answer for either.
	const FName Probe(TEXT("health_reservation_test_reading"));
	TMap<FName, FCataclysmStatInputs> Inputs;
	FCataclysmStatModifier Share;
	Share.Bucket = ECataclysmStatBucket::Flat;
	Share.Source = ECataclysmModifierSource::Enchantment;
	Share.Value = 80.0f;
	Inputs.FindOrAdd(FName(UCataclysmAbilitySystemComponent::HealthReservedPercentStat))
		.Modifiers = {Share};
	FCataclysmStatModifier Below;
	Below.Bucket = ECataclysmStatBucket::Flat;
	Below.Source = ECataclysmModifierSource::Enchantment;
	Below.Value = 1.0f;
	Below.Condition = ECataclysmStatCondition::HealthBelowPercent;
	Below.ConditionValue = 50.0f;
	FCataclysmStatModifier Above = Below;
	Above.Value = 10.0f;
	Above.Condition = ECataclysmStatCondition::HealthAbovePercent;
	Inputs.FindOrAdd(Probe).Modifiers = {Below, Above};
	Player.ASC->SetStatInputs(MoveTemp(Inputs));

	TestEqual(TEXT("the health the reservation left is 200"),
			  Player.Get(FVital::GetHealthAttribute()), 200.0f, 0.01f);
	TestEqual(TEXT("so the below-half row is on and the above-half row off"),
			  Player.ASC->StatAppliedTo(Probe, FGameplayTagContainer(), 0.0f), 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReservationIsShownTest,
	"Cataclysm.HealthReservation.TheBarAndTheSheetShowWhatIsReserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The HUD draws the reserved band from `UCataclysmCombatOverlay::HealthReservedOf`,
 * and the character sheet's Health line says what is reserved. A system needs a
 * visible part, ruled 2026-09-30.
 */
bool FCataclysmReservationIsShownTest::RunTest(const FString&)
{
	using namespace CataclysmHealthReservationTest;

	FReserving Player;
	if (!TestNotNull(TEXT("a player"), Player.ASC))
	{
		return false;
	}
	Player.Hold(/*Health=*/200.0f, /*SharePercent=*/80.0f);

	TestEqual(TEXT("the bar's band is the 800 reserved"),
			  UCataclysmCombatOverlay::HealthReservedOf(Player.Player), 800.0f, 0.01f);
	TestEqual(TEXT("which is the last four fifths of the bar"),
			  UCataclysmCombatOverlay::BarFractionFor(800.0f, 1000.0f), 0.8f, 0.001f);

	const FCataclysmStatLine Line =
		UCataclysmCharacterSheetLayout::LineFor(TEXT("max_health"), Player.ASC, 1);
	TestTrue(FString::Printf(TEXT("the sheet's Health line says what is reserved: '%s'"),
							 *Line.Note),
			 Line.Note.Contains(TEXT("reserved"))
				 && Line.Note.Contains(UCataclysmCharacterSheetLayout::Number(800.0f)));

	Player.Reserve(0.0f);
	TestTrue(TEXT("and says nothing when nothing is reserved"),
			 UCataclysmCharacterSheetLayout::LineFor(TEXT("max_health"), Player.ASC, 1)
				 .Note.IsEmpty());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
