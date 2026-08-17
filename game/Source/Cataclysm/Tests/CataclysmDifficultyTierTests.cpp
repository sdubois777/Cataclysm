// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmGameMode.h"

/**
 * Which difficulty tier a hit resolves at. Issue #514.
 *
 * WHAT THE TIER DECIDES, AND IT IS ONLY ONE THING: what armour is worth.
 * `UCataclysmDamageCalculation::ArmorReduction` is `armor / (armor + 800 x
 * tier)` capped at 75%, so the same armour removes far more of a hit at tier 1
 * than at tier 8. Nothing else in the damage calculation reads it.
 *
 * WHAT WAS WRONG. `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` is
 * the only place in the running game that resolves an incoming hit, and it
 * passed a literal 1 because nothing in the project held a tier at all. The
 * Abyssal Warden's designed armour of 5,954 removes 88.2% of a hit at tier 1 --
 * capped to 75% -- against 48.19% at tier 8, so every armoured thing in the game
 * was 2.07 times harder to hurt than the simulation says it should be.
 *
 * THE LAST TEST HERE IS THE ONE THAT MATTERS. The others check that
 * `ACataclysmGameMode::DifficultyTierFor` answers correctly; that one drives a
 * real hit through the real attribute set and reads the health lost, which is
 * the only thing that shows the answer reaches the arithmetic.
 *
 * ONE LINE IS NOT COVERED HERE AND IT IS NAMED RATHER THAN HIDDEN: finding the
 * game mode inside `DifficultyTierIn`. A world built by an automation test has
 * no authority game mode and cannot be given one -- `UWorld::AuthorityGameMode`
 * is private, and `UWorld::SetGameMode` on a world with no game instance
 * crashes, which is measured rather than assumed: it did, on 2026-08-16, taking
 * the whole test run down with it. That is why the rule was split out into
 * `DifficultyTierFor`, which is checked below in full.
 */

namespace CataclysmDifficultyTierTest
{
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}

	/** The console variable, so a test can set it and put it back. */
	static IConsoleVariable* TierVariable()
	{
		return IConsoleManager::Get().FindConsoleVariable(
			TEXT("Cataclysm.DifficultyTier"));
	}

	/**
	 * The Abyssal Warden's designed armour at difficulty tier 8.
	 *
	 * QUOTED FROM ISSUE #514, and chosen because it belongs to the most armoured
	 * creature in the vertical slice: the figure where the difference between the
	 * tiers is largest and hardest to mistake for noise.
	 */
	constexpr float WardenArmorAtTierEight = 5954.0f;
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------
// Which tier is answered, and by what
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmTierWithNoGameModeTest,
	"Cataclysm.Difficulty.NoGameModeAnswersTheLowestTier")
{
	using namespace CataclysmDifficultyTierTest;

	// THE BEHAVIOUR THAT KEEPS EVERY OTHER TEST UNCHANGED. Every automation test
	// builds a bare world with no game mode, and a hit resolved there still has
	// to produce a number. Tier 1 is what the hard-coded line used to give, so
	// nothing that does not care about the tier is affected by reading one.
	IConsoleVariable* Variable = TierVariable();
	if (!TestNotNull(TEXT("the Cataclysm.DifficultyTier console variable"),
					 Variable))
	{
		return false;
	}
	const int32 Was = Variable->GetInt();
	ON_SCOPE_EXIT { Variable->Set(Was); };
	Variable->Set(0);

	TestEqual(TEXT("no game mode answers the lowest tier"),
		ACataclysmGameMode::DifficultyTierFor(nullptr),
		ACataclysmGameMode::LowestDifficultyTier);

	TestEqual(TEXT("and so does a world that cannot find one"),
		ACataclysmGameMode::DifficultyTierIn(nullptr),
		ACataclysmGameMode::LowestDifficultyTier);
	return true;
}

