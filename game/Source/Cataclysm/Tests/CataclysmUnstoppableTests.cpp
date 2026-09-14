// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * `Ravager_keystone_spine_003` Unstoppable. Issue #1515.
 *
 * "You cannot be stunned, slowed or knocked back while an enemy is within 4
 * metres of you."
 *
 * TWO OF THE THREE CLAUSES ARE BUILDABLE AND THE THIRD IS NOT. Stuns and shoves
 * both pass through `UCataclysmSkillEffects::AfterCrowdControlResistance`, so a
 * hundred crowd control resistance is what "cannot be stunned or knocked back"
 * means. **"Slowed" HAS NO MECHANIC AT ALL**: `game/Data/StatusEffects.csv` has
 * no slow row, its `EffectKind` column only ever holds Buff, Debuff or DoT, and
 * movement reductions come from dungeon rules rather than from anything a
 * character can resist. So there is nothing for the third clause to forbid, and
 * nothing here pretends otherwise.
 *
 * THE NODE NEEDS NO NEW STAT. `crowd_control_resistance` already exists, is
 * already in the stat-name map, and already means "shorten this by that
 * percentage, and at 100 refuse it". The whole of this change is that
 * `AfterCrowdControlResistance` now ASKS THE STAT PIPELINE instead of reading
 * the gameplay attribute, because a CONDITIONED row is never folded into an
 * attribute and this node's row carries a condition.
 *
 * NOTHING HERE STATES A COUNT OR A DISTANCE TO THE PIPELINE, which is the rule
 * `CataclysmEnemiesInReachTests.cpp` records: every case spawns characters and
 * lets the game measure. A test that supplies the missing step proves nothing.
 *
 * EVERY CHARACTER IS AN ENEMY CHARACTER, THE ONE CARRYING THE NODE INCLUDED,
 * and its side is set by hand. A player pawn's ability system lives on a player
 * state that a synthetic world has no controller to create.
 */
namespace CataclysmUnstoppableTest
{
	/** Centimetres in a metre, so a case can place a character in metres. */
	constexpr float M = 100.0f;

	/** The reach the row names. */
	constexpr float FourMetres = 4.0f;

	/** What the node grants: enough to refuse a crowd control effect outright. */
	constexpr float WholeResistance = 100.0f;

	/** A stun long enough that a shortened one would still be a stun. */
	constexpr float ThreeSeconds = 3.0f;

	/** A shove far enough that a halved one would still move a body. */
	constexpr float FourMetreShove = 400.0f;

	/**
	 * A character on the given side, harmless, hard to kill, and not a boss.
	 *
	 * COMMON RARITY IS NOT OPTIONAL. A boss cannot be stunned at all, so without
	 * it the stun cases below would be answered by that rule rather than by the
	 * one they are about. `CataclysmCrowdControlResistanceTests.cpp` learned the
	 * same thing.
	 */
	ACataclysmEnemyCharacter* SpawnOn(UWorld* World, const FVector& Where,
									  ECataclysmTeam Side)
	{
		ACataclysmEnemyCharacter* Made =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where,
														FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(Side));
			Made->SetRarityStep(0);
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
	 * The node's row: the whole resistance, while at least one enemy stands
	 * within four metres.
	 *
	 * FLAT RATHER THAN INCREASED, because the stat has no base to increase. A
	 * character starts at zero crowd control resistance, and "increased" would
	 * multiply that nothing by a percentage and grant nothing.
	 */
	FCataclysmStatModifier UnstoppableRow()
	{
		FCataclysmStatModifier Made;
		Made.Bucket = ECataclysmStatBucket::Flat;
		Made.Source = ECataclysmModifierSource::PassiveKeystone;
		Made.Value = WholeResistance;
		Made.Condition = ECataclysmStatCondition::EnemiesInReachAtLeast;
		Made.ConditionValue = 1.0f;
		Made.Scale = ECataclysmStatScale::Fixed;
		Made.ScaleStep = 0.0f;
		Made.ReachMetres = FourMetres;
		return Made;
	}

