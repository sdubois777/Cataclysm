// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimSequence.h"
#include "Character/CataclysmCorruptedSentinelCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
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
 * Tests for the Corrupted Sentinel, the fifth of the seven Demonic vertical
 * slice creatures and the only one that cannot move.
 *
 * WHAT THESE GUARD, and each is something the creature can fail at silently:
 *
 * 1. **NOT DEALING A FREE MELEE HIT AT FOURTEEN METRES.** This creature's reach
 *    is its SHOT's range, because that is the distance at which the brain stops
 *    walking and starts attacking. `ACataclysmEnemyCharacter::AttackTarget`
 *    applies direct damage at that reach, so a creature that did not override it
 *    would hit for a full weapon's worth at fourteen metres, through walls,
 *    every two seconds, on top of the bolt it already fired -- and nothing on
 *    screen would say where the damage came from.
 *    `ItsBasicAttackDealsNothingByItself` is that check.
 *
 * 2. THE MORTAR BEING REACHABLE AT ALL. Siege Bolt has a cooldown of zero and
 *    the same fourteen metre range, so if the array put it first the brain would
 *    choose it every time and the mortar would never fire. That is issue #491 on
 *    the Abyssal Warden with the numbers changed.
 *
 * 3. THE CREATURE STAYING PUT. Its designed speed is zero and
 *    `ACataclysmEnemyCharacter` never sets one, so a creature that stopped
 *    setting it would move at Unreal's default 600 and stop being a turret.
 *
 * 4. THE TWO SHOTS BEING DIFFERENT SHAPES. A flat bolt marks a lane and a lobbed
 *    shell marks a circle where it lands, and getting that backwards marks
 *    ground nothing will happen on. Issue #459.
 *
 * WHAT THESE DELIBERATELY DO NOT CHECK. Whether the muzzle flash agrees with the
 * moment the shot is dealt. Issue #478 is that the release moment inside
 * `Fire_Planted` has never been measured, and `tools/measure_sentinel_release.py`
 * records two ways of measuring it that both failed their control.
 */

namespace CataclysmSentinelTest
{
	using Sentinel_t = ACataclysmCorruptedSentinelCharacter;

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

	static ACataclysmCorruptedSentinelCharacter* SpawnSentinel(
		UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACataclysmCorruptedSentinelCharacter>(
			ACataclysmCorruptedSentinelCharacter::StaticClass(), Where,
			FRotator::ZeroRotator, Params);
	}

