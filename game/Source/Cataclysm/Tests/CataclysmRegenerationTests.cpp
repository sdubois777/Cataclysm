// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmRegeneration.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
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
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRegenerationWornShieldComesBack,
	"Cataclysm.Regeneration.AWornEnergyShieldRefillsInFiveSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRegenerationWornShieldComesBack::RunTest(const FString& Parameters)
{
	using namespace CataclysmRegenerationTest;

	/**
	 * THE TEST THE OTHER ONE COULD NOT BE. The shield delay test above writes
	 * both the maximum energy shield and the rate by hand, saying so: "The
	 * player has no energy shield by default, so give it one to have something
	 * to refill." That proves the three second wait and cannot notice that no
	 * character in the game had both a shield and a rate.
	 *
	 * They did not. Only the Ritualist had any energy shield regeneration, the
	 * only gear source was an INCREASED affix which multiplies zero, and no item
	 * base granted it. A Ravager wearing a Vestment held 120 maximum energy
	 * shield and a refill rate of nothing, so the shield never came back at all.
	 * Issue #1237.
	 *
	 * SO THIS WEARS A REAL ITEM AND WRITES NOTHING. Every number below is read
	 * off the character after the equipment has been applied.
	 */
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}

	UCataclysmEquipmentComponent* Equipment = Player->GetEquipment();
	UAbilitySystemComponent* AbilitySystem = SystemOf(Player);
	if (!TestNotNull(TEXT("an equipment component"), Equipment)
		|| !TestNotNull(TEXT("an ability system"), AbilitySystem))
	{
		return false;
	}

	// A Vestment is the chest base whose implicit is an energy shield. The
	// player's own class supplies none, which is the case that was broken.
	FCataclysmItem Vestment;
	Vestment.Base = FName(TEXT("Chest_Vestment"));

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	Equipment->Equip(Vestment, Removed, AlsoRemoved, Went);
	Equipment->RefreshAttributes(AbilitySystem);

	const float Maximum =
		Read(Player, UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute());
	const float Rate =
		Read(Player, UCataclysmVitalAttributeSet::GetEnergyShieldRegenAttribute());

	if (!TestTrue(FString::Printf(
			TEXT("wearing a Vestment gives a shield, got %.1f"), Maximum),
		Maximum > 0.0f))
	{
		return false;
	}
	if (!TestTrue(FString::Printf(
			TEXT("and a rate to refill it, got %.1f a second"), Rate),
		Rate > 0.0f))
	{
		return false;
	}

	// FIVE SECONDS, AND THE RULE IS THAT IT DOES NOT DEPEND ON THE SIZE. Every
	// source of maximum energy shield grants a fifth of what it gave, so this
	// holds whatever the character is wearing and whichever class it is.
	// tools/tests/test_an_energy_shield_refills_in_five_seconds.py holds the
	// data side of the same rule.
	TestTrue(FString::Printf(
			TEXT("%.1f shield at %.1f a second refills in %.1f seconds"),
			Maximum, Rate, Maximum / Rate),
		FMath::IsNearlyEqual(Maximum / Rate, 5.0f, 0.01f));

	// AND IT ACTUALLY COMES BACK, walked one step at a time rather than
	// asserted from the two numbers above.
	Write(Player, UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);

	const float Step = UCataclysmRegeneration::StepSeconds;
	const int32 StepsInFiveSeconds = FMath::RoundToInt(5.0f / Step);
	for (int32 Taken = 0; Taken < StepsInFiveSeconds; ++Taken)
	{
		UCataclysmRegeneration::ApplyStep(Player, Step, LongSinceHurt);
	}

	const float Filled =
		Read(Player, UCataclysmVitalAttributeSet::GetEnergyShieldAttribute());
	TestTrue(FString::Printf(
			TEXT("five seconds of steps fill the shield: %.1f of %.1f"),
			Filled, Maximum),
		FMath::IsNearlyEqual(Filled, Maximum, 0.5f));

	// HALF THE STEPS FILL HALF OF IT, so the figure above is a rate and not a
	// single write that happened to land on the maximum.
	Write(Player, UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);
	for (int32 Taken = 0; Taken < StepsInFiveSeconds / 2; ++Taken)
	{
		UCataclysmRegeneration::ApplyStep(Player, Step, LongSinceHurt);
	}

	const float Half =
		Read(Player, UCataclysmVitalAttributeSet::GetEnergyShieldAttribute());
	TestTrue(FString::Printf(
			TEXT("and half the steps fill half of it: %.1f of %.1f"),
			Half, Maximum),
		FMath::IsNearlyEqual(Half, Maximum * 0.5f, 0.5f));

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

	// WRITING HEALTH TO ZERO NOW KILLS, AND IT DID NOT WHEN THIS TEST WAS
	// WRITTEN. Issue #971. `UCataclysmVitalAttributeSet::PostAttributeBaseChange`
	// runs the death check on every direct write, so the line above is a death
	// rather than a number. Said out loud rather than worked around, because a
	// reader arriving at the control below needs to know why it takes two steps
	// to reach a state that used to take one.
	TestTrue(TEXT("writing health to zero killed it"),
		UCataclysmSkillEffects::IsDead(Player));

	// AND STANDING IT BACK UP IS WHAT MAKES THE CONTROL BELOW POSSIBLE. Alive at
	// zero health is a state the game no longer produces by itself, and it is
	// still exactly the state this test needs: it is what makes the refusal
	// further down about being DEAD rather than about being at zero.
	//
	// `ClearDead` AND NOT `Revive`, which would refill all three vitals and hand
	// the control the answer it is meant to measure.
	UCataclysmSkillEffects::ClearDead(Player);

	UCataclysmRegeneration::ApplyStep(Player, 1.0f,
									  CataclysmRegenerationTest::LongSinceHurt);
	if (!TestEqual(TEXT("alive at zero health, it does come back"),
			CataclysmRegenerationTest::Read(Player, Health), 50.0f))
	{
		return false;
	}

	// THE WRITE IS THE DEATH, AND `MarkDead` IS NOW THE SECOND ASK. It refuses,
	// because the tag is already there, and it stays because this test is about
	// a character being refused for being dead however it got that way.
	CataclysmRegenerationTest::Write(Player, Health, 0.0f);
	UCataclysmSkillEffects::MarkDead(Player);
	TestTrue(TEXT("and it is dead again"),
		UCataclysmSkillEffects::IsDead(Player));

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

	// BEGINPLAY USED TO HAVE TO BE DISPATCHED BY HAND HERE, and this test is why
	// issue #654 was filed. The helper twenty test files copied was called
	// MakeWorldThatHasBegunPlay and actors spawned in its world never received
	// BeginPlay, because UWorld::BeginPlay does nothing without a game mode and a
	// world built by UWorld::CreateWorld has none. Two lines calling
	// DispatchBeginPlay stood here as the workaround.
	//
	// The world begins play for real now, so a spawned actor starts its own
	// clock, which is what the running game does. Nothing is called by hand.

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHealingCeilingTest,
	"Cataclysm.Regeneration.HealingStopsAtAReducedCeilingAndAnUncappedOneDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHealingCeilingTest::RunTest(const FString&)
{
	using namespace CataclysmRegenerationTest;
	using Vital = UCataclysmVitalAttributeSet;

	// THE MASOCHIST'S POINT OF NO RETURN KEYSTONE: "You cannot be healed above
	// 50% of your maximum health, but you deal 25% more damage." Issue #988.
	//
	// THE STAT IS A REDUCTION OF THE CEILING RATHER THAN THE CEILING ITSELF, so
	// zero means no cap with no sentinel, and two sources would stack in the
	// restrictive direction. The attribute's own declaration says why.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	UAbilitySystemComponent* System = Player ? SystemOf(Player) : nullptr;
	if (!TestNotNull(TEXT("a player with an ability system"), System))
	{
		return false;
	}

	Write(Player, Vital::GetMaxHealthAttribute(), 1'000.0f);

	// A CHARACTER WITHOUT THE NODE IS HEALED ALL THE WAY UP, asserted first so
	// every figure below is evidence of the cap rather than of anything else.
	Write(Player, Vital::GetHealthAttribute(), 100.0f);
	UCataclysmRegeneration::TopUp(
		*System, Vital::GetHealthAttribute(), Vital::GetMaxHealthAttribute(),
		/*Gain=*/5'000.0f, FGameplayTagContainer());
	TestEqual(TEXT("with no cap, healing reaches full health"),
			  Read(Player, Vital::GetHealthAttribute()), 1'000.0f, 0.01f);

	// AND WITH THE NODE IT STOPS HALF WAY. A reduction of 50 puts the ceiling at
	// 500 of 1000, and the healing offered is ten times what fits.
	Write(Player, Vital::GetHealingCeilingReductionAttribute(), 50.0f);
	Write(Player, Vital::GetHealthAttribute(), 100.0f);
	UCataclysmRegeneration::TopUp(
		*System, Vital::GetHealthAttribute(), Vital::GetMaxHealthAttribute(),
		/*Gain=*/5'000.0f, FGameplayTagContainer());
	TestEqual(TEXT("a reduction of fifty stops healing at half of maximum"),
			  Read(Player, Vital::GetHealthAttribute()), 500.0f, 0.01f);

	// A CHARACTER ALREADY ABOVE THE CEILING IS NOT PULLED DOWN TO IT. The node
	// says "cannot be healed above", not "is reduced to". Somebody who takes the
	// node at full health keeps the health they have and stops gaining.
	Write(Player, Vital::GetHealthAttribute(), 900.0f);
	UCataclysmRegeneration::TopUp(
		*System, Vital::GetHealthAttribute(), Vital::GetMaxHealthAttribute(),
		/*Gain=*/5'000.0f, FGameplayTagContainer());
	TestEqual(TEXT("a character above the ceiling keeps its health"),
			  Read(Player, Vital::GetHealthAttribute()), 900.0f, 0.01f);

	// AND BELOW THE CEILING HEALING STILL LANDS IN FULL, so the cap is a ceiling
	// rather than a refusal. Fifty offered to a character on 100 all lands,
	// because 150 is well under the ceiling of 500.
	Write(Player, Vital::GetHealthAttribute(), 100.0f);
	UCataclysmRegeneration::TopUp(
		*System, Vital::GetHealthAttribute(), Vital::GetMaxHealthAttribute(),
		/*Gain=*/50.0f, FGameplayTagContainer());
	TestEqual(TEXT("healing that fits under the ceiling lands in full"),
			  Read(Player, Vital::GetHealthAttribute()), 150.0f, 0.01f);

	// MANA IS NOT CAPPED BY IT. The node says health, and mana comes through
	// this same function, so this is the assertion that keeps the two apart.
	Write(Player, Vital::GetMaxManaAttribute(), 1'000.0f);
	Write(Player, Vital::GetManaAttribute(), 100.0f);
	UCataclysmRegeneration::TopUp(
		*System, Vital::GetManaAttribute(), Vital::GetMaxManaAttribute(),
		/*Gain=*/5'000.0f, FGameplayTagContainer());
	TestEqual(TEXT("mana fills to its own maximum despite the health cap"),
			  Read(Player, Vital::GetManaAttribute()), 1'000.0f, 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
