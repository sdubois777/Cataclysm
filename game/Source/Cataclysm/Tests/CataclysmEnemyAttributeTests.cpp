// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * Tests that an enemy actually carries the stat block it was designed with.
 *
 * WHAT THESE GUARD. Issue #372. `ApplyStartingAttributes` wrote exactly three
 * attributes -- maximum health, current health and attack damage -- out of the
 * roughly twenty an enemy has. Armour, all eight resistances, evasion and both
 * crit figures sat at the attribute sets' own defaults and nothing ever wrote to
 * them. The Brute, which the design document calls heavily armoured and gives
 * the second-highest armour share of the seven vertical slice enemies, had no
 * armour at all.
 *
 * THE SPLIT THESE TESTS ENFORCE, because it is the part that is easy to get
 * wrong later. The design model scales health, damage and armour by the
 * encounter's score and the enemy's rarity, and takes resistance, evasion and
 * the two crit figures unchanged from the archetype. So armour is SUPPLIED by
 * whoever spawns the creature, and the other four are DECLARED by the class. A
 * later change that moves armour onto the class would have to invent a score,
 * and one that moves crit chance off it would make a creature's crit depend on
 * which floor it was standing on.
 */

namespace CataclysmEnemyAttributeTest
{
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game,
										   /*bInformEngineOfWorld=*/false);
		if (World)
		{
			FURL URL;
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}
		return World;
	}

	/** What the ability system currently holds for one attribute. */
	static float Held(const AActor* Actor, const FGameplayAttribute& Attribute)
	{
		const UAbilitySystemComponent* System =
			UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(Attribute) : -1.0f;
	}

	/** Whether the actor's ability system holds the set this attribute lives in. */
	static bool Holds(const AActor* Actor, const FGameplayAttribute& Attribute)
	{
		const UAbilitySystemComponent* System =
			UCataclysmTargeting::AbilitySystemOf(Actor);
		return System && System->HasAttributeSetForAttribute(Attribute);
	}

	/** Every damage-type resistance, so a test can check all eight at once. */
	static TArray<TPair<const TCHAR*, FGameplayAttribute>> EveryResistance()
	{
		return {
			{TEXT("War"), UCataclysmResistanceAttributeSet::GetWarResistanceAttribute()},
			{TEXT("Demonic"), UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute()},
			{TEXT("Death"), UCataclysmResistanceAttributeSet::GetDeathResistanceAttribute()},
			{TEXT("Pestilence"), UCataclysmResistanceAttributeSet::GetPestilenceResistanceAttribute()},
			{TEXT("Famine"), UCataclysmResistanceAttributeSet::GetFamineResistanceAttribute()},
			{TEXT("Celestial"), UCataclysmResistanceAttributeSet::GetCelestialResistanceAttribute()},
			{TEXT("Chaos"), UCataclysmResistanceAttributeSet::GetChaosResistanceAttribute()},
			{TEXT("Void"), UCataclysmResistanceAttributeSet::GetVoidResistanceAttribute()},
		};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmABruteCarriesItsDesignedDefences,
	"Cataclysm.Brute.ItCarriesItsDesignedDefences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmABruteCarriesItsDesignedDefences::RunTest(const FString&)
{
	using namespace CataclysmEnemyAttributeTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute"), Brute))
	{
		return false;
	}

	// CALLED RATHER THAN WAITED FOR. InitAbilityActorInfo does not run for an
	// actor spawned into a world from UWorld::CreateWorld, so without this every
	// attribute below reads the attribute set's own default -- which is exactly
	// what this test failed with the first time it was run.
	Brute->ApplyStartingAttributes();

	// THE THREE FIGURES THE DESIGN GIVES THE BRUTE, and the whole complaint of
	// issue #372 is that a spawned Brute held none of them.
	TestEqual(TEXT("its crit chance is the designed 5 percent"),
		Held(Brute, UCataclysmCombatAttributeSet::GetCritChanceAttribute()),
		ACataclysmBruteCharacter::DesignedCritChancePercent, 0.01f);

	TestEqual(TEXT("its crit multiplier is the designed 200 percent"),
		Held(Brute, UCataclysmCombatAttributeSet::GetCritMultiplierAttribute()),
		ACataclysmBruteCharacter::DesignedCritMultiplierPercent, 0.01f);

	// ONE ALL-DAMAGE RESISTANCE. The design model gives an enemy one resistance
	// figure rather than a per-type profile, and the project owner ruled on
	// 2026-08-12 that an enemy carries that one figure and not the eight.
	TestEqual(TEXT("its all-damage resistance is the designed 15 percent"),
		Held(Brute, UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute()),
		ACataclysmBruteCharacter::DesignedResistancePercent, 0.01f);

	// AND IT DOES NOT HOLD THE EIGHT AT ALL -- not zero values, no attribute set.
	// This used to be the other way round, one figure written into all eight, and
	// it resisted nothing: a player's hit carries no damage type, so the lookup
	// selected none of the eight and skipped every one of them. Issue #486.
	for (const TPair<const TCHAR*, FGameplayAttribute>& Resistance : EveryResistance())
	{
		TestFalse(
			*FString::Printf(TEXT("it has no %s resistance attribute at all"),
							 Resistance.Key),
			Holds(Brute, Resistance.Value));
	}

	// AND THE CRIT MULTIPLIER IS NOT THE BASE ENEMY'S. Without this the test
	// above would pass if the Brute silently inherited the default, because it
	// compares against the Brute's own constant either way.
	ACataclysmEnemyCharacter* Ordinary =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(1000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("an ordinary enemy to compare against"), Ordinary))
	{
		Ordinary->ApplyStartingAttributes();

		TestNotEqual(TEXT("the Brute's crit multiplier differs from the default"),
			Held(Brute, UCataclysmCombatAttributeSet::GetCritMultiplierAttribute()),
			Held(Ordinary, UCataclysmCombatAttributeSet::GetCritMultiplierAttribute()));

		TestNotEqual(TEXT("and so does its resistance"),
			Held(Brute, UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute()),
			Held(Ordinary, UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute()));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmArmourIsSuppliedRatherThanDeclared,
	"Cataclysm.Enemy.ArmourIsSuppliedRatherThanDeclared",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmArmourIsSuppliedRatherThanDeclared::RunTest(const FString&)
{
	using namespace CataclysmEnemyAttributeTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Enemy = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("enemy"), Enemy))
	{
		return false;
	}

	const FGameplayAttribute Armour =
		UCataclysmCombatAttributeSet::GetArmorAttribute();

	// NONE UNTIL IT IS TOLD, which is the shape issue #372 settled on. Armour is
	// a share of a score-scaled base in the design model and nothing in the
	// engine knows a score, so the class cannot compute it.
	TestEqual(TEXT("an enemy nobody has armoured has no armour"),
		Held(Enemy, Armour), 0.0f, 0.01f);

	Enemy->SetArmour(250.0f);
	TestEqual(TEXT("and carries what it is given"),
		Held(Enemy, Armour), 250.0f, 0.01f);

	// ZERO IS A REAL ANSWER, unlike for health. The Imp's armour share is 0.0 in
	// the design model, so an unarmoured creature is designed rather than
	// unconfigured and must be expressible.
	Enemy->SetArmour(0.0f);
	TestEqual(TEXT("and can be set back to none, because zero armour is designed"),
		Held(Enemy, Armour), 0.0f, 0.01f);

	// A NEGATIVE FIGURE IS REFUSED rather than clamped silently, which is what
	// SetHealth and SetAttackDamage already do.
	Enemy->SetArmour(180.0f);
	Enemy->SetArmour(-40.0f);
	TestEqual(TEXT("a negative figure is refused and the last good one stands"),
		Held(Enemy, Armour), 180.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmAnOrdinaryEnemyCarriesTheBaselineProfile,
	"Cataclysm.Enemy.AnOrdinaryEnemyCarriesTheBaselineProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAnOrdinaryEnemyCarriesTheBaselineProfile::RunTest(const FString&)
{
	using namespace CataclysmEnemyAttributeTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Enemy = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("enemy"), Enemy))
	{
		return false;
	}

	Enemy->ApplyStartingAttributes();

	// THE MODEL'S BASELINE ARCHETYPE, so a creature whose own figures have not
	// been decided carries what the model gives an undesigned one rather than
	// whatever the attribute set happened to default to.
	// tools/tests/test_enemy_profile_defaults.py holds the two together.
	TestEqual(TEXT("crit chance defaults to 5 percent"),
		Held(Enemy, UCataclysmCombatAttributeSet::GetCritChanceAttribute()),
		5.0f, 0.01f);

	TestEqual(TEXT("crit multiplier defaults to 150 percent"),
		Held(Enemy, UCataclysmCombatAttributeSet::GetCritMultiplierAttribute()),
		150.0f, 0.01f);

	TestEqual(TEXT("evasion defaults to none"),
		Held(Enemy, UCataclysmCombatAttributeSet::GetEvasionAttribute()),
		0.0f, 0.01f);

	TestEqual(TEXT("all-damage resistance defaults to none"),
		Held(Enemy, UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute()),
		0.0f, 0.01f);

	for (const TPair<const TCHAR*, FGameplayAttribute>& Resistance : EveryResistance())
	{
		TestFalse(
			*FString::Printf(TEXT("no %s resistance attribute at all"),
							 Resistance.Key),
			Holds(Enemy, Resistance.Value));
	}

	// DEMONIC, BECAUSE THE WHOLE VERTICAL SLICE IS. An enemy that dealt an
	// untyped hit would meet none of the player's eight resistances, which is
	// the state issue #486 describes and the one thing that has to stay typed.
	TestEqual(TEXT("an enemy deals Demonic damage by default"),
		Enemy->DamageType, FName(TEXT("Demonic")));

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
