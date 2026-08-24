// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"
#include "Misc/ScopeExit.h"

/**
 * Spell damage, and increased damage against a target's own damage type. #895.
 *
 * NINE AFFIXES, AND NONE OF THEM DID ANYTHING. `SpellDamage` and the eight
 * `DamageVs...` attributes all existed, were clamped, were replicated, and no
 * code in the project read any of them.
 *
 * THE EIGHT WERE BROKEN TWICE OVER. Each was recorded in the design workbook as
 * an `increased` modifier, which multiplies whatever its stat already holds, and
 * the stat it names IS the bucket of conditional increases: nothing else puts
 * anything in it, so every one of the eight multiplied zero. They are `flat`
 * contributions into that bucket now, which is what the design meant by "they
 * add into the same bracket as Increased Damage". The same correction was made
 * to cooldown reduction the same day; docs/DECISIONS.md carries both.
 *
 * THE ADDITIVE RULE IS THE POINT, AND THE DIFFERENCE IS LARGE. A character at
 * +125% increased damage striking a matching enemy with a top-tier +400% affix
 * deals 6.25 times its base if the two add and 11.25 times if they multiply. The
 * design says they add, and cites Diablo 4 and Last Epoch for it.
 */
namespace CataclysmConditionalDamageTest
{
	/** An attacker whose damage stats can be set. */
	struct FCaster
	{
		explicit FCaster(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template
			// and a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmVitalAttributeSet>(Actor));

			Combat = NewCombat;
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Combat->SetAttackDamage(1'000.0f);
			Combat->SetSpellDamage(0.0f);
		}

		~FCaster()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmCombatAttributeSet> Combat = nullptr;
	};

	/**
	 * A creature of a named damage type, with nothing that would mitigate.
	 *
	 * ITS DEFENCES ARE STRIPPED AFTER IT BEGINS PLAY, because an enemy applies
	 * its archetype's armour, resistance and energy shield there, and this
	 * measures what the attacker sent rather than what the target stopped.
	 */
	ACataclysmEnemyCharacter* SpawnTarget(UWorld* World, const TCHAR* DamageType)
	{
		ACataclysmEnemyCharacter* Enemy =
			World->SpawnActor<ACataclysmEnemyCharacter>(
				FVector(500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (!Enemy)
		{
			return nullptr;
		}

		Enemy->DamageType = FName(DamageType);

		if (UAbilitySystemComponent* AbilitySystem =
				Enemy->GetAbilitySystemComponent())
		{
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetDamageReductionAttribute(), 0.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(),
				0.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 0.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);

			// Deep enough that nothing here kills it, so every hit is measured
			// in full rather than being cut short at zero health.
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000'000.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), 1'000'000.0f);
		}

		return Enemy;
	}

	/** How much health one hit took off it. */
	float HealthLostTo(FCaster& Attacker, ACataclysmEnemyCharacter* Target,
					   const FGameplayTagContainer& SkillTags)
	{
		UAbilitySystemComponent* AbilitySystem =
			Target->GetAbilitySystemComponent();
		const FGameplayAttribute Health =
			UCataclysmVitalAttributeSet::GetHealthAttribute();

		const float Before = AbilitySystem->GetNumericAttribute(Health);
		UCataclysmSkillEffects::ApplyHit(
			Attacker.Actor, Target, /*DamagePercent=*/100.0f, SkillTags,
			FCataclysmHitDelivery());
		return Before - AbilitySystem->GetNumericAttribute(Health);
	}

	/** The tag list of a skill that is a spell. */
	FGameplayTagContainer SpellTags()
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
			FName(UCataclysmSkillEffects::SpellTagName),
			/*ErrorIfNotFound=*/false));
		return Tags;
	}
}

