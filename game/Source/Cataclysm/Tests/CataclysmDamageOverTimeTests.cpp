// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * The three damage over time stats. Issue #895.
 *
 * WHAT WAS WRONG, AND IT WAS TWO THINGS. `DotDamage`, `DotFrequency` and
 * `DotDuration` all existed as attributes, were clamped, were replicated, and no
 * code in the project read any of the three, so the three affixes granting them
 * were worth nothing.
 *
 * AND THE ENGINE READ A STATED NUMBER THE WRONG WAY ROUND.
 * `UCataclysmSkillEffects::ApplyBurn` treated Burn's stated 20% of the hit as
 * the TOTAL across its four seconds. The design says the opposite: "A damage
 * over time effect deals a fixed amount per tick. It is not a total handed out
 * in instalments." Under the total reading, raising the tick rate divides the
 * same total into more, smaller ticks and adds nothing at all, so one of the
 * three stats could not have been worth anything even once it was read. The
 * project owner chose the per-tick reading on 2026-08-24; docs/DECISIONS.md
 * carries the reasoning.
 *
 * THAT 20% OF THE HIT IS HISTORY AND NOT BURN'S CURRENT NUMBER. Later the same
 * day the owner moved every ailment from a percent of the hit to a flat amount
 * per tick, and Burn became 25 a second for four seconds. The per-tick rule
 * above is unchanged and is what these tests check; only the base moved.
 */
namespace CataclysmDamageOverTimeTest
{
	/**
	 * A character whose three damage over time stats can be set.
	 *
	 * Named apart from the harnesses in the neighbouring test files on purpose:
	 * the Unreal unity build concatenates these translation units, so two
	 * structs of one name in two files compile until both are clean and then
	 * collide.
	 */
	struct FDotCharacter
	{
		explicit FDotCharacter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// A raw pointer on purpose: AddAttributeSetSubobject is a template
			// and a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmVitalAttributeSet>(Actor));

			Combat = NewCombat;
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			// THE BASELINE IS A HUNDRED FOR ALL THREE, which is what the design
			// gives them and what game/Data/ClassStats.csv states on the shared
			// Default line. The attribute set starts them there too, and this
			// says so rather than relying on it.
			Combat->SetDotDamage(100.0f);
			Combat->SetDotFrequency(100.0f);
			Combat->SetDotDuration(100.0f);
		}

		~FDotCharacter()
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
}

#define CATACLYSM_DOT_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_DOT_TEST(FCataclysmDotBaselineTest,
	"Cataclysm.DamageOverTime.ACharacterWithNoneOfTheThreeStatsChangesNothing")
{
	using namespace CataclysmDamageOverTimeTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// A NULL SOURCE IS THE UNSCALED CASE, and it is how a blow dealt in someone
	// else's name declines all three.
	const FCataclysmDamageOverTimeNumbers Plain =
		UCataclysmSkillEffects::DamageOverTimeNumbers(
			nullptr, /*DamagePerTick=*/20.0f, /*DurationSeconds=*/4.0f);

	TestTrue(TEXT("it is usable"), Plain.bUsable);
	TestEqual(TEXT("one tick deals what was asked"), Plain.DamagePerTick, 20.0f, 0.001f);
	TestEqual(TEXT("ticks are a second apart"), Plain.SecondsPerTick, 1.0f, 0.001f);
	TestEqual(TEXT("it runs for as long as was asked"),
		Plain.DurationSeconds, 4.0f, 0.001f);
	TestEqual(TEXT("which is four ticks"), Plain.Ticks, 4.0f, 0.001f);

	// EIGHTY AND NOT TWENTY, which is the whole change. A fixed amount per tick
	// for four ticks is four times what one tick deals; a total handed out in
	// instalments would be twenty.
	TestEqual(TEXT("and eighty damage altogether, not twenty"),
		Plain.TotalDamage, 80.0f, 0.001f);

	// A CHARACTER SITTING ON THE BASELINE MATCHES THE UNSCALED CASE EXACTLY.
	// Without this, a baseline read as zero rather than as a hundred would look
	// like the stats working when it is the effect being deleted.
	FDotCharacter Sitting(World);
	const FCataclysmDamageOverTimeNumbers Baseline =
		UCataclysmSkillEffects::DamageOverTimeNumbers(
			Sitting.AbilitySystem, 20.0f, 4.0f);

	TestEqual(TEXT("a character at the 100 baseline changes nothing"),
		Baseline.TotalDamage, Plain.TotalDamage, 0.001f);

	return true;
}