	/** Something on the players' side with health to lose. Spawned far away and
	 *  then moved, because two capsules created at contact distance push each
	 *  other apart. */
	static ACataclysmEnemyCharacter* SpawnTarget(UWorld* World,
												 const FVector& Where)
	{
		ACataclysmEnemyCharacter* Target =
			World->SpawnActor<ACataclysmEnemyCharacter>(
				ACataclysmEnemyCharacter::StaticClass(),
				FVector(30000.0f, 30000.0f, 0.0f), FRotator::ZeroRotator);
		if (!Target)
		{
			return nullptr;
		}
		Target->SetGenericTeamId(
			UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		Target->SetHealth(100000.0f);
		Target->SetActorLocation(Where);
		return Target;
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
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmSentinelCarriesItsDesignedProfile,
	"Cataclysm.Sentinel.ItCarriesItsDesignedProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSentinelCarriesItsDesignedProfile::RunTest(const FString&)
{
	using namespace CataclysmSentinelTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmCorruptedSentinelCharacter* Sentinel =
		SpawnSentinel(World, FVector::ZeroVector);
	if (!Sentinel)
	{
		AddError(TEXT("could not spawn a Corrupted Sentinel"));
		return false;
	}

	TestEqual(TEXT("it reaches as far as it shoots"),
		Sentinel->AttackReachCm(), Sentinel_t::SiegeBoltRangeCm);
	TestEqual(TEXT("it shoots on its designed interval"),
		Sentinel->SecondsBetweenAttacks(),
		Sentinel_t::DesignedAttackIntervalSeconds);
	TestEqual(TEXT("it notices as far as it can shoot, which no other creature "
				   "needs"),
		Sentinel->SightRadiusCm(), Sentinel_t::SiegeBoltRangeCm);

	// **IT DOES NOT ROAM, AND EVERY OTHER CREATURE DOES.** A roam radius on a
	// creature that cannot walk is a destination it can never reach.
	TestEqual(TEXT("it does not wander, because it cannot"),
		Sentinel->RoamRadiusCm(), 0.0f);

	TestNotEqual(TEXT("its reach is not the base enemy's"),
		Sentinel->AttackReachCm(), 200.0f);
	TestNotEqual(TEXT("its interval is not the base enemy's"),
		Sentinel->SecondsBetweenAttacks(), 1.5f);

	const UCapsuleComponent* Capsule = Sentinel->GetCapsuleComponent();
	TestEqual(TEXT("its capsule is its designed radius"),
		Capsule->GetScaledCapsuleRadius(), Sentinel_t::SentinelCapsuleRadius);
	TestEqual(TEXT("its capsule is its designed half-height"),
		Capsule->GetScaledCapsuleHalfHeight(),
		Sentinel_t::SentinelCapsuleHalfHeight);

	const UCharacterMovementComponent* Movement =
		Sentinel->GetCharacterMovement();

	// **THE ONE THAT MAKES IT THIS CREATURE.** `ACataclysmEnemyCharacter` never
	// sets MaxWalkSpeed, so a creature that stopped setting it would move at
	// Unreal's default 600 cm/s -- faster than two of the three player classes
	// -- and a turret would quietly become a melee enemy.
	TestEqual(TEXT("it cannot move at all"), Movement->MaxWalkSpeed, 0.0f);
	TestNotEqual(TEXT("and that is not Unreal's default, which is what a "
					  "creature that forgot to set one would have"),
		Movement->MaxWalkSpeed, 600.0f);

	// BUT IT CAN STILL TURN, which is what lets a rooted creature track a
	// player who circles it.
	TestEqual(TEXT("it turns at its designed rate even though it cannot walk"),
		Movement->RotationRate.Yaw,
		static_cast<double>(Sentinel_t::DesignedTurnRateDegreesPerSecond));
	TestTrue(TEXT("and that rate is not zero"),
		Movement->RotationRate.Yaw > 0.0);

	// AND IT CARRIES AN ENERGY SHIELD, which no creature built before it does.
	// Read off the live attribute rather than off the constant, because the
	// constant reaching the attribute is the half a header cannot show.
	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Sentinel);
	if (!AbilitySystem)
	{
		AddError(TEXT("the creature has no ability system"));
		return false;
	}

	const float Shield = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetEnergyShieldAttribute());

	// AGAINST MAXIMUM HEALTH, WHICH IS WHAT THE SHIELD IS A FRACTION OF.
	// `ApplyStartingAttributes` reads MaxHealth, not the current value, so
	// comparing against the current one would agree at spawn and stop agreeing
	// the moment anything hurt the creature.
	const float MaxHealth = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute());

