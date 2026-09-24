// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Nothing Stops It, the Ravager's Final Onslaught option 3. Issue #1515.
 *
 * "You cannot be brought below 1 health by a single hit. When a hit would have
 * done so you take no damage for 2 seconds, no more than once every 20 seconds."
 *
 * RULED 2026-09-23 UNDER THE OWNER'S DELEGATION: a damage over time tick is not
 * "a single hit", so only a hit is saved, and the window after a save stops all
 * damage, ticks included.
 *
 * THE STATS ARE GIVEN BY HAND, as the rows will give them: the option has no
 * row yet, and the rows follow when the design workbook comes back to this work.
 * `lethal_hit_survived_every_seconds` is the 20 and `damage_immunity_after_
 * lethal_hit_seconds` the 2.
 */
namespace CataclysmLethalHitTest
{
	using Vital = UCataclysmVitalAttributeSet;

	constexpr float Pool = 1000.0f;

	/** A character carrying the attribute sets a blow needs at both ends. */
	struct FScopedFighter
	{
		explicit FScopedFighter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template and
			// a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAllResist =
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAllResist);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), Pool);
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), Pool);
		}

		~FScopedFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		}

		/** Give this character flat figures, as passive rows would. */
		void Hold(const TMap<FName, float>& Stats)
		{
			TMap<FName, FCataclysmStatInputs> Inputs;
			for (const TPair<FName, float>& Stat : Stats)
			{
				FCataclysmStatModifier Flat;
				Flat.Bucket = ECataclysmStatBucket::Flat;
				Flat.Source = ECataclysmModifierSource::PassiveKeystone;
				Flat.Value = Stat.Value;
				FCataclysmStatInputs& Line = Inputs.FindOrAdd(Stat.Key);
				Line.Base = 0.0f;
				Line.Modifiers = {Flat};
			}
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		/** Hold the whole option: saved once every 20 seconds, then 2 of nothing. */
		void HoldNothingStopsIt()
		{
			Hold({{FName(UCataclysmAbilitySystemComponent::LethalHitSurvivedEverySecondsStat),
				   20.0f},
				  {FName(UCataclysmAbilitySystemComponent::ImmuneAfterLethalHitSecondsStat),
				   2.0f}});
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A blow worth `Damage` from `From` to `To`. */
	void Strike(const FScopedFighter& From, const FScopedFighter& To, float Damage)
	{
		UCataclysmSkillEffects::ApplyDirectDamage(From.Actor, To.Actor, Damage);
	}

	/** A damage over time tick worth `Damage`. */
	void TickOf(const FScopedFighter& From, const FScopedFighter& To, float Damage)
	{
		FCataclysmHitDelivery AsATick;
		AsATick.bIsDamageOverTime = true;
		UCataclysmSkillEffects::ApplyDirectDamage(From.Actor, To.Actor, Damage, AsATick);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLethalHitSavedTest,
	"Cataclysm.LethalHit.ALethalHitLeavesOneHealthAndTheNextTwoSecondsTakeNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A blow worth five times the pool leaves one health; a blow one second later
 * takes nothing; a blow after the two seconds hurts again.
 */
bool FCataclysmLethalHitSavedTest::RunTest(const FString&)
{
	using namespace CataclysmLethalHitTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Attacker(World);
	FScopedFighter Ravager(World);
	Ravager.HoldNothingStopsIt();

	const float SavedAt = World->GetTimeSeconds();
	Strike(Attacker, Ravager, Pool * 5.0f);
	TestEqual(TEXT("a blow worth five times the pool leaves one health"),
			  Ravager.Health(), 1.0f, 0.001f);
	TestEqual(TEXT("and the next save waits twenty seconds"),
			  Ravager.AbilitySystem->LethalHitSurvivalAllowedAt() - SavedAt, 20.0f,
			  0.01f);

	World->TimeSeconds += 1.0f;
	Strike(Attacker, Ravager, 300.0f);
	TestEqual(TEXT("one second later a blow of 300 takes nothing"),
			  Ravager.Health(), 1.0f, 0.001f);

	World->TimeSeconds += 1.1f;
	Strike(Attacker, Ravager, 0.5f);
	TestEqual(TEXT("and after the two seconds a blow hurts again"),
			  Ravager.Health(), 0.5f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLethalHitUnheldTest,
	"Cataclysm.LethalHit.WithoutTheIntervalALethalHitKills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A plain character dies to the blow, and so does one holding only the two
 * seconds: the interval above zero is what says the option is held.
 */
bool FCataclysmLethalHitUnheldTest::RunTest(const FString&)
{
	using namespace CataclysmLethalHitTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Attacker(World);
	FScopedFighter Plain(World);
	FScopedFighter WindowOnly(World);
	WindowOnly.Hold(
		{{FName(UCataclysmAbilitySystemComponent::ImmuneAfterLethalHitSecondsStat), 2.0f}});

	Strike(Attacker, Plain, Pool * 5.0f);
	Strike(Attacker, WindowOnly, Pool * 5.0f);
	TestEqual(TEXT("a plain character is killed"), Plain.Health(), 0.0f, 0.001f);
	TestEqual(TEXT("and so is one holding only the two seconds"),
			  WindowOnly.Health(), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLethalHitTickTest,
	"Cataclysm.LethalHit.ATickIsNotASingleHitButTheWindowStopsTicksToo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A lethal damage over time tick is not saved, and a tick inside the window
 * after a saved hit takes nothing. Both halves of the 2026-09-23 ruling.
 */
bool FCataclysmLethalHitTickTest::RunTest(const FString&)
{
	using namespace CataclysmLethalHitTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Attacker(World);
	FScopedFighter Ticked(World);
	FScopedFighter Struck(World);
	Ticked.HoldNothingStopsIt();
	Struck.HoldNothingStopsIt();

	TickOf(Attacker, Ticked, Pool * 5.0f);
	TestEqual(TEXT("a lethal tick is not a single hit, so it kills"),
			  Ticked.Health(), 0.0f, 0.001f);

	Strike(Attacker, Struck, Pool * 5.0f);
	World->TimeSeconds += 1.0f;
	TickOf(Attacker, Struck, 0.5f);
	TestEqual(TEXT("but a tick inside the window after a saved hit takes nothing"),
			  Struck.Health(), 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLethalHitIntervalTest,
	"Cataclysm.LethalHit.TheSaveComesOnceInTwentySecondsAndOnlyForALethalHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A second lethal hit ten seconds after a save kills; one twenty seconds after
 * is saved again; and a blow that would not kill spends nothing.
 */
bool FCataclysmLethalHitIntervalTest::RunTest(const FString&)
{
	using namespace CataclysmLethalHitTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Attacker(World);
	FScopedFighter Soon(World);
	FScopedFighter Later(World);
	FScopedFighter Grazed(World);
	Soon.HoldNothingStopsIt();
	Later.HoldNothingStopsIt();
	Grazed.HoldNothingStopsIt();

	Strike(Attacker, Soon, Pool * 5.0f);
	Strike(Attacker, Later, Pool * 5.0f);
	Strike(Attacker, Grazed, 400.0f);
	TestEqual(TEXT("a blow that would not kill lands in full"), Grazed.Health(),
			  Pool - 400.0f, 0.001f);
	TestTrue(TEXT("and spends no save"),
			 Grazed.AbilitySystem->LethalHitSurvivalAllowedAt() < 0.0f);

	World->TimeSeconds += 10.0f;
	Strike(Attacker, Soon, 5.0f);
	TestEqual(TEXT("ten seconds after a save, a lethal hit kills"), Soon.Health(), 0.0f,
			  0.001f);

	World->TimeSeconds += 10.1f;
	Strike(Attacker, Later, Pool * 5.0f);
	TestEqual(TEXT("twenty seconds after, one is saved again"), Later.Health(), 1.0f,
			  0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
