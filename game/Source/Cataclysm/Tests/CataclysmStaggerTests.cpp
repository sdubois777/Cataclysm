// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the Staggered state. Issue #45.
 *
 * WHAT A STAGGER IS HERE, answered by the project owner on 2026-09-11: "A
 * knockback, pull or knockdown also leaves the target Staggered for 1 second."
 * Asked whether a staggered target can still act, the owner answered yes. So it
 * is a state other effects read, not a hold: it takes none of the
 * anti-stun-lock rules and needs none of its own.
 *
 * WHAT THESE GUARD. That the three verbs the owner named leave the state and
 * that a fourth displacement does not; that a shove which never landed leaves
 * nothing; and that the state is not one of the holds.
 *
 * WHAT THEY DELIBERATELY DO NOT COVER: that the tag falls off when its second
 * is up. Expiry belongs to the Gameplay Ability System's duration effects and
 * runs on the world's timer manager, which a world built by `UWorld::CreateWorld`
 * never ticks. `CataclysmForcedMovementTests.cpp` and `CataclysmStunTests.cpp`
 * say the same, for the same reason.
 *
 * NOTHING HERE READS AN ENCHANTMENT. Eleven enchantment rows will apply this
 * state, lengthen it, check for it or restrict it; none of them is written yet.
 */
namespace CataclysmStaggerTest
{
	/** A creature with health, on a side, able to shove or to be shoved. */
	struct FScopedFighter
	{
		FScopedFighter(UWorld* World, const FVector& Where, ECataclysmTeam Team,
					   float CrowdControlResistance = 0.0f)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(
				Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(Team));

			// SetHealth sets the MAXIMUM, which the knockdown's damage threshold
			// is a percentage of. A thousand makes that threshold a round
			// hundred, and every knockdown below is designed and skips it anyway.
			Actor->SetHealth(1000.0f);

			if (CrowdControlResistance != 0.0f)
			{
				if (UAbilitySystemComponent* AbilitySystem =
						Actor->GetAbilitySystemComponent())
				{
					AbilitySystem->SetNumericAttributeBase(
						UCataclysmCombatAttributeSet::
							GetCrowdControlResistanceAttribute(),
						CrowdControlResistance);
				}
			}
		}

		~FScopedFighter()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		FVector Where() const { return Actor->GetActorLocation(); }

		ACataclysmEnemyCharacter* Actor = nullptr;
	};
}

// EVERY TEST BELOW OPENS THIS NAMESPACE INSIDE ITS OWN BODY RATHER THAN AT FILE
// SCOPE, for the reason `CataclysmForcedMovementTests.cpp` states at length:
// this module is built as a unity blob, so a `using namespace` written at file
// scope reaches every other file concatenated with it.

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_TEST(FCataclysmStaggerTagExistsTest,
	"Cataclysm.Stagger.TheStateExistsInTheVocabulary")
{
	// WITHOUT THIS THE WHOLE STATE FAILS SILENTLY. The tag is requested by name
	// with ErrorIfNotFound false, so a vocabulary that lost it would answer an
	// invalid tag and every application below would quietly do nothing.
	TestTrue(TEXT("State.Staggered is a tag this build knows"),
			 UCataclysmSkillEffects::StaggeredTag().IsValid());
	return true;
}

