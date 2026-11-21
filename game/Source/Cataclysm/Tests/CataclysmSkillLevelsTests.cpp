// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmExperience.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmSkillLevels.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Dungeon/CataclysmEnemyScore.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmSkillBar.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A skill levels as it is used. Issue #2764, the shape ruled by the project owner on 2026-11-19 and its numbers by the
 * owner and the coordinating session the same day; `UCataclysmSkillLevels` holds them.
 *
 * WHAT IS CHECKED HERE: the price of a level against the character's curve and Path of Exile's paired levels; the cap
 * by the character's level, with the experience kept; the damage factor, 2.85 times at level 20; the bar's words; the
 * split of a kill by the health each skill removed, and `skill_experience_gain` raising it (issue #2803); a real
 * skill's blow through `UCataclysmSkillEffects::ApplyHit`, scaled by its level and credited to it, and the basic
 * attack's and a blow on the user's own self neither; a real
 * creature's death paying its skills; the character record's version 10 step and the save of each skill's experience;
 * and the skill bar's boxes. The PLAY LOOP line's words are `Cataclysm.PlayLoop.TheSkillLevelLineSaysTheSkillAndBothLevels`.
 *
 * Registered under `Cataclysm.SkillLevels.`, which no other group's name begins with. EVERY PRICE IS ASKED OF
 * `UCataclysmExperience`, not written here, so a re-tuned character curve moves these with it.
 */
namespace CataclysmSkillLevelsTest
{
	using Levels = UCataclysmSkillLevels;

	const FName MoltenCleave(TEXT("Demonic_Greataxe_Heavy")); // the starting Greataxe's Heavy, a designed skill

	/** A possessed player, as a kill finds it, with its state and its ability system. */
	struct FSkillLevelsPlayer
	{
		UWorld* World = nullptr;
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UAbilitySystemComponent* Abilities = nullptr;

		bool IsUsable() const { return World && State && Character && Abilities; }
	};

	/** The world, the player possessed, and its Greataxe's skills granted. */
	FSkillLevelsPlayer MakeSkillLevelsPlayer()
	{
		FSkillLevelsPlayer Out;
		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}
		Out.State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Character = Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Out.State && Controller && Out.Character)
		{
			Controller->SetPlayerState(Out.State);
			Controller->Possess(Out.Character);
			Out.Abilities = Out.Character->GetAbilitySystemComponent();
		}
		return Out;
	}

	/** The player's skill template whose row is `Row`, or null. */
	const UCataclysmSkillTemplate* SkillOfRow(UAbilitySystemComponent* Abilities, FName Row)
	{
		for (const FGameplayAbilitySpec& Spec : Abilities->GetActivatableAbilities())
		{
			const UCataclysmSkillTemplate* Skill = Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());
			if (Skill && Skill->SkillRow == Row)
			{
				return Skill;
			}
		}
		return nullptr;
	}

	/** The player's basic attack, a skill template made for the basic attack's key, or null. */
	const UCataclysmSkillTemplate* BasicAttackOf(UAbilitySystemComponent* Abilities)
	{
		for (const FGameplayAbilitySpec& Spec : Abilities->GetActivatableAbilities())
		{
			const UCataclysmSkillTemplate* Skill = Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());
			if (Skill && Skill->GetDesignedSlot() == ECataclysmAbilitySlot::BasicAttack)
			{
				return Skill;
			}
		}
		return nullptr;
	}

	/** A creature to strike, apart from the others, at its default figures. */
	ACataclysmEnemyCharacter* SpawnCreature(UWorld* World, float X)
	{
		return World->SpawnActor<ACataclysmEnemyCharacter>(FVector(X, 0.0f, 0.0f), FRotator::ZeroRotator);
	}

	/** One blow by `Skill`, 100% of weapon damage, through the one place every blow is dealt. Answers what was sent. */
	float Strike(AActor* By, AActor* Target, const UCataclysmSkillTemplate* Skill, FCataclysmDamageResult& OutResult)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.Skill = Skill;
		return UCataclysmSkillEffects::ApplyHit(By, Target, 100.0f, FGameplayTagContainer(), Delivery, &OutResult);
	}

	/** The health `Skill`'s row has removed from `Creature`: 0 when none. */
	float CreditedTo(const ACataclysmEnemyCharacter* Creature, FName Row)
	{
		const float* Held = Creature->GetHealthRemovedBySkill().Find(Row);
		return Held ? *Held : 0.0f;
	}
}

