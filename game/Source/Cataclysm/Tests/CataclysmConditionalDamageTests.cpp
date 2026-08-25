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
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);

			Combat = NewCombat;

			// KEPT SINCE ISSUE #958, so a test can move the attacker's own
			// health. A bonus that applies only below a health threshold is
			// judged against this set by
			// `UCataclysmAbilitySystemComponent::CurrentConditions`, so a test
			// that cannot write to it cannot make such a bonus apply.
			Vitals = NewVitals;

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
		TObjectPtr<UCataclysmVitalAttributeSet> Vitals = nullptr;
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

	// AND NEITHER DOES THE PER-SKILL FORM OF THE SAME QUESTION. Issue #958.
	TestEqual(TEXT("nor does the per-skill form of it"),
		UCataclysmSkillEffects::IncreasesForSkill(nullptr,
												  FGameplayTagContainer()),
		0.0f, 0.001f);

	// AND A CHARACTER WITH NO SPELL DAMAGE ADDS NONE.
	TestEqual(TEXT("a character with no spell damage adds none"),
		UCataclysmSkillEffects::SpellDamageOf(Attacker.AbilitySystem,
											  FGameplayTagContainer()),
		0.0f, 0.001f);
	TestEqual(TEXT("and nothing at all adds none either"),
		UCataclysmSkillEffects::SpellDamageOf(nullptr, FGameplayTagContainer()),
		0.0f, 0.001f);

	return true;
}

// --------------------------------------------------------------------------
// Increased damage that depends on the character's own state. Issue #958.
//
// THE NODE THESE TWO ARE ABOUT is the Masochist's Living on the Edge: "While at
// or below 35% health, +2% increased damage per point", held at its full ten
// points, so 20 percentage points of increase.
//
// "INCREASED DAMAGE" IS TWO STATS AND SO IT IS TWO TESTS. The project owner
// settled on 2026-08-25 that the words mean every kind of damage the character
// DEALS -- attack damage and spell damage -- and not damage over time, which the
// design's own affix solve depends on being a direct-hit stat. The workbook
// carries the node as two rows and each has to arrive by its own route: attack
// damage through the increases bracket a hit reopens, spell damage as a flat
// addition worked out per skill.
//
// WHY EACH HAS TO BE A REAL HIT RATHER THAN A PIPELINE CALL. Both figures were
// read straight off a gameplay attribute, and an attribute by design carries no
// bonus that depends on the character's state. Every test in
// CataclysmStatPipelineTests.cpp would go on passing with those reads put back,
// and a wounded Masochist would simply never get the damage.
// --------------------------------------------------------------------------

namespace CataclysmConditionalDamageTest
{
	/** Twenty percentage points of increase, below 35% of maximum health. */
	FCataclysmStatModifier LivingOnTheEdge()
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::Increased;
		Modifier.Source = ECataclysmModifierSource::PassiveKeystone;
		Modifier.Value = 20.0f;
		Modifier.Condition = ECataclysmStatCondition::HealthAtOrBelowPercent;
		Modifier.ConditionValue = 35.0f;
		return Modifier;
	}

	/** A gear implicit of this size, which is where a base damage comes from. */
	FCataclysmStatModifier FlatFromGear(float Value)
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::Flat;
		Modifier.Source = ECataclysmModifierSource::GearImplicit;
		Modifier.Value = Value;
		return Modifier;
	}

	/**
	 * Blood Rush at its full eight points: "+2% increased damage per point for
	 * 2 seconds after you pay a health cost", so 16 points for two seconds.
	 * Issue #962.
	 */
	FCataclysmStatModifier BloodRush()
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::Increased;
		Modifier.Source = ECataclysmModifierSource::PassiveKeystone;
		Modifier.Value = 16.0f;
		Modifier.Condition =
			ECataclysmStatCondition::WithinSecondsOfHealthCost;
		Modifier.ConditionValue = 2.0f;
		return Modifier;
	}

	/**
	 * Vicious Onslaught at its full ten points: "+1% increased Attack Damage per
	 * point for every 5% of your maximum health that is missing", so ten
	 * percentage points per whole 5% missing. Issue #968.
	 */
	FCataclysmStatModifier ViciousOnslaught()
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::Increased;
		Modifier.Source = ECataclysmModifierSource::PassiveKeystone;
		Modifier.Value = 10.0f;
		Modifier.Scale =
			ECataclysmStatScale::PerPercentOfMaximumHealthMissing;
		Modifier.ScaleStep = 5.0f;
		return Modifier;
	}
}

