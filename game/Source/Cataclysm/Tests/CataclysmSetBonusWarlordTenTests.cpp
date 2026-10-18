// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "Items/CataclysmItem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Warlord's Will's ten-piece bonus, the mechanism. Ruled by the project owner on 2026-10-17.
 *
 * "Every 30 seconds, you gain a stack of 'Tactical Advantage.' When you use a movement ability, you consume all
 * stacks of 'Tactical Advantage' and gain 100% more movement speed and 50% more damage for 5 seconds. This bonus
 * scales with the number of enemies in the area"
 *
 * THE ROWS ARE MADE UP HERE, IN THE SHAPE THE ENTRY GIVES THE ENCHANTMENT SESSION: three rows scaled by their own
 * stacks on `movement_skill`, one stack at most, 5 s, a trigger cooldown of 30 s (Divine Retribution's ten-piece
 * precedent: no stack is counted); and two rows scaled by `enemies_in_reach_while_own_stack`, 10% more per enemy, at
 * most 5, one beside each damage row. They are put on a real non-set enchantment, "Double your energy shield", and
 * passed through `UCataclysmItemModifiers::AccumulateEnchantmentsInto`, so the loader's lines are what is tested. The
 * real rows, worn at ten pieces with a control at nine, are the enchantment session's test.
 *
 * A WEARER OF THE DAMAGE TESTS IS A CREATURE ON THE PLAYERS' SIDE, as `CataclysmEnemiesInReachTests.cpp` uses, because
 * the count is measured from an avatar that is a character. Its lines hold the made-up rows and nothing else, on a
 * base of 100, so figures are read as ratios against the control read first. Time is moved by adding to the world's
 * clock. The creatures counted stand 3 m from the wearer and 3 m from each other; the one beyond the reach stands at
 * 9 m.
 */
namespace CataclysmSetBonusWarlordTenTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The real enchantment the made-up rows are put on, and the drawback beside it, which has no effect row. */
	const TCHAR* const Benefit = TEXT("Positive_Double_your_energy_shield");
	const TCHAR* const Drawback = TEXT("Negative_Can_t_use_a_basic_attack");

	/** The window the rows open, and the wait between two windows. */
	constexpr float WindowSeconds = 5.0f;
	constexpr float CooldownSeconds = 30.0f;

	/** One of the shipped tables, read from `game/Data`. Null when it does not load. */
	template <typename RowType>
	UDataTable* LoadData(const TCHAR* FileName)
	{
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *(FPaths::ProjectDir() / TEXT("Data") / FileName)))
		{
			return nullptr;
		}
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = RowType::StaticStruct();
		return Table->CreateTableFromCSVString(Contents).Num() > 0 ? nullptr : Table;
	}

	/** The five made-up rows, as a CSV the effect table reads. */
	FString RowsCsv()
	{
		FString Csv = TEXT("Name,Enchantment,Stat,ValueKind,ValueLow,ValueHigh,RequiredTags,Condition,ConditionValue,Scale,"
						   "ScaleStep,Action,ActionEvent,FractionOf,ScaleMaxSteps,StackSeconds,ScaleOffset,EverySeconds,"
						   "EveryNth,ScaleStepHigh,StackSecondsHigh,Condition2,ConditionValue2,ConditionValueHigh,"
						   "TriggerCooldown,EventValue,Ailment,DamageShare\n");
		const auto Window = [&Csv](int32 Index, const TCHAR* Stat, int32 Value)
		{
			Csv += FString::Printf(TEXT("%s#%d,%s,%s,more,%d,%d,,,0,own_stacks,1,,movement_skill,,1,5,0,0,0,0,0,,0,0,30,0,,0\n"),
								   Benefit, Index, Benefit, Stat, Value, Value);
		};
		const auto PerEnemy = [&Csv](int32 Index, const TCHAR* Stat)
		{
			Csv += FString::Printf(
				TEXT("%s#%d,%s,%s,more,10,10,,,0,enemies_in_reach_while_own_stack,1,,,,5,0,0,0,0,0,0,,0,0,0,0,,0\n"),
				Benefit, Index, Benefit, Stat);
		};
		Window(1, TEXT("movement_speed"), 100);
		Window(2, TEXT("attack_damage"), 50);
		Window(3, TEXT("spell_damage"), 50);
		PerEnemy(4, TEXT("attack_damage"));
		PerEnemy(5, TEXT("spell_damage"));
		return Csv;
	}

	/** What the made-up rows become on a wearer: the stat lines and the grants. */
	struct FMade
	{
		TMap<FName, TArray<FCataclysmStatModifier>> Totals;
		TArray<FCataclysmPoolAction> Actions;
	};

	/** The rows passed through the loader. False, with a failed set-up assertion, when a table did not load. */
	bool Make(FAutomationTestBase& Test, FMade& Out)
	{
		UDataTable* Positive = LoadData<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
		UDataTable* Negative = LoadData<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
		UDataTable* Effects = NewObject<UDataTable>();
		Effects->RowStruct = FCataclysmEnchantmentEffectRow::StaticStruct();
		const bool bEffectsRead = Effects->CreateTableFromCSVString(RowsCsv()).Num() == 0;
		if (!Test.TestNotNull(TEXT("set-up: the positive enchantments"), Positive)
			|| !Test.TestNotNull(TEXT("set-up: the negative enchantments"), Negative)
			|| !Test.TestTrue(TEXT("set-up: the five made-up rows read as an effect table"), bEffectsRead))
		{
			return false;
		}

		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		FCataclysmRolledEnchantment Rolled;
		Rolled.Positive = FName(Benefit);
		Rolled.Negative = FName(Drawback);
		Item.Enchantments.Add(Rolled);
		Item.EnchantmentCount = 1;
		UCataclysmItemModifiers::AccumulateEnchantmentsInto(Out.Totals, {Item}, Effects, Positive, Negative,
															&Out.Actions);
		return true;
	}

	/** One stat line on a base, holding what the rows put on that stat. */
	void AddLine(TMap<FName, FCataclysmStatInputs>& Lines, const FMade& Made, const TCHAR* Stat, float Base)
	{
		FCataclysmStatInputs& Line = Lines.Add(FName(Stat));
		Line.Base = Base;
		if (const TArray<FCataclysmStatModifier>* Modifiers = Made.Totals.Find(FName(Stat)))
		{
			Line.Modifiers = *Modifiers;
		}
	}

	/** A character on the given side, harmless and hard to kill. */
	ACataclysmEnemyCharacter* SpawnOn(UWorld* World, const FVector& Where, ECataclysmTeam Side)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(Side));
			Made->SetHealth(1000000.0f);
			Made->SetAttackDamage(0.0f);
		}
		return Made;
	}

	/** A creature on the players' side wearing the rows: its two damage lines on a base of 100, and the grants. */
	UCataclysmAbilitySystemComponent* DamageWearer(FAutomationTestBase& Test, UWorld* World)
	{
		FMade Made;
		if (!Make(Test, Made))
		{
			return nullptr;
		}
		ACataclysmEnemyCharacter* Wearer = SpawnOn(World, FVector::ZeroVector, ECataclysmTeam::Players);
		UCataclysmAbilitySystemComponent* System =
			Wearer ? Cast<UCataclysmAbilitySystemComponent>(Wearer->GetAbilitySystemComponent()) : nullptr;
		if (!Test.TestNotNull(TEXT("set-up: a wearer with an ability system"), System))
		{
			return nullptr;
		}
		TMap<FName, FCataclysmStatInputs> Lines;
		AddLine(Lines, Made, TEXT("attack_damage"), 100.0f);
		AddLine(Lines, Made, TEXT("spell_damage"), 100.0f);
		System->SetStatInputs(MoveTemp(Lines));
		System->SetPoolActions(MoveTemp(Made.Actions));
		return System;
	}

	/** The wearer's figure for a stat, asked with no tags, as a blow's lookup asks it. */
	float Figure(const UCataclysmAbilitySystemComponent* System, const TCHAR* Stat)
	{
		return System->StatForSkill(FName(Stat), FGameplayTagContainer(), /*Fallback=*/0.0f);
	}

	/** The point `Index` of six on a circle of 3 m about the wearer: each 3 m from the wearer and from its neighbours. */
	FVector OnTheRing(int32 Index)
	{
		const float Radians = FMath::DegreesToRadians(60.0f * static_cast<float>(Index));
		return FVector(3.0f * M * FMath::Cos(Radians), 3.0f * M * FMath::Sin(Radians), 0.0f);
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarlordTenLoaderTest,
	"Cataclysm.SetBonus.Warlord10.TheRowsBecomeThreeWindowGrantsAndTwoEnemyCountsReadingTheDamageWindows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * What the loader makes of the five rows. Three grants on `movement_skill`, each one stack at most for 5 s with a
 * 30 s trigger cooldown, one for each window row. On each damage stat two modifiers: the window's, scaled by its own
 * stacks under a key, and the enemy count's, under THE SAME KEY, with the 8 m reach the loader states and a cap of 5.
 * Movement speed has the window's modifier alone. The engine's own check refuses none of the seven.
 */
bool FCataclysmWarlordTenLoaderTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusWarlordTenTest;
	FMade Made;
	if (!Make(*this, Made))
	{
		return false;
	}

	// THE CONTROL: the rows reached the loader at all.
	if (!TestEqual(TEXT("control: the rows made three stat lines"), Made.Totals.Num(), 3))
	{
		return false;
	}

	TestEqual(TEXT("three grants, one for each window row"), Made.Actions.Num(), 3);
	for (const FCataclysmPoolAction& Grant : Made.Actions)
	{
		TestEqual(TEXT("each grant is on movement_skill"), Grant.Event, FName(TEXT("movement_skill")));
		TestEqual(TEXT("each holds one stack at most"), Grant.StackCap, 1);
		TestEqual(TEXT("each lasts five seconds"), Grant.StackSeconds, WindowSeconds, 0.0001f);
		TestEqual(TEXT("each waits thirty seconds after it fires"), Grant.TriggerCooldownSeconds, CooldownSeconds,
				  0.0001f);
	}

	const TArray<FCataclysmStatModifier>* Speed = Made.Totals.Find(FName(TEXT("movement_speed")));
	TestTrue(TEXT("movement speed has one modifier, 100% more scaled by its own stacks"),
			 Speed && Speed->Num() == 1 && (*Speed)[0].Bucket == ECataclysmStatBucket::More
				 && FMath::IsNearlyEqual((*Speed)[0].Value, 100.0f)
				 && (*Speed)[0].Scale == ECataclysmStatScale::PerOwnStack);

	for (const TCHAR* Stat : {TEXT("attack_damage"), TEXT("spell_damage")})
	{
		const TArray<FCataclysmStatModifier>* Line = Made.Totals.Find(FName(Stat));
		const FCataclysmStatModifier* Window = nullptr;
		const FCataclysmStatModifier* PerEnemy = nullptr;
		if (Line)
		{
			for (const FCataclysmStatModifier& Each : *Line)
			{
				Window = Each.Scale == ECataclysmStatScale::PerOwnStack ? &Each : Window;
				PerEnemy = Each.Scale == ECataclysmStatScale::PerEnemyInReachWhileOwnStackHeld ? &Each : PerEnemy;
			}
		}
		if (!TestTrue(*FString::Printf(TEXT("%s has two modifiers, the window's and the enemy count's"), Stat),
				Line && Line->Num() == 2 && Window && PerEnemy))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s: the window is 50%% more under a key"), Stat),
				 Window->Bucket == ECataclysmStatBucket::More && FMath::IsNearlyEqual(Window->Value, 50.0f)
					 && !Window->StackKey.IsNone());
		TestEqual(*FString::Printf(TEXT("%s: the enemy count reads the window's own key"), Stat),
				  PerEnemy->StackKey, Window->StackKey);
		TestTrue(*FString::Printf(TEXT("%s: the enemy count is 10%% more a step, one enemy a step"), Stat),
				 PerEnemy->Bucket == ECataclysmStatBucket::More && FMath::IsNearlyEqual(PerEnemy->Value, 10.0f)
					 && FMath::IsNearlyEqual(PerEnemy->ScaleStep, 1.0f));
		TestEqual(*FString::Printf(TEXT("%s: the enemy count reaches 8 m, the loader's"), Stat),
				  PerEnemy->ReachMetres, UCataclysmItemModifiers::OwnStackWindowReachMetres, 0.0001f);
		TestEqual(TEXT("and that is the ruling's 8 m"), UCataclysmItemModifiers::OwnStackWindowReachMetres, 8.0f,
				  0.0001f);
		TestEqual(*FString::Printf(TEXT("%s: the enemy count stops at 5"), Stat), PerEnemy->ScaleMaxSteps, 5);
		TestTrue(*FString::Printf(TEXT("%s: the engine's own check refuses neither modifier"), Stat),
				 UCataclysmStatPipeline::ValidateModifier(*Window).IsEmpty()
					 && UCataclysmStatPipeline::ValidateModifier(*PerEnemy).IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarlordTenSpeedTest,
	"Cataclysm.SetBonus.Warlord10.AMovementUseDoublesTheSpeedTheMovementComponentIsGivenForFiveSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * WHETHER `more` ON `movement_speed` IS READ BY THE MOVEMENT CODE, which the reconnaissance could not settle: every
 * movement speed row merged before this one is `increased`. A player character wearing the movement speed row, on a
 * speed line of 5 m/s. The control is the speed its movement component is given with no window open. After a
 * movement use, the quarter-second step the character runs (`RefreshMovementSpeedIfItCanChangeUnannounced`) gives
 * the component twice the speed; 4.9 s after the use it is still twice; 5.1 s after, the plain speed again.
 *
 * THE STEP IS CALLED BY HAND, as the regeneration step would call it. The movement use's own announcement is heard
 * before the grant is made (`ActOnEvent` broadcasts first), so the speed rises at the next step and not on the use's
 * own frame; that lateness is read, not asserted here.
 */
bool FCataclysmWarlordTenSpeedTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusWarlordTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMade Made;
	if (!Make(*this, Made))
	{
		return false;
	}
	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* System = PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!TestNotNull(TEXT("set-up: an ability system"), System)
		|| !TestNotNull(TEXT("set-up: a movement component"), Movement))
	{
		return false;
	}
	constexpr float MetresPerSecond = 5.0f;
	System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetMovementSpeedAttribute(), MetresPerSecond);
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	TMap<FName, FCataclysmStatInputs> Lines;
	AddLine(Lines, Made, TEXT("movement_speed"), MetresPerSecond);
	System->SetStatInputs(MoveTemp(Lines));
	System->SetPoolActions(MoveTemp(Made.Actions));
	Character->RefreshMovementSpeed();

	const float Plain = Movement->MaxWalkSpeed;
	if (!TestEqual(TEXT("control: with no window open the component is given 5 m/s"), Plain,
			MetresPerSecond * ACataclysmPlayerCharacter::CentimetresPerMetre, 0.01f))
	{
		return false;
	}

	System->NoteMovementSkillUsed();
	Character->RefreshMovementSpeedIfItCanChangeUnannounced();
	TestEqual(TEXT("after a movement use the component is given twice the speed"), Movement->MaxWalkSpeed / Plain,
			  2.0f, 0.0001f);

	World->TimeSeconds += WindowSeconds - 0.1f;
	Character->RefreshMovementSpeedIfItCanChangeUnannounced();
	TestEqual(TEXT("4.9 s after the use it is still twice"), Movement->MaxWalkSpeed / Plain, 2.0f, 0.0001f);

	World->TimeSeconds += 0.2f;
	Character->RefreshMovementSpeedIfItCanChangeUnannounced();
	TestEqual(TEXT("5.1 s after the use it is the plain speed again"), Movement->MaxWalkSpeed / Plain, 1.0f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarlordTenDamageTest,
	"Cataclysm.SetBonus.Warlord10.AMovementUseGivesHalfAgainDamageForFiveSecondsAndOnlyOnceInThirty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The damage half, with nobody near. The control: with no window open both damage figures are their base. A movement
 * use gives 1.5 times on both; 4.9 s after it, still; 5.1 s after, the base again. A second movement use 10 s after
 * the first gives nothing, because the grant waits 30 s after it fires: this is "every 30 seconds". One 30.1 s after
 * the first gives the window again.
 */
bool FCataclysmWarlordTenDamageTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusWarlordTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	UCataclysmAbilitySystemComponent* System = DamageWearer(*this, World);
	if (!System)
	{
		return false;
	}

	const float Attack = Figure(System, TEXT("attack_damage"));
	const float Spell = Figure(System, TEXT("spell_damage"));
	if (!TestEqual(TEXT("control: with no window open attack damage is its base"), Attack, 100.0f, 0.01f)
		|| !TestEqual(TEXT("control: and so is spell damage"), Spell, 100.0f, 0.01f))
	{
		return false;
	}

	const double First = World->GetTimeSeconds();
	System->NoteMovementSkillUsed();
	TestEqual(TEXT("a movement use gives 50% more attack damage"), Figure(System, TEXT("attack_damage")) / Attack,
			  1.5f, 0.0001f);
	TestEqual(TEXT("and 50% more spell damage"), Figure(System, TEXT("spell_damage")) / Spell, 1.5f, 0.0001f);

	World->TimeSeconds = First + WindowSeconds - 0.1f;
	TestEqual(TEXT("4.9 s after the use it is still 50% more"), Figure(System, TEXT("attack_damage")) / Attack, 1.5f,
			  0.0001f);
	World->TimeSeconds = First + WindowSeconds + 0.1f;
	TestEqual(TEXT("5.1 s after the use it is the base again"), Figure(System, TEXT("attack_damage")) / Attack, 1.0f,
			  0.0001f);

	World->TimeSeconds = First + 10.0f;
	System->NoteMovementSkillUsed();
	TestEqual(TEXT("a second movement use 10 s after the first gives nothing"),
			  Figure(System, TEXT("attack_damage")) / Attack, 1.0f, 0.0001f);
	TestEqual(TEXT("and nothing to spell damage"), Figure(System, TEXT("spell_damage")) / Spell, 1.0f, 0.0001f);

	World->TimeSeconds = First + CooldownSeconds + 0.1f;
	System->NoteMovementSkillUsed();
	TestEqual(TEXT("one 30.1 s after the first gives 50% more again"), Figure(System, TEXT("attack_damage")) / Attack,
			  1.5f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarlordTenEnemiesTest,
	"Cataclysm.SetBonus.Warlord10.TheDamageRisesTenPercentMorePerEnemyWithinEightMetresAndStopsAtFive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The owner's ruling: +10% more damage per enemy within 8 m, capped at 5 enemies, on top of the 50% more. Two "more"
 * figures multiply, as every pair of "more" sources does in `UCataclysmStatPipeline::Accumulate`.
 *
 * In order. Control: alone, with no window, the base. A creature is spawned 9 m off and the window is opened: 1.5
 * times, so the one at 9 m is not counted. Creatures are spawned 3 m off one at a time, each 3 m from the one before,
 * and the reading is taken after each: each adds a tenth to the second factor, to 1.5 x 1.5 = 2.25 at five, and the
 * sixth adds nothing. Spell damage at six is 2.25 too. 5.1 s after the use, with six near, the base again: the count
 * gives nothing outside the window.
 */
bool FCataclysmWarlordTenEnemiesTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusWarlordTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	UCataclysmAbilitySystemComponent* System = DamageWearer(*this, World);
	if (!System)
	{
		return false;
	}

	const float Alone = Figure(System, TEXT("attack_damage"));
	if (!TestEqual(TEXT("control: alone, with no window open, attack damage is its base"), Alone, 100.0f, 0.01f))
	{
		return false;
	}

	// 9 M ALONG -X, so it is 9 m from the wearer and more than 6 m from every point of the ring below.
	if (!TestNotNull(TEXT("set-up: a creature 9 m off"),
			SpawnOn(World, FVector(-9.0f * M, 0.0f, 0.0f), ECataclysmTeam::Monsters)))
	{
		return false;
	}

	const double Used = World->GetTimeSeconds();
	System->NoteMovementSkillUsed();
	TestEqual(TEXT("with the window open and only the creature at 9 m about, 1.5 times"),
			  Figure(System, TEXT("attack_damage")) / Alone, 1.5f, 0.0001f);

	const float Expected[] = {1.65f, 1.8f, 1.95f, 2.1f, 2.25f, 2.25f};
	for (int32 Index = 0; Index < 6; ++Index)
	{
		if (!TestNotNull(TEXT("set-up: a creature 3 m off"),
				SpawnOn(World, OnTheRing(Index), ECataclysmTeam::Monsters)))
		{
			return false;
		}
		TestEqual(*FString::Printf(TEXT("with %d near and the one at 9 m, %.2f times"), Index + 1, Expected[Index]),
				  Figure(System, TEXT("attack_damage")) / Alone, Expected[Index], 0.0001f);
	}
	TestEqual(TEXT("spell damage with six near is 2.25 times too"), Figure(System, TEXT("spell_damage")) / 100.0f,
			  2.25f, 0.0001f);

	World->TimeSeconds = Used + WindowSeconds + 0.1f;
	TestEqual(TEXT("5.1 s after the use, with six near, attack damage is its base again"),
			  Figure(System, TEXT("attack_damage")) / Alone, 1.0f, 0.0001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
