// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmTargetCandidates.h"
#include "Components/CapsuleComponent.h"
#include "Dungeon/CataclysmFloorBrief.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for a creature's target search since issue #1547, which looks at lists
 * of the characters it could attack instead of asking the physics engine for
 * every body in range.
 *
 * WHAT THESE GUARD. The change is only worth having if it chooses the target the
 * sphere chose, is cheap for the reason it was made, and is never out of date.
 * One test for each:
 * - both searches run side by side in every arrangement the plan on #1547
 *   lists, and must give the same answer;
 * - a monster standing in a wave of its own side must look at the player's side
 *   and nothing else;
 * - an arrival, a departure and Madness, each made between two searches with the
 *   clock standing still, must be seen by the second search.
 *
 * A TEST WORLD HAS NO ART, so every character here is its capsule alone, which
 * is the one case where the two searches measure exactly the same thing. The
 * class comment on UCataclysmTargetCandidates says what differs with the art
 * loaded.
 */

namespace CataclysmTargetCandidatesTest
{
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}

	/** Metres, so these read the way the design document does. */
	constexpr float M = 100.0f;

	static FGenericTeamId PlayersSide()
	{
		return UCataclysmTeams::IdFor(ECataclysmTeam::Players);
	}

	static FGenericTeamId MonstersSide()
	{
		return UCataclysmTeams::IdFor(ECataclysmTeam::Monsters);
	}

	/**
	 * A character on the given side. An enemy character re-teamed for the
	 * player's side, because a player pawn's ability system lives on a player
	 * state that a synthetic world has no controller to create.
	 */
	static ACataclysmEnemyCharacter* SpawnOn(UWorld* World, const FVector& Where,
											 const FGenericTeamId& Side)
	{
		ACataclysmEnemyCharacter* Made =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(Side);
			Made->SetHealth(1000.0f);
			Made->SetAttackDamage(0.0f);
		}
		return Made;
	}

	/** The search exactly as ChooseTarget made it before issue #1547. */
	static AActor* WhatTheSphereChooses(const ACataclysmCharacterBase* Searcher)
	{
		const TArray<AActor*> Nearby = UCataclysmTargeting::FindEnemiesInSphere(
			Searcher->GetWorld(), Searcher, Searcher->GetActorLocation(),
			Searcher->NoticesFromCm(), /*MaxTargets=*/1);
		return Nearby.IsEmpty() ? nullptr : Nearby[0];
	}

	/** The search as the creature's own controller makes it now. */
	static AActor* WhatTheControllerChooses(const ACataclysmCharacterBase* Searcher)
	{
		const ACataclysmEnemyController* Brain =
			Cast<ACataclysmEnemyController>(Searcher->GetController());
		return Brain ? Brain->ChooseTarget() : nullptr;
	}

	static FString NameOf(const AActor* Actor)
	{
		return Actor ? Actor->GetName() : FString(TEXT("nothing"));
	}

	/**
	 * Both answers for one searcher, checked against each other. Returns the
	 * controller's, so a caller can also say what it should have been.
	 */
	static AActor* BothAgree(FAutomationTestBase& Test, const FString& Who,
							 const ACataclysmCharacterBase* Searcher)
	{
		AActor* Sphere = WhatTheSphereChooses(Searcher);
		AActor* Lists = WhatTheControllerChooses(Searcher);
		Test.TestTrue(
			FString::Printf(TEXT("%s: the lists chose %s and the sphere %s"),
							*Who, *NameOf(Lists), *NameOf(Sphere)),
			Lists == Sphere);
		return Lists;
	}

	/**
	 * Madness for longer than any test takes. Applied directly, the way
	 * Cataclysm.AI.AMaddenedEnemyAttacksAnythingNearbyFriendOrFoe applies it:
	 * what the tag does is under test here, not how a skill grants it.
	 */
	static bool Madden(AActor* Actor)
	{
		UCataclysmSkillEffects::ApplyTagForDuration(
			Actor, Actor, UCataclysmTeams::MadnessTag(), /*DurationSeconds=*/60.0f);
		return UCataclysmTeams::IsMaddened(Actor);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetListsChooseWhatTheSphereChose,
	"Cataclysm.AI.TheTargetListsChooseWhatTheSphereChose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTargetListsChooseWhatTheSphereChose::RunTest(const FString&)
{
	using namespace CataclysmTargetCandidatesTest;

	// ---- THE EDGE OF AN ORDINARY FLOOR'S NOTICE DISTANCE, FROM BOTH SIDES. The
	// sphere counts a capsule that reaches into it, not only one whose centre is
	// inside, so the lists must count the same.
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };

		if (!TestNotNull(TEXT("the world has the target lists, so the controller's "
							  "answer below is the new search and not the sphere"),
				UCataclysmTargetCandidates::In(World)))
		{
			return false;
		}

		ACataclysmEnemyCharacter* Monster =
			SpawnOn(World, FVector::ZeroVector, MonstersSide());
		ACataclysmEnemyCharacter* Target =
			SpawnOn(World, FVector(40 * M, 0.0f, 0.0f), PlayersSide());
		if (!TestNotNull(TEXT("a monster"), Monster)
			|| !TestNotNull(TEXT("something for it to notice"), Target))
		{
			return false;
		}

		const float Notice = Monster->NoticesFromCm();
		const float Reach = Target->GetCapsuleComponent()->GetScaledCapsuleRadius();

		// FIVE CENTIMETRES EACH WAY: far more than either search's rounding, and
		// far less than a capsule.
		Target->SetActorLocation(FVector(Notice + Reach - 5.0f, 0.0f, 0.0f));
		TestEqual(TEXT("a capsule whose centre is outside the notice distance but "
					   "which reaches 5 cm into it is found by both"),
			BothAgree(*this, TEXT("just inside"), Monster),
			static_cast<AActor*>(Target));

		Target->SetActorLocation(FVector(Notice + Reach + 5.0f, 0.0f, 0.0f));
		TestNull(TEXT("and one that stops 5 cm short is found by neither"),
			BothAgree(*this, TEXT("just outside"), Monster));
	}

	// ---- A HORDE ARENA: a notice distance wider than the arena, and a wave of
	// the searcher's own side standing between it and the player's.
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };

		ACataclysmEnemyCharacter* Searcher =
			SpawnOn(World, FVector::ZeroVector, MonstersSide());
		if (!TestNotNull(TEXT("a monster"), Searcher))
		{
			return false;
		}
		Searcher->SightRadiusMultiplier =
			FCataclysmDungeonFloorRules::HordeSightRadiusMultiplier;

		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Column = 0; Column < 5; ++Column)
			{
				SpawnOn(World, FVector((3.0f + 2.0f * Column) * M,
									   (-4.0f + 2.0f * Row) * M, 0.0f),
						MonstersSide());
			}
		}

		ACataclysmEnemyCharacter* Further =
			SpawnOn(World, FVector(150 * M, 37 * M, 0.0f), PlayersSide());
		ACataclysmEnemyCharacter* Nearer =
			SpawnOn(World, FVector(-90 * M, 11 * M, 0.0f), PlayersSide());
		if (!TestNotNull(TEXT("one on the player's side"), Further)
			|| !TestNotNull(TEXT("and another"), Nearer))
		{
			return false;
		}

		TestEqual(TEXT("across a Horde arena and past twenty monsters, both find "
					   "the nearer of two on the player's side"),
			BothAgree(*this, TEXT("Horde arena"), Searcher),
			static_cast<AActor*>(Nearer));
	}

	// ---- EVERY KIND OF SIDE, THE DEAD, AND MADNESS: the arrangement the plan on
	// #1547 lists, all within seven metres of each other so that nothing here is
	// near anybody's notice distance and the edge is left to the first case.
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };

		// THE PLAYER'S SIDE: a commander, two minions it summoned, a thrall it
		// took, and one of its own that is dead.
		ACataclysmEnemyCharacter* Commander =
			SpawnOn(World, FVector::ZeroVector, PlayersSide());
		if (!TestNotNull(TEXT("a commander"), Commander))
		{
			return false;
		}
		ACataclysmMinion* Minion = ACataclysmMinion::Spawn(
			Commander, FVector(1.6f * M, 0.7f * M, 0.0f), /*Lifetime=*/60.0f,
			/*bBurns=*/false);
		ACataclysmMinion* SecondMinion = ACataclysmMinion::Spawn(
			Commander, FVector(-1.9f * M, 1.3f * M, 0.0f), /*Lifetime=*/60.0f,
			/*bBurns=*/false);
		ACataclysmEnemyCharacter* Thrall =
			SpawnOn(World, FVector(0.4f * M, -2.1f * M, 0.0f), MonstersSide());
		ACataclysmEnemyCharacter* Corpse =
			SpawnOn(World, FVector(-1.5f * M, -0.6f * M, 0.0f), PlayersSide());

		// THE MONSTERS, at uneven distances so no searcher has two candidates the
		// same distance away, and one character that is on no side at all.
		ACataclysmEnemyCharacter* Near =
			SpawnOn(World, FVector(3.1f * M, -1.2f * M, 0.0f), MonstersSide());
		ACataclysmEnemyCharacter* Far =
			SpawnOn(World, FVector(-3.4f * M, -1.6f * M, 0.0f), MonstersSide());
		ACataclysmEnemyCharacter* Crazed =
			SpawnOn(World, FVector(2.2f * M, 2.9f * M, 0.0f), MonstersSide());
		ACataclysmEnemyCharacter* Loner =
			SpawnOn(World, FVector(-0.8f * M, 3.6f * M, 0.0f), FGenericTeamId::NoTeam);

		if (!TestNotNull(TEXT("a minion"), Minion)
			|| !TestNotNull(TEXT("a second minion"), SecondMinion)
			|| !TestNotNull(TEXT("a creature to take"), Thrall)
			|| !TestNotNull(TEXT("a corpse"), Corpse)
			|| !TestNotNull(TEXT("a near monster"), Near)
			|| !TestNotNull(TEXT("a far monster"), Far)
			|| !TestNotNull(TEXT("a monster to madden"), Crazed)
			|| !TestNotNull(TEXT("a character with no side"), Loner))
		{
			return false;
		}

		// EACH OF THESE IS WHAT MAKES ITS CASE MEAN SOMETHING, so each is checked
		// rather than assumed.
		TestTrue(TEXT("the thrall can be taken"),
			UCataclysmCommand::Subjugate(Commander, Thrall));
		TestTrue(TEXT("and is then on the commander's side"),
			UCataclysmTeams::TeamOf(Thrall) == PlayersSide());
		TestTrue(TEXT("the minions are on the commander's side"),
			UCataclysmTeams::TeamOf(Minion) == PlayersSide()
			&& UCataclysmTeams::TeamOf(SecondMinion) == PlayersSide());
		TestTrue(TEXT("the loner is on no side"),
			UCataclysmTeams::TeamOf(Loner) == FGenericTeamId::NoTeam);
		TestTrue(TEXT("the corpse is dead"), UCataclysmSkillEffects::MarkDead(Corpse));

		struct FSearcher
		{
			const TCHAR* Who;
			ACataclysmCharacterBase* Character;
		};
		const FSearcher Searchers[] = {
			{TEXT("the commander"), Commander},
			{TEXT("its minion"), Minion},
			{TEXT("its second minion"), SecondMinion},
			{TEXT("the thrall"), Thrall},
			{TEXT("the near monster"), Near},
			{TEXT("the far monster"), Far},
			{TEXT("the monster to be maddened"), Crazed},
			{TEXT("the character with no side"), Loner},
		};

		int32 Found = 0;
		for (const FSearcher& Each : Searchers)
		{
			Found += BothAgree(*this, FString(TEXT("before Madness, ")) + Each.Who,
							   Each.Character) != nullptr;
		}
		TestEqual(TEXT("every one of them found something, so the agreement above "
					   "is not eight searches finding nothing"), Found, 8);

		TestEqual(TEXT("the near monster goes for the nearest living character on "
					   "the player's side, a minion, and not the corpse nearer the "
					   "commander"),
			WhatTheControllerChooses(Near), static_cast<AActor*>(Minion));

		// MADNESS ON A MONSTER AND ON A MINION, with the clock where it was.
		if (!TestTrue(TEXT("a monster can be maddened"), Madden(Crazed))
			|| !TestTrue(TEXT("and so can a minion"), Madden(SecondMinion)))
		{
			return false;
		}

		for (const FSearcher& Each : Searchers)
		{
			BothAgree(*this, FString(TEXT("after Madness, ")) + Each.Who,
					  Each.Character);
		}

		TestEqual(TEXT("the commander goes for its own maddened minion, the nearest "
					   "thing that is now hostile to it"),
			WhatTheControllerChooses(Commander), static_cast<AActor*>(SecondMinion));
		TestEqual(TEXT("a monster not maddened goes for the maddened minion too"),
			WhatTheControllerChooses(Far), static_cast<AActor*>(SecondMinion));
		TestEqual(TEXT("and the maddened monster goes for whatever is nearest, "
					   "whichever side it is on"),
			WhatTheControllerChooses(Crazed), static_cast<AActor*>(Minion));
	}

	// ---- SEVERAL MADDENED AT ONCE, the way Anathema leaves a crowd: every
	// monster in it searches, maddened or not.
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };

		if (!TestNotNull(TEXT("one on the player's side"),
				SpawnOn(World, FVector(2.9f * M, 7.3f * M, 0.0f), PlayersSide())))
		{
			return false;
		}

		// TWO ROWS OF FIVE WITH UNEVEN GAPS, so no two candidates are the same
		// distance from any searcher.
		const float Across[] = {0.0f, 1.4f, 3.0f, 4.5f, 6.2f};
		TArray<ACataclysmEnemyCharacter*> Crowd;
		for (int32 Row = 0; Row < 2; ++Row)
		{
			for (const float X : Across)
			{
				Crowd.Add(SpawnOn(World,
					FVector((X + 0.35f * Row) * M, 1.9f * Row * M, 0.0f), MonstersSide()));
			}
		}

		for (int32 Index = 0; Index < Crowd.Num(); ++Index)
		{
			if (!TestNotNull(TEXT("a monster in the crowd"), Crowd[Index]))
			{
				return false;
			}
			if (Index % 3 == 0 && !TestTrue(TEXT("maddened"), Madden(Crowd[Index])))
			{
				return false;
			}
		}

		for (int32 Index = 0; Index < Crowd.Num(); ++Index)
		{
			BothAgree(*this,
				FString::Printf(TEXT("crowd monster %d, %s"), Index,
								Index % 3 == 0 ? TEXT("maddened") : TEXT("not maddened")),
				Crowd[Index]);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAMonsterLooksOnlyAtWhatItCouldAttack,
	"Cataclysm.AI.AMonstersTargetSearchLooksOnlyAtWhatItCouldAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAMonsterLooksOnlyAtWhatItCouldAttack::RunTest(const FString&)
{
	using namespace CataclysmTargetCandidatesTest;

	// **THIS IS WHAT THE CHANGE IS FOR.** A Horde wave of 139 creatures and one
	// player. Before issue #1547 every creature's search returned all 140 bodies
	// and refused 139 of them; the owner's capture measured that at 1.35 ms a
	// search with 125 to 149 creatures thinking. A monster that is not maddened
	// can only attack the player's side, so that is all it should look at.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UCataclysmTargetCandidates* Candidates = UCataclysmTargetCandidates::In(World);
	if (!TestNotNull(TEXT("the world has the target lists"), Candidates))
	{
		return false;
	}

	// TWELVE ACROSS AND UNEVEN GAPS, 1.5 metres one way and 1.7 the other, so no
	// creature has two neighbours the same distance away.
	constexpr int32 WaveSize = 139;
	TArray<ACataclysmEnemyCharacter*> Wave;
	for (int32 Index = 0; Index < WaveSize; ++Index)
	{
		ACataclysmEnemyCharacter* Creature = SpawnOn(World,
			FVector((Index % 12) * 1.5f * M, (Index / 12) * 1.7f * M, 0.0f),
			MonstersSide());
		if (!TestNotNull(TEXT("a creature of the wave"), Creature))
		{
			return false;
		}
		Creature->SightRadiusMultiplier =
			FCataclysmDungeonFloorRules::HordeSightRadiusMultiplier;
		Wave.Add(Creature);
	}

	ACataclysmEnemyCharacter* Player =
		SpawnOn(World, FVector(-30 * M, -30 * M, 0.0f), PlayersSide());
	if (!TestNotNull(TEXT("the player"), Player))
	{
		return false;
	}

	auto ChoiceOf = [](const ACataclysmEnemyCharacter* Creature) -> AActor*
	{
		const ACataclysmEnemyController* Brain =
			Cast<ACataclysmEnemyController>(Creature->GetController());
		return Brain ? Brain->ChooseTarget() : nullptr;
	};

	// THE FIRST SEARCH BUILDS THE LISTS, and looks at one entry: the player.
	TestEqual(TEXT("a creature of the wave goes for the player"),
		ChoiceOf(Wave[5]), static_cast<AActor*>(Player));
	TestEqual(TEXT("having looked at one character, not at 140"),
		Candidates->LookedAtByTheLastSearch(), 1);

	// EVERY OTHER CREATURE THINKING IN THE SAME FRAME READS THE SAME LISTS.
	const int32 BuiltBefore = Candidates->ListsBuiltSoFar();
	int32 WentForThePlayer = 0;
	int32 LookedAtOne = 0;
	for (const ACataclysmEnemyCharacter* Creature : Wave)
	{
		WentForThePlayer += ChoiceOf(Creature) == Player;
		LookedAtOne += Candidates->LookedAtByTheLastSearch() == 1;
	}
	TestEqual(TEXT("all 139 go for the player"), WentForThePlayer, WaveSize);
	TestEqual(TEXT("each having looked at one character"), LookedAtOne, WaveSize);
	TestEqual(TEXT("and 139 searches in one frame built the lists no more times"),
		Candidates->ListsBuiltSoFar(), BuiltBefore);

	// ONE OF THEM MADDENED. Its neighbours can now attack it, so they look at it
	// as well as the player, and at nobody else.
	if (!TestTrue(TEXT("a creature of the wave can be maddened"), Madden(Wave[0])))
	{
		return false;
	}
	TestEqual(TEXT("its neighbour goes for it rather than the distant player"),
		ChoiceOf(Wave[1]), static_cast<AActor*>(Wave[0]));
	TestEqual(TEXT("having looked at two characters: the player and it"),
		Candidates->LookedAtByTheLastSearch(), 2);

	// AND A MADDENED CREATURE CAN ATTACK ANYTHING, so it looks at everything.
	TestEqual(TEXT("the maddened creature goes for its nearest neighbour"),
		ChoiceOf(Wave[0]), static_cast<AActor*>(Wave[1]));
	TestEqual(TEXT("having looked at every character in the level"),
		Candidates->LookedAtByTheLastSearch(), WaveSize + 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTheTargetListsAreNeverOutOfDate,
	"Cataclysm.AI.TheTargetListsSeeAnArrivalADepartureAndMadnessAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTheTargetListsAreNeverOutOfDate::RunTest(const FString&)
{
	using namespace CataclysmTargetCandidatesTest;

	// THE LISTS ARE BUILT ONCE A FRAME, and a test calls Think many times inside
	// one frame without moving the clock. What changes in between has to be seen
	// all the same, or every test that spawns, kills or maddens something between
	// two thinking passes would be testing a stale list. The clock never moves in
	// this test, so each rebuild below is the one the change itself asked for.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UCataclysmTargetCandidates* Candidates = UCataclysmTargetCandidates::In(World);
	ACataclysmEnemyCharacter* Monster = SpawnOn(World, FVector::ZeroVector, MonstersSide());
	ACataclysmEnemyCharacter* Neighbour =
		SpawnOn(World, FVector(1.5f * M, 0.0f, 0.0f), MonstersSide());
	if (!TestNotNull(TEXT("the world has the target lists"), Candidates)
		|| !TestNotNull(TEXT("a monster"), Monster)
		|| !TestNotNull(TEXT("its neighbour"), Neighbour))
	{
		return false;
	}

	const ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(Monster->GetController());
	if (!TestNotNull(TEXT("the monster has a controller"), Brain))
	{
		return false;
	}

	TestNull(TEXT("two monsters have nothing to attack"), Brain->ChooseTarget());

	// NOTHING CHANGING BUILDS NOTHING, which is the control for everything below.
	int32 Built = Candidates->ListsBuiltSoFar();
	TestNull(TEXT("asked again with nothing changed"), Brain->ChooseTarget());
	TestEqual(TEXT("the second search reads the lists the first one built"),
		Candidates->ListsBuiltSoFar(), Built);

	// AN ARRIVAL.
	ACataclysmEnemyCharacter* Arrival =
		SpawnOn(World, FVector(5 * M, 0.0f, 0.0f), PlayersSide());
	if (!TestNotNull(TEXT("an arrival"), Arrival))
	{
		return false;
	}
	TestEqual(TEXT("a character spawned since the last search is found by the next"),
		Brain->ChooseTarget(), static_cast<AActor*>(Arrival));
	TestEqual(TEXT("because its spawn rebuilt the lists"),
		Candidates->ListsBuiltSoFar(), Built + 1);

	// A DEPARTURE. It would be refused as invalid from a stale list as well, so
	// the rebuild is what this checks.
	Built = Candidates->ListsBuiltSoFar();
	Arrival->Destroy();
	TestNull(TEXT("a character destroyed since the last search is gone from the next"),
		Brain->ChooseTarget());
	TestEqual(TEXT("because its destruction rebuilt the lists"),
		Candidates->ListsBuiltSoFar(), Built + 1);

	// MADNESS, on the monster's own side.
	Built = Candidates->ListsBuiltSoFar();
	if (!TestTrue(TEXT("the neighbour can be maddened"), Madden(Neighbour)))
	{
		return false;
	}
	TestEqual(TEXT("a neighbour maddened since the last search is a target at the next"),
		Brain->ChooseTarget(), static_cast<AActor*>(Neighbour));
	TestEqual(TEXT("because its Madness rebuilt the lists"),
		Candidates->ListsBuiltSoFar(), Built + 1);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
