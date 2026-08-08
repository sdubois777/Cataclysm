// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmProjectile.h"
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
#include "Components/StaticMeshComponent.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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

	/**
	 * Move the world clock forward without ticking anything.
	 *
	 * WHY THIS IS NEEDED AT ALL. A world built by UWorld::CreateWorld is never
	 * ticked, so its clock never moves, so nothing that waits -- a cooldown, a
	 * wind-up, the pause between roam legs -- can ever finish. Before this,
	 * those could only be checked by reading the deadline the code stored and
	 * trusting the comparison against it.
	 *
	 * WHY WRITING THE CLOCK RATHER THAN TICKING. UWorld::TimeSeconds is public.
	 * Ticking a synthetic world runs physics, movement and animation, none of
	 * which these tests want and one of which is recorded on issue #374 as
	 * having hung the test process for minutes. Everything under test here reads
	 * the clock through GetTimeSeconds and nothing else, so moving the clock is
	 * the whole of what "time passed" means to it.
	 */
	static void AdvanceWorldClock(UWorld* World, double Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/**
	 * Run the Brute's abilities to completion so only its ordinary swing is
	 * left, and return how many it used.
	 *
	 * Several tests below are about chasing or swinging, and a Brute with an
	 * ability ready would rather use that. This spends them.
	 *
	 * IT ASKS BEFORE IT ACTS, and that is the point. Written to call Think and
	 * react to what came back, it performed the ordinary swing on the pass that
	 * found no ability left -- so a test that measured health afterwards saw no
	 * change, because the hit it was waiting for had already happened inside
	 * the helper. Asking ChooseAbility first means it stops without acting.
	 *
	 * @param DistanceCm  how far the target is, which decides what is available
	 */
	static int32 SpendAbilities(UWorld* World, ACataclysmEnemyController* Brain,
								float DistanceCm)
	{
		int32 Used = 0;
		for (int32 Pass = 0; Pass < 8; ++Pass)
		{
			if (Brain->ChooseAbility(DistanceCm) == INDEX_NONE)
			{
				break;
			}

			Brain->Think();                    // starts the wind-up
			AdvanceWorldClock(World, 2.0);     // past the longest of them, 1.4 s
			Brain->Think();                    // lands it
			++Used;
		}
		return Used;
	}

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

	// SPEND THE STOMP FIRST. A Brute in contact would rather stomp than swing,
	// because the stomp reaches from its own feet and hits for 250% against the
	// swing's 100%. This test is about the ordinary swing, which is what is left
	// once the stomp is cooling down.
	SpendAbilities(World, Brain, 80.0f);

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

	// SPEND THE ROCK THROW FIRST. At five metres the Brute would rather throw
	// than walk, and its throwing range is its whole notice radius, so it only
	// chases once that is cooling down. That is the behaviour, not a nuisance:
	// it closes while it waits to throw again.
	SpendAbilities(World, Brain, 5 * M);

	TestEqual(TEXT("with its abilities cooling down it chases"),
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

	// Inside the notice radius, outside reach, abilities spent: chasing.
	Player.Actor->SetActorLocation(FVector(5 * M, 0, 0));
	SpendAbilities(World, Brain, 5 * M);
	TestEqual(TEXT("with the player five metres away and its abilities spent, "
				   "it chases"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Chasing));
	TestTrue(TEXT("and it reports itself as chasing"), Brute.Actor->IsChasing());

	// In reach: attacking, which deliberately does NOT count as chasing. It has
	// stopped moving by then, so the standing animation is the right one.
	Player.Actor->SetActorLocation(FVector(80.0f, 0.0f,
		Player.Actor->GetActorLocation().Z));
	SpendAbilities(World, Brain, 80.0f);
	TestEqual(TEXT("in reach with its abilities spent it swings"),
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

	SpendAbilities(World, Brain, Contact);

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

	// THE FIRST THING IT DOES IS THROW A ROCK, not walk. Its throwing range is
	// its whole notice radius, so the moment it notices the player it has
	// something better to do than close the distance.
	TestEqual(TEXT("the player stepping inside its notice radius makes it "
				   "wind up a rock"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp));
	TestEqual(TEXT("and the player is what it is going after"),
		Brain->CurrentTarget.Get(), static_cast<AActor*>(Player.Actor));
	TestFalse(TEXT("and the place it was wandering to is forgotten"),
		Brain->bHasRoamTarget);

	// Once the rock is thrown and cooling down, it closes.
	SpendAbilities(World, Brain, 5 * M);
	TestEqual(TEXT("and with that cooling down it chases"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Chasing));

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

// --------------------------------------------------------------------------
// Abilities: choosing between them by range, and cooling down
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteChoosesAbilityByRangeTest,
	"Cataclysm.AI.ABruteChoosesItsAbilityByHowFarAwayYouAre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteChoosesAbilityByRangeTest::RunTest(const FString&)
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

	const TArray<FCataclysmEnemyAbility> Abilities = Brute.Actor->EnemyAbilities();
	TestEqual(TEXT("the Brute has two abilities beyond its ordinary swing"),
		Abilities.Num(), 2);
	TestEqual(TEXT("the stomp is first, so it wins where both reach"),
		Abilities[ACataclysmBruteCharacter::StompAbility].Name, FName(TEXT("Stomp")));
	TestEqual(TEXT("and the rock throw second"),
		Abilities[ACataclysmBruteCharacter::RockThrowAbility].Name,
		FName(TEXT("Rip and Toss")));

	// THE WHOLE POINT OF THE FEATURE, stated as a table of distances.
	const int32 Stomp = ACataclysmBruteCharacter::StompAbility;
	const int32 Throw = ACataclysmBruteCharacter::RockThrowAbility;

	TestEqual(TEXT("pressed against it: the stomp, which reaches from its feet"),
		Brain->ChooseAbility(50.0f), Stomp);
	TestEqual(TEXT("at three metres: still the stomp"),
		Brain->ChooseAbility(300.0f), Stomp);
	TestEqual(TEXT("at five metres, past the stomp: the rock"),
		Brain->ChooseAbility(500.0f), Throw);
	TestEqual(TEXT("at nine metres: still the rock"),
		Brain->ChooseAbility(900.0f), Throw);

	// BEYOND EVERYTHING, so it has nothing to use and must close instead.
	TestEqual(TEXT("at twelve metres, past the throw: nothing, so it walks"),
		Brain->ChooseAbility(1200.0f), int32(INDEX_NONE));

	// THE ROCK IS NOT THROWN AT SOMETHING IT COULD HIT. Inside melee reach the
	// stomp is the only candidate, and when it is cooling down the answer is
	// the ordinary swing rather than a rock at point blank range.
	TestTrue(TEXT("the rock throw does not start until beyond melee reach"),
		Abilities[Throw].MinRangeCm
			>= ACataclysmBruteCharacter::DesignedMeleeReachCm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteAbilitiesCoolDownTest,
	"Cataclysm.AI.ABruteWaitsOutACooldownAndSwingsMeanwhile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteAbilitiesCoolDownTest::RunTest(const FString&)
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
	Brute.Actor->SetAttackDamage(35.0f);
	ACataclysmEnemyController* Brain = Brute.Brain();
	if (!Brain)
	{
		AddError(TEXT("A spawned Brute has no controller."));
		return false;
	}

	const int32 Stomp = ACataclysmBruteCharacter::StompAbility;

	// EVERYTHING IS READY WHEN IT SPAWNS. Cooldowns start elapsed rather than
	// at a world time of zero, or a Brute in a fresh world could not stomp for
	// its first five seconds.
	TestTrue(TEXT("a freshly spawned Brute can stomp at once"),
		Brain->IsAbilityReady(Stomp));

	FScopedFighter Player(World, FVector(20 * M, 0, 0), ECataclysmTeam::Players,
						  /*Health=*/100000.0f, /*AttackDamage=*/0.0f);
	Player.Actor->SetActorLocation(FVector(200.0f, 0.0f,
		Player.Actor->GetActorLocation().Z));

	// Two metres away: inside the stomp, outside melee reach.
	TestEqual(TEXT("it starts winding up the stomp"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp));
	TestEqual(TEXT("and says which ability it is committed to"),
		Brain->WindingUpAbility, Stomp);

	// COMMITTED. The design says so, and this is where that is enforced: it
	// does not reconsider while the wind-up runs, whatever the target does.
	Player.Actor->SetActorLocation(FVector(20 * M, 0, 0));
	TestEqual(TEXT("the player running away does not cancel it"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp));
	TestEqual(TEXT("it is still the same ability"), Brain->WindingUpAbility, Stomp);

	// AND IT LANDS WHERE IT WAS MARKED. The aim point was recorded when the
	// wind-up started, which is what makes walking clear of one work.
	TestTrue(TEXT("the aim point is where the player was, not where they are"),
		FVector::Dist2D(Brain->WindUpAimedAt, FVector(200.0f, 0.0f, 0.0f)) < 100.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRockFliesWhereItWasAimedTest,
	"Cataclysm.AI.AThrownRockGoesWhereItWasAimedNotWhereYouWentAfterwards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRockFliesWhereItWasAimedTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	// THE ONLY ABILITY THE AIM POINT ACTUALLY AFFECTS, which is why this test
	// exists separately from the stomp's. The stomp is a ring at the Brute's
	// own feet, so it ignores where it was aimed entirely and walking out of it
	// works by leaving the Brute's radius. The rock is thrown at a point, and
	// nothing checked that the point was the marked one until this: a
	// deliberate break that made the throw follow the target passed every other
	// test in the suite.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	auto FlyProjectiles = [World]()
	{
		for (TActorIterator<ACataclysmProjectile> It(World); It; ++It)
		{
			// Generous steps: the rock travels 1200 cm a second and has at most
			// ten metres to cover, so forty passes is far more than enough.
			for (int32 Step = 0; Step < 40 && It->Step(0.05f); ++Step)
			{
			}
		}
	};

	FScopedBrute Brute(World, FVector::ZeroVector);
	Brute.Actor->SetAttackDamage(35.0f);
	ACataclysmEnemyController* Brain = Brute.Brain();
	if (!Brain)
	{
		AddError(TEXT("A spawned Brute has no controller."));
		return false;
	}

	// Five metres: past the stomp, inside the throw.
	FScopedFighter Player(World, FVector(5 * M, 0, 0), ECataclysmTeam::Players,
						  /*Health=*/100000.0f, /*AttackDamage=*/0.0f);

	TestEqual(TEXT("the Brute winds up a rock"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp));
	TestEqual(TEXT("and it is the rock, not the stomp"),
		Brain->WindingUpAbility,
		int32(ACataclysmBruteCharacter::RockThrowAbility));

	// SIDEWAYS, WELL CLEAR OF THE BLAST. The rock does not pierce, so its 210
	// cm radius is the blast where it stops; six metres to the side is clear of
	// that by a wide margin.
	Player.Actor->SetActorLocation(FVector(5 * M, 6 * M,
		Player.Actor->GetActorLocation().Z));

	const float Before = Player.Health();
	AdvanceWorldClock(World,
		ACataclysmBruteCharacter::RockThrowWindUpSeconds + 0.1);
	Brain->Think();
	FlyProjectiles();

	TestEqual(TEXT("a player who stepped aside took nothing"),
		Player.Health(), Before);

	// AND THE CONTROL, or the assertion above would pass on a rock that never
	// hits anyone at all.
	Player.Actor->SetActorLocation(FVector(5 * M, 0.0f,
		Player.Actor->GetActorLocation().Z));
	AdvanceWorldClock(World,
		ACataclysmBruteCharacter::RockThrowCooldownSeconds + 0.1);

	TestEqual(TEXT("it winds up another rock"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp));

	const float BeforeStanding = Player.Health();
	AdvanceWorldClock(World,
		ACataclysmBruteCharacter::RockThrowWindUpSeconds + 0.1);
	Brain->Think();
	FlyProjectiles();

	TestTrue(FString::Printf(
		TEXT("and a player who stood still was hit (%.0f to %.0f)"),
		BeforeStanding, Player.Health()),
		Player.Health() < BeforeStanding);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStompCanBeWalkedOutOfTest,
	"Cataclysm.AI.WalkingOutOfAStompDuringItsWindUpAvoidsItCompletely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStompCanBeWalkedOutOfTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	// THE POINT OF HAVING A TELEGRAPH AT ALL, and the design states it as a
	// rule: "leaving the area avoids the attack completely". Everything else
	// about the wind-up -- the state, the deadline, the recorded aim point --
	// is machinery in service of this one observable fact, and until this test
	// existed none of it was checked against what a player would actually feel.
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

	// Two metres away: inside the 3.5 metre stomp.
	FScopedFighter Player(World, FVector(20 * M, 0, 0), ECataclysmTeam::Players,
						  /*Health=*/100000.0f, /*AttackDamage=*/0.0f);
	Player.Actor->SetActorLocation(FVector(200.0f, 0.0f,
		Player.Actor->GetActorLocation().Z));

	TestEqual(TEXT("the Brute begins a stomp"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp));
	TestEqual(TEXT("and it is the stomp, not the rock"),
		Brain->WindingUpAbility, int32(ACataclysmBruteCharacter::StompAbility));

	// OUT OF THE RING BEFORE IT LANDS. The stomp reaches 3.5 metres from the
	// Brute's own feet, so five is clear of it.
	Player.Actor->SetActorLocation(FVector(5 * M, 0.0f,
		Player.Actor->GetActorLocation().Z));

	const float Before = Player.Health();
	AdvanceWorldClock(World, ACataclysmBruteCharacter::StompWindUpSeconds + 0.1);

	TestEqual(TEXT("the stomp lands"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Attacking));
	TestEqual(TEXT("and the player who walked clear took nothing"),
		Player.Health(), Before);

	// AND THE OTHER HALF, or the test above would pass on a stomp that never
	// hits anybody. Standing in it costs health.
	Player.Actor->SetActorLocation(FVector(200.0f, 0.0f,
		Player.Actor->GetActorLocation().Z));
	AdvanceWorldClock(World,
		ACataclysmBruteCharacter::StompCooldownSeconds + 0.1);

	TestEqual(TEXT("it begins another stomp"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp));

	const float BeforeStanding = Player.Health();
	AdvanceWorldClock(World, ACataclysmBruteCharacter::StompWindUpSeconds + 0.1);
	Brain->Think();

	TestTrue(FString::Printf(
		TEXT("and a player who stood in it lost health (%.0f to %.0f)"),
		BeforeStanding, Player.Health()),
		Player.Health() < BeforeStanding);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteAlwaysHasSomethingToDoTest,
	"Cataclysm.AI.ABruteInReachIsNeverIdleBecauseEverythingIsCoolingDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteAlwaysHasSomethingToDoTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	// THE PROPERTY THE DESIGN INSISTS ON. The Basic slot has no cooldown, so an
	// enemy in reach can never end up standing still with everything
	// unavailable. It is structural here rather than a rule to remember: the
	// ordinary swing is not in EnemyAbilities at all, so it cannot be cooling
	// down.
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

	for (const FCataclysmEnemyAbility& Ability : Brute.Actor->EnemyAbilities())
	{
		TestTrue(FString::Printf(
			TEXT("%s has a cooldown, so it cannot be the fallback"),
			*Ability.Name.ToString()),
			Ability.CooldownSeconds > 0.0f);
	}

	FScopedFighter Player(World, FVector(20 * M, 0, 0), ECataclysmTeam::Players,
						  /*Health=*/100000.0f, /*AttackDamage=*/0.0f);

	// Spend the stomp, then stand in contact. The stomp is on cooldown and the
	// rock throw does not reach point blank, so only the swing is left.
	Player.Actor->SetActorLocation(FVector(200.0f, 0.0f,
		Player.Actor->GetActorLocation().Z));
	SpendAbilities(World, Brain, 200.0f);
	TestFalse(TEXT("the stomp is now cooling down"),
		Brain->IsAbilityReady(ACataclysmBruteCharacter::StompAbility));

	const float Contact = ACataclysmBruteCharacter::DesignedMeleeReachCm
		- 10.0f;
	Player.Actor->SetActorLocation(FVector(Contact, 0.0f,
		Player.Actor->GetActorLocation().Z));

	TestEqual(TEXT("in contact with the stomp cooling down, it chooses nothing"),
		Brain->ChooseAbility(Contact), int32(INDEX_NONE));

	const float Before = Player.Health();
	TestEqual(TEXT("so it falls back to its ordinary swing rather than standing"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Attacking));
	TestTrue(FString::Printf(TEXT("and the player lost health (%.0f to %.0f)"),
		Before, Player.Health()), Player.Health() < Before);

	return true;
}

// --------------------------------------------------------------------------
// Seeing the thing in the air
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileIsVisibleTest,
	"Cataclysm.AI.AFiredProjectileHasSomethingOnScreenToSee",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileIsVisibleTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	// EVERY PROJECTILE IN THE GAME USED TO BE INVISIBLE, not just the Brute's
	// rock. ACataclysmProjectile had one component, an empty scene component to
	// give the actor a position, so a thrown rock and every player projectile
	// skill dealt their damage with nothing travelling between the two. Reported
	// from a play session on 2026-08-08 as the rock throw not throwing a rock.
	//
	// NO PARAGON ART NEEDED. The placeholder is an engine basic shape, so this
	// runs in continuous integration and on a fresh clone.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Thrower(World, FVector::ZeroVector, ECataclysmTeam::Monsters);

	ACataclysmProjectile* Rock = ACataclysmProjectile::Fire(
		Thrower.Actor, FVector::ZeroVector, FVector(10 * M, 0.0f, 0.0f),
		ACataclysmBruteCharacter::RockThrowRadiusCm,
		ACataclysmBruteCharacter::RockThrowSpeedCmPerSecond,
		/*InPierce=*/0, /*bInReturns=*/false,
		ACataclysmBruteCharacter::RockThrowDamagePercent,
		FGameplayTagContainer(), /*bInBurns=*/false);

	if (!Rock)
	{
		AddError(TEXT("Firing a projectile returned nothing."));
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Rock)) { Rock->Destroy(); } };

	if (!Rock->PlaceholderBody)
	{
		AddError(TEXT("A fired projectile has no placeholder body component, so "
					  "there is nothing on screen when one is thrown."));
		return false;
	}

	TestTrue(TEXT("the placeholder has a mesh set, so it draws something"),
		Rock->PlaceholderBody->GetStaticMesh() != nullptr);

	// SIZED TO WHAT IT HITS WITH, so what the player sees and what the sweep
	// uses cannot become two different widths.
	//
	// DOUBLE, NOT FLOAT. FVector components are double in Unreal 5, and mixing
	// the two makes TestEqual ambiguous and stops the module compiling.
	const double Expected = static_cast<double>(Rock->BodyRadiusCm * 2.0f)
		/ static_cast<double>(ACataclysmCharacterBase::BasicShapeSize);
	TestEqual(TEXT("and is scaled to the width the projectile actually hits with"),
		Rock->PlaceholderBody->GetRelativeScale3D().X, Expected);

	TestTrue(TEXT("which is a real size rather than zero"), Expected > 0.0);

	// IT MUST NOT COLLIDE. The projectile sweeps the world itself to find what
	// it passes through, so a colliding mesh would give it a second and
	// differently sized way to hit things.
	TestEqual(TEXT("the placeholder does not collide"),
		static_cast<int32>(Rock->PlaceholderBody->GetCollisionEnabled()),
		static_cast<int32>(ECollisionEnabled::NoCollision));

	return true;
}

// --------------------------------------------------------------------------
// Finishing an attack
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteFinishesItsAbilitiesTest,
	"Cataclysm.AI.ABrutePlaysTheSecondHalfOfAnAbilityInsteadOfCancelling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteFinishesItsAbilitiesTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	// WHAT THIS GUARDS. Reported from a play session on 2026-08-08: "he winds
	// up, and then cancels", for both the stomp and the rock throw. Each
	// ability is two clips and only the wind-up was ever played, so at the
	// moment of impact the mesh was handed straight back to the standing and
	// walking animations and the attack visibly stopped part way through.
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

	// BeginPlay does not fire in a world built this way, so the art half of
	// this test would silently never run without asking for the body directly.
	Brute.Actor->ResolveBody(/*bIncludeAnimation=*/true);

	FScopedFighter Player(World, FVector(20 * M, 0, 0), ECataclysmTeam::Players,
						  /*Health=*/100000.0f, /*AttackDamage=*/0.0f);
	Player.Actor->SetActorLocation(FVector(200.0f, 0.0f,
		Player.Actor->GetActorLocation().Z));

	if (Brute.Actor->StompReleaseAnimation == nullptr)
	{
		// The Paragon packs are gitignored, so this is the state in continuous
		// integration and on a fresh clone. Landing an ability must still work
		// and must not claim to be holding a mesh it has no animation for.
		Brute.Actor->UseEnemyAbility(ACataclysmBruteCharacter::StompAbility,
									 Player.Actor, FVector::ZeroVector);
		TestFalse(TEXT("with no release animation loaded it does not claim to hold "
					   "the mesh"),
			Brute.Actor->IsSwinging());
		AddInfo(TEXT("The Paragon art is absent, so only the no-art half of this "
					 "test ran. Install the pack to exercise the rest."));
		return true;
	}

	// --- The stomp ---------------------------------------------------------

	const float WindUpBegan = static_cast<float>(World->GetTimeSeconds());
	TestEqual(TEXT("the Brute begins a stomp"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp));

	// THE WIND-UP FILLS THE WHOLE TELEGRAPH. The ground smash wind-up clip is
	// 0.83 seconds inside a 1.4 second telegraph, so at normal speed it used to
	// finish and freeze on its last frame for the remaining half second.
	TestEqual(TEXT("and holds the mesh for the whole wind-up"),
		Brute.Actor->SwingUntilSeconds - WindUpBegan,
		ACataclysmBruteCharacter::StompWindUpSeconds);

	AdvanceWorldClock(World, ACataclysmBruteCharacter::StompWindUpSeconds + 0.1);

	const float Landed = static_cast<float>(World->GetTimeSeconds());
	TestEqual(TEXT("the stomp lands"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Attacking));

	// THE POINT OF THE WHOLE TEST. Before this, nothing was played when an
	// ability landed, so SwingUntilSeconds was already in the past here and
	// locomotion took the mesh back on the very next frame.
	TestTrue(TEXT("landing the stomp starts an animation"),
		Brute.Actor->IsSwinging());
	TestEqual(TEXT("and it is the release clip, held for its own length"),
		Brute.Actor->SwingUntilSeconds - Landed,
		Brute.Actor->StompReleaseAnimation->GetPlayLength());

	// --- The rock throw ----------------------------------------------------

	if (Brute.Actor->RockThrowReleaseAnimation == nullptr)
	{
		AddError(TEXT("The stomp release animation loaded but the rock throw one "
					  "did not, which means one of the two paths is wrong."));
		return false;
	}

	// Out to five metres: beyond the stomp's 3.5 m ring, inside the throw's 10 m.
	Player.Actor->SetActorLocation(FVector(5 * M, 0.0f,
		Player.Actor->GetActorLocation().Z));
	AdvanceWorldClock(World,
		ACataclysmBruteCharacter::RockThrowCooldownSeconds + 0.1);

	const float ThrowBegan = static_cast<float>(World->GetTimeSeconds());
	TestEqual(TEXT("the Brute begins a rock throw"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp));
	TestEqual(TEXT("and it is the rock, not the stomp"),
		Brain->WindingUpAbility,
		int32(ACataclysmBruteCharacter::RockThrowAbility));

	// THE RIP CLIP IS LONGER THAN ITS TELEGRAPH, which is the opposite problem
	// to the stomp's: at normal speed it was cut off before the rock left the
	// ground. Holding for the telegraph and deriving the play rate fixes both.
	TestEqual(TEXT("the wind-up holds the mesh for the whole telegraph"),
		Brute.Actor->SwingUntilSeconds - ThrowBegan,
		ACataclysmBruteCharacter::RockThrowWindUpSeconds);

	AdvanceWorldClock(World,
		ACataclysmBruteCharacter::RockThrowWindUpSeconds + 0.1);

	const float Thrown = static_cast<float>(World->GetTimeSeconds());
	Brain->Think();

	TestTrue(TEXT("landing the throw starts an animation"),
		Brute.Actor->IsSwinging());
	TestEqual(TEXT("and it is the throw clip, held for its own length"),
		Brute.Actor->SwingUntilSeconds - Thrown,
		Brute.Actor->RockThrowReleaseAnimation->GetPlayLength());

	// AND THE TWO RELEASES ARE DIFFERENT CLIPS, or a copy-paste in
	// ReleaseAnimationFor would give the Brute one animation for everything and
	// every assertion above would still pass.
	TestNotEqual(TEXT("the stomp and the throw release differently"),
		Brute.Actor->StompReleaseAnimation.Get(),
		Brute.Actor->RockThrowReleaseAnimation.Get());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteWindUpSpeedTest,
	"Cataclysm.AI.ABruteNeverPlaysAWindUpSlowerThanItWasAuthored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteWindUpSpeedTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	// THE PLAY RATE, READ OFF THE MESH RATHER THAN INFERRED. The test above
	// checks how long the mesh is HELD, which says nothing about the speed the
	// clip inside it runs at. This reads the rate that was actually set.
	//
	// THE RULE IS ASYMMETRIC AND THAT IS THE POINT. Stretching a short clip
	// across a long telegraph was tried and reported from a play session as a
	// glitch: the arms went up in slow motion and the release then ran at full
	// speed. A clip shorter than its window holds its last pose instead, which
	// is what Rampage's separate 0.03 second loop clip exists to do. A clip
	// LONGER than its window still has to be compressed or it is cut off.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedBrute Brute(World, FVector::ZeroVector);
	Brute.Actor->ResolveBody(/*bIncludeAnimation=*/true);

	if (Brute.Actor->StompAnimation == nullptr
		|| Brute.Actor->RockThrowAnimation == nullptr)
	{
		AddInfo(TEXT("The Paragon art is absent, so this test could not run. "
					 "Install the pack to exercise it."));
		return true;
	}

	USkeletalMeshComponent* MeshComponent = Brute.Actor->GetMesh();
	UAnimSingleNodeInstance* Single =
		MeshComponent ? MeshComponent->GetSingleNodeInstance() : nullptr;
	if (!Single)
	{
		AddInfo(TEXT("The mesh has no single node instance, so the play rate "
					 "could not be read. The hold-length assertions in the test "
					 "above still ran."));
		return true;
	}

	struct FCase
	{
		const TCHAR* What;
		int32 Ability;
		UAnimSequence* Clip;
		float Window;
	};

	const FCase Cases[] =
	{
		{ TEXT("the ground smash"), ACataclysmBruteCharacter::StompAbility,
		  Brute.Actor->StompAnimation.Get(),
		  ACataclysmBruteCharacter::StompWindUpSeconds },
		{ TEXT("the rock throw"), ACataclysmBruteCharacter::RockThrowAbility,
		  Brute.Actor->RockThrowAnimation.Get(),
		  ACataclysmBruteCharacter::RockThrowWindUpSeconds },
	};

	int32 Compressed = 0;
	for (const FCase& Case : Cases)
	{
		Brute.Actor->BeginEnemyAbilityWindUp(Case.Ability, nullptr);

		const float Length = Case.Clip->GetPlayLength();
		const float Rate = Single->GetPlayRate();

		TestEqual(FString::Printf(
			TEXT("%s plays its %.2f s clip inside a %.2f s telegraph at the "
				 "expected rate"),
			Case.What, Length, Case.Window),
			Rate, FMath::Max(1.0f, Length / Case.Window));

		// NEVER IN SLOW MOTION, whichever clip it is. This is the assertion the
		// play session produced.
		TestTrue(FString::Printf(
			TEXT("%s is never played slower than it was authored (rate %.3f)"),
			Case.What, Rate),
			Rate >= 1.0f);

		// AND IT ALWAYS FITS. A clip that still ran past its window would be cut
		// off, which is the fault the compression exists to prevent.
		TestTrue(FString::Printf(
			TEXT("%s finishes inside its telegraph (%.2f s at rate %.3f)"),
			Case.What, Length, Rate),
			Length / Rate <= Case.Window + KINDA_SMALL_NUMBER);

		if (Rate > 1.0f)
		{
			++Compressed;
		}
	}

	// AT LEAST ONE CLIP IS ACTUALLY LONGER THAN ITS WINDOW, or the compression
	// branch is never taken and the assertions above would pass on code that
	// simply returned 1.0 every time.
	TestTrue(TEXT("at least one wind-up clip has to be compressed to fit"),
		Compressed >= 1);

	return true;
}

