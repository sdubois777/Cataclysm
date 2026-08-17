// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for holding a character still.
 *
 * WHAT THESE GUARD. Issue #371: the Brute's Stomp is designed to stun for 1.5
 * seconds and nothing in the project could stun anything. Section VI of
 * docs/Cataclysm_GDD_v2.md defines a stun as the target being unable to act at
 * all and gives it three rules against stun-locking, and
 * sim/cataclysm_sim/damage.py models two of them -- it says in terms that it
 * resolves one hit with no clock and that the game enforces the third, the five
 * second immunity window. This is that enforcement, so it exists nowhere else
 * and nothing else checks it.
 *
 * WHAT THEY DELIBERATELY DO NOT COVER: that the tag falls off when its duration
 * ends. Expiry belongs to the Gameplay Ability System's duration effects, which
 * ApplyTagForDuration already used for burn and for every buff before this
 * change existed, and it runs on the world's timer manager -- which a world
 * built by UWorld::CreateWorld never ticks. Nothing this change wrote decides
 * when a stun ends. What is new here is which stuns are allowed to start and
 * what a stunned character may do, and both of those are checked below.
 */

namespace CataclysmStunTest
{
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}

	/** A character with health, on a side, able to be an instigator. */
	struct FScopedFighter
	{
		FScopedFighter(UWorld* World, const FVector& Where, ECataclysmTeam Team,
					   float Health = 1000.0f)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(
				Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(Team));

			// SetHealth sets the MAXIMUM, which is what the damage threshold is
			// a percentage of. A thousand makes the threshold a round hundred.
			Actor->SetHealth(Health);
			Actor->SetAttackDamage(50.0f);
		}

		~FScopedFighter()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		ACataclysmEnemyCharacter* Actor = nullptr;
	};
}

