// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for a monster or a summoned imp deciding who to attack and going there.
 *
 * WHAT THESE GUARD. Issue #163: nothing in the project except the player had a
 * controller, so a summoned imp stood where it was put for its whole twenty
 * seconds, a monster stood where it was spawned, and the Madness debuff -- "the
 * enemy attacks anything nearby, friend or foe" -- granted a tag that nothing
 * read.
 *
 * WHAT THEY DELIBERATELY DO NOT COVER: that a pawn actually moves across the
 * ground. Walking is done by the character movement component following a path,
 * and a path needs a navigation mesh, which a world built by UWorld::CreateWorld
 * has no way to build. These check the decision -- who the target is, and
 * whether the controller chose to chase it or to hit it -- and the damage that
 * follows. Movement itself is checked in a Play-In-Editor session in
 * game/Content/Maps/L_Sandbox.umap, which does have navigation.
 */

namespace CataclysmBehaviourTest
{
	/**
	 * A world that has BEGUN PLAY.
	 *
	 * Needed for the same reason CataclysmSandboxTests needs it: a character
	 * registers its attribute sets in BeginPlay, so in a world that never begins
	 * play nothing has health to lose. Actors spawned after this point get their
	 * BeginPlay as they spawn, which is the order the real game uses.
	 */
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game,
										   /*bInformEngineOfWorld=*/false);
		if (World)
		{
			FURL URL;
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}
		return World;
	}

	/** Metres, so the tests read like the design document does. */
	constexpr float M = 100.0f;

	/** A character on the given side, with health and an attack. */
	struct FScopedFighter
	{
		FScopedFighter(UWorld* World, const FVector& Where, ECataclysmTeam Team,
					   float Health = 1000.0f, float AttackDamage = 50.0f)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(
				Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(Team));
			Actor->SetHealth(Health);
			Actor->SetAttackDamage(AttackDamage);
		}

		~FScopedFighter()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		float Health() const
		{
			const UAbilitySystemComponent* AbilitySystem =
				UCataclysmTargeting::AbilitySystemOf(Actor);
			return AbilitySystem
				? AbilitySystem->GetNumericAttribute(
					UCataclysmVitalAttributeSet::GetHealthAttribute())
				: 0.0f;
		}

		ACataclysmEnemyController* Brain() const
		{
			return Cast<ACataclysmEnemyController>(Actor->GetController());
		}

		ACataclysmEnemyCharacter* Actor = nullptr;
	};
}

// --------------------------------------------------------------------------
// A monster
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMonsterChasesAndAttacksTest,
	"Cataclysm.AI.AMonsterChasesWhatItSeesAndHitsWhatItReaches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMonsterChasesAndAttacksTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Monster(World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	FScopedFighter Player(World, FVector(10 * M, 0, 0), ECataclysmTeam::Players,
						  /*Health=*/1000.0f, /*AttackDamage=*/0.0f);

	// EVERY MONSTER NOW HAS A CONTROLLER, which is the whole of what was
	// missing. Before this the only pawn in the project with one was the player.
	ACataclysmEnemyController* Brain = Monster.Brain();
	if (!Brain)
	{
		AddError(TEXT("A spawned monster has no controller, so nothing drives it."));
		return false;
	}

	// Ten metres away: inside its notice radius of fifteen, outside its reach of
	// two, so it walks.
	TestEqual(TEXT("A monster ten metres from the player chases them"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Chasing));
	TestEqual(TEXT("And the player is what it is going after"),
		Brain->CurrentTarget.Get(), static_cast<AActor*>(Player.Actor));

	// One metre away: inside its reach, so it stops and hits.
	Player.Actor->SetActorLocation(FVector(1 * M, 0, 0));

	const float Before = Player.Health();
	TestEqual(TEXT("A monster within reach attacks instead of walking"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Attacking));
	TestTrue(FString::Printf(TEXT("And the player lost health (%.0f to %.0f)"),
		Before, Player.Health()), Player.Health() < Before);

	// Thirty metres away: outside its notice radius, so it stops and has no
	// target at all rather than walking after something it cannot see.
	Player.Actor->SetActorLocation(FVector(30 * M, 0, 0));

	TestEqual(TEXT("A monster with nothing in sight is idle"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Idle));
	TestNull(TEXT("And has no target"), Brain->CurrentTarget.Get());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMonstersLeaveEachOtherAloneTest,
	"Cataclysm.AI.AMonsterDoesNotAttackAnotherMonster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMonstersLeaveEachOtherAloneTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter First(World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	FScopedFighter Second(World, FVector(1 * M, 0, 0), ECataclysmTeam::Monsters);

	ACataclysmEnemyController* Brain = First.Brain();
	if (!Brain)
	{
		AddError(TEXT("A spawned monster has no controller."));
		return false;
	}

	// Standing a metre apart, well within reach of each other. Without sides
	// this is a fight; with them it is two monsters waiting for something to
	// happen.
	TestEqual(TEXT("Two monsters side by side do nothing to each other"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Idle));
	TestEqual(TEXT("And neither has lost health"), Second.Health(), 1000.0f);

	return true;
}

