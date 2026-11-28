// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Animation/AnimSequence.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmCorruptedSentinelCharacter.h"
#include "Character/CataclysmCreatureAnimInstance.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmGatekeeperCharacter.h"
#include "Character/CataclysmHellhoundCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmSuccubusCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A charging creature runs rather than sliding in its idle pose. Issue #2809, from the owner's playtest of 2026-11-21:
 * "Enemies using any charge attack aren't sprinting like they're charging, they're just sliding into position."
 *
 * WHY IT SLID. Every charge -- the Hellhound's Hellrush, the Abyssal Warden's Stampede and the creature modifier
 * Inferno Charge, which any creature may carry -- runs through `ACataclysmEnemyCharacter::BeginCharge`, whose steps move
 * the body with `SetActorLocation`. The movement component's velocity stays nought, so `GetVelocity()` reported nought
 * for the whole run, and every creature that chooses its loop from that velocity chose its idle.
 *
 * THE ART-FREE HALF RUNS EVERYWHERE: a charging creature reports the lane's direction times the charge's speed, and a
 * standing one nought. THE CLIP HALF NEEDS THE PARAGON ART, which only the main checkout has; without it each creature
 * test reports a skipped half.
 *
 * THE RATE IS CAPPED AT EACH CREATURE'S 2.5, the coordinating session's ruling J1 of 2026-11-21: no creature has a clip
 * that matches a charge's speed under it, and past it a creature "will read as a sped-up film". The owner judges in play
 * whether a capped run still reads as sliding.
 */
namespace CataclysmChargeGaitTest
{
	constexpr float M = 100.0f;

	template <typename TCreature>
	TCreature* Spawn(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TCreature* Creature = World->SpawnActor<TCreature>(TCreature::StaticClass(), Where, FRotator::ZeroRotator, Params);
		if (Creature)
		{
			Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		}
		return Creature;
	}

	FString NoArtReason(const TCHAR* Creature)
	{
		return FString::Printf(TEXT("%s's Paragon pack is not on this machine, so it has no clips: WHICH CLIP IT PLAYS "
									"AND AT WHAT RATE ARE NOT CHECKED. That it reports the charge's speed is"),
							   Creature);
	}

	/**
	 * A creature standing still reports no movement and plays its idle (THE CONTROL); charging at `ChargeSpeed` it
	 * reports that speed along the lane and plays its jog at `ExpectedRate`; stopped, it reports no movement again.
	 */
	template <typename TCreature>
	void JogsThroughACharge(FAutomationTestBase& Test, UWorld* World, const TCHAR* Name, float ChargeSpeed,
							float ExpectedRate)
	{
		TCreature* Creature = Spawn<TCreature>(World, FVector::ZeroVector);
		if (!Test.TestNotNull(TEXT("set-up: the creature was spawned"), Creature))
		{
			return;
		}
		const bool bHasArt = Creature->JogAnimation && Creature->IdleAnimation;

		Test.TestTrue(TEXT("control: standing, it reports no movement"), Creature->GetVelocity().IsNearlyZero());
		Creature->UpdateLoopingAnimation();
		if (bHasArt)
		{
			Test.TestTrue(TEXT("control: and plays its idle"),
						  Creature->CurrentLoopingAnimation == Creature->IdleAnimation);
		}

		Creature->BeginCharge(FVector(10 * M, 0.0f, 0.0f), ChargeSpeed, /*HalfWidthCm=*/50.0f,
							  /*DamagePercent=*/0.0f);
		if (!Test.TestTrue(TEXT("set-up: the charge set off"), Creature->IsCharging()))
		{
			return;
		}
		const FVector Charging = Creature->GetVelocity();
		Test.TestEqual(TEXT("charging, it reports the charge's speed"), static_cast<float>(Charging.Size2D()),
					   ChargeSpeed, 0.5f);
		Test.TestTrue(TEXT("along the lane"), Charging.GetSafeNormal2D().Equals(FVector(1.0f, 0.0f, 0.0f), 0.001f));

		Creature->UpdateLoopingAnimation();
		if (bHasArt)
		{
			Test.TestTrue(TEXT("and plays its jog"), Creature->CurrentLoopingAnimation == Creature->JogAnimation);
			// THE LOOP'S RATE ON EITHER PLAYBACK: `GetPlayRate` answers only in single-node mode, and the Imp plays on
			// `UCataclysmCreatureAnimInstance` since issue #2976 (window additive-w1 measured 0.0 here with the art).
			Test.TestEqual(TEXT("at the rate the charge's speed asks, capped"),
						   UCataclysmCreatureAnimInstance::LoopPlayRate(Creature->GetMesh()), ExpectedRate, 0.001f);
		}
		else
		{
			CataclysmTestSkip::ReportSkippedHalf(Test, NoArtReason(Name));
		}

		Creature->CancelCharge();
		Test.TestTrue(TEXT("stopped, it reports no movement again"), Creature->GetVelocity().IsNearlyZero());
	}

