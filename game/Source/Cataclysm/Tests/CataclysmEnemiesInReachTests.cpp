// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Items/CataclysmItem.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Rows that count the enemies standing near a character. Issue #1597.
 *
 * WHAT THESE ARE FOR. Five Ravager nodes are written as a count of nearby
 * enemies -- "+2% increased Attack Damage per point while an enemy is within 4
 * metres", "You take 15% less damage while three or more enemies are within 4
 * metres of you", "You deal 2% more damage for each enemy within 4 metres of
 * you", and the Wade In capstone option's armour and attack damage per enemy.
 * Two shapes serve all five: a condition, `EnemiesInReachAtLeast`, and a scale,
 * `PerEnemyInReach`.
 *
 * NOTHING IN ANY TEST HERE STATES A COUNT OR A DISTANCE TO THE PIPELINE. Every
 * case spawns characters and lets the game measure. That is deliberate and it is
 * the lesson recorded against the change before this one: five tests written for
 * the defender's distance reading each built their own state and filled it in
 * themselves, so the proof case that broke the line filling it failed NOTHING,
 * three times running. A test that supplies the missing step proves nothing.
 *
 * WHAT A CASE STATES IS WHAT THE ROW STATES: the reach in metres and the count
 * asked for. Where the characters stand is the input, and the stat is the
 * output.
 *
 * EVERY CHARACTER HERE IS AN ENEMY CHARACTER, THE ONE ON THE PLAYER'S SIDE
 * INCLUDED. `CataclysmTargetCandidatesTests.cpp` does the same and says why: a
 * player pawn's ability system lives on a player state that a synthetic world
 * has no controller to create. The side is set by hand, which is the only thing
 * the counting reads.
 */
namespace CataclysmEnemiesInReachTest
{
	/** Centimetres in a metre, so a case can place a character in metres. */
	constexpr float M = 100.0f;