CATACLYSM_CONDITIONAL_TEST(FCataclysmIncreasedDamageBelowAThresholdTest,
	"Cataclysm.ConditionalDamage.IncreasedDamageThatDependsOnHealthReachesARealHit")
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

	// THE CHARACTER SHEET AS `UCataclysmPlayerClassStats::ApplyTo` WOULD LEAVE
	// IT. A weapon carrying 1000 flat attack damage, nothing that increases it
	// all the time, and the node's 20 points which apply only when wounded. The
	// attribute therefore holds a plain 1000 and the remembered bracket is
	// nothing, which is exactly what ApplyTo writes for this character: it
	// judges every condition as unknown and so refuses it.
	Attacker.Combat->SetAttackDamage(1'000.0f);
	Attacker.AbilitySystem->SetAttackDamageIncreases(0.0f);

	FCataclysmStatInputs Inputs;
	Inputs.Base = 0.0f;
	Inputs.Modifiers.Add(FlatFromGear(1'000.0f));
	Inputs.Modifiers.Add(LivingOnTheEdge());

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(TEXT("attack_damage")), Inputs);
	Attacker.AbilitySystem->SetStatInputs(MoveTemp(Stats));

	// A HUNDRED HEALTH OUT OF A HUNDRED, so the share is the figure itself.
	Attacker.Vitals->SetMaxHealth(100.0f);
	Attacker.Vitals->SetHealth(100.0f);

	const float AtFullHealth =
		HealthLostTo(Attacker, Target, FGameplayTagContainer());

	// WOUNDED, AND NOTHING ELSE CHANGED. Not a point spent, not a stat written,
	// not the gameplay attribute touched. The health moved and that is all.
	Attacker.Vitals->SetHealth(30.0f);

	const float AtLowHealth =
		HealthLostTo(Attacker, Target, FGameplayTagContainer());

	if (!TestTrue(FString::Printf(TEXT("both hits landed (%.0f, %.0f)"),
								  AtFullHealth, AtLowHealth),
				  AtFullHealth > 0.0f && AtLowHealth > 0.0f))
	{
		return false;
	}

	TestEqual(FString::Printf(
		TEXT("the wounded attacker deals 20%% more, and dealt %.3f times"),
		AtLowHealth / AtFullHealth),
		AtLowHealth / AtFullHealth, 1.20f, 0.01f);

	// AND BACK AGAIN, which is what "resolved when the stat is used and never
	// stored" means. A bonus folded into the attribute would stay after the
	// character healed, and no test above would notice.
	Attacker.Vitals->SetHealth(100.0f);
	const float Healed = HealthLostTo(Attacker, Target, FGameplayTagContainer());

	TestEqual(TEXT("and loses it again on healing past the threshold"),
		Healed, AtFullHealth, 0.01f);

	return true;
}

CATACLYSM_CONDITIONAL_TEST(FCataclysmSpellDamageBelowAThresholdTest,
	"Cataclysm.ConditionalDamage.SpellDamageFollowsABonusThatDependsOnHealth")
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

	// NO WEAPON DAMAGE AT ALL, so the whole hit is the spell damage and the
	// ratio measures that stat and nothing else. The other half of the node is
	// covered by the test above.
	Attacker.Combat->SetAttackDamage(0.0f);
	Attacker.Combat->SetSpellDamage(500.0f);
	Attacker.AbilitySystem->SetAttackDamageIncreases(0.0f);

	FCataclysmStatInputs Inputs;
	Inputs.Base = 0.0f;
	Inputs.Modifiers.Add(FlatFromGear(500.0f));
	Inputs.Modifiers.Add(LivingOnTheEdge());

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(TEXT("spell_damage")), Inputs);
	Attacker.AbilitySystem->SetStatInputs(MoveTemp(Stats));

	Attacker.Vitals->SetMaxHealth(100.0f);
	Attacker.Vitals->SetHealth(100.0f);

	const float AtFullHealth = HealthLostTo(Attacker, Target, SpellTags());

	Attacker.Vitals->SetHealth(30.0f);
	const float AtLowHealth = HealthLostTo(Attacker, Target, SpellTags());

	if (!TestTrue(FString::Printf(TEXT("both spells landed (%.0f, %.0f)"),
								  AtFullHealth, AtLowHealth),
				  AtFullHealth > 0.0f && AtLowHealth > 0.0f))
	{
		return false;
	}

	TestEqual(FString::Printf(
		TEXT("the wounded caster deals 20%% more, and dealt %.3f times"),
		AtLowHealth / AtFullHealth),
		AtLowHealth / AtFullHealth, 1.20f, 0.01f);

	// AND A SKILL THAT IS NOT A SPELL TAKES NONE OF IT, wounded or not, which is
	// the rule the node must not break: spell damage is added to a spell and to
	// nothing else.
	TestEqual(TEXT("and a skill that is not a spell takes none of it"),
		HealthLostTo(Attacker, Target, FGameplayTagContainer()), 0.0f, 0.01f);

	return true;
}