// `using namespace` inside each test, never at file scope: a unity build concatenates this file with others.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelsPriceTest,
	"Cataclysm.SkillLevels.ThePriceOfALevelIsAQuarterOfTheCharactersClimbToItsPairedLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillLevelsPriceTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelsTest;

	AddInfo(FString::Printf(TEXT("SKILL PRICES: level 2 %.1f (character level %d), 3 %.1f (%d), 5 %.1f (%d), 10 %.1f (%d), "
								 "20 %.1f (%d)"),
							Levels::ExperienceToReach(2), Levels::PairedCharacterLevel(2), Levels::ExperienceToReach(3),
							Levels::PairedCharacterLevel(3), Levels::ExperienceToReach(5),
							Levels::PairedCharacterLevel(5), Levels::ExperienceToReach(10),
							Levels::PairedCharacterLevel(10), Levels::ExperienceToReach(20),
							Levels::PairedCharacterLevel(20)));

	TestEqual(TEXT("level 1 is free"), Levels::ExperienceToReach(1), 0.0);
	TestEqual(TEXT("level 2: a quarter of the character's climb to level 2"), Levels::ExperienceToReach(2),
			  0.25 * static_cast<double>(UCataclysmExperience::TotalToReach(2)));
	TestEqual(TEXT("LEVEL 3: A QUARTER OF THE CLIMB TO CHARACTER LEVEL 4, the level Path of Exile pairs with gem level 3"),
			  Levels::ExperienceToReach(3), 0.25 * static_cast<double>(UCataclysmExperience::TotalToReach(4)));
	TestEqual(TEXT("level 20: a quarter of the climb to character level 70"), Levels::ExperienceToReach(20),
			  0.25 * static_cast<double>(UCataclysmExperience::TotalToReach(70)));

	bool bRises = true;
	for (int32 Level = 2; Level <= Levels::MaxLevel; ++Level)
	{
		bRises = bRises && Levels::PairedCharacterLevel(Level) > Levels::PairedCharacterLevel(Level - 1)
			&& Levels::ExperienceToReach(Level) > Levels::ExperienceToReach(Level - 1);
	}
	TestTrue(TEXT("every level is paired with a higher character level, and costs more"), bRises);
	TestEqual(TEXT("level 1 is paired with character level 1"), Levels::PairedCharacterLevel(1), 1);
	TestEqual(TEXT("and level 20 with 70, Path of Exile's"), Levels::PairedCharacterLevel(20), 70);
	TestEqual(TEXT("past the cap, the cap's"), Levels::PairedCharacterLevel(25), 70);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelsCapTest,
	"Cataclysm.SkillLevels.ASkillIsCappedByTheCharactersLevelAndKeepsItsExperience",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillLevelsCapTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelsTest;

	const double Plenty = 1.0e12;
	const double ToThree = Levels::ExperienceToReach(3);
	TestEqual(TEXT("no experience, level 1"), Levels::LevelFor(0.0, 100), 1);
	TestEqual(TEXT("a level 1 character's skill stays at 1 whatever it holds"), Levels::LevelFor(Plenty, 1), 1);
	TestEqual(TEXT("A LEVEL 3 CHARACTER'S STOPS AT 2: level 3 is paired with character level 4"),
			  Levels::LevelFor(Plenty, 3), 2);
	TestEqual(TEXT("and a level 4 character's reaches 3"), Levels::LevelFor(Plenty, 4), 3);
	TestEqual(TEXT("a level 100 character's reaches the cap, 20"), Levels::LevelFor(Plenty, 100), 20);
	TestEqual(TEXT("one short of level 3 is level 2"), Levels::LevelFor(ToThree - 0.5, 100), 2);
	TestEqual(TEXT("and level 3's price exactly is level 3"), Levels::LevelFor(ToThree, 100), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelsFactorTest,
	"Cataclysm.SkillLevels.EachLevelMultipliesTheDamagePercentageAndLevelTwentyIsTwoPointEightFiveTimesLevelOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillLevelsFactorTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelsTest;

	AddInfo(FString::Printf(TEXT("SKILL FACTORS: level 1 %.4f, 2 %.4f, 3 %.4f, 5 %.4f, 10 %.4f, 20 %.4f"),
							Levels::DamageFactor(1), Levels::DamageFactor(2), Levels::DamageFactor(3),
							Levels::DamageFactor(5), Levels::DamageFactor(10), Levels::DamageFactor(20)));
	TestEqual(TEXT("level 1 is the skill's stated percentage"), Levels::DamageFactor(1), 1.0);
	TestEqual(TEXT("each level multiplies it by 1.0567"), Levels::DamageFactor(2), 1.0567, 1.0e-9);
	TestEqual(TEXT("level 3, twice"), Levels::DamageFactor(3), 1.0567 * 1.0567, 1.0e-9);
	TestEqual(TEXT("LEVEL 20 IS 2.85 TIMES LEVEL 1, Path of Exile's Cleave, as the owner ruled"),
			  Levels::DamageFactor(20), 2.85, 0.005);
	TestEqual(TEXT("and no more past the cap"), Levels::DamageFactor(25), Levels::DamageFactor(20));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelsWordsTest,
	"Cataclysm.SkillLevels.TheBarsWordsSayTheLevelAndTheShareEarnedRoundedDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillLevelsWordsTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelsTest;

	const double ToTwo = Levels::ExperienceToReach(2);
	AddInfo(FString::Printf(TEXT("SKILL BAR WORDS: '%s' | '%s' | '%s' | '%s' | '%s'"), *Levels::TextFor(0.0, 100),
							*Levels::TextFor(ToTwo / 2.0, 100), *Levels::TextFor(ToTwo - 0.01, 100),
							*Levels::TextFor(1.0e12, 3), *Levels::TextFor(1.0e12, 100)));
	TestEqual(TEXT("a new skill"), Levels::TextFor(0.0, 100), FString(TEXT("Level 1: 0.0%")));
	TestEqual(TEXT("half way to level 2"), Levels::TextFor(ToTwo / 2.0, 100), FString(TEXT("Level 1: 50.0%")));
	TestEqual(TEXT("ONE SHORT OF LEVEL 2 READS 99.9%, NOT 100.0%"), Levels::TextFor(ToTwo - 0.01, 100),
			  FString(TEXT("Level 1: 99.9%")));
	TestEqual(TEXT("a paid-for level the character's level holds back says what it waits for"),
			  Levels::TextFor(1.0e12, 3), FString(TEXT("Level 2: waits for character level 4")));
	TestEqual(TEXT("the cap says so"), Levels::TextFor(1.0e12, 100), FString(TEXT("Level 20: maximum")));
	TestEqual(TEXT("the bar is empty at nothing"), Levels::FractionFor(0.0, 100), 0.0f);
	TestEqual(TEXT("half full half way"), Levels::FractionFor(ToTwo / 2.0, 100), 0.5f, 1.0e-6f);
	TestEqual(TEXT("full while waiting on the character"), Levels::FractionFor(1.0e12, 3), 1.0f);
	TestEqual(TEXT("and full at the cap"), Levels::FractionFor(1.0e12, 100), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelsSplitTest,
	"Cataclysm.SkillLevels.AKillIsSharedAmongTheSkillsByTheHealthEachRemoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillLevelsSplitTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	ACataclysmPlayerState* Healed = World->SpawnActor<ACataclysmPlayerState>();
	if (!TestTrue(TEXT("set-up: two player states"), State && Healed))
	{
		return false;
	}
	const FName Heavy(TEXT("Row_Heavy"));
	const FName Special(TEXT("Row_Special"));

	// 60 AND 30 OF A CREATURE'S 100 HEALTH, the basic attack the other 10: 60% and 30% of 1000.
	State->GrantSkillExperience(1000, TMap<FName, float>{{Heavy, 60.0f}, {Special, 30.0f}}, 100.0f);
	// A CREATURE THAT HEALED: 150 and 50 removed of a 100 maximum, measured against the 200 removed.
	Healed->GrantSkillExperience(1000, TMap<FName, float>{{Heavy, 150.0f}, {Special, 50.0f}}, 100.0f);
	const double HeavyBefore = State->GetSkillExperience(Heavy);
	State->GrantSkillExperience(0, TMap<FName, float>{{Heavy, 60.0f}}, 100.0f);
	State->GrantSkillExperience(1000, TMap<FName, float>(), 100.0f);

	AddInfo(FString::Printf(TEXT("KILL SPLIT: 60 and 30 of 100 -> %.1f and %.1f; healed, 150 and 50 of 100 -> %.1f and %.1f"),
							State->GetSkillExperience(Heavy), State->GetSkillExperience(Special),
							Healed->GetSkillExperience(Heavy), Healed->GetSkillExperience(Special)));
	TestEqual(TEXT("THE HEAVY'S SHARE: 60% of the kill"), State->GetSkillExperience(Heavy), 600.0, 1.0e-9);
	TestEqual(TEXT("the Special's: 30%"), State->GetSkillExperience(Special), 300.0, 1.0e-9);
	TestEqual(TEXT("A HEALED CREATURE PAYS NO MORE THAN THE KILL: 75%"), Healed->GetSkillExperience(Heavy), 750.0, 1.0e-9);
	TestEqual(TEXT("and 25%"), Healed->GetSkillExperience(Special), 250.0, 1.0e-9);
	TestEqual(TEXT("a kill worth nothing pays nothing"), State->GetSkillExperience(Heavy), HeavyBefore);
	TestEqual(TEXT("and the character's own experience is not this function's"), State->GetExperienceIntoLevel(),
			  static_cast<int64>(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelsGainTest,
	"Cataclysm.SkillLevels.IncreasedSkillExperienceGainRaisesEverySkillsShareAndNotTheCharacters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `skill_experience_gain` multiplies a kill's experience before it is shared, so every skill's share rises alike, and
 * the character's own experience does not read it. Issue #2803, ruled 2026-11-21. The gem Of The Scholar will write it
 * once gems can be socketed (#46); here it is set directly. 50 increased: 60 and 30 of a 100-health creature's 1000
 * give 900 and 450 where they gave 600 and 300. Prints "SKILL EXPERIENCE GAIN".
 */
bool FCataclysmSkillLevelsGainTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmPlayerState* Plain = World->SpawnActor<ACataclysmPlayerState>();
	ACataclysmPlayerState* Raised = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* RaisedSystem =
		Raised ? Cast<UCataclysmAbilitySystemComponent>(Raised->GetAbilitySystemComponent()) : nullptr;
	if (!TestTrue(TEXT("set-up: two player states, the second with an ability system"), Plain && RaisedSystem))
	{
		return false;
	}
	RaisedSystem->InitAbilityActorInfo(Raised, Raised);
	{
		FCataclysmStatModifier Increase;
		Increase.Bucket = ECataclysmStatBucket::Increased;
		Increase.Source = ECataclysmModifierSource::Gem;
		Increase.Value = 50.0f;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(ACataclysmPlayerState::SkillExperienceGainStat));
		Line.Base = 0.0f;
		Line.Modifiers = {Increase};
		RaisedSystem->SetStatInputs(MoveTemp(Inputs));
	}
	const FName Heavy(TEXT("Row_Heavy"));
	const FName Special(TEXT("Row_Special"));

	Plain->GrantSkillExperience(1000, TMap<FName, float>{{Heavy, 60.0f}, {Special, 30.0f}}, 100.0f);
	Raised->GrantSkillExperience(1000, TMap<FName, float>{{Heavy, 60.0f}, {Special, 30.0f}}, 100.0f);

	AddInfo(FString::Printf(TEXT("SKILL EXPERIENCE GAIN: without %.1f and %.1f; with 50 increased %.1f and %.1f; factor "
								 "%.2f and %.2f; the character's own kill of 100 -> %lld"),
							Plain->GetSkillExperience(Heavy), Plain->GetSkillExperience(Special),
							Raised->GetSkillExperience(Heavy), Raised->GetSkillExperience(Special),
							Plain->SkillExperienceGainFactor(), Raised->SkillExperienceGainFactor(),
							Raised->ExperienceAfterGain(100)));
	TestEqual(TEXT("CONTROL: without the stat, 60% of the kill"), Plain->GetSkillExperience(Heavy), 600.0, 1.0e-9);
	TestEqual(TEXT("and 30%"), Plain->GetSkillExperience(Special), 300.0, 1.0e-9);
	TestEqual(TEXT("WITH 50 INCREASED, the Heavy's share is half as much again: 900"), Raised->GetSkillExperience(Heavy),
			  900.0, 1.0e-6);
	TestEqual(TEXT("and the Special's alike: 450"), Raised->GetSkillExperience(Special), 450.0, 1.0e-6);
	TestEqual(TEXT("the factor is 1.5"), Raised->SkillExperienceGainFactor(), 1.5, 1.0e-6);
	TestEqual(TEXT("CONTROL: the character's own kill is not raised: 100 stays 100"), Raised->ExperienceAfterGain(100),
			  static_cast<int64>(100));
	TestEqual(TEXT("and the character gained no experience of its own here"), Raised->GetExperienceIntoLevel(),
			  static_cast<int64>(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelsBlowTest,
	"Cataclysm.SkillLevels.ASkillsBlowOnACreatureIsScaledByItsLevelAndCreditedToIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillLevelsBlowTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelsTest;

	FSkillLevelsPlayer Player = MakeSkillLevelsPlayer();
	ON_SCOPE_EXIT
	{
		if (Player.World)
		{
			Player.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	const UCataclysmSkillTemplate* Heavy = Player.IsUsable() ? SkillOfRow(Player.Abilities, MoltenCleave) : nullptr;
	ACataclysmEnemyCharacter* First = Player.World ? SpawnCreature(Player.World, 300.0f) : nullptr;
	ACataclysmEnemyCharacter* Levelled = Player.World ? SpawnCreature(Player.World, 600.0f) : nullptr;
	ACataclysmEnemyCharacter* Again = Player.World ? SpawnCreature(Player.World, 900.0f) : nullptr;
	if (!TestTrue(TEXT("set-up: a possessed player holding Molten Cleave, and three creatures"),
				  Heavy && First && Levelled && Again))
	{
		return false;
	}
	Player.State->SetLevelAndExperience(4, 0);

	FCataclysmDamageResult FirstResult;
	const float SentAtOne = Strike(Player.Character, First, Heavy, FirstResult);
	Player.State->SetSkillExperience(MoltenCleave, Levels::ExperienceToReach(3));
	const int32 LevelThen = Player.State->SkillLevelOf(MoltenCleave);
	FCataclysmDamageResult LevelledResult;
	const float SentAtThree = Strike(Player.Character, Levelled, Heavy, LevelledResult);
	Player.State->SetSkillExperience(MoltenCleave, 0.0);
	FCataclysmDamageResult AgainResult;
	const float SentAtOneAgain = Strike(Player.Character, Again, Heavy, AgainResult);

	AddInfo(FString::Printf(TEXT("SKILL BLOW: level 1 sent %.3f, took %.3f from health; level %d sent %.3f (x%.4f); level 1 "
								 "again sent %.3f; credited %.3f, %.3f and %.3f"),
							SentAtOne, FirstResult.DealtToHealth, LevelThen, SentAtThree,
							SentAtOne > 0.0f ? SentAtThree / SentAtOne : 0.0f, SentAtOneAgain,
							CreditedTo(First, MoltenCleave), CreditedTo(Levelled, MoltenCleave),
							CreditedTo(Again, MoltenCleave)));

	TestTrue(TEXT("set-up: the blow landed and took health"), SentAtOne > 0.0f && FirstResult.DealtToHealth > 0.0f);
	TestEqual(TEXT("set-up: the skill stood at level 3 for the second blow"), LevelThen, 3);
	TestEqual(TEXT("LEVEL 3 SENDS 1.0567 SQUARED TIMES LEVEL 1, in the base bucket"), SentAtThree / SentAtOne,
			  static_cast<float>(Levels::DamageFactor(3)), 1.0e-4f);
	TestEqual(TEXT("CONTROL: back at level 1 it sends level 1's"), SentAtOneAgain, SentAtOne, 1.0e-3f);
	TestEqual(TEXT("THE HEALTH THE BLOW TOOK IS CREDITED TO MOLTEN CLEAVE"), CreditedTo(First, MoltenCleave),
			  FirstResult.DealtToHealth, 1.0e-3f);
	TestEqual(TEXT("and the levelled blow's too"), CreditedTo(Levelled, MoltenCleave), LevelledResult.DealtToHealth,
			  1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelsNotTheBasicTest,
	"Cataclysm.SkillLevels.TheBasicAttackAndABlowOnTheUserAreNeitherScaledNorCredited",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillLevelsNotTheBasicTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelsTest;

	FSkillLevelsPlayer Player = MakeSkillLevelsPlayer();
	ON_SCOPE_EXIT
	{
		if (Player.World)
		{
			Player.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	const UCataclysmSkillTemplate* Basic = Player.IsUsable() ? BasicAttackOf(Player.Abilities) : nullptr;
	const UCataclysmSkillTemplate* Heavy = Player.IsUsable() ? SkillOfRow(Player.Abilities, MoltenCleave) : nullptr;
	ACataclysmEnemyCharacter* First = Player.World ? SpawnCreature(Player.World, 300.0f) : nullptr;
	ACataclysmEnemyCharacter* Second = Player.World ? SpawnCreature(Player.World, 600.0f) : nullptr;
	if (!TestTrue(TEXT("set-up: a possessed player with its basic attack and Molten Cleave, and two creatures"),
				  Basic && Heavy && First && Second))
	{
		return false;
	}
	Player.State->SetLevelAndExperience(4, 0);

	// THE BASIC ATTACK CARRIES NO SKILL ROW: `UCataclysmWeaponSkills::BasicAttackFor` builds it from the weapon's base,
	// not from a row of the skill matrix, so nothing can be kept under it. Its blows are struck twice, with Molten
	// Cleave raised to level 3 between them, to show a skill's level does not reach the basic attack.
	FCataclysmDamageResult BeforeResult;
	const float BasicBefore = Strike(Player.Character, First, Basic, BeforeResult);
	Player.State->SetSkillExperience(MoltenCleave, Levels::ExperienceToReach(3));
	FCataclysmDamageResult AfterResult;
	const float BasicAfter = Strike(Player.Character, Second, Basic, AfterResult);
	Player.State->SetSkillExperience(MoltenCleave, 0.0);

	// A LEVELLED SKILL'S BLOW ON ITS OWN USER, at level 1 and at level 3.
	FCataclysmDamageResult SelfOne;
	const float OnTheUserAtOne = Strike(Player.Character, Player.Character, Heavy, SelfOne);
	Player.State->SetSkillExperience(MoltenCleave, Levels::ExperienceToReach(3));
	FCataclysmDamageResult SelfThree;
	const float OnTheUserAtThree = Strike(Player.Character, Player.Character, Heavy, SelfThree);

	AddInfo(FString::Printf(TEXT("NOT LEVELLED: the basic attack (row %s) sent %.3f and %.3f, credited %d row(s); on the user "
								 "Molten Cleave sent %.3f at level 1 and %.3f at level %d"),
							*Basic->SkillRow.ToString(), BasicBefore, BasicAfter,
							First->GetHealthRemovedBySkill().Num() + Second->GetHealthRemovedBySkill().Num(),
							OnTheUserAtOne, OnTheUserAtThree, Player.State->SkillLevelOf(MoltenCleave)));

	TestTrue(TEXT("set-up: the basic attack landed"), BasicBefore > 0.0f && BeforeResult.DealtToHealth > 0.0f);
	TestTrue(TEXT("THE BASIC ATTACK CARRIES NO SKILL ROW, so it can earn and level nothing"), Basic->SkillRow.IsNone());
	TestEqual(TEXT("THE BASIC ATTACK IS NOT SCALED, with a skill at level 3"), BasicAfter, BasicBefore, 1.0e-3f);
	TestEqual(TEXT("AND CREDITS NOTHING"),
			  First->GetHealthRemovedBySkill().Num() + Second->GetHealthRemovedBySkill().Num(), 0);
	TestTrue(TEXT("set-up: the blow on the user was dealt"), OnTheUserAtOne > 0.0f);
	TestEqual(TEXT("A LEVELLED SKILL'S BLOW ON ITS OWN USER KEEPS THE STATED FIGURE"), OnTheUserAtThree, OnTheUserAtOne,
			  1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelsDeathTest,
	"Cataclysm.SkillLevels.ACreaturesDeathPaysEachSkillItsShare",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillLevelsDeathTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelsTest;

	FSkillLevelsPlayer Player = MakeSkillLevelsPlayer();
	ON_SCOPE_EXIT
	{
		if (Player.World)
		{
			Player.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	ACataclysmEnemyCharacter* Hurt = Player.World ? SpawnCreature(Player.World, 300.0f) : nullptr;
	ACataclysmEnemyCharacter* Untouched = Player.World ? SpawnCreature(Player.World, 600.0f) : nullptr;
	if (!TestTrue(TEXT("set-up: a possessed player and two creatures"), Player.IsUsable() && Hurt && Untouched))
	{
		return false;
	}

	// A BOSS, as the character level's kill test makes one: worth something wherever the sandbox's floor sits.
	constexpr int32 BossStep = 4;
	const int32 Worth = UCataclysmEnemyScore::ScoreFor(UCataclysmEnemyScore::FloorIn(Player.World), BossStep);
	const float MaxHealth = Hurt->MaxHealthForTheSkillSplit();
	const float Removed = 0.3f * MaxHealth;
	Hurt->NoteHealthRemovedBySkill(MoltenCleave, Removed);
	Hurt->SetRarityStep(BossStep);
	Hurt->HandleDeath();
	const double Paid = Player.State->GetSkillExperience(MoltenCleave);
	Untouched->SetRarityStep(BossStep);
	Untouched->HandleDeath();
	const double PaidAfterTheUntouched = Player.State->GetSkillExperience(MoltenCleave);

	AddInfo(FString::Printf(TEXT("DEATH PAYS SKILLS: worth %d, maximum health %.1f, Molten Cleave removed %.1f, paid %.3f; "
								 "after a creature no skill touched, %.3f"),
							Worth, MaxHealth, Removed, Paid, PaidAfterTheUntouched));
	TestTrue(TEXT("set-up: a Boss is worth something here, and the creature has health"), Worth > 0 && MaxHealth > 0.0f);
	TestEqual(TEXT("A KILL PAYS MOLTEN CLEAVE THE SHARE OF THE CREATURE'S HEALTH IT REMOVED: 30%"), Paid,
			  0.3 * static_cast<double>(Worth), 1.0e-3);
	TestEqual(TEXT("and a creature no skill touched pays it nothing"), PaidAfterTheUntouched, Paid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelsKillingBlowTest,
	"Cataclysm.SkillLevels.ASkillsKillingBlowIsCountedInTheDeathsSplit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A creature a skill kills pays that skill for the blow that killed it. Issue #2764, slice 1b. Until then the credit was
 * made after the blow had been dealt, and the blow's death -- handled inside the health write -- had already paid, so a
 * kill in one hit paid its skill nothing and every other kill left its last blow out. Two Bosses, so they are worth
 * something here. The first has 5 health, and one Molten Cleave blow kills it from full. The second is given one and a
 * half times that blow's whole figure (what it dealt to health plus its overkill): the first blow leaves it alive, the
 * second kills it. Each has lost its whole health to Molten Cleave, so each pays the skill its whole worth. The blows
 * cannot be critical, so every one has the same figure.
 */
bool FCataclysmSkillLevelsKillingBlowTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelsTest;

	FSkillLevelsPlayer Player = MakeSkillLevelsPlayer();
	ON_SCOPE_EXIT
	{
		if (Player.World)
		{
			Player.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	const UCataclysmSkillTemplate* Heavy = Player.IsUsable() ? SkillOfRow(Player.Abilities, MoltenCleave) : nullptr;
	ACataclysmEnemyCharacter* OneBlow = Player.World ? SpawnCreature(Player.World, 300.0f) : nullptr;
	ACataclysmEnemyCharacter* TwoBlows = Player.World ? SpawnCreature(Player.World, 600.0f) : nullptr;
	UAbilitySystemComponent* OneBlowsSystem = OneBlow ? OneBlow->GetAbilitySystemComponent() : nullptr;
	UAbilitySystemComponent* TwoBlowsSystem = TwoBlows ? TwoBlows->GetAbilitySystemComponent() : nullptr;
	if (!TestTrue(TEXT("set-up: a possessed player holding Molten Cleave, and two creatures"),
				  Heavy && OneBlowsSystem && TwoBlowsSystem))
	{
		return false;
	}
	const auto SetHealth = [](UAbilitySystemComponent* System, float Health)
	{
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Health);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
	};
	const auto StrikeOnce = [&Player, Heavy](AActor* Target, FCataclysmDamageResult& OutResult)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.Skill = Heavy;
		Delivery.bCannotCriticallyStrike = true;
		UCataclysmSkillEffects::ApplyHit(Player.Character, Target, 100.0f, FGameplayTagContainer(), Delivery, &OutResult);
	};

	// BOSSES, as the death test makes one: worth something wherever the sandbox's floor sits. The rarity first, then the
	// health, because the rarity sets the creature's starting figures again.
	constexpr int32 BossStep = 4;
	const int32 Worth = UCataclysmEnemyScore::ScoreFor(UCataclysmEnemyScore::FloorIn(Player.World), BossStep);
	OneBlow->SetRarityStep(BossStep);
	TwoBlows->SetRarityStep(BossStep);
	constexpr float SmallHealth = 5.0f;
	SetHealth(OneBlowsSystem, SmallHealth);

	FCataclysmDamageResult Killing;
	StrikeOnce(OneBlow, Killing);
	const bool bOneBlowKilled = UCataclysmSkillEffects::IsDead(OneBlow);
	const double PaidForOne = Player.State->GetSkillExperience(MoltenCleave);

	// THE SECOND CREATURE'S HEALTH, ONE AND A HALF BLOWS, from the first blow's whole figure.
	const float WholeBlow = Killing.DealtToHealth + Killing.Overkill;
	const float TwoBlowsHealth = 1.5f * WholeBlow;
	SetHealth(TwoBlowsSystem, TwoBlowsHealth);
	FCataclysmDamageResult FirstOfTwo;
	StrikeOnce(TwoBlows, FirstOfTwo);
	const bool bAliveAfterTheFirst = !UCataclysmSkillEffects::IsDead(TwoBlows);
	FCataclysmDamageResult SecondOfTwo;
	StrikeOnce(TwoBlows, SecondOfTwo);
	const bool bTwoBlowsKilled = UCataclysmSkillEffects::IsDead(TwoBlows);
	const double PaidForTwo = Player.State->GetSkillExperience(MoltenCleave) - PaidForOne;

	AddInfo(FString::Printf(TEXT("KILLING BLOW: worth %d; one blow on %.1f health dealt %.3f (whole %.3f), killed %s, paid "
								 "%.3f; two blows on %.3f health dealt %.3f and %.3f, killed %s, paid %.3f"),
							Worth, SmallHealth, Killing.DealtToHealth, WholeBlow, bOneBlowKilled ? TEXT("yes") : TEXT("NO"),
							PaidForOne, TwoBlowsHealth, FirstOfTwo.DealtToHealth, SecondOfTwo.DealtToHealth,
							bTwoBlowsKilled ? TEXT("yes") : TEXT("NO"), PaidForTwo));
	TestTrue(TEXT("set-up: a Boss is worth something here, and the blow has a figure"), Worth > 0 && WholeBlow > SmallHealth);
	TestTrue(TEXT("set-up: one blow killed the small creature"), bOneBlowKilled);
	TestTrue(TEXT("set-up: the other lived through the first blow and died to the second"),
			 bAliveAfterTheFirst && bTwoBlowsKilled);
	TestEqual(TEXT("A KILL IN ONE SKILL BLOW PAYS THE SKILL THE CREATURE'S WHOLE WORTH"), PaidForOne,
			  static_cast<double>(Worth), 1.0e-3);
	TestEqual(TEXT("AND A KILL IN TWO PAYS IT THE WHOLE WORTH, the killing blow's share included"), PaidForTwo,
			  static_cast<double>(Worth), 1.0e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelsMigrationTest,
	"Cataclysm.SkillLevels.ACharacterFromBeforeSkillLevelsMigratesWithNoSkillExperience",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Migrate_9_to_10` writes an empty `SkillExperience`, keeps a list already there, and refuses a key holding anything
 * but a list: run on the committed version 9 file as it is, with one entry, and with a word. And `Character_v9.json`
 * reads through the chain into a record at the current version with every other field kept.
 */
bool FCataclysmSkillLevelsMigrationTest::RunTest(const FString&)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Character_v9.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}
	const TSharedPtr<FJsonObject> AsItIs = CataclysmSaveFixtures::Parse(Text);
	const TSharedPtr<FJsonObject> Held = CataclysmSaveFixtures::Parse(Text);
	const TSharedPtr<FJsonObject> Word = CataclysmSaveFixtures::Parse(Text);
	if (!TestTrue(TEXT("set-up: Character_v9.json is a JSON object"), AsItIs.IsValid() && Held.IsValid() && Word.IsValid())
		|| !TestFalse(TEXT("set-up: and holds no skill experience"), AsItIs->HasField(TEXT("SkillExperience"))))
	{
		return false;
	}
	const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("SkillRow"), TEXT("Demonic_Greataxe_Heavy"));
	Entry->SetNumberField(TEXT("Experience"), 12.5);
	TArray<TSharedPtr<FJsonValue>> OneEntry;
	OneEntry.Add(MakeShared<FJsonValueObject>(Entry));
	Held->SetArrayField(TEXT("SkillExperience"), OneEntry);
	Word->SetStringField(TEXT("SkillExperience"), TEXT("plenty"));

	const TArrayView<const FCataclysmSaveMigrationStep> Steps = GetDefault<UCataclysmCharacterSave>()->MigrationSteps();
	FString AsItIsMessage;
	FString HeldMessage;
	FString WordMessage;
	const ECataclysmSaveMigrationResult AsItIsResult = FCataclysmSaveMigration::Migrate(
		Steps, UCataclysmCharacterSave::SchemaVersionNow, AsItIs.ToSharedRef(), AsItIsMessage);
	const ECataclysmSaveMigrationResult HeldResult = FCataclysmSaveMigration::Migrate(
		Steps, UCataclysmCharacterSave::SchemaVersionNow, Held.ToSharedRef(), HeldMessage);
	const ECataclysmSaveMigrationResult WordResult = FCataclysmSaveMigration::Migrate(
		Steps, UCataclysmCharacterSave::SchemaVersionNow, Word.ToSharedRef(), WordMessage);
	const TArray<TSharedPtr<FJsonValue>>* Written = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Kept = nullptr;
	const bool bWritten = AsItIs->TryGetArrayField(TEXT("SkillExperience"), Written) && Written != nullptr;
	const bool bKept = Held->TryGetArrayField(TEXT("SkillExperience"), Kept) && Kept != nullptr;

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmCharacterSave* Read = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Character_v9.json would not load: %s -- %s"),
								 FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmCharacterSave::SchemaVersionNow);
	CataclysmSaveFixtures::AddTheCharactersVersion10Defaults(*Expected);
	FString Rewritten;
	FString Error;
	const bool bWroteBack = FCataclysmSaveStorage::ToJson(Read, Rewritten, Error);
	FString Where;
	const bool bSame = bWroteBack
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	AddInfo(FString::Printf(TEXT("SKILLS MIGRATED: as it is %d (%s) -> %s; one entry %d (%s) -> %d; a word %d (%s); "
								 "Character_v9.json %s at version %d, the rest %s"),
							static_cast<int32>(AsItIsResult), *AsItIsMessage,
							bWritten ? *FString::Printf(TEXT("%d entries"), Written->Num()) : TEXT("NO LIST"),
							static_cast<int32>(HeldResult), *HeldMessage, bKept ? Kept->Num() : -1,
							static_cast<int32>(WordResult), *WordMessage, FCataclysmSaveStorage::Describe(Result),
							Read->SchemaVersion, bSame ? TEXT("kept") : *Where));

	TestEqual(TEXT("a character at 9 migrates"), static_cast<int32>(AsItIsResult),
			  static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestTrue(TEXT("GAINING AN EMPTY LIST OF SKILL EXPERIENCE, written out"), bWritten && Written->Num() == 0);
	TestTrue(TEXT("one holding an entry keeps it"), bKept && Kept->Num() == 1);
	TestNotEqual(TEXT("one holding a word is refused"), static_cast<int32>(WordResult),
				 static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestEqual(TEXT("Character_v9.json is migrated"), static_cast<int32>(Result),
			  static_cast<int32>(ECataclysmSaveLoadResult::Migrated));
	TestEqual(TEXT("to version 10"), Read->SchemaVersion, 10);
	TestEqual(TEXT("holding no skill experience"), Read->SkillExperience.Num(), 0);
	TestTrue(TEXT("and every other field"), bSame);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelsSaveTest,
	"Cataclysm.SkillLevels.SkillExperienceIsGatheredIntoTheCharacterRecordAndPutBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillLevelsSaveTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelsTest;

	FSkillLevelsPlayer Player = MakeSkillLevelsPlayer();
	ON_SCOPE_EXIT
	{
		if (Player.World)
		{
			Player.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	// THE PLAYER THE RECORD IS LOADED INTO: a second possessed player in the same world, as the character restore tests
	// load into one, so the record goes back through the game's own load, `FCataclysmSaveApply::CharacterInto`, and its
	// `RestoreSkillExperience` line. Before window skills-w2 this test called `RestoreSkillExperience` itself, and no
	// test loaded a record holding skill experience through `CharacterInto`.
	FSkillLevelsPlayer Loaded;
	Loaded.World = Player.World;
	if (Player.World)
	{
		Loaded.State = Player.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Player.World->SpawnActor<APlayerController>();
		Loaded.Character = Player.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Loaded.State && Controller && Loaded.Character)
		{
			Controller->SetPlayerState(Loaded.State);
			Controller->Possess(Loaded.Character);
			Loaded.Abilities = Loaded.Character->GetAbilitySystemComponent();
		}
	}
	if (!TestTrue(TEXT("set-up: two possessed players"), Player.IsUsable() && Loaded.IsUsable()))
	{
		return false;
	}
	const FName Away(TEXT("Demonic_Greatsword_Ultimate")); // a skill whose weapon is not worn
	Player.State->SetSkillExperience(MoltenCleave, 37810.5);
	Player.State->SetSkillExperience(Away, 1234.25);

	UCataclysmCharacterSave* Record = NewObject<UCataclysmCharacterSave>();
	const bool bGathered = FCataclysmSaveGather::CharacterFrom(*Player.Character, *Record);

	// CONTROL: THE SAME RECORD WITH NO VERSION IS REFUSED. `CharacterFrom` does not set the version, and a record
	// nobody gave one is version 0, which `FCataclysmSaveMigration::Migrate` refuses ("the first real version is 1").
	// Window skills-w1 (2026-11-19) ran this test without the line below, and the read came back null for that reason;
	// this keeps the cause measured.
	FString Unversioned;
	FString UnversionedError;
	const bool bWroteUnversioned = FCataclysmSaveStorage::ToJson(Record, Unversioned, UnversionedError);
	ECataclysmSaveLoadResult UnversionedResult = ECataclysmSaveLoadResult::NotValidJson;
	FString UnversionedMessage;
	const UCataclysmSaveRecord* ReadUnversioned = bWroteUnversioned
		? FCataclysmSaveStorage::FromJson(Unversioned, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(),
										  UnversionedResult, UnversionedMessage)
		: nullptr;

	// THE VERSION THE SAVE WRITER GIVES EVERY CHARACTER RECORD, before it gathers
	// (`UCataclysmSaveWriter::WriteTheCharacterRecord`), the one place the game makes a character record to write.
	Record->SchemaVersion = UCataclysmCharacterSave::SchemaVersionNow;
	FString Json;
	FString Error;
	const bool bWritten = FCataclysmSaveStorage::ToJson(Record, Json, Error);
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmCharacterSave* Read = bWritten ? Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		Json, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message)) : nullptr;
	int32 SkillsRefused = 0;
	FString Why;
	const bool bPutBack = Read && FCataclysmSaveApply::CharacterInto(*Read, *Loaded.Character, SkillsRefused, Why);

	AddInfo(FString::Printf(TEXT("SKILLS SAVED: gathered %s, %d entries; with no version read back %s (%s -- %s); at "
								 "version %d read back %s (%s -- %s), %d entries; loaded through CharacterInto %s (%s); "
								 "Molten Cleave %.2f, the Greatsword's Ultimate %.2f"),
							bGathered ? TEXT("yes") : TEXT("NO"), Record->SkillExperience.Num(),
							ReadUnversioned ? TEXT("yes") : TEXT("NO"), FCataclysmSaveStorage::Describe(UnversionedResult),
							*UnversionedMessage, Record->SchemaVersion, Read ? TEXT("yes") : TEXT("NO"),
							FCataclysmSaveStorage::Describe(Result), *Message, Read ? Read->SkillExperience.Num() : -1,
							bPutBack ? TEXT("yes") : TEXT("NO"), *Why, Loaded.State->GetSkillExperience(MoltenCleave),
							Loaded.State->GetSkillExperience(Away)));
	TestTrue(TEXT("set-up: the record was gathered and written, with and without its version"),
			 bGathered && bWritten && bWroteUnversioned);
	TestEqual(TEXT("THE RECORD HOLDS BOTH SKILLS"), Record->SkillExperience.Num(), 2);
	TestNull(TEXT("CONTROL: WITH NO VERSION IT IS REFUSED, as in window skills-w1"), ReadUnversioned);
	TestEqual(TEXT("by the version check"), static_cast<int32>(UnversionedResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::MigrationRefusedIt));
	if (!TestNotNull(*FString::Printf(TEXT("AT THE WRITER'S VERSION IT READS BACK (%s -- %s)"),
									  FCataclysmSaveStorage::Describe(Result), *Message),
					 Read))
	{
		return false;
	}
	TestEqual(TEXT("as a record of this version, not migrated"), static_cast<int32>(Result),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestEqual(TEXT("holding both skills"), Read->SkillExperience.Num(), 2);
	if (!TestTrue(*FString::Printf(TEXT("LOADED THROUGH THE GAME'S LOAD, FCataclysmSaveApply::CharacterInto (%s)"), *Why),
				  bPutBack))
	{
		return false;
	}
	TestEqual(TEXT("PUT BACK, MOLTEN CLEAVE'S FRACTION"), Loaded.State->GetSkillExperience(MoltenCleave), 37810.5);
	TestEqual(TEXT("AND THE SKILL WHOSE WEAPON IS OFF, KEPT"), Loaded.State->GetSkillExperience(Away), 1234.25);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelsBarTest,
	"Cataclysm.SkillLevels.TheSkillBarShowsEachSkillsLevelAndNotTheBasicAttacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillLevelsBarTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelsTest;

	FSkillLevelsPlayer Player = MakeSkillLevelsPlayer();
	ON_SCOPE_EXIT
	{
		if (Player.World)
		{
			Player.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const auto BoxOn = [](const TArray<FCataclysmSkillBarSlot>& Bar, ECataclysmAbilitySlot Slot)
	{
		return Bar.FindByPredicate([Slot](const FCataclysmSkillBarSlot& Box) { return Box.Slot == Slot; });
	};

	const TArray<FCataclysmSkillBarSlot> Fresh = UCataclysmSkillBar::Read(Player.Character);
	Player.State->SetLevelAndExperience(4, 0);
	Player.State->SetSkillExperience(MoltenCleave, Levels::ExperienceToReach(3));
	const TArray<FCataclysmSkillBarSlot> Later = UCataclysmSkillBar::Read(Player.Character);
	const FCataclysmSkillBarSlot* HeavyFresh = BoxOn(Fresh, ECataclysmAbilitySlot::Heavy);
	const FCataclysmSkillBarSlot* HeavyLater = BoxOn(Later, ECataclysmAbilitySlot::Heavy);
	const FCataclysmSkillBarSlot* BasicLater = BoxOn(Later, ECataclysmAbilitySlot::BasicAttack);

	AddInfo(FString::Printf(TEXT("SKILL BAR LEVELS: the Heavy box %d '%s', then %d '%s' %.3f; the basic attack's %d '%s'"),
							HeavyFresh ? HeavyFresh->SkillLevel : -1, HeavyFresh ? *HeavyFresh->SkillLevelText : TEXT("-"),
							HeavyLater ? HeavyLater->SkillLevel : -1, HeavyLater ? *HeavyLater->SkillLevelText : TEXT("-"),
							HeavyLater ? HeavyLater->SkillLevelFraction : -1.0f,
							BasicLater ? BasicLater->SkillLevel : -1, BasicLater ? *BasicLater->SkillLevelText : TEXT("-")));
	// THE BAR HAS NO BOX FOR THE BASIC ATTACK, by design since the bar was added (#801): `UCataclysmSkillBar::SlotsShown`
	// lists the six slots a player presses. Window skills-w1 (2026-11-19) ran this test asking for one, and its set-up
	// failed on that; the basic attack's half is asserted below as the box's absence.
	if (!TestTrue(TEXT("set-up: the bar has a Heavy box"), HeavyFresh && HeavyLater))
	{
		return false;
	}
	TestEqual(TEXT("A NEW CHARACTER'S HEAVY IS LEVEL 1"), HeavyFresh->SkillLevel, 1);
	TestEqual(TEXT("in words"), HeavyFresh->SkillLevelText, FString(TEXT("Level 1: 0.0%")));
	TestEqual(TEXT("AT LEVEL 3'S EXPERIENCE AND CHARACTER LEVEL 4, LEVEL 3"), HeavyLater->SkillLevel, 3);
	TestEqual(TEXT("with an empty bar into it"), HeavyLater->SkillLevelFraction, 0.0f);
	TestNull(TEXT("THE BASIC ATTACK HAS NO BOX ON THE BAR, so it shows no level"), BasicLater);
	TestTrue(TEXT("THE POP-UP SAYS THE LEVEL UNDER THE NAME"),
			 UCataclysmSkillBar::PopupLinesFor(*HeavyLater).Num() > 1
				 && UCataclysmSkillBar::PopupLinesFor(*HeavyLater)[1] == HeavyLater->SkillLevelText);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