	TestTrue(FString::Printf(
			TEXT("it carries an energy shield of %.1f against %.1f maximum "
				 "health"),
			Shield, MaxHealth),
		Shield > 0.0f);
	TestTrue(FString::Printf(
			TEXT("and the shield is its designed fraction of that health: "
				 "%.4f against a designed %.4f"),
			MaxHealth > 0.0f ? Shield / MaxHealth : -1.0f,
			Sentinel_t::DesignedEnergyShieldFraction),
		MaxHealth > 0.0f
		&& FMath::Abs(Shield / MaxHealth
					  - Sentinel_t::DesignedEnergyShieldFraction) < 0.01f);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmSentinelOffersItsMortarThenItsBolt,
	"Cataclysm.Sentinel.ItOffersItsMortarThenItsBolt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSentinelOffersItsMortarThenItsBolt::RunTest(const FString&)
{
	using namespace CataclysmSentinelTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmCorruptedSentinelCharacter* Sentinel =
		SpawnSentinel(World, FVector::ZeroVector);
	if (!Sentinel)
	{
		AddError(TEXT("could not spawn a Corrupted Sentinel"));
		return false;
	}

	const TArray<FCataclysmEnemyAbility> Abilities = Sentinel->EnemyAbilities();
	if (Abilities.Num() != 2)
	{
		AddError(FString::Printf(
			TEXT("the Corrupted Sentinel offers %d abilities and its design "
				 "gives it two, Siege Bolt and Brimstone Mortar."),
			Abilities.Num()));
		return false;
	}

	const FCataclysmEnemyAbility& Mortar =
		Abilities[Sentinel_t::BrimstoneMortarAbility];
	const FCataclysmEnemyAbility& Bolt = Abilities[Sentinel_t::SiegeBoltAbility];

	// **THE ORDER, WHICH IS THE WHOLE OF ISSUE #491 APPLIED HERE.**
	// `ChooseAbility` takes the first entry whose range and cooldown fit. Siege
	// Bolt has a cooldown of zero and the same fourteen metre range, so a bolt
	// listed first is the only thing this creature would ever do.
	TestEqual(TEXT("the mortar comes first"),
		Mortar.Name, FName(TEXT("Brimstone Mortar")));
	TestEqual(TEXT("the bolt comes second"),
		Bolt.Name, FName(TEXT("Siege Bolt")));
	TestTrue(TEXT("and the one that comes second is the one with no cooldown, "
				  "which is what makes it the basic attack rather than what "
				  "crowds the other out"),
		Bolt.CooldownSeconds <= 0.0f && Mortar.CooldownSeconds > 0.0f);

	// BOTH ARE PROJECTILES, AND ONE LOBS. A flat shot marks a lane along the
	// way; a lobbed one marks a circle where it lands, because it flies over
	// everything between. Issue #459.
	TestTrue(TEXT("the bolt is a projectile"),
		Bolt.Shape == ECataclysmSkillShape::Projectile);
	TestTrue(TEXT("the mortar is a projectile too"),
		Mortar.Shape == ECataclysmSkillShape::Projectile);
	TestFalse(TEXT("the bolt travels flat, so its marker is a lane"),
		Bolt.bArcsOntoItsTarget);
	TestTrue(TEXT("the mortar lobs, so its marker is a circle where it lands"),
		Mortar.bArcsOntoItsTarget);

	TestEqual(TEXT("the bolt reaches its designed range"),
		Bolt.MaxRangeCm, Sentinel_t::SiegeBoltRangeCm);
	TestEqual(TEXT("and marks a lane of its designed half-width"),
		Bolt.MarkerRadiusCm, Sentinel_t::SiegeBoltRadiusCm);
	TestEqual(TEXT("and warns for its designed telegraph"),
		Bolt.WindUpSeconds, Sentinel_t::SiegeBoltWindUpSeconds);

	// THE TELEGRAPH IS EXACTLY HALF THE INTERVAL, which is the most the rule
	// allows and is what "it uses the whole of its allowance" means.
	TestEqual(TEXT("the bolt's telegraph is exactly half its attack interval"),
		Bolt.WindUpSeconds,
		Sentinel_t::DesignedAttackIntervalSeconds / 2.0f);

	// AND IT STILL LEAVES ROOM. A telegraph as long as the interval would mean
	// the creature began warning about the next shot before this one landed.
	TestTrue(TEXT("and it is shorter than the interval, so the marker leaves "
				  "the ground between shots"),
		Bolt.WindUpSeconds < Sentinel_t::DesignedAttackIntervalSeconds);

	TestEqual(TEXT("the mortar reaches its designed range"),
		Mortar.MaxRangeCm, Sentinel_t::BrimstoneMortarRangeCm);
	TestEqual(TEXT("and marks a circle of its designed radius"),
		Mortar.MarkerRadiusCm, Sentinel_t::BrimstoneMortarRadiusCm);
	TestEqual(TEXT("and warns for its designed telegraph"),
		Mortar.WindUpSeconds, Sentinel_t::BrimstoneMortarWindUpSeconds);
	TestEqual(TEXT("its cooldown is the one really in use, so a console "
				   "override reaches the brain without a rebuild"),
		Mortar.CooldownSeconds,
		Sentinel_t::BrimstoneMortarCooldownSecondsInUse());

	// AND THE MORTAR REFUSES A TARGET STANDING ON TOP OF IT, because below
	// `radius + own body` the circle it marks covers its own feet. Issue #475.
	TestEqual(TEXT("the mortar refuses anything closer than its own blast"),
		Mortar.MinRangeCm, Sentinel_t::BrimstoneMortarMinimumRangeCm);
	TestTrue(TEXT("and that distance is still inside its range, so the ability "
				  "can be used at all"),
		Mortar.MinRangeCm < Mortar.MaxRangeCm);

	// THE BOLT HAS NO MINIMUM, and that is not an oversight: a lane starts at
	// the creature whatever the distance, so there is no circle covering its
	// own feet to avoid.
	TestEqual(TEXT("the bolt has no minimum range, because a lane starts at the "
				   "creature however far it goes"),
		Bolt.MinRangeCm, 0.0f);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmSentinelBasicAttackDealsNothingByItself,
	"Cataclysm.Sentinel.ItsBasicAttackDealsNothingByItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSentinelBasicAttackDealsNothingByItself::RunTest(const FString&)
{
	using namespace CataclysmSentinelTest;

	// **WHAT THIS EXISTS FOR.** `ACataclysmEnemyCharacter::AttackTarget` applies
	// direct damage, and `ACataclysmEnemyController::Think` calls it whenever a
	// target is inside `AttackReachCm`. This creature's reach is its SHOT's
	// range of fourteen metres, because that is where the brain has to stop
	// walking and start attacking for a creature that cannot walk.
	//
	// So a Corrupted Sentinel that did not override AttackTarget would deal a
	// full weapon's worth of melee damage at fourteen metres, through walls,
	// every two seconds, ON TOP of the bolt it already fired -- and nothing on
	// screen would say where it came from. Its basic attack is Siege Bolt, which
	// is an entry in EnemyAbilities because it is telegraphed.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmCorruptedSentinelCharacter* Sentinel =
		SpawnSentinel(World, FVector::ZeroVector);
	if (!Sentinel)
	{
		AddError(TEXT("could not spawn a Corrupted Sentinel"));
		return false;
	}
	Sentinel->SetAttackDamage(100.0f);

	// STANDING WELL INSIDE ITS REACH, which is the whole point: at this distance
	// the base class's melee hit would land.
	ACataclysmEnemyCharacter* Target =
		SpawnTarget(World, FVector(500.0f, 0.0f, 0.0f));
	if (!Target)
	{
		AddError(TEXT("could not spawn something to shoot at"));
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
	TestTrue(FString::Printf(
			TEXT("the target stands %.0f cm away, inside the creature's %.0f cm "
				 "reach"),
			500.0f, Sentinel->AttackReachCm()),
		Sentinel->AttackReachCm() > 500.0f);

	Sentinel->AttackTarget(Target);

	TestEqual(TEXT("the basic attack path deals nothing at all"),
		HealthOf(Target), Before);

	// AND NOTHING WAS FIRED EITHER. AttackTarget is not where the bolt comes
	// from; the ability is.
	TestNull(TEXT("and it fires nothing either, because the bolt is an ability"),
		Sentinel->LastShotFired.Get());

	// THE CONTROL. A plain enemy at the same distance with the same damage DOES
	// hurt the target, which is what says the check above is about this
	// creature's override rather than about the damage pipeline being asleep.
	ACataclysmEnemyCharacter* Ordinary =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(),
			FVector(-500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!Ordinary)
	{
		AddError(TEXT("could not spawn an ordinary enemy for the control"));
		return false;
	}
	Ordinary->SetAttackDamage(100.0f);
	Ordinary->AttackTarget(Target);

	TestTrue(FString::Printf(
			TEXT("but an ordinary enemy's basic attack does hurt it: %.1f to "
				 "%.1f"),
			Before, HealthOf(Target)),
		HealthOf(Target) < Before);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmSentinelFiresTheRightShotForEachAbility,
	"Cataclysm.Sentinel.EachAbilityFiresTheShapeOfShotItMarked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSentinelFiresTheRightShotForEachAbility::RunTest(const FString&)
{
	using namespace CataclysmSentinelTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmCorruptedSentinelCharacter* Sentinel =
		SpawnSentinel(World, FVector::ZeroVector);
	if (!Sentinel)
	{
		AddError(TEXT("could not spawn a Corrupted Sentinel"));
		return false;
	}
	Sentinel->SetAttackDamage(100.0f);

	const FVector Aim =
		Sentinel->GetActorLocation() + FVector(1000.0f, 0.0f, 0.0f);

	// THE BOLT FIRST.
	Sentinel->UseEnemyAbility(Sentinel_t::SiegeBoltAbility, nullptr, Aim);

	ACataclysmProjectile* Bolt = Sentinel->LastShotFired.Get();
	if (!IsValid(Bolt))
	{
		AddError(TEXT("Siege Bolt fired nothing at all"));
		return false;
	}

	// **RadiusCm, NOT BodyRadiusCm, AND THE TWO ARE DIFFERENT THINGS.**
	// `RadiusCm` is what the skill's Radius parameter means -- the half-width it
	// hits along for a piercing shot, or the blast where it stops for one that
	// does not. `BodyRadiusCm` is how wide the flying object itself is, and it
	// stays at the projectile's own 40 cm default unless the shot pierces. An
	// earlier draft of this test asserted the marked radius against
	// `BodyRadiusCm` and failed, reading 40 against 210.
	TestEqual(TEXT("the bolt hits across the lane that was marked"),
		Bolt->RadiusCm, Sentinel_t::SiegeBoltRadiusCm);
	TestEqual(TEXT("and the thing in the air is the ordinary width, because it "
				   "does not pierce"),
		Bolt->BodyRadiusCm, ACataclysmProjectile::DefaultBodyRadiusCm);

	// FLAT, WHICH IS A SPEED AND NO FLIGHT TIME. A projectile given a flight
	// time follows a parabola onto its target and ignores the speed entirely.
	TestEqual(TEXT("it travels flat at its designed speed"),
		Bolt->SpeedCmPerSecond, Sentinel_t::SiegeBoltSpeedCmPerSecond);
	TestEqual(TEXT("and it is not lobbed"), Bolt->FlightSeconds, 0.0f);

	// THEN THE MORTAR.
	Sentinel->UseEnemyAbility(Sentinel_t::BrimstoneMortarAbility, nullptr, Aim);

	ACataclysmProjectile* Shell = Sentinel->LastShotFired.Get();
	if (!IsValid(Shell) || Shell == Bolt)
	{
		AddError(TEXT("Brimstone Mortar fired nothing, or fired the same "
					  "projectile the bolt did"));
		return false;
	}

	TestEqual(TEXT("the shell bursts as wide as the circle that was marked"),
		Shell->RadiusCm, Sentinel_t::BrimstoneMortarRadiusCm);
	TestEqual(TEXT("and the shell in the air is the ordinary width, because it "
				   "does not pierce either"),
		Shell->BodyRadiusCm, ACataclysmProjectile::DefaultBodyRadiusCm);

	// LOBBED, WHICH IS A FLIGHT TIME AND NO SPEED. Issue #465: a ballistic shot
	// has no single speed, so it is given a time and works the rest out.
	TestTrue(FString::Printf(
			TEXT("it is lobbed, spending %.3f s in the air"),
			Shell->FlightSeconds),
		Shell->FlightSeconds > 0.0f);

	// AND THE FLIGHT TIME IS THE ARC, RECOMPUTED. A parabola sags g t^2 / 8
	// below its chord, so a sag of `Arc x range` is in the air for
	// sqrt(8 x Arc x range / g).
	const float RangeCm =
		static_cast<float>(FVector::Dist2D(Sentinel->GetActorLocation(), Aim));
	const float Expected = FMath::Sqrt(
		8.0f * Sentinel_t::BrimstoneMortarApexFraction * RangeCm
		/ ACataclysmProjectile::LobGravityCmPerSecondSquared);

	TestTrue(FString::Printf(
			TEXT("its flight time is the designed arc over %.0f cm: %.4f s "
				 "against %.4f"),
			RangeCm, Shell->FlightSeconds, Expected),
		FMath::Abs(Shell->FlightSeconds - Expected) < 0.01f);

	// AND A SHORTER LOB IS IN THE AIR FOR LESS TIME, which is the whole reason
	// the design states a fraction rather than a time. Issue #474: a stated time
	// made every short throw a near-vertical mortar.
	const FVector Nearer =
		Sentinel->GetActorLocation() + FVector(400.0f, 0.0f, 0.0f);
	TestTrue(TEXT("a shorter lob spends less time in the air than a longer one"),
		Sentinel->BrimstoneMortarFlightSecondsFor(Nearer)
			< Sentinel->BrimstoneMortarFlightSecondsFor(Aim));

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmSentinelWearsItsMeshAndHidesThePlaceholder,
	"Cataclysm.Sentinel.ItWearsItsMeshAndHidesThePlaceholder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSentinelWearsItsMeshAndHidesThePlaceholder::RunTest(const FString&)
{
	using namespace CataclysmSentinelTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasNotBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmCorruptedSentinelCharacter* Sentinel =
		SpawnSentinel(World, FVector::ZeroVector);
	if (!Sentinel)
	{
		AddError(TEXT("could not spawn a Corrupted Sentinel"));
		return false;
	}

	if (!Sentinel->PlaceholderBody)
	{
		AddError(TEXT("the enemy base no longer creates a PlaceholderBody, so "
					  "there is nothing to hide and this check is meaningless"));
		return false;
	}

	TestTrue(TEXT("the placeholder cylinder starts visible"),
		Sentinel->PlaceholderBody->IsVisible());

	const bool bDressed = Sentinel->ResolveBody(/*bIncludeAnimation=*/true);

	TestEqual(
		TEXT("the placeholder is hidden exactly when the real mesh resolved"),
		Sentinel->PlaceholderBody->IsVisible(), !bDressed);

	if (!bDressed)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Minions pack is not present, so what was checked "
				 "is that the placeholder is KEPT rather than that it is "
				 "hidden, and neither the mesh nor any of its clips was "
				 "loaded."));
		return true;
	}

	USkeletalMeshComponent* MeshComponent = Sentinel->GetMesh();
	if (!MeshComponent)
	{
		AddError(TEXT("the creature has no skeletal mesh component"));
		return false;
	}

	TestNotNull(TEXT("it is wearing a skeletal mesh"),
		MeshComponent->GetSkeletalMeshAsset());
	TestEqual(TEXT("the mesh is dropped so its feet are on the capsule bottom"),
		MeshComponent->GetRelativeLocation().Z,
		static_cast<double>(-Sentinel_t::SentinelCapsuleHalfHeight));
	TestEqual(TEXT("and yawed the engine's -90 degrees for a character mesh"),
		MeshComponent->GetRelativeRotation().Yaw, -90.0);

	TestNotNull(TEXT("its rooted idle loaded"), Sentinel->IdleAnimation.Get());

	TestEqual(TEXT("it loaded both firing clips it alternates between"),
		Sentinel->FireAnimations.Num(), Sentinel_t::FireAnimationCount);
	TestEqual(TEXT("and every way it has of falling over"),
		Sentinel->DeathAnimations.Num(), Sentinel_t::DeathAnimationCount);

	// EVERY DEATH CLIP IS INSIDE THE TIME THE BODY IS KEPT, or the corpse is
	// removed part way through the fall. Eight of them is the most in the
	// project, so this is the creature where one long clip could hide.
	for (const TObjectPtr<UAnimSequence>& Clip : Sentinel->DeathAnimations)
	{
		if (!Clip)
		{
			AddError(TEXT("a death clip failed to load"));
			continue;
		}
		TestTrue(FString::Printf(
				TEXT("%s is %.4f s, inside the 4 seconds a corpse is kept"),
				*Clip->GetName(), Clip->GetPlayLength()),
			Clip->GetPlayLength() < 4.0f);
	}

	// THE ROOTED IDLE IS ONE POSE, which is what makes a rooted state cheap to
	// hold open for a variable time. The pack's other idle is 7.4 seconds and is
	// the creature standing up, which this one never does.
	if (const UAnimSequence* Idle = Sentinel->IdleAnimation.Get())
	{
		TestTrue(FString::Printf(
				TEXT("its rooted idle is a single pose, %.4f s long"),
				Idle->GetPlayLength()),
			Idle->GetPlayLength() < 0.1f);
	}

	// AND THE FIRING CLIP REALLY IS LONGER THAN THE INTERVAL, which is what
	// makes this the only creature whose attack clip has to be sped up.
	for (const TObjectPtr<UAnimSequence>& Clip : Sentinel->FireAnimations)
	{
		if (!Clip)
		{
			AddError(TEXT("a firing clip failed to load"));
			continue;
		}
		TestTrue(FString::Printf(
				TEXT("%s is %.4f s and the header records %.4f"),
				*Clip->GetName(), Clip->GetPlayLength(),
				Sentinel_t::FireAnimationSeconds),
			FMath::Abs(Clip->GetPlayLength()
					   - Sentinel_t::FireAnimationSeconds) < 0.01f);
	}

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmSentinelAlternatesItsFiringClips,
	"Cataclysm.Sentinel.ItAlternatesItsTwoFiringClipsAndReturnsToItsRootedPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSentinelAlternatesItsFiringClips::RunTest(const FString&)
{
	using namespace CataclysmSentinelTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmCorruptedSentinelCharacter* Sentinel =
		SpawnSentinel(World, FVector::ZeroVector);
	if (!Sentinel)
	{
		AddError(TEXT("could not spawn a Corrupted Sentinel"));
		return false;
	}

	const bool bDressed = Sentinel->ResolveBody(/*bIncludeAnimation=*/true);
	if (!bDressed || !Sentinel->IdleAnimation
		|| Sentinel->FireAnimations.IsEmpty())
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Minions pack is not present, so there are no "
				 "clips to alternate between. Neither the alternation nor the "
				 "return to the rooted pose was checked on this machine."));
		return true;
	}

