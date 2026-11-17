// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageConversion.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"

/**
 * Converted damage's total is fixed when it is converted. Ruled by the coordinating session 2026-11-01, and by the
 * project owner the same day: "No: stored total is fixed".
 *
 * WHAT IS CHECKED. The Masochist's The Breaking Point converts a blow into a bleed on its own character, "over 5
 * seconds". Until this change four kinds of thing changed what that bleed dealt: a duration stat added ticks of the
 * same size (Vessel of Plagues, Symphony of Pain); Ceaseless Penance held it while its period ran on, with no bound;
 * `damage_over_time_taken` and Vessel Unbroken's `debuff_damage_suppressed` scaled or zeroed each tick; and the
 * defences the blow had already met before it was converted -- damage reduction, damage taken and the rest -- met
 * every tick again. Each test below sets one of those on a converted Masochist and asserts it takes exactly what it
 * converted.
 *
 * EACH ALSO HAS A CONTROL, so the stat it sets is shown to be live: an enemy's burn laid on a character carrying the
 * same stat is changed by it, against a character carrying none. Without the control a stat that never reached any
 * tick would pass every assertion here.
 *
 * PLAIN ACTORS, as `CataclysmDamageConversionTests.cpp` uses, with a combat set added so the stats have somewhere to
 * live. A plain actor is no player character, so no difficulty tier's resistance penalty reaches it, and it holds no
 * resistance, armour or energy shield unless a test writes one. Its stats are written as attribute bases, which
 * `UCataclysmDamageCalculation::Resolve` and `UCataclysmDebuffs::DurationOn` read when no stat line is recorded.
 */
namespace CataclysmConvertedTotalTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;
	using Conversion = UCataclysmDamageConversion;

	/** A character with health, the Masochist's resource set and a combat set. */
	struct FConvertedTotalRig
	{
		explicit FConvertedTotalRig(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(Vital::GetMaxHealthAttribute(), 1'000.0f);
			Set(Vital::GetHealthAttribute(), 1'000.0f);
		}

		~FConvertedTotalRig()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		/** The Breaking Point taken, at its base window of 3 seconds. */
		void TakeTheBreakingPoint() const
		{
			Set(UCataclysmClassResourceAttributeSet::GetDamageToBleedingOnLowHealthAttribute(), 1.0f);
			Set(UCataclysmClassResourceAttributeSet::GetDamageToBleedingWindowAttribute(), Conversion::BaseWindowSeconds);
		}

		/** Health at this share of the thousand it starts with. A fall below half opens the window. */
		void MoveHealthTo(float Share) const
		{
			Set(Vital::GetHealthAttribute(), 1'000.0f * Share);
		}

		float HealthHeld() const
		{
			return AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		}

		bool IsBleeding() const { return UCataclysmDebuffs::IsBleeding(AbilitySystem); }

		bool IsBurning() const
		{
			return AbilitySystem->HasMatchingGameplayTag(UCataclysmSkillEffects::BurnTag());
		}

		/**
		 * Ceaseless Penance, as the row records it: `debuffs_do_not_expire`, flat 1, while health is above half.
		 * The shape `CataclysmDebuffTests.cpp` gives it, since a conditioned row is never folded into an attribute.
		 * It records a stat line for this one stat; every other stat is still answered from its attribute, since
		 * `StatForSkill` falls back for a stat with no line.
		 */
		void TakeCeaselessPenance() const
		{
			FCataclysmStatModifier Flag;
			Flag.Bucket = ECataclysmStatBucket::Flat;
			Flag.Source = ECataclysmModifierSource::PassiveKeystone;
			Flag.Value = 1.0f;
			Flag.Condition = ECataclysmStatCondition::HealthAbovePercent;
			Flag.ConditionValue = 50.0f;

			FCataclysmStatInputs Inputs;
			Inputs.Base = 0.0f;
			Inputs.Modifiers.Add(Flag);

			TMap<FName, FCataclysmStatInputs> Stats;
			Stats.Add(FName(UCataclysmDebuffs::DoNotExpireStat), Inputs);
			AbilitySystem->SetStatInputs(MoveTemp(Stats));
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** An enemy's burn of this much a tick for 5 seconds, its figure as written. */
	bool LayAnEnemysBurn(const FConvertedTotalRig& Enemy, const FConvertedTotalRig& Carrier, float PerTick)
	{
		return UCataclysmSkillEffects::ApplyDamageOverTime(Enemy.Actor, Carrier.Actor, PerTick, 5.0f,
			UCataclysmSkillEffects::BurnTag(), /*bScalesWithInstigator=*/false);
	}
}

#define CATACLYSM_CONVERTED_TOTAL_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_CONVERTED_TOTAL_TEST(FCataclysmConvertedDurationStatTest,
	"Cataclysm.ConvertedDamageTotal.ADurationStatAddsNoDamage")
{
	using namespace CataclysmConvertedTotalTest;

	// `debuff_duration_taken` AT 150, VESSEL OF PLAGUES' "Debuffs on you last 50% longer". Until this change a blow of
	// 100 converted at 20 a tick lasted 7.5 seconds and ticked seven times: 140. Now it runs The Breaking Point's own
	// 5 seconds and deals its 100. CONTROL: an enemy's burn on a character with the same stat still runs at 5.5 s.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FConvertedTotalRig Masochist(World);
		const FConvertedTotalRig Control(World);
		const FConvertedTotalRig Enemy(World);
		Masochist.Set(Combat::GetDebuffDurationTakenAttribute(), 150.0f);
		Control.Set(Combat::GetDebuffDurationTakenAttribute(), 150.0f);
		Masochist.TakeTheBreakingPoint();
		Masochist.MoveHealthTo(0.4f);
		const float Before = Masochist.HealthHeld();
		if (!TestTrue(TEXT("set-up: the window is open"), Masochist.AbilitySystem->IsConvertingDamageToBleeding())
			|| !TestEqual(TEXT("set-up: the blow, 100, is converted whole"),
				Conversion::ConvertIfActive(Masochist.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false), 100.0f, 0.01f)
			|| !TestTrue(TEXT("set-up: the control's burn lands"), LayAnEnemysBurn(Enemy, Control, 10.0f)))
		{
			World->DestroyWorld(false);
			return false;
		}

		CataclysmTestWorld::RunClock(World, 5.5f);

		TestTrue(TEXT("control: a burn on a character with the same stat still runs at 5.5 s"), Control.IsBurning());
		TestFalse(TEXT("the converted damage has run out at 5.5 s, its own 5 seconds"), Masochist.IsBleeding());

		CataclysmTestWorld::RunClock(World, 2.5f);

		const float Took = Before - Masochist.HealthHeld();
		TestEqual(FString::Printf(TEXT("by 8 s the Masochist took the 100 it converted and no more: %.2f"), Took),
			Took, 100.0f, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERTED_TOTAL_TEST(FCataclysmConvertedDotTakenTest,
	"Cataclysm.ConvertedDamageTotal.MoreDamageOverTimeTakenDoesNotRaiseIt")
{
	using namespace CataclysmConvertedTotalTest;

	// `damage_over_time_taken` AT 150, VESSEL OF PLAGUES' "debuffs on you deal 50% more damage to you". Until this
	// change each converted tick of 20 dealt 30: 150 for a blow of 100. CONTROL: the same burn of 10 a tick deals half
	// as much again on a character with the stat as on one without it.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FConvertedTotalRig Masochist(World);
		const FConvertedTotalRig Control(World);
		const FConvertedTotalRig Plain(World);
		const FConvertedTotalRig Enemy(World);
		Masochist.Set(Combat::GetDamageOverTimeTakenAttribute(), 150.0f);
		Control.Set(Combat::GetDamageOverTimeTakenAttribute(), 150.0f);
		Masochist.TakeTheBreakingPoint();
		Masochist.MoveHealthTo(0.4f);
		const float Before = Masochist.HealthHeld();
		if (!TestTrue(TEXT("set-up: the window is open"), Masochist.AbilitySystem->IsConvertingDamageToBleeding())
			|| !TestEqual(TEXT("set-up: the blow, 100, is converted whole"),
				Conversion::ConvertIfActive(Masochist.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false), 100.0f, 0.01f)
			|| !TestTrue(TEXT("set-up: the control's burn lands"), LayAnEnemysBurn(Enemy, Control, 10.0f))
			|| !TestTrue(TEXT("set-up: and the plain character's"), LayAnEnemysBurn(Enemy, Plain, 10.0f)))
		{
			World->DestroyWorld(false);
			return false;
		}

		CataclysmTestWorld::RunClock(World, 5.5f);

		const float ControlTook = 1'000.0f - Control.HealthHeld();
		const float PlainTook = 1'000.0f - Plain.HealthHeld();
		TestTrue(FString::Printf(TEXT("control: the plain character's burn dealt something (%.2f)"), PlainTook),
			PlainTook > 0.0f);
		TestEqual(FString::Printf(TEXT("control: and the stat made the same burn deal half as much again (%.2f against %.2f)"),
								  ControlTook, PlainTook),
			ControlTook, PlainTook * 1.5f, 0.01f);
		const float Took = Before - Masochist.HealthHeld();
		TestEqual(FString::Printf(TEXT("the Masochist took the 100 it converted and no more: %.2f"), Took),
			Took, 100.0f, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERTED_TOTAL_TEST(FCataclysmConvertedSuppressionTest,
	"Cataclysm.ConvertedDamageTotal.SuppressedDebuffDamageDoesNotZeroIt")
{
	using namespace CataclysmConvertedTotalTest;

	// `debuff_damage_suppressed` HELD, VESSEL UNBROKEN'S "Debuffs on you deal no damage at all". Until this change
	// every converted tick dealt nothing, and The Breaking Point with Vessel Unbroken took nothing at all. Now the
	// converted 100 is taken. CONTROL: an enemy's burn deals nothing on a character holding the flag, and something on
	// one without it.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FConvertedTotalRig Masochist(World);
		const FConvertedTotalRig Control(World);
		const FConvertedTotalRig Plain(World);
		const FConvertedTotalRig Enemy(World);
		Masochist.Set(Combat::GetDebuffDamageSuppressedAttribute(), 1.0f);
		Control.Set(Combat::GetDebuffDamageSuppressedAttribute(), 1.0f);
		Masochist.TakeTheBreakingPoint();
		Masochist.MoveHealthTo(0.4f);
		const float Before = Masochist.HealthHeld();
		if (!TestTrue(TEXT("set-up: the window is open"), Masochist.AbilitySystem->IsConvertingDamageToBleeding())
			|| !TestEqual(TEXT("set-up: the blow, 100, is converted whole"),
				Conversion::ConvertIfActive(Masochist.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false), 100.0f, 0.01f)
			|| !TestTrue(TEXT("set-up: the control's burn lands"), LayAnEnemysBurn(Enemy, Control, 10.0f))
			|| !TestTrue(TEXT("set-up: and the plain character's"), LayAnEnemysBurn(Enemy, Plain, 10.0f)))
		{
			World->DestroyWorld(false);
			return false;
		}

		CataclysmTestWorld::RunClock(World, 5.5f);

		TestTrue(FString::Printf(TEXT("control: the plain character's burn dealt something (%.2f)"),
								 1'000.0f - Plain.HealthHeld()),
			Plain.HealthHeld() < 1'000.0f);
		TestEqual(FString::Printf(TEXT("control: and the same burn dealt nothing to the character holding the flag (%.2f)"),
								  1'000.0f - Control.HealthHeld()),
			Control.HealthHeld(), 1'000.0f, 0.01f);
		const float Took = Before - Masochist.HealthHeld();
		TestEqual(FString::Printf(TEXT("the Masochist took the 100 it converted: %.2f"), Took), Took, 100.0f, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERTED_TOTAL_TEST(FCataclysmConvertedDefencesTest,
	"Cataclysm.ConvertedDamageTotal.DefencesDoNotMitigateItTwice")
{
	using namespace CataclysmConvertedTotalTest;

	// DAMAGE REDUCTION AT 50 AND DAMAGE TAKEN AT 120: the blow met both before it was converted. Until this change
	// each converted tick met them again, 20 x 0.5 x 1.2 = 12, so a blow of 100 took 60. Now it takes its 100.
	// CONTROL: the same burn deals 0.6 times as much on a character with both stats as on one without.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FConvertedTotalRig Masochist(World);
		const FConvertedTotalRig Control(World);
		const FConvertedTotalRig Plain(World);
		const FConvertedTotalRig Enemy(World);
		for (const FConvertedTotalRig* Defended : {&Masochist, &Control})
		{
			Defended->Set(Combat::GetDamageReductionAttribute(), 50.0f);
			Defended->Set(Combat::GetDamageTakenAttribute(), 120.0f);
		}
		Masochist.TakeTheBreakingPoint();
		Masochist.MoveHealthTo(0.4f);
		const float Before = Masochist.HealthHeld();
		if (!TestTrue(TEXT("set-up: the window is open"), Masochist.AbilitySystem->IsConvertingDamageToBleeding())
			|| !TestEqual(TEXT("set-up: the blow, 100, is converted whole"),
				Conversion::ConvertIfActive(Masochist.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false), 100.0f, 0.01f)
			|| !TestTrue(TEXT("set-up: the control's burn lands"), LayAnEnemysBurn(Enemy, Control, 10.0f))
			|| !TestTrue(TEXT("set-up: and the plain character's"), LayAnEnemysBurn(Enemy, Plain, 10.0f)))
		{
			World->DestroyWorld(false);
			return false;
		}

		CataclysmTestWorld::RunClock(World, 5.5f);

		const float ControlTook = 1'000.0f - Control.HealthHeld();
		const float PlainTook = 1'000.0f - Plain.HealthHeld();
		TestTrue(FString::Printf(TEXT("control: the plain character's burn dealt something (%.2f)"), PlainTook),
			PlainTook > 0.0f);
		TestEqual(FString::Printf(TEXT("control: and the two stats made the same burn deal 0.6 of it (%.2f against %.2f)"),
								  ControlTook, PlainTook),
			ControlTook, PlainTook * 0.6f, 0.01f);
		const float Took = Before - Masochist.HealthHeld();
		TestEqual(FString::Printf(TEXT("the Masochist took the 100 it converted: %.2f"), Took), Took, 100.0f, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERTED_TOTAL_TEST(FCataclysmConvertedHeldTest,
	"Cataclysm.ConvertedDamageTotal.AHeldDebuffAddsNoTicks")
{
	using namespace CataclysmConvertedTotalTest;

	// CEASELESS PENANCE, "Debuffs on you no longer expire while you are above 50% health", ON A MASOCHIST HEALED BACK
	// TO 90% AFTER A BLOW OF 100 WAS CONVERTED. Until this change `HoldStep` held the converted bleed too: its end
	// stood still while it ticked 20 a second, so by 10.25 s it had dealt ten ticks, 200, and still ran. Now it runs
	// its 5 seconds and deals its 100. CONTROL: an enemy's burn on a character taking the same option at 90% is held
	// and still runs at 10.25 s, though it was laid for 5.
	//
	// `HoldStep` IS CALLED BY HAND every quarter second, the step `ACataclysmCharacterBase` calls it on, since a plain
	// actor has no regeneration step of its own.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FConvertedTotalRig Masochist(World);
		const FConvertedTotalRig Control(World);
		const FConvertedTotalRig Enemy(World);
		Masochist.TakeTheBreakingPoint();
		Masochist.MoveHealthTo(0.4f);
		if (!TestTrue(TEXT("set-up: the window is open"), Masochist.AbilitySystem->IsConvertingDamageToBleeding())
			|| !TestEqual(TEXT("set-up: the blow, 100, is converted whole"),
				Conversion::ConvertIfActive(Masochist.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false), 100.0f, 0.01f)
			|| !TestTrue(TEXT("set-up: the control's burn lands"), LayAnEnemysBurn(Enemy, Control, 1.0f)))
		{
			World->DestroyWorld(false);
			return false;
		}
		Masochist.TakeCeaselessPenance();
		Control.TakeCeaselessPenance();
		Masochist.MoveHealthTo(0.9f);
		Control.MoveHealthTo(0.9f);
		const float Before = Masochist.HealthHeld();

		for (int32 Step = 0; Step < 41; ++Step)
		{
			CataclysmTestWorld::RunClock(World, 0.25f);
			UCataclysmDebuffs::HoldStep(Masochist.Actor, 0.25f);
			UCataclysmDebuffs::HoldStep(Control.Actor, 0.25f);
		}

		TestTrue(TEXT("control: the burn, held, still runs at 10.25 s"), Control.IsBurning());
		TestFalse(TEXT("the converted damage has run out"), Masochist.IsBleeding());
		const float Took = Before - Masochist.HealthHeld();
		TestEqual(FString::Printf(TEXT("the Masochist took the 100 it converted and no more: %.2f"), Took),
			Took, 100.0f, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

#undef CATACLYSM_CONVERTED_TOTAL_TEST

#endif  // WITH_AUTOMATION_TESTS