	/** A character on the given side, harmless and hard to kill. */
	ACataclysmEnemyCharacter* SpawnOn(UWorld* World, const FVector& Where,
									  ECataclysmTeam Side)
	{
		ACataclysmEnemyCharacter* Made =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where,
													   FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(Side));
			Made->SetHealth(1'000'000.0f);
			Made->SetAttackDamage(0.0f);
		}
		return Made;
	}

	UCataclysmAbilitySystemComponent* SystemOf(ACataclysmEnemyCharacter* Character)
	{
		return Character
			? Cast<UCataclysmAbilitySystemComponent>(
				  Character->GetAbilitySystemComponent())
			: nullptr;
	}

	/**
	 * One row of a stat line, with every field stated.
	 *
	 * EVERY FIELD, INCLUDING THE ONES A CASE DOES NOT USE, so a reader of a case
	 * sees the whole row rather than the difference from a default they have to
	 * go and look up. The two that matter here are the last two: a row carries
	 * its own reach, and `ConditionValue` holds the count asked for while
	 * `ScaleStep` holds how many enemies one step is worth, so neither of those
	 * can also hold the distance.
	 */
	FCataclysmStatModifier Row(ECataclysmStatBucket Bucket, float Value,
							   ECataclysmStatCondition Condition,
							   float ConditionValue,
							   ECataclysmStatScale Scale, float ScaleStep,
							   float ReachMetres)
	{
		FCataclysmStatModifier Made;
		Made.Bucket = Bucket;
		Made.Source = ECataclysmModifierSource::PassiveKeystone;
		Made.Value = Value;
		Made.Condition = Condition;
		Made.ConditionValue = ConditionValue;
		Made.Scale = Scale;
		Made.ScaleStep = ScaleStep;
		Made.ReachMetres = ReachMetres;
		return Made;
	}

	/** A row that applies always and is worth its value: the unconditional case. */
	FCataclysmStatModifier Plain(ECataclysmStatBucket Bucket, float Value)
	{
		return Row(Bucket, Value, ECataclysmStatCondition::Always, 0.0f,
				   ECataclysmStatScale::Fixed, 0.0f, /*ReachMetres=*/-1.0f);
	}

	/** An attack damage line with a base of 100, carrying these rows. */
	void GiveLine(UCataclysmAbilitySystemComponent* System,
				  const TArray<FCataclysmStatModifier>& Rows)
	{
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line =
			Inputs.FindOrAdd(FName(UCataclysmItemModifiers::AttackDamageStat));
		Line.Base = 100.0f;
		Line.Modifiers = Rows;
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** What the pipeline says that line is worth at this instant. */
	float AttackDamage(UCataclysmAbilitySystemComponent* System)
	{
		return System->StatForSkill(
			FName(UCataclysmItemModifiers::AttackDamageStat),
			FGameplayTagContainer(), /*Fallback=*/0.0f);
	}

	/** A blow that may not critically strike, so two hits can be compared. */
	FCataclysmHitDelivery NoCritical()
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bCannotCriticallyStrike = true;
		return Delivery;
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is
// built as a unity blob and a `using namespace` at file scope reaches the other
// files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemiesInReachNearAndFarTest,
	"Cataclysm.EnemiesInReach.AnEnemyInsideTheReachRaisesTheStatAndOneBeyondItDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Ravager's `basic_spine_003`: more attack damage while an enemy is within
 * four metres.
 *
 * THREE READINGS OF ONE LINE, not two, and the middle one is the control. A
 * build that counted every hostile character whatever the distance would pass a
 * test that only checked "alone" against "surrounded". The creature ten metres
 * away is there to be refused.
 */
bool FCataclysmEnemiesInReachNearAndFarTest::RunTest(const FString&)
{
	using namespace CataclysmEnemiesInReachTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Ravager =
		SpawnOn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	UCataclysmAbilitySystemComponent* System = SystemOf(Ravager);
	if (!TestNotNull(TEXT("a character carrying the node"), System))
	{
		return false;
	}

	GiveLine(System, {Row(ECataclysmStatBucket::Increased, 50.0f,
						  ECataclysmStatCondition::EnemiesInReachAtLeast, 1.0f,
						  ECataclysmStatScale::Fixed, 0.0f,
						  /*ReachMetres=*/4.0f)});

	const float Alone = AttackDamage(System);

	// TEN METRES AWAY, WHICH IS OUTSIDE THE FOUR THE ROW NAMES.
	SpawnOn(World, FVector(10.0f * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	const float WithOneAcrossTheRoom = AttackDamage(System);

	// AND TWO METRES AWAY, ON A DIFFERENT AXIS so that a spawn refused for
	// overlapping another body cannot quietly move a character somewhere else.
	SpawnOn(World, FVector(0.0f, 2.0f * M, 0.0f), ECataclysmTeam::Monsters);
	const float WithOneClose = AttackDamage(System);

	TestEqual(TEXT("alone, the row grants nothing"), Alone, 100.0f, 0.01f);
	TestEqual(TEXT("a creature ten metres away is not within four"),
			  WithOneAcrossTheRoom, 100.0f, 0.01f);
	TestEqual(TEXT("one within four metres grants the whole increase"),
			  WithOneClose, 150.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemiesInReachCountTest,
	"Cataclysm.EnemiesInReach.ARowAskingForThreeIsNotSatisfiedByTwo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Ravager's `keystone_a_kA`: "while three or more enemies are within 4
 * metres of you".
 *
 * WHY THIS IS A SEPARATE CASE FROM THE ONE ABOVE. "While an enemy is within 4
 * metres" and "while three or more enemies are within 4 metres" are the same
 * condition with a different number, and a build that read the count as a
 * yes-or-no would pass the first case and fail this one. Two is the reading that
 * tells them apart.
 */
bool FCataclysmEnemiesInReachCountTest::RunTest(const FString&)
{
	using namespace CataclysmEnemiesInReachTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Ravager =
		SpawnOn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	UCataclysmAbilitySystemComponent* System = SystemOf(Ravager);
	if (!TestNotNull(TEXT("a character carrying the node"), System))
	{
		return false;
	}

	GiveLine(System, {Row(ECataclysmStatBucket::Increased, 50.0f,
						  ECataclysmStatCondition::EnemiesInReachAtLeast, 3.0f,
						  ECataclysmStatScale::Fixed, 0.0f,
						  /*ReachMetres=*/4.0f)});

	SpawnOn(World, FVector(0.0f, 2.0f * M, 0.0f), ECataclysmTeam::Monsters);
	const float WithOne = AttackDamage(System);

	SpawnOn(World, FVector(2.0f * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	const float WithTwo = AttackDamage(System);

	SpawnOn(World, FVector(0.0f, -2.0f * M, 0.0f), ECataclysmTeam::Monsters);
	const float WithThree = AttackDamage(System);

	TestEqual(TEXT("one is not three"), WithOne, 100.0f, 0.01f);
	TestEqual(TEXT("two is not three either"), WithTwo, 100.0f, 0.01f);
	TestEqual(TEXT("three is"), WithThree, 150.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemiesInReachScaleTest,
	"Cataclysm.EnemiesInReach.TheBonusGrowsWithEachEnemyInReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Ravager's `keystone_d_kB` and the Wade In capstone option: a size for each
 * enemy rather than a threshold.
 *
 * A CHARACTER ALONE GETS NOTHING BY THE ARITHMETIC AND NOT BY A SPECIAL CASE,
 * which is the first reading here. That is the case a build which forgot the
 * count would get wrong, in the player's favour and invisibly: a scale left
 * unread grants its full value at every state.
 */
bool FCataclysmEnemiesInReachScaleTest::RunTest(const FString&)
{
	using namespace CataclysmEnemiesInReachTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Ravager =
		SpawnOn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	UCataclysmAbilitySystemComponent* System = SystemOf(Ravager);
	if (!TestNotNull(TEXT("a character carrying the node"), System))
	{
		return false;
	}

	// TEN PER CENT FOR EVERY ONE OF THEM: a step of one enemy, worth ten.
	GiveLine(System, {Row(ECataclysmStatBucket::Increased, 10.0f,
						  ECataclysmStatCondition::Always, 0.0f,
						  ECataclysmStatScale::PerEnemyInReach,
						  /*ScaleStep=*/1.0f, /*ReachMetres=*/4.0f)});

	const float Alone = AttackDamage(System);

	SpawnOn(World, FVector(0.0f, 2.0f * M, 0.0f), ECataclysmTeam::Monsters);
	const float WithOne = AttackDamage(System);

	SpawnOn(World, FVector(2.0f * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	SpawnOn(World, FVector(0.0f, -2.0f * M, 0.0f), ECataclysmTeam::Monsters);
	const float WithThree = AttackDamage(System);

	TestEqual(TEXT("a character alone gets nothing"), Alone, 100.0f, 0.01f);
	TestEqual(TEXT("one enemy is worth ten per cent"), WithOne, 110.0f, 0.01f);
	TestEqual(TEXT("three are worth thirty"), WithThree, 130.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemiesInReachOwnReachTest,
	"Cataclysm.EnemiesInReach.EachRowCountsWithinItsOwnReachAndNotASharedOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two rows with different reaches, read in the same lookup against one creature
 * standing between them.
 *
 * THIS IS WHY THE LISTS RETURN DISTANCES AND NOT A COUNT. One walk is made, at
 * the widest reach any row asks for, and each row then counts the entries inside
 * its own. A build that walked once and shared the answer would grant both rows
 * or neither, and the creature at five metres is placed to make that visible:
 * inside the eight-metre row and outside the three-metre one.
 *
 * TWO SEPARATE READINGS ARE TAKEN, one line at a time, because a single attack
 * damage figure carrying both rows could not say which of the two granted it.
 */
bool FCataclysmEnemiesInReachOwnReachTest::RunTest(const FString&)
{
	using namespace CataclysmEnemiesInReachTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Ravager =
		SpawnOn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	UCataclysmAbilitySystemComponent* System = SystemOf(Ravager);
	if (!TestNotNull(TEXT("a character carrying the node"), System))
	{
		return false;
	}

	SpawnOn(World, FVector(5.0f * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);

	const auto WithReach = [&](float ReachMetres)
	{
		GiveLine(System, {Row(ECataclysmStatBucket::Increased, 50.0f,
							  ECataclysmStatCondition::EnemiesInReachAtLeast,
							  1.0f, ECataclysmStatScale::Fixed, 0.0f,
							  ReachMetres)});
		return AttackDamage(System);
	};

	const float ForThreeMetres = WithReach(3.0f);
	const float ForEightMetres = WithReach(8.0f);

	// AND BOTH ROWS TOGETHER, which is the arrangement a character with several
	// such nodes actually has. Only the eight-metre row may grant anything.
	GiveLine(System, {Row(ECataclysmStatBucket::Increased, 50.0f,
						  ECataclysmStatCondition::EnemiesInReachAtLeast, 1.0f,
						  ECataclysmStatScale::Fixed, 0.0f, 3.0f),
					  Row(ECataclysmStatBucket::Increased, 50.0f,
						  ECataclysmStatCondition::EnemiesInReachAtLeast, 1.0f,
						  ECataclysmStatScale::Fixed, 0.0f, 8.0f)});
	const float ForBoth = AttackDamage(System);

	TestEqual(TEXT("five metres is outside a three metre row"),
			  ForThreeMetres, 100.0f, 0.01f);
	TestEqual(TEXT("and inside an eight metre one"),
			  ForEightMetres, 150.0f, 0.01f);
	TestEqual(TEXT("carrying both, only the eight metre row grants anything"),
			  ForBoth, 150.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemiesInReachNoReachTest,
	"Cataclysm.EnemiesInReach.ARowNamingNoReachAndOneAskingForNoneGrantNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The two ways a row can be authored wrong, with a creature standing right next
 * to the character so that a build granting everything is visible.
 *
 * NEITHER MAY HOLD ALWAYS. A row whose reach was never authored carries -1 and
 * must count nobody rather than everybody; a row asking for "at least nought
 * enemies" is true of an empty room and would grant its bonus everywhere. Both
 * refuse, so a row authored wrong reads as a node granting nothing rather than
 * as a node granting everything.
 *
 * A THIRD ROW IS PRESENT AND UNCONDITIONAL, so that a reading of 100 cannot be
 * the pipeline having failed to run at all. The line is worth 120 throughout.
 */
bool FCataclysmEnemiesInReachNoReachTest::RunTest(const FString&)
{
	using namespace CataclysmEnemiesInReachTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Ravager =
		SpawnOn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	UCataclysmAbilitySystemComponent* System = SystemOf(Ravager);
	if (!TestNotNull(TEXT("a character carrying the node"), System))
	{
		return false;
	}

	SpawnOn(World, FVector(0.0f, 1.0f * M, 0.0f), ECataclysmTeam::Monsters);

	const FCataclysmStatModifier Unconditional =
		Plain(ECataclysmStatBucket::Increased, 20.0f);

	// A CONDITION WITH NO REACH: "at least one enemy within nowhere".
	GiveLine(System, {Unconditional,
					  Row(ECataclysmStatBucket::Increased, 50.0f,
						  ECataclysmStatCondition::EnemiesInReachAtLeast, 1.0f,
						  ECataclysmStatScale::Fixed, 0.0f,
						  /*ReachMetres=*/-1.0f)});
	const float ConditionWithoutAReach = AttackDamage(System);

	// A SCALE WITH NO REACH: a size for each enemy within nowhere.
	GiveLine(System, {Unconditional,
					  Row(ECataclysmStatBucket::Increased, 10.0f,
						  ECataclysmStatCondition::Always, 0.0f,
						  ECataclysmStatScale::PerEnemyInReach, 1.0f,
						  /*ReachMetres=*/-1.0f)});
	const float ScaleWithoutAReach = AttackDamage(System);

	// AND A COUNT OF NOUGHT, which is true of an empty room.
	GiveLine(System, {Unconditional,
					  Row(ECataclysmStatBucket::Increased, 50.0f,
						  ECataclysmStatCondition::EnemiesInReachAtLeast, 0.0f,
						  ECataclysmStatScale::Fixed, 0.0f,
						  /*ReachMetres=*/4.0f)});
	const float AtLeastNought = AttackDamage(System);

	TestEqual(TEXT("a condition naming no reach counts nobody"),
			  ConditionWithoutAReach, 120.0f, 0.01f);
	TestEqual(TEXT("a scale naming no reach is worth nothing"),
			  ScaleWithoutAReach, 120.0f, 0.01f);
	TestEqual(TEXT("at least nought enemies refuses rather than holding always"),
			  AtLeastNought, 120.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemiesInReachAlliesTest,
	"Cataclysm.EnemiesInReach.AnAllyStandingNextToYouIsNotAnEnemyInReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The control on the whole mechanism: it counts ENEMIES, not characters.
 *
 * A BUILD THAT COUNTED EVERY BODY NEARBY WOULD PASS EVERY CASE ABOVE, because
 * every character placed in them is on the other side. The minion standing at
 * the character's shoulder is what tells a proximity count from a hostility one,
 * and a Ritualist surrounded by its own summons is the arrangement in the game
 * where that difference is the whole answer.
 */
bool FCataclysmEnemiesInReachAlliesTest::RunTest(const FString&)
{
	using namespace CataclysmEnemiesInReachTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Ravager =
		SpawnOn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	UCataclysmAbilitySystemComponent* System = SystemOf(Ravager);
	if (!TestNotNull(TEXT("a character carrying the node"), System))
	{
		return false;
	}

	GiveLine(System, {Row(ECataclysmStatBucket::Increased, 10.0f,
						  ECataclysmStatCondition::Always, 0.0f,
						  ECataclysmStatScale::PerEnemyInReach, 1.0f,
						  /*ReachMetres=*/4.0f)});

	// TWO ON THE CHARACTER'S OWN SIDE, both closer than either enemy below.
	SpawnOn(World, FVector(0.0f, 1.0f * M, 0.0f), ECataclysmTeam::Players);
	SpawnOn(World, FVector(1.0f * M, 0.0f, 0.0f), ECataclysmTeam::Players);
	const float AmongAllies = AttackDamage(System);

	SpawnOn(World, FVector(0.0f, -2.0f * M, 0.0f), ECataclysmTeam::Monsters);
	const float AndOneEnemy = AttackDamage(System);

	TestEqual(TEXT("two allies at arm's length count for nothing"),
			  AmongAllies, 100.0f, 0.01f);
	TestEqual(TEXT("and the one enemy among them is worth exactly one"),
			  AndOneEnemy, 110.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemiesInReachDeadEnemyTest,
	"Cataclysm.EnemiesInReach.ADeadEnemyStandingNextToYouCountsForNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A creature that has been killed stops counting, and it is the HOSTILITY test
 * that decides so rather than the side filter.
 *
 * THIS IS THE ONLY CASE IN THIS FILE THAT REACHES THAT TEST, and it exists
 * because a guard proof showed the others cannot. Removing
 * `UCataclysmTargeting::IsHostileTo` from the walk failed none of the other
 * seven: in the ally case the searcher is on the players' side and not maddened,
 * so the side filter looks only at that side's MADDENED characters, an empty
 * list, and an ally never arrives at the hostility test at all.
 *
 * `UCataclysmTargeting::MatchesAttitude` refuses a dead character before it
 * compares sides, so a corpse on the other side passes the side filter and is
 * refused only there.
 *
 * NOT A CONTRIVANCE TO COVER A LINE. A Ravager fighting a crowd is killing the
 * crowd, and a bonus that kept counting the bodies would be largest just after a
 * fight rather than during one. The class comment on
 * `UCataclysmTargetCandidates` records that a corpse is left standing for a
 * while before it is removed, so this is the ordinary state of a fight.
 *
 * TWO ENEMIES AND ONE DEATH, so the reading before the death is the control. A
 * build that counted corpses would report the same number twice, and asserting
 * one figure alone could not tell that from a build that counted correctly.
 */
bool FCataclysmEnemiesInReachDeadEnemyTest::RunTest(const FString&)
{
	using namespace CataclysmEnemiesInReachTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Ravager =
		SpawnOn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	UCataclysmAbilitySystemComponent* System = SystemOf(Ravager);
	if (!TestNotNull(TEXT("a character carrying the node"), System))
	{
		return false;
	}

	GiveLine(System, {Row(ECataclysmStatBucket::Increased, 10.0f,
						  ECataclysmStatCondition::Always, 0.0f,
						  ECataclysmStatScale::PerEnemyInReach, 1.0f,
						  /*ReachMetres=*/4.0f)});

	ACataclysmEnemyCharacter* Living =
		SpawnOn(World, FVector(0.0f, 2.0f * M, 0.0f), ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Killed =
		SpawnOn(World, FVector(2.0f * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	if (!TestNotNull(TEXT("one enemy that lives"), Living)
		|| !TestNotNull(TEXT("one enemy to kill"), Killed))
	{
		return false;
	}

	const float WithBothAlive = AttackDamage(System);

	// KILLED THE WAY EVERY DEATH IN THE GAME IS RECORDED, so the test is not
	// asserting against a state only a test can produce.
	const bool bMarked = UCataclysmSkillEffects::MarkDead(Killed);
	if (!TestTrue(TEXT("the second enemy was actually marked dead"), bMarked))
	{
		return false;
	}

	const float WithOneDead = AttackDamage(System);

	TestEqual(TEXT("two living enemies are worth twenty per cent"),
			  WithBothAlive, 120.0f, 0.01f);
	TestEqual(TEXT("and the corpse among them counts for nothing"),
			  WithOneDead, 110.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemiesInReachRealBlowTest,
	"Cataclysm.EnemiesInReach.TheCountReachesARealBlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole chain, from where characters stand to what reached health.
 *
 * THE TARGET IS TEN METRES AWAY AND THE ROW REACHES FOUR, so the creature being
 * hit cannot itself be the enemy being counted. That is the trap this
 * arrangement exists to avoid: a target within reach would make the row true on
 * every blow, and the case would pass against a build that counted only the
 * character it was hitting.
 *
 * TWO BLOWS ON THE SAME TARGET, with one bystander arriving between them. The
 * bystander takes no part in either blow.
 */
bool FCataclysmEnemiesInReachRealBlowTest::RunTest(const FString&)
{
	using namespace CataclysmEnemiesInReachTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Ravager =
		SpawnOn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* Target =
		SpawnOn(World, FVector(10.0f * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmAbilitySystemComponent* System = SystemOf(Ravager);
	if (!TestNotNull(TEXT("a character carrying the node"), System)
		|| !TestNotNull(TEXT("something to hit"), Target))
	{
		return false;
	}

	Ravager->SetAttackDamage(100.0f);
	GiveLine(System, {Row(ECataclysmStatBucket::Increased, 50.0f,
						  ECataclysmStatCondition::EnemiesInReachAtLeast, 1.0f,
						  ECataclysmStatScale::Fixed, 0.0f,
						  /*ReachMetres=*/4.0f)});

	const auto Strike = [&]()
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Ravager, Target, 100.0f,
										 FGameplayTagContainer(), NoCritical(),
										 &Resolved);
		return Resolved;
	};

	const FCataclysmDamageResult Unsurrounded = Strike();
	SpawnOn(World, FVector(0.0f, 2.0f * M, 0.0f), ECataclysmTeam::Monsters);
	const FCataclysmDamageResult Surrounded = Strike();

	if (!TestTrue(TEXT("both blows landed"),
				  Unsurrounded.DealtToHealth > 0.0f
					  && Surrounded.DealtToHealth > 0.0f))
	{
		return false;
	}
	TestFalse(TEXT("neither blow was evaded"),
			  Unsurrounded.bEvaded || Surrounded.bEvaded);
	TestFalse(TEXT("neither blow was blocked"),
			  Unsurrounded.bBlocked || Surrounded.bBlocked);
	TestFalse(TEXT("neither blow critically struck"),
			  Unsurrounded.bWasCritical || Surrounded.bWasCritical);

	TestEqual(TEXT("a bystander two metres away is worth half again"),
			  Surrounded.DealtToHealth / Unsurrounded.DealtToHealth, 1.5f, 0.01f);

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
