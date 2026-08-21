// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimSequence.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmHellhoundCharacter.h"
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
 * Tests for the Hellhound, the third of the seven Demonic vertical slice
 * creatures.
 *
 * WHAT THESE GUARD, and every one of them is something the creature can fail at
 * without anything saying so:
 *
 * 1. **THE LANE BURNING THE HELLHOUND OR ITS OWN SIDE.** It did until
 *    2026-08-20, when the project owner set a general rule that a creature
 *    does not burn itself or its allies. This creature is the one that rule
 *    was written against: its trail was the one source of friendly fire in the
 *    game.
 *
 *    A lane that started burning its own side again would still burn the
 *    player perfectly well, so the defect is invisible from the outside: the
 *    creature would simply start killing its own pack.
 *    `TheLaneItLeavesSparesTheHellhoundAndItsAllies` proves it both ways, by
 *    turning `ACataclysmGroundZone::bBurnsEveryone` ON on the same zone and
 *    requiring the same sweep to burn the same two creatures. That control is
 *    also the only thing in the game that exercises that flag at all, which is
 *    now kept with no callers by the owner's choice.
 *
 * 2. BEING A HELLHOUND RATHER THAN AN INHERITED TRAINING DUMMY. Every assertion
 *    on a designed figure also asserts it differs from the base enemy's,
 *    because a test that would pass on the base class proves nothing.
 *
 * 3. THE CHARGE STAYING FASTER THAN THE WALK. This creature is the fastest
 *    thing in the game at 7.5 metres per second, and its charge speed is the
 *    one number on it that was chosen rather than derived. A charge slower than
 *    the creature's own walk is strictly worse than not charging, and nothing
 *    about the code would look wrong.
 *
 * 4. WEARING ITS ART WITHOUT BREAKING WITHOUT IT. The Paragon packs are
 *    gitignored, so these run both ways and must pass either way. A run on a
 *    machine without the art says so through `CataclysmTestSkip`.
 *
 * WHAT THESE DELIBERATELY DO NOT CHECK. Whether the creature looks right. Its
 * walk plays at 2.478 against a ceiling of 2.5, which is by far the tightest
 * animation fit in the project, and whether that reads as a running animal or
 * as a sped-up film is a judgement the automation command cannot make: it runs
 * with `-nullrhi`. The arithmetic is checked here; the appearance is issue
 * #756's neighbour and belongs to the project owner.
 */

namespace CataclysmHellhoundTest
{
	using Hellhound_t = ACataclysmHellhoundCharacter;

	/** One frame at sixty frames a second, the same slice the Abyssal Warden's
	 *  charge tests advance by. */
	static constexpr float FrameSeconds = 1.0f / 60.0f;

	/** How far a charge travels in one such frame. 23.8 cm at this creature's
	 *  designed speed, and the tolerance every distance below is measured to,
	 *  because a charge stops on the first step that reaches its end rather
	 *  than exactly at it. */
	static constexpr float ChargeTravelPerFrameCm =
		Hellhound_t::HellrushSpeedCmPerSecond * FrameSeconds;

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

	static ACataclysmHellhoundCharacter* SpawnHellhound(UWorld* World,
														const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACataclysmHellhoundCharacter>(
			ACataclysmHellhoundCharacter::StaticClass(), Where,
			FRotator::ZeroRotator, Params);
	}

	/**
	 * A plain enemy put exactly where it is wanted.
	 *
	 * SPAWNED FAR AWAY AND THEN MOVED, which is not fussiness: two capsules
	 * created at contact distance push each other apart before anything can be
	 * measured, and every distance these tests take is then wrong by however
	 * far they shoved each other.
	 *
	 * @param Team  which side it is on. `ECataclysmTeam::Players` is what
	 *              `FindEnemiesInLine` finds when a creature on the Monsters'
	 *              side asks; `ECataclysmTeam::Monsters` is that creature's ALLY
	 *              and is the case the burning lane exists to change.
	 */
	static ACataclysmEnemyCharacter* SpawnBystander(UWorld* World,
													const FVector& Where,
													ECataclysmTeam Team)
	{
		ACataclysmEnemyCharacter* Bystander =
			World->SpawnActor<ACataclysmEnemyCharacter>(
				ACataclysmEnemyCharacter::StaticClass(),
				FVector(20000.0f, 20000.0f, 0.0f), FRotator::ZeroRotator);
		if (!Bystander)
		{
			return nullptr;
		}
		Bystander->SetGenericTeamId(UCataclysmTeams::IdFor(Team));
		Bystander->SetHealth(100000.0f);
		Bystander->SetActorLocation(Where);
		return Bystander;
	}

	/** What an actor's health attribute says now. Negative means it has no
	 *  ability system at all, which is a different failure from being hurt. */
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

	/** Where the charge is aimed, at the creature's own height.
	 *
	 *  THE CONTROLLER FLATTENS THIS ONTO THE FLOOR and this does not, which
	 *  changes nothing either way: `BeginCharge` replaces the point's height
	 *  with the creature's own, and `IsInLine` drops the height before it
	 *  measures. A test world built with `UWorld::CreateWorld` holds no floor
	 *  to flatten onto, so asking for one here would only invent a number. */
	static FVector StraightAhead(const AActor* From, float DistanceCm)
	{
		return From->GetActorLocation() + FVector(DistanceCm, 0.0f, 0.0f);
	}

