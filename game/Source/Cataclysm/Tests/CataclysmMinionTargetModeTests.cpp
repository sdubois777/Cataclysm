// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Which enemy a minion goes for, from its type row. Issue #340.
 *
 * WHAT WAS WRONG. `game/Data/MinionTypes.csv` gives every minion a `TargetMode`,
 * "Nearest" for four rows and "Furthest" for the Ballista. Nothing read the
 * column, so every character in the game picked the nearest hostile and a
 * ballista was no exception.
 *
 * THE GAME PROMISED OTHERWISE IN WORDS A PLAYER READS. `War_Spear_Special` in
 * `game/Data/WeaponSkills.csv` says the ballista "fires massive bolts at the
 * furthest enemy within 15 meters every 2 seconds", and the skill table in
 * `docs/Cataclysm_GDD_v2.md` says the same. So this was a broken promise rather
 * than an unimplemented column.
 *
 * THESE DRIVE `ACataclysmEnemyController::ChooseTarget`, WHICH IS THE REAL PATH.
 * `ACataclysmMinion::AttackOnce` also finds a target, and its own comment says it
 * is "used by tests and by anything that wants one swing without a controller;
 * the ordinary case is the controller calling AttackTarget with what it chose".
 * A test written against that one would have passed without the game changing at
 * all.
 *
 * TWO OF THE THREE TESTS ARE ABOUT WHAT DID NOT MOVE. The change is in the
 * controller every character in the game shares, so "everything else still picks
 * the nearest" is the claim that needs evidence, not the feature. Each of those
 * two places a nearer AND a further candidate, so an implementation that always
 * returned the furthest would fail them.
 */
namespace CataclysmMinionTargetModeTest
{
	/** Metres, so these read the way the design document does. */
	constexpr float M = 100.0f;

	/** Near enough that both minions can see it. Both notice from 15 metres. */
	constexpr float NearMetres = 3.0f;

	/** Further, and still inside that 15. The gap is what the tests measure. */
	constexpr float FarMetres = 10.0f;

	static FGenericTeamId PlayersSide()
	{
		return UCataclysmTeams::IdFor(ECataclysmTeam::Players);
	}

	static FGenericTeamId MonstersSide()
	{
		return UCataclysmTeams::IdFor(ECataclysmTeam::Monsters);
	}

