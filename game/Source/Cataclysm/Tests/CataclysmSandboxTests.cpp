// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmHellhoundCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "GameFramework/PlayerStart.h"
#include "Player/CataclysmGameMode.h"

/**
 * Tests for the sandbox having something in it to fight.
 *
 * WHAT THESE GUARD. Issue #170: game/Content/Maps/L_Sandbox.umap held a floor,
 * a light, a sky, a player start and navigation, and no enemy at all. Every
 * skill behaviour built so far had nothing to act on, so a cone found no
 * targets, a projectile passed through empty space, and burning ground burned
 * nobody. All of it was covered by automation tests that spawn their own
 * actors, and none of it could be seen.
 *
 * WHAT THEY DELIBERATELY CHECK. Not only that actors appear, but that they are
 * actors the combat code can find and hurt -- which is the whole point of
 * putting them there. An enemy that spawns and is invisible to
 * UCataclysmTargeting would satisfy a naive count and change nothing.
 */

namespace CataclysmSandboxTest
{
	/**
	 * A world that has BEGUN PLAY, unlike the ones the other test files build.
	 *
	 * That matters here and nowhere else. An enemy registers its attribute sets
	 * in BeginPlay, and a world that never begins play never calls it -- so a
	 * spawned enemy would have no health attribute to read and this test would
	 * be checking nothing. Actors spawned after this point get their BeginPlay
	 * called as they spawn, which is the same order the real game uses.
	 */
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTrainingDummiesSpawnTest,
	"Cataclysm.Sandbox.TrainingDummiesAppearAndCanBeFoundAndHurt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTrainingDummiesSpawnTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSandboxTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmGameMode* GameMode = World->SpawnActor<ACataclysmGameMode>();
	if (!GameMode)
	{
		AddError(TEXT("Could not spawn the game mode."));
		return false;
	}

	// Called directly rather than through StartPlay, because StartPlay also
	// wants a player controller and a pawn and this test is about the dummies.
	// ASKED FOR EXPLICITLY RATHER THAN TAKEN FROM THE DEFAULT. The default is 0
	// as of 2026-08-07, because five cylinders crowd around the Brute and make it
	// hard to watch. This test is about whether the spawner works, so it says how
	// many it wants.
	GameMode->TrainingDummyCount = 5;
	const int32 Spawned = GameMode->SpawnTrainingDummies();

	TestTrue(FString::Printf(
		TEXT("The sandbox spawns training dummies (%d)"), Spawned), Spawned > 0);
	TestEqual(TEXT("And records every one it made"),
		GameMode->TrainingDummies.Num(), Spawned);

	if (Spawned == 0)
	{
		return false;
	}

	// THEY ARE ACTORS THE COMBAT CODE CAN FIND. A dummy the targeting cannot
	// see is a dummy no skill can hit, which would leave issue #170 open while
	// looking fixed. Searched from the centre of the ring with a radius wide
	// enough to cover it.
	AActor* Caster = World->SpawnActor<AActor>();
	const TArray<AActor*> Found = UCataclysmTargeting::FindEnemiesInSphere(
		World, Caster, GameMode->TrainingDummies[0]->GetActorLocation(),
		/*RadiusCm=*/200.0f);

	TestTrue(TEXT("A skill's targeting finds a dummy standing in front of it"),
		Found.Contains(GameMode->TrainingDummies[0]));

	// AND THEY CAN BE HURT. Health above zero is what makes damage mean
	// anything; a dummy at zero health is already dead.
	ACataclysmEnemyCharacter* First = GameMode->TrainingDummies[0];
	UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(First);
	if (!AbilitySystem)
	{
		AddError(TEXT("A training dummy has no ability system, so nothing can "
					  "damage it."));
		return false;
	}