// --------------------------------------------------------------------------
// Madness
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMaddenedEnemyTurnsOnItsOwnTest,
	"Cataclysm.AI.AMaddenedEnemyAttacksAnythingNearbyFriendOrFoe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMaddenedEnemyTurnsOnItsOwnTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Maddened(World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	FScopedFighter Neighbour(World, FVector(1 * M, 0, 0), ECataclysmTeam::Monsters);

	ACataclysmEnemyController* Brain = Maddened.Brain();
	ACataclysmEnemyController* NeighboursBrain = Neighbour.Brain();
	if (!Brain || !NeighboursBrain)
	{
		AddError(TEXT("A spawned monster has no controller."));
		return false;
	}

	// Before: two monsters on the same side, doing nothing to each other.
	TestEqual(TEXT("Before Madness it has no target"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Idle));

	const FGameplayTag Madness = UCataclysmTeams::MadnessTag();
	if (!Madness.IsValid())
	{
		AddError(TEXT("Status.Madness is not a gameplay tag. It is generated into "
					  "game/Config/Tags/CataclysmTags.ini from the Debuffs sheet of "
					  "docs/All_Things_Cataclysm.xlsx by tools/generate_gameplay_tags.py."));
		return false;
	}

	// Subjugate applies this tag for three seconds. Applied directly here,
	// because what the tag makes happen is the thing under test, not how it is
	// granted -- Cataclysm.Skills covers Subjugate granting it.
	UCataclysmSkillEffects::ApplyTagForDuration(
		Maddened.Actor, Maddened.Actor, Madness, /*DurationSeconds=*/3.0f);

	if (!UCataclysmTeams::IsMaddened(Maddened.Actor))
	{
		AddError(TEXT("The Madness tag did not stick, so the rest of this test "
					  "would prove nothing."));
		return false;
	}

	// After: it attacks a monster of its own kind, which is exactly what the
	// design says Madness does.
	const float Before = Neighbour.Health();
	TestEqual(TEXT("A maddened monster attacks the monster beside it"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Attacking));
	TestEqual(TEXT("And its target is that monster"),
		Brain->CurrentTarget.Get(), static_cast<AActor*>(Neighbour.Actor));
	TestTrue(FString::Printf(TEXT("Which lost health (%.0f to %.0f)"),
		Before, Neighbour.Health()), Neighbour.Health() < Before);

	// READ IN BOTH DIRECTIONS, so the neighbour fights back rather than standing
	// still while it is hit by something it still regards as an ally.
	TestEqual(TEXT("And the neighbour fights back"),
		static_cast<int32>(NeighboursBrain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Attacking));
	TestEqual(TEXT("Against the maddened one"),
		NeighboursBrain->CurrentTarget.Get(), static_cast<AActor*>(Maddened.Actor));

	return true;
}