CATACLYSM_DOT_TEST(FCataclysmDotThreeStatsTest,
	"Cataclysm.DamageOverTime.AllThreeStatsRaiseItAndAllThreeMultiply")
{
	using namespace CataclysmDamageOverTimeTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDotCharacter Character(World);

	const auto Numbers = [&Character]
	{
		return UCataclysmSkillEffects::DamageOverTimeNumbers(
			Character.AbilitySystem, /*DamagePerTick=*/20.0f,
			/*DurationSeconds=*/4.0f);
	};

	const float Baseline = Numbers().TotalDamage;

	// EACH ONE ALONE RAISES THE TOTAL, AND EACH RAISES A DIFFERENT NUMBER.
	// Checking only the total would pass if all three were wired to the same
	// thing, which is the mistake worth catching.
	Character.Combat->SetDotDamage(148.0f);
	TestEqual(TEXT("damage over time raises what one tick deals"),
		Numbers().DamagePerTick, 20.0f * 1.48f, 0.001f);
	TestEqual(TEXT("and leaves the gap between ticks alone"),
		Numbers().SecondsPerTick, 1.0f, 0.001f);
	Character.Combat->SetDotDamage(100.0f);

	Character.Combat->SetDotFrequency(148.0f);
	TestEqual(TEXT("frequency shortens the gap between ticks"),
		Numbers().SecondsPerTick, 1.0f / 1.48f, 0.001f);
	TestEqual(TEXT("and leaves what one tick deals alone"),
		Numbers().DamagePerTick, 20.0f, 0.001f);
	Character.Combat->SetDotFrequency(100.0f);

	Character.Combat->SetDotDuration(148.0f);
	TestEqual(TEXT("duration makes it run longer"),
		Numbers().DurationSeconds, 4.0f * 1.48f, 0.001f);
	Character.Combat->SetDotDuration(100.0f);

	// AND ALL THREE TOGETHER MULTIPLY, which is the design's own worked example
	// and the whole reason there are three stats rather than one: "A character
	// with 48% more of each does not deal 148% of the base total; it deals
	// 1.48 x 1.48 x 1.48, which is 324%."
	Character.Combat->SetDotDamage(148.0f);
	Character.Combat->SetDotFrequency(148.0f);
	Character.Combat->SetDotDuration(148.0f);

	TestEqual(TEXT("48% more of each is 324% of the total, not 148%"),
		Numbers().TotalDamage, Baseline * 1.48f * 1.48f * 1.48f, 0.01f);

	// STATED AS A RATIO AS WELL, so a reader can see the 3.24 without doing the
	// arithmetic, and so this fails loudly if the three ever stop multiplying.
	TestEqual(TEXT("which is 3.24 times"),
		Numbers().TotalDamage / Baseline, 3.24f, 0.01f);

	return true;
}

CATACLYSM_DOT_TEST(FCataclysmBurnIsPerTickTest,
	"Cataclysm.DamageOverTime.BurnsStatedAmountIsWhatOneTickDeals")
{
	using namespace CataclysmDamageOverTimeTest;

	const FCataclysmStatusEffectNumbers Burn = UCataclysmSkillEffects::BurnNumbers();
	if (!TestTrue(TEXT("Burn states a duration and an amount"), Burn.bUsable))
	{
		return false;
	}

	// READ OFF THE EFFECT TABLE RATHER THAN WRITTEN HERE, so re-tuning Burn does
	// not break this test. Only the reading of the number is being checked.
	//
	// ASKED THROUGH DamagePerTickAgainst RATHER THAN OF ONE COLUMN, so it holds
	// whichever base Burn states. It was a percent of the hit until 2026-08-24
	// and is a flat amount since, and this computed `100 * PercentOfHit / 100`,
	// which now reads zero and would fail on a working burn. The 100 stands for
	// a 100 damage hit and is what makes the percent-of-hit reading legible.
	const float PerTick = Burn.DamagePerTickAgainst(100.0f);
	const FCataclysmDamageOverTimeNumbers Numbers =
		UCataclysmSkillEffects::DamageOverTimeNumbers(
			nullptr, PerTick, Burn.DurationSeconds);

	TestEqual(TEXT("one tick of a burn from a 100 damage hit is the stated amount"),
		Numbers.DamagePerTick, PerTick, 0.001f);

	// AND THE TOTAL IS THAT AMOUNT ONCE PER SECOND, so it is the duration times
	// the per-tick figure rather than the per-tick figure by itself. With Burn
	// at 4 seconds and 25 that is 100 rather than 25.
	TestEqual(TEXT("and the total is one tick's worth for every second it runs"),
		Numbers.TotalDamage, PerTick * Burn.DurationSeconds, 0.01f);

	TestTrue(FString::Printf(
		TEXT("which is more than one tick alone: %.1f against %.1f"),
		Numbers.TotalDamage, PerTick),
		Numbers.TotalDamage > PerTick + 0.01f);

	return true;
}

