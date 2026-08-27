// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A melee critical strike's chance to make what it hit bleed. Issue #1032.
 *
 * THE MASOCHIST'S MUTILATION MASTERY IS THE ONLY SOURCE: "Your melee critical
 * strikes have a 5% chance per point to apply Bleeding", eight points, so 40%
 * at most. `Masochist_basic_fc_b2` in `game/Data/PassiveNodes.csv`.
 *
 * THREE SEPARATE PIECES ARE UNDER TEST HERE AND ONLY THE LAST IS THE NODE:
 *
 *   1. `Type.Melee` reaching the defender at all. A skill's tags stop where the
 *      blow is BUILT -- `ApplyHit` uses them to work out the damage and then
 *      hands on an `FCataclysmHitDelivery` -- and only the handful of tags
 *      `ApplyTypedSpec` puts on the spec reach whoever is hit. Melee is the
 *      fifth to make that journey.
 *   2. The rule in `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`,
 *      which requires all three of melee, critical, and damage that actually
 *      reached health.
 *   3. The authored row that gives a real character the chance. That one is in
 *      `CataclysmPassiveTreeTests.cpp` beside the other real-character tests,
 *      as `Cataclysm.Passives.MutilationMasteryGivesARealMasochistFortyPercent`.
 *
 * THE ROLL ITSELF IS NOT EXERCISED END TO END AND CANNOT BE. It is
 * `FMath::FRandRange(0.0f, 100.0f)` inside `PostGameplayEffectExecute` with no
 * injection point, unlike the evasion, block and critical strike rolls, which
 * `UCataclysmDamageCalculation::Resolve` takes as parameters. So every test
 * below drives the chance to 100, where the comparison is true for every draw
 * the range can return, or to 0, where the guard above the roll means no draw is
 * made at all. Both ends are decided without the roll. What is NOT covered is
 * that a chance of 40 makes a bleed happen about 40% of the time; a test of that
 * would either be statistical or would have to pin a roll that cannot be pinned.
 * Issue #1034 is the same gap on the blunt weapon stun, which copies this shape.
 *
 * EVERY NEGATIVE DIRECTION HAS ITS OWN TEST, and they are the ones that matter.
 * A build that dropped any one of the three conditions would still pass a test
 * that only checked "a melee critical strike at full chance applies Bleeding",
 * and would hand the node several times what it reads.
 */
namespace CataclysmMeleeBleedTest
{
	using Combat = UCataclysmCombatAttributeSet;
	using Debuffs = UCataclysmDebuffs;
	using Effects = UCataclysmSkillEffects;
	using Vital = UCataclysmVitalAttributeSet;

