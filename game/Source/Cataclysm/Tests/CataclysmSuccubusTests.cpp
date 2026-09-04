// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

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
#include "Character/CataclysmSuccubusCharacter.h"
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
 * Tests for the Succubus, the sixth of the seven Demonic vertical slice
 * creatures and the only one that makes the others stronger.
 *
 * WHAT THESE GUARD, and each is something the creature can fail at silently:
 *
 * 1. **THE AURA BUFFING THE RIGHT SIDE.** Dominion is the only thing in the game
 *    that helps a creature rather than hurting one, so it is the only thing that
 *    searches for ALLIES. `FindEnemiesInSphere` and `FindAlliesInSphere` differ
 *    by one word and getting it wrong would grant the player 20% increased stats
 *    from the creature that is trying to kill them. `ItsAuraBuffsAlliesAndNotThePlayer`
 *    is that check.
 *
 * 2. **THE AURA ENDING WHEN THE CREATURE DIES.** The design document's whole
 *    claim about this creature is "killing it first is the correct play, and
 *    this is what makes that true". A buff that outlived its caster by even a
 *    second would make that false, and nothing on screen would say so, because a
 *    buffed creature looks exactly like an unbuffed one.
 *
 * 3. NOT DEALING A FREE MELEE HIT AT EIGHT METRES. This creature's reach is its
 *    SHOT's range, because that is where the brain stops walking and starts
 *    attacking. `ACataclysmEnemyCharacter::AttackTarget` applies direct damage at
 *    that reach, so a creature that did not override it would hit for a full
 *    weapon's worth at eight metres, through walls, every 2.6 seconds, on top of
 *    the bolt it already fired. The Corrupted Sentinel found this first.
 *
 * 4. THE CURSE BEING REACHABLE AT ALL. Soulfire has a cooldown of zero and the
 *    same eight metre range, so if the array put it first the brain would choose
 *    it every time and Wither the Living would never be cast. That is issue #491
 *    on the Abyssal Warden with the numbers changed.
 *
 * WHAT A BUFFED ALLY ACTUALLY GAINS is 20% more movement speed and 20% more
 * attack speed, and nothing else. The project owner set those two on
 * 2026-08-20; the design had said only "20% increased stats", which named
 * none. The arithmetic is checked in CataclysmEnemyCommanderTests.cpp rather
 * than here, because what the tag DOES belongs to every creature and not to
 * the one that grants it.
 */