	// ROOTED TO BEGIN WITH.
	Sentinel->UpdateLoopingAnimation();
	TestEqual(TEXT("it holds its rooted pose before anything happens"),
		Sentinel->CurrentLoopingAnimation.Get(),
		Sentinel->IdleAnimation.Get());

	// FOUR SHOTS, AND THE CLIP HAS TO CHANGE EVERY TIME. Alternating rather
	// than drawing is the point: two clips drawn at random repeat about half the
	// time, which is the "one clip looping" the pack's two firing clips exist to
	// avoid.
	const UAnimSequence* Previous = nullptr;
	for (int32 Shot = 0; Shot < 4; ++Shot)
	{
		Sentinel->OneShotEndsAtSeconds = 0.0f;
		Sentinel->BeginEnemyAbilityWindUp(Sentinel_t::SiegeBoltAbility, nullptr);

		const UAnimSequence* Played = Sentinel->LastPlayedAnimation.Get();
		if (!Played)
		{
			AddError(FString::Printf(
				TEXT("shot %d played no clip at all"), Shot));
			return false;
		}
		TestTrue(TEXT("the clip it played is one of its firing clips"),
			Sentinel->FireAnimations.Contains(Sentinel->LastPlayedAnimation));

		if (Previous)
		{
			TestNotEqual(FString::Printf(
					TEXT("shot %d plays a different clip from shot %d"),
					Shot, Shot - 1),
				Played, Previous);
		}
		Previous = Played;
	}