	/**
	 * A character on the given side, at the given distance along X.
	 *
	 * AN ENEMY CHARACTER RE-TEAMED, EVEN FOR THE PLAYER'S SIDE, which is what
	 * `CataclysmTargetCandidatesTests.cpp` does and for the reason it gives: a
	 * player pawn's ability system lives on a player state, and a synthetic world
	 * has no controller to create one.
	 */
	static ACataclysmEnemyCharacter* SpawnOn(UWorld* World, float Metres,
											 const FGenericTeamId& Side)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(Metres * M, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(Side);
			Made->SetHealth(1000.0f);
			Made->SetAttackDamage(0.0f);
		}
		return Made;
	}

	/** What this character's own brain picks, which is the path the game uses. */
	static AActor* WhatItPicks(const ACataclysmCharacterBase* Searcher)
	{
		const ACataclysmEnemyController* Brain =
			Cast<ACataclysmEnemyController>(Searcher->GetController());
		return Brain ? Brain->ChooseTarget() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBallistaPicksFurthestTest,
	"Cataclysm.MinionTargetMode.ABallistaShootsTheFurthestEnemyItCanSee",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Ballista's row says "Furthest" and now it does.
 *
 * THE PRECONDITION IS ASSERTED FIRST. A ballista whose row could not be found
 * keeps the defaults, and the default is nearest — which would fail this test for
 * a reason that has nothing to do with the rule.
 */
bool FCataclysmBallistaPicksFurthestTest::RunTest(const FString&)
{
	using namespace CataclysmMinionTargetModeTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Summoner = SpawnOn(World, 0.0f, PlayersSide());
	ACataclysmEnemyCharacter* Near = SpawnOn(World, NearMetres, MonstersSide());
	ACataclysmEnemyCharacter* Far = SpawnOn(World, FarMetres, MonstersSide());
	if (!TestNotNull(TEXT("a summoner"), Summoner)
		|| !TestNotNull(TEXT("a near enemy"), Near)
		|| !TestNotNull(TEXT("a far enemy"), Far))
	{
		return false;
	}

	ACataclysmMinion* Ballista = ACataclysmMinion::Spawn(
		Summoner, FVector::ZeroVector, /*Lifetime=*/20.0f, /*bBurns=*/false,
		TEXT("Ballista"));
	if (!TestNotNull(TEXT("a ballista"), Ballista))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Ballista)) { Ballista->Destroy(); } };

	if (!TestEqual(TEXT("it was made from the Ballista row"),
				   Ballista->TypeName, FString(TEXT("Ballista"))))
	{
		AddError(TEXT("DT_MinionTypes could not supply the Ballista row, so this "
					  "test cannot tell its target mode from the default. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	TestTrue(TEXT("and its row says it picks the furthest"),
			 Ballista->PicksTheFurthestTarget());

	TestEqual(TEXT("so it shoots the further of two enemies it can see"),
			  WhatItPicks(Ballista), static_cast<AActor*>(Far));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmImpPicksNearestTest,
	"Cataclysm.MinionTargetMode.AMinionWhoseRowSaysNearestStillPicksTheNearest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The other four rows say "Nearest" and must not have moved.
 *
 * THE SAME FIXTURE AS THE BALLISTA, DELIBERATELY. Both enemies are placed at the
 * same two distances, so this fails against an implementation that reads the
 * column and gets the sense backwards, and against one that gives every minion
 * the furthest.
 */
bool FCataclysmImpPicksNearestTest::RunTest(const FString&)
{
	using namespace CataclysmMinionTargetModeTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Summoner = SpawnOn(World, 0.0f, PlayersSide());
	ACataclysmEnemyCharacter* Near = SpawnOn(World, NearMetres, MonstersSide());
	ACataclysmEnemyCharacter* Far = SpawnOn(World, FarMetres, MonstersSide());
	if (!TestNotNull(TEXT("a summoner"), Summoner)
		|| !TestNotNull(TEXT("a near enemy"), Near)
		|| !TestNotNull(TEXT("a far enemy"), Far))
	{
		return false;
	}

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Summoner, FVector::ZeroVector, /*Lifetime=*/20.0f, /*bBurns=*/false,
		TEXT("Imp"));
	if (!TestNotNull(TEXT("an imp"), Imp))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	if (!TestEqual(TEXT("it was made from the Imp row"),
				   Imp->TypeName, FString(TEXT("Imp"))))
	{
		AddError(TEXT("DT_MinionTypes could not supply the Imp row. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	TestFalse(TEXT("its row does not say furthest"),
			  Imp->PicksTheFurthestTarget());

	TestEqual(TEXT("so it goes for the nearer of the two, as it always did"),
			  WhatItPicks(Imp), static_cast<AActor*>(Near));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOrdinaryCreatureUnmovedTest,
	"Cataclysm.MinionTargetMode.ACreatureThatIsNotAMinionStillPicksTheNearest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The evidence that matters most, because the change is in the controller every
 * character in the game shares.
 *
 * A CREATURE THAT IS NOT A MINION ANSWERS THE NEW QUESTION WITH THE BASE CLASS
 * DEFAULT, and this is what holds that default in place. Without it, a later
 * change to `ACataclysmCharacterBase::PicksTheFurthestTarget` would move every
 * monster in the game and no test would say so.
 */
bool FCataclysmOrdinaryCreatureUnmovedTest::RunTest(const FString&)
{
	using namespace CataclysmMinionTargetModeTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Monster = SpawnOn(World, 0.0f, MonstersSide());
	ACataclysmEnemyCharacter* Near = SpawnOn(World, NearMetres, PlayersSide());
	ACataclysmEnemyCharacter* Far = SpawnOn(World, FarMetres, PlayersSide());
	if (!TestNotNull(TEXT("a monster"), Monster)
		|| !TestNotNull(TEXT("a near target"), Near)
		|| !TestNotNull(TEXT("a far target"), Far))
	{
		return false;
	}

	TestFalse(TEXT("a plain creature does not pick the furthest"),
			  Monster->PicksTheFurthestTarget());

	TestEqual(TEXT("and it still goes for the nearer of two it can see"),
			  WhatItPicks(Monster), static_cast<AActor*>(Near));

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
