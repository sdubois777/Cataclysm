// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * An enemy's energy shield. Issue #485.
 *
 * WHAT WAS WRONG. The designed fraction reached
 * `game/Data/EnemyArchetypes.csv` and reached `FCataclysmEnemyArchetypeRow` in
 * `game/Source/Cataclysm/Data/CataclysmDataRows.h`, and then stopped.
 * `ACataclysmEnemyCharacter` had no property for it and
 * `ApplyStartingAttributes` never wrote `MaxEnergyShield`, so every enemy in the
 * editor had a shield of zero whatever the design said. The layer itself works --
 * it is step 7 of the eight in `UCataclysmDamageCalculation::Resolve` -- so the
 * number was the only thing missing.
 *
 * THIS CHANGES NO CREATURE IN THE GAME TODAY, and that is worth stating rather
 * than discovering. Only two of the seven designed slice enemies carry a shield,
 * the Succubus at 0.50 and the Corrupted Sentinel at 0.35, and NEITHER HAS A C++
 * CLASS -- only the Brute and the Abyssal Warden are built, and both are designed
 * at 0.00. What this builds is the route, so the number arrives the moment either
 * creature exists. `tools/tests/test_enemy_energy_shield_reaches_the_engine.py`
 * fails as soon as one of them appears without setting the fraction.
 */