	const float Health = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetHealthAttribute());
	const float MaxHealth = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute());

	TestTrue(FString::Printf(TEXT("A dummy has health (%.0f)"), Health),
		Health > 0.0f);
	TestEqual(TEXT("And starts at full"), Health, MaxHealth);

	// ENOUGH TO SURVIVE A HEAVY ATTACK, which is what makes the sandbox useful.
	// An unupgraded Greataxe supplies about 41 attack damage and the Heavy slot
	// is 250% of it, so roughly 102. A dummy at the enemy default of 100 health
	// dies to one press and there is nothing to watch.
	TestTrue(FString::Printf(
		TEXT("And enough of it to survive a Heavy Attack (%.0f)"), Health),
		Health > 200.0f);

	// They stand apart from each other, so an area skill hitting one is a
	// choice rather than a certainty.
	if (GameMode->TrainingDummies.Num() >= 2)
	{
		const float Apart = FVector::Dist(
			GameMode->TrainingDummies[0]->GetActorLocation(),
			GameMode->TrainingDummies[1]->GetActorLocation());
		TestTrue(FString::Printf(TEXT("Two dummies stand apart (%.0f cm)"), Apart),
			Apart > 100.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTrainingDummiesRingThePlayerStartTest,
	"Cataclysm.Sandbox.TrainingDummiesRingWhereThePlayerAppears",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTrainingDummiesRingThePlayerStartTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSandboxTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// AROUND THE PLAYER START, NOT THE WORLD ORIGIN. A ring at the origin is
	// useless in a level whose player start is somewhere else: the player spawns
	// with nothing near them and the sandbox looks empty. Placed well away from
	// the origin so the two cannot be confused.
	const FVector StartWhere(3000.0f, -2000.0f, 0.0f);
	APlayerStart* Start = World->SpawnActor<APlayerStart>(
		StartWhere, FRotator::ZeroRotator);
	if (!Start)
	{
		AddError(TEXT("Could not spawn a player start."));
		return false;
	}

	ACataclysmGameMode* GameMode = World->SpawnActor<ACataclysmGameMode>();
	if (!GameMode)
	{
		AddError(TEXT("Could not spawn the game mode."));
		return false;
	}

	// Asked for explicitly; see the note in the spawn test above.
	GameMode->TrainingDummyCount = 5;
	const int32 Spawned = GameMode->SpawnTrainingDummies();
	TestTrue(TEXT("Dummies were spawned"), Spawned > 0);

	for (const TObjectPtr<ACataclysmEnemyCharacter>& Dummy : GameMode->TrainingDummies)
	{
		if (!IsValid(Dummy))
		{
			AddError(TEXT("A recorded training dummy is not valid."));
			continue;
		}

		// Compared in the horizontal plane, because a spawn can be pushed
		// upward by the floor and the ring is a horizontal arrangement.
		FVector Offset = Dummy->GetActorLocation() - StartWhere;
		Offset.Z = 0.0f;

		TestTrue(FString::Printf(
			TEXT("A dummy stands near the player start, not the origin "
				 "(%.0f cm away)"), Offset.Size()),
			Offset.Size() < 1000.0f);

		// Nowhere near the world origin, which is 3606 cm from the start.
		TestTrue(TEXT("And not at the world origin"),
			Dummy->GetActorLocation().Size() > 1500.0f);
	}

	return true;
}

// --------------------------------------------------------------------------
// The designed creatures carry the model's figures. Issue #525
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSandboxEnemiesAreArmouredTest,
	"Cataclysm.Sandbox.TheDesignedCreaturesSpawnWithArmour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSandboxEnemiesAreArmouredTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSandboxTest;

	// WHAT THIS EXISTS FOR. Until issue #525 nothing outside a test ever called
	// ACataclysmEnemyCharacter::SetArmour, so StartingArmour stayed at its zero
	// default on every creature in the sandbox. The Brute is the one the design
	// calls heavily armoured and it had none, and the difficulty tier -- which
	// does nothing except divide an armour value -- was invisible in play.
	//
	// THE FIGURES THEMSELVES ARE PINNED IN PYTHON, by
	// tools/tests/test_brute_matches_the_model.py and its Warden counterpart,
	// which compare the header against sim/cataclysm_sim/enemy_stats.py. What
	// this checks is the half that reading the header cannot: that the number
	// reaches a live attribute on a spawned actor.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmGameMode* GameMode = World->SpawnActor<ACataclysmGameMode>();
	if (!GameMode)
	{
		AddError(TEXT("Could not spawn the game mode."));
		return false;
	}

	const int32 Brutes = GameMode->SpawnBrutes();
	const int32 Wardens = GameMode->SpawnAbyssalWardens();
	const int32 Hellhounds = GameMode->SpawnHellhounds();

	TestTrue(TEXT("The sandbox spawns a Brute"), Brutes > 0);
	TestTrue(TEXT("and an Abyssal Warden"), Wardens > 0);
	TestTrue(TEXT("and a Hellhound"), Hellhounds > 0);

	const FGameplayAttribute Armour =
		UCataclysmCombatAttributeSet::GetArmorAttribute();
	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();

	for (const TPair<FString, AActor*>& Creature : TArray<TPair<FString, AActor*>>{
			{TEXT("Brute"), Brutes > 0 ? GameMode->Brutes[0].Get() : nullptr},
			{TEXT("Abyssal Warden"),
			 Wardens > 0 ? GameMode->AbyssalWardens[0].Get() : nullptr},
			{TEXT("Hellhound"),
			 Hellhounds > 0 ? GameMode->Hellhounds[0].Get() : nullptr}})
	{
		if (!IsValid(Creature.Value))
		{
			AddError(FString::Printf(TEXT("No %s was spawned."), *Creature.Key));
			continue;
		}

		const UAbilitySystemComponent* AbilitySystem =
			UCataclysmTargeting::AbilitySystemOf(Creature.Value);
		if (!AbilitySystem)
		{
			AddError(FString::Printf(
				TEXT("The %s has no ability system."), *Creature.Key));
			continue;
		}

		const float ArmourValue = AbilitySystem->GetNumericAttribute(Armour);
		const float HealthValue = AbilitySystem->GetNumericAttribute(Health);

		TestTrue(FString::Printf(TEXT("The %s carries armour (%.0f)"),
			*Creature.Key, ArmourValue), ArmourValue > 0.0f);
		TestTrue(FString::Printf(TEXT("and health (%.0f)"), HealthValue),
			HealthValue > 0.0f);

		// SMALL ENOUGH TO BE KILLED, which is the whole of issue #525. An
		// ungeared character's Heavy slot deals about 102 before the creature's
		// own mitigation, and 11,000 health meant 116 uses of a 1.5 second
		// cooldown. The bound is loose on purpose: the exact figure is the
		// model's and is pinned in Python, and this only has to notice a return
		// to a number nobody can fight.
		TestTrue(FString::Printf(
			TEXT("and not so much of it that it cannot be killed (%.0f)"),
			HealthValue), HealthValue < 3000.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSandboxArmourReducesAHitTest,
	"Cataclysm.Sandbox.ASandboxEnemysArmourActuallyReducesAHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSandboxArmourReducesAHitTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSandboxTest;

	// THE ATTRIBUTE HOLDING A NUMBER IS NOT THE SAME AS THE NUMBER DOING
	// ANYTHING. That distinction is exactly what went wrong before issue #481 on
	// the simulation side: armour was computed, exported, checked for sanity, and
	// read by no arithmetic anywhere. This measures the same hit twice against
	// the same creature, once armoured and once not.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmGameMode* GameMode = World->SpawnActor<ACataclysmGameMode>();
	if (!GameMode || GameMode->SpawnBrutes() == 0)
	{
		AddError(TEXT("Could not spawn a Brute."));
		return false;
	}

	ACataclysmBruteCharacter* Brute = GameMode->Brutes[0].Get();
	ACataclysmEnemyCharacter* Attacker =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(200.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!IsValid(Brute) || !IsValid(Attacker))
	{
		AddError(TEXT("Could not arrange the fight."));
		return false;
	}
	Attacker->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));

	UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Brute);
	if (!AbilitySystem)
	{
		AddError(TEXT("The Brute has no ability system."));
		return false;
	}

	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute Armour =
		UCataclysmCombatAttributeSet::GetArmorAttribute();

	const float ArmourValue = AbilitySystem->GetNumericAttribute(Armour);
	if (!TestTrue(TEXT("The Brute starts armoured"), ArmourValue > 0.0f))
	{
		return false;
	}

	// THE SAME HIT, TWICE. Small against the creature's health so neither
	// application is clamped by the health remaining.
	constexpr float Blow = 50.0f;

	const float BeforeArmoured = AbilitySystem->GetNumericAttribute(Health);
	UCataclysmSkillEffects::ApplyDirectDamage(Attacker, Brute, Blow);
	const float WithArmour = BeforeArmoured
		- AbilitySystem->GetNumericAttribute(Health);

	Brute->SetArmour(0.0f);

	const float BeforeBare = AbilitySystem->GetNumericAttribute(Health);
	UCataclysmSkillEffects::ApplyDirectDamage(Attacker, Brute, Blow);
	const float WithoutArmour = BeforeBare
		- AbilitySystem->GetNumericAttribute(Health);

	TestTrue(FString::Printf(TEXT("An armoured Brute loses health (%.2f)"),
		WithArmour), WithArmour > 0.0f);
	TestTrue(FString::Printf(
		TEXT("and less of it than the same Brute with no armour "
			 "(%.2f against %.2f)"), WithArmour, WithoutArmour),
		WithArmour < WithoutArmour);

	return true;
}


