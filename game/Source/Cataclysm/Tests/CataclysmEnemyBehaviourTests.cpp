// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
// UAnimSequence is only forward declared on the Brute, and the gait test builds
// throwaway ones with NewObject, which needs the whole type.
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
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

	/** The player capsule radius in CataclysmPlayerCharacter.cpp. */
	constexpr float PlayerCapsuleRadiusCm = 42.0f;

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

	/**
	 * A Brute, which is the only character in the project that roams.
	 *
	 * SEPARATE FROM FScopedFighter RATHER THAN TEMPLATED OVER IT, because a
	 * Brute needs no health or attack damage set for any roaming test: roaming
	 * happens when there is nothing to fight, so the fighting half is dead
	 * weight here.
	 */
	struct FScopedBrute
	{
		FScopedBrute(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<ACataclysmBruteCharacter>(
				Where, FRotator::ZeroRotator);
			check(Actor);
		}

		~FScopedBrute()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		ACataclysmEnemyController* Brain() const
		{
			return Cast<ACataclysmEnemyController>(Actor->GetController());
		}

		ACataclysmBruteCharacter* Actor = nullptr;
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

// --------------------------------------------------------------------------
// Roaming
//
// WHY THESE CAN BE CHECKED HEADLESS WHEN MOVEMENT CANNOT. Walking needs a
// navigation mesh and a world built by UWorld::CreateWorld has none, which is
// why the file header says movement itself is checked in a Play-In-Editor
// session. Roaming is a decision before it is a walk: which point, chosen from
// where, and what state the brain reports. All of that is assertable here.
// ACataclysmEnemyController::ChooseRoamTarget exists as a separate function
// precisely so it can be called in a world with no navigation system.
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRoamingIsOptInTest,
	"Cataclysm.AI.OnlyACharacterThatAsksToRoamRoams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRoamingIsOptInTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THE REGRESSION THIS EXISTS FOR. Adding a Roaming state could easily have
	// made every enemy in the project wander off, and the four tests above that
	// assert Idle would have been "fixed" by editing them. They were not
	// edited, and this says why: an ordinary enemy does not roam because it
	// never asked to.
	FScopedFighter Ordinary(World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	TestEqual(TEXT("an ordinary enemy does not ask to roam"),
		Ordinary.Actor->RoamRadiusCm(), 0.0f);

	ACataclysmEnemyController* OrdinaryBrain = Ordinary.Brain();
	if (!OrdinaryBrain)
	{
		AddError(TEXT("A spawned monster has no controller."));
		return false;
	}
	TestEqual(TEXT("so with nothing in sight it is Idle, exactly as before"),
		static_cast<int32>(OrdinaryBrain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Idle));

	FVector Unused = FVector::ZeroVector;
	TestFalse(TEXT("and it cannot pick a roam target at all"),
		OrdinaryBrain->ChooseRoamTarget(Unused));

	// A summoned imp must not wander away from the fight it was made for.
	FScopedFighter Summoner(World, FVector(50 * M, 0, 0), ECataclysmTeam::Players);
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Summoner.Actor, FVector(50 * M, 0, 0), /*Lifetime=*/20.0f, /*bBurns=*/false);
	if (!Imp)
	{
		AddError(TEXT("Could not summon an imp."));
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	TestEqual(TEXT("a summoned imp does not ask to roam either"),
		Imp->RoamRadiusCm(), 0.0f);

	// And the Brute, which is the one that does.
	FScopedBrute Brute(World, FVector(20 * M, 0, 0));
	TestEqual(TEXT("a Brute asks to roam, within its designed radius"),
		Brute.Actor->RoamRadiusCm(),
		ACataclysmBruteCharacter::BruteRoamRadiusCm);
	TestTrue(TEXT("which is greater than zero, or nothing would happen"),
		Brute.Actor->RoamRadiusCm() > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteRoamsWithNothingInSightTest,
	"Cataclysm.AI.ABruteWithNothingInSightWalksSomewhereAndStops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteRoamsWithNothingInSightTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FVector Spawn(5 * M, 3 * M, 0);
	FScopedBrute Brute(World, Spawn);

	ACataclysmEnemyController* Brain = Brute.Brain();
	if (!Brain)
	{
		AddError(TEXT("A spawned Brute has no controller."));
		return false;
	}

	// THE ANCHOR IS WHERE IT WAS PUT. Compared in the horizontal plane only: a
	// character is pushed up by its capsule half-height as it spawns, so its Z
	// is not the Z it was asked for and never was the interesting part.
	const FVector AnchorOffset = Brain->RoamAnchor - Spawn;
	TestTrue(FString::Printf(
		TEXT("the roam anchor is where it spawned (%.0f cm away in the plane)"),
		FVector2D(AnchorOffset.X, AnchorOffset.Y).Size()),
		FVector2D(AnchorOffset.X, AnchorOffset.Y).Size() < 1.0f);

	// Nothing hostile anywhere in the world, so it roams rather than standing.
	TestEqual(TEXT("a Brute alone in the world roams"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Roaming));
	TestNull(TEXT("and has no target, because there is nothing to have"),
		Brain->CurrentTarget.Get());
	TestTrue(TEXT("and it has chosen somewhere to go"), Brain->bHasRoamTarget);

	const float ChosenDistance = FVector::Dist2D(Brain->RoamTarget, Brain->RoamAnchor);
	TestTrue(FString::Printf(
		TEXT("which is inside its roam radius (%.0f cm of %.0f)"),
		ChosenDistance, ACataclysmBruteCharacter::BruteRoamRadiusCm),
		ChosenDistance <= ACataclysmBruteCharacter::BruteRoamRadiusCm);

	// IT DOES NOT RE-PICK EVERY PASS. Think runs four times a second, and a
	// character that chose a new destination each time would vibrate on the
	// spot instead of walking anywhere. This is the assertion that catches it.
	const FVector FirstChoice = Brain->RoamTarget;
	TestEqual(TEXT("a second pass is still roaming"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Roaming));
	TestEqual(TEXT("and is still walking to the same place"),
		Brain->RoamTarget, FirstChoice);

	// STILL HOLDING IT, not merely still remembering it. RoamTarget keeps its
	// value after arrival too, so the line above passes whether the character is
	// walking or has given up; this is the one that tells them apart.
	//
	// It is also what proves the move request was accepted. Roam treats a move
	// status of Idle as the walk having ended, so if the request had failed
	// outright -- which is a live possibility in a world with no navigation mesh
	// -- this pass would have cleared the flag and this assertion would fail.
	TestTrue(TEXT("and is still actually going there rather than having given up"),
		Brain->bHasRoamTarget);

	// THE SAFETY NET HAS A DEADLINE AND IT IS IN THE FUTURE. A roam move is
	// ordered once and never re-issued, so a walk that ends short of the target
	// would leave the character standing still for good. The deadline is what
	// notices. It must be at least the floor, and it must be ahead of now, or it
	// would fire on the pass after setting off.
	TestTrue(FString::Printf(
		TEXT("the roam leg has a deadline ahead of now (%.2f, now %.2f)"),
		Brain->RoamLegDeadline, World->GetTimeSeconds()),
		Brain->RoamLegDeadline > World->GetTimeSeconds());
	TestTrue(FString::Printf(
		TEXT("and it is at least the minimum a short leg gets (%.2f of %.2f)"),
		Brain->RoamLegDeadline - static_cast<float>(World->GetTimeSeconds()),
		ACataclysmEnemyController::RoamLegMinimumSeconds),
		Brain->RoamLegDeadline - static_cast<float>(World->GetTimeSeconds())
			>= ACataclysmEnemyController::RoamLegMinimumSeconds);

	// ARRIVING. Nothing moves the character in a world with no navigation, so
	// the arrival is staged by putting it on its own target.
	Brute.Actor->SetActorLocation(
		FVector(FirstChoice.X, FirstChoice.Y, Brute.Actor->GetActorLocation().Z));

	TestEqual(TEXT("standing on its target it is still roaming"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Roaming));
	TestFalse(TEXT("but it has let go of the target it reached"),
		Brain->bHasRoamTarget);

	// THE PAUSE, CHECKED BY ITS DEADLINE RATHER THAN BY WAITING. This world is
	// never ticked so GetTimeSeconds does not advance; see the property comment
	// on RoamPauseUntil.
	//
	// CAST, for the reason CataclysmBruteTests already records about FRotator:
	// UWorld::GetTimeSeconds returns a double and RoamPauseSeconds is a float,
	// and TestEqual has an overload for each, so the mixed pair is ambiguous
	// and does not compile.
	const float ExpectedPauseEnd = static_cast<float>(World->GetTimeSeconds())
		+ ACataclysmEnemyController::RoamPauseSeconds;
	TestEqual(TEXT("and it is standing there for the pause"),
		Brain->RoamPauseUntil, ExpectedPauseEnd);

	TestEqual(TEXT("a pass during the pause does not set off again"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Roaming));
	TestFalse(TEXT("and still has not chosen anywhere new"), Brain->bHasRoamTarget);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRoamTargetsAreSpreadTest,
	"Cataclysm.AI.RoamTargetsStayInRangeAndAreNotAllTheSamePlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRoamTargetsAreSpreadTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedBrute Brute(World, FVector::ZeroVector);
	ACataclysmEnemyController* Brain = Brute.Brain();
	if (!Brain)
	{
		AddError(TEXT("A spawned Brute has no controller."));
		return false;
	}

	const float Radius = ACataclysmBruteCharacter::BruteRoamRadiusCm;

	// EVERY POINT, NOT A SAMPLE OF ONE. The choice is random, so a single call
	// proves almost nothing: a bug that put one point in a hundred outside the
	// radius would pass a single-call test ninety-nine times out of a hundred.
	constexpr int32 Draws = 200;
	int32 OutsideRadius = 0;
	int32 Failures = 0;
	float Furthest = 0.0f;
	TSet<FString> Distinct;

	for (int32 Draw = 0; Draw < Draws; ++Draw)
	{
		FVector Chosen = FVector::ZeroVector;
		if (!Brain->ChooseRoamTarget(Chosen))
		{
			++Failures;
			continue;
		}

		const float Distance = FVector::Dist2D(Chosen, Brain->RoamAnchor);
		Furthest = FMath::Max(Furthest, Distance);
		if (Distance > Radius)
		{
			++OutsideRadius;
		}

		Distinct.Add(FString::Printf(TEXT("%.0f,%.0f"), Chosen.X, Chosen.Y));
	}

	TestEqual(TEXT("a Brute can always choose somewhere to roam"), Failures, 0);
	TestEqual(FString::Printf(
		TEXT("every one of %d roam targets is inside the roam radius "
			 "(furthest was %.0f cm of %.0f)"), Draws, Furthest, Radius),
		OutsideRadius, 0);

	// NOT ALL THE SAME PLACE. A ChooseRoamTarget that returned the anchor every
	// time would satisfy every assertion above and produce a Brute that never
	// moves. Two hundred draws from a continuous distribution collapsing to
	// fewer than fifty distinct points is not something chance does.
	TestTrue(FString::Printf(
		TEXT("the targets are spread rather than one point (%d distinct of %d)"),
		Distinct.Num(), Draws),
		Distinct.Num() > 50);

	// AND THEY REACH THE OUTER PART OF THE CIRCLE. Drawing the distance without
	// the square root would cluster them near the anchor; this catches that.
	TestTrue(FString::Printf(
		TEXT("at least one target is in the outer half of the circle "
			 "(furthest %.0f cm, half-radius %.0f)"), Furthest, Radius * 0.5f),
		Furthest > Radius * 0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteActuallyHitsWhatItReachesTest,
	"Cataclysm.AI.ABruteInContactReachStopsRoamingAndLandsAHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteActuallyHitsWhatItReachesTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	// WHY A BRUTE RATHER THAN AN ORDINARY ENEMY, WHICH IS ALREADY COVERED. The
	// Brute's reach is 90 cm, which is exactly the two capsule radii touching,
	// with no margin at all. Every other enemy reaches 200 cm and has over a
	// metre to spare. So the Brute is the only character where the distance
	// comparison's own arithmetic decides whether it ever lands a hit, and
	// issue #373 records it failing to for exactly that reason.
	//
	// Cataclysm.Brute.CanActuallyReachThePlayer checks the arithmetic and builds
	// no world. This checks the consequence: a Brute put in contact with a
	// player takes health off them.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedBrute Brute(World, FVector::ZeroVector);
	Brute.Actor->SetAttackDamage(35.0f);

	ACataclysmEnemyController* Brain = Brute.Brain();
	if (!Brain)
	{
		AddError(TEXT("A spawned Brute has no controller."));
		return false;
	}

	// SPAWNED CLEAR AND THEN MOVED IN, which is the idiom the monster test above
	// uses and it is not stylistic. Two capsules of radius 48 cm cannot both be
	// spawned 80 cm apart without overlapping, and the spawn displaces one of
	// them to make room -- so a Brute and a player spawned in contact end up
	// further apart than either was asked for. Written the direct way first,
	// this test failed with the Brute reporting Chasing at a distance that
	// should have been inside its reach.
	FScopedFighter Player(World, FVector(20 * M, 0, 0), ECataclysmTeam::Players,
						  /*Health=*/1000.0f, /*AttackDamage=*/0.0f);

	// Just inside the 90 cm contact reach, measured in the floor plane.
	Player.Actor->SetActorLocation(FVector(80.0f, 0.0f,
		Player.Actor->GetActorLocation().Z));

	// STATED RATHER THAN ASSUMED, so that a future failure says whether the
	// characters were where the test meant them to be.
	const float Apart = FVector::Dist2D(
		Brute.Actor->GetActorLocation(), Player.Actor->GetActorLocation());
	TestTrue(FString::Printf(
		TEXT("the two are inside the Brute's %.0f cm reach (%.1f cm apart)"),
		ACataclysmBruteCharacter::DesignedMeleeReachCm, Apart),
		Apart <= ACataclysmBruteCharacter::DesignedMeleeReachCm);

	const float Before = Player.Health();

	TestEqual(TEXT("a Brute in contact reach attacks rather than roaming"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Attacking));
	TestEqual(TEXT("and the player is what it is hitting"),
		Brain->CurrentTarget.Get(), static_cast<AActor*>(Player.Actor));
	TestEqual(TEXT("and it counted the attack"), Brain->AttacksOrdered, 1);
	TestTrue(FString::Printf(TEXT("and the player lost health (%.0f to %.0f)"),
		Before, Player.Health()), Player.Health() < Before);

	// ATTACKING WINS OVER ROAMING, so a Brute cannot wander off mid-fight.
	TestFalse(TEXT("and it is not holding a place to wander to"),
		Brain->bHasRoamTarget);

	// THE ATTACK INTERVAL IS A RATE LIMIT AND IT HOLDS. The world is never
	// ticked so no time passes, and the Brute's interval is 2.8 seconds, so a
	// second pass must not produce a second hit.
	const float AfterFirst = Player.Health();
	TestEqual(TEXT("a second pass with no time elapsed still reports attacking"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Attacking));
	TestEqual(TEXT("but does not hit again, because 2.8 seconds have not passed"),
		Brain->AttacksOrdered, 1);
	TestEqual(TEXT("so the player lost no more health"),
		Player.Health(), AfterFirst);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChaseStopsCloseEnoughToHitTest,
	"Cataclysm.AI.AChaseStopsCloseEnoughToActuallyHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmChaseStopsCloseEnoughToHitTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	// WHAT THIS GUARDS, and it is the fault behind "he doesn't attack when he
	// reaches me" reported on 2026-08-07.
	//
	// FAIMoveRequest is constructed with bReachTestIncludesAgentRadius and
	// bReachTestIncludesGoalRadius both true. That silently adds BOTH capsule
	// radii to the acceptance radius. For the Brute, whose acceptance radius is
	// 0.8 of a 90 cm reach, the request that reads as "stop within 72 cm" means
	// "stop within 72 + 48 + 42 = 162 cm" -- so the chase ends nearly a metre
	// outside the distance at which it could swing, and it stands there.
	//
	// The flags are the only observable part. Where the character actually
	// stops needs a navigation mesh, which no automation test world has.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedBrute Brute(World, FVector::ZeroVector);
	FScopedFighter Player(World, FVector(10 * M, 0, 0), ECataclysmTeam::Players);

	ACataclysmEnemyController* Brain = Brute.Brain();
	if (!Brain)
	{
		AddError(TEXT("A spawned Brute has no controller."));
		return false;
	}

	const float Reach = ACataclysmBruteCharacter::DesignedMeleeReachCm;
	const FAIMoveRequest Request =
		Brain->MakeChaseMoveRequest(Player.Actor, Reach);

	TestFalse(TEXT("the reach test does not add the chaser's own capsule radius"),
		Request.IsReachTestIncludingAgentRadius());
	TestFalse(TEXT("nor the target's"),
		Request.IsReachTestIncludingGoalRadius());

	// AND THE NUMBER IT STOPS AT IS INSIDE WHAT IT CAN HIT. Stated as the
	// consequence rather than as the flags, so the test says why it cares.
	const float StoppingDistance = Request.GetAcceptanceRadius()
		+ (Request.IsReachTestIncludingAgentRadius()
			? ACataclysmBruteCharacter::BruteCapsuleRadius : 0.0f)
		+ (Request.IsReachTestIncludingGoalRadius()
			? PlayerCapsuleRadiusCm : 0.0f);

	TestTrue(FString::Printf(
		TEXT("so the chase aims to stop at %.0f cm, inside the %.0f cm it can "
			 "hit from"), StoppingDistance, Reach),
		StoppingDistance <= Reach);

	// WITH THE DEFAULTS IT WOULD NOT BE, which is what makes the assertion
	// above worth making. Stated so the number is on the record.
	const float WithEngineDefaults = Request.GetAcceptanceRadius()
		+ ACataclysmBruteCharacter::BruteCapsuleRadius + PlayerCapsuleRadiusCm;
	TestTrue(FString::Printf(
		TEXT("whereas the engine defaults would stop it at %.0f cm, well "
			 "outside %.0f"), WithEngineDefaults, Reach),
		WithEngineDefaults > Reach);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteSwingIsVisibleTest,
	"Cataclysm.AI.ABruteHoldsItsSwingAnimationInsteadOfCuttingItOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteSwingIsVisibleTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedBrute Brute(World, FVector::ZeroVector);

	// CALLED RATHER THAN WAITED FOR, which the Brute's own header asks for:
	// whether BeginPlay runs at all depends on how the world was made, so a
	// test that spawned and then checked could not tell "the art is missing"
	// apart from "BeginPlay did not fire". Measured here: it does not fire, and
	// without this the art half of this test silently never ran.
	Brute.Actor->ResolveBody(/*bIncludeAnimation=*/true);

	TestFalse(TEXT("a Brute that has not swung is not swinging"),
		Brute.Actor->IsSwinging());

	// WITHOUT THE ART, PLAYING A SWING DOES NOTHING AND SAYS SO. The Paragon
	// packs are gitignored, so this is the state in continuous integration and
	// on a fresh clone, and it must not pretend to swing.
	if (Brute.Actor->AttackAnimation == nullptr)
	{
		Brute.Actor->PlayAttackAnimation();
		TestFalse(TEXT("with no attack animation loaded it never claims to swing"),
			Brute.Actor->IsSwinging());
		AddInfo(TEXT("The Paragon art is absent, so only the no-art half of this "
					 "test ran. Install the pack to exercise the rest."));
		return true;
	}

	// THROUGH AttackTarget, NOT PlayAttackAnimation. Calling the animation
	// directly would pass even if nothing ever triggered it, which is exactly
	// the mistake the first version of this test made: three deliberate breaks
	// went unnoticed. The swing has to be a consequence of hitting something.
	FScopedFighter Player(World, FVector(80.0f, 0.0f, 0.0f), ECataclysmTeam::Players,
						  /*Health=*/1000.0f, /*AttackDamage=*/0.0f);
	Brute.Actor->SetAttackDamage(35.0f);

	const float Now = static_cast<float>(World->GetTimeSeconds());
	Brute.Actor->AttackTarget(Player.Actor);

	TestTrue(TEXT("landing a hit starts the swing"), Brute.Actor->IsSwinging());

	// THE SWING LASTS THE ANIMATION, NOT THE ATTACK INTERVAL, and this reads
	// the deadline the code stored rather than the animation asset. Reading the
	// asset proved nothing: it is the same number whatever the code does.
	const float Held = Brute.Actor->SwingUntilSeconds - Now;
	TestEqual(TEXT("and holds the mesh for exactly the animation's own length"),
		Held, Brute.Actor->AttackAnimation->GetPlayLength());
	TestTrue(FString::Printf(
		TEXT("which is shorter than the %.1f s between attacks, so it is not "
			 "frozen in its finishing pose (%.2f s)"),
		ACataclysmBruteCharacter::DesignedAttackIntervalSeconds, Held),
		Held < ACataclysmBruteCharacter::DesignedAttackIntervalSeconds);

	// AND LOCOMOTION LEAVES IT ALONE. The Brute has stopped moving to attack,
	// so the next frame's choice would be the standing animation and the swing
	// would be cut off after one frame -- which looks exactly like not
	// attacking. DriveLocomotion is called here to prove it does not.
	const USkeletalMeshComponent* MeshComponent = Brute.Actor->GetMesh();
	const UAnimSingleNodeInstance* Single =
		MeshComponent ? MeshComponent->GetSingleNodeInstance() : nullptr;
	if (!Single)
	{
		AddError(TEXT("The Brute has no single node animation instance, so the "
					  "swing cannot be checked."));
		return false;
	}

	TestEqual(TEXT("the swing animation is what is on the mesh"),
		Single->GetAnimationAsset(),
		static_cast<UAnimationAsset*>(Brute.Actor->AttackAnimation));

	Brute.Actor->DriveLocomotion();

	TestEqual(TEXT("and a frame of locomotion does not replace it"),
		Single->GetAnimationAsset(),
		static_cast<UAnimationAsset*>(Brute.Actor->AttackAnimation));
	TestTrue(TEXT("and it is still swinging afterwards"),
		Brute.Actor->IsSwinging());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteRunsWhileChasingTest,
	"Cataclysm.AI.ABruteUsesADifferentGaitWhileChasingThanWhileWandering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteRunsWhileChasingTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	// THE DECISION, NOT THE PLAYBACK, for the reason the Brute's own animation
	// tests give: applying an animation needs a component that has run InitAnim
	// and a synthetic world does not reliably give one. AnimationForGroundSpeed
	// is a pure function and can be asked anything.
	//
	// AND WITHOUT THE ART, WHICH IS THE POINT OF THE PLACEHOLDERS BELOW. The
	// Paragon packs are gitignored, so on a fresh clone and in this worktree no
	// animation loads at all and a test that compared real assets would silently
	// check nothing. Two distinct throwaway UAnimSequence objects stand in.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedBrute Brute(World, FVector::ZeroVector);

	UAnimSequence* Standing = NewObject<UAnimSequence>();
	UAnimSequence* Walking = NewObject<UAnimSequence>();
	UAnimSequence* Running = NewObject<UAnimSequence>();
	Brute.Actor->IdleAnimation = Standing;
	Brute.Actor->WalkAnimation = Walking;
	Brute.Actor->ChaseAnimation = Running;

	const float Moving = ACataclysmBruteCharacter::DesignedWalkSpeedCmPerSecond;
	float Rate = 0.0f;

	TestEqual(TEXT("standing still it stands, chasing or not"),
		Brute.Actor->AnimationForGroundSpeed(0.0f, Rate, /*bChasing=*/true),
		Standing);

	TestEqual(TEXT("moving with nothing to chase it walks"),
		Brute.Actor->AnimationForGroundSpeed(Moving, Rate, /*bChasing=*/false),
		Walking);

	TestEqual(TEXT("moving toward something it has noticed it runs"),
		Brute.Actor->AnimationForGroundSpeed(Moving, Rate, /*bChasing=*/true),
		Running);

	// THE SAME SPEED PRODUCES DIFFERENT ANIMATIONS, which is the whole point.
	// The Brute moves at 250 cm/s either way, because its movement speed is a
	// designed number, so ground speed cannot distinguish the two states and
	// the brain has to.
	TestNotEqual(TEXT("so the same ground speed gives two different gaits"),
		Brute.Actor->AnimationForGroundSpeed(Moving, Rate, /*bChasing=*/false),
		Brute.Actor->AnimationForGroundSpeed(Moving, Rate, /*bChasing=*/true));

	// FALLS BACK TO THE WALK WITH NO CHASE CLIP, which is the state of every
	// fresh clone, rather than falling back to nothing and freezing the pose.
	Brute.Actor->ChaseAnimation = nullptr;
	TestEqual(TEXT("with no chase animation loaded it walks while chasing"),
		Brute.Actor->AnimationForGroundSpeed(Moving, Rate, /*bChasing=*/true),
		Walking);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteChaseSpeedTakesEffectTest,
	"Cataclysm.AI.ABruteActuallyMovesFasterWhenAChaseSpeedIsSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteChaseSpeedTakesEffectTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	// WHY THIS EXISTS. The project owner reported on 2026-08-07 that the chase
	// animation "is the same -- he's not actually moving faster than the walk,
	// just animating faster", after Cataclysm.Brute.ChaseSpeed was added for
	// exactly that. Two things could produce that report: the console variable
	// was not set, or it does not work. This test settles which.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	IConsoleVariable* ChaseSpeed = IConsoleManager::Get().FindConsoleVariable(
		TEXT("Cataclysm.Brute.ChaseSpeed"));
	if (!ChaseSpeed)
	{
		AddError(TEXT("Cataclysm.Brute.ChaseSpeed is not registered."));
		return false;
	}
	const float Original = ChaseSpeed->GetFloat();
	ON_SCOPE_EXIT { ChaseSpeed->Set(Original); };

	FScopedBrute Brute(World, FVector::ZeroVector);
	ACataclysmEnemyController* Brain = Brute.Brain();
	UCharacterMovementComponent* Movement = Brute.Actor->GetCharacterMovement();
	if (!Brain || !Movement)
	{
		AddError(TEXT("A spawned Brute has no controller or no movement."));
		return false;
	}

	const float Designed =
		ACataclysmBruteCharacter::DesignedWalkSpeedCmPerSecond;

	const float DesignedChase =
		ACataclysmBruteCharacter::DesignedChaseSpeedCmPerSecond;

	TestTrue(TEXT("the designed chase speed is faster than the patrol speed, "
				  "or noticing the player would change nothing"),
		DesignedChase > Designed);

	// Nothing in sight and no override: the patrol speed.
	ChaseSpeed->Set(0.0f);
	Brain->Think();
	Brute.Actor->ApplyChaseSpeed();
	TestEqual(TEXT("wandering, it moves at its designed patrol speed"),
		Movement->MaxWalkSpeed, Designed);

	// Chasing, no override: the designed CHASE speed. This is the change made
	// on 2026-08-07 -- zero used to mean "no change at all" because there was
	// no designed second speed to fall back on. There is one now.
	FScopedFighter Player(World, FVector(5 * M, 0, 0), ECataclysmTeam::Players,
						  /*Health=*/1000.0f, /*AttackDamage=*/0.0f);
	TestEqual(TEXT("it is chasing"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Chasing));
	Brute.Actor->ApplyChaseSpeed();
	TestEqual(TEXT("chasing with no override, it moves at the designed chase speed"),
		Movement->MaxWalkSpeed, DesignedChase);

	// Chasing with an override: it moves at that speed instead.
	constexpr float Faster = 320.0f;
	ChaseSpeed->Set(Faster);
	Brain->Think();
	Brute.Actor->ApplyChaseSpeed();
	TestEqual(TEXT("chasing with an override set, it moves at the override"),
		Movement->MaxWalkSpeed, Faster);
	TestNotEqual(TEXT("which is not the designed chase speed, so the override "
					  "is really what is being read"),
		Movement->MaxWalkSpeed, DesignedChase);

	// AND IT GOES BACK WHEN THE CHASE ENDS, or a Brute that once saw the player
	// would wander at chase speed for the rest of its life.
	Player.Actor->SetActorLocation(FVector(30 * M, 0, 0));
	TestNotEqual(TEXT("the player leaves and it stops chasing"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Chasing));
	Brute.Actor->ApplyChaseSpeed();
	TestEqual(TEXT("so it drops back to its designed speed to wander"),
		Movement->MaxWalkSpeed, Designed);

	// AND CLEARING THE OVERRIDE RESTORES THE DESIGNED SPEED MID-CHASE, so that
	// setting it back to zero while watching undoes it immediately.
	Player.Actor->SetActorLocation(FVector(5 * M, 0, 0));
	Brain->Think();
	Brute.Actor->ApplyChaseSpeed();
	TestEqual(TEXT("chasing again at the override"),
		Movement->MaxWalkSpeed, Faster);

	ChaseSpeed->Set(0.0f);
	Brute.Actor->ApplyChaseSpeed();
	TestEqual(TEXT("and clearing it returns to the designed chase speed at once, "
				   "not to the patrol speed"),
		Movement->MaxWalkSpeed, DesignedChase);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteChaseStateComesFromTheBrainTest,
	"Cataclysm.AI.ABruteAsksItsBrainWhetherItIsChasing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteChaseStateComesFromTheBrainTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedBrute Brute(World, FVector::ZeroVector);
	ACataclysmEnemyController* Brain = Brute.Brain();
	if (!Brain)
	{
		AddError(TEXT("A spawned Brute has no controller."));
		return false;
	}

	FScopedFighter Player(World, FVector(20 * M, 0, 0), ECataclysmTeam::Players,
						  /*Health=*/1000.0f, /*AttackDamage=*/0.0f);

	// Out of sight: it wanders, and wandering is not chasing.
	Brain->Think();
	TestFalse(TEXT("a wandering Brute is not chasing"), Brute.Actor->IsChasing());

	// Inside the notice radius, outside reach: chasing.
	Player.Actor->SetActorLocation(FVector(5 * M, 0, 0));
	TestEqual(TEXT("with the player five metres away it chases"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Chasing));
	TestTrue(TEXT("and it reports itself as chasing"), Brute.Actor->IsChasing());

	// In reach: attacking, which deliberately does NOT count as chasing. It has
	// stopped moving by then, so the standing animation is the right one.
	Player.Actor->SetActorLocation(FVector(80.0f, 0.0f,
		Player.Actor->GetActorLocation().Z));
	TestEqual(TEXT("in reach it attacks"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Attacking));
	TestFalse(TEXT("and an attacking Brute is not chasing, because it has stopped"),
		Brute.Actor->IsChasing());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRoamLegsAreWorthWalkingTest,
	"Cataclysm.AI.ARoamingBruteDoesNotPickSomewhereItIsAlreadyStanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRoamLegsAreWorthWalkingTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	// WHAT THIS GUARDS, reported from a play session on 2026-08-07: the Brute
	// took "weird half steps one at a time, waits a few seconds, moves again".
	// A roam target is a random reachable point and nothing stopped it landing
	// where the character already stands. One that does counts as arrived on
	// the next pass without a step, then holds the full two second pause before
	// drawing again.
	//
	// STATISTICAL, BECAUSE THE DRAW IS RANDOM, and the two cases are far enough
	// apart that this is not a close call. The Brute may roam 600 cm and walks
	// 250 cm in the second that a leg is required to last, so an unfiltered
	// uniform draw lands too close (250/600)^2 = 17% of the time, about 17 in
	// 100. Re-drawing up to six times makes it 0.17^6, about two in a hundred
	// thousand. A threshold of 5 in 100 sits between the two with room to spare
	// either way.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedBrute Brute(World, FVector::ZeroVector);
	ACataclysmEnemyController* Brain = Brute.Brain();
	if (!Brain)
	{
		AddError(TEXT("A spawned Brute has no controller."));
		return false;
	}

	const float Speed = Brute.Actor->GetCharacterMovement()->GetMaxSpeed();
	const float ShortestWorthwhile =
		Speed * ACataclysmEnemyController::ShortestWorthwhileRoamLegSeconds;

	TestTrue(TEXT("a worthwhile leg is longer than the arrival radius, or "
				  "arriving would be instant however far it walked"),
		ShortestWorthwhile > ACataclysmEnemyController::RoamAcceptanceRadiusCm);
	TestTrue(TEXT("and shorter than the roam radius, or nothing would qualify"),
		ShortestWorthwhile < ACataclysmBruteCharacter::BruteRoamRadiusCm);

	constexpr int32 Legs = 100;
	int32 TooShort = 0;
	float Shortest = TNumericLimits<float>::Max();

	for (int32 Leg = 0; Leg < Legs; ++Leg)
	{
		// Back to a standing start each time: no target held, no pause running.
		// The character does not move in a world with no navigation, so it is
		// still on its anchor.
		Brain->bHasRoamTarget = false;
		Brain->RoamPauseUntil = 0.0f;

		if (Brain->Think() != ECataclysmBrainAction::Roaming
			|| !Brain->bHasRoamTarget)
		{
			AddError(FString::Printf(
				TEXT("Leg %d did not produce a roam target."), Leg));
			return false;
		}

		const float Length = FVector::Dist2D(
			Brute.Actor->GetActorLocation(), Brain->RoamTarget);
		Shortest = FMath::Min(Shortest, Length);
		if (Length < ShortestWorthwhile)
		{
			++TooShort;
		}
	}

	TestTrue(FString::Printf(
		TEXT("almost every roam leg is worth walking: %d of %d were shorter "
			 "than %.0f cm, shortest was %.0f cm"),
		TooShort, Legs, ShortestWorthwhile, Shortest),
		TooShort < 5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteAttacksAtTrueContactTest,
	"Cataclysm.AI.ABruteAttacksWhenItsCapsuleIsTouchingThePlayers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteAttacksAtTrueContactTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	// THE CASE THE GAME ACTUALLY PRODUCES, WHICH 80 CM IS NOT. Two capsules
	// cannot overlap, so the closest a Brute can physically stand to the player
	// is the sum of their radii, 48 + 42 = 90 cm. Its reach is also exactly 90.
	// So in a real session the chase always ends at the one distance where the
	// comparison has no margin at all, and whether it attacks is decided by
	// whether the separation lands a hair under or a hair over.
	//
	// The project owner reported on 2026-08-07 that the Brute "doesn't actually
	// attack when he reaches me". This is the test for that.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedBrute Brute(World, FVector::ZeroVector);
	Brute.Actor->SetAttackDamage(35.0f);

	ACataclysmEnemyController* Brain = Brute.Brain();
	if (!Brain)
	{
		AddError(TEXT("A spawned Brute has no controller."));
		return false;
	}

	FScopedFighter Player(World, FVector(20 * M, 0, 0), ECataclysmTeam::Players,
						  /*Health=*/1000.0f, /*AttackDamage=*/0.0f);

	// Exactly touching: the two capsule radii, and not a centimetre less.
	const float Contact = ACataclysmBruteCharacter::BruteCapsuleRadius
		+ PlayerCapsuleRadiusCm;
	Player.Actor->SetActorLocation(FVector(Contact, 0.0f,
		Player.Actor->GetActorLocation().Z));

	const float Apart = FVector::Dist2D(
		Brute.Actor->GetActorLocation(), Player.Actor->GetActorLocation());
	TestEqual(TEXT("the two are exactly at contact distance"), Apart, Contact);
	TestEqual(TEXT("which is exactly the Brute's designed reach"),
		Contact, ACataclysmBruteCharacter::DesignedMeleeReachCm);

	const float Before = Player.Health();
	TestEqual(TEXT("a Brute touching the player attacks rather than chasing"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Attacking));
	TestTrue(FString::Printf(TEXT("and the player lost health (%.0f to %.0f)"),
		Before, Player.Health()), Player.Health() < Before);

	// AND A HAIR FURTHER OUT, which is where a collision solver realistically
	// leaves two touching capsules. Without a tolerance this is the case that
	// makes a Brute chase for ever without ever landing a hit.
	Player.Actor->SetActorLocation(FVector(Contact + 1.0f, 0.0f,
		Player.Actor->GetActorLocation().Z));

	const float BeforeNudged = Player.Health();
	TestEqual(TEXT("a Brute one centimetre outside contact still attacks"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Attacking));
	TestTrue(TEXT("though the attack interval may hold the hit back"),
		Player.Health() <= BeforeNudged);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteChasesThenGoesBackToRoamingTest,
	"Cataclysm.AI.ABruteStopsRoamingToChaseAndRoamsAgainWhenThePlayerLeaves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteChasesThenGoesBackToRoamingTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedBrute Brute(World, FVector::ZeroVector);

	// FAR ENOUGH AWAY TO BE INVISIBLE TO IT. Twenty metres against a notice
	// radius of seven.
	FScopedFighter Player(World, FVector(20 * M, 0, 0), ECataclysmTeam::Players,
						  /*Health=*/1000.0f, /*AttackDamage=*/0.0f);

	ACataclysmEnemyController* Brain = Brute.Brain();
	if (!Brain)
	{
		AddError(TEXT("A spawned Brute has no controller."));
		return false;
	}

	TestEqual(TEXT("with the player far away the Brute roams"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Roaming));
	TestTrue(TEXT("and has somewhere it is going"), Brain->bHasRoamTarget);

	const FVector AbandonedTarget = Brain->RoamTarget;

	// The player walks up to five metres, inside the seven metre notice radius
	// and outside the 90 cm reach.
	Player.Actor->SetActorLocation(FVector(5 * M, 0, 0));

	TestEqual(TEXT("the player stepping inside its notice radius makes it chase"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Chasing));
	TestEqual(TEXT("and the player is what it is going after"),
		Brain->CurrentTarget.Get(), static_cast<AActor*>(Player.Actor));
	TestFalse(TEXT("and the place it was wandering to is forgotten"),
		Brain->bHasRoamTarget);

	// The player leaves again. This is item 7 of what was asked for: it goes
	// back to roaming rather than standing still where the chase ended.
	Player.Actor->SetActorLocation(FVector(20 * M, 0, 0));

	TestEqual(TEXT("the player leaving puts it back to roaming"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Roaming));
	TestNull(TEXT("with no target any more"), Brain->CurrentTarget.Get());
	TestTrue(TEXT("and somewhere new to go"), Brain->bHasRoamTarget);

	// A FRESH POINT, NOT THE ONE IT ABANDONED. Resuming the old target would
	// send it back to a place it chose for reasons that stopped applying when
	// it noticed the player. The two points are drawn from a continuous
	// distribution, so being equal would mean the target was never cleared.
	TestNotEqual(TEXT("and it is a freshly chosen point, not the abandoned one"),
		Brain->RoamTarget, AbandonedTarget);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
