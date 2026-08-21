// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimSequence.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmGatekeeperCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the Gatekeeper, the seventh and last of the Demonic vertical slice
 * creatures and the only one in the game with phases.
 *
 * WHAT THESE GUARD, and every one is something the creature fails at in
 * silence -- no error, no warning, and nothing on screen that looks wrong:
 *
 * 1. **NOT DEALING A FREE MELEE HIT AT TWO METRES.** This creature's reach is
 *    Dread Cleave's own radius, because that is where the brain stops walking
 *    and starts attacking. `ACataclysmEnemyCharacter::AttackTarget` applies
 *    direct damage at that reach, so a Gatekeeper that did not override it
 *    would deal a full weapon's worth at two metres every three seconds ON TOP
 *    of the sweep it already made -- and its damage share of 2.10 is the
 *    highest in the slice, which would make that the largest unexplained
 *    number in the game. `ItsBasicAttackDealsNothingByItself` carries a control
 *    so the check cannot pass by the damage pipeline being asleep.
 *
 * 2. **THE SUMMON CAP COUNTING THE DEAD.** `ImpsStillAlive` forgets dead Imps
 *    before counting them. If it stopped, the cap would fill up and stay full
 *    and Call the Damned would never summon again -- and the creature would go
 *    on spending the cast and its cooldown with nothing to show for it.
 *    Killing the Imps is supposed to buy the player ten seconds, so this is the
 *    whole reason the adds are worth fighting.
 *
 * 3. **THE BURNING GROUND SPARING ITS OWN SIDE.** A ground zone that burned
 *    allies would still burn the player perfectly well, so nothing about
 *    playing the fight would look wrong. The project owner set a general rule
 *    on 2026-08-20 that a creature does not burn itself or its own side, and
 *    Soulfall is one of the two abilities that had been designed to.
 *
 * 4. **PHASE 3 KEEPING PHASE 1'S ABILITIES.** `ChooseAbility` asks whether an
 *    ability's phase is at most the creature's, not whether it is equal. Asking
 *    the wrong one would leave the boss in its last third with only the ring
 *    and the summon, standing still whenever both were cooling down.
 *
 * WHAT IS DELIBERATELY NOT HERE. The phase machine's own arithmetic --
 * thresholds, forward-only, noticing on the hit rather than on a frame -- is
 * `CataclysmEnemyPhaseTests.cpp`, because it belongs to every creature rather
 * than to this one. These tests use it and do not re-check it.
 *
 * AND THE NUMBERS THEMSELVES ARE CHECKED IN PYTHON.
 * `tools/tests/test_gatekeeper_matches_the_model.py` holds every constant in
 * the header to `sim/`. Continuous integration never builds this file, so that
 * one is what runs on a pull request and this one is what runs on a machine
 * with the engine.
 */