#define CATACLYSM_CONDITIONAL_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_CONDITIONAL_TEST(FCataclysmDamageAgainstTypeReadsTheTargetTest,
	"Cataclysm.ConditionalDamage.ItReadsTheTargetsTypeAndNotTheAttackers")
{
	using namespace CataclysmConditionalDamageTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FCaster Attacker(World);
	ACataclysmEnemyCharacter* Demon = SpawnTarget(World, TEXT("Demonic"));
	ACataclysmEnemyCharacter* Deathly = SpawnTarget(World, TEXT("Death"));
	if (!TestNotNull(TEXT("a Demonic target"), Demon)
		|| !TestNotNull(TEXT("a Death target"), Deathly))
	{
		return false;
	}

	// WHAT AN ORDINARY HIT TAKES, measured before anything is granted, so the
	// figures below are evidence of the affix rather than of the hit.
	const float Plain = HealthLostTo(Attacker, Demon, FGameplayTagContainer());
	if (!TestTrue(FString::Printf(TEXT("an ordinary hit lands (%.1f)"), Plain),
				  Plain > 0.0f))
	{
		return false;
	}

	Attacker.Combat->SetDamageVsDemonic(400.0f);

	// AGAINST THE TYPE IT NAMES, five times as much: 1 + 4.00.
	TestEqual(TEXT("400% against Demonic makes a hit on a Demon five times"),
		HealthLostTo(Attacker, Demon, FGameplayTagContainer()),
		Plain * 5.0f, Plain * 0.02f);

	// AND AGAINST ANY OTHER TYPE, NOTHING. "They read the target, not the
	// weapon... This affix applies when that type matches and does nothing
	// otherwise."
	TestEqual(TEXT("and changes nothing about a hit on a Death creature"),
		HealthLostTo(Attacker, Deathly, FGameplayTagContainer()),
		Plain, Plain * 0.02f);

	return true;
}