	/**
	 * Run a charge that has already been started until it ends.
	 *
	 * @return how many frames it took, or the cap when it never ended.
	 */
	static int32 RunTheChargeOut(ACataclysmHellhoundCharacter* Hellhound)
	{
		// A GENEROUS CAP RATHER THAN THE EXACT COUNT. The lane needs 42 frames
		// at this creature's designed speed; the cap is here so a charge that
		// never ends fails this test rather than hanging the whole run.
		constexpr int32 MostFramesACharge = 400;

		int32 Frames = 0;
		while (Frames < MostFramesACharge && Hellhound->IsCharging())
		{
			Hellhound->AdvanceCharge(FrameSeconds);
			++Frames;
		}
		return Frames;
	}
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmHellhoundCarriesItsDesignedProfile,
	"Cataclysm.Hellhound.ItCarriesItsDesignedProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHellhoundCarriesItsDesignedProfile::RunTest(const FString&)
{
	using namespace CataclysmHellhoundTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmHellhoundCharacter* Hellhound =
		SpawnHellhound(World, FVector::ZeroVector);
	if (!Hellhound)
	{
		AddError(TEXT("could not spawn a Hellhound"));
		return false;
	}

	TestEqual(TEXT("it bites from exactly its designed reach"),
		Hellhound->AttackReachCm(), Hellhound_t::DesignedMeleeReachCm);
	TestEqual(TEXT("it bites on its designed interval"),
		Hellhound->SecondsBetweenAttacks(),
		Hellhound_t::DesignedAttackIntervalSeconds);
	TestEqual(TEXT("it wanders its designed distance"),
		Hellhound->RoamRadiusCm(), Hellhound_t::HellhoundRoamRadiusCm);
	TestEqual(TEXT("it notices at its designed distance"),
		Hellhound->SightRadiusCm(), Hellhound_t::HellhoundNoticeRadiusCm);

	// AND EVERY ONE OF THOSE DIFFERS FROM THE BASE ENEMY'S, which is what makes
	// the four above mean anything. A creature that inherited the base's figures
	// would pass all four.
	TestNotEqual(TEXT("its reach is not the base enemy's"),
		Hellhound->AttackReachCm(), 200.0f);
	TestNotEqual(TEXT("its interval is not the base enemy's"),
		Hellhound->SecondsBetweenAttacks(), 1.5f);
	TestTrue(TEXT("it roams, where the base enemy does not"),
		Hellhound->RoamRadiusCm() > 0.0f);

	const UCapsuleComponent* Capsule = Hellhound->GetCapsuleComponent();
	TestEqual(TEXT("its capsule is its designed radius"),
		Capsule->GetScaledCapsuleRadius(), Hellhound_t::HellhoundCapsuleRadius);
	TestEqual(TEXT("its capsule is its designed half-height"),
		Capsule->GetScaledCapsuleHalfHeight(),
		Hellhound_t::HellhoundCapsuleHalfHeight);

	const UCharacterMovementComponent* Movement =
		Hellhound->GetCharacterMovement();
	TestEqual(TEXT("it walks at its designed speed"),
		Movement->MaxWalkSpeed, Hellhound_t::DesignedWalkSpeedCmPerSecond);
	// CAST BECAUSE FRotator's COMPONENTS ARE DOUBLES IN UNREAL 5. Comparing one
	// against a float constant is an ambiguous overload, error C2666, and the
	// message names eight candidates rather than saying so.
	TestEqual(TEXT("it turns at its designed rate"),
		Movement->RotationRate.Yaw,
		static_cast<double>(Hellhound_t::DesignedTurnRateDegreesPerSecond));

	// THE ONE THAT WOULD OTHERWISE BE SILENT, AND IT POINTS THE OPPOSITE WAY
	// FROM THE ABYSSAL WARDEN'S. `ACataclysmEnemyCharacter` never sets
	// MaxWalkSpeed, so a creature that forgot to would move at Unreal's default
	// 600. For the Warden that would be a silent speeding past the player; for
	// this creature, designed at 750, it is a silent SLOWING that quietly undoes
	// the one thing that makes it what it is.
	TestTrue(TEXT("and it is faster than Unreal's default walk speed, which is "
				  "what a forgotten MaxWalkSpeed would leave it at"),
		Movement->MaxWalkSpeed > 600.0f);

	// THE FASTEST THING IN THE GAME, which is its design rather than an
	// oversight. Player classes run at 350, 400 and 460 cm/s.
	constexpr float FastestPlayerClassCmPerSecond = 460.0f;
	TestTrue(TEXT("it outruns the fastest player class, which is what an "
				  "aggressive charger has to do"),
		Hellhound_t::DesignedWalkSpeedCmPerSecond
			> FastestPlayerClassCmPerSecond);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmHellhoundOffersOnlyItsCharge,
	"Cataclysm.Hellhound.ItOffersOneAbilityAndOnlyBeyondWalkingDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHellhoundOffersOnlyItsCharge::RunTest(const FString&)
{
	using namespace CataclysmHellhoundTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmHellhoundCharacter* Hellhound =
		SpawnHellhound(World, FVector::ZeroVector);
	if (!Hellhound)
	{
		AddError(TEXT("could not spawn a Hellhound"));
		return false;
	}

	const TArray<FCataclysmEnemyAbility> Abilities = Hellhound->EnemyAbilities();

	// ONE, SO THERE IS NO PRIORITY TO GET WRONG. `ChooseAbility` takes the first
	// entry whose range and cooldown fit and never looks at the shape, so the
	// order of this array is the priority order. The Abyssal Warden has two and
	// issue #491 was about their order; this creature cannot have that defect
	// while the array holds one entry, and this is what would notice a second
	// arriving without anyone deciding where it goes.
	if (Abilities.Num() != 1)
	{
		AddError(FString::Printf(
			TEXT("the Hellhound offers %d abilities and its design gives it one, "
				 "Hellrush. A bite is not an entry here at all -- it is "
				 "MeleeReachCm plus AttackIntervalSeconds."), Abilities.Num()));
		return false;
	}

	const FCataclysmEnemyAbility& Hellrush = Abilities[0];

	TestEqual(TEXT("the one ability is Hellrush"),
		Hellrush.Name, FName(TEXT("Hellrush")));
	TestTrue(TEXT("it is a Movement ability, which is what makes the brain aim "
				  "it at the end of its own lane rather than at the player"),
		Hellrush.Shape == ECataclysmSkillShape::Movement);
	TestEqual(TEXT("it charges its designed range"),
		Hellrush.MaxRangeCm, Hellhound_t::HellrushRangeCm);
	TestEqual(TEXT("its cooldown is the one really in use, so a console "
				   "override reaches the brain without a rebuild"),
		Hellrush.CooldownSeconds, Hellhound_t::HellrushCooldownSecondsInUse());
	TestEqual(TEXT("its wind-up is its designed telegraph"),
		Hellrush.WindUpSeconds, Hellhound_t::HellrushWindUpSeconds);

	// ONE HALF-WIDTH FOR ALL THREE THINGS, which is the whole reason the marker
	// can be trusted. The corridor the player sees, the corridor that hits, and
	// the corridor that goes on burning are the same constant. The Brute's rock
	// throw shipped the opposite arrangement four times.
	TestEqual(TEXT("the marker is drawn at the lane's own half-width"),
		Hellrush.MarkerRadiusCm, Hellhound_t::HellrushRadiusCm);
	TestEqual(TEXT("and the fire is left along the same half-width, so what "
				   "burned you is what the marker showed"),
		Hellhound_t::HellrushGroundRadiusCm, Hellhound_t::HellrushRadiusCm);

	// NOT AT SOMETHING IT COULD SIMPLY WALK TO. A charge covering less ground
	// than the creature could walk during its own wind-up is strictly worse than
	// not winding up at all. The header derives the figure; this checks that the
	// derivation is the number the brain is actually handed.
	TestEqual(TEXT("it refuses anything nearer than it could walk to during its "
				   "own telegraph"),
		Hellrush.MinRangeCm, Hellhound_t::HellrushMinimumRangeCm);
	TestEqual(TEXT("and that distance is its walk speed times its wind-up"),
		Hellrush.MinRangeCm,
		Hellhound_t::DesignedWalkSpeedCmPerSecond
			* Hellhound_t::HellrushWindUpSeconds);

	// AND THE BAND IS NOT EMPTY, which is what would happen if the creature got
	// faster or the wind-up got longer: the shortest distance worth charging
	// would pass the furthest it can charge and the ability could never be used.
	TestTrue(TEXT("there is a distance at which the charge is legal at all"),
		Hellrush.MinRangeCm < Hellrush.MaxRangeCm);

	// IT NOTICES AT THE FAR END OF ITS OWN CHARGE. `ChooseAbility` refuses a
	// distance greater than MaxRangeCm and allows one exactly equal to it, so
	// the charge is legal from the first moment the creature has seen anything
	// at all. That is worth stating because it is what a person watching the
	// sandbox will see happen first.
	TestEqual(TEXT("its notice radius is the far end of its charge's range"),
		Hellhound->SightRadiusCm(), Hellrush.MaxRangeCm);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmHellhoundChargeIsFasterThanItsOwnWalk,
	"Cataclysm.Hellhound.TheChargeIsFasterThanItsOwnWalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHellhoundChargeIsFasterThanItsOwnWalk::RunTest(const FString&)
{
	using namespace CataclysmHellhoundTest;

	// THE ONE NUMBER ON THIS CREATURE THAT WAS CHOSEN RATHER THAN DERIVED, and
	// therefore the one most likely to be changed by somebody tuning a play
	// session. The Abyssal Warden's charge speed falls out of its clip's length;
	// this creature has no charge clip at all, so 1428.57 came from making the
	// charge last the same 0.700 seconds the Warden's does over a longer range.
	//
	// WHAT MUST STAY TRUE OF IT WHATEVER IT BECOMES: a creature that already
	// moves at 750 cm/s gains nothing from a charge that TRAVELS slower than
	// that. It would stand still for an 0.83 second telegraph and then cross the
	// ground more slowly than it was already crossing it.
	TestTrue(FString::Printf(
			TEXT("the charge travels at %.2f cm/s and the creature walks at "
				 "%.2f cm/s, so the run itself is a dash"),
			Hellhound_t::HellrushSpeedCmPerSecondInUse(),
			Hellhound_t::DesignedWalkSpeedCmPerSecond),
		Hellhound_t::HellrushSpeedCmPerSecondInUse()
			> Hellhound_t::DesignedWalkSpeedCmPerSecond);

	// WHAT IS NOT TRUE OF IT, SAID OUT LOUD SO NOBODY WRITES THIS CHECK AGAIN.
	// The whole move does NOT beat walking the same ground: 0.83 seconds of
	// telegraph plus 0.70 seconds of running is 1.53 seconds to cover 10 metres,
	// against 1.33 to walk them. An earlier draft of this test asserted the
	// opposite and failed, which is how the figure above was measured.
	//
	// THAT IS THE DESIGN RATHER THAN A DEFECT, and the creature's own header
	// says so: "this creature already moves at 7.5 metres per second, so a
	// charge does not have to be what makes it fast". Hellrush is not a way of
	// closing distance. It hits everything along the lane, shoves it four metres
	// aside and leaves the ground burning for four seconds, and walking does
	// none of those.
	//
	// THE TEST THE DESIGN DOES MAKE is the one below: the charge has to cover
	// more ground than the creature could simply walk during its own telegraph.
	// Below that the creature stood still for nothing.
	TestTrue(FString::Printf(
			TEXT("it charges %.0f cm, further than the %.1f cm it could walk "
				 "during its own %.2f s telegraph"),
			Hellhound_t::HellrushRangeCm,
			Hellhound_t::DesignedWalkSpeedCmPerSecond
				* Hellhound_t::HellrushWindUpSeconds,
			Hellhound_t::HellrushWindUpSeconds),
		Hellhound_t::HellrushRangeCm
			> Hellhound_t::DesignedWalkSpeedCmPerSecond
				* Hellhound_t::HellrushWindUpSeconds);

	// THE OVERRIDE IS THE DESIGNED FIGURE UNLESS SOMEBODY SET ONE. Checked so
	// that a console variable left set in a previous test cannot make the two
	// above pass for the wrong reason.
	TestEqual(TEXT("no console override is in force during this run"),
		Hellhound_t::HellrushSpeedCmPerSecondInUse(),
		Hellhound_t::HellrushSpeedCmPerSecond);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmHellhoundChargeRunsItsWholeLane,
	"Cataclysm.Hellhound.HellrushRunsItsWholeLaneAtItsDesignedSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHellhoundChargeRunsItsWholeLane::RunTest(const FString&)
{
	using namespace CataclysmHellhoundTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmHellhoundCharacter* Hellhound =
		SpawnHellhound(World, FVector::ZeroVector);
	if (!Hellhound)
	{
		AddError(TEXT("could not spawn a Hellhound"));
		return false;
	}

	const FVector Start = Hellhound->GetActorLocation();
	const FVector Aim = StraightAhead(Hellhound, Hellhound_t::HellrushRangeCm);

	// THROUGH THE CREATURE'S OWN ABILITY RATHER THAN THROUGH BeginCharge, which
	// is the difference between checking the base class's charge and checking
	// that this creature is wired to it. Calling BeginCharge with the header's
	// constants would pass on a creature whose UseEnemyAbility does nothing.
	Hellhound->UseEnemyAbility(Hellhound_t::HellrushAbility, nullptr, Aim);

	if (!Hellhound->IsCharging())
	{
		AddError(TEXT("using Hellrush did not start a charge at all"));
		return false;
	}

	const int32 Frames = RunTheChargeOut(Hellhound);

	TestFalse(TEXT("the charge ends rather than running forever"),
		Hellhound->IsCharging());

	// IT RAN THE WHOLE LANE. Within one frame's travel, because a charge stops
	// on the first step that reaches its end rather than exactly at it.
	TestTrue(FString::Printf(
			TEXT("it travelled %.1f cm of its designed %.0f cm"),
			Hellhound->ChargeTravelledCm, Hellhound_t::HellrushRangeCm),
		FMath::Abs(Hellhound->ChargeTravelledCm - Hellhound_t::HellrushRangeCm)
			<= ChargeTravelPerFrameCm);

	// AND IT ENDED WHERE IT AIMED, which is not the same statement: a charge
	// that travelled the right distance in the wrong direction passes the check
	// above.
	const float EndedAtX = static_cast<float>(Hellhound->GetActorLocation().X);
	TestTrue(FString::Printf(
			TEXT("it ended at x=%.1f, aiming at x=%.1f"), EndedAtX,
			static_cast<float>(Aim.X)),
		FMath::Abs(EndedAtX - static_cast<float>(Aim.X))
			<= ChargeTravelPerFrameCm);

	TestTrue(TEXT("and it did not drift sideways off its own lane"),
		FMath::Abs(Hellhound->GetActorLocation().Y - Start.Y) < 1.0);

	// AT ITS DESIGNED SPEED. Read from how long it took rather than from the
	// constant, so a charge that reached the right place at the wrong speed --
	// which is what a dropped override or a clamped step would produce -- is
	// caught here.
	const float SecondsTaken = Frames * FrameSeconds;
	const float MeasuredSpeed = Hellhound->ChargeTravelledCm / SecondsTaken;

	TestTrue(FString::Printf(
			TEXT("it travelled at about %.0f cm/s against a designed %.2f"),
			MeasuredSpeed, Hellhound_t::HellrushSpeedCmPerSecond),
		FMath::Abs(MeasuredSpeed - Hellhound_t::HellrushSpeedCmPerSecond)
			< Hellhound_t::HellrushSpeedCmPerSecond * 0.05f);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmHellhoundChargeLeavesItsLaneBurning,
	"Cataclysm.Hellhound.HellrushLeavesTheWholeLaneBurningBehindIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHellhoundChargeLeavesItsLaneBurning::RunTest(const FString&)
{
	using namespace CataclysmHellhoundTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmHellhoundCharacter* Hellhound =
		SpawnHellhound(World, FVector::ZeroVector);
	if (!Hellhound)
	{
		AddError(TEXT("could not spawn a Hellhound"));
		return false;
	}

	// PRICED OFF THE CREATURE'S OWN ATTACK DAMAGE AT THE MOMENT THE FIRE IS
	// LAID, so it has to have some before the charge starts. Set here rather
	// than left to whatever the archetype supplies, because this test asserts
	// the exact figure the lane is worth.
	constexpr float AttackDamage = 100.0f;
	Hellhound->SetAttackDamage(AttackDamage);

	const FVector Start = Hellhound->GetActorLocation();
	const FVector Aim = StraightAhead(Hellhound, Hellhound_t::HellrushRangeCm);

	Hellhound->UseEnemyAbility(Hellhound_t::HellrushAbility, nullptr, Aim);

	ACataclysmGroundZone* Lane = Hellhound->LastLaneLeftBurning.Get();
	if (!IsValid(Lane))
	{
		AddError(TEXT("Hellrush left no burning lane at all. The design says the "
					  "creature leaves that lane on fire, and nothing else in "
					  "the game does."));
		return false;
	}

	// LAID WHEN THE CREATURE SETS OFF, NOT WHEN IT ARRIVES. A lane that appeared
	// only after the charge finished would burn nobody who was standing in it
	// during the run, which is most of the people it is meant to burn. The
	// creature is still charging at this point, which is what proves it.
	TestTrue(TEXT("the fire is on the ground while the charge is still running"),
		Hellhound->IsCharging());

	// A LANE RATHER THAN A PATCH. The design says the creature leaves "that lane
	// on fire"; a patch where it stopped would be a different ability -- the one
	// the player's Infernal Plunge has.
	TestTrue(TEXT("it covers a path rather than a point"), Lane->IsLong());
	TestEqual(TEXT("its near end is where the creature set off from"),
		Lane->GetActorLocation(), Start);
	TestEqual(TEXT("its far end is where the charge was aimed"),
		Lane->FarEnd, Aim);
	TestEqual(TEXT("it is as wide as the marker that warned about it"),
		Lane->RadiusCm, Hellhound_t::HellrushGroundRadiusCm);

	// A QUARTER OF A HIT PER SECOND. Standing in the whole four seconds costs
	// one whole hit, which is the same as being run over once.
	TestEqual(TEXT("one second in it is its designed share of an ordinary hit"),
		Lane->DamagePerTick,
		AttackDamage * Hellhound_t::HellrushGroundPercent / 100.0f);

	// AND IT BURNS ONLY THE CREATURE'S ENEMIES. It burned everything standing
	// in it, including the Hellhound, until 2026-08-20, when the project owner
	// set the rule that a creature does not burn itself or its own side.
	// Checked here as well as in the next test because it is decided at the
	// call site, and a caller that started passing true again would leave every
	// other assertion in this test passing.
	TestFalse(TEXT("it burns only the creature's enemies, not whatever stands "
				   "in it"),
		Lane->bBurnsEveryone);

	// FOUR SECONDS AND THEN GONE. Read off the actor's own lifespan, which is
	// what SpawnAlong sets and what actually removes it. A tolerance rather than
	// equality because GetLifeSpan reports what is LEFT on the timer, and the
	// world's clock is not guaranteed to stand exactly still between the two
	// lines.
	TestTrue(FString::Printf(
			TEXT("it burns for %.2f s against a designed %.2f"),
			Lane->GetLifeSpan(), Hellhound_t::HellrushGroundSeconds),
		FMath::Abs(Lane->GetLifeSpan() - Hellhound_t::HellrushGroundSeconds)
			< 0.01f);

	return true;
}

// --------------------------------------------------------------------------
// The one genuinely new behaviour in the creature
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmHellhoundLaneSparesItsOwnSide,
	"Cataclysm.Hellhound.TheLaneItLeavesSparesTheHellhoundAndItsAllies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHellhoundLaneSparesItsOwnSide::RunTest(const FString&)
{
	using namespace CataclysmHellhoundTest;

	// **A CREATURE DOES NOT BURN ITSELF OR ITS OWN SIDE**, set by the project
	// owner on 2026-08-20 as a general rule. This creature is the one it was
	// written against: its trail carried `GroundHitsAllies=1` and burned other
	// demons and the Hellhound itself, and the design document called it the
	// one source of friendly fire in the game.
	//
	// WHY IT NEEDS A TEST OF ITS OWN, IN BOTH DIRECTIONS. A lane that started
	// burning its own side again would still burn the player perfectly well, so
	// the defect is invisible from the outside: the creature would simply start
	// killing its own pack, and every other test in this file would go on
	// passing.
	//
	// AND THE CONTROL AT THE END STILL EXERCISES `bBurnsEveryone`, which now
	// has no callers anywhere in the game. It is kept by the project owner's
	// choice so the option is on the record; a kept feature nothing exercises
	// is a feature nobody notices has rotted.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmHellhoundCharacter* Hellhound =
		SpawnHellhound(World, FVector::ZeroVector);
	if (!Hellhound)
	{
		AddError(TEXT("could not spawn a Hellhound"));
		return false;
	}

	constexpr float AttackDamage = 100.0f;
	Hellhound->SetAttackDamage(AttackDamage);
	Hellhound->SetHealth(100000.0f);

	// THREE THINGS STANDING IN THE LANE, ONE OF EACH KIND THAT MATTERS.
	//
	// The creature itself is already at the near end of its own lane and does
	// not need placing: `IsInLine` measures from the start point, so a creature
	// standing exactly on it is inside.
	const float HalfWay = Hellhound_t::HellrushRangeCm / 2.0f;

	ACataclysmEnemyCharacter* Ally = SpawnBystander(
		World, FVector(HalfWay, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Player = SpawnBystander(
		World, FVector(HalfWay + 100.0f, 0.0f, 0.0f), ECataclysmTeam::Players);
	if (!Ally || !Player)
	{
		AddError(TEXT("could not spawn something to stand in the lane"));
		return false;
	}

	Hellhound->UseEnemyAbility(Hellhound_t::HellrushAbility, nullptr,
							   StraightAhead(Hellhound,
											 Hellhound_t::HellrushRangeCm));

	ACataclysmGroundZone* Lane = Hellhound->LastLaneLeftBurning.Get();
	if (!IsValid(Lane))
	{
		AddError(TEXT("Hellrush left no burning lane to sweep"));
		return false;
	}
	if (Lane->DamagePerTick <= 0.0f)
	{
		AddError(FString::Printf(
			TEXT("the lane is worth %.2f a second, so a sweep cannot hurt "
				 "anybody and this test would pass by doing nothing"),
			Lane->DamagePerTick));
		return false;
	}

	const float HellhoundBefore = HealthOf(Hellhound);
	const float AllyBefore = HealthOf(Ally);
	const float PlayerBefore = HealthOf(Player);
	if (HellhoundBefore < 0.0f || AllyBefore < 0.0f || PlayerBefore < 0.0f)
	{
		AddError(TEXT("something in the lane has no ability system, so its "
					  "health cannot be read and nothing here means anything"));
		return false;
	}

	// SWEPT BY HAND RATHER THAN WAITED FOR. A zone sweeps on a timer a second
	// apart, and a test world built with UWorld::CreateWorld is never ticked, so
	// nothing would ever fire.
	Lane->Sweep();

	TestEqual(TEXT("the sweep found only the player's side standing in the "
				   "lane, of the three that are"),
		Lane->LastSweepCount, 1);

	// **THE RULE.**
	TestEqual(FString::Printf(
			TEXT("the Hellhound did not burn itself: %.1f health"),
			HealthOf(Hellhound)),
		HealthOf(Hellhound), HellhoundBefore);

	TestEqual(FString::Printf(
			TEXT("nor another enemy on its own side: %.1f health"),
			HealthOf(Ally)),
		HealthOf(Ally), AllyBefore);

	// AND THE ABILITY STILL DOES ITS JOB, which is the half that makes the two
	// above mean something rather than describing a lane that burns nobody.
	TestTrue(FString::Printf(
			TEXT("and it burns the player's side: %.1f health to %.1f"),
			PlayerBefore, HealthOf(Player)),
		HealthOf(Player) < PlayerBefore);

	// AND NOW THE CONTROL, WHICH IS WHAT MAKES THE THREE ABOVE MEAN SOMETHING.
	// The same zone, the same three creatures, the same sweep, with the one flag
	// turned ON. If the flag is not what decides, the assertions above pass for
	// some other reason -- a sweep that finds nobody, an ally with no ability
	// system -- and the test is worthless.
	//
	// **NOTHING IN THE GAME SETS IT.** This is the only place it is set at all,
	// and it is set here on a zone the test owns rather than on a creature.
	Lane->bBurnsEveryone = true;

	const float HellhoundAfterFirst = HealthOf(Hellhound);
	const float AllyAfterFirst = HealthOf(Ally);
	const float PlayerAfterFirst = HealthOf(Player);

	Lane->Sweep();

	TestEqual(TEXT("with the flag set the sweep finds all three"),
		Lane->LastSweepCount, 3);

	TestTrue(TEXT("with the flag set the Hellhound IS burned by its own lane, "
				  "which is what says the flag is what decides"),
		HealthOf(Hellhound) < HellhoundAfterFirst);
	TestTrue(TEXT("and so is another enemy on its side"),
		HealthOf(Ally) < AllyAfterFirst);
	TestTrue(TEXT("and the player's side is burned either way, which is why a "
				  "caller passing true again would be invisible in play"),
		HealthOf(Player) < PlayerAfterFirst);

	return true;
}

// --------------------------------------------------------------------------
// Displacement. Issue #625, the third of the three abilities the design names
// as shoving the player and the one that had no creature to live on.
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmHellhoundChargeShovesWhatItRunsThrough,
	"Cataclysm.Hellhound.HellrushShovesWhatItRunsThroughOutOfTheLane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHellhoundChargeShovesWhatItRunsThrough::RunTest(const FString&)
{
	using namespace CataclysmHellhoundTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmHellhoundCharacter* Hellhound =
		SpawnHellhound(World, FVector::ZeroVector);
	if (!Hellhound)
	{
		AddError(TEXT("could not spawn a Hellhound"));
		return false;
	}

	// OFF THE CENTRE LINE BUT INSIDE THE LANE. Standing exactly on the axis
	// would put it directly in front of the creature, and the shove is away from
	// the creature, so it would be pushed ALONG the lane and this could not tell
	// sideways from forwards.
	const float HalfWay = Hellhound_t::HellrushRangeCm / 2.0f;
	const float OffAxis = Hellhound_t::HellrushRadiusCm / 2.0f;

	ACataclysmEnemyCharacter* InTheLane = SpawnBystander(
		World, FVector(HalfWay, OffAxis, 0.0f), ECataclysmTeam::Players);
	if (!InTheLane)
	{
		AddError(TEXT("could not spawn something to stand in the lane"));
		return false;
	}
	const FVector Before = InTheLane->GetActorLocation();

	Hellhound->UseEnemyAbility(Hellhound_t::HellrushAbility, nullptr,
							   StraightAhead(Hellhound,
											 Hellhound_t::HellrushRangeCm));
	RunTheChargeOut(Hellhound);

	TestEqual(TEXT("it ran through exactly one thing, once"),
		Hellhound->ChargeHitCount, 1);

	const FVector After = InTheLane->GetActorLocation();
	const float Moved = static_cast<float>(FVector::Dist2D(Before, After));

	// IT MOVED, AND ROUGHLY THE DESIGNED DISTANCE. A tolerance rather than
	// equality, because the shove is swept and a capsule can stop short against
	// another body. What must not happen is it not moving at all, which is what
	// a knockback that reached nothing looks like.
	TestTrue(FString::Printf(
			TEXT("it was shoved; designed %.0f cm, moved %.0f cm"),
			Hellhound_t::HellrushKnockbackCm, Moved),
		Moved > Hellhound_t::HellrushKnockbackCm * 0.5f);

	// IT ENDS OUTSIDE THE LANE, which is what shoving somebody aside has to mean
	// for a charge: the ground the creature is running down is cleared.
	const float OutFromAxis = static_cast<float>(FMath::Abs(After.Y));
	TestTrue(FString::Printf(
			TEXT("it ended %.0f cm off the axis, outside the %.0f cm lane"),
			OutFromAxis, Hellhound_t::HellrushRadiusCm),
		OutFromAxis > Hellhound_t::HellrushRadiusCm);

	// AND AWAY FROM THE CENTRE LINE, not across it into the creature's path. It
	// started at +Y, so it must end further out in +Y.
	TestTrue(TEXT("it was pushed away from the centre line, not through it"),
		After.Y > Before.Y);

	return true;
}

// --------------------------------------------------------------------------
// Its art
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmHellhoundWearsItsMeshAndHidesThePlaceholder,
	"Cataclysm.Hellhound.ItWearsItsMeshAndHidesThePlaceholder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHellhoundWearsItsMeshAndHidesThePlaceholder::RunTest(const FString&)
{
	using namespace CataclysmHellhoundTest;

	// A WORLD THAT HAS NOT BEGUN PLAY. This test is about what ResolveBody does,
	// and it proves it by watching the placeholder go from visible to hidden.
	// The creature's own BeginPlay calls ResolveBody, so in a world that has
	// begun play the cylinder is already hidden by the time the first line below
	// runs and there is no transition left to watch.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasNotBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmHellhoundCharacter* Hellhound =
		SpawnHellhound(World, FVector::ZeroVector);
	if (!Hellhound)
	{
		AddError(TEXT("could not spawn a Hellhound"));
		return false;
	}

	if (!Hellhound->PlaceholderBody)
	{
		AddError(TEXT("the enemy base no longer creates a PlaceholderBody, so "
					  "there is nothing to hide and this check is meaningless"));
		return false;
	}

	// IT STARTS VISIBLE, which is what makes the rest of this mean something. A
	// placeholder that was hidden from birth would pass the check below on a
	// creature that never turns it off.
	TestTrue(TEXT("the placeholder cylinder starts visible"),
		Hellhound->PlaceholderBody->IsVisible());

	const bool bDressed = Hellhound->ResolveBody(/*bIncludeAnimation=*/true);

	// THE RELATIONSHIP RATHER THAN AN ABSOLUTE, so this runs the same on a
	// machine with the Paragon Iggy and Scorch pack and on one without. With the
	// art the mesh resolves and the cylinder must go; without it ResolveBody
	// returns false and the cylinder is all there is, so it must stay.
	TestEqual(
		TEXT("the placeholder is hidden exactly when the real mesh resolved"),
		Hellhound->PlaceholderBody->IsVisible(), !bDressed);

	if (!bDressed)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Iggy and Scorch pack is not present, so what was "
				 "checked is that the placeholder is KEPT rather than that it is "
				 "hidden, and neither the mesh nor any of its clips was loaded. "
				 "Both directions matter; only one ran here."));
		return true;
	}

	USkeletalMeshComponent* MeshComponent = Hellhound->GetMesh();
	if (!MeshComponent)
	{
		AddError(TEXT("the creature has no skeletal mesh component"));
		return false;
	}

	TestNotNull(TEXT("it is wearing a skeletal mesh"),
		MeshComponent->GetSkeletalMeshAsset());

	// FEET ON THE CAPSULE'S BOTTOM. A skeletal mesh is authored with its origin
	// at the feet and a capsule's origin is its centre, so the mesh has to drop
	// by the half-height or the creature stands buried to the waist.
	TestEqual(TEXT("the mesh is dropped so its feet are on the capsule bottom"),
		MeshComponent->GetRelativeLocation().Z,
		static_cast<double>(-Hellhound_t::HellhoundCapsuleHalfHeight));

	// THE ENGINE'S YAW FOR A CHARACTER MESH, which face -Y while the actor faces
	// +X. The stride measurement found this rig's forward axis to be -Y, which is
	// what says the convention holds for a rig that is two creatures.
	TestEqual(TEXT("and yawed the engine's -90 degrees for a character mesh"),
		MeshComponent->GetRelativeRotation().Yaw, -90.0);

	// EVERY CLIP IT NEEDS. A missing one is logged as a warning and the creature
	// then fights with nothing to show for it.
	TestNotNull(TEXT("its bite clip loaded"), Hellhound->MaulAnimation.Get());
	TestNotNull(TEXT("its standing clip loaded"),
		Hellhound->IdleAnimation.Get());
	TestNotNull(TEXT("its walking clip loaded"), Hellhound->JogAnimation.Get());
	TestNotNull(TEXT("its charge clip loaded"),
		Hellhound->HellrushAnimation.Get());

	// TWO DEATHS, AND THE COUNT IS WHAT MATTERS rather than only that they
	// loaded: ACataclysmEnemyCharacter::PlayDeathAnimation draws one of however
	// many entries there are, so dropping a null entry would change which one is
	// drawn rather than merely losing a clip.
	TestEqual(TEXT("it has two death clips to draw between"),
		Hellhound->DeathAnimations.Num(), 2);

	// AND THE BITE REALLY IS THE LENGTH THE HEADER SAYS IT IS. That figure was
	// measured out of the asset by tools/probe_hellhound_animation.py and is
	// what the static_assert against the attack interval rests on; this is the
	// only place the asset itself is asked.
	if (const UAnimSequence* Maul = Hellhound->MaulAnimation.Get())
	{
		TestTrue(FString::Printf(
				TEXT("its bite clip is %.4f s and the header records %.4f"),
				Maul->GetPlayLength(), Hellhound_t::MaulAnimationSeconds),
			FMath::Abs(Maul->GetPlayLength()
					   - Hellhound_t::MaulAnimationSeconds) < 0.01f);

		TestTrue(TEXT("and it fits inside the interval between bites, so one "
					  "bite is not still playing when the next begins"),
			Maul->GetPlayLength()
				< Hellhound_t::DesignedAttackIntervalSeconds);
	}

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmHellhoundReturnsToARestingPoseAfterABite,
	"Cataclysm.Hellhound.ItReturnsToARestingPoseAfterABite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHellhoundReturnsToARestingPoseAfterABite::RunTest(const FString&)
{
	using namespace CataclysmHellhoundTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmHellhoundCharacter* Hellhound =
		SpawnHellhound(World, FVector::ZeroVector);
	if (!Hellhound)
	{
		AddError(TEXT("could not spawn a Hellhound"));
		return false;
	}

	const bool bDressed = Hellhound->ResolveBody(/*bIncludeAnimation=*/true);
	if (!bDressed || !Hellhound->IdleAnimation || !Hellhound->MaulAnimation)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Iggy and Scorch pack is not present, so there are "
				 "no clips to move between. Whether a bite gives the mesh back "
				 "was NOT checked on this machine."));
		return true;
	}

	// STANDING TO BEGIN WITH.
	Hellhound->UpdateLoopingAnimation();
	TestEqual(TEXT("it stands in its idle before anything happens"),
		Hellhound->CurrentLoopingAnimation.Get(),
		Hellhound->IdleAnimation.Get());

	// A BITE TAKES THE MESH AND RECORDS WHEN IT WILL GIVE IT BACK. Without that
	// the creature holds the last frame of the bite until the next one, which is
	// what a one-shot in single-node mode does on its own.
	Hellhound->AttackTarget(nullptr);

	TestEqual(TEXT("the bite is the clip that played"),
		Hellhound->LastPlayedAnimation.Get(), Hellhound->MaulAnimation.Get());
	TestTrue(TEXT("a bite records when it will finish"),
		Hellhound->OneShotEndsAtSeconds > World->GetTimeSeconds());
	TestNull(TEXT("and nothing is looping while it plays"),
		Hellhound->CurrentLoopingAnimation.Get());

	// WHILE IT IS STILL PLAYING, NOTHING TAKES THE MESH BACK.
	Hellhound->UpdateLoopingAnimation();
	TestNull(TEXT("the bite is left alone until it ends"),
		Hellhound->CurrentLoopingAnimation.Get());

	// AND ONCE IT HAS ENDED, THE RESTING POSE COMES BACK. The end time is moved
	// into the past rather than the world being ticked forward, which keeps this
	// instant and exercises the same branch.
	//
	// ONE STEP IS ENOUGH FOR THIS CREATURE, unlike the Abyssal Warden, whose
	// basic attack is three clips in a row. The bound is kept anyway so that a
	// bite which grew into a sequence fails here rather than hanging.
	constexpr int32 MostClipsABiteMayHave = 8;
	for (int32 Step = 0; Step < MostClipsABiteMayHave; ++Step)
	{
		if (Hellhound->CurrentLoopingAnimation)
		{
			break;
		}
		Hellhound->OneShotEndsAtSeconds = 0.0f;
		Hellhound->UpdateLoopingAnimation();
	}

	TestEqual(TEXT("it returns to its idle once the bite has finished"),
		Hellhound->CurrentLoopingAnimation.Get(),
		Hellhound->IdleAnimation.Get());

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmHellhoundPlaysItsChargeClipDuringTheTelegraph,
	"Cataclysm.Hellhound.ItPlaysItsChargeClipWhileTheLaneIsOnTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHellhoundPlaysItsChargeClipDuringTheTelegraph::RunTest(const FString&)
{
	using namespace CataclysmHellhoundTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmHellhoundCharacter* Hellhound =
		SpawnHellhound(World, FVector::ZeroVector);
	if (!Hellhound)
	{
		AddError(TEXT("could not spawn a Hellhound"));
		return false;
	}

	const bool bDressed = Hellhound->ResolveBody(/*bIncludeAnimation=*/true);
	if (!bDressed || !Hellhound->HellrushAnimation)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Iggy and Scorch pack is not present, so there is "
				 "no charge clip to play. Whether the wind-up plays one was NOT "
				 "checked on this machine."));
		return true;
	}