namespace CataclysmEnemyShieldTest
{
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}

	/** The Succubus's designed fraction, from ARCHETYPES in the model. */
	constexpr float SuccubusShieldFraction = 0.50f;

	static float Attribute(const UAbilitySystemComponent* Abilities,
						   const FGameplayAttribute& Which)
	{
		return Abilities ? Abilities->GetNumericAttribute(Which) : -1.0f;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_TEST(FCataclysmEnemyShieldIsAShareOfHealthTest,
	"Cataclysm.Enemy.AnEnergyShieldIsAShareOfTheCreaturesHealth")
{
	using namespace CataclysmEnemyShieldTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Enemy =
		World->SpawnActor<ACataclysmEnemyCharacter>();
	if (!Enemy)
	{
		AddError(TEXT("could not spawn an enemy"));
		return false;
	}

	UAbilitySystemComponent* Abilities = Enemy->GetAbilitySystemComponent();
	if (!Abilities)
	{
		AddError(TEXT("the enemy has no ability system component"));
		return false;
	}

	// NO SHIELD BY DEFAULT, which is right: five of the seven designed slice
	// enemies have a fraction of exactly zero.
	Enemy->SetHealth(1000.0f);
	TestEqual(TEXT("an enemy with no designed fraction has no shield"),
		Attribute(Abilities,
				  UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()),
		0.0f);

	// AND THE SHIELD IS THE FRACTION OF THE HEALTH, which is the arithmetic
	// `stats_for` in sim/cataclysm_sim/enemy_stats.py does.
	Enemy->SetEnergyShieldFraction(SuccubusShieldFraction);
	TestEqual(TEXT("half of 1000 health is a 500 shield"),
		Attribute(Abilities,
				  UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()),
		500.0f);

	// FULL, NOT EMPTY. A creature arrives with its shield up.
	TestEqual(TEXT("and it starts full"),
		Attribute(Abilities,
				  UCataclysmVitalAttributeSet::GetEnergyShieldAttribute()),
		500.0f);

	// AND IT FOLLOWS THE HEALTH, because the fraction is what is stored rather
	// than an absolute figure that could disagree with the health beside it.
	Enemy->SetHealth(2000.0f);
	TestEqual(TEXT("doubling the health doubles the shield"),
		Attribute(Abilities,
				  UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()),
		1000.0f);

	// ORDER DOES NOT MATTER. Setting the fraction before the health has to give
	// the same answer as setting it after, or a spawner would have to know which
	// way round to call them.
	ACataclysmEnemyCharacter* Other =
		World->SpawnActor<ACataclysmEnemyCharacter>();
	if (Other)
	{
		Other->SetEnergyShieldFraction(SuccubusShieldFraction);
		Other->SetHealth(2000.0f);
		TestEqual(TEXT("the fraction set first gives the same shield"),
			Attribute(Other->GetAbilitySystemComponent(),
					  UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()),
			1000.0f);
	}

	// A NEGATIVE FRACTION IS REFUSED, exactly as a negative armour is, and the
	// previous value stands rather than being replaced by nonsense.
	Enemy->SetEnergyShieldFraction(-1.0f);
	TestEqual(TEXT("a negative fraction is refused"),
		Attribute(Abilities,
				  UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()),
		1000.0f);
	return true;
}

CATACLYSM_TEST(FCataclysmEnemyShieldAbsorbsBeforeHealthTest,
	"Cataclysm.Enemy.AnEnergyShieldAbsorbsAHitBeforeHealthDoes")
{
	using namespace CataclysmEnemyShieldTest;

	// THE POINT OF THE WHOLE CHANGE. The two tests above show the number arrives;
	// this shows it does something, by landing a real hit through
	// UCataclysmVitalAttributeSet::PostGameplayEffectExecute and reading both
	// pools.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Attacker =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Defender =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(1000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!Attacker || !Defender)
	{
		AddError(TEXT("could not spawn an attacker and a defender"));
		return false;
	}

	// Hostile, or the hit finds nobody to land on.
	Defender->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	Defender->SetHealth(1000.0f);
	Defender->SetEnergyShieldFraction(SuccubusShieldFraction);

	UAbilitySystemComponent* Abilities = Defender->GetAbilitySystemComponent();
	if (!Abilities)
	{
		AddError(TEXT("the defender has no ability system component"));
		return false;
	}

	const float ShieldBefore = Attribute(
		Abilities, UCataclysmVitalAttributeSet::GetEnergyShieldAttribute());
	const float HealthBefore = Attribute(
		Abilities, UCataclysmVitalAttributeSet::GetHealthAttribute());
	if (!TestEqual(TEXT("it starts with a 500 shield"), ShieldBefore, 500.0f))
	{
		return false;
	}

	// SMALLER THAN THE SHIELD, so the whole hit has somewhere to go and health
	// must be untouched. A hit larger than the shield would prove less: it would
	// pass whether or not the shield existed.
	UCataclysmSkillEffects::ApplyDirectDamage(Attacker, Defender, 200.0f);

	const float ShieldAfter = Attribute(
		Abilities, UCataclysmVitalAttributeSet::GetEnergyShieldAttribute());
	const float HealthAfter = Attribute(
		Abilities, UCataclysmVitalAttributeSet::GetHealthAttribute());

	TestEqual(TEXT("the shield took the hit"), ShieldAfter, 300.0f);
	TestEqual(TEXT("and health is untouched"), HealthAfter, HealthBefore);
	return true;
}

CATACLYSM_TEST(FCataclysmWardenShieldIsDesignedZeroTest,
	"Cataclysm.Warden.ItsEnergyShieldFractionIsADesignedZero")
{
	using namespace CataclysmEnemyShieldTest;

	// WRITTEN OUT RATHER THAN LEFT TO THE BASE CLASS'S DEFAULT, which is what
	// this creature already does for its designed evasion of 0.0 and for the
	// same reason: the zero is visibly designed rather than visibly forgotten.
	// Its survivability is armour and resistance, the highest in the slice.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmAbyssalWardenCharacter* Warden =
		World->SpawnActor<ACataclysmAbyssalWardenCharacter>();
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	TestEqual(TEXT("its designed fraction is zero"),
		ACataclysmAbyssalWardenCharacter::DesignedEnergyShieldFraction, 0.0f);
	TestEqual(TEXT("and it carries that rather than the base's default"),
		Warden->EnergyShieldFraction,
		ACataclysmAbyssalWardenCharacter::DesignedEnergyShieldFraction);

	Warden->SetHealth(50'000.0f);
	TestEqual(TEXT("so a great deal of health still gives no shield"),
		Attribute(Warden->GetAbilitySystemComponent(),
				  UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()),
		0.0f);
	return true;
}

#endif  // WITH_AUTOMATION_TESTS