// --------------------------------------------------------------------------
// Being stunned
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStompStunsWhatItCatchesTest,
	"Cataclysm.AI.AStompStunsEverythingItCatchesAndNotWhatWalkedClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStompStunsWhatItCatchesTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	// THE BRUTE IS THE FIRST THING IN THE GAME THAT STUNS THE PLAYER, which the
	// design states in terms at docs/Cataclysm_GDD_v2.md:3670. Until this the
	// Stomp dealt its 250% and let go immediately, and the class comment said so.
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

	// Two metres away: inside the 3.5 metre ring.
	FScopedFighter Player(World, FVector(20 * M, 0, 0), ECataclysmTeam::Players,
						  /*Health=*/100000.0f, /*AttackDamage=*/0.0f);
	Player.Actor->SetActorLocation(FVector(200.0f, 0.0f,
		Player.Actor->GetActorLocation().Z));

	TestFalse(TEXT("the player starts unstunned"),
		UCataclysmSkillEffects::IsStunned(Player.Actor));

	TestEqual(TEXT("the Brute begins a stomp"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp));
	TestFalse(TEXT("and beginning one stuns nobody -- the wind-up is a warning"),
		UCataclysmSkillEffects::IsStunned(Player.Actor));

	AdvanceWorldClock(World, ACataclysmBruteCharacter::StompWindUpSeconds + 0.1);
	Brain->Think();

	TestTrue(TEXT("when it lands the player is stunned"),
		UCataclysmSkillEffects::IsStunned(Player.Actor));

	// WALKING OUT AVOIDS THE STUN AND NOT ONLY THE DAMAGE. A telegraph that
	// spared the health but took the control would be worse than no telegraph,
	// because the player would have read it correctly and been punished anyway.
	FScopedFighter Bystander(World, FVector(9 * M, 0, 0), ECataclysmTeam::Players,
							 /*Health=*/100000.0f, /*AttackDamage=*/0.0f);

	AdvanceWorldClock(World,
		ACataclysmBruteCharacter::StompCooldownSeconds + 0.1);
	TestEqual(TEXT("it begins another stomp"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp));
	AdvanceWorldClock(World, ACataclysmBruteCharacter::StompWindUpSeconds + 0.1);
	Brain->Think();

	TestFalse(TEXT("and somebody nine metres away was not stunned"),
		UCataclysmSkillEffects::IsStunned(Bystander.Actor));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStunnedBruteDoesNothingTest,
	"Cataclysm.AI.AStunnedEnemyDoesNothingAndAbandonsAWindUpItHadStarted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStunnedBruteDoesNothingTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	// THE FIFTH WIND-UP RULE, WHICH NOTHING COULD SATISFY BEFORE. The design
	// gives the wind-up five rules and ECataclysmBrainAction records that the
	// fifth -- interrupting cancels it -- was not implemented because nothing in
	// the project could interrupt anything. A stun is the first thing that can.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedBrute Brute(World, FVector::ZeroVector);
	Brute.Actor->SetHealth(1000.0f);
	Brute.Actor->SetAttackDamage(35.0f);
	ACataclysmEnemyController* Brain = Brute.Brain();
	if (!Brain)
	{
		AddError(TEXT("A spawned Brute has no controller."));
		return false;
	}

	FScopedFighter Player(World, FVector(20 * M, 0, 0), ECataclysmTeam::Players,
						  /*Health=*/100000.0f, /*AttackDamage=*/0.0f);
	Player.Actor->SetActorLocation(FVector(200.0f, 0.0f,
		Player.Actor->GetActorLocation().Z));

	TestEqual(TEXT("the Brute begins a stomp"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp));
	TestEqual(TEXT("and it is committed to the stomp"),
		Brain->WindingUpAbility, int32(ACataclysmBruteCharacter::StompAbility));

	// Stunned half a second into a 1.4 second wind-up.
	AdvanceWorldClock(World, 0.5);
	TestTrue(TEXT("the player stuns it"),
		UCataclysmSkillEffects::ApplyStun(
			Player.Actor, Brute.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/1000.0f, /*bStunIsDesigned=*/true));

	TestEqual(TEXT("it now reports being stunned rather than winding up"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Stunned));
	TestEqual(TEXT("and the wind-up was abandoned, not paused"),
		Brain->WindingUpAbility, int32(INDEX_NONE));

	// PAST WHEN THE STOMP WOULD HAVE LANDED. This is the half that matters: a
	// stun that only blocked Think would leave WindUpLandsAt in the past, and
	// the first pass after the stun ended would land a stomp whose telegraph the
	// player had already survived.
	const float Before = Player.Health();
	AdvanceWorldClock(World, ACataclysmBruteCharacter::StompWindUpSeconds + 1.0);

	TestEqual(TEXT("it is still stunned"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Stunned));
	TestEqual(TEXT("and the stomp it had started never landed"),
		Player.Health(), Before);

	// IT IS NOT ON COOLDOWN EITHER, because it was never spent. An interrupted
	// attack did not happen; it did not happen AND cost the enemy its Heavy.
	TestTrue(TEXT("the stomp it never landed is still available"),
		Brain->IsAbilityReady(ACataclysmBruteCharacter::StompAbility));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStunnedBruteDoesNotSwingTest,
	"Cataclysm.AI.AStunnedEnemyInReachDoesNotSwing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStunnedBruteDoesNotSwingTest::RunTest(const FString&)
{
	using namespace CataclysmBehaviourTest;

	// THE ORDINARY SWING HAS NO COOLDOWN AND NO WIND-UP, so it is the attack a
	// stun that only cancelled wind-ups would fail to stop. The design says a
	// stunned target cannot act at all, not that it cannot use its abilities.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedBrute Brute(World, FVector::ZeroVector);
	Brute.Actor->SetHealth(1000.0f);
	Brute.Actor->SetAttackDamage(35.0f);
	ACataclysmEnemyController* Brain = Brute.Brain();
	if (!Brain)
	{
		AddError(TEXT("A spawned Brute has no controller."));
		return false;
	}

	FScopedFighter Player(World, FVector(20 * M, 0, 0), ECataclysmTeam::Players,
						  /*Health=*/100000.0f, /*AttackDamage=*/0.0f);

	// Spend the abilities, then stand in contact so only the swing is left.
	Player.Actor->SetActorLocation(FVector(200.0f, 0.0f,
		Player.Actor->GetActorLocation().Z));
	SpendAbilities(World, Brain, 200.0f);

	const float Contact = ACataclysmBruteCharacter::DesignedMeleeReachCm - 10.0f;
	Player.Actor->SetActorLocation(FVector(Contact, 0.0f,
		Player.Actor->GetActorLocation().Z));

	// THE CLOCK IS NOT ADVANCED HERE, AND THAT IS DELIBERATE. Both abilities are
	// cooling down for five seconds from when they landed inside SpendAbilities,
	// and winding the clock forward far enough to be sure the attack interval
	// had elapsed would also bring the stomp back -- at which point the Brute
	// stomps rather than swinging and this stops being a test about the swing.
	// It does not need to: the interval gate is `!bHasAttacked || ...`, and this
	// Brute has not swung yet.
	TestEqual(TEXT("with everything cooling down, only the swing is left"),
		Brain->ChooseAbility(Contact), int32(INDEX_NONE));

	const float BeforeUnstunned = Player.Health();
	TestEqual(TEXT("unstunned and in reach, it attacks"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Attacking));
	TestTrue(TEXT("and the player lost health"),
		Player.Health() < BeforeUnstunned);

	TestTrue(TEXT("the player stuns it"),
		UCataclysmSkillEffects::ApplyStun(
			Player.Actor, Brute.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/1000.0f, /*bStunIsDesigned=*/true));

	// Just past the attack interval, so the swing is due again and the stun is
	// the only thing holding it. Short enough to leave the stomp cooling down.
	AdvanceWorldClock(World,
		ACataclysmBruteCharacter::DesignedAttackIntervalSeconds + 0.1);
	TestEqual(TEXT("and the stomp is still cooling down, so the swing is due"),
		Brain->ChooseAbility(Contact), int32(INDEX_NONE));

	const float BeforeStunned = Player.Health();

	TestEqual(TEXT("stunned and in reach, it does nothing"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Stunned));
	TestEqual(TEXT("and the player took nothing"),
		Player.Health(), BeforeStunned);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