	/** A bare actor holding every attribute set, usable as either side. */
	struct FScopedFighter
	{
		explicit FScopedFighter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template
			// and a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAll =
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAll);

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			// LARGE ENOUGH THAT NOTHING BELOW EVER APPROACHES DEATH, and large
			// enough that the floor `Resolve` puts on the health step never
			// interferes: it ends with `Min(Damage, Health)`, so a defender with
			// little health left reports its health rather than the hit.
			AbilitySystem->SetNumericAttributeBase(
				Vital::GetMaxHealthAttribute(), 1'000'000.0f);
			AbilitySystem->SetNumericAttributeBase(
				Vital::GetHealthAttribute(), 1'000'000.0f);
		}

		~FScopedFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** Enough weapon damage that `ApplyHit` has something to deal. */
		void ArmFor(float AttackDamage) const
		{
			AbilitySystem->SetNumericAttributeBase(
				Combat::GetAttackDamageAttribute(), AttackDamage);
		}

		/** Give this fighter a critical strike chance and multiplier. */
		void SetCritical(float ChancePercent, float MultiplierPercent) const
		{
			AbilitySystem->SetNumericAttributeBase(
				Combat::GetCritChanceAttribute(), ChancePercent);
			AbilitySystem->SetNumericAttributeBase(
				Combat::GetCritMultiplierAttribute(), MultiplierPercent);
		}

		/** What Mutilation Mastery would have written onto this character. */
		void SetBleedOnCritChance(float Percent) const
		{
			AbilitySystem->SetNumericAttributeBase(
				Combat::GetBleedOnCritChanceAttribute(), Percent);
		}

		float BleedOnCritChance() const
		{
			return AbilitySystem->GetNumericAttribute(
				Combat::GetBleedOnCritChanceAttribute());
		}

		bool IsBleeding() const { return Debuffs::IsBleeding(AbilitySystem); }

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	/**
	 * A tag container holding `Type.Melee`, the way a Fist skill row does.
	 *
	 * REQUESTED BY NAME RATHER THAN DECLARED NATIVELY, for the reason
	 * `UCataclysmDebuffs::BleedTag` gives: a native declaration would create the
	 * tag whether or not `game/Config/Tags/CataclysmTags.ini` still lists it,
	 * hiding exactly the disagreement that matters. An invalid tag here makes
	 * the test say so rather than quietly passing an empty container.
	 */
	static FGameplayTagContainer MeleeSkillTags()
	{
		FGameplayTagContainer Tags;
		const FGameplayTag Melee = UCataclysmDamageCalculation::MeleeTag();
		if (Melee.IsValid())
		{
			Tags.AddTag(Melee);
		}
		return Tags;
	}

	/** An attacker that will make anything it critically strikes in melee bleed. */
	static void MakeAMutilator(const FScopedFighter& Attacker)
	{
		Attacker.ArmFor(1'000.0f);

		// 150% IS THE DESIGN'S OWN DEFAULT, from the shared class stat line in
		// the Class Stats sheet of `docs/All_Things_Cataclysm.xlsx`. The chance
		// of 100 is not: it is a pin, and the paragraph at the top of this file
		// says why the real 40 cannot be used here.
		Attacker.SetCritical(/*Chance=*/100.0f, /*Multiplier=*/150.0f);
		Attacker.SetBleedOnCritChance(100.0f);
	}
}

#define CATACLYSM_MELEE_BLEED_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// The one direction that applies Bleeding
// ---------------------------------------------------------------------------

CATACLYSM_MELEE_BLEED_TEST(FCataclysmMeleeCritBleedsTest,
	"Cataclysm.MeleeBleed.AMeleeCriticalStrikeAtFullChanceAppliesBleeding")
{
	using namespace CataclysmMeleeBleedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	// THE TAG HAS TO EXIST BEFORE ANY OF THIS MEANS ANYTHING. Without it
	// `MeleeSkillTags` hands back an empty container, the blow is not melee, and
	// every assertion below would be measuring the wrong thing while reading
	// like a rule that does not fire.
	const FGameplayTagContainer Melee = MeleeSkillTags();
	if (!TestFalse(TEXT("the vocabulary still has Type.Melee"), Melee.IsEmpty()))
	{
		World->DestroyWorld(false);
		return false;
	}

	{
		const FScopedFighter Attacker(World);
		const FScopedFighter Defender(World);
		MakeAMutilator(Attacker);

		TestFalse(TEXT("nothing is bleeding before the blow"),
			Defender.IsBleeding());

		// 0 ALWAYS CRITICALLY STRIKES, because every chance above zero beats it.
		const CataclysmTestWorld::FScopedCritRoll AlwaysCrits(0.0f);
		Effects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f, Melee);

		TestTrue(TEXT("a melee critical strike at a chance of 100 makes the "
					  "defender bleed"),
			Defender.IsBleeding());

		// AND IT IS A DEBUFF THE FIVE MASOCHIST NODES CAN SEE, which is the
		// point of applying it through `ApplyDamageOverTime` rather than by
		// writing a tag. `UCataclysmDebuffs::CountOn` reads what really landed.
		TestEqual(TEXT("and carries exactly one debuff for it"),
			Debuffs::CountOn(Defender.AbilitySystem), 1);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_MELEE_BLEED_TEST(FCataclysmCallerSetMeleeBleedsTest,
	"Cataclysm.MeleeBleed.ACallersOwnMeleeFlagReachesTheDefenderWithoutASkillTag")
{
	using namespace CataclysmMeleeBleedTest;

	// TWO ROUTES SET THE SAME FLAG AND THEY ARE COMBINED RATHER THAN ONE
	// OVERRIDING THE OTHER. `ApplyHit` reads `Type.Melee` off the skill's tags,
	// and a caller may also say so outright on the delivery -- which is how an
	// enemy's C++ ability states it, since it has no skill row to carry a tag.
	// The test above covers the tag route; this covers the caller's, with an
	// EMPTY tag container, so a build that only honoured the tag fails here.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedFighter Attacker(World);
		const FScopedFighter Defender(World);
		MakeAMutilator(Attacker);

		FCataclysmHitDelivery Struck;
		Struck.bIsMelee = true;

		const CataclysmTestWorld::FScopedCritRoll AlwaysCrits(0.0f);
		Effects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
						  FGameplayTagContainer(), Struck);

		TestTrue(TEXT("a blow the caller called melee makes the defender bleed "
					  "even with no skill tags at all"),
			Defender.IsBleeding());
	}

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// The four directions that apply nothing. Each drops one condition and keeps
// the rest, so a passing test names which condition the build has lost.
// ---------------------------------------------------------------------------

