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
#include "GameplayTagsManager.h"
#include "Misc/OutputDeviceRedirector.h"
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


/**
 * THE WARNING THAT FIRED 16,033 TIMES. Issue #1516.
 *
 * `StatusEffectNumbers` warned whenever `bUsable` was false, and `bUsable` asks
 * only whether a row is DAMAGE OVER TIME. Five rows carry a `Strength` instead
 * and are applied by `ApplyNamedEffect`, so all five were accused, on every
 * application, of lacking damage they were never written to carry. In the Horde
 * session of 2026-09-09 that was 16,033 of 28,693 log lines -- 56% -- of which
 * Shred alone wrote 12,284.
 *
 * BOTH DIRECTIONS ARE CHECKED AND THE SECOND IS THE ONE THAT MATTERS. Deleting
 * the warning outright would have passed the first test on its own, so the pair
 * is the point: a row carrying a strength is quiet, and a row carrying neither a
 * strength nor a per-tick amount still warns.
 */
namespace CataclysmStatusEffectWarningTest
{
	/**
	 * Everything written to the log while this is alive.
	 *
	 * MODELLED ON `FScopedLogCapture` IN `CataclysmWeaponSlotsTests.cpp`, and
	 * for the same reason: a warning does not fail an automation test, because
	 * `FAutomationTestBase::bElevateLogWarningsToErrors` is false. Reading the
	 * log is the only way to see whether this one was written at all, so
	 * `AddExpectedError` is no use here in either direction.
	 *
	 * NOTHING IS FILTERED IN `Serialize`, AND THAT ORDER IS DELIBERATE.
	 * `FOutputDeviceRedirector` buffers lines, so they have to be flushed
	 * through before anything is counted; deciding on the way in would judge
	 * lines that had not arrived yet.
	 */
	struct FScopedWarningCapture : public FOutputDevice
	{
		struct FCapturedLine
		{
			FString Text;
			ELogVerbosity::Type Verbosity = ELogVerbosity::NoLogging;
			FName Category;
		};

		FScopedWarningCapture()
		{
			if (GLog)
			{
				GLog->AddOutputDevice(this);
			}
		}

		virtual ~FScopedWarningCapture()
		{
			if (GLog)
			{
				GLog->RemoveOutputDevice(this);
			}
		}

		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity,
							   const class FName& Category) override
		{
			Lines.Add({ FString(Text), Verbosity, Category });
		}

		/**
		 * Every LogCataclysm line at exactly Warning verbosity holding Needle.
		 *
		 * CASE-SENSITIVE, because `FString::Contains` is not by default and a
		 * check that ignored case would pass on text this warning never writes.
		 */
		TArray<FString> CataclysmWarningsContaining(const TCHAR* Needle)
		{
			if (GLog)
			{
				GLog->Flush();
			}

			TArray<FString> Found;
			for (const FCapturedLine& Line : Lines)
			{
				if (Line.Category == FName(TEXT("LogCataclysm"))
					&& Line.Verbosity == ELogVerbosity::Warning
					&& Line.Text.Contains(Needle, ESearchCase::CaseSensitive))
				{
					Found.Add(Line.Text);
				}
			}
			return Found;
		}

		TArray<FCapturedLine> Lines;
	};

	/** The clause the unusable-row warning is built from. */
	const TCHAR* WarningNeedle =
		TEXT("must be above zero or nothing is applied");

	/**
	 * Requested by name rather than declared, matching `UCataclysmTeams` and for
	 * the same reason: a native declaration would create the tag whether or not
	 * the design still listed it.
	 */
	FGameplayTag TagNamed(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(
			FName(Name), /*ErrorIfNotFound=*/false);
	}
}

CATACLYSM_DOT_TEST(FCataclysmStrengthRowIsQuietTest,
	"Cataclysm.DamageOverTime.ARowStatingAStrengthDoesNotWarnAboutDamageItNeverCarried")
{
	using namespace CataclysmStatusEffectWarningTest;

	const FGameplayTag Shred = TagNamed(TEXT("Status.Debuff.Shred"));
	if (!TestTrue(TEXT("Status.Debuff.Shred is a gameplay tag"), Shred.IsValid()))
	{
		return false;
	}

	FCataclysmStatusEffectNumbers Numbers;
	TArray<FString> Warned;
	{
		FScopedWarningCapture Capture;
		Numbers = UCataclysmSkillEffects::NumbersForEffectTag(Shred);
		Warned = Capture.CataclysmWarningsContaining(WarningNeedle);
	}

	// THE PRECONDITION, AND WITHOUT IT THIS TEST QUIETLY STOPS COVERING ITS OWN
	// PATH. Give Shred a flat amount per tick and the row becomes usable, no
	// warning is written for a reason that has nothing to do with issue #1516,
	// and the check below still passes. These three say the row is still the
	// shape this test is about.
	TestEqual(TEXT("Shred states a strength"), Numbers.Strength, 10.0f, 0.001f);
	TestEqual(TEXT("and no flat amount per tick"),
		Numbers.FlatDamagePerTick, 0.0f, 0.001f);
	TestEqual(TEXT("and no percent of the hit"),
		Numbers.PercentOfHit, 0.0f, 0.001f);

	TestEqual(FString::Printf(
		TEXT("reading it writes no unusable-row warning, and wrote %d: %s"),
		Warned.Num(), Warned.Num() > 0 ? *Warned[0] : TEXT("")),
		Warned.Num(), 0);

	// AND IT IS STILL NOT DAMAGE OVER TIME. `bUsable` gates four callers that
	// decide whether to apply a per-tick effect -- CataclysmContagion,
	// CataclysmSkillTemplate, CataclysmVitalAttributeSet and ApplyBurn -- and a
	// fix that quietened the warning by flipping this flag instead would have
	// started applying Shred as a damage over time worth zero a tick.
	TestFalse(TEXT("and is still not usable as damage over time"),
		Numbers.bUsable);

	return true;
}

