// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Player/CataclysmPlayerState.h"

/**
 * Health, mana and energy shield coming back over time. Issue #653.
 *
 * WHAT WENT WRONG. HealthRegen, ManaRegen and EnergyShieldRegen were declared on
 * UCataclysmVitalAttributeSet, initialised, clamped and replicated, and no code
 * anywhere read them. Every ability subtracts its mana cost and refuses to
 * activate without it, so mana only ever went down and a play session ended with
 * every ability permanently refused. The only thing that restored mana was
 * dying. It was reported from play as "sometimes all of my abilities just
 * become disabled".
 *
 * THE DESIGN SETTLES THE SHAPE AND THESE TESTS GUARD IT. A flat amount per
 * second for all three pools, and one delay that applies to the energy shield
 * alone: it "refills 3 seconds after the character last took damage", taking
 * damage again inside that window restarts the wait, and damage over time
 * restarts it as well. Health and mana have no delay, and nothing in the design
 * gives them one.
 *
 * WHAT IS NOT COVERED. A world built by UWorld::CreateWorld is never ticked, so
 * no timer in it ever fires. Nothing below waits for the repeating timer on
 * ACataclysmCharacterBase to come round; every test calls
 * UCataclysmRegeneration::ApplyStep directly, which is what that timer calls.
 * That the timer is started at all is checked by reading it back from the
 * timer manager rather than by watching it fire.
 */

namespace CataclysmRegenerationTest
{
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

	static ACataclysmEnemyCharacter* SpawnEnemy(UWorld* World, float Health)
	{
		ACataclysmEnemyCharacter* Spawned =
			World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
														FRotator::ZeroRotator);
		if (Spawned)
		{
			Spawned->SetGenericTeamId(
				UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Spawned->SetHealth(Health);
		}
		return Spawned;
	}

	static ACataclysmPlayerCharacter* SpawnPlayer(UWorld* World)
	{
		ACataclysmPlayerCharacter* Pawn =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector,
														 FRotator::ZeroRotator);
		ACataclysmPlayerState* State =
			World->SpawnActor<ACataclysmPlayerState>();
		if (Pawn && State)
		{
			Pawn->SetPlayerState(State);
			Pawn->OnRep_PlayerState();
		}
		return Pawn;
	}

	static UAbilitySystemComponent* SystemOf(AActor* Actor)
	{
		// Through the interface, so this answers for a player, whose ability
		// system lives on its player state, and for a creature, whose lives on
		// its pawn, without knowing which it is holding.
		return UCataclysmTargeting::AbilitySystemOf(Actor);
	}

	static float Read(AActor* Actor, const FGameplayAttribute& Attribute)
	{
		const UAbilitySystemComponent* System = SystemOf(Actor);
		return System ? System->GetNumericAttribute(Attribute) : -1.0f;
	}

	static void Write(AActor* Actor, const FGameplayAttribute& Attribute,
					  float Value)
	{
		if (UAbilitySystemComponent* System = SystemOf(Actor))
		{
			System->SetNumericAttributeBase(Attribute, Value);
		}
	}

	/** Long enough that the energy shield's three second wait has passed. */
	static constexpr float LongSinceHurt = 60.0f;
}