CATACLYSM_TEST(FCataclysmTierFromTheGameModeTest,
	"Cataclysm.Difficulty.TheGameModesOwnTierIsWhatIsAnswered")
{
	using namespace CataclysmDifficultyTierTest;

	IConsoleVariable* Variable = TierVariable();
	if (!TestNotNull(TEXT("the Cataclysm.DifficultyTier console variable"),
					 Variable))
	{
		return false;
	}
	const int32 Was = Variable->GetInt();
	ON_SCOPE_EXIT { Variable->Set(Was); };
	Variable->Set(0);

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmGameMode* Mode = World->SpawnActor<ACataclysmGameMode>();
	if (!Mode)
	{
		AddError(TEXT("could not spawn the game mode"));
		return false;
	}

	// THE SANDBOX IS WHERE A NEW CHARACTER STANDS, so tier 1 is the start of the
	// game rather than a placeholder.
	TestEqual(TEXT("its default is the lowest tier"),
		Mode->DifficultyTier, ACataclysmGameMode::LowestDifficultyTier);

	Mode->DifficultyTier = 6;
	TestEqual(TEXT("and whatever it says is what is answered"),
		ACataclysmGameMode::DifficultyTierFor(Mode), 6);

	// CLAMPED RATHER THAN TRUSTED, in both directions. The property is clamped in
	// the editor by its own metadata; this is what happens when it is set from
	// C++, which the editor's clamp never sees.
	Mode->DifficultyTier = 99;
	TestEqual(TEXT("above the highest tier is clamped to it"),
		ACataclysmGameMode::DifficultyTierFor(Mode),
		ACataclysmGameMode::HighestDifficultyTier);

	Mode->DifficultyTier = 0;
	TestEqual(TEXT("below the lowest is clamped to it"),
		ACataclysmGameMode::DifficultyTierFor(Mode),
		ACataclysmGameMode::LowestDifficultyTier);
	return true;
}

CATACLYSM_TEST(FCataclysmTierConsoleOverrideTest,
	"Cataclysm.Difficulty.TheConsoleVariableBeatsTheGameMode")
{
	using namespace CataclysmDifficultyTierTest;

	IConsoleVariable* Variable = TierVariable();
	if (!TestNotNull(TEXT("the Cataclysm.DifficultyTier console variable"),
					 Variable))
	{
		return false;
	}
	const int32 Was = Variable->GetInt();
	ON_SCOPE_EXIT { Variable->Set(Was); };

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmGameMode* Mode = World->SpawnActor<ACataclysmGameMode>();
	if (!Mode)
	{
		AddError(TEXT("could not spawn the game mode"));
		return false;
	}
	Mode->DifficultyTier = 2;

	// ZERO MEANS "USE THE GAME MODE'S", so the console variable is a sentinel
	// rather than a second copy of the default.
	Variable->Set(0);
	TestEqual(TEXT("zero defers to the game mode"),
		ACataclysmGameMode::DifficultyTierFor(Mode), 2);

	Variable->Set(8);
	TestEqual(TEXT("and a set value beats it"),
		ACataclysmGameMode::DifficultyTierFor(Mode), 8);

	// A console variable is typed by hand while playing, and a tier of 40 would
	// make armour worth almost nothing without saying so.
	Variable->Set(40);
	TestEqual(TEXT("above the highest tier is clamped to it"),
		ACataclysmGameMode::DifficultyTierFor(Mode),
		ACataclysmGameMode::HighestDifficultyTier);

	Variable->Set(-3);
	TestEqual(TEXT("a negative reads as unset, so the game mode answers"),
		ACataclysmGameMode::DifficultyTierFor(Mode), 2);
	return true;
}

CATACLYSM_TEST(FCataclysmTierRangeTest,
	"Cataclysm.Difficulty.TheTierRangeIsOneToEight")
{
	// EIGHT, AND IT IS NOT A NUMBER CHOSEN HERE. `DIFFICULTY_TIERS` in
	// `sim/cataclysm_sim/affixes.py` is 8, and
	// `tools/tests/test_difficulty_tier_matches_the_model.py` holds this header
	// to it -- which is the check that really runs, because continuous
	// integration compiles no C++.
	TestEqual(TEXT("the lowest tier is one"),
		ACataclysmGameMode::LowestDifficultyTier, 1);
	TestEqual(TEXT("and the highest is eight"),
		ACataclysmGameMode::HighestDifficultyTier, 8);
	return true;
}