namespace CataclysmGatekeeperTest
{
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}

	static void TearDown(UWorld* World)
	{
		if (World)
		{
			World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	}

	static ACataclysmGatekeeperCharacter* SpawnGatekeeper(UWorld* World,
														  const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACataclysmGatekeeperCharacter>(
			ACataclysmGatekeeperCharacter::StaticClass(), Where,
			FRotator::ZeroRotator, Params);
	}

	/** Something with health to lose, on whichever side is asked for. Spawned
	 *  far away and then moved, because two capsules created at contact
	 *  distance push each other apart -- and this creature's capsule is 48 cm
	 *  wide on a body three metres tall. */
	static ACataclysmEnemyCharacter* SpawnOn(UWorld* World, ECataclysmTeam Side,
											 const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ACataclysmEnemyCharacter* Actor =
			World->SpawnActor<ACataclysmEnemyCharacter>(
				ACataclysmEnemyCharacter::StaticClass(),
				FVector(30000.0f, 30000.0f, 0.0f), FRotator::ZeroRotator,
				Params);
		if (!Actor)
		{
			return nullptr;
		}
		Actor->SetGenericTeamId(UCataclysmTeams::IdFor(Side));
		Actor->SetHealth(100000.0f);
		Actor->SetActorLocation(Where);
		return Actor;
	}

	static float HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* AbilitySystem =
			UCataclysmTargeting::AbilitySystemOf(Actor);
		if (!AbilitySystem)
		{
			return -1.0f;
		}
		return AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute());
	}

	/** Set health directly, the way a spawner does, without dealing damage.
	 *  `SetHealth` sets the MAXIMUM as well, which would keep the fraction at
	 *  1.0 and never move the creature out of phase 1. */
	static void WoundTo(ACataclysmEnemyCharacter* Creature, float Health)
	{
		if (UAbilitySystemComponent* AbilitySystem =
				UCataclysmTargeting::AbilitySystemOf(Creature))
		{
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
		}
	}

	static void AdvanceWorldClock(UWorld* World, double Seconds)
	{
		World->TimeSeconds += Seconds;
	}
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmGatekeeperCarriesItsDesignedProfile,
	"Cataclysm.Gatekeeper.ItCarriesItsDesignedProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGatekeeperCarriesItsDesignedProfile::RunTest(const FString&)
{
	using namespace CataclysmGatekeeperTest;
	using Gatekeeper_t = ACataclysmGatekeeperCharacter;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmGatekeeperCharacter* Boss =
		SpawnGatekeeper(World, FVector::ZeroVector);
	if (!Boss)
	{
		AddError(TEXT("could not spawn a Gatekeeper"));
		return false;
	}

	// **THE SLOWEST ATTACK INTERVAL IN THE ROSTER**, and it is what makes a 2
	// metre telegraphed sweep legal at all: the rules size a marker from half
	// the cycle it runs on.
	TestEqual(TEXT("seconds between sweeps"), Boss->SecondsBetweenAttacks(),
		Gatekeeper_t::DesignedAttackIntervalSeconds);

	// AND IT COMES FROM DesignedSecondsBetweenAttacks. `SecondsBetweenAttacks`
	// is final on the enemy base and divides this by the creature's buffs, so
	// a creature that overrode the wrong one would ignore every buff in the
	// game without a word.
	TestEqual(TEXT("and the designed figure is what it divides"),
		Boss->DesignedSecondsBetweenAttacks(),
		Gatekeeper_t::DesignedAttackIntervalSeconds);

	// ITS REACH IS DREAD CLEAVE'S RADIUS, which is what makes the AttackTarget
	// override below necessary rather than tidy.
	TestEqual(TEXT("its reach is Dread Cleave's radius"), Boss->AttackReachCm(),
		Gatekeeper_t::DreadCleaveRadiusCm);

	// **THE LARGEST NOTICE RADIUS IN THE GAME**, because it cannot close a gap
	// on an unwilling player and so must notice as far as Soulfall reaches.
	TestEqual(TEXT("it notices as far as Soulfall lobs"),
		Boss->SightRadiusCm(), Gatekeeper_t::GatekeeperNoticeRadiusCm);

	TestTrue(TEXT("and that is at least as far as it can attack"),
		Boss->SightRadiusCm() >= Gatekeeper_t::SoulfallRangeCm);

	// IT CAN WALK, and slower than every player class. A creature whose speed
	// was left unset moves at Unreal's default 600, which would make the
	// slowest thing in the design the second fastest thing in the game.
	TestEqual(TEXT("it walks at its designed speed"),
		Boss->GetCharacterMovement()->MaxWalkSpeed,
		Gatekeeper_t::DesignedWalkSpeedCmPerSecond);

	TestEqual(TEXT("it turns at its designed rate"),
		static_cast<float>(Boss->GetCharacterMovement()->RotationRate.Yaw),
		Gatekeeper_t::DesignedTurnRateDegreesPerSecond);

	TestEqual(TEXT("the capsule's radius is the designed body radius"),
		Boss->GetCapsuleComponent()->GetUnscaledCapsuleRadius(),
		Gatekeeper_t::GatekeeperCapsuleRadius);

	// **BY FAR THE TALLEST CAPSULE IN THE PROJECT**, from a mesh 3.11 metres
	// tall. The next tallest creature is 98.1.
	TestEqual(TEXT("the capsule's half-height comes from the mesh"),
		Boss->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),
		Gatekeeper_t::GatekeeperCapsuleHalfHeight);

	// IT ABSORBS RATHER THAN DODGING, and the zeroes are written out in the
	// header so that they are visibly designed rather than left unset.
	TestEqual(TEXT("it does not dodge"), Boss->EvasionPercent,
		Gatekeeper_t::DesignedEvasionPercent);

	TestEqual(TEXT("and it carries no energy shield"),
		Boss->EnergyShieldFraction,
		Gatekeeper_t::DesignedEnergyShieldFraction);

	TestEqual(TEXT("it resists its designed share of every hit"),
		Boss->ResistancePercent, Gatekeeper_t::DesignedResistancePercent);

	// **THE HIGHEST CRITICAL MULTIPLIER IN THE SLICE.**
	TestEqual(TEXT("its critical strike chance"), Boss->CritChancePercent,
		Gatekeeper_t::DesignedCritChancePercent);

	TestEqual(TEXT("and what one is worth"), Boss->CritMultiplierPercent,
		Gatekeeper_t::DesignedCritMultiplierPercent);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmGatekeeperIsTheOnlyCreatureWithPhases,
	"Cataclysm.Gatekeeper.ItIsTheOnlyCreatureWithPhases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGatekeeperIsTheOnlyCreatureWithPhases::RunTest(const FString&)
{
	using namespace CataclysmGatekeeperTest;
	using Gatekeeper_t = ACataclysmGatekeeperCharacter;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmGatekeeperCharacter* Boss =
		SpawnGatekeeper(World, FVector::ZeroVector);
	if (!Boss)
	{
		AddError(TEXT("could not spawn a Gatekeeper"));
		return false;
	}

	// A CREATURE OVERRIDES NOTHING TO GET PHASES. It sets the thresholds and
	// each ability names the phase it arrives in.
	if (Boss->PhaseHealthFractions.Num() != 2)
	{
		AddError(FString::Printf(
			TEXT("the Gatekeeper names %d phase thresholds and it should name "
				 "two, which make three phases."),
			Boss->PhaseHealthFractions.Num()));
		return false;
	}

	// **HIGHEST FIRST**, because RefreshPhase counts the thresholds at or below
	// the creature's health fraction. Listed the other way round it would reach
	// its last phase at 59% health.
	TestEqual(TEXT("the second phase's threshold is first"),
		Boss->PhaseHealthFractions[0],
		Gatekeeper_t::SecondPhaseHealthFraction);

	TestEqual(TEXT("and the third phase's is second"),
		Boss->PhaseHealthFractions[1],
		Gatekeeper_t::ThirdPhaseHealthFraction);

	TestTrue(TEXT("and the third is below the second"),
		Gatekeeper_t::ThirdPhaseHealthFraction
			< Gatekeeper_t::SecondPhaseHealthFraction);

	// AND THE PLAIN CREATURE HAS NONE, which is what says the two lines above
	// are this class's doing rather than something every enemy gets.
	ACataclysmEnemyCharacter* Ordinary =
		SpawnOn(World, ECataclysmTeam::Monsters, FVector(3000.0f, 0.0f, 0.0f));
	if (!Ordinary)
	{
		AddError(TEXT("could not spawn an ordinary enemy for the control"));
		return false;
	}

	TestEqual(TEXT("an ordinary enemy names no phase thresholds at all"),
		Ordinary->PhaseHealthFractions.Num(), 0);

	TestEqual(TEXT("and stays in phase 1 however hurt it is"),
		Ordinary->CurrentPhase(), 1);

	Ordinary->SetHealth(1000.0f);
	WoundTo(Ordinary, 1.0f);
	Ordinary->RefreshPhase();

	TestEqual(TEXT("still phase 1 at one point of health"),
		Ordinary->CurrentPhase(), 1);

	// --- AND THE BOSS REALLY MOVES THROUGH ITS THREE ----------------------

	Boss->SetHealth(1000.0f);

	TestEqual(TEXT("the boss opens in phase 1"), Boss->CurrentPhase(), 1);

	WoundTo(Boss, 1000.0f * Gatekeeper_t::SecondPhaseHealthFraction);
	Boss->RefreshPhase();

	TestEqual(TEXT("at exactly its second threshold it is in phase 2"),
		Boss->CurrentPhase(), 2);

	WoundTo(Boss, 1000.0f * Gatekeeper_t::ThirdPhaseHealthFraction);
	Boss->RefreshPhase();

	TestEqual(TEXT("and at its third it is in phase 3"),
		Boss->CurrentPhase(), 3);

	// **AND THERE IS NO FOURTH**, however low health goes. Two thresholds make
	// three phases and that is all.
	WoundTo(Boss, 1.0f);
	Boss->RefreshPhase();

	TestEqual(TEXT("there is no phase beyond the third"),
		Boss->CurrentPhase(), 3);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmGatekeeperOffersItsAbilitiesBiggestFirstAndTheSweepLast,
	"Cataclysm.Gatekeeper.ItOffersItsAbilitiesBiggestFirstAndTheSweepLast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGatekeeperOffersItsAbilitiesBiggestFirstAndTheSweepLast::RunTest(
	const FString&)
{
	using namespace CataclysmGatekeeperTest;
	using Gatekeeper_t = ACataclysmGatekeeperCharacter;

	// **WHAT THIS EXISTS FOR.** `ACataclysmEnemyController::ChooseAbility` takes
	// the FIRST entry whose phase, range and cooldown fit and never looks at the
	// shape. Dread Cleave's cooldown is zero, so a Dread Cleave at the front of
	// the array would be the only thing this creature ever did. Issue #491 is
	// that defect on the Abyssal Warden.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmGatekeeperCharacter* Boss =
		SpawnGatekeeper(World, FVector::ZeroVector);
	if (!Boss)
	{
		AddError(TEXT("could not spawn a Gatekeeper"));
		return false;
	}

	const TArray<FCataclysmEnemyAbility> Abilities = Boss->EnemyAbilities();

	if (Abilities.Num() != 4)
	{
		AddError(FString::Printf(
			TEXT("the Gatekeeper offers %d abilities and it should offer four: "
				 "the ring, the summon, the gout and the sweep."),
			Abilities.Num()));
		return false;
	}

	TestEqual(TEXT("the ring is first, because it is the heaviest thing it may "
				   "use"),
		Abilities[Gatekeeper_t::SoulHarvestAbility].Name,
		FName(TEXT("Soul Harvest")));

	TestEqual(TEXT("then the summon"),
		Abilities[Gatekeeper_t::CallTheDamnedAbility].Name,
		FName(TEXT("Call the Damned")));

	TestEqual(TEXT("then the gout"),
		Abilities[Gatekeeper_t::SoulfallAbility].Name, FName(TEXT("Soulfall")));

	TestEqual(TEXT("**and the sweep LAST**, because its cooldown is zero and "
				   "anything behind it would be unreachable"),
		Abilities[Gatekeeper_t::DreadCleaveAbility].Name,
		FName(TEXT("Dread Cleave")));

	TestEqual(TEXT("the sweep has no cooldown of its own"),
		Abilities[Gatekeeper_t::DreadCleaveAbility].CooldownSeconds, 0.0f);

	// --- WHICH PHASE EACH ARRIVES IN --------------------------------------

	TestEqual(TEXT("the sweep is there from the start"),
		Abilities[Gatekeeper_t::DreadCleaveAbility].Phase, 1);

	TestEqual(TEXT("and so is the gout"),
		Abilities[Gatekeeper_t::SoulfallAbility].Phase, 1);

	TestEqual(TEXT("the summon arrives in phase 2"),
		Abilities[Gatekeeper_t::CallTheDamnedAbility].Phase,
		Gatekeeper_t::CallTheDamnedPhase);

	TestEqual(TEXT("and the ring in phase 3"),
		Abilities[Gatekeeper_t::SoulHarvestAbility].Phase,
		Gatekeeper_t::SoulHarvestPhase);

	// --- WHAT EACH DRAWS AND HOW LONG IT WARNS ----------------------------

	TestEqual(TEXT("the sweep is a Strike"),
		Abilities[Gatekeeper_t::DreadCleaveAbility].Shape,
		ECataclysmSkillShape::Strike);

	TestEqual(TEXT("the gout is a Projectile"),
		Abilities[Gatekeeper_t::SoulfallAbility].Shape,
		ECataclysmSkillShape::Projectile);

	// **THE FIRST USE OF THE Summon SHAPE BY ANY ENEMY.** A shape the array
	// does not set reads as None, which draws no marker -- and for a summon
	// that is exactly what it should do anyway, so the value is what proves the
	// field is being set at all.
	TestEqual(TEXT("the summon is a Summon"),
		Abilities[Gatekeeper_t::CallTheDamnedAbility].Shape,
		ECataclysmSkillShape::Summon);

	TestEqual(TEXT("and the ring is a Strike"),
		Abilities[Gatekeeper_t::SoulHarvestAbility].Shape,
		ECataclysmSkillShape::Strike);

	TestEqual(TEXT("the sweep marks the ground it reaches"),
		Abilities[Gatekeeper_t::DreadCleaveAbility].MarkerRadiusCm,
		Gatekeeper_t::DreadCleaveRadiusCm);

	TestEqual(TEXT("the gout marks the circle it lands in"),
		Abilities[Gatekeeper_t::SoulfallAbility].MarkerRadiusCm,
		Gatekeeper_t::SoulfallRadiusCm);

	TestEqual(TEXT("**the summon marks nothing at all**, because there is no "
				   "ground for it to be drawn on"),
		Abilities[Gatekeeper_t::CallTheDamnedAbility].MarkerRadiusCm, 0.0f);

	TestEqual(TEXT("and it has no wind-up either, so the creature is not held "
				   "still with nothing on screen explaining why"),
		Abilities[Gatekeeper_t::CallTheDamnedAbility].WindUpSeconds, 0.0f);

	TestEqual(TEXT("the ring marks its whole 6.5 metres"),
		Abilities[Gatekeeper_t::SoulHarvestAbility].MarkerRadiusCm,
		Gatekeeper_t::SoulHarvestRadiusCm);

	// THE GOUT ARCS ONTO ITS TARGET AND NOTHING ELSE DOES. That is what makes
	// its marker a circle where it lands rather than a lane along the way.
	TestTrue(TEXT("the gout arcs onto where it is aimed"),
		Abilities[Gatekeeper_t::SoulfallAbility].bArcsOntoItsTarget);

	TestFalse(TEXT("and the sweep does not"),
		Abilities[Gatekeeper_t::DreadCleaveAbility].bArcsOntoItsTarget);

	// --- WHAT EACH CAN REACH ----------------------------------------------

	// **THE GOUT HAS A MINIMUM AND NOTHING ELSE DOES.** Below marked radius
	// plus its own body the circle covers the ground the creature stands on,
	// which is a melee attack wearing a thrown attack's telegraph.
	TestEqual(TEXT("the gout will not be lobbed at the creature's own feet"),
		Abilities[Gatekeeper_t::SoulfallAbility].MinRangeCm,
		Gatekeeper_t::SoulfallMinimumRangeCm);

	TestEqual(TEXT("the ring reaches as far as it is wide, and no further"),
		Abilities[Gatekeeper_t::SoulHarvestAbility].MaxRangeCm,
		Gatekeeper_t::SoulHarvestRadiusCm);

	TestEqual(TEXT("and it has no minimum, because it is at the creature's own "
				   "feet"),
		Abilities[Gatekeeper_t::SoulHarvestAbility].MinRangeCm, 0.0f);

	TestEqual(TEXT("the summon reaches as far as the Imps appear from it"),
		Abilities[Gatekeeper_t::CallTheDamnedAbility].MaxRangeCm,
		Gatekeeper_t::CallTheDamnedRangeCm);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmGatekeeperKeepsEveryEarlierPhasesAbilities,
	"Cataclysm.Gatekeeper.ItKeepsEveryEarlierPhasesAbilities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGatekeeperKeepsEveryEarlierPhasesAbilities::RunTest(const FString&)
{
	using namespace CataclysmGatekeeperTest;
	using Gatekeeper_t = ACataclysmGatekeeperCharacter;

	// **WHAT THIS EXISTS FOR.** `ChooseAbility` skips an ability whose phase is
	// GREATER than the creature's, not one whose phase DIFFERS. Phases add and
	// never take away, which is the research finding the whole boss design
	// rests on: across ten shipped bosses in Path of Exile and Last Epoch, a
	// transition adds a named ability and never removes one.
	//
	// ASKED FOR EQUALITY INSTEAD, this creature would lose Dread Cleave and
	// Soulfall on reaching phase 2 and stand there summoning -- and once the
	// summon was cooling down it would do nothing at all, with no error and
	// nothing on screen saying why.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmGatekeeperCharacter* Boss =
		SpawnGatekeeper(World, FVector::ZeroVector);
	if (!Boss)
	{
		AddError(TEXT("could not spawn a Gatekeeper"));
		return false;
	}

	ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(Boss->GetController());
	if (!Brain)
	{
		AddError(TEXT("the Gatekeeper has no controller, so nothing chooses "
					  "its abilities"));
		return false;
	}

	Boss->SetHealth(1000.0f);

	// STANDING WELL INSIDE EVERY ABILITY'S RANGE, so the only thing that can
	// separate them is the phase. 100 cm is inside the sweep's 200, the
	// summon's 400 and the ring's 650 -- and INSIDE the gout's minimum of 348,
	// which is why the gout is checked at a second distance below.
	constexpr float Close = 100.0f;

	// **SOMETHING TO FIGHT, WHICH Think REQUIRES.**
	// `ACataclysmEnemyController::Think` searches for a target before it does
	// anything else and returns without acting when there is none, so the
	// fall-back section at the end of this test stamps no cooldown at all
	// without one. Written without it, that section read the ring as still
	// ready on every pass and the test failed -- which is how this comment came
	// to exist.
	ACataclysmEnemyCharacter* Player =
		SpawnOn(World, ECataclysmTeam::Players, FVector(Close, 0.0f, 0.0f));
	if (!Player)
	{
		AddError(TEXT("could not spawn something for the boss to fight"));
		return false;
	}

	// --- PHASE 1: the sweep, and neither of the later two ------------------

	TestEqual(TEXT("in phase 1 the creature is in phase 1"),
		Boss->CurrentPhase(), 1);

	TestEqual(TEXT("in phase 1 the only thing available up close is the sweep"),
		Brain->ChooseAbility(Close), (int32)Gatekeeper_t::DreadCleaveAbility);

	// AND THE GOUT IS AVAILABLE FROM THE START TOO, at a distance it can
	// actually be lobbed to. Declared here and used again in phase 2 below.
	const float Lobbing =
		(Gatekeeper_t::SoulfallMinimumRangeCm + Gatekeeper_t::SoulfallRangeCm)
		/ 2.0f;

	TestEqual(TEXT("and further out, past the ring, it lobs"),
		Brain->ChooseAbility(Lobbing), (int32)Gatekeeper_t::SoulfallAbility);

	// --- PHASE 2: the summon arrives, and the sweep is still there ---------

	WoundTo(Boss, 1000.0f * Gatekeeper_t::SecondPhaseHealthFraction);
	Boss->RefreshPhase();

	if (Boss->CurrentPhase() != 2)
	{
		AddError(FString::Printf(
			TEXT("the creature is in phase %d after being wounded to its "
				 "second threshold, so this test cannot check what phase 2 "
				 "adds."),
			Boss->CurrentPhase()));
		return false;
	}

	TestEqual(TEXT("**phase 2 adds the summon**, which now outranks the sweep"),
		Brain->ChooseAbility(Close), (int32)Gatekeeper_t::CallTheDamnedAbility);

	// **AND THE SWEEP HAS NOT BEEN TAKEN AWAY**, which is the whole point.
	// Shown by spending the summon and asking again: with the summon cooling
	// down, a creature that had lost phase 1 would have nothing at all to do at
	// this distance.
	Brain->Think();
	AdvanceWorldClock(World, Gatekeeper_t::DesignedAttackIntervalSeconds + 0.05);

	TestFalse(TEXT("the summon is on cooldown once it has been cast"),
		Brain->IsAbilityReady(Gatekeeper_t::CallTheDamnedAbility));

	TestEqual(TEXT("**and the creature falls back to the sweep rather than "
				   "standing still**, which is what phase 1 still being "
				   "available means"),
		Brain->ChooseAbility(Close), (int32)Gatekeeper_t::DreadCleaveAbility);

	// AND THE GOUT IS STILL THERE TOO, at a distance it can be lobbed to.
	TestEqual(TEXT("and further out it still lobs"),
		Brain->ChooseAbility(Lobbing), (int32)Gatekeeper_t::SoulfallAbility);

	// --- PHASE 3: the ring arrives, and BOTH earlier phases remain ---------

	WoundTo(Boss, 1000.0f * Gatekeeper_t::ThirdPhaseHealthFraction);
	Boss->RefreshPhase();

	if (Boss->CurrentPhase() != 3)
	{
		AddError(FString::Printf(
			TEXT("the creature is in phase %d after being wounded to its third "
				 "threshold."),
			Boss->CurrentPhase()));
		return false;
	}

	TestEqual(TEXT("**phase 3 adds the ring**, which outranks everything"),
		Brain->ChooseAbility(Close), (int32)Gatekeeper_t::SoulHarvestAbility);

	// **AND EVERY EARLIER ABILITY IS STILL AVAILABLE.** This is the check the
	// whole test is named for: in phase 3 not one of the four is skipped by
	// phase.
	const TArray<FCataclysmEnemyAbility> Abilities = Boss->EnemyAbilities();
	for (int32 Index = 0; Index < Abilities.Num(); ++Index)
	{
		TestTrue(*FString::Printf(
				TEXT("in phase 3, %s (phase %d) is still available"),
				*Abilities[Index].Name.ToString(), Abilities[Index].Phase),
			Abilities[Index].Phase <= Boss->CurrentPhase());
	}

	// AND THE CREATURE FALLS BACK THROUGH THEM AS EACH COOLS DOWN, which is
	// what "still available" means in practice rather than as a phase number.
	//
	// TWO PASSES FOR THE RING, because it has a two second wind-up: the first
	// begins it and the second lands it, and the cooldown is stamped when it
	// LANDS. The summon needs one pass, because a Summon draws no marker and so
	// has no wind-up to wait through.
	Brain->Think();
	AdvanceWorldClock(World, Gatekeeper_t::SoulHarvestWindUpSeconds + 0.05);
	Brain->Think();

	// **PAST THE SUMMON'S OWN COOLDOWN, NOT ONLY PAST THE ATTACK INTERVAL.**
	// The phase 2 section above already spent the summon, so waiting out the
	// three second interval alone would leave it cooling down and the creature
	// would fall past it to the sweep. That is what this test read first time
	// it was written: a 3 where it wanted a 1.
	//
	// AND IT DOES NOT BRING THE RING BACK: the ring cools down for twenty
	// seconds and this waits ten, which is why the assertion below still means
	// something.
	AdvanceWorldClock(World, Gatekeeper_t::CallTheDamnedCooldownSeconds + 0.05);

	TestFalse(TEXT("the ring is on cooldown once it has landed"),
		Brain->IsAbilityReady(Gatekeeper_t::SoulHarvestAbility));

	TestEqual(TEXT("**so the creature falls back to the summon rather than "
				   "standing still**"),
		Brain->ChooseAbility(Close), (int32)Gatekeeper_t::CallTheDamnedAbility);

	Brain->Think();
	AdvanceWorldClock(World, Gatekeeper_t::DesignedAttackIntervalSeconds + 0.05);

	TestFalse(TEXT("and the summon is on cooldown once it has been cast"),
		Brain->IsAbilityReady(Gatekeeper_t::CallTheDamnedAbility));

	TestEqual(TEXT("**and with both of those spent it falls back to the "
				   "sweep**, which is what a phase adding rather than "
				   "replacing means"),
		Brain->ChooseAbility(Close), (int32)Gatekeeper_t::DreadCleaveAbility);

	// AND THE SWEEP IS ALWAYS THERE, because its cooldown is zero. That is what
	// stops the boss ever having nothing at all to do.
	TestTrue(TEXT("the sweep is never on cooldown, so the creature always has "
				  "something"),
		Brain->IsAbilityReady(Gatekeeper_t::DreadCleaveAbility));

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmGatekeeperBasicAttackDealsNothingByItself,
	"Cataclysm.Gatekeeper.ItsBasicAttackDealsNothingByItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGatekeeperBasicAttackDealsNothingByItself::RunTest(const FString&)
{
	using namespace CataclysmGatekeeperTest;

	// **WHAT THIS EXISTS FOR.** `ACataclysmEnemyCharacter::AttackTarget` applies
	// direct damage, and `ACataclysmEnemyController::Think` calls it whenever a
	// target is inside `AttackReachCm`. This creature's reach is Dread Cleave's
	// own radius of two metres.
	//
	// So a Gatekeeper that did not override AttackTarget would deal a full
	// weapon's worth of melee damage at two metres, through walls, every three
	// seconds, ON TOP of the sweep it already made -- and its damage share of
	// 2.10 is the highest in the slice, so that hit would be the largest
	// unexplained number in the game.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmGatekeeperCharacter* Boss =
		SpawnGatekeeper(World, FVector::ZeroVector);
	if (!Boss)
	{
		AddError(TEXT("could not spawn a Gatekeeper"));
		return false;
	}
	Boss->SetAttackDamage(100.0f);

	// STANDING WELL INSIDE ITS REACH, which is the whole point: at this
	// distance the base class's melee hit would land.
	ACataclysmEnemyCharacter* Target =
		SpawnOn(World, ECataclysmTeam::Players, FVector(100.0f, 0.0f, 0.0f));
	if (!Target)
	{
		AddError(TEXT("could not spawn something to hit"));
		return false;
	}

	const float Before = HealthOf(Target);
	if (Before <= 0.0f)
	{
		AddError(TEXT("the target has no health to lose, so this test would "
					  "pass by doing nothing"));
		return false;
	}

	// AND THE CREATURE REALLY WOULD REACH IT, checked rather than assumed. If
	// the reach ever shrank below this distance the assertion after it would
	// pass for the wrong reason.
	TestTrue(*FString::Printf(
			TEXT("the target stands 100 cm away, inside the creature's %.0f cm "
				 "reach"),
			Boss->AttackReachCm()),
		Boss->AttackReachCm() > 100.0f);

	Boss->AttackTarget(Target);

	TestEqual(TEXT("the basic attack path deals nothing at all"),
		HealthOf(Target), Before);

	TestNull(TEXT("and it lobs nothing either, because the gout is an ability"),
		Boss->LastGoutLobbed.Get());

	TestNull(TEXT("and leaves no burning ground"),
		Boss->LastGroundLeftBurning.Get());

	// THE CONTROL. A plain enemy at the same distance with the same damage DOES
	// hurt the target, which is what says the check above is about this
	// creature's override rather than about the damage pipeline being asleep.
	ACataclysmEnemyCharacter* Ordinary =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(),
			FVector(-100.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!Ordinary)
	{
		AddError(TEXT("could not spawn an ordinary enemy for the control"));
		return false;
	}
	Ordinary->SetAttackDamage(100.0f);
	Ordinary->AttackTarget(Target);

	TestTrue(*FString::Printf(
			TEXT("but an ordinary enemy's basic attack does hurt it: %.1f to "
				 "%.1f"),
			Before, HealthOf(Target)),
		HealthOf(Target) < Before);

	// **AND THE SWEEP ITSELF DOES HURT.** Which is what says the creature is
	// not simply unable to deal damage: the same target, the same distance,
	// through the ability rather than through the base class's melee path.
	const float BeforeTheSweep = HealthOf(Target);
	Boss->UseEnemyAbility(ACataclysmGatekeeperCharacter::DreadCleaveAbility,
						  Target, Target->GetActorLocation());

	TestTrue(*FString::Printf(
			TEXT("and Dread Cleave, which IS its basic attack, hurts it: %.1f "
				 "to %.1f"),
			BeforeTheSweep, HealthOf(Target)),
		HealthOf(Target) < BeforeTheSweep);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmGatekeeperSweepIsAConeAndItsRingIsNot,
	"Cataclysm.Gatekeeper.ItsSweepIsAConeAndItsRingIsNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGatekeeperSweepIsAConeAndItsRingIsNot::RunTest(const FString&)
{
	using namespace CataclysmGatekeeperTest;
	using Gatekeeper_t = ACataclysmGatekeeperCharacter;

	// **STANDING BEHIND IT IS THE ANSWER TO DREAD CLEAVE AND IS NOT AN ANSWER
	// TO SOUL HARVEST.** That difference is the whole of what the two Angle
	// figures mean, and the C++ expresses it by using two different searches --
	// a cone for one and a sphere for the other. A creature that used the cone
	// search for both would have a finale that could be walked around, and
	// nothing about the marker on screen would say so.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmGatekeeperCharacter* Boss =
		SpawnGatekeeper(World, FVector::ZeroVector);
	if (!Boss)
	{
		AddError(TEXT("could not spawn a Gatekeeper"));
		return false;
	}
	Boss->SetAttackDamage(100.0f);
	Boss->SetActorRotation(FRotator::ZeroRotator);   // facing +X

	// ONE IN FRONT AND ONE BEHIND, BOTH THE SAME DISTANCE AWAY and both well
	// inside the sweep's two metres. Same distance so that the only thing
	// separating them is which side of the creature they are on.
	const float Inside = Gatekeeper_t::DreadCleaveRadiusCm / 2.0f;

	ACataclysmEnemyCharacter* InFront =
		SpawnOn(World, ECataclysmTeam::Players, FVector(Inside, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Behind =
		SpawnOn(World, ECataclysmTeam::Players, FVector(-Inside, 0.0f, 0.0f));
	if (!InFront || !Behind)
	{
		AddError(TEXT("could not spawn the two things the cone chooses "
					  "between"));
		return false;
	}

	const float FrontBefore = HealthOf(InFront);
	const float BehindBefore = HealthOf(Behind);

	Boss->UseEnemyAbility(Gatekeeper_t::DreadCleaveAbility, InFront,
						  InFront->GetActorLocation());

	TestTrue(*FString::Printf(
			TEXT("the sweep hits what is in front of it: %.1f to %.1f"),
			FrontBefore, HealthOf(InFront)),
		HealthOf(InFront) < FrontBefore);

	TestEqual(TEXT("**and misses what is behind it**, at the same distance"),
		HealthOf(Behind), BehindBefore);

	// --- AND THE RING CATCHES BOTH ----------------------------------------

	// FURTHER OUT THAN THE SWEEP REACHES, so the two are not being measured on
	// the same ground: this pair is outside Dread Cleave's 200 cm and inside
	// Soul Harvest's 650.
	const float RingOnly =
		(Gatekeeper_t::DreadCleaveRadiusCm + Gatekeeper_t::SoulHarvestRadiusCm)
		/ 2.0f;
	InFront->SetActorLocation(FVector(RingOnly, 0.0f, 0.0f));
	Behind->SetActorLocation(FVector(-RingOnly, 0.0f, 0.0f));

	const float FrontBeforeTheRing = HealthOf(InFront);
	const float BehindBeforeTheRing = HealthOf(Behind);

	// THE SWEEP CANNOT REACH THEM THERE, checked rather than assumed, or the
	// ring's result below would prove nothing about the ring.
	Boss->UseEnemyAbility(Gatekeeper_t::DreadCleaveAbility, InFront,
						  InFront->GetActorLocation());

	TestEqual(TEXT("the sweep cannot reach that far in front"),
		HealthOf(InFront), FrontBeforeTheRing);

	Boss->UseEnemyAbility(Gatekeeper_t::SoulHarvestAbility, InFront,
						  Boss->GetActorLocation());

	TestTrue(*FString::Printf(
			TEXT("the ring catches what is in front: %.1f to %.1f"),
			FrontBeforeTheRing, HealthOf(InFront)),
		HealthOf(InFront) < FrontBeforeTheRing);

	TestTrue(*FString::Printf(
			TEXT("**and what is behind too**, because there is no standing "
				 "behind a full circle: %.1f to %.1f"),
			BehindBeforeTheRing, HealthOf(Behind)),
		HealthOf(Behind) < BehindBeforeTheRing);

	// AND NOTHING OUTSIDE THE RING. Placed just beyond the radius rather than
	// far away, because a check that only refuses the far side of the map is
	// not checking the radius.
	ACataclysmEnemyCharacter* TooFar =
		SpawnOn(World, ECataclysmTeam::Players,
				FVector(0.0f, Gatekeeper_t::SoulHarvestRadiusCm + 200.0f, 0.0f));
	if (!TooFar)
	{
		AddError(TEXT("could not spawn something outside the ring"));
		return false;
	}

	const float OutsideBefore = HealthOf(TooFar);
	Boss->UseEnemyAbility(Gatekeeper_t::SoulHarvestAbility, TooFar,
						  Boss->GetActorLocation());

	TestEqual(TEXT("and nothing standing outside the ring is touched"),
		HealthOf(TooFar), OutsideBefore);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmGatekeeperBurningGroundSparesItsOwnSide,
	"Cataclysm.Gatekeeper.ItsBurningGroundSparesItsOwnSide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGatekeeperBurningGroundSparesItsOwnSide::RunTest(const FString&)
{
	using namespace CataclysmGatekeeperTest;
	using Gatekeeper_t = ACataclysmGatekeeperCharacter;

	// **WHAT THIS EXISTS FOR.** Soulfall was designed with `GroundHitsAllies=1`
	// so that the summoned Imps of phase 2 burned in it, and on 2026-08-20 the
	// project owner set a general rule that a creature does not burn itself or
	// its own side.
	//
	// A GROUND ZONE THAT BURNED ALLIES WOULD STILL BURN THE PLAYER PERFECTLY
	// WELL, so nothing about playing the fight would look wrong. The only
	// visible sign would be the boss's own Imps dying to it, which reads as the
	// Imps being fragile.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmGatekeeperCharacter* Boss =
		SpawnGatekeeper(World, FVector::ZeroVector);
	if (!Boss)
	{
		AddError(TEXT("could not spawn a Gatekeeper"));
		return false;
	}
	Boss->SetAttackDamage(100.0f);

	// AIMED WELL PAST ITS OWN MINIMUM RANGE, so this is a lob the creature
	// would really make.
	const FVector LandsAt(Gatekeeper_t::SoulfallRangeCm / 2.0f, 0.0f, 0.0f);

	ACataclysmEnemyCharacter* Player =
		SpawnOn(World, ECataclysmTeam::Players, LandsAt);
	if (!Player)
	{
		AddError(TEXT("could not spawn something for the fire to burn"));
		return false;
	}

	Boss->UseEnemyAbility(Gatekeeper_t::SoulfallAbility, Player, LandsAt);

	// --- WHAT IT LOBBED ---------------------------------------------------

	ACataclysmProjectile* Gout = Boss->LastGoutLobbed.Get();
	if (!Gout)
	{
		AddError(TEXT("Soulfall lobbed nothing at all"));
		return false;
	}

	// **RadiusCm, NOT BodyRadiusCm.** RadiusCm is what the skill's Radius
	// parameter means and what the marker drew; BodyRadiusCm is how wide the
	// flying object is.
	TestEqual(TEXT("the gout bursts as wide as the circle that was marked"),
		Gout->RadiusCm, Gatekeeper_t::SoulfallRadiusCm);

	// A FLIGHT TIME AND NO SPEED AT ALL, so it lobs. A ballistic shot has no
	// single speed, so the projectile is told how long it has. A projectile
	// given NEITHER is a beam.
	TestTrue(*FString::Printf(
			TEXT("it is given a flight time of %.4f s, so it arcs"),
			Gout->FlightSeconds),
		Gout->FlightSeconds > 0.0f);

	TestEqual(TEXT("and the flight time is the one the creature computed for "
				   "that distance"),
		Gout->FlightSeconds, Boss->SoulfallFlightSecondsFor(LandsAt));

	// --- WHAT IT LEFT BEHIND ----------------------------------------------

	ACataclysmGroundZone* Fire = Boss->LastGroundLeftBurning.Get();
	if (!Fire)
	{
		AddError(TEXT("Soulfall left no burning ground, which is most of what "
					  "the ability is for"));
		return false;
	}

	// **THE ORDINARY KIND OF ZONE**, which is the one that knows whose side it
	// is on.
	TestFalse(TEXT("the fire does not burn everything standing in it"),
		Fire->bBurnsEveryone);

	TestFalse(TEXT("and it is round rather than a lane, because a gout lands "
				   "somewhere rather than travelling"),
		Fire->IsLong());

	TestEqual(TEXT("it is as wide as the ground the burst covered"),
		Fire->RadiusCm, Gatekeeper_t::SoulfallGroundRadiusCm);

	// AT THE PLACE IT WAS MARKED, not where the target is now. That is the
	// whole of what a telegraph buys.
	TestTrue(*FString::Printf(
			TEXT("and it is laid where the circle was marked, at %s"),
			*Fire->GetActorLocation().ToCompactString()),
		Fire->GetActorLocation().Equals(LandsAt, 1.0f));

	// --- AND NOW WHO IT BURNS ---------------------------------------------

	// ONE OF EACH, BOTH AT THE CENTRE OF THE FIRE. Same place so that the only
	// thing separating them is which side they are on.
	ACataclysmEnemyCharacter* Ally =
		SpawnOn(World, ECataclysmTeam::Monsters, LandsAt);
	if (!Ally)
	{
		AddError(TEXT("could not spawn an ally to stand in the fire"));
		return false;
	}

	const float PlayerBefore = HealthOf(Player);
	const float AllyBefore = HealthOf(Ally);
	const float BossBefore = HealthOf(Boss);

	Fire->Sweep();

	TestTrue(*FString::Printf(
			TEXT("the fire burns the player standing in it: %.1f to %.1f"),
			PlayerBefore, HealthOf(Player)),
		HealthOf(Player) < PlayerBefore);

	TestEqual(TEXT("**and does NOT burn an ally standing in the same spot**"),
		HealthOf(Ally), AllyBefore);

	TestEqual(TEXT("exactly one thing was caught"), Fire->LastSweepCount, 1);

	// AND IT DOES NOT BURN THE CREATURE THAT LAID IT, which the rule covers
	// too. Walked into its own fire rather than lobbed at nothing, because a
	// boss that cannot catch anybody will cross its own patches.
	Boss->SetActorLocation(LandsAt);
	Fire->Sweep();

	TestEqual(TEXT("and it does not burn the Gatekeeper that laid it either"),
		HealthOf(Boss), BossBefore);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmGatekeeperSummonCapCountsOnlyTheLiving,
	"Cataclysm.Gatekeeper.ItsSummonCapCountsOnlyTheLiving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGatekeeperSummonCapCountsOnlyTheLiving::RunTest(const FString&)
{
	using namespace CataclysmGatekeeperTest;
	using Gatekeeper_t = ACataclysmGatekeeperCharacter;

	// **WHAT THIS EXISTS FOR.** `ImpsStillAlive` forgets dead Imps before
	// counting them. Nothing tells this creature that one of its Imps has died,
	// so the forgetting happens at the moment of counting.
	//
	// IF IT STOPPED, the cap would fill up and stay full: Call the Damned would
	// go on spending its cast and its ten second cooldown and summon nothing,
	// for the rest of the fight, with no error and nothing on screen saying
	// why. And killing the adds -- which is the whole reason they are worth
	// fighting -- would achieve nothing.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmGatekeeperCharacter* Boss =
		SpawnGatekeeper(World, FVector::ZeroVector);
	if (!Boss)
	{
		AddError(TEXT("could not spawn a Gatekeeper"));
		return false;
	}

	TestEqual(TEXT("it starts with nothing summoned"), Boss->ImpsStillAlive(), 0);

	// --- ONE CAST ---------------------------------------------------------

	Boss->UseEnemyAbility(Gatekeeper_t::CallTheDamnedAbility, nullptr,
						  Boss->GetActorLocation());

	TestEqual(TEXT("one cast brings its designed number of Imps"),
		Boss->ImpsStillAlive(), Gatekeeper_t::CallTheDamnedCount);

	// AND THEY ARRIVE OUTSIDE THE CREATURE'S OWN BODY AND INSIDE ITS RANGE, so
	// an Imp does not appear inside the boss and get pushed out.
	for (const TWeakObjectPtr<ACataclysmImpCharacter>& Weak : Boss->CalledImps)
	{
		ACataclysmImpCharacter* Imp = Weak.Get();
		if (!Imp)
		{
			continue;
		}
		const float Away = FVector::Dist2D(Imp->GetActorLocation(),
										   Boss->GetActorLocation());
		TestTrue(*FString::Printf(
				TEXT("an Imp arrived %.0f cm out, clear of the creature's own "
					 "%.0f cm body and inside its %.0f cm range"),
				Away, Gatekeeper_t::GatekeeperCapsuleRadius,
				Gatekeeper_t::CallTheDamnedRangeCm),
			Away > Gatekeeper_t::GatekeeperCapsuleRadius
				&& Away <= Gatekeeper_t::CallTheDamnedRangeCm + 1.0f);
	}

	// --- CASTING UNTIL THE CAP IS REACHED ---------------------------------

	// AS MANY CASTS AS IT TAKES, plus two, so that the loop proves the cap
	// holds rather than that the arithmetic happens to stop there.
	const int32 CastsToFill =
		Gatekeeper_t::CallTheDamnedMaxAlive / Gatekeeper_t::CallTheDamnedCount;
	for (int32 Cast = 1; Cast < CastsToFill + 2; ++Cast)
	{
		Boss->UseEnemyAbility(Gatekeeper_t::CallTheDamnedAbility, nullptr,
							  Boss->GetActorLocation());
	}

	TestEqual(TEXT("**however many times it casts, the cap holds**"),
		Boss->ImpsStillAlive(), Gatekeeper_t::CallTheDamnedMaxAlive);

	// --- AND NOW THE PART THAT SILENTLY BREAKS ----------------------------

	// KILL THEM ALL. A dead Imp is a valid pointer for as long as its death
	// clip runs, so this is exactly the case a null check alone would miss.
	//
	// THROUGH HandleDeath RATHER THAN THROUGH DAMAGE, which is what
	// CataclysmEnemyDeathTests.cpp does. It is the one call that marks a
	// creature dead, and going through the damage pipeline would make this
	// test depend on the Imp's 25% evasion roll as well as on the thing it is
	// about.
	int32 Killed = 0;
	for (const TWeakObjectPtr<ACataclysmImpCharacter>& Weak : Boss->CalledImps)
	{
		if (ACataclysmImpCharacter* Imp = Weak.Get())
		{
			Imp->HandleDeath();
			if (UCataclysmSkillEffects::IsDead(Imp))
			{
				++Killed;
			}
		}
	}

	if (Killed == 0)
	{
		AddError(TEXT("no Imp could be killed, so the rest of this test would "
					  "pass by doing nothing"));
		return false;
	}

	// THEY ARE STILL VALID POINTERS, which is the whole difficulty. If they had
	// already been collected, a null check alone would have been enough and
	// this test would not be checking what it is named for.
	int32 StillPointedAt = 0;
	for (const TWeakObjectPtr<ACataclysmImpCharacter>& Weak : Boss->CalledImps)
	{
		if (Weak.Get())
		{
			++StillPointedAt;
		}
	}

	TestTrue(*FString::Printf(
			TEXT("the dead Imps are still valid pointers (%d of %d), so a null "
				 "check alone would count them"),
			StillPointedAt, Killed),
		StillPointedAt > 0);

	TestEqual(TEXT("**and the cap counts none of them**"),
		Boss->ImpsStillAlive(), 0);

	// AND THE NEXT CAST FILLS THE FIELD AGAIN, which is what killing them buys
	// the player: the ten seconds until this happens.
	Boss->UseEnemyAbility(Gatekeeper_t::CallTheDamnedAbility, nullptr,
						  Boss->GetActorLocation());

	TestEqual(TEXT("so the next cast summons a full wave again"),
		Boss->ImpsStillAlive(), Gatekeeper_t::CallTheDamnedCount);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmGatekeeperWearsItsMeshAndHidesThePlaceholder,
	"Cataclysm.Gatekeeper.ItWearsItsMeshAndHidesThePlaceholder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGatekeeperWearsItsMeshAndHidesThePlaceholder::RunTest(const FString&)
{
	using namespace CataclysmGatekeeperTest;
	using Gatekeeper_t = ACataclysmGatekeeperCharacter;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmGatekeeperCharacter* Boss =
		SpawnGatekeeper(World, FVector::ZeroVector);
	if (!Boss)
	{
		AddError(TEXT("could not spawn a Gatekeeper"));
		return false;
	}

	const bool bDressed = Boss->ResolveBody(/*bIncludeAnimation=*/true);

	if (!bDressed)
	{
		// THE PARAGON PACKS ARE NOT COMMITTED, so this half cannot run on a
		// machine without them. Reported through the helper rather than with
		// AddInfo, because `python tools/unreal_build.py tests` only names a
		// skip that went through the helper. Issue #467.
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Sevarog pack is not installed, so the mesh, the "
				 "six clips, the death clip and the placeholder check were not "
				 "exercised. See game/docs/enemy-source-assets.md."));
		return true;
	}

	USkeletalMeshComponent* MeshComponent = Boss->GetMesh();
	if (!MeshComponent)
	{
		AddError(TEXT("the Gatekeeper has no skeletal mesh component"));
		return false;
	}

	TestNotNull(TEXT("it is wearing a skeletal mesh"),
		MeshComponent->GetSkeletalMeshAsset());

	// THE MESH DROPS BY THE CAPSULE'S HALF-HEIGHT, so its feet are on the
	// bottom of the capsule rather than at its centre. For this creature that
	// is a drop of 1.55 metres, by far the largest in the project, so a missing
	// drop would leave it buried to the waist.
	TestEqual(TEXT("the mesh is dropped so its feet are on the capsule bottom"),
		static_cast<float>(MeshComponent->GetRelativeLocation().Z),
		-Gatekeeper_t::GatekeeperCapsuleHalfHeight);

	TestEqual(TEXT("and turned by the engine's character-mesh yaw"),
		static_cast<float>(MeshComponent->GetRelativeRotation().Yaw), -90.0f);

	TestNotNull(TEXT("the idle loaded"), Boss->IdleAnimation.Get());
	TestNotNull(TEXT("the walk loaded"), Boss->JogAnimation.Get());
	TestNotNull(TEXT("the sweep clip loaded"), Boss->CleaveAnimation.Get());
	TestNotNull(TEXT("the gout clip loaded"), Boss->SoulfallAnimation.Get());
	TestNotNull(TEXT("the summon clip loaded"), Boss->CallAnimation.Get());
	TestNotNull(TEXT("the ultimate clip loaded"), Boss->UltimateAnimation.Get());

	TestEqual(TEXT("it has exactly one way to fall over"),
		Boss->DeathAnimations.Num(), Gatekeeper_t::DeathAnimationCount);

	// **FOUR DIFFERENT CLIPS FOR FOUR DIFFERENT ABILITIES.** A boss whose lob
	// and hammer swing looked the same would be a boss the player could not
	// read, and the pack ships enough motion to avoid it.
	if (Boss->CleaveAnimation.Get() && Boss->SoulfallAnimation.Get()
		&& Boss->CallAnimation.Get() && Boss->UltimateAnimation.Get())
	{
		TestNotEqual(TEXT("the gout's clip is not the sweep's"),
			Boss->SoulfallAnimation.Get(), Boss->CleaveAnimation.Get());
		TestNotEqual(TEXT("the summon's clip is not the sweep's"),
			Boss->CallAnimation.Get(), Boss->CleaveAnimation.Get());
		TestNotEqual(TEXT("and the ultimate's is its own"),
			Boss->UltimateAnimation.Get(), Boss->CleaveAnimation.Get());
	}

	// THE MEASURED LENGTHS ARE THE ONES THE HEADER CARRIES. Every wind-up play
	// rate is computed from these, so a re-imported clip of a different length
	// would change how fast the creature swings without anything saying so.
	if (const UAnimSequence* Sweep = Boss->CleaveAnimation.Get())
	{
		TestEqual(TEXT("the sweep clip is the length the header records"),
			Sweep->GetPlayLength(), Gatekeeper_t::CleaveAnimationSeconds,
			0.001f);
	}
	if (const UAnimSequence* Gout = Boss->SoulfallAnimation.Get())
	{
		TestEqual(TEXT("the gout clip is the length the header records"),
			Gout->GetPlayLength(), Gatekeeper_t::SoulfallAnimationSeconds,
			0.001f);
	}
	if (const UAnimSequence* Ultimate = Boss->UltimateAnimation.Get())
	{
		TestEqual(TEXT("the ultimate clip is the length the header records"),
			Ultimate->GetPlayLength(), Gatekeeper_t::UltimateAnimationSeconds,
			0.001f);
	}

	// OTHERWISE THE CYLINDER SITS INSIDE THE CREATURE.
	if (Boss->PlaceholderBody)
	{
		TestFalse(TEXT("and the placeholder cylinder is hidden"),
			Boss->PlaceholderBody->IsVisible());
	}

	// **THE WALK RATE IS A PLACEHOLDER, NOT A DERIVATION.**
	// `tools/measure_animation_stride.py` failed its own control on this rig --
	// 0.0 cm/s for all three locomotion clips and 14.7 for the idle, which must
	// read zero -- so the rate is 1.0 and unverified. Issue #778. This checks
	// only that it is inside the clamp, which is all that can be checked
	// without a measurement.
	TestTrue(*FString::Printf(
			TEXT("the walk's play rate is %.4f, inside the %.1f to %.1f clamp. "
				 "It is a PLACEHOLDER and not derived; see issue #778"),
			Gatekeeper_t::JogPlayRate, Gatekeeper_t::MinimumPlayRate,
			Gatekeeper_t::MaximumPlayRate),
		Gatekeeper_t::JogPlayRate >= Gatekeeper_t::MinimumPlayRate
			&& Gatekeeper_t::JogPlayRate <= Gatekeeper_t::MaximumPlayRate);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmGatekeeperPutsItsLoopBackAfterAOneShot,
	"Cataclysm.Gatekeeper.ItPutsItsLoopBackAfterAOneShot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGatekeeperPutsItsLoopBackAfterAOneShot::RunTest(const FString&)
{
	using namespace CataclysmGatekeeperTest;
	using Gatekeeper_t = ACataclysmGatekeeperCharacter;

	// A ONE-SHOT IN SINGLE-NODE MODE PLAYS ONCE AND THEN HOLDS ITS LAST FRAME
	// FOR EVER. The creature is owed its idle back, and the thing that gives it
	// back is UpdateLoopingAnimation noticing the one-shot has finished.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmGatekeeperCharacter* Boss =
		SpawnGatekeeper(World, FVector::ZeroVector);
	if (!Boss)
	{
		AddError(TEXT("could not spawn a Gatekeeper"));
		return false;
	}

	if (!Boss->ResolveBody(/*bIncludeAnimation=*/true))
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Sevarog pack is not installed, so there are no "
				 "clips to play and nothing about the one-shot and the loop "
				 "was checked. See game/docs/enemy-source-assets.md."));
		return true;
	}

	if (Boss->bAnimationBlueprintBound)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("an animation Blueprint took the mesh, so the single-clip "
				 "fallback this test is about does not run."));
		return true;
	}

	Boss->UpdateLoopingAnimation();

	TestEqual(TEXT("standing still, it loops its idle"),
		Boss->CurrentLoopingAnimation.Get(), Boss->IdleAnimation.Get());

	// --- A SWEEP INTERRUPTS IT --------------------------------------------

	Boss->BeginEnemyAbilityWindUp(Gatekeeper_t::DreadCleaveAbility, nullptr);

	TestEqual(TEXT("winding up the sweep plays the sweep clip"),
		Boss->LastPlayedAnimation.Get(), Boss->CleaveAnimation.Get());

	TestNull(TEXT("and the loop is cleared, so something has to put it back"),
		Boss->CurrentLoopingAnimation.Get());

	TestTrue(*FString::Printf(
			TEXT("and the creature knows when the clip ends: %.4f s from now"),
			Boss->OneShotEndsAtSeconds - World->GetTimeSeconds()),
		Boss->OneShotEndsAtSeconds > World->GetTimeSeconds());

	// **THE CLIP KEEPS THE MESH UNTIL IT ENDS.** Asked part way through, the
	// creature must still be swinging rather than back in its idle.
	Boss->UpdateLoopingAnimation();

	TestNull(TEXT("part way through the swing, nothing has taken the mesh back"),
		Boss->CurrentLoopingAnimation.Get());

	// --- AND THEN IT IS OWED BACK -----------------------------------------

	AdvanceWorldClock(World, Gatekeeper_t::CleaveAnimationSeconds + 0.5);
	Boss->UpdateLoopingAnimation();

	TestEqual(TEXT("once the swing has finished, the idle is back"),
		Boss->CurrentLoopingAnimation.Get(), Boss->IdleAnimation.Get());

	// --- THE SUMMON PLAYS A DIFFERENT CLIP AND HAS NO WIND-UP TO FIT -------

	Boss->UseEnemyAbility(Gatekeeper_t::CallTheDamnedAbility, nullptr,
						  Boss->GetActorLocation());

	TestEqual(TEXT("the summon plays its own clip, from UseEnemyAbility rather "
				   "than from the wind-up hook, because it has no wind-up"),
		Boss->LastPlayedAnimation.Get(), Boss->CallAnimation.Get());

	return true;
}

#endif // WITH_AUTOMATION_TESTS