CATACLYSM_CONDITIONAL_TEST(FCataclysmConditionalDamageAddsTest,
	"Cataclysm.ConditionalDamage.ItAddsIntoTheSameBracketRatherThanMultiplying")
{
	using namespace CataclysmConditionalDamageTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FCaster Attacker(World);
	ACataclysmEnemyCharacter* Demon = SpawnTarget(World, TEXT("Demonic"));
	if (!TestNotNull(TEXT("a Demonic target"), Demon))
	{
		return false;
	}

	// A CHARACTER ALREADY CARRYING GENERIC INCREASED DAMAGE. Its attack damage
	// attribute is the finished figure with those increases already inside it,
	// which is why the character has to remember the bracket separately.
	constexpr float Generic = 1.25f;
	constexpr float Conditional = 4.00f;

	Attacker.Combat->SetAttackDamage(1'000.0f * (1.0f + Generic));
	Attacker.AbilitySystem->SetAttackDamageIncreases(Generic);

	const float WithoutTheAffix =
		HealthLostTo(Attacker, Demon, FGameplayTagContainer());
	if (!TestTrue(TEXT("the hit lands at all"), WithoutTheAffix > 0.0f))
	{
		return false;
	}

	Attacker.Combat->SetDamageVsDemonic(Conditional * 100.0f);
	const float WithTheAffix =
		HealthLostTo(Attacker, Demon, FGameplayTagContainer());

	// THE TWO ANSWERS ARE FAR APART AND ONLY ONE IS THE DESIGN'S.
	//
	//   adding      (1 + 1.25 + 4.00) / (1 + 1.25) = 2.778 times
	//   multiplying (1 + 1.25) x (1 + 4.00) / (1 + 1.25) = 5.000 times
	const float Adding =
		(1.0f + Generic + Conditional) / (1.0f + Generic);
	const float Multiplying = 1.0f + Conditional;

	TestEqual(FString::Printf(
		TEXT("the conditional increase adds into the same bracket: %.3f times"),
		WithTheAffix / WithoutTheAffix),
		WithTheAffix / WithoutTheAffix, Adding, 0.02f);

	TestTrue(FString::Printf(
		TEXT("and is not a second multiplier, which would be %.3f times"),
		Multiplying),
		!FMath::IsNearlyEqual(WithTheAffix / WithoutTheAffix, Multiplying, 0.02f));

	return true;
}

CATACLYSM_CONDITIONAL_TEST(FCataclysmSpellDamageTest,
	"Cataclysm.ConditionalDamage.SpellDamageIsAddedToASpellAndToNothingElse")
{
	using namespace CataclysmConditionalDamageTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FCaster Attacker(World);
	ACataclysmEnemyCharacter* Target = SpawnTarget(World, TEXT("Demonic"));
	if (!TestNotNull(TEXT("a target"), Target))
	{
		return false;
	}

	const FGameplayTagContainer Spell = SpellTags();
	if (!TestTrue(TEXT("the Type.Spell tag exists in the vocabulary"),
				  UCataclysmSkillEffects::IsSpell(Spell)))
	{
		return false;
	}

	// WITH NO SPELL DAMAGE, A SPELL AND A STRIKE DEAL THE SAME. That is the
	// project owner's choice: a spell keeps the weapon's damage as its base
	// rather than ignoring it as Path of Exile does, so a Wand's own flat damage
	// is worth something to the caster holding it.
	const float PlainStrike =
		HealthLostTo(Attacker, Target, FGameplayTagContainer());
	const float PlainSpell = HealthLostTo(Attacker, Target, Spell);

	if (!TestTrue(TEXT("a hit lands at all"), PlainStrike > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("with no spell damage a spell deals what a strike deals"),
		PlainSpell, PlainStrike, PlainStrike * 0.02f);

	// AND SPELL DAMAGE RAISES THE SPELL AND NOT THE STRIKE.
	Attacker.Combat->SetSpellDamage(500.0f);

	const float ArmedSpell = HealthLostTo(Attacker, Target, Spell);
	const float ArmedStrike =
		HealthLostTo(Attacker, Target, FGameplayTagContainer());

	TestTrue(FString::Printf(
		TEXT("spell damage raises a spell, %.1f to %.1f"),
		PlainSpell, ArmedSpell),
		ArmedSpell > PlainSpell + 1.0f);

	TestEqual(TEXT("and leaves a skill that is not a spell alone"),
		ArmedStrike, PlainStrike, PlainStrike * 0.02f);

	return true;
}

CATACLYSM_CONDITIONAL_TEST(FCataclysmConditionalDamageReadsTest,
	"Cataclysm.ConditionalDamage.NothingToReadMeansNoChange")
{
	using namespace CataclysmConditionalDamageTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FCaster Attacker(World);
	AActor* NotACreature = World->SpawnActor<AActor>();

	// ONLY AN ENEMY CARRIES A DAMAGE TYPE, so a hit on anything else finds no
	// type to match and the affix correctly does nothing.
	Attacker.Combat->SetDamageVsDemonic(400.0f);
	TestEqual(TEXT("an actor that is not a creature has no type to match"),
		UCataclysmSkillEffects::DamageAgainstTypeOf(
			Attacker.AbilitySystem, NotACreature), 0.0f, 0.001f);
	TestEqual(TEXT("and neither does nothing at all"),
		UCataclysmSkillEffects::DamageAgainstTypeOf(
			Attacker.AbilitySystem, nullptr), 0.0f, 0.001f);

	// AN ABILITY SYSTEM THIS PROJECT DID NOT MAKE REMEMBERS NO BRACKET, and zero
	// is the right answer: with no increases the arithmetic is what it was
	// before any of this existed.
	TestEqual(TEXT("nothing at all remembers no increases"),
		UCataclysmSkillEffects::IncreasesBehindAttackDamage(nullptr),
		0.0f, 0.001f);

	// AND A CHARACTER WITH NO SPELL DAMAGE ADDS NONE.
	TestEqual(TEXT("a character with no spell damage adds none"),
		UCataclysmSkillEffects::SpellDamageOf(Attacker.AbilitySystem),
		0.0f, 0.001f);
	TestEqual(TEXT("and nothing at all adds none either"),
		UCataclysmSkillEffects::SpellDamageOf(nullptr), 0.0f, 0.001f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