	// AND THE SHOT TAKES THE MESH UNTIL THE INTERVAL IS UP. The clip is 2.40
	// seconds played at 1.20 into a 2.00 second interval, so it finishes exactly
	// as the next shot begins and there is no gap to fill.
	TestTrue(TEXT("a shot records when it will finish"),
		Sentinel->OneShotEndsAtSeconds > World->GetTimeSeconds());
	TestNull(TEXT("and nothing is looping while it plays"),
		Sentinel->CurrentLoopingAnimation.Get());

	Sentinel->UpdateLoopingAnimation();
	TestNull(TEXT("the shot is left alone until it ends"),
		Sentinel->CurrentLoopingAnimation.Get());

	Sentinel->OneShotEndsAtSeconds = 0.0f;
	Sentinel->UpdateLoopingAnimation();
	TestEqual(TEXT("and the rooted pose comes back once it has"),
		Sentinel->CurrentLoopingAnimation.Get(),
		Sentinel->IdleAnimation.Get());

	// THE PLAY RATE IS THE ONE ISSUE #369 SETTLED: the clip is played to fit the
	// interval, which for 2.40 into 2.00 is 1.20.
	const float Expected = Sentinel_t::FireAnimationSeconds
						 / Sentinel_t::DesignedAttackIntervalSeconds;
	TestEqual(TEXT("the firing clip is played to fit the attack interval"),
		Sentinel_t::FirePlayRate(), Expected);
	TestTrue(FString::Printf(
			TEXT("and %.3f is inside the %.1f ceiling"),
			Sentinel_t::FirePlayRate(), Sentinel_t::MaximumPlayRate),
		Sentinel_t::FirePlayRate() < Sentinel_t::MaximumPlayRate);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