// --------------------------------------------------------------------------
// The arithmetic, with no world at all
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRegenerationIsPerSecond,
	"Cataclysm.Regeneration.ARateIsAnAmountPerSecondAndAStepIsAShareOfIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRegenerationIsPerSecond::RunTest(const FString&)
{
	// THE DESIGN'S OWN SHAPE. "The base regeneration rate is a small flat value
	// per second, supplied the same way base health is. This applies to health,
	// mana and energy shield regeneration alike." Not a percentage of the
	// maximum: the same passage says reading it that way would have 50 points
	// of Vitality returning half a character's health every second.
	TestEqual(TEXT("a whole second of a rate of 4 is 4"),
		UCataclysmRegeneration::GainPerStep(4.0f, 1.0f), 4.0f);
	TestEqual(TEXT("a quarter second of a rate of 4 is 1"),
		UCataclysmRegeneration::GainPerStep(4.0f, 0.25f), 1.0f);
	TestEqual(TEXT("two seconds of a rate of 4 is 8"),
		UCataclysmRegeneration::GainPerStep(4.0f, 2.0f), 8.0f);

	// A rate of zero is a real design position -- an enemy has no regeneration
	// at all -- and a negative rate is not a drain, because nothing in the
	// design states one.
	TestEqual(TEXT("no rate gains nothing"),
		UCataclysmRegeneration::GainPerStep(0.0f, 1.0f), 0.0f);
	TestEqual(TEXT("a negative rate is not a drain"),
		UCataclysmRegeneration::GainPerStep(-5.0f, 1.0f), 0.0f);
	TestEqual(TEXT("no time gains nothing"),
		UCataclysmRegeneration::GainPerStep(4.0f, 0.0f), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRegenerationShieldWaitsThreeSeconds,
	"Cataclysm.Regeneration.AnEnergyShieldWaitsThreeSecondsAfterBeingHurt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRegenerationShieldWaitsThreeSeconds::RunTest(const FString&)
{
	// THE NUMBER IS THE DESIGN'S. The Energy Shield section of
	// docs/Cataclysm_GDD_v2.md: "It refills 3 seconds after the character last
	// took damage. Taking damage again inside that window restarts the wait."
	TestEqual(TEXT("the wait is the design's three seconds"),
		UCataclysmRegeneration::ShieldRefillDelaySeconds, 3.0f);

	TestFalse(TEXT("hurt this instant, the shield does not refill"),
		UCataclysmRegeneration::ShieldMayRefill(0.0f));
	TestFalse(TEXT("nor a second later"),
		UCataclysmRegeneration::ShieldMayRefill(1.0f));
	TestFalse(TEXT("nor just short of three seconds"),
		UCataclysmRegeneration::ShieldMayRefill(2.99f));
	TestTrue(TEXT("at three seconds it may"),
		UCataclysmRegeneration::ShieldMayRefill(3.0f));
	TestTrue(TEXT("and long after, it may"),
		UCataclysmRegeneration::ShieldMayRefill(60.0f));

	return true;
}

// --------------------------------------------------------------------------
// What one step does to a character
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRegenerationRefillsSpentMana,
	"Cataclysm.Regeneration.SpentManaComesBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRegenerationRefillsSpentMana::RunTest(const FString&)
{
	// THIS IS THE REPORTED BUG. Before this, the only thing in the whole
	// project that added mana was ACataclysmPlayerCharacter::Revive, so a player
	// who spent their pool and did not die could never cast again.
	UWorld* World = CataclysmRegenerationTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player =
		CataclysmRegenerationTest::SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}

	const FGameplayAttribute Mana =
		UCataclysmVitalAttributeSet::GetManaAttribute();
	const FGameplayAttribute Rate =
		UCataclysmVitalAttributeSet::GetManaRegenAttribute();

	CataclysmRegenerationTest::Write(Player, Mana, 0.0f);
	CataclysmRegenerationTest::Write(Player, Rate, 4.0f);

	TestEqual(TEXT("the pool starts empty"),
		CataclysmRegenerationTest::Read(Player, Mana), 0.0f);

	UCataclysmRegeneration::ApplyStep(Player, /*SecondsInStep=*/1.0f,
									  CataclysmRegenerationTest::LongSinceHurt);

	TestEqual(TEXT("one second at a rate of 4 returns 4"),
		CataclysmRegenerationTest::Read(Player, Mana), 4.0f);

	UCataclysmRegeneration::ApplyStep(Player, 1.0f,
									  CataclysmRegenerationTest::LongSinceHurt);

	TestEqual(TEXT("and it keeps coming back"),
		CataclysmRegenerationTest::Read(Player, Mana), 8.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRegenerationStopsAtTheMaximum,
	"Cataclysm.Regeneration.APoolStopsAtItsMaximumRatherThanOverflowing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRegenerationStopsAtTheMaximum::RunTest(const FString&)
{
	UWorld* World = CataclysmRegenerationTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player =
		CataclysmRegenerationTest::SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}

	const FGameplayAttribute Mana =
		UCataclysmVitalAttributeSet::GetManaAttribute();
	const float Maximum = CataclysmRegenerationTest::Read(
		Player, UCataclysmVitalAttributeSet::GetMaxManaAttribute());

	CataclysmRegenerationTest::Write(Player, Mana, Maximum - 1.0f);
	CataclysmRegenerationTest::Write(
		Player, UCataclysmVitalAttributeSet::GetManaRegenAttribute(), 1000.0f);

	// A HUGE RATE AGAINST ONE POINT OF ROOM. The clamp in PreAttributeChange
	// would catch this anyway; what is checked here is that the step itself
	// stops, so it does not write the maximum over itself every quarter second
	// and fire an attribute change for a value that did not change.
	UCataclysmRegeneration::ApplyStep(Player, 1.0f,
									  CataclysmRegenerationTest::LongSinceHurt);

	TestEqual(TEXT("it fills to the maximum and no further"),
		CataclysmRegenerationTest::Read(Player, Mana), Maximum);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRegenerationShieldObeysTheDelay,
	"Cataclysm.Regeneration.AnEnergyShieldDoesNotRefillWhileTheWaitIsRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRegenerationShieldObeysTheDelay::RunTest(const FString&)
{
	UWorld* World = CataclysmRegenerationTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player =
		CataclysmRegenerationTest::SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}

	// The player has no energy shield by default, so give it one to have
	// something to refill.
	CataclysmRegenerationTest::Write(
		Player, UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(),
		100.0f);
	const FGameplayAttribute Shield =
		UCataclysmVitalAttributeSet::GetEnergyShieldAttribute();
	CataclysmRegenerationTest::Write(Player, Shield, 0.0f);
	CataclysmRegenerationTest::Write(
		Player, UCataclysmVitalAttributeSet::GetEnergyShieldRegenAttribute(),
		10.0f);

	// Hurt one second ago. The design gives the shield three.
	UCataclysmRegeneration::ApplyStep(Player, 1.0f,
									  /*SecondsSinceLastDamage=*/1.0f);
	TestEqual(TEXT("one second after being hurt, nothing comes back"),
		CataclysmRegenerationTest::Read(Player, Shield), 0.0f);

	UCataclysmRegeneration::ApplyStep(Player, 1.0f, 2.99f);
	TestEqual(TEXT("nor just short of the three"),
		CataclysmRegenerationTest::Read(Player, Shield), 0.0f);

	UCataclysmRegeneration::ApplyStep(Player, 1.0f, 3.0f);
	TestEqual(TEXT("at three seconds it begins"),
		CataclysmRegenerationTest::Read(Player, Shield), 10.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRegenerationHealthIgnoresTheDelay,
	"Cataclysm.Regeneration.HealthAndManaComeBackWithoutWaiting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRegenerationHealthIgnoresTheDelay::RunTest(const FString&)
{
	// THE DELAY IS THE SHIELD'S ALONE. Nothing in the design gives health or
	// mana one, and the enchantment that proves the shield has a delay --
	// "regeneration begins immediately after taking damage with no delay" --
	// names only the shield. A player who could not regain mana during a fight
	// would be back in the reported bug for as long as the fight lasted.
	UWorld* World = CataclysmRegenerationTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player =
		CataclysmRegenerationTest::SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}

	const FGameplayAttribute Mana =
		UCataclysmVitalAttributeSet::GetManaAttribute();
	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();

	CataclysmRegenerationTest::Write(Player, Mana, 0.0f);
	CataclysmRegenerationTest::Write(
		Player, UCataclysmVitalAttributeSet::GetManaRegenAttribute(), 4.0f);
	CataclysmRegenerationTest::Write(Player, Health, 1.0f);
	CataclysmRegenerationTest::Write(
		Player, UCataclysmVitalAttributeSet::GetHealthRegenAttribute(), 4.0f);

	// Hurt this very instant, which stops the shield and must stop neither of
	// these.
	UCataclysmRegeneration::ApplyStep(Player, 1.0f,
									  /*SecondsSinceLastDamage=*/0.0f);

	TestEqual(TEXT("mana comes back while being hit"),
		CataclysmRegenerationTest::Read(Player, Mana), 4.0f);
	TestEqual(TEXT("and so does health"),
		CataclysmRegenerationTest::Read(Player, Health), 5.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRegenerationSkipsTheDead,
	"Cataclysm.Regeneration.NothingComesBackToSomethingAlreadyDead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRegenerationSkipsTheDead::RunTest(const FString&)
{
	// AN ENEMY IS DESTROYED ON THE TICK AFTER IT DIES, because HandleDeath runs
	// inside the gameplay effect callback that dealt the killing blow. So there
	// is a real window in which a dead creature is still standing there with an
	// ability system, and without this check a step landing in that window would
	// pull it back off zero health and undo its own death.
	UWorld* World = CataclysmRegenerationTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player =
		CataclysmRegenerationTest::SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}

	// A PLAYER RATHER THAN A CREATURE, because a creature has no regeneration
	// at all and would sit still for the wrong reason. This one has a rate and
	// is refused only because it is dead.
	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();
	CataclysmRegenerationTest::Write(Player, Health, 0.0f);
	CataclysmRegenerationTest::Write(
		Player, UCataclysmVitalAttributeSet::GetHealthRegenAttribute(), 50.0f);

	// Alive at zero health, it would come back, which is what makes the check
	// below about being dead rather than about being at zero.
	UCataclysmRegeneration::ApplyStep(Player, 1.0f,
									  CataclysmRegenerationTest::LongSinceHurt);
	if (!TestEqual(TEXT("alive at zero health, it does come back"),
			CataclysmRegenerationTest::Read(Player, Health), 50.0f))
	{
		return false;
	}

	CataclysmRegenerationTest::Write(Player, Health, 0.0f);
	UCataclysmSkillEffects::MarkDead(Player);

	UCataclysmRegeneration::ApplyStep(Player, 1.0f,
									  CataclysmRegenerationTest::LongSinceHurt);

	TestEqual(TEXT("once dead, nothing comes back"),
		CataclysmRegenerationTest::Read(Player, Health), 0.0f);

	return true;
}

// --------------------------------------------------------------------------
// The clock that drives it
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRegenerationClockIsRunning,
	"Cataclysm.Regeneration.TheClockThatDrivesItStartsWhenACharacterDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRegenerationClockIsRunning::RunTest(const FString&)
{
	// WITHOUT THIS, EVERY OTHER TEST IN THIS FILE COULD PASS WHILE NOTHING
	// REGENERATED IN THE RUNNING GAME. All of them call ApplyStep directly,
	// which is what the timer calls; none of them would notice if the timer were
	// never started. That is exactly the shape of the bug this feature fixes --
	// an attribute that exists, is initialised, and that nothing ever reads --
	// so it gets its own guard.
	//
	// WHAT IT CANNOT DO IS WATCH THE TIMER FIRE. A world built by
	// UWorld::CreateWorld is never ticked.
	UWorld* World = CataclysmRegenerationTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player =
		CataclysmRegenerationTest::SpawnPlayer(World);
	ACataclysmEnemyCharacter* Enemy =
		CataclysmRegenerationTest::SpawnEnemy(World, 549.0f);
	if (!TestNotNull(TEXT("a player pawn"), Player)
		|| !TestNotNull(TEXT("an enemy"), Enemy))
	{
		return false;
	}

	// BEGINPLAY HAS TO BE DISPATCHED BY HAND, AND THAT IS A PROPERTY OF THE
	// HARNESS RATHER THAN OF THIS FEATURE. The helper every test file in this
	// project copies is called MakeWorldThatHasBegunPlay, and actors spawned in
	// its world never receive BeginPlay: UWorld::BeginPlay only does anything
	// when the world has a game mode, and a world built by UWorld::CreateWorld
	// has none, so the world's bBegunPlay is never set and nothing dispatches to
	// an actor. The name says otherwise. Issue #654.
	//
	// Everything else in this file calls UCataclysmRegeneration::ApplyStep
	// directly and does not care. This test is the one that does.
	Player->DispatchBeginPlay();
	Enemy->DispatchBeginPlay();

	TestTrue(TEXT("the player's regeneration clock is running"),
		Player->IsRegenerating());

	// A creature's rates are all zero, so its clock does nothing -- but it must
	// still be running, because a modifier or an archetype property could give
	// a creature a rate later and nothing would start the clock for it.
	TestTrue(TEXT("and so is a creature's, even though its rates are zero"),
		Enemy->IsRegenerating());

	return true;
}

// --------------------------------------------------------------------------
// A creature has no regeneration, and that is a stated position
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRegenerationEnemiesDoNotRegenerate,
	"Cataclysm.Regeneration.ACreatureDoesNotRegenerateAtAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRegenerationEnemiesDoNotRegenerate::RunTest(const FString&)
{
	// THE DESIGN GIVES REGENERATION TO CLASSES, NOT TO CREATURES. Each of the
	// three Demonic class stat lines states a health and a mana regeneration
	// figure; EnemyArchetypes.csv has no column for either. Without this the
	// attribute set's own placeholder of 1.0 would have handed every creature a
	// heal nobody designed, and a Brute would recover while the player backs
	// away from it.
	UWorld* World = CataclysmRegenerationTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Enemy =
		CataclysmRegenerationTest::SpawnEnemy(World, 549.0f);
	if (!TestNotNull(TEXT("an enemy"), Enemy))
	{
		return false;
	}

	TestEqual(TEXT("a creature has no health regeneration"),
		CataclysmRegenerationTest::Read(
			Enemy, UCataclysmVitalAttributeSet::GetHealthRegenAttribute()),
		0.0f);
	TestEqual(TEXT("nor any mana regeneration"),
		CataclysmRegenerationTest::Read(
			Enemy, UCataclysmVitalAttributeSet::GetManaRegenAttribute()),
		0.0f);
	TestEqual(TEXT("nor any energy shield regeneration"),
		CataclysmRegenerationTest::Read(
			Enemy, UCataclysmVitalAttributeSet::GetEnergyShieldRegenAttribute()),
		0.0f);

	// And a step really does nothing to it, rather than only the rate being
	// zero somewhere it might not be read.
	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();
	CataclysmRegenerationTest::Write(Enemy, Health, 100.0f);
	UCataclysmRegeneration::ApplyStep(Enemy, 10.0f,
									  CataclysmRegenerationTest::LongSinceHurt);

	TestEqual(TEXT("ten seconds later it has healed nothing"),
		CataclysmRegenerationTest::Read(Enemy, Health), 100.0f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