CATACLYSM_DOT_TEST(FCataclysmDotRefusalsTest,
	"Cataclysm.DamageOverTime.NothingIsAppliedWhenNothingCanBe")
{
	using namespace CataclysmDamageOverTimeTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	TestFalse(TEXT("a tick that deals nothing is not applied"),
		UCataclysmSkillEffects::DamageOverTimeNumbers(nullptr, 0.0f, 4.0f).bUsable);
	TestFalse(TEXT("and neither is one that lasts no time"),
		UCataclysmSkillEffects::DamageOverTimeNumbers(nullptr, 20.0f, 0.0f).bUsable);

	// A FREQUENCY OF ZERO WOULD BE A DIVISION BY ZERO. Nothing in the game can
	// produce one -- the class line gives 100 and every source is an increase --
	// but the refusal is what makes that safe rather than lucky.
	FDotCharacter Stopped(World);
	Stopped.Combat->SetDotFrequency(0.0f);
	TestFalse(TEXT("a frequency of zero applies nothing rather than dividing by it"),
		UCataclysmSkillEffects::DamageOverTimeNumbers(
			Stopped.AbilitySystem, 20.0f, 4.0f).bUsable);

	return true;
}

CATACLYSM_DOT_TEST(FCataclysmDotMultiplierTest,
	"Cataclysm.DamageOverTime.AHundredMeansUnchangedAndNoAttributeMeansUnchanged")
{
	using namespace CataclysmDamageOverTimeTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayAttribute DotDamage =
		UCataclysmCombatAttributeSet::GetDotDamageAttribute();

	TestEqual(TEXT("nothing at all reads as unchanged"),
		UCataclysmSkillEffects::AsMultiplier(nullptr, DotDamage), 1.0f, 0.001f);

	FDotCharacter Character(World);
	TestEqual(TEXT("a hundred reads as unchanged"),
		UCataclysmSkillEffects::AsMultiplier(Character.AbilitySystem, DotDamage),
		1.0f, 0.001f);

	Character.Combat->SetDotDamage(148.0f);
	TestEqual(TEXT("and 148 reads as 1.48"),
		UCataclysmSkillEffects::AsMultiplier(Character.AbilitySystem, DotDamage),
		1.48f, 0.001f);

	// A NEGATIVE IS NOT A SMALLER EFFECT, it is one that cannot be applied, so
	// it is floored at zero and refused above rather than run backwards.
	Character.Combat->SetDotDamage(-50.0f);
	TestEqual(TEXT("and a negative reads as nothing rather than as backwards"),
		UCataclysmSkillEffects::AsMultiplier(Character.AbilitySystem, DotDamage),
		0.0f, 0.001f);

	return true;
}

CATACLYSM_DOT_TEST(FCataclysmDotMinionTest,
	"Cataclysm.DamageOverTime.ABlowDealtInSomeoneElsesNameTakesNoneOfTheirs")
{
	using namespace CataclysmDamageOverTimeTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// A MINION'S BURN IS APPLIED WITH ITS SUMMONER AS THE INSTIGATOR, and the
	// design names damage over time among what a minion does not take from its
	// summoner. ACataclysmMinion asks for the unscaled figures by passing a null
	// source, which is what this checks the effect of.
	FDotCharacter Summoner(World);
	Summoner.Combat->SetDotDamage(300.0f);
	Summoner.Combat->SetDotFrequency(300.0f);
	Summoner.Combat->SetDotDuration(300.0f);

	const FCataclysmDamageOverTimeNumbers Own =
		UCataclysmSkillEffects::DamageOverTimeNumbers(
			Summoner.AbilitySystem, 20.0f, 4.0f);
	const FCataclysmDamageOverTimeNumbers Minions =
		UCataclysmSkillEffects::DamageOverTimeNumbers(nullptr, 20.0f, 4.0f);

	// THE SUMMONER'S OWN BURN DOES TAKE THEM, which is what makes the check
	// below evidence of the exclusion rather than of the stats being dead.
	TestTrue(FString::Printf(
		TEXT("the summoner's own burn is scaled: %.1f against %.1f"),
		Own.TotalDamage, Minions.TotalDamage),
		Own.TotalDamage > Minions.TotalDamage + 0.01f);

	TestEqual(TEXT("and a blow dealt in the summoner's name takes none of it"),
		Minions.TotalDamage, 80.0f, 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