// --------------------------------------------------------------------------
// A summoned imp
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmImpChasesWhatItAttacksTest,
	"Cataclysm.AI.ASummonedImpGoesToTheFightRatherThanStandingStill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpChasesWhatItAttacksTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Summoner(World, FVector::ZeroVector, ECataclysmTeam::Players,
							/*Health=*/1000.0f, /*AttackDamage=*/100.0f);
	FScopedFighter Monster(World, FVector(10 * M, 0, 0), ECataclysmTeam::Monsters,
						   /*Health=*/1000.0f, /*AttackDamage=*/0.0f);

	const FVector Where(2 * M, 0, 0);
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Summoner.Actor, Where, /*Lifetime=*/20.0f, /*bBurns=*/false);
	if (!Imp)
	{
		AddError(TEXT("Could not summon an imp."));
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	// IT IS WHERE IT WAS PUT, which it was not before. As a bare AActor with no
	// scene component it had no root component, and an actor with no root
	// component reports the world origin however it was spawned -- so it
	// searched for targets around (0,0,0) rather than around itself.
	// Compared in the horizontal plane: a character is pushed upward by the
	// capsule's half-height where a bare actor was not, so its Z is not the Z it
	// was spawned at and never was the interesting part.
	const FVector Offset = Imp->GetActorLocation() - Where;
	TestTrue(FString::Printf(
		TEXT("An imp is at the place it was summoned (%.0f cm away in the plane)"),
		FVector2D(Offset.X, Offset.Y).Size()),
		FVector2D(Offset.X, Offset.Y).Size() < 1.0f);

	// AND THE WORLD CAN FIND IT, which it also could not before: it had no
	// collision of any kind, so no sphere overlap ever returned it and nothing
	// could damage or kill it.
	const TArray<AActor*> Found = UCataclysmTargeting::FindEnemiesInSphere(
		World, Monster.Actor, Where, /*RadiusCm=*/200.0f);
	TestTrue(TEXT("An imp is something a monster's targeting can find"),
		Found.Contains(Imp));

	ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(Imp->GetController());
	if (!Brain)
	{
		AddError(TEXT("A summoned imp has no controller, so nothing drives it."));
		return false;
	}

	// Eight metres from the monster: inside its notice radius, outside its
	// reach. This is the case issue #163 reported -- "an imp summoned across the
	// room from a fight does nothing for its whole life".
	TestEqual(TEXT("An imp walks to a monster it can see but not reach"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Chasing));
	TestEqual(TEXT("And the monster is what it is going after"),
		Brain->CurrentTarget.Get(), static_cast<AActor*>(Monster.Actor));

	// Within its three metre reach: it stops and hits.
	Monster.Actor->SetActorLocation(Where + FVector(1 * M, 0, 0));

	const float Before = Monster.Health();
	TestEqual(TEXT("An imp within reach attacks"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Attacking));
	TestEqual(TEXT("It counted the attack"), Imp->AttacksMade, 1);
	TestTrue(FString::Printf(TEXT("And the monster lost health (%.0f to %.0f)"),
		Before, Monster.Health()), Monster.Health() < Before);

	// A SHARE OF THE SUMMONER'S WEAPON DAMAGE, not of its own, which it has
	// none of. The summoner's attack damage is 100 and an imp deals 25% of it.
	const float Expected =
		100.0f * ACataclysmMinion::DamagePercentOfSummoner / 100.0f;
	TestEqual(TEXT("For a share of its summoner's weapon damage"),
		Before - Monster.Health(), Expected);

	// It never turns on the character that made it, whatever else is nearby.
	Monster.Actor->SetActorLocation(FVector(50 * M, 0, 0));
	Summoner.Actor->SetActorLocation(Where + FVector(1 * M, 0, 0));

	TestEqual(TEXT("An imp standing next to its summoner is idle"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Idle));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