// --------------------------------------------------------------------------
// The vocabulary
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStunTagsExistTest,
	"Cataclysm.Stun.TheTwoStunTagsExistInTheVocabulary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStunTagsExistTest::RunTest(const FString&)
{
	// WITHOUT THIS THE WHOLE FEATURE FAILS SILENTLY AND PASSES EVERY OTHER
	// TEST THAT DOES NOT LOOK. Both tags are requested by name with
	// ErrorIfNotFound false, so a vocabulary that has lost them returns an
	// invalid tag, ApplyTagForDuration refuses it, ApplyStun returns false, and
	// nothing anywhere reports that stunning stopped working. They come from the
	// Tags sheet of docs/All_Things_Cataclysm.xlsx by way of
	// tools/generate_gameplay_tags.py, so an edit to the workbook can remove
	// them without touching a line of C++.
	TestTrue(TEXT("State.Stunned is a known tag"),
		UCataclysmSkillEffects::StunnedTag().IsValid());
	TestTrue(TEXT("State.StunImmune is a known tag"),
		UCataclysmSkillEffects::StunImmuneTag().IsValid());

	// Not the same tag, which a copy-paste in either accessor would make them.
	TestNotEqual(TEXT("and they are two different tags"),
		UCataclysmSkillEffects::StunnedTag(),
		UCataclysmSkillEffects::StunImmuneTag());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNothingIsStunnedByDefaultTest,
	"Cataclysm.Stun.AskingWhetherNothingIsStunnedAnswersNo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmNothingIsStunnedByDefaultTest::RunTest(const FString&)
{
	using namespace CataclysmStunTest;

	// THE FAILURE MODE THIS GUARDS IS THE WORST ONE THE FEATURE HAS. IsStunned
	// gates the player's movement and every skill, so an implementation that
	// answered "yes" when it could not tell would freeze the character for the
	// rest of the session with nothing on screen explaining why. It has to fail
	// toward being able to act.
	TestFalse(TEXT("a null actor is not stunned"),
		UCataclysmSkillEffects::IsStunned(nullptr));

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// Something real, with an ability system, that simply has not been stunned.
	FScopedFighter Fighter(World, FVector::ZeroVector, ECataclysmTeam::Players);
	TestFalse(TEXT("and a character nobody has stunned is not stunned"),
		UCataclysmSkillEffects::IsStunned(Fighter.Actor));

	// An actor with no ability system at all, which is what a piece of scenery
	// is, and what a pawn whose player state has not arrived yet looks like.
	AActor* Scenery = World->SpawnActor<AActor>();
	if (!Scenery)
	{
		AddError(TEXT("Could not spawn a plain actor."));
		return false;
	}
	TestFalse(TEXT("and something with no ability system is not stunned"),
		UCataclysmSkillEffects::IsStunned(Scenery));
	Scenery->Destroy();

	return true;
}

// --------------------------------------------------------------------------
// The three anti-stun-lock rules
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSmallHitsDoNotStunTest,
	"Cataclysm.Stun.AHitTooSmallToMatterDoesNotStun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSmallHitsDoNotStunTest::RunTest(const FString&)
{
	using namespace CataclysmStunTest;

	// THE FIRST ANTI-STUN-LOCK RULE. docs/Cataclysm_GDD_v2.md:1520 -- "A hit
	// must take at least 10% of the target's maximum health to stun", which
	// stops constant interruption by small hits. STUN_DAMAGE_THRESHOLD in
	// sim/cataclysm_sim/damage.py is the same 10.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Attacker(World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	FScopedFighter Defender(World, FVector(100.0f, 0.0f, 0.0f),
							ECataclysmTeam::Players, /*Health=*/1000.0f);

	// A thousand maximum health puts the bar at exactly one hundred.
	const float Threshold =
		1000.0f * UCataclysmSkillEffects::StunDamageThresholdPercent / 100.0f;

	TestFalse(TEXT("a hit one point under the bar does not stun"),
		UCataclysmSkillEffects::ApplyStun(
			Attacker.Actor, Defender.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/Threshold - 1.0f, /*bStunIsDesigned=*/false));
	TestFalse(TEXT("and the target is not stunned"),
		UCataclysmSkillEffects::IsStunned(Defender.Actor));

	// THE OTHER HALF, or the test above would pass on a stun that never works.
	TestTrue(TEXT("a hit exactly on the bar does stun"),
		UCataclysmSkillEffects::ApplyStun(
			Attacker.Actor, Defender.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/Threshold, /*bStunIsDesigned=*/false));
	TestTrue(TEXT("and the target is stunned"),
		UCataclysmSkillEffects::IsStunned(Defender.Actor));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDesignedStunIgnoresThresholdTest,
	"Cataclysm.Stun.ADesignedStunLandsWithoutClearingTheDamageThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDesignedStunIgnoresThresholdTest::RunTest(const FString&)
{
	using namespace CataclysmStunTest;

	// THE EXEMPTION, AND IT IS THE ONE THE BRUTE'S STOMP RELIES ON. An attack
	// whose whole purpose is to stun should not fail to because the target has
	// a large health pool. stun_is_designed in sim/cataclysm_sim/damage.py says
	// so, and says equally that it does not exempt the immunity window.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Attacker(World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	FScopedFighter Defender(World, FVector(100.0f, 0.0f, 0.0f),
							ECataclysmTeam::Players, /*Health=*/1000.0f);

	// One point of damage against a thousand health: a hundredth of the bar.
	TestTrue(TEXT("a designed stun lands on a scratch"),
		UCataclysmSkillEffects::ApplyStun(
			Attacker.Actor, Defender.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/1.0f, /*bStunIsDesigned=*/true));
	TestTrue(TEXT("and the target is stunned"),
		UCataclysmSkillEffects::IsStunned(Defender.Actor));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStunImmunityWindowTest,
	"Cataclysm.Stun.AStunnedTargetCannotBeStunnedAgainInsideTheWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStunImmunityWindowTest::RunTest(const FString&)
{
	using namespace CataclysmStunTest;

	// THE SECOND ANTI-STUN-LOCK RULE, and the one that existed nowhere before.
	// sim/cataclysm_sim/damage.py:150 says outright that the window is a rule
	// about time, that the module resolves one hit with no clock, and that the
	// game enforces it. The game had no stun, so nothing enforced it anywhere.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Attacker(World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	FScopedFighter Defender(World, FVector(100.0f, 0.0f, 0.0f),
							ECataclysmTeam::Players, /*Health=*/1000.0f);

	TestTrue(TEXT("the first stun lands"),
		UCataclysmSkillEffects::ApplyStun(
			Attacker.Actor, Defender.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/1.0f, /*bStunIsDesigned=*/true));

	// A SECOND DESIGNED STUN, which skips the damage threshold, so the window is
	// the only thing that can refuse it. That is what makes this test about the
	// window rather than about the threshold.
	TestFalse(TEXT("a second stun inside the window is refused"),
		UCataclysmSkillEffects::ApplyStun(
			Attacker.Actor, Defender.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/1.0f, /*bStunIsDesigned=*/true));

	// A DIFFERENT ATTACKER IS STILL REFUSED. The window belongs to the target,
	// not to whoever swung, or two Brutes would chain a player indefinitely --
	// which is the exact failure the rule was written to prevent.
	FScopedFighter Other(World, FVector(200.0f, 0.0f, 0.0f),
						 ECataclysmTeam::Monsters);
	TestFalse(TEXT("and a second attacker is refused too"),
		UCataclysmSkillEffects::ApplyStun(
			Other.Actor, Defender.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/1.0f, /*bStunIsDesigned=*/true));

	// SOMEBODY ELSE IS NOT IMMUNE, or the test above would pass on an ApplyStun
	// that had simply stopped working after its first call.
	TestTrue(TEXT("but a different target can still be stunned"),
		UCataclysmSkillEffects::ApplyStun(
			Attacker.Actor, Other.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/1.0f, /*bStunIsDesigned=*/true));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStunWindowIsLongerThanTheStunTest,
	"Cataclysm.Stun.TheImmunityWindowOutlastsTheLongestStunTheGameCanApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStunWindowIsLongerThanTheStunTest::RunTest(const FString&)
{
	// THE PROPERTY THAT MAKES THE WINDOW MEAN ANYTHING. If a stun could outlast
	// the immunity it grants, a target would leave one stun already eligible for
	// the next and the rule would stop nothing. The Brute's Stomp is the longest
	// stun in the design at 1.5 seconds against a 5 second window.
	// LONGEST_DESIGNED_STUN in sim/cataclysm_sim/enemy_abilities.py is the same
	// 1.5 and is asserted at import time there.
	TestTrue(FString::Printf(
		TEXT("the %.1f second window outlasts the 1.5 second Stomp"),
		UCataclysmSkillEffects::StunImmunityWindowSeconds),
		UCataclysmSkillEffects::StunImmunityWindowSeconds > 1.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeadThingsAreNotStunnedTest,
	"Cataclysm.Stun.AnOrdinaryHitDoesNotStunSomethingAlreadyDead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeadThingsAreNotStunnedTest::RunTest(const FString&)
{
	using namespace CataclysmStunTest;

	// can_be_stunned in sim/cataclysm_sim/damage.py rejects a defender at zero
	// health before it applies the threshold, so a killing blow does not stun a
	// corpse. Copied here rather than reasoned about.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Attacker(World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	FScopedFighter Defender(World, FVector(100.0f, 0.0f, 0.0f),
							ECataclysmTeam::Players, /*Health=*/1000.0f);

	UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Defender.Actor);
	if (!AbilitySystem)
	{
		AddError(TEXT("The defender has no ability system component."));
		return false;
	}
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);

	TestFalse(TEXT("a hit on something at zero health does not stun it"),
		UCataclysmSkillEffects::ApplyStun(
			Attacker.Actor, Defender.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/1000.0f, /*bStunIsDesigned=*/false));

	return true;
}

