// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmDamageConversion.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"

/**
 * Converted damage is damage taken, deferred, and never damage dealt. Issue #2664, ruled by the coordinating session
 * 2026-11-01.
 *
 * WHAT IS CHECKED. The Masochist's The Breaking Point lays its converted bleed on its own character, so the bleed's
 * instigator is the character that took the blow. Until this change everything that credits a blow's instigator
 * credited the Masochist with dealing it: its leech paid stored damage back, the last blow it recorded named the
 * Masochist as its own attacker (so a Masochist killed by it was credited with killing itself), and the Masochist was
 * recorded as struck by itself. Each test converts a blow and reads one of those.
 *
 * EACH HAS A CONTROL that the reading is live: a blow somebody did deal is credited as before.
 *
 * PLAIN ACTORS, as `CataclysmConvertedDamageTotalTests.cpp` uses. A plain actor has no regeneration step, so a leech
 * payment is never paid out; what is read is whether one was made. And no movement sample ever reads one, so an
 * enemy told once that it moved stays moving, which lets a bleed laid on it tick.
 */
namespace CataclysmConvertedDealtTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Conversion = UCataclysmDamageConversion;

	/** A character with health, the Masochist's resource set and a combat set. */
	struct FConvertedDealtRig
	{
		explicit FConvertedDealtRig(UWorld* World)
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

		~FConvertedDealtRig()
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

		/** The Breaking Point taken, then health put at 40%, which opens its window. */
		void ConvertingFromNow() const
		{
			Set(UCataclysmClassResourceAttributeSet::GetDamageToBleedingOnLowHealthAttribute(), 1.0f);
			Set(UCataclysmClassResourceAttributeSet::GetDamageToBleedingWindowAttribute(), Conversion::BaseWindowSeconds);
			Set(Vital::GetHealthAttribute(), 400.0f);
		}

		float HealthHeld() const
		{
			return AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

#define CATACLYSM_CONVERTED_DEALT_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_CONVERTED_DEALT_TEST(FCataclysmConvertedDamageIsNotLeechedTest,
	"Cataclysm.ConvertedDamageDealt.AMasochistLeechesNothingFromItsOwnConvertedDamage")
{
	using namespace CataclysmConvertedDealtTest;

	// A MASOCHIST WITH 10% LIFE LEECH CONVERTS A BLOW OF 100; ONE TICK OF 20 LANDS. Until this change the tick was
	// credited to its instigator, the Masochist, which leeched 2 from it: one payment. Now it makes none. CONTROL: a
	// character with the same leech lays an ordinary bleed of 20 a tick on a moving enemy, and its one tick makes one
	// payment.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FConvertedDealtRig Masochist(World);
		const FConvertedDealtRig Leecher(World);
		const FConvertedDealtRig Enemy(World);
		Masochist.Set(Vital::GetLifeLeechAttribute(), 10.0f);
		Leecher.Set(Vital::GetLifeLeechAttribute(), 10.0f);
		Enemy.AbilitySystem->NoteMovedMetres(1.0f);
		Masochist.ConvertingFromNow();
		const float Before = Masochist.HealthHeld();
		if (!TestTrue(TEXT("set-up: the window is open"), Masochist.AbilitySystem->IsConvertingDamageToBleeding())
			|| !TestEqual(TEXT("set-up: the blow, 100, is converted whole"),
				Conversion::ConvertIfActive(Masochist.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false), 100.0f, 0.01f)
			|| !TestTrue(TEXT("set-up: the leecher's bleed lands on the enemy"),
				UCataclysmSkillEffects::ApplyDamageOverTime(Leecher.Actor, Enemy.Actor, 20.0f, 5.0f,
					UCataclysmDebuffs::BleedTag(), /*bScalesWithInstigator=*/false)))
		{
			World->DestroyWorld(false);
			return false;
		}

		CataclysmTestWorld::RunClock(World, 1.5f);

		TestEqual(TEXT("set-up: one converted tick of 20 landed on the Masochist"),
			Before - Masochist.HealthHeld(), 20.0f, 0.01f);
		TestTrue(TEXT("set-up: and the leecher's bleed ticked on the enemy"), Enemy.HealthHeld() < 1'000.0f);
		TestEqual(TEXT("control: the leecher's own bleed on an enemy made one leech payment"),
			Leecher.AbilitySystem->GetLeechPayments().Num(), 1);
		TestEqual(TEXT("the Masochist's own converted tick made no leech payment"),
			Masochist.AbilitySystem->GetLeechPayments().Num(), 0);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERTED_DEALT_TEST(FCataclysmConvertedDamageIsNobodysBlowTest,
	"Cataclysm.ConvertedDamageDealt.ItsOwnConvertedDamageIsNobodysBlow")
{
	using namespace CataclysmConvertedDealtTest;

	// A MASOCHIST CONVERTS A BLOW OF 100; ONE TICK LANDS. Until this change the last blow it recorded named the
	// Masochist as its attacker and its dealer -- so one that killed it credited the Masochist with killing itself --
	// and the Masochist was recorded as struck by itself. Now the blow is on record, naming nobody, and the Masochist
	// is not struck by itself. CONTROL: an enemy's burn on another character records the enemy as attacker and as
	// having struck it.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FConvertedDealtRig Masochist(World);
		const FConvertedDealtRig Control(World);
		const FConvertedDealtRig Enemy(World);
		Masochist.ConvertingFromNow();
		const float Before = Masochist.HealthHeld();
		if (!TestTrue(TEXT("set-up: the window is open"), Masochist.AbilitySystem->IsConvertingDamageToBleeding())
			|| !TestEqual(TEXT("set-up: the blow, 100, is converted whole"),
				Conversion::ConvertIfActive(Masochist.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false), 100.0f, 0.01f)
			|| !TestTrue(TEXT("set-up: the enemy's burn lands on the control"),
				UCataclysmSkillEffects::ApplyDamageOverTime(Enemy.Actor, Control.Actor, 10.0f, 5.0f,
					UCataclysmSkillEffects::BurnTag(), /*bScalesWithInstigator=*/false)))
		{
			World->DestroyWorld(false);
			return false;
		}

		CataclysmTestWorld::RunClock(World, 1.5f);

		TestEqual(TEXT("set-up: one converted tick of 20 landed on the Masochist"),
			Before - Masochist.HealthHeld(), 20.0f, 0.01f);

		const FCataclysmLastBlow& ControlBlow = Control.AbilitySystem->GetLastBlow();
		TestTrue(TEXT("control: the burn's tick is on record"), ControlBlow.IsOnRecord());
		TestTrue(TEXT("control: and names the enemy as its attacker"), ControlBlow.Attacker.Get() == Enemy.Actor);
		TestTrue(TEXT("control: and the enemy is recorded as having struck the control"),
			Control.AbilitySystem->WasStruckBy(Enemy.AbilitySystem));

		const FCataclysmLastBlow& OwnBlow = Masochist.AbilitySystem->GetLastBlow();
		TestTrue(TEXT("set-up: the converted tick is on record as the Masochist's last blow"), OwnBlow.IsOnRecord());
		TestTrue(TEXT("the converted tick names no attacker, so a death by it is credited to nobody"),
			OwnBlow.Attacker.Get() == nullptr);
		TestTrue(TEXT("and no dealer"), OwnBlow.DealtBy.Get() == nullptr);
		TestFalse(TEXT("and the Masochist is not recorded as struck by itself"),
			Masochist.AbilitySystem->WasStruckBy(Masochist.AbilitySystem));
	}

	World->DestroyWorld(false);
	return true;
}

#undef CATACLYSM_CONVERTED_DEALT_TEST

#endif  // WITH_AUTOMATION_TESTS