namespace CataclysmSuccubusTest
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

	static ACataclysmSuccubusCharacter* SpawnSuccubus(UWorld* World,
													  const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACataclysmSuccubusCharacter>(
			ACataclysmSuccubusCharacter::StaticClass(), Where,
			FRotator::ZeroRotator, Params);
	}

	/** Something with health to lose, on whichever side is asked for. Spawned
	 *  far away and then moved, because two capsules created at contact
	 *  distance push each other apart. */
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

	/** Move the world clock forward without ticking anything.
	 *
	 *  A world built by UWorld::CreateWorld is never ticked, so its clock
	 *  never moves and nothing that waits can finish. Everything under test
	 *  here reads the clock through GetTimeSeconds and nothing else, so
	 *  moving the clock is the whole of what "time passed" means to it. The
	 *  same helper is in CataclysmEnemyBehaviourTests.cpp, where the reasoning
	 *  is written out in full. */
	static void AdvanceWorldClock(UWorld* World, double Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	static FGameplayTag CommanderTag()
	{
		return UCataclysmSkillShapes::StatusTagFor(
			ACataclysmSuccubusCharacter::DominionEffectName);
	}

	static FGameplayTag WitheredTouchTag()
	{
		return UCataclysmSkillShapes::StatusTagFor(
			ACataclysmSuccubusCharacter::WitherEffectName);
	}
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmSuccubusCarriesItsDesignedProfile,
	"Cataclysm.Succubus.ItCarriesItsDesignedProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSuccubusCarriesItsDesignedProfile::RunTest(const FString&)
{
	using namespace CataclysmSuccubusTest;
	using Succubus_t = ACataclysmSuccubusCharacter;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmSuccubusCharacter* Succubus =
		SpawnSuccubus(World, FVector::ZeroVector);
	if (!Succubus)
	{
		AddError(TEXT("could not spawn a Succubus"));
		return false;
	}

	TestEqual(TEXT("seconds between shots"), Succubus->SecondsBetweenAttacks(),
		Succubus_t::DesignedAttackIntervalSeconds);

	// ITS REACH IS ITS SHOT'S RANGE, which is what makes the AttackTarget
	// override below necessary rather than tidy.
	TestEqual(TEXT("its reach is Soulfire's range"), Succubus->AttackReachCm(),
		Succubus_t::SoulfireRangeCm);

	TestEqual(TEXT("it notices as far as the four creatures that can walk do"),
		Succubus->SightRadiusCm(), Succubus_t::SuccubusNoticeRadiusCm);

	// IT CAN WALK, unlike the Corrupted Sentinel. A creature whose speed was
	// left unset moves at Unreal's default 600, which would make this the
	// fastest-moving caster in the game rather than the slowest.
	TestEqual(TEXT("it walks at its designed speed"),
		Succubus->GetCharacterMovement()->MaxWalkSpeed,
		Succubus_t::DesignedWalkSpeedCmPerSecond);

	TestEqual(TEXT("it turns at its designed rate"),
		static_cast<float>(Succubus->GetCharacterMovement()->RotationRate.Yaw),
		Succubus_t::DesignedTurnRateDegreesPerSecond);

	TestEqual(TEXT("the capsule's radius is the designed body radius"),
		Succubus->GetCapsuleComponent()->GetUnscaledCapsuleRadius(),
		Succubus_t::SuccubusCapsuleRadius);

	TestEqual(TEXT("the capsule's half-height comes from the mesh"),
		Succubus->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),
		Succubus_t::SuccubusCapsuleHalfHeight);

	// THE LARGEST ENERGY SHIELD IN THE ROSTER, at half its own health. It sits
	// in front of health and is not reduced by armour, which for a creature with
	// an armour share of 0.20 is most of what killing it costs.
	TestEqual(TEXT("half its health again is an energy shield"),
		Succubus->EnergyShieldFraction,
		Succubus_t::DesignedEnergyShieldFraction);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmSuccubusOffersItsCurseThenItsBolt,
	"Cataclysm.Succubus.ItOffersItsCurseThenItsBolt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSuccubusOffersItsCurseThenItsBolt::RunTest(const FString&)
{
	using namespace CataclysmSuccubusTest;
	using Succubus_t = ACataclysmSuccubusCharacter;

	// **WHAT THIS EXISTS FOR.** `ACataclysmEnemyController::ChooseAbility` takes
	// the FIRST entry whose range and cooldown fit and never looks at the shape.
	// Soulfire has a cooldown of zero and the same eight metre range as the
	// curse, so a Soulfire at the front of the array would be the only thing
	// this creature ever did. Issue #491 is that defect on the Abyssal Warden.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmSuccubusCharacter* Succubus =
		SpawnSuccubus(World, FVector::ZeroVector);
	if (!Succubus)
	{
		AddError(TEXT("could not spawn a Succubus"));
		return false;
	}

	const TArray<FCataclysmEnemyAbility> Abilities = Succubus->EnemyAbilities();

	if (Abilities.Num() != 2)
	{
		AddError(FString::Printf(
			TEXT("the Succubus offers %d abilities and it should offer two: the "
				 "curse and the bolt. Dominion is NOT one of them -- it is held "
				 "on for as long as the creature lives and must not compete for "
				 "the attack interval."),
			Abilities.Num()));
		return false;
	}

	TestEqual(TEXT("the curse is first, so the zero-cooldown bolt cannot crowd "
				   "it out"),
		Abilities[Succubus_t::WitherTheLivingAbility].Name,
		FName(TEXT("Wither the Living")));

	TestEqual(TEXT("and the bolt is second"),
		Abilities[Succubus_t::SoulfireAbility].Name, FName(TEXT("Soulfire")));

	// THE BOLT HAS NO COOLDOWN, WHICH IS WHAT MAKES IT THE BASIC ATTACK, and it
	// is also exactly why the order above matters.
	TestEqual(TEXT("the bolt has no cooldown of its own"),
		Abilities[Succubus_t::SoulfireAbility].CooldownSeconds, 0.0f);

	TestEqual(TEXT("and the curse does"),
		Abilities[Succubus_t::WitherTheLivingAbility].CooldownSeconds,
		Succubus_t::WitherCooldownSeconds);

	// **NEITHER SHAPE IS ONE ANY ENEMY HAD USED BEFORE.** A shape the array does
	// not set reads as None, which draws no marker and, for the curse, would be
	// indistinguishable from what it correctly does -- so the bolt's Projectile
	// is what proves the field is being set at all.
	TestEqual(TEXT("the curse is a Debuff"),
		Abilities[Succubus_t::WitherTheLivingAbility].Shape,
		ECataclysmSkillShape::Debuff);

	TestEqual(TEXT("the bolt is a Projectile"),
		Abilities[Succubus_t::SoulfireAbility].Shape,
		ECataclysmSkillShape::Projectile);

	// THE CURSE MARKS NO GROUND, AND THE BOLT MARKS A LANE. There is no ground
	// for a curse to be drawn on, which is the design document's own sentence.
	TestEqual(TEXT("the curse draws no marker at all"),
		Abilities[Succubus_t::WitherTheLivingAbility].MarkerRadiusCm, 0.0f);

	TestEqual(TEXT("and it has no wind-up either, because there would be "
				   "nothing on screen to explain the pause"),
		Abilities[Succubus_t::WitherTheLivingAbility].WindUpSeconds, 0.0f);

	TestEqual(TEXT("the bolt marks a lane the width of its own radius"),
		Abilities[Succubus_t::SoulfireAbility].MarkerRadiusCm,
		Succubus_t::SoulfireRadiusCm);

	TestFalse(TEXT("and the bolt travels flat rather than arcing, so the marker "
				   "is a lane rather than a circle"),
		Abilities[Succubus_t::SoulfireAbility].bArcsOntoItsTarget);

	TestEqual(TEXT("the bolt's wind-up is exactly half the attack interval"),
		Abilities[Succubus_t::SoulfireAbility].WindUpSeconds,
		Succubus_t::DesignedAttackIntervalSeconds / 2.0f);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmSuccubusBasicAttackDealsNothingByItself,
	"Cataclysm.Succubus.ItsBasicAttackDealsNothingByItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSuccubusBasicAttackDealsNothingByItself::RunTest(const FString&)
{
	using namespace CataclysmSuccubusTest;

	// **WHAT THIS EXISTS FOR.** `ACataclysmEnemyCharacter::AttackTarget` applies
	// direct damage, and `ACataclysmEnemyController::Think` calls it whenever a
	// target is inside `AttackReachCm`. This creature's reach is its SHOT's
	// range of eight metres.
	//
	// So a Succubus that did not override AttackTarget would deal a full
	// weapon's worth of melee damage at eight metres, through walls, every 2.6
	// seconds, ON TOP of the bolt it already fired -- and its damage share of
	// 1.60 is the second highest in the slice, so that hit would be the largest
	// unexplained number in the game.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmSuccubusCharacter* Succubus =
		SpawnSuccubus(World, FVector::ZeroVector);
	if (!Succubus)
	{
		AddError(TEXT("could not spawn a Succubus"));
		return false;
	}
	Succubus->SetAttackDamage(100.0f);

	// STANDING WELL INSIDE ITS REACH, which is the whole point: at this distance
	// the base class's melee hit would land.
	ACataclysmEnemyCharacter* Target =
		SpawnOn(World, ECataclysmTeam::Players, FVector(400.0f, 0.0f, 0.0f));
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
			TEXT("the target stands 400 cm away, inside the creature's %.0f cm "
				 "reach"),
			Succubus->AttackReachCm()),
		Succubus->AttackReachCm() > 400.0f);

	Succubus->AttackTarget(Target);

	TestEqual(TEXT("the basic attack path deals nothing at all"),
		HealthOf(Target), Before);

	TestNull(TEXT("and it fires nothing either, because the bolt is an ability"),
		Succubus->LastShotFired.Get());

	// THE CONTROL. A plain enemy at the same distance with the same damage DOES
	// hurt the target, which is what says the check above is about this
	// creature's override rather than about the damage pipeline being asleep.
	ACataclysmEnemyCharacter* Ordinary =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(),
			FVector(-400.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
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
	FCataclysmSuccubusFiresTheRightShotAndCastsTheRightCurse,
	"Cataclysm.Succubus.ItFiresTheRightShotAndCastsTheRightCurse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSuccubusFiresTheRightShotAndCastsTheRightCurse::RunTest(
	const FString&)
{
	using namespace CataclysmSuccubusTest;
	using Succubus_t = ACataclysmSuccubusCharacter;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmSuccubusCharacter* Succubus =
		SpawnSuccubus(World, FVector::ZeroVector);
	if (!Succubus)
	{
		AddError(TEXT("could not spawn a Succubus"));
		return false;
	}

	ACataclysmEnemyCharacter* Target =
		SpawnOn(World, ECataclysmTeam::Players, FVector(600.0f, 0.0f, 0.0f));
	if (!Target)
	{
		AddError(TEXT("could not spawn something to shoot at"));
		return false;
	}

	// --- Soulfire ---------------------------------------------------------

	Succubus->UseEnemyAbility(Succubus_t::SoulfireAbility, Target,
							  Target->GetActorLocation());

	ACataclysmProjectile* Bolt = Succubus->LastShotFired.Get();
	if (!Bolt)
	{
		AddError(TEXT("Soulfire fired nothing at all"));
		return false;
	}

	// **RadiusCm, NOT BodyRadiusCm.** `ACataclysmProjectile` has two radii and
	// they are different: RadiusCm is what the skill's Radius parameter means
	// and what the marker drew, and BodyRadiusCm is how wide the flying object
	// is and stays at 40 cm. A test asserting the marked radius against the
	// body radius fails reading 40.
	TestEqual(TEXT("the bolt is as wide as the lane that was marked"),
		Bolt->RadiusCm, Succubus_t::SoulfireRadiusCm);

	// A SPEED AND NO FLIGHT TIME, so it travels flat. A projectile given
	// NEITHER is a beam; one given a flight time and no speed is a lob.
	TestEqual(TEXT("and it travels at the slowest projectile speed in the game"),
		Bolt->SpeedCmPerSecond, Succubus_t::SoulfireSpeedCmPerSecond);

	// --- Wither the Living ------------------------------------------------

	const FGameplayTag Withered = WitheredTouchTag();
	if (!Withered.IsValid())
	{
		AddError(TEXT("Status.Debuff.WitheredTouch is not in the tag vocabulary, so "
					  "the curse could grant nothing whatever the code did. See "
					  "game/Config/Tags/CataclysmTags.ini."));
		return false;
	}

	TestFalse(TEXT("the target is not cursed before it is cursed"),
		UCataclysmSkillEffects::HasTag(Target, Withered));

	Succubus->UseEnemyAbility(Succubus_t::WitherTheLivingAbility, Target,
							  Target->GetActorLocation());

	TestTrue(TEXT("Wither the Living puts Withered Touch on its target"),
		UCataclysmSkillEffects::HasTag(Target, Withered));

	TestEqual(TEXT("and the creature recorded who it cursed"),
		Succubus->LastCursed.Get(), static_cast<AActor*>(Target));

	// THE CURSE FIRES NO PROJECTILE. It is a Debuff; the only thing it does is
	// apply the effect. LastShotFired still holds the bolt from above, which is
	// what says the curse did not overwrite it with a shot of its own.
	TestEqual(TEXT("and it fired nothing, because a curse is not a projectile"),
		Succubus->LastShotFired.Get(), Bolt);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmSuccubusAuraBuffsAlliesAndNotThePlayer,
	"Cataclysm.Succubus.ItsAuraBuffsAlliesAndNotThePlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSuccubusAuraBuffsAlliesAndNotThePlayer::RunTest(const FString&)
{
	using namespace CataclysmSuccubusTest;
	using Succubus_t = ACataclysmSuccubusCharacter;

	// **WHAT THIS EXISTS FOR.** Dominion is the only thing in the game that
	// helps a creature rather than hurting one, so it is the only caller of
	// `FindAlliesInSphere`. That name differs from `FindEnemiesInSphere` by one
	// word, and getting it wrong would grant Commander to the player -- the
	// creature that is trying to kill you making you stronger, with nothing on
	// screen to explain it.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	const FGameplayTag Commander = CommanderTag();
	if (!Commander.IsValid())
	{
		AddError(TEXT("Status.Buff.Commander is not in the tag vocabulary, so the "
					  "aura could grant nothing whatever the code did. See "
					  "game/Config/Tags/CataclysmTags.ini."));
		return false;
	}

	ACataclysmSuccubusCharacter* Succubus =
		SpawnSuccubus(World, FVector::ZeroVector);
	if (!Succubus)
	{
		AddError(TEXT("could not spawn a Succubus"));
		return false;
	}

	// ONE OF EACH, BOTH THE SAME DISTANCE AWAY AND BOTH WELL INSIDE THE FIELD.
	// Same distance so that the only thing separating them is which side they
	// are on.
	const float Inside = Succubus_t::DominionRadiusCm / 2.0f;

	ACataclysmEnemyCharacter* Ally =
		SpawnOn(World, ECataclysmTeam::Monsters, FVector(Inside, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Enemy =
		SpawnOn(World, ECataclysmTeam::Players, FVector(-Inside, 0.0f, 0.0f));
	if (!Ally || !Enemy)
	{
		AddError(TEXT("could not spawn the two things the aura chooses "
					  "between"));
		return false;
	}

	// AND ONE ALLY OUTSIDE THE FIELD, so that "everything got buffed" cannot
	// pass this test. Placed just beyond the radius rather than far away,
	// because a check that only refuses the far side of the map is not checking
	// the radius.
	ACataclysmEnemyCharacter* TooFar =
		SpawnOn(World, ECataclysmTeam::Monsters,
				FVector(0.0f, Succubus_t::DominionRadiusCm + 200.0f, 0.0f));
	if (!TooFar)
	{
		AddError(TEXT("could not spawn an ally outside the field"));
		return false;
	}

	const int32 Buffed = Succubus->PulseDominion();

	TestEqual(TEXT("exactly one thing is inside the field and on the right "
				   "side"),
		Buffed, 1);

	TestTrue(TEXT("the ally inside the field holds Commander"),
		UCataclysmSkillEffects::HasTag(Ally, Commander));

	TestFalse(TEXT("**the player's side does NOT**, at the same distance"),
		UCataclysmSkillEffects::HasTag(Enemy, Commander));

	TestFalse(TEXT("and neither does an ally standing outside the radius"),
		UCataclysmSkillEffects::HasTag(TooFar, Commander));

	// THE SUCCUBUS DOES NOT BUFF ITSELF. The design says "every allied enemy
	// within 8 metres" and a creature is not its own ally; `FindAlliesInSphere`
	// excludes the instigator, and this is what says it still does.
	TestFalse(TEXT("and the Succubus does not buff itself"),
		UCataclysmSkillEffects::HasTag(Succubus, Commander));

	// --- WALKING OUT TAKES IT AWAY ---------------------------------------

	Ally->SetActorLocation(
		FVector(Succubus_t::DominionRadiusCm + 200.0f, 0.0f, 0.0f));

	const int32 AfterWalkingOut = Succubus->PulseDominion();

	TestEqual(TEXT("nothing is left in the field once the ally walks out"),
		AfterWalkingOut, 0);

	TestFalse(TEXT("and the ally that walked out has lost Commander, rather "
				   "than keeping it until a duration expires"),
		UCataclysmSkillEffects::HasTag(Ally, Commander));

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmSuccubusAuraEndsWhenItDies,
	"Cataclysm.Succubus.ItsAuraEndsWhenItDies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSuccubusAuraEndsWhenItDies::RunTest(const FString&)
{
	using namespace CataclysmSuccubusTest;
	using Succubus_t = ACataclysmSuccubusCharacter;

	// **THIS IS THE CREATURE'S WHOLE REASON FOR EXISTING.**
	// `docs/Cataclysm_GDD_v2.md` says of Dominion: "Held on rather than cast,
	// because killing it first is the correct play and only an aura makes that
	// true. A buff that is cast and then lasts a duration survives the caster,
	// so killing the Succubus achieves nothing until the timer runs out."
	//
	// A buffed creature looks exactly like an unbuffed one, so if this stopped
	// working nothing on screen would say so.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	const FGameplayTag Commander = CommanderTag();
	if (!Commander.IsValid())
	{
		AddError(TEXT("Status.Buff.Commander is not in the tag vocabulary."));
		return false;
	}

	ACataclysmSuccubusCharacter* Succubus =
		SpawnSuccubus(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Ally =
		SpawnOn(World, ECataclysmTeam::Monsters,
				FVector(Succubus_t::DominionRadiusCm / 2.0f, 0.0f, 0.0f));
	if (!Succubus || !Ally)
	{
		AddError(TEXT("could not spawn a Succubus and an ally"));
		return false;
	}

	Succubus->PulseDominion();

	if (!UCataclysmSkillEffects::HasTag(Ally, Commander))
	{
		AddError(TEXT("the ally was never buffed, so this test would pass by "
					  "doing nothing"));
		return false;
	}

	TestEqual(TEXT("the Succubus is holding up one buff"),
		Succubus->DominionHolders.Num(), 1);

	Succubus->HandleDeath();

	TestTrue(TEXT("the Succubus is dead"),
		UCataclysmSkillEffects::IsDead(Succubus));

	TestFalse(TEXT("**and the ally lost Commander the moment it died**, rather "
				   "than keeping it until a duration expired"),
		UCataclysmSkillEffects::HasTag(Ally, Commander));

	TestEqual(TEXT("and the creature is holding up nothing"),
		Succubus->DominionHolders.Num(), 0);

	// AND A SWEEP AFTER DEATH PUTS NOTHING BACK. The timer is cleared when the
	// actor leaves the level, but a sweep already in flight could still run, so
	// PulseDominion refuses outright once the creature is dead.
	const int32 AfterDeath = Succubus->PulseDominion();

	TestEqual(TEXT("and a sweep that runs after it died buffs nobody"),
		AfterDeath, 0);

	TestFalse(TEXT("so the ally is still unbuffed"),
		UCataclysmSkillEffects::HasTag(Ally, Commander));

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmSuccubusWearsItsMeshAndHidesThePlaceholder,
	"Cataclysm.Succubus.ItWearsItsMeshAndHidesThePlaceholder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSuccubusWearsItsMeshAndHidesThePlaceholder::RunTest(const FString&)
{
	using namespace CataclysmSuccubusTest;
	using Succubus_t = ACataclysmSuccubusCharacter;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmSuccubusCharacter* Succubus =
		SpawnSuccubus(World, FVector::ZeroVector);
	if (!Succubus)
	{
		AddError(TEXT("could not spawn a Succubus"));
		return false;
	}

	const bool bDressed = Succubus->ResolveBody(/*bIncludeAnimation=*/true);

	if (!bDressed)
	{
		// THE PARAGON PACKS ARE NOT COMMITTED, so this half cannot run on a
		// machine without them. Reported through the helper rather than with
		// AddInfo, because `python tools/unreal_build.py tests` only names a
		// skip that went through the helper. Issue #467.
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Countess pack is not installed, so the mesh, the "
				 "five clips and the placeholder check were not exercised. See "
				 "game/docs/enemy-source-assets.md."));
		return true;
	}

	USkeletalMeshComponent* MeshComponent = Succubus->GetMesh();
	if (!MeshComponent)
	{
		AddError(TEXT("the Succubus has no skeletal mesh component"));
		return false;
	}

	TestNotNull(TEXT("it is wearing a skeletal mesh"),
		MeshComponent->GetSkeletalMeshAsset());

	// THE MESH DROPS BY THE CAPSULE'S HALF-HEIGHT, so its feet are on the bottom
	// of the capsule rather than at its centre.
	TestEqual(TEXT("the mesh is dropped so its feet are on the capsule bottom"),
		static_cast<float>(MeshComponent->GetRelativeLocation().Z),
		-Succubus_t::SuccubusCapsuleHalfHeight);

	// AND THE ENGINE'S YAW. A character mesh faces -Y while the actor faces +X,
	// and the stride measurement found this rig's forward axis to be -Y, which
	// is what says the convention holds for it.
	TestEqual(TEXT("and turned by the engine's character-mesh yaw"),
		static_cast<float>(MeshComponent->GetRelativeRotation().Yaw), -90.0f);

	TestNotNull(TEXT("the idle loaded"), Succubus->IdleAnimation.Get());
	TestNotNull(TEXT("the walk loaded"), Succubus->JogAnimation.Get());
	TestNotNull(TEXT("the attack clip loaded"),
		Succubus->AttackAnimation.Get());

	// **A SEPARATE CAST CLIP, WHICH IS WHAT MAKES THE CURSE INTERRUPTIBLE IN
	// PRACTICE.** The design's counter to a Debuff is interrupting the caster,
	// and a player cannot interrupt what they cannot tell apart from an ordinary
	// attack.
	TestNotNull(TEXT("the cast clip loaded"), Succubus->CastAnimation.Get());

	if (Succubus->AttackAnimation.Get() && Succubus->CastAnimation.Get())
	{
		TestNotEqual(TEXT("and the curse's clip is not the bolt's"),
			Succubus->CastAnimation.Get(), Succubus->AttackAnimation.Get());
	}

	TestEqual(TEXT("it has exactly one way to fall over"),
		Succubus->DeathAnimations.Num(), Succubus_t::DeathAnimationCount);

	// OTHERWISE THE CYLINDER SITS INSIDE THE CREATURE.
	if (Succubus->PlaceholderBody)
	{
		TestFalse(TEXT("and the placeholder cylinder is hidden"),
			Succubus->PlaceholderBody->IsVisible());
	}

	// THE WALK IS PLAYED AT THE RATE THAT KEEPS ITS FEET STILL, derived from a
	// measurement rather than chosen.
	const float Rate = Succubus_t::JogPlayRate();
	TestTrue(FString::Printf(
			TEXT("the walk's play rate is %.4f, inside the %.1f to %.1f clamp"),
			Rate, Succubus_t::MinimumPlayRate, Succubus_t::MaximumPlayRate),
		Rate > Succubus_t::MinimumPlayRate && Rate < Succubus_t::MaximumPlayRate);

	return true;
}


// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmSuccubusCastReleasesWhenItsBoltLeaves,
	"Cataclysm.Succubus.ItsCastReleasesWhenItsBoltLeaves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSuccubusCastReleasesWhenItsBoltLeaves::RunTest(const FString&)
{
	using namespace CataclysmSuccubusTest;
	using Succubus_t = ACataclysmSuccubusCharacter;

	// **WHAT THIS EXISTS FOR.** The bolt is fired exactly when the wind-up ends.
	// The cast clip releases 0.156 seconds in and runs for 0.9, so fitting it to
	// the 1.3 second wind-up by its whole LENGTH released the cast 1.14 seconds
	// before the bolt appeared. Issue #784, measured under issue #526.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmSuccubusCharacter* Succubus =
		SpawnSuccubus(World, FVector::ZeroVector);
	if (!Succubus)
	{
		AddError(TEXT("could not spawn a Succubus"));
		return false;
	}

	const float Rate = Succubus_t::SoulfirePlayRate();
	const float Delay = Succubus_t::SoulfireDelaySeconds();

	TestEqual(TEXT("the cast plays at its authored speed"), Rate, 1.0f);

	TestTrue(*FString::Printf(TEXT("and waits %.4f s before starting"), Delay),
		Delay > 0.0f);

	// **THE ONE EQUATION THIS WHOLE CHANGE IS.**
	TestEqual(TEXT("the delay plus the release is the wind-up, so the cast "
				   "leaves the hand as the bolt is fired"),
		Delay + Succubus_t::SoulfireReleaseSeconds / Rate,
		Succubus_t::SoulfireWindUpSeconds, 0.001f);

	TestTrue(*FString::Printf(
			TEXT("the clip finishes %.4f s in, inside the %.2f s interval"),
			Delay + Succubus_t::AttackAnimationSeconds / Rate,
			Succubus_t::DesignedAttackIntervalSeconds),
		Delay + Succubus_t::AttackAnimationSeconds / Rate
			< Succubus_t::DesignedAttackIntervalSeconds);

	// --- and the creature really waits ------------------------------------

	ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(Succubus->GetController());
	if (!Brain)
	{
		AddError(TEXT("the Succubus has no controller"));
		return false;
	}

	Succubus->LastPlayedAnimation = nullptr;
	Succubus->BeginEnemyAbilityWindUp(Succubus_t::SoulfireAbility, nullptr);

	TestEqual(TEXT("beginning the wind-up schedules the cast rather than "
				   "playing it"),
		Succubus->PendingWindUpAbility, (int32)Succubus_t::SoulfireAbility);

	TestNull(TEXT("so nothing is playing yet"),
		Succubus->LastPlayedAnimation.Get());

	Brain->WindingUpAbility = Succubus_t::SoulfireAbility;

	AdvanceWorldClock(World, Delay + 0.01);
	Succubus->StartPendingWindUpClip();

	TestEqual(TEXT("**once the wait is over the cast starts**"),
		Succubus->PendingWindUpAbility, (int32)INDEX_NONE);

	// --- a cancelled cast never starts ------------------------------------

	Succubus->LastPlayedAnimation = nullptr;
	Succubus->BeginEnemyAbilityWindUp(Succubus_t::SoulfireAbility, nullptr);
	Brain->WindingUpAbility = INDEX_NONE;

	AdvanceWorldClock(World, Delay + 1.0);
	Succubus->StartPendingWindUpClip();

	TestEqual(TEXT("**a cancelled cast is forgotten rather than started**"),
		Succubus->PendingWindUpAbility, (int32)INDEX_NONE);

	TestNull(TEXT("and nothing was played"),
		Succubus->LastPlayedAnimation.Get());

	return true;
}

#endif // WITH_AUTOMATION_TESTS