CATACLYSM_TEST(FCataclysmStaggerKnockbackTest,
	"Cataclysm.Stagger.AKnockbackThatLandsLeavesTheTargetStaggered")
{
	using namespace CataclysmStaggerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Attacker(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter Target(World, FVector(400.0f, 0.0f, 0.0f),
						  ECataclysmTeam::Monsters);
	const FVector Before = Target.Where();

	TestTrue(TEXT("the shove lands"),
			 UCataclysmSkillEffects::ApplyKnockback(Attacker.Actor, Target.Actor,
													300.0f));
	TestTrue(TEXT("and the target actually moved"),
			 !Target.Where().Equals(Before, 1.0f));
	TestTrue(TEXT("so it is staggered"),
			 UCataclysmSkillEffects::IsStaggered(Target.Actor));

	return true;
}

CATACLYSM_TEST(FCataclysmStaggerResistedShoveTest,
	"Cataclysm.Stagger.AShoveTheTargetFullyResistsLeavesNoStagger")
{
	using namespace CataclysmStaggerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// 100 CROWD CONTROL RESISTANCE STOPS A SHOVE ENTIRELY, which is the one
	// place this game lets a stat reach immunity. A displacement that never
	// happened must leave no stagger: the state says a shove landed.
	FScopedFighter Attacker(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter Target(World, FVector(400.0f, 0.0f, 0.0f),
						  ECataclysmTeam::Monsters, /*CrowdControlResistance=*/100.0f);
	const FVector Before = Target.Where();

	TestFalse(TEXT("the shove does not land"),
			  UCataclysmSkillEffects::ApplyKnockback(Attacker.Actor,
													 Target.Actor, 300.0f));
	TestTrue(TEXT("the target stayed where it was"),
			 Target.Where().Equals(Before, 1.0f));
	TestFalse(TEXT("so it is not staggered"),
			  UCataclysmSkillEffects::IsStaggered(Target.Actor));

	return true;
}

CATACLYSM_TEST(FCataclysmStaggerPullTest,
	"Cataclysm.Stagger.APullThatLandsLeavesTheTargetStaggered")
{
	using namespace CataclysmStaggerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// A DRAG IS THIS SAME FUNCTION, applied after the caster has moved:
	// `UCataclysmSkillTemplate::ApplyForcedMovementTo` sends both verbs here. So
	// a drag staggers too, and nothing separate has to.
	FScopedFighter Caster(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter Target(World, FVector(500.0f, 0.0f, 0.0f),
						  ECataclysmTeam::Monsters);
	const FVector Before = Target.Where();

	TestTrue(TEXT("the pull lands"),
			 UCataclysmSkillEffects::ApplyPull(Caster.Actor, Target.Actor,
											   200.0f));
	TestTrue(TEXT("and the target actually moved"),
			 !Target.Where().Equals(Before, 1.0f));
	TestTrue(TEXT("so it is staggered"),
			 UCataclysmSkillEffects::IsStaggered(Target.Actor));

	return true;
}

CATACLYSM_TEST(FCataclysmStaggerKnockdownTest,
	"Cataclysm.Stagger.AKnockdownLeavesTheTargetStaggered")
{
	using namespace CataclysmStaggerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Attacker(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter Target(World, FVector(400.0f, 0.0f, 0.0f),
						  ECataclysmTeam::Monsters);

	// A DESIGNED KNOCKDOWN, which is what all three rows that state one are. It
	// skips the damage threshold and nothing else, so the zero damage here is
	// not what decides the outcome.
	TestTrue(TEXT("the knockdown lands"),
			 UCataclysmSkillEffects::ApplyKnockdown(
				 Attacker.Actor, Target.Actor, /*DurationSeconds=*/3.0f,
				 /*DamageDealt=*/0.0f, /*bKnockdownIsDesigned=*/true));
	TestTrue(TEXT("the target is on the floor"),
			 UCataclysmSkillEffects::IsKnockedDown(Target.Actor));
	TestTrue(TEXT("and staggered as well"),
			 UCataclysmSkillEffects::IsStaggered(Target.Actor));

	return true;
}

CATACLYSM_TEST(FCataclysmStaggerLaunchTest,
	"Cataclysm.Stagger.ALaunchDoesNotStagger")
{
	using namespace CataclysmStaggerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE OWNER NAMED THREE VERBS: a knockback, a pull and a knockdown. A launch
	// is the fourth displacement in the game and is not one of them, so it is
	// left out rather than read in. `docs/DECISIONS.md` records that judgement
	// and what it would take to change it.
	FScopedFighter Caster(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter Target(World, FVector(400.0f, 0.0f, 0.0f),
						  ECataclysmTeam::Monsters);
	const FVector Before = Target.Where();

	TestTrue(TEXT("the launch lands"),
			 UCataclysmSkillEffects::ApplyLaunch(Caster.Actor, Target.Actor,
												 200.0f));
	TestTrue(TEXT("and the target actually moved"),
			 !Target.Where().Equals(Before, 1.0f));
	TestFalse(TEXT("but a launch leaves no stagger"),
			  UCataclysmSkillEffects::IsStaggered(Target.Actor));

	return true;
}

CATACLYSM_TEST(FCataclysmStaggerIsNotAHoldTest,
	"Cataclysm.Stagger.AStaggeredTargetIsNotStunnedKnockedDownOrPinned")
{
	using namespace CataclysmStaggerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// A STAGGERED TARGET CAN STILL ACT, which is the owner's answer of
	// 2026-09-11 and the whole difference between this state and a hold. What
	// stops a character acting is one of the three states below: the
	// controllers read those and nothing reads this one.
	FScopedFighter Attacker(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter Target(World, FVector(400.0f, 0.0f, 0.0f),
						  ECataclysmTeam::Monsters);

	UCataclysmSkillEffects::ApplyKnockback(Attacker.Actor, Target.Actor, 300.0f);

	TestTrue(TEXT("the target is staggered"),
			 UCataclysmSkillEffects::IsStaggered(Target.Actor));
	TestFalse(TEXT("it is not stunned"),
			  UCataclysmSkillEffects::IsStunned(Target.Actor));
	TestFalse(TEXT("it is not knocked down"),
			  UCataclysmSkillEffects::IsKnockedDown(Target.Actor));
	TestFalse(TEXT("and it is not pinned"),
			  UCataclysmSkillEffects::IsPinned(Target.Actor));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