	/** Give this character the node, on a crowd control resistance line of zero. */
	void GiveTheNode(UCataclysmAbilitySystemComponent* System)
	{
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(
			FName(UCataclysmSkillEffects::CrowdControlResistanceStat));
		Line.Base = 0.0f;
		Line.Modifiers = {UnstoppableRow()};
		System->SetStatInputs(MoveTemp(Inputs));
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is
// built as a unity blob and a `using namespace` at file scope reaches the other
// files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUnstoppableStatNameTest,
	"Cataclysm.Unstoppable.TheStatNameIsTheOneTheMapKnows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The name this code asks for is the name the data is written against.
 *
 * WHY A TEST RATHER THAN CARE. The stat name appears in three places -- this
 * constant, the key in `UCataclysmPlayerClassStats::StatToAttribute`, and the
 * `Stat` column of the rows. A character that disagreed by ONE CHARACTER would
 * be granted the row and never read it, and nothing anywhere would say so: the
 * pipeline would answer the fallback, the node would do nothing, and every test
 * that writes the attribute directly would still pass.
 */
bool FCataclysmUnstoppableStatNameTest::RunTest(const FString&)
{
	using namespace CataclysmUnstoppableTest;

	const FString Name(UCataclysmSkillEffects::CrowdControlResistanceStat);
	const TMap<FString, FGameplayAttribute>& Map =
		UCataclysmPlayerClassStats::StatToAttribute();

	const FGameplayAttribute* Found = Map.Find(Name);
	if (!TestNotNull(*FString::Printf(
			TEXT("the stat name '%s' is a key in the stat-name map"), *Name),
					 Found))
	{
		return false;
	}

	TestTrue(TEXT("and it names the crowd control resistance attribute"),
			 *Found ==
				 UCataclysmCombatAttributeSet::
					 GetCrowdControlResistanceAttribute());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUnstoppableReachTest,
	"Cataclysm.Unstoppable.TheResistanceArrivesOnlyWhileAnEnemyIsWithinFourMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Three readings of one character, and the middle one is the control.
 *
 * A BUILD THAT COUNTED EVERY HOSTILE CHARACTER WHATEVER THE DISTANCE would pass
 * a test that only checked "alone" against "surrounded". The creature ten metres
 * away is there to be refused.
 *
 * AND THREE READINGS OF THE SAME CHARACTER IS ALSO THE POINT. This node's
 * condition turns true and false as bodies move, so the answer has to be asked
 * afresh every time the effect lands rather than worked out once and kept. A
 * stat asked once and cached would answer the first of these three for ever.
 */
bool FCataclysmUnstoppableReachTest::RunTest(const FString&)
{
	using namespace CataclysmUnstoppableTest;

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
	GiveTheNode(System);

	const float Alone = UCataclysmSkillEffects::AfterCrowdControlResistance(
		Ravager, ThreeSeconds);

	// TEN METRES AWAY, WHICH IS OUTSIDE THE FOUR THE ROW NAMES.
	SpawnOn(World, FVector(10.0f * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	const float WithOneAcrossTheRoom =
		UCataclysmSkillEffects::AfterCrowdControlResistance(Ravager,
														   ThreeSeconds);

	// AND TWO METRES AWAY, ON A DIFFERENT AXIS so that a spawn refused for
	// overlapping another body cannot quietly move a character somewhere else.
	SpawnOn(World, FVector(0.0f, 2.0f * M, 0.0f), ECataclysmTeam::Monsters);
	const float WithOneClose =
		UCataclysmSkillEffects::AfterCrowdControlResistance(Ravager,
														   ThreeSeconds);

	TestEqual(TEXT("alone, the node grants nothing and the whole stun stands"),
			  Alone, ThreeSeconds, 0.001f);
	TestEqual(TEXT("a creature ten metres away is not within four, so the stun "
				   "still stands"),
			  WithOneAcrossTheRoom, ThreeSeconds, 0.001f);
	TestEqual(TEXT("one within four metres refuses the stun entirely"),
			  WithOneClose, 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUnstoppableStunTest,
	"Cataclysm.Unstoppable.ARealStunIsRefusedWhileAnEnemyIsNear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * End to end through `ApplyStun`, not only through the stat function.
 *
 * A RULE NOTHING ASKS FOR IS A RULE THAT DOES NOT RUN, and this project has
 * shipped that mistake before. The reading above says what the stat is worth;
 * this says a stun actually fails to land.
 *
 * TWO CHARACTERS RATHER THAN ONE IN TWO STATES. A stunned target cannot be
 * stunned again for five seconds, so a second attempt on the same body would be
 * refused by that rule instead of the one under test.
 */
bool FCataclysmUnstoppableStunTest::RunTest(const FString&)
{
	using namespace CataclysmUnstoppableTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE ATTACKER NEEDS AN ABILITY SYSTEM OF ITS OWN, because the stun reads one
	// off the instigator to build its effect context. It stands ten metres off so
	// that IT is never what satisfies the node's condition.
	ACataclysmEnemyCharacter* Attacker =
		SpawnOn(World, FVector(0.0f, 10.0f * M, 0.0f), ECataclysmTeam::Monsters);

	ACataclysmEnemyCharacter* Crowded =
		SpawnOn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* Alone =
		SpawnOn(World, FVector(30.0f * M, 0.0f, 0.0f), ECataclysmTeam::Players);
	UCataclysmAbilitySystemComponent* CrowdedSystem = SystemOf(Crowded);
	UCataclysmAbilitySystemComponent* AloneSystem = SystemOf(Alone);
	if (!TestNotNull(TEXT("a crowded character"), CrowdedSystem) ||
		!TestNotNull(TEXT("a character standing alone"), AloneSystem))
	{
		return false;
	}

	// BOTH CARRY THE NODE. The only difference between them is what is standing
	// near them, which is what the node reads.
	GiveTheNode(CrowdedSystem);
	GiveTheNode(AloneSystem);

	SpawnOn(World, FVector(2.0f * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);

	TestFalse(TEXT("a stun does not land while an enemy is within four metres"),
			  UCataclysmSkillEffects::ApplyStun(Attacker, Crowded, ThreeSeconds,
												/*DamageDealt=*/0.0f,
												/*bStunIsDesigned=*/true));
	TestFalse(TEXT("and that character is not stunned"),
			  UCataclysmSkillEffects::IsStunned(Crowded));

	// AND THE SAME NODE ON A CHARACTER WITH NOBODY NEAR STOPS NOTHING, which is
	// what says the refusal above came from the condition rather than from the
	// node granting resistance all the time.
	TestTrue(TEXT("the same stun lands on a character carrying the same node "
				  "with nobody near it"),
			 UCataclysmSkillEffects::ApplyStun(Attacker, Alone, ThreeSeconds,
											   /*DamageDealt=*/0.0f,
											   /*bStunIsDesigned=*/true));
	TestTrue(TEXT("and that character is stunned"),
			 UCataclysmSkillEffects::IsStunned(Alone));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUnstoppableShoveTest,
	"Cataclysm.Unstoppable.ARealShoveIsRefusedWhileAnEnemyIsNear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The other half of the row, through the other channel.
 *
 * A STUN AND A SHOVE REACH THE RESISTANCE BY DIFFERENT ROUTES. `ApplyStun` calls
 * it directly; a knockback, a pull, a drag and a launch are one body with four
 * directions and that body calls it. A change that fixed one and not the other
 * would satisfy the stun test above and leave half the row unbuilt.
 *
 * MEASURED AS A DISTANCE MOVED, not as a return value, because the return value
 * would be true for a shove of any length.
 */
bool FCataclysmUnstoppableShoveTest::RunTest(const FString&)
{
	using namespace CataclysmUnstoppableTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Attacker =
		SpawnOn(World, FVector(0.0f, 10.0f * M, 0.0f), ECataclysmTeam::Monsters);

	auto ShovedWhenCrowdedBy = [&](bool bSomebodyNear, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Target =
			SpawnOn(World, Where, ECataclysmTeam::Players);
		UCataclysmAbilitySystemComponent* System = SystemOf(Target);
		if (!System)
		{
			return -1.0f;
		}
		GiveTheNode(System);

		if (bSomebodyNear)
		{
			SpawnOn(World, Where + FVector(2.0f * M, 0.0f, 0.0f),
					ECataclysmTeam::Monsters);
		}

		const FVector Before = Target->GetActorLocation();
		UCataclysmSkillEffects::ApplyKnockback(Attacker, Target,
											   FourMetreShove);
		return static_cast<float>(
			(Target->GetActorLocation() - Before).Size());
	};

	// ALONE FIRST, so a shove that moved nobody at all would fail here rather
	// than pass as the node working.
	const float Alone = ShovedWhenCrowdedBy(false, FVector::ZeroVector);
	TestTrue(TEXT("a character carrying the node with nobody near it is still "
				  "shoved"),
			 Alone > 1.0f);

	const float Crowded =
		ShovedWhenCrowdedBy(true, FVector(30.0f * M, 0.0f, 0.0f));
	TestEqual(TEXT("and the same node with an enemy within four metres refuses "
				   "the shove entirely"),
			  Crowded, 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUnstoppableWrittenAttributeTest,
	"Cataclysm.Unstoppable.ACharacterThePipelineKnowsNothingAboutStillResists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The fallback, which is what stops this change breaking every creature.
 *
 * WHAT THIS GUARDS. `StatForSkill` answers the fallback when the stat line holds
 * no entry for the stat, and that is the case for EVERY creature in the game and
 * for a player before its first refresh. The attribute is passed as that
 * fallback, so a written attribute goes on meaning exactly what it meant.
 *
 * A version of this change that asked the pipeline and DROPPED the attribute
 * would leave the four tests in `CataclysmCrowdControlResistanceTests.cpp`
 * failing and the enemy modifier Unyielding doing nothing. This says so in one
 * place rather than leaving it to be inferred from those failures.
 */
bool FCataclysmUnstoppableWrittenAttributeTest::RunTest(const FString&)
{
	using namespace CataclysmUnstoppableTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Creature =
		SpawnOn(World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	UCataclysmAbilitySystemComponent* System = SystemOf(Creature);
	if (!TestNotNull(TEXT("a creature"), System))
	{
		return false;
	}

	// NOTHING IS GIVEN TO THE PIPELINE. This creature has no stat line at all,
	// which is the state every creature in the game is in.
	System->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetCrowdControlResistanceAttribute(),
		50.0f);

	TestEqual(TEXT("a written attribute still halves a stun, with no stat line "
				   "anywhere"),
			  UCataclysmSkillEffects::AfterCrowdControlResistance(Creature,
																  ThreeSeconds),
			  ThreeSeconds * 0.5f, 0.001f);

	System->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetCrowdControlResistanceAttribute(),
		WholeResistance);

	TestEqual(TEXT("and a written hundred still refuses it outright"),
			  UCataclysmSkillEffects::AfterCrowdControlResistance(Creature,
																  ThreeSeconds),
			  0.0f, 0.001f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