// --------------------------------------------------------------------------
// That the tier reaches a real hit, which is the whole point
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmTierReachesARealHitTest,
	"Cataclysm.Difficulty.AnArmouredDefenderTakesMoreDamageAtAHigherTier")
{
	using namespace CataclysmDifficultyTierTest;

	// THE DEFECT ITSELF. Everything above checks what the tier is; this checks
	// that it reaches the arithmetic, by driving a hit through
	// UCataclysmVitalAttributeSet::PostGameplayEffectExecute -- the only place in
	// the running game that resolves one -- and reading the health lost.
	IConsoleVariable* Variable = TierVariable();
	if (!TestNotNull(TEXT("the Cataclysm.DifficultyTier console variable"),
					 Variable))
	{
		return false;
	}
	const int32 Was = Variable->GetInt();
	ON_SCOPE_EXIT { Variable->Set(Was); };

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// BOTH SIDES ARE ENEMY CHARACTERS, because both need an ability system
	// component: UCataclysmSkillEffects::ApplyDirectDamage returns false and does
	// nothing when either side lacks one. A bare AActor was tried first and both
	// readings came back as zero, which looked exactly like the tier failing to
	// reach the arithmetic.
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

	// ARMOURED LIKE THE MOST ARMOURED CREATURE IN THE SLICE, and given health far
	// beyond what one hit can remove so no reading is clamped by the health left.
	Defender->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	Defender->SetHealth(10'000'000.0f);

	UAbilitySystemComponent* Abilities = Defender->GetAbilitySystemComponent();
	if (!Abilities)
	{
		AddError(TEXT("the defender has no ability system component"));
		return false;
	}
	Abilities->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetArmorAttribute(),
		WardenArmorAtTierEight);

	// EVASION AND BLOCK OFF, so the two readings differ by the armour step alone
	// rather than by a roll.
	Abilities->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
	Abilities->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);

	const auto HealthNow = [&]() -> float
	{
		return Abilities->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute());
	};

	const auto DamageTakenAtTier = [&](int32 Tier) -> float
	{
		Variable->Set(Tier);
		const float Before = HealthNow();
		UCataclysmSkillEffects::ApplyDirectDamage(Attacker, Defender, 10'000.0f);
		return Before - HealthNow();
	};

	const float AtTierOne = DamageTakenAtTier(1);
	const float AtTierEight = DamageTakenAtTier(8);

	// SOMETHING LANDED AT ALL, so a difference below is a difference rather than
	// two hits that both did nothing.
	TestTrue(FString::Printf(TEXT("a hit lands at tier 1: %.1f"), AtTierOne),
		AtTierOne > 0.0f);

	// AND MORE LANDS AT TIER 8, because the same armour is worth less there.
	TestTrue(FString::Printf(
			TEXT("more lands at tier 8 than at tier 1: %.1f against %.1f"),
			AtTierEight, AtTierOne),
		AtTierEight > AtTierOne);

	// THE SIZE OF THE DIFFERENCE, NOT ONLY ITS DIRECTION. Armour of 5,954 removes
	// 75% of a hit at tier 1 -- the cap -- and 48.19% at tier 8, so 25% lands
	// against 51.81%, which is 2.07 times as much. Checked as a band, because the
	// point is that the gap is the designed size rather than a rounding
	// difference somewhere in the order.
	const float Ratio = AtTierEight / FMath::Max(AtTierOne, 1.0f);
	TestTrue(FString::Printf(
			TEXT("the gap is about the designed 2.07 times: %.2f"), Ratio),
		Ratio > 1.9f && Ratio < 2.3f);

	// AND THE ARITHMETIC IT IS DERIVED FROM, so a failure above says which half
	// moved: the damage calculation's own answer for the same armour.
	TestEqual(TEXT("armour of 5,954 is capped at tier 1"),
		UCataclysmDamageCalculation::ArmorReduction(WardenArmorAtTierEight, 1),
		UCataclysmDamageCalculation::ArmorReductionCap);
	TestTrue(TEXT("and is worth about 48% at tier 8"),
		FMath::IsNearlyEqual(
			UCataclysmDamageCalculation::ArmorReduction(WardenArmorAtTierEight, 8),
			48.19f, 0.01f));
	return true;
}

#endif  // WITH_AUTOMATION_TESTS