CATACLYSM_MELEE_BLEED_TEST(FCataclysmOrdinaryMeleeDoesNotBleedTest,
	"Cataclysm.MeleeBleed.AnOrdinaryMeleeHitAppliesNothing")
{
	using namespace CataclysmMeleeBleedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedFighter Attacker(World);
		const FScopedFighter Defender(World);
		MakeAMutilator(Attacker);

		// 100 NEVER CRITICALLY STRIKES, because the comparison is strictly less
		// than. Everything else about this blow is what the positive test used.
		{
			const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
			Effects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
							  MeleeSkillTags());

			TestFalse(TEXT("a melee hit that did not critically strike applies "
						   "no Bleeding"),
				Defender.IsBleeding());
			TestEqual(TEXT("and leaves no debuff behind"),
				Debuffs::CountOn(Defender.AbilitySystem), 0);
		}

		// AND THE SAME ATTACKER'S CRITICAL STRIKE DOES BLEED IT, which is what
		// makes the reading above a rule rather than a blow that failed to land
		// for some other reason -- a missing weapon, an unregistered attribute
		// set, a tag the vocabulary lost.
		{
			const CataclysmTestWorld::FScopedCritRoll AlwaysCrits(0.0f);
			Effects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
							  MeleeSkillTags());
			TestTrue(TEXT("while the same attacker's critical strike does"),
				Defender.IsBleeding());
		}
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_MELEE_BLEED_TEST(FCataclysmRangedCritDoesNotBleedTest,
	"Cataclysm.MeleeBleed.ACriticalStrikeThatIsNotMeleeAppliesNothing")
{
	using namespace CataclysmMeleeBleedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedFighter Attacker(World);
		const FScopedFighter Defender(World);
		MakeAMutilator(Attacker);

		const CataclysmTestWorld::FScopedCritRoll AlwaysCrits(0.0f);

		// NO TAGS AND NO FLAG, which is what a spell or a bow shot is. This is
		// the direction that would break if the defender read melee off
		// something every hit carries, or if `ApplyTypedSpec` added the tag
		// unconditionally.
		Effects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);

		TestFalse(TEXT("a critical strike that was not struck in melee applies "
					   "no Bleeding"),
			Defender.IsBleeding());
		TestEqual(TEXT("and leaves no debuff behind"),
			Debuffs::CountOn(Defender.AbilitySystem), 0);

		// AND THE SAME ROLL WITH THE MELEE TAG ON IT DOES.
		Effects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
						  MeleeSkillTags());
		TestTrue(TEXT("while the same blow struck in melee does"),
			Defender.IsBleeding());
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_MELEE_BLEED_TEST(FCataclysmNoNodeDoesNotBleedTest,
	"Cataclysm.MeleeBleed.AnAttackerWithoutTheNodeAppliesNothing")
{
	using namespace CataclysmMeleeBleedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedFighter Attacker(World);
		const FScopedFighter Defender(World);
		MakeAMutilator(Attacker);

		// WHERE EVERY CHARACTER IN THE GAME STARTS. The attribute is initialised
		// to zero in `UCataclysmCombatAttributeSet`, no class line states it,
		// and no affix grants it: one passive node is its only source. So this
		// is the reading for every character that has not spent a point on
		// Mutilation Mastery, which is all of them but one build.
		Attacker.SetBleedOnCritChance(0.0f);

		const CataclysmTestWorld::FScopedCritRoll AlwaysCrits(0.0f);
		Effects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
						  MeleeSkillTags());

		TestFalse(TEXT("a melee critical strike from a character without the "
					   "node applies no Bleeding"),
			Defender.IsBleeding());
		TestEqual(TEXT("and leaves no debuff behind"),
			Debuffs::CountOn(Defender.AbilitySystem), 0);

		// AND THE CHANCE IS WHAT DECIDED IT. Giving the same attacker the node's
		// full chance and repeating the same blow bleeds the same defender, so
		// the reading above is the chance and not the harness.
		Attacker.SetBleedOnCritChance(100.0f);
		Effects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
						  MeleeSkillTags());
		TestTrue(TEXT("while the same attacker holding the node does"),
			Defender.IsBleeding());
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_MELEE_BLEED_TEST(FCataclysmShieldedCritDoesNotBleedTest,
	"Cataclysm.MeleeBleed.ABlowAnEnergyShieldSwallowedWholeAppliesNothing")
{
	using namespace CataclysmMeleeBleedTest;

	// THE THIRD CONDITION IS THAT THE BLOW REACHED HEALTH, and an energy shield
	// large enough to absorb the whole hit is the real case that fails it. Step
	// 8 of `UCataclysmDamageCalculation::Resolve` takes the shield off before
	// health takes the remainder, so `DealtToHealth` is zero and the character
	// was not wounded at all -- which is the reading the node's own sentence
	// implies and, more practically, is what stops a defender being made to
	// bleed by a blow that did not break its shield.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedFighter Attacker(World);
		const FScopedFighter Defender(World);
		MakeAMutilator(Attacker);

		// THE MAXIMUM FIRST AND THE CURRENT SECOND. The vital attribute set
		// clamps the shield to its maximum, so writing the current one first
		// would be clamped straight back to zero and this test would pass for
		// the wrong reason.
		//
		// 100,000 AGAINST A 1,500 CRITICAL STRIKE, so there is no arithmetic to
		// get wrong: 1,000 weapon damage at 150% leaves the shield barely
		// dented and health untouched.
		Defender.AbilitySystem->SetNumericAttributeBase(
			Vital::GetMaxEnergyShieldAttribute(), 100'000.0f);
		Defender.AbilitySystem->SetNumericAttributeBase(
			Vital::GetEnergyShieldAttribute(), 100'000.0f);

		const float HealthBefore = Defender.AbilitySystem->GetNumericAttribute(
			Vital::GetHealthAttribute());

		const CataclysmTestWorld::FScopedCritRoll AlwaysCrits(0.0f);
		Effects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
						  MeleeSkillTags());

		// THE PREMISE, ASSERTED RATHER THAN ASSUMED. Without this the test would
		// pass against a build where the shield did nothing and the blow simply
		// missed for some other reason.
		TestEqual(TEXT("the shield took the whole blow and health lost nothing"),
			Defender.AbilitySystem->GetNumericAttribute(
				Vital::GetHealthAttribute()),
			HealthBefore, 0.01f);

		TestFalse(TEXT("so the melee critical strike applies no Bleeding"),
			Defender.IsBleeding());
	}

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// What is applied, and what the stat will hold
// ---------------------------------------------------------------------------

CATACLYSM_MELEE_BLEED_TEST(FCataclysmBleedNumbersAreDesignedTest,
	"Cataclysm.MeleeBleed.TheBleedingAppliedIsTheOneTheSheetDesigned")
{
	using namespace CataclysmMeleeBleedTest;

	// THE NODE STATES NEITHER A MAGNITUDE NOR A DURATION. "Your melee critical
	// strikes have a 5% chance per point to apply Bleeding" says only which
	// effect, so both numbers have to come from the `DoT_Bleed` row of
	// `game/Data/StatusEffects.csv` rather than being invented in C++.
	//
	// READ OFF THE ROW RATHER THAN WRITTEN HERE, so re-tuning Bleed does not
	// break this test. Only that the reading works is being checked, plus the
	// two figures the row states today, which are quoted so a change to them is
	// visible in a diff rather than silent.
	const FCataclysmStatusEffectNumbers Bleed = Effects::BleedNumbers();

	if (!TestTrue(TEXT("Bleed states a duration and an amount"), Bleed.bUsable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	TestEqual(TEXT("Bleed runs for five seconds"), Bleed.DurationSeconds, 5.0f,
			  0.001f);
	TestEqual(TEXT("and deals a flat twenty a tick"), Bleed.FlatDamagePerTick,
			  20.0f, 0.001f);

	// AND BURN IS STILL READ THE SAME WAY. `BurnNumbers` and `BleedNumbers` are
	// now one function with two callers rather than two copies, so a change made
	// for Bleed reaches Burn. This is the assertion that would notice.
	const FCataclysmStatusEffectNumbers Burn = Effects::BurnNumbers();
	TestTrue(TEXT("and Burn still reads its own row through the shared reader"),
		Burn.bUsable);
	TestTrue(TEXT("which is a different row, with its own duration"),
		!FMath::IsNearlyEqual(Burn.DurationSeconds, Bleed.DurationSeconds)
			|| !FMath::IsNearlyEqual(Burn.FlatDamagePerTick,
									 Bleed.FlatDamagePerTick));

	return true;
}

CATACLYSM_MELEE_BLEED_TEST(FCataclysmBleedChanceIsClampedTest,
	"Cataclysm.MeleeBleed.TheChanceIsClampedToNoughtAndAHundred")
{
	using namespace CataclysmMeleeBleedTest;

	// A CHANCE ABOVE 100 IS NOT A LARGER CHANCE AND A NEGATIVE ONE IS NOT A
	// SMALLER CHANCE; both are data that means nothing. The clamp says so once,
	// where the cooldown skip chance is clamped, rather than at the roll.
	//
	// NO NODE REACHES EITHER END TODAY -- Mutilation Mastery stops at 40 -- so
	// this is a guard against a future source rather than a live case, and it is
	// asserted for the same reason the cooldown skip chance's is.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedFighter Character(World);

		TestEqual(TEXT("every character starts at no chance at all"),
			Character.BleedOnCritChance(), 0.0f, 0.001f);

		Character.SetBleedOnCritChance(150.0f);
		TestEqual(TEXT("a chance above a hundred is held at a hundred"),
			Character.BleedOnCritChance(), 100.0f, 0.001f);

		Character.SetBleedOnCritChance(-25.0f);
		TestEqual(TEXT("and a negative one is held at zero"),
			Character.BleedOnCritChance(), 0.0f, 0.001f);

		// AND A FIGURE INSIDE THE RANGE IS LEFT ALONE, which is what says the
		// clamp is a clamp rather than something that pins every write. 40 is
		// the node at its eight points.
		Character.SetBleedOnCritChance(40.0f);
		TestEqual(TEXT("while the node's own forty is untouched"),
			Character.BleedOnCritChance(), 40.0f, 0.001f);
	}

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