CATACLYSM_DOT_TEST(FCataclysmTagOnlyRowIsQuietTest,
	"Cataclysm.DamageOverTime.ARowThatIsOnlyItsTagDoesNotWarn")
{
	using namespace CataclysmStatusEffectWarningTest;

	// MADNESS STATES A DURATION AND NOTHING ELSE, and that is a whole row: its
	// tag is the effect, which `UCataclysmTeams` reads. Issue #899. The chance to
	// madden reads this row on every blow that lands it, so the warning used to
	// fire on each of them. The test after this one is why a row stating no
	// duration still warns.
	const FGameplayTag Madness = TagNamed(TEXT("Status.Debuff.Madness"));
	if (!TestTrue(TEXT("Status.Debuff.Madness is a gameplay tag"),
				  Madness.IsValid()))
	{
		return false;
	}

	FCataclysmStatusEffectNumbers Numbers;
	TArray<FString> Warned;
	{
		FScopedWarningCapture Capture;
		Numbers = UCataclysmSkillEffects::NumbersForEffectTag(Madness);
		Warned = Capture.CataclysmWarningsContaining(WarningNeedle);
	}

	// THE PRECONDITION: the row is still the shape this test is about, a
	// duration with no strength and no amount.
	TestEqual(TEXT("Madness lasts three seconds"), Numbers.DurationSeconds,
		3.0f, 0.001f);
	TestEqual(TEXT("and states no strength"), Numbers.Strength, 0.0f, 0.001f);
	TestEqual(TEXT("and no flat amount per tick"),
		Numbers.FlatDamagePerTick, 0.0f, 0.001f);

	TestEqual(FString::Printf(
		TEXT("reading it writes no unusable-row warning, and wrote %d: %s"),
		Warned.Num(), Warned.Num() > 0 ? *Warned[0] : TEXT("")),
		Warned.Num(), 0);

	// AND IT IS STILL NOT DAMAGE OVER TIME, for the reason the Shred test above
	// gives.
	TestFalse(TEXT("and is still not usable as damage over time"),
		Numbers.bUsable);

	return true;
}

CATACLYSM_DOT_TEST(FCataclysmEmptyRowStillWarnsTest,
	"Cataclysm.DamageOverTime.ARowStatingNeitherAStrengthNorAnAmountStillWarns")
{
	using namespace CataclysmStatusEffectWarningTest;

	// NECROTIC FOG STATES NOTHING AT ALL -- no duration, no strength, no amount
	// -- which is exactly the row this guard exists for. Burn was in that state
	// until issue #895 and nothing reported it. A row that is only its tag, such
	// as Madness, has been quiet since issue #899, and the test above says so.
	const FGameplayTag Fog = TagNamed(TEXT("Status.DoT.NecroticFog"));
	if (!TestTrue(TEXT("Status.DoT.NecroticFog is a gameplay tag"), Fog.IsValid()))
	{
		return false;
	}

	FCataclysmStatusEffectNumbers Numbers;
	TArray<FString> Warned;
	{
		FScopedWarningCapture Capture;
		Numbers = UCataclysmSkillEffects::NumbersForEffectTag(Fog);
		Warned = Capture.CataclysmWarningsContaining(WarningNeedle);
	}

	// THE PRECONDITION AGAIN, and here it is what stops the test passing for the
	// wrong reason. Give this row a strength and it should fall silent; the test
	// would then be asserting that a quiet row is loud, and would fail honestly
	// rather than check nothing.
	TestEqual(TEXT("Necrotic Fog states no strength"),
		Numbers.Strength, 0.0f, 0.001f);
	TestEqual(TEXT("no flat amount per tick"),
		Numbers.FlatDamagePerTick, 0.0f, 0.001f);
	TestEqual(TEXT("and no percent of the hit"),
		Numbers.PercentOfHit, 0.0f, 0.001f);

	TestTrue(TEXT("a row stating none of the three still warns"),
		Warned.Num() > 0);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