// --------------------------------------------------------------------------
// Refusals that are not rules
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmZeroLengthStunIsNoStunTest,
	"Cataclysm.Stun.AStunOfNoLengthIsNotApplied",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmZeroLengthStunIsNoStunTest::RunTest(const FString&)
{
	using namespace CataclysmStunTest;

	// An ability with no StunSeconds must not spend the target's immunity
	// window on a stun of no length. Every ability in the model except the
	// Stomp is in exactly that position.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Attacker(World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	FScopedFighter Defender(World, FVector(100.0f, 0.0f, 0.0f),
							ECataclysmTeam::Players, /*Health=*/1000.0f);

	TestFalse(TEXT("a stun of zero seconds is not applied"),
		UCataclysmSkillEffects::ApplyStun(
			Attacker.Actor, Defender.Actor, /*DurationSeconds=*/0.0f,
			/*DamageDealt=*/1000.0f, /*bStunIsDesigned=*/true));

	// AND IT DID NOT BURN THE WINDOW EITHER, which is the part that would
	// silently break the next real stun.
	TestTrue(TEXT("and a real stun straight afterwards still lands"),
		UCataclysmSkillEffects::ApplyStun(
			Attacker.Actor, Defender.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/1000.0f, /*bStunIsDesigned=*/true));

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmABossCannotBeStunnedTest,
	"Cataclysm.Stun.ABossCannotBeStunnedAtAllAndAHeraldStillCan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmABossCannotBeStunnedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStunTest;

	// RULE THREE OF THE ANTI-STUN-LOCK RULES, the last to arrive. Issue #395.
	// Boss-ness derives from the rarity step the spawner set: 4 (Boss) and up.
	//
	// THE BOUNDARY IS TESTED FROM BOTH SIDES, because the rule is a threshold
	// and a threshold checked from one side only cannot fail off by one.
	// Herald, at step 3, is the highest rarity the player may still stun --
	// the Abyssal Warden's reference rarity, and a mini-boss is not a boss.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Attacker(World, FVector::ZeroVector, ECataclysmTeam::Monsters);

	FScopedFighter Herald(World, FVector(100.0f, 0.0f, 0.0f),
						  ECataclysmTeam::Players, /*Health=*/1000.0f);
	Herald.Actor->SetRarityStep(
		ACataclysmEnemyCharacter::FirstBossRarityStep - 1);

	TestFalse(TEXT("a Herald is not a boss"), Herald.Actor->IsBoss());
	TestTrue(TEXT("and a designed stun still lands on it"),
		UCataclysmSkillEffects::ApplyStun(
			Attacker.Actor, Herald.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/0.0f, /*bStunIsDesigned=*/true));

	FScopedFighter Boss(World, FVector(200.0f, 0.0f, 0.0f),
						ECataclysmTeam::Players, /*Health=*/1000.0f);
	Boss.Actor->SetRarityStep(ACataclysmEnemyCharacter::FirstBossRarityStep);

	TestTrue(TEXT("step 4 is a boss"), Boss.Actor->IsBoss());

	// A DESIGNED STUN WITH FULL-HEALTH DAMAGE, the strongest stun the game can
	// express. bStunIsDesigned skips only the damage threshold; "a boss cannot
	// be stunned AT ALL" outranks it.
	TestFalse(TEXT("a boss refuses the strongest stun the game can express"),
		UCataclysmSkillEffects::ApplyStun(
			Attacker.Actor, Boss.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/1000.0f, /*bStunIsDesigned=*/true));
	TestFalse(TEXT("and it is not stunned afterwards"),
		UCataclysmSkillEffects::IsStunned(Boss.Actor));

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
