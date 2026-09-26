// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmFear.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Fear, and fleeing without it. Ruled 2026-09-25.
 *
 * A CREATURE'S FLIGHT IS READ FROM ITS BRAIN, as `Cataclysm.AI.*` reads a chase:
 * `Think` says what the creature is doing and `LastFleeGoal` where it aimed,
 * because a world built for a test never ticks movement.
 *
 * THE PLAYER'S HALF IS READ FROM WHAT THE CONTROLLER ASKS, not from the
 * controller. No automation test has a player controller driving input -- see
 * `ACataclysmPlayerController::PawnCannotWalk` -- so these tests check the fear
 * and the direction the controller walks the player along, and the controller's
 * use of them is judged in play.
 */
namespace CataclysmFearTest
{
	using Fear = UCataclysmFear;
	constexpr float M = 100.0f;

	/** A creature of a team, healthy and harmless unless told otherwise. */
	ACataclysmEnemyCharacter* Spawn(UWorld* World, const FVector& Where, ECataclysmTeam Team)
	{
		ACataclysmEnemyCharacter* Made =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(Team));
			Made->SetHealth(1'000.0f);
			Made->SetAttackDamage(0.0f);
		}
		return Made;
	}

	ACataclysmEnemyController* BrainOf(const ACataclysmEnemyCharacter* Creature)
	{
		return Creature ? Cast<ACataclysmEnemyController>(Creature->GetController()) : nullptr;
	}

	int32 Did(ACataclysmEnemyController* Brain)
	{
		return static_cast<int32>(Brain->Think());
	}

	constexpr int32 Fleeing = static_cast<int32>(ECataclysmBrainAction::Fleeing);
	constexpr int32 Attacking = static_cast<int32>(ECataclysmBrainAction::Attacking);
}