/**
 * A spawned creature carries the rarity the game mode was told to give it.
 *
 * WHAT THIS GUARDS. Issue #721. Nothing anywhere called
 * `ACataclysmEnemyCharacter::SetRarityStep` outside the automation tests, so
 * every creature in a play session spawned Common however the game mode was
 * configured. Three systems read that field and all three sat at the bottom
 * rung: the drop rate, which is 0.16 items for a Common and exactly 5 for a
 * Boss; the magic find a rarer enemy adds to its own drops; and the design's
 * rule that a boss cannot be stunned at all.
 *
 * THE SETTINGS ARE ASKED FOR EXPLICITLY rather than taken from the defaults, and
 * that is the whole arrangement rather than a convenience. This test is about
 * whether the spawner writes what it was told, not about what the shipped config
 * happens to hold.
 *
 * IT USED TO ASSERT THE DEFAULTS AS WELL, AND THAT WAS WRONG. Issue #736. The
 * game mode is UCLASS(Config = Game), so its class default object is populated
 * from `game/Config/DefaultGame.ini` before any test runs, and this test failed
 * for anybody who had set a rung there to watch loot drop -- which was the only
 * way to see a rare creature before issue #508. The comment above those
 * assertions said "nothing in a test world reads an ini", which is not true and
 * is what made the failure read as a fault in the spawner.
 *
 * WHAT GUARDS THE SHIPPED DEFAULTS INSTEAD, and always did:
 * `tools/tests/test_the_game_modes_ini_keys_are_real_settings.py`. It reads the
 * ini and the header as text, it runs on every pull request where this does not,
 * and its message says which file to edit. One claim, one place.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpawnedRarityTest,
	"Cataclysm.Sandbox.ACreatureSpawnsAtTheRarityItWasConfiguredWith",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSpawnedRarityTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSandboxTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmGameMode* GameMode = World->SpawnActor<ACataclysmGameMode>();
	if (!GameMode)
	{
		AddError(TEXT("Could not spawn the game mode."));
		return false;
	}

	// NOTHING HERE READS A DEFAULT. See the note above the test: the defaults are
	// whatever game/Config/DefaultGame.ini holds, which is a file a person edits
	// while trying something out, and asserting on it here made this test fail
	// for a reason that had nothing to do with the spawner. Issue #736.
	//
	// A DIFFERENT RUNG FOR EACH, so a spawner that wrote the same value to all
	// three, or wrote one creature's setting onto another, fails here.
	constexpr int32 BossStep = ACataclysmEnemyCharacter::FirstBossRarityStep;
	constexpr int32 HeraldStep = 3;
	constexpr int32 EliteStep = 1;

	GameMode->BruteRarityStep = BossStep;
	GameMode->AbyssalWardenRarityStep = HeraldStep;
	GameMode->TrainingDummyRarityStep = EliteStep;
	GameMode->TrainingDummyCount = 2;

	GameMode->SpawnBrutes();
	GameMode->SpawnAbyssalWardens();
	GameMode->SpawnTrainingDummies();

	if (!TestTrue(TEXT("at least one Brute was spawned"),
				  GameMode->Brutes.Num() > 0))
	{
		return false;
	}
	for (const ACataclysmBruteCharacter* Brute : GameMode->Brutes)
	{
		if (!TestNotNull(TEXT("a spawned Brute"), Brute))
		{
			return false;
		}
		TestEqual(TEXT("the Brute spawned at the rarity asked for"),
			Brute->RarityStep, BossStep);

		// AND THE RARITY MEANS WHAT IT IS FOR. IsBoss is what the stun rule
		// reads, and it is the only consequence visible without killing anything.
		TestTrue(TEXT("and is therefore a boss, which cannot be stunned"),
			Brute->IsBoss());
	}

	if (!TestTrue(TEXT("at least one Abyssal Warden was spawned"),
				  GameMode->AbyssalWardens.Num() > 0))
	{
		return false;
	}
	for (const ACataclysmAbyssalWardenCharacter* Warden : GameMode->AbyssalWardens)
	{
		if (!TestNotNull(TEXT("a spawned Abyssal Warden"), Warden))
		{
			return false;
		}
		TestEqual(TEXT("the Warden spawned at its own rarity, not the Brute's"),
			Warden->RarityStep, HeraldStep);

		// HERALD IS DELIBERATELY BELOW THE BOSS LINE. The Abyssal Warden's
		// reference rarity is Herald and the design makes it a mini-boss the
		// player may stun, so this is the boundary rather than a spare case.
		TestFalse(TEXT("a Herald is not a boss and can be stunned"),
			Warden->IsBoss());
	}

	if (!TestTrue(TEXT("training dummies were spawned"),
				  GameMode->TrainingDummies.Num() > 0))
	{
		return false;
	}
	for (const ACataclysmEnemyCharacter* Dummy : GameMode->TrainingDummies)
	{
		if (!TestNotNull(TEXT("a spawned training dummy"), Dummy))
		{
			return false;
		}
		TestEqual(TEXT("the dummy spawned at its own rarity"),
			Dummy->RarityStep, EliteStep);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSandboxHellhoundHasRoomToChargeTest,
	"Cataclysm.Sandbox.TheHellhoundIsPlacedWithAClearLaneToChargeDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSandboxHellhoundHasRoomToChargeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSandboxTest;

	// WHAT THIS EXISTS FOR. All three spawners put a single creature at angle
	// zero, which is +X, so the Brute and the Abyssal Warden already stand on
	// one line with the sandbox floor's edge just beyond them. A Hellhound put
	// on that same line would have nowhere to charge that is not through another
	// creature, and the lane it leaves burning would set both of them alight
	// every five seconds -- which is correct behaviour and makes watching any of
	// the three much harder.
	//
	// IT IS A GEOMETRY CHECK ON REAL SPAWNED ACTORS rather than on the settings,
	// because the settings are already held to the model in
	// tools/tests/test_hellhound_matches_the_model.py and what can still go
	// wrong here is the arithmetic that turns a bearing into a position.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmGameMode* GameMode = World->SpawnActor<ACataclysmGameMode>();
	if (!GameMode)
	{
		AddError(TEXT("Could not spawn the game mode."));
		return false;
	}

	// NO PLAYER START IN THIS WORLD, so all three spawners fall back to the
	// world origin as their centre. That is what makes the distances below
	// readable: every position is measured from (0,0).
	const int32 Brutes = GameMode->SpawnBrutes();
	const int32 Wardens = GameMode->SpawnAbyssalWardens();
	const int32 Hellhounds = GameMode->SpawnHellhounds();

	if (Brutes <= 0 || Wardens <= 0 || Hellhounds <= 0)
	{
		AddError(FString::Printf(
			TEXT("The sandbox placed %d Brutes, %d Abyssal Wardens and %d "
				 "Hellhounds, and this test needs one of each."),
			Brutes, Wardens, Hellhounds));
		return false;
	}

	const ACataclysmHellhoundCharacter* Hellhound = GameMode->Hellhounds[0].Get();
	const ACataclysmBruteCharacter* Brute = GameMode->Brutes[0].Get();
	const ACataclysmAbyssalWardenCharacter* Warden =
		GameMode->AbyssalWardens[0].Get();
	if (!IsValid(Hellhound) || !IsValid(Brute) || !IsValid(Warden))
	{
		AddError(TEXT("One of the three creatures did not survive spawning."));
		return false;
	}

	const FVector Centre = FVector::ZeroVector;
	const FVector Where = Hellhound->GetActorLocation();

	// IT IS ON THE OPPOSITE SIDE OF THE PLAYER START FROM THE OTHER TWO, which
	// both stand at positive X. This is the assertion the whole bearing exists
	// for, and it is what a bearing accidentally reset to zero would fail.
	TestTrue(FString::Printf(
			TEXT("the Brute is at x=%.0f and the Abyssal Warden at x=%.0f, both "
				 "in front of the player start"),
			Brute->GetActorLocation().X, Warden->GetActorLocation().X),
		Brute->GetActorLocation().X > 0.0 && Warden->GetActorLocation().X > 0.0);

	TestTrue(FString::Printf(
			TEXT("and the Hellhound is at x=%.0f, on the other side of the "
				 "player start from both"),
			Where.X),
		Where.X < 0.0);

	// AND FAR ENOUGH FROM EACH THAT ITS WHOLE CHARGE LANE IS CLEAR. The lane
	// runs 10 metres from where it stands towards the player start, so the
	// nearest either of the others may be is that length plus the lane's own
	// half-width.
	const double ClearanceNeeded =
		ACataclysmHellhoundCharacter::HellrushRangeCm
		+ ACataclysmHellhoundCharacter::HellrushRadiusCm;

	for (const TPair<FString, const AActor*>& Other :
		 TArray<TPair<FString, const AActor*>>{
			{TEXT("Brute"), Brute}, {TEXT("Abyssal Warden"), Warden}})
	{
		const double Gap =
			FVector::Dist2D(Where, Other.Value->GetActorLocation());
		TestTrue(FString::Printf(
				TEXT("the %s is %.0f cm away, clear of the %.0f cm the "
					 "Hellhound's charge and its burning lane need"),
				*Other.Key, Gap, ClearanceNeeded),
			Gap > ClearanceNeeded);
	}

	// AND IT IS STILL ON THE FLOOR. The sandbox floor is 4000 cm across, so it
	// reaches 2000 cm from the player start in every direction, and a creature
	// placed beyond that has no navigation mesh under it and cannot path at all.
	constexpr double SandboxFloorReachCm = 2000.0;
	const double OutFromCentre = FVector::Dist2D(Where, Centre);
	const double HalfABody =
		ACataclysmHellhoundCharacter::HellhoundCapsuleRadius;

	TestTrue(FString::Printf(
			TEXT("it stands %.0f cm out and its body is %.0f cm wide, inside "
				 "the floor's %.0f cm reach"),
			OutFromCentre, HalfABody, SandboxFloorReachCm),
		OutFromCentre + HalfABody < SandboxFloorReachCm);

	// AND BEYOND ITS OWN NOTICE RADIUS, so it does not set off at a player who
	// has only just appeared. The player walks towards it and it starts when
	// they are 10 metres away, which is also the far end of its charge's range.
	TestTrue(FString::Printf(
			TEXT("it stands %.0f cm out, beyond the %.0f cm at which it notices "
				 "anybody"),
			OutFromCentre,
			ACataclysmHellhoundCharacter::HellhoundNoticeRadiusCm),
		OutFromCentre
			> ACataclysmHellhoundCharacter::HellhoundNoticeRadiusCm);

	// AND IT IS FACING THE PLAYER START rather than away from it, which is what
	// the other two spawners do and what makes the first thing it does visible.
	const FVector Forward = Hellhound->GetActorForwardVector();
	const FVector TowardsCentre = (Centre - Where).GetSafeNormal2D();
	TestTrue(TEXT("it is facing the player start"),
		FVector::DotProduct(Forward.GetSafeNormal2D(), TowardsCentre) > 0.9);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