	// THE CLIP RUNS DURING THE WIND-UP, NOT DURING THE TRAVEL, and that reads
	// backwards at first. Every ability in this project plays its animation
	// across the telegraph and resolves at the end of it: the creature rears and
	// gathers itself while the lane is on the floor, then sets off.
	Hellhound->BeginEnemyAbilityWindUp(Hellhound_t::HellrushAbility, nullptr);

	TestEqual(TEXT("the wind-up plays the charge clip"),
		Hellhound->LastPlayedAnimation.Get(),
		Hellhound->HellrushAnimation.Get());

	// AND IT IS OVER BY THE TIME THE CREATURE SETS OFF. The clip is longer than
	// the telegraph, so PlayOneShot compresses it into the window rather than
	// letting it run past the moment the charge begins.
	const float EndsIn =
		Hellhound->OneShotEndsAtSeconds - World->GetTimeSeconds();

	TestTrue(FString::Printf(
			TEXT("it finishes in %.3f s, inside the %.2f s telegraph"),
			EndsIn, Hellhound_t::HellrushWindUpSeconds),
		EndsIn > 0.0f
		&& EndsIn <= Hellhound_t::HellrushWindUpSeconds + UE_KINDA_SMALL_NUMBER);

	// AND THE COMPRESSION IS INSIDE THE CEILING, so the clip is not being
	// silently clamped to something slower than the window it has to fit.
	if (const UAnimSequence* Charge = Hellhound->HellrushAnimation.Get())
	{
		const float NeededRate =
			Charge->GetPlayLength() / Hellhound_t::HellrushWindUpSeconds;
		TestTrue(FString::Printf(
				TEXT("the charge clip is %.4f s and needs a play rate of %.3f, "
					 "against a ceiling of %.1f"),
				Charge->GetPlayLength(), NeededRate,
				Hellhound_t::MaximumPlayRate),
			NeededRate <= Hellhound_t::MaximumPlayRate);
	}

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmHellhoundWalksRatherThanSlides,
	"Cataclysm.Hellhound.ItWalksRatherThanSlides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHellhoundWalksRatherThanSlides::RunTest(const FString&)
{
	using namespace CataclysmHellhoundTest;

	// THE ARITHMETIC THAT STOPS THE FOOT SLIDING, checked without a world.
	//
	// A planted foot travels backwards at the clip's authored speed while the
	// body travels forwards at the designed speed. Playing the clip at the ratio
	// of the two makes them cancel. `tools/measure_animation_stride.py` measured
	// Jog_Fwd at 302.6 cm/s on 2026-08-20 and the creature is designed at 750,
	// so the rate is 2.478.
	const float Expected = Hellhound_t::DesignedWalkSpeedCmPerSecond
						 / Hellhound_t::AuthoredJogSpeedCmPerSecond;

	TestEqual(TEXT("the walk plays at the ratio of designed to authored speed"),
		Hellhound_t::JogPlayRate(), Expected);

	// AND THE PRODUCT IS THE SPEED IT ACTUALLY MOVES AT, which is the thing that
	// matters and is not the same statement. If either figure were read from the
	// wrong place this would still pass the check above and fail here.
	TestEqual(TEXT("so the foot travels backwards at the body's own speed"),
		Hellhound_t::JogPlayRate() * Hellhound_t::AuthoredJogSpeedCmPerSecond,
		Hellhound_t::DesignedWalkSpeedCmPerSecond);

	// THE RATE IS INSIDE THE CLAMP, so it is not being silently corrected.
	//
	// **THIS IS THE TIGHTEST ANIMATION FIT IN THE PROJECT.** 2.478 against a
	// ceiling of 2.5 leaves 0.022, where the Abyssal Warden needs 0.994 and the
	// Brute 1.11 to walk. Nothing in the Paragon Iggy and Scorch pack is faster
	// -- Travelmode_Fwd measures 268.1 cm/s against Jog_Fwd's 302.6 -- so a
	// designed speed that rises even slightly has nowhere to go but an animation
	// Blueprint or a slower creature. This check is what says so out loud rather
	// than letting the clamp swallow it and the feet start sliding.
	TestTrue(FString::Printf(
			TEXT("the rate is %.4f, inside the %.1f ceiling by %.4f"),
			Hellhound_t::JogPlayRate(), Hellhound_t::MaximumPlayRate,
			Hellhound_t::MaximumPlayRate - Hellhound_t::JogPlayRate()),
		Hellhound_t::JogPlayRate() > Hellhound_t::MinimumPlayRate
		&& Hellhound_t::JogPlayRate() < Hellhound_t::MaximumPlayRate);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