#define CATACLYSM_FEAR_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_FEAR_TEST(FCataclysmFearedCreatureFleesTest,
	"Cataclysm.Fear.AFearedCreatureMovesAwayFromTheSourceAndDoesNotAttack")
{
	using namespace CataclysmFearTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// A PLAYER IN REACH, one metre off, so an unfeared creature attacks.
	ACataclysmEnemyCharacter* Creature = Spawn(World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Player = Spawn(World, FVector(1 * M, 0, 0), ECataclysmTeam::Players);
	ACataclysmEnemyController* Brain = BrainOf(Creature);
	if (!TestNotNull(TEXT("a creature with a brain"), Brain) || !TestNotNull(TEXT("a player"), Player))
	{
		return false;
	}
	if (!TestEqual(TEXT("set-up: unfeared, it attacks the player in its reach"), Did(Brain),
				   Attacking))
	{
		return false;
	}

	const FVector Source = Player->GetActorLocation();
	if (!TestTrue(TEXT("the fear lands"), Fear::ApplyFear(Player, Creature, 2.0f, Source)))
	{
		AddError(TEXT("If State.Feared is missing, regenerate the gameplay tags from the "
					  "workbook: python tools/generate_gameplay_tags.py"));
		return false;
	}
	TestTrue(TEXT("it is feared"), Fear::IsFeared(Creature));
	TestTrue(TEXT("and that is crowd control"), UCataclysmSkillEffects::IsCrowdControlled(Creature));
	TestEqual(TEXT("feared, with the player still in reach, it flees and does not attack"),
			  Did(Brain), Fleeing);

	const FVector Here = Creature->GetActorLocation();
	const float Before = FVector::Dist2D(Here, Source);
	const float Aimed = FVector::Dist2D(Brain->LastFleeGoal, Source);
	TestEqual(TEXT("it aims six metres further from the source"), Aimed - Before, 6.0f * M, 1.0f);
	TestTrue(TEXT("on the side away from it"),
			 FVector::DotProduct(Brain->LastFleeGoal - Here, Here - Source) > 0.0f);
	return true;
}

CATACLYSM_FEAR_TEST(FCataclysmFearRefusedTest,
	"Cataclysm.Fear.ABossIsNotFearedAndTheWindowRefusesASecondFearOrAStun")
{
	using namespace CataclysmFearTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Source = Spawn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* Boss = Spawn(World, FVector(3 * M, 0, 0), ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Plain = Spawn(World, FVector(0, 3 * M, 0), ECataclysmTeam::Monsters);
	if (!TestNotNull(TEXT("a boss"), Boss) || !TestNotNull(TEXT("a creature"), Plain))
	{
		return false;
	}
	Boss->SetRarityStep(4);
	if (!TestTrue(TEXT("set-up: the Boss rung is a boss"), Boss->IsBoss()))
	{
		return false;
	}
	TestFalse(TEXT("a boss cannot be feared"),
			  Fear::ApplyFear(Source, Boss, 2.0f, Source->GetActorLocation()));
	TestFalse(TEXT("and is not"), Fear::IsFeared(Boss));

	if (!TestTrue(TEXT("a first fear lands"),
				  Fear::ApplyFear(Source, Plain, 2.0f, Source->GetActorLocation())))
	{
		return false;
	}
	CataclysmTestWorld::RunClock(World, 2.5f);
	if (!TestFalse(TEXT("set-up: two seconds on, it has run out"), Fear::IsFeared(Plain)))
	{
		return false;
	}
	TestFalse(TEXT("inside the five-second window a second fear is refused"),
			  Fear::ApplyFear(Source, Plain, 2.0f, Source->GetActorLocation()));
	TestFalse(TEXT("and so is a stun, because the window is shared"),
			  UCataclysmSkillEffects::ApplyStun(Source, Plain, 1.0f, 0.0f, /*bStunIsDesigned=*/true));
	CataclysmTestWorld::RunClock(World, 3.0f);
	TestTrue(TEXT("once the window has passed, fear lands again"),
			 Fear::ApplyFear(Source, Plain, 2.0f, Source->GetActorLocation()));
	return true;
}

CATACLYSM_FEAR_TEST(FCataclysmFearResistanceTest,
	"Cataclysm.Fear.CrowdControlResistanceShortensAFearInProportion")
{
	using namespace CataclysmFearTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Source = Spawn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* Plain = Spawn(World, FVector(3 * M, 0, 0), ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Half = Spawn(World, FVector(0, 3 * M, 0), ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Whole = Spawn(World, FVector(0, -3 * M, 0), ECataclysmTeam::Monsters);
	UAbilitySystemComponent* HalfSystem = UCataclysmTargeting::AbilitySystemOf(Half);
	UAbilitySystemComponent* WholeSystem = UCataclysmTargeting::AbilitySystemOf(Whole);
	if (!TestNotNull(TEXT("an ability system"), HalfSystem)
		|| !TestNotNull(TEXT("and another"), WholeSystem))
	{
		return false;
	}
	HalfSystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetCrowdControlResistanceAttribute(), 50.0f);
	WholeSystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetCrowdControlResistanceAttribute(), 100.0f);

	const FVector From = Source->GetActorLocation();
	TestTrue(TEXT("a plain creature is feared"), Fear::ApplyFear(Source, Plain, 2.0f, From));
	TestTrue(TEXT("so is one at 50% resistance"), Fear::ApplyFear(Source, Half, 2.0f, From));
	TestFalse(TEXT("and one at 100% is not"), Fear::ApplyFear(Source, Whole, 2.0f, From));

	CataclysmTestWorld::RunClock(World, 1.5f);
	TestTrue(TEXT("a second and a half on, the plain two-second fear still holds"),
			 Fear::IsFeared(Plain));
	TestFalse(TEXT("and the halved one, one second long, has run out"), Fear::IsFeared(Half));
	return true;
}

CATACLYSM_FEAR_TEST(FCataclysmFearedPlayerTest,
	"Cataclysm.Fear.AFearedPlayerCannotAttackAndIsWalkedAwayFromTheSource")
{
	using namespace CataclysmFearTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Player = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Source =
		Spawn(World, FVector(-2 * M, 0, 0), ECataclysmTeam::Monsters);
	if (!TestNotNull(TEXT("a player state"), State) || !TestNotNull(TEXT("a controller"), Controller)
		|| !TestNotNull(TEXT("a player character"), Player) || !TestNotNull(TEXT("a source"), Source))
	{
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Player);

	TestTrue(TEXT("set-up: unfeared, the player may swing"), UCataclysmBasicAttack::MaySwing(Player));
	TestTrue(TEXT("and is walked nowhere"), Fear::FleeDirectionFor(Player).IsNearlyZero());

	if (!TestTrue(TEXT("Funereal Procession's two-second fear lands on the player"),
				  Fear::ApplyFear(Source, Player, 2.0f, Source->GetActorLocation())))
	{
		return false;
	}
	TestFalse(TEXT("a feared player cannot swing"), UCataclysmBasicAttack::MaySwing(Player));
	const FVector Away = Fear::FleeDirectionFor(Player);
	TestTrue(FString::Printf(TEXT("and is walked away from the source, along +X: (%.2f, %.2f)"),
							 Away.X, Away.Y),
			 Away.X > 0.99f);
	return true;
}

CATACLYSM_FEAR_TEST(FCataclysmFearHeldTest,
	"Cataclysm.Fear.NowhereToRunHoldsAFearedCreatureStill")
{
	using namespace CataclysmFearTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Holder = Spawn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* Creature = Spawn(World, FVector(3 * M, 0, 0), ECataclysmTeam::Monsters);
	ACataclysmEnemyController* Brain = BrainOf(Creature);
	if (!TestNotNull(TEXT("a creature with a brain"), Brain))
	{
		return false;
	}
	Creature->NoteHeldBy(Holder, World->GetTimeSeconds() + 10.0f, 8.0f * M);
	if (!TestTrue(TEXT("set-up: the creature is held"), Creature->IsHeld())
		|| !TestTrue(TEXT("and feared"),
					 Fear::ApplyFear(Holder, Creature, 2.0f, Holder->GetActorLocation())))
	{
		return false;
	}
	TestEqual(TEXT("it still reports fleeing"), Did(Brain), Fleeing);
	TestTrue(TEXT("but aims nowhere: the hold wins and it stands still"),
			 Brain->LastFleeGoal.IsZero());
	return true;
}

CATACLYSM_FEAR_TEST(FCataclysmFearDirgeTest,
	"Cataclysm.Fear.TheDirgeCrescendoMakesEveryCreatureImmuneToFear")
{
	using namespace CataclysmFearTest;
	using Effects = UCataclysmDungeonModifierEffects;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode))
	{
		return false;
	}
	Mode->DungeonModifiers = {FName(Effects::DirgeResonanceKey)};
	Mode->FloorNumber = 1;
	if (!TestNotNull(TEXT("a floor carrying the row was built"), Mode->BuildFloor()))
	{
		return false;
	}
	Mode->ClearFloorEnemies();

	ACataclysmEnemyCharacter* Source = Spawn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* Early = Spawn(World, FVector(4 * M, 0, 0), ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Late = Spawn(World, FVector(8 * M, 0, 0), ECataclysmTeam::Monsters);
	if (!TestNotNull(TEXT("a creature"), Early) || !TestNotNull(TEXT("another"), Late))
	{
		return false;
	}
	Mode->FloorEnemies.Add(Early);
	Mode->FloorEnemies.Add(Late);

	TestTrue(TEXT("before the crescendo a creature can be feared"),
			 Fear::ApplyFear(Source, Early, 2.0f, Source->GetActorLocation()));

	const int32 Beats = FMath::CeilToInt(Effects::DirgeResonanceEverySeconds
										 / ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	for (int32 Beat = 0; Beat < Beats; ++Beat)
	{
		Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	}
	if (!TestTrue(TEXT("set-up: the crescendo has come"),
				  UCataclysmSkillEffects::HasTag(Late, UCataclysmFear::FearImmuneTag())))
	{
		return false;
	}
	TestFalse(TEXT("after it, a creature the dirge reached cannot be feared"),
			  Fear::ApplyFear(Source, Late, 2.0f, Source->GetActorLocation()));
	return true;
}

CATACLYSM_FEAR_TEST(FCataclysmFleeFromTest,
	"Cataclysm.Fear.FleeFromMovesACreatureAwayWithoutFearingIt")
{
	using namespace CataclysmFearTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Creature = Spawn(World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Player = Spawn(World, FVector(1 * M, 0, 0), ECataclysmTeam::Players);
	ACataclysmEnemyController* Brain = BrainOf(Creature);
	if (!TestNotNull(TEXT("a creature with a brain"), Brain) || !TestNotNull(TEXT("a player"), Player))
	{
		return false;
	}

	Creature->FleeFrom(Player->GetActorLocation(), World->GetTimeSeconds() + 2.0f);
	TestEqual(TEXT("told to flee, it flees though the player is in reach"), Did(Brain), Fleeing);
	TestFalse(TEXT("and it is not feared"), Fear::IsFeared(Creature));
	TestTrue(TEXT("it aims away from the point"),
			 FVector::DotProduct(Brain->LastFleeGoal - Creature->GetActorLocation(),
								 Creature->GetActorLocation() - Player->GetActorLocation())
				 > 0.0f);

	CataclysmTestWorld::RunClock(World, 2.5f);
	TestEqual(TEXT("once its time is up it fights again"), Did(Brain), Attacking);
	return true;
}

// ---------------------------------------------------------------------------
// Madness, under the same two rules. The design document's anti-stun-lock table
// gives Madness the 5 second window and boss immunity; neither was built until
// the fear change, and these two tests were run on the code before it to measure
// that.
// ---------------------------------------------------------------------------

CATACLYSM_FEAR_TEST(FCataclysmMadnessBossTest,
	"Cataclysm.Fear.MadnessIsRefusedOnABossAsFearIs")
{
	using namespace CataclysmFearTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Source = Spawn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* Plain = Spawn(World, FVector(3 * M, 0, 0), ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Boss = Spawn(World, FVector(0, 3 * M, 0), ECataclysmTeam::Monsters);
	if (!TestNotNull(TEXT("a creature"), Plain) || !TestNotNull(TEXT("a boss"), Boss))
	{
		return false;
	}
	Boss->SetRarityStep(4);
	if (!TestTrue(TEXT("set-up: the Boss rung is a boss"), Boss->IsBoss())
		|| !TestTrue(TEXT("set-up: a creature that is not a boss is maddened"),
					 UCataclysmSkillEffects::ApplyNamedEffect(Source, Plain,
															  UCataclysmTeams::MadnessTag(), 3.0f)
						 && UCataclysmTeams::IsMaddened(Plain)))
	{
		return false;
	}
	TestFalse(TEXT("madness on a boss is refused"),
			  UCataclysmSkillEffects::ApplyNamedEffect(Source, Boss, UCataclysmTeams::MadnessTag(),
													   3.0f));
	TestFalse(TEXT("and the boss is not maddened"), UCataclysmTeams::IsMaddened(Boss));
	return true;
}

CATACLYSM_FEAR_TEST(FCataclysmMadnessWindowTest,
	"Cataclysm.Fear.MadnessIsRefusedInsideTheWindowAfterAStun")
{
	using namespace CataclysmFearTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Source = Spawn(World, FVector::ZeroVector, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* Stunned = Spawn(World, FVector(3 * M, 0, 0), ECataclysmTeam::Monsters);
	if (!TestNotNull(TEXT("a creature"), Stunned)
		|| !TestTrue(TEXT("set-up: a one-second stun lands"),
					 UCataclysmSkillEffects::ApplyStun(Source, Stunned, 1.0f, 0.0f,
													   /*bStunIsDesigned=*/true)))
	{
		return false;
	}
	CataclysmTestWorld::RunClock(World, 1.5f);
	if (!TestFalse(TEXT("set-up: the stun has run out"), UCataclysmSkillEffects::IsStunned(Stunned)))
	{
		return false;
	}
	TestFalse(TEXT("inside the five-second window madness is refused"),
			  UCataclysmSkillEffects::ApplyNamedEffect(Source, Stunned, UCataclysmTeams::MadnessTag(),
													   3.0f));
	TestFalse(TEXT("and the creature is not maddened"), UCataclysmTeams::IsMaddened(Stunned));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