	/** The rate a jog designed at `DesignedRate` for `DesignedSpeed` plays at `Speed`, capped as the creature caps it. */
	float CappedRate(float DesignedRate, float Speed, float DesignedSpeed, float Minimum, float Maximum)
	{
		return FMath::Clamp(DesignedRate * Speed / DesignedSpeed, Minimum, Maximum);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChargeVelocityTest,
	"Cataclysm.ChargeGait.AChargingCreatureReportsItsLaneTimesItsSpeedAndAStandingOneNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The art-free core, on a plain creature: what `GetVelocity` reports before, during and after a charge. Its last step
 * runs the charge to its end, which stops it with no cancel.
 */
bool FCataclysmChargeVelocityTest::RunTest(const FString&)
{
	using namespace CataclysmChargeGaitTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Creature = Spawn<ACataclysmEnemyCharacter>(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	TestTrue(TEXT("control: standing, it reports no movement"), Creature->GetVelocity().IsNearlyZero());

	constexpr float Speed = 1000.0f;
	Creature->BeginCharge(FVector(0.0f, 5 * M, 0.0f), Speed, /*HalfWidthCm=*/50.0f, /*DamagePercent=*/0.0f);
	if (!TestTrue(TEXT("set-up: the charge set off"), Creature->IsCharging()))
	{
		return false;
	}
	TestTrue(TEXT("charging, it reports the lane's direction times the charge's speed"),
			 Creature->GetVelocity().Equals(FVector(0.0f, Speed, 0.0f), 0.5f));

	Creature->AdvanceCharge(0.1f);
	TestTrue(TEXT("set-up: a tenth of a second on, it is still charging"), Creature->IsCharging());
	TestTrue(TEXT("and still reports the same"), Creature->GetVelocity().Equals(FVector(0.0f, Speed, 0.0f), 0.5f));

	Creature->AdvanceCharge(1.0f);
	TestFalse(TEXT("set-up: a second on, the five metre lane is run"), Creature->IsCharging());
	TestTrue(TEXT("and at the lane's end it reports no movement"), Creature->GetVelocity().IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmImpChargeGaitTest,
	"Cataclysm.ChargeGait.AnImpCarryingInfernoChargeJogsAtItsCeilingWhileItCharges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpChargeGaitTest::RunTest(const FString&)
{
	using namespace CataclysmChargeGaitTest;
	using Imp_t = ACataclysmImpCharacter;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const float Speed = UCataclysmEnemyModifiers::InfernoChargeSpeedCmPerSecond;
	const float Rate = CappedRate(Imp_t::JogPlayRate(), Speed, Imp_t::DesignedWalkSpeedCmPerSecond,
								  Imp_t::MinimumPlayRate, Imp_t::MaximumPlayRate);
	TestEqual(TEXT("set-up: the ceiling binds, ruling J1"), Rate, Imp_t::MaximumPlayRate, 0.0001f);
	JogsThroughACharge<Imp_t>(*this, World, TEXT("The Imp"), Speed, Rate);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHellhoundChargeGaitTest,
	"Cataclysm.ChargeGait.AHellhoundJogsAtItsCeilingThroughHellrush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHellhoundChargeGaitTest::RunTest(const FString&)
{
	using namespace CataclysmChargeGaitTest;
	using Hound_t = ACataclysmHellhoundCharacter;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const float Speed = Hound_t::HellrushSpeedCmPerSecond;
	const float Rate = CappedRate(Hound_t::JogPlayRate(), Speed, Hound_t::DesignedWalkSpeedCmPerSecond,
								  Hound_t::MinimumPlayRate, Hound_t::MaximumPlayRate);
	TestEqual(TEXT("set-up: the ceiling binds, ruling J1"), Rate, Hound_t::MaximumPlayRate, 0.0001f);
	JogsThroughACharge<Hound_t>(*this, World, TEXT("The Hellhound"), Speed, Rate);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSuccubusChargeGaitTest,
	"Cataclysm.ChargeGait.ASuccubusCarryingInfernoChargeJogsAtItsCeilingWhileItCharges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** ITS JOG, NOT ITS UNUSED Sprint_Fwd, the coordinating session's ruling J3 of 2026-11-21. */
bool FCataclysmSuccubusChargeGaitTest::RunTest(const FString&)
{
	using namespace CataclysmChargeGaitTest;
	using Succubus_t = ACataclysmSuccubusCharacter;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const float Speed = UCataclysmEnemyModifiers::InfernoChargeSpeedCmPerSecond;
	const float Rate = CappedRate(Succubus_t::JogPlayRate(), Speed, Succubus_t::DesignedWalkSpeedCmPerSecond,
								  Succubus_t::MinimumPlayRate, Succubus_t::MaximumPlayRate);
	TestEqual(TEXT("set-up: the ceiling binds, ruling J1"), Rate, Succubus_t::MaximumPlayRate, 0.0001f);
	JogsThroughACharge<Succubus_t>(*this, World, TEXT("The Succubus"), Speed, Rate);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGatekeeperChargeGaitTest,
	"Cataclysm.ChargeGait.AGatekeeperCarryingInfernoChargeJogsAtItsCeilingWhileItCharges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * ITS JOG RATE IS A PLACEHOLDER OF 1.0 AT ITS DESIGNED 3 METRES A SECOND, not a measured figure (issue #778), and it is
 * scaled only while it charges: a Gatekeeper walking keeps its placeholder, as before.
 */
bool FCataclysmGatekeeperChargeGaitTest::RunTest(const FString&)
{
	using namespace CataclysmChargeGaitTest;
	using Gatekeeper_t = ACataclysmGatekeeperCharacter;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const float Speed = UCataclysmEnemyModifiers::InfernoChargeSpeedCmPerSecond;
	const float Rate = CappedRate(Gatekeeper_t::JogPlayRate, Speed, Gatekeeper_t::DesignedWalkSpeedCmPerSecond,
								  Gatekeeper_t::MinimumPlayRate, Gatekeeper_t::MaximumPlayRate);
	TestEqual(TEXT("set-up: the ceiling binds, ruling J1"), Rate, Gatekeeper_t::MaximumPlayRate, 0.0001f);
	JogsThroughACharge<Gatekeeper_t>(*this, World, TEXT("The Gatekeeper"), Speed, Rate);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSentinelChargeGaitTest,
	"Cataclysm.ChargeGait.ASentinelCarryingInfernoChargeJogsAtItsUnmeasuredRateWhileItCharges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * ITS DESIGNED WALKING SPEED IS NOUGHT, since nobody's Sentinel walks, so there is no speed to scale its jog by: it
 * jogs at its unmeasured 1.0 (issue #2793), now chosen because it reports moving.
 */
bool FCataclysmSentinelChargeGaitTest::RunTest(const FString&)
{
	using namespace CataclysmChargeGaitTest;
	using Sentinel_t = ACataclysmCorruptedSentinelCharacter;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	JogsThroughACharge<Sentinel_t>(*this, World, TEXT("The Corrupted Sentinel"),
								   UCataclysmEnemyModifiers::InfernoChargeSpeedCmPerSecond,
								   Sentinel_t::UnmeasuredJogPlayRate);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWardenStampedeRunTest,
	"Cataclysm.ChargeGait.AWardenPlaysStampedeThroughTheRunAndHoldsItsFirstFrameThroughTheWindUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The coordinating session's ruling J2 of 2026-11-21: `Stampede` is the pack's authored charge, and its 0.70 seconds
 * equals the run, so it plays through the run at its authored rate and the wind-up holds its first frame. Until this it
 * played through the wind-up and the run showed the idle.
 */
bool FCataclysmWardenStampedeRunTest::RunTest(const FString&)
{
	using namespace CataclysmChargeGaitTest;
	using Warden_t = ACataclysmAbyssalWardenCharacter;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	Warden_t* Warden = Spawn<Warden_t>(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("set-up: a Warden"), Warden))
	{
		return false;
	}
	const float RunSeconds = Warden_t::StampedeRangeCm / Warden_t::StampedeSpeedCmPerSecond;
	TestEqual(TEXT("set-up: the run lasts the clip's 0.70 seconds"), RunSeconds, 0.70f, 0.001f);

	Warden->BeginEnemyAbilityWindUp(Warden_t::StampedeAbility, nullptr);
	const bool bHasArt = Warden->StampedeAnimation != nullptr;
	if (bHasArt)
	{
		TestTrue(TEXT("the wind-up shows Stampede"), Warden->LastPlayedAnimation == Warden->StampedeAnimation);
		TestEqual(TEXT("held still"), Warden->GetMesh()->GetPlayRate(), 0.0f, 0.0001f);
		TestEqual(TEXT("on its first frame"), Warden->GetMesh()->GetPosition(), 0.0f, 0.0001f);
	}

	Warden->UseEnemyAbility(Warden_t::StampedeAbility, nullptr, FVector(Warden_t::StampedeRangeCm, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: the charge set off"), Warden->IsCharging()))
	{
		return false;
	}
	TestEqual(TEXT("charging, it reports Stampede's speed"), static_cast<float>(Warden->GetVelocity().Size2D()),
			  Warden_t::StampedeSpeedCmPerSecond, 0.5f);
	if (bHasArt)
	{
		TestTrue(TEXT("the run plays Stampede"), Warden->LastPlayedAnimation == Warden->StampedeAnimation);
		TestEqual(TEXT("at its authored rate"), Warden->GetMesh()->GetPlayRate(), 1.0f, 0.0001f);
		TestEqual(TEXT("for as long as the run lasts"),
				  Warden->OneShotEndsAtSeconds - static_cast<float>(World->GetTimeSeconds()), RunSeconds, 0.01f);
	}
	else
	{
		CataclysmTestSkip::ReportSkippedHalf(*this, NoArtReason(TEXT("The Abyssal Warden")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChargeLosesTheStandingTallyTest,
	"Cataclysm.ChargeGait.AHeldSkillThatAsksToStandStillLosesItsTallyWhenItsCreatureCharges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE ONE READER OF A VELOCITY WHOSE BEHAVIOUR THE CHARGE NOW CHANGES IN PLAY. The Greatsword's Unbroken: "While you do
 * not move you gain 5% more damage every second ... The whole bonus is lost the instant you take a step", whose tally
 * asks `GetVelocity`. Until issue #2809 a creature holding it kept its tally through a charge, which moved it metres.
 * Only a Corrupted Stalker or a Worn Residue double with a Greatsword can hold it, and only one carrying Inferno Charge
 * can charge. THE CONTROL is the same skill on a creature standing beside it.
 */
bool FCataclysmChargeLosesTheStandingTallyTest::RunTest(const FString&)
{
	using namespace CataclysmChargeGaitTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	auto HoldUnbroken = [this](ACataclysmEnemyCharacter* Creature) -> UCataclysmSelfBuffSkill*
	{
		UCataclysmAbilitySystemComponent* System =
			Creature ? Cast<UCataclysmAbilitySystemComponent>(Creature->GetAbilitySystemComponent()) : nullptr;
		if (!System)
		{
			return nullptr;
		}
		// MANA ENOUGH FOR THE SLOT'S COST, which a creature does not otherwise carry.
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 10000.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 10000.0f);
		const FGameplayAbilitySpecHandle Handle = System->GiveAbilityInSlot(
			UCataclysmSelfBuffSkill::StaticClass(), ECataclysmAbilitySlot::Support, /*Level=*/100, Creature);
		FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Handle);
		UCataclysmSelfBuffSkill* Skill = Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Skill)
		{
			return nullptr;
		}
		Skill->SkillName = TEXT("Unbroken");
		Skill->Params = UCataclysmSkillShapes::ParseParams(
			TEXT("Duration=10; MoreDamagePer=5; ScalingSource=Second; Requires=Stationary; Immune=Displacement"));
		Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT(""));
		TestTrue(TEXT("set-up: Unbroken goes up"),
				 System->TryActivateAbility(Skill->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false));
		return Skill;
	};

	ACataclysmEnemyCharacter* Charger = Spawn<ACataclysmEnemyCharacter>(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Stander = Spawn<ACataclysmEnemyCharacter>(World, FVector(0.0f, 20 * M, 0.0f));
	UCataclysmSelfBuffSkill* Charging = HoldUnbroken(Charger);
	UCataclysmSelfBuffSkill* Standing = HoldUnbroken(Stander);
	if (!TestNotNull(TEXT("set-up: the charger holds Unbroken"), Charging)
		|| !TestNotNull(TEXT("set-up: and so does the creature standing"), Standing))
	{
		return false;
	}

	// DRIVEN DIRECTLY, as `Cataclysm.Skills.ABuffsBonusGrowsEachSecondAndIsLostOnAStep` drives it: the count runs on the
	// world's timer manager.
	for (int32 Second = 0; Second < 2; ++Second)
	{
		Charging->SecondPassed();
		Standing->SecondPassed();
	}
	TestEqual(TEXT("set-up: two seconds standing count two"), Charging->SecondsHeld, 2);

	Charger->BeginCharge(FVector(10 * M, 0.0f, 0.0f), UCataclysmEnemyModifiers::InfernoChargeSpeedCmPerSecond,
						 /*HalfWidthCm=*/50.0f, /*DamagePercent=*/0.0f);
	if (!TestTrue(TEXT("set-up: the charge set off"), Charger->IsCharging()))
	{
		return false;
	}
	Charging->SecondPassed();
	Standing->SecondPassed();
	TestEqual(TEXT("a second charging loses the whole bonus"), Charging->SecondsHeld, 0);
	TestEqual(TEXT("control: the one standing counts three"), Standing->SecondsHeld, 3);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