CATACLYSM_CONDITIONAL_TEST(FCataclysmIncreasedDamageInAWindowTest,
	"Cataclysm.ConditionalDamage.IncreasedDamageFollowsAWindowAfterAHealthCost")
{
	using namespace CataclysmConditionalDamageTest;

	// THE MASOCHIST'S BLOOD RUSH NODE reaching a real hit. Issue #962.
	//
	// WHAT THIS PROVES THAT THE PIPELINE TEST DOES NOT. The pipeline test hands
	// the arithmetic a number of seconds. This one moves the world's clock and
	// lands three real hits, so it also covers the two joins between them:
	// `CurrentConditions` reading the timestamp, and the attack damage bracket
	// being worked out again at the moment of the hit rather than read off the
	// gameplay attribute.
	//
	// IT SHOWS A BONUS GOING AWAY WITH NOTHING HAPPENING, which no earlier test
	// could. A health threshold changes when the character is hit; a window
	// shuts while the character stands still.
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

	// THE CHARACTER SHEET AS `UCataclysmPlayerClassStats::ApplyTo` WOULD LEAVE
	// IT: a weapon's 1000 flat attack damage, nothing that increases it all the
	// time, and the node's 16 points which apply only inside the window.
	Attacker.Combat->SetAttackDamage(1'000.0f);
	Attacker.AbilitySystem->SetAttackDamageIncreases(0.0f);

	FCataclysmStatInputs Inputs;
	Inputs.Base = 0.0f;
	Inputs.Modifiers.Add(FlatFromGear(1'000.0f));
	Inputs.Modifiers.Add(BloodRush());

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(TEXT("attack_damage")), Inputs);
	Attacker.AbilitySystem->SetStatInputs(MoveTemp(Stats));

	// A CHARACTER THAT HAS PAID NOTHING, MEASURED FIRST. Without this the
	// comparison below would pass just as well if the window were open from
	// birth, which would make the node worth its bonus all the time.
	const float NeverPaid =
		HealthLostTo(Attacker, Target, FGameplayTagContainer());
	if (!TestTrue(FString::Printf(TEXT("the first hit lands (%.0f)"), NeverPaid),
				  NeverPaid > 0.0f))
	{
		return false;
	}

	// PAYING A HEALTH COST, BY THE SAME CALL THE SKILL PATH MAKES. That the
	// skill path really makes it is a separate test, in
	// CataclysmSkillTemplateTests.cpp, because this one cannot cast twice.
	Attacker.AbilitySystem->NoteHealthCostPaid();
	const float InsideTheWindow =
		HealthLostTo(Attacker, Target, FGameplayTagContainer());

	TestEqual(FString::Printf(
		TEXT("inside the window the hit is 16%% larger, and was %.3f times"),
		InsideTheWindow / NeverPaid),
		InsideTheWindow / NeverPaid, 1.16f, 0.01f);

	// AND THREE SECONDS LATER, WITH NOTHING ELSE CHANGED, it is back to what it
	// was. Not a point unspent, not a stat rewritten, not a hit taken. The clock
	// moved and that is all.
	World->TimeSeconds += 3.0f;
	const float AfterItShuts =
		HealthLostTo(Attacker, Target, FGameplayTagContainer());

	TestEqual(TEXT("and once the window shuts it is back to what it was"),
		AfterItShuts, NeverPaid, 0.01f);

	return true;
}

CATACLYSM_CONDITIONAL_TEST(FCataclysmDamageGrowsWithMissingHealthTest,
	"Cataclysm.ConditionalDamage.IncreasedDamageGrowsWithHowMuchHealthIsMissing")
{
	using namespace CataclysmConditionalDamageTest;

	// THE MASOCHIST'S VICIOUS ONSLAUGHT NODE reaching a real hit. Issue #968.
	//
	// WHAT THIS PROVES THAT THE PIPELINE TEST DOES NOT. The pipeline test hands
	// the arithmetic a health share. This one moves the attacker's real health
	// and lands three real hits, so it also covers the join between them: the
	// attack damage bracket being worked out again at the moment of the hit,
	// with the character's own health in hand.
	//
	// A SCALING BONUS COULD NOT REACH A HIT ANY OTHER WAY. It is never written
	// onto the gameplay attribute, because its size is different at every health
	// figure and the attribute is written once when gear changes.
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

	Attacker.Combat->SetAttackDamage(1'000.0f);
	Attacker.AbilitySystem->SetAttackDamageIncreases(0.0f);

	FCataclysmStatInputs Inputs;
	Inputs.Base = 0.0f;
	Inputs.Modifiers.Add(FlatFromGear(1'000.0f));
	Inputs.Modifiers.Add(ViciousOnslaught());

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(TEXT("attack_damage")), Inputs);
	Attacker.AbilitySystem->SetStatInputs(MoveTemp(Stats));

	// A HUNDRED HEALTH OUT OF A HUNDRED, so the share is the figure itself.
	Attacker.Vitals->SetMaxHealth(100.0f);
	Attacker.Vitals->SetHealth(100.0f);

	const float AtFullHealth =
		HealthLostTo(Attacker, Target, FGameplayTagContainer());
	if (!TestTrue(FString::Printf(TEXT("the first hit lands (%.0f)"),
								  AtFullHealth),
				  AtFullHealth > 0.0f))
	{
		return false;
	}

	// HALF HEALTH IS TEN WHOLE STEPS OF 5%, so +100% and twice the damage.
	Attacker.Vitals->SetHealth(50.0f);
	const float AtHalfHealth =
		HealthLostTo(Attacker, Target, FGameplayTagContainer());

	TestEqual(FString::Printf(
		TEXT("at half health the hit is twice as large, and was %.3f times"),
		AtHalfHealth / AtFullHealth),
		AtHalfHealth / AtFullHealth, 2.0f, 0.01f);

	// AND IT GROWS FURTHER AS THE CHARACTER IS HURT MORE, which is what
	// separates a scaling bonus from a conditional one. A condition would give
	// the same answer at 50% and at 25%.
	Attacker.Vitals->SetHealth(25.0f);
	const float AtQuarterHealth =
		HealthLostTo(Attacker, Target, FGameplayTagContainer());

	TestEqual(FString::Printf(
		TEXT("at a quarter health it is 2.5 times, and was %.3f times"),
		AtQuarterHealth / AtFullHealth),
		AtQuarterHealth / AtFullHealth, 2.5f, 0.01f);

	TestTrue(TEXT("so it is strictly larger at a quarter than at a half"),
		AtQuarterHealth > AtHalfHealth);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
